// The sunrise overlook (SF_SUNRISE): the plan, the beats, the scene's tick, its dynamic draw and
// its dirty marks. See sunrise.h; the landscape is sunrise_land.cpp.
//
// Same idioms as the Lookout (lookout.cpp): actions take u 0..1 and leave him at rest at u = 1;
// scripts are const Step tables run by the spine's interpreter (script.h); a beat is longer than
// its script (an overrun counts in SpineState::overruns, tests require none). One rng call per
// statement, table sines, no doubles, no allocation, no static mutable state.
//
// Timeline: build, arrive, settle, pour, dawn, sunbreak, light, linger, leave (no pauses). No
// fillers, majors or minors (World::busy all loop). About 250 to 300 s.
//
// The palette's mood is the scene's own (World::mood_override): 0.32 (deep blue before dawn) from
// the loop's start, stepped to 1.0 (rose) through the dawn, then on toward gold through the sunbreak
// and the light (1.6); each step a palette change and a cache rebuild (about 10 a loop). The light
// on the ridges and the fog are steps in the cached background, each repainting only its rows.
#include "sunrise.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../anim/ease.h"
#include "../render/renderer.h"
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
inline bool sameRect(const Rect& a, const Rect& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }
inline void unite(Rect& r, const Rect& b) {
  if (b.empty()) return;
  if (r.empty()) {
    r = b;
    return;
  }
  r = Rect{r.x0 < b.x0 ? r.x0 : b.x0, r.y0 < b.y0 ? r.y0 : b.y0, r.x1 > b.x1 ? r.x1 : b.x1, r.y1 > b.y1 ? r.y1 : b.y1};
}
const Rect kEmpty{0, 0, 0, 0};

constexpr float kWalkV = 30.f;        // nominal walk speed in the scripts (the pace is set per tick)
constexpr float kPace = 0.42f;        // heights a second: an unhurried walk out along the rock (PROPOSED)
constexpr float kFadeS = 2.5f;        // the leave's last seconds go to black
constexpr float kSitDrop = 0.215f;    // POSE_PERCH: his feet hang this much of his height below the seat
constexpr float kSitS = 1.8f;
constexpr float kNearH = 84.f, kTipH = 58.f;   // his height at the slab's near end and at its tip (PROPOSED)
constexpr float kMood0 = 0.32f, kMoodRose = 1.0f;   // deep blue before dawn; rose at the dawn's end (PROPOSED)
constexpr uint8_t kSettleS = 14, kPourS = 15, kBreakS = 34, kLightS = 46;
constexpr float kBreakAt = 5.f;       // s into the sunbreak the sun's top clears the ridge
constexpr float kSunRiseEnd = 8.f;    // px of sky under the sun when the light beat ends
// Where on the slab (u along it, v back to front): his pack stop, his seat at the very end, the dogs'.
constexpr float kPackU = 0.875f, kPathV = 0.52f, kSeatU = 1.0f, kSeatV = 0.8f, kPackDownV = 0.26f;
const float kDogU[2] = {0.93f, 0.885f}, kDogV[2] = {0.6f, 0.7f};

const char* const kBeatNames[RB_COUNT] = {"arrive", "settle", "pour", "dawn", "sunbreak", "light", "linger", "leave"};

// The build: phases (s) sky, ridges, fog, the slope and the ledge, hold; the end.
const float kPhase[6] = {0.f, 1.8f, 5.4f, 7.2f, 10.6f, SS_BUILD_SECONDS};
constexpr float kRidgeDrop = 120.f, kFgDrop = 170.f;

// The light's steps, in seconds from the start of their beat (sunbreak or light). What: 0..3 a
// ridge, 4 the fog's glow, 5 the fog thinning, 6 the slab.
struct LightStep {
  uint8_t beat;
  float s;
  uint8_t what, value;
};
const LightStep kLight[] = {
    {RB_SUNBREAK, 11.f, 0, 1}, {RB_SUNBREAK, 22.f, 0, 2}, {RB_LIGHT, 2.f, 1, 1},  {RB_LIGHT, 6.f, 1, 2},
    {RB_LIGHT, 10.f, 2, 1},    {RB_LIGHT, 14.f, 2, 2},    {RB_LIGHT, 18.f, 3, 1}, {RB_LIGHT, 22.f, 3, 2},
    {RB_LIGHT, 25.f, 4, 1},    {RB_LIGHT, 28.f, 6, 1},    {RB_LIGHT, 31.f, 4, 2}, {RB_LIGHT, 34.f, 5, 1},
    {RB_LIGHT, 38.f, 5, 2},    {RB_LIGHT, 42.f, 5, 3},
};

enum DogMode : uint8_t { DM_NONE = 0, DM_WAIT, DM_FOLLOW, DM_SIT, DM_OFF };

// ---- places ------------------------------------------------------------------------------------

const SunriseLand& land(const World& w) { return w.sr.land; }
Vec2 slab(const World& w, float u, float v) { return sunriseSlabAt(land(w), u, v); }
// The slab's top at x along the line v (back 0 .. front 1).
float slabY(const World& w, float x, float v) {
  const Vec2 a = slab(w, 0.f, v), b = slab(w, 1.f, v);
  const float u = (x - a.x) / (b.x - a.x);
  return lerp(a.y, b.y, u);
}
float heightAt(const World& w, float y) {
  const float y0 = slab(w, 0.f, kPathV).y, y1 = slab(w, 1.f, kPathV).y;
  return lerp(kNearH, kTipH, clamp01((y0 - y) / (y0 - y1)));
}
Vec2 trailStart(const World& w) { return Vec2{-46.f, slabY(w, -46.f, kPathV)}; }
Vec2 packStop(const World& w) { return slab(w, kPackU, kPathV); }
Vec2 seat(const World& w) { return slab(w, kSeatU, kSeatV); }
void targetTo(World& w, Vec2 p) { w.spine.fp[1] = p; }

// Seconds to walk from a to b at kPace (his height following the slab), plus the ease in.
float walkSeconds(const World& w, Vec2 a, Vec2 b) {
  const int n = 32;
  float t = 0.f;
  const float dx = fabsf(b.x - a.x) / (float)n;
  for (int i = 0; i < n; ++i) t += dx / (kPace * heightAt(w, lerp(a.y, b.y, ((float)i + 0.5f) / (float)n)));
  return t + 0.3f;
}

uint32_t buildTicks() { return (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f); }
uint32_t beatStartTick(const LoopPlan& p, int beat) {
  uint32_t t = buildTicks();
  for (int b = 0; b < beat; ++b) t += (uint32_t)p.fsec[b] * SS_TICK_HZ;
  return t;
}

// ---- arrive ------------------------------------------------------------------------------------

void doArrive(World& w, float) {
  Sunrise& S = w.sr;
  S.lamp = 1.f;
  w.spine.fp[0] = trailStart(w);
  targetTo(w, packStop(w));
}

// ---- settle ------------------------------------------------------------------------------------

// Crouched, he slips the pack off beside him: it is on the rock from the middle of this on.
void actPackDown(World& w, float u) {
  Sunrise& S = w.sr;
  w.fx.lean = 0.12f * envelope(u, 0.3f, 0.3f);
  if (u >= 0.5f && !S.pack_down) {
    S.pack_down = true;
    w.hiker.pack_on = false;
    S.pack_at = slab(w, kPackU - 0.02f, kPackDownV);
  }
  if (u >= 1.f) fxRest(w);
}
// A hand up to the lamp on his forehead: it goes off.
void actLampOff(World& w, float u) {
  setArm(w, 0, envelope(u, 0.3f, 0.3f), 2.6f, 2.2f);
  if (u >= 0.5f) w.sr.lamp = 0.f;
  if (u >= 1.f) fxRest(w);
}
void doToSeat(World& w, float) { targetTo(w, seat(w)); }
// Down onto the rock's very end, legs over the edge (the Lookout's sit on the deck).
void actSitEdge(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    setPose(w, POSE_PERCH, kSitS);
    w.spine.v0 = h.feet;
  }
  h.feet.y = w.spine.v0.y + kSitDrop * h.height * easeInOut(u);
  if (u >= 1.f && w.sr.sat_tick == 0) w.sr.sat_tick = w.tick + 1;
}

// ---- pour --------------------------------------------------------------------------------------

// The flask out of his jacket in the far hand, poured into the cup in the near one, put away.
void actPour(World& w, float u) {
  Sunrise& S = w.sr;
  if (u <= 0.f) {
    hikerCarry(w.hiker, CARRY_MUG);
    S.cup = true;
  }
  setArm(w, 1, envelope(u, 0.15f, 0.15f), 1.75f, 0.85f);
  setArm(w, 0, 1.f, 0.95f, 1.25f);
  const bool out = u > 0.12f && u < 0.86f;
  const bool pouring = u > 0.28f && u < 0.68f;
  S.flask = pouring ? 2.f : (out ? 1.f : 0.f);
  if (u >= 0.42f && S.steam_t < 0.f) S.steam_t = 0.f;
  if (u >= 1.f) {
    S.flask = 0.f;
    w.fx.arm_w[1] = 0.f;
  }
}
// A sip: the cup up to his mouth, the head back a touch.
void actSip(World& w, float u) {
  const float e = envelope(u, 0.3f, 0.3f);
  setArm(w, 0, 1.f, lerp(0.95f, 0.55f, e), lerp(1.25f, 2.35f, e));
  w.fx.head = -0.12f * e;
  if (u >= 1.f) {
    w.fx.head = 0.f;
    setArm(w, 0, 1.f, 0.95f, 1.25f);
  }
}
// The cup raised to the sun, held, brought back.
void actToast(World& w, float u) {
  const float e = envelope(u, 0.25f, 0.25f);
  setArm(w, 0, 1.f, lerp(0.95f, 2.25f, e), lerp(1.25f, 0.35f, e));
  w.fx.head = -0.16f * e;
  w.fx.lean = -0.04f * e;
  if (u >= 1.f) {
    w.fx.head = w.fx.lean = 0.f;
    setArm(w, 0, 1.f, 0.95f, 1.25f);
  }
}
// Still but for a slow breath (the Lookout's sunset breath).
void doBreathe(World& w, float) {
  w.hiker.breath_rate = 0.9f;
  w.hiker.breath_scale = 1.f / (0.011f * w.hiker.height);
}

// ---- leave -------------------------------------------------------------------------------------

void actFinish(World& w, float u) {   // the last of the cup, then it goes in a pocket
  actSip(w, u);
  if (u >= 1.f) {
    hikerCarry(w.hiker, CARRY_NONE);
    w.sr.cup = false;
    w.sr.steam_t = -1.f;
    w.fx.arm_w[0] = 0.f;
  }
}
void actStandUp(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    setPose(w, POSE_STAND, kSitS);
    w.spine.v0 = h.feet;
    h.breath_rate = h.breath_scale = 1.f;
  }
  h.feet.y = w.spine.v0.y - kSitDrop * h.height * easeInOut(u);
  if (u >= 1.f) w.sr.sat_tick = 0;
}
void doToPack(World& w, float) { targetTo(w, packStop(w)); }
void actPackUp(World& w, float u) {
  Sunrise& S = w.sr;
  w.fx.lean = 0.12f * envelope(u, 0.3f, 0.3f);
  if (u >= 0.5f && S.pack_down) {
    S.pack_down = false;
    w.hiker.pack_on = true;
  }
  if (u >= 1.f) fxRest(w);
}
void doTrailOut(World& w, float) { targetTo(w, trailStart(w)); }

// ---- scripts -----------------------------------------------------------------------------------

const Step kArrive[] = {
    doit(doArrive),
    place(AT_FAM0, RIGHT),
    walk(AT_FAM1, kWalkV),
};
const Step kSettle[] = {
    still(0.4f),
    pose(POSE_CROUCH, 0.6f, 0.f),
    act(actPackDown, 1.4f),
    pose(POSE_STAND, 0.6f, 0.f),
    act(actLampOff, 1.6f),
    doit(doToSeat),
    walk(AT_FAM1, kWalkV),
    face(RIGHT),
    act(actSitEdge, kSitS),
    still(0.6f),
};
const Step kPour[] = {
    act(actPour, 9.f),
    still(0.8f),
    act(actSip, 2.6f),
    doit(doBreathe),
};
const Step kLightScript[] = {
    still(18.f),
    act(actToast, 6.f),
    still(3.f),
    act(actSip, 2.6f),
};
const Step kLinger[] = {
    still(4.f),
    act(actSip, 2.6f),
};
const Step kLeave[] = {
    act(actFinish, 2.6f),
    act(actStandUp, kSitS),
    still(0.4f),
    doit(doToPack),
    walk(AT_FAM1, kWalkV),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.f),
    act(actPackUp, 1.4f),
    pose(POSE_STAND, 0.6f, 0.f),
    face(LEFT),
    still(0.8f),   // a last look out over the valley
    doit(doTrailOut),
    walk(AT_FAM1, kWalkV),
};

struct Script {
  const Step* steps;
  uint8_t n;
};
#define SCRIPT(a) Script{a, (uint8_t)(sizeof(a) / sizeof(a[0]))}
const Script kScripts[RB_COUNT] = {SCRIPT(kArrive), SCRIPT(kSettle), SCRIPT(kPour), Script{nullptr, 0},
                                   Script{nullptr, 0}, SCRIPT(kLightScript), SCRIPT(kLinger), SCRIPT(kLeave)};

// ---- beats -------------------------------------------------------------------------------------

void setMood(World& w, float m) {
  if (w.mood_override != m) {
    w.mood_override = m;
    if (w.sr.steps < 255) ++w.sr.steps;
  }
}

// The light's steps for a beat, at its time t (0..1).
void lightSteps(World& w, int beat, float t) {
  Sunrise& S = w.sr;
  const float sec = t * (float)w.plan.fsec[beat];
  for (const LightStep& l : kLight) {
    if (l.beat != beat || sec < l.s) continue;
    uint8_t* f = l.what < SR_RIDGES ? &S.lit[l.what] : (l.what == 4 ? &S.fog_glow : (l.what == 5 ? &S.fog_thin : &S.slab_lit));
    if (*f < l.value) *f = l.value;
  }
}

template <int B>
void beatBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  const Script& s = kScripts[B];
  if (s.steps) scriptStart(w, s.steps, s.n);
}
void beatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  SpineState& sp = w.spine;
  if (sp.run.steps && !scriptDone(w) && sp.overruns < 255) ++sp.overruns;
  sp.run.steps = nullptr;
}
void beatUpdate(SceneCtx& ctx, float, float) {
  if (ctx.w->spine.run.steps) scriptRun(*ctx.w);
}
// The dawn: the mood from deep blue to rose in fsteps even steps (step k at k / (n + 1) of the beat).
void dawnUpdate(SceneCtx& ctx, float t, float dt) {
  World& w = *ctx.w;
  const int n = w.plan.fsteps > 0 ? w.plan.fsteps : 1;
  int k = (int)floorf(t * (float)(n + 1));
  if (k > n) k = n;
  setMood(w, kMood0 + (kMoodRose - kMood0) * (float)k / (float)n);
  beatUpdate(ctx, t, dt);
}
// The sunbreak: two steps toward gold; the far ridge catches the light.
void breakUpdate(SceneCtx& ctx, float t, float dt) {
  World& w = *ctx.w;
  setMood(w, t < 0.35f ? kMoodRose : (t < 0.72f ? 1.12f : 1.24f));
  lightSteps(w, RB_SUNBREAK, t);
  beatUpdate(ctx, t, dt);
}
void lightUpdate(SceneCtx& ctx, float t, float dt) {
  World& w = *ctx.w;
  setMood(w, t < 0.3f ? 1.24f : (t < 0.65f ? 1.38f : 1.52f));
  lightSteps(w, RB_LIGHT, t);
  if (w.sr.hawk == 0 && t * (float)w.plan.fsec[RB_LIGHT] >= 8.f) w.sr.hawk = 1;
  beatUpdate(ctx, t, dt);
}
void leaveUpdate(SceneCtx& ctx, float t, float dt) {
  World& w = *ctx.w;
  const float d = (float)w.plan.fsec[RB_LEAVE];
  w.fade = easeInOut(window(t, 1.f - kFadeS / d, 1.f));
  beatUpdate(ctx, t, dt);
}

typedef void (*BeginFn)(SceneCtx&);
typedef void (*UpdateFn)(SceneCtx&, float, float);
const BeginFn kBegin[RB_COUNT] = {beatBegin<0>, beatBegin<1>, beatBegin<2>, beatBegin<3>,
                                  beatBegin<4>, beatBegin<5>, beatBegin<6>, beatBegin<7>};
const UpdateFn kUpdate[RB_COUNT] = {beatUpdate, beatUpdate, beatUpdate, dawnUpdate,
                                    breakUpdate, lightUpdate, beatUpdate, leaveUpdate};

void buildUpdate(SceneCtx& ctx, float t, float dt) {
  kBuildBeat.update(ctx, t, dt);
  ctx.w->sr.build_t = t;
}
void buildEnd(SceneCtx& ctx) {
  kBuildBeat.end(ctx);
  ctx.w->sr.build_t = 1.f;
  ctx.w->sr.stage = 1;
}
const Beat kSunriseBuild = {"build", SS_BUILD_SECONDS, nullptr, buildUpdate, buildEnd};

// ---- the build's layers ------------------------------------------------------------------------

int buildPhaseOf(float t) {
  const float sec = clamp01(t) * SS_BUILD_SECONDS;
  int p = 0;
  while (p < 5 && sec >= kPhase[p + 1]) ++p;
  return p;   // 0 sky, 1 ridges, 2 fog, 3 the slope and the ledge, 4 hold, 5 done
}

SrDraw buildDraw(const World& w, uint16_t* active) {
  const Sunrise& S = w.sr;
  const float sec = clamp01(S.build_t) * SS_BUILD_SECONDS;
  const int p = buildPhaseOf(S.build_t);
  const float u = p < 5 ? window(sec, kPhase[p], kPhase[p + 1]) : 1.f;
  SrDraw d = sunriseDrawDone(SRL_BIT(SRL_SKY));
  d.muted = S.muted;
  uint16_t act = 0;
  if (p == 0) {
    d.sky = easeInOut(window(u, 0.12f, 1.f));
    act = SRL_BIT(SRL_SKY);
  }
  if (p >= 1) d.layers = (uint16_t)(d.layers | SRL_BIT(SRL_RIDGES));
  if (p == 1) {
    for (int k = 0; k < SR_RIDGES; ++k) {
      const float a = 0.13f * (float)k;   // far leads, near follows
      d.drop[k] = kRidgeDrop * (1.f - easeOutCubic(window(u, a, a + 0.6f)));
    }
    act = SRL_BIT(SRL_RIDGES);
  }
  if (p >= 2) d.layers = (uint16_t)(d.layers | SRL_BIT(SRL_FOG));
  if (p == 2) {
    d.fog_u = u;
    act = SRL_BIT(SRL_FOG);
  }
  if (p >= 3) d.layers = (uint16_t)(d.layers | SRL_BIT(SRL_HILL) | SRL_BIT(SRL_SLAB));
  if (p == 3) {
    d.fg_drop = kFgDrop * (1.f - easeOutCubic(window(u, 0.f, 0.55f)));
    d.plants_u = window(u, 0.45f, 1.f) * 1.f - (u < 0.45f ? 1.f : 0.f);
    act = (uint16_t)(SRL_BIT(SRL_HILL) | SRL_BIT(SRL_SLAB));
  }
  if (active) *active = act;
  return d;
}

uint16_t cachedOf(uint16_t drawn, uint16_t active) {
  if (active == 0) return drawn;
  int f = 0;
  while (!(active & SRL_BIT(f))) ++f;
  return (uint16_t)(drawn & (SRL_BIT(f) - 1u));
}

// The static draw with the light as it is now.
SrDraw nowDraw(const World& w, uint16_t layers) {
  const Sunrise& S = w.sr;
  SrDraw d = sunriseDrawDone(layers);
  for (int k = 0; k < SR_RIDGES; ++k) d.lit[k] = S.lit[k];
  d.fog_glow = S.fog_glow;
  d.fog_thin = S.fog_thin;
  d.slab_lit = S.slab_lit;
  d.muted = S.muted;
  return d;
}

// ---- the sun, the stars ------------------------------------------------------------------------

int sunTop(const SunriseLand& sl, int x) {
  const int i = x - SR_SUN_X0;
  return sl.sun_top[i < 0 ? 0 : (i >= SR_SUN_W ? SR_SUN_W - 1 : i)];
}
Vec2 sunPixel(const World& w) {
  const Vec2 c = sunriseSunAt(w);
  return Vec2{floorf(c.x) + 0.5f, floorf(c.y) + 0.5f};
}
float haloR(const Sunrise& S) { return S.muted ? 0.f : S.sun_r + 7.f; }
Rect sunBounds(const World& w) {
  const Vec2 c = sunPixel(w);
  const float r = maxf(w.sr.sun_r, haloR(w.sr));
  return Rect{(int)floorf(c.x - r) - 1, (int)floorf(c.y - r) - 1, (int)ceilf(c.x + r) + 1, (int)ceilf(c.y + r) + 1};
}
// Whether any pixel of a disc of `radius` at pixel-centred `c` shows over the skyline.
bool discShown(const SunriseLand& sl, Vec2 c, float radius) {
  if (radius <= 0.f) return false;
  const float r = radius + 0.1f;
  for (int x = (int)floorf(c.x - r); x <= (int)ceilf(c.x + r); ++x) {
    if (x < 0 || x >= SS_WIDTH) continue;
    const float dx = c.x < (float)x ? (float)x - c.x : (c.x > (float)(x + 1) ? c.x - (float)(x + 1) : 0.f);
    if (dx >= r) continue;
    const float top = c.y - sqrtf(r * r - dx * dx);
    if ((int)floorf(top) < sunTop(sl, x)) return true;
  }
  return false;
}
bool sunUp(const World& w) { return w.sr.sun_r > 0.f && w.tick >= w.sr.rise_tick; }
bool sunOrHaloShown(const World& w) {
  if (!sunUp(w)) return false;
  return discShown(land(w), sunPixel(w), maxf(w.sr.sun_r, haloR(w.sr)));
}

// A disc clipped to the rows above the skyline, column run by column run.
void drawDisc(const World& w, Surface& s, const Rect& clip, Vec2 ctr, float r, Pixel col) {
  const Rect c = Rect{(int)floorf(ctr.x - r) - 1, (int)floorf(ctr.y - r) - 1, (int)ceilf(ctr.x + r) + 1, (int)ceilf(ctr.y + r) + 1}.clipped(clip);
  if (c.empty()) return;
  int x = c.x0;
  while (x < c.x1) {
    const int top = sunTop(land(w), x);
    int e = x + 1;
    while (e < c.x1 && sunTop(land(w), e) == top) ++e;
    const int y1 = top < c.y1 ? top : c.y1;
    if (y1 > c.y0) fillCircle(s, ctr, r, col, Rect{x, c.y0, e, y1});
    x = e;
  }
}

void drawSun(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Sunrise& S = w.sr;
  if (!sunOrHaloShown(w) || sunBounds(w).clipped(clip).empty()) return;
  const Vec2 c = sunPixel(w);
  if (!S.muted) drawDisc(w, s, clip, c, haloR(S), mixRGB(pal[SKY4], pal[SUN], 0.38f));
  const Pixel disc = S.muted ? mixRGB(pal[SUN], pal[CLOUD_LIT], 0.55f) : pal[SUN];
  drawDisc(w, s, clip, c, S.sun_r, disc);
}

Rect starBounds(const SrStar& s) {
  if (s.r10 == 0) return Rect{s.x, s.y, s.x + 1, s.y + 1};
  return Rect{s.x - 2, s.y - 2, s.x + 3, s.y + 3};
}
// 4 out .. 0 gone: down a step every 0.5 s to gone at `out`.
int starLevel(const SrStar& s, uint32_t tick) {
  const int dt = (int)s.out - (int)tick;   // ticks to gone
  if (dt <= 0) return 0;
  const int k = 1 + (2 * (dt - 1)) / SS_TICK_HZ;
  return k > 4 ? 4 : k;
}

void drawStars(const World& w, Surface& s, const Rect& clip, const Palette& pal, float sky) {
  for (const SrStar& st : w.sr.stars) {
    const int lvl = starLevel(st, w.tick);
    if (lvl == 0 || starBounds(st).clipped(clip).empty()) continue;
    const Pixel col = mixRGB(pal[(Slot)(SKY0 + sunriseSkyBand(st.y))], pal[STAR], 0.25f * (float)lvl * sky);
    if (st.r10 == 0) fillRect(s, Rect{st.x, st.y, st.x + 1, st.y + 1}, col, clip);
    else fillCircle(s, Vec2{(float)st.x + 0.5f, (float)st.y + 0.5f}, 0.1f * (float)st.r10, col, clip);
  }
}

void planStars(World& w, Rng& rng) {
  Sunrise& S = w.sr;
  const LoopPlan& p = w.plan;
  // Gone between the arrive's start and most of the way through the dawn, the faint ones first.
  const float t0 = (float)beatStartTick(p, RB_ARRIVE) / (float)SS_TICK_HZ + 2.f;
  const float t1 = (float)beatStartTick(p, RB_DAWN) / (float)SS_TICK_HZ + 0.85f * (float)p.fsec[RB_DAWN];
  for (int i = 0; i < SR_STARS; ++i) {
    SrStar& st = S.stars[i];
    st = SrStar{};
    const bool bright = i >= SR_STARS - SR_STARS_BRIGHT;   // the bright ones go last
    const float r = bright ? rng.range(1.6f, 2.0f) : 0.f;
    st.r10 = (uint8_t)(r * 10.f + 0.5f);
    for (int tries = 0; tries < 48; ++tries) {
      const int x = rng.rangeInt(6, SS_WIDTH - 7);
      const int y = rng.rangeInt(6, 150);
      st.x = (int16_t)x;
      st.y = (int16_t)y;
      if (st.r10 && sunriseSkyBand(y - 2) != sunriseSkyBand(y + 2)) continue;
      bool clear = true;
      for (int j = 0; j < i && clear; ++j) {
        const int dx = S.stars[j].x - x, dy = S.stars[j].y - y;
        if (dx * dx + dy * dy < 14 * 14) clear = false;
      }
      if (clear) break;
    }
    const float u = powf((float)(i + 1) / (float)SR_STARS, 0.9f);
    const float jit = rng.range(-1.5f, 1.5f);
    const float t = t0 + (t1 - t0) * u + jit;
    st.out = (uint16_t)(maxf(t0, t) * (float)SS_TICK_HZ + 0.5f);
  }
}

// ---- the props: lamp, flask, steam, the pack on the rock ----------------------------------------

Vec2 lampAt(const World& w, float* r_out) {
  float r = 0.f;
  const Vec2 h = hikerHeadPos(w.hiker, &r);
  if (r_out) *r_out = r;
  return Vec2{h.x + (float)w.hiker.facing * 0.85f * r, h.y - 0.2f * r};
}
// The cup's mouth (CARRY_MUG's body stands on his near hand).
Vec2 cupMouth(const World& w) {
  const Vec2 hand = hikerHandPos(w.hiker, 0);
  return Vec2{hand.x + (float)w.hiker.facing * 0.02f * w.hiker.height, hand.y - 0.07f * w.hiker.height};
}

Rect propsBounds(const World& w) {
  const Sunrise& S = w.sr;
  Rect r = kEmpty;
  if (!w.hiker.visible) return r;
  const float H = w.hiker.height;
  if (S.lamp > 0.5f) {
    const Vec2 l = lampAt(w, nullptr);
    const float f = (float)w.hiker.facing;
    const float xa = l.x, xb = l.x + f * 34.f;
    unite(r, Rect{(int)floorf(minf(xa, xb)) - 3, (int)floorf(l.y) - 4, (int)ceilf(maxf(xa, xb)) + 3, (int)ceilf(l.y + 30.f)});
  }
  if (S.flask > 0.f) {
    const Vec2 a = hikerHandPos(w.hiker, 1), m = cupMouth(w);
    unite(r, Rect{(int)floorf(minf(a.x, m.x) - 0.2f * H) - 2, (int)floorf(minf(a.y, m.y) - 0.2f * H) - 2,
                  (int)ceilf(maxf(a.x, m.x) + 0.2f * H) + 2, (int)ceilf(maxf(a.y, m.y) + 0.1f * H) + 2});
  }
  if (S.steam_t >= 0.f && S.cup) {
    const Vec2 m = cupMouth(w);
    unite(r, Rect{(int)floorf(m.x - 8.f), (int)floorf(m.y - 0.32f * H) - 3, (int)ceilf(m.x + 8.f), (int)ceilf(m.y) + 2});
  }
  return r;
}

void drawProps(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Sunrise& S = w.sr;
  if (!w.hiker.visible) return;
  const float H = w.hiker.height;
  const float f = (float)w.hiker.facing;
  if (S.lamp > 0.5f) {   // the headlamp: a warm point and a faint beam down onto the rock ahead
    float hr = 0.f;
    const Vec2 l = lampAt(w, &hr);
    const Vec2 beam[3] = {l, Vec2{l.x + f * 34.f, l.y + 18.f}, Vec2{l.x + f * 26.f, l.y + 29.f}};
    fillPolygonAlpha(s, beam, 3, pal[STAR], 46, clip);
    fillCircle(s, l, maxf(1.3f, 0.026f * H), pal[STAR], clip);
  }
  if (S.flask > 0.f) {   // the flask: upright in the far hand, or tipped over the cup with a thin stream
    const Vec2 a = hikerHandPos(w.hiker, 1);
    const Pixel col = mixRGB(pal[CLOUD_SHADE], pal[ACTOR], 0.35f);
    const float len = 0.12f * H, wd = maxf(2.f, 0.05f * H);
    if (S.flask > 1.5f) {
      const Vec2 m = cupMouth(w);
      const Vec2 mouth{m.x - f * 0.03f * H, m.y - 0.05f * H};
      strokeCapsule(s, Vec2{mouth.x - f * len * 0.95f, mouth.y - len * 0.35f}, mouth, wd, col, clip);
      strokeCapsule(s, mouth, Vec2{m.x, m.y + 1.f}, 1.f, pal[TRUNK], clip);
    } else {
      strokeCapsule(s, Vec2{a.x, a.y - len}, a, wd, col, clip);
    }
  }
  if (S.steam_t >= 0.f && S.cup) {   // steam off the cup: three soft wisps rising and fading in turn
    const Vec2 m = cupMouth(w);
    for (int i = 0; i < 3; ++i) {
      float ph = S.steam_t * 0.55f + (float)i / 3.f;
      ph -= floorf(ph);
      const float y = m.y - 2.f - ph * 0.3f * H;
      const float x = m.x + 2.2f * sinT(ph * 1.3f + 0.27f * (float)i);
      const float rr = 1.2f + 1.6f * ph;
      fillEllipse(s, Vec2{x, y}, rr, rr * 0.8f, mixRGB(pal[SMOKE], pal[SKY4], 0.3f + 0.5f * ph), clip);
    }
  }
}

float packH(const World& w) { return heightAt(w, w.sr.pack_at.y); }
Rect packRect(const World& w) {
  if (!w.sr.pack_down) return kEmpty;
  const Vec2 p = w.sr.pack_at;
  const float H = packH(w);
  return Rect{(int)floorf(p.x - 0.13f * H) - 2, (int)floorf(p.y - 0.33f * H) - 2, (int)ceilf(p.x + 0.13f * H) + 2, (int)ceilf(p.y) + 2};
}
void drawPack(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!w.sr.pack_down) return;
  const Vec2 p = w.sr.pack_at;
  const float H = packH(w);
  const Vec2 body[6] = {{p.x - 0.1f * H, p.y},          {p.x + 0.1f * H, p.y},          {p.x + 0.11f * H, p.y - 0.2f * H},
                        {p.x + 0.07f * H, p.y - 0.27f * H}, {p.x - 0.07f * H, p.y - 0.27f * H}, {p.x - 0.11f * H, p.y - 0.2f * H}};
  fillPolygon(s, body, 6, pal[ACTOR], clip);
  fillCircle(s, Vec2{p.x, p.y - 0.28f * H}, 0.05f * H, pal[ACTOR], clip);   // the bedroll, end on
}

// ---- the dogs ----------------------------------------------------------------------------------

Rect dogRect(const World& w, int k) {
  const SrDog& d = w.sr.dogs[k];
  if (d.slot < 0 || d.mode == DM_NONE || d.mode == DM_WAIT) return kEmpty;
  const Critter& c = w.critters[d.slot];
  return c.visible ? critterBounds(c) : kEmpty;
}

void stepDogs(World& w) {
  Sunrise& S = w.sr;
  const Hiker& h = w.hiker;
  for (int k = 0; k < 2; ++k) {
    SrDog& d = S.dogs[k];
    if (d.slot < 0 || d.mode == DM_NONE) continue;
    Critter& c = w.critters[d.slot];
    d.t += SS_TICK_DT;
    const bool want = ((w.dogs.want >> (d.tone == DT_LUCY ? 0 : 1)) & 1) != 0;
    if (!want && d.mode != DM_OFF && d.mode != DM_WAIT) {   // it went in: off the way it came
      d.mode = DM_OFF;
      critterMoveTo(c, -60.f, 70.f);
    }
    if (d.mode == DM_WAIT) {
      if (!want) continue;
      if (h.visible && h.feet.x > 70.f + 50.f * (float)k) {   // it follows him up the trail
        d.mode = DM_FOLLOW;
        critterInit(c, CR_DOG, Vec2{-40.f, slabY(w, -40.f, kPathV)}, 1);
        c.tone = d.tone;
        c.speed_dial = c.jitter_dial = 0.5f;
        c.visible = true;
      } else {
        continue;
      }
    }
    // Where it is headed: behind him on the walk, its seat once he is down, behind him again on the
    // way back.
    const float gap = 30.f + 26.f * (float)k;
    float tx = c.feet.x;
    if (d.mode == DM_FOLLOW) {
      if (S.sat_tick != 0 || (h.target == POSE_PERCH)) tx = slab(w, kDogU[k], kDogV[k]).x;
      else tx = h.feet.x - (float)h.facing * gap;
      if (fabsf(tx - c.feet.x) > 5.f) {
        if (c.moving == false || fabsf(c.move_to - tx) > 4.f) critterMoveTo(c, tx, 22.f + 0.7f * fabsf(tx - c.feet.x));
      } else if (!c.moving && (S.sat_tick != 0)) {
        if (c.facing != 1) critterMoveTo(c, c.feet.x + 1.5f, 8.f);
        else if (c.action != CA_SIT) critterSetAction(c, CA_SIT, 0.6f);
      }
    }
    critterTick(c, SS_TICK_DT);
    // Its feet on the rock (the seat's line once it is near it), its size by how far out it is.
    const float v = d.mode == DM_FOLLOW && S.sat_tick != 0 ? kDogV[k] : kPathV;
    c.feet.y = slabY(w, c.feet.x, v);
    c.scale = dogToneScale(d.tone) * heightAt(w, c.feet.y) / 90.f;
    c.tail_add = (c.action == CA_SIT ? 0.18f : 0.1f) * sinT(d.t * (d.tone == DT_LUCY ? 1.5f : 3.2f));
    if (d.mode == DM_OFF && c.feet.x < -50.f) {
      c.visible = false;
      d.mode = DM_NONE;
    }
  }
}

}  // namespace

// ---- public ------------------------------------------------------------------------------------

const char* sunriseBeatName(int beat) { return beat >= 0 && beat < RB_COUNT ? kBeatNames[beat] : "?"; }

void sunrisePlan(LoopPlan& p, Rng& rng, const Inputs&) {
  p.n_fbeats = RB_COUNT;
  for (int b = 0; b < 8; ++b) p.fvar[b] = p.fsec[b] = 0;
  for (uint8_t& s : p.pause_s) s = 0;
  p.n_fillers = 0;
  p.n_majors = 0;
  const int steps = rng.rangeInt(5, 6);    // the dawn's palette steps, one every 10 to 14 s
  p.fsteps = (uint8_t)steps;
  const int dawn = rng.rangeInt(62, 82);   // PROPOSED
  p.fsec[RB_DAWN] = (uint8_t)dawn;
  const int linger = rng.rangeInt(18, 28);
  p.fsec[RB_LINGER] = (uint8_t)linger;
  p.fbirds = 0;
}

void sunriseBeginLoop(World& w, Rng& rng) {
  Sunrise& S = w.sr;
  LoopPlan& p = w.plan;
  memset(&S, 0, sizeof S);
  sunriseLandGenerate(S.land, rng);
  // Two slow clouds high in the sky, drifting left.
  {
    const float x0 = rng.range(80.f, 380.f);
    const float y0 = rng.range(62.f, 92.f);
    cloudGenerate(w.clouds[0], rng, x0, y0, 0.4f);
    const float x1 = rng.range(330.f, 600.f);
    const float y1 = rng.range(118.f, 142.f);
    cloudGenerate(w.clouds[1], rng, x1, y1, 0.3f);
  }
  // The beats' seconds: the walks fitted to the slab.
  const float in = walkSeconds(w, trailStart(w), packStop(w));
  const float out = walkSeconds(w, seat(w), packStop(w)) + walkSeconds(w, packStop(w), trailStart(w));
  p.fsec[RB_ARRIVE] = (uint8_t)ceilf(in + 2.f);
  p.fsec[RB_SETTLE] = kSettleS;
  p.fsec[RB_POUR] = kPourS;
  p.fsec[RB_SUNBREAK] = kBreakS;
  p.fsec[RB_LIGHT] = kLightS;
  p.fsec[RB_LEAVE] = (uint8_t)ceilf(out + 2.6f + kSitS + 0.4f + 0.6f + 1.4f + 0.6f + 0.8f + kFadeS + 3.f);
  planStars(w, rng);
  // The camp's things off; the scene's own mood, its own bird, no minors.
  w.mood_override = kMood0;
  w.busy = true;
  w.amb.bird_manual = true;
  w.amb.bird.active = false;
  w.fx.on = false;
  w.hiker.visible = false;
  S.steam_t = -1.f;
  S.walk_k = kPace;
  S.d_cached = sunriseCached(w);
  S.dogs[0].slot = S.dogs[1].slot = -1;
  w.spine.fp[0] = w.spine.fp[1] = trailStart(w);
  majorsReset(w.major);
  // The hawk is drawn only right of the support cliff (it flies in the air under the ledge).
  S.hawk_cut = S.land.under.x - 4.f;
}

void sunriseInputs(World& w, const Inputs& in) {
  Sunrise& S = w.sr;
  const LoopPlan& p = w.plan;
  S.muted = in.weather.overcast >= 0.5f;   // a gray, dry morning (rain and storm never get here)
  // The sun breaks where the real one rises: the scene looks east, a little to the right of him at
  // the ledge's end; with no real sky, a little right of centre.
  float x = 650.f;
  if (in.sky.have) x = 640.f + (in.sky.sun_az - 95.f) * 2.6f;
  S.sun_x0 = x < 600.f ? 600.f : (x > 744.f ? 744.f : x);
  S.sun_r = 17.f;
  // Its centre starts just under the ridge, so its top clears it kBreakAt seconds into the sunbreak,
  // and keeps rising until there is kSunRiseEnd px of sky under it at the light's end.
  const uint32_t t_break = beatStartTick(p, RB_SUNBREAK);
  const uint32_t t_end = beatStartTick(p, RB_LINGER);
  const float d = 2.f * S.sun_r + kSunRiseEnd + 2.f;
  S.sun_v = d / (float)(t_end - t_break - (uint32_t)(kBreakAt * (float)SS_TICK_HZ));
  S.rise_tick = t_break;
  S.sun_x0 = floorf(S.sun_x0);   // a whole column (sunriseSunAt starts its centre under the ridge there)
  // The dogs that are out come up behind him and sit beside him.
  int n = 0;
  for (int k = 0; k < 2; ++k) {
    if (!((in.dog_out >> k) & 1)) continue;
    SrDog& g = S.dogs[n];
    g.slot = (int8_t)(CRITTER_STORY + k);
    g.tone = k == 0 ? DT_LUCY : DT_COPPER;
    g.mode = DM_WAIT;
    ++n;
  }
}

Vec2 sunriseSunAt(const World& w) {
  const Sunrise& S = w.sr;
  const float sky = (float)sunTop(S.land, (int)S.sun_x0);
  const float y0 = sky + S.sun_r + 1.f + S.sun_v * kBreakAt * (float)SS_TICK_HZ;
  const float t = w.tick > S.rise_tick ? (float)(w.tick - S.rise_tick) : 0.f;
  const float dy = S.sun_v * t;
  return Vec2{S.sun_x0 + 0.3f * dy, y0 - dy};
}

bool sunriseSunShown(const World& w) { return sunUp(w) && discShown(land(w), sunPixel(w), w.sr.sun_r); }

int sunriseStarsOut(const World& w) {
  int n = 0;
  for (const SrStar& s : w.sr.stars) n += starLevel(s, w.tick) > 0 ? 1 : 0;
  return n;
}

void sunriseStep(World& w) {
  Sunrise& S = w.sr;
  if (S.steam_t >= 0.f) S.steam_t += SS_TICK_DT;
  if (S.broke_tick == 0 && sunriseSunShown(w)) S.broke_tick = w.tick;
  // The hawk: in from the right below the ledge, a slow glide to the left, gone behind the cliff.
  Bird& b = w.amb.bird;
  if (S.hawk == 1 && !b.active) {
    b.active = true;
    b.dir = -1.f;
    b.x = (float)SS_WIDTH + 30.f;
    b.y0 = b.y = S.land.ft.y + S.land.face_tip + 24.f;
    b.speed = 24.f * SS_TICK_DT;
    b.path_ph = 0.2f;
    b.flap = 0.f;
    b.scale = 1.6f;
    b.slot = ACTOR;
    b.glide = 1.f;
    b.pair = false;
  }
  if (S.hawk == 1 && b.active) {
    b.flap += SS_TICK_DT * 0.9f;
    b.flap -= floorf(b.flap);
    b.x += b.dir * b.speed;
    b.y = b.y0 + 4.f * sinT(b.x / 300.f + b.path_ph);
    if (b.x < S.hawk_cut - 40.f) {
      b.active = false;
      S.hawk = 2;
    }
  }
  stepDogs(w);
}

void sunriseHikerPre(World& w) {
  Hiker& h = w.hiker;
  // On the rock his size follows how far out along it he is; sitting, it holds.
  if (w.sr.sat_tick == 0 && h.target != POSE_PERCH) h.height = heightAt(w, h.feet.y);
  if (h.walking) h.walk_speed = w.sr.walk_k * h.height;
}

uint16_t sunriseCached(const World& w) {
  if (w.sr.stage == 0) {
    uint16_t act = 0;
    const SrDraw d = buildDraw(w, &act);
    return cachedOf(d.layers, act);
  }
  return SRL_ALL;
}

int sunriseBuildPhase(const World& w) { return w.sr.stage == 0 ? buildPhaseOf(w.sr.build_t) : 5; }

void sunriseBuildRows(const World& w, int& y0, int& y1) {
  y0 = y1 = 0;
  if (w.sr.stage != 0) return;
  switch (buildPhaseOf(w.sr.build_t)) {
    case 0: y0 = 0; y1 = SS_HEIGHT; break;
    case 1: y0 = 170; y1 = SS_HEIGHT; break;
    case 2: y0 = (int)w.sr.land.fog_top - 2; y1 = (int)w.sr.land.fog_bot + 2; break;
    case 3: y0 = (int)minf(w.sr.land.hill[0], w.sr.land.bt.y) - 60; y1 = SS_HEIGHT; break;
    default: break;
  }
}

void sunriseDrawStatic(const World& w, Surface& frame, const Palette& pal) {
  sunriseLandDraw(w.sr.land, frame, frame.bounds(), pal, nowDraw(w, sunriseCached(w)));
}

void sunriseDrawDynamic(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Sunrise& S = w.sr;
  const uint16_t cached = sunriseCached(w);
  const SrDraw d = S.stage == 0 ? buildDraw(w, nullptr) : nowDraw(w, SRL_ALL);
  const uint16_t dyn = (uint16_t)(d.layers & ~cached);

  // 1. The sky while it fades in, then the sky's things: the stars, the sun, the clouds.
  if (dyn & SRL_BIT(SRL_SKY)) {
    SrDraw sd = d;
    sd.layers = SRL_BIT(SRL_SKY);
    sunriseLandDraw(S.land, s, clip, pal, sd);
  }
  const float sky = S.stage == 0 && buildPhaseOf(S.build_t) == 0 ? d.sky : 1.f;
  drawStars(w, s, clip, pal, sky);
  drawSun(w, s, clip, pal);
  if (sky < 1.f) {
    Palette cp = pal;
    cp.c[CLOUD_LIT] = mixRGB(pal[SKY4], pal[CLOUD_LIT], sky);
    cp.c[CLOUD_SHADE] = mixRGB(pal[SKY4], pal[CLOUD_SHADE], sky);
    for (const Cloud& c : w.clouds) cloudDraw(c, s, cp, clip);
  } else {
    for (const Cloud& c : w.clouds) cloudDraw(c, s, pal, clip);
  }

  // 2. The build's layers in front of the sky, animating or in front of one that is.
  if (dyn & ~SRL_BIT(SRL_SKY)) {
    SrDraw ld = d;
    ld.layers = (uint16_t)(dyn & ~SRL_BIT(SRL_SKY));
    sunriseLandDraw(S.land, s, clip, pal, ld);
  }

  // 3. The hawk in the air under the ledge (not over the cliff).
  if (w.amb.bird.active) {
    const Rect hc = clip.clipped(Rect{(int)ceilf(S.hawk_cut), 0, SS_WIDTH, SS_HEIGHT});
    if (!hc.empty()) ambientDrawBird(w, s, hc, pal);
  }

  // 4. On the rock, far to near: the pack, the dogs, him with his lamp, flask and steam.
  struct Ord {
    int kind;   // 0 the pack, 1 + k a dog, 9 him
    float y;
  } items[4];
  int n = 0;
  if (S.pack_down) items[n++] = Ord{0, S.pack_at.y};
  for (int k = 0; k < 2; ++k)
    if (!dogRect(w, k).empty()) items[n++] = Ord{1 + k, w.critters[S.dogs[k].slot].feet.y};
  if (w.hiker.visible) items[n++] = Ord{9, w.hiker.feet.y - (S.sat_tick ? kSitDrop * w.hiker.height : 0.f)};
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && items[j - 1].y > items[j].y; --j) {
      const Ord t = items[j];
      items[j] = items[j - 1];
      items[j - 1] = t;
    }
  for (int i = 0; i < n; ++i) {
    if (items[i].kind == 0) {
      drawPack(w, s, clip, pal);
    } else if (items[i].kind == 9) {
      hikerDraw(w.hiker, s, pal, clip);
      drawProps(w, s, clip, pal);
    } else {
      critterDraw(w.critters[S.dogs[items[i].kind - 1].slot], s, pal, clip);
    }
  }

  // 5. The loop's fade, over everything.
  if (w.fade > 0.f) {
    const float a = w.fade >= 1.f ? 255.f : w.fade * 255.f + 0.5f;
    const uint8_t alpha = (uint8_t)a;
    for (int y = clip.y0; y < clip.y1; ++y) blendSpan(s, y, clip.x0, clip.x1, pal[ACTOR], alpha);
  }
}

void sunriseMarkDirty(World& w, Renderer& r) {
  Sunrise& S = w.sr;
  // The sun (with its glow) where it was and where it is, when its pixels move.
  const Rect cur = sunOrHaloShown(w) ? sunBounds(w) : kEmpty;
  if (!sameRect(cur, S.d_sun)) {
    if (!S.d_sun.empty()) r.markDirtyRect(S.d_sun);
    if (!cur.empty()) r.markDirtyRect(cur);
    S.d_sun = cur;
  }
  for (SrStar& st : S.stars) {
    const int lvl = starLevel(st, w.tick);
    if (lvl != st.lvl_d) {
      r.markDirtyRect(starBounds(st));
      st.lvl_d = (uint8_t)lvl;
    }
  }
  // The light's steps: repaint the cache's rows that ridge (or the fog, or the slab) covers.
  for (int k = 0; k < SR_RIDGES; ++k) {
    if (S.lit[k] == S.d_lit[k]) continue;
    int y0, y1;
    sunriseRidgeRows(S.land, k, y0, y1);
    r.repaintRows(y0, y1);
    r.markDirtyRows(y0, y1);
    S.d_lit[k] = S.lit[k];
  }
  if (S.fog_glow != S.d_fog_glow || S.fog_thin != S.d_fog_thin) {
    int y0, y1;
    sunriseRidgeRows(S.land, -1, y0, y1);
    r.repaintRows(y0, y1);
    r.markDirtyRows(y0, y1);
    S.d_fog_glow = S.fog_glow;
    S.d_fog_thin = S.fog_thin;
  }
  if (S.slab_lit != S.d_slab_lit) {
    int y0, y1;
    sunriseRidgeRows(S.land, SR_RIDGES, y0, y1);
    r.repaintRows(y0, y1);
    r.markDirtyRows(y0, y1);
    S.d_slab_lit = S.slab_lit;
  }
  // The props move with his breath and the steam drifts: marked every tick they show.
  const Rect pr = propsBounds(w);
  if (!pr.empty() || !S.d_props.empty()) {
    if (!S.d_props.empty()) r.markDirtyRect(S.d_props);
    if (!pr.empty()) r.markDirtyRect(pr);
    S.d_props = pr;
  }
  const Rect pk = packRect(w);
  if (!sameRect(pk, S.d_pack)) {
    if (!S.d_pack.empty()) r.markDirtyRect(S.d_pack);
    if (!pk.empty()) r.markDirtyRect(pk);
    S.d_pack = pk;
  }
  for (int k = 0; k < 2; ++k) {
    const Rect dr = dogRect(w, k);
    if (dr.empty() && S.d_dog[k].empty()) continue;
    if (!S.d_dog[k].empty()) r.markDirtyRect(S.d_dog[k]);
    if (!dr.empty()) r.markDirtyRect(dr);
    S.d_dog[k] = dr;
  }
}

void sunriseBuildTimeline(Timeline& tl, const LoopPlan& p) {
  tl.add(kSunriseBuild);
  for (int b = 0; b < RB_COUNT; ++b) tl.add(Beat{kBeatNames[b], (float)p.fsec[b], kBegin[b], kUpdate[b], beatEnd});
}

uint32_t sunriseLoopTicks(const LoopPlan& p) {
  uint32_t t = buildTicks();
  for (int b = 0; b < RB_COUNT; ++b) t += (uint32_t)p.fsec[b] * SS_TICK_HZ;
  return t;
}

int sunriseFormatPlan(const LoopPlan& p, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int k = snprintf(buf, n, "family=sunrise");
  char tmp[32];
  for (int b = 0; b < RB_COUNT && k >= 0 && (size_t)k < n; ++b) {
    snprintf(tmp, sizeof tmp, " %s=%d", kBeatNames[b], (int)p.fsec[b]);
    const int m = snprintf(buf + k, n - (size_t)k, "%s", tmp);
    if (m > 0) k += m;
  }
  if (k >= 0 && (size_t)k < n) {
    const int m = snprintf(buf + k, n - (size_t)k, " steps=%d", (int)p.fsteps);
    if (m > 0) k += m;
  }
  return k;
}

void sunrisePaletteTweak(const World& w, float mood, const WeatherTint& t, Palette& p) {
  if (t.night_dim > 0.f) return;   // the night rule's caps stand: no added colour
  const float k = w.sr.muted ? 0.4f : 1.f;
  // Rose low in the sky toward the dawn's end, turning gold through the sunbreak and the light.
  const float rose = clamp01((mood - 0.5f) / 0.4f) * (1.f - clamp01((mood - 1.05f) / 0.25f));
  const float gold = clamp01((mood - 1.05f) / 0.25f);
  const Pixel R = rgb565_hex(0xDC8C92), G = rgb565_hex(0xF2C27A);   // PROPOSED, unverified on the panel
  const float a4 = k * (0.48f * rose + 0.42f * gold), a3 = k * (0.32f * rose + 0.24f * gold), a2 = k * 0.14f * rose;
  if (a4 > 0.f) p.c[SKY4] = mixRGB(p.c[SKY4], gold > rose ? G : R, a4);
  if (a3 > 0.f) p.c[SKY3] = mixRGB(p.c[SKY3], gold > rose ? G : R, a3);
  if (a2 > 0.f) p.c[SKY2] = mixRGB(p.c[SKY2], R, a2);
  if (rose + gold > 0.f) {
    const float c = k * (0.32f * rose + 0.3f * gold);
    p.c[CLOUD_LIT] = mixRGB(p.c[CLOUD_LIT], gold > rose ? G : R, c);
  }
  p.c[SUN] = w.sr.muted ? mixRGB(rgb565_hex(0xF2C27A), p.c[CLOUD_LIT], 0.4f) : rgb565_hex(0xF7AE4A);
}

}  // namespace ss
