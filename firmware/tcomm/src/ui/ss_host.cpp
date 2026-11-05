// The campsite screensaver on the panel. See ss_host.h. Panel only: the PC simulator
// (tools/build_sim.sh) compiles src/ui/*.cpp and never starts it.
#include "ss_host.h"
#if defined(ESP_PLATFORM)

#include <stdio.h>
#include <string.h>

#include <house_weather.h>
#include <ss_app.h>

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "../app/controller.h"
#include "../board/board.h"
#include "../core/brightness.h"
#include "../core/sky.h"
#include "../ws/lvgl_port/lvgl_port.h"

namespace ui {

namespace {

static_assert(sizeof(lv_color_t) == sizeof(ss::Pixel), "LVGL draws RGB565, as the screensaver does");

constexpr int64_t kTickUs = 1000000 / SS_TICK_HZ;   // 40 ms
constexpr int kMaxCatchUp = 4;                      // ticks one frame may run to catch up; past that the backlog is dropped
constexpr uint32_t kTimerMs = 10;                   // the frame timer polls this often; a frame happens when a tick is due
constexpr uint32_t kInputsMs = 1000;                // the inputs are rebuilt this often
constexpr uint32_t kMoodMs = 30000;                 // the time of day, this often
constexpr int kMoodBlendMin = 12;                   // a sky phase change is spread over 12 minutes either side
constexpr int64_t kRevealGiveUpUs = 2000000;        // a reveal that hasn't finished by now just ends

void *g_fb = nullptr;
ss::App *g_app = nullptr;
void *g_mem = nullptr;              // SS_APP_BYTES in PSRAM
lv_timer_t *g_timer = nullptr;
ss::Inputs g_in;
bool g_in_ready = false;
bool g_lent = false;                // the fast block is LVGL's draw buffer (else core took one from PSRAM)
int64_t g_last_us = 0, g_backlog_us = 0;

// The touch hook and the frame timer both run in the LVGL task.
bool g_touch = false;               // a press while it runs: reveal from (g_tx, g_ty) at the next frame
int16_t g_tx = 0, g_ty = 0;
bool g_finger = false;              // the waking finger is down: its touches aren't presses
bool g_dark_stop = false;           // the screen went dark: stop at the next frame, no reveal

struct Reveal {
  bool on;
  int64_t t0_us, last_us, capture_us;
  uint32_t frames;
};
Reveal g_rv = {};
ss::Pixel *g_capture = nullptr;     // where captureFlush puts LVGL's pixels

// The main screen's copy (Mike, 5 Nov: "Keep a ready copy"). While the scene runs, LVGL keeps
// drawing the main screen's changes, a slice after each scene frame, straight into this full-frame copy in
// PSRAM (LVGL's direct mode) instead of the panel. A touch starts the circle from it at once, and
// a stop puts it on the panel as it is: no full redraw (244 ms on this panel) either way. Without
// the memory for it (768,000 bytes), LVGL draws nothing while the scene runs and the main screen
// is drawn at the touch instead (captureMainScreen).
// The main screen redraws all of itself about once a minute (244-262 ms here), which in one go
// would freeze the scene for a quarter second. So LVGL never redraws on its own while the scene
// runs: copyPump takes what it has marked and has it draw at most kSlicePx of it after each scene
// frame (about 12 ms: a whole screen over about 20 frames). A touch or a stop draws what's left.
constexpr size_t kFrameBytes = (size_t)SS_WIDTH * SS_HEIGHT * sizeof(ss::Pixel);
constexpr uint32_t kNeverMs = 0x7FFFFFFF;
constexpr uint32_t kSlicePx = SS_WIDTH * 24;
constexpr int kPendMax = 16;   // within LV_INV_BUF_SIZE (32)
ss::Pixel *g_copy = nullptr;
lv_disp_draw_buf_t g_copy_db;
lv_disp_draw_buf_t *g_lvgl_db = nullptr;   // LVGL's own draw buffer while the copy is in use
void (*g_lvgl_flush)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *) = nullptr;
uint32_t g_lvgl_period = 0;
lv_area_t g_pend[kPendMax];                // what LVGL has marked and not yet drawn into the copy
int g_npend = 0;
uint32_t g_copy_refr = 0, g_copy_px = 0;   // LVGL's redraws into the copy, for the stats
int64_t g_copy_us = 0;

// Starting on its own, and the inputs.
bool g_auto = true;
uint32_t g_idle_set_ms = 0;         // ss idle N (serial, until restart); 0: Settings > SCREENSAVER
uint32_t g_idle_ms = 10 * 60000u;   // the wait in use: Settings' screensaver_min unless set by hand
uint32_t g_idle_now = 0;            // the last idle time seen, for screensaver_inputs
uint32_t g_lit_since = 0;           // the screen has been lit since (a dark screen keeps moving it)
uint32_t g_stopped_at = 0;
bool g_stopped_once = false;
float g_mood_override = -1.f;
uint8_t g_scene = 0;
float g_mood = 3.f;
bool g_mood_known = false;
uint32_t g_mood_at = 0, g_inputs_at = 0;
uint16_t g_shakes = 0;
bool g_shakes_known = false;
uint32_t g_seed = 0;

// Stats since the last screensaver_stats call.
struct Stats {
  uint32_t frames, ticks, strips, dropped;
  int64_t tick_us, render_us, worst_us;
  int64_t since_us;
};
Stats g_st;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// The renderer's clock for its time split (appProfile): copy in, layers, copy out.
uint32_t clockUs() { return (uint32_t)esp_timer_get_time(); }

void *psramAlloc(size_t n) { return heap_caps_aligned_alloc(16, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void psramFree(void *p) { heap_caps_free(p); }

ss::HostAlloc hostAlloc() {
  ss::HostAlloc a;
  a.alloc_large = psramAlloc;
  a.free_large = psramFree;
  a.fast = nullptr;
  g_lent = false;
  // LVGL's draw buffer: internal RAM, idle while LVGL draws into the copy (or nothing). Partial
  // mode, one buffer (lvgl_port.h LVGL_PORT_BUFFER_HEIGHT 40: 64,000 bytes).
  lv_disp_t *d = lv_disp_get_default();
  lv_disp_draw_buf_t *db = g_lvgl_db ? g_lvgl_db : (d ? lv_disp_get_draw_buf(d) : nullptr);
  if (db && db->buf1 && db->size * sizeof(lv_color_t) >= SS_FAST_BYTES && ((uintptr_t)db->buf1 & 3u) == 0) {
    a.fast = db->buf1;
    g_lent = true;
  }
  return a;
}

// ---- The inputs ----

// The palette's mood (palette.h: 0 night, 1 dawn, 2 morning, 3 day, 4 golden hour, 5 dusk) from the
// panel's own sky phases (core/sky.h), which are the same six. Within a phase it's the phase; across
// a change it moves evenly over 2 x kMoodBlendMin minutes (the phase averaged over that window), so
// the scene's light changes gradually. A phase forced for testing, or no clock: the view's phase.
float phaseMood(const tc::View &v, const tc::Settings &s, const tc::WallTime &w, int32_t off) {
  const int c = (int)v.phase;
  if (!w.valid) return (float)c;
  if ((int)tc::sky_at(w.utc, off, s.latitude, s.longitude).phase != c) return (float)c;
  float sum = 0.f;
  int n = 0;
  for (int k = -kMoodBlendMin; k <= kMoodBlendMin; ++k) {
    int p = (int)tc::sky_at((int64_t)w.utc + 60 * k, off, s.latitude, s.longitude).phase;
    if (p - c > 3) p -= 6;   // dusk -> night is 5 -> 6, not 5 -> 0
    else if (c - p > 3) p += 6;
    sum += (float)p;
    ++n;
  }
  float m = sum / (float)n;
  if (m < 0.f) m += 6.f;
  if (m >= 6.f) m -= 6.f;
  return m;
}

// Everything the scene takes from the panel (ss_app.h Inputs). Stage 9 (night) and 10 (the Lookout
// at the real sunset) fill night_dim and lookout_window; until then the core's own night rule
// follows night_mode and the Lookout comes up only when the loop picks it.
ss::Inputs buildInputs(const tc::View &v) {
  ss::Inputs in;
  memset(&in, 0, sizeof in);   // ss_app.h: zero all of it first
  in.mood = g_mood_override >= 0.f ? g_mood_override : g_mood;
  ss::OutsideReading r;
  r.valid = v.outside.valid;
  r.forecast = (int)v.outside.code;
  r.temp_f = (float)v.outside.temp_tenths / 10.f;
  r.humidity = (float)v.outside.hum_pct;
  r.danger_heat = v.outside.heat;
  r.danger_cold = v.outside.cold;
  const ss::SceneWeather sw = ss::sceneWeatherFromOutside(r);
  in.weather_code = sw.code;
  in.have_outside = sw.have_outside;
  in.weather.overcast = sw.overcast;
  in.weather.fog = sw.fog;
  in.weather.night_dim = 0.f;
  const tc::FloorView &f = v.floor[(int)tc::other_floor(v.me)];
  in.other_status = f.no_signal ? 5 : (int)f.status;
  in.dog_out = (int)v.dog.out;
  in.chip = true;
  in.night_mode = v.mode == tc::Mode::Night;
  in.lookout_window = false;
  in.spine_force = g_scene;
  return in;
}

// ---- Running and stopping ----

// The touch hook (lvgl_port.h): while it runs, every touch is its own; after, the waking finger
// stays its own until lifted, so the screen under it isn't pressed.
bool touchHook(bool pressed, int x, int y) {
  if (g_app) {
    if (pressed) {
      if (!g_finger && !g_touch && !g_rv.on) {
        g_tx = (int16_t)(x < 0 ? 0 : (x >= SS_WIDTH ? SS_WIDTH - 1 : x));
        g_ty = (int16_t)(y < 0 ? 0 : (y >= SS_HEIGHT ? SS_HEIGHT - 1 : y));
        g_touch = true;
      }
      g_finger = true;
    } else {
      g_finger = false;
    }
    return true;
  }
  if (g_finger) {
    if (!pressed) g_finger = false;
    return true;
  }
  return false;
}

// LVGL's flush while it draws into the copy: direct mode has already put the pixels in place.
void copyFlush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *) {
  g_copy_refr++;
  g_copy_px += (uint32_t)lv_area_get_size(a);
  lv_disp_flush_ready(drv);
}

// Starts the copy: LVGL first draws whatever it still owes the panel, so the frame buffer is the
// main screen exactly as LVGL has it; that goes into the copy, and LVGL draws into the copy from
// then on. False (and nothing changed) without the memory.
bool copyOn(lv_disp_t *d) {
  ss::Pixel *c = static_cast<ss::Pixel *>(psramAlloc(kFrameBytes));
  if (!c) return false;
  lv_refr_now(d);
  memcpy(c, g_fb, kFrameBytes);
  lv_disp_drv_t *drv = d->driver;
  g_lvgl_db = drv->draw_buf;
  g_lvgl_flush = drv->flush_cb;
  lv_disp_draw_buf_init(&g_copy_db, c, nullptr, (uint32_t)SS_WIDTH * SS_HEIGHT);
  drv->draw_buf = &g_copy_db;
  drv->direct_mode = 1;
  drv->flush_cb = copyFlush;
  if (d->refr_timer) {   // LVGL doesn't redraw on its own meanwhile (copyPump does it in slices)
    g_lvgl_period = d->refr_timer->period;
    lv_timer_set_period(d->refr_timer, kNeverMs);
    lv_timer_reset(d->refr_timer);
  }
  g_copy = c;
  g_npend = 0;
  g_copy_refr = g_copy_px = 0;
  g_copy_us = 0;
  return true;
}

void pendPush(const lv_area_t &a) {
  for (int i = 0; i < g_npend; ++i)
    if (_lv_area_is_in(&a, &g_pend[i], 0)) return;
  if (g_npend == kPendMax) {   // too many pieces: the whole screen
    lv_area_set(&g_pend[0], 0, 0, SS_WIDTH - 1, SS_HEIGHT - 1);
    g_npend = 1;
    return;
  }
  g_pend[g_npend++] = a;
}

// What LVGL has marked since the last look, into g_pend.
void takeMarked(lv_disp_t *d) {
  for (uint16_t i = 0; i < d->inv_p; ++i)
    if (!d->inv_area_joined[i]) pendPush(d->inv_areas[i]);
  d->inv_p = 0;
  memset(d->inv_area_joined, 0, sizeof d->inv_area_joined);
}

// LVGL draws these areas into the copy now.
void drawIntoCopy(lv_disp_t *d, const lv_area_t *a, int n) {
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < n; ++i) {
    d->inv_areas[i] = a[i];
    d->inv_area_joined[i] = 0;
  }
  d->inv_p = (uint16_t)n;
  lv_refr_now(d);
  g_copy_us += esp_timer_get_time() - t0;
}

// After each scene frame: one slice of what's waiting. The layout pass first: new objects get
// their places (and mark them) there, and inside the redraw it would mark them all into this one
// slice (measured: a full-screen object came out as one 326 ms redraw).
void copyPump(lv_disp_t *d) {
  lv_obj_update_layout(d->act_scr);
  if (d->prev_scr) lv_obj_update_layout(d->prev_scr);
  lv_obj_update_layout(d->top_layer);
  lv_obj_update_layout(d->sys_layer);
  takeMarked(d);
  if (!g_npend) return;
  lv_area_t &a = g_pend[0];
  const int w = lv_area_get_width(&a);
  int rows = (int)(kSlicePx / (uint32_t)(w > 0 ? w : 1));
  if (rows < 1) rows = 1;
  lv_area_t s = a;
  if (s.y2 - s.y1 + 1 > rows) s.y2 = (lv_coord_t)(s.y1 + rows - 1);
  a.y1 = (lv_coord_t)(s.y2 + 1);
  if (a.y1 > a.y2) {
    for (int i = 1; i < g_npend; ++i) g_pend[i - 1] = g_pend[i];
    --g_npend;
  }
  drawIntoCopy(d, &s, 1);
}

// Everything that's waiting, now (a touch, a stop).
void copyCatchUp(lv_disp_t *d) {
  takeMarked(d);
  if (!g_npend) return;
  drawIntoCopy(d, g_pend, g_npend);
  g_npend = 0;
}

// Ends the copy. show: first bring it up to date and put it on the panel, which then shows the
// main screen exactly as LVGL has it (so nothing needs redrawing).
void copyOff(lv_disp_t *d, bool show) {
  if (!g_copy) return;
  if (show) {
    copyCatchUp(d);
    memcpy(g_fb, g_copy, kFrameBytes);
  }
  lv_disp_drv_t *drv = d->driver;
  drv->direct_mode = 0;
  drv->draw_buf = g_lvgl_db;
  drv->flush_cb = g_lvgl_flush;
  if (d->refr_timer) lv_timer_set_period(d->refr_timer, g_lvgl_period);
  g_lvgl_db = nullptr;
  g_lvgl_flush = nullptr;
  psramFree(g_copy);
  g_copy = nullptr;
  if (!show) lv_obj_invalidate(lv_scr_act());   // whatever was waiting for the copy goes to the panel instead
  g_npend = 0;
}

// Gives the screen back to LVGL. With the copy: the panel gets the main screen as LVGL has it now.
// Without: everything repainted.
void endRun() {
  if (!g_app) return;
  if (g_timer) {
    lv_timer_del(g_timer);
    g_timer = nullptr;
  }
  const ss::HostAlloc a = hostAlloc();
  ss::appStop(g_app, a);   // frees the cache (and the fast block when core took it from PSRAM)
  g_app = nullptr;
  psramFree(g_mem);
  g_mem = nullptr;
  g_rv.on = false;
  g_touch = false;
  g_dark_stop = false;
  g_stopped_at = nowMs();
  g_stopped_once = true;
  lv_disp_t *d = lv_disp_get_default();
  if (g_copy && d) {
    copyOff(d, true);
  } else {
    if (d) lv_disp_enable_invalidation(d, true);
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    if (d && d->refr_timer) lv_timer_resume(d->refr_timer);
  }
  // The touch hook stays: it keeps the waking finger from LVGL until it lifts.
}

// LVGL's flush while it draws the main screen for the reveal: into the frame core handed back.
void captureFlush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  if (g_capture) {
    const int w = a->x2 - a->x1 + 1;
    const lv_color_t *src = px;
    for (int y = a->y1; y <= a->y2; ++y, src += w)
      memcpy(g_capture + (size_t)y * SS_WIDTH + a->x1, src, (size_t)w * sizeof(ss::Pixel));
  }
  lv_disp_flush_ready(drv);
}

// The main screen exactly as LVGL would show it now (every layer, banners included), drawn with
// LVGL's own buffer (core handed it back) into `main`. LVGL then thinks the screen shows it, so
// invalidations stay off until the reveal ends (endRun repaints everything once more).
void captureMainScreen(lv_disp_t *d, ss::Pixel *main) {
  lv_disp_drv_t *drv = d->driver;
  void (*flush)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *) = drv->flush_cb;
  g_capture = main;
  drv->flush_cb = captureFlush;
  lv_disp_enable_invalidation(d, true);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(d);
  lv_disp_enable_invalidation(d, false);
  if (d->refr_timer) lv_timer_pause(d->refr_timer);   // the invalidation resumed it
  drv->flush_cb = flush;
  g_capture = nullptr;
}

void revealFrame() {
  const int64_t t = esp_timer_get_time();
  const float dt = (float)(t - g_rv.last_us) / 1e6f;
  g_rv.last_us = t;
  ss::Surface fb{static_cast<ss::Pixel *>(g_fb), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  const bool done = ss::appRevealStep(g_app, fb, dt);
  g_rv.frames++;
  const int64_t t1 = esp_timer_get_time();
  if (!done && t1 - g_rv.t0_us < kRevealGiveUpUs) return;
  const bool copy = g_copy != nullptr;
  endRun();
  const int64_t t2 = esp_timer_get_time();
  board::elog("screensaver: off by a touch at %d,%d; main screen %s in %lu ms, all of it showing after %lu ms (%lu frames)%s, "
              "LVGL's again after %lu ms",
              g_tx, g_ty, copy ? "copied" : "drawn", (unsigned long)(g_rv.capture_us / 1000),
              (unsigned long)((t1 - g_rv.t0_us) / 1000), (unsigned long)g_rv.frames, done ? "" : ", cut short",
              (unsigned long)((t2 - g_rv.t0_us) / 1000));
}

void beginReveal() {
  g_touch = false;
  const int64_t t0 = esp_timer_get_time();
  lv_disp_trig_activity(nullptr);   // a touch for the idle time and the backlight (full light at night)
  lv_disp_t *d = lv_disp_get_default();
  ss::Pixel *main = d ? ss::appRevealBegin(g_app, g_tx, g_ty) : nullptr;
  if (!main) {
    board::elog("screensaver: off by a touch (no reveal)");
    endRun();
    return;
  }
  if (g_copy) {
    copyCatchUp(d);   // the copy up to date: what's still waiting, if anything (the clock, say)
    memcpy(main, g_copy, kFrameBytes);
  } else {
    captureMainScreen(d, main);
  }
  g_rv.on = true;
  g_rv.t0_us = t0;
  g_rv.last_us = esp_timer_get_time();
  g_rv.capture_us = g_rv.last_us - t0;
  g_rv.frames = 0;
  revealFrame();
}

void frame(lv_timer_t *) {
  if (!g_app) return;
  if (g_rv.on) {
    revealFrame();
    return;
  }
  if (g_touch) {
    beginReveal();
    return;
  }
  if (g_dark_stop) {
    board::elog("screensaver: off, the screen went dark");
    endRun();
    return;
  }
  const int64_t t0 = esp_timer_get_time();
  g_backlog_us += t0 - g_last_us;
  g_last_us = t0;
  if (g_backlog_us < kTickUs) return;
  int n = 0;
  while (g_backlog_us >= kTickUs && n < kMaxCatchUp) {   // the simulation never skips a tick; drawing may
    ss::appTick(g_app);
    g_backlog_us -= kTickUs;
    ++n;
  }
  if (g_backlog_us >= kTickUs) {
    g_st.dropped += (uint32_t)(g_backlog_us / kTickUs);
    g_backlog_us %= kTickUs;
  }
  const int64_t t1 = esp_timer_get_time();
  ss::Surface fb{static_cast<ss::Pixel *>(g_fb), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  const int strips = ss::appRender(g_app, fb);
  const int64_t t2 = esp_timer_get_time();
  if (g_copy) copyPump(lv_disp_get_default());   // a slice of the main screen's changes into its copy
  const int64_t t3 = esp_timer_get_time();
  g_st.frames++;
  g_st.ticks += (uint32_t)n;
  g_st.strips += (uint32_t)strips;
  g_st.tick_us += t1 - t0;
  g_st.render_us += t2 - t1;
  if (t3 - t0 > g_st.worst_us) g_st.worst_us = t3 - t0;   // the copy's slice included
}

void refreshInputsSoon() { g_inputs_at = nowMs() - kInputsMs; }

}  // namespace

void screensaver_set_frame_buffer(void *fb) { g_fb = fb; }

bool screensaver_running() { return g_app != nullptr; }

bool screensaver_start(uint32_t seed) {
  if (g_app || !g_fb) return false;
  if (!g_in_ready) {   // before the first service call: a clear day
    memset(&g_in, 0, sizeof g_in);
    g_in.mood = 3.f;
    g_in.chip = true;
  }
  g_mem = psramAlloc(SS_APP_BYTES);
  if (!g_mem) return false;
  lv_disp_t *d = lv_disp_get_default();
  // With the main screen's copy if there's room for it and the scene's cache after it; else
  // without, and LVGL draws nothing until the end (its changes meanwhile are repainted then).
  for (int with_copy = 1; with_copy >= 0 && !g_app; --with_copy) {
    if (with_copy && !(d && copyOn(d))) continue;
    if (!with_copy && d) {
      lv_disp_enable_invalidation(d, false);
      if (d->refr_timer) lv_timer_pause(d->refr_timer);
    }
    const ss::HostAlloc a = hostAlloc();
    g_app = ss::appStart(g_mem, a, seed, g_in);
    if (g_app) break;
    if (with_copy) {
      copyOff(d, false);
    } else if (d) {
      lv_disp_enable_invalidation(d, true);
      lv_obj_invalidate(lv_scr_act());
    }
  }
  if (!g_app) {
    psramFree(g_mem);
    g_mem = nullptr;
    return false;
  }
  if (!g_copy) board::elog("screensaver: no room for the main screen's copy; a touch draws it first");
  ss::appSetClock(g_app, clockUs);
  ss::appInvalidate(g_app);
  g_seed = seed;
  g_touch = g_dark_stop = false;
  g_finger = false;
  g_rv = {};
  memset(&g_st, 0, sizeof g_st);
  g_last_us = g_st.since_us = esp_timer_get_time();
  g_backlog_us = kTickUs;   // the first frame draws at once
  lvgl_port_set_touch_hook(touchHook);
  g_timer = lv_timer_create(frame, kTimerMs, nullptr);
  return true;
}

void screensaver_stop() { endRun(); }

void screensaver_touch(int x, int y) {
  if (!g_app || g_rv.on) return;
  g_tx = (int16_t)(x < 0 ? 0 : (x >= SS_WIDTH ? SS_WIDTH - 1 : x));
  g_ty = (int16_t)(y < 0 ? 0 : (y >= SS_HEIGHT ? SS_HEIGHT - 1 : y));
  g_touch = true;
}

void screensaver_service(const tc::View &v, const tc::Settings &s, const tc::WallTime &w, int32_t utc_offset,
                         uint32_t idle_ms) {
  const uint32_t now = nowMs();
  g_idle_now = idle_ms;
  g_idle_ms = g_idle_set_ms ? g_idle_set_ms : (uint32_t)s.screensaver_min * 60000u;
  // A new ring from the other floor: BellView::shakes counts rings both ways, and an incoming one
  // sets alert (controller.cpp, MsgType::Knock). The controller has already played the chime.
  if (!g_shakes_known || v.bell.shakes != g_shakes) {
    const bool rang = g_shakes_known && v.bell.alert;
    g_shakes = v.bell.shakes;
    g_shakes_known = true;
    if (rang && g_app && !g_rv.on) {
      screensaver_doorbell();
      board::elog("screensaver: doorbell from the other floor, the hiker answers");
    }
  }
  if (!g_mood_known || now - g_mood_at >= kMoodMs) {
    g_mood = phaseMood(v, s, w, utc_offset);
    g_mood_at = now;
    g_mood_known = true;
  }
  if (!g_in_ready || now - g_inputs_at >= kInputsMs) {
    g_inputs_at = now;
    const ss::Inputs in = buildInputs(v);
    if (!g_in_ready || memcmp(&in, &g_in, sizeof in) != 0) {
      g_in = in;
      g_in_ready = true;
      if (g_app && !g_rv.on) ss::appSetInputs(g_app, g_in);
    }
  }
  const bool lit = v.backlight != tc::kBacklightOff;
  if (!lit) g_lit_since = now;
  if (g_app) {
    if (!lit && !g_rv.on) g_dark_stop = true;   // brief: it doesn't run while the screen is off
    return;
  }
  if (!g_auto || !lit || idle_ms < g_idle_ms) return;
  if (now - g_lit_since < g_idle_ms) return;                        // the screen only just lit (a doorbell at night)
  if (g_stopped_once && now - g_stopped_at < g_idle_ms) return;     // stopped without a touch (serial): a full idle time again
  const uint32_t seed = esp_random() & 0x7fffffffu;
  if (screensaver_start(seed))
    board::elog("screensaver: on after %lu s without a touch, seed %lu", (unsigned long)(idle_ms / 1000), (unsigned long)seed);
}

void screensaver_doorbell() {
  if (!g_app) return;
  ss::Event e;
  e.type = ss::EV_DOORBELL;
  e.arg = 0;
  ss::appPostEvent(g_app, e);
}

void screensaver_set_auto(bool on) { g_auto = on; }
bool screensaver_auto() { return g_auto; }
void screensaver_set_idle(uint32_t seconds) { g_idle_set_ms = seconds ? (seconds < 5 ? 5 : seconds) * 1000u : 0; }
uint32_t screensaver_idle() { return (g_idle_set_ms ? g_idle_set_ms : g_idle_ms) / 1000u; }

void screensaver_set_mood(float mood) {
  g_mood_override = mood < 0.f ? -1.f : (mood >= 6.f ? 5.99f : mood);
  refreshInputsSoon();
}

void screensaver_set_scene(int family) {
  g_scene = (uint8_t)(family < 0 || family > 4 ? 0 : family);
  refreshInputsSoon();
}

int screensaver_stats(char *buf, size_t n) {
  const int64_t now = esp_timer_get_time();
  const Stats s = g_st;
  memset(&g_st, 0, sizeof g_st);
  g_st.since_us = now;
  ss::RenderProfile p;
  memset(&p, 0, sizeof p);
  if (g_app) p = ss::appProfile(g_app);
  const uint32_t copy_refr = g_copy_refr, copy_px = g_copy_px;
  const int64_t copy_us = g_copy_us;
  g_copy_refr = g_copy_px = 0;
  g_copy_us = 0;
  const float strip_px = (float)(SS_WIDTH * SS_STRIP_ROWS);
  const float secs = (float)(now - s.since_us) / 1e6f;
  const float f = s.frames ? (float)s.frames : 1.f;
  return snprintf(buf, n,
                  "screensaver: %s; %.1f s: %lu frames (%.1f fps), %lu ticks, %lu dropped; tick %.2f ms, draw %.2f ms, "
                  "worst %.1f ms per frame; %.2f strips a frame, %.0f%% of their width copied; draw split: copy in %.2f, "
                  "layers %.2f, copy out %.2f, cache %.2f ms; main screen copy %s (%lu areas, %lu px, %lu ms redrawn); fast block %s; "
                  "free heap %u, free PSRAM %u (largest %u)",
                  g_app ? "running" : "stopped", (double)secs, (unsigned long)s.frames,
                  secs > 0.f ? (double)(s.frames / secs) : 0.0, (unsigned long)s.ticks, (unsigned long)s.dropped,
                  (double)(s.tick_us / f / 1000.f), (double)(s.render_us / f / 1000.f), (double)(s.worst_us / 1000.f),
                  (double)(s.strips / f), p.strips ? (double)(100.f * p.px / (p.strips * strip_px)) : 0.0,
                  (double)(p.copy_in_us / f / 1000.f), (double)(p.draw_us / f / 1000.f), (double)(p.copy_out_us / f / 1000.f),
                  (double)(p.static_us / f / 1000.f), g_copy ? "on" : "off", (unsigned long)copy_refr,
                  (unsigned long)copy_px, (unsigned long)(copy_us / 1000), g_lent ? "LVGL's (internal)" : "PSRAM",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

int screensaver_inputs(char *buf, size_t n) {
  static const char *const kScene[5] = {"its own pick", "evening", "morning", "midday", "lookout"};
  const ss::Inputs &in = g_in;
  char state[32];
  if (g_app) snprintf(state, sizeof state, "running, seed %lu", (unsigned long)g_seed);
  else snprintf(state, sizeof state, "stopped");
  return snprintf(buf, n,
                  "screensaver inputs: %s; mood %.2f (%s); scene weather %d%s, overcast %.2f, fog %.2f; other floor %d "
                  "(5 = no signal); dogs %d; night %s; next loop %s; starts on its own %s after %lu s (%s; idle now %lu s)",
                  state, (double)in.mood, g_mood_override >= 0.f ? "set by hand" : "the clock",
                  in.weather_code, in.have_outside ? " from the outside reading" : " (no reading: its own pick)",
                  (double)in.weather.overcast, (double)in.weather.fog, in.other_status, in.dog_out,
                  in.night_mode ? "yes" : "no", kScene[g_scene], g_auto ? "on" : "off", (unsigned long)(screensaver_idle()),
                  g_idle_set_ms ? "set by hand until restart" : "Settings > SCREENSAVER",
                  (unsigned long)(g_idle_now / 1000));
}

}  // namespace ui

#endif  // ESP_PLATFORM
