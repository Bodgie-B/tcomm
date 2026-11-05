// Tests for core/scene/boomerang: the three endings, bounds, strips against
// full frames, the clip, long runs for NaN, the dirty strips a flight costs,
// determinism, and the contact sheets. Plain executable; returns non-zero on
// failure.
//
//   test_boomerang [--bg FILE.ppm] [--out DIR] [--mood F]
// Writes into DIR (default: the current directory) boomerang_a.ppm, boomerang_b.ppm and
// boomerang_c.ppm: a frame every 0.25 s at 2x downscale, 4 tiles across, left to right and down;
// then a tile that strobes the whole flight on one frame (the boomerang every 0.1 s, the eagle
// every 0.4 s); then the ending at native pixels (4x on the catch and on the trunk, 3x on the
// grab, before it and after it). And eagle_zoom.ppm: one wingbeat at 3x, then the poses that
// matter (cruising, the flare, carrying, facing left). The background is a plain scene built from
// the palette slots unless --bg gives an 800x480 P6 PPM (a frame from ss_preview; the scenario
// below is aimed at the one named there). Turn the PPMs into PNGs with the sheet.py in
// HOW_TO_ADD_SCENES.md section 9: `python -I sheet.py OUT.png 1 1 IN.ppm`.
// PPM (a frame from ss_preview). Turn the PPMs into PNGs with the sheet.py in
// HOW_TO_ADD_SCENES.md section 9: `python -I sheet.py OUT.png 1 1 IN.ppm`.
#include "anim/rng.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/boomerang.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

using namespace ss;

static int g_fail = 0;
static int g_checks = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);  \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static const int W = SS_WIDTH;
static const int H = SS_HEIGHT;
static const Rect kFrame{0, 0, W, H};
static const Rect kChip{538, 412, 797, 477};   // the status chip and dog badges (HOW_TO_ADD_SCENES.md 8.1)
static const Rect kHim{392, 298, 424, 396};    // where he stands in the scenario below (hat tip at y 306): the eagle never gets closer than this
static const Rect kHimMirror{376, 298, 408, 396};   // the same scenario mirrored about x = 400

static bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }
static bool sameRect(const Rect& a, const Rect& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }
static float dist(Vec2 a, Vec2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
static bool finite(float v) { return v == v && fabsf(v) < 1.0e6f; }
static bool finiteV(Vec2 v) { return finite(v.x) && finite(v.y); }

struct Frame {
  std::vector<Pixel> px;
  Surface s;
  Frame(int w, int h, Pixel fill) : px((size_t)w * (size_t)h, fill) { s = Surface{px.data(), w, h, w, 0}; }
  Frame(const Frame& o) : px(o.px) { s = Surface{px.data(), o.s.width, o.s.height, o.s.stride, 0}; }
  Frame& operator=(const Frame& o) {
    px = o.px;
    s = Surface{px.data(), o.s.width, o.s.height, o.s.stride, 0};
    return *this;
  }
  Pixel at(int x, int y) const { return s.row(y)[x]; }
  Rect all() const { return Rect{0, 0, s.width, s.height}; }
};

static Palette g_pal;
static Pixel g_bg;   // the flat background every pixel test starts from

static void initPalette(float mood) {
  const WeatherTint tint{0.f, 0.f, mood < 0.5f ? 1.f : 0.f};
  computePalette(mood, tint, g_pal);
  g_bg = g_pal[SKY2];
}

// ---- the scenario every test shares ------------------------------------------------
// Against the real midday frame (ss_preview --seed 7 --mood 3 --spine midday --weather clear
// --skip 4500 --every 500 --frames 1: tick 5000): he stands at x 406, feet at y 392, the
// arm forward at about (416, 346); the left foreground pine's trunk (x 69..76) has its right face at
// x 77 (bare trunk below y 385; the foliage hides it higher); the camp pine (x 558) and the
// stream are to the right, the chip bottom right.
static BoomSpec specFor(int ending) {
  BoomSpec s;
  s.ending = (uint8_t)ending;
  s.hand = Vec2{416.f, 346.f};
  s.catch_pt = Vec2{412.f, 348.f};
  s.trunk = Vec2{77.f, 412.f};
  s.miss = 44.f;
  s.trunk_turns = 0.08f;
  s.reach = 130.f;
  s.rise = 84.f;
  s.dip = 28.f;
  s.seconds = 3.4f;
  s.wind_up = 0.5f;
  return s;
}

static BoomFlight planned(int ending, uint64_t seed) {
  Rng rng;
  rng.seed(seed);
  BoomFlight f;
  boomPlan(f, specFor(ending), rng);
  return f;
}

// ---- bounds scan: compare a frame with the flat background, restoring it -------------

static std::vector<Pixel> g_bgrow;
static void scanRestore(Frame& f, const Rect& b, int& drawn, int& outside) {
  if (g_bgrow.size() != (size_t)f.s.width || g_bgrow[0] != g_bg) g_bgrow.assign((size_t)f.s.width, g_bg);
  for (int y = 0; y < f.s.height; ++y) {
    Pixel* row = f.s.row(y);
    if (memcmp(row, g_bgrow.data(), (size_t)f.s.width * sizeof(Pixel)) == 0) continue;
    for (int x = 0; x < f.s.width; ++x) {
      if (row[x] == g_bg) continue;
      ++drawn;
      if (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) ++outside;
    }
    memcpy(row, g_bgrow.data(), (size_t)f.s.width * sizeof(Pixel));
  }
}

struct BoundsStats {
  int states = 0, drawn = 0, outside = 0, empty_but_drawn = 0;
};

static void boundsBoomerang(BoundsStats& st, Frame& f, const Boomerang& b) {
  boomerangDraw(b, f.s, f.all(), g_pal);
  const Rect r = boomerangBounds(b);
  int drawn = 0, outside = 0;
  scanRestore(f, r, drawn, outside);
  ++st.states;
  st.drawn += drawn;
  st.outside += outside;
  if (drawn > 0 && r.empty()) ++st.empty_but_drawn;
}
static void boundsEagle(BoundsStats& st, Frame& f, const Eagle& e) {
  eagleDraw(e, f.s, f.all(), g_pal);
  const Rect r = eagleBounds(e);
  int drawn = 0, outside = 0;
  scanRestore(f, r, drawn, outside);
  ++st.states;
  st.drawn += drawn;
  st.outside += outside;
  if (drawn > 0 && r.empty()) ++st.empty_but_drawn;
}

// ---- 1. sizes and budgets ---------------------------------------------------------------

static void testSizes() {
  printf("sizes\n");
  printf("  sizeof(Boomerang) = %zu, sizeof(Eagle) = %zu, sizeof(BoomPlan) = %zu, sizeof(BoomFlight) = %zu\n",
         sizeof(Boomerang), sizeof(Eagle), sizeof(BoomPlan), sizeof(BoomFlight));
  CHECK(sizeof(BoomFlight) <= 320, "BoomFlight is %zu bytes (a scene keeps one, and a copy for its dirty tracking)", sizeof(BoomFlight));

  Boomerang b{true, 1, Vec2{400.f, 240.f}, 0.f};
  int most_b = 0;
  for (int i = 0; i < 16; ++i) {
    b.rot = (float)i / 16.f;
    const int n = boomerangPrims(b);
    if (n > most_b) most_b = n;
  }
  Eagle e{true, 1, Vec2{400.f, 240.f}, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f};
  const int n_e = eaglePrims(e);
  printf("  rasterizer calls: boomerang %d, eagle %d (the largest critter rig issues 9)\n", most_b, n_e);
  CHECK(most_b <= 4, "boomerang issues %d", most_b);
  CHECK(n_e <= 9, "eagle issues %d", n_e);

  // About 18 px across, and about 50 px from wingtip to wingtip with the wings out straight.
  int xmin = 9999, xmax = -9999, ymin = 9999, ymax = -9999;
  Frame f(W, H, g_bg);
  for (int i = 0; i < 64; ++i) {
    b.rot = (float)i / 64.f;
    boomerangDraw(b, f.s, f.all(), g_pal);
    const Rect r = boomerangBounds(b);
    int drawn = 0, outside = 0;
    for (int y = r.y0; y < r.y1; ++y)
      for (int x = r.x0; x < r.x1; ++x)
        if (f.at(x, y) != g_bg) {
          xmin = x < xmin ? x : xmin;
          xmax = x > xmax ? x : xmax;
          ymin = y < ymin ? y : ymin;
          ymax = y > ymax ? y : ymax;
        }
    scanRestore(f, r, drawn, outside);
  }
  // The widest the V ever is across any direction: its diagonal over the spin.
  b.rot = 0.f;
  boomerangDraw(b, f.s, f.all(), g_pal);
  {
    int y0 = 9999, y1 = -9999;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        if (f.at(x, y) != g_bg) {
          y0 = y < y0 ? y : y0;
          y1 = y > y1 ? y : y1;
        }
    int drawn = 0, outside = 0;
    scanRestore(f, boomerangBounds(b), drawn, outside);
    const int across = y1 - y0 + 1;
    printf("  boomerang across (tip to tip, rot 0): %d px\n", across);
    CHECK(across >= 16 && across <= 21, "boomerang is %d px across", across);
  }
}

// ---- 2. ending A: caught -----------------------------------------------------------------------

static void testCatch() {
  printf("A: catch\n");
  float worst_end_step = 0.f;
  for (int seed = 1; seed <= 16; ++seed) {
    Rng rng;
    rng.seed((uint64_t)seed);
    BoomFlight f;
    const BoomSpec sp = specFor(BOOM_CATCH);
    boomPlan(f, sp, rng);
    CHECK(f.phase == BP_WAIT && f.boom.on, "starts waiting in the hand");
    CHECK(dist(f.boom.pos, sp.hand) < 0.01f, "starts at the hand");
    int thrown = 0, caught = 0, ticks = 0;
    Vec2 prev = f.boom.pos;
    float max_step = 0.f, maxx = -1e9f, miny = 1e9f, last_step = 0.f;
    int bad_bounds = 0;
    while (!boomDone(f) && ticks < 25 * 30) {
      boomStep(f, SS_TICK_DT);
      ++ticks;
      if (f.events & BE_THROWN) ++thrown;
      if (f.events & BE_CAUGHT) ++caught;
      const float step = dist(prev, f.boom.pos);
      if (step > max_step) max_step = step;
      last_step = step;
      prev = f.boom.pos;
      if (f.phase == BP_FLY) {
        if (f.boom.pos.x > maxx) maxx = f.boom.pos.x;
        if (f.boom.pos.y < miny) miny = f.boom.pos.y;
      }
      const Rect b = boomerangBounds(f.boom);
      if (b.empty() || overlaps(b, kChip)) ++bad_bounds;
      CHECK(!f.eagle.on, "A has no eagle");
    }
    CHECK(f.phase == BP_HAND && boomDone(f), "seed %d ends in the hand (phase %d)", seed, (int)f.phase);
    CHECK(thrown == 1 && caught == 1, "seed %d: thrown %d caught %d", seed, thrown, caught);
    const float d = dist(f.boom.pos, sp.catch_pt);
    CHECK(d <= 2.f, "seed %d: ends %.2f px from the catch point", seed, (double)d);
    CHECK(fabsf(f.boom.rot - sp.hand_turns) < 1.0e-4f, "seed %d: rests at its hand angle (%.4f)", seed, (double)f.boom.rot);
    CHECK(max_step < 22.f, "seed %d: no jump, biggest step %.1f px", seed, (double)max_step);
    CHECK(last_step < 6.f, "seed %d: slows into the hand, last step %.1f px", seed, (double)last_step);
    if (last_step > worst_end_step) worst_end_step = last_step;
    CHECK(maxx >= sp.hand.x + 0.6f * sp.reach, "seed %d: loop reaches out to x %.0f", seed, (double)maxx);
    CHECK(miny <= sp.hand.y - 0.6f * sp.rise, "seed %d: loop rises to y %.0f", seed, (double)miny);
    CHECK(bad_bounds == 0, "seed %d: %d ticks with empty bounds or into the chip's corner", seed, bad_bounds);
    const float t_catch = f.plan.t_throw + f.plan.dur;
    CHECK(fabsf(f.t - t_catch) <= SS_TICK_DT + 1.0e-3f, "seed %d: caught at %.2f s, planned %.2f", seed, (double)f.t, (double)t_catch);
    // It stays: nothing moves or changes for good.
    const Boomerang held = f.boom;
    for (int i = 0; i < 250; ++i) boomStep(f, SS_TICK_DT);
    CHECK(boomerangSame(held, f.boom) && f.events == 0, "seed %d: still in the hand 10 s later", seed);
  }
  printf("  16 seeds: ends within 2 px of the catch point; last step before the catch at most %.1f px\n", (double)worst_end_step);
}

// ---- 3. ending B: the eagle ------------------------------------------------------------------------

// The scenario mirrored about x = 400: he stands at 394, the loop goes out to the left.
static BoomSpec mirrored(BoomSpec s) {
  s.hand.x = 800.f - s.hand.x;
  s.catch_pt.x = 800.f - s.catch_pt.x;
  s.trunk.x = 800.f - s.trunk.x;
  s.trunk_turns = 0.5f - s.trunk_turns;
  s.reach = -s.reach;
  return s;
}

static void eagleCase(bool mirror) {
  printf("B: eagle\n");
  int facing_seen[2] = {0, 0};
  float worst_meet = 0.f, worst_carry = 0.f, max_lead = 0.f, min_in = 99.f;
  for (int seed = 1; seed <= 40; ++seed) {
    Rng rng;
    rng.seed((uint64_t)seed);
    BoomFlight f;
    const BoomSpec sp = mirror ? mirrored(specFor(BOOM_EAGLE)) : specFor(BOOM_EAGLE);
    const Rect him = mirror ? kHimMirror : kHim;
    boomPlan(f, sp, rng);
    const BoomPlan pl = f.plan;
    facing_seen[pl.facing > 0 ? 1 : 0]++;
    const float lead = pl.t_throw - sp.wind_up;
    if (lead > max_lead) max_lead = lead;
    CHECK(pl.t_e0 >= 0.f, "seed %d: the eagle enters at %.2f, after the plan starts", seed, (double)pl.t_e0);
    CHECK(pl.t_land > pl.t_throw && pl.t_land < pl.t_throw + pl.dur, "seed %d: the grab is part way through the flight", seed);
    const float t_in = pl.t_land - pl.t_e0;
    if (t_in < min_in) min_in = t_in;
    CHECK(t_in >= 1.3f, "seed %d: glides in for %.2f s", seed, (double)t_in);
    CHECK(pl.e_m.x > 40.f && pl.e_m.x < 760.f && pl.e_m.y > 40.f && pl.e_m.y < 400.f,
          "seed %d: meeting at (%.0f, %.0f)", seed, (double)pl.e_m.x, (double)pl.e_m.y);

    // Run to just before the meeting, then look at the two at the planned time.
    int ticks = 0;
    Vec2 prev_boom = f.boom.pos, prev_eagle = f.eagle.pos;
    bool have_prev = false;
    float max_boom_step = 0.f;
    bool entered_off_screen = false, was_visible = false;
    int bad_corner = 0, wrong_way = 0, thrown = 0, snatched = 0, gone = 0, on_him = 0;
    while (f.t < pl.t_land - 0.05f) {
      boomStep(f, SS_TICK_DT);
      ++ticks;
      if (f.events & BE_THROWN) ++thrown;
      const float st = dist(prev_boom, f.boom.pos);
      if (f.phase == BP_FLY && st > max_boom_step) max_boom_step = st;
      prev_boom = f.boom.pos;
      if (f.eagle.on) {
        const Rect eb = eagleBounds(f.eagle);
        if (!was_visible && eb.empty()) entered_off_screen = true;
        if (!eb.empty()) was_visible = true;
        if (overlaps(eb, kChip)) ++bad_corner;
        if (overlaps(eb, him)) ++on_him;
        if (have_prev && ((f.eagle.pos.x - prev_eagle.x) * (float)f.eagle.facing) < 0.f) ++wrong_way;
        have_prev = true;
        prev_eagle = f.eagle.pos;
      }
      if (overlaps(boomerangBounds(f.boom), kChip)) ++bad_corner;
    }
    CHECK(entered_off_screen, "seed %d: the eagle first appears off screen", seed);
    CHECK(was_visible, "seed %d: the eagle is seen before the grab", seed);
    boomStep(f, (pl.t_land - 0.001f) - f.t);   // 1 ms before the planned time
    CHECK(f.phase == BP_FLY && f.eagle.on, "seed %d: still flying 1 ms before the grab", seed);
    const float meet = dist(eagleTalons(f.eagle), f.boom.pos);
    if (meet > worst_meet) worst_meet = meet;
    CHECK(meet <= 3.f, "seed %d: talons %.2f px from the boomerang at the planned time", seed, (double)meet);
    CHECK(f.eagle.reach > 0.95f, "seed %d: talons out at the grab (%.2f)", seed, (double)f.eagle.reach);
    boomStep(f, 0.002f);   // across the planned time
    CHECK(f.phase == BP_CARRIED && (f.events & BE_SNATCHED), "seed %d: snatched at the planned time", seed);
    ++snatched;
    CHECK(dist(eagleTalons(f.eagle), f.boom.pos) < 0.01f, "seed %d: carried in the talons", seed);

    // Carry it off: in the talons every tick, out of the frame through the top, both gone for good.
    Rect last_vis{0, 0, 0, 0};
    int carry_off = 0;
    ticks = 0;
    while (!boomDone(f) && ticks < 25 * 30) {
      boomStep(f, SS_TICK_DT);
      ++ticks;
      if (f.events & BE_EAGLE_GONE) ++gone;
      if (f.events & BE_SNATCHED) ++snatched;
      if (f.phase == BP_CARRIED) {
        const float d = dist(eagleTalons(f.eagle), f.boom.pos);
        if (d > worst_carry) worst_carry = d;
        if (d > 0.01f) ++carry_off;
        const Rect eb = eagleBounds(f.eagle);
        if (!eb.empty()) last_vis = eb;
        if (overlaps(eb, kChip) || overlaps(boomerangBounds(f.boom), kChip)) ++bad_corner;
        if (overlaps(eb, him)) ++on_him;
        if (((f.eagle.pos.x - prev_eagle.x) * (float)f.eagle.facing) < 0.f) ++wrong_way;
        prev_eagle = f.eagle.pos;
      }
    }
    CHECK(thrown == 1 && snatched == 1 && gone == 1, "seed %d: events thrown %d snatched %d gone %d", seed, thrown, snatched, gone);
    CHECK(f.phase == BP_GONE && boomDone(f), "seed %d: ends gone (phase %d)", seed, (int)f.phase);
    CHECK(!f.eagle.on && !f.boom.on, "seed %d: both off", seed);
    CHECK(eagleBounds(f.eagle).empty() && boomerangBounds(f.boom).empty(), "seed %d: no bounds left to mark", seed);
    CHECK(carry_off == 0, "seed %d: %d ticks the boomerang was off the talons", seed, carry_off);
    // Bounds are clipped to the frame, so the last ones it had touch the edge it went out through.
    CHECK(last_vis.y0 == 0 || last_vis.x0 == 0 || last_vis.x1 == W, "seed %d: leaves through an edge (last bounds %d,%d..%d,%d)", seed,
          last_vis.x0, last_vis.y0, last_vis.x1, last_vis.y1);
    CHECK(pl.t_throw - sp.wind_up < 1.5f, "seed %d: lead before the throw %.2f s", seed, (double)(pl.t_throw - sp.wind_up));
    CHECK(bad_corner == 0, "seed %d: %d ticks into the chip's corner", seed, bad_corner);
    CHECK(wrong_way == 0, "seed %d: %d ticks the eagle moved against its facing", seed, wrong_way);
    CHECK(on_him == 0, "seed %d: the eagle overlapped him on %d ticks", seed, on_him);
    CHECK(fabsf(pl.e_m.x - sp.catch_pt.x) >= 0.5f * sp.reach, "seed %d: the grab is %.0f px from the catch point (reach %.0f)", seed,
          (double)fabsf(pl.e_m.x - sp.catch_pt.x), (double)sp.reach);
    CHECK(max_boom_step < 24.f, "seed %d: no jump in the boomerang, biggest step %.1f px", seed, (double)max_boom_step);
    const Boomerang b0 = f.boom;
    const Eagle e0 = f.eagle;
    for (int i = 0; i < 100; ++i) boomStep(f, SS_TICK_DT);
    CHECK(boomerangSame(b0, f.boom) && eagleSame(e0, f.eagle) && f.events == 0, "seed %d: stays gone", seed);
  }
  const int want_way = mirror ? 0 : 1;   // out along the loop: right in the scenario, left mirrored
  CHECK(facing_seen[want_way] >= 36, "faces the way of the loop in at least 36 of 40: left %d, right %d", facing_seen[0], facing_seen[1]);
  printf("  40 seeds: worst gap at the planned time %.2f px, worst gap while carried %.4f px, eagle faces left %d / right %d\n",
         (double)worst_meet, (double)worst_carry, facing_seen[0], facing_seen[1]);
  printf("  longest lead before the throw %.2f s, shortest glide-in %.2f s\n", (double)max_lead, (double)min_in);
}

static void testEagle() {
  eagleCase(false);
  eagleCase(true);
}

// ---- 4. ending C: it sticks ------------------------------------------------------------------------

static void testStick() {
  printf("C: stick\n");
  static const float kDirs[6] = {0.08f, 0.f, 0.15f, -0.08f, 0.5f, 0.35f};
  float worst_tip = 0.f;
  for (int seed = 1; seed <= 24; ++seed) {
    Rng rng;
    rng.seed((uint64_t)seed);
    BoomFlight f;
    BoomSpec sp = specFor(BOOM_STICK);
    sp.trunk_turns = kDirs[seed % 6];
    if (sp.trunk_turns == 0.5f || sp.trunk_turns == 0.35f) sp.trunk = Vec2{60.f, 300.f};   // a trunk the arm leans the other way off
    boomPlan(f, sp, rng);
    int stuck = 0, ticks = 0;
    Vec2 prev = f.boom.pos;
    float max_step = 0.f, min_catch = 1e9f, max_wobble = 0.f, hit_gap = 0.f;
    int bad_bounds = 0, tip_off = 0;
    const float rest = f.plan.rot_land;
    while (!boomDone(f) && ticks < 25 * 30) {
      boomStep(f, SS_TICK_DT);
      ++ticks;
      if (f.events & BE_STUCK) ++stuck;
      const float step = dist(prev, f.boom.pos);
      if (step > max_step) max_step = step;
      prev = f.boom.pos;
      if (f.phase == BP_FLY && f.t > f.plan.t_throw + 0.5f * f.plan.dur) {   // the way back: it starts at the hand
        const float dc = dist(f.boom.pos, sp.catch_pt);
        if (dc < min_catch) min_catch = dc;
      }
      if (f.phase == BP_STUCK) {
        const float g = dist(boomerangTip(f.boom, false), sp.trunk);
        if (g > worst_tip) worst_tip = g;
        if (g > 0.5f) ++tip_off;
        const float w = fabsf(f.boom.rot - rest);
        if (w > max_wobble) max_wobble = w;
        if (stuck == 1 && f.events & BE_STUCK) hit_gap = g;
      }
      const Rect b = boomerangBounds(f.boom);
      if (b.empty() || overlaps(b, kChip)) ++bad_bounds;
      CHECK(!f.eagle.on, "C has no eagle");
    }
    CHECK(stuck == 1 && f.phase == BP_STUCK && boomDone(f), "seed %d: ends stuck (phase %d, events %d)", seed, (int)f.phase, stuck);
    CHECK(tip_off == 0, "seed %d: the tip left the trunk point on %d ticks", seed, tip_off);
    CHECK(hit_gap <= 0.5f, "seed %d: tip %.2f px off at the hit", seed, (double)hit_gap);
    CHECK(f.boom.rot == rest, "seed %d: ends at its rest angle exactly", seed);
    CHECK(max_wobble > 0.02f, "seed %d: wobbles (%.3f turns)", seed, (double)max_wobble);
    CHECK(max_step < 24.f, "seed %d: no jump, biggest step %.1f px", seed, (double)max_step);
    CHECK(min_catch > 0.7f * sp.miss && min_catch < 1.5f * sp.miss, "seed %d: misses the catch point by %.0f px (miss %.0f)", seed, (double)min_catch,
          (double)sp.miss);
    CHECK(bad_bounds == 0, "seed %d: %d ticks with empty bounds or into the chip's corner", seed, bad_bounds);
    if (sp.trunk_turns > -0.05f && sp.trunk_turns < 0.2f) {   // leaning out to the right: the free arm hangs below the stuck one
      const float free_y = boomerangTip(f.boom, true).y;
      CHECK(free_y > sp.trunk.y + 4.f, "seed %d: free arm hangs below (y %.1f vs %.1f)", seed, (double)free_y, (double)sp.trunk.y);
    }
    const Boomerang held = f.boom;
    for (int i = 0; i < 750; ++i) boomStep(f, SS_TICK_DT);
    CHECK(boomerangSame(held, f.boom) && f.events == 0, "seed %d: still in the trunk 30 s later", seed);
  }
  printf("  24 seeds, 6 trunk angles: tip within %.3f px of the trunk point throughout\n", (double)worst_tip);
}

// ---- 5. bounds ---------------------------------------------------------------------------------------

static Eagle randomEagle(Rng& r, bool inside) {
  Eagle e{};
  e.on = true;
  e.facing = r.chance(0.5f) ? 1 : -1;
  const float x0 = inside ? 70.f : -60.f, x1 = inside ? 730.f : 860.f;
  const float y0 = inside ? 60.f : -60.f, y1 = inside ? 420.f : 540.f;
  e.pos.x = r.range(x0, x1);
  e.pos.y = r.range(y0, y1);
  e.pitch = r.range(-0.9f, 0.9f);
  e.flap = r.range(0.f, 1.f);
  e.beat = r.range(0.f, 1.f);
  e.flare = r.range(0.f, 1.f);
  e.fan = r.range(0.f, 1.f);
  e.reach = r.range(0.f, 1.f);
  return e;
}

static void testBounds() {
  printf("bounds cover every drawn pixel\n");
  Frame f(W, H, g_bg);
  BoundsStats sb, se;
  // Along every ending's flight, every tick.
  for (int ending = 0; ending < 3; ++ending) {
    for (int seed = 1; seed <= 6; ++seed) {
      BoomFlight fl = planned(ending, (uint64_t)seed);
      for (int i = 0; i < 25 * 14; ++i) {
        boundsBoomerang(sb, f, fl.boom);
        boundsEagle(se, f, fl.eagle);
        boomStep(fl, SS_TICK_DT);
      }
    }
  }
  // Random poses over the whole parameter space, in the frame and across its edges.
  Rng r;
  r.seed(99);
  for (int i = 0; i < 1500; ++i) {
    const bool inside = (i & 1) == 0;
    Boomerang b{true, r.chance(0.5f) ? (int8_t)1 : (int8_t)-1, Vec2{inside ? r.range(40.f, 760.f) : r.range(-30.f, 830.f),
                                                                      inside ? r.range(40.f, 440.f) : r.range(-30.f, 510.f)},
                r.range(-3.f, 3.f)};
    boundsBoomerang(sb, f, b);
    boundsEagle(se, f, randomEagle(r, inside));
  }
  printf("  boomerang: %d states, %d pixels drawn, %d outside the bounds\n", sb.states, sb.drawn, sb.outside);
  printf("  eagle:     %d states, %d pixels drawn, %d outside the bounds\n", se.states, se.drawn, se.outside);
  CHECK(sb.outside == 0 && sb.empty_but_drawn == 0, "boomerang drew %d pixels outside its bounds", sb.outside);
  CHECK(se.outside == 0 && se.empty_but_drawn == 0, "eagle drew %d pixels outside its bounds", se.outside);
  CHECK(sb.drawn > 20000 && se.drawn > 100000, "the test actually drew things (%d, %d)", sb.drawn, se.drawn);

  // Off, or off screen: empty.
  Boomerang off{false, 1, Vec2{400.f, 240.f}, 0.f};
  Eagle eoff{};
  CHECK(boomerangBounds(off).empty() && eagleBounds(eoff).empty(), "an element that is off has empty bounds");
  Eagle up{true, 1, Vec2{400.f, -200.f}, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.f};
  CHECK(eagleBounds(up).empty(), "an eagle far above the frame has empty bounds");
  // A tight box: not looser than the primitives need (about 100 x 70 px at most).
  Eagle big{true, 1, Vec2{400.f, 240.f}, 0.f, 0.25f, 1.f, 1.f, 1.f, 1.f};
  const Rect rb = eagleBounds(big);
  const Rect bb = boomerangBounds(Boomerang{true, 1, Vec2{400.f, 240.f}, 0.1f});
  printf("  eagle bounds at the flare: %d x %d px; boomerang bounds: %d x %d px\n", rb.x1 - rb.x0, rb.y1 - rb.y0, bb.x1 - bb.x0, bb.y1 - bb.y0);
  CHECK(rb.x1 - rb.x0 <= 110 && rb.y1 - rb.y0 <= 80, "eagle bounds %d x %d", rb.x1 - rb.x0, rb.y1 - rb.y0);
}

// ---- 6. strips equal full frames; nothing outside the clip -----------------------------------------------

// The biggest difference in any of the three RGB565 channels, in steps of that channel.
static int channelGap(Pixel a, Pixel b) {
  const int dr = (int)(a >> 11) - (int)(b >> 11);
  const int dg = (int)((a >> 5) & 63) - (int)((b >> 5) & 63);
  const int db = (int)(a & 31) - (int)(b & 31);
  int m = dr < 0 ? -dr : dr;
  if ((dg < 0 ? -dg : dg) > m) m = dg < 0 ? -dg : dg;
  if ((db < 0 ? -db : db) > m) m = db < 0 ? -db : db;
  return m;
}

static void testStripsAndClip() {
  printf("strips equal the full frame; nothing outside the clip\n");
  Rng r;
  r.seed(5);
  int states = 0, strips = 0, mismatches = 0, clip_leaks = 0, clip_mismatch = 0, clip_lsb = 0, clips = 0;
  std::vector<Pixel> sbuf((size_t)W * SS_STRIP_ROWS);
  // A varied set of states: every ending along its flight, plus random poses straddling strip edges.
  std::vector<BoomFlight> states_v;
  for (int ending = 0; ending < 3; ++ending) {
    BoomFlight fl = planned(ending, 3 + (uint64_t)ending);
    for (int i = 0; i < 25 * 9; ++i) {
      if (i % 4 == 0) states_v.push_back(fl);
      boomStep(fl, SS_TICK_DT);
    }
  }
  for (int i = 0; i < 80; ++i) {
    BoomFlight fl{};
    fl.boom = Boomerang{true, r.chance(0.5f) ? (int8_t)1 : (int8_t)-1, Vec2{r.range(20.f, 780.f), r.range(20.f, 460.f)}, r.range(0.f, 1.f)};
    fl.eagle = randomEagle(r, false);
    states_v.push_back(fl);
  }
  for (size_t si = 0; si < states_v.size(); ++si) {
    const BoomFlight& fl = states_v[si];
    Frame full(W, H, g_bg);
    boomDraw(fl, full.s, full.all(), g_pal);
    ++states;
    for (int i = 0; i < SS_STRIP_COUNT; ++i) {
      const int y0 = i * SS_STRIP_ROWS;
      for (Pixel& p : sbuf) p = g_bg;
      Surface strip{sbuf.data(), W, SS_STRIP_ROWS, W, y0};
      boomDraw(fl, strip, Rect{0, y0, W, y0 + SS_STRIP_ROWS}, g_pal);
      ++strips;
      for (int y = y0; y < y0 + SS_STRIP_ROWS; ++y)
        if (memcmp(strip.row(y), full.s.row(y), (size_t)W * sizeof(Pixel)) != 0) {
          ++mismatches;
          break;
        }
    }
    // A clip, drawn into a frame that starts as the background: nothing outside it, and inside it the full draw.
    // Full-width clips (what every strip is) must match exactly. A clip narrowed in x may differ by one step in
    // one colour channel at a pixel: the rasterizer's coverage sums start at the clip's left edge, so a float
    // rounds the other way. Mark's critters do the same (22 of 3000 draws in a check with this clip shape), and the
    // renderer never clips in x, so that is allowed here and nothing more.
    for (int k = 0; k < 4; ++k) {
      Rect c;
      if (fl.boom.on && k == 0) {   // around the boomerang, cutting through it
        c = Rect{(int)fl.boom.pos.x - 3, (int)fl.boom.pos.y - 5, (int)fl.boom.pos.x + 4, (int)fl.boom.pos.y + 6};
      } else if (fl.eagle.on && k == 1) {
        c = Rect{(int)fl.eagle.pos.x - 9, (int)fl.eagle.pos.y - 14, (int)fl.eagle.pos.x + 17, (int)fl.eagle.pos.y + 9};
      } else if (k == 3) {   // a band of rows, full width, through whatever is there
        const int y0 = (int)(fl.boom.on ? fl.boom.pos.y : fl.eagle.pos.y) - r.rangeInt(0, 6);
        c = Rect{0, y0, W, y0 + r.rangeInt(1, 14)};
      } else {
        const int x0 = r.rangeInt(0, W - 2), y0 = r.rangeInt(0, H - 2);
        c = Rect{x0, y0, x0 + r.rangeInt(1, 200), y0 + r.rangeInt(1, 120)};
      }
      Frame part(W, H, g_bg);
      boomDraw(fl, part.s, c, g_pal);
      ++clips;
      const Rect cc = c.clipped(kFrame);
      const bool full_width = c.x0 <= 0 && c.x1 >= W;
      bool leak = false, big = false, lsb = false;
      for (int y = 0; y < H; ++y) {
        if (memcmp(part.s.row(y), full.s.row(y), (size_t)W * sizeof(Pixel)) == 0) continue;
        for (int x = 0; x < W; ++x) {
          const Pixel a = part.at(x, y), b = full.at(x, y);
          const bool in = x >= cc.x0 && x < cc.x1 && y >= cc.y0 && y < cc.y1;
          if (!in && a != g_bg) leak = true;
          if (in && a != b) {
            if (full_width || channelGap(a, b) > 1) big = true;
            else lsb = true;
          }
        }
      }
      if (leak) ++clip_leaks;
      if (big) ++clip_mismatch;
      if (lsb) ++clip_lsb;
    }
  }
  printf("  %d states x %d strips: %d differ from the full frame; %d clipped draws: %d leaked, %d differ inside (%d more by one step in a channel, x-narrowed only)\n",
         states, SS_STRIP_COUNT, mismatches, clips, clip_leaks, clip_mismatch, clip_lsb);
  CHECK(mismatches == 0, "%d of %d strips differ from the full-frame draw", mismatches, strips);
  CHECK(clip_leaks == 0, "%d draws touched pixels outside their clip", clip_leaks);
  CHECK(clip_mismatch == 0, "%d clipped draws differ inside the clip", clip_mismatch);
}

// ---- 7. long runs: nothing goes NaN ------------------------------------------------------------------------

static BoomSpec randomSpec(Rng& r) {
  BoomSpec s;
  s.ending = (uint8_t)r.rangeInt(0, 3);   // 3 is invalid: it must act like A
  s.hand = Vec2{r.range(-50.f, 850.f), r.range(-50.f, 530.f)};
  s.catch_pt = Vec2{r.range(-50.f, 850.f), r.range(-50.f, 530.f)};
  s.trunk = Vec2{r.range(-50.f, 850.f), r.range(-50.f, 530.f)};
  s.trunk_turns = r.range(-1.f, 2.f);
  s.reach = r.chance(0.1f) ? 0.f : r.range(-320.f, 320.f);
  s.rise = r.range(-30.f, 220.f);
  s.dip = r.range(-60.f, 120.f);
  s.seconds = r.range(-1.f, 9.f);
  s.wind_up = r.range(-1.f, 4.f);
  s.miss = r.range(-30.f, 90.f);
  s.hand_turns = r.range(-1.f, 2.f);
  return s;
}

static bool sane(const BoomFlight& f) {
  const Boomerang& b = f.boom;
  const Eagle& e = f.eagle;
  if (!finiteV(b.pos) || !finite(b.rot) || !finiteV(e.pos) || !finite(e.pitch) || !finite(e.flap) || !finite(e.beat) ||
      !finite(e.flare) || !finite(e.fan) || !finite(e.reach) || !finite(f.t))
    return false;
  const Rect rb = boomerangBounds(b), re = eagleBounds(e);
  return rb.x0 >= 0 && rb.x1 <= W && rb.y0 >= 0 && rb.y1 <= H && re.x0 >= 0 && re.x1 <= W && re.y0 >= 0 && re.y1 <= H;
}

static void testNoNaN() {
  printf("long runs\n");
  Rng r;
  r.seed(2026);
  static const float kDts[6] = {SS_TICK_DT, SS_TICK_DT, 0.001f, 0.5f, 0.f, 7.f};
  int runs = 0, ticks = 0, bad = 0, ended[3] = {0, 0, 0};
  Frame f(W, H, g_bg);
  for (int i = 0; i < 600; ++i) {
    const BoomSpec sp = randomSpec(r);
    Rng pr;
    pr.seed(r.next());
    BoomFlight fl;
    boomPlan(fl, sp, pr);
    const float dt = kDts[i % 6];
    const int n = dt >= 1.f ? 40 : (dt >= 0.1f ? 400 : (int)(60.f / (dt > 0.f ? dt : 0.04f)));
    for (int k = 0; k < n && !bad; ++k) {
      boomStep(fl, k % 97 == 0 && dt == SS_TICK_DT ? 0.f : dt);
      ++ticks;
      if (!sane(fl)) {
        ++bad;
        printf("  insane state: run %d ending %d tick %d t %.3f\n", i, (int)sp.ending, k, (double)fl.t);
      }
      if (k % 31 == 0) boomDraw(fl, f.s, f.all(), g_pal);   // draws must not crash on anything
    }
    // And one long step: 60 s straight into the end.
    if (dt == SS_TICK_DT) {
      boomStep(fl, 60.f);
      if (!sane(fl)) ++bad;
      CHECK(boomDone(fl) || fl.phase == BP_FLY || fl.phase == BP_WAIT, "run %d: odd phase", i);
      if (boomDone(fl)) ++ended[fl.phase == BP_HAND ? 0 : (fl.phase == BP_GONE ? 1 : 2)];
    }
    ++runs;
    // The draws above left pixels behind; wipe them so the next run starts clean.
    for (Pixel& p : f.px) p = g_bg;
  }
  printf("  %d random plans (bad geometry, negative times, invalid endings, dt 0 to 7 s), %d steps, %d bad states; long step ended: A %d, B %d, C %d\n",
         runs, ticks, bad, ended[0], ended[1], ended[2]);
  CHECK(bad == 0, "%d non-finite or out-of-frame bounds states", bad);
  CHECK(ended[0] > 20 && ended[1] > 20 && ended[2] > 20, "all endings covered");
}

// ---- 8. dirty strips a flight costs ---------------------------------------------------------------------------

static uint16_t rowMask(const Rect& r) {
  if (r.empty()) return 0;
  const int y0 = r.y0 < 0 ? 0 : r.y0, y1 = r.y1 > H ? H : r.y1;
  if (y1 <= y0) return 0;
  uint16_t m = 0;
  for (int s = y0 / SS_STRIP_ROWS; s <= (y1 - 1) / SS_STRIP_ROWS; ++s) m = (uint16_t)(m | (1u << s));
  return m;
}
static int bits(uint16_t m) {
  int n = 0;
  for (; m; m = (uint16_t)(m & (m - 1))) ++n;
  return n;
}

// The World's `track`: an element marks where it was and where it is when its
// state or its bounds changed, and marks nothing when it did not.
struct Tracker {
  Boomerang bs;
  Eagle es;
  Rect bb{0, 0, 0, 0}, eb{0, 0, 0, 0};
  explicit Tracker(const BoomFlight& f) : bs(f.boom), es(f.eagle) {
    bb = boomerangBounds(f.boom);
    eb = eagleBounds(f.eagle);
  }
  int frame(const BoomFlight& f) {
    uint16_t m = 0;
    const Rect nb = boomerangBounds(f.boom);
    if (!boomerangSame(bs, f.boom) || !sameRect(nb, bb)) {
      m = (uint16_t)(m | rowMask(bb) | rowMask(nb));
      bb = nb;
      bs = f.boom;
    }
    const Rect ne = eagleBounds(f.eagle);
    if (!eagleSame(es, f.eagle) || !sameRect(ne, eb)) {
      m = (uint16_t)(m | rowMask(eb) | rowMask(ne));
      eb = ne;
      es = f.eagle;
    }
    return bits(m);
  }
};

static void testDirtyStrips() {
  printf("dirty strips per frame (25 Hz, old and new bounds, of %d)\n", SS_STRIP_COUNT);
  static const char* kNames[3] = {"A catch", "B eagle", "C stick"};
  static const float kAvgMax[3] = {3.0f, 5.5f, 3.0f};
  static const int kMaxMax[3] = {4, 7, 4};
  for (int ending = 0; ending < 3; ++ending) {
    double sum_flight = 0.0;
    long n_flight = 0;
    int worst = 0;
    long after_marks = 0;
    for (int seed = 1; seed <= 16; ++seed) {
      BoomFlight f = planned(ending, (uint64_t)seed);
      Tracker tr(f);
      int ticks = 0;
      int waits = 0;
      while (!boomDone(f) && ticks < 25 * 30) {
        boomStep(f, SS_TICK_DT);
        ++ticks;
        const int n = tr.frame(f);
        if (f.phase == BP_WAIT) waits += n;
        if (f.phase != BP_WAIT || n > 0) {
          sum_flight += n;
          ++n_flight;
        }
        if (n > worst) worst = n;
      }
      // B is excused: the eagle may be gliding in while the boomerang still waits (the lead).
      if (ending != BOOM_EAGLE) CHECK(waits == 0, "seed %d: %d strips marked while it waits in the hand", seed, waits);
      for (int i = 0; i < 50; ++i) {
        boomStep(f, SS_TICK_DT);
        after_marks += tr.frame(f);
      }
    }
    const double avg = sum_flight / (double)n_flight;
    printf("  %s: average %.2f strips a frame over the flight, worst frame %d; after it ends: %ld\n", kNames[ending], avg, worst, after_marks);
    CHECK(avg <= (double)kAvgMax[ending], "%s: average %.2f strips a frame", kNames[ending], avg);
    CHECK(worst <= kMaxMax[ending], "%s: worst frame %d strips", kNames[ending], worst);
    CHECK(after_marks == 0, "%s: %ld strips marked after the end (it must cost nothing)", kNames[ending], after_marks);
  }
}

// ---- 9. determinism -----------------------------------------------------------------------------------------------

static uint32_t hashFrame(const Frame& f) {
  uint32_t h = 2166136261u;
  for (Pixel p : f.px) h = (h ^ p) * 16777619u;
  return h;
}

static void testDeterminism() {
  printf("determinism\n");
  // A plan draws ten values from the Rng whatever the ending.
  uint32_t after[3];
  for (int ending = 0; ending < 3; ++ending) {
    Rng r;
    r.seed(77);
    BoomFlight f;
    boomPlan(f, specFor(ending), r);
    after[ending] = r.next();
  }
  Rng ref;
  ref.seed(77);
  for (int i = 0; i < 10; ++i) ref.next();
  const uint32_t want = ref.next();
  CHECK(after[0] == want && after[1] == want && after[2] == want, "boomPlan draws exactly ten values for every ending");

  for (int ending = 0; ending < 3; ++ending) {
    uint32_t hashes[2][40];
    for (int run = 0; run < 2; ++run) {
      BoomFlight f = planned(ending, 41);
      Frame fr(W, H, g_bg);
      for (int i = 0; i < 40; ++i) {
        for (int k = 0; k < 4; ++k) boomStep(f, SS_TICK_DT);
        boomDraw(f, fr.s, fr.all(), g_pal);
        hashes[run][i] = hashFrame(fr);
        for (Pixel& p : fr.px) p = g_bg;
      }
    }
    CHECK(memcmp(hashes[0], hashes[1], sizeof(hashes[0])) == 0, "ending %d: two runs from one seed draw the same frames", ending);
  }
  // Different seeds give different flights (the roll is used).
  BoomFlight a = planned(BOOM_EAGLE, 1), b = planned(BOOM_EAGLE, 2);
  CHECK(a.plan.t_land != b.plan.t_land || a.plan.spin != b.plan.spin, "seeds 1 and 2 plan differently");
  // The same plan stepped in different dts reaches the same place (the motion is analytic).
  BoomFlight p1 = planned(BOOM_CATCH, 9), p2 = planned(BOOM_CATCH, 9);
  for (int i = 0; i < 50; ++i) boomStep(p1, 0.04f);   // 2.0 s
  for (int i = 0; i < 200; ++i) boomStep(p2, 0.01f);  // 2.0 s
  CHECK(dist(p1.boom.pos, p2.boom.pos) < 0.5f && fabsf(p1.boom.rot - p2.boom.rot) < 0.01f, "dt 0.04 and 0.01 agree (%.3f px)", (double)dist(p1.boom.pos, p2.boom.pos));
}

// ---- 10. look: the spin and the wingbeat -----------------------------------------------------------------------------

static void testLook() {
  printf("look\n");
  // The spin: turns a second, and per tick, in flight.
  BoomFlight f = planned(BOOM_CATCH, 3);
  float r0 = 0.f, r1 = 0.f;
  while (f.phase != BP_FLY) boomStep(f, SS_TICK_DT);
  for (int i = 0; i < 25; ++i) boomStep(f, SS_TICK_DT);
  r0 = f.boom.rot;
  boomStep(f, SS_TICK_DT);
  r1 = f.boom.rot;
  const float per_tick = fabsf(r1 - r0);
  printf("  spin: %.0f degrees a tick (%.1f turns a second)\n", (double)(per_tick * 360.f), (double)(per_tick * 25.f));
  CHECK(per_tick > 0.08f && per_tick < 0.2f, "spin %.3f turns a tick", (double)per_tick);

  // The wingbeat: slow and deep. Time the wraps of the flap phase over the whole glide and the carry.
  BoomFlight g = planned(BOOM_EAGLE, 4);
  int wraps = 0;
  float prev = 0.f, t_first = 0.f, t_last = 0.f;
  while (g.phase != BP_GONE && g.t < 40.f) {
    boomStep(g, SS_TICK_DT);
    if (!g.eagle.on) continue;
    const float flap = g.eagle.flap - floorf(g.eagle.flap);
    if (prev > 0.9f && flap < 0.1f) {
      if (wraps == 0) t_first = g.t;
      t_last = g.t;
      ++wraps;
    }
    prev = flap;
  }
  CHECK(wraps >= 3, "the eagle beats its wings at least three times in view (%d)", wraps);
  const float period = wraps >= 2 ? (t_last - t_first) / (float)(wraps - 1) : 0.f;
  Eagle e{true, 1, Vec2{400.f, 240.f}, 0.f, 0.25f, 1.f, 0.f, 0.5f, 0.f};
  const Rect up = eagleBounds(e);
  e.flap = 0.75f;
  const Rect down = eagleBounds(e);
  printf("  wingbeat: %d beats in view, one every %.2f s; wing tips at the top of the stroke y %d, at the bottom y %d\n", wraps,
         (double)period, up.y0, down.y0);
  CHECK(period > 1.0f && period < 1.4f, "one beat every %.2f s: slow", (double)period);
  CHECK(up.y0 < down.y0 - 8, "wings travel at least 8 px between the top and the bottom of the stroke");
}

// ---- PPM, contact sheets -----------------------------------------------------------------------------------------------

struct Img {
  int w = 0, h = 0;
  std::vector<uint8_t> rgb;
  void init(int ww, int hh, uint8_t g) {
    w = ww;
    h = hh;
    rgb.assign((size_t)w * (size_t)h * 3, g);
  }
};

static bool writePPM(const char* path, const Img& im) {
  FILE* fp = fopen(path, "wb");
  if (!fp) return false;
  fprintf(fp, "P6\n%d %d\n255\n", im.w, im.h);
  fwrite(im.rgb.data(), 1, im.rgb.size(), fp);
  return fclose(fp) == 0;
}

static void putPx(Img& im, int x, int y, Pixel p) {
  uint8_t* d = &im.rgb[((size_t)y * (size_t)im.w + (size_t)x) * 3];
  d[0] = red8(p);
  d[1] = green8(p);
  d[2] = blue8(p);
}

// `div` 2: average each 2x2 block; `mul` 3: repeat each pixel 3x3 (nearest, to show the real pixels).
static Img tileFrom(const Frame& f, int div, int mul, int cx0, int cy0, int cw, int ch) {
  Img t;
  if (div > 1) {
    t.init(cw / div, ch / div, 0);
    for (int y = 0; y < t.h; ++y)
      for (int x = 0; x < t.w; ++x) {
        int r = 0, g = 0, b = 0;
        for (int j = 0; j < div; ++j)
          for (int i = 0; i < div; ++i) {
            const Pixel p = f.at(cx0 + x * div + i, cy0 + y * div + j);
            r += red8(p);
            g += green8(p);
            b += blue8(p);
          }
        uint8_t* d = &t.rgb[((size_t)y * (size_t)t.w + (size_t)x) * 3];
        d[0] = (uint8_t)(r / (div * div));
        d[1] = (uint8_t)(g / (div * div));
        d[2] = (uint8_t)(b / (div * div));
      }
  } else {
    t.init(cw * mul, ch * mul, 0);
    for (int y = 0; y < t.h; ++y)
      for (int x = 0; x < t.w; ++x) putPx(t, x, y, f.at(cx0 + x / mul, cy0 + y / mul));
  }
  return t;
}

// Crop or pad a tile to w x h (dark grey), so tiles of different zooms share a grid.
static Img fitTile(const Img& in, int w, int h) {
  Img out;
  out.init(w, h, 40);
  const int cw = in.w < w ? in.w : w, ch = in.h < h ? in.h : h;
  for (int y = 0; y < ch; ++y) memcpy(&out.rgb[(size_t)y * (size_t)w * 3], &in.rgb[(size_t)y * (size_t)in.w * 3], (size_t)cw * 3);
  return out;
}

static Img gridOf(const std::vector<Img>& tiles, int cols) {
  const int gap = 4;
  const int tw = tiles[0].w, th = tiles[0].h;
  const int rows = ((int)tiles.size() + cols - 1) / cols;
  Img s;
  s.init(cols * tw + (cols + 1) * gap, rows * th + (rows + 1) * gap, 40);
  for (size_t i = 0; i < tiles.size(); ++i) {
    const int x0 = gap + ((int)i % cols) * (tw + gap), y0 = gap + ((int)i / cols) * (th + gap);
    for (int y = 0; y < th; ++y) memcpy(&s.rgb[((size_t)(y0 + y) * (size_t)s.w + (size_t)x0) * 3], &tiles[i].rgb[(size_t)y * (size_t)tw * 3], (size_t)tw * 3);
  }
  return s;
}

// The plain scene: sky bands, ridges, tree line, ground, a stream corner, the
// pine at the trunk point and a stand-in for him, all in palette slots.
static void plainScene(Frame& f) {
  const Palette& p = g_pal;
  const Rect all = f.all();
  static const Slot kSky[5] = {SKY0, SKY1, SKY2, SKY3, SKY4};
  for (int i = 0; i < 5; ++i) fillRect(f.s, Rect{0, i * 62, W, i * 62 + 62}, p[kSky[i]], all);
  fillRect(f.s, Rect{0, 310, W, H}, p[GROUND], all);
  const Vec2 far_r[8] = {{0.f, 310.f}, {0.f, 260.f}, {140.f, 225.f}, {300.f, 262.f}, {470.f, 232.f}, {640.f, 268.f}, {800.f, 240.f}, {800.f, 310.f}};
  fillPolygon(f.s, far_r, 8, p[RIDGE_FAR], all);
  const Vec2 mid_r[7] = {{0.f, 320.f}, {0.f, 285.f}, {180.f, 262.f}, {380.f, 290.f}, {590.f, 268.f}, {800.f, 288.f}, {800.f, 320.f}};
  fillPolygon(f.s, mid_r, 7, p[RIDGE_MID], all);
  for (int i = 0; i < 11; ++i) {   // a tree line
    const float x = 40.f + (float)i * 72.f;
    const Vec2 t[3] = {{x - 16.f, 330.f}, {x, 262.f + (float)((i * 37) % 24)}, {x + 16.f, 330.f}};
    fillPolygon(f.s, t, 3, p[TREES_MID], all);
  }
  fillRect(f.s, Rect{0, 326, W, 346}, p[GRASS], all);
  const Vec2 water[4] = {{440.f, 480.f}, {560.f, 405.f}, {800.f, 385.f}, {800.f, 480.f}};
  fillPolygon(f.s, water, 4, p[WATER], all);
  // The pine whose right face is at the trunk point.
  fillRect(f.s, Rect{69, 190, 77, 440}, p[TRUNK], all);
  for (int i = 0; i < 4; ++i) {
    const float w = 22.f + (float)i * 14.f, y = 90.f + (float)i * 42.f;
    const Vec2 t[3] = {{73.f - w, y + 60.f}, {73.f, y - 20.f}, {73.f + w, y + 60.f}};
    fillPolygon(f.s, t, 3, p[i & 1 ? FOLIAGE : FOLIAGE_DARK], all);
  }
  // Him: legs, body, head, and the arm out to the hand.
  strokeCapsule(f.s, Vec2{404.f, 392.f}, Vec2{406.f, 342.f}, 12.f, p[ACTOR], all);
  fillCircle(f.s, Vec2{406.f, 322.f}, 7.f, p[ACTOR], all);
  strokeCapsule(f.s, Vec2{406.f, 352.f}, Vec2{417.f, 347.f}, 4.f, p[ACTOR], all);
}

static bool loadPPM(const char* path, Frame& f) {
  FILE* fp = fopen(path, "rb");
  if (!fp) return false;
  int w = 0, h = 0, mx = 0;
  if (fscanf(fp, "P6 %d %d %d", &w, &h, &mx) != 3 || w != W || h != H || mx != 255) {
    fclose(fp);
    return false;
  }
  fgetc(fp);
  std::vector<uint8_t> rgb((size_t)w * (size_t)h * 3);
  const size_t got = fread(rgb.data(), 1, rgb.size(), fp);
  fclose(fp);
  if (got != rgb.size()) return false;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const uint8_t* d = &rgb[((size_t)y * (size_t)W + (size_t)x) * 3];
      f.s.row(y)[x] = rgb565(d[0], d[1], d[2]);
    }
  return true;
}

static bool g_ok_sheets = true;

static void flightSheet(const char* path, int ending, uint64_t seed, const Frame& bg) {
  BoomFlight f = planned(ending, seed);
  const BoomFlight f0 = f;
  std::vector<Img> tiles;
  Frame fr = bg;
  const float kDt = 0.25f;
  int after = 0;
  for (int i = 0; i < 60; ++i) {
    fr = bg;
    boomDraw(f, fr.s, fr.all(), g_pal);
    tiles.push_back(tileFrom(fr, 2, 1, 0, 0, W, H));
    if (boomDone(f) && ++after >= 2) break;
    boomStep(f, kDt);
  }
  // The strobe: the boomerang every 0.1 s, the eagle every 0.4 s, on one frame.
  Frame st = bg;
  BoomFlight g = f0;
  for (int i = 0; i < 120 && !boomDone(g); ++i) {
    boomerangDraw(g.boom, st.s, st.all(), g_pal);
    if (i % 4 == 0) eagleDraw(g.eagle, st.s, st.all(), g_pal);
    boomStep(g, 0.1f);
  }
  boomerangDraw(g.boom, st.s, st.all(), g_pal);
  tiles.push_back(tileFrom(st, 2, 1, 0, 0, W, H));
  // The ending at native pixels, 400 x 240 each: 4x on the catch and on the trunk; 3x on the grab, just
  // before it (the flare) and just after (the carry), since the eagle is 50 px across.
  {
    struct Look {
      float at;     // seconds after the plan's end of flight
      Vec2 centre;
      int mul, cw, ch;
    };
    BoomFlight d = f0;
    const BoomPlan& pl = d.plan;
    Look looks[2];
    int n = 0;
    if (ending == BOOM_EAGLE) {
      const Vec2 c{pl.e_m.x - (float)pl.facing * 16.f, pl.e_m.y - 8.f};
      looks[n++] = Look{-0.25f, c, 3, 134, 80};
      looks[n++] = Look{0.12f, c, 3, 134, 80};
    } else if (ending == BOOM_STICK) {
      looks[n++] = Look{0.12f, Vec2{pl.stick.x + 22.f, pl.stick.y}, 4, 100, 60};
    } else {
      looks[n++] = Look{0.6f, Vec2{pl.k[pl.nk - 1].x, pl.k[pl.nk - 1].y}, 4, 100, 60};
    }
    for (int i = 0; i < n; ++i) {
      d = f0;
      boomStep(d, pl.t_land + looks[i].at);
      Frame df = bg;
      boomDraw(d, df.s, df.all(), g_pal);
      int cx0 = (int)looks[i].centre.x - looks[i].cw / 2, cy0 = (int)looks[i].centre.y - looks[i].ch / 2;
      cx0 = cx0 < 0 ? 0 : (cx0 > W - looks[i].cw ? W - looks[i].cw : cx0);
      cy0 = cy0 < 0 ? 0 : (cy0 > H - looks[i].ch ? H - looks[i].ch : cy0);
      tiles.push_back(fitTile(tileFrom(df, 1, looks[i].mul, cx0, cy0, looks[i].cw, looks[i].ch), 400, 240));
    }
  }
  const Img sheet = gridOf(tiles, 4);
  if (!writePPM(path, sheet)) {
    printf("  could not write %s\n", path);
    g_ok_sheets = false;
  } else {
    printf("  wrote %s: %zu tiles, %dx%d\n", path, tiles.size(), sheet.w, sheet.h);
  }
}

static void eagleSheet(const char* path) {
  const int cw = 104, ch = 76;
  std::vector<Img> tiles;
  auto scene = [&](Frame& z) {   // sky on the left, dark trees on the right: the eagle straddles both
    fillRect(z.s, Rect{0, 0, cw / 2, ch}, g_pal[SKY2], z.all());
    fillRect(z.s, Rect{cw / 2, 0, cw, ch}, g_pal[TREES_MID], z.all());
  };
  Eagle e{true, 1, Vec2{54.f, 36.f}, 0.04f, 0.f, 1.f, 0.f, 0.5f, 0.2f};
  for (int i = 0; i < 12; ++i) {
    Frame z(cw, ch, g_bg);
    scene(z);
    e.flap = (float)i / 12.f;
    eagleDraw(e, z.s, z.all(), g_pal);
    tiles.push_back(tileFrom(z, 1, 3, 0, 0, cw, ch));
  }
  {   // cruising: shallow beats, tail part closed
    Frame z(cw, ch, g_bg);
    scene(z);
    Eagle c{true, 1, Vec2{52.f, 38.f}, -0.15f, 0.f, 0.35f, 0.f, 0.3f, 0.f};
    eagleDraw(c, z.s, z.all(), g_pal);
    tiles.push_back(tileFrom(z, 1, 3, 0, 0, cw, ch));
  }
  {   // the flare: wings up, tail spread, feet reaching, the boomerang in reach
    Frame z(cw, ch, g_bg);
    scene(z);
    Eagle c{true, 1, Vec2{50.f, 26.f}, -0.25f, 0.25f, 0.9f, 1.f, 1.f, 0.85f};
    eagleDraw(c, z.s, z.all(), g_pal);
    const Vec2 tal = eagleTalons(c);
    boomerangDraw(Boomerang{true, 1, Vec2{tal.x, tal.y + 6.f}, 0.8f}, z.s, z.all(), g_pal);
    tiles.push_back(tileFrom(z, 1, 3, 0, 0, cw, ch));
  }
  {   // carrying it off: climbing, power stroke, the boomerang hanging from its feet
    Frame z(cw, ch, g_bg);
    scene(z);
    Eagle c{true, 1, Vec2{50.f, 28.f}, 0.5f, 0.45f, 1.f, 0.f, 0.3f, 0.9f};
    const Vec2 tal = eagleTalons(c);
    boomerangDraw(Boomerang{true, 1, tal, 0.25f}, z.s, z.all(), g_pal);
    eagleDraw(c, z.s, z.all(), g_pal);
    tiles.push_back(tileFrom(z, 1, 3, 0, 0, cw, ch));
  }
  {   // facing left, mid downstroke
    Frame z(cw, ch, g_bg);
    scene(z);
    Eagle c{true, -1, Vec2{52.f, 36.f}, 0.1f, 0.62f, 1.f, 0.f, 0.5f, 0.2f};
    eagleDraw(c, z.s, z.all(), g_pal);
    tiles.push_back(tileFrom(z, 1, 3, 0, 0, cw, ch));
  }
  const Img sheet = gridOf(tiles, 4);
  if (!writePPM(path, sheet)) {
    printf("  could not write %s\n", path);
    g_ok_sheets = false;
  } else {
    printf("  wrote %s: %zu tiles at 3x, %dx%d\n", path, tiles.size(), sheet.w, sheet.h);
  }
}

static void testSheets(const char* dir, const char* bgfile) {
  printf("contact sheets\n");
  Frame bg(W, H, g_bg);
  if (bgfile && loadPPM(bgfile, bg)) printf("  background: %s\n", bgfile);
  else {
    if (bgfile) printf("  could not read %s as an 800x480 P6; using the plain scene\n", bgfile);
    plainScene(bg);
  }
  char path[512];
  snprintf(path, sizeof path, "%s/boomerang_a.ppm", dir);
  flightSheet(path, BOOM_CATCH, 1, bg);
  snprintf(path, sizeof path, "%s/boomerang_b.ppm", dir);
  flightSheet(path, BOOM_EAGLE, 1, bg);
  snprintf(path, sizeof path, "%s/boomerang_c.ppm", dir);
  flightSheet(path, BOOM_STICK, 1, bg);
  snprintf(path, sizeof path, "%s/eagle_zoom.ppm", dir);
  eagleSheet(path);
  CHECK(g_ok_sheets, "all sheets written");
}

int main(int argc, char** argv) {
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  const char* bgfile = nullptr;
  const char* dir = ".";
  float mood = 3.f;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--bg") && i + 1 < argc) bgfile = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) dir = argv[++i];
    else if (!strcmp(argv[i], "--mood") && i + 1 < argc) mood = (float)atof(argv[++i]);
  }
  initPalette(3.f);   // the checks run on the day palette; --mood only changes the sheets
  testSizes();
  testCatch();
  testEagle();
  testStick();
  testBounds();
  testStripsAndClip();
  testNoNaN();
  testDirtyStrips();
  testDeterminism();
  testLook();
  initPalette(mood);
  testSheets(dir, bgfile);
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
