// ESP-NOW transport (phase 3). With TCOMM_RADIO_ESPNOW off (phase 2) these are stubs and demo
// mode carries the messages instead. First tried against the C6 gateway, 2026-10-25, built with
// bash tools/fw.sh radio; not yet run on two panels.
#include "../config/tcomm_config.h"
#include "board.h"

#if TCOMM_RADIO_ESPNOW
#include <Arduino.h>
#include <esp_event.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "../config/secrets.h"
#include <atomic>
#include "../core/protocol.h"
#include "../core/voice_proto.h"
#include "../core/voice_tx.h"  // kReachPingTag
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ESP-NOW keys are exactly 16 bytes; a shorter literal would make memcpy read past it.
static_assert(sizeof(TCOMM_PMK) == 17, "TCOMM_PMK must be 16 characters");
static_assert(sizeof(TCOMM_LMK_PANELS) == 17, "TCOMM_LMK_PANELS must be 16 characters");
static_assert(sizeof(TCOMM_LMK_GATEWAY) == 17, "TCOMM_LMK_GATEWAY must be 16 characters");

namespace board {
static const uint8_t kMacs[3][6] = {TCOMM_MAC_UPSTAIRS, TCOMM_MAC_DOWNSTAIRS, TCOMM_MAC_GATEWAY};
static const uint8_t kNoMac[6] = {0, 0, 0, 0, 0, 0};  // a board not in secrets.h yet
struct Frame {
  uint8_t len;
  uint8_t data[tc::kMaxMsg];
};
static QueueHandle_t g_rx = nullptr;

// For "radio" over serial. Each count is written by one task only (the Wi-Fi task receives and
// reports deliveries, the LVGL task sends), so they need no lock.
static struct {
  uint32_t in, unknown, wrong_sender, bad_len, full;  // frames received, and dropped by reason
  uint32_t out, send_err, unset;                      // frames sent, refused, to a board not set
  uint32_t reached, missed;                           // sends a radio acknowledged, or didn't
} g_count;
static uint8_t g_last_unknown[6];

// Voice frames (types 20-30; docs/voice-notes/DESIGN.md 4.6, 4.7): never traced, so a 100-chunk upload
// (or upstairs relaying one, 34 frames a second each way) never wipes the doorbell's trace ring, and no
// elog per error. Sent from the mic task, the vrelay task and the LVGL task, so those counts are atomic;
// the Wi-Fi task alone writes the others.
static struct {
  std::atomic<uint32_t> out{0}, err{0}, unset{0};      // radio_send_voice: taken, refused, to a board not set
  uint32_t in = 0, full = 0;                           // received for this panel, dropped (queue full)
  uint32_t relay_in = 0, relay_full = 0;               // upstairs: queued for the vrelay task, or dropped
  std::atomic<uint32_t> relay_out{0}, relay_err{0};    // passed on, or refused by the radio
  std::atomic<uint32_t> capped{0};                     // refused by the air cap below
} g_voice;
// Field fix 2026-11-20: whatever goes wrong above it (a resend loop, a relay loop, a bug), this panel
// never puts more than tc::kVoicePanelPerS voice frames a second on the air (bursts of kVoicePanelBurst),
// so the TComm frames and their ACKs always find room. A refused frame is like a radio refusal.
static tc::VoiceRateCap g_vcap(tc::kVoicePanelPerS, tc::kVoicePanelBurst);
static portMUX_TYPE g_vcap_lock = portMUX_INITIALIZER_UNLOCKED;
// Upstairs only: frames downstairs and the gateway send each other through this panel. 32 frames, the
// storage in PSRAM (about 8 KB), drained by the "vrelay" task (core 0, priority 3) with no LVGL in the
// path, so a long redraw never drops a relayed frame.
struct RelayFrame {
  uint8_t to, len;
  uint8_t data[tc::kMaxMsg];
};
static constexpr int kRelayFrames = 32;
static QueueHandle_t g_relay = nullptr;
static StaticQueue_t g_relay_q;
// The vrelay task's stack (2560 bytes) is in PSRAM too, so the task costs internal RAM only its TCB
// (field fix 2026-11-20: it was 2.5 KB of internal heap on upstairs, the panel that lost its ACKs).
// Safe there: this build runs all its code from PSRAM already (CONFIG_SPIRAM_FETCH_INSTRUCTIONS), the
// sdkconfig allows task stacks in PSRAM (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM), and the task only
// takes a queue, a spinlock and esp_now_send: it never writes flash, so it never runs with the cache off.
static constexpr uint32_t kRelayStack = 2560;
static StaticTask_t g_vrelay_tcb;
// The reach test (DESIGN 13.0, piece R): while it runs, the send reports for frames to the gateway, and
// the VOICE_GOT kind 30 answers to its own pings, are counted here.
static volatile bool g_reach_on = false;
static std::atomic<uint32_t> g_reach_ok{0}, g_reach_missed{0}, g_reach_got{0};
// The last frames in and out (not the ones to a board that isn't set).
enum TraceResult : int8_t { kOk, kUnknownMac, kWrongSender, kBadLen, kQueueFull, kSendErr };
struct Trace {
  uint32_t ms;
  bool out;
  int8_t who;  // 0 upstairs, 1 downstairs, 2 gateway, -1 unknown
  int8_t result;
  uint8_t type, len;
  uint16_t seq, ack_seq;
};
static constexpr int kTraces = 24;
static Trace g_trace[kTraces];
static uint32_t g_traces = 0;  // written so far
static portMUX_TYPE g_trace_lock = portMUX_INITIALIZER_UNLOCKED;

static void trace(bool out, int who, const uint8_t *d, int len, TraceResult r) {
  Trace t = {};
  t.ms = millis();
  t.out = out;
  t.who = (int8_t)who;
  t.result = r;
  t.len = (uint8_t)(len > 255 ? 255 : len < 0 ? 0 : len);
  if (len >= 5) {
    t.type = d[1];
    t.seq = (uint16_t)(d[3] | d[4] << 8);
  }
  if (len >= 8 && d[1] == (uint8_t)tc::MsgType::Ack) t.ack_seq = (uint16_t)(d[5] | d[6] << 8);
  portENTER_CRITICAL(&g_trace_lock);
  g_trace[g_traces % kTraces] = t;
  g_traces++;
  portEXIT_CRITICAL(&g_trace_lock);
}

static int sender_of(const uint8_t *mac) {
  for (int i = 0; i < 3; i++)
    if (memcmp(kMacs[i], kNoMac, 6) && memcmp(mac, kMacs[i], 6) == 0) return i;
  return -1;
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  // Only the three known devices; and the header's sender byte must match the MAC it came from.
  int who = sender_of(info->src_addr);
  TraceResult r = kOk;
  if (who < 0) {
    r = kUnknownMac;
    g_count.unknown++;
    memcpy(g_last_unknown, info->src_addr, 6);
  } else if (len < 5 || len > (int)tc::kMaxMsg) {
    r = kBadLen;
    g_count.bad_len++;
  } else if (data[2] != (uint8_t)who) {
    r = kWrongSender;
    g_count.wrong_sender++;
  } else if (tc::is_voice_frame(data, (size_t)len)) {
    // Not traced (check fix C7). On upstairs, what tc::voice_relay_frame says to pass on goes to the
    // vrelay task and never reaches the controller.
    if (g_relay) {
      int to = tc::voice_relay_frame(data, (size_t)len, (uint8_t)who, tc::kSenderUpstairs);
      if (to >= 0) {
        RelayFrame rf;
        rf.to = (uint8_t)to;
        rf.len = (uint8_t)len;
        memcpy(rf.data, data, len);
        if (xQueueSend(g_relay, &rf, 0) == pdTRUE) g_voice.relay_in++;  // the Wi-Fi task must never block
        else g_voice.relay_full++;
        return;
      }
    }
    if (g_reach_on && who == tc::kSenderGateway && len == (int)tc::kVoiceGotLen && data[1] == (uint8_t)tc::VType::Got &&
        data[tc::kVoiceHeader] == (uint8_t)tc::VType::Ping) {
      uint32_t key = (uint32_t)data[7] | (uint32_t)data[8] << 8 | (uint32_t)data[9] << 16 | (uint32_t)data[10] << 24;
      if ((key >> 16) == tc::kReachPingTag) g_reach_got++;  // still queued: the controller ignores its nonce
    }
    Frame f;
    f.len = (uint8_t)len;
    memcpy(f.data, data, len);
    if (xQueueSend(g_rx, &f, 0) == pdTRUE) g_voice.in++;
    else g_voice.full++;
    return;
  } else {
    Frame f;
    f.len = (uint8_t)len;
    memcpy(f.data, data, len);
    if (xQueueSend(g_rx, &f, 0) == pdTRUE) g_count.in++;  // the WiFi task must never block
    else {
      r = kQueueFull;
      g_count.full++;
    }
  }
  trace(false, who, data, len, r);
}

// Whether a radio acknowledged the frame. The receiver's hardware does that before decrypting, so
// a board with other keys still counts as reached; its TComm ACK is what says it took the frame.
static void on_sent(const uint8_t *mac, esp_now_send_status_t s) {
  if (s == ESP_NOW_SEND_SUCCESS) g_count.reached++;
  else g_count.missed++;
  if (g_reach_on && mac && !memcmp(mac, kMacs[2], 6)) {
    if (s == ESP_NOW_SEND_SUCCESS) g_reach_ok++;
    else g_reach_missed++;
  }
}

static void vrelay_task(void *) {
  RelayFrame f;
  for (;;) {
    if (xQueueReceive(g_relay, &f, portMAX_DELAY) != pdTRUE) continue;
    tc::voice_set_sender(f.data, tc::kSenderUpstairs);  // the header's sender is always this hop's device
    if (radio_send_voice(f.to, f.data, f.len)) g_voice.relay_out++;
    else g_voice.relay_err++;
  }
}

int radio_own_floor() {
  uint8_t mine[6];
  if (esp_read_mac(mine, ESP_MAC_WIFI_STA) != ESP_OK) return -1;
  for (int i = 0; i < 2; i++)
    if (memcmp(mine, kMacs[i], 6) == 0) return i;
  return -1;
}

static bool check(esp_err_t e, const char *what) {
  if (e != ESP_OK) elog("radio: %s failed: %s", what, esp_err_to_name(e));
  return e == ESP_OK;
}

// Wi-Fi started with ESP-IDF's own calls, as its ESP-NOW example does, not Arduino's WiFi class:
// including WiFi.h links Arduino's whole Network library, and with it the TCP/IP stack, about
// 200 KB that this build copies into PSRAM with the rest of the code (2026-10-25). ESP-NOW uses
// none of it. The panel never joins a Wi-Fi network: the clock comes from the WeatherGate's
// OUTSIDE message (Mike, 2026-11-07), so the station join and SNTP are no longer linked in.
// Station mode is only there so ESP-NOW has an interface and a channel.
bool radio_begin() {
  g_rx = xQueueCreate(16, sizeof(Frame));
  esp_err_t e = esp_event_loop_create_default();  // the Wi-Fi driver posts its events here
  if (e != ESP_ERR_INVALID_STATE) check(e, "event loop");  // one made already is fine
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.nvs_enable = 0;  // no Wi-Fi settings to keep, so nothing written to flash
  if (!check(esp_wifi_init(&cfg), "wifi init")) return false;
  bool ok = check(esp_wifi_set_storage(WIFI_STORAGE_RAM), "wifi storage");
  ok &= check(esp_wifi_set_mode(WIFI_MODE_STA), "wifi mode");
  if (!check(esp_wifi_start(), "wifi start")) return false;
  ok &= check(esp_wifi_set_channel(TCOMM_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE), "set channel");
  if (!check(esp_now_init(), "esp_now_init")) return false;
  ok &= check(esp_now_set_pmk((const uint8_t *)TCOMM_PMK), "set PMK");
  ok &= check(esp_now_register_recv_cb(on_recv), "register receive");
  ok &= check(esp_now_register_send_cb(on_sent), "register send");
  int me = radio_own_floor();
  if (me < 0) elog("radio: this board's MAC isn't in secrets.h");
  for (int i = 0; i < 3; i++) {
    if (i == me || !memcmp(kMacs[i], kNoMac, 6)) continue;  // the second panel may not be set yet
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, kMacs[i], 6);
    p.channel = TCOMM_ESPNOW_CHANNEL;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = true;  // HANDOFF: ESP-NOW's built-in CCMP
    memcpy(p.lmk, i == 2 ? TCOMM_LMK_GATEWAY : TCOMM_LMK_PANELS, 16);
    ok &= check(esp_now_add_peer(&p), "add peer");
  }
  // Upstairs relays downstairs' voice frames to the gateway and back (DESIGN 4.7). Only upstairs needs
  // the queue, so the downstairs panel keeps its 8 KB of PSRAM.
  if (me == tc::kSenderUpstairs && !g_relay) {
    // Both blocks before the queue is published: on_recv is registered already and reads g_relay, so
    // g_relay is never set and then taken back (a frame could reach xQueueSend with a null queue), and a
    // failure leaves nothing allocated (review fix 2026-11-20).
    void *store = heap_caps_malloc(kRelayFrames * sizeof(RelayFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *stack = heap_caps_malloc(kRelayStack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (store && stack) g_relay = xQueueCreateStatic(kRelayFrames, sizeof(RelayFrame), (uint8_t *)store, &g_relay_q);
    if (!g_relay) {
      elog("radio: no voice relay (no memory)");  // never queued for: the controller ignores the frames
      if (store) heap_caps_free(store);
      if (stack) heap_caps_free(stack);
    } else if (!xTaskCreateStaticPinnedToCore(vrelay_task, "vrelay", kRelayStack, nullptr, 3, (StackType_t *)stack,
                                              &g_vrelay_tcb, 0)) {
      elog("radio: no voice relay task");  // not with both buffers given; the queue stays (frames: full)
    }
  }
  return ok;
}

void radio_send(uint8_t to, const uint8_t *data, size_t len) {
  if (to > 2) return;
  if (!memcmp(kMacs[to], kNoMac, 6)) {  // counted, but not traced: retries would fill the trace
    g_count.unset++;
    return;
  }
  esp_err_t e = esp_now_send(kMacs[to], data, len);
  if (e == ESP_OK) g_count.out++;
  else g_count.send_err++;
  trace(true, to, data, (int)len, e == ESP_OK ? kOk : kSendErr);
  if (e != ESP_OK) elog("radio: send to %u failed: %s", to, esp_err_to_name(e));
}

bool radio_send_voice(uint8_t to, const uint8_t *data, size_t len) {
  if (to > 2 || !data || !len || len > tc::kMaxMsg) return false;
  if (!memcmp(kMacs[to], kNoMac, 6)) {
    g_voice.unset++;
    return false;
  }
  portENTER_CRITICAL(&g_vcap_lock);
  const bool room = g_vcap.take(millis());
  portEXIT_CRITICAL(&g_vcap_lock);
  if (!room) {
    g_voice.capped++;
    return false;
  }
  if (esp_now_send(kMacs[to], data, len) == ESP_OK) {
    g_voice.out++;
    return true;
  }
  g_voice.err++;  // ESP_ERR_ESPNOW_NO_MEM and the like: the resend logic picks the chunk up
  return false;
}

// Serial `reach N [ms]` and `reach ping N [ms]` (DESIGN 13.0, piece R; declared in tcomm.ino, since
// board.h is frozen). From the loop task, outside the LVGL lock: it blocks the loop for N x ms.
//  - reach: N frames of type 31 (kVoiceReachType: nothing decodes it, so nothing acts on it), 249 bytes
//    each, straight to the gateway, and how many a radio acknowledged. MAC-level only: the receiver's
//    hardware acknowledges before decrypting, so this is not proof the C6 takes this panel's frames.
//  - reach ping: N VOICE_PINGs (nonce = kReachPingTag << 16 | try), and how many VOICE_GOT kind 30 came
//    back end to end (needs the C6 0.5; the controller ignores these nonces).
// The send reports of anything else this panel sends to the gateway meanwhile are counted too (a
// NOTICE now and then, rare).
void radio_reach(int n, int gap_ms, bool ping) {
  if (n < 1) n = 1;
  if (n > 2000) n = 2000;
  if (gap_ms < 5) gap_ms = 5;
  if (gap_ms > 1000) gap_ms = 1000;
  int me = radio_own_floor();
  if (me < 0 || !memcmp(kMacs[2], kNoMac, 6)) {
    Serial.println("reach: this board or the gateway is not in secrets.h");
    return;
  }
  g_reach_ok = 0;
  g_reach_missed = 0;
  g_reach_got = 0;
  g_reach_on = true;
  uint32_t refused = 0, t0 = millis();
  size_t flen = 0;
  uint8_t buf[tc::kMaxMsg];
  for (int i = 0; i < n; i++) {
    if (ping) {
      tc::VoiceMsg m;
      m.type = tc::VType::Ping;
      m.sender = (uint8_t)me;
      m.floor = (uint8_t)me;
      m.seq = (uint16_t)i;
      m.u.ping.nonce = ((uint32_t)tc::kReachPingTag << 16) | (uint16_t)(i + 1);
      flen = tc::voice_encode(m, buf, sizeof(buf));
    } else {
      flen = 249;
      for (size_t k = 0; k < flen; k++) buf[k] = (uint8_t)(k * 7 + i);
      buf[0] = tc::kProtoVersion;
      buf[1] = tc::kVoiceReachType;
      buf[2] = (uint8_t)me;
      buf[3] = (uint8_t)i;
      buf[4] = (uint8_t)(i >> 8);
      buf[5] = (uint8_t)me;
    }
    if (!flen || esp_now_send(kMacs[2], buf, flen) != ESP_OK) refused++;
    delay(gap_ms);
  }
  delay(ping ? 400 : 200);  // the last send reports, and the last answers
  g_reach_on = false;
  uint32_t ok = g_reach_ok, missed = g_reach_missed, got = g_reach_got;
  Serial.printf("reach: %d %s of %u bytes to the gateway, one every %d ms (%lu ms): a radio acknowledged %lu (%.1f %%), "
                "none did for %lu, %lu refused here\n",
                n, ping ? "VOICE_PINGs" : "frames", (unsigned)flen, gap_ms, (unsigned long)(millis() - t0), (unsigned long)ok,
                100.0 * ok / n, (unsigned long)missed, (unsigned long)refused);
  if (ping)
    Serial.printf("reach: VOICE_GOT kind 30 back end to end: %lu of %d (%.1f %%)\n", (unsigned long)got, n, 100.0 * got / n);
  Serial.println(ping ? "reach: direct is the default only with 97 % or more both here and in plain reach runs (DESIGN 13.0)"
                      : "reach: MAC level only; 97 % or more on every run, then reach ping and the C6 hello's down_peer (DESIGN 13.0)");
}

bool radio_poll(uint8_t *out, size_t *len) {
  Frame f;
  if (!g_rx || xQueueReceive(g_rx, &f, 0) != pdTRUE) return false;
  memcpy(out, f.data, f.len);
  *len = f.len;
  return true;
}

static const char *who_word(int who) {
  return who == 0 ? "upstairs" : who == 1 ? "downstairs" : who == 2 ? "gateway" : "unknown MAC";
}

static const char *type_word(uint8_t t) {
  static const char *const kNames[] = {"?", "STATE", "KNOCK", "REPLY", "ACK", "DOG", "READING", "LUCY", "OUTSIDE",
                                       "EVENT", "EVENT_SUM", "MAIL", "COMFORT", "REQUESTS", "COMFORT_DEL", "UPDATE", "NOTE"};
  return t < sizeof(kNames) / sizeof(kNames[0]) ? kNames[t] : "?";
}

static void print_mac(const char *label, const uint8_t *m) {
  if (!memcmp(m, kNoMac, 6)) Serial.printf(" %s not set", label);
  else Serial.printf(" %s %02x:%02x:%02x:%02x:%02x:%02x", label, m[0], m[1], m[2], m[3], m[4], m[5]);
}

void radio_report() {
  uint8_t mine[6] = {};
  esp_read_mac(mine, ESP_MAC_WIFI_STA);
  uint8_t ch = 0;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&ch, &second);
  int me = radio_own_floor();
  Serial.printf("radio: on, channel %u, this board is %s;", ch, me < 0 ? "NOT in secrets.h" : who_word(me));
  print_mac("upstairs", kMacs[0]);
  print_mac("downstairs", kMacs[1]);
  print_mac("gateway", kMacs[2]);
  Serial.println();
  Serial.printf("radio: in %lu; dropped %lu unknown MAC, %lu wrong sender byte, %lu bad length, %lu queue full\n",
                (unsigned long)g_count.in, (unsigned long)g_count.unknown, (unsigned long)g_count.wrong_sender,
                (unsigned long)g_count.bad_len, (unsigned long)g_count.full);
  if (g_count.unknown) {
    Serial.print("radio: last unknown MAC");
    print_mac("", g_last_unknown);
    Serial.println();
  }
  Serial.printf("radio: out %lu (a radio acknowledged %lu, none did for %lu), %lu refused, %lu for a board not set\n",
                (unsigned long)g_count.out, (unsigned long)g_count.reached, (unsigned long)g_count.missed,
                (unsigned long)g_count.send_err, (unsigned long)g_count.unset);
  Serial.printf("radio: voice (not traced) in %lu, %lu dropped queue full; out %lu, %lu refused, %lu for a board not set, "
                "%lu over the air cap\n",
                (unsigned long)g_voice.in, (unsigned long)g_voice.full, (unsigned long)g_voice.out, (unsigned long)g_voice.err,
                (unsigned long)g_voice.unset, (unsigned long)g_voice.capped);
  if (g_relay)
    Serial.printf("radio: voice relayed for downstairs: %lu in, %lu out, %lu dropped queue full, %lu refused\n",
                  (unsigned long)g_voice.relay_in, (unsigned long)g_voice.relay_out, (unsigned long)g_voice.relay_full,
                  (unsigned long)g_voice.relay_err);
  Trace t[kTraces];
  uint32_t n;
  portENTER_CRITICAL(&g_trace_lock);
  memcpy(t, g_trace, sizeof(t));
  n = g_traces;
  portEXIT_CRITICAL(&g_trace_lock);
  uint32_t first = n > kTraces ? n - kTraces : 0, now = millis();
  static const char *const kWhy[] = {"", " DROPPED: unknown MAC", " DROPPED: wrong sender byte", " DROPPED: bad length",
                                     " DROPPED: queue full", " SEND REFUSED"};
  for (uint32_t i = first; i < n; i++) {
    const Trace &e = t[i % kTraces];
    Serial.printf("  %7.1f s ago  %s %-11s %-9s seq %5u", (now - e.ms) / 1000.0f, e.out ? "out to " : "in from",
                  who_word(e.who), type_word(e.type), e.seq);
    if (e.type == (uint8_t)tc::MsgType::Ack) Serial.printf(" for seq %u", e.ack_seq);
    Serial.printf(" (%u bytes)%s\n", e.len, kWhy[e.result]);
  }
}

}  // namespace board

#else

#include <Arduino.h>

namespace board {
int radio_own_floor() { return -1; }
bool radio_begin() { return false; }
void radio_send(uint8_t, const uint8_t *, size_t) {}
bool radio_send_voice(uint8_t, const uint8_t *, size_t) { return false; }
void radio_reach(int, int, bool) { Serial.println("reach: radio off in this build"); }
bool radio_poll(uint8_t *, size_t *) { return false; }
void radio_report() { Serial.println("radio: off in this build (bash tools/fw.sh radio turns it on)"); }
}  // namespace board

#endif
