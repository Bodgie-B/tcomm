// The archery. See archery.h.
#include "archery.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"

namespace ss {

namespace {

const Rect kEmpty{0, 0, 0, 0};

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool finite(float v) { return v - v == 0.f; }   // false for NaN and infinity
inline float fin(float v, float otherwise) { return finite(v) ? v : otherwise; }
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }

inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
              a.y1 > b.y1 ? a.y1 : b.y1};
}

constexpr float kPi = 3.14159265f;
constexpr float kHalfPi = 1.57079633f;
constexpr float kInvTwoPi = 1.f / (2.f * kPi);

// Running float extents, turned into an integer rect with a margin for the
// antialiased fringe (as kite.cpp and props.cpp do).
struct Ext {
  float x0 = 3.0e38f, y0 = 3.0e38f, x1 = -3.0e38f, y1 = -3.0e38f;
  void pt(Vec2 p, float r = 0.f) {
    x0 = minf(x0, p.x - r);
    y0 = minf(y0, p.y - r);
    x1 = maxf(x1, p.x + r);
    y1 = maxf(y1, p.y + r);
  }
  Rect rect() const {
    if (x1 < x0) return kEmpty;
    return Rect{(int)floorf(x0) - 2, (int)floorf(y0) - 2, (int)ceilf(x1) + 2, (int)ceilf(y1) + 2};
  }
};

// ---- tuning (all PROPOSED; how it looks is UNVERIFIED) ---------------------------------

constexpr float kMaxDt = 0.1f;

// The shot's rhythm, seconds.
constexpr float kRaiseS = 0.9f;
constexpr float kDrawS = 1.3f;
constexpr float kHoldMin = 1.5f, kHoldVar = 0.7f;
constexpr float kReleaseS = 0.7f;
constexpr float kFollowMinS = 0.25f;
constexpr float kReactS = 1.6f, kReactLastS = 2.4f;
constexpr float kPauseS = 0.5f;

// Flight and landing.
constexpr float kArrowSpeed = 400.f;       // px/s along the path
constexpr float kFlightMin = 0.25f, kFlightMax = 0.9f;
constexpr float kQuiverS = 0.5f;           // "quivering for half a second"
constexpr float kQuiverHz = 6.f;
constexpr float kSlideS = 0.35f;           // PAST: the slide
constexpr float kSplitS = 0.3f;            // the halves splay apart over this
constexpr float kSplitAngle = 0.31f;       // rad, each half
constexpr float kSplitAt = 0.38f;          // the shaft stays whole this far (of its length) behind the tip
constexpr float kArcMin = 3.f, kArcMax = 70.f;   // px a path may rise above its chord
constexpr float kClearTop = 12.f;          // px a tree shot clears the target's top by

// The target (px).
constexpr float kTargetH = 38.f;           // centre above the stand's ground point
constexpr float kRimW = 1.5f;              // the darker straw edge
constexpr float kLegSpread = 13.f, kLegTop = 11.f, kLegW = 2.6f;

// The bow (px).
constexpr float kBowHalf = 25.f;           // grip to tip
constexpr float kSagRest = 7.f;            // tips behind the grip, unstrung pull
constexpr float kSagGain = 0.22f;          // limbs bend this much more per px of pull
constexpr float kBuzzPx = 2.6f;
constexpr float kShaftW = 1.3f;

// The pose (units of height; the rig's arm is kUpperL + kForeL long).
constexpr float kUpperL = 0.16f, kForeL = 0.15f;   // mirrors hiker.cpp (kUpper, kFore): keep in step
constexpr float kBowReach = 0.308f;                // bow hand from the shoulder, a hair under a straight arm
constexpr float kBackOff = 0.085f, kDropOff = 0.012f;   // string hand at the string: behind and under the bow hand
constexpr float kAnchorX = 0.055f, kAnchorY = -0.05f;   // string hand at full draw, from the shoulder: under the cheek
constexpr float kFlickX = -0.10f, kFlickY = -0.015f;    // and where the release flicks it
constexpr float kLeanAim = -0.05f, kHeadAim = 0.05f;
constexpr float kStanceThigh[2] = {0.17f, -0.15f};
constexpr float kStanceKnee = 0.05f;

// How high the bow arm points, per kind (rad above level).
constexpr float kElev[SHOT_KINDS] = {0.02f, 0.14f, 0.30f, 0.10f, 0.10f, 0.10f};

// ---- small maths ----------------------------------------------------------------------------

// atan(x) for |x| <= 1: an odd minimax polynomial, error under 1e-5 rad. Pure
// arithmetic and sqrtf, so the PC and the board agree (no libm atan2f).
float atanUnit(float x) {
  const float x2 = x * x;
  return x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f + x2 * 0.0208351f))));
}
float atan2A(float y, float x) {
  const float ax = fabsf(x), ay = fabsf(y);
  if (ax < 1.0e-9f && ay < 1.0e-9f) return 0.f;
  float r = ay <= ax ? atanUnit(ay / ax) : kHalfPi - atanUnit(ax / ay);
  if (x < 0.f) r = kPi - r;
  return y < 0.f ? -r : r;
}
float wrapPi(float a) {
  a -= 2.f * kPi * floorf((a + kPi) * kInvTwoPi);
  return a;
}

// Blend angles the short way round (an arm going from folded back to raised must not sweep through the body).
float lerpA(float a, float b, float t) { return a + wrapPi(b - a) * t; }

inline Vec2 unit(Vec2 v, Vec2 otherwise) {
  const float n = sqrtf(v.x * v.x + v.y * v.y);
  if (n < 1.0e-6f) return otherwise;
  return Vec2{v.x / n, v.y / n};
}
inline Vec2 rotate(Vec2 v, float rad) {
  const float c = cosT(rad * kInvTwoPi), s = sinT(rad * kInvTwoPi);
  return Vec2{v.x * c - v.y * s, v.x * s + v.y * c};
}

// ---- the arm: two-link IK in the rig's body frame (x forward, y down, from the shoulder) -----------

// The shoulder and elbow angles (rig convention: absolute from straight down,
// + forward; forearm = shoulder + elbow) that put the hand at (hx, hy). The
// elbow is placed on the upper side of the line, so a bent arm folds up and
// back (the draw elbow). Out-of-reach targets are pulled in.
void solveArm(float hx, float hy, float& sh, float& el) {
  float d = sqrtf(hx * hx + hy * hy);
  const Vec2 u = unit(Vec2{hx, hy}, Vec2{1.f, 0.f});
  d = clampf(d, 0.03f, kUpperL + kForeL - 0.002f);
  const float a = (kUpperL * kUpperL - kForeL * kForeL + d * d) / (2.f * d);
  const float h = sqrtf(maxf(0.f, kUpperL * kUpperL - a * a));
  const Vec2 n{u.y, -u.x};   // for a hand straight ahead: up
  const Vec2 e{a * u.x + h * n.x, a * u.y + h * n.y};
  const Vec2 hand{u.x * d, u.y * d};
  sh = atan2A(e.x, e.y);
  el = wrapPi(atan2A(hand.x - e.x, hand.y - e.y) - sh);
}

// ---- the looks: everything a draw reads, quantized -----------------------------------------------------

int16_t qh(float v) {   // half pixels
  if (!finite(v)) return 0;
  return (int16_t)floorf(clampf(v, -15000.f, 15000.f) * 2.f + 0.5f);
}
inline float fromH(int16_t v) { return (float)v * 0.5f; }

struct TargetLook {
  uint8_t on;
  int16_t x, y;   // the stand's ground point, whole pixels
};

struct BowLook {
  uint8_t on, arrow;
  int8_t dir;
  int8_t tilt;        // 1/64 rad, + tips the top toward the target
  uint8_t sag;        // quarter px: how far the tips are behind the grip
  int16_t bx, by;     // the grip (the bow hand), half px
  int16_t nx, ny;     // the nock: where the string is pulled to
  int16_t tx, ty;     // the nocked arrow's tip
};

struct ArrowLook {
  uint8_t mode;       // 0 none, 1 whole, 2 split
  uint8_t behind;     // drawn behind the target
  uint8_t spread;     // split: half-angle, 1/64 rad
  int8_t dx, dy;      // direction tail to tip, x120
  int16_t tx, ty;     // the tip, half px
};
constexpr float kDirQ = 120.f;

void makeTargetLook(const ArcheryState& a, TargetLook& lk) {
  memset(&lk, 0, sizeof lk);
  if (!a.target_on) return;
  lk.on = 1;
  lk.x = (int16_t)floorf(clampf(fin(a.target.x, 0.f), -15000.f, 15000.f) + 0.5f);
  lk.y = (int16_t)floorf(clampf(fin(a.target.y, 0.f), -15000.f, 15000.f) + 0.5f);
}

// The bow, solved from the state: the string, the nocked arrow, their frame.
struct BowSolve {
  Vec2 B;            // grip
  Vec2 w, f;         // up along the bow, and toward the target (unit)
  float sag;         // px
  float tilt;        // rad
  Vec2 nock;
  Vec2 tip;          // nocked arrow's tip
  Vec2 dirv;         // unit, nock to tip
};

void bowSolve(const ArcheryState& a, BowSolve& s) {
  const float d = (float)a.dir;
  const Vec2 B{fin(a.bow_hand.x, 0.f), fin(a.bow_hand.y, 0.f)};
  const Vec2 S{fin(a.string_hand.x, 0.f), fin(a.string_hand.y, 0.f)};
  s.B = B;
  s.tilt = (float)(int)floorf(clampf(0.03f * a.aim + 0.22f * a.flick, -1.9f, 1.9f) * 64.f + 0.5f) / 64.f;
  const float st = sinT(s.tilt * kInvTwoPi), ct = cosT(s.tilt * kInvTwoPi);
  s.w = Vec2{d * st, -ct};
  s.f = Vec2{d * ct, st};
  const float sx = S.x - B.x, sy = S.y - B.y;
  float nf = sx * s.f.x + sy * s.f.y;       // negative: behind the grip
  const float nw = clampf(sx * s.w.x + sy * s.w.y, -0.6f * kBowHalf, 0.6f * kBowHalf);
  if (a.nocked) {
    const float pull = maxf(0.f, -nf - kSagRest);
    s.sag = kSagRest + kSagGain * pull;
    nf = minf(nf, -s.sag);
    s.nock = Vec2{B.x + s.f.x * nf + s.w.x * nw, B.y + s.f.y * nf + s.w.y * nw};
  } else {
    s.sag = kSagRest;
    const float off = -s.sag + kBuzzPx * a.buzz;
    s.nock = Vec2{B.x + s.f.x * off, B.y + s.f.y * off};
  }
  const Vec2 shelf{B.x, B.y - 1.2f};
  s.dirv = unit(Vec2{shelf.x - s.nock.x, shelf.y - s.nock.y}, Vec2{d, 0.f});
  if (sqrtf((shelf.x - s.nock.x) * (shelf.x - s.nock.x) + (shelf.y - s.nock.y) * (shelf.y - s.nock.y)) < 3.f) s.dirv = Vec2{d, 0.f};
  s.tip = Vec2{s.nock.x + s.dirv.x * kArcheryArrowLen, s.nock.y + s.dirv.y * kArcheryArrowLen};
}

void makeBowLook(const ArcheryState& a, BowLook& lk) {
  memset(&lk, 0, sizeof lk);
  if (!a.bow_on) return;
  BowSolve s;
  bowSolve(a, s);
  lk.on = 1;
  lk.dir = a.dir < 0 ? (int8_t)-1 : (int8_t)1;
  lk.tilt = (int8_t)floorf(s.tilt * 64.f + 0.5f);
  lk.sag = (uint8_t)clampf(s.sag * 4.f + 0.5f, 0.f, 255.f);
  lk.bx = qh(s.B.x);
  lk.by = qh(s.B.y);
  lk.nx = qh(s.nock.x);
  lk.ny = qh(s.nock.y);
  if (a.nocked && a.aim > 0.35f) {
    lk.arrow = 1;
    lk.tx = qh(s.tip.x);
    lk.ty = qh(s.tip.y);
  }
}

void makeArrowLook(const ArcheryArrow& r, ArrowLook& lk) {
  memset(&lk, 0, sizeof lk);
  if (r.mode == ARROW_FREE) return;
  lk.mode = r.mode == ARROW_SPLIT ? 2 : 1;
  lk.behind = r.end == AEND_PAST ? 1 : 0;
  lk.tx = qh(r.tip.x);
  lk.ty = qh(r.tip.y);
  const Vec2 d = unit(r.dir, Vec2{1.f, 0.f});
  lk.dx = (int8_t)floorf(clampf(d.x, -1.f, 1.f) * kDirQ + 0.5f);
  lk.dy = (int8_t)floorf(clampf(d.y, -1.f, 1.f) * kDirQ + 0.5f);
  if (lk.mode == 2) lk.spread = (uint8_t)clampf(r.spread * 64.f + 0.5f, 0.f, 63.f);
}

// ---- the geometry, from a Look alone ---------------------------------------------------------------------

Vec2 targetCentre(const TargetLook& lk) { return Vec2{(float)lk.x + 0.5f, (float)lk.y - kTargetH + 0.5f}; }

Rect targetRect(const TargetLook& lk) {
  if (!lk.on) return kEmpty;
  Ext e;
  e.pt(targetCentre(lk), kArcheryTargetR + 1.f);
  e.pt(Vec2{(float)lk.x + 0.5f - kLegSpread, (float)lk.y + 0.5f}, kLegW);
  e.pt(Vec2{(float)lk.x + 0.5f + kLegSpread, (float)lk.y + 0.5f}, kLegW);
  return e.rect();
}

constexpr int kLimbN = 9;
struct BowGeom {
  Vec2 limb[kLimbN];
  float hw[kLimbN];
  Vec2 top, bot, nock, tail, tip;
  bool arrow;
};

void makeBowGeom(const BowLook& lk, BowGeom& g) {
  const float d = (float)lk.dir;
  const float tilt = (float)lk.tilt / 64.f;
  const float st = sinT(tilt * kInvTwoPi), ct = cosT(tilt * kInvTwoPi);
  const Vec2 w{d * st, -ct}, f{d * ct, st};
  const Vec2 B{fromH(lk.bx), fromH(lk.by)};
  const float sag = (float)lk.sag * 0.25f;
  for (int i = 0; i < kLimbN; ++i) {
    const float u = -1.f + 2.f * (float)i / (float)(kLimbN - 1);
    g.limb[i] = Vec2{B.x + w.x * kBowHalf * u - f.x * sag * u * u, B.y + w.y * kBowHalf * u - f.y * sag * u * u};
    g.hw[i] = 0.7f + 0.8f * (1.f - fabsf(u));
  }
  g.top = g.limb[kLimbN - 1];
  g.bot = g.limb[0];
  g.nock = Vec2{fromH(lk.nx), fromH(lk.ny)};
  g.arrow = lk.arrow != 0;
  g.tail = g.nock;
  g.tip = Vec2{fromH(lk.tx), fromH(lk.ty)};
}

Rect bowRect(const BowLook& lk) {
  if (!lk.on) return kEmpty;
  BowGeom g;
  makeBowGeom(lk, g);
  Ext e;
  for (int i = 0; i < kLimbN; ++i) e.pt(g.limb[i], g.hw[i] + 1.f);
  e.pt(g.nock, 1.5f);
  if (g.arrow) {
    e.pt(g.tail, 4.f);
    e.pt(g.tip, 1.5f);
  }
  return e.rect();
}

struct ArrowGeom {
  Vec2 tip, tail;          // whole arrow
  Vec2 q, tail_a, tail_b;  // split: where the halves part, and their tails
};

void makeArrowGeom(const ArrowLook& lk, ArrowGeom& g) {
  const Vec2 d = unit(Vec2{(float)lk.dx / kDirQ, (float)lk.dy / kDirQ}, Vec2{1.f, 0.f});
  g.tip = Vec2{fromH(lk.tx), fromH(lk.ty)};
  g.tail = Vec2{g.tip.x - d.x * kArcheryArrowLen, g.tip.y - d.y * kArcheryArrowLen};
  if (lk.mode == 2) {
    const float whole = kSplitAt * kArcheryArrowLen, rest = kArcheryArrowLen - whole;
    g.q = Vec2{g.tip.x - d.x * whole, g.tip.y - d.y * whole};
    const float sp = (float)lk.spread / 64.f;
    const Vec2 back{-d.x, -d.y};
    const Vec2 ra = rotate(back, sp), rb = rotate(back, -sp);
    g.tail_a = Vec2{g.q.x + ra.x * rest, g.q.y + ra.y * rest};
    g.tail_b = Vec2{g.q.x + rb.x * rest, g.q.y + rb.y * rest};
  }
}

Rect arrowRect(const ArrowLook& lk) {
  if (lk.mode == 0) return kEmpty;
  ArrowGeom g;
  makeArrowGeom(lk, g);
  Ext e;
  e.pt(g.tip, 1.5f);
  if (lk.mode == 2) {
    e.pt(g.q, 1.5f);
    e.pt(g.tail_a, 4.f);
    e.pt(g.tail_b, 4.f);
  } else {
    e.pt(g.tail, 4.f);
  }
  return e.rect();
}

// ---- the arrow: flight, stick, quiver -------------------------------------------------------------------

// The point and direction (unit, tail to tip) on the flight path at s in 0..1.
void flightAt(const ArcheryArrow& r, float s, Vec2& p, Vec2& d) {
  s = clamp01(s);
  const float dx = r.to.x - r.from.x, dy = r.to.y - r.from.y;
  p = Vec2{r.from.x + dx * s, r.from.y + dy * s - 4.f * r.arc * s * (1.f - s)};
  d = unit(Vec2{dx, dy - 4.f * r.arc * (1.f - 2.f * s)}, Vec2{dx < 0.f ? -1.f : 1.f, 0.f});
}

float settleTime(const ArcheryArrow& r) { return (r.end == AEND_PAST ? kSlideS : 0.f) + kQuiverS; }

// Where a landed arrow is `since` seconds after it landed: the slide, then the quiver.
void stuckAt(const ArcheryArrow& r, float since, Vec2& tip, Vec2& d) {
  tip = r.to;
  d = r.dir_end;
  float lq = since;
  if (r.end == AEND_PAST) {   // skids on, and the tail settles toward the ground
    const float side = r.to.x >= r.from.x ? 1.f : -1.f;
    tip.x += side * r.slide * easeOutCubic(window(since, 0.f, kSlideS));
    const Vec2 flat{side, 0.14f};
    const float k = easeInOut(window(since, 0.f, kSlideS));
    d = unit(Vec2{lerp(d.x, flat.x, k), lerp(d.y, flat.y, k)}, flat);
    lq = since - kSlideS;
  }
  if (lq > 0.f && lq < kQuiverS) {
    const float env = 1.f - lq / kQuiverS;
    d = rotate(d, r.amp * env * env * sinT(kQuiverHz * lq));
  }
}

float landingAmp(uint8_t end) {
  switch (end) {
    case AEND_TARGET: return 0.16f;
    case AEND_GROUND: return 0.20f;
    case AEND_PAST: return 0.08f;
    default: return 0.14f;   // the trunk
  }
}

// ---- the arrows --------------------------------------------------------------------------------------------

void land(ArcheryState& a, int i) {
  ArcheryArrow& r = a.arrows[i];
  r.mode = ARROW_STUCK;
  a.events = (uint8_t)(a.events | AEV_LANDED);
  a.last_end = r.end;
  a.last_ring = r.ring;
  if (r.victim >= 0 && r.victim < ARCHERY_ARROWS && r.victim != i) {
    ArcheryArrow& v = a.arrows[r.victim];
    if (v.mode == ARROW_STUCK) {
      v.mode = ARROW_SPLIT;
      v.split_t = 0.f;
      v.spread = 0.f;
      a.events = (uint8_t)(a.events | AEV_SPLIT);
    }
  }
}

void stepArrow(ArcheryState& a, int i, float dt) {
  ArcheryArrow& r = a.arrows[i];
  if (r.mode == ARROW_FREE) return;
  r.t += dt;
  if (r.mode == ARROW_FLY) {
    if (r.t >= r.flight) land(a, i);
    else flightAt(r, r.t / r.flight, r.tip, r.dir);
  }
  if (r.mode == ARROW_STUCK || r.mode == ARROW_SPLIT) stuckAt(r, r.t - r.flight, r.tip, r.dir);
  if (r.mode == ARROW_SPLIT) {
    r.split_t += dt;
    r.spread = kSplitAngle * easeOutBack(window(r.split_t, 0.f, kSplitS));
  }
}

bool arrowSettled(const ArcheryArrow& r) {
  switch (r.mode) {
    case ARROW_FLY: return false;
    case ARROW_STUCK: return r.t - r.flight >= settleTime(r);
    case ARROW_SPLIT: return r.split_t >= kSplitS;
    default: return true;
  }
}

// The arrow leaves the string. False when all the slots are in use.
bool release(ArcheryState& a) {
  int slot = -1;
  for (int i = 0; i < ARCHERY_ARROWS && slot < 0; ++i)
    if (a.arrows[i].mode == ARROW_FREE) slot = i;
  if (slot < 0) return false;
  BowSolve bs;
  bowSolve(a, bs);
  const uint8_t kind = a.kinds[a.shot];
  const float j0 = clamp01(a.jit[a.shot][0]), j1 = clamp01(a.jit[a.shot][1]);
  const float dir = (float)a.dir;
  const Vec2 c = archeryTargetCenter(a);
  const float tx = floorf(clampf(fin(a.target.x, 0.f), -15000.f, 15000.f) + 0.5f);

  ArcheryArrow r;
  memset(&r, 0, sizeof r);
  r.victim = -1;
  r.from = bs.tip;
  float arc = 0.f;
  const float reach = maxf(40.f, dir * (c.x - bs.tip.x));   // how far off the target is, along the shot
  Vec2 to = c;
  uint8_t end = AEND_TARGET;
  uint8_t k = kind;
  if (k == SHOT_SPLIT) {   // the last bullseye arrow, if there is one
    int v = -1;
    for (int i = 0; i < ARCHERY_ARROWS; ++i)
      if (a.arrows[i].mode == ARROW_STUCK && a.arrows[i].end == AEND_TARGET && a.arrows[i].ring == 0) v = i;
    if (v >= 0) {
      r.victim = (int8_t)v;
      to = Vec2{a.arrows[v].to.x + 0.3f * dir, a.arrows[v].to.y + 0.2f};
      arc = a.arrows[v].arc + 0.4f;
    } else {
      k = SHOT_BULLSEYE;
    }
  }
  switch (k) {
    case SHOT_SHORT:
      end = AEND_GROUND;
      to = Vec2{bs.tip.x + dir * maxf(30.f, reach * (0.5f + 0.18f * j0)), a.ground_y};
      arc = -1.f;
      break;
    case SHOT_PAST:
      end = AEND_PAST;
      to = Vec2{tx + dir * (32.f + 20.f * j0), a.ground_y};
      arc = -1.f;
      r.slide = 11.f + 6.f * j1;
      break;
    case SHOT_TREE:
      end = AEND_TREE;
      to = a.trunk;
      arc = 6.f + 0.04f * reach;
      {   // clear the target's top where the path passes it
        const float x0 = bs.tip.x, x1 = to.x;
        if (fabsf(x1 - x0) > 1.f) {
          const float st = (c.x - x0) / (x1 - x0);
          if (st > 0.02f && st < 0.98f) {
            const float top = c.y - kArcheryTargetR - kClearTop;
            const float chord = bs.tip.y + (to.y - bs.tip.y) * st;
            arc = maxf(arc, (chord - top) / (4.f * st * (1.f - st)));
          }
        }
      }
      arc = minf(arc, kArcMax);
      break;
    case SHOT_BULLSEYE:
      if (r.victim < 0) {
        const float rad = 0.5f + 2.9f * j0;
        to = Vec2{c.x + rad * cosT(j1), c.y + rad * sinT(j1)};
        arc = -1.f;
      }
      break;
    case SHOT_RING: {
      const float rad = 5.4f + 6.4f * j0;
      to = Vec2{c.x + rad * cosT(j1), c.y + rad * sinT(j1)};
      arc = -1.f;
      break;
    }
    default: break;
  }
  if (arc < 0.f) {   // leave along the nocked shaft: the path starts at the arrow's own slope
    const float span = dir * (to.x - bs.tip.x);
    const float slope = bs.dirv.y / maxf(0.3f, bs.dirv.x * dir);
    arc = span > 8.f ? clampf((to.y - bs.tip.y - slope * span) * 0.25f, kArcMin, kArcMax) : kArcMin;
  }
  r.to = to;
  r.arc = arc;
  r.end = end;
  r.ring = end == AEND_TARGET ? (uint8_t)archeryRingAt(a, to) : (uint8_t)4;
  r.amp = r.victim >= 0 ? 0.10f : landingAmp(end);
  const float dx = to.x - r.from.x, dy = to.y - r.from.y;
  r.flight = clampf(sqrtf(dx * dx + dy * dy) / kArrowSpeed, kFlightMin, kFlightMax);
  Vec2 p;
  flightAt(r, 1.f, p, r.dir_end);
  flightAt(r, 0.f, r.tip, r.dir);
  r.mode = ARROW_FLY;
  a.arrows[slot] = r;
  a.cur = (uint8_t)slot;
  return true;
}

// ---- the shot -----------------------------------------------------------------------------------------------

void enter(ArcheryState& a, uint8_t phase) {
  a.phase = phase;
  a.t = 0.f;
}

void beginShot(ArcheryState& a) {
  const uint8_t kind = a.kinds[a.shot];
  a.elev = kElev[kind < SHOT_KINDS ? kind : (uint8_t)SHOT_BULLSEYE];
  a.hold_len = kHoldMin + kHoldVar * clamp01(a.jit[a.shot][2]);
  a.nocked = 1;
  a.bow_on = 1;
  a.react = 0;
  a.cur = ARCHERY_ARROWS;
  enter(a, AP_RAISE);
}

bool arrowsSettled(const ArcheryState& a) {
  for (int i = 0; i < ARCHERY_ARROWS; ++i)
    if (!arrowSettled(a.arrows[i])) return false;
  return true;
}

void stepShot(ArcheryState& a, float dt) {
  a.t += dt;
  const float t = a.t;
  switch (a.phase) {
    case AP_RAISE:
      a.aim = easeInOut(window(t, 0.f, kRaiseS));
      a.draw = a.shake = a.flick = a.buzz = 0.f;
      if (t >= kRaiseS) enter(a, AP_DRAW);
      break;
    case AP_DRAW:
      a.aim = 1.f;
      a.draw = easeInOut(window(t, 0.f, kDrawS));
      a.shake = 0.3f * a.draw;
      if (t >= kDrawS) enter(a, AP_HOLD);
      break;
    case AP_HOLD:
      a.aim = a.draw = 1.f;
      a.shake = 0.4f + 0.6f * window(t, 0.f, a.hold_len);
      if (t >= a.hold_len) {
        release(a);
        a.nocked = 0;
        a.events = (uint8_t)(a.events | AEV_RELEASED);
        a.shake = 0.f;
        a.buzz = 0.f;
        enter(a, AP_RELEASE);
      }
      break;
    case AP_RELEASE: {   // the hand flicks back, the bow arm follows through, the string buzzes
      a.flick = easeInOut(window(t, 0.f, 0.08f)) * (1.f - easeInOut(window(t, 0.08f, kReleaseS)));
      const float env = 1.f - window(t, 0.f, 0.5f);
      a.buzz = env * env * sinT(12.f * t);
      if (t >= kReleaseS) {
        a.flick = a.buzz = 0.f;
        enter(a, AP_FOLLOW);
      }
      break;
    }
    case AP_FOLLOW: {   // he watches it in
      const bool in_air = a.cur < ARCHERY_ARROWS && a.arrows[a.cur].mode == ARROW_FLY;
      if (!in_air && t >= kFollowMinS) {
        a.react = a.last_end == AEND_TARGET ? (uint8_t)1 : (uint8_t)2;
        enter(a, AP_REACT);
      }
      break;
    }
    case AP_REACT: {
      const bool last = a.shot + 1 >= a.n_queue;
      const float len = last ? kReactLastS : kReactS;
      const float env = easeInOut(window(t, 0.35f, 0.9f)) * (1.f - easeInOut(window(t, len - 0.7f, len)));
      a.aim = 1.f - easeInOut(window(t, 0.25f, 0.95f));
      a.proud = a.react == 1 ? env : 0.f;
      a.slump = a.react == 2 ? env : 0.f;
      if (t >= len) {
        a.aim = a.proud = a.slump = a.draw = 0.f;
        enter(a, AP_PAUSE);
      }
      break;
    }
    case AP_PAUSE:
      if (t >= kPauseS) {
        ++a.shot;
        if (a.shot < a.n_queue) beginShot(a);
        else enter(a, AP_IDLE);
      }
      break;
    default:   // AP_IDLE
      a.aim = a.draw = a.flick = a.buzz = a.shake = a.proud = a.slump = 0.f;
      if (a.shot < a.n_queue) beginShot(a);
      break;
  }
}

bool queueShot(ArcheryState& a, uint8_t kind, float j0, float j1, float j2) {
  if (kind >= SHOT_KINDS) return false;
  if (a.phase == AP_IDLE && a.shot >= a.n_queue) a.n_queue = a.shot = 0;   // all played: start a new run
  if (a.n_queue >= ARCHERY_ARROWS) return false;
  int free_slots = 0;
  for (int i = 0; i < ARCHERY_ARROWS; ++i) free_slots += a.arrows[i].mode == ARROW_FREE ? 1 : 0;
  if ((int)(a.n_queue - a.shot) >= free_slots) return false;
  a.kinds[a.n_queue] = kind;
  a.jit[a.n_queue][0] = j0;
  a.jit[a.n_queue][1] = j1;
  a.jit[a.n_queue][2] = j2;
  ++a.n_queue;
  a.bow_on = 1;
  return true;
}

}  // namespace

// ---- commands -------------------------------------------------------------------------------------------------

void archeryInit(ArcheryState& a, int dir, Vec2 target, Vec2 trunk) {
  memset(&a, 0, sizeof a);
  a.phase = AP_IDLE;
  a.dir = dir < 0 ? (int8_t)-1 : (int8_t)1;
  a.target_on = 1;
  a.target = target;
  a.trunk = trunk;
  a.ground_y = target.y;
  a.last_end = AEND_NONE;
  a.last_ring = 4;
  a.was_done = 1;
  a.cur = ARCHERY_ARROWS;
  for (int i = 0; i < ARCHERY_ARROWS; ++i) a.arrows[i].victim = -1;
}

void archeryFace(ArcheryState& a, int dir) { a.dir = dir < 0 ? (int8_t)-1 : (int8_t)1; }
void archeryEquip(ArcheryState& a, bool on) { a.bow_on = on ? 1 : 0; }
void archeryShowTarget(ArcheryState& a, bool on) { a.target_on = on ? 1 : 0; }

void archeryClear(ArcheryState& a) {
  for (int i = 0; i < ARCHERY_ARROWS; ++i) {
    memset(&a.arrows[i], 0, sizeof a.arrows[i]);
    a.arrows[i].victim = -1;
  }
  a.phase = AP_IDLE;
  a.n_queue = a.shot = 0;
  a.nocked = 0;
  a.react = 0;
  a.cur = ARCHERY_ARROWS;
  a.t = 0.f;
  a.aim = a.draw = a.flick = a.buzz = a.shake = a.proud = a.slump = 0.f;
  a.last_end = AEND_NONE;
  a.last_ring = 4;
  a.events = 0;
  a.was_done = 1;
}

bool archeryShoot(ArcheryState& a, uint8_t kind, Rng& rng) {
  const float j0 = rng.uniform();
  const float j1 = rng.uniform();
  const float j2 = rng.uniform();
  return queueShot(a, kind, j0, j1, j2);
}

bool archeryPlay(ArcheryState& a, uint8_t variant, Rng& rng) {
  float j[4][3];
  for (int i = 0; i < 4; ++i) {
    j[i][0] = rng.uniform();
    j[i][1] = rng.uniform();
    j[i][2] = rng.uniform();
  }
  if (variant >= ARCH_VARIANTS) return false;
  archeryClear(a);
  static const uint8_t kA[3] = {SHOT_SHORT, SHOT_PAST, SHOT_BULLSEYE};
  static const uint8_t kB[1] = {SHOT_TREE};
  static const uint8_t kC[2] = {SHOT_BULLSEYE, SHOT_SPLIT};
  const uint8_t* kinds = variant == ARCH_A ? kA : (variant == ARCH_B ? kB : kC);
  const int n = variant == ARCH_A ? 3 : (variant == ARCH_B ? 1 : 2);
  bool ok = true;
  for (int i = 0; i < n; ++i) ok = queueShot(a, kinds[i], j[i][0], j[i][1], j[i][2]) && ok;
  return ok;
}

bool archeryBusy(const ArcheryState& a) { return a.phase != AP_IDLE || a.shot < a.n_queue; }

bool archeryDone(const ArcheryState& a) {
  return a.phase == AP_IDLE && a.shot >= a.n_queue && a.proud <= 0.f && a.slump <= 0.f && arrowsSettled(a);
}

Vec2 archeryTargetCenter(const ArcheryState& a) {
  TargetLook lk;   // (the plan needs a centre even while the target is hidden)
  memset(&lk, 0, sizeof lk);
  lk.on = 1;
  lk.x = (int16_t)floorf(clampf(fin(a.target.x, 0.f), -15000.f, 15000.f) + 0.5f);
  lk.y = (int16_t)floorf(clampf(fin(a.target.y, 0.f), -15000.f, 15000.f) + 0.5f);
  return targetCentre(lk);
}

int archeryRingAt(const ArcheryState& a, Vec2 p) {
  const Vec2 c = archeryTargetCenter(a);
  const float dx = p.x - c.x, dy = p.y - c.y;
  const float d2 = dx * dx + dy * dy;
  if (d2 < ARCHERY_R_INNER * ARCHERY_R_INNER) return 0;
  if (d2 < ARCHERY_R_MID * ARCHERY_R_MID) return 1;
  if (d2 < ARCHERY_R_OUTER * ARCHERY_R_OUTER) return 2;
  if (d2 < (kArcheryTargetR - kRimW) * (kArcheryTargetR - kRimW)) return 3;
  return 4;
}

// ---- the step ---------------------------------------------------------------------------------------------------

void archeryStep(ArcheryState& a, Vec2 bow_hand, Vec2 string_hand, float dt) {
  if (!finite(dt) || dt < 0.f) dt = 0.f;
  if (dt > kMaxDt) dt = kMaxDt;
  if (!finite(bow_hand.x) || !finite(bow_hand.y)) bow_hand = a.bow_hand;
  if (!finite(string_hand.x) || !finite(string_hand.y)) string_hand = a.string_hand;
  a.events = 0;
  a.bow_hand = bow_hand;
  a.string_hand = string_hand;
  a.ph_a += dt * 6.3f;
  a.ph_a -= floorf(a.ph_a);
  a.ph_b += dt * 2.3f;
  a.ph_b -= floorf(a.ph_b);

  for (int i = 0; i < ARCHERY_ARROWS; ++i) stepArrow(a, i, dt);
  stepShot(a, dt);

  const bool done = archeryDone(a);
  if (done && !a.was_done) a.events = (uint8_t)(a.events | AEV_DONE);
  a.was_done = done ? (uint8_t)1 : (uint8_t)0;
}

// ---- the pose --------------------------------------------------------------------------------------------------------

void archeryBend(HikerPose& p, const ArcheryPoseIn& in) {
  const float aim = clamp01(fin(in.aim, 0.f));
  if (aim > 0.f) {
    const float draw = clamp01(fin(in.draw, 0.f)), flick = clamp01(fin(in.flick, 0.f)), shake = clamp01(fin(in.shake, 0.f));
    const float ph_a = fin(in.ph_a, 0.f), ph_b = fin(in.ph_b, 0.f);
    // Feet apart, a touch of lean back, the head level with the arrow.
    p.lean = lerp(p.lean, kLeanAim, aim);
    p.head = lerp(p.head, kHeadAim, aim);
    for (int i = 0; i < 2; ++i) {
      p.thigh[i] = lerp(p.thigh[i], kStanceThigh[i], aim);
      p.knee[i] = lerp(p.knee[i], kStanceKnee, aim);
    }
    // The bow arm: straight out toward the target, a little above level, trembling.
    const float tr = shake * (0.007f * sinT(ph_a) + 0.004f * sinT(ph_b + 0.3f));
    const float e = clampf(fin(in.elev, 0.f), -0.5f, 0.8f) + tr + 0.12f * flick;
    p.shoulder[0] = lerpA(p.shoulder[0], kHalfPi + e, aim);
    p.elbow[0] = lerpA(p.elbow[0], 0.f, aim);
    // The string hand: from the string (behind and under the bow hand) back to the
    // cheek; flicked back on the release. Both ends are solved for the elbow.
    const float ce = cosT(e * kInvTwoPi), se = sinT(e * kInvTwoPi);
    const float bx = kBowReach * ce, by = -kBowReach * se;
    const float h0x = bx - kBackOff * ce, h0y = by + kBackOff * se + kDropOff;
    const float h1x = kAnchorX + flick * kFlickX + shake * 0.003f * sinT(ph_b);
    const float h1y = kAnchorY + flick * kFlickY + shake * 0.003f * sinT(ph_a + 0.1f);
    float sh, el;
    solveArm(lerp(h0x, h1x, draw), lerp(h0y, h1y, draw), sh, el);
    p.shoulder[1] = lerpA(p.shoulder[1], sh, aim);
    p.elbow[1] = lerpA(p.elbow[1], el, aim);
  }
  const float proud = clamp01(fin(in.proud, 0.f)), slump = clamp01(fin(in.slump, 0.f));
  if (proud > 0.f) {   // chest out, chin up, the bow held low in front, a small fist pump
    const float ph = fin(in.ph_a, 0.f);
    p.lean = lerp(p.lean, -0.07f, proud);
    p.head = lerp(p.head, -0.22f, proud);
    p.lift += 0.006f * proud * (0.5f + 0.5f * sinT(ph * 0.5f));
    p.shoulder[0] = lerpA(p.shoulder[0], 0.6f, proud);
    p.elbow[0] = lerpA(p.elbow[0], 0.4f, proud);
    p.shoulder[1] = lerpA(p.shoulder[1], 2.35f + 0.14f * sinT(ph * 0.5f), proud);
    p.elbow[1] = lerpA(p.elbow[1], 1.0f, proud);
  }
  if (slump > 0.f) {   // head drops, a hand to the back of the head
    const float ph = fin(in.ph_a, 0.f);
    p.lean = lerp(p.lean, 0.06f, slump);
    p.head = lerp(p.head, 0.32f, slump);
    p.shoulder[0] = lerpA(p.shoulder[0], 0.3f, slump);
    p.elbow[0] = lerpA(p.elbow[0], 0.3f, slump);
    p.shoulder[1] = lerpA(p.shoulder[1], 2.4f, slump);
    p.elbow[1] = lerpA(p.elbow[1], 2.4f + 0.16f * sinT(ph), slump);
  }
}

void archeryPose(const ArcheryState& a, HikerPose& p) {
  if (a.aim <= 0.f && a.proud <= 0.f && a.slump <= 0.f) return;
  ArcheryPoseIn in;
  in.aim = a.aim;
  in.draw = a.draw;
  in.flick = a.flick;
  in.shake = a.shake;
  in.elev = a.elev;
  in.ph_a = a.ph_a;
  in.ph_b = a.ph_b;
  in.proud = a.proud;
  in.slump = a.slump;
  archeryBend(p, in);
}

// ---- bounds, same, dirty ---------------------------------------------------------------------------------------------

Rect archeryTargetBounds(const ArcheryState& a) {
  TargetLook lk;
  makeTargetLook(a, lk);
  return targetRect(lk);
}

Rect archeryBowBounds(const ArcheryState& a) {
  BowLook lk;
  makeBowLook(a, lk);
  return bowRect(lk);
}

Rect archeryArrowBounds(const ArcheryState& a, int i) {
  if (i < 0 || i >= ARCHERY_ARROWS) return kEmpty;
  ArrowLook lk;
  makeArrowLook(a.arrows[i], lk);
  return arrowRect(lk);
}

Rect archeryBounds(const ArcheryState& a) {
  Rect r = unite(archeryTargetBounds(a), archeryBowBounds(a));
  for (int i = 0; i < ARCHERY_ARROWS; ++i) r = unite(r, archeryArrowBounds(a, i));
  return r;
}

bool archerySame(const ArcheryState& a, const ArcheryState& b) {
  Rect none[ARCHERY_DIRTY_MAX];
  return archeryDirty(a, b, none) == 0;
}

int archeryDirty(const ArcheryState& before, const ArcheryState& after, Rect out[ARCHERY_DIRTY_MAX]) {
  int n = 0;
  {
    BowLook la, lb;
    makeBowLook(before, la);
    makeBowLook(after, lb);
    if (memcmp(&la, &lb, sizeof la) != 0) {
      const Rect r = unite(bowRect(la), bowRect(lb));
      if (!r.empty()) out[n++] = r;
    }
  }
  {
    TargetLook la, lb;
    makeTargetLook(before, la);
    makeTargetLook(after, lb);
    if (memcmp(&la, &lb, sizeof la) != 0) {
      const Rect r = unite(targetRect(la), targetRect(lb));
      if (!r.empty()) out[n++] = r;
    }
  }
  for (int i = 0; i < ARCHERY_ARROWS; ++i) {
    ArrowLook la, lb;
    makeArrowLook(before.arrows[i], la);
    makeArrowLook(after.arrows[i], lb);
    if (memcmp(&la, &lb, sizeof la) != 0) {
      const Rect r = unite(arrowRect(la), arrowRect(lb));
      if (!r.empty()) out[n++] = r;
    }
  }
  return n;
}

// ---- draw ----------------------------------------------------------------------------------------------------------------

namespace {

// One segment of the string: a quad 2 * hw px wide, square ended so neighbours butt.
void drawSeg(Surface& s, Vec2 a, Vec2 b, float hw, Pixel color, const Rect& clip) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f) return;
  const float nx = -dy / len * hw, ny = dx / len * hw;
  const Vec2 q[4] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
  fillPolygon(s, q, 4, color, clip);
}

// A shaft with a fletch of two swept vanes at the tail. `sc` shrinks the fletch.
void drawArrow(Surface& s, Vec2 tail, Vec2 tip, float sc, const Palette& pal, const Rect& clip) {
  const Vec2 d = unit(Vec2{tip.x - tail.x, tip.y - tail.y}, Vec2{1.f, 0.f});
  const Vec2 p{-d.y, d.x};
  static const float kVane[4][2] = {{-0.6f, 0.f}, {5.8f, 0.f}, {5.8f, 1.7f}, {-0.6f, 2.7f}};   // along, across
  for (int side = -1; side <= 1; side += 2) {
    Vec2 q[4];
    for (int i = 0; i < 4; ++i) {
      const float al = kVane[i][0] * sc, ac = kVane[i][1] * sc * (float)side;
      q[i] = Vec2{tail.x + d.x * al + p.x * ac, tail.y + d.y * al + p.y * ac};
    }
    fillPolygon(s, q, 4, pal[TENT_LIT], clip);
  }
  strokeCapsule(s, tail, tip, kShaftW, pal[ACTOR], clip);
}

void drawArrowLook(Surface& s, const ArrowLook& lk, const Palette& pal, const Rect& clip) {
  if (lk.mode == 0) return;
  if (!overlaps(arrowRect(lk), clip)) return;
  ArrowGeom g;
  makeArrowGeom(lk, g);
  if (lk.mode == 2) {
    drawArrow(s, g.tail_a, g.q, 0.9f, pal, clip);
    drawArrow(s, g.tail_b, g.q, 0.9f, pal, clip);
    strokeCapsule(s, g.q, g.tip, kShaftW, pal[ACTOR], clip);
    return;
  }
  drawArrow(s, g.tail, g.tip, 1.f, pal, clip);
}

}  // namespace

void archeryTargetDraw(const ArcheryState& a, Surface& s, const Rect& clip, const Palette& pal) {
  TargetLook lk;
  makeTargetLook(a, lk);
  if (!lk.on || !overlaps(targetRect(lk), clip)) return;
  const Vec2 c = targetCentre(lk);
  const float x = (float)lk.x + 0.5f, y = (float)lk.y + 0.5f;
  // The stand: two legs splayed to the ground.
  strokeCapsule(s, Vec2{x - 4.f, c.y + kLegTop}, Vec2{x - kLegSpread, y}, kLegW, pal[TRUNK], clip);
  strokeCapsule(s, Vec2{x + 4.f, c.y + kLegTop}, Vec2{x + kLegSpread, y}, kLegW, pal[TRUNK], clip);
  // The straw disc with its darker edge, then the rings.
  const Pixel edge = mixRGB(pal[TENT_LIT], pal[TRUNK], 0.62f);
  const Pixel straw = mixRGB(pal[TENT_LIT], pal[TRUNK], 0.30f);
  fillCircle(s, c, kArcheryTargetR, edge, clip);
  fillCircle(s, c, kArcheryTargetR - kRimW, straw, clip);
  fillCircle(s, c, ARCHERY_R_OUTER, pal[MUSHROOM], clip);
  fillCircle(s, c, ARCHERY_R_MID, pal[TENT_LIT], clip);
  fillCircle(s, c, ARCHERY_R_INNER, pal[MUSHROOM], clip);
}

void archeryArrowsDraw(const ArcheryState& a, Surface& s, const Rect& clip, const Palette& pal, bool behind_target) {
  for (int i = 0; i < ARCHERY_ARROWS; ++i) {
    ArrowLook lk;
    makeArrowLook(a.arrows[i], lk);
    if ((lk.behind != 0) == behind_target) drawArrowLook(s, lk, pal, clip);
  }
}

void archeryBowDraw(const ArcheryState& a, Surface& s, const Rect& clip, const Palette& pal) {
  BowLook lk;
  makeBowLook(a, lk);
  if (!lk.on || !overlaps(bowRect(lk), clip)) return;
  BowGeom g;
  makeBowGeom(lk, g);
  const Pixel ink = pal[ACTOR];
  // The limb: a tapered ribbon along the arc.
  Vec2 poly[2 * kLimbN];
  for (int i = 0; i < kLimbN; ++i) {
    const Vec2 p0 = g.limb[i > 0 ? i - 1 : 0], p1 = g.limb[i < kLimbN - 1 ? i + 1 : kLimbN - 1];
    const Vec2 t = unit(Vec2{p1.x - p0.x, p1.y - p0.y}, Vec2{0.f, 1.f});
    poly[i] = Vec2{g.limb[i].x - t.y * g.hw[i], g.limb[i].y + t.x * g.hw[i]};
    poly[2 * kLimbN - 1 - i] = Vec2{g.limb[i].x + t.y * g.hw[i], g.limb[i].y - t.x * g.hw[i]};
  }
  fillPolygon(s, poly, 2 * kLimbN, pal[TRUNK], clip);   // wood: shows against his black silhouette when it hangs at his side
  // The string: tip to nock to tip, 1 px.
  drawSeg(s, g.top, g.nock, 0.55f, ink, clip);
  drawSeg(s, g.nock, g.bot, 0.55f, ink, clip);
  if (g.arrow) drawArrow(s, g.tail, g.tip, 1.f, pal, clip);
}

void archeryDraw(const ArcheryState& a, Surface& s, const Rect& clip, const Palette& pal) {
  archeryArrowsDraw(a, s, clip, pal, true);
  archeryTargetDraw(a, s, clip, pal);
  archeryArrowsDraw(a, s, clip, pal, false);
  archeryBowDraw(a, s, clip, pal);
}

}  // namespace ss
