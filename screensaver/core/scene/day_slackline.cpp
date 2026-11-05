// The slackline as a surprise major. See day_slackline.h.
//
// The script (kSlackA, kSlackB) is the hiker's half: the walks, the crouches at the stake, and one doit
// per command to the walker followed by an until on his mode. The module's steps (the walker, the
// critter, the spring) run from the tick, before the hiker's step. Scratch use of MajorState, private to
// this file: p[0] where he crouches at the stake   p[1] where he steps up   p[2] where he watches the
// critter from.
// No random draws: the module's few values come from DaySlack::rng, seeded from the layout.
#include "day_slackline.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "day.h"
#include "landscape.h"
#include "slackline.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

MajorState& M(World& w) { return w.major; }

// ---- tuning (PROPOSED; how it looks is UNVERIFIED) ---------------------------------------------------------

constexpr float kGroundBelowTrunk = 14.f;   // the ground his feet are on: this far below the camp pine's foot
constexpr float kAnchorUp = 26.f;           // the line's ends are this high above it
constexpr float kSpan = 226.f;              // the trunk's left face to the stake (the plan: 180 to 260)
constexpr float kMountIn = 52.f;            // where he steps up: this far from the trunk
constexpr float kTieIn = 22.f;              // where he crouches at the stake: this far from it
constexpr float kWatchIn = 52.f;            // where he watches the critter from: this far from the stake

uint64_t seedOf(const World& w) {
  return ((uint64_t)(uint32_t)(w.fire.pos.x * 16.f) << 21) ^ (uint64_t)(uint32_t)(w.fire.pos.y * 16.f) ^ 0x51AC0000ull;
}

DaySlack& D(World& w) { return w.day.slack; }
const DaySlack& D(const World& w) { return w.day.slack; }
bool live(const World& w) { return w.day.kind == DAY_SLACK; }

// ---- his actions -----------------------------------------------------------------------------------------------

// Crouching at the stake: the line and the stake are up as he comes up, or gone.
void actTie(World& w, float u) {
  if (u <= 0.f) scriptSetPose(w, POSE_CROUCH, 0.6f);
  if (u >= 0.6f) D(w).line_on = 1;
  if (u >= 1.f) scriptFxRest(w);
}
void actUntie(World& w, float u) {
  if (u <= 0.f) scriptSetPose(w, POSE_CROUCH, 0.6f);
  if (u >= 0.6f) D(w).line_on = 0;
  if (u >= 1.f) scriptFxRest(w);
}

void doMount(World& w, float) {
  Hiker& h = w.hiker;
  h.facing = -1;
  slackWalkerMount(D(w).walker, h.feet.x, h.feet.y, -1);
}
void doWob0(World& w, float) { slackWalkerWobble(D(w).walker, 0.f); }
void doWob15(World& w, float) { slackWalkerWobble(D(w).walker, 0.15f); }
void doWob20(World& w, float) { slackWalkerWobble(D(w).walker, 0.2f); }
void doWob95(World& w, float) { slackWalkerWobble(D(w).walker, 0.95f); }
void doWalk3(World& w, float) {
  slackWalkerWalk(D(w).walker, D(w).line, 3);
  slackWalkerWobble(D(w).walker, 0.45f);
}
void doWalk6(World& w, float) {
  slackWalkerWalk(D(w).walker, D(w).line, 6);
  slackWalkerWobble(D(w).walker, 0.55f);
}
void doWalkAcross(World& w, float) {
  slackWalkerWobble(D(w).walker, 0.2f);
  slackWalkerWalk(D(w).walker, D(w).line, 0);
}
void doFlourish(World& w, float) {
  slackWalkerWobble(D(w).walker, 0.f);
  slackWalkerFlourish(D(w).walker);
}
void doFall(World& w, float) { slackWalkerFall(D(w).walker, D(w).rng); }
void doDismount(World& w, float) { slackWalkerDismount(D(w).walker); }

bool standing(const World& w) { return live(w) && D(w).walker.mode == SW_STAND; }
bool walkerOff(const World& w) { return live(w) && D(w).walker.mode == SW_OFF; }
// On the line and at the middle of it, walking on (B: the pose begins when the step in hand ends).
bool atMid(const World& w) {
  if (!live(w)) return false;
  const SlackWalker& k = D(w).walker;
  return k.mode == SW_WALK && (k.x - slacklineMidX(D(w).line)) * (float)k.dir >= -4.f;
}

// The squirrel or the chipmunk (the layout picks) starts at the trunk end of the line.
void doCritter(World& w, float) {
  DaySlack& s = D(w);
  const CritterKind kind = (((int)w.fire.pos.x) & 1) != 0 ? CR_CHIPMUNK : CR_SQUIRREL;
  const SlackCross plan = (M(w).var & 3u) == 0u ? SLACK_CROSS_HALF : SLACK_CROSS_BOTH;
  slackCritterInit(s.sc, s.line, kind, +1, plan, s.rng);
  critterInit(s.cr, kind, slackCritterFeet(s.sc, s.line), s.sc.dir);
  s.cr.speed_dial = 0.5f;
  s.cr.visible = true;
  s.crit_on = 1;
}
void doCritterOff(World& w, float) { D(w).crit_on = 0; }
bool critSitting(const World& w) { return live(w) && slackCritterSitting(D(w).sc); }
bool critDone(const World& w) { return live(w) && slackCritterDone(D(w).sc); }

#define STAND pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
// Out to the stake, crouch and tie it, along to where he steps up.
#define SET_UP STAND, go(AT_M0, 30.f), face(LEFT), act(actTie, 2.2f), pose(POSE_STAND, 0.7f, 0.4f), walk(AT_M1, 34.f), face(LEFT), still(0.6f)
// A try: up, a quiet moment, so many steps, a wobble, the fall; then a scratch of the head and back to the start.
#define TRY(walk_fn) doit(doMount), until(standing), doit(doWob15), still(1.2f), doit(walk_fn), until(standing), doit(doWob95), still(1.6f), \
                     doit(doFall), until(walkerOff)
#define BACK_TO_START pose(POSE_SCRATCH_HEAD, 0.4f, 1.8f), pose(POSE_STAND, 0.6f, 0.2f), walk(AT_M1, 34.f), face(LEFT), still(0.8f)
// The critter crosses while he watches from under the line, hands on hips once it sits.
#define CRITTER walk(AT_M2, 34.f), face(RIGHT), still(0.8f), doit(doCritter), until(critSitting), pose(POSE_HANDS_ON_HIPS, 0.7f, 0.4f), \
                until(critDone), doit(doCritterOff), pose(POSE_STAND, 0.6f, 0.3f)
// Out to the stake, untie it.
#define TAKE_DOWN go(AT_M0, 30.f), face(LEFT), act(actUntie, 2.2f), pose(POSE_STAND, 0.7f, 0.4f)

// A: falls and tries again.
const Step kSlackA[] = {
    SET_UP,
    TRY(doWalk3), BACK_TO_START,
    TRY(doWalk6),
    CRITTER, TAKE_DOWN, TO_SIT,
};
// B: all the way across, a pose in the middle, a soft step off.
const Step kSlackB[] = {
    SET_UP,
    doit(doMount), until(standing), doit(doWob20), still(1.0f), doit(doWalkAcross), until(atMid), doit(doFlourish), until(standing),
    still(0.8f), doit(doWalkAcross), until(standing), doit(doWob0), still(0.8f), doit(doDismount), until(walkerOff),
    CRITTER, TAKE_DOWN, TO_SIT,
};

#undef STAND
#undef TO_SIT
#undef SET_UP
#undef TRY
#undef BACK_TO_START
#undef CRITTER
#undef TAKE_DOWN

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------------

uint8_t slackPlanVar(uint32_t r) { return (uint8_t)((r >> 11) % 2u); }

// PROPOSED seconds: the script's length over a few seeds plus slack (tests/test_day_majors2.cpp reads how
// early the script is done and wants 3 s).
int slackSeconds(uint8_t var) {
  if (var & DAY_NOSHOW) return 46;
  return (var & 3u) == 0u ? 64 : 63;
}

const Step* slackScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  if ((var & 3u) == 1u) {
    n = (uint8_t)(sizeof(kSlackB) / sizeof(kSlackB[0]));
    return kSlackB;
  }
  n = (uint8_t)(sizeof(kSlackA) / sizeof(kSlackA[0]));
  return kSlackA;
}

void slackMajorBegin(World& w) {
  MajorState& m = M(w);
  const float g = (float)w.land.camp.base_y + kGroundBelowTrunk;
  const float bx = (float)w.land.camp_x - 4.f;   // the trunk's left face
  const float ax = bx - kSpan;
  m.p[0] = Vec2{ax + kTieIn, g};
  m.p[1] = Vec2{bx - kMountIn, g};
  m.p[2] = Vec2{ax + kWatchIn, g};
  m.p[3] = m.p[1];
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  w.day.kind = DAY_SLACK;
  if (dayWet(w)) m.var = (uint8_t)(m.var | DAY_NOSHOW);   // a rain the plan could not know of: he only looks at the sky
  DaySlack& s = D(w);
  s.rng.seed(seedOf(w));
  slacklineInit(s.line, Vec2{ax, g - kAnchorUp}, Vec2{bx, g - kAnchorUp}, g);
  slackWalkerInit(s.walker, s.rng);
}

void slackMajorTick(World& w, float dt) {
  if (!live(w)) return;
  DaySlack& s = D(w);
  if (!s.line_on) return;
  slackWalkerStep(s.walker, s.line, dt);
  if (s.crit_on) slackCritterStep(s.sc, s.line, dt);
  slacklineStep(s.line, dt);
}

void slackMajorEnd(World& w) {
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where it was)
}

}  // namespace ss
