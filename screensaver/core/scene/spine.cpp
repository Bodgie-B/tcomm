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
// and a LoopPlan field (spineVariant maps beat ids to fields). Adding a filler:
// a FillerId, a script in kFillerScripts, a name, a weight in spinePlan and its
// seconds in spineFillerSeconds. A filler starts wherever the last beat left him
// (sitting by the fire, or standing after setup) and ends sitting at camp.sit
// facing the fire, so any filler fits any slot.
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
  OP_GO,         // walk to the anchor at s px/s by a route: around the back of the fire pit,
                 // and in two legs when the move is steeper than it is wide (walks only move x)
  OP_REPEAT,     // while pred(w): go back a steps
};

enum Anchor : uint8_t {
  AT_HERE = 0, AT_SPOT, AT_PAST, AT_TENT_WORK, AT_PACK, AT_PEG_ROCK, AT_WOOD_WORK, AT_FIRE,
  AT_BLOW, AT_SIT, AT_COOL, AT_ROCK_SIT, AT_FORD0, AT_FORD1, AT_EDGE_L, AT_EDGE_R,
  AT_PACK_WORK, AT_BAG_WORK, AT_DOOR, AT_VIEW, AT_READ, AT_LIE, AT_LISTEN, AT_SKIP,
  AT_MUG,          // the mug where it lies (its right side), or the pack if it is packed
  AT_STONE,        // cairn: the next flat stone, alternating sides
  AT_CAIRN_WORK,   // cairn: beside it, on the side he comes from
  AT_CAIRN
};

typedef void (*ActFn)(World& w, float u);
typedef bool (*PredFn)(const World& w);

struct Step {
  uint8_t op;
  uint8_t a;       // pose, carry, facing; OP_REPEAT: steps back
  uint8_t at;      // anchor
  float x, y;      // offset from the anchor (OP_POSE: x is the blend seconds)
  float s;         // seconds, or px/s for walks
  ActFn fn;
  PredFn pred;
};

namespace {

// ---- step constructors (keep the tables readable) ---------------------------

constexpr Step place(uint8_t at, int face) {
  return Step{OP_PLACE, (uint8_t)face, at, 0.f, 0.f, 0.f, nullptr, nullptr};
}
constexpr Step walk(uint8_t at, float v, float x = 0.f, float y = 0.f) {
  return Step{OP_WALK, 0, at, x, y, v, nullptr, nullptr};
}
constexpr Step via(uint8_t at, float v, float x = 0.f, float y = 0.f) {
  return Step{OP_VIA, 0, at, x, y, v, nullptr, nullptr};
}
constexpr Step go(uint8_t at, float v, float x = 0.f, float y = 0.f) {
  return Step{OP_GO, 0, at, x, y, v, nullptr, nullptr};
}
constexpr Step pose(PoseId p, float blend, float hold) {
  return Step{OP_POSE, (uint8_t)p, 0, blend, 0.f, blend + hold, nullptr, nullptr};
}
constexpr Step still(float s) { return Step{OP_WAIT, 0, 0, 0.f, 0.f, s, nullptr, nullptr}; }
constexpr Step waitAway() { return Step{OP_WAIT_AWAY, 0, 0, 0.f, 0.f, 0.f, nullptr, nullptr}; }
constexpr Step face(int dir) { return Step{OP_FACE, (uint8_t)dir, 0, 0.f, 0.f, 0.f, nullptr, nullptr}; }
constexpr Step faceTo(uint8_t at) { return Step{OP_FACE, 3, at, 0.f, 0.f, 0.f, nullptr, nullptr}; }
constexpr Step carry(CarryId c) { return Step{OP_CARRY, (uint8_t)c, 0, 0.f, 0.f, 0.f, nullptr, nullptr}; }
constexpr Step act(ActFn fn, float s) { return Step{OP_ACT, 0, 0, 0.f, 0.f, s, fn, nullptr}; }
constexpr Step doit(ActFn fn) { return Step{OP_DO, 0, 0, 0.f, 0.f, 0.f, fn, nullptr}; }
constexpr Step repeatWhile(PredFn p, int back) {
  return Step{OP_REPEAT, (uint8_t)back, 0, 0.f, 0.f, 0.f, nullptr, p};
}

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

// ---- evening beats and fillers: helpers ------------------------------------------------

// 0 at both ends of u: eased in over the first `a`, out over the last `b`.
float envelope(float u, float a, float b) {
  return easeInOut(window(u, 0.f, a)) * (1.f - easeInOut(window(u, 1.f - b, 1.f)));
}

// s in seconds: 0 -> 1 over [c, c + up], held for `hold`, back to 0 over `up`.
float plateau(float s, float c, float up, float hold) {
  return easeInOut(window(s, c, c + up)) * (1.f - easeInOut(window(s, c + up + hold, c + 2.f * up + hold)));
}

void setArm(World& w, int i, float k, float sh, float el) {
  w.fx.arm_w[i] = k;
  w.fx.arm_sh[i] = sh;
  w.fx.arm_el[i] = el;
}

// Every pose offset back to 0 (an action's last tick).
void fxRest(World& w) {
  HikerFx& fx = w.fx;
  fx.lean = fx.head = fx.lift = fx.reach = 0.f;
  fx.reach_to = 1.3f;
  fx.arm_w[0] = fx.arm_w[1] = 0.f;
}

// On the ground beside him, `dx` px ahead.
Vec2 besideHim(const Hiker& h, float dx) { return Vec2{h.feet.x + (float)h.facing * dx, h.feet.y + 2.f}; }

void setDown(World& w, ItemId id, Vec2 at) {
  Item& it = w.items[id];
  it.held = false;
  it.on_fire = false;
  it.down = true;
  it.pos = at;
  it.facing = (int8_t)w.hiker.facing;
}

// Stage 5 hook: while he eats, critters (chipmunk, mouse) edge closer and
// scatter whenever he moves (each sip or bite raises fx.arm_w). Empty step
// until stage 5 schedules them from here.
void hookDinnerCritters(World&, float) {}
// Stage 5 hook: the night theft (a raccoon takes the mug, the pot, the fork or
// a shoe left out). Empty step until stage 5.
void hookTheft(World&, float) {}

// ---- dinner ------------------------------------------------------------------------------

// The mug: picked up where it was left, or out of the pack; put down beside him.
void doMugUp(World& w, float) {
  w.items[ITEM_MUG].down = false;
  hikerCarry(w.hiker, CARRY_MUG);
}
void doMugDown(World& w, float) {
  hikerCarry(w.hiker, CARRY_NONE);
  setDown(w, ITEM_MUG, besideHim(w.hiker, 13.f));
}

// Sitting with the mug at his chest; a sip lifts it to his mouth and tips his head back.
void mugHold(World& w, float env, float sip, float head) {
  setArm(w, 0, env, lerp(0.2f, 0.9f, sip), lerp(2.3f, 2.45f, sip));
  w.fx.head = env * (head - 0.32f * sip);
}

// Dinner a: sips every 6 s, eyes on the fire.
constexpr float kMugDinnerS = 22.f;
void actMugDinner(World& w, float u) {
  const float s = u * kMugDinnerS;
  float sip = 0.f;
  for (int k = 0; k < 3; ++k) sip = maxf(sip, plateau(s, 3.f + 6.f * (float)k, 0.55f, 0.7f));
  mugHold(w, envelope(u, 0.05f, 0.05f), sip, 0.16f);
  if (u >= 1.f) fxRest(w);
}

// Dinner b: the pot. Carried in both hands (CARRY_MUG holds the arms in the
// carry pose; the pot is drawn over the mug), set on the fire, boils over.
Vec2 potOnFire(const World& w) { return Vec2{w.fire.pos.x, w.fire.pos.y - 5.f}; }

void doPotUp(World& w, float) {
  Item& p = w.items[ITEM_POT];
  p.held = true;
  p.down = false;
  p.on_fire = false;
  hikerCarry(w.hiker, CARRY_MUG);
}

void actPotOnFire(World& w, float u) {
  Item& p = w.items[ITEM_POT];
  if (u <= 0.f) {
    w.spine.v0 = p.pos;
    p.held = false;
    p.down = true;
    hikerCarry(w.hiker, CARRY_NONE);
  }
  const Vec2 top = potOnFire(w);
  const float k = easeInOut(window(u, 0.1f, 0.8f));
  p.pos = Vec2{lerp(w.spine.v0.x, top.x, k), lerp(w.spine.v0.y, top.y, k) - 5.f * sinT(0.5f * k)};
  p.on_fire = k > 0.5f;
  w.fx.reach_to = 1.2f;
  w.fx.reach = envelope(u, 0.2f, 0.3f);
  if (u >= 1.f) {
    p.pos = top;
    fxRest(w);
  }
}

// It boils over: steam bursts out with a hiss (implied), the lid rattles, and
// he jumps back: half a yelp, leaning back with his arms up.
constexpr float kBoilS = 2.4f;
void actBoilOver(World& w, float u) {
  const float s = u * kBoilS;
  Steam& st = w.steam;
  const Vec2 top = potOnFire(w);
  st.at = Vec2{top.x, top.y - 10.f};
  st.n = w.plan.steam_n;
  st.t = s;
  w.items[ITEM_POT].pos.y = top.y - 1.2f * (0.5f + 0.5f * sinT(s * 5.f)) * (1.f - window(s, 0.f, 1.2f));
  const float k = easeOutCubic(window(s, 0.15f, 0.35f)) * (1.f - easeInOut(window(s, 0.8f, 1.7f)));
  w.fx.lean = -0.22f * k;
  w.fx.head = -0.25f * k;
  setArm(w, 0, 0.5f * k, 2.5f, 0.5f);
  setArm(w, 1, 0.5f * k, 2.1f, 0.7f);
  if (u >= 1.f) {
    st.t = -1.f;
    w.items[ITEM_POT].pos = top;
    fxRest(w);
  }
}

// He hooks the pot off the fire with the stick and sets it down in front of him.
void actPotOff(World& w, float u) {
  Item& p = w.items[ITEM_POT];
  const Vec2 top = potOnFire(w), dst = w.spine.camp.pot_down;
  w.fx.reach_to = 1.35f;
  w.fx.reach = envelope(u, 0.2f, 0.25f);
  const float k = easeInOut(window(u, 0.3f, 0.78f));
  p.pos = Vec2{lerp(top.x, dst.x, k), lerp(top.y, dst.y, k) - 12.f * sinT(0.5f * k)};
  // Kept in front of the flames all the way: it lands in front of them too.
  if (u >= 1.f) {
    p.on_fire = false;
    p.pos = dst;
    fxRest(w);
  }
}

// Eats out of the pot: down to the pot, up to his mouth, every 2.2 s.
constexpr float kEatPotS = 12.f;
void actEatPot(World& w, float u) {
  const float s = u * kEatPotS;
  const float env = envelope(u, 0.04f, 0.06f);
  const float ph = s / 2.2f;
  const float f = ph - floorf(ph);
  const float dip = plateau(f, 0.f, 0.16f, 0.08f);
  const float lift = plateau(f, 0.42f, 0.16f, 0.14f);
  setArm(w, 0, env, 0.5f + 0.25f * dip + 0.4f * lift, 1.3f - 1.1f * dip + 1.15f * lift);
  w.fx.head = env * (0.08f + 0.25f * dip - 0.14f * lift);
  if (u >= 1.f) fxRest(w);
}

// Dinner c: the tiny fork.
void doForkUp(World& w, float) {
  Item& f = w.items[ITEM_FORK];
  f.held = true;
  f.down = false;
}
void doForkDown(World& w, float) { setDown(w, ITEM_FORK, besideHim(w.hiker, 12.f)); }

// Digs in the pack: leans in, the arm working.
void actRummage(World& w, float u) {
  const float env = envelope(u, 0.15f, 0.15f);
  w.fx.lean = env * (0.12f + 0.06f * sinT(u * 3.f));
  w.fx.reach_to = 0.9f;
  w.fx.reach = env * (0.5f + 0.3f * sinT(u * 4.f + 0.25f));
  if (u >= 1.f) fxRest(w);
}

// Holds it up at arm's length and has a long look at it.
void actHoldUp(World& w, float u) {
  const float env = envelope(u, 0.18f, 0.15f);
  w.fx.reach_to = 2.05f;
  w.fx.reach = env;
  w.fx.head = -0.32f * env;
  w.fx.lean = -0.04f * env;
  if (u >= 1.f) fxRest(w);
}

// Shrugs: up about 2 px with both forearms out, palms up; back down.
void actShrugFork(World& w, float u) {
  const float k = easeOutCubic(window(u, 0.f, 0.25f)) * (1.f - easeInOut(window(u, 0.6f, 1.f)));
  w.fx.lift = 0.024f * k;
  w.fx.head = 0.1f * k;
  setArm(w, 0, 0.75f * k, 0.3f, 1.55f);
  setArm(w, 1, 0.75f * k, 0.3f, 1.55f);
  if (u >= 1.f) fxRest(w);
}

// Eats with it anyway: small quick bites.
constexpr float kEatForkS = 10.f;
void actEatFork(World& w, float u) {
  const float s = u * kEatForkS;
  const float env = envelope(u, 0.05f, 0.06f);
  const float ph = s / 0.8f;
  const float lift = plateau(ph - floorf(ph), 0.05f, 0.2f, 0.12f);
  setArm(w, 0, env, 0.55f + 0.35f * lift, 1.5f + 0.95f * lift);
  w.fx.head = env * (0.1f - 0.12f * lift);
  if (u >= 1.f) fxRest(w);
}

// ---- food bag -----------------------------------------------------------------------------

// Line length that rests the sack on the ground under the branch tip.
float bagGroundLine(const World& w) { return w.spine.camp.bag_work.y - 2.f - w.bag.tip.y - 14.9f; }

void doBagUp(World& w, float) {
  w.bag.state = BAG_CARRIED;
  hikerCarry(w.hiker, CARRY_BAG);
}

// The rope is over the branch already: the sack goes on it, resting on the ground.
void doBagOnRope(World& w, float) {
  hikerCarry(w.hiker, CARRY_NONE);
  FoodBag& b = w.bag;
  b.state = BAG_HUNG;
  b.line = bagGroundLine(w);
  b.swing = 0.f;
}

// Foodbag a: up it goes on the first pull, past and back.
void actHoist(World& w, float u) {
  FoodBag& b = w.bag;
  if (u <= 0.f) w.spine.u0 = b.line;
  b.line = lerp(w.spine.u0, 16.f, easeOutBack(window(u, 0.1f, 1.f)));
  b.swing = 0.12f * sinT(1.5f * u) * (1.f - u);
  if (u >= 1.f) {
    b.line = 16.f;
    b.swing = 0.f;
  }
}

// Foodbag b: two swings back and forth with the bag in his near hand. Ends
// with the arm back, ready: the toss that follows takes the arm from there.
void actWindUp(World& w, float u) {
  const float env = easeInOut(window(u, 0.f, 0.15f));
  setArm(w, 0, env, 0.1f + 0.8f * sinT(2.f * u - 0.25f), 0.3f);
  w.fx.lean = env * 0.05f * sinT(2.f * u);
}

// The neck of a sack hanging from his near hand.
Vec2 handNeck(const World& w) {
  const Vec2 h = hikerHandPos(w.hiker, 0);
  return Vec2{h.x, h.y + 3.6f};
}

// One toss. The arm sweeps up and lets go at u 0.15; the sack flies to `apex`
// (relative to the branch tip). A miss comes back down into his hand; a catch
// goes over and drops to hang from the tip.
void toss(World& w, float u, float ax, float ay, bool catches) {
  FoodBag& b = w.bag;
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    hikerCarry(h, CARRY_NONE);
    b.state = BAG_FLYING;
  }
  const float sweep = easeOutCubic(window(u, 0.f, 0.18f));
  const float down = easeInOut(window(u, catches ? 0.6f : 0.82f, 1.f));
  setArm(w, 0, 1.f - down, lerp(-0.7f, 2.0f, sweep), lerp(0.3f, 0.2f, sweep));
  w.fx.lean = -0.06f * sweep * (1.f - down);
  const Vec2 apex{b.tip.x + ax, b.tip.y + ay};
  if (u < 0.15f) {   // still in his hand
    b.pos = handNeck(w);
    b.swing = 0.f;
    w.spine.v0 = b.pos;
    return;
  }
  const Vec2 p0 = w.spine.v0;
  if (!catches) {
    const float t = window(u, 0.15f, 0.82f);
    b.pos = Vec2{lerp(p0.x, apex.x, sinT(0.5f * t)), p0.y + (apex.y - p0.y) * 4.f * t * (1.f - t)};
    b.swing = 0.7f * sinT(t);
    if (u >= 0.82f) {   // caught again
      b.state = BAG_CARRIED;
      hikerCarry(h, CARRY_BAG);
    }
  } else {
    const Vec2 hang{b.tip.x, b.tip.y + 16.f};
    const float t1 = window(u, 0.15f, 0.55f), t2 = window(u, 0.55f, 0.75f);
    if (u < 0.55f) {
      b.pos = Vec2{lerp(p0.x, apex.x, t1), lerp(p0.y, apex.y, easeOutCubic(t1))};
    } else {
      b.pos = Vec2{lerp(apex.x, hang.x, t2), lerp(apex.y, hang.y, easeInCubic(t2))};
    }
    b.swing = 0.9f * sinT(0.5f * t1) * (1.f - t2);
    if (u >= 0.75f) {   // over the branch and hanging
      b.state = BAG_HUNG;
      b.line = 16.f;
      b.swing = 0.f;
    }
  }
  if (u >= 1.f) fxRest(w);
}
// He stands left of the tip; the branch runs from the tip to the right.
void actTossShort(World& w, float u) { toss(w, u, -22.f, -44.f, false); }   // nowhere near
void actTossClose(World& w, float u) { toss(w, u, -8.f, -54.f, false); }    // closer, still short of the branch
void actTossOver(World& w, float u) { toss(w, u, 12.f, -58.f, true); }      // over it, and down to hang

// It caught: the bag swings on its line and settles, three swings.
void actSettle(World& w, float u) {
  const float d = 1.f - u;
  w.bag.swing = 0.42f * sinT(3.f * u) * d * sqrtf(d);
  if (u >= 1.f) w.bag.swing = 0.f;
}

// Foodbag c: too heavy. He pulls; the sack barely leaves the ground, trembling.
void actStrain(World& w, float u) {
  FoodBag& b = w.bag;
  const float s = u * 2.6f;
  const float env = envelope(u, 0.2f, 0.25f);
  const float L0 = bagGroundLine(w);
  b.line = L0 - env * (3.f + 0.8f * sinT(s * 3.5f));
  w.fx.lean = env * (-0.16f + 0.025f * sinT(s * 3.5f));
  w.fx.lift = env * 0.008f * (0.5f + 0.5f * sinT(s * 3.5f + 0.25f));
  if (u >= 1.f) {
    b.line = L0;
    fxRest(w);
  }
}

// ...then a crouch-and-heave: it shoots up past the branch and bounces back.
void actHeave(World& w, float u) {
  FoodBag& b = w.bag;
  if (u <= 0.f) w.spine.u0 = b.line;
  float e;
  if (u < 0.22f) {
    e = 1.35f * easeOutCubic(window(u, 0.f, 0.22f));
  } else {
    const float v = window(u, 0.22f, 1.f);
    e = 1.f + 0.35f * cosT(1.5f * v) * (1.f - v) * (1.f - v);
  }
  b.line = lerp(w.spine.u0, 16.f, e);
  b.swing = 0.15f * sinT(2.f * u) * (1.f - u);
  w.fx.lift = 0.03f * (1.f - easeInOut(window(u, 0.05f, 0.3f))) * easeOutCubic(window(u, 0.f, 0.05f));
  if (u >= 1.f) {
    b.line = 16.f;
    b.swing = 0.f;
    fxRest(w);
  }
}

// ---- the tent at night -------------------------------------------------------------------

// He ducks in at the door and is gone; the lantern comes on inside.
void doEnterTent(World& w, float) {
  w.fx.on = false;
  w.fx.path_on = false;
  w.tent.shadow = false;
  hikerSetPose(w.hiker, POSE_STAND, 0.f);
}
void doLightOn(World& w, float) {
  w.tent.lit = true;
  w.tent.shadow = true;
  w.tent.shadow_x = -4.f;
}
void doLightOut(World& w, float) { w.tent.lit = false; }

// Close b: out of the tent once more, crouched at the door.
void doEmerge(World& w, float) {
  Hiker& h = w.hiker;
  h.feet = w.spine.camp.door;
  h.facing = 1;
  hikerSetPose(h, POSE_CROUCH, 0.f);
  w.fx.on = true;
  w.fx.path_on = false;
  w.tent.shadow = false;
}

// The fire is put to bed: embers, and the smoke dies away.
void doFireDown(World& w, float) {
  w.fire.state = FIRE_EMBERS;
  w.amb.smoke_k_to = 0.f;
}

// Close b: two prods with the stick; the fire answers each with a flare.
void actProd(World& w, float u) {
  FirePit& f = w.fire;
  if (u <= 0.f) w.spine.u0 = f.fuel;
  const float s = u * 2.6f;
  const float poke = maxf(plateau(s, 0.3f, 0.25f, 0.2f), plateau(s, 1.3f, 0.25f, 0.2f));
  w.fx.reach_to = 1.15f;
  w.fx.reach = envelope(u, 0.1f, 0.15f) * (0.55f + 0.45f * poke);
  f.fuel = minf(1.f, w.spine.u0 + 0.3f * poke);
  if (u >= 1.f) {
    f.fuel = minf(1.f, w.spine.u0 + 0.08f);   // stirred up a little
    fxRest(w);
  }
}

// Close b: a long look up at the sky.
void actLookUp(World& w, float u) {
  const float k = envelope(u, 0.15f, 0.15f);
  w.fx.head = -0.55f * k;
  w.fx.lean = -0.07f * k;
  if (u >= 1.f) fxRest(w);
}

// Close c: reading in bed. The light goes out, comes back, out, back, then out for good.
constexpr float kFlickerS = 4.6f;
void actFlicker(World& w, float u) {
  const float s = u * kFlickerS;
  const bool off = s < 0.5f || (s >= 1.9f && s < 2.3f) || u >= 1.f;
  w.tent.lit = !off;
}

// ---- fillers -----------------------------------------------------------------------------

// mug_sunset: two sips in 26 s and nothing else, looking out a little upward.
constexpr float kMugSunsetS = 26.f;
void actMugSunset(World& w, float u) {
  const float s = u * kMugSunsetS;
  const float sip = maxf(plateau(s, 6.f, 0.6f, 0.9f), plateau(s, 17.f, 0.6f, 0.9f));
  mugHold(w, envelope(u, 0.04f, 0.04f), sip, -0.1f);
  if (u >= 1.f) fxRest(w);
}

// read: the book out of the pack; a page turn every 8 s (a quick flick of the near arm).
void doBookUp(World& w, float) {
  w.items[ITEM_BOOK].down = false;
  hikerCarry(w.hiker, CARRY_BOOK);
}
void doBookDown(World& w, float) {
  hikerCarry(w.hiker, CARRY_NONE);
  setDown(w, ITEM_BOOK, besideHim(w.hiker, 12.f));
}
constexpr float kReadS = 30.f;
void actRead(World& w, float u) {
  const float s = u * kReadS;
  float turn = 0.f;
  for (int k = 0; k < 3; ++k) turn = maxf(turn, plateau(s, 7.f + 8.f * (float)k, 0.12f, 0.04f));
  setArm(w, 0, turn, 0.85f, 2.4f);
  if (u >= 1.f) fxRest(w);
}

// cairn: how many stones to reach, then one stone at a time, each dropping in
// with a small settle.
void doCairnStart(World& w, float) {
  const int t = (int)w.cairn.n + (int)w.plan.cairn_add;
  w.spine.target = (uint8_t)(t > CAIRN_MAX ? CAIRN_MAX : t);
}
bool cairnNotDone(const World& w) { return w.cairn.n < w.spine.target && w.cairn.n < CAIRN_MAX; }
void actPlaceStone(World& w, float u) {
  Cairn& c = w.cairn;
  if (u <= 0.f) {
    hikerCarry(w.hiker, CARRY_NONE);
    if (c.n < CAIRN_MAX) ++c.n;
  }
  c.pop = u;
  w.fx.reach_to = 0.7f;
  w.fx.reach = 0.6f * envelope(u, 0.2f, 0.4f);
  if (u >= 1.f) {
    c.pop = 1.f;
    fxRest(w);
  }
}

// skip_stone: from a crouch, the arm goes back, sweeps through low and lets
// go; the stone touches the water skip_n times downstream; he watches the rings.
constexpr float kSkipS = 4.6f, kSkipRelease = 0.62f;
void actSkip(World& w, float u) {
  const float s = u * kSkipS;
  const float back = easeInOut(window(s, 0.f, 0.5f));
  const float sweep = easeOutCubic(window(s, 0.5f, 0.72f));
  const float rest = 1.f - easeInOut(window(s, 1.3f, 2.0f));
  setArm(w, 0, back * rest, lerp(-0.9f, 1.55f, sweep), lerp(0.4f, 0.1f, sweep));
  w.fx.lean = 0.08f * back * rest - 0.06f * sweep * rest;
  w.fx.head = 0.15f * easeInOut(window(s, 0.8f, 1.4f)) * (1.f - easeInOut(window(s, 3.6f, 4.4f)));
  Skip& k = w.skip;
  if (k.t < 0.f && s >= kSkipRelease && u < 1.f) {   // let go: lay out the touches downstream
    k.n = w.plan.skip_n;
    k.p[0] = hikerHandPos(w.hiker, 0);
    float x = w.spine.camp.skip.x;
    for (int i = 1; i <= k.n && i <= SKIP_MAX; ++i) {
      x += 40.f - 6.f * (float)(i - 1);
      float cy, half;
      if (!landscapeStreamSpan(w.land, minf(x, 640.f), cy, half)) cy = k.p[i - 1].y + 6.f;
      k.p[i] = Vec2{minf(x, 640.f), cy + 0.15f * half * (float)(i & 1)};
    }
  }
  if (k.t >= 0.f || s >= kSkipRelease) k.t = s - kSkipRelease;
  if (u >= 1.f) {
    k.t = -1.f;
    fxRest(w);
  }
}

// cloud_watch: one small shift in 40 s, the hips over a few px.
void doPackOff(World& w, float) { w.hiker.pack_on = false; }
void actShift(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) w.spine.u0 = h.feet.x;
  const float k = sinT(0.5f * u);
  w.fx.lift = 0.02f * k;
  w.fx.head = -0.12f * k;
  h.feet.x = w.spine.u0 + (float)h.facing * 3.f * easeInOut(u);
  if (u >= 1.f) fxRest(w);
}

// stream_listen: the top of his near knee while he sits (SIT pose; the seat
// is the lowest point, a torso half width under the hips). Approximate: it
// mirrors the rig's SIT geometry, checked by eye in the preview.
Vec2 kneeTop(const World& w) {
  const Hiker& h = w.hiker;
  const float H = h.height;
  const float th = hikerPoseDef(POSE_SIT).thigh[0] * (1.f / 6.2831853f);
  const float hip_y = h.feet.y - 0.075f * H;
  return Vec2{h.feet.x + (float)h.facing * sinT(th) * 0.235f * H, hip_y + cosT(th) * 0.235f * H - 0.048f * H};
}

// Head bowed, eyes closed (as far as a silhouette can tell); a leaf drifts
// down on a sine and lands on his knee, and he leaves it there.
constexpr float kListenS = 30.f, kLeafFrom = 7.f, kLeafLand = 18.f;
void actListen(World& w, float u) {
  const float s = u * kListenS;
  const Hiker& h = w.hiker;
  w.fx.head = 0.24f * envelope(u, 0.08f, 0.06f);
  Leaf& l = w.leaf;
  const Vec2 k = kneeTop(w);
  const float f = (float)h.facing;
  // On top of the knee, tip up and forward, so it shows against the sky or
  // ground above the silhouette (it is ACTOR too).
  const Vec2 rest{k.x - 4.f * f, k.y - 3.5f};
  const float rest_rot = h.facing > 0 ? -0.07f : 0.57f;
  if (s >= kLeafFrom) {
    l.on = true;
    l.base_y = h.feet.y + 2.f;
    const float t = window(s, kLeafFrom, kLeafLand);
    const float fall = 1.f - t;
    l.pos = Vec2{rest.x + 26.f * f * fall + 13.f * sinT(1.7f * t) * fall, rest.y - 150.f * fall};
    l.rot = lerp(rest_rot, rest_rot + 0.14f * sinT(2.3f * t), fall);
  }
  if (u >= 1.f) w.fx.head = 0.f;
}

// He gets up; the leaf slides off his knee to the ground and stays there.
void actStandLeaf(World& w, float u) {
  Leaf& l = w.leaf;
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    w.spine.v0 = l.pos;
    w.spine.u0 = l.rot;
    hikerSetPose(h, POSE_STAND, 0.9f);
  }
  const float k = easeInCubic(window(u, 0.1f, 0.65f));
  l.pos = Vec2{w.spine.v0.x + 7.f * (float)h.facing * k, lerp(w.spine.v0.y, h.feet.y + 4.f, k)};
  l.rot = lerp(w.spine.u0, h.facing > 0 ? 0.02f : 0.48f, k);
  l.base_y = u >= 0.65f ? l.pos.y : h.feet.y + 2.f;
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

// ---- evening beats ----------------------------------------------------------------
// Each starts where the last beat or filler left him (sitting by the fire)
// and gets up first.

#define FROM_REST pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
#define PACK_CROUCH go(AT_PACK_WORK, kStroll), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.9f)

// Dinner a: the mug, sips by the fire.
const Step kDinnerA[] = {
    FROM_REST,
    go(AT_MUG, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.9f),
    doit(doMugUp),
    pose(POSE_STAND, 0.6f, 0.1f),
    TO_SIT,
    doit(hookDinnerCritters),   // stage 5: critters edge in; each sip scatters them
    act(actMugDinner, kMugDinnerS),
    still(0.8f),
    doit(doMugDown),
    still(1.2f),
};

// Dinner b: the pot on the fire boils over; he jumps back, hooks it off with a
// stick and eats out of it.
const Step kDinnerB[] = {
    FROM_REST,
    PACK_CROUCH,
    doit(doPotUp),
    pose(POSE_CARRY, 0.6f, 0.1f),
    go(AT_BLOW, 30.f),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    act(actPotOnFire, 1.0f),
    still(0.4f),
    go(AT_SIT, 20.f),
    face(TO_FIRE),
    pose(POSE_SIT, 0.9f, 0.5f),
    still(4.0f),
    act(actBoilOver, kBoilS),
    still(0.8f),
    pose(POSE_CROUCH, 0.7f, 0.2f),
    carry(CARRY_STICK),
    act(actPotOff, 2.0f),
    carry(CARRY_NONE),
    pose(POSE_SIT, 0.9f, 0.4f),
    doit(hookDinnerCritters),   // stage 5: critters edge in; each bite scatters them
    act(actEatPot, kEatPotS),
    still(1.0f),
};

// Dinner c: the tiny fork, held up and considered, used anyway.
const Step kDinnerC[] = {
    FROM_REST,
    PACK_CROUCH,
    act(actRummage, 2.4f),
    doit(doForkUp),
    pose(POSE_STAND, 0.6f, 0.1f),
    face(RIGHT),
    act(actHoldUp, 3.2f),
    act(actShrugFork, 1.4f),
    still(0.4f),
    TO_SIT,
    doit(hookDinnerCritters),   // stage 5: critters edge in; each bite scatters them
    act(actEatFork, kEatForkS),
    still(0.6f),
    doit(doForkDown),
    still(1.0f),
};

// Food bag, shared: out of the pack, over to the bag branch. Shared end: to
// the tent door, in, and the lantern comes on.
#define BAG_FETCH FROM_REST, PACK_CROUCH, doit(doBagUp), pose(POSE_CARRY, 0.6f, 0.1f)
#define TO_TENT                                                                                 \
  go(AT_DOOR, 36.f), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.5f), doit(doEnterTent), still(1.6f), \
      doit(doLightOn), still(1.0f)

// Food bag a: up on the first pull.
const Step kFoodbagA[] = {
    BAG_FETCH,
    go(AT_BAG_WORK, 36.f),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    doit(doBagOnRope),
    pose(POSE_STAND, 0.6f, 0.4f),
    pose(POSE_REACH_UP, 0.7f, 0.f),
    act(actHoist, 1.6f),
    still(0.4f),
    pose(POSE_STAND, 0.7f, 0.3f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.2f),
    pose(POSE_STAND, 0.6f, 0.2f),
    TO_TENT,
};

// Food bag b: the rope throw. Two misses, the third goes over.
const Step kFoodbagB[] = {
    BAG_FETCH,
    go(AT_BAG_WORK, 36.f, -26.f, 0.f),
    face(RIGHT),
    pose(POSE_STAND, 0.5f, 0.4f),
    act(actWindUp, 1.5f),
    act(actTossShort, 1.5f),
    still(0.7f),
    act(actWindUp, 1.5f),
    act(actTossClose, 1.5f),
    still(0.9f),
    act(actWindUp, 1.5f),
    act(actTossOver, 1.5f),
    act(actSettle, 3.0f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.0f),
    pose(POSE_STAND, 0.6f, 0.2f),
    TO_TENT,
};

// Food bag c: too heavy. A pull that barely lifts it, a head scratch, then a
// crouch and a heave that sends it up with a big overshoot.
const Step kFoodbagC[] = {
    BAG_FETCH,
    go(AT_BAG_WORK, 36.f),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    doit(doBagOnRope),
    pose(POSE_STAND, 0.6f, 0.3f),
    pose(POSE_REACH_UP, 0.7f, 0.1f),
    act(actStrain, 2.6f),
    pose(POSE_STAND, 0.6f, 0.6f),
    pose(POSE_SCRATCH_HEAD, 0.6f, 1.8f),
    pose(POSE_STAND, 0.5f, 0.3f),
    pose(POSE_CROUCH, 0.6f, 0.5f),
    pose(POSE_REACH_UP, 0.4f, 0.f),
    act(actHeave, 2.6f),
    pose(POSE_STAND, 0.7f, 0.3f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.2f),
    pose(POSE_STAND, 0.6f, 0.2f),
    TO_TENT,
};

// Bear: placeholder for stage 5. The lit tent with his shadow inside; the
// beat burns the fire down (beatUpdate<SB_BEAR>).
const Step kBearHold[] = {doit(doLightOn), still(1.0f)};

// Close a: the light goes out, the fire sinks to embers, the smoke stops.
const Step kCloseA[] = {
    still(4.0f), doit(doLightOut), still(3.5f), doit(doFireDown), still(4.0f),
    doit(hookTheft),   // stage 5: the night theft
    still(0.5f),
};

// Close b: out once more to check the fire, a long look at the sky, back in.
const Step kCloseB[] = {
    still(3.0f),
    doit(doEmerge),
    pose(POSE_STAND, 0.9f, 0.4f),
    go(AT_BLOW, 28.f),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    carry(CARRY_STICK),
    act(actProd, 2.6f),
    carry(CARRY_NONE),
    pose(POSE_STAND, 0.7f, 0.3f),
    face(RIGHT),
    act(actLookUp, 6.0f),
    go(AT_DOOR, 28.f),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.4f),
    doit(doEnterTent),
    doit(doLightOn),
    still(3.0f),
    doit(doLightOut),
    still(2.0f),
    doit(doFireDown),
    still(2.5f),
    doit(hookTheft),   // stage 5: the night theft
};

// Close c: reading in bed; the light flickers out and back twice first.
const Step kCloseC[] = {
    still(4.0f), act(actFlicker, kFlickerS), still(3.0f), doit(doFireDown), still(4.0f),
    doit(hookTheft),   // stage 5: the night theft
    still(0.5f),
};

// ---- fillers ------------------------------------------------------------------------

// mug_sunset: sits looking out over the valley with the mug; two sips.
const Step kFillMugSunset[] = {
    FROM_REST,
    go(AT_MUG, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.9f),
    doit(doMugUp),
    pose(POSE_STAND, 0.6f, 0.1f),
    go(AT_VIEW, kStroll),
    face(RIGHT),
    pose(POSE_SIT, 0.9f, 0.5f),
    act(actMugSunset, kMugSunsetS),
    doit(doMugDown),
    still(0.6f),
    pose(POSE_STAND, 0.8f, 0.3f),
    TO_SIT,
};

// read: the book out of the pack, against the reading rock, pages turning.
const Step kFillRead[] = {
    FROM_REST,
    PACK_CROUCH,
    doit(doBookUp),
    pose(POSE_STAND, 0.6f, 0.1f),
    go(AT_READ, kStroll),
    face(RIGHT),
    pose(POSE_READ, 1.0f, 0.5f),
    act(actRead, kReadS),
    doit(doBookDown),
    still(0.6f),
    pose(POSE_STAND, 0.9f, 0.3f),
    TO_SIT,
};

// cairn: flat stones one at a time from either side, each set on top; he
// steps back to admire it.
const Step kFillCairn[] = {
    FROM_REST,
    doit(doCairnStart),
    go(AT_STONE, 30.f),                 // <- repeat from here (9 steps)
    pose(POSE_CROUCH, 0.6f, 0.6f),
    carry(CARRY_ROCK),
    pose(POSE_STAND, 0.5f, 0.1f),
    go(AT_CAIRN_WORK, 28.f),
    faceTo(AT_CAIRN),
    pose(POSE_CROUCH, 0.6f, 0.15f),
    act(actPlaceStone, 0.9f),
    pose(POSE_STAND, 0.55f, 0.25f),
    repeatWhile(cairnNotDone, 9),
    still(0.6f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 0.2f),
    act(actBackStep, 1.2f),
    still(2.6f),
    pose(POSE_STAND, 0.7f, 0.3f),
    TO_SIT,
};

// skip_stone: crouched at the water, a throw, rings downstream, a nod.
const Step kFillSkipStone[] = {
    FROM_REST,
    go(AT_SKIP, kStroll),
    face(RIGHT),
    still(0.8f),
    pose(POSE_CROUCH, 0.7f, 1.8f),
    act(actSkip, kSkipS),
    still(0.5f),
    pose(POSE_STAND, 0.7f, 0.2f),
    act(actNod, 1.4f),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 5.5f),
    pose(POSE_STAND, 0.7f, 0.3f),
    TO_SIT,
};

// cloud_watch: on his back in the grass for 40 s, one small shift.
const Step kFillCloudWatch[] = {
    FROM_REST,
    go(AT_LIE, kStroll),
    face(RIGHT),
    doit(doPackOff),
    still(0.5f),
    pose(POSE_SIT, 0.9f, 0.4f),
    pose(POSE_LIE, 1.3f, 13.f),
    act(actShift, 1.8f),
    still(23.f),
    pose(POSE_SIT, 1.1f, 0.8f),
    pose(POSE_STAND, 0.9f, 0.3f),
    TO_SIT,
};

// stream_listen: sits by the water, head bowed; a leaf lands on his knee.
const Step kFillStreamListen[] = {
    FROM_REST,
    go(AT_LISTEN, kStroll),
    face(RIGHT),
    pose(POSE_SIT, 0.9f, 0.8f),
    act(actListen, kListenS),
    still(1.5f),
    act(actStandLeaf, 1.2f),
    still(0.3f),
    TO_SIT,
};

#undef FROM_REST
#undef TO_SIT
#undef PACK_CROUCH
#undef BAG_FETCH
#undef TO_TENT
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
    {SCRIPT(kDinnerA), SCRIPT(kDinnerB), SCRIPT(kDinnerC)},
    {SCRIPT(kFoodbagA), SCRIPT(kFoodbagB), SCRIPT(kFoodbagC)},
    {SCRIPT(kBearHold), NO_SCRIPT, NO_SCRIPT},
    {SCRIPT(kCloseA), SCRIPT(kCloseB), SCRIPT(kCloseC)},
};

const Script kFillerScripts[FILL_COUNT] = {SCRIPT(kFillMugSunset), SCRIPT(kFillRead),      SCRIPT(kFillCairn),
                                           SCRIPT(kFillSkipStone), SCRIPT(kFillCloudWatch), SCRIPT(kFillStreamListen)};

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
    {38, 41, 33},   // dinner
    {38, 48, 47},   // foodbag (the walks: pack, bag pine, tent door)
    {40, 0, 0},     // bear (placeholder for stage 5)
    {22, 32, 24},   // close (the last 2.5 s fade to black)
};

// The close's fade to black takes its last kFadeS seconds; the script is done by then.
constexpr float kFadeS = 2.5f;

// Filler seconds: the script plus a few. The cairn adds one stone cycle per stone.
const uint8_t kFillerSeconds[FILL_COUNT] = {47, 50, 0, 36, 58, 46};
constexpr int kCairnBaseS = 20, kCairnPerStoneS = 7;

const char* const kBeatNames[SB_COUNT] = {"arrival", "setup", "firewood", "fire",
                                          "dinner",  "foodbag", "bear",   "close"};
const char* const kFillerNames[FILL_COUNT] = {"mug_sunset", "read", "cairn", "skip_stone", "cloud_watch",
                                              "stream_listen"};

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
    case AT_PACK_WORK: return c.pack_work;
    case AT_BAG_WORK: return c.bag_work;
    case AT_DOOR: return c.door;
    case AT_VIEW: return c.view;
    case AT_READ: return c.read;
    case AT_LIE: return c.lie;
    case AT_LISTEN: return c.listen;
    case AT_SKIP: return c.skip;
    case AT_CAIRN: return w.cairn.pos;
    case AT_MUG: {
      const Item& m = w.items[ITEM_MUG];
      return m.down ? Vec2{m.pos.x + 13.f, m.pos.y - 2.f} : c.pack_work;
    }
    case AT_STONE: return c.stones[w.cairn.n & 1];
    case AT_CAIRN_WORK: {
      const float side = w.hiker.feet.x < w.cairn.pos.x ? -1.f : 1.f;
      return Vec2{w.cairn.pos.x + 13.f * side, w.cairn.pos.y - 2.f};
    }
    default: return w.hiker.feet;
  }
}

void walkTo(World& w, Vec2 to, float speed) {
  Hiker& h = w.hiker;
  HikerFx& fx = w.fx;
  fx.path_on = true;
  fx.path_x0 = h.feet.x;
  fx.path_y0 = h.feet.y;
  fx.path_x1 = to.x;
  fx.path_y1 = to.y;
  if (to.x == h.feet.x) h.feet.y = to.y;   // nothing to walk; arrive in place
  hikerWalkTo(h, to.x, speed);
}

// OP_GO's route from a to b: a waypoint behind the fire pit when the straight
// line would cross it, or a sideways waypoint (away from the fire) when the
// move is steeper than it is wide, since walks only move x and feet.y slides
// along. False when the straight line is fine.
bool routeVia(const World& w, Vec2 a, Vec2 b, Vec2& via) {
  const Vec2 F = w.fire.pos;
  const float dx = b.x - a.x, dy = b.y - a.y;
  if (fabsf(dx) > 1.f && (a.x - F.x) * (b.x - F.x) < 0.f) {
    const float y = a.y + dy * (F.x - a.x) / dx;
    if (y > F.y - 17.f && y < F.y + 16.f) {
      via = Vec2{F.x, F.y - 19.f};
      return true;
    }
  }
  if (fabsf(dx) < fabsf(dy)) {
    const float side = a.x < F.x ? -1.f : 1.f;
    const float x = side > 0.f ? maxf(a.x, b.x) + 0.5f * fabsf(dy) + 4.f : minf(a.x, b.x) - 0.5f * fabsf(dy) - 4.f;
    via = Vec2{minf(630.f, maxf(12.f, x)), 0.5f * (a.y + b.y)};
    return true;
  }
  return false;
}

void startWalk(World& w, const Step& s) {
  const Vec2 a = anchorPos(w, s.at);
  walkTo(w, Vec2{a.x + s.x, a.y + s.y}, s.s);
}

void startGo(World& w, const Step& s) {
  ScriptRun& r = w.spine.run;
  const Vec2 a = anchorPos(w, s.at);
  r.goal = Vec2{a.x + s.x, a.y + s.y};
  r.leg = routeVia(w, w.hiker.feet, r.goal, r.via) ? 0 : 1;
  walkTo(w, r.leg == 0 ? r.via : r.goal, s.s);
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
    case OP_GO: startGo(w, s); break;
    case OP_POSE: hikerSetPose(h, (PoseId)s.a, s.x); break;
    case OP_FACE:
      if (s.a == TO_FIRE) h.facing = w.fire.pos.x < h.feet.x ? -1 : 1;
      else if (s.a == 3) h.facing = anchorPos(w, s.at).x < h.feet.x ? -1 : 1;
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
    case OP_GO: {
      ScriptRun& r = w.spine.run;
      if (r.leg == 0) {
        if (w.hiker.walking && fabsf(w.hiker.feet.x - r.via.x) > 4.f) return false;
        r.leg = 1;   // on to the goal without breaking stride
        walkTo(w, r.goal, s.s);
        return false;
      }
      return hikerArrived(w.hiker);
    }
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
  ++r.beat_ticks;
  for (int guard = 0; guard < 16 && r.steps && r.i < r.n; ++guard) {
    const Step& s = r.steps[r.i];
    if (s.op == OP_REPEAT) {   // a loop back, decided on the spot
      if (s.pred && s.pred(w) && s.a <= r.i) r.i = (uint8_t)(r.i - s.a);
      else ++r.i;
      r.entered = false;
      continue;
    }
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
  if (r.steps && r.i >= r.n && r.done_at == 0) r.done_at = r.beat_ticks;
}

// ---- beats ----------------------------------------------------------------------------

void startScript(World& w, const Script& sc) {
  ScriptRun& r = w.spine.run;
  r.steps = sc.steps;
  r.n = sc.n;
  r.i = 0;
  r.entered = false;
  r.ticks = 0;
  r.leg = 1;
  r.beat_ticks = 0;
  r.done_at = 0;
}

template <int B>
void beatBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  startScript(w, kScripts[B][spineVariant(w.plan, B)]);
}

template <int F>
void fillerBegin(SceneCtx& ctx) {
  startScript(*ctx.w, kFillerScripts[F]);
}

void beatUpdate(SceneCtx& ctx, float, float) { runScript(*ctx.w); }

// Bear (placeholder): the fire burns down over the beat while he is in the tent.
void bearUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  runScript(w);
  if (w.fire.state == FIRE_LIT) w.fire.fuel = lerp(1.f, 0.45f, easeInOut(t));
}

// Close: the script, and the frame fading to black over the last kFadeS
// seconds, reaching 1 exactly on the beat's last tick (t = 1).
void closeUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  runScript(w);
  const float d = (float)kDurations[SB_CLOSE][spineVariant(w.plan, SB_CLOSE)];
  const float fade = easeInOut(window(t, 1.f - kFadeS / d, 1.f));
  // The script must be done before the fade starts; the beat's end comes
  // after the rollover has reset the count, so it is counted here.
  const ScriptRun& r = w.spine.run;
  if (w.fade == 0.f && fade > 0.f && r.steps && r.i < r.n && w.spine.overruns < 255) ++w.spine.overruns;
  w.fade = fade;
}

void beatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
#ifdef SS_SPINE_TRACE   // tuning aid: where each script finished in its beat
  printf("trace beat_end step=%u/%u feet=%.1f,%.1f ticks=%u done_at=%u frac=%.3f\n", (unsigned)w.spine.run.i,
         (unsigned)w.spine.run.n, (double)w.hiker.feet.x, (double)w.hiker.feet.y, (unsigned)w.spine.run.beat_ticks,
         (unsigned)w.spine.run.done_at,
         w.spine.run.beat_ticks ? (double)w.spine.run.done_at / (double)w.spine.run.beat_ticks : 0.0);
#endif
  if (w.spine.run.steps && w.spine.run.i < w.spine.run.n && w.spine.overruns < 255) ++w.spine.overruns;
  w.spine.run.steps = nullptr;
}

typedef void (*BeginFn)(SceneCtx&);
typedef void (*UpdateFn)(SceneCtx&, float, float);
const BeginFn kBegin[SB_COUNT] = {beatBegin<0>, beatBegin<1>, beatBegin<2>, beatBegin<3>,
                                  beatBegin<4>, beatBegin<5>, beatBegin<6>, beatBegin<7>};
const UpdateFn kUpdate[SB_COUNT] = {beatUpdate, beatUpdate, beatUpdate, beatUpdate,
                                    beatUpdate, beatUpdate, bearUpdate, closeUpdate};
const BeginFn kFillerBegin[FILL_COUNT] = {fillerBegin<0>, fillerBegin<1>, fillerBegin<2>,
                                          fillerBegin<3>, fillerBegin<4>, fillerBegin<5>};
static_assert(SB_COUNT == 8 && FILL_COUNT == 6, "extend kBegin, kUpdate and kFillerBegin");

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

int spineFillerSeconds(const LoopPlan& p, int f) {
  if (f < 0 || f >= FILL_COUNT) return 0;
  if (f == FILL_CAIRN) return kCairnBaseS + kCairnPerStoneS * (int)p.cairn_add;
  return kFillerSeconds[f];
}

namespace {

// Whole seconds of everything but the pauses.
int busySeconds(const LoopPlan& p) {
  int s = (int)(SS_BUILD_SECONDS + 0.5f);
  for (int b = 0; b < SB_COUNT; ++b) s += kDurations[b][spineVariant(p, b)];
  for (int i = 0; i < p.n_fillers; ++i) s += spineFillerSeconds(p, p.filler[i]);
  return s;
}

int pauseSeconds(const LoopPlan& p) {
  int s = 0;
  for (int b = 0; b + 1 < SB_COUNT; ++b) s += p.pause_s[b];
  for (int i = 0; i < p.n_fillers; ++i) s += p.filler_pause_s[i];
  return s;
}

// The pause slots in use, in timeline order, as pointers into the plan.
int pauseSlots(LoopPlan& p, uint8_t* slot[SB_COUNT + SPINE_FILLER_MAX]) {
  int n = 0;
  for (int b = 0; b + 1 < SB_COUNT; ++b) slot[n++] = &p.pause_s[b];
  for (int i = 0; i < p.n_fillers; ++i) slot[n++] = &p.filler_pause_s[i];
  return n;
}

// Aim inside the policy range with a margin, so no rounding lands on its edge.
constexpr int kAimMin = SS_LOOP_MIN_SECONDS + 10, kAimMax = SS_LOOP_MAX_SECONDS - 10;
constexpr uint8_t kPauseMin = 8, kPauseMax = 20;

}  // namespace

void spinePlan(LoopPlan& p, Rng& rng, const Inputs& in) {
  // One rng call per statement: argument evaluation order differs between
  // compilers, and the board must replay the PC preview.
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
  for (uint8_t& s : p.pause_s) s = (uint8_t)rng.rangeInt(kPauseMin, kPauseMax);
  p.pause_s[SB_CLOSE] = 0;   // the loop ends with the close
  p.away_s = (uint8_t)rng.rangeInt(6, 10);

  // Fillers: 2 to 4, weighted, no repeats. The sunset mug is likelier from
  // golden hour on (mood 3.5 and up).
  float wt[FILL_COUNT] = {1.f, 1.f, 1.2f, 1.f, 1.f, 1.f};
  if (in.mood >= 3.5f) wt[FILL_MUG_SUNSET] = 3.f;
  p.n_fillers = (uint8_t)rng.rangeInt(2, SPINE_FILLER_MAX);
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) {
    p.filler[i] = 0;
    p.filler_after[i] = 0;
    p.filler_pause_s[i] = 0;
  }
  for (int i = 0; i < p.n_fillers; ++i) {
    const int f = rng.weighted(wt, FILL_COUNT);
    p.filler[i] = (uint8_t)f;
    wt[f] = 0.f;
  }
  // Slots in play order: after fire and after dinner; a third goes after
  // dinner too; with four, the first goes after setup.
  static const uint8_t kSlots[3][SPINE_FILLER_MAX] = {{SB_FIRE, SB_DINNER, 0, 0},
                                                      {SB_FIRE, SB_DINNER, SB_DINNER, 0},
                                                      {SB_SETUP, SB_FIRE, SB_DINNER, SB_DINNER}};
  for (int i = 0; i < p.n_fillers; ++i) p.filler_after[i] = kSlots[p.n_fillers - 2][i];
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) {
    const uint8_t s = (uint8_t)rng.rangeInt(kPauseMin, kPauseMax);
    if (i < p.n_fillers) p.filler_pause_s[i] = s;
  }
  p.cairn_add = (uint8_t)rng.rangeInt(4, 6);
  p.skip_n = rng.chance(0.35f) ? 4 : 3;
  p.steam_n = (uint8_t)rng.rangeInt(3, 5);

  // Loop length policy: stretch or squeeze the pauses (round robin, one
  // second at a time, within 8..20 s) until the loop lands in range.
  uint8_t* slot[SB_COUNT + SPINE_FILLER_MAX];
  const int ns = pauseSlots(p, slot);
  const int busy = busySeconds(p);
  for (int guard = 0, k = 0; guard < 400 && busy + pauseSeconds(p) < kAimMin; ++guard, k = (k + 1) % ns)
    if (*slot[k] < kPauseMax) ++*slot[k];
  for (int guard = 0, k = 0; guard < 400 && busy + pauseSeconds(p) > kAimMax; ++guard, k = (k + 1) % ns)
    if (*slot[k] > kPauseMin) --*slot[k];
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

  // The evening places (see CampLayout).
  c.pack_work = Vec2{c.pack.x + 16.f, c.pack.y - 3.f};
  c.bag_work = Vec2{(float)(w.land.camp_x - 82) - 20.f, 356.f};   // left of the tip, facing the tree
  c.door = Vec2{c.tent.x + 20.f, c.tent.y + 4.f};
  c.pot_down = Vec2{F.x + 20.f, F.y + 11.f};
  c.view = Vec2{F.x + 92.f, F.y - 34.f};
  c.lie = Vec2{F.x + 140.f, F.y - 44.f};
  const float listen_x = F.x + 112.f, skip_x = F.x + 205.f;
  c.listen = landscapeStreamSpan(w.land, listen_x, cy, half) ? Vec2{listen_x, cy - half - 1.f} : Vec2{listen_x, F.y + 8.f};
  c.skip = landscapeStreamSpan(w.land, skip_x, cy, half) ? Vec2{skip_x, cy - half - 1.f} : Vec2{skip_x, F.y + 4.f};
  static const float kStoneDx[2] = {-36.f, 34.f};
  for (int k = 0; k < 2; ++k) {
    const float x = c.cairn.x + kStoneDx[k];
    c.stones[k] = landscapeStreamSpan(w.land, x, cy, half) ? Vec2{x, cy - half + 1.f} : Vec2{x, c.cairn.y + 4.f};
  }

  // The reading rock: a flat rock behind the fire on its right, near the tent's
  // door side, added to the landscape's rocks (so it is in the cache and pops
  // in with the others in the build). There is always a free slot: generation
  // stops at LS_MAX_ROCKS - 1.
  {
    static const float kShape[6][2] = {{-1.0f, 0.5f},  {-0.8f, -0.35f}, {-0.25f, -0.85f},
                                       {0.45f, -0.75f}, {0.95f, -0.1f},  {0.85f, 0.5f}};
    const float rx = F.x + 18.f, ry = 348.f, rw = 24.f, rh = 12.f;
    Landscape& ls = w.land;
    if (ls.n_rocks < LS_MAX_ROCKS) {
      LsRock& r = ls.rocks[ls.n_rocks++];
      for (int k = 0; k < 6; ++k) r.p[k] = Vec2{rx + kShape[k][0] * rw * 0.5f, ry + kShape[k][1] * rh - rh * 0.5f};
    }
    c.read = Vec2{rx + 13.f, ry + 2.f};
  }

  w.spine.run = ScriptRun{nullptr, 0, 0, false, 0, 1, Vec2{0.f, 0.f}, Vec2{0.f, 0.f}, 0, 0};
  w.spine.overruns = 0;
  w.spine.u0 = 0.f;
  w.spine.v0 = Vec2{0.f, 0.f};
  w.spine.target = 0;
}

void spineBuildTimeline(Timeline& tl, const LoopPlan& p) {
  tl.add(kBuildBeat);
  for (int b = 0; b < SB_COUNT; ++b) {
    const uint8_t v = spineVariant(p, b);
    tl.add(Beat{kBeatNames[b], (float)kDurations[b][v], kBegin[b], kUpdate[b], beatEnd});
    if (b == SB_CLOSE) break;   // the loop ends here
    tl.add(Beat{"pause", (float)p.pause_s[b], nullptr, nullptr, nullptr});
    for (int i = 0; i < p.n_fillers; ++i) {
      if (p.filler_after[i] != b) continue;
      const int f = p.filler[i];
      tl.add(Beat{kFillerNames[f], (float)spineFillerSeconds(p, f), kFillerBegin[f], beatUpdate, beatEnd});
      tl.add(Beat{"pause", (float)p.filler_pause_s[i], nullptr, nullptr, nullptr});
    }
  }
}

uint32_t spineLoopTicks(const LoopPlan& p) {
  // Every beat is whole seconds except the build, which is whole ticks.
  const uint32_t build = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  const int rest = busySeconds(p) - (int)(SS_BUILD_SECONDS + 0.5f) + pauseSeconds(p);
  return build + (uint32_t)rest * SS_TICK_HZ;
}

int spineFormatPlan(const LoopPlan& p, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int k = snprintf(buf, n, "arrival=%d setup=%d firewood=%d fire=%d dinner=%d foodbag=%d bear=%d close=%d fillers=",
                   p.arrival, p.setup, p.firewood, p.fire, p.dinner, p.foodbag, p.bear, p.close);
  if (k < 0 || (size_t)k >= n) return k;
  if (p.n_fillers == 0) return k + snprintf(buf + k, n - (size_t)k, "none");
  for (int i = 0; i < p.n_fillers && i < SPINE_FILLER_MAX; ++i) {
    const int m = snprintf(buf + k, n - (size_t)k, "%s%s", i ? "," : "", spineFillerName(p.filler[i]));
    if (m < 0) return k;
    k += m;
    if ((size_t)k >= n) return k;
  }
  return k;
}

}  // namespace ss
