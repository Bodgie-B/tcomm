// The plinking as a surprise major. See day_plink.h.
//
// The script is the hiker's half: the walks, the crouches, the arms that lift the cans on the stick,
// take the rifle apart and put it together, load and unload. The module's own step (plinkStep from
// the hands this tick) and the pose bend (kneel, shoulder, shrug) are the World's (dayStep, dayPose in
// day.cpp). The beat's tick (plinkMajorTick) reads the module's events: the startle at the first shot, the
// bag's line cut by the bullet and the bag's fall.
// No random draws here: each shot's two values come from DayPlink::rng, seeded from the layout.
#include "day_plink.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "day.h"
#include "director.h"
#include "housedogs.h"
#include "landscape.h"
#include "plink.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

MajorState& M(World& w) { return w.major; }
DayPlink& D(World& w) { return w.day.plink; }
PlinkState& P(World& w) { return w.day.plink.p; }
bool live(const World& w) { return w.day.kind == DAY_PLINK; }
float ramp(float u, float a, float b) { return easeInOut(window(u, a, b)); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- tuning (PROPOSED; how it looks is UNVERIFIED) ---------------------------------------------------------

constexpr float kLane = 356.f;            // the ground line under the branch
constexpr float kPlaceLeft = 22.f;        // he stands this far left of the can he lifts onto the branch
constexpr float kStickCarry = 2.9f;       // the stick held upright at his side (rig angle: up, leaning forward a little)
constexpr float kBagDropY = 4.f;          // the dropped bag lies this far above his lane
constexpr float kGravity = 650.f;

uint64_t seedOf(const World& w) {
  return ((uint64_t)(uint32_t)(w.fire.pos.x * 16.f) << 22) ^ (uint64_t)(uint32_t)(w.fire.pos.y * 16.f) ^ 0x91C7000ull;
}

float canX(World& w, int i) { return plinkCanSpot(P(w), i).x; }
float cx(const World& w) { return (float)w.land.camp_x; }

// ---- helpers the script's steps use -------------------------------------------------------------------------

void setPlaceMark(World& w) {
  const int i = M(w).count < PLINK_CANS ? M(w).count : PLINK_CANS - 1;
  M(w).p[3] = Vec2{canX(w, i) - kPlaceLeft, kLane};
}
void doStartPlace(World& w, float) {
  M(w).count = 0;
  setPlaceMark(w);
}
void doPlaced(World& w, float) {
  if (M(w).count < PLINK_CANS) ++M(w).count;
  if (M(w).count < PLINK_CANS) setPlaceMark(w);
}
bool morePlaces(const World& w) { return w.major.count < PLINK_CANS; }

void setPickMark(World& w) {
  const int k = M(w).stage[0] < PLINK_PAIRS ? M(w).stage[0] : PLINK_PAIRS - 1;
  M(w).p[3] = Vec2{P(w).pair_x[k] - 14.f, kLane};
}
void doStartPick(World& w, float) {
  M(w).stage[0] = 0;
  setPickMark(w);
}
void doPicked(World& w, float) {
  if (M(w).stage[0] < PLINK_PAIRS) ++M(w).stage[0];
  if (M(w).stage[0] < PLINK_PAIRS) setPickMark(w);
}
bool morePicks(const World& w) { return w.major.stage[0] < PLINK_PAIRS; }

// ---- his actions ----------------------------------------------------------------------------------------------

void crouchAt0(World& w, float u) {
  if (u <= 0.f) scriptSetPose(w, POSE_CROUCH, 0.6f);
}

// A crouch at the wood pile, and the stick is in his near hand, upright.
void actTakeStick(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f) plinkStick(P(w), PI_HAND, kStickCarry);
  if (u >= 1.f) scriptFxRest(w);
}
// A crouch at the pack, and the sack of cans is in his far hand.
void actCansOut(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f && P(w).sack != PI_HAND) {
    plinkSack(P(w), PI_HAND);
    P(w).sack_n = PLINK_CANS;
  }
  if (u >= 1.f) scriptFxRest(w);
}

// One can onto the branch: the stick comes down level in front of him, a can is set on its fork, the
// arm goes up with the stick (a little lean back), the can is set down on the bark, the stick comes away.
void actPlaceCan(World& w, float u) {
  PlinkState& p = P(w);
  const int i = M(w).count < PLINK_CANS ? M(w).count : PLINK_CANS - 1;
  const float low = ramp(u, 0.f, 0.15f), up = ramp(u, 0.15f, 0.7f), down = ramp(u, 0.82f, 1.f);
  if (u <= 0.f && p.can[i].mode == CAN_OFF) {   // out of the sack, in his far hand
    plinkSetCan(p, i, CAN_HELD);
    if (p.sack_n > 0) --p.sack_n;
  }
  if (u >= 0.15f && p.can[i].mode == CAN_HELD) plinkSetCan(p, i, CAN_TIP);   // set on the fork
  if (u >= 0.72f && p.can[i].mode == CAN_TIP) plinkSetCanOn(p, i, 1.f);       // set down on the bark
  const Hiker& h = w.hiker;
  // The stick's angle at the top: from where his hand will be (the arm up) to the can's place.
  const Vec2 hand{h.feet.x + (float)h.facing * 13.f, h.feet.y - 94.f};
  const Vec2 spot = plinkCanSpot(p, i);
  const float tgt = clampf(plinkAngle(((float)h.facing) * (spot.x - hand.x), spot.y - hand.y), 2.3f, 3.0f);
  float sh = lerp(0.3f, 1.15f, low);
  sh = lerp(sh, 2.55f, up);
  sh = lerp(sh, 0.3f, down);
  scriptSetArm(w, 0, 1.f, sh, 0.15f);
  scriptSetArm(w, 1, low * (1.f - ramp(u, 0.15f, 0.3f)), 1.15f, 0.3f);
  float ang = lerp(kStickCarry, 1.95f, low);
  ang = lerp(ang, tgt, up);
  ang = lerp(ang, kStickCarry, down);
  plinkStick(p, PI_HAND, ang);
  w.fx.lean = -0.05f * up * (1.f - down);
  if (u >= 1.f) {
    scriptFxRest(w);
    plinkStick(p, PI_HAND, kStickCarry);
  }
}

}  // namespace

// Stick and sack laid down at the pack, the two halves of the rifle out of it.
void plinkActRifleOut(World& w, float u) {
  PlinkState& p = P(w);
  crouchAt0(w, u);
  if (u >= 0.3f && p.stick != PI_DOWN) {
    plinkLay(p, M(w).p[1]);
    plinkStick(p, PI_DOWN, kStickCarry);
    plinkSack(p, PI_DOWN);
  }
  if (u >= 0.6f) {
    plinkRifle(p, true, 0.f);
    const float k = ramp(u, 0.6f, 0.85f);
    scriptSetArm(w, 0, k, 0.55f, 0.35f);
    scriptSetArm(w, 1, k, 0.55f, 0.35f);
  }
  if (u >= 1.f) scriptFxRest(w);
}

// The barrel goes into the stock with a twist of the far wrist (two little rocks), then it is one rifle.
void plinkActAssemble(World& w, float u) {
  PlinkState& p = P(w);
  const float come = ramp(u, 0.0f, 0.3f) * (1.f - ramp(u, 0.8f, 1.f));
  scriptSetArm(w, 0, come, 0.95f, 0.8f);
  scriptSetArm(w, 1, come, 0.95f, 0.8f + 0.18f * sinT(u * 2.f) * ramp(u, 0.3f, 0.5f));
  plinkRifle(p, true, ramp(u, 0.25f, 0.7f));
  if (u >= 1.f) {
    plinkRifle(p, true, 1.f);
    scriptFxRest(w);
  }
}

namespace {

void actKneel(World& w, float u) {
  if (u <= 0.f) plinkKneel(P(w), true);
}

}  // namespace

// The far hand to his hip pocket and back with the magazine, up under the action, one tug of the charging handle.
void plinkActLoad(World& w, float u) {
  PlinkState& p = P(w);
  const float pocket = ramp(u, 0.f, 0.2f) * (1.f - ramp(u, 0.3f, 0.45f));
  const float under = ramp(u, 0.3f, 0.5f) * (1.f - ramp(u, 0.65f, 0.75f));
  const float tug = ramp(u, 0.72f, 0.82f) * (1.f - ramp(u, 0.86f, 0.96f));
  scriptSetArm(w, 1, clampf(pocket + under + tug, 0.f, 1.f), pocket > under + tug ? -0.25f : (tug > under ? 1.15f : 0.7f),
               pocket > under + tug ? 0.8f : (tug > under ? 0.5f : 0.9f));
  if (u >= 0.3f && p.mag == MAG_NONE) plinkMag(p, MAG_HAND);
  if (u >= 0.6f && p.mag == MAG_HAND) {
    plinkMag(p, MAG_IN);
    p.rounds = 8;
  }
  if (u >= 0.9f) plinkLoad(p);
  if (u >= 1.f) scriptFxRest(w);
}

namespace {

void doString(World& w, float) {
  PlinkState& p = P(w);
  plinkQueueClear(p);
  const int v = M(w).var & 3;
  if (v == 1) {   // B: five down, then the last one will not
    static const uint8_t k[10] = {SK_HIT, SK_HIT, SK_HIT, SK_HIT, SK_HIT, SK_MISS_U, SK_MISS_L, SK_CHECK, SK_MISS_R, SK_SHRUG};
    static const uint8_t c[10] = {5, 4, 3, 2, 1, 0, 0, 0, 0, 0};
    for (int i = 0; i < 10; ++i) plinkQueue(p, k[i], c[i]);
  } else if (v == 2) {   // C: five down, the bag's line, a scratch of the head, the last can
    static const uint8_t k[8] = {SK_HIT, SK_HIT, SK_HIT, SK_HIT, SK_HIT, SK_BAG, SK_SCRATCH, SK_HIT};
    static const uint8_t c[8] = {5, 4, 3, 2, 1, 0, 0, 0};
    for (int i = 0; i < 8; ++i) plinkQueue(p, k[i], c[i]);
  } else {   // A: a clean run (the third can only hops, teeters and topples)
    static const uint8_t k[6] = {SK_HIT, SK_HIT, SK_WEAK, SK_HIT, SK_HIT, SK_HIT};
    static const uint8_t c[6] = {5, 4, 3, 2, 1, 0};
    for (int i = 0; i < 6; ++i) plinkQueue(p, k[i], c[i]);
  }
  plinkStart(p);
}
bool stringDone(const World& w) { return live(w) && plinkStringDone(w.day.plink.p); }

}  // namespace

// The magazine out and into his pocket, the charging handle back and held, a look into the action: only then does he stand.
void plinkActUnload(World& w, float u) {
  PlinkState& p = P(w);
  const float pocket = ramp(u, 0.05f, 0.25f) * (1.f - ramp(u, 0.45f, 0.55f));
  const float hold = ramp(u, 0.55f, 0.65f) * (1.f - ramp(u, 0.9f, 1.f));
  scriptSetArm(w, 1, clampf(pocket + hold, 0.f, 1.f), pocket >= hold ? -0.2f : 1.1f, pocket >= hold ? 0.8f : 0.6f);
  if (u >= 0.2f && p.mag == MAG_IN) plinkMag(p, MAG_HAND);   // the magazine leaves the rifle
  if (u >= 0.5f && p.mag == MAG_HAND) plinkMag(p, MAG_NONE);   // and goes into his pocket
  if (u >= 0.55f) plinkUnload(p);                              // the chamber is clear
  w.fx.head = 0.3f * ramp(u, 0.6f, 0.72f) * (1.f - ramp(u, 0.88f, 1.f));
  if (u >= 1.f) scriptFxRest(w);
}

namespace {
void actStand(World& w, float u) {
  if (u <= 0.f) plinkKneel(P(w), false);
}

}  // namespace

// A crouch at the pack: the barrel out of the stock, both halves into the pack, the stick and the sack in his hands.
void plinkActStow(World& w, float u) {
  PlinkState& p = P(w);
  crouchAt0(w, u);
  const float k = ramp(u, 0.1f, 0.3f) * (1.f - ramp(u, 0.5f, 0.6f));
  scriptSetArm(w, 0, k, 0.55f, 0.35f);
  scriptSetArm(w, 1, k, 0.55f, 0.35f);
  if (u >= 0.1f && u < 0.6f) plinkRifle(p, true, 1.f - ramp(u, 0.15f, 0.45f));
  if (u >= 0.6f) plinkRifle(p, false, 0.f);
  if (u >= 0.8f && p.stick == PI_DOWN) {
    plinkStick(p, PI_HAND, kStickCarry);
    plinkSack(p, PI_HAND);
  }
  if (u >= 1.f) scriptFxRest(w);
}

namespace {

// A crouch, and the cans within his reach go into the sack.
void actSweep(World& w, float u) {
  crouchAt0(w, u);
  PlinkState& p = P(w);
  const int k = M(w).stage[0] < PLINK_PAIRS ? M(w).stage[0] : PLINK_PAIRS - 1;
  if (u >= 0.5f) plinkSweep(p, p.pair_x[k], 22.f);
  if (u >= 1.f) scriptFxRest(w);
}

// B: the stick up to the stubborn can, a nudge, it falls; it bounces off his hat; he picks it up.
void actPoke(World& w, float u) {
  PlinkState& p = P(w);
  const Hiker& h = w.hiker;
  const float up = ramp(u, 0.f, 0.4f), down = ramp(u, 0.8f, 1.f);
  const Vec2 hand{h.feet.x + (float)h.facing * 13.f, h.feet.y - 94.f};
  const Vec2 spot = plinkCanSpot(p, 0);
  const float tgt = clampf(plinkAngle(((float)h.facing) * (spot.x - hand.x), spot.y - hand.y), 2.3f, 3.0f);
  scriptSetArm(w, 0, 1.f, lerp(lerp(0.3f, 2.55f, up), 0.3f, down), 0.15f);
  plinkStick(p, PI_HAND, lerp(lerp(kStickCarry, tgt, up), kStickCarry, down));
  w.fx.lean = -0.05f * up * (1.f - down);
  if (u >= 0.5f && p.can[0].mode == CAN_ON) plinkPoke(p, 0, h.feet.x + 2.f);
  w.fx.head = 0.16f * ramp(u, 0.64f, 0.7f) * (1.f - ramp(u, 0.72f, 0.8f));   // it bonks off the hat
  if (u >= 1.f) {
    scriptFxRest(w);
    plinkStick(p, PI_HAND, kStickCarry);
  }
}
void actPickOne(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f) plinkSweep(P(w), w.hiker.feet.x + 2.f, 22.f);
  if (u >= 1.f) scriptFxRest(w);
}

// C: a crouch, and the food within his reach is in his arms (and gone).
void actGatherA(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f) plinkTakeFood(P(w), w.hiker.feet.x + 16.f, 24.f);
  if (u >= 1.f) scriptFxRest(w);
}
void actGatherB(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f) plinkTakeFood(P(w), w.hiker.feet.x + 16.f, 44.f);
  if (u >= 1.f) scriptFxRest(w);
}
void doGatherMark(World& w, float) { M(w).p[3] = Vec2{cx(w) - 62.f, kLane}; }
// Crouched at the bag: stick and sack laid down by the branch's foot, the food in, the bag in his hands.
void actStuff(World& w, float u) {
  PlinkState& p = P(w);
  crouchAt0(w, u);
  if (u >= 0.2f && p.stick == PI_HAND) {
    plinkLay(p, Vec2{w.spine.camp.bag_work.x - 32.f, 357.f});
    plinkStick(p, PI_DOWN, kStickCarry);
    plinkSack(p, PI_DOWN);
  }
  if (u >= 0.8f && w.bag.state == BAG_DROPPED) {
    w.bag.state = BAG_CARRIED;
    hikerCarry(w.hiker, CARRY_BAG);
  }
  if (u >= 1.f) scriptFxRest(w);
}
// Over the branch and down to hang: the evening's toss that catches (spine.cpp toss).
void actRehang(World& w, float u) {
  FoodBag& b = w.bag;
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) {
    hikerCarry(h, CARRY_NONE);
    b.state = BAG_FLYING;
  }
  const float sweep = easeOutCubic(window(u, 0.f, 0.18f));
  const float down = easeInOut(window(u, 0.6f, 1.f));
  scriptSetArm(w, 0, 1.f - down, lerp(-0.7f, 2.0f, sweep), lerp(0.3f, 0.2f, sweep));
  w.fx.lean = -0.06f * sweep * (1.f - down);
  const Vec2 apex{b.tip.x + 12.f, b.tip.y - 58.f};
  if (u < 0.15f) {   // still in his hand
    const Vec2 hd = hikerHandPos(h, 0);
    b.pos = Vec2{hd.x, hd.y + 3.6f};
    b.swing = 0.f;
    m.v0 = b.pos;
    return;
  }
  const Vec2 p0 = m.v0;
  const Vec2 hang{b.tip.x, b.tip.y + 16.f};
  const float t1 = window(u, 0.15f, 0.55f), t2 = window(u, 0.55f, 0.75f);
  if (u < 0.55f) b.pos = Vec2{lerp(p0.x, apex.x, t1), lerp(p0.y, apex.y, easeOutCubic(t1))};
  else b.pos = Vec2{lerp(apex.x, hang.x, t2), lerp(apex.y, hang.y, easeInCubic(t2))};
  b.swing = 0.9f * sinT(0.5f * t1) * (1.f - t2);
  if (u >= 0.75f) {
    b.state = BAG_HUNG;
    b.line = 16.f;
    b.swing = 0.f;
    P(w).rope_cut = 0;
  }
  if (u >= 1.f) scriptFxRest(w);
}
void actPickGear(World& w, float u) {
  PlinkState& p = P(w);
  crouchAt0(w, u);
  if (u >= 0.6f && p.stick == PI_DOWN) {
    plinkStick(p, PI_HAND, kStickCarry);
    plinkSack(p, PI_HAND);
  }
  if (u >= 1.f) scriptFxRest(w);
}

// An underhand lob of the stick onto the wood pile: it is there (and gone) as it leaves his hand.
void actTossStick(World& w, float u) {
  PlinkState& p = P(w);
  const float k = ramp(u, 0.05f, 0.4f) * (1.f - ramp(u, 0.6f, 1.f));
  scriptSetArm(w, 0, k, lerp(-0.4f, 1.8f, ramp(u, 0.1f, 0.45f)), 0.3f);
  if (u < 0.4f) plinkStick(p, PI_HAND, lerp(kStickCarry, 1.6f, ramp(u, 0.1f, 0.4f)));
  else plinkStick(p, PI_NONE, kStickCarry);
  if (u >= 1.f) scriptFxRest(w);
}
void actSackIn(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.6f) {
    plinkSack(P(w), PI_NONE);
    P(w).sack_n = 0;
  }
  if (u >= 1.f) scriptFxRest(w);
}

bool dogsClear(const World& w) { return houseDogsClear(w); }

#define STAND pose(POSE_STAND, 0.8f, 0.3f)
// The stick from the wood pile, the sack of cans from the pack, then the six cans onto the branch, tip first.
#define SET_UP STAND, go(AT_WOOD_WORK, 44.f), face(RIGHT), act(actTakeStick, 1.6f), pose(POSE_STAND, 0.6f, 0.2f), \
               go(AT_PACK_WORK, 44.f), face(LEFT), act(actCansOut, 1.8f), pose(POSE_STAND, 0.6f, 0.2f), \
               doit(doStartPlace), go(AT_M3, 50.f), \
               face(RIGHT), act(actPlaceCan, 1.8f), doit(doPlaced), walk(AT_M3, 38.f), repeatWhile(morePlaces, 4)
// Back to the pack: the stick and sack down, the rifle out in two halves and together, down to the corner.
#define RIFLE go(AT_PACK_WORK, 50.f), face(LEFT), act(plinkActRifleOut, 2.0f), pose(POSE_STAND, 0.6f, 0.2f), \
              act(plinkActAssemble, 2.0f), go(AT_M0, 42.f), face(RIGHT), until(dogsClear), \
              act(actKneel, 0.9f), act(plinkActLoad, 2.2f), still(0.3f)
// The string, then: unloaded, looked into, and only then up; the rifle away; the stick and sack back in his hands.
#define SHOOT doit(doString), until(stringDone), act(plinkActUnload, 2.2f), act(actStand, 0.9f), \
              go(AT_PACK_WORK, 42.f), face(LEFT), act(plinkActStow, 2.2f), pose(POSE_STAND, 0.6f, 0.2f)
// The cans, a pair at a time, into the sack.
#define PAIRS doit(doStartPick), go(AT_M3, 50.f), \
              face(RIGHT), act(actSweep, 1.0f), doit(doPicked), walk(AT_M3, 42.f), repeatWhile(morePicks, 4)
// The stick on the pile, the sack in the pack, and sit.
#define FINISH go(AT_WOOD_WORK, 50.f, 20.f, -6.f), face(RIGHT), act(actTossStick, 0.9f), \
               go(AT_PACK_WORK, 44.f), face(LEFT), act(actSackIn, 1.4f), pose(POSE_STAND, 0.6f, 0.2f), \
               go(AT_SIT, 38.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)

const Step kPlinkA[] = {SET_UP, RIFLE, SHOOT, PAIRS, FINISH};
// B: after the pairs, under the stubborn can with the stick.
const Step kPlinkB[] = {SET_UP, RIFLE, SHOOT, PAIRS, walk(AT_M2, 42.f), face(RIGHT), act(actPoke, 2.4f), act(actPickOne, 1.0f),
                        pose(POSE_STAND, 0.6f, 0.2f), FINISH};
// C: the food first, then the bag back over the branch, then the cans.
const Step kPlinkC[] = {SET_UP, RIFLE, SHOOT,
                        go(AT_M2, 50.f), face(RIGHT), act(actGatherA, 1.2f), doit(doGatherMark), walk(AT_M3, 42.f),
                        act(actGatherB, 1.2f), walk(AT_M2, 42.f), act(actStuff, 1.4f), pose(POSE_STAND, 0.6f, 0.2f),
                        walk(AT_BAG_WORK, 38.f), face(RIGHT), act(actRehang, 2.0f), pose(POSE_STAND, 0.5f, 0.2f),
                        act(actPickGear, 1.2f), pose(POSE_STAND, 0.6f, 0.2f),
                        PAIRS, FINISH};

#undef STAND
#undef SET_UP
#undef RIFLE
#undef SHOOT
#undef PAIRS
#undef FINISH

// ---- the beat's events -------------------------------------------------------------------------------------------

}  // namespace

// The first shot: two birds burst out of the camp pine and a squirrel dashes off. A spawn that gets no slot is skipped.
void plinkStartle(World& w) {
  const float x = cx(w);
  struct Spawn {
    CritterKind kind;
    Vec2 feet;
    int facing;
  };
  const Spawn sp[3] = {{CR_BIRD, Vec2{x - 26.f, 236.f}, -1}, {CR_BIRD, Vec2{x + 22.f, 228.f}, 1}, {CR_SQUIRREL, Vec2{x + 8.f, 344.f}, 1}};
  for (int k = 0; k < 3; ++k) {
    const int s = directorSpawn(w, sp[k].kind, sp[k].feet, sp[k].facing);
    if (s < 0) continue;
    w.crit[s].tag = CT_MAJOR;
    M(w).c[k] = (int8_t)s;
  }
  directorScatter(w, plinkMuzzle(P(w)), 900.f);
}

namespace {

// The bullet cuts the bag's line just under the tip: the bag drops (and the rope's end stays).
void cutLine(World& w) {
  FoodBag& b = w.bag;
  PlinkState& p = P(w);
  if (b.state != BAG_HUNG) return;
  b.state = BAG_DROPPED;
  b.swing = 0.f;
  b.pos = Vec2{b.tip.x, b.tip.y + b.line + 18.f};
  p.rope_cut = 1;
  p.bag_vy = 1.f;
}

void fallBag(World& w, float dt) {
  PlinkState& p = P(w);
  if (p.bag_vy == 0.f || w.bag.state != BAG_DROPPED) return;
  p.bag_vy += kGravity * dt;
  w.bag.pos.y += p.bag_vy * dt;
  const float ground = w.spine.camp.bag_work.y - kBagDropY;
  if (w.bag.pos.y >= ground) {
    w.bag.pos.y = ground;
    p.bag_vy = 0.f;
    plinkSpill(p, w.bag.pos, ground + 2.f, D(w).rng);
  }
}

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------------

uint8_t plinkPlanVar(uint32_t r) { return (uint8_t)((r >> 13) % 3u); }

// PROPOSED seconds: the script's length over a few seeds plus slack (tests/test_day_plink.cpp reads how early the
// script is done and wants 3 s).
int plinkSeconds(uint8_t var) {
  if (var & DAY_NOSHOW) return 46;
  switch (var & 3u) {
    case 0: return 103;
    case 1: return 117;
    default: return 120;
  }
}

const Step* plinkScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  const int v = var & 3;
  if (v == 1) {
    n = (uint8_t)(sizeof(kPlinkB) / sizeof(kPlinkB[0]));
    return kPlinkB;
  }
  if (v == 2) {
    n = (uint8_t)(sizeof(kPlinkC) / sizeof(kPlinkC[0]));
    return kPlinkC;
  }
  n = (uint8_t)(sizeof(kPlinkA) / sizeof(kPlinkA[0]));
  return kPlinkA;
}

void plinkMajorBegin(World& w) {
  MajorState& m = M(w);
  const CampLayout& c = w.spine.camp;
  const float x = cx(w);
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  w.day.kind = DAY_PLINK;
  if (dayWet(w)) m.var = (uint8_t)(m.var | DAY_NOSHOW);   // a rain the plan could not know of: he only looks at the sky
  // C needs the bag on its branch to shoot (never otherwise at midday after the build, but cheap to check): play A.
  if ((m.var & 3u) == 2u && w.bag.state != BAG_HUNG) m.var = (uint8_t)(m.var & ~3u);
  // The firing spot: the lower left corner, on the bank just right of the big foreground pine, a little above the water.
  const Rect pine = landscapeFgPineBounds(w.land, 1);
  const float fx = clampf((float)pine.x1 + 14.f, 150.f, 180.f);
  float fy = 432.f, cy = 0.f, half = 0.f;
  if (landscapeStreamSpan(w.land, fx, cy, half)) fy = clampf(cy - 1.06f * half - 5.f, 412.f, 440.f);
  m.p[0] = Vec2{fx, fy};
  m.p[1] = Vec2{c.pack.x + 40.f, 358.f};
  m.p[2] = Vec2{x - 96.f, kLane};
  m.p[3] = m.p[0];
  DayPlink& d = D(w);
  float px[PLINK_PAIRS] = {x - 40.f, x + 6.f, x + 44.f}, py[PLINK_PAIRS];
  for (int k = 0; k < PLINK_PAIRS; ++k) py[k] = scriptGroundAt(w, px[k], 351.f);
  plinkInit(d.p, Vec2{x, (float)LS_BRANCH_ROOT_Y}, Vec2{x - (float)LS_BRANCH_LEN, (float)LS_BRANCH_TIP_Y}, 1, px, py);
  plinkLay(d.p, m.p[1]);
  d.rng.seed(seedOf(w));
  if ((m.var & 3u) == 1u) m.p[2] = Vec2{canX(w, 0) - 20.f, kLane};   // B: under the stubborn can
}

void plinkMajorTick(World& w, float dt) {
  if (!live(w)) return;
  const uint8_t ev = plinkTakeEvents(P(w));
  if (ev & PEV_FIRST) plinkStartle(w);
  if (ev & PEV_BAG) cutLine(w);
  fallBag(w, dt);
}

void plinkMajorEnd(World& w) {
  // The bag back on its branch and out of his hands, whatever the script did (an overrun must not keep a dropped bag).
  if (w.bag.state == BAG_DROPPED || w.bag.state == BAG_CARRIED || w.bag.state == BAG_FLYING) {
    w.bag.state = BAG_HUNG;
    w.bag.line = 16.f;
    w.bag.swing = 0.f;
  }
  if (w.hiker.carry == CARRY_BAG) hikerCarry(w.hiker, CARRY_NONE);
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where it was)
}

// (the long shot too: it runs on DAY_PLINK, LONGRANGE_PLAN 6; a dog that goes out mid-way waits until it ends)
bool plinkHoldsDogs(const World& w) {
  return w.major.on && (w.major.id == MJ_PLINK || w.major.id == MJ_LONGSHOT) && w.day.kind == DAY_PLINK;
}
bool plinkRifleInHands(const World& w) { return w.day.kind == DAY_PLINK && plinkRifleOut(w.day.plink.p); }
bool plinkHandsFullNow(const World& w) { return w.day.kind == DAY_PLINK && plinkHandsFull(w.day.plink.p); }

}  // namespace ss
