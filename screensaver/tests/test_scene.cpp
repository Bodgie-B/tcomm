// Scene tests: determinism, dirty-strip correctness, palette rebuild, the
// reserved corner, and inspection images. Plain executable; returns non-zero on
// failure. Writes landscape_*.ppm in the current directory (run from build/).
#include "ss_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

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

// (a) Same seed and inputs give identical frames; another seed differs.
static void testDeterminism() {
  Rig a, b, c;
  const Inputs in = makeInputs(3.f);
  CHECK(a.start(42, in) && b.start(42, in) && c.start(43, in), "start");
  for (int t = 0; t < 60; ++t) { appTick(a.app); appTick(b.app); appTick(c.app); }
  appRender(a.app, a.surf);
  appRender(b.app, b.surf);
  appRender(c.app, c.surf);
  CHECK(sameFrame(a, b), "same seed differs at pixel %ld", firstDiff(a, b));
  CHECK(!sameFrame(a, c), "seed+1 produced an identical frame");
  a.stop(); b.stop(); c.stop();
}

// (b) Only clouds move: few strips per frame, and the result equals a full render.
static void testDirtyStrips() {
  Rig a, b;
  const Inputs in = makeInputs(3.f);
  CHECK(a.start(7, in) && b.start(7, in), "start");
  const int first = appRender(a.app, a.surf);
  CHECK(first == SS_STRIP_COUNT, "first render wrote %d strips, want %d", first, SS_STRIP_COUNT);

  int worst = 0;
  printf("strips per frame over 10 cloud-only ticks:");
  for (int t = 0; t < 10; ++t) {
    appTick(a.app);
    appTick(b.app);
    const int n = appRender(a.app, a.surf);
    printf(" %d", n);
    if (n > worst) worst = n;
    CHECK(n <= 6, "tick %d wrote %d strips (max 6)", t, n);
    CHECK(n >= 1, "tick %d wrote no strips although clouds moved", t);
  }
  printf("  (max %d)\n", worst);
  appInvalidate(b.app);
  appRender(b.app, b.surf);
  CHECK(sameFrame(a, b), "incremental render differs from full render at pixel %ld", firstDiff(a, b));

  // Longer run, rendering every 5th tick, checked against a full render of the
  // twin at checkpoints. 7000 ticks is more than one cloud wrap (1080 px at 4 px/s
  // is 6750 ticks for the slower cloud, 4500 for the faster one).
  for (int t = 0; t < 7000; ++t) {
    appTick(a.app);
    appTick(b.app);
    if (t % 5 == 0) appRender(a.app, a.surf);
    if (t % 700 == 699) {
      appRender(a.app, a.surf);
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      CHECK(sameFrame(a, b), "long run: differs at tick %d, pixel %ld", t, firstDiff(a, b));
    }
  }
  a.stop(); b.stop();
}

// (c) A palette change rebuilds the cache and every strip.
static void testPaletteRebuild() {
  Rig a;
  Inputs in = makeInputs(3.f);
  CHECK(a.start(5, in), "start");
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

  // A mood change below the 1/48 threshold keeps the palette: clouds still dirty
  // a few strips, but not all of them.
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

// Loop rollover: the placeholder beat is 600 s, then a new place is generated.
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

// (d) The bottom-right corner holds only ground and grass.
static void testQuietCorner() {
  int bad = 0;
  for (uint64_t seed = 1; seed <= 60; ++seed) {
    const Inputs in = makeInputs((float)((seed - 1) % 6));   // moods 0..5
    Palette pal;
    computePalette(in.mood, in.weather, pal);
    Rig a;
    CHECK(a.start(seed, in), "start");
    appRender(a.app, a.surf);
    for (int y = 420; y < SS_HEIGHT; ++y) {
      for (int x = 660; x < SS_WIDTH; ++x) {
        const Pixel p = a.fb[y * SS_WIDTH + x];
        if (p != pal[GROUND] && p != pal[GRASS]) {
          if (bad < 5) printf("seed %d: pixel (%d,%d) = %04x is not ground or grass\n", (int)seed, x, y, p);
          ++bad;
        }
      }
    }
    a.stop();
  }
  CHECK(bad == 0, "%d corner pixels are not GROUND or GRASS", bad);
}

// (e) Inspection images.
static void writeImages() {
  struct Shot { const char* path; float mood; bool night; };
  const Shot shots[4] = {{"landscape_day.ppm", 3.f, false},
                         {"landscape_golden.ppm", 4.f, false},
                         {"landscape_dusk.ppm", 5.f, false},
                         {"landscape_night.ppm", 0.f, true}};
  for (const Shot& s : shots) {
    Rig a;
    CHECK(a.start(11, makeInputs(s.mood, s.night)), "start");
    appRender(a.app, a.surf);
    CHECK(writePPM(s.path, a.fb), "could not write %s", s.path);
    a.stop();
  }
}

int main() {
  testDeterminism();
  testDirtyStrips();
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
