/*
 * TComm Wi-Fi updater (docs/wifi-updates.md). A small sketch that lives in the panel's second app
 * slot (app1, at 0x310000; `bash tools/fw.sh updater COMx` puts it there over USB, leaving the panel
 * firmware in app0 alone). The panel firmware restarts into it from Settings > Update over Wi-Fi,
 * from serial "update", or after crashing three times in a row at start-up. The updater joins the
 * home Wi-Fi named in secrets.h, waits up to 10 minutes for a new panel firmware from the laptop
 * (`bash tools/fw.sh ota HOST`: an HTTP upload to /update, with the password in secrets.h), writes
 * it into app0 and restarts into it. With nothing received, it restarts into the panel firmware as
 * it was. The laptop sends the file itself (curl), so nothing has to get in through its firewall:
 * the first try, with ArduinoOTA and espota, authenticated and then stalled because the panel
 * couldn't connect back to the laptop (2026-10-31).
 *
 * Taking firmware over Wi-Fi needs a web server and a lot of Arduino's Network library, which the
 * panel firmware would carry in PSRAM for good (these builds run all their code from there). The
 * panel firmware only has ESP-IDF's smaller stack, for the time after a power cut.
 *
 * Written 2026-10-31. On the upstairs panel that day it started from serial "update", joined the
 * Wi-Fi, took the radio build over HTTP and restarted into it. UNVERIFIED: its screen (nobody has
 * looked at it yet) and the downstairs panel's Wi-Fi.
 */
#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPUpdateServer.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "gfx/font5x7.h"                   // tcomm/src, on the include path (tools/fw.sh)
#include "ws/rgb_lcd_port/rgb_lcd_port.h"  // the panel's pins and timings, and Waveshare's I2C and expander

#if __has_include("config/secrets.h")
#include "config/secrets.h"
#else
#include "config/secrets.example.h"  // a compile check only: tools/fw.sh won't upload this build
#endif
#ifndef TCOMM_WIFI_SSID
#define TCOMM_WIFI_SSID ""
#define TCOMM_WIFI_PASS ""
#endif
#ifndef TCOMM_OTA_PASS
#define TCOMM_OTA_PASS ""
#endif

extern io_extension_obj_t IO_EXTENSION;  // Waveshare's driver state (not exported by its header)

namespace {

constexpr uint32_t kJoinMs = 2 * 60 * 1000;   // to join the Wi-Fi
constexpr uint32_t kWaitMs = 10 * 60 * 1000;  // then for an upload
constexpr int kW = EXAMPLE_LCD_H_RES, kH = EXAMPLE_LCD_V_RES;
constexpr uint16_t kBg = 0x10A2, kCream = 0xFF9B, kGold = 0xF609, kRed = 0xE247;  // RGB565

uint16_t *g_fb = nullptr;
char g_host[24] = "tcomm-panel";
const esp_partition_t *g_panel_slot = nullptr;  // app0, the panel firmware
bool g_receiving = false, g_stay = false;
uint32_t g_wait_from = 0, g_last_data = 0;
WebServer g_web(80);
HTTPUpdateServer g_update;  // writes into app0 and, once it's all there, restarts into it

// ---- The screen: Waveshare's panel settings without LVGL, and text in the 5 x 7 font. ----

void fill(int x, int y, int w, int h, uint16_t c) {
  if (!g_fb) return;
  for (int r = y; r < y + h && r < kH; r++)
    for (int col = x; col < x + w && col < kW; col++) g_fb[r * kW + col] = c;
}

void text(int x, int y, const char *s, int scale, uint16_t fg) {
  for (; *s && x + 5 * scale <= kW; s++, x += 6 * scale) {
    const uint8_t *g = gfx::glyph5x7(*s);
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (g[r] & (0x10 >> c)) fill(x + c * scale, y + r * scale, scale, scale, fg);
  }
}

// A status line under the title (rows 0 to 3), cleared first; to serial too unless quiet.
void line(int row, const char *s, uint16_t fg = kCream, bool quiet = false) {
  int y = 180 + row * 70;
  fill(0, y, kW, 40, kBg);
  text(40, y, s, 5, fg);
  if (!quiet) Serial.printf("updater: %s\n", s);
}

void screen_begin() {
  DEV_I2C_Init();
  IO_EXTENSION_Init();
  // As the panel firmware's board::expander_begin: the isolated inputs back to inputs, and the amp
  // (EXIO3) and the isolated outputs off.
  uint8_t mode[2] = {0x02, 0xDE};
  DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, mode, 2);
  uint8_t out[2] = {0x03, (uint8_t)(IO_EXTENSION.Last_io_value & ~((1u << 3) | (1u << 6) | (1u << 7)))};
  DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, out, 2);

  // Waveshare's waveshare_esp32_s3_rgb_lcd_init (ws/rgb_lcd_port), without its LVGL hook, at display
  // preset C: 16 MHz with a 10-line copy buffer, as the panel firmware runs.
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = 16 * 1000 * 1000;
  cfg.timings.h_res = kW;
  cfg.timings.v_res = kH;
  cfg.timings.hsync_pulse_width = 8;
  cfg.timings.hsync_back_porch = 8;
  cfg.timings.hsync_front_porch = 4;
  cfg.timings.vsync_pulse_width = 8;
  cfg.timings.vsync_back_porch = 9;  // 8 puts the picture a row high (2026-10-22)
  cfg.timings.vsync_front_porch = 4;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = EXAMPLE_RGB_DATA_WIDTH;
  cfg.bits_per_pixel = EXAMPLE_RGB_BIT_PER_PIXEL;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = kW * 10;
  cfg.dma_burst_size = 64;  // Waveshare's psram_trans_align 64, under its current name
  cfg.hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC;
  cfg.vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC;
  cfg.de_gpio_num = EXAMPLE_LCD_IO_RGB_DE;
  cfg.pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK;
  cfg.disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP;
  const int data[16] = {EXAMPLE_LCD_IO_RGB_DATA0,  EXAMPLE_LCD_IO_RGB_DATA1,  EXAMPLE_LCD_IO_RGB_DATA2,
                        EXAMPLE_LCD_IO_RGB_DATA3,  EXAMPLE_LCD_IO_RGB_DATA4,  EXAMPLE_LCD_IO_RGB_DATA5,
                        EXAMPLE_LCD_IO_RGB_DATA6,  EXAMPLE_LCD_IO_RGB_DATA7,  EXAMPLE_LCD_IO_RGB_DATA8,
                        EXAMPLE_LCD_IO_RGB_DATA9,  EXAMPLE_LCD_IO_RGB_DATA10, EXAMPLE_LCD_IO_RGB_DATA11,
                        EXAMPLE_LCD_IO_RGB_DATA12, EXAMPLE_LCD_IO_RGB_DATA13, EXAMPLE_LCD_IO_RGB_DATA14,
                        EXAMPLE_LCD_IO_RGB_DATA15};
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
  cfg.flags.fb_in_psram = 1;
  esp_lcd_panel_handle_t panel = nullptr;
  void *fb = nullptr;
  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK ||
      esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK) {
    Serial.println("updater: no screen");  // carry on without it
    return;
  }
  g_fb = (uint16_t *)fb;
  fill(0, 0, kW, kH, kBg);
  uint8_t backlight[2] = {0x05, 0x30};  // a little under full (0 is full, 255 off)
  DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, backlight, 2);
}

// The Wi-Fi name this panel goes by: its floor, from its MAC and the table in secrets.h.
void name_from_mac() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  const uint8_t up[6] = TCOMM_MAC_UPSTAIRS, down[6] = TCOMM_MAC_DOWNSTAIRS;
  if (!memcmp(mac, up, 6)) strcpy(g_host, "tcomm-upstairs");
  else if (!memcmp(mac, down, 6)) strcpy(g_host, "tcomm-downstairs");
}

// Restarts into the panel firmware. If app0 holds none (an upload that failed part way), the
// updater stays and keeps waiting.
void back_to_panel(const char *why) {
  line(3, why);
  if (g_panel_slot && esp_ota_set_boot_partition(g_panel_slot) == ESP_OK) {
    delay(3000);
    esp_restart();
  }
  line(3, "NO PANEL FIRMWARE. STILL WAITING", kRed);
  g_stay = true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(2);  // never wait on a laptop that isn't reading
  screen_begin();
  name_from_mac();
  g_panel_slot = esp_ota_get_next_update_partition(nullptr);  // the slot this isn't running from
  text(40, 50, "TCOMM UPDATE", 9, kGold);
  line(0, g_host);
  if (!TCOMM_WIFI_SSID[0]) {
    line(1, "NO WI-FI NAME IN SECRETS.H", kRed);
    delay(20000);
    back_to_panel("BACK TO THE PANEL");
    return;
  }
  line(1, "JOINING THE WI-FI");
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(g_host);
  WiFi.begin(TCOMM_WIFI_SSID, TCOMM_WIFI_PASS);
  for (uint32_t t0 = millis(); WiFi.status() != WL_CONNECTED && millis() - t0 < kJoinMs;) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    line(1, "NO WI-FI CONNECTION", kRed);
    back_to_panel("BACK TO THE PANEL");
    return;
  }
  char s[48];
  snprintf(s, sizeof(s), "WI-FI %s", WiFi.localIP().toString().c_str());
  line(1, s);
  if (!TCOMM_OTA_PASS[0]) {  // never take an upload from anyone on the network
    line(2, "NO UPLOAD PASSWORD IN SECRETS.H", kRed);
    delay(20000);
    back_to_panel("BACK TO THE PANEL");
    return;
  }

  MDNS.begin(g_host);  // tcomm-upstairs.local, where Windows can find it
  Update.onProgress([](size_t done, size_t) {
    static size_t shown = 0;
    g_last_data = millis();
    if (!g_receiving) {
      g_receiving = true;
      shown = 0;
      line(3, "");
    }
    if (done >= shown && done - shown < 64 * 1024) return;  // a redraw every 64 KB
    shown = done;
    char m[32];
    snprintf(m, sizeof(m), "RECEIVING %u KB", (unsigned)(done / 1024));
    line(2, m, kCream, true);
  });
  g_update.setup(&g_web, "/update", "tcomm", TCOMM_OTA_PASS);
  g_web.begin();
  g_wait_from = millis();
  line(2, "READY FOR THE UPLOAD");
}

void loop() {
  g_web.handleClient();
  if (g_receiving && millis() - g_last_data > 15000) {  // stopped part way, or refused
    g_receiving = false;
    g_wait_from = millis();  // a fresh 10 minutes to try again
    line(2, "UPLOAD FAILED. TRY AGAIN", kRed);
  }
  if (!g_receiving && !g_stay && WiFi.status() == WL_CONNECTED) {
    uint32_t waited = millis() - g_wait_from;
    if (waited >= kWaitMs) {
      back_to_panel("NO UPLOAD. BACK TO THE PANEL");
    } else {
      static uint32_t shown = UINT32_MAX;
      uint32_t left = (kWaitMs - waited) / 1000;
      if (left != shown) {
        shown = left;
        char m[40];
        snprintf(m, sizeof(m), "BACK TO NORMAL IN %u:%02u", (unsigned)(left / 60), (unsigned)(left % 60));
        line(3, m, kCream, true);
      }
    }
  }
  delay(5);
}
