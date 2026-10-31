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
#include "director.h"
#include "script.h"
#include "spine_family.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

// A pose change while fx offsets are on: World adds them to h.pose every tick,
// so take them out of the blend's start first, or they count twice.
void setPose(World& w, PoseId p, float seconds) {
  HikerPose& P = w.hiker.pose;
  P.lean -= w.fx.lean;
  P.head -= w.fx.head;
  P.lift -= w.fx.lift;
  hikerSetPose(w.hiker, p, seconds);
}

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
// Where the pack stands while his hands hold it (World's rule for a held pack).
Vec2 packAtHands(const World& w) {
  const Vec2 m = hikerHandsMid(w.hiker);
  return Vec2{m.x, minf(m.y + 13.f, w.hiker.feet.y + 3.f)};
}
// From where it fell behind his heels round to his hands, lifted a little on
// the way (it used to jump there in one tick).
void actPackLift(World& w, float u) {
  if (u <= 0.f) w.spine.v0 = w.pack.pos;
  const Vec2 to = packAtHands(w);
  const float k = easeInOut(u);
  w.pack.pos = Vec2{lerp(w.spine.v0.x, to.x, k), lerp(w.spine.v0.y, to.y, k) - 6.f * sinT(0.5f * k)};
  if (u >= 1.f) {
    w.pack.held = true;
    hikerCarry(w.hiker, CARRY_BAG);
  }
}
// Down from his hands to stand on the ground in front of his feet (it used
// to drop there in one tick, 4.5 to 7 px).
void actPackSetDown(World& w, float u) {
  if (w.pack.held) {
    w.pack.held = false;
    hikerCarry(w.hiker, CARRY_NONE);
    w.spine.v0 = w.pack.pos;
  }
  w.pack.pos.y = lerp(w.spine.v0.y, w.hiker.feet.y + 3.f, easeInOut(u));
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
    setPose(w, POSE_YELP, 0.3f);
  }
  if (u >= 0.32f && h.target == POSE_YELP) setPose(w, POSE_STAND, 0.45f);
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

// Setup (memory): patching the mouse hole at the tent's base. The near hand
// works at the hole; it is gone two thirds of the way through.
bool tentBitten(const World& w) { return w.tent.bite; }
void actPatch(World& w, float u) {
  const float e = easeInOut(window(u, 0.f, 0.15f)) * (1.f - easeInOut(window(u, 0.8f, 1.f)));
  w.fx.reach_to = 0.55f;
  w.fx.reach = 0.8f * e;
  w.fx.lean = 0.12f * e * (1.f + 0.4f * sinT(u * 6.f));
  if (u >= 0.66f && w.tent.bite) {
    w.tent.bite = false;
    w.memory.mouse_hole = false;
  }
  if (u >= 1.f) w.fx.reach = w.fx.lean = 0.f;
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
    setPose(w, POSE_YELP, 0.35f);
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

// ---- critter scenes (stage 5) ---------------------------------------------------------
// The dinner visitors and the night thief run beside the script, from
// spineCritterScenes every tick; the hooks in the scripts start them. The bear
// beat is a script of its own (below the food bag).

// A bite or sip: the hand passing half way up, counted once on the way up.
void noteBite(World& w, float lift) {
  SpineState& sp = w.spine;
  if (lift > 0.5f && !sp.bite_up) {
    sp.bite_up = true;
    if (sp.bites < 255) ++sp.bites;
  } else if (lift < 0.3f) {
    sp.bite_up = false;
  }
}

// Ground the critters can stand on at x: `want`, or higher up the bank if the
// stream (with its far bank strip) comes closer.
float groundAt(const World& w, float x, float want) {
  float cy, half;
  if (!landscapeStreamSpan(w.land, x, cy, half)) return want;
  return minf(want, cy - 1.06f * half - 14.f);
}

constexpr float kDeerWalk = 30.f, kDeerStep = 14.f, kChipRun = 70.f;
constexpr int kDeerMaxSteps = 7;

// Where the deer edges toward: well short of the hiker sitting at camp.sit.
Vec2 deerNearest(const World& w) {
  const Vec2 s = w.spine.camp.sit;
  const float x = s.x + 150.f;
  return Vec2{x, groundAt(w, x, s.y - 16.f)};
}
Vec2 deerSpot(const World& w) {
  const float x = (float)w.land.camp_x + 44.f;
  return Vec2{x, groundAt(w, x, 350.f)};
}

// Dinner: a deer comes out at the right edge of camp and a chipmunk runs in
// from the left behind the fire. Both are shy: the first move he makes, they go.
void hookDinnerCritters(World& w, float) {
  SpineState& sp = w.spine;
  if (sp.deer < 0) {
    const float x = (float)SS_WIDTH + 50.f;
    const int d = directorSpawn(w, CR_DEER, Vec2{x, groundAt(w, x, 340.f)}, -1);
    if (d >= 0) {
      w.crit[d].tag = CT_DINNER;
      w.crit[d].flags = CF_SHY;
      directorWalk(w, d, deerSpot(w), kDeerWalk, CA_MOVE);
      sp.deer = (int8_t)d;
      sp.deer_steps = sp.bites;
      sp.deer_t = 0.f;
    }
  }
  if (sp.chip < 0) {
    const float y = w.fire.pos.y - 32.f;
    const int c = directorSpawn(w, CR_CHIPMUNK, Vec2{-18.f, y}, 1);
    if (c >= 0) {
      w.crit[c].tag = CT_DINNER;
      w.crit[c].flags = CF_SHY;
      directorWalk(w, c, Vec2{w.fire.pos.x - 96.f, y}, kChipRun, CA_RUN);
      sp.chip = (int8_t)c;
      sp.chip_stage = 0;
      sp.chip_t = 0.f;
    }
  }
}

// Dinner b: the pot boils over and he jumps back; everything near the camp bolts.
void doDinnerScatter(World& w, float) { directorScatter(w, w.hiker.feet, 600.f); }

void deerTick(World& w) {
  SpineState& sp = w.spine;
  if (sp.deer < 0) return;
  if (!directorOwns(w, sp.deer, CT_DINNER)) {   // scattered (or gone): no longer ours
    if (!w.crit[sp.deer].in_use || w.crit[sp.deer].tag != CT_DINNER) sp.deer = -1;
    return;
  }
  Critter& c = w.critters[sp.deer];
  sp.deer_t += SS_TICK_DT;
  if (!critterArrived(c)) {
    sp.deer_t = 0.f;
    return;
  }
  // Arrived somewhere: freeze, head up, looking at him.
  if (c.action == CA_IDLE && sp.deer_t > 0.25f) critterSetAction(c, CA_LOOK, 0.4f);
  // Dinner b has no bites before the pot boils over: the deer edges in every
  // few seconds instead, drawn by the smell.
  if (w.plan.dinner == 1 && sp.deer_t > 3.2f && sp.bites == sp.deer_steps) {
    ++sp.bites;
    sp.bite_up = false;
  }
  const int taken = (int)sp.deer_steps;
  if (sp.bites > sp.deer_steps && sp.deer_t > 0.6f) {
    sp.deer_steps = sp.bites;
    const Vec2 near = deerNearest(w);
    // A bold deer (memory: the brave/shy split) comes all the way; a shy one stops at four.
    const int most = memoryBrave(w.memory, CR_DEER) ? kDeerMaxSteps : 4;
    if (taken < most && c.feet.x - kDeerStep > near.x) {
      const float x = c.feet.x - kDeerStep;
      const Vec2 from = deerSpot(w);
      const float u = clamp01((from.x - x) / maxf(1.f, from.x - near.x));
      critterSetAction(c, CA_MOVE, 0.2f);
      directorWalk(w, sp.deer, Vec2{x, lerp(from.y, near.y, u)}, 16.f, CA_MOVE);
    }
  }
}

void chipTick(World& w) {
  SpineState& sp = w.spine;
  if (sp.chip < 0) return;
  if (!directorOwns(w, sp.chip, CT_DINNER)) {
    if (!w.crit[sp.chip].in_use || w.crit[sp.chip].tag != CT_DINNER) sp.chip = -1;
    return;
  }
  Critter& c = w.critters[sp.chip];
  sp.chip_t += SS_TICK_DT;
  const float y = w.fire.pos.y - 32.f;
  switch (sp.chip_stage) {
    case 0:   // in from the left, then up on its haunches for a look
      if (critterArrived(c)) {
        critterSetAction(c, CA_SIT, 0.35f);
        sp.chip_stage = 1;
        sp.chip_t = 0.f;
      }
      break;
    case 1:
      if (sp.chip_t > 0.6f && c.action == CA_SIT) critterSetAction(c, CA_LOOK, 0.3f);
      if (sp.chip_t > 1.8f) {   // on, behind the fire
        directorWalk(w, sp.chip, Vec2{w.fire.pos.x - 24.f, y}, kChipRun, CA_RUN);
        sp.chip_stage = 2;
        sp.chip_t = 0.f;
      }
      break;
    case 2:   // finds a crumb behind the fire and eats it, sitting up
      if (critterArrived(c) && sp.chip_t > 0.3f) {
        c.carry = CC_CRUMB;
        critterSetAction(c, CA_SIT, 0.3f);
        sp.chip_stage = 3;
        sp.chip_t = 0.f;
      }
      break;
    default:   // eat, look at him, eat
      if (sp.chip_t > 0.4f && c.action == CA_SIT) critterSetAction(c, CA_EAT, 0.3f);
      if (sp.chip_t > 4.0f && c.action == CA_EAT) critterSetAction(c, CA_LOOK, 0.25f);
      if (sp.chip_t > 5.6f) sp.chip_t = 0.2f;
      break;
  }
}

// The close: a shoe by the tent door; a raccoon walks off with it.
constexpr float kThiefWalk = 38.f;

// Food bag, at the tent door: one crouch and a shoe comes off and stays
// outside. Not while the raccoon has it (memory), nor on the night it brought
// it back (raccoon_pack): he keeps it in the tent then. Its spot is kept
// either way (the thief comes to the door).
void doShoeOff(World& w, float) {
  Item& s = w.items[ITEM_SHOE];
  const Hiker& h = w.hiker;
  s.held = false;
  s.on_fire = false;
  s.critter = 0;
  s.down = !w.memory.shoe_taken && !w.memory.shoe_back;
  s.facing = 1;
  s.pos = Vec2{h.feet.x + 11.f, h.feet.y + 2.f};
}

// The night thief: the shoe at the door if it is out; else the mug or the
// book, whichever is lying out; else it noses round the door and leaves.
void hookTheft(World& w, float) {
  SpineState& sp = w.spine;
  if (sp.thief >= 0) return;
  int loot = -1;
  if (w.items[ITEM_SHOE].down) loot = ITEM_SHOE;
  else if (w.items[ITEM_MUG].down) loot = ITEM_MUG;
  else if (w.items[ITEM_BOOK].down) loot = ITEM_BOOK;
  const Vec2 at = w.items[loot >= 0 ? loot : ITEM_SHOE].pos;
  const int r = directorSpawn(w, CR_RACCOON, Vec2{-34.f, at.y - 1.f}, 1);
  if (r < 0) return;
  w.crit[r].tag = CT_THIEF;
  // Quicker when it is far in, so it is out before the fade.
  const float v = maxf(kThiefWalk, (at.x + 21.f) / 7.f);
  directorWalk(w, r, Vec2{at.x - 13.f, at.y - 1.f}, v, CA_MOVE);
  sp.thief = (int8_t)r;
  sp.thief_loot = (uint8_t)(loot + 1);
  sp.thief_stage = 0;
  sp.thief_t = 0.f;
}

void thiefTick(World& w) {
  SpineState& sp = w.spine;
  if (sp.thief < 0) return;
  if (!directorOwns(w, sp.thief, CT_THIEF)) {
    if (!w.crit[sp.thief].in_use || w.crit[sp.thief].tag != CT_THIEF) sp.thief = -1;
    return;
  }
  Critter& c = w.critters[sp.thief];
  sp.thief_t += SS_TICK_DT;
  auto next = [&](uint8_t st) {
    sp.thief_stage = st;
    sp.thief_t = 0.f;
  };
  switch (sp.thief_stage) {
    case 0:
      if (critterArrived(c)) {
        critterSetAction(c, CA_SNIFF, 0.4f);
        next(1);
      }
      break;
    case 1:   // a good long sniff (nothing to take: a second one, nosing about)
      if (sp.thief_t > (sp.thief_loot ? 1.8f : 3.2f)) {
        critterSetAction(c, sp.thief_loot ? CA_CARRY : CA_LOOK, 0.5f);
        next(2);
      }
      break;
    case 2:   // picks it up
      if (sp.thief_t > 0.25f && sp.thief_loot && w.items[sp.thief_loot - 1].down) {
        Item& it = w.items[sp.thief_loot - 1];
        it.down = false;
        if (sp.thief_loot == ITEM_SHOE + 1) {   // the shoe it carries itself (the rig draws it)
          c.carry = CC_SHOE;
          w.memory.shoe_taken = true;
        } else {   // the mug or the book, in its mouth
          it.held = true;
          it.critter = (uint8_t)(sp.thief + 1);
          it.angle = 0.f;
          it.placed = false;
        }
        w.memory.last_stolen = sp.thief_loot;
      }
      if (sp.thief_t > 0.7f) {   // turns toward the tent
        directorWalk(w, sp.thief, Vec2{c.feet.x - 3.f, c.feet.y}, 10.f, CA_MOVE);
        next(3);
      }
      break;
    case 3:
      if (critterArrived(c) && sp.thief_t > 0.3f) {
        critterSetAction(c, CA_LOOK, 0.35f);
        next(4);
      }
      break;
    case 4:   // the guilty pause, then off the way it came
      if (sp.thief_t > 1.9f) {   // (from far in, quicker: out before the fade)
        directorExit(w, sp.thief, -1, maxf(kThiefWalk, (c.feet.x + 60.f) / 6.f), CA_MOVE);
        sp.thief = -1;   // the director releases it once it is out of sight
        next(5);
      }
      break;
    default: break;
  }
}

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
  noteBite(w, sip);
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
  noteBite(w, lift * env);
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
  noteBite(w, lift * env);
  setArm(w, 0, env, 0.55f + 0.35f * lift, 1.5f + 0.95f * lift);
  w.fx.head = env * (0.1f - 0.12f * lift);
  if (u >= 1.f) fxRest(w);
}

// Dinner d: the fish he caught (fishing_good) has been roasting on its stick.
void doFishOffFire(World& w, float) {
  Item& f = w.items[ITEM_FISH];
  f.on_fire = false;
  f.down = false;
  f.held = true;
  f.critter = 0;
  f.size = 1.f;
  f.angle = 0.1f;
}
// Eats it off the stick: three bites, the fish shorter each time, down to the bones.
constexpr float kEatFishS = 12.f;
void actEatFish(World& w, float u) {
  const float s = u * kEatFishS;
  const float env = envelope(u, 0.05f, 0.06f);
  const float ph = s / 4.f;   // a bite every 4 s: up, chew, down
  const int k = (int)ph;
  const float lift = plateau(ph - floorf(ph), 0.18f, 0.16f, 0.3f);
  noteBite(w, lift * env);
  setArm(w, 0, env, 0.45f + 0.5f * lift, 1.25f + 1.0f * lift);
  w.fx.head = env * (0.1f - 0.16f * lift + 0.04f * sinT(s * 2.5f) * (1.f - lift));   // chewing
  Item& f = w.items[ITEM_FISH];
  f.angle = 0.1f + 0.08f * lift;
  // Each bite takes a piece at the top of the lift.
  int bites = k;
  if (ph - floorf(ph) > 0.4f) ++bites;
  static const float kLeft[4] = {1.f, 0.68f, 0.38f, 0.f};
  f.size = kLeft[bites > 3 ? 3 : bites];
  if (u >= 1.f) fxRest(w);
}
// The bones into the fire, underarm.
void actTossBones(World& w, float u) {
  Item& f = w.items[ITEM_FISH];
  if (u <= 0.f) w.spine.v0 = hikerHandPos(w.hiker, 0);
  const float sw = easeInOut(window(u, 0.1f, 0.4f));
  setArm(w, 0, envelope(u, 0.15f, 0.3f), lerp(0.3f, 1.6f, sw), 0.3f);
  if (u >= 0.35f && f.held) {
    f.held = false;
    f.down = true;
    f.on_fire = true;   // sorted in front of the flames all the way
  }
  if (!f.held && f.down) {
    const float k = clamp01((u - 0.35f) / 0.5f);
    const Vec2 to{w.fire.pos.x - 2.f, w.fire.pos.y - 4.f};
    const Vec2 a = w.spine.v0;
    f.pos = Vec2{lerp(a.x, to.x, k), lerp(a.y, to.y, k) - 22.f * 4.f * k * (1.f - k)};
    f.angle = 0.1f + 0.9f * k;
    if (k >= 1.f) f.down = f.on_fire = false;   // gone in the flames
  }
  if (u >= 1.f) fxRest(w);
}
// Pats his stomach: full.
void actPatStomach(World& w, float u) {
  const float env = envelope(u, 0.15f, 0.15f);
  const float pat = 0.5f + 0.5f * sinT(u * 5.f);
  setArm(w, 0, env, 0.25f + 0.12f * pat, 1.75f + 0.2f * pat);
  w.fx.lean = -0.1f * env;
  w.fx.head = -0.12f * env;
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

// ---- the bear ------------------------------------------------------------------------------
// He is in the lit tent. The bear walks in from the right, sniffs toward the
// tent (the shadow inside freezes, then trembles), goes to the bag pine and
// tries: a) climbing, b) shaking the trunk, c) swatting with a stick. The
// shadow follows the bear's x with a lag the whole time (bearUpdate).

constexpr float kBearWalk = 22.f, kBearRun = 72.f;

Critter* bearOf(World& w) {
  return directorOwns(w, w.spine.bear, CT_BEAR) ? &w.critters[w.spine.bear] : nullptr;
}
// The bear's slot still holds the bear (once released, the slot may go to
// another critter: the tag tells).
bool bearHeld(const World& w) {
  const int b = w.spine.bear;
  return b >= 0 && w.crit[b].in_use && w.crit[b].tag == CT_BEAR;
}
bool bearArrived(const World& w) { return !bearHeld(w) || critterArrived(w.critters[w.spine.bear]); }
bool bearGone(const World& w) { return !bearHeld(w); }

float trunkX(const World& w) { return (float)w.land.camp_x; }
constexpr float kBearTreeY = 346.f;   // in front of the camp pine's foot (340)

// Where it stops to sniff, right of the bag pine.
Vec2 bearSniffSpot(const World& w) {
  const float x = trunkX(w) + 96.f;
  return Vec2{x, groundAt(w, x, kBearTreeY)};
}

void bearWalk(World& w, Vec2 to, float v, bool back = false) {
  if (bearHeld(w)) directorWalk(w, w.spine.bear, to, v, CA_MOVE, back);
}

void doBearEnter(World& w, float) {
  SpineState& sp = w.spine;
  const float x = (float)SS_WIDTH + 60.f;
  const int b = directorSpawn(w, CR_BEAR, Vec2{x, groundAt(w, x, 340.f)}, -1);
  sp.bear = (int8_t)b;
  if (b < 0) return;
  w.crit[b].tag = CT_BEAR;
  w.crit[b].flags = CF_BRAVE;
  // The bear's pace barely varies, so the beat's length holds on every seed.
  Critter& c = w.critters[b];
  c.speed_dial = 0.4f + 0.2f * c.speed_dial;
  sp.shadow_v = w.tent.shadow_x;
  sp.fear = 0.f;
  bearWalk(w, bearSniffSpot(w), kBearWalk);
}

// Nose up, sniffing toward the tent; the shadow freezes, then shakes.
void actBearSniff(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) critterSetAction(*c, CA_SNIFF, 0.7f);
  c->head_add = 0.35f * envelope(u, 0.15f, 0.2f);
  w.spine.fear = u < 0.12f ? 0.f : (u < 0.45f ? 0.05f : 1.f);
  if (u >= 1.f) {
    c->head_add = 0.f;
    critterSetAction(*c, CA_IDLE, 0.5f);
  }
}

// A long look up at the bag.
void actBearLookUp(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) critterSetAction(*c, CA_LOOK, 0.6f);
  c->head_add = 0.45f * envelope(u, 0.3f, 0.25f);
  if (u >= 1.f) c->head_add = 0.f;
}

void doBearToTrunk(World& w, float) {   // climb and shake: right of the trunk, facing it
  const float dx = w.plan.bear == 0 ? 5.f : 20.f;
  bearWalk(w, Vec2{trunkX(w) + dx, kBearTreeY}, kBearWalk);
}

void doBearLeave(World& w, float) {
  SpineState& sp = w.spine;
  if (bearHeld(w)) directorExit(w, sp.bear, 1, kBearWalk, CA_MOVE);
  sp.fear = 0.f;
}

// a) Climb: up the trunk to just under the branch.
constexpr float kBearClimbH = 74.f;   // the climbing figure, feet to the top of its head
void doBearClimb(World& w, float) {
  Critter* c = bearOf(w);
  if (!c) return;
  critterClimbTo(*c, (float)LS_BRANCH_ROOT_Y + 7.f + kBearClimbH, 13.f);
}

// At the top, one paw out toward the bag along the branch: well short. Two tries.
void actBearReachTrunk(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  const float s = u * 2.f;
  const float k = s - floorf(s);
  const float strain = sinT(0.5f * window(k, 0.f, 0.45f)) * (1.f - window(k, 0.75f, 1.f));
  c->fore_add = u >= 1.f ? 0.f : -0.25f * strain + 0.06f * strain * sinT(u * 9.f);
  c->head_add = u >= 1.f ? 0.f : 0.25f * envelope(u, 0.1f, 0.1f);
}

// Slides down the trunk and lands with a thump: a little dust at the foot.
void actBearSlide(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) w.spine.u0 = c->feet.y;
  c->feet.y = lerp(w.spine.u0, kBearTreeY, easeInCubic(u));
  if (u >= 1.f) {
    for (int k = 0; k < 2; ++k) {
      const int s = worldTakeBit(w, BIT_DUST);
      if (s < 0) break;
      Bit& b = w.bits[s];
      b.pos = Vec2{c->feet.x + (k ? 14.f : -10.f), kBearTreeY - 3.f};
      b.vel = Vec2{k ? 14.f : -12.f, -9.f};
      b.land_y = kBearTreeY + 1.f;
    }
    critterSetAction(*c, CA_SIT, 0.45f);
  }
}

// Sits where it landed and looks up at the bag for a while.
void actBearSitLook(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  c->head_add = 0.75f * envelope(u, 0.25f, 0.2f);
  if (u >= 1.f) critterSetAction(*c, CA_IDLE, 0.7f);
}

// The hung bag swings when something hits it: an impulse on its damped swing.
void bagKick(World& w, float v) {
  if (w.bag.state != BAG_HUNG) return;
  w.spine.bag_v += v;
  w.spine.bag_osc = true;
}

// b) Shake: up on its hind legs, arms round the trunk, shaking. The bag swings
// and cones drop: one to the ground, one on its head.
void actBearRear(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) critterSetAction(*c, CA_REAR, 1.0f);
}

void dropCone(World& w, Vec2 from, float hit_y, float land_y) {
  const int s = worldTakeBit(w, BIT_CONE);
  if (s < 0) return;
  Bit& b = w.bits[s];
  b.pos = from;
  b.vel = Vec2{0.f, 30.f};
  b.ground_y = hit_y;
  b.land_y = land_y;
}

constexpr float kShakeS = 5.5f;
void actBearShake(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  const float s = u * kShakeS, prev = s - SS_TICK_DT;
  if (u <= 0.f) critterSetAction(*c, CA_SHAKE, 0.45f);
  if (u < 0.9f) {   // the trunk shakes: each jolt pumps the bag along the way it already swings
    const float ph = s * 2.f;
    if (floorf(ph) != floorf(ph - SS_TICK_DT * 2.f)) bagKick(w, w.spine.bag_v < 0.f ? -0.55f : 0.55f);
  }
  if (prev < 1.6f && s >= 1.6f)   // a cone off the canopy to the ground
    dropCone(w, Vec2{trunkX(w) - 26.f, 236.f}, kBearTreeY + 6.f, kBearTreeY + 6.f);
  if (prev < 4.4f && s >= 4.4f) {   // ...and one right on its head
    const Vec2 h = critterHead(*c);
    dropCone(w, Vec2{h.x, h.y - 64.f}, h.y + 2.f, c->feet.y + 4.f);
  }
}

// Nose to the bark, up and down the trunk.
void actBearSniffTrunk(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) critterSetAction(*c, CA_SNIFF, 0.5f);
  c->head_add = 0.3f * sinT(0.5f * u) * sinT(0.5f * u);
  if (u >= 1.f) {
    c->head_add = 0.f;
    critterSetAction(*c, CA_IDLE, 0.4f);
  }
}

// Bonk: it drops and turns, frozen a moment, then runs.
void actBearStartle(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) critterSetAction(*c, CA_STARTLE, 0.9f);
  w.spine.fear = 1.f;
}
void doBearRunOff(World& w, float) {
  SpineState& sp = w.spine;
  if (bearHeld(w)) directorExit(w, sp.bear, 1, kBearRun, CA_RUN);
  sp.fear = 0.f;
}

// c) The stick: the fire-poking stick if it is lying out, else a fallen branch
// (laid by the bag pine at the start of the loop). Carried in the mouth,
// trailing back along its side, swung from the paw.
//
// The point of the lying stick its mouth takes it by: kItemMouthGrip of the
// stick ahead of the mouth (facing `f`), as the director carries it.
float stickGripX(const Item& st, int f) {
  return st.pos.x + (float)f * (0.5f - kItemMouthGrip) * kItemStickLen;
}
void doBearToStick(World& w, float) {
  Critter* c = bearOf(w);
  if (!c) return;
  const Item& st = w.items[ITEM_STICK];
  const Vec2 m = critterMouth(*c);
  const float reach = fabsf(m.x - c->feet.x);
  // Its mouth over the grip point, facing the way it faces now; when that is
  // behind it, a short step back (no turn), or round if it is far.
  int f = c->facing;
  float x = stickGripX(st, f) - (float)f * reach;
  const bool behind = (x - c->feet.x) * (float)f < 0.f;
  const bool back = behind && fabsf(x - c->feet.x) < 40.f;
  if (behind && !back) {
    f = -f;
    x = stickGripX(st, f) - (float)f * reach;
  }
  // By the fire the stick lies in front of the stones: a few px behind it
  // walks it behind the flames, never into them.
  const float y = st.pos.y < 380.f ? st.pos.y - 1.f : st.pos.y - 5.f;
  bearWalk(w, Vec2{x, groundAt(w, x, y)}, back ? 12.f : (fabsf(x - c->feet.x) > 200.f ? 30.f : kBearWalk), back);
}

// Head down to the stick; it comes up from the ground into its mouth (eased:
// the lying stick and the carried one are the same line at the start).
void actBearPickStick(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) critterSetAction(*c, CA_SNIFF, 0.4f);
  Item& st = w.items[ITEM_STICK];
  const uint8_t mine = (uint8_t)(w.spine.bear + 1);
  constexpr float kGrab = 0.45f, kUp = 0.75f;
  if (u >= kGrab && !st.critter) {
    w.spine.v0 = Vec2{stickGripX(st, c->facing), st.pos.y - 1.75f};   // the grip point, on the ground
    st.critter = mine;
    st.held = true;
    st.down = false;
    st.placed = true;
    st.facing = (int8_t)c->facing;
    st.squeeze = 1.f;
    st.grip = kItemMouthGrip;
    st.angle = -0.25f;   // lying flat, back from the grip
  }
  if (st.critter == mine && st.placed) {
    const float e = easeInOut(window(u, kGrab, kUp));
    const Vec2 m = critterMouth(*c);
    st.pos = Vec2{lerp(w.spine.v0.x, m.x, e), lerp(w.spine.v0.y, m.y, e)};
    st.angle = lerp(-0.25f, -0.29f, e);   // the back end trailing a little low, along its side
    if (u >= kUp) st.placed = false;     // the director carries it from here
  }
  if (u >= 1.f) critterSetAction(*c, CA_IDLE, 0.4f);
}

void doBearToBag(World& w, float) {
  Critter* c = bearOf(w);
  if (!c) return;
  const float side = c->feet.x < w.bag.tip.x ? -1.f : 1.f;
  bearWalk(w, Vec2{w.bag.tip.x + 20.f * side, kBearTreeY + 2.f}, fabsf(w.bag.tip.x - c->feet.x) > 150.f ? 30.f : 24.f);
}

// Up on its hind legs with the stick raised.
void actBearRaiseStick(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) {
    critterSetAction(*c, CA_REACH, 1.0f);
    w.spine.u0 = w.items[ITEM_STICK].angle;
  }
  // The director moves it from mouth to paw as it rears, held by the end there.
  Item& st = w.items[ITEM_STICK];
  st.angle = lerp(w.spine.u0, -0.04f, easeInOut(window(u, 0.1f, 0.75f)));   // up as it rears
}

// Two swats: a wind-up back, a fast sweep through the bag, back up.
constexpr float kSwatS = 3.4f;
void actBearSwat(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  Item& st = w.items[ITEM_STICK];
  const float s = u * kSwatS;
  const float k = s < 1.7f ? s / 1.7f : (s - 1.7f) / 1.7f;
  const float back = easeInOut(window(k, 0.f, 0.35f));
  const float sweep = easeInCubic(window(k, 0.35f, 0.5f));
  const float up = easeInOut(window(k, 0.6f, 1.f));
  st.angle = -0.04f - 0.06f * back + 0.26f * sweep * (1.f - up) + 0.06f * back * up;
  if (u >= 1.f) st.angle = -0.04f;
  // The stick reaches the bag at about a third of the sweep.
  const float k0 = k - SS_TICK_DT / 1.7f;
  if (k0 < 0.42f && k >= 0.42f) bagKick(w, (float)c->facing * 1.1f);
  c->fore_add = 0.12f * sweep * (1.f - up);
  if (u >= 1.f) c->fore_add = 0.f;
}

// It lets go: the stick topples forward off its paw and lands flat on the
// ground (the carried line and the lying stick meet at the end).
void actBearDropStick(World& w, float u) {
  Critter* c = bearOf(w);
  Item& st = w.items[ITEM_STICK];
  if (!c || st.critter != (uint8_t)(w.spine.bear + 1)) return;
  const float ground = c->feet.y + 3.f;
  if (!st.placed) {
    st.placed = true;
    w.spine.v0 = st.pos;
    w.spine.u0 = st.angle;
  }
  st.grip = 0.f;
  st.pos = Vec2{w.spine.v0.x, lerp(w.spine.v0.y, ground - 1.75f, easeInCubic(u))};
  st.angle = lerp(w.spine.u0, 0.25f, u * u);
  if (u >= 1.f) {
    const float sx = (st.facing < 0 ? -1.f : 1.f) * st.squeeze;
    st.critter = 0;
    st.held = false;
    st.placed = false;
    st.down = true;
    st.squeeze = 1.f;
    st.pos = Vec2{w.spine.v0.x + sx * 0.5f * kItemStickLen, ground};
  }
}

// Stands looking up at the bag a long time, then down on all fours.
void actBearStandLook(World& w, float u) {
  Critter* c = bearOf(w);
  if (!c) return;
  if (u <= 0.f) critterSetAction(*c, CA_REAR, 0.6f);
  c->head_add = 0.5f * envelope(u, 0.15f, 0.15f);
  if (u >= 1.f) {
    c->head_add = 0.f;
    critterSetAction(*c, CA_IDLE, 0.9f);
  }
}

// Laid at the start of a loop whose bear swats with a stick but whose
// firewood left none out: a fallen branch right of the bag pine.
void layFallenBranch(World& w) {
  Item& st = w.items[ITEM_STICK];
  if (st.down || st.held) return;
  st.down = true;
  st.facing = -1;
  st.critter = 0;
  // Well left of where it stops to sniff (trunk + 96), so it walks on to it
  // and takes it facing the way it came.
  st.pos = Vec2{trunkX(w) + 20.f, groundAt(w, trunkX(w) + 20.f, kBearTreeY + 6.f)};
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
  w.memory.cairn_n = c.n;   // memory: what the next loop finds
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
    setPose(w, POSE_STAND, 0.9f);
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
      pose(POSE_CROUCH, 0.7f, 1.4f), act(actPackLift, 0.6f),            \
      pose(POSE_CARRY, 0.6f, 0.2f), walk(AT_PACK, 30.f, 16.f, -3.f),    \
      face(LEFT), pose(POSE_CROUCH, 0.6f, 0.4f), act(actPackSetDown, 0.3f), \
      pose(POSE_STAND, 0.6f, 0.2f), walk(AT_TENT_WORK, 30.f), face(LEFT), \
      pose(POSE_CROUCH, 0.6f, 0.f), act(actSpread, 1.6f), pose(POSE_STAND, 0.5f, 0.f)

// Setup, shared end (memory): the mice got in last loop. One more crouch at the
// tent, the hole at its base patched. Skipped when there is no hole.
#define PATCH_HOLE                                                                              \
  skipUnless(tentBitten, 5), go(AT_TENT_WORK, 30.f, -38.f, 5.f), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.2f), \
      act(actPatch, 2.6f), pose(POSE_STAND, 0.6f, 0.3f)

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
    PATCH_HOLE,
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
    PATCH_HOLE,
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
    PATCH_HOLE,
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
    doit(hookDinnerCritters),   // a deer edges in a step a sip; a chipmunk behind the fire
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
    doit(hookDinnerCritters),   // they edge in while the pot heats...
    still(11.0f),
    doit(doDinnerScatter),      // ...and bolt when it boils over
    act(actBoilOver, kBoilS),
    still(0.8f),
    pose(POSE_CROUCH, 0.7f, 0.2f),
    carry(CARRY_STICK),
    act(actPotOff, 2.0f),
    carry(CARRY_NONE),
    pose(POSE_SIT, 0.9f, 0.4f),
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
    doit(hookDinnerCritters),   // a deer edges in a step a bite; a chipmunk behind the fire
    act(actEatFork, kEatForkS),
    still(0.6f),
    doit(doForkDown),
    still(1.0f),
};

// Dinner d (fish_dinner, only after fishing_good): the fish off the fire,
// eaten off the stick in three bites, the bones into the fire, a pat on the stomach.
const Step kDinnerD[] = {
    FROM_REST,
    go(AT_BLOW, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.4f),
    doit(doFishOffFire),
    pose(POSE_STAND, 0.6f, 0.1f),
    TO_SIT,
    doit(hookDinnerCritters),   // a deer edges in a step a bite; a chipmunk behind the fire
    act(actEatFish, kEatFishS),
    still(0.6f),
    act(actTossBones, 1.2f),
    still(0.8f),
    act(actPatStomach, 2.4f),
    still(1.0f),
};

// Food bag, shared: out of the pack, over to the bag branch. Shared end: to
// the tent door, in, and the lantern comes on.
#define BAG_FETCH FROM_REST, PACK_CROUCH, doit(doBagUp), pose(POSE_CARRY, 0.6f, 0.1f)
#define TO_TENT                                                                             \
  go(AT_DOOR, 36.f), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.5f), doit(doShoeOff), still(0.5f), \
      doit(doEnterTent), still(1.6f), doit(doLightOn), still(1.0f)

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

// Bear, shared start: in from the right at a slow walk, a stop to sniff
// toward the tent. The fire burns down all the while (bearUpdate).
#define BEAR_IN doit(doBearEnter), until(bearArrived), still(0.8f), act(actBearSniff, 5.0f), still(0.6f)
#define BEAR_OUT doit(doBearLeave), until(bearGone)

// Bear a: climbs the trunk, reaches for the bag and can't, slides down with a
// thump, sits and looks up, leaves.
const Step kBearA[] = {
    BEAR_IN,
    doit(doBearToTrunk),
    until(bearArrived),
    act(actBearLookUp, 2.0f),
    doit(doBearClimb),
    until(bearArrived),
    still(0.6f),
    act(actBearReachTrunk, 4.4f),
    still(0.5f),
    act(actBearSlide, 0.55f),
    still(0.8f),
    act(actBearSitLook, 5.0f),
    still(0.6f),
    BEAR_OUT,
};

// Bear b: rears and shakes the trunk; the bag swings, cones fall, one on its
// head; it startles and runs.
const Step kBearB[] = {
    BEAR_IN,
    doit(doBearToTrunk),
    until(bearArrived),
    act(actBearLookUp, 2.2f),
    act(actBearSniffTrunk, 2.6f),
    still(0.5f),
    act(actBearRear, 1.3f),
    act(actBearShake, kShakeS),
    act(actBearStartle, 0.9f),
    still(1.2f),
    doit(doBearRunOff),
    until(bearGone),
};

// Bear c: fetches the stick (or a fallen branch), rears, swats the bag twice,
// drops the stick, a long look up, leaves.
const Step kBearC[] = {
    BEAR_IN,
    doit(doBearToStick),
    until(bearArrived),
    act(actBearPickStick, 1.6f),
    doit(doBearToBag),
    until(bearArrived),
    still(0.4f),
    act(actBearRaiseStick, 1.4f),
    act(actBearSwat, kSwatS),
    still(0.3f),
    act(actBearDropStick, 0.5f),
    act(actBearStandLook, 5.5f),
    still(0.6f),
    BEAR_OUT,
};

// Close a: the light goes out, the fire sinks to embers, the smoke stops.
const Step kCloseA[] = {
    still(4.0f), doit(doLightOut), still(1.5f),
    doit(hookTheft),   // the night theft runs beside the rest (spineCritterScenes)
    still(2.0f), doit(doFireDown), still(4.0f),
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
    still(1.0f),
    doit(hookTheft),   // the night theft, once he is back in
    still(3.0f),
    doit(doLightOut),
    still(2.0f),
    doit(doFireDown),
    still(2.5f),
};

// Close c: reading in bed; the light flickers out and back twice first.
const Step kCloseC[] = {
    still(4.0f), act(actFlicker, kFlickerS), still(0.5f),
    doit(hookTheft),   // the night theft runs beside the rest (spineCritterScenes)
    still(2.5f), doit(doFireDown), still(4.0f),
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
#undef PATCH_HOLE
#undef WOOD_TRIP
#undef WOOD_DOWN
#undef FIRE_START
#undef FIRE_BURN
#undef BEAR_IN
#undef BEAR_OUT

struct Script {
  const Step* steps;
  uint8_t n;
};

#define SCRIPT(a) Script{a, N(a)}
#define NO_SCRIPT Script{nullptr, 0}

// Variants per beat, in LoopPlan order. A null entry is a variant not built.
const Script kScripts[SB_COUNT][4] = {
    {SCRIPT(kArrivalA), SCRIPT(kArrivalB), SCRIPT(kArrivalC)},
    {SCRIPT(kSetupA), SCRIPT(kSetupB), SCRIPT(kSetupC)},
    {SCRIPT(kFirewoodA), SCRIPT(kFirewoodB), SCRIPT(kFirewoodC)},
    {SCRIPT(kFireA), SCRIPT(kFireB), SCRIPT(kFireC)},
    {SCRIPT(kDinnerA), SCRIPT(kDinnerB), SCRIPT(kDinnerC), SCRIPT(kDinnerD)},
    {SCRIPT(kFoodbagA), SCRIPT(kFoodbagB), SCRIPT(kFoodbagC)},
    {SCRIPT(kBearA), SCRIPT(kBearB), SCRIPT(kBearC)},
    {SCRIPT(kCloseA), SCRIPT(kCloseB), SCRIPT(kCloseC)},
};

// birds_on_water's script lives in majors.cpp with its birds (its own beat hooks).
const Script kFillerScripts[FILL_COUNT] = {SCRIPT(kFillMugSunset),  SCRIPT(kFillRead),        SCRIPT(kFillCairn),
                                           SCRIPT(kFillSkipStone),  SCRIPT(kFillCloudWatch),  SCRIPT(kFillStreamListen),
                                           NO_SCRIPT};

// Beat seconds per variant: the script plus room for slower walks on some
// seeds (fire pit and rock positions vary). Whole seconds keep the loop whole ticks.
#undef SCRIPT
#undef NO_SCRIPT
#undef N

const uint8_t kDurations[SB_COUNT][4] = {
    {25, 29, 33, 0},    // arrival
    {39, 39, 35, 0},    // setup
    {34, 62, 35, 0},    // firewood (b is two trips)
    {31, 43, 37, 0},    // fire
    {38, 48, 33, 34},   // dinner (b: the visitors come while the pot heats; d: the fish)
    {38, 48, 47, 0},    // foodbag (the walks: pack, bag pine, tent door)
    {58, 42, 59, 0},    // bear (c: with the branch by the pine; see beatSeconds)
    {34, 50, 37, 0},    // close (the theft; the last 2.5 s fade to black)
};

// Bear c fetching the firewood stick from beside the fire walks much further.
constexpr uint8_t kBearStickFarS = 17;
// Setup with the mouse hole to patch (memory): a walk back and a crouch.
constexpr uint8_t kPatchS = 7;

// A beat's seconds in this plan.
int beatSeconds(const LoopPlan& p, int b) {
  const uint8_t v = spineVariant(p, b);
  int s = kDurations[b][v];
  if (b == SB_BEAR && v == 2 && p.firewood == 2) s += kBearStickFarS;
  if (b == SB_SETUP && p.patch) s += kPatchS;
  return s;
}

// The close's fade to black takes its last kFadeS seconds; the script is done by then.
constexpr float kFadeS = 2.5f;

// Filler seconds: the script plus a few. The cairn adds one stone cycle per stone.
const uint8_t kFillerSeconds[FILL_COUNT] = {47, 50, 0, 36, 58, 46, 0};   // birds: birdsFillerSeconds
constexpr int kCairnBaseS = 20, kCairnPerStoneS = 7;

const char* const kBeatNames[SB_COUNT] = {"arrival", "setup", "firewood", "fire",
                                          "dinner",  "foodbag", "bear",   "close"};
const char* const kFillerNames[FILL_COUNT] = {"mug_sunset", "read", "cairn", "skip_stone", "cloud_watch",
                                              "stream_listen", "birds_on_water"};

// Fillers per family (bit = SpineFamily): the evening all of them; the morning
// read, cairn, cloud_watch, stream_listen and birds_on_water; midday all but
// mug_sunset (SPINES_WEATHER_LOOKOUT_SPEC section 1).
constexpr uint8_t kFE = 1u << SF_EVENING, kFM = 1u << SF_MORNING, kFD = 1u << SF_MIDDAY;
const uint8_t kFillerFamilies[FILL_COUNT] = {
    kFE,                          // mug_sunset
    kFE | kFM | kFD,              // read
    kFE | kFM | kFD,              // cairn
    kFE | kFD,                    // skip_stone
    kFE | kFM | kFD,              // cloud_watch
    kFE | kFM | kFD,              // stream_listen
    kFE | kFM | kFD,              // birds_on_water
};

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
    case AT_M0:
    case AT_M1:
    case AT_M2:
    case AT_M3: return w.major.p[at - AT_M0];
    case AT_GUY_PEG: return c.guy_peg;
    case AT_FAM0:
    case AT_FAM1: return w.spine.fp[at - AT_FAM0];
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
    case OP_POSE: setPose(w, (PoseId)s.a, s.x); break;
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
    case OP_UNTIL: return !s.pred || s.pred(w);
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
    if (s.op == OP_SKIP) {     // steps that apply only sometimes, decided on the spot
      r.i = (uint8_t)(r.i + 1 + ((s.pred && s.pred(w)) ? 0 : s.a));
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
  // Right after the build-up: a bear that will want a stick and a firewood
  // trip that brings none back get a fallen branch by the bag pine.
  if (B == SB_ARRIVAL && w.plan.bear == 2 && w.plan.firewood != 2) layFallenBranch(w);
  // Memory: last loop's cairn is still there, one stone shorter.
  if (B == SB_ARRIVAL && w.cairn.n == 0) {
    w.cairn.n = w.plan.cairn_start;
    w.cairn.pop = 1.f;
    w.memory.cairn_n = w.cairn.n;
  }
  // Beats that own critters keep the minors away while they run.
  if (B == SB_DINNER || B == SB_BEAR || B == SB_CLOSE) w.busy = true;
  startScript(w, kScripts[B][spineVariant(w.plan, B)]);
}

template <int F>
void fillerBegin(SceneCtx& ctx) {
  startScript(*ctx.w, kFillerScripts[F]);
}

void beatUpdate(SceneCtx& ctx, float, float) { runScript(*ctx.w); }

// Bear: the script, the fire burning down over the beat while he is in the
// tent, and his shadow in the tent following the bear's x with a lag,
// trembling as `fear` says; calm again once the bear has gone.
void bearUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  runScript(w);
  if (w.fire.state == FIRE_LIT) w.fire.fuel = lerp(1.f, 0.45f, easeInOut(t));
  SpineState& sp = w.spine;
  Tent& tent = w.tent;
  if (!bearHeld(w)) sp.bear = -1;   // released: the slot is no longer the bear's
  const bool here = sp.bear >= 0 && w.critters[sp.bear].visible;
  float to = -4.f;   // where the shadow sits at rest (doLightOn)
  if (here) {
    const float bx = w.critters[sp.bear].feet.x;
    to = minf(9.f, maxf(-14.f, -14.f + (bx - 470.f) * 0.065f));
  } else {
    sp.fear = 0.f;
  }
  sp.shadow_v += (to - sp.shadow_v) * (SS_TICK_DT / 0.9f);
  tent.shadow_x = floorf(sp.shadow_v + 0.5f);
  // Fear: frozen at once, trembling builds in about half a second, calms in two.
  if (sp.fear > 0.f && tent.shadow_jitter < 0.05f) tent.shadow_jitter = minf(sp.fear, 0.05f);   // freezes at once
  const float rate = sp.fear > tent.shadow_jitter ? SS_TICK_DT / 0.5f : SS_TICK_DT / 2.f;
  tent.shadow_jitter += (sp.fear - tent.shadow_jitter) * minf(1.f, rate);
  if (sp.fear == 0.f && tent.shadow_jitter < 0.03f) tent.shadow_jitter = 0.f;
}

// Close: the script, and the frame fading to black over the last kFadeS
// seconds, reaching 1 exactly on the beat's last tick (t = 1).
void closeUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  w.tent.shadow_jitter = 0.f;   // the bear has long gone
  runScript(w);
  const float d = (float)beatSeconds(w.plan, SB_CLOSE);
  const float fade = easeInOut(window(t, 1.f - kFadeS / d, 1.f));
  // The script (and the theft: every critter out of the frame) must be done
  // before the fade starts; the beat's end comes after the rollover has reset
  // the count, so it is counted here.
  const ScriptRun& r = w.spine.run;
  const bool busy = (r.steps && r.i < r.n) || directorCount(w) > 0;
  if (w.fade == 0.f && fade > 0.f && busy && w.spine.overruns < 255) ++w.spine.overruns;
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
  w.busy = false;
}

// A beat that owns critters: at its end, any of its critters still at work
// (one that overran, a dinner guest he never stirred) walks out by the nearer
// edge, so none outlives the beat. Scattered ones are already on their way.
template <int B>
void beatEndOwned(SceneCtx& ctx) {
  beatEnd(ctx);
  World& w = *ctx.w;
  const uint8_t tag = B == SB_DINNER ? CT_DINNER : (B == SB_BEAR ? CT_BEAR : CT_THIEF);
  for (int i = 0; i < CRITTER_MAX; ++i) {
    if (!directorOwns(w, i, tag)) continue;
    const Critter& c = w.critters[i];
    const int side = c.feet.x < 0.5f * (float)SS_WIDTH ? -1 : 1;
    const bool big = c.kind == CR_DEER || c.kind == CR_BEAR || c.kind == CR_RACCOON;
    directorExit(w, i, side, big ? 45.f : 90.f, big ? CA_MOVE : CA_RUN);
  }
}

typedef void (*BeginFn)(SceneCtx&);
typedef void (*UpdateFn)(SceneCtx&, float, float);
const BeginFn kBegin[SB_COUNT] = {beatBegin<0>, beatBegin<1>, beatBegin<2>, beatBegin<3>,
                                  beatBegin<4>, beatBegin<5>, beatBegin<6>, beatBegin<7>};
const UpdateFn kUpdate[SB_COUNT] = {beatUpdate, beatUpdate, beatUpdate, beatUpdate,
                                    beatUpdate, beatUpdate, bearUpdate, closeUpdate};
typedef void (*EndFn)(SceneCtx&);
const EndFn kEnd[SB_COUNT] = {beatEnd, beatEnd, beatEnd, beatEnd, beatEndOwned<SB_DINNER>,
                              beatEnd, beatEndOwned<SB_BEAR>, beatEndOwned<SB_CLOSE>};
const BeginFn kFillerBegin[FILL_COUNT] = {fillerBegin<0>, fillerBegin<1>, fillerBegin<2>, fillerBegin<3>,
                                          fillerBegin<4>, fillerBegin<5>, birdsFillerBegin};
static_assert(SB_COUNT == 8 && FILL_COUNT == 7, "extend kBegin, kUpdate and kFillerBegin");

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

void scriptStart(World& w, const Step* steps, uint8_t n) { startScript(w, Script{steps, n}); }
void scriptRun(World& w) { runScript(w); }
bool scriptDone(const World& w) { return !w.spine.run.steps || w.spine.run.i >= w.spine.run.n; }
Vec2 scriptAnchor(const World& w, uint8_t at) { return anchorPos(w, at); }
void scriptSetPose(World& w, PoseId p, float seconds) { setPose(w, p, seconds); }
void scriptFxRest(World& w) { fxRest(w); }
void scriptSetArm(World& w, int i, float k, float sh, float el) { setArm(w, i, k, sh, el); }
float scriptEnvelope(float u, float a, float b) { return envelope(u, a, b); }
float scriptGroundAt(const World& w, float x, float want) { return groundAt(w, x, want); }
void scriptSetDown(World& w, ItemId id, Vec2 at) { setDown(w, id, at); }

int spineVariantCount(int beat) {
  if (beat < 0 || beat >= SB_COUNT) return 0;
  int n = 0;
  while (n < 3 && kScripts[beat][n].steps) ++n;   // (dinner's 4th is placed, not drawn)
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
  if (f == FILL_BIRDS_ON_WATER) return birdsFillerSeconds();
  return kFillerSeconds[f];
}

bool spineFillerAllowed(int family, int f) {
  return f >= 0 && f < FILL_COUNT && family >= 0 && family < SF_COUNT && ((kFillerFamilies[f] >> family) & 1u);
}

Beat spineFillerBeat(const LoopPlan& p, int f) {
  if (f == FILL_BIRDS_ON_WATER)
    return Beat{kFillerNames[f], (float)spineFillerSeconds(p, f), birdsFillerBegin, birdsFillerUpdate, birdsFillerEnd};
  return Beat{kFillerNames[f], (float)spineFillerSeconds(p, f), kFillerBegin[f], beatUpdate, beatEnd};
}

const char* spineFamilyName(int family) {
  static const char* const kNames[SF_COUNT] = {"evening", "morning", "midday", "lookout"};
  return family >= 0 && family < SF_COUNT ? kNames[family] : "?";
}

uint8_t spineFamilyFor(const Inputs& in) {
  uint8_t f = SF_EVENING;
  if (in.spine_force >= 1 && in.spine_force <= SF_COUNT) {
    f = (uint8_t)(in.spine_force - 1);
  } else if (in.lookout_window) {
    f = SF_LOOKOUT;
  } else if (!in.night_mode) {
    const float m = in.mood - 6.f * floorf(in.mood / 6.f);   // wrapped into [0, 6)
    if (m >= 0.5f && m < 2.5f) f = SF_MORNING;
    else if (m >= 2.5f && m < 3.5f) f = SF_MIDDAY;
  }
  // Not built yet: they play the evening until they are.
  if (f == SF_MIDDAY || f == SF_LOOKOUT) f = SF_EVENING;
  return f;
}

namespace {

// Whole seconds of everything but the pauses.
int busySeconds(const LoopPlan& p) {
  int s = (int)(SS_BUILD_SECONDS + 0.5f);
  for (int b = 0; b < SB_COUNT; ++b) s += beatSeconds(p, b);
  for (int i = 0; i < p.n_fillers; ++i) s += spineFillerSeconds(p, p.filler[i]);
  for (int i = 0; i < p.n_majors; ++i)
    s += majorSeconds(p.major[i], p.major_var[i]) + p.major_pre_s[i] + p.major_post_s[i];
  return s;
}

int pauseSeconds(const LoopPlan& p) { return spinePauseSeconds(p, SB_COUNT - 1); }

// The pause slots in use, in timeline order, as pointers into the plan.
int pauseSlots(LoopPlan& p, int beat_pauses, uint8_t* slot[SB_COUNT + SPINE_FILLER_MAX]) {
  int n = 0;
  for (int b = 0; b < beat_pauses; ++b) slot[n++] = &p.pause_s[b];
  for (int i = 0; i < p.n_fillers; ++i) slot[n++] = &p.filler_pause_s[i];
  return n;
}

// The evening's filler slots in play order: after dinner; then fire and
// dinner; a third after dinner too; with four, the first goes after setup.
void eveningFillerSlots(LoopPlan& p) {
  static const uint8_t kSlots[SPINE_FILLER_MAX + 1][SPINE_FILLER_MAX] = {
      {0, 0, 0, 0}, {SB_DINNER, 0, 0, 0}, {SB_FIRE, SB_DINNER, 0, 0}, {SB_FIRE, SB_DINNER, SB_DINNER, 0},
      {SB_SETUP, SB_FIRE, SB_DINNER, SB_DINNER}};
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) p.filler_after[i] = i < p.n_fillers ? kSlots[p.n_fillers][i] : 0;
}

const FitSpec kEveningFit = {busySeconds, SB_COUNT - 1, eveningFillerSlots};

// Aim inside the policy range with a margin, so no rounding lands on its edge.
constexpr int kAimMin = SS_LOOP_MIN_SECONDS + 10, kAimMax = SS_LOOP_MAX_SECONDS - 10;
constexpr uint8_t kPauseMin = 8, kPauseMax = 20;

// The evening's plan (the stage 4 to 6 spine; approved, do not change).
void eveningPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem) {
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

  // Fillers: 2 to 4 drawn, weighted, no repeats (the length policy below may
  // drop some). The sunset mug is likelier from golden hour on (mood 3.5 and up).
  float wt[FILL_COUNT] = {1.f, 1.f, 1.2f, 1.f, 1.f, 1.f, 1.f};
  if (in.mood >= 3.5f) wt[FILL_MUG_SUNSET] = 3.f;
  p.n_fillers = (uint8_t)rng.rangeInt(2, SPINE_FILLER_MAX);
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) {
    p.filler[i] = 0;
    p.filler_after[i] = 0;
    p.filler_pause_s[i] = 0;
  }
  for (int i = 0; i < p.n_fillers && i < SPINE_FILLER_MAX; ++i) {
    const int f = rng.weighted(wt, FILL_COUNT);
    p.filler[i] = (uint8_t)f;
    wt[f] = 0.f;
  }
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) {
    const uint8_t s = (uint8_t)rng.rangeInt(kPauseMin, kPauseMax);
    if (i < p.n_fillers) p.filler_pause_s[i] = s;
  }
  // Memory: the cairn stands from loop start one stone shorter than it was
  // left, and the filler adds to it (at least one stone, up to the top).
  p.cairn_start = mem.cairn_n > 0 ? (uint8_t)(mem.cairn_n - 1) : 0;
  const int add = rng.rangeInt(4, 6);
  p.cairn_add = (uint8_t)(add < CAIRN_MAX - p.cairn_start ? add : CAIRN_MAX - p.cairn_start);
  p.patch = mem.mouse_hole;
  p.skip_n = rng.chance(0.35f) ? 4 : 3;
  p.steam_n = (uint8_t)rng.rangeInt(3, 5);

  static const MajorSlots kEveningSlots = {SF_EVENING, {SB_SETUP, SB_FIRE, SB_DINNER, SB_FOODBAG}, 1 | 2 | 4 | 8};
  majorsPlan(p, rng, in, mem, kEveningSlots);
  // fishing_good leaves a fish roasting over the fire: dinner is the fish.
  auto hasMajor = [&p](int id) {
    for (int i = 0; i < p.n_majors; ++i)
      if (p.major[i] == id) return true;
    return false;
  };
  if (hasMajor(MJ_FISHING_GOOD)) p.dinner = SPINE_DINNER_FISH;

  spineFitLength(p, mem, kEveningFit);
}

}  // namespace

int spinePauseSeconds(const LoopPlan& p, int beat_pauses) {
  int s = 0;
  for (int b = 0; b < beat_pauses; ++b) s += p.pause_s[b];
  for (int i = 0; i < p.n_fillers; ++i) s += p.filler_pause_s[i];
  return s;
}

void spineFitLength(LoopPlan& p, Memory& mem, const FitSpec& f) {
  // Loop length policy. Squeeze the pauses (round robin, a second at a time,
  // down to 8 s); while still too long, drop fillers down to one, majors down
  // to two, the last filler, then shorten the still beats round the majors;
  // then stretch the pauses (up to 20 s) until the loop is long enough.
  f.filler_slots(p);
  auto total = [&p, &f]() { return f.busy(p) + spinePauseSeconds(p, f.beat_pauses); };
  uint8_t* slot[SB_COUNT + SPINE_FILLER_MAX];
  int ns = pauseSlots(p, f.beat_pauses, slot);
  for (int guard = 0, k = 0; guard < 400 && total() > kAimMax; ++guard, k = (k + 1) % ns)
    if (*slot[k] > kPauseMin) --*slot[k];
  auto dropFiller = [&]() {
    --p.n_fillers;
    p.filler[p.n_fillers] = 0;
    p.filler_after[p.n_fillers] = 0;
    p.filler_pause_s[p.n_fillers] = 0;
    f.filler_slots(p);
  };
  while (total() > kAimMax && p.n_fillers > 1) dropFiller();
  while (total() > kAimMax && p.n_majors > 2) {
    // Drop the last major, or an earlier one if that leaves a set of majors
    // one of the last loops played (memory) and another choice does not.
    uint16_t all = 0;
    for (int i = 0; i < p.n_majors; ++i) all = (uint16_t)(all | (1u << p.major[i]));
    // fishing_good stays (dinner is planned round it).
    int drop = p.major[p.n_majors - 1] == MJ_FISHING_GOOD ? p.n_majors - 2 : p.n_majors - 1;
    for (int i = p.n_majors - 1; i >= 0; --i)
      if (p.major[i] != MJ_FISHING_GOOD && !memoryRecentSet(mem, (uint16_t)(all & ~(1u << p.major[i])))) {
        drop = i;
        break;
      }
    for (int i = drop; i + 1 < p.n_majors; ++i) {
      p.major[i] = p.major[i + 1];
      p.major_after[i] = p.major_after[i + 1];
      p.major_var[i] = p.major_var[i + 1];
      p.major_pre_s[i] = p.major_pre_s[i + 1];
      p.major_post_s[i] = p.major_post_s[i + 1];
    }
    --p.n_majors;
    p.major[p.n_majors] = p.major_after[p.n_majors] = p.major_var[p.n_majors] = 0;
    p.major_pre_s[p.n_majors] = p.major_post_s[p.n_majors] = 0;
  }
  while (total() > kAimMax && p.n_fillers > 0) dropFiller();
  for (int guard = 0, k = 0; guard < 64 && total() > kAimMax && p.n_majors > 0; ++guard, k = (k + 1) % (2 * p.n_majors)) {
    uint8_t& st = (k & 1) ? p.major_post_s[k >> 1] : p.major_pre_s[k >> 1];
    if (st > 4) --st;
  }
  ns = pauseSlots(p, f.beat_pauses, slot);
  for (int guard = 0, k = 0; guard < 400 && total() < kAimMin; ++guard, k = (k + 1) % ns)
    if (*slot[k] < kPauseMax) ++*slot[k];
  uint16_t set = 0;
  for (int i = 0; i < p.n_majors; ++i) {
    memoryPushMajor(mem, p.major[i]);
    set = (uint16_t)(set | (1u << p.major[i]));
  }
  memoryPushSet(mem, set);
  mem.last_n = p.n_majors;
}

void spinePlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem) {
  if (p.family == SF_MORNING) {
    morningPlan(p, rng, in, mem);
  } else {
    p.family = SF_EVENING;
    eveningPlan(p, rng, in, mem);
  }
  mem.last_family = p.family;
  for (int i = 0; i < 8; ++i) mem.last_fvar[i] = p.fvar[i];
}

void spineBeginLoop(World& w) {
  if (w.plan.family == SF_MORNING) morningLayout(w);
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
  c.bag_work = Vec2{(float)(w.land.camp_x - LS_BRANCH_LEN) - 20.f, 356.f};   // left of the tip, facing the tree
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
  SpineState& sp = w.spine;
  sp.bear = sp.deer = sp.chip = sp.thief = -1;
  sp.bites = sp.deer_steps = sp.chip_stage = sp.thief_stage = 0;
  sp.bite_up = false;
  sp.deer_t = sp.chip_t = sp.thief_t = 0.f;
  sp.shadow_v = 0.f;
  sp.fear = 0.f;
  sp.bag_v = 0.f;
  sp.bag_osc = false;
  sp.fp[0] = sp.fp[1] = Vec2{0.f, 0.f};
  sp.fcrit = -1;
  sp.fstage = 0;
  sp.ft = 0.f;
}

void spineCritterScenes(World& w) {
  deerTick(w);
  chipTick(w);
  thiefTick(w);
  if (w.plan.family == SF_MORNING) morningTick(w);
  // The bag's damped swing once something hit it (about 1 swing a second,
  // settling over a few seconds, like the foodbag beat's settle).
  SpineState& sp = w.spine;
  FoodBag& b = w.bag;
  if (sp.bag_osc && b.state == BAG_HUNG) {
    const float om = 6.2831853f * 0.95f, zeta = 0.14f;
    const float acc = -om * om * b.swing - 2.f * zeta * om * sp.bag_v;
    sp.bag_v += acc * SS_TICK_DT;
    b.swing += sp.bag_v * SS_TICK_DT;
    b.swing = minf(0.7f, maxf(-0.7f, b.swing));
    if (fabsf(b.swing) < 0.004f && fabsf(sp.bag_v) < 0.03f) {
      b.swing = 0.f;
      sp.bag_v = 0.f;
      sp.bag_osc = false;
    }
  }
}

void spineBuildTimeline(Timeline& tl, const LoopPlan& p) {
  if (p.family == SF_MORNING) return morningBuildTimeline(tl, p);
  tl.add(kBuildBeat);
  for (int b = 0; b < SB_COUNT; ++b) {
    tl.add(Beat{kBeatNames[b], (float)beatSeconds(p, b), kBegin[b], kUpdate[b], kEnd[b]});
    if (b == SB_CLOSE) break;   // the loop ends here
    tl.add(Beat{"pause", (float)p.pause_s[b], nullptr, nullptr, nullptr});
    for (int i = 0; i < p.n_fillers; ++i) {
      if (p.filler_after[i] != b) continue;
      tl.add(spineFillerBeat(p, p.filler[i]));
      tl.add(Beat{"pause", (float)p.filler_pause_s[i], nullptr, nullptr, nullptr});
    }
    for (int i = 0; i < p.n_majors; ++i) {
      if (p.major_after[i] != b) continue;
      tl.add(Beat{"still", (float)p.major_pre_s[i], nullptr, nullptr, nullptr});
      tl.add(majorBeat(p, i));
      tl.add(Beat{"still", (float)p.major_post_s[i], nullptr, nullptr, nullptr});
    }
  }
}

uint32_t spineLoopTicks(const LoopPlan& p) {
  if (p.family == SF_MORNING) return morningLoopTicks(p);
  // Every beat is whole seconds except the build, which is whole ticks.
  const uint32_t build = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  const int rest = busySeconds(p) - (int)(SS_BUILD_SECONDS + 0.5f) + pauseSeconds(p);
  return build + (uint32_t)rest * SS_TICK_HZ;
}

int spineFormatPlan(const LoopPlan& p, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  if (p.family == SF_MORNING) return morningFormatPlan(p, buf, n);
  int k = snprintf(buf, n, "family=evening arrival=%d setup=%d firewood=%d fire=%d dinner=%d foodbag=%d bear=%d close=%d fillers=",
                   p.arrival, p.setup, p.firewood, p.fire, p.dinner, p.foodbag, p.bear, p.close);
  if (k < 0 || (size_t)k >= n) return k;
  auto put = [&](const char* str) {
    if (k < 0 || (size_t)k >= n) return;
    const int m = snprintf(buf + k, n - (size_t)k, "%s", str);
    if (m > 0) k += m;
  };
  if (p.n_fillers == 0) put("none");
  for (int i = 0; i < p.n_fillers && i < SPINE_FILLER_MAX; ++i) {
    if (i) put(",");
    put(spineFillerName(p.filler[i]));
  }
  put(" majors=");
  if (p.n_majors == 0) put("none");
  for (int i = 0; i < p.n_majors && i < SPINE_MAJOR_MAX; ++i) {
    if (i) put(",");
    put(majorName(p.major[i]));
  }
  return k;
}

}  // namespace ss
