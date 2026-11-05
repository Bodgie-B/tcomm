// Tests for core/scene/bike: the three scripted variants of the plan (A through the
// stream and back, B the front wheel hits the rock, C a wheelie that tips over) driven
// by the real hiker rig over the real landscape; the module's copy of the rig's lengths
// held to hikerHandPos; the riding pose (hip on the saddle, hands on the grips, feet on the
// pedals); bounds that cover every pixel drawn; clipping (a strip draw equals the frame
// draw and touches nothing outside the clip); the dirty rects' contract (cover every pixel
// that changed between two ticks); where the splash droplets land; the endings of each
// variant; no NaN or runaway over long random runs, bad inputs and a replay; the dirty
// strips a ride costs. Plain executable; returns non-zero on failure.
//
// Writes inspection sheets to the directory in argv[1] (default: the current directory):
// bike_a.ppm, bike_b.ppm and bike_c.ppm (a frame every 0.25 s of each variant, the bottom of
// the picture at 2x downscale, 4 across, the time in seconds in each corner) and
// bike_zoom.ppm (the bike and rider at 3x through one pedal cycle, the wheelie, the splash,
// the endings, and in the other moods). Convert to PNG for review (the scratch folder's
// ppm2png.py).
#include "anim/ease.h"
#include "anim/rng.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/bike.h"
#include "scene/buildup.h"
#include "scene/hiker.h"
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

static Pixel g_noise[SS_WIDTH * SS_HEIGHT];   // a noise background: any stray write shows
static Pixel g_a[SS_WIDTH * SS_HEIGHT];
static Pixel g_b[SS_WIDTH * SS_HEIGHT];
static Pixel g_c[SS_WIDTH * SS_HEIGHT];
static Pixel g_flat[SS_WIDTH * SS_HEIGHT];
static void* g_scratch = nullptr;
static Landscape g_ls;

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
static bool finitePose(const HikerPose& p) {
  const float f[] = {p.lean, p.head, p.lift, p.dx, p.thigh[0], p.thigh[1], p.knee[0], p.knee[1],
                     p.shoulder[0], p.shoulder[1], p.elbow[0], p.elbow[1]};
  for (float v : f)
    if (!finiteF(v)) return false;
  return true;
}
static bool finiteBike(const BikeState& b) {
  const float f[] = {b.t, b.rt, b.scale, b.pivot.x, b.pivot.y, b.pitch, b.lay, b.speed, b.wr, b.wf, b.wrw, b.wfw,
                     b.crank, b.rest_lift, b.lane_pitch, b.v0, b.hip.x, b.hip.y, b.vel.x, b.vel.y, b.mark.x, b.mark.y,
                     b.lean0, b.spawn[0], b.spawn[1], b.ring[0], b.ring[1]};
  for (float v : f)
    if (!finiteF(v)) return false;
  return finitePose(b.from);
}
static bool finiteSplash(const SplashState& s) {
  for (int i = 0; i < SPLASH_BITS; ++i) {
    const SplashBit& t = s.bit[i];
    if (!finiteF(t.x) || !finiteF(t.y) || !finiteF(t.vx) || !finiteF(t.vy) || !finiteF(t.life)) return false;
  }
  for (int i = 0; i < SPLASH_RINGS; ++i)
    if (!finiteF(s.ring[i].x) || !finiteF(s.ring[i].y) || !finiteF(s.ring[i].age)) return false;
  for (int i = 0; i < SPLASH_CROWNS; ++i)
    if (!finiteF(s.crown[i].x) || !finiteF(s.crown[i].y) || !finiteF(s.crown[i].age)) return false;
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

static const int kMoods = 6;
static const char* const kMoodName[kMoods] = {"night", "dawn", "morning", "day", "golden", "dusk"};
static Pixel g_land[kMoods][SS_WIDTH * SS_HEIGHT];
static Palette g_pal[kMoods];

static void buildLandscapes() {
  Rng rng;
  rng.seed(7);
  landscapeGenerate(g_ls, rng);
  BuildState b;
  buildReset(b);
  buildSetProgress(b, 1.f);
  for (int m = 0; m < kMoods; ++m) {
    g_pal[m] = palFor((float)m);
    Surface s = frameSurface(g_land[m]);
    landscapeDrawStatic(g_ls, s, g_pal[m], (float)m, b);
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

// ---- the courses (what a scene would build from the Landscape) -------------------------------

// A and B: from the far bank (above the stream) down across it to the near bank; the same line both ways.
static void courseAB(BikeCourse& c, int dir) {
  const Vec2 lane[4] = {{-110.f, 380.f}, {250.f, 392.f}, {520.f, 452.f}, {920.f, 470.f}};
  bikeCourseFit(c, g_ls, lane, 4, dir);
}
// C: along the dry flat of the clearing, well above the stream until x = 690.
static void courseC(BikeCourse& c, int dir) {
  const Vec2 lane[2] = {{-110.f, 368.f}, {920.f, 368.f}};
  bikeCourseFit(c, g_ls, lane, 2, dir);
  c.water_x0 = c.water_x1 = 0.f;
  c.rock_x = 0.f;
  c.wheelie_x = dir > 0 ? 170.f : 630.f;
}

// ---- the simulation ---------------------------------------------------------------------------------

struct Sim {
  Hiker h;
  BikeState b;
  SplashState sp;
  Rng rng;
  BikeCourse crs;
  int variant;   // 0 A, 1 B, 2 C
  int tick;
  int ph;
  int ph_tick;
  int a1;         // A: the tick the first pass left
  int a2;         // A: the tick the second pass left
};

static void simInit(Sim& s, int variant, int dir) {
  memset(&s, 0, sizeof s);
  s.variant = variant;
  if (variant == 2) courseC(s.crs, dir);
  else courseAB(s.crs, dir);
  hikerInit(s.h, Vec2{100.f, 350.f}, 1);
  s.h.pack_on = false;
  hikerSetPose(s.h, POSE_STAND, 0.f);
  hikerTick(s.h, 0.f);
  bikeInit(s.b, s.sp, 90.f);
  bikeSetCourse(s.b, s.crs);
  s.rng.seed(1234u + (uint64_t)variant);
  bikeRide(s.b, dir, variant == 0 ? BIKE_VAR_PASS : (variant == 1 ? BIKE_VAR_ROCK : BIKE_VAR_WHEELIE), false);
  bikeApplyRider(s.b, s.h);   // he is on the bike (off the edge) from the first frame
}

static void simStep(Sim& s) {
  hikerTick(s.h, kDt);
  bikeStep(s.b, s.sp, s.rng, kDt);
  if (bikeDriving(s.b)) bikeApplyRider(s.b, s.h);
  ++s.tick;
}

static float nowS(const Sim& s) { return (float)s.tick * kDt; }

// A's script: a pass to the right, a still moment, then back to the left, proud.
static void scriptA(Sim& s) {
  if (s.ph == 0 && s.b.mode == BIKE_AWAY) {
    s.ph = 1;
    s.ph_tick = s.tick;
    s.a1 = s.tick;
  }
  if (s.ph == 2 && s.b.mode == BIKE_AWAY && s.a2 == 0) s.a2 = s.tick;
  if (s.ph == 1 && s.tick - s.ph_tick >= 38) {   // about 1.5 s
    bikeRide(s.b, -1, BIKE_VAR_PASS, true);
    s.ph = 2;
    s.ph_tick = s.tick;
  }
}

// ---- per-tick checks ------------------------------------------------------------------------------

static void renderBike(const BikeState& b, const Palette& pal, const Pixel* bg, const Rect& region, Pixel* out) {
  for (int y = region.y0; y < region.y1; ++y) memcpy(out + (size_t)y * kW, bg + (size_t)y * kW, sizeof(Pixel) * kW);
  Surface s = frameSurface(out);
  bikeDraw(b, s, region, pal);
}
static void renderSplash(const SplashState& sp, const Palette& pal, const Pixel* bg, const Rect& region, Pixel* out) {
  for (int y = region.y0; y < region.y1; ++y) memcpy(out + (size_t)y * kW, bg + (size_t)y * kW, sizeof(Pixel) * kW);
  Surface s = frameSurface(out);
  splashDraw(sp, s, region, pal);
}

static void checkStray(const char* who, int tick, const Rect* rs, int n, const Rect& reg0, const Pixel* before, const Pixel* after,
                       const Rect& rows) {
  for (int i = 0; i < n; ++i) CHECK(inside(reg0, rs[i]) && !rs[i].empty(), "%s tick %d: dirty rect %d outside the bounds", who, tick, i);
  int stray = 0;
  for (int y = rows.y0; y < rows.y1; ++y)
    for (int x = 0; x < kW; ++x) {
      if (before[y * kW + x] == after[y * kW + x]) continue;
      bool in = false;
      for (int i = 0; i < n; ++i) in = in || (x >= rs[i].x0 && x < rs[i].x1 && y >= rs[i].y0 && y < rs[i].y1);
      if (!in) ++stray;
    }
  CHECK(stray == 0, "%s tick %d: %d changed pixels outside the dirty rects (%d rects)", who, tick, stray, n);
}

// The dirty rects of one tick cover every pixel that changed, for the bike and for the splash.
static void checkTick(const BikeState& pb, const BikeState& cb, const SplashState& ps, const SplashState& cs, const Palette& pal,
                      const Pixel* bg, const char* who, int tick) {
  {
    Rect rs[BIKE_DIRTY_MAX];
    const int n = bikeDirty(pb, cb, rs);
    CHECK(n >= 0 && n <= BIKE_DIRTY_MAX, "%s tick %d: %d bike dirty rects", who, tick, n);
    CHECK(bikeSame(pb, cb) == (n == 0), "%s tick %d: bikeSame %d but %d rects", who, tick, bikeSame(pb, cb), n);
    const Rect reg0 = unite(bikeBounds(pb), bikeBounds(cb));
    const Rect reg = reg0.clipped(kFrame);
    if (!reg.empty()) {
      const Rect rows{0, reg.y0, kW, reg.y1};
      renderBike(pb, pal, bg, rows, g_a);
      renderBike(cb, pal, bg, rows, g_b);
      checkStray(who, tick, rs, n, reg0, g_a, g_b, rows);
    }
  }
  {
    Rect rs[SPLASH_DIRTY_MAX];
    const int n = splashDirty(ps, cs, rs);
    CHECK(n >= 0 && n <= SPLASH_DIRTY_MAX, "%s tick %d: %d splash dirty rects", who, tick, n);
    CHECK(splashSame(ps, cs) == (n == 0), "%s tick %d: splashSame %d but %d rects", who, tick, splashSame(ps, cs), n);
    const Rect reg0 = unite(splashBounds(ps), splashBounds(cs));
    const Rect reg = reg0.clipped(kFrame);
    if (!reg.empty()) {
      const Rect rows{0, reg.y0, kW, reg.y1};
      renderSplash(ps, pal, bg, rows, g_a);
      renderSplash(cs, pal, bg, rows, g_b);
      // The rects need not lie inside the (shrunk) bounds of one side, but they must cover every change.
      int stray = 0;
      for (int y = rows.y0; y < rows.y1; ++y)
        for (int x = 0; x < kW; ++x) {
          if (g_a[y * kW + x] == g_b[y * kW + x]) continue;
          bool in = false;
          for (int i = 0; i < n; ++i) in = in || (x >= rs[i].x0 && x < rs[i].x1 && y >= rs[i].y0 && y < rs[i].y1);
          if (!in) ++stray;
        }
      CHECK(stray == 0, "%s tick %d: %d changed splash pixels outside the dirty rects (%d rects)", who, tick, stray, n);
    }
  }
}

// What the two draws read: bike or splash, drawn alone on a background.
struct Subject {
  const BikeState* b;
  const SplashState* s;
};
static void drawSubject(const Subject& j, Surface& s, const Rect& clip, const Palette& pal) {
  if (j.b) bikeDraw(*j.b, s, clip, pal);
  if (j.s) splashDraw(*j.s, s, clip, pal);
}
static Rect boundsSubject(const Subject& j) {
  if (j.b) return bikeBounds(*j.b);
  return splashBounds(*j.s);
}

static void checkDraw(const Subject& j, const Palette& pal, const char* who, int tick, uint32_t salt) {
  const Rect b = boundsSubject(j);
  // 1. The whole-frame draw touches nothing outside the bounds.
  memcpy(g_a, g_noise, sizeof g_a);
  Surface full = frameSurface(g_a);
  drawSubject(j, full, kFrame, pal);
  const Rect d = diffBox(g_a, g_noise, kFrame);
  CHECK(d.empty() || inside(b, d), "%s tick %d: pixels outside the bounds: drawn %d,%d..%d,%d bounds %d,%d..%d,%d", who, tick, d.x0,
        d.y0, d.x1, d.y1, b.x0, b.y0, b.x1, b.y1);
  // 2. Strip draws equal the frame draw.
  memcpy(g_b, g_noise, sizeof g_b);
  for (int sy = 0; sy < SS_STRIP_COUNT; ++sy) {
    Surface strip{g_b + (size_t)sy * SS_STRIP_ROWS * kW, kW, SS_STRIP_ROWS, kW, sy * SS_STRIP_ROWS};
    drawSubject(j, strip, Rect{0, sy * SS_STRIP_ROWS, kW, (sy + 1) * SS_STRIP_ROWS}, pal);
  }
  CHECK(memcmp(g_a, g_b, sizeof g_a) == 0, "%s tick %d: strip draws differ from the frame draw", who, tick);
  // 3. A clip, even one that cuts across it, is respected.
  uint32_t h = salt * 2654435761u + (uint32_t)tick * 40503u + 1u;
  for (int c = 0; c < 2; ++c) {
    h = h * 1664525u + 1013904223u;
    const int x0 = (int)((h >> 8) % 760u), y0 = (int)((h >> 18) % 440u);
    h = h * 1664525u + 1013904223u;
    Rect clip{x0, y0, x0 + 20 + (int)((h >> 8) % 300u), y0 + 10 + (int)((h >> 20) % 150u)};
    if (c == 1 && !b.empty()) clip = Rect{(b.x0 + b.x1) / 2 - 15, (b.y0 + b.y1) / 2 - 20, (b.x0 + b.x1) / 2 + 40, (b.y0 + b.y1) / 2 + 20};
    memcpy(g_c, g_noise, sizeof g_c);
    Surface s = frameSurface(g_c);
    drawSubject(j, s, clip, pal);
    const Rect dc = diffBox(g_c, g_noise, kFrame);
    CHECK(dc.empty() || inside(clip.clipped(kFrame), dc), "%s tick %d: draw outside clip %d,%d..%d,%d: %d,%d..%d,%d", who, tick, clip.x0,
          clip.y0, clip.x1, clip.y1, dc.x0, dc.y0, dc.x1, dc.y1);
  }
}

// ---- the splash droplets: where each lands --------------------------------------------------------------

static const float kBitG = 640.f;   // bike.cpp's kBitGravity (kept in step)

struct SplashAudit {
  long checked, landed_in, off_water;
  long worst_y;   // lowest row any droplet reached
};

// Before a step: for each droplet that is about to land this tick, is it in the stream?
static void auditBits(const SplashState& sp, float dt, SplashAudit& au, const char* who, int tick) {
  for (int i = 0; i < SPLASH_BITS; ++i) {
    const SplashBit& t = sp.bit[i];
    if (t.kind == 0) continue;
    CHECK(finiteF(t.x) && finiteF(t.y), "%s tick %d: droplet %d not finite", who, tick, i);
    CHECK(t.y < (float)kH, "%s tick %d: droplet %d below the frame (y %.1f)", who, tick, i, (double)t.y);
    if ((long)t.y > au.worst_y) au.worst_y = (long)t.y;
    if (t.kind != 1) continue;
    const float vy = t.vy + kBitG * dt;
    const float y = t.y + vy * dt, x = t.x + t.vx * dt;
    if (y >= t.y_land && vy > 0.f) {
      ++au.checked;
      float cy = 0.f, half = 0.f;
      const bool ok = landscapeStreamSpan(g_ls, x, cy, half) && fabsf(t.y_land - cy) <= half + 1.f;
      CHECK(ok, "%s tick %d: a droplet lands at (%.1f, %.1f), not in the stream (centre %.1f, half %.1f)", who, tick, (double)x,
            (double)t.y_land, (double)cy, (double)half);
      if (ok) ++au.landed_in;
    }
    // A droplet that lands is never below the row it was told to come down on by more than a step.
    CHECK(t.y <= t.y_land + 12.f, "%s tick %d: droplet %d far below its landing row", who, tick, i);
  }
}

// ---- 2x sheets ------------------------------------------------------------------------------------------

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
    if (x < 0 || y < 0 || x >= w() || y >= h()) return;
    unsigned char* o = rgb + ((size_t)y * (size_t)w() + (size_t)x) * 3;
    o[0] = r;
    o[1] = g;
    o[2] = b;
  }
  int originX(int i) const { return gap + (i % cols) * (tw + gap); }
  int originY(int i) const { return gap + (i / cols) * (th + gap); }
  // Rows y0.. of the frame, scaled down by 2 (box filter).
  void tileDown2(int i, const Pixel* fb, int y0) {
    const int ox = originX(i), oy = originY(i);
    for (int y = 0; y < th; ++y)
      for (int x = 0; x < tw; ++x) {
        int r = 0, g = 0, b = 0;
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx) {
            const int sy = y0 + y * 2 + dy;
            const Pixel p = sy < kH ? fb[sy * kW + x * 2 + dx] : (Pixel)0;
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

// The scene as a contact sheet shows it: landscape, then bike and hiker in the right order, then the splash.
static void drawScene(const Sim& s, const Palette& pal, const Pixel* bg, Pixel* out) {
  memcpy(out, bg, sizeof(Pixel) * (size_t)kW * kH);
  Surface surf = frameSurface(out);
  if (bikeInFront(s.b)) {
    hikerDraw(s.h, surf, pal, kFrame);
    bikeDraw(s.b, surf, kFrame, pal);
  } else {
    bikeDraw(s.b, surf, kFrame, pal);
    hikerDraw(s.h, surf, pal, kFrame);
  }
  splashDraw(s.sp, surf, kFrame, pal);
}

// ---- running a variant -------------------------------------------------------------------------------------

struct Stats {
  long ticks;
  long bike_bounds_strips, bike_dirty_strips, splash_dirty_strips, both_strips, all_strips;   // sums over ticks while a ride or splash shows
  int both_max;
  long shown_ticks;
  long wet_ticks, wet_strips;   // ticks with a wheel in the water, and strips on them (bike + splash + hiker)
  long dry_ticks, dry_strips;
};

struct Result {
  Sim s;
  Stats st;
  SplashAudit au;
  float max_x, min_x;
  int ticks;
  int frames_a;
  bool wet_seen;
  uint8_t modes_seen[BIKE_MODES];
  uint8_t riders_seen[RIDER_MODES];
  float min_y_head;   // the highest the rider's head reached
  int hit_tick;       // B: tick of the rock hit, 0 if none
  uint32_t landed0;
};

static Result g_res;

static Result* runVariant(int variant, int seconds, const char* name, Sheet* sheet, bool deep, int sheet_skip_empty, int crop_y0) {
  Result& res = g_res;
  memset(&res, 0, sizeof res);
  Sim& s = res.s;
  simInit(s, variant, 1);
  const Palette& pal = g_pal[3];
  const Pixel* bg = g_land[3];
  res.max_x = -1.0e9f;
  res.min_x = 1.0e9f;
  res.min_y_head = 1.0e9f;
  const int total = seconds * 25;
  double next_frame_tick = 0.0;
  int next_frame = 0;
  long t0_tick = 0;
  for (int tick = 0; tick <= total; ++tick) {
    const BikeState pb = s.b;
    const SplashState ps = s.sp;
    if (variant == 0) scriptA(s);
    if (tick > 0) {
      if (deep) auditBits(s.sp, kDt, res.au, name, tick);
      simStep(s);
    }
    res.modes_seen[s.b.mode] = 1;
    res.riders_seen[s.b.rider] = 1;
    if (s.b.mode != BIKE_HIDDEN && s.b.mode != BIKE_AWAY) {
      if (s.b.pivot.x > res.max_x) res.max_x = s.b.pivot.x;
      if (s.b.pivot.x < res.min_x) res.min_x = s.b.pivot.x;
    }
    if ((s.b.flags & 3u) != 0) res.wet_seen = true;
    if ((s.b.flags & 4u) && res.hit_tick == 0) res.hit_tick = tick;
    if (bikeDriving(s.b)) {
      float rad;
      const Vec2 hp = hikerHeadPos(s.h, &rad);
      if (hp.y < res.min_y_head) res.min_y_head = hp.y;
    }
    CHECK(finiteBike(s.b) && finiteSplash(s.sp) && finitePose(s.h.pose), "%s tick %d: state not finite", name, tick);

    // Dirty strips a frame, as the renderer would count them (the rider is the hiker's: his rows are marked anyway).
    {
      Rect rb[BIKE_DIRTY_MAX], rsp[SPLASH_DIRTY_MAX];
      const int nb = bikeDirty(pb, s.b, rb), ns = splashDirty(ps, s.sp, rsp);
      uint16_t mb = 0, msp = 0;
      for (int i = 0; i < nb; ++i) mb = (uint16_t)(mb | rowsMask(rb[i]));
      for (int i = 0; i < ns; ++i) msp = (uint16_t)(msp | rowsMask(rsp[i]));
      const uint16_t mw = (uint16_t)(rowsMask(bikeBounds(pb)) | rowsMask(bikeBounds(s.b)));
      const uint16_t mh = bikeDriving(s.b) ? (uint16_t)(rowsMask(hikerBounds(s.h))) : (uint16_t)0;
      const bool shown = (s.b.mode != BIKE_HIDDEN && s.b.mode != BIKE_AWAY) || splashActive(s.sp);
      if (shown) {
        ++res.st.shown_ticks;
        res.st.bike_bounds_strips += popc(mw);
        res.st.bike_dirty_strips += popc(mb);
        res.st.splash_dirty_strips += popc(msp);
        const int all = popc((uint16_t)(mb | msp | mh));
        res.st.both_strips += popc((uint16_t)(mb | msp));
        res.st.all_strips += all;
        if (popc((uint16_t)(mb | msp)) > res.st.both_max) res.st.both_max = popc((uint16_t)(mb | msp));
        if ((s.b.flags & 3u) != 0) {
          ++res.st.wet_ticks;
          res.st.wet_strips += all;
        } else {
          ++res.st.dry_ticks;
          res.st.dry_strips += all;
        }
      }
    }

    if (deep) {
      checkTick(pb, s.b, ps, s.sp, pal, bg, name, tick);
      if (tick % 4 == 0) {
        Subject jb{&s.b, nullptr}, js{nullptr, &s.sp};
        checkDraw(jb, pal, name, tick, (uint32_t)variant);
        checkDraw(js, pal, name, tick, (uint32_t)variant + 7u);
      }
    }

    if (sheet && tick == (int)(next_frame_tick + 0.5)) {
      // A frame every 0.25 s (6.25 ticks), skipping the ones where nothing is in the picture.
      const Rect bb = unite(bikeBounds(s.b), splashBounds(s.sp));
      const bool empty = bb.empty() || bb.x1 <= 0 || bb.x0 >= kW;
      next_frame_tick += 6.25;
      if (!(empty && sheet_skip_empty)) {
        drawScene(s, pal, bg, g_c);
        sheet->tileDown2(next_frame, g_c, crop_y0);
        char lab[16];
        const float t = (float)(tick - t0_tick) * kDt;
        snprintf(lab, sizeof lab, "%d.%02d", (int)t, (int)(t * 100.f + 0.5f) % 100);
        label(*sheet, next_frame, lab);
        ++next_frame;
        res.frames_a = next_frame;
      }
    }
  }
  res.ticks = total;
  return &res;
}

static void printStrips(const char* what, const Stats& st) {
  if (st.shown_ticks == 0) {
    printf("  %s: nothing shown\n", what);
    return;
  }
  const double n = (double)st.shown_ticks;
  printf("  %s: %ld ticks with the bike or its water showing; strips per frame\n", what, st.shown_ticks);
  printf("      bike alone: whole bounds %.2f avg, bikeDirty %.2f   splash alone: splashDirty %.2f   bike + splash %.2f (max %d)\n",
         (double)st.bike_bounds_strips / n, (double)st.bike_dirty_strips / n, (double)st.splash_dirty_strips / n,
         (double)st.both_strips / n, st.both_max);
  printf("      with the rider's rows too: %.2f avg overall", (double)st.all_strips / n);
  if (st.wet_ticks) printf(", %.2f while a wheel is in the water (%ld ticks)", (double)st.wet_strips / (double)st.wet_ticks, st.wet_ticks);
  if (st.dry_ticks) printf(", %.2f dry (%ld ticks)", (double)st.dry_strips / (double)st.dry_ticks, st.dry_ticks);
  printf("\n");
}

// ---- the tests ---------------------------------------------------------------------------------------------------

static float urand(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return (float)((s >> 8) & 0xFFFF) / 65536.f;
}

// The module's copy of the rig's lengths against the rig itself.
static void testRigCopy() {
  printf("the rig copy\n");
  uint32_t seed = 4242u;
  float worst = 0.f;
  for (int i = 0; i < 400; ++i) {
    Hiker h;
    hikerInit(h, Vec2{100.f + 500.f * urand(seed), 200.f + 250.f * urand(seed)}, urand(seed) < 0.5f ? 1 : -1);
    h.height = 60.f + 60.f * urand(seed);
    h.pose.lean = -1.5f + 4.f * urand(seed);
    h.pose.head = -0.6f + 1.2f * urand(seed);
    h.pose.lift = 0.1f * urand(seed);
    h.pose.dx = -0.1f + 0.2f * urand(seed);
    for (int k = 0; k < 2; ++k) {
      h.pose.thigh[k] = -1.f + 3.f * urand(seed);
      h.pose.knee[k] = 3.f * urand(seed);
      h.pose.shoulder[k] = -1.f + 5.f * urand(seed);
      h.pose.elbow[k] = 2.5f * urand(seed);
    }
    h.breath_phase = urand(seed);
    for (int w = 0; w < 2; ++w) {
      const Vec2 a = hikerHandPos(h, w), b = bikeRigHand(h, w);
      const float e = fmaxf(fabsf(a.x - b.x), fabsf(a.y - b.y));
      if (e > worst) worst = e;
      CHECK(e < 0.02f, "pose %d hand %d: hikerHandPos (%.2f, %.2f) against the module's (%.2f, %.2f)", i, w, (double)a.x, (double)a.y,
            (double)b.x, (double)b.y);
    }
  }
  printf("  400 random poses: hand positions agree to %.4f px\n", (double)worst);
}

// Hip on the saddle, hands on the grips, feet on the pedals, over a crank cycle; both ways, three styles.
static const float kHipU = 8.2f, kHipW = 47.8f, kGripU = 41.5f, kGripW = 46.6f, kBBu = 18.f, kBBw = 12.f, kCrankLen = 8.5f;

static Vec2 bikePt(const BikeState& b, float u, float w) {
  const float p = b.pitch - floorf(b.pitch);
  const float c = cosT(p), s = sinT(p), d = (float)b.dir;
  return Vec2{b.pivot.x + d * (u * b.scale * c - w * b.scale * s), b.pivot.y - (u * b.scale * s + w * b.scale * c)};
}

static void testRidingPose() {
  printf("the riding pose\n");
  float worst_hip = 0.f, worst_hand = 0.f, worst_foot = 0.f, min_knee = 9.f, max_knee = -9.f;
  for (int style = 0; style < 3; ++style) {   // ride, proud, wheelie (the last by hand: pitch and arms straight)
    for (int dir = -1; dir <= 1; dir += 2) {
      BikeState b;
      SplashState sp;
      bikeInit(b, sp, 90.f);
      BikeCourse c;
      courseC(c, dir);
      bikeSetCourse(b, c);
      bikeRide(b, dir, BIKE_VAR_PASS, style == 1);
      b.pivot.x = 300.f;
      Rng rng;
      rng.seed(1);
      for (int step = 0; step < 36; ++step) {
        b.crank = (float)step / 36.f;
        b.pivot.y = bikeLaneY(b.course, b.pivot.x);
        b.pitch = style == 2 ? 0.118f : 0.f;
        b.mode = style == 2 ? BIKE_WHEELIE : BIKE_RIDE;
        Hiker h;
        hikerInit(h, Vec2{0.f, 0.f}, 1);
        h.walk_w = 1.f;   // no breathing: the checks are about geometry
        CHECK(bikeApplyRider(b, h), "apply returns true while driving");
        CHECK(h.facing == dir, "the hiker faces the way he rides");
        const Vec2 hip = bikeRigHip(h);
        const Vec2 want = bikePt(b, kHipU, kHipW);
        const float eh = fmaxf(fabsf(hip.x - want.x), fabsf(hip.y - want.y));
        worst_hip = fmaxf(worst_hip, eh);
        CHECK(eh < 0.3f, "style %d dir %d step %d: hip %.2f,%.2f is not on the saddle %.2f,%.2f", style, dir, step, (double)hip.x, (double)hip.y,
              (double)want.x, (double)want.y);
        const Vec2 hand = hikerHandPos(h, 0), grip = bikePt(b, kGripU, kGripW);
        const float eg = fmaxf(fabsf(hand.x - grip.x), fabsf(hand.y - grip.y));
        worst_hand = fmaxf(worst_hand, eg);
        CHECK(eg < 0.4f, "style %d dir %d step %d: hand %.2f,%.2f is not on the grip %.2f,%.2f", style, dir, step, (double)hand.x, (double)hand.y,
              (double)grip.x, (double)grip.y);
        for (int k = 0; k < 2; ++k) {
          min_knee = fminf(min_knee, h.pose.knee[k]);
          max_knee = fmaxf(max_knee, h.pose.knee[k]);
        }
        // The ball of each foot over its pedal: from the pose, by the rig's own lengths.
        const float H = 90.f, d = (float)dir;
        for (int k = 0; k < 2; ++k) {
          const float psi = b.crank + 0.5f * (float)k;
          const Vec2 ped = bikePt(b, kBBu + kCrankLen * sinT(psi), kBBw + kCrankLen * cosT(psi));
          const float t = h.pose.thigh[k] / 6.28318531f, sh = (h.pose.thigh[k] - h.pose.knee[k]) / 6.28318531f;
          const float ax = hip.x + d * (sinT(t) * 0.235f * H + sinT(sh) * 0.235f * H);
          const float ay = hip.y + cosT(t) * 0.235f * H + cosT(sh) * 0.235f * H;
          const float f = 0.75f * (h.pose.thigh[k] - h.pose.knee[k]);
          const float bx = ax + d * cosT(f / 6.28318531f) * 0.055f * H, by = ay - sinT(f / 6.28318531f) * 0.055f * H;
          const float ef = fmaxf(fabsf(bx - ped.x), fabsf(by + 3.6f - ped.y));   // the ball is 3.6 px above the pedal's axis
          worst_foot = fmaxf(worst_foot, ef);
          CHECK(ef < 1.0f, "style %d dir %d step %d foot %d: ball of the foot is %.1f px from its pedal", style, dir, step, k, (double)ef);
        }
      }
    }
  }
  printf("  hip on the saddle within %.2f px, hands on the grips within %.2f px, feet on the pedals within %.2f px; knee bend %.2f..%.2f rad\n",
         (double)worst_hip, (double)worst_hand, (double)worst_foot, (double)min_knee, (double)max_knee);
  CHECK(max_knee < 2.9f && min_knee > 0.f, "no knee bends backward or past its limit");
}

// Write only the rows of the sheet that hold frames.
static void finishSheet(Sheet& sh, int frames, const char* dir, const char* name) {
  char path[512];
  snprintf(path, sizeof path, "%s%s%s", dir, dir[0] ? "/" : "", name);
  int used = (frames + sh.cols - 1) / sh.cols;
  if (used < 1) used = 1;
  if (used > sh.rows) used = sh.rows;
  Sheet cut = sh;
  cut.rows = used;
  CHECK(frames <= sh.cols * sh.rows, "%s: %d frames do not fit the sheet (%d tiles)", name, frames, sh.cols * sh.rows);
  CHECK(writePPM(path, sh.rgb, cut.w(), cut.h()), "could not write %s", path);   // the rows run on from the top, so the first `used` are contiguous
  printf("  wrote %s (%dx%d, %d frames)\n", path, cut.w(), cut.h(), frames);
  free(sh.rgb);
}

static void testVariantA(const char* dir) {
  printf("variant A: through the stream and back\n");
  Sheet sh;
  sh.init(4, 28, 400, 95);   // tiles show frame rows 290..480 at 2x: the stream and the lane are down there
  Result* r = runVariant(0, 27, "A", &sh, true, 1, 290);
  finishSheet(sh, r->frames_a, dir, "bike_a.ppm");
  CHECK(r->s.b.mode == BIKE_AWAY && r->s.ph == 2, "A ends with the second pass off the far edge (mode %d phase %d)", r->s.b.mode, r->s.ph);
  CHECK(r->max_x > 800.f, "A: the first pass left by the right edge (rear tyre reached %.0f)", (double)r->max_x);
  CHECK(r->min_x < -20.f, "A: and the way back left by the left (%.0f)", (double)r->min_x);
  CHECK(r->wet_seen, "A: a wheel was in the water");
  CHECK(r->riders_seen[RIDER_SEAT] && !r->riders_seen[RIDER_FLY] && !r->riders_seen[RIDER_FALL], "A: he stayed on the bike");
  CHECK(r->s.b.rider == RIDER_FREE, "A: the module lets the hiker go at the end");
  CHECK(bikeDone(r->s.b), "A: bikeDone");
  CHECK(r->au.checked > 40, "A: droplets landed to be checked (%ld)", r->au.checked);
  CHECK(r->au.worst_y < kH - 4, "A: no droplet fell near the bottom of the frame (lowest row %ld)", r->au.worst_y);
  printf("  droplets: %ld landings checked, all in the stream; lowest row any reached %ld; %u landed, %u faded in the air\n", r->au.checked,
         r->au.worst_y, r->s.sp.landed, r->s.sp.faded);
  printf("  the first pass leaves the frame after %.1f s; the way back (starting 1.5 s later) takes %.1f s\n", (double)r->s.a1 * (double)kDt,
         (double)(r->s.a2 - r->s.a1 - 38) * (double)kDt);
  CHECK(r->s.a1 > 25 * 8 && r->s.a1 < 25 * 14, "A: a pass takes 8 to 14 s (%.1f)", (double)r->s.a1 * (double)kDt);
  // The water drains away after the bike has gone.
  Sim& s = r->s;
  for (int i = 0; i < 25 * 3; ++i) simStep(s);
  CHECK(!splashActive(s.sp), "A: the splash is gone three seconds after the bike");
  printStrips("A", r->st);
}

static void testVariantB(const char* dir) {
  printf("variant B: the front wheel hits the rock\n");
  Sheet sh;
  sh.init(4, 16, 400, 95);
  Result* r = runVariant(1, 10, "B", &sh, true, 1, 290);
  finishSheet(sh, r->frames_a, dir, "bike_b.ppm");
  const Sim& s = r->s;
  CHECK(s.b.flags & 4u, "B: the rock was hit");
  CHECK(r->riders_seen[RIDER_FLY] && r->riders_seen[RIDER_SPLASH], "B: he flew and splashed");
  CHECK(s.b.mode == BIKE_DOWN && s.b.rider == RIDER_DOWN, "B ends with the bike down and him down (mode %d rider %d)", s.b.mode, s.b.rider);
  CHECK(bikeDone(s.b), "B: bikeDone");
  CHECK(s.b.lay > 0.95f, "B: the bike is on its side (lay %.2f)", (double)s.b.lay);
  // The bike lies against the rock: its front tyre's contact is at the rock's near face.
  const float face = s.crs.rock_x - 9.5f;
  CHECK(fabsf(s.b.mark.x + 14.5f - face) < 2.f, "B: the front tyre's leading edge is at the rock's face (contact x %.1f + 14.5, face %.1f)", (double)s.b.mark.x, (double)face);
  // He is in the water.
  float cy = 0.f, half = 0.f;
  const bool in = landscapeStreamSpan(g_ls, s.h.feet.x, cy, half) && fabsf(s.h.feet.y - cy) < half;
  CHECK(in, "B: he ends in the stream (feet %.0f,%.0f; stream centre %.0f, half %.0f)", (double)s.h.feet.x, (double)s.h.feet.y, (double)cy, (double)half);
  CHECK(s.h.feet.x > s.crs.rock_x + 20.f, "B: past the rock, over the bars (feet x %.0f, rock %.0f)", (double)s.h.feet.x, (double)s.crs.rock_x);
  float rad;
  const Vec2 head = hikerHeadPos(s.h, &rad);
  CHECK(head.y < s.h.feet.y - 10.f && head.y > s.h.feet.y - 70.f, "B: sitting up, the head above his seat (%.0f, feet %.0f)", (double)head.y, (double)s.h.feet.y);
  CHECK(r->au.checked > 20, "B: droplets landed to be checked (%ld)", r->au.checked);
  CHECK(r->au.worst_y < kH - 4, "B: no droplet below the frame (lowest row %ld)", r->au.worst_y);
  printf("  hit the rock at tick %d (%.2f s); he sits at %.0f,%.0f in the stream; droplets: %ld landings checked, lowest row %ld; %u landed, %u faded\n", r->hit_tick,
         (double)r->hit_tick * (double)kDt, (double)s.h.feet.x, (double)s.h.feet.y, r->au.checked, r->au.worst_y, s.sp.landed, s.sp.faded);
  printStrips("B", r->st);
}

static void testVariantC(const char* dir) {
  printf("variant C: the wheelie\n");
  Sheet sh;
  sh.init(4, 20, 400, 100);   // C is on the dry flat higher up and the wheelie reaches higher: rows 240..440
  Result* r = runVariant(2, 15, "C", &sh, true, 1, 240);
  finishSheet(sh, r->frames_a, dir, "bike_c.ppm");
  CHECK(r->modes_seen[BIKE_WHEELIE] && r->modes_seen[BIKE_TIP], "C: wheelie and tip seen");
  CHECK(r->riders_seen[RIDER_FALL], "C: he came off");
  CHECK(r->s.b.mode == BIKE_DOWN && r->s.b.rider == RIDER_DOWN, "C ends with the bike down and him down (mode %d rider %d)", r->s.b.mode, r->s.b.rider);
  // The picture a second after the fall is over: the ending the scene holds.
  Sim s;
  simInit(s, 2, 1);
  int done_tick = -1;
  for (int i = 0; i < 25 * 17 && (done_tick < 0 || s.tick < done_tick + 25); ++i) {
    simStep(s);
    if (done_tick < 0 && bikeDone(s.b)) done_tick = s.tick;
  }
  CHECK(done_tick > 0, "C: bikeDone becomes true (tick %d)", done_tick);
  CHECK(s.b.mode == BIKE_DOWN && s.b.rider == RIDER_DOWN, "C: down a second after bikeDone");
  CHECK(fabsf(s.b.pitch - 0.5f) < 0.02f, "C: the bike is upside down (pitch %.3f turns)", (double)s.b.pitch);
  CHECK(s.b.wrw > 0.4f && s.b.wfw > 0.4f, "C: the wheels are still spinning a second after it is done (%.2f, %.2f turns/s)", (double)s.b.wrw, (double)s.b.wfw);
  const float wr0 = s.b.wr;
  Sim t = s;
  for (int i = 0; i < 10; ++i) simStep(t);
  CHECK(fabsf(t.b.wr - wr0) > 0.01f, "C: and they are turning in the picture");
  for (int i = 0; i < 25 * 3; ++i) simStep(t);
  CHECK(t.b.wrw > 0.1f, "C: still turning three seconds on (%.2f turns/s)", (double)t.b.wrw);
  // On his back: the head and the feet on the ground, the head a little above it.
  float rad;
  const Vec2 head = hikerHeadPos(s.h, &rad);
  CHECK(fabsf(head.y - s.h.feet.y) < 24.f, "C: his head is near the ground (%.0f against %.0f)", (double)head.y, (double)s.h.feet.y);
  CHECK(head.x < s.h.feet.x - 20.f, "C: his head is behind him, his feet toward where he was going (%.0f, %.0f)", (double)head.x, (double)s.h.feet.x);
  // The bike is on top of him: its bounds cover his torso's x range and sit above his ground.
  const Rect bb = bikeBounds(s.b), hb = hikerBounds(s.h);
  CHECK(bb.x0 < hb.x1 && bb.x1 > hb.x0 && bb.y1 <= hb.y1, "C: the bike lies over him (bike %d..%d, him %d..%d)", bb.x0, bb.x1, hb.x0, hb.x1);
  CHECK(bikeInFront(s.b), "C: it is drawn over him");
  // How high the front wheel went: the wheelie's best angle.
  printf("  ended at tick %d: pitch %.3f turns, wheels %.2f turns/s, head %.0f feet %.0f\n", s.tick, (double)s.b.pitch, (double)s.b.wrw, (double)head.y,
         (double)s.h.feet.y);
  printStrips("C", r->st);
}

// The wheelie's own timeline: pop, hold for seconds, then tip slowly.
static void testWheelieTimeline() {
  printf("the wheelie's timeline\n");
  Sim s;
  simInit(s, 2, 1);
  float hold_from = -1.f, hold_to = -1.f, max_pitch = 0.f, tip_t = -1.f, tip_start = -1.f;
  float t_pop = -1.f;
  for (int i = 0; i < 25 * 14; ++i) {
    simStep(s);
    if (s.b.mode == BIKE_WHEELIE && t_pop < 0.f) t_pop = nowS(s);
    if (s.b.mode == BIKE_WHEELIE && s.b.pitch > 0.09f && hold_from < 0.f) hold_from = nowS(s);
    if (s.b.mode == BIKE_WHEELIE && s.b.pitch > 0.09f) hold_to = nowS(s);
    if (s.b.mode == BIKE_TIP && tip_start < 0.f) tip_start = nowS(s);
    if (s.b.mode == BIKE_DOWN && tip_t < 0.f) tip_t = nowS(s);
    if (s.b.pitch > max_pitch && s.b.mode != BIKE_DOWN) max_pitch = s.b.pitch;
  }
  printf("  pop at %.1f s; nose above 32 degrees from %.1f s to %.1f s (%.1f s); tips over from %.1f s; lying at %.1f s (fall %.1f s)\n", (double)t_pop,
         (double)hold_from, (double)hold_to, (double)(hold_to - hold_from), (double)tip_start, (double)tip_t, (double)(tip_t - tip_start));
  CHECK(t_pop > 0.f && hold_from > 0.f, "the wheelie began");
  CHECK(hold_to - hold_from > 3.0f, "it holds for more than 3 s (%.1f)", (double)(hold_to - hold_from));
  CHECK(tip_t - tip_start > 0.8f, "and the fall itself is not instant (%.1f s)", (double)(tip_t - tip_start));
  CHECK(max_pitch > 0.12f && max_pitch < 0.5f, "the front wheel comes up to about 45 degrees (max %.3f turns)", (double)max_pitch);
  // The speed never reverses; he keeps rolling through the hold.
  Sim u;
  simInit(u, 2, 1);
  float x_prev = -1.0e9f;
  bool mono = true;
  for (int i = 0; i < 25 * 14; ++i) {
    simStep(u);
    if (u.b.mode == BIKE_WHEELIE || u.b.mode == BIKE_RIDE) {
      if (u.b.pivot.x < x_prev - 0.001f) mono = false;
      x_prev = u.b.pivot.x;
    }
  }
  CHECK(mono, "the bike never rolls backward");
}

// Mirrored: the same endings when he rides from the right.
static void testMirrored() {
  printf("mirrored rides\n");
  for (int variant = 0; variant < 3; ++variant) {
    Sim s;
    simInit(s, variant, -1);
    const int secs = variant == 0 ? 13 : (variant == 1 ? 12 : 16);
    float max_x = -1e9f, min_x = 1e9f;
    for (int i = 0; i < 25 * secs; ++i) {
      simStep(s);
      if (s.b.mode != BIKE_AWAY && s.b.mode != BIKE_HIDDEN) {
        max_x = fmaxf(max_x, s.b.pivot.x);
        min_x = fminf(min_x, s.b.pivot.x);
      }
      CHECK(finiteBike(s.b) && finitePose(s.h.pose), "mirrored %d: finite", variant);
    }
    if (variant == 0) {
      CHECK(s.b.mode == BIKE_AWAY && min_x < -20.f, "mirrored A: rode off the left edge (%d, min x %.0f)", s.b.mode, (double)min_x);
    } else if (variant == 1) {
      CHECK(s.b.mode == BIKE_DOWN && s.b.rider == RIDER_DOWN && s.b.lay > 0.95f, "mirrored B ends down (%d %d)", s.b.mode, s.b.rider);
      float cy = 0.f, half = 0.f;
      CHECK(landscapeStreamSpan(g_ls, s.h.feet.x, cy, half) && fabsf(s.h.feet.y - cy) < half, "mirrored B: he is in the stream (feet %.0f,%.0f)", (double)s.h.feet.x,
            (double)s.h.feet.y);
      CHECK(s.h.feet.x < s.crs.rock_x - 20.f && s.h.facing == -1, "mirrored B: over the bars to the left of the rock (%.0f, rock %.0f)", (double)s.h.feet.x, (double)s.crs.rock_x);
    } else {
      CHECK(s.b.mode == BIKE_DOWN && s.b.rider == RIDER_DOWN && fabsf(s.b.pitch - 0.5f) < 0.02f, "mirrored C ends upside down (%d %d %.3f)", s.b.mode, s.b.rider, (double)s.b.pitch);
      float rad;
      const Vec2 head = hikerHeadPos(s.h, &rad);
      CHECK(head.x > s.h.feet.x + 20.f, "mirrored C: head behind him, to the right (%.0f, %.0f)", (double)head.x, (double)s.h.feet.x);
    }
  }
}

// ---- the splash on its own ----------------------------------------------------------------------------------------

static void testSplash() {
  printf("the splash\n");
  // Every droplet either lands in the stream or fades in the air; none falls below the frame; a replay is the same.
  Sim s;
  simInit(s, 1, 1);
  SplashAudit au{};
  uint32_t flew = 0;
  for (int i = 0; i < 25 * 14; ++i) {
    auditBits(s.sp, kDt, au, "splash", i);
    simStep(s);
    for (int k = 0; k < SPLASH_BITS; ++k)
      if (s.sp.bit[k].kind && s.sp.bit[k].life > 0.f && s.sp.bit[k].y == s.sp.bit[k].y) flew += 0;
  }
  // Drain.
  for (int i = 0; i < 25 * 4; ++i) {
    auditBits(s.sp, kDt, au, "splash", 1000 + i);
    simStep(s);
  }
  CHECK(!splashActive(s.sp) || s.b.mode == BIKE_DOWN, "the splash dies away");
  int live = 0;
  for (int k = 0; k < SPLASH_BITS; ++k) live += s.sp.bit[k].kind != 0;
  CHECK(live == 0, "no droplet is left in the air after four seconds (%d)", live);
  CHECK(s.sp.landed > 30, "many droplets landed in the stream (%u)", s.sp.landed);
  CHECK(s.sp.landed + s.sp.faded > 60, "and many were thrown (%u landed, %u faded)", s.sp.landed, s.sp.faded);
  printf("  B: %u droplets landed in the stream, %u faded in the air; %ld landings audited against the stream polygon\n", s.sp.landed, s.sp.faded, au.checked);
  (void)flew;

  // The first moments of a front wheel entering: the crown and a fan, within the first 0.3 s.
  Sim w;
  simInit(w, 0, 1);
  int crowns_seen = 0, bits_max = 0;
  bool was_on[SPLASH_CROWNS] = {};
  for (int i = 0; i < 25 * 8; ++i) {
    simStep(w);
    int b = 0;
    for (int k = 0; k < SPLASH_CROWNS; ++k) {
      if (w.sp.crown[k].on && !was_on[k]) ++crowns_seen;   // counted as it appears
      was_on[k] = w.sp.crown[k].on != 0;
    }
    for (int k = 0; k < SPLASH_BITS; ++k) b += w.sp.bit[k].kind != 0;
    bits_max = b > bits_max ? b : bits_max;
  }
  CHECK(crowns_seen >= 2, "a crown at each wheel on the way in (%d)", crowns_seen);
  CHECK(bits_max >= 20, "a big fan of droplets (%d at once)", bits_max);
  printf("  A: up to %d droplets in the air at once, %d crowns thrown\n", bits_max, crowns_seen);
}

// ---- hidden, free, hand-offs -----------------------------------------------------------------------------------------------

static void testCommands() {
  printf("commands\n");
  Sim s;
  simInit(s, 0, 1);
  bikeHide(s.b);
  CHECK(bikeBounds(s.b).empty(), "hidden: no bounds");
  CHECK(!bikeDriving(s.b), "hidden: not driving him");
  Hiker h = s.h;
  const Hiker h0 = h;
  CHECK(!bikeApplyRider(s.b, h) && memcmp(&h, &h0, sizeof h) == 0, "hidden: bikeApplyRider touches nothing");
  memcpy(g_a, g_noise, sizeof g_a);
  Surface su = frameSurface(g_a);
  bikeDraw(s.b, su, kFrame, g_pal[3]);
  splashDraw(s.sp, su, kFrame, g_pal[3]);
  CHECK(memcmp(g_a, g_noise, sizeof g_a) == 0, "hidden: draws nothing");

  // A ride begins off the starting edge, rolling.
  bikeRide(s.b, 1, BIKE_VAR_PASS, false);
  CHECK(s.b.mode == BIKE_RIDE && s.b.rider == RIDER_SEAT && s.b.speed > 50.f, "a ride starts rolling with him on the saddle");
  Rect bb = bikeBounds(s.b);
  CHECK(!bb.empty() && bb.x1 <= 0, "it starts wholly off the left edge (bounds %d..%d)", bb.x0, bb.x1);
  bikeRide(s.b, -1, BIKE_VAR_PASS, false);
  bb = bikeBounds(s.b);
  CHECK(!bb.empty() && bb.x0 >= kW, "from the right it starts wholly off the right edge (bounds %d..%d)", bb.x0, bb.x1);

  // bikeFreeRider: the bike stays, the hiker is the scene's.
  bikeRide(s.b, 1, BIKE_VAR_WHEELIE, false);
  for (int i = 0; i < 25 * 3; ++i) simStep(s);
  bikeFreeRider(s.b);
  CHECK(!bikeDriving(s.b) && !bikeBounds(s.b).empty(), "freed: bike still shown");
  CHECK(!bikeApplyRider(s.b, h), "freed: bikeApplyRider does nothing");
  // A rock the scene shows early.
  bikeHide(s.b);
  bikeRock(s.b, true);
  CHECK(!bikeBounds(s.b).empty(), "the rock alone is drawn and bounded");
  memcpy(g_a, g_noise, sizeof g_a);
  bikeDraw(s.b, su, kFrame, g_pal[3]);
  CHECK(!diffBox(g_a, g_noise, kFrame).empty(), "the rock is drawn when asked");
  bikeHide(s.b);
  CHECK(bikeBounds(s.b).empty(), "bikeHide takes the rock away too");

  // Sizes.
  printf("  sizeof(BikeState) = %d bytes, sizeof(SplashState) = %d bytes, sizeof(BikeCourse) = %d\n", (int)sizeof(BikeState), (int)sizeof(SplashState),
         (int)sizeof(BikeCourse));
  CHECK(sizeof(BikeState) <= 512 && sizeof(SplashState) <= 2048, "states stay small");

  // The lane.
  BikeCourse c;
  courseAB(c, 1);
  CHECK(c.water_x1 > c.water_x0 + 60.f, "the fit found the water (%.0f..%.0f)", (double)c.water_x0, (double)c.water_x1);
  CHECK(c.rock_x > c.water_x0 + 10.f && c.rock_x < c.water_x1 - 10.f, "the rock is in the water (%.0f)", (double)c.rock_x);
  float cy = 0.f, half = 0.f;
  CHECK(landscapeStreamSpan(g_ls, c.rock_x, cy, half) && fabsf(bikeLaneY(c, c.rock_x) - cy) < half, "and on the lane inside the stream");
  CHECK(bikeLaneY(c, -500.f) == c.y[0] && bikeLaneY(c, 5000.f) == c.y[c.n - 1], "the lane is flat beyond its ends");
  printf("  the A/B lane crosses the stream from x=%.0f to x=%.0f (%.0f px); rock at x=%.0f\n", (double)c.water_x0, (double)c.water_x1, (double)(c.water_x1 - c.water_x0),
         (double)c.rock_x);
}

// ---- long runs, bad input, replay --------------------------------------------------------------------------------------------

static uint32_t g_lcg;
static uint32_t rnd() {
  g_lcg = g_lcg * 1664525u + 1013904223u;
  return g_lcg >> 8;
}
static float rndf() { return (float)(rnd() & 0xFFFF) / 65536.f; }

static uint32_t stressRun(int ticks, bool bad_input, int* worst_rects, int* worst_splash) {
  g_lcg = 20260211u;
  BikeState b;
  SplashState sp;
  bikeInit(b, sp, 90.f);
  BikeCourse c;
  courseAB(c, 1);
  bikeSetCourse(b, c);
  Rng rng;
  rng.seed(5);
  Hiker h;
  hikerInit(h, Vec2{300.f, 360.f}, 1);
  uint32_t hash = 2166136261u;
  *worst_rects = 0;
  *worst_splash = 0;
  for (int t = 0; t < ticks; ++t) {
    if (rnd() % 150u == 0) {
      switch (rnd() % 10u) {
        case 0: bikeRide(b, 1, BIKE_VAR_PASS, false); break;
        case 1: bikeRide(b, -1, BIKE_VAR_PASS, true); break;
        case 2: bikeRide(b, (rnd() & 1u) ? 1 : -1, BIKE_VAR_ROCK, false); break;
        case 3: bikeRide(b, (rnd() & 1u) ? 1 : -1, BIKE_VAR_WHEELIE, false); break;
        case 4: bikeFreeRider(b); break;
        case 5: bikeHide(b); break;
        case 6: bikeRock(b, (rnd() & 1u) != 0); break;
        case 7: {
          BikeCourse cc;
          if (rnd() & 1u) courseC(cc, (rnd() & 1u) ? 1 : -1);
          else courseAB(cc, (rnd() & 1u) ? 1 : -1);
          bikeSetCourse(b, cc);
          break;
        }
        case 8: bikeInit(b, sp, 60.f + 60.f * rndf()); bikeSetCourse(b, c); break;
        default: bikeRide(b, 1, BIKE_VAR_ROCK, false); break;
      }
    }
    float dt = kDt * (0.5f + 2.f * rndf());
    if (bad_input) {
      const uint32_t r = rnd() % 200u;
      if (r == 0) dt = NAN;
      else if (r == 1) dt = INFINITY;
      else if (r == 2) dt = -1.f;
      else if (r == 3) dt = 50.f;
      else if (r == 4) dt = 0.f;
    }
    const BikeState pb = b;
    const SplashState ps = sp;
    hikerTick(h, kDt);
    bikeStep(b, sp, rng, dt);
    if (bikeDriving(b)) bikeApplyRider(b, h);
    if (!finiteBike(b) || !finiteSplash(sp) || !finitePose(h.pose) || !finiteF(h.feet.x) || !finiteF(h.feet.y)) {
      CHECK(false, "tick %d: state not finite (mode %d rider %d)", t, b.mode, b.rider);
      return 0;
    }
    Rect rs[BIKE_DIRTY_MAX], rsp[SPLASH_DIRTY_MAX];
    const int n = bikeDirty(pb, b, rs), ns = splashDirty(ps, sp, rsp);
    if (n > *worst_rects) *worst_rects = n;
    if (ns > *worst_splash) *worst_splash = ns;
    const Rect bb = bikeBounds(b);
    // (The rock can lie far from the bike, so the width is the frame's and a bit.)
    if (!bb.empty() && (bb.x1 - bb.x0 > 1300 || bb.y1 - bb.y0 > 400)) {
      CHECK(false, "tick %d: absurd bounds %d,%d..%d,%d (mode %d)", t, bb.x0, bb.y0, bb.x1, bb.y1, b.mode);
      return 0;
    }
    if (fabsf(b.pivot.x) > 2000.f || fabsf(b.pivot.y) > 2000.f || fabsf(b.hip.x) > 4000.f || fabsf(b.hip.y) > 4000.f) {
      CHECK(false, "tick %d: ran away (%.0f, %.0f) hip (%.0f, %.0f)", t, (double)b.pivot.x, (double)b.pivot.y, (double)b.hip.x, (double)b.hip.y);
      return 0;
    }
    for (int k = 0; k < SPLASH_BITS; ++k)
      if (sp.bit[k].kind && sp.bit[k].y >= (float)kH) {
        CHECK(false, "tick %d: droplet below the frame (%.0f)", t, (double)sp.bit[k].y);
        return 0;
      }
    if (t % 400 == 0) {
      Subject jb{&b, nullptr}, js{nullptr, &sp};
      for (const Subject* j : {&jb, &js}) {
        memcpy(g_a, g_noise, sizeof g_a);
        Surface su = frameSurface(g_a);
        drawSubject(*j, su, kFrame, g_pal[3]);
        const Rect d = diffBox(g_a, g_noise, kFrame);
        const Rect bd = boundsSubject(*j);
        if (!(d.empty() || inside(bd, d))) {
          CHECK(false, "tick %d: drawn outside the bounds (mode %d)", t, b.mode);
          return 0;
        }
      }
    }
    const unsigned char* bytes = (const unsigned char*)&b.pivot;
    for (int i = 0; i < 8; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    hash = (hash ^ b.mode) * 16777619u;
    hash = (hash ^ (uint32_t)(h.feet.x * 16.f)) * 16777619u;
    hash = (hash ^ sp.landed) * 16777619u;
  }
  return hash;
}

static void testLongRuns() {
  printf("long runs\n");
  int worst = 0, worst_sp = 0;
  const uint32_t h1 = stressRun(120000, false, &worst, &worst_sp);
  printf("  120000 random ticks: at most %d bike dirty rects, %d splash dirty rects\n", worst, worst_sp);
  CHECK(h1 != 0, "stress run completed");
  int w2 = 0, s2 = 0;
  const uint32_t h2 = stressRun(120000, false, &w2, &s2);
  CHECK(h1 == h2 && worst == w2 && worst_sp == s2, "the same run twice gives the same bike (%08x, %08x)", h1, h2);
  int w3 = 0, s3 = 0;
  const uint32_t h3 = stressRun(60000, true, &w3, &s3);
  CHECK(h3 != 0, "NaN, infinity and silly step sizes in: still finite and bounded");

  // A replay from the same commands is the same, byte for byte (no hidden state, no clock).
  Sim a, b;
  simInit(a, 1, 1);
  simInit(b, 1, 1);
  for (int i = 0; i < 25 * 12; ++i) {
    simStep(a);
    simStep(b);
  }
  CHECK(memcmp(&a.b, &b.b, sizeof a.b) == 0 && memcmp(&a.sp, &b.sp, sizeof a.sp) == 0, "two identical runs end in identical bytes");
  // The same pose twice from the same state.
  Hiker h1p = a.h, h2p = a.h;
  bikeApplyRider(a.b, h1p);
  bikeApplyRider(a.b, h2p);
  CHECK(memcmp(&h1p, &h2p, sizeof h1p) == 0, "bikeApplyRider is a pure function of the state");
  // A different Rng gives different water but the same bike.
  Sim c1, c2;
  simInit(c1, 0, 1);
  simInit(c2, 0, 1);
  c2.rng.seed(777);
  for (int i = 0; i < 25 * 7; ++i) {
    simStep(c1);
    simStep(c2);
  }
  CHECK(c1.b.pivot.x == c2.b.pivot.x && c1.b.mode == c2.b.mode, "the Rng changes only the water, not the bike");
  CHECK(memcmp(&c1.sp, &c2.sp, sizeof c1.sp) != 0, "and the water differs with the Rng");
  // Half rate: the bike stepped every second tick with twice the dt ends the same way.
  Sim f, g;
  simInit(f, 0, 1);
  simInit(g, 0, 1);
  for (int i = 0; i < 25 * 8; ++i) simStep(f);
  for (int i = 0; i < 25 * 8 / 2; ++i) {
    hikerTick(g.h, 2.f * kDt);
    bikeStep(g.b, g.sp, g.rng, 2.f * kDt);
    if (bikeDriving(g.b)) bikeApplyRider(g.b, g.h);
  }
  CHECK(fabsf(f.b.pivot.x - g.b.pivot.x) < 8.f && f.b.mode == g.b.mode, "at 12.5 Hz the bike arrives in the same place (%.0f against %.0f)", (double)f.b.pivot.x, (double)g.b.pivot.x);
  // dt 0 changes no pixel.
  Sim z;
  simInit(z, 0, 1);
  for (int i = 0; i < 25 * 5; ++i) simStep(z);
  const BikeState before = z.b;
  bikeStep(z.b, z.sp, z.rng, 0.f);
  CHECK(bikeSame(before, z.b), "a zero dt draws the same bike");
}

// ---- how it reads, every mood -----------------------------------------------------------------------------------------------

static float lum(Pixel p) { return (0.2126f * (float)red8(p) + 0.7152f * (float)green8(p) + 0.0722f * (float)blue8(p)) / 255.f; }

static void runTo(Sim& s, float secs) {
  const int n = (int)(secs * 25.f + 0.5f);
  while (s.tick < n) simStep(s);
}

static void centreCrop(const Sim& s, int zw, int zh, Rect& crop) {
  Rect u = unite(unite(bikeBounds(s.b), hikerBounds(s.h)), splashBounds(s.sp));
  if (u.empty()) u = Rect{0, 0, zw, zh};
  int x0 = (u.x0 + u.x1) / 2 - zw / 2, y0 = (u.y0 + u.y1) / 2 - zh / 2;
  if (y0 + zh > kH) y0 = kH - zh;
  crop = Rect{x0, y0, x0 + zw, y0 + zh};
}

static void testMoods(const char* dir) {
  printf("moods\n");
  for (int m = 0; m < kMoods; ++m) {
    const Palette& p = g_pal[m];
    const float sky = lum(p[SKY2]), frame = lum(p[MUSHROOM]), ink = lum(p[ACTOR]), spoke = lum(p[ACTOR_LIGHT]), ground = lum(p[GROUND]),
                water = lum(p[WATER]), foam = lum(p[WATER_LIGHT]);
    printf("  %-8s frame %.2f  ink %.2f  spokes %.2f | ground %.2f, water %.2f, foam %.2f | frame against ground %.2f, ink against ground %.2f, foam against water %.2f\n",
           kMoodName[m], (double)frame, (double)ink, (double)spoke, (double)ground, (double)water, (double)foam, (double)fabsf(frame - ground), (double)fabsf(ink - ground),
           (double)fabsf(foam - water));
    (void)sky;
    CHECK(fabsf(ink - water) > 0.08f, "%s: the tyres and rider show against the water", kMoodName[m]);
    CHECK(fabsf(foam - water) > 0.02f, "%s: spray shows against the water", kMoodName[m]);
    if (m == 0) {
      CHECK(frame <= NIGHT_LUM_MAX && foam <= NIGHT_LUM_MAX && spoke <= NIGHT_HIGHLIGHT_MAX, "night: within the night caps");
    }
  }

  // The zoom sheet: 4 across, 5 down, each 3x.
  const int zw = 160, zh = 100, z = 3, cols = 4, rows = 5;
  Sheet sh;
  sh.init(cols, rows, zw * z, zh * z);
  int tile = 0;
  Rect crop;
  const Palette& pal = g_pal[3];
  const Pixel* bg = g_land[3];

  // Rows 1 and 2: eight phases of one pedal cycle, riding along the dry flat.
  {
    Sim s;
    simInit(s, 2, 1);
    runTo(s, 2.2f);
    for (int i = 0; i < 8; ++i) {
      // Hold the bike still and turn its cranks through half a turn in eighths (the other half is the same with his legs swapped).
      s.b.crank = (float)i / 16.f;
      bikeApplyRider(s.b, s.h);
      drawScene(s, pal, bg, g_c);
      if (i == 0) centreCrop(s, zw, zh, crop);
      sh.tileUp(tile++, g_c, crop, z);
    }
  }
  // Row 3: the wheelie: the crouch, the pop, the hold, the tip.
  {
    Sim s;
    simInit(s, 2, 1);
    const float t_pop = 170.f / 92.f + 0.6f;   // about when it begins
    const float times[4] = {0.f, 0.f, 0.f, 0.f};
    (void)times;
    int got = 0;
    float want[4] = {0.15f, 1.0f, 2.4f, 5.4f};   // seconds after the pop begins
    float pop_at = -1.f;
    for (int i = 0; i < 25 * 14 && got < 4; ++i) {
      simStep(s);
      if (s.b.mode == BIKE_WHEELIE && pop_at < 0.f) pop_at = nowS(s);
      if (pop_at >= 0.f && nowS(s) - pop_at >= want[got]) {
        drawScene(s, pal, bg, g_c);
        centreCrop(s, zw, zh, crop);
        sh.tileUp(tile++, g_c, crop, z);
        ++got;
      }
    }
    (void)t_pop;
    while (got++ < 4) ++tile;
  }
  // Row 4: A's way back (proud), the rock hit, the dive, the belly-flop.
  {
    Sim s;
    simInit(s, 0, -1);
    s.b.proud = 0;
    bikeRide(s.b, -1, BIKE_VAR_PASS, true);
    runTo(s, 2.0f);
    drawScene(s, pal, bg, g_c);
    centreCrop(s, zw, zh, crop);
    sh.tileUp(tile++, g_c, crop, z);
    Sim r;
    simInit(r, 1, 1);
    bool shot[3] = {false, false, false};
    int hit = -1;
    for (int i = 0; i < 25 * 10; ++i) {
      simStep(r);
      if ((r.b.flags & 4u) && hit < 0) hit = r.tick;
      if (hit >= 0) {
        const float ts = (float)(r.tick - hit) * kDt;
        if (!shot[0] && ts >= 0.08f) {
          shot[0] = true;
          drawScene(r, pal, bg, g_c);
          centreCrop(r, zw, zh, crop);
          sh.tileUp(tile++, g_c, crop, z);
        }
        if (!shot[1] && ts >= 0.42f) {
          shot[1] = true;
          drawScene(r, pal, bg, g_c);
          centreCrop(r, zw, zh, crop);
          sh.tileUp(tile++, g_c, crop, z);
        }
        if (!shot[2] && ts >= 0.85f) {
          shot[2] = true;
          drawScene(r, pal, bg, g_c);
          centreCrop(r, zw, zh, crop);
          sh.tileUp(tile++, g_c, crop, z);
        }
      }
    }
  }
  // Row 5: sitting in the water, the fall in C, C at rest, and the bike at dusk riding through the stream.
  {
    Sim r;
    simInit(r, 1, 1);
    runTo(r, 9.f);
    drawScene(r, pal, bg, g_c);
    {   // him sitting in the stream, with the bike lying behind him: the crop is centred between the two, nearer him
      const Rect hb = hikerBounds(r.h), bk = bikeBounds(r.b);
      const int cx = (hb.x0 + hb.x1) / 2 + ((bk.x0 + bk.x1) / 2 - (hb.x0 + hb.x1) / 2) / 3;
      int y0 = (hb.y0 + hb.y1) / 2 - zh / 2;
      if (y0 + zh > kH) y0 = kH - zh;
      crop = Rect{cx - zw / 2, y0, cx - zw / 2 + zw, y0 + zh};
    }
    sh.tileUp(tile++, g_c, crop, z);
    Sim c;
    simInit(c, 2, 1);
    int tip = -1;
    for (int i = 0; i < 25 * 16; ++i) {
      simStep(c);
      if (c.b.mode == BIKE_TIP && tip < 0) tip = c.tick;
      if (tip >= 0 && c.tick - tip == 15) {
        drawScene(c, pal, bg, g_c);
        centreCrop(c, zw, zh, crop);
        sh.tileUp(tile++, g_c, crop, z);
      }
    }
    drawScene(c, pal, bg, g_c);
    centreCrop(c, zw, zh, crop);
    sh.tileUp(tile++, g_c, crop, z);
    Sim d;
    simInit(d, 0, 1);
    runTo(d, 5.6f);
    drawScene(d, g_pal[5], g_land[5], g_c);
    centreCrop(d, zw, zh, crop);
    sh.tileUp(tile++, g_c, crop, z);
  }
  char path[512];
  snprintf(path, sizeof path, "%s%sbike_zoom.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d): rows 1-2 one pedal cycle in eighths, row 3 the wheelie (crouch, pop, hold, tip), row 4 A's proud way back and B's rock hit,\n"
         "  dive and belly-flop, row 5 B sitting in the stream, C mid-fall and at rest, A at dusk\n",
         path, sh.w(), sh.h());
  free(sh.rgb);
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);   // a crash still shows how far it got
  g_scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(g_scratch);
  const char* dir = argc > 1 ? argv[1] : "";
  fillNoise(g_noise);
  for (int i = 0; i < kW * kH; ++i) g_flat[i] = rgb565_hex(0x6E9FB8);
  buildLandscapes();

  printf("bike: sizeof(BikeState) %d bytes, sizeof(SplashState) %d bytes; wheels %.0f px, wheelbase %.0f px\n", (int)sizeof(BikeState), (int)sizeof(SplashState),
         (double)kBikeWheelR, (double)kBikeWheelbase);
  testRigCopy();
  testRidingPose();
  testCommands();
  testVariantA(dir);
  testVariantB(dir);
  testVariantC(dir);
  testWheelieTimeline();
  testMirrored();
  testSplash();
  testLongRuns();
  testMoods(dir);

  printf("%d checks, %d failed\n", g_checks, g_fail);
  free(g_scratch);
  return g_fail ? 1 : 0;
}
