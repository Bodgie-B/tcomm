// Scene tests: determinism, dirty-strip correctness through the build-up and
// the ambient layer, the ambient strip budget, palette rebuild, the reserved
// corner, App size, the story spine (determinism, quiet corner over a loop,
// every variant in frame and on time, strips and time per frame), props, and
// inspection images. Plain executable; returns non-zero on failure. Writes
// landscape_*.ppm and props_sheet.ppm in the current directory (run from build/).
#include "ss_app.h"
#include "scene/hiker.h"
#include "scene/props.h"

#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

// The reserved corner (scene/landscape.h; that header is core-internal).
#define LS_QUIET_X0_T 660
#define LS_QUIET_Y0_T 420

static int g_fail = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree};

struct Rig {
  void* mem;
  Pixel* fb;
  Surface surf;
  App* app;

  bool start(uint64_t seed, const Inputs& in) {
    mem = malloc(SS_APP_BYTES);
    fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
    surf = Surface{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = (mem && fb) ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    free(fb);
    app = nullptr;
  }
};

static Inputs makeInputs(float mood, bool night = false) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.weather.night_dim = night ? 1.0f : 0.0f;
  in.night_mode = night;
  in.have_outside = true;
  return in;
}

static bool sameFrame(const Rig& a, const Rig& b) {
  return memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) == 0;
}

static long firstDiff(const Rig& a, const Rig& b) {
  for (long i = 0; i < (long)SS_WIDTH * SS_HEIGHT; ++i) {
    if (a.fb[i] != b.fb[i]) return i;
  }
  return -1;
}

static bool writePPM(const char* path, const Pixel* fb) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", SS_WIDTH, SS_HEIGHT);
  for (int i = 0; i < SS_WIDTH * SS_HEIGHT; ++i) {
    const uint8_t px[3] = {red8(fb[i]), green8(fb[i]), blue8(fb[i])};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
  return true;
}

static void tickN(Rig& r, int n) {
  for (int i = 0; i < n; ++i) appTick(r.app);
}

// Build-up ends at tick 325; 400 is well into the ambient beat.
static const int kAfterBuild = 400;

// (a) Same seed and inputs give identical frames during and after the
// build-up; another seed differs. `a` renders every tick (incremental), `b`
// only at the checkpoints (each a full rebuild), so this also checks that the
// dirty strips reproduce a full render.
static void testDeterminism() {
  Rig a, b, c;
  const Inputs in = makeInputs(3.f);
  CHECK(a.start(42, in) && b.start(42, in) && c.start(43, in), "start");
  static const int kAt[3] = {50, 300, 700};
  int t = 0;
  for (int k = 0; k < 3; ++k) {
    for (; t < kAt[k]; ++t) {
      appTick(a.app);
      appTick(b.app);
      appTick(c.app);
      appRender(a.app, a.surf);
    }
    appInvalidate(b.app);
    appRender(b.app, b.surf);
    appRender(c.app, c.surf);
    CHECK(sameFrame(a, b), "same seed differs at tick %d, pixel %ld", t, firstDiff(a, b));
    CHECK(!sameFrame(a, c), "seed+1 produced an identical frame at tick %d", t);
  }
  a.stop(); b.stop(); c.stop();
}

// Incremental rendering equals a full render at every build-up phase and
// through a long ambient run (more than one cloud wrap: 1080 px at 4 px/s is
// 6750 ticks for the slower cloud).
static void testDirtyStrips() {
  Rig a, b;
  const Inputs in = makeInputs(4.f);
  CHECK(a.start(7, in) && b.start(7, in), "start");
  const int first = appRender(a.app, a.surf);
  CHECK(first == SS_STRIP_COUNT, "first render wrote %d strips, want %d", first, SS_STRIP_COUNT);

  // Checkpoints inside every build phase and at its boundaries (phase starts at
  // ticks 45, 95, 165, 220, 290; the beat ends at 325).
  static const int kAt[] = {1, 25, 44, 45, 46, 70, 95, 120, 150, 165, 190, 220, 240, 270, 290,
                            300, 310, 320, 325, 326, 340, 360, 400};
  int t = 0;
  for (int at : kAt) {
    for (; t < at; ++t) {
      appTick(a.app);
      appTick(b.app);
      const int n = appRender(a.app, a.surf);
      CHECK(n >= 0 && n <= SS_STRIP_COUNT, "tick %d wrote %d strips", t, n);
    }
    appInvalidate(b.app);
    appRender(b.app, b.surf);
    CHECK(sameFrame(a, b), "build-up: incremental differs from full at tick %d (phase %d), pixel %ld", t,
          appBuildPhase(a.app), firstDiff(a, b));
  }
  CHECK(appBuildPhase(a.app) == 6, "build-up not done by tick %d (phase %d)", t, appBuildPhase(a.app));

  for (int k = 0; k < 7000; ++k, ++t) {
    appTick(a.app);
    appTick(b.app);
    if (k % 5 == 0) appRender(a.app, a.surf);
    if (k % 700 == 699) {
      appRender(a.app, a.surf);
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      CHECK(sameFrame(a, b), "long run: differs at tick %d, pixel %ld", t, firstDiff(a, b));
    }
  }
  a.stop(); b.stop();
}

// (b) Strip budget just after the build-up: average strips per frame over
// ticks 400..900, rendering every tick. Since stage 4 the hiker walks in during
// this window; testSpineBudget measures the ambient-only pauses.
static void testAmbientBudget() {
  static const uint64_t kSeeds[3] = {11, 7, 42};
  for (uint64_t seed : kSeeds) {
    Rig a;
    CHECK(a.start(seed, makeInputs(3.f)), "start");
    for (int t = 0; t < kAfterBuild; ++t) {
      appTick(a.app);
      appRender(a.app, a.surf);
    }
    long sum = 0;
    int worst = 0, least = SS_STRIP_COUNT, frames = 0;
    for (int t = kAfterBuild; t < 900; ++t) {
      appTick(a.app);
      const int n = appRender(a.app, a.surf);
      sum += n;
      ++frames;
      if (n > worst) worst = n;
      if (n < least) least = n;
    }
    const float avg = (float)sum / (float)frames;
    printf("seed %d: ambient strips per frame over ticks %d..900: avg %.2f, min %d, max %d\n", (int)seed,
           kAfterBuild, (double)avg, least, worst);
    CHECK(avg <= 9.f, "seed %d: ambient average %.2f strips per frame (budget 9)", (int)seed, (double)avg);
    CHECK(least >= 1, "seed %d: a frame wrote no strips although the stream flows", (int)seed);
    a.stop();
  }
}

// (c) A palette change rebuilds the cache and every strip.
static void testPaletteRebuild() {
  Rig a;
  Inputs in = makeInputs(3.f);
  CHECK(a.start(5, in), "start");
  tickN(a, kAfterBuild);
  appRender(a.app, a.surf);
  appTick(a.app);
  const int quiet = appRender(a.app, a.surf);
  CHECK(quiet < SS_STRIP_COUNT, "unchanged inputs rebuilt everything (%d strips)", quiet);

  in.mood = 5.f;
  appSetInputs(a.app, in);
  appTick(a.app);
  const int n = appRender(a.app, a.surf);
  CHECK(n == SS_STRIP_COUNT, "mood 3 -> 5 wrote %d strips, want %d", n, SS_STRIP_COUNT);
  printf("mood 3 -> 5 rebuild wrote %d strips\n", n);

  // A weather change over the threshold does the same.
  appTick(a.app);
  appRender(a.app, a.surf);
  in.weather.overcast = 0.5f;
  appSetInputs(a.app, in);
  appTick(a.app);
  CHECK(appRender(a.app, a.surf) == SS_STRIP_COUNT, "overcast change did not rebuild");

  // A mood change below the 1/48 threshold keeps the palette: the ambient
  // layer still dirties some strips, but not all of them.
  appTick(a.app);
  appRender(a.app, a.surf);
  in.mood = 5.f + 1.f / 100.f;
  appSetInputs(a.app, in);
  appTick(a.app);
  const int sub_n = appRender(a.app, a.surf);
  CHECK(sub_n < SS_STRIP_COUNT, "sub-threshold mood change wrote %d strips", sub_n);

  // Night mode alone (weather untouched) rebuilds everything.
  appTick(a.app);
  appRender(a.app, a.surf);
  in.night_mode = true;
  appSetInputs(a.app, in);
  appTick(a.app);
  const int night_n = appRender(a.app, a.surf);
  CHECK(night_n == SS_STRIP_COUNT, "night_mode toggle wrote %d strips, want %d", night_n, SS_STRIP_COUNT);
  a.stop();
}

// Loop rollover: build-up plus ambient beat is 600 s, then a new place is generated.
static void testLoopRollover() {
  Rig a;
  CHECK(a.start(9, makeInputs(3.f)), "start");
  const uint64_t seed0 = appLoopSeed(a.app);
  appRender(a.app, a.surf);
  int loops = 0, at = -1;
  const int ticks = 600 * SS_TICK_HZ + 50;
  for (int t = 0; t < ticks; ++t) {
    if (appTick(a.app)) { ++loops; at = t + 1; }
  }
  printf("loop rollover at tick %d (nominal %d)\n", at, 600 * SS_TICK_HZ);
  CHECK(loops == 1, "expected one loop start in 600 s, got %d", loops);
  CHECK(at == 600 * SS_TICK_HZ, "rollover at tick %d, want exactly %d", at, 600 * SS_TICK_HZ);
  CHECK(appLoopSeed(a.app) != seed0, "loop seed did not change");
  CHECK(appRender(a.app, a.surf) == SS_STRIP_COUNT, "new loop did not rebuild the cache");
  a.stop();
}

// Count pixels in the reserved corner that are not ground, its dark side or grass.
static int badCornerPixels(const Rig& a, const Palette& pal, const char* what, int seed, int tick, int shown) {
  int bad = 0;
  for (int y = LS_QUIET_Y0_T; y < SS_HEIGHT; ++y) {
    for (int x = LS_QUIET_X0_T; x < SS_WIDTH; ++x) {
      const Pixel p = a.fb[y * SS_WIDTH + x];
      if (p != pal[GROUND] && p != pal[GROUND_DARK] && p != pal[GRASS]) {
        if (shown + bad < 5) {
          printf("%s seed %d tick %d: pixel (%d,%d) = %04x is not ground or grass\n", what, seed, tick, x, y, p);
        }
        ++bad;
      }
    }
  }
  return bad;
}

// (d) The bottom-right corner holds only ground and grass: 60 seeds over all
// moods once the build-up is done, and 3 seeds x 2 moods at several ticks
// during and after it.
static void testQuietCorner() {
  int bad = 0;
  for (uint64_t seed = 1; seed <= 60; ++seed) {
    const Inputs in = makeInputs((float)((seed - 1) % 6));   // moods 0..5
    Palette pal;
    computePalette(in.mood, in.weather, pal);
    Rig a;
    CHECK(a.start(seed, in), "start");
    tickN(a, 340);
    appRender(a.app, a.surf);
    bad += badCornerPixels(a, pal, "built", (int)seed, 340, bad);
    a.stop();
  }
  static const float kMoods[2] = {3.f, 5.f};
  static const int kAt[4] = {0, 100, 250, 500};
  for (uint64_t seed = 1; seed <= 3; ++seed) {
    for (float mood : kMoods) {
      const Inputs in = makeInputs(mood);
      Palette pal;
      computePalette(in.mood, in.weather, pal);
      Rig a;
      CHECK(a.start(seed, in), "start");
      int t = 0;
      for (int at : kAt) {
        for (; t < at; ++t) {
          appTick(a.app);
          appRender(a.app, a.surf);
        }
        appRender(a.app, a.surf);
        bad += badCornerPixels(a, pal, "ticks", (int)seed, t, bad);
      }
      a.stop();
    }
  }
  CHECK(bad == 0, "%d corner pixels are not GROUND, GROUND_DARK or GRASS", bad);
}

// sizeof(App) against the host budget.
static void testAppSize() {
  printf("sizeof(App) = %u bytes (budget %d)\n", (unsigned)appAppBytes(), SS_APP_BYTES);
  CHECK(appAppBytes() <= (uint32_t)SS_APP_BYTES, "App is %u bytes", (unsigned)appAppBytes());
}

// (e) Inspection images, after the build-up.
static void writeImages() {
  struct Shot { const char* path; float mood; bool night; };
  const Shot shots[4] = {{"landscape_day.ppm", 3.f, false},
                         {"landscape_golden.ppm", 4.f, false},
                         {"landscape_dusk.ppm", 5.f, false},
                         {"landscape_night.ppm", 0.f, true}};
  for (const Shot& s : shots) {
    Rig a;
    CHECK(a.start(11, makeInputs(s.mood, s.night)), "start");
    tickN(a, kAfterBuild);
    appRender(a.app, a.surf);
    CHECK(writePPM(s.path, a.fb), "could not write %s", s.path);
    a.stop();
  }
}


// ---- story spine (stage 4) ----------------------------------------------------

static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }

// Same seed, same frames through the spine. `a` renders every tick, `b` only
// at the checkpoints (full rebuild), so this also checks dirty strips with the
// hiker and props moving.
static void testSpineDeterminism() {
  Rig a, b;
  const Inputs in = makeInputs(3.f);
  CHECK(a.start(42, in) && b.start(42, in), "start");
  static const int kAt[4] = {500, 1500, 2500, 3500};
  int t = 0;
  for (int at : kAt) {
    for (; t < at; ++t) {
      appTick(a.app);
      appTick(b.app);
      appRender(a.app, a.surf);
    }
    appInvalidate(b.app);
    appRender(b.app, b.surf);
    CHECK(sameFrame(a, b), "spine: same seed differs at tick %d (%s), pixel %ld", t, appBeatName(a.app),
          firstDiff(a, b));
  }
  a.stop();
  b.stop();
}

// The reserved corner stays ground and grass for a whole loop of story.
static void testSpineQuietCorner() {
  int bad = 0;
  for (uint64_t seed = 1; seed <= 3; ++seed) {
    const Inputs in = makeInputs(3.f);
    Palette pal;
    computePalette(in.mood, in.weather, pal);
    Rig a;
    CHECK(a.start(seed, in), "start");
    for (int t = 1; t < 600 * SS_TICK_HZ; ++t) {
      appTick(a.app);
      if (t % 250 == 0) {
        appInvalidate(a.app);
        appRender(a.app, a.surf);
        bad += badCornerPixels(a, pal, "spine", (int)seed, t, bad);
      }
    }
    a.stop();
  }
  CHECK(bad == 0, "spine: %d corner pixels are not GROUND, GROUND_DARK or GRASS", bad);
}

static bool parsePlan(const App* a, int v[4]) {
  char buf[200];
  appLoopPlan(a, buf, sizeof buf);
  return sscanf(buf, "arrival=%d setup=%d firewood=%d fire=%d", &v[0], &v[1], &v[2], &v[3]) == 4;
}

// Every variant of the four built beats, found by trying seeds: while the
// hiker is visible his bounds stay in the frame (and out of the reserved
// corner), each beat ends with him in frame, and no script runs past its beat.
static void testSpineVariants() {
  static const char* const kBeats[4] = {"arrival", "setup", "firewood", "fire"};
  bool seen[4][3] = {};
  int seeds_run = 0;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs(3.f)), "start");
    int v[4];
    CHECK(parsePlan(a.app, v), "seed %d: plan line does not parse", (int)seed);
    bool fresh = false;
    for (int b = 0; b < 4; ++b) fresh = fresh || !seen[b][v[b]];
    if (!fresh) {
      a.stop();
      continue;
    }
    ++seeds_run;
    char prev[32] = "build";
    int t = 0, bad = 0;
    for (; t < 600 * SS_TICK_HZ && !beatIs(a.app, "dinner"); ++t) {
      appTick(a.app);
      Rect r;
      const bool vis = appHikerRect(a.app, &r);
      if (vis && bad < 3) {
        const bool in_frame = r.x1 > 0 && r.x0 < SS_WIDTH && r.y0 >= 0 && r.y1 <= SS_HEIGHT;
        const bool corner = r.x1 > LS_QUIET_X0_T && r.y1 > LS_QUIET_Y0_T;
        CHECK(in_frame && !corner, "seed %d tick %d (%s): hiker rect %d,%d,%d,%d", (int)seed, t,
              appBeatName(a.app), r.x0, r.y0, r.x1, r.y1);
        if (!in_frame || corner) ++bad;
      }
      if (!beatIs(a.app, prev)) {   // a beat just ended
        for (int b = 0; b < 4; ++b) {
          if (strcmp(prev, kBeats[b]) != 0) continue;
          CHECK(vis && r.x0 >= 0 && r.x1 <= SS_WIDTH, "seed %d: %s variant %d ends with the hiker out of frame",
                (int)seed, kBeats[b], v[b]);
          seen[b][v[b]] = true;
        }
        snprintf(prev, sizeof prev, "%s", appBeatName(a.app));
      }
    }
    CHECK(beatIs(a.app, "dinner"), "seed %d: never reached dinner (tick %d)", (int)seed, t);
    CHECK(appSpineOverruns(a.app) == 0, "seed %d (arrival=%d setup=%d firewood=%d fire=%d): %d beat(s) overran",
          (int)seed, v[0], v[1], v[2], v[3], appSpineOverruns(a.app));
    a.stop();
  }
  int missing = 0;
  for (int b = 0; b < 4; ++b)
    for (int k = 0; k < 3; ++k)
      if (!seen[b][k]) {
        ++missing;
        printf("variant %s %d never picked in 40 seeds\n", kBeats[b], k);
      }
  printf("spine variants: all 12 run in %d seeds\n", seeds_run);
  CHECK(missing == 0, "%d variants unseen", missing);
}

// Strips and time per frame over the spine (render every tick), overall and in
// the pauses, where only the ambient layer and a breathing hiker move.
static void testSpineBudget() {
  static const uint64_t kSeeds[3] = {11, 7, 42};
  for (uint64_t seed : kSeeds) {
    Rig a;
    CHECK(a.start(seed, makeInputs(3.f)), "start");
    long sum = 0, psum = 0;
    int frames = 0, pframes = 0, worst = 0;
    double us = 0.0;
    for (int t = 0; t < 600 * SS_TICK_HZ; ++t) {
      const auto t0 = std::chrono::steady_clock::now();
      appTick(a.app);
      const int n = appRender(a.app, a.surf);
      const auto t1 = std::chrono::steady_clock::now();
      if (beatIs(a.app, "dinner")) break;
      if (t < 330) continue;   // the build-up rebuilds everything; it has its own numbers
      us += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1000.0;
      sum += n;
      ++frames;
      if (n > worst) worst = n;
      if (beatIs(a.app, "pause")) {
        psum += n;
        ++pframes;
      }
    }
    const float avg = frames ? (float)sum / (float)frames : 0.f;
    const float pavg = pframes ? (float)psum / (float)pframes : 0.f;
    printf("seed %d: spine (arrival..fire) %d frames: avg %.2f strips, max %d, %.3f ms/frame; pauses avg %.2f strips\n",
           (int)seed, frames, (double)avg, worst, frames ? us / 1000.0 / frames : 0.0, (double)pavg);
    CHECK(pavg <= 9.f, "seed %d: pauses average %.2f strips per frame (budget 9)", (int)seed, (double)pavg);
    a.stop();
  }
}

// hikerHandsMid (props.cpp) mirrors the rig's kinematics: the point just above
// the hands' midpoint is inside the carried wood in every pose and both facings.
static void testHandsMid() {
  static Pixel buf[200 * 160];
  Surface s{buf, 200, 160, 200, 0};
  Palette pal;
  computePalette(3.f, WeatherTint{0.f, 0.f, 0.f}, pal);
  int bad = 0;
  for (int p = 0; p < POSE_COUNT; ++p) {
    for (int f = -1; f <= 1; f += 2) {
      Hiker h;
      hikerInit(h, Vec2{100.f, 140.f}, f);
      hikerCarry(h, CARRY_WOOD);
      hikerSetPose(h, (PoseId)p, 0.f);
      hikerTick(h, 0.f);
      for (Pixel& px : buf) px = pal[SKY4];
      hikerDraw(h, s, pal, Rect{0, 0, 200, 160});
      const Vec2 m = hikerHandsMid(h);
      const int x = (int)m.x, y = (int)(m.y - 0.02f * h.height);
      const bool ok = x >= 0 && x < 200 && y >= 0 && y < 160 && buf[y * 200 + x] == pal[ACTOR];
      if (!ok && bad < 4) printf("hands mid off the wood: pose %s facing %d at (%d,%d)\n", hikerPoseName((PoseId)p), f, x, y);
      if (!ok) ++bad;
    }
  }
  CHECK(bad == 0, "hikerHandsMid misses the carried wood in %d pose/facing cases", bad);
}

// Props: bounds cover every drawn pixel, and an inspection sheet.
static int propsOutside(const Pixel* fb, Pixel bg, const Rect& r) {
  int n = 0;
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x)
      if (fb[y * SS_WIDTH + x] != bg && (x < r.x0 || x >= r.x1 || y < r.y0 || y >= r.y1)) ++n;
  return n;
}

static void testProps() {
  Pixel* fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
  Pixel* sheet = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
  Surface s{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  Surface sh{sheet, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  const Rect all{0, 0, SS_WIDTH, SS_HEIGHT};
  Palette pal;
  computePalette(3.f, WeatherTint{0.f, 0.f, 0.f}, pal);
  const Pixel bg = pal[GROUND];
  fillRect(sh, all, bg, all);
  int leaks = 0;
  auto check = [&](const char* what, const Rect& r, auto draw) {
    fillRect(s, all, bg, all);
    draw(s);
    const int n = propsOutside(fb, bg, r);
    if (n) printf("%s: %d pixels outside its bounds\n", what, n);
    leaks += n;
    draw(sh);
  };
  // Row 1: the tent going up, slumped, collapsed, lit with a shadow.
  static const float kProg[5] = {0.05f, 0.3f, 0.6f, 0.85f, 1.f};
  for (int i = 0; i < 8; ++i) {
    Tent t;
    tentInit(t, Vec2{60.f + 95.f * (float)i, 100.f});
    t.state = TENT_RAISING;
    if (i < 5) t.progress = kProg[i];
    else {
      t.progress = i == 5 ? 0.72f : 1.f;
      t.state = i == 5 ? TENT_COLLAPSED : (i == 6 ? TENT_COLLAPSED : TENT_UP);
      t.sag = i == 5 ? 0.55f : (i == 6 ? 1.f : 0.f);
      t.wobble = i == 5 ? -6.f : 0.f;
      t.lit = t.shadow = i == 7;
    }
    check("tent", tentBounds(t), [&](Surface& d) { tentDraw(t, d, all, pal); });
  }
  // Row 2: pack both ways, bag hung (still, swinging), dropped; cairn 1..7.
  for (int f = 0; f < 2; ++f) {
    const PackProp p{Vec2{40.f + 40.f * (float)f, 200.f}, (int8_t)(f ? -1 : 1), true, false};
    check("pack", packBounds(p), [&](Surface& d) { packDraw(p, d, all, pal); });
  }
  for (int i = 0; i < 3; ++i) {
    FoodBag b;
    bagInit(b, Vec2{150.f + 45.f * (float)i, 160.f});
    b.state = i < 2 ? BAG_HUNG : BAG_DROPPED;
    b.swing = i == 1 ? 0.5f : 0.f;
    b.pos = Vec2{b.tip.x, 200.f};
    check("bag", bagBounds(b), [&](Surface& d) { bagDraw(b, d, all, pal); });
  }
  for (int n = 1; n <= CAIRN_MAX; ++n) {
    const Cairn c{Vec2{300.f + 30.f * (float)n, 200.f}, (uint8_t)n, n == CAIRN_MAX ? 0.6f : 1.f};
    check("cairn", cairnBounds(c), [&](Surface& d) { cairnDraw(c, d, all, pal); });
  }
  // Row 3: every item, both facings; the wood pile at 1 and 1.65; a dragged stick.
  for (int id = 0; id < ITEM_COUNT; ++id) {
    for (int f = 0; f < 2; ++f) {
      Item it{Vec2{50.f + 120.f * (float)id + 50.f * (float)f, 300.f}, true, false, (int8_t)(f ? -1 : 1),
              id == ITEM_WOOD && f ? 1.65f : 1.f, 300.f};
      if (id == ITEM_STICK && f) {
        it.down = false;
        it.held = true;
        it.pos = Vec2{it.pos.x + 30.f, 270.f};
      }
      check(itemName((ItemId)id), itemBounds((ItemId)id, it), [&](Surface& d) { itemDraw((ItemId)id, it, d, all, pal); });
    }
  }
  // Row 4: the armload over a walking hiker with wood.
  Hiker h;
  hikerInit(h, Vec2{100.f, 440.f}, 1);
  hikerCarry(h, CARRY_WOOD);
  hikerSetPose(h, POSE_CARRY, 0.f);
  hikerTick(h, 0.f);
  check("armload", armloadBounds(h, 1.f), [&](Surface& d) { armloadDraw(h, 1.f, d, all, pal); });
  hikerDraw(h, sh, pal, all);
  CHECK(leaks == 0, "props draw %d pixels outside their bounds", leaks);
  CHECK(writePPM("props_sheet.ppm", sheet), "could not write props_sheet.ppm");
  free(fb);
  free(sheet);
}

int main() {
  testAppSize();
  testDeterminism();
  testDirtyStrips();
  testAmbientBudget();
  testPaletteRebuild();
  testLoopRollover();
  testQuietCorner();
  testHandsMid();
  testProps();
  testSpineDeterminism();
  testSpineQuietCorner();
  testSpineVariants();
  testSpineBudget();
  writeImages();
  if (g_fail) {
    printf("%d failure(s)\n", g_fail);
    return 1;
  }
  printf("test_scene: all passed\n");
  return 0;
}
