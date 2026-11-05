// The morning where the camp stays up (core/scene/morning_stay.cpp; MORNING_DAYTIME_PLAN.md section 4).
//
// The variant split over many seeds (about 60 % stay, PROPOSED) and Mark's break camp unchanged when it is
// picked (the evening, midday and morning pins live in test_spines.cpp and test_midday.cpp); every beat and
// variant plays over stay loops with no overrun, the hiker in frame (but on his walk out at the end), the
// reserved corner clean, every critter in frame and gone at the end, the loop 480 to 660 s; each pancake
// outcome (back in the pan, on his hat, taken by a bird) and the boil-over occur; the raccoon's raid takes
// the shoe, or the mug (the night thief's rule) once the shoe is gone, he is out of sight 12 s or more
// behind the big pine, and he comes back; the camp still stands when the loop ends; the doorbell reaction
// freezes every part of it and the loop ends the same, later by the ticks it froze; incremental rendering
// equals a full render through stay loops; sizeof(App).
// Plain executable; returns non-zero on failure.
#include "ss_app.h"
#include "scene/spine_family.h"
#include "scene/world.h"

#include <math.h>
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
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

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

static Inputs makeInputs(float mood, int force) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.spine_force = (uint8_t)force;
  in.have_outside = true;   // clear weather from "data"
  return in;
}
static const int kForceMorning = 1 + SF_MORNING;

static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }

static bool isMajor(const char* name) {
  static const char* const kMajor[] = {"squirrel_splash", "deer_stick",    "pinecone",       "map_chase",    "fish_leap",
                                       "raccoon_pack",    "ants_cookie",   "shadow_theater", "ranger_ridge", "echo",
                                       "firefly_jar",     "stargaze",      "fishing_small",  "fishing_log",  "fishing_good",
                                       "bathe",           "fetch",         "kite",           "boomerang",    "rain_shelter"};
  for (const char* k : kMajor)
    if (strcmp(name, k) == 0) return true;
  return false;
}

static const char* const kBeats[SY_COUNT] = {"wake", "coffee", "pee", "breakfast", "wash", "tidy", "day_hike"};
static const int kVariants[SY_COUNT] = {3, 4, 1, 3, 1, 2, 1};

static int beatIndex(const char* name) {
  for (int i = 0; i < SY_COUNT; ++i)
    if (strcmp(name, kBeats[i]) == 0) return i;
  return -1;
}

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
        if (shown + bad < 5) printf("stay seed %d tick %d: corner pixel (%d,%d) = %04x\n", seed, tick, x, y, p);
        ++bad;
      }
    }
  }
  return bad;
}

// ---- Mark's break camp, when picked, is what it was ------------------------------------------------

// The first loop (`ss_preview --loops 1 --seed S --mood 1.5 --spine morning`, less the weather token) of eight
// seeds whose pick is Mark's morning, captured from the tree before the stay went in (2026-11-02): plan, loop
// seed, length and the memory after it. Byte for byte what the stay's pick leaves them. (After a stay loop
// the memory and the loop's length differ, so later loops can not be compared with it: the loop rng itself
// is untouched, but the distant bird's draws depend on the loop's length.)
static const uint64_t kMarkSeeds[8] = {2, 3, 4, 5, 8, 10, 11, 14};
static const char* const kMark[8] = {
    "loop=0 seed=2 length=602s plan=family=morning wake=0 coffee=2 bag_down=0 break_camp=0 douse=1 leave=2 fillers=birds_on_water,cairn,stream_listen majors=pinecone,fish_leap,squirrel_splash fog=14 memory=loop=1 cairn=7 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,squirrel,owl,bird,bear ring=pinecone,fish_leap,squirrel_splash",
    "loop=0 seed=3 length=645s plan=family=morning wake=1 coffee=2 bag_down=0 break_camp=0 douse=1 leave=1 fillers=read,cloud_watch majors=ranger_ridge,fishing_log,squirrel_splash fog=15 memory=loop=1 cairn=0 shoe=0 stolen=none hole=0 rod=1 brave=deer,owl,mouse,dog ring=ranger_ridge,fishing_log,squirrel_splash",
    "loop=0 seed=4 length=594s plan=family=morning wake=0 coffee=0 bag_down=0 break_camp=0 douse=1 leave=1 fillers=cairn,stream_listen,birds_on_water majors=fish_leap,pinecone,ranger_ridge fog=11 memory=loop=1 cairn=5 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,frog,deer,raccoon,owl,bird,dog ring=fish_leap,pinecone,ranger_ridge",
    "loop=0 seed=5 length=609s plan=family=morning wake=0 coffee=0 bag_down=0 break_camp=0 douse=0 leave=0 fillers=birds_on_water,read,stream_listen majors=deer_stick,ranger_ridge,map_chase fog=15 memory=loop=1 cairn=0 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,raccoon,bird,bear,dog ring=deer_stick,ranger_ridge,map_chase",
    "loop=0 seed=8 length=650s plan=family=morning wake=1 coffee=1 bag_down=0 break_camp=2 douse=0 leave=2 fillers=birds_on_water,stream_listen,read majors=ants_cookie,echo,pinecone fog=13 memory=loop=1 cairn=1 shoe=0 stolen=none hole=0 rod=0 brave=deer,bird,mouse ring=ants_cookie,echo,pinecone",
    "loop=0 seed=10 length=650s plan=family=morning wake=2 coffee=2 bag_down=1 break_camp=0 douse=0 leave=1 fillers=birds_on_water,stream_listen,cloud_watch majors=ants_cookie,echo,pinecone fog=11 memory=loop=1 cairn=0 shoe=0 stolen=none hole=0 rod=0 brave=chipmunk,raccoon,owl,bird,bear,dog ring=ants_cookie,echo,pinecone",
    "loop=0 seed=11 length=584s plan=family=morning wake=0 coffee=2 bag_down=1 break_camp=0 douse=0 leave=1 fillers=birds_on_water,cloud_watch majors=fishing_small,echo fog=11 memory=loop=1 cairn=0 shoe=0 stolen=none hole=0 rod=1 brave=squirrel,raccoon,bear,mouse,dog ring=fishing_small,echo",
    "loop=0 seed=14 length=642s plan=family=morning wake=1 coffee=1 bag_down=0 break_camp=1 douse=1 leave=0 fillers=cairn,birds_on_water,stream_listen majors=map_chase,echo,fishing_small fog=15 memory=loop=1 cairn=6 shoe=0 stolen=none hole=0 rod=1 brave=chipmunk,deer,raccoon,owl,bear,dog ring=map_chase,echo,fishing_small",
};

static void testMarkUnchanged() {
  int same = 0;
  for (int i = 0; i < 8; ++i) {
    Rig a;
    CHECK(a.start(kMarkSeeds[i], makeInputs(1.5f, kForceMorning)), "start");
    char plan[320], mem[320];
    appLoopPlan(a.app, plan, sizeof plan);
    CHECK(appWorld(a.app)->plan.stay == 0, "seed %d: the stay was picked", (int)kMarkSeeds[i]);
    const unsigned long long ls = (unsigned long long)appLoopSeed(a.app);
    const uint32_t len = appLoopTicks(a.app);
    uint32_t ticks = 0;
    for (; ticks + 1 < len; ++ticks) appTick(a.app);
    appMemory(a.app, mem, sizeof mem);
    for (int guard = 0; guard < 40 * 60 * SS_TICK_HZ; ++guard) {
      ++ticks;
      if (appTick(a.app)) break;
    }
    char got[800];
    snprintf(got, sizeof got, "loop=0 seed=%llu length=%.0fs plan=%s memory=%s", ls, (double)ticks / SS_TICK_HZ, plan, mem);
    const bool ok = strcmp(got, kMark[i]) == 0;
    if (!ok) printf("seed %d\n  want %s\n  got  %s\n", (int)kMarkSeeds[i], kMark[i], got);
    CHECK(ok, "Mark's morning --loops line differs (seed %d)", (int)kMarkSeeds[i]);
    same += ok ? 1 : 0;
    a.stop();
  }
  printf("Mark's morning: %d of 8 first loops identical to the tree before the stay\n", same);
}

// ---- the split -----------------------------------------------------------------------------

// First loops of 300 seeds (forced morning), and 3 seeds of 12 loops in a row on one App: about 60 % stay,
// and both kinds follow both kinds.
static void testSplit() {
  int stay = 0, total = 0;
  for (uint64_t seed = 1; seed <= 300; ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs(1.5f, kForceMorning)), "start");
    const World* w = appWorld(a.app);
    CHECK(appSpineFamily(a.app) == SF_MORNING, "seed %d: family %d", (int)seed, appSpineFamily(a.app));
    stay += w->plan.stay ? 1 : 0;
    ++total;
    a.stop();
  }
  printf("split: %d of %d first loops (seeds 1..300) are the stay (%.1f %%; proposed 60)\n", stay, total,
         100.0 * (double)stay / (double)total);
  CHECK(stay * 100 >= 52 * total && stay * 100 <= 68 * total, "%d of %d are the stay", stay, total);

  int after[2][2] = {};   // [last was stay][this is stay]
  int loops_stay = 0, loops = 0;
  for (uint64_t seed = 31; seed <= 33; ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs(1.5f, kForceMorning)), "start");
    int last = -1;
    for (int loop = 0; loop < 12; ++loop) {
      const int st = appWorld(a.app)->plan.stay ? 1 : 0;
      if (last >= 0) ++after[last][st];
      last = st;
      loops_stay += st;
      ++loops;
      const uint32_t len = appLoopTicks(a.app);
      for (uint32_t t = 0; t + 1 < len; ++t) appTick(a.app);
      CHECK(appSpineOverruns(a.app) == 0 && appCritterCount(a.app) == 0, "seed %d loop %d: %d overruns, %d critters left", (int)seed,
            loop, appSpineOverruns(a.app), appCritterCount(a.app));
      CHECK(appTick(a.app), "seed %d loop %d: no rollover at the plan's length", (int)seed, loop);
    }
    a.stop();
  }
  printf("split in turn: %d of %d loops are the stay; stay after stay %d, break after stay %d, stay after break %d, break after break %d\n",
         loops_stay, loops, after[1][1], after[0][1], after[1][0], after[0][0]);
  CHECK(after[1][1] > 0 && after[0][1] > 0 && after[1][0] > 0 && after[0][0] > 0, "a kind never followed a kind");
}

// ---- every beat ---------------------------------------------------------------------------------

struct Seen {
  bool beat[SY_COUNT][4] = {};
  bool boil = false, pot_off = false;
  bool cake_a = false, cake_b = false, cake_c = false;
  bool squirrel = false, raccoon = false, rustle = false, leaf = false;
  bool sack_open = false, sack_tied = false, drops = false, brush = false, map = false;
  int shoe_raids = 0, mug_raids = 0, book_raids = 0, no_loot = 0;
  // The script of each beat variant: the latest it finished (ticks) and the shortest slack to the beat's end.
  int done_max[SY_COUNT][4] = {}, slack_min[SY_COUNT][4] = {}, beat_len[SY_COUNT][4] = {};
  Seen() {
    for (auto& r : slack_min)
      for (int& v : r) v = 1 << 30;
  }
};

static int g_stay_loops = 0;

// One stay loop on a fresh App, ticked to its last tick with every check.
static void runStayLoop(uint64_t seed, Seen& sn, int& bad_frame, int& bad_corner, int& bad_end, int& len_bad, int& crit_bad,
                        int& corner_samples) {
  const Inputs in = makeInputs(1.5f, kForceMorning);
  Rig a;
  CHECK(a.start(seed, in), "start");
  const World* w = appWorld(a.app);
  const LoopPlan plan = w->plan;
  for (int b = 0; b < SY_COUNT; ++b) {
    CHECK(plan.fvar[b] < kVariants[b], "seed %d: %s variant %d", (int)seed, kBeats[b], plan.fvar[b]);
    if (plan.fvar[b] < 4) sn.beat[b][plan.fvar[b]] = true;
  }
  CHECK(plan.away_s >= 12 && plan.away_s <= 18, "seed %d: away %d s", (int)seed, plan.away_s);
  const int len = (int)appLoopTicks(a.app);
  if (len < 480 * SS_TICK_HZ || len > 660 * SS_TICK_HZ) {
    ++len_bad;
    printf("seed %d: stay loop is %.1f s\n", (int)seed, (double)len / SS_TICK_HZ);
  }
  char prev[32] = "build";
  bool shoe_down0 = false, mug_down0 = false, book_down0 = false;
  int away_t0 = -1, away_ticks = 0;
  bool away_seen_raccoon = false;
  int pee_start = -1;
  const LsPine& pine = w->land.fg[1];
  for (int t = 1; t < len; ++t) {
    appTick(a.app);
    const char* beat = appBeatName(a.app);
    if (!beatIs(a.app, prev)) {   // a beat just changed
      const int b = beatIndex(prev);
      if (b >= 0 && plan.fvar[b] < 4) {   // how long its script took, and what was left of the beat
        const int v = plan.fvar[b], bt = (int)w->spine.run.beat_ticks, d = (int)w->spine.run.done_at;
        if (d > sn.done_max[b][v]) sn.done_max[b][v] = d;
        if (bt - d < sn.slack_min[b][v]) sn.slack_min[b][v] = bt - d;
        sn.beat_len[b][v] = bt;
      }
      if (b >= 0 && b < SY_DAYHIKE) {
        Rect r;
        CHECK(appHikerBounds(a.app, &r) && r.x0 >= 0 && r.x1 <= SS_WIDTH, "seed %d: %s ends with the hiker out of frame", (int)seed,
              kBeats[b]);
      }
      if (strcmp(prev, "pee") == 0) {   // the raid's result
        const Memory& m = w->memory;
        if (shoe_down0) {
          ++sn.shoe_raids;
          CHECK(m.shoe_taken && m.last_stolen == ITEM_SHOE + 1 && !w->items[ITEM_SHOE].alt && w->items[ITEM_SHOE].down,
                "seed %d: the raid did not take one shoe (taken %d stolen %d alt %d down %d)", (int)seed, m.shoe_taken, m.last_stolen,
                w->items[ITEM_SHOE].alt, w->items[ITEM_SHOE].down);
        } else if (mug_down0) {
          ++sn.mug_raids;
          CHECK(m.last_stolen == ITEM_MUG + 1 && !w->items[ITEM_MUG].critter && w->items[ITEM_MUG].pos.x < 0.f,
                "seed %d: the raid did not take the mug away (stolen %d, it lies at %.0f)", (int)seed, m.last_stolen, (double)w->items[ITEM_MUG].pos.x);
        } else if (book_down0) {
          ++sn.book_raids;
          CHECK(m.last_stolen == ITEM_BOOK + 1, "seed %d: the raid did not take the book", (int)seed);
        } else {
          ++sn.no_loot;
        }
        CHECK(away_ticks >= (int)plan.away_s * SS_TICK_HZ - 3, "seed %d: out of sight only %.1f s (plan %d)", (int)seed,
              (double)away_ticks / SS_TICK_HZ, plan.away_s);
        CHECK(away_ticks <= 24 * SS_TICK_HZ, "seed %d: out of sight %.1f s", (int)seed, (double)away_ticks / SS_TICK_HZ);
        CHECK(away_seen_raccoon || (!shoe_down0 && !mug_down0 && !book_down0), "seed %d: no raccoon seen at the raid", (int)seed);
        CHECK(appCritterCount(a.app) == 0, "seed %d: %d critters left after the pee", (int)seed, appCritterCount(a.app));
      }
      snprintf(prev, sizeof prev, "%s", beat);
      if (strcmp(beat, "pee") == 0) {
        shoe_down0 = w->items[ITEM_SHOE].down;
        mug_down0 = w->items[ITEM_MUG].down;
        book_down0 = w->items[ITEM_BOOK].down;
        away_t0 = -1;
        away_ticks = 0;
        away_seen_raccoon = false;
        pee_start = t;
      }
    }
    Rect r;
    const bool vis = appHikerBounds(a.app, &r);
    if (vis) {
      const bool corner = r.x1 > LS_QUIET_X0_T && r.y1 > LS_QUIET_Y0_T;
      const bool rows = r.y0 >= 0 && r.y1 <= SS_HEIGHT;
      bool cols = r.x0 >= 0 && r.x1 <= SS_WIDTH;
      if (beatIs(a.app, "day_hike") || isMajor(beat)) cols = r.x1 > 0 && r.x0 < SS_WIDTH;
      if (!(rows && cols) || corner) {
        if (bad_frame < 5) printf("seed %d tick %d (%s): hiker rect %d,%d,%d,%d\n", (int)seed, t, beat, r.x0, r.y0, r.x1, r.y1);
        ++bad_frame;
      }
    }
    for (int i = 0; i < 8; ++i) {
      Rect cr;
      int k = -1;
      if (!appCritterRect(a.app, i, &cr, &k)) continue;
      const bool in_frame = cr.x1 > 0 && cr.x0 < SS_WIDTH && cr.y0 >= 0 && cr.y1 <= SS_HEIGHT;
      const bool corner = cr.x1 > LS_QUIET_X0_T && cr.y1 > LS_QUIET_Y0_T;
      if (!in_frame || corner) {
        if (crit_bad < 3) printf("seed %d tick %d (%s): critter kind %d rect %d,%d,%d,%d\n", (int)seed, t, beat, k, cr.x0, cr.y0, cr.x1, cr.y1);
        ++crit_bad;
      }
      if (beatIs(a.app, "pee")) {
        if (k == CR_SQUIRREL) sn.squirrel = true;
        if (k == CR_RACCOON) {
          sn.raccoon = true;
          away_seen_raccoon = true;
        }
      }
    }
    // The pee: how long he is out of sight (still, at the tree's foot, not walking).
    if (beatIs(a.app, "pee") && t > pee_start) {
      const bool at_tree = fabsf(w->hiker.feet.x - ((float)pine.x + 4.f)) < 2.5f && !appHikerWalking(a.app);
      if (at_tree && away_t0 < 0) away_t0 = t;
      if (away_t0 >= 0 && at_tree) away_ticks = t - away_t0;
      if (w->rustle.t >= 0.f) sn.rustle = true;
      for (const Bit& b : w->bits)
        if (b.kind == BIT_LEAF) sn.leaf = true;
    }
    if (beatIs(a.app, "coffee") && plan.fvar[SY_COFFEE] == 3 && w->steam.t >= 0.f && w->steam.n > 2 && w->steam.t > 0.3f) sn.boil = true;
    if (beatIs(a.app, "breakfast")) {
      const Item& cake = w->items[ITEM_CAKE];
      const Item& pan = w->items[ITEM_PAN];
      if (plan.fvar[SY_BREAKFAST] == 0 && cake.down == false && pan.fill > 0.5f && sn.beat[SY_BREAKFAST][0] && (w->spine.target & 1u))
        sn.cake_a = true;
      if (cake.alt && cake.held) sn.cake_b = true;
      if (plan.fvar[SY_BREAKFAST] == 2 && cake.down) {   // in a bird's feet: inside the bird's bounds
        for (int i = 0; i < 8; ++i) {
          Rect cr;
          int k = -1;
          if (appCritterRect(a.app, i, &cr, &k) && k == CR_BIRD && cake.pos.x >= (float)cr.x0 - 10.f && cake.pos.x <= (float)cr.x1 + 10.f &&
              cake.pos.y >= (float)cr.y0 - 10.f && cake.pos.y <= (float)cr.y1 + 10.f && w->items[ITEM_PAN].fill < 0.5f && cake.fill < 0.6f &&
              cake.fill > 0.5f)
            sn.cake_c = true;
        }
      }
    }
    if (beatIs(a.app, "wash")) {
      if (w->items[ITEM_BRUSH].held) sn.brush = true;
      for (const Bit& b : w->bits)
        if (b.kind == BIT_DROP || b.kind == BIT_RING) sn.drops = true;
    }
    if (beatIs(a.app, "tidy")) {
      if (w->items[ITEM_SACK].puff > 0.9f) sn.sack_open = true;
      if (w->items[ITEM_SACK].alt) sn.sack_tied = true;
    }
    if (beatIs(a.app, "day_hike") && w->major.g[0].kind == GL_SHAPE) sn.map = true;
    if (beatIs(a.app, "coffee") && plan.fvar[SY_COFFEE] == 3 && !w->items[ITEM_POT].on_fire && w->items[ITEM_POT].down) sn.pot_off = true;
    if (t % 250 == 0 && appFade(a.app) == 0.f) {
      Palette pal;
      scenePalette(a.app, in, pal);
      appInvalidate(a.app);
      appRender(a.app, a.surf);
      bad_corner += badCornerPixels(a, pal, (int)seed, t, bad_corner);
      ++corner_samples;
    }
  }
  {   // the day hike's script, on the last tick (it has no next beat to read it at)
    const int bt = (int)w->spine.run.beat_ticks, d = (int)w->spine.run.done_at;
    if (d > sn.done_max[SY_DAYHIKE][0]) sn.done_max[SY_DAYHIKE][0] = d;
    if (bt - d < sn.slack_min[SY_DAYHIKE][0]) sn.slack_min[SY_DAYHIKE][0] = bt - d;
    sn.beat_len[SY_DAYHIKE][0] = bt;
  }
  // One tick left: the camp as he left it.
  const int over = appSpineOverruns(a.app);
  if (over) {
    char p[320];
    appLoopPlan(a.app, p, sizeof p);
    printf("seed %d (%s): %d beat(s) overran\n", (int)seed, p, over);
    bad_end += 1;
  }
  Rect rr;
  if (appCritterCount(a.app) != 0 || appHikerBounds(a.app, &rr) || appFade(a.app) < 0.99f) {
    ++bad_end;
    printf("seed %d: at the end %d critters, hiker visible %d, fade %.3f\n", (int)seed, appCritterCount(a.app), appHikerBounds(a.app, &rr),
           (double)appFade(a.app));
  }
  const bool standing = w->tent.state == TENT_UP && w->tent.progress == 1.f && w->bag.state == BAG_HUNG && w->fire.state == FIRE_UNLIT &&
                        w->items[ITEM_WOOD].down && w->items[ITEM_POT].down && !w->items[ITEM_POT].on_fire && !w->items[ITEM_POT].held &&
                        !w->hiker.visible && w->steam.t < 0.f;
  CHECK(standing, "seed %d: the camp is not standing at the end (tent %d/%.2f bag %d fire %d wood %d pot %d/%d/%d)", (int)seed, w->tent.state,
        (double)w->tent.progress, w->bag.state, w->fire.state, w->items[ITEM_WOOD].down, w->items[ITEM_POT].down, w->items[ITEM_POT].on_fire,
        w->items[ITEM_POT].held);
  CHECK(w->items[ITEM_PAN].down && !w->items[ITEM_PAN].on_fire && !w->items[ITEM_PAN].held, "seed %d: the pan is not down by the fire", (int)seed);
  CHECK(!w->items[ITEM_CAKE].down && !w->items[ITEM_CAKE].held, "seed %d: a pancake is left", (int)seed);
  CHECK(!w->items[ITEM_BRUSH].held && w->major.g[0].kind == GL_NONE, "seed %d: the brush or the map is left in his hands", (int)seed);
  CHECK(appTick(a.app), "seed %d: no rollover at the plan's length", (int)seed);
  a.stop();
  ++g_stay_loops;
}

static void testStayLoops() {
  Seen sn;
  int bad_frame = 0, bad_corner = 0, bad_end = 0, len_bad = 0, crit_bad = 0, corner_samples = 0;
  int found = 0;
  for (uint64_t seed = 1; seed <= 400 && found < 40; ++seed) {
    {
      Rig a;
      CHECK(a.start(seed, makeInputs(1.5f, kForceMorning)), "start");
      const bool stay = appWorld(a.app)->plan.stay != 0;
      a.stop();
      if (!stay) continue;
    }
    ++found;
    runStayLoop(seed, sn, bad_frame, bad_corner, bad_end, len_bad, crit_bad, corner_samples);
  }
  int missing = 0;
  for (int b = 0; b < SY_COUNT; ++b)
    for (int k = 0; k < kVariants[b]; ++k)
      if (!sn.beat[b][k]) {
        ++missing;
        printf("stay %s variant %d never ran in %d loops\n", kBeats[b], k, found);
      }
  printf("stay: %d loops; raids: %d shoe, %d mug, %d book, %d nothing; squirrel %d raccoon %d rustle %d; boil-over %d (pot off %d); pancake a %d b %d c %d; "
         "sack open %d tied %d; wash drops %d brush %d; map %d; %d corner samples\n",
         found, sn.shoe_raids, sn.mug_raids, sn.book_raids, sn.no_loot, sn.squirrel, sn.raccoon, sn.rustle, sn.boil, sn.pot_off, sn.cake_a, sn.cake_b,
         sn.cake_c, sn.sack_open, sn.sack_tied, sn.drops, sn.brush, sn.map, corner_samples);
  for (int b = 0; b < SY_COUNT; ++b)
    for (int k = 0; k < kVariants[b]; ++k)
      printf("  %-9s %c: script done by %5.1f s of %5.1f s (slack %4.1f s)\n", kBeats[b], 'a' + k, (double)sn.done_max[b][k] / SS_TICK_HZ,
             (double)sn.beat_len[b][k] / SS_TICK_HZ, (double)sn.slack_min[b][k] / SS_TICK_HZ);
  CHECK(found >= 40, "only %d stay loops in 400 seeds", found);
  CHECK(missing == 0, "%d stay variants unseen", missing);
  CHECK(bad_frame == 0, "%d hiker samples out of frame", bad_frame);
  CHECK(crit_bad == 0, "%d critter samples out of frame or in the corner", crit_bad);
  CHECK(bad_corner == 0, "%d corner pixels not ground or grass", bad_corner);
  CHECK(bad_end == 0, "%d loops overran or did not end empty and black", bad_end);
  CHECK(len_bad == 0, "%d loops outside 480..660 s", len_bad);
  CHECK(sn.squirrel && sn.raccoon && sn.rustle && sn.leaf, "the pee's squirrel %d, raccoon %d, rustle %d or leaves %d never showed", sn.squirrel, sn.raccoon, sn.rustle, sn.leaf);
  CHECK(sn.boil && sn.pot_off, "the boil-over (%d) or the pot taken off the fire (%d) never happened", sn.boil, sn.pot_off);
  CHECK(sn.cake_a, "no pancake landed back in the pan");
  CHECK(sn.cake_b, "no pancake landed on his hat");
  CHECK(sn.cake_c, "no bird took a pancake");
  CHECK(sn.sack_open && sn.sack_tied && sn.drops && sn.brush && sn.map, "tidy open %d tied %d, wash drops %d brush %d, map %d", sn.sack_open, sn.sack_tied,
        sn.drops, sn.brush, sn.map);
  CHECK(sn.shoe_raids > 0 && sn.no_loot == 0, "raids: shoe %d, nothing taken %d", sn.shoe_raids, sn.no_loot);
}

// The fallback: the shoe is gone (memory), so the raid takes the mug. Loops in a row on one App, memory
// kept: after a raid took the shoe, the next stay morning's raid takes the mug.
static void testFallback() {
  int shoe = 0, mug = 0;
  for (uint64_t seed = 41; seed <= 46 && (shoe == 0 || mug == 0); ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs(1.5f, kForceMorning)), "start");
    const World* w = appWorld(a.app);
    for (int loop = 0; loop < 10; ++loop) {
      const bool stay = w->plan.stay != 0;
      const bool shoe_gone0 = w->memory.shoe_taken;
      const uint32_t len = appLoopTicks(a.app);
      uint8_t stolen_at_pee_end = 0;
      char prev[32] = "";
      for (uint32_t t = 0; t + 1 < len; ++t) {
        appTick(a.app);
        if (stay && strcmp(prev, "pee") == 0 && !beatIs(a.app, "pee")) stolen_at_pee_end = w->memory.last_stolen;
        snprintf(prev, sizeof prev, "%s", appBeatName(a.app));
      }
      if (stay && shoe_gone0) {
        // The wake b shoe (stepped on) may have been the only one out; otherwise the mug.
        const bool ok = stolen_at_pee_end == ITEM_MUG + 1 || stolen_at_pee_end == ITEM_BOOK + 1;
        CHECK(ok, "seed %d loop %d: the shoe was gone and the raid took item %d", (int)seed, loop, stolen_at_pee_end - 1);
        mug += ok && stolen_at_pee_end == ITEM_MUG + 1 ? 1 : 0;
      } else if (stay) {
        shoe += stolen_at_pee_end == ITEM_SHOE + 1 ? 1 : 0;
      }
      CHECK(appTick(a.app), "seed %d loop %d: no rollover", (int)seed, loop);
    }
    a.stop();
  }
  printf("fallback: raids that took the shoe %d, the mug (the shoe already gone) %d\n", shoe, mug);
  CHECK(shoe > 0 && mug > 0, "shoe raids %d, mug raids %d", shoe, mug);
}

// ---- the doorbell freeze -------------------------------------------------------------------------

struct Snap {
  float xt, xt2, rustle, steam, cake_x, cake_y, sack, hiker_x, hiker_y, c0x, c1x;
  uint8_t xstep;
  uint32_t frozen;
};
static Snap snap(const World* w) {
  Snap s;
  s.xt = w->spine.xt;
  s.xt2 = w->spine.xt2;
  s.rustle = w->rustle.t;
  s.steam = w->steam.t;
  s.cake_x = w->items[ITEM_CAKE].pos.x;
  s.cake_y = w->items[ITEM_CAKE].pos.y;
  s.sack = w->items[ITEM_SACK].puff + w->items[ITEM_SACK].fill + w->items[ITEM_SACK].squash;
  s.hiker_x = w->hiker.feet.x;
  s.hiker_y = w->hiker.feet.y;
  s.c0x = w->critters[0].feet.x;
  s.c1x = w->critters[1].feet.x;
  s.xstep = w->spine.xstep;
  s.frozen = w->frozen;
  return s;
}
static bool sameSnap(const Snap& a, const Snap& b) {
  return a.xt == b.xt && a.xt2 == b.xt2 && a.rustle == b.rustle && a.steam == b.steam && a.cake_x == b.cake_x && a.cake_y == b.cake_y &&
         a.sack == b.sack && a.hiker_x == b.hiker_x && a.hiker_y == b.hiker_y && a.c0x == b.c0x && a.c1x == b.c1x && a.xstep == b.xstep;
}

static void testFreeze() {
  int rung = 0, froze_ticks = 0, bad = 0, bells_pee = 0, bells_cake = 0, bells_sack = 0, bells_creep = 0;
  for (uint64_t seed = 1; seed <= 400 && (bells_pee == 0 || bells_cake == 0 || bells_sack == 0 || bells_creep == 0); ++seed) {
    Rig a;
    CHECK(a.start(seed, makeInputs(1.5f, kForceMorning)), "start");
    const World* w = appWorld(a.app);
    if (!w->plan.stay) {
      a.stop();
      continue;
    }
    const uint32_t len = appLoopTicks(a.app);
    uint32_t t = 0;
    bool done_pee = false, done_cake = false, done_sack = false, done_creep = false;

    while (t < len + 600) {
      const bool want = (!done_pee && beatIs(a.app, "pee") && w->spine.xstage == 1 && w->spine.xstep == 1 && w->spine.xt2 > 0.5f) ||
                        (!done_cake && beatIs(a.app, "breakfast") && w->items[ITEM_CAKE].down && w->items[ITEM_CAKE].pos.y < w->fire.pos.y - 30.f) ||
                        (!done_sack && beatIs(a.app, "tidy") && w->items[ITEM_SACK].puff > 0.4f) ||
                        (!done_creep && beatIs(a.app, "day_hike") && w->spine.xstage == 3 && w->spine.xt > 3.f);
      if (want && !w->react.active) {
        if (beatIs(a.app, "pee")) done_pee = true, ++bells_pee;
        else if (beatIs(a.app, "breakfast")) done_cake = true, ++bells_cake;
        else if (beatIs(a.app, "tidy")) done_sack = true, ++bells_sack;
        else done_creep = true, ++bells_creep;
        Event e;
        e.type = EV_DOORBELL;
        e.arg = 0;
        appPostEvent(a.app, e);

        ++rung;
        appTick(a.app);   // the bell is taken: the reaction starts
        ++t;
        if (!w->react.active) continue;   // (he could not react: no freeze to check)
        const Snap s0 = snap(w);
        const uint32_t tick0 = w->tick;
        int n = 0;
        while (w->react.active && n < 400) {
          appTick(a.app);
          ++t;
          ++n;
          if (w->react.active) {
            const Snap s1 = snap(w);
            if (!sameSnap(s0, s1)) {
              if (bad < 3) printf("seed %d: the story moved during the doorbell reaction (tick %u, xt %.3f -> %.3f)\n", (int)seed, appTickCount(a.app), (double)s0.xt, (double)s1.xt);
              ++bad;
            }
          }
        }
        froze_ticks += n;
        CHECK(n >= 80 && n <= 100, "seed %d: the reaction lasted %d ticks", (int)seed, n);
        CHECK(w->tick > tick0, "seed %d: World::tick stood still", (int)seed);
        continue;
      }
      if (appTick(a.app)) break;
      ++t;
    }
    // The loop ended (rolled over) after its plan length plus every frozen tick.
    CHECK(t >= len, "seed %d: ended early (%u of %u)", (int)seed, t, len);
    a.stop();
  }
  printf("freeze: %d doorbells (pee %d, flip %d, sack %d, empty camp %d), %d frozen ticks, story moved %d times\n", rung, bells_pee, bells_cake,
         bells_sack, bells_creep, froze_ticks, bad);
  CHECK(bad == 0, "%d story changes during the reaction", bad);
  CHECK(bells_pee && bells_cake && bells_sack && bells_creep, "bells: pee %d flip %d sack %d camp %d", bells_pee, bells_cake, bells_sack, bells_creep);

  // And the loop ends the same: a bell in the middle of the pee holds every beat for the ticks it froze, with
  // no overrun and nothing left, and the rollover comes at the plan's length plus those ticks.
  uint64_t seed = 1;
  for (; seed <= 400; ++seed) {
    Rig p;
    p.start(seed, makeInputs(1.5f, kForceMorning));
    const bool stay = appWorld(p.app)->plan.stay != 0;
    p.stop();
    if (stay) break;
  }
  Rig a;
  CHECK(a.start(seed, makeInputs(1.5f, kForceMorning)), "start");
  const World* w = appWorld(a.app);
  const uint32_t len = appLoopTicks(a.app);
  uint32_t ticks = 0, frozen = 0;
  int over = -1, crit = -1;
  bool rang = false;
  for (; ticks < len + 1000; ++ticks) {
    if (!rang && beatIs(a.app, "pee") && w->spine.xstage == 1 && w->spine.xt > 3.f) {
      rang = true;
      Event e;
      e.type = EV_DOORBELL;
      e.arg = 0;
      appPostEvent(a.app, e);
    }
    frozen = w->frozen > frozen ? w->frozen : frozen;
    over = appSpineOverruns(a.app);
    crit = appCritterCount(a.app);
    if (appTick(a.app)) break;
  }
  CHECK(rang && frozen > 0, "the bell in the pee never rang or never froze (%u)", frozen);
  CHECK(ticks + 1 == len + frozen, "rolled over after %u ticks, want %u + %u frozen", ticks + 1, len, frozen);
  CHECK(over == 0 && crit == 0, "with a bell in the pee: %d overruns, %d critters at the end", over, crit);
  a.stop();
}

// ---- incremental equals full ---------------------------------------------------------------------

static bool busyScene(const World* w) {
  return w->rustle.t >= 0.f || w->steam.t >= 0.f || w->items[ITEM_CAKE].down || w->items[ITEM_CAKE].held || w->items[ITEM_SACK].down ||
         w->items[ITEM_BRUSH].held || w->major.g[0].kind != GL_NONE || w->spine.xstage != 0;
}

static void testIncremental() {
  long sum = 0, frames = 0;
  int worst = 0, diffs = 0, checks = 0, loops = 0;
  for (uint64_t seed = 1; seed <= 400 && loops < 3; ++seed) {
    const Inputs in = makeInputs(1.5f, kForceMorning);
    Rig a, b;
    CHECK(a.start(seed, in) && b.start(seed, in), "start");
    if (!appWorld(a.app)->plan.stay) {
      a.stop();
      b.stop();
      continue;
    }
    ++loops;
    const World* w = appWorld(a.app);
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
      if (t % 397 == 0 || (busyScene(w) && t % 29 == 0)) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        if (memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0) {
          long i = 0;
          while (a.fb[i] == b.fb[i]) ++i;
          if (diffs < 4)
            printf("seed %d tick %d (%s): incremental differs from full at (%ld,%ld)\n", (int)seed, t, appBeatName(a.app), i % SS_WIDTH, i / SS_WIDTH);
          ++diffs;
        }
      }
    }
    a.stop();
    b.stop();
  }
  printf("stay strips per frame (%d loops, build excluded): avg %.2f, max %d; %d full-render checks\n", loops, frames ? (double)sum / (double)frames : 0.0,
         worst, checks);
  CHECK(diffs == 0, "%d checkpoints where incremental and full renders differ", diffs);
  CHECK(frames > 0 && (double)sum / (double)frames < 9.0, "strips per frame %.2f", frames ? (double)sum / (double)frames : 0.0);
}

static void testAppSize() {
  printf("sizeof(App) = %u bytes (budget %d)\n", (unsigned)appAppBytes(), SS_APP_BYTES);
  CHECK(appAppBytes() <= (uint32_t)SS_APP_BYTES, "App is %u bytes", (unsigned)appAppBytes());
}

int main() {
  testMarkUnchanged();
  testSplit();
  testStayLoops();
  testFallback();
  testFreeze();
  testIncremental();
  testAppSize();
  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all morning-stay tests passed\n");
  return 0;
}
