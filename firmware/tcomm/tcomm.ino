/*
 * TComm panel firmware, phase 2: the full interface with demo data (a simulated other panel and
 * gateway). Waveshare ESP32-S3-Touch-LCD-4.3C, Arduino core 3.3.11, LVGL 8.4.
 *
 * Layers (handoff): ui/ (LVGL) -> app/ (controller, plain C++) -> core/ (state machines, protocol,
 * plain C++) ; board/ (hardware) ; ws/ (Waveshare's drivers, Apache-2.0, LVGL task stack raised).
 * The application logic runs in an LVGL timer every 50 ms, so it shares the LVGL task with the
 * interface and needs no locks of its own. The large artwork is drawn by its own task on core 0,
 * so the interface never waits for it. Audio has its own task; ESP-NOW frames arrive through a
 * queue. Serial (115200): type "help".
 */
#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_task_wdt.h"
#include "src/app/controller.h"
#include "src/app/demo.h"
#include "src/app/panel_app.h"
#include "src/board/board.h"
#include "src/board/device_platform.h"
#include "src/config/tcomm_config.h"
#include "src/core/brightness.h"
#include "src/core/civil.h"
#include "src/core/synth.h"
#include "src/gfx/canvas.h"
#include "src/ui/art.h"
#include "src/ui/ui.h"
#include "src/ws/gt911/gt911.h"
#include "src/ws/i2c/i2c.h"
#include "src/ws/io_extension/io_extension.h"
#include "src/ws/lvgl_port/lvgl_port.h"
#include "src/ws/rgb_lcd_port/rgb_lcd_port.h"
#include "src/ws/speaker_microphone/codec_dev.h"
#if __has_include("src/config/secrets.h")
#include "src/config/secrets.h"  // the location; the keys are only used by board_radio.cpp
#endif

// ---- Display tests (Settings > Test) ----
// Mike sees streaks at the left and the picture shifted a row or two whenever a large area is
// redrawn, and it can linger. With Waveshare's copy (bounce) buffers the processor copies the
// picture out of PSRAM through the 32 KB data cache, line by line and again at the top of every
// frame, so anything else using PSRAM makes the copy late. All six first presets glitched (20-line
// buffers worst). Without a copy buffer the display reads PSRAM by DMA itself, past the cache and
// the processor; that is Espressif's own default. Mike compares by eye.
struct DisplayPreset {
  uint8_t mhz, lines;  // lines 0: no copy buffer
};
static const DisplayPreset kDisplayPresets[] = {{16, 0}, {12, 0}, {16, 10}, {12, 10}};
static const int kDisplayPresetCount = sizeof(kDisplayPresets) / sizeof(kDisplayPresets[0]);
// Preset in use; C until another is chosen. On core 3.2.0 with Espressif's high_perf libraries, C
// (Waveshare's 16 MHz with 10-line copy buffers) is stable through status changes and scrolling
// (Mike, 2026-10-22).
static int g_disp = 2;
static bool g_strips = false;
static uint8_t *g_scratch = nullptr;
static const size_t kScratch = 32 * 1024;
static esp_lcd_panel_handle_t g_panel = nullptr;

static void load_display_test() {
  uint8_t v = board::load_display_test();
  if (v == 0xFF) return;
  g_disp = (v & 0x0F) < kDisplayPresetCount ? (v & 0x0F) : 2;
  g_strips = (v & 0x80) != 0;
}
static void save_display_test() { board::save_display_test((uint8_t)(g_disp | (g_strips ? 0x80 : 0))); }

class PanelPlatform : public board::DevicePlatform {
 public:
  void display_test_label(char *out, size_t n) override {
    const DisplayPreset &p = kDisplayPresets[g_disp];
    if (p.lines) snprintf(out, n, "Display %c: %u MHz, %u-line copy buffer", 'A' + g_disp, p.mhz, p.lines);
    else snprintf(out, n, "Display %c: %u MHz, no copy buffer", 'A' + g_disp, p.mhz);
  }
  void display_test_next() override { display_test_set((g_disp + 1) % kDisplayPresetCount); }
  void display_test_set(int next) {
    bool restart = kDisplayPresets[next].lines != kDisplayPresets[g_disp].lines;
    g_disp = next;
    save_display_test();
    if (restart) {  // the copy buffers are allocated when the display starts
      board::elog("display test: new buffer size, restarting");
      delay(50);
      esp_restart();
    }
    esp_lcd_rgb_panel_set_pclk(g_panel, kDisplayPresets[g_disp].mhz * 1000000u);  // from the next frame
  }
  bool strip_drawing() override { return g_strips; }
  void toggle_strip_drawing() override {
    g_strips = !g_strips && g_scratch;
    ui::set_art_scratch(g_strips ? g_scratch : nullptr, g_strips ? kScratch : 0);
    save_display_test();
  }
};

static PanelPlatform g_platform;
static tc::Controller g_ctrl(g_platform);
#if TCOMM_DEMO
static tc::Demo g_demo(g_platform);
static tc::Demo *g_demo_ptr = &g_demo;
#else
static tc::Demo *g_demo_ptr = nullptr;
#endif
static tc::PanelApp g_app(g_ctrl, g_platform, g_demo_ptr);
static uint32_t g_last_idle = 0;

static void *psram_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

// Redraw statistics for "info": every LVGL refresh also costs PSRAM bandwidth the display needs.
static uint32_t g_refr_count = 0, g_refr_px = 0, g_refr_ms = 0, g_refr_max = 0, g_stats_since = 0;
static void refr_monitor(lv_disp_drv_t *, uint32_t time, uint32_t px) {
  g_refr_count++;
  g_refr_px += px;
  g_refr_ms += time;
  if (time > g_refr_max) g_refr_max = time;
}

// The drawing task: sky, floor tiles, outside and doorbell art (ui::run_art_job). Core 0, below
// audio, so a chime never waits for a drawing.
static TaskHandle_t g_art_task = nullptr;
static void art_task(void *) {
  g_art_task = xTaskGetCurrentTaskHandle();
  for (;;)
    if (!ui::run_art_job()) vTaskDelay(pdMS_TO_TICKS(10));
}

// Called after each row of the larger drawings. The drawing task lets core 0's idle task run now
// and then (the task watchdog checks it); the LVGL task, which still draws the small icons, feeds
// the watchdog it joined.
static TaskHandle_t g_watched_task = nullptr;
static void row_hook() {
  TaskHandle_t me = xTaskGetCurrentTaskHandle();
  if (me == g_art_task) {
    static uint32_t last = 0;
    if (millis() - last >= 10) {
      vTaskDelay(1);
      last = millis();
    }
  } else if (g_watched_task && me == g_watched_task) {
    esp_task_wdt_reset();
  }
}

// "pattern": a test screen for the link to the display. Red marks the top five rows, blue the
// bottom five, green and yellow the first and last five columns. The 7-row stripes don't repeat
// every 10 or 20 rows, so a line that starts with stale pixels from the copy buffer shows up.
static lv_obj_t *g_pattern = nullptr;
static lv_obj_t *pattern_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
static void toggle_pattern() {
  if (g_pattern) {
    lv_obj_del(g_pattern);
    g_pattern = nullptr;
    return;
  }
  g_pattern = pattern_box(lv_layer_sys(), 0, 0, 800, 480, 0x000000);
  lv_obj_add_flag(g_pattern, LV_OBJ_FLAG_CLICKABLE);  // nothing underneath gets pressed meanwhile
  for (int y = 0; y < 480; y += 14) pattern_box(g_pattern, 0, y, 800, 7, 0xFFFFFF);
  pattern_box(g_pattern, 0, 0, 800, 5, 0xFF0000);
  pattern_box(g_pattern, 0, 475, 800, 5, 0x0000FF);
  pattern_box(g_pattern, 0, 0, 5, 480, 0x00FF00);
  pattern_box(g_pattern, 795, 0, 5, 480, 0xFFFF00);
  lv_obj_t *card = pattern_box(g_pattern, 230, 180, 340, 120, 0x303030);
  lv_obj_t *l = lv_label_create(card);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, "RED = top 5 rows\nBLUE = bottom 5 rows\nGREEN left, YELLOW right\ntype \"pattern\" again to close");
  lv_obj_center(l);
}

// Runs inside the LVGL task.
static void app_tick(lv_timer_t *) {
  // The LVGL task joins the task watchdog: if drawing or a display flush ever hangs, the panel
  // reboots (and the 7-day log shows it) instead of freezing silently.
  static bool watched = esp_task_wdt_add(nullptr) == ESP_OK;
  if (watched) {
    if (!g_watched_task) g_watched_task = xTaskGetCurrentTaskHandle();
    esp_task_wdt_reset();
  }
  uint32_t idle = lv_disp_get_inactive_time(nullptr);
  if (idle < g_last_idle) g_ctrl.touch();  // any touch counts for the backlight
  g_last_idle = idle;
  uint8_t buf[tc::kMaxMsg];
  size_t n = 0;
  while (board::radio_poll(buf, &n)) g_ctrl.on_radio(buf, n);
  g_ctrl.tick();
#if TCOMM_DEMO
  g_demo.tick();
#endif
  tc::WallTime w = g_platform.wall();
  ui::Frame f{&g_ctrl.view(), &g_ctrl.settings(), g_ctrl.own(), w, w.valid ? g_platform.utc_offset(w.utc) : 0, millis(),
              &g_ctrl};
  uint32_t t0 = millis();
  ui::render(f);
  uint32_t ms = millis() - t0;
  if (ms > 100) board::elog("frame took %lu ms", (unsigned long)ms);
  // The calendar's and Settings' animations, measured here (the spec drops one under about 20 fps).
  static uint32_t anim_seen = 0;
  ui::AnimReport a = ui::anim_report();
  if (a.serial != anim_seen) {
    anim_seen = a.serial;
    board::elog("%s: %d frames in %lu ms, %lu fps", a.what, a.frames, (unsigned long)a.ms,
                (unsigned long)(a.ms ? a.frames * 1000UL / a.ms : 0));
  }
}

// "bench": times each piece of artwork on this board. Runs in the loop task on its own canvases.
static void bench_line(const char *what, uint32_t since) {
  Serial.printf("%s %lu ms\n", what, (unsigned long)(millis() - since));
}

static void bench() {
  gfx::Canvas c;
  uint32_t t;
  if (c.create(art::kScreenW, art::kScreenH, false)) {
    t = millis();
    art::sky(c, tc::Phase::Midday, 0.5f, false);
    bench_line("sky (midday)", t);
    t = millis();
    art::sky(c, tc::Phase::Night, 0.3f, true);
    bench_line("sky (night, moon)", t);
  } else Serial.println("bench: no memory for the sky");
  if (c.create(art::kFloorW, art::kFloorH, true)) {
    t = millis();
    art::floor_tile(c, tc::Floor::Upstairs, 0, tc::Mode::Day);
    bench_line("floor tile", t);
  }
  // The same two in strips through internal RAM, as the drawing task does them. Its own buffer:
  // the drawing task may be using the shared one right now.
  const size_t kStrip = 32 * 1024;
  uint8_t *strip = (uint8_t *)heap_caps_malloc(kStrip, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (strip) {
    if (c.create(art::kScreenW, art::kScreenH, false)) {
      t = millis();
      gfx::draw_in_strips(c, strip, kStrip, [](gfx::Canvas &s) { art::sky(s, tc::Phase::Midday, 0.5f, false); });
      bench_line("sky (midday), strips", t);
    }
    if (c.create(art::kFloorW, art::kFloorH, true)) {
      t = millis();
      gfx::draw_in_strips(c, strip, kStrip, [](gfx::Canvas &s) { art::floor_tile(s, tc::Floor::Upstairs, 0, tc::Mode::Day); });
      bench_line("floor tile, strips", t);
    }
    free(strip);
  } else Serial.println("bench: no internal RAM for strips");
  if (c.create(art::kSideW, art::kFloorH, true)) {
    t = millis();
    art::outside_tile(c, tc::Mode::Day);
    bench_line("outside tile", t);
    t = millis();
    art::bell_tile(c, tc::Mode::Day, false);
    bench_line("doorbell tile", t);
  }
  if (c.create(454, art::kStripH, true)) {
    t = millis();
    art::strip(c, tc::Phase::Midday, 0.5f, false, false, 0);
    bench_line("date strip", t);
  }
  if (c.create(80, 80, true)) {
    t = millis();
    art::badge(c, 0, gfx::hex(0xFBF1DC), gfx::hex(0x1F6B3A));
    bench_line("badge", t);
  }
}

// ---- SHT40 (phase 5): read from the loop task; the controller takes each reading under the LVGL
// lock, since it runs in the LVGL task. ----
#if TCOMM_SENSOR
static bool g_sht_found = false, g_sht_valid = false;
static uint32_t g_sht_serial = 0, g_sht_next = 0, g_sht_reads = 0, g_sht_fails = 0;
static int g_sht_misses = 0;
static int16_t g_sht_temp = 0;  // the last good reading, before the Settings temperature offset
static uint16_t g_sht_hum = 0;

static void sensor_tick() {
  uint32_t now = millis();
  if ((int32_t)(now - g_sht_next) < 0) return;
  if (!g_sht_found) {
    g_sht_found = board::sht40_begin(&g_sht_serial);
    if (!g_sht_found) {
      g_sht_next = now + 30000;  // look again later: it may be connected while the panel runs
      return;
    }
    Serial.printf("[tcomm] SHT40 found, serial %08lX\n", (unsigned long)g_sht_serial);
  }
  g_sht_next = now + TCOMM_SENSOR_PERIOD_MS;
  int16_t t;
  uint16_t h;
  g_sht_reads++;
  if (board::sht40_measure(t, h)) {
    g_sht_misses = 0;
    g_sht_temp = t;
    g_sht_hum = h;
    g_sht_valid = true;
  } else {
    g_sht_fails++;
    if (++g_sht_misses < TCOMM_SENSOR_MISSES) return;  // one bad read changes nothing on screen
    Serial.println("[tcomm] SHT40 not answering: the inside reading shows dashes until it's back");
    g_sht_valid = false;
    g_sht_found = false;  // look for it again in 30 s (unplugged, or a loose wire)
    g_sht_next = now + 30000;
  }
  if (lvgl_port_lock(200)) {  // the controller only sends it on when it moved enough (handoff)
    g_ctrl.on_local_reading(g_sht_valid, g_sht_temp, g_sht_hum);
    lvgl_port_unlock();
  }
}

static void sensor_info() {
  if (g_sht_valid)
    Serial.printf("sensor SHT40 %08lX: %s%d.%d F, %u.%u %% (before the offset); %lu reads, %lu failed\n",
                  (unsigned long)g_sht_serial, g_sht_temp < 0 ? "-" : "", abs(g_sht_temp) / 10, abs(g_sht_temp) % 10,
                  g_sht_hum / 10, g_sht_hum % 10, (unsigned long)g_sht_reads, (unsigned long)g_sht_fails);
  else
    Serial.printf("sensor: %s; %lu reads, %lu failed\n", g_sht_found ? "SHT40 found, no good reading yet" : "no SHT40 found",
                  (unsigned long)g_sht_reads, (unsigned long)g_sht_fails);
}
#endif

void setup() {
  Serial.begin(115200);
  // With a laptop attached but not reading, the USB serial port blocks each write for up to the TX
  // timeout. 0 is worse in core 3.2.0: HWCDC::write counts its retries down from the timeout, so 0
  // wraps round and the write waits until the laptop reads. That froze the sound task after a few
  // taps in the ringtone list, until a serial monitor opened (2026-10-24). 2 ms gives up at once,
  // and only the loop task writes to serial now (board::elog, board::elog_flush).
  Serial.setTxTimeoutMs(2);
  board::storage_begin();
  tc::Settings settings;
  tc::OwnState own;
  tc::DogState dog;
  tc::MailState mail;
  bool have_settings = board::load_settings(settings);
#if defined(TCOMM_LATITUDE) && defined(TCOMM_LONGITUDE)
  // The location for sunrise and sunset lives in the untracked secrets.h, not the public repo. It
  // wins over a saved copy, so changing it there and reflashing is enough.
  settings.latitude = TCOMM_LATITUDE;
  settings.longitude = TCOMM_LONGITUDE;
#endif
  board::load_own(own);
  board::load_dog(dog);
  board::load_mail(mail);
  load_display_test();
  const DisplayPreset &dp = kDisplayPresets[g_disp];

  // Waveshare bring-up order: I2C, expander, touch (resets through EXIO1), panel, codec.
  DEV_I2C_Init();
  IO_EXTENSION_Init();
  board::i2c_begin();
  esp_lcd_touch_handle_t tp = touch_gt911_init(DEV_I2C_Get_Bus_Device());
  esp_lcd_panel_handle_t panel = g_panel =
      waveshare_esp32_s3_rgb_lcd_init(dp.mhz * 1000000u, (size_t)EXAMPLE_LCD_H_RES * dp.lines);
  codec_init();
  board::expander_begin();
  board::expander_output(board::kExioDisp, true);
  board::expander_backlight(tc::kBacklightOff);  // dark until the first tick picks the level (no 3 AM flash)
  board::calendar_fs_begin();  // the SD card (its D3 is on the expander); g_ctrl.begin reads the calendar

  g_platform.begin(settings);
  gfx::set_allocator({psram_alloc, free});
  gfx::set_row_hook(row_hook);
  board::audio_begin(settings.ringtone);
#if TCOMM_RADIO_ESPNOW && !TCOMM_DEMO
  // The radio drops frames whose sender byte doesn't match the MAC they came from, so the board's
  // own MAC decides its floor, and the Settings swap is locked.
  int mac_floor = board::radio_own_floor();
  if (mac_floor >= 0) {
    settings.floor = (tc::Floor)mac_floor;
    g_app.lock_floor(true);
  }
  board::radio_begin();
#endif
#if TCOMM_DEMO
  g_platform.set_demo(&g_demo);
#endif
  // The sensor's first reading comes from the loop within a second; with none found, demo mode
  // shows its fixed inside reading instead.
  bool sensor_present = false;
#if TCOMM_SENSOR
  sensor_present = g_sht_found = board::sht40_begin(&g_sht_serial);
  if (g_sht_found) Serial.printf("[tcomm] SHT40 found, serial %08lX\n", (unsigned long)g_sht_serial);
#endif
  (void)sensor_present;

  if (lvgl_port_init(panel, tp) != ESP_OK) Serial.println("[tcomm] LVGL port failed to start");
  if (lvgl_port_lock(-1)) {
    g_ctrl.begin(settings, own, dog, mail);
    if (!have_settings) g_platform.save_settings(settings);
#if TCOMM_DEMO
    g_demo.begin(g_ctrl);
    if (!sensor_present) g_ctrl.on_local_reading(true, TCOMM_FAKE_INSIDE_TEMP, TCOMM_FAKE_INSIDE_HUM);
#endif
    ui::init(g_app);
    lv_timer_create(app_tick, 50, nullptr);
    lv_disp_get_default()->driver->monitor_cb = refr_monitor;
    lvgl_port_unlock();
  }
  // Strip drawing (a display test, off unless chosen): the drawing task works in 32 KB of internal
  // RAM a strip of rows at a time and writes each finished strip to PSRAM once. Mike found the
  // screen worse with it on (2026-10-22), so it stays a choice until the display tests settle it.
  g_scratch = (uint8_t *)heap_caps_malloc(kScratch, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!g_scratch) g_strips = false;
  ui::set_art_scratch(g_strips ? g_scratch : nullptr, g_strips ? kScratch : 0);
  xTaskCreatePinnedToCore(art_task, "art", 8192, nullptr, 1, nullptr, 0);
  Serial.printf("[tcomm] started: %s, clock %s, free PSRAM %u\n", tc::floor_word(settings.floor),
                g_platform.wall().valid ? "from RTC" : "NOT SET", (unsigned)ESP.getFreePsram());
  char disp[64];
  g_platform.display_test_label(disp, sizeof(disp));
  Serial.printf("[tcomm] %s, strip drawing %s\n", disp, g_strips ? "on" : "off");
}


// ---- Serial console, for testing without touching the screen ----

// "knock": the other floor's panel rings this one, delivered as if it came by radio, so one panel
// can be tested alone. It chimes and shows the banner like any ring.
static void knock_from_other_floor() {
  tc::Msg m;
  m.type = tc::MsgType::Knock;
  m.sender = tc::sender_id(tc::other_floor(g_ctrl.settings().floor));
  m.seq = (uint16_t)(esp_random() | 1);
  m.u.knock.id = (uint16_t)(esp_random() | 1);
  uint8_t buf[tc::kMaxMsg];
  size_t n = tc::encode(m, buf, sizeof(buf));
  if (n) g_ctrl.on_radio(buf, n);
}

// "mail other": the other floor's panel taps its mail badge, delivered as if it came by radio, so
// one panel can be tested alone. Downstairs only acknowledges; upstairs marks or takes back.
static void mail_from_other_floor() {
  tc::Floor other = tc::other_floor(g_ctrl.settings().floor);
  const tc::MailState &mine = g_ctrl.mail();
  if (other == tc::Floor::Downstairs && !mine.waiting) {
    Serial.println("mail: nothing waiting for downstairs to acknowledge");
    return;
  }
  tc::WallTime w = g_platform.wall();
  tc::Msg m;
  m.type = tc::MsgType::Mail;
  m.sender = tc::sender_id(other);
  m.seq = (uint16_t)(esp_random() | 1);
  m.u.mail.waiting = mine.waiting ? 0 : 1;
  m.u.mail.since = w.valid ? w.utc : 0;
  m.u.mail.ver = mine.ver + 1;
  uint8_t buf[tc::kMaxMsg];
  size_t n = tc::encode(m, buf, sizeof(buf));
  if (n) g_ctrl.on_radio(buf, n);
  Serial.printf("mail: %s taps theirs; now %s\n", tc::floor_lower(other), g_ctrl.mail().waiting ? "waiting" : "none waiting");
}

static void command(char *s) {
  int Y, M, D, h, m, sec = 0, v;
  char ch;
  if (!strcmp(s, "help")) {
    Serial.println("status N (0-4) | bell | dog | time YYYY-MM-DD HH:MM[:SS] (local) | demo N (0-7) | info | bench");
    Serial.printf("ring N (0-%d, the place in the list): choose a ringtone and play it, as a tap does\n",
                  tc::kRingtoneCount - 1);
    Serial.println("sound tests: play N (0 chime, 1 soft, 2 come on, 3 not now) | listen | amp hold 0/1 | codec reopen");
    Serial.println("sound checks: log (the last 64 events) | regs (amp pin and codec registers, into the log)");
    Serial.println("bus test: i2c test (1000 reads each of the codec and touch chip IDs, at 400 and 100 kHz)");
    Serial.println("display tests: display A-D (as Settings > Test) | pattern (toggle) | pclk N (MHz, 8-20, until restart)");
    Serial.println("SD card: sd (what's there) | sd format yes (FAT32, erasing it, only if it won't mount as it is)");
    Serial.println("radio (ESP-NOW counts, the last frames, and what the outside tile and dog pill show)");
    Serial.println("shot (the screen as raw RGB565, about a second; tools/shot2png.py makes a PNG)");
    Serial.println("mail (tap this panel's mail badge) | mail other (the other floor taps theirs, as if by radio)");
    Serial.println("gear (as a tap on the gear: Settings rolls down or up) | knock (the other floor rings, as if by radio)");
    return;
  }
  if (!strcmp(s, "bench")) {
    bench();
    return;
  }
  if (!strcmp(s, "listen")) {  // takes about 0.4 s, outside the LVGL lock
    float db[2], peak;
    if (board::audio_listen(300, db, peak))
      Serial.printf("mic: %.1f dBFS RMS, peak %.1f dBFS (300 ms)\n", db[0], peak);
    else
      Serial.println("mic: nothing heard from the microphones");
    return;
  }
  if (!strcmp(s, "sd")) {
    board::sd_report();
    return;
  }
  if (!strcmp(s, "sd format yes")) {  // only formats a card that can't be mounted as it is
    board::sd_format_if_unreadable();
    return;
  }
  if (!strcmp(s, "log")) {
    board::elog_print();
    return;
  }
  if (!strcmp(s, "shot")) {  // outside the LVGL lock: a redraw meanwhile can show half done
    void *fb = nullptr;
    if (!g_panel || esp_lcd_rgb_panel_get_frame_buffer(g_panel, 1, &fb) != ESP_OK || !fb) {
      Serial.println("shot: no frame buffer");
      return;
    }
    // Long enough that a laptop reading the port never misses a piece; back to 2 ms (see setup).
    Serial.setTxTimeoutMs(250);
    Serial.printf("SHOT %d %d RGB565\n", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    const uint8_t *px = (const uint8_t *)fb;
    for (int y = 0; y < EXAMPLE_LCD_V_RES; y++)
      Serial.write(px + (size_t)y * EXAMPLE_LCD_H_RES * 2, EXAMPLE_LCD_H_RES * 2);
    Serial.println();
    Serial.println("SHOT END");
    Serial.setTxTimeoutMs(2);
    return;
  }
  if (!strcmp(s, "regs")) {
    board::audio_log_state();
    return;
  }
  if (sscanf(s, "amp hold %d", &v) == 1) {
    board::audio_amp_hold(v != 0);
    Serial.printf("amp hold %s\n", v ? "on" : "off");
    return;
  }
  if (!strcmp(s, "i2c test")) {  // a few seconds, outside the LVGL lock: touch keeps working
    board::i2c_test(400000, 1000);
    board::i2c_test(100000, 1000);
    Serial.printf("i2c errors since start-up: %lu\n", (unsigned long)board::i2c_errors());
    return;
  }
  if (!strcmp(s, "codec reopen")) {
    board::audio_reopen_codec();
    return;
  }
  if (!lvgl_port_lock(1000)) return;
  if (sscanf(s, "status %d", &v) == 1 && tc::status_valid((uint8_t)v)) g_ctrl.choose_status((tc::Status)v);
  else if (!strcmp(s, "bell")) g_ctrl.tap_bell();
  else if (!strcmp(s, "dog")) g_ctrl.tap_dog();
  else if (!strcmp(s, "mail")) Serial.println(g_ctrl.tap_mail() ? "mail: tapped" : "mail: nothing waiting to acknowledge");
  else if (!strcmp(s, "mail other")) mail_from_other_floor();
  else if (!strcmp(s, "gear")) ui::tap_gear();
  else if (!strcmp(s, "knock")) knock_from_other_floor();
  else if (sscanf(s, "time %d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &sec) >= 5)
    g_app.set_clock_local(tc::seconds_from_civil(Y, M, D, h, m, sec));
  else if (sscanf(s, "demo %d", &v) == 1 && v >= 0 && v <= 7) g_app.demo((ui::DemoAction)v);
  else if (sscanf(s, "ring %d", &v) == 1 && v >= 0 && v < tc::kRingtoneCount) {
    tc::Settings st = g_ctrl.settings();
    st.ringtone = tc::ringtone_id(v);  // N is the place in the list
    g_app.apply_settings(st);
    g_app.preview(tc::Sound::Chime, st.volume);
  }
  else if (sscanf(s, "play %d", &v) == 1 && v >= 0 && v <= 3)
    g_platform.preview((tc::Sound)v, g_ctrl.settings().volume);  // the ringtone chosen stays as it is
  else if (!strcmp(s, "pattern")) toggle_pattern();
  else if (sscanf(s, "display %c", &ch) == 1 && toupper(ch) >= 'A' && toupper(ch) < 'A' + kDisplayPresetCount)
    g_platform.display_test_set(toupper(ch) - 'A');  // restarts the panel when the buffer size changes
  else if (sscanf(s, "pclk %d", &v) == 1 && v >= 8 && v <= 20) {
    // Takes effect at the next frame; back to 16 MHz at the next boot.
    esp_err_t e = esp_lcd_rgb_panel_set_pclk(g_panel, (uint32_t)v * 1000000u);
    Serial.printf("pixel clock %d MHz: %s\n", v, esp_err_to_name(e));
  }
  else if (!strcmp(s, "radio")) {
    board::radio_report();
    // What the panel made of it.
    const tc::View &vw = g_ctrl.view();
    Serial.printf("outside tile: %s, %s, %s, forecast %u%s%s, \"%s\"\n", vw.outside.valid ? "showing" : "dashes",
                  vw.outside.temp, vw.outside.hum, (unsigned)vw.outside.code, vw.outside.heat ? ", danger heat" : "",
                  vw.outside.cold ? ", danger cold" : "", vw.outside.phrase);
    Serial.printf("mail: %s (change %lu)\n", g_ctrl.mail().waiting ? "waiting for downstairs" : "none waiting",
                  (unsigned long)g_ctrl.mail().ver);
    Serial.printf("dog pill: %s; other floor's tile: %s\n", vw.dog.pill[0] ? vw.dog.pill : "(none)",
                  vw.floor[(int)vw.me == 0 ? 1 : 0].word);
  }
  else if (!strcmp(s, "info")) {
    tc::WallTime w = g_platform.wall();
    Serial.printf("floor %s, status %s, clock %s (quality %u), free heap %u, free PSRAM %u\n",
                  tc::floor_word(g_ctrl.settings().floor), tc::status_word(g_ctrl.own().status), w.valid ? "set" : "NOT SET",
                  w.quality, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    Serial.printf("since the last info (%lu s): %lu redraws, %lu px, %lu ms drawing, slowest %lu ms\n",
                  (unsigned long)((millis() - g_stats_since) / 1000), (unsigned long)g_refr_count,
                  (unsigned long)g_refr_px, (unsigned long)g_refr_ms, (unsigned long)g_refr_max);
    g_refr_count = g_refr_px = g_refr_ms = g_refr_max = 0;
    g_stats_since = millis();
    board::audio_report();
    board::CalendarFs fs;
    bool have_fs = board::calendar_fs_info(fs);
    Serial.printf("calendar: %d of %d slots in memory; files on %s, %d of them, %llu of %llu MB used\n",
                  g_ctrl.events().count(), tc::kMaxEvents,
                  !have_fs ? "NOTHING (no card, no FAT partition)" : fs.sd ? "the SD card" : "the FAT partition (no SD card)",
                  fs.files, (unsigned long long)(fs.used >> 20), (unsigned long long)(fs.total >> 20));
    Serial.printf("i2c: %lu failed transfers since start-up (expander, sensor, clock)\n", (unsigned long)board::i2c_errors());
#if TCOMM_SENSOR
    sensor_info();
#endif
  } else Serial.println("unknown command; type help");
  lvgl_port_unlock();
}

void loop() {
  static char buf[64];
  static int len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      buf[len] = 0;
      if (len) command(buf);
      len = 0;
    } else if (len < (int)sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
  g_platform.flush_saves();  // NVS writes happen here, not in the LVGL task
  board::elog_flush();       // and so do writes to USB serial
#if TCOMM_SENSOR
  sensor_tick();
#endif
  delay(20);
}
