// The midday spine (SF_MIDDAY): he comes back from a day hike, has lunch,
// wades the stream, naps in the shade, has a go at the map and lays the wood
// for the evening. SPINES_WEATHER_LOOKOUT_SPEC.md section 3; the framework is
// section 1 (spine_family.h); the morning (spine_morning.cpp) is the template.
//
// Same idioms as spine.cpp: actions take u 0..1 and leave the world at rest at
// u = 1; scripts are const Step tables (script.h); a beat's seconds are fixed
// per variant and longer than its script (an overrun counts in
// SpineState::overruns, tests require zero). One rng call per statement,
// table sines, no doubles, no allocation.
//
// Loop start (middayLayout): the camp as the morning would have left it had he
// stayed, pitched, the fire out under its ash, the bag hung, the pack by the
// tent, the wood pile (small when the afternoon will fetch more), the cairn;
// he is away on a day hike. The camp pops in with the plants in the build
// (buildUpdate), as the morning's does.
//
// Timeline: build, return, lunch, wade, nap, map, afternoon, each but the last
// followed by a pause; fillers (all but mug_sunset) after lunch and nap;
// majors after return, lunch and wade (he is outside for all of them). The
// afternoon's last 2.5 s fade to black as the evening's close does.
//
// The beat's own things (SpineState): one critter slot (fcrit: the lunch and
// nap chipmunks), one bit (fbit: the bee, the fly, the leaf), the major's
// glyph slot 0 (no major runs in a spine beat: the fish at his feet, the map),
// and what is in his hand that World does not place (fhold: the hat, the
// boot, the map). middayTick drives them all every tick, after the hiker's
// step, so they sit on his hands as they are this tick.
#include <math.h>
#include <stdio.h>
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
// Crossed `at` this tick (u0 last tick's u, kept by the caller).
inline bool crossed(float u, float u0, float at) { return u >= at && u0 < at; }

constexpr float kFadeS = 2.5f;
constexpr float kChipRun = 70.f;

// What his near hand holds that World does not place (SpineState::fhold).
enum Hold : uint8_t { HOLD_NONE = 0, HOLD_HAT, HOLD_SHOE, HOLD_HAT_FACE, HOLD_MAP, HOLD_POT };

// The beat's critter, bit or glyph (SpineState::fstage).
enum Stage : uint8_t {
  FS_NONE = 0,
  FS_BEE_IN, FS_BEE_HOVER, FS_BEE_SHOO, FS_BEE_WAIT, FS_BEE_BACK, FS_BEE_HOVER2, FS_BEE_OFF,   // lunch a
  FS_CHIP_IN, FS_CHIP_EDGE, FS_CHIP_GRAB, FS_CHIP_OFF,                                    // lunch b
  FS_FISH_CIRCLE, FS_FISH_DART,                                                           // wade a
  FS_RAID_IN, FS_RAID_DIG, FS_RAID_EAT, FS_RAID_OFF,                                      // nap a
  FS_LEAF_FALL, FS_LEAF_REST, FS_LEAF_SLIDE,                                              // nap b
  FS_FLY_IN, FS_FLY_CIRCLE, FS_FLY_DODGE, FS_FLY_CIRCLE2, FS_FLY_OFF,                     // nap c
  FS_MAP_HELD, FS_MAP_GUST, FS_MAP_SAT                                                    // map
};

void stageTo(World& w, uint8_t s) {
  w.spine.fstage = s;
  w.spine.ft = 0.f;
}

Vec2 bez(Vec2 a, Vec2 c, Vec2 b, float u) {
  const float v = 1.f - u;
  return Vec2{v * v * a.x + 2.f * v * u * c.x + u * u * b.x, v * v * a.y + 2.f * v * u * c.y + u * u * b.y};
}

// ---- small things: flecks, rings, drops ---------------------------------------------------

// A fleck (a burr, a stone out of the boot) off his hand, falling to the ground.
void fleck(World& w, Vec2 at, Vec2 vel) {
  const int b = worldTakeBit(w, BIT_DIRT);
  if (b < 0) return;
  Bit& d = w.bits[b];
  d.pos = at;
  d.vel = vel;
  d.ground_y = w.hiker.feet.y + 2.f;
  d.land_y = w.hiker.feet.y + 2.f;
}
// A ring spreading on the water at `at`, `size` (1 a frog's).
void ring(World& w, Vec2 at, float size) {
  const int b = worldTakeBit(w, BIT_RING);
  if (b < 0) return;
  Bit& r = w.bits[b];
  r.pos = at;
  r.spin = size;
  r.land_y = at.y;
}
// A drop thrown up from the water at `at`; a small ring where it falls back in.
void drop(World& w, Vec2 at, Vec2 vel, float water_y) {
  const int b = worldTakeBit(w, BIT_DROP);
  if (b < 0) return;
  Bit& d = w.bits[b];
  d.pos = at;
  d.vel = vel;
  d.ground_y = water_y;
  d.land_y = water_y;
}
// The water's surface at his feet (his waterline while he is in it).
float waterLine(const World& w) { return w.fx.wet_y > 0.f ? w.fx.wet_y : w.hiker.feet.y - 4.f; }

// ---- the beat's critter (a chipmunk) -----------------------------------------------------
// One slot (SpineState::fcrit), tagged CT_DINNER as the evening's beat-owned
// critters are, CF_BRAVE (it goes on with what it came for whatever he does).

bool critGone(const World& w) {
  const int s = w.spine.fcrit;
  return s < 0 || !w.crit[s].in_use || w.crit[s].tag != CT_DINNER;
}
int spawnChip(World& w, Vec2 from, int facing) {
  const int s = directorSpawn(w, CR_CHIPMUNK, from, facing);
  if (s < 0) return -1;
  w.crit[s].tag = CT_DINNER;
  w.crit[s].flags = CF_BRAVE;
  memoryApplyPersona(w.memory, w.critters[s]);
  w.spine.fcrit = (int8_t)s;
  w.busy = true;
  return s;
}

// ---- the beat's bit (a bee, a fly, a leaf) ------------------------------------------------

Bit* bitOf(World& w) {
  const int b = w.spine.fbit;
  return b >= 0 && b < BITS_MAX && w.bits[b].kind != BIT_NONE ? &w.bits[b] : nullptr;
}
bool bitGone(const World& w) {
  const int b = w.spine.fbit;
  return b < 0 || b >= BITS_MAX || w.bits[b].kind == BIT_NONE;
}
void freeBit(World& w) {
  if (Bit* b = bitOf(w)) *b = Bit{};
  w.spine.fbit = -1;
}
// The buzz of a bee or a fly: a few px of jitter.
Vec2 buzz(float t, float k) {
  return Vec2{k * (1.4f * sinT(t * 3.7f) + 0.9f * sinT(t * 5.3f + 0.2f)), k * (1.2f * sinT(t * 4.1f + 0.4f) + 0.7f * sinT(t * 6.7f))};
}

// ---- the beat's glyph (the fish, the map) --------------------------------------------------

Glyph* glyphOf(World& w, uint8_t shape) {
  Glyph& g = w.major.g[0];
  return g.kind == GL_SHAPE && g.shape == shape ? &g : nullptr;
}
Glyph& newGlyph(World& w, uint8_t shape, Vec2 at) {
  Glyph& g = w.major.g[0];
  g = Glyph{};
  g.kind = GL_SHAPE;
  g.shape = shape;
  g.slot = ACTOR;
  g.facing = 1;
  g.scale = 1.f;
  g.pos = at;
  g.q = at;
  g.base_y = (int16_t)(w.hiker.feet.y + 2.f);
  return g;
}

// ---- return ----------------------------------------------------------------------------------

// In from the right, back from the day's walk with a stick: no pack (it is by
// the tent), the stick in his hand.
void doEnter(World& w, float) {
  Hiker& h = w.hiker;
  hikerInit(h, w.spine.camp.edge_r, -1);
  h.pack_on = false;
  hikerCarry(h, CARRY_STICK);
  w.fx.on = true;
  w.fx.path_on = false;
}
// The walking stick laid down behind where he sits (it stays there; the
// afternoon may put it on the wood pile).
void doStickDown(World& w, float) {
  Hiker& h = w.hiker;
  hikerCarry(h, CARRY_NONE);
  Item& s = w.items[ITEM_STICK];
  s.held = false;
  s.on_fire = false;
  s.critter = 0;
  s.down = true;
  s.size = 0.75f;
  s.facing = 1;
  s.angle = 0.f;
  s.pos = Vec2{h.feet.x - (float)h.facing * 6.f, h.feet.y + 1.f};
}

// The hat in his near hand (World does not place it: HOLD_HAT puts it there).
void hatToHand(World& w) {
  Item& hat = w.items[ITEM_HAT];
  w.hiker.hat_on = false;
  hat.held = false;
  hat.down = true;
  hat.critter = 0;
  hat.on_fire = false;
  hat.size = 1.f;
  hat.facing = (int8_t)w.hiker.facing;
  hat.pos = hikerHandPos(w.hiker, 0);
  w.spine.fhold = HOLD_HAT;
}
void hatOn(World& w) {
  Item& hat = w.items[ITEM_HAT];
  w.hiker.hat_on = true;
  hat.held = hat.down = false;
  w.spine.fhold = HOLD_NONE;
}

// The arm to the hat (absolute shoulder and elbow angles), and where it fans from.
constexpr float kHatSh = 2.75f, kHatEl = 2.0f;
constexpr float kFanSh = 1.05f, kFanEl = 1.85f;

// Sitting: up to the hat, off, down in front of his face, ready to fan.
void actHatOff(World& w, float u) {
  const float k = easeInOut(window(u, 0.f, 0.35f));
  const float d = easeInOut(window(u, 0.5f, 1.f));
  setArm(w, 0, k, lerp(kHatSh, kFanSh, d), lerp(kHatEl, kFanEl, d));
  w.fx.head = -0.1f * k;
  if (u >= 0.42f && w.hiker.hat_on) hatToHand(w);
}
// Fans his face with it, chin up: hot work, the day's walk.
constexpr float kFanS = 6.f;
void actFan(World& w, float u) {
  const float s = u * kFanS;
  const float e = envelope(u, 0.1f, 0.12f);
  setArm(w, 0, 1.f, kFanSh + 0.08f * e * sinT(s * 1.1f), kFanEl + 0.42f * e * sinT(s * 2.2f));
  w.fx.head = -0.1f - 0.08f * e;
  w.fx.lean = -0.06f * e;
}
// And back on his head; the arm down.
void actHatOn(World& w, float u) {
  const float k = 1.f - easeInOut(window(u, 0.55f, 1.f));
  const float up = easeInOut(window(u, 0.f, 0.45f));
  setArm(w, 0, k, lerp(kFanSh, kHatSh, up), lerp(kFanEl, kHatEl, up));
  w.fx.head = -0.1f * k;
  if (u >= 0.45f && !w.hiker.hat_on) hatOn(w);
  if (u >= 1.f) fxRest(w);
}

// Return b: the limp, over the walk in. A dip on one step of every two, the
// head going down with it.
void doLimpStart(World& w, float) {
  Hiker& h = w.hiker;
  const Vec2 to = w.spine.fp[0];
  HikerFx& fx = w.fx;
  fx.path_on = true;
  fx.path_x0 = h.feet.x;
  fx.path_y0 = h.feet.y;
  fx.path_x1 = to.x;
  fx.path_y1 = to.y;
  hikerWalkTo(h, to.x, 21.f);
}
bool walking(const World& w) { return w.hiker.walking; }
void actLimp(World& w, float) {
  const Hiker& h = w.hiker;
  const float s = h.walking ? sinT(h.walk_phase) : 0.f;
  const float dip = s > 0.f ? s * s : 0.f;
  w.fx.lean = 0.14f * dip;
  w.fx.head = 0.16f * dip;
  w.fx.lift = 0.f;
}
void doFxRest(World& w, float) { fxRest(w); }

// Where the boot is held out to be looked at: the arm out in front, the boot
// clear of his outline.
constexpr float kShoeSh = 1.5f, kShoeEl = 0.5f;
// The boot off: leaning over his foot, two tugs; it comes away in his hand.
void actBootOff(World& w, float u) {
  Hiker& h = w.hiker;
  const float k = envelope(u, 0.12f, 0.02f);
  const float tug = plateau(u * 2.4f, 0.6f, 0.12f, 0.1f) + plateau(u * 2.4f, 1.15f, 0.12f, 0.1f);
  const float up = easeInOut(window(u, 0.7f, 1.f));
  w.fx.lean = (0.36f - 0.1f * tug) * (1.f - up) + 0.08f * up;
  w.fx.head = 0.25f * k;
  setArm(w, 0, maxf(k, up), lerp(0.75f - 0.15f * tug, kShoeSh, up), lerp(0.15f, kShoeEl, up));
  if (u >= 0.7f && w.spine.fhold != HOLD_SHOE) {
    Item& s = w.items[ITEM_SHOE];
    s.held = false;
    s.down = true;
    s.critter = 0;
    s.on_fire = false;
    s.size = 1.f;
    s.facing = (int8_t)h.facing;
    s.pos = hikerHandPos(h, 0);
    w.spine.fhold = HOLD_SHOE;
  }
}
// Held up and shaken: a stone drops out of it; he peers in.
constexpr float kShakeShoeS = 3.2f;
void actShakeShoe(World& w, float u) {
  const float s = u * kShakeShoeS;
  const float shake = s < 1.6f ? sinT(s * 3.2f) * window(s, 0.f, 0.3f) : 0.f;
  const float peer = plateau(s, 1.9f, 0.3f, 0.6f);
  setArm(w, 0, 1.f, kShoeSh + 0.12f * shake, kShoeEl + 0.3f * shake + 0.35f * peer);
  w.fx.lean = 0.08f;
  w.fx.head = 0.3f * peer;
  if (crossed(s, s - SS_TICK_DT, 0.8f)) {
    const Vec2 at = w.items[ITEM_SHOE].pos;
    fleck(w, Vec2{at.x, at.y + 1.f}, Vec2{(float)w.hiker.facing * 12.f, 10.f});
  }
}
// And back on: down to the foot, a tug, the arm back.
void actBootOn(World& w, float u) {
  const float d = easeInOut(window(u, 0.f, 0.3f));
  const float k = 1.f - easeInOut(window(u, 0.8f, 1.f));
  const float tug = plateau(u * 2.f, 0.8f, 0.12f, 0.1f);
  setArm(w, 0, k, lerp(kShoeSh, 0.75f - 0.15f * tug, d), lerp(kShoeEl, 0.15f, d));
  w.fx.lean = k * lerp(0.08f, 0.36f - 0.1f * tug, d);
  w.fx.head = 0.25f * k * d;
  if (u >= 0.32f && w.spine.fhold == HOLD_SHOE) {
    w.items[ITEM_SHOE].down = false;
    w.spine.fhold = HOLD_NONE;
  }
  if (u >= 1.f) fxRest(w);
}

// Return c: burrs. Picked off his sleeve (the near hand to the far forearm,
// pluck, flick), off his trousers (bent over), and one off his hat; each one
// flicked away (a fleck).
void flick(World& w) {
  const Hiker& h = w.hiker;
  fleck(w, hikerHandPos(h, 0), Vec2{(float)h.facing * 55.f, -45.f});
}
constexpr float kSleeveS = 3.2f;
void actPickSleeve(World& w, float u) {
  const float s = u * kSleeveS;
  const float k = envelope(u, 0.12f, 0.12f);
  float out = 0.f;
  for (int i = 0; i < 2; ++i) out = maxf(out, plateau(s, 0.9f + 1.1f * (float)i, 0.12f, 0.08f));
  setArm(w, 1, k, 0.75f, 0.5f);                               // the sleeve held out
  setArm(w, 0, k, 0.9f + 0.4f * out, 1.75f - 1.2f * out);    // to it, and flicked out
  w.fx.head = 0.2f * k;
  if (crossed(s, s - SS_TICK_DT, 1.0f) || crossed(s, s - SS_TICK_DT, 2.1f)) flick(w);
  if (u >= 1.f) fxRest(w);
}
constexpr float kLegsS = 3.4f;
void actPickLegs(World& w, float u) {
  const float s = u * kLegsS;
  const float k = envelope(u, 0.15f, 0.15f);
  float out = 0.f;
  for (int i = 0; i < 2; ++i) out = maxf(out, plateau(s, 1.0f + 1.1f * (float)i, 0.12f, 0.08f));
  w.fx.lean = 0.38f * k;
  w.fx.head = 0.15f * k;
  setArm(w, 0, k, 0.25f + 0.9f * out, 0.15f);
  if (crossed(s, s - SS_TICK_DT, 1.1f) || crossed(s, s - SS_TICK_DT, 2.2f)) flick(w);
  if (u >= 1.f) fxRest(w);
}
constexpr float kPickHatS = 2.8f;
void actPickHat(World& w, float u) {
  const float s = u * kPickHatS;
  const float k = envelope(u, 0.15f, 0.15f);
  const float out = plateau(s, 1.3f, 0.12f, 0.15f);
  setArm(w, 0, k, lerp(2.55f, 2.1f, out), lerp(2.1f, 0.4f, out));
  w.fx.head = -0.2f * k;
  if (crossed(s, s - SS_TICK_DT, 1.38f)) flick(w);
  if (u >= 1.f) fxRest(w);
}

// ---- lunch -----------------------------------------------------------------------------------

// Digs in the pack: leans in, the arm working.
void actRummage(World& w, float u) {
  const float env = envelope(u, 0.15f, 0.15f);
  w.fx.lean = env * (0.12f + 0.06f * sinT(u * 3.f));
  w.fx.reach_to = 0.9f;
  w.fx.reach = env * (0.5f + 0.3f * sinT(u * 4.f + 0.25f));
  if (u >= 1.f) fxRest(w);
}
// Out of the pack: the pot (cold: last night's) and the fork.
void doLunchUp(World& w, float) {
  Item& p = w.items[ITEM_POT];
  p.held = true;
  p.down = false;
  p.on_fire = false;
  Item& f = w.items[ITEM_FORK];
  f.held = true;
  f.down = false;
  hikerCarry(w.hiker, CARRY_MUG);
}
// Sat down: the pot held out over his knees in the far hand (on the ground
// in front of him his knees would hide it), the fork in the near hand.
// HOLD_POT keeps it at that hand and the arm there.
constexpr float kPotSh = 1.2f, kPotEl = 0.45f;
void holdPotArm(World& w) {
  if (w.spine.fhold == HOLD_POT) setArm(w, 1, 1.f, kPotSh, kPotEl);
}
// fxRest, but the far arm keeps the pot.
void restKeepPot(World& w) {
  fxRest(w);
  holdPotArm(w);
}
void actPotToLap(World& w, float u) {
  Item& p = w.items[ITEM_POT];
  Hiker& h = w.hiker;
  if (u <= 0.f) {
    p.held = false;
    p.down = true;
    p.facing = (int8_t)h.facing;
    hikerCarry(h, CARRY_NONE);
    w.spine.fhold = HOLD_POT;
  }
  setArm(w, 1, 1.f, kPotSh, kPotEl);
  setArm(w, 0, 1.f - easeInOut(u), 0.9f, 1.0f);
  if (u >= 1.f) restKeepPot(w);
}
// A bite counted once, on the way up (the chipmunk edges in on them).
void noteBite(World& w, float lift) {
  SpineState& sp = w.spine;
  if (lift > 0.5f && !sp.bite_up) {
    sp.bite_up = true;
    if (sp.bites < 255) ++sp.bites;
  } else if (lift < 0.3f) {
    sp.bite_up = false;
  }
}
// Fork down into the pot, up to his mouth, every `period` seconds.
void eat(World& w, float u, float secs, float period, float hunch) {
  const float s = u * secs;
  const float env = envelope(u, minf(0.3f, 0.5f / secs), minf(0.3f, 0.6f / secs));
  const float ph = s / period;
  const float f = ph - floorf(ph);
  const float dip = plateau(f, 0.f, 0.16f, 0.08f);
  const float lift = plateau(f, 0.42f, 0.16f, 0.14f);
  noteBite(w, lift * env);
  // Down into the pot (out over his knees), up to his mouth.
  setArm(w, 0, env, lerp(lerp(0.75f, 1.2f, dip), 0.9f, lift), lerp(lerp(1.3f, 0.55f, dip), 2.45f, lift));
  w.fx.head = env * (0.08f + 0.25f * dip - 0.14f * lift + 0.1f * hunch);
  w.fx.lean = env * (0.05f * dip + 0.14f * hunch);
  holdPotArm(w);
  if (u >= 1.f) restKeepPot(w);
}
constexpr float kEatA = 7.f, kEatB = 4.4f, kEatC = 9.f, kEatLast = 2.6f, kEatFast = 4.2f;
void actEatA(World& w, float u) { eat(w, u, kEatA, 2.2f, 0.f); }
void actEatB(World& w, float u) { eat(w, u, kEatB, 2.2f, 0.f); }
void actEatC(World& w, float u) { eat(w, u, kEatC, 2.2f, 0.f); }
void actEatLast(World& w, float u) { eat(w, u, kEatLast, 2.2f, 0.f); }
void actEatFast(World& w, float u) { eat(w, u, kEatFast, 0.7f, 1.f); }

// Lunch a: a bee comes for the pot. In from the right, it hovers over the pot;
// he swats (it darts off, waits, comes back); he eats faster; it goes.
Vec2 potTop(const World& w) {
  const Item& p = w.items[ITEM_POT];
  return Vec2{p.pos.x, p.pos.y - 15.f};
}
void doBeeIn(World& w, float) {
  const int b = worldTakeBit(w, BIT_BEE);
  if (b < 0) return;
  w.spine.fbit = (int8_t)b;
  const Vec2 p = potTop(w);
  w.bits[b].pos = Vec2{p.x + 150.f, p.y - 90.f};
  w.bits[b].land_y = w.hiker.feet.y + 4.f;
  w.spine.fp[1] = w.bits[b].pos;
  stageTo(w, FS_BEE_IN);
}
bool beeHere(const World& w) { return bitGone(w) || w.spine.fstage == FS_BEE_HOVER; }
bool beeBack(const World& w) { return bitGone(w) || w.spine.fstage == FS_BEE_HOVER2; }
// The swat: the fork hand out and up at it, fast; the bee is off as it comes.
void actSwat(World& w, float u) {
  const float out = easeOutCubic(window(u, 0.f, 0.3f)) * (1.f - easeInOut(window(u, 0.5f, 1.f)));
  setArm(w, 0, out, 1.95f, 0.35f);
  w.fx.head = -0.2f * out;
  w.fx.lean = -0.08f * out;
  if (u >= 0.3f && w.spine.fstage == FS_BEE_HOVER) {
    if (Bit* b = bitOf(w)) w.spine.fp[1] = b->pos;
    stageTo(w, FS_BEE_SHOO);
  }
  holdPotArm(w);
  if (u >= 1.f) restKeepPot(w);
}
// It is back: a glare at it, chin up.
void actGlare(World& w, float u) {
  const float k = envelope(u, 0.25f, 0.25f);
  w.fx.head = -0.22f * k;
  w.fx.lean = -0.05f * k;
  holdPotArm(w);
  if (u >= 1.f) restKeepPot(w);
}
void doBeeOff(World& w, float) {
  if (Bit* b = bitOf(w)) {
    w.spine.fp[1] = b->pos;
    stageTo(w, FS_BEE_OFF);
  }
}

// Lunch b: a chipmunk edges in from the left a hop a bite, along the front of
// the pit; at the pot it snatches a crumb and runs.
// The crumbs: on the ground just beyond his feet (he sits facing the fire).
Vec2 crumbs(const World& w) { return Vec2{w.spine.camp.sit.x - 34.f, w.fire.pos.y + 14.f}; }
Vec2 chipStart(const World& w) { return Vec2{w.fire.pos.x - 66.f, w.fire.pos.y + 14.f}; }
Vec2 chipNear(const World& w) { return Vec2{crumbs(w).x - 14.f, w.fire.pos.y + 14.f}; }
void doChipIn(World& w, float) {
  const Vec2 a = chipStart(w);
  const int s = spawnChip(w, Vec2{-16.f, a.y}, 1);
  if (s < 0) return;
  directorWalk(w, s, a, kChipRun, CA_RUN);
  w.spine.bites = 0;
  w.spine.bite_up = false;
  w.spine.target = 0;
  stageTo(w, FS_CHIP_IN);
}
bool chipGrabbed(const World& w) { return critGone(w) || w.spine.fstage >= FS_CHIP_OFF; }
// He sees it go: looks down after it, then a shrug.
void actWatchChip(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.25f);
  w.fx.head = 0.3f * k;
  w.fx.lean = 0.08f * k;
  holdPotArm(w);
  if (u >= 1.f) restKeepPot(w);
}
void actShrugSit(World& w, float u) {
  const float k = easeOutCubic(window(u, 0.f, 0.25f)) * (1.f - easeInOut(window(u, 0.6f, 1.f)));
  w.fx.head = 0.12f * k;
  setArm(w, 0, 0.75f * k, 0.3f, 1.55f);
  if (w.spine.fhold == HOLD_POT) holdPotArm(w);   // one-handed: the pot stays out
  else setArm(w, 1, 0.75f * k, 0.3f, 1.55f);
  if (u >= 1.f) restKeepPot(w);
}

// Lunch, shared end: the pot up off the ground, the fork in it.
void actPotUp(World& w, float u) {
  Item& p = w.items[ITEM_POT];
  if (u <= 0.f) {
    w.spine.v0 = p.pos;
    w.spine.fhold = HOLD_NONE;
  }
  const Vec2 to = hikerHandsMid(w.hiker);
  const float k = easeInOut(window(u, 0.2f, 1.f));
  p.pos = Vec2{lerp(w.spine.v0.x, to.x, k), lerp(w.spine.v0.y, to.y + 3.f, k)};
  setArm(w, 1, 1.f - k, kPotSh, kPotEl);
  if (u >= 1.f) {
    p.down = false;
    p.held = true;
    hikerCarry(w.hiker, CARRY_MUG);
    fxRest(w);
  }
}
// Into the pack, pot and fork.
void doIntoPack(World& w, float) {
  hikerCarry(w.hiker, CARRY_NONE);
  static const ItemId kIn[3] = {ITEM_POT, ITEM_FORK, ITEM_MUG};
  for (ItemId id : kIn) {
    Item& it = w.items[id];
    if (!it.held || it.critter) continue;
    it.held = false;
    it.down = false;
  }
}

// ---- wade ------------------------------------------------------------------------------------

// Sitting on the bank: both boots off, two tugs each; they go down beside him.
void actBootsOff(World& w, float u) {
  Hiker& h = w.hiker;
  const float k = envelope(u, 0.1f, 0.12f);
  float tug = 0.f;
  for (int i = 0; i < 2; ++i) tug = maxf(tug, plateau(u * 3.f, 0.5f + 1.1f * (float)i, 0.12f, 0.12f));
  w.fx.lean = k * (0.36f - 0.12f * tug);
  w.fx.head = 0.22f * k;
  setArm(w, 0, k, 0.72f - 0.2f * tug, 0.15f);
  setArm(w, 1, k, 0.62f - 0.2f * tug, 0.2f);
  if (u >= 0.8f && !w.items[ITEM_SHOE].down) {
    Item& s = w.items[ITEM_SHOE];
    s.held = false;
    s.critter = 0;
    s.on_fire = false;
    s.size = 1.f;
    s.down = true;
    s.facing = (int8_t)h.facing;
    s.pos = besideHim(h, 17.f);
  }
  if (u >= 1.f) fxRest(w);
}
// And on again, back on the bank.
void actBootsOn(World& w, float u) {
  const float k = envelope(u, 0.12f, 0.12f);
  float tug = 0.f;
  for (int i = 0; i < 2; ++i) tug = maxf(tug, plateau(u * 3.f, 0.9f + 0.9f * (float)i, 0.12f, 0.12f));
  w.fx.lean = k * (0.36f - 0.12f * tug);
  w.fx.head = 0.22f * k;
  setArm(w, 0, k, 0.72f - 0.2f * tug, 0.15f);
  setArm(w, 1, k, 0.62f - 0.2f * tug, 0.2f);
  if (u >= 0.25f) w.items[ITEM_SHOE].down = false;
  if (u >= 1.f) fxRest(w);
}
void doWadeOn(World& w, float) { w.fx.wade = true; }
void doWadeOff(World& w, float) { w.fx.wade = false; }

// Wade a: a fish circles his feet under the water (the major's fish, small
// and dark). He watches it go round, bends slowly, grabs: it is gone.
constexpr float kFishLap = 2.8f;   // seconds a lap
// Just under the surface round his shins (his feet are deep under the
// water's redraw), drawn over the water either side of him.
Vec2 fishAt(const World& w, float lap, float* dir) {
  const Vec2 f = w.hiker.feet;
  const float a = lap;
  if (dir) *dir = -sinT(a);   // x velocity's sign
  return Vec2{f.x + 17.f * cosT(a), waterLine(w) + 5.f + 3.5f * sinT(a)};
}
void doFishIn(World& w, float) {
  Glyph& g = newGlyph(w, SH_FISH, w.hiker.feet);
  g.slot = ACTOR;
  g.scale = 0.55f;
  w.spine.fk = 0.f;   // laps so far
  stageTo(w, FS_FISH_CIRCLE);
}
// Head down, watching it; he turns round with it as it passes behind him.
constexpr float kWatchFishS = 7.f;
void actWatchFish(World& w, float u) {
  Hiker& h = w.hiker;
  const float k = envelope(u, 0.1f, 0.02f);
  w.fx.head = 0.38f * k;
  w.fx.lean = 0.12f * k;
  if (const Glyph* g = glyphOf(w, SH_FISH)) {
    const float dx = g->pos.x - h.feet.x;
    if (dx > 9.f) h.facing = 1;
    if (dx < -9.f) h.facing = -1;
  }
}
// The grab: a lunge, both hands into the water; it darts off. Splashes.
void actGrabFish(World& w, float u) {
  Hiker& h = w.hiker;
  const float k = easeOutCubic(window(u, 0.f, 0.22f)) * (1.f - easeInOut(window(u, 0.55f, 1.f)));
  w.fx.lean = 0.12f + 0.3f * k;
  w.fx.head = 0.38f * (1.f - easeInOut(window(u, 0.5f, 1.f)));
  setArm(w, 0, k, 0.3f, 0.05f);
  setArm(w, 1, k, 0.2f, 0.1f);
  if (crossed(u, u - SS_TICK_DT / 1.2f, 0.22f)) {
    const float wl = waterLine(w);
    const Vec2 hd = hikerHandsMid(h);
    ring(w, Vec2{hd.x, wl + 2.f}, 0.9f);
    ring(w, Vec2{hd.x + (float)h.facing * 10.f, wl + 4.f}, 0.6f);
    for (int i = 0; i < 2; ++i)
      drop(w, Vec2{hd.x + (float)(i * 2 - 1) * 4.f, wl - 2.f}, Vec2{(float)(i * 2 - 1) * 35.f, -110.f}, wl + 2.f);
    if (glyphOf(w, SH_FISH)) stageTo(w, FS_FISH_DART);
  }
  if (u >= 1.f) fxRest(w);
}
// Shakes the water off his hands.
void actShakeHands(World& w, float u) {
  const float k = envelope(u, 0.15f, 0.25f);
  const float s = sinT(u * 5.f);
  setArm(w, 0, k, 0.9f + 0.35f * s, 0.9f);
  setArm(w, 1, k, 0.8f - 0.35f * s, 0.9f);
  w.fx.head = 0.12f * k;
  if (crossed(u, u - SS_TICK_DT / 1.4f, 0.35f)) {
    const Vec2 hd = hikerHandsMid(w.hiker);
    drop(w, hd, Vec2{20.f, -40.f}, waterLine(w) + 2.f);
  }
  if (u >= 1.f) fxRest(w);
}

// Wade b: he slips: arms up, flailing, and down he sits in the water with a splash.
void actSlip(World& w, float u) {
  Hiker& h = w.hiker;
  const float fl = easeOutCubic(window(u, 0.f, 0.15f)) * (1.f - easeInOut(window(u, 0.55f, 0.9f)));
  const float s = sinT(u * 6.f);
  setArm(w, 0, fl, 2.3f + 0.4f * s, 0.4f);
  setArm(w, 1, fl, 2.1f - 0.4f * s, 0.5f);
  w.fx.lean = -0.25f * fl + 0.08f * fl * s;
  w.fx.head = -0.2f * fl;
  if (crossed(u, u - SS_TICK_DT / 1.8f, 0.3f)) setPose(w, POSE_SIT, 0.32f);
  if (crossed(u, u - SS_TICK_DT / 1.8f, 0.48f)) {   // the splash
    const float wl = h.feet.y - 2.f - 0.2f * h.height * 0.6f;
    ring(w, Vec2{h.feet.x + 14.f, h.feet.y - 3.f}, 1.1f);
    ring(w, Vec2{h.feet.x - 16.f, h.feet.y - 1.f}, 0.9f);
    for (int i = 0; i < 4; ++i) {
      const float sx = (float)(i - 2) + 0.5f;
      drop(w, Vec2{h.feet.x + sx * 8.f, wl}, Vec2{sx * 30.f, -150.f + 15.f * fabsf(sx)}, h.feet.y - 2.f);
    }
  }
  if (u >= 1.f) fxRest(w);
}

// Wade c: skipping stones from midstream. A stone up off the bed; a throw
// (the skip_stone filler's, from where he stands): the first goes plop, the
// second skips.
void actPickStone(World& w, float u) {
  const float k = envelope(u, 0.25f, 0.3f);
  w.fx.reach_to = 0.3f;
  w.fx.reach = k;
  w.fx.lean = 0.15f * k;
  if (crossed(u, u - SS_TICK_DT, 0.45f)) ring(w, Vec2{hikerHandPos(w.hiker, 0).x, waterLine(w) + 2.f}, 0.4f);
  if (u >= 1.f) fxRest(w);
}
constexpr float kSkipS = 4.6f, kSkipRelease = 0.62f;
void skipThrow(World& w, float u, int n) {
  const float s = u * kSkipS;
  const float back = easeInOut(window(s, 0.f, 0.5f));
  const float sweep = easeOutCubic(window(s, 0.5f, 0.72f));
  const float rest = 1.f - easeInOut(window(s, 1.3f, 2.0f));
  setArm(w, 0, back * rest, lerp(-0.9f, 1.55f, sweep), lerp(0.4f, 0.1f, sweep));
  w.fx.lean = 0.08f * back * rest - 0.06f * sweep * rest;
  w.fx.head = 0.15f * easeInOut(window(s, 0.8f, 1.4f)) * (1.f - easeInOut(window(s, 3.6f, 4.4f)));
  Skip& k = w.skip;
  if (k.t < 0.f && s >= kSkipRelease && u < 1.f) {   // let go: lay the touches out downstream
    const Hiker& h = w.hiker;
    k.n = (uint8_t)n;
    k.p[0] = hikerHandPos(h, 0);
    float x = h.feet.x + (n == 1 ? 26.f : 18.f);
    for (int i = 1; i <= k.n && i <= SKIP_MAX; ++i) {
      x += n == 1 ? 0.f : 40.f - 6.f * (float)(i - 1);
      float cy, half;
      if (!landscapeStreamSpan(w.land, minf(x, 640.f), cy, half)) cy = k.p[i - 1].y + 6.f;
      k.p[i] = Vec2{minf(x, 640.f), cy + 0.15f * half * (float)(i & 1)};
    }
  }
  if (k.t >= 0.f || s >= kSkipRelease) k.t = s - kSkipRelease;
  if (u >= 1.f) {
    k.t = -1.f;
    fxRest(w);
  }
}
void actPlop(World& w, float u) { skipThrow(w, u, 1); }
void actSkip(World& w, float u) { skipThrow(w, u, w.plan.skip_n); }
// That one sank: the head drops, a sigh.
void actSigh(World& w, float u) {
  const float s = sinT(0.5f * u);
  w.fx.lean = 0.08f * s;
  w.fx.head = 0.3f * s;
}

// ---- nap -------------------------------------------------------------------------------------

// The hat over his face as he lies (HOLD_HAT_FACE: middayTick keeps it there).
Vec2 hatOnFace(const World& w) {
  float r = 0.f;
  const Vec2 hd = hikerHeadPos(w.hiker, &r);
  return Vec2{hd.x, hd.y - 0.45f * r};
}
// Lying back, hands behind his head: the near hand takes the hat down over
// his face and goes back.
void actHatOverFace(World& w, float u) {
  const float k = plateau(u * 2.f, 0.1f, 0.35f, 0.15f);
  setArm(w, 0, k, 2.9f, 1.4f);
  if (u >= 0.45f && w.hiker.hat_on) {
    hatToHand(w);
    w.spine.fhold = HOLD_HAT_FACE;
    w.items[ITEM_HAT].facing = (int8_t)(-w.hiker.facing);
    w.items[ITEM_HAT].pos = hatOnFace(w);
  }
  if (u >= 1.f) fxRest(w);
}
// Waking: a long stretch, arms out over his head; the hat off his face and on.
void actStretchLie(World& w, float u) {
  const float k = envelope(u, 0.3f, 0.35f);
  setArm(w, 0, k, 3.35f, 0.15f);
  setArm(w, 1, k, 3.25f, 0.2f);
  w.fx.lift = 0.006f * k;
  if (u >= 1.f) fxRest(w);
}
void actHatOffFace(World& w, float u) {
  const float k = plateau(u * 2.f, 0.1f, 0.35f, 0.2f);
  setArm(w, 0, k, 2.9f, 1.4f);
  if (u >= 0.45f && w.spine.fhold == HOLD_HAT_FACE) hatOn(w);
  if (u >= 1.f) fxRest(w);
}

// Nap a: the chipmunk raids the pack by the tent (he sleeps through it): its
// nose in, the pack shaking, out with a bar, a nibble, away.
void doRaidIn(World& w, float) {
  const Vec2 pk = w.spine.camp.pack;
  const int s = spawnChip(w, Vec2{-16.f, pk.y - 1.f}, 1);
  if (s < 0) return;
  directorWalk(w, s, Vec2{pk.x - 14.f, pk.y - 1.f}, kChipRun, CA_RUN);
  stageTo(w, FS_RAID_IN);
}

// Nap b: a leaf comes down on his belly; later his hand brushes it off, and
// he sleeps on.
// On top of his middle as he lies (the stem end), so the leaf shows above
// his outline (it is ACTOR too). Checked by eye in the preview.
Vec2 belly(const World& w) {
  const Hiker& h = w.hiker;
  return Vec2{h.feet.x - (float)h.facing * 7.f, h.feet.y - 22.f};
}
void doLeafIn(World& w, float) {
  const int b = worldTakeBit(w, BIT_LEAF);
  if (b < 0) return;
  w.spine.fbit = (int8_t)b;
  const Vec2 to = belly(w);
  w.spine.fp[1] = Vec2{to.x + 18.f, 250.f};   // off the pine's lower boughs above him
  // Marked resting from the start: the minors' gust takes every leaf that is
  // not (and not leaf_fall's own); this one is placed here every tick.
  w.bits[b].resting = true;
  w.bits[b].pos = w.spine.fp[1];
  w.bits[b].land_y = w.hiker.feet.y + 2.f;
  stageTo(w, FS_LEAF_FALL);
}
bool leafLanded(const World& w) { return bitGone(w) || w.spine.fstage >= FS_LEAF_REST; }
// The near hand comes from behind his head over his chest, sweeps down along
// his middle toward his hips, and goes back.
void actBrush(World& w, float u) {
  const float k = plateau(u * 2.4f, 0.1f, 0.55f, 0.4f);
  const float sweep = easeInOut(window(u, 0.3f, 0.55f));
  setArm(w, 0, k, lerp(2.55f, 1.5f, sweep), lerp(1.0f, 0.1f, sweep));
  if (u >= 0.45f && w.spine.fstage == FS_LEAF_REST) {
    if (Bit* b = bitOf(w)) w.spine.fp[1] = b->pos;
    stageTo(w, FS_LEAF_SLIDE);
  }
  if (u >= 1.f) fxRest(w);
}

// Nap c: a fly circles his head; a lazy swat in his sleep; it comes back; a
// bigger one knocks the hat off his face and he sits up with a start.
Vec2 headOf(const World& w) {
  float r = 0.f;
  return hikerHeadPos(w.hiker, &r);
}
void doFlyIn(World& w, float) {
  const int b = worldTakeBit(w, BIT_MOTH);
  if (b < 0) return;
  w.spine.fbit = (int8_t)b;
  const Vec2 hd = headOf(w);
  w.spine.fp[1] = Vec2{hd.x - 130.f, hd.y - 90.f};
  w.bits[b].pos = w.spine.fp[1];
  w.bits[b].land_y = w.hiker.feet.y + 3.f;
  stageTo(w, FS_FLY_IN);
}
bool flyCircling(const World& w) { return bitGone(w) || w.spine.fstage == FS_FLY_CIRCLE; }
bool flyBack(const World& w) { return bitGone(w) || w.spine.fstage == FS_FLY_CIRCLE2; }
bool bitGoneP(const World& w) { return bitGone(w); }
// A lazy swat (the near hand flops over his face); the fly hops away.
void actSleepSwat(World& w, float u) {
  const float k = plateau(u * 2.f, 0.05f, 0.3f, 0.2f);
  setArm(w, 0, k, 2.7f, 1.1f);
  if (u >= 0.3f && w.spine.fstage == FS_FLY_CIRCLE) {
    if (Bit* b = bitOf(w)) w.spine.fp[1] = b->pos;
    stageTo(w, FS_FLY_DODGE);
  }
  if (u >= 1.f) fxRest(w);
}
// The big swat: the hat goes flying off his face to the ground by his head.
void actBigSwat(World& w, float u) {
  Hiker& h = w.hiker;
  Item& hat = w.items[ITEM_HAT];
  const float k = easeOutCubic(window(u, 0.f, 0.2f)) * (1.f - easeInOut(window(u, 0.45f, 0.8f)));
  setArm(w, 0, k, 2.6f, 0.5f);
  if (u >= 0.2f && w.spine.fhold == HOLD_HAT_FACE) {
    w.spine.fhold = HOLD_NONE;
    w.spine.v0 = hat.pos;
  }
  if (u >= 0.2f) {
    const Vec2 to{h.feet.x + (float)(-h.facing) * 26.f, h.feet.y + 1.f};
    const float t = window(u, 0.2f, 0.75f);
    hat.pos = Vec2{lerp(w.spine.v0.x, to.x, t), lerp(w.spine.v0.y, to.y, t) - 16.f * sinT(0.5f * t)};
    hat.facing = t > 0.5f ? (int8_t)h.facing : (int8_t)(-h.facing);
    if (u >= 0.75f) hat.pos = to;
  }
  if (u >= 0.25f && w.spine.fstage == FS_FLY_CIRCLE2) {
    if (Bit* b = bitOf(w)) w.spine.fp[1] = b->pos;
    stageTo(w, FS_FLY_OFF);
  }
  if (u >= 1.f) fxRest(w);
}
// Up he sits with a start, and has a look round.
void actStartUp(World& w, float u) {
  if (u <= 0.f) setPose(w, POSE_SIT, 0.45f);
  const float k = easeOutCubic(window(u, 0.2f, 0.4f)) * (1.f - easeInOut(window(u, 0.5f, 1.f)));
  w.fx.lean = -0.12f * k;
  w.fx.head = -0.25f * k;
  if (u >= 1.f) fxRest(w);
}
void actLookRound(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.2f);
  w.fx.head = k * (-0.18f + 0.12f * sinT(u * 1.5f));
  if (u >= 1.f) fxRest(w);
}
// The hat off the ground (behind him: he has turned), into his hand.
void actHatFromGround(World& w, float u) {
  const float k = envelope(u, 0.25f, 0.3f);
  w.fx.lean = 0.2f * k;
  w.fx.reach_to = 0.55f;
  w.fx.reach = k;
  if (u >= 0.5f && w.items[ITEM_HAT].down && w.spine.fhold == HOLD_NONE && !w.hiker.hat_on) w.spine.fhold = HOLD_HAT;
  if (u >= 1.f) {
    w.fx.reach = w.fx.lean = 0.f;
    w.fx.reach_to = 1.3f;
  }
}
// From the hand (at his side) up onto his head; the arm down.
void actHatUpOn(World& w, float u) {
  const float k = plateau(u * 2.f, 0.1f, 0.4f, 0.2f);
  setArm(w, 0, k, kHatSh, kHatEl);
  if (u >= 0.4f && !w.hiker.hat_on) hatOn(w);
  if (u >= 1.f) fxRest(w);
}

// ---- map -------------------------------------------------------------------------------------

// Holding the map up in front of him, both hands (lift: raised, - lowered).
void holdArms(World& w, float k, float lift) {
  setArm(w, 0, k, 1.25f + lift, 0.8f);
  setArm(w, 1, k, 1.12f + lift, 0.95f);
}
void doMapOut(World& w, float) {
  Glyph& g = newGlyph(w, SH_MAP, hikerHandsMid(w.hiker));
  g.slot = CLOUD_LIT;
  g.k = 0.f;
  g.scale = 1.15f;
  g.facing = (int8_t)w.hiker.facing;
  w.spine.fhold = HOLD_MAP;
  w.spine.fk = 0.f;
  stageTo(w, FS_MAP_HELD);
}
void actUnfold(World& w, float u) {
  holdArms(w, easeInOut(window(u, 0.f, 0.45f)), 0.f);
  w.fx.head = 0.25f * easeInOut(window(u, 0.f, 0.5f));
  if (Glyph* g = glyphOf(w, SH_MAP)) g->k = easeInOut(window(u, 0.35f, 1.f));
}
void actStudy(World& w, float u) {
  holdArms(w, 1.f, 0.04f * sinT(u * 1.3f));
  w.fx.head = 0.25f + 0.05f * sinT(u * 1.7f);
}
// Turned round in his hands: half a turn, lifted a little to do it.
void actTurn(World& w, float u) {
  holdArms(w, 1.f, 0.14f * sinT(0.5f * u));
  w.fx.head = 0.25f;
  if (Glyph* g = glyphOf(w, SH_MAP)) {
    if (u <= 0.f) w.spine.u0 = g->rot;
    g->rot = w.spine.u0 + 0.5f * easeInOut(u);
    if (u >= 1.f) g->rot -= floorf(g->rot);
  }
}
// One hand off it to scratch his head (the map in the other).
void actScratchMap(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.2f);
  holdArms(w, 1.f, 0.f);
  setArm(w, 0, 1.f, lerp(1.25f, kHatSh, k), lerp(0.8f, kHatEl + 0.22f * sinT(u * 7.f), k));
  w.spine.fk = k;
  w.fx.head = 0.25f - 0.15f * k;
  if (u >= 1.f) w.spine.fk = 0.f;
}
// A long look at the ridge, the map lowered.
void actLookRidge(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.2f);
  holdArms(w, 1.f, -0.4f * k);
  w.fx.head = lerp(0.25f, -0.3f, k);
  w.fx.lean = -0.05f * k;
}
void actNodMap(World& w, float u) {
  holdArms(w, 1.f, 0.f);
  w.fx.head = 0.25f + 0.2f * sinT(0.5f * window(u, 0.f, 0.5f)) + 0.2f * sinT(0.5f * window(u, 0.5f, 1.f));
}
// Folded, in quarters, and into his pocket.
void actFold(World& w, float u) {
  const float k = 1.f - easeInOut(window(u, 0.7f, 1.f));
  holdArms(w, k, -0.1f * u);
  w.fx.head = 0.25f * k;
  if (Glyph* g = glyphOf(w, SH_MAP)) {
    g->k = 1.f - 0.25f * floorf(minf(4.f, u * 5.f));
    g->rot = 0.f;
    if (u >= 0.85f) g->kind = GL_NONE;
  }
  if (u >= 0.85f && w.spine.fhold == HOLD_MAP) {
    w.spine.fhold = HOLD_NONE;
    stageTo(w, FS_NONE);
  }
  if (u >= 1.f) fxRest(w);
}
// Map b: a gust takes it up out of his hands; he lunges and has it back.
constexpr float kGustS = 2.0f;
void actGust(World& w, float u) {
  const float s = u * kGustS;
  const float away = easeOutCubic(window(s, 0.25f, 0.8f)) * (1.f - easeInOut(window(s, 1.15f, 1.7f)));
  const float lunge = easeOutCubic(window(s, 0.45f, 0.85f)) * (1.f - easeInOut(window(s, 1.2f, 1.9f)));
  const float f = (float)w.hiker.facing;
  holdArms(w, 1.f - lunge, 0.f);
  setArm(w, 0, lunge, 2.0f, 0.15f);
  setArm(w, 1, lunge, 1.9f, 0.2f);
  w.fx.lean = 0.3f * lunge;
  w.fx.head = 0.25f * (1.f - lunge) - 0.25f * lunge;
  Glyph* g = glyphOf(w, SH_MAP);
  if (!g) return;
  if (s >= 0.2f && s < 1.7f && w.spine.fstage == FS_MAP_HELD) stageTo(w, FS_MAP_GUST);
  if (w.spine.fstage == FS_MAP_GUST) {
    const Vec2 m = hikerHandsMid(w.hiker);
    g->pos = Vec2{m.x + f * 34.f * away, m.y - 3.f - 26.f * away};
    g->rot = 0.1f * away * sinT(s * 2.3f);
    g->t = 0.6f * away * sinT(s * 1.6f);
    if (s >= 1.7f) {
      g->t = 0.f;
      g->rot = 0.f;
      stageTo(w, FS_MAP_HELD);
    }
  }
  if (u >= 1.f) {
    w.fx.lean = 0.f;
    holdArms(w, 1.f, 0.f);
  }
}
void actFoldFast(World& w, float u) {
  holdArms(w, 1.f, -0.15f * u);
  w.fx.head = 0.2f;
  if (Glyph* g = glyphOf(w, SH_MAP)) g->k = 1.f - 0.34f * floorf(minf(3.f, u * 3.4f));
}
// And sits on it: up off the ground a moment, the folded map slipped under him.
void actSitOnMap(World& w, float u) {
  Hiker& h = w.hiker;
  const float k = 1.f - easeInOut(window(u, 0.3f, 0.6f));
  holdArms(w, k, -0.2f);
  w.fx.lift = 0.03f * plateau(u, 0.25f, 0.12f, 0.15f);
  w.fx.head = 0.2f * k;
  Glyph* g = glyphOf(w, SH_MAP);
  if (g && u >= 0.3f) {
    if (w.spine.fstage != FS_MAP_SAT) {
      stageTo(w, FS_MAP_SAT);
      w.spine.v0 = g->pos;
    }
    const Vec2 to{h.feet.x + (float)h.facing * 9.f, h.feet.y - 1.f};   // its edge shows under his thighs
    const float t = easeInOut(window(u, 0.3f, 0.5f));
    g->pos = Vec2{lerp(w.spine.v0.x, to.x, t), lerp(w.spine.v0.y, to.y, t)};
    g->t = 0.2f * t;   // flat under him
    g->base_y = (int16_t)(h.feet.y + 2.f);
  }
  if (u >= 1.f) fxRest(w);
}
// Up, and the map off the ground into his pocket.
void doMapPocket(World& w, float) {
  if (Glyph* g = glyphOf(w, SH_MAP)) g->kind = GL_NONE;
  w.spine.fhold = HOLD_NONE;
  stageTo(w, FS_NONE);
}

// ---- afternoon --------------------------------------------------------------------------------

// Afternoon a: deadfall under the camp pine, gathered into an armload.
void actGather(World& w, float u) {
  const float env = envelope(u, 0.12f, 0.15f);
  w.fx.lean = env * (0.1f + 0.06f * sinT(u * 2.f));
  w.fx.reach_to = 0.55f;
  w.fx.reach = env * (0.55f + 0.35f * sinT(u * 3.f + 0.25f));
  if (u >= 1.f) fxRest(w);
}
void doArmload(World& w, float) { hikerCarry(w.hiker, CARRY_WOOD); }
void doWoodOnPile(World& w, float) {
  Item& pile = w.items[ITEM_WOOD];
  pile.down = true;
  pile.size = 1.f;
  hikerCarry(w.hiker, CARRY_NONE);
}
// Afternoon b: the walking stick on the pile; the pile squared up.
void doStickUp(World& w, float) {
  w.items[ITEM_STICK].down = false;
  hikerCarry(w.hiker, CARRY_STICK);
}
void doStickOnPile(World& w, float) {
  Item& pile = w.items[ITEM_WOOD];
  pile.size = minf(1.2f, pile.size + 0.12f);
  hikerCarry(w.hiker, CARRY_NONE);
}
void actSquare(World& w, float u) {
  const float k = envelope(u, 0.15f, 0.15f);
  const float pat = plateau(u * 2.f, 0.3f, 0.15f, 0.1f) + plateau(u * 2.f, 1.1f, 0.15f, 0.1f);
  w.fx.reach_to = 1.0f + 0.25f * pat;
  w.fx.reach = 0.8f * k;
  w.fx.lean = 0.1f * k;
  if (u >= 1.f) fxRest(w);
}
// The book the read filler left out, if it did: into the pack with the rest.
bool bookDown(const World& w) { return w.items[ITEM_BOOK].down; }
void doBookPocket(World& w, float) { w.items[ITEM_BOOK].down = false; }
void doMugUp(World& w, float) {
  Item& m = w.items[ITEM_MUG];
  m.down = false;
  m.held = false;
  hikerCarry(w.hiker, CARRY_MUG);
}
void doMugDown(World& w, float) {
  hikerCarry(w.hiker, CARRY_NONE);
  scriptSetDown(w, ITEM_MUG, besideHim(w.hiker, 14.f));
}
// Sitting with the mug, looking out over the valley: two slow sips.
void mugHold(World& w, float env, float sip, float head) {
  setArm(w, 0, env, lerp(0.2f, 0.9f, sip), lerp(2.3f, 2.45f, sip));
  w.fx.head = env * (head - 0.32f * sip);
}
constexpr float kMugLookS = 13.f;
void actMugLook(World& w, float u) {
  const float s = u * kMugLookS;
  const float sip = maxf(plateau(s, 2.f, 0.6f, 1.0f), plateau(s, 8.5f, 0.6f, 1.0f));
  mugHold(w, envelope(u, 0.05f, 0.05f), sip, -0.08f);
  if (u >= 1.f) fxRest(w);
}

// ---- the tick ----------------------------------------------------------------------------------

void critTick(World& w) {
  SpineState& sp = w.spine;
  if (sp.fcrit < 0) return;
  if (critGone(w)) {   // released (out of sight), or never spawned
    sp.fcrit = -1;
    w.busy = false;
    if ((sp.fstage >= FS_CHIP_IN && sp.fstage <= FS_CHIP_OFF) || (sp.fstage >= FS_RAID_IN && sp.fstage <= FS_RAID_OFF))
      stageTo(w, FS_NONE);
    return;
  }
  Critter& c = w.critters[sp.fcrit];
  const float t = sp.ft;
  switch (sp.fstage) {
    case FS_CHIP_IN:
      if (critterArrived(c)) {
        c.facing = 1;
        critterSetAction(c, CA_SIT, 0.3f);
        stageTo(w, FS_CHIP_EDGE);
      }
      break;
    case FS_CHIP_EDGE: {   // a hop nearer each bite he takes, up to the pot
      if (!critterArrived(c)) break;
      if (c.action == CA_IDLE && t > 0.3f) critterSetAction(c, CA_SIT, 0.3f);
      const Vec2 near = chipNear(w);
      if ((sp.bites > sp.target || t > 2.6f) && t > 0.6f) {
        sp.target = sp.bites;
        if (c.feet.x + 6.f < near.x) {
          const float x = minf(near.x, c.feet.x + 16.f);
          critterSetAction(c, CA_MOVE, 0.15f);
          directorWalk(w, sp.fcrit, Vec2{x, near.y}, 22.f, CA_HOP);
          stageTo(w, FS_CHIP_EDGE);
        } else {   // close enough: the dash for the crumb by the pot
          directorWalk(w, sp.fcrit, Vec2{crumbs(w).x - 5.f, near.y}, kChipRun, CA_RUN);
          stageTo(w, FS_CHIP_GRAB);
        }
      }
      break;
    }
    case FS_CHIP_GRAB:
      if (critterArrived(c) && c.carry == CC_NONE) {
        c.carry = CC_CRUMB;
        critterSetAction(c, CA_SIT, 0.2f);
      }
      if (c.carry == CC_CRUMB && t > 0.9f) {
        directorExit(w, sp.fcrit, -1, 95.f, CA_RUN);
        stageTo(w, FS_CHIP_OFF);
      }
      break;
    case FS_RAID_IN:
      if (critterArrived(c)) {
        c.facing = 1;
        critterSetAction(c, CA_SNIFF, 0.3f);
        stageTo(w, FS_RAID_DIG);
      }
      break;
    case FS_RAID_DIG: {   // nose in, then a paw; the pack shakes
      if (t > 1.2f && c.action == CA_SNIFF) critterSetAction(c, CA_REACH, 0.3f);
      const float bx = sp.camp.pack.x;
      w.pack.pos.x = bx + (t > 1.5f && t < 3.4f ? (((int)(t * 6.f) & 1) ? 1.f : 0.f) : 0.f);
      if (t > 3.6f) {
        w.pack.pos.x = bx;
        c.carry = CC_BAR;
        critterSetAction(c, CA_SIT, 0.3f);
        stageTo(w, FS_RAID_EAT);
      }
      break;
    }
    case FS_RAID_EAT:   // a nibble, a look round, away with it
      if (t > 0.5f && t - SS_TICK_DT <= 0.5f) critterSetAction(c, CA_EAT, 0.3f);
      if (t > 2.6f && t - SS_TICK_DT <= 2.6f) critterSetAction(c, CA_LOOK, 0.25f);
      if (t > 3.8f) {
        directorExit(w, sp.fcrit, -1, 80.f, CA_RUN);
        stageTo(w, FS_RAID_OFF);
      }
      break;
    default: break;
  }
}

void bitTick(World& w) {
  SpineState& sp = w.spine;
  if (sp.fbit < 0) return;
  Bit* b = bitOf(w);
  if (!b) {
    sp.fbit = -1;
    return;
  }
  const float t = sp.ft;
  const Hiker& h = w.hiker;
  switch (sp.fstage) {
    // The bee (lunch a).
    case FS_BEE_IN: {
      const Vec2 to = potTop(w);
      const float u = clamp01(t / 2.4f);
      const Vec2 a = sp.fp[1];
      b->pos = bez(a, Vec2{lerp(a.x, to.x, 0.6f), to.y - 40.f}, to, easeOutCubic(u));
      const Vec2 z = buzz(t, 1.f - u);
      b->pos.x += z.x;
      b->pos.y += z.y;
      if (u >= 1.f) stageTo(w, FS_BEE_HOVER);
      break;
    }
    case FS_BEE_HOVER:
    case FS_BEE_HOVER2: {   // round the pot and his fork hand
      const Vec2 c = potTop(w);
      const Vec2 z = buzz(t, 0.8f);
      b->pos = Vec2{c.x + 7.f * sinT(t * 0.45f) + z.x, c.y - 4.f + 4.f * sinT(t * 0.7f) + z.y};
      break;
    }
    case FS_BEE_SHOO: {   // knocked away, up and out
      const Vec2 a = sp.fp[1], to{potTop(w).x + 46.f, potTop(w).y - 58.f};
      const float u = clamp01(t / 0.6f);
      b->pos = bez(a, Vec2{a.x + 30.f, a.y}, to, easeOutCubic(u));
      if (u >= 1.f) stageTo(w, FS_BEE_WAIT);
      break;
    }
    case FS_BEE_WAIT: {   // a moment out there, then back for more
      const Vec2 c{potTop(w).x + 46.f, potTop(w).y - 58.f};
      const Vec2 z = buzz(t, 1.f);
      b->pos = Vec2{c.x + 4.f * sinT(t * 0.6f) + z.x, c.y + 3.f * sinT(t * 0.9f) + z.y};
      if (t > 2.4f) {
        sp.fp[1] = b->pos;
        stageTo(w, FS_BEE_BACK);
      }
      break;
    }
    case FS_BEE_BACK: {
      const Vec2 a = sp.fp[1], to = potTop(w);
      const float u = clamp01(t / 1.4f);
      b->pos = bez(a, Vec2{a.x - 20.f, to.y - 30.f}, to, easeInOut(u));
      if (u >= 1.f) stageTo(w, FS_BEE_HOVER2);
      break;
    }
    case FS_BEE_OFF: {   // gives up: away over the camp to the right
      const Vec2 a = sp.fp[1], to{a.x + 260.f, -16.f};
      const float u = clamp01(t / 3.f);
      b->pos = bez(a, Vec2{a.x + 60.f, a.y - 10.f}, to, easeInCubic(u));
      if (u >= 1.f) freeBit(w);
      break;
    }
    // The leaf (nap b).
    case FS_LEAF_FALL: {
      const Vec2 a = sp.fp[1], to = belly(w);
      const float y = a.y + 28.f * t;
      const float u = clamp01((y - a.y) / maxf(1.f, to.y - a.y));
      b->pos = Vec2{lerp(a.x, to.x, easeInOut(u)) + 14.f * sinT(t / 2.8f) * (1.f - u * u), minf(y, to.y)};
      b->spin = 0.08f * cosT(t / 2.8f) + 0.02f;
      b->land_y = h.feet.y + 2.f;
      if (u >= 1.f) {
        b->pos = to;
        b->spin = h.facing > 0 ? -0.07f : 0.57f;   // tip up (as on his knee in stream_listen)
        stageTo(w, FS_LEAF_REST);
      }
      break;
    }
    case FS_LEAF_REST:
      b->pos = belly(w);
      b->spin = h.facing > 0 ? -0.07f : 0.57f;
      break;
    case FS_LEAF_SLIDE: {   // flicked off: up a little, then down to the grass in front of him, and it stays
      const Vec2 a = sp.fp[1], to{a.x + (float)h.facing * 16.f, h.feet.y + 4.f};
      const float u = clamp01(t / 1.6f);
      const float up = 10.f * sinT(0.5f * window(u, 0.f, 0.45f));
      b->pos = Vec2{lerp(a.x, to.x, u) + 4.f * sinT(u * 1.5f), lerp(a.y, to.y, easeInCubic(window(u, 0.2f, 1.f))) - up};
      b->spin = lerp(h.facing > 0 ? -0.07f : 0.57f, 0.25f, u);
      b->land_y = to.y;
      if (u >= 1.f) {
        b->resting = true;
        sp.fbit = -1;   // a leaf on the ground like any other now
        stageTo(w, FS_NONE);
      }
      break;
    }
    // The fly (nap c).
    case FS_FLY_IN: {
      const Vec2 hd = headOf(w);
      const Vec2 to{hd.x + 11.f, hd.y - 8.f};
      const float u = clamp01(t / 2.2f);
      const Vec2 a = sp.fp[1];
      b->pos = bez(a, Vec2{lerp(a.x, to.x, 0.5f), a.y + 10.f}, to, easeOutCubic(u));
      if (u >= 1.f) stageTo(w, FS_FLY_CIRCLE);
      break;
    }
    case FS_FLY_CIRCLE:
    case FS_FLY_CIRCLE2: {
      const Vec2 hd = headOf(w);
      const float a = t / 1.5f;
      const Vec2 z = buzz(t, 0.7f);
      b->pos = Vec2{hd.x + 11.f * cosT(a) + z.x, hd.y - 8.f + 6.f * sinT(a) + z.y};
      break;
    }
    case FS_FLY_DODGE: {   // up out of the way, a few loops, back down
      const Vec2 a = sp.fp[1];
      const float up = easeOutCubic(window(t, 0.f, 0.4f)) * (1.f - easeInOut(window(t, 2.6f, 3.4f)));
      const Vec2 z = buzz(t, 1.f);
      b->pos = Vec2{a.x + 6.f * sinT(t * 0.7f) + z.x, a.y - 26.f * up + z.y};
      if (t > 3.4f) stageTo(w, FS_FLY_CIRCLE2);
      break;
    }
    case FS_FLY_OFF: {
      const Vec2 a = sp.fp[1], to{a.x + 150.f, -14.f};
      const float u = clamp01(t / 2.4f);
      b->pos = bez(a, Vec2{a.x + 20.f, a.y - 30.f}, to, easeInCubic(u));
      b->pos.x += 3.f * sinT(t * 1.7f);
      if (u >= 1.f) freeBit(w);
      break;
    }
    default: break;
  }
}

void glyphTick(World& w) {
  SpineState& sp = w.spine;
  const Hiker& h = w.hiker;
  if (Glyph* g = glyphOf(w, SH_FISH)) {
    if (sp.fstage == FS_FISH_CIRCLE) {
      float dir = 1.f;
      const float lap = sp.ft / kFishLap;
      g->pos = fishAt(w, lap, &dir);
      g->facing = dir < 0.f ? -1 : 1;
      g->rot = 0.03f * sinT(lap * 3.f);
      g->base_y = (int16_t)(h.feet.y + 3.f);
    } else if (sp.fstage == FS_FISH_DART) {   // off downstream (left), and gone under
      const float u = clamp01(sp.ft / 0.9f);
      g->facing = -1;
      g->pos.x -= 120.f * SS_TICK_DT;
      g->pos.y += 14.f * SS_TICK_DT;
      g->rot = 0.f;
      g->scale = 0.55f * (1.f - u);
      g->base_y = (int16_t)(h.feet.y + 3.f);
      if (u >= 1.f) {
        g->kind = GL_NONE;
        stageTo(w, FS_NONE);
      }
    }
  }
  if (sp.fhold == HOLD_MAP && sp.fstage == FS_MAP_HELD) {
    if (Glyph* g = glyphOf(w, SH_MAP)) {
      const Vec2 m = hikerHandsMid(h), one = hikerHandPos(h, 1);
      g->pos = Vec2{lerp(m.x, one.x, sp.fk), lerp(m.y, one.y, sp.fk) - 3.f};
      g->facing = (int8_t)h.facing;
      g->base_y = (int16_t)(h.feet.y + 2.f);
    }
  }
}

// What his hand holds that World does not place.
void holdTick(World& w) {
  const Hiker& h = w.hiker;
  switch (w.spine.fhold) {
    case HOLD_HAT: {
      Item& hat = w.items[ITEM_HAT];
      const Vec2 p = hikerHandPos(h, 0);
      hat.pos = Vec2{p.x, p.y + 3.f};
      hat.facing = (int8_t)h.facing;
      break;
    }
    case HOLD_SHOE: {
      Item& s = w.items[ITEM_SHOE];
      const Vec2 p = hikerHandPos(h, 0);
      s.pos = Vec2{p.x, p.y + 4.f};
      s.facing = (int8_t)h.facing;
      break;
    }
    case HOLD_HAT_FACE:
      w.items[ITEM_HAT].pos = hatOnFace(w);
      break;
    case HOLD_POT: {   // the pot's rim in his far hand
      Item& p = w.items[ITEM_POT];
      const Vec2 q = hikerHandPos(h, 1);
      p.pos = Vec2{q.x - (float)h.facing * 1.f, q.y + 8.f};
      p.facing = (int8_t)h.facing;
      break;
    }
    default: break;
  }
}

// ---- scripts -----------------------------------------------------------------------------

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
#define FROM_REST pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
#define PACK_CROUCH go(AT_PACK_WORK, kStroll), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.6f)

// Return, shared: in from the right over the ford with his stick.
#define RETURN_IN doit(doEnter), walk(AT_FORD0, kStroll), walk(AT_FORD1, kWade)
// The stick down behind where he will sit.
#define STICK_DOWN face(LEFT), pose(POSE_CROUCH, 0.5f, 0.2f), doit(doStickDown), pose(POSE_STAND, 0.5f, 0.1f)

// Return a: drops onto his seat by the pit; the hat off, a long fan, back on.
const Step kReturnA[] = {
    RETURN_IN,
    go(AT_FAM0, kStroll),
    STICK_DOWN,
    go(AT_SIT, 30.f),
    face(TO_FIRE),
    pose(POSE_SIT, 0.55f, 0.6f),
    still(0.8f),
    act(actHatOff, 1.3f),
    act(actFan, kFanS),
    act(actHatOn, 1.3f),
    still(1.2f),
};
// Return b: limps in; sits; the boot off, a stone shaken out of it, on again.
const Step kReturnB[] = {
    RETURN_IN,
    doit(doLimpStart),
    act(actLimp, 0.5f),
    repeatWhile(walking, 1),
    doit(doFxRest),
    STICK_DOWN,
    go(AT_SIT, 22.f),
    face(TO_FIRE),
    pose(POSE_SIT, 0.9f, 0.4f),
    act(actBootOff, 2.4f),
    act(actShakeShoe, kShakeShoeS),
    act(actBootOn, 2.0f),
    still(1.0f),
};
// Return c: burrs all over him: picked off his sleeve, his trousers, his hat.
const Step kReturnC[] = {
    RETURN_IN,
    go(AT_FAM0, kStroll),
    STICK_DOWN,
    act(actPickSleeve, kSleeveS),
    act(actPickLegs, kLegsS),
    act(actPickHat, kPickHatS),
    pose(POSE_STAND, 0.4f, 0.2f),
    TO_SIT,
    still(0.6f),
};

// Lunch, shared start: the pot and the fork out of the pack; sits; the pot
// down in front of him. Shared end: pot (fork in it) back in the pack.
#define LUNCH_START                                                                                 \
  FROM_REST, PACK_CROUCH, act(actRummage, 2.0f), doit(doLunchUp), pose(POSE_CARRY, 0.6f, 0.1f),     \
      go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.4f), act(actPotToLap, 0.9f)
#define LUNCH_END                                                                                   \
  still(0.6f), act(actPotUp, 0.9f), pose(POSE_STAND, 0.8f, 0.2f), PACK_CROUCH, doit(doIntoPack),    \
      pose(POSE_STAND, 0.6f, 0.2f), TO_SIT

// Lunch a: a bee comes for it; a swat; it comes back; he eats faster; it goes.
const Step kLunchA[] = {
    LUNCH_START,
    doit(doBeeIn),
    act(actEatA, kEatA),
    until(beeHere),
    act(actSwat, 1.1f),
    act(actEatB, kEatB),
    until(beeBack),
    act(actGlare, 1.4f),
    act(actEatFast, kEatFast),
    doit(doBeeOff),
    still(1.0f),
    LUNCH_END,
};
// Lunch b: a chipmunk edges in a hop a bite, gets a crumb by the pot, runs.
const Step kLunchB[] = {
    LUNCH_START,
    doit(doChipIn),
    act(actEatC, kEatC),
    until(chipGrabbed),
    act(actWatchChip, 2.2f),
    act(actShrugSit, 1.4f),
    act(actEatLast, kEatLast),
    until(critGone),
    LUNCH_END,
};

// Wade, shared start: to the bank, sits, boots off (down beside him), up, in.
// Shared end: back to the bank, out, boots on, back to the fire.
#define WADE_START                                                                                     \
  FROM_REST, go(AT_FAM0, kStroll), face(LEFT), pose(POSE_SIT, 0.9f, 0.3f), act(actBootsOff, 2.8f),     \
      pose(POSE_STAND, 0.8f, 0.2f), face(RIGHT), doit(doWadeOn), still(0.3f)
#define WADE_END                                                                                       \
  face(LEFT), walk(AT_FAM0, kWade), doit(doWadeOff), pose(POSE_SIT, 0.9f, 0.2f), act(actBootsOn, 2.6f), \
      pose(POSE_STAND, 0.8f, 0.2f), TO_SIT

// Wade a: a fish circles his feet; he watches, bends, grabs: it is gone.
const Step kWadeA[] = {
    WADE_START,
    walk(AT_FAM1, kWade),
    still(0.8f),
    doit(doFishIn),
    act(actWatchFish, kWatchFishS),
    pose(POSE_CROUCH, 1.6f, 0.4f),
    act(actGrabFish, 1.2f),
    pose(POSE_STAND, 0.7f, 0.1f),
    act(actShakeHands, 1.4f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.2f),
    pose(POSE_STAND, 0.5f, 0.1f),
    WADE_END,
};
// Wade b: he slips and sits down in the water; sits there a moment; a shrug.
const Step kWadeB[] = {
    WADE_START,
    walk(AT_FAM1, kWade, -24.f),
    act(actSlip, 1.8f),
    still(2.6f),
    act(actShrugSit, 1.6f),
    still(1.0f),
    pose(POSE_STAND, 1.1f, 0.3f),
    WADE_END,
};
// Wade c: stones from midstream: the first goes plop, the second skips.
const Step kWadeC[] = {
    WADE_START,
    walk(AT_FAM1, kWade),
    pose(POSE_CROUCH, 0.6f, 0.1f),
    act(actPickStone, 1.0f),
    pose(POSE_STAND, 0.6f, 0.1f),
    act(actPlop, kSkipS),
    act(actSigh, 1.2f),
    pose(POSE_CROUCH, 0.6f, 0.1f),
    act(actPickStone, 1.0f),
    pose(POSE_STAND, 0.6f, 0.1f),
    act(actSkip, kSkipS),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.0f),
    pose(POSE_STAND, 0.5f, 0.1f),
    WADE_END,
};

// Nap, shared start: in the shade of the camp pine, sits, lies back, the hat
// down over his face. Shared end: a stretch, the hat on, up, back to the fire.
#define NAP_START                                                                                     \
  FROM_REST, go(AT_FAM0, kStroll), face(LEFT), pose(POSE_SIT, 0.9f, 0.4f), pose(POSE_LIE, 1.4f, 0.5f), \
      act(actHatOverFace, 1.8f)
#define NAP_WAKE                                                                                      \
  act(actStretchLie, 2.4f), act(actHatOffFace, 1.4f), pose(POSE_SIT, 1.1f, 0.4f),                      \
      pose(POSE_STAND, 0.9f, 0.3f), TO_SIT

// Nap a: a chipmunk raids the pack by the tent and is off with a bar; he sleeps on.
const Step kNapA[] = {
    NAP_START,
    still(3.0f),
    doit(doRaidIn),
    until(critGone),
    still(2.5f),
    NAP_WAKE,
};
// Nap b: a leaf lands on his belly; he brushes it off without waking.
const Step kNapB[] = {
    NAP_START,
    still(2.0f),
    doit(doLeafIn),
    until(leafLanded),
    still(5.0f),
    act(actBrush, 2.4f),
    still(4.0f),
    NAP_WAKE,
};
// Nap c: a fly; a swat in his sleep; it is back; a bigger one knocks the hat
// off; he sits up with a start; the fly goes; the hat on.
const Step kNapC[] = {
    NAP_START,
    still(1.5f),
    doit(doFlyIn),
    until(flyCircling),
    still(2.4f),
    act(actSleepSwat, 1.2f),
    until(flyBack),
    still(1.8f),
    act(actBigSwat, 1.0f),
    act(actStartUp, 1.2f),
    act(actLookRound, 2.2f),
    until(bitGoneP),
    face(RIGHT),
    act(actHatFromGround, 1.2f),
    act(actHatUpOn, 1.2f),
    still(0.4f),
    pose(POSE_STAND, 0.9f, 0.3f),
    TO_SIT,
};

// Map, shared start: sits against the reading rock looking out; the map out
// of his pocket, unfolded. Shared end: up, back to the fire.
#define MAP_START                                                                                      \
  FROM_REST, go(AT_READ, kStroll), face(RIGHT), pose(POSE_SIT, 0.9f, 0.4f), doit(doMapOut),             \
      act(actUnfold, 1.6f)
#define MAP_END pose(POSE_STAND, 0.9f, 0.3f), TO_SIT

// Map a: turned round twice, a scratch of the head, a look at the ridge,
// turned again; a nod; folded away.
const Step kMapA[] = {
    MAP_START,
    act(actStudy, 3.0f),
    act(actTurn, 1.2f),
    act(actStudy, 2.0f),
    act(actTurn, 1.2f),
    act(actScratchMap, 2.4f),
    act(actLookRidge, 3.0f),
    act(actTurn, 1.2f),
    act(actStudy, 1.6f),
    act(actNodMap, 1.2f),
    act(actFold, 1.8f),
    still(0.8f),
    MAP_END,
};
// Map b: a gust nearly takes it; he grabs it back, folds it, sits on it.
const Step kMapB[] = {
    MAP_START,
    act(actStudy, 2.6f),
    act(actTurn, 1.2f),
    act(actStudy, 1.2f),
    act(actGust, kGustS),
    act(actFoldFast, 1.4f),
    act(actSitOnMap, 2.0f),
    still(2.4f),
    act(actSigh, 1.2f),
    pose(POSE_STAND, 0.9f, 0.2f),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    doit(doMapPocket),
    MAP_END,
};

// Afternoon: the book, if the read filler left it out (`at`: its right side).
#define BOOK_UP(at) skipUnless(bookDown, 4), go(at, kStroll), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.3f), doit(doBookPocket)
// Afternoon, shared end: the mug out of the pack, sits by the pit looking
// out, two sips; the mug down beside him; still (the fade comes over him).
#define MUG_SIT                                                                                         \
  PACK_CROUCH, doit(doMugUp), pose(POSE_STAND, 0.6f, 0.1f), go(AT_SIT, kStroll), face(RIGHT),             \
      pose(POSE_SIT, 0.9f, 0.4f), act(actMugLook, kMugLookS), doit(doMugDown), still(0.6f)

// Afternoon a: the pile is small: an armload of deadfall from under the pine.
const Step kAfternoonA[] = {
    FROM_REST,
    go(AT_FAM0, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    act(actGather, 2.6f),
    doit(doArmload),
    pose(POSE_CARRY, 0.6f, 0.1f),
    go(AT_WOOD_WORK, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    doit(doWoodOnPile),
    pose(POSE_STAND, 0.6f, 0.2f),
    BOOK_UP(AT_FAM1),
    MUG_SIT,
};
// Afternoon b: the pile will do: the walking stick on it, the pile squared up.
const Step kAfternoonB[] = {
    FROM_REST,
    go(AT_FAM1, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    doit(doStickUp),
    pose(POSE_STAND, 0.6f, 0.1f),
    go(AT_WOOD_WORK, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    doit(doStickOnPile),
    act(actSquare, 2.2f),
    pose(POSE_STAND, 0.6f, 0.2f),
    BOOK_UP(AT_FAM0),
    MUG_SIT,
};

#undef FROM_REST
#undef TO_SIT
#undef PACK_CROUCH
#undef RETURN_IN
#undef STICK_DOWN
#undef LUNCH_START
#undef LUNCH_END
#undef WADE_START
#undef WADE_END
#undef NAP_START
#undef NAP_WAKE
#undef MAP_START
#undef MAP_END
#undef MUG_SIT
#undef BOOK_UP

struct Script {
  const Step* steps;
  uint8_t n;
};
#define SCRIPT(a) Script{a, N(a)}
#define NO_SCRIPT Script{nullptr, 0}

const Script kScripts[MD_COUNT][3] = {
    {SCRIPT(kReturnA), SCRIPT(kReturnB), SCRIPT(kReturnC)},
    {SCRIPT(kLunchA), SCRIPT(kLunchB), NO_SCRIPT},
    {SCRIPT(kWadeA), SCRIPT(kWadeB), SCRIPT(kWadeC)},
    {SCRIPT(kNapA), SCRIPT(kNapB), SCRIPT(kNapC)},
    {SCRIPT(kMapA), SCRIPT(kMapB), NO_SCRIPT},
    {SCRIPT(kAfternoonA), SCRIPT(kAfternoonB), NO_SCRIPT},
};
#undef SCRIPT
#undef NO_SCRIPT
#undef N

// Beat seconds per variant (whole seconds: the script plus room for the
// longer walks on some seeds). Tuned with SS_SPINE_TRACE.
const uint8_t kDurations[MD_COUNT][3] = {
    {35, 40, 36},   // return (b: the limp is slow)
    {49, 52, 0},    // lunch (the pack, the pot, then the sitting)
    {41, 32, 43},   // wade
    {52, 49, 47},   // nap (the walk to the pine and back)
    {34, 30, 0},    // map
    {51, 40, 0},    // afternoon (a: the walk to the pine; the last 2.5 s fade to black)
};

const char* const kBeatNames[MD_COUNT] = {"return", "lunch", "wade", "nap", "map", "afternoon"};

// The afternoon's trip for the book the read filler leaves out (if it plays).
constexpr int kBookS = 9;
bool hasRead(const LoopPlan& p) {
  for (int i = 0; i < p.n_fillers; ++i)
    if (p.filler[i] == FILL_READ) return true;
  return false;
}

#ifdef SS_MIDDAY_TUNE   // tuning aid: every beat long enough to measure its script
int beatSeconds(const LoopPlan& p, int b) { return 120 + 0 * kDurations[b][p.fvar[b]] + 0 * kBookS * (int)hasRead(p); }
#else
int beatSeconds(const LoopPlan& p, int b) {
  return kDurations[b][p.fvar[b]] + (b == MD_AFTERNOON && hasRead(p) ? kBookS : 0);
}
#endif

// ---- beats --------------------------------------------------------------------------------

// The places the midday's beats use (SpineState::fp: AT_FAM0, AT_FAM1).
// Where he lies in the shade: left of the camp pine's trunk, his head toward it.
Vec2 napSpot(const World& w) { return Vec2{(float)w.land.camp_x - 50.f, 352.f}; }
void beatPoints(World& w, int b) {
  SpineState& sp = w.spine;
  const CampLayout& c = sp.camp;
  const Vec2 F = w.fire.pos;
  const int v = w.plan.fvar[b];
  sp.target = 0;
  sp.fp[0] = sp.fp[1] = c.sit;
  if (b == MD_RETURN) {   // where the stick goes down, behind his seat
    sp.fp[0] = Vec2{c.sit.x + 34.f, c.sit.y + 1.f};
  } else if (b == MD_WADE) {   // the bank where the boots come off; midstream
    float cy, half;
    const float x0 = F.x + 102.f, x1 = x0 + 50.f;
    sp.fp[0] = landscapeStreamSpan(w.land, x0, cy, half) ? Vec2{x0, cy - half - 1.f} : Vec2{x0, F.y + 12.f};
    sp.fp[1] = landscapeStreamSpan(w.land, x1, cy, half) ? Vec2{x1, cy + 0.1f * half} : Vec2{x1, sp.fp[0].y + 30.f};
  } else if (b == MD_NAP) {
    sp.fp[0] = napSpot(w);
  } else if (b == MD_AFTERNOON) {
    // a: deadfall under the pine, then the book. b: the walking stick's end,
    // where he picks it up, then the book (its right side, as AT_MUG has the mug).
    const Item& bk = w.items[ITEM_BOOK];
    const Vec2 book = bk.down ? Vec2{bk.pos.x + 13.f, bk.pos.y - 2.f} : c.sit;
    const Item& st = w.items[ITEM_STICK];
    const Vec2 stick = st.down ? Vec2{st.pos.x + 0.375f * kItemStickLen + 4.f, st.pos.y - 1.f} : c.sit;
    sp.fp[0] = v == 0 ? Vec2{(float)w.land.camp_x - 26.f, 353.f} : book;
    sp.fp[1] = v == 0 ? book : stick;
  }
}

// Props back where a beat must leave them (nothing in his hands, the hat on,
// the boots on, out of the water). True when anything was out of place.
bool settleProps(World& w) {
  bool bad = false;
  Hiker& h = w.hiker;
  SpineState& sp = w.spine;
  if (!h.hat_on) {
    bad = true;
    h.hat_on = true;
  }
  Item& hat = w.items[ITEM_HAT];
  if (hat.down || hat.held) {
    bad = true;
    hat.down = hat.held = false;
  }
  Item& shoe = w.items[ITEM_SHOE];
  if (shoe.down || shoe.held) {
    bad = true;
    shoe.down = shoe.held = false;
  }
  for (int i = 0; i < ITEM_COUNT; ++i) {
    Item& it = w.items[i];
    if (it.held && !it.critter) {
      bad = true;
      it.held = false;
    }
  }
  if (h.carry != CARRY_NONE) {
    bad = true;
    hikerCarry(h, CARRY_NONE);
  }
  if (w.fx.wade) {
    bad = true;
    w.fx.wade = false;
  }
  if (w.skip.t >= 0.f) {
    bad = true;
    w.skip.t = -1.f;
  }
  Glyph& g = w.major.g[0];
  if (g.kind == GL_SHAPE && (g.shape == SH_FISH || g.shape == SH_MAP)) {
    bad = true;
    g.kind = GL_NONE;
  }
  if (sp.fbit >= 0) {
    bad = true;
    freeBit(w);
  }
  w.pack.pos = sp.camp.pack;
  sp.fhold = HOLD_NONE;
  sp.fk = 0.f;
  return bad;
}

template <int B>
void beatBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  beatPoints(w, B);
  w.spine.bites = 0;
  w.spine.bite_up = false;
  const Script& s = kScripts[B][w.plan.fvar[B]];
  scriptStart(w, s.steps, s.n);
}
void beatUpdate(SceneCtx& ctx, float, float) { scriptRun(*ctx.w); }
// A beat's end: a script still running counts an overrun, and so does a
// critter of its still in the scene (sent off by the nearer edge), a bit
// still flying, or a prop out of place.
void beatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
#ifdef SS_SPINE_TRACE   // tuning aid: where each script finished in its beat
  int tb = 0;
  for (int k = 0; k < MD_COUNT; ++k)
    if (strcmp(kBeatNames[k], appBeatName(ctx.app)) == 0) tb = k;
  printf("trace midday_end %s v%d step=%u/%u feet=%.1f,%.1f ticks=%u done_at=%u frac=%.3f\n", appBeatName(ctx.app),
         (int)w.plan.fvar[tb], (unsigned)w.spine.run.i, (unsigned)w.spine.run.n, (double)w.hiker.feet.x, (double)w.hiker.feet.y,
         (unsigned)w.spine.run.beat_ticks, (unsigned)w.spine.run.done_at,
         w.spine.run.beat_ticks ? (double)w.spine.run.done_at / (double)w.spine.run.beat_ticks : 0.0);
#endif
  bool late = !scriptDone(w);
  SpineState& sp = w.spine;
  if (sp.fcrit >= 0 && !critGone(w)) {
    late = true;
    const Critter& c = w.critters[sp.fcrit];
    directorExit(w, sp.fcrit, c.feet.x < 0.5f * (float)SS_WIDTH ? -1 : 1, 90.f, CA_RUN);
  }
  if (settleProps(w)) late = true;
  if (late && sp.overruns < 255) ++sp.overruns;
  sp.run.steps = nullptr;
  sp.fstage = FS_NONE;
  w.busy = sp.fcrit >= 0 && !critGone(w);
}
// Afternoon: the script, and the frame fading to black over the last kFadeS
// seconds (1 on the beat's last tick); the script must be done (him sitting
// still) and the scene empty before the fade starts.
void afternoonUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  scriptRun(w);
  const float d = (float)beatSeconds(w.plan, MD_AFTERNOON);
  const float fade = easeInOut(window(t, 1.f - kFadeS / d, 1.f));
  const bool busy = !scriptDone(w) || directorCount(w) > 0 || w.spine.fbit >= 0;
  if (w.fade == 0.f && fade > 0.f && busy && w.spine.overruns < 255) ++w.spine.overruns;
  w.fade = fade;
}

typedef void (*BeginFn)(SceneCtx&);
const BeginFn kBegin[MD_COUNT] = {beatBegin<0>, beatBegin<1>, beatBegin<2>, beatBegin<3>, beatBegin<4>, beatBegin<5>};
static_assert(MD_COUNT == 6, "extend kBegin");

// The build beat with the camp popping in during its plants phase, as the
// morning's: the tent pitches itself with an overshoot, the pack and the wood
// pile pop up, the bag drops down its line, the cairn stacks up.
float woodSize(const LoopPlan& p) { return p.fvar[MD_AFTERNOON] == 0 ? 0.45f : 1.f; }
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
  w.items[ITEM_WOOD].size = woodSize(w.plan) * maxf(0.f, buildPlantScale(b, c.wood.x, c.wood.y));
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
  w.items[ITEM_WOOD].size = woodSize(w.plan);
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
const Beat kMiddayBuild = {"build", SS_BUILD_SECONDS, nullptr, buildUpdate, buildEnd};

// ---- plan -----------------------------------------------------------------------------------

constexpr int kAimMin = SS_LOOP_MIN_SECONDS + 10;
constexpr uint8_t kPauseMin = 8, kPauseMax = 20;

int busySeconds(const LoopPlan& p) {
  int s = (int)(SS_BUILD_SECONDS + 0.5f);
  for (int b = 0; b < MD_COUNT; ++b) s += beatSeconds(p, b);
  for (int i = 0; i < p.n_fillers; ++i) s += spineFillerSeconds(p, p.filler[i]);
  for (int i = 0; i < p.n_majors; ++i)
    s += majorSeconds(p.major[i], p.major_var[i]) + p.major_pre_s[i] + p.major_post_s[i];
  return s;
}

// Fillers after lunch and after nap (a third and a fourth: lunch, nap again).
void fillerSlots(LoopPlan& p) {
  static const uint8_t kSlots[SPINE_FILLER_MAX + 1][SPINE_FILLER_MAX] = {
      {0, 0, 0, 0},
      {MD_LUNCH, 0, 0, 0},
      {MD_LUNCH, MD_NAP, 0, 0},
      {MD_LUNCH, MD_LUNCH, MD_NAP, 0},
      {MD_LUNCH, MD_LUNCH, MD_NAP, MD_NAP}};
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) p.filler_after[i] = i < p.n_fillers ? kSlots[p.n_fillers][i] : 0;
}

const FitSpec kMiddayFit = {busySeconds, MD_COUNT - 1, fillerSlots};

}  // namespace

// ---- public ------------------------------------------------------------------------------------

const char* middayBeatName(int beat) { return beat >= 0 && beat < MD_COUNT ? kBeatNames[beat] : "?"; }

int middayVariantCount(int beat) {
  if (beat < 0 || beat >= MD_COUNT) return 0;
  int n = 0;
  while (n < 3 && kScripts[beat][n].steps) ++n;
  return n;
}

void middayPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem) {
  // One rng call per statement: argument evaluation order differs between
  // compilers, and the board must replay the PC preview.
  // Variants: a variant the last midday played is a quarter as likely.
  const bool again = mem.last_family == SF_MIDDAY && mem.loop_count > 1;
  p.n_fbeats = MD_COUNT;
  for (int b = 0; b < 8; ++b) p.fvar[b] = 0;
  for (int b = 0; b < MD_COUNT; ++b) {
    const int n = middayVariantCount(b);
    float wt[3] = {1.f, 1.f, 1.f};
    for (int k = n; k < 3; ++k) wt[k] = 0.f;
    if (again && mem.last_fvar[b] < n) wt[mem.last_fvar[b]] = 0.25f;
    const int v = rng.weighted(wt, 3);
    p.fvar[b] = (uint8_t)v;
  }
  for (uint8_t& s : p.pause_s) s = 0;
  for (int b = 0; b < MD_COUNT; ++b) {
    const uint8_t s = (uint8_t)rng.rangeInt(kPauseMin, kPauseMax);
    p.pause_s[b] = b == MD_AFTERNOON ? 0 : s;   // the loop ends with the afternoon
  }

  // Fillers: 2 to 4 of the midday's (all but mug_sunset), weighted, no
  // repeats (the length policy may drop some).
  float wt[FILL_COUNT];
  for (int f = 0; f < FILL_COUNT; ++f) wt[f] = spineFillerAllowed(SF_MIDDAY, f) ? (f == FILL_CAIRN ? 1.2f : 1.f) : 0.f;
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
  p.skip_n = rng.chance(0.35f) ? 4 : 3;
  p.steam_n = 3;

  static const MajorSlots kMiddaySlots = {SF_MIDDAY, {MD_RETURN, MD_LUNCH, MD_WADE, 0}, 1 | 2 | 4};
  majorsPlan(p, rng, in, mem, kMiddaySlots);
  // fishing_good leaves a fish roasting over the fire for the evening's
  // dinner; at midday the pit is cold and lunch is over: it is left out (the
  // pool's midday mask still has it; see the notes).
  for (int i = 0; i < p.n_majors;) {
    if (p.major[i] != MJ_FISHING_GOOD) {
      ++i;
      continue;
    }
    for (int j = i; j + 1 < p.n_majors; ++j) {
      p.major[j] = p.major[j + 1];
      p.major_after[j] = p.major_after[j + 1];
      p.major_var[j] = p.major_var[j + 1];
      p.major_pre_s[j] = p.major_pre_s[j + 1];
      p.major_post_s[j] = p.major_post_s[j + 1];
    }
    --p.n_majors;
    p.major[p.n_majors] = p.major_after[p.n_majors] = p.major_var[p.n_majors] = 0;
    p.major_pre_s[p.n_majors] = p.major_post_s[p.n_majors] = 0;
  }
  spineFitLength(p, mem, kMiddayFit);
  // Still short (few, short majors and fillers): a filler back in, if one is free.
  while (busySeconds(p) + spinePauseSeconds(p, MD_COUNT - 1) < kAimMin && p.n_fillers < SPINE_FILLER_MAX) {
    int f = -1;
    for (int k = 0; k < FILL_COUNT && f < 0; ++k) {
      if (!spineFillerAllowed(SF_MIDDAY, k)) continue;
      bool used = false;
      for (int i = 0; i < p.n_fillers; ++i) used = used || p.filler[i] == k;
      if (!used) f = k;
    }
    if (f < 0) break;
    p.filler[p.n_fillers] = (uint8_t)f;
    p.filler_pause_s[p.n_fillers] = kPauseMin;
    ++p.n_fillers;
    fillerSlots(p);
    if (busySeconds(p) + spinePauseSeconds(p, MD_COUNT - 1) > SS_LOOP_MAX_SECONDS - 10) {   // too much: back out
      --p.n_fillers;
      p.filler[p.n_fillers] = p.filler_pause_s[p.n_fillers] = 0;
      fillerSlots(p);
      break;
    }
  }
}

void middayLayout(World& w) {
  CampLayout& c = w.spine.camp;
  const LoopPlan& p = w.plan;
  SpineState& sp = w.spine;
  // The camp as he left it this morning: everything popped in by the build (campPop).
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
  // He is out on a day hike: no pack on him, out of the scene until the return.
  w.hiker.pack_on = false;
  w.fx.on = false;
  sp.fbit = -1;
  sp.fhold = HOLD_NONE;
  sp.fk = 0.f;
  w.skip.t = -1.f;
  w.memory.cairn_n = p.cairn_start;
}

void middayBuildTimeline(Timeline& tl, const LoopPlan& p) {
  tl.add(kMiddayBuild);
  for (int b = 0; b < MD_COUNT; ++b) {
    tl.add(Beat{kBeatNames[b], (float)beatSeconds(p, b), kBegin[b], b == MD_AFTERNOON ? afternoonUpdate : beatUpdate,
                beatEnd});
    if (b == MD_AFTERNOON) break;   // the loop ends here
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

uint32_t middayLoopTicks(const LoopPlan& p) {
  const uint32_t build = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  const int rest = busySeconds(p) - (int)(SS_BUILD_SECONDS + 0.5f) + spinePauseSeconds(p, MD_COUNT - 1);
  return build + (uint32_t)rest * SS_TICK_HZ;
}

int middayFormatPlan(const LoopPlan& p, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int k = snprintf(buf, n, "family=midday");
  auto put = [&](const char* str) {
    if (k < 0 || (size_t)k >= n) return;
    const int m = snprintf(buf + k, n - (size_t)k, "%s", str);
    if (m > 0) k += m;
  };
  char tmp[24];
  for (int b = 0; b < MD_COUNT; ++b) {
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
  return k;
}

void middayTick(World& w) {
  SpineState& sp = w.spine;
  sp.ft += SS_TICK_DT;
  critTick(w);
  bitTick(w);
  glyphTick(w);
  holdTick(w);
}

}  // namespace ss
