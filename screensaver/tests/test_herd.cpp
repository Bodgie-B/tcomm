// Tests for core/scene/herd, the deer visit's standalone part (stage 22, DEER_PLAN.md section 9; Mike,
// 2026-11-18). No World: a small kinematic driver (spawn, walk, run, arrive, release) stands in for the
// director and the critters, acts on the orders herdTick gives, and tells it what it needs to know.
//
//  [1] the cue tables: times never go backwards, every list starts well, every role that enters has an
//      exit, the last cue of every main list is before the alarm
//  [2] the graze cycle: five deer for 60 s from 20 seeds: no two changes within 0.3 s, never all heads
//      down, the doe is never the last head down, bouts within their lengths, ears and tails flick
//  [3] a whole visit, each variant and size, three layouts, two speeds: everyone enters, the soft alarm's
//      wave comes in order, the flags follow the heads, they leave in a loose line (REVIEW_FIXES F12), the
//      camp is empty before the beat ends, the settled picture keeps its 2 px
//  [4] the hard alarm (a bell, a flash, a dog) and the second look; a visitor at the panel
//  [5] the hoof prints: bounds hold every pixel, dirty only when something changed, the fade ends once
//  [6] determinism
//
// With the argument `log` it prints the orders of one visit per variant (the choreography as a timeline)
// instead of testing.
#include "gfx/raster.h"
#include "scene/critters.h"
#include "scene/herd.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

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

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}
static const uint64_t kFnv0 = 1469598103934665603ull;

static const char* kRoleName[HR_COUNT] = {"doe", "fawn", "door", "wall", "grazer", "grazer2"};
static const char* kCmdName[HC_COUNT] = {"ENTER", "WALK", "RUN", "BACK", "FACE", "ACT", "GRAZE_ON", "GRAZE_OFF", "ALERT",
                                         "TAIL", "STAMP", "EAR", "NUDGE", "RING", "DROPS", "PRINT", "WOBBLE", "DOOR",
                                         "HOLD", "NIBBLE", "STEP", "HIT", "GONE"};

static HerdLayout layoutFor(int k) {
  // fire x 238 to 262, the camp pine 550 to 570: the layouts the plan names, and the middle
  static const float fx[3] = {250.f, 238.f, 262.f};
  static const float cx[3] = {560.f, 550.f, 570.f};
  HerdLayout L;
  L.fire = Vec2{fx[k], 398.f};
  L.tent = Vec2{fx[k] - 44.f, 352.f};
  L.door = Vec2{fx[k] - 44.f + 20.f, 356.f};
  L.splash = Vec2{fx[k] + 190.f, 405.f};
  L.camp_x = cx[k];
  L.nibble = Vec2{fx[k] + 94.f, 407.f};   // HS_N4 (step C4); nibble_on stays false: the plain visit
  return L;
}

// ---------------------------------------------------------------------------
// The stand-in driver.

struct Sim {
  HerdState h;
  Rng rng;
  float speed_mul = 1.f;
  float x[HERD_MAX], y[HERD_MAX], x0[HERD_MAX], y0[HERD_MAX], tx[HERD_MAX], ty[HERD_MAX], v[HERD_MAX];
  bool in_use[HERD_MAX], moving[HERD_MAX], exiting[HERD_MAX];
  uint8_t face[HERD_MAX];
  float t = 0.f;
  int tick = 0;
  uint64_t hash = kFnv0;
  struct Rec {
    float t;
    int deer, cmd, a, b;
  };
  std::vector<Rec> log;
  float first_head[HERD_MAX], first_tail[HERD_MAX], leave_t[HERD_MAX], gone_t[HERD_MAX];
  float tail_to[HERD_MAX], tail_prev[HERD_MAX];
  int entered_count[HERD_MAX];
  int hits = 0, steps = 0, rings = 0, drops = 0, prints = 0, wobbles = 0;
  float door = 0.f;
  float last_door_t = -1.f;

  void begin(uint64_t seed, int layout, uint8_t plan, bool shy = false) {
    rng.seed(seed);
    herdInit(h, rng, layoutFor(layout), plan, shy);
    memset(x, 0, sizeof x);
    memset(y, 0, sizeof y);
    memset(in_use, 0, sizeof in_use);
    memset(moving, 0, sizeof moving);
    memset(exiting, 0, sizeof exiting);
    for (int i = 0; i < HERD_MAX; ++i) {
      face[i] = 1;
      first_head[i] = first_tail[i] = leave_t[i] = gone_t[i] = -1.f;
      tail_to[i] = 0.f;
      tail_prev[i] = 0.f;
      entered_count[i] = 0;
    }
    t = 0.f;
    tick = 0;
    hash = kFnv0;
    log.clear();
  }

  void order(const HerdEv& e, bool record) {
    HerdDeer& d = h.d[e.deer];
    const int i = e.deer;
    if (record) log.push_back(Rec{t, i, e.cmd, e.a, e.b});
    hash = fnv(hash, &e, sizeof e);
    switch (e.cmd) {
      case HC_ENTER:
        in_use[i] = true;
        ++entered_count[i];
        x[i] = kHerdSpawnX;
        y[i] = (float)e.a;
        moving[i] = false;
        exiting[i] = false;
        face[i] = 1;
        break;
      case HC_WALK:
      case HC_RUN: {
        const Vec2 to = herdSpot(h, (uint8_t)e.a, d.role);
        x0[i] = x[i];
        y0[i] = y[i];
        tx[i] = to.x;
        ty[i] = to.y;
        v[i] = (float)e.b * speed_mul;
        moving[i] = true;
        face[i] = to.x >= x[i] ? 1 : 0;
        if (e.a == HS_OUT_L || e.a == HS_OUT_R) {
          exiting[i] = true;
          if (leave_t[i] < 0.f) leave_t[i] = t;
        }
        break;
      }
      case HC_BACK: {
        const float dir = face[i] ? -1.f : 1.f;
        x0[i] = x[i];
        y0[i] = y[i];
        tx[i] = x[i] + dir * (float)e.a;
        ty[i] = y[i];
        v[i] = (float)e.b * speed_mul;
        moving[i] = true;
        break;
      }
      case HC_FACE: face[i] = e.a > 0 ? 1 : 0; break;
      case HC_ACT:
        // a head coming up: straight ahead (look), level (idle) or back over the shoulder (head turn)
        if (e.a == CA_LOOK || e.a == CA_IDLE || e.a == CA_HEADTURN) {
          if (first_head[i] < 0.f && h.alarm != 0 && t >= h.alarm_t) first_head[i] = t - h.alarm_t;
        }
        if (e.a != CA_MOVE && e.a != CA_RUN) moving[i] = false;   // an action drops a walk
        break;
      case HE_STEP: {
        const float dir = face[i] ? 1.f : -1.f;
        x0[i] = x[i];
        y0[i] = y[i];
        tx[i] = x[i] + dir * (float)e.a;
        ty[i] = y[i];
        v[i] = (float)e.b * speed_mul;
        moving[i] = true;
        ++steps;
        break;
      }
      case HE_HIT:
        ++hits;
        if (e.a == 2) ++rings;   // a paw on the water makes a ring too
        break;
      case HC_RING: ++rings; break;
      case HC_DROPS: drops += e.a; break;
      case HC_PRINT:
        ++prints;
        herdPrintAdd(h, (int)x[i] + e.b, (int)y[i] + 2, e.a == HP_WET);
        break;
      case HC_WOBBLE: ++wobbles; break;
      case HC_DOOR:
        door = (float)e.a * 0.01f;
        last_door_t = t;
        break;
      case HE_GONE: gone_t[i] = t; break;
      default: break;
    }
  }

  void move() {
    for (int i = 0; i < h.n; ++i) {
      if (!in_use[i] || !moving[i]) continue;
      const float step = v[i] * SS_TICK_DT;
      const float remain = tx[i] - x[i];
      const float span = fabsf(tx[i] - x0[i]);
      if (fabsf(remain) <= step) {
        x[i] = tx[i];
        y[i] = ty[i];
        moving[i] = false;
        if (exiting[i]) in_use[i] = false;   // released once out of sight
      } else {
        x[i] += remain > 0.f ? step : -step;
        const float prog = span > 0.f ? fabsf(x[i] - x0[i]) / span : 1.f;
        y[i] = y0[i] + (ty[i] - y0[i]) * prog;
      }
    }
  }

  // One tick. `flags` the HI_ bits for this tick.
  void step(uint8_t flags = 0, bool record = true) {
    HerdIn in;
    memset(&in, 0, sizeof in);
    in.dt = SS_TICK_DT;
    in.flags = flags;
    for (int i = 0; i < HERD_MAX; ++i) {
      in.in_use[i] = in_use[i];
      in.arrived[i] = !moving[i];
      in.x[i] = x[i];
    }
    HerdOut out;
    herdTick(h, rng, in, out);
    t += SS_TICK_DT;
    ++tick;
    for (int k = 0; k < out.n; ++k) order(out.ev[k], record);
    move();
    for (int i = 0; i < h.n; ++i) {
      const HerdView vw = herdView(h.d[i]);
      hash = fnv(hash, &vw, sizeof vw);
      // the flag: the first time its target changes after the alarm (HC_TAIL is the machine's own, not an order)
      if (h.alarm != 0 && h.d[i].tail_to != tail_prev[i]) {
        if (first_tail[i] < 0.f && h.d[i].tail_to > 0.f) {
          first_tail[i] = t - h.alarm_t;
          tail_to[i] = h.d[i].tail_to;
        }
        tail_prev[i] = h.d[i].tail_to;
      }
    }
  }
  void run(float seconds, uint8_t flags = 0) {
    const int n = (int)(seconds / SS_TICK_DT + 0.5f);
    for (int k = 0; k < n; ++k) step(flags);
  }
  int inScene() const {
    int n = 0;
    for (int i = 0; i < h.n; ++i) n += in_use[i] ? 1 : 0;
    return n;
  }
};

static uint8_t planOf(int variant, int size) { return (uint8_t)((variant + 1) | (size << 2)); }
static const char* kVarName[3] = {"A", "B", "C"};

// A deer's idle bounds at (x, y), the same as the critters would give: the box to keep 2 px apart.
static Rect deerBox(float x, float y, uint8_t role, uint8_t face) {
  Critter c;
  critterInit(c, CR_DEER, Vec2{x, y}, face ? 1 : -1);
  c.scale = herdScale(role);
  c.tone = role == HR_FAWN ? kDeerFawn : 0;
  critterTick(c, 0.f);
  return critterBounds(c);
}

// A deer's bounds at a canter at (x, y): the most of every phase of the stride (the legs reach further
// back and forward than standing). The way out keeps these 2 px apart (REVIEW_FIXES F12).
static Rect runBox(float x, float y, uint8_t role, uint8_t face) {
  static bool ready = false;
  static Rect rel[HR_COUNT][2];   // relative to the feet, per facing (0 left, 1 right)
  if (!ready) {
    for (int r = 0; r < HR_COUNT; ++r)
      for (int f = 0; f < 2; ++f) {
        Rect u{0, 0, 0, 0};
        for (int ph = 0; ph < 16; ++ph) {
          Critter c;
          critterInit(c, CR_DEER, Vec2{0.f, 0.f}, f ? 1 : -1);
          c.scale = herdScale((uint8_t)r);
          c.tone = r == HR_FAWN ? kDeerFawn : 0;
          c.tail_add = 1.f;
          critterSetAction(c, CA_RUN, 0.f);
          c.moving = true;
          c.gait_w = 1.f;
          c.move_from = c.move_to = 0.f;
          c.phase = (float)ph / 16.f;
          critterTick(c, 0.f);
          const Rect b = critterBounds(c);
          if (ph == 0) u = b;
          u = Rect{b.x0 < u.x0 ? b.x0 : u.x0, b.y0 < u.y0 ? b.y0 : u.y0, b.x1 > u.x1 ? b.x1 : u.x1, b.y1 > u.y1 ? b.y1 : u.y1};
        }
        rel[r][f] = u;
      }
    ready = true;
  }
  const Rect& q = rel[role < HR_COUNT ? role : 0][face ? 1 : 0];
  const int ix = (int)floorf(x), iy = (int)floorf(y);
  return Rect{ix + q.x0, iy + q.y0, ix + q.x1 + 1, iy + q.y1 + 1};
}

// ---------------------------------------------------------------------------

static void testTables() {
  printf("[1] the cue tables\n");
  int total = 0;
  for (int v = 0; v < 3; ++v)
    for (int role = 0; role < HR_COUNT; ++role) {
      int n = 0;
      const HerdCue* L = herdCues(HL_MAIN, (uint8_t)role, v, n);
      CHECK(n > 0 && L, "main list %s %s is empty", kVarName[v], kRoleName[role]);
      total += n;
      uint16_t prev = 0;
      int enters = 0;
      float last = 0.f;
      for (int k = 0; k < n; ++k) {
        CHECK(L[k].at >= prev, "%s %s: cue %d goes back in time (%d < %d)", kVarName[v], kRoleName[role], k, L[k].at, prev);
        prev = L[k].at;
        CHECK(L[k].cmd < HE_STEP, "%s %s: cue %d is not an order a table may hold (%d)", kVarName[v], kRoleName[role], k, L[k].cmd);
        if (L[k].cmd == HC_ENTER) {
          ++enters;
          CHECK(k == 0, "%s %s: ENTER is not the first cue", kVarName[v], kRoleName[role]);
        }
        if (L[k].cmd == HC_WALK || L[k].cmd == HC_RUN) {
          CHECK(L[k].a >= 0 && L[k].a < HS_COUNT, "%s %s: bad spot %d", kVarName[v], kRoleName[role], L[k].a);
          CHECK(L[k].b >= 8 && L[k].b <= 120, "%s %s: odd speed %d", kVarName[v], kRoleName[role], L[k].b);
        }
        if (L[k].cmd == HC_ACT) CHECK(L[k].a >= 0 && L[k].a < CA_COUNT && critterSupports(CR_DEER, (CritterAction)L[k].a),
                                      "%s %s: cue %d asks the deer for action %d", kVarName[v], kRoleName[role], k, L[k].a);
        last = (float)L[k].at * 0.01f;
      }
      CHECK(enters == 1, "%s %s: %d ENTER cues", kVarName[v], kRoleName[role], enters);
      CHECK(last < kHerdSoftAt - kHerdShyEarlier - 1.f, "%s %s: its last cue (%.1f s) is not before the (shy) alarm (%.1f s)",
            kVarName[v], kRoleName[role], (double)last, (double)(kHerdSoftAt - kHerdShyEarlier));
    }
  for (int role = 0; role < HR_COUNT; ++role) {
    for (int hard = 0; hard < 2; ++hard) {
      int n = 0;
      const HerdCue* L = herdCues(hard ? HL_WAVE_HARD : HL_WAVE_SOFT, (uint8_t)role, 0, n);
      CHECK(n > 0, "no %s wave for %s", hard ? "hard" : "soft", kRoleName[role]);
      total += n;
      uint16_t prev = 0;
      bool head = false;
      for (int k = 0; k < n; ++k) {
        CHECK(L[k].at >= prev, "%s wave %s goes back in time", hard ? "hard" : "soft", kRoleName[role]);
        prev = L[k].at;
        if (L[k].cmd == HC_ACT && (L[k].a == CA_LOOK || L[k].a == CA_IDLE || L[k].a == CA_HEADTURN)) head = true;
        CHECK(L[k].cmd != HC_WALK && L[k].cmd != HC_RUN && !L[k].wait, "a wave must not move or wait");
      }
      CHECK(head, "%s wave for %s has no head up", hard ? "hard" : "soft", kRoleName[role]);
      CHECK((float)prev * 0.01f < (hard ? 1.f : 5.5f), "%s wave for %s ends at %.2f s, after its leave time", hard ? "hard" : "soft",
            kRoleName[role], (double)prev * 0.01);
    }
    // every role that enters has a way out, both ways and hard
    static const uint8_t kExits[4] = {HL_EXIT_SOFT, HL_EXIT_HARD_L, HL_EXIT_HARD_R, HL_SECOND};
    for (int k = 0; k < 3; ++k) {
      int n = 0;
      const HerdCue* L = herdCues(kExits[k], (uint8_t)role, 0, n);
      bool out = false;
      for (int c = 0; c < n; ++c)
        if (L[c].cmd == HC_RUN && (L[c].a == HS_OUT_L || L[c].a == HS_OUT_R)) out = true;
      CHECK(out, "%s has no exit in list %d", kRoleName[role], kExits[k]);
    }
  }
  int n2 = 0;
  herdCues(HL_SECOND, HR_DOE, 0, n2);
  CHECK(n2 > 0, "no second look for the doe");
  printf("  %d cues in the tables in all (%d bytes each: %d bytes), sizeof(HerdState) %d, sizeof(HerdDeer) %d\n",
         herdCueCount(), (int)sizeof(HerdCue), herdCueCount() * (int)sizeof(HerdCue), (int)sizeof(HerdState),
         (int)sizeof(HerdDeer));
  (void)total;
  CHECK(sizeof(HerdState) <= 800, "HerdState grew to %d bytes", (int)sizeof(HerdState));
  // herdRoles
  uint8_t roles[HERD_MAX];
  CHECK(herdRoles(0, roles) == 5 && herdRoles(1, roles) == 6 && herdRoles(2, roles) == 4, "herd sizes");
  // herdReach is what the bounds say (idle, adult, both ways)
  for (int role = 0; role < HR_COUNT; ++role)
    for (int fr = 0; fr < 2; ++fr) {
      float left, right;
      herdReach((uint8_t)role, fr != 0, left, right);
      const Rect b = deerBox(100.f, 100.f, (uint8_t)role, (uint8_t)fr);
      CHECK((float)(100 - b.x0) <= left + 2.f && (float)(b.x1 - 100) <= right + 2.f && (float)(100 - b.x0) >= left - 8.f &&
                (float)(b.x1 - 100) >= right - 8.f,
            "herdReach for the %s facing %s is %.0f left and %.0f right, the idle bounds say %d and %d", kRoleName[role],
            fr ? "right" : "left", (double)left, (double)right, 100 - b.x0, b.x1 - 100);
    }
  // spots stay in the frame, off the chip box and the quiet corner, and the standing ones at y >= 343 near the pine
  for (int lk = 0; lk < 3; ++lk) {
    HerdState h;
    Rng r;
    r.seed(1);
    herdInit(h, r, layoutFor(lk), planOf(0, 1), false);
    for (int s = 0; s < HS_COUNT; ++s)
      for (int role = 0; role < HR_COUNT; ++role) {
        const Vec2 p = herdSpot(h, (uint8_t)s, (uint8_t)role);
        if (s == HS_OUT_L || s == HS_OUT_R) continue;
        // (570, not 560, since step C4: the sixth deer keeps 80 px from the doe, 14 px above the water there)
        CHECK(p.x >= 0.f && p.x <= 570.f, "spot %d (%s) x %.0f is off the camp side", s, kRoleName[role], (double)p.x);
        CHECK(p.y >= 343.f && p.y <= 412.f, "spot %d (%s) y %.0f", s, kRoleName[role], (double)p.y);
        if (p.x > h.camp_x - 70.f && p.x < h.camp_x + 70.f && s != HS_LOOKIN && s != HS_WAY2)
          CHECK(p.y >= 343.f, "a deer in front of the camp pine at y %.0f", (double)p.y);
      }
  }
}

// ---------------------------------------------------------------------------

struct GrazeStats {
  int changes = 0, close = 0, all_down = 0, doe_last = 0;
  float down_min = 99.f, down_max = 0.f, up_min = 99.f, up_max = 0.f;
  int ears[HERD_MAX] = {}, flicks[HERD_MAX] = {};
};

// Five deer, entered and grazing from the start, for `seconds`.
static void grazeRun(Sim& s, float seconds, GrazeStats& g, uint64_t seed) {
  s.begin(seed, 0, planOf(0, 0));
  const int n = s.h.n;
  for (int i = 0; i < n; ++i) {
    HerdDeer& d = s.h.d[i];
    d.entered = 10;
    d.mode = HM_CUES;
    d.cue = 255;   // no cues: they only graze
    s.in_use[i] = true;
    s.x[i] = d.spot.x;
    s.y[i] = d.spot.y;
    s.face[i] = 1;
    s.h.d[i].grazing = 1;
    s.h.d[i].mode = HM_GRAZE;
    d.next = s.rng.range(0.5f, 2.0f);
  }
  bool down[HERD_MAX] = {};
  bool first[HERD_MAX] = {true, true, true, true, true, true};   // the first bout began at 0, not at a change
  float since[HERD_MAX] = {};
  float last_change = -9.f;
  int prev_ear[HERD_MAX] = {}, prev_flick[HERD_MAX] = {};
  const int ticks = (int)(seconds / SS_TICK_DT);
  for (int k = 0; k < ticks; ++k) {
    s.step(0, false);
    // a head is "down" when the machine says so
    int downs = 0;
    bool changed_now = false;
    for (int i = 0; i < n; ++i) {
      const bool dn = s.h.d[i].head_down != 0;
      if (dn != down[i]) {
        ++g.changes;
        changed_now = true;
        const float len = since[i];
        const bool was_first = first[i];
        first[i] = false;
        if (!was_first) {   // a finished bout (not the first, which began at 0)
          if (down[i]) {
            g.down_min = len < g.down_min ? len : g.down_min;
            g.down_max = len > g.down_max ? len : g.down_max;
          } else {
            g.up_min = len < g.up_min ? len : g.up_min;
            g.up_max = len > g.up_max ? len : g.up_max;
          }
        }
        since[i] = 0.f;
        down[i] = dn;
      }
      since[i] += SS_TICK_DT;
      downs += dn ? 1 : 0;
      if (s.h.d[i].ear > prev_ear[i]) ++g.ears[i];
      prev_ear[i] = s.h.d[i].ear;
      if (s.h.d[i].flick > prev_flick[i]) ++g.flicks[i];
      prev_flick[i] = s.h.d[i].flick;
    }
    if (changed_now) {
      // two changes in one tick, or within 0.3 s of the last
      int in_tick = 0;
      (void)in_tick;
      if (s.t - last_change < 0.299f) ++g.close;
      last_change = s.t;
    }
    if (downs == n) ++g.all_down;
    if (downs == n - 1 && !down[0]) ++g.doe_last;   // all the others down and the doe up: allowed, counted
  }
}

static void testGraze() {
  printf("[2] the graze cycle\n");
  GrazeStats tot;
  int step_total = 0;
  float max_off = 0.f;
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    Sim s;
    GrazeStats g;
    grazeRun(s, 60.f, g, seed);
    tot.changes += g.changes;
    tot.close += g.close;
    tot.all_down += g.all_down;
    tot.doe_last += g.doe_last;
    tot.down_min = g.down_min < tot.down_min ? g.down_min : tot.down_min;
    tot.down_max = g.down_max > tot.down_max ? g.down_max : tot.down_max;
    tot.up_min = g.up_min < tot.up_min ? g.up_min : tot.up_min;
    tot.up_max = g.up_max > tot.up_max ? g.up_max : tot.up_max;
    step_total += s.steps;
    for (int i = 0; i < s.h.n; ++i) {
      tot.ears[i] += g.ears[i];
      tot.flicks[i] += g.flicks[i];
      const float off = fabsf(s.x[i] - s.h.d[i].spot.x);
      if (off > max_off) max_off = off;
    }
  }
  printf("  20 runs of 60 s: %d head changes, %d within 0.3 s of another, %d ticks with every head down\n", tot.changes,
         tot.close, tot.all_down);
  printf("  bouts: head down %.1f to %.1f s, head up %.1f to %.1f s; %d steps; a deer strays at most %.0f px from its place\n",
         (double)tot.down_min, (double)tot.down_max, (double)tot.up_min, (double)tot.up_max, step_total, (double)max_off);
  CHECK(tot.changes > 200, "only %d head changes in 20 minutes of five deer", tot.changes);
  CHECK(tot.close == 0, "%d head changes within 0.3 s of another", tot.close);
  CHECK(tot.all_down == 0, "every head was down for %d ticks", tot.all_down);
  CHECK(tot.down_min >= 3.5f - 0.05f, "a head-down bout of %.2f s (min 3.5)", (double)tot.down_min);
  CHECK(tot.up_min >= 1.2f - 0.05f, "a head-up bout of %.2f s (min 1.2)", (double)tot.up_min);
  // The longest allow what the rules and a step add: a step is about 1.2 s, the watch rule holds a head
  // up in 0.3 s retries, and a doe may have to stay up while the others take turns.
  CHECK(tot.down_max <= 7.f + 1.2f, "a head-down bout of %.2f s (max 7 and a few retries)", (double)tot.down_max);
  CHECK(tot.up_max <= 10.f, "a head-up bout of %.2f s", (double)tot.up_max);
  CHECK(max_off <= 30.f + 10.f, "a deer strayed %.0f px from its place", (double)max_off);
  for (int i = 0; i < 5; ++i) {
    CHECK(tot.ears[i] >= 20 * 7, "deer %d flicked an ear only %d times in 20 minutes", i, tot.ears[i]);
    CHECK(tot.flicks[i] >= 20 * 3, "deer %d flicked its tail only %d times in 20 minutes", i, tot.flicks[i]);
    CHECK(tot.ears[i] <= 20 * 35, "deer %d flicked an ear %d times in 20 minutes", i, tot.ears[i]);
  }
  // The doe, every tick, with all the others down: never down herself (counted above as all_down == 0). And
  // herdMayLower itself: the last head up may not lower.
  {
    Sim s;
    s.begin(1, 0, planOf(0, 0));
    for (int i = 0; i < s.h.n; ++i) {
      s.h.d[i].entered = 10;
      s.h.d[i].mode = HM_GRAZE;
      s.h.d[i].head_down = 1;
    }
    s.h.d[0].head_down = 0;
    CHECK(!herdMayLower(s.h, 0), "the last head up may lower");
    CHECK(herdMayLower(s.h, 1), "a head may not lower with the doe's up");
    s.h.d[1].head_down = 0;
    CHECK(herdMayLower(s.h, 0) && herdMayLower(s.h, 1), "two heads up: either may lower");
  }
  // the bout lengths, straight from herdGrazeNext
  {
    Rng r;
    r.seed(5);
    float lo_d = 99.f, hi_d = 0.f, lo_u = 99.f, hi_u = 0.f;
    for (int i = 0; i < 4000; ++i) {
      const float d = herdGrazeNext(r, true, 1.f), u = herdGrazeNext(r, false, 1.f);
      lo_d = d < lo_d ? d : lo_d;
      hi_d = d > hi_d ? d : hi_d;
      lo_u = u < lo_u ? u : lo_u;
      hi_u = u > hi_u ? u : hi_u;
    }
    CHECK(lo_d >= 3.5f && hi_d < 7.f && lo_u >= 1.2f && hi_u < 3.f, "herdGrazeNext out of range: %.2f..%.2f, %.2f..%.2f", (double)lo_d,
          (double)hi_d, (double)lo_u, (double)hi_u);
    CHECK(herdGrazeNext(r, true, 1.3f) >= 3.5f * 1.3f, "the shy scale");
  }
}

// ---------------------------------------------------------------------------

static void printLog(int variant, int size) {
  Sim s;
  s.begin(7, 0, planOf(variant, size));
  s.run(kHerdSeconds);
  printf("visit %s, size %d (the orders; the graze cycle's HC_ACT and HE_STEP are the many small ones):\n", kVarName[variant], size);
  for (const Sim::Rec& r : s.log) {
    const char* nm = r.cmd < HC_COUNT ? kCmdName[r.cmd] : "?";
    const char* act = r.cmd == HC_ACT && r.a < CA_COUNT ? critterActionName((CritterAction)r.a) : "";
    printf("  %6.2f  %-7s %-10s %4d %4d %s\n", (double)r.t, kRoleName[s.h.d[r.deer].role], nm, r.a, r.b, act);
  }
}

struct Visit {
  float enter[HERD_MAX];
  float all_gone = -1.f;
  float alarm_t = -1.f;
};

static void testVisit() {
  printf("[3] a whole visit: each variant and size, three layouts, two speeds\n");
  int visits = 0;
  float last_gone_max = 0.f, spread_min = 1e9f, still_true_min = 1e9f;
  for (int variant = 0; variant < 3; ++variant)
    for (int size = 0; size < 3; ++size)
      for (int lk = 0; lk < 3; ++lk)
        for (int sp = 0; sp < 2; ++sp) {
          Sim s;
          s.speed_mul = sp == 0 ? 1.f : 0.85f;
          s.begin(100 + (uint64_t)(variant * 31 + size * 7 + lk), lk, planOf(variant, size));
          char tag[64];
          snprintf(tag, sizeof tag, "%s size %d layout %d speed %.2f", kVarName[variant], size, lk, (double)s.speed_mul);
          float enter_t[HERD_MAX];
          for (int i = 0; i < HERD_MAX; ++i) enter_t[i] = -1.f;
          int max_in = 0;
          float x_at_alarm[HERD_MAX] = {};
          float y_at_alarm[HERD_MAX] = {};
          bool alarm_seen = false;
          float picture_from = variant == 0 ? 73.f : (variant == 1 ? 69.5f : 67.f);
          float all_gone = -1.f;
          // overlap checks
          int pic_overlap = 0, exit_close = 0, exit_pair_i = 0, exit_pair_j = 0;
          float exit_min = 1e9f, exit_pair_t = 0.f;
          const int ticks = (int)(kHerdSeconds / SS_TICK_DT);
          for (int k = 0; k < ticks; ++k) {
            s.step();
            const int in_scene = s.inScene();
            if (in_scene > max_in) max_in = in_scene;
            for (int i = 0; i < s.h.n; ++i)
              if (s.in_use[i] && enter_t[i] < 0.f) enter_t[i] = s.t;
            if (s.h.alarm && !alarm_seen) {
              alarm_seen = true;
              for (int i = 0; i < s.h.n; ++i) {
                x_at_alarm[i] = s.x[i];
                y_at_alarm[i] = s.y[i];
              }
            }
            if (in_scene == 0 && max_in > 0 && all_gone < 0.f && s.t > 80.f) all_gone = s.t;
            // the settled picture: from picture_from to the alarm, no two bounds touch and every pair 2 px apart
            if (s.t >= picture_from && !s.h.alarm && (k % 5) == 0) {
              Rect b[HERD_MAX];
              for (int i = 0; i < s.h.n; ++i) b[i] = deerBox(s.x[i], s.y[i], s.h.d[i].role, s.face[i]);
              for (int i = 0; i < s.h.n; ++i)
                for (int j = i + 1; j < s.h.n; ++j) {
                  const bool apart_x = b[i].x1 + 2 <= b[j].x0 || b[j].x1 + 2 <= b[i].x0;
                  const bool apart_y = b[i].y1 + 2 <= b[j].y0 || b[j].y1 + 2 <= b[i].y0;
                  if (!apart_x && !apart_y) {
                    ++pic_overlap;
                    if (pic_overlap == 1)
                      printf("  %s: at %.1f s the %s (%.0f,%.0f) and the %s (%.0f,%.0f) touch\n", tag, (double)s.t,
                             kRoleName[s.h.d[i].role], (double)s.x[i], (double)s.y[i], kRoleName[s.h.d[j].role], (double)s.x[j],
                             (double)s.y[j]);
                  }
                }
            }
            // the fawn has turned to face its mother by the settled picture (it came back from the water
            // facing away from her)
            if (s.t >= picture_from && s.t < picture_from + SS_TICK_DT * 0.5f + SS_TICK_DT) {
              const int fi = herdIndexOf(s.h, HR_FAWN), di = herdIndexOf(s.h, HR_DOE);
              CHECK(s.face[fi] == (s.x[di] > s.x[fi] ? 1 : 0), "%s: at %.1f s the fawn faces away from its mother", tag, (double)s.t);
            }
            // the way out, sampled every 5 ticks: at true speed no two leavers' canter boxes come within 2 px
            // while either shows in the frame (REVIEW_FIXES F12), the fawn included. The 0.85 runs are not a
            // case the panel has (the driver divides every speed by the dial's factor, so speeds on the screen
            // are the table's) and the leave times assume true speed, so there only the adults' feet are held
            // 28 px apart (they do bunch: every slow run fails the 2 px rule).
            if (s.h.alarm == 1 && (k % 5) == 0) {
              for (int i = 0; i < s.h.n; ++i)
                for (int j = i + 1; j < s.h.n; ++j) {
                  if (!s.in_use[i] || !s.in_use[j] || !s.exiting[i] || !s.exiting[j]) continue;
                  const Rect bi = runBox(s.x[i], s.y[i], s.h.d[i].role, s.face[i]);
                  const Rect bj = runBox(s.x[j], s.y[j], s.h.d[j].role, s.face[j]);
                  if ((bi.x1 <= 0 || bi.x0 >= 800) && (bj.x1 <= 0 || bj.x0 >= 800)) continue;   // both out of sight
                  bool apart = bi.x1 + 2 <= bj.x0 || bj.x1 + 2 <= bi.x0 || bi.y1 + 2 <= bj.y0 || bj.y1 + 2 <= bi.y0;
                  if (sp != 0) {
                    const bool adults = s.h.d[i].role != HR_FAWN && s.h.d[j].role != HR_FAWN && s.x[i] >= 0.f && s.x[j] >= 0.f;
                    apart = !adults || fabsf(s.x[i] - s.x[j]) >= 28.f;
                  }
                  if (!apart) {
                    ++exit_close;
                    if (exit_close == 1)
                      printf("  %s: at %.2f s the leaving %s (%.0f) and %s (%.0f) are under 2 px apart\n", tag, (double)s.t,
                             kRoleName[s.h.d[i].role], (double)s.x[i], kRoleName[s.h.d[j].role], (double)s.x[j]);
                  }
                  if (s.h.d[i].role == HR_FAWN || s.h.d[j].role == HR_FAWN || s.x[i] < 0.f || s.x[j] < 0.f) continue;
                  const float d = fabsf(s.x[i] - s.x[j]);
                  if (d < exit_min) {
                    exit_min = d;
                    exit_pair_t = s.t;
                    exit_pair_i = i;
                    exit_pair_j = j;
                  }
                }
            }
          }
          ++visits;
          // everyone entered, once
          const int want = s.h.n;
          int entered = 0;
          for (int i = 0; i < want; ++i) entered += s.entered_count[i] == 1 ? 1 : 0;
          CHECK(entered == want, "%s: %d of %d deer entered exactly once", tag, entered, want);
          CHECK(max_in == want, "%s: at most %d of %d deer in the scene together", tag, max_in, want);
          CHECK(s.h.alarm == 1 && s.h.trigger == HT_TIME, "%s: alarm %d trigger %d", tag, s.h.alarm, s.h.trigger);
          CHECK(fabsf(s.h.alarm_t - kHerdSoftAt) < 0.1f, "%s: the alarm came at %.2f s", tag, (double)s.h.alarm_t);
          CHECK(all_gone > 0.f, "%s: the camp is not empty at %.0f s", tag, (double)kHerdSeconds);
          // F12: the last stillness stays about 8 s. At true speed (the driver corrects for the speed dial) the
          // camp is empty 6.5 s or more before the beat ends for every size and layout; the 0.85 runs are a
          // stress case (every walk 15 % slow) and keep 3 s.
          const float still_min = s.speed_mul < 1.f ? 3.f : 6.5f;
          CHECK(all_gone <= kHerdSeconds - still_min, "%s: the last deer is out at %.1f s, under %.1f s before the beat's end", tag,
                (double)all_gone, (double)still_min);
          if (all_gone > last_gone_max) last_gone_max = all_gone;
          if (sp == 0 && kHerdSeconds - all_gone < still_true_min) still_true_min = kHerdSeconds - all_gone;
          CHECK(pic_overlap == 0, "%s: %d overlaps in the settled picture", tag, pic_overlap);
          CHECK(exit_close == 0, "%s: %d samples with two leavers under 2 px apart", tag, exit_close);
          if (sp == 0) CHECK(exit_min >= 64.f, "%s: leavers came within %.0f px of each other (the %s and the %s at %.2f s)", tag,
                             (double)exit_min, kRoleName[s.h.d[exit_pair_i].role], kRoleName[s.h.d[exit_pair_j].role],
                             (double)exit_pair_t);
          if (exit_min < spread_min) spread_min = exit_min;
          // every deer is gone at the end and the order of leaving is the doe first, the fawn last
          float first_leave = 1e9f, last_leave = -1.f;
          int first_role = -1, last_role = -1;
          for (int i = 0; i < s.h.n; ++i) {
            CHECK(s.h.d[i].mode == HM_GONE, "%s: the %s is not gone (mode %d)", tag, kRoleName[s.h.d[i].role], s.h.d[i].mode);
            if (s.leave_t[i] < first_leave) {
              first_leave = s.leave_t[i];
              first_role = s.h.d[i].role;
            }
            if (s.leave_t[i] > last_leave) {
              last_leave = s.leave_t[i];
              last_role = s.h.d[i].role;
            }
          }
          CHECK(first_role == HR_DOE, "%s: %s left first", tag, kRoleName[first_role < 0 ? 0 : first_role]);
          CHECK(last_role == HR_FAWN, "%s: %s left last", tag, kRoleName[last_role < 0 ? 0 : last_role]);
          CHECK(fabsf((first_leave - kHerdSoftAt) - 6.f) < 0.15f, "%s: the doe left %.2f s after the alarm", tag, (double)(first_leave - kHerdSoftAt));
          // the soft wave: heads in order, flags a beat behind, only the doe's and the fawn's go all the way
          if (sp == 0) {
            const int doe = herdIndexOf(s.h, HR_DOE), fawn = herdIndexOf(s.h, HR_FAWN), door = herdIndexOf(s.h, HR_DOOR),
                      wall = herdIndexOf(s.h, HR_WALL), g4 = herdIndexOf(s.h, HR_GRAZER), g5 = herdIndexOf(s.h, HR_GRAZER2);
            int order[6], m = 0;
            order[m++] = doe;
            order[m++] = fawn;
            if (g4 >= 0) order[m++] = g4;
            if (g5 >= 0) order[m++] = g5;
            order[m++] = door;
            order[m++] = wall;
            for (int k = 1; k < m; ++k) {
              const float a = s.first_head[order[k - 1]], b = s.first_head[order[k]];
              CHECK(a >= 0.f && b > a, "%s: head order: %s %.2f then %s %.2f", tag, kRoleName[s.h.d[order[k - 1]].role], (double)a,
                    kRoleName[s.h.d[order[k]].role], (double)b);
              if (k >= 2) CHECK(b - a >= 0.25f && b - a <= 0.75f, "%s: gap %.2f s between %s and %s", tag, (double)(b - a),
                                kRoleName[s.h.d[order[k - 1]].role], kRoleName[s.h.d[order[k]].role]);
            }
            for (int k = 0; k < m; ++k) {
              const int i = order[k];
              const float lag = s.first_tail[i] - s.first_head[i];
              if (s.h.d[i].role == HR_DOE) CHECK(lag >= 1.f && lag <= 1.6f, "%s: the doe's flag %.2f s after her head", tag, (double)lag);
              else CHECK(lag >= 0.39f && lag <= 0.81f, "%s: the %s's flag %.2f s after its head", tag, kRoleName[s.h.d[i].role], (double)lag);
              const bool full = s.h.d[i].role == HR_DOE || s.h.d[i].role == HR_FAWN;
              CHECK(s.tail_to[i] == (full ? 1.f : 0.5f), "%s: the %s's flag goes to %.2f", tag, kRoleName[s.h.d[i].role], (double)s.tail_to[i]);
            }
          }
          // when the alarm sounds nothing is moving but the wave, and the stamps hit twice and the door ends ajar
          if (variant != 1) CHECK(s.hits >= 2, "%s: %d hoof hits", tag, s.hits);
          if (variant != 1) CHECK(fabsf(s.door - 0.10f) < 0.001f, "%s: the flap ends at %.2f, want 0.10", tag, (double)s.door);
          else CHECK(fabsf(s.door - 0.35f) < 0.001f, "%s: the false alarm's flap ends at %.2f, want 0.35", tag, (double)s.door);
          CHECK(s.rings >= 3 && s.drops >= 6, "%s: the fawn made %d rings and %d drops", tag, s.rings, s.drops);
          CHECK(s.prints >= 6 && s.prints <= HERD_PRINTS, "%s: %d prints", tag, s.prints);
          (void)x_at_alarm;
          (void)y_at_alarm;
        }
  printf("  %d visits; the camp is empty by %.1f s at the latest (the beat is %.0f s; at true speed at least %.1f s of empty camp);"
         " leavers' feet keep at least %.0f px apart\n",
         visits, (double)last_gone_max, (double)kHerdSeconds, (double)still_true_min, (double)spread_min);
}

// ---------------------------------------------------------------------------

static void testAlarms() {
  printf("[4] the hard alarm, the second look, a visitor\n");
  // a bell at 30, 50 and 70 s: everyone out of sight within 9 s
  for (int variant = 0; variant < 3; ++variant)
    for (float bell : {30.f, 50.f, 70.f}) {
      Sim s;
      s.begin(55 + (uint64_t)variant, 0, planOf(variant, 1));
      s.run(bell);
      s.step(HI_BELL);
      CHECK(s.h.alarm == 2 && s.h.trigger == HT_BELL && s.h.flee == -1, "bell: alarm %d trigger %d flee %d", s.h.alarm, s.h.trigger, s.h.flee);
      const float t0 = s.t;
      float gone = -1.f;
      for (int k = 0; k < (int)(12.f / SS_TICK_DT); ++k) {
        s.step();
        if (s.inScene() == 0 && gone < 0.f) gone = s.t - t0;
      }
      CHECK(gone > 0.f && gone <= 9.f, "bell at %.0f s (%s): the camp is empty after %.1f s", (double)bell, kVarName[variant], (double)gone);
      if (bell < 45.f) s.run(40.f);   // the second look begins 14 s after the last deer left
      // all heads up within 0.8 s, every flag to 1.0, off at +1.1 s (the fawn at +1.4)
      float head_max = 0.f;
      int heads = 0;
      for (const Sim::Rec& r : s.log) {
        if (r.t < t0 - 0.001f) continue;
        if (r.cmd == HC_ACT && (r.a == CA_LOOK) && r.t - t0 < 2.f) {
          ++heads;
          if (r.t - t0 > head_max) head_max = r.t - t0;
        }
        if (r.cmd == HC_TAIL) CHECK(r.a == 100, "hard alarm: a flag to %d", r.a);
      }
      CHECK(head_max <= 0.8f, "bell: the last head went up %.2f s after it", (double)head_max);
      // a deer that was not grazing yet (walking in, or not in) may skip its head; those in the scene did one
      for (int i = 0; i < s.h.n; ++i)
        if (s.leave_t[i] >= 0.f) {
          const float off = s.leave_t[i] - t0;
          const float want = kHerdHardOff + (s.h.d[i].role == HR_FAWN ? 0.3f : 0.f);
          CHECK(fabsf(off - want) < 0.15f || off < want, "bell: the %s left %.2f s after (want %.2f)", kRoleName[s.h.d[i].role], (double)off,
                (double)want);
        }
      (void)heads;
      // the second look: for the early bell and not the 50 or 70 s ones
      const bool expect_second = bell < 45.f;
      CHECK((s.h.second_done != 0) == expect_second, "bell at %.0f s: second look %d", (double)bell, s.h.second_done);
    }
  // the second look in full: bell at 30 s, the doe and the fawn come back 14 s after the last one left,
  // graze, and are driven off by the 77 s alarm
  {
    Sim s;
    s.begin(77, 0, planOf(0, 0));
    s.run(30.f);
    s.step(HI_BELL);
    s.run(10.f);
    CHECK(s.inScene() == 0, "bell at 30: the camp should be empty by 40 s");
    const float gone = s.h.gone_t;
    CHECK(gone > 30.f && gone < 40.f, "gone_t %.2f", (double)gone);
    s.run(kHerdSeconds - 40.f);
    CHECK(s.h.second_done == 1, "no second look");
    CHECK(fabsf(s.h.second_t - (gone + 14.f)) < 0.05f, "the second look began at %.2f, want %.2f", (double)s.h.second_t, (double)(gone + 14.f));
    const int doe = herdIndexOf(s.h, HR_DOE), fawn = herdIndexOf(s.h, HR_FAWN);
    CHECK(s.entered_count[doe] == 2 && s.entered_count[fawn] == 2, "the doe and the fawn entered %d and %d times", s.entered_count[doe], s.entered_count[fawn]);
    CHECK(s.entered_count[herdIndexOf(s.h, HR_DOOR)] == 1, "a yearling came back");
    CHECK(s.h.alarm == 1 && fabsf(s.h.alarm_t - kHerdSoftAt) < 0.1f, "the soft alarm after the second look: %d at %.1f", s.h.alarm, (double)s.h.alarm_t);
    for (int i = 0; i < s.h.n; ++i) CHECK(s.h.d[i].mode == HM_GONE, "deer %d not gone after the second look", i);
  }
  // a dog going out at 40 s: they go right; every deer out within 9 s; a dog already wanted at the start
  // (F21: prev_in starts at 0) is the same alarm on the first tick
  {
    Sim s;
    s.begin(88, 1, planOf(1, 1));
    s.run(40.f);
    s.step(HI_DOG);
    CHECK(s.h.alarm == 2 && s.h.trigger == HT_DOG && s.h.flee == 1, "dog: alarm %d trigger %d flee %d", s.h.alarm, s.h.trigger, s.h.flee);
    const float t0 = s.t;
    float gone = -1.f;
    float xmax_dir = 0.f;
    for (int k = 0; k < (int)(12.f / SS_TICK_DT); ++k) {
      s.step(HI_DOG);   // the dog stays out: a level, not a second edge
      for (int i = 0; i < s.h.n; ++i)
        if (s.exiting[i] && s.in_use[i]) xmax_dir = s.x[i] > xmax_dir ? s.x[i] : xmax_dir;
      if (s.inScene() == 0 && gone < 0.f) gone = s.t - t0;
    }
    CHECK(gone > 0.f && gone <= 9.f, "dog at 40 s: the camp is empty after %.1f s", (double)gone);
    CHECK(xmax_dir > 700.f, "dog: the deer did not run right (%.0f)", (double)xmax_dir);
    s.run(30.f);
    CHECK(s.h.second_done == 0 && s.h.second_t < 0.f, "a dog's alarm gave a second look");
    Sim z;
    z.begin(89, 0, planOf(0, 0));
    z.step(HI_DOG);
    CHECK(z.h.alarm == 2, "a dog wanted from the first tick is not an alarm (F21)");
    for (int i = 0; i < z.h.n; ++i) CHECK(z.h.d[i].mode == HM_GONE, "deer %d not released with the dog out from the start", i);
    z.run(10.f, HI_DOG);
    CHECK(z.log.empty(), "deer were ordered with a dog out from the start (%d orders)", (int)z.log.size());
  }
  // lightning: hard, to the left
  {
    Sim s;
    s.begin(90, 2, planOf(2, 0));
    s.run(36.f);
    s.step(HI_FLASH);
    CHECK(s.h.alarm == 2 && s.h.trigger == HT_FLASH && s.h.flee == -1, "flash: alarm %d trigger %d", s.h.alarm, s.h.trigger);
  }
  // a deer not yet in the frame at a bell turns back; one not yet entered is released and never comes
  {
    Sim s;
    s.begin(91, 0, planOf(0, 0));
    s.run(3.5f);   // the doe at x about -10 (walking in since 2 s)
    CHECK(s.in_use[0] && s.x[0] < 40.f, "the doe should be just in (x %.0f)", (double)s.x[0]);
    s.step(HI_BELL);
    for (int i = 1; i < s.h.n; ++i) CHECK(s.h.d[i].mode == HM_GONE, "deer %d (not yet in) should be let go", i);
    s.run(8.f);
    CHECK(s.inScene() == 0, "the doe did not leave");
    for (int i = 1; i < s.h.n; ++i) CHECK(s.entered_count[i] == 0, "deer %d came in after the bell", i);
  }
  // a bell before any deer is in (1 s): nobody comes then, but the second look still comes 14 s later, so the
  // beat is not a whole empty camp
  {
    Sim s;
    s.begin(92, 0, planOf(0, 0));
    s.run(1.0f);
    s.step(HI_BELL);
    CHECK(s.h.alarm == 2 && fabsf(s.h.gone_t - s.t) < 0.05f, "a bell at 1 s: alarm %d, gone_t %.2f", s.h.alarm, (double)s.h.gone_t);
    s.run(kHerdSeconds - s.t);
    const int doe = herdIndexOf(s.h, HR_DOE), fawn = herdIndexOf(s.h, HR_FAWN);
    CHECK(s.h.second_done == 1 && fabsf(s.h.second_t - 15.f) < 0.1f, "a bell at 1 s: second look %d at %.2f s", s.h.second_done,
          (double)s.h.second_t);
    CHECK(s.entered_count[doe] == 1 && s.entered_count[fawn] == 1, "a bell at 1 s: the doe and the fawn entered %d and %d times",
          s.entered_count[doe], s.entered_count[fawn]);
    for (int i = 0; i < s.h.n; ++i) CHECK(s.h.d[i].mode == HM_GONE, "a bell at 1 s: deer %d not gone at the end", i);
  }
  // a visitor at the panel: three heads up within 0.6 s, nobody leaves, grazing again by 5 s; a third time nothing
  {
    Sim s;
    GrazeStats g;
    grazeRun(s, 20.f, g, 3);
    s.log.clear();
    s.run(0.f);
    const int before = (int)s.log.size();
    (void)before;
    s.step(HI_PASSER);
    int looks = 0;
    float t_first = s.t;
    for (const Sim::Rec& r : s.log)
      if (r.cmd == HC_ACT && r.a == CA_HEADTURN) ++looks;
    s.step(HI_PASSER);   // held: no second edge
    s.run(0.2f, HI_PASSER);
    for (const Sim::Rec& r : s.log)
      if (r.cmd == HC_ACT && r.a == CA_HEADTURN) ++looks;
    looks = 0;
    for (const Sim::Rec& r : s.log)
      if (r.cmd == HC_ACT && r.a == CA_HEADTURN) ++looks;
    CHECK(looks == 3, "a visitor made %d deer look up, want 3", looks);
    CHECK(s.h.looks == 1, "looks %d", s.h.looks);
    const int doe_i = 0;
    CHECK(s.h.d[doe_i].look_t > 2.5f, "the doe is not holding (look_t %.2f)", (double)s.h.d[doe_i].look_t);
    int holding = 0;
    for (int i = 0; i < s.h.n; ++i) holding += s.h.d[i].look_t > 0.f ? 1 : 0;
    CHECK(holding == 3, "%d deer holding", holding);
    for (int i = 0; i < s.h.n; ++i) {
      const HerdView vw = herdView(s.h.d[i]);
      // a watching deer's ear is forward, or in the middle of a flick (half back or back): never plain up
      if (s.h.d[i].look_t > 0.f)
        CHECK(vw.ear == kDeerEarFwd || vw.ear == kDeerEarMid || vw.ear == kDeerEarBack, "a watching deer's ears (%d)", vw.ear);
    }
    s.run(3.2f);
    holding = 0;
    for (int i = 0; i < s.h.n; ++i) holding += s.h.d[i].look_t > 0.f ? 1 : 0;
    CHECK(holding == 0, "still %d holding after 3.4 s", holding);
    s.run(6.f);   // grazing again, nobody left
    int grazing = 0;
    for (int i = 0; i < s.h.n; ++i) grazing += s.h.d[i].mode == HM_GRAZE ? 1 : 0;
    CHECK(grazing == s.h.n && s.h.alarm == 0, "after the visitor: %d grazing, alarm %d", grazing, s.h.alarm);
    s.step(0);
    s.step(HI_PASSER);   // a second look is allowed
    s.step(0);
    s.run(1.f);
    s.step(HI_PASSER);   // a third is not
    CHECK(s.h.looks == 2, "the visitor looked %d times, at most 2", s.h.looks);
    (void)t_first;
  }
}

// ---------------------------------------------------------------------------

static void drawPrints(const HerdState& h, std::vector<Pixel>& px, Pixel bg, const Palette& pal, int W, int H) {
  px.assign((size_t)W * (size_t)H, bg);
  Surface s{px.data(), W, H, W, 0};
  herdPrintsDraw(h, s, pal, Rect{0, 0, W, H});
}

static void testPrints() {
  printf("[5] the hoof prints\n");
  const int W = 520, H = 480;
  const Palette& pal = moodKey(MOOD_DAY);
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  HerdState h;
  Rng r;
  r.seed(3);
  herdInit(h, r, layoutFor(0), planOf(0, 0), false);
  CHECK(h.n_prints == 0 && herdPrintsBounds(h).empty() && !herdPrintsLive(h), "an empty herd has prints");
  for (int i = 0; i < HERD_PRINTS; ++i) CHECK(herdPrintAdd(h, 300 + i * 11, 380 + (i % 3) * 4, i < 5), "print %d refused", i);
  CHECK(!herdPrintAdd(h, 10, 10, true), "a 15th print was accepted");
  CHECK(herdPrintsLive(h), "no live prints");
  // the bounds hold every pixel the draw touches, at every alpha it goes through
  std::vector<Pixel> px;
  const Pixel bg = pal[GROUND];
  int bad = 0, drawn = 0;
  h.t = 0.f;
  for (int step = 0; step < 4000; ++step) {   // 160 s: dry, seen, faded
    if (step == 1500) herdPrintsSeen(h);
    if (step % 40 == 0) {
      drawPrints(h, px, bg, pal, W, H);
      const Rect b = herdPrintsBounds(h);
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
          if (px[(size_t)y * (size_t)W + (size_t)x] != bg) {
            ++drawn;
            if (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) ++bad;
          }
    }
    herdPrintsStep(h, SS_TICK_DT);
  }
  printf("  %d inked pixels checked against the bounds, %d outside\n", drawn, bad);
  CHECK(drawn > 0, "nothing drew");
  CHECK(bad == 0, "%d print pixels outside the bounds", bad);
  CHECK(herdPrintsDone(h) && herdPrintsBounds(h).empty(), "the fade did not end");
  // a clip: drawing in strips equals drawing whole
  {
    HerdState g;
    herdInit(g, r, layoutFor(0), planOf(0, 0), false);
    for (int i = 0; i < 9; ++i) herdPrintAdd(g, 20 + i * 50, 30 + i * 47, i % 2 == 0);
    std::vector<Pixel> whole, strips((size_t)W * (size_t)H, bg);
    drawPrints(g, whole, bg, pal, W, H);
    Surface s{strips.data(), W, H, W, 0};
    for (int y0 = 0; y0 < H; y0 += 32) herdPrintsDraw(g, s, pal, Rect{0, y0, W, y0 + 32 < H ? y0 + 32 : H});
    CHECK(whole == strips, "strip-by-strip print draw differs from the whole");
    Surface sp{strips.data() + 64 * W, W, 64, W, 64};   // a strip surface with an origin: rows 64..127
    std::vector<Pixel> ref((size_t)W * 64, bg);
    Surface sr{ref.data(), W, 64, W, 64};
    herdPrintsDraw(g, sr, pal, sr.bounds());
    bool same = true;
    for (int y = 64; y < 128; ++y)
      for (int x = 0; x < W; ++x) same = same && ref[(size_t)(y - 64) * (size_t)W + (size_t)x] == whole[(size_t)y * (size_t)W + (size_t)x];
    CHECK(same, "a strip surface with an origin draws other pixels");
    (void)sp;
  }
  // herdDirty: empty when nothing changed; when something did, the rows hold every pixel that differs;
  // over a whole life it reports a handful of times, not every tick, and the fade's end once
  {
    HerdState g;
    herdInit(g, r, layoutFor(0), planOf(0, 0), false);
    for (int i = 0; i < 7; ++i) herdPrintAdd(g, 100 + i * 20, 390 + (i % 2) * 5, i < 5);
    HerdState prev = g;
    std::vector<Pixel> a, b;
    int dirty_ticks = 0, missed = 0, false_same = 0, end_reports = 0;
    bool was_live = true;
    for (int step = 0; step < 3200; ++step) {   // 128 s: dries 40 s, seen at 60 s, gone by 85
      if (step == 1500) herdPrintsSeen(g);
      herdPrintsStep(g, SS_TICK_DT);
      Rect d;
      const bool dirty = herdDirty(prev, g, d);
      const bool same = herdSame(prev, g);
      CHECK(dirty == !same, "herdSame and herdDirty disagree");
      drawPrints(prev, a, bg, pal, W, H);
      drawPrints(g, b, bg, pal, W, H);
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
          if (a[(size_t)y * (size_t)W + (size_t)x] != b[(size_t)y * (size_t)W + (size_t)x]) {
            if (!dirty || x < d.x0 || x >= d.x1 || y < d.y0 || y >= d.y1) ++missed;
          }
      if (!dirty && a != b) ++false_same;
      if (dirty) ++dirty_ticks;
      const bool live = herdPrintsLive(g);
      if (was_live && !live) {
        CHECK(dirty && herdPrintsBounds(g).empty(), "the last fade step reported no rows or kept bounds");
        ++end_reports;
      }
      was_live = live;
      prev = g;
    }
    printf("  a life of 128 s: %d ticks of 3200 were dirty; the end of the fade reported %d time(s)\n", dirty_ticks, end_reports);
    CHECK(missed == 0, "%d pixels changed outside the rows herdDirty gave", missed);
    CHECK(false_same == 0, "%d ticks said the same when the picture changed", false_same);
    CHECK(dirty_ticks > 8 && dirty_ticks < 120, "%d dirty ticks in a print's life", dirty_ticks);
    CHECK(end_reports == 1, "the end reported %d times", end_reports);
    CHECK(herdPrintsDone(g), "not done");
  }
  // alpha: wet 152 (150 as the plan has it, quantized) down to 72; scuffs 88; fade in 8 steps
  {
    HerdState g;
    herdInit(g, r, layoutFor(0), planOf(0, 0), false);
    herdPrintAdd(g, 100, 380, true);
    herdPrintAdd(g, 120, 380, false);
    CHECK(herdPrintAlpha(g, 0) >= 144 && herdPrintAlpha(g, 0) <= 152, "a fresh wet print alpha %d", herdPrintAlpha(g, 0));
    CHECK(herdPrintAlpha(g, 1) == 88, "a scuff alpha %d", herdPrintAlpha(g, 1));
    g.t = 40.f;
    CHECK(herdPrintAlpha(g, 0) >= 72 && herdPrintAlpha(g, 0) <= 80, "a dry print alpha %d", herdPrintAlpha(g, 0));
    int steps_seen = 0, last = herdPrintAlpha(g, 1);
    herdPrintsSeen(g);
    for (int k = 0; k < 1000; ++k) {
      herdPrintsStep(g, SS_TICK_DT);
      const int a = herdPrintAlpha(g, 1);
      if (a != last) {
        ++steps_seen;
        CHECK(a < last, "the fade went up");
        last = a;
      }
    }
    CHECK(steps_seen >= 7 && steps_seen <= 9, "the fade took %d steps", steps_seen);
    CHECK(last == 0, "the fade ended at %d", last);
  }
}

// ---------------------------------------------------------------------------

static void testDeterminism() {
  printf("[6] determinism\n");
  int n = 0;
  for (int variant = 0; variant < 3; ++variant)
    for (int size = 0; size < 3; ++size) {
      uint64_t h[2];
      for (int run = 0; run < 2; ++run) {
        Sim s;
        s.begin(300 + (uint64_t)variant * 5 + (uint64_t)size, size, planOf(variant, size), size == 2);
        s.run(kHerdSeconds);
        h[run] = fnv(s.hash, &s.h, sizeof s.h);
      }
      ++n;
      CHECK(h[0] == h[1], "%s size %d: two runs differ (%016llx, %016llx)", kVarName[variant], size, (unsigned long long)h[0],
            (unsigned long long)h[1]);
    }
  // different seeds give different visits
  Sim a, b;
  a.begin(1, 0, planOf(0, 0));
  b.begin(2, 0, planOf(0, 0));
  a.run(60.f);
  b.run(60.f);
  CHECK(a.hash != b.hash, "two seeds gave the same visit");
  printf("  %d visits run twice, identical\n", n);
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "log") == 0) {
    for (int v = 0; v < 3; ++v) printLog(v, 0);
    return 0;
  }
  testTables();
  testGraze();
  testVisit();
  testAlarms();
  testPrints();
  testDeterminism();
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
