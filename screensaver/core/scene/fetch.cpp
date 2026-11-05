// The camp dog and a game of fetch. See fetch.h.
//
// The dog's stage machine (MajorState::stage[0], seconds in stage st[0]) runs
// from fetchTick; the script (kFetchA2 ..) is the hiker's half. They talk
// through MajorState::cue: the script's doit() steps set the low bits, the tick
// sets the high ones (kE1: the dog has dropped the stick and is waiting; kE2 and
// kE3 as each variant needs) and the script's until() steps wait on them.
// Scratch use of MajorState, all private to this file:
//   p[0] his throw spot   p[1] where the stick is meant to land
//   p[2] the water point (variant 1)   p[3] the bank point (variant 1)
//   count   throws released so far   mode   the stick glyph's index (255 none)
//   stage[2] / st[2]   the stick's state (kS*) and seconds in it
//   c[2]   the GL_WATER patch's glyph index (-1 none)   c[1] the second dog
//   v0   the stick's launch point   u0 / u1   the release time / flight time
//   mt   spray accumulator
// No random draws: variation is the plan's byte, the layout, and hash01.
#include "fetch.h"

#include <math.h>

#include "../anim/ease.h"
#include "director.h"
#include "landscape.h"
#include "memory.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline Vec2 mixV(Vec2 a, Vec2 b, float t) { return Vec2{lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }

// A repeatable 0..1 from two integers (no rng inside a beat).
float hash01(uint32_t a, uint32_t b) {
  uint32_t x = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
  x ^= x >> 15;
  x *= 0x2C1B3C6Du;
  x ^= x >> 12;
  x *= 0x297A2D39u;
  x ^= x >> 15;
  return (float)(x >> 8) * (1.f / 16777216.f);
}

// Cue bits (the script sets 1..8, the tick sets 256 up; 32..128 are flags).
constexpr uint16_t kGo = 1, kC1 = 2, kC2 = 4, kC3 = 8;       // go / he winds up / he has thrown / the dog may leave
constexpr uint16_t kSpawned = 32, kReleased = 64, kFail = 128;
constexpr uint16_t kE1 = 256, kE2 = 512, kE3 = 1024;

enum DogStage : uint8_t {
  ST_IN = 0,      // trotting to a drop spot with the stick (or a branch)
  ST_DIP,         // head down: it lets go of the stick
  ST_BOW,         // the play-bow, tail going, waiting for him to wind up
  ST_TURN,        // moving to the ready spot or turning to face the throw
  ST_READY,       // eyes on the stick in his hand
  ST_RUN,         // after the stick
  ST_HOP,         // the catch
  ST_LIE,         // lying beside him (the good fetch's end)
  ST_STAND,       // up again
  ST_PICK,        // head down: the stick back in its mouth
  ST_OUT,         // off to the left
  // variant 1: the stream
  ST_B_WATCH = 20, ST_B_LOOK, ST_B_LOOK2, ST_B_TURN, ST_B_WADE_IN, ST_B_DIP, ST_B_WADE_OUT, ST_B_TROT, ST_B_SHAKE, ST_B_DROP, ST_B_SIT,
  // variant 2: the wrong stick
  ST_C_SNIFF = 40, ST_C_AWAY, ST_C_EMPTY, ST_C_IN, ST_C_HERE, ST_C_SIT
};

// The stick glyph's states (MajorState::stage[2]).
enum StickState : uint8_t { KS_NONE = 0, KS_LYING, KS_FLYING, KS_BOUNCING, KS_FLOATING, KS_DROPPING };

constexpr float kStickLen = 24.f;     // px at glyph scale 1
constexpr float kRingLife = 1.6f;
constexpr float kBounceS = 0.5f;      // seconds the stick bounces on after it lands
constexpr float kBounceH = 26.f;      // PROPOSED: high enough for the dog to snatch it out of the air
constexpr float kBounceDx = 12.f;
constexpr float kFlightRev = 1.3f;    // turns a second it tumbles
constexpr float kSprayG = 340.f;      // px/s^2 on the spray
constexpr float kSprayLife = 0.7f;

MajorState& M(World& w) { return w.major; }
const MajorState& M(const World& w) { return w.major; }
inline void fire(MajorState& m, uint16_t bit) { m.cue = (uint16_t)(m.cue | bit); }
inline void clear(MajorState& m, uint16_t bits) { m.cue = (uint16_t)(m.cue & ~bits); }
inline bool has(const MajorState& m, uint16_t bit) { return (m.cue & bit) != 0; }

uint8_t variantOf(const MajorState& m) { return (uint8_t)(m.var & 3u); }
bool own(const World& w, int k) { return M(w).c[k] >= 0 && directorOwns(w, M(w).c[k], CT_MAJOR); }
Critter& cr(World& w, int k) { return w.critters[M(w).c[k]]; }
void stageTo(World& w, int k, int st) {
  M(w).stage[k] = (uint8_t)st;
  M(w).st[k] = 0.f;
}
bool arrived(World& w, int k) { return critterArrived(cr(w, k)); }
// critterMoveTo scales its speed by the dog's own dial (0.8..1.2): undo it, so
// a speed is a speed whatever the loop's persona.
float vArg(const Critter& c, float v) { return v / (0.8f + 0.4f * c.speed_dial); }
void dogWalk(World& w, int k, Vec2 to, float v, CritterAction gait) {
  directorWalk(w, M(w).c[k], to, vArg(cr(w, k), v), gait);
}
void exitTo(World& w, int k, int side, float v, CritterAction gait) {
  directorExit(w, M(w).c[k], side, vArg(cr(w, k), v), gait);
}
// Turn on the spot to face `dir` (a tiny move the rig turns round for).
void faceC(World& w, int k, int dir) {
  Critter& c = cr(w, k);
  if (c.facing != dir) dogWalk(w, k, Vec2{c.feet.x + 1.5f * (float)dir, c.feet.y}, 8.f, CA_MOVE);
}

// ---- places ---------------------------------------------------------------------------------

Vec2 spot(const World& w) { return M(w).p[0]; }   // where he throws from
// Where the dog drops the stick: left of him the first time, right of him after.
Vec2 dropLeft(const World& w) { return Vec2{spot(w).x - 42.f, spot(w).y + 5.f}; }
Vec2 dropRight(const World& w) { return Vec2{spot(w).x + 48.f, scriptGroundAt(w, spot(w).x + 48.f, spot(w).y + 5.f)}; }
Vec2 readyAt(const World& w) { return Vec2{spot(w).x + 26.f, spot(w).y + 7.f}; }
Vec2 shakeAt(const World& w) { return Vec2{spot(w).x + 38.f, spot(w).y + 6.f}; }
Vec2 homeOf2(const World& w) { return Vec2{spot(w).x - 30.f, spot(w).y + 9.f}; }

// Dry ground at x for something thrown from his spot (the straight run to it stays out of the water).
float landY(const World& w, float x) {
  const Vec2 s = spot(w);
  return minf(scriptGroundAt(w, x, s.y - 12.f), directorDryY(w, Vec2{s.x + 24.f, s.y + 7.f}, x));
}
// How far right of him the n-th throw of the good fetch lands (the last is the longest).
float throwDx(uint8_t var, int n) {
  static const float k2[2] = {150.f, 260.f}, k3[3] = {145.f, 195.f, 270.f};
  const int i = n < 0 ? 0 : (n > 2 ? 2 : n);
  return (var & FETCH_THREE) ? k3[i] : k2[i < 2 ? i : 1];
}
int totalThrows(uint8_t var) {
  if ((var & 3u) != 0) return 1;
  return (var & FETCH_THREE) ? 3 : 2;
}

// ---- glyphs -----------------------------------------------------------------------------------

Glyph* glyphNew(World& w, uint8_t kind, Vec2 pos, int base_y) {
  for (Glyph& g : M(w).g) {
    if (g.kind != GL_NONE) continue;
    g = Glyph{};
    g.kind = kind;
    g.pos = pos;
    g.q = pos;
    g.facing = 1;
    g.scale = 1.f;
    g.slot = ACTOR;
    g.base_y = (int16_t)base_y;
    return &g;
  }
  return nullptr;
}
Glyph* stickGlyph(World& w) {
  const int i = M(w).mode;
  if (i < 0 || i >= GLYPH_MAX) return nullptr;
  Glyph& g = M(w).g[i];
  return g.kind == GL_SHAPE && g.shape == SH_STICK ? &g : nullptr;
}
void stickState(World& w, StickState s) { stageTo(w, 2, s); }
// A stick glyph at pos (replacing the last one).
Glyph* stickNew(World& w, Vec2 pos, float rot, int base_y, StickState s) {
  if (Glyph* old = stickGlyph(w)) old->kind = GL_NONE;
  Glyph* g = glyphNew(w, GL_SHAPE, pos, base_y);
  if (!g) {
    M(w).mode = 255;
    stickState(w, KS_NONE);
    return nullptr;
  }
  g->shape = SH_STICK;
  g->slot = TRUNK;
  g->rot = rot;
  M(w).mode = (uint8_t)(g - M(w).g);
  stickState(w, s);
  return g;
}
void stickGone(World& w) {
  if (Glyph* g = stickGlyph(w)) g->kind = GL_NONE;
  M(w).mode = 255;
  stickState(w, KS_NONE);
}
void ringAt(World& w, Vec2 at, float scale, float delay) {
  if (Glyph* g = glyphNew(w, GL_RING, at, (int)at.y)) {
    g->t = delay;
    g->scale = scale;
  }
}
// n drops of water thrown up from `at`: velocity from hash01 and `seed`, to either side by `side`
// (0 both, +1 or -1 one).
void spray(World& w, Vec2 at, int n, uint32_t seed, int side, float speed) {
  for (int i = 0; i < n; ++i) {
    Glyph* g = glyphNew(w, GL_DOT, at, (int)at.y + 2);
    if (!g) return;
    const float sx = side != 0 ? (float)side : ((i & 1) ? 1.f : -1.f);
    g->slot = WATER_LIGHT;
    g->scale = 1.1f;
    g->q = Vec2{sx * speed * (0.45f + 0.8f * hash01(seed, (uint32_t)i)), -speed * (0.9f + 0.9f * hash01((uint32_t)i, seed))};
    g->rot = at.x;   // the drop's origin (a dot draws from pos alone)
    g->k = at.y;
    g->t = 0.f;
  }
}
void ageGlyphs(World& w, float dt) {
  for (Glyph& g : M(w).g) {
    if (g.kind == GL_RING) {
      g.t += dt;
      if (g.t >= kRingLife) g.kind = GL_NONE;
    } else if (g.kind == GL_DOT && g.slot == WATER_LIGHT) {
      g.t += dt;
      g.pos = Vec2{g.rot + g.q.x * g.t, g.k + g.q.y * g.t + 0.5f * kSprayG * g.t * g.t};
      if (g.t >= kSprayLife) g.kind = GL_NONE;
    }
  }
}

// The dog's wading: the stream's water redrawn over its legs below the waterline.
void wadeOn(World& w, bool on) {
  MajorState& m = M(w);
  Glyph* g = m.c[2] >= 0 ? &m.g[m.c[2]] : nullptr;
  if (g && g->kind != GL_WATER) g = nullptr;
  if (!on) {
    if (g) g->kind = GL_NONE;
    m.c[2] = -1;
    return;
  }
  if (!own(w, 0)) return;
  const Critter& d = cr(w, 0);
  if (!g) {
    g = glyphNew(w, GL_WATER, d.feet, (int)d.feet.y + 3);
    if (!g) return;
    m.c[2] = (int8_t)(g - m.g);
  }
  g->pos = Vec2{d.feet.x - 30.f, d.feet.y - 6.f};
  g->q = Vec2{d.feet.x + 30.f, d.feet.y + 8.f};
  g->base_y = (int16_t)(d.feet.y + 3.f);
}

// ---- the stick in flight ----------------------------------------------------------------------

float restRot(float rot) { return floorf(rot * 2.f + 0.5f) * 0.5f + 0.02f; }

void stickTick(World& w) {
  MajorState& m = M(w);
  Glyph* g = stickGlyph(w);
  if (!g) return;
  const float t = m.st[2];
  const Vec2 land = Vec2{m.p[1].x, m.p[1].y - 1.5f};
  const bool water = variantOf(m) == 1;
  switch (m.stage[2]) {
    case KS_FLYING: {
      const float u = clampf(t / m.u1, 0.f, 1.f);
      const float h = 46.f + 0.14f * absf(land.x - m.v0.x);
      g->pos = Vec2{lerp(m.v0.x, land.x, u), lerp(m.v0.y, land.y, u) - 4.f * h * u * (1.f - u)};
      g->rot = 0.3f + kFlightRev * t;
      g->base_y = (int16_t)(m.p[1].y + 1.f);
      if (t >= m.u1) {
        g->rot = restRot(g->rot);
        if (water) {   // into the stream: rings, a splash, then it floats
          g->pos = Vec2{land.x, land.y + 1.f};
          ringAt(w, Vec2{land.x, land.y + 2.f}, 0.7f, 0.f);
          ringAt(w, Vec2{land.x, land.y + 2.f}, 0.5f, -0.3f);
          spray(w, Vec2{land.x, land.y}, 5, 7u, 0, 55.f);
          stickState(w, KS_FLOATING);
        } else {
          stickState(w, KS_BOUNCING);
        }
      }
      break;
    }
    case KS_BOUNCING: {
      const float u = clampf(t / kBounceS, 0.f, 1.f);
      const float a = g->rot;
      g->pos = Vec2{land.x + kBounceDx * easeOutCubic(u), land.y - 4.f * kBounceH * u * (1.f - u)};
      g->rot = lerp(a, restRot(a), easeInOut(window(u, 0.4f, 1.f)) * 0.3f + 0.f);
      if (u >= 1.f) {
        g->pos = Vec2{land.x + kBounceDx, land.y};
        g->rot = restRot(g->rot);
        stickState(w, KS_LYING);
      }
      break;
    }
    case KS_FLOATING:
      g->pos.y = land.y + 1.f + 0.8f * sinT(0.45f * t);
      g->pos.x = land.x - 1.5f * t * 0.3f;
      break;
    case KS_DROPPING: {   // out of the mouth to the ground
      const float u = clampf(t / 0.2f, 0.f, 1.f);
      g->pos = Vec2{m.v0.x, lerp(m.v0.y, g->q.y, u * u)};
      if (u >= 1.f) stickState(w, KS_LYING);
      break;
    }
    default: break;
  }
}

// ---- the dog -----------------------------------------------------------------------------------

int spawnDog(World& w, int k, Vec2 feet, int facing, uint8_t tone, float scale) {
  MajorState& m = M(w);
  const int s = directorSpawn(w, CR_DOG, feet, facing);
  m.c[k] = (int8_t)s;
  stageTo(w, k, 0);
  if (s < 0) return -1;
  w.crit[s].tag = CT_MAJOR;
  w.crit[s].flags = CF_BRAVE;
  memoryApplyPersona(w.memory, w.critters[s]);
  w.critters[s].tone = tone;
  w.critters[s].scale = scale;
  return s;
}

// The wag: the dog's own clock (the beat's), faster and wider for a bouncy dog (the loop's persona:
// speed_dial high), tamer in a gait (the gait wags the tail too) and when lying.
void wagTick(World& w, int k) {
  if (!own(w, k)) return;
  Critter& c = cr(w, k);
  const float b = c.speed_dial;
  float mul = 1.f;
  switch (c.action) {
    case CA_BOW: mul = 1.4f; break;
    case CA_LIE: mul = 0.55f; break;
    case CA_MOVE: case CA_RUN: case CA_HOP: mul = 0.25f; break;
    case CA_SHAKE: mul = 0.f; break;
    default: break;
  }
  c.tail_add = (0.16f + 0.5f * b) * mul * sinT(M(w).t * (2.2f + 2.6f * b) + 0.31f * (float)k);
}

void setStickCarry(Critter& c, bool on) { c.carry = on ? (uint8_t)CC_STICK : (uint8_t)CC_NONE; }

// The dog lets go: the stick falls from its mouth to the ground.
void dropStick(World& w) {
  MajorState& m = M(w);
  Critter& d = cr(w, 0);
  setStickCarry(d, false);
  const Vec2 mouth = critterMouth(d);
  if (Glyph* g = stickNew(w, mouth, d.facing > 0 ? 0.03f : -0.03f, (int)d.feet.y + 1, KS_DROPPING)) {
    g->q = Vec2{mouth.x, d.feet.y - 1.5f};   // where it lands (a stick glyph reads only pos, rot and scale)
    m.v0 = mouth;
  }
}

// Where he is when the dog is gone to its scatter or never came: every wait passes.
void failScene(World& w) {
  MajorState& m = M(w);
  fire(m, (uint16_t)(kFail | kE1 | kE2 | kE3));
}

void dogTick(World& w, float dt);
void dog2Tick(World& w);

}  // namespace

// ---- his actions ---------------------------------------------------------------------------------

namespace {

float env(float u, float a, float b) { return scriptEnvelope(u, a, b); }

void cueGo(World& w, float) {
  fire(M(w), kGo);
  M(w).tc = M(w).t;
}
void cueWind(World& w, float) { fire(M(w), kC1); }
void cueLeave(World& w, float) { fire(M(w), kC3); }
bool evE1(const World& w) { return (M(w).cue & (kE1 | kFail)) != 0; }
bool evE2(const World& w) { return (M(w).cue & (kE2 | kFail)) != 0; }
bool isGone(const World& w) {
  for (const CritterSlot& s : w.crit)
    if (s.in_use && s.tag == CT_MAJOR) return false;
  return true;
}

// He picks the stick up off the ground in front of him: it goes into his hand as the rig's own stick.
void doTakeStick(World& w, float) {
  MajorState& m = M(w);
  stickGone(w);
  Hiker& h = w.hiker;
  hikerCarry(h, CARRY_ROD);
  h.rod_stick = true;
  h.rod_len = 0.27f;   // about 24 px: the stick's own length
  h.rod_bend = h.rod_tilt = 0.f;
  h.rod_line = h.rod_float = false;
  clear(m, kReleased);
}

// Out of his hand: the glyph from his hand, aimed at where this throw lands; its flight time from
// how far the dog has to run (about 118 px/s, so it arrives as the stick lands).
void release(World& w) {
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  const Vec2 hand = hikerHandPos(h, 0), tip = hikerRodTip(h);
  const Vec2 start = mixV(hand, tip, 0.5f);
  const int n = m.count;
  const Vec2 s = spot(w);
  Vec2 land;
  Vec2 run;
  if (variantOf(m) == 1) {
    land = m.p[2];
    run = m.p[3];
  } else {
    const float dx = variantOf(m) == 2 ? 250.f : throwDx(m.var, n);
    land = Vec2{s.x + dx, landY(w, s.x + dx)};
    run = Vec2{land.x - 9.f, land.y};
  }
  const float dog_x = own(w, 0) ? cr(w, 0).feet.x : readyAt(w).x;
  const float T = clampf(0.2f + absf(run.x - dog_x) / 118.f, 1.0f, 1.9f);
  m.u0 = m.t;
  m.u1 = T;
  m.v0 = start;
  m.p[1] = land;
  stickNew(w, start, 0.3f, (int)land.y + 1, KS_FLYING);
  hikerCarry(h, CARRY_NONE);
  h.rod_stick = false;
  m.count = (uint8_t)(n + 1);
  clear(m, kE1);   // the next wait is for the dog's return
  fire(m, (uint16_t)(kReleased | kC2));
}

// The throw: from the wind-up (POSE_CAST) he whips it forward; the stick leaves his hand a moment in.
void actThrow(World& w, float u) {
  MajorState& m = M(w);
  if (u <= 0.f) scriptSetPose(w, POSE_STAND, 0.22f);
  if (u >= 0.17f && !has(m, kReleased)) release(w);
  const float e = easeOutCubic(window(u, 0.f, 0.2f)) * (1.f - easeInOut(window(u, 0.55f, 1.f)));
  scriptSetArm(w, 0, e, 2.0f, 0.15f);
  scriptSetArm(w, 1, 0.5f * e, 1.4f, 0.4f);
  w.fx.lean = 0.12f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// A hand to his brow, following the stick.
void actWatch(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 0, e, 1.95f, 1.35f);
  w.fx.head = -0.1f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// The dog shakes off beside him: he leans away, an arm up across his face, then wipes it.
void actFlinch(World& w, float u) {
  const float lean = easeInOut(window(u, 0.04f, 0.2f)) * (1.f - easeInOut(window(u, 0.82f, 1.f)));
  const float wipe = easeInOut(window(u, 0.45f, 0.55f)) * (1.f - easeInOut(window(u, 0.85f, 0.95f)));
  w.fx.lean = -0.17f * lean;
  w.fx.head = 0.22f * lean;
  const float sweep = 0.18f * sinT(3.f * window(u, 0.5f, 0.9f));
  scriptSetArm(w, 0, lean, 2.1f + sweep * wipe, 1.55f - 0.2f * wipe);
  scriptSetArm(w, 1, 0.5f * lean, 1.0f, 1.2f);
  if (u >= 1.f) scriptFxRest(w);
}
// A pat for the dog beside him (he is crouched).
void actPat(World& w, float u) {
  const float e = env(u, 0.2f, 0.2f);
  w.fx.reach_to = 0.7f;
  w.fx.reach = e * (0.8f + 0.12f * sinT(u * 5.f));
  w.fx.head = 0.15f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// A shrug at a branch three times the dog's size: both palms up, a tilt of the head.
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
// To his throw spot; the dog trots in from the left, lets the stick go at his feet and bows.
#define INTRO STAND, go(AT_M0, 30.f), face(LEFT), still(0.4f), doit(cueGo), until(evE1), still(1.5f)
// The stick off the ground in front of him; the dog gets ready. Then the wind-up and the throw.
#define PICK_UP doit(cueWind), pose(POSE_CROUCH, 0.6f, 0.5f), doit(doTakeStick), pose(POSE_STAND, 0.6f, 0.1f), face(RIGHT), still(0.2f)
#define THROW pose(POSE_CAST, 0.7f, 0.2f), act(actThrow, 1.1f), pose(POSE_STAND, 0.4f, 0.f), act(actWatch, 3.0f)
#define CROUCH_PAT pose(POSE_CROUCH, 0.7f, 0.2f), act(actPat, 3.0f), pose(POSE_STAND, 0.6f, 0.1f)
#define LEAVE doit(cueLeave), until(isGone), still(0.4f), pose(POSE_STAND, 0.6f, 0.1f), TO_SIT

// Variant 0, two throws: the second one longer; the dog ends lying beside him.
const Step kFetchA2[] = {
    INTRO,  PICK_UP, THROW, until(evE1), still(1.0f),
    PICK_UP, THROW,  until(evE1), still(1.8f),
    CROUCH_PAT, LEAVE,
};
// Variant 0, three throws.
const Step kFetchA3[] = {
    INTRO,   PICK_UP, THROW, until(evE1), still(1.0f),
    PICK_UP, THROW,   until(evE1), still(1.0f),
    PICK_UP, THROW,   until(evE1), still(1.8f),
    CROUCH_PAT, LEAVE,
};
// Variant 1: into the stream. He waits; the dog comes back and shakes; he flinches; the dog sits.
const Step kFetchB[] = {
    INTRO, PICK_UP, THROW,
    pose(POSE_HANDS_ON_HIPS, 0.6f, 2.5f), until(evE1),
    act(actFlinch, 3.0f), until(evE2), still(1.2f),
    CROUCH_PAT, LEAVE,
};
// Variant 2: the wrong stick. The dog is off right a while; it comes back dragging a branch; a shrug; both sit.
const Step kFetchC[] = {
    INTRO, PICK_UP, THROW,
    pose(POSE_HANDS_ON_HIPS, 0.6f, 2.0f), until(evE1), still(1.0f), face(RIGHT), until(evE2),
    act(actShrug, 2.4f), pose(POSE_SIT, 0.9f, 3.2f),
    doit(cueLeave), until(isGone), still(0.4f), pose(POSE_STAND, 0.8f, 0.2f), TO_SIT,
};

#undef STAND
#undef TO_SIT
#undef INTRO
#undef PICK_UP
#undef THROW
#undef CROUCH_PAT
#undef LEAVE

}  // namespace

// ---- the dog's tick ------------------------------------------------------------------------------

namespace {

// A return trot's speed: a bouncy dog is quicker (PROPOSED).
float trotSpeed(const Critter& c) { return 54.f + 22.f * c.speed_dial; }

void leaveStage(World& w) {   // the common end: stand, take the stick, off to the left
  stageTo(w, 0, ST_STAND);
  critterSetAction(cr(w, 0), CA_IDLE, 0.4f);
}

void dogTick(World& w, float) {
  MajorState& m = M(w);
  const uint8_t var = variantOf(m);
  if (!own(w, 0)) return;
  Critter& d = cr(w, 0);
  const float t = m.st[0];
  switch (m.stage[0]) {
    case ST_IN:   // trots to the drop spot; at it, head down
      if (arrived(w, 0)) {
        d.head_add = 0.f;
        critterSetAction(d, CA_SNIFF, 0.3f);
        stageTo(w, 0, ST_DIP);
      }
      break;
    case ST_DIP:
      if (t >= 0.38f) {
        dropStick(w);
        const bool last = m.count >= totalThrows(m.var) && m.count > 0;
        if (var == 0 && last) {
          critterSetAction(d, CA_LIE, 0.8f);
          fire(m, kE1);
          stageTo(w, 0, ST_LIE);
        } else {
          critterSetAction(d, CA_BOW, 0.45f);
          fire(m, kE1);
          stageTo(w, 0, ST_BOW);
        }
      }
      break;
    case ST_BOW:   // waits, tail going, for him to pick the stick up and wind up
      if (has(m, kC1)) {
        clear(m, kC1);
        if (m.count == 0) dogWalk(w, 0, readyAt(w), 54.f, CA_MOVE);
        else faceC(w, 0, 1);
        stageTo(w, 0, ST_TURN);
      }
      break;
    case ST_TURN:
      if (arrived(w, 0)) {
        critterSetAction(d, CA_LOOK, 0.3f);
        stageTo(w, 0, ST_READY);
      }
      break;
    case ST_READY:   // the stick leaves his hand: after it
      if (has(m, kC2)) {
        clear(m, (uint16_t)(kC2 | kE1));
        const Vec2 run = var == 1 ? m.p[3] : Vec2{m.p[1].x - 9.f, m.p[1].y};
        const float v = clampf(absf(run.x - d.feet.x) / maxf(0.5f, m.u1 - 0.2f), 60.f, 170.f);
        dogWalk(w, 0, Vec2{run.x, run.y}, v, CA_RUN);
        stageTo(w, 0, ST_RUN);
      }
      break;
    case ST_RUN: {
      const float t_land = m.u0 + m.u1 - 0.02f;   // the stick touches down
      if (var == 0) {
        if ((arrived(w, 0) && m.t >= t_land) || m.t >= t_land + 0.35f) {
          critterSetAction(d, CA_HOP, 0.55f);
          stageTo(w, 0, ST_HOP);
        }
      } else if (var == 1) {   // at the bank: a skid, then it watches the stick float
        if (arrived(w, 0) && m.t >= t_land) {
          critterSetAction(d, CA_LOOK, 0.3f);
          stageTo(w, 0, ST_B_WATCH);
        }
      } else if (arrived(w, 0) && m.t >= t_land + 0.6f) {   // at the landing spot: a sniff about
        critterSetAction(d, CA_SNIFF, 0.4f);
        stageTo(w, 0, ST_C_SNIFF);
      }
      break;
    }
    case ST_HOP:   // up for it: the stick into its mouth at the top of the hop
      if (t >= 0.27f && !d.carry) {
        stickGone(w);
        setStickCarry(d, true);
      }
      if (t >= 0.58f) {
        d.head_add = 0.2f;   // proud
        dogWalk(w, 0, dropRight(w), trotSpeed(d), CA_MOVE);
        stageTo(w, 0, ST_IN);
      }
      break;
    case ST_LIE:
      if (has(m, kC3)) leaveStage(w);
      break;
    case ST_STAND:
      if (t >= 0.55f) {
        if (var == 2) {   // it keeps the branch
          exitTo(w, 0, -1, 62.f, CA_MOVE);
          stageTo(w, 0, ST_OUT);
        } else {
          critterSetAction(d, CA_SNIFF, 0.3f);
          stageTo(w, 0, ST_PICK);
        }
      }
      break;
    case ST_PICK:
      if (t >= 0.36f) {
        stickGone(w);
        setStickCarry(d, true);
        d.head_add = 0.2f;
        exitTo(w, 0, -1, 105.f, CA_RUN);
        stageTo(w, 0, ST_OUT);
      }
      break;
    // ---- variant 1: the stream ----
    case ST_B_WATCH:   // at the bank, watching the stick float; then it looks back at him
      if (t >= 0.9f) {
        faceC(w, 0, -1);
        stageTo(w, 0, ST_B_LOOK);
      }
      break;
    case ST_B_LOOK:
      if (arrived(w, 0)) {
        critterSetAction(d, CA_LOOK, 0.3f);
        stageTo(w, 0, ST_B_LOOK2);
      }
      break;
    case ST_B_LOOK2:   // a long look at him, then it turns back and goes in
      if (t >= 1.1f) {
        faceC(w, 0, 1);
        stageTo(w, 0, ST_B_TURN);
      }
      break;
    case ST_B_TURN:
      if (arrived(w, 0)) {
        wadeOn(w, true);
        dogWalk(w, 0, Vec2{m.p[2].x - 15.f, m.p[2].y + 3.f}, 34.f, CA_MOVE);
        stageTo(w, 0, ST_B_WADE_IN);
      }
      break;
    case ST_B_WADE_IN:
      wadeOn(w, true);
      if (arrived(w, 0)) {
        critterSetAction(d, CA_SNIFF, 0.3f);
        stageTo(w, 0, ST_B_DIP);
      }
      break;
    case ST_B_DIP:
      wadeOn(w, true);
      if (t >= 0.36f) {
        stickGone(w);
        setStickCarry(d, true);
        d.head_add = 0.2f;
        faceC(w, 0, -1);
        dogWalk(w, 0, Vec2{m.p[3].x - 30.f, m.p[3].y}, 36.f, CA_MOVE);
        stageTo(w, 0, ST_B_WADE_OUT);
      }
      break;
    case ST_B_WADE_OUT:
      if (arrived(w, 0)) {
        wadeOn(w, false);
        dogWalk(w, 0, shakeAt(w), trotSpeed(d), CA_MOVE);
        stageTo(w, 0, ST_B_TROT);
      } else {
        wadeOn(w, true);
      }
      break;
    case ST_B_TROT:
      if (arrived(w, 0)) {
        faceC(w, 0, -1);
        critterSetAction(d, CA_SHAKE, 0.2f);
        m.mt = 0.f;
        fire(m, kE1);
        stageTo(w, 0, ST_B_SHAKE);
      }
      break;
    case ST_B_SHAKE: {   // spray from both sides, 2.2 s
      m.mt += SS_TICK_DT;
      if (m.mt >= 0.1f && t < 2.2f) {
        m.mt = 0.f;
        const uint32_t k = (uint32_t)(t * 10.f);
        spray(w, Vec2{d.feet.x + 2.f * (float)d.facing, d.feet.y - 13.f}, 2, k, 0, 62.f);
      }
      if (t >= 2.4f) {
        critterSetAction(d, CA_SNIFF, 0.3f);
        stageTo(w, 0, ST_B_DROP);
      }
      break;
    }
    case ST_B_DROP:
      if (t >= 0.38f) {
        dropStick(w);
        critterSetAction(d, CA_SIT, 0.5f);
        fire(m, kE2);
        stageTo(w, 0, ST_B_SIT);
      }
      break;
    case ST_B_SIT:
      if (has(m, kC3)) leaveStage(w);
      break;
    // ---- variant 2: the wrong stick ----
    case ST_C_SNIFF:
      if (t >= 1.7f) {   // it has the stick; it is looking for something better
        stickGone(w);
        setStickCarry(d, true);
        d.head_add = 0.2f;
        exitTo(w, 0, 1, 120.f, CA_RUN);
        stageTo(w, 0, ST_C_AWAY);
      }
      break;
    default: break;
  }
}

// The second dog: in after the first, then it shadows it, a little behind and in front, and sits when it sits.
void dog2Tick(World& w) {
  MajorState& m = M(w);
  if (!(m.var & FETCH_SECOND)) return;
  const Vec2 s = spot(w);
  if (m.c[1] < 0 && m.stage[1] == 0) {
    if (has(m, kGo) && m.t - m.tc >= 1.6f) {
      if (spawnDog(w, 1, Vec2{-72.f, s.y + 9.f}, 1, 1, 0.8f) >= 0) {
        dogWalk(w, 1, homeOf2(w), 62.f, CA_MOVE);
        stageTo(w, 1, 1);
      } else {
        m.stage[1] = 3;
      }
    }
    return;
  }
  if (!own(w, 1)) return;
  Critter& d2 = cr(w, 1);
  if (has(m, kC3)) {   // the day's done: off left, a trot behind the first
    if (m.stage[1] != 2) {
      exitTo(w, 1, -1, 100.f, CA_RUN);
      stageTo(w, 1, 2);
    }
    return;
  }
  if (m.stage[1] != 1) return;
  Vec2 want = homeOf2(w);
  const bool lead = own(w, 0) && m.stage[0] != ST_C_AWAY && m.stage[0] != ST_C_EMPTY && m.stage[0] != ST_OUT;
  if (lead) {
    const Critter& a = cr(w, 0);
    want = Vec2{a.feet.x - 42.f * (float)a.facing, a.feet.y + 8.f};
  }
  want.x = clampf(want.x, 40.f, s.x + 290.f);
  want.y = scriptGroundAt(w, want.x, want.y);
  const float dx = want.x - d2.feet.x;
  if (critterArrived(d2)) {
    if (absf(dx) > 12.f) {
      const float v = clampf(40.f + 1.4f * absf(dx), 45.f, 130.f);
      dogWalk(w, 1, want, v, v > 90.f ? CA_RUN : CA_MOVE);
    } else if (d2.action == CA_IDLE && !d2.blending && m.st[1] > 0.6f) {
      critterSetAction(d2, CA_SIT, 0.5f);
    }
  } else if (absf(want.x - d2.move_to) > 16.f) {
    const float v = clampf(40.f + 1.4f * absf(dx), 45.f, 130.f);
    dogWalk(w, 1, want, v, v > 90.f ? CA_RUN : CA_MOVE);
  }
}

// The branch's return (variant 2): in from the right dragging it, to a spot beside him.
void branchTick(World& w) {
  MajorState& m = M(w);
  if (variantOf(m) != 2) return;
  const Vec2 s = spot(w);
  switch (m.stage[0]) {
    case ST_C_AWAY:   // out of the frame to the right: a beat of empty scene
      if (!own(w, 0)) {
        fire(m, kE1);
        stageTo(w, 0, ST_C_EMPTY);
      }
      break;
    case ST_C_EMPTY:   // then it is back
      if (m.st[0] >= 3.4f) {
        const float x = (float)SS_WIDTH + 70.f;
        const float y = scriptGroundAt(w, s.x + 60.f, s.y + 5.f);
        if (spawnDog(w, 0, Vec2{x, y}, -1, 0, 1.f) < 0) {
          failScene(w);
          return;
        }
        Critter& d = cr(w, 0);
        d.carry = CC_BRANCH;
        d.head_add = -0.1f;
        dogWalk(w, 0, Vec2{s.x + 60.f, y}, 66.f, CA_MOVE);
        stageTo(w, 0, ST_C_IN);
      }
      break;
    case ST_C_IN:
      if (own(w, 0) && arrived(w, 0)) {
        critterSetAction(cr(w, 0), CA_IDLE, 0.3f);
        fire(m, kE2);
        stageTo(w, 0, ST_C_HERE);
      }
      break;
    case ST_C_HERE:
      if (own(w, 0) && m.st[0] >= 2.2f) {
        cr(w, 0).head_add = 0.f;
        critterSetAction(cr(w, 0), CA_SIT, 0.6f);
        stageTo(w, 0, ST_C_SIT);
      }
      break;
    case ST_C_SIT:
      if (has(m, kC3) && own(w, 0)) {
        leaveStage(w);
      }
      break;
    default: break;
  }
}

}  // namespace

// ---- public ------------------------------------------------------------------------------------

uint8_t fetchPlanVar(uint32_t r, int dog_out, bool forced) {
  uint8_t v = (uint8_t)(r % 3u);
  if (v == 0 && ((r / 3u) % 2u) == 1u) v = (uint8_t)(v | FETCH_THREE);
  if (dog_out == 3) v = (uint8_t)(v | FETCH_SECOND);
  if (forced) v = (uint8_t)(v | FETCH_FORCED);
  return v;
}

// PROPOSED seconds: the longest script seen over 8 seeds a variant (a bouncy dog trots back faster than a lazy one)
// plus 4 s; tests/test_dog.cpp reads how early the script is done and wants 3 s.
int fetchSeconds(uint8_t var) {
  switch (var & 3u) {
    case 0: return (var & FETCH_THREE) ? 59 : 50;
    case 1: return 45;
    default: return 55;
  }
}

const Step* fetchScript(uint8_t var, uint8_t& n) {
  const int v = var & 3u;
  if (v == 1) {
    n = (uint8_t)(sizeof(kFetchB) / sizeof(kFetchB[0]));
    return kFetchB;
  }
  if (v == 2) {
    n = (uint8_t)(sizeof(kFetchC) / sizeof(kFetchC[0]));
    return kFetchC;
  }
  if (var & FETCH_THREE) {
    n = (uint8_t)(sizeof(kFetchA3) / sizeof(kFetchA3[0]));
    return kFetchA3;
  }
  n = (uint8_t)(sizeof(kFetchA2) / sizeof(kFetchA2[0]));
  return kFetchA2;
}

void fetchBegin(World& w) {
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  const float x = F.x + 78.f;
  m.p[0] = Vec2{x, scriptGroundAt(w, x, F.y - 10.f)};
  m.p[1] = m.p[0];
  // The water point and the bank in front of it (variant 1): the stream 135 px right of him.
  const float wx = x + 135.f;
  float cy, half;
  if (landscapeStreamSpan(w.land, wx, cy, half)) {
    m.p[2] = Vec2{wx, cy - 0.1f * half};
    m.p[3] = Vec2{wx - 50.f, cy - 1.06f * half - 3.f};
  } else {
    m.p[2] = Vec2{wx, m.p[0].y + 36.f};
    m.p[3] = Vec2{wx - 50.f, m.p[0].y + 18.f};
  }
  m.count = 0;
  m.mode = 255;
  m.c[2] = -1;
  m.mt = 0.f;
  m.stage[2] = KS_NONE;
}

void fetchTick(World& w, float dt) {
  MajorState& m = M(w);
  stickTick(w);
  ageGlyphs(w, dt);
  if (!has(m, kGo)) return;
  if (!has(m, kSpawned)) {   // the dog trots in from the left edge with a stick in its mouth
    fire(m, kSpawned);
    const Vec2 d0 = dropLeft(w);
    if (spawnDog(w, 0, Vec2{-72.f, d0.y}, 1, 0, 1.f) < 0) {
      failScene(w);
      return;
    }
    Critter& d = cr(w, 0);
    setStickCarry(d, true);
    d.head_add = 0.2f;
    dogWalk(w, 0, d0, trotSpeed(d), CA_MOVE);
    stageTo(w, 0, ST_IN);
  }
  dogTick(w, dt);
  branchTick(w);
  dog2Tick(w);
  wagTick(w, 0);
  wagTick(w, 1);
}

float fetchStickReach() { return 0.5f * kStickLen + 2.f; }

// A stick: a TRUNK capsule with a thin pale line along its upper side (ACTOR_LIGHT: pale wood by day,
// a firelit rim at night), `rot` turns, `scale` times 24 px long.
void fetchStickDraw(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal) {
  const float h = 0.5f * kStickLen * g.scale;
  const Vec2 d{cosT(g.rot) * h, sinT(g.rot) * h};
  const Vec2 up{sinT(g.rot) * 0.9f * g.scale, -cosT(g.rot) * 0.9f * g.scale};
  strokeCapsule(s, Vec2{g.pos.x - d.x, g.pos.y - d.y}, Vec2{g.pos.x + d.x, g.pos.y + d.y}, 2.4f * g.scale, pal[TRUNK], clip);
  strokeCapsule(s, Vec2{g.pos.x - d.x * 0.9f + up.x, g.pos.y - d.y * 0.9f + up.y},
                Vec2{g.pos.x + d.x * 0.9f + up.x, g.pos.y + d.y * 0.9f + up.y}, 0.9f * g.scale, pal[ACTOR_LIGHT], clip);
}

}  // namespace ss
