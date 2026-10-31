// The Lookout (SF_LOOKOUT): the sunset set piece. SPINES_WEATHER_LOOKOUT_SPEC.md
// section 5, brief section 7. See lookout.h; the landscape and the tower are
// lookout_land.cpp.
//
// Same idioms as the spine families (spine_midday.cpp): actions take u 0..1
// and leave him at rest at u = 1; scripts are const Step tables run by the
// spine's interpreter (script.h); a beat's seconds are longer than its script
// (an overrun counts in SpineState::overruns, tests require zero). One rng
// call per statement, table sines, no doubles, no allocation, no static
// mutable state.
//
// Timeline: build, approach, trail_pause, reveal, pause, climb, pause,
// explore, pause, settle, sunset, fade. No fillers, majors or minors
// (World::busy is set for the whole loop). Length 250 to 320 s: the sunset
// stretched within 90 to 120 s first, then the three pauses (lookoutBeginLoop).
//
// Positions: the scripts walk between two scratch points (SpineState::fp,
// the AT_FAM0 / AT_FAM1 anchors) that each beat's actions set from the
// landscape's accessors (the trail's vertices, the stair's knots, the cab's
// points). Walks only move x and his feet follow the straight line to the
// target, which is exactly a trail leg or a stair flight; his height and pace
// are set every tick from where he is (lookoutHikerPre).
//
// The palette's mood is the scene's own (World::mood_override): 4.0 from loop
// start, stepped to 5.9 through the sunset in 10 to 12 steps, then 0.0 and 0.1
// in the fade; each step is a palette change and a cache rebuild.
#include "lookout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../anim/ease.h"
#include "../ss_app.h"
#include "script.h"
#include "spine_family.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float envelope(float u, float a, float b) { return scriptEnvelope(u, a, b); }
inline void setArm(World& w, int i, float k, float sh, float el) { scriptSetArm(w, i, k, sh, el); }
inline void fxRest(World& w) { scriptFxRest(w); }
inline void setPose(World& w, PoseId p, float s) { scriptSetPose(w, p, s); }
inline bool crossed(float u, float u0, float at) { return u >= at && u0 < at; }

constexpr float kWalkV = 30.f;        // nominal walk speed in the scripts (the pace is set per tick)
constexpr float kStairPace = 0.2f;    // heights per second on a flight: deliberate steps
constexpr float kDeckPace = 0.4f;     // on the landings and the deck
constexpr float kFadeS = 2.5f;        // the fade's last seconds go to black
constexpr float kSitDrop = 0.215f;    // POSE_PERCH: his feet hang this much of his height below the seat
constexpr float kMoodFrom = 4.f, kMoodTo = 5.9f;
constexpr int kAimMin = 255, kAimMax = 315;   // the loop's length, s (policy 250 to 320, with a margin)
constexpr uint8_t kSunMin = 90, kSunMax = 120, kPauseMin = 2, kPauseMax = 12;

// Glyph slots in World::major (no major runs in a Lookout loop).
enum : int { G_RAILBIRD = 0, G_CHIP = 1, G_TAP = 2, G_STAR = 3 };

enum BirdMode : uint8_t { BM_NONE = 0, BM_HAWK, BM_CROSS, BM_IN, BM_CIRCLE, BM_DOWN };
enum RailMode : uint8_t { RB_NONE = 0, RB_IN, RB_SIT, RB_OFF, RB_GONE };
enum ChipMode : uint8_t { CH_NONE = 0, CH_IN, CH_SIT, CH_OFF, CH_GONE };

// Fixed beat seconds (the approach and the climb are fitted to the trail).
constexpr uint8_t kTrailPauseS = 8, kRevealS = 12, kExploreS = 36, kSettleS = 20, kFadeBeatS = 10;

// The build: phases (s) sky, valley, trail, plants, hold; the end.
const float kPhase[6] = {0.f, 1.8f, 4.6f, 7.4f, 11.6f, SS_BUILD_SECONDS};
constexpr float kValleyDrop = 110.f;   // px the valley layers rise from behind the hill

const char* const kBeatNames[LB_COUNT] = {"approach", "trail_pause", "reveal", "climb",
                                          "explore",  "settle",      "sunset", "fade"};

// ---- places ------------------------------------------------------------------------------

Vec2 knot(const World& w, int i) {
  Vec2 k[6];
  float s[6];
  lookoutStairKnots(w.lk, k, s);
  return k[i];
}

LookoutCabPoints cabPoints(const World& w) {
  LookoutCabPoints c;
  lookoutCabPoints(w.lk, c);
  return c;
}

Vec2 towerP(const World& w, float dx, float dy) { return Vec2{(float)w.lk.tower_x + dx, (float)w.lk.tower_gy + dy}; }

void targetTo(World& w, Vec2 p) { w.spine.fp[1] = p; }

// ---- approach ----------------------------------------------------------------------------

void doTrailStart(World& w, float) {
  LookoutState& L = w.look;
  L.leg = 1;
  L.speed_k = L.walk_k;
  L.h_min = 90.f;
  w.spine.fp[0] = w.lk.trail.p[0];
  targetTo(w, w.lk.trail.p[1]);
}
void doNextLeg(World& w, float) {
  LookoutState& L = w.look;
  ++L.leg;
  if (L.leg <= L.pause_at) targetTo(w, w.lk.trail.p[L.leg]);
}
bool moreLegs(const World& w) { return w.look.leg <= w.look.pause_at; }

// ---- trail_pause -------------------------------------------------------------------------

// Out of breath: bent forward a little, the chest heaving, then upright.
void actBreathe(World& w, float u) {
  const float e = envelope(u, 0.2f, 0.3f);
  w.fx.lean = e * (0.16f + 0.05f * sinT(u * 4.f));
  w.fx.head = 0.18f * e;
  if (u >= 1.f) fxRest(w);
}
// Head up toward the ridge (the hand shading his eyes is POSE_LOOK_AROUND).
void actLookUp(World& w, float u) {
  w.fx.head = -0.22f * envelope(u, 0.15f, 0.2f);
  if (u >= 1.f) fxRest(w);
}

// ---- reveal ------------------------------------------------------------------------------

void actWatchTower(World& w, float u) {
  w.fx.head = -0.3f * envelope(u, 0.1f, 0.15f);
  w.fx.lean = -0.05f * envelope(u, 0.1f, 0.15f);
  if (u >= 1.f) fxRest(w);
}

// ---- climb -------------------------------------------------------------------------------

void doLastLeg(World& w, float) {
  LookoutState& L = w.look;
  L.speed_k = L.walk_k;
  targetTo(w, w.lk.trail.p[w.lk.trail.n - 1]);
  if (L.rail_mode == RB_NONE) {   // a bird comes down on the deck's rail
    L.rail_mode = RB_IN;
    L.rail_t = 0.f;
  }
}
void flightTo(World& w, int k) {
  w.look.climb_to = 1.f;
  w.look.speed_k = kStairPace;
  targetTo(w, knot(w, k));
}
void landingTo(World& w, int k) {
  w.look.climb_to = 0.f;
  w.look.speed_k = kDeckPace;
  targetTo(w, knot(w, k));
}
void doFlight1(World& w, float) { flightTo(w, 1); }
void doLanding1(World& w, float) { landingTo(w, 2); }
// He moves on: the bird on the rail leaves.
void doFlight2(World& w, float) {
  flightTo(w, 3);
  if (w.look.rail_mode == RB_SIT || w.look.rail_mode == RB_IN) {
    w.look.rail_mode = RB_OFF;
    w.look.rail_t = 0.f;
  }
}
void doLanding2(World& w, float) { landingTo(w, 4); }
void doFlight3(World& w, float) { flightTo(w, 5); }
void doDeck(World& w, float) {
  w.look.climb_to = 0.f;
  w.look.speed_k = kDeckPace;
}
// On the landing, looking out over the valley (west): still, the head a touch up.
void actLookValley(World& w, float u) {
  w.fx.head = -0.1f * envelope(u, 0.15f, 0.15f);
  if (u >= 1.f) fxRest(w);
}

// ---- explore -----------------------------------------------------------------------------

void doPeekFrontRight(World& w, float) {
  w.look.speed_k = kDeckPace;
  if (w.look.chip_mode == CH_NONE) {
    w.look.chip_mode = CH_IN;
    w.look.chip_t = 0.f;
  }
  targetTo(w, towerP(w, 22.f, -(float)LK_LEG_H));
}
void doPeekFrontLeft(World& w, float) { targetTo(w, towerP(w, -16.f, -(float)LK_LEG_H)); }
void doWestPeek(World& w, float) { targetTo(w, cabPoints(w).peek_feet[2]); }
void doEastEnd(World& w, float) { targetTo(w, towerP(w, 37.5f, -(float)LK_LEG_H)); }   // past the wall (+30): the door
void doInside(World& w, float) {
  w.look.inside = true;
  targetTo(w, towerP(w, 9.f, -(float)LK_LEG_H));
}
void doOutside(World& w, float) { w.look.inside = false; }
void doToCompass(World& w, float) { targetTo(w, towerP(w, 25.f, -(float)LK_LEG_H)); }

// Face to the glass: leaning in, head forward.
void actPeek(World& w, float u) {
  const float e = envelope(u, 0.25f, 0.25f);
  w.fx.lean = 0.24f * e;
  w.fx.head = 0.22f * e;
  if (u >= 1.f) fxRest(w);
}
// A hand on the fire-finder's table top (ahead and down from his shoulder), the head bent over it.
void actFinder(World& w, float u) {
  const float e = envelope(u, 0.2f, 0.2f);
  w.fx.reach = e;
  w.fx.reach_to = 1.0f;
  w.fx.head = 0.3f * e;
  w.fx.lean = 0.08f * e;
  if (u >= 1.f) fxRest(w);
}
// Two taps on the compass plate on the rail, each a little ring of arcs.
constexpr float kTapS = 2.6f;
float plateauTap(float s) {
  const float a = window(s, 0.8f, 1.f) * (1.f - window(s, 1.f, 1.2f));
  const float b = window(s, 1.4f, 1.6f) * (1.f - window(s, 1.6f, 1.8f));
  return a + b;
}
void actTap(World& w, float u) {
  const float s = u * kTapS;
  const float e = envelope(u, 0.2f, 0.2f);
  const float tap = plateauTap(s);
  setArm(w, 0, e, 0.42f - 0.1f * tap, 0.05f);
  w.fx.head = 0.15f * e;
  if (crossed(s, w.spine.u0, 1.0f) || crossed(s, w.spine.u0, 1.6f)) w.look.tap_t = 0.45f;
  w.spine.u0 = s;
  if (u >= 1.f) {
    fxRest(w);
    w.spine.u0 = 0.f;
  }
}
// Looking down over the rail at the chipmunk on the frame below.
void actLookDown(World& w, float u) {
  w.fx.head = 0.38f * envelope(u, 0.2f, 0.2f);
  w.fx.lean = 0.1f * envelope(u, 0.2f, 0.2f);
  if (u >= 1.f) fxRest(w);
}

// ---- settle ------------------------------------------------------------------------------

void doToSeat(World& w, float) {
  if (w.look.chip_mode == CH_SIT || w.look.chip_mode == CH_IN) {   // the chipmunk goes when he does
    w.look.chip_mode = CH_OFF;
    w.look.chip_t = 0.f;
  }
  targetTo(w, lookoutRailSeat(w.lk));
}
// Down onto the deck's edge, legs over: the pose blends while his feet go down
// below the seat, so the seat stays on the deck.
constexpr float kSitS = 1.8f;
void actSitEdge(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    setPose(w, POSE_PERCH, kSitS);
    w.spine.v0 = h.feet;
  }
  h.feet.y = w.spine.v0.y + kSitDrop * h.height * easeInOut(u);
}

// ---- scripts -----------------------------------------------------------------------------

const Step kApproach[] = {
    doit(doTrailStart),
    place(AT_FAM0, RIGHT),
    walk(AT_FAM1, kWalkV),
    doit(doNextLeg),
    repeatWhile(moreLegs, 2),
};

const Step kTrailPause[] = {
    still(0.3f),
    face(RIGHT),
    act(actBreathe, 2.8f),
    pose(POSE_LOOK_AROUND, 0.7f, 0.f),
    act(actLookUp, 2.4f),
    pose(POSE_STAND, 0.7f, 0.f),
};

const Step kReveal[] = {
    act(actWatchTower, 7.f),
    pose(POSE_HANDS_ON_HIPS, 0.8f, 2.4f),
    pose(POSE_STAND, 0.7f, 0.f),
};

constexpr float kLookValleyS = 6.f;
const Step kClimb[] = {
    doit(doLastLeg),  walk(AT_FAM1, kWalkV),
    doit(doFlight1),  walk(AT_FAM1, kWalkV),
    doit(doLanding1), walk(AT_FAM1, kWalkV),
    face(LEFT),       act(actLookValley, kLookValleyS),
    doit(doFlight2),  walk(AT_FAM1, kWalkV),
    doit(doLanding2), walk(AT_FAM1, kWalkV),
    doit(doFlight3),  walk(AT_FAM1, kWalkV),
    doit(doDeck),     still(1.f),
};

const Step kExplore[] = {
    doit(doPeekFrontRight), walk(AT_FAM1, kWalkV), face(RIGHT), act(actPeek, 3.f),
    doit(doPeekFrontLeft),  walk(AT_FAM1, kWalkV), face(RIGHT), act(actPeek, 3.f),
    doit(doWestPeek),       walk(AT_FAM1, kWalkV), face(RIGHT), act(actPeek, 3.f),
    doit(doEastEnd),        walk(AT_FAM1, kWalkV),
    doit(doInside),         walk(AT_FAM1, kWalkV), face(LEFT),  act(actFinder, 4.f), still(1.5f),
    doit(doEastEnd),        walk(AT_FAM1, kWalkV), doit(doOutside),
    doit(doToCompass),      walk(AT_FAM1, kWalkV), face(RIGHT), act(actTap, kTapS),
    face(LEFT),             act(actLookDown, 3.f),
};

const Step kSettle[] = {
    doit(doToSeat), walk(AT_FAM1, kWalkV), face(LEFT), act(actSitEdge, kSitS), still(1.f),
};

struct Script {
  const Step* steps;
  uint8_t n;
};
#define SCRIPT(a) Script{a, (uint8_t)(sizeof(a) / sizeof(a[0]))}
const Script kScripts[LB_COUNT] = {SCRIPT(kApproach), SCRIPT(kTrailPause), SCRIPT(kReveal), SCRIPT(kClimb),
                                   SCRIPT(kExplore),  SCRIPT(kSettle),     Script{nullptr, 0}, Script{nullptr, 0}};

// ---- beats -------------------------------------------------------------------------------

int beatSeconds(const LoopPlan& p, int b) { return p.fsec[b]; }

void startHawk(World& w);

template <int B>
void beatBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  LookoutState& L = w.look;
  if (B == LB_TRAIL_PAUSE && !L.hawk) startHawk(w);
  if (B == LB_REVEAL) L.stage = LKS_REVEAL;
  if (B == LB_SUNSET) {   // still but for his breath: slower and deeper (4 s, 1 px)
    L.sun_on = true;
    L.sun_s = 0.f;
    w.hiker.breath_rate = 0.9f;
    w.hiker.breath_scale = 1.f / (0.011f * w.hiker.height);
  }
  if (B == LB_FADE) w.mood_override = 0.f;   // deep blue: the night key
  const Script& s = kScripts[B];
  if (s.steps) scriptStart(w, s.steps, s.n);
}

void beatUpdate(SceneCtx& ctx, float, float) {
  if (ctx.w->spine.run.steps) scriptRun(*ctx.w);
}

void revealUpdate(SceneCtx& ctx, float t, float dt) {
  ctx.w->look.reveal = t;
  beatUpdate(ctx, t, dt);
}

// The sunset: the palette's mood steps from 4.0 to 5.9 in `fsteps` even steps,
// the last a stretch before the beat ends (step k at k / (n + 1) of the beat).
void sunsetUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  LookoutState& L = w.look;
  const int n = w.plan.fsteps;
  L.sun_s = t * (float)w.plan.fsec[LB_SUNSET];
  int k = (int)floorf(t * (float)(n + 1));
  if (k > n) k = n;
  if (k != L.steps) {
    L.steps = (uint8_t)k;
    w.mood_override = kMoodFrom + (kMoodTo - kMoodFrom) * (float)k / (float)n;
  }
}

// The fade: 0.0 at its start (beatBegin), 0.1 at 3 s; the star at 4 s; the
// last kFadeS seconds to black, 1 on the beat's last tick.
void fadeUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  const float d = (float)w.plan.fsec[LB_FADE];
  const float s = t * d;
  Glyph& g = w.major.g[G_STAR];
  const float k = easeOutCubic(window(s, 4.f, 5.4f));
  if (k > 0.f) {
    g.kind = GL_DOT;
    g.slot = STAR;
    g.pos = w.look.star;
    g.scale = 1.6f * k;
    g.base_y = -1000;
  }
  w.fade = easeInOut(window(t, 1.f - kFadeS / d, 1.f));
}

void beatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  SpineState& sp = w.spine;
#ifdef SS_SPINE_TRACE   // tuning aid: where each script finished in its beat
  printf("trace lookout_end %s step=%u/%u feet=%.1f,%.1f ticks=%u done_at=%u\n", appBeatName(ctx.app),
         (unsigned)sp.run.i, (unsigned)sp.run.n, (double)w.hiker.feet.x, (double)w.hiker.feet.y,
         (unsigned)sp.run.beat_ticks, (unsigned)sp.run.done_at);
#endif
  if (sp.run.steps && !scriptDone(w) && sp.overruns < 255) ++sp.overruns;
  sp.run.steps = nullptr;
}

template <int B>
void beatEndT(SceneCtx& ctx) {
  beatEnd(ctx);
  World& w = *ctx.w;
  LookoutState& L = w.look;
  if (B == LB_REVEAL) {
    L.reveal = 1.f;
    L.stage = LKS_TOWER;
  }
  if (B == LB_SUNSET) L.sun_on = false;
}

typedef void (*BeginFn)(SceneCtx&);
typedef void (*UpdateFn)(SceneCtx&, float, float);
typedef void (*EndFn)(SceneCtx&);
const BeginFn kBegin[LB_COUNT] = {beatBegin<0>, beatBegin<1>, beatBegin<2>, beatBegin<3>,
                                  beatBegin<4>, beatBegin<5>, beatBegin<6>, beatBegin<7>};
const UpdateFn kUpdate[LB_COUNT] = {beatUpdate, beatUpdate, revealUpdate, beatUpdate,
                                    beatUpdate, beatUpdate, sunsetUpdate, fadeUpdate};
const EndFn kEnd[LB_COUNT] = {beatEndT<0>, beatEndT<1>, beatEndT<2>, beatEndT<3>,
                              beatEndT<4>, beatEndT<5>, beatEndT<6>, beatEndT<7>};

// The build beat: the camp's (the fade up from black, World::build), and the
// Lookout's own layers from its progress.
void buildUpdate(SceneCtx& ctx, float t, float dt) {
  kBuildBeat.update(ctx, t, dt);
  ctx.w->look.build_t = t;
}
void buildEnd(SceneCtx& ctx) {
  kBuildBeat.end(ctx);
  ctx.w->look.build_t = 1.f;
  ctx.w->look.stage = LKS_LAND;
}
const Beat kLookoutBuild = {"build", SS_BUILD_SECONDS, nullptr, buildUpdate, buildEnd};

// ---- the birds, the rail bird, the chipmunk, the tap -------------------------------------

void startHawk(World& w) {
  LookoutState& L = w.look;
  Bird& b = w.amb.bird;
  L.hawk = true;
  L.bird_mode = BM_HAWK;
  L.bird_t = 0.f;
  b.active = true;
  b.dir = -1.f;
  b.x = (float)SS_WIDTH + 24.f;
  b.y0 = 36.f;
  b.speed = 22.f * SS_TICK_DT;
  b.path_ph = 0.3f;
  b.flap = 0.f;
  b.scale = 1.8f;
  b.slot = ACTOR;
  b.glide = 1.f;
  b.pair = false;
}

void startBird(World& w, const LkBird& p) {
  LookoutState& L = w.look;
  Bird& b = w.amb.bird;
  b.active = true;
  b.dir = (float)p.dir;
  b.x = p.dir > 0 ? -16.f : (float)SS_WIDTH + 16.f;
  b.y0 = p.y;
  b.y = p.y;
  b.speed = p.speed * SS_TICK_DT;
  b.path_ph = p.y / 97.f;
  b.flap = 0.f;
  b.scale = 0.9f;
  b.slot = ACTOR;
  b.glide = 0.f;
  b.pair = p.pair;
  b.pdx = -(float)p.dir * 11.f;
  b.pdy = 3.f;
  b.pscale = 0.85f;
  L.bird_t = 0.f;
  L.bird_mode = p.lands ? BM_IN : BM_CROSS;
  if (p.lands) {   // in from the left on the circle's top line
    b.dir = 1.f;
    b.x = -16.f;
    b.y0 = b.y = L.land_at.y - 50.f - 24.f;
    b.pdx = -11.f;
  }
  if (L.birds_flown < 0xFFFF) ++L.birds_flown;
}

constexpr float kCircleR = 24.f, kCircleS = 5.f, kDownS = 4.f;

void birdsStep(World& w) {
  LookoutState& L = w.look;
  Bird& b = w.amb.bird;
  const float dt = SS_TICK_DT;
  if (L.bird_mode == BM_NONE && L.sun_on && L.bird_i < L.n_birds && L.sun_s >= L.birds[L.bird_i].at) {
    L.bird_cur = L.bird_i;
    startBird(w, L.birds[L.bird_i]);
    ++L.bird_i;
  }
  if (L.bird_mode == BM_NONE) return;
  L.bird_t += dt;
  b.flap += dt * (b.glide > 0.5f ? 0.9f : 2.8f);
  b.flap -= floorf(b.flap);
  const Vec2 c{L.land_at.x, L.land_at.y - 50.f};
  switch (L.bird_mode) {
    case BM_HAWK:
    case BM_CROSS:
      b.x += b.dir * b.speed;
      b.y = b.y0 + (L.bird_mode == BM_HAWK ? 5.f : 8.f) * sinT(b.x / 320.f + b.path_ph);
      if (b.x < -40.f || b.x > (float)SS_WIDTH + 40.f) {
        b.active = false;
        L.bird_mode = BM_NONE;
      }
      break;
    case BM_IN:
      b.x += b.speed;
      b.y = b.y0;
      if (b.x >= c.x) {
        b.x = c.x;
        L.bird_mode = BM_CIRCLE;
        L.bird_t = 0.f;
        b.glide = 0.6f;
      }
      break;
    case BM_CIRCLE: {   // one turn, clockwise on screen from the top
      const float a = 0.75f + clamp01(L.bird_t / kCircleS);
      b.x = c.x + kCircleR * 1.4f * cosT(a);
      b.y = c.y + kCircleR * sinT(a);
      if (L.bird_t >= kCircleS) {
        L.bird_mode = BM_DOWN;
        L.bird_t = 0.f;
      }
      break;
    }
    case BM_DOWN: {     // down into the far treeline, smaller and smaller
      const float u = clamp01(L.bird_t / kDownS);
      const float e = easeInOut(u);
      b.x = lerp(c.x, L.land_at.x + 14.f, e);
      b.y = lerp(c.y - kCircleR, L.land_at.y, e);
      b.scale = lerp(0.9f, 0.3f, e);
      b.glide = 1.f;
      if (u >= 1.f) {
        b.active = false;
        L.bird_mode = BM_NONE;
        if (L.pairs_landed < 255) ++L.pairs_landed;
      }
      break;
    }
    default: break;
  }
}

void railBirdStep(World& w) {
  LookoutState& L = w.look;
  Glyph& g = w.major.g[G_RAILBIRD];
  if (L.rail_mode == RB_NONE || L.rail_mode == RB_GONE) return;
  L.rail_t += SS_TICK_DT;
  const Vec2 p = L.perch_at;
  g.kind = GL_SHAPE;
  g.shape = SH_PERCH;
  g.slot = ACTOR;
  g.scale = 1.f;
  g.base_y = (int16_t)floorf(p.y);
  const float tx = w.hiker.feet.x;
  if (L.rail_mode == RB_IN) {   // a glide down from up the valley side
    const float u = clamp01(L.rail_t / 2.6f);
    const float e = easeOutCubic(u);
    g.pos = Vec2{lerp(p.x - 110.f, p.x, e), lerp(p.y - 60.f, p.y, e) - 6.f * sinT(0.5f * u)};
    g.facing = 1;
    g.k = 1.f;
    g.t = L.rail_t * 3.f;
    if (u >= 1.f) {
      L.rail_mode = RB_SIT;
      L.rail_t = 0.f;
    }
  } else if (L.rail_mode == RB_SIT) {   // perched, looking his way, turning now and then
    g.pos = p;
    g.k = 0.f;
    g.t = 0.f;
    g.facing = (int8_t)(tx < p.x ? -1 : 1);
  } else if (L.rail_mode == RB_OFF) {   // up and away to the west
    const float u = L.rail_t / 2.4f;
    g.pos = Vec2{p.x - 150.f * u * u - 20.f * u, p.y - 70.f * u};
    g.facing = -1;
    g.k = 1.f;
    g.t = L.rail_t * 3.5f;
    if (u >= 1.f) {
      L.rail_mode = RB_GONE;
      g = Glyph{};
    }
  }
}

void chipStep(World& w) {
  LookoutState& L = w.look;
  Glyph& g = w.major.g[G_CHIP];
  if (L.chip_mode == CH_NONE || L.chip_mode == CH_GONE) return;
  L.chip_t += SS_TICK_DT;
  const Vec2 p = L.chip_at;
  const float leg_x = p.x - 8.f;   // where it comes out from behind the left leg
  g.kind = GL_SHAPE;
  g.shape = SH_CHIP;
  g.slot = ACTOR;
  g.scale = 1.f;
  g.base_y = (int16_t)floorf(p.y);
  if (L.chip_mode == CH_IN) {
    const float u = clamp01(L.chip_t / 1.2f);
    g.pos = Vec2{lerp(leg_x, p.x, easeInOut(u)), p.y};
    g.facing = 1;
    g.k = 0.f;
    if (u >= 1.f) {
      L.chip_mode = CH_SIT;
      L.chip_t = 0.f;
    }
  } else if (L.chip_mode == CH_SIT) {   // watching him: turned his way, head up while he is above
    g.pos = p;
    g.facing = (int8_t)(w.hiker.feet.x < p.x ? -1 : 1);
    g.k = 1.f;
  } else if (L.chip_mode == CH_OFF) {
    const float u = clamp01(L.chip_t / 0.9f);
    g.pos = Vec2{lerp(p.x, leg_x, easeInOut(u)), p.y};
    g.facing = -1;
    g.k = 0.f;
    g.scale = 1.f - easeInOut(window(u, 0.6f, 1.f));
    if (u >= 1.f) {
      L.chip_mode = CH_GONE;
      g = Glyph{};
    }
  }
}

void tapStep(World& w) {
  LookoutState& L = w.look;
  Glyph& g = w.major.g[G_TAP];
  if (L.tap_t > 0.f) {
    L.tap_t -= SS_TICK_DT;
    g.kind = GL_ARCS;
    g.slot = CLOUD_LIT;
    g.pos = towerP(w, 31.f, -(float)LK_LEG_H - (float)LK_RAIL_H - 2.f);
    g.rot = 0.75f;   // facing up
    g.scale = L.tap_t > 0.22f ? 3.f : 4.5f;
    g.k = 0.9f;
    g.base_y = (int16_t)floorf(g.pos.y);
  } else if (g.kind != GL_NONE) {
    g = Glyph{};
  }
}

// ---- the build's layers ------------------------------------------------------------------

int buildPhaseOf(float t) {
  const float sec = clamp01(t) * SS_BUILD_SECONDS;
  int p = 0;
  while (p < 5 && sec >= kPhase[p + 1]) ++p;
  return p;   // 0 sky, 1 valley, 2 trail, 3 plants, 4 hold, 5 done
}

// What the build shows now, and which layers are animating (the cache holds
// the finished ones below the first of those; the dynamic pass the rest).
LkDraw buildDraw(const World& w, float mood, uint16_t* active) {
  const float sec = clamp01(w.look.build_t) * SS_BUILD_SECONDS;
  const int p = buildPhaseOf(w.look.build_t);
  const float u = p < 5 ? window(sec, kPhase[p], kPhase[p + 1]) : 1.f;
  LkDraw d = lookoutDrawDone(mood, 0.f, LKL_LAND);
  uint16_t act = 0;
  d.layers = (uint16_t)(LKL_BIT(LKL_SKY) | LKL_BIT(LKL_HILL));   // the hill is the ground: there from the start
  if (p == 0) {
    d.sky = easeInOut(window(u, 0.12f, 1.f));
    act = LKL_BIT(LKL_SKY);
  }
  if (p >= 1) d.layers = (uint16_t)(d.layers | LKL_BIT(LKL_VALLEY));
  if (p == 1) {
    for (int l = 0; l < 3; ++l) {
      const float a = 0.14f * (float)l;   // far leads, near follows
      d.valley_drop[l] = kValleyDrop * (1.f - easeOutCubic(window(u, a, a + 0.72f)));
    }
    act = LKL_BIT(LKL_VALLEY);
  }
  if (p >= 2) d.layers = (uint16_t)(d.layers | LKL_BIT(LKL_TRAIL));
  if (p == 2) {
    d.trail = easeInOut(window(u, 0.f, 0.94f));
    act = LKL_BIT(LKL_TRAIL);
  }
  if (p >= 3) d.layers = (uint16_t)(d.layers | LKL_BIT(LKL_PLANTS));
  if (p == 3) {
    d.plants_u = u;
    act = LKL_BIT(LKL_PLANTS);
  }
  if (active) *active = act;
  return d;
}

// Layers below the first animating one that are drawn (and so finished).
uint16_t cachedOf(uint16_t drawn, uint16_t active) {
  if (active == 0) return drawn;
  int f = 0;
  while (!(active & LKL_BIT(f))) ++f;
  return (uint16_t)(drawn & (LKL_BIT(f) - 1u));
}

// ---- the fit -----------------------------------------------------------------------------

// Seconds to walk from a to b at `k` heights per second, his height following
// the hill (walks move x; the pace is k times his height): the integral of
// |dx| / (k h(y)) along the straight line, plus the walk's ease in.
float walkSeconds(const LookoutLand& lk, Vec2 a, Vec2 b, float k) {
  const int n = 48;
  float t = 0.f;
  const float dx = fabsf(b.x - a.x) / (float)n;
  for (int i = 0; i < n; ++i) {
    const float f = ((float)i + 0.5f) / (float)n;
    const float y = lerp(a.y, b.y, f);
    t += dx / (k * lookoutHeightAt(lk, y));
  }
  return t + 0.3f;
}

int totalSeconds(const LoopPlan& p) {
  int s = (int)(SS_BUILD_SECONDS + 0.5f);
  for (int b = 0; b < LB_COUNT; ++b) s += p.fsec[b];
  s += p.pause_s[LB_REVEAL] + p.pause_s[LB_CLIMB] + p.pause_s[LB_EXPLORE];
  return s;
}

}  // namespace

// ---- public ------------------------------------------------------------------------------

const char* lookoutBeatName(int beat) { return beat >= 0 && beat < LB_COUNT ? kBeatNames[beat] : "?"; }

void lookoutPlan(LoopPlan& p, Rng& rng, const Inputs&) {
  // One rng call per statement. The beats' seconds wait for the landscape
  // (lookoutBeginLoop); here only the sunset's palette steps.
  p.n_fbeats = LB_COUNT;
  for (int b = 0; b < 8; ++b) p.fvar[b] = p.fsec[b] = 0;
  for (uint8_t& s : p.pause_s) s = 0;
  p.n_fillers = 0;
  p.n_majors = 0;
  const int steps = rng.rangeInt(8, 10);   // one step every 11 to 15 s (brief section 7: 10 to 15)
  p.fsteps = (uint8_t)steps;
  p.fbirds = 0;
}

void lookoutBeginLoop(World& w, Rng& rng) {
  LookoutState& L = w.look;
  LoopPlan& p = w.plan;
  L = LookoutState{};
  // The landscape and its clouds from the loop rng, after every other draw of
  // the loop start (one rng call per statement throughout).
  lookoutLandGenerate(w.lk, rng);
  const LookoutLand& lk = w.lk;
  // Horizon clouds, slow, over the valley on the west side where the sky
  // comes down lowest: the upper one clear of the tower, the lower one of the
  // hill (both stay over sky as they drift, about 0.5 px/s, over the loop).
  {
    const float x0 = rng.range(300.f, 440.f);
    const float y0 = rng.range(112.f, 136.f);
    cloudGenerate(w.clouds[0], rng, x0, y0, 0.5f);
    const float x1 = rng.range(170.f, 300.f);
    const float y1 = rng.range(176.f, 190.f);
    cloudGenerate(w.clouds[1], rng, x1, y1, 0.35f);
  }
  {
    const float sx = rng.range(90.f, 330.f);
    const float sy = rng.range(30.f, 78.f);
    L.star = Vec2{sx, sy};
    // The far treeline: the near valley crest there, a few px into the trees.
    const float lx = rng.range(70.f, 280.f);
    const float f = (lx - (float)LS_CREST_X0) / (float)LS_CREST_STEP;
    const int i = (int)f;
    L.land_at = Vec2{lx, lerp(lk.valley[2][i], lk.valley[2][i + 1], f - (float)i) - 3.f};
  }

  // The trail: the approach ends at the last left turn; the pace is a stroll
  // (0.5 heights a second), quicker on a long trail so the approach stays
  // near 40 s (up to 0.75).
  const LkTrail& tr = lk.trail;
  L.pause_at = (uint8_t)(tr.n - 2);
  float a50 = 0.f;
  for (int i = 1; i <= L.pause_at; ++i) a50 += walkSeconds(lk, tr.p[i - 1], tr.p[i], 0.5f);
  L.walk_k = a50 > 40.f ? minf(0.75f, 0.5f * a50 / 40.f) : 0.5f;
  float app = 0.f;
  for (int i = 1; i <= L.pause_at; ++i) app += walkSeconds(lk, tr.p[i - 1], tr.p[i], L.walk_k);
  // The climb: the last leg, three flights, two landings, the look over the valley.
  Vec2 k[6];
  float ks[6];
  lookoutStairKnots(lk, k, ks);
  float climb = walkSeconds(lk, tr.p[tr.n - 2], tr.p[tr.n - 1], L.walk_k);
  for (int i = 1; i < 6; ++i) {
    const bool flight = (i & 1) == 1;
    climb += fabsf(k[i].x - k[i - 1].x) / ((flight ? kStairPace : kDeckPace) * 44.f) + 0.3f;
  }
  climb += kLookValleyS + 1.f;

  p.fsec[LB_APPROACH] = (uint8_t)ceilf(app + 2.f);
  p.fsec[LB_TRAIL_PAUSE] = kTrailPauseS;
  p.fsec[LB_REVEAL] = kRevealS;
  p.fsec[LB_CLIMB] = (uint8_t)ceilf(climb + 3.f);
  p.fsec[LB_EXPLORE] = kExploreS;
  p.fsec[LB_SETTLE] = kSettleS;
  p.fsec[LB_SUNSET] = kSunMax;
  p.fsec[LB_FADE] = kFadeBeatS;
  // The length: the sunset as long as it can be (90 to 120 s), then the
  // pauses (2 to 12 s) round robin to land in the range.
  static const uint8_t kPauses[3] = {LB_REVEAL, LB_CLIMB, LB_EXPLORE};
  for (uint8_t b : kPauses) p.pause_s[b] = 4;
  while (totalSeconds(p) > kAimMax && p.fsec[LB_SUNSET] > kSunMin) --p.fsec[LB_SUNSET];
  for (int guard = 0, j = 0; guard < 64 && totalSeconds(p) > kAimMax; ++guard, j = (j + 1) % 3)
    if (p.pause_s[kPauses[j]] > kPauseMin) --p.pause_s[kPauses[j]];
  for (int guard = 0, j = 0; guard < 64 && totalSeconds(p) < kAimMin; ++guard, j = (j + 1) % 3)
    if (p.pause_s[kPauses[j]] < kPauseMax) ++p.pause_s[kPauses[j]];

  // The sunset's birds: one every 15 to 25 s from 5 to 10 s in, until 12 s
  // before its end; a third of them pairs; one pair (of those well inside the
  // sunset) circles and lands in the far treeline.
  const float sun = (float)p.fsec[LB_SUNSET];
  float t = rng.range(5.f, 10.f);
  L.n_birds = 0;
  while (t < sun - 12.f && L.n_birds < LK_BIRDS_MAX) {
    LkBird& b = L.birds[L.n_birds++];
    const float y = rng.range(44.f, 168.f);
    const float v = rng.range(40.f, 60.f);
    const bool right = rng.chance(0.5f);
    const bool pair = rng.chance(0.35f);
    b = LkBird{t, y, v, (int8_t)(right ? 1 : -1), pair, false};
    const float gap = rng.range(15.f, 25.f);
    t += gap;
  }
  if (L.n_birds >= 2) {   // the landing pair: the second or third, so it has time to come down (about 16 s)
    const int pick = rng.rangeInt(1, L.n_birds > 2 ? 2 : 1);
    L.birds[pick].pair = true;
    L.birds[pick].lands = true;
  }
  p.fbirds = L.n_birds;

  // His places: the rail bird on the deck's front rail past the cab's east
  // corner (against the sky); the chipmunk on the frame's middle girt, by the left leg.
  L.perch_at = towerP(w, 35.f, -(float)LK_LEG_H - (float)LK_RAIL_H - 0.8f);
  L.chip_at = towerP(w, -26.f, -0.5f * (float)LK_LEG_H - 1.f);

  // The scene: the palette's own mood, nothing of the camp, the bird the scene's.
  w.mood_override = kMoodFrom;
  w.busy = true;                 // no minors, all loop
  w.amb.bird_manual = true;
  w.amb.bird.active = false;
  w.fx.on = false;
  w.hiker.visible = false;
  L.h_min = 90.f;
  L.stage = LKS_BUILD;
  L.d_cached = lookoutCached(w);
  L.d_bphase = 0;
  w.spine.fp[0] = w.spine.fp[1] = tr.p[0];
  majorsReset(w.major);
}

void lookoutBuildTimeline(Timeline& tl, const LoopPlan& p) {
  tl.add(kLookoutBuild);
  for (int b = 0; b < LB_COUNT; ++b) {
    tl.add(Beat{kBeatNames[b], (float)beatSeconds(p, b), kBegin[b], kUpdate[b], kEnd[b]});
    if (b == LB_REVEAL || b == LB_CLIMB || b == LB_EXPLORE)
      tl.add(Beat{"pause", (float)p.pause_s[b], nullptr, nullptr, nullptr});
  }
}

uint32_t lookoutLoopTicks(const LoopPlan& p) {
  const uint32_t build = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  const int rest = totalSeconds(p) - (int)(SS_BUILD_SECONDS + 0.5f);
  return build + (uint32_t)rest * SS_TICK_HZ;
}

int lookoutFormatPlan(const LoopPlan& p, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int k = snprintf(buf, n, "family=lookout");
  auto put = [&](const char* str) {
    if (k < 0 || (size_t)k >= n) return;
    const int m = snprintf(buf + k, n - (size_t)k, "%s", str);
    if (m > 0) k += m;
  };
  char tmp[40];
  for (int b = 0; b < LB_COUNT; ++b) {
    snprintf(tmp, sizeof tmp, " %s=%d", kBeatNames[b], (int)p.fsec[b]);
    put(tmp);
  }
  snprintf(tmp, sizeof tmp, " pauses=%d,%d,%d steps=%d birds=%d", (int)p.pause_s[LB_REVEAL], (int)p.pause_s[LB_CLIMB],
           (int)p.pause_s[LB_EXPLORE], (int)p.fsteps, (int)p.fbirds);
  put(tmp);
  return k;
}

void lookoutStep(World& w) {
  birdsStep(w);
  railBirdStep(w);
  chipStep(w);
  tapStep(w);
}

void lookoutHikerPre(World& w) {
  Hiker& h = w.hiker;
  LookoutState& L = w.look;
  const float want = lookoutHeightAt(w.lk, h.feet.y);
  if (want < L.h_min) L.h_min = want;
  h.height = L.h_min;
  if (h.walking) h.walk_speed = L.speed_k * h.height;
}

void lookoutHikerPost(World& w) {
  LookoutState& L = w.look;
  const float step = SS_TICK_DT / 0.35f;
  const float d = L.climb_to - L.climb_w;
  L.climb_w += d > step ? step : (d < -step ? -step : d);
  hikerApplyClimb(w.hiker, easeInOut(L.climb_w));
}

uint16_t lookoutCached(const World& w) {
  const LookoutState& L = w.look;
  if (L.stage == LKS_BUILD) {
    uint16_t act = 0;
    const LkDraw d = buildDraw(w, 4.f, &act);
    return cachedOf(d.layers, act);
  }
  if (L.stage == LKS_TOWER) return (uint16_t)(LKL_LAND | LKL_BIT(LKL_TOWER));
  return LKL_LAND;
}

int lookoutBuildPhase(const World& w) { return w.look.stage == LKS_BUILD ? buildPhaseOf(w.look.build_t) : 5; }

void lookoutBuildRows(const World& w, int& y0, int& y1) {
  y0 = y1 = 0;
  if (w.look.stage != LKS_BUILD) return;
  switch (buildPhaseOf(w.look.build_t)) {
    case 0: y0 = 0; y1 = SS_HEIGHT; break;   // the sky's fade (and the clouds with it)
    case 1: y0 = 180; y1 = 400; break;       // the valley rising behind the hill
    case 2: {                                // the trail drawing itself up the hill
      y0 = w.lk.tower_gy - 4;
      y1 = (int)ceilf(w.lk.trail.p[0].y) + 4;
      break;
    }
    case 3: y0 = 96; y1 = SS_HEIGHT; break;  // pines and grass popping up
    default: break;
  }
}

void lookoutDrawStatic(const World& w, Surface& frame, const Palette& pal, float mood) {
  lookoutLandDraw(w.lk, frame, frame.bounds(), pal, lookoutDrawDone(mood, 1.f, lookoutCached(w)));
}

void lookoutDrawDynamic(const World& w, Surface& s, const Rect& clip, const Palette& pal, float mood) {
  const LookoutState& L = w.look;
  const uint16_t cached = lookoutCached(w);
  LkDraw d = L.stage == LKS_BUILD ? buildDraw(w, mood, nullptr) : lookoutDrawDone(mood, 0.f, LKL_LAND);
  const uint16_t dyn = (uint16_t)(d.layers & ~cached);

  // 1. The sky while it fades in, then the sky's things: clouds (fading in with the sky) and the bird.
  if (dyn & LKL_BIT(LKL_SKY)) {
    LkDraw sd = d;
    sd.layers = LKL_BIT(LKL_SKY);
    lookoutLandDraw(w.lk, s, clip, pal, sd);
  }
  const float sky = L.stage == LKS_BUILD ? (buildPhaseOf(L.build_t) == 0 ? d.sky : 1.f) : 1.f;
  if (sky < 1.f) {
    Palette cp = pal;
    cp.c[CLOUD_LIT] = mixRGB(pal[SKY4], pal[CLOUD_LIT], sky);
    cp.c[CLOUD_SHADE] = mixRGB(pal[SKY4], pal[CLOUD_SHADE], sky);
    for (const Cloud& c : w.clouds) cloudDraw(c, s, cp, clip);
  } else {
    for (const Cloud& c : w.clouds) cloudDraw(c, s, pal, clip);
  }

  // 2. The build's layers in front of the sky, animating or in front of one that is.
  if (dyn & ~LKL_BIT(LKL_SKY)) {
    LkDraw ld = d;
    ld.layers = (uint16_t)(dyn & ~LKL_BIT(LKL_SKY));
    lookoutLandDraw(w.lk, s, clip, pal, ld);
  }

  // 3. The tower rising (all of it), or the cab's front behind him while he is outside it.
  if (L.stage == LKS_REVEAL && L.reveal > 0.f) {
    const LkDraw td = lookoutDrawDone(mood, L.reveal, (uint16_t)(LKL_BIT(LKL_TOWER) | LKL_BIT(LKL_CAB) | LKL_BIT(LKL_RAILS)));
    lookoutLandDraw(w.lk, s, clip, pal, td);
  }
  const bool tower = L.stage == LKS_TOWER;
  if (tower && !L.inside) lookoutLandDraw(w.lk, s, clip, pal, lookoutDrawDone(mood, 1.f, LKL_BIT(LKL_CAB)));

  // 4. Him and the scene's glyphs, far to near.
  for (int i = 0; i < w.n_actors; ++i) {
    const ActorRef& a = w.actors[i];
    if (a.kind == ACT_HIKER) hikerDraw(w.hiker, s, pal, clip);
    else if (a.kind == ACT_MAJOR) glyphDraw(w.major.g[a.idx], s, clip, pal);
  }

  // 5. In front of him: the cab's front when he is inside it, and the rails.
  if (tower) {
    const uint16_t front = (uint16_t)(LKL_BIT(LKL_RAILS) | (L.inside ? LKL_BIT(LKL_CAB) : 0));
    lookoutLandDraw(w.lk, s, clip, pal, lookoutDrawDone(mood, 1.f, front));
  }
  // The birds, over the tower too (a bird never flies behind the cab).
  ambientDrawBird(w, s, clip, pal);

  // 6. The loop's fade, over everything.
  if (w.fade > 0.f) {
    const float a = w.fade >= 1.f ? 255.f : w.fade * 255.f + 0.5f;
    const uint8_t alpha = (uint8_t)a;
    for (int y = clip.y0; y < clip.y1; ++y) blendSpan(s, y, clip.x0, clip.x1, pal[ACTOR], alpha);
  }
}

void lookoutPaletteTweak(const World&, float mood, const WeatherTint& t, float flash, Palette& p) {
  // Through the sunset (4.0 to 5.9) and into the fade (mood 0 = 6) the clouds
  // hold their colour: computed 0.45 of mood behind the sky (never before
  // 4.0), with the same weather and night rule, so the fade's one step does
  // not snap them to night grey.
  const float m = mood < 1.f ? mood + 6.f : mood;
  if (!(m > kMoodFrom && m <= 6.f + 1e-3f)) return;
  const float lag = maxf(kMoodFrom, m - 0.45f);
  Palette q;
  computePalette(lag, t, flash, q);
  p.c[CLOUD_LIT] = q.c[CLOUD_LIT];
  p.c[CLOUD_SHADE] = q.c[CLOUD_SHADE];
}

}  // namespace ss
