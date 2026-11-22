// Tests for the plinking as a surprise major (core/scene/day_plink.cpp, plink.cpp, day.cpp; Mike, 2026-11-17), wired into
// the midday family beside the bike, the archery, the slackline and the ropes course:
//
//   [1] the plan: none at night, in the other families, in a rain, snow or storm the OUTSIDE data says is there, or with
//       a house dog out; how many midday loops play it; every variant comes up
//   [2] each variant plays to its end in the real scene: no overrun, the script done with 3 s to spare, no critter left,
//       him seated at rest with his hands empty, the hat on, the daytime union zero again, the bag hung with its line 16
//       and no swing; him in frame and out of the quiet corner, the piece out of the chip's box, in frame; the story
//       (A all six cans down and the gag can teetering, B the last can still up after the string and then poked off, C
//       the bag dropped and the five food items spilled, gathered, the bag back on the branch); the rifle's safety
//       (loaded only kneeling at the firing spot, unloaded before he stands, out of his hands at the end); the birds and
//       the squirrel at the first shot; drawn tick by tick it equals a full render; strips per frame
//   [3] a random rain after the plan sends it to the no-show: nothing drawn, no overrun, him seated
//   [4] the doorbell at ten moments of each variant: the story stands still for the whole reaction, he only looks round
//       (his facing never changes) while the rifle is in his hands, and the beat carries on and ends clean
//   [5] a dog that comes out while it plays stays out of the scene until it has ended, and comes in after
//   [6] two runs of each variant draw the same frames; sizeof(App) stays under the 30,000 bytes of the PC wire
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/day.h"
#include "scene/housedogs.h"
#include "scene/majors.h"
#include "scene/memory.h"
#include "scene/spine_family.h"
#include "scene/weather.h"
#include "scene/world.h"

#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);  \
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

static Inputs makeInputs(float mood, int family, bool night = false, bool outside = false, int code = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.spine_force = (uint8_t)(1 + family);
  in.night_mode = night;
  in.have_outside = outside;
  in.weather_code = code;
  return in;
}
static bool inChipBox(const Rect& r) { return !r.empty() && r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }
static bool inCorner(const Rect& r) { return !r.empty() && r.x1 > LS_QUIET_X0 && r.y1 > LS_QUIET_Y0; }
static bool inFrame(const Rect& r) { return r.x0 >= 0 && r.x1 <= SS_WIDTH && r.y0 >= 0 && r.y1 <= SS_HEIGHT; }
static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }
static int indexOf(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return i;
  return -1;
}
static bool wetWeather(const World& w) { return w.weather.kind == WX_RAIN || w.weather.kind == WX_SNOW || w.weather.kind == WX_STORM; }

static int findSeeds(int var, const Inputs& in, uint64_t* out, int n, uint64_t from = 1, uint64_t to = 1600) {
  int k = 0;
  for (uint64_t seed = from; seed <= to && k < n; ++seed) {
    Rig r;
    if (!r.start(seed, in)) return k;
    const World* w = appWorld(r.app);
    const int i = indexOf(w->plan, MJ_PLINK);
    if (i >= 0 && (w->plan.major_var[i] & 3) == var && !wetWeather(*w)) out[k++] = seed;
    r.stop();
  }
  return k;
}

// ---- [1] the plan ----------------------------------------------------------------------------------------------

static MajorSlots middaySlots() {
  MajorSlots s;
  memset(&s, 0, sizeof s);
  s.family = SF_MIDDAY;
  s.after[0] = 0;
  s.after[1] = 1;
  s.after[2] = 2;
  s.open = 7;
  return s;
}
static int plansWith(int n, const Inputs& in, const MajorSlots& sl, int var = -1) {
  int hits = 0;
  for (int seed = 1; seed <= n; ++seed) {
    Rng rng;
    rng.seed((uint64_t)seed);
    LoopPlan p;
    memset(&p, 0, sizeof p);
    Memory mem;
    memset(&mem, 0, sizeof mem);
    majorsPlan(p, rng, in, mem, sl);
    const int i = indexOf(p, MJ_PLINK);
    hits += i >= 0 && (var < 0 || (p.major_var[i] & 3) == var);
  }
  return hits;
}

static void testPlan() {
  printf("[1] the plan: gating, how often, every variant\n");
  const MajorSlots mid = middaySlots();
  const int N = 800;
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  const int hits = plansWith(N, day, mid);
  printf("  midday, %d fresh plans: plink %5.1f %%\n", N, 100.0 * hits / N);
  CHECK(hits > N / 25 && hits < N / 3, "plink in %d of %d midday plans", hits, N);
  for (int v = 0; v < 3; ++v) CHECK(plansWith(N, day, mid, v) > 0, "variant %c never planned", 'A' + v);
  const Inputs night1 = makeInputs(3.f, SF_MIDDAY, true), night2 = makeInputs(0.2f, SF_MIDDAY), night3 = makeInputs(5.9f, SF_MIDDAY);
  for (const Inputs* in : {&night1, &night2, &night3}) CHECK(plansWith(N, *in, mid) == 0, "plink at night (mood %.1f)", (double)in->mood);
  for (int code = 0; code < 8; ++code) {
    const Inputs in = makeInputs(3.f, SF_MIDDAY, false, true, code);
    const bool wet = code == 1 || code == 2 || code == 7;
    const int k = plansWith(N, in, mid);
    if (wet) CHECK(k == 0, "planned in weather %d", code);
    else CHECK(k > N / 25, "only %d planned in weather %d", k, code);
  }
  Inputs dog = makeInputs(3.f, SF_MIDDAY);
  dog.dog_out = 1;
  CHECK(plansWith(N, dog, mid) == 0, "planned with a dog out");
  dog.dog_out = 3;
  CHECK(plansWith(N, dog, mid) == 0, "planned with both dogs out");
  int other = 0;
  for (int fam = 0; fam < SF_COUNT; ++fam) {
    if (fam == SF_MIDDAY || fam == SF_LOOKOUT) continue;
    MajorSlots sl = mid;
    sl.family = (uint8_t)fam;
    sl.open = fam == SF_EVENING ? 15 : 7;
    for (float mood : {1.5f, 3.f, 4.f}) other += plansWith(200, makeInputs(mood, fam), sl);
  }
  CHECK(other == 0, "%d plink plans in the evening or the morning", other);
  CHECK(strcmp(majorName(MJ_PLINK), "plink") == 0, "name");
  CHECK(majorSeconds(MJ_PLINK, 0) > 0 && majorSeconds(MJ_PLINK, 1) > majorSeconds(MJ_PLINK, 0), "seconds");
}

// ---- [2] each variant plays ----------------------------------------------------------------------------------------------

struct Ev {
  int beat_ticks, done_at, overruns, critters;
  bool end_ok;
  char why[96];
  long strips_sum;
  int strips_n, strips_max;
  int diffs, checks;
  // the story
  int shots, max_down, teeter, bag_dropped, bag_hung_after, food_max, food_gone_after, can0_up_after_string, can0_down_end, critter_max;
  bool loaded_standing, loaded_walking, rifle_in_hands_at_end, safety_dir;
  int bad_hiker, bad_piece, bad_box, bad_corner, hidden;
  float max_elev;
  PlinkState last;
  bool have_last;
  uint32_t hash;
};

static bool atRest(const World& w, bool pack_was, char* why, size_t n) {
  const Hiker& h = w.hiker;
  const char* bad = nullptr;
  if (!h.hat_on) bad = "hat off";
  else if (h.carry != CARRY_NONE) bad = "carrying";
  else if (h.walking) bad = "walking";
  else if (h.pack_on != pack_was) bad = "his pack is not as it was";
  else if (w.fx.arm_w[0] != 0.f || w.fx.arm_w[1] != 0.f || w.fx.lean != 0.f || fabsf(w.fx.head) > 1.0e-6f || w.fx.lift != 0.f) bad = "arms or lean still set";
  else if (w.day.kind != DAY_NONE) bad = "the daytime union is not zero";
  else if (h.target != POSE_SIT) bad = "not sitting";
  else if (w.bag.state != BAG_HUNG || w.bag.line != 16.f || w.bag.swing != 0.f) bad = "the bag is not hung with its line 16";
  else if (!inFrame(w.hiker_rect) || w.hiker_rect.empty()) bad = "not in frame";
  if (bad) snprintf(why, n, "%s", bad);
  return bad == nullptr;
}

static uint32_t hashFrame(const Pixel* fb) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < SS_WIDTH * SS_HEIGHT; i += 7) h = (h ^ fb[i]) * 16777619u;
  return h;
}

static int countMode(const PlinkState& p, int mode) {
  int n = 0;
  for (const PlinkCan& c : p.can) n += c.mode == mode ? 1 : 0;
  return n;
}

static void observe(const World& w, Ev& ev) {
  const World* pw = &w;
  if (pw->day.kind != DAY_PLINK) return;
  const PlinkState& p = pw->day.plink.p;
  ev.last = p;
  ev.have_last = true;
  const int down = countMode(p, CAN_DOWN);
  if (down > ev.max_down) ev.max_down = down;
  if (p.can[3].mode == CAN_ON && p.can[3].aux == 2) ++ev.teeter;
  if (w.bag.state == BAG_DROPPED) ++ev.bag_dropped;
  const int food = plinkFoodLeft(p);
  if (food > ev.food_max) ev.food_max = food;
  if (ev.food_max >= 5 && food == 0) ++ev.food_gone_after;
  if (ev.bag_dropped > 0 && w.bag.state == BAG_HUNG) ++ev.bag_hung_after;
  // safety
  if (plinkLoaded(p)) {
    if (w.hiker.walking) ev.loaded_walking = true;
    if (p.kneel < 0.9f && p.up > 0.f && p.phase != PP_LOWER) ev.loaded_standing = true;
    const Vec2 ax = plinkAxis(p);
    if (ax.x * (float)w.hiker.facing <= 0.f) ev.safety_dir = false;
    if (p.elev > ev.max_elev) ev.max_elev = p.elev;
  }
  if (p.flash > 0.f && p.flash >= 0.079f) ++ev.shots;
}

static Ev playBeat(uint64_t seed, const Inputs& in, int var, bool check_draw, bool quiet, int hash_every = 0) {
  Ev ev;
  memset(&ev, 0, sizeof ev);
  ev.safety_dir = true;
  ev.hash = 2166136261u;
  Rig a, b;
  const bool ok = a.start(seed, in) && b.start(seed, in);
  CHECK(ok, "start seed %d", (int)seed);
  if (!ok) return ev;
  const World* w = appWorld(a.app);
  const uint32_t len = appLoopTicks(a.app);
  char tag[48];
  snprintf(tag, sizeof tag, "plink %c seed %d", 'A' + var, (int)seed);
  CHECK(indexOf(w->plan, MJ_PLINK) >= 0, "%s: not in the plan", tag);
  uint32_t t = 0;
  bool in_beat = false, ended = false, pack_was = true;
  int n_in = 0;
  for (; t + 1 < len && !ended; ++t) {
    if (!in_beat) pack_was = w->hiker.pack_on;
    appTick(a.app);
    appTick(b.app);
    const bool now = beatIs(a.app, "plink");
    if (!now && !in_beat) {
      if (t % 250 == 0 || (t + 60 < len && t % 5 == 0)) appRender(a.app, a.surf);
      continue;
    }
    if (!now && in_beat) {
      ended = true;
      ev.beat_ticks = (int)w->spine.run.beat_ticks;
      ev.done_at = (int)w->spine.run.done_at;
      ev.overruns = appSpineOverruns(a.app);
      ev.critters = appCritterCount(a.app);
      ev.end_ok = atRest(*w, pack_was, ev.why, sizeof ev.why);
      break;
    }
    in_beat = true;
    ++n_in;
    const int strips = appRender(a.app, a.surf);
    ev.strips_sum += strips;
    ++ev.strips_n;
    ev.strips_max = strips > ev.strips_max ? strips : ev.strips_max;
    if (hash_every && n_in % hash_every == 0) ev.hash = (ev.hash ^ hashFrame(a.fb)) * 16777619u;
    if (check_draw && n_in % 17 == 0) {
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      ++ev.checks;
      if (memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0) {
        if (ev.diffs < 3) {
          long k = 0;
          while (a.fb[k] == b.fb[k]) ++k;
          printf("  %s beat tick %d: incremental differs from full at (%ld,%ld)\n", tag, n_in, k % SS_WIDTH, k / SS_WIDTH);
        }
        ++ev.diffs;
      }
    }
    Rect hr;
    if (appHikerBounds(a.app, &hr)) {
      if (!inFrame(hr)) ++ev.bad_hiker;
      if (inChipBox(hr)) ++ev.bad_box;
      if (inCorner(hr)) ++ev.bad_corner;
    } else {
      ++ev.hidden;
    }
    if (w->day.kind == DAY_PLINK) {   // (each layer on its own: the union of the rifle and the cans is a wide box with nothing in it)
      const Rect lay[2] = {plinkTreeBounds(w->day.plink.p), plinkHeldBounds(w->day.plink.p)};
      for (const Rect& pb : lay) {
        if (pb.empty()) continue;
        if (inChipBox(pb)) ++ev.bad_box;
        if (!inFrame(pb)) ++ev.bad_piece;
      }
    }
    const int cc = appCritterCount(a.app);
    if (cc > ev.critter_max) ev.critter_max = cc;
    observe(*w, ev);
    if (w->day.kind == DAY_PLINK && plinkStringDone(w->day.plink.p) && w->day.plink.p.first_done && ev.can0_up_after_string == 0 && !ev.rifle_in_hands_at_end) {
      ev.can0_up_after_string = w->day.plink.p.can[0].mode == CAN_ON ? 1 : 2;
    }
  }
  CHECK(ended, "%s: the beat never ended", tag);
  CHECK(ev.bad_hiker == 0, "%s: %d ticks with him out of frame", tag, ev.bad_hiker);
  CHECK(ev.bad_piece == 0, "%s: %d ticks with the piece out of frame", tag, ev.bad_piece);
  CHECK(ev.bad_box == 0, "%s: %d samples in the chip's box (x 538..796, y 412..476)", tag, ev.bad_box);
  CHECK(ev.bad_corner == 0, "%s: %d ticks with him in the quiet corner", tag, ev.bad_corner);
  CHECK(ev.hidden == 0, "%s: %d ticks with him out of the scene", tag, ev.hidden);
  CHECK(ev.overruns == 0, "%s: %d overruns", tag, ev.overruns);
  CHECK(ev.critters == 0, "%s: %d critters left", tag, ev.critters);
  CHECK(ev.done_at > 0 && ev.beat_ticks - ev.done_at >= 75, "%s: script done at %d of %d ticks (wants 3 s spare)", tag, ev.done_at, ev.beat_ticks);
  CHECK(ev.end_ok, "%s: ends with %s", tag, ev.why);
  CHECK(ev.diffs == 0, "%s: %d checkpoints where incremental and full renders differ", tag, ev.diffs);
  const double avg = ev.strips_n ? (double)ev.strips_sum / (double)ev.strips_n : 0.0;
  if (!quiet)
    printf("  %-18s %4d beat ticks, script done at %d (%.1f s spare), strips avg %.2f max %d, %d full-render checks\n", tag, ev.beat_ticks, ev.done_at,
           (double)(ev.beat_ticks - ev.done_at) / 25.0, avg, ev.strips_max, ev.checks);
  const double limit = (w->weather.kind == WX_HEAT || w->weather.kind == WX_WIND) ? 11.0 : 10.0;
  CHECK(wetWeather(*w) || ev.strips_n == 0 || avg <= limit, "%s: %.2f strips a frame (weather %s)", tag, avg, weatherKindName(w->weather.kind));
  a.stop();
  b.stop();
  return ev;
}

static void checkStory(int var, uint64_t seed, const Ev& e) {
  char tag[48];
  snprintf(tag, sizeof tag, "plink %c seed %d", 'A' + var, (int)seed);
  CHECK(e.have_last, "%s: the piece was never up", tag);
  static const int kShots[3] = {6, 8, 7};
  CHECK(e.shots == kShots[var], "%s: %d shots seen, wanted %d", tag, e.shots, kShots[var]);
  CHECK(!e.loaded_walking, "%s: the rifle was loaded while he walked", tag);
  CHECK(!e.loaded_standing, "%s: the rifle was loaded and up while he stood", tag);
  CHECK(e.safety_dir, "%s: the rifle was loaded and pointing the way he does not face", tag);
  CHECK(e.max_elev < 0.75f, "%s: elevation %.2f", tag, (double)e.max_elev);
  CHECK(e.critter_max >= 1, "%s: nothing startled at the first shot", tag);
  // the last state seen: everything put away
  const PlinkState& p = e.last;
  CHECK(countMode(p, CAN_DOWN) == 0 && countMode(p, CAN_FLY) == 0 && countMode(p, CAN_ON) == 0 && countMode(p, CAN_HELD) == 0 && countMode(p, CAN_TIP) == 0,
        "%s: cans left out at the end", tag);
  CHECK(p.rifle == 0 && p.stick == PI_NONE && p.sack == PI_NONE && p.mag == MAG_NONE && p.rounds == 0 && p.chambered == 0 && p.rope_cut == 0 && p.kneel == 0.f,
        "%s: rifle %d stick %d sack %d mag %d rounds %d rope %d kneel %.1f left at the end", tag, p.rifle, p.stick, p.sack, p.mag, p.rounds, p.rope_cut,
        (double)p.kneel);
  CHECK(plinkFoodLeft(p) == 0, "%s: food left on the ground", tag);
  for (const PlinkBit& b : p.bit) CHECK(b.kind == PB_NONE, "%s: a bit left", tag);
  if (var == 0) {
    CHECK(e.max_down >= 5, "%s: only %d cans down at once", tag, e.max_down);
    CHECK(e.teeter > 10, "%s: the gag can teetered for %d ticks", tag, e.teeter);
    CHECK(e.can0_up_after_string == 2, "%s: can 0 is up after the string", tag);
    CHECK(e.bag_dropped == 0, "%s: the bag dropped", tag);
  } else if (var == 1) {
    CHECK(e.can0_up_after_string == 1, "%s: the stubborn can came down in the string", tag);
    CHECK(e.max_down >= 5, "%s: only %d cans down at once", tag, e.max_down);
    CHECK(e.bag_dropped == 0, "%s: the bag dropped", tag);
  } else {
    CHECK(e.bag_dropped > 10, "%s: the bag dropped for %d ticks", tag, e.bag_dropped);
    CHECK(e.food_max == 5, "%s: %d food items spilled", tag, e.food_max);
    CHECK(e.food_gone_after > 0, "%s: the food was never gathered", tag);
    CHECK(e.bag_hung_after > 0, "%s: the bag never went back up", tag);
  }
}

static uint64_t g_seed[3][1];

static void testPlays() {
  printf("[2] each variant plays to the end in the real scene\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  for (int var = 0; var < 3; ++var) {
    uint64_t s[1] = {0};
    const int n = findSeeds(var, day, s, 1);
    g_seed[var][0] = n ? s[0] : 0;
    CHECK(n == 1, "no seed plays plink %c", 'A' + var);
  }
  for (int var = 0; var < 3; ++var) {
    if (!g_seed[var][0]) continue;
    const Ev e = playBeat(g_seed[var][0], day, var, true, false);
    checkStory(var, g_seed[var][0], e);
  }
}

static void testSweep() {
  printf("[2b] every variant over more seeds (layouts differ: the fire, the stream, the pine)\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  int plays = 0;
  for (int var = 0; var < 3; ++var) {
    uint64_t seeds[6];
    const int n = findSeeds(var, day, seeds, 6, 40);
    CHECK(n >= 5, "only %d seeds for plink %c", n, 'A' + var);
    for (int i = 0; i < n; ++i) {
      const Ev e = playBeat(seeds[i], day, var, false, true);
      checkStory(var, seeds[i], e);
      ++plays;
    }
  }
  printf("  %d beats played\n", plays);
}

// ---- [3] a random rain -----------------------------------------------------------------------------------------------------

static void testNoShow() {
  printf("[3] a rain the plan could not know of sends it to the no-show\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  static const int kKinds[3] = {WX_RAIN, WX_SNOW, WX_STORM};
  const uint64_t seed = g_seed[0][0] ? g_seed[0][0] : 1;
  for (int j = 0; j < 3; ++j) {
    Rig a;
    CHECK(a.start(seed, day), "start");
    World* w = const_cast<World*>(appWorld(a.app));
    w->weather.kind = (uint8_t)kKinds[j];
    int shown = 0;
    bool in_beat = false, pack_was = w->hiker.pack_on;
    for (uint32_t t = 0; t + 1 < appLoopTicks(a.app); ++t) {
      if (!in_beat) pack_was = w->hiker.pack_on;
      appTick(a.app);
      if (beatIs(a.app, "plink")) {
        in_beat = true;
        shown += !dayBounds(w->day).empty();
      } else if (in_beat) {
        break;
      }
    }
    CHECK(in_beat, "the plink beat played");
    CHECK(shown == 0, "plink in %s: something was drawn on %d ticks", weatherKindName(kKinds[j]), shown);
    CHECK(appSpineOverruns(a.app) == 0 && appCritterCount(a.app) == 0, "plink in %s: overruns %d, critters %d", weatherKindName(kKinds[j]), appSpineOverruns(a.app),
          appCritterCount(a.app));
    char why[96];
    CHECK(atRest(*w, pack_was, why, sizeof why), "plink in %s: ends with %s", weatherKindName(kKinds[j]), why);
    a.stop();
  }
}

// ---- [4] the doorbell -----------------------------------------------------------------------------------------------------------

// What the story keeps, as bytes: the piece, less what follows his hands (his waving arm moves them) and what is computed from them.
struct Snap {
  PlinkState p;
  float mt;
  bool on;
};
static Snap snapOf(const World& w) {
  Snap s;
  memset(&s, 0, sizeof s);
  s.on = w.day.kind == DAY_PLINK;
  if (s.on) {
    s.p = w.day.plink.p;
    s.p.near_hand = s.p.far_hand = Vec2{0.f, 0.f};
    s.p.elev = 0.f;
    s.p.dir = 0;       // (with the stick or the sack in his hands he turns to the bell and back, and the piece follows his facing)
    s.p.flash = 0.f;   // (a flash showing when the bell rings is switched off: plinkFreeze)
  }
  s.mt = w.major.t;
  return s;
}
static bool sameSnap(const Snap& a, const Snap& b) { return a.on == b.on && a.mt == b.mt && (!a.on || memcmp(&a.p, &b.p, sizeof a.p) == 0); }

static void testFreeze() {
  printf("[4] the doorbell freezes the piece and the beat; with the rifle in his hands he only looks round\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  int tried = 0, froze = 0, looked = 0, rifle_bells = 0;
  for (int var = 0; var < 3; ++var) {
    const uint64_t seed = g_seed[var][0];
    if (!seed) continue;
    int ok_here = 0;
    for (int at = 0; at < 12; ++at) {
      Rig r;
      CHECK(r.start(seed, day), "start");
      const World* w = appWorld(r.app);
      const uint32_t len = appLoopTicks(r.app);
      for (uint32_t t = 0; t < len && !beatIs(r.app, "plink"); ++t) appTick(r.app);
      const uint32_t skip = 100u + 200u * (uint32_t)at + (uint32_t)(var * 37);
      for (uint32_t i = 0; i < skip && beatIs(r.app, "plink"); ++i) appTick(r.app);
      if (!beatIs(r.app, "plink")) {
        r.stop();
        continue;
      }
      Event e;
      e.type = EV_DOORBELL;
      e.arg = 0;
      appPostEvent(r.app, e);
      appTick(r.app);
      if (!w->react.active) {
        r.stop();
        continue;
      }
      ++tried;
      ++ok_here;
      const bool rifle = w->day.kind == DAY_PLINK && plinkRifleOut(w->day.plink.p);
      if (rifle) {
        ++rifle_bells;
        looked += w->react.look_only ? 1 : 0;
        CHECK(w->react.look_only, "%s %c moment %d: the rifle is in his hands and he does more than look", "plink", 'A' + var, at);
      }
      const int facing0 = w->hiker.facing;
      const Snap s0 = snapOf(*w);
      bool still = true, face_kept = true;
      int n = 0;
      while (w->react.active && n < 200) {
        appTick(r.app);
        ++n;
        if (w->react.active) {
          if (!sameSnap(s0, snapOf(*w))) {
            if (still) {
              const Snap s1 = snapOf(*w);
              const unsigned char *x = (const unsigned char*)&s0.p, *y = (const unsigned char*)&s1.p;
              size_t k = 0;
              while (k < sizeof s0.p && x[k] == y[k]) ++k;
              printf("  first change at byte %u of PlinkState (phase %d t %.2f up %.2f kneel %.2f; mt %.3f vs %.3f)", (unsigned)k, (int)s0.p.phase, (double)s0.p.t, (double)s0.p.up,
                     (double)s0.p.kneel, (double)s0.mt, (double)s1.mt);
              putchar(10);
            }
            still = false;
          }
          if (rifle && w->hiker.facing != facing0) face_kept = false;
        }
      }
      CHECK(n >= 80 && n <= 90, "plink %c: the reaction lasted %d ticks", 'A' + var, n);
      CHECK(still, "plink %c seed %d moment %d: the piece or the beat's clock moved during the reaction", 'A' + var, (int)seed, at);
      CHECK(face_kept, "plink %c moment %d: he turned during the reaction with the rifle in his hands", 'A' + var, at);
      froze += still;
      for (int i = 0; i < 40; ++i) appTick(r.app);
      CHECK(!sameSnap(s0, snapOf(*w)), "plink %c: nothing moved after the reaction", 'A' + var);
      for (int guard = 0; beatIs(r.app, "plink") && guard < 5000; ++guard) appTick(r.app);
      CHECK(appSpineOverruns(r.app) == 0 && appCritterCount(r.app) == 0, "plink %c: after the bell %d overruns, %d critters", 'A' + var, appSpineOverruns(r.app),
            appCritterCount(r.app));
      r.stop();
    }
    CHECK(ok_here >= 4, "plink %c: only %d bells found him able to react", 'A' + var, ok_here);
  }
  printf("  %d bells rung into the beats, the piece still through the whole reaction in %d; %d with the rifle out, all look-only\n", tried, froze, rifle_bells);
  CHECK(froze >= 24, "only %d bells found him able to react", froze);
  CHECK(rifle_bells >= 6 && looked == rifle_bells, "%d bells with the rifle out, %d look-only", rifle_bells, looked);
}

// ---- [5] the dogs ------------------------------------------------------------------------------------------------------------------

static void testDogs() {
  printf("[5] a dog that comes out while it plays stays out of the scene until it has ended\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  const uint64_t seed = g_seed[0][0] ? g_seed[0][0] : 1;
  Rig r;
  CHECK(r.start(seed, day), "start");
  World* w = const_cast<World*>(appWorld(r.app));
  const uint32_t len = appLoopTicks(r.app);
  uint32_t t = 0;
  for (; t < len && !beatIs(r.app, "plink"); ++t) appTick(r.app);
  for (int i = 0; i < 125; ++i) appTick(r.app);   // 5 s into the set-up
  {
    Inputs with_dog = day;
    with_dog.dog_out = 1;
    appSetInputs(r.app, with_dog);   // Lucy is out from here on
  }
  int in_scene = 0, ticks = 0;
  for (; beatIs(r.app, "plink") && t < len + 4000; ++t) {
    appTick(r.app);
    appRender(r.app, r.surf);
    ++ticks;
    if (beatIs(r.app, "plink") && !houseDogsClear(*w)) ++in_scene;
  }
  CHECK(ticks > 1500, "the beat ran only %d ticks", ticks);
  CHECK(in_scene == 0, "a house dog was in the scene on %d ticks of the plinking", in_scene);
  // After it: the dog comes in.
  int seen = 0;
  for (int i = 0; i < 25 * 90 && !seen; ++i) {
    appTick(r.app);
    if (!houseDogsClear(*w)) seen = 1;
  }
  CHECK(seen, "no dog came in after the plinking");
  r.stop();
}

// ---- [6] determinism, the App's size ---------------------------------------------------------------------------------------------------

static void testDeterminism() {
  printf("[6] two runs of each variant draw the same frames\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  for (int var = 0; var < 3; ++var) {
    if (!g_seed[var][0]) continue;
    const Ev a = playBeat(g_seed[var][0], day, var, false, true, 9);
    const Ev b = playBeat(g_seed[var][0], day, var, false, true, 9);
    CHECK(a.hash == b.hash, "plink %c: two runs drew different frames", 'A' + var);
  }
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));   // zeroed
  printf("sizeof(DayPlink) = %u, sizeof(DayBike) = %u\n", (unsigned)sizeof(DayPlink), (unsigned)sizeof(DayBike));
  CHECK(sizeof(DayPlink) <= sizeof(DayBike), "the plinking must fit in the bike's place");
  testPlan();
  testPlays();
  testSweep();
  testNoShow();
  testFreeze();
  testDogs();
  testDeterminism();
  printf("sizeof(App) = %u bytes (budget %d, wire limit 30000)\n", (unsigned)appAppBytes(), SS_APP_BYTES);
  CHECK(appAppBytes() <= 30000u, "App is %u bytes", (unsigned)appAppBytes());
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
