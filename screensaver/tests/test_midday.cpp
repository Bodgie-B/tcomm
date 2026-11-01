// Midday spine tests (SPINES_WEATHER_LOOKOUT_SPEC.md section 3): every
// midday variant plays over 40 seeds with no overrun, the hiker in frame
// whenever he is visible (but for his walk in at the start), the reserved
// corner clean, the loop 480 to 660 s, every beat ending with him at rest and
// the props back (the hat on, nothing in his hands, the boots on, out of the
// water); the camp built and him away when the build ends; each variant's
// critter, bit or glyph shows; incremental rendering equals a full render
// through midday loops; strips per frame; families in turn on one App; and
// the regression lines: the evening's and the morning's `ss_preview --loops
// 10` output (seeds 1 and 7 at mood 4; seed 1 at mood 1.5 --spine morning) is
// what it was before the midday went in, and the midday's own (seed 1, mood
// 3) is pinned. Plain executable; returns non-zero on failure.
#include "ss_app.h"
#include "scene/spine_family.h"
#include "scene/weather.h"
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
static Inputs makeInputs(float mood, int force = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.spine_force = (uint8_t)force;
  return in;
}
static const int kForceMorning = 1 + SF_MORNING, kForceMidday = 1 + SF_MIDDAY;

static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }

// ---- the regression lines --------------------------------------------------------------

// Captured before the spine families went in (ss_preview --loops 10 --mood 4);
// the same strings test_spines.cpp holds.
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
// ss_preview --loops 10 --seed 1 --mood 1.5 --spine morning, captured when the
// midday went in (2026-10-31), from the tree with the morning's review fixes
// (book_up in the leave) and checked byte-identical against that same tree
// built without the midday. A later change to the morning moves these lines:
// re-capture them then.
static const char* const kMorning1[10] = {
    "loop=0 seed=1 length=617s plan=family=morning wake=2 coffee=1 bag_down=1 break_camp=0 douse=0 leave=0 fillers=cloud_watch,birds_on_water,read majors=pinecone,echo,deer_stick fog=12 memory=loop=1 cairn=0 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,frog,deer,mouse ring=pinecone,echo,deer_stick",
    "loop=1 seed=2542665771 length=641s plan=family=morning wake=2 coffee=0 bag_down=0 break_camp=2 douse=0 leave=0 fillers=cairn,cloud_watch,read majors=map_chase,squirrel_splash fog=12 memory=loop=2 cairn=6 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,frog,deer,mouse ring=pinecone,echo,deer_stick,map_chase,squirrel_splash",
    "loop=2 seed=4185136349 length=615s plan=family=morning wake=0 coffee=1 bag_down=1 break_camp=1 douse=0 leave=1 fillers=read,stream_listen,cairn majors=fish_leap,ranger_ridge,bathe fog=12 memory=loop=3 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=deer,raccoon,owl,bird ring=pinecone,echo,deer_stick,map_chase,squirrel_splash,fish_leap,ranger_ridge,bathe",
    "loop=3 seed=818092176 length=618s plan=family=morning wake=1 coffee=2 bag_down=0 break_camp=2 douse=1 leave=2 fillers=read,stream_listen majors=fishing_log,map_chase,ants_cookie fog=16 memory=loop=4 cairn=7 shoe=0 stolen=none hole=0 rod=1 brave=deer,raccoon,owl,bird ring=map_chase,squirrel_splash,fish_leap,ranger_ridge,bathe,fishing_log,map_chase,ants_cookie",
    "loop=4 seed=1469185293 length=615s plan=family=morning wake=0 coffee=1 bag_down=0 break_camp=0 douse=0 leave=0 fillers=stream_listen,birds_on_water majors=fishing_small,deer_stick,ranger_ridge fog=12 memory=loop=5 cairn=6 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,frog,raccoon,owl,bird,mouse ring=ranger_ridge,bathe,fishing_log,map_chase,ants_cookie,fishing_small,deer_stick,ranger_ridge",
    "loop=5 seed=1368558889 length=650s plan=family=morning wake=1 coffee=0 bag_down=1 break_camp=1 douse=1 leave=1 fillers=stream_listen,cairn,birds_on_water,cloud_watch majors=fish_leap,echo,pinecone fog=12 memory=loop=6 cairn=7 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,frog,raccoon,owl,bird,mouse ring=map_chase,ants_cookie,fishing_small,deer_stick,ranger_ridge,fish_leap,echo,pinecone",
    "loop=6 seed=1733334511 length=608s plan=family=morning wake=0 coffee=2 bag_down=0 break_camp=2 douse=0 leave=0 fillers=birds_on_water,cairn,stream_listen majors=map_chase,fishing_log,squirrel_splash fog=14 memory=loop=7 cairn=7 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,frog,deer,owl,bear,mouse ring=deer_stick,ranger_ridge,fish_leap,echo,pinecone,map_chase,fishing_log,squirrel_splash",
    "loop=7 seed=2267487621 length=625s plan=family=morning wake=2 coffee=0 bag_down=1 break_camp=1 douse=0 leave=2 fillers=cairn,cloud_watch,birds_on_water majors=fish_leap,ants_cookie,deer_stick fog=15 memory=loop=8 cairn=7 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,frog,deer,owl,bear,mouse ring=echo,pinecone,map_chase,fishing_log,squirrel_splash,fish_leap,ants_cookie,deer_stick",
    "loop=8 seed=3723266097 length=649s plan=family=morning wake=2 coffee=2 bag_down=1 break_camp=0 douse=1 leave=0 fillers=stream_listen,cloud_watch majors=ranger_ridge,fishing_small,bathe fog=16 memory=loop=9 cairn=6 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,frog,raccoon,bird,bear ring=fishing_log,squirrel_splash,fish_leap,ants_cookie,deer_stick,ranger_ridge,fishing_small,bathe",
    "loop=9 seed=3456489385 length=626s plan=family=morning wake=1 coffee=0 bag_down=0 break_camp=2 douse=1 leave=2 fillers=read,cloud_watch,stream_listen majors=pinecone,echo,map_chase fog=13 memory=loop=10 cairn=6 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,frog,raccoon,bird,bear ring=ants_cookie,deer_stick,ranger_ridge,fishing_small,bathe,pinecone,echo,map_chase",
};
// The midday's own, pinned when it was built (ss_preview --loops 10 --seed 1 --mood 3).
static const char* const kMidday1[10] = {
    "loop=0 seed=1 length=631s plan=family=midday return=2 lunch=0 wade=1 nap=0 map=0 afternoon=0 fillers=cloud_watch,stream_listen,cairn majors=deer_stick,pinecone,ranger_ridge memory=loop=1 cairn=6 shoe=0 stolen=none hole=0 rod=0 brave=squirrel,frog,bird,mouse ring=deer_stick,pinecone,ranger_ridge",
    "loop=1 seed=3851155901 length=603s plan=family=midday return=1 lunch=1 wade=0 nap=1 map=1 afternoon=1 fillers=cairn,cloud_watch,skip_stone majors=ants_cookie,fish_leap,map_chase memory=loop=2 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=squirrel,frog,bird,mouse ring=deer_stick,pinecone,ranger_ridge,ants_cookie,fish_leap,map_chase",
    "loop=2 seed=3700665890 length=650s plan=family=midday return=2 lunch=1 wade=2 nap=0 map=1 afternoon=0 fillers=stream_listen,cairn,cloud_watch majors=deer_stick,bathe memory=loop=3 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,squirrel,frog,deer,raccoon,owl,bird,bear ring=deer_stick,pinecone,ranger_ridge,ants_cookie,fish_leap,map_chase,deer_stick,bathe",
    "loop=3 seed=1306309954 length=629s plan=family=midday return=0 lunch=0 wade=1 nap=1 map=0 afternoon=1 fillers=cloud_watch,read majors=fishing_log,squirrel_splash,echo memory=loop=4 cairn=6 shoe=0 stolen=none hole=0 rod=1 brave=chipmunk,squirrel,frog,deer,raccoon,owl,bird,bear ring=ants_cookie,fish_leap,map_chase,deer_stick,bathe,fishing_log,squirrel_splash,echo",
    "loop=4 seed=353847738 length=598s plan=family=midday return=1 lunch=1 wade=1 nap=2 map=1 afternoon=1 fillers=read,cairn,skip_stone majors=pinecone,ants_cookie memory=loop=5 cairn=7 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,frog,deer,owl,mouse ring=map_chase,deer_stick,bathe,fishing_log,squirrel_splash,echo,pinecone,ants_cookie",
    "loop=5 seed=3740524944 length=593s plan=family=midday return=0 lunch=0 wade=0 nap=1 map=0 afternoon=0 fillers=birds_on_water,read majors=fishing_small,ranger_ridge,fish_leap memory=loop=6 cairn=6 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,frog,deer,owl,mouse ring=fishing_log,squirrel_splash,echo,pinecone,ants_cookie,fishing_small,ranger_ridge,fish_leap",
    "loop=6 seed=718072938 length=638s plan=family=midday return=1 lunch=1 wade=2 nap=0 map=0 afternoon=1 fillers=cloud_watch,birds_on_water,read majors=fishing_log,bathe memory=loop=7 cairn=5 shoe=0 stolen=none hole=0 rod=1 brave=chipmunk,squirrel,deer,raccoon,bear,mouse ring=echo,pinecone,ants_cookie,fishing_small,ranger_ridge,fish_leap,fishing_log,bathe",
    "loop=7 seed=858216218 length=650s plan=family=midday return=2 lunch=1 wade=1 nap=1 map=1 afternoon=0 fillers=skip_stone,stream_listen,birds_on_water,cairn majors=pinecone,deer_stick,echo memory=loop=8 cairn=7 shoe=0 stolen=none hole=0 rod=1 brave=chipmunk,squirrel,deer,raccoon,bear,mouse ring=fishing_small,ranger_ridge,fish_leap,fishing_log,bathe,pinecone,deer_stick,echo",
    "loop=8 seed=4139544639 length=589s plan=family=midday return=1 lunch=0 wade=0 nap=2 map=1 afternoon=1 fillers=cairn,birds_on_water,read majors=squirrel_splash,ranger_ridge,ants_cookie memory=loop=9 cairn=7 shoe=0 stolen=none hole=0 rod=1 brave=frog,deer,raccoon,bear ring=fishing_log,bathe,pinecone,deer_stick,echo,squirrel_splash,ranger_ridge,ants_cookie",
    "loop=9 seed=617169458 length=556s plan=family=midday return=2 lunch=1 wade=1 nap=0 map=0 afternoon=1 fillers=skip_stone,cairn majors=deer_stick,map_chase,pinecone memory=loop=10 cairn=7 shoe=0 stolen=none hole=0 rod=1 brave=frog,deer,raccoon,bear ring=deer_stick,echo,squirrel_splash,ranger_ridge,ants_cookie,deer_stick,map_chase,pinecone",
};

// One --loops line, as ss_preview's previewRunLoops writes it (less the
// weather token it may add), with `strip` taken off the front of the plan.
static void loopLines(uint64_t seed, const Inputs& in, const char* strip, char out[10][800]) {
  Rig a;
  CHECK(a.start(seed, in), "start");
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
    const bool has = strncmp(plan, strip, strlen(strip)) == 0;
    CHECK(has, "seed %d loop %d: the plan line does not start with %s: %s", (int)seed, loop, strip, plan);
    snprintf(out[loop], 800, "loop=%d seed=%llu length=%.0fs plan=%s memory=%s", loop, ls, (double)ticks / SS_TICK_HZ,
             has ? plan + strlen(strip) : plan, mem);
  }
  a.stop();
}

static int compareLines(const char* what, uint64_t seed, const Inputs& in, const char* strip,
                        const char* const want[10]) {
  char got[10][800];
  loopLines(seed, in, strip, got);
  int same = 0;
  for (int i = 0; i < 10; ++i) {
    const bool ok = strcmp(got[i], want[i]) == 0;
    if (!ok) printf("%s seed %d loop %d\n  want %s\n  got  %s\n", what, (int)seed, i, want[i], got[i]);
    CHECK(ok, "%s --loops line differs (seed %d loop %d)", what, (int)seed, i);
    same += ok ? 1 : 0;
  }
  return same;
}

static void testRegression() {
  const int e1 = compareLines("evening", 1, makeInputs(4.f), "family=evening ", kEvening1);
  const int e7 = compareLines("evening", 7, makeInputs(4.f), "family=evening ", kEvening7);
  const int m1 = compareLines("morning", 1, makeInputs(1.5f, kForceMorning), "", kMorning1);
  const int d1 = compareLines("midday", 1, makeInputs(3.f, kForceMidday), "", kMidday1);
  printf("regression: evening seed 1 %d/10, seed 7 %d/10, morning seed 1 %d/10, midday seed 1 %d/10 lines identical\n",
         e1, e7, m1, d1);
}

// ---- the pick -------------------------------------------------------------------------

// Forced, the midday plays whatever the mood; picked by the mood it still
// falls back to the evening (test_scene and test_spines run the evening at
// mood 3: spineFamilyFor). The mood picks the midday now; test_scene forces
// the evening for its own checks.
static void testFamilyPick() {
  const float moods[] = {2.5f, 3.f, 3.49f, 9.f};
  for (float m : moods) {
    CHECK(spineFamilyFor(makeInputs(m, kForceMidday)) == SF_MIDDAY, "forced midday at mood %.2f", (double)m);
    CHECK(spineFamilyFor(makeInputs(m)) == SF_MIDDAY, "mood %.2f: family %d (the mood picks the midday)",
          (double)m, spineFamilyFor(makeInputs(m)));
  }
  Inputs night = makeInputs(3.f, kForceMidday);
  night.night_mode = true;
  CHECK(spineFamilyFor(night) == SF_MIDDAY, "forced midday at night");
}

// ---- every variant ---------------------------------------------------------------------

static const char* const kDBeats[6] = {"return", "lunch", "wade", "nap", "map", "afternoon"};
static const int kDVariants[6] = {3, 2, 3, 3, 2, 2};

static int beatIndex(const char* name) {
  for (int i = 0; i < 6; ++i)
    if (strcmp(name, kDBeats[i]) == 0) return i;
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

// The palette the app draws with now: the inputs' weather plus the scene's
// (the loop's weather: overcast, fog, the lightning's flash; app.cpp
// sceneWeather). Unverified against later changes to that composition.
static void scenePalette(const App* a, const Inputs& in, Palette& pal) {
  WeatherTint w = in.weather;
  const float fog = appWorld(a)->fog_extra;
  if (fog > 0.f) w.fog = w.fog + fog > 1.f ? 1.f : w.fog + fog;
  const float flash = weatherTint(*appWorld(a), w);
  computePalette(in.mood, w, flash, pal);
}

static int badCornerPixels(const Rig& a, const Palette& pal, int seed, int tick, int shown) {
  int bad = 0;
  for (int y = LS_QUIET_Y0_T; y < SS_HEIGHT; ++y) {
    for (int x = LS_QUIET_X0_T; x < SS_WIDTH; ++x) {
      const Pixel p = a.fb[y * SS_WIDTH + x];
      if (p != pal[GROUND] && p != pal[GROUND_DARK] && p != pal[GRASS]) {
        if (shown + bad < 5) printf("midday seed %d tick %d: corner pixel (%d,%d) = %04x\n", seed, tick, x, y, p);
        ++bad;
      }
    }
  }
  return bad;
}

// What a beat must leave behind: him at rest, nothing in his hands, the hat
// on, the boots on, out of the water, the beat's bit and glyph gone.
static bool propsAtRest(const World* w, char* why, size_t n) {
  const Hiker& h = w->hiker;
  const char* bad = nullptr;
  if (!h.hat_on) bad = "hat off";
  else if (h.carry != CARRY_NONE) bad = "carrying";
  else if (w->items[ITEM_HAT].down || w->items[ITEM_HAT].held) bad = "hat item out";
  else if (w->items[ITEM_SHOE].down) bad = "boots off";
  else if (w->fx.wade) bad = "wading";
  else if (w->spine.fbit >= 0) bad = "a bit still flying";
  else if (w->spine.fhold != 0) bad = "something in his hand";
  else if (h.walking) bad = "walking";
  for (int i = 0; i < ITEM_COUNT && !bad; ++i)
    if (w->items[i].held && !w->items[i].critter) bad = "an item held";
  if (bad) snprintf(why, n, "%s", bad);
  return bad == nullptr;
}

// Loop end: the afternoon has him sitting, the mug down beside him, the book
// (if the read filler left it out) back in the pack; the walking stick may lie
// where he left it (afternoon a).
static void testMiddayVariants() {
  bool seen[6][3] = {};
  int bad_frame = 0, bad_corner = 0, bad_end = 0, overrun_loops = 0, len_bad = 0, corner_samples = 0, bad_rest = 0;
  int lo = 1 << 30, hi = 0, kinds = 0, reb_hi = 0;
  bool bee = false, chip_lunch = false, fish = false, raid = false, leaf = false, fly = false, map = false;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    const Inputs in = makeInputs(3.f, kForceMidday);
    Rig a;
    CHECK(a.start(seed, in), "start");
    const World* w = appWorld(a.app);
    CHECK(appSpineFamily(a.app) == SF_MIDDAY, "seed %d: family %d", (int)seed, appSpineFamily(a.app));
    const LoopPlan plan = w->plan;
    for (int b = 0; b < 6; ++b) {
      CHECK(plan.fvar[b] < kDVariants[b], "seed %d: %s variant %d", (int)seed, kDBeats[b], plan.fvar[b]);
      if (plan.fvar[b] < 3) seen[b][plan.fvar[b]] = true;
    }
    for (int i = 0; i < plan.n_majors; ++i)
      CHECK(plan.major[i] != MJ_FISHING_GOOD, "seed %d: fishing_good in a midday plan", (int)seed);
    const int len = (int)appLoopTicks(a.app);
    lo = len < lo ? len : lo;
    hi = len > hi ? len : hi;
    if (len < 480 * SS_TICK_HZ || len > 660 * SS_TICK_HZ) {
      ++len_bad;
      printf("seed %d: midday loop is %.1f s\n", (int)seed, (double)len / SS_TICK_HZ);
    }
    char prev[32] = "build";
    // Props at rest on a beat's last tick (the next beat's first may already
    // be a weather gag in a pause taking his hat off: weather.cpp).
    bool rest_ok = true;
    char rest_why[48] = "";
    for (int t = 1; t < len; ++t) {   // up to the loop's last tick
      appTick(a.app);
      // The camp is built and he is away when the build ends.
      if (t == 330) {
        const float wood = plan.fvar[5] == 0 ? 0.45f : 1.f;
        CHECK(w->tent.state == TENT_UP && !w->tent.lit && w->bag.state == BAG_HUNG && w->pack.shown &&
                  w->pack.pop == 1.f && w->fire.ash == 1.f && w->fire.state == FIRE_UNLIT &&
                  w->items[ITEM_WOOD].size == wood && w->cairn.n == plan.cairn_start,
              "seed %d: the camp at tick 330: tent %d lit %d bag %d pack %d/%.2f ash %.2f fire %d wood %.2f cairn %d/%d",
              (int)seed, w->tent.state, w->tent.lit, w->bag.state, w->pack.shown, (double)w->pack.pop,
              (double)w->fire.ash, w->fire.state, (double)w->items[ITEM_WOOD].size, w->cairn.n, plan.cairn_start);
      }
      Rect r;
      const bool vis = appHikerBounds(a.app, &r);
      const char* beat = appBeatName(a.app);
      if (t == 320) CHECK(!w->fx.on && !vis, "seed %d: he is in the scene before the return", (int)seed);
      if (vis) {
        const bool corner = r.x1 > LS_QUIET_X0_T && r.y1 > LS_QUIET_Y0_T;
        const bool rows = r.y0 >= 0 && r.y1 <= SS_HEIGHT;
        // Wholly in the frame, but on his way in at the start (the return)
        // and in the majors (their own rule: test_scene, at least partly in).
        bool cols = r.x0 >= 0 && r.x1 <= SS_WIDTH;
        if (beatIs(a.app, "return") || isMajor(beat)) cols = r.x1 > 0 && r.x0 < SS_WIDTH;
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
        if (beatIs(a.app, "lunch") && k == CR_CHIPMUNK) chip_lunch = true;
        if (beatIs(a.app, "nap") && k == CR_CHIPMUNK) raid = true;
        const bool in_frame = cr.x1 > 0 && cr.x0 < SS_WIDTH && cr.y0 >= 0 && cr.y1 <= SS_HEIGHT;
        const bool corner = cr.x1 > LS_QUIET_X0_T && cr.y1 > LS_QUIET_Y0_T;
        if (!in_frame || corner) {
          if (kinds < 3) printf("seed %d tick %d (%s): critter kind %d rect %d,%d,%d,%d\n", (int)seed, t, beat, k, cr.x0,
                                cr.y0, cr.x1, cr.y1);
          ++kinds;
        }
      }
      if (beatIs(a.app, "lunch") || beatIs(a.app, "nap")) {
        for (const Bit& b : w->bits) {
          if (b.kind == BIT_BEE && w->spine.fbit >= 0 && b.pos.y > 0.f) bee = true;
          if (b.kind == BIT_MOTH && w->spine.fbit >= 0 && b.pos.y > 0.f) fly = true;
          if (b.kind == BIT_LEAF && w->spine.fbit >= 0 && w->spine.fstage > 0 && b.pos.y > 300.f) leaf = true;
        }
      }
      const Glyph& g = w->major.g[0];
      if (beatIs(a.app, "wade") && g.kind == GL_SHAPE && g.shape == SH_FISH) fish = true;
      if (beatIs(a.app, "map") && g.kind == GL_SHAPE && g.shape == SH_MAP && g.k > 0.9f) map = true;
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
        if (b >= 0 && b < 5) {
          CHECK(vis && r.x0 >= 0 && r.x1 <= SS_WIDTH, "seed %d: %s %d ends with the hiker out of frame", (int)seed,
                kDBeats[b], plan.fvar[b]);
          if (!rest_ok) {
            if (bad_rest < 5) printf("seed %d: %s %d ends with %s\n", (int)seed, kDBeats[b], plan.fvar[b], rest_why);
            ++bad_rest;
          }
        }
        snprintf(prev, sizeof prev, "%s", beat);
      }
      rest_ok = propsAtRest(w, rest_why, sizeof rest_why);
    }
    // One tick left (the afternoon's last): every beat but it has ended, and
    // it counts an unfinished script when its fade starts.
    const int over = appSpineOverruns(a.app);
    if (over) {
      char p[320];
      appLoopPlan(a.app, p, sizeof p);
      printf("seed %d (%s): %d beat(s) overran\n", (int)seed, p, over);
      ++overrun_loops;
    }
    char why[48];
    if (appCritterCount(a.app) != 0 || appFade(a.app) < 0.99f || !propsAtRest(w, why, sizeof why) ||
        w->hiker.target != POSE_SIT || w->items[ITEM_BOOK].down) {
      ++bad_end;
      printf("seed %d: at the end %d critters, fade %.3f, pose %d, book out %d, props %s\n", (int)seed,
             appCritterCount(a.app), (double)appFade(a.app), w->hiker.target, w->items[ITEM_BOOK].down,
             propsAtRest(w, why, sizeof why) ? "ok" : why);
    }
    const int reb = appPaletteRebuilds(a.app);
    reb_hi = reb > reb_hi ? reb : reb_hi;
    const bool rolled = appTick(a.app);
    CHECK(rolled, "seed %d: no rollover at the plan's length", (int)seed);
    a.stop();
  }
  int missing = 0;
  for (int b = 0; b < 6; ++b)
    for (int k = 0; k < kDVariants[b]; ++k)
      if (!seen[b][k]) {
        ++missing;
        printf("midday %s variant %d never ran in 40 seeds\n", kDBeats[b], k);
      }
  printf("midday: 40 loops, %.0f..%.0f s, palette rebuilds up to %d a loop, %d corner samples\n",
         (double)lo / SS_TICK_HZ, (double)hi / SS_TICK_HZ, reb_hi, corner_samples);
  CHECK(missing == 0, "%d midday variants unseen", missing);
  CHECK(overrun_loops == 0, "%d midday loops overran", overrun_loops);
  CHECK(bad_frame == 0, "%d hiker samples out of frame", bad_frame);
  CHECK(kinds == 0, "%d critter samples out of frame or in the corner", kinds);
  CHECK(bad_corner == 0, "%d corner pixels not ground or grass", bad_corner);
  CHECK(bad_rest == 0, "%d beats ended with props out of place", bad_rest);
  CHECK(bad_end == 0, "%d loops did not end empty, black and him sitting", bad_end);
  CHECK(len_bad == 0, "%d loops outside 480..660 s", len_bad);
  CHECK(bee, "the lunch a bee never showed");
  CHECK(chip_lunch, "the lunch b chipmunk never showed");
  CHECK(fish, "the wade a fish never showed");
  CHECK(raid, "the nap a chipmunk never showed");
  CHECK(leaf, "the nap b leaf never landed");
  CHECK(fly, "the nap c fly never showed");
  CHECK(map, "the map never opened");
}

// Incremental rendering (every tick) equals a full render at checkpoints
// through whole midday loops: the camp popping in, the hat and boot in his
// hand, the fish, the map, the bits. Strips per frame on the way.
static void testMiddayIncremental() {
  static const uint64_t kSeeds[4] = {1, 2, 3, 6};
  long sum = 0, frames = 0;
  int worst = 0, diffs = 0, checks = 0;
  for (uint64_t seed : kSeeds) {
    const Inputs in = makeInputs(3.f, kForceMidday);
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
  printf("midday strips per frame (4 loops, build excluded): avg %.2f, max %d; %d full-render checks\n",
         frames ? (double)sum / (double)frames : 0.0, worst, checks);
  CHECK(diffs == 0, "%d checkpoints where incremental and full renders differ", diffs);
}

// Families in turn on one App (the inputs change between loops, as the
// host's clock moves them): midday after evening, after morning and after
// midday. No overrun, no critter left, every loop in the length range.
static void testMixedLoops() {
  int bad = 0, n[SF_COUNT] = {};
  // The next loop's: midday, midday, evening, midday, morning, midday.
  static const float kMood[6] = {3.f, 3.f, 4.f, 3.f, 1.5f, 3.f};
  static const int kForce[6] = {kForceMidday, kForceMidday, 0, kForceMidday, 0, kForceMidday};
  for (uint64_t seed = 31; seed <= 33; ++seed) {
    Rig a;
    Inputs in = makeInputs(3.f, kForceMidday);
    CHECK(a.start(seed, in), "start");
    for (int loop = 0; loop < 12; ++loop) {
      const int fam = appSpineFamily(a.app);
      if (fam >= 0 && fam < SF_COUNT) ++n[fam];
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
      in.mood = kMood[loop % 6];
      in.spine_force = (uint8_t)kForce[loop % 6];
      appSetInputs(a.app, in);
      CHECK(appTick(a.app), "seed %d loop %d: no rollover at the plan's length", (int)seed, loop);
    }
    a.stop();
  }
  printf("mixed families: %d evening, %d morning and %d midday loops in turn\n", n[SF_EVENING], n[SF_MORNING],
         n[SF_MIDDAY]);
  CHECK(bad == 0, "%d mixed loops overran, left critters or missed the length range", bad);
  CHECK(n[SF_MIDDAY] >= 18 && n[SF_EVENING] >= 6 && n[SF_MORNING] >= 6, "families did not alternate");
}

static void testAppSize() {
  printf("sizeof(App) = %u bytes (budget %d)\n", (unsigned)appAppBytes(), SS_APP_BYTES);
  CHECK(appAppBytes() <= (uint32_t)SS_APP_BYTES, "App is %u bytes", (unsigned)appAppBytes());
}

int main() {
  testFamilyPick();
  testRegression();
  testMiddayVariants();
  testMiddayIncremental();
  testMixedLoops();
  testAppSize();
  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all midday tests passed\n");
  return 0;
}
