// Tests for core/scene/ropes: the three variants of the ropes course (A all
// three with the net mishap, B the bridge and the swing, C the swing and the
// net) driven by a real hiker rig over a real landscape. Checks that the bounds
// cover every pixel drawn, that a strip draw equals the frame draw and nothing
// lands outside the clip or in the status chip's corner, ropesDirty's contract
// (empty when nothing visible changed, otherwise covering every changed pixel),
// that ropes.cpp's copy of the rig's kinematics agrees with the rig, that his
// feet stay on the sagging planks, that the tyre's pendulum stays finite and
// settles, each variant's ending and length, no NaN over long runs and bad
// input, a replay, and the dirty strips per frame.
// Plain executable; returns non-zero on failure.
//
// Writes inspection sheets to the directory in argv[1] (default: the current
// directory) as PPM: ropes_a.ppm, ropes_b.ppm, ropes_c.ppm (a frame every
// second, 2x downscaled, 6 across, the time in seconds in each corner),
// ropes_zoom.ppm (each item at 3x with him on it) and ropes_moods.ppm (the
// built course at 2x in every mood). Convert to PNG with the scratch folder's
// ppm2png.py.
#include "anim/ease.h"
#include "anim/rng.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/buildup.h"
#include "scene/hiker.h"
#include "scene/landscape.h"
#include "scene/ropes.h"

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

// ---- helpers ------------------------------------------------------------------------------------

static const int kW = SS_WIDTH, kH = SS_HEIGHT;
static const float kDt = SS_TICK_DT;
static const Rect kFrame{0, 0, kW, kH};
static const Rect kChip{538, 412, 796, 476};   // the status chip, its badges and the doorbell pulse
static const float kX0 = 150.f;                 // the course's left foot in the sheets
static const float kGY = 330.f;                 // and its ground line

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

// ---- the sim ---------------------------------------------------------------------------------

struct Sim {
  Hiker h;
  RopesState r;
  int tick;
};

static void simInit(Sim& s, int variant, uint32_t seed, float x0 = kX0, float gy = kGY) {
  memset(&s, 0, sizeof s);
  hikerInit(s.h, Vec2{x0 - 40.f, gy}, 1);
  hikerSetPose(s.h, POSE_STAND, 0.f);
  hikerTick(s.h, 0.f);
  ropesInit(s.r, x0, gy);
  Rng rng;
  rng.seed(seed);
  ropesStart(s.r, variant, rng);
}

// One scene tick, in the order the header gives.
static void simTick(Sim& s, float dt = kDt) {
  ropesStep(s.r, s.h, dt);
  hikerTick(s.h, dt);
  ropesPose(s.r, s.h);
  ++s.tick;
}

static bool finiteSim(const Sim& s) {
  const RopesState& r = s.r;
  const float f[] = {r.x0, r.gy, r.t, r.st, r.bt, r.bw, r.bx, r.dep, r.bnc, r.bnv, r.bph, r.bidx, r.th, r.om,
                     r.pump, r.spin, r.tmax, r.nsw, r.nsv, r.ax, r.ay, r.rot};
  for (float v : f)
    if (!finiteF(v)) return false;
  const Hiker& h = s.h;
  const float g[] = {h.feet.x, h.feet.y, h.pose.lean, h.pose.head, h.pose.lift, h.pose.dx, h.pose.thigh[0], h.pose.thigh[1],
                     h.pose.knee[0], h.pose.knee[1], h.pose.shoulder[0], h.pose.shoulder[1], h.pose.elbow[0], h.pose.elbow[1]};
  for (float v : g)
    if (!finiteF(v)) return false;
  return true;
}

// ---- bounds, clip, strips, chip: one state over the noise background ----------------------------------------------

static void checkDraw(const RopesState& r, const Palette& pal, const char* who, int tick, uint32_t salt) {
  const Rect b = ropesBounds(r);
  // 1. The whole-frame draw touches nothing outside the bounds, nothing in the chip's corner.
  memcpy(g_a, g_noise, sizeof g_a);
  Surface full = frameSurface(g_a);
  ropesDraw(r, full, kFrame, pal);
  const Rect d = diffBox(g_a, g_noise, kFrame);
  CHECK(d.empty() || inside(b, d), "%s tick %d: pixels outside the bounds: drawn %d,%d..%d,%d bounds %d,%d..%d,%d", who, tick, d.x0,
        d.y0, d.x1, d.y1, b.x0, b.y0, b.x1, b.y1);
  CHECK(b.empty() ? d.empty() : true, "%s tick %d: pixels drawn with empty bounds", who, tick);
  const Rect dc = diffBox(g_a, g_noise, kChip);
  CHECK(dc.empty(), "%s tick %d: pixels in the chip corner %d,%d..%d,%d", who, tick, dc.x0, dc.y0, dc.x1, dc.y1);

  // 2. Strip draws equal the frame draw.
  memcpy(g_b, g_noise, sizeof g_b);
  for (int sy = 0; sy < SS_STRIP_COUNT; ++sy) {
    Surface strip{g_b + (size_t)sy * SS_STRIP_ROWS * kW, kW, SS_STRIP_ROWS, kW, sy * SS_STRIP_ROWS};
    ropesDraw(r, strip, Rect{0, sy * SS_STRIP_ROWS, kW, (sy + 1) * SS_STRIP_ROWS}, pal);
  }
  CHECK(memcmp(g_a, g_b, sizeof g_a) == 0, "%s tick %d: strip draws differ from the frame draw", who, tick);

  // 3. A clip, even one that cuts across the course, is respected.
  uint32_t h = salt * 2654435761u + (uint32_t)tick * 40503u + 1u;
  for (int c = 0; c < 2; ++c) {
    h = h * 1664525u + 1013904223u;
    const int x0 = (int)((h >> 8) % 760u), y0 = (int)((h >> 18) % 440u);
    h = h * 1664525u + 1013904223u;
    Rect clip{x0, y0, x0 + 20 + (int)((h >> 8) % 300u), y0 + 10 + (int)((h >> 20) % 150u)};
    if (c == 1 && !b.empty()) clip = Rect{(b.x0 + b.x1) / 2 - 15, (b.y0 + b.y1) / 2 - 30, (b.x0 + b.x1) / 2 + 60, (b.y0 + b.y1) / 2 + 30};
    memcpy(g_c, g_noise, sizeof g_c);
    Surface s = frameSurface(g_c);
    ropesDraw(r, s, clip, pal);
    const Rect dd = diffBox(g_c, g_noise, kFrame);
    CHECK(dd.empty() || inside(clip.clipped(kFrame), dd), "%s tick %d: draw outside clip %d,%d..%d,%d: %d,%d..%d,%d", who, tick, clip.x0,
          clip.y0, clip.x1, clip.y1, dd.x0, dd.y0, dd.x1, dd.y1);
  }
}

// ropesDirty between two states: covers every pixel that differs, inside the bounds; same <=> no rects.
static void renderRegion(const RopesState& r, const Palette& pal, const Pixel* bg, const Rect& region, Pixel* out) {
  for (int y = region.y0; y < region.y1; ++y) memcpy(out + (size_t)y * kW, bg + (size_t)y * kW, sizeof(Pixel) * kW);
  Surface s = frameSurface(out);
  ropesDraw(r, s, region, pal);
}

static void checkTick(const RopesState& prev, const RopesState& cur, const Palette& pal, const Pixel* bg, const char* who, int tick) {
  Rect rs[ROPES_DIRTY_MAX];
  const int n = ropesDirty(prev, cur, rs);
  CHECK(n >= 0 && n <= ROPES_DIRTY_MAX, "%s tick %d: %d dirty rects", who, tick, n);
  CHECK(ropesSame(prev, cur) == (n == 0), "%s tick %d: ropesSame disagrees with ropesDirty (%d rects)", who, tick, n);
  const Rect reg0 = unite(ropesBounds(prev), ropesBounds(cur));
  for (int i = 0; i < n; ++i) CHECK(inside(reg0, rs[i]) && !rs[i].empty(), "%s tick %d: dirty rect %d outside the bounds", who, tick, i);
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

// ---- 2x sheets ---------------------------------------------------------------------------------------------------------------

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

// A 3x5 digit font for the labels.
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

static void drawScene(const Sim& s, const Palette& pal, const Pixel* bg, Pixel* out) {
  memcpy(out, bg, sizeof(Pixel) * (size_t)kW * kH);
  Surface surf = frameSurface(out);
  ropesDraw(s.r, surf, kFrame, pal);
  hikerDraw(s.h, surf, pal, kFrame);
}

// ---- running a variant ----------------------------------------------------------------------------

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The lowest drawn row per column of the hiker alone against the floor: how far the soles are from the planks.
static void feetGap(const Hiker& h, const RopesState& r, float* min_gap, float* max_gap) {
  const int xa = (int)h.feet.x - 60, xb = (int)h.feet.x + 60;
  Hiker hh = h;
  const Rect hb = hikerBounds(hh);
  for (int y = hb.y0; y < hb.y1; ++y)
    for (int x = xa; x < xb; ++x) g_a[y * kW + x] = g_flat[y * kW + x];
  Surface s = frameSurface(g_a);
  hikerDraw(hh, s, g_pal[3], Rect{xa, hb.y0, xb, hb.y1});
  const float bg = (float)green8(g_flat[0]), ink = (float)green8(g_pal[3][ACTOR]);
  float mn = 1e9f, mx = -1e9f;
  for (int x = xa; x < xb; ++x) {
    float bottom = -1e9f;
    for (int y = hb.y1 - 1; y >= hb.y0; --y) {
      const float cov = clampf((bg - (float)green8(g_a[y * kW + x])) / (bg - ink), 0.f, 1.f);
      if (cov > 0.08f) {
        bottom = (float)y + cov;
        break;
      }
    }
    if (bottom < -1e8f) continue;
    const float gap = ropesFloorY(r, (float)x + 0.5f) - bottom;
    if (gap < mn) mn = gap;
    if (gap > mx) mx = gap;
  }
  *min_gap = mn;
  *max_gap = mx;
}

struct Stats {
  long ticks[3];          // build, items, tear
  long ropes_strips[3];   // strips the props dirty a frame
  long all_strips[3];     // props and him
  int ropes_max[3], all_max[3];
  long clean;             // ticks on which the props marked nothing
  long all_ticks;
};

struct RunResult {
  Sim s;
  Stats st;
  float seconds;
  uint8_t seg_seen[SG_COUNT];
  float max_th, max_rot, min_rot;
  float min_gap, max_gap;       // the soles from the planks while he is on them (negative: through them)
  float worst_touch;            // the largest |closest approach| over those ticks
  int gap_ticks;
  float hang_ankle_err;         // the caught ankle from its rung
  float hang_head_below;        // how far below the hips his head hangs, least over the hang
  int hang_ticks;
  int moved_idle;               // ticks on which the module moved him outside its segments
  int facing_flips;
  float spin_end;
  Rect extent;                  // the union of the props' bounds over the run
  int ticks;
};

struct ShotDef {
  int seg;
  float at;
};
static const ShotDef kShots[10] = {{SG_B_CROSS1, 1.6f}, {SG_B_BRACE, 1.1f}, {SG_S_PUMP, 4.6f},  {SG_S_SPIN, 1.55f}, {SG_S_DIZZY, 2.4f},
                                   {SG_N_CLIMB, 4.2f},  {SG_N_CATCH, 1.2f}, {SG_N_FLIP, 0.45f}, {SG_N_HANG, 1.2f},  {SG_N_SITUP, 1.9f}};

struct RunOpts {
  bool deep;        // per-tick dirty/draw checks
  Sheet* sheet;     // a frame a second
  Sheet* zoom;      // the ten shots at 3x
  float dt;
};

static void runVariant(int variant, uint32_t seed, const RunOpts& o, RunResult& res) {
  memset(&res, 0, sizeof res);
  Sim& s = res.s;
  simInit(s, variant, seed);
  const Palette& pal = g_pal[3];
  const Pixel* bg = g_land[3];
  char who[16];
  snprintf(who, sizeof who, "%c%u", 'A' + variant, seed);
  res.min_gap = 1e9f;
  res.max_gap = -1e9f;
  res.min_rot = 1e9f;
  res.hang_head_below = 1e9f;
  const int node_row = 4 + (s.r.k[4] & 1);
  int next_frame = 0;
  bool shot_done[10] = {false};
  uint8_t last_facing = (uint8_t)s.h.facing;
  const int max_ticks = (int)(200.f / o.dt);
  for (int tick = 0; tick < max_ticks; ++tick) {
    const RopesState prev = s.r;
    const Hiker hprev = s.h;
    const int seg0 = ropesSegment(prev);
    simTick(s, o.dt);
    const int seg = ropesSegment(s.r);
    if (seg >= 0) res.seg_seen[seg] = 1;
    CHECK(finiteSim(s), "%s tick %d: state not finite (segment %s)", who, tick, ropesSegName(seg));
    if (!finiteSim(s)) break;
    const float th = fabsf(s.r.th);
    if (th > res.max_th) res.max_th = th;
    if (seg == SG_N_FLIP || seg == SG_N_HANG || seg == SG_N_DROP) {
      if (s.r.rot > res.max_rot) res.max_rot = s.r.rot;
    }
    if (seg == SG_S_SPIN || seg == SG_S_PUMP || seg == SG_S_SLOW) {
      if ((uint8_t)s.h.facing != last_facing) ++res.facing_flips;
      last_facing = (uint8_t)s.h.facing;
    }
    res.spin_end = s.r.spin;

    // He is not moved while the module does not have him.
    if ((seg0 == SG_BUILD || seg0 == SG_TEAR) && (seg == seg0)) {
      if (s.h.feet.x != hprev.feet.x || s.h.feet.y != hprev.feet.y || s.h.facing != hprev.facing) ++res.moved_idle;
    }

    // Strips a frame, as the renderer would count them.
    {
      Rect rs[ROPES_DIRTY_MAX];
      const int n = ropesDirty(prev, s.r, rs);
      uint16_t mr = 0;
      for (int i = 0; i < n; ++i) mr = (uint16_t)(mr | rowsMask(rs[i]));
      const uint16_t mh = (uint16_t)(rowsMask(hikerBounds(hprev)) | rowsMask(hikerBounds(s.h)));
      const int ph = seg0 == SG_BUILD ? 0 : (seg0 == SG_TEAR ? 2 : 1);
      ++res.st.ticks[ph];
      res.st.ropes_strips[ph] += popc(mr);
      res.st.all_strips[ph] += popc((uint16_t)(mr | mh));
      if (popc(mr) > res.st.ropes_max[ph]) res.st.ropes_max[ph] = popc(mr);
      if (popc((uint16_t)(mr | mh)) > res.st.all_max[ph]) res.st.all_max[ph] = popc((uint16_t)(mr | mh));
      if (n == 0) ++res.st.clean;
      ++res.st.all_ticks;
    }
    res.extent = unite(res.extent, ropesBounds(s.r));

    if (o.deep) {
      checkTick(prev, s.r, pal, bg, who, tick);
      if (tick % 4 == 0) checkDraw(s.r, pal, who, tick, seed + (uint32_t)variant);
    }

    // The feet on the planks.
    if ((seg == SG_B_CROSS1 || seg == SG_B_BRACE || seg == SG_B_CROSS2) && (tick & 1) == 0) {
      float mn, mx;
      feetGap(s.h, s.r, &mn, &mx);
      if (mn < res.min_gap) res.min_gap = mn;
      if (mn > res.max_gap) res.max_gap = mn;
      ++res.gap_ticks;
    }
    // Upside down by one foot.
    if (seg == SG_N_HANG) {
      Vec2 hips, head, ankle;
      ropesRigJoints(s.h, &hips, &head, nullptr, nullptr, &ankle);
      const Vec2 n2 = ropesNetNode(s.r, 2, node_row), n3 = ropesNetNode(s.r, 3, node_row);
      const float ex = 0.5f * (n2.x + n3.x) - ankle.x, ey = (n2.y - 1.1f) - ankle.y;
      const float err = sqrtf(ex * ex + ey * ey);
      if (err > res.hang_ankle_err) res.hang_ankle_err = err;
      if (head.y - hips.y < res.hang_head_below) res.hang_head_below = head.y - hips.y;
      ++res.hang_ticks;
    }

    if (o.sheet && s.r.t >= (float)next_frame - 1e-4f) {
      drawScene(s, pal, bg, g_c);
      o.sheet->tileDown2(next_frame, g_c);
      char lab[16];
      snprintf(lab, sizeof lab, "%d", next_frame);
      label(*o.sheet, next_frame, lab);
      ++next_frame;
    }
    if (o.zoom) {
      for (int i = 0; i < 10; ++i) {
        if (shot_done[i] || seg != kShots[i].seg || s.r.st < kShots[i].at) continue;
        shot_done[i] = true;
        drawScene(s, pal, bg, g_c);
        Vec2 hips;
        ropesRigJoints(s.h, &hips, nullptr, nullptr, nullptr, nullptr);
        const int z = 3, cw = o.zoom->tw / z, ch = o.zoom->th / z;
        const Rect crop{(int)hips.x - cw / 2, (int)hips.y - ch / 2 - 8, (int)hips.x - cw / 2 + cw, (int)hips.y - ch / 2 - 8 + ch};
        o.zoom->tileUp(i, g_c, crop, z);
      }
    }
    if (ropesDone(s.r)) break;
  }
  res.seconds = s.r.t;
  res.ticks = s.tick;
}

// ---- the tests ------------------------------------------------------------------------------------------

static RunResult g_res[3];

static void printStrips(const char* name, const Stats& st) {
  static const char* const kPh[3] = {"build", "items", "tear "};
  for (int i = 0; i < 3; ++i) {
    if (st.ticks[i] == 0) continue;
    const double n = (double)st.ticks[i];
    printf("  %s %s: %ld ticks; strips per frame: props %.2f avg (max %d), props and him %.2f avg (max %d)\n", name, kPh[i], st.ticks[i],
           (double)st.ropes_strips[i] / n, st.ropes_max[i], (double)st.all_strips[i] / n, st.all_max[i]);
  }
  printf("  %s: the props marked nothing on %.0f%% of the %ld ticks\n", name, 100.0 * (double)st.clean / (double)st.all_ticks, st.all_ticks);
}

static void testKinematics() {
  printf("the rig's kinematics, as ropes.cpp copies them\n");
  float worst = 0.f;
  int cases = 0;
  for (int facing = -1; facing <= 1; facing += 2)
    for (int p = 0; p < POSE_COUNT; ++p)
      for (int variant = 0; variant < 4; ++variant) {
        Hiker h;
        memset(&h, 0, sizeof h);
        hikerInit(h, Vec2{300.f, 340.f}, facing);
        hikerSetPose(h, (PoseId)p, 0.f);
        if (variant == 3) {   // mid-stride
          hikerSetPose(h, POSE_STAND, 0.f);
          hikerWalkTo(h, 600.f * (float)facing + 300.f, 34.f);
          for (int i = 0; i < 40; ++i) hikerTick(h, kDt);
        } else {
          hikerTick(h, 0.37f * (float)p);   // some breath
        }
        if (variant == 1) {
          h.pose.lift = 0.3f;
          h.pose.dx = 0.1f;
        }
        if (variant == 2) {
          h.pose.lean += 1.1f;
          h.pose.thigh[0] += 2.f;
          h.pose.shoulder[1] -= 1.5f;
          h.pose.head = 0.7f;
        }
        Vec2 head, hn, hf;
        ropesRigJoints(h, nullptr, &head, &hn, &hf, nullptr);
        float rad;
        const Vec2 rh = hikerHeadPos(h, &rad), rn = hikerHandPos(h, 0), rf = hikerHandPos(h, 1);
        const float e = fmaxf(fmaxf(fabsf(head.x - rh.x), fabsf(head.y - rh.y)),
                              fmaxf(fmaxf(fabsf(hn.x - rn.x), fabsf(hn.y - rn.y)), fmaxf(fabsf(hf.x - rf.x), fabsf(hf.y - rf.y))));
        if (e > worst) worst = e;
        ++cases;
        CHECK(e < 0.2f, "pose %s facing %d variant %d: joints differ from the rig by %.3f px", hikerPoseName((PoseId)p), facing, variant,
              (double)e);
      }
  printf("  %d poses, both facings, with lift, dx, a wild pose and a walk: worst difference %.4f px\n", cases, (double)worst);
}

static void testPlans() {
  printf("the plans\n");
  RopesState r;
  ropesInit(r, kX0, kGY);
  CHECK(!ropesDone(r) && !ropesHikerBusy(r) && ropesSegment(r) == -1, "idle: not done, not busy");
  CHECK(ropesBounds(r).empty(), "idle: nothing drawn");
  CHECK(ropesBounds(r).empty() && sizeof(RopesState) <= 400, "sizeof(RopesState) = %d", (int)sizeof(RopesState));
  for (int v = 0; v < 3; ++v) {
    Rng rng;
    rng.seed(9);
    ropesStart(r, v, rng);
    bool has[SG_COUNT] = {false};
    for (int i = 0; i < r.n_seg; ++i) has[r.plan[i]] = true;
    const bool bridge = has[SG_B_CROSS1], swing = has[SG_S_PUMP], net = has[SG_N_HANG];
    CHECK(bridge == (v != 2) && swing && net == (v != 1), "variant %c has the right items (bridge %d swing %d net %d)", 'A' + v, bridge, swing, net);
    CHECK(r.plan[0] == SG_BUILD && r.plan[r.n_seg - 1] == SG_TEAR && r.n_seg <= ROPES_PLAN_MAX, "variant %c: build first, tear-down last", 'A' + v);
    printf("  variant %c: %d segments, fixed time %.1f s\n", 'A' + v, r.n_seg, (double)ropesFixedSeconds(r));
  }
  // ropesStart draws exactly two values.
  Rng a, b;
  a.seed(5);
  b.seed(5);
  ropesStart(r, 0, a);
  b.next();
  b.next();
  CHECK(a.next() == b.next(), "ropesStart draws exactly two values");
  ropesStart(r, 7, a);
  CHECK(r.variant == ROPES_A, "an unknown variant becomes A");
  printf("  sizeof(RopesState) = %d bytes\n", (int)sizeof(RopesState));
}

static void testPendulum() {
  printf("the tyre's pendulum\n");
  Sim s;
  simInit(s, ROPES_B, 4);
  while (ropesSegment(s.r) == SG_BUILD) simTick(s);
  const int held = s.r.seg;
  s.r.plan[held] = SG_B_PROUD;   // a timed segment with nothing of the swing in it; kept from ending below

  // Free: starts at 0.5 rad, never grows, settles.
  s.r.th = 0.5f;
  s.r.om = 0.f;
  s.r.pump = 0.f;
  float amp_first = 0.f, amp_late = 0.f, amp_end = 0.f;
  bool fin = true;
  for (int i = 0; i < 30 * 25; ++i) {
    s.r.st = 0.f;
    simTick(s);
    fin = fin && finiteF(s.r.th) && finiteF(s.r.om);
    const float t = (float)(i + 1) * kDt, th = fabsf(s.r.th);
    if (t <= 2.8f && th > amp_first) amp_first = th;
    if (t > 12.f && t <= 15.f && th > amp_late) amp_late = th;
    if (t > 26.f && th > amp_end) amp_end = th;
  }
  CHECK(fin, "the pendulum stays finite");
  CHECK(amp_first <= 0.505f, "free swing never grows (%.3f)", (double)amp_first);
  CHECK(amp_late < 0.03f, "free swing has died down by 12 s (%.4f rad)", (double)amp_late);
  CHECK(amp_end == 0.f && s.r.om == 0.f, "and is at rest: exactly still (%.5f)", (double)amp_end);
  printf("  free: 0.5 rad falls to %.4f by 12..15 s and is still after 26 s\n", (double)amp_late);

  // Pumped: grows to about its cap and stays under it; stops, settles.
  s.r.plan[held] = SG_S_PUMP;
  s.r.th = 0.f;
  s.r.om = 0.45f;
  float peak = 0.f, peak_late = 0.f;
  for (int i = 0; i < 40 * 25; ++i) {
    s.r.st = 3.f - kDt;
    simTick(s);
    const float th = fabsf(s.r.th);
    if (th > peak) peak = th;
    if (i > 20 * 25 && th > peak_late) peak_late = th;
    fin = fin && finiteF(s.r.th) && finiteF(s.r.om);
  }
  CHECK(fin && peak < 0.9f, "pumped swing is finite and bounded (%.3f)", (double)peak);
  CHECK(peak_late > 0.6f * s.r.tmax && peak_late < 1.25f * s.r.tmax, "pumped swing sits near its cap %.2f (%.3f)", (double)s.r.tmax, (double)peak_late);
  s.r.plan[held] = SG_B_PROUD;
  float after = 0.f;
  for (int i = 0; i < 40 * 25; ++i) {
    s.r.st = 0.f;
    simTick(s);
    if (i > 30 * 25 && fabsf(s.r.th) > after) after = fabsf(s.r.th);
  }
  CHECK(after < 0.01f, "and settles once he stops (%.4f after 30 s)", (double)after);
  printf("  pumped: %.3f rad against the cap %.3f; settled to %.4f after 30 s\n", (double)peak_late, (double)s.r.tmax, (double)after);
}

static void testVariants(const char* dir) {
  printf("the variants\n");
  static const char* const kName[3] = {"ropes_a.ppm", "ropes_b.ppm", "ropes_c.ppm"};
  static const int kRows[3] = {17, 13, 14};
  static const char* const kTitle[3] = {"A: all three, the net mishap", "B: the bridge and the swing", "C: the swing and the net"};
  for (int v = 0; v < 3; ++v) {
    printf("variant %s\n", kTitle[v]);
    Sheet sh;
    sh.init(6, kRows[v], 400, 240);
    RunOpts o{true, &sh, nullptr, kDt};
    RunResult& r = g_res[v];
    runVariant(v, 1, o, r);
    char path[512];
    snprintf(path, sizeof path, "%s%s%s", dir, dir[0] ? "/" : "", kName[v]);
    CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
    printf("  wrote %s (%dx%d); the run took %.1f s\n", path, sh.w(), sh.h(), (double)r.seconds);
    free(sh.rgb);

    CHECK(ropesDone(r.s.r), "%c ends done", 'A' + v);
    CHECK(!ropesHikerBusy(r.s.r) && ropesSegment(r.s.r) == -1, "%c: not busy after the end", 'A' + v);
    CHECK(ropesBounds(r.s.r).empty(), "%c: the course is gone at the end", 'A' + v);
    CHECK(r.s.h.facing == 1 && r.s.h.visible && r.s.h.hat_on && r.s.h.carry == CARRY_NONE && r.s.h.pack_on, "%c: he ends facing right, hat on, hands empty", 'A' + v);
    CHECK(fabsf(r.s.h.feet.y - kGY) < 1.f, "%c: he ends on the ground line (y %.1f)", 'A' + v, (double)r.s.h.feet.y);
    CHECK(!r.s.h.walking && r.s.h.target == POSE_STAND && !r.s.h.blending, "%c: and at rest, standing (pose %s)", 'A' + v, hikerPoseName(r.s.h.target));
    CHECK(r.s.h.feet.x > kX0 && r.s.h.feet.x < kX0 + kRopesWidth, "%c: within the course's width (x %.0f)", 'A' + v, (double)r.s.h.feet.x);
    CHECK(r.s.r.rot == 0.f && fabsf(r.s.h.pose.lift) < 0.02f, "%c: no rotation or lift left over", 'A' + v);
    CHECK(r.moved_idle == 0, "%c: he was never moved during the build or the tear-down (%d ticks)", 'A' + v, r.moved_idle);
    CHECK(r.seconds >= 60.f && r.seconds <= 90.f, "%c lasts 60..90 s (%.1f)", 'A' + v, (double)r.seconds);
    CHECK(r.extent.x0 >= (int)kX0 - 6 && r.extent.x1 <= (int)(kX0 + kRopesWidth) + 6, "%c: the props stay within %d px (x %d..%d)", 'A' + v, (int)kRopesWidth,
          r.extent.x0, r.extent.x1);
    CHECK(r.extent.y0 >= (int)(kGY - kRopesHeight) - 8 && r.extent.y1 <= (int)kGY + 8, "%c: and within the height (y %d..%d)", 'A' + v, r.extent.y0, r.extent.y1);
    CHECK(inside(Rect{0, 0, kW, kH}, r.extent), "%c: and inside the frame", 'A' + v);
    // Which segments played.
    const bool bridge = r.seg_seen[SG_B_CROSS2], swing = r.seg_seen[SG_S_RECOVER], net = r.seg_seen[SG_N_STAND];
    CHECK(bridge == (v != 2) && swing && net == (v != 1), "%c: the right items ran", 'A' + v);
    if (swing) {
      CHECK(r.max_th > 0.3f, "%c: the tyre swings out (%.2f rad)", 'A' + v, (double)r.max_th);
      CHECK(r.spin_end >= 2.f && r.spin_end <= 3.f && r.spin_end == floorf(r.spin_end), "%c: it spins whole turns (%.2f)", 'A' + v, (double)r.spin_end);
      CHECK(r.facing_flips >= 3, "%c: he turns round on it (%d flips)", 'A' + v, r.facing_flips);
      CHECK(fabsf(r.s.r.th) < 0.05f, "%c: the tyre ends nearly still (%.3f rad)", 'A' + v, (double)r.s.r.th);
    }
    if (bridge) {
      CHECK(r.gap_ticks > 100, "%c: the bridge was crossed (%d ticks checked)", 'A' + v, r.gap_ticks);
      CHECK(r.min_gap >= -1.f, "%c: his soles never go through the planks (%.2f px)", 'A' + v, (double)r.min_gap);
      CHECK(r.max_gap <= 1.f, "%c: and touch them within 1 px (%.2f px)", 'A' + v, (double)r.max_gap);
      printf("  feet on the planks over %d ticks: closest approach between %.2f and %.2f px\n", r.gap_ticks, (double)r.min_gap, (double)r.max_gap);
    }
    if (net) {
      CHECK(r.hang_ticks > 50, "%c: he hung upside down (%d ticks)", 'A' + v, r.hang_ticks);
      CHECK(r.max_rot > 2.9f && r.max_rot < 5.f, "%c: the rig turned a half turn about the hips and more for the drop (%.2f rad)", 'A' + v, (double)r.max_rot);
      CHECK(r.hang_ankle_err < 1.5f, "%c: the caught ankle stayed on its rung (%.2f px off)", 'A' + v, (double)r.hang_ankle_err);
      CHECK(r.hang_head_below > 20.f, "%c: his head hangs below his hips (%.1f px)", 'A' + v, (double)r.hang_head_below);
      printf("  hang: %d ticks, ankle %.2f px from its rung, head %.1f px below the hips, rotation up to %.2f rad\n", r.hang_ticks,
             (double)r.hang_ankle_err, (double)r.hang_head_below, (double)r.max_rot);
    }
    printStrips(kTitle[v], r.st);
  }
}

static void testSeeds() {
  printf("lengths over seeds 1..40\n");
  for (int v = 0; v < 3; ++v) {
    double sum = 0.0;
    float mn = 1e9f, mx = -1e9f;
    int bad = 0;
    RunOpts o{false, nullptr, nullptr, kDt};
    for (uint32_t seed = 1; seed <= 40; ++seed) {
      RunResult& r = g_res[v];
      runVariant(v, seed, o, r);
      const float t = r.seconds;
      sum += (double)t;
      if (t < mn) mn = t;
      if (t > mx) mx = t;
      if (!ropesDone(r.s.r) || t < 60.f || t > 90.f) ++bad;
      CHECK(ropesDone(r.s.r), "%c seed %u: ends done", 'A' + v, seed);
      CHECK(r.s.h.target == POSE_STAND && r.s.h.facing == 1 && fabsf(r.s.h.feet.y - kGY) < 1.f, "%c seed %u: ends standing facing right on the ground", 'A' + v, seed);
      if (v != ROPES_B) CHECK(r.hang_ticks > 50 && r.hang_ankle_err < 1.5f, "%c seed %u: the hang works (%d ticks, %.2f px)", 'A' + v, seed, r.hang_ticks, (double)r.hang_ankle_err);
      if (v != ROPES_C) CHECK(r.min_gap >= -1.f && r.max_gap <= 1.f, "%c seed %u: feet on the planks (%.2f .. %.2f)", 'A' + v, seed, (double)r.min_gap, (double)r.max_gap);
    }
    printf("  variant %c: %.1f s shortest, %.1f s average, %.1f s longest; %d of 40 outside 60..90 s\n", 'A' + v, (double)mn, sum / 40.0, (double)mx, bad);
    CHECK(bad == 0, "variant %c: every length within 60..90 s", 'A' + v);
  }
}

static void testDeepSeeds() {
  printf("draw, clip, strips, chip and dirty on more seeds\n");
  for (int v = 0; v < 3; ++v)
    for (uint32_t seed = 2; seed <= 4; ++seed) {
      RunOpts o{true, nullptr, nullptr, kDt};
      runVariant(v, seed, o, g_res[v]);
      CHECK(ropesDone(g_res[v].s.r), "%c seed %u done", 'A' + v, seed);
    }
  // At half rate (the scene steps every second tick with twice the dt).
  RunOpts half{true, nullptr, nullptr, 2.f * kDt};
  for (int v = 0; v < 3; ++v) {
    runVariant(v, 6, half, g_res[v]);
    CHECK(ropesDone(g_res[v].s.r) && g_res[v].seconds >= 58.f && g_res[v].seconds <= 92.f, "%c at 12.5 Hz also finishes (%.1f s)", 'A' + v, (double)g_res[v].seconds);
  }
  // A layout pushed to the bottom right: the props and he still stay out of the status chip's corner.
  Sim s;
  simInit(s, ROPES_A, 3, 500.f, 385.f);
  int checked = 0;
  for (int i = 0; i < 100 * 25 && !ropesDone(s.r); ++i) {
    simTick(s);
    if (i % 5 == 0) {
      checkDraw(s.r, g_pal[3], "corner", i, 77u);
      ++checked;
    }
    const Rect hb = hikerBounds(s.h);
    CHECK(hb.clipped(kChip).empty() || !ropesHikerBusy(s.r), "corner layout tick %d: he is in the chip's corner", i);
  }
  printf("  %d states of the layout at x0 500, ground 385 checked against the chip\n", checked);
}

static void testFeetOnFloor() {
  printf("a hiker on the floor profile at rest\n");
  // ropesFloorY is the ground away from the bridge and the walkway's top face on it.
  Sim s;
  simInit(s, ROPES_A, 1);
  while (ropesSegment(s.r) == SG_BUILD) simTick(s);
  CHECK(fabsf(ropesFloorY(s.r, kX0 - 30.f) - kGY) < 0.01f, "the ground left of the ramp");
  CHECK(fabsf(ropesFloorY(s.r, kX0 + kRopesRampX) - (kGY - 31.5f)) < 1.5f, "the ramp meets the walkway's top");
  CHECK(ropesFloorY(s.r, kX0 + 76.f) > kGY - 31.5f + 5.f, "the walkway sags in the middle (y %.1f)", (double)ropesFloorY(s.r, kX0 + 76.f));
  CHECK(fabsf(ropesFloorY(s.r, kX0 + kRopesWidth) - kGY) < 0.01f, "the ground at the far end");
}

static uint32_t hashSim(const Sim& s) {
  uint32_t h = 2166136261u;
  const unsigned char* p = (const unsigned char*)&s.r;
  for (size_t i = 0; i < sizeof s.r; ++i) h = (h ^ p[i]) * 16777619u;
  const float f[] = {s.h.feet.x, s.h.feet.y, s.h.pose.lean, s.h.pose.lift, s.h.pose.dx, s.h.pose.thigh[0], s.h.pose.shoulder[1]};
  p = (const unsigned char*)f;
  for (size_t i = 0; i < sizeof f; ++i) h = (h ^ p[i]) * 16777619u;
  return (h ^ (uint32_t)s.h.facing) * 16777619u;
}

static void testLongRuns() {
  printf("long runs, bad input, replay\n");
  int runs = 0;
  uint32_t acc = 0;
  for (int v = 0; v < 3; ++v)
    for (uint32_t seed = 1; seed <= 30; ++seed)
      for (int half = 0; half < 2; ++half) {
        Sim s;
        simInit(s, v, seed * 7u + 1u);
        const float dt = half ? 2.f * kDt : kDt;
        bool ok = true;
        for (int i = 0; i < 150 * 25 && !ropesDone(s.r); ++i) {
          simTick(s, dt);
          ok = ok && finiteSim(s);
          if (!ok) break;
        }
        CHECK(ok && ropesDone(s.r), "%c seed %u (half %d): finite and done", 'A' + v, seed, half);
        acc = acc * 31u + hashSim(s);
        ++runs;
      }
  printf("  %d runs (30 seeds x 3 variants x 2 rates), all finite and done\n", runs);

  // Replay: the same seed twice is the same, byte for byte.
  uint32_t ha[3], hb[3];
  for (int pass = 0; pass < 2; ++pass)
    for (int v = 0; v < 3; ++v) {
      Sim s;
      simInit(s, v, 99u);
      for (int i = 0; i < 40 * 25; ++i) simTick(s);
      (pass ? hb : ha)[v] = hashSim(s);
    }
  CHECK(ha[0] == hb[0] && ha[1] == hb[1] && ha[2] == hb[2], "the same seed gives the same course and hiker");
  Sim a, b;
  simInit(a, ROPES_A, 5);
  simInit(b, ROPES_A, 5);
  for (int i = 0; i < 70 * 25; ++i) {
    simTick(a);
    simTick(b);
  }
  CHECK(memcmp(&a.r, &b.r, sizeof a.r) == 0 && memcmp(&a.h, &b.h, sizeof a.h) == 0, "two identical runs end in identical bytes");
  Sim d1, d2;
  simInit(d1, ROPES_A, 5);
  simInit(d2, ROPES_C, 5);
  CHECK(d1.r.k[0] == d2.r.k[0] && d1.r.k[4] == d2.r.k[4], "the variant does not change the seeded parameters");
  bool differ = false;
  for (uint32_t seed = 1; seed < 20; ++seed) {
    Sim q;
    simInit(q, ROPES_A, seed);
    differ = differ || q.r.k[0] != d1.r.k[0] || q.r.k[4] != d1.r.k[4];
  }
  CHECK(differ, "different seeds give different parameters");

  // Bad input.
  Sim s;
  simInit(s, ROPES_A, 8);
  for (int i = 0; i < 20 * 25; ++i) simTick(s);
  const float bad[] = {NAN, -1.f, INFINITY, 50.f, 0.f, -INFINITY, 1e-9f};
  for (float dt : bad) {
    ropesStep(s.r, s.h, dt);
    hikerTick(s.h, 0.04f);
    ropesPose(s.r, s.h);
    CHECK(finiteSim(s), "dt %g leaves the state finite", (double)dt);
  }
  RopesState nan_r;
  ropesInit(nan_r, NAN, INFINITY);
  CHECK(finiteF(nan_r.x0) && finiteF(nan_r.gy), "ropesInit with NaN is made finite");
  CHECK(ropesBounds(nan_r).empty(), "and draws nothing before it starts");
  (void)acc;
}

// ---- the sheets that need more than a run -------------------------------------------------------------------

static void testZoom(const char* dir) {
  printf("zoom sheet\n");
  Sheet zoom;
  zoom.init(5, 2, 450, 450);
  RunOpts o{false, nullptr, &zoom, kDt};
  RunResult& r = g_res[0];
  runVariant(ROPES_A, 5, o, r);
  const char* const names[10] = {"cross", "brace", "pump", "spin", "dizzy", "climb", "caught", "flip", "hang", "dazed"};
  (void)names;
  char path[512];
  snprintf(path, sizeof path, "%s%sropes_zoom.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, zoom.rgb, zoom.w(), zoom.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d): bridge, brace, swing, spin, dizzy; climb, caught, flip, hang, dazed\n", path, zoom.w(), zoom.h());
  free(zoom.rgb);
}

static void testMoods(const char* dir) {
  printf("moods\n");
  Sheet sh;
  sh.init(2, 3, 700, 360);
  for (int m = 0; m < kMoods; ++m) {
    Sim s;
    simInit(s, ROPES_A, 5);
    while (ropesSegment(s.r) == SG_BUILD) simTick(s);
    for (int i = 0; i < 30; ++i) simTick(s);   // he walks a little toward the ramp
    checkDraw(s.r, g_pal[m], kMoodName[m], m, 5u);
    drawScene(s, g_pal[m], g_land[m], g_c);
    const Rect crop{(int)kX0 - 25, (int)kGY - 175, (int)kX0 - 25 + 350, (int)kGY - 175 + 180};
    sh.tileUp(m, g_c, crop, 2);
  }
  char path[512];
  snprintf(path, sizeof path, "%s%sropes_moods.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d): night, dawn, morning, day, golden, dusk\n", path, sh.w(), sh.h());
  free(sh.rgb);
}

int main(int argc, char** argv) {
  g_scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(g_scratch);
  const char* dir = argc > 1 ? argv[1] : "";
  fillNoise(g_noise);
  for (int i = 0; i < kW * kH; ++i) g_flat[i] = rgb565_hex(0x6E9FB8);
  buildLandscapes();

  printf("ropes: sizeof(RopesState) %d bytes, course %.0f x %.0f px\n", (int)sizeof(RopesState), (double)kRopesWidth, (double)kRopesHeight);
  testKinematics();
  testPlans();
  testPendulum();
  testFeetOnFloor();
  testVariants(dir);
  testDeepSeeds();
  testSeeds();
  testLongRuns();
  testZoom(dir);
  testMoods(dir);

  printf("%d checks, %d failed\n", g_checks, g_fail);
  free(g_scratch);
  return g_fail ? 1 : 0;
}
