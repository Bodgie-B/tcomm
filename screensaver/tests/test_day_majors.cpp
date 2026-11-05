// Tests for the kite and the boomerang majors (core/scene/day_kite.cpp, day_boomerang.cpp;
// MORNING_DAYTIME_PLAN.md sections 2 and 3), wired into the midday family:
//
//   [1] the plan: neither at night, in the other families or with the night rule on; the kite
//       never in a rain, snow or storm the OUTSIDE data says is there (and in wind, heat, chill
//       and clear it does play); the boomerang not while one is stuck in the camp pine; how
//       many midday loops play each
//   [2] each variant plays to its end in the real scene: no overrun, the script done with 3 s to
//       spare, no critter left, him seated at rest with his hands empty and the hat on, the kite
//       hidden, the boomerang gone (or stuck), him and the kite and the boomerang in frame (but
//       for the kite B's deliberate exit and the eagle's flight in and out), everything out of the
//       chip's box (x 538..796, y 412..476), each variant's own events seen
//   [3] a kite planned before a random rain stays in his pocket (KITE_NOSHOW); the kite feels the
//       weather's wind
//   [4] the doorbell freeze: the kite, the boomerang, the eagle and the beat's clock stand still
//       for the whole reaction, and the beat carries on and ends clean
//   [5] a boomerang stuck in the camp pine stays there for the rest of the loop and the whole of the
//       next, drawn in the tree of that loop's landscape, then it is gone and may be thrown again
//   [6] drawn tick by tick each variant equals a full render; strips per frame; two runs agree
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/boomerang.h"
#include "scene/day_boomerang.h"
#include "scene/day_kite.h"
#include "scene/kite.h"
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

// The preview's inputs for --mood M --spine midday [--night] [--weather W].
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
// The chip's box (scene/chip.h): nothing of the scene may be in it.
static bool inChipBox(const Rect& r) { return r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }
static bool inFrame(const Rect& r) { return r.x0 >= 0 && r.y0 >= 0 && r.x1 <= SS_WIDTH && r.y1 <= SS_HEIGHT; }
static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }
// The kite's state, or a hidden one when the daytime union (scene/day.h) holds something else.
static const KiteState& kiteOf(const World& w) {
  static const KiteState none{};
  return w.day.kind == DAY_KITE ? w.day.kite : none;
}

static int indexOf(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return i;
  return -1;
}

// The first seed whose loop-0 plan has major `id` with variant `var` (the low two bits), or 0.
static uint64_t findSeed(int id, int var, const Inputs& in, uint64_t from = 1, uint64_t to = 600) {
  for (uint64_t seed = from; seed <= to; ++seed) {
    Rig r;
    if (!r.start(seed, in)) return 0;
    const World* w = appWorld(r.app);
    const LoopPlan& p = w->plan;
    const int i = indexOf(p, id);
    // A loop that opens in random rain, snow or storm plays the no-show instead (KITE_NOSHOW).
    const int k = w->weather.kind;
    const bool wet = k == WX_RAIN || k == WX_SNOW || k == WX_STORM;
    const bool ok = i >= 0 && (p.major_var[i] & 3) == var && !wet;
    r.stop();
    if (ok) return seed;
  }
  return 0;
}

// ---- [1] the plan ----------------------------------------------------------------------------------

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

// How many of `n` plans (fresh memory) contain `id`.
static int plansWith(int id, int n, const Inputs& in, const MajorSlots& sl, uint8_t boom_stuck = 0) {
  int hits = 0;
  for (int seed = 1; seed <= n; ++seed) {
    Rng rng;
    rng.seed((uint64_t)seed);
    LoopPlan p;
    memset(&p, 0, sizeof p);
    Memory mem;
    memset(&mem, 0, sizeof mem);
    mem.boom_stuck = boom_stuck;
    majorsPlan(p, rng, in, mem, sl);
    hits += indexOf(p, id) >= 0;
  }
  return hits;
}

static void testPlan() {
  printf("[1] the plan: gating and how often\n");
  const MajorSlots mid = middaySlots();
  const int N = 600;
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  const int kite = plansWith(MJ_KITE, N, day, mid), boom = plansWith(MJ_BOOMERANG, N, day, mid);
  const int fetch = plansWith(MJ_FETCH, N, day, mid);
  printf("  midday, %d fresh plans: kite %.1f %%, boomerang %.1f %%, fetch %.1f %%\n", N, 100.0 * kite / N, 100.0 * boom / N,
         100.0 * fetch / N);
  CHECK(kite > N / 10 && kite < N / 2, "kite in %d of %d midday plans", kite, N);
  CHECK(boom > N / 10 && boom < N / 2, "boomerang in %d of %d midday plans", boom, N);

  // Night: the night rule on, or a night mood (a forced midday family can meet either).
  const Inputs night1 = makeInputs(3.f, SF_MIDDAY, true), night2 = makeInputs(0.2f, SF_MIDDAY), night3 = makeInputs(5.9f, SF_MIDDAY);
  for (const Inputs* in : {&night1, &night2, &night3}) {
    CHECK(plansWith(MJ_KITE, N, *in, mid) == 0, "a kite at night (mood %.1f, night mode %d)", (double)in->mood, (int)in->night_mode);
    CHECK(plansWith(MJ_BOOMERANG, N, *in, mid) == 0, "a boomerang at night (mood %.1f, night mode %d)", (double)in->mood, (int)in->night_mode);
  }
  CHECK(plansWith(MJ_FETCH, N, night1, mid) > 0, "(the night test does not empty the pool)");
  // The OUTSIDE data (weather_code: 0 clear, 1 rain, 2 snow, 3 wind, 4 heat, 5 fog, 6 chill, 7 storm).
  static const char* const kName[8] = {"clear", "rain", "snow", "wind", "heat", "fog", "chill", "storm"};
  for (int code = 0; code < 8; ++code) {
    const Inputs in = makeInputs(3.f, SF_MIDDAY, false, true, code);
    const int k = plansWith(MJ_KITE, N, in, mid), b = plansWith(MJ_BOOMERANG, N, in, mid);
    const bool wet = code == 1 || code == 2 || code == 7;
    printf("  data says %-5s: kite %3d, boomerang %3d of %d plans\n", kName[code], k, b, N);
    if (wet) CHECK(k == 0, "a kite planned in %s", kName[code]);
    else CHECK(k > N / 10, "only %d kites planned in %s", k, kName[code]);
    CHECK(b > N / 10, "only %d boomerangs planned in %s", b, kName[code]);
  }
  // Still stuck in the pine: no boomerang. Gone: back in the pool.
  CHECK(plansWith(MJ_BOOMERANG, N, day, mid, 1) == 0, "a boomerang planned while one is stuck (1)");
  CHECK(plansWith(MJ_BOOMERANG, N, day, mid, 2) == 0, "a boomerang planned while one is stuck (2)");
  CHECK(plansWith(MJ_KITE, N, day, mid, 2) >= kite, "the stuck boomerang took kites out of the plans");
  // The other families never play either (their masks).
  int other = 0;
  for (int fam = 0; fam < SF_COUNT; ++fam) {
    if (fam == SF_MIDDAY || fam == SF_LOOKOUT) continue;
    MajorSlots sl = mid;
    sl.family = (uint8_t)fam;
    sl.open = fam == SF_EVENING ? 15 : 7;
    for (float mood : {1.5f, 3.f, 4.f}) {
      const Inputs in = makeInputs(mood, fam);
      other += plansWith(MJ_KITE, 200, in, sl) + plansWith(MJ_BOOMERANG, 200, in, sl);
    }
  }
  CHECK(other == 0, "%d kite or boomerang plans in the evening or the morning", other);
}

// ---- [2] each variant plays ----------------------------------------------------------------------------

struct Ev {
  bool kite_mode[KITE_MODES];
  int ground_enters, lap, flew_after_lap;
  int hidden_ticks;      // the hiker out of sight
  bool back_in;          // ... and in sight again
  bool phase[8];
  bool eagle, eagle_in_frame, boom_in_hand;
  int beat_ticks, done_at, overruns, critters;
  bool end_ok;
  char why[80];
  long strips_sum;
  int strips_n, strips_max;
  int diffs, checks;
};

// What a beat must leave behind (checked on the tick it ended).
static bool atRest(const World& w, bool boom_stuck_ok, char* why, size_t n) {
  const Hiker& h = w.hiker;
  const char* bad = nullptr;
  if (!h.hat_on) bad = "hat off";
  else if (h.carry != CARRY_NONE) bad = "carrying";
  else if (h.walking) bad = "walking";
  else if (w.fx.arm_w[0] != 0.f || w.fx.arm_w[1] != 0.f || w.fx.lean != 0.f || w.fx.head != 0.f || w.fx.lift != 0.f) bad = "arms or lean still set";
  else if (kiteOf(w).mode != KITE_HIDDEN) bad = "the kite is still out";
  else if (w.boom.eagle.on) bad = "the eagle is still out";
  else if (w.boom.boom.on && !(boom_stuck_ok && w.boom.phase == BP_STUCK)) bad = "the boomerang is still out";
  else if (w.major.u0 != 0.f && w.major.id == MJ_KITE) bad = "scripted wind left on";
  else if (h.target != POSE_SIT) bad = "not sitting";
  if (bad) snprintf(why, n, "%s", bad);
  return bad == nullptr;
}

// Play loop 0 of `seed` (midday) through the beat `name`, checking every tick; `b` shadows it for the
// incremental-versus-full checks. Returns the events seen.
static Ev playBeat(uint64_t seed, const Inputs& in, const char* name, int id, int var, bool b_exits_frame, bool boom_stuck_ok,
                   void (*each_tick)(const World&, App*, int) = nullptr, bool check_draw = true, bool quiet = false) {
  Ev ev;
  memset(&ev, 0, sizeof ev);
  Rig a, b;
  const bool ok = a.start(seed, in) && b.start(seed, in);
  CHECK(ok, "start seed %d", (int)seed);
  if (!ok) return ev;
  const World* w = appWorld(a.app);
  const uint32_t len = appLoopTicks(a.app);
  char tag[48];
  snprintf(tag, sizeof tag, "%s %c seed %d", name, 'A' + var, (int)seed);
  CHECK(indexOf(w->plan, id) >= 0, "%s: not in the plan", tag);
  uint32_t t = 0;
  bool in_beat = false, ended = false;
  KiteMode last_mode = KITE_HIDDEN;
  int bad_hiker = 0, bad_kite = 0, bad_boom = 0, bad_box = 0, n_in = 0;
  for (; t + 1 < len && !ended; ++t) {
    appTick(a.app);
    appTick(b.app);
    const bool now = beatIs(a.app, name);
    if (!now && !in_beat) {   // lead-up: no drawing until just before it (marks accumulate in the dirty mask)
      if (t % 250 == 0) appRender(a.app, a.surf);
      if (t + 60 < len && !beatIs(a.app, "build")) {
        // render every tick for the last 50 ticks before the beat: unknown from here, so render every 5th tick
        if (t % 5 == 0) appRender(a.app, a.surf);
      }
      continue;
    }
    if (!now && in_beat) {   // the tick it ended: majorEnd has run
      ended = true;
      ev.beat_ticks = (int)w->spine.run.beat_ticks;
      ev.done_at = (int)w->spine.run.done_at;
      ev.overruns = appSpineOverruns(a.app);
      ev.critters = appCritterCount(a.app);
      ev.end_ok = atRest(*w, boom_stuck_ok, ev.why, sizeof ev.why);
      break;
    }
    in_beat = true;
    ++n_in;
    const int strips = appRender(a.app, a.surf);
    ev.strips_sum += strips;
    ++ev.strips_n;
    ev.strips_max = strips > ev.strips_max ? strips : ev.strips_max;
    if (check_draw && n_in % 29 == 0) {
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
    // The hiker.
    Rect hr;
    const bool vis = appHikerBounds(a.app, &hr);
    if (vis) {
      if (b_exits_frame && (hr.x1 > SS_WIDTH || hr.x0 < 0)) {   // (kite B: dragged off the edge, back in from it)
      } else if (!inFrame(hr)) {
        ++bad_hiker;
      }
      if (inChipBox(hr)) ++bad_box;
      if (ev.hidden_ticks > 0) ev.back_in = true;
    } else {
      ++ev.hidden_ticks;
    }
    // The kite.
    const KiteState& k = kiteOf(*w);
    ev.kite_mode[k.mode] = true;
    if (k.mode == KITE_GROUND && last_mode != KITE_GROUND) ++ev.ground_enters;
    if (k.mode == KITE_HELD && k.lay > 0.5f) ++ev.lap;
    if (k.mode == KITE_FLY && ev.lap > 0) ++ev.flew_after_lap;
    last_mode = (KiteMode)k.mode;
    if (k.mode != KITE_HIDDEN) {
      const Rect kb = kiteBounds(k);
      if (!kb.empty()) {
        if (inChipBox(kb)) ++bad_box;
        const bool leaving = b_exits_frame && (k.mode == KITE_PULL || k.mode == KITE_GONE);
        if (!leaving && !inFrame(kb)) {
          if (bad_kite < 3) printf("  %s tick %d: kite bounds %d,%d,%d,%d mode %d\n", tag, n_in, kb.x0, kb.y0, kb.x1, kb.y1, k.mode);
          ++bad_kite;
        }
      }
    }
    // The boomerang and the eagle.
    const BoomFlight& f = w->boom;
    if (f.boom.on) {
      ev.phase[f.phase] = true;
      const Rect bb = boomerangBounds(f.boom);
      if (!bb.empty()) {
        if (inChipBox(bb)) ++bad_box;
        if (!inFrame(bb)) ++bad_boom;
      }
    }
    if (f.eagle.on) {
      ev.eagle = true;
      const Rect eb = eagleBounds(f.eagle);
      if (inChipBox(eb)) ++bad_box;
      if (inFrame(eb)) ev.eagle_in_frame = true;
    }
    if (f.boom.on && f.phase == BP_HAND) ev.boom_in_hand = true;
    if (each_tick) each_tick(*w, a.app, n_in);
  }
  CHECK(ended, "%s: the beat never ended", tag);
  CHECK(bad_hiker == 0, "%s: %d ticks with him out of frame", tag, bad_hiker);
  CHECK(bad_kite == 0, "%s: %d ticks with the kite out of frame", tag, bad_kite);
  CHECK(bad_boom == 0, "%s: %d ticks with the boomerang out of frame", tag, bad_boom);
  CHECK(bad_box == 0, "%s: %d samples in the chip's box (x 538..796, y 412..476)", tag, bad_box);
  CHECK(ev.overruns == 0, "%s: %d overruns", tag, ev.overruns);
  CHECK(ev.critters == 0, "%s: %d critters left", tag, ev.critters);
  CHECK(ev.done_at > 0 && ev.beat_ticks - ev.done_at >= 75, "%s: script done at %d of %d ticks (wants 3 s spare)", tag, ev.done_at, ev.beat_ticks);
  CHECK(ev.end_ok, "%s: ends with %s", tag, ev.why);
  CHECK(ev.diffs == 0, "%s: %d checkpoints where incremental and full renders differ", tag, ev.diffs);
  printf("  %-22s %4d beat ticks, script done at %d (%.1f s spare), strips avg %.2f max %d, %d full-render checks\n", tag, ev.beat_ticks,
         ev.done_at, (double)(ev.beat_ticks - ev.done_at) / 25.0, ev.strips_n ? (double)ev.strips_sum / (double)ev.strips_n : 0.0, ev.strips_max,
         ev.checks);
  // (majors may average 10 strips a frame, in the weather the scene has by day: rain and snow dirty every strip on their own)
  const bool precip = w->weather.kind == WX_RAIN || w->weather.kind == WX_SNOW || w->weather.kind == WX_STORM;
  // (the heat's shimmer dirties the far ridge's rows by itself: 11 there, since the pool grew and the seeds moved)
  const double strip_limit = w->weather.kind == WX_HEAT ? 11.0 : 10.0;
  CHECK(precip || ev.strips_n == 0 || (double)ev.strips_sum / (double)ev.strips_n <= strip_limit, "%s: %.2f strips a frame (weather %s)", tag,
        ev.strips_n ? (double)ev.strips_sum / (double)ev.strips_n : 0.0, weatherKindName(w->weather.kind));
  a.stop();
  b.stop();
  return ev;
}

static uint64_t g_seed[2][3];

static void testPlays() {
  printf("[2] each variant plays to the end in the real scene; [6] drawn tick by tick it equals a full render\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  for (int var = 0; var < 3; ++var) {
    g_seed[0][var] = findSeed(MJ_KITE, var, day);
    g_seed[1][var] = findSeed(MJ_BOOMERANG, var, day);
    CHECK(g_seed[0][var] != 0, "no seed plays the kite variant %d", var);
    CHECK(g_seed[1][var] != 0, "no seed plays the boomerang variant %d", var);
  }
  // Kite.
  if (g_seed[0][0]) {
    const Ev e = playBeat(g_seed[0][0], day, "kite", MJ_KITE, 0, false, false);
    CHECK(e.kite_mode[KITE_FLY] && e.kite_mode[KITE_REEL] && e.kite_mode[KITE_HELD], "kite A: flew, reeled in, held");
    CHECK(!e.kite_mode[KITE_PULL] && !e.kite_mode[KITE_GONE] && !e.kite_mode[KITE_NOSE], "kite A: serene means no pull, no loss, no crash");
    CHECK(e.hidden_ticks == 0, "kite A: he left the picture");
  }
  if (g_seed[0][1]) {
    const Ev e = playBeat(g_seed[0][1], day, "kite", MJ_KITE, 1, true, false);
    CHECK(e.kite_mode[KITE_PULL] && e.kite_mode[KITE_GONE], "kite B: pulled, then lost");
    CHECK(e.hidden_ticks >= 25 * 3 && e.back_in, "kite B: a beat of empty scene (%d ticks), then back in (%d)", e.hidden_ticks, (int)e.back_in);
  }
  if (g_seed[0][2]) {
    const Ev e = playBeat(g_seed[0][2], day, "kite", MJ_KITE, 2, false, false);
    CHECK(e.kite_mode[KITE_NOSE] && e.kite_mode[KITE_SPIN] && e.kite_mode[KITE_DROP], "kite C: a nosedive, a spin and a crash, a drop");
    CHECK(e.ground_enters >= 3, "kite C: came down %d times", e.ground_enters);
    CHECK(e.lap > 10 && e.flew_after_lap > 25, "kite C: in his lap %d ticks, then flying %d", e.lap, e.flew_after_lap);
  }
  // Boomerang.
  if (g_seed[1][0]) {
    const Ev e = playBeat(g_seed[1][0], day, "boomerang", MJ_BOOMERANG, 0, false, false);
    CHECK(e.phase[BP_FLY] && e.phase[BP_HAND] && !e.eagle && e.boom_in_hand, "boomerang A: thrown and caught (fly %d hand %d eagle %d)", e.phase[BP_FLY],
          e.phase[BP_HAND], (int)e.eagle);
  }
  if (g_seed[1][1]) {
    const Ev e = playBeat(g_seed[1][1], day, "boomerang", MJ_BOOMERANG, 1, false, false);
    CHECK(e.eagle && e.eagle_in_frame && e.phase[BP_CARRIED], "boomerang B: the eagle took it (eagle %d in frame %d carried %d)", (int)e.eagle,
          (int)e.eagle_in_frame, e.phase[BP_CARRIED]);
  }
  if (g_seed[1][2]) {
    const Ev e = playBeat(g_seed[1][2], day, "boomerang", MJ_BOOMERANG, 2, false, true);
    CHECK(e.phase[BP_STUCK] && !e.eagle, "boomerang C: it stuck");
  }
}

// The first `n` seeds (from `from`) whose loop-0 plan has major `id` with variant `var`.
static int findSeeds(int id, int var, const Inputs& in, uint64_t* out, int n, uint64_t from = 1, uint64_t to = 900) {
  int k = 0;
  for (uint64_t seed = from; seed <= to && k < n; ++seed) {
    Rig r;
    if (!r.start(seed, in)) return k;
    const LoopPlan& p = appWorld(r.app)->plan;
    const int i = indexOf(p, id);
    if (i >= 0 && (p.major_var[i] & 3) == var) out[k++] = seed;
    r.stop();
  }
  return k;
}

// Every variant over more seeds (the layouts differ: the fire, the stream, the pine): no overrun, in frame, out of
// the chip's box, the script done with 3 s to spare, at rest at the end. (No full renders: [2] and [6] did those.)
static void testSweep() {
  printf("[2b] every variant over more seeds\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  int plays = 0;
  for (int id = 0; id < 2; ++id)
    for (int var = 0; var < 3; ++var) {
      uint64_t seeds[8];
      const int n = findSeeds(id == 0 ? MJ_KITE : MJ_BOOMERANG, var, day, seeds, 8, 40);
      CHECK(n >= 6, "only %d seeds for %s %c", n, id == 0 ? "kite" : "boomerang", 'A' + var);
      for (int k = 0; k < n; ++k) {
        // (a random rain or storm in the loop turns a kite into the no-show: it is checked in [3]; skip those here)
        Rig q;
        q.start(seeds[k], day);
        const World* qw = appWorld(q.app);
        const bool wet = qw->weather.kind == WX_RAIN || qw->weather.kind == WX_SNOW || qw->weather.kind == WX_STORM;
        q.stop();
        if (id == 0 && wet) continue;
        playBeat(seeds[k], day, id == 0 ? "kite" : "boomerang", id == 0 ? MJ_KITE : MJ_BOOMERANG, var, id == 0 && var == 1,
                 id == 1 && var == 2, nullptr, false, true);
        ++plays;
      }
    }
  printf("  %d beats played\n", plays);
}

// At night (the night rule on, or a night mood) a forced midday loop plays neither, over real loops.
static void testNight() {
  printf("[1b] night, in real loops\n");
  int seen = 0, loops = 0;
  const Inputs ins[3] = {makeInputs(3.f, SF_MIDDAY, true), makeInputs(0.2f, SF_MIDDAY), makeInputs(5.8f, SF_MIDDAY, false, true, 0)};
  for (const Inputs& in : ins)
    for (uint64_t seed = 1; seed <= 80; ++seed) {
      Rig r;
      r.start(seed, in);
      const LoopPlan& p = appWorld(r.app)->plan;
      seen += indexOf(p, MJ_KITE) >= 0 || indexOf(p, MJ_BOOMERANG) >= 0;
      ++loops;
      r.stop();
    }
  printf("  %d night loops, %d with a kite or a boomerang\n", loops, seen);
  CHECK(seen == 0, "%d night loops planned a kite or a boomerang", seen);
}

// ---- [3] a random rain, and the wind ---------------------------------------------------------------------

static int g_kite_seen = 0;
static void seeKite(const World& w, App*, int) { g_kite_seen += kiteOf(w).mode != KITE_HIDDEN; }
static float g_wind_max, g_wind_early;
static void feelWind(const World& w, App*, int n) {
  if (kiteOf(w).mode == KITE_FLY && n > 600) g_wind_max = kiteOf(w).wind > g_wind_max ? kiteOf(w).wind : g_wind_max;
  if (kiteOf(w).mode == KITE_FLY && n > 400 && n < 460) g_wind_early = kiteOf(w).wind;
}

static void testWeather() {
  printf("[3] a rain the plan could not know of; the wind the kite feels\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  // The weather is drawn after the plan: put a rain (or a storm, or snow) into a loop whose plan has the kite.
  static const int kKinds[3] = {WX_RAIN, WX_SNOW, WX_STORM};
  for (int j = 0; j < 3; ++j) {
    const uint64_t seed = g_seed[0][j] ? g_seed[0][j] : findSeed(MJ_KITE, j, day);
    Rig a;
    CHECK(a.start(seed, day), "start");
    World* w = const_cast<World*>(appWorld(a.app));
    w->weather.kind = (uint8_t)kKinds[j];
    int none = 0;
    uint32_t t = 0;
    bool in_beat = false;
    for (; t + 1 < appLoopTicks(a.app); ++t) {
      appTick(a.app);
      if (beatIs(a.app, "kite")) {
        in_beat = true;
        none += kiteOf(*w).mode != KITE_HIDDEN;
      } else if (in_beat) {
        break;
      }
    }
    CHECK(in_beat, "the kite beat played");
    CHECK(none == 0, "%s: the kite showed on %d ticks", weatherKindName(kKinds[j]), none);
    CHECK(appSpineOverruns(a.app) == 0 && (w->major.var & KITE_NOSHOW) != 0, "%s: overruns %d, no-show flag %d", weatherKindName(kKinds[j]),
          appSpineOverruns(a.app), (int)((w->major.var & KITE_NOSHOW) != 0));
    CHECK(w->hiker.hat_on && !w->hiker.walking && w->hiker.target == POSE_SIT, "%s: he is not seated at rest", weatherKindName(kKinds[j]));
    a.stop();
  }
  // The wind: the same kite A in a calm and in a hard wind with gusts (the weather's own fields).
  const uint64_t seed = g_seed[0][0] ? g_seed[0][0] : findSeed(MJ_KITE, 0, day);
  for (int windy = 0; windy < 2; ++windy) {
    Rig a;
    CHECK(a.start(seed, day), "start");
    World* w = const_cast<World*>(appWorld(a.app));
    g_wind_max = g_wind_early = 0.f;
    uint32_t t = 0;
    bool in_beat = false;
    for (; t + 1 < appLoopTicks(a.app); ++t) {
      if (windy) {   // (what weatherStep would hold on a windy day: a steady -0.9 and a gust envelope near 1)
        w->weather.wind = -0.9f;
        w->weather.gust = 0.9f;
      }
      appTick(a.app);
      if (beatIs(a.app, "kite")) {
        in_beat = true;
        static int n;
        n = (int)(t % 100000);
        (void)n;
        feelWind(*w, a.app, (int)w->major.t * 25);
      } else if (in_beat) {
        break;
      }
    }
    printf("  weather wind %s: the kite felt up to %.2f\n", windy ? "-0.9 gust 0.9" : "none", (double)g_wind_max);
    if (windy) CHECK(g_wind_max > 0.7f, "a hard wind: the kite felt %.2f", (double)g_wind_max);
    else CHECK(g_wind_max < 0.8f && g_wind_max > 0.25f, "a calm day: the kite felt %.2f", (double)g_wind_max);
    a.stop();
  }
  (void)seeKite;
}

// ---- [4] the doorbell -------------------------------------------------------------------------------------

struct Snap {
  float kx, ky, kt, kr, bx, by, bt, ex, ey, mt;
  uint8_t km, bp;
};
static Snap snapOf(const World& w) {
  Snap s;
  memset(&s, 0, sizeof s);
  // (A kite in his hand, or a loose line, goes where his hand goes, and a boomerang in his hand does too: their
  // positions are not the story's.)
  const bool kite_free = kiteOf(w).mode != KITE_HELD && kiteOf(w).mode != KITE_GONE && kiteOf(w).mode != KITE_HIDDEN;
  s.kx = kite_free ? kiteOf(w).pos.x : 0.f;
  s.ky = kite_free ? kiteOf(w).pos.y : 0.f;
  s.kt = kiteOf(w).t;
  s.kr = kiteOf(w).rot + kiteOf(w).phase + kiteOf(w).wind + kiteOf(w).fold + kiteOf(w).lay + (kiteOf(w).mode == KITE_GROUND ? 0.f : kiteOf(w).r);   // (the slack on the ground follows his hand)
  s.km = kiteOf(w).mode;
  const bool boom_free = w.boom.boom.on && w.boom.phase != BP_WAIT && w.boom.phase != BP_HAND;
  s.bx = boom_free ? w.boom.boom.pos.x : 0.f;
  s.by = boom_free ? w.boom.boom.pos.y : 0.f;
  s.bt = w.boom.t;
  s.bp = w.boom.phase;
  s.ex = w.boom.eagle.pos.x;
  s.ey = w.boom.eagle.pos.y;
  s.mt = w.major.t;
  return s;
}
// Which fields of two snapshots differ (names), for the failure message.
static void diffSnap(const Snap& a, const Snap& b, char* out, size_t n) {
  out[0] = 0;
  auto add = [&](const char* name, bool d) {
    if (d && strlen(out) + strlen(name) + 2 < n) {
      strcat(out, name);
      strcat(out, " ");
    }
  };
  add("kite.x", a.kx != b.kx);
  add("kite.y", a.ky != b.ky);
  add("kite.t", a.kt != b.kt);
  add("kite.rot/phase/wind/r/fold", a.kr != b.kr);
  add("kite.mode", a.km != b.km);
  add("boom.x", a.bx != b.bx);
  add("boom.y", a.by != b.by);
  add("boom.t", a.bt != b.bt);
  add("boom.phase", a.bp != b.bp);
  add("eagle", a.ex != b.ex || a.ey != b.ey);
  add("major.t", a.mt != b.mt);
}

static void testFreeze() {
  printf("[4] the doorbell freezes the kite, the boomerang, the eagle and the beat\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  struct Case { int id; const char* name; int var; } cases[6] = {{MJ_KITE, "kite", 0}, {MJ_KITE, "kite", 1}, {MJ_KITE, "kite", 2},
                                                                 {MJ_BOOMERANG, "boomerang", 0}, {MJ_BOOMERANG, "boomerang", 1},
                                                                 {MJ_BOOMERANG, "boomerang", 2}};
  int tried = 0, froze = 0;
  for (const Case& c : cases) {
    const uint64_t seed = g_seed[c.id == MJ_KITE ? 0 : 1][c.var];
    if (!seed) continue;
    for (int at = 0; at < 3; ++at) {   // three moments in the beat
      Rig r;
      CHECK(r.start(seed, day), "start");
      const World* w = appWorld(r.app);
      uint32_t t = 0;
      const uint32_t len = appLoopTicks(r.app);
      for (; t < len && !beatIs(r.app, c.name); ++t) appTick(r.app);
      const uint32_t skip = c.id == MJ_KITE ? 300u + 330u * (uint32_t)at : 230u + 190u * (uint32_t)at;
      for (uint32_t k = 0; k < skip && beatIs(r.app, c.name); ++k) appTick(r.app);
      if (!beatIs(r.app, c.name)) {
        r.stop();
        continue;
      }
      Event e;
      e.type = EV_DOORBELL;
      e.arg = 0;
      appPostEvent(r.app, e);
      appTick(r.app);
      if (!w->react.active) {   // he could not react just then (out of frame): nothing to freeze
        r.stop();
        continue;
      }
      ++tried;
      const Snap s0 = snapOf(*w);
      bool still = true;
      char moved[120] = "";
      int n = 0;
      while (w->react.active && n < 200) {
        appTick(r.app);
        ++n;
        if (w->react.active) {
          const Snap s1 = snapOf(*w);
          if (memcmp(&s0, &s1, sizeof s0) != 0) {
            still = false;
            if (moved[0] == 0) diffSnap(s0, s1, moved, sizeof moved);
          }
        }
      }
      CHECK(n >= 80 && n <= 90, "%s %d: the reaction lasted %d ticks", c.name, c.var, n);
      CHECK(still, "%s %c seed %d moment %d: %s moved during the reaction", c.name, 'A' + c.var,
            (int)seed, at, moved);
      froze += still;
      // Carried on: the clocks run again, and the beat ends clean.
      for (int k = 0; k < 40; ++k) appTick(r.app);
      const Snap s2 = snapOf(*w);
      CHECK(memcmp(&s0, &s2, sizeof s0) != 0, "%s %c: nothing moved after the reaction", c.name, 'A' + c.var);
      for (int guard = 0; beatIs(r.app, c.name) && guard < 4000; ++guard) appTick(r.app);
      CHECK(appSpineOverruns(r.app) == 0 && appCritterCount(r.app) == 0, "%s %c: after the bell %d overruns, %d critters", c.name, 'A' + c.var,
            appSpineOverruns(r.app), appCritterCount(r.app));
      char why[80];
      CHECK(atRest(*w, c.id == MJ_BOOMERANG && c.var == 2, why, sizeof why), "%s %c: after the bell he ends with %s", c.name, 'A' + c.var, why);
      r.stop();
    }
  }
  printf("  %d bells rung into the beats, the scene still through the whole reaction in %d\n", tried, froze);
  CHECK(froze >= 10, "only %d bells found him able to react", froze);
}

// ---- [5] stuck in the pine ----------------------------------------------------------------------------------

// The trunk point the major aims at: the camp pine's left face, 18 px above its foot.
static Vec2 trunkOf(const World& w) { return Vec2{(float)w.land.camp_x - 4.5f, (float)w.land.camp.base_y - 18.f}; }

static void testStuck() {
  printf("[5] a boomerang stuck in the camp pine stays for this loop and the next\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  const uint64_t seed = g_seed[1][2];
  CHECK(seed != 0, "no seed plays boomerang C");
  if (!seed) return;
  Rig a;
  CHECK(a.start(seed, day), "start");
  const World* w = appWorld(a.app);
  CHECK(w->memory.boom_stuck == 0 && !w->boom.boom.on, "a fresh memory has no boomerang up there");
  uint32_t len = appLoopTicks(a.app);
  Boomerang final0{};
  Vec2 trunk0{};
  bool stuck_seen = false;
  uint32_t t = 0;
  for (; t + 1 < len; ++t) {
    appTick(a.app);
    if (w->boom.phase == BP_STUCK) {
      stuck_seen = true;
      final0 = w->boom.boom;
      trunk0 = trunkOf(*w);
    }
  }
  CHECK(stuck_seen, "it stuck");
  CHECK(w->memory.boom_stuck == 2, "memory %d after it stuck", (int)w->memory.boom_stuck);
  CHECK(w->boom.phase == BP_STUCK && w->boom.boom.on, "still in the pine at the end of the loop (it stays there the rest of the loop)");
  {
    const Vec2 tip = boomerangTip(w->boom.boom, false);
    CHECK(fabsf(tip.x - trunk0.x) < 0.6f && fabsf(tip.y - trunk0.y) < 0.6f, "tip %.1f,%.1f is not at the trunk point %.1f,%.1f", (double)tip.x, (double)tip.y,
          (double)trunk0.x, (double)trunk0.y);
  }
  char mem[400];
  appMemory(a.app, mem, sizeof mem);
  CHECK(strstr(mem, "boom=2") != nullptr, "memory line: %s", mem);

  // The next loop: it is in the tree of that loop's landscape from the first tick, the plan has no boomerang,
  // and it is drawn once the build is done.
  CHECK(appTick(a.app), "rolled over");
  len = appLoopTicks(a.app);
  CHECK(w->memory.boom_stuck == 1, "memory %d in the next loop", (int)w->memory.boom_stuck);
  CHECK(indexOf(w->plan, MJ_BOOMERANG) < 0, "a boomerang planned while one is stuck");
  CHECK(w->boom.phase == BP_STUCK && w->boom.boom.on, "the next loop starts with it in the pine");
  const Vec2 trunk1 = trunkOf(*w);
  {
    const Vec2 tip = boomerangTip(w->boom.boom, false);
    CHECK(fabsf(tip.x - trunk1.x) < 0.6f && fabsf(tip.y - trunk1.y) < 0.6f, "next loop: tip %.1f,%.1f is not at the trunk point %.1f,%.1f", (double)tip.x,
          (double)tip.y, (double)trunk1.x, (double)trunk1.y);
    // The same boomerang hanging the same way: its angle, and where it hangs from the tip.
    CHECK(fabsf(w->boom.boom.rot - final0.rot) < 1e-4f && w->boom.boom.flip == final0.flip, "angle %.4f flip %d vs %.4f %d", (double)w->boom.boom.rot,
          (int)w->boom.boom.flip, (double)final0.rot, (int)final0.flip);
    const Vec2 off1{w->boom.boom.pos.x - tip.x, w->boom.boom.pos.y - tip.y};
    Boomerang f0 = final0;
    const Vec2 tip0 = boomerangTip(f0, false);
    const Vec2 off0{f0.pos.x - tip0.x, f0.pos.y - tip0.y};
    CHECK(fabsf(off1.x - off0.x) < 0.05f && fabsf(off1.y - off0.y) < 0.05f, "it hangs differently: %.2f,%.2f vs %.2f,%.2f", (double)off1.x, (double)off1.y,
          (double)off0.x, (double)off0.y);
  }
  bool drawn = false;
  for (t = 0; t < 400; ++t) {
    appTick(a.app);
    appRender(a.app, a.surf);
  }
  {   // drawn: switching it off changes the picture inside its bounds, and only there
    const Rect bb = boomerangBounds(w->boom.boom);
    CHECK(!bb.empty() && !inChipBox(bb), "its bounds %d,%d,%d,%d", bb.x0, bb.y0, bb.x1, bb.y1);
    appInvalidate(a.app);
    appRender(a.app, a.surf);
    Pixel* copy = (Pixel*)malloc((size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
    memcpy(copy, a.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
    World* mw = const_cast<World*>(w);
    mw->boom.boom.on = false;
    appInvalidate(a.app);
    appRender(a.app, a.surf);
    long inside = 0, outside = 0;
    for (int y = 0; y < SS_HEIGHT; ++y)
      for (int x = 0; x < SS_WIDTH; ++x) {
        if (copy[y * SS_WIDTH + x] == a.fb[y * SS_WIDTH + x]) continue;
        if (x >= bb.x0 && x < bb.x1 && y >= bb.y0 && y < bb.y1) ++inside;
        else ++outside;
      }
    mw->boom.boom.on = true;
    free(copy);
    drawn = inside > 10;
    CHECK(drawn && outside == 0, "the stuck boomerang: %ld pixels inside its bounds, %ld outside", inside, outside);
  }
  for (t += 0; t + 1 < len && appSpineOverruns(a.app) == 0; ++t) appTick(a.app);
  // (the loop is played out: it is still there at its end, and the memory is down to the last loop)
  CHECK(w->boom.phase == BP_STUCK && w->boom.boom.on, "still in the pine at the end of the next loop");
  CHECK(appTick(a.app), "rolled over again");
  CHECK(w->memory.boom_stuck == 0 && !w->boom.boom.on, "two loops on it is gone (memory %d, on %d)", (int)w->memory.boom_stuck, (int)w->boom.boom.on);
  a.stop();
}

// The loop after a boomerang stuck in the midday's pine can be an evening or a morning: it is in the pine there too,
// drawn, and the strips it marks are the right ones (incremental equals full), the bear's tree and all.
static void testStuckElsewhere() {
  printf("[5b] the stuck boomerang in the evening and in the morning that follow\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  const uint64_t seed = g_seed[1][2];
  if (!seed) return;
  for (int fam = 0; fam < 2; ++fam) {
    Rig a, b;
    CHECK(a.start(seed, day) && b.start(seed, day), "start");
    const World* w = appWorld(a.app);
    const uint32_t len = appLoopTicks(a.app);
    for (uint32_t t = 0; t + 1 < len; ++t) {
      appTick(a.app);
      appTick(b.app);
    }
    const Inputs next = fam == 0 ? makeInputs(4.f, SF_EVENING) : makeInputs(1.5f, SF_MORNING);
    appSetInputs(a.app, next);
    appSetInputs(b.app, next);
    CHECK(appTick(a.app) && appTick(b.app), "rolled over");
    const char* what = fam == 0 ? "evening" : "morning";
    CHECK(appSpineFamily(a.app) == (fam == 0 ? SF_EVENING : SF_MORNING), "%s: family %d", what, appSpineFamily(a.app));
    CHECK(w->boom.phase == BP_STUCK && w->boom.boom.on && w->memory.boom_stuck == 1, "%s: starts with it in the pine (memory %d)", what,
          (int)w->memory.boom_stuck);
    int diffs = 0, checks = 0;
    for (int t = 0; t < 900; ++t) {
      appTick(a.app);
      appTick(b.app);
      appRender(a.app, a.surf);
      if (t > 340 && t % 71 == 0) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        diffs += memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0;
      }
    }
    CHECK(diffs == 0, "%s: %d of %d checkpoints differ", what, diffs, checks);
    const Rect bb = boomerangBounds(w->boom.boom);
    appInvalidate(a.app);
    appRender(a.app, a.surf);
    Pixel* copy = (Pixel*)malloc((size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
    memcpy(copy, a.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
    World* mw = const_cast<World*>(w);
    mw->boom.boom.on = false;
    appInvalidate(a.app);
    appRender(a.app, a.surf);
    long inside = 0, outside = 0;
    for (int y = 0; y < SS_HEIGHT; ++y)
      for (int x = 0; x < SS_WIDTH; ++x) {
        if (copy[y * SS_WIDTH + x] == a.fb[y * SS_WIDTH + x]) continue;
        if (x >= bb.x0 && x < bb.x1 && y >= bb.y0 && y < bb.y1) ++inside;
        else ++outside;
      }
    free(copy);
    CHECK(inside > 10 && outside == 0, "%s: the stuck boomerang drew %ld pixels inside its bounds and %ld outside", what, inside, outside);
    a.stop();
    b.stop();
  }
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));   // zeroed
  testPlan();
  testNight();
  testPlays();
  testSweep();
  testWeather();
  testFreeze();
  testStuck();
  testStuckElsewhere();
  printf("sizeof(App) = %u bytes (budget %d)\n", (unsigned)appAppBytes(), SS_APP_BYTES);
  CHECK(appAppBytes() <= (uint32_t)SS_APP_BYTES, "App is %u bytes", (unsigned)appAppBytes());
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
