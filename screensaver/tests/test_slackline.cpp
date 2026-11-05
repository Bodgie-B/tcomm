// Tests for core/scene/slackline: the line (its sag, its spring, its settling, its drawing and
// dirty marks), the walker (the feet stay on the sagging line within a pixel, the legs reach them,
// the mount, steps, flourish, fall and dismount), the critter crossing, and the two scripted
// variants of the plan (A: three steps and a fall, then further and a fall; B: all the way across
// with a pose in the middle, a bouncing line and a soft step off), each driven by the real hiker
// rig and the real critter rig over the real landscape. Also: bounds cover every pixel drawn, a
// strip draw equals the frame draw and nothing lands outside the clip, slacklineDirty covers
// every changed pixel, no NaN over long random runs and bad input, a replay, and the dirty
// strips a frame costs. Plain executable; returns non-zero on failure.
//
// Writes inspection sheets to the directory in argv[1] (default: the current directory) as PPM:
// slackline_a.ppm, slackline_b.ppm (a frame every 0.5 s, 2x downscaled, 6 across, the time in
// seconds in each corner), slackline_critter.ppm (the same, the squirrel crossing both ways while
// he watches) and slackline_zoom.ppm (the poses at 3x). Convert with a PPM-to-PNG script
// (the scratch folder's ppm2png.py).
#include "anim/ease.h"
#include "anim/rng.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/buildup.h"
#include "scene/critters.h"
#include "scene/hiker.h"
#include "scene/landscape.h"
#include "scene/slackline.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

static int g_fail = 0;
static int g_checks = 0;
static int g_printed = 0;
// Only the first 30 failures are printed; the count is exact.
#define CHECK(cond, ...)                                              \
  do {                                                                \
    ++g_checks;                                                       \
    if (!(cond)) {                                                    \
      ++g_fail;                                                       \
      if (g_printed++ < 30) {                                         \
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

// The set: the camp pine's trunk (x 553..563 below its skirt) is the near anchor, a stake at the edge of
// the clearing the far one, 226 px apart, 26 px above the ground his feet are on.
static const float kGround = 352.f;
static const Vec2 kNearAnchor{556.f, 326.f};
static const Vec2 kFarAnchor{330.f, 326.f};
static const int kDir = -1;               // he walks from the tree toward the stake: toward -x
static const float kMountX = 504.f;       // where he steps up
static const float kWatchX = 380.f;       // where he watches the critter from, on the ground

static Pixel g_noise[SS_WIDTH * SS_HEIGHT];   // a noise background: any stray write shows
static Pixel g_a[SS_WIDTH * SS_HEIGHT];
static Pixel g_b[SS_WIDTH * SS_HEIGHT];
static Pixel g_c[SS_WIDTH * SS_HEIGHT];
static Pixel g_flat[SS_WIDTH * SS_HEIGHT];
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

static bool finiteLine(const Slackline& s) {
  const float f[] = {s.a.x, s.a.y, s.b.x, s.b.y, s.ground_y, s.lx, s.load, s.d, s.v, s.acc_w, s.acc_wx};
  for (float v : f)
    if (!finiteF(v)) return false;
  return true;
}
static bool finiteWalker(const SlackWalker& w) {
  const float f[] = {w.t, w.step_u, w.step_x0, w.blend, w.x, w.fx[0], w.fx[1], w.on[0], w.on[1], w.lift[0], w.lift[1],
                     w.fx0[0], w.fx0[1], w.x0, w.ground_y, w.far_x, w.wob, w.wob_to, w.ph, w.load};
  for (float v : f)
    if (!finiteF(v)) return false;
  return true;
}
static bool finiteCrit(const SlackCritter& c) {
  const float f[] = {c.t, c.x, c.v, c.goal, c.vmax, c.weight, c.sit_s, c.start_x, c.far_x, c.mid_x};
  for (float v : f)
    if (!finiteF(v)) return false;
  return true;
}
static bool finitePose(const HikerPose& p) {
  const float f[] = {p.lean, p.head, p.lift, p.dx, p.thigh[0], p.thigh[1], p.knee[0], p.knee[1],
                     p.shoulder[0], p.shoulder[1], p.elbow[0], p.elbow[1]};
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
static float lum(Pixel p) { return (0.2126f * (float)red8(p) + 0.7152f * (float)green8(p) + 0.0722f * (float)blue8(p)) / 255.f; }

// The static landscape of the loop for each mood, without actors.
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

// The stake at the far end (the scene's prop; the module draws only the wraps and the knot).
static void drawStake(Surface& s, const Palette& pal, const Rect& clip) {
  strokeCapsule(s, Vec2{kFarAnchor.x, kFarAnchor.y - 6.f}, Vec2{kFarAnchor.x, kGround - 3.f}, 5.f, pal[TRUNK], clip);
}

static Slackline makeLine() {
  Slackline l;
  slacklineInit(l, kFarAnchor, kNearAnchor, kGround);
  return l;
}

// ---- PPM sheets ------------------------------------------------------------------------------

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

// ---- drawing one line over a background ------------------------------------------------------------

static void renderRegion(const Slackline& l, const Palette& pal, const Pixel* bg, const Rect& region, Pixel* out) {
  for (int y = region.y0; y < region.y1; ++y) memcpy(out + (size_t)y * kW, bg + (size_t)y * kW, sizeof(Pixel) * kW);
  Surface s = frameSurface(out);
  slacklineDraw(l, s, region, pal);
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

// Bounds, clipping and strips for one line state, over the noise background.
static void checkDraw(const Slackline& l, const Palette& pal, const char* who, int tick, uint32_t salt) {
  const Rect b = slacklineBounds(l);
  memcpy(g_a, g_noise, sizeof g_a);
  Surface full = frameSurface(g_a);
  slacklineDraw(l, full, kFrame, pal);
  const Rect d = diffBox(g_a, g_noise, kFrame);
  CHECK(!d.empty(), "%s %d: nothing drawn", who, tick);
  CHECK(d.empty() || inside(b, d), "%s %d: pixels outside the bounds: drawn %d,%d..%d,%d bounds %d,%d..%d,%d", who, tick,
        d.x0, d.y0, d.x1, d.y1, b.x0, b.y0, b.x1, b.y1);

  memcpy(g_b, g_noise, sizeof g_b);
  for (int sy = 0; sy < SS_STRIP_COUNT; ++sy) {
    Surface strip{g_b + (size_t)sy * SS_STRIP_ROWS * kW, kW, SS_STRIP_ROWS, kW, sy * SS_STRIP_ROWS};
    slacklineDraw(l, strip, Rect{0, sy * SS_STRIP_ROWS, kW, (sy + 1) * SS_STRIP_ROWS}, pal);
  }
  CHECK(memcmp(g_a, g_b, sizeof g_a) == 0, "%s %d: strip draws differ from the frame draw", who, tick);

  uint32_t h = salt * 2654435761u + (uint32_t)tick * 40503u + 1u;
  for (int c = 0; c < 3; ++c) {
    h = h * 1664525u + 1013904223u;
    const int x0 = (int)((h >> 8) % 760u), y0 = (int)((h >> 18) % 440u);
    h = h * 1664525u + 1013904223u;
    Rect clip{x0, y0, x0 + 20 + (int)((h >> 8) % 300u), y0 + 10 + (int)((h >> 20) % 150u)};
    if (c == 1 && !b.empty()) clip = Rect{(b.x0 + b.x1) / 2 - 30, b.y0 - 2, (b.x0 + b.x1) / 2 + 40, (b.y0 + b.y1) / 2 + 2};   // across the line
    if (c == 2 && !b.empty()) clip = Rect{b.x0 - 3, b.y0 - 5, b.x0 + 9, b.y1 + 5};                                          // through the knot
    memcpy(g_c, g_noise, sizeof g_c);
    Surface s = frameSurface(g_c);
    slacklineDraw(l, s, clip, pal);
    const Rect dc = diffBox(g_c, g_noise, kFrame);
    CHECK(dc.empty() || inside(clip.clipped(kFrame), dc), "%s %d: draw outside clip %d,%d..%d,%d: %d,%d..%d,%d", who, tick, clip.x0,
          clip.y0, clip.x1, clip.y1, dc.x0, dc.y0, dc.x1, dc.y1);
  }
}

// prev -> cur: slacklineDirty covers every pixel that changed, stays inside the bounds, and is empty
// exactly when the two states are the same.
static void checkTick(const Slackline& prev, const Slackline& cur, const Palette& pal, const Pixel* bg, const char* who, int tick,
                      int* strips) {
  CHECK(finiteLine(cur), "%s tick %d: line state not finite", who, tick);
  Rect rs[SLACK_DIRTY_MAX];
  const int n = slacklineDirty(prev, cur, rs);
  CHECK(n >= 0 && n <= SLACK_DIRTY_MAX, "%s tick %d: %d dirty rects", who, tick, n);
  const bool same = slacklineSame(prev, cur);
  CHECK(same == (n == 0), "%s tick %d: slacklineSame %d but %d dirty rects", who, tick, same, n);
  const Rect reg0 = unite(slacklineBounds(prev), slacklineBounds(cur));
  uint16_t m = 0;
  for (int i = 0; i < n; ++i) {
    CHECK(inside(reg0, rs[i]) && !rs[i].empty(), "%s tick %d: dirty rect %d outside the bounds", who, tick, i);
    m = (uint16_t)(m | rowsMask(rs[i]));
  }
  if (strips) *strips = popc(m);
  const Rect reg = reg0.clipped(kFrame);
  if (reg.empty()) return;
  const Rect rows{0, reg.y0, kW, reg.y1};
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

// ---- the feet on the line, in pixels ------------------------------------------------------------------

// Draw the hiker alone on a flat colour near his feet; find the lowest ink and compare it with the
// line's upper edge at that column. gap > 0: the sole is below the line's top (into it).
struct FeetProbe {
  bool found;
  float low_gap;    // at the lowest ink pixel of all: its bottom edge minus the line's top there
  float pierce;     // the largest of that over every column (how far any sole sinks into the line)
  int x_low;
};
static FeetProbe probeFeet(const Hiker& h, const Slackline& l, const Palette& pal) {
  FeetProbe r{false, 0.f, -1.0e9f, 0};
  const int x0 = (int)h.feet.x - 36, x1 = x0 + 72;
  const int y0 = (int)h.feet.y - 30, y1 = (int)h.feet.y + 8;
  if (x0 < 0 || x1 > kW || y0 < 0 || y1 > kH) return r;
  const Rect clip{x0, y0, x1, y1};
  for (int y = y0; y < y1; ++y) memcpy(g_c + (size_t)y * kW + x0, g_flat + (size_t)y * kW + x0, sizeof(Pixel) * (size_t)(x1 - x0));
  Surface s = frameSurface(g_c);
  hikerDraw(h, s, pal, clip);
  const float bg = lum(g_flat[0]), ink = lum(pal[ACTOR]);
  // Per column, where the ink ends: the lowest pixel with any coverage, plus its coverage (a
  // horizontal edge at y + c). Sub-pixel, so the check is not at the mercy of a 50% threshold.
  float best_edge = -1.0e9f;
  int best_x = 0;
  for (int x = x0; x < x1; ++x) {
    float edge = -1.0e9f;
    for (int y = y1 - 1; y >= y0; --y) {
      const float c = clamp01((bg - lum(g_c[y * kW + x])) / (bg - ink));
      if (c >= 0.04f) {
        edge = (float)y + c;
        break;
      }
    }
    if (edge < -1.0e8f) continue;
    const float gap = edge - slacklineTopY(l, (float)x + 0.5f);
    if (gap > r.pierce) r.pierce = gap;
    if (edge > best_edge) {
      best_edge = edge;
      best_x = x;
    }
  }
  if (best_edge < -1.0e8f) return r;
  r.found = true;
  r.x_low = best_x;
  r.low_gap = best_edge - slacklineTopY(l, (float)best_x + 0.5f);
  return r;
}

// ---- the rig's kinematics, as the test knows them (hiker.cpp build()), to check the legs ---------------

static Vec2 ankleOf(const HikerPose& P, float H, int i) {   // local frame, hips at (dx*H, 0), y down
  const float L = 0.235f * H;
  const float t = P.thigh[i] * 0.15915494f, s = (P.thigh[i] - P.knee[i]) * 0.15915494f;
  return Vec2{P.dx * H + sinT(t) * L + sinT(s) * L, cosT(t) * L + cosT(s) * L};
}
static float soleOf(const HikerPose& P, float H, int i) {   // how far the foot's lowest point is below the ankle
  const float shin = P.thigh[i] - P.knee[i];
  const float sn = sinf(0.75f * shin);
  float m = 0.f;
  m = fmaxf(m, sn * 0.02f * H);
  m = fmaxf(m, -sn * 0.07f * H);
  return m + 0.029f * H;
}

// ---- the sim: a script, the line, the walker, the critter ---------------------------------------------------

struct Stats {
  long ticks;
  long line_changed;
  long line_strips, all_strips, beyond;   // sums
  int line_max, all_max, beyond_max;
  long on_ticks, on_line_strips, on_all_strips;
  int on_all_max;
};

struct Sim {
  Hiker h;
  Slackline line;
  SlackWalker w;
  Rng rng;
  SlackCritter sc;
  Critter cr;
  bool crit_on;
  int variant;      // 0 A, 1 B, 2 the critter alone (he is on the ground already), 3 the chipmunk (half plan)
  int tick, ph, ph_tick;
  // events
  int attempt, mounts, falls, flourishes, dismounts;
  int steps[4];
  float reach[4];         // how far along the line he got on each attempt, px from where he stepped up
  float flourish_x;
  float d_lo, d_hi;       // the spring's range during the flourish
  int d_flips;            // sign changes of the spring's velocity during the flourish and just after
  float last_v;
  bool in_flourish;
  float fall_x[4];
  float land_y[4];
  float max_sag_walker, max_sag_critter;
  float crit_min_x, crit_max_x;
  int crit_sit_ticks;
  float crit_sit_x;
  bool crit_sat_action;
  float crit_feet_err;
  int hands_on_hips;
  uint8_t last_mode;
  float last_step_u;
  bool crit_started;
  int end_tick;
};

static float nowIn(const Sim& s) { return (float)(s.tick - s.ph_tick) * kDt; }
static void phase(Sim& s, int ph) {
  s.ph = ph;
  s.ph_tick = s.tick;
}

static void simInit(Sim& s, int variant) {
  memset(&s, 0, sizeof s);
  s.variant = variant;
  s.rng.seed(7u + (uint64_t)variant);
  s.line = makeLine();
  slackWalkerInit(s.w, s.rng);
  const float x0 = variant >= 2 ? kWatchX : 456.f;
  hikerInit(s.h, Vec2{x0, kGround}, variant >= 2 ? 1 : -1);
  hikerSetPose(s.h, POSE_STAND, 0.f);
  hikerTick(s.h, 0.f);
  s.crit_min_x = 1.0e9f;
  s.crit_max_x = -1.0e9f;
  s.d_lo = 1.0e9f;
  s.d_hi = -1.0e9f;
  s.last_mode = SW_OFF;
  s.last_step_u = -1.f;
}

static void startCritter(Sim& s, CritterKind kind, SlackCross plan) {
  slackCritterInit(s.sc, s.line, kind, +1, plan, s.rng);   // from the tree end
  const Vec2 f = slackCritterFeet(s.sc, s.line);
  critterInit(s.cr, kind, f, s.sc.dir);
  s.cr.speed_dial = 0.5f;
  s.crit_on = true;
  s.crit_started = true;
}

// Variant A: three steps and a fall, then six steps and a fall.
static void scriptA(Sim& s) {
  const float t = nowIn(s);
  Hiker& h = s.h;
  SlackWalker& w = s.w;
  switch (s.ph) {
    case 0:
      if (t >= 1.0f) {
        hikerWalkTo(h, kMountX, 34.f);
        phase(s, 1);
      }
      break;
    case 1:
      if (hikerArrived(h)) phase(s, 2);
      break;
    case 2:
      if (t >= 0.8f) {
        h.facing = kDir;
        slackWalkerMount(w, h.feet.x, h.feet.y, kDir);
        ++s.attempt;
        ++s.mounts;
        phase(s, 3);
      }
      break;
    case 3:
      if (w.mode == SW_STAND) {
        slackWalkerWobble(w, 0.15f);
        phase(s, 4);
      }
      break;
    case 4:
      if (t >= 1.2f) {
        slackWalkerWalk(w, s.line, s.attempt == 1 ? 3 : 6);
        slackWalkerWobble(w, s.attempt == 1 ? 0.45f : 0.55f);
        phase(s, 5);
      }
      break;
    case 5:
      if (w.mode == SW_STAND) {
        slackWalkerWobble(w, 0.95f);
        phase(s, 6);
      }
      break;
    case 6:
      if (t >= 1.6f) {
        slackWalkerFall(w, s.rng);
        phase(s, 7);
      }
      break;
    case 7:
      if (w.mode == SW_OFF) {
        s.land_y[s.attempt] = h.feet.y;
        s.fall_x[s.attempt] = h.feet.x;
        if (s.attempt >= 2) {
          phase(s, 20);
        } else {
          hikerSetPose(h, POSE_SCRATCH_HEAD, 0.4f);
          phase(s, 8);
        }
      }
      break;
    case 8:
      if (t >= 2.2f) {
        hikerWalkTo(h, kMountX, 34.f);
        phase(s, 9);
      }
      break;
    case 9:
      if (hikerArrived(h)) phase(s, 2);
      break;
    default: break;
  }
}

// Variant B: across, a pose in the middle, a soft step off at the far end.
static void scriptB(Sim& s) {
  const float t = nowIn(s);
  Hiker& h = s.h;
  SlackWalker& w = s.w;
  switch (s.ph) {
    case 0:
      if (t >= 1.0f) {
        hikerWalkTo(h, kMountX, 34.f);
        phase(s, 1);
      }
      break;
    case 1:
      if (hikerArrived(h)) phase(s, 2);
      break;
    case 2:
      if (t >= 0.8f) {
        h.facing = kDir;
        slackWalkerMount(w, h.feet.x, h.feet.y, kDir);
        ++s.attempt;
        ++s.mounts;
        phase(s, 3);
      }
      break;
    case 3:
      if (w.mode == SW_STAND) {
        slackWalkerWobble(w, 0.20f);
        phase(s, 4);
      }
      break;
    case 4:
      if (t >= 1.0f) {
        slackWalkerWalk(w, s.line, 0);
        phase(s, 5);
      }
      break;
    case 5:   // at the middle, the pose (it begins when the step in hand is done)
      if (w.mode == SW_WALK && (w.x - slacklineMidX(s.line)) * (float)kDir >= -4.f) {
        slackWalkerWobble(w, 0.f);
        slackWalkerFlourish(w);
        phase(s, 6);
      }
      break;
    case 6:
      if (w.mode == SW_FLOURISH && !s.in_flourish) {
        s.in_flourish = true;
        ++s.flourishes;
        s.flourish_x = w.x;
      }
      if (s.in_flourish && w.mode == SW_STAND) {
        s.in_flourish = false;
        phase(s, 7);
      }
      break;
    case 7:
      if (t >= 0.8f) {
        slackWalkerWobble(w, 0.2f);
        slackWalkerWalk(w, s.line, 0);
        phase(s, 8);
      }
      break;
    case 8:
      if (w.mode == SW_STAND) {
        slackWalkerWobble(w, 0.f);
        phase(s, 9);
      }
      break;
    case 9:
      if (t >= 0.8f) {
        slackWalkerDismount(w);
        ++s.dismounts;
        phase(s, 10);
      }
      break;
    case 10:
      if (w.mode == SW_OFF) {
        s.land_y[0] = h.feet.y;
        s.fall_x[0] = h.feet.x;
        phase(s, 20);
      }
      break;
    default: break;
  }
}

// The critter crossing: he is on the ground, watching; hands on hips when it sits.
static void scriptCrit(Sim& s) {
  const float t = nowIn(s);
  Hiker& h = s.h;
  switch (s.ph) {
    case 0:
      if (t >= 1.0f) {
        startCritter(s, s.variant == 3 ? CR_CHIPMUNK : CR_SQUIRREL, s.variant == 3 ? SLACK_CROSS_HALF : SLACK_CROSS_BOTH);
        phase(s, 1);
      }
      break;
    case 1:
      if (slackCritterSitting(s.sc)) {
        hikerSetPose(h, POSE_HANDS_ON_HIPS, 0.6f);
        ++s.hands_on_hips;
        phase(s, 2);
      }
      break;
    case 2:
      if (slackCritterDone(s.sc)) {
        hikerSetPose(h, POSE_STAND, 0.5f);
        phase(s, 20);
      }
      break;
    default: break;
  }
}

static void simScript(Sim& s) {
  if (s.variant == 0) scriptA(s);
  else if (s.variant == 1) scriptB(s);
  else scriptCrit(s);
}

// One tick, in the order the header gives.
static void simStep(Sim& s) {
  simScript(s);
  slackWalkerStep(s.w, s.line, kDt);
  if (s.crit_on) slackCritterStep(s.sc, s.line, kDt);
  slacklineStep(s.line, kDt);
  hikerTick(s.h, kDt);
  slackWalkerApply(s.w, s.line, s.h);
  if (s.crit_on) {
    slackCritterDrive(s.sc, s.cr);
    critterTick(s.cr, kDt);
    slackCritterPlace(s.sc, s.line, s.cr);
  }
}

static void drawSim(const Sim& s, const Palette& pal, const Pixel* bg, Pixel* out) {
  memcpy(out, bg, sizeof(Pixel) * (size_t)kW * kH);
  Surface surf = frameSurface(out);
  drawStake(surf, pal, kFrame);
  slacklineDraw(s.line, surf, kFrame, pal);
  if (s.crit_on && !slackCritterDone(s.sc)) critterDraw(s.cr, surf, pal, kFrame);
  hikerDraw(s.h, surf, pal, kFrame);
}

struct Result {
  Sim s;
  Stats st;
  FeetProbe worst;      // over the ticks he stood on the line: the largest |low_gap| and the largest pierce
  float worst_gap;
  float worst_pierce;
  int probes, probe_fail;
  int on_modes[SW_MODES];
  int ticks;
  float sag_walker_at_mid;
};

// The sag of the line at the middle, beyond its resting belly.
static float midSag(const Slackline& l) { return slacklineY(l, slacklineMidX(l)) - 0.5f * (l.a.y + l.b.y); }

static Result* runSim(int variant, Sheet* sheet, bool deep, int tail_ticks) {
  static Result res;
  memset(&res, 0, sizeof res);
  Sim& s = res.s;
  simInit(s, variant);
  const Palette& pal = g_pal[3];
  const Pixel* bg = g_land[3];
  res.worst_gap = 0.f;
  res.worst_pierce = -1.0e9f;
  int next_frame = 0;
  int end_at = -1;
  for (int tick = 0; tick < 25 * 400; ++tick) {
    s.tick = tick;
    const Slackline prev_line = s.line;
    const Hiker prev_h = s.h;
    const Critter prev_c = s.cr;
    const SlackWalker prev_w = s.w;
    simStep(s);

    // events
    if (prev_w.mode == SW_WALK && prev_w.step_u >= 0.f && s.w.step_u < 0.f && s.attempt >= 0 && s.attempt < 4) ++s.steps[s.attempt];
    if (s.w.mode == SW_FALL && s.last_mode != SW_FALL && s.w.hard) ++s.falls;
    s.last_mode = s.w.mode;
    if (s.attempt >= 1 && s.attempt < 4 && s.w.mode != SW_OFF) {
      const float prog = (s.w.x - kMountX) * (float)kDir;
      if (prog > s.reach[s.attempt]) s.reach[s.attempt] = prog;
    }
    res.on_modes[s.w.mode < SW_MODES ? s.w.mode : 0] = 1;
    const float sag = midSag(s.line);
    if (s.w.mode != SW_OFF && slackWalkerOnLine(s.w) && sag > s.max_sag_walker) s.max_sag_walker = sag;
    if (s.crit_on && s.w.mode == SW_OFF && sag > s.max_sag_critter) s.max_sag_critter = sag;
    if (s.in_flourish || (s.flourishes > 0 && s.w.mode == SW_STAND && s.ph == 6)) {
      s.d_lo = fminf(s.d_lo, s.line.d);
      s.d_hi = fmaxf(s.d_hi, s.line.d);
      if ((s.line.v > 0.f) != (s.last_v > 0.f)) ++s.d_flips;
    }
    s.last_v = s.line.v;
    if (s.crit_on && !slackCritterDone(s.sc)) {
      s.crit_min_x = fminf(s.crit_min_x, s.sc.x);
      s.crit_max_x = fmaxf(s.crit_max_x, s.sc.x);
      const float err = fabsf(s.cr.feet.y - slacklineTopY(s.line, s.cr.feet.x));
      if (err > s.crit_feet_err) s.crit_feet_err = err;
      if (slackCritterSitting(s.sc)) {
        ++s.crit_sit_ticks;
        s.crit_sit_x = s.sc.x;
        if (s.cr.action == CA_SIT) s.crit_sat_action = true;
      }
    }

    // dirty strips a frame, as the renderer would count them: the line's rects, the hiker's and the critter's bounds
    {
      Rect rs[SLACK_DIRTY_MAX];
      const int n = slacklineDirty(prev_line, s.line, rs);
      uint16_t ml = 0;
      for (int i = 0; i < n; ++i) ml = (uint16_t)(ml | rowsMask(rs[i]));
      uint16_t mo = (uint16_t)(rowsMask(hikerBounds(prev_h)) | rowsMask(hikerBounds(s.h)));
      if (s.crit_on) mo = (uint16_t)(mo | rowsMask(critterBounds(prev_c)) | rowsMask(critterBounds(s.cr)));
      const int a = popc(ml), b = popc((uint16_t)(ml | mo)), c = popc((uint16_t)(ml & ~mo));
      Stats& st = res.st;
      ++st.ticks;
      if (n) ++st.line_changed;
      st.line_strips += a;
      st.all_strips += b;
      st.beyond += c;
      if (a > st.line_max) st.line_max = a;
      if (b > st.all_max) st.all_max = b;
      if (c > st.beyond_max) st.beyond_max = c;
      if (s.w.mode != SW_OFF) {
        ++st.on_ticks;
        st.on_line_strips += a;
        st.on_all_strips += b;
        if (b > st.on_all_max) st.on_all_max = b;
      }
    }

    // the feet on the line, in pixels
    const bool standing_on = s.w.mode == SW_STAND || s.w.mode == SW_WALK || s.w.mode == SW_FLOURISH;
    if (standing_on && (deep || (tick & 3) == 0)) {
      const FeetProbe p = probeFeet(s.h, s.line, pal);
      ++res.probes;
      if (!p.found) {
        ++res.probe_fail;
      } else {
        if (fabsf(p.low_gap) > fabsf(res.worst_gap)) res.worst_gap = p.low_gap;
        if (p.pierce > res.worst_pierce) res.worst_pierce = p.pierce;
        CHECK(fabsf(p.low_gap) <= 1.0f, "%s tick %d mode %d: the lowest sole is %.2f px from the line's top (x %d)",
              variant == 0 ? "A" : (variant == 1 ? "B" : "crit"), tick, s.w.mode, (double)p.low_gap, p.x_low);
        CHECK(p.pierce <= 1.5f, "%s tick %d mode %d: a sole sinks %.2f px into the line", variant == 0 ? "A" : (variant == 1 ? "B" : "crit"),
              tick, s.w.mode, (double)p.pierce);
      }
    }

    const char* name = variant == 0 ? "A" : (variant == 1 ? "B" : (variant == 2 ? "crit" : "chip"));
    CHECK(finiteLine(s.line) && finiteWalker(s.w) && finiteCrit(s.sc) && finitePose(s.h.pose) && finiteF(s.h.feet.x) && finiteF(s.h.feet.y),
          "%s tick %d: a state is not finite", name, tick);
    if (deep) checkTick(prev_line, s.line, pal, bg, name, tick, nullptr);
    if (deep && (tick % 10) == 0) checkDraw(s.line, pal, name, tick, (uint32_t)variant);

    if (sheet && tick == (25 * next_frame + 1) / 2) {   // every 0.5 s: ticks 0, 13, 25, 38, ...
      if (next_frame < sheet->cols * sheet->rows) {
        drawSim(s, pal, bg, g_c);
        sheet->tileDown2(next_frame, g_c);
        char lab[16];
        snprintf(lab, sizeof lab, "%d.%d", (next_frame / 2), (next_frame & 1) ? 5 : 0);
        label(*sheet, next_frame, lab);
      }
      ++next_frame;
    }

    if (s.ph == 20 && end_at < 0) end_at = tick + tail_ticks;
    if (end_at >= 0 && tick >= end_at) {
      res.ticks = tick;
      s.end_tick = tick;
      return &res;
    }
  }
  res.ticks = 25 * 400;
  s.end_tick = res.ticks;
  return &res;
}

static void printStrips(const char* what, const Stats& st) {
  if (st.ticks == 0) return;
  const double n = (double)st.ticks;
  printf("  %s: %ld ticks, the line changed on %.0f%% of them; strips per frame:\n", what, st.ticks, 100.0 * (double)st.line_changed / n);
  printf("      line alone %.2f avg (max %d)   line beyond the hiker's and critter's %.2f avg (max %d)   line + hiker + critter %.2f avg (max %d)\n",
         (double)st.line_strips / n, st.line_max, (double)st.beyond / n, st.beyond_max, (double)st.all_strips / n, st.all_max);
  if (st.on_ticks)
    printf("      while he is on the line (%ld ticks): line alone %.2f avg, everything %.2f avg (max %d)\n", st.on_ticks,
           (double)st.on_line_strips / (double)st.on_ticks, (double)st.on_all_strips / (double)st.on_ticks, st.on_all_max);
}

static int framesFor(const Result& r) { return (r.ticks * 2) / 25 + 2; }

static void writeSheet(const char* dir, const char* name, Sheet& sh) {
  char path[512];
  snprintf(path, sizeof path, "%s%s%s", dir, dir[0] ? "/" : "", name);
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d)\n", path, sh.w(), sh.h());
  free(sh.rgb);
}

// ---- the tests ------------------------------------------------------------------------------------

static void testLinePhysics() {
  printf("the line: sag, spring, settling\n");
  Slackline l = makeLine();
  const float mid = slacklineMidX(l);
  CHECK(l.a.x < l.b.x && fabsf(l.a.x - kFarAnchor.x) < 0.2f && fabsf(l.b.x - kNearAnchor.x) < 0.2f, "anchors in order");
  CHECK(slacklineSettled(l), "a new line is settled");
  CHECK(fabsf(slacklineY(l, l.a.x) - l.a.y) < 0.01f && fabsf(slacklineY(l, l.b.x) - l.b.y) < 0.01f, "the line passes through its anchors");
  const float rest_mid = slacklineY(l, mid) - 326.f;
  printf("  resting sag at mid-span %.2f px\n", (double)rest_mid);
  CHECK(rest_mid > 1.5f && rest_mid < 4.f, "a resting sag of a couple of px (%.2f)", (double)rest_mid);
  CHECK(fabsf(slacklineTopY(l, mid) - (slacklineY(l, mid) - 1.f)) < 0.001f, "the top edge is a px above the centre");

  // Swapped anchors and a too-short line.
  Slackline sw;
  slacklineInit(sw, kNearAnchor, kFarAnchor, kGround);
  CHECK(sw.a.x < sw.b.x, "anchors given right to left are swapped");
  slacklineInit(sw, Vec2{100.f, 300.f}, Vec2{104.f, 300.f}, kGround);
  CHECK(sw.b.x - sw.a.x >= 16.f, "a short line is stretched");

  // Under the walker, at mid-span: deep. Settles to the steady sag.
  for (int i = 0; i < 100; ++i) {
    slacklineLoad(l, mid, 1.f);
    slacklineStep(l, kDt);
  }
  const float deep_mid = midSag(l) - 0.f;
  printf("  walker at mid-span: line sags %.2f px (resting %.2f)\n", (double)(slacklineY(l, mid) - 326.f), (double)rest_mid);
  CHECK(slacklineY(l, mid) - 326.f > 14.f && slacklineY(l, mid) - 326.f < 21.f, "the walker sags the line 15 to 20 px at mid-span");
  (void)deep_mid;
  CHECK(fabsf(l.lx - mid) < 0.01f, "the low point is at the load");
  // The low point follows the load: deeper at mid than near the anchor.
  Slackline near = makeLine();
  const float x_near = near.a.x + 0.15f * (near.b.x - near.a.x);
  for (int i = 0; i < 100; ++i) {
    slacklineLoad(near, x_near, 1.f);
    slacklineStep(near, kDt);
  }
  const float near_depth = slacklineY(near, x_near) - (326.f + 2.5f * 4.f * 0.15f * 0.85f);
  printf("  walker 15%% along: extra sag %.2f px\n", (double)near_depth);
  CHECK(near_depth < 0.6f * (slacklineY(l, mid) - 326.f - rest_mid), "near an anchor the same weight sags less");
  // The V is smooth: no kink between neighbouring pixels, falling away on both sides of the low point.
  float worst_kink = 0.f;
  for (float x = l.a.x + 2.f; x < l.b.x - 2.f; x += 1.f) {
    const float k = slacklineY(l, x - 1.f) - 2.f * slacklineY(l, x) + slacklineY(l, x + 1.f);
    if (fabsf(k) > worst_kink) worst_kink = fabsf(k);
    CHECK(slacklineY(l, x) <= slacklineY(l, mid) + 0.01f, "the low point is the lowest (x %.0f)", (double)x);
  }
  printf("  largest bend between neighbouring pixels %.3f px\n", (double)worst_kink);
  CHECK(worst_kink < 0.2f, "the loaded line has no kink (%.3f)", (double)worst_kink);

  // A squirrel's weight: slight.
  Slackline sq = makeLine();
  for (int i = 0; i < 100; ++i) {
    slacklineLoad(sq, mid, 0.1f);
    slacklineStep(sq, kDt);
  }
  const float sq_sag = slacklineY(sq, mid) - 326.f - rest_mid;
  printf("  squirrel at mid-span: %.2f px more than resting\n", (double)sq_sag);
  CHECK(sq_sag > 0.5f && sq_sag < 3.5f, "a squirrel dips the line a little (%.2f)", (double)sq_sag);

  // The load steps off: the line bounces a few times, then settles exactly.
  int crossings = 0;
  float peak_up = 0.f;
  float prev = slacklineY(l, mid) - 326.f - rest_mid;
  float settle_t = -1.f;
  bool was_settled = false;
  Slackline before = l;
  int quiet_run = 0;
  for (int i = 0; i < 25 * 12; ++i) {
    slacklineStep(l, kDt);
    const float y = slacklineY(l, mid) - 326.f - rest_mid;   // vs resting; + is down
    if ((prev > 0.f) != (y > 0.f) && fabsf(y) > 1.0e-4f) ++crossings;
    if (-y > peak_up) peak_up = -y;
    prev = y;
    if (slacklineSettled(l) && !was_settled) {
      was_settled = true;
      settle_t = (float)i * kDt;
    }
    if (slacklineSame(before, l)) ++quiet_run;
    else quiet_run = 0;
    before = l;
  }
  printf("  after the load leaves: crossed rest %d times, rose %.1f px above it, settled after %.1f s\n", crossings, (double)peak_up,
         (double)settle_t);
  CHECK(crossings >= 5, "it bounces a few times (rest crossed %d times)", crossings);
  CHECK(peak_up > 3.f && peak_up < 12.f, "the first bounce rises a few px above rest (%.1f)", (double)peak_up);
  CHECK(was_settled && settle_t > 1.5f && settle_t < 7.f, "it settles in a few seconds (%.1f)", (double)settle_t);
  CHECK(slacklineSettled(l) && l.d == 0.f && l.v == 0.f, "and is exactly at rest");
  CHECK(quiet_run > 25 * 3, "and then draws the same pixels tick after tick (%d)", quiet_run);
  CHECK(fabsf(slacklineY(l, mid) - 326.f - rest_mid) < 0.001f, "back at the resting sag");

  // A kick makes it bounce on its own; a second load adds in at the weighted mean x.
  Slackline k = makeLine();
  slacklineKick(k, 80.f);
  float maxd = 0.f;
  for (int i = 0; i < 100; ++i) {
    slacklineStep(k, kDt);
    maxd = fmaxf(maxd, fabsf(k.d));
  }
  CHECK(maxd > 2.f, "a kick moves it (%.1f)", (double)maxd);
  Slackline two = makeLine();
  slacklineLoad(two, 400.f, 1.f);
  slacklineLoad(two, 500.f, 1.f);
  slacklineStep(two, kDt);
  CHECK(fabsf(two.lx - 450.f) < 0.01f, "two loads sit at their mean (%.1f)", (double)two.lx);

  // Bad input and long steps never break it.
  Slackline b = makeLine();
  slacklineLoad(b, NAN, 1.f);
  slacklineLoad(b, 400.f, NAN);
  slacklineLoad(b, INFINITY, 1.f);
  slacklineLoad(b, 400.f, -3.f);
  slacklineKick(b, NAN);
  slacklineStep(b, NAN);
  slacklineStep(b, -1.f);
  slacklineStep(b, 1000.f);
  slacklineLoad(b, -9000.f, 1.f);
  slacklineStep(b, 0.1f);
  CHECK(finiteLine(b) && b.lx >= b.a.x && b.lx <= b.b.x, "bad input leaves a finite line");
  // dt does not matter much: a long step lands near the same sag.
  Slackline fa = makeLine(), fb = makeLine();
  for (int i = 0; i < 150; ++i) {
    slacklineLoad(fa, mid, 1.f);
    slacklineStep(fa, 0.04f);
  }
  for (int i = 0; i < 75; ++i) {
    slacklineLoad(fb, mid, 1.f);
    slacklineStep(fb, 0.08f);
  }
  CHECK(fabsf(slacklineY(fa, mid) - slacklineY(fb, mid)) < 0.6f, "a doubled dt reaches the same sag");
}

static void testLineDraw() {
  printf("the line: bounds, clip, strips, dirty rects\n");
  const Palette& pal = g_pal[3];
  const Pixel* bg = g_land[3];
  Slackline rest = makeLine();
  checkDraw(rest, pal, "rest", 0, 1u);
  Slackline l = makeLine();
  const float xs[] = {360.f, 400.f, 443.f, 500.f, 540.f};
  int n = 0;
  for (float x : xs) {
    Slackline m = makeLine();
    for (int i = 0; i < 80; ++i) {
      slacklineLoad(m, x, 1.f);
      slacklineStep(m, kDt);
    }
    checkDraw(m, pal, "loaded", (int)x, 2u);
    ++n;
  }
  // A release, tick by tick: every pair of states.
  for (int i = 0; i < 80; ++i) {
    slacklineLoad(l, 443.f, 1.f);
    slacklineStep(l, kDt);
  }
  Slackline prev = l;
  long strips_sum = 0;
  int strips_max = 0, ticks = 0;
  for (int i = 0; i < 25 * 8; ++i) {
    slacklineStep(l, kDt);
    int strips = 0;
    checkTick(prev, l, pal, bg, "release", i, &strips);
    strips_sum += strips;
    if (strips > strips_max) strips_max = strips;
    ++ticks;
    if (i % 6 == 0) checkDraw(l, pal, "release", i, 3u);
    prev = l;
  }
  printf("  a bouncing line alone dirties %.2f strips a tick on average (max %d)\n", (double)strips_sum / (double)ticks, strips_max);
  CHECK(strips_max <= 2, "a bouncing line touches at most 2 strips (%d)", strips_max);
  // A moving load (the critter trotting) and sloped anchors.
  Slackline sl;
  slacklineInit(sl, Vec2{300.f, 310.f}, Vec2{540.f, 340.f}, kGround);
  checkDraw(sl, pal, "sloped", 0, 4u);
  Slackline p2 = sl;
  for (int i = 0; i < 25 * 4; ++i) {
    slacklineLoad(sl, 320.f + 2.f * (float)i, 0.1f);
    slacklineStep(sl, kDt);
    checkTick(p2, sl, pal, bg, "sloped", i, nullptr);
    p2 = sl;
  }
  // Same state, nothing dirty; a sub-quarter-pixel change marks only when a node moves a quarter pixel.
  Rect rs[SLACK_DIRTY_MAX];
  CHECK(slacklineDirty(rest, rest, rs) == 0 && slacklineSame(rest, rest), "same state, nothing dirty");
  Slackline tiny = rest;
  tiny.d = 0.001f;   // a load sag too small to move any node a quarter pixel
  CHECK(slacklineDirty(rest, tiny, rs) == 0, "a tiny change marks nothing");
  // Moving an anchor marks the whole line and the knots.
  Slackline moved = rest;
  slacklineInit(moved, Vec2{kFarAnchor.x, kFarAnchor.y + 8.f}, kNearAnchor, kGround);
  const int nm = slacklineDirty(rest, moved, rs);
  Rect cover{0, 0, 0, 0};
  for (int i = 0; i < nm; ++i) cover = unite(cover, rs[i]);
  CHECK(nm >= 1 && inside(cover, slacklineBounds(rest)) && inside(cover, slacklineBounds(moved)), "a moved anchor marks everything");
  // Draw at night: the colours come from the palette, nothing else.
  (void)n;
}

static void testMoods() {
  printf("the line against the ground in every mood\n");
  for (int m = 0; m < kMoods; ++m) {
    const Palette& p = g_pal[m];
    const float line = lum(p[TENT_LIT]), ground = lum(p[GROUND]), grass = lum(p[GRASS]), knot = lum(p[MUSHROOM]);
    printf("  %-8s line %.2f  ground %.2f  grass %.2f  knot %.2f  line against ground %.2f\n", kMoodName[m], (double)line, (double)ground,
           (double)grass, (double)knot, (double)fabsf(line - ground));
    CHECK(fabsf(line - ground) > 0.15f && fabsf(line - grass) > 0.12f, "%s: the line shows against the ground", kMoodName[m]);
    if (m == 0) CHECK(line <= NIGHT_LUM_MAX && knot <= NIGHT_LUM_MAX, "night: line and knot within the night cap");
  }
}

// ---- the walker's legs and feet ------------------------------------------------------------------------

static void testWalkerGeometry() {
  printf("the walker: the legs reach the feet, the feet stand on the sagging line\n");
  Rng rng;
  rng.seed(3);
  const float H = 90.f;
  int checked = 0;
  float worst_x = 0.f, worst_y = 0.f, worst_low = 0.f;
  for (int dir = -1; dir <= 1; dir += 2) {
    Slackline l = dir < 0 ? makeLine() : makeLine();
    SlackWalker w;
    slackWalkerInit(w, rng);
    Hiker h;
    const float x0 = dir < 0 ? kMountX : 380.f;
    hikerInit(h, Vec2{x0, kGround}, dir);
    hikerSetPose(h, POSE_STAND, 0.f);
    slackWalkerMount(w, x0, kGround, dir);
    int tick = 0;
    int stage = 0;
    float stage_t = 0.f;
    for (; tick < 25 * 40; ++tick) {
      stage_t += kDt;
      if (stage == 0 && w.mode == SW_STAND) { slackWalkerWobble(w, 0.4f); stage = 1; stage_t = 0.f; }
      if (stage == 1 && stage_t > 1.f) { slackWalkerWalk(w, l, 4); stage = 2; }
      if (stage == 2 && w.mode == SW_STAND) { slackWalkerFlourish(w); stage = 3; stage_t = 0.f; }
      if (stage == 3 && w.mode == SW_STAND) { slackWalkerWobble(w, 0.9f); stage = 4; stage_t = 0.f; }
      if (stage == 4 && stage_t > 1.2f) { slackWalkerFall(w, rng); stage = 5; }
      if (stage == 5 && w.mode == SW_OFF) break;
      slackWalkerStep(w, l, kDt);
      slacklineStep(l, kDt);
      hikerTick(h, kDt);
      slackWalkerApply(w, l, h);
      if (w.mode == SW_OFF) continue;
      const HikerPose& P = h.pose;
      CHECK(finitePose(P), "tick %d: pose finite", tick);
      // Only with the walker's pose at full weight (blend 1) are the legs exactly his.
      if (w.blend < 0.999f) continue;
      float s[2], low = -1.0e9f;
      for (int i = 0; i < 2; ++i) {
        s[i] = w.ground_y + (slacklineTopY(l, w.fx[i]) - w.ground_y) * w.on[i] - w.lift[i];
        low = fmaxf(low, s[i]);
      }
      CHECK(fabsf(h.feet.y - low) < 0.01f && fabsf(h.feet.x - w.x) < 0.01f, "tick %d: the rig's feet are the lowest foot's surface", tick);
      // The rig grounds the lowest contact on feet.y: find it from the pose and compare each foot with its target.
      float contact_low = -1.0e9f;
      Vec2 an[2];
      for (int i = 0; i < 2; ++i) {
        an[i] = ankleOf(P, H, i);
        contact_low = fmaxf(contact_low, an[i].y + soleOf(P, H, i));
      }
      for (int i = 0; i < 2; ++i) {
        const float want_x = (float)dir * (w.fx[i] - w.x);
        const float got_x = an[i].x;   // from the feet.x, along the facing
        const float ex = fabsf(got_x - want_x);
        const float sole_world = h.feet.y + (an[i].y + soleOf(P, H, i)) - contact_low;
        const float ey = fabsf(sole_world - s[i]);
        if (ex > worst_x) worst_x = ex;
        if (ey > worst_y) worst_y = ey;
        // (an unreachable target straightens the leg; the walker never asks for one except in the deepest steps)
        CHECK(ex < 1.5f, "tick %d dir %d foot %d: ankle %.2f px off its x", tick, dir, i, (double)ex);
        CHECK(ey < 0.4f, "tick %d dir %d foot %d: sole %.2f px off its surface (mode %d)", tick, dir, i, (double)ey, w.mode);
      }
      if (fabsf(contact_low - (contact_low)) > worst_low) worst_low = 0.f;
      ++checked;
    }
    CHECK(w.mode == SW_OFF, "dir %d: the sequence ends with him off the line", dir);
    CHECK(fabsf(h.feet.y - kGround) < 0.5f, "dir %d: he is on the ground (%.2f)", dir, (double)h.feet.y);
  }
  printf("  %d poses checked; worst ankle x error %.2f px, worst sole y error %.2f px\n", checked, (double)worst_x, (double)worst_y);

  // The mirror: the same story facing +x (right to left above); feet probe in pixels while standing both ways.
  for (int dir = -1; dir <= 1; dir += 2) {
    Slackline l = makeLine();
    SlackWalker w;
    slackWalkerInit(w, rng);
    Hiker h;
    const float x0 = dir < 0 ? kMountX : 380.f;
    hikerInit(h, Vec2{x0, kGround}, dir);
    slackWalkerMount(w, x0, kGround, dir);
    float worst = 0.f, pierce = -1.0e9f;
    int probes = 0;
    int walked = 0;
    for (int t = 0; t < 25 * 30; ++t) {
      if (w.mode == SW_STAND && walked == 0) { slackWalkerWobble(w, 0.3f); slackWalkerWalk(w, l, 5); walked = 1; }
      if (w.mode == SW_STAND && walked == 1 && w.t > 1.f) { slackWalkerFlourish(w); walked = 2; }
      if (w.mode == SW_STAND && walked == 2 && w.t > 1.f) { slackWalkerDismount(w); walked = 3; }
      slackWalkerStep(w, l, kDt);
      slacklineStep(l, kDt);
      hikerTick(h, kDt);
      slackWalkerApply(w, l, h);
      if (w.mode == SW_STAND || w.mode == SW_WALK || w.mode == SW_FLOURISH) {
        const FeetProbe p = probeFeet(h, l, g_pal[3]);
        if (p.found) {
          ++probes;
          if (fabsf(p.low_gap) > fabsf(worst)) worst = p.low_gap;
          if (p.pierce > pierce) pierce = p.pierce;
        }
      }
      if (walked == 3 && w.mode == SW_OFF) break;
    }
    printf("  facing %+d: %d probes, the lowest sole is within %.2f px of the line's top, the deepest sink %.2f px\n", dir, probes,
           (double)fabsf(worst), (double)pierce);
    CHECK(probes > 100 && fabsf(worst) <= 1.f && pierce <= 1.5f, "facing %+d: the feet are on the line", dir);
    CHECK(w.mode == SW_OFF && fabsf(h.feet.y - kGround) < 0.5f, "facing %+d: the dismount ends on the ground", dir);
  }
}

// ---- the variants -------------------------------------------------------------------------------------

static void testVariantA(const char* dir) {
  printf("variant A: three steps and a fall, then further\n");
  Result* r0 = runSim(0, nullptr, false, 25 * 8);
  const int frames = framesFor(*r0);
  Sheet sh;
  sh.init(6, (frames + 5) / 6, 400, 240);
  Result* r = runSim(0, &sh, true, 25 * 8);
  writeSheet(dir, "slackline_a.ppm", sh);
  const Sim& s = r->s;
  printf("  %.1f s; mounts %d, falls %d, steps %d then %d, got %.0f then %.0f px along; probes %d (worst sole %.2f px from the line, deepest sink %.2f px)\n",
         (double)r->ticks * (double)kDt, s.mounts, s.falls, s.steps[1], s.steps[2], (double)s.reach[1], (double)s.reach[2], r->probes,
         (double)fabsf(r->worst_gap), (double)r->worst_pierce);
  CHECK(s.mounts == 2 && s.falls == 2, "A: he steps on twice and falls twice (%d, %d)", s.mounts, s.falls);
  CHECK(s.steps[1] == 3, "A: the first try gets three steps (%d)", s.steps[1]);
  CHECK(s.steps[2] == 6, "A: the second gets further (%d steps)", s.steps[2]);
  CHECK(s.reach[2] > s.reach[1] + 15.f, "A: and is further along the line (%.0f vs %.0f px)", (double)s.reach[2], (double)s.reach[1]);
  CHECK(s.w.mode == SW_OFF && s.ph == 20, "A ends with him off the line");
  CHECK(fabsf(s.h.feet.y - kGround) < 0.01f, "A: he is on the ground at the end (%.2f)", (double)s.h.feet.y);
  CHECK(fabsf(s.land_y[1] - kGround) < 0.01f && fabsf(s.land_y[2] - kGround) < 0.01f, "A: each fall lands him on the ground");
  CHECK(s.h.facing == -1 || s.h.facing == 1, "A: facing is a direction");
  CHECK(slacklineSettled(s.line), "A: the line has settled by the end (d %.3f)", (double)s.line.d);
  CHECK(r->on_modes[SW_MOUNT] && r->on_modes[SW_STAND] && r->on_modes[SW_WALK] && r->on_modes[SW_FALL], "A uses mount, stand, walk and fall");
  CHECK(!r->on_modes[SW_FLOURISH], "A has no flourish");
  CHECK(r->probes > 100 && r->probe_fail == 0, "A: his feet were probed in pixels (%d, %d failed)", r->probes, r->probe_fail);
  CHECK(s.max_sag_walker > 8.f, "A: the line sags under him (%.1f px)", (double)s.max_sag_walker);
  printStrips("A", r->st);

  // Replay: the same run again is the same, tick for tick (compare the end states).
  Sim keep = r->s;
  Result* r2 = runSim(0, nullptr, false, 25 * 8);
  CHECK(r2->ticks == r->ticks && memcmp(&keep.h.pose, &r2->s.h.pose, sizeof keep.h.pose) == 0 && keep.w.x == r2->s.w.x && keep.line.d == r2->s.line.d,
        "A: a replay gives the same run");
}

static void testVariantB(const char* dir) {
  printf("variant B: all the way across, a pose in the middle, the line bounces\n");
  Result* r0 = runSim(1, nullptr, false, 25 * 8);
  const int frames = framesFor(*r0);
  Sheet sh;
  sh.init(6, (frames + 5) / 6, 400, 240);
  Result* r = runSim(1, &sh, true, 25 * 8);
  writeSheet(dir, "slackline_b.ppm", sh);
  const Sim& s = r->s;
  const float mid = slacklineMidX(s.line);
  printf("  %.1f s; flourish at x=%.0f (middle %.0f), spring range %.1f..%.1f px with %d reversals, steps %d, ended x=%.0f on y=%.1f; probes %d (worst sole %.2f px)\n",
         (double)r->ticks * (double)kDt, (double)s.flourish_x, (double)mid, (double)s.d_lo, (double)s.d_hi, s.d_flips, s.steps[1],
         (double)s.fall_x[0], (double)s.land_y[0], r->probes, (double)fabsf(r->worst_gap));
  CHECK(s.mounts == 1 && s.flourishes == 1 && s.dismounts == 1 && s.falls == 0, "B: one mount, one flourish, one dismount, no fall");
  CHECK(fabsf(s.flourish_x - mid) < 14.f, "B: the pose is in the middle (x %.0f, middle %.0f)", (double)s.flourish_x, (double)mid);
  CHECK(s.d_hi - s.d_lo > 8.f && s.d_flips >= 4, "B: the line bounces (range %.1f px, %d reversals)", (double)(s.d_hi - s.d_lo), s.d_flips);
  CHECK(s.reach[1] > 150.f, "B: he gets all the way across (%.0f px)", (double)s.reach[1]);
  CHECK(s.fall_x[0] < s.line.a.x + 60.f && s.fall_x[0] > s.line.a.x - 10.f, "B: he steps off at the far end (x %.0f; stake at %.0f)", (double)s.fall_x[0],
        (double)s.line.a.x);
  CHECK(s.w.mode == SW_OFF && s.ph == 20, "B ends with him off the line");
  CHECK(fabsf(s.h.feet.y - kGround) < 0.01f, "B: he is on the ground (%.2f)", (double)s.h.feet.y);
  CHECK(slacklineSettled(s.line), "B: the line has settled (d %.3f)", (double)s.line.d);
  CHECK(r->on_modes[SW_MOUNT] && r->on_modes[SW_WALK] && r->on_modes[SW_FLOURISH] && r->on_modes[SW_FALL], "B uses mount, walk, flourish and the step off");
  CHECK(r->probes > 100 && r->probe_fail == 0, "B: his feet were probed (%d, %d failed)", r->probes, r->probe_fail);
  printStrips("B", r->st);
}

static void testCritter(const char* dir) {
  printf("the critter crossing: squirrel both ways with a sit in the middle, chipmunk to the middle and back\n");
  Result* r0 = runSim(2, nullptr, false, 25 * 3);
  const int frames = framesFor(*r0);
  Sheet sh;
  sh.init(6, (frames + 5) / 6, 400, 240);
  Result* r = runSim(2, &sh, true, 25 * 3);
  writeSheet(dir, "slackline_critter.ppm", sh);
  const Sim& s = r->s;
  const float mid = slacklineMidX(s.line);
  printf("  squirrel: %.1f s; x from %.0f to %.0f, sat %.1f s at x=%.1f; line sag with it %.2f px beyond resting; feet error %.4f px\n",
         (double)r->ticks * (double)kDt, (double)s.crit_min_x, (double)s.crit_max_x, (double)s.crit_sit_ticks * (double)kDt,
         (double)s.crit_sit_x, (double)(s.max_sag_critter - (slacklineY(makeLine(), mid) - 326.f)), (double)s.crit_feet_err);
  CHECK(slackCritterDone(s.sc), "the squirrel finishes");
  CHECK(s.crit_max_x > s.line.b.x - 14.f || s.crit_min_x < s.line.a.x + 14.f, "it covers the whole line");
  CHECK(s.crit_min_x < s.line.a.x + 12.f && s.crit_max_x > s.line.b.x - 12.f, "it goes from one end to the other (%.0f..%.0f)", (double)s.crit_min_x, (double)s.crit_max_x);
  CHECK(fabsf(s.crit_sit_x - mid) < 0.5f && s.crit_sit_ticks >= 25 * 4, "it sits in the middle for 4 s or more (%.1f s at %.1f)", (double)s.crit_sit_ticks * (double)kDt,
        (double)s.crit_sit_x);
  CHECK(s.crit_sat_action, "the rig was told to sit");
  CHECK(s.crit_feet_err < 0.01f, "its feet are on the line's top edge every tick (%.4f)", (double)s.crit_feet_err);
  CHECK(s.hands_on_hips == 1, "he watched with his hands on his hips once");
  CHECK(fabsf(s.sc.x - s.sc.start_x) < 0.01f, "it finishes where it began");
  CHECK(s.max_sag_critter - (slacklineY(makeLine(), mid) - 326.f) < 4.f && s.max_sag_critter > (slacklineY(makeLine(), mid) - 326.f) + 0.4f,
        "the line sags slightly under it");
  CHECK(r->probe_fail == 0, "no probes taken");
  printStrips("squirrel crossing", r->st);

  Result* c = runSim(3, nullptr, true, 25 * 3);
  const Sim& cs = c->s;
  printf("  chipmunk (half plan): %.1f s; x %.0f..%.0f, sat %.1f s at x=%.1f\n", (double)c->ticks * (double)kDt, (double)cs.crit_min_x, (double)cs.crit_max_x,
         (double)cs.crit_sit_ticks * (double)kDt, (double)cs.crit_sit_x);
  CHECK(slackCritterDone(cs.sc) && fabsf(cs.crit_sit_x - mid) < 0.5f && cs.crit_sit_ticks >= 25 * 4, "the chipmunk sits in the middle and goes back");
  CHECK(cs.crit_max_x < cs.sc.start_x + 0.1f && cs.crit_min_x < mid + 1.f && cs.crit_min_x > mid - 1.f, "the half plan goes only to the middle (x %.0f..%.0f)",
        (double)cs.crit_min_x, (double)cs.crit_max_x);
  CHECK(cs.crit_feet_err < 0.01f, "chipmunk feet on the line (%.4f)", (double)cs.crit_feet_err);
  printStrips("chipmunk crossing", c->st);

  // The ease: speed ramps up and down, never jumps, never passes vmax.
  Slackline l = makeLine();
  Rng rng;
  rng.seed(9);
  SlackCritter sc;
  slackCritterInit(sc, l, CR_SQUIRREL, -1, SLACK_CROSS_BOTH, rng);
  float prev_v = 0.f, max_dv = 0.f, vmax = 0.f;
  int turns = 0;
  float last_sign = 0.f;
  for (int i = 0; i < 25 * 60 && !slackCritterDone(sc); ++i) {
    const uint8_t was = sc.state;
    slackCritterStep(sc, l, kDt);
    slacklineStep(l, kDt);
    if (was == SC_TROT && sc.state == SC_TROT) max_dv = fmaxf(max_dv, fabsf(sc.v - prev_v));   // (arriving snaps it to a stop)
    prev_v = sc.v;
    vmax = fmaxf(vmax, fabsf(sc.v));
    if (sc.v != 0.f) {
      const float sg = sc.v > 0.f ? 1.f : -1.f;
      if (last_sign != 0.f && sg != last_sign) ++turns;
      last_sign = sg;
    }
  }
  printf("  squirrel from the left end: top speed %.1f px/s, largest speed change in a tick %.2f px/s, %d reversals\n", (double)vmax, (double)max_dv, turns);
  CHECK(slackCritterDone(sc) && fabsf(sc.x - sc.start_x) < 0.01f, "from the left end it also ends where it began");
  CHECK(max_dv <= 130.f * kDt + 0.01f, "the speed changes by at most the acceleration (%.2f)", (double)max_dv);
  CHECK(vmax <= sc.vmax + 0.01f && vmax > 40.f, "it trots at about 50 to 65 px/s (%.1f)", (double)vmax);
  CHECK(turns == 1, "the squirrel reverses once, at the far end (%d)", turns);
}

// ---- long runs, bad input, replay ---------------------------------------------------------------------

static uint32_t g_lcg;
static uint32_t rnd() {
  g_lcg = g_lcg * 1664525u + 1013904223u;
  return g_lcg >> 8;
}
static float rndf() { return (float)(rnd() & 0xFFFF) / 65536.f; }

// Random commands in any state, odd dts and bad values: nothing may go NaN, the feet stay on the
// line or the ground, the line stays bounded. Returns a hash of the final states for the replay.
static uint32_t stressRun(uint32_t seed, int ticks, bool bad, int* mounts_seen, float* worst_above, float* max_d) {
  g_lcg = seed;
  Rng rng;
  rng.seed(seed);
  Slackline l = makeLine();
  SlackWalker w;
  slackWalkerInit(w, rng);
  Hiker h;
  hikerInit(h, Vec2{kMountX, kGround}, -1);
  SlackCritter sc;
  memset(&sc, 0, sizeof sc);
  Critter cr;
  bool crit_on = false;
  uint32_t hash = 2166136261u;
  *worst_above = 0.f;
  *max_d = 0.f;
  *mounts_seen = 0;
  for (int t = 0; t < ticks; ++t) {
    float dt = kDt;
    if (rnd() % 7 == 0) dt = 0.005f + 0.1f * rndf();
    if (bad) {
      const uint32_t r = rnd() % 400;
      if (r == 0) dt = NAN;
      else if (r == 1) dt = -0.5f;
      else if (r == 2) dt = 50.f;
      else if (r == 3) dt = INFINITY;
    }
    // random commands
    const uint32_t r = rnd() % 60;
    if (r == 0 && w.mode == SW_OFF) {
      const int d = (rnd() & 1) ? 1 : -1;
      const float x = d < 0 ? kMountX : 380.f;
      h.feet = Vec2{x, kGround};
      h.facing = d;
      slackWalkerMount(w, x, kGround, d);
      ++*mounts_seen;
    } else if (r == 1) slackWalkerWalk(w, l, (int)(rnd() % 8) - 2);
    else if (r == 2) slackWalkerWobble(w, rndf() * 1.4f - 0.2f);
    else if (r == 3 && rnd() % 4 == 0) slackWalkerFlourish(w);
    else if (r == 4 && rnd() % 5 == 0) slackWalkerFall(w, rng);
    else if (r == 5 && rnd() % 6 == 0) slackWalkerDismount(w);
    else if (r == 6 && rnd() % 6 == 0) slacklineKick(l, (rndf() - 0.5f) * 200.f);
    else if (r == 7 && rnd() % 8 == 0) {
      slackCritterInit(sc, l, (rnd() & 1) ? CR_SQUIRREL : CR_CHIPMUNK, (rnd() & 1) ? 1 : -1, (rnd() & 1) ? SLACK_CROSS_BOTH : SLACK_CROSS_HALF, rng);
      critterInit(cr, (CritterKind)sc.kind, slackCritterFeet(sc, l), sc.dir);
      crit_on = true;
    }
    slackWalkerStep(w, l, dt);
    if (crit_on) slackCritterStep(sc, l, dt);
    slacklineStep(l, dt);
    hikerTick(h, kDt);
    slackWalkerApply(w, l, h);
    if (crit_on) {
      slackCritterDrive(sc, cr);
      critterTick(cr, kDt);
      slackCritterPlace(sc, l, cr);
    }
    CHECK(finiteLine(l) && finiteWalker(w) && finiteCrit(sc) && finitePose(h.pose) && finiteF(h.feet.x) && finiteF(h.feet.y), "stress %u tick %d: not finite",
          seed, t);
    if (!finiteLine(l) || !finiteWalker(w)) break;
    const Rect hb = hikerBounds(h);
    CHECK(hb.x0 > -200 && hb.x1 < kW + 200 && hb.y0 > -50 && hb.y1 < kH + 50, "stress %u tick %d: the hiker wandered off (%d,%d..%d,%d)", seed, t, hb.x0, hb.y0, hb.x1, hb.y1);
    CHECK(fabsf(l.d) < 80.f, "stress %u tick %d: the spring ran away (%.1f)", seed, t, (double)l.d);
    if (fabsf(l.d) > *max_d) *max_d = fabsf(l.d);
    if (w.mode != SW_OFF) {
      // never below the ground; never more than a few px above the line's resting y while on it
      CHECK(h.feet.y <= kGround + 0.01f, "stress %u tick %d: feet below the ground (%.2f)", seed, t, (double)h.feet.y);
      const float above = kGround - h.feet.y;
      if (above > *worst_above) *worst_above = above;
    }
    if (crit_on && !slackCritterDone(sc)) CHECK(fabsf(cr.feet.y - slacklineTopY(l, cr.feet.x)) < 0.01f, "stress %u tick %d: critter off the line", seed, t);
    hash = (hash ^ (uint32_t)(int32_t)(h.feet.x * 16.f)) * 16777619u;
    hash = (hash ^ (uint32_t)(int32_t)(h.feet.y * 16.f)) * 16777619u;
    hash = (hash ^ (uint32_t)(int32_t)(l.d * 64.f)) * 16777619u;
    hash = (hash ^ (uint32_t)(int32_t)(h.pose.thigh[0] * 1000.f)) * 16777619u;
    hash = (hash ^ (uint32_t)w.mode) * 16777619u;
  }
  return hash;
}

static void testLongRuns() {
  printf("long random runs, bad input, replay\n");
  int mounts = 0;
  float above = 0.f, md = 0.f;
  const uint32_t h1 = stressRun(11u, 25 * 3600, false, &mounts, &above, &md);
  printf("  1 hour of random commands: %d mounts, feet at most %.1f px above the ground, spring at most %.1f px\n", mounts, (double)above, (double)md);
  CHECK(mounts > 20, "the stress run mounted %d times", mounts);
  int m2 = 0;
  float a2 = 0.f, md2 = 0.f;
  const uint32_t h2 = stressRun(11u, 25 * 3600, false, &m2, &a2, &md2);
  CHECK(h1 == h2, "a replay of the long run is identical");
  const uint32_t h3 = stressRun(12u, 25 * 1800, false, &m2, &a2, &md2);
  CHECK(h3 != h1, "another seed gives another run");
  int m3 = 0;
  float a3 = 0.f, md3 = 0.f;
  stressRun(13u, 25 * 1800, true, &m3, &a3, &md3);   // bad dt values sprinkled in
  printf("  with bad dts (NaN, infinity, negative, 50 s) mixed in: spring at most %.1f px\n", (double)md3);
  // A critter or walker commanded in the wrong state is ignored, not corrupted.
  Rng rng;
  rng.seed(5);
  Slackline l = makeLine();
  SlackWalker w;
  slackWalkerInit(w, rng);
  slackWalkerWalk(w, l, 3);
  slackWalkerFlourish(w);
  slackWalkerFall(w, rng);
  slackWalkerDismount(w);
  CHECK(w.mode == SW_OFF, "commands to a walker who is not on the line change nothing");
  slackWalkerMount(w, kMountX, kGround, -1);
  slackWalkerMount(w, 100.f, 300.f, 1);
  CHECK(w.mode == SW_MOUNT && fabsf(w.x0 - kMountX) < 0.01f, "a second mount while mounting is ignored");
  slackWalkerMount(w, NAN, kGround, 1);
  CHECK(finiteWalker(w), "a bad mount is ignored");
}

static void testSizes() {
  printf("state sizes: Slackline %d bytes, SlackWalker %d bytes, SlackCritter %d bytes; the line is also kept once more for slacklineDirty\n",
         (int)sizeof(Slackline), (int)sizeof(SlackWalker), (int)sizeof(SlackCritter));
  CHECK(sizeof(Slackline) <= 96 && sizeof(SlackWalker) <= 160 && sizeof(SlackCritter) <= 80, "the states are small");
}

// ---- the zoom sheet ---------------------------------------------------------------------------------------

// A frame of the sim cropped around `cx` at 3x.
static void zoomTile(Sheet& sh, int idx, const Sim& s, const Palette& pal, const Pixel* bg, float cx, float cy, const char* what) {
  drawSim(s, pal, bg, g_c);
  const int zw = sh.tw / 3, zh = sh.th / 3;
  const Rect crop{(int)cx - zw / 2, (int)cy - zh + 12, (int)cx - zw / 2 + zw, (int)cy + 12};
  sh.tileUp(idx, g_c, crop, 3);
  (void)what;
}

static void testZoom(const char* dir) {
  printf("zoom sheet: the poses at 3x\n");
  const Palette& pal = g_pal[3];
  const Pixel* bg = g_land[3];
  Sheet sh;
  sh.init(4, 3, 140 * 3, 120 * 3);
  Sim s;
  simInit(s, 1);
  s.ph = 99;   // the zoom drives the walker itself, not scriptB
  s.h.feet = Vec2{kMountX, kGround};
  s.h.facing = kDir;
  int idx = 0;
  bool got[12] = {false};
  int stage = 0;
  float st = 0.f;
  for (int t = 0; t < 25 * 120 && idx < 8; ++t) {
    st += kDt;
    SlackWalker& w = s.w;
    switch (stage) {
      case 0: slackWalkerMount(w, kMountX, kGround, kDir); stage = 1; st = 0.f; break;
      case 1: if (!got[0] && w.mode == SW_MOUNT && w.t >= 0.42f) { zoomTile(sh, 0, s, pal, bg, w.x, s.h.feet.y, "mount"); got[0] = true; }
              if (w.mode == SW_STAND) { slackWalkerWobble(w, 0.f); stage = 2; st = 0.f; } break;
      case 2: if (st >= 1.5f) { zoomTile(sh, 1, s, pal, bg, w.x, s.h.feet.y, "calm"); stage = 3; st = 0.f; slackWalkerWobble(w, 0.5f); } break;
      case 3: if (st >= 2.5f) { zoomTile(sh, 2, s, pal, bg, w.x, s.h.feet.y, "wobble"); stage = 4; st = 0.f; slackWalkerWobble(w, 0.95f); } break;
      case 4: if (st >= 2.0f) { zoomTile(sh, 3, s, pal, bg, w.x, s.h.feet.y, "flail"); stage = 5; st = 0.f; slackWalkerWobble(w, 0.15f); } break;
      case 5: if (st >= 1.5f) { slackWalkerWalk(w, s.line, 4); stage = 6; } break;
      case 6: if (!got[4] && w.mode == SW_WALK && w.step_u >= 0.38f) { zoomTile(sh, 4, s, pal, bg, w.x, s.h.feet.y, "step"); got[4] = true; }
              if (w.mode == SW_STAND) { slackWalkerFlourish(w); stage = 7; st = 0.f; } break;
      case 7: if (!got[5] && w.mode == SW_FLOURISH && w.t >= 2.2f) { zoomTile(sh, 5, s, pal, bg, w.x, s.h.feet.y, "up"); got[5] = true; }
              if (!got[6] && w.mode == SW_FLOURISH && w.t >= 3.3f) { zoomTile(sh, 6, s, pal, bg, w.x, s.h.feet.y, "bow"); got[6] = true; }
              if (w.mode == SW_STAND) { slackWalkerDismount(w); stage = 8; st = 0.f; } break;
      case 8: if (!got[7] && w.mode == SW_FALL && w.t >= 0.42f) { zoomTile(sh, 7, s, pal, bg, w.x, s.h.feet.y, "dismount"); got[7] = true; idx = 8; } break;
      default: break;
    }
    simStep(s);   // (variant 1's script is not used: simScript would run; zoom drives the walker itself)
  }
  // The fall, in its three moments, from a fresh sim.
  Sim f;
  simInit(f, 1);
  f.ph = 99;
  f.h.feet = Vec2{kMountX, kGround};
  slackWalkerMount(f.w, kMountX, kGround, kDir);
  f.h.facing = kDir;
  bool g8 = false, g9 = false, g10 = false;
  int fs = 0;
  for (int t = 0; t < 25 * 40 && !g10; ++t) {
    SlackWalker& w = f.w;
    if (fs == 0 && w.mode == SW_STAND) { slackWalkerWobble(w, 0.95f); fs = 1; f.ph_tick = t; }
    if (fs == 1 && (t - f.ph_tick) * kDt > 2.f) { Rng r; r.seed(2); slackWalkerFall(w, r); fs = 2; }
    if (fs == 2 && w.mode == SW_FALL) {
      if (!g8 && w.t >= 0.45f) { zoomTile(sh, 8, f, pal, bg, w.x, f.h.feet.y, "tip"); g8 = true; }
      if (!g9 && w.t >= 0.80f) { zoomTile(sh, 9, f, pal, bg, w.x, f.h.feet.y, "air"); g9 = true; }
      if (!g10 && w.t >= 1.03f) { zoomTile(sh, 10, f, pal, bg, w.x, f.h.feet.y, "landing"); g10 = true; }
    }
    simStep(f);
  }
  // The critter, sitting on the line, and the line bowing under the walker at the end of the sheet.
  Sim c;
  simInit(c, 2);
  c.ph = 99;
  startCritter(c, CR_SQUIRREL, SLACK_CROSS_BOTH);
  for (int t = 0; t < 25 * 60 && !slackCritterSitting(c.sc); ++t) simStep(c);
  for (int t = 0; t < 25 * 2; ++t) simStep(c);
  zoomTile(sh, 11, c, pal, bg, c.sc.x - 20.f, c.cr.feet.y + 10.f, "critter");
  writeSheet(dir, "slackline_zoom.ppm", sh);
}

int main(int argc, char** argv) {
  g_scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(g_scratch);
  const char* dir = argc > 1 ? argv[1] : "";
  fillNoise(g_noise);
  for (int i = 0; i < kW * kH; ++i) g_flat[i] = rgb565_hex(0x6E9FB8);
  buildLandscapes();

  testSizes();
  testLinePhysics();
  testMoods();
  testLineDraw();
  testWalkerGeometry();
  testVariantA(dir);
  testVariantB(dir);
  testCritter(dir);
  testLongRuns();
  testZoom(dir);

  printf("%d checks, %d failed\n", g_checks, g_fail);
  free(g_scratch);
  return g_fail ? 1 : 0;
}
