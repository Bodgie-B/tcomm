// The hiker's doorbell reaction (scene/react.h): a doorbell while he is in
// frame freezes the story for kReactSeconds (3.4 s, 85 ticks at 25 Hz) while
// he hops, turns to the right, waves and points; with his hands full he only
// hops and looks. Then everything goes back exactly and the story resumes.
//
//  1  a bell while he is visible starts the reaction on the next tick;
//  2  during it the beat name and progress hold still and his rect moves;
//  3  it ends after 85 ticks (+-1), with his facing and World::fx restored
//     byte for byte;
//  4  story equivalence: a run with a bell at tick T, shifted by the freeze,
//     shows the same beat, progress, hiker rect and loop plan as the run
//     without it, over 2000 ticks, and both loops end with no overrun and no
//     forced minor, the loop ending the freeze's length later;
//  5  a bell while he is out of frame (the build-up, away, faded) does nothing;
//  6  a bell in a Lookout loop does nothing;
//  7  two bells 25 ticks apart are one reaction, ending with the first's;
//  8  determinism: the same bell at the same tick renders the same frames,
//     and the incremental render equals a full one all through the reaction;
//  9  with his hands full the near arm stays put and he hops.
// Plus a sweep: bells at many moments of every spine family and several
// weathers, with checks 1-3 and 9 applied to each. Also prints the (seed,
// tick) moments the review contact sheets are made from ("SHEET ..." lines).
// Plain executable; returns non-zero on failure.
#include "ss_app.h"
#include "scene/hiker.h"
#include "scene/react.h"
#include "scene/spine.h"
#include "scene/world.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>

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
    mem = nullptr;
    fb = nullptr;
  }
};

// Inputs. `family` < 0: the loop's family follows the inputs (as the preview's
// does); else it is forced. `weather` < 0: no OUTSIDE reading (the core picks).
static Inputs makeInputs(float mood, int family, int weather) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.have_outside = weather >= 0;
  in.weather_code = weather >= 0 ? weather : 0;
  in.spine_force = family >= 0 ? (uint8_t)(1 + family) : 0;
  return in;
}

static void tickN(Rig& r, int n) {
  for (int i = 0; i < n; ++i) appTick(r.app);
}

static void bellNow(Rig& r) {
  Event e;
  e.type = EV_DOORBELL;
  e.arg = 0;
  appPostEvent(r.app, e);
}

static uint64_t fnv(const void* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  return h;
}

static uint64_t frameHash(const Rig& r) { return fnv(r.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)); }

static bool sameFrame(const Rig& a, const Rig& b) {
  return memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) == 0;
}

static bool sameRect(const Rect& a, const Rect& b) {
  return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

// Everything the equivalence check can see of the story at one moment.
struct Obs {
  char beat[40];
  float prog;
  bool vis;
  Rect rect;
  uint64_t plan;     // hash of appLoopPlan
  char minor[40];
  int minors;        // minor effects started this loop
  int critters;
  int overruns, forced;
  int facing, carry, busy, family, gag, shiver;
  uint32_t tc;       // the timeline's tick count (holds still in the freeze)
};

static void observe(const App* a, Obs& o) {
  memset(&o, 0, sizeof o);
  strncpy(o.beat, appBeatName(a), sizeof o.beat - 1);
  o.prog = appBeatProgress(a);
  Rect r{0, 0, 0, 0};
  o.vis = appHikerRect(a, &r);
  o.rect = r;
  char plan[400];
  appLoopPlan(a, plan, sizeof plan);
  o.plan = fnv(plan, strlen(plan));
  strncpy(o.minor, appMinorName(a), sizeof o.minor - 1);
  o.minors = appMinorCount(a);
  o.critters = appCritterCount(a);
  o.overruns = appSpineOverruns(a);
  o.forced = appMinorForced(a);
  const World* w = appWorld(a);
  o.facing = w->hiker.facing;
  o.carry = (int)w->hiker.carry;
  o.busy = appBusy(a) ? 1 : 0;
  o.family = appSpineFamily(a);
  o.gag = (int)w->weather.gag;
  o.shiver = (w->weather.shiver > 0.f || w->weather.shiver_dx != 0.f) ? 1 : 0;   // chill's shiver (+-1.5 px)
  o.tc = appTickCount(a);
}

// He is in frame and nothing stops a reaction (written from the brief's rule,
// not from react.cpp: in the scene, in frame, not a Lookout loop, not faded).
static bool canReact(const App* a) {
  const World* w = appWorld(a);
  Rect rc{0, 0, 0, 0};
  const bool vis = appHikerRect(a, &rc);
  return vis && !rc.empty() && !w->react.active && appSpineFamily(a) != SF_LOOKOUT && w->fade < 0.5f;
}

// ---------------------------------------------------------------------------
// Checks 1-3 and 9 on the app's current state: ring the bell, run the
// reaction out and look at every tick of it.

// kReactTicks (85) comes from scene/react.h.

struct ReactReport {
  bool started = false;
  int frozen = 0;            // ticks from the bell to the one that ends it, inclusive
  bool hands_full = false;
  int pre_facing = 0;
  int pre_carry = 0;
  bool walking = false;
  bool waved = false;        // the composed pose's near arm went up past 2.0 rad (empty hands; react.cpp reactPose)
  bool hopped = false;
  bool turned = false;
  bool rect_moved = false;
  int facing_bad_first = 0;   // first tick (1-based) in 12..76 on which he did not face right, 0 if none
  int facing_bad_n = 0;
  float max_pose_sh0 = 0.f;  // the composed pose's near shoulder angle, highest over the reaction
  float pre_pose_sh0 = 0.f;
};

static ReactReport runReaction(Rig& r, const char* label) {
  ReactReport rep;
  const World* w = appWorld(r.app);
  if (!canReact(r.app)) return rep;

  // The state at the bell: the tick before the first reaction tick.
  HikerFx pre_fx;
  memcpy(&pre_fx, &w->fx, sizeof pre_fx);
  Obs pre;
  observe(r.app, pre);
  Rect crit_pre[CRITTER_MAX];
  for (int s = 0; s < CRITTER_MAX; ++s) {
    crit_pre[s] = Rect{0, 0, 0, 0};
    appCritterRect(r.app, s, &crit_pre[s], nullptr);
  }
  rep.pre_facing = w->hiker.facing;
  rep.pre_carry = (int)w->hiker.carry;
  rep.hands_full = w->hiker.carry != CARRY_NONE;
  rep.walking = w->hiker.walking;
  rep.pre_pose_sh0 = w->hiker.pose.shoulder[0];
  rep.max_pose_sh0 = -9.f;
  const float pre_lift = w->fx.lift;
  const float pre_arm_w0 = w->fx.arm_w[0];
  float max_lift = pre_lift, max_arm_w0 = pre_arm_w0;
  bool arm_w0_moved = false;
  bool facing_ok_mid = true;
  bool facing_left_before_end = false;
  bool crit_frozen = true;

  bellNow(r);
  int end_tick = 0;
  for (int i = 1; i <= 140; ++i) {
    appTick(r.app);
    const bool active = w->react.active;
    if (i == 1) {
      // Check 1.
      CHECK(active, "%s: bell while visible did not start the reaction (beat %s, facing %d, carry %d)", label, pre.beat,
            pre.facing, pre.carry);
      if (!active) return rep;
      rep.started = true;
    }
    // Check 2: the story holds still on every tick of the freeze, the last
    // one (which ends it) included.
    Obs o;
    observe(r.app, o);
    CHECK(strcmp(o.beat, pre.beat) == 0 && o.prog == pre.prog && o.tc == pre.tc,
          "%s: tick %d of the reaction: beat %s %.5f tc %u, was %s %.5f tc %u", label, i, o.beat, (double)o.prog, o.tc,
          pre.beat, (double)pre.prog, pre.tc);
    CHECK(o.minors == pre.minors && o.critters == pre.critters,
          "%s: tick %d: minors %d->%d critters %d->%d moved in the freeze", label, i, pre.minors, o.minors, pre.critters,
          o.critters);
    for (int s = 0; s < CRITTER_MAX; ++s) {
      Rect c{0, 0, 0, 0};
      appCritterRect(r.app, s, &c, nullptr);
      if (!sameRect(c, crit_pre[s])) crit_frozen = false;
    }
    if (!sameRect(o.rect, pre.rect)) rep.rect_moved = true;
    if (w->fx.lift > max_lift) max_lift = w->fx.lift;
    if (w->fx.arm_w[0] > max_arm_w0) max_arm_w0 = w->fx.arm_w[0];
    if (w->fx.arm_w[0] != pre_arm_w0) arm_w0_moved = true;
    if (w->hiker.pose.shoulder[0] > rep.max_pose_sh0) rep.max_pose_sh0 = w->hiker.pose.shoulder[0];
    // He faces right through the middle of it, whichever way he faced.
    // (Not while a daytime module has his facing: the bike's rider, the walker on the slackline, the ropes course's
    // items. The bell holds him still there: scene/day.h dayHoldsFacing.)
    if (i >= 12 && i <= 76 && w->hiker.facing != 1 && !dayHoldsFacing(*w)) {
      facing_ok_mid = false;
      if (!rep.facing_bad_first) rep.facing_bad_first = i;
      ++rep.facing_bad_n;
    }
    if (w->hiker.facing == -1) facing_left_before_end = true;
    if (!active) {
      end_tick = i;
      break;
    }
  }
  rep.frozen = end_tick;
  CHECK(end_tick != 0, "%s: reaction still running after 140 ticks", label);
  if (!end_tick) return rep;

  // Check 3: its length, his facing, his offsets.
  CHECK(end_tick >= kReactTicks - 1 && end_tick <= kReactTicks + 1, "%s: reaction lasted %d ticks, want %d +-1", label,
        end_tick, kReactTicks);
  CHECK(w->hiker.facing == rep.pre_facing, "%s: facing %d after the reaction, was %d", label, w->hiker.facing,
        rep.pre_facing);
  const bool fx_same = memcmp(&w->fx, &pre_fx, sizeof pre_fx) == 0;
  CHECK(fx_same, "%s: World::fx differs from the tick before the bell after the reaction", label);
  if (!fx_same) {
    // Say which fields moved.
    const float* a = (const float*)&w->fx;
    const float* b = (const float*)&pre_fx;
    for (size_t k = 0; k < sizeof pre_fx / sizeof(float); ++k) {
      if (memcmp(&a[k], &b[k], sizeof(float)) != 0) printf("    fx float #%zu: %g vs %g\n", k, (double)a[k], (double)b[k]);
    }
  }
  CHECK(crit_frozen, "%s: a critter moved during the reaction", label);

  // The story resumes on the next tick.
  appTick(r.app);
  CHECK(appTickCount(r.app) == pre.tc + 1, "%s: the story did not resume after the reaction (tc %u, was %u)", label,
        appTickCount(r.app), pre.tc);

  // His moves.
  rep.hopped = max_lift > pre_lift + 0.02f;
  rep.waved = rep.max_pose_sh0 > 2.0f;
  rep.turned = rep.pre_facing == -1 && facing_ok_mid && !dayHoldsFacing(*w);
  CHECK(rep.rect_moved, "%s: his rect never changed during the reaction", label);
  CHECK(rep.hopped, "%s: fx.lift never rose above %.3f (max %.3f)", label, (double)pre_lift, (double)max_lift);
  CHECK(facing_ok_mid, "%s: he did not face right through the middle of the reaction (was %d; facing %d on %d ticks, first on tick %d)",
        label, rep.pre_facing, w->hiker.facing, rep.facing_bad_n, rep.facing_bad_first);
  if (rep.pre_facing == 1) CHECK(!facing_left_before_end, "%s: facing right and he turned left", label);
  if (rep.hands_full) {
    // Check 9: no wave, only the hop.
    CHECK(!arm_w0_moved, "%s: hands full (carry %d) but fx.arm_w[0] moved from %.3f to max %.3f", label, rep.pre_carry,
          (double)pre_arm_w0, (double)max_arm_w0);
  } else {
    CHECK(rep.waved, "%s: empty hands but the near arm never went up (near shoulder max %.2f)", label,
          (double)rep.max_pose_sh0);
  }
  return rep;
}

// ---------------------------------------------------------------------------
// Moment search: step a run and use appHikerRect / hiker.carry / hiker.facing.

enum MomentKind { MK_RIGHT, MK_LEFT, MK_FULL, MK_GAG };

static bool momentOk(const App* a, MomentKind kind) {
  const World* w = appWorld(a);
  Rect rc, full;
  if (kind == MK_GAG) return canReact(a) && w->weather.gag != WG_NONE;   // a weather gag on him (any pose)
  if (!appHikerRect(a, &rc) || !appHikerBounds(a, &full)) return false;
  if (!canReact(a) || appHikerWalking(a) || w->hiker.blending) return false;
  if (!sameRect(rc, full)) return false;   // all of him in frame
  if (rc.x0 < 40 || rc.x1 > SS_WIDTH - 40 || rc.y0 < 20) return false;
  switch (kind) {
    case MK_RIGHT: return w->hiker.carry == CARRY_NONE && w->hiker.facing == 1;
    case MK_LEFT: return w->hiker.carry == CARRY_NONE && w->hiker.facing == -1;
    case MK_FULL: return w->hiker.carry != CARRY_NONE;
    case MK_GAG: return false;
  }
  return false;
}

// First tick in [from, to] of a fresh run where `kind` holds; -1 if none.
static int findMoment(uint64_t seed, const Inputs& in, MomentKind kind, int from, int to) {
  Rig r;
  if (!r.start(seed, in)) return -1;
  int found = -1;
  for (int t = 0; t <= to; ++t) {
    if (t >= from && momentOk(r.app, kind)) {
      found = t;
      break;
    }
    appTick(r.app);
  }
  r.stop();
  return found;
}

static const char* kKindName[] = {"right", "left", "hands_full", "gag"};

// ---------------------------------------------------------------------------
// Check 4: story equivalence.

static const int kWindow = 2000;
static Obs g_a[kWindow + 1];

struct LoopEnd {
  long roll_tick = -1;   // appTick calls until the loop rolled over
  int max_overruns = 0, max_forced = 0;
  uint64_t next_seed = 0;
};

// Run on to the next loop's start, tracking the loop's overruns and forced minors.
static LoopEnd runToRollover(Rig& r, long ticks_so_far) {
  LoopEnd e;
  long t = ticks_so_far;
  for (int guard = 0; guard < 40 * 60 * SS_TICK_HZ; ++guard) {
    const bool roll = appTick(r.app);
    ++t;
    if (roll) {
      e.roll_tick = t;
      e.next_seed = appLoopSeed(r.app);
      return e;
    }
    const int o = appSpineOverruns(r.app), f = appMinorForced(r.app);
    if (o > e.max_overruns) e.max_overruns = o;
    if (f > e.max_forced) e.max_forced = f;
  }
  return e;
}

static void equivalence(uint64_t seed, const Inputs& in, int T, const char* label) {
  Rig A, B;
  if (!A.start(seed, in) || !B.start(seed, in)) {
    CHECK(false, "%s: start", label);
    return;
  }
  tickN(A, T);
  tickN(B, T);
  CHECK(canReact(B.app), "%s: no reaction possible at tick %d", label, T);
  const uint32_t loop_ticks = appLoopTicks(A.app);
  CHECK((uint32_t)T + 90 + kWindow < loop_ticks, "%s: window crosses the loop end (%u ticks)", label, loop_ticks);

  // Run A: no bell. g_a[k] is A after T + k ticks.
  for (int k = 0; k <= kWindow; ++k) {
    observe(A.app, g_a[k]);
    appTick(A.app);
  }
  const LoopEnd ea = runToRollover(A, T + kWindow + 1);

  // Run B: the bell at T. D = ticks the story stood still.
  bellNow(B);
  int D = 0;
  for (int i = 1; i <= 140; ++i) {
    appTick(B.app);
    if (!appWorld(B.app)->react.active) {
      D = i;
      break;
    }
  }
  CHECK(D >= kReactTicks - 1 && D <= kReactTicks + 1, "%s: reaction lasted %d ticks", label, D);
  int d_beat = 0, d_prog = 0, d_rect = 0, d_rect_nominor = 0, d_rect_gag = 0, d_plan = 0;
  int d_minor_name = 0, d_minor_count = 0, d_crit = 0, d_other = 0;
  int first_minor_diff = -1, last_minor_diff = -1, first_hard = -1;
  int ka_minor = -1, kb_minor = -1;   // the window tick each run's first minor starts at
  int b_minors0 = 0;
  bool prev_gag = false;   // the weather moves him after the rect is made: the rect lags its flags by a tick
  // B after T + D + k ticks is A after T + k ticks.
  for (int k = 0; k <= kWindow; ++k) {
    Obs b;
    observe(B.app, b);
    if (k == 0) b_minors0 = b.minors;
    const Obs& a = g_a[k];
    const bool minor_now = a.minor[0] || b.minor[0];
    const bool gag_cur = a.gag != 0 || b.gag != 0 || a.shiver || b.shiver;   // a weather gag or shiver on him (wind hat, heat fan, chill hug and shiver)
    const bool gag_now = gag_cur || prev_gag;
    prev_gag = gag_cur;
    if (strcmp(a.beat, b.beat) != 0) {
      ++d_beat;
      if (first_hard < 0) first_hard = k;
    }
    if (a.prog != b.prog) {
      ++d_prog;
      if (first_hard < 0) first_hard = k;
    }
    if (a.plan != b.plan) {
      ++d_plan;
      if (first_hard < 0) first_hard = k;
    }
    if (a.vis != b.vis || !sameRect(a.rect, b.rect)) {
      ++d_rect;
      if (!minor_now && gag_now) ++d_rect_gag;
      if (!minor_now && !gag_now) ++d_rect_nominor;
      if (first_hard < 0 && !minor_now && !gag_now) first_hard = k;
    }
    if (strcmp(a.minor, b.minor) != 0) {
      ++d_minor_name;
      if (first_minor_diff < 0) first_minor_diff = k;
      last_minor_diff = k;
    }
    if (a.minors != b.minors) ++d_minor_count;
    if (a.critters != b.critters) ++d_crit;
    if (a.facing != b.facing || a.carry != b.carry || a.busy != b.busy || a.family != b.family) ++d_other;
    if (ka_minor < 0 && a.minors > g_a[0].minors) ka_minor = k;
    if (kb_minor < 0 && b.minors > b_minors0) kb_minor = k;
    if (k < kWindow) appTick(B.app);
  }
  const LoopEnd eb = runToRollover(B, (long)T + D + kWindow);

  CHECK(d_beat == 0, "%s: beat name differs on %d ticks (first at k=%d)", label, d_beat, first_hard);
  CHECK(d_prog == 0, "%s: beat progress differs on %d ticks (first at k=%d)", label, d_prog, first_hard);
  CHECK(d_plan == 0, "%s: loop plan differs on %d ticks", label, d_plan);
  CHECK(d_rect_nominor == 0, "%s: hiker rect differs on %d ticks with no minor effect or weather gag running (first k=%d)",
        label, d_rect_nominor, first_hard);
  CHECK(ea.max_overruns == 0 && eb.max_overruns == 0, "%s: spine overruns A=%d B=%d", label, ea.max_overruns,
        eb.max_overruns);
  CHECK(ea.max_forced == 0 && eb.max_forced == 0, "%s: minors forced A=%d B=%d", label, ea.max_forced, eb.max_forced);
  CHECK(ea.roll_tick > 0 && eb.roll_tick > 0, "%s: a loop never ended", label);
  CHECK(eb.roll_tick - ea.roll_tick == D, "%s: loop ends %ld ticks later with the bell, want %d", label,
        eb.roll_tick - ea.roll_tick, D);
  printf("  eq %-26s T=%5d D=%d, %d ticks compared: beat %d prog %d plan %d rect %d (gag only: %d, neither: %d) | minor name %d "
         "count %d critters %d other %d (first minor diff k=%d last k=%d; first minor start A k=%d B k=%d) | "
         "overruns A%d B%d forced A%d B%d | loop %ld vs %ld ticks, next seed %s\n",
         label, T, D, kWindow + 1, d_beat, d_prog, d_plan, d_rect, d_rect_gag, d_rect_nominor, d_minor_name, d_minor_count, d_crit,
         d_other, first_minor_diff, last_minor_diff, ka_minor, kb_minor, ea.max_overruns, eb.max_overruns,
         ea.max_forced, eb.max_forced, ea.roll_tick, eb.roll_tick, ea.next_seed == eb.next_seed ? "same" : "DIFFERS");
  B.stop();   // last started, first stopped: the rasterizer's scratch pointer is a stack (appStart/appStop)
  A.stop();
}

// ---------------------------------------------------------------------------
// Check 5: out of frame.

// Ring the bell now and run 120 ticks: no reaction, and the beat clock moves every tick.
static void bellDoesNothing(Rig& r, const char* label) {
  const World* w = appWorld(r.app);
  Obs pre;
  observe(r.app, pre);
  bellNow(r);
  bool ever = false;
  uint32_t tc = pre.tc;
  int stalled = 0;
  for (int i = 0; i < 120; ++i) {
    appTick(r.app);
    if (w->react.active) ever = true;
    const uint32_t now = appTickCount(r.app);
    if (now == tc) ++stalled;   // (a loop rolling over resets it to a smaller number: not a stall)
    tc = now;
  }
  CHECK(!ever, "%s: the bell started a reaction (beat %s)", label, pre.beat);
  CHECK(stalled == 0, "%s: the beat clock stalled on %d of 120 ticks after the bell", label, stalled);
}

static void testOutOfFrame() {
  // The build-up: he is not there yet.
  {
    Rig r;
    CHECK(r.start(5, makeInputs(4.f, -1, 0)), "start");
    tickN(r, 10);
    Rect rc;
    CHECK(!appHikerRect(r.app, &rc), "hiker visible at tick 10 of the build-up");
    CHECK(appBuildPhase(r.app) < 6, "build phase %d at tick 10", appBuildPhase(r.app));
    const float p0 = appBeatProgress(r.app);
    bellNow(r);
    appTick(r.app);
    CHECK(!appWorld(r.app)->react.active, "bell during the build-up started a reaction");
    CHECK(appBeatProgress(r.app) > p0, "beat progress did not move after a bell in the build-up (%.5f -> %.5f)",
          (double)p0, (double)appBeatProgress(r.app));
    bellDoesNothing(r, "build-up t=11");
    printf("  out of frame: build-up at tick 10 (beat %s, progress %.4f -> %.4f), no reaction\n", "build", (double)p0,
           (double)appBeatProgress(r.app));
    r.stop();
  }
  // Away and faded, in each family.
  struct Cfg { const char* name; float mood; int fam; };
  const Cfg cfgs[] = {{"evening", 4.f, SF_EVENING}, {"morning", 1.5f, SF_MORNING}, {"midday", 3.f, SF_MIDDAY}};
  int away_tested = 0, faded_tested = 0, faded_in_frame = 0;
  for (const Cfg& c : cfgs) {
    for (uint64_t seed = 1; seed <= 2; ++seed) {
      const Inputs in = makeInputs(c.mood, c.fam, 0);
      char label[120];
      // After the build-up, the first tick he is out of frame.
      {
        Rig r;
        r.start(seed, in);
        int t = 0;
        bool found = false;
        for (; t < 9000; ++t) {
          appTick(r.app);
          Rect rc;
          if (t > 400 && appBuildPhase(r.app) == 6 && !appHikerRect(r.app, &rc) && appFade(r.app) < 0.5f) {
            found = true;
            break;
          }
        }
        if (found) {
          snprintf(label, sizeof label, "%s seed %llu away t=%d", c.name, (unsigned long long)seed, t + 1);
          bellDoesNothing(r, label);
          ++away_tested;
        }
        r.stop();
      }
      // The loop's end: the first tick the fade is at least half, preferring one with him still in frame.
      {
        Rig r;
        r.start(seed, in);
        const uint32_t len = appLoopTicks(r.app);
        int pick = -1;
        bool pick_vis = false;
        for (uint32_t t = 0; t + 3 < len; ++t) {
          appTick(r.app);
          if (appFade(r.app) < 0.5f) continue;
          Rect rc;
          const bool vis = appHikerRect(r.app, &rc);
          if (pick < 0 || (vis && !pick_vis)) {
            pick = (int)t + 1;
            pick_vis = vis;
            if (vis) break;
          }
        }
        if (pick >= 0) {
          Rig h;
          h.start(seed, in);
          tickN(h, pick);
          snprintf(label, sizeof label, "%s seed %llu faded t=%d (fade %.2f, hiker %s)", c.name,
                   (unsigned long long)seed, pick, (double)appFade(h.app), pick_vis ? "in frame" : "out of frame");
          bellNow(h);
          bool ever = false;
          for (int i = 0; i < 3; ++i) {
            appTick(h.app);
            if (appWorld(h.app)->react.active) ever = true;
          }
          CHECK(!ever, "%s: faded out and the bell started a reaction", label);
          ++faded_tested;
          if (pick_vis) ++faded_in_frame;
          h.stop();
        }
        r.stop();
      }
    }
  }
  CHECK(away_tested >= 3, "only %d 'away' moments found", away_tested);
  CHECK(faded_tested >= 3, "only %d faded moments found", faded_tested);
  printf("  out of frame: %d away moments and %d faded moments (%d of them with him still in frame): no reaction\n",
         away_tested, faded_tested, faded_in_frame);
}

// ---------------------------------------------------------------------------
// Check 6: a Lookout loop.

static void testLookout() {
  int tested = 0;
  for (uint64_t seed = 1; seed <= 3; ++seed) {
    const Inputs in = makeInputs(5.f, SF_LOOKOUT, 0);
    Rig r;
    CHECK(r.start(seed, in), "start");
    CHECK(appSpineFamily(r.app) == SF_LOOKOUT, "family %d, want the lookout", appSpineFamily(r.app));
    int next = 200, here = 0;
    for (int t = 0; t < 5000 && here < 3; ++t) {
      appTick(r.app);
      Rect rc;
      if (t >= next && appHikerRect(r.app, &rc) && !rc.empty()) {
        char label[80];
        snprintf(label, sizeof label, "lookout seed %llu t=%d", (unsigned long long)seed, t + 1);
        bellDoesNothing(r, label);
        t += 120;
        next = t + 400;
        ++here;
        ++tested;
      }
    }
    r.stop();
  }
  CHECK(tested >= 6, "only %d lookout moments with the hiker in frame", tested);
  // And the bell leaves a Lookout loop's frames exactly as they were.
  {
    const Inputs in = makeInputs(5.f, SF_LOOKOUT, 0);
    Rig a, b;
    a.start(2, in);
    b.start(2, in);
    for (int t = 0; t < 3000; ++t) {
      Rect rc;
      appTick(a.app);
      appTick(b.app);
      if (t > 300 && appHikerRect(a.app, &rc)) break;
    }
    bellNow(b);
    int differ = 0;
    for (int i = 0; i < 100; ++i) {
      appTick(a.app);
      appTick(b.app);
      appRender(a.app, a.surf);
      appRender(b.app, b.surf);
      if (!sameFrame(a, b)) ++differ;
    }
    CHECK(differ == 0, "a bell in a Lookout loop changed %d of 100 frames", differ);
    b.stop();
    a.stop();
  }
  printf("  lookout: %d moments with him in frame, no reaction, frames unchanged by a bell\n", tested);
}

// ---------------------------------------------------------------------------
// Check 7: two bells 25 ticks apart.

static void testTwoBells(uint64_t seed, const Inputs& in, int T) {
  Rig a, b;
  CHECK(a.start(seed, in) && b.start(seed, in), "start");
  tickN(a, T);
  tickN(b, T);
  CHECK(canReact(a.app), "two bells: no reaction possible at tick %d", T);
  const World* wa = appWorld(a.app);
  const World* wb = appWorld(b.app);
  bellNow(a);   // a: one bell
  bellNow(b);   // b: this one, and another 25 ticks later
  int starts_a = 0, starts_b = 0, end_a = 0, end_b = 0;
  bool was_a = false, was_b = false, same = true;
  for (int i = 1; i <= 220; ++i) {
    appTick(a.app);
    if (i == 26) bellNow(b);   // posted after 25 ticks of the first's reaction
    appTick(b.app);
    const bool ra = wa->react.active, rb = wb->react.active;
    if (ra && !was_a) ++starts_a;
    if (rb && !was_b) ++starts_b;
    if (!ra && was_a && !end_a) end_a = i;
    if (!rb && was_b && !end_b) end_b = i;
    was_a = ra;
    was_b = rb;
    Obs oa, ob;
    observe(a.app, oa);
    observe(b.app, ob);
    if (memcmp(&oa, &ob, sizeof oa) != 0 || memcmp(&wa->fx, &wb->fx, sizeof wa->fx) != 0 || ra != rb) same = false;
  }
  CHECK(starts_b == 1, "two bells started %d reactions", starts_b);
  CHECK(starts_a == 1, "one bell started %d reactions", starts_a);
  CHECK(end_a > 0 && end_a == end_b, "two bells end at tick %d, one bell at %d", end_b, end_a);
  CHECK(same, "two bells 25 ticks apart: the run differs from the single bell's");
  printf("  two bells (seed %llu T=%d): %d reaction, ends at tick %d (single bell: %d), whole run identical to the "
         "single bell's: %s\n",
         (unsigned long long)seed, T, starts_b, end_b, end_a, same ? "yes" : "NO");
  b.stop();
  a.stop();
}

// ---------------------------------------------------------------------------
// Check 8: determinism, and incremental render == full render all through it.

static void testRenderDeterminism(uint64_t seed, const Inputs& in, int T) {
  Rig a, b, c, d;   // a, b: bell at T, rendered every tick. c: no bell. d: the bell, a full render at checkpoints.
  CHECK(a.start(seed, in) && b.start(seed, in) && c.start(seed, in) && d.start(seed, in), "start");
  const int from = T - 50, n = 300;
  tickN(a, from);
  tickN(b, from);
  tickN(c, from);
  tickN(d, from);
  CHECK(canReact(a.app), "determinism: no reaction possible at tick %d", T);
  int diff_ab = 0, diff_ac = 0, diff_ad = 0, checked_d = 0;
  int first_ab = -1, first_ad = -1;
  for (int i = 0; i < n; ++i) {
    if (from + i == T) {   // T ticks run: the bell
      bellNow(a);
      bellNow(b);
      bellNow(d);
    }
    appTick(a.app);
    appTick(b.app);
    appTick(c.app);
    appTick(d.app);
    appRender(a.app, a.surf);
    appRender(b.app, b.surf);
    appRender(c.app, c.surf);
    if (frameHash(a) != frameHash(b)) {
      ++diff_ab;
      if (first_ab < 0) first_ab = i;
    }
    if (!sameFrame(a, c)) ++diff_ac;
    if (i % 3 == 2) {   // a full render of the same state against the incremental one
      appInvalidate(d.app);
      appRender(d.app, d.surf);
      ++checked_d;
      if (!sameFrame(a, d)) {
        ++diff_ad;
        if (first_ad < 0) first_ad = i;
      }
    }
  }
  CHECK(diff_ab == 0, "same bell, same tick, same seed: %d of %d frames differ (first at %d)", diff_ab, n, first_ab);
  CHECK(diff_ac > 20, "the reaction barely changed the frames (%d of %d differ from the no-bell run)", diff_ac, n);
  CHECK(diff_ad == 0, "incremental render differs from a full render on %d of %d checked frames (first at %d)", diff_ad,
        checked_d, first_ad);
  printf("  render (seed %llu T=%d): %d frames hashed, two bell runs differ on %d; the bell changed %d frames; "
         "incremental vs full render differ on %d of %d\n",
         (unsigned long long)seed, T, n, diff_ab, diff_ac, diff_ad, checked_d);
  d.stop();
  c.stop();
  b.stop();
  a.stop();
}

// ---------------------------------------------------------------------------
// Sweep: bells at many moments of a run, each checked as in 1-3 and 9.

struct SweepTally {
  int reactions = 0, empty = 0, full = 0, left = 0, walking = 0, waved_pose = 0, waved_not_in_pose = 0;
  int carry_seen[CARRY_COUNT] = {0};
};

static void sweep(const char* name, float mood, int family, int weather, uint64_t seed, int ticks, SweepTally& tally) {
  Rig r;
  CHECK(r.start(seed, makeInputs(mood, family, weather)), "start");
  int next = 330 + (int)(seed * 37 % 90);
  int t = 0;
  while (t < ticks) {
    if (t >= next && canReact(r.app)) {
      char label[120];
      snprintf(label, sizeof label, "sweep %s w%d seed %llu t=%d beat %s", name, weather, (unsigned long long)seed, t,
               appBeatName(r.app));
      const ReactReport rep = runReaction(r, label);
      if (rep.started) {
        t += rep.frozen + 1;
        ++tally.reactions;
        if (rep.hands_full) {
          ++tally.full;
          if (rep.pre_carry >= 0 && rep.pre_carry < CARRY_COUNT) ++tally.carry_seen[rep.pre_carry];
        } else {
          ++tally.empty;
          if (rep.max_pose_sh0 > 2.0f) {
            ++tally.waved_pose;
          } else {
            ++tally.waved_not_in_pose;
            printf("    note: %s: the wave never reached the composed pose (near shoulder max %.2f, before %.2f)\n",
                   label, (double)rep.max_pose_sh0, (double)rep.pre_pose_sh0);
          }
        }
        if (rep.pre_facing < 0) ++tally.left;
        if (rep.walking) ++tally.walking;
        next = t + 97 + (int)(seed * 13 % 61);
        continue;
      }
    }
    appTick(r.app);
    ++t;
  }
  r.stop();
}

// ---------------------------------------------------------------------------
// The weather's gags on him (wind: the hat blown off and fetched; heat: the
// fan; chill: the hug) run on the ambient layer, so they keep going through
// the freeze. A bell in the middle of each, one per stage of the gag.

static const char* kFamName[] = {"evening", "morning", "midday", "lookout"};
static const char* weatherArg(int code) {
  static const char* const n[] = {"clear", "rain", "snow", "wind", "heat", "fog", "chill", "storm"};
  return code >= 0 && code < 8 ? n[code] : "?";
}

static void testWeatherGags() {
  struct Wx { int code; int gag; };
  const Wx wxs[] = {{3, WG_HAT}, {4, WG_FAN}, {6, WG_HUG}};
  int moments = 0, no_wave_hug = 0;
  for (const Wx& wx : wxs) {
    int tested = 0, lost = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
      const Inputs in = makeInputs(4.f, SF_EVENING, wx.code);
      // Scan a run with no bell for the first tick of each distinct (stage, hug fully on).
      int at[16], n = 0;
      bool seen[2][8] = {{false}};
      {
        Rig r;
        r.start(seed, in);
        for (int t = 0; t < 9000 && n < 16; ++t) {
          const World* w = appWorld(r.app);
          if (w->weather.gag == wx.gag && canReact(r.app)) {
            const int full = w->weather.hug_w >= 0.99f ? 1 : 0;
            const int st = w->weather.stage < 8 ? w->weather.stage : 7;
            if (!seen[full][st]) {
              seen[full][st] = true;
              at[n++] = t;
            }
          }
          appTick(r.app);
        }
        r.stop();
      }
      for (int i = 0; i < n; ++i) {
        Rig q;
        q.start(seed, in);
        tickN(q, at[i]);
        const World* w = appWorld(q.app);
        const int stage = w->weather.stage;
        const float hug = w->weather.hug_w, ov = w->weather.ov_w;
        char label[120];
        snprintf(label, sizeof label, "gag %s seed %llu t=%d stage %d hug %.2f ov %.2f", weatherArg(wx.code),
                 (unsigned long long)seed, at[i], stage, (double)hug, (double)ov);
        const ReactReport rep = runReaction(q, label);
        ++tested;
        ++moments;
        if (rep.facing_bad_n) {
          printf("      facing glitch: %s: facing wrong on %d tick(s), first on tick %d of the reaction (tick %d)\n"
                 "      repro: ss_preview --seed %llu --mood 4 --spine %s --weather %s --skip %d --doorbell-at %d --every 1 "
                 "--frames 6\n",
                 label, rep.facing_bad_n, rep.facing_bad_first, at[i] + rep.facing_bad_first, (unsigned long long)seed,
                 kFamName[SF_EVENING], weatherArg(wx.code), at[i] + rep.facing_bad_first - 3, at[i]);
        }
        if (rep.started && !rep.hands_full && rep.max_pose_sh0 <= 2.0f) {
          ++lost;
          if (wx.gag == WG_HUG) ++no_wave_hug;
          printf("    no wave: %s: composed near shoulder only reached %.2f (rest %.2f)\n"
                 "      repro: ss_preview --seed %llu --mood 4 --spine %s --weather %s --skip %d --doorbell-at %d "
                 "--every 5 --frames 20\n",
                 label, (double)rep.max_pose_sh0, (double)rep.pre_pose_sh0, (unsigned long long)seed,
                 kFamName[SF_EVENING], weatherArg(wx.code), at[i] - 10, at[i]);
        }
        q.stop();
      }
    }
    printf("  gag %-5s: %d bells during the gag, %d without a visible wave\n", weatherArg(wx.code), tested, lost);
  }
  (void)moments;
  CHECK(no_wave_hug == 0,
        "chill hug: %d bells during the hug left the near arm out of the wave (weatherApplyPose blends the hug over the "
        "reaction's arm)",
        no_wave_hug);
}

// Things in his hand that are not `carry` (the fork, the wrung-out hat): the
// reaction waves with the near arm, so they go up with it. Not a failure;
// listed so Mike can look.
static void noteSmallHeld() {
  struct Cfg { float mood; int fam; };
  const Cfg cfgs[] = {{4.f, SF_EVENING}, {1.5f, SF_MORNING}, {3.f, SF_MIDDAY}};
  int shown = 0;
  for (const Cfg& c : cfgs) {
    for (uint64_t seed = 1; seed <= 4; ++seed) {
      const Inputs in = makeInputs(c.mood, c.fam, 0);
      bool seen[ITEM_COUNT] = {false};
      int at[ITEM_COUNT], n = 0, which[ITEM_COUNT];
      {
        Rig r;
        r.start(seed, in);
        for (int t = 0; t < 14000; ++t) {
          const World* w = appWorld(r.app);
          if (canReact(r.app) && w->hiker.carry == CARRY_NONE) {
            for (int i = 0; i < ITEM_COUNT; ++i) {
              if (w->items[i].held && !w->items[i].critter && !seen[i]) {
                seen[i] = true;
                at[n] = t;
                which[n++] = i;
              }
            }
          }
          appTick(r.app);
        }
        r.stop();
      }
      for (int i = 0; i < n; ++i) {
        Rig q;
        q.start(seed, in);
        tickN(q, at[i]);
        char label[100];
        snprintf(label, sizeof label, "%s seed %llu t=%d beat %s holds the %s", kFamName[c.fam],
                 (unsigned long long)seed, at[i], appBeatName(q.app), itemName((ItemId)which[i]));   // (the stay's pan, cake and brush too)
        const ReactReport rep = runReaction(q, label);
        if (shown < 6 && rep.started) {
          printf("    note: %s, carry is NONE so he waves with it; repro: ss_preview --seed %llu --mood %.1f --spine %s "
                 "--skip %d --doorbell-at %d --every 5 --frames 20\n",
                 label, (unsigned long long)seed, (double)c.mood, kFamName[c.fam], at[i] - 10, at[i]);
          ++shown;
        }
        q.stop();
      }
    }
  }
}

// ---------------------------------------------------------------------------

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not eat the lines before it
  const auto t0 = std::chrono::steady_clock::now();
  printf("kReactSeconds %.2f s = %d ticks\n", (double)kReactSeconds, (int)(kReactSeconds * SS_TICK_HZ + 0.5f));

  // The moments the review sheets and the named checks use: the preview's own
  // inputs (mood 4, the family from the inputs, no weather reading), so the
  // same (seed, tick) means the same thing to `ss_preview --seed S --mood 4`.
  const Inputs pv = makeInputs(4.f, -1, -1);
  struct Moment { MomentKind kind; uint64_t seed; int tick; };
  Moment moments[3] = {{MK_RIGHT, 0, -1}, {MK_LEFT, 0, -1}, {MK_FULL, 0, -1}};
  for (Moment& m : moments) {
    for (uint64_t seed = 1; seed <= 60 && m.tick < 0; ++seed) {
      const int t = findMoment(seed, pv, m.kind, 500, 9000);
      if (t >= 0) {
        m.seed = seed;
        m.tick = t;
      }
    }
    CHECK(m.tick >= 0, "no %s moment found in seeds 1..60", kKindName[m.kind]);
  }
  for (const Moment& m : moments) {
    if (m.tick < 0) continue;
    Rig r;
    r.start(m.seed, pv);
    tickN(r, m.tick);
    const World* w = appWorld(r.app);
    Rect rc;
    appHikerRect(r.app, &rc);
    printf("SHEET %s seed=%llu T=%d  (beat %s %.2f, facing %d, carry %d, rect %d,%d-%d,%d)\n", kKindName[m.kind],
           (unsigned long long)m.seed, m.tick, appBeatName(r.app), (double)appBeatProgress(r.app), w->hiker.facing,
           (int)w->hiker.carry, rc.x0, rc.y0, rc.x1, rc.y1);
    // Checks 1-3 (and 9 for the full-hands moment) on this very moment.
    char label[80];
    snprintf(label, sizeof label, "%s seed %llu T=%d", kKindName[m.kind], (unsigned long long)m.seed, m.tick);
    const ReactReport rep = runReaction(r, label);
    CHECK(rep.started, "%s: no reaction", label);
    if (m.kind == MK_FULL) CHECK(rep.hands_full, "%s: not hands full", label);
    if (m.kind == MK_LEFT) CHECK(rep.pre_facing == -1 && rep.turned, "%s: did not turn", label);
    printf("  %s: reaction %d ticks, rect moved %d, hop %d, wave %d, turned %d, near shoulder max %.2f (before %.2f)\n",
           label, rep.frozen, rep.rect_moved, rep.hopped, rep.waved, rep.turned, (double)rep.max_pose_sh0,
           (double)rep.pre_pose_sh0);
    r.stop();
  }

  // Sweep: checks 1-3 and 9 at many moments, every family and several weathers.
  printf("sweep:\n");
  SweepTally tally;
  struct SweepCfg { const char* name; float mood; int fam; int weather; };
  const SweepCfg cfgs[] = {
      {"evening", 4.f, SF_EVENING, 0}, {"evening", 4.f, SF_EVENING, 1}, {"evening", 4.f, SF_EVENING, 6},
      {"morning", 1.5f, SF_MORNING, 0}, {"morning", 1.5f, SF_MORNING, 5}, {"midday", 3.f, SF_MIDDAY, 0},
      {"midday", 3.f, SF_MIDDAY, 4},    {"midday", 3.f, SF_MIDDAY, 7},
  };
  for (const SweepCfg& c : cfgs) {
    for (uint64_t seed = 1; seed <= 2; ++seed) sweep(c.name, c.mood, c.fam, c.weather, seed, 11000, tally);
  }
  printf("  sweep: %d reactions (%d empty-handed, %d hands full, %d facing left, %d while walking); composed-pose wave: "
         "%d yes, %d not\n",
         tally.reactions, tally.empty, tally.full, tally.left, tally.walking, tally.waved_pose, tally.waved_not_in_pose);
  printf("  carry kinds seen with full hands:");
  for (int c = 1; c < CARRY_COUNT; ++c) printf(" %d:%d", c, tally.carry_seen[c]);
  printf("\n");
  CHECK(tally.reactions >= 60, "only %d reactions in the sweep", tally.reactions);
  CHECK(tally.full >= 5 && tally.left >= 5, "sweep too thin: %d full, %d left", tally.full, tally.left);

  // Check 4: story equivalence, three moments on the preview inputs and one per other family.
  printf("equivalence:\n");
  for (const Moment& m : moments) {
    if (m.tick < 0) continue;
    char label[80];
    snprintf(label, sizeof label, "%s seed %llu", kKindName[m.kind], (unsigned long long)m.seed);
    equivalence(m.seed, pv, m.tick, label);
  }
  {
    struct EqCfg { const char* name; float mood; int fam; int weather; uint64_t seed; };
    const EqCfg eqs[] = {{"morning seed 3", 1.5f, SF_MORNING, 0, 3}, {"midday seed 4", 3.f, SF_MIDDAY, 0, 4},
                         {"evening rain seed 5", 4.f, SF_EVENING, 1, 5}, {"evening wind seed 2", 4.f, SF_EVENING, 3, 2},
                         {"evening heat seed 2", 4.f, SF_EVENING, 4, 2}, {"evening chill seed 2", 4.f, SF_EVENING, 6, 2}};
    for (const EqCfg& e : eqs) {
      const Inputs in = makeInputs(e.mood, e.fam, e.weather);
      const int t = findMoment(e.seed, in, MK_RIGHT, 700, 6000);
      CHECK(t >= 0, "%s: no moment", e.name);
      if (t >= 0) equivalence(e.seed, in, t, e.name);
    }
    // A bell in the middle of a weather gag (wind hat, heat fan, chill hug).
    const EqCfg gags[] = {{"wind gag seed 2", 4.f, SF_EVENING, 3, 2}, {"heat gag seed 2", 4.f, SF_EVENING, 4, 2},
                          {"chill gag seed 2", 4.f, SF_EVENING, 6, 2}};
    for (const EqCfg& e : gags) {
      const Inputs in = makeInputs(e.mood, e.fam, e.weather);
      const int t = findMoment(e.seed, in, MK_GAG, 700, 6000);
      CHECK(t >= 0, "%s: no moment", e.name);
      if (t >= 0) equivalence(e.seed, in, t, e.name);
    }
  }

  // The weather's gags on him, and small things in his hand that are not `carry`.
  printf("weather gags / held things:\n");
  testWeatherGags();
  noteSmallHeld();

  // Checks 5 and 6.
  printf("out of frame / lookout:\n");
  testOutOfFrame();
  testLookout();

  // Checks 7 and 8.
  printf("two bells / determinism:\n");
  for (const Moment& m : moments) {
    if (m.tick >= 0) testTwoBells(m.seed, pv, m.tick);
  }
  for (const Moment& m : moments) {
    if (m.tick >= 0) testRenderDeterminism(m.seed, pv, m.tick);
  }

  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("%s: %d failure(s), %.1f s\n", g_fail ? "FAILED" : "ok", g_fail, secs);
  return g_fail ? 1 : 0;
}
