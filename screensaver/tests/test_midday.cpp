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
#include "scene/stage22_switch.h"
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
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};   // no fast block lent: core takes one

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
    "loop=0 seed=1 length=567s plan=arrival=2 setup=1 firewood=1 fire=0 dinner=0 foodbag=0 bear=1 close=0 fillers=cloud_watch,stream_listen majors=pinecone,fish_leap memory=loop=1 cairn=0 shoe=1 stolen=shoe hole=0 rod=0 brave=chipmunk,frog,deer,bear,mouse,dog ring=pinecone,fish_leap",
    "loop=1 seed=3851155901 length=616s plan=arrival=1 setup=1 firewood=0 fire=0 dinner=1 foodbag=2 bear=0 close=0 fillers=stream_listen,mug_sunset majors=map_chase,ants_cookie,echo memory=loop=2 cairn=0 shoe=1 stolen=mug hole=0 rod=0 brave=chipmunk,frog,deer,bear,mouse,dog ring=pinecone,fish_leap,map_chase,ants_cookie,echo",
    "loop=2 seed=1714365033 length=650s plan=arrival=1 setup=1 firewood=2 fire=0 dinner=2 foodbag=1 bear=1 close=2 fillers=skip_stone,cairn majors=raccoon_pack,deer_stick,pinecone,shadow_theater memory=loop=3 cairn=4 shoe=0 stolen=mug hole=1 rod=0 brave=frog,owl,bear,mouse ring=fish_leap,map_chase,ants_cookie,echo,raccoon_pack,deer_stick,pinecone,shadow_theater",
    "loop=3 seed=594093305 length=628s plan=arrival=1 setup=1 firewood=1 fire=2 dinner=2 foodbag=1 bear=2 close=2 fillers=read,cloud_watch majors=ranger_ridge,bathe,raccoon_pack memory=loop=4 cairn=3 shoe=1 stolen=shoe hole=0 rod=0 brave=frog,owl,bear,mouse ring=echo,raccoon_pack,deer_stick,pinecone,shadow_theater,ranger_ridge,bathe,raccoon_pack",
    "loop=4 seed=2072051055 length=615s plan=arrival=2 setup=2 firewood=1 fire=2 dinner=0 foodbag=0 bear=2 close=0 fillers=cloud_watch,birds_on_water majors=ants_cookie,deer_stick,map_chase memory=loop=5 cairn=2 shoe=1 stolen=mug hole=1 rod=0 brave=squirrel,frog,deer,owl,bird,mouse,dog ring=pinecone,shadow_theater,ranger_ridge,bathe,raccoon_pack,ants_cookie,deer_stick,map_chase",
    "loop=5 seed=326364185 length=628s plan=arrival=2 setup=0 firewood=2 fire=1 dinner=2 foodbag=0 bear=2 close=1 fillers=mug_sunset,stream_listen majors=fish_leap,shadow_theater memory=loop=6 cairn=1 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,frog,deer,owl,bird,mouse,dog ring=ranger_ridge,bathe,raccoon_pack,ants_cookie,deer_stick,map_chase,fish_leap,shadow_theater",
    "loop=6 seed=2408721995 length=650s plan=arrival=2 setup=1 firewood=0 fire=1 dinner=0 foodbag=1 bear=2 close=2 fillers=cloud_watch,stream_listen majors=deer_stick,pinecone,echo memory=loop=7 cairn=0 shoe=1 stolen=mug hole=1 rod=0 brave=frog,owl,bird,bear,mouse,dog ring=ants_cookie,deer_stick,map_chase,fish_leap,shadow_theater,deer_stick,pinecone,echo",
    "loop=7 seed=781737917 length=599s plan=arrival=2 setup=1 firewood=2 fire=0 dinner=1 foodbag=2 bear=2 close=0 fillers=cloud_watch majors=ants_cookie,ranger_ridge,raccoon_pack memory=loop=8 cairn=0 shoe=0 stolen=mug hole=0 rod=0 brave=frog,owl,bird,bear,mouse,dog ring=fish_leap,shadow_theater,deer_stick,pinecone,echo,ants_cookie,ranger_ridge,raccoon_pack",
    "loop=8 seed=1261543552 length=650s plan=arrival=2 setup=1 firewood=2 fire=1 dinner=2 foodbag=0 bear=1 close=2 fillers=cloud_watch,skip_stone,read majors=map_chase,fish_leap,raccoon_pack memory=loop=9 cairn=0 shoe=1 stolen=shoe hole=1 rod=0 brave=chipmunk,squirrel,owl,bear ring=pinecone,echo,ants_cookie,ranger_ridge,raccoon_pack,map_chase,fish_leap,raccoon_pack",
    "loop=9 seed=2630882420 length=640s plan=arrival=0 setup=1 firewood=2 fire=2 dinner=0 foodbag=2 bear=0 close=0 fillers=skip_stone,mug_sunset majors=echo,pinecone,shadow_theater memory=loop=10 cairn=0 shoe=1 stolen=mug hole=0 rod=0 brave=chipmunk,squirrel,owl,bear ring=ranger_ridge,raccoon_pack,map_chase,fish_leap,raccoon_pack,echo,pinecone,shadow_theater",
};
static const char* const kEvening7[10] = {
    "loop=0 seed=7 length=622s plan=arrival=0 setup=1 firewood=1 fire=2 dinner=2 foodbag=2 bear=2 close=2 fillers=cloud_watch,cairn majors=deer_stick,shadow_theater memory=loop=1 cairn=5 shoe=1 stolen=shoe hole=0 rod=0 brave=chipmunk,squirrel,frog,raccoon,bear ring=deer_stick,shadow_theater",
    "loop=1 seed=2356717238 length=632s plan=arrival=1 setup=1 firewood=2 fire=1 dinner=1 foodbag=2 bear=2 close=1 fillers=mug_sunset majors=map_chase,raccoon_pack,bathe memory=loop=2 cairn=4 shoe=0 stolen=mug hole=0 rod=0 brave=chipmunk,squirrel,frog,raccoon,bear ring=deer_stick,shadow_theater,map_chase,raccoon_pack,bathe",
    "loop=2 seed=1817722286 length=650s plan=arrival=2 setup=1 firewood=1 fire=1 dinner=2 foodbag=2 bear=0 close=2 fillers=mug_sunset,cairn majors=fish_leap,ants_cookie,pinecone,raccoon_pack memory=loop=3 cairn=7 shoe=1 stolen=shoe hole=0 rod=0 brave=squirrel,frog,deer,bear,mouse ring=shadow_theater,map_chase,raccoon_pack,bathe,fish_leap,ants_cookie,pinecone,raccoon_pack",
    "loop=3 seed=3923558491 length=613s plan=arrival=1 setup=0 firewood=2 fire=2 dinner=0 foodbag=0 bear=0 close=1 fillers=cloud_watch,birds_on_water majors=echo,map_chase,shadow_theater memory=loop=4 cairn=6 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,frog,deer,bear,mouse ring=bathe,fish_leap,ants_cookie,pinecone,raccoon_pack,echo,map_chase,shadow_theater",
    "loop=4 seed=3055961913 length=650s plan=arrival=1 setup=0 firewood=1 fire=1 dinner=0 foodbag=1 bear=0 close=0 fillers=mug_sunset,birds_on_water,skip_stone,stream_listen majors=ranger_ridge,deer_stick memory=loop=5 cairn=5 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,bird ring=ants_cookie,pinecone,raccoon_pack,echo,map_chase,shadow_theater,ranger_ridge,deer_stick",
    "loop=5 seed=3078075808 length=611s plan=arrival=2 setup=2 firewood=2 fire=2 dinner=0 foodbag=0 bear=1 close=0 fillers=cloud_watch,skip_stone majors=pinecone,ants_cookie,fish_leap memory=loop=6 cairn=4 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,bird ring=echo,map_chase,shadow_theater,ranger_ridge,deer_stick,pinecone,ants_cookie,fish_leap",
    "loop=6 seed=1481336605 length=624s plan=arrival=0 setup=0 firewood=2 fire=0 dinner=1 foodbag=1 bear=1 close=1 fillers=birds_on_water majors=echo,map_chase,raccoon_pack,shadow_theater memory=loop=7 cairn=3 shoe=0 stolen=mug hole=0 rod=0 brave=squirrel,raccoon,bear ring=deer_stick,pinecone,ants_cookie,fish_leap,echo,map_chase,raccoon_pack,shadow_theater",
    "loop=7 seed=3625058471 length=650s plan=arrival=0 setup=2 firewood=1 fire=2 dinner=2 foodbag=2 bear=1 close=0 fillers=cloud_watch,mug_sunset,cairn,skip_stone majors=bathe,ranger_ridge memory=loop=8 cairn=7 shoe=1 stolen=shoe hole=1 rod=0 brave=squirrel,raccoon,bear ring=ants_cookie,fish_leap,echo,map_chase,raccoon_pack,shadow_theater,bathe,ranger_ridge",
    "loop=8 seed=3169107222 length=650s plan=arrival=1 setup=1 firewood=2 fire=0 dinner=0 foodbag=2 bear=2 close=1 fillers=read,cairn,skip_stone majors=pinecone,deer_stick,fish_leap memory=loop=9 cairn=7 shoe=1 stolen=mug hole=0 rod=0 brave=chipmunk,frog,mouse ring=map_chase,raccoon_pack,shadow_theater,bathe,ranger_ridge,pinecone,deer_stick,fish_leap",
    "loop=9 seed=1914874671 length=620s plan=arrival=1 setup=2 firewood=0 fire=1 dinner=1 foodbag=0 bear=0 close=1 fillers=stream_listen majors=echo,ranger_ridge,map_chase,shadow_theater memory=loop=10 cairn=6 shoe=1 stolen=mug hole=0 rod=0 brave=chipmunk,frog,mouse ring=ranger_ridge,pinecone,deer_stick,fish_leap,echo,ranger_ridge,map_chase,shadow_theater",
};
// ss_preview --loops 10 --seed 1 --mood 1.5 --spine morning. Captured when the midday went in (2026-10-31) and
// re-captured when the camp-stays morning (morning_stay.cpp) went in (2026-11-02): from then a loop is the stay
// (stay=1 in the plan) 60 % of the time, and all ten of seed 1's are. Mark's break camp is pinned by
// test_morning_stay.cpp (the first loops of eight seeds, byte-identical to the tree before the stay). A later
// change to the morning moves these lines: re-capture them then.
static const char* const kMorning1[10] = {
    "loop=0 seed=1 length=630s plan=family=morning stay=1 wake=0 coffee=2 pee=0 breakfast=0 wash=0 tidy=0 day_hike=0 fillers=stream_listen majors=fishing_small,deer_stick,fish_leap fog=12 memory=loop=1 cairn=0 shoe=1 stolen=shoe hole=0 rod=1 brave=chipmunk,frog,deer,mouse,dog ring=fishing_small,deer_stick,fish_leap",
    "loop=1 seed=2542665771 length=613s plan=family=morning stay=1 wake=2 coffee=0 pee=0 breakfast=2 wash=0 tidy=1 day_hike=0 fillers=birds_on_water,cairn majors=ants_cookie,ranger_ridge fog=15 memory=loop=2 cairn=4 shoe=1 stolen=mug hole=0 rod=1 brave=chipmunk,frog,deer,mouse,dog ring=fishing_small,deer_stick,fish_leap,ants_cookie,ranger_ridge",
    "loop=2 seed=4185136349 length=639s plan=family=morning stay=1 wake=2 coffee=0 pee=0 breakfast=1 wash=0 tidy=0 day_hike=0 fillers=cloud_watch majors=map_chase,deer_stick,bathe fog=12 memory=loop=3 cairn=3 shoe=1 stolen=mug hole=0 rod=1 brave=deer,raccoon,owl,bird ring=fishing_small,deer_stick,fish_leap,ants_cookie,ranger_ridge,map_chase,deer_stick,bathe",
    "loop=3 seed=1763470111 length=642s plan=family=morning stay=1 wake=0 coffee=2 pee=0 breakfast=2 wash=0 tidy=0 day_hike=0 fillers=cairn,read majors=fishing_log,pinecone fog=11 memory=loop=4 cairn=6 shoe=1 stolen=mug hole=0 rod=1 brave=deer,raccoon,owl,bird ring=fish_leap,ants_cookie,ranger_ridge,map_chase,deer_stick,bathe,fishing_log,pinecone",
    "loop=4 seed=3074750227 length=627s plan=family=morning wake=0 coffee=1 bag_down=1 break_camp=1 douse=1 leave=0 fillers=read,birds_on_water majors=fish_leap,fishing_small,ants_cookie fog=14 memory=loop=5 cairn=5 shoe=1 stolen=mug hole=0 rod=1 brave=chipmunk,frog,raccoon,owl,bird,bear,mouse,dog ring=map_chase,deer_stick,bathe,fishing_log,pinecone,fish_leap,fishing_small,ants_cookie",
    "loop=5 seed=1909220697 length=650s plan=family=morning wake=1 coffee=0 bag_down=0 break_camp=2 douse=0 leave=2 fillers=cairn,cloud_watch,birds_on_water majors=squirrel_splash,echo,bathe fog=11 memory=loop=6 cairn=7 shoe=1 stolen=mug hole=0 rod=1 brave=chipmunk,frog,raccoon,owl,bird,bear,mouse,dog ring=fishing_log,pinecone,fish_leap,fishing_small,ants_cookie,squirrel_splash,echo,bathe",
    "loop=6 seed=2607530289 length=617s plan=family=morning stay=1 wake=2 coffee=1 pee=0 breakfast=0 wash=0 tidy=0 day_hike=0 fillers=cloud_watch majors=deer_stick,ants_cookie,pinecone fog=13 memory=loop=7 cairn=6 shoe=1 stolen=mug hole=0 rod=1 brave=raccoon,owl,bird,dog ring=fishing_small,ants_cookie,squirrel_splash,echo,bathe,deer_stick,ants_cookie,pinecone",
    "loop=7 seed=2335055262 length=649s plan=family=morning stay=1 wake=0 coffee=0 pee=0 breakfast=2 wash=0 tidy=1 day_hike=0 fillers=cloud_watch majors=ranger_ridge,fish_leap,fishing_log fog=14 memory=loop=8 cairn=5 shoe=1 stolen=mug hole=0 rod=1 brave=raccoon,owl,bird,dog ring=echo,bathe,deer_stick,ants_cookie,pinecone,ranger_ridge,fish_leap,fishing_log",
    "loop=8 seed=3056835256 length=650s plan=family=morning stay=1 wake=2 coffee=1 pee=0 breakfast=2 wash=0 tidy=0 day_hike=0 fillers=cairn,stream_listen majors=squirrel_splash,echo,map_chase fog=13 memory=loop=9 cairn=7 shoe=1 stolen=mug hole=0 rod=1 brave=frog,raccoon,owl,bird,mouse,dog ring=ants_cookie,pinecone,ranger_ridge,fish_leap,fishing_log,squirrel_splash,echo,map_chase",
    "loop=9 seed=2195922103 length=630s plan=family=morning stay=1 wake=2 coffee=2 pee=0 breakfast=1 wash=0 tidy=1 day_hike=0 fillers=read majors=pinecone,deer_stick,bathe fog=13 memory=loop=10 cairn=6 shoe=1 stolen=mug hole=0 rod=1 brave=frog,raccoon,owl,bird,mouse,dog ring=fish_leap,fishing_log,squirrel_splash,echo,map_chase,pinecone,deer_stick,bathe",
};
// The midday's own (ss_preview --loops 10 --seed 1 --mood 3 --spine midday), pinned when it was built and
// re-captured when the kite and the boomerang went in (2026-11-02) and again when the mountain bike, the archery, the
// slackline and the ropes course did (2026-11-03) and again when the plinking did (2026-11-17): the evening's and the
// morning's lines above did not move any time (checked with the new majors at weight 0, and again at their weights:
// byte-identical to the tree without them; with the plinking the first line to move is loop 2, where plink takes the ropes
// course's place, and the later loops follow through the memory's ring).
static const char* const kMidday1[10] = {
    "loop=0 seed=1 length=638s plan=family=midday return=2 lunch=0 wade=1 nap=0 map=0 afternoon=0 fillers=cloud_watch,stream_listen,cairn majors=fish_leap,map_chase,fetch memory=loop=1 cairn=6 shoe=0 stolen=none hole=1 rod=0 brave=squirrel,frog,bird,mouse ring=fish_leap,map_chase,fetch",
    "loop=1 seed=3851155901 length=619s plan=family=midday return=1 lunch=1 wade=0 nap=1 map=1 afternoon=1 fillers=cairn,cloud_watch,skip_stone majors=fishing_log,ranger_ridge,pinecone memory=loop=2 cairn=7 shoe=0 stolen=none hole=1 rod=1 brave=squirrel,frog,bird,mouse ring=fish_leap,map_chase,fetch,fishing_log,ranger_ridge,pinecone",
    "loop=2 seed=3700665890 length=596s plan=family=midday return=2 lunch=1 wade=2 nap=0 map=1 afternoon=0 fillers=stream_listen,cairn majors=ants_cookie,plink memory=loop=3 cairn=7 shoe=0 stolen=none hole=1 rod=1 brave=chipmunk,squirrel,frog,deer,raccoon,owl,bird,bear ring=fish_leap,map_chase,fetch,fishing_log,ranger_ridge,pinecone,ants_cookie,plink",
    "loop=3 seed=750640662 length=650s plan=family=midday return=1 lunch=0 wade=1 nap=2 map=0 afternoon=1 fillers=birds_on_water,skip_stone majors=archery,deer_stick,ropes memory=loop=4 cairn=6 shoe=0 stolen=none hole=1 rod=1 brave=chipmunk,squirrel,frog,deer,raccoon,owl,bird,bear ring=fishing_log,ranger_ridge,pinecone,ants_cookie,plink,archery,deer_stick,ropes",
    "loop=4 seed=1791658589 length=650s plan=family=midday return=2 lunch=1 wade=0 nap=0 map=0 afternoon=0 fillers=skip_stone,cairn,read majors=echo,slackline,boomerang memory=loop=5 cairn=7 shoe=0 stolen=none hole=1 rod=1 brave=squirrel,frog,deer,raccoon,mouse,dog ring=ants_cookie,plink,archery,deer_stick,ropes,echo,slackline,boomerang",
    "loop=5 seed=690833878 length=603s plan=family=midday return=0 lunch=0 wade=1 nap=2 map=1 afternoon=1 fillers=birds_on_water,read majors=fetch,ranger_ridge,kite memory=loop=6 cairn=6 shoe=0 stolen=none hole=1 rod=1 brave=squirrel,frog,deer,raccoon,mouse,dog ring=deer_stick,ropes,echo,slackline,boomerang,fetch,ranger_ridge,kite",
    "loop=6 seed=3739516147 length=633s plan=family=midday return=1 lunch=1 wade=0 nap=0 map=0 afternoon=0 fillers=birds_on_water,skip_stone majors=plink,ropes memory=loop=7 cairn=5 shoe=0 stolen=none hole=1 rod=1 brave=chipmunk,squirrel,deer,raccoon,owl,mouse,dog ring=echo,slackline,boomerang,fetch,ranger_ridge,kite,plink,ropes",
    "loop=7 seed=1920143497 length=610s plan=family=midday return=0 lunch=0 wade=1 nap=2 map=0 afternoon=1 fillers=cairn,skip_stone,birds_on_water majors=archery,ants_cookie memory=loop=8 cairn=7 shoe=0 stolen=none hole=1 rod=1 brave=chipmunk,squirrel,deer,raccoon,owl,mouse,dog ring=boomerang,fetch,ranger_ridge,kite,plink,ropes,archery,ants_cookie",
    "loop=8 seed=4122961066 length=614s plan=family=midday return=0 lunch=1 wade=2 nap=0 map=0 afternoon=0 fillers=birds_on_water,read majors=fish_leap,fishing_small,bathe memory=loop=9 cairn=6 shoe=0 stolen=none hole=1 rod=1 brave=squirrel,deer,owl,bear,mouse ring=kite,plink,ropes,archery,ants_cookie,fish_leap,fishing_small,bathe",
    "loop=9 seed=3578621840 length=650s plan=family=midday return=2 lunch=0 wade=1 nap=2 map=1 afternoon=1 fillers=birds_on_water,read,cloud_watch majors=echo,pinecone,slackline memory=loop=10 cairn=5 shoe=0 stolen=none hole=1 rod=1 brave=squirrel,deer,owl,bear,mouse ring=archery,ants_cookie,fish_leap,fishing_small,bathe,echo,pinecone,slackline",
};

// Stage 22 went live (step C7, 2026-11-19): with SS_S22 on (the default) the same two runs, re-captured once. Read
// line by line against the lines above before they were accepted: the morning's loop 0 is the day's first morning
// loop, so tai chi is forced and plays alone after the wake; from loop 1 on the loop seeds differ (a forced pick ends
// the plan with another loop rng state, as after a forced fetch) and the later loops follow; tai chi comes again in
// loop 6 by its weight. The midday's loops 0 to 2 keep their seeds: the long shot's weight in the pool moves which
// major a draw lands on (kite for fetch in loop 0; fetch and echo in loop 1), loop 2 plays the long shot where the
// plinking was (the hillside rule), loop 3 is a deer visit (" herd=", two majors) and the later loops follow through
// the loop seeds and the memory's ring. With SS_S22 off the lines above still hold, byte for byte.
// 2026-11-25, the frog he licks (frog_lick, a midday pool row of weight 1.0): the midday S22 lines re-captured, the
// morning's untouched (the frog weighs 0 there, and Rng::weighted skips a zero weight with the same draw). Loops 0 to 3
// keep their seeds: the new row moves where the plan's weighted draws land (loop 0 map_chase, fish_leap, kite in that
// order and a shorter fit, 594 s; loop 1 ants_cookie for pinecone; loop 2 ranger_ridge and frog_lick for ants_cookie
// and the long shot; loop 3 the same deer visit and majors, 627 s with the new ring). From loop 4 the loop seeds
// differ (loop 3's length changed the distant bird's draws, and with them the loop rng's last draw) and the later
// loops follow. With SS_S22 off kMidday1 still holds, byte for byte.
static const char* const kMorning1S22[10] = {
    "loop=0 seed=1 length=614s plan=family=morning stay=1 wake=0 coffee=2 pee=0 breakfast=0 wash=0 tidy=0 day_hike=0 fillers=stream_listen majors=taichi fog=12 memory=loop=1 cairn=0 shoe=1 stolen=shoe hole=0 rod=0 brave=chipmunk,squirrel,raccoon,owl,bear,dog ring=taichi",
    "loop=1 seed=545980955 length=632s plan=family=morning wake=2 coffee=0 bag_down=1 break_camp=1 douse=1 leave=1 fillers=stream_listen,read,cairn majors=deer_stick,ranger_ridge,echo fog=14 memory=loop=2 cairn=6 shoe=1 stolen=shoe hole=0 rod=0 brave=chipmunk,squirrel,raccoon,owl,bear,dog ring=taichi,deer_stick,ranger_ridge,echo",
    "loop=2 seed=4143389021 length=618s plan=family=morning stay=1 wake=2 coffee=0 pee=0 breakfast=0 wash=0 tidy=1 day_hike=0 fillers=birds_on_water majors=ants_cookie,fish_leap,map_chase fog=12 memory=loop=3 cairn=5 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,raccoon,bird,dog ring=taichi,deer_stick,ranger_ridge,echo,ants_cookie,fish_leap,map_chase",
    "loop=3 seed=3596995242 length=602s plan=family=morning stay=1 wake=1 coffee=1 pee=0 breakfast=1 wash=0 tidy=0 day_hike=0 fillers=cloud_watch,cairn majors=ranger_ridge,pinecone fog=12 memory=loop=4 cairn=7 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,raccoon,bird,dog ring=deer_stick,ranger_ridge,echo,ants_cookie,fish_leap,map_chase,ranger_ridge,pinecone",
    "loop=4 seed=1935303903 length=650s plan=family=morning stay=1 wake=2 coffee=0 pee=0 breakfast=2 wash=0 tidy=1 day_hike=0 fillers=cairn,cloud_watch majors=deer_stick,echo,fish_leap fog=13 memory=loop=5 cairn=7 shoe=1 stolen=mug hole=0 rod=0 brave=frog,deer,bird,bear,mouse ring=ants_cookie,fish_leap,map_chase,ranger_ridge,pinecone,deer_stick,echo,fish_leap",
    "loop=5 seed=2880643625 length=621s plan=family=morning stay=1 wake=1 coffee=1 pee=0 breakfast=1 wash=0 tidy=0 day_hike=0 fillers=read majors=taichi fog=11 memory=loop=6 cairn=6 shoe=1 stolen=mug hole=0 rod=0 brave=frog,deer,bird,bear,mouse ring=fish_leap,map_chase,ranger_ridge,pinecone,deer_stick,echo,fish_leap,taichi",
    "loop=6 seed=908732574 length=644s plan=family=morning stay=1 wake=1 coffee=2 pee=0 breakfast=2 wash=0 tidy=1 day_hike=0 fillers=cloud_watch majors=ants_cookie,ranger_ridge,bathe fog=16 memory=loop=7 cairn=5 shoe=1 stolen=mug hole=0 rod=0 brave=chipmunk,squirrel,frog,deer,raccoon,mouse ring=pinecone,deer_stick,echo,fish_leap,taichi,ants_cookie,ranger_ridge,bathe",
    "loop=7 seed=3485884071 length=602s plan=family=morning stay=1 wake=2 coffee=0 pee=0 breakfast=0 wash=0 tidy=0 day_hike=0 fillers=birds_on_water majors=taichi fog=16 memory=loop=8 cairn=4 shoe=1 stolen=mug hole=0 rod=0 brave=chipmunk,squirrel,frog,deer,raccoon,mouse ring=deer_stick,echo,fish_leap,taichi,ants_cookie,ranger_ridge,bathe,taichi",
    "loop=8 seed=1727479985 length=648s plan=family=morning stay=1 wake=2 coffee=3 pee=0 breakfast=2 wash=0 tidy=1 day_hike=0 fillers=birds_on_water,cairn majors=map_chase,echo,pinecone fog=12 memory=loop=9 cairn=7 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,deer,bird,bear,dog ring=taichi,ants_cookie,ranger_ridge,bathe,taichi,map_chase,echo,pinecone",
    "loop=9 seed=1943570874 length=650s plan=family=morning stay=1 wake=0 coffee=2 pee=0 breakfast=0 wash=0 tidy=0 day_hike=0 fillers=birds_on_water,cairn majors=deer_stick,fish_leap,ants_cookie fog=16 memory=loop=10 cairn=7 shoe=1 stolen=mug hole=0 rod=0 brave=squirrel,deer,bird,bear,dog ring=bathe,taichi,map_chase,echo,pinecone,deer_stick,fish_leap,ants_cookie",
};
static const char* const kMidday1S22[10] = {
    "loop=0 seed=1 length=636s plan=family=midday return=2 lunch=0 wade=1 nap=0 map=0 afternoon=0 fillers=cloud_watch,stream_listen,cairn majors=ants_cookie,fish_leap,boomerang memory=loop=1 cairn=6 shoe=0 stolen=none hole=0 rod=0 brave=squirrel,frog,bird,mouse ring=ants_cookie,fish_leap,boomerang",
    "loop=1 seed=3851155901 length=629s plan=family=midday return=1 lunch=1 wade=0 nap=1 map=1 afternoon=1 fillers=cairn,cloud_watch,skip_stone majors=kite,echo,map_chase memory=loop=2 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=squirrel,frog,bird,mouse ring=ants_cookie,fish_leap,boomerang,kite,echo,map_chase",
    "loop=2 seed=3700665890 length=642s plan=family=midday return=2 lunch=1 wade=2 nap=0 map=1 afternoon=0 fillers=stream_listen,cairn majors=ranger_ridge,frog_lick memory=loop=3 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,squirrel,frog,deer,raccoon,owl,bird,bear ring=ants_cookie,fish_leap,boomerang,kite,echo,map_chase,ranger_ridge,frog_lick",
    "loop=3 seed=1306309954 length=633s plan=family=midday return=0 lunch=0 wade=1 nap=1 map=0 afternoon=1 fillers=cloud_watch,read majors=ropes,pinecone,boomerang memory=loop=4 cairn=6 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,squirrel,frog,deer,raccoon,owl,bird,bear ring=kite,echo,map_chase,ranger_ridge,frog_lick,ropes,pinecone,boomerang",
    "loop=4 seed=353847738 length=599s plan=family=midday return=1 lunch=1 wade=1 nap=2 map=1 afternoon=1 fillers=read,cairn,skip_stone majors=fish_leap,bathe memory=loop=5 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=squirrel,frog,deer,owl,mouse ring=map_chase,ranger_ridge,frog_lick,ropes,pinecone,boomerang,fish_leap,bathe",
    "loop=5 seed=3740524944 length=642s plan=family=midday return=0 lunch=0 wade=0 nap=1 map=0 afternoon=0 fillers=birds_on_water,read majors=slackline,archery,fetch memory=loop=6 cairn=6 shoe=0 stolen=none hole=0 rod=0 brave=squirrel,frog,deer,owl,mouse ring=ropes,pinecone,boomerang,fish_leap,bathe,slackline,archery,fetch",
    "loop=6 seed=3824192997 length=643s plan=family=midday return=1 lunch=1 wade=2 nap=2 map=1 afternoon=1 herd=5 fillers=cairn,read majors=kite,ranger_ridge memory=loop=7 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,deer,raccoon,bird,dog ring=boomerang,fish_leap,bathe,slackline,archery,fetch,kite,ranger_ridge",
    "loop=7 seed=2084183280 length=647s plan=family=midday return=1 lunch=1 wade=0 nap=1 map=0 afternoon=0 fillers=read,skip_stone,cairn majors=echo,ants_cookie,slackline memory=loop=8 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,deer,raccoon,bird,dog ring=slackline,archery,fetch,kite,ranger_ridge,echo,ants_cookie,slackline",
    "loop=8 seed=2081981123 length=608s plan=family=midday return=1 lunch=0 wade=2 nap=0 map=1 afternoon=1 fillers=read majors=deer_stick,longshot,map_chase memory=loop=9 cairn=6 shoe=0 stolen=none hole=0 rod=0 brave=frog,deer,owl,dog ring=kite,ranger_ridge,echo,ants_cookie,slackline,deer_stick,longshot,map_chase",
    "loop=9 seed=3646442602 length=561s plan=family=midday return=0 lunch=1 wade=0 nap=1 map=0 afternoon=1 fillers=cloud_watch majors=frog_lick memory=loop=10 cairn=5 shoe=0 stolen=none hole=0 rod=0 brave=frog,deer,owl,dog ring=ranger_ridge,echo,ants_cookie,slackline,deer_stick,longshot,map_chase,frog_lick",
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
  // Stage 22 (INTEGRATION_PLAN 6.1; Mike, 2026-11-18): the new pieces change the morning's and the midday's plans,
  // so those two have a pin set each way (step C7 re-captured the SS_S22 ones). The evening's never move: the same
  // pins either way.
  const bool s22 = ssS22Switch() != 0;
  const int m1 = compareLines("morning", 1, makeInputs(1.5f, kForceMorning), "", s22 ? kMorning1S22 : kMorning1);
  const int d1 = compareLines("midday", 1, makeInputs(3.f, kForceMidday), "", s22 ? kMidday1S22 : kMidday1);
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
                                       "bathe",           "fetch",         "kite",        "boomerang",
                                       "bike",            "archery",       "slackline",   "ropes",         "plink",
                                       "taichi",          "longshot",      "mushroom",     "frog_lick"};
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
      // The twin renders every tick too: the trip's sky (the mushroom's, and the frog's since 2026-11-25, which seed 6's
      // first loop plays) latches its cache bands per render (REVIEW_FIXES F28), so a twin drawn only at the checks
      // would show an older sky. At a check it is invalidated and drawn whole.
      const bool check = t % 397 == 0 || t == 260 || t == 300;
      if (check) appInvalidate(b.app);
      appRender(b.app, b.surf);
      if (check) {
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
