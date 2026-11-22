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
#include "esp_memory_utils.h"
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
#include "src/core/voice_dsp.h"  // kVoiceBoostMaxDb (serial voice boost)
#include "src/gfx/canvas.h"
#include "src/ui/art.h"
#include "src/ui/ss_host.h"
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

// Strip drawing's 32 KB of internal RAM is taken only while it is on, not at every start (memory
// audit 2026-11-12, recommendation 2). On: false, with a line in the log (and on serial), when there
// isn't the memory; strip drawing then stays off.
static bool strip_scratch_on() {
  if (!g_scratch) g_scratch = (uint8_t *)heap_caps_malloc(kScratch, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!g_scratch) {
    g_strips = false;
    board::elog("strip drawing: refused, no %u bytes of internal RAM free in one piece (largest %u); it stays off",
                (unsigned)kScratch, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return false;
  }
  g_strips = true;
  ui::set_art_scratch(g_scratch, kScratch);
  return true;
}

// Off: the drawing task stops taking the scratch at once; a drawing that began with it may still be
// using it, so it is freed when that one is done (up to 2 s here; the drawing task needs no lock).
// Still busy after that (it shouldn't be): kept, and used again if strip drawing comes back on.
static void strip_scratch_off() {
  g_strips = false;
  ui::set_art_scratch(nullptr, 0);
  if (!g_scratch) return;
  for (int i = 0; i < 400 && ui::art_scratch_busy(); i++) delay(5);
  if (ui::art_scratch_busy()) {
    board::elog("strip drawing: off; its scratch is still in use by a drawing, so it is kept");
    return;
  }
  heap_caps_free(g_scratch);
  g_scratch = nullptr;
}

// The reach test (docs/voice-notes/DESIGN.md 13.0, piece R), in board_radio.cpp. Declared here because
// board.h is frozen for the voice work; to move into board.h when the shared interfaces next open.
namespace board {
void radio_reach(int n, int gap_ms, bool ping);
}

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
  bool updater_installed() override { return board::updater_installed(); }
  void start_updater() override { board::restart_into_updater(); }
  // A voice message's ding stops the screensaver first, so the ding's render never competes with the
  // screensaver's PSRAM (DESIGN 2.3, check fix C3), and counts as activity for the idle time. The
  // controller plays Ding only when the ding rules allow a wake. The LVGL lock is recursive, so this is
  // safe from the LVGL task (lock held) and from a serial command (lock not held).
  void play(tc::Sound s, uint8_t volume) override {
    if (s == tc::Sound::Ding && lvgl_port_lock(100)) {
      if (ui::screensaver_running()) ui::screensaver_stop();
      lv_disp_trig_activity(nullptr);
      lvgl_port_unlock();
    }
    board::DevicePlatform::play(s, volume);
  }
  void toggle_strip_drawing() override {
    if (g_strips) strip_scratch_off();
    else if (!strip_scratch_on()) return;  // refused (no memory): stays off, nothing saved
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
  const int32_t off = w.valid ? g_platform.utc_offset(w.utc) : 0;
  ui::Frame f{&g_ctrl.view(), &g_ctrl.settings(), g_ctrl.own(), w, off, millis(), &g_ctrl};
  uint32_t t0 = millis();
  ui::render(f);
  uint32_t ms = millis() - t0;
  if (ms > 100) board::elog("frame took %lu ms", (unsigned long)ms);
  // The screensaver: on after 10 minutes without a touch, fed the panel's own state (ss_host.h).
  ui::screensaver_service(g_ctrl.view(), g_ctrl.settings(), w, off, idle);
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

// ---- The presence sensor (Mike, 2026-11-11; docs/presence-sensor.md) ----
// One plain on/off sensor on the isolated input DI0 (expander pin EXIO0), upstairs only, and only
// once Settings says it is fitted. Read from the loop task about every 100 ms (one I2C read, never
// from the LVGL task), debounced by two agreeing reads, and handed to the platform; the controller
// asks for it there. Settings are read without the LVGL lock: single bytes.
static tc::PresenceFilter g_pres;
static uint32_t g_pres_next = 0;
static bool g_pres_was_on = false;

static void presence_tick() {
  uint32_t now = millis();
  if ((int32_t)(now - g_pres_next) < 0) return;
  g_pres_next = now + tc::kPresencePollMs;
  const tc::Settings &s = g_ctrl.settings();
  const bool on = s.presence_fitted && s.floor == tc::Floor::Upstairs;
  if (!on) {
    if (g_pres_was_on) {  // switched off: forget it
      g_pres.reset();
      g_platform.set_presence(false, -1);
      g_pres_was_on = false;
    }
    return;
  }
  g_pres_was_on = true;
  uint8_t pins = 0;
  bool ok = board::expander_read(0x04, pins);  // the pins' levels; bit 0 is DI0 (EXIO0)
  g_pres.feed(ok, (pins & 1) != 0, s.presence_active_high);
  g_platform.set_presence(g_pres.present(), g_pres.have_level() ? (g_pres.level() ? 1 : 0) : -1);
}

// "presence": the raw level, the debounced state and the minutes since it last saw anyone.
static void presence_report() {
  const tc::Settings &s = g_ctrl.settings();
  const tc::View::PresenceView &pv = g_ctrl.view().presence;
  Serial.printf("presence: %s (floor %s); sensor %s on DI0, active %s, hold %u s\n", s.presence_fitted ? "fitted" : "NOT fitted",
                tc::floor_word(s.floor), s.floor == tc::Floor::Upstairs ? "read" : "not read here",
                s.presence_active_high ? "high" : "low", (unsigned)s.presence_hold_s);
  int raw = g_platform.presence_raw();
  Serial.printf("presence: raw level %s, debounced %s%s, occupied %s; %lu failed reads\n",
                raw < 0 ? "not read" : raw ? "high" : "low", g_platform.presence_debounced() ? "present" : "absent",
                g_platform.presence_sim() >= 0 ? " (forced by presence sim)" : "", pv.occupied ? "yes" : "no",
                (unsigned long)g_pres.errors());
  if (pv.since_min < 0) Serial.println("presence: not seen since this start");
  else Serial.printf("presence: last seen %ld min ago\n", (long)pv.since_min);
}

// "rooms": the last ROOMS this panel queued for the gateway (Mike, 2026-11-22) and whether it was ACKed.
static void rooms_report() {
  const tc::Controller::RoomsInfo &r = g_ctrl.rooms_info();
  if (g_ctrl.settings().floor != tc::Floor::Upstairs) {
    Serial.println("rooms: only the upstairs panel sends them");
    return;
  }
  if (!r.any) {
    Serial.println("rooms: none sent yet (the first goes about 20 s after start-up)");
    return;
  }
  char up[24] = "no reading", down[24] = "no reading";
  if (r.flags & tc::kRoomsUpValid) snprintf(up, sizeof(up), "%.1f F %.1f %%", r.up_t / 10.0, r.up_h / 10.0);
  if (r.flags & tc::kRoomsDownValid) snprintf(down, sizeof(down), "%.1f F %.1f %%", r.down_t / 10.0, r.down_h / 10.0);
  Serial.printf("rooms: upstairs %s, downstairs %s, occupied %s\n", up, down,
                !(r.flags & tc::kRoomsOccKnown) ? "unknown (no sensor fitted)" : (r.flags & tc::kRoomsOccupied) ? "yes" : "no");
  Serial.printf("rooms: seq %u, %lu s ago, time %lu; %s\n", (unsigned)r.seq, (unsigned long)((millis() - r.at) / 1000),
                (unsigned long)r.time,
                r.acked ? "ACKed by the gateway" : r.failed ? "no ACK (trying again every minute)" : "waiting for the ACK");
}

// "presence sim 0|1|off": forces the sensor absent or present, or lets the real one back in.
static void presence_sim(int v) {
  g_platform.set_presence_sim(v < 0 ? -1 : (v ? 1 : 0));
  Serial.printf("presence sim: %s\n", v < 0 ? "off, the sensor again" : v ? "forced present" : "forced absent");
}

void setup() {
  Serial.begin(115200);
  // With a laptop attached but not reading, the USB serial port blocks each write for up to the TX
  // timeout. 0 is worse in core 3.2.0: HWCDC::write counts its retries down from the timeout, so 0
  // wraps round and the write waits until the laptop reads. That froze the sound task after a few
  // taps in the ringtone list, until a serial monitor opened (2026-10-24). 2 ms gives up at once,
  // and only the loop task writes to serial now (board::elog, board::elog_flush).
  Serial.setTxTimeoutMs(2);
  board::crash_guard_begin();  // before anything that could be what keeps crashing
  board::storage_begin();
  tc::Settings settings;
  tc::OwnState own;
  tc::DogState dog;
  tc::MailState mail;
  bool have_settings = board::load_settings(settings);  // an older saved layout is read too (settings_migrate.h)
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
  void *fb = nullptr;   // the screensaver draws straight into it (src/ui/ss_host.h)
  if (panel && esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) == ESP_OK) ui::screensaver_set_frame_buffer(fb);
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
    uint32_t comfort_n = 0;
    bool have_count = board::load_comfort_count(comfort_n);
    // Mike, 2026-11-07: the count can't go backwards. If the saved one was lost or is behind, the
    // number in comfort.csv's last complete line is the floor, so no answer number is reused.
    uint32_t file_n = board::calendar_last_number(tc::kComfortFile);
    if (file_n > comfort_n) {
      Serial.printf("[tcomm] comfort count: saved %lu, comfort.csv's last line %lu; using the larger\n",
                    (unsigned long)(have_count ? comfort_n : 0), (unsigned long)file_n);
      comfort_n = file_n;
      have_count = true;
    }
    if (have_count) g_ctrl.set_comfort_count(comfort_n);
    tc::ComfortPending pending[tc::kComfortPendingMax];  // answers and removals still waiting for upstairs
    int npending = 0;
    if (board::load_comfort_pending(pending, tc::kComfortPendingMax, npending) && npending > 0) {
      Serial.printf("[tcomm] comfort: %d answers or removals were waiting for upstairs\n", npending);
      g_ctrl.restore_comfort_pending(pending, npending);
    }
    tc::RequestState requests;  // the arrows held when the panel last saved them (requests.h)
    if (board::load_requests(requests)) g_ctrl.restore_requests(requests);
    // The tile messages (note.h, Mike 2026-11-07): each floor's text and times come back, so a restart
    // keeps a message that hasn't run out yet on both panels.
    tc::NoteState notes[2];
    if (board::load_notes(notes)) g_ctrl.restore_notes(notes);
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
  // Its 32 KB are only taken when it is on (strip_scratch_on; without the memory it stays off, the
  // log says so, and the line below shows "off").
  if (g_strips) strip_scratch_on();
  ui::set_copy_pause([] { delayMicroseconds(20); });  // between the 4 KB pieces of a picture's copy
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

// "nomail other" (or "nomail ack"): the other floor's panel taps its no-mail badge, as if by radio
// (Mike, 2026-11-18). Downstairs acknowledges a mark; upstairs marks it or takes it back.
static void nomail_from_other_floor() {
  tc::Floor other = tc::other_floor(g_ctrl.settings().floor);
  const tc::MailState &mine = g_ctrl.mail();
  if (other == tc::Floor::Downstairs && !mine.none) {
    Serial.println("nomail: nothing marked for downstairs to acknowledge");
    return;
  }
  tc::WallTime w = g_platform.wall();
  tc::Msg m;
  m.type = tc::MsgType::Mail;
  m.sender = tc::sender_id(other);
  m.seq = (uint16_t)(esp_random() | 1);
  m.u.mail.waiting = mine.none ? 0 : 2;  // 2 = no mail on the wire
  m.u.mail.since = w.valid ? w.utc : 0;
  m.u.mail.ver = mine.ver + 1;
  uint8_t buf[tc::kMaxMsg];
  size_t n = tc::encode(m, buf, sizeof(buf));
  if (n) g_ctrl.on_radio(buf, n);
  Serial.printf("nomail: %s taps theirs; now %s\n", tc::floor_lower(other), g_ctrl.mail().none ? "marked" : "not marked");
}

// Starts the screensaver and says with what seed, so a run can be repeated (ss seed N).
static void ss_start(uint32_t seed) {
  if (ui::screensaver_start(seed)) Serial.printf("screensaver: started, seed %lu (a touch or ss again stops it)\n", (unsigned long)seed);
  else Serial.println("screensaver: couldn't start (no frame buffer, or no memory)");
}
static bool g_ss_log = false;   // "ss log": its frame stats every 5 s while it runs

// "ss touch X Y": two whole numbers after the words.
static bool cmd_touch(const char *s, int &x, int &y) {
  if (strncmp(s, "ss touch ", 9)) return false;
  char *end = nullptr;
  const char *p = s + 9;
  x = (int)strtol(p, &end, 10);
  if (end == p) return false;
  p = end;
  y = (int)strtol(p, &end, 10);
  return end != p;
}

// The serial commands' numbers, read by hand: sscanf brought in newlib's scanf for these few lines,
// about 17 KB of code that these builds copy into PSRAM (2026-11-01).
// "word N": true, with v set, when s is the word, a space and a whole number (as sscanf's "%d").
static bool cmd_int(const char *s, const char *word, int &v) {
  size_t n = strlen(word);
  if (strncmp(s, word, n) || s[n] != ' ') return false;
  char *end = nullptr;
  long x = strtol(s + n + 1, &end, 10);
  if (end == s + n + 1) return false;
  v = (int)x;
  return true;
}

// "comfort": the answer count here, and the last 5 lines of comfort.csv (the whole file is read:
// fine for a debug command).
static void comfort_report() {
  Serial.printf("comfort: %lu answers here\n", (unsigned long)g_ctrl.comfort_count());
  size_t len = 0;
  uint8_t *buf = g_platform.read_file("comfort.csv", len);
  if (!buf || !len) {
    Serial.println("comfort.csv: nothing in it yet");
    if (buf) g_platform.free_big(buf);
    return;
  }
  while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) len--;
  size_t at = len;
  for (int lines = 0; at > 0 && lines < 5;) {
    at--;
    if (buf[at] == '\n' && ++lines == 5) at++;
  }
  Serial.write(buf + at, len - at);
  Serial.println();
  g_platform.free_big(buf);
}

// "req": the heat and cooling arrows held, oldest first, with the minutes since each was sent, and
// whether the "Action taken on temperature" line shows (requests.h). The ages come from the clock
// here, as the screen's own are.
static void req_report() {
  const tc::RequestState &r = g_ctrl.requests();
  const tc::WallTime w = g_platform.wall();
  Serial.printf("req: %d held\n", (int)r.n);
  for (int i = 0; i < r.n && i < tc::kMaxArrows; i++) {
    const tc::ReqArrow &a = r.a[i];
    char ago[32];
    if (!a.sent) snprintf(ago, sizeof(ago), "at an unknown time");
    else if (!w.valid) snprintf(ago, sizeof(ago), "(clock not set)");
    else snprintf(ago, sizeof(ago), "%lu min ago", (unsigned long)(w.utc > a.sent ? (w.utc - a.sent) / 60 : 0));
    Serial.printf("  %d: %s from the %s, sent %s\n", i + 1, tc::ask_code((tc::Ask)a.ask),
                  a.source == (uint8_t)tc::ReqSource::Tenant ? "tenant" : "predicted", ago);
  }
  Serial.printf("action taken on temperature: %s; tapped away: %s\n", g_ctrl.view().req.acted ? "showing" : "not showing",
                r.acted_dismissed ? "yes" : "no");
}

// "note": both floors' tile messages and the minutes each has left (note.h). The clock is read here,
// as the screen's own is. With no clock a message stays until it is tapped away (Mike, 2026-11-07).
static void note_report() {
  const tc::WallTime w = g_platform.wall();
  for (int i = 0; i < 2; i++) {
    const tc::Floor f = (tc::Floor)i;
    const tc::NoteState &n = g_ctrl.note(f);
    Serial.printf("note %s: ", tc::floor_lower(f));
    if (!n.text[0]) Serial.println("none");
    else if (!n.until) Serial.printf("\"%s\", until tapped away\n", n.text);
    else if (!w.valid) Serial.printf("\"%s\", (clock not set)\n", n.text);
    else if (n.until <= w.utc) Serial.printf("\"%s\", time up\n", n.text);
    else Serial.printf("\"%s\", %lu min left\n", n.text, (unsigned long)((n.until - w.utc + 59) / 60));
  }
}

// "note <text>": this panel's floor message for the longest time (4 hours, note.h). The model drops
// spaces at either end and keeps at most kNoteTextMax bytes.
static void note_send(const char *text) {
  while (*text == ' ') text++;
  if (!*text) {
    Serial.println("note: give the text, as note <text>, or note clear");
    return;
  }
  Serial.println(g_ctrl.set_note(text, tc::kNoteMaxS) ? "note: set for 4 hours" : "note: nothing changed");
}

// "time YYYY-MM-DD HH:MM[:SS]" into f: year, month, day, hour, minute, second (left as it was if
// not given).
static bool cmd_time(const char *s, int f[6]) {
  if (strncmp(s, "time ", 5)) return false;
  const char *p = s + 5;
  static const char kAfter[6] = {'-', '-', ' ', ':', ':', 0};  // ' ': spaces, which strtol skips
  for (int i = 0; i < 6; i++) {
    char *end = nullptr;
    long x = strtol(p, &end, 10);
    if (end == p) return i == 5;
    f[i] = (int)x;
    p = end;
    if (kAfter[i] == '-' || kAfter[i] == ':') {
      if (*p != kAfter[i]) return i == 4;  // no seconds
      p++;
    }
  }
  return true;
}

// "display X": the first character after the word and any spaces.
static bool cmd_char(const char *s, const char *word, char &ch) {
  size_t n = strlen(word);
  if (strncmp(s, word, n) || s[n] != ' ') return false;
  const char *p = s + n;
  while (*p == ' ') p++;
  ch = *p;
  return ch != 0;
}

// Voice notes (piece A, board_audio.cpp). To move into board.h when the shared interfaces next open.
namespace board {
void voice_dump(uint32_t secs, int gain_db);
}

// "reach N [ms]" and "reach ping N [ms]" (piece R): the count and the spacing (30 ms when not given).
static bool cmd_reach(const char *s, int &n, int &ms, bool &ping) {
  if (strncmp(s, "reach ", 6)) return false;
  const char *p = s + 6;
  ping = !strncmp(p, "ping ", 5);
  if (ping) p += 5;
  char *end = nullptr;
  long a = strtol(p, &end, 10);
  if (end == p || a < 1) return false;
  n = (int)a;
  ms = 30;
  p = end;
  long b = strtol(p, &end, 10);
  if (end != p) ms = (int)b;
  return true;
}

static void command(char *s) {
  int v, t[6] = {0, 0, 0, 0, 0, 0};
  char ch;
  if (!strcmp(s, "help")) {
    Serial.println("status N (0-4) | bell | dog | time YYYY-MM-DD HH:MM[:SS] (local) | demo N (0-7) | info | bench");
    Serial.printf("ring N (0-%d, the place in the list): choose a ringtone and play it, as a tap does\n",
                  tc::kRingtoneCount - 1);
    Serial.println("sound tests: play N (0 chime, 1 soft, 2 come on, 3 not now) | listen | amp hold 0/1 | codec reopen");
    Serial.println("sound checks: log (the last 64 events) | regs (amp pin and codec registers, into the log)");
    Serial.println("voice notes: sound ding (the arrival ding, at the soft chime level) | voice dump [seconds 1-3 [gain dB 0-36]] "
                   "(records through the real capture path, sends nothing, prints the ADPCM chunks as hex)");
    Serial.println("voice notes sound: mic raw [ms 50-1500, default 1000] (the raw 44.1 kHz stereo microphone blocks as hex, "
                   "with the I2S and ES7210 settings; tools/mic_raw_wav.py makes a WAV) | voice codec 0|1|2 (0 by the "
                   "gateway, the default: mu-law when it takes it, else ADPCM; 1 mu-law; 2 ADPCM; until restart) | voice boost N (dB before encoding, 0-30, default 15; until restart) | voice treble N (high shelf at 1500 Hz, 0-20 dB, default 10, 0 = off; until restart)");
    Serial.println("bus test: i2c test (1000 reads each of the codec and touch chip IDs, at 400 and 100 kHz)");
    Serial.println("display tests: display A-D (as Settings > Test) | pattern (toggle) | pclk N (MHz, 8-20, until restart)");
    Serial.println("SD card: sd (what's there) | sd format yes (FAT32, erasing it, only if it won't mount as it is)");
    Serial.println("radio (ESP-NOW counts, the last frames, and what the outside tile and dog pill show)");
    Serial.println("voice notes link: voice (the route, the last job's numbers, the inbox count) | voice route auto|direct|relay "
                   "(downstairs, until restart) | reach N [ms] (N frames to the gateway, MAC-level acknowledgements) | "
                   "reach ping N [ms] (N VOICE_PINGs, answers back end to end; needs the C6 0.5)");
    Serial.println("shot (the screen as raw RGB565, about a second; tools/shot2png.py makes a PNG)");
    Serial.println("mail (tap this panel's mail badge) | mail other (the other floor taps theirs, as if by radio)");
    Serial.println("nomail (tap this panel's no-mail badge, quiet) | nomail other or nomail ack (the other floor taps theirs, as if by radio)");
    Serial.println("comfort (answers here and the last 5 lines of comfort.csv) | comfort 0-4 (the tenant taps MUCH TOO HOT, TOO WARM, "
                   "JUST RIGHT, TOO COOL, MUCH TOO COLD; downstairs only) | comfort remove N (answer N marked removed, as the chart's REMOVE)");
    Serial.println("req (the arrows held) | req send 0-3 (the tenant sends urgent heat, mild heat, mild cooling, urgent cooling; "
                   "downstairs only) | req tap (a tap on the arrows) | req acted (a tap on Action taken)");
    Serial.println("note (both floors' tile messages, and the minutes left) | note <text> (this panel's floor's message, 4 hours) | note clear (take it away)");
    Serial.println("gear (as a tap on the gear: Settings rolls down or up) | knock (the other floor rings, as if by radio)");
    Serial.println("presence (the presence sensor on DI0: raw level, debounced state, minutes since last seen; upstairs, once "
                   "fitted in Settings) | presence sim 0 / 1 / off (force absent or present for tests, or the real sensor again)");
    Serial.println("rooms (the last ROOMS for Apple Home sent to the gateway, upstairs only, and whether it was ACKed)");
    Serial.println("update both (the other panel and this one restart into the Wi-Fi updater, as Settings > Update over Wi-Fi > BOTH PANELS does)");
    Serial.println("update (restart into the Wi-Fi updater, as Settings > Update over Wi-Fi does)");
    Serial.println("phase N (the sky as if it were 0 night, 1 dawn, 2 morning, 3 midday, 4 golden hour, 5 dusk: the palette, "
                   "the backlight and the screensaver follow; 6 = the clock again)");
    Serial.println("screensaver (on by itself after 10 min without a touch): ss (on or off now, no reveal) | ss seed N | "
                   "ss touch X Y (as a touch there: the reveal) | ss scene F (0 any, 1 evening, 2 morning, 3 midday, 4 lookout)");
    Serial.println("  ss mood N (0 night .. 5 dusk, or hundredths: 304 = 3.04; -1 the clock's) | ss bell (the hiker's reaction only; knock rings for real) | "
                   "ss auto 0/1 | ss idle N (seconds until restart; 0 = the Settings value) | ss inputs | ss stats | ss log (stats every 5 s)");
    Serial.println("  ss weather N (0 clear, 1 rain, 2 snow, 3 wind, 4 heat, 5 fog, 6 chill, 7 storm; -1 the outside reading; "
                   "from the next loop: ss seed N) | ss nightgain N (the night scene's brightness in tenths, 16 = 1.6; "
                   "0 = the PC look; until restart) | ss lookout N (0 round the real sunset, 1 on, 2 off) | "
                   "ss spread N (bands a frame for a medium palette step; 0 = at once, the default) | "
                   "ss carry [N] (the word the next run starts from: mushrooms, tai chi; until restart)");
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
  if (!strncmp(s, "sd fail ", 8)) {  // test only: 1 the card fails for good, 2 fails once, 0 back to normal
    board::storage_test_fail(atoi(s + 8));
    Serial.printf("sd fail %d\n", atoi(s + 8));
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
  if (cmd_int(s, "amp hold", v)) {
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
  if (!strcmp(s, "sound ding")) {  // the voice-message arrival ding (G5, one strike) at the soft chime's level
    const tc::Settings &st = g_ctrl.settings();
    g_platform.play(tc::Sound::Ding, st.volume > st.soft_chime_drop ? (uint8_t)(st.volume - st.soft_chime_drop) : (uint8_t)0);
    return;
  }
  if (!strncmp(s, "mic raw", 7) && (s[7] == 0 || s[7] == ' ')) {  // takes seconds, outside the LVGL lock
    int ms = 1000;
    if (sscanf(s + 7, "%d", &ms) < 1) ms = 1000;
    board::mic_raw_dump((uint32_t)(ms < 50 ? 50 : ms > 1500 ? 1500 : ms));
    return;
  }
  if (!strncmp(s, "voice boost", 11) && (s[11] == 0 || s[11] == ' ')) {
    int db = -1;
    if (sscanf(s + 11, "%d", &db) == 1) {
      if (db < 0 || db > tc::kVoiceBoostMaxDb) Serial.printf("voice boost: 0 to %d dB\n", tc::kVoiceBoostMaxDb);
      else board::voice_set_boost(db);
    }
    Serial.printf("voice boost: %d dB before encoding, soft limiter above -7 dBFS (from the next recording, until restart)\n",
                  board::voice_boost());
    return;
  }
  if (!strncmp(s, "voice treble", 12) && (s[12] == 0 || s[12] == ' ')) {
    int db = -1;
    if (sscanf(s + 12, "%d", &db) == 1) {
      if (db < 0 || db > tc::kVoiceTrebleMaxDb) Serial.printf("voice treble: 0 to %d dB\n", tc::kVoiceTrebleMaxDb);
      else board::voice_set_treble(db);
    }
    Serial.printf("voice treble: %d dB high shelf at %d Hz before the boost, 0 = off (from the next recording, until restart)\n",
                  board::voice_treble(), (int)tc::kVoiceTrebleHz);
    return;
  }
  if (!strncmp(s, "voice dump", 10) && (s[10] == 0 || s[10] == ' ')) {  // takes seconds, outside the LVGL lock
    int secs = 3, gain = -1;
    int got = sscanf(s + 10, "%d %d", &secs, &gain);
    if (got < 1) secs = 3;
    if (got < 2 || gain < 0 || gain > 36) gain = -1;
    board::voice_dump((uint32_t)(secs < 1 ? 1 : secs > 3 ? 3 : secs), gain);
    return;
  }
  {
    int rn = 0, rms = 0;
    bool rping = false;
    if (cmd_reach(s, rn, rms, rping)) {  // N x ms, outside the LVGL lock (DESIGN 13.0)
      board::radio_reach(rn, rms, rping);
      return;
    }
  }
  if (!lvgl_port_lock(1000)) return;
  if (cmd_int(s, "status", v) && tc::status_valid((uint8_t)v)) g_ctrl.choose_status((tc::Status)v);
  else if (!strcmp(s, "bell")) g_ctrl.tap_bell();
  else if (!strcmp(s, "dog")) g_ctrl.tap_dog();
  else if (!strcmp(s, "mail")) Serial.println(g_ctrl.tap_mail() ? "mail: tapped" : "mail: nothing waiting to acknowledge");
  else if (!strcmp(s, "mail other")) mail_from_other_floor();
  else if (!strcmp(s, "nomail")) Serial.println(g_ctrl.tap_nomail() ? "nomail: tapped" : "nomail: nothing marked to acknowledge");
  else if (!strcmp(s, "nomail other") || !strcmp(s, "nomail ack")) nomail_from_other_floor();
  else if (!strcmp(s, "comfort")) comfort_report();
  else if (cmd_int(s, "comfort remove", v) && v >= 0) {  // Mike, 2026-11-06: the chart's REMOVE, by number
    Serial.printf("comfort remove %d: %s\n", v, g_ctrl.comfort_remove((uint32_t)v) ? "taken" : "not taken (upstairs, or no such answer)");
  }
  else if (!strcmp(s, "req")) req_report();
  else if (cmd_int(s, "req send", v) && v >= 0 && v <= 3) {
    // The answer that goes with each arrow, as the survey picks them (docs/temperature-requests.md).
    static const tc::Feel kFeel[4] = {tc::Feel::MuchTooCold, tc::Feel::TooCool, tc::Feel::TooWarm, tc::Feel::MuchTooHot};
    static const tc::Ask kAsk[4] = {tc::Ask::UrgentHeat, tc::Ask::MildHeat, tc::Ask::MildCool, tc::Ask::UrgentCool};
    bool ok = g_ctrl.comfort_answer(kFeel[v], kAsk[v], tc::AskSent::Send);
    Serial.printf("req: %s sent: %s\n", tc::ask_code(kAsk[v]), ok ? "taken" : "not taken (upstairs, or the survey isn't asking)");
  } else if (!strcmp(s, "req tap"))
    Serial.println(g_ctrl.tap_requests() ? "req tap: taken" : "req tap: no arrows showing");
  else if (!strcmp(s, "req acted"))
    Serial.println(g_ctrl.tap_acted() ? "req acted: put away" : "req acted: nothing to put away");
  else if (cmd_int(s, "comfort", v) && tc::feel_valid((uint8_t)v))
    Serial.printf("comfort: %s\n", g_ctrl.comfort_answer((tc::Feel)v) ? "answer taken" : "not taken (upstairs, or the survey isn't asking)");
  else if (!strcmp(s, "note")) note_report();
  else if (!strcmp(s, "note clear")) Serial.println(g_ctrl.clear_note() ? "note: taken away" : "note: nothing showing here to take away");
  else if (!strncmp(s, "note ", 5)) note_send(s + 5);
  else if (!strcmp(s, "presence")) presence_report();
  else if (!strcmp(s, "rooms")) rooms_report();
  else if (!strcmp(s, "presence sim 0")) presence_sim(0);
  else if (!strcmp(s, "presence sim 1")) presence_sim(1);
  else if (!strcmp(s, "presence sim off")) presence_sim(-1);
  else if (!strcmp(s, "gear")) ui::tap_gear();
  else if (!strcmp(s, "update both"))  // Mike, 2026-11-07: the same as Settings > Update over Wi-Fi > BOTH PANELS
    Serial.println(g_ctrl.update_both() ? "update both: asking the other panel, then this one restarts" : "update both: this panel has no updater installed");
  else if (!strcmp(s, "update")) board::restart_into_updater();
  else if (!strcmp(s, "knock")) knock_from_other_floor();
  else if (cmd_int(s, "phase", v) && v >= 0 && v <= 6) {
    g_ctrl.force_phase(v);   // the controller's test hook: 6 (or anything past 5) follows the clock
    static const char *const kPhase[7] = {"night", "dawn", "morning", "midday", "golden hour", "dusk", "the clock's"};
    Serial.printf("sky: %s\n", kPhase[v]);
  } else if (cmd_int(s, "ss spread", v)) {
    ui::screensaver_set_spread_medium(v);
    Serial.printf("screensaver: a medium palette step washes down %d bands a frame\n", ui::screensaver_spread_medium());
  } else if (cmd_int(s, "ss lookout", v)) {
    ui::screensaver_set_lookout(v);
    static const char *const kLook[3] = {"round the real sunset, once an evening", "on (every loop)", "off"};
    Serial.printf("screensaver: the Lookout %s\n", kLook[v == 1 || v == 2 ? v : 0]);
  } else if (cmd_int(s, "ss nightgain", v)) {
    ui::screensaver_set_night_gain((float)v / 10.f);
    Serial.printf("screensaver: night gain %.1f\n", (double)ui::screensaver_night_gain());
  } else if (cmd_int(s, "ss carry", v)) {
    ui::screensaver_set_carry((uint16_t)(v & 0xFFFF));
    Serial.printf("screensaver: carry word %u from the next start\n", (unsigned)ui::screensaver_carry());
  } else if (!strcmp(s, "ss carry")) {
    const unsigned c = ui::screensaver_carry();
    Serial.printf("screensaver: carry word %u (mushroom patch stage %u%s; tai chi byte %u, done this morning %s)\n", c,
                  c & 7u, (c & 0x40u) ? ", soaked" : "", c >> 8, (c & 0x400u) ? "yes" : "no");
  } else if (cmd_int(s, "ss weather", v)) {
    ui::screensaver_set_weather(v);
    Serial.printf("screensaver: weather %d%s (from the next loop)\n", v < 0 || v > 7 ? -1 : v, v < 0 || v > 7 ? ", the outside reading's" : "");
  } else if (!strcmp(s, "ss")) {
    if (ui::screensaver_running()) {
      ui::screensaver_stop();
      Serial.println("screensaver: stopped");
    } else {
      ss_start(esp_random() & 0x7fffffffu);   // in range for ss seed N
    }
  } else if (cmd_int(s, "ss seed", v)) {
    if (ui::screensaver_running()) ui::screensaver_stop();
    ss_start((uint32_t)v);
  } else if (cmd_int(s, "ss scene", v)) {
    ui::screensaver_set_scene(v);
    Serial.printf("screensaver: scene %d from the next start\n", v);
  } else if (!strcmp(s, "ss bell")) ui::screensaver_doorbell();
  else if (cmd_int(s, "ss mood", v)) ui::screensaver_set_mood(v >= 100 ? (float)v / 100.f : (float)v);   // 304: 3.04
  else if (cmd_touch(s, v, t[0])) ui::screensaver_touch(v, t[0]);
  else if (cmd_int(s, "ss auto", v)) {
    ui::screensaver_set_auto(v != 0);
    Serial.printf("screensaver: %s\n", v ? "starts on its own after the idle time" : "only starts by hand");
  } else if (cmd_int(s, "ss idle", v)) {
    ui::screensaver_set_idle(v < 0 ? 0 : (uint32_t)v);
    if (v > 0) Serial.printf("screensaver: starts after %lu s without a touch (until restart)\n", (unsigned long)ui::screensaver_idle());
    else Serial.println("screensaver: starts after the wait in Settings > SCREENSAVER");
  } else if (!strcmp(s, "ss log")) {
    g_ss_log = !g_ss_log;
    Serial.printf("screensaver: frame stats every 5 s %s\n", g_ss_log ? "on" : "off");
  } else if (!strcmp(s, "ss inputs")) {
    char b[400];
    ui::screensaver_inputs(b, sizeof b);
    Serial.println(b);
  } else if (!strcmp(s, "ss stats")) {
    char b[700];
    ui::screensaver_stats(b, sizeof b);
    Serial.println(b);
  }
  else if (cmd_time(s, t))
    g_app.set_clock_local(tc::seconds_from_civil(t[0], t[1], t[2], t[3], t[4], t[5]));
  else if (cmd_int(s, "demo", v) && v >= 0 && v <= 7) g_app.demo((ui::DemoAction)v);
  else if (cmd_int(s, "ring", v) && v >= 0 && v < tc::kRingtoneCount) {
    tc::Settings st = g_ctrl.settings();
    st.ringtone = tc::ringtone_id(v);  // N is the place in the list
    g_app.apply_settings(st);
    g_app.preview(tc::Sound::Chime, st.volume);
  }
  else if (cmd_int(s, "play", v) && v >= 0 && v <= 3)
    g_platform.preview((tc::Sound)v, g_ctrl.settings().volume);  // the ringtone chosen stays as it is
  else if (!strcmp(s, "pattern")) toggle_pattern();
  else if (cmd_char(s, "display", ch) && toupper(ch) >= 'A' && toupper(ch) < 'A' + kDisplayPresetCount)
    g_platform.display_test_set(toupper(ch) - 'A');  // restarts the panel when the buffer size changes
  else if (cmd_int(s, "pclk", v) && v >= 8 && v <= 20) {
    // Takes effect at the next frame; back to 16 MHz at the next boot.
    esp_err_t e = esp_lcd_rgb_panel_set_pclk(g_panel, (uint32_t)v * 1000000u);
    Serial.printf("pixel clock %d MHz: %s\n", v, esp_err_to_name(e));
  }
  else if (!strcmp(s, "cold test on") || !strcmp(s, "cold test off")) {
    g_ctrl.set_cold_test(s[11] == 'n');
    Serial.printf("cold test: danger cold %s (not saved)\n", g_ctrl.cold_test() ? "forced on while an outside reading shows" : "back to the gateway's reading");
  } else if (!strncmp(s, "voice test ", 11)) {
    g_ctrl.voice_test_arrival(s + 11);
    Serial.println("voice test: shown as an arrival from the other floor (not sent, no phone push)");
  } else if (!strcmp(s, "voice")) {
    char b[448];
    g_ctrl.voice_report(b, sizeof b);
    Serial.println(b);
  } else if (!strncmp(s, "voice codec", 11) && (s[11] == 0 || s[11] == ' ')) {
    int c = 0;
    if (sscanf(s + 11, "%d", &c) == 1) {
      if (c == 0) g_ctrl.set_voice_codec_auto();
      else if (c == 1) g_ctrl.set_voice_codec(tc::VoiceCodec::Mulaw);
      else if (c == 2) g_ctrl.set_voice_codec(tc::VoiceCodec::Adpcm);
      else Serial.println("voice codec: 0 (by the gateway), 1 (mu-law) or 2 (ADPCM)");
    }
    const bool mu = g_ctrl.voice_codec() == tc::VoiceCodec::Mulaw;
    Serial.printf("voice codec: %s, %d chunks a clip (from the next recording), %s\n", mu ? "1 mu-law" : "2 ADPCM",
                  tc::voice_clip_chunks((uint8_t)g_ctrl.voice_codec()),
                  g_ctrl.voice_codec_by_hand() ? "set by hand until restart (voice codec 0: by the gateway)"
                  : g_ctrl.voice_gateway_mulaw() ? "the gateway takes mu-law" : "the gateway takes ADPCM only (or not heard)");
  } else if (!strncmp(s, "voice route", 11) && (s[11] == 0 || s[11] == ' ')) {
    const char *r = s + 11;
    while (*r == ' ') r++;
    if (!strcmp(r, "auto")) g_ctrl.set_voice_route(tc::Controller::VoiceRoute::Auto);
    else if (!strcmp(r, "direct")) g_ctrl.set_voice_route(tc::Controller::VoiceRoute::Direct);
    else if (!strcmp(r, "relay")) g_ctrl.set_voice_route(tc::Controller::VoiceRoute::Relay);
    else if (*r) Serial.println("voice route: auto, direct or relay");
    static const char *const kRoute[3] = {"auto", "direct", "relay"};
    Serial.printf("voice route: %s%s\n", kRoute[(int)g_ctrl.voice_route()],
                  g_ctrl.settings().floor == tc::Floor::Upstairs ? " (upstairs always talks to the gateway directly)" : "");
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
    Serial.printf("nomail: %s\n", g_ctrl.mail().none ? "marked" : "not marked");
    Serial.printf("dog pill: %s; other floor's tile: %s\n", vw.dog.pill[0] ? vw.dog.pill : "(none)",
                  vw.floor[(int)vw.me == 0 ? 1 : 0].word);
  }
  else if (!strcmp(s, "info")) {
    tc::WallTime w = g_platform.wall();
    // utc: the clock in seconds since 1970, for tools/two_panels.ps1 to compare with the PC's and
    // the other panel's.
    Serial.printf("floor %s, status %s, clock %s (quality %u, utc %lu), free heap %u, free PSRAM %u\n",
                  tc::floor_word(g_ctrl.settings().floor), tc::status_word(g_ctrl.own().status), w.valid ? "set" : "NOT SET",
                  w.quality, (unsigned long)w.utc, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    // The memory audit's savings (2026-11-12), to measure on a panel: compare with the screensaver
    // running (ss stats has the same figures) and with danger cold showing.
    char ice_held[32] = "not held";
    if (ui::icicles_held()) snprintf(ice_held, sizeof(ice_held), "held (%u bytes)", (unsigned)ui::icicles_bytes());
    Serial.printf("memory: internal free %u (largest block %u), PSRAM free %u (largest block %u); spare picture %s; "
                  "strip drawing scratch %s; icicles %s\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                  ui::spare_lent() ? "lent to the screensaver" : "home", g_scratch ? "held (32,768 bytes)" : "not held",
                  ice_held);
    // Field fix 2026-11-20: the controller's larger state (voice notes and the view) should be in PSRAM.
    Serial.printf("memory: controller %u bytes in internal RAM, its state block in %s\n", (unsigned)sizeof(g_ctrl),
                  esp_ptr_external_ram(&g_ctrl.view()) ? "PSRAM" : "INTERNAL RAM (PSRAM had no room)");
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
  static bool healthy = false;
  if (!healthy && millis() > 60000) {  // a minute up: whatever crashed before is behind us
    healthy = true;
    board::crash_guard_healthy();
  }
#if TCOMM_SENSOR
  sensor_tick();
#endif
  presence_tick();
  static uint32_t ss_log = 0;   // the screensaver's frame stats, every 5 s while it runs (ss log)
  if (g_ss_log && ui::screensaver_running() && millis() - ss_log >= 5000) {
    ss_log = millis();
    char b[700];
    ui::screensaver_stats(b, sizeof b);
    Serial.println(b);
  }
  delay(20);
}
