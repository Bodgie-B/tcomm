// The Christmas season's story (xmas.h; Mike, 2026-11-26): the plan byte, the decorating, the lights going
// on and off, the raccoon's theft. A story file: it says who walks where and leaves every pixel to xmas.cpp
// (tools/sync_ss_core.py builds it at the firmware's -Os). Unverified on the panel.
#include "xmas.h"

#if SS_XMAS

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../ss_app.h"
#include "director.h"
#include "script.h"
#include "spine_family.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

inline float fl(int v) { return (float)v; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }
inline uint8_t u8(float v) { return (uint8_t)(v <= 0.f ? 0 : (v >= 255.f ? 255 : (int)(v + 0.5f))); }

XmasMock& X(World& w) { return w.xmas.m; }
int dayOf(const World& w) { return (w.plan.xmas & XMAS_PLAN_ON) ? (int)(w.plan.xmas & XMAS_PLAN_DAY) : -1; }

// Presents by the week: two at the season's start, one more each week, the whole pile on Christmas Day.
uint8_t giftsFor(int day) {
  if (day >= XMAS_DAY_CHRISTMAS) return XMAS_GIFTS_ALL;
  if (day < 0) return 0;
  const int n = 2 + day / 7;
  return (uint8_t)(n > 5 ? 5 : n);
}
// The bush's floor by the date (the panel starts each screensaver run with fresh loop memory, so a run in
// late December still finds a well-stocked bush): one ornament every nine days, at most three; each theft
// adds one more, to XMAS_BUSH_MAX.
uint8_t bushFloor(int day) { return (uint8_t)(day < 0 ? 0 : (day / 9 > 3 ? 3 : day / 9)); }

// The tree pieces off (the bush stays).
void clearTree(World& w) {
  XmasMock& m = X(w);
  const uint8_t bush = m.bush, wheel = m.wheel;
  memset(&m, 0, sizeof m);
  m.bush = bush;
  m.wheel = wheel;
}

// ---- the places --------------------------------------------------------------------------------------

// Beside the box on the bank, facing the wheel (the switch is on the box).
Vec2 boxSpot(const World& w) {
  const Vec2 b = xmasBoxAt(w.land);
  const float x = b.x - 15.f;
  return Vec2{x, scriptGroundAt(w, x, b.y + 1.f)};
}
// Left of the ladder's foot, where he lifts it and lays it down.
Vec2 ladderSpot(const World& w) {
  const Vec2 f = xmasLadderFoot(w.land);
  return Vec2{f.x - 15.f, f.y + 2.f};
}
// At the ladder's foot, where he steps on.
Vec2 ladderStep(const World& w) {
  const Vec2 f = xmasLadderFoot(w.land);
  return Vec2{f.x - 1.f, f.y + 3.f};
}

// ---- actions -----------------------------------------------------------------------------------------

void restArms(World& w) { w.fx.arm_w[0] = w.fx.arm_w[1] = 0.f; }

// The generator from the edge of the woods, in his arms.
void doGenInArms(World& w, float) {
  XmasMock& m = X(w);
  m.gen = 1;
  m.carry = 1;
  m.lift = 255;
  hikerCarry(w.hiker, CARRY_XMAS);
}
// Set down: the box on the bank, the wheel on its post, raised out of the water. The wire is laid.
void actSetGen(World& w, float u) {
  if (u >= 0.55f && X(w).carry) {
    X(w).carry = 0;
    hikerCarry(w.hiker, CARRY_NONE);
  }
  const float e = scriptEnvelope(u, 0.2f, 0.3f);
  scriptSetArm(w, 0, e, 0.9f, 0.4f);
  scriptSetArm(w, 1, e, 0.8f, 0.5f);
  if (u >= 1.f) scriptFxRest(w);
}
// The ladder up off the grass, swung to lean on the pine (and back down).
void actLadderUp(World& w, float u) {
  XmasMock& m = X(w);
  m.extras = (uint8_t)(m.extras | XE_LADDER);
  m.ladder = u8(255.f * easeInOut(window(u, 0.15f, 0.9f)));
  const float e = scriptEnvelope(u, 0.15f, 0.2f);
  const float k = fl(m.ladder) / 255.f;
  scriptSetArm(w, 0, e, lerp(0.5f, 2.3f, k), 0.3f);
  scriptSetArm(w, 1, e, lerp(0.4f, 2.1f, k), 0.4f);
  if (u >= 1.f) scriptFxRest(w);
}
void actLadderDown(World& w, float u) {
  XmasMock& m = X(w);
  m.ladder = u8(255.f * (1.f - easeInOut(window(u, 0.1f, 0.85f))));
  const float e = scriptEnvelope(u, 0.15f, 0.2f);
  const float k = fl(m.ladder) / 255.f;
  scriptSetArm(w, 0, e, lerp(0.5f, 2.3f, k), 0.3f);
  scriptSetArm(w, 1, e, lerp(0.4f, 2.1f, k), 0.4f);
  if (u >= 1.f) scriptFxRest(w);
}

// Up or down the ladder between rungs A and B (-1: the ground at its foot). He stands on a rung: feet.x
// on the rung, his lift the rung's height over the ground; each rung a little bob of the knees. The lift
// stays when it ends (he is still up there): the next act keeps it, the last one down rests it.
void climbTo(World& w, float u, int a, int b) {
  Hiker& h = w.hiker;
  const Vec2 g = ladderStep(w);
  const Vec2 pa = a < 0 ? g : xmasLadderRung(w.land, a);
  const Vec2 pb = b < 0 ? g : xmasLadderRung(w.land, b);
  if (u <= 0.f) {
    h.facing = 1;   // the tree is to his right
    h.feet.y = g.y;
    scriptSetPose(w, POSE_CLIMB, 0.3f);
  }
  const float k = easeInOut(u);
  const int steps = a > b ? a - b : b - a;
  const float bob = 0.012f * absf(sinT(0.5f * u * fl(steps < 1 ? 1 : steps)));
  h.feet.x = floorf(lerp(pa.x, pb.x, k) - 1.5f);
  w.fx.lift = (g.y - lerp(pa.y, pb.y, k)) / h.height + bob * (1.f - k) * k * 4.f;
  if (u >= 1.f) {
    w.fx.lift = (g.y - pb.y) / h.height;
    if (b < 0) {
      w.fx.lift = 0.f;
      h.feet.x = g.x;
      scriptSetPose(w, POSE_STAND, 0.4f);
      scriptFxRest(w);
    } else {
      scriptSetPose(w, POSE_REACH_UP, 0.4f);
    }
  }
}
template <int A, int B>
void actClimb(World& w, float u) {
  climbTo(w, u, A, B);
}

// One wrap of lights round tier K: the near hand reaches round the boughs and back, the far one feeds
// the string up from the coil. The wrap is on when it ends.
template <int K>
void actString(World& w, float u) {
  XmasMock& m = X(w);
  m.extras = (uint8_t)(m.extras | XE_COIL | XE_RUN);
  const float e = scriptEnvelope(u, 0.12f, 0.15f);
  const float s = sinT(1.5f * u);
  const float up = K <= 1 ? 2.5f : (K == 2 ? 2.2f : (K == 3 ? 1.5f : 0.9f));
  scriptSetArm(w, 0, e, up + 0.45f * s, 0.4f + 0.3f * s);
  scriptSetArm(w, 1, e, up - 0.5f, 0.9f);
  if (u >= 1.f) {
    if (m.wraps < K + 1) m.wraps = (uint8_t)(K + 1);
    restArms(w);
  }
}
void doCoilAway(World& w, float) { X(w).extras = (uint8_t)(X(w).extras & ~(XE_COIL | XE_RUN)); }

// The cardinals: in with the garland, wound up in it, freed, the end pulled round, away.
void actBirdsIn(World& w, float u) {
  XmasMock& m = X(w);
  m.garland = XG_FLYING;
  m.fly = u8(255.f * u);
  w.fx.head = -0.25f * scriptEnvelope(u, 0.3f, 0.2f);
  if (u >= 1.f) {
    m.garland = XG_TANGLED;
    m.fly = 0;
    w.fx.head = 0.f;
  }
}
void actTangle(World& w, float u) {
  XmasMock& m = X(w);
  m.garland = XG_TANGLED;
  m.fly = u8(255.f * u);
  if (u >= 1.f) m.fly = 0;
}
void actFree(World& w, float u) {
  XmasMock& m = X(w);
  const float e = scriptEnvelope(u, 0.25f, 0.25f);
  scriptSetArm(w, 0, e, 2.4f + 0.2f * sinT(2.f * u), 0.5f);
  m.fly = u8(255.f * u);
  if (u >= 1.f) {
    m.garland = XG_PART;
    m.fly = 0;
    scriptFxRest(w);
  }
}
void actBirdPull(World& w, float u) {
  XmasMock& m = X(w);
  m.garland = XG_PART;
  m.fly = u8(255.f * u);
  if (u >= 1.f) {
    m.garland = XG_LEAVING;
    m.fly = 0;
  }
}
void actBirdsAway(World& w, float u) {
  XmasMock& m = X(w);
  m.garland = XG_LEAVING;
  m.fly = u8(255.f * u);
  w.fx.head = -0.3f * scriptEnvelope(u, 0.2f, 0.3f);
  if (u >= 1.f) {
    m.garland = XG_DRAPED;
    m.fly = 0;
    w.fx.head = 0.f;
  }
}

// An ornament on its hook: the near arm up to it, on at the top of the reach.
void hang(World& w, float u, int i) {
  const Hiker& h = w.hiker;
  const Vec2 o = xmasOrnament(w.land, i);
  const float sh_y = h.feet.y - 0.75f * h.height;   // his shoulder, about
  const float dx = absf(o.x - h.feet.x) + 8.f, dy = sh_y - o.y;
  const float ang = clamp01(atan2f(dy, dx) / 3.14159265f + 0.5f) * 3.14159265f;   // from straight down
  const float e = scriptEnvelope(u, 0.3f, 0.3f);
  scriptSetArm(w, 0, e, ang, 0.15f);
  if (u >= 0.55f) X(w).orns = (uint8_t)(X(w).orns | (1u << i));
  if (u >= 1.f) scriptFxRest(w);
}
template <int I>
void actHang(World& w, float u) {
  hang(w, u, I);
}

// The star on top, from the top rung.
void actStarOn(World& w, float u) {
  const float e = scriptEnvelope(u, 0.3f, 0.3f);
  scriptSetArm(w, 0, e, 2.95f, 0.1f);
  scriptSetArm(w, 1, 0.6f * e, 2.7f, 0.3f);
  if (u >= 0.55f) X(w).extras = (uint8_t)(X(w).extras | XE_STAR);
  if (u >= 1.f) restArms(w);
}

// The wheel lowered into the current: it turns, the lights come on (and the presents are there).
void actLowerWheel(World& w, float u) {
  XmasMock& m = X(w);
  m.lift = u8(255.f * (1.f - easeInOut(window(u, 0.1f, 0.75f))));
  const float e = scriptEnvelope(u, 0.15f, 0.25f);
  scriptSetArm(w, 0, e, 0.9f, 0.3f);
  if (m.lift == 0 && m.power == XP_OFF) {
    m.power = XP_ON;
    m.gifts = giftsFor(dayOf(w));
  }
  if (u >= 1.f) scriptFxRest(w);
}
// And raised out of it: the wheel stops, the lights go out.
void actRaiseWheel(World& w, float u) {
  XmasMock& m = X(w);
  m.lift = u8(255.f * easeInOut(window(u, 0.2f, 0.85f)));
  if (m.lift > 0) m.power = XP_OFF;
  const float e = scriptEnvelope(u, 0.15f, 0.25f);
  scriptSetArm(w, 0, e, 0.9f + 0.4f * fl(m.lift) / 255.f, 0.3f);
  if (u >= 1.f) scriptFxRest(w);
}

// A little shake of the head, sitting (the ornament gone to the bush).
void actHeadShake(World& w, float u) {
  const float e = scriptEnvelope(u, 0.15f, 0.2f);
  w.fx.head = e * (0.06f + 0.14f * sinT(2.5f * u));
  w.fx.lean = -0.06f * e;
  if (u >= 1.f) scriptFxRest(w);
}

// ---- points the scripts walk to (AT_FAM0, AT_FAM1: the evening and the morning beats set their own) ----

void doPtsDeck(World& w, float) {
  w.spine.fp[0] = boxSpot(w);
  w.spine.fp[1] = ladderSpot(w);
}
bool starDay(const World& w) { return dayOf(w) >= XMAS_STAR_DAY; }

// ---- the scripts -------------------------------------------------------------------------------------

// The decorating (about 112 s; 124 with the star; the beat 132 s): the generator, the ladder and the lights, the birds and
// the garland, the ornaments, the star from Dec 11, the ladder down, the wheel lowered: the lights on.
// AT_FAM1 is right of the ladder's foot; the tree's front and sides are offsets from it.
const Step kDecorate[] = {
    doit(doPtsDeck),
    pose(POSE_STAND, 0.4f, 0.2f),
    face(LEFT),
    walk(AT_EDGE_L, 40.f),
    still(2.5f),
    doit(doGenInArms),
    pose(POSE_CARRY, 0.f, 0.f),
    face(RIGHT),
    walk(AT_FAM0, 36.f),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    act(actSetGen, 1.6f),
    pose(POSE_STAND, 0.5f, 0.2f),
    face(LEFT),
    go(AT_FAM1, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.5f, 0.1f),
    act(actLadderUp, 2.2f),
    pose(POSE_STAND, 0.4f, 0.1f),
    walk(AT_FAM1, 20.f, 14.f, 1.f),   // to the foot
    act((actClimb<-1, 3>), 2.4f),
    act(actString<0>, 3.0f),
    act((actClimb<3, 1>), 1.2f),
    act(actString<1>, 3.0f),
    act((actClimb<1, -1>), 1.4f),
    walk(AT_FAM1, kStroll, 44.f, 6.f),   // in front of the tree
    face(RIGHT),
    pose(POSE_REACH_UP, 0.5f, 0.f),
    act(actString<2>, 2.6f),
    pose(POSE_STAND, 0.5f, 0.f),
    act(actString<3>, 2.6f),
    pose(POSE_CROUCH, 0.5f, 0.f),
    act(actString<4>, 2.6f),
    doit(doCoilAway),
    pose(POSE_STAND, 0.5f, 0.2f),
    walk(AT_FAM1, kStroll, -8.f, 12.f),   // back a little, to look
    face(RIGHT),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 0.4f),
    act(actBirdsIn, 4.0f),
    act(actTangle, 2.4f),
    pose(POSE_SCRATCH_HEAD, 0.5f, 1.0f),
    pose(POSE_STAND, 0.4f, 0.f),
    walk(AT_FAM1, kStroll, 86.f, 6.f),   // under the tangled bird, right of the trunk
    face(RIGHT),
    act(actFree, 1.8f),
    act(actBirdPull, 2.4f),
    act(actBirdsAway, 3.0f),
    walk(AT_FAM1, kStroll, 34.f, 6.f),   // the tree's front, left of the trunk
    face(LEFT),
    act(actHang<6>, 1.4f),
    act(actHang<4>, 1.4f),
    act(actHang<0>, 1.5f),
    act(actHang<2>, 1.4f),
    face(RIGHT),
    act(actHang<5>, 1.4f),
    act(actHang<1>, 1.5f),
    act(actHang<3>, 1.4f),
    skipUnless(starDay, 6),
    walk(AT_FAM1, kStroll, 14.f, 1.f),
    act((actClimb<-1, 6>), 3.2f),
    act(actStarOn, 2.0f),
    act((actClimb<6, -1>), 2.8f),
    pose(POSE_STAND, 0.3f, 0.1f),
    still(0.1f),
    walk(AT_FAM1, 20.f),
    face(RIGHT),
    pose(POSE_CROUCH, 0.5f, 0.1f),
    act(actLadderDown, 2.0f),
    pose(POSE_STAND, 0.4f, 0.1f),
    go(AT_FAM0, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.5f, 0.1f),
    act(actLowerWheel, 2.2f),
    still(0.4f),
    pose(POSE_STAND, 0.5f, 0.1f),
    face(LEFT),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.6f),
    pose(POSE_STAND, 0.5f, 0.1f),
    go(AT_WOOD_WORK, kStroll),   // where the fire beat starts from (the firewood beat used to leave him there)
};
constexpr int kDecorateS = 132;

// Every other evening loop: down to the wheel, lowered, the lights on; back toward the fire.
const Step kLightsOn[] = {
    doit(doPtsDeck),
    pose(POSE_STAND, 0.4f, 0.2f),
    go(AT_FAM0, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.1f),
    act(actLowerWheel, 2.0f),
    still(0.4f),
    pose(POSE_STAND, 0.5f, 0.1f),
    face(LEFT),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.0f),
    pose(POSE_STAND, 0.5f, 0.1f),
    go(AT_WOOD_WORK, kStroll),
};
constexpr int kLightsOnS = 33;

// The morning before break camp: from the fire to the wheel, raised, the lights out; to the tent.
const Step kLightsOff[] = {
    doit(doPtsDeck),
    pose(POSE_STAND, 0.8f, 0.3f),
    go(AT_FAM0, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.1f),
    act(actRaiseWheel, 2.0f),
    pose(POSE_STAND, 0.5f, 0.1f),
    go(AT_TENT_WORK, kStroll),
};
constexpr int kLightsOffS = 26;

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

// ---- the beats ----------------------------------------------------------------------------------------

void deckBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  // This evening's window has played now (the plan named it in Memory::xmas_window), not before: a run
  // stopped before the beat leaves it for the next.
  w.memory.taichi_next = (uint8_t)(w.memory.taichi_next | w.memory.xmas_window);
  XmasMock& m = X(w);
  clearTree(w);
  m.extras = (uint8_t)(XE_STOCKING | XE_LADDER | XE_COIL);
  m.ladder = 0;
  scriptStart(w, kDecorate, N(kDecorate));
}
void onBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  xmasPoseFinished(w);
  XmasMock& m = X(w);
  m.power = XP_OFF;
  m.lift = 255;
  m.gifts = 0;   // with the lights
  scriptStart(w, kLightsOn, N(kLightsOn));
}
void offBegin(SceneCtx& ctx) { scriptStart(*ctx.w, kLightsOff, N(kLightsOff)); }
void storyUpdate(SceneCtx& ctx, float, float) { scriptRun(*ctx.w); }
void storyEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  if (w.spine.run.steps && !scriptDone(w) && w.spine.overruns < 255) ++w.spine.overruns;
  w.spine.run.steps = nullptr;
  // An overrun must not leave him up the ladder or with the generator in his arms.
  XmasMock& m = X(w);
  if (m.carry) {
    m.carry = 0;
    hikerCarry(w.hiker, CARRY_NONE);
  }
  if (w.fx.lift != 0.f) {
    w.fx.lift = 0.f;
    w.hiker.feet = ladderStep(w);
  }
  m.extras = (uint8_t)(m.extras & ~(XE_COIL | XE_RUN));
  if (m.garland != XG_NONE && m.garland != XG_DRAPED) m.garland = XG_DRAPED;
}

// ---- the theft (the bauble_thief major) ----------------------------------------------------------------

constexpr uint16_t kGo = 1, kE1 = 256;
void cueGo(World& w, float) { w.major.cue = (uint16_t)(w.major.cue | kGo); }
bool ev1(const World& w) { return (w.major.cue & kE1) != 0; }
bool gone(const World& w) {
  for (const CritterSlot& s : w.crit)
    if (s.in_use && s.tag == CT_MAJOR) return false;
  return true;
}
bool notSeated(const World& w) {
  const Vec2 s = w.spine.camp.sit;
  return !(w.hiker.target == POSE_SIT && absf(w.hiker.feet.x - s.x) < 3.f && absf(w.hiker.feet.y - s.y) < 3.f);
}

const Step kTheftOut[] = {
    skipUnless(notSeated, 4), pose(POSE_STAND, 0.8f, 0.3f), go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.4f),
    still(0.5f),   doit(cueGo),  until(ev1),   still(0.4f), face(RIGHT), still(0.6f),
    act(actHeadShake, 1.8f),     until(gone),  still(0.6f), face(TO_FIRE), still(0.5f),
};
const Step kTheftTent[] = {still(0.5f), doit(cueGo), until(gone), still(1.0f)};

constexpr float kCoonWalk = 40.f;

// The low ornament's place: where the raccoon stands up under it (facing left, the paw to it).
Vec2 reachSpot(const World& w) {
  const Vec2 o = xmasOrnament(w.land, XMAS_LOW_ORN);
  const float x = o.x + 9.f;
  return Vec2{x, scriptGroundAt(w, x, xmasTreeBase(w.land) + 7.f)};
}
Vec2 bushSpot(const World& w) {
  const Vec2 b = xmasBushAt(w.land);
  const float x = b.x - 24.f;
  return Vec2{x, scriptGroundAt(w, x, b.y)};
}

}  // namespace

// ---- public ------------------------------------------------------------------------------------------

uint8_t xmasPlanByte(const Inputs& in, Memory& mem, uint8_t family) {
  const int day = xmasSeasonDay(in.month, in.mday);
  if (day < 0 || spineSetPiece(family)) return 0;
  uint8_t b = (uint8_t)(XMAS_PLAN_ON | (uint8_t)day);
  if (family != SF_EVENING) {
    mem.taichi_next = (uint8_t)(mem.taichi_next & ~(XMAS_DECK_EARLY | XMAS_DECK_LATE));   // a new day
    return b;
  }
  const float m = in.mood - 6.f * floorf(in.mood / 6.f);
  const bool early = !in.night_mode && m >= 3.5f && m < 5.5f;
  const uint8_t bit = early ? XMAS_DECK_EARLY : XMAS_DECK_LATE;
  mem.xmas_window = bit;
  if ((mem.taichi_next & bit) == 0) b = (uint8_t)(b | XMAS_PLAN_DECK);
  return b;
}

void xmasPoseFinished(World& w) {
  XmasMock& m = X(w);
  const int day = dayOf(w);
  clearTree(w);
  m.gen = 1;
  m.wraps = XMAS_WRAPS;
  m.power = XP_ON;
  m.garland = XG_DRAPED;
  m.orns = (uint8_t)((1u << XMAS_ORNS) - 1u);
  m.extras = (uint8_t)(XE_STOCKING | (day >= XMAS_STAR_DAY ? XE_STAR : 0));
  m.gifts = giftsFor(day);
}

void xmasBeginLoop(World& w) {
  if (spineSetPiece(w.plan.family)) return;   // the Lookout's running state shares these bytes
  memset(&w.xmas, 0, sizeof w.xmas);
  Memory& mem = w.memory;
  const int day = dayOf(w);
  if (day < 0) {
    mem.xmas_bush = 0;   // out of season: the bush starts bare again next year
    return;
  }
  const uint8_t floor_n = bushFloor(day);
  if (mem.xmas_bush < floor_n) mem.xmas_bush = floor_n;
  if (mem.xmas_bush > XMAS_BUSH_MAX) mem.xmas_bush = XMAS_BUSH_MAX;
  X(w).bush = (uint8_t)(1 + mem.xmas_bush);
}

void xmasEndLoop(World& w) { (void)w; }

bool xmasTreeShown(const World& w) { return w.xmas.m.gen != 0 && w.xmas.m.carry == 0 && w.xmas.m.wraps == XMAS_WRAPS; }

void xmasStoryTick(World& w) {
  if (dayOf(w) < 0) return;
  const uint8_t f = w.plan.family;
  if (f != SF_MORNING && f != SF_MIDDAY) return;
  // The morning's and midday's tree is up whenever the tent is (it comes down with the tent).
  const bool up = w.tent.state == TENT_UP && w.tent.progress >= 1.f;
  if (up && !X(w).gen) xmasPoseFinished(w);
  if (!up && X(w).gen) clearTree(w);
}

void xmasAmbient(World& w) {
  if (dayOf(w) < 0) return;
  XmasMock& m = X(w);
  const bool turning = m.gen && !m.carry && m.lift == 0;
  if (turning) m.wheel = (uint8_t)((w.tick >> 1) & 63u);   // half a turn every 5 s; a new picture every other tick
  // A crackle of sparks off the box now and then while it runs (about every 14 s).
  uint8_t sp = 0;
  if (turning && m.power != XP_OFF) {
    const uint32_t ph = w.tick % 347u;
    if (ph < 12u) sp = (uint8_t)(1u + ph * 8u / 12u);
  }
  m.spark = sp;
}

Beat xmasEveningBeat(const LoopPlan& p) {
  if (p.xmas & XMAS_PLAN_DECK) return Beat{"decorate", (float)kDecorateS, deckBegin, storyUpdate, storyEnd};
  return Beat{"lights_on", (float)kLightsOnS, onBegin, storyUpdate, storyEnd};
}
int xmasEveningSeconds(const LoopPlan& p) {
  if (!(p.xmas & XMAS_PLAN_ON)) return 0;
  return (p.xmas & XMAS_PLAN_DECK) ? kDecorateS : kLightsOnS;
}
Beat xmasMorningBeat() { return Beat{"lights_off", (float)kLightsOffS, offBegin, storyUpdate, storyEnd}; }
int xmasMorningSeconds() { return kLightsOffS; }

void xmasTheftBegin(World& w) {
  MajorState& m = w.major;
  m.p[0] = reachSpot(w);
  m.p[1] = bushSpot(w);
  const float x = fl(SS_WIDTH) + 34.f;
  m.p[2] = Vec2{x, scriptGroundAt(w, x, m.p[1].y - 14.f)};
}

void xmasTheftTick(World& w, float) {
  MajorState& m = w.major;
  XmasMock& x = X(w);
  if (!(m.cue & kGo)) return;
  if (m.stage[0] == 0 && m.c[0] < 0) {   // in from the right, past its bush, to the tree
    const int s = directorSpawn(w, CR_RACCOON, m.p[2], -1);
    m.c[0] = (int8_t)s;
    m.st[0] = 0.f;
    if (s < 0) {
      m.cue = (uint16_t)(m.cue | kE1);
      m.stage[0] = 9;
      return;
    }
    w.crit[s].tag = CT_MAJOR;
    w.crit[s].flags = CF_BRAVE;
    Critter& c = w.critters[s];
    c.tone = kToneMasked;
    c.scale = 1.3f;
    c.speed_dial = 0.8f;   // its own pace, not the loop's persona: the major's seconds count on it
    c.jitter_dial = 0.3f;
    directorWalk(w, s, m.p[0], kCoonWalk, CA_MOVE);
    m.stage[0] = 1;
    return;
  }
  const int s = m.c[0];
  if (s < 0 || !directorOwns(w, s, CT_MAJOR)) {
    if (x.coon == XR_HELD) {   // scattered with it: it drops nothing, the bush gets it all the same next time
      x.coon = XR_NONE;
      x.coon_slot = 0;
    }
    if (m.stage[0] != 0 && gone(w)) m.cue = (uint16_t)(m.cue | kE1);
    return;
  }
  Critter& c = w.critters[s];
  const float st = m.st[0];
  auto stage = [&m](int k) {
    m.stage[0] = (uint8_t)k;
    m.st[0] = 0.f;
  };
  switch (m.stage[0]) {
    case 1:   // under the ornament: up on its hind legs, a paw to it
      if (!critterArrived(c)) break;
      c.facing = -1;
      critterSetAction(c, CA_REACH, 0.4f);
      stage(2);
      break;
    case 2:
      if (st >= 1.3f && x.coon != XR_HELD) {   // got it: the hook empty, the garland pulled loose
        x.orns = (uint8_t)(x.orns & ~(1u << XMAS_LOW_ORN));
        x.extras = (uint8_t)(x.extras | XE_GAP);
        x.coon = XR_HELD;
        x.coon_slot = (uint8_t)(s + 1);
      }
      if (st >= 1.9f) {
        critterSetAction(c, CA_IDLE, 0.3f);
        directorWalk(w, s, m.p[1], kCoonWalk, CA_MOVE);
        stage(3);
      }
      break;
    case 3:   // to its bush; the ornament goes on it
      if (!critterArrived(c)) break;
      c.facing = 1;
      critterSetAction(c, CA_REACH, 0.3f);
      stage(4);
      break;
    case 4:
      if (st >= 1.0f && x.coon == XR_HELD) {
        x.coon = XR_NONE;
        x.coon_slot = 0;
        if (w.memory.xmas_bush < XMAS_BUSH_MAX) ++w.memory.xmas_bush;
        x.bush = (uint8_t)(1 + w.memory.xmas_bush);
        m.cue = (uint16_t)(m.cue | kE1);
      }
      if (st >= 1.4f) {
        critterSetAction(c, CA_SIT, 0.4f);   // admires it
        stage(5);
      }
      break;
    case 5:
      if (st >= 1.8f) {
        directorExit(w, s, 1, kCoonWalk, CA_MOVE);
        stage(6);
      }
      break;
    default: break;
  }
}

const Step* xmasTheftScript(uint8_t var, uint8_t& n) {
  if (var == 1) {
    n = N(kTheftTent);
    return kTheftTent;
  }
  n = N(kTheftOut);
  return kTheftOut;
}
int xmasTheftSeconds(uint8_t var) { return var == 1 ? 22 : 25; }

#undef N

}  // namespace ss

#endif  // SS_XMAS
