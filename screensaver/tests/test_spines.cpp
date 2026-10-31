// Spine family tests (SPINES_WEATHER_LOOKOUT_SPEC.md sections 1 and 2): the
// family pick; the evening's --loops output is what it was before the
// families went in (the strings below were captured from `ss_preview --loops
// 10 --mood 4 --seed 1` and `--seed 7` before any of this was written); every
// morning variant plays over 40 seeds with no overrun, the hiker in frame
// whenever he is visible (but for his walk out of the frame at the end), the
// reserved corner clean, the loop 480 to 660 s and the fog lifting in its
// steps; the camp is pitched when the build ends; incremental rendering equals
// a full render through a morning loop; strips per frame; sizeof(App).
// Plain executable; returns non-zero on failure.
#include "ss_app.h"
#include "scene/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

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
  void* mem = nullptr;
  Pixel* fb = nullptr;
  Surface surf{};
  App* app = nullptr;

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

// The preview's inputs for --mood M [--spine F] (have_outside false, as ss_preview has it).
static Inputs makeInputs(float mood, int force = 0, bool night = false) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.weather.night_dim = night ? 1.0f : 0.0f;
  in.night_mode = night;
  in.spine_force = (uint8_t)force;
  return in;
}
static const int kForceMorning = 1 + SF_MORNING;

static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }

// ---- the evening must not move ------------------------------------------------------

// Captured before the spine families went in (ss_preview --loops 10 --mood 4).
static const char* const kEvening1[10] = {
    "loop=0 seed=1 length=650s plan=arrival=2 setup=1 firewood=1 fire=0 dinner=0 foodbag=0 bear=1 close=0 fillers=cloud_watch,stream_listen majors=pinecone,map_chase memory=loop=1 cairn=0 shoe=1 stolen=shoe hole=1 rod=0 brave=chipmunk,frog,deer,bear,mouse ring=pinecone,map_chase",
    "loop=1 seed=1027777925 length=638s plan=arrival=2 setup=1 firewood=1 fire=1 dinner=3 foodbag=2 bear=1 close=1 fillers=mug_sunset majors=echo,fishing_good,squirrel_splash memory=loop=2 cairn=0 shoe=1 stolen=mug hole=0 rod=0 brave=chipmunk,frog,deer,bear,mouse ring=pinecone,map_chase,echo,fishing_good,squirrel_splash",
    "loop=2 seed=2322246646 length=650s plan=arrival=1 setup=2 firewood=2 fire=2 dinner=0 foodbag=0 bear=1 close=1 fillers=skip_stone,cairn majors=ants_cookie,fish_leap,shadow_theater memory=loop=3 cairn=6 shoe=1 stolen=mug hole=0 rod=0 brave=chipmunk,frog,deer,raccoon,owl,bear,mouse ring=pinecone,map_chase,echo,fishing_good,squirrel_splash,ants_cookie,fish_leap,shadow_theater",
    "loop=3 seed=1541821764 length=595s plan=arrival=1 setup=0 firewood=0 fire=1 dinner=2 foodbag=0 bear=0 close=2 fillers=skip_stone majors=deer_stick,fishing_small,ranger_ridge memory=loop=4 cairn=5 shoe=1 stolen=mug hole=0 rod=1 brave=chipmunk,frog,deer,raccoon,owl,bear,mouse ring=fishing_good,squirrel_splash,ants_cookie,fish_leap,shadow_theater,deer_stick,fishing_small,ranger_ridge",
    "loop=4 seed=1409978845 length=604s plan=arrival=1 setup=0 firewood=0 fire=0 dinner=1 foodbag=2 bear=1 close=0 fillers=read majors=ants_cookie,bathe,echo memory=loop=5 cairn=4 shoe=1 stolen=book hole=0 rod=1 brave=chipmunk,deer,raccoon,owl,bear ring=fish_leap,shadow_theater,deer_stick,fishing_small,ranger_ridge,ants_cookie,bathe,echo",
    "loop=5 seed=1956810847 length=611s plan=arrival=0 setup=0 firewood=0 fire=2 dinner=1 foodbag=2 bear=1 close=2 fillers=mug_sunset majors=ranger_ridge,map_chase,fishing_log memory=loop=6 cairn=3 shoe=1 stolen=mug hole=0 rod=1 brave=chipmunk,deer,raccoon,owl,bear ring=fishing_small,ranger_ridge,ants_cookie,bathe,echo,ranger_ridge,map_chase,fishing_log",
    "loop=6 seed=418933486 length=619s plan=arrival=2 setup=0 firewood=1 fire=0 dinner=0 foodbag=1 bear=0 close=1 fillers=birds_on_water majors=fish_leap,squirrel_splash,raccoon_pack memory=loop=7 cairn=2 shoe=0 stolen=mug hole=0 rod=1 brave=squirrel,raccoon,bird,mouse ring=bathe,echo,ranger_ridge,map_chase,fishing_log,fish_leap,squirrel_splash,raccoon_pack",
    "loop=7 seed=3582373868 length=645s plan=arrival=2 setup=2 firewood=2 fire=0 dinner=2 foodbag=0 bear=1 close=1 fillers=stream_listen majors=fishing_small,ants_cookie,pinecone,shadow_theater memory=loop=8 cairn=1 shoe=1 stolen=shoe hole=0 rod=1 brave=squirrel,raccoon,bird,mouse ring=fishing_log,fish_leap,squirrel_splash,raccoon_pack,fishing_small,ants_cookie,pinecone,shadow_theater",
    "loop=8 seed=2338450490 length=623s plan=arrival=2 setup=1 firewood=1 fire=0 dinner=2 foodbag=0 bear=0 close=0 fillers=cairn majors=deer_stick,echo,bathe memory=loop=9 cairn=4 shoe=1 stolen=shoe hole=1 rod=1 brave=squirrel,frog,deer,raccoon,bird ring=raccoon_pack,fishing_small,ants_cookie,pinecone,shadow_theater,deer_stick,echo,bathe",
    "loop=9 seed=3100862149 length=624s plan=arrival=1 setup=2 firewood=1 fire=0 dinner=3 foodbag=0 bear=0 close=1 fillers=read majors=ranger_ridge,fishing_good,squirrel_splash memory=loop=10 cairn=3 shoe=1 stolen=book hole=0 rod=0 brave=squirrel,frog,deer,raccoon,bird ring=pinecone,shadow_theater,deer_stick,echo,bathe,ranger_ridge,fishing_good,squirrel_splash",
};
static const char* const kEvening7[10] = {
    "loop=0 seed=7 length=650s plan=arrival=0 setup=1 firewood=1 fire=2 dinner=2 foodbag=2 bear=2 close=2 fillers=cloud_watch,cairn majors=echo,squirrel_splash memory=loop=1 cairn=5 shoe=1 stolen=shoe hole=0 rod=0 brave=chipmunk,squirrel,frog,raccoon,bear ring=echo,squirrel_splash",
    "loop=1 seed=2356717238 length=622s plan=arrival=1 setup=1 firewood=2 fire=1 dinner=1 foodbag=2 bear=2 close=1 fillers=mug_sunset majors=map_chase,raccoon_pack memory=loop=2 cairn=4 shoe=0 stolen=mug hole=1 rod=0 brave=chipmunk,squirrel,frog,raccoon,bear ring=echo,squirrel_splash,map_chase,raccoon_pack",
    "loop=2 seed=1817722286 length=611s plan=arrival=2 setup=1 firewood=1 fire=1 dinner=2 foodbag=2 bear=0 close=2 fillers=mug_sunset majors=fish_leap,deer_stick,pinecone memory=loop=3 cairn=3 shoe=1 stolen=shoe hole=0 rod=0 brave=squirrel,frog,deer,bear,mouse ring=echo,squirrel_splash,map_chase,raccoon_pack,fish_leap,deer_stick,pinecone",
    "loop=3 seed=3923558491 length=619s plan=arrival=1 setup=0 firewood=2 fire=2 dinner=0 foodbag=0 bear=0 close=1 fillers=cloud_watch majors=ranger_ridge,bathe,shadow_theater memory=loop=4 cairn=2 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,frog,deer,bear,mouse ring=map_chase,raccoon_pack,fish_leap,deer_stick,pinecone,ranger_ridge,bathe,shadow_theater",
    "loop=4 seed=3055961913 length=608s plan=arrival=1 setup=0 firewood=1 fire=1 dinner=0 foodbag=1 bear=0 close=0 fillers=mug_sunset majors=fishing_log,map_chase memory=loop=5 cairn=1 shoe=1 stolen=mug hole=0 rod=1 brave=squirrel,bird ring=fish_leap,deer_stick,pinecone,ranger_ridge,bathe,shadow_theater,fishing_log,map_chase",
    "loop=5 seed=3078075808 length=650s plan=arrival=2 setup=2 firewood=2 fire=2 dinner=0 foodbag=0 bear=1 close=0 fillers=cloud_watch,skip_stone majors=squirrel_splash,ants_cookie,raccoon_pack memory=loop=6 cairn=0 shoe=0 stolen=mug hole=1 rod=1 brave=squirrel,bird ring=ranger_ridge,bathe,shadow_theater,fishing_log,map_chase,squirrel_splash,ants_cookie,raccoon_pack",
    "loop=6 seed=1481336605 length=622s plan=arrival=0 setup=0 firewood=2 fire=0 dinner=1 foodbag=1 bear=1 close=1 fillers=birds_on_water majors=echo,pinecone,fish_leap,raccoon_pack memory=loop=7 cairn=0 shoe=1 stolen=shoe hole=0 rod=1 brave=squirrel,raccoon,bear ring=map_chase,squirrel_splash,ants_cookie,raccoon_pack,echo,pinecone,fish_leap,raccoon_pack",
    "loop=7 seed=3625058471 length=641s plan=arrival=0 setup=2 firewood=1 fire=2 dinner=2 foodbag=2 bear=1 close=0 fillers=cloud_watch,mug_sunset majors=bathe,fishing_small memory=loop=8 cairn=0 shoe=1 stolen=mug hole=0 rod=1 brave=squirrel,raccoon,bear ring=ants_cookie,raccoon_pack,echo,pinecone,fish_leap,raccoon_pack,bathe,fishing_small",
    "loop=8 seed=3169107222 length=621s plan=arrival=1 setup=1 firewood=2 fire=0 dinner=0 foodbag=2 bear=2 close=1 fillers=read majors=map_chase,squirrel_splash,deer_stick memory=loop=9 cairn=0 shoe=1 stolen=mug hole=0 rod=1 brave=chipmunk,frog,mouse ring=pinecone,fish_leap,raccoon_pack,bathe,fishing_small,map_chase,squirrel_splash,deer_stick",
    "loop=9 seed=1914874671 length=605s plan=arrival=1 setup=2 firewood=0 fire=1 dinner=1 foodbag=0 bear=0 close=1 fillers=stream_listen majors=ants_cookie,echo,pinecone memory=loop=10 cairn=0 shoe=1 stolen=mug hole=0 rod=1 brave=chipmunk,frog,mouse ring=bathe,fishing_small,map_chase,squirrel_splash,deer_stick,ants_cookie,echo,pinecone",
};

// One --loops line, as ss_preview's previewRunLoops writes it, with the
// family prefix the plan now carries taken out (it must be there).
static void loopLines(uint64_t seed, char out[10][800]) {
  Rig a;
  CHECK(a.start(seed, makeInputs(4.f)), "start");
  for (int loop = 0; loop < 10; ++loop) {
    char plan[320], mem[320];
    appLoopPlan(a.app, plan, sizeof plan);
    const unsigned long long ls = (unsigned long long)appLoopSeed(a.app);
    const uint32_t len = appLoopTicks(a.app);
    uint32_t ticks = 0;
    for (; ticks + 1 < len; ++ticks) appTick(a.app);
    appMemory(a.app, mem, sizeof mem);
    for (int guard = 0; guard < 40 * 60 * SS_TICK_HZ; ++guard) {
      ++ticks;
      if (appTick(a.app)) break;
    }
    const char* pre = "family=evening ";
    const bool has = strncmp(plan, pre, strlen(pre)) == 0;
    CHECK(has, "seed %d loop %d: the evening plan line does not start with %s: %s", (int)seed, loop, pre, plan);
    snprintf(out[loop], 800, "loop=%d seed=%llu length=%.0fs plan=%s memory=%s", loop, ls, (double)ticks / SS_TICK_HZ,
             has ? plan + strlen(pre) : plan, mem);
  }
  a.stop();
}

static void testEveningRegression() {
  const uint64_t seeds[2] = {1, 7};
  const char* const* want[2] = {kEvening1, kEvening7};
  int same = 0;
  for (int s = 0; s < 2; ++s) {
    char got[10][800];
    loopLines(seeds[s], got);
    for (int i = 0; i < 10; ++i) {
      const bool ok = strcmp(got[i], want[s][i]) == 0;
      if (!ok) printf("seed %d loop %d\n  want %s\n  got  %s\n", (int)seeds[s], i, want[s][i], got[i]);
      CHECK(ok, "evening --loops line differs (seed %d loop %d)", (int)seeds[s], i);
      same += ok ? 1 : 0;
    }
  }
  printf("evening regression: %d of 20 --loops lines (seeds 1 and 7, mood 4) identical to before the families\n", same);
}

// ---- the pick -----------------------------------------------------------------------

static void testFamilyPick() {
  struct Case { float mood; int force; bool night, lookout; int want; };
  const Case cases[] = {
      {0.2f, 0, false, false, SF_EVENING},  {0.5f, 0, false, false, SF_MORNING}, {1.5f, 0, false, false, SF_MORNING},
      {2.49f, 0, false, false, SF_MORNING}, {2.5f, 0, false, false, SF_EVENING},   // midday: not built, the evening
      {3.f, 0, false, false, SF_EVENING},   {4.f, 0, false, false, SF_EVENING},  {5.5f, 0, false, false, SF_EVENING},
      {7.5f, 0, false, false, SF_MORNING},  {1.5f, 0, true, false, SF_EVENING},  {1.5f, 0, false, true, SF_EVENING},
      {4.f, kForceMorning, false, false, SF_MORNING}, {1.5f, 1 + SF_EVENING, false, false, SF_EVENING},
  };
  for (const Case& c : cases) {
    Inputs in = makeInputs(c.mood, c.force, c.night);
    in.lookout_window = c.lookout;
    CHECK(spineFamilyFor(in) == c.want, "mood %.2f force %d night %d lookout %d: family %d, want %d", (double)c.mood,
          c.force, c.night, c.lookout, spineFamilyFor(in), c.want);
  }
  // The pick costs the loop rng nothing: a forced evening at mood 1.5 plans
  // what the evening at mood 1.5 would (the plan only reads the mood for the
  // sunset mug's weight, the same on both sides of 1.5).
  Rig a, b;
  CHECK(a.start(3, makeInputs(1.5f, 1 + SF_EVENING)) && b.start(3, makeInputs(1.6f, 1 + SF_EVENING)), "start");
  char pa[320], pb[320];
  appLoopPlan(a.app, pa, sizeof pa);
  appLoopPlan(b.app, pb, sizeof pb);
  CHECK(strcmp(pa, pb) == 0, "forced evening plans differ: %s / %s", pa, pb);
  CHECK(appSpineFamily(a.app) == SF_EVENING, "forced evening plays family %d", appSpineFamily(a.app));
  a.stop();
  b.stop();
}

// ---- the morning ---------------------------------------------------------------------

static const char* const kMBeats[6] = {"wake", "coffee", "bag_down", "break_camp", "douse", "leave"};
static const int kMVariants[6] = {3, 3, 2, 3, 2, 3};

static int beatIndex(const char* name) {
  for (int i = 0; i < 6; ++i)
    if (strcmp(name, kMBeats[i]) == 0) return i;
  return -1;
}

static bool isMajor(const char* name) {
  static const char* const kMajor[] = {"squirrel_splash", "deer_stick",    "pinecone", "map_chase",   "fish_leap",
                                       "raccoon_pack",    "ants_cookie",   "shadow_theater", "ranger_ridge", "echo",
                                       "firefly_jar",     "stargaze",      "fishing_small",  "fishing_log",  "fishing_good",
                                       "bathe"};
  for (const char* k : kMajor)
    if (strcmp(name, k) == 0) return true;
  return false;
}

// The palette the app draws with now (the morning's fog on top of the inputs').
static void scenePalette(const App* a, const Inputs& in, Palette& pal) {
  WeatherTint w = in.weather;
  const float fog = appWorld(a)->fog_extra;
  if (fog > 0.f) w.fog = w.fog + fog > 1.f ? 1.f : w.fog + fog;
  computePalette(in.mood, w, pal);
}

static int badCornerPixels(const Rig& a, const Palette& pal, int seed, int tick, int shown) {
  int bad = 0;
  for (int y = LS_QUIET_Y0_T; y < SS_HEIGHT; ++y) {
    for (int x = LS_QUIET_X0_T; x < SS_WIDTH; ++x) {
      const Pixel p = a.fb[y * SS_WIDTH + x];
      if (p != pal[GROUND] && p != pal[GROUND_DARK] && p != pal[GRASS]) {
        if (shown + bad < 5) printf("morning seed %d tick %d: corner pixel (%d,%d) = %04x\n", seed, tick, x, y, p);
        ++bad;
      }
    }
  }
  return bad;
}

static void testMorningVariants() {
  bool seen[6][3] = {};
  int bad_frame = 0, bad_corner = 0, bad_end = 0, overrun_loops = 0, len_bad = 0, corner_samples = 0;
  int lo = 1 << 30, hi = 0, reb_lo = 1 << 30, reb_hi = 0, kinds = 0;
  bool bird = false, chip = false;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    const Inputs in = makeInputs(1.5f, kForceMorning);
    Rig a;
    CHECK(a.start(seed, in), "start");
    const World* w = appWorld(a.app);
    CHECK(appSpineFamily(a.app) == SF_MORNING, "seed %d: family %d", (int)seed, appSpineFamily(a.app));
    const LoopPlan plan = w->plan;
    for (int b = 0; b < 6; ++b) {
      CHECK(plan.fvar[b] < kMVariants[b], "seed %d: %s variant %d", (int)seed, kMBeats[b], plan.fvar[b]);
      if (plan.fvar[b] < 3) seen[b][plan.fvar[b]] = true;
    }
    const int len = (int)appLoopTicks(a.app);
    lo = len < lo ? len : lo;
    hi = len > hi ? len : hi;
    if (len < 480 * SS_TICK_HZ || len > 660 * SS_TICK_HZ) {
      ++len_bad;
      printf("seed %d: morning loop is %.1f s\n", (int)seed, (double)len / SS_TICK_HZ);
    }
    float fog_prev = w->fog_extra;
    bool fog_up = false;
    char prev[32] = "build";
    for (int t = 1; t < len; ++t) {   // up to the loop's last tick
      appTick(a.app);
      // The camp is pitched and he is asleep in it when the build ends.
      if (t == 330) {
        CHECK(w->tent.state == TENT_UP && !w->tent.lit && w->bag.state == BAG_HUNG && w->pack.shown &&
                  w->pack.pop == 1.f && w->fire.ash == 1.f && w->fire.state == FIRE_UNLIT && !w->hiker.visible &&
                  w->cairn.n == plan.cairn_start,
              "seed %d: the camp at tick 330: tent %d lit %d bag %d pack %d/%.2f ash %.2f fire %d hiker %d cairn %d/%d",
              (int)seed, w->tent.state, w->tent.lit, w->bag.state, w->pack.shown, (double)w->pack.pop,
              (double)w->fire.ash, w->fire.state, w->hiker.visible, w->cairn.n, plan.cairn_start);
      }
      if (w->fog_extra > fog_prev) fog_up = true;
      fog_prev = w->fog_extra;
      Rect r;
      const bool vis = appHikerBounds(a.app, &r);
      const char* beat = appBeatName(a.app);
      if (vis) {
        const bool corner = r.x1 > LS_QUIET_X0_T && r.y1 > LS_QUIET_Y0_T;
        const bool rows = r.y0 >= 0 && r.y1 <= SS_HEIGHT;
        // Wholly in the frame, but on his way out at the end (the leave) and
        // in the majors (their own rule: test_scene, at least partly in).
        bool cols = r.x0 >= 0 && r.x1 <= SS_WIDTH;
        if (beatIs(a.app, "leave") || isMajor(beat)) cols = r.x1 > 0 && r.x0 < SS_WIDTH;
        if (!(rows && cols) || corner) {
          if (bad_frame < 5)
            printf("seed %d tick %d (%s): hiker rect %d,%d,%d,%d\n", (int)seed, t, beat, r.x0, r.y0, r.x1, r.y1);
          ++bad_frame;
        }
      }
      for (int i = 0; i < 8; ++i) {
        Rect cr;
        int k = -1;
        if (!appCritterRect(a.app, i, &cr, &k)) continue;
        if (beatIs(a.app, "wake") && k == CR_BIRD) bird = true;
        if (beatIs(a.app, "coffee") && k == CR_CHIPMUNK) chip = true;
        const bool in_frame = cr.x1 > 0 && cr.x0 < SS_WIDTH && cr.y0 >= 0 && cr.y1 <= SS_HEIGHT;
        const bool corner = cr.x1 > LS_QUIET_X0_T && cr.y1 > LS_QUIET_Y0_T;
        if (!in_frame || corner) {
          if (kinds < 3) printf("seed %d tick %d (%s): critter kind %d rect %d,%d,%d,%d\n", (int)seed, t, beat, k, cr.x0,
                                cr.y0, cr.x1, cr.y1);
          ++kinds;
        }
      }
      if (t % 250 == 0 && appFade(a.app) == 0.f) {
        Palette pal;
        scenePalette(a.app, in, pal);
        appInvalidate(a.app);
        appRender(a.app, a.surf);
        bad_corner += badCornerPixels(a, pal, (int)seed, t, bad_corner);
        ++corner_samples;
      }
      if (!beatIs(a.app, prev)) {   // a beat just ended
        const int b = beatIndex(prev);
        if (b >= 0 && b < 5)
          CHECK(vis && r.x0 >= 0 && r.x1 <= SS_WIDTH, "seed %d: %s %d ends with the hiker out of frame", (int)seed,
                kMBeats[b], plan.fvar[b]);
        snprintf(prev, sizeof prev, "%s", beat);
      }
    }
    // One tick left (the leave's last): every beat but it has ended, and it
    // counts an unfinished script when its fade starts.
    const int over = appSpineOverruns(a.app);
    if (over) {
      char p[320];
      appLoopPlan(a.app, p, sizeof p);
      printf("seed %d (%s): %d beat(s) overran\n", (int)seed, p, over);
      ++overrun_loops;
    }
    Rect r;
    if (appCritterCount(a.app) != 0 || appHikerBounds(a.app, &r) || appFade(a.app) < 0.99f) {
      ++bad_end;
      printf("seed %d: at the end %d critters, hiker visible %d, fade %.3f\n", (int)seed, appCritterCount(a.app),
             appHikerBounds(a.app, &r), (double)appFade(a.app));
    }
    // The fog: never up again once lifting, gone by the end of the coffee's
    // pause, one palette rebuild per 0.05 step (plus the loop's own).
    CHECK(!fog_up, "seed %d: the fog came back", (int)seed);
    CHECK(w->fog_extra == 0.f, "seed %d: fog %.2f left at the end", (int)seed, (double)w->fog_extra);
    const int reb = appPaletteRebuilds(a.app);
    reb_lo = reb < reb_lo ? reb : reb_lo;
    reb_hi = reb > reb_hi ? reb : reb_hi;
    CHECK(reb == 1 + plan.fog_steps, "seed %d: %d palette rebuilds for %d fog steps", (int)seed, reb, plan.fog_steps);
    const bool rolled = appTick(a.app);
    CHECK(rolled, "seed %d: no rollover at the plan's length", (int)seed);
    a.stop();
  }
  int missing = 0;
  for (int b = 0; b < 6; ++b)
    for (int k = 0; k < kMVariants[b]; ++k)
      if (!seen[b][k]) {
        ++missing;
        printf("morning %s variant %d never ran in 40 seeds\n", kMBeats[b], k);
      }
  printf("morning: 40 loops, %.0f..%.0f s, palette rebuilds %d..%d a loop, %d corner samples\n",
         (double)lo / SS_TICK_HZ, (double)hi / SS_TICK_HZ, reb_lo, reb_hi, corner_samples);
  CHECK(missing == 0, "%d morning variants unseen", missing);
  CHECK(overrun_loops == 0, "%d morning loops overran", overrun_loops);
  CHECK(bad_frame == 0, "%d hiker samples out of frame", bad_frame);
  CHECK(kinds == 0, "%d critter samples out of frame or in the corner", kinds);
  CHECK(bad_corner == 0, "%d corner pixels not ground or grass", bad_corner);
  CHECK(bad_end == 0, "%d loops did not end empty and black", bad_end);
  CHECK(len_bad == 0, "%d loops outside 480..660 s", len_bad);
  CHECK(bird, "the wake c bird never showed");
  CHECK(chip, "the coffee c chipmunk never showed");
}

// Incremental rendering (every tick) equals a full render at checkpoints
// through a whole morning loop: the camp popping in, the door, the ash, the
// fly, the fog steps. Strips per frame on the way.
static void testMorningIncremental() {
  static const uint64_t kSeeds[4] = {1, 2, 3, 6};
  long sum = 0, frames = 0;
  int worst = 0, diffs = 0, checks = 0;
  for (uint64_t seed : kSeeds) {
    const Inputs in = makeInputs(1.5f, kForceMorning);
    Rig a, b;
    CHECK(a.start(seed, in) && b.start(seed, in), "start");
    const int len = (int)appLoopTicks(a.app);
    for (int t = 1; t < len; ++t) {
      appTick(a.app);
      appTick(b.app);
      const int n = appRender(a.app, a.surf);
      if (t > 330) {
        sum += n;
        ++frames;
        if (n > worst) worst = n;
      }
      if (t % 397 == 0 || t == 260 || t == 300) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        if (memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0) {
          long i = 0;
          while (a.fb[i] == b.fb[i]) ++i;
          if (diffs < 4)
            printf("seed %d tick %d (%s): incremental differs from full at (%ld,%ld)\n", (int)seed, t,
                   appBeatName(a.app), i % SS_WIDTH, i / SS_WIDTH);
          ++diffs;
        }
      }
    }
    a.stop();
    b.stop();
  }
  printf("morning strips per frame (4 loops, build excluded): avg %.2f, max %d; %d full-render checks\n",
         frames ? (double)sum / (double)frames : 0.0, worst, checks);
  CHECK(diffs == 0, "%d checkpoints where incremental and full renders differ", diffs);
}

// Families in turn on one App (the inputs change between loops, as the
// host's clock moves them): evening and morning loops alternate in pairs, so
// a morning follows an evening (the raccoon may have the shoe: wake b treads
// on the mug) and a morning follows a morning (memory's last variants). No
// overrun, no critter left, every loop in the length range.
static void testMixedLoops() {
  int bad = 0, mornings = 0, evenings = 0;
  for (uint64_t seed = 21; seed <= 23; ++seed) {
    Rig a;
    Inputs in = makeInputs(4.f);
    CHECK(a.start(seed, in), "start");
    for (int loop = 0; loop < 12; ++loop) {
      const int fam = appSpineFamily(a.app);
      (fam == SF_MORNING ? mornings : evenings)++;
      const uint32_t len = appLoopTicks(a.app);
      if (len < 480u * SS_TICK_HZ || len > 660u * SS_TICK_HZ) ++bad;
      for (uint32_t t = 0; t + 1 < len; ++t) appTick(a.app);
      if (appSpineOverruns(a.app) != 0 || appCritterCount(a.app) != 0) {
        char p[320];
        appLoopPlan(a.app, p, sizeof p);
        printf("seed %d loop %d (%s): %d overruns, %d critters left\n", (int)seed, loop, p, appSpineOverruns(a.app),
               appCritterCount(a.app));
        ++bad;
      }
      in.mood = (loop / 2) % 2 == 0 ? 1.5f : 4.f;   // the next loop's family: two of each in turn
      appSetInputs(a.app, in);
      CHECK(appTick(a.app), "seed %d loop %d: no rollover at the plan's length", (int)seed, loop);
    }
    a.stop();
  }
  printf("mixed families: %d evening and %d morning loops in turn\n", evenings, mornings);
  CHECK(bad == 0, "%d mixed loops overran, left critters or missed the length range", bad);
  CHECK(mornings >= 12 && evenings >= 12, "families did not alternate (%d evening, %d morning)", evenings, mornings);
}

static void testAppSize() {
  printf("sizeof(App) = %u bytes (budget %d)\n", (unsigned)appAppBytes(), SS_APP_BYTES);
  CHECK(appAppBytes() <= (uint32_t)SS_APP_BYTES, "App is %u bytes", (unsigned)appAppBytes());
}

int main() {
  testFamilyPick();
  testEveningRegression();
  testMorningVariants();
  testMorningIncremental();
  testMixedLoops();
  testAppSize();
  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all spine family tests passed\n");
  return 0;
}
