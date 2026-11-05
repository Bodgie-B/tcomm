// Tests for core/scene/archery: the three scripted variants of the plan (A two
// misses then a bullseye and a proud pose, B an arrow over the target into the
// pine, C a bullseye and a second arrow that splits the first) driven by a real
// hiker rig over a real landscape; where each arrow ends; the archer's pose
// (bow arm out, string hand to the cheek, mirrored); bounds that cover every
// pixel drawn; clipping (a strip draw equals the frame draw and touches nothing
// outside the clip); archeryDirty's contract (covers every changed pixel); no
// NaN or runaway over long random runs, bad inputs and a replay; the dirty
// strips an archer costs; and how it reads in every mood (night included).
// Plain executable; returns non-zero on failure.
//
// Writes inspection sheets to the directory in argv[1] (default: the current
// directory): archery_a.ppm, archery_b.ppm and archery_c.ppm (a frame every
// 0.25 s from 1 s before each release to 1.75 s after it, 2x downscaled, 4
// across, the seconds from the release in each corner) and archery_zoom.ppm (the
// archer at 3x through raise, draw, hold, release, follow-through, proud and
// shrug; and the target with arrows in it; by day and at night). Convert to PNG
// for review (the scratch folder's ppm2png.py).
#include "anim/ease.h"
#include "anim/rng.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/archery.h"
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
static const float kGround = 352.f;   // his feet
static const float kLane = 346.f;     // the target's feet and the ground the arrows come down on

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
static float dist2(Vec2 a, Vec2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

static bool finiteArcher(const ArcheryState& a) {
  const float f[] = {a.t, a.aim, a.draw, a.flick, a.buzz, a.shake, a.proud, a.slump, a.elev, a.hold_len, a.ph_a, a.ph_b,
                     a.target.x, a.target.y, a.trunk.x, a.trunk.y, a.ground_y, a.bow_hand.x, a.bow_hand.y,
                     a.string_hand.x, a.string_hand.y};
  for (float v : f)
    if (!finiteF(v)) return false;
  for (int i = 0; i < ARCHERY_ARROWS; ++i) {
    const ArcheryArrow& r = a.arrows[i];
    const float g[] = {r.t, r.flight, r.arc, r.slide, r.amp, r.split_t, r.spread, r.from.x, r.from.y, r.to.x, r.to.y,
                       r.dir_end.x, r.dir_end.y, r.tip.x, r.tip.y, r.dir.x, r.dir.y};
    for (float v : g)
      if (!finiteF(v)) return false;
  }
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

// The static landscape of the loop for each mood, without actors.
static const int kMoods = 6;
static const char* const kMoodName[kMoods] = {"night", "dawn", "morning", "day", "golden", "dusk"};
static Pixel g_land[kMoods][SS_WIDTH * SS_HEIGHT];
static Palette g_pal[kMoods];
static Landscape g_ls;

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

// Draw the archery over `bg` into `out`, only inside `region` (full-width rows, so
// the rasterizer's rows match a whole-frame draw exactly).
static void renderRegion(const ArcheryState& a, const Palette& pal, const Pixel* bg, const Rect& region, Pixel* out) {
  for (int y = region.y0; y < region.y1; ++y) memcpy(out + (size_t)y * kW, bg + (size_t)y * kW, sizeof(Pixel) * kW);
  Surface s = frameSurface(out);
  archeryDraw(a, s, region, pal);
}

// ---- the scene a variant runs in -------------------------------------------------------------

// The archer's side. dir +1: he stands at x 240 facing right, the target at 470, the camp pine's
// trunk (its left face) at about 556. dir -1: the same, mirrored about the middle of the frame.
struct Layout {
  int dir;
  float archer_x, target_x;
  Vec2 trunk;
};
static Layout layoutFor(int dir) {
  Layout l;
  l.dir = dir;
  const float tx = (float)g_ls.camp_x - 3.5f;   // a pixel inside the trunk's left face (it is 9 px wide)
  if (dir > 0) {
    l.archer_x = 240.f;
    l.target_x = 470.f;
    l.trunk = Vec2{tx, 330.f};
  } else {
    l.archer_x = (float)kW - 240.f;
    l.target_x = (float)kW - 470.f;
    l.trunk = Vec2{(float)kW - tx, 330.f};
  }
  return l;
}

struct Sim {
  Hiker h;
  ArcheryState a;
  Layout lay;
  int tick;
};

static void simInit(Sim& s, int dir) {
  memset(&s, 0, sizeof s);
  s.lay = layoutFor(dir);
  hikerInit(s.h, Vec2{s.lay.archer_x, kGround}, dir);
  hikerSetPose(s.h, POSE_STAND, 0.f);
  hikerTick(s.h, 0.f);
  archeryInit(s.a, dir, Vec2{s.lay.target_x, kLane}, s.lay.trunk);
  archeryStep(s.a, hikerHandPos(s.h, 0), hikerHandPos(s.h, 1), 0.f);   // (the hands, before anything is drawn)
}

static void simTick(Sim& s) {
  hikerTick(s.h, kDt);
  archeryPose(s.a, s.h.pose);
  archeryStep(s.a, hikerHandPos(s.h, 0), hikerHandPos(s.h, 1), kDt);
  ++s.tick;
}

static void drawScene(const Sim& s, const Palette& pal, const Pixel* bg, Pixel* out) {
  memcpy(out, bg, sizeof(Pixel) * (size_t)kW * kH);
  Surface surf = frameSurface(out);
  hikerDraw(s.h, surf, pal, kFrame);
  archeryDraw(s.a, surf, kFrame, pal);
}

static bool sameBytes(const ArcheryState& a, const ArcheryState& b) { return memcmp(&a, &b, sizeof a) == 0; }

// ---- per-tick checks ------------------------------------------------------------------------

static void checkTick(const ArcheryState& prev, const ArcheryState& cur, const Palette& pal, const Pixel* bg,
                      const char* who, int tick) {
  CHECK(finiteArcher(cur), "%s tick %d: state not finite", who, tick);
  Rect rs[ARCHERY_DIRTY_MAX];
  const int n = archeryDirty(prev, cur, rs);
  CHECK(n >= 0 && n <= ARCHERY_DIRTY_MAX, "%s tick %d: %d dirty rects", who, tick, n);
  const bool same = archerySame(prev, cur);
  CHECK(same == (n == 0), "%s tick %d: archerySame %d but %d dirty rects", who, tick, same, n);
  const Rect reg0 = unite(archeryBounds(prev), archeryBounds(cur));
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

// Bounds, clipping, strips and the parts, for one state, over the noise background.
static void checkDraw(const ArcheryState& a, const Palette& pal, const char* who, int tick, uint32_t salt) {
  const Rect b = archeryBounds(a);
  // 1. The whole-frame draw touches nothing outside the bounds.
  memcpy(g_a, g_noise, sizeof g_a);
  Surface full = frameSurface(g_a);
  archeryDraw(a, full, kFrame, pal);
  const Rect d = diffBox(g_a, g_noise, kFrame);
  CHECK(d.empty() || inside(b, d), "%s tick %d: pixels outside the bounds: drawn %d,%d..%d,%d bounds %d,%d..%d,%d", who,
        tick, d.x0, d.y0, d.x1, d.y1, b.x0, b.y0, b.x1, b.y1);
  CHECK(!b.empty() || d.empty(), "%s tick %d: pixels drawn with empty bounds", who, tick);

  // 1b. Each part stays inside its own bounds.
  {
    struct Part { Rect bounds; int which; };
    const Part parts[3] = {{archeryTargetBounds(a), 0}, {archeryBowBounds(a), 1}, {Rect{0, 0, 0, 0}, 2}};
    for (const Part& p : parts) {
      Rect pb = p.bounds;
      memcpy(g_c, g_noise, sizeof g_c);
      Surface s = frameSurface(g_c);
      if (p.which == 0) archeryTargetDraw(a, s, kFrame, pal);
      else if (p.which == 1) archeryBowDraw(a, s, kFrame, pal);
      else {
        for (int i = 0; i < ARCHERY_ARROWS; ++i) pb = unite(pb, archeryArrowBounds(a, i));
        archeryArrowsDraw(a, s, kFrame, pal, true);
        archeryArrowsDraw(a, s, kFrame, pal, false);
      }
      const Rect dp = diffBox(g_c, g_noise, kFrame);
      CHECK(dp.empty() || inside(pb, dp), "%s tick %d: part %d draws outside its bounds", who, tick, p.which);
    }
  }

  // 2. Strip draws equal the frame draw.
  memcpy(g_b, g_noise, sizeof g_b);
  for (int sy = 0; sy < SS_STRIP_COUNT; ++sy) {
    Surface strip{g_b + (size_t)sy * SS_STRIP_ROWS * kW, kW, SS_STRIP_ROWS, kW, sy * SS_STRIP_ROWS};
    archeryDraw(a, strip, Rect{0, sy * SS_STRIP_ROWS, kW, (sy + 1) * SS_STRIP_ROWS}, pal);
  }
  CHECK(memcmp(g_a, g_b, sizeof g_a) == 0, "%s tick %d: strip draws differ from the frame draw", who, tick);

  // 3. A clip, even one that cuts across the archery, is respected.
  uint32_t h = salt * 2654435761u + (uint32_t)tick * 40503u + 1u;
  for (int c = 0; c < 2; ++c) {
    h = h * 1664525u + 1013904223u;
    const int x0 = (int)((h >> 8) % 760u), y0 = (int)((h >> 18) % 440u);
    h = h * 1664525u + 1013904223u;
    Rect clip{x0, y0, x0 + 20 + (int)((h >> 8) % 300u), y0 + 10 + (int)((h >> 20) % 150u)};
    if (c == 1 && !b.empty()) {   // a cut through the middle
      clip = Rect{(b.x0 + b.x1) / 2 - 15, (b.y0 + b.y1) / 2 - 20, (b.x0 + b.x1) / 2 + 40, (b.y0 + b.y1) / 2 + 20};
    }
    memcpy(g_c, g_noise, sizeof g_c);
    Surface s = frameSurface(g_c);
    archeryDraw(a, s, clip, pal);
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

// A 3x5 digit font for the time labels: 0-9, the point, the minus.
static const unsigned char kDigits[12][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {0, 0, 0, 0, 2}, {0, 0, 7, 0, 0}};   // 10: the point, 11: the minus

static void label(Sheet& sh, int tile, const char* text) {
  const int ox = sh.originX(tile) + 3, oy = sh.originY(tile) + 3;
  const int n = (int)strlen(text);
  for (int y = -1; y < 11; ++y)
    for (int x = -1; x < n * 8 + 1; ++x) sh.put(ox + x, oy + y, 0, 0, 0);
  for (int c = 0; c < n; ++c) {
    const int g = text[c] == '.' ? 10 : (text[c] == '-' ? 11 : text[c] - '0');
    if (g < 0 || g > 11) continue;
    for (int y = 0; y < 5; ++y)
      for (int x = 0; x < 3; ++x)
        if (kDigits[g][y] & (4 >> x))
          for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) sh.put(ox + c * 8 + x * 2 + dx, oy + y * 2 + dy, 255, 255, 255);
  }
}

// ---- running a variant ------------------------------------------------------------------------

struct Stats {
  long ticks;
  long dirty_sum, beyond_sum;     // strips archeryDirty marks, and those outside the hiker's strips
  int dirty_max, beyond_max;
  long flight_ticks, flight_dirty;      // an arrow in the air
  long still_ticks, still_dirty;        // between shots: nothing but breathing
  long arrow_only_ticks, arrow_only_dirty;   // only arrows changed (a stuck arrow quivering, a flight): the bow did not
};

struct Result {
  Sim s;
  Stats st;
  int releases, lands, splits, dones;
  int t_rel[ARCHERY_ARROWS], t_land[ARCHERY_ARROWS];   // ticks
  uint8_t end_of[ARCHERY_ARROWS];
  float max_proud, max_slump, max_aim;
  Vec2 tree_tip;
  int ticks_done;
  uint32_t order_bits;   // bit 0: a release came before its landing, always
};

// `capture`: ticks at which to draw into a sheet tile (ascending), with labels.
struct Capture {
  int n;
  int tick[64];
  char text[64][12];
};

static Result* runVariant(int variant, int dir, int mood, int seconds, uint64_t seed, const char* name, Sheet* sheet,
                          const Capture* cap, bool deep) {
  static Result res;
  memset(&res, 0, sizeof res);
  Sim& s = res.s;
  simInit(s, dir);
  Rng rng;
  rng.seed(seed);
  archeryPlay(s.a, (uint8_t)variant, rng);
  const Palette& pal = g_pal[mood];
  const Pixel* bg = g_land[mood];
  const int total = seconds * 25;
  int next_cap = 0;
  const bool flipped = dir < 0;
  (void)flipped;
  for (int tick = 0; tick < total; ++tick) {
    const ArcheryState prev = s.a;
    simTick(s);
    const ArcheryState& cur = s.a;
    if (cur.events & AEV_RELEASED) {
      if (res.releases < ARCHERY_ARROWS) res.t_rel[res.releases] = s.tick;
      ++res.releases;
    }
    if (cur.events & AEV_LANDED) {
      if (res.lands < ARCHERY_ARROWS) {
        res.t_land[res.lands] = s.tick;
        res.end_of[res.lands] = cur.last_end;
      }
      ++res.lands;
      CHECK(res.lands <= res.releases, "%s: an arrow landed before one was released", name);
    }
    if (cur.events & AEV_SPLIT) ++res.splits;
    if (cur.events & AEV_DONE) {
      ++res.dones;
      if (!res.ticks_done) res.ticks_done = s.tick;
    }
    if (cur.proud > res.max_proud) res.max_proud = cur.proud;
    if (cur.slump > res.max_slump) res.max_slump = cur.slump;
    if (cur.aim > res.max_aim) res.max_aim = cur.aim;

    // Dirty strips a frame, as the renderer would count them.
    {
      Rect rs[ARCHERY_DIRTY_MAX];
      const int n = archeryDirty(prev, cur, rs);
      uint16_t md = 0;
      for (int i = 0; i < n; ++i) md = (uint16_t)(md | rowsMask(rs[i]));
      const uint16_t mh = rowsMask(hikerBounds(s.h));
      const int a = popc(md), c = popc((uint16_t)(md & ~mh));
      ++res.st.ticks;
      res.st.dirty_sum += a;
      res.st.beyond_sum += c;
      if (a > res.st.dirty_max) res.st.dirty_max = a;
      if (c > res.st.beyond_max) res.st.beyond_max = c;
      bool flying = false;
      for (int i = 0; i < ARCHERY_ARROWS; ++i) flying = flying || cur.arrows[i].mode == ARROW_FLY;
      if (flying) {
        ++res.st.flight_ticks;
        res.st.flight_dirty += a;
      }
      if (cur.phase == AP_PAUSE || cur.phase == AP_IDLE) {
        ++res.st.still_ticks;
        res.st.still_dirty += a;
      }
    }

    if (deep) {
      checkTick(prev, cur, pal, bg, name, s.tick);
      if (s.tick % 4 == 0) checkDraw(cur, pal, name, s.tick, (uint32_t)variant);
    } else {
      CHECK(finiteArcher(cur), "%s tick %d: state not finite", name, s.tick);
    }

    if (sheet && cap && next_cap < cap->n && s.tick == cap->tick[next_cap]) {
      drawScene(s, pal, bg, g_c);
      sheet->tileDown2(next_cap, g_c);
      label(*sheet, next_cap, cap->text[next_cap]);
      ++next_cap;
    }
  }
  return &res;
}

// A run with no drawing, only to find when the shots go: ticks of each release and landing.
static void findTimes(int variant, int dir, uint64_t seed, int seconds, int* rel, int* land, int* n) {
  Sim s;
  simInit(s, dir);
  Rng rng;
  rng.seed(seed);
  archeryPlay(s.a, (uint8_t)variant, rng);
  *n = 0;
  int nl = 0;
  for (int t = 0; t < seconds * 25; ++t) {
    simTick(s);
    if (s.a.events & AEV_RELEASED) {
      if (*n < ARCHERY_ARROWS) rel[*n] = s.tick;
      ++*n;
    }
    if (s.a.events & AEV_LANDED) {
      if (nl < ARCHERY_ARROWS) land[nl] = s.tick;
      ++nl;
    }
  }
}

static void buildCapture(Capture& cap, const int* rel, int n) {
  cap.n = 0;
  for (int i = 0; i < n; ++i)
    for (int k = -4; k <= 7; ++k) {
      if (cap.n >= 64) return;
      cap.tick[cap.n] = rel[i] + k * 25 / 4;   // every 0.25 s (6.25 ticks: 0, 6, 12, 18, 25, ...)
      const float secs = (float)k * 0.25f;
      snprintf(cap.text[cap.n], sizeof cap.text[cap.n], "%.2f", (double)secs);
      ++cap.n;
    }
}

static void printStrips(const char* what, const Stats& st) {
  const double n = (double)st.ticks;
  printf("  %s: strips per frame, archery alone (bow, target, arrows): %.2f avg, max %d; outside the hiker's strips %.2f avg, max %d\n",
         what, (double)st.dirty_sum / n, st.dirty_max, (double)st.beyond_sum / n, st.beyond_max);
  printf("      in flight %.2f avg over %ld ticks; between shots %.2f avg over %ld ticks\n",
         st.flight_ticks ? (double)st.flight_dirty / (double)st.flight_ticks : 0.0, st.flight_ticks,
         st.still_ticks ? (double)st.still_dirty / (double)st.still_ticks : 0.0, st.still_ticks);
}

static int sheetRows(int n, int cols) { return (n + cols - 1) / cols; }

static void runAndSheet(int variant, int mood, uint64_t seed, int seconds, const char* name, const char* file, const char* dir,
                        Result** out) {
  int rel[ARCHERY_ARROWS], land[ARCHERY_ARROWS], n = 0;
  findTimes(variant, 1, seed, seconds, rel, land, &n);
  Capture* cap = (Capture*)calloc(1, sizeof(Capture));
  buildCapture(*cap, rel, n);
  Sheet sh;
  sh.init(4, sheetRows(cap->n, 4), 400, 240);
  *out = runVariant(variant, 1, mood, seconds, seed, name, &sh, cap, true);
  char path[512];
  snprintf(path, sizeof path, "%s%s%s.ppm", dir, dir[0] ? "/" : "", file);
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d), %d frames\n", path, sh.w(), sh.h(), cap->n);
  free(sh.rgb);
  free(cap);
}

// ---- the variants -----------------------------------------------------------------------------

static void testVariantA(const char* dir) {
  printf("variant A: two misses, then a bullseye\n");
  Result* r;
  runAndSheet(ARCH_A, 3, 99u, 26, "A", "archery_a", dir, &r);
  const ArcheryState& a = r->s.a;
  CHECK(r->releases == 3 && r->lands == 3, "A: three shots released and landed (%d, %d)", r->releases, r->lands);
  CHECK(r->splits == 0, "A: nothing split");
  CHECK(r->dones == 1, "A: AEV_DONE raised once (%d)", r->dones);
  CHECK(archeryDone(a) && !archeryBusy(a), "A: done and idle at the end");
  CHECK(r->end_of[0] == AEND_GROUND && r->end_of[1] == AEND_PAST && r->end_of[2] == AEND_TARGET,
        "A: ends are ground, past, target (%d %d %d)", r->end_of[0], r->end_of[1], r->end_of[2]);
  const Vec2 c = archeryTargetCenter(a);
  const ArcheryArrow& g = a.arrows[0];
  const ArcheryArrow& p = a.arrows[1];
  const ArcheryArrow& t = a.arrows[2];
  CHECK(g.mode == ARROW_STUCK && g.tip.x < a.target.x - 25.f && g.tip.x > r->s.lay.archer_x + 40.f,
        "A: the first is in the ground short of the target (x %.0f, target %.0f)", (double)g.tip.x, (double)a.target.x);
  CHECK(fabsf(g.tip.y - kLane) < 0.01f, "A: and in the ground line (y %.1f)", (double)g.tip.y);
  CHECK(p.mode == ARROW_STUCK && p.tip.x > a.target.x + 30.f + p.slide - 1.f,
        "A: the second is past the target, slid on (x %.0f, target %.0f, slide %.0f)", (double)p.tip.x, (double)a.target.x, (double)p.slide);
  CHECK(p.slide > 8.f && p.tip.x > p.to.x + 8.f, "A: it slid along the ground (%.1f px)", (double)(p.tip.x - p.to.x));
  CHECK(t.mode == ARROW_STUCK && archeryRingAt(a, t.tip) == 0 && dist2(t.tip, c) < ARCHERY_R_INNER,
        "A: the third is in the inner ring (%.2f px from the centre, ring %d)", (double)dist2(t.tip, c), archeryRingAt(a, t.tip));
  CHECK(t.ring == 0 && a.last_ring == 0, "A: and says so (ring %d)", t.ring);
  CHECK(r->max_proud > 0.95f, "A: a proud pose after the bullseye (%.2f)", (double)r->max_proud);
  CHECK(r->max_slump > 0.9f, "A: a shrug after the misses (%.2f)", (double)r->max_slump);
  // The shots come in order with a good rest between them.
  CHECK(r->t_rel[1] - r->t_land[0] > 25 * 5 && r->t_rel[2] - r->t_land[1] > 25 * 5, "A: at least 5 s from a landing to the next release");
  CHECK(r->t_rel[0] > 25 * 3, "A: a long wind-up before the first release (%.1f s)", (double)r->t_rel[0] * (double)kDt);
  const float hold = a.hold_len;
  CHECK(hold >= 1.5f && hold <= 2.3f, "A: a long hold (%.2f s)", (double)hold);
  CHECK(r->max_aim > 0.999f, "A: the bow was fully raised");
  printf("  releases at %.1f, %.1f, %.1f s; done at %.1f s; the bullseye %.2f px from the centre\n",
         (double)r->t_rel[0] * (double)kDt, (double)r->t_rel[1] * (double)kDt, (double)r->t_rel[2] * (double)kDt,
         (double)r->ticks_done * (double)kDt, (double)dist2(t.tip, c));
  printStrips("A", r->st);
  // Many seeds: the landings stay where the plan says.
  int bad = 0;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    Sim s;
    simInit(s, 1);
    Rng rng;
    rng.seed(seed);
    archeryPlay(s.a, ARCH_A, rng);
    for (int i = 0; i < 25 * 30; ++i) simTick(s);
    const Vec2 cc = archeryTargetCenter(s.a);
    const bool ok = s.a.arrows[0].end == AEND_GROUND && s.a.arrows[0].tip.x < s.a.target.x - 25.f &&
                    s.a.arrows[1].end == AEND_PAST && s.a.arrows[1].tip.x > s.a.target.x + 30.f &&
                    s.a.arrows[2].end == AEND_TARGET && dist2(s.a.arrows[2].tip, cc) < ARCHERY_R_INNER && archeryDone(s.a);
    if (!ok) ++bad;
  }
  CHECK(bad == 0, "A: 40 seeds all end as planned (%d did not)", bad);
}

// The arrow's path against the target's disc, at a fine time step.
static float minTipDistToTarget(ArcheryState a, Vec2 bow, Vec2 str, int arrow) {
  float m = 1e9f;
  const Vec2 c = archeryTargetCenter(a);
  for (int i = 0; i < 2000 && a.arrows[arrow].mode == ARROW_FLY; ++i) {
    archeryStep(a, bow, str, 0.002f);
    if (a.arrows[arrow].mode != ARROW_FLY) break;
    const float d = dist2(a.arrows[arrow].tip, c);
    if (d < m) m = d;
  }
  return m;
}

static void testVariantB(const char* dir) {
  printf("variant B: over the target into the tree\n");
  Result* r;
  runAndSheet(ARCH_B, 2, 99u, 14, "B", "archery_b", dir, &r);
  const ArcheryState& a = r->s.a;
  CHECK(r->releases == 1 && r->lands == 1, "B: one shot (%d, %d)", r->releases, r->lands);
  CHECK(r->end_of[0] == AEND_TREE, "B: it ends in the tree (%d)", r->end_of[0]);
  const ArcheryArrow& w = a.arrows[0];
  CHECK(w.mode == ARROW_STUCK && dist2(w.tip, a.trunk) < 0.01f, "B: the tip is at the trunk point (%.3f px off: %.2f,%.2f vs %.2f,%.2f)",
        (double)dist2(w.tip, a.trunk), (double)w.tip.x, (double)w.tip.y, (double)a.trunk.x, (double)a.trunk.y);
  CHECK(r->max_slump > 0.95f, "B: a shrug after it (%.2f)", (double)r->max_slump);
  CHECK(archeryDone(a) && r->dones == 1, "B: done");
  CHECK(w.tip.x > a.target.x + 40.f, "B: the trunk is beyond the target");
  // It clears the target.
  {
    Sim s;
    simInit(s, 1);
    Rng rng;
    rng.seed(99u);
    archeryPlay(s.a, ARCH_B, rng);
    for (int i = 0; i < 25 * 8 && s.a.arrows[0].mode != ARROW_FLY; ++i) simTick(s);
    CHECK(s.a.arrows[0].mode == ARROW_FLY, "B: in flight");
    const float m = minTipDistToTarget(s.a, s.a.bow_hand, s.a.string_hand, 0);
    CHECK(m > kArcheryTargetR + 6.f, "B: the arrow passes the target with room to spare (%.1f px from its centre)", (double)m);
    printf("  closest the tip comes to the target's centre: %.1f px (disc radius %.0f)\n", (double)m, (double)kArcheryTargetR);
  }
  // It stays: 30 s later the arrow is exactly as it was, and nothing about it marks dirty.
  Sim s2 = r->s;
  const ArcheryArrow before = s2.a.arrows[0];
  const Rect b0 = archeryArrowBounds(s2.a, 0);
  for (int i = 0; i < 25 * 30; ++i) simTick(s2);
  const ArcheryArrow& after = s2.a.arrows[0];
  CHECK(after.mode == ARROW_STUCK && after.tip.x == before.tip.x && after.tip.y == before.tip.y && after.dir.x == before.dir.x &&
            after.dir.y == before.dir.y,
        "B: in the tree 30 s later, unmoved");
  CHECK(archeryArrowBounds(s2.a, 0).x0 == b0.x0 && archeryArrowBounds(s2.a, 0).y1 == b0.y1, "B: and the same bounds");
  printStrips("B", r->st);
  // Many seeds.
  int bad = 0;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    Sim q;
    simInit(q, 1);
    Rng rng;
    rng.seed(seed);
    archeryPlay(q.a, ARCH_B, rng);
    for (int i = 0; i < 25 * 14; ++i) simTick(q);
    if (!(q.a.arrows[0].end == AEND_TREE && dist2(q.a.arrows[0].tip, q.a.trunk) < 0.01f && archeryDone(q.a))) ++bad;
  }
  CHECK(bad == 0, "B: 40 seeds all end in the trunk (%d did not)", bad);
}

static void testVariantC(const char* dir) {
  printf("variant C: a bullseye, then an arrow that splits it\n");
  Result* r;
  runAndSheet(ARCH_C, 4, 99u, 20, "C", "archery_c", dir, &r);
  const ArcheryState& a = r->s.a;
  CHECK(r->releases == 2 && r->lands == 2, "C: two shots (%d, %d)", r->releases, r->lands);
  CHECK(r->splits == 1, "C: one split event (%d)", r->splits);
  const Vec2 c = archeryTargetCenter(a);
  const ArcheryArrow& f = a.arrows[0];
  const ArcheryArrow& g = a.arrows[1];
  CHECK(f.ring == 0 && dist2(f.to, c) < ARCHERY_R_INNER, "C: the first is a bullseye (%.2f px from the centre)", (double)dist2(f.to, c));
  CHECK(f.mode == ARROW_SPLIT, "C: and has split (mode %d)", f.mode);
  CHECK(f.spread > 0.25f, "C: its halves have splayed (%.2f rad)", (double)f.spread);
  CHECK(g.mode == ARROW_STUCK && g.end == AEND_TARGET && g.ring == 0, "C: the second is in the target, in the inner ring");
  CHECK(dist2(g.to, f.to) < 0.5f, "C: the second landed on the first (%.2f px apart)", (double)dist2(g.to, f.to));
  CHECK(dist2(g.tip, f.tip) < 0.7f, "C: and they share a tip (%.2f px apart)", (double)dist2(g.tip, f.tip));
  CHECK(g.victim == 0, "C: the second knows its victim");
  CHECK(r->max_proud > 0.95f, "C: proud");
  CHECK(archeryDone(a) && r->dones == 1, "C: done");
  // The split happens when the second arrow lands, not before.
  CHECK(r->t_land[1] >= r->t_rel[1], "C: the split follows the second release");
  printf("  the second arrow landed %.2f px from the first\n", (double)dist2(g.to, f.to));
  printStrips("C", r->st);
  // A split with nothing to split is a bullseye.
  {
    Sim s;
    simInit(s, 1);
    Rng rng;
    rng.seed(5u);
    archeryShoot(s.a, SHOT_SPLIT, rng);
    for (int i = 0; i < 25 * 12; ++i) simTick(s);
    CHECK(s.a.arrows[0].mode == ARROW_STUCK && s.a.arrows[0].ring == 0 && s.a.arrows[0].victim < 0, "C: a split with no bullseye is a bullseye");
  }
  int bad = 0;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    Sim q;
    simInit(q, 1);
    Rng rng;
    rng.seed(seed);
    archeryPlay(q.a, ARCH_C, rng);
    for (int i = 0; i < 25 * 22; ++i) simTick(q);
    const bool ok = q.a.arrows[0].mode == ARROW_SPLIT && q.a.arrows[1].mode == ARROW_STUCK && q.a.arrows[1].ring == 0 &&
                    dist2(q.a.arrows[1].to, q.a.arrows[0].to) < 0.5f && archeryDone(q.a);
    if (!ok) ++bad;
  }
  CHECK(bad == 0, "C: 40 seeds all split (%d did not)", bad);
}

// The same three, facing left, over a flat background: the logic and the draw mirror.
static void testMirrored() {
  printf("facing left\n");
  for (int variant = 0; variant < ARCH_VARIANTS; ++variant) {
    char name[16];
    snprintf(name, sizeof name, "mirror %c", 'A' + variant);
    Result* r = runVariant(variant, -1, 3, 26, 99u, name, nullptr, nullptr, variant == 0);
    const ArcheryState& a = r->s.a;
    const Vec2 c = archeryTargetCenter(a);
    CHECK(archeryDone(a), "%s: done", name);
    if (variant == 0) {
      CHECK(a.arrows[0].end == AEND_GROUND && a.arrows[0].tip.x > a.target.x + 25.f, "%s: short of the target, on his side", name);
      CHECK(a.arrows[1].end == AEND_PAST && a.arrows[1].tip.x < a.target.x - 30.f, "%s: past it, to the left", name);
      CHECK(a.arrows[2].ring == 0 && dist2(a.arrows[2].tip, c) < ARCHERY_R_INNER, "%s: bullseye", name);
    } else if (variant == 1) {
      CHECK(a.arrows[0].end == AEND_TREE && dist2(a.arrows[0].tip, a.trunk) < 0.01f && a.trunk.x < a.target.x, "%s: in the trunk to the left", name);
    } else {
      CHECK(a.arrows[0].mode == ARROW_SPLIT && a.arrows[1].ring == 0, "%s: split", name);
    }
  }
}

// ---- the pose ------------------------------------------------------------------------------------

static void testPose() {
  printf("pose\n");
  // Zero aim, proud and slump change nothing, exactly.
  {
    HikerPose p0 = hikerPoseDef(POSE_STAND), p = p0;
    archeryBend(p, ArcheryPoseIn{});
    CHECK(memcmp(&p0, &p, sizeof p) == 0, "an idle archer leaves the pose alone");
    ArcheryState idle;
    archeryInit(idle, 1, Vec2{470.f, kLane}, Vec2{556.f, 330.f});
    archeryPose(idle, p);
    CHECK(memcmp(&p0, &p, sizeof p) == 0, "archeryPose on an idle state is a no-op");
  }
  // Through the draw, on the real rig.
  for (int dir = 1; dir >= -1; dir -= 2) {
    Hiker h;
    hikerInit(h, Vec2{300.f, kGround}, dir);
    hikerSetPose(h, POSE_STAND, 0.f);
    hikerTick(h, 0.f);
    float back_prev = -1.f;
    for (int i = 0; i <= 8; ++i) {
      const float draw = (float)i / 8.f;
      hikerSetPose(h, POSE_STAND, 0.f);
      hikerTick(h, 0.f);
      ArcheryPoseIn in;
      in.aim = 1.f;
      in.draw = draw;
      in.elev = 0.1f;
      archeryBend(h.pose, in);
      const Vec2 b = hikerHandPos(h, 0), s = hikerHandPos(h, 1);
      float hr;
      const Vec2 head = hikerHeadPos(h, &hr);
      const float fwd = (float)dir * (b.x - h.feet.x);
      const float back = (float)dir * (b.x - s.x);   // string hand behind the bow hand
      if (i == 0) {
        CHECK(fwd > 24.f && fwd < 29.5f, "dir %d: the bow arm is straight out (%.1f px ahead of his feet)", dir, (double)fwd);
        CHECK(b.y > kGround - 0.85f * h.height && b.y < kGround - 0.6f * h.height, "dir %d: at shoulder height (y %.1f)", dir, (double)b.y);
        CHECK(back > 4.f && back < 14.f, "dir %d: the string hand starts at the string (%.1f px behind)", dir, (double)back);
      }
      if (i == 8) {
        CHECK(back > 18.f && back < 32.f, "dir %d: at full draw the string hand is a draw length back (%.1f px)", dir, (double)back);
        CHECK(dist2(s, head) < 11.f, "dir %d: and near the face (%.1f px from the head centre)", dir, (double)dist2(s, head));
        CHECK(fwd > 24.f, "dir %d: the bow arm is still out", dir);
        CHECK((float)dir * (head.x - h.feet.x) < 5.f && (float)dir * (head.x - h.feet.x) > -8.f, "dir %d: leaning only a little (head %.1f px off his feet)", dir,
              (double)((float)dir * (head.x - h.feet.x)));
      }
      CHECK(back >= back_prev - 0.05f, "dir %d: the string hand only goes back as the draw grows (draw %.2f)", dir, (double)draw);
      back_prev = back;
    }
  }
  // Mirrored: the hands are mirror images about his feet.
  {
    Hiker l, r;
    hikerInit(l, Vec2{300.f, kGround}, -1);
    hikerInit(r, Vec2{300.f, kGround}, 1);
    for (Hiker* q : {&l, &r}) {
      hikerSetPose(*q, POSE_STAND, 0.f);
      hikerTick(*q, 0.f);
      ArcheryPoseIn in;
      in.aim = 0.8f;
      in.draw = 0.6f;
      in.flick = 0.3f;
      in.shake = 0.5f;
      in.elev = 0.2f;
      in.ph_a = 0.3f;
      in.proud = 0.2f;
      archeryBend(q->pose, in);
    }
    for (int i = 0; i < 2; ++i) {
      const Vec2 a = hikerHandPos(l, i), b = hikerHandPos(r, i);
      CHECK(fabsf((a.x - 300.f) + (b.x - 300.f)) < 0.01f && fabsf(a.y - b.y) < 0.01f, "hand %d mirrors", i);
    }
  }
  // Every input, however silly: finite angles.
  {
    const float vals[] = {0.f, 0.5f, 1.f, -3.f, 40.f, NAN, INFINITY, -INFINITY};
    int bad = 0;
    for (float a : vals)
      for (float d : vals)
        for (float f : vals)
          for (float e : vals) {
            HikerPose p = hikerPoseDef(POSE_STAND);
            ArcheryPoseIn in;
            in.aim = a;
            in.draw = d;
            in.flick = f;
            in.elev = e;
            in.shake = a;
            in.proud = f;
            in.slump = d;
            in.ph_a = e;
            archeryBend(p, in);
            const float* q = &p.lean;
            for (int i = 0; i < 4 + 8 + 2 * 4; ++i)
              if (!finiteF(q[i])) ++bad;
          }
    CHECK(bad == 0, "no NaN or infinity out of the pose, whatever goes in (%d bad)", bad);
  }
  // The hold trembles, a little.
  {
    Sim s;
    simInit(s, 1);
    Rng rng;
    rng.seed(3u);
    archeryShoot(s.a, SHOT_BULLSEYE, rng);
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    int n = 0;
    for (int i = 0; i < 25 * 8 && s.a.phase != AP_RELEASE; ++i) {
      simTick(s);
      if (s.a.phase == AP_HOLD && s.a.t > 0.8f) {
        x0 = fminf(x0, s.a.bow_hand.x);
        x1 = fmaxf(x1, s.a.bow_hand.x);
        y0 = fminf(y0, s.a.bow_hand.y);
        y1 = fmaxf(y1, s.a.bow_hand.y);
        ++n;
      }
    }
    CHECK(n > 10, "a hold of more than a second");
    const float span = fmaxf(x1 - x0, y1 - y0);
    CHECK(span > 0.2f && span < 2.5f, "the bow hand trembles by a pixel or so, not more (%.2f px)", (double)span);
    printf("  bow hand tremble over the hold: %.2f px\n", (double)span);
  }
}

// ---- the parts in every state, in every mood -------------------------------------------------------

static void testDrawStates() {
  printf("draw states\n");
  // The target alone, hidden, moved; the bow alone at every aim, tilt and pull.
  {
    ArcheryState a;
    archeryInit(a, 1, Vec2{470.f, kLane}, Vec2{556.f, 330.f});
    const Rect tb = archeryTargetBounds(a);
    CHECK(!tb.empty() && tb.x1 - tb.x0 >= 34 && tb.x1 - tb.x0 < 60 && tb.y1 - tb.y0 >= 52 && tb.y1 - tb.y0 < 80, "target bounds %d,%d..%d,%d", tb.x0, tb.y0, tb.x1,
          tb.y1);
    CHECK(archeryBowBounds(a).empty() && archeryArrowBounds(a, 0).empty(), "no bow and no arrows before they are used");
    checkDraw(a, g_pal[3], "target", 0, 1u);
    archeryShowTarget(a, false);
    CHECK(archeryBounds(a).empty(), "hidden target: no bounds");
    memcpy(g_a, g_noise, sizeof g_a);
    Surface s = frameSurface(g_a);
    archeryDraw(a, s, kFrame, g_pal[3]);
    CHECK(memcmp(g_a, g_noise, sizeof g_a) == 0, "hidden target draws nothing");
    archeryShowTarget(a, true);
    // Moved by the caller: bounds follow.
    a.target = Vec2{300.f, 380.f};
    const Rect tb2 = archeryTargetBounds(a);
    CHECK(tb2.x0 < tb.x0 - 100 && tb2.y1 > tb.y1 + 20, "the target moves with the caller");
  }
  // The bow, equipped and hanging, then every aim and pull.
  {
    Sim s;
    simInit(s, 1);
    archeryEquip(s.a, true);
    simTick(s);
    const Rect lo = archeryBowBounds(s.a);
    CHECK(!lo.empty() && lo.y1 - lo.y0 < 70, "an equipped bow hangs at his side (%d rows)", lo.y1 - lo.y0);
    for (int i = 0; i < 2; ++i) checkDraw(s.a, g_pal[3], "bow hanging", i, 5u);
    archeryEquip(s.a, false);
    CHECK(archeryBowBounds(s.a).empty(), "put away: no bow");
  }
  // Every mood: a mid-draw frame, a flying arrow and every stuck state, drawn right.
  for (int m = 0; m < kMoods; ++m) {
    Sim s;
    simInit(s, 1);
    Rng rng;
    rng.seed(11u);
    archeryPlay(s.a, ARCH_A, rng);
    int checked = 0;
    for (int i = 0; i < 25 * 24; ++i) {
      simTick(s);
      bool flying = false;
      for (int k = 0; k < ARCHERY_ARROWS; ++k) flying = flying || s.a.arrows[k].mode == ARROW_FLY;
      if (i % 20 == 0 || flying) {
        checkDraw(s.a, g_pal[m], kMoodName[m], s.tick, (uint32_t)m);
        ++checked;
      }
    }
    CHECK(checked > 40, "%s: states checked", kMoodName[m]);
  }
  // Two draws of the same state are the same picture.
  {
    Sim s;
    simInit(s, 1);
    Rng rng;
    rng.seed(2u);
    archeryPlay(s.a, ARCH_C, rng);
    for (int i = 0; i < 25 * 14; ++i) simTick(s);
    renderRegion(s.a, g_pal[3], g_land[3], kFrame, g_a);
    renderRegion(s.a, g_pal[3], g_land[3], kFrame, g_b);
    CHECK(memcmp(g_a, g_b, sizeof g_a) == 0, "a draw is a pure function of the state");
  }
}

// A tick that changes nothing visible marks nothing.
static void testDirtyQuiet() {
  printf("dirty marking\n");
  ArcheryState a;
  archeryInit(a, 1, Vec2{470.f, kLane}, Vec2{556.f, 330.f});
  Rect rs[ARCHERY_DIRTY_MAX];
  CHECK(archeryDirty(a, a, rs) == 0 && archerySame(a, a), "an unchanged state marks nothing");
  // The target appearing and vanishing marks its whole area.
  ArcheryState h = a;
  archeryShowTarget(h, false);
  int n = archeryDirty(h, a, rs);
  Rect cover{0, 0, 0, 0};
  for (int i = 0; i < n; ++i) cover = unite(cover, rs[i]);
  CHECK(n > 0 && inside(cover, archeryTargetBounds(a)), "appearing marks everything it covers");
  n = archeryDirty(a, h, rs);
  cover = Rect{0, 0, 0, 0};
  for (int i = 0; i < n; ++i) cover = unite(cover, rs[i]);
  CHECK(n > 0 && inside(cover, archeryTargetBounds(a)), "vanishing marks everything it covered");
  // A hand that moves by a hundredth of a pixel almost never changes a pixel (the draw is quantized to half pixels).
  {
    Sim s;
    simInit(s, 1);
    Rng r;
    r.seed(12u);
    archeryShoot(s.a, SHOT_BULLSEYE, r);
    int same = 0, total = 0;
    for (int i = 0; i < 25 * 9; ++i) {
      simTick(s);
      if (!s.a.bow_on) continue;
      ArcheryState moved = s.a;
      moved.bow_hand.x += 0.01f;
      moved.string_hand.y += 0.01f;
      ++total;
      same += archerySame(s.a, moved) ? 1 : 0;
    }
    CHECK(total > 100 && same * 100 >= total * 90, "a hundredth of a pixel changes nothing %d times in %d", same, total);
  }
  // A zero dt changes nothing.
  Sim z;
  simInit(z, 1);
  Rng rng;
  rng.seed(7u);
  archeryShoot(z.a, SHOT_BULLSEYE, rng);
  for (int i = 0; i < 25 * 6; ++i) simTick(z);
  const ArcheryState before = z.a;
  archeryStep(z.a, z.a.bow_hand, z.a.string_hand, 0.f);
  CHECK(archerySame(before, z.a), "a zero dt draws the same");
}

// ---- the commands, one at a time ---------------------------------------------------------------------

static void testCommands() {
  printf("commands\n");
  Sim s;
  simInit(s, 1);
  CHECK(archeryDone(s.a) && !archeryBusy(s.a), "a fresh archer is done and idle");
  // A shot draws exactly three values, whatever its kind.
  for (int k = 0; k < SHOT_KINDS; ++k) {
    Rng a, b;
    a.seed(77u);
    b.seed(77u);
    ArcheryState st;
    archeryInit(st, 1, Vec2{470.f, kLane}, Vec2{556.f, 330.f});
    archeryShoot(st, (uint8_t)k, a);
    b.uniform();
    b.uniform();
    b.uniform();
    CHECK(a.state == b.state && a.inc == b.inc, "kind %d draws exactly three values", k);
  }
  // archeryPlay draws twelve, whatever the variant.
  for (int v = 0; v < ARCH_VARIANTS; ++v) {
    Rng a, b;
    a.seed(78u);
    b.seed(78u);
    ArcheryState st;
    archeryInit(st, 1, Vec2{470.f, kLane}, Vec2{556.f, 330.f});
    CHECK(archeryPlay(st, (uint8_t)v, a), "variant %d queues", v);
    for (int i = 0; i < 12; ++i) b.uniform();
    CHECK(a.state == b.state, "variant %d draws exactly twelve values", v);
    CHECK(st.n_queue == (v == 0 ? 3 : (v == 1 ? 1 : 2)), "variant %d queues its shots (%d)", v, st.n_queue);
  }
  {
    Rng a;
    a.seed(1u);
    ArcheryState st;
    archeryInit(st, 1, Vec2{470.f, kLane}, Vec2{556.f, 330.f});
    CHECK(!archeryPlay(st, 9, a) && !archeryShoot(st, 77, a), "an unknown variant or kind is refused");
  }
  // The queue holds four; the fifth is refused; a finished run starts a new one.
  {
    Rng a;
    a.seed(2u);
    Sim q;
    simInit(q, 1);
    int ok = 0;
    for (int i = 0; i < 6; ++i) ok += archeryShoot(q.a, SHOT_RING, a) ? 1 : 0;
    CHECK(ok == ARCHERY_ARROWS, "four shots fit (%d)", ok);
    for (int i = 0; i < 25 * 50; ++i) simTick(q);
    CHECK(archeryDone(q.a), "four shots play out");
    int stuck = 0;
    for (int i = 0; i < ARCHERY_ARROWS; ++i) stuck += q.a.arrows[i].mode == ARROW_STUCK ? 1 : 0;
    CHECK(stuck == 4, "and leave four arrows (%d)", stuck);
    CHECK(!archeryShoot(q.a, SHOT_RING, a), "no slot for a fifth until the arrows are cleared");
    archeryClear(q.a);
    CHECK(archeryShoot(q.a, SHOT_RING, a), "cleared: a new shot");
    for (int i = 0; i < 25 * 14; ++i) simTick(q);
    CHECK(archeryDone(q.a) && q.a.arrows[0].mode == ARROW_STUCK, "and it plays");
    // Rings land where the plan says.
    Rng r3;
    r3.seed(3u);
    int in_ring[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 30; ++i) {
      Sim w;
      simInit(w, 1);
      archeryShoot(w.a, SHOT_RING, r3);
      for (int t = 0; t < 25 * 10; ++t) simTick(w);
      ++in_ring[w.a.arrows[0].ring < 5 ? w.a.arrows[0].ring : 4];
      CHECK(w.a.arrows[0].ring == 1 || w.a.arrows[0].ring == 2, "a ring shot lands in the middle or outer ring (%d)", w.a.arrows[0].ring);
      CHECK(archeryRingAt(w.a, w.a.arrows[0].tip) == w.a.arrows[0].ring, "and its ring is where its tip is");
    }
    printf("  30 ring shots: %d middle, %d outer\n", in_ring[1], in_ring[2]);
  }
  // A shot plays to the end by itself, with its events.
  {
    Rng a;
    a.seed(4u);
    Sim q;
    simInit(q, 1);
    archeryShoot(q.a, SHOT_BULLSEYE, a);
    CHECK(archeryBusy(q.a) && q.a.bow_on, "queued: busy, the bow is out");
    int rel = 0, land = 0, done = 0, phases = 0;
    uint32_t seen = 0;
    for (int i = 0; i < 25 * 14; ++i) {
      simTick(q);
      rel += (q.a.events & AEV_RELEASED) ? 1 : 0;
      land += (q.a.events & AEV_LANDED) ? 1 : 0;
      done += (q.a.events & AEV_DONE) ? 1 : 0;
      seen |= 1u << q.a.phase;
    }
    for (int p = 0; p <= AP_PAUSE; ++p) phases += (seen >> p) & 1u;
    CHECK(rel == 1 && land == 1 && done == 1, "one release, one landing, one done (%d %d %d)", rel, land, done);
    CHECK(phases == AP_PAUSE + 1, "every phase was visited (%d of %d)", phases, AP_PAUSE + 1);
  }
  // The hands hold still in the idle pose: nothing is drawn but a hanging bow.
  {
    Sim q;
    simInit(q, 1);
    archeryEquip(q.a, true);
    for (int i = 0; i < 100; ++i) simTick(q);
    CHECK(q.a.aim == 0.f && q.a.phase == AP_IDLE, "equipped is not aiming");
  }
  // archeryClear removes everything and stops a shot in the middle.
  {
    Rng a;
    a.seed(6u);
    Sim q;
    simInit(q, 1);
    archeryPlay(q.a, ARCH_A, a);
    for (int i = 0; i < 25 * 7; ++i) simTick(q);
    archeryClear(q.a);
    simTick(q);
    CHECK(!archeryBusy(q.a) && archeryDone(q.a), "cleared mid-run: idle and done");
    CHECK(archeryArrowBounds(q.a, 0).empty() && archeryArrowBounds(q.a, 1).empty(), "no arrows left");
  }
  printf("  sizeof(ArcheryState) = %d bytes, sizeof(ArcheryArrow) = %d\n", (int)sizeof(ArcheryState), (int)sizeof(ArcheryArrow));
  CHECK(sizeof(ArcheryState) <= 512, "ArcheryState is small (%d bytes)", (int)sizeof(ArcheryState));
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
  ArcheryState a;
  archeryInit(a, 1, Vec2{470.f, kLane}, Vec2{556.f, 330.f});
  Rng rng;
  rng.seed(5u);
  Vec2 bow{280.f, 290.f}, str{258.f, 292.f};
  uint32_t hash = 2166136261u;
  *worst_rects = 0;
  *max_abs = 0.f;
  for (int t = 0; t < ticks; ++t) {
    if (rnd() % 120u == 0) {
      switch (rnd() % 9u) {
        case 0: archeryShoot(a, (uint8_t)(rnd() % (uint32_t)SHOT_KINDS), rng); break;
        case 1: archeryPlay(a, (uint8_t)(rnd() % 3u), rng); break;
        case 2: archeryClear(a); break;
        case 3: archeryEquip(a, (rnd() & 1u) != 0u); break;
        case 4: archeryFace(a, (rnd() & 1u) ? 1 : -1); break;
        case 5: archeryShowTarget(a, (rnd() & 1u) != 0u); break;
        case 6:
          archeryInit(a, (rnd() & 1u) ? 1 : -1, Vec2{300.f + 400.f * rndf(), 330.f + 40.f * rndf()}, Vec2{500.f + 200.f * rndf(), 300.f + 60.f * rndf()});
          break;
        default: archeryShoot(a, SHOT_SPLIT, rng); break;
      }
    }
    // The hands wander (he walks, breathes, or is somewhere else entirely).
    bow.x += (rndf() - 0.5f) * 3.f;
    bow.y += (rndf() - 0.5f) * 1.5f;
    str.x = bow.x - 20.f + (rndf() - 0.5f) * 8.f;
    str.y = bow.y + 3.f + (rndf() - 0.5f) * 3.f;
    if (bow.x < -300.f || bow.x > 1100.f) bow.x = 300.f;
    if (bow.y < 100.f || bow.y > 400.f) bow.y = 290.f;
    float dt = kDt;
    if (bad_input) {
      const uint32_t r = rnd() % 200u;
      if (r == 0) dt = NAN;
      else if (r == 1) dt = -1.f;
      else if (r == 2) dt = 50.f;
      else if (r == 3) dt = 0.f;
      else if (r == 4) bow.x = NAN;
      else if (r == 5) str.y = INFINITY;
    }
    const ArcheryState prev = a;
    archeryStep(a, bow, str, dt);
    if (bad_input && !finiteF(bow.x)) bow.x = a.bow_hand.x;
    if (bad_input && !finiteF(str.y)) str.y = a.string_hand.y;
    // The pose too, on a copy of the rig's.
    HikerPose p = hikerPoseDef(POSE_STAND);
    archeryPose(a, p);
    if (!finiteArcher(a) || !finiteF(p.shoulder[1]) || !finiteF(p.elbow[1])) {
      CHECK(false, "tick %d: state not finite (phase %d)", t, a.phase);
      return 0;
    }
    float ap = 0.f;
    for (int i = 0; i < ARCHERY_ARROWS; ++i) ap = fmaxf(ap, fmaxf(fabsf(a.arrows[i].tip.x), fabsf(a.arrows[i].tip.y)));
    if (ap > *max_abs) *max_abs = ap;
    Rect rs[ARCHERY_DIRTY_MAX];
    const int n = archeryDirty(prev, a, rs);
    if (n > *worst_rects) *worst_rects = n;
    const Rect b = archeryBounds(a);
    if (!b.empty() && (b.x1 - b.x0 > 4000 || b.y1 - b.y0 > 4000)) {
      CHECK(false, "tick %d: absurd bounds %d,%d..%d,%d (phase %d)", t, b.x0, b.y0, b.x1, b.y1, a.phase);
      return 0;
    }
    if (t % 400 == 0) {
      memcpy(g_a, g_noise, sizeof g_a);
      Surface s = frameSurface(g_a);
      archeryDraw(a, s, kFrame, g_pal[3]);
      const Rect d = diffBox(g_a, g_noise, kFrame);
      if (!(d.empty() || inside(b, d))) {
        CHECK(false, "tick %d: drawn outside the bounds (phase %d)", t, a.phase);
        return 0;
      }
    }
    const unsigned char* bytes = (const unsigned char*)&a.arrows[0].tip;
    for (int i = 0; i < 8; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    bytes = (const unsigned char*)&a.bow_hand;
    for (int i = 0; i < 8; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    hash = (hash ^ a.phase) * 16777619u;
  }
  return hash;
}

static void testLongRuns() {
  printf("long runs\n");
  int worst = 0;
  float mx = 0.f;
  const uint32_t h1 = stressRun(200000, false, &worst, &mx);
  printf("  200000 random ticks: at most %d dirty rects, farthest arrow coordinate %.0f px\n", worst, (double)mx);
  CHECK(h1 != 0, "stress run completed");
  CHECK(mx < 4000.f, "no arrow runs away (%.0f)", (double)mx);
  int worst2 = 0;
  float mx2 = 0.f;
  const uint32_t h2 = stressRun(200000, false, &worst2, &mx2);
  CHECK(h1 == h2 && worst == worst2, "the same run twice gives the same archery (%08x, %08x)", h1, h2);
  int worst3 = 0;
  float mx3 = 0.f;
  const uint32_t h3 = stressRun(100000, true, &worst3, &mx3);
  CHECK(h3 != 0 && mx3 < 4000.f, "NaN, infinity and silly values in: still finite and bounded");
  CHECK(worst <= ARCHERY_DIRTY_MAX && worst3 <= ARCHERY_DIRTY_MAX, "dirty rects within the cap");

  // A hand a long way off, or at a silly value: bounds stay finite and in range, and nothing breaks.
  {
    Sim q;
    simInit(q, 1);
    Rng rng;
    rng.seed(4u);
    archeryShoot(q.a, SHOT_TREE, rng);
    for (int i = 0; i < 25 * 6; ++i) simTick(q);
    for (float v : {1.0e9f, -1.0e9f, 3.0e38f, -3.0e38f, 1.0e5f}) {
      archeryStep(q.a, Vec2{v, -v}, Vec2{-v, v}, kDt);
      const Rect b = archeryBounds(q.a);
      CHECK(b.x0 > -40000 && b.x1 < 40000 && b.y0 > -40000 && b.y1 < 40000, "a hand at %g: bounds stay in range (%d,%d..%d,%d)", (double)v, b.x0, b.y0, b.x1, b.y1);
      memcpy(g_a, g_noise, sizeof g_a);
      Surface sf = frameSurface(g_a);
      archeryDraw(q.a, sf, kFrame, g_pal[3]);
      const Rect d = diffBox(g_a, g_noise, kFrame);
      CHECK(d.empty() || inside(b, d), "a hand at %g: drawn inside the bounds", (double)v);
      CHECK(finiteArcher(q.a), "a hand at %g: state finite", (double)v);
    }
  }

  // A replay from the same commands is the same, byte for byte (no hidden state, no clock).
  Sim a, b;
  simInit(a, 1);
  simInit(b, 1);
  for (Sim* q : {&a, &b}) {
    Rng rng;
    rng.seed(21u);
    archeryPlay(q->a, ARCH_A, rng);
    for (int i = 0; i < 25 * 30; ++i) simTick(*q);
  }
  CHECK(sameBytes(a.a, b.a), "two identical runs end in identical bytes");
  // dt-independence: the arrow's landing point does not depend on the tick size.
  {
    Sim q;
    simInit(q, 1);
    Rng rng;
    rng.seed(33u);
    archeryShoot(q.a, SHOT_BULLSEYE, rng);
    for (int i = 0; i < 25 * 8 && q.a.arrows[0].mode != ARROW_FLY; ++i) simTick(q);
    ArcheryState x = q.a, y = q.a;
    const Vec2 bw = q.a.bow_hand, sh = q.a.string_hand;
    for (int i = 0; i < 400; ++i) archeryStep(x, bw, sh, 0.01f);
    for (int i = 0; i < 100; ++i) archeryStep(y, bw, sh, 0.04f);
    CHECK(x.arrows[0].mode == ARROW_STUCK && y.arrows[0].mode == ARROW_STUCK &&
              dist2(x.arrows[0].tip, y.arrows[0].tip) < 0.01f && dist2(x.arrows[0].to, y.arrows[0].to) < 0.01f,
          "the same landing at dt 0.01 and 0.04");
  }
}

// ---- how it reads, every mood -----------------------------------------------------------------------------------

static float lum(Pixel p) { return (0.2126f * (float)red8(p) + 0.7152f * (float)green8(p) + 0.0722f * (float)blue8(p)) / 255.f; }

// A tile: the whole scene of `s` in mood `mood`, a zoomed crop of it.
static void shootTile(Sheet& sh, int tile, const Sim& s, int mood, const Rect& crop, int z) {
  drawScene(s, g_pal[mood], g_land[mood], g_c);
  sh.tileUp(tile, g_c, crop, z);
}

// Tick until `ev` has been raised `nth` times (the tick it is raised on is the current one).
static bool runToEvent(Sim& s, uint8_t ev, int nth, int max_ticks) {
  int seen = 0;
  for (int i = 0; i < max_ticks; ++i) {
    simTick(s);
    if (s.a.events & ev) ++seen;
    if (seen >= nth) return true;
  }
  return false;
}
static void advance(Sim& s, int ticks) {
  for (int i = 0; i < ticks; ++i) simTick(s);
}

static void testMoodsAndZoom(const char* dir) {
  printf("moods and zoom\n");
  for (int m = 0; m < kMoods; ++m) {
    const Palette& p = g_pal[m];
    const float sky = lum(p[SKY2]), ink = lum(p[ACTOR]), red = lum(p[MUSHROOM]), cream = lum(p[TENT_LIT]);
    const float straw = lum(mixRGB(p[TENT_LIT], p[TRUNK], 0.30f)), edge = lum(mixRGB(p[TENT_LIT], p[TRUNK], 0.62f));
    printf("  %-8s sky %.2f  ink %.2f  rings %.2f / %.2f  straw %.2f edge %.2f  ink against sky %.2f, edge against straw %.2f\n", kMoodName[m],
           (double)sky, (double)ink, (double)red, (double)cream, (double)straw, (double)edge, (double)fabsf(ink - sky),
           (double)fabsf(edge - straw));
    CHECK(fabsf(ink - sky) > 0.06f, "%s: the bow and arrows show against the sky", kMoodName[m]);
    CHECK(lum(p[TRUNK]) - ink > 0.04f, "%s: the wooden bow shows against his black silhouette (%.2f over %.2f)", kMoodName[m], (double)lum(p[TRUNK]), (double)ink);
    CHECK(fabsf(red - cream) > 0.05f, "%s: the two ring colours differ", kMoodName[m]);
    CHECK(fabsf(edge - straw) > 0.02f, "%s: the straw edge is darker than the straw", kMoodName[m]);
    CHECK(edge < straw, "%s: darker, not lighter", kMoodName[m]);
    if (m == 0) {   // night rule: nothing bright in the target, no pure white
      CHECK(red <= NIGHT_LUM_MAX && cream <= NIGHT_LUM_MAX && straw <= NIGHT_LUM_MAX && edge <= NIGHT_LUM_MAX,
            "night: every target colour within the night cap");
    }
  }

  // The zoom sheet. Rows 0 and 2: the archer at 3x (day, night); rows 1 and 3: the target and its arrows.
  const int ncols = 8, zw = 110, zh = 112, z = 3;
  Sheet sh;
  sh.init(ncols, 4, zw * z, zh * z);
  const Layout lay = layoutFor(1);
  for (int rowset = 0; rowset < 2; ++rowset) {
    const int mood = rowset == 0 ? 3 : 0;
    const int arow = rowset * 2, trow = rowset * 2 + 1;

    // The archer: draw begins, half drawn, full draw, the late hold, the release, the flick, proud, the shrug.
    int rel[ARCHERY_ARROWS], land[ARCHERY_ARROWS], nrel = 0;
    findTimes(ARCH_A, 1, 99u, 26, rel, land, &nrel);
    CHECK(nrel == 3, "zoom: three releases found");
    const Rect acrop{(int)lay.archer_x - 48, (int)kGround - 108, (int)lay.archer_x - 48 + zw, (int)kGround - 108 + zh};
    const int r3 = rel[2];
    const int atk[ncols] = {r3 - 25 * 3 - 12,   // raised, the draw about to begin
                            r3 - 25 * 2 - 12,   // half drawn
                            r3 - 25 * 2 + 6,    // at full draw
                            r3 - 3,             // the late hold, trembling
                            r3 + 1,             // the release
                            r3 + 4,             // the flick
                            land[2] + 25 + 12,  // proud, at its height
                            land[0] + 25 + 6};  // the shrug after the first miss
    int order[ncols];
    for (int i = 0; i < ncols; ++i) order[i] = i;
    for (int i = 0; i < ncols; ++i)
      for (int j = i + 1; j < ncols; ++j)
        if (atk[order[j]] < atk[order[i]]) {
          const int t = order[i];
          order[i] = order[j];
          order[j] = t;
        }
    {
      Sim s;
      simInit(s, 1);
      Rng rng;
      rng.seed(99u);
      archeryPlay(s.a, ARCH_A, rng);
      int next = 0;
      for (int tick = 0; tick < 25 * 26 && next < ncols; ++tick) {
        simTick(s);
        while (next < ncols && s.tick == atk[order[next]]) {
          shootTile(sh, arow * ncols + order[next], s, mood, acrop, z);
          ++next;
        }
      }
      CHECK(next == ncols, "zoom: every archer frame was taken (%d of %d)", next, ncols);
    }

    // The target and arrows.
    const Rect tcrop{(int)lay.target_x - 36, (int)kLane - 78, (int)lay.target_x - 36 + zw, (int)kLane - 78 + zh};
    auto around = [&](Vec2 p) {
      return Rect{(int)p.x - zw / 2, (int)p.y - zh * 2 / 3, (int)p.x - zw / 2 + zw, (int)p.y - zh * 2 / 3 + zh};
    };
    {   // 0: bare
      Sim q;
      simInit(q, 1);
      shootTile(sh, trow * ncols + 0, q, mood, tcrop, z);
    }
    {   // 1: a bullseye, quiver over
      Sim q;
      simInit(q, 1);
      Rng r2;
      r2.seed(99u);
      archeryShoot(q.a, SHOT_BULLSEYE, r2);
      runToEvent(q, AEV_LANDED, 1, 25 * 12);
      advance(q, 20);
      shootTile(sh, trow * ncols + 1, q, mood, tcrop, z);
    }
    {   // 2: a ring hit, mid-quiver
      Sim q;
      simInit(q, 1);
      Rng r2;
      r2.seed(8u);
      archeryShoot(q.a, SHOT_RING, r2);
      runToEvent(q, AEV_LANDED, 1, 25 * 12);
      advance(q, 3);
      shootTile(sh, trow * ncols + 2, q, mood, tcrop, z);
    }
    {   // 3, 4: the split, as it opens, and when it has
      Sim q;
      simInit(q, 1);
      Rng r2;
      r2.seed(99u);
      archeryPlay(q.a, ARCH_C, r2);
      runToEvent(q, AEV_SPLIT, 1, 25 * 20);
      advance(q, 2);
      shootTile(sh, trow * ncols + 3, q, mood, tcrop, z);
      advance(q, 25);
      shootTile(sh, trow * ncols + 4, q, mood, tcrop, z);
    }
    {   // 5, 6: in the ground short of the target, then the one that slid past
      Sim q;
      simInit(q, 1);
      Rng r2;
      r2.seed(99u);
      archeryPlay(q.a, ARCH_A, r2);
      runToEvent(q, AEV_LANDED, 1, 25 * 20);
      advance(q, 3);
      shootTile(sh, trow * ncols + 5, q, mood, around(q.a.arrows[0].tip), z);
      runToEvent(q, AEV_LANDED, 1, 25 * 20);
      advance(q, 6);
      shootTile(sh, trow * ncols + 6, q, mood, around(Vec2{q.a.target.x + 30.f, kLane}), z);
    }
    {   // 7: in the pine
      Sim q;
      simInit(q, 1);
      Rng r2;
      r2.seed(99u);
      archeryPlay(q.a, ARCH_B, r2);
      runToEvent(q, AEV_LANDED, 1, 25 * 20);
      advance(q, 4);
      shootTile(sh, trow * ncols + 7, q, mood, around(q.a.trunk), z);
    }
  }
  char path[512];
  snprintf(path, sizeof path, "%s%sarchery_zoom.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d): rows day archer, day target, night archer, night target\n", path, sh.w(), sh.h());
  printf("      archer columns: draw begins, half drawn, full draw, late hold, release, flick, proud, shrug\n");
  printf("      target columns: bare, bullseye, ring hit, split opening, split, in the ground, slid past, in the pine\n");
  free(sh.rgb);
}

int main(int argc, char** argv) {
  g_scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(g_scratch);
  const char* dir = argc > 1 ? argv[1] : "";
  fillNoise(g_noise);
  for (int i = 0; i < kW * kH; ++i) g_flat[i] = rgb565_hex(0x6E9FB8);
  buildLandscapes();

  printf("archery: sizeof(ArcheryState) %d bytes, target %.0f px across, arrow %.0f px, up to %d arrows\n", (int)sizeof(ArcheryState),
         (double)(2.f * kArcheryTargetR), (double)kArcheryArrowLen, ARCHERY_ARROWS);
  testCommands();
  testPose();
  testDrawStates();
  testDirtyQuiet();
  testVariantA(dir);
  testVariantB(dir);
  testVariantC(dir);
  testMirrored();
  testLongRuns();
  testMoodsAndZoom(dir);

  printf("%d checks, %d failed\n", g_checks, g_fail);
  free(g_scratch);
  return g_fail ? 1 : 0;
}
