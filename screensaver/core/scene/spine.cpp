// The story spine. See spine.h.
//
// A script is a const table of Steps. The interpreter runs the current step
// each tick; instant steps (face, carry, do) chain within one tick, waiting
// steps (walk, pose hold, timed action) hold the script until they finish.
// Positions are anchors in the CampLayout plus an offset, so one table fits
// every seed.
//
// Adding a variant: write a Step table, add it to the beat's kScripts row and
// its duration to kDurations (whole seconds, a few more than the script needs:
// the test fails on any overrun). Adding a beat: a row in both tables, a name,
// and a LoopPlan field (spineVariant maps beat ids to fields).
#include "spine.h"

#include <math.h>
#include <stdio.h>

#include "../anim/ease.h"
#include "../ss_app.h"
#include "world.h"

namespace ss {

enum StepOp : uint8_t {
  OP_PLACE,      // feet at the anchor, facing a (0 left, 1 right), in the scene
  OP_WALK,       // walk to the anchor at s px/s; waits for arrival
  OP_VIA,        // walk toward the anchor; done within 4 px, so the next walk keeps the stride
  OP_POSE,       // ease into pose a over x seconds, then hold until s seconds from the start
  OP_WAIT,       // s seconds of nothing new
  OP_WAIT_AWAY,  // the plan's away_s seconds (firewood, out of frame)
  OP_FACE,       // a: 0 left, 1 right, 2 toward the fire
  OP_CARRY,      // hikerCarry(a)
  OP_ACT,        // fn(w, u) every tick for s seconds, u 0..1 (exactly 1 on the last tick)
  OP_DO,         // fn(w, 1) once
};

enum Anchor : uint8_t {
  AT_HERE = 0, AT_SPOT, AT_PAST, AT_TENT_WORK, AT_PACK, AT_PEG_ROCK, AT_WOOD_WORK, AT_FIRE,
  AT_BLOW, AT_SIT, AT_COOL, AT_ROCK_SIT, AT_FORD0, AT_FORD1, AT_EDGE_L, AT_EDGE_R
};

typedef void (*ActFn)(World& w, float u);

struct Step {
  uint8_t op;
  uint8_t a;       // pose, carry, facing
  uint8_t at;      // anchor
  float x, y;      // offset from the anchor (OP_POSE: x is the blend seconds)
  float s;         // seconds, or px/s for walks
  ActFn fn;
};

namespace {

// ---- step constructors (keep the tables readable) ---------------------------

constexpr Step place(uint8_t at, int face) { return Step{OP_PLACE, (uint8_t)face, at, 0.f, 0.f, 0.f, nullptr}; }
constexpr Step walk(uint8_t at, float v, float x = 0.f, float y = 0.f) {
  return Step{OP_WALK, 0, at, x, y, v, nullptr};
}
constexpr Step via(uint8_t at, float v, float x = 0.f, float y = 0.f) {
  return Step{OP_VIA, 0, at, x, y, v, nullptr};
}
constexpr Step pose(PoseId p, float blend, float hold) {
  return Step{OP_POSE, (uint8_t)p, 0, blend, 0.f, blend + hold, nullptr};
}
constexpr Step still(float s) { return Step{OP_WAIT, 0, 0, 0.f, 0.f, s, nullptr}; }
constexpr Step waitAway() { return Step{OP_WAIT_AWAY, 0, 0, 0.f, 0.f, 0.f, nullptr}; }
constexpr Step face(int dir) { return Step{OP_FACE, (uint8_t)dir, 0, 0.f, 0.f, 0.f, nullptr}; }
constexpr Step carry(CarryId c) { return Step{OP_CARRY, (uint8_t)c, 0, 0.f, 0.f, 0.f, nullptr}; }
constexpr Step act(ActFn fn, float s) { return Step{OP_ACT, 0, 0, 0.f, 0.f, s, fn}; }
constexpr Step doit(ActFn fn) { return Step{OP_DO, 0, 0, 0.f, 0.f, 0.f, fn}; }

constexpr int LEFT = 0, RIGHT = 1, TO_FIRE = 2;

// Normal stroll, careful wading, a run.
constexpr float kStroll = 34.f, kWade = 22.f, kRun = 78.f;

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

// ---- actions -------------------------------------------------------------------
// Each takes u 0..1 over its step and leaves the world at rest at u = 1.

// Two nods: decided.
void actNod(World& w, float u) { w.fx.head = 0.26f * (0.5f - 0.5f * cosT(2.f * u)); }

// Lean forward, slip the pack off halfway, straighten up.
void actShrug(World& w, float u) {
  w.fx.lean = 0.22f * sinT(0.5f * u);
  if (u >= 0.5f && w.hiker.pack_on) {
    Hiker& h = w.hiker;
    h.pack_on = false;
    w.pack = PackProp{Vec2{h.feet.x - (float)h.facing * 15.f, h.feet.y + 1.f}, (int8_t)h.facing, true, false};
  }
}

// The rig has no pack pose: he holds it in CARRY with the food sack as a
// stand-in the pack is drawn over (CARRY keeps the arms still while walking).
void doPackPickUp(World& w, float) {
  w.pack.held = true;
  hikerCarry(w.hiker, CARRY_BAG);
}
void doPackSetDown(World& w, float) {
  w.pack.held = false;
  w.pack.pos.y = w.hiker.feet.y + 3.f;   // stands on the ground in front of his feet, not at his hands
  hikerCarry(w.hiker, CARRY_NONE);
}

// Tent: spread the fabric, back pole, front pole, pegs. Each tweens progress
// from where it is to its stage end.
void tentTween(World& w, float u, float to, float (*ease)(float)) {
  Tent& t = w.tent;
  if (u <= 0.f) w.spine.u0 = t.progress;
  t.state = TENT_RAISING;
  t.progress = lerp(w.spine.u0, to, ease(u));
}
void actSpread(World& w, float u) { tentTween(w, u, 0.15f, easeOutCubic); }
void actRaiseBack(World& w, float u) { tentTween(w, u, 0.5f, easeInOut); }
void actRaiseFront(World& w, float u) { tentTween(w, u, 0.85f, easeInOut); }
void actRaiseHalf(World& w, float u) { tentTween(w, u, 0.72f, easeInOut); }

// Last pegs pulled taut: the poles stretch a touch past and settle.
void actPeg(World& w, float u) {
  Tent& t = w.tent;
  if (u <= 0.f) w.spine.u0 = t.progress;
  t.progress = lerp(w.spine.u0, 1.f, easeInOut(window(u, 0.f, 0.5f)));
  t.bump = 0.07f * sinT(0.5f * window(u, 0.4f, 1.f));
  t.state = u >= 1.f ? TENT_UP : TENT_RAISING;
}

// Setup a: a peg bends and the half-raised tent slumps sideways.
void actPegBend(World& w, float u) {
  Tent& t = w.tent;
  t.state = TENT_COLLAPSED;
  t.sag = 0.55f * easeOutBack(window(u, 0.f, 0.6f));
  t.wobble = -6.f * sinT(0.5f * window(u, 0.f, 0.7f)) * (1.f - window(u, 0.7f, 1.f));
}

// Setup a: two blows with the rock. The tent jumps on each hit.
void actHammer(World& w, float u) {
  const float k = u < 0.5f ? u * 2.f : (u - 0.5f) * 2.f;   // per blow
  // Wind up (arm rises a little), strike (drops fast), rest.
  w.fx.reach = u >= 1.f ? 0.f : 0.55f * sinT(0.5f * window(k, 0.f, 0.7f));
  w.fx.lean = u >= 1.f ? 0.f : 0.08f * window(k, 0.5f, 0.7f) * (1.f - window(k, 0.7f, 1.f));
  w.tent.wobble = u >= 1.f ? 0.f : 2.5f * window(k, 0.62f, 0.7f) * (1.f - window(k, 0.7f, 1.f));
}

// Setup a: the peg holds; the tent springs up with an overshoot.
void actPopUp(World& w, float u) {
  Tent& t = w.tent;
  if (u <= 0.f) w.spine.u0 = t.progress;
  const float e = easeOutBack(u);
  t.progress = minf(1.f, lerp(w.spine.u0, 1.f, minf(1.f, u * 1.6f)));
  t.sag = 0.55f * (1.f - minf(1.f, e));
  t.bump = 0.08f * maxf(0.f, e - 1.f) / 0.1f;
  t.state = u >= 1.f ? TENT_UP : TENT_RAISING;
  if (u >= 1.f) t.bump = 0.f;
}

// Setup b: up, then it folds in on itself.
void actCollapse(World& w, float u) {
  Tent& t = w.tent;
  t.state = TENT_COLLAPSED;
  const float fall = easeInCubic(window(u, 0.f, 0.6f));
  const float bounce = 0.12f * sinT(0.5f * window(u, 0.6f, 1.f));   // flops, lifts, settles
  t.sag = fall - bounce;
  t.wobble = 5.f * sinT(0.5f * window(u, 0.f, 0.6f));
  if (u >= 1.f) t.wobble = 0.f;
}

// Setup b: second try, it springs up and overshoots.
void actReraise(World& w, float u) {
  Tent& t = w.tent;
  const float e = easeOutBack(window(u, 0.1f, 1.f));
  t.sag = maxf(0.f, 1.f - e);
  t.bump = e > 1.f ? (e - 1.f) * 0.8f : 0.f;
  t.state = u >= 1.f ? TENT_UP : TENT_RAISING;
  if (u >= 1.f) {
    t.sag = 0.f;
    t.bump = 0.f;
  }
}

// Setup c: steps back to admire it, hands on hips.
void actBackStep(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) w.spine.u0 = h.feet.x;
  const float k = easeInOut(u);
  h.feet.x = w.spine.u0 - (float)h.facing * 16.f * k;
  w.fx.lift = 0.012f * sinT(2.f * u) * sinT(2.f * u);   // two small steps
}

// Setup c: heel catches the guy line. Arms fling, he tips back, hops,
// overcorrects forward and recovers; the tent shakes.
void actStumble(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    w.spine.u0 = h.feet.x;
    hikerSetPose(h, POSE_YELP, 0.3f);
  }
  if (u >= 0.32f && h.target == POSE_YELP) hikerSetPose(h, POSE_STAND, 0.45f);
  const float back = sinT(0.5f * window(u, 0.f, 0.3f));
  const float fwd = sinT(0.5f * window(u, 0.28f, 0.55f));
  w.fx.lean = -0.4f * back + 0.22f * fwd;
  w.fx.lift = 0.f;
  h.feet.x = w.spine.u0 - (float)h.facing * 9.f * easeOutCubic(window(u, 0.05f, 0.35f));
  const float wob = window(u, 0.08f, 1.f);
  w.tent.wobble = 6.f * sinT(2.5f * wob) * (1.f - wob) * (1.f - wob);
  if (u >= 1.f) {
    w.fx.lean = 0.f;
    w.tent.wobble = 0.f;
  }
}

// Firewood: set the armload down by the pit.
void doWoodDown(World& w, float) {
  Item& pile = w.items[ITEM_WOOD];
  pile.size = pile.down ? 1.65f : 1.f;   // a second load makes it a heap
  pile.down = true;
  hikerCarry(w.hiker, CARRY_NONE);
  w.fx.armload = 0.f;
}
void doArmloadOn(World& w, float) {
  hikerCarry(w.hiker, CARRY_WOOD);
  w.fx.armload = 1.f;
}
void doStickHeld(World& w, float) {
  Item& s = w.items[ITEM_STICK];
  s.held = true;
  s.down = false;
}
// The dragged stick drops where it lay, flat on the ground.
void doStickDown(World& w, float) {
  Item& s = w.items[ITEM_STICK];
  const Hiker& h = w.hiker;
  s.held = false;
  s.down = true;
  s.pos = Vec2{s.pos.x - (float)h.facing * 31.f, h.feet.y + 2.f};
}

// Fire: laid and ready, then blown on. A puff flares the kindling, it sinks
// back between puffs, and each puff leaves it a little higher.
void doKindle(World& w, float) {
  w.fire.state = FIRE_KINDLING;
  w.fire.kindle = 0.f;
}
void blowPuffs(World& w, float u, float peak, float hold) {
  FirePit& f = w.fire;
  f.state = FIRE_KINDLING;
  const float k = u * 3.f;
  const float i = floorf(k);
  const float ph = k - i;
  const float base = hold * (i / 3.f);
  const float flare = sinT(0.5f * window(ph, 0.1f, 0.6f));
  const float flicker = 0.06f * sumSines(f.clock * 1.7f, 2);
  f.kindle = clamp01(base + (peak * (0.55f + 0.15f * i) - base) * flare + flicker * flare);
  if (u >= 1.f) f.kindle = hold;
}
void actBlowCatch(World& w, float u) { blowPuffs(w, u, 0.6f, 0.4f); }
void actBlowFail(World& w, float u) {
  blowPuffs(w, u, 0.45f, 0.15f);
  if (u >= 1.f) w.fire.kindle = 0.f;   // dies back
}
void doCatch(World& w, float) { w.fire.state = FIRE_LIT; }

// A sigh after a failed try: shoulders and head drop and come back.
void actSigh(World& w, float u) {
  const float s = sinT(0.5f * u);
  w.fx.lean = 0.1f * s;
  w.fx.head = 0.3f * s;
}

// Reaches into the fire to push a log in.
void actReach(World& w, float u) {
  w.fx.reach_to = 1.3f;
  w.fx.reach = easeInOut(window(u, 0.f, 0.8f));
}

// Kneels forward at the bank and holds his hand in the water, swishing it.
void actCool(World& w, float u) {
  const float in = easeInOut(window(u, 0.f, 0.15f)) * (1.f - easeInOut(window(u, 0.85f, 1.f)));
  w.fx.reach_to = 0.18f + 0.08f * sinT(2.5f * u);
  w.fx.reach = in;
  w.fx.lean = 0.5f * in;
  if (u >= 1.f) {
    w.fx.reach = w.fx.lean = 0.f;
    w.fx.reach_to = 1.3f;
  }
}

// Burned: the arm snaps back, he yelps and jumps back from the fire.
void actYelp(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    w.spine.u0 = h.feet.x;
    hikerSetPose(h, POSE_YELP, 0.35f);
  }
  w.fx.reach = 1.f - easeOutCubic(window(u, 0.f, 0.25f));
  h.feet.x = w.spine.u0 - (float)h.facing * 14.f * easeOutCubic(window(u, 0.12f, 0.6f));
  if (u >= 1.f) w.fx.reach = 0.f;
}

// Shakes the burned hand a few times.
void actShakeHand(World& w, float u) {
  w.fx.reach = u >= 1.f ? 0.f : 0.35f * (0.5f + 0.5f * sinT(3.f * u)) * (1.f - u);
}

// Fire c: the smoke turns and follows him; he leans away, it lets go.
void actSmokeFollows(World& w, float u) {
  Ambient& a = w.amb;
  a.smoke_to_x = w.hiker.feet.x;
  a.smoke_pull_to = u < 0.7f ? 1.f : 0.f;
  const float lean = easeInOut(window(u, 0.2f, 0.35f)) * (1.f - easeInOut(window(u, 0.8f, 1.f)));
  w.fx.lean = -0.18f * lean;
  w.fx.head = -0.3f * lean;
  if (u >= 1.f) {
    w.fx.lean = 0.f;
    w.fx.head = 0.f;
  }
}

// Placeholder for the food bag beat (replaced in the second half of stage 4).
void doBagHung(World& w, float) {
  w.bag.state = BAG_HUNG;
  w.bag.line = 16.f;
  w.bag.swing = 0.f;
}

// ---- scripts ----------------------------------------------------------------------

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

// Arrival a: straight in from the left, looks around, nods.
const Step kArrivalA[] = {
    place(AT_EDGE_L, RIGHT),
    walk(AT_SPOT, kStroll),
    still(0.7f),
    pose(POSE_LOOK_AROUND, 0.9f, 2.4f),
    face(LEFT),
    still(2.0f),
    pose(POSE_STAND, 0.6f, 0.4f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 0.3f),
    act(actNod, 1.4f),
    still(1.4f),
    pose(POSE_STAND, 0.8f, 0.3f),
    face(TO_FIRE),
};

// Arrival b: walks past the spot, stops, looks back, comes back to it.
const Step kArrivalB[] = {
    place(AT_EDGE_L, RIGHT),
    walk(AT_PAST, 36.f),
    still(1.0f),
    face(LEFT),
    pose(POSE_LOOK_AROUND, 0.8f, 1.8f),
    pose(POSE_STAND, 0.5f, 0.3f),
    walk(AT_SPOT, 28.f),
    still(0.6f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 0.3f),
    act(actNod, 1.4f),
    still(1.2f),
    pose(POSE_STAND, 0.8f, 0.3f),
    face(TO_FIRE),
};

// Arrival c: from the right, wades the stream, rests on a rock, picks the spot.
const Step kArrivalC[] = {
    place(AT_EDGE_R, LEFT),
    walk(AT_FORD0, kStroll),
    walk(AT_FORD1, kWade),
    walk(AT_ROCK_SIT, 32.f),
    still(0.4f),
    pose(POSE_SIT, 0.9f, 3.0f),
    pose(POSE_STAND, 0.9f, 0.4f),
    pose(POSE_LOOK_AROUND, 0.7f, 1.4f),
    pose(POSE_STAND, 0.4f, 0.f),
    walk(AT_SPOT, 38.f),
    still(0.6f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 0.3f),
    act(actNod, 1.4f),
    pose(POSE_STAND, 0.8f, 0.3f),
    face(TO_FIRE),
};

// Setup, shared start: pack off, rummage, carry the pack to the tent, spread
// the fabric. Shared end of each raise: back pole, front pole.
#define SETUP_START                                                     \
  pose(POSE_STAND, 0.3f, 0.3f), act(actShrug, 1.1f),                    \
      pose(POSE_CROUCH, 0.7f, 1.4f), doit(doPackPickUp),                \
      pose(POSE_CARRY, 0.6f, 0.2f), walk(AT_PACK, 30.f, 16.f, -3.f),    \
      face(LEFT), pose(POSE_CROUCH, 0.6f, 0.4f), doit(doPackSetDown),   \
      pose(POSE_STAND, 0.6f, 0.2f), walk(AT_TENT_WORK, 30.f), face(LEFT), \
      pose(POSE_CROUCH, 0.6f, 0.f), act(actSpread, 1.6f), pose(POSE_STAND, 0.5f, 0.f)

// Setup a: bent peg; a rock as a hammer.
const Step kSetupA[] = {
    SETUP_START,
    pose(POSE_REACH_UP, 0.7f, 0.f),
    act(actRaiseBack, 1.8f),
    act(actRaiseHalf, 1.4f),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    act(actPegBend, 1.0f),
    still(1.0f),
    pose(POSE_STAND, 0.6f, 0.3f),
    walk(AT_PEG_ROCK, 30.f),
    pose(POSE_CROUCH, 0.5f, 0.3f),
    carry(CARRY_ROCK),
    pose(POSE_STAND, 0.5f, 0.1f),
    walk(AT_TENT_WORK, 30.f),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.1f),
    act(actHammer, 1.6f),
    carry(CARRY_NONE),
    pose(POSE_STAND, 0.6f, 0.f),
    act(actPopUp, 1.4f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 1.5f),
    pose(POSE_STAND, 0.7f, 0.3f),
};

// Setup b: up, collapses, head scratch, second try.
const Step kSetupB[] = {
    SETUP_START,
    pose(POSE_REACH_UP, 0.7f, 0.f),
    act(actRaiseBack, 1.8f),
    act(actRaiseFront, 1.8f),
    pose(POSE_CROUCH, 0.6f, 0.f),
    act(actPeg, 1.2f),
    pose(POSE_STAND, 0.6f, 0.6f),
    act(actCollapse, 0.8f),
    still(0.9f),
    pose(POSE_SCRATCH_HEAD, 0.7f, 2.2f),
    pose(POSE_STAND, 0.5f, 0.1f),
    pose(POSE_CROUCH, 0.6f, 0.7f),
    pose(POSE_REACH_UP, 0.7f, 0.f),
    act(actReraise, 1.8f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 1.4f),
    pose(POSE_STAND, 0.7f, 0.2f),
};

// Setup c: up first try; steps back to admire it and trips on a guy line.
const Step kSetupC[] = {
    SETUP_START,
    pose(POSE_REACH_UP, 0.7f, 0.f),
    act(actRaiseBack, 1.8f),
    act(actRaiseFront, 1.8f),
    pose(POSE_CROUCH, 0.6f, 0.f),
    act(actPeg, 1.2f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 0.2f),
    act(actBackStep, 1.2f),
    still(0.5f),
    act(actStumble, 2.4f),
    still(0.6f),
    pose(POSE_CROUCH, 0.6f, 0.9f),
    pose(POSE_STAND, 0.6f, 0.3f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.0f),
    pose(POSE_STAND, 0.6f, 0.2f),
};

// Firewood, shared: looks to the trees, walks off left behind the pines,
// stays away, comes back with an armload and sets it down.
#define WOOD_TRIP                                                              \
  face(LEFT), pose(POSE_LOOK_AROUND, 0.7f, 1.2f), pose(POSE_STAND, 0.5f, 0.f), \
      walk(AT_EDGE_L, 40.f), waitAway(), carry(CARRY_WOOD), pose(POSE_CARRY, 0.f, 0.f)
#define WOOD_DOWN \
  pose(POSE_CROUCH, 0.6f, 0.3f), doit(doWoodDown), pose(POSE_STAND, 0.6f, 0.4f)

const Step kFirewoodA[] = {
    WOOD_TRIP,
    walk(AT_WOOD_WORK, 32.f),
    WOOD_DOWN,
};

// Firewood b: not enough; a second trip, and the armload is absurd.
const Step kFirewoodB[] = {
    WOOD_TRIP,
    walk(AT_WOOD_WORK, 32.f),
    WOOD_DOWN,
    pose(POSE_SCRATCH_HEAD, 0.6f, 1.2f),
    pose(POSE_STAND, 0.5f, 0.f),
    walk(AT_EDGE_L, 42.f),
    still(4.0f),
    doit(doArmloadOn),
    pose(POSE_CARRY, 0.f, 0.f),
    walk(AT_WOOD_WORK, 24.f),
    pose(POSE_CROUCH, 0.7f, 0.4f),
    doit(doWoodDown),
    pose(POSE_STAND, 0.7f, 0.3f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.0f),
    pose(POSE_STAND, 0.6f, 0.2f),
};

// Firewood c: comes back with the wood and a long stick dragging behind.
const Step kFirewoodC[] = {
    WOOD_TRIP,
    doit(doStickHeld),
    walk(AT_WOOD_WORK, 28.f),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    doit(doWoodDown),
    doit(doStickDown),
    pose(POSE_STAND, 0.6f, 0.4f),
};

// Fire, shared start: round the back of the pit to its right side, crouch and
// lay the kindling, then blow.
#define FIRE_START                                                                    \
  via(AT_FIRE, kStroll, 0.f, -16.f), walk(AT_BLOW, 30.f), face(LEFT),                 \
      pose(POSE_CROUCH, 0.6f, 1.8f), doit(doKindle), pose(POSE_BLOW, 0.8f, 0.2f)
// Shared end: it catches, he pokes a log in, burns his hand, yelps, runs to the
// stream, cools it, comes back and sits.
#define FIRE_BURN                                                                       \
  doit(doCatch), pose(POSE_CROUCH, 0.6f, 2.2f), act(actReach, 1.2f), still(0.25f),       \
      act(actYelp, 0.8f), pose(POSE_STAND, 0.35f, 0.f), walk(AT_COOL, kRun), face(RIGHT), \
      pose(POSE_CROUCH, 0.45f, 0.f), act(actCool, 3.2f), pose(POSE_STAND, 0.7f, 0.f), act(actShakeHand, 1.2f), \
      walk(AT_SIT, 28.f), face(LEFT), pose(POSE_SIT, 0.9f, 0.6f)

const Step kFireA[] = {
    FIRE_START,
    act(actBlowCatch, 4.5f),
    FIRE_BURN,
    still(1.0f),
};

// Fire b: two tries die back before the third catches.
const Step kFireB[] = {
    FIRE_START,
    act(actBlowFail, 3.6f),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    act(actSigh, 1.2f),
    pose(POSE_BLOW, 0.7f, 0.2f),
    act(actBlowFail, 3.6f),
    pose(POSE_CROUCH, 0.6f, 1.0f),
    pose(POSE_BLOW, 0.7f, 0.2f),
    act(actBlowCatch, 4.5f),
    FIRE_BURN,
    still(0.6f),
};

// Fire c: once he sits, the smoke finds him.
const Step kFireC[] = {
    FIRE_START,
    act(actBlowCatch, 4.5f),
    FIRE_BURN,
    still(0.8f),
    act(actSmokeFollows, 7.0f),
    still(0.6f),
};

// Placeholders, replaced in the second half of stage 4: hold a pose that
// follows from the one before so the loop plays end to end.
const Step kDinnerHold[] = {pose(POSE_SIT, 0.8f, 0.f)};
const Step kFoodbagHold[] = {pose(POSE_STAND, 1.0f, 1.0f), doit(doBagHung), pose(POSE_LOOK_AROUND, 0.8f, 0.f)};
const Step kBearHold[] = {pose(POSE_STAND, 0.6f, 1.0f), pose(POSE_HANDS_ON_HIPS, 0.7f, 0.f)};
const Step kCloseHold[] = {pose(POSE_STAND, 0.6f, 0.4f), pose(POSE_SIT, 1.0f, 0.f)};

#undef SETUP_START
#undef WOOD_TRIP
#undef WOOD_DOWN
#undef FIRE_START
#undef FIRE_BURN

struct Script {
  const Step* steps;
  uint8_t n;
};

#define SCRIPT(a) Script{a, N(a)}
#define NO_SCRIPT Script{nullptr, 0}

// Variants per beat, in LoopPlan order. A null entry is a variant not built.
const Script kScripts[SB_COUNT][3] = {
    {SCRIPT(kArrivalA), SCRIPT(kArrivalB), SCRIPT(kArrivalC)},
    {SCRIPT(kSetupA), SCRIPT(kSetupB), SCRIPT(kSetupC)},
    {SCRIPT(kFirewoodA), SCRIPT(kFirewoodB), SCRIPT(kFirewoodC)},
    {SCRIPT(kFireA), SCRIPT(kFireB), SCRIPT(kFireC)},
    {SCRIPT(kDinnerHold), NO_SCRIPT, NO_SCRIPT},
    {SCRIPT(kFoodbagHold), NO_SCRIPT, NO_SCRIPT},
    {SCRIPT(kBearHold), NO_SCRIPT, NO_SCRIPT},
    {SCRIPT(kCloseHold), NO_SCRIPT, NO_SCRIPT},
};

// Beat seconds per variant: the script plus room for slower walks on some
// seeds (fire pit and rock positions vary). Whole seconds keep the loop whole ticks.
#undef SCRIPT
#undef NO_SCRIPT
#undef N

const uint8_t kDurations[SB_COUNT][3] = {
    {25, 29, 33},   // arrival
    {39, 39, 35},   // setup
    {34, 62, 35},   // firewood (b is two trips)
    {31, 43, 37},   // fire
    {8, 0, 0},      // dinner (placeholder)
    {8, 0, 0},      // foodbag (placeholder)
    {8, 0, 0},      // bear (placeholder)
    {8, 0, 0},      // close (placeholder)
};

const char* const kBeatNames[SB_COUNT] = {"arrival", "setup", "firewood", "fire",
                                          "dinner",  "foodbag", "bear",   "close"};
const char* const kFillerNames[FILL_COUNT] = {"cairn", "read", "mug", "view", "nap"};

// ---- the interpreter ------------------------------------------------------------------

Vec2 anchorPos(const World& w, uint8_t at) {
  const CampLayout& c = w.spine.camp;
  switch (at) {
    case AT_SPOT: return c.spot;
    case AT_PAST: return c.past;
    case AT_TENT_WORK: return c.tent_work;
    case AT_PACK: return c.pack;
    case AT_PEG_ROCK: return c.peg_rock;
    case AT_WOOD_WORK: return c.wood_work;
    case AT_FIRE: return w.fire.pos;
    case AT_BLOW: return c.blow;
    case AT_SIT: return c.sit;
    case AT_COOL: return c.cool;
    case AT_ROCK_SIT: return c.rock_sit;
    case AT_FORD0: return c.ford0;
    case AT_FORD1: return c.ford1;
    case AT_EDGE_L: return c.edge_l;
    case AT_EDGE_R: return c.edge_r;
    default: return w.hiker.feet;
  }
}

void startWalk(World& w, const Step& s) {
  Hiker& h = w.hiker;
  const Vec2 a = anchorPos(w, s.at);
  const Vec2 to{a.x + s.x, a.y + s.y};
  HikerFx& fx = w.fx;
  fx.path_on = true;
  fx.path_x0 = h.feet.x;
  fx.path_y0 = h.feet.y;
  fx.path_x1 = to.x;
  fx.path_y1 = to.y;
  if (to.x == h.feet.x) h.feet.y = to.y;   // nothing to walk; arrive in place
  hikerWalkTo(h, to.x, s.s);
}

void enterStep(World& w, const Step& s) {
  Hiker& h = w.hiker;
  switch (s.op) {
    case OP_PLACE: {
      const Vec2 p = anchorPos(w, s.at);
      hikerInit(h, p, s.a == RIGHT ? 1 : -1);
      w.fx.on = true;
      w.fx.path_on = false;
      break;
    }
    case OP_WALK:
    case OP_VIA: startWalk(w, s); break;
    case OP_POSE: hikerSetPose(h, (PoseId)s.a, s.x); break;
    case OP_FACE:
      if (s.a == TO_FIRE) h.facing = w.fire.pos.x < h.feet.x ? -1 : 1;
      else h.facing = s.a == RIGHT ? 1 : -1;
      break;
    case OP_CARRY: hikerCarry(h, (CarryId)s.a); break;
    case OP_DO: s.fn(w, 1.f); break;
    default: break;
  }
}

// True when step `s`, entered `ticks` ticks ago, has finished.
bool stepDone(World& w, const Step& s, uint16_t ticks) {
  const float t = (float)ticks * SS_TICK_DT;
  switch (s.op) {
    case OP_WALK: return hikerArrived(w.hiker);
    case OP_VIA: {
      const Vec2 a = anchorPos(w, s.at);
      return !w.hiker.walking || fabsf(w.hiker.feet.x - (a.x + s.x)) <= 4.f;
    }
    case OP_POSE:
    case OP_WAIT: return t + 1e-4f >= s.s;
    case OP_WAIT_AWAY: return t + 1e-4f >= (float)w.plan.away_s;
    case OP_ACT: {
      const float u = s.s > 0.f ? clamp01(t / s.s) : 1.f;
      s.fn(w, u);
      return u >= 1.f;
    }
    default: return true;   // instant
  }
}

void runScript(World& w) {
  ScriptRun& r = w.spine.run;
  for (int guard = 0; guard < 16 && r.steps && r.i < r.n; ++guard) {
    const Step& s = r.steps[r.i];
    if (!r.entered) {
      r.entered = true;
      r.ticks = 0;
      enterStep(w, s);
    } else {
      ++r.ticks;
    }
    if (!stepDone(w, s, r.ticks)) return;
    ++r.i;
    r.entered = false;
  }
}

// ---- beats ----------------------------------------------------------------------------

template <int B>
void beatBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  const Script& sc = kScripts[B][spineVariant(w.plan, B)];
  ScriptRun& r = w.spine.run;
  r.steps = sc.steps;
  r.n = sc.n;
  r.i = 0;
  r.entered = false;
  r.ticks = 0;
}

void beatUpdate(SceneCtx& ctx, float, float) { runScript(*ctx.w); }

void beatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  if (w.spine.run.steps && w.spine.run.i < w.spine.run.n && w.spine.overruns < 255) ++w.spine.overruns;
  w.spine.run.steps = nullptr;
}

typedef void (*BeginFn)(SceneCtx&);
const BeginFn kBegin[SB_COUNT] = {beatBegin<0>, beatBegin<1>, beatBegin<2>, beatBegin<3>,
                                  beatBegin<4>, beatBegin<5>, beatBegin<6>, beatBegin<7>};

// Picks the far-bank rock nearest the crossing for arrival c; false if none fits.
bool pickRock(const World& w, Vec2& sit) {
  const Landscape& ls = w.land;
  float best = 1.0e9f;
  bool found = false;
  for (int i = 0; i < ls.n_rocks; ++i) {
    const LsRock& r = ls.rocks[i];
    float x0 = r.p[0].x, x1 = r.p[0].x, base = r.p[0].y;
    for (const Vec2& p : r.p) {
      x0 = minf(x0, p.x);
      x1 = maxf(x1, p.x);
      base = maxf(base, p.y);
    }
    const float cx = 0.5f * (x0 + x1);
    float cy, half;
    if (cx < 440.f || cx > 640.f) continue;
    if (!landscapeStreamSpan(ls, cx, cy, half) || base > cy - half + 2.f) continue;   // far bank only
    if (base < (float)LS_GROUND_Y + 22.f) continue;
    const float d = fabsf(cx - w.spine.camp.ford1.x);
    if (d < best) {
      best = d;
      sit = Vec2{x0 - 9.f, base};   // sits on the camp side of it
      found = true;
    }
  }
  return found;
}

}  // namespace

// ---- public ---------------------------------------------------------------------------

int spineVariantCount(int beat) {
  if (beat < 0 || beat >= SB_COUNT) return 0;
  int n = 0;
  while (n < 3 && kScripts[beat][n].steps) ++n;
  return n;
}

uint8_t spineVariant(const LoopPlan& p, int beat) {
  const uint8_t v[SB_COUNT] = {p.arrival, p.setup, p.firewood, p.fire, p.dinner, p.foodbag, p.bear, p.close};
  return beat >= 0 && beat < SB_COUNT ? v[beat] : 0;
}

const char* spineBeatName(int beat) { return beat >= 0 && beat < SB_COUNT ? kBeatNames[beat] : "?"; }
const char* spineFillerName(int f) { return f >= 0 && f < FILL_COUNT ? kFillerNames[f] : "?"; }

void spinePlan(LoopPlan& p, Rng& rng, const Inputs& in) {
  (void)in;   // time of day and weather pick variants in later stages
  uint8_t v[SB_COUNT];
  for (int b = 0; b < SB_COUNT; ++b) v[b] = (uint8_t)rng.rangeInt(0, spineVariantCount(b) - 1);
  p.arrival = v[SB_ARRIVAL];
  p.setup = v[SB_SETUP];
  p.firewood = v[SB_FIREWOOD];
  p.fire = v[SB_FIRE];
  p.dinner = v[SB_DINNER];
  p.foodbag = v[SB_FOODBAG];
  p.bear = v[SB_BEAR];
  p.close = v[SB_CLOSE];
  p.n_fillers = 0;   // fillers are not built yet
  for (uint8_t& f : p.filler) f = 0;
  for (uint8_t& s : p.pause_s) s = (uint8_t)rng.rangeInt(8, 20);
  p.away_s = (uint8_t)rng.rangeInt(6, 10);
}

void spineLayout(World& w) {
  CampLayout& c = w.spine.camp;
  const Vec2 F = w.fire.pos;
  c.tent = Vec2{F.x - 44.f, 352.f};
  c.tent_work = Vec2{c.tent.x + 36.f, c.tent.y + 8.f};
  c.pack = Vec2{c.tent.x - 30.f, c.tent.y + 7.f};
  c.peg_rock = Vec2{c.tent.x + 100.f, c.tent.y + 22.f};
  c.spot = Vec2{F.x + 48.f, F.y - 10.f};
  c.past = Vec2{c.spot.x + 92.f, c.spot.y - 4.f};
  c.wood = Vec2{F.x - 30.f, F.y - 8.f};
  c.wood_work = Vec2{c.wood.x - 18.f, c.wood.y - 2.f};
  c.blow = Vec2{F.x + 31.f, F.y - 2.f};
  c.sit = Vec2{F.x + 40.f, F.y - 6.f};
  c.edge_l = Vec2{-46.f, 372.f};

  // The bank below the camp, where he cools his hand: boots at the waterline, so the crouch puts his hands in it.
  float cy, half;
  const float cool_x = F.x + 95.f;
  c.cool = landscapeStreamSpan(w.land, cool_x, cy, half) ? Vec2{cool_x, cy - half + 8.f} : Vec2{cool_x, F.y + 16.f};

  // The crossing on the right: near bank kept above the reserved corner.
  c.ford0 = landscapeStreamSpan(w.land, 732.f, cy, half) ? Vec2{732.f, minf(410.f, cy + half + 5.f)}
                                                         : Vec2{732.f, 404.f};
  c.ford1 = landscapeStreamSpan(w.land, 652.f, cy, half) ? Vec2{652.f, cy - half - 8.f} : Vec2{652.f, 356.f};
  c.edge_r = Vec2{848.f, c.ford0.y};
  if (!pickRock(w, c.rock_sit)) c.rock_sit = Vec2{590.f, landscapeStreamSpan(w.land, 590.f, cy, half) ? cy - half - 8.f : 360.f};

  const float cairn_x = F.x + 150.f;
  c.cairn = landscapeStreamSpan(w.land, cairn_x, cy, half) ? Vec2{cairn_x, cy - half - 6.f} : Vec2{cairn_x, F.y};

  w.spine.run = ScriptRun{nullptr, 0, 0, false, 0};
  w.spine.overruns = 0;
  w.spine.u0 = 0.f;
}

void spineBuildTimeline(Timeline& tl, const LoopPlan& p) {
  tl.add(kBuildBeat);
  uint32_t ticks = (uint32_t)(SS_BUILD_SECONDS * (float)SS_TICK_HZ + 0.5f);
  for (int b = 0; b < SB_COUNT; ++b) {
    const uint8_t v = spineVariant(p, b);
    const float d = (float)kDurations[b][v];
    tl.add(Beat{kBeatNames[b], d, kBegin[b], beatUpdate, beatEnd});
    ticks += (uint32_t)kDurations[b][v] * SS_TICK_HZ;
    if (b + 1 < SB_COUNT) {
      tl.add(Beat{"pause", (float)p.pause_s[b], nullptr, nullptr, nullptr});
      ticks += (uint32_t)p.pause_s[b] * SS_TICK_HZ;
    }
  }
  // Whatever is left is ambient, so the loop is exactly SS_LOOP_SECONDS.
  const uint32_t total = (uint32_t)SS_LOOP_SECONDS * SS_TICK_HZ;
  const uint32_t rest = total > ticks ? total - ticks : 1u;
  tl.add(Beat{"ambient", (float)rest / (float)SS_TICK_HZ, nullptr, nullptr, nullptr});
}

int spineFormatPlan(const LoopPlan& p, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int k = snprintf(buf, n, "arrival=%d setup=%d firewood=%d fire=%d dinner=%d foodbag=%d bear=%d close=%d fillers=",
                   p.arrival, p.setup, p.firewood, p.fire, p.dinner, p.foodbag, p.bear, p.close);
  if (k < 0 || (size_t)k >= n) return k;
  if (p.n_fillers == 0) return k + snprintf(buf + k, n - (size_t)k, "none");
  for (int i = 0; i < p.n_fillers && i < 4; ++i) {
    const int m = snprintf(buf + k, n - (size_t)k, "%s%s", i ? "," : "", spineFillerName(p.filler[i]));
    if (m < 0) return k;
    k += m;
    if ((size_t)k >= n) return k;
  }
  return k;
}

}  // namespace ss
