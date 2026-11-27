// The campsite screensaver on the panel. See ss_host.h. Panel only: the PC simulator
// (tools/build_sim.sh) compiles src/ui/*.cpp and never starts it.
#include "ss_host.h"
#if defined(ESP_PLATFORM)

#include <stdio.h>
#include <string.h>

#include <house_weather.h>
#include <mush_patch.h>
#include <scene/xmas.h>
#include <set_piece_day.h>
#include <ss_app.h>

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "../app/controller.h"
#include "../board/board.h"
#include "../core/brightness.h"
#include "../core/civil.h"
#include "../core/sky.h"
#include "../core/zambretti.h"
#include "../gfx/canvas.h"
#include "art.h"
#include "ui.h"   // the spare picture's loan (spare_lend, spare_give_back, spare_reclaim)
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
int g_weather_set = -1;             // ss weather N: a scene weather by hand (testing); -1 the outside reading's
// The night scene's brightness (palette.h WeatherTint::night_gain). The PC look (0.55) left only the
// stream readable on the panel at its night backlight (Mike, 2026-11-05); 1.6 brings the scene to
// about the panel's own night tiles. ss nightgain N changes it until restart, in tenths.
float g_night_gain = 1.6f;
// Stage 10: the Lookout around the real sunset, once an evening (sky.h in_lookout_window).
// Stage 15: the sunrise overlook around the real sunrise, once a morning, the same way. Each counts as
// shown only once its key moment has played (ss_app.h appSetPieceShown: the sun down, the sun up), so a
// touch in its first seconds doesn't use up the day's showing (set_piece_day.h).
int g_lookout_set = 0;              // ss lookout N: 0 the clock's windows, 1 the Lookout now, 2 both off, 3 the sunrise now
bool g_lookout_now = false;         // what the next loop reads (Inputs::lookout_window)
bool g_sunrise_now = false;         // and Inputs::sunrise_window
int32_t g_today = -1;               // the local day number, for once a day
ss::SetPieceDay g_lookout_shown = {-1};   // the day a Lookout was last shown
ss::SetPieceDay g_sunrise_shown = {-1};   // the day a sunrise was last shown
int g_sunset_min = -1;              // today's sunset in local minutes, for ss inputs
int g_sunrise_min = -1;             // today's sunrise, the same
// The sunrise window (PROPOSED, the Lookout's mirrored): from 30 minutes before today's sunrise to 15
// after, so the scene (it starts in the blue before dawn; the sun breaks about two and a half minutes
// in) begins a little before the real one. sky.h has no sunrise window of its own.
constexpr int kSunriseBeforeMin = 30;
constexpr int kSunriseAfterMin = 15;
constexpr int kPaletteSpread = 1;   // background bands a frame for a small palette step (appSetPaletteSpread): ~16 ms more a frame for 15 frames, not one 110 ms stall
// And for a medium one, the Lookout's sunset steps: at once (0), as the brief has them. Mike watched
// them wash down at 1, 3 and 5 bands a frame and chose instant (2026-11-05), the brief pause and all.
// ss spread N changes it until restart.
int g_spread_medium = 0;
int g_pal_seen = 0;                 // appPaletteRebuilds when last looked (it restarts at 0 each loop)
float g_mood = 3.f;
// Stage 12: where the sun and the moon really are (sky.h), worked out with the mood. All zero (the
// scene's old parked sun and moon) with no clock, or while a phase is forced for testing.
ss::SkyInputs g_sky = {};
uint8_t g_month = 0;                // the local month 1-12 for the scene's leaves (Inputs::month); 0 with no clock
uint8_t g_mday = 0;                 // the local day of the month 1-31 (Inputs::mday): the Christmas season; 0 with no clock
// The Christmas evening's two decorating windows (scene/xmas.h XMAS_DECK_*: carry bits 12 and 13) start over
// each day at local noon, so the next evening decorates twice again (Mike, 2026-11-26). The day they last
// started over: days since 1970 counted from local noon; -1 with no clock.
int32_t g_deck_day = -1;
int32_t g_noon_day = -1;
constexpr uint16_t kDeckBits = (uint16_t)((ss::XMAS_DECK_EARLY | ss::XMAS_DECK_LATE) << 8);
// Stage 22's carry word (ss_app.h Inputs::carry; Mike, 2026-11-19): what one run hands the next, so the
// mushrooms ripen across runs and tai chi keeps its chapter and plays once a morning. Low byte the
// mushroom patch (mush_patch.h), high byte tai chi (scene/memory.h taichi_next). RAM only: a restart
// starts it at 0, which is a bare bank and chapter 1.
uint16_t g_carry = 0;
uint32_t g_taichi_at = 0;           // nowMs when "tai chi done this morning" was set, for the 6-hour rule
uint32_t g_mush_at = 0;             // nowMs the patch last grew while stopped; minutes since then carry over
constexpr uint16_t kTaichiDoneBit = 1u << 10;           // taichi_next bit 2: tai chi has played this morning
constexpr uint32_t kTaichiDoneMs = 6u * 3600u * 1000u;  // cleared 6 hours after it was stored (INTEGRATION_PLAN 3.1 point 6)
bool g_mood_known = false;
int g_phase_seen = -1;              // the sky phase the mood was last worked out in
uint32_t g_mood_at = 0, g_inputs_at = 0;
uint16_t g_shakes = 0;
bool g_shakes_known = false;
uint32_t g_seed = 0;

// Stats since the last screensaver_stats call.
struct Stats {
  uint32_t frames, ticks, strips, dropped;
  uint32_t palettes;                // palette changes (each rebuilds the cache)
  int64_t palette_worst_us;         // the slowest frame that had one
  int64_t tick_us, render_us, worst_us;
  int64_t since_us;
};
Stats g_st;

void refreshInputsSoon();
void storeCarry(uint16_t c, uint32_t now);

// The tenant's mail icon (Mike, 2026-11-07): while mail waits on the DOWNSTAIRS panel, the solid mail
// badge sits in the top-right corner of every frame, steady. It is a small sprite built once (a cream
// ring round the dark disc so it reads on a night sky), kept as RGB565 with the opaque span of each
// row, so a stamp is a few row copies. The edge is hard (alpha cut at half) so a stamp over itself
// changes nothing.
// Stage 13: the renderer draws it into each strip it composites, after the scene and before the strip
// goes to the frame buffer (appSetOverlay). Stamped after the whole render, as it was, the frame
// buffer showed the scene there for a moment whenever strips 0-1 were redrawn (rain, snow, the
// fades): the badge flickered. Strips not redrawn keep it. On and off, only its own square is redrawn
// (appInvalidateRect), not the whole frame.
constexpr int kMailPx = 48, kMailEdge = 16;
constexpr int kMailX = SS_WIDTH - kMailEdge - kMailPx, kMailY = kMailEdge;
ss::Pixel g_mail_px[kMailPx * kMailPx];
uint8_t g_mail_x0[kMailPx], g_mail_x1[kMailPx];   // opaque span [x0, x1) of each row
bool g_mail_built = false;
bool g_mail_failed = false;         // the sprite's canvas didn't fit: not retried every frame, only on the next start (Mike, 2026-11-07)
bool g_mail_want = false;           // the downstairs panel has mail waiting (set by screensaver_service)
bool g_mail_on = false;             // the badge is in the frame

bool buildMailSprite() {
  gfx::Canvas c;
  if (!c.create(kMailPx, kMailPx, true)) return false;
  const gfx::Color dark = gfx::hex(0x2B2433), cream = gfx::hex(0xF3EAD8);
  art::mail(c, true, dark, cream);
  c.ring(kMailPx / 2.f, kMailPx / 2.f, kMailPx / 2.f, kMailPx / 2.f - 2.5f, cream);
  for (int y = 0; y < kMailPx; ++y) {
    int a = kMailPx, b = 0;
    for (int x = 0; x < kMailPx; ++x) {
      uint8_t al = 0;
      const gfx::Color col = c.get(x, y, &al);
      g_mail_px[y * kMailPx + x] = (ss::Pixel)gfx::to565(col);
      if (al >= 128) {
        if (x < a) a = x;
        b = x + 1;
      }
    }
    g_mail_x0[y] = (uint8_t)(a < b ? a : 0);
    g_mail_x1[y] = (uint8_t)(a < b ? b : 0);
  }
  c.destroy();
  return true;
}

// The renderer's overlay (appSetOverlay): the badge's rows that fall in this strip, into the strip.
void mailOverlay(void *, ss::Surface &strip, const ss::Rect &clip) {
  if (!g_mail_on) return;
  const int y0 = clip.y0 > kMailY ? clip.y0 : kMailY;
  const int y1 = clip.y1 < kMailY + kMailPx ? clip.y1 : kMailY + kMailPx;
  for (int y = y0; y < y1; ++y) {
    const int r = y - kMailY;
    const int n = g_mail_x1[r] - g_mail_x0[r];
    if (n > 0)
      memcpy(strip.row(y) + kMailX + g_mail_x0[r], &g_mail_px[r * kMailPx + g_mail_x0[r]], (size_t)n * sizeof(ss::Pixel));
  }
}

// The badge on or off to match g_mail_want: its square is redrawn at the next render.
void syncMail() {
  if (g_mail_want && !g_mail_on) {
    if (!g_mail_built && !g_mail_failed) {
      g_mail_built = buildMailSprite();
      g_mail_failed = !g_mail_built;
    }
    if (!g_mail_built) return;
    g_mail_on = true;
  } else if (!g_mail_want && g_mail_on) {
    g_mail_on = false;   // acknowledged: the scene is redrawn without the badge
  } else {
    return;
  }
  ss::appInvalidateRect(g_app, kMailX, kMailY, kMailX + kMailPx, kMailY + kMailPx);
}

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// The renderer's clock for its time split (appProfile): copy in, layers, copy out.
uint32_t clockUs() { return (uint32_t)esp_timer_get_time(); }

void *psramAlloc(size_t n) { return heap_caps_aligned_alloc(16, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void psramFree(void *p) { heap_caps_free(p); }

// The scene's layer cache is the interface's spare picture, borrowed for the run (memory audit
// 2026-11-12, recommendation 1: 768,000 bytes of PSRAM less while it runs). Ownership, in full in
// core/spare_loan.h:
// - The core asks for its cache through HostAlloc::alloc_large (Renderer::init, in appStart) and
//   frees it through free_large (Renderer::shutdown, in appStop). sceneAlloc lends the spare for
//   that one request (the full-frame size; the fast block and anything else get PSRAM as before)
//   and sceneFree hands it back. So the one borrow and the one return are the core's own cache
//   allocation and free, and whatever ends a run ends it through endRun -> appStop: a touch (the
//   reveal, cut short or not, or no reveal), the screen going dark, a stop from serial (ss, ss
//   seed N). A failed start returns it inside appStart. The doorbell (a knock) doesn't end a run.
//   An update restarts the panel, so nothing is left to return.
// - The spare is lent only while nothing else has it (no art drawing in it, no tile fade showing
//   it); otherwise, or with the wrong size, the cache is the scene's own PSRAM, as before.
// - While it is lent, the art (sky, tiles, outside, doorbell) can't claim it, so their change-overs
//   wait until the stop and are asked for again on the frames after it. Nothing else draws into it:
//   the reveal's main-screen picture goes into the cache, which is the screensaver's own until
//   appStop.
// - After every appStop and every failed appStart, spareHome checks it is back (spare_reclaim).
// The cache is the frame's size in RGB565; the spare is the sky's size in RGB565: the same, checked
// here, and spare_lend checks the size asked for again.
static_assert((size_t)art::kScreenW * art::kScreenH * sizeof(ss::Pixel) == kFrameBytes,
              "the spare picture is the scene cache's size");
bool g_cache_spare = false;   // this run's cache is the borrowed spare picture

void *sceneAlloc(size_t n) {
  if (n == kFrameBytes && !g_cache_spare) {
    if (void *p = spare_lend(n)) {
      g_cache_spare = true;
      return p;
    }
  }
  return psramAlloc(n);
}

void sceneFree(void *p) {
  if (g_cache_spare && spare_give_back(p)) {
    g_cache_spare = false;
    return;
  }
  psramFree(p);
}

// After appStop, or an appStart that failed: the spare must be home.
void spareHome(const char *after) {
  if (spare_reclaim()) board::elog("screensaver: the spare picture was still lent after %s; taken back", after);
  g_cache_spare = false;
}

ss::HostAlloc hostAlloc() {
  ss::HostAlloc a;
  a.alloc_large = sceneAlloc;
  a.free_large = sceneFree;
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

// The real sky for the scene: the sun's and the moon's compass bearing and height now, and the
// moon's phase, from the clock and the panel's place. Not while the mood is forced (a forced night
// with the real afternoon sun would be neither): then the scene parks them by mood, as it used to.
ss::SkyInputs realSky(const tc::View &v, const tc::Settings &s, const tc::WallTime &w, int32_t off) {
  ss::SkyInputs k;
  memset(&k, 0, sizeof k);
  if (!w.valid || g_mood_override >= 0.f) return k;
  if (tc::sky_at(w.utc, off, s.latitude, s.longitude).phase != v.phase) return k;   // a phase forced by hand
  const tc::SkyPos sun = tc::sun_position(w.utc, s.latitude, s.longitude);
  const tc::MoonSky moon = tc::moon_sky(w.utc, s.latitude, s.longitude);
  k.have = true;
  k.sun_az = sun.az;
  k.sun_alt = sun.alt;
  k.moon_az = moon.pos.az;
  k.moon_alt = moon.pos.alt;
  k.moon_phase = moon.phase;
  return k;
}

// Everything the scene takes from the panel (ss_app.h Inputs). night_dim stays 0: the core's night
// rule follows night_mode, with the panel's night gain; lookout_window comes from screensaver_service.
ss::Inputs buildInputs(const tc::View &v) {
  ss::Inputs in;
  memset(&in, 0, sizeof in);   // ss_app.h: zero all of it first
  in.mood = g_mood_override >= 0.f ? g_mood_override : g_mood;
  if (g_mood_override < 0.f) in.sky = g_sky;
  in.month = g_month;
  in.mday = g_mday;
  in.carry = g_carry;
  ss::OutsideReading r;
  r.valid = v.outside.valid;
  r.forecast = (int)v.outside.code;
  // The forecast's words pick the weather (Mike, 2026-11-11): the icon alone made it rain on "rain
  // later". 0 for words that aren't one of the 26 Zambretti texts; then the icon decides.
  r.phrase = tc::zambretti_letter(v.outside.phrase);
  r.temp_f = (float)v.outside.temp_tenths / 10.f;
  r.humidity = (float)v.outside.hum_pct;
  r.danger_heat = v.outside.heat;
  r.danger_cold = v.outside.cold;
  const ss::SceneWeather sw = ss::sceneWeatherFromOutside(r);
  in.weather_code = sw.code;
  in.have_outside = sw.have_outside;
  in.weather.overcast = sw.overcast;
  in.weather.fog = sw.fog;
  if (g_weather_set >= 0) {   // by hand, with the tint a reading of that kind would bring (as sim/soak_main.cpp)
    static const float kTint[8][2] = {{0.00f, 0.00f}, {0.75f, 0.15f}, {0.60f, 0.25f}, {0.25f, 0.00f},
                                      {0.00f, 0.05f}, {0.35f, 0.80f}, {0.45f, 0.10f}, {0.95f, 0.20f}};
    in.weather_code = g_weather_set;
    in.have_outside = true;
    in.weather.overcast = kTint[g_weather_set][0];
    in.weather.fog = kTint[g_weather_set][1];
  }
  in.weather.night_dim = 0.f;
  in.weather.night_gain = g_night_gain;
  const tc::FloorView &f = v.floor[(int)tc::other_floor(v.me)];
  in.other_status = f.no_signal ? 5 : (int)f.status;
  in.dog_out = (int)v.dog.out;   // the badges, and Lucy and Copper in camp (stage 13)
  in.chip = true;
  // The tenant's heat and cooling arrows (stage 13), on the UPSTAIRS panel's screensaver only: the ones
  // its UPSTAIRS tile shows (RequestView, oldest first), drawn by the chip beside the dog badges and
  // flashing as the tile's do. A predicted arrow keeps its own colours.
  if (v.me == tc::Floor::Upstairs) {
    const int n = v.req.n < tc::kMaxArrows ? v.req.n : tc::kMaxArrows;
    for (int i = 0; i < n && i < 3; ++i)
      in.req[i] = (uint8_t)((uint8_t)v.req.ask[i] | (v.req.source[i] == tc::ReqSource::Predicted ? SS_REQ_PREDICTED : 0));
  }
  // Stage 14. The same arrows on BOTH panels for the fire and ice (too hot: fires, too cold: ice);
  // predicted ones are marked and the core leaves them out.
  {
    const int n = v.req.n < tc::kMaxArrows ? v.req.n : tc::kMaxArrows;
    for (int i = 0; i < n && i < 3; ++i)
      in.temp_ask[i] = (uint8_t)((uint8_t)v.req.ask[i] | (v.req.source[i] == tc::ReqSource::Predicted ? SS_REQ_PREDICTED : 0));
  }
  // The mail bird: on the DOWNSTAIRS panel, from the same state as its mail badge (g_mail_want).
  in.mail = v.me == tc::Floor::Downstairs && v.mail.show;
  in.passer_by = v.passer_by;   // someone walked up to the upstairs panel: the hiker waves
  in.night_mode = v.mode == tc::Mode::Night;
  in.lookout_window = g_lookout_now;
  in.sunrise_window = g_sunrise_now;
  in.visitors = true;           // the visiting couple drop by now and then (stage 15)
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
  const uint16_t carry = ss::appCarry(g_app);   // what this run hands the next, read before the App goes
  storeCarry(carry, nowMs());
  g_mush_at = nowMs();   // the patch grew by loops while it ran; by the clock from here
  const ss::HostAlloc a = hostAlloc();
  ss::appStop(g_app, a);   // frees the cache (and the fast block when core took it from PSRAM): the spare goes home
  g_app = nullptr;
  spareHome("the stop");
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

// After each frame's ticks: a set piece counts as today's once its key moment has played (the
// Lookout's sun down, the sunrise's sun up over the ridge), not when it begins, so a touch in its
// first seconds leaves the day's showing for the next start in the window. One forced for testing
// (ss lookout 1 or 3, ss scene 4 or 5) never counts. Then the inputs stop asking for it at once.
void noteSetPieces() {
  if (!g_app) return;
  const int fam = ss::appSpineFamily(g_app);   // scene/spine.h: 3 SF_LOOKOUT, 4 SF_SUNRISE
  if (fam != 3 && fam != 4) return;
  const bool shown = ss::appSetPieceShown(g_app);
  if (!shown) return;
  const bool forced = g_lookout_set != 0 || g_scene == 4 || g_scene == 5;
  bool counted;
  if (fam == 3) {
    counted = ss::setPieceNote(g_lookout_shown, g_today, true, shown, forced);
    if (counted && g_sunset_min >= 0)
      board::elog("screensaver: the Lookout shown, sunset at %d:%02d", g_sunset_min / 60, g_sunset_min % 60);
  } else {
    counted = ss::setPieceNote(g_sunrise_shown, g_today, true, shown, forced);
    if (counted && g_sunrise_min >= 0)
      board::elog("screensaver: the sunrise shown, sunrise at %d:%02d", g_sunrise_min / 60, g_sunrise_min % 60);
  }
  if (counted) refreshInputsSoon();
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
  noteSetPieces();   // two calls a frame; counts a set piece once its key moment has played
  const int64_t t1 = esp_timer_get_time();
  ss::Surface fb{static_cast<ss::Pixel *>(g_fb), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  const int pal = ss::appPaletteRebuilds(g_app);   // after the ticks: the palette this frame draws with
  const int new_pal = pal >= g_pal_seen ? pal - g_pal_seen : pal;
  g_pal_seen = pal;
  syncMail();
  const int strips = ss::appRender(g_app, fb);
  const int64_t t2 = esp_timer_get_time();
  if (g_copy) copyPump(lv_disp_get_default());   // a slice of the main screen's changes into its copy
  const int64_t t3 = esp_timer_get_time();
  if (new_pal > 0) {
    g_st.palettes += (uint32_t)new_pal;
    if (t3 - t0 > g_st.palette_worst_us) g_st.palette_worst_us = t3 - t0;
  }
  g_st.frames++;
  g_st.ticks += (uint32_t)n;
  g_st.strips += (uint32_t)strips;
  g_st.tick_us += t1 - t0;
  g_st.render_us += t2 - t1;
  if (t3 - t0 > g_st.worst_us) g_st.worst_us = t3 - t0;   // the copy's slice included
}

void refreshInputsSoon() { g_inputs_at = nowMs() - kInputsMs; }

// A new carry word; the 6-hour clock starts when "tai chi done" goes on (the patch changes the word
// every 10 minutes while it grows, so the word's own changes can't time it).
void storeCarry(uint16_t c, uint32_t now) {
  if ((c & kTaichiDoneBit) && !(g_carry & kTaichiDoneBit)) g_taichi_at = now;
  g_carry = c;
}

// The carry word, once a service call. While a run is on it follows the run (appCarry changes at loop
// ends). While none is: the mushroom patch grows by the clock as it would by loops (mush_patch.h,
// one step per 10 dry minutes; rain from the outside reading counts as a wet loop), and "tai chi done
// this morning" goes 6 hours after it was stored, so the next morning's first loop opens with it.
void serviceCarry(uint32_t now) {
  if (g_app) {
    storeCarry(ss::appCarry(g_app), now);
    g_mush_at = now;
    // A noon passed during the run is the run's own: a midday or morning loop in it clears the
    // decorating bits, and any set after that are this evening's, so the end of the run keeps them.
    if (g_noon_day >= 0) g_deck_day = g_noon_day;
    return;
  }
  uint16_t c = g_carry;
  if ((c & kTaichiDoneBit) && now - g_taichi_at >= kTaichiDoneMs) c = (uint16_t)(c & ~kTaichiDoneBit);
  if (g_noon_day >= 0 && g_noon_day != g_deck_day) {   // past local noon: this evening may decorate twice again
    c = (uint16_t)(c & ~kDeckBits);
    g_deck_day = g_noon_day;
  }
  const uint32_t since = now - g_mush_at;
  if (since >= 60000u) {
    // The real reading only: a weather set by hand (ss weather) is for testing and grows nothing.
    const bool raining = g_in_ready && g_weather_set < 0 && g_in.have_outside &&
                         (g_in.weather_code == 1 || g_in.weather_code == 7);
    const uint8_t b = (uint8_t)(c & 0xFFu);
    uint8_t nb = b;
    if (raining) {
      nb = ss::mushAfterMinutes(b, 0, true);
      g_mush_at = now;
    } else {
      const uint32_t mins = since / 60000u;
      const uint32_t steps = mins / ss::MUSH_MINUTES_PER_STEP;
      if (steps) {
        nb = ss::mushAfterMinutes(b, mins, false);
        g_mush_at += steps * ss::MUSH_MINUTES_PER_STEP * 60000u;   // the remainder counts toward the next step
      }
    }
    c = (uint16_t)((c & 0xFF00u) | nb);
  }
  storeCarry(c, now);
}

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
  g_in.carry = g_carry;   // what the last run left (the inputs may not have been rebuilt since)
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
    spareHome("a failed start");   // appStart frees what it took; the next try may borrow it again
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
  if (!g_cache_spare) board::elog("screensaver: the spare picture wasn't free (art being drawn, or a fade), so the scene's cache has its own PSRAM");
  ss::appSetClock(g_app, clockUs);
  ss::appSetOverlay(g_app, mailOverlay, nullptr);   // the downstairs mail badge, drawn into each strip
  ss::appSetPaletteSpread(g_app, kPaletteSpread, g_spread_medium);   // palette steps without a full-frame stall
  g_pal_seen = ss::appPaletteRebuilds(g_app);
  ss::appInvalidate(g_app);
  g_seed = seed;
  g_touch = g_dark_stop = false;
  g_finger = false;
  g_rv = {};
  g_mail_on = false;   // a fresh frame, all drawn by the first render (syncMail turns it on there)
  g_mail_failed = false;   // a new screensaver run may find the memory for the badge
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
  serviceCarry(now);
  g_mail_want = v.me == tc::Floor::Downstairs && v.mail.show;
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
  if ((int)v.phase != g_phase_seen) {   // a new phase (or one forced, or let go): the mood at once
    g_phase_seen = (int)v.phase;
    g_mood_known = false;
  }
  if (!g_mood_known || now - g_mood_at >= kMoodMs) {
    g_mood = phaseMood(v, s, w, utc_offset);
    g_sky = realSky(v, s, w, utc_offset);   // the sun moves about a pixel a minute: this often is plenty
    g_month = w.valid ? (uint8_t)tc::civil_from_seconds((int64_t)w.utc + utc_offset).month : 0;
    g_mday = w.valid ? (uint8_t)tc::civil_from_seconds((int64_t)w.utc + utc_offset).day : 0;
    g_noon_day = w.valid ? (int32_t)(((int64_t)w.utc + utc_offset - 43200) / 86400) : -1;
    g_mood_at = now;
    g_mood_known = true;
  }
  if (!g_in_ready || now - g_inputs_at >= kInputsMs) {
    g_inputs_at = now;
    // Stage 10, the Lookout: in the window round today's real sunset (sky.h), until one has begun
    // this evening. The scene reads it when a loop begins.
    bool rise_window = false;
    if (w.valid) {
      g_today = (int32_t)(((int64_t)w.utc + utc_offset) / 86400);
      const int64_t set = tc::sunset_today(w.utc, utc_offset, s.latitude, s.longitude);
      g_sunset_min = set ? (int)(((set + utc_offset) % 86400) / 60) : -1;
      // Today's sunrise (none in polar day or night) and the morning window round it.
      const tc::Civil c = tc::civil_from_seconds((int64_t)w.utc + utc_offset);
      const tc::SunTimes st = tc::sun_times(c.year, c.month, c.day, utc_offset, s.latitude, s.longitude);
      g_sunrise_min = st.ok ? (int)(((st.rise + utc_offset) % 86400) / 60) : -1;
      rise_window = st.ok && (int64_t)w.utc >= st.rise - kSunriseBeforeMin * 60 && (int64_t)w.utc < st.rise + kSunriseAfterMin * 60;
    } else {
      g_today = g_sunset_min = g_sunrise_min = -1;
    }
    const bool window = w.valid && tc::in_lookout_window(w.utc, utc_offset, s.latitude, s.longitude);
    g_lookout_now = g_lookout_set == 1 || (g_lookout_set == 0 && ss::setPieceWanted(g_lookout_shown, g_today, window));
    g_sunrise_now = g_lookout_set == 3 || (g_lookout_set == 0 && ss::setPieceWanted(g_sunrise_shown, g_today, rise_window));
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
  g_scene = (uint8_t)(family < 0 || family > 5 ? 0 : family);
  refreshInputsSoon();
}

void screensaver_set_spread_medium(int bands) {
  g_spread_medium = bands < 0 ? 0 : (bands > SS_STRIP_COUNT ? SS_STRIP_COUNT : bands);
  if (g_app) ss::appSetPaletteSpread(g_app, kPaletteSpread, g_spread_medium);
}
int screensaver_spread_medium() { return g_spread_medium; }

void screensaver_set_lookout(int mode) {
  g_lookout_set = mode >= 1 && mode <= 3 ? mode : 0;
  refreshInputsSoon();
}

void screensaver_set_night_gain(float gain) {
  g_night_gain = gain < 0.f ? 0.f : (gain > 4.f ? 4.f : gain);
  refreshInputsSoon();
}
float screensaver_night_gain() { return g_night_gain; }

void screensaver_set_weather(int code) {
  g_weather_set = code < 0 || code > 7 ? -1 : code;
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
                  "layers %.2f, copy out %.2f, cache %.2f ms; %lu palette changes (slowest such frame %.1f ms); "
                  "main screen copy %s (%lu areas, %lu px, %lu ms redrawn); fast block %s; scene cache %s; "
                  "spare picture %s (%lu art claims refused); "
                  "free heap %u, free PSRAM %u (largest %u)",
                  g_app ? "running" : "stopped", (double)secs, (unsigned long)s.frames,
                  secs > 0.f ? (double)(s.frames / secs) : 0.0, (unsigned long)s.ticks, (unsigned long)s.dropped,
                  (double)(s.tick_us / f / 1000.f), (double)(s.render_us / f / 1000.f), (double)(s.worst_us / 1000.f),
                  (double)(s.strips / f), p.strips ? (double)(100.f * p.px / (p.strips * strip_px)) : 0.0,
                  (double)(p.copy_in_us / f / 1000.f), (double)(p.draw_us / f / 1000.f), (double)(p.copy_out_us / f / 1000.f),
                  (double)(p.static_us / f / 1000.f), (unsigned long)s.palettes, (double)(s.palette_worst_us / 1000.f),
                  g_copy ? "on" : "off", (unsigned long)copy_refr,
                  (unsigned long)copy_px, (unsigned long)(copy_us / 1000), g_lent ? "LVGL's (internal)" : "PSRAM",
                  !g_app ? "none" : (g_cache_spare ? "the spare picture" : "its own PSRAM"),
                  spare_lent() ? "lent" : "home", (unsigned long)spare_refused(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

uint16_t screensaver_carry() { return g_carry; }

void screensaver_set_carry(uint16_t word) {
  g_carry = word;
  g_taichi_at = g_mush_at = nowMs();
  refreshInputsSoon();
}

int screensaver_inputs(char *buf, size_t n) {
  static const char *const kScene[6] = {"its own pick", "evening", "morning", "midday", "lookout", "sunrise"};
  const ss::Inputs &in = g_in;
  char state[32];
  if (g_app) snprintf(state, sizeof state, "running, seed %lu", (unsigned long)g_seed);
  else snprintf(state, sizeof state, "stopped");
  char look[200];
  if (g_lookout_set) {
    static const char *const kSet[4] = {"", "the Lookout on", "both off", "the sunrise on"};
    snprintf(look, sizeof look, "%s (set by hand)", kSet[g_lookout_set]);
  } else {
    char set[16] = "none", rise[16] = "none";
    if (g_sunset_min >= 0) snprintf(set, sizeof set, "%d:%02d", g_sunset_min / 60, g_sunset_min % 60);
    if (g_sunrise_min >= 0) snprintf(rise, sizeof rise, "%d:%02d", g_sunrise_min / 60, g_sunrise_min % 60);
    snprintf(look, sizeof look, "%s (sunset %s; window %d min before to %d after; %s today); sunrise %s (sunrise %s; window %d before to %d after; %s today)",
             in.lookout_window ? "on" : "off", set, tc::kLookoutBeforeMin, tc::kLookoutAfterMin,
             g_lookout_shown.shown_day == g_today && g_today >= 0 ? "shown" : "not shown", in.sunrise_window ? "on" : "off", rise,
             kSunriseBeforeMin, kSunriseAfterMin, g_sunrise_shown.shown_day == g_today && g_today >= 0 ? "shown" : "not shown");
  }
  return snprintf(buf, n,
                  "screensaver inputs: %s; mood %.2f (%s); scene weather %d%s, overcast %.2f, fog %.2f; other floor %d "
                  "(5 = no signal); dogs %d; arrows %u %u %u; night %s (night gain %.1f); lookout %s; next loop %s; starts on its own %s after %lu s (%s; idle now %lu s)",
                  state, (double)in.mood, g_mood_override >= 0.f ? "set by hand" : "the clock",
                  in.weather_code,
                  g_weather_set >= 0 ? " set by hand" : (in.have_outside ? " from the outside reading" : " (no reading: its own pick)"),
                  (double)in.weather.overcast, (double)in.weather.fog, in.other_status, in.dog_out,
                  (unsigned)in.req[0], (unsigned)in.req[1], (unsigned)in.req[2],
                  in.night_mode ? "yes" : "no", (double)(g_night_gain > 0.f ? g_night_gain : 0.55f), look, kScene[g_scene],
                  g_auto ? "on" : "off", (unsigned long)(screensaver_idle()),
                  g_idle_set_ms ? "set by hand until restart" : "Settings > SCREENSAVER",
                  (unsigned long)(g_idle_now / 1000));
}

}  // namespace ui

#endif  // ESP_PLATFORM
