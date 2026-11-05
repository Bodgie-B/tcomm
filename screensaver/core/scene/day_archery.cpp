// The archery as a surprise major. See day_archery.h.
//
// The script (kArcheryA, kArcheryB, kArcheryC) is the hiker's half: the walks, the crouches, the bow
// check, and one doit per shot followed by an until(archeryDone). The bow's own step (archeryStep, from the
// hands this tick) and the pose bend are the World's (dayStep, dayPose in day.cpp). Scratch use of
// MajorState, private to this file:
//   p[0] his mark   p[1] where he crouches to set the stand up and to take it down   p[2] B: where he
//   stands to pull the arrow out of the pine
// No random draws: each shot's three values come from DayArchery::rng, seeded from the layout.
#include "day_archery.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "archery.h"
#include "day.h"
#include "landscape.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

MajorState& M(World& w) { return w.major; }
float env(float u, float a, float b) { return scriptEnvelope(u, a, b); }

// ---- tuning (PROPOSED; how it looks is UNVERIFIED) ---------------------------------------------------------

constexpr float kLaneAboveFire = 26.f;    // the lane runs this far above the fire pit's y
constexpr float kMarkRight = 40.f;        // his mark: this far right of the fire pit's x
constexpr float kTargetLeftOfPine = 74.f; // the stand: this far left of the camp pine's trunk
constexpr float kSetupLeft = 34.f;        // where he crouches to plant it: this far left of the stand
constexpr float kTrunkY = 330.f;          // the pine's bare trunk, where SHOT_TREE sticks

uint64_t seedOf(const World& w) {
  return ((uint64_t)(uint32_t)(w.fire.pos.x * 16.f) << 22) ^ (uint64_t)(uint32_t)(w.fire.pos.y * 16.f) ^ 0xA2C0000ull;
}

DayArchery& D(World& w) { return w.day.archery; }
bool live(const World& w) { return w.day.kind == DAY_ARCHERY; }

// ---- his actions -----------------------------------------------------------------------------------------------

void doBow(World& w, float) { archeryEquip(D(w).a, true); }
void doBowAway(World& w, float) { archeryEquip(D(w).a, false); }

// He crouches at the stand's spot and plants it: it is there as he comes up.
void actSetTarget(World& w, float u) {
  if (u <= 0.f) scriptSetPose(w, POSE_CROUCH, 0.6f);
  if (u >= 0.5f) archeryShowTarget(D(w).a, true);
  if (u >= 1.f) scriptFxRest(w);
}
// ... and takes it down: the arrows out of it (B keeps its one for the pine), the stand away.
void actPull(World& w, float u) {
  if (u <= 0.f) scriptSetPose(w, POSE_CROUCH, 0.6f);
  if (u >= 0.4f && (M(w).var & 3u) != 1u) archeryClear(D(w).a);
  if (u >= 0.85f) archeryShowTarget(D(w).a, false);
  if (u >= 1.f) scriptFxRest(w);
}
// B: up on his toes at the trunk, the arrow out.
void doPullTree(World& w, float) { archeryClear(D(w).a); }

// Checking the bow: held up in the near hand, head bent to it, a turn of the wrist.
void actCheckBow(World& w, float u) {
  const float e = env(u, 0.25f, 0.25f);
  scriptSetArm(w, 0, e, 1.45f + 0.1f * sinT(u * 3.f), 1.15f);
  w.fx.head = 0.2f * e;
  if (u >= 1.f) scriptFxRest(w);
}
void actShrug(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 0, e, 0.9f, 1.7f);
  scriptSetArm(w, 1, 0.8f * e, 0.8f, 1.6f);
  w.fx.head = 0.2f * e;
  w.fx.lift = 0.012f * e;
  if (u >= 1.f) scriptFxRest(w);
}

void shoot(World& w, uint8_t kind) { archeryShoot(D(w).a, kind, D(w).rng); }
void doShort(World& w, float) { shoot(w, SHOT_SHORT); }
void doPast(World& w, float) { shoot(w, SHOT_PAST); }
void doBull(World& w, float) { shoot(w, SHOT_BULLSEYE); }
void doTree(World& w, float) { shoot(w, SHOT_TREE); }
void doSplit(World& w, float) { shoot(w, SHOT_SPLIT); }
bool shotOver(const World& w) { return live(w) && archeryDone(w.day.archery.a); }

#define STAND pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), doit(doBowAway), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
// With the bow, out to the stand's spot, plant it, back to his mark, check the bow.
#define SET_UP STAND, doit(doBow), go(AT_M1, 30.f), face(RIGHT), act(actSetTarget, 2.2f), pose(POSE_STAND, 0.7f, 0.5f), \
               go(AT_M0, 30.f), face(RIGHT), still(0.5f), act(actCheckBow, 2.6f), still(0.8f)
// Out to the stand again, the arrows out of it, the stand down.
#define TAKE_DOWN go(AT_M1, 30.f), face(RIGHT), act(actPull, 2.6f), pose(POSE_STAND, 0.7f, 0.4f)

// A: short of it, past it, then the bullseye.
const Step kArcheryA[] = {
    SET_UP,
    doit(doShort), until(shotOver), still(1.4f),
    doit(doPast), until(shotOver), still(1.4f),
    doit(doBull), until(shotOver), still(0.8f),
    TAKE_DOWN, TO_SIT,
};
// B: over it and into the pine, for good; a shrug; the stand down and on to the pine for the arrow.
const Step kArcheryB[] = {
    SET_UP,
    doit(doTree), until(shotOver), still(0.6f), act(actShrug, 2.2f), still(0.6f),
    TAKE_DOWN,
    walk(AT_M2, 30.f), face(RIGHT), pose(POSE_REACH_UP, 0.8f, 1.6f), doit(doPullTree), pose(POSE_STAND, 0.8f, 0.5f),
    TO_SIT,
};
// C: a bullseye, then one that lands on it and splits it.
const Step kArcheryC[] = {
    SET_UP,
    doit(doBull), until(shotOver), still(1.6f),
    doit(doSplit), until(shotOver), still(0.8f),
    TAKE_DOWN, TO_SIT,
};

#undef STAND
#undef TO_SIT
#undef SET_UP
#undef TAKE_DOWN

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------------

uint8_t archeryPlanVar(uint32_t r) { return (uint8_t)((r >> 9) % 3u); }

// PROPOSED seconds: the script's length over a few seeds plus slack (tests/test_day_majors2.cpp reads how
// early the script is done and wants 3 s).
int archerySeconds(uint8_t var) {
  if (var & DAY_NOSHOW) return 46;
  switch (var & 3u) {
    case 0: return 70;
    case 1: return 63;
    default: return 61;
  }
}

const Step* archeryScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  const int v = var & 3;
  if (v == 1) {
    n = (uint8_t)(sizeof(kArcheryB) / sizeof(kArcheryB[0]));
    return kArcheryB;
  }
  if (v == 2) {
    n = (uint8_t)(sizeof(kArcheryC) / sizeof(kArcheryC[0]));
    return kArcheryC;
  }
  n = (uint8_t)(sizeof(kArcheryA) / sizeof(kArcheryA[0]));
  return kArcheryA;
}

void archeryMajorBegin(World& w) {
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  const float ly = F.y - kLaneAboveFire;
  const float tx = (float)w.land.camp_x - kTargetLeftOfPine;
  m.p[0] = Vec2{F.x + kMarkRight, ly};
  m.p[1] = Vec2{tx - kSetupLeft, ly + 4.f};
  m.p[2] = Vec2{(float)w.land.camp_x - 40.f, ly + 12.f};   // (in front of the stand, not through it)
  m.p[3] = m.p[0];
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  w.day.kind = DAY_ARCHERY;
  if (dayWet(w)) m.var = (uint8_t)(m.var | DAY_NOSHOW);   // a rain the plan could not know of: he only looks at the sky
  DayArchery& d = D(w);
  archeryInit(d.a, 1, Vec2{tx, ly + 2.f}, Vec2{(float)w.land.camp_x - 3.5f, kTrunkY});
  archeryShowTarget(d.a, false);   // it is not up yet
  d.rng.seed(seedOf(w));
}

void archeryMajorTick(World&, float) {}   // (the bow is stepped after the hiker, dayStep)

void archeryMajorEnd(World& w) {
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where it was)
}

}  // namespace ss
