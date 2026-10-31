// Surprise majors. See majors.h.
//
// Each major is three things: a begin (its points, glyphs, nothing random:
// variation comes from the plan's variant byte and the layout), a script for
// the hiker (script.h), and a tick that runs beside the script every tick of
// the beat (critter stage machines through the director, glyph motion). The
// script and the tick talk through MajorState::cue: the script's doit() steps
// set the low bits (kGo, kC1..kC3) and its until() steps wait on the high
// bits the tick sets (kE1..kE3).
//
// Adding a major: an id in MajorId, a name and seconds in the tables below, a
// begin, a tick and a script, the row in kMajors, and its gating in
// majorsPlan. The test in test_scene fails if its script or critters outlast
// its beat.
#include "majors.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../anim/ease.h"
#include "../render/renderer.h"
#include "../ss_app.h"
#include "director.h"
#include "landscape.h"
#include "memory.h"
#include "script.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

constexpr float kPi = 3.14159265f;
const Rect kNoRect{0, 0, 0, 0};

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }
inline Vec2 mixV(Vec2 a, Vec2 b, float t) { return Vec2{lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
inline float dist(Vec2 a, Vec2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

// A thrown thing from a to b with its top `h` px above the straight line.
Vec2 lob(Vec2 a, Vec2 b, float h, float u) {
  return Vec2{lerp(a.x, b.x, u), lerp(a.y, b.y, u) - 4.f * h * u * (1.f - u)};
}

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

// Cue bits: the script sets kGo and kC1..kC3, the tick sets kE1..kE3.
constexpr uint16_t kGo = 1, kC1 = 2, kC2 = 4, kC3 = 8, kE1 = 256, kE2 = 512, kE3 = 1024, kSweep = 16, kFired = 32;

MajorState& M(World& w) { return w.major; }
const MajorState& M(const World& w) { return w.major; }

void cueGo(World& w, float) { M(w).cue |= kGo; M(w).tc = M(w).t; }
void cue1(World& w, float) { M(w).cue |= kC1; M(w).tc = M(w).t; }
void cue2(World& w, float) { M(w).cue |= kC2; M(w).tc = M(w).t; }
void cue3(World& w, float) { M(w).cue |= kC3; M(w).tc = M(w).t; }
bool ev1(const World& w) { return (M(w).cue & kE1) != 0; }
bool ev2(const World& w) { return (M(w).cue & kE2) != 0; }
bool ev3(const World& w) { return (M(w).cue & kE3) != 0; }
inline bool has(const MajorState& m, uint16_t bit) { return (m.cue & bit) != 0; }
inline void fire(MajorState& m, uint16_t bit) { m.cue = (uint16_t)(m.cue | bit); }

// No critter of the major's is left in the scene (fled ones included).
bool gone(const World& w) {
  for (const CritterSlot& s : w.crit)
    if (s.in_use && s.tag == CT_MAJOR) return false;
  return true;
}

// ---- glyphs ---------------------------------------------------------------------------

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

int glyphIndex(const World& w, const Glyph* g) { return g ? (int)(g - M(w).g) : -1; }
Glyph* glyphAt(World& w, int i) { return i >= 0 && i < GLYPH_MAX && M(w).g[i].kind != GL_NONE ? &M(w).g[i] : nullptr; }

// Shapes are drawn from local points: x along `facing`, turned by rot turns, times scale.
struct Xf {
  Vec2 o;
  float c, s, k, f;
  Vec2 operator()(float x, float y) const {
    const float lx = x * f * k, ly = y * k;
    return Vec2{o.x + c * lx - s * ly, o.y + s * lx + c * ly};
  }
};
Xf xfOf(const Glyph& g) { return Xf{g.pos, cosT(g.rot), sinT(g.rot), g.scale, g.facing < 0 ? -1.f : 1.f}; }

void capX(Surface& s, const Xf& X, float x0, float y0, float x1, float y1, float wd, Pixel c, const Rect& clip) {
  strokeCapsule(s, X(x0, y0), X(x1, y1), wd * X.k, c, clip);
}
void polyX(Surface& s, const Xf& X, const float (*p)[2], int n, Pixel c, const Rect& clip) {
  Vec2 v[16];
  if (n > 16) n = 16;
  for (int i = 0; i < n; ++i) v[i] = X(p[i][0], p[i][1]);
  fillPolygon(s, v, n, c, clip);
}

// Radius (local px) that holds a shape, for its bounds.
float shapeReach(uint8_t sh) {
  switch (sh) {
    case SH_TWIG: return 8.f;
    case SH_MAP: return 12.f;
    case SH_FISH: return 11.f;
    case SH_COOKIE: return 1.5f;   // times scale (the radius)
    case SH_RANGER: return 9.f;
    case SH_CONE: return 5.5f;
    case SH_RABBIT: case SH_BIRD: case SH_DOG: return 20.f;
    case SH_BEAR: return 21.f;
    case SH_ROD: return 0.5f * kItemRodLen + 3.f;
    case SH_LOG: return 23.f;
    case SH_DEADFISH: return 11.f;
    default: return 12.f;
  }
}

constexpr float kRingLife = 1.6f;
constexpr float kArcSpread = 0.1f;   // turns either side of the arcs' heading

void drawRing(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal) {
  const float u = g.t / kRingLife;
  if (g.t < 0.f || u >= 1.f) return;
  const float r = (3.f + 12.f * easeOutCubic(u)) * g.scale;
  const float th = (0.6f + 1.6f * (1.f - u)) * minf(1.f, g.scale);
  fillEllipse(s, g.pos, r, 0.32f * r, pal[WATER_LIGHT], clip);
  const float ri = r - th, ryi = 0.32f * r - 0.6f * th;
  if (ri > 0.5f && ryi > 0.4f) fillEllipse(s, g.pos, ri, ryi, pal[WATER], clip);
}

// Three nested thin arcs: the middle one's apex at pos, opening along rot.
void drawArcs(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal) {
  if (g.scale <= 0.5f || g.k <= 0.05f) return;
  const Vec2 d{cosT(g.rot), sinT(g.rot)};
  const float gap = maxf(2.2f, 0.32f * g.scale);
  const Vec2 c{g.pos.x - d.x * g.scale, g.pos.y - d.y * g.scale};   // centre of curvature
  for (int j = -1; j <= 1; ++j) {
    const float r = g.scale + (float)j * gap;
    if (r < 1.5f) continue;
    const float th = g.k * (j == 1 ? 0.8f : 1.f);
    const float spread = kArcSpread * (1.f + 0.15f * (float)j);
    Vec2 v[16];
    for (int i = 0; i < 8; ++i) {
      const float a = g.rot - spread + 2.f * spread * (float)i / 7.f;
      v[i] = Vec2{c.x + (r + 0.5f * th) * cosT(a), c.y + (r + 0.5f * th) * sinT(a)};
      v[15 - i] = Vec2{c.x + (r - 0.5f * th) * cosT(a), c.y + (r - 0.5f * th) * sinT(a)};
    }
    fillPolygon(s, v, 16, pal[(Slot)g.slot], clip);
  }
}

// The ant line: pos is the nest, q the cookie; t the leader's distance walked
// on this leg, rot the leg (0 out to the cookie, 1 home), scale the spacing,
// n ants, k what they carry home (1 crumb, 2 chunk, 3 the cookie).
Vec2 antAt(const Glyph& g, int i, bool& show, Vec2& dir) {
  const Vec2 d{g.q.x - g.pos.x, g.q.y - g.pos.y};
  const float L = maxf(1.f, sqrtf(d.x * d.x + d.y * d.y));
  const Vec2 u{d.x / L, d.y / L};
  float s = g.t - (float)i * g.scale;
  show = true;
  Vec2 p;
  if (g.rot < 0.5f) {   // out: from the nest
    if (s <= 0.f) {
      show = false;
      return g.pos;
    }
    dir = u;
    if (s >= L) {   // bunched at the cookie
      const float side = (i & 1) ? 1.f : -1.f;
      return Vec2{g.q.x - u.x * 2.f + side * 1.5f * (float)(i % 3), g.q.y + side * 1.2f};
    }
    p = Vec2{g.pos.x + u.x * s, g.pos.y + u.y * s};
  } else {              // home: from the cookie
    dir = Vec2{-u.x, -u.y};
    if (s >= L) {
      show = false;
      return g.pos;
    }
    if (s < 0.f) s = 0.f;
    p = Vec2{g.q.x - u.x * s, g.q.y - u.y * s};
  }
  const float w = 0.7f * sinT(s * 0.11f + 0.37f * (float)i);
  return Vec2{p.x - u.y * w, p.y + u.x * w};
}

void drawAnts(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal) {
  const Pixel a = pal[ACTOR];
  Vec2 cargo[3];
  bool cshow[3] = {false, false, false};
  for (int i = 0; i < g.n; ++i) {
    bool show;
    Vec2 dir{1.f, 0.f};
    const Vec2 p = antAt(g, i, show, dir);
    if (!show) continue;
    fillCircle(s, p, 0.95f, a, clip);
    fillCircle(s, Vec2{p.x + dir.x * 1.5f, p.y + dir.y * 1.5f - 0.2f}, 0.65f, a, clip);
    if (i < 3) {
      cargo[i] = p;
      cshow[i] = true;
    }
  }
  if (g.rot < 0.5f || g.k < 1.f) return;
  if (g.k < 1.5f && cshow[0]) {
    fillCircle(s, Vec2{cargo[0].x, cargo[0].y - 1.8f}, 0.85f, a, clip);
  } else if (g.k < 2.5f && cshow[0] && cshow[1]) {
    const Vec2 m = mixV(cargo[0], cargo[1], 0.5f);
    fillEllipse(s, Vec2{m.x, m.y - 2.2f}, 2.f, 1.4f, a, clip);
  } else if (g.k >= 2.5f && cshow[1] && cshow[2]) {
    const Vec2 m = mixV(cargo[1], cargo[2], 0.5f);
    fillCircle(s, Vec2{m.x, m.y - 4.2f}, 3.6f, a, clip);
    fillCircle(s, Vec2{m.x - 1.2f, m.y - 5.f}, 0.6f, pal[ACTOR_LIGHT], clip);
  }
}

// Hand shadows: a forearm from the hiker's shoulder up to the hand, then the shape.
void drawForearm(Surface& s, const Xf& X, Pixel c, const Rect& clip) { capX(s, X, -1.f, 3.f, -4.f, 18.f, 3.6f, c, clip); }

void drawShape(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal) {
  if (g.scale <= 0.05f) return;
  const Xf X = xfOf(g);
  const Pixel c = pal[(Slot)g.slot];
  switch (g.shape) {
    case SH_TWIG:
      if (g.k < 0.5f) {
        capX(s, X, -6.f, 0.f, 6.f, -1.f, 1.6f, c, clip);
      } else {
        capX(s, X, -6.5f, 0.f, -0.8f, -2.2f, 1.6f, c, clip);
        capX(s, X, 0.8f, -2.4f, 6.5f, -0.5f, 1.6f, c, clip);
      }
      break;
    case SH_MAP: {   // a sheet: folded small, opened wide; t tumbles it (squash across)
      const float hw = 2.5f + 6.5f * g.k, hh = 2.f + 3.5f * g.k;
      float sy = cosT(g.t);
      if (absf(sy) < 0.18f) sy = sy < 0.f ? -0.18f : 0.18f;
      const float p[4][2] = {{-hw, -hh * sy}, {hw, -hh * sy - 1.f * g.k}, {hw + 1.f, hh * sy}, {-hw + 1.f, hh * sy + 0.6f * g.k}};
      polyX(s, X, p, 4, c, clip);
      if (g.k > 0.4f) {   // the fold lines
        capX(s, X, 0.f, -hh * sy * 0.9f, 0.5f, hh * sy * 0.9f, 0.7f, pal[ACTOR_LIGHT], clip);
        if (g.k > 0.8f) capX(s, X, -hw * 0.85f, 0.f, hw * 0.85f, 0.f, 0.6f, pal[ACTOR_LIGHT], clip);
      }
      break;
    }
    case SH_FISH: {
      static const float body[8][2] = {{6.5f, 0.f},  {4.5f, -2.2f}, {0.f, -2.8f}, {-4.f, -1.8f},
                                       {-5.5f, 0.f}, {-4.f, 1.6f},  {0.f, 2.5f},  {4.5f, 2.f}};
      static const float tail[3][2] = {{-4.8f, 0.f}, {-9.5f, -3.6f}, {-9.f, 3.6f}};
      polyX(s, X, body, 8, c, clip);
      polyX(s, X, tail, 3, c, clip);
      break;
    }
    case SH_COOKIE:
      fillCircle(s, g.pos, g.scale, c, clip);
      if (g.scale > 2.2f) {
        fillCircle(s, Vec2{g.pos.x - 0.35f * g.scale, g.pos.y - 0.3f * g.scale}, 0.6f, pal[ACTOR_LIGHT], clip);
        fillCircle(s, Vec2{g.pos.x + 0.4f * g.scale, g.pos.y + 0.1f * g.scale}, 0.55f, pal[ACTOR_LIGHT], clip);
      }
      break;
    case SH_RANGER: {   // 8 px: body, head, hat brim, the arm out to the lantern
      const float bob = 0.4f * sinT(g.t * 2.f);
      capX(s, X, 0.f, -0.8f, 0.f, -4.8f + bob, 2.4f, c, clip);
      fillCircle(s, X(0.f, -6.3f + bob), 1.35f * g.scale, c, clip);
      capX(s, X, -1.9f, -7.3f + bob, 1.9f, -7.3f + bob, 0.9f, c, clip);
      capX(s, X, 0.4f, -4.3f + bob, 2.8f, -3.3f + bob, 0.9f, c, clip);
      break;
    }
    case SH_CONE: {
      const float a = g.t - floorf(g.t);
      const float k = absf(cosT(a));
      const float rx = 3.2f + 1.2f * (1.f - k), ry = 4.4f - 1.2f * (1.f - k);
      fillEllipse(s, Vec2{g.pos.x, g.pos.y - ry}, rx, ry, c, clip);
      break;
    }
    case SH_RABBIT: {   // a fist with two tall ears; the ears flick with t
      drawForearm(s, X, c, clip);
      fillEllipse(s, X(0.f, 0.f), 4.6f * g.scale, 3.8f * g.scale, c, clip);
      fillEllipse(s, X(4.f, 1.f), 2.4f * g.scale, 2.f * g.scale, c, clip);
      const float f = 1.5f * maxf(0.f, sinT(g.t * 2.f));
      capX(s, X, -1.5f, -2.5f, -3.5f - f, -13.f, 2.3f, c, clip);
      capX(s, X, 1.2f, -2.8f, 2.f, -13.5f + f, 2.3f, c, clip);
      break;
    }
    case SH_BIRD: {     // wings out in a V that flaps with t
      drawForearm(s, X, c, clip);
      fillEllipse(s, X(0.f, 0.f), 3.6f * g.scale, 2.4f * g.scale, c, clip);
      fillCircle(s, X(4.f, -1.5f), 1.8f * g.scale, c, clip);
      static const float beak[3][2] = {{5.4f, -2.2f}, {8.5f, -1.2f}, {5.4f, -0.6f}};
      polyX(s, X, beak, 3, c, clip);
      const float fl = 7.f * sinT(g.t * 2.f);
      const float wl[4][2] = {{-1.5f, -1.f}, {-9.f, -4.f - fl}, {-8.f, -2.f - fl}, {1.f, 0.5f}};
      const float wr[4][2] = {{0.f, -1.5f}, {-3.f, -10.f - fl}, {-1.5f, -10.f - fl}, {2.f, -0.5f}};
      polyX(s, X, wl, 4, c, clip);
      polyX(s, X, wr, 4, c, clip);
      break;
    }
    case SH_DOG: {      // long snout, the lower jaw opens by k; an ear up
      drawForearm(s, X, c, clip);
      fillEllipse(s, X(0.f, 0.f), 4.8f * g.scale, 3.8f * g.scale, c, clip);
      static const float snout[4][2] = {{2.f, -2.5f}, {10.5f, -1.2f}, {10.5f, 0.6f}, {2.5f, 1.f}};
      polyX(s, X, snout, 4, c, clip);
      const float o = 3.2f * g.k;
      const float jaw[4][2] = {{2.f, 1.5f}, {9.f, 1.6f + o}, {8.5f, 3.2f + o}, {1.5f, 3.6f}};
      polyX(s, X, jaw, 4, c, clip);
      static const float ear[3][2] = {{-2.5f, -2.f}, {-4.5f, -9.f}, {0.5f, -3.f}};
      polyX(s, X, ear, 3, c, clip);
      break;
    }
    case SH_ROD: {      // end over end: a thin line with its reel, or the stick
      const float h = 0.5f * kItemRodLen;
      if (g.k > 0.5f) {
        capX(s, X, -h, 0.f, h, 0.f, 2.6f, pal[TRUNK], clip);
      } else {
        capX(s, X, -h, 0.f, h, 0.f, 1.6f, c, clip);
        fillCircle(s, X(-0.72f * h, 0.f), 2.f * g.scale, c, clip);
      }
      break;
    }
    case SH_LOG: {      // afloat: the log, a stub of a branch, the cut end
      capX(s, X, -20.f, 0.f, 20.f, 0.f, 7.f, pal[TRUNK], clip);
      capX(s, X, 5.f, -2.f, 10.f, -8.f, 2.2f, pal[TRUNK], clip);
      fillCircle(s, X(-19.f, 0.f), 2.3f * g.scale, pal[GROUND_DARK], clip);
      break;
    }
    case SH_DEADFISH: { // belly up: the fish upside down, a pale belly line on top, an x for an eye
      static const float body[8][2] = {{6.5f, 0.f},  {4.5f, 2.2f}, {0.f, 2.8f}, {-4.f, 1.8f},
                                       {-5.5f, 0.f}, {-4.f, -1.6f}, {0.f, -2.5f}, {4.5f, -2.f}};
      static const float tail[3][2] = {{-4.8f, 0.f}, {-9.5f, 3.6f}, {-9.f, -3.6f}};
      polyX(s, X, body, 8, c, clip);
      polyX(s, X, tail, 3, c, clip);
      const Pixel lt = pal[ACTOR_LIGHT];
      capX(s, X, -3.6f, -1.6f, 3.6f, -1.4f, 1.3f, lt, clip);
      capX(s, X, 3.6f, -0.4f, 5.4f, 1.3f, 1.0f, lt, clip);
      capX(s, X, 3.6f, 1.3f, 5.4f, -0.4f, 1.0f, lt, clip);
      break;
    }
    case SH_BEAR: {     // round head, two round ears, a snout; the jaw opens by k (a roar)
      drawForearm(s, X, c, clip);
      fillCircle(s, X(0.f, 0.f), 6.f * g.scale, c, clip);
      fillCircle(s, X(-4.2f, -5.2f), 2.5f * g.scale, c, clip);
      fillCircle(s, X(3.2f, -6.f), 2.5f * g.scale, c, clip);
      fillEllipse(s, X(6.f, 0.5f), 3.8f * g.scale, 2.6f * g.scale, c, clip);
      const float o = 4.f * g.k;
      const float jaw[4][2] = {{3.f, 2.f}, {9.f, 2.5f + o}, {8.f, 4.5f + o}, {2.f, 4.5f}};
      polyX(s, X, jaw, 4, c, clip);
      break;
    }
    default: break;
  }
}

// ---- small helpers the majors share -------------------------------------------------------

int spawnC(World& w, int k, CritterKind kind, Vec2 feet, int facing, uint8_t flags) {
  MajorState& m = M(w);
  const int s = directorSpawn(w, kind, feet, facing);
  m.c[k] = (int8_t)s;
  m.stage[k] = 0;
  m.st[k] = 0.f;
  if (s < 0) return -1;
  w.crit[s].tag = CT_MAJOR;
  w.crit[s].flags = flags;
  memoryApplyPersona(w.memory, w.critters[s]);
  return s;
}
bool own(const World& w, int k) { return M(w).c[k] >= 0 && directorOwns(w, M(w).c[k], CT_MAJOR); }
Critter& cr(World& w, int k) { return w.critters[M(w).c[k]]; }
void stageTo(World& w, int k, int st) {
  M(w).stage[k] = (uint8_t)st;
  M(w).st[k] = 0.f;
}
bool arrivedC(World& w, int k) { return critterArrived(cr(w, k)); }
void walkC(World& w, int k, Vec2 to, float v, CritterAction gait) { directorWalk(w, M(w).c[k], to, v, gait); }
void exitC(World& w, int k, int side, float v, CritterAction gait) {
  directorExit(w, M(w).c[k], side, v, gait);
}
// Turn on the spot to face `dir` (a tiny move the rig turns round for).
void faceC(World& w, int k, int dir) {
  Critter& c = cr(w, k);
  if (c.facing != dir) walkC(w, k, Vec2{c.feet.x + 1.5f * (float)dir, c.feet.y}, 8.f, CA_MOVE);
}

float trunkX(const World& w) { return (float)w.land.camp_x; }
float branchY(const World& w, float x) {
  const float u = clamp01((trunkX(w) - x) / (float)LS_BRANCH_LEN);
  return lerp((float)LS_BRANCH_ROOT_Y, (float)LS_BRANCH_TIP_Y, u);
}
// Feet of a squirrel standing on the branch at x.
Vec2 onBranch(const World& w, float x) { return Vec2{x, branchY(w, x) - 1.f}; }

// Squirrel k up the camp pine and out along the bag branch to x: run in from
// the right edge, climb the trunk, out on the limb. Stages 0..3; true once there.
constexpr float kClimbFeetY = 236.f;
bool squirrelUp(World& w, int k, float x, float delay) {
  MajorState& m = M(w);
  Critter& c = cr(w, k);
  const float tx = trunkX(w);
  switch (m.stage[k]) {
    case 0:
      if (m.st[k] < delay) return false;
      walkC(w, k, Vec2{tx + 4.f, 342.f}, 105.f, CA_RUN);
      stageTo(w, k, 1);
      return false;
    case 1:
      if (!arrivedC(w, k)) return false;
      critterClimbTo(c, kClimbFeetY, 70.f);
      stageTo(w, k, 2);
      return false;
    case 2:
      if (!arrivedC(w, k) || m.st[k] < 0.5f) return false;
      // Hauls itself onto the limb and runs out along it.
      c.feet = onBranch(w, tx - 6.f);
      critterSetAction(c, CA_IDLE, 0.3f);
      walkC(w, k, onBranch(w, x), 70.f, CA_RUN);
      stageTo(w, k, 3);
      return false;
    case 3:
      if (!arrivedC(w, k)) return false;
      stageTo(w, k, 4);
      return true;
    default: return m.stage[k] == 4;
  }
}
// Back along the limb, down the trunk and off to the right. Stages 10..13.
void squirrelDown(World& w, int k) {
  MajorState& m = M(w);
  Critter& c = cr(w, k);
  const float tx = trunkX(w);
  switch (m.stage[k]) {
    case 10:
      walkC(w, k, onBranch(w, tx - 6.f), 70.f, CA_RUN);
      stageTo(w, k, 11);
      break;
    case 11:
      if (!arrivedC(w, k)) break;
      c.feet = Vec2{tx + 4.f, kClimbFeetY};
      critterClimbTo(c, 342.f, 90.f);
      stageTo(w, k, 12);
      break;
    case 12:
      if (!arrivedC(w, k)) break;
      critterSetAction(c, CA_IDLE, 0.2f);
      exitC(w, k, 1, 110.f, CA_RUN);
      stageTo(w, k, 13);
      break;
    default: break;
  }
}

// Where to stand on the far skyline: the highest point between x0 and x1
// that a ridge (not a mid pine) makes, so a tiny figure there reads against
// the sky.
Vec2 ridgeSpot(const World& w, float x0, float x1) {
  const Landscape& ls = w.land;
  float best_y = 1e9f, best_x = 0.5f * (x0 + x1);
  const float tx = (float)w.land.camp_x;
  for (float x = x0; x <= x1; x += 4.f) {
    // Not behind the camp pine or its bag branch (they stand far above the ridges).
    if (x > tx - (float)LS_BRANCH_LEN - 16.f && x < tx + 56.f) continue;
    float sky = minf(landscapeCrestAt(ls, 0, x), landscapeCrestAt(ls, 1, x));
    sky = minf(sky, landscapeCrestAt(ls, 2, x) - 22.f);   // the far tree line's tallest teeth
    bool clear = true;
    for (int i = 0; i < ls.n_mid && clear; ++i) {
      const LsPine& p = ls.mid[i];
      const float half = 0.5f * 0.01f * (float)p.width_pct * (float)p.h + 8.f;
      if (absf((float)p.x - x) < half && (float)(p.base_y - p.h) < sky + 14.f) clear = false;
    }
    if (clear && sky < best_y) {
      best_y = sky;
      best_x = x;
    }
  }
  if (best_y > 1e8f) best_y = landscapeCrestAt(ls, 0, best_x);
  return Vec2{best_x, best_y + 0.5f};
}

// The stream's centre at x (or a fallback), and the near-bank waterline.
Vec2 streamAt(const World& w, float x, float fallback_y) {
  float cy, half;
  return landscapeStreamSpan(w.land, x, cy, half) ? Vec2{x, cy} : Vec2{x, fallback_y};
}

// He is sitting at camp.sit already (the beat before left him there).
bool notSeated(const World& w) {
  const Vec2 s = w.spine.camp.sit;
  return !(w.hiker.target == POSE_SIT && absf(w.hiker.feet.x - s.x) < 3.f && absf(w.hiker.feet.y - s.y) < 3.f);
}

// ---- the hiker's actions ------------------------------------------------------------------

float env(float u, float a, float b) { return scriptEnvelope(u, a, b); }
// s: 0 -> 1 over [c, c + up], held for `hold`, back to 0 over `up`.
float plateauM(float s, float c, float up, float hold) {
  return easeInOut(window(s, c, c + up)) * (1.f - easeInOut(window(s, c + up + hold, c + 2.f * up + hold)));
}
// u passed `at` since `prev` (a one-shot inside an action or a tick).
inline bool crossed(float u, float prev, float at) { return prev < at && u >= at; }

// Laughing: three bounces of the shoulders, head thrown back, a hand slapping his knee.
void actLaugh(World& w, float u) {
  const float e = env(u, 0.1f, 0.15f);
  const float b = absf(sinT(1.5f * u));
  w.fx.lean = e * (-0.16f + 0.2f * b);
  w.fx.head = e * (-0.32f + 0.22f * b);
  w.fx.lift = e * 0.02f * b;
  scriptSetArm(w, 0, e, 0.75f + 0.45f * b, 0.45f);   // slapping his knee with each bounce
  scriptSetArm(w, 1, 0.8f * e, 0.5f, 1.7f);
  if (u >= 1.f) scriptFxRest(w);
}

// Sitting, a fright: he rocks back with both arms up, then settles.
void actYelpSit(World& w, float u) {
  const float e = easeOutCubic(window(u, 0.f, 0.12f)) * (1.f - easeInOut(window(u, 0.45f, 1.f)));
  w.fx.lean = -0.32f * e;
  w.fx.head = -0.25f * e;
  scriptSetArm(w, 0, e, 2.5f, 0.5f);
  scriptSetArm(w, 1, e, 2.3f, 0.6f);
  if (u >= 1.f) scriptFxRest(w);
}

void actNodM(World& w, float u) { w.fx.head = 0.26f * (0.5f - 0.5f * cosT(2.f * u)); }

// A cone off the head: it ducks, then comes back up.
void actConk(World& w, float u) {
  w.fx.head = 0.4f * env(u, 0.12f, 0.6f);
  w.fx.lean = 0.08f * env(u, 0.12f, 0.6f);
  if (u >= 1.f) scriptFxRest(w);
}

// Looks up (head back), turns toward the pine half way, holds the look.
void actLookUpPine(World& w, float u) {
  w.fx.head = -0.42f * env(u, 0.15f, 0.15f);
  if (u >= 0.4f) w.hiker.facing = 1;
  if (u >= 1.f) scriptFxRest(w);
}

// Rubs the top of his head with the near hand.
void actRubHead(World& w, float u) {
  const float e = env(u, 0.2f, 0.2f);
  scriptSetArm(w, 0, e, 2.75f + 0.12f * sinT(u * 3.f), 2.1f);
  w.fx.head = 0.12f * e;
  if (u >= 1.f) scriptFxRest(w);
}

// ---- 1. squirrel_splash ---------------------------------------------------------------
// Two squirrels chase up the camp pine; one spins off the bag branch into the
// stream; he laughs; it climbs out onto a rock, shakes off and sits as if
// nothing happened.

constexpr float kFallG = 700.f;

void beginSplash(World& w) {
  MajorState& m = M(w);
  // The splash point: below and left of the limb's far end, in the water.
  const float x = trunkX(w) - (float)LS_BRANCH_LEN - 10.f;
  m.p[2] = streamAt(w, x, 420.f);
  // The rock it climbs onto: the nearest one to the splash, or the far bank.
  float best = 1e9f;
  float cy, half;
  const bool span = landscapeStreamSpan(w.land, x, cy, half);
  m.p[3] = Vec2{x - 34.f, span ? cy - 1.06f * half - 4.f : 400.f};
  for (int i = 0; i < w.land.n_rocks; ++i) {
    const LsRock& r = w.land.rocks[i];
    float x0 = r.p[0].x, x1 = r.p[0].x, top = r.p[0].y;
    for (const Vec2& p : r.p) {
      x0 = minf(x0, p.x);
      x1 = maxf(x1, p.x);
      top = minf(top, p.y);
    }
    const float cx = 0.5f * (x0 + x1);
    const float d = absf(cx - x);
    if (d < 150.f && d < best && x1 - x0 > 13.f && top < 414.f && (cx < 640.f || top < 412.f)) {
      best = d;
      m.p[3] = Vec2{cx, top + 2.f};
    }
  }
}

void tickSplash(World& w, float dt) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  if (m.c[0] < 0 && m.c[1] < 0 && !has(m, kFired)) {
    const float y = scriptGroundAt(w, 840.f, 344.f);
    spawnC(w, 0, CR_SQUIRREL, Vec2{840.f, y}, -1, CF_BRAVE);
    spawnC(w, 1, CR_SQUIRREL, Vec2{845.f, y}, -1, CF_BRAVE);
    fire(m, kFired);
  }
  const float tx = trunkX(w);
  // A: up and out to the far end of the limb; turns at bay; spins off.
  if (own(w, 0)) {
    Critter& a = cr(w, 0);
    const int st = m.stage[0];
    if (st <= 4) {
      if (squirrelUp(w, 0, tx - 70.f, 0.f) && m.st[0] > 0.f && a.facing < 0) faceC(w, 0, 1);
    } else if (st == 5) {   // falling, spinning
      const float T = m.u1;
      const float u = minf(1.f, m.st[0] / T);
      const Vec2 p0 = m.p[0], p1 = m.p[2];
      const float vy0 = (p1.y - p0.y - 0.5f * kFallG * T * T) / T;
      const float tt = u * T;
      a.feet = Vec2{lerp(p0.x, p1.x, u), p0.y + vy0 * tt + 0.5f * kFallG * tt * tt};
      a.facing = ((int)(m.st[0] * 14.f) & 1) ? 1 : -1;
      if (u >= 1.f) {   // in: rings and drops, and it is under
        a.feet = Vec2{-600.f, a.feet.y};
        if (Glyph* g = glyphNew(w, GL_RING, p1, (int)p1.y)) g->t = 0.f;
        if (Glyph* g = glyphNew(w, GL_RING, p1, (int)p1.y)) g->t = -0.35f;
        for (int i = 0; i < 4; ++i) {
          Glyph* g = glyphNew(w, GL_DOT, p1, (int)p1.y + 1);
          if (!g) break;
          g->slot = WATER_LIGHT;
          g->scale = 1.1f;
          g->q = Vec2{(float)(i - 2) * 7.f + 3.5f, -18.f - 6.f * (float)(i & 1)};   // where it flies to
          g->t = 0.f;
        }
        fire(m, kE1);
        stageTo(w, 0, 6);
      }
    } else if (st == 6) {   // under; then out at the rock's foot
      if (m.st[0] > 1.1f) {
        a.feet = Vec2{m.p[3].x + 16.f, m.p[3].y + 9.f};
        a.facing = -1;
        critterSetAction(a, CA_IDLE, 0.1f);
        if (Glyph* g = glyphNew(w, GL_RING, Vec2{a.feet.x, a.feet.y}, (int)a.feet.y)) g->t = 0.f, g->scale = 0.6f;
        stageTo(w, 0, 7);
      }
    } else if (st == 7) {
      if (m.st[0] > 0.4f) {
        walkC(w, 0, m.p[3], 40.f, CA_HOP);
        stageTo(w, 0, 8);
      }
    } else if (st == 8) {
      if (arrivedC(w, 0)) {
        critterSetAction(a, CA_SHAKE, 0.3f);
        stageTo(w, 0, 9);
      }
    } else if (st == 9) {
      if (m.st[0] > 1.8f && a.action == CA_SHAKE) critterSetAction(a, CA_SIT, 0.4f);
      if (m.st[0] > 2.4f && a.action == CA_SIT) critterSetAction(a, CA_LOOK, 0.5f);
      if (m.st[0] > 4.8f) {
        exitC(w, 0, a.feet.x > 400.f ? 1 : -1, 100.f, CA_RUN);
        stageTo(w, 0, 20);
      }
    }
  }
  // B: chases A up and along the limb; lunges; looks down; goes home.
  if (own(w, 1)) {
    Critter& b = cr(w, 1);
    const int st = m.stage[1];
    if (st <= 4) {
      if (squirrelUp(w, 1, tx - 44.f, 0.7f) && own(w, 0) && m.stage[0] == 4 && m.st[1] > 0.9f) {
        critterSetAction(b, CA_HOP, 0.35f);   // the lunge
        stageTo(w, 1, 5);
        // A goes: a spin off the end of the limb.
        Critter& a = cr(w, 0);
        m.p[0] = a.feet;
        const float dy = m.p[2].y - m.p[0].y, vy0 = -90.f;
        m.u1 = (-vy0 + sqrtf(vy0 * vy0 + 2.f * kFallG * dy)) / kFallG;
        critterSetAction(a, CA_STARTLE, 0.2f);
        stageTo(w, 0, 5);
      }
    } else if (st == 5) {
      if (m.st[1] > 0.6f && b.action != CA_LOOK) {
        critterSetAction(b, CA_LOOK, 0.3f);
        b.head_add = -0.35f;   // looking down at the water
      }
      if (m.st[1] > 2.8f) {
        b.head_add = 0.f;
        stageTo(w, 1, 10);
      }
    } else {
      squirrelDown(w, 1);
    }
  }
  // The drops fly up and fall back; rings age.
  for (Glyph& g : m.g) {
    if (g.kind == GL_RING) {
      g.t += dt;
      if (g.t >= kRingLife) g.kind = GL_NONE;
    } else if (g.kind == GL_DOT && g.slot == WATER_LIGHT) {
      g.t += dt;
      const float u = g.t / 0.6f;
      g.pos = Vec2{m.p[2].x + g.q.x * u, m.p[2].y + g.q.y * 4.f * u * (1.f - u)};
      if (u >= 1.f) g.kind = GL_NONE;
    }
  }
}

#define SIT_HERE skipUnless(notSeated, 4), pose(POSE_STAND, 0.8f, 0.3f), go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.4f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
#define FROM_REST pose(POSE_STAND, 0.8f, 0.3f)

const Step kSplash[] = {
    SIT_HERE,      still(0.4f),       doit(cueGo),        still(1.5f), face(RIGHT), until(ev1),
    still(0.35f),  act(actLaugh, 2.8f), still(0.8f),      until(gone), still(0.5f), face(TO_FIRE),
    still(0.8f),
};

// ---- 2. deer_stick ---------------------------------------------------------------------
// A deer steps on a stick: a snap (a little dust); it startles; he laughs; it
// walks off with its head high.

void beginDeer(World& w) {
  MajorState& m = M(w);
  const Vec2 s = w.spine.camp.sit;
  const float x = minf(s.x + 190.f, trunkX(w) - 20.f);
  m.p[0] = Vec2{x, scriptGroundAt(w, x, s.y - 26.f)};
  if (Glyph* g = glyphNew(w, GL_SHAPE, m.p[0], (int)m.p[0].y)) {
    g->shape = SH_TWIG;
    g->slot = ROCK;   // a pale dry stick (TRUNK vanishes on the dark ground)
    g->scale = 1.25f;
    g->rot = 0.01f;
  }
}

void tickDeer(World& w, float) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  if (m.c[0] < 0 && !has(m, kFired)) {
    const float x = (float)SS_WIDTH + 50.f;
    spawnC(w, 0, CR_DEER, Vec2{x, scriptGroundAt(w, x, m.p[0].y)}, -1, CF_BRAVE);
    if (m.c[0] >= 0) walkC(w, 0, Vec2{m.p[0].x + 12.f, m.p[0].y}, 32.f, CA_MOVE);
    fire(m, kFired);
  }
  if (!own(w, 0)) return;
  Critter& d = cr(w, 0);
  switch (m.stage[0]) {
    case 0:
      if (arrivedC(w, 0)) {   // snap
        for (Glyph& g : m.g)
          if (g.kind == GL_SHAPE && g.shape == SH_TWIG) g.k = 1.f;
        const int s = worldTakeBit(w, BIT_DUST);
        if (s >= 0) {
          Bit& b = w.bits[s];
          b.pos = Vec2{m.p[0].x, m.p[0].y - 2.f};
          b.vel = Vec2{0.f, -6.f};
          b.land_y = m.p[0].y;
        }
        critterSetAction(d, CA_STARTLE, 0.45f);
        fire(m, kE1);
        stageTo(w, 0, 1);
      }
      break;
    case 1:
      if (m.st[0] > 1.1f) {
        critterSetAction(d, CA_LOOK, 0.4f);   // at him: offended
        stageTo(w, 0, 2);
      }
      break;
    case 2:
      if (m.st[0] > 3.2f) {
        critterSetAction(d, CA_HEADTURN, 0.8f);   // head up
        stageTo(w, 0, 3);
      }
      break;
    case 3:
      if (m.st[0] > 1.4f) {
        d.head_add = 0.3f;   // and keeps it up all the way out
        exitC(w, 0, 1, 40.f, CA_MOVE);
        stageTo(w, 0, 4);
      }
      break;
    default: break;
  }
}

const Step kDeer[] = {
    SIT_HERE, still(0.4f), doit(cueGo), until(ev1), still(0.5f), face(RIGHT), still(0.6f),
    act(actLaugh, 2.8f), until(gone), still(0.5f), face(TO_FIRE), still(0.6f),
};

// ---- 3. pinecone -------------------------------------------------------------------------
// A squirrel out on the bag branch flicks its cone away: it lands on the tent
// (which shakes), bounces off his head and onto the ground. He looks up.

constexpr float kConeT[4] = {1.5f, 0.75f, 0.55f, 0.3f};
constexpr float kConeH[4] = {80.f, 26.f, 16.f, 4.f};

void beginCone(World&) {}

void tickCone(World& w, float dt) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  if (m.c[0] < 0 && !has(m, kFired)) {
    spawnC(w, 0, CR_SQUIRREL, Vec2{840.f, scriptGroundAt(w, 840.f, 344.f)}, -1, CF_BRAVE);
    fire(m, kFired);
  }
  Glyph* cone = glyphAt(w, (int)m.count - 1);
  if (own(w, 0)) {
    Critter& s = cr(w, 0);
    switch (m.stage[0]) {
      case 0: case 1: case 2: case 3:
        if (squirrelUp(w, 0, trunkX(w) - 58.f, 0.f)) {
          s.carry = CC_NUT;
          critterSetAction(s, CA_SIT, 0.3f);
        }
        break;
      case 4:
        if (m.st[0] > 0.4f && s.action == CA_SIT) critterSetAction(s, CA_EAT, 0.3f);
        if (m.st[0] > 3.4f) {   // done with it: flicked away
          s.carry = CC_NONE;
          critterSetAction(s, CA_LOOK, 0.2f);
          const Vec2 mouth = critterMouth(s);
          Glyph* g = glyphNew(w, GL_SHAPE, mouth, (int)w.hiker.feet.y + 4);
          if (g) {
            g->shape = SH_CONE;
            g->slot = TRUNK;
            m.count = (uint8_t)(glyphIndex(w, g) + 1);
          }
          m.p[0] = mouth;
          m.p[1] = Vec2{w.tent.pos.x + 13.f, w.tent.pos.y - 49.f};   // the front pole's top
          m.u0 = 0.f;
          stageTo(w, 0, 5);
        }
        break;
      case 5:
        if (has(m, kE2) && m.st[0] > 0.f && m.st[0] < 20.f && ev3(w)) {
          s.head_add = -0.3f;   // peering down at him
          stageTo(w, 0, 6);
        }
        break;
      case 6:
        if (m.st[0] > 2.2f) {
          s.head_add = 0.f;
          stageTo(w, 0, 10);
        }
        break;
      default: squirrelDown(w, 0); break;
    }
  }
  // The cone's flight: to the tent, off it to his head, off that to the ground, a hop.
  if (cone && m.stage[0] >= 5) {
    m.u0 += dt;
    int seg = 0;
    float t0 = 0.f;
    while (seg < 4 && m.u0 >= t0 + kConeT[seg]) t0 += kConeT[seg++];
    const Hiker& h = w.hiker;
    if (seg >= 1 && !has(m, kE1)) {   // the tent: shakes; aim at his head now
      fire(m, kE1);
      m.u1 = m.u0;
      m.p[0] = m.p[1];
      float r = 4.f;
      const Vec2 head = hikerHeadPos(h, &r);
      m.p[1] = Vec2{head.x, head.y - r - 1.f};
    }
    if (seg >= 2 && !has(m, kE2)) {   // his head
      fire(m, kE2);
      m.p[0] = m.p[1];
      m.p[1] = Vec2{h.feet.x + 24.f, h.feet.y + 4.f};
    }
    if (seg >= 3 && !has(m, kE3)) {
      fire(m, kE3);
      m.p[0] = m.p[1];
      m.p[1] = Vec2{m.p[0].x + 7.f, m.p[0].y};
    }
    if (seg < 4) {
      const float u = (m.u0 - t0) / kConeT[seg];
      cone->pos = lob(m.p[0], m.p[1], kConeH[seg], u);
      cone->t += dt * 2.2f;
    } else {
      cone->pos = m.p[1];
      cone->t = 0.25f;   // lying on its side
    }
    // The tent shakes from the hit.
    if (has(m, kE1)) {
      const float tau = m.u0 - m.u1;
      w.tent.wobble = tau < 1.2f ? floorf(0.5f + 3.5f * expf(-3.f * tau) * sinT(3.f * tau)) : 0.f;
    }
  }
}

const Step kCone[] = {
    SIT_HERE,
    still(0.4f),
    doit(cueGo),
    until(ev2),
    act(actConk, 0.6f),
    still(0.5f),
    act(actLookUpPine, 4.2f),
    act(actRubHead, 2.4f),
    until(gone),
    still(0.5f),
    face(TO_FIRE),
    still(0.8f),
};

// ---- 4. map_chase ------------------------------------------------------------------------
// He opens a map on the reading rock; the wind takes it, tumbling off to the
// right; he runs after it out of the frame and comes back folding it.

void beginMap(World& w) {
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  m.p[0] = Vec2{F.x + 54.f, 351.f};       // crouched right of the reading rock, facing it
  m.p[1] = Vec2{(float)SS_WIDTH + 50.f, 352.f};
  m.p[2] = Vec2{F.x + 70.f, F.y - 8.f};
  m.p[3] = Vec2{F.x + 17.f, 332.f};       // the map spread on the rock's top
}

void actUnfold(World& w, float u) {
  const float e = env(u, 0.2f, 0.25f);
  scriptSetArm(w, 0, e, 1.1f, 0.5f);
  scriptSetArm(w, 1, e, 1.0f, 0.6f);
  if (Glyph* g = glyphAt(w, 0)) {
    g->k = easeInOut(window(u, 0.15f, 0.85f));
    g->pos = mixV(hikerHandsMid(w.hiker), M(w).p[3], easeInOut(window(u, 0.4f, 1.f)));
    g->t = 0.2f * easeInOut(window(u, 0.4f, 1.f));   // laid flat on the rock (seen edge-on, a sliver)
    g->base_y = (int16_t)(M(w).p[3].y + 17.f);       // just in front of the rock
  }
  if (u >= 1.f) scriptFxRest(w);
}

void actStudy(World& w, float u) {
  const float e = env(u, 0.1f, 0.1f);
  w.fx.head = 0.3f * e;
  scriptSetArm(w, 0, e, 1.0f + 0.1f * sinT(u * 3.f), 0.4f);
  if (u >= 1.f) scriptFxRest(w);
}

// The wind lifts a corner; he grabs at it and misses.
void actGrab(World& w, float u) {
  const float e = env(u, 0.2f, 0.4f);
  w.fx.reach_to = 1.45f;
  w.fx.reach = e;
  w.fx.lean = 0.16f * e;
  if (u >= 1.f) scriptFxRest(w);
}

void tickMap(World& w, float dt) {
  MajorState& m = M(w);
  if (has(m, kGo) && !has(m, kFired)) {   // out of his pocket, folded
    if (Glyph* g = glyphNew(w, GL_SHAPE, hikerHandsMid(w.hiker), (int)w.hiker.feet.y + 1)) {
      g->shape = SH_MAP;
      g->k = 0.f;
      g->rot = 0.02f;
    }
    fire(m, kFired);
  }
  Glyph* g = glyphAt(w, 0);
  if (!g) return;
  if (has(m, kC1) && !has(m, kC2)) {   // the wind has it
    const float tau = m.t - m.tc;
    if (tau < 0.6f) {
      g->rot = 0.02f - 0.1f * easeOutCubic(tau / 0.6f);
      g->t = 0.2f - 0.15f * easeOutCubic(tau / 0.6f);   // a corner lifts: it opens to the wind
      g->pos.y = m.p[3].y - 5.f * easeOutCubic(tau / 0.6f);
    } else if (g->pos.x < (float)SS_WIDTH + 30.f) {
      const float s = tau - 0.6f;
      g->pos.x += 112.f * dt;
      g->pos.y = m.p[3].y - 8.f - 4.f * s - 22.f * absf(sinT(0.85f * s));
      if (g->pos.y < 300.f) g->pos.y = 300.f;
      g->rot += 0.55f * dt;
      g->t += 1.3f * dt;
      g->base_y = (int16_t)(m.p[3].y + 20.f);
    } else {
      g->scale = 0.f;   // gone off the edge
    }
  }
  if (has(m, kC2)) {   // he has it back: folding it as he walks
    const float tau = m.t - m.tc;
    g->scale = 1.f;
    g->rot = 0.f;
    g->t = 0.f;
    g->k = 1.f - 0.25f * floorf(minf(4.f, tau / 1.8f));
    g->pos = hikerHandsMid(w.hiker);
    g->pos.y -= 2.f;
    g->base_y = (int16_t)(w.hiker.feet.y + 1.f);
    if (has(m, kC3)) g->kind = GL_NONE;   // into his pocket
  }
}

const Step kMap[] = {
    FROM_REST,
    go(AT_M0, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    doit(cueGo),
    act(actUnfold, 1.8f),
    act(actStudy, 4.6f),
    doit(cue1),
    still(0.3f),
    act(actGrab, 0.9f),
    pose(POSE_YELP, 0.25f, 0.3f),
    face(RIGHT),
    pose(POSE_STAND, 0.25f, 0.f),
    walk(AT_M1, kRun),
    still(2.2f),
    doit(cue2),
    pose(POSE_CARRY, 0.3f, 0.f),
    go(AT_M2, 40.f),
    doit(cue3),
    pose(POSE_STAND, 0.5f, 0.2f),
    TO_SIT,
};

// ---- 5. fish_leap --------------------------------------------------------------------------
// Sitting on the bank, a fish arcs out of the water right in front of him; he
// yelps. Variant 1: it lands in the fire pit and flops twice; he picks it up
// and tosses it back.

void beginFish(World& w) {
  MajorState& m = M(w);
  const Vec2 L = w.spine.camp.listen;
  float cy, half;
  const bool span = landscapeStreamSpan(w.land, L.x + 32.f, cy, half);
  m.p[0] = Vec2{L.x + 32.f, span ? cy : L.y + 12.f};               // out of the water
  m.p[1] = m.var ? Vec2{w.fire.pos.x + 2.f, w.fire.pos.y - 3.f}     // the fire pit
                 : Vec2{L.x + 76.f, span ? cy + 2.f : L.y + 14.f};  // back in
  const Vec2 C = w.spine.camp.cool;
  m.p[2] = Vec2{w.fire.pos.x + 24.f, w.fire.pos.y - 2.f};           // crouched at the pit
  m.p[3] = Vec2{C.x + 4.f, C.y - 2.f};                              // at the water to throw it
}

constexpr float kFishOutT[2] = {1.1f, 1.45f}, kFishOutH[2] = {34.f, 66.f};

Glyph* fishGlyph(World& w) {
  for (Glyph& g : M(w).g)
    if (g.kind == GL_SHAPE && g.shape == SH_FISH) return &g;
  return nullptr;
}

void ringAt(World& w, Vec2 p, float scale) {
  if (Glyph* g = glyphNew(w, GL_RING, p, (int)p.y)) {
    g->t = 0.f;
    g->scale = scale;
  }
}

void tickFish(World& w, float dt) {
  MajorState& m = M(w);
  for (Glyph& g : m.g)
    if (g.kind == GL_RING) {
      g.t += dt;
      if (g.t >= kRingLife) g.kind = GL_NONE;
    }
  if (!has(m, kGo)) return;
  Glyph* f = fishGlyph(w);
  const int v = m.var ? 1 : 0;
  if (!has(m, kFired)) {
    fire(m, kFired);
    f = glyphNew(w, GL_SHAPE, m.p[0], (int)w.hiker.feet.y + 3);
    if (f) {
      f->shape = SH_FISH;
      f->facing = m.p[1].x > m.p[0].x ? 1 : -1;
    }
    ringAt(w, m.p[0], 1.f);
    fire(m, kE1);
    m.u0 = 0.f;
    m.count = 0;
  }
  if (!f) return;
  m.u0 += dt;
  const float T = kFishOutT[v];
  if (m.count == 0) {   // the leap
    const float u = minf(1.f, m.u0 / T);
    const Vec2 a = lob(m.p[0], m.p[1], kFishOutH[v], u);
    const Vec2 b = lob(m.p[0], m.p[1], kFishOutH[v], minf(1.f, u + 0.03f));
    f->pos = a;
    // Nose along the flight (turns), in the fish's own facing frame.
    const float dx = (b.x - a.x) * (float)f->facing, dy = b.y - a.y;
    f->rot = (float)f->facing * atan2f(dy, dx > 1e-4f || dx < -1e-4f ? dx : 1e-4f) / (2.f * kPi);
    if (u >= 1.f) {
      m.count = 1;
      m.u0 = 0.f;
      if (!v) {   // back in the water
        ringAt(w, m.p[1], 1.f);
        f->kind = GL_NONE;
        fire(m, kE2);
      } else {
        f->rot = 0.f;
        f->base_y = (int16_t)(w.fire.pos.y + 6.f);   // in front of the flames
      }
    }
  } else if (m.count == 1 && v) {   // flops twice in the pit
    const float tau = m.u0;
    const float hop = tau < 0.4f ? sinT(0.5f * tau / 0.4f) : (tau > 0.9f && tau < 1.3f ? sinT(0.5f * (tau - 0.9f) / 0.4f) : 0.f);
    f->pos = Vec2{m.p[1].x, m.p[1].y - 7.f * hop};
    f->rot = 0.06f * hop * (tau < 0.6f ? 1.f : -1.f);
    if (tau > 0.6f && tau < 0.65f) f->facing = (int8_t)-f->facing;
    if (tau > 1.6f) {
      f->rot = 0.f;
      f->pos = m.p[1];
      fire(m, kE2);
      m.count = 2;
    }
  } else if (m.count == 2 && has(m, kC1)) {   // in his hands
    f->pos = hikerHandsMid(w.hiker);
    f->pos.y -= 1.f;
    f->rot = 0.f;
    f->facing = (int8_t)w.hiker.facing;
    f->base_y = (int16_t)(w.hiker.feet.y + 1.f);
    if (has(m, kC2)) {
      m.count = 3;
      m.u0 = 0.f;
      m.p[0] = f->pos;
      const Vec2 s = streamAt(w, f->pos.x + 46.f, f->pos.y + 20.f);
      m.p[1] = Vec2{s.x, s.y + 3.f};
    }
  } else if (m.count == 3) {   // tossed back
    const float u = minf(1.f, m.u0 / 0.7f);
    f->pos = lob(m.p[0], m.p[1], 24.f, u);
    f->rot = (float)f->facing * lerp(-0.08f, 0.12f, u);
    if (u >= 1.f) {
      ringAt(w, m.p[1], 1.f);
      f->kind = GL_NONE;
      m.count = 4;
      fire(m, kE3);
    }
  }
}

// Throws the fish back underarm.
void actToss(World& w, float u) {
  const float e = env(u, 0.15f, 0.3f);
  const float sw = easeInOut(window(u, 0.2f, 0.55f));
  scriptSetArm(w, 0, e, lerp(-0.5f, 1.9f, sw), 0.25f);
  w.fx.lean = 0.12f * e;
  if (u >= 0.42f && !has(M(w), kC2)) cue2(w, 1.f);
  if (u >= 1.f) scriptFxRest(w);
}

bool fishLeapt(const World& w) { return (M(w).cue & kE1) != 0 && M(w).u0 > 0.15f; }
bool fishIn(const World& w) { return M(w).var != 0; }

const Step kFish[] = {
    FROM_REST,
    go(AT_LISTEN, kStroll),
    face(RIGHT),
    pose(POSE_SIT, 0.9f, 0.8f),
    still(2.5f),
    doit(cueGo),
    until(fishLeapt),
    act(actYelpSit, 1.6f),
    until(ev2),
    still(0.6f),
    skipUnless(fishIn, 15),
    face(LEFT),
    still(1.4f),
    pose(POSE_STAND, 0.9f, 0.3f),
    go(AT_M2, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.4f),
    doit(cue1),
    pose(POSE_STAND, 0.6f, 0.2f),
    go(AT_M3, kStroll),
    face(RIGHT),
    act(actToss, 1.3f),
    until(ev3),
    still(0.6f),
    act(actNodM, 1.4f),
    still(0.4f),
    still(1.0f),
    act(actNodM, 1.4f),
    pose(POSE_STAND, 0.9f, 0.3f),
    TO_SIT,
};

// ---- 6. raccoon_pack -----------------------------------------------------------------------
// While he naps (var 0) or sleeps in the tent (var 1), a raccoon goes through
// his pack and sits eating a granola bar; he stirs; it looks up guiltily and
// waddles off. With last loop's shoe (memory) it first brings it back and
// drops it, then takes the mug or the book, whichever is down.

void beginRaccoon(World& w) {
  MajorState& m = M(w);
  const Vec2 P = w.pack.pos;
  m.p[0] = Vec2{P.x - 16.f, P.y};                  // at the pack, nose in it
  m.p[1] = Vec2{P.x - 14.f, P.y + 2.f};            // where the shoe is dropped
  m.p[2] = Vec2{m.p[1].x + 14.f, m.p[1].y - 2.f};  // he crouches there to pick it up
  m.count = 0;   // ItemId + 1 of the loot, picked when it goes for it
}

bool shoeBack(const World& w) { return M(w).u1 > 0.5f; }

void tickRaccoon(World& w, float) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  if (m.c[0] < 0 && !has(m, kFired)) {
    fire(m, kFired);
    if (spawnC(w, 0, CR_RACCOON, Vec2{-36.f, m.p[0].y}, 1, CF_BRAVE) >= 0) {
      if (w.memory.shoe_taken && m.var == 0) cr(w, 0).carry = CC_SHOE;
      walkC(w, 0, m.p[0], 38.f, CA_MOVE);
    }
  }
  if (!own(w, 0)) {
    if (has(m, kFired) && gone(w)) fire(m, kE2);
    return;
  }
  Critter& r = cr(w, 0);
  const float st = m.st[0];
  switch (m.stage[0]) {
    case 0:
      if (!arrivedC(w, 0)) break;
      if (r.carry == CC_SHOE) {   // last loop's shoe, back
        r.carry = CC_NONE;
        Item& s = w.items[ITEM_SHOE];
        s = Item{m.p[1], true, false, 1, 1.f, m.p[1].y, false, 0, 0.f, 0.f, 1.f, false};
        w.memory.shoe_taken = false;
        w.memory.shoe_back = true;   // he keeps it in the tent tonight (doShoeOff)
        m.u1 = 1.f;
      }
      critterSetAction(r, CA_SNIFF, 0.4f);
      stageTo(w, 0, 1);
      break;
    case 1:   // nose in the pack, then a paw
      if (st > 1.4f && r.action == CA_SNIFF) critterSetAction(r, CA_REACH, 0.4f);
      w.pack.pos.x = m.p[0].x + 16.f + (st > 1.6f && st < 3.2f ? (((int)(st * 6.f) & 1) ? 1.f : 0.f) : 0.f);
      if (st > 3.4f) {
        w.pack.pos.x = m.p[0].x + 16.f;
        r.carry = CC_BAR;
        critterSetAction(r, CA_SIT, 0.4f);
        stageTo(w, 0, 2);
      }
      break;
    case 2:   // eats
      if (st > 0.5f && r.action == CA_SIT) critterSetAction(r, CA_EAT, 0.3f);
      if (st > 1.5f) fire(m, kE1);
      if (m.var == 1 && st > 4.5f) {   // he stirs in the tent: the shadow sits up and turns
        w.tent.shadow_y = 0.f;
        stageTo(w, 0, 3);
      }
      if (m.var == 0 && has(m, kC2)) stageTo(w, 0, 3);
      break;
    case 3:   // caught: looks up, frozen
      if (m.var == 1) w.tent.shadow_x = st < 2.6f ? -10.f : -4.f;
      if (st > 0.25f && r.action != CA_LOOK) {
        critterSetAction(r, CA_LOOK, 0.3f);
        r.head_add = 0.15f;
      }
      if (st > 2.4f) {
        r.head_add = 0.f;
        // Takes something else with it: the mug or the book, if one is down.
        int loot = -1;
        if (m.var == 0 && m.u1 > 0.5f) {
          if (w.items[ITEM_MUG].down) loot = ITEM_MUG;
          else if (w.items[ITEM_BOOK].down) loot = ITEM_BOOK;
        }
        if (loot >= 0) {
          m.count = (uint8_t)(loot + 1);
          const Item& it = w.items[loot];
          walkC(w, 0, Vec2{it.pos.x - 12.f, it.pos.y - 1.f}, 40.f, CA_MOVE);
          stageTo(w, 0, 4);
        } else {
          exitC(w, 0, -1, 40.f, CA_MOVE);
          stageTo(w, 0, 6);
        }
      }
      break;
    case 4:
      if (arrivedC(w, 0)) {
        Item& it = w.items[m.count - 1];
        it.down = false;
        it.held = true;
        it.critter = (uint8_t)(m.c[0] + 1);
        it.angle = 0.f;
        w.memory.last_stolen = m.count;
        critterSetAction(r, CA_CARRY, 0.4f);
        stageTo(w, 0, 5);
      }
      break;
    case 5:
      if (st > 1.0f) {   // and scurries off with it
        exitC(w, 0, -1, 80.f, CA_RUN);
        stageTo(w, 0, 6);
      }
      break;
    default: break;
  }
}

void doPickShoe(World& w, float) { w.items[ITEM_SHOE].down = false; }   // back on his foot
void doPackOffM(World& w, float) { w.hiker.pack_on = false; }

const Step kRaccoonNap[] = {
    FROM_REST,
    go(AT_LIE, kStroll),
    face(RIGHT),
    doit(doPackOffM),
    still(0.4f),
    pose(POSE_SIT, 0.9f, 0.4f),
    pose(POSE_LIE, 1.3f, 1.5f),
    doit(cueGo),
    until(ev1),
    still(2.0f),
    pose(POSE_SIT, 1.1f, 0.4f),
    face(LEFT),
    still(0.5f),
    doit(cue2),
    until(ev2),
    still(0.8f),
    pose(POSE_STAND, 0.9f, 0.3f),
    skipUnless(shoeBack, 5),
    go(AT_M2, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.5f),
    doit(doPickShoe),
    pose(POSE_STAND, 0.6f, 0.2f),
    TO_SIT,
};

const Step kRaccoonTent[] = {doit(cueGo), until(ev2), still(1.0f)};

// ---- 7. ants_cookie -------------------------------------------------------------------------
// Sitting with a cookie set down in front of him, he watches a line of ants
// carry off a crumb, then a chunk, then the whole cookie.

constexpr float kAntV = 22.f, kAntGap = 6.f, kAntDwell = 1.2f;

void beginAnts(World& w) {
  MajorState& m = M(w);
  const Vec2 s = w.spine.camp.sit;
  m.p[0] = Vec2{s.x + 5.f, s.y + 16.f};             // the cookie, on the ground in front of his seat
  m.p[1] = Vec2{m.p[0].x + 58.f, m.p[0].y + 28.f};  // the nest, in the grass
  m.count = 0;   // trip
}

void doCookie(World& w, float) {
  MajorState& m = M(w);
  if (Glyph* g = glyphNew(w, GL_SHAPE, m.p[0], (int)m.p[0].y)) {
    g->shape = SH_COOKIE;
    g->scale = 4.f;
  }
}

void actCookieDown(World& w, float u) {
  const float e = env(u, 0.3f, 0.3f);
  w.fx.reach_to = 0.4f;
  w.fx.reach = e;
  w.fx.lean = 0.15f * e;
  if (u >= 0.5f && !has(M(w), kC1)) {
    cue1(w, 1.f);
    doCookie(w, 1.f);
  }
  if (u >= 1.f) scriptFxRest(w);
}

void actHeadDown(World& w, float u) { w.fx.head = 0.24f * easeInOut(u); }

// The whole cookie gone: a sitting shrug, head back up.
void actShrugSit(World& w, float u) {
  const float k = easeOutCubic(window(u, 0.f, 0.25f)) * (1.f - easeInOut(window(u, 0.6f, 1.f)));
  w.fx.head = lerp(0.24f, 0.08f, easeInOut(window(u, 0.f, 0.3f))) * (u < 0.6f ? 1.f : 1.f - easeInOut(window(u, 0.6f, 1.f)));
  w.fx.lift = 0.012f * k;
  scriptSetArm(w, 0, 0.75f * k, 0.3f, 1.55f);
  scriptSetArm(w, 1, 0.75f * k, 0.3f, 1.55f);
  if (u >= 1.f) scriptFxRest(w);
}

void tickAnts(World& w, float dt) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  Glyph* cookie = nullptr;
  Glyph* ants = nullptr;
  for (Glyph& g : m.g) {
    if (g.kind == GL_SHAPE && g.shape == SH_COOKIE) cookie = &g;
    if (g.kind == GL_ANTS) ants = &g;
  }
  if (!has(m, kFired)) {
    fire(m, kFired);
    ants = glyphNew(w, GL_ANTS, m.p[1], (int)m.p[1].y);
    if (!ants) return;
    ants->q = m.p[0];
    ants->scale = kAntGap;
    ants->n = 3;
    ants->rot = 0.f;
    ants->t = 0.f;
    ants->k = 0.f;
  }
  if (!ants) return;
  static const uint8_t kTripN[3] = {3, 5, 9};
  const float L = dist(m.p[0], m.p[1]);
  const float leg = L + (float)(ants->n - 1) * kAntGap;
  ants->t += kAntV * dt;
  if (ants->rot < 0.5f) {   // out to the cookie, then a moment there
    if (ants->t >= leg + kAntV * kAntDwell) {
      ants->rot = 1.f;
      ants->t = 0.f;
      ants->k = (float)(m.count + 1);
      if (cookie) {
        static const float kLeft[3] = {3.4f, 2.5f, 0.f};
        cookie->scale = kLeft[m.count];
        if (cookie->scale <= 0.f) cookie->kind = GL_NONE;
      }
    }
  } else if (ants->t >= leg + kAntGap) {   // all home: the next trip, or done
    ++m.count;
    if (m.count >= 3) {
      ants->kind = GL_NONE;
      fire(m, kE3);
      return;
    }
    ants->rot = 0.f;
    ants->t = 0.f;
    ants->k = 0.f;
    ants->n = kTripN[m.count];
  }
}

const Step kAnts[] = {
    SIT_HERE,
    still(0.6f),
    act(actCookieDown, 1.6f),
    still(1.6f),
    doit(cueGo),
    act(actHeadDown, 1.0f),
    until(ev3),
    still(0.8f),
    act(actShrugSit, 2.0f),
    still(1.0f),
};

// ---- 8. shadow_theater -----------------------------------------------------------------------
// In the lit tent his hands make a rabbit, a bird, a dog on the wall; a
// chipmunk and a squirrel come to watch. He makes a bear; the real bear comes
// round the tent; the watchers scatter. His shadow bear roars; the real bear
// startles and runs. He never knew.

constexpr float kShowRabbit = 1.5f, kShowBird = 9.f, kShowDog = 15.5f, kShowBear = 22.f;

void beginTheater(World& w) {
  MajorState& m = M(w);
  const Vec2 T = w.tent.pos;
  m.p[0] = Vec2{T.x + 8.f, T.y - 33.f};   // the hands on the wall
  m.p[1] = Vec2{T.x + 62.f, T.y + 16.f};  // the chipmunk's seat
  m.p[2] = Vec2{T.x + 98.f, T.y + 20.f};  // the squirrel's
  m.p[3] = Vec2{T.x - 26.f, T.y - 13.f};  // the bear, behind the tent: reared, it towers over it
  m.u0 = w.tent.shadow_x;
}

void tickTheater(World& w, float dt) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  const float t = m.t - m.tc;
  Tent& tent = w.tent;
  // His head shadow moves over to make room for the hands, and back at the end.
  const float back = has(m, kE2) ? easeInOut(minf(1.f, (t - m.u1) / 1.2f)) : 0.f;
  tent.shadow_x = floorf(0.5f + lerp(lerp(m.u0, -11.f, easeInOut(minf(1.f, t / 1.2f))), m.u0, back));
  if (!has(m, kFired) && t >= kShowRabbit) {
    fire(m, kFired);
    if (Glyph* g = glyphNew(w, GL_SHAPE, m.p[0], (int)tent.pos.y)) {
      g->shape = SH_RABBIT;
      g->scale = 0.f;
    }
  }
  Glyph* h = nullptr;
  for (Glyph& g : m.g)
    if (g.kind == GL_SHAPE && g.shape >= SH_RABBIT) h = &g;
  if (h && !has(m, kE2)) {
    // Which animal now; each change dips the hands and brings the next one up.
    const uint8_t want = t < kShowBird ? SH_RABBIT : (t < kShowDog ? SH_BIRD : (t < kShowBear ? SH_DOG : SH_BEAR));
    const float since = t - (want == SH_RABBIT ? kShowRabbit : want == SH_BIRD ? kShowBird : want == SH_DOG ? kShowDog : kShowBear);
    if (h->shape != want && since >= 0.2f) h->shape = want;
    const float big = want == SH_BEAR ? 1.22f : 1.f;
    h->scale = big * (since < 0.2f && want != SH_RABBIT ? 1.f - 0.7f * since / 0.2f : easeOutBack(minf(1.f, (since - 0.2f) / 0.35f + (want == SH_RABBIT ? 0.6f : 0.f))));
    if (want == SH_RABBIT && since < 0.35f) h->scale = easeOutBack(since / 0.35f);
    h->t += dt * (want == SH_BIRD ? 2.2f : 0.9f);
    h->pos = m.p[0];
    if (want == SH_RABBIT) h->pos.y -= 3.f * maxf(0.f, sinT(window(since, 2.f, 2.6f) * 0.5f) + sinT(window(since, 4.f, 4.6f) * 0.5f));
    if (want == SH_BIRD) h->pos = Vec2{m.p[0].x + 2.f * sinT(since * 0.3f), m.p[0].y - 2.f - 2.f * sinT(since * 0.2f)};
    if (want == SH_DOG) {   // three barks
      const float b = since - 1.5f;
      h->k = (b > 0.f && b < 0.3f) || (b > 0.8f && b < 1.1f) || (b > 2.1f && b < 2.4f) ? 1.f : 0.f;
    }
    if (want == SH_BEAR && !has(m, kE1)) h->k = 0.25f + 0.15f * sinT(since * 0.4f);   // a low growl
  }
  // The watchers.
  if (t > 2.5f && m.c[0] < 0 && !(m.cue & 64)) {
    m.cue |= 64;
    if (spawnC(w, 0, CR_CHIPMUNK, Vec2{-20.f, m.p[1].y}, 1, 0) >= 0) walkC(w, 0, m.p[1], 70.f, CA_RUN);
  }
  if (t > 4.0f && m.c[1] < 0 && !(m.cue & 128)) {
    m.cue |= 128;
    const float x = (float)SS_WIDTH + 30.f;
    if (spawnC(w, 1, CR_SQUIRREL, Vec2{x, scriptGroundAt(w, x, m.p[2].y)}, -1, 0) >= 0)
      walkC(w, 1, m.p[2], 100.f, CA_RUN);
  }
  for (int k = 0; k < 2; ++k) {
    if (!own(w, k)) continue;
    Critter& c = cr(w, k);
    if (m.stage[k] == 0 && arrivedC(w, k)) {
      faceC(w, k, k == 0 ? -1 : -1);
      stageTo(w, k, 1);
    } else if (m.stage[k] == 1 && arrivedC(w, k) && m.st[k] > 0.3f) {
      critterSetAction(c, CA_SIT, 0.35f);
      stageTo(w, k, 2);
    } else if (m.stage[k] == 2) {
      if (m.st[k] > 1.5f && c.action == CA_SIT) critterSetAction(c, CA_LOOK, 0.3f);   // rapt
      if (m.st[k] > 4.f && c.action == CA_LOOK) critterSetAction(c, CA_SIT, 0.3f);
      if (m.st[k] > 6.f) m.st[k] = 1.6f;
    }
  }
  // The bear comes round the back of the tent while his shadow bear growls.
  if (t > kShowBear + 1.f && m.c[2] < 0 && !(m.cue & kSweep)) {
    m.cue |= kSweep;
    if (spawnC(w, 2, CR_BEAR, Vec2{-70.f, m.p[3].y}, 1, CF_BRAVE) >= 0) {   // from behind the pines
      cr(w, 2).speed_dial = 0.5f;
      walkC(w, 2, m.p[3], 36.f, CA_MOVE);
    }
  }
  if (own(w, 2)) {
    Critter& b = cr(w, 2);
    switch (m.stage[2]) {
      case 0:
        if (b.visible && b.feet.x > 10.f) {
          directorScatter(w, b.feet, 700.f);   // the watchers see it first
          stageTo(w, 2, 1);
        }
        break;
      case 1:
        if (arrivedC(w, 2)) {
          critterSetAction(b, CA_REAR, 1.1f);
          stageTo(w, 2, 2);
        }
        break;
      case 2:
        if (m.st[2] > 1.6f) {   // the shadow bear roars
          fire(m, kE1);
          if (h) {
            h->k = 1.f;
            h->scale = 1.45f;
          }
          stageTo(w, 2, 3);
        }
        break;
      case 3:
        if (h) {
          h->k = m.st[2] < 1.4f ? 1.f : 0.3f;
          h->scale = m.st[2] < 1.4f ? 1.45f : 1.22f;
        }
        if (m.st[2] > 0.35f && b.action != CA_STARTLE) critterSetAction(b, CA_STARTLE, 0.3f);
        if (m.st[2] > 1.0f) {
          exitC(w, 2, -1, 115.f, CA_RUN);
          stageTo(w, 2, 4);
        }
        break;
      default: break;
    }
  }
  // Bear gone: the hands come down, his head shadow goes back.
  if (has(m, kE1) && m.c[2] >= 0 && !w.crit[m.c[2]].in_use && !has(m, kE2)) {
    fire(m, kE2);
    m.u1 = t + 1.6f;
  }
  if (has(m, kE2) && h) {
    const float d = t - m.u1;
    if (d > 0.f) h->scale = 1.22f * (1.f - minf(1.f, d / 0.4f));
    if (d >= 0.4f) h->kind = GL_NONE;
  }
  if (has(m, kE2) && t - m.u1 > 1.4f && gone(w)) fire(m, kE3);
}

const Step kTheater[] = {doit(cueGo), until(ev3), still(1.0f)};

// ---- 9. ranger_ridge ----------------------------------------------------------------------
// A tiny figure on the far ridge; its lantern blinks three times. He stops,
// waves; the lantern blinks back twice. It walks on and is gone.

constexpr float kBlinkOn = 0.4f, kBlinkOff = 0.35f;

void beginRanger(World& w) {
  MajorState& m = M(w);
  m.p[0] = w.spine.camp.view;
  m.p[0].y += 2.f;
  m.p[1] = ridgeSpot(w, 470.f, 760.f);
}

// The lantern's n blinks starting at t0: on or off at t.
bool blinkOn(float t, float t0, int n) {
  const float d = t - t0;
  if (d < 0.f) return false;
  const int i = (int)(d / (kBlinkOn + kBlinkOff));
  if (i >= n) return false;
  return d - (float)i * (kBlinkOn + kBlinkOff) < kBlinkOn;
}

void tickRanger(World& w, float dt) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  const float t = m.t;
  if (!has(m, kFired)) {
    fire(m, kFired);
    m.u0 = t;   // the figure comes up
    if (Glyph* g = glyphNew(w, GL_SHAPE, m.p[1], (int)m.p[1].y)) {
      g->shape = SH_RANGER;
      g->scale = 0.f;
      g->facing = -1;
    }
    if (Glyph* g = glyphNew(w, GL_DOT, m.p[1], (int)m.p[1].y + 1)) {
      g->slot = FIRE_CORE;
      g->scale = 0.f;
    }
  }
  Glyph* fig = glyphAt(w, 0);
  Glyph* lamp = glyphAt(w, 1);
  if (!fig || !lamp) return;
  const float up = t - m.u0;
  float k = easeOutCubic(minf(1.f, up / 1.2f));
  bool lit = blinkOn(t, m.u0 + 2.2f, 3);
  if (!has(m, kE1) && up > 2.2f + 3.f * (kBlinkOn + kBlinkOff)) fire(m, kE1);
  if (has(m, kC2)) {
    lit = blinkOn(t, m.tc + 0.8f, 2);
    const float after = t - (m.tc + 0.8f + 2.f * (kBlinkOn + kBlinkOff));
    if (after > 0.f && !has(m, kE2)) fire(m, kE2);
    if (after > 0.4f) {   // walks on along the crest with the lantern lit, and drops from sight
      lit = true;
      fig->t += dt * 0.9f;
      fig->pos.x -= 4.5f * dt;
      fig->pos.y = minf(landscapeCrestAt(w.land, 0, fig->pos.x), landscapeCrestAt(w.land, 1, fig->pos.x)) + 0.5f;
      k = 1.f - easeInOut(window(after, 3.5f, 4.5f));
      if (after > 4.6f && !has(m, kE3)) fire(m, kE3);
    }
  }
  fig->scale = k;
  lamp->pos = Vec2{fig->pos.x - 3.f * k, fig->pos.y - 3.3f * k};
  lamp->scale = lit && k > 0.5f ? 1.5f : 0.f;
}

// Hm? A small double take: head dips and comes up, a lean in.
void actDoubleTake(World& w, float u) {
  w.fx.head = 0.2f * sinT(u) * (1.f - u);
  w.fx.lean = 0.06f * env(u, 0.3f, 0.3f);
  if (u >= 1.f) scriptFxRest(w);
}

const Step kRanger[] = {
    FROM_REST,
    go(AT_M0, kStroll),
    face(RIGHT),
    pose(POSE_HANDS_ON_HIPS, 0.7f, 1.6f),
    doit(cueGo),
    until(ev1),
    still(0.3f),
    act(actDoubleTake, 1.2f),
    pose(POSE_STAND, 0.5f, 0.3f),
    pose(POSE_WAVE, 0.6f, 2.6f),
    pose(POSE_STAND, 0.6f, 0.f),
    doit(cue2),
    until(ev2),
    act(actNodM, 1.4f),
    until(ev3),
    still(0.8f),
    TO_SIT,
};

// ---- 10. echo -------------------------------------------------------------------------------
// He shouts at the valley: arcs leave him; a smaller set comes back from the
// ridge, then a tinier one; then a tiny one from a squirrel on a rock nearby.

constexpr float kEchoOutT = 1.5f, kEcho1At = 2.1f, kEcho1T = 1.4f, kEcho2At = 3.9f, kEcho2T = 1.2f,
                kEchoSqAt = 6.4f, kEchoSqT = 0.7f;

void beginEcho(World& w) {
  MajorState& m = M(w);
  m.p[0] = w.spine.camp.view;
  m.p[0].y += 2.f;
  m.p[1] = ridgeSpot(w, 520.f, 780.f);
  // The squirrel's seat: on a rock ahead of him to the right, or the ground.
  const float x = m.p[0].x + 96.f;
  m.p[2] = Vec2{x, scriptGroundAt(w, x, m.p[0].y + 6.f)};
  float best = 1e9f;
  for (int i = 0; i < w.land.n_rocks; ++i) {
    const LsRock& r = w.land.rocks[i];
    float x0 = r.p[0].x, x1 = r.p[0].x, top = r.p[0].y;
    for (const Vec2& p : r.p) {
      x0 = minf(x0, p.x);
      x1 = maxf(x1, p.x);
      top = minf(top, p.y);
    }
    const float cx = 0.5f * (x0 + x1);
    const float d = absf(cx - x);
    if (cx > m.p[0].x + 50.f && cx < 620.f && d < best && top < 405.f && top > 300.f) {
      best = d;
      m.p[2] = Vec2{cx, top + 2.f};
    }
  }
}

void actShout(World& w, float u) {
  const float e = env(u, 0.25f, 0.25f);
  scriptSetArm(w, 0, e, 2.0f, 2.3f);
  scriptSetArm(w, 1, e, 1.9f, 2.4f);
  w.fx.head = -0.18f * e;
  w.fx.lean = e * (-0.06f + 0.14f * env(window(u, 0.4f, 0.7f), 0.3f, 0.5f));
  if (u >= 0.45f && !has(M(w), kC1)) cue1(w, 1.f);
  if (u >= 1.f) scriptFxRest(w);
}

void actListenTilt(World& w, float u) {
  w.fx.head = -0.14f * env(u, 0.2f, 0.25f);
  w.fx.lean = -0.05f * env(u, 0.2f, 0.25f);
  if (u >= 1.f) scriptFxRest(w);
}

void actLookDown(World& w, float u) {
  w.fx.head = 0.3f * env(u, 0.2f, 0.3f);
  if (u >= 1.f) scriptFxRest(w);
}

// One set of arcs from a to b over `dur` from t0, size s0 -> s1.
void arcAlong(Glyph* g, float t, float t0, float dur, Vec2 a, Vec2 b, float s0, float s1, float th) {
  if (!g) return;
  const float u = (t - t0) / dur;
  if (u < 0.f || u > 1.f) {
    g->scale = 0.f;
    return;
  }
  g->pos = mixV(a, b, easeOutCubic(u));
  g->rot = atan2f(b.y - a.y, b.x - a.x) / (2.f * kPi);
  g->scale = lerp(s0, s1, u);
  g->k = th * (u < 0.75f ? 1.f : 1.f - (u - 0.75f) / 0.25f);
  g->base_y = (int16_t)(maxf(a.y, b.y) + 4.f);
}

void tickEcho(World& w, float) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  if (m.c[0] < 0 && !has(m, kFired)) {
    fire(m, kFired);
    const float x = (float)SS_WIDTH + 30.f;
    if (spawnC(w, 0, CR_SQUIRREL, Vec2{x, scriptGroundAt(w, x, m.p[2].y)}, -1, CF_BRAVE) >= 0)
      walkC(w, 0, m.p[2], 95.f, CA_RUN);
    for (int i = 0; i < 4; ++i)
      if (Glyph* g = glyphNew(w, GL_ARCS, m.p[0], (int)m.p[0].y + 4)) g->scale = 0.f;
  }
  if (own(w, 0)) {
    Critter& s = cr(w, 0);
    switch (m.stage[0]) {
      case 0:
        if (arrivedC(w, 0)) {
          faceC(w, 0, -1);
          stageTo(w, 0, 1);
        }
        break;
      case 1:
        if (arrivedC(w, 0) && m.st[0] > 0.3f) {
          critterSetAction(s, CA_SIT, 0.35f);
          fire(m, kE1);
          stageTo(w, 0, 2);
        }
        break;
      case 2:
        if (has(m, kC1) && m.t - m.tc > kEchoSqAt - 0.5f) {
          critterSetAction(s, CA_LOOK, 0.25f);   // chin up: its turn
          s.head_add = 0.2f;
          stageTo(w, 0, 3);
        }
        break;
      case 3:
        if (m.st[0] > 1.0f) s.head_add = 0.f;
        if (m.st[0] > 3.6f) {
          exitC(w, 0, 1, 100.f, CA_RUN);
          stageTo(w, 0, 4);
        }
        break;
      default: break;
    }
  }
  if (!has(m, kC1)) return;
  const float t = m.t - m.tc;
  float r = 4.f;
  const Vec2 head = hikerHeadPos(w.hiker, &r);
  const Vec2 mouth{head.x + (float)w.hiker.facing * (r + 2.f), head.y + 0.4f * r};
  const Vec2 R{m.p[1].x, m.p[1].y - 6.f};
  arcAlong(glyphAt(w, 0), t, 0.f, kEchoOutT, mouth, mixV(mouth, R, 0.55f), 6.f, 28.f, 2.f);
  arcAlong(glyphAt(w, 1), t, kEcho1At, kEcho1T, R, mixV(R, mouth, 0.75f), 12.f, 16.f, 1.6f);
  arcAlong(glyphAt(w, 2), t, kEcho2At, kEcho2T, R, mixV(R, mouth, 0.75f), 7.f, 9.f, 1.3f);
  if (m.c[0] >= 0 && w.crit[m.c[0]].in_use) {
    const Vec2 sm = critterMouth(w.critters[m.c[0]]);
    arcAlong(glyphAt(w, 3), t, kEchoSqAt, kEchoSqT, Vec2{sm.x - 3.f, sm.y - 1.f}, Vec2{sm.x - 16.f, sm.y - 5.f}, 3.f, 4.5f, 1.1f);
  }
  if (t > kEchoSqAt + kEchoSqT && !has(m, kE2)) fire(m, kE2);
}

const Step kEcho[] = {
    FROM_REST,
    go(AT_M0, kStroll),
    face(RIGHT),
    pose(POSE_STAND, 0.5f, 0.4f),
    doit(cueGo),
    until(ev1),
    still(1.2f),
    act(actShout, 1.6f),
    still(0.9f),
    act(actListenTilt, 2.0f),
    act(actNodM, 1.4f),
    until(ev2),
    still(0.5f),
    act(actLookDown, 1.8f),
    pose(POSE_SCRATCH_HEAD, 0.6f, 1.8f),
    pose(POSE_STAND, 0.6f, 0.2f),
    until(gone),
    TO_SIT,
};

// ---- 11. firefly_jar -------------------------------------------------------------------------
// Dusk or night: fireflies blink on over the grass. He catches them in a jar
// in three sweeps, looks at the glowing jar a long while, opens the lid, and
// they spread back out and blink off one by one.

enum FlyState : uint8_t { FLY_FREE = 0, FLY_TO_JAR, FLY_IN_JAR, FLY_OUT, FLY_DONE };

void beginFireflies(World& w) {
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  m.p[0] = Vec2{F.x + 92.f, F.y - 30.f};
  m.p[1] = Vec2{F.x + 150.f, F.y - 32.f};
  m.p[2] = Vec2{F.x + 208.f, F.y - 34.f};
  const int n = m.var < 12 ? 12 : (m.var > 20 ? 20 : m.var);
  for (int i = 0; i < n; ++i) {
    const float hx = F.x + 70.f + 210.f * hash01((uint32_t)i, 11u + (uint32_t)F.x);
    const float hy = 300.f + 72.f * hash01((uint32_t)i, 29u + (uint32_t)F.y);
    Glyph* g = glyphNew(w, GL_DOT, Vec2{hx, hy}, 0);
    if (!g) break;
    g->slot = FIRE_CORE;
    g->q = Vec2{hx, hy};
    g->scale = 0.f;
    g->t = hash01((uint32_t)i, 7u);   // its own phase
    g->k = FLY_FREE;
    g->rot = 0.f;
  }
}

Vec2 jarAt(const World& w) {
  const Hiker& h = w.hiker;
  const Vec2 hand = hikerHandPos(h, 0);
  return Vec2{hand.x + 0.015f * h.height * (float)h.facing, hand.y - 0.045f * h.height};
}

void tickFireflies(World& w, float dt) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  const float t = m.t - m.tc;
  const Vec2 jar = jarAt(w);
  const int held_y = (int)w.hiker.feet.y + 2;
  int left = 0, i = 0;
  for (Glyph& g : m.g) {
    if (g.kind != GL_DOT || g.slot != FIRE_CORE) continue;
    const float ph = g.t;
    const float on_at = 0.3f * (float)i;
    const float blink = sinT(t * 0.42f + ph);
    const bool lit = t > on_at && blink > -0.1f;
    switch ((uint8_t)g.k) {
      case FLY_FREE:
        g.pos = Vec2{g.q.x + 7.f * sinT(t * 0.05f + ph), g.q.y + 5.f * sinT(t * 0.037f + 2.f * ph)};
        g.scale = lit ? 0.8f : 0.f;
        g.base_y = (int16_t)(g.q.y + 30.f);
        if ((has(m, kSweep) && dist(g.pos, jar) < 38.f) || has(m, kC2)) g.k = FLY_TO_JAR;
        if (has(m, kC3)) g.k = FLY_DONE;
        break;
      case FLY_TO_JAR:
        g.pos.x += (jar.x - g.pos.x) * minf(1.f, dt * 7.f);
        g.pos.y += (jar.y - g.pos.y) * minf(1.f, dt * 7.f);
        g.scale = 0.8f;
        g.base_y = (int16_t)held_y;
        if (dist(g.pos, jar) < 2.5f) g.k = FLY_IN_JAR;
        break;
      case FLY_IN_JAR:
        g.pos = Vec2{jar.x + 1.6f * sinT(t * 0.27f + ph), jar.y + 2.4f * sinT(t * 0.21f + 1.7f * ph)};
        g.scale = blink > -0.5f ? 0.8f : 0.f;
        g.base_y = (int16_t)held_y;
        if (has(m, kC3)) {
          g.k = FLY_OUT;
          g.rot = t;
        }
        break;
      case FLY_OUT: {   // back out to its spot, blinking, then off one by one
        const float s = t - g.rot;
        g.pos.x += (g.q.x - g.pos.x) * minf(1.f, dt * 0.9f);
        g.pos.y += (g.q.y - 10.f - g.pos.y) * minf(1.f, dt * 0.9f) - 4.f * dt;
        g.scale = lit ? 0.8f : 0.f;
        g.base_y = (int16_t)(g.q.y + 30.f);
        if (s > 5.5f + 0.35f * (float)i) g.k = FLY_DONE;
        break;
      }
      default:
        g.kind = GL_NONE;
        break;
    }
    if (g.kind != GL_NONE) ++left;
    ++i;
  }
  if (has(m, kC3) && left == 0) fire(m, kE3);
}

// One sweep of the jar through the air, low to high and back.
void actSweep(World& w, float u) {
  MajorState& m = M(w);
  const float e = env(u, 0.12f, 0.12f);
  scriptSetArm(w, 0, e, 0.35f + 2.0f * sinT(0.5f * u), 0.25f);
  w.fx.lean = 0.08f * e;
  if (u > 0.15f && u < 0.85f) m.cue |= kSweep;
  else m.cue &= (uint16_t)~kSweep;
  if (u >= 1.f) scriptFxRest(w);
}

// The far hand lifts the lid; they go at 0.55.
void actOpenLid(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 1, e, 1.75f, 1.6f);
  w.fx.head = 0.1f * e;
  if (u >= 0.55f && !has(M(w), kC3)) cue3(w, 1.f);
  if (u >= 1.f) scriptFxRest(w);
}

void actWatchUp(World& w, float u) {
  w.fx.head = -0.3f * env(u, 0.2f, 0.25f);
  if (u >= 1.f) scriptFxRest(w);
}

const Step kFireflies[] = {
    FROM_REST,
    doit(cueGo),
    carry(CARRY_JAR),
    go(AT_M0, kStroll),
    face(RIGHT),
    still(1.6f),
    act(actSweep, 1.6f),
    go(AT_M1, 30.f),
    act(actSweep, 1.6f),
    go(AT_M2, 30.f),
    act(actSweep, 1.6f),
    doit(cue2),
    still(1.2f),
    pose(POSE_HOLD_MUG, 0.8f, 5.5f),
    act(actOpenLid, 1.4f),
    pose(POSE_STAND, 0.8f, 0.f),
    act(actWatchUp, 4.5f),
    until(ev3),
    carry(CARRY_NONE),
    TO_SIT,
};

// ---- 12. stargaze -------------------------------------------------------------------------------
// Night: on his back in the grass, he counts stars as they come out (5 to 8,
// popping in one at a time), pointing at each; loses count, shakes his head,
// starts over.

constexpr float kStarFirst = 1.0f, kStarGap = 2.2f, kRecountGap = 0.75f;

int starCount(const MajorState& m) { return m.var < 5 ? 5 : (m.var > 8 ? 8 : m.var); }

void beginStars(World& w) {
  MajorState& m = M(w);
  const Vec2 L = w.spine.camp.lie;
  const int n = starCount(m);
  int made = 0;
  for (int tries = 0; tries < 200 && made < n; ++tries) {
    const float x = L.x - 30.f + 300.f * hash01((uint32_t)tries, 3u + (uint32_t)L.x);
    const float y = 44.f + 140.f * hash01((uint32_t)tries, 17u + (uint32_t)L.y);
    if (x > 780.f || (x - 640.f) * (x - 640.f) + (y - 110.f) * (y - 110.f) < 60.f * 60.f) continue;
    bool ok = true;
    for (int i = 0; i < LS_STAR_COUNT && ok; ++i)
      ok = absf((float)w.land.stars[i].x - x) + absf((float)w.land.stars[i].y - y) > 14.f;
    for (const Glyph& g : m.g)
      if (ok && g.kind == GL_DOT && g.slot == STAR) ok = dist(g.pos, Vec2{x, y}) > 26.f;
    if (!ok) continue;
    Glyph* g = glyphNew(w, GL_DOT, Vec2{x, y}, (int)y);
    if (!g) break;
    g->slot = STAR;
    g->scale = 0.f;
    g->keep = true;   // they stay out for the rest of the loop
    g->k = (float)made;
    ++made;
  }
  m.count = (uint8_t)made;
}

Glyph* starAt(World& w, int i) {
  for (Glyph& g : M(w).g)
    if (g.kind == GL_DOT && g.slot == STAR && (int)g.k == i && !g.keep) return &g;
  for (Glyph& g : M(w).g)
    if (g.kind == GL_DOT && g.slot == STAR && (int)g.k == i) return &g;
  return nullptr;
}

// The near arm's shoulder angle pointing from him lying down at p.
float pointAt(const World& w, Vec2 p) {
  const Hiker& h = w.hiker;
  float r = 4.f;
  const Vec2 head = hikerHeadPos(h, &r);
  const Vec2 sh{head.x - (float)h.facing * 0.09f * h.height, head.y + 0.02f * h.height};
  return atan2f((float)h.facing * (p.x - sh.x), p.y - sh.y);
}

void tickStars(World& w, float) {
  MajorState& m = M(w);
  if (!has(m, kGo)) return;
  const float t = m.t - m.tc;
  for (int i = 0; i < m.count; ++i) {
    Glyph* g = starAt(w, i);
    if (!g) continue;
    const float s = t - (kStarFirst + kStarGap * (float)i);
    g->scale = s <= 0.f ? 0.f : 1.35f * easeOutBack(minf(1.f, s / 0.4f));
  }
}

float countSeconds(const MajorState& m) { return kStarFirst + kStarGap * (float)m.count + 0.6f; }

// Pointing at the newest star as each comes out.
void actCount(World& w, float u) {
  MajorState& m = M(w);
  const float T = countSeconds(m);
  const float t = u * T;
  const int i = (int)((t - kStarFirst) / kStarGap);
  float k = 0.f;
  if (t > kStarFirst && i < m.count) {
    Glyph* g = starAt(w, i);
    if (g) {
      const float into = t - kStarFirst - kStarGap * (float)i;
      const float a = pointAt(w, g->pos);
      w.fx.reach_to = i == 0 || into > 0.5f ? a : lerp(w.fx.reach_to, a, into / 0.5f);
      k = 1.f;
    }
  }
  w.fx.reach += (k * (1.f - easeInOut(window(u, 0.93f, 1.f))) - w.fx.reach) * 0.2f;
  if (u >= 1.f) scriptFxRest(w);
}

// Lost count: the head rolls side to side.
void actHeadShake(World& w, float u) {
  w.fx.head = 0.16f * sinT(u * 2.5f) * env(u, 0.1f, 0.2f);
  if (u >= 1.f) scriptFxRest(w);
}

// From the top, quicker: one, two, three...
void actRecount(World& w, float u) {
  MajorState& m = M(w);
  const float T = kRecountGap * (float)m.count + 0.6f;
  const float t = u * T;
  const int i = (int)(t / kRecountGap);
  if (i < m.count) {
    if (Glyph* g = starAt(w, i)) {
      const float a = pointAt(w, g->pos);
      const float into = t - kRecountGap * (float)i;
      w.fx.reach_to = i == 0 ? a : lerp(w.fx.reach_to, a, minf(1.f, into / 0.3f));
    }
  }
  w.fx.reach = (1.f - easeInOut(window(u, 0.85f, 1.f))) * easeInOut(window(u, 0.f, 0.1f));
  if (u >= 1.f) scriptFxRest(w);
}

// Each act's length depends on the star count, so the steps are built per count.
#define STARGAZE_STEPS(n)                                                                              \
  FROM_REST, go(AT_LIE, kStroll), face(RIGHT), doit(doPackOffM), still(0.5f), pose(POSE_SIT, 0.9f, 0.4f), \
      pose(POSE_LIE, 1.3f, 1.5f), doit(cueGo), act(actCount, kStarFirst + kStarGap * (float)(n) + 0.6f),   \
      act(actHeadShake, 1.4f), still(0.8f), act(actRecount, kRecountGap * (float)(n) + 0.6f), still(3.5f), \
      pose(POSE_SIT, 1.1f, 0.6f), pose(POSE_STAND, 0.9f, 0.3f), TO_SIT

const Step kStars5[] = {STARGAZE_STEPS(5)};
const Step kStars6[] = {STARGAZE_STEPS(6)};
const Step kStars7[] = {STARGAZE_STEPS(7)};
const Step kStars8[] = {STARGAZE_STEPS(8)};

#undef STARGAZE_STEPS

// ---- 13 to 16. the stream vignettes (Mike's idea) -------------------------------------------
// fishing_small, fishing_log, fishing_good and bathe (and the birds_on_water
// filler, further down). Shared: the stream as a path (the log and the dead
// fish drift along it as the ambient glints do), the fishing spot on the bank
// below the camp, the rod and its line, rings, and drops.

// The stream's centreline parameter (point index) where it crosses x.
float streamParamAt(const World& w, float x) {
  const Landscape& ls = w.land;
  for (int i = 0; i + 1 < LS_STREAM_PTS; ++i) {
    const LsStreamPt& a = ls.stream[i];
    const LsStreamPt& b = ls.stream[i + 1];
    if (x <= a.x && x >= b.x) return (float)i + (a.x > b.x ? (a.x - x) / (a.x - b.x) : 0.f);
  }
  return x > ls.stream[0].x ? 0.f : (float)(LS_STREAM_PTS - 1);
}
// A point on the stream at s, `lane` half widths toward the camp bank (+) or the near bank (-).
Vec2 streamPoint(const World& w, float s, float lane) {
  const LsStreamPt q = landscapeStreamAt(w.land, s);
  return Vec2{q.x + q.nx * lane * q.hw, q.y + q.ny * lane * q.hw};
}
// The way the water runs at s (downstream, toward -x), as a unit vector.
Vec2 streamDir(const World& w, float s) {
  const LsStreamPt a = landscapeStreamAt(w.land, s - 0.05f), b = landscapeStreamAt(w.land, s + 0.05f);
  const float l = maxf(1e-3f, dist(Vec2{a.x, a.y}, Vec2{b.x, b.y}));
  return Vec2{(b.x - a.x) / l, (b.y - a.y) / l};
}
// s carried downstream at px_per_s for dt.
float streamOn(const World& w, float s, float px_per_s, float dt) {
  const LsStreamPt a = landscapeStreamAt(w.land, s), b = landscapeStreamAt(w.land, s + 0.05f);
  const float seg = maxf(20.f, dist(Vec2{a.x, a.y}, Vec2{b.x, b.y}) / 0.05f);
  return s + px_per_s * dt / seg;
}
// Turns for a shape whose nose (+x, `facing` mirrored) points along d.
float turnsAlong(Vec2 d, int facing) {
  return facing < 0 ? atan2f(-d.y, -d.x) / (2.f * kPi) : atan2f(d.y, d.x) / (2.f * kPi);
}
// The water's waterline y at x (the centreline, and its half width), or `fallback`.
float waterMidY(const World& w, float x, float fallback) {
  float cy, half;
  return landscapeStreamSpan(w.land, x, cy, half) ? cy : fallback;
}

// Rings age out; drops (GL_DOT with k 1, velocity in q) fall and stop at base_y;
// a flying bit (GL_DOT with k 2, from q to the mode's target) is moved by its owner.
void ageGlyphs(World& w, float dt) {
  for (Glyph& g : M(w).g) {
    if (g.kind == GL_RING) {
      g.t += dt;
      if (g.t >= kRingLife) g.kind = GL_NONE;
    } else if (g.kind == GL_DOT && g.k > 0.5f && g.k < 1.5f) {
      g.q.y += 520.f * dt;
      g.pos.x += g.q.x * dt;
      g.pos.y += g.q.y * dt;
      if (g.pos.y >= (float)g.base_y) g.kind = GL_NONE;
    }
  }
}
void dropAt(World& w, Vec2 p, Vec2 v, float ground) {
  if (Glyph* g = glyphNew(w, GL_DOT, p, (int)ground)) {
    g->slot = WATER_LIGHT;
    g->scale = 0.95f;
    g->q = v;
    g->k = 1.f;
  }
}
// A drip off him: from somewhere on his body (a repeatable spread), down to his feet.
void dripFrom(World& w, uint32_t n) {
  const Hiker& h = w.hiker;
  if (!h.visible) return;
  const float a = hash01(n, 7u), b = hash01(n, 11u);
  const Vec2 p{h.feet.x + (a - 0.5f) * 16.f, h.feet.y - h.height * (0.25f + 0.4f * b)};
  dropAt(w, p, Vec2{(a - 0.5f) * 14.f, 6.f}, h.feet.y + 2.f);
}

// The line's states (MajorState::mode, seconds in it: mt).
enum LineMode : uint8_t {
  LN_NONE = 0,   // reeled in (no line drawn)
  LN_CAST,       // the float flying out (an action moves it)
  LN_FLOAT,      // the float resting on the water, bobbing (u0: how far it is pulled under)
  LN_FISH,       // a tiny fish dangling under the tip
  LN_FLICK,      // the tiny fish thrown back, the line in
  LN_THROWN,     // the rod itself flying across the stream
  LN_LOG,        // snagged on the log
  LN_HOOKED,     // a proper fish on, under the water at v0
  LN_LANDED,     // the fish out on the bank, flopping (the line in, the rod down)
  LN_HELD,       // the fish in his hands
  LN_ROCK,       // the fish on the flat rock
  LN_DRIP        // (fishing_log) he is back, dripping
};

void lineTo(World& w, uint8_t mode) {
  MajorState& m = M(w);
  m.mode = mode;
  m.mt = 0.f;
}

// Where he fishes: on the bank below the camp, a little left of where he
// cooled his hand, facing upstream (right). The float rests on the water
// ahead of him in the camp-side half of the stream; a thrown rod lands on the
// near bank beyond it.
void fishingSpot(World& w) {
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  const float x = F.x + 76.f;
  float cy, half;
  m.p[0] = landscapeStreamSpan(w.land, x, cy, half) ? Vec2{x, cy - half - 1.f} : Vec2{x, F.y + 12.f};
  const float fx = x + 74.f;
  m.p[1] = landscapeStreamSpan(w.land, fx, cy, half) ? Vec2{fx, cy - 0.3f * half} : Vec2{fx, m.p[0].y + 16.f};
  const float tx = x + 160.f;
  m.p[2] = landscapeStreamSpan(w.land, tx, cy, half) ? Vec2{tx, minf(470.f, cy + half + 13.f)} : Vec2{tx, 462.f};
}

bool rodIsStick(const World& w) { return M(w).stick; }
bool rodIsRod(const World& w) { return !M(w).stick; }
bool tentUp(const World& w) { return w.tent.state == TENT_UP; }
bool tentNotUp(const World& w) { return w.tent.state != TENT_UP; }

void beginFishing(World& w) {
  MajorState& m = M(w);
  fishingSpot(w);
  // The rod went last loop (memory): a stick off the wood pile instead.
  m.stick = w.memory.rod_lost;
  w.memory.rod_lost = false;
  lineTo(w, LN_NONE);
  m.u0 = 0.f;
}

// The stick off the wood pile, string already on it.
void doTakeStick(World& w, float) {
  Hiker& h = w.hiker;
  hikerCarry(h, CARRY_ROD);
  h.rod_stick = true;
  h.rod_len = 0.6f;
  h.rod_bend = h.rod_tilt = 0.f;
  h.rod_line = h.rod_float = false;
}
// Digs the rod out of the pack, telescoped short.
void actRodOut(World& w, float u) {
  const float e = env(u, 0.15f, 0.15f);
  w.fx.lean = e * (0.12f + 0.06f * sinT(u * 3.f));
  w.fx.reach_to = 0.9f;
  w.fx.reach = e * (0.5f + 0.3f * sinT(u * 4.f + 0.25f));
  if (u >= 0.7f && w.hiker.carry != CARRY_ROD) {
    Hiker& h = w.hiker;
    hikerCarry(h, CARRY_ROD);
    h.rod_stick = false;
    h.rod_len = 0.28f;
    h.rod_bend = h.rod_tilt = 0.f;
    h.rod_line = h.rod_float = false;
  }
  if (u >= 1.f) scriptFxRest(w);
}
// Two quick pulls and the rod is out to full length.
void actExtend(World& w, float u) {
  Hiker& h = w.hiker;
  const float p1 = plateauM(u, 0.1f, 0.12f, 0.08f), p2 = plateauM(u, 0.5f, 0.12f, 0.08f);
  scriptSetArm(w, 1, maxf(p1, p2), 0.1f, 0.4f);   // the far hand pulls the sections out
  h.rod_tilt = 0.1f * (p1 + p2);
  h.rod_len = u < 0.2f ? 0.28f : (u < 0.6f ? 0.52f : 0.75f);
  if (u >= 1.f) {
    h.rod_tilt = 0.f;
    scriptFxRest(w);
  }
}

// The cast: from POSE_CAST (rod back) he whips it forward; the float flies
// out from the tip and lands with a ring.
void actCastOut(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) {
    scriptSetPose(w, POSE_FISH_STAND, 0.22f);
    h.rod_tilt = 0.f;
    h.rod_bend = 0.f;
    h.rod_line = true;
    h.rod_float = true;
    m.v0 = hikerRodTip(h);
    lineTo(w, LN_CAST);
    m.u0 = 0.f;
  }
  if (m.mode == LN_CAST) {
    const float k = window(u, 0.08f, 0.7f);
    h.rod_end = lob(m.v0, m.p[1], 34.f, easeOutCubic(k));
    if (k >= 1.f) {
      ringAt(w, m.p[1], 0.6f);
      lineTo(w, LN_FLOAT);
    }
  }
}
#define CAST pose(POSE_CAST, 0.6f, 0.15f), act(actCastOut, 1.0f)

// The float bobs twice and goes under (u0 how far down, px).
void actNibble(World& w, float u) {
  MajorState& m = M(w);
  const float d1 = plateauM(u, 0.1f, 0.06f, 0.08f), d2 = plateauM(u, 0.42f, 0.06f, 0.08f);
  const float under = easeInOut(window(u, 0.78f, 0.9f));
  m.u0 = maxf(2.f * maxf(d1, d2), 4.5f * under);
  if (crossed(u, m.u1, 0.12f) || crossed(u, m.u1, 0.44f)) ringAt(w, m.p[1], 0.35f);
  if (crossed(u, m.u1, 0.8f)) ringAt(w, m.p[1], 0.5f);
  m.u1 = u >= 1.f ? 0.f : u;
}

// A tiny fish (about 3 px tall) comes up on the line.
Glyph* tinyFish(World& w) {
  for (Glyph& g : M(w).g)
    if (g.kind == GL_SHAPE && g.shape == SH_FISH) return &g;
  return nullptr;
}

// The yank: the tip goes up hard (harder each time: p), he rocks back and
// his shoulders come up; out comes the tiny fish.
void yank(World& w, float u, float p) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  const float up = easeOutCubic(window(u, 0.f, 0.28f));
  h.rod_tilt = (0.3f + 0.22f * p) * up;
  const float jolt = sinT(0.5f * window(u, 0.f, 0.55f));
  w.fx.lean = -0.07f * p * jolt;
  w.fx.lift = 0.006f * p * p * jolt;
  w.fx.head = -0.12f * p * jolt;
  scriptSetArm(w, 1, 0.5f * jolt, 0.9f + 0.3f * p, 0.8f);   // the far hand comes up with it
  if (u >= 0.12f && m.mode == LN_FLOAT) {
    lineTo(w, LN_FISH);
    m.v0 = h.rod_end;
    ringAt(w, m.p[1], 0.7f);
    if (Glyph* g = glyphNew(w, GL_SHAPE, m.p[1], (int)h.feet.y + 2)) {
      g->shape = SH_FISH;
      g->scale = 0.5f;
      g->rot = 0.25f;
    }
  }
  if (u >= 1.f) scriptFxRest(w);
}
void actYank1(World& w, float u) { yank(w, u, 1.f); }
void actYank2(World& w, float u) { yank(w, u, 1.6f); }
void actYank3(World& w, float u) { yank(w, u, 2.4f); }

// He lowers the rod toward him and has a long look at what is on it.
void actLookTiny(World& w, float u) {
  Hiker& h = w.hiker;
  const float e = env(u, 0.25f, 0.2f);
  h.rod_tilt = lerp(h.rod_tilt, 0.15f, 0.08f);
  w.fx.head = -0.3f * e;
  w.fx.lean = 0.07f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// A flick of the tip and the tiny fish goes back in; the line comes in.
void actFlick(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  h.rod_tilt = lerp(0.15f, -0.2f, easeOutCubic(window(u, 0.15f, 0.35f))) * (1.f - easeInOut(window(u, 0.5f, 1.f)));
  if (u >= 0.3f && m.mode == LN_FISH) {
    lineTo(w, LN_FLICK);
    h.rod_line = false;
    if (Glyph* g = tinyFish(w)) m.v0 = g->pos;
  }
  if (u >= 1.f) h.rod_tilt = 0.f;
}

// The third one: the rod goes back over his shoulder (POSE_CAST), and he
// hurls it, end over end, across the stream.
void actHurl(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) {
    const Vec2 tip = hikerRodTip(h), hand = hikerHandPos(h, 0);
    if (Glyph* g = glyphNew(w, GL_SHAPE, mixV(hand, tip, 0.45f), (int)h.feet.y + 1)) {
      g->shape = SH_ROD;
      g->k = m.stick ? 1.f : 0.f;
      g->facing = 1;
      g->rot = turnsAlong(Vec2{tip.x - hand.x, tip.y - hand.y}, 1);
      m.v0 = g->pos;
      m.u1 = g->rot;
    }
    // The third tiny fish drops off the line, back into the water.
    if (Glyph* f = tinyFish(w)) {
      f->k = 3.f;
      f->q = f->pos;
      f->t = 0.f;
    }
    hikerCarry(h, CARRY_NONE);
    h.rod_line = h.rod_float = false;
    h.rod_tilt = h.rod_bend = 0.f;
    lineTo(w, LN_THROWN);
    w.memory.rod_lost = true;   // next loop he fishes with a stick
    scriptSetPose(w, POSE_STAND, 0.25f);
  }
  // Both arms flung forward and up, then down.
  const float e = easeOutCubic(window(u, 0.f, 0.2f)) * (1.f - easeInOut(window(u, 0.55f, 1.f)));
  scriptSetArm(w, 0, e, 2.0f, 0.15f);
  scriptSetArm(w, 1, e, 1.8f, 0.25f);
  w.fx.lean = 0.14f * e;
  if (u >= 1.f) scriptFxRest(w);
}
constexpr float kRodFlightS = 1.4f;

// Storming off, and the tent.
void doStomp(World& w, float) { w.fx.stomp = 1.f; }
void doStompOff(World& w, float) { w.fx.stomp = 0.f; }
void doIntoTent(World& w, float) {
  w.fx.on = false;
  w.fx.path_on = false;
  scriptSetPose(w, POSE_STAND, 0.f);
}
// Once, hard: the tent jolts, swings back and settles.
void actTentShake(World& w, float u) {
  const float t = u * 1.6f;
  const float a = t < 0.15f ? 0.f : 5.f * expf(-3.2f * (t - 0.15f)) * sinT(1.8f * (t - 0.15f));
  w.tent.wobble = floorf(a + 0.5f);
  if (u >= 1.f) w.tent.wobble = 0.f;
}
void doOutOfTent(World& w, float) {
  Hiker& h = w.hiker;
  h.feet = w.spine.camp.door;
  h.facing = 1;
  scriptSetPose(w, POSE_CROUCH, 0.f);
  w.fx.on = true;
  w.fx.path_on = false;
}
// A long breath out: shoulders and head drop, and come back up.
void actSighM(World& w, float u) {
  const float s = sinT(0.5f * u);
  w.fx.lean = 0.1f * s;
  w.fx.head = 0.3f * s;
  if (u >= 1.f) scriptFxRest(w);
}

// The tick the three fishing majors share: the float, the tiny fish, the rod in flight.
void tickLine(World& w, float dt) {
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  m.mt += dt;
  ageGlyphs(w, dt);
  if (Glyph* f = tinyFish(w)) {   // let go of (k 3): falls back into the water
    if (f->k > 2.5f) {
      f->t += dt;
      const float u = minf(1.f, f->t / 0.8f);
      const Vec2 to{m.p[1].x - 6.f, m.p[1].y + 1.f};
      f->pos = lob(f->q, to, 12.f, u);
      f->rot = 0.25f + 0.8f * u;
      if (u >= 1.f) {
        ringAt(w, to, 0.45f);
        f->kind = GL_NONE;
      }
    }
  }
  switch (m.mode) {
    case LN_FLOAT: {
      const float bob = 0.7f * sinT(m.t * 0.55f);
      h.rod_end = Vec2{m.p[1].x, m.p[1].y + bob + m.u0};
      h.rod_float = m.u0 < 4.f;   // pulled under
      break;
    }
    case LN_FISH: {   // swinging under the tip
      const Vec2 tip = hikerRodTip(h);
      const float sw = 3.f * sinT(m.mt * 1.3f) * expf(-0.6f * m.mt);
      const Vec2 hang{tip.x + sw, tip.y + 15.f};
      h.rod_end = m.mt < 0.25f ? mixV(m.v0, hang, easeOutCubic(m.mt / 0.25f)) : hang;
      h.rod_float = false;
      if (Glyph* g = tinyFish(w)) {
        g->pos = Vec2{h.rod_end.x, h.rod_end.y + 2.f};
        g->rot = 0.25f + 0.03f * sinT(m.mt * 2.6f);
        g->facing = 1;
        g->base_y = (int16_t)(h.feet.y + 2.f);
      }
      break;
    }
    case LN_FLICK: {
      Glyph* g = tinyFish(w);
      if (!g) break;
      const float u = minf(1.f, m.mt / 0.7f);
      const Vec2 to{m.p[1].x + 10.f, m.p[1].y + 2.f};
      g->pos = lob(m.v0, to, 16.f, u);
      g->rot = 0.25f + 0.6f * u;
      if (u >= 1.f) {
        ringAt(w, to, 0.45f);
        g->kind = GL_NONE;
        lineTo(w, LN_NONE);
      }
      break;
    }
    case LN_THROWN: {
      Glyph* g = nullptr;
      for (Glyph& x : m.g)
        if (x.kind == GL_SHAPE && x.shape == SH_ROD) g = &x;
      if (!g) break;
      const float u = minf(1.f, m.mt / kRodFlightS);
      g->pos = lob(m.v0, m.p[2], 70.f, u);
      g->rot = m.u1 - 2.25f * u;     // end over end
      g->base_y = (int16_t)lerp(h.feet.y + 1.f, m.p[2].y, u);
      if (u >= 1.f) {   // down on the near bank, where it stays
        g->kind = GL_NONE;
        Item& r = w.items[ITEM_ROD];
        r = Item{m.p[2], true, false, 1, 1.f, m.p[2].y, false, 0, 0.02f, 0.f, 1.f, false};
        r.alt = m.stick;
        const int s = worldTakeBit(w, BIT_DUST);
        if (s >= 0) {
          Bit& b = w.bits[s];
          b.pos = Vec2{m.p[2].x - 14.f, m.p[2].y - 2.f};
          b.land_y = m.p[2].y;
        }
        lineTo(w, LN_NONE);
      }
      break;
    }
    default: break;
  }
}

// ---- 13. fishing_small ----------------------------------------------------------------------
// Three tiny fish, each yank bigger; he hurls the rod across the stream and
// storms off to the tent (it shakes once). Memory: the rod is lost.

void tickFishSmall(World& w, float dt) { tickLine(w, dt); }

// Fetch the rod (or a stick), carry it up, out to the bank, rod out.
#define GET_ROD                                                                                               \
  skipUnless(rodIsStick, 5), go(AT_WOOD_WORK, kStroll), face(RIGHT), pose(POSE_CROUCH, 0.6f, 0.4f),          \
      doit(doTakeStick), pose(POSE_CARRY, 0.5f, 0.1f), skipUnless(rodIsRod, 5), go(AT_PACK_WORK, kStroll),    \
      face(LEFT), pose(POSE_CROUCH, 0.6f, 0.3f), act(actRodOut, 1.4f), pose(POSE_CARRY, 0.5f, 0.1f),          \
      go(AT_M0, kStroll), face(RIGHT), pose(POSE_FISH_STAND, 0.6f, 0.3f), skipUnless(rodIsRod, 1),           \
      act(actExtend, 1.1f)

const Step kFishSmall[] = {
    FROM_REST,
    GET_ROD,
    // One.
    CAST, still(3.0f), act(actNibble, 1.1f), act(actYank1, 0.75f), act(actLookTiny, 1.8f), act(actFlick, 1.0f),
    still(0.8f),
    // Two: quicker, bigger, a shake of the head.
    CAST, still(2.6f), act(actNibble, 1.0f), act(actYank2, 0.6f), act(actLookTiny, 1.4f), act(actHeadShake, 1.2f),
    act(actFlick, 0.9f), still(0.8f),
    // Three: that is it.
    CAST, still(2.0f), act(actNibble, 0.9f), act(actYank3, 0.5f), act(actLookTiny, 1.1f), still(0.3f),
    pose(POSE_CAST, 0.7f, 0.25f), act(actHurl, 0.8f), still(1.2f),
    // Storms off: into the tent, which shakes; out again a while later.
    doit(doStomp), skipUnless(tentUp, 10), go(AT_DOOR, 50.f), face(LEFT), doit(doStompOff),
    pose(POSE_CROUCH, 0.35f, 0.1f), doit(doIntoTent), act(actTentShake, 1.6f), still(2.6f), doit(doOutOfTent),
    pose(POSE_STAND, 0.8f, 0.6f), act(actSighM, 1.6f),
    // (No tent to sulk in: to the pack, hands on hips.)
    skipUnless(tentNotUp, 3), go(AT_PACK_WORK, 50.f), doit(doStompOff), pose(POSE_HANDS_ON_HIPS, 0.4f, 3.0f),
    TO_SIT,
};

// ---- 14. fishing_log ------------------------------------------------------------------------
// A log floats down; the line snags it; he digs in, is pulled into the water
// and towed off downstream out of the frame. A while later he walks back in
// from the left, dripping, wrings out his hat and sits by the fire.

constexpr float kLogV = 52.f, kLogHold = 12.f, kTowV = 74.f, kTowBack = 44.f;

void beginFishLog(World& w) {
  beginFishing(w);
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  m.p[2] = Vec2{F.x - 66.f, F.y - 12.f};   // where he wrings out his hat, left of the fire
  const float x = m.p[0].x + 22.f;         // pulled in here, off the bank
  float cy, half;
  m.p[3] = landscapeStreamSpan(w.land, x, cy, half) ? Vec2{x, cy - 0.45f * half} : Vec2{x, m.p[0].y + 10.f};
}

Glyph* logGlyph(World& w) {
  for (Glyph& g : M(w).g)
    if (g.kind == GL_SHAPE && g.shape == SH_LOG) return &g;
  return nullptr;
}
bool logSnagged(const World& w) { return (M(w).cue & kE1) != 0; }
bool towedOff(const World& w) { return (M(w).cue & kE2) != 0; }
bool emptyDone(const World& w) { return M(w).t - M(w).tc >= (float)M(w).var; }

// He has no idea: looks about, up at the sky, back at the float.
void actWhistle(World& w, float u) {
  const float e = env(u, 0.12f, 0.15f);
  w.fx.head = e * (-0.28f + 0.12f * sinT(u * 1.5f));
  w.fx.lean = -0.04f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// Snagged: the rod bows, he is jerked forward a step.
void actSnag(World& w, float u) {
  Hiker& h = w.hiker;
  h.rod_bend = 0.85f * easeOutCubic(window(u, 0.f, 0.3f));
  h.rod_tilt = -0.25f * easeOutCubic(window(u, 0.f, 0.3f));
  const float j = sinT(0.5f * window(u, 0.f, 0.6f));
  w.fx.lean = 0.2f * j;
  w.fx.head = 0.15f * j - 0.25f * window(u, 0.4f, 1.f);
  if (u >= 1.f) scriptFxRest(w);
}
// He leans back and digs his heels in; it pulls him forward a step, then another (a stumble).
void actHeave(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) m.u1 = h.feet.x;
  const float back = env(u, 0.12f, 0.1f);
  const float st1 = plateauM(u, 0.35f, 0.08f, 0.05f), st2 = plateauM(u, 0.7f, 0.08f, 0.05f);
  w.fx.lean = -0.32f * back + 0.3f * maxf(st1, st2);
  w.fx.lift = 0.02f * maxf(st1, st2);
  w.fx.head = -0.2f * back;
  h.feet.x = m.u1 + 5.f * easeInOut(window(u, 0.35f, 0.45f)) + 6.f * easeInOut(window(u, 0.7f, 0.8f));
  h.rod_bend = 0.85f + 0.15f * sinT(u * 5.f);
  if (u >= 1.f) scriptFxRest(w);
}
// Pulled off the bank into the water, flailing.
void actPulledIn(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) {
    m.v0 = h.feet;
    w.fx.wade = true;
  }
  const float k = easeInCubic(window(u, 0.f, 0.75f));
  h.feet = mixV(m.v0, m.p[3], k);
  const float j = sinT(0.5f * window(u, 0.f, 0.7f));
  w.fx.lean = 0.35f * j - 0.3f * window(u, 0.7f, 1.f);
  w.fx.lift = 0.03f * j;
  scriptSetArm(w, 1, j, 2.4f, 0.4f);   // the far arm flails up
  if (u >= 1.f) {
    w.fx.lift = 0.f;
    w.fx.arm_w[1] = 0.f;
    w.fx.lean = -0.3f;   // the tow keeps him leaning back
  }
}
void doTow(World& w, float) {
  cue2(w, 1.f);
  M(w).u1 = 0.f;
}
// Out of the frame: gone (the rod with him), the clock for the empty stretch starts.
void doGone(World& w, float) {
  Hiker& h = w.hiker;
  w.fx.on = false;
  w.fx.wade = false;
  w.fx.lean = 0.f;
  hikerCarry(h, CARRY_NONE);
  h.rod_line = h.rod_float = false;
  h.rod_bend = h.rod_tilt = 0.f;
  w.memory.rod_lost = true;   // it went down the river
  M(w).tc = M(w).t;
}
// Back from the left, soaked.
void doBackFromLeft(World& w, float) {
  Hiker& h = w.hiker;
  h.feet = w.spine.camp.edge_l;
  h.facing = 1;
  scriptSetPose(w, POSE_STAND, 0.f);
  scriptFxRest(w);
  w.fx.on = true;
  w.fx.path_on = false;
  lineTo(w, LN_DRIP);
}
// Hat off, wrung out (two drops), held a moment.
void actWring(World& w, float u) {
  Hiker& h = w.hiker;
  Item& hat = w.items[ITEM_HAT];
  const float up = plateauM(u, 0.f, 0.12f, 0.06f);
  if (u >= 0.12f && h.hat_on) {
    h.hat_on = false;
    hat.held = true;
    hat.down = false;
    hat.facing = (int8_t)h.facing;
    hat.size = 1.f;
  }
  const float tw = env(u, 0.3f, 0.12f) * (u > 0.22f ? 1.f : 0.f);
  const float wr = sinT(u * 4.f);
  if (u < 0.22f) {
    scriptSetArm(w, 0, up, 2.75f, 2.0f);   // up to the hat
  } else {
    scriptSetArm(w, 0, tw, 0.95f + 0.3f * wr, 1.1f);
    scriptSetArm(w, 1, tw, 0.95f - 0.3f * wr, 1.2f);
  }
  w.fx.head = 0.22f * tw;
  if (crossed(u, M(w).u1, 0.5f) || crossed(u, M(w).u1, 0.7f)) {
    const Vec2 p = hikerHandsMid(h);
    dropAt(w, Vec2{p.x, p.y + 4.f}, Vec2{0.f, 10.f}, h.feet.y + 2.f);
  }
  M(w).u1 = u >= 1.f ? 0.f : u;
  if (u >= 1.f) w.fx.head = 0.f;
}
// And back on his head.
void actHatOnM(World& w, float u) {
  Hiker& h = w.hiker;
  Item& hat = w.items[ITEM_HAT];
  const float e = env(u, 0.4f, 0.4f);
  scriptSetArm(w, 0, e, 2.75f, 2.0f);
  if (u >= 0.45f && !h.hat_on) {
    h.hat_on = true;
    hat.held = hat.down = false;
  }
  if (u >= 1.f) scriptFxRest(w);
}

void tickFishLog(World& w, float dt) {
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  tickLine(w, dt);
  // The log: from upstream off the right edge, down the stream past him.
  Glyph* lg = logGlyph(w);
  if (has(m, kGo) && !has(m, kFired)) {
    fire(m, kFired);
    m.u0 = 0.f;
    m.count = 0;
    lg = glyphNew(w, GL_SHAPE, streamPoint(w, streamParamAt(w, 860.f), 0.3f), 0);
    if (lg) {
      lg->shape = SH_LOG;
      lg->slot = TRUNK;
      lg->q.x = streamParamAt(w, 860.f);   // q.x: its stream parameter
    }
  }
  if (lg) {
    float v = kLogV;
    if (has(m, kE1) && !has(m, kC2)) v = kLogHold;   // he is holding it back
    if (has(m, kC2)) v = kTowV;
    lg->q.x = streamOn(w, lg->q.x, v, dt);
    const Vec2 c = streamPoint(w, lg->q.x, 0.3f);
    lg->pos = Vec2{c.x, c.y - 2.f + 0.6f * sinT(m.t * 0.7f)};
    lg->rot = turnsAlong(streamDir(w, lg->q.x), 1) + 0.008f * sinT(m.t * 0.4f);
    lg->base_y = (int16_t)(c.y + 3.f);
    // The line snags its near end as it comes past the float.
    if (!has(m, kE1) && lg->pos.x - 18.f <= m.p[1].x) {
      fire(m, kE1);
      lineTo(w, LN_LOG);
      h.rod_float = false;
    }
    // (Near the bottom-left the stream leaves the frame: the line's end stays in it, as he does.)
    if (m.mode == LN_LOG)
      h.rod_end = Vec2{lg->pos.x + 14.f * cosT(lg->rot), minf(472.f, lg->pos.y + 14.f * sinT(lg->rot))};
    if (lg->pos.x < -80.f || lg->pos.y > 520.f) {
      lg->kind = GL_NONE;
      lg = nullptr;
    }
  }
  // The tow: once the log has gone past him he is dragged behind it, leaning
  // back, heels in, splashes trailing; never below the frame's bottom.
  if (has(m, kC2) && !has(m, kE2)) {
    {
      const float x = lg ? minf(h.feet.x, lg->pos.x + kTowBack) : h.feet.x - kTowV * dt;
      if (x < h.feet.x - 0.01f) h.facing = -1;
      h.feet.x = x;
      float cy, half;
      const float y = landscapeStreamSpan(w.land, x, cy, half) ? cy - 0.3f * half : h.feet.y;
      h.feet.y = minf(474.f, y);
    }
    h.rod_bend = 0.9f + 0.1f * sinT(m.t * 3.f);
    w.fx.lean = -0.42f + 0.06f * sinT(m.t * 2.2f);
    m.u1 += dt;
    if (m.u1 > 0.32f && h.visible) {
      m.u1 = 0.f;
      ringAt(w, Vec2{h.feet.x + 9.f, w.fx.wet_y > 0.f ? w.fx.wet_y + 2.f : h.feet.y - 4.f}, 0.75f);
    }
    if (!h.visible || h.feet.x < -60.f) fire(m, kE2);
  }
  // Back: dripping for a few seconds.
  if (m.mode == LN_DRIP && m.mt < 5.f) {
    const uint32_t n = (uint32_t)(m.mt / 0.2f);
    if (n != m.count) {
      m.count = (uint8_t)n;
      dripFrom(w, n + 100u);
    }
  }
}

const Step kFishLog[] = {
    FROM_REST,
    GET_ROD,
    CAST,
    still(1.0f),
    doit(cueGo),                 // a log comes down from the right; he has not seen it
    act(actWhistle, 4.0f),
    until(logSnagged),
    act(actSnag, 0.8f),
    act(actHeave, 2.4f),
    act(actPulledIn, 1.3f),
    doit(doTow),
    until(towedOff),
    doit(doGone),
    until(emptyDone),            // the scene sits empty (var seconds)
    doit(doBackFromLeft),
    go(AT_M2, 32.f),
    face(RIGHT),
    still(0.4f),
    act(actWring, 3.4f),
    act(actHatOnM, 1.0f),
    still(0.4f),
    TO_SIT,
};

// ---- 15. fishing_good -----------------------------------------------------------------------
// Sitting with the rod out, a proper bite: he pulls, the rod bows, a fish
// arcs out onto the bank and flops; he pounces and holds it up. At the flat
// rock by the fire he guts it (three strokes, a flick into the stream) and
// props it on a stick over the fire. Dinner d (spine.cpp) follows.

void beginFishGood(World& w) {
  beginFishing(w);
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  m.p[2] = Vec2{m.p[0].x + 30.f, m.p[0].y + 1.f};   // where the fish lands on the bank
  m.p[3] = Vec2{F.x + 54.f, 351.f};                  // crouched right of the flat (reading) rock
}

Glyph* bigFish(World& w) {
  for (Glyph& g : M(w).g)
    if (g.kind == GL_SHAPE && g.shape == SH_FISH) return &g;
  return nullptr;
}
Vec2 rockTop(const World& w) { return Vec2{w.fire.pos.x + 17.f, 333.f}; }

// Up, the rod bows; he plays it in, leaning back (the fish comes toward the bank under water).
void actPlay(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) {
    scriptSetPose(w, POSE_FISH_STAND, 0.35f);
    lineTo(w, LN_HOOKED);
    m.v0 = m.p[1];
    h.rod_float = false;
  }
  const float k = easeInOut(window(u, 0.1f, 0.3f));
  h.rod_bend = k * (0.8f + 0.2f * sinT(u * 4.5f));
  h.rod_tilt = 0.3f * k;
  w.fx.lean = -0.22f * k * (0.8f + 0.2f * sinT(u * 4.5f));
  w.fx.head = 0.1f * k;
  // The fish under the water, pulled in toward the bank.
  const float x = lerp(m.p[1].x, m.p[2].x + 14.f, easeInOut(u));
  float cy, half;
  const float y = landscapeStreamSpan(w.land, x, cy, half) ? cy - 0.6f * half : m.p[1].y;
  m.v0 = Vec2{x + 3.f * sinT(u * 6.f), y};
  h.rod_end = m.v0;
  if (u >= 1.f) {
    w.fx.lean = -0.22f;
    w.fx.head = 0.1f;
  }
}
// Out it comes: a heave and the fish arcs out of the water onto the bank.
void actLandFish(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) {
    ringAt(w, m.v0, 1.f);
    if (Glyph* g = glyphNew(w, GL_SHAPE, m.v0, (int)m.p[2].y + 1)) {
      g->shape = SH_FISH;
      g->facing = -1;
    }
    lineTo(w, LN_LANDED);
    m.u1 = 0.f;
  }
  h.rod_bend = 1.f - easeOutCubic(window(u, 0.f, 0.3f));
  h.rod_tilt = lerp(0.3f, 0.7f, easeOutCubic(window(u, 0.f, 0.35f)));
  w.fx.lean = lerp(-0.22f, -0.32f, window(u, 0.f, 0.3f)) * (1.f - window(u, 0.6f, 1.f));
  w.fx.head = lerp(0.1f, -0.15f, window(u, 0.f, 0.5f)) * (1.f - window(u, 0.7f, 1.f));
  if (Glyph* g = bigFish(w)) {
    const float k = minf(1.f, u / 0.85f);
    const Vec2 a = lob(m.v0, m.p[2], 46.f, k), b = lob(m.v0, m.p[2], 46.f, minf(1.f, k + 0.03f));
    g->pos = Vec2{a.x, a.y - 3.f};
    g->rot = turnsAlong(Vec2{b.x - a.x, b.y - a.y + 1e-4f}, g->facing);
    h.rod_end = g->pos;
    if (k >= 1.f) {
      g->rot = 0.f;
      g->pos = Vec2{m.p[2].x, m.p[2].y - 3.f};
      h.rod_line = false;
    }
  }
  if (u >= 1.f) {
    h.rod_tilt = h.rod_bend = 0.f;
    scriptFxRest(w);
  }
}
// The rod down on the bank beside him.
void doRodDown(World& w, float) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  hikerCarry(h, CARRY_NONE);
  h.rod_line = h.rod_float = false;
  h.rod_tilt = h.rod_bend = 0.f;
  Item& r = w.items[ITEM_ROD];
  r = Item{Vec2{m.p[0].x - 4.f, m.p[0].y + 1.f}, true, false, (int8_t)-1, 1.f, m.p[0].y, false, 0, 0.03f, 0.f, 1.f,
           false};
  r.alt = m.stick;
}
// He pounces on it.
void actPounce(World& w, float u) {
  Hiker& h = w.hiker;
  MajorState& m = M(w);
  if (u <= 0.f) {
    m.u1 = h.feet.x;
    scriptSetPose(w, POSE_CROUCH, 0.3f);
  }
  h.feet.x = m.u1 + 18.f * easeOutCubic(window(u, 0.05f, 0.4f));
  w.fx.lean = 0.25f * env(u, 0.15f, 0.4f);
  w.fx.reach_to = 0.9f;
  w.fx.reach = env(u, 0.2f, 0.35f);
  if (u >= 0.4f && m.mode == LN_LANDED) lineTo(w, LN_HELD);
  if (u >= 1.f) scriptFxRest(w);
}
// Holds it up at arm's length, the near arm out ahead and up so the fish
// hangs clear of him against the sky; looks at it and nods.
void actHoldFish(World& w, float u) {
  const float e = env(u, 0.15f, 0.12f);
  scriptSetArm(w, 0, e, 2.35f, 0.05f);
  w.fx.head = -0.2f * e + 0.24f * (0.5f - 0.5f * cosT(2.f * window(u, 0.45f, 0.95f)));
  if (u >= 1.f) scriptFxRest(w);
}
// Fish to the flat rock; fish back in hand.
void doFishOnRock(World& w, float) { lineTo(w, LN_ROCK); }
void doFishInHand(World& w, float) { lineTo(w, LN_HELD); }
// Three quick strokes of the knife (nothing drawn but the strokes).
void actGut(World& w, float u) {
  const float e = env(u, 0.12f, 0.12f);
  const float s = maxf(0.f, sinT(u * 3.f));
  w.fx.reach_to = 1.0f + 0.35f * s;
  w.fx.reach = 0.85f * e;
  w.fx.lean = 0.18f * e;
  w.fx.head = 0.15f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// One flick toward the stream: a bit arcs into the water (a ring).
void actFlickGuts(World& w, float u) {
  MajorState& m = M(w);
  const float e = env(u, 0.2f, 0.4f);
  w.fx.reach_to = lerp(1.1f, 0.2f, easeOutCubic(window(u, 0.15f, 0.4f)));
  w.fx.reach = e;
  if (u >= 0.35f && !has(m, kC3)) {
    fire(m, kC3);
    const Vec2 from = rockTop(w);
    if (Glyph* g = glyphNew(w, GL_DOT, from, (int)from.y + 30)) {
      g->slot = ACTOR;
      g->scale = 1.1f;
      g->q = from;
      g->k = 2.f;
      g->t = 0.f;
    }
  }
  if (u >= 1.f) scriptFxRest(w);
}
// Onto a stick, propped over the fire.
void actPropFish(World& w, float u) {
  MajorState& m = M(w);
  w.fx.reach_to = 1.3f;
  w.fx.reach = env(u, 0.25f, 0.3f);
  if (u >= 0.5f && m.mode == LN_HELD) {
    if (Glyph* g = bigFish(w)) g->kind = GL_NONE;
    lineTo(w, LN_NONE);
    Item& f = w.items[ITEM_FISH];
    f = Item{Vec2{w.fire.pos.x + 15.f, w.fire.pos.y + 1.f}, true, false, (int8_t)-1, 1.f, w.fire.pos.y, true, 0,
             0.17f, 0.f, 1.f, false};
  }
  if (u >= 1.f) scriptFxRest(w);
}

void tickFishGood(World& w, float dt) {
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  tickLine(w, dt);
  Glyph* f = bigFish(w);
  if (m.mode == LN_HOOKED) {   // rings where it fights
    m.u1 += dt;
    if (m.u1 > 0.55f) {
      m.u1 = 0.f;
      ringAt(w, m.v0, 0.45f);
    }
  }
  if (!f) return;
  if (m.mode == LN_LANDED && m.mt > 0.85f) {   // flopping on the bank
    const float t = m.mt - 0.85f;
    const float ph = t / 0.55f;
    const float hop = sinT(0.5f * (ph - floorf(ph)));
    f->pos = Vec2{m.p[2].x, m.p[2].y - 3.f - 6.f * hop};
    f->rot = 0.07f * hop * ((int)ph & 1 ? 1.f : -1.f);
    if ((int)ph != (int)(ph - dt / 0.55f)) f->facing = (int8_t)-f->facing;
    f->base_y = (int16_t)(m.p[2].y + 1.f);
  } else if (m.mode == LN_HELD) {   // in his hand: by the tail when he holds it up, along his hands when he carries it
    const Vec2 hand = hikerHandPos(h, 0);
    const bool up = h.target == POSE_REACH_UP;
    f->facing = (int8_t)h.facing;
    f->rot = up ? 0.25f : 0.02f;
    // Held up by the tail, hanging clear of his arm; carried out in front of him.
    f->pos = up ? Vec2{hand.x + 1.f * (float)h.facing, hand.y + 9.5f} : Vec2{hand.x + 7.f * (float)h.facing, hand.y + 1.f};
    f->base_y = (int16_t)(h.feet.y + 1.f);
  } else if (m.mode == LN_ROCK) {
    f->pos = rockTop(w);
    f->rot = 0.f;
    f->facing = -1;
    f->base_y = (int16_t)(rockTop(w).y + 18.f);
  }
  // The bit flicked into the stream.
  for (Glyph& g : m.g) {
    if (g.kind != GL_DOT || g.k < 1.5f) continue;
    g.t += dt;
    const float x = g.q.x + 52.f;
    const Vec2 to{x, waterMidY(w, x, g.q.y + 90.f) - 6.f};
    const float u = minf(1.f, g.t / 0.9f);
    g.pos = lob(g.q, to, 30.f, u);
    g.base_y = (int16_t)(to.y + 1.f);
    if (u >= 1.f) {
      ringAt(w, to, 0.5f);
      g.kind = GL_NONE;
    }
  }
}

const Step kFishGood[] = {
    FROM_REST,
    GET_ROD,
    CAST,
    pose(POSE_FISH_SIT, 1.0f, 0.4f),
    still(3.6f),
    act(actNibble, 1.2f),
    still(0.2f),
    act(actPlay, 2.8f),
    act(actLandFish, 1.0f),
    doit(doRodDown),
    act(actPounce, 0.9f),
    pose(POSE_STAND, 0.5f, 0.1f),
    pose(POSE_REACH_UP, 0.6f, 0.f),
    act(actHoldFish, 2.6f),
    still(0.5f),
    pose(POSE_CARRY, 0.5f, 0.f),
    go(AT_M3, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    doit(doFishOnRock),
    still(0.3f),
    act(actGut, 1.8f),
    act(actFlickGuts, 0.8f),
    still(0.6f),
    doit(doFishInHand),
    pose(POSE_CARRY, 0.5f, 0.f),
    go(AT_BLOW, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.6f, 0.2f),
    act(actPropFish, 1.2f),
    still(0.5f),
    pose(POSE_STAND, 0.6f, 0.2f),
    TO_SIT,
};

// ---- 16. bathe ---------------------------------------------------------------------------------
// Hat off on the rock, into the middle of the stream, sits down in it,
// scrubs, splashes, leans back content. Downstream a few fish float past
// belly up; he looks at them, sniffs his armpit, shrugs. Out, a shake, hat
// on, back to the fire.

void beginBathe(World& w) {
  MajorState& m = M(w);
  const Vec2 F = w.fire.pos;
  float cy, half;
  const float x = F.x + 66.f;
  m.p[0] = landscapeStreamSpan(w.land, x, cy, half) ? Vec2{x, cy - half - 1.f} : Vec2{x, F.y + 12.f};
  // The hat: on top of a rock close by, else on the bank beside him.
  m.p[1] = Vec2{m.p[0].x - 18.f, m.p[0].y - 1.f};
  float best = 70.f;
  for (int i = 0; i < w.land.n_rocks; ++i) {
    const LsRock& r = w.land.rocks[i];
    float x0 = r.p[0].x, x1 = r.p[0].x, top = r.p[0].y, base = r.p[0].y;
    for (const Vec2& p : r.p) {
      x0 = minf(x0, p.x);
      x1 = maxf(x1, p.x);
      top = minf(top, p.y);
      base = maxf(base, p.y);
    }
    const float cx = 0.5f * (x0 + x1);
    const float d = absf(cx - m.p[0].x) + 0.5f * absf(base - m.p[0].y);
    if (d < best && x1 - x0 > 12.f && cx < 640.f) {
      best = d;
      m.p[1] = Vec2{cx, top + 2.f};
    }
  }
  // Where he sits: in the near half of the stream, so the water runs behind
  // him as well as round him (he is taller than the stream is wide).
  const float sx = m.p[0].x + 60.f;
  m.p[2] = landscapeStreamSpan(w.land, sx, cy, half) ? Vec2{sx, cy + 0.4f * half} : Vec2{sx, m.p[0].y + 40.f};
  // Where he crouches for the hat.
  m.p[3] = Vec2{m.p[1].x + 12.f, maxf(m.p[1].y + 2.f, m.p[0].y - 6.f)};
  if (m.p[3].y > m.p[0].y) m.p[3].y = m.p[0].y;
  lineTo(w, LN_NONE);
  m.count = 0;
}

void actHatOff(World& w, float u) {
  Hiker& h = w.hiker;
  Item& hat = w.items[ITEM_HAT];
  const float e = env(u, 0.35f, 0.4f);
  scriptSetArm(w, 0, e, 2.75f, 2.0f);
  if (u >= 0.45f && h.hat_on) {
    h.hat_on = false;
    hat.held = true;
    hat.down = false;
    hat.facing = (int8_t)h.facing;
    hat.size = 1.f;
    hat.on_fire = false;
    hat.critter = 0;
  }
  if (u >= 1.f) w.fx.arm_w[0] = 0.f;
}
void doHatDown(World& w, float) {
  Item& hat = w.items[ITEM_HAT];
  hat.held = false;
  hat.down = true;
  hat.pos = M(w).p[1];
  hat.facing = 1;
}
void doHatUp(World& w, float) {
  Item& hat = w.items[ITEM_HAT];
  hat.down = false;
  hat.held = true;
}
bool packOn(const World& w) { return w.hiker.pack_on; }
void doPackOffB(World& w, float) {
  w.hiker.pack_on = false;
  w.pack.shown = true;
  w.pack.held = false;
  w.pack.pos = Vec2{w.hiker.feet.x - 14.f, w.hiker.feet.y + 1.f};
  w.pack.facing = (int8_t)w.hiker.facing;
}
void doWadeOn(World& w, float) { w.fx.wade = true; }
void doWadeOff(World& w, float) { w.fx.wade = false; }

// Scrubbing: arm circles, one arm then the other, head bobbing along.
void actScrub(World& w, float u) {
  const float e = env(u, 0.1f, 0.12f);
  const float a = u * 7.f;
  scriptSetArm(w, 0, e, 1.6f + 0.55f * sinT(a), 1.4f + 0.5f * cosT(a));
  scriptSetArm(w, 1, e, 1.4f + 0.55f * sinT(a + 0.5f), 1.6f + 0.5f * cosT(a + 0.5f));
  w.fx.head = 0.12f * e * sinT(a * 0.5f);
  w.fx.lean = 0.06f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// Splashing: both hands slap the water, three times (rings either side).
void actSplash(World& w, float u) {
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  const float e = env(u, 0.08f, 0.15f);
  const float ph = u * 3.f;
  const float f = ph - floorf(ph);
  const float up = f < 0.6f ? easeInOut(f / 0.6f) : 1.f - easeInCubic((f - 0.6f) / 0.4f);
  scriptSetArm(w, 0, e, lerp(0.6f, 1.9f, up), 0.2f);
  scriptSetArm(w, 1, e, lerp(0.5f, 1.8f, up), 0.3f);
  w.fx.lean = -0.08f * e * up;
  if (crossed(u, m.u1, 0.33f) || crossed(u, m.u1, 0.66f) || crossed(u, m.u1, 0.99f)) {
    const float wl = w.fx.wet_y > 0.f ? w.fx.wet_y : h.feet.y - 14.f;
    ringAt(w, Vec2{h.feet.x + 22.f, wl + 3.f}, 0.8f);
    ringAt(w, Vec2{h.feet.x - 20.f, wl + 5.f}, 0.7f);
    for (int k = 0; k < 3; ++k)
      dropAt(w, Vec2{h.feet.x + (float)(k - 1) * 12.f, wl - 2.f}, Vec2{(float)(k - 1) * 30.f, -120.f - 20.f * (float)k},
             wl + 2.f);
  }
  m.u1 = u >= 1.f ? 0.f : u;
  if (u >= 1.f) scriptFxRest(w);
}
// Leans back, content, hands behind his head; a long, still moment.
void actLeanBack(World& w, float u) {
  const float e = env(u, 0.1f, 0.08f);
  w.fx.lean = -0.3f * e;
  w.fx.head = -0.18f * e;
  scriptSetArm(w, 0, e, 2.9f, 2.3f);
  scriptSetArm(w, 1, e, 2.8f, 2.4f);
  if (u >= 1.f) scriptFxRest(w);
}
// He notices them: turns to look downstream and down at the water.
void actNotice(World& w, float u) {
  if (u >= 0.25f) w.hiker.facing = -1;
  w.fx.head = 0.22f * easeInOut(window(u, 0.3f, 1.f));
}
void actLookFish(World& w, float u) {
  w.fx.head = 0.22f + 0.05f * sinT(u * 1.5f);
  if (u >= 1.f) w.fx.head = 0.22f;
}
// Lifts an arm, sniffs his armpit.
void actSniff(World& w, float u) {
  const float e = env(u, 0.25f, 0.2f);
  scriptSetArm(w, 1, e, 2.55f, 1.2f);   // the far arm up
  w.fx.head = lerp(0.22f, 0.45f, e) - 0.04f * e * sinT(u * 4.f);
  w.fx.lean = 0.08f * e;
  if (u >= 1.f) {
    scriptFxRest(w);
    w.fx.head = 0.f;
  }
}
// Out of the water: a dog-shake, drops flying off him.
void actShakeOff(World& w, float u) {
  MajorState& m = M(w);
  const float e = env(u, 0.1f, 0.2f);
  const float s = sinT(u * 7.f);
  w.fx.lean = 0.12f * e * s;
  w.fx.head = 0.2f * e * sinT(u * 7.f + 0.25f);
  scriptSetArm(w, 0, e, 0.9f + 0.6f * s, 0.6f);
  scriptSetArm(w, 1, e, 0.9f - 0.6f * s, 0.6f);
  const uint32_t n = (uint32_t)(u * 14.f);
  if (n != m.count && u < 1.f) {
    m.count = (uint8_t)n;
    const Hiker& h = w.hiker;
    const float a = hash01(n, 3u), b = hash01(n, 5u);
    dropAt(w, Vec2{h.feet.x + (a - 0.5f) * 20.f, h.feet.y - h.height * (0.35f + 0.4f * b)},
           Vec2{(a - 0.5f) * 110.f, -70.f - 60.f * b}, h.feet.y + 2.f);
  }
  if (u >= 1.f) scriptFxRest(w);
}

constexpr float kDeadFishV = 15.f;

void tickBathe(World& w, float dt) {
  MajorState& m = M(w);
  m.mt += dt;
  ageGlyphs(w, dt);
  // Downstream of him the fish surface belly up, one after another, and drift off.
  if (has(m, kC1) && !has(m, kFired)) {
    fire(m, kFired);
    m.u0 = streamParamAt(w, w.hiker.feet.x);
  }
  if (has(m, kFired)) {
    const float since = m.t - m.tc;
    for (int i = 0; i < (int)m.var; ++i) {
      const float at = 1.3f * (float)i;
      // Surfaced bits live in stage[2] (no critter uses it): the shake-off
      // step reuses m.count as its drop counter, which respawned the fish.
      if (since < at || (m.stage[2] >> i) & 1u) continue;
      m.stage[2] = (uint8_t)(m.stage[2] | (1u << i));
      const float s = m.u0 + 0.32f + 0.12f * (float)i;
      const float lane = (i == 1 ? -0.35f : 0.15f) + 0.05f * (float)i;
      if (Glyph* g = glyphNew(w, GL_SHAPE, streamPoint(w, s, lane), 0)) {
        g->shape = SH_DEADFISH;
        g->facing = -1;
        g->scale = 0.f;
        g->q = Vec2{s, lane};
        g->t = 0.f;
        ringAt(w, g->pos, 0.55f);
      }
    }
    for (Glyph& g : m.g) {
      if (g.kind != GL_SHAPE || g.shape != SH_DEADFISH) continue;
      g.t += dt;
      g.scale = easeOutBack(minf(1.f, g.t / 0.6f));
      g.q.x = streamOn(w, g.q.x, kDeadFishV, dt);
      const Vec2 c = streamPoint(w, g.q.x, g.q.y);
      g.pos = Vec2{c.x, c.y - 1.f + 0.5f * sinT(g.t * 0.6f + g.q.y)};
      g.rot = turnsAlong(streamDir(w, g.q.x), -1) + 0.012f * sinT(g.t * 0.45f);
      g.base_y = (int16_t)(c.y + 2.f);
      if (g.pos.x < -16.f || g.pos.y > 486.f) g.kind = GL_NONE;
    }
  }
}

const Step kBathe[] = {
    FROM_REST,
    go(AT_M3, kStroll),
    face(LEFT),
    act(actHatOff, 1.1f),
    pose(POSE_CROUCH, 0.5f, 0.2f),
    doit(doHatDown),
    pose(POSE_STAND, 0.5f, 0.2f),
    skipUnless(packOn, 1),
    doit(doPackOffB),
    go(AT_M0, kStroll),
    face(RIGHT),
    doit(doWadeOn),
    still(0.3f),
    walk(AT_M2, kWade),
    pose(POSE_SIT, 1.2f, 0.4f),
    act(actScrub, 4.2f),
    act(actSplash, 2.4f),
    act(actLeanBack, 9.0f),
    doit(cue1),            // the fish come up downstream; he does not see them yet
    still(1.6f),
    act(actNotice, 1.0f),
    act(actLookFish, 1.8f),
    act(actSniff, 2.2f),
    act(actShrugSit, 2.0f),
    still(0.8f),
    pose(POSE_STAND, 1.0f, 0.2f),
    walk(AT_M0, kWade),
    doit(doWadeOff),
    face(RIGHT),
    act(actShakeOff, 1.6f),
    still(0.3f),
    go(AT_M3, kStroll),
    face(LEFT),
    pose(POSE_CROUCH, 0.5f, 0.2f),
    doit(doHatUp),
    pose(POSE_STAND, 0.5f, 0.1f),
    act(actHatOnM, 1.0f),
    still(0.4f),
    TO_SIT,
};

#undef GET_ROD
#undef CAST

// ---- the birds_on_water filler -----------------------------------------------------------------
// He sits on the bank; two or three birds come down on the water, bob along,
// dip their heads, flick their wings, one chases another; he watches the
// nearest; they fly off together.

constexpr float kBirdLandS = 2.4f, kBirdChaseAt = 12.f, kBirdOffAt = 19.f, kBirdWatchS = 24.f;
constexpr float kBirdSink = 3.f;   // px of the bird under the waterline

void tickBirds(World& w, float dt);

// The hiker's eyes on the nearest bird (head and facing).
void actWatchBirds(World& w, float u) {
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  float hr = 4.f;
  const Vec2 head = hikerHeadPos(h, &hr);
  float best = 1e9f, want = 0.f;
  int face = h.facing;
  for (int k = 0; k < 3; ++k) {
    if (!own(w, k)) continue;
    const Critter& c = cr(w, k);
    if (!c.visible) continue;
    const float d = dist(c.feet, head);
    if (d < best) {
      best = d;
      const float dx = absf(c.feet.x - head.x) + 20.f;
      want = maxf(-0.45f, minf(0.35f, 0.5f * (c.feet.y - head.y) / dx));
      if (absf(c.feet.x - head.x) > 12.f) face = c.feet.x < head.x ? -1 : 1;
    }
  }
  if (best > 1e8f) want = 0.f;
  m.u1 += (want - m.u1) * 0.06f;
  w.fx.head = m.u1 * env(u, 0.05f, 0.08f);
  if (face != h.facing && m.mt > 1.5f) {   // turns to follow, but not every flutter
    h.facing = face;
    m.mt = 0.f;
  }
  if (u >= 1.f) w.fx.head = 0.f;
}

const Step kBirds[] = {
    FROM_REST,
    go(AT_LISTEN, kStroll),
    face(RIGHT),
    pose(POSE_SIT, 0.9f, 0.8f),
    doit(cueGo),
    act(actWatchBirds, kBirdWatchS),
    until(gone),
    still(0.6f),
    pose(POSE_STAND, 0.9f, 0.3f),
    TO_SIT,
};

// Where bird k sits on the water (its stream parameter and lane in p[1 + k]).
Vec2 birdWater(const World& w, int k, float bob) {
  const Vec2 sl = M(w).p[1 + k];
  const Vec2 c = streamPoint(w, sl.x, sl.y);
  return Vec2{c.x, c.y + kBirdSink + bob};
}
Glyph* birdPatch(World& w, int k) {
  for (Glyph& g : M(w).g)
    if (g.kind == GL_WATER && g.n == k) return &g;
  return nullptr;
}
Vec2 bez2(Vec2 a, Vec2 c, Vec2 b, float u) {
  const float v = 1.f - u;
  return Vec2{v * v * a.x + 2.f * v * u * c.x + u * u * b.x, v * v * a.y + 2.f * v * u * c.y + u * u * b.y};
}

void tickBirds(World& w, float dt) {
  MajorState& m = M(w);
  m.mt += dt;
  ageGlyphs(w, dt);
  if (!has(m, kGo)) return;
  const float T = m.t - m.tc;   // since he sat down and they set off
  const Vec2 L = w.spine.camp.listen;
  static const float kLane[3] = {0.1f, -0.3f, 0.3f};
  for (int k = 0; k < (int)m.var && k < 3; ++k) {
    if (m.c[k] < 0 && m.stage[k] == 0) {   // not yet in: each a moment after the last
      if (T < 0.8f * (float)k) continue;
      m.p[1 + k] = Vec2{streamParamAt(w, L.x + 50.f + 38.f * (float)k), kLane[k]};
      const Vec2 from{850.f + 25.f * (float)k, 140.f + 16.f * (float)k};
      if (spawnC(w, k, CR_BIRD, from, -1, CF_BRAVE) < 0) {
        m.stage[k] = 9;   // no slot: this one stays away
        continue;
      }
      critterSetAction(cr(w, k), CA_MOVE, 0.1f);
      m.v0 = from;
      stageTo(w, k, 1);
      continue;
    }
    if (!own(w, k)) continue;
    Critter& c = cr(w, k);
    const float st = m.st[k];
    const float bob = 0.7f * sinT(T * 0.45f + 0.3f * (float)k);
    switch (m.stage[k]) {
      case 1: {   // the swoop down onto the water
        const Vec2 from{850.f + 25.f * (float)k, 140.f + 16.f * (float)k};
        const Vec2 to = birdWater(w, k, 0.f);
        const float u = minf(1.f, st / kBirdLandS);
        const Vec2 p = bez2(from, Vec2{to.x + 70.f, to.y - 40.f}, to, easeOutCubic(u));
        c.facing = p.x < c.feet.x ? -1 : 1;
        c.feet = p;
        if (u >= 1.f) {
          critterSetAction(c, CA_IDLE, 0.25f);
          ringAt(w, Vec2{to.x, to.y - kBirdSink}, 0.7f);
          if (Glyph* g = glyphNew(w, GL_WATER, to, (int)to.y + 1)) g->n = (uint8_t)k;
          stageTo(w, k, 2);
        }
        break;
      }
      case 2: {   // afloat: drifting, dipping, a wing flick; the chase; then off together
        Vec2& sl = m.p[1 + k];
        sl.x = streamOn(w, sl.x, 2.2f, dt);
        // The chase: the first goes for the second, which scoots off downstream.
        if (T > kBirdChaseAt && T < kBirdChaseAt + 1.3f && (int)m.var >= 2 && k < 2) {
          const float v = k == 0 ? -26.f : 20.f;   // the chaser paddles back up toward it
          sl.x = streamOn(w, sl.x, v, dt);
          c.facing = k == 0 ? 1 : -1;
          if (crossed(T, T - dt, kBirdChaseAt + 0.1f) || crossed(T, T - dt, kBirdChaseAt + 0.8f))
            ringAt(w, Vec2{c.feet.x, c.feet.y - kBirdSink}, 0.45f);
        } else if (T >= kBirdChaseAt + 1.3f && k < 2) {
          c.facing = -1;
        }
        c.feet = birdWater(w, k, bob);
        // Head dips (pecks at the water) and a wing flick, each bird to its own clock.
        const float d0 = 2.5f + 1.1f * (float)k, d1 = 8.5f + 0.9f * (float)k;
        const bool dip = (st > d0 && st < d0 + 1.f) || (st > d1 && st < d1 + 1.f);
        if (dip && c.action != CA_EAT) critterSetAction(c, CA_EAT, 0.25f);
        if (!dip && c.action == CA_EAT) critterSetAction(c, CA_IDLE, 0.25f);
        const float fl = 5.f + 1.7f * (float)k;
        c.fore_add = 1.1f * plateauM(st, fl, 0.12f, 0.25f) + 0.9f * plateauM(st, fl + 0.7f, 0.1f, 0.15f);
        if (crossed(st, st - dt, fl + 0.05f)) ringAt(w, Vec2{c.feet.x, c.feet.y - kBirdSink}, 0.4f);
        if (Glyph* g = birdPatch(w, k)) {
          const float wl = c.feet.y - kBirdSink;
          g->pos = Vec2{floorf(c.feet.x - 9.f), floorf(wl)};
          g->q = Vec2{ceilf(c.feet.x + 9.f), ceilf(wl + kBirdSink + 3.f)};
          g->base_y = (int16_t)(c.feet.y + 1.f);
        }
        if (T > kBirdOffAt + 0.25f * (float)k) {   // off together, upstream and up
          c.fore_add = 0.f;
          critterSetAction(c, CA_MOVE, 0.15f);
          ringAt(w, Vec2{c.feet.x, c.feet.y - kBirdSink}, 0.6f);
          if (Glyph* g = birdPatch(w, k)) g->kind = GL_NONE;
          m.p[1 + k] = c.feet;   // where it took off
          stageTo(w, k, 3);
        }
        break;
      }
      case 3: {
        const Vec2 from = m.p[1 + k];
        const Vec2 to{870.f + 20.f * (float)k, 110.f + 14.f * (float)k};
        const float u = minf(1.f, st / 2.4f);
        const Vec2 p = bez2(from, Vec2{from.x + 40.f, from.y - 110.f}, to, easeInOut(u));
        c.facing = p.x < c.feet.x ? -1 : 1;
        c.feet = p;
        if (u >= 1.f) {
          directorRelease(w, m.c[k]);
          m.c[k] = -1;
          stageTo(w, k, 9);
        }
        break;
      }
      default: break;
    }
  }
}

#undef SIT_HERE
#undef TO_SIT
#undef FROM_REST

// ---- the pool --------------------------------------------------------------------------------

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

typedef void (*MajorBeginFn)(World&);
typedef void (*MajorTickFn)(World&, float);

// Slots a major can go in (bits: 1 after setup, 2 after fire, 4 after dinner, 8 after foodbag).
constexpr uint8_t kOut = 1 | 2 | 4, kTent = 8;

// Families a major plays in (bit = SpineFamily). The evening keeps its full
// pool. The morning and midday (SPINES_WEATHER_LOOKOUT_SPEC section 1): the
// daylight majors and the stream vignettes; stargaze, firefly_jar,
// shadow_theater and raccoon_pack stay the evening's. fishing_good is left out
// of the morning (judgment call): it leaves a fish roasting over the fire for
// the evening's dinner, and the morning has no dinner to eat it in. Midday has
// it as the spec lists it (its implementer decides what lunch does with the fish).
constexpr uint8_t kEve = 1u << SF_EVENING, kMorn = 1u << SF_MORNING, kMid = 1u << SF_MIDDAY;
constexpr uint8_t kDay = kEve | kMorn | kMid;

struct MajorDef {
  const char* name;
  uint8_t seconds[2];    // per variant (0 and 1; others use 0's)
  float weight;
  uint8_t slots;
  uint8_t families;
  MajorBeginFn begin;
  MajorTickFn tick;
  const Step* steps[2];
  uint8_t n[2];
};

const MajorDef kMajors[MJ_COUNT] = {
    {"squirrel_splash", {31, 31}, 1.f, kOut, kDay, beginSplash, tickSplash, {kSplash, kSplash}, {N(kSplash), N(kSplash)}},
    {"deer_stick", {42, 42}, 1.f, kOut, kDay, beginDeer, tickDeer, {kDeer, kDeer}, {N(kDeer), N(kDeer)}},
    {"pinecone", {30, 30}, 1.f, kOut, kDay, beginCone, tickCone, {kCone, kCone}, {N(kCone), N(kCone)}},
    {"map_chase", {43, 43}, 1.f, kOut, kDay, beginMap, tickMap, {kMap, kMap}, {N(kMap), N(kMap)}},
    {"fish_leap", {23, 37}, 1.f, kOut, kDay, beginFish, tickFish, {kFish, kFish}, {N(kFish), N(kFish)}},
    {"raccoon_pack", {58, 26}, 1.f, kOut | kTent, kEve, beginRaccoon, tickRaccoon, {kRaccoonNap, kRaccoonTent},
     {N(kRaccoonNap), N(kRaccoonTent)}},
    {"ants_cookie", {46, 46}, 1.f, kOut, kDay, beginAnts, tickAnts, {kAnts, kAnts}, {N(kAnts), N(kAnts)}},
    {"shadow_theater", {44, 44}, 1.2f, kTent, kEve, beginTheater, tickTheater, {kTheater, kTheater},
     {N(kTheater), N(kTheater)}},
    {"ranger_ridge", {32, 32}, 1.f, kOut, kDay, beginRanger, tickRanger, {kRanger, kRanger}, {N(kRanger), N(kRanger)}},
    {"echo", {34, 34}, 1.f, kOut, kDay, beginEcho, tickEcho, {kEcho, kEcho}, {N(kEcho), N(kEcho)}},
    {"firefly_jar", {42, 42}, 1.5f, kOut, kEve, beginFireflies, tickFireflies, {kFireflies, kFireflies},
     {N(kFireflies), N(kFireflies)}},
    {"stargaze", {48, 48}, 1.5f, kOut, kEve, beginStars, tickStars, {kStars5, kStars5}, {N(kStars5), N(kStars5)}},
    {"fishing_small", {60, 60}, 0.6f, kOut, kDay, beginFishing, tickFishSmall, {kFishSmall, kFishSmall},
     {N(kFishSmall), N(kFishSmall)}},
    // fishing_log: plus its variant's seconds of empty scene (8..12).
    {"fishing_log", {56, 56}, 0.6f, kOut, kDay, beginFishLog, tickFishLog, {kFishLog, kFishLog}, {N(kFishLog), N(kFishLog)}},
    {"fishing_good", {50, 50}, 0.6f, 2, (uint8_t)(kEve | kMid), beginFishGood, tickFishGood, {kFishGood, kFishGood},
     {N(kFishGood), N(kFishGood)}},
    {"bathe", {54, 54}, 0.6f, 2 | 4, kDay, beginBathe, tickBathe, {kBathe, kBathe}, {N(kBathe), N(kBathe)}},
};
static_assert(MJ_COUNT == 16, "a row per major");

// The script for a major and variant (stargaze: one table per star count).
void scriptOf(int id, int var, const Step*& s, uint8_t& n) {
  if (id == MJ_STARGAZE) {
    static const Step* const kS[4] = {kStars5, kStars6, kStars7, kStars8};
    static const uint8_t kN[4] = {N(kStars5), N(kStars6), N(kStars7), N(kStars8)};
    const int k = (var < 5 ? 5 : (var > 8 ? 8 : var)) - 5;
    s = kS[k];
    n = kN[k];
    return;
  }
  const int v = var == 1 ? 1 : 0;
  s = kMajors[id].steps[v];
  n = kMajors[id].n[v];
}

#undef N

// ---- the beat ----------------------------------------------------------------------------------

void clearGlyphs(MajorState& m, bool keep_kept) {
  for (Glyph& g : m.g)
    if (!(keep_kept && g.keep)) g.kind = GL_NONE;
}

template <int I>
void majorBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  MajorState& m = w.major;
  clearGlyphs(m, true);
  m.on = true;
  m.id = w.plan.major[I];
  m.var = w.plan.major_var[I];
  for (Vec2& p : m.p) p = w.hiker.feet;
  for (int k = 0; k < 3; ++k) {
    m.c[k] = -1;
    m.stage[k] = 0;
    m.st[k] = 0.f;
  }
  m.cue = 0;
  m.t = m.tc = m.u0 = m.u1 = 0.f;
  m.count = 0;
  w.busy = true;
  const MajorDef& d = kMajors[m.id];
  d.begin(w);
  const Step* s;
  uint8_t n;
  scriptOf(m.id, m.var, s, n);
  scriptStart(w, s, n);
}

void majorUpdate(SceneCtx& ctx, float, float) {
  World& w = *ctx.w;
  MajorState& m = w.major;
  m.t += SS_TICK_DT;
  for (float& s : m.st) s += SS_TICK_DT;
  scriptRun(w);
  if (m.id < MJ_COUNT) kMajors[m.id].tick(w, SS_TICK_DT);
}

void majorEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  MajorState& m = w.major;
#ifdef SS_SPINE_TRACE
  printf("trace major_end %s var=%u step=%u/%u ticks=%u done_at=%u frac=%.3f critters_left=%d\n", majorName(m.id),
         (unsigned)m.var, (unsigned)w.spine.run.i, (unsigned)w.spine.run.n, (unsigned)w.spine.run.beat_ticks,
         (unsigned)w.spine.run.done_at,
         w.spine.run.beat_ticks ? (double)w.spine.run.done_at / (double)w.spine.run.beat_ticks : 0.0, (int)!gone(w));
#endif
  bool late = !scriptDone(w);
  for (int i = 0; i < CRITTER_MAX; ++i) {
    if (!w.crit[i].in_use || w.crit[i].tag != CT_MAJOR) continue;
    late = true;
    directorRelease(w, i);
  }
  if (late && w.spine.overruns < 255) ++w.spine.overruns;
  w.spine.run.steps = nullptr;
  // What a major leaves behind: a cone it dropped stays on the ground.
  for (Glyph& g : m.g) {
    if (g.kind != GL_SHAPE || g.shape != SH_CONE) continue;
    const int s = worldTakeBit(w, BIT_CONE);
    if (s < 0) continue;
    Bit& b = w.bits[s];
    b.resting = true;
    b.pos = g.pos;
    b.spin = 0.25f;
    b.ground_y = b.land_y = g.pos.y;
  }
  clearGlyphs(m, true);
  w.tent.wobble = 0.f;
  w.fx.stomp = 0.f;     // (the stream majors' walk and waterline; their scripts end with both off)
  w.fx.wade = false;
  w.hiker.hat_on = true;              // an overrun must not leave him hatless or holding the rod
  if (w.hiker.carry == CARRY_ROD) hikerCarry(w.hiker, CARRY_NONE);
  m.on = false;
  w.busy = false;
}

typedef void (*BeginFn)(SceneCtx&);
const BeginFn kMajorBegin[SPINE_MAJOR_MAX] = {majorBegin<0>, majorBegin<1>, majorBegin<2>, majorBegin<3>};
static_assert(SPINE_MAJOR_MAX == 4, "extend kMajorBegin");

}  // namespace

// ---- public --------------------------------------------------------------------------------------

const char* majorName(int id) { return id >= 0 && id < MJ_COUNT ? kMajors[id].name : "?"; }

int majorSeconds(int id, int var) {
  if (id < 0 || id >= MJ_COUNT) return 0;
  if (id == MJ_STARGAZE) {   // the counting grows with the stars
    const int n = var < 5 ? 5 : (var > 8 ? 8 : var);
    return 30 + (int)(kStarGap * (float)n + kRecountGap * (float)n + 0.5f);
  }
  if (id == MJ_FISHING_LOG) return kMajors[id].seconds[0] + var;
  return kMajors[id].seconds[var == 1 ? 1 : 0];
}

Beat majorBeat(const LoopPlan& p, int i) {
  const int id = p.major[i];
  return Beat{majorName(id), (float)majorSeconds(id, p.major_var[i]), kMajorBegin[i], majorUpdate, majorEnd};
}

namespace {
bool isFishing(int id) { return id == MJ_FISHING_SMALL || id == MJ_FISHING_LOG || id == MJ_FISHING_GOOD; }
}  // namespace

void majorsPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem, const MajorSlots& slots) {
  // Night: the stars are out (mood within half a step of 0 / 6) or night mode.
  float mood = in.mood - 6.f * floorf(in.mood / 6.f);
  const float dn = mood < 3.f ? mood : 6.f - mood;
  const bool night = in.night_mode || dn < 0.5f;
  const bool dusk = night || mood >= 4.5f;

  float base[MJ_COUNT];
  for (int i = 0; i < MJ_COUNT; ++i) base[i] = (kMajors[i].families >> slots.family) & 1u ? kMajors[i].weight : 0.f;
  if (!dusk) base[MJ_FIREFLY_JAR] = 0.f;
  if (!(in.night_mode || dn <= 0.3f)) base[MJ_STARGAZE] = 0.f;
  // The ring buffer: anything picked in the last MEMORY_RING picks is much
  // less likely, the more recent the pick the less likely (the square of its
  // age, at most half the weight of a fresh one).
  // The last loop's own majors (the newest last_n picks) are out unless
  // nothing else is left.
  // The raccoon's sequel: the night after it brought the shoe back (with
  // nothing to bring now) it may raid the tent side; that one is not held
  // back by the ring (it never could follow its own return otherwise).
  const bool sequel = mem.shoe_back && !mem.shoe_taken;
  for (int i = 0; i < MJ_COUNT; ++i) {
    const int age = memoryMajorAge(mem, (uint8_t)i);
    if (age < 0 || (sequel && i == MJ_RACCOON_PACK)) continue;
    const float k = (float)(1 + age) / (float)(MEMORY_RING + 1);
    base[i] *= age < (int)mem.last_n ? 1e-4f : 0.5f * k * k;
  }

  // How many: 2 to 4 (mostly 3). One rng call per statement throughout.
  static const float kHowMany[3] = {0.3f, 0.45f, 0.25f};
  const int want = 2 + rng.weighted(kHowMany, 3);
  uint8_t free_slots = slots.open;
  int n = 0;
  uint16_t have = 0;
  uint8_t pick[SPINE_MAJOR_MAX] = {}, slot[SPINE_MAJOR_MAX] = {}, var[SPINE_MAJOR_MAX] = {};
  for (int k = 0; k < SPINE_MAJOR_MAX; ++k) {
    float wt[MJ_COUNT];
    for (int i = 0; i < MJ_COUNT; ++i) {
      uint8_t allowed = kMajors[i].slots & free_slots;
      // The raccoon raids the tent side only while it has no shoe to bring
      // back; the night after it brought it back, only the tent side.
      if (i == MJ_RACCOON_PACK && mem.shoe_taken) allowed &= kOut;
      if (i == MJ_RACCOON_PACK && sequel) allowed &= kTent;
      // Fishing with a stick needs the wood pile, which only exists after the
      // firewood beat, so a rod-less fishing major cannot go in slot 1.
      if (mem.rod_lost && isFishing(i)) allowed &= (uint8_t)~1u;
      wt[i] = allowed ? base[i] : 0.f;
      for (int j = 0; j < n; ++j) {
        if (pick[j] == i) wt[i] = 0.f;
        if (isFishing(pick[j]) && isFishing(i)) wt[i] = 0.f;   // one rod: at most one fishing major a loop
      }
      // A set of majors one of the last loops played (so far, as a prefix) only if nothing else fits.
      if (n > 0 && memoryRecentSet(mem, (uint16_t)(have | (1u << i)))) wt[i] *= 1e-4f;
    }
    float sum = 0.f;
    for (float x : wt) sum += x;
    const int id = rng.weighted(wt, MJ_COUNT);
    const uint32_t r_slot = rng.next();
    const uint32_t r_var = rng.next();
    if (k >= want || sum <= 0.f) continue;
    // Past the first two, a pick is optional: never one held back by the ring
    // or the recent sets (only that one was left, say for S4).
    if (k >= 2 && wt[id] < 1e-3f) continue;
    uint8_t allowed = kMajors[id].slots & free_slots;
    if (id == MJ_RACCOON_PACK && mem.shoe_taken) allowed &= kOut;
    if (id == MJ_RACCOON_PACK && sequel) allowed &= kTent;
    int cnt = 0;
    for (int b = 0; b < 4; ++b) cnt += (allowed >> b) & 1;
    if (cnt == 0) continue;
    int choose = (int)(r_slot % (uint32_t)cnt), s = 0;
    for (int b = 0; b < 4; ++b) {
      if (!((allowed >> b) & 1)) continue;
      if (choose-- == 0) {
        s = b;
        break;
      }
    }
    free_slots = (uint8_t)(free_slots & ~(1u << s));
    uint8_t v = 0;
    if (id == MJ_FISH_LEAP) v = (r_var % 5u) < 2u ? 1 : 0;   // into the fire pit 2 times in 5
    if (id == MJ_RACCOON_PACK) v = s == 3 ? 1 : 0;           // in the tent: he is asleep
    if (id == MJ_FIREFLY_JAR) v = (uint8_t)(12u + r_var % 9u);
    if (id == MJ_STARGAZE) v = (uint8_t)(5u + r_var % 4u);
    if (id == MJ_FISHING_LOG) v = (uint8_t)(8u + r_var % 5u);   // seconds the scene sits empty
    if (id == MJ_BATHE) v = (uint8_t)(2u + r_var % 2u);         // dead fish that float past
    pick[n] = (uint8_t)id;
    have = (uint16_t)(have | (1u << id));
    slot[n] = (uint8_t)s;
    var[n] = v;
    ++n;
  }
  // Play order: by slot.
  const uint8_t* kAfter = slots.after;
  p.n_majors = 0;
  for (int s = 0; s < 4; ++s)
    for (int j = 0; j < n; ++j) {
      if (slot[j] != s) continue;
      const int i = p.n_majors++;
      p.major[i] = pick[j];
      p.major_after[i] = kAfter[s];
      p.major_var[i] = var[j];
    }
  for (int i = 0; i < SPINE_MAJOR_MAX; ++i) {
    const uint8_t a = (uint8_t)rng.rangeInt(4, 8);
    const uint8_t b = (uint8_t)rng.rangeInt(4, 8);
    if (i < p.n_majors) {
      p.major_pre_s[i] = a;
      p.major_post_s[i] = b;
    } else {
      p.major[i] = p.major_after[i] = p.major_var[i] = p.major_pre_s[i] = p.major_post_s[i] = 0;
    }
  }
}

Rect glyphBounds(const Glyph& g) {
  float r = 0.f;
  Vec2 a = g.pos, b = g.pos;
  switch (g.kind) {
    case GL_RING:
      if (g.t < 0.f || g.t >= kRingLife) return kNoRect;
      r = 16.f * g.scale + 2.f;
      return Rect{(int)floorf(g.pos.x - r), (int)floorf(g.pos.y - 0.32f * r - 2.f), (int)ceilf(g.pos.x + r) + 1,
                  (int)ceilf(g.pos.y + 0.32f * r + 2.f) + 1};
    case GL_DOT:
      if (g.scale <= 0.f) return kNoRect;
      r = g.scale + 1.f;
      break;
    case GL_SHAPE:
      if (g.scale <= 0.05f) return kNoRect;
      r = (g.shape == SH_COOKIE ? g.scale + 1.f : shapeReach(g.shape) * g.scale) + 1.5f;
      if (g.shape == SH_CONE) a.y -= 4.4f;
      break;
    case GL_ARCS:
      if (g.scale <= 0.5f || g.k <= 0.05f) return kNoRect;
      r = 0.6f * g.scale + 4.f + g.k;
      break;
    case GL_WATER:
      return Rect{(int)floorf(g.pos.x), (int)floorf(g.pos.y), (int)ceilf(g.q.x), (int)ceilf(g.q.y)};
    case GL_ANTS:
      a = Vec2{minf(g.pos.x, g.q.x), minf(g.pos.y, g.q.y)};
      b = Vec2{maxf(g.pos.x, g.q.x), maxf(g.pos.y, g.q.y)};
      r = 9.f;
      break;
    default: return kNoRect;
  }
  return Rect{(int)floorf(a.x - r) - 1, (int)floorf(a.y - r) - 1, (int)ceilf(b.x + r) + 2, (int)ceilf(b.y + r) + 2};
}

void glyphDraw(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal) {
  if (g.kind == GL_NONE || g.kind == GL_WATER || glyphBounds(g).clipped(clip).empty()) return;   // water: World draws it
  switch (g.kind) {
    case GL_RING: drawRing(g, s, clip, pal); break;
    case GL_DOT:
      if (g.slot == FIRE_CORE && g.scale > 1.2f) {   // a lantern: a warm rim round a bright core (reads on a pale sky too)
        fillCircle(s, g.pos, g.scale, pal[FIRE], clip);
        fillCircle(s, g.pos, 0.55f * g.scale, pal[FIRE_CORE], clip);
      } else {
        fillCircle(s, g.pos, g.scale, pal[(Slot)g.slot], clip);
      }
      break;
    case GL_SHAPE: drawShape(g, s, clip, pal); break;
    case GL_ARCS: drawArcs(g, s, clip, pal); break;
    case GL_ANTS: drawAnts(g, s, clip, pal); break;
    default: break;
  }
}

namespace {
bool glyphSame(const Glyph& a, const Glyph& b) {
  return a.kind == b.kind && a.shape == b.shape && a.slot == b.slot && a.facing == b.facing && a.n == b.n &&
         a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.q.x == b.q.x && a.q.y == b.q.y && a.rot == b.rot &&
         a.scale == b.scale && a.t == b.t && a.k == b.k && a.base_y == b.base_y;
}
}  // namespace

void majorsTrack(MajorState& m, Renderer& r) {
  for (int i = 0; i < GLYPH_MAX; ++i) {
    const Glyph& g = m.g[i];
    const Rect cur = g.kind == GL_NONE ? kNoRect : glyphBounds(g);
    const Rect& prev = m.g_r[i];
    const bool same = glyphSame(g, m.g_s[i]) && prev.x0 == cur.x0 && prev.y0 == cur.y0 &&
                      prev.x1 == cur.x1 && prev.y1 == cur.y1;
    if (same) continue;
    if (!prev.empty()) r.markDirtyRect(prev);
    if (!cur.empty()) r.markDirtyRect(cur);
    m.g_r[i] = cur;
    m.g_s[i] = g;
  }
}

void majorsReset(MajorState& m) {
  memset(static_cast<void*>(&m), 0, sizeof(MajorState));
  for (int8_t& c : m.c) c = -1;
}

// ---- the birds_on_water filler ---------------------------------------------------------------

void birdsFillerBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  MajorState& m = w.major;
  clearGlyphs(m, true);
  m.on = true;
  m.id = MJ_COUNT;   // not a major: majorUpdate's table is not used
  m.var = ((int)w.fire.pos.x % 3) == 0 ? 2 : 3;   // how many birds (from the loop's layout: no rng in a beat)
  for (Vec2& p : m.p) p = w.hiker.feet;
  for (int k = 0; k < 3; ++k) {
    m.c[k] = -1;
    m.stage[k] = 0;
    m.st[k] = 0.f;
  }
  m.cue = 0;
  m.t = m.tc = m.u0 = m.u1 = m.mt = 0.f;
  m.count = m.mode = 0;
  m.stick = false;
  scriptStart(w, kBirds, (uint8_t)(sizeof(kBirds) / sizeof(kBirds[0])));
}

void birdsFillerUpdate(SceneCtx& ctx, float, float) {
  World& w = *ctx.w;
  MajorState& m = w.major;
  m.t += SS_TICK_DT;
  for (float& s : m.st) s += SS_TICK_DT;
  scriptRun(w);
  tickBirds(w, SS_TICK_DT);
}

void birdsFillerEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  MajorState& m = w.major;
  bool late = !scriptDone(w);
  for (int i = 0; i < CRITTER_MAX; ++i) {
    if (!w.crit[i].in_use || w.crit[i].tag != CT_MAJOR) continue;
    late = true;
    directorRelease(w, i);
  }
  if (late && w.spine.overruns < 255) ++w.spine.overruns;
  w.spine.run.steps = nullptr;
  clearGlyphs(m, true);
  m.on = false;
}

int birdsFillerSeconds() { return 38; }

}  // namespace ss
