// Stream life tests (stage 17, scene/stream.h): the boulder in the current, floaters, fish.
//
// - every draw stays inside its bounds and inside the water (the mock visitors inside theirs);
// - incremental rendering equals a full render in each camp family, with floaters, a snag and fish
//   on screen, and two runs from one seed draw the same frames;
// - the loop RNG is untouched: scrambling the stream's own Rng mid-loop leaves the next loop's seed;
// - floater lifecycle: about one every 20 to 40 s, each one leaves past the left edge;
// - snag and release: held on the upstream face for its hold time, swings free, passes the boulder;
// - the water parts round the boulder; nothing shows through the rock;
// - nothing runs in the Lookout or the sunrise;
// - round 2: the heron and the deer come, stay, leave and spook, inside their bounds and out of the
//   chip's corner, incremental equals full while they are in; how often they come; the leaves by month;
// - round 3: a house dog out in camp goes for a visitor (housedogs.h HA_CHASE): it notices, runs at it,
//   the visitor goes before the dog is near, the dog stops on the camp-side bank, looks, and goes back
//   to its own life; out of the chip's corner; incremental equals full and two runs agree.
#include "gfx/raster.h"
#include "ss_app.h"
#include "scene/landscape.h"
#include "scene/stream.h"
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
    mem = malloc(SS_APP_BYTES);
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
};

static Inputs inputsFor(int family, float mood, bool night = false, int weather = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.have_outside = true;
  in.weather_code = weather;
  in.spine_force = (uint8_t)(1 + family);
  in.night_mode = night;
  in.weather.night_dim = night ? 1.f : 0.f;
  return in;
}

static float sAtX(const World& w, float x) {
  for (int i = 0; i + 1 < LS_STREAM_PTS; ++i) {
    const LsStreamPt& a = w.land.stream[i];
    const LsStreamPt& b = w.land.stream[i + 1];
    if (x <= a.x && x >= b.x) return (float)i + (a.x - x) / (a.x - b.x);
  }
  return 0.f;
}

static Floater makeFloater(const World& w, uint8_t kind, float x, float lane, float ang, float len, float hold) {
  Floater f{};
  f.kind = kind;
  f.phase = FP_DRIFT;
  f.tint = 1;
  f.side = 1;
  f.s = sAtX(w, x);
  f.lane = lane;
  f.ang = ang;
  f.spin = 0.03f;
  f.len = len;
  f.hold = hold;
  return f;
}

static void hush(World& w) {
  w.stream.next_float = w.tick + 25u * 3000u;
  w.stream.next_fish = w.tick + 25u * 3000u;
  for (Floater& f : w.stream.f) f.kind = FK_NONE;
  w.stream.fish.kind = FISH_NONE;
}

static bool inside(const Rect& r, int x, int y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

static void unite(Rect& r, const Rect& b) {
  if (b.empty()) return;
  if (r.empty()) { r = b; return; }
  r = Rect{r.x0 < b.x0 ? r.x0 : b.x0, r.y0 < b.y0 ? r.y0 : b.y0, r.x1 > b.x1 ? r.x1 : b.x1, r.y1 > b.y1 ? r.y1 : b.y1};
}

// Draws `what` onto a frame of one colour and counts pixels changed outside `ok` and outside the water
// (grown by `margin`).
struct Spill { int outside_bounds, outside_water, drawn; };
template <class F>
static Spill drawCheck(const World& w, const Rect& ok, float margin, F what) {
  std::vector<Pixel> px((size_t)SS_WIDTH * SS_HEIGHT, (Pixel)0x0841);
  Surface s{px.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  what(s);
  Spill r{0, 0, 0};
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x) {
      if (px[(size_t)y * SS_WIDTH + x] == (Pixel)0x0841) continue;
      ++r.drawn;
      if (!inside(ok, x, y)) ++r.outside_bounds;
      float cy, half;
      if (!landscapeStreamSpan(w.land, (float)x, cy, half) || fabsf((float)y - cy) > half + margin) ++r.outside_water;
    }
  return r;
}

// (1) Bounds against draws, and the water: every floater kind at several headings and phases, the
// fish at every stage, the foam and wake (moon glint on), the mock visitors.
static void testBounds() {
  Rig r;
  CHECK(r.start(5, inputsFor(SF_MIDDAY, 3.f)), "start");
  for (int t = 0; t < 500; ++t) appTick(r.app);
  World& w = r.w();
  hush(w);
  const Palette& pal = *appPalette(r.app);
  const Rect frame{0, 0, SS_WIDTH, SS_HEIGHT};
  int cases = 0;
  static const uint8_t kKinds[3] = {FK_LEAF, FK_STICK, FK_CONE};
  static const float kX[5] = {760.f, 600.f, 420.f, 200.f, 130.f};
  for (uint8_t kind : kKinds)
    for (float x : kX)
      for (int a = 0; a < 8; ++a)
        for (int ph = 0; ph < 3; ++ph) {
          Floater f = makeFloater(w, kind, x, -0.25f + 0.1f * (float)a, (float)a / 8.f, kind == FK_STICK ? 20.f : 13.f, 0.f);
          f.t = 0.37f * (float)a;
          if (ph == 1) { f.phase = FP_HELD; f.hold = 4.f; f.lane = LS_BOULDER_LANE - 0.18f; }
          if (ph == 2) { f.phase = FP_SWING; f.t = 0.8f; f.hold = 4.f; }
          w.stream.f[0] = f;
          const Rect b = streamFloaterBounds(w, 0);
          const Spill sp = drawCheck(w, b, 3.f, [&](Surface& s) { streamDrawFloater(w, 0, s, frame, pal); });
          ++cases;
          CHECK(sp.outside_bounds == 0, "floater kind %d x %.0f a %d phase %d: %d px outside its bounds", kind, x, a, ph, sp.outside_bounds);
          CHECK(sp.outside_water == 0, "floater kind %d x %.0f a %d phase %d: %d px outside the water", kind, x, a, ph, sp.outside_water);
          CHECK(x < 200.f || x > 800.f || sp.drawn > 6, "floater kind %d x %.0f: drew only %d px", kind, x, sp.drawn);
        }
  w.stream.f[0].kind = FK_NONE;
  // The fish: jumps both ways and rises at every 0.05 s of their lives, at both ends of their range.
  for (int kind = FISH_JUMP; kind <= FISH_RISE; ++kind)
    for (int dir = -1; dir <= 1; dir += 2)
      for (float x : {82.f, 300.f, 500.f})
        for (float lane : {-0.32f, -0.09f})
        for (float t = 0.f; t < 3.6f; t += 0.05f) {
          StreamFish& f = w.stream.fish;
          float cy, half;
          landscapeStreamSpan(w.land, x, cy, half);
          f.kind = (uint8_t)kind;
          f.dir = (int8_t)dir;
          f.x = (int16_t)x;
          f.y = (int16_t)floorf(cy - lane * half / 1.06f + 0.5f);   // as startFish places it (kFishLane*)
          f.t = t;
          const Rect b = streamFishBounds(w);
          const Rect fb = streamFoamBounds(w);
          Rect ok = b;
          unite(ok, fb);
          const Spill sp = drawCheck(w, ok, 3.f, [&](Surface& s) { streamDrawWater(w, s, frame, pal); });
          ++cases;
          CHECK(sp.outside_bounds == 0, "fish kind %d dir %d x %.0f t %.2f: %d px outside its bounds", kind, dir, x, t, sp.outside_bounds);
          CHECK(sp.outside_water == 0, "fish kind %d x %.0f t %.2f: %d px outside the water", kind, x, t, sp.outside_water);
        }
  w.stream.fish.kind = FISH_NONE;
  // The foam, the wake and the moon glint through their cycles; the near draw (foam) too.
  for (int st = 0; st < 80; ++st) {
    w.stream.step = (uint16_t)st;
    w.stream.moon = (uint8_t)(1 + (st & 15));
    const Rect fb = streamFoamBounds(w);
    Rect ok = fb;
    unite(ok, streamRockBounds(w.land));
    const Spill sp = drawCheck(w, ok, 3.f, [&](Surface& s) {
      streamDrawWater(w, s, frame, pal);
      streamDrawNear(w, s, frame, pal);
    });
    ++cases;
    CHECK(sp.outside_bounds == 0, "foam step %d: %d px outside its bounds", st, sp.outside_bounds);
    CHECK(sp.outside_water == 0, "foam step %d: %d px outside the water", st, sp.outside_water);
  }
  w.stream.moon = 0;
  // The rock (static) inside its bounds; the stones and the water's marks inside the water.
  {
    // Only the rock: the stones are drawn by the same call, so clip to the rock's own box and check it
    // filled most of it.
    const Spill sp = drawCheck(w, streamRockBounds(w.land), 0.f, [&](Surface& s) {
      streamDrawStaticRock(w.land, s, pal, streamRockBounds(w.land), 1.f);
    });
    CHECK(sp.outside_bounds == 0 && sp.drawn > 400, "the rock: %d px outside its bounds, %d drawn", sp.outside_bounds, sp.drawn);
    const Spill sp2 = drawCheck(w, frame, 2.f, [&](Surface& s) {
      streamDrawStaticRock(w.land, s, pal, frame, 1.f);
      streamDrawStaticWater(w.land, s, pal, frame);
    });
    CHECK(sp2.outside_water == 0, "the boulder, stones and marks: %d px outside the water", sp2.outside_water);
  }
  // The mock visitors inside their bounds.
  for (uint8_t m = SM_DUCK; m <= SM_DRAGONFLY; ++m)
    for (int t = 0; t < 75; t += 3) {
      w.stream.mock = m;
      w.tick += 3;   // the mock moves on the World's clock
      Rect ok = streamMockBounds(w);
      unite(ok, streamFoamBounds(w));
      unite(ok, streamRockBounds(w.land));
      const Spill sp2 = drawCheck(w, ok, 40.f, [&](Surface& s) { streamDrawNear(w, s, frame, pal); });
      ++cases;
      CHECK(sp2.outside_bounds == 0, "mock %d t %d: %d px outside its bounds", m, t, sp2.outside_bounds);
    }
  w.stream.mock = SM_NONE;
  printf("bounds: %d cases\n", cases);
  r.stop();
}

// Poses a leaf, a snagging stick and a fish into a running World (the same in every run).
static void poseLife(World& w) {
  hush(w);
  w.stream.f[0] = makeFloater(w, FK_STICK, 700.f, LS_BOULDER_LANE - 0.18f, 0.05f, 17.f, 4.f);
  w.stream.f[1] = makeFloater(w, FK_LEAF, 560.f, 0.3f, 0.2f, 12.f, 0.f);
  float cy, half;
  landscapeStreamSpan(w.land, 420.f, cy, half);
  w.stream.fish = StreamFish{FISH_JUMP, 1, 420, (int16_t)floorf(cy), 0.f};
  w.stream.next_float = w.tick + 25u * 20u;   // and the stream's own schedule after that
  w.stream.next_fish = w.tick + 25u * 9u;
}

// (2) Incremental rendering equals a full render, in each camp family and at night (the moon glint),
// with the posed life and the stream's own; two runs from one seed draw the same frames.
static void testIncremental() {
  struct Case { int family; float mood; bool night; int weather; };
  static const Case kCases[] = {{SF_EVENING, 4.5f, false, 0}, {SF_MORNING, 1.5f, false, 0}, {SF_MIDDAY, 3.f, false, 0},
                                {SF_EVENING, 0.f, true, 0},    {SF_MIDDAY, 3.f, false, 1}};
  for (const Case& c : kCases) {
    Rig a, b, d;
    const Inputs in = inputsFor(c.family, c.mood, c.night, c.weather);
    CHECK(a.start(21, in) && b.start(21, in) && d.start(21, in), "start");
    int checks = 0, bad = 0, diff = 0;
    for (int t = 0; t < 3600; ++t) {
      if (t == 600) {
        poseLife(a.w());
        poseLife(b.w());
        poseLife(d.w());
      }
      appTick(a.app);
      appTick(b.app);
      appTick(d.app);
      appRender(a.app, a.surf);
      appRender(d.app, d.surf);
      if (t > 400 && t % 37 == 0) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        if (memcmp(a.fb.data(), b.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) {
          ++bad;
          if (bad == 1) {
            for (size_t i = 0; i < a.fb.size(); ++i)
              if (a.fb[i] != b.fb[i]) {
                printf("  family %d tick %d: first difference at x %d y %d\n", c.family, t, (int)(i % SS_WIDTH), (int)(i / SS_WIDTH));
                break;
              }
          }
        }
        if (memcmp(a.fb.data(), d.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) ++diff;
      }
    }
    CHECK(bad == 0, "family %d mood %.1f night %d weather %d: %d of %d incremental frames differ from a full render", c.family,
          c.mood, c.night, c.weather, bad, checks);
    CHECK(diff == 0, "family %d: two runs from one seed differ in %d of %d frames", c.family, diff, checks);
    a.stop();
    b.stop();
    d.stop();
  }
}

// (3) The loop RNG is not the stream's: scrambling the stream's own Rng mid-loop leaves every later
// loop seed and plan as they were.
static void testLoopRngUntouched() {
  Rig a, b;
  const Inputs in = inputsFor(SF_MIDDAY, 3.f);
  CHECK(a.start(9, in) && b.start(9, in), "start");
  int loops = 0;
  for (int t = 0; t < 25 * 1400 && loops < 2; ++t) {
    if (t == 3000) b.w().stream.rng.seed(12345u);
    if (t == 3001) b.w().stream.next_float = b.w().tick + 3u;   // and spend a few draws
    const bool na = appTick(a.app), nb = appTick(b.app);
    CHECK(na == nb, "the loops end at different ticks (%d)", t);
    if (na) {
      ++loops;
      CHECK(appLoopSeed(a.app) == appLoopSeed(b.app), "loop %d: the loop seed moved with the stream's draws", loops);
      char pa[400], pb[400];
      appLoopPlan(a.app, pa, sizeof pa);
      appLoopPlan(b.app, pb, sizeof pb);
      CHECK(strcmp(pa, pb) == 0, "loop %d: plans differ:\n  %s\n  %s", loops, pa, pb);
    }
  }
  CHECK(loops == 2, "only %d loops", loops);
  a.stop();
  b.stop();
}

// (4) Floater lifecycle and the fish: over about 40 minutes of the camp families, a floater every 20 to
// 40 s on average, each one gone past the left edge within a few minutes; snags happen; fish jump
// every few minutes, rise rings more often, and never leave the water's x range they were given.
static void testLifecycle() {
  int spawns = 0, snags = 0, jumps = 0, rises = 0, longest = 0;
  double ticks = 0.;
  static const int kFam[3] = {SF_EVENING, SF_MORNING, SF_MIDDAY};
  static const float kMood[3] = {4.5f, 1.5f, 3.f};
  for (int k = 0; k < 3; ++k) {
    Rig r;
    CHECK(r.start(31 + (uint64_t)k, inputsFor(kFam[k], kMood[k])), "start");
    uint8_t was[STREAM_FLOATERS] = {0, 0};
    uint8_t was_phase[STREAM_FLOATERS] = {0, 0};
    int age[STREAM_FLOATERS] = {0, 0};
    uint8_t fish_was = 0;
    for (int t = 0; t < 25 * 800; ++t) {
      appTick(r.app);
      const World& w = *appWorld(r.app);
      if (!buildDone(w.build)) continue;
      ticks += 1.;
      for (int i = 0; i < STREAM_FLOATERS; ++i) {
        const Floater& f = w.stream.f[i];
        if (f.kind != FK_NONE && (was[i] == FK_NONE || age[i] == 0)) ++spawns, age[i] = 0;
        if (f.kind != FK_NONE) {
          ++age[i];
          if (age[i] > longest) longest = age[i];
          if (f.phase == FP_HELD && was_phase[i] != FP_HELD) ++snags;
          CHECK(f.lane > -0.95f && f.lane < 0.95f, "a floater left the water: lane %.2f", f.lane);
        } else {
          age[i] = 0;
        }
        was[i] = f.kind;
        was_phase[i] = f.phase;
      }
      if (w.stream.fish.kind != FISH_NONE && fish_was == FISH_NONE) {
        if (w.stream.fish.kind == FISH_JUMP) ++jumps;
        else ++rises;
        CHECK(w.stream.fish.x >= 79 && w.stream.fish.x <= 541, "a fish at x %d", w.stream.fish.x);
      }
      fish_was = w.stream.fish.kind;
    }
    r.stop();
  }
  const double mins = ticks / 25. / 60.;
  const double gap = ticks / 25. / (spawns > 0 ? spawns : 1);
  printf("lifecycle: %.1f min, %d floaters (one per %.1f s), %d snagged, longest afloat %.0f s, %d jumps, %d rises\n", mins,
         spawns, gap, snags, longest / 25., jumps, rises);
  CHECK(gap >= 20. && gap <= 42., "a floater every %.1f s (want 20 to 40)", gap);
  CHECK(snags >= 3, "only %d snags", snags);
  CHECK(longest < 25 * 150, "a floater stayed %d ticks", longest);
  CHECK(jumps >= 3 && jumps < rises, "%d jumps, %d rises", jumps, rises);
  CHECK(mins / (jumps > 0 ? jumps : 1) >= 1.2 && mins / (jumps > 0 ? jumps : 1) <= 6., "a jump every %.1f min", mins / (jumps > 0 ? jumps : 1));
}

// (5) Snag and release: a stick aimed at the boulder stops on its upstream face, holds for its hold
// time, swings free to one side and drifts on past; a leaf not aimed at it passes by.
static void testSnag() {
  Rig r;
  CHECK(r.start(5, inputsFor(SF_MIDDAY, 3.f)), "start");
  for (int t = 0; t < 500; ++t) appTick(r.app);
  World& w = r.w();
  hush(w);
  const float hold = 4.4f;
  w.stream.f[0] = makeFloater(w, FK_STICK, 720.f, LS_BOULDER_LANE - 0.18f, 0.05f, 17.f, hold);
  w.stream.f[1] = makeFloater(w, FK_LEAF, 720.f, 0.4f, 0.1f, 12.f, 0.f);
  int held = 0, swing = 0, t_free = -1;
  float s_held = -1.f, lane_free = 0.f;
  bool held_moved = false, leaf_stopped = false;
  float leaf_s = w.stream.f[1].s;
  for (int t = 0; t < 25 * 90 && w.stream.f[0].kind != FK_NONE; ++t) {
    appTick(r.app);
    const Floater& f = w.stream.f[0];
    if (f.phase == FP_HELD) {
      if (s_held < 0.f) s_held = f.s;
      else if (f.s != s_held) held_moved = true;
      ++held;
    }
    if (f.phase == FP_SWING) ++swing;
    if (f.phase == FP_FREE && t_free < 0) {
      t_free = t;
      lane_free = f.lane;
    }
    if (w.stream.f[1].kind != FK_NONE) {
      if (w.stream.f[1].s <= leaf_s) leaf_stopped = true;
      leaf_s = w.stream.f[1].s;
      CHECK(w.stream.f[1].phase == FP_DRIFT, "the leaf snagged");
    }
  }
  float bs;
  landscapeBoulder(w.land, &bs);
  CHECK(s_held > 0.f && s_held < bs, "held at s %.3f, the boulder at %.3f", s_held, bs);
  CHECK(!held_moved, "it moved while held");
  CHECK(abs(held - (int)(hold * 25.f)) <= 1, "held %d ticks, want %d", held, (int)(hold * 25.f));
  CHECK(swing >= 38 && swing <= 42, "swung %d ticks", swing);
  CHECK(t_free > 0 && fabsf(lane_free - LS_BOULDER_LANE) > 0.3f, "free at tick %d in lane %.2f", t_free, lane_free);
  CHECK(w.stream.f[0].kind == FK_NONE, "the stick never left");
  CHECK(!leaf_stopped, "the leaf stopped");
  // The water parts: at the boulder a lane through it is pushed out past its foot; far away it is not.
  const float W = 6.f / 22.f;
  for (float l = -0.3f; l <= 0.1f; l += 0.05f) {
    const float p = streamPartLane(w.land, bs, l, 0.f);
    CHECK(fabsf(p - LS_BOULDER_LANE) >= W * 0.9f, "lane %.2f at the boulder: %.2f", l, p);
    CHECK(streamPartLane(w.land, bs - 1.5f, l, 0.f) == l && streamPartLane(w.land, bs + 1.5f, l, 0.f) == l, "lane %.2f moved away from it", l);
  }
  r.stop();
}

// (6) Nothing shows through the rock: its middle stays the same pixel for minutes of glints, floaters
// and fish going by, in each camp family.
static void testRockSolid() {
  static const int kFam[3] = {SF_EVENING, SF_MORNING, SF_MIDDAY};
  static const float kMood[3] = {4.5f, 1.5f, 3.f};
  for (int k = 0; k < 3; ++k) {
    Rig r;
    CHECK(r.start(77, inputsFor(kFam[k], kMood[k])), "start");
    Pixel first = 0;
    int changed = 0, seen = 0;
    for (int t = 0; t < 25 * 240; ++t) {
      appTick(r.app);
      appRender(r.app, r.surf);
      const World& w = *appWorld(r.app);
      if (!buildDone(w.build) || appPaletteRebuilds(r.app) > 1) continue;
      if (t % 300 == 0) {   // a floater straight at it now and then
        World& ww = r.w();
        if (ww.stream.f[1].kind == FK_NONE) ww.stream.f[1] = makeFloater(ww, FK_LEAF, 700.f, LS_BOULDER_LANE + 0.05f, 0.f, 12.f, 0.f);
      }
      const Vec2 b = landscapeBoulder(w.land);
      const Pixel p = r.fb[(size_t)((int)b.y - 11) * SS_WIDTH + (int)b.x - 3];
      if (seen++ == 0) first = p;
      else if (p != first) ++changed;
    }
    CHECK(changed == 0, "family %d: the rock's middle changed in %d of %d frames", kFam[k], changed, seen);
    r.stop();
  }
}

// (7) The Lookout and the sunrise: no stream life at all.
static void testSetPieces() {
  Inputs in = inputsFor(SF_EVENING, 4.5f);
  in.spine_force = 0;
  in.lookout_window = true;
  Rig r;
  CHECK(r.start(3, in), "start");
  CHECK(appSpineFamily(r.app) == SF_LOOKOUT, "not the Lookout (%d)", appSpineFamily(r.app));
  for (int t = 0; t < 25 * 120; ++t) appTick(r.app);
  const World& w = *appWorld(r.app);
  // The stream's state shares the Lookout's bytes (world.h), so it is not looked at here: what the
  // Lookout must not see is anything of the stream drawn or sorted (and test_lookout pins its frames).
  bool any = false;
  for (int i = 0; i < w.n_actors; ++i) any |= w.actors[i].kind == ACT_STREAM;
  CHECK(!any, "the boulder is an actor in the Lookout");
  r.stop();
}

// ---- round 2: the visitors and the months -----------------------------------------------------

static bool inChipBox(const Rect& r) { return r.x1 > 538 && r.y1 > 412 && !r.empty(); }

struct VisitLog {
  bool came = false, stood = false, left = false, done = false, spooked = false, drank = false, looked = false;
  int strikes = 0, rings = 0, stay_ticks = 0, bad_bounds = 0, in_corner = 0, while_busy = 0, max_crit = 0;
  int end_crit = -1;
};

// Runs one forced visit to its end (or `ticks`), checking every tick that the heron draws inside its
// bounds and keeps out of the chip's corner, that a visitor is never in during a busy beat, and the
// deer's actions and rings.
static VisitLog runVisit(uint64_t seed, int family, float mood, uint8_t kind, int ticks, bool spook = false, bool whole = false) {
  VisitLog lg;
  Rig r;
  CHECK(r.start(seed, inputsFor(family, mood)), "start");
  r.w().stream.visit.force = kind;
  const Palette* pal = appPalette(r.app);
  const Rect frame{0, 0, SS_WIDTH, SS_HEIGHT};
  float last_ring = -1.f;
  int spook_at = -1;
  for (int t = 0; t < ticks; ++t) {
    if (appTick(r.app)) {   // the loop ended: nothing of it may be left
      lg.end_crit = appCritterCount(r.app);
      break;
    }
    World& w = r.w();
    const StreamVisit& V = w.stream.visit;
    if (V.stage == GS_IN) lg.came = true;
    if (V.stage == GS_STAY) {
      lg.stood = true;
      ++lg.stay_ticks;
      if (spook && spook_at < 0 && lg.stay_ticks == 25 * 5) {   // he walks right up to it
        const Vec2 at = kind == SV_HERON ? V.spot : w.critters[V.slot].feet;
        w.hiker.feet = Vec2{at.x - 40.f, at.y - 8.f};
        w.hiker.visible = true;
        spook_at = t;
      }
    }
    if (V.stage == GS_OUT) {
      if (!lg.left && getenv("SS_VDEBUG"))
        printf("    left: beat %s busy %d fade %.2f fi %d rain %.2f night %d spooked %d t %.1f stay %.1f\n", w.minors.beat, w.busy,
               (double)w.fade, w.fi.on, (double)w.weather.rain, w.stream.night, V.spooked, (double)V.t, (double)V.stay);
      lg.left = true;
      if (V.spooked) lg.spooked = true;
    }
    if (V.stage == GS_DONE && lg.came) {
      lg.done = true;
      if (!spook && !whole) break;
    }
    // The deer leaves for any beat that owns the critters; the heron (no critter) only for one that uses
    // the water (stream.cpp kWaterMajors), so for it only fire and ice counts here.
    if ((V.stage == GS_IN || V.stage == GS_STAY) && ((kind == SV_DEER && w.busy) || w.fi.on)) ++lg.while_busy;
    if (V.ring_t >= 0.f && (last_ring < 0.f || V.ring_t < last_ring)) ++lg.rings;
    last_ring = V.ring_t;
    if (kind == SV_HERON && V.stage == GS_STAY && V.act == 4 && V.act_t < SS_TICK_DT * 1.5f) ++lg.strikes;
    if (kind == SV_DEER && V.slot >= 0 && w.crit[V.slot].in_use) {
      const Critter& c = w.critters[V.slot];
      if (c.action == CA_SNIFF) lg.drank = true;
      if (c.action == CA_LOOK) lg.looked = true;
      Rect cr;
      int k;
      if (appCritterRect(r.app, V.slot, &cr, &k) && cr.x1 > 660 && cr.y1 > 420) ++lg.in_corner;
    }
    int n = appCritterCount(r.app);
    if (n > lg.max_crit) lg.max_crit = n;
    const Rect vb = streamVisitBounds(w);
    if (inChipBox(vb)) ++lg.in_corner;
    if (!vb.empty() && t % 3 == 0) {
      const Spill sp = drawCheck(w, vb, 1000.f, [&](Surface& s) {
        streamDrawVisit(w, s, frame, *pal);
        streamDrawWater(w, s, vb, *pal);   // the ring (clipped: the rest of the water layer is not the visit's)
      });
      if (sp.outside_bounds) ++lg.bad_bounds;
    }
  }
  if (spook) CHECK(spook_at >= 0 && lg.spooked, "kind %d: walked up to at tick %d, spooked %d", kind, spook_at, lg.spooked);
  r.stop();
  return lg;
}

// (8) The heron: comes in, stands (a step, a look back, a strike now and then), leaves; inside its
// bounds and out of the chip's corner all the while; never in during a busy beat; it spooks.
static void testHeron() {
  int strikes = 0, rings = 0, came = 0, long_stays = 0;
  for (uint64_t seed = 41; seed < 46; ++seed) {
    const VisitLog lg = runVisit(seed, seed & 1 ? SF_MORNING : SF_EVENING, seed & 1 ? 1.5f : 4.6f, SV_HERON, 25 * 400);
    came += lg.came;
    long_stays += lg.stay_ticks >= 25 * 30;
    // It may find no quiet moment in its window (then it never comes); once in, it always leaves.
    CHECK(!lg.came || (lg.stood && lg.left && lg.done), "seed %llu: came %d stood %d left %d done %d", (unsigned long long)seed,
          lg.came, lg.stood, lg.left, lg.done);
    CHECK(lg.bad_bounds == 0, "seed %llu: the heron drew outside its bounds %d times", (unsigned long long)seed, lg.bad_bounds);
    CHECK(lg.in_corner == 0, "seed %llu: the heron in the chip's corner %d ticks", (unsigned long long)seed, lg.in_corner);
    CHECK(lg.while_busy == 0, "seed %llu: in during a busy beat %d ticks", (unsigned long long)seed, lg.while_busy);
    CHECK(lg.stay_ticks <= 25 * 91, "seed %llu: stood %d ticks", (unsigned long long)seed, lg.stay_ticks);
    strikes += lg.strikes;
    rings += lg.rings;
    printf("heron seed %llu: stood %.1f s, %d strikes, %d rings%s\n", (unsigned long long)seed, lg.stay_ticks / 25.f, lg.strikes,
           lg.rings, lg.spooked ? ", spooked" : "");
  }
  CHECK(came >= 4 && long_stays >= 2, "%d of 5 came, %d stood 30 s or more", came, long_stays);
  CHECK(strikes >= 1 && rings >= 1, "%d strikes, %d rings over five visits", strikes, rings);
  const VisitLog sp = runVisit(45, SF_MORNING, 1.5f, SV_HERON, 25 * 200, true);
  CHECK(sp.spooked && sp.done, "the heron did not fly off when he came close");
}

// (9) The deer: walks in, drinks (rings), looks round, drinks, walks off; its slot is freed, it never
// stands in the corner, and it runs when he comes close.
static void testDeer() {
  for (uint64_t seed = 51; seed < 55; ++seed) {
    const VisitLog lg = runVisit(seed, seed & 1 ? SF_MORNING : SF_EVENING, seed & 1 ? 1.2f : 4.8f, SV_DEER, 25 * 400);
    CHECK(lg.came && lg.stood && lg.left && lg.done, "seed %llu: came %d stood %d left %d done %d", (unsigned long long)seed, lg.came,
          lg.stood, lg.left, lg.done);
    CHECK(lg.drank && lg.looked && lg.rings >= 3, "seed %llu: drank %d looked %d rings %d", (unsigned long long)seed, lg.drank,
          lg.looked, lg.rings);
    CHECK(lg.in_corner == 0 && lg.bad_bounds == 0 && lg.while_busy == 0, "seed %llu: corner %d bounds %d busy %d",
          (unsigned long long)seed, lg.in_corner, lg.bad_bounds, lg.while_busy);
    printf("deer seed %llu: drank and looked, %d rings, stood %.1f s%s\n", (unsigned long long)seed, lg.rings, lg.stay_ticks / 25.f,
           lg.spooked ? ", spooked" : "");
  }
  const VisitLog sp = runVisit(57, SF_MIDDAY, 3.f, SV_DEER, 25 * 200, true);
  CHECK(sp.spooked && sp.done, "the deer did not run when he came close");
  // A whole loop with a deer: no critter left at its end.
  const VisitLog whole = runVisit(52, SF_EVENING, 4.8f, SV_DEER, 25 * 900, false, true);
  CHECK(whole.end_crit == 0, "critters left at the loop's end: %d", whole.end_crit);
}

// (10) Incremental equals full while a heron or a deer is in, and two runs agree.
static void testVisitIncremental() {
  static const uint8_t kKinds[2] = {SV_HERON, SV_DEER};
  for (uint8_t kind : kKinds) {
    Rig a, b, d;
    const Inputs in = inputsFor(SF_MORNING, 1.5f);
    CHECK(a.start(45, in) && b.start(45, in) && d.start(45, in), "start");
    a.w().stream.visit.force = kind;
    b.w().stream.visit.force = kind;
    d.w().stream.visit.force = kind;
    int checks = 0, bad = 0, diff = 0, present = 0;
    for (int t = 0; t < 25 * 160; ++t) {
      appTick(a.app);
      appTick(b.app);
      appTick(d.app);
      appRender(a.app, a.surf);
      appRender(d.app, d.surf);
      const StreamVisit& V = a.w().stream.visit;
      const bool in = V.stage >= GS_IN && V.stage <= GS_OUT;
      if (t > 400 && (t % 23 == 0 || (in && t % 5 == 0))) {
        present += in;
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        if (memcmp(a.fb.data(), b.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) {
          if (++bad == 1)
            for (size_t i = 0; i < a.fb.size(); ++i)
              if (a.fb[i] != b.fb[i]) {
                printf("  visit %d tick %d stage %d: first difference at x %d y %d\n", kind, t, V.stage, (int)(i % SS_WIDTH),
                       (int)(i / SS_WIDTH));
                break;
              }
        }
        if (memcmp(a.fb.data(), d.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) ++diff;
      }
    }
    CHECK(present > 50, "visit %d: present in only %d checked frames", kind, present);
    CHECK(bad == 0, "visit %d: %d of %d incremental frames differ from a full render", kind, bad, checks);
    CHECK(diff == 0, "visit %d: two runs differ in %d frames", kind, diff);
    a.stop();
    b.stop();
    d.stop();
  }
}

// (11) How often they come: the loop's roll over many first loops, by day and at dawn and dusk.
static void testVisitRates() {
  struct Case { int family; float mood; const char* name; };
  static const Case kCases[] = {{SF_MIDDAY, 3.f, "midday"}, {SF_EVENING, 4.8f, "dusk"}, {SF_MORNING, 1.f, "dawn"}};
  for (const Case& c : kCases) {
    int heron = 0, deer = 0;
    const int n = 1500;
    for (int i = 0; i < n; ++i) {
      Rig r;
      r.start(1000 + (uint64_t)i, inputsFor(c.family, c.mood));
      for (int t = 0; t < 340 && !r.w().stream.visit.planned; ++t) appTick(r.app);
      heron += r.w().stream.visit.kind == SV_HERON;
      deer += r.w().stream.visit.kind == SV_DEER;
      r.stop();
    }
    printf("visits, %s: heron %.2f%%, deer %.2f%% of loops\n", c.name, 100.f * heron / n, 100.f * deer / n);
    const bool twi = c.mood != 3.f;
    CHECK(fabsf((float)heron / n - (twi ? 0.024f : 0.008f)) < (twi ? 0.012f : 0.007f), "%s: heron %d of %d", c.name, heron, n);
    CHECK(fabsf((float)deer / n - (twi ? 0.030f : 0.010f)) < (twi ? 0.014f : 0.008f), "%s: deer %d of %d", c.name, deer, n);
  }
}

// (12) The months: many coloured leaves in mid autumn, a few green ones in summer, a very few brown in
// winter; month 0 is the year-round mix (and round 1's draws, unchanged).
static void testMonths() {
  struct M { uint8_t month; const char* name; };
  static const M kMonths[] = {{0, "unknown"}, {7, "summer"}, {10, "mid autumn"}, {1, "winter"}};
  float leaves_per_min[4];
  for (int k = 0; k < 4; ++k) {
    Inputs in = inputsFor(SF_MIDDAY, 3.f);
    in.month = kMonths[k].month;
    int tint[5] = {0, 0, 0, 0, 0}, leaves = 0, all = 0;
    int ticks = 0;
    for (uint64_t seed = 71; seed < 74; ++seed) {
    Rig r;
    CHECK(r.start(seed, in), "start");
    uint8_t was[STREAM_FLOATERS] = {0, 0};
    for (int t = 0; t < 25 * 1200; ++t) {
      appTick(r.app);
      const World& w = *appWorld(r.app);
      if (!buildDone(w.build)) continue;
      ++ticks;
      for (int i = 0; i < STREAM_FLOATERS; ++i) {
        const Floater& f = w.stream.f[i];
        if (f.kind != FK_NONE && was[i] == FK_NONE) {
          ++all;
          if (f.kind == FK_LEAF) {
            ++leaves;
            if (f.tint < 5) ++tint[f.tint];
          }
        }
        was[i] = f.kind;
      }
    }
    r.stop();
    }
    leaves_per_min[k] = leaves / (ticks / 25.f / 60.f);
    printf("month %s: %d floaters, %d leaves (%.1f a minute): orange %d russet %d gold %d green %d brown %d\n", kMonths[k].name, all,
           leaves, leaves_per_min[k], tint[0], tint[1], tint[2], tint[3], tint[4]);
    switch (kMonths[k].month) {
      case 7: CHECK(tint[3] >= leaves * 3 / 4 && tint[0] + tint[1] + tint[4] == 0, "summer leaves not green"); break;
      case 10: CHECK(tint[0] + tint[1] + tint[2] >= leaves * 8 / 10 && tint[4] == 0, "autumn leaves not coloured"); break;
      case 1: CHECK(tint[4] >= leaves / 2 && tint[3] == 0, "winter leaves not brown"); break;
      default: CHECK(tint[4] == 0, "brown leaves with no month"); break;
    }
  }
  CHECK(leaves_per_min[2] > 2.f * leaves_per_min[1] && leaves_per_min[1] > leaves_per_min[3],
        "leaves a minute: autumn %.1f, summer %.1f, winter %.1f", leaves_per_min[2], leaves_per_min[1], leaves_per_min[3]);
}

// (13) The house dogs and the visitors: Lucy, Copper or both out, a heron or a deer comes; a dog
// notices it, runs at it, the visitor goes while the dog is still well short of it, the dog pulls up on
// the camp side of the water, looks after it and goes back to its own life. Never in the chip's box;
// incremental equals full and two runs agree while it happens.
struct ChaseLog {
  bool chased = false, ran = false, gone_before = false, stopped_dry = false, back = false, spooked = false;
  float gap = 0.f;      // px between the dog and the visitor when the visitor went
  int in_box = 0, bad = 0, diff = 0, checks = 0;
};

static ChaseLog runChase(uint64_t seed, int family, float mood, uint8_t kind, int dog_out, bool compare) {
  ChaseLog lg;
  Inputs in = inputsFor(family, mood);
  in.dog_out = dog_out;
  Rig a, b, d;
  CHECK(a.start(seed, in), "start");
  if (compare) CHECK(b.start(seed, in) && d.start(seed, in), "start");
  a.w().stream.visit.force = kind;
  if (compare) {
    b.w().stream.visit.force = kind;
    d.w().stream.visit.force = kind;
  }
  int chaser = -1;
  bool was_out = false;
  for (int t = 0; t < 25 * 420; ++t) {
    if (appTick(a.app)) break;
    if (compare) {
      appTick(b.app);
      appTick(d.app);
    }
    World& w = a.w();
    const StreamVisit& V = w.stream.visit;
    for (int k = 0; k < HD_COUNT; ++k) {
      const HouseDog& h = w.dogs.d[k];
      if (h.act == HA_CHASE && h.slot >= 0) {
        lg.chased = true;
        chaser = k;
        if (h.step >= 1) lg.ran = true;
        if (h.step == 2) {   // pulled up: on the camp side of the water
          float cy, half;
          const Vec2 f = w.critters[h.slot].feet;
          if (!landscapeStreamSpan(w.land, f.x, cy, half) || f.y < cy - half + 1.f) lg.stopped_dry = true;
        }
      }
      if (h.slot >= 0 && w.crit[h.slot].in_use) {
        Rect cr;
        int kk;
        if (appCritterRect(a.app, h.slot, &cr, &kk) && inChipBox(cr)) ++lg.in_box;
      }
    }
    if (lg.chased && chaser >= 0 && w.dogs.d[chaser].act != HA_CHASE && w.dogs.d[chaser].phase == HP_HERE) lg.back = true;
    if (V.stage == GS_OUT && !was_out) {
      was_out = true;
      lg.spooked = V.spooked != 0;
      if (chaser >= 0 && w.dogs.d[chaser].slot >= 0) {
        const Vec2 f = w.critters[w.dogs.d[chaser].slot].feet;
        const Vec2 at = kind == SV_HERON ? V.spot : (V.slot >= 0 ? w.critters[V.slot].feet : V.spot);
        lg.gap = sqrtf((f.x - at.x) * (f.x - at.x) + (f.y - at.y) * (f.y - at.y));
        lg.gone_before = lg.gap > 40.f;
      }
    }
    if (compare && t > 400) {
      appRender(a.app, a.surf);
      appRender(d.app, d.surf);
      const bool live = lg.chased && !lg.back;
      if (t % 23 == 0 || (live && t % 4 == 0)) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++lg.checks;
        if (memcmp(a.fb.data(), b.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) ++lg.bad;
        if (memcmp(a.fb.data(), d.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) ++lg.diff;
      }
    }
    if (lg.back && was_out && V.stage == GS_DONE) break;
  }
  a.stop();
  if (compare) {
    b.stop();
    d.stop();
  }
  return lg;
}

static void testDogChase() {
  struct Case { uint64_t seed; int family; float mood; uint8_t kind; int dog_out; bool compare; const char* name; };
  static const Case kCases[] = {
      {45, SF_MORNING, 1.5f, SV_HERON, 1, true, "heron, Lucy"},   {43, SF_MORNING, 1.5f, SV_HERON, 2, false, "heron, Copper"},
      {41, SF_MORNING, 1.5f, SV_HERON, 3, false, "heron, both"},  {51, SF_MORNING, 1.2f, SV_DEER, 2, true, "deer, Copper"},
      {53, SF_MORNING, 1.2f, SV_DEER, 1, false, "deer, Lucy"},    {52, SF_EVENING, 4.8f, SV_DEER, 3, false, "deer, both"},
  };
  for (const Case& c : kCases) {
    const ChaseLog lg = runChase(c.seed, c.family, c.mood, c.kind, c.dog_out, c.compare);
    printf("dog chase, %s: chased %d ran %d visitor gone %s (%.0f px off) spooked %d dry %d back %d\n", c.name, lg.chased,
           lg.ran, lg.gone_before ? "first" : "late", (double)lg.gap, lg.spooked, lg.stopped_dry, lg.back);
    CHECK(lg.chased && lg.ran && lg.spooked, "%s: chased %d ran %d spooked %d", c.name, lg.chased, lg.ran, lg.spooked);
    CHECK(lg.gone_before, "%s: the visitor went only %.0f px ahead of the dog", c.name, (double)lg.gap);
    CHECK(lg.stopped_dry && lg.back, "%s: pulled up dry %d, back to its own life %d", c.name, lg.stopped_dry, lg.back);
    CHECK(lg.in_box == 0, "%s: a dog in the chip's box %d ticks", c.name, lg.in_box);
    if (c.compare) {
      CHECK(lg.checks > 50 && lg.bad == 0, "%s: %d of %d incremental frames differ from a full render", c.name, lg.bad, lg.checks);
      CHECK(lg.diff == 0, "%s: two runs differ in %d frames", c.name, lg.diff);
    }
  }
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));   // zeroed; the bounds checks draw without appRender
  printf("sizeof(App) = %u bytes (budget %d), sizeof(World) = %u, sizeof(StreamLife) = %u, sizeof(Landscape) = %u\n",
         appAppBytes(), SS_APP_BYTES, (unsigned)sizeof(World), (unsigned)sizeof(StreamLife), (unsigned)sizeof(Landscape));
  testBounds();
  testSnag();
  testLoopRngUntouched();
  testSetPieces();
  testIncremental();
  testRockSolid();
  testLifecycle();
  testHeron();
  testDeer();
  testVisitIncremental();
  testVisitRates();
  testMonths();
  testDogChase();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
