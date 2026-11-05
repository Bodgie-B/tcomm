// The ropes course as a surprise major. See day_ropes.h.
//
// The script (kRopes) is short: walk to the start, doit(doStart), look round while the posts go up,
// until(ropesDone), walk back. The module does the rest; its step (ropesStep: time, props, and his
// commands) runs from the tick before the hiker's step, its pose overlay (ropesPose) is dayPose's.
// Scratch use of MajorState, private to this file: p[0] the way round the tent   p[1] the start, 40 px
// left of the ramp.
// No random draws: the module's two values come from DayRopes::rng, seeded from the layout.
#include "day_ropes.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "day.h"
#include "ropes.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

MajorState& M(World& w) { return w.major; }

// ---- tuning (PROPOSED; how it looks is UNVERIFIED) ---------------------------------------------------------

constexpr float kGroundAbovePine = 6.f;   // the posts stand this far above the camp pine's foot
constexpr float kRightOfTent = 28.f;      // the ramp's toe: this far right of the tent's centre
constexpr float kTrunkClear = 22.f;       // the last post stays this far left of the trunk's left face
constexpr float kStartLeft = 40.f;        // he starts this far left of the ramp (ropes.h)

uint64_t seedOf(const World& w) {
  return ((uint64_t)(uint32_t)(w.fire.pos.x * 16.f) << 23) ^ (uint64_t)(uint32_t)(w.fire.pos.y * 16.f) ^ 0x80BE0000ull;
}

DayRopes& D(World& w) { return w.day.ropes; }
bool live(const World& w) { return w.day.kind == DAY_ROPES; }

void doStart(World& w, float) {
  w.fx.path_on = false;
  w.hiker.walking = false;
  const uint8_t v = (uint8_t)(M(w).var & 3u);
  ropesStart(D(w).r, v > 2 ? 0 : v, D(w).rng);
}
bool courseDone(const World& w) { return live(w) && ropesDone(w.day.ropes.r); }

#define STAND pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)

const Step kRopes[] = {
    STAND, go(AT_M0, 30.f), go(AT_M1, 30.f), face(RIGHT), still(0.6f),
    doit(doStart), pose(POSE_LOOK_AROUND, 0.7f, 2.4f), until(courseDone),
    still(0.8f), TO_SIT,
};

#undef STAND
#undef TO_SIT

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------------

// A a tenth of the time, B half and C two fifths: A (about 105 s with the walks) is the longest beat there is.
uint8_t ropesPlanVar(uint32_t r) {
  const uint32_t k = (r >> 13) % 10u;
  return k < 1u ? (uint8_t)0 : (k < 6u ? (uint8_t)1 : (uint8_t)2);
}

// PROPOSED seconds: the module's own lengths (A 83 to 87 s, B 61 to 63, C 75 to 77, from a standing start) plus the
// walks and slack (tests/test_day_majors2.cpp reads how early the script is done and wants 3 s).
int ropesSeconds(uint8_t var) {
  if (var & DAY_NOSHOW) return 46;
  switch (var & 3u) {
    case 0: return 107;
    case 1: return 79;
    default: return 98;
  }
}

const Step* ropesScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  n = (uint8_t)(sizeof(kRopes) / sizeof(kRopes[0]));
  return kRopes;
}

void ropesMajorBegin(World& w) {
  MajorState& m = M(w);
  const float gy = (float)w.land.camp.base_y - kGroundAbovePine;
  const float x0r = (float)w.land.camp_x - 4.f - kTrunkClear - kRopesWidth;
  const float x0l = w.spine.camp.tent.x + kRightOfTent;
  const float x0 = x0l < x0r ? x0l : x0r;
  m.p[0] = Vec2{w.spine.camp.tent.x + 66.f, gy + 8.f};   // round the tent's right side, behind it
  m.p[1] = Vec2{x0 - kStartLeft, gy};
  m.p[2] = m.p[3] = m.p[1];
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  w.day.kind = DAY_ROPES;
  if (dayWet(w)) m.var = (uint8_t)(m.var | DAY_NOSHOW);   // a rain the plan could not know of: he only looks at the sky
  DayRopes& d = D(w);
  ropesInit(d.r, x0, gy);
  d.rng.seed(seedOf(w));
}

void ropesMajorTick(World& w, float dt) {
  if (!live(w)) return;
  ropesStep(D(w).r, w.hiker, dt);
}

void ropesMajorEnd(World& w) {
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where it was)
}

}  // namespace ss
