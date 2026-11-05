// The kite as a surprise major. See day_kite.h.
//
// The script (kKiteA, kKiteB, kKiteC) is the hiker's half; the tick (kiteMajorTick) is the part that
// has to run every tick of the beat: the scripted wind, and variant B's pull. The kite's own step
// (kiteStep) runs from worldTick after the hiker, so the line leaves the hand he has now.
// Scratch use of MajorState, all private to this file:
//   p[0] his launch spot   p[1] where he walks to (the fallen kite; B: where he stops on his way in)
//   p[2] B: the dry lane's y at the right edge (p[2].y)
//   count   C: launches so far   u0   the scripted wind, 0..1 (0 none; the world step takes the
//   larger of it and the weather's)   u1   the time (m.t) the pull began   tc   the time the gust
//   cue fired   mt   B: how much of the loose line he has gathered in, 0..1
//   stage[0]   B: 0 flying, 1 dragged, 2 running on off the edge, 3 gone
// Cue bits: the script sets kGust, kBreeze; the tick sets kE1.
// No random draws: variation is the plan's byte, the layout, and hash of the layout.
#include "day_kite.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "day.h"
#include "director.h"
#include "kite.h"
#include "landscape.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

constexpr uint16_t kGust = 2, kBreeze = 4, kFlag = 32;
constexpr uint16_t kE1 = 256;

MajorState& M(World& w) { return w.major; }
const MajorState& M(const World& w) { return w.major; }
inline void fire(MajorState& m, uint16_t bit) { m.cue = (uint16_t)(m.cue | bit); }
inline void clear(MajorState& m, uint16_t bits) { m.cue = (uint16_t)(m.cue & ~bits); }
inline bool has(const MajorState& m, uint16_t bit) { return (m.cue & bit) != 0; }
uint8_t variantOf(const MajorState& m) { return (uint8_t)(m.var & 3u); }
float env(float u, float a, float b) { return scriptEnvelope(u, a, b); }
Vec2 handNow(const World& w) { return hikerHandPos(w.hiker, 0); }

// 0 before a, 1 on b..c, 0 after d.
float bump(float t, float a, float b, float c, float d) {
  return easeInOut(window(t, a, b)) * (1.f - easeInOut(window(t, c, d)));
}

// A repeatable value from the layout (no rng inside a beat): the kite's sway phase.
uint32_t seedOf(const World& w) {
  return (uint32_t)(w.fire.pos.x * 7.f) * 2654435761u + (uint32_t)(w.fire.pos.y * 13.f) * 40503u + 17u;
}

// ---- his actions ---------------------------------------------------------------------------------

// The folded kite appears in his near hand (a pocket of the jacket: it is small and thin folded).
void doOut(World& w, float) {
  KiteState& k = w.day.kite;
  kiteInit(k, w.hiker.feet.y, 1, seedOf(w));
  k.hand = handNow(w);
  kiteHold(k, 1.f, 0.f);
}

// He holds it up and shakes it out: unfolds over the action, the arm lifted a little to show it.
void actUnfold(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 0, e, 2.0f, 0.9f);
  w.fx.head = -0.1f * e;
  kiteHold(w.day.kite, 1.f - easeInOut(window(u, 0.15f, 0.9f)), 0.f);
  if (u >= 1.f) {
    scriptFxRest(w);
    kiteHold(w.day.kite, 0.f, 0.f);
  }
}

// A: he walks backward (facing the kite's way) so the line has room to pay out.
void actBackStep(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    w.fx.path_on = false;
    hikerWalkTo(h, h.feet.x - 44.f, 22.f);
  }
  h.facing = 1;
}

// How a launch goes: the toss (1 hard .. 0 a breeze) and the line to pay out.
void launchParams(const MajorState& m, float& toss, float& line) {
  const uint8_t v = variantOf(m);
  if (v == 0) {
    toss = 1.f;
    line = 230.f;
  } else if (v == 1) {
    toss = 1.f;
    line = 200.f;
  } else {   // C: the third lifts less and the line is shorter
    static const float kToss[3] = {1.f, 1.f, 0.7f}, kLine[3] = {180.f, 180.f, 150.f};
    const int i = m.count < 2 ? (int)m.count : 2;
    toss = kToss[i];
    line = kLine[i];
  }
}

// The throw: the arm goes up and forward, the kite leaves the hand a moment in.
void actToss(World& w, float u) {
  MajorState& m = M(w);
  if (u <= 0.f) clear(m, kFlag);
  const float e = env(u, 0.35f, 0.4f);
  scriptSetArm(w, 0, e, 2.5f, 0.5f);
  w.fx.lean = -0.05f * e;
  if (u >= 0.45f && !has(m, kFlag)) {
    float toss, line;
    launchParams(m, toss, line);
    kiteLaunch(w.day.kite, toss);
    kiteFly(w.day.kite, line);
    if (variantOf(m) == 2 && m.count < 255) ++m.count;
    fire(m, kFlag);
  }
  if (u >= 1.f) scriptFxRest(w);
}

// Standing with the line in his hands, looking up: the head tilts back and drifts a little.
void actWatchKite(World& w, float u) {
  const float e = env(u, 0.15f, 0.15f);
  w.fx.head = (-0.13f + 0.03f * sinT(u * 3.f)) * e;
  if (u >= 1.f) scriptFxRest(w);
}

// The first drops (KITE_NOSHOW): a hand out, palm up, a look at the sky.
void actPalm(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 0, e, 1.55f, 0.35f);
  w.fx.head = -0.2f * e;
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

// The gust (A: a puff; B: the one that wins) is the tick's; the script only starts it.
void doGust(World& w, float) {
  MajorState& m = M(w);
  m.tc = m.t;
  fire(m, kGust);
}
bool kiteFlying(const World& w) { return w.day.kite.mode == KITE_FLY; }
bool kiteHeld(const World& w) { return w.day.kite.mode == KITE_HELD; }
bool kiteDown(const World& w) { return w.day.kite.mode == KITE_GROUND && w.day.kite.t > 1.2f; }
bool pulledOff(const World& w) { return (M(w).cue & kE1) != 0; }

void doReel(World& w, float) { kiteReel(w.day.kite); }
// He folds it back up in his hands.
void actFold(World& w, float u) {
  kiteHold(w.day.kite, easeInOut(window(u, 0.1f, 0.9f)), 0.f);
  const float e = env(u, 0.3f, 0.3f);
  scriptSetArm(w, 0, e, 1.5f, 1.2f);
  w.fx.head = 0.12f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// Put away: into the jacket (it is small, folded, and he is sitting).
void doAway(World& w, float) {
  kiteHide(w.day.kite);
  M(w).u0 = 0.f;
}

// C: where he goes for the kite that came down, and the pick up.
void doPickSpot(World& w, float) {
  MajorState& m = M(w);
  const float x = w.day.kite.pos.x - 16.f;
  m.p[1] = Vec2{x, scriptGroundAt(w, x, m.p[0].y)};
}
void doPick(World& w, float) { kiteHold(w.day.kite, 0.f, 0.f); }
// The failure for this launch: a nosedive, a spin and a crash, a lift that drops.
void doFail(World& w, float) {
  const MajorState& m = M(w);
  if (m.count <= 1) kiteNosedive(w.day.kite);
  else if (m.count == 2) kiteSpinCrash(w.day.kite);
  else kiteDrop(w.day.kite);
}
// C: he sits down with it in his lap (the pose set on the first tick), then holds it there.
void actSitLap(World& w, float u) {
  if (u <= 0.f) scriptSetPose(w, POSE_SIT, 1.2f);
  kiteHold(w.day.kite, 0.f, easeInOut(window(u, 0.1f, 0.35f)));
  if (u >= 1.f) kiteHold(w.day.kite, 0.f, 1.f);
}
// ... and a breeze lifts it out of his hands.
void doBreeze(World& w, float) {
  MajorState& m = M(w);
  fire(m, kBreeze);
  kiteLaunch(w.day.kite, 0.f);
  kiteFly(w.day.kite, 200.f);
}
// He scrambles up after the line: up on his feet, then a few quick steps after it.
void actScramble(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) {
    clear(m, kFlag);
    scriptSetPose(w, POSE_FISH_STAND, 0.9f);
  }
  if (u >= 0.4f && !has(m, kFlag)) {
    fire(m, kFlag);
    w.fx.path_on = false;
    hikerWalkTo(h, h.feet.x + 36.f, 46.f);
  }
  h.facing = 1;
}

// B. The pull. The gust hits: he notices, then (tick) he is dragged off. This starts it.
void doPull(World& w, float) {
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  kitePull(w.day.kite);
  m.u1 = m.t;
  m.stage[0] = 1;
  // His way out is the dry bank: a straight run from here to the edge that stays out of the stream.
  const float ye = directorDryY(w, h.feet, 830.f);
  m.p[2] = Vec2{830.f, ye};
  w.fx.path_on = true;
  w.fx.path_x0 = h.feet.x;
  w.fx.path_y0 = h.feet.y;
  w.fx.path_x1 = 830.f;
  w.fx.path_y1 = ye;
}
// He comes back in from the right edge, along the same bank.
void doReenter(World& w, float) {
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  h.feet = Vec2{(float)SS_WIDTH + 70.f, m.p[2].y};
  h.facing = -1;
  h.walking = false;
  hikerSetPose(h, POSE_STAND, 0.f);
  const float x = m.p[0].x + 215.f;
  m.p[1] = Vec2{x, scriptGroundAt(w, x, m.p[0].y)};
  m.mt = 0.f;
}
// He winds the loose line in, hand over hand; the tick (kiteWorldStep) shortens it.
void actGather(World& w, float u) {
  MajorState& m = M(w);
  m.mt = easeInOut(window(u, 0.1f, 0.95f));
  const float s = sinT(u * 5.f);
  scriptSetArm(w, 0, 1.f, 1.15f + 0.35f * s, 1.0f);
  scriptSetArm(w, 1, 1.f, 1.15f - 0.35f * s, 1.0f);
  w.fx.head = 0.15f;
  if (u >= 1.f) {
    scriptFxRest(w);
    m.mt = 0.f;
    kiteHide(w.day.kite);
  }
}

#define STAND pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
// Up, the kite from his pocket, to his launch spot and shaken out.
#define INTRO STAND, doit(doOut), go(AT_M0, 30.f), face(RIGHT), still(0.3f), act(actUnfold, 1.6f), still(0.4f)
// Toss it; hold the line (the kite is up after 1.2 s).
#define LAUNCH act(actToss, 1.0f), pose(POSE_FISH_STAND, 0.6f, 0.2f)
// C: a failed try and the walk to where it lies, the pick up.
#define ATTEMPT act(actToss, 1.0f), doit(doFail), until(kiteDown), still(0.3f)
#define ATTEMPT_LATE act(actToss, 1.0f), still(0.5f), doit(doFail), until(kiteDown), still(0.3f)
#define PICKUP doit(doPickSpot), walk(AT_M1, 34.f), pose(POSE_CROUCH, 0.5f, 0.5f), doit(doPick), \
               pose(POSE_STAND, 0.6f, 0.1f), face(RIGHT), still(0.3f)
// Reel it in, fold it, sit down with it.
#define REEL_IN doit(doReel), until(kiteHeld), act(actFold, 1.8f), TO_SIT, doit(doAway)

// A, serene: back, toss, a long calm watch (a puff of wind), reel in.
const Step kKiteA[] = {
    INTRO,
    act(actBackStep, 2.2f), still(0.3f),
    LAUNCH, until(kiteFlying),
    act(actWatchKite, 9.0f),
    doit(doGust), act(actWatchKite, 9.0f),
    act(actWatchKite, 6.0f),
    REEL_IN,
};
// B, the kite wins: it flies a while, the gust, he digs in and is dragged off the right edge, a beat of
// empty scene, back in without it.
const Step kKiteB[] = {
    INTRO,
    LAUNCH, until(kiteFlying),
    act(actWatchKite, 6.0f),
    doit(doGust), act(actWatchKite, 1.8f),
    doit(doPull), until(pulledOff),
    still(3.4f),
    doit(doReenter), walk(AT_M1, 38.f),
    still(0.8f), act(actShrug, 2.2f), act(actGather, 2.8f), still(0.4f),
    TO_SIT,
};
// C, it won't fly: three failed tries, he sits with it in his lap and a breeze takes it.
const Step kKiteC[] = {
    INTRO,
    ATTEMPT, PICKUP,
    ATTEMPT, PICKUP,
    ATTEMPT_LATE, PICKUP,
    act(actSitLap, 5.6f), doit(doBreeze), still(0.9f),
    act(actScramble, 2.6f), pose(POSE_FISH_STAND, 0.5f, 0.1f),
    act(actWatchKite, 4.5f),
    REEL_IN,
};
// Rain, snow or a storm that the plan could not know of: the kite stays in his pocket.
const Step kKiteNone[] = {
    STAND,
    still(0.6f), act(actPalm, 3.0f), still(0.8f), act(actShrug, 2.4f), still(0.6f),
    TO_SIT,
};

#undef STAND
#undef TO_SIT
#undef INTRO
#undef LAUNCH
#undef ATTEMPT
#undef ATTEMPT_LATE
#undef PICKUP
#undef REEL_IN

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------

uint8_t kitePlanVar(uint32_t r) { return (uint8_t)((r >> 3) % 3u); }

// PROPOSED seconds: the script's length over a few seeds plus slack (tests/test_day_majors.cpp reads how
// early the script is done and wants 3 s).
int kiteSeconds(uint8_t var) {
  if (var & KITE_NOSHOW) return 46;
  switch (var & 3u) {
    case 0: return 45;
    case 1: return 57;
    default: return 61;
  }
}

const Step* kiteScript(uint8_t var, uint8_t& n) {
  if (var & KITE_NOSHOW) {
    n = (uint8_t)(sizeof(kKiteNone) / sizeof(kKiteNone[0]));
    return kKiteNone;
  }
  const int v = var & 3;
  if (v == 1) {
    n = (uint8_t)(sizeof(kKiteB) / sizeof(kKiteB[0]));
    return kKiteB;
  }
  if (v == 2) {
    n = (uint8_t)(sizeof(kKiteC) / sizeof(kKiteC[0]));
    return kKiteC;
  }
  n = (uint8_t)(sizeof(kKiteA) / sizeof(kKiteA[0]));
  return kKiteA;
}

void kiteMajorBegin(World& w) {
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  const float x = F.x + 62.f;
  m.p[0] = Vec2{x, scriptGroundAt(w, x, F.y - 10.f)};
  m.p[1] = m.p[2] = m.p[0];
  m.count = 0;
  m.u0 = m.u1 = m.mt = 0.f;
  m.stage[0] = 0;
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  w.day.kind = DAY_KITE;
  kiteInit(w.day.kite, m.p[0].y, 1, seedOf(w));
  const uint8_t wk = w.weather.kind;
  if (wk == WX_RAIN || wk == WX_SNOW || wk == WX_STORM) m.var = (uint8_t)(m.var | KITE_NOSHOW);
}

void kiteMajorTick(World& w, float) {
  MajorState& m = M(w);
  const uint8_t var = variantOf(m);
  if (m.var & KITE_NOSHOW) return;
  Hiker& h = w.hiker;
  KiteState& k = w.day.kite;
  // The scripted wind: A a puff, B the gust that wins, C the breeze that lifts it from his lap.
  float g = 0.f;
  if (has(m, kGust)) {
    const float t = m.t - m.tc;
    if (var == 0) g = 0.3f + 0.45f * bump(t, 0.f, 2.f, 6.f, 9.f);
    else if (var == 1) g = 0.3f + 0.7f * easeInOut(window(t, 0.f, 1.4f));
  }
  if (has(m, kBreeze)) g = 0.45f;
  m.u0 = g;
  if (var != 1 || m.stage[0] == 0) return;

  // B: the kite drags him. He digs in (leans back), skids, then runs; the speed is the kite's.
  HikerFx& fx = w.fx;
  const float tp = m.t - m.u1;
  if (m.stage[0] == 1) {
    const float v = kitePullSpeed(k);
    const float dig = easeInOut(window(tp, 0.05f, 0.45f)) * (1.f - easeInOut(window(tp, 1.5f, 2.6f)));
    fx.lean = -0.2f * dig + 0.07f * easeInOut(window(tp, 2.4f, 4.f));
    scriptSetArm(w, 0, 1.f, 1.9f, 0.45f);   // both hands on the line
    scriptSetArm(w, 1, 1.f, 1.75f, 0.5f);
    if (v > 0.5f) hikerWalkTo(h, 1400.f, v);
    if (k.hand.x > 830.f) {   // far off the edge: the kite is gone, the loose line trails
      kiteLose(k);
      m.stage[0] = 2;
    }
  } else if (m.stage[0] == 2) {   // runs on until he is well out of sight
    fx.lean = 0.07f;
    hikerWalkTo(h, 1400.f, 70.f);
    if (h.feet.x > 905.f) {
      hikerSetPose(h, POSE_STAND, 0.f);
      h.walking = false;
      scriptFxRest(w);
      m.stage[0] = 3;
      fire(m, kE1);
    }
  }
}

void kiteMajorEnd(World& w) {
  if (w.day.kind == DAY_KITE) kiteHide(w.day.kite);
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where it was)
  M(w).u0 = 0.f;
  M(w).mt = 0.f;
}

// ---- the world's step ---------------------------------------------------------------------------------

namespace {
constexpr float kTrailPx = 56.f;   // the loose line's length behind his hand: keep in step with kite.cpp kTrail
}

void kiteWorldStep(World& w, bool frozen) {
  if (w.day.kind != DAY_KITE) return;
  KiteState& k = w.day.kite;
  if (k.mode == KITE_HIDDEN) return;
  const Hiker& h = w.hiker;
  k.ground_y = h.feet.y;   // where it comes down is where he stands
  const Vec2 hand = handNow(w);
  float wind = kiteWindFrom(w.weather.wind, w.weather.gust);
  const bool mine = w.major.on && w.major.id == MJ_KITE;
  if (mine && w.major.u0 > wind) wind = w.major.u0;
  kiteStep(k, hand, wind, frozen ? 0.f : SS_TICK_DT);
  if (frozen && k.mode == KITE_HELD) {   // in his hand: it goes where the hand goes (kite.cpp stepHeld's `to`)
    const float d = (float)k.dir;
    const float grip = lerp(10.f, 0.f, k.lay);
    k.pos = Vec2{k.hand.x + sinT(k.rot) * grip + d * 4.f * k.lay, k.hand.y - cosT(k.rot) * grip - k.lay};
  }
  if (mine && k.mode == KITE_GONE && w.major.mt > 0.f) {   // B: he winds the loose line in
    const float u = w.major.mt;
    k.end = Vec2{lerp(hand.x + (float)k.dir * kTrailPx, hand.x, u), lerp(k.ground_y - 1.f, hand.y, u)};
  }
}

}  // namespace ss
