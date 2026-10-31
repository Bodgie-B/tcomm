// A stand-in for the upstairs panel, for testing the C6 gateway before the panels' own ESP-NOW is
// switched on. It runs on any ESP32 whose Wi-Fi MAC is a panel's in secrets.h (the first panel,
// 14:C1:9F:54:BE:20, is upstairs). It prints each frame from the gateway, decoded and in hex, and
// sends back the same ACK the panel firmware sends (Controller::on_radio).
//
// Type into its serial port:
//   ack      ACK everything (the default)
//   drop     ACK nothing, so the gateway retries and then reports "failed"
//   drop N   skip the ACKs for the next N frames (tries), then ACK again
//
// Build and upload: bash firmware/tools/fw.sh listener COMx (see gateway/docs/gateway-c6.md). Flashing it onto the
// panel replaces the panel firmware until bash firmware/tools/fw.sh puts it back; the SD card and the
// panel's saved settings are left alone.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>
#if __has_include("config/secrets.h")
#include "config/secrets.h"
#else
#include "config/secrets.example.h"  // a compile check only: firmware/tools/fw.sh won't upload this build
#endif
#include "core/protocol.h"

static_assert(sizeof(TCOMM_PMK) == 17, "TCOMM_PMK must be 16 characters");
static_assert(sizeof(TCOMM_LMK_GATEWAY) == 17, "TCOMM_LMK_GATEWAY must be 16 characters");

namespace {

const uint8_t kPanels[2][6] = {TCOMM_MAC_UPSTAIRS, TCOMM_MAC_DOWNSTAIRS};
const uint8_t kGateway[6] = TCOMM_MAC_GATEWAY;

struct RxFrame {
  bool from_gateway;
  uint8_t len;
  uint8_t data[tc::kMaxMsg];
};
QueueHandle_t g_rx = nullptr;
int g_me = -1;            // 0 upstairs, 1 downstairs
bool g_drop_all = false;
int g_drop_next = 0;
uint32_t g_frames = 0, g_acks = 0;

void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len <= 0 || len > (int)tc::kMaxMsg) return;
  RxFrame f;
  f.from_gateway = !memcmp(info->src_addr, kGateway, 6);
  f.len = (uint8_t)len;
  memcpy(f.data, data, len);
  xQueueSend(g_rx, &f, 0);
}

void print_mac(const char *label, const uint8_t *m) {
  Serial.printf("%s %02x:%02x:%02x:%02x:%02x:%02x\n", label, m[0], m[1], m[2], m[3], m[4], m[5]);
}

void print_hex(const uint8_t *d, size_t n) {
  Serial.print("   hex:");
  for (size_t i = 0; i < n; i++) Serial.printf(" %02X", d[i]);
  Serial.println();
}

void handle(const RxFrame &f) {
  g_frames++;
  if (!f.from_gateway) {
    Serial.printf("frame from another MAC (%u bytes), ignored\n", f.len);
    return;
  }
  if (f.len < 5 || f.data[2] != tc::kSenderGateway) {
    Serial.printf("frame from the gateway's MAC with sender byte %u, ignored (a panel drops it)\n",
                  f.len >= 3 ? f.data[2] : 255);
    return;
  }
  tc::Msg m;
  if (!tc::decode(f.data, f.len, m)) {
    Serial.printf("frame from the gateway didn't decode (%u bytes)\n", f.len);
    print_hex(f.data, f.len);
    return;
  }
  static uint16_t last_seq[2] = {0, 0};
  int k = m.type == tc::MsgType::Outside ? 0 : m.type == tc::MsgType::Lucy ? 1 : -1;
  const char *repeat = k >= 0 && last_seq[k] == m.seq ? " (a retry)" : "";
  if (k >= 0) last_seq[k] = m.seq;
  if (m.type == tc::MsgType::Outside) {
    const auto &o = m.u.outside;
    int t = o.temp_tenths_f < 0 ? -o.temp_tenths_f : o.temp_tenths_f;
    Serial.printf("OUTSIDE seq %u%s: %s%d.%d F, %u%%, forecast %u, flags %u, time %lu, \"%s\"\n", m.seq, repeat,
                  o.temp_tenths_f < 0 ? "-" : "", t / 10, t % 10, o.humidity, o.forecast, o.flags,
                  (unsigned long)o.time, o.phrase);
  } else if (m.type == tc::MsgType::Lucy) {
    Serial.printf("LUCY seq %u%s: %s, since %lu\n", m.seq, repeat, m.u.lucy.out ? "out" : "back in",
                  (unsigned long)m.u.lucy.since);
  } else {
    Serial.printf("type %u seq %u from the gateway (it only sends OUTSIDE and LUCY)\n", (unsigned)m.type, m.seq);
  }
  print_hex(f.data, f.len);

  if (g_drop_all || g_drop_next > 0) {
    if (g_drop_next > 0) g_drop_next--;
    Serial.println("   no ACK (dropping)");
    return;
  }
  // The panel's ACK, byte for byte: header seq 0, then the seq and type it answers.
  tc::Msg a;
  a.type = tc::MsgType::Ack;
  a.sender = (uint8_t)g_me;
  a.u.ack.seq = m.seq;
  a.u.ack.type = (uint8_t)m.type;
  uint8_t buf[16];
  size_t n = tc::encode(a, buf, sizeof(buf));
  esp_err_t e = n ? esp_now_send(kGateway, buf, n) : ESP_FAIL;
  if (e == ESP_OK) g_acks++;
  Serial.printf("   ACK %s:", e == ESP_OK ? "sent" : esp_err_to_name(e));
  for (size_t i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
  Serial.println();
}

void command(const char *s) {
  if (!strcmp(s, "ack")) {
    g_drop_all = false;
    g_drop_next = 0;
    Serial.println("ACKing everything");
  } else if (!strcmp(s, "drop")) {
    g_drop_all = true;
    Serial.println("ACKing nothing (type ack to undo)");
  } else if (!strncmp(s, "drop ", 5) && atoi(s + 5) > 0) {
    g_drop_all = false;
    g_drop_next = atoi(s + 5);
    Serial.printf("skipping the next %d ACKs\n", g_drop_next);
  } else if (s[0]) {
    Serial.printf("frames %lu, ACKs %lu. Commands: ack, drop, drop N\n", (unsigned long)g_frames,
                  (unsigned long)g_acks);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);  // time for the PC's serial monitor to catch the first lines
  uint8_t mine[6];
  esp_read_mac(mine, ESP_MAC_WIFI_STA);
  for (int i = 0; i < 2; i++)
    if (!memcmp(mine, kPanels[i], 6)) g_me = i;
  print_mac("listener: this board is", mine);
  if (g_me < 0) {
    Serial.println("listener: that MAC isn't TCOMM_MAC_UPSTAIRS or TCOMM_MAC_DOWNSTAIRS in secrets.h;");
    Serial.println("listener: the gateway ignores ACKs from it. Acting as upstairs anyway.");
    g_me = 0;
  }
  Serial.printf("listener: acting as the %s panel\n", g_me == 0 ? "upstairs" : "downstairs");
  print_mac("listener: gateway", kGateway);

  g_rx = xQueueCreate(16, sizeof(RxFrame));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);  // TCOMM_ESPNOW_CHANNEL
  if (esp_now_init() != ESP_OK) {
    Serial.println("listener: esp_now_init failed");
    return;
  }
  esp_now_set_pmk((const uint8_t *)TCOMM_PMK);
  esp_now_register_recv_cb(on_recv);
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, kGateway, 6);
  p.channel = 1;
  p.ifidx = WIFI_IF_STA;
  p.encrypt = true;
  memcpy(p.lmk, TCOMM_LMK_GATEWAY, 16);
  esp_err_t e = esp_now_add_peer(&p);
  Serial.printf("listener: %s, channel 1. Commands: ack, drop, drop N\n",
                e == ESP_OK ? "listening" : esp_err_to_name(e));
}

void loop() {
  RxFrame f;
  while (g_rx && xQueueReceive(g_rx, &f, 0) == pdTRUE) handle(f);
  static char line[32];
  static size_t n = 0;
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == '\r' || c == '\n') {
      line[n] = 0;
      command(line);
      n = 0;
    } else if (n < sizeof(line) - 1) {
      line[n++] = (char)c;
    }
  }
  delay(1);
}
