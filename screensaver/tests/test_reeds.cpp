// Reeds and cattails tests (stage 22 step B1, the plants: scene/reeds.h; CATTAILS_PLAN.md section 10 as
// INTEGRATION_PLAN.md and REVIEW_FIXES.md changed it; Mike, 2026-11-18).
//
// - sites over seeds 1 to 300: on their waterlines, in frame, out of the chip's box and the quiet corner, in
//   their strips, RS_NEAR clear of the bather and the mushroom picker, RS_UPSTREAM right of the tai chi line,
//   RS_FIRESIDE's pick reed 14 px behind his seat at its height; the same seed gives the same sites;
// - every draw inside reedsBounds: every month, snow, the wind at both extremes, every hold at its limits,
//   every stalk taken, the build's pop; the fringe stays on the waterlines;
// - incremental rendering equals a full render in the three camp families, in wind and in snow, with pushes,
//   holds and takes thrown in; two runs agree; a track with nothing changed marks nothing;
// - the grass's sway is bit for bit the baseline's (the ambientWindAt refactor, stage22-baseline/probe.txt);
// - the interface: a push rings down within 3 s, a hold lets go two ticks after its last call and is back in
//   the wind within 2 s, take is once only, a taken reed is not offered, stand points are on dry ground,
//   a blast moves only the clumps in its radius;
// - REVIEW_FIXES F9 / F23: a body frozen in a footprint by the doorbell does not keep pushing;
// - the loop RNG and the plans do not move with the piece; nothing in a Lookout loop;
// - budgets: sizeof(Reeds) <= 224, sizeof(App) under the 29,992 wire, strips a frame over whole loops no more
//   than 0.3 above the same loops without the piece; primitives printed.
// Step C1 (the pick, the stem, the small life, the blackbird):
// - the seated pick leaves his place, facing, pose target and carry as they were, holds the reed at his hand,
//   takes it and ends with the stem in his mouth and nothing of the overlay left; the stroll ends where it began,
//   and a major under it puts him home on that tick (both with SS_S22 on: the pick waits for the switch);
// - the stem's draws stay in reedsMouthBounds in every pose and facing, wilted and in his hand; its clocks stand
//   still through a doorbell; it drops as dinner begins;
// - the dragonfly by day only, in its rows; fireflies at night only, never above row 352; fluff in autumn only;
// - the blackbird fires in midday loops, rides the head's tip, lets the stalk go; nothing forced;
// - the hat gag and the pick never overlap, and no minor starts during a pick.
#include "gfx/raster.h"
#include "ss_app.h"
#include "scene/landscape.h"
#include "scene/hiker.h"
#include "scene/reeds.h"
#include "scene/stage22_switch.h"
#include "scene/world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

using namespace ss;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

struct Rig {
  void* mem = nullptr;
  std::vector<Pixel> fb;
  Surface surf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = calloc(1, SS_APP_BYTES);
    fb.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    surf = Surface{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = appStart(mem, kAlloc, seed, in);
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    app = nullptr;
  }
  World& w() { return const_cast<World&>(*appWorld(app)); }
  void ticks(int n) {
    for (int i = 0; i < n; ++i) appTick(app);
  }
};

static Inputs inputsFor(int family, float mood, bool night = false, int weather = 0, int month = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.have_outside = true;
  in.weather_code = weather;
  in.spine_force = (uint8_t)(1 + family);
  in.night_mode = night;
  in.weather.night_dim = night ? 1.f : 0.f;
  in.month = (uint8_t)month;
  return in;
}

static bool has(const World& w, int c) { return (w.reeds.site[c].flags & 1u) != 0; }

static bool bank(const World& w, float x, bool near_bank, float& y) {
  float cy, half;
  if (!landscapeStreamSpan(w.land, x, cy, half)) return false;
  y = near_bank ? cy + half : cy - half;
  return true;
}

static void hideReeds(World& w) {
  for (ReedSite& s : w.reeds.site) s.flags = 0;
  for (int i = 0; i + 1 < LS_REED_FRINGE; ++i) w.land.reed_fringe[i] = 0;
}

static bool inside(const Rect& r, int x, int y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

static Palette dayPalette() {
  Palette p;
  WeatherTint t;
  memset(&t, 0, sizeof t);
  computePalette(3.f, t, p);
  return p;
}

// Draws clump c alone on a frame of one colour; counts pixels drawn outside `ok`.
static int spill(const World& w, int c, const Rect& ok, int* drawn = nullptr) {
  static std::vector<Pixel> px((size_t)SS_WIDTH * SS_HEIGHT);
  for (Pixel& p : px) p = (Pixel)0x0841;
  Surface s{px.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  const Palette pal = dayPalette();
  reedsDraw(w, c, s, Rect{0, 0, SS_WIDTH, SS_HEIGHT}, pal);
  int out = 0, n = 0;
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x) {
      if (px[(size_t)y * SS_WIDTH + x] == (Pixel)0x0841) continue;
      ++n;
      if (!inside(ok, x, y)) ++out;
    }
  if (drawn) *drawn = n;
  return out;
}

// (1) Sites, seeds 1 to 300.
static void testSites() {
  int seen[REED_CLUMPS] = {0, 0, 0, 0, 0};
  const int kSeeds = 300;
  // The strips each clump's box must stay in (CATTAILS_PLAN 2.2): [first row, end row).
  const int strip_y0[REED_CLUMPS] = {416, 416, 352, 352, 320};
  const int strip_y1[REED_CLUMPS] = {480, 480, 416, 448, 384};
  for (int seed = 1; seed <= kSeeds; ++seed) {
    Rig r;
    CHECK(r.start((uint64_t)seed, inputsFor(SF_MIDDAY, 3.f)), "start");
    r.ticks(25 * 14);   // past the build: the boxes are the settled ones
    World& w = r.w();
    const Vec2 F = w.fire.pos;
    for (int c = 0; c < REED_CLUMPS; ++c) {
      if (!has(w, c)) continue;
      ++seen[c];
      const ReedSite& S = w.reeds.site[c];
      const bool near_bank = c == RS_FORE || c == RS_NEAR;
      float y = 0.f;
      CHECK(bank(w, (float)S.x, near_bank, y), "seed %d clump %d: no stream at x %d", seed, c, S.x);
      const float want = near_bank ? (c == RS_FORE ? fminf(y + 2.f, 488.f) : y + 2.f) : y + 1.f;
      CHECK(fabsf((float)S.y - want) <= 3.f, "seed %d clump %d: base y %d, waterline gives %.1f", seed, c, S.y, (double)want);
      const Rect b = reedsBounds(w, c);
      CHECK(!b.empty() && S.x > 0 && S.x < SS_WIDTH, "seed %d clump %d: x %d, box empty %d", seed, c, S.x, (int)b.empty());
      const Rect fb = b.clipped(Rect{0, 0, SS_WIDTH, SS_HEIGHT});
      CHECK(!(fb.x1 > 538 && fb.y1 > 412), "seed %d clump %d: box %d,%d..%d,%d meets the chip's box", seed, c, fb.x0, fb.y0,
            fb.x1, fb.y1);
      CHECK(fb.y0 >= strip_y0[c] && fb.y1 <= strip_y1[c], "seed %d clump %d: box rows %d..%d outside %d..%d", seed, c, fb.y0,
            fb.y1, strip_y0[c], strip_y1[c]);
      CHECK(!(S.x > 592 - 8 && S.x < 632 + 8 && !near_bank), "seed %d clump %d: on the boulder's bank (x %d)", seed, c, S.x);
    }
    if (has(w, RS_NEAR)) {
      const float nx = fminf((float)w.land.mush_x - 44.f, 501.f);
      CHECK((float)w.reeds.site[RS_NEAR].x <= nx - 36.f + 0.5f, "seed %d: RS_NEAR at %d, Nx %.0f", seed, w.reeds.site[RS_NEAR].x,
            (double)nx);
      CHECK((float)w.reeds.site[RS_NEAR].x >= F.x + 126.f + 40.f - 1.f, "seed %d: RS_NEAR at %d, bather at %.0f", seed,
            w.reeds.site[RS_NEAR].x, (double)(F.x + 126.f));
    }
    if (has(w, RS_UPSTREAM))
      CHECK((float)w.reeds.site[RS_UPSTREAM].x >= F.x + 254.f - 1.f, "seed %d: RS_UPSTREAM at %d, F.x %.0f", seed,
            w.reeds.site[RS_UPSTREAM].x, (double)F.x);
    if (has(w, RS_FIRESIDE)) {   // F24: the pick reed (stalk 1), at rest: 12 to 18 px behind his seat, at sit.y - 4
      Reeds keep = w.reeds;
      for (int c = 0; c < REED_CLUMPS; ++c) w.reeds.sway_q[c][0] = w.reeds.sway_q[c][1] = 0;
      for (ReedHold& h : w.reeds.hold) h.ttl = 0;
      ReedStalkInfo si;
      CHECK(reedsStalk(w, RS_FIRESIDE, 1, si) && !si.cattail, "seed %d: fireside stalk 1 is not a reed", seed);
      const Vec2 sit = w.spine.camp.sit;
      CHECK(si.tip.x - sit.x >= 12.f && si.tip.x - sit.x <= 18.f, "seed %d: pick reed tip %.1f px behind the seat",
            seed, (double)(si.tip.x - sit.x));
      CHECK(fabsf(si.tip.y - (sit.y - 4.f)) <= 6.f, "seed %d: pick reed tip y %.1f, seat y %.1f", seed, (double)si.tip.y,
            (double)sit.y);
      w.reeds = keep;
    }
    if (seed <= 20) {   // the same seed, the same sites
      Rig q;
      q.start((uint64_t)seed, inputsFor(SF_EVENING, 4.f));
      CHECK(memcmp(q.w().reeds.site, w.reeds.site, sizeof w.reeds.site) == 0, "seed %d: sites differ by family", seed);
      CHECK(memcmp(q.w().land.reed_fringe, w.land.reed_fringe, LS_REED_FRINGE - 1) == 0, "seed %d: fringe differs", seed);
      q.stop();
    }
    r.stop();
  }
  printf("sites over %d seeds: fore %d, near %d, upstream %d, fireside %d, far %d\n", kSeeds, seen[0], seen[1], seen[2],
         seen[3], seen[4]);
  CHECK(seen[RS_FORE] == kSeeds, "RS_FORE missing in %d seeds", kSeeds - seen[RS_FORE]);
  CHECK(seen[RS_NEAR] > kSeeds / 2, "RS_NEAR in only %d of %d seeds", seen[RS_NEAR], kSeeds);
  CHECK(seen[RS_UPSTREAM] > kSeeds / 2, "RS_UPSTREAM in only %d", seen[RS_UPSTREAM]);
  CHECK(seen[RS_FIRESIDE] > kSeeds / 3, "RS_FIRESIDE in only %d", seen[RS_FIRESIDE]);
  CHECK(seen[RS_FAR] > kSeeds / 2 && seen[RS_FAR] < kSeeds * 9 / 10, "RS_FAR in %d (about 70 %% wanted)", seen[RS_FAR]);
}

// (2) Every draw inside its box.
static void testBounds() {
  int worst = 0, tried = 0;
  for (int seed = 1; seed <= 12; ++seed) {
    Rig r;
    r.start((uint64_t)seed, inputsFor(SF_MIDDAY, 3.f));
    r.ticks(25 * 14);
    World& w = r.w();
    const Reeds keep = w.reeds;
    for (int c = 0; c < REED_CLUMPS; ++c) {
      if (!has(w, c)) continue;
      const Rect b = reedsBounds(w, c);
      const int n = w.reeds.site[c].n_cat + w.reeds.site[c].n_reed;
      for (int month = 0; month <= 12; ++month)
        for (int snow = 0; snow < 2; ++snow)
          for (int wind = 0; wind < 5; ++wind)   // calm; leaves and heads both far left, both far right; split
            for (int mode = 0; mode < 4; ++mode) {   // 0 free, 1 every stalk held to a corner, 2 let go, 3 taken
              static const int8_t kWind[5][2] = {{0, 0}, {-120, -120}, {120, 120}, {-120, 120}, {120, -120}};
              w.reeds = keep;
              w.reeds.month = (uint8_t)month;
              w.weather.snow_roof = snow ? 1.f : 0.f;
              w.reeds.sway_q[c][0] = kWind[wind][0];
              w.reeds.sway_q[c][1] = kWind[wind][1];
              if (mode == 3) w.reeds.site[c].gone = 0x1F;
              for (int k = 0; k < n && (mode == 1 || mode == 2); k += 2) {
                for (int corner = 0; corner < 4; ++corner) {
                  w.reeds.hold[0].ttl = 0;
                  w.reeds.hold[1].ttl = 0;
                  reedsBend(w, c, k, Vec2{corner & 1 ? 40.f : -40.f, corner & 2 ? 40.f : -40.f});
                  if (k + 1 < n) reedsBend(w, c, k + 1, Vec2{corner & 1 ? -40.f : 40.f, corner & 2 ? -40.f : 40.f});
                  if (mode == 2)   // 8 ticks after it was let go: half a turn of the 1.6 Hz ring, the far swing back
                    for (ReedHold& h : w.reeds.hold)
                      if (h.ttl) h.ttl = 253 - 8;
                  const int o = spill(w, c, b);
                  ++tried;
                  if (o > worst) worst = o;
                  CHECK(o == 0, "seed %d clump %d month %d snow %d wind %d mode %d stalk %d corner %d: %d px outside", seed, c,
                        month, snow, wind, mode, k, corner, o);
                }
              }
              if (mode == 0 || mode == 3) {
                const int o = spill(w, c, b);
                ++tried;
                CHECK(o == 0, "seed %d clump %d month %d snow %d wind %d mode %d: %d px outside", seed, c, month, snow, wind,
                      mode, o);
              }
            }
      w.reeds = keep;
      w.weather.snow_roof = 0.f;
      // The build's pop: the box while building (the overshoot's) holds every scale the plants phase passes.
      const BuildState done = w.build;
      for (int i = 0; i <= 40; ++i) {
        buildSetProgress(w.build, 0.35f + 0.5f * (float)i / 40.f);
        const Rect bb = reedsBounds(w, c);
        if (bb.empty()) continue;
        CHECK(spill(w, c, bb) == 0, "seed %d clump %d: build step %d spills its box", seed, c, i);
      }
      w.build = done;
    }
    // The fringe in the cache: each cluster on its waterline, at most 14 px above it and 3 below.
    std::vector<Pixel> px((size_t)SS_WIDTH * SS_HEIGHT, (Pixel)0x0841);
    Surface s{px.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    reedsDrawStatic(w.land, s, dayPalette(), Rect{0, 0, SS_WIDTH, SS_HEIGHT}, w.build);
    int n = 0, bad = 0;
    for (int y = 0; y < SS_HEIGHT; ++y)
      for (int x = 0; x < SS_WIDTH; ++x) {
        if (px[(size_t)y * SS_WIDTH + x] == (Pixel)0x0841) continue;
        ++n;
        bool ok = false;
        for (int i = 0; i + 1 < LS_REED_FRINGE && !ok; ++i) {
          const uint8_t v = w.land.reed_fringe[i];
          if (!v) continue;
          const float fx = (float)(((v & 0x7F) - 1) * 8);
          float by;
          if (!bank(w, fx, (v & 0x80) != 0, by)) continue;
          by = floorf(by) + 1.f;
          ok = fabsf((float)x - fx) <= 8.f && (float)y >= by - 14.f && (float)y <= by + 3.f;
        }
        if (!ok) ++bad;
      }
    CHECK(n > 0 && bad == 0, "seed %d: fringe drew %d px, %d off its waterlines", seed, n, bad);
    r.stop();
  }
  printf("bounds: %d draws checked, worst spill %d px\n", tried, worst);
}

// (3) Incremental rendering equals a full render; two runs agree.
static void testIncremental() {
  struct Case { int family; float mood; bool night; int weather; int month; };
  static const Case kCases[] = {{SF_EVENING, 4.4f, false, 3, 0}, {SF_MORNING, 1.5f, false, 3, 10},
                                {SF_MIDDAY, 3.f, false, 3, 8},   {SF_MIDDAY, 3.f, false, 2, 1},
                                {SF_EVENING, 0.f, true, 0, 7},   {SF_MIDDAY, 3.f, false, 0, 7},
                                {SF_EVENING, 4.8f, false, 0, 7}};
  for (const Case& cs : kCases) {
    Rig a, b, d;
    const Inputs in = inputsFor(cs.family, cs.mood, cs.night, cs.weather, cs.month);
    CHECK(a.start(6, in) && b.start(6, in) && d.start(6, in), "start");
    int checks = 0, bad = 0, diff = 0;
    for (int t = 0; t < 3000; ++t) {
      for (Rig* r : {&a, &b, &d}) {   // the same touches in all three
        World& w = r->w();
        if (t == 500) reedsPush(w, RS_FORE, 1.f, 1.f);
        if (t >= 700 && t < 760) reedsBend(w, RS_NEAR, 0, Vec2{6.f, 3.f});
        if (t == 900) reedsTake(w, RS_UPSTREAM, 2);
        if (t == 950) reedsBlast(w, Vec2{(float)w.reeds.site[RS_FORE].x, 440.f}, 80.f, 0.8f);
        if (t == 300) {   // step C1: a pick wanted at once (the switch decides whether it starts); a stem to drop
          w.reeds.want_pick = 1;
          w.reeds.pick_after = 0;
        }
        if (t == 1500) reedsGiveChew(w, 20.f);
        if (t == 2400) reedsDropChew(w);
      }
      appTick(a.app);
      appTick(b.app);
      appTick(d.app);
      appRender(a.app, a.surf);
      appRender(d.app, d.surf);
      if (t > 300 && (t % 131 == 0 || (t >= 700 && t < 800 && t % 7 == 0) || (t >= 500 && t < 540))) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        if (memcmp(a.fb.data(), b.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) {
          if (++bad == 1)
            for (size_t i = 0; i < a.fb.size(); ++i)
              if (a.fb[i] != b.fb[i]) {
                printf("  family %d tick %d: first difference at x %d y %d\n", cs.family, t, (int)(i % SS_WIDTH),
                       (int)(i / SS_WIDTH));
                break;
              }
        }
        if (memcmp(a.fb.data(), d.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) ++diff;
      }
    }
    CHECK(checks >= 20, "only %d checkpoints", checks);
    CHECK(bad == 0, "family %d weather %d month %d: %d of %d incremental frames differ from a full render", cs.family,
          cs.weather, cs.month, bad, checks);
    CHECK(diff == 0, "family %d: two runs differ in %d of %d frames", cs.family, diff, checks);
    a.stop();
    b.stop();
    d.stop();
  }
  // A track with nothing changed marks nothing; a change of one quarter unit marks the clump's box.
  {
    Rig r;
    r.start(6, inputsFor(SF_MIDDAY, 3.f));
    r.ticks(25 * 20);
    World& w = r.w();
    w.reeds.fly = 0;   // the dragonfly away (in flight it is marked every tick: its wings beat)
    Renderer rr;
    memset(static_cast<void*>(&rr), 0, sizeof rr);
    reedsTrack(w, rr);
    rr.dirty_mask = 0;
    reedsTrack(w, rr);
    CHECK(rr.dirty_mask == 0, "an unchanged track marked strips %04x", (unsigned)rr.dirty_mask);
    w.reeds.sway_q[RS_FORE][1] = (int8_t)(w.reeds.sway_q[RS_FORE][1] + 1);
    reedsTrack(w, rr);
    CHECK((rr.dirty_mask & (1u << 14)) != 0, "a quarter unit on RS_FORE's heads marked %04x", (unsigned)rr.dirty_mask);
    r.stop();
  }
}

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}

// (4) The grass, as stage22-baseline/probe.txt has it ("sway seed 1 evening 2000 ticks 334a8431a659fb66").
static void testGrass() {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 4.f;
  Rig r;
  r.start(1, in);
  uint64_t h = 1469598103934665603ull;
  for (int t = 0; t < 2000; ++t) {
    appTick(r.app);
    h = fnv(h, r.w().amb.sway, sizeof r.w().amb.sway);
  }
  CHECK(h == 0x334a8431a659fb66ull, "the grass's sway hash is %016llx, the baseline's 334a8431a659fb66", (unsigned long long)h);
  r.stop();
}

// The first seed whose loop 0 has all five clumps (the sites do not depend on the family).
static uint64_t allFiveSeed() {
  for (uint64_t s = 1; s < 200; ++s) {
    Rig q;
    q.start(s, inputsFor(SF_MIDDAY, 3.f));
    bool all = true;
    for (int c = 0; c < REED_CLUMPS; ++c) all = all && has(q.w(), c);
    q.stop();
    if (all) return s;
  }
  return 0;
}

// (5) The interface.
static void testInterface() {
  // Seeds with every clump, calm air.
  const uint64_t seed = allFiveSeed();
  CHECK(seed != 0, "no seed with all five clumps");
  if (!seed) return;
  const Inputs in = inputsFor(SF_MIDDAY, 3.f);
  {   // a push rings and comes back to the wind within 3 s (a twin run without the push is the wind)
    Rig a, b;
    a.start(seed, in);
    b.start(seed, in);
    a.ticks(25 * 20);
    b.ticks(25 * 20);
    reedsPush(a.w(), RS_UPSTREAM, 1.f, 1.f);
    int moved = 0, last_diff = 0, crossings = 0, prev_sign = 0;
    for (int t = 0; t < 75; ++t) {
      appTick(a.app);
      appTick(b.app);
      const int d = a.w().reeds.sway_q[RS_UPSTREAM][1] - b.w().reeds.sway_q[RS_UPSTREAM][1];
      if (d != 0) ++moved;
      const int sg = d > 1 ? 1 : (d < -1 ? -1 : 0);
      if (sg && prev_sign && sg != prev_sign) ++crossings;
      if (sg) prev_sign = sg;
      last_diff = d;
    }
    CHECK(moved > 10, "the push moved the heads on only %d ticks", moved);
    CHECK(crossings >= 2, "the push rang %d times (want at least 2 swings back)", crossings);
    CHECK(abs(last_diff) <= 1, "3 s after a push the heads are %d quarter units from the wind", last_diff);
    a.stop();
    b.stop();
  }
  {   // a hold: let go two ticks after its last call, back in the wind within 2 s
    Rig a, b;
    a.start(seed, in);
    b.start(seed, in);
    a.ticks(25 * 20);
    b.ticks(25 * 20);
    const int k = a.w().reeds.site[RS_UPSTREAM].n_cat;   // its first reed
    for (int t = 0; t < 20; ++t) {   // called after the reeds' own step, as a minor's step or a script's act is
      appTick(a.app);
      appTick(b.app);
      reedsBend(a.w(), RS_UPSTREAM, k, Vec2{8.f, 4.f});
    }
    const Vec2 held = reedsTip(a.w(), RS_UPSTREAM, k), free_tip = reedsTip(b.w(), RS_UPSTREAM, k);
    CHECK(fabsf(held.x - free_tip.x) > 3.f, "the hold did not move the tip (%.1f vs %.1f)", (double)held.x, (double)free_tip.x);
    appTick(a.app);
    appTick(b.app);
    bool still_held = false;
    for (const ReedHold& h : a.w().reeds.hold) still_held = still_held || (h.ttl >= 254 && h.clump == RS_UPSTREAM);
    CHECK(still_held, "the hold let go after one tick without a call");
    appTick(a.app);
    appTick(b.app);
    bool let_go = true;
    for (const ReedHold& h : a.w().reeds.hold) let_go = let_go && !(h.ttl >= 254 && h.clump == RS_UPSTREAM);
    CHECK(let_go, "the hold still held two ticks after its last call");
    for (int t = 0; t < 50; ++t) {
      appTick(a.app);
      appTick(b.app);
    }
    const Vec2 ta = reedsTip(a.w(), RS_UPSTREAM, k), tb = reedsTip(b.w(), RS_UPSTREAM, k);
    CHECK(fabsf(ta.x - tb.x) < 0.75f && fabsf(ta.y - tb.y) < 0.75f, "2 s after letting go the tip is %.2f, %.2f px off",
          (double)(ta.x - tb.x), (double)(ta.y - tb.y));
    bool free_slots = true;
    for (const ReedHold& h : a.w().reeds.hold) free_slots = free_slots && h.ttl == 0;
    CHECK(free_slots, "the hold's slot was not freed");
    a.stop();
    b.stop();
  }
  {   // take, nearest, stand points, blast
    Rig r;
    r.start(seed, in);
    r.ticks(25 * 20);
    World& w = r.w();
    const ReedSite& U = w.reeds.site[RS_UPSTREAM];
    const int k = U.n_cat;
    CHECK(!reedsTake(w, RS_UPSTREAM, 0), "a cattail was taken without allow_cattail");
    CHECK(reedsTake(w, RS_UPSTREAM, k), "the first take of a reed failed");
    CHECK(!reedsTake(w, RS_UPSTREAM, k), "a reed was taken twice");
    {   // a taken cattail reports the top of the stub it draws: the whole stalk, wind and all, scaled by 0.6 about
        // its foot (drawCattail draws catStub with 0.6 of the offset, which is exactly that)
      const Reeds keep = w.reeds;
      for (int c = 0; c < REED_CLUMPS; ++c) w.reeds.sway_q[c][0] = w.reeds.sway_q[c][1] = 40;   // a good lean
      ReedStalkInfo s0;
      reedsStalk(w, RS_UPSTREAM, 1, s0);
      const Vec2 whole = s0.tip;
      CHECK(reedsTake(w, RS_UPSTREAM, 1, true), "a cattail with allow_cattail was not taken");
      const Vec2 stub = reedsTip(w, RS_UPSTREAM, 1);
      const Vec2 f = s0.foot;
      CHECK(fabsf((f.y - stub.y) - 0.6f * (f.y - whole.y)) <= 0.25f && fabsf((stub.x - f.x) - 0.6f * (whole.x - f.x)) <= 0.25f,
            "the stub's tip %.2f,%.2f is not 0.6 of the whole stalk's %.2f,%.2f (foot %.1f,%.1f)", (double)stub.x,
            (double)stub.y, (double)whole.x, (double)whole.y, (double)f.x, (double)f.y);
      reedsBend(w, RS_UPSTREAM, 1, Vec2{8.f, 4.f});
      bool held = false;
      for (const ReedHold& h : w.reeds.hold) held = held || (h.ttl != 0 && h.clump == RS_UPSTREAM && h.stalk == 1);
      CHECK(!held, "a taken stalk took a hold");
      const uint8_t gone = w.reeds.site[RS_UPSTREAM].gone;
      w.reeds = keep;
      w.reeds.site[RS_UPSTREAM].gone = gone;
    }
    const Vec2 tip = reedsTip(w, RS_UPSTREAM, k);
    for (int i = 0; i < 4; ++i) {
      const int s = reedsNearestStalk(w, RS_UPSTREAM, Vec2{tip.x + (float)(i - 2), tip.y}, true);
      CHECK(s != k, "the taken reed was offered");
    }
    ReedStalkInfo si;
    CHECK(reedsStalk(w, RS_UPSTREAM, k, si) && !si.present, "a taken reed reports present");
    for (int c = 0; c < REED_CLUMPS; ++c)
      for (int side = -1; side <= 1; side += 2) {
        Vec2 f;
        const bool ok = reedsStandPoint(w, c, side, f);
        const bool camp_bank = c == RS_UPSTREAM || c == RS_FIRESIDE || c == RS_FAR;
        CHECK(ok == camp_bank, "clump %d: stand point %d, camp bank %d", c, (int)ok, (int)camp_bank);
        if (!ok) continue;
        float y;
        CHECK(bank(w, f.x, false, y) && f.y <= y - 4.f && f.y <= 412.f, "clump %d side %d: stand point %.0f,%.0f not on dry ground",
              c, side, (double)f.x, (double)f.y);
      }
    CHECK(reedsNearest(w, Vec2{(float)U.x, (float)U.y}, 5.f, false) == RS_UPSTREAM, "nearest at its own root");
    CHECK(reedsNearest(w, Vec2{(float)w.reeds.site[RS_NEAR].x, (float)w.reeds.site[RS_NEAR].y}, 5.f, true) != RS_NEAR,
          "camp_only offered the near bank");
    int16_t v[REED_CLUMPS];
    for (int c = 0; c < REED_CLUMPS; ++c) v[c] = w.reeds.push_v[c];
    reedsBlast(w, Vec2{(float)w.reeds.site[RS_FAR].x - 10.f, (float)w.reeds.site[RS_FAR].y - 10.f}, 40.f, 1.f);
    for (int c = 0; c < REED_CLUMPS; ++c)
      CHECK((w.reeds.push_v[c] != v[c]) == (c == RS_FAR), "blast near RS_FAR: clump %d moved %d", c,
            (int)(w.reeds.push_v[c] != v[c]));
    r.stop();
  }
}

// (6) F9 / F23: a body caught in a footprint during the doorbell does not keep pushing the clump.
static void testFrozenBrush() {
  uint64_t seed = 0;
  for (uint64_t s = 1; s < 200 && !seed; ++s) {
    Rig q;
    q.start(s, inputsFor(SF_MIDDAY, 3.f));
    if (has(q.w(), RS_FAR)) seed = s;
    q.stop();
  }
  for (int frozen = 0; frozen < 2; ++frozen) {
    Rig r;
    r.start(seed, inputsFor(SF_MIDDAY, 3.f));
    r.ticks(25 * 20);
    World& w = r.w();
    const ReedSite& S = w.reeds.site[RS_FAR];
    // A deer standing mid-stride in RS_FAR's footprint, its `moving` flag up (as a freeze leaves it).
    const int slot = CRITTER_STORY - 1;
    CritterSlot keep_s = w.crit[slot];
    w.crit[slot].in_use = true;
    critterInit(w.critters[slot], CR_DEER, Vec2{(float)S.x + 2.f, (float)S.y - 2.f}, -1);
    w.critters[slot].visible = true;
    w.critters[slot].moving = true;
    w.critters[slot].move_to = (float)S.x - 80.f;
    w.react.active = frozen != 0;
    float worst = 0.f;
    for (int t = 0; t < 85; ++t) {
      reedsStep(w);   // the ambient side only: what runs through a freeze
      if (t >= 50) {
        const float dev = fabsf((float)w.reeds.push[RS_FAR] / 256.f - reedsWind(w, (float)S.x));
        if (dev > worst) worst = dev;
      }
    }
    if (frozen) CHECK(worst < 1.f, "frozen: the clump is %.2f wind units off the wind 2 s into the bell", (double)worst);
    else CHECK(worst > 1.f, "not frozen: a walking body did not push the clump (%.2f)", (double)worst);
    w.react.active = false;
    w.crit[slot] = keep_s;
    r.stop();
  }
}

// (7) The plans do not move with the piece; a Lookout loop has none of it. What this compares is a run with the
// clumps against one with them hidden after the start, so it proves the plants' story-time life (brush, holds,
// wind) never reaches the plan or the next loop's seed. That reedsInit draws nothing from the loop RNG is by
// construction (it takes a const Rng& and draws from a copy); the proof against the tree before stage 22 is the
// pinned plan lines of test_spines.cpp and test_midday.cpp, which pass unchanged.
static void testLoopRng() {
  Rig a, b;
  const Inputs in = inputsFor(SF_MIDDAY, 3.f);
  a.start(9, in);
  b.start(9, in);
  hideReeds(b.w());
  b.w().reeds.rng = 12345u;
  int loops = 0;
  for (int t = 0; t < 25 * 1400 && loops < 2; ++t) {
    const bool na = appTick(a.app), nb = appTick(b.app);
    CHECK(na == nb, "the loops end at different ticks (%d)", t);
    if (na) {
      ++loops;
      CHECK(appLoopSeed(a.app) == appLoopSeed(b.app), "loop %d: the loop seed moved with the reeds", loops);
      char pa[400], pb[400];
      appLoopPlan(a.app, pa, sizeof pa);
      appLoopPlan(b.app, pb, sizeof pb);
      CHECK(strcmp(pa, pb) == 0, "loop %d: plans differ:\n  %s\n  %s", loops, pa, pb);
      hideReeds(b.w());
    }
  }
  CHECK(loops == 2, "only %d loops", loops);
  a.stop();
  b.stop();
  Rig l;
  Inputs li;
  memset(&li, 0, sizeof li);
  li.mood = 4.5f;
  li.spine_force = (uint8_t)(1 + SF_LOOKOUT);
  l.start(3, li);
  l.ticks(25 * 30);
  ReedClumpInfo ci;
  bool any = false;
  for (int c = 0; c < REED_CLUMPS; ++c) any = any || reedsClump(l.w(), c, ci) || !reedsBounds(l.w(), c).empty();
  CHECK(!any, "a Lookout loop reports reeds");
  l.stop();
}

// (8) Budgets: sizes, strips a frame with and without the piece over whole loops, primitives.
static void testBudgets() {
  printf("sizeof(Reeds) = %u (cap 224), sizeof(App) = %u (wire 29992), World %u\n", (unsigned)sizeof(Reeds), appAppBytes(),
         (unsigned)sizeof(World));
  CHECK(sizeof(Reeds) <= 224, "Reeds is %u bytes", (unsigned)sizeof(Reeds));
  CHECK(appAppBytes() <= 29992, "App is %u bytes", appAppBytes());
  struct Fam { int family; float mood; bool night; };
  const Fam fams[3] = {{SF_EVENING, 4.f, false}, {SF_MORNING, 1.5f, false}, {SF_MIDDAY, 3.f, false}};
  // Seeds 1 and 7 are the baseline probe's (seed 1 has no RS_FIRESIDE); the third seed has all five clumps,
  // RS_FAR's strip 10 included (the quiet morning is where that strip costs the most).
  const uint64_t five = allFiveSeed();
  for (const Fam& f : fams)
    for (uint64_t seed : {1ull, 7ull, (unsigned long long)five}) {
      Rig a, b;
      Inputs in = inputsFor(f.family, f.mood, f.night);
      in.have_outside = false;   // random weather, as the pinned runs
      a.start(seed, in);
      b.start(seed, in);
      hideReeds(b.w());
      long long sa = 0, sb = 0;
      int n = 0, prim_max = 0;
      long long prim_sum = 0;
      for (int t = 0; t < 25 * 700; ++t) {
        const bool na = appTick(a.app);
        appTick(b.app);
        if (na) break;
        sa += appRender(a.app, a.surf);
        sb += appRender(b.app, b.surf);
        ++n;
        int p = 0;
        for (int c = 0; c < REED_CLUMPS; ++c) p += reedsPrimitives(a.w(), c);
        prim_sum += p;
        if (p > prim_max) prim_max = p;
      }
      const double ra = (double)sa / n, rb = (double)sb / n;
      printf("strips a frame, %s seed %llu: %.3f with the reeds, %.3f without (+%.3f); reed primitives a frame %.1f, max %d\n",
             f.family == SF_EVENING ? "evening" : (f.family == SF_MORNING ? "morning" : "midday"), (unsigned long long)seed, ra,
             rb, ra - rb, (double)prim_sum / n, prim_max);
      CHECK(ra - rb <= 0.3, "family %d seed %llu: +%.3f strips a frame", f.family, (unsigned long long)seed, ra - rb);
      a.stop();
      b.stop();
    }
  {   // the busiest look: every clump, autumn fluff and snow caps
    Rig r;
    r.start(6, inputsFor(SF_MIDDAY, 3.f, false, 2, 11));
    r.ticks(25 * 90);
    int p = 0;
    for (int c = 0; c < REED_CLUMPS; ++c) p += reedsPrimitives(r.w(), c);
    printf("primitives, seed 6 in November snow: %d\n", p);
    r.stop();
  }
}

// ---- step C1: the pick, the stem, the small life, the blackbird ------------------------------------------

static bool isPause(App* app) { return strcmp(appBeatName(app), "pause") == 0; }

// The first loop-0 tick where a forced pick of `form` (1 seated, 2 the stroll) starts, from `seed` on. The
// stroll is set up by standing him 40 px from a camp-bank clump's stand point at a free pause's first tick
// (a test's liberty: the scene never puts him there on purpose).
static bool findPick(Rig& r, uint64_t& seed, int form, int max_seeds) {
  for (int tries = 0; tries < max_seeds; ++tries, ++seed) {
    if (!r.start(seed, inputsFor(SF_MIDDAY, 3.f, false, 0, 7))) return false;
    World& w = r.w();
    if (form == 1 && !has(w, RS_FIRESIDE)) {
      r.stop();
      continue;
    }
    w.reeds.want_pick = 1;
    w.reeds.pick_after = 0;
    if (form == 2) w.reeds.site[RS_FIRESIDE].flags = 0;   // no seated pick to prefer
    for (int t = 0; t < 25 * 600; ++t) {
      if (appTick(r.app)) break;
      if (form == 2 && w.reeds.pick == 0 && isPause(r.app) && w.minors.beat_left > 16.f && w.fx.on && !w.hiker.walking &&
          !w.hiker.blending && has(w, RS_UPSTREAM)) {
        // Stand him on the camp side 40 px left of the upstream clump's stand point.
        Vec2 st;
        if (reedsStandPoint(w, RS_UPSTREAM, -1, st)) {
          float y;
          const float x = st.x - 40.f;
          if (bank(w, x, false, y) && y - 6.f > 300.f) {
            w.hiker.feet = Vec2{x, y - 8.f};
            w.fx.path_on = false;
            w.reeds.want_pick = 1;
            w.reeds.pick_after = 0;
          }
        }
      }
      if (w.reeds.pick == (form == 1 ? 1 : 3)) return true;
    }
    r.stop();
  }
  return false;
}

static void testPickSeated() {
  if (ssS22Switch() == 0) {
    printf("pick: the switch is off, no pick starts by itself (checked over the bounds and incremental runs)\n");
    return;
  }
  uint64_t seed = 1;
  Rig r;
  CHECK(findPick(r, seed, 1, 40), "no seed in 40 plays a seated pick");
  if (!r.app) return;
  World& w = r.w();
  const Hiker h0 = w.hiker;
  const int c = w.reeds.pick_clump, k = w.reeds.pick_stalk;
  float worst_gap = 0.f;
  int ticks = 0, moved = 0;
  while (w.reeds.pick != 0 && ticks < 25 * 10) {
    appTick(r.app);
    ++ticks;
    const Hiker& h = w.hiker;
    if (h.target != h0.target || h.facing != h0.facing || fabsf(h.feet.x - h0.feet.x) > 0.01f ||
        fabsf(h.feet.y - h0.feet.y) > 0.01f || h.carry != h0.carry)
      ++moved;
    const float t = (float)w.reeds.pick_t / 25.f;
    if (w.reeds.pick == 1 && t >= 2.1f && t < 3.f) {
      const Vec2 hand = hikerHandPos(h, 0), tip = reedsTip(w, c, k);
      worst_gap = fmaxf(worst_gap, sqrtf((hand.x - tip.x) * (hand.x - tip.x) + (hand.y - tip.y) * (hand.y - tip.y)));
    }
  }
  printf("pick seated: seed %llu, %.1f s, hand to tip at most %.1f px from 2.1 to 3.0 s\n", (unsigned long long)seed,
         ticks / 25.0, (double)worst_gap);
  CHECK(moved == 0, "the seated pick moved his place, facing, pose target or carry on %d ticks", moved);
  CHECK(ticks >= 25 * 6 && ticks <= 25 * 7, "the seated pick ran %d ticks (want about 6.4 s)", ticks);
  CHECK(worst_gap <= 6.f, "his hand was %.1f px from the reed's tip while he held it", (double)worst_gap);
  CHECK((w.reeds.site[c].gone >> k) & 1u, "the reed was not taken");
  CHECK(reedsChewing(w), "no stem in his mouth after the pick");
  HikerPose p = w.hiker.pose, q = p;
  reedsApplyPose(w, q);
  CHECK(memcmp(&p, &q, sizeof p) == 0, "the overlay still moves him after the pick");
  CHECK(w.reeds.want_pick == 0, "a second pick is still wanted");
  r.stop();
}

static void testPickStroll() {
  if (ssS22Switch() == 0) return;
  uint64_t seed = 1;
  Rig r;
  CHECK(findPick(r, seed, 2, 60), "no seed in 60 plays a stroll pick");
  if (!r.app) return;
  World& w = r.w();
  const Vec2 home{(float)w.reeds.home_x, (float)w.reeds.home_y};
  const int face = w.reeds.home_face, pose = w.reeds.home_pose;
  int ticks = 0;
  while (w.reeds.pick != 0 && ticks < 25 * 30) {
    appTick(r.app);
    ++ticks;
  }
  const bool took = reedsChewing(w);
  printf("pick stroll: seed %llu, %.1f s, %s\n", (unsigned long long)seed, ticks / 25.0, took ? "took a reed" : "reach failed");
  CHECK(ticks < 25 * 30, "the stroll never ended");
  CHECK(fabsf(w.hiker.feet.x - home.x) < 0.01f && fabsf(w.hiker.feet.y - home.y) < 0.01f, "he ended at %.1f, %.1f, not %.0f, %.0f",
        (double)w.hiker.feet.x, (double)w.hiker.feet.y, (double)home.x, (double)home.y);
  CHECK(w.hiker.facing == face && (int)w.hiker.target == pose, "facing %d pose %d, was %d %d", w.hiker.facing,
        (int)w.hiker.target, face, pose);
  CHECK(took, "the stroll's reach failed");
  r.stop();
  // Cut short: a major on under the walk puts him home on that tick.
  seed = 1;
  Rig q;
  if (!findPick(q, seed, 2, 60)) return;
  World& v = q.w();
  for (int t = 0; t < 25 && v.reeds.pick != 0; ++t) appTick(q.app);
  CHECK(v.reeds.pick == 4, "not walking 1 s into the stroll (stage %d)", v.reeds.pick);
  const Vec2 vh{(float)v.reeds.home_x, (float)v.reeds.home_y};
  v.major.on = true;
  reedsStoryStep(v);
  v.major.on = false;
  CHECK(v.reeds.pick == 0 && fabsf(v.hiker.feet.x - vh.x) < 0.01f && fabsf(v.hiker.feet.y - vh.y) < 0.01f,
        "cut short, he is at %.1f, %.1f (home %.0f, %.0f), stage %d", (double)v.hiker.feet.x, (double)v.hiker.feet.y,
        (double)vh.x, (double)vh.y, v.reeds.pick);
  q.stop();
  // A major's begin drops the stem (reedsDropChew) on the walk home, then the stroll is cut short: the falling
  // stem's ground must not move his home (review of C1: they shared Reeds::home_y).
  seed = 1;
  Rig d;
  if (!findPick(d, seed, 2, 60)) return;
  World& u = d.w();
  for (int t = 0; t < 25 * 30 && !(u.reeds.pick == 8 && reedsChewing(u)); ++t) appTick(d.app);
  CHECK(u.reeds.pick == 8 && reedsChewing(u), "never walking home with the stem (stage %d)", u.reeds.pick);
  const Vec2 uh{(float)u.reeds.home_x, (float)u.reeds.home_y};
  reedsDropChew(u);
  u.major.on = true;
  reedsStoryStep(u);
  u.major.on = false;
  CHECK(u.reeds.pick == 0 && fabsf(u.hiker.feet.x - uh.x) < 0.01f && fabsf(u.hiker.feet.y - uh.y) < 0.01f,
        "stem dropped on the walk home, then cut short: he is at %.1f, %.1f (home %.0f, %.0f)", (double)u.hiker.feet.x,
        (double)u.hiker.feet.y, (double)uh.x, (double)uh.y);
  d.stop();
}

// The stem: its draws stay in reedsMouthBounds in every pose, both facings, wilted, in his hand; its clocks
// stand still through a doorbell; it drops as a beat that uses his mouth begins.
static void testStem() {
  Rig r;
  r.start(6, inputsFor(SF_MIDDAY, 3.f, false, 0, 7));
  r.ticks(25 * 30);
  World& w = r.w();
  const Hiker keep = w.hiker;
  int worst = 0, tried = 0;
  static std::vector<Pixel> px((size_t)SS_WIDTH * SS_HEIGHT);
  const Palette pal = dayPalette();
  for (int p = 0; p < POSE_COUNT; ++p)
    for (int f = -1; f <= 1; f += 2)
      for (int mode = 0; mode < 3; ++mode) {   // in his mouth, wilted, in his hand
        w.hiker = keep;
        w.hiker.facing = f;
        hikerSetPose(w.hiker, (PoseId)p, 0.f);
        hikerTick(w.hiker, 0.f);
        w.reeds.chew = mode == 2 ? 1 : 2;
        w.reeds.chew_until = storyTick(w) + (mode == 1 ? 25u * 3u : 25u * 200u);
        for (int t = 0; t < 40; ++t) {   // through its roll and chew
          w.tick += 7;
          for (Pixel& q : px) q = (Pixel)0x0841;
          Surface s{px.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
          reedsDrawMouth(w, s, Rect{0, 0, SS_WIDTH, SS_HEIGHT}, pal);
          const Rect b = reedsMouthBounds(w);
          int out = 0, n = 0;
          for (int y = 0; y < SS_HEIGHT; ++y)
            for (int x = 0; x < SS_WIDTH; ++x)
              if (px[(size_t)y * SS_WIDTH + x] != (Pixel)0x0841) {
                ++n;
                if (!inside(b, x, y)) ++out;
              }
          ++tried;
          if (out > worst) worst = out;
          CHECK(n > 0 && out == 0, "pose %d facing %d mode %d: %d of %d px outside the mouth's box", p, f, mode, out, n);
        }
      }
  printf("stem: %d draws checked, worst spill %d px\n", tried, worst);
  w.hiker = keep;
  w.reeds.chew = 0;
  r.stop();

  // Its clocks stand still through a doorbell.
  Rig d;
  d.start(6, inputsFor(SF_MIDDAY, 3.f, false, 0, 7));
  d.ticks(25 * 40);
  World& v = d.w();
  reedsGiveChew(v, 200.f);
  const uint32_t until = v.reeds.chew_until, st0 = storyTick(v);
  Event e;
  e.type = EV_DOORBELL;
  e.arg = 0;
  appPostEvent(d.app, e);
  int frozen = 0;
  for (int t = 0; t < 25 * 6; ++t) {
    appTick(d.app);
    if (v.react.active) ++frozen;
  }
  CHECK(v.reeds.chew_until == until, "the doorbell moved the stem's deadline");
  CHECK(frozen > 0 && storyTick(v) - st0 < 25u * 6u, "story time ran %u ticks in a bell of %d frozen", storyTick(v) - st0,
        frozen);
  printf("stem through a doorbell: %d frozen ticks, deadline held\n", frozen);
  d.stop();

  // It drops as a beat that uses his mouth begins (the evening's dinner).
  int dropped = 0, tried_d = 0;
  for (uint64_t seed = 1; seed <= 8 && tried_d < 3; ++seed) {
    Rig q;
    q.start(seed, inputsFor(SF_EVENING, 4.f, false, 0, 7));
    World& x = q.w();
    bool given = false;
    for (int t = 0; t < 25 * 700; ++t) {
      if (appTick(q.app)) break;
      const bool dinner = strcmp(appBeatName(q.app), "dinner") == 0;
      if (!given && !dinner && x.fx.on && x.hiker.visible && t > 25 * 60) {
        reedsGiveChew(x, 900.f);
        given = true;
      }
      if (given && dinner) {
        ++tried_d;
        if (!reedsChewing(x)) ++dropped;
        break;
      }
      if (given && !reedsChewing(x) && !dinner) given = false;   // another ending came first: give it again
    }
    q.stop();
  }
  CHECK(tried_d > 0 && dropped == tried_d, "the stem dropped at %d of %d dinners", dropped, tried_d);
}

// Small life: where and when.
static void testSmallLife() {
  struct Case { int family; float mood; bool night; int weather; int month; bool fly; bool ff; bool seeds; };
  static const Case kCases[] = {{SF_MIDDAY, 3.f, false, 0, 7, true, false, false}, {SF_EVENING, 0.f, true, 0, 7, false, true, false},
                                {SF_MIDDAY, 3.f, false, 1, 7, false, false, false}, {SF_MIDDAY, 3.f, false, 0, 11, false, false, true},
                                {SF_EVENING, 0.f, true, 0, 1, false, false, false}, {SF_MORNING, 1.6f, false, 3, 10, false, false, true}};
  for (const Case& cs : kCases) {
    int fly = 0, ff = 0, seeds = 0, bad_fly = 0, bad_ff = 0, bad_seed = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
      Rig r;
      r.start(seed, inputsFor(cs.family, cs.mood, cs.night, cs.weather, cs.month));
      World& w = r.w();
      for (int t = 0; t < 25 * 300; ++t) {
        if (appTick(r.app)) break;
        const Reeds& R = w.reeds;
        if (R.d_fly[0] != -32768) {
          ++fly;
          if (R.d_fly[1] < 376 || R.d_fly[1] > 472 || R.d_fly[0] > 520) ++bad_fly;
        }
        for (const ReedSeed& s : R.seed)
          if (s.on == 1) {
            ++seeds;
            if (s.y0 < 352) ++bad_seed;
          }
        // Fireflies: drawn pixels only where they may be (rows 352 and below).
        if (t % 50 == 0 && R.dusk) {
          static std::vector<Pixel> px((size_t)SS_WIDTH * SS_HEIGHT);
          for (Pixel& q : px) q = (Pixel)0x0841;
          Surface s{px.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
          Reeds keep = w.reeds;
          w.reeds.fly = 0;
          for (ReedSeed& sd : w.reeds.seed) sd.on = 0;
          w.reeds.fall_kind = 0;
          reedsDrawAir(w, s, Rect{0, 0, SS_WIDTH, SS_HEIGHT}, dayPalette());
          w.reeds = keep;
          for (int y = 0; y < SS_HEIGHT; ++y)
            for (int x = 0; x < SS_WIDTH; ++x)
              if (px[(size_t)y * SS_WIDTH + x] != (Pixel)0x0841) {
                ++ff;
                if (y < 352) ++bad_ff;
              }
        }
      }
      r.stop();
    }
    printf("small life family %d mood %.1f weather %d month %d: dragonfly %d ticks, firefly px %d, seed ticks %d\n", cs.family,
           (double)cs.mood, cs.weather, cs.month, fly, ff, seeds);
    CHECK(cs.fly ? fly > 0 : fly == 0, "dragonfly ticks %d (want %s)", fly, cs.fly ? "some" : "none");
    CHECK(cs.ff ? ff > 0 : ff == 0, "firefly pixels %d (want %s)", ff, cs.ff ? "some" : "none");
    CHECK(cs.seeds ? seeds > 0 : seeds == 0, "seed ticks %d (want %s)", seeds, cs.seeds ? "some" : "none");
    CHECK(bad_fly == 0 && bad_ff == 0 && bad_seed == 0, "out of place: dragonfly %d, firefly %d, seeds %d", bad_fly, bad_ff,
          bad_seed);
  }
}

// The red-winged blackbird (minor reed_bird): it fires in midday loops, its feet are on the head's tip every
// perched tick, nothing is forced, and the stalk is let go after it leaves.
static void testReedBird() {
  if (ssS22Switch() == 0) {
    printf("reed_bird: the switch is off, it never plays\n");
    return;
  }
  int fired = 0, perched = 0, off_tip = 0, forced = 0, held_after = 0;
  for (uint64_t seed = 1; seed <= 6; ++seed) {
    Rig r;
    r.start(seed, inputsFor(SF_MIDDAY, 3.f, false, 0, 7));
    World& w = r.w();
    int count = 0, gone_t = -1, clump = -1;
    for (int t = 0;; ++t) {
      if (appTick(r.app)) break;
      const int c = appMinorCount(r.app);
      const bool bird = strcmp(appMinorName(r.app), "reed_bird") == 0;
      if (c != count) {
        count = c;
        if (bird) ++fired;
      }
      if (bird && (w.minors.stage == 1 || w.minors.stage == 2)) {
        clump = w.minors.n;
        const int s = w.minors.slot[0];
        if (s >= 0) {
          ++perched;
          const Vec2 tip = reedsTip(w, clump, 0), f = w.critters[s].feet;
          if (fabsf(f.x - tip.x) > 1.01f || fabsf(f.y - tip.y) > 0.01f) ++off_tip;
        }
      }
      if (!bird && clump >= 0 && gone_t < 0) gone_t = t;
      if (gone_t >= 0 && t == gone_t + 25 * 3) {
        for (const ReedHold& h : w.reeds.hold)
          if (h.ttl != 0 && h.clump == clump && h.stalk == 0) ++held_after;
        clump = -1;
        gone_t = -1;
      }
    }
    forced += appMinorForced(r.app);
    r.stop();
  }
  printf("reed_bird: fired %d times in 6 midday loops, %d perched ticks\n", fired, perched);
  CHECK(fired > 0, "reed_bird never fired in 6 midday loops");
  CHECK(off_tip == 0, "the bird's feet left the head's tip on %d of %d perched ticks", off_tip, perched);
  CHECK(forced == 0, "%d minors forced", forced);
  CHECK(held_after == 0, "the stalk still held 3 s after the bird left (%d)", held_after);
}

// The pick and the weather's hat gag never overlap; no minor starts during a pick (but in the evening, whose
// minors stay as they were).
static void testExclusion() {
  if (ssS22Switch() == 0) return;
  int picks = 0, overlap = 0, minor_in_pick = 0;
  for (uint64_t seed = 1; seed <= 60; ++seed) {
    Rig r;
    r.start(seed, inputsFor(seed & 1 ? SF_MIDDAY : SF_MORNING, seed & 1 ? 3.f : 1.6f, false, 3, 7));
    World& w = r.w();
    w.reeds.want_pick = 1;
    w.reeds.pick_after = 0;
    int count = appMinorCount(r.app);
    bool in_pick = false;
    for (int t = 0; t < 25 * 400; ++t) {
      if (appTick(r.app)) break;
      const bool p = reedsPickActive(w) && w.reeds.pick != 10;   // the toss is a second, not the pick
      if (p && !in_pick) ++picks;
      in_pick = p;
      if (p && w.weather.gag != 0) ++overlap;
      const int c = appMinorCount(r.app);
      if (c != count && reedsPickActive(w)) ++minor_in_pick;
      count = c;
    }
    r.stop();
  }
  printf("exclusion: %d picks in 60 windy loops\n", picks);
  CHECK(picks > 0, "no pick in 60 windy loops");
  CHECK(overlap == 0, "the hat gag and the pick overlapped on %d ticks", overlap);
  CHECK(minor_in_pick == 0, "%d minors started during a pick", minor_in_pick);
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));   // zeroed; the bounds checks draw without appRender
  testBudgets();
  testGrass();
  testSites();
  testBounds();
  testIncremental();
  testInterface();
  testFrozenBrush();
  testLoopRng();
  testPickSeated();
  testPickStroll();
  testStem();
  testSmallLife();
  testReedBird();
  testExclusion();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
