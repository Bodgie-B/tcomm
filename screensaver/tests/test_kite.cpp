// Tests for core/scene/kite: the three scripted variants of the plan (A serene,
// B the kite wins, C three failed launches, then it lifts out of his lap)
// driven by a real hiker rig's hand over a real landscape; bounds that cover
// every pixel drawn; clipping (a strip draw equals the frame draw and touches
// nothing outside the clip); kiteDirty's contract (empty when nothing visible
// changed, otherwise covering every changed pixel); every mode's motion and its
// ending; no NaN or runaway over long random runs, bad inputs and a replay;
// the dirty strips a flying kite costs; and how it reads in every mood.
// Plain executable; returns non-zero on failure.
//
// Writes inspection sheets to the directory in argv[1] (default: the current
// directory): kite_a.ppm, kite_b.ppm and kite_c.ppm (a frame every 0.5 s of
// each variant, 2x downscaled, 6 across, the time in seconds in each corner)
// and kite_zoom.ppm (the kite and tail at 3x, in every mood, in seven states).
// Convert to PNG for review (the scratch folder's ppm2png.py).
#include "anim/ease.h"
#include "anim/rng.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/buildup.h"
#include "scene/hiker.h"
#include "scene/kite.h"
#include "scene/landscape.h"

#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

static int g_fail = 0;
static int g_checks = 0;
static int g_printed = 0;
// Only the first 25 failures are printed; the count is exact.
#define CHECK(cond, ...)                                              \
  do {                                                                \
    ++g_checks;                                                       \
    if (!(cond)) {                                                    \
      ++g_fail;                                                       \
      if (g_printed++ < 25) {                                         \
        printf("  FAIL line %d: %s -- ", __LINE__, #cond);            \
        printf(__VA_ARGS__);                                          \
        printf("\n");                                                 \
      }                                                               \
    }                                                                 \
  } while (0)

// ---- helpers -----------------------------------------------------------------------------

static const int kW = SS_WIDTH, kH = SS_HEIGHT;
static const float kDt = SS_TICK_DT;
static const Rect kFrame{0, 0, kW, kH};
static const float kGround = 352.f;   // the hiker's feet and the kite's landing line

static Pixel g_noise[SS_WIDTH * SS_HEIGHT];   // a noise background: any stray write shows
static Pixel g_a[SS_WIDTH * SS_HEIGHT];
static Pixel g_b[SS_WIDTH * SS_HEIGHT];
static Pixel g_c[SS_WIDTH * SS_HEIGHT];
static void* g_scratch = nullptr;

static void fillNoise(Pixel* fb) {
  uint32_t h = 987654u;
  for (int i = 0; i < kW * kH; ++i) {
    h = h * 1664525u + 1013904223u;
    fb[i] = (Pixel)(h >> 16);
  }
}

static Surface frameSurface(Pixel* fb) { return Surface{fb, kW, kH, kW, 0}; }

static bool inside(const Rect& outer, const Rect& r) {
  return r.x0 >= outer.x0 && r.y0 >= outer.y0 && r.x1 <= outer.x1 && r.y1 <= outer.y1;
}
static Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
              a.y1 > b.y1 ? a.y1 : b.y1};
}
static bool finiteF(float v) { return v - v == 0.f; }
static bool finiteKite(const KiteState& k) {
  const float f[] = {k.t, k.hand.x, k.hand.y, k.pos.x, k.pos.y, k.vel.x, k.vel.y, k.rot, k.spin, k.r, k.line,
                     k.ground_y, k.phase, k.wind, k.fold, k.lay, k.end.x, k.end.y};
  for (float v : f)
    if (!finiteF(v)) return false;
  return true;
}

// Strips a row range touches, as the renderer's dirty mask.
static uint16_t rowsMask(const Rect& r) {
  if (r.empty()) return 0;
  const int y0 = r.y0 < 0 ? 0 : r.y0, y1 = r.y1 > kH ? kH : r.y1;
  if (y1 <= y0) return 0;
  uint16_t m = 0;
  for (int s = y0 / SS_STRIP_ROWS; s <= (y1 - 1) / SS_STRIP_ROWS; ++s) m = (uint16_t)(m | (1u << s));
  return m;
}
static int popc(uint16_t m) {
  int n = 0;
  for (; m; m = (uint16_t)(m & (m - 1))) ++n;
  return n;
}

static Palette palFor(float mood) {
  Palette p;
  computePalette(mood, WeatherTint{0.f, 0.f, mood < 0.5f ? 1.f : 0.f}, p);
  return p;
}

// The static landscape of the loop for each mood, without actors: the clearing the kite flies over.
static const int kMoods = 6;
static const char* const kMoodName[kMoods] = {"night", "dawn", "morning", "day", "golden", "dusk"};
static Pixel g_land[kMoods][SS_WIDTH * SS_HEIGHT];
static Palette g_pal[kMoods];

static void buildLandscapes() {
  static Landscape ls;
  Rng rng;
  rng.seed(7);
  landscapeGenerate(ls, rng);
  BuildState b;
  buildReset(b);
  buildSetProgress(b, 1.f);
  for (int m = 0; m < kMoods; ++m) {
    g_pal[m] = palFor((float)m);
    Surface s = frameSurface(g_land[m]);
    landscapeDrawStatic(ls, s, g_pal[m], (float)m, b);
  }
}

// Draw the kite over `bg` into `out`, only inside `region` (full-width rows, so the
// rasterizer's rows match a whole-frame draw exactly).
static void renderRegion(const KiteState& k, const Palette& pal, const Pixel* bg, const Rect& region, Pixel* out) {
  for (int y = region.y0; y < region.y1; ++y) memcpy(out + (size_t)y * kW, bg + (size_t)y * kW, sizeof(Pixel) * kW);
  Surface s = frameSurface(out);
  kiteDraw(k, s, region, pal);
}

static Rect diffBox(const Pixel* a, const Pixel* b, const Rect& region) {
  Rect r{kW, kH, 0, 0};
  for (int y = region.y0; y < region.y1; ++y)
    for (int x = region.x0; x < region.x1; ++x)
      if (a[y * kW + x] != b[y * kW + x]) {
        if (x < r.x0) r.x0 = x;
        if (y < r.y0) r.y0 = y;
        if (x + 1 > r.x1) r.x1 = x + 1;
        if (y + 1 > r.y1) r.y1 = y + 1;
      }
  if (r.x1 <= r.x0) return Rect{0, 0, 0, 0};
  return r;
}

// ---- the scripted variants ----------------------------------------------------------------

struct Sim {
  Hiker h;
  KiteState k;
  int variant;        // 0 A, 1 B, 2 C
  int tick;
  int ph;             // the script's phase
  int ph_tick;        // the tick it began
  float wind;         // the input wind this tick
  bool shown;         // the hiker is in the picture
  int crashes;        // times the kite came to rest on the ground (C)
  int pulled_past;    // PULL ticks with the hand beyond x = 800 (B)
  float max_hand_x;
  uint8_t last_mode;
  bool half_rate;     // step the kite every second tick with twice the dt
  int pick;           // the tick he began to crouch for the kite, 0 when not
};

static float bump(float t, float a, float b, float c, float d) {   // 0 before a, 1 on b..c, 0 after d
  return easeInOut(window(t, a, b)) * (1.f - easeInOut(window(t, c, d)));
}

static void simInit(Sim& s, int variant, bool half) {
  memset(&s, 0, sizeof s);
  s.variant = variant;
  s.half_rate = half;
  s.shown = true;
  const float x0 = variant == 0 ? 330.f : (variant == 1 ? 300.f : 280.f);
  hikerInit(s.h, Vec2{x0, kGround}, 1);
  hikerSetPose(s.h, POSE_STAND, 0.f);
  hikerTick(s.h, 0.f);
  kiteInit(s.k, kGround, 1, 11u + (uint32_t)variant * 5u);
  s.k.hand = hikerHandPos(s.h, 0);
  kiteHold(s.k, 0.f, 0.f);
  s.wind = 0.3f;
  s.last_mode = KITE_HIDDEN;
}

static float nowS(const Sim& s) { return (float)s.tick * kDt; }
static bool atS(const Sim& s, float secs) { return s.tick == (int)(secs * 25.f + 0.5f); }
static void phase(Sim& s, int ph) {
  s.ph = ph;
  s.ph_tick = s.tick;
}
static float inPhase(const Sim& s) { return (float)(s.tick - s.ph_tick) * kDt; }

// A: he walks backward, the kite lifts first time and climbs; a long calm watch with
// one gust; he reels it in and folds it.
static void scriptA(Sim& s) {
  const float t = nowS(s);
  s.wind = 0.3f + 0.45f * bump(t, 19.f, 21.f, 24.f, 28.f);
  if (atS(s, 2.f)) {
    hikerWalkTo(s.h, 270.f, 24.f);
    s.h.facing = 1;   // backward
  }
  if (atS(s, 3.2f)) {
    kiteLaunch(s.k, 1.f);
    kiteFly(s.k, 230.f);
  }
  if (atS(s, 6.f)) hikerSetPose(s.h, POSE_FISH_STAND, 0.8f);
  if (atS(s, 38.f)) kiteReel(s.k);
  if (s.ph == 0 && s.k.mode == KITE_HELD && t > 38.f) phase(s, 1);
  if (s.ph == 1) {
    const float u = easeInOut(window(inPhase(s), 1.f, 2.5f));
    kiteHold(s.k, u, 0.f);
    if (atS(s, 0.f)) {}
    if (inPhase(s) < 0.1f) hikerSetPose(s.h, POSE_STAND, 0.8f);
  }
}

// B: the same start, then a gust, and the kite pulls him off the right edge at a run;
// a beat of empty scene; he walks back in, the line dangling.
static void scriptB(Sim& s) {
  const float t = nowS(s);
  s.wind = 0.45f + 0.5f * easeInOut(window(t, 8.f, 9.5f));
  if (s.ph == 3 || s.ph == 4) s.wind = 0.3f;
  if (atS(s, 1.5f)) {
    kiteLaunch(s.k, 1.f);
    kiteFly(s.k, 200.f);
  }
  if (atS(s, 2.5f)) hikerSetPose(s.h, POSE_FISH_STAND, 0.8f);
  if (atS(s, 9.6f)) {
    kitePull(s.k);
    phase(s, 1);
  }
  if (s.ph == 1) {   // dragged
    const float v = kitePullSpeed(s.k);
    if (v > 0.5f) {
      hikerWalkTo(s.h, 1400.f, v);
      s.h.facing = 1;
    }
    if (s.k.hand.x > 830.f) {
      kiteLose(s.k);
      phase(s, 2);
    }
  }
  if (s.ph == 2) {   // off the edge: a few seconds of nobody
    hikerWalkTo(s.h, 1400.f, 80.f);
    if (s.h.feet.x > 900.f) s.shown = false;
    if (!s.shown && inPhase(s) > 4.f) {
      hikerInit(s.h, Vec2{900.f, kGround}, -1);
      hikerSetPose(s.h, POSE_STAND, 0.f);
      hikerWalkTo(s.h, 590.f, 34.f);
      s.shown = true;
      phase(s, 3);
    }
  }
  if (s.ph == 3 && !s.h.walking && inPhase(s) > 1.f) phase(s, 4);
}

// C: three launches that fail (a nosedive, a spin and crash, a lift that drops), then he
// sits with it in his lap and a breeze lifts it out of his hands.
static void walkToKite(Sim& s) {
  const float want = s.k.pos.x - 14.f;
  if (!s.h.walking && fabsf(s.h.feet.x - want) > 3.f) hikerWalkTo(s.h, want, 34.f);
}
static bool atKite(const Sim& s) {
  const float dx = s.k.pos.x - s.k.hand.x;
  return !s.h.walking && fabsf(dx) < 22.f;
}
// Walks to the fallen kite, crouches, picks it up and stands: true when it is in his hands.
static bool pickUp(Sim& s) {
  walkToKite(s);
  if (s.pick == 0 && atKite(s)) {
    hikerSetPose(s.h, POSE_CROUCH, 0.5f);
    s.pick = s.tick;
  }
  if (s.pick != 0 && s.tick - s.pick >= 20) {
    kiteHold(s.k, 0.f, 0.f);
    hikerSetPose(s.h, POSE_STAND, 0.6f);
    s.pick = 0;
    return true;
  }
  return false;
}
static void scriptC(Sim& s) {
  const float t = nowS(s);
  s.wind = 0.3f;
  const float ip = inPhase(s);
  switch (s.ph) {
    case 0:
      if (t >= 1.5f) {
        kiteLaunch(s.k, 1.f);
        kiteFly(s.k, 180.f);
        phase(s, 1);
      }
      break;
    case 1:   // 1: it nosedives
      if (ip >= 0.9f) {
        kiteNosedive(s.k);
        phase(s, 2);
      }
      break;
    case 2:   // wait for it to lie still, then fetch it
      if (s.k.mode == KITE_GROUND && s.k.t > 1.2f) phase(s, 3);
      break;
    case 3:
      if (pickUp(s)) phase(s, 4);
      break;
    case 4:   // 2: it spins and crashes
      if (ip >= 1.5f) {
        kiteLaunch(s.k, 1.f);
        kiteFly(s.k, 180.f);
        phase(s, 5);
      }
      break;
    case 5:
      if (ip >= 1.f) {
        kiteSpinCrash(s.k);
        phase(s, 6);
      }
      break;
    case 6:
      if (s.k.mode == KITE_GROUND && s.k.t > 1.2f) phase(s, 7);
      break;
    case 7:
      if (pickUp(s)) phase(s, 8);
      break;
    case 8:   // 3: it lifts a little, and drops
      if (ip >= 1.5f) {
        kiteLaunch(s.k, 0.7f);
        kiteFly(s.k, 150.f);
        phase(s, 9);
      }
      break;
    case 9:
      if (ip >= 1.f) {
        kiteDrop(s.k);
        phase(s, 10);
      }
      break;
    case 10:
      if (s.k.mode == KITE_GROUND && s.k.t > 1.5f) phase(s, 11);
      break;
    case 11:
      if (pickUp(s)) phase(s, 12);
      break;
    case 12:   // he sits down with it in his lap
      if (ip >= 1.f) {
        hikerSetPose(s.h, POSE_SIT, 1.2f);
        phase(s, 13);
      }
      break;
    case 13:
      kiteHold(s.k, 0.f, easeInOut(window(ip, 0.f, 1.2f)));
      if (ip >= 4.5f) {   // a breeze lifts it out of his hands
        kiteLaunch(s.k, 0.f);
        kiteFly(s.k, 200.f);
        s.wind = 0.45f;
        phase(s, 14);
      }
      break;
    case 14:
      s.wind = 0.45f;
      if (ip >= 0.8f) {   // he scrambles up after the line
        hikerSetPose(s.h, POSE_FISH_STAND, 1.0f);
        hikerWalkTo(s.h, s.h.feet.x + 50.f, 50.f);
        phase(s, 15);
      }
      break;
    default: s.wind = 0.4f; break;
  }
}

static void simScript(Sim& s) {
  if (s.variant == 0) scriptA(s);
  else if (s.variant == 1) scriptB(s);
  else scriptC(s);
}

// ---- per-tick checks ------------------------------------------------------------------------

struct Stats {
  // While flying (FLY, settled): strips the kite dirties a frame.
  long fly_ticks;
  long bounds_strips, dirty_strips, beyond_hiker;   // sums
  int bounds_max, dirty_max, beyond_max;
  long changed_ticks;                               // flying ticks on which anything changed
  long all_ticks, dirty_all;
};

static void checkTick(const KiteState& prev, const KiteState& cur, const Palette& pal, const Pixel* bg, const char* who,
                      int tick) {
  CHECK(finiteKite(cur), "%s tick %d: state not finite", who, tick);
  Rect rs[KITE_DIRTY_MAX];
  const int n = kiteDirty(prev, cur, rs);
  CHECK(n >= 0 && n <= KITE_DIRTY_MAX, "%s tick %d: %d dirty rects", who, tick, n);
  const bool same = kiteSame(prev, cur);
  CHECK(same == (n == 0), "%s tick %d: kiteSame %d but %d dirty rects", who, tick, same, n);
  const Rect reg0 = unite(kiteBounds(prev), kiteBounds(cur));
  const Rect reg = reg0.clipped(kFrame);
  const Rect rows{0, reg.y0, kW, reg.y1};
  for (int i = 0; i < n; ++i) CHECK(inside(reg0, rs[i]) && !rs[i].empty(), "%s tick %d: dirty rect %d outside the bounds", who, tick, i);
  if (reg.empty()) return;
  renderRegion(prev, pal, bg, rows, g_a);
  renderRegion(cur, pal, bg, rows, g_b);
  int stray = 0;
  for (int y = rows.y0; y < rows.y1; ++y)
    for (int x = 0; x < kW; ++x) {
      if (g_a[y * kW + x] == g_b[y * kW + x]) continue;
      bool in = false;
      for (int i = 0; i < n; ++i) in = in || (x >= rs[i].x0 && x < rs[i].x1 && y >= rs[i].y0 && y < rs[i].y1);
      if (!in) ++stray;
    }
  CHECK(stray == 0, "%s tick %d: %d changed pixels outside the dirty rects (%d rects)", who, tick, stray, n);
}

// Bounds, clipping and strips for one state, over the noise background.
static void checkDraw(const KiteState& k, const Palette& pal, const char* who, int tick, uint32_t salt) {
  const Rect b = kiteBounds(k);
  // 1. The whole-frame draw touches nothing outside the bounds.
  memcpy(g_a, g_noise, sizeof g_a);
  Surface full = frameSurface(g_a);
  kiteDraw(k, full, kFrame, pal);
  const Rect d = diffBox(g_a, g_noise, kFrame);
  CHECK(d.empty() || inside(b, d), "%s tick %d: pixels outside the bounds: drawn %d,%d..%d,%d bounds %d,%d..%d,%d", who,
        tick, d.x0, d.y0, d.x1, d.y1, b.x0, b.y0, b.x1, b.y1);
  CHECK(b.empty() == d.empty() || !b.empty(), "%s tick %d: pixels drawn with empty bounds", who, tick);

  // 2. Strip draws equal the frame draw.
  memcpy(g_b, g_noise, sizeof g_b);
  for (int sy = 0; sy < SS_STRIP_COUNT; ++sy) {
    Surface strip{g_b + (size_t)sy * SS_STRIP_ROWS * kW, kW, SS_STRIP_ROWS, kW, sy * SS_STRIP_ROWS};
    kiteDraw(k, strip, Rect{0, sy * SS_STRIP_ROWS, kW, (sy + 1) * SS_STRIP_ROWS}, pal);
  }
  CHECK(memcmp(g_a, g_b, sizeof g_a) == 0, "%s tick %d: strip draws differ from the frame draw", who, tick);

  // 3. A clip, even one that cuts across the kite, is respected.
  uint32_t h = salt * 2654435761u + (uint32_t)tick * 40503u + 1u;
  for (int c = 0; c < 2; ++c) {
    h = h * 1664525u + 1013904223u;
    const int x0 = (int)((h >> 8) % 760u), y0 = (int)((h >> 18) % 440u);
    h = h * 1664525u + 1013904223u;
    Rect clip{x0, y0, x0 + 20 + (int)((h >> 8) % 300u), y0 + 10 + (int)((h >> 20) % 150u)};
    if (c == 1 && !b.empty()) {   // a cut through the middle of the kite
      clip = Rect{(b.x0 + b.x1) / 2 - 15, (b.y0 + b.y1) / 2 - 20, (b.x0 + b.x1) / 2 + 40, (b.y0 + b.y1) / 2 + 20};
    }
    memcpy(g_c, g_noise, sizeof g_c);
    Surface s = frameSurface(g_c);
    kiteDraw(k, s, clip, pal);
    const Rect dc = diffBox(g_c, g_noise, kFrame);
    CHECK(dc.empty() || inside(clip.clipped(kFrame), dc), "%s tick %d: draw outside clip %d,%d..%d,%d: %d,%d..%d,%d", who,
          tick, clip.x0, clip.y0, clip.x1, clip.y1, dc.x0, dc.y0, dc.x1, dc.y1);
  }
}

// ---- 2x sheets -------------------------------------------------------------------------------

static bool writePPM(const char* path, const unsigned char* rgb, int w, int h) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  fwrite(rgb, 1, (size_t)w * (size_t)h * 3, f);
  fclose(f);
  return true;
}

struct Sheet {
  int cols, rows, tw, th, gap;
  unsigned char* rgb;
  int w() const { return cols * tw + (cols + 1) * gap; }
  int h() const { return rows * th + (rows + 1) * gap; }
  void init(int c, int r, int tile_w, int tile_h) {
    cols = c;
    rows = r;
    tw = tile_w;
    th = tile_h;
    gap = 4;
    rgb = (unsigned char*)malloc((size_t)w() * (size_t)h() * 3);
    for (size_t i = 0; i < (size_t)w() * (size_t)h(); ++i) {
      rgb[i * 3] = 40;
      rgb[i * 3 + 1] = 40;
      rgb[i * 3 + 2] = 40;
    }
  }
  void put(int x, int y, unsigned char r, unsigned char g, unsigned char b) {
    unsigned char* o = rgb + ((size_t)y * (size_t)w() + (size_t)x) * 3;
    o[0] = r;
    o[1] = g;
    o[2] = b;
  }
  int originX(int i) const { return gap + (i % cols) * (tw + gap); }
  int originY(int i) const { return gap + (i / cols) * (th + gap); }
  // A rect of the frame, scaled down by 2 (box filter) or up by an integer.
  void tileDown2(int i, const Pixel* fb) {
    const int ox = originX(i), oy = originY(i);
    for (int y = 0; y < th; ++y)
      for (int x = 0; x < tw; ++x) {
        int r = 0, g = 0, b = 0;
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx) {
            const Pixel p = fb[(y * 2 + dy) * kW + x * 2 + dx];
            r += red8(p);
            g += green8(p);
            b += blue8(p);
          }
        put(ox + x, oy + y, (unsigned char)(r / 4), (unsigned char)(g / 4), (unsigned char)(b / 4));
      }
  }
  void tileUp(int i, const Pixel* fb, const Rect& crop, int z) {
    const int ox = originX(i), oy = originY(i);
    for (int y = 0; y < th; ++y)
      for (int x = 0; x < tw; ++x) {
        const int sx = crop.x0 + x / z, sy = crop.y0 + y / z;
        if (sx < 0 || sy < 0 || sx >= kW || sy >= kH) continue;
        const Pixel p = fb[sy * kW + sx];
        put(ox + x, oy + y, red8(p), green8(p), blue8(p));
      }
  }
};

// A 3x5 digit font for the time labels.
static const unsigned char kDigits[11][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {0, 0, 0, 0, 2}};   // 10: the point

static void label(Sheet& sh, int tile, const char* text) {
  const int ox = sh.originX(tile) + 3, oy = sh.originY(tile) + 3;
  const int n = (int)strlen(text);
  for (int y = -1; y < 11; ++y)
    for (int x = -1; x < n * 8 + 1; ++x) sh.put(ox + x, oy + y, 0, 0, 0);
  for (int c = 0; c < n; ++c) {
    const int g = text[c] == '.' ? 10 : text[c] - '0';
    if (g < 0 || g > 10) continue;
    for (int y = 0; y < 5; ++y)
      for (int x = 0; x < 3; ++x)
        if (kDigits[g][y] & (4 >> x))
          for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) sh.put(ox + c * 8 + x * 2 + dx, oy + y * 2 + dy, 255, 255, 255);
  }
}

// ---- running a variant ------------------------------------------------------------------------

struct Result {
  Sim s;
  Stats st;
  uint8_t modes_seen[KITE_MODES];
  float min_kite_y, max_kite_x;
  int ticks;
};

static void drawScene(const Sim& s, const Palette& pal, const Pixel* bg, Pixel* out) {
  memcpy(out, bg, sizeof(Pixel) * (size_t)kW * kH);
  Surface surf = frameSurface(out);
  if (s.shown) hikerDraw(s.h, surf, pal, kFrame);
  kiteDraw(s.k, surf, kFrame, pal);
}

static Result* runVariant(int variant, bool half, int seconds, const char* name, Sheet* sheet, bool deep) {
  static Result res;
  memset(&res, 0, sizeof res);
  Sim& s = res.s;
  simInit(s, variant, half);
  const Palette& pal = g_pal[3];
  const Pixel* bg = g_land[3];
  res.min_kite_y = 1.0e9f;
  res.max_kite_x = -1.0e9f;
  const int total = seconds * 25;
  int next_frame = 0;
  for (int tick = 0; tick <= total; ++tick) {
    s.tick = tick;
    const KiteState prev = s.k;
    simScript(s);
    hikerTick(s.h, kDt);
    const Vec2 hand = hikerHandPos(s.h, 0);
    if (!half || (tick & 1) == 0) kiteStep(s.k, hand, s.wind, half ? 2.f * kDt : kDt);
    else s.k.hand = hand;   // (the hand moved; the kite did not step: kiteStep reads it next time)
    if (s.k.hand.x > s.max_hand_x) s.max_hand_x = s.k.hand.x;
    if (s.k.mode == KITE_PULL && s.k.hand.x > 800.f) ++s.pulled_past;
    if (s.k.mode == KITE_GROUND && s.last_mode != KITE_GROUND) ++s.crashes;
    s.last_mode = s.k.mode;
    res.modes_seen[s.k.mode] = 1;
    if (s.k.mode != KITE_HIDDEN && s.k.mode != KITE_GONE) {
      if (s.k.pos.y < res.min_kite_y) res.min_kite_y = s.k.pos.y;
      if (s.k.pos.x > res.max_kite_x) res.max_kite_x = s.k.pos.x;
    }

    // Dirty strips a frame, as the renderer would count them.
    {
      const bool changed = !kiteSame(prev, s.k);
      uint16_t mb = 0;
      if (changed) mb = (uint16_t)(rowsMask(kiteBounds(prev)) | rowsMask(kiteBounds(s.k)));
      Rect rs[KITE_DIRTY_MAX];
      const int n = kiteDirty(prev, s.k, rs);
      uint16_t md = 0;
      for (int i = 0; i < n; ++i) md = (uint16_t)(md | rowsMask(rs[i]));
      const uint16_t mh = s.shown ? rowsMask(hikerBounds(s.h)) : (uint16_t)0;
      ++res.st.all_ticks;
      res.st.dirty_all += popc(md);
      if (s.k.mode == KITE_FLY && s.k.t > 3.f) {
        ++res.st.fly_ticks;
        if (changed) ++res.st.changed_ticks;
        const int a = popc(mb), b = popc(md), c = popc((uint16_t)(md & ~mh));
        res.st.bounds_strips += a;
        res.st.dirty_strips += b;
        res.st.beyond_hiker += c;
        if (a > res.st.bounds_max) res.st.bounds_max = a;
        if (b > res.st.dirty_max) res.st.dirty_max = b;
        if (c > res.st.beyond_max) res.st.beyond_max = c;
      }
    }

    if (deep) {
      checkTick(prev, s.k, pal, bg, name, tick);
      if (tick % 4 == 0) checkDraw(s.k, pal, name, tick, (uint32_t)variant);
    } else {
      CHECK(finiteKite(s.k), "%s tick %d: state not finite", name, tick);
    }

    if (sheet && tick == (25 * next_frame + 1) / 2) {   // every 0.5 s: ticks 0, 13, 25, 38, ...
      drawScene(s, pal, bg, g_c);
      sheet->tileDown2(next_frame, g_c);
      char lab[16];
      snprintf(lab, sizeof lab, "%d.%d", (next_frame / 2), (next_frame & 1) ? 5 : 0);
      label(*sheet, next_frame, lab);
      ++next_frame;
    }
  }
  res.ticks = total;
  return &res;
}

static void printStrips(const char* what, const Stats& st) {
  if (st.fly_ticks == 0) {
    printf("  %s: no settled flying ticks\n", what);
    return;
  }
  const double n = (double)st.fly_ticks;
  printf("  %s: flying %ld ticks, anything changed on %.0f%% of them; strips per frame, kite alone:\n", what, st.fly_ticks,
         100.0 * (double)st.changed_ticks / n);
  printf("      whole bounds %.2f avg (max %d)   kiteDirty %.2f avg (max %d)   kiteDirty beyond the hiker's strips %.2f avg (max %d)\n",
         (double)st.bounds_strips / n, st.bounds_max, (double)st.dirty_strips / n, st.dirty_max,
         (double)st.beyond_hiker / n, st.beyond_max);
}

// ---- the tests ------------------------------------------------------------------------------------

static void testVariantA(const char* dir) {
  printf("variant A: serene\n");
  Sheet sh;
  sh.init(6, 16, 400, 240);
  Result* r = runVariant(0, false, 47, "A", &sh, true);
  char path[512];
  snprintf(path, sizeof path, "%s%skite_a.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d)\n", path, sh.w(), sh.h());
  free(sh.rgb);
  CHECK(r->modes_seen[KITE_LAUNCH] && r->modes_seen[KITE_FLY] && r->modes_seen[KITE_REEL], "A: launch, fly and reel all seen");
  CHECK(!r->modes_seen[KITE_NOSE] && !r->modes_seen[KITE_SPIN] && !r->modes_seen[KITE_DROP] && !r->modes_seen[KITE_PULL],
        "A: no crash or pull");
  CHECK(r->s.k.mode == KITE_HELD, "A ends with the kite reeled in and held (mode %d)", r->s.k.mode);
  CHECK(r->s.k.fold > 0.95f, "A ends folded (fold %.2f)", (double)r->s.k.fold);
  CHECK(r->min_kite_y < kGround - 150.f, "A: the kite climbed well above his head (highest y %.0f)", (double)r->min_kite_y);
  CHECK(r->min_kite_y > 20.f, "A: and stayed in the picture (highest y %.0f)", (double)r->min_kite_y);
  printStrips("A (kite stepped every tick)", r->st);

  Result* h = runVariant(0, true, 47, "A half", nullptr, false);
  CHECK(h->s.k.mode == KITE_HELD, "A at 12.5 Hz also ends held (mode %d)", h->s.k.mode);
  printStrips("A (kite stepped every second tick, dt doubled)", h->st);
}

static void testVariantB(const char* dir) {
  printf("variant B: the kite wins\n");
  Sheet sh;
  sh.init(6, 14, 400, 240);
  Result* r = runVariant(1, false, 40, "B", &sh, true);
  char path[512];
  snprintf(path, sizeof path, "%s%skite_b.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d)\n", path, sh.w(), sh.h());
  free(sh.rgb);
  CHECK(r->modes_seen[KITE_PULL], "B: the pull happened");
  CHECK(r->s.max_hand_x > 800.f, "B: he was pulled past x = 800 (%.0f)", (double)r->s.max_hand_x);
  CHECK(r->s.pulled_past > 0, "B: he was still being pulled when his hand left the picture (%d ticks)", r->s.pulled_past);
  CHECK(r->s.k.mode == KITE_GONE, "B ends with the kite gone (mode %d)", r->s.k.mode);
  CHECK(r->s.k.hand.x < 700.f && r->s.shown, "B: he walked back in (hand x %.0f)", (double)r->s.k.hand.x);
  const Rect b = kiteBounds(r->s.k);
  CHECK(!b.empty() && b.x1 > b.x0 + 8 && inside(Rect{0, 0, kW, kH}, b), "B: the line dangles from his hand, in the picture (%d,%d..%d,%d)", b.x0,
        b.y0, b.x1, b.y1);
  CHECK(b.y1 <= (int)kGround + 3, "B: the loose line lies on the ground (bottom %d)", b.y1);
  printf("  hand reached x=%.0f; the kite was far at x=%.0f\n", (double)r->s.max_hand_x, (double)r->max_kite_x);
}

static void testVariantC(const char* dir) {
  printf("variant C: it won't fly\n");
  Sheet sh;
  sh.init(6, 22, 400, 240);
  Result* r = runVariant(2, false, 64, "C", &sh, true);
  char path[512];
  snprintf(path, sizeof path, "%s%skite_c.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d)\n", path, sh.w(), sh.h());
  free(sh.rgb);
  CHECK(r->modes_seen[KITE_NOSE] && r->modes_seen[KITE_SPIN] && r->modes_seen[KITE_DROP], "C: nosedive, spin and drop all seen");
  CHECK(r->s.crashes == 3, "C: it came down three times (%d)", r->s.crashes);
  CHECK(r->s.ph >= 14, "C: the script got to the lap and the breeze (phase %d)", r->s.ph);
  CHECK(r->s.k.mode == KITE_FLY, "C ends flying (mode %d)", r->s.k.mode);
  CHECK(r->s.k.pos.y < r->s.k.hand.y - 100.f, "C: and high above his hand (kite y %.0f, hand y %.0f)", (double)r->s.k.pos.y,
        (double)r->s.k.hand.y);
  CHECK(r->s.k.r > 150.f, "C: with the line paid out (%.0f px)", (double)r->s.k.r);
  printStrips("C (the climb after the lift)", r->st);
}

// ---- the modes one at a time -----------------------------------------------------------------------

struct Rig {
  KiteState k;
  Vec2 hand;
  float wind;
  void init(int side = 1) {
    kiteInit(k, kGround, side, 3u);
    hand = Vec2{300.f, 330.f};
    wind = 0.35f;
    k.hand = hand;
    kiteHold(k, 0.f, 0.f);
    run(0.5f);
  }
  void run(float secs) {
    const int n = (int)(secs * 25.f + 0.5f);
    for (int i = 0; i < n; ++i) kiteStep(k, hand, wind, kDt);
  }
};

// The lowest row any pixel of the kite reaches, drawn over a flat background.
static Pixel g_flat[SS_WIDTH * SS_HEIGHT];
static Rect drawnBox(const KiteState& k) {
  memcpy(g_a, g_flat, sizeof g_a);
  Surface s = frameSurface(g_a);
  kiteDraw(k, s, kFrame, g_pal[3]);
  return diffBox(g_a, g_flat, kFrame);
}
static float lowestPx(const KiteState& k) {
  const Rect b = drawnBox(k);
  return b.empty() ? -1e9f : (float)b.y1;
}

static void testModes() {
  printf("modes\n");
  Rig r;

  // HIDDEN: nothing drawn, no bounds.
  kiteInit(r.k, kGround, 1, 1u);
  CHECK(kiteBounds(r.k).empty(), "hidden: empty bounds");
  kiteStep(r.k, Vec2{100.f, 300.f}, 0.3f, kDt);
  CHECK(kiteBounds(r.k).empty() && r.k.mode == KITE_HIDDEN, "hidden after a step");
  memcpy(g_a, g_noise, sizeof g_a);
  Surface s = frameSurface(g_a);
  kiteDraw(r.k, s, kFrame, g_pal[3]);
  CHECK(memcmp(g_a, g_noise, sizeof g_a) == 0, "hidden draws nothing");

  // HELD: in his hand, folded or flat, upright or in his lap.
  r.init();
  CHECK(r.k.mode == KITE_HELD, "held");
  float dx = r.k.pos.x - r.hand.x, dy = r.k.pos.y - r.hand.y;
  CHECK(sqrtf(dx * dx + dy * dy) < 16.f, "held: the kite is at his hand (%.1f px)", (double)sqrtf(dx * dx + dy * dy));
  for (int i = 0; i < 100; ++i) {   // carried: no lag behind a walking hand
    r.hand.x += 1.4f;
    kiteStep(r.k, r.hand, r.wind, kDt);
  }
  dx = r.k.pos.x - r.hand.x;
  CHECK(fabsf(dx) < 12.f, "held: carried along with a walking hand (%.1f px off)", (double)dx);
  kiteHold(r.k, 1.f, 0.f);
  r.run(1.f);
  CHECK(r.k.fold > 0.97f && r.k.lay < 0.03f, "held: folds when asked (fold %.2f)", (double)r.k.fold);
  kiteHold(r.k, 0.f, 1.f);
  r.run(1.f);
  CHECK(r.k.lay > 0.97f && r.k.fold < 0.03f, "held: lies in his lap when asked (lay %.2f)", (double)r.k.lay);
  const Rect lap = kiteBounds(r.k);
  CHECK(lap.y1 - lap.y0 < 80, "held in his lap: low and flat (%d rows)", lap.y1 - lap.y0);

  // LAUNCH then FLY by itself; climbs, the line pays out to its length.
  r.init();
  kiteLaunch(r.k, 1.f);
  kiteFly(r.k, 200.f);
  CHECK(r.k.mode == KITE_LAUNCH, "launch");
  r.run(0.5f);
  CHECK(r.k.pos.y < r.hand.y - 20.f, "launch: climbing within half a second (y %.0f)", (double)r.k.pos.y);
  r.run(0.9f);
  CHECK(r.k.mode == KITE_FLY, "launch becomes fly after about 1.2 s (mode %d)", r.k.mode);
  r.run(8.f);
  CHECK(fabsf(r.k.r - 200.f) < 1.f, "fly: the line is out to 200 (%.1f)", (double)r.k.r);
  dx = r.k.pos.x - r.hand.x;
  dy = r.hand.y - r.k.pos.y;
  CHECK(sqrtf(dx * dx + dy * dy) <= 201.f, "fly: never farther than the line (%.1f)", (double)sqrtf(dx * dx + dy * dy));
  CHECK(dx > 40.f && dy > 100.f, "fly: up and out on its side (dx %.0f, dy %.0f)", (double)dx, (double)dy);
  // Mirrored: the other side.
  Rig m;
  m.init(-1);
  kiteLaunch(m.k, 1.f);
  kiteFly(m.k, 200.f);
  m.run(10.f);
  CHECK(m.k.pos.x < m.hand.x - 40.f, "fly: side -1 flies on the -x side (dx %.0f)", (double)(m.k.pos.x - m.hand.x));

  // Wind: stronger wind lays it lower and farther out; it sways (the position moves) in flight.
  Rig lo, hi;
  lo.init();
  hi.init();
  lo.wind = 0.3f;
  hi.wind = 1.f;
  for (Rig* q : {&lo, &hi}) {
    kiteLaunch(q->k, 1.f);
    kiteFly(q->k, 200.f);
    q->run(10.f);
  }
  const float elev_lo = (lo.hand.y - lo.k.pos.y) / 200.f, elev_hi = (hi.hand.y - hi.k.pos.y) / 200.f;
  CHECK(elev_hi < elev_lo - 0.1f, "wind lays the kite lower (height/line %.2f at wind 0.3, %.2f at 1)", (double)elev_lo, (double)elev_hi);
  float xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
  for (int i = 0; i < 25 * 20; ++i) {
    lo.run(0.04f);
    xmin = fminf(xmin, lo.k.pos.x);
    xmax = fmaxf(xmax, lo.k.pos.x);
    ymin = fminf(ymin, lo.k.pos.y);
    ymax = fmaxf(ymax, lo.k.pos.y);
  }
  CHECK(xmax - xmin > 8.f && xmax - xmin < 60.f, "fly sways side to side by a few tens of px (%.1f)", (double)(xmax - xmin));
  CHECK(ymax - ymin > 1.f && ymax - ymin < 40.f, "fly bobs a little (%.1f)", (double)(ymax - ymin));
  printf("  calm fly sway %.1f px wide, bob %.1f px; height over line %.2f calm, %.2f at full wind\n", (double)(xmax - xmin),
         (double)(ymax - ymin), (double)elev_lo, (double)elev_hi);

  // Dead calm: it cannot hold its height.
  Rig dead;
  dead.init();
  dead.wind = 0.f;
  kiteLaunch(dead.k, 1.f);
  kiteFly(dead.k, 200.f);
  dead.run(10.f);
  CHECK(dead.k.pos.y > lo.k.pos.y + 30.f, "no wind: it sags (y %.0f against %.0f)", (double)dead.k.pos.y, (double)lo.k.pos.y);

  // The line walks out and winds in at its own speeds.
  lo.k.line = 120.f;
  lo.run(1.f);
  CHECK(lo.k.r < 200.f - 55.f && lo.k.r > 200.f - 80.f, "winding in to 120: 70 px/s is a bit under (%.0f)", (double)lo.k.r);

  // REEL ends HELD at the hand.
  lo.init();
  kiteLaunch(lo.k, 1.f);
  kiteFly(lo.k, 200.f);
  lo.run(8.f);
  kiteReel(lo.k);
  lo.run(6.f);
  CHECK(lo.k.mode == KITE_HELD, "reel ends held (mode %d)", lo.k.mode);
  dx = lo.k.pos.x - lo.hand.x;
  dy = lo.k.pos.y - lo.hand.y;
  CHECK(sqrtf(dx * dx + dy * dy) < 16.f, "reel: and the kite is at his hand");

  // NOSE, SPIN, DROP: end GROUND, on the ground, never below it, line gathered in.
  const char* names[3] = {"nosedive", "spin and crash", "drop"};
  for (int m3 = 0; m3 < 3; ++m3) {
    Rig q;
    q.init();
    kiteLaunch(q.k, 1.f);
    kiteFly(q.k, 180.f);
    q.run(1.5f);
    const float y_before = q.k.pos.y;
    if (m3 == 0) kiteNosedive(q.k);
    if (m3 == 1) kiteSpinCrash(q.k);
    if (m3 == 2) kiteDrop(q.k);
    const uint8_t want_mode = m3 == 0 ? KITE_NOSE : (m3 == 1 ? KITE_SPIN : KITE_DROP);
    CHECK(q.k.mode == want_mode, "%s: mode set", names[m3]);
    float t_land = -1.f, max_y = -1e9f, low_px = -1e9f;
    for (int i = 0; i < 25 * 12; ++i) {
      q.run(0.04f);
      max_y = fmaxf(max_y, lowestPx(q.k));
      if (q.k.mode == KITE_GROUND && t_land < 0.f) t_land = (float)i * kDt;
      low_px = fmaxf(low_px, q.k.pos.y);
    }
    CHECK(q.k.mode == KITE_GROUND, "%s: ends on the ground (mode %d)", names[m3], q.k.mode);
    CHECK(t_land > 0.3f && t_land < 6.f, "%s: lands in a believable time (%.2f s)", names[m3], (double)t_land);
    CHECK(max_y <= kGround + 4.f, "%s: never below the ground line (lowest %.0f)", names[m3], (double)max_y);
    CHECK(q.k.lay > 0.9f, "%s: lying flat (lay %.2f)", names[m3], (double)q.k.lay);
    CHECK(fabsf(q.k.vel.x) < 1.f && fabsf(q.k.vel.y) < 1.f, "%s: at rest", names[m3]);
    CHECK(q.k.r < 140.f, "%s: the slack is gathered in (%.0f px of line)", names[m3], (double)q.k.r);
    printf("  %-15s from y=%.0f lands after %.2f s at x=%.0f\n", names[m3], (double)y_before, (double)t_land,
           (double)q.k.pos.x);
    // He walks over and picks it up.
    kiteHold(q.k, 0.f, 0.f);
    q.hand = Vec2{q.k.pos.x - 6.f, 330.f};
    q.run(1.5f);
    CHECK(q.k.mode == KITE_HELD && q.k.lay < 0.05f, "%s: picked up (lay %.2f)", names[m3], (double)q.k.lay);
  }
  // A commanded crash on a held kite is ignored.
  Rig ig;
  ig.init();
  kiteNosedive(ig.k);
  kiteSpinCrash(ig.k);
  kiteDrop(ig.k);
  kitePull(ig.k);
  kiteReel(ig.k);
  CHECK(ig.k.mode == KITE_HELD, "air commands do nothing to a held kite");

  // PULL: the pull speed ramps from nothing to a run; zero in every other mode.
  Rig p;
  p.init();
  kiteLaunch(p.k, 1.f);
  kiteFly(p.k, 200.f);
  p.run(6.f);
  CHECK(kitePullSpeed(p.k) == 0.f, "no pull speed while flying");
  kitePull(p.k);
  CHECK(p.k.mode == KITE_PULL, "pull");
  float v_prev = 0.f, v_max = 0.f;
  bool mono = true;
  float v_at_half = -1.f, v_at_1 = -1.f;
  float lowest_elev = 1e9f;
  for (int i = 0; i < 25 * 6; ++i) {
    p.run(0.04f);
    const float v = kitePullSpeed(p.k);
    if (v < v_prev - 1e-3f) mono = false;
    v_prev = v;
    v_max = fmaxf(v_max, v);
    if (i == 12) v_at_half = v;
    if (i == 25) v_at_1 = v;
    lowest_elev = fminf(lowest_elev, p.hand.y - p.k.pos.y);
    p.hand.x += v * kDt;   // he is dragged
  }
  CHECK(mono, "pull speed only ever grows");
  CHECK(v_at_half < 1.f, "pull: he digs in for half a second (%.1f px/s at 0.5 s)", (double)v_at_half);
  CHECK(v_at_1 > 5.f && v_at_1 < 40.f, "pull: a skid by 1 s (%.1f px/s)", (double)v_at_1);
  CHECK(v_max > 85.f && v_max <= 90.5f, "pull: up to a run (%.1f px/s)", (double)v_max);
  dx = p.k.pos.x - p.hand.x;
  dy = p.hand.y - p.k.pos.y;
  CHECK(dx > 120.f && dy < 150.f, "pull: the kite lies low and far ahead (dx %.0f, up %.0f)", (double)dx, (double)dy);
  const float dist = sqrtf(dx * dx + dy * dy);
  CHECK(dist > 190.f && dist <= 201.f, "pull: the line is taut (%.0f of 200)", (double)dist);
  Rig q2;
  q2.init(-1);
  kiteLaunch(q2.k, 1.f);
  kiteFly(q2.k, 200.f);
  q2.run(4.f);
  kitePull(q2.k);
  q2.run(5.f);
  CHECK(kitePullSpeed(q2.k) < -80.f, "pull on side -1 drags toward -x (%.1f)", (double)kitePullSpeed(q2.k));

  // GONE: only the loose line, trailing behind the hand.
  Rig g;
  g.init();
  kiteLaunch(g.k, 1.f);
  kiteFly(g.k, 200.f);
  g.run(5.f);
  kiteLose(g.k);
  g.hand.x = 600.f;
  g.run(0.2f);
  for (int i = 0; i < 75; ++i) {
    g.hand.x -= 1.4f;
    g.run(0.04f);
  }
  CHECK(g.k.mode == KITE_GONE && g.k.dir == 1, "gone: the line trails on the side he came from (dir %d)", g.k.dir);
  CHECK(g.k.end.x > g.hand.x + 45.f && g.k.end.x < g.hand.x + 75.f, "gone: the loose end lies 56 to 70 px behind (it lags a walking hand) (%.0f)", (double)(g.k.end.x - g.hand.x));
  CHECK(fabsf(g.k.end.y - (kGround - 1.f)) < 0.1f, "gone: and on the ground");
  const Rect gb = kiteBounds(g.k);
  CHECK(!gb.empty() && gb.y1 <= (int)kGround + 3, "gone: line bounds above the ground (%d)", gb.y1);

  // Hide.
  kiteHide(g.k);
  CHECK(kiteBounds(g.k).empty(), "hide: nothing");

  // Sizes: the diamond is about 26 x 34 px, tail 36 below.
  Rig sz;
  sz.init();
  kiteLaunch(sz.k, 1.f);
  kiteFly(sz.k, 200.f);
  sz.run(12.f);
  sz.k.rot = 0.f;
  sz.k.vel = Vec2{0.f, 0.f};
  sz.k.fold = sz.k.lay = 0.f;
  memcpy(g_a, g_flat, sizeof g_a);
  Surface ss = frameSurface(g_a);
  kiteDraw(sz.k, ss, kFrame, g_pal[3]);
  // The sail's two colors, found by pixel.
  const Pixel c_l = g_pal[3][MUSHROOM], c_r = g_pal[3][TENT_LIT];
  Rect sail{kW, kH, 0, 0};
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x)
      if (g_a[y * kW + x] == c_l || g_a[y * kW + x] == c_r) {
        if (x < sail.x0) sail.x0 = x;
        if (y < sail.y0) sail.y0 = y;
        if (x + 1 > sail.x1) sail.x1 = x + 1;
        if (y + 1 > sail.y1) sail.y1 = y + 1;
      }
  // Bows have the same colors, so take the sail from the rows above the tail's start.
  Rect body{kW, kH, 0, 0};
  const int cy = (int)floorf(sz.k.pos.y + 0.5f);
  for (int y = cy - 12; y <= cy + 23; ++y)
    for (int x = 0; x < kW; ++x)
      if (g_a[y * kW + x] == c_l || g_a[y * kW + x] == c_r) {
        if (x < body.x0) body.x0 = x;
        if (y < body.y0) body.y0 = y;
        if (x + 1 > body.x1) body.x1 = x + 1;
        if (y + 1 > body.y1) body.y1 = y + 1;
      }
  // The whole kite, tail and all, held up high enough that nothing touches the ground: no line.
  Rig up;
  up.init();
  up.hand = Vec2{300.f, 220.f};
  up.run(0.6f);
  const Rect all = drawnBox(up.k);
  printf("  upright sail %dx%d px (colour pixels, outline not counted); held with its tail %dx%d, bounds %dx%d\n",
         body.x1 - body.x0, body.y1 - body.y0, all.x1 - all.x0, all.y1 - all.y0, kiteBounds(up.k).x1 - kiteBounds(up.k).x0,
         kiteBounds(up.k).y1 - kiteBounds(up.k).y0);
  CHECK(body.x1 - body.x0 >= 20 && body.x1 - body.x0 <= 28, "sail is about 26 px wide (%d)", body.x1 - body.x0);
  CHECK(body.y1 - body.y0 >= 25 && body.y1 - body.y0 <= 36, "sail is about 34 px tall (%d)", body.y1 - body.y0);
  CHECK(all.x1 - all.x0 >= 24 && all.x1 - all.x0 <= 34, "kite is about 28 px wide with its outline (%d)", all.x1 - all.x0);
  CHECK(all.y1 - all.y0 >= 60 && all.y1 - all.y0 <= 80, "kite and tail are about 70 rows (%d)", all.y1 - all.y0);
  (void)sail;
}

// ---- the draw and the dirty areas, in the modes --------------------------------------------------------

static void testDrawStates() {
  printf("draw in every mode\n");
  // A catalogue of states: each mode at a few points in time, flipped both ways, in a few places,
  // including half off the edge of the frame.
  int states = 0;
  for (int side = -1; side <= 1; side += 2) {
    for (int where = 0; where < 4; ++where) {
      const Vec2 hand = where == 0 ? Vec2{300.f, 330.f} : (where == 1 ? Vec2{40.f, 340.f} : (where == 2 ? Vec2{770.f, 330.f} : Vec2{420.f, 300.f}));
      for (int m = 0; m < 9; ++m) {
        Rig r;
        r.init(side);
        r.hand = hand;
        r.run(0.3f);
        kiteLaunch(r.k, 1.f);
        kiteFly(r.k, 150.f + 30.f * (float)m);
        r.run(0.4f + 0.5f * (float)m);
        if (m == 3) kiteNosedive(r.k);
        if (m == 4) kiteSpinCrash(r.k);
        if (m == 5) kiteDrop(r.k);
        if (m == 6) kitePull(r.k);
        if (m == 7) kiteLose(r.k);
        if (m == 8) kiteReel(r.k);
        for (int step = 0; step < 6; ++step) {
          r.run(0.28f);
          checkDraw(r.k, g_pal[3], "state", states, (uint32_t)side + 3u);
          ++states;
        }
      }
    }
  }
  // Folded and lying, held.
  for (int i = 0; i < 6; ++i) {
    Rig r;
    r.init(i & 1 ? -1 : 1);
    kiteHold(r.k, (float)(i % 3) * 0.5f, (float)(i / 3));
    r.run(1.f);
    checkDraw(r.k, g_pal[3], "held", i, 1u);
    ++states;
  }
  printf("  %d states drawn, bounded, clipped and stripped\n", states);
}

static void testDirtyQuiet() {
  printf("dirty marks\n");
  // A kite whose state has not changed marks nothing; a held kite and a hidden one too.
  Rig r;
  r.init();
  KiteState a = r.k;
  Rect rs[KITE_DIRTY_MAX];
  CHECK(kiteDirty(a, a, rs) == 0 && kiteSame(a, a), "same state, nothing dirty");
  // Sub-pixel changes that do not move a pixel do not mark either.
  KiteState b = a;
  b.pos.x += 0.2f;
  b.pos.y -= 0.2f;
  const bool moved_px = !kiteSame(a, b);
  const int n = kiteDirty(a, b, rs);
  CHECK((n == 0) == !moved_px, "a sub-pixel change marks exactly when a pixel moves");
  // The step on a quiet kite: the line of a hidden kite is nothing.
  KiteState h0;
  kiteInit(h0, kGround, 1, 5u);
  KiteState h1 = h0;
  h1.pos = Vec2{500.f, 20.f};
  CHECK(kiteDirty(h0, h1, rs) == 0, "a hidden kite marks nothing wherever its fields are");
  // Appearing marks its whole bounds; vanishing the same.
  KiteState up;
  kiteInit(up, kGround, 1, 5u);
  up.hand = Vec2{300.f, 330.f};
  kiteHold(up, 0.f, 0.f);
  up.pos = Vec2{300.f, 318.f};
  const int na = kiteDirty(h0, up, rs);
  Rect cover{0, 0, 0, 0};
  for (int i = 0; i < na; ++i) cover = unite(cover, rs[i]);
  CHECK(na > 0 && inside(cover, kiteBounds(up)), "appearing marks everything it covers");
  const int nv = kiteDirty(up, h0, rs);
  cover = Rect{0, 0, 0, 0};
  for (int i = 0; i < nv; ++i) cover = unite(cover, rs[i]);
  CHECK(nv > 0 && inside(cover, kiteBounds(up)), "vanishing marks everything it covered");
}

// ---- long runs, bad input, replay --------------------------------------------------------------------------

static uint32_t g_lcg;
static uint32_t rnd() {
  g_lcg = g_lcg * 1664525u + 1013904223u;
  return g_lcg >> 8;
}
static float rndf() { return (float)(rnd() & 0xFFFF) / 65536.f; }

static uint32_t stressRun(int ticks, bool bad_input, int* worst_rects, float* max_abs) {
  g_lcg = 20260211u;
  KiteState k;
  kiteInit(k, 380.f, 1, 77u);
  Vec2 hand{300.f, 340.f};
  float vx = 0.f;
  uint32_t hash = 2166136261u;
  *worst_rects = 0;
  *max_abs = 0.f;
  for (int t = 0; t < ticks; ++t) {
    if (rnd() % 90u == 0) {
      switch (rnd() % 14u) {
        case 0: kiteHold(k, rndf(), rndf()); break;
        case 1: kiteLaunch(k, rndf()); break;
        case 2: kiteFly(k, 20.f + 380.f * rndf()); break;
        case 3: kiteReel(k); break;
        case 4: kiteNosedive(k); break;
        case 5: kiteSpinCrash(k); break;
        case 6: kiteDrop(k); break;
        case 7: kitePull(k); break;
        case 8: kiteLose(k); break;
        case 9: kiteHide(k); break;
        case 10: kiteInit(k, 300.f + 100.f * rndf(), (rnd() & 1u) ? 1 : -1, rnd()); break;
        case 11: kiteFly(k, 400.f); break;
        default: kiteLaunch(k, 1.f); kiteFly(k, 100.f + 200.f * rndf()); break;
      }
    }
    if (rnd() % 40u == 0) vx = (rndf() - 0.5f) * 160.f;   // he walks, or is dragged, or stands
    hand.x += vx * kDt;
    if (hand.x < -300.f) hand.x = -300.f, vx = 0.f;
    if (hand.x > 1200.f) hand.x = 1200.f, vx = 0.f;
    hand.y = 330.f + 4.f * sinT((float)t * 0.01f);
    float wind = rndf();
    float dt = kDt;
    if (bad_input) {
      const uint32_t r = rnd() % 200u;
      if (r == 0) wind = NAN;
      else if (r == 1) wind = INFINITY;
      else if (r == 2) wind = -3.f;
      else if (r == 3) wind = 40.f;
      else if (r == 4) dt = NAN;
      else if (r == 5) dt = -1.f;
      else if (r == 6) dt = 50.f;
      else if (r == 7) dt = 0.f;
      else if (r == 8) hand.x = NAN;
      else if (r == 9) hand.y = INFINITY;
    }
    const KiteState prev = k;
    kiteStep(k, hand, wind, dt);
    if (bad_input && !finiteF(hand.x)) hand.x = k.hand.x;
    if (bad_input && !finiteF(hand.y)) hand.y = k.hand.y;
    if (!finiteKite(k)) {
      CHECK(false, "tick %d: state not finite (mode %d)", t, k.mode);
      return 0;
    }
    const float ap = fmaxf(fabsf(k.pos.x), fabsf(k.pos.y));
    if (ap > *max_abs) *max_abs = ap;
    Rect rs[KITE_DIRTY_MAX];
    const int n = kiteDirty(prev, k, rs);
    if (n > *worst_rects) *worst_rects = n;
    const Rect b = kiteBounds(k);
    if (!b.empty() && (b.x1 - b.x0 > 3000 || b.y1 - b.y0 > 3000)) {
      CHECK(false, "tick %d: absurd bounds %d,%d..%d,%d (mode %d)", t, b.x0, b.y0, b.x1, b.y1, k.mode);
      return 0;
    }
    if (t % 500 == 0) {
      memcpy(g_a, g_noise, sizeof g_a);
      Surface s = frameSurface(g_a);
      kiteDraw(k, s, kFrame, g_pal[3]);
      const Rect d = diffBox(g_a, g_noise, kFrame);
      if (!(d.empty() || inside(b, d))) {
        CHECK(false, "tick %d: drawn outside the bounds (mode %d)", t, k.mode);
        return 0;
      }
    }
    const unsigned char* bytes = (const unsigned char*)&k.pos;
    for (int i = 0; i < 8; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    hash = (hash ^ k.mode) * 16777619u;
  }
  return hash;
}

static void testLongRuns() {
  printf("long runs\n");
  int worst = 0;
  float mx = 0.f;
  const uint32_t h1 = stressRun(200000, false, &worst, &mx);
  printf("  200000 random ticks: at most %d dirty rects, farthest coordinate %.0f px\n", worst, (double)mx);
  CHECK(h1 != 0, "stress run completed");
  CHECK(mx < 3000.f, "the kite never runs away (%.0f)", (double)mx);
  int worst2 = 0;
  float mx2 = 0.f;
  const uint32_t h2 = stressRun(200000, false, &worst2, &mx2);
  CHECK(h1 == h2 && worst == worst2, "the same run twice gives the same kite (%08x, %08x)", h1, h2);
  int worst3 = 0;
  float mx3 = 0.f;
  const uint32_t h3 = stressRun(100000, true, &worst3, &mx3);
  CHECK(h3 != 0 && mx3 < 3000.f, "NaN, infinity and silly values in: still finite and bounded");
  CHECK(worst <= KITE_DIRTY_MAX, "dirty rects within the cap");

  // The step with dt 0 changes no pixel.
  Rig r;
  r.init();
  kiteLaunch(r.k, 1.f);
  kiteFly(r.k, 200.f);
  r.run(6.f);
  const KiteState before = r.k;
  kiteStep(r.k, r.hand, r.wind, 0.f);
  CHECK(kiteSame(before, r.k), "a zero dt draws the same");

  // A replay from the same commands is the same, byte for byte (no hidden state, no clock).
  Rig a, b;
  a.init();
  b.init();
  for (Rig* q : {&a, &b}) {
    kiteLaunch(q->k, 0.8f);
    kiteFly(q->k, 210.f);
    q->run(7.f);
    kitePull(q->k);
    q->run(3.f);
    kiteLose(q->k);
    q->run(2.f);
  }
  CHECK(memcmp(&a.k, &b.k, sizeof a.k) == 0, "two identical runs end in identical bytes");
  printf("  sizeof(KiteState) = %d bytes\n", (int)sizeof(KiteState));
  CHECK(sizeof(KiteState) <= 96, "KiteState is small (%d bytes)", (int)sizeof(KiteState));
}

// ---- how it reads, every mood --------------------------------------------------------------------------------------

static float lum(Pixel p) { return (0.2126f * (float)red8(p) + 0.7152f * (float)green8(p) + 0.0722f * (float)blue8(p)) / 255.f; }

static void testMoods(const char* dir) {
  printf("moods\n");
  for (int m = 0; m < kMoods; ++m) {
    const Palette& p = g_pal[m];
    const float sky = lum(p[SKY2]), l = lum(p[MUSHROOM]), r = lum(p[TENT_LIT]), ink = lum(p[ACTOR]);
    const float best = fmaxf(fabsf(l - sky), fabsf(r - sky));
    printf("  %-8s sky %.2f  sail %.2f / %.2f  ink %.2f  sail against sky %.2f, ink against sky %.2f\n", kMoodName[m], (double)sky,
           (double)l, (double)r, (double)ink, (double)best, (double)fabsf(ink - sky));
    CHECK(fabsf(ink - sky) > 0.06f || best > 0.08f, "%s: the kite shows against the sky", kMoodName[m]);
    if (m == 0) {   // night rule: nothing bright in the kite, no pure white
      CHECK(l <= NIGHT_LUM_MAX && r <= NIGHT_LUM_MAX, "night: sail within the night cap");
    }
  }

  // Zoom sheet: rows are moods, columns are states, 3x on the kite and tail.
  const int zw = 100, zh = 120, z = 3, ncols = 9;
  Sheet sh;
  sh.init(ncols, kMoods, zw * z, zh * z);
  for (int m = 0; m < kMoods; ++m) {
    for (int c = 0; c < ncols; ++c) {
      Rig r;
      r.init();
      r.hand = Vec2{200.f, 336.f};
      r.wind = 0.3f;
      r.run(0.2f);
      if (c <= 5) {
        kiteLaunch(r.k, 1.f);
        kiteFly(r.k, 200.f);
        r.run(9.f);
      }
      Hiker h;
      memset(&h, 0, sizeof h);
      if (c >= 6) {   // beside him, for scale: held folded, in his lap, and the loose line trailing
        hikerInit(h, Vec2{c == 8 ? 260.f : 200.f, kGround}, c == 8 ? -1 : 1);
        hikerSetPose(h, c == 6 ? POSE_FISH_STAND : (c == 7 ? POSE_SIT : POSE_STAND), 0.f);
        hikerTick(h, 0.f);
        r.hand = hikerHandPos(h, 0);
      }
      if (c == 1) {   // a gust
        r.wind = 0.95f;
        r.run(3.f);
      }
      if (c == 2) {   // pulling
        kitePull(r.k);
        r.run(2.6f);
      }
      if (c == 3) {   // mid nosedive
        kiteNosedive(r.k);
        r.run(0.55f);
      }
      if (c == 4) {   // mid spin
        kiteSpinCrash(r.k);
        r.run(0.7f);
      }
      if (c == 5) {   // crashed, lying on the ground
        kiteNosedive(r.k);
        r.run(5.f);
      }
      if (c == 6) {   // held, folded
        kiteHold(r.k, 1.f, 0.f);
        r.run(1.f);
      }
      if (c == 7) {   // in his lap
        kiteHold(r.k, 0.f, 1.f);
        r.run(1.5f);
      }
      if (c == 8) {   // the kite gone, he walks, the line trails
        kiteLose(r.k);
        hikerWalkTo(h, 100.f, 34.f);
        for (int i = 0; i < 80; ++i) {
          hikerTick(h, kDt);
          r.hand = hikerHandPos(h, 0);
          kiteStep(r.k, r.hand, r.wind, kDt);
        }
      }
      const Vec2 focus = r.k.pos;
      memcpy(g_c, g_land[m], sizeof g_c);
      Surface surf = frameSurface(g_c);
      if (c >= 5) {
        Hiker beside = h;
        if (c == 5) {
          hikerInit(beside, Vec2{200.f, kGround}, 1);
          hikerSetPose(beside, POSE_STAND, 0.f);
          hikerTick(beside, 0.f);
        }
        hikerDraw(beside, surf, g_pal[m], kFrame);
      }
      kiteDraw(r.k, surf, kFrame, g_pal[m]);
      const Rect crop{(int)focus.x - zw / 2, (int)focus.y - 38, (int)focus.x - zw / 2 + zw, (int)focus.y - 38 + zh};
      sh.tileUp(m * ncols + c, g_c, crop, z);
    }
  }
  char path[512];
  snprintf(path, sizeof path, "%s%skite_zoom.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d): rows %s", path, sh.w(), sh.h(), kMoodName[0]);
  for (int m = 1; m < kMoods; ++m) printf(", %s", kMoodName[m]);
  printf("; columns calm, gust, pull, dive, spin, crashed, folded, lap, loose line\n");
  free(sh.rgb);
}

// ---- the weather helper ------------------------------------------------------------------------------------------------

static void testWindFrom() {
  printf("wind from the weather\n");
  CHECK(fabsf(kiteWindFrom(0.f, 0.f) - 0.3f) < 1e-6f, "a calm day is 0.3");
  CHECK(kiteWindFrom(-1.f, 1.f) <= 1.f && kiteWindFrom(-1.f, 1.f) > 0.95f, "a hard gust is 1");
  CHECK(kiteWindFrom(-0.8f, 0.f) > 0.3f && kiteWindFrom(-0.8f, 0.f) < kiteWindFrom(-0.8f, 1.f), "a gust on a windy day is stronger");
  CHECK(kiteWindFrom(5.f, 5.f) == 1.f && kiteWindFrom(0.f, -2.f) == 0.3f, "out of range stays in range");
}

int main(int argc, char** argv) {
  g_scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(g_scratch);
  const char* dir = argc > 1 ? argv[1] : "";
  fillNoise(g_noise);
  for (int i = 0; i < kW * kH; ++i) g_flat[i] = rgb565_hex(0x6E9FB8);
  buildLandscapes();

  printf("kite: sizeof(KiteState) %d bytes, diamond %.0fx%.0f px, tail %.0f px\n", (int)sizeof(KiteState), (double)kKiteW,
         (double)kKiteH, (double)kKiteTail);
  testWindFrom();
  testModes();
  testDrawStates();
  testDirtyQuiet();
  testVariantA(dir);
  testVariantB(dir);
  testVariantC(dir);
  testLongRuns();
  testMoods(dir);

  printf("%d checks, %d failed\n", g_checks, g_fail);
  free(g_scratch);
  return g_fail ? 1 : 0;
}
