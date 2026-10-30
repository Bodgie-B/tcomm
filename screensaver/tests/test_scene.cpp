// Scene tests: determinism, dirty-strip correctness through the build-up and
// the ambient layer, the ambient strip budget, palette rebuild, the reserved
// corner, App size, and inspection images. Plain executable; returns non-zero on
// failure. Writes landscape_*.ppm in the current directory (run from build/).
#include "ss_app.h"

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

// (b) Strip budget with only the ambient layer running: average strips per
// frame over ticks 400..900, rendering every tick.
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

int main() {
  testAppSize();
  testDeterminism();
  testDirtyStrips();
  testAmbientBudget();
  testPaletteRebuild();
  testLoopRollover();
  testQuietCorner();
  writeImages();
  if (g_fail) {
    printf("%d failure(s)\n", g_fail);
    return 1;
  }
  printf("test_scene: all passed\n");
  return 0;
}
