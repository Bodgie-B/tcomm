// The morning spine (SF_MORNING): he wakes in the fog, makes coffee, takes the
// bag down, breaks camp, douses the fire and goes. SPINES_WEATHER_LOOKOUT_SPEC.md
// section 2; the framework is section 1 (spine_family.h).
//
// Same idioms as spine.cpp: actions take u 0..1 and leave the world at rest at
// u = 1; scripts are const Step tables (script.h); a beat's seconds are fixed
// per variant and longer than its script (an overrun counts in
// SpineState::overruns, tests require zero). One rng call per statement,
// table sines, no doubles, no allocation.
//
// Loop start (morningLayout): the camp as he left it last night, pitched,
// the fire out under its ash, the bag hung, the pack by the tent, the cairn
// one stone shorter than it was left; he is asleep in the tent. The camp's
// props pop in with the plants in the build (morningBuildUpdate), so the build
// still reads as the scene assembling. The loop opens in fog
// (World::fog_extra) that lifts, in 0.05 steps, from the end of the build to
// the end of the coffee's pause (morningTick).
//
// Timeline: build, wake, coffee, bag_down, break_camp, douse, leave, each but
// the last followed by a pause; fillers (read, cairn, cloud_watch,
// stream_listen, birds_on_water) after coffee and break_camp; majors after
// wake, coffee and bag_down (he is outside for all of them). The leave's last
// 2.5 s fade to black as the evening's close does.
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

// s in seconds: 0 -> 1 over [c, c + up], held for `hold`, back to 0 over `up`.
float plateau(float s, float c, float up, float hold) {
  return easeInOut(window(s, c, c + up)) * (1.f - easeInOut(window(s, c + up + hold, c + 2.f * up + hold)));
}
inline float envelope(float u, float a, float b) { return scriptEnvelope(u, a, b); }
inline void setArm(World& w, int i, float k, float sh, float el) { scriptSetArm(w, i, k, sh, el); }
inline void fxRest(World& w) { scriptFxRest(w); }
inline void setPose(World& w, PoseId p, float s) { scriptSetPose(w, p, s); }
Vec2 besideHim(const Hiker& h, float dx) { return Vec2{h.feet.x + (float)h.facing * dx, h.feet.y + 2.f}; }

constexpr float kFadeS = 2.5f;
constexpr float kLowFuel = 0.36f;   // the coffee fire: a small flame
constexpr float kBirdUp = 128.f;    // where the wake c bird comes from and goes (frame y)
constexpr float kChipRun = 70.f;

// ---- the morning's critters (a bird on the tent, a chipmunk on the rock) ----------------
// One slot (SpineState::fcrit), tagged CT_DINNER as the evening's beat-owned
// critters are, driven from morningTick by SpineState::fstage. The scripts
// start them (doit hooks) and wait on them (until).

enum CritStage : uint8_t {
  FC_NONE = 0,
  FC_BIRD_IN, FC_BIRD_TAP, FC_BIRD_WAIT, FC_BIRD_OFF,       // wake c
  FC_CHIP_IN, FC_CHIP_WATCH, FC_CHIP_OFF                    // coffee c
};

Critter* critOf(World& w) {
  const int s = w.spine.fcrit;
  return s >= 0 && directorOwns(w, s, CT_DINNER) ? &w.critters[s] : nullptr;
}
bool critGone(const World& w) {
  const int s = w.spine.fcrit;
  return s < 0 || !w.crit[s].in_use || w.crit[s].tag != CT_DINNER;
}

Vec2 bez(Vec2 a, Vec2 c, Vec2 b, float u) {
  const float v = 1.f - u;
  return Vec2{v * v * a.x + 2.f * v * u * c.x + u * u * b.x, v * v * a.y + 2.f * v * u * c.y + u * u * b.y};
}
void flyAlong(Critter& c, Vec2 a, Vec2 ctl, Vec2 b, float u) {
  const Vec2 p = bez(a, ctl, b, u);
  if (fabsf(p.x - c.feet.x) > 0.05f) c.facing = p.x < c.feet.x ? -1 : 1;
  c.feet = p;
}

// The tent's front apex, where the bird lands (props.cpp tentGeom: kTentFrontX
// 14, kTentFrontH 50; keep in step).
Vec2 ridgeTop(const World& w) { return Vec2{w.tent.pos.x + 11.f, w.tent.pos.y - 49.f}; }

void stageTo(World& w, uint8_t s) {
  w.spine.fstage = s;
  w.spine.ft = 0.f;
}

// Wake c: a bird swoops in from the upper right and lands on the ridge.
void doBirdIn(World& w, float) {
  SpineState& sp = w.spine;
  const Vec2 from{(float)SS_WIDTH + 24.f, kBirdUp};
  const int s = directorSpawn(w, CR_BIRD, from, -1);
  if (s < 0) return;
  w.crit[s].tag = CT_DINNER;
  w.crit[s].flags = CF_BRAVE;
  w.crit[s].sort_y = (int16_t)(w.tent.pos.y + 1.f);   // in front of the tent it stands on
  sp.fcrit = (int8_t)s;
  critterSetAction(w.critters[s], CA_MOVE, 0.1f);
  stageTo(w, FC_BIRD_IN);
  w.busy = true;
}
bool birdLanded(const World& w) { return critGone(w) || w.spine.fstage >= FC_BIRD_WAIT; }
// He bursts out: it goes, up and away over the camp to the left.
void doBirdOff(World& w, float) {
  if (Critter* c = critOf(w)) {
    critterSetAction(*c, CA_MOVE, 0.12f);
    w.spine.fp[1] = c->feet;
    stageTo(w, FC_BIRD_OFF);
  }
}

// Coffee c: a chipmunk runs in from the left behind the tent and sits up on
// the reading rock, watching the mug.
Vec2 chipRock(const World& w) { return Vec2{w.fire.pos.x + 14.f, 333.f}; }   // on the rock's top (spineLayout: rock at F.x + 18, top ~332)
void doChipIn(World& w, float) {
  SpineState& sp = w.spine;
  const Vec2 from{-16.f, 337.f};
  const int s = directorSpawn(w, CR_CHIPMUNK, from, 1);
  if (s < 0) return;
  w.crit[s].tag = CT_DINNER;
  w.crit[s].flags = CF_BRAVE;
  memoryApplyPersona(w.memory, w.critters[s]);
  sp.fcrit = (int8_t)s;
  directorWalk(w, s, chipRock(w), kChipRun, CA_RUN);
  stageTo(w, FC_CHIP_IN);
  w.busy = true;
}
bool chipArrived(const World& w) { return critGone(w) || w.spine.fstage >= FC_CHIP_WATCH; }
void doChipOff(World& w, float) {
  if (critOf(w)) {
    directorExit(w, w.spine.fcrit, -1, kChipRun, CA_RUN);
    stageTo(w, FC_CHIP_OFF);
  }
}

void critTick(World& w) {
  SpineState& sp = w.spine;
  if (sp.fcrit < 0) return;
  if (critGone(w)) {   // released (out of sight), or never spawned
    sp.fcrit = -1;
    sp.fstage = FC_NONE;
    w.busy = false;
    return;
  }
  Critter& c = w.critters[sp.fcrit];
  sp.ft += SS_TICK_DT;
  const float t = sp.ft;
  switch (sp.fstage) {
    case FC_BIRD_IN: {
      const Vec2 a{(float)SS_WIDTH + 24.f, kBirdUp}, b = ridgeTop(w);
      const float u = clamp01(t / 2.6f);
      flyAlong(c, a, Vec2{lerp(a.x, b.x, 0.55f), b.y + 22.f}, b, easeOutCubic(u));
      if (u >= 1.f) {
        c.facing = -1;
        critterSetAction(c, CA_IDLE, 0.25f);
        stageTo(w, FC_BIRD_TAP);
      }
      break;
    }
    case FC_BIRD_TAP: {   // three pecks at the fabric; the tent twitches with each
      if (t > 0.6f && t - SS_TICK_DT <= 0.6f) critterSetAction(c, CA_EAT, 0.2f);
      const float k = t - 0.6f;
      float tw = 0.f;
      for (int i = 0; i < 3; ++i) tw = maxf(tw, plateau(k, 0.25f + 0.55f * (float)i, 0.06f, 0.04f));
      w.tent.wobble = floorf(0.5f + 1.5f * tw);
      if (t > 2.6f) {
        w.tent.wobble = 0.f;
        critterSetAction(c, CA_LOOK, 0.25f);
        stageTo(w, FC_BIRD_WAIT);
      }
      break;
    }
    case FC_BIRD_OFF: {
      const Vec2 a = sp.fp[1], b{-30.f, kBirdUp - 10.f};
      const float u = clamp01(t / 2.2f);
      flyAlong(c, a, Vec2{lerp(a.x, b.x, 0.4f), a.y - 50.f}, b, easeInOut(u));
      if (u >= 1.f) directorRelease(w, sp.fcrit);
      break;
    }
    case FC_CHIP_IN:
      if (critterArrived(c)) {
        c.facing = 1;
        critterSetAction(c, CA_SIT, 0.3f);
        stageTo(w, FC_CHIP_WATCH);
      }
      break;
    case FC_CHIP_WATCH:   // sits up, looks, sniffs toward the mug
      if (t > 3.f && t - SS_TICK_DT <= 3.f) critterSetAction(c, CA_SNIFF, 0.3f);
      if (t > 6.f && t - SS_TICK_DT <= 6.f) critterSetAction(c, CA_SIT, 0.3f);
      break;
    default: break;
  }
}

// ---- wake -----------------------------------------------------------------------------

// He stirs: the tent bulges and twitches a few times.
void actStir(World& w, float u) {
  const float s = u * 2.4f;
  const float k = plateau(s, 0.1f, 0.2f, 0.3f) - 0.7f * plateau(s, 1.1f, 0.15f, 0.2f) + 0.6f * plateau(s, 1.7f, 0.15f, 0.1f);
  w.tent.wobble = u >= 1.f ? 0.f : floorf(0.5f + 2.5f * k);
}
// The door unzipped from inside: the flap rolls open in two tugs.
void actUnzip(World& w, float u) {
  Tent& t = w.tent;
  t.door = easeInOut(window(u, 0.f, 0.45f)) * 0.45f + easeInOut(window(u, 0.55f, 1.f)) * 0.55f;
  t.wobble = u >= 1.f ? 0.f : floorf(0.5f + 1.2f * (plateau(u, 0.05f, 0.05f, 0.1f) + plateau(u, 0.55f, 0.05f, 0.1f)));
}
// Wake c: the bird has him up: the flap thrown open at once.
void actBurstDoor(World& w, float u) {
  w.tent.door = easeOutCubic(u);
  w.tent.wobble = u >= 1.f ? 0.f : floorf(0.5f + 3.f * sinT(1.5f * u) * (1.f - u));
}

// Out through the door on all fours: crouched and pitched forward, hands on
// the ground ahead, two shuffles forward.
void doCrawlOut(World& w, float) {
  Hiker& h = w.hiker;
  hikerInit(h, Vec2{w.spine.camp.door.x - 6.f, w.spine.camp.door.y}, 1);
  hikerSetPose(h, POSE_CROUCH, 0.f);
  h.pack_on = false;
  w.fx.on = true;
  w.fx.path_on = false;
  w.fx.lean = 0.5f;
  w.fx.head = -0.35f;
  setArm(w, 0, 1.f, 0.55f, 0.1f);
  setArm(w, 1, 1.f, 0.35f, 0.1f);
}
void actCrawl(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) w.spine.u0 = h.feet.x;
  const float k = 1.f - easeInOut(window(u, 0.75f, 1.f));   // straightening up at the end
  const float ph = window(u, 0.f, 0.75f) * 2.f;              // two shuffles
  h.feet.x = w.spine.u0 + 14.f * easeInOut(window(u, 0.f, 0.75f));
  w.fx.lean = 0.5f * k;
  w.fx.head = -0.35f * k;
  w.fx.lift = 0.012f * k * sinT(ph) * sinT(ph);
  setArm(w, 0, k, 0.55f + 0.18f * sinT(ph), 0.1f);
  setArm(w, 1, k, 0.35f - 0.18f * sinT(ph), 0.1f);
  if (u >= 1.f) fxRest(w);
}
// Wake c: out in one go, crouched, then up with a start.
void doBurstOut(World& w, float) {
  Hiker& h = w.hiker;
  hikerInit(h, Vec2{w.spine.camp.door.x + 4.f, w.spine.camp.door.y}, 1);
  hikerSetPose(h, POSE_CROUCH, 0.f);
  h.pack_on = false;
  w.fx.on = true;
  w.fx.path_on = false;
}

// The big stretch: arms up (POSE_REACH_UP), up on his toes, leaning back a little.
void actStretch(World& w, float u) {
  const float k = envelope(u, 0.25f, 0.3f);
  w.fx.lean = -0.12f * k;
  w.fx.lift = 0.014f * k;
  w.fx.head = -0.2f * k;
  if (u >= 1.f) fxRest(w);
}

// Wake b: the thing he left by the door last night (the shoe, if the raccoon
// does not have it; else the mug), under his foot as he steps out.
// He treads on it: a yelp, then three hops holding the foot (one arm down to
// it), backing off a few px.
void actTread(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    w.spine.u0 = h.feet.x;
    setPose(w, POSE_YELP, 0.25f);
  }
  if (u >= 0.18f && h.target == POSE_YELP) setPose(w, POSE_STAND, 0.4f);
  const float hops = window(u, 0.2f, 0.9f) * 3.f;
  const float hop = u >= 0.2f && u < 0.9f ? fabsf(sinT(0.5f * hops)) : 0.f;
  const float k = envelope(u, 0.2f, 0.15f);
  w.fx.lift = 0.05f * hop;
  w.fx.lean = 0.25f * k;
  w.fx.head = 0.15f * k;
  setArm(w, 0, k, 0.55f, -0.25f);
  setArm(w, 1, 0.5f * k, -0.4f, 0.9f);
  h.feet.x = w.spine.u0 - (float)h.facing * 10.f * easeInOut(window(u, 0.2f, 0.9f));
  if (u >= 1.f) fxRest(w);
}
// Picked up: the shoe goes on (gone); the mug he keeps in his hand for the
// pack. SpineState::target says which it was for the steps that follow.
// SpineState::target is scratch per beat (beatPoints clears it): wake b's
// what-he-trod-on, break_camp b's fly-in-hands (a value no counter reaches),
// douse a's kick count.
constexpr uint8_t kGotShoe = 2, kGotMug = 3, kFlyHeld = 0xF1;
void doDoorItemUp(World& w, float) {
  if (w.items[ITEM_SHOE].down) {
    w.items[ITEM_SHOE].down = false;
    w.spine.target = kGotShoe;
  } else {
    w.items[ITEM_MUG].down = false;
    hikerCarry(w.hiker, CARRY_MUG);
    w.spine.target = kGotMug;
  }
}
bool gotMug(const World& w) { return w.spine.target == kGotMug; }
bool gotShoe(const World& w) { return w.spine.target == kGotShoe; }
void doIntoPack(World& w, float) {   // whatever is in his hands goes in the pack
  hikerCarry(w.hiker, CARRY_NONE);
  Item& pot = w.items[ITEM_POT];
  if (pot.held) {
    pot.held = false;
    pot.down = false;
  }
}
// Puts the shoe on: a lean over the foot.
void actShoeOn(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.25f);
  w.fx.lean = 0.2f * k;
  w.fx.reach_to = 0.2f;
  w.fx.reach = 0.7f * k * (0.8f + 0.2f * sinT(u * 3.f));
  if (u >= 1.f) fxRest(w);
}

// The wake c start: looks up after the bird, a hand to his head.
void actWatchBird(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.2f);
  w.fx.head = -0.5f * k;
  w.fx.lean = -0.08f * k;
  if (u >= 1.f) fxRest(w);
}

// ---- coffee ---------------------------------------------------------------------------

// Digs in the pack: leans in, the arm working.
void actRummage(World& w, float u) {
  const float env = envelope(u, 0.15f, 0.15f);
  w.fx.lean = env * (0.12f + 0.06f * sinT(u * 3.f));
  w.fx.reach_to = 0.9f;
  w.fx.reach = env * (0.5f + 0.3f * sinT(u * 4.f + 0.25f));
  if (u >= 1.f) fxRest(w);
}

Vec2 potOnFire(const World& w) { return Vec2{w.fire.pos.x, w.fire.pos.y - 5.f}; }
void doPotUp(World& w, float) {
  Item& p = w.items[ITEM_POT];
  p.held = true;
  p.down = false;
  p.on_fire = false;
  hikerCarry(w.hiker, CARRY_MUG);
}
// Onto the cold pit (or the low fire), from his hands.
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
// Rekindling last night's embers under the ash: three puffs, each flaring it a
// little higher, the ash thinning; it catches small.
void actRekindle(World& w, float u) {
  FirePit& f = w.fire;
  f.state = FIRE_KINDLING;
  const float k = u * 3.f;
  const float i = floorf(k);
  const float ph = k - i;
  const float base = 0.3f * (i / 3.f);
  const float flare = sinT(0.5f * window(ph, 0.1f, 0.6f));
  f.kindle = clamp01(base + (0.4f + 0.08f * i - base) * flare);
  f.ash = 1.f - 0.6f * easeInOut(u);
  if (u >= 1.f) {
    f.state = FIRE_LIT;
    f.fuel = kLowFuel;
    f.kindle = 0.f;
  }
}
// The pot to the mug: a reach to the pot on the fire and back.
void actPour(World& w, float u) {
  const float k = envelope(u, 0.25f, 0.3f);
  w.fx.reach_to = 1.2f;
  w.fx.reach = 0.85f * k;
  w.fx.lean = 0.1f * k;
  if (u >= 1.f) fxRest(w);
}

void doMugUp(World& w, float) {
  w.items[ITEM_MUG].down = false;
  w.items[ITEM_MUG].held = false;
  hikerCarry(w.hiker, CARRY_MUG);
}
void doMugDown(World& w, float) {
  hikerCarry(w.hiker, CARRY_NONE);
  scriptSetDown(w, ITEM_MUG, besideHim(w.hiker, 14.f));
}

// Sitting with the mug at his chest; a sip lifts it to his mouth and tips his
// head back (the evening's mugHold).
void mugHold(World& w, float env, float sip, float head) {
  setArm(w, 0, env, lerp(0.2f, 0.9f, sip), lerp(2.3f, 2.45f, sip));
  w.fx.head = env * (head - 0.32f * sip);
}

// Coffee a: three slow sips and long still looks out into the lifting fog.
constexpr float kSipFogS = 24.f;
void actSipFog(World& w, float u) {
  const float s = u * kSipFogS;
  float sip = 0.f;
  for (int k = 0; k < 3; ++k) sip = maxf(sip, plateau(s, 2.5f + 8.f * (float)k, 0.6f, 1.0f));
  mugHold(w, envelope(u, 0.04f, 0.04f), sip, -0.06f);
  if (u >= 1.f) fxRest(w);
}
// Coffee c, while the chipmunk comes: one sip.
constexpr float kSipOneS = 5.f;
void actSipOne(World& w, float u) {
  const float s = u * kSipOneS;
  mugHold(w, envelope(u, 0.08f, 0.08f), plateau(s, 1.6f, 0.6f, 1.0f), -0.06f);
  if (u >= 1.f) fxRest(w);
}
// Coffee b: the first sip burns: he jerks back, fans his mouth with the other
// hand; then blows on the mug (held up close, head down) before every sip.
constexpr float kBurnS = 24.f;
void actBurn(World& w, float u) {
  const float s = u * kBurnS;
  const float env = envelope(u, 0.03f, 0.04f);
  const float sip0 = plateau(s, 1.5f, 0.5f, 0.3f);
  const float yelp = plateau(s, 2.4f, 0.12f, 0.9f);
  const float fan = plateau(s, 2.6f, 0.2f, 2.6f);
  // Two careful sips later, each after a blow.
  float blow = 0.f, sip = sip0;
  for (int k = 0; k < 2; ++k) {
    const float c = 9.f + 7.5f * (float)k;
    blow = maxf(blow, plateau(s, c, 0.5f, 1.6f));
    sip = maxf(sip, plateau(s, c + 2.8f, 0.5f, 0.8f));
  }
  setArm(w, 0, env, lerp(lerp(0.2f, 0.9f, sip), 0.75f, blow), lerp(lerp(2.3f, 2.45f, sip), 2.25f, blow));
  w.fx.head = env * (-0.06f - 0.32f * sip + 0.22f * blow) - 0.3f * yelp;
  w.fx.lean = -0.14f * yelp + 0.06f * blow;
  // The far hand fans in front of his mouth.
  setArm(w, 1, fan, 1.05f, 1.75f + 0.45f * sinT(s * 2.6f));
  if (u >= 1.f) fxRest(w);
}
// Coffee c: the chipmunk on the rock behind him has its eye on the mug. He
// notices, and holds the mug in close, hunched over it, until it gives up.
constexpr float kGuardS = 12.f;
void actGuard(World& w, float u) {
  const float s = u * kGuardS;
  const float env = envelope(u, 0.04f, 0.06f);
  const float look = plateau(s, 0.6f, 0.4f, 1.6f);       // a glance back
  const float close = easeInOut(window(s, 2.4f, 3.2f));   // and in it comes
  setArm(w, 0, env, lerp(0.2f, 0.02f, close), lerp(2.3f, 2.62f, close));
  w.fx.head = env * (-0.06f + 0.3f * look + 0.12f * close);
  w.fx.lean = env * 0.16f * close;
  if (u >= 1.f) {
    // Still holding it close when the beat moves on: the next action eases it.
    setArm(w, 0, 1.f, 0.02f, 2.62f);
    w.fx.lean = 0.16f;
    w.fx.head = 0.06f;
  }
}
// It is gone: he sits back and has his sip in peace.
constexpr float kGuardSipS = 7.f;
void actGuardSip(World& w, float u) {
  const float s = u * kGuardSipS;
  const float back = easeInOut(window(s, 0.f, 1.2f));
  const float sip = plateau(s, 2.6f, 0.6f, 1.2f);
  const float env = 1.f - easeInOut(window(u, 0.9f, 1.f));
  setArm(w, 0, env, lerp(lerp(0.02f, 0.2f, back), 0.9f, sip), lerp(lerp(2.62f, 2.3f, back), 2.45f, sip));
  w.fx.head = env * (lerp(0.06f, -0.06f, back) - 0.32f * sip);
  w.fx.lean = env * 0.16f * (1.f - back);
  if (u >= 1.f) fxRest(w);
}

// ---- bag_down -------------------------------------------------------------------------

float bagGroundLine(const World& w) { return w.spine.camp.bag_work.y - 2.f - w.bag.tip.y - 14.9f; }

// Hand over hand on the rope: both arms up, working in turn.
void ropeHands(World& w, float k, float ph) {
  setArm(w, 0, k, 2.6f + 0.25f * sinT(ph), 0.25f);
  setArm(w, 1, k, 2.6f - 0.25f * sinT(ph), 0.25f);
}
// Lowered by the rope, down to the ground.
void actLower(World& w, float u) {
  FoodBag& b = w.bag;
  if (u <= 0.f) w.spine.u0 = b.line;
  b.line = lerp(w.spine.u0, bagGroundLine(w), easeInOut(u));
  b.swing = 0.05f * sinT(2.f * u) * (1.f - u);
  ropeHands(w, envelope(u, 0.1f, 0.15f), u * 4.f);
  if (u >= 1.f) {
    b.swing = 0.f;
    fxRest(w);
  }
}
// Bag_down b: lowered part way (still above his reach), and it sticks.
constexpr float kStuckLine = 32.f;
void actLowerHalf(World& w, float u) {
  FoodBag& b = w.bag;
  if (u <= 0.f) w.spine.u0 = b.line;
  b.line = lerp(w.spine.u0, kStuckLine, easeInOut(u));
  ropeHands(w, easeInOut(window(u, 0.f, 0.15f)), u * 2.f);
}
// Two yanks: he leans back hard, the bag jumps and swings on its line.
void actYank(World& w, float u) {
  FoodBag& b = w.bag;
  const float s = u * 2.4f;
  const float y = maxf(plateau(s, 0.2f, 0.12f, 0.15f), plateau(s, 1.2f, 0.12f, 0.15f));
  setArm(w, 0, 1.f, 2.35f - 0.3f * y, 0.2f);
  setArm(w, 1, 1.f, 2.35f - 0.3f * y, 0.2f);
  w.fx.lean = -0.22f * y;
  b.line = kStuckLine - 5.f * y;
  b.swing = 0.18f * sinT(1.6f * u) * (1.f - u * 0.5f);
}
// It lets go: the sack drops from its line onto his head (he still has the
// rope), bounces off and lands beside him.
void actBagOnHead(World& w, float u) {
  FoodBag& b = w.bag;
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    Vec2 neck{b.tip.x + sinT(b.swing / 6.2831853f) * b.line, b.tip.y + b.line};
    w.spine.v0 = neck;
    b.state = BAG_FLYING;
    setArm(w, 1, 0.f, 0.f, 0.f);
  }
  float r = 0.f;
  const Vec2 head = hikerHeadPos(h, &r);
  const Vec2 on{head.x, head.y - r - 14.f};   // the sack's bottom on the crown of his hat
  const Vec2 land{h.feet.x + (float)h.facing * 18.f, h.feet.y + 1.f};
  if (u < 0.4f) {
    const float k = easeInCubic(window(u, 0.f, 0.4f));
    b.pos = Vec2{lerp(w.spine.v0.x, on.x, k), lerp(w.spine.v0.y, on.y, k)};
    b.swing = 0.2f * (1.f - k);
  } else {
    const float k = window(u, 0.4f, 0.85f);
    b.pos = Vec2{lerp(on.x, land.x, k), lerp(on.y, land.y - 15.f, k) - 18.f * 4.f * k * (1.f - k)};
    b.swing = 1.2f * k;
  }
  // His arms come down; the head ducks at the hit.
  const float hit = plateau(u, 0.38f, 0.04f, 0.1f);
  setArm(w, 0, 1.f - easeInOut(window(u, 0.f, 0.5f)), 2.35f, 0.2f);
  w.fx.head = 0.4f * hit;
  w.fx.lean = 0.12f * hit;
  if (u >= 0.85f && b.state == BAG_FLYING) {
    b.state = BAG_DROPPED;
    b.pos = land;
    b.swing = 0.f;
  }
  if (u >= 1.f) fxRest(w);
}
// He staggers: back a step, forward, sways, steadies.
void actStagger(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) w.spine.u0 = h.feet.x;
  const float k = envelope(u, 0.1f, 0.3f);
  w.fx.lean = k * 0.2f * sinT(u * 2.5f);
  w.fx.head = k * 0.25f * sinT(u * 2.5f + 0.2f);
  h.feet.x = w.spine.u0 - (float)h.facing * 6.f * sinT(0.5f * window(u, 0.f, 0.7f));
  if (u >= 1.f) fxRest(w);
}
// Untying the sack from the rope: hands working at it.
void actUntie(World& w, float u) {
  const float k = envelope(u, 0.15f, 0.2f);
  w.fx.reach_to = 0.35f;
  w.fx.reach = k * (0.75f + 0.15f * sinT(u * 5.f));
  w.fx.lean = 0.1f * k;
  if (u >= 1.f) fxRest(w);
}
void doBagCarry(World& w, float) {
  w.bag.state = BAG_CARRIED;
  hikerCarry(w.hiker, CARRY_BAG);
}
void doBagIntoPack(World& w, float) {
  w.bag.state = BAG_NONE;
  hikerCarry(w.hiker, CARRY_NONE);
}

// ---- break_camp ------------------------------------------------------------------------

// A peg pulled with three tugs, leaning back on each; the tent twitches.
void tugs(World& w, float u, int n, float strength) {
  const float s = u * (float)n;
  const float ph = s - floorf(s);
  const float t = sinT(0.5f * window(ph, 0.15f, 0.7f));
  const float k = envelope(u, 0.08f, 0.1f);
  w.fx.reach_to = 0.3f;
  w.fx.reach = k * (0.8f - 0.2f * t);
  w.fx.lean = k * (0.12f - strength * t);
  w.tent.wobble = u >= 1.f ? 0.f : floorf(0.5f + 1.5f * t * k);
  if (u >= 1.f) fxRest(w);
}
void actTugs(World& w, float u) { tugs(w, u, 3, 0.2f); }
// Break_camp a: one will not come: a long heave, and he goes over backwards.
void actHeaveStuck(World& w, float u) {
  const float k = envelope(u, 0.15f, 0.05f);
  w.fx.reach_to = 0.45f;
  w.fx.reach = k;
  w.fx.lean = -0.3f * k * (0.8f + 0.2f * sinT(u * 6.f));
  if (u >= 1.f) w.fx.reach = w.fx.lean = 0.f;
}
void actFallBack(World& w, float u) {
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    w.spine.u0 = h.feet.x;
    setPose(w, POSE_LIE, 0.4f);
  }
  h.feet.x = w.spine.u0 - (float)h.facing * 16.f * easeOutCubic(window(u, 0.f, 0.4f));
  w.fx.lift = 0.02f * sinT(0.5f * window(u, 0.35f, 0.6f));   // a bounce
}
// With the rock as a lever: crouched, a slow heave, and it comes.
void actLever(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.2f);
  w.fx.reach_to = 0.35f;
  w.fx.reach = k * 0.85f;
  w.fx.lean = k * (0.18f - 0.3f * easeInOut(window(u, 0.4f, 0.75f)));
  w.tent.wobble = u >= 1.f ? 0.f : floorf(0.5f + 2.f * plateau(u, 0.6f, 0.05f, 0.1f));
  if (u >= 1.f) fxRest(w);
}
void doDropRock(World& w, float) { hikerCarry(w.hiker, CARRY_NONE); }

// The tent comes down: it slumps (TENT_COLLAPSED), flops and settles flat.
void actSlump(World& w, float u) {
  Tent& t = w.tent;
  t.state = TENT_COLLAPSED;
  t.door = 0.f;
  const float fall = easeInCubic(window(u, 0.f, 0.6f));
  t.sag = fall - 0.1f * sinT(0.5f * window(u, 0.6f, 1.f));
  t.wobble = u >= 1.f ? 0.f : floorf(0.5f + 3.f * sinT(0.5f * window(u, 0.f, 0.6f)));
  if (u >= 1.f) t.sag = 1.f;
}
// Gathered up: arm over arm, the heap narrows (progress back toward the spread).
void actGather(World& w, float u) {
  Tent& t = w.tent;
  if (u <= 0.f) w.spine.u0 = t.progress;
  t.progress = lerp(w.spine.u0, 0.06f, easeInOut(u));
  const float k = envelope(u, 0.1f, 0.15f);
  setArm(w, 0, k, 0.6f + 0.35f * sinT(u * 3.f), 0.3f);
  setArm(w, 1, k, 0.6f - 0.35f * sinT(u * 3.f), 0.3f);
  w.fx.lean = 0.2f * k;
  if (u >= 1.f) fxRest(w);
}
// Rolled into its sack: the tent is packed, the sack in his hands.
void doTentBundle(World& w, float) {
  w.tent.state = TENT_PACKED;
  w.tent.sag = 0.f;
  w.tent.wobble = 0.f;
  hikerCarry(w.hiker, CARRY_BAG);
}

// Break_camp b: a gust takes the fly off the tent and tumbles it across the
// clearing (a GL_SHAPE in the tent's colour: no major runs now, so the major's
// glyph slots are free). He chases it, stamps on it, brings it back.
Glyph* flyGlyph(World& w) {
  Glyph& g = w.major.g[0];
  return g.kind == GL_SHAPE ? &g : nullptr;
}
constexpr float kFlyS = 3.2f;
void actFlyOff(World& w, float u) {
  Glyph& g = w.major.g[0];
  const Vec2 a = ridgeTop(w), b = w.spine.fp[1];
  if (u <= 0.f) {
    g = Glyph{};
    g.kind = GL_SHAPE;
    g.shape = SH_MAP;
    g.slot = TENT;
    g.facing = 1;
    g.k = 1.f;
    g.scale = 1.7f;
  }
  const float s = u * kFlyS;
  const float k = easeOutCubic(u);
  g.pos = Vec2{lerp(a.x, b.x, k), lerp(a.y, b.y - 3.f, k) - 26.f * sinT(0.5f * k) - 8.f * fabsf(sinT(0.9f * s)) * (1.f - k)};
  g.rot = 0.12f * sinT(0.7f * s) * (1.f - k);
  g.t = 0.18f + 1.6f * s * (1.f - 0.6f * k);
  if (u >= 1.f) {
    g.t = 0.25f;   // lying flat: squashed to a sliver, then the stamp flattens it out
    g.rot = 0.f;
    g.pos = Vec2{b.x, b.y - 2.f};
  }
  g.base_y = (int16_t)(b.y + 1.f);
}
// He sees it go: a start.
void actStartle(World& w, float u) {
  const float k = envelope(u, 0.1f, 0.4f);
  w.fx.lean = -0.15f * k;
  w.fx.head = -0.25f * k;
  setArm(w, 0, 0.6f * k, 1.9f, 0.6f);
  if (u >= 1.f) fxRest(w);
}
// Two stamps on it.
void actStamp(World& w, float u) {
  const float s = u * 2.f;
  const float st = sinT(0.5f * window(s - floorf(s), 0.f, 0.6f));
  w.fx.lift = u >= 1.f ? 0.f : 0.03f * st;
  w.fx.lean = u >= 1.f ? 0.f : 0.1f * st;
  w.fx.head = u >= 1.f ? 0.f : 0.2f * envelope(u, 0.1f, 0.1f);
}
// Picked up and folded as he carries it back (World keeps it at his hands
// in morningTick while fstage says so).
void doFlyUp(World& w, float) {
  if (Glyph* g = flyGlyph(w)) {
    g->t = 0.f;
    g->k = 0.6f;
    g->scale = 1.2f;
  }
  w.spine.target = kFlyHeld;   // the fly rides in his hands
  hikerCarry(w.hiker, CARRY_BOOK);   // (keeps his arms still as he walks; the fly covers it)
}
void doFlyDown(World& w, float) {
  if (Glyph* g = flyGlyph(w)) g->kind = GL_NONE;
  w.spine.target = 0;
  hikerCarry(w.hiker, CARRY_NONE);
}
void flyTick(World& w) {
  if (w.spine.target != kFlyHeld) return;
  Glyph* g = flyGlyph(w);
  if (!g) return;
  g->pos = hikerHandsMid(w.hiker);
  g->pos.y -= 1.f;
  g->rot = 0.f;
  g->base_y = (int16_t)(w.hiker.feet.y + 1.f);
}

// Break_camp c: it will not go in its sack. He stuffs it, kneels on it, sits on
// it; the heap squashes down each time; then it goes.
void actStuff(World& w, float u) {
  const float s = u * 3.f;
  const float p = sinT(0.5f * window(s - floorf(s), 0.f, 0.7f));
  const float k = envelope(u, 0.08f, 0.12f);
  w.fx.reach_to = 0.55f;
  w.fx.reach = k * (0.5f + 0.5f * p);
  w.fx.lean = k * (0.15f + 0.12f * p);
  w.tent.wobble = u >= 1.f ? 0.f : floorf(0.5f + 2.f * p * k);
  if (u >= 1.f) fxRest(w);
}
void actKneel(World& w, float u) {
  Tent& t = w.tent;
  if (u <= 0.f) w.spine.u0 = t.progress;
  const float b = sinT(0.5f * window(u, 0.2f, 0.5f)) + sinT(0.5f * window(u, 0.55f, 0.85f));
  w.fx.lift = 0.02f * b;
  w.fx.lean = 0.1f * envelope(u, 0.1f, 0.1f);
  t.progress = lerp(w.spine.u0, 0.035f, easeInOut(u));
  if (u >= 1.f) fxRest(w);
}
void actSitSquash(World& w, float u) {
  Tent& t = w.tent;
  if (u <= 0.f) w.spine.u0 = t.progress;
  const float b = sinT(0.5f * window(u, 0.1f, 0.35f)) + 0.6f * sinT(0.5f * window(u, 0.4f, 0.6f));
  w.fx.lift = 0.025f * b;
  t.progress = lerp(w.spine.u0, 0.f, easeInOut(window(u, 0.f, 0.7f)));
  if (u >= 1.f) fxRest(w);
}

// ---- douse --------------------------------------------------------------------------------

// The pot off the fire, set down beside the pit.
void actPotOff(World& w, float u) {
  Item& p = w.items[ITEM_POT];
  const Vec2 top = potOnFire(w), dst = w.spine.camp.pot_down;
  w.fx.reach_to = 1.3f;
  w.fx.reach = envelope(u, 0.2f, 0.25f);
  const float k = easeInOut(window(u, 0.25f, 0.8f));
  p.pos = Vec2{lerp(top.x, dst.x, k), lerp(top.y, dst.y, k) - 10.f * sinT(0.5f * k)};
  if (u >= 1.f) {
    p.on_fire = false;
    p.pos = dst;
    fxRest(w);
  }
}
// The fire put out: no flame, the smoke dies away, the ash back.
void fireOut(World& w) {
  FirePit& f = w.fire;
  f.state = FIRE_UNLIT;
  f.fuel = 0.f;
  w.amb.smoke_k_to = 0.f;
}
// Douse a: three kicks of dirt into the pit, flecks flying; the fire sinks
// with each, and a last puff of smoke goes up.
void actKick(World& w, float u) {
  Hiker& h = w.hiker;
  FirePit& f = w.fire;
  const float s = u * 3.f;
  const int n = (int)s;
  const float ph = s - (float)n;
  const float kick = sinT(0.5f * window(ph, 0.1f, 0.55f));
  w.fx.lean = u >= 1.f ? 0.f : -0.12f * kick;
  w.fx.lift = u >= 1.f ? 0.f : 0.012f * kick;
  if (u <= 0.f) w.spine.target = 0;
  if (ph >= 0.3f && n < 3 && w.spine.target < n + 1) {   // the foot comes through: flecks fly
    w.spine.target = (uint8_t)(n + 1);
    for (int k = 0; k < 2; ++k) {
      const int b = worldTakeBit(w, BIT_DIRT);
      if (b < 0) break;
      Bit& d = w.bits[b];
      d.pos = Vec2{h.feet.x + (float)h.facing * 9.f, h.feet.y - 3.f};
      d.vel = Vec2{(float)h.facing * (48.f + 14.f * (float)k), -60.f + 12.f * (float)k};
      d.ground_y = f.pos.y - 1.f;
      d.land_y = f.pos.y + 2.f;
    }
    f.fuel = kLowFuel * (1.f - 0.34f * (float)(n + 1));
    f.ash = minf(1.f, f.ash + 0.2f);
  }
  if (u >= 1.f) {
    fireOut(w);
    f.ash = 1.f;
  }
}
// A last smoke puff off the pit (the steam prop, one puff).
constexpr float kPuffS = 2.f;
void actLastPuff(World& w, float u) {
  Steam& st = w.steam;
  st.at = Vec2{w.fire.pos.x, w.fire.pos.y - 8.f};
  st.n = 1;
  st.t = u >= 1.f ? -1.f : u * kPuffS;
}
// Stirs the ashes with the stick.
void actStirAsh(World& w, float u) {
  const float k = envelope(u, 0.15f, 0.2f);
  w.fx.reach_to = 1.1f;
  w.fx.reach = k * (0.6f + 0.3f * sinT(u * 3.f));
  w.fx.lean = 0.1f * k;
  if (u >= 1.f) fxRest(w);
}
// Douse b: the pot lifted off the fire and poured on it: a burst of steam,
// the fire out at once.
constexpr float kPourS = 2.6f;
void actPourOut(World& w, float u) {
  Item& p = w.items[ITEM_POT];
  FirePit& f = w.fire;
  const Vec2 top = potOnFire(w);
  const float s = u * kPourS;
  const float lift = easeInOut(window(s, 0.f, 0.5f));
  p.pos = Vec2{top.x + 4.f * lift, top.y - 9.f * lift + 2.f * sinT(s * 1.5f) * window(s, 0.6f, 1.6f)};
  p.on_fire = true;   // kept in front of the flames
  w.fx.reach_to = 1.25f;
  w.fx.reach = envelope(u, 0.12f, 0.15f);
  Steam& st = w.steam;
  if (s >= 0.6f) {
    if (f.state == FIRE_LIT) {
      fireOut(w);
      f.ash = 1.f;
    }
    st.at = Vec2{top.x, top.y - 10.f};
    st.n = w.plan.steam_n;
    st.t = s - 0.6f;
  }
  if (u >= 1.f) {
    st.t = -1.f;
    fxRest(w);
  }
}
// Waves the steam away from his face, leaning back.
void actWaveSteam(World& w, float u) {
  const float k = envelope(u, 0.15f, 0.2f);
  setArm(w, 1, k, 1.2f, 1.2f + 0.5f * sinT(u * 4.f));
  w.fx.lean = -0.12f * k;
  w.fx.head = -0.15f * k;
  if (u >= 1.f) fxRest(w);
}
// The pot set down beside the pit after pouring.
void actPotAside(World& w, float u) {
  Item& p = w.items[ITEM_POT];
  if (u <= 0.f) w.spine.v0 = p.pos;
  const Vec2 dst = w.spine.camp.pot_down;
  const float k = easeInOut(u);
  p.pos = Vec2{lerp(w.spine.v0.x, dst.x, k), lerp(w.spine.v0.y, dst.y, k) - 6.f * sinT(0.5f * k)};
  w.fx.reach_to = 1.2f;
  w.fx.reach = envelope(u, 0.15f, 0.3f);
  if (u >= 1.f) {
    p.on_fire = false;
    p.pos = dst;
    fxRest(w);
  }
}
void doPotUpFromGround(World& w, float) {
  Item& p = w.items[ITEM_POT];
  p.down = false;
  p.on_fire = false;
  p.held = true;
  hikerCarry(w.hiker, CARRY_MUG);
}

// ---- leave ---------------------------------------------------------------------------------

// Pack up off the ground to his hands, then on his back with a shrug.
Vec2 packAtHands(const World& w) {
  const Vec2 m = hikerHandsMid(w.hiker);
  return Vec2{m.x, minf(m.y + 13.f, w.hiker.feet.y + 3.f)};
}
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
void actPackOn(World& w, float u) {
  Hiker& h = w.hiker;
  if (u >= 0.35f && w.pack.shown) {
    w.pack.shown = false;
    w.pack.held = false;
    hikerCarry(h, CARRY_NONE);
    h.pack_on = true;
  }
  w.fx.lean = 0.2f * sinT(0.5f * u);   // shrugged on
  w.fx.lift = 0.01f * sinT(0.5f * window(u, 0.35f, 0.8f));
  if (u >= 1.f) fxRest(w);
}
void doMugPocket(World& w, float) {   // the mug off the ground, into a pocket of the pack
  w.items[ITEM_MUG].down = false;
}
bool mugDown(const World& w) { return w.items[ITEM_MUG].down; }
// The book the read filler left by the reading rock goes in the pack too
// (review finding: it stayed on the ground through the fade).
void doBookPocket(World& w, float) { w.items[ITEM_BOOK].down = false; }
bool bookDown(const World& w) { return w.items[ITEM_BOOK].down; }
// Leave c: one more stone on the cairn (the cairn filler's stones), a pat.
bool cairnRoom(const World& w) { return w.cairn.n < CAIRN_MAX; }
void actAddStone(World& w, float u) {
  Cairn& c = w.cairn;
  if (u <= 0.f) {
    hikerCarry(w.hiker, CARRY_NONE);
    if (c.n < CAIRN_MAX) ++c.n;
  }
  c.pop = u;
  w.memory.cairn_n = c.n;
  w.fx.reach_to = 0.7f;
  w.fx.reach = 0.6f * envelope(u, 0.2f, 0.4f);
  if (u >= 1.f) {
    c.pop = 1.f;
    fxRest(w);
  }
}
void actPat(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.2f);
  w.fx.reach_to = 0.95f;
  w.fx.reach = k * (0.85f + 0.15f * sinT(u * 3.f));
  if (u >= 1.f) fxRest(w);
}
// Two nods: that is everything.
void actNod(World& w, float u) { w.fx.head = 0.26f * (0.5f - 0.5f * cosT(2.f * u)); }
// The long look back at the clearing before he goes.
void actLookBack(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.2f);
  w.fx.head = -0.1f * k;
  if (u >= 1.f) fxRest(w);
}

// ---- scripts -----------------------------------------------------------------------------

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
#define FROM_REST pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
#define PACK_CROUCH go(AT_PACK_WORK, kStroll), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.6f)
// Wake, shared: asleep a while; he stirs; the door unzips; out on all fours, up.
#define WAKE_OUT                                                                                         \
  still(2.0f), act(actStir, 2.4f), still(1.2f), act(actUnzip, 2.2f), still(0.6f), doit(doCrawlOut), \
      act(actCrawl, 2.6f), pose(POSE_STAND, 0.9f, 0.4f)

// Wake a: up, the big stretch, a scratch of the head.
const Step kWakeA[] = {
    WAKE_OUT,
    pose(POSE_REACH_UP, 1.0f, 0.2f),
    act(actStretch, 2.4f),
    pose(POSE_STAND, 0.8f, 0.5f),
    pose(POSE_SCRATCH_HEAD, 0.7f, 2.0f),
    pose(POSE_STAND, 0.6f, 0.3f),
    pose(POSE_LOOK_AROUND, 0.8f, 1.6f),
    pose(POSE_STAND, 0.6f, 0.3f),
};

// Wake b: out, stands, steps on what he left by the door; hops holding his
// foot; picks it up (the shoe goes on; the mug goes in the pack).
const Step kWakeB[] = {
    WAKE_OUT,
    still(0.4f),
    walk(AT_FAM0, 22.f),
    act(actTread, 2.8f),
    still(0.6f),
    pose(POSE_SCRATCH_HEAD, 0.6f, 1.0f),
    pose(POSE_STAND, 0.5f, 0.1f),
    walk(AT_FAM1, 22.f),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    doit(doDoorItemUp),
    skipUnless(gotMug, 6),
    pose(POSE_STAND, 0.6f, 0.1f),
    PACK_CROUCH,
    doit(doIntoPack),
    pose(POSE_STAND, 0.6f, 0.2f),
    skipUnless(gotShoe, 2),
    act(actShoeOn, 1.6f),
    pose(POSE_STAND, 0.6f, 0.3f),
};

// Wake c: a bird lands on the tent's ridge and taps; he bursts out; it goes.
const Step kWakeC[] = {
    still(1.5f),
    doit(doBirdIn),
    until(birdLanded),
    still(1.0f),
    act(actStir, 2.4f),
    act(actBurstDoor, 0.35f),
    doit(doBurstOut),
    doit(doBirdOff),
    pose(POSE_STAND, 0.45f, 0.1f),
    act(actStartle, 0.9f),
    face(LEFT),
    act(actWatchBird, 2.6f),
    until(critGone),
    pose(POSE_SCRATCH_HEAD, 0.7f, 1.6f),
    pose(POSE_STAND, 0.6f, 0.3f),
    face(RIGHT),
    pose(POSE_REACH_UP, 0.9f, 0.2f),
    act(actStretch, 2.0f),
    pose(POSE_STAND, 0.8f, 0.3f),
};

// Coffee, shared start: the pot out of the pack onto the pit, the fire
// rekindled small; the mug out of the pack, filled; sits against the
// reading rock, looking out.
#define COFFEE_START                                                                                      \
  FROM_REST, PACK_CROUCH, act(actRummage, 1.4f), doit(doPotUp), pose(POSE_CARRY, 0.6f, 0.1f),             \
      go(AT_BLOW, 30.f), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.2f), act(actPotOnFire, 1.0f),              \
      pose(POSE_BLOW, 0.7f, 0.2f), act(actRekindle, 4.5f), pose(POSE_CROUCH, 0.6f, 0.8f),                 \
      pose(POSE_STAND, 0.6f, 0.1f), PACK_CROUCH, doit(doMugUp), pose(POSE_STAND, 0.6f, 0.1f),             \
      go(AT_BLOW, 30.f), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.1f), act(actPour, 1.6f),                   \
      pose(POSE_STAND, 0.6f, 0.1f), go(AT_READ, kStroll), face(RIGHT), pose(POSE_SIT, 0.9f, 0.4f)
// Shared end: the mug down beside him (by the rock), back to the fire.
#define COFFEE_END doit(doMugDown), still(0.6f), pose(POSE_STAND, 0.8f, 0.3f), TO_SIT

const Step kCoffeeA[] = {COFFEE_START, act(actSipFog, kSipFogS), still(2.0f), COFFEE_END};
const Step kCoffeeB[] = {COFFEE_START, act(actBurn, kBurnS), still(1.0f), COFFEE_END};
const Step kCoffeeC[] = {
    COFFEE_START,
    doit(doChipIn),
    act(actSipOne, kSipOneS),
    until(chipArrived),
    still(1.0f),
    act(actGuard, kGuardS),
    doit(doChipOff),
    still(1.0f),
    until(critGone),
    act(actGuardSip, kGuardSipS),
    COFFEE_END,
};

// Bag_down, shared start: under the bag branch, hands up to the rope.
// Shared end: untied, carried to the pack, in.
#define BAG_START FROM_REST, go(AT_BAG_WORK, 36.f), face(RIGHT), pose(POSE_REACH_UP, 0.7f, 0.2f)
#define BAG_END                                                                                         \
  pose(POSE_CROUCH, 0.6f, 0.2f), act(actUntie, 1.6f), doit(doBagCarry), pose(POSE_CARRY, 0.6f, 0.1f), \
      PACK_CROUCH, doit(doBagIntoPack), pose(POSE_STAND, 0.6f, 0.2f), TO_SIT

const Step kBagDownA[] = {BAG_START, act(actLower, 3.2f), still(0.4f), BAG_END};
const Step kBagDownB[] = {
    BAG_START,
    act(actLowerHalf, 1.8f),
    act(actYank, 2.4f),
    still(0.3f),
    act(actBagOnHead, 1.4f),
    pose(POSE_STAND, 0.3f, 0.f),
    act(actStagger, 1.8f),
    still(0.5f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.0f),
    pose(POSE_STAND, 0.5f, 0.1f),
    walk(AT_FAM0, 24.f),
    face(RIGHT),
    BAG_END,
};

// Break_camp, shared start: at the front peg, three tugs. Shared end: the
// tent slumps, he gathers it up, rolls it into its sack and packs it.
#define CAMP_START FROM_REST, go(AT_TENT_WORK, kStroll), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.3f), act(actTugs, 2.4f)
#define CAMP_DOWN                                                                                         \
  pose(POSE_STAND, 0.6f, 0.3f), act(actSlump, 1.6f), still(0.6f), pose(POSE_CROUCH, 0.6f, 0.2f),         \
      act(actGather, 3.0f)
#define CAMP_PACK                                                                                         \
  doit(doTentBundle), pose(POSE_CARRY, 0.6f, 0.2f), PACK_CROUCH, doit(doIntoPack), pose(POSE_STAND, 0.6f, 0.3f), \
      pose(POSE_HANDS_ON_HIPS, 0.6f, 1.0f), pose(POSE_STAND, 0.5f, 0.1f), TO_SIT

// Break_camp a: a peg will not come: he heaves, goes over backwards, gets up,
// levers it out with a rock.
const Step kCampA[] = {
    CAMP_START,
    act(actHeaveStuck, 2.0f),
    act(actFallBack, 1.4f),
    still(1.2f),
    pose(POSE_SIT, 0.8f, 0.4f),
    pose(POSE_STAND, 0.8f, 0.3f),
    pose(POSE_SCRATCH_HEAD, 0.6f, 1.0f),
    pose(POSE_STAND, 0.5f, 0.1f),
    walk(AT_PEG_ROCK, 30.f),
    pose(POSE_CROUCH, 0.5f, 0.3f),
    carry(CARRY_ROCK),
    pose(POSE_STAND, 0.5f, 0.1f),
    walk(AT_TENT_WORK, 30.f),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.1f),
    act(actLever, 2.2f),
    doit(doDropRock),
    CAMP_DOWN,
    CAMP_PACK,
};
// Break_camp b: a gust takes the fly; he chases it, stamps on it, brings it back.
const Step kCampB[] = {
    CAMP_START,
    pose(POSE_STAND, 0.6f, 0.2f),
    act(actFlyOff, kFlyS),
    act(actStartle, 0.8f),
    face(RIGHT),
    go(AT_FAM0, kRun),
    act(actStamp, 1.2f),
    still(0.4f),
    pose(POSE_CROUCH, 0.5f, 0.2f),
    doit(doFlyUp),
    pose(POSE_STAND, 0.5f, 0.1f),
    go(AT_TENT_WORK, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    doit(doFlyDown),
    CAMP_DOWN,
    CAMP_PACK,
};
// Break_camp c: the tent will not go in its sack: he stuffs it, kneels on it,
// sits on it, and in it goes.
const Step kCampC[] = {
    CAMP_START,
    CAMP_DOWN,
    act(actStuff, 2.4f),
    pose(POSE_STAND, 0.5f, 0.2f),
    walk(AT_FAM0, 20.f),
    pose(POSE_CROUCH, 0.6f, 0.1f),
    act(actKneel, 2.0f),
    pose(POSE_SIT, 0.7f, 0.1f),
    act(actSitSquash, 2.2f),
    still(0.6f),
    pose(POSE_STAND, 0.8f, 0.2f),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    CAMP_PACK,
};

// Douse, shared start: to the pit. Shared end: the pot to the pack.
#define DOUSE_START FROM_REST, go(AT_BLOW, kStroll), face(LEFT)
#define DOUSE_END                                                                              \
  pose(POSE_CROUCH, 0.6f, 0.2f), doit(doPotUpFromGround), pose(POSE_CARRY, 0.6f, 0.1f), PACK_CROUCH, \
      doit(doIntoPack), pose(POSE_STAND, 0.6f, 0.2f), TO_SIT

// Douse a: the pot off, dirt kicked on the embers, a last puff, a stir.
const Step kDouseA[] = {
    DOUSE_START,
    pose(POSE_CROUCH, 0.6f, 0.1f),
    act(actPotOff, 1.2f),
    pose(POSE_STAND, 0.6f, 0.2f),
    act(actKick, 3.0f),
    act(actLastPuff, kPuffS),
    pose(POSE_CROUCH, 0.6f, 0.1f),
    carry(CARRY_STICK),
    act(actStirAsh, 2.0f),
    carry(CARRY_NONE),
    DOUSE_END,
};
// Douse b: the pot poured on it: a steam burst; he waves it away.
const Step kDouseB[] = {
    DOUSE_START,
    pose(POSE_CROUCH, 0.6f, 0.2f),
    act(actPourOut, kPourS),
    act(actWaveSteam, 1.8f),
    act(actPotAside, 0.8f),
    still(0.6f),
    DOUSE_END,
};

// Leave, shared start: the pack on at the pack, a long look back.
#define LEAVE_PACK                                                                                \
  FROM_REST, PACK_CROUCH, act(actPackLift, 0.6f), pose(POSE_STAND, 0.6f, 0.1f), act(actPackOn, 1.2f), \
      pose(POSE_STAND, 0.5f, 0.1f), face(RIGHT), act(actLookBack, 2.6f)
#define MUG_UP skipUnless(mugDown, 4), go(AT_MUG, kStroll), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.3f), doit(doMugPocket)
// The book, if the read filler left it out (AT_FAM0: its right side, beatPoints).
#define BOOK_UP skipUnless(bookDown, 4), go(AT_FAM0, kStroll), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.3f), doit(doBookPocket)
#define OFF_RIGHT pose(POSE_STAND, 0.6f, 0.1f), go(AT_FORD1, kStroll), walk(AT_FORD0, kWade), walk(AT_EDGE_R, kStroll)

// Leave a: off right over the ford.
const Step kLeaveA[] = {LEAVE_PACK, MUG_UP, BOOK_UP, OFF_RIGHT};
// Leave b: off left; at the edge of the clearing (just short of the
// foreground pines, which would hide him) he stops, comes back for the mug, goes.
const Step kLeaveB[] = {
    LEAVE_PACK,
    face(LEFT),
    go(AT_EDGE_L, kStroll, 196.f, -4.f),
    still(0.6f),
    pose(POSE_SCRATCH_HEAD, 0.6f, 0.9f),
    pose(POSE_STAND, 0.5f, 0.1f),
    face(RIGHT),
    MUG_UP,
    BOOK_UP,
    pose(POSE_STAND, 0.6f, 0.1f),
    face(LEFT),
    act(actNod, 1.2f),
    go(AT_EDGE_L, 40.f),
};
// Leave c: a stone on the cairn, a pat, and off right over the ford. With the
// cairn already full (memory fills it over a few loops) the whole detour,
// pat included, is skipped.
const Step kLeaveC[] = {
    LEAVE_PACK,
    MUG_UP,
    BOOK_UP,
    pose(POSE_STAND, 0.6f, 0.1f),
    skipUnless(cairnRoom, 9),
    go(AT_STONE, 30.f),
    pose(POSE_CROUCH, 0.6f, 0.5f),
    carry(CARRY_ROCK),
    pose(POSE_STAND, 0.5f, 0.1f),
    go(AT_CAIRN_WORK, 28.f),
    faceTo(AT_CAIRN),
    pose(POSE_CROUCH, 0.6f, 0.15f),
    act(actAddStone, 0.9f),
    act(actPat, 1.4f),
    OFF_RIGHT,
};

#undef FROM_REST
#undef TO_SIT
#undef PACK_CROUCH
#undef WAKE_OUT
#undef COFFEE_START
#undef COFFEE_END
#undef BAG_START
#undef BAG_END
#undef CAMP_START
#undef CAMP_DOWN
#undef CAMP_PACK
#undef DOUSE_START
#undef DOUSE_END
#undef LEAVE_PACK
#undef MUG_UP
#undef BOOK_UP
#undef OFF_RIGHT

struct Script {
  const Step* steps;
  uint8_t n;
};
#define SCRIPT(a) Script{a, N(a)}
#define NO_SCRIPT Script{nullptr, 0}

const Script kScripts[MB_COUNT][3] = {
    {SCRIPT(kWakeA), SCRIPT(kWakeB), SCRIPT(kWakeC)},
    {SCRIPT(kCoffeeA), SCRIPT(kCoffeeB), SCRIPT(kCoffeeC)},
    {SCRIPT(kBagDownA), SCRIPT(kBagDownB), NO_SCRIPT},
    {SCRIPT(kCampA), SCRIPT(kCampB), SCRIPT(kCampC)},
    {SCRIPT(kDouseA), SCRIPT(kDouseB), NO_SCRIPT},
    {SCRIPT(kLeaveA), SCRIPT(kLeaveB), SCRIPT(kLeaveC)},
};
#undef SCRIPT
#undef NO_SCRIPT
#undef N

// Beat seconds per variant (whole seconds: the script plus room for the
// longer walks on some seeds). Tuned with SS_SPINE_TRACE.
const uint8_t kDurations[MB_COUNT][3] = {
    {28, 30, 27},   // wake (c: the bird's visit)
    {69, 68, 74},   // coffee (the pot, the fire, the mug, then the sitting)
    {33, 41, 0},    // bag_down (the walks: bag pine, pack)
    {46, 43, 41},   // break_camp
    {26, 23, 0},    // douse
    {46, 46, 55},   // leave (the walk out, then the last 2.5 s fade to black; +7 for the book pickup)
};

const char* const kBeatNames[MB_COUNT] = {"wake", "coffee", "bag_down", "break_camp", "douse", "leave"};

#ifdef SS_MORNING_TUNE   // tuning aid: every beat long enough to measure its script
int beatSeconds(const LoopPlan& p, int b) { return 120 + 0 * kDurations[b][p.fvar[b]]; }
#else
int beatSeconds(const LoopPlan& p, int b) { return kDurations[b][p.fvar[b]]; }
#endif

// ---- beats --------------------------------------------------------------------------------

// A beat's own points (AT_FAM0, AT_FAM1), set as it starts.
void beatPoints(World& w, int b) {
  SpineState& sp = w.spine;
  const CampLayout& c = sp.camp;
  const int v = w.plan.fvar[b];
  sp.target = 0;
  if (b == MB_WAKE && v == 1) {   // treads on what is by the door, crouches over it
    const Vec2 at{c.door.x + 30.f, c.door.y + 3.f};
    sp.fp[0] = Vec2{at.x - 4.f, at.y - 3.f};
    sp.fp[1] = Vec2{at.x - 12.f, at.y - 3.f};
  } else if (b == MB_BAG_DOWN && v == 1) {   // picks the dropped bag up, left of it
    sp.fp[0] = Vec2{c.bag_work.x + 6.f, c.bag_work.y};
  } else if (b == MB_BREAK_CAMP && v == 1) {   // where the fly lands, and he runs to
    const float x = c.tent.x + 168.f;
    sp.fp[1] = Vec2{x, scriptGroundAt(w, x, c.tent.y + 6.f)};
    sp.fp[0] = Vec2{x - 13.f, sp.fp[1].y};
  } else if (b == MB_BREAK_CAMP && v == 2) {   // on the heap
    sp.fp[0] = Vec2{c.tent.x + 4.f, c.tent.y + 2.f};
  } else if (b == MB_LEAVE) {   // the book's right side, if it lies out
    const Item& bk = w.items[ITEM_BOOK];
    sp.fp[0] = Vec2{bk.pos.x + 9.f, bk.pos.y};
  }
}

template <int B>
void beatBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  beatPoints(w, B);
  const Script& s = kScripts[B][w.plan.fvar[B]];
  scriptStart(w, s.steps, s.n);
}
void beatUpdate(SceneCtx& ctx, float, float) { scriptRun(*ctx.w); }
// A beat's end: a script still running counts an overrun, and so does a
// critter of its still in the scene (sent off by the nearer edge).
void beatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
#ifdef SS_SPINE_TRACE   // tuning aid: where each script finished in its beat
  printf("trace morning_end %s step=%u/%u feet=%.1f,%.1f ticks=%u done_at=%u frac=%.3f\n", appBeatName(ctx.app),
         (unsigned)w.spine.run.i,
         (unsigned)w.spine.run.n, (double)w.hiker.feet.x, (double)w.hiker.feet.y, (unsigned)w.spine.run.beat_ticks,
         (unsigned)w.spine.run.done_at,
         w.spine.run.beat_ticks ? (double)w.spine.run.done_at / (double)w.spine.run.beat_ticks : 0.0);
#endif
  bool late = !scriptDone(w);
  SpineState& sp = w.spine;
  if (sp.fcrit >= 0 && !critGone(w)) {
    late = true;
    const Critter& c = w.critters[sp.fcrit];
    directorExit(w, sp.fcrit, c.feet.x < 0.5f * (float)SS_WIDTH ? -1 : 1, 90.f, CA_RUN);
  }
  if (late && sp.overruns < 255) ++sp.overruns;
  sp.run.steps = nullptr;
  w.busy = false;
  w.tent.wobble = 0.f;
  // An overrun must not leave the fly in the air or a critter stage driving
  // the tent after the beat.
  if (Glyph* g = flyGlyph(w)) g->kind = GL_NONE;
  sp.fstage = FC_NONE;
  sp.target = 0;
}
// Leave: the script, and the frame fading to black over the last kFadeS
// seconds (1 on the beat's last tick); the script must be done (him out of
// the frame) before the fade starts.
void leaveUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  scriptRun(w);
  const float d = (float)beatSeconds(w.plan, MB_LEAVE);
  const float fade = easeInOut(window(t, 1.f - kFadeS / d, 1.f));
  const bool busy = !scriptDone(w) || directorCount(w) > 0;
  if (w.fade == 0.f && fade > 0.f && busy && w.spine.overruns < 255) ++w.spine.overruns;
  w.fade = fade;
}

typedef void (*BeginFn)(SceneCtx&);
const BeginFn kBegin[MB_COUNT] = {beatBegin<0>, beatBegin<1>, beatBegin<2>, beatBegin<3>, beatBegin<4>, beatBegin<5>};
static_assert(MB_COUNT == 6, "extend kBegin");

// The build beat with the camp popping in during its plants phase: the tent
// pitches itself with an overshoot, the pack and the wood pile pop up, the bag
// drops down its line, the cairn stacks up to last night's height.
void campPop(World& w) {
  const BuildState& b = w.build;
  const CampLayout& c = w.spine.camp;
  {
    Tent& t = w.tent;
    const float e = buildPlantScale(b, t.pos.x, t.pos.y);
    t.state = e <= 0.f ? TENT_PACKED : (e >= 1.f && buildDone(b) ? TENT_UP : TENT_RAISING);
    t.progress = minf(1.f, maxf(0.f, e));
    t.bump = e > 1.f ? 0.9f * (e - 1.f) : 0.f;
  }
  w.pack.pop = maxf(0.f, buildPlantScale(b, c.pack.x, c.pack.y));
  {
    const float e = buildPlantScale(b, w.bag.tip.x, (float)w.land.camp.base_y);
    w.bag.state = e > 0.f ? BAG_HUNG : BAG_NONE;
    w.bag.line = maxf(1.f, 16.f * e);
  }
  w.items[ITEM_WOOD].size = maxf(0.f, buildPlantScale(b, c.wood.x, c.wood.y));
  static const ItemId kDoor[2] = {ITEM_SHOE, ITEM_MUG};
  for (ItemId id : kDoor) {
    Item& it = w.items[id];
    if (it.down) it.size = maxf(0.f, buildPlantScale(b, it.pos.x, it.pos.y));
  }
  {
    const float e = clamp01(buildPlantScale(b, w.cairn.pos.x, w.cairn.pos.y));
    w.cairn.n = (uint8_t)floorf((float)w.plan.cairn_start * e + 0.5f);
    w.cairn.pop = 1.f;
  }
}
void campFinal(World& w) {
  Tent& t = w.tent;
  t.state = TENT_UP;
  t.progress = 1.f;
  t.bump = 0.f;
  w.pack.pop = 1.f;
  w.bag.state = BAG_HUNG;
  w.bag.line = 16.f;
  w.items[ITEM_WOOD].size = 1.f;
  static const ItemId kDoor[2] = {ITEM_SHOE, ITEM_MUG};
  for (ItemId id : kDoor)
    if (w.items[id].down) w.items[id].size = 1.f;
  w.cairn.n = w.plan.cairn_start;
  w.cairn.pop = 1.f;
  w.memory.cairn_n = w.cairn.n;
}
void buildUpdate(SceneCtx& ctx, float t, float dt) {
  kBuildBeat.update(ctx, t, dt);
  campPop(*ctx.w);
}
void buildEnd(SceneCtx& ctx) {
  kBuildBeat.end(ctx);
  campFinal(*ctx.w);
}
const Beat kMorningBuild = {"build", SS_BUILD_SECONDS, nullptr, buildUpdate, buildEnd};

// ---- plan -----------------------------------------------------------------------------------

constexpr int kAimMin = SS_LOOP_MIN_SECONDS + 10;
constexpr uint8_t kPauseMin = 8, kPauseMax = 20;

int busySeconds(const LoopPlan& p) {
  int s = (int)(SS_BUILD_SECONDS + 0.5f);
  for (int b = 0; b < MB_COUNT; ++b) s += beatSeconds(p, b);
  for (int i = 0; i < p.n_fillers; ++i) s += spineFillerSeconds(p, p.filler[i]);
  for (int i = 0; i < p.n_majors; ++i)
    s += majorSeconds(p.major[i], p.major_var[i]) + p.major_pre_s[i] + p.major_post_s[i];
  return s;
}

// Fillers after coffee and after break_camp (a second, third, fourth: coffee,
// break_camp, coffee again).
void fillerSlots(LoopPlan& p) {
  static const uint8_t kSlots[SPINE_FILLER_MAX + 1][SPINE_FILLER_MAX] = {
      {0, 0, 0, 0},
      {MB_COFFEE, 0, 0, 0},
      {MB_COFFEE, MB_BREAK_CAMP, 0, 0},
      {MB_COFFEE, MB_COFFEE, MB_BREAK_CAMP, 0},
      {MB_COFFEE, MB_COFFEE, MB_BREAK_CAMP, MB_BREAK_CAMP}};
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) p.filler_after[i] = i < p.n_fillers ? kSlots[p.n_fillers][i] : 0;
}

const FitSpec kMorningFit = {busySeconds, MB_COUNT - 1, fillerSlots};

// Ticks from the loop start to the start of the timeline entry after beat
// `b`'s own pause (before its fillers and majors).
uint32_t ticksThroughPause(const LoopPlan& p, int last) {
  const uint32_t build = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  int s = 0;
  for (int b = 0; b <= last; ++b) {
    s += beatSeconds(p, b) + p.pause_s[b];
    if (b == last) break;
    for (int i = 0; i < p.n_fillers; ++i)
      if (p.filler_after[i] == b) s += spineFillerSeconds(p, p.filler[i]) + p.filler_pause_s[i];
    for (int i = 0; i < p.n_majors; ++i)
      if (p.major_after[i] == b) s += p.major_pre_s[i] + majorSeconds(p.major[i], p.major_var[i]) + p.major_post_s[i];
  }
  return build + (uint32_t)s * SS_TICK_HZ;
}

}  // namespace

// ---- public ------------------------------------------------------------------------------------

const char* morningBeatName(int beat) { return beat >= 0 && beat < MB_COUNT ? kBeatNames[beat] : "?"; }

int morningVariantCount(int beat) {
  if (beat < 0 || beat >= MB_COUNT) return 0;
  int n = 0;
  while (n < 3 && kScripts[beat][n].steps) ++n;
  return n;
}

void morningPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem) {
  // One rng call per statement: argument evaluation order differs between
  // compilers, and the board must replay the PC preview.
  // Variants: a variant the last morning played is a quarter as likely.
  const bool again = mem.last_family == SF_MORNING && mem.loop_count > 1;
  p.n_fbeats = MB_COUNT;
  for (int b = 0; b < 8; ++b) p.fvar[b] = 0;
  for (int b = 0; b < MB_COUNT; ++b) {
    const int n = morningVariantCount(b);
    float wt[3] = {1.f, 1.f, 1.f};
    for (int k = n; k < 3; ++k) wt[k] = 0.f;
    if (again && mem.last_fvar[b] < n) wt[mem.last_fvar[b]] = 0.25f;
    const int v = rng.weighted(wt, 3);
    p.fvar[b] = (uint8_t)v;
  }
  for (uint8_t& s : p.pause_s) s = 0;
  for (int b = 0; b < MB_COUNT; ++b) {
    const uint8_t s = (uint8_t)rng.rangeInt(kPauseMin, kPauseMax);
    p.pause_s[b] = b == MB_LEAVE ? 0 : s;   // the loop ends with the leave
  }

  // Fillers: 2 to 4 of the morning's, weighted, no repeats (the length policy may drop some).
  float wt[FILL_COUNT];
  for (int f = 0; f < FILL_COUNT; ++f) wt[f] = spineFillerAllowed(SF_MORNING, f) ? (f == FILL_CAIRN ? 1.2f : 1.f) : 0.f;
  p.n_fillers = (uint8_t)rng.rangeInt(2, SPINE_FILLER_MAX);
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) p.filler[i] = p.filler_after[i] = p.filler_pause_s[i] = 0;
  for (int i = 0; i < p.n_fillers && i < SPINE_FILLER_MAX; ++i) {
    const int f = rng.weighted(wt, FILL_COUNT);
    p.filler[i] = (uint8_t)f;
    wt[f] = 0.f;
  }
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) {
    const uint8_t s = (uint8_t)rng.rangeInt(kPauseMin, kPauseMax);
    if (i < p.n_fillers) p.filler_pause_s[i] = s;
  }
  // Memory: the cairn as the evening has it, one stone shorter than it was left.
  p.cairn_start = mem.cairn_n > 0 ? (uint8_t)(mem.cairn_n - 1) : 0;
  const int add = rng.rangeInt(4, 6);
  p.cairn_add = (uint8_t)(add < CAIRN_MAX - p.cairn_start ? add : CAIRN_MAX - p.cairn_start);
  p.patch = false;
  p.skip_n = 3;
  p.steam_n = (uint8_t)rng.rangeInt(3, 5);
  p.fog_steps = (uint8_t)rng.rangeInt(11, 16);   // 0.55 .. 0.8

  static const MajorSlots kMorningSlots = {SF_MORNING, {MB_WAKE, MB_COFFEE, MB_BAG_DOWN, 0}, 1 | 2 | 4};
  majorsPlan(p, rng, in, mem, kMorningSlots);
  spineFitLength(p, mem, kMorningFit);
  // Still short (few, short majors and fillers): a filler back in, if one is free.
  while (busySeconds(p) + spinePauseSeconds(p, MB_COUNT - 1) < kAimMin && p.n_fillers < SPINE_FILLER_MAX) {
    int f = -1;
    for (int k = 0; k < FILL_COUNT && f < 0; ++k) {
      if (!spineFillerAllowed(SF_MORNING, k)) continue;
      bool used = false;
      for (int i = 0; i < p.n_fillers; ++i) used = used || p.filler[i] == k;
      if (!used) f = k;
    }
    if (f < 0) break;
    p.filler[p.n_fillers] = (uint8_t)f;
    p.filler_pause_s[p.n_fillers] = kPauseMin;
    ++p.n_fillers;
    fillerSlots(p);
    if (busySeconds(p) + spinePauseSeconds(p, MB_COUNT - 1) > SS_LOOP_MAX_SECONDS - 10) {   // too much: back out
      --p.n_fillers;
      p.filler[p.n_fillers] = p.filler_pause_s[p.n_fillers] = 0;
      fillerSlots(p);
      break;
    }
  }
  p.fog_t0 = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  p.fog_t1 = ticksThroughPause(p, MB_COFFEE);
}

void morningLayout(World& w) {
  CampLayout& c = w.spine.camp;
  const LoopPlan& p = w.plan;
  // The morning's places.
  c.guy_peg = Vec2{c.tent.x + 62.f, c.tent.y + 6.f};
  // The camp as he left it: everything popped in by the build (campPop).
  tentInit(w.tent, c.tent);
  w.tent.bite = w.memory.mouse_hole;
  w.tent.state = TENT_PACKED;
  w.fire.ash = 1.f;
  w.pack = PackProp{c.pack, 1, true, false};
  w.pack.pop = 0.f;
  w.bag.state = BAG_NONE;
  w.bag.line = 16.f;
  Item& wood = w.items[ITEM_WOOD];
  wood.down = true;
  wood.size = 0.f;
  wood.pos = c.wood;
  w.cairn.n = 0;
  w.hiker.pack_on = false;
  w.fx.on = false;
  // Wake b: what he left by the door last night (the shoe, unless the raccoon
  // has it; else the mug), right where he steps out; and where he crouches to
  // pick it up.
  const Vec2 at{c.door.x + 30.f, c.door.y + 3.f};
  if (p.fvar[MB_WAKE] == 1) {
    const ItemId id = w.memory.shoe_taken ? ITEM_MUG : ITEM_SHOE;
    Item& it = w.items[id];
    it.down = true;
    it.held = false;
    it.pos = at;
    it.facing = 1;
    it.size = 0.f;
  }
  w.fog_extra = 0.05f * (float)p.fog_steps;
  w.memory.cairn_n = p.cairn_start;
}

void morningBuildTimeline(Timeline& tl, const LoopPlan& p) {
  tl.add(kMorningBuild);
  for (int b = 0; b < MB_COUNT; ++b) {
    tl.add(Beat{kBeatNames[b], (float)beatSeconds(p, b), kBegin[b], b == MB_LEAVE ? leaveUpdate : beatUpdate, beatEnd});
    if (b == MB_LEAVE) break;   // the loop ends here
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

uint32_t morningLoopTicks(const LoopPlan& p) {
  const uint32_t build = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  const int rest = busySeconds(p) - (int)(SS_BUILD_SECONDS + 0.5f) + spinePauseSeconds(p, MB_COUNT - 1);
  return build + (uint32_t)rest * SS_TICK_HZ;
}

int morningFormatPlan(const LoopPlan& p, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int k = snprintf(buf, n, "family=morning");
  auto put = [&](const char* str) {
    if (k < 0 || (size_t)k >= n) return;
    const int m = snprintf(buf + k, n - (size_t)k, "%s", str);
    if (m > 0) k += m;
  };
  char tmp[24];
  for (int b = 0; b < MB_COUNT; ++b) {
    snprintf(tmp, sizeof tmp, " %s=%d", kBeatNames[b], (int)p.fvar[b]);
    put(tmp);
  }
  put(" fillers=");
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
  snprintf(tmp, sizeof tmp, " fog=%d", (int)p.fog_steps);
  put(tmp);
  return k;
}

void morningTick(World& w) {
  const LoopPlan& p = w.plan;
  // The fog lifts from the end of the build to the end of the coffee's pause,
  // eased, in 0.05 steps (each a palette change: a cache rebuild).
  float k = 1.f;
  if (w.tick >= p.fog_t1) k = 0.f;
  else if (w.tick > p.fog_t0) k = 1.f - easeInOut((float)(w.tick - p.fog_t0) / (float)(p.fog_t1 - p.fog_t0));
  const float steps = ceilf((float)p.fog_steps * k - 1e-3f);
  w.fog_extra = 0.05f * maxf(0.f, steps);
  critTick(w);
  flyTick(w);
}

}  // namespace ss
