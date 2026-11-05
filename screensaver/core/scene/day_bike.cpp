// The mountain bike as a surprise major. See day_bike.h.
//
// The script (kBikeA, kBikeB, kBikeC) is the hiker's half; while the module drives him (bikeDriving) it
// only waits (until). The tick (bikeMajorTick) steps the bike and its water every tick of the beat; the
// pose overlay (bikeApplyRider) is dayPose's, in the World's hiker step (day.h).
// Scratch use of MajorState, private to this file: p[2] where he crouches over the fallen bike.
// No random draws: the water's stream is its own Rng (DayBike::rng) seeded from the layout.
#include "day_bike.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "bike.h"
#include "day.h"
#include "landscape.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

MajorState& M(World& w) { return w.major; }
float env(float u, float a, float b) { return scriptEnvelope(u, a, b); }

// ---- tuning (PROPOSED; how it looks is UNVERIFIED) ---------------------------------------------------------

constexpr float kLaneBehindFire = 28.f;   // the dry lane runs this far above the fire pit's y
constexpr float kWaterDepth = 0.4f;       // the wet lane rides this fraction of the half width above the stream's centre
constexpr float kDipX = 210.f;            // the lane leaves the camp ground here, down the bank into the water
constexpr float kBankTopX = 500.f;        // and is up the far bank here, short of the chip
constexpr float kBankGap = 8.f;           // the bank lane runs this far above the water's edge
constexpr float kRockIn = 24.f;           // B: the rock stands this far into the water (the dive carries him about 200 px)
constexpr float kWheelieX = 40.f;         // C: where the wheelie pops (dir +1)
constexpr float kWalkOff = 38.f;          // px/s he goes off to fetch the bike
constexpr float kChipClearY = 396.f;      // the lane's lowest y right of x 500: wheels and splash stay above y 412
constexpr float kSplashMaxX = 400.f;      // no spray right of this: the bits fly about 60 px and the chip starts at 538

uint64_t seedOf(const World& w) {
  return ((uint64_t)(uint32_t)(w.fire.pos.x * 16.f) << 24) ^ (uint64_t)(uint32_t)(w.fire.pos.y * 16.f) ^ 0xB1CE0000ull;
}

// The lane over the layout: along behind the fire pit, down the bank into the stream in the middle
// of the frame, up the far side and along the bank just above the water to the right edge. The
// stream climbs to the right: on the left it runs along the bottom edge (spray and B's dive would
// leave the frame), and on the right it rises to the camp ground's height, so a lane kept level
// there would be in the water, and the spray would fall past the status chip's corner (chip.h: x from
// 538, y from 412). So the only water is the dip in the middle: the spray flies about 60 px and B's
// dive about 200 px from the rock, both short of the chip.
void laneFor(const World& w, Vec2* pts) {
  const float yb = fminf(w.fire.pos.y - kLaneBehindFire, kChipClearY);
  auto wet = [&](float x, float fallback) {
    float cy = 0.f, half = 0.f;
    return landscapeStreamSpan(w.land, x, cy, half) ? cy - kWaterDepth * half : fallback;
  };
  auto bank = [&](float x) {   // dry ground just above the water, never lower than the camp lane
    float cy = 0.f, half = 0.f;
    return landscapeStreamSpan(w.land, x, cy, half) ? fminf(yb, cy - half - kBankGap) : yb;
  };
  pts[0] = Vec2{-110.f, yb};
  pts[1] = Vec2{kDipX, yb};
  pts[2] = Vec2{kDipX + 80.f, wet(kDipX + 80.f, yb)};
  pts[3] = Vec2{kDipX + 170.f, wet(kDipX + 170.f, yb)};
  pts[4] = Vec2{kBankTopX, bank(kBankTopX)};
  pts[5] = Vec2{620.f, bank(620.f)};
  pts[6] = Vec2{740.f, bank(740.f)};
  pts[7] = Vec2{920.f, bank(790.f)};
}

// ---- his actions -----------------------------------------------------------------------------------------------

DayBike& D(World& w) { return w.day.bike; }
bool live(const World& w) { return w.day.kind == DAY_BIKE; }

// Out of sight: the pack comes off for the ride, nothing leads him now.
void doGone(World& w, float) {
  w.fx.path_on = false;
  w.hiker.walking = false;
  w.hiker.pack_on = false;
}
void doRockOn(World& w, float) { bikeRock(D(w).bike, true); }
void doRide1(World& w, float) { bikeRide(D(w).bike, 1, BIKE_VAR_PASS, false); }
void doRide2(World& w, float) { bikeRide(D(w).bike, -1, BIKE_VAR_PASS, true); }
void doRideB(World& w, float) { bikeRide(D(w).bike, 1, BIKE_VAR_ROCK, false); }
void doRideC(World& w, float) { bikeRide(D(w).bike, 1, BIKE_VAR_WHEELIE, false); }

bool bikeAway(const World& w) { return live(w) && w.day.bike.bike.mode == BIKE_AWAY; }
bool rideDone(const World& w) { return live(w) && bikeDone(w.day.bike.bike); }

// A: he is off the left edge on his own two feet again, the pack back on, to walk in.
void doReturn(World& w, float) {
  Hiker& h = w.hiker;
  bikeFreeRider(D(w).bike);
  h.feet = scriptAnchor(w, AT_EDGE_L);
  h.facing = 1;
  h.walking = false;
  h.pack_on = w.day.pack_was != 0;
  hikerSetPose(h, POSE_STAND, 0.f);
}

// B: he gets up out of the water, dripping (the waterline is the World's, fx.wade).
void doStandB(World& w, float) {
  bikeFreeRider(D(w).bike);
  w.fx.wade = true;
  scriptSetPose(w, POSE_STAND, 1.4f);
}
// C: he sits up off his back; the bike stays where it fell until he picks it up.
void doSitUpC(World& w, float) {
  bikeFreeRider(D(w).bike);
  scriptSetPose(w, POSE_SIT, 1.1f);
}

// Where he crouches to pick it up: B beside the middle of the bike in the water (nearer us than the rock),
// C over the bike where he lay.
void doSpotB(World& w, float) {
  const BikeState& b = D(w).bike;
  const float x = b.pivot.x + 30.f;
  M(w).p[2] = Vec2{x, bikeLaneY(b.course, x) + 8.f};
}
void doSpotC(World& w, float) {
  const BikeState& b = D(w).bike;
  M(w).p[2] = Vec2{b.pivot.x + 34.f, w.hiker.feet.y + 6.f};
}
void doHide(World& w, float) { bikeHide(D(w).bike); }   // (picked up: it goes)
void doWadeOff(World& w, float) { w.fx.wade = false; }
void doPackBack(World& w, float) { w.hiker.pack_on = w.day.pack_was != 0; }

// Shaking the water off: both arms swinging, a quick head shake.
void actShake(World& w, float u) {
  const float e = env(u, 0.15f, 0.2f);
  const float s = sinT(u * 7.f);
  scriptSetArm(w, 0, e, 0.55f + 0.35f * s, 0.35f);
  scriptSetArm(w, 1, e, 0.55f - 0.35f * s, 0.35f);
  w.fx.head = 0.12f * e * s;
  w.fx.lean = 0.04f * e * s;
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

#define STAND pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
// To fetch the bike: off the left edge, out of sight.
#define FETCH STAND, go(AT_EDGE_L, kWalkOff), doit(doGone)
#define PICK_UP walk(AT_M2, 30.f), face(LEFT), pose(POSE_CROUCH, 0.6f, 1.4f), doit(doHide), pose(POSE_STAND, 0.7f, 0.6f)

// A: through the stream and out the right edge, a moment of empty scene, back proud and off the left edge.
const Step kBikeA[] = {
    FETCH, still(2.2f),
    doit(doRide1), until(bikeAway), still(1.8f),
    doit(doRide2), until(bikeAway),
    doit(doReturn), TO_SIT,
};
// B: the rock; over the handlebars into the water; up, dripping, a shrug; he picks the bike out of the stream.
const Step kBikeB[] = {
    FETCH, doit(doRockOn), still(2.6f),
    doit(doRideB), until(rideDone), still(2.8f),
    doit(doStandB), still(1.6f), act(actShake, 2.6f), still(0.6f), act(actShrug, 2.2f), still(0.4f),
    doit(doSpotB), PICK_UP,
    go(AT_SIT, 26.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f), doit(doWadeOff), doit(doPackBack),
};
// C: the wheelie that tips over; on his back under the bike, its wheels spinning; sits up, a scratch of the
// head, stands, picks it up.
const Step kBikeC[] = {
    FETCH, still(2.6f),
    doit(doRideC), until(rideDone), still(3.0f),
    doit(doSitUpC), still(2.0f), pose(POSE_SCRATCH_HEAD, 0.7f, 1.6f), pose(POSE_STAND, 1.0f, 0.5f), act(actShrug, 2.2f), still(0.4f),
    doit(doSpotC), PICK_UP,
    doit(doPackBack), TO_SIT,
};

#undef STAND
#undef TO_SIT
#undef FETCH
#undef PICK_UP

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------------

uint8_t bikePlanVar(uint32_t r) { return (uint8_t)((r >> 7) % 3u); }

// PROPOSED seconds: the script's length over a few seeds plus slack (tests/test_day_majors2.cpp reads how
// early the script is done and wants 3 s).
int bikeSeconds(uint8_t var) {
  if (var & DAY_NOSHOW) return 46;
  switch (var & 3u) {
    case 0: return 58;
    case 1: return 56;
    default: return 57;
  }
}

const Step* bikeScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  const int v = var & 3;
  if (v == 1) {
    n = (uint8_t)(sizeof(kBikeB) / sizeof(kBikeB[0]));
    return kBikeB;
  }
  if (v == 2) {
    n = (uint8_t)(sizeof(kBikeC) / sizeof(kBikeC[0]));
    return kBikeC;
  }
  n = (uint8_t)(sizeof(kBikeA) / sizeof(kBikeA[0]));
  return kBikeA;
}

void bikeMajorBegin(World& w) {
  MajorState& m = M(w);
  m.p[2] = w.hiker.feet;
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  w.day.kind = DAY_BIKE;
  w.day.pack_was = w.hiker.pack_on ? 1 : 0;
  if (dayWet(w)) m.var = (uint8_t)(m.var | DAY_NOSHOW);   // a rain the plan could not know of: he only looks at the sky
  DayBike& d = D(w);
  bikeInit(d.bike, d.splash, w.hiker.height);
  Vec2 pts[8];
  laneFor(w, pts);
  BikeCourse c;
  if ((m.var & 3u) == 2u) {   // C: the dry flat all the way, the wheelie at its own spot
    pts[1] = Vec2{920.f, pts[0].y};
    bikeCourseFit(c, w.land, pts, 2, 1);
    c.water_x0 = c.water_x1 = 0.f;
    c.rock_x = 0.f;
    c.wheelie_x = kWheelieX;
  } else {
    bikeCourseFit(c, w.land, pts, 8, 1);
    // The splash and B's dive keep left of the status chip's corner (x from 538): no spray past
    // kSplashMaxX (a backstop: the lane is out of the water by kBankTopX), the rock early in the water.
    if (c.water_x1 > kSplashMaxX) c.water_x1 = kSplashMaxX;
    if (c.water_x1 > c.water_x0) c.rock_x = fminf(c.water_x0 + kRockIn, c.water_x1 - 10.f);
  }
  bikeSetCourse(d.bike, c);
  d.rng.seed(seedOf(w));
}

void bikeMajorTick(World& w, float dt) {
  if (!live(w)) return;
  DayBike& d = D(w);
  bikeStep(d.bike, d.splash, d.rng, dt);
}

void bikeMajorEnd(World& w) {
  if (live(w)) {
    w.hiker.pack_on = w.day.pack_was != 0;
    bikeFreeRider(D(w).bike);
  }
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where it was)
}

}  // namespace ss
