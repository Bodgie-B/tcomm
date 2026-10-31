// Scene tests: determinism, dirty-strip correctness through the build-up and
// the ambient layer, the ambient strip budget, palette rebuild, the reserved
// corner, App size, the story spine (determinism, quiet corner over two loops,
// every variant and filler in frame and on time, loop length, the loop fade,
// strips and time per frame over a whole loop and the bear beat), critters in
// the scene (in frame, out of the corner, clear of the walking hiker, the
// dinner and theft hooks, none left at a loop's end), loop memory, props, and
// inspection images. Plain executable; returns non-zero on failure. Writes
// landscape_*.ppm and props_sheet.ppm in the current directory (run from build/).
#include "ss_app.h"
#include "scene/director.h"
#include "scene/hiker.h"
#include "scene/props.h"
#include "scene/world.h"
#include "scene/landscape.h"
#include <math.h>

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

// Loop rollover: the loop ends with the close, at exactly the plan's length,
// then a new place is generated.
static void testLoopRollover() {
  Rig a;
  CHECK(a.start(9, makeInputs(3.f)), "start");
  const uint64_t seed0 = appLoopSeed(a.app);
  const int want = (int)appLoopTicks(a.app);
  appRender(a.app, a.surf);
  int loops = 0, at = -1;
  const int ticks = want + 50;
  for (int t = 0; t < ticks; ++t) {
    if (appTick(a.app)) { ++loops; at = t + 1; }
  }
  printf("loop rollover at tick %d (plan %d)\n", at, want);
  CHECK(loops == 1, "expected one loop start in %d ticks, got %d", ticks, loops);
  CHECK(at == want, "rollover at tick %d, want exactly %d", at, want);
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
  // Tick 0 is black (a loop opens from the last one's fade), so the first
  // sample is at 30, when the fade is gone.
  static const int kAt[4] = {30, 100, 250, 500};
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
  static const int kAt[9] = {500, 1500, 2500, 3500, 6000, 7000, 9000, 12000, 13000};
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

// The reserved corner stays ground and grass for two whole loops of story,
// sampled every 250 ticks (samples inside the loop fade, where everything
// darkens on purpose, are skipped and counted).
static void testSpineQuietCorner() {
  int bad = 0, samples = 0, faded = 0;
  for (uint64_t seed = 1; seed <= 3; ++seed) {
    const Inputs in = makeInputs(3.f);
    Palette pal;
    computePalette(in.mood, in.weather, pal);
    Rig a;
    CHECK(a.start(seed, in), "start");
    int loops = 0;
    for (int t = 1; loops < 2; ++t) {
      if (appTick(a.app)) ++loops;
      if (t % 250 == 0) {
        if (appFade(a.app) > 0.f) {
          ++faded;
          continue;
        }
        ++samples;
        appInvalidate(a.app);
        appRender(a.app, a.surf);
        bad += badCornerPixels(a, pal, "spine", (int)seed, t, bad);
      }
    }
    a.stop();
  }
  printf("spine quiet corner: %d samples over two loops x 3 seeds (%d in a fade skipped)\n", samples, faded);
  CHECK(bad == 0, "spine: %d corner pixels are not GROUND, GROUND_DARK or GRASS", bad);
  CHECK(faded <= 6, "%d samples fell in a fade", faded);
}

static const char* const kBeats[8] = {"arrival", "setup", "firewood", "fire", "dinner", "foodbag", "bear", "close"};
static const char* const kFillers[7] = {"mug_sunset", "read",          "cairn",         "skip_stone",
                                        "cloud_watch", "stream_listen", "birds_on_water"};

static bool parsePlan(const App* a, int v[8]) {
  char buf[200];
  appLoopPlan(a, buf, sizeof buf);
  return sscanf(buf, "arrival=%d setup=%d firewood=%d fire=%d dinner=%d foodbag=%d bear=%d close=%d", &v[0], &v[1],
                &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8;
}

static int beatIndex(const char* name, const char* const* names, int n) {
  for (int i = 0; i < n; ++i)
    if (strcmp(name, names[i]) == 0) return i;
  return -1;
}

// Every variant of every beat, and every filler, found by trying seeds (cap
// 60): while the hiker is visible his bounds stay in the frame (and out of
// the reserved corner) for the whole loop; arrival to dinner and every filler
// end with him in frame; the food bag and the close end with him in the tent;
// no script runs past its beat.
// Critters, every tick: in frame vertically and out of the reserved corner
// while visible; never over the hiker's rect while he walks.
struct CritterWatch {
  int bad = 0, overlap = 0, walk_samples = 0;
  bool kind_seen[9] = {};
  void tick(const App* a, int seed, int t) {
    Rect hr;
    const bool hv = appHikerBounds(a, &hr);
    const bool walking = appHikerWalking(a);
    for (int i = 0; i < 8; ++i) {
      Rect r;
      int k = -1;
      if (!appCritterRect(a, i, &r, &k)) continue;
      if (k >= 0 && k < 9) kind_seen[k] = true;
      const bool in_frame = r.x1 > 0 && r.x0 < SS_WIDTH && r.y0 >= 0 && r.y1 <= SS_HEIGHT;
      const bool corner = r.x1 > LS_QUIET_X0_T && r.y1 > LS_QUIET_Y0_T;
      if ((!in_frame || corner) && bad < 3)
        printf("seed %d tick %d (%s): critter %d kind %d rect %d,%d,%d,%d\n", seed, t, appBeatName(a), i, k, r.x0, r.y0,
               r.x1, r.y1);
      if (!in_frame || corner) ++bad;
      if (hv && walking) {
        ++walk_samples;
        const bool over = r.x0 < hr.x1 && hr.x0 < r.x1 && r.y0 < hr.y1 && hr.y0 < r.y1;
        if (over && overlap < 3)
          printf("seed %d tick %d (%s): critter %d kind %d overlaps the walking hiker\n", seed, t, appBeatName(a), i, k);
        if (over) ++overlap;
      }
    }
  }
};

static void testSpineVariants() {
  static const int kVariants[8] = {3, 3, 3, 3, 3, 3, 3, 3};
  CritterWatch cw;
  bool bear_far = false;       // bear c fetching the firewood stick from the fire
  int dinner_runs = 0, dinner_with = 0, thefts = 0, closes = 0;
  bool seen[8][4] = {};   // dinner 3 (fish_dinner) comes only with fishing_good: testStreamVignettes
  bool fseen[7] = {};
  int seeds_run = 0;
  for (uint64_t seed = 1; seed <= 60; ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs(3.f)), "start");
    int v[8];
    CHECK(parsePlan(a.app, v), "seed %d: plan line does not parse", (int)seed);
    char plan[200];
    appLoopPlan(a.app, plan, sizeof plan);
    bool fresh = false;
    for (int b = 0; b < 8; ++b) fresh = fresh || !seen[b][v[b]];
    fresh = fresh || (v[6] == 2 && v[2] == 2 && !bear_far);   // also run the far-stick bear
    for (int f = 0; f < 7; ++f) fresh = fresh || (!fseen[f] && strstr(plan, kFillers[f]));
    if (!fresh) {
      a.stop();
      continue;
    }
    ++seeds_run;
    char prev[32] = "build";
    int t = 0, bad = 0;
    bool rolled = false;
    bool dinner_critters = false, raccoon = false;
    for (; !rolled; ++t) {
      rolled = appTick(a.app);
      Rect r;
      const bool vis = appHikerBounds(a.app, &r);   // unclipped: the in-frame checks must be able to fail
      if (!rolled) cw.tick(a.app, (int)seed, t);
      if (beatIs(a.app, "dinner") && appCritterCount(a.app) > 0) dinner_critters = true;
      if (beatIs(a.app, "close")) {
        for (int i = 0; i < 8; ++i) {
          int k = -1;
          Rect cr;
          if (appCritterRect(a.app, i, &cr, &k) && k == 4) raccoon = true;
        }
      }
      if (vis && bad < 3) {
        const bool in_frame = r.x1 > 0 && r.x0 < SS_WIDTH && r.y0 >= 0 && r.y1 <= SS_HEIGHT;
        const bool corner = r.x1 > LS_QUIET_X0_T && r.y1 > LS_QUIET_Y0_T;
        CHECK(in_frame && !corner, "seed %d tick %d (%s): hiker rect %d,%d,%d,%d", (int)seed, t,
              appBeatName(a.app), r.x0, r.y0, r.x1, r.y1);
        if (!in_frame || corner) ++bad;
      }
      if (rolled) break;   // the new loop's world: the close was checked by the last tick
      if (!beatIs(a.app, prev)) {   // a beat just ended
        const int b = beatIndex(prev, kBeats, 8);
        const int f = beatIndex(prev, kFillers, 7);
        if (b >= 0 && b <= 4) {
          CHECK(vis && r.x0 >= 0 && r.x1 <= SS_WIDTH, "seed %d: %s variant %d ends with the hiker out of frame",
                (int)seed, kBeats[b], v[b]);
        } else if (b == 5) {
          CHECK(!vis, "seed %d: foodbag variant %d ends with the hiker outside the tent", (int)seed, v[b]);
        }
        if (f >= 0) {
          CHECK(vis && r.x0 >= 0 && r.x1 <= SS_WIDTH, "seed %d: filler %s ends with the hiker out of frame", (int)seed,
                kFillers[f]);
          fseen[f] = true;
        }
        if (b >= 0) seen[b][v[b]] = true;
        snprintf(prev, sizeof prev, "%s", appBeatName(a.app));
      }
      if (beatIs(a.app, "close") && appBeatProgress(a.app) > 0.98f) {
        CHECK(!vis, "seed %d: close variant %d ends with the hiker outside the tent", (int)seed, v[7]);
        seen[7][v[7]] = true;
      }
    }
    ++dinner_runs;
    if (dinner_critters) ++dinner_with;
    ++closes;
    if (raccoon) ++thefts;
    CHECK(dinner_critters, "seed %d: dinner variant %d brought no critters", (int)seed, v[4]);
    CHECK(raccoon, "seed %d: close variant %d had no raccoon", (int)seed, v[7]);
    if (v[6] == 2 && v[2] == 2) bear_far = true;
    a.stop();
  }
  printf("critters: %d dinners with visitors of %d, %d thefts of %d closes; %d critter samples while he walked\n",
         dinner_with, dinner_runs, thefts, closes, cw.walk_samples);
  CHECK(cw.bad == 0, "%d critter samples out of frame or in the reserved corner", cw.bad);
  CHECK(cw.overlap == 0, "%d critter samples over the walking hiker", cw.overlap);
  CHECK(cw.kind_seen[3] && cw.kind_seen[0] && cw.kind_seen[4] && cw.kind_seen[7],
        "deer %d chipmunk %d raccoon %d bear %d never appeared", cw.kind_seen[3], cw.kind_seen[0], cw.kind_seen[4],
        cw.kind_seen[7]);
  CHECK(bear_far, "bear c never fetched the firewood stick in the seeds run");
  int missing = 0;
  for (int b = 0; b < 8; ++b)
    for (int k = 0; k < kVariants[b]; ++k)
      if (!seen[b][k]) {
        ++missing;
        printf("variant %s %d never ran in 60 seeds\n", kBeats[b], k);
      }
  for (int f = 0; f < 7; ++f)
    if (!fseen[f]) {
      ++missing;
      printf("filler %s never ran in 60 seeds\n", kFillers[f]);
    }
  printf("spine variants: all 24 variants and 7 fillers run in %d seeds\n", seeds_run);
  CHECK(missing == 0, "%d variants or fillers unseen", missing);
}

// No script overruns its beat, for 60 seeds (checked on the close's last
// tick, before the rollover resets the count).
// Memory survives the loop rollover: the shoe the raccoon took stays taken,
// and the loop count goes up.
static void testMemory() {
  Rig a;
  CHECK(a.start(1, makeInputs(3.f)), "start");
  CHECK(appMemoryLoopCount(a.app) == 1, "first loop counts %u", (unsigned)appMemoryLoopCount(a.app));
  CHECK(!appMemoryShoeTaken(a.app), "the shoe is taken before anything happened");
  const uint32_t len = appLoopTicks(a.app);
  for (uint32_t t = 0; t + 1 < len; ++t) appTick(a.app);
  const bool taken = appMemoryShoeTaken(a.app);
  CHECK(taken, "the raccoon did not take the shoe in loop 1");
  CHECK(appCritterCount(a.app) == 0, "critters left at the end of the loop");
  CHECK(appTick(a.app), "no rollover");
  CHECK(appMemoryShoeTaken(a.app) == taken, "the new loop forgot the raccoon has the shoe (only raccoon_pack brings it back)");
  CHECK(appMemoryLoopCount(a.app) == 2, "second loop counts %u", (unsigned)appMemoryLoopCount(a.app));
  printf("memory: shoe_taken %d after loop 1, loop_count %u in loop 2\n", (int)appMemoryShoeTaken(a.app),
         (unsigned)appMemoryLoopCount(a.app));
  a.stop();
}

static void testSpineOverruns() {
  int bad = 0;
  for (uint64_t seed = 1; seed <= 60; ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs(3.f)), "start");
    const uint32_t len = appLoopTicks(a.app);
    for (uint32_t t = 0; t + 1 < len; ++t) appTick(a.app);
    // One tick left: every beat but the close has ended, and the close counts
    // an unfinished script when its fade starts.
    const int n = appSpineOverruns(a.app);
    const int left = appCritterCount(a.app);
    CHECK(left == 0, "seed %d: %d critters still in the scene at the end of the loop", (int)seed, left);
    if (n) {
      char plan[200];
      appLoopPlan(a.app, plan, sizeof plan);
      printf("seed %d (%s): %d beat(s) overran\n", (int)seed, plan, n);
      ++bad;
    }
    a.stop();
  }
  CHECK(bad == 0, "%d seeds had overrunning beats", bad);
}

// Loop length policy: 480 to 660 s for 20 seeds, and the rollover lands on it.
static void testLoopLength() {
  int lo = 1 << 30, hi = 0;
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs((float)(seed % 6))), "start");
    const int len = (int)appLoopTicks(a.app);
    lo = len < lo ? len : lo;
    hi = len > hi ? len : hi;
    CHECK(len >= 480 * SS_TICK_HZ && len <= 660 * SS_TICK_HZ, "seed %d: loop is %.1f s", (int)seed,
          (double)len / SS_TICK_HZ);
    if (seed <= 3) {
      int at = -1;
      for (int t = 0; t < len + 5 && at < 0; ++t)
        if (appTick(a.app)) at = t + 1;
      CHECK(at == len, "seed %d: rolled over at tick %d, plan says %d", (int)seed, at, len);
    }
    a.stop();
  }
  printf("loop length over 20 seeds: %.0f..%.0f s\n", (double)lo / SS_TICK_HZ, (double)hi / SS_TICK_HZ);
}

// The fade: none through the close until its last 2.5 s, black (1) on its
// last tick, and gone again by tick 30 of the next loop.
static void testLoopFade() {
  for (uint64_t seed = 1; seed <= 3; ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs(3.f)), "start");
    CHECK(appFade(a.app) == 1.f, "seed %d: a loop does not open from black (fade %.3f)", (int)seed,
          (double)appFade(a.app));
    const uint32_t len = appLoopTicks(a.app);
    float before_last = -1.f, mid_close = -1.f;
    uint32_t t = 0;
    for (; t + 1 < len; ++t) {
      appTick(a.app);
      if (beatIs(a.app, "close") && appBeatProgress(a.app) < 0.5f) mid_close = appFade(a.app);
      if (t + 1 >= 30 && t + 1 < 40) CHECK(appFade(a.app) == 0.f, "seed %d: fade %.3f at tick %u", (int)seed,
                                        (double)appFade(a.app), t + 1);
    }
    before_last = appFade(a.app);   // the close's second-to-last tick
    const bool rolled = appTick(a.app);   // the close's last tick, then the new loop
    CHECK(rolled, "seed %d: no rollover at the plan's length", (int)seed);
    CHECK(mid_close == 0.f, "seed %d: fading already in the first half of the close (%.3f)", (int)seed,
          (double)mid_close);
    CHECK(before_last > 0.99f, "seed %d: fade %.4f one tick before the end", (int)seed, (double)before_last);
    CHECK(appFade(a.app) == 1.f, "seed %d: the new loop opens at fade %.3f", (int)seed, (double)appFade(a.app));
    for (int k = 0; k < 30; ++k) appTick(a.app);
    CHECK(appFade(a.app) == 0.f, "seed %d: fade %.3f at tick 30 of the next loop", (int)seed, (double)appFade(a.app));
    a.stop();
  }
}

// Strips and time per frame over a whole loop (render every tick, build-up
// excluded: it rebuilds the cache and has its own numbers), and in the pauses,
// where only the ambient layer and a breathing hiker move.
static int majorIndex(const char* name);
static void testSpineBudget() {
  static const uint64_t kSeeds[4] = {11, 7, 42, 1};   // seed 1: bear b
  for (uint64_t seed : kSeeds) {
    Rig a;
    CHECK(a.start(seed, makeInputs(3.f)), "start");
    int pv[8];
    CHECK(parsePlan(a.app, pv), "plan");
    const int bear_v = pv[6];
    long sum = 0, psum = 0, bsum = 0, msum = 0;
    int frames = 0, pframes = 0, worst = 0, bframes = 0, bworst = 0, mframes = 0, mworst = 0;
    double us = 0.0;
    for (int t = 0;; ++t) {
      const auto t0 = std::chrono::steady_clock::now();
      const bool rolled = appTick(a.app);
      const int n = appRender(a.app, a.surf);
      const auto t1 = std::chrono::steady_clock::now();
      if (rolled) break;
      if (t < 330) continue;
      us += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1000.0;
      sum += n;
      ++frames;
      if (n > worst) worst = n;
      if (beatIs(a.app, "pause")) {
        psum += n;
        ++pframes;
      }
      if (majorIndex(appBeatName(a.app)) >= 0) {   // the surprise majors (stage 6)
        msum += n;
        ++mframes;
        if (n > mworst) mworst = n;
      }
      if (beatIs(a.app, "bear")) {
        bsum += n;
        ++bframes;
        if (n > bworst) bworst = n;
      }
    }
    const float bavg = bframes ? (float)bsum / (float)bframes : 0.f;
    printf("seed %d: bear beat (variant %d) %d frames: avg %.2f strips, max %d\n", (int)seed, bear_v, bframes,
           (double)bavg, bworst);
    CHECK(bavg <= 10.f, "seed %d: bear beat averages %.2f strips per frame (budget 10)", (int)seed, (double)bavg);
    const float mavg = mframes ? (float)msum / (float)mframes : 0.f;
    char plan[400];
    appLoopPlan(a.app, plan, sizeof plan);
    printf("seed %d: majors (%s) %d frames: avg %.2f strips, max %d\n", (int)seed, strstr(plan, "majors=") ? strstr(plan, "majors=") + 7 : "?", mframes, (double)mavg, mworst);
    CHECK(mavg <= 10.f, "seed %d: the majors average %.2f strips per frame (budget 10)", (int)seed, (double)mavg);
    const float avg = frames ? (float)sum / (float)frames : 0.f;
    const float pavg = pframes ? (float)psum / (float)pframes : 0.f;
    printf("seed %d: whole loop %d frames: avg %.2f strips, max %d, %.3f ms/frame; pauses avg %.2f strips\n",
           (int)seed, frames, (double)avg, worst, frames ? us / 1000.0 / frames : 0.0, (double)pavg);
    CHECK(pavg <= 9.f, "seed %d: pauses average %.2f strips per frame (budget 9)", (int)seed, (double)pavg);
    CHECK(avg <= 9.f, "seed %d: loop average %.2f strips per frame (budget 9)", (int)seed, (double)avg);
    a.stop();
  }
}

// hikerHandsMid (props.cpp, from hikerHandPos in the rig): the point just above
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
      Item it{Vec2{40.f + 105.f * (float)id + 45.f * (float)f, 300.f}, true, false, (int8_t)(f ? -1 : 1),
              id == ITEM_WOOD && f ? 1.65f : 1.f, 300.f, false, 0, 0.f, 0.f, 1.f, false};
      if ((id == ITEM_STICK || id == ITEM_FORK) && f) {   // dragged stick; fork standing in a hand
        it.down = false;
        it.held = true;
        it.pos = Vec2{it.pos.x + (id == ITEM_STICK ? 30.f : 0.f), 270.f};
      }
      check(itemName((ItemId)id), itemBounds((ItemId)id, it), [&](Surface& d) { itemDraw((ItemId)id, it, d, all, pal); });
    }
  }
  // Row 3b: a thrown bag with its rope, steam puffs, a skipped stone's rings
  // (the stone in flight), a leaf at three turns.
  {
    FoodBag b;
    bagInit(b, Vec2{560.f, 330.f});
    b.state = BAG_FLYING;
    b.pos = Vec2{600.f, 350.f};
    b.rope = Vec2{620.f, 395.f};
    b.swing = 0.6f;
    check("bag flying", bagBounds(b), [&](Surface& d) { bagDraw(b, d, all, pal); });
    for (int k = 0; k < 3; ++k) {
      const Steam st{Vec2{660.f + 30.f * (float)k, 390.f}, 0.3f + 0.35f * (float)k, 5};
      check("steam", steamBounds(st), [&](Surface& d) { steamDraw(st, d, all, pal); });
    }
    Skip sk;
    sk.n = 4;
    sk.p[0] = Vec2{300.f, 360.f};
    for (int i = 1; i <= SKIP_MAX; ++i) sk.p[i] = Vec2{300.f + 34.f * (float)i, 375.f + 2.f * (float)i};
    sk.t = skipHitTime(sk, 3) + 0.1f;
    check("skip", skipBounds(sk), [&](Surface& d) { skipDraw(sk, d, all, pal); });
    for (int k = 0; k < 3; ++k) {
      const Leaf l{true, Vec2{480.f + 20.f * (float)k, 380.f}, 0.15f * (float)k, 380.f};
      check("leaf", leafBounds(l), [&](Surface& d) { leafDraw(l, d, all, pal); });
    }
  }
  // Row 3c: a stick held by a critter at three angles (held by the end, in the
  // mouth, squeezed mid turn), a falling and a resting pine cone, a dust puff.
  for (int k = 0; k < 3; ++k) {
    Item it{Vec2{560.f + 40.f * (float)k, 470.f}, false, true, (int8_t)(k == 1 ? -1 : 1), 1.f, 470.f, false, 1,
            -0.2f + 0.15f * (float)k, k == 1 ? kItemMouthGrip : 0.f, k == 2 ? 0.4f : 1.f, false};
    check("stick held by a critter", itemBounds(ITEM_STICK, it), [&](Surface& d) { itemDraw(ITEM_STICK, it, d, all, pal); });
  }
  for (int k = 0; k < 3; ++k) {
    Bit b{};
    b.kind = k < 2 ? BIT_CONE : BIT_DUST;
    b.pos = Vec2{720.f + 25.f * (float)k, 460.f};
    b.resting = k == 1;
    b.spin = 0.1f;
    b.t = 0.4f;
    b.land_y = 460.f;
    check("bit", bitBounds(b), [&](Surface& d) { bitDraw(b, d, all, pal); });
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

// Minor effects (scene/minors.h), over whole loops: 12 to 40 a loop (seeds
// 1..3 at mood 3); none starts while dinner, bear or close runs, or any beat
// that is not a calm one (majors); every minor in the pool fires in 12 seeds
// (moods day, night, dusk in turn: the owl and moth need the dark); each
// minor's critters are gone when it ends; same-seed frames identical at 3
// ticks of a dusk loop full of minors.
static const char* const kMinors[11] = {"chipmunk_cross", "squirrel_nut", "frog_splash", "bird_chip",
                                        "deer_edge",      "owl_watch",    "mouse_tent",  "leaf_fall",
                                        "moth_lantern",   "bee_visit",    "cloud_animal"};
static const char* const kCalmBeats[12] = {"pause",      "arrival",     "setup", "firewood", "fire",
                                           "foodbag",    "mug_sunset",  "read",  "cairn",    "skip_stone",
                                           "cloud_watch", "stream_listen"};

static void testMinors() {
  int fired_by[11] = {};
  int bad_beat = 0, forced = 0, total = 0, unknown = 0;
  for (uint64_t seed = 1; seed <= 12; ++seed) {
    const float mood = seed % 3 == 1 ? 3.f : (seed % 3 == 2 ? 0.3f : 5.f);
    Rig a;
    CHECK(a.start(seed, makeInputs(mood)), "start");
    int count = 0, ticks = 0, seed_forced = 0;
    char prev[32] = "";
    for (;; ++ticks) {
      if (appTick(a.app)) break;
      const int c = appMinorCount(a.app);
      const char* name = appMinorName(a.app);
      if (c != count) {   // a minor started this tick
        count = c;
        const int k = beatIndex(name, kMinors, 11);
        if (k >= 0) ++fired_by[k];
        else ++unknown;
        if (beatIndex(appBeatName(a.app), kCalmBeats, 12) < 0) {
          if (bad_beat < 3) printf("seed %d tick %d: minor %s started in beat %s\n", (int)seed, ticks, name, appBeatName(a.app));
          ++bad_beat;
        }
      }
      // A minor that had to be forced to end (past its age limit, or a critter
      // released in sight because its exit never got it out).
      if (appMinorForced(a.app) > seed_forced) {
        if (forced < 3) printf("seed %d tick %d: %s was forced to end\n", (int)seed, ticks, prev[0] ? prev : name);
        forced += appMinorForced(a.app) - seed_forced;
        seed_forced = appMinorForced(a.app);
      }
      snprintf(prev, sizeof prev, "%s", name);
    }
    total += count;
    if (seed <= 3) {
      printf("seed %d (mood %.1f): %d minors in a %d s loop\n", (int)seed, (double)mood, count, ticks / SS_TICK_HZ);
      CHECK(count >= 12 && count <= 40, "seed %d: %d minors in a loop (want 12..40)", (int)seed, count);
    }
    a.stop();
  }
  int missing = 0;
  for (int k = 0; k < 11; ++k) {
    if (fired_by[k] == 0) {
      ++missing;
      printf("minor %s never fired in 12 seeds\n", kMinors[k]);
    }
  }
  printf("minors: %d in 12 loops (%.1f a loop); each fired:", total, (double)total / 12.0);
  for (int k = 0; k < 11; ++k) printf(" %s %d", kMinors[k], fired_by[k]);
  printf("\n");
  CHECK(missing == 0, "%d minors never fired", missing);
  CHECK(unknown == 0, "%d minors started with an unknown name", unknown);
  CHECK(bad_beat == 0, "%d minors started in a beat that owns the critters", bad_beat);
  CHECK(forced == 0, "%d minors were forced to end", forced);

  // Determinism through minors at dusk (owl, moth).
  Rig x, y;
  CHECK(x.start(3, makeInputs(5.f)) && y.start(3, makeInputs(5.f)), "start");
  static const int kAt[3] = {2000, 7000, 11900};
  int t = 0;
  for (int k = 0; k < 3; ++k) {
    for (; t < kAt[k]; ++t) {
      appTick(x.app);
      appTick(y.app);
      appRender(x.app, x.surf);
    }
    appInvalidate(y.app);
    appRender(y.app, y.surf);
    CHECK(sameFrame(x, y), "minors: same seed differs at tick %d (%s, minor %s), pixel %ld", t, appBeatName(x.app),
          appMinorName(x.app), firstDiff(x, y));
  }
  x.stop();
  y.stop();
}

// ---- stage 6: surprise majors, variety, memory ---------------------------------------

#define N_MAJORS 16
static const char* const kMajorNames[N_MAJORS] = {
    "squirrel_splash", "deer_stick", "pinecone",    "map_chase",     "fish_leap",     "raccoon_pack",
    "ants_cookie",     "shadow_theater", "ranger_ridge", "echo",     "firefly_jar",   "stargaze",
    "fishing_small",   "fishing_log",    "fishing_good", "bathe"};

// The part of a plan line that the variety rule compares: the spine variants
// and the majors (the fillers and pauses are left out).
static void planTuple(const App* a, char* out, size_t n) {
  char buf[400];
  appLoopPlan(a, buf, sizeof buf);
  const char* f = strstr(buf, " fillers=");
  const char* m = strstr(buf, " majors=");
  const int head = f ? (int)(f - buf) : (int)strlen(buf);
  snprintf(out, n, "%.*s%s", head, buf, m ? m : "");
}

static int majorIndex(const char* name) { return beatIndex(name, kMajorNames, N_MAJORS); }

// The loop's majors as a bit set (bit = index in kMajorNames).
static unsigned majorSet(const App* a) {
  char buf[400];
  appLoopPlan(a, buf, sizeof buf);
  const char* m = strstr(buf, "majors=");
  unsigned set = 0;
  for (int k = 0; m && k < N_MAJORS; ++k) {
    const char* q = strstr(m, kMajorNames[k]);
    if (q && (q[strlen(kMajorNames[k])] == ',' || q[strlen(kMajorNames[k])] == 0)) set |= 1u << k;
  }
  return set;
}
static int memField(const App* a, const char* key);
static bool ownsCritters(const char* name) {
  return majorIndex(name) >= 0 || !strcmp(name, "dinner") || !strcmp(name, "bear") || !strcmp(name, "close");
}

// 30 loops from one seed at night (every major is eligible): the first 10
// differ in (spine variants + majors); every major fires at least once; each
// major sits between two still beats of 4 to 8 s (so majors never touch each
// other or dinner, bear or close); World::busy is true exactly while dinner,
// bear, close or a major runs; no beat overruns and no critter is left at a
// loop's end.
static void testVariety() {
  Rig a;
  CHECK(a.start(1, makeInputs(0.f, true)), "start");
  char tuples[10][400];
  int fired[N_MAJORS] = {};
  int loops = 0, bad_busy = 0, bad_still = 0, bad_end = 0, majors_run = 0, repeats = 0;
  unsigned last_set = majorSet(a.app);
  char prev[32] = "build", before[32] = "";
  int still_ticks = 0;
  bool after_major = false;
  planTuple(a.app, tuples[0], sizeof tuples[0]);
  printf("variety loop 0: %s\n", tuples[0]);
  uint32_t len = appLoopTicks(a.app), t = 0;
  while (loops < 30) {
    if (t + 1 == len) {   // the loop's last tick: every beat but the close has ended
      if (appSpineOverruns(a.app) != 0 || appCritterCount(a.app) != 0) {
        ++bad_end;
        char plan[400];
        appLoopPlan(a.app, plan, sizeof plan);
        printf("loop %d: %d overruns, %d critters left (%s)\n", loops, appSpineOverruns(a.app),
               appCritterCount(a.app), plan);
      }
    }
    const bool rolled = appTick(a.app);
    ++t;
    if (rolled) {
      ++loops;
      t = 0;
      len = appLoopTicks(a.app);
      // No major two loops running; the one exception is the raccoon's sequel
      // (its tent raid the night after it brought the shoe back: shoe=0 now).
      const unsigned set = majorSet(a.app);
      unsigned again = set & last_set;
      if (memField(a.app, "shoe=") == 0) again &= ~(1u << 5);   // raccoon_pack
      if (again) {
        if (repeats < 3) printf("loop %d repeats a major of loop %d (sets %x, %x)\n", loops, loops - 1, last_set, set);
        ++repeats;
      }
      last_set = set;
      if (loops < 10) {
        planTuple(a.app, tuples[loops], sizeof tuples[loops]);
        printf("variety loop %d: %s\n", loops, tuples[loops]);
      }
      snprintf(prev, sizeof prev, "%s", appBeatName(a.app));
      after_major = false;
      continue;
    }
    const char* beat = appBeatName(a.app);
    if (appBusy(a.app) != ownsCritters(beat)) {
      if (bad_busy < 3) printf("loop %d tick %u: busy %d in beat %s\n", loops, (unsigned)t, (int)appBusy(a.app), beat);
      ++bad_busy;
    }
    if (strcmp(beat, prev) != 0) {   // a beat change
      const int was = majorIndex(prev), now = majorIndex(beat);
      const int ended = !strcmp(prev, "still") ? still_ticks : 0;   // the still beat that just ended
      if (now >= 0) {
        ++fired[now];
        ++majors_run;
        if (strcmp(prev, "still") || ended < 4 * SS_TICK_HZ || ended > 8 * SS_TICK_HZ) {
          if (bad_still < 3) printf("loop %d: %s follows %s (%d ticks)\n", loops, beat, prev, ended);
          ++bad_still;
        }
      }
      if (was >= 0 && strcmp(beat, "still")) {
        if (bad_still < 3) printf("loop %d: %s is followed by %s\n", loops, prev, beat);
        ++bad_still;
      }
      if (after_major && strcmp(prev, "still") == 0 && (ended < 4 * SS_TICK_HZ || ended > 8 * SS_TICK_HZ)) {
        if (bad_still < 3) printf("loop %d: the still after %s is %d ticks\n", loops, before, ended);
        ++bad_still;
      }
      if (strcmp(prev, "still") == 0) after_major = false;
      if (was >= 0) {
        after_major = true;
        snprintf(before, sizeof before, "%s", prev);
      }
      still_ticks = 0;
      snprintf(prev, sizeof prev, "%s", beat);
    }
    if (!strcmp(beat, "still")) ++still_ticks;
  }
  int same = 0;
  for (int i = 0; i < 10; ++i)
    for (int j = i + 1; j < 10; ++j)
      if (!strcmp(tuples[i], tuples[j])) {
        ++same;
        printf("loops %d and %d have the same variants and majors\n", i, j);
      }
  // The majors alone, as sets: no set repeats within the ten loops either.
  int same_set = 0;
  unsigned sets[10] = {};
  for (int i = 0; i < 10; ++i) {
    const char* m = strstr(tuples[i], "majors=");
    for (int k = 0; m && k < N_MAJORS; ++k) {
      const char* q = strstr(m, kMajorNames[k]);
      if (q && (q[strlen(kMajorNames[k])] == ',' || q[strlen(kMajorNames[k])] == 0)) sets[i] |= 1u << k;
    }
    for (int j = 0; j < i; ++j)
      if (sets[i] == sets[j]) {
        ++same_set;
        printf("loops %d and %d play the same majors\n", j, i);
      }
  }
  CHECK(same_set == 0, "%d pairs of the first 10 loops play the same set of majors", same_set);
  int never = 0;
  printf("majors over 30 loops (%d run):", majors_run);
  for (int i = 0; i < N_MAJORS; ++i) {
    printf(" %s %d", kMajorNames[i], fired[i]);
    if (!fired[i]) ++never;
  }
  printf("\n");
  CHECK(same == 0, "%d pairs of the first 10 loops are alike", same);
  CHECK(never == 0, "%d majors never fired in 30 loops", never);
  CHECK(bad_still == 0, "%d majors without a 4 to 8 s still beat either side", bad_still);
  CHECK(bad_busy == 0, "%d ticks where busy disagreed with the beat", bad_busy);
  CHECK(bad_end == 0, "%d loops ended with an overrun or a critter left", bad_end);
  CHECK(repeats == 0, "%d loops repeat a major of the loop before", repeats);
  a.stop();

  // By day, the night majors stay out (gating), and every loop has 2 to 4 majors.
  Rig d;
  CHECK(d.start(5, makeInputs(3.f)), "start");
  int night = 0, few = 0;
  for (int l = 0; l < 12; ++l) {
    char plan[400];
    appLoopPlan(d.app, plan, sizeof plan);
    if (strstr(plan, "firefly_jar") || strstr(plan, "stargaze")) ++night;
    const char* m = strstr(plan, "majors=");
    int n = m && strncmp(m, "majors=none", 11) ? 1 : 0;
    for (const char* c = m; c && *c; ++c) n += *c == ',';
    if (n < 2 || n > 4) ++few;
    const uint32_t L = appLoopTicks(d.app);
    for (uint32_t k = 0; k < L; ++k) appTick(d.app);
  }
  CHECK(night == 0, "%d day loops planned a night major", night);
  CHECK(few == 0, "%d loops with fewer than 2 or more than 4 majors", few);
  d.stop();
}

// Memory across loops: the cairn left at the end of one loop stands one stone
// shorter in the next once the build-up is over; the memory line parses.
static int memField(const App* a, const char* key) {
  char buf[400];
  appMemory(a, buf, sizeof buf);
  const char* p = strstr(buf, key);
  return p ? atoi(p + strlen(key)) : -1;
}

static void testMemoryCarry() {
  Rig a;
  CHECK(a.start(3, makeInputs(3.f)), "start");
  int checked = 0;
  for (int loop = 0; loop < 10 && checked < 2; ++loop) {
    const uint32_t L = appLoopTicks(a.app);
    while (appTickCount(a.app) + 1 < L) appTick(a.app);   // the loop's last tick next
    const int left = memField(a.app, "cairn=");
    CHECK(appTick(a.app), "no rollover at the plan's length");
    while (!beatIs(a.app, "arrival")) appTick(a.app);
    appTick(a.app);
    const int now = memField(a.app, "cairn=");
    if (left > 0) ++checked;
    CHECK(now == (left > 0 ? left - 1 : 0), "loop %d left a cairn of %d; the next starts with %d", loop, left, now);
    if (loop == 0) {
      char mem[400];
      appMemory(a.app, mem, sizeof mem);
      printf("memory line: %s\n", mem);
    }
  }
  printf("memory: the cairn carried over (one stone shorter) %d times\n", checked);
  CHECK(checked > 0, "no loop left a cairn in 10 loops");
  a.stop();

  // The mouse hole and the shoe, 12 loops from seed 1: a loop where the
  // mouse_tent minor ran twice leaves a hole, which the next setup patches;
  // the raccoon brings back the shoe the night thief took (shoe 1 -> 0 within
  // a loop).
  Rig b;
  CHECK(b.start(1, makeInputs(3.f)), "start");
  int holes = 0, patched = 0, bad_hole = 0, returned = 0;
  for (int loop = 0; loop < 12; ++loop) {
    const int hole0 = memField(b.app, "hole="), shoe0 = memField(b.app, "shoe=");
    int mice = 0, count = 0, after_setup = -1;
    bool in_setup = false, came_back = false;
    while (!appTick(b.app)) {
      if (appMinorCount(b.app) != count) {
        count = appMinorCount(b.app);
        if (!strcmp(appMinorName(b.app), "mouse_tent")) ++mice;
      }
      const bool setup = beatIs(b.app, "setup");
      if (in_setup && !setup && after_setup < 0) after_setup = memField(b.app, "hole=");
      in_setup = setup;
      if (shoe0 == 1 && !came_back && memField(b.app, "shoe=") == 0) came_back = true;
    }
    if (came_back) ++returned;
    const int hole1 = memField(b.app, "hole=");   // the new loop has started: what the last one left
    if (hole0 == 1) {
      ++patched;
      if (after_setup != 0) {
        printf("loop %d: the mouse hole was still there after setup\n", loop);
        ++bad_hole;
      }
    }
    if (mice >= 2) {
      ++holes;
      if (hole1 != 1) {
        printf("loop %d: the mouse ran %d times but left no hole\n", loop, mice);
        ++bad_hole;
      }
    }
  }
  printf("memory: %d loops left a mouse hole, %d patched in setup, the shoe came back %d times\n", holes, patched,
         returned);
  CHECK(holes > 0 && patched > 0, "no mouse hole made (%d) or patched (%d) in 12 loops", holes, patched);
  CHECK(bad_hole == 0, "%d mouse hole mistakes", bad_hole);
  CHECK(returned > 0, "the raccoon never brought the shoe back in 12 loops");
  b.stop();
}

// The director's hiker rule (review fix 5): he walks straight at a critter
// that is neither shy nor brave; it startles and runs before his bounds and
// its bounds meet. A brave one stands its ground. Driven on a bare World (no
// timeline), so the rule is checked on its own.
static void testHikerScatter() {
  World* w = (World*)calloc(1, sizeof(World));
  CHECK(w != nullptr, "alloc");
  if (!w) return;
  static const CritterKind kKinds[3] = {CR_CHIPMUNK, CR_DEER, CR_RACCOON};
  for (int brave = 0; brave < 2; ++brave) {
    for (int k = 0; k < 3; ++k) {
      Rng rng;
      rng.seed(11 + (uint64_t)k);
      worldBeginLoop(*w, rng);
      const float y = 360.f;
      Hiker& h = w->hiker;
      hikerInit(h, Vec2{100.f, y}, 1);
      w->fx.on = true;
      const int s = directorSpawn(*w, kKinds[k], Vec2{470.f, y}, -1);
      CHECK(s >= 0, "spawn");
      if (s < 0) continue;
      w->crit[s].tag = CT_MINOR;
      w->crit[s].flags = brave ? CF_BRAVE : 0;
      hikerWalkTo(h, 700.f, 40.f);
      int scattered_at = -1, overlap_at = -1, gap = 0;
      for (int t = 0; t < 15 * SS_TICK_HZ && h.walking; ++t) {
        hikerTick(h, SS_TICK_DT);
        const Rect hr = hikerBounds(h);
        h.visible = true;
        w->hiker_rect = w->hiker_full = hr;
        directorStep(*w);
        if (!w->crit[s].in_use) break;
        const Rect r = critterBounds(w->critters[s]);
        if (scattered_at < 0 && w->crit[s].mode != CM_FREE) {
          scattered_at = t;
          gap = r.x0 - hr.x1;
        }
        if (overlap_at < 0 && r.x0 < hr.x1 && hr.x0 < r.x1 && r.y0 < hr.y1 && hr.y0 < r.y1) overlap_at = t;
      }
      if (brave) {
        CHECK(scattered_at < 0, "a brave %s scattered at tick %d", critterName(kKinds[k]), scattered_at);
      } else {
        printf("hiker scatter: %s startles %d px ahead of him (tick %d)\n", critterName(kKinds[k]), gap, scattered_at);
        CHECK(scattered_at >= 0, "the %s never scattered from the walking hiker", critterName(kKinds[k]));
        CHECK(overlap_at < 0 || (scattered_at >= 0 && scattered_at < overlap_at),
              "the %s was overlapped (tick %d) before it scattered (tick %d)", critterName(kKinds[k]), overlap_at,
              scattered_at);
        CHECK(overlap_at < 0, "the %s fled but he still walked over it (tick %d)", critterName(kKinds[k]), overlap_at);
      }
    }
  }
  free(w);
}

// Review fix 8: when a major begins, the minors have cleared the stage: none
// of their critters is in the scene and no leaf of theirs is lying, falling
// or blowing about. 20 seeds at mood 4 and 20 at night, a loop each.
static void testMajorsStartClear() {
  int majors = 0, critters = 0, leaves = 0;
  for (int night = 0; night < 2; ++night) {
    for (uint64_t seed = 1; seed <= 20; ++seed) {
      Rig a;
      CHECK(a.start(seed, night ? makeInputs(0.f, true) : makeInputs(4.f)), "start");
      char prev[32] = "";
      while (!appTick(a.app)) {
        const char* b = appBeatName(a.app);
        if (!strcmp(b, prev)) continue;
        snprintf(prev, sizeof prev, "%s", b);
        if (majorIndex(b) < 0) continue;
        ++majors;
        if (appMinorCritters(a.app) != 0) {
          if (critters < 3)
            printf("%s seed %d tick %u: %s begins with %d minor critters (%s)\n", night ? "night" : "mood 4", (int)seed,
                   (unsigned)appTickCount(a.app), b, appMinorCritters(a.app), appMinorName(a.app));
          ++critters;
        }
        if (appLeaves(a.app) != 0) {
          if (leaves < 3)
            printf("%s seed %d tick %u: %s begins with %d leaves about\n", night ? "night" : "mood 4", (int)seed,
                   (unsigned)appTickCount(a.app), b, appLeaves(a.app));
          ++leaves;
        }
      }
      a.stop();
    }
  }
  printf("majors start clear: %d majors over 40 loops, %d with minor critters, %d with leaves\n", majors, critters,
         leaves);
  CHECK(majors > 60, "only %d majors in 40 loops", majors);
  CHECK(critters == 0, "%d majors began with a minor's critter in the scene", critters);
  CHECK(leaves == 0, "%d majors began with a leaf about", leaves);
}


// ---- the stream vignettes (Mike's idea) -------------------------------------------------------
// fishing_small, fishing_log, fishing_good (and dinner 3, fish_dinner, which
// it brings), bathe and the birds_on_water filler: seeds tried until each has
// run (cap 80), rendering every tick. While visible the hiker stays in frame
// and out of the corner; at the end of each of these beats he is in frame,
// out of the water and has his hat on; the rod, hat and fish items and the
// log, the dead fish and the thrown rod never touch the quiet corner; no
// overruns; strips per frame over the new majors (<= 10); determinism at 3
// ticks inside one of them; and rod_lost across loops (a stick next time).
static const char* const kStream[6] = {"fishing_small", "fishing_log", "fishing_good", "bathe", "birds_on_water",
                                       "fish_dinner"};
static bool inCorner(const Rect& r) { return !r.empty() && r.x1 > LS_QUIET_X0_T && r.y1 > LS_QUIET_Y0_T; }

static void testStreamVignettes() {
  bool seen[6] = {};
  long strips[5] = {};
  int frames[5] = {}, worst[5] = {};
  int bad_frame = 0, bad_end = 0, bad_corner = 0, overruns = 0, seeds_run = 0, ends = 0;
  uint64_t det_seed = 0;
  int det_at = -1;
  for (uint64_t seed = 1; seed <= 80; ++seed) {
    bool all = true;
    for (bool b : seen) all = all && b;
    if (all) break;
    const Inputs in = makeInputs(seed % 3 == 0 ? 0.f : 4.f, seed % 3 == 0);
    Rig a;
    CHECK(a.start(seed, in), "start");
    char plan[400];
    appLoopPlan(a.app, plan, sizeof plan);
    int v[8];
    CHECK(parsePlan(a.app, v), "plan");
    const bool fish_dinner = v[4] == 3;
    CHECK(fish_dinner == (strstr(plan, "fishing_good") != nullptr), "seed %d: dinner=%d with plan %s", (int)seed, v[4],
          plan);
    bool fresh = fish_dinner && !seen[5];
    for (int k = 0; k < 5; ++k) fresh = fresh || (!seen[k] && strstr(plan, kStream[k]));
    if (!fresh) {
      a.stop();
      continue;
    }
    ++seeds_run;
    char prev[32] = "build";
    const uint32_t len = appLoopTicks(a.app);
    for (uint32_t t = 1; t <= len; ++t) {
      if (t == len) {   // the loop's last tick: every beat but the close has ended
        if (appSpineOverruns(a.app)) {
          ++overruns;
          printf("seed %d (%s): %d overruns\n", (int)seed, plan, appSpineOverruns(a.app));
        }
      }
      if (appTick(a.app)) break;
      const int n = appRender(a.app, a.surf);
      const World* w = appWorld(a.app);
      const char* beat = appBeatName(a.app);
      int k = beatIndex(beat, kStream, 5);
      if (k < 0 && fish_dinner && !strcmp(beat, "dinner")) k = 5;
      if (k >= 0 && k < 5) {
        strips[k] += n;
        ++frames[k];
        if (n > worst[k]) worst[k] = n;
        if (det_seed == 0 && k == 1) {   // fishing_log: the determinism check runs on it
          det_seed = seed;
          det_at = (int)t;
        }
      }
      Rect hr;
      if (appHikerBounds(a.app, &hr)) {
        const bool ok = hr.x1 > 0 && hr.x0 < SS_WIDTH && hr.y0 >= 0 && hr.y1 <= SS_HEIGHT && !inCorner(hr);
        if (!ok && bad_frame < 3)
          printf("seed %d tick %u (%s): hiker rect %d,%d,%d,%d\n", (int)seed, (unsigned)t, beat, hr.x0, hr.y0, hr.x1,
                 hr.y1);
        if (!ok) ++bad_frame;
      }
      for (int i = ITEM_ROD; i <= ITEM_FISH; ++i) {
        const Item& it = w->items[i];
        if (!it.down && !(it.held && w->hiker.visible)) continue;
        const Rect r = itemBounds((ItemId)i, it);
        if (inCorner(r) || (it.down && (r.y1 > SS_HEIGHT + 2 || r.x0 < -2 || r.x1 > SS_WIDTH + 2))) {
          if (bad_corner < 3)
            printf("seed %d tick %u: %s at %d,%d,%d,%d\n", (int)seed, (unsigned)t, itemName((ItemId)i), r.x0, r.y0,
                   r.x1, r.y1);
          ++bad_corner;
        }
      }
      for (const Glyph& g : w->major.g) {
        if (g.kind != GL_SHAPE || (g.shape != SH_LOG && g.shape != SH_DEADFISH && g.shape != SH_ROD)) continue;
        if (inCorner(glyphBounds(g))) {
          if (bad_corner < 3) printf("seed %d tick %u: glyph %d in the corner\n", (int)seed, (unsigned)t, g.shape);
          ++bad_corner;
        }
      }
      if (strcmp(beat, prev) != 0) {   // a beat ended: was it one of ours?
        int was = beatIndex(prev, kStream, 5);
        if (was < 0 && fish_dinner && !strcmp(prev, "dinner")) was = 5;
        if (was >= 0) {
          seen[was] = true;
          ++ends;
          const Hiker& h = w->hiker;
          float cy, half;
          const bool wet = w->fx.wet_y > 0.f || w->fx.wade ||
                           (landscapeStreamSpan(w->land, h.feet.x, cy, half) && fabsf(h.feet.y - cy) < half);
          const bool ok = h.visible && hr.x0 >= 0 && hr.x1 <= SS_WIDTH && !wet && h.hat_on;
          if (!ok) {
            ++bad_end;
            printf("seed %d: %s ends with the hiker %s (visible %d, wet %d, hat %d)\n", (int)seed, kStream[was],
                   "out of place", (int)h.visible, (int)wet, (int)h.hat_on);
          }
        }
        snprintf(prev, sizeof prev, "%s", beat);
      }
    }
    a.stop();
  }
  int missing = 0;
  for (int k = 0; k < 6; ++k)
    if (!seen[k]) {
      ++missing;
      printf("stream: %s never ran in 80 seeds\n", kStream[k]);
    }
  printf("stream vignettes: all 6 ran in %d seeds, %d beat ends checked\n", seeds_run, ends);
  for (int k = 0; k < 5; ++k) {
    const float avg = frames[k] ? (float)strips[k] / (float)frames[k] : 0.f;
    printf("stream: %s %d frames: avg %.2f strips, max %d\n", kStream[k], frames[k], (double)avg, worst[k]);
    CHECK(avg <= 10.f, "%s averages %.2f strips per frame (budget 10)", kStream[k], (double)avg);
  }
  CHECK(missing == 0, "%d stream vignettes never ran", missing);
  CHECK(bad_frame == 0, "%d ticks with the hiker out of frame or in the corner", bad_frame);
  CHECK(bad_end == 0, "%d stream beats ended with him out of frame, wet or hatless", bad_end);
  CHECK(bad_corner == 0, "%d ticks with a rod, hat, fish, log or dead fish in the quiet corner", bad_corner);
  CHECK(overruns == 0, "%d stream loops overran", overruns);

  // Determinism at 3 ticks of fishing_log: rendered every tick vs full rebuilds.
  CHECK(det_seed != 0, "no fishing_log to check determinism on");
  if (det_seed) {
    const Inputs in = makeInputs(det_seed % 3 == 0 ? 0.f : 4.f, det_seed % 3 == 0);
    Rig x, y;
    CHECK(x.start(det_seed, in) && y.start(det_seed, in), "start");
    const int at[3] = {det_at + 15 * SS_TICK_HZ, det_at + 27 * SS_TICK_HZ, det_at + 50 * SS_TICK_HZ};
    int t = 0;
    for (int k = 0; k < 3; ++k) {
      for (; t < at[k]; ++t) {
        appTick(x.app);
        appTick(y.app);
        appRender(x.app, x.surf);
      }
      appInvalidate(y.app);
      appRender(y.app, y.surf);
      CHECK(sameFrame(x, y), "stream: same seed differs at tick %d (%s), pixel %ld", t, appBeatName(x.app),
            firstDiff(x, y));
    }
    printf("stream determinism: seed %d, 3 ticks in %s\n", (int)det_seed, "fishing_log");
    x.stop();
    y.stop();
  }

  // rod_lost: a loop whose fishing threw (or lost) the rod leaves rod=1; the
  // next fishing major fishes with a stick and clears it.
  Rig r;
  CHECK(r.start(1, makeInputs(4.f)), "start");
  int lost = 0, sticks = 0, bad_stick = 0;
  int rod_at_begin = -1;
  char prev[32] = "";
  for (int loop = 0; loop < 25; ++loop) {
    while (!appTick(r.app)) {
      const char* beat = appBeatName(r.app);
      if (strcmp(beat, prev) == 0) continue;
      const World* w = appWorld(r.app);
      if (!strncmp(beat, "fishing_", 8)) {   // just began: the flag was read and cleared
        if (w->major.stick != (rod_at_begin == 1)) ++bad_stick;
        if (w->major.stick) ++sticks;
        if (w->memory.rod_lost) ++bad_stick;
      }
      if (!strcmp(beat, "still")) rod_at_begin = memField(r.app, "rod=");
      if ((!strcmp(prev, "fishing_small") || !strcmp(prev, "fishing_log")) && !w->memory.rod_lost) ++bad_stick;
      if (!strcmp(prev, "fishing_small") || !strcmp(prev, "fishing_log")) ++lost;
      snprintf(prev, sizeof prev, "%s", beat);
    }
  }
  printf("stream memory: the rod lost %d times, fished with a stick %d times\n", lost, sticks);
  CHECK(lost > 0 && sticks > 0, "rod lost %d, stick used %d in 25 loops", lost, sticks);
  CHECK(bad_stick == 0, "%d rod_lost mistakes", bad_stick);
  r.stop();
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
  testSpineOverruns();
  testMemory();
  testVariety();
  testMemoryCarry();
  testLoopLength();
  testLoopFade();
  testSpineBudget();
  testMinors();
  testHikerScatter();
  testMajorsStartClear();
  testStreamVignettes();
  writeImages();
  if (g_fail) {
    printf("%d failure(s)\n", g_fail);
    return 1;
  }
  printf("test_scene: all passed\n");
  return 0;
}
