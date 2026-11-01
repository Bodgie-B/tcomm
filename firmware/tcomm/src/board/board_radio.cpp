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
#include <time.h>
#include "../config/secrets.h"
#include "../core/protocol.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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
static void on_sent(const uint8_t *, esp_now_send_status_t s) {
  if (s == ESP_NOW_SEND_SUCCESS) g_count.reached++;
  else g_count.missed++;
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

// ---- Internet time (Mike, 2026-10-31) ----
// A panel that has lost its clock (a power cut: no coin cell) joins the home Wi-Fi from secrets.h
// for a few seconds, asks an internet time server, and goes back to ESP-NOW's channel. The Pi's
// weather time was the first answer, but it ran 10 minutes slow, and the Pi went off the network.
// This brings in ESP-IDF's TCP/IP stack (not Arduino's Network library): about 150 KB more code,
// which these builds copy into PSRAM (measured with a probe, 2026-10-31). While it's joined, the
// other panel and the gateway can't reach this one unless the router is on channel 1 too; their
// messages are retried.
#ifndef TCOMM_WIFI_SSID
#define TCOMM_WIFI_SSID ""
#define TCOMM_WIFI_PASS ""
#endif
static esp_netif_t *g_sta = nullptr;  // only with a Wi-Fi name in secrets.h
enum class TimeSync : uint8_t { kIdle, kJoining, kAsking, kLeaving };
static TimeSync g_ts = TimeSync::kIdle;
static uint32_t g_ts_since = 0, g_ts_tries = 0, g_ts_set = 0;
static const char *g_ts_last = "not tried yet";
static volatile bool g_got_ip = false;

static void on_got_ip(void *, esp_event_base_t, int32_t, void *) { g_got_ip = true; }

bool time_sync_busy() { return g_ts != TimeSync::kIdle; }

bool time_sync_start() {
  if (!g_sta || g_ts != TimeSync::kIdle) return false;
  wifi_config_t wc = {};
  strncpy((char *)wc.sta.ssid, TCOMM_WIFI_SSID, sizeof(wc.sta.ssid));
  strncpy((char *)wc.sta.password, TCOMM_WIFI_PASS, sizeof(wc.sta.password));
  if (!check(esp_wifi_set_config(WIFI_IF_STA, &wc), "time: Wi-Fi settings")) return false;
  g_got_ip = false;
  if (!check(esp_wifi_connect(), "time: Wi-Fi connect")) return false;
  g_ts = TimeSync::kJoining;
  g_ts_since = millis();
  g_ts_tries++;
  elog("time: joining the Wi-Fi to ask for the time");
  return true;
}

static void time_sync_leave(const char *result) {
  if (g_ts == TimeSync::kAsking) esp_netif_sntp_deinit();
  esp_wifi_disconnect();
  g_ts = TimeSync::kLeaving;
  g_ts_since = millis();
  g_ts_last = result;
  elog("time: %s", result);
}

uint32_t time_sync_poll() {
  switch (g_ts) {
    case TimeSync::kIdle:
      return 0;
    case TimeSync::kJoining:
      if (g_got_ip) {
        esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&c) != ESP_OK) {
          time_sync_leave("couldn't start asking for the time");
          return 0;
        }
        g_ts = TimeSync::kAsking;
        g_ts_since = millis();
      } else if (millis() - g_ts_since > 15000) {
        time_sync_leave("no Wi-Fi connection");
      }
      return 0;
    case TimeSync::kAsking:
      if (esp_netif_sntp_sync_wait(0) == ESP_OK) {  // SNTP has set the system clock
        uint32_t utc = (uint32_t)time(nullptr);
        g_ts_set++;
        time_sync_leave("set from the internet");
        return utc;
      }
      if (millis() - g_ts_since > 10000) time_sync_leave("no answer from the time server");
      return 0;
    case TimeSync::kLeaving:
      // The channel can only change once the disconnect has gone through.
      if (esp_wifi_set_channel(TCOMM_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) == ESP_OK) {
        g_ts = TimeSync::kIdle;
      } else if (millis() - g_ts_since > 10000) {
        elog("time: couldn't get back to channel %d", TCOMM_ESPNOW_CHANNEL);
        g_ts = TimeSync::kIdle;
      }
      return 0;
  }
  return 0;
}

// Wi-Fi started with ESP-IDF's own calls, as its ESP-NOW example does, not Arduino's WiFi class:
// including WiFi.h links Arduino's whole Network library, and with it the TCP/IP stack, about
// 200 KB that this build copies into PSRAM with the rest of the code (2026-10-25). ESP-NOW uses
// none of it. Station mode, connected to the home Wi-Fi only for the internet time above.
bool radio_begin() {
  g_rx = xQueueCreate(16, sizeof(Frame));
  esp_err_t e = esp_event_loop_create_default();  // the Wi-Fi driver posts its events here
  if (e != ESP_ERR_INVALID_STATE) check(e, "event loop");  // one made already is fine
  if (TCOMM_WIFI_SSID[0]) {  // the network interface goes in before Wi-Fi starts, as ESP-IDF asks
    e = esp_netif_init();
    if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) g_sta = esp_netif_create_default_wifi_sta();
    if (g_sta) {
      esp_netif_set_hostname(g_sta, radio_own_floor() == 1 ? "tcomm-downstairs" : "tcomm-upstairs");
      check(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, nullptr), "time: IP event");
    }
  }
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
                                       "EVENT", "EVENT_SUM", "MAIL"};
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
  if (g_sta)
    Serial.printf("radio: internet time: %lu tries, %lu set; last: %s\n", (unsigned long)g_ts_tries,
                  (unsigned long)g_ts_set, g_ts_last);
  else
    Serial.println("radio: internet time off (no Wi-Fi name in secrets.h)");
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
bool radio_poll(uint8_t *, size_t *) { return false; }
void radio_report() { Serial.println("radio: off in this build (bash tools/fw.sh radio turns it on)"); }
bool time_sync_start() { return false; }
bool time_sync_busy() { return false; }
uint32_t time_sync_poll() { return 0; }
}  // namespace board

#endif
