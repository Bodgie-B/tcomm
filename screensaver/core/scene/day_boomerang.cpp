// The boomerang and its eagle as a surprise major. See day_boomerang.h.
//
// The script (kBoomA, kBoomB, kBoomC) is the hiker's half. His arm in the throw and the catch is
// driven from the tick (boomMajorTick) off the boomerang's own clock (BoomFlight::t, which starts at
// boomPlan), so the release lands on the throw tick exactly: the boomerang is in his hand until
// the module's throw time T, his arm reaches the release pose at T, and the flight starts at that
// pose's hand (releaseHand). The script waits on kE1, which the tick raises when the ending has
// happened and settled. Scratch use of MajorState, all private to this file:
//   p[0] his throw spot   p[1] C: where he stands at the tree   p[2] C: the trunk point
//   stage[0]  0 not planned, 1 flying, 2 the ending happened (waiting to settle), 3 his arm
//             fading out, 4 done
//   u0  C: the shoulder angle of his grip at the tree   u1  the clock time the ending settles
//   mt  C: the tug's wobble (turns), applied to the stuck boomerang after the module's step
//   count  the tugs
// Cue bits: the tick sets kE1.
// No random draws: the module's ten draws come from a private Rng seeded from the layout.
#include "day_boomerang.h"

#include <math.h>

#include "../anim/ease.h"
#include "boomerang.h"
#include "director.h"
#include "landscape.h"
#include "memory.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

constexpr uint16_t kE1 = 256;

MajorState& M(World& w) { return w.major; }
inline void fire(MajorState& m, uint16_t bit) { m.cue = (uint16_t)(m.cue | bit); }
inline bool has(const MajorState& m, uint16_t bit) { return (m.cue & bit) != 0; }
uint8_t variantOf(const MajorState& m) { return (uint8_t)(m.var & 3u); }
float env(float u, float a, float b) { return scriptEnvelope(u, a, b); }

float bump(float t, float a, float b, float c, float d) {
  return easeInOut(window(t, a, b)) * (1.f - easeInOut(window(t, c, d)));
}

// ---- tuning (PROPOSED; how it looks is UNVERIFIED) -------------------------------------------------

constexpr float kRelSh = 2.0f, kRelEl = 0.15f, kRelLean = 0.12f;    // the release: the arm out ahead, above level
constexpr float kCockSh = 3.3f, kCockEl = 0.9f;                      // the wind-up: the arm up and back
constexpr float kWatchSh = 1.95f, kWatchEl = 1.35f;                  // a hand to the brow
constexpr float kInspectSh = 1.55f, kInspectEl = 1.25f;              // holding it up to look at
constexpr float kWindUp = 5.2f;                                      // s from boomPlan to the throw
constexpr float kReach = -140.f, kRise = 84.f, kDip = 24.f, kMiss = 56.f;
constexpr float kFlightS[3] = {3.4f, 3.4f, 4.2f};                    // throw to catch (A), snatch (B), hit (C)
constexpr float kHandTurns = 0.30f;                                  // its angle in his hand (the tips toward his front)
// C: the tip goes into the camp pine's trunk on its left face (trunk 9 px wide), under the lowest
// foliage (the trunk's bare part is y 318 to 340); the free arm hangs out to the left and down.
constexpr float kTrunkTurns = 0.43f;
constexpr float kTrunkDy = -18.f;                                    // from the pine's base

Vec2 trunkPoint(const World& w) {
  return Vec2{(float)w.land.camp_x - 4.5f, (float)w.land.camp.base_y + kTrunkDy};
}

uint64_t seedOf(const World& w) {
  return ((uint64_t)(uint32_t)(w.fire.pos.x * 16.f) << 20) ^ (uint64_t)(uint32_t)(w.fire.pos.y * 16.f) ^ 0xB00E0000ull;
}

// His near hand when he has the throw's release pose (feet where they are, facing left): the same
// pose stepHiker composes from POSE_STAND plus the fx the tick sets (arm angles at weight 1, lean).
Vec2 releaseHand(const World& w) {
  Hiker t = w.hiker;
  t.facing = -1;
  t.walking = false;
  t.walk_w = 0.f;
  t.pose = hikerPoseDef(POSE_STAND);
  t.pose.lean += kRelLean;
  t.pose.shoulder[0] = kRelSh;
  t.pose.elbow[0] = kRelEl;
  return hikerHandPos(t, 0);
}

// Where he stands, and the arm angle he reaches with, so his hand meets the stuck boomerang's free tip.
void gripAt(const World& w, Vec2 tip, Vec2& stand, float& sh, float lane_y) {
  Hiker t = w.hiker;
  t.facing = 1;
  t.walking = false;
  t.walk_w = 0.f;
  t.feet = Vec2{0.f, lane_y};
  float best = 1e9f;
  sh = 1.2f;
  float dx = 0.f;
  for (int i = 0; i <= 16; ++i) {
    const float s = 0.5f + 0.1f * (float)i;
    t.pose = hikerPoseDef(POSE_STAND);
    t.pose.shoulder[0] = s;
    t.pose.elbow[0] = 0.25f;
    const Vec2 hnd = hikerHandPos(t, 0);
    const float d = fabsf(hnd.y - tip.y);
    if (d < best) {
      best = d;
      sh = s;
      dx = hnd.x;
    }
  }
  stand = Vec2{tip.x - dx, lane_y};
}

// ---- his actions -----------------------------------------------------------------------------------

// Plan the flight: the boomerang appears in his hand. Everything after this is on its clock.
void doPlan(World& w, float) {
  MajorState& m = M(w);
  BoomSpec s;
  s.ending = variantOf(m) > 2 ? (uint8_t)BOOM_CATCH : variantOf(m);
  s.hand = releaseHand(w);
  s.catch_pt = s.hand;   // A: it comes back to the release pose's hand (he reaches out for it)
  s.trunk = trunkPoint(w);
  s.trunk_turns = kTrunkTurns;
  s.reach = kReach;
  s.rise = kRise;
  s.dip = kDip;
  s.seconds = kFlightS[s.ending];
  s.wind_up = kWindUp;
  s.miss = kMiss;
  s.hand_turns = kHandTurns;
  Rng rng;
  rng.seed(seedOf(w));
  boomPlan(w.boom, s, rng);
  m.stage[0] = 1;
  m.u1 = 1e9f;
  if (s.ending == BOOM_STICK) {   // where he stands to tug at it: the final state is the module's own
    BoomFlight end = w.boom;
    boomStep(end, 60.f);
    const Vec2 tip = boomerangTip(end.boom, true);
    const float lane = scriptGroundAt(w, tip.x - 25.f, m.p[0].y);
    Vec2 stand;
    float sh;
    gripAt(w, tip, stand, sh, lane);
    stand.y = scriptGroundAt(w, stand.x, m.p[0].y);
    m.p[1] = stand;
    m.u0 = sh;
  }
}

bool resolved(const World& w) { return (w.major.cue & kE1) != 0; }

// C: he tugs at it, twice, and it will not come. The wobble is the stuck boomerang's, set here and
// applied by the tick after the module's step.
void actTug(World& w, float u) {
  MajorState& m = M(w);
  const float t = u * 4.6f;
  const float p1 = bump(t, 0.5f, 1.0f, 1.2f, 1.7f), p2 = bump(t, 2.4f, 2.9f, 3.2f, 3.7f);
  const float pull = p1 > p2 ? p1 : p2;
  const float e = env(u, 0.1f, 0.1f);
  scriptSetArm(w, 0, e, m.u0, 0.25f);
  scriptSetArm(w, 1, 0.8f * e, m.u0 - 0.15f, 0.35f);
  w.fx.lean = -0.18f * pull + 0.05f * e;
  w.fx.head = 0.12f * e - 0.06f * pull;
  w.fx.lift = 0.012f * pull;
  m.mt = 0.035f * pull * sinT(t * 9.f);   // (turns about the embedded tip)
  if (u >= 1.f) {
    scriptFxRest(w);
    m.mt = 0.f;
  }
}
void actShrug(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 0, e, 0.9f, 1.7f);
  scriptSetArm(w, 1, 0.8f * e, 0.8f, 1.6f);
  w.fx.head = 0.2f * e;
  w.fx.lift = 0.012f * e;
  if (u >= 1.f) scriptFxRest(w);
}
void doAway(World& w, float) {
  if (w.boom.phase != BP_STUCK) w.boom.boom.on = false;   // put away (in his hand: into the jacket)
}

#define STAND pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
// Up, to his throw spot, facing out over the camp; the boomerang in his hand and the throw (the tick).
#define INTRO STAND, go(AT_M0, 30.f), face(LEFT), still(0.4f), doit(doPlan), until(resolved)

// A: caught, a little surprised; hands on hips, proud.
const Step kBoomA[] = {
    INTRO,
    pose(POSE_HANDS_ON_HIPS, 0.8f, 2.6f),
    doit(doAway), still(0.4f),
    TO_SIT,
};
// B: the eagle has it; he watches it go, shrugs.
const Step kBoomB[] = {
    INTRO,
    still(0.6f), act(actShrug, 2.4f), pose(POSE_HANDS_ON_HIPS, 0.8f, 1.4f),
    TO_SIT,
};
// C: stuck in the pine. A scratch of the head, to the tree, two tugs, nothing, back.
const Step kBoomC[] = {
    INTRO,
    pose(POSE_SCRATCH_HEAD, 0.7f, 1.5f), pose(POSE_STAND, 0.6f, 0.1f),
    walk(AT_M1, 32.f), face(RIGHT), still(0.5f),
    act(actTug, 4.6f), still(0.4f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 1.2f), act(actShrug, 2.2f),
    TO_SIT,
};

#undef STAND
#undef TO_SIT
#undef INTRO

// ---- the arm, off the boomerang's clock --------------------------------------------------------------

void applyArm(World& w, float k, float sh, float el, float lean, float head, float lift) {
  scriptSetArm(w, 0, k, sh, el);
  scriptSetArm(w, 1, 0.5f * k, 1.1f, 0.5f);
  w.fx.lean = lean;
  w.fx.head = head;
  w.fx.lift = lift;
}

// His arm at clock time t (the throw is at T, plan.dur later the ending, te when it has settled).
void armTick(World& w) {
  MajorState& m = M(w);
  const BoomFlight& f = w.boom;
  const uint8_t var = variantOf(m);
  const float t = f.t, T = f.plan.t_throw;
  float k = 1.f, sh = kRelSh, el = kRelEl, lean = kRelLean, head = 0.f, lift = 0.f;
  if (t < T - 1.1f) {   // he turns it over in his hand and looks at it
    k = bump(t, 0.3f, 1.2f, T - 1.6f, T - 1.1f);
    sh = kInspectSh + 0.08f * sinT(t * 0.8f);
    el = kInspectEl;
    lean = 0.f;
    head = 0.2f * k;
  } else if (t < T - 0.30f) {   // the wind-up: the arm up and back
    k = easeInOut(window(t, T - 1.1f, T - 0.45f));
    sh = kCockSh;
    el = kCockEl;
    lean = -0.06f * k;
  } else if (t < T) {   // the whip
    const float wv = easeOutCubic(window(t, T - 0.30f, T));
    sh = lerp(kCockSh, kRelSh, wv);
    el = lerp(kCockEl, kRelEl, wv);
    lean = lerp(-0.06f, kRelLean, wv);
  } else {
    const float tr = t - T;
    const float tl = f.plan.t_land;
    if (var == 1) {   // B: the arm stays out, the head follows it up
      head = -0.12f * easeInOut(window(tr, 0.3f, 1.0f));
    } else {          // A, C: he follows it with a hand to the brow; A reaches out again for the catch
      float watch = easeInOut(window(tr, 0.4f, 1.1f));
      if (var == 0) watch *= 1.f - easeInOut(window(t, tl - 1.1f, tl - 0.35f));
      sh = lerp(kRelSh, kWatchSh, watch);
      el = lerp(kRelEl, kWatchEl, watch);
      lean = kRelLean * (1.f - watch);
      head = -0.14f * watch;
      if (var == 0 && f.phase == BP_HAND) {   // caught: a start back, then he looks at it
        const float c = t - tl;
        lift = 0.02f * bump(c, 0.f, 0.08f, 0.12f, 0.4f);
        lean = kRelLean - 0.2f * bump(c, 0.f, 0.1f, 0.2f, 0.6f);
        head = -0.1f * bump(c, 0.f, 0.15f, 0.3f, 0.8f);
      }
    }
  }
  if (m.stage[0] >= 3) k *= 1.f - easeInOut(window(t, m.u1, m.u1 + 0.8f));   // the ending is over: it fades
  applyArm(w, k, sh, el, lean, head, lift);
}

// C: rotate the stuck boomerang about the embedded tip by `turns` (a tug). The module recomputes its
// rest pose every step, so this is laid on after boomStep, every tick of the tugging, and gone after.
void wobble(BoomFlight& f, float turns) {
  Boomerang& b = f.boom;
  const Vec2 tip = boomerangTip(b, false);
  const float c = cosT(turns), s = sinT(turns);
  const float dx = b.pos.x - tip.x, dy = b.pos.y - tip.y;
  b.pos = Vec2{tip.x + dx * c - dy * s, tip.y + dx * s + dy * c};
  b.rot += turns;
}

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------

uint8_t boomPlanVar(uint32_t r) { return (uint8_t)((r >> 5) % 3u); }

// PROPOSED seconds: the script's length plus slack (tests/test_day_majors.cpp wants 3 s of it spare).
int boomSeconds(uint8_t var) {
  switch (var & 3u) {
    case 0: return 25;
    case 1: return 26;
    default: return 48;
  }
}

const Step* boomScript(uint8_t var, uint8_t& n) {
  const int v = var & 3;
  if (v == 1) {
    n = (uint8_t)(sizeof(kBoomB) / sizeof(kBoomB[0]));
    return kBoomB;
  }
  if (v == 2) {
    n = (uint8_t)(sizeof(kBoomC) / sizeof(kBoomC[0]));
    return kBoomC;
  }
  n = (uint8_t)(sizeof(kBoomA) / sizeof(kBoomA[0]));
  return kBoomA;
}

void boomMajorBegin(World& w) {
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  const float x = F.x + 100.f;
  m.p[0] = Vec2{x, scriptGroundAt(w, x, F.y - 10.f)};
  m.p[1] = m.p[2] = m.p[0];
  m.count = 0;
  m.u0 = m.u1 = m.mt = 0.f;
  m.stage[0] = 0;
  w.boom = BoomFlight{};
}

void boomMajorTick(World& w, float dt) {
  MajorState& m = M(w);
  if (m.stage[0] == 0) return;
  BoomFlight& f = w.boom;
  const uint8_t var = variantOf(m);
  boomStep(f, dt);
  if (m.stage[0] == 1) {   // the ending: when it happens, and when it has settled (te)
    if ((f.events & BE_CAUGHT) != 0) {
      m.stage[0] = 2;
      m.u1 = f.t + 0.4f;
    } else if ((f.events & BE_EAGLE_GONE) != 0) {
      m.stage[0] = 2;
      m.u1 = f.t + 0.3f;
    } else if ((f.events & BE_STUCK) != 0) {
      m.stage[0] = 2;
      m.u1 = f.t + 0.8f;
      w.memory.boom_stuck = 2;   // it stays up there: this loop and the next
    }
  }
  if (m.stage[0] == 2 && f.t >= m.u1) {
    fire(m, kE1);
    m.stage[0] = 3;
  }
  if (var == 2 && m.stage[0] < 3 && f.t >= f.plan.t_throw + 0.62f * f.plan.dur) w.hiker.facing = 1;   // it is overhead: he turns to follow it
  if (m.stage[0] <= 3) armTick(w);
  if (m.stage[0] == 3 && f.t >= m.u1 + 0.8f) {
    scriptFxRest(w);
    m.stage[0] = 4;
  }
  if (var == 2 && f.phase == BP_STUCK && m.mt != 0.f) wobble(f, m.mt);
}

void boomMajorEnd(World& w) {
  MajorState& m = M(w);
  m.mt = 0.f;
  BoomFlight& f = w.boom;
  if (f.phase == BP_STUCK) {
    boomStep(f, 0.f);   // its rest pose exactly, whatever the tugging left
    f.events = 0;
  } else {
    f.boom.on = false;
    f.eagle.on = false;
  }
}

// ---- the world's hooks -------------------------------------------------------------------------------------

void boomWorldStep(World& w) {
  BoomFlight& f = w.boom;
  if (!f.boom.on || !(f.phase == BP_WAIT || f.phase == BP_HAND)) return;
  if (!(w.major.on && w.major.id == MJ_BOOMERANG)) return;
  f.boom.pos = hikerHandPos(w.hiker, 0);   // in his hand: before the throw, after the catch
}

void boomRestoreStuck(World& w) {
  BoomSpec s;
  s.ending = BOOM_STICK;
  s.hand = Vec2{300.f, 340.f};   // (the flight is not shown: only where it ends)
  s.catch_pt = s.hand;
  s.trunk = trunkPoint(w);
  s.trunk_turns = kTrunkTurns;
  s.reach = kReach;
  s.rise = kRise;
  s.dip = kDip;
  s.seconds = kFlightS[BOOM_STICK];
  s.wind_up = kWindUp;
  s.miss = kMiss;
  s.hand_turns = kHandTurns;
  Rng rng;
  rng.seed(seedOf(w));
  boomPlan(w.boom, s, rng);
  boomStep(w.boom, 60.f);   // a pure function of the clock: straight to the end, still in the trunk
  w.boom.events = 0;
}

}  // namespace ss
