// TComm gateway on the Waveshare ESP32-C6-GEEK. The Pi's bridge (WeatherGate and LucyGate) sends
// JSON lines over the USB port; the gateway sends each one to the upstairs panel over ESP-NOW,
// waits for its ACK, and answers the Pi once per request. The logic is in src/gw (tested on the
// PC by gateway/tests/test_gateway.cpp); this file is the board: the USB serial port, the radio, the LCD.
//
// Build and upload: bash firmware/tools/fw.sh gateway COMx (see gateway/docs/gateway-c6.md). Keys and MACs come from TComm's
// own firmware/tcomm/src/config/secrets.h, so the panels and the gateway can't disagree.
// Never log the keys or show them on the screen.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <hal/usb_serial_jtag_ll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "src/gw/gateway.h"
#include "src/lcd/lcd.h"

#if __has_include("config/secrets.h")
#include "config/secrets.h"
#define GW_SECRETS 1
#else
// A compile check without the keys. firmware/tools/fw.sh won't upload this build; if it were flashed, the
// screen says so and nothing is sent.
#include "config/secrets.example.h"
#define GW_SECRETS 0
#endif

// ESP-NOW keys are exactly 16 bytes; a shorter literal would make memcpy read past it.
static_assert(sizeof(TCOMM_PMK) == 17, "TCOMM_PMK must be 16 characters");
static_assert(sizeof(TCOMM_LMK_GATEWAY) == 17, "TCOMM_LMK_GATEWAY must be 16 characters");

namespace {

const uint8_t kPanels[2][6] = {TCOMM_MAC_UPSTAIRS, TCOMM_MAC_DOWNSTAIRS};  // by sender byte
const uint8_t kGatewayMac[6] = TCOMM_MAC_GATEWAY;
const uint8_t kNoMac[6] = {0, 0, 0, 0, 0, 0};

struct RxFrame {
  int8_t from;  // gw::kUpstairs, gw::kDownstairs, or -1
  uint8_t len;
  uint8_t data[tc::kMaxMsg];
};
QueueHandle_t g_rx = nullptr;
bool g_radio = false;     // ESP-NOW is up and the upstairs panel is a peer
// Tries a radio acknowledged, or didn't, since the last failure note (both types together). The
// panel's hardware acknowledges before it decrypts, so "reached" with no TComm ACK means a panel
// heard the frame but didn't take it.
volatile uint32_t g_reached = 0, g_missed = 0;
uint8_t g_mac[6];
char g_mac_text[18];
// For finish_usb_tx (below): the USB port's send buffer, and its transfers.
int g_tx_empty = 0;   // availableForWrite() with nothing waiting
uint32_t g_wrote = 0;  // when the port was last written to; 0 once that transfer is ended
void wrote_usb() { g_wrote = millis() | 1; }

// Short plain-text notes for a person watching the port. Never starts with "{", so the bridge
// can't mistake one for a reply; kept to start-up problems and the odd radio error.
void note(const char *fmt, ...) {
  char line[120] = "gw: ";
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + 4, sizeof(line) - 5, fmt, ap);
  va_end(ap);
  size_t n = strlen(line);
  line[n++] = '\n';
  Serial.write((const uint8_t *)line, n);
  wrote_usb();
}

int role_of(const uint8_t *mac) {
  for (int i = 0; i < 2; i++)
    if (memcmp(kPanels[i], kNoMac, 6) && !memcmp(mac, kPanels[i], 6)) return i;
  return -1;
}

void on_sent(const uint8_t *, esp_now_send_status_t s) {
  if (s == ESP_NOW_SEND_SUCCESS) g_reached = g_reached + 1;
  else g_missed = g_missed + 1;
}

void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len <= 0 || len > (int)tc::kMaxMsg) return;
  RxFrame f;
  f.from = (int8_t)role_of(info->src_addr);
  f.len = (uint8_t)len;
  memcpy(f.data, data, len);
  xQueueSend(g_rx, &f, 0);  // the Wi-Fi task must never wait
}

// After a "failed" reply, one note on what the radio saw, which tells the likely causes apart.
void explain_failure() {
  uint32_t ok = g_reached, no = g_missed;
  g_reached = 0;
  g_missed = 0;
  if (!g_radio) note("no ACK: the radio isn't running (see the line above, or the screen)");
  else if (ok) note("no ACK, though a radio took %lu of %lu tries: panel radio off in its build, or other keys?",
                    (unsigned long)ok, (unsigned long)(ok + no));
  else note("no ACK, and no radio took any of %lu tries: panel off, too far, or on another channel?",
            (unsigned long)no);
}

class BoardIO : public gw::GatewayIO {
 public:
  uint32_t now_ms() override { return millis(); }

  void send_frame(const uint8_t *data, size_t len) override {
    if (!g_radio) return;  // the request still gets its "failed" reply after 1.5 s
    esp_err_t e = esp_now_send(kPanels[gw::kUpstairs], data, len);
    if (e != ESP_OK && millis() - last_err_ >= 60000) {  // at most one note a minute
      last_err_ = millis();
      note("send failed: %s", esp_err_to_name(e));
    }
  }

  void reply(const char *line) override {
    // One write per line, so a line never goes out in pieces with something else between.
    char buf[gw::kMaxLine + 2];
    size_t n = strnlen(line, gw::kMaxLine);
    memcpy(buf, line, n);
    buf[n++] = '\n';
    Serial.write((const uint8_t *)buf, n);
    wrote_usb();
    if (strstr(line, "\"failed\":")) explain_failure();
  }

 private:
  uint32_t last_err_ = 0;
};

BoardIO g_io;
gw::Gateway *g_gw = nullptr;

bool check(esp_err_t e, const char *what, char *problem, size_t cap) {
  if (e != ESP_OK && !problem[0]) snprintf(problem, cap, "%s: %s", what, esp_err_to_name(e));
  return e == ESP_OK;
}

// Station mode on channel 1; never joins Wi-Fi. The upstairs panel (and the downstairs one once
// its MAC is in secrets.h) are encrypted peers with the gateway's link key.
bool radio_begin(char *problem, size_t cap) {
  g_rx = xQueueCreate(16, sizeof(RxFrame));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);  // on USB power: keep the receiver on
  bool ok = check(esp_wifi_set_channel(gw::kChannel, WIFI_SECOND_CHAN_NONE), "set channel", problem, cap);
  if (!check(esp_now_init(), "esp_now_init", problem, cap)) return false;
  ok &= check(esp_now_set_pmk((const uint8_t *)TCOMM_PMK), "set PMK", problem, cap);
  ok &= check(esp_now_register_recv_cb(on_recv), "receive callback", problem, cap);
  ok &= check(esp_now_register_send_cb(on_sent), "send callback", problem, cap);
  for (int i = 0; i < 2; i++) {
    if (!memcmp(kPanels[i], kNoMac, 6)) continue;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, kPanels[i], 6);
    p.channel = gw::kChannel;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = true;
    memcpy(p.lmk, TCOMM_LMK_GATEWAY, 16);
    ok &= check(esp_now_add_peer(&p), i == gw::kUpstairs ? "add upstairs" : "add downstairs", problem, cap);
  }
  if (!memcmp(kPanels[gw::kUpstairs], kNoMac, 6)) {
    if (!problem[0]) snprintf(problem, cap, "upstairs MAC not set");
    return false;
  }
  return ok;
}

// ---- The screen: what the gateway is, and what it's doing. Redrawn only when something changes.

char g_problem[48] = "";

struct Shown {
  bool pi = false;
  uint32_t requests = UINT32_MAX, acked = 0, failed = 0, rejected = 0;
  char last[40] = "";
};
Shown g_shown;

void lcd_row(int y, const char *s, int scale, uint16_t fg) {
  char padded[41];
  int cols = lcd::kW / (6 * scale);
  snprintf(padded, sizeof(padded), "%-*.*s", cols, cols, s);  // spaces wipe what was there
  lcd::text(0, y, padded, scale, fg, lcd::kBlack);
}

// Seven rows, all in the 12 x 14 size (20 characters across): the 6 x 7 size was too small to read
// on this screen (Mike read BAD as BRD, 2026-10-25).
int row_y(int row) { return 2 + 19 * row; }

void lcd_static() {
  char s[40];
  lcd_row(row_y(0), "TCOMM GATEWAY", 2, lcd::kWhite);
  lcd_row(row_y(1), g_mac_text, 2, lcd::kWhite);
  snprintf(s, sizeof(s), "CH %d %s", gw::kChannel, gw::kFirmware);
  lcd_row(row_y(2), s, 2, lcd::kGrey);
  // The full text of a problem goes to the serial port; the screen shows what fits.
  if (g_problem[0]) lcd_row(row_y(6), g_problem, 2, lcd::kRed);
  else if (memcmp(g_mac, kGatewayMac, 6)) lcd_row(row_y(6), "MAC NOT IN SECRETS.H", 2, lcd::kYellow);
}

void lcd_update() {
  const gw::GwStatus &st = g_gw->status();
  if (st.pi_seen == g_shown.pi && st.requests == g_shown.requests && st.acked == g_shown.acked &&
      st.failed == g_shown.failed && st.rejected == g_shown.rejected && !strcmp(st.last, g_shown.last))
    return;
  char s[48];
  lcd_row(row_y(3), st.pi_seen ? "PI: TALKING" : "PI: WAITING", 2, st.pi_seen ? lcd::kGreen : lcd::kYellow);
  // ACKed and failed; rejected requests (a bridge problem) only once there are some.
  int n = snprintf(s, sizeof(s), "OK %lu FAIL %lu", (unsigned long)st.acked, (unsigned long)st.failed);
  if (st.rejected && n > 0 && n < (int)sizeof(s))
    snprintf(s + n, sizeof(s) - n, " BAD %lu", (unsigned long)st.rejected);
  lcd_row(row_y(4), s, 2, lcd::kWhite);
  uint16_t c = strstr(st.last, "failed") ? lcd::kRed : strstr(st.last, "rejected") ? lcd::kYellow
               : strstr(st.last, "acked")  ? lcd::kGreen : lcd::kWhite;
  lcd_row(row_y(5), st.last, 2, c);
  g_shown.pi = st.pi_seen;
  g_shown.requests = st.requests;
  g_shown.acked = st.acked;
  g_shown.failed = st.failed;
  g_shown.rejected = st.rejected;
  snprintf(g_shown.last, sizeof(g_shown.last), "%s", st.last);
}

// ---- The USB serial port: one JSON object per line from the Pi.

char g_line[gw::kMaxLine + 1];
size_t g_len = 0;
bool g_too_long = false;

void read_serial() {
  for (int budget = 512; budget > 0 && Serial.available() > 0; budget--) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;  // a bare CR can't be in valid JSON; CRLF endings are fine
    if (c != '\n') {
      if (g_len < gw::kMaxLine) g_line[g_len++] = (char)c;
      else g_too_long = true;
      continue;
    }
    g_line[g_len] = 0;
    if (g_too_long) g_gw->on_line(g_line, gw::kMaxLine + 1);  // over 512 bytes: bad_json, dropped
    else if (g_len) g_gw->on_line(g_line, g_len);             // blank lines are ignored
    g_len = 0;
    g_too_long = false;
  }
}

// Core 3.2.0's USB serial (HWCDC) leaves out the end of a transfer. Data that ends on a full
// 64-byte packet isn't finished as far as the computer is concerned: Windows and Linux both hold
// it until a shorter packet comes. On 2026-10-25 every Lucy "acked" reply with a 2-digit ms was
// exactly 64 bytes, and each came out only with the reply to the laptop's next line, seconds
// later. ESP-IDF's own driver ends every transfer with a zero-length packet; HWCDC doesn't, so
// this does, once the ring buffer is empty and the last packet has gone. It also wakes the send
// interrupt if data has waited 5 ms in the buffer. (HWCDC's flush() is no help: it throws the
// data away when it thinks the host has gone.)
void finish_usb_tx() {
  if (!g_wrote) return;
  uint32_t now = millis();
  if (Serial.availableForWrite() < g_tx_empty) {  // still in the ring buffer
    if (now - g_wrote >= 5 && HWCDC::isPlugged()) {
      usb_serial_jtag_ll_txfifo_flush();
      usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
      wrote_usb();  // and again in 5 ms if it's still there
    }
    return;
  }
  if (now - g_wrote < 2 || !usb_serial_jtag_ll_txfifo_writable()) return;  // the last packet is going
  usb_serial_jtag_ll_txfifo_flush();  // with the FIFO empty: a zero-length packet ends the transfer
  g_wrote = 0;
}

}  // namespace

void setup() {
  // Quiet: only errors from the system's own logs (they start "E (", never "{").
  esp_log_level_set("*", ESP_LOG_ERROR);
  Serial.setRxBufferSize(1024);
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);       // USB Serial/JTAG; the rate is ignored
  Serial.setTxTimeoutMs(20);  // with nothing reading the port, a write gives up after 20 ms
  g_tx_empty = Serial.availableForWrite();

  esp_read_mac(g_mac, ESP_MAC_WIFI_STA);
  snprintf(g_mac_text, sizeof(g_mac_text), "%02x:%02x:%02x:%02x:%02x:%02x", g_mac[0], g_mac[1], g_mac[2], g_mac[3],
           g_mac[4], g_mac[5]);

  if (!GW_SECRETS) snprintf(g_problem, sizeof(g_problem), "NO SECRETS.H");
  else g_radio = radio_begin(g_problem, sizeof(g_problem));

  // The seq counter starts somewhere new each boot (the radio is on now, so esp_random is random),
  // so a panel never takes a fresh frame for a repeat of one from before the reset.
  static gw::Gateway gw_obj(g_io, g_mac_text, (uint16_t)esp_random());
  g_gw = &gw_obj;
  g_gw->boot();  // the first line after a reset (the chip's own start-up text comes before it)
  if (g_problem[0]) note("%s", g_problem);
  else if (memcmp(g_mac, kGatewayMac, 6)) note("this board's MAC isn't TCOMM_MAC_GATEWAY in secrets.h; the panels will ignore it");

  lcd::begin();
  lcd_static();
  lcd_update();
}

void loop() {
  read_serial();
  RxFrame f;
  while (g_rx && xQueueReceive(g_rx, &f, 0) == pdTRUE) g_gw->on_frame(f.from, f.data, f.len);
  g_gw->tick();
  finish_usb_tx();
  static uint32_t drawn = 0;
  if (millis() - drawn >= 200) {
    drawn = millis();
    lcd_update();
  }
  delay(1);
}
