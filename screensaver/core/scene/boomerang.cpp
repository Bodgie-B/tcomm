// The boomerang and the eagle. See boomerang.h.
//
// Drawing follows critters.cpp: each piece is built as a short list of
// primitives (capsule, circle, polygon) in frame space; the draw rasterizes the
// list and the bounds fold the same list, so they cannot disagree.
//
// Motion is analytic. The boomerang's loop is a Catmull-Rom spline through five
// knots (six for the miss), walked with a time warp that slows it into its end;
// its spin angle is fixed backward from the angle it has to have at the end, so
// it lands in the hand, the eagle's talons or the trunk with no jump. The
// eagle's talon path is two cubic Hermite pieces meeting at the grab, planned
// from the meeting point and the boomerang's velocity there.
#include "boomerang.h"

#include <math.h>

#include "../anim/ease.h"

namespace ss {

namespace {

constexpr float kTwoPi = 6.28318531f;
constexpr float kInvTwoPi = 1.f / kTwoPi;

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline Vec2 add(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
inline Vec2 sub(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }
inline Vec2 mul(Vec2 a, float k) { return Vec2{a.x * k, a.y * k}; }
inline bool sameV(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

const Rect kFrame{0, 0, SS_WIDTH, SS_HEIGHT};
const Rect kNoBounds{0, 0, 0, 0};

// ---- tunables (PROPOSED) ---------------------------------------------------------

// The boomerang: arms 9.5 px from elbow to tip centre, 108 degrees apart,
// 3.1 px thick with a fatter elbow: 15.4 px between the tip centres, about
// 18.5 px across with the capsule width.
constexpr float kArm = 9.5f;
constexpr float kArmW = 3.1f;
constexpr float kElbowR = 2.0f;
constexpr float kHalfAngle = 0.15f;      // turns: each arm 54 degrees off the bisector
constexpr float kStripeA = 0.52f, kStripeB = 0.80f;   // the painted band, share of arm A
constexpr float kSpinRate = 2.8f;        // turns/s (+0.5 by the roll): 43 degrees a tick at 25 Hz
constexpr float kSlowEnd = 0.45f;        // A: seconds the spin winds down over before the hand
constexpr float kWobbleT = 1.4f;         // C: seconds from the thunk to still
constexpr float kWobbleHz = 3.6f;
constexpr float kWobbleAmp = 0.085f;     // turns (31 degrees) on the first swing
constexpr float kCarryTurns = 0.25f;     // B: hangs by its elbow, arms down
constexpr float kSwayAmp = 0.03f;        // B: turns of swing under the talons

// The eagle.
constexpr float kBeatHz = 0.85f;         // slow, deep: one beat a bit over a second
constexpr float kWingLen = 30.f;         // shoulder to the longest finger, px, before the view foreshortens it: about 51 px tip to tip level
constexpr float kEntryY = -70.f;         // talon height above the top edge at the exit point
constexpr float kEdgePad = 75.f;         // talon x beyond the side edge at the entry point (the eagle spans 50 px)
constexpr float kOverHim = 76.f;         // talon height over the catch point the glide keeps (he stands under it; the near wing hangs 20 px lower)
constexpr float kEagleHalfLen = 44.f;    // how far past the catch point the eagle is still over him (its tail trails 32 px)
constexpr float kGrabFlareLead = 0.6f;   // seconds before the grab the wings go up
constexpr float kLegReach = 6.4f;        // px the feet drop when reaching

// ---- primitive list ----------------------------------------------------------------

enum PrimKind : uint8_t { PRIM_CAP, PRIM_ELLIPSE, PRIM_POLY };
constexpr int kMaxPrims = 10;
constexpr int kMaxPolyPts = 24;

struct Prim {
  uint8_t kind;
  Slot slot;
  uint8_t n;
  float w, h;   // capsule width; ellipse radii
  Vec2 p[kMaxPolyPts];
};
struct Prims {
  int n;
  Prim v[kMaxPrims];
};

Prim* push(Prims& ps, PrimKind k, Slot slot) {
  if (ps.n >= kMaxPrims) return nullptr;   // cannot happen: the eagle issues 9
  Prim* q = &ps.v[ps.n++];
  q->kind = k;
  q->slot = slot;
  q->n = 0;
  q->w = q->h = 0.f;
  return q;
}
void cap(Prims& ps, Vec2 a, Vec2 b, float w, Slot slot = ACTOR) {
  if (Prim* q = push(ps, PRIM_CAP, slot)) { q->p[0] = a; q->p[1] = b; q->n = 2; q->w = w; }
}
void disc(Prims& ps, Vec2 c, float r, Slot slot = ACTOR) {
  if (Prim* q = push(ps, PRIM_ELLIPSE, slot)) { q->p[0] = c; q->n = 1; q->w = r; q->h = r; }
}

void primExtent(const Prim& q, float& x0, float& y0, float& x1, float& y1) {
  float px = 1.f, py = 1.f;
  if (q.kind == PRIM_CAP) px = py = 0.5f * q.w + 1.f;
  if (q.kind == PRIM_ELLIPSE) {
    px = q.w * 1.03f + 1.f;
    py = q.h * 1.03f + 1.f;
  }
  x0 = y0 = 3.0e38f;
  x1 = y1 = -3.0e38f;
  for (int i = 0; i < q.n; ++i) {
    x0 = minf(x0, q.p[i].x);
    x1 = maxf(x1, q.p[i].x);
    y0 = minf(y0, q.p[i].y);
    y1 = maxf(y1, q.p[i].y);
  }
  x0 -= px;
  y0 -= py;
  x1 += px;
  y1 += py;
}

Rect boundsOf(const Prims& ps) {
  if (ps.n == 0) return kNoBounds;
  float bx0 = 3.0e38f, by0 = 3.0e38f, bx1 = -3.0e38f, by1 = -3.0e38f;
  for (int i = 0; i < ps.n; ++i) {
    float x0, y0, x1, y1;
    primExtent(ps.v[i], x0, y0, x1, y1);
    bx0 = minf(bx0, x0);
    by0 = minf(by0, y0);
    bx1 = maxf(bx1, x1);
    by1 = maxf(by1, y1);
  }
  // A NaN or a runaway never gets a rect: nothing to mark, nothing drawn.
  if (!(bx0 > -1.0e6f && bx1 < 1.0e6f && by0 > -1.0e6f && by1 < 1.0e6f)) return kNoBounds;
  const Rect r{(int)floorf(bx0) - 2, (int)floorf(by0) - 2, (int)ceilf(bx1) + 2, (int)ceilf(by1) + 2};
  const Rect c = r.clipped(kFrame);
  return c.empty() ? kNoBounds : c;
}

void drawPrims(const Prims& ps, Surface& s, const Rect& clip, const Palette& pal) {
  for (int i = 0; i < ps.n; ++i) {
    const Prim& q = ps.v[i];
    float x0, y0, x1, y1;
    primExtent(q, x0, y0, x1, y1);
    if (x1 < (float)clip.x0 || x0 > (float)clip.x1 || y1 < (float)clip.y0 || y0 > (float)clip.y1) continue;
    const Pixel col = pal[q.slot];
    switch (q.kind) {
      case PRIM_CAP: strokeCapsule(s, q.p[0], q.p[1], q.w, col, clip); break;
      case PRIM_ELLIPSE: fillCircle(s, q.p[0], q.w, col, clip); break;
      default: fillPolygon(s, q.p, q.n, col, clip); break;
    }
  }
}

// ---- the boomerang ---------------------------------------------------------------------

// Local frame, px, origin halfway along the arms' reach, tips toward +x, y down.
// `flip` mirrors it about the bisector, which moves the stripe to the other arm.
struct BoomGeom {
  Vec2 elbow, tip_a, tip_b;   // arm A is striped; arm B is the one that sticks in a trunk
};
BoomGeom boomGeom(int flip) {
  const float cx = kArm * cosT(kHalfAngle);
  const float cy = kArm * sinT(kHalfAngle) * (float)flip;
  return BoomGeom{Vec2{-0.5f * cx, 0.f}, Vec2{0.5f * cx, cy}, Vec2{0.5f * cx, -cy}};
}
Vec2 rotTurns(Vec2 v, float turns) {
  const float c = cosT(turns), sn = sinT(turns);
  return Vec2{v.x * c - v.y * sn, v.x * sn + v.y * c};
}
// Angle (turns) of the vector from tip B to the elbow, with rot 0: what a
// trunk's `trunk_turns` is measured against.
float baseToElbow(int flip) { return flip > 0 ? 0.5f - kHalfAngle : 0.5f + kHalfAngle; }

void buildBoom(const Boomerang& b, Prims& ps) {
  ps.n = 0;
  if (!b.on) return;
  const BoomGeom g = boomGeom(b.flip < 0 ? -1 : 1);
  const float c = cosT(b.rot), sn = sinT(b.rot);
  auto world = [&](Vec2 v) { return Vec2{b.pos.x + v.x * c - v.y * sn, b.pos.y + v.x * sn + v.y * c}; };
  const Vec2 e = world(g.elbow), ta = world(g.tip_a), tb = world(g.tip_b);
  cap(ps, e, tb, kArmW);
  cap(ps, e, ta, kArmW);
  disc(ps, e, kElbowR);
  const Vec2 d = sub(ta, e);
  cap(ps, add(e, mul(d, kStripeA)), add(e, mul(d, kStripeB)), 1.5f, ACTOR_LIGHT);
}

// ---- the eagle ---------------------------------------------------------------------------

// Local frame, px: x toward the head, y down, origin at the body's middle. The
// body is short and deep beside the wings (an eagle's span is more than twice
// its length, and only the wings say "eagle" at this size), the head big, the
// bill heavy and hooked at the tip.
const float kBody[10][2] = {{9.0f, -4.0f},  {10.0f, -0.5f}, {7.5f, 3.4f},  {3.0f, 5.0f},  {-3.0f, 4.4f},
                            {-7.0f, 2.2f},  {-8.4f, 0.0f},  {-7.0f, -2.6f}, {-1.0f, -5.0f}, {4.5f, -5.4f}};
const float kBeak[7][2] = {{14.6f, -7.0f}, {19.6f, -6.4f}, {22.0f, -3.8f}, {21.4f, -0.8f}, {19.8f, -2.6f}, {17.6f, -2.3f}, {14.4f, -2.2f}};
const Vec2 kHead{12.0f, -4.6f};
constexpr float kHeadR = 3.6f;
const Vec2 kEye{13.2f, -5.5f};
const Vec2 kShoulder{3.0f, -1.5f};
constexpr float kRootSplit = 3.5f;   // the two roots sit this far apart each side of the body line (the view shows the near one lower)

// A wing in its own frame: u along the span from the shoulder, v from the
// leading edge toward the trailing edge. Broad, a wrist bulge, then four
// splayed primaries and scalloped secondaries.
constexpr int kWingN = 21;
const float kWing[kWingN][2] = {
    {0.0f, -0.6f},  {4.0f, -1.2f},  {9.0f, -1.4f},  {14.0f, -0.9f}, {19.0f, 0.2f},  {23.5f, 1.4f},  {26.8f, 3.0f},
    {23.8f, 4.2f},  {26.0f, 7.8f},  {22.4f, 7.6f},  {23.6f, 11.6f}, {19.8f, 10.8f}, {19.8f, 14.6f}, {16.6f, 12.4f},
    {14.0f, 13.0f}, {12.0f, 11.4f}, {9.5f, 13.0f},  {7.0f, 11.6f},  {4.0f, 12.6f},  {1.0f, 10.6f},  {-0.5f, 8.0f}};
constexpr float kWingTipU = 26.8f;
constexpr float kWrist = 13.f;   // the outer wing bends here when it lags the stroke

// The view of the wings. They spread across the body, swept back by 23
// degrees; we look down on the bird from 60 degrees up, so the near wing
// shows below the body and the far one above it. Flapping lifts both about the
// body's axis: the pair opens to a wide arrowhead when level and closes up
// over the back, or down under the belly, at the ends of the stroke.
constexpr float kSinS = 0.390f, kCosS = 0.921f;   // the sweep: 23 degrees back from square to the body
constexpr float kSinB = 0.866f, kCosB = 0.5f;     // the view
constexpr float kLevel = 0.10f;                   // radians the wings ride above flat when not beating

// Where the talons sit in the body frame for a given reach.
inline Vec2 talonLocal(float reach) { return Vec2{2.6f + 1.8f * reach, 8.1f + kLegReach * reach}; }

struct EagleXf {
  Vec2 o;
  float fx, c, s;   // facing sign, cos and sin of the pitch
  Vec2 at(float x, float y) const { return Vec2{o.x + fx * (x * c + y * s), o.y + (-x * s + y * c)}; }
};
EagleXf eagleXf(const Eagle& e) {
  const float ph = e.pitch * kInvTwoPi;
  return EagleXf{e.pos, e.facing < 0 ? -1.f : 1.f, cosT(ph), sinT(ph)};
}

// A unit of span, projected, in the body frame (y down).
Vec2 spanDir(float sigma, float phi) {
  const float ph = phi * kInvTwoPi;
  return Vec2{-kSinS, kCosS * (sigma * cosT(ph) * kSinB - sinT(ph) * kCosB)};
}

// One wing. `sigma` +1 the near wing (toward us, so lower on the screen), -1
// the far one; `phi` radians of flap (0 level, + up) for the arm, `phi_out`
// for the hand beyond the wrist, which trails the stroke; `k` its size.
void addWing(Prims& ps, const EagleXf& B, float sigma, float phi, float phi_out, float k) {
  Prim* q = push(ps, PRIM_POLY, ACTOR);
  if (!q) return;
  q->n = (uint8_t)kWingN;
  const float sc = k * kWingLen / kWingTipU;   // along the span
  const float sv = 1.15f * k;                  // across the chord: broad, not stretched
  const float wr = kWrist * sc;
  const Vec2 a = spanDir(sigma, phi), b = spanDir(sigma, phi_out);
  for (int i = 0; i < kWingN; ++i) {
    const float u = kWing[i][0] * sc, v = kWing[i][1] * sv;
    const float ui = minf(u, wr), uo = maxf(u - wr, 0.f);
    q->p[i] = B.at(kShoulder.x + ui * a.x + uo * b.x - v, kShoulder.y + sigma * kRootSplit + ui * a.y + uo * b.y);   // the chord runs back along the body
  }
}

void buildEagle(const Eagle& e, Prims& ps) {
  ps.n = 0;
  if (!e.on) return;
  const EagleXf B = eagleXf(e);
  const float stroke = sinT(e.flap);
  const float F = clampf(e.beat * stroke + 0.95f * e.flare, -1.f, 1.1f);   // -1 down .. +1 up
  const float phi = kLevel + 0.95f * F;
  const float phi_out = phi - 0.45f * e.beat * cosT(e.flap);   // the tips trail the stroke
  addWing(ps, B, -1.f, phi, phi_out, 0.92f);   // the far wing, a little smaller for the distance

  // Tail: closed it is a narrow wedge, fanned it is a broad scalloped arc.
  if (Prim* q = push(ps, PRIM_POLY, ACTOR)) {
    const float spread = 3.2f + 6.4f * clamp01(e.fan);
    q->n = 11;
    q->p[0] = B.at(-6.5f, -1.8f);
    for (int j = 0; j < 5; ++j) {
      const float jj = (float)(j - 2) * 0.5f;   // -1 .. 1
      q->p[1 + 2 * j] = B.at(-17.5f + 1.6f * jj * jj, spread * jj);
      if (j < 4) q->p[2 + 2 * j] = B.at(-15.5f + 1.6f * jj * jj, spread * (jj + 0.25f));
    }
    q->p[10] = B.at(-6.5f, 1.8f);
  }

  const float r = clamp01(e.reach);
  cap(ps, B.at(-1.4f, 3.8f), B.at(0.4f + 1.8f * r, 5.2f + (kLegReach - 0.2f) * r), 2.0f);
  cap(ps, B.at(1.0f, 3.8f), B.at(2.6f + 1.8f * r, 5.4f + kLegReach * r), 2.1f);

  if (Prim* q = push(ps, PRIM_POLY, ACTOR)) {
    q->n = 10;
    for (int i = 0; i < 10; ++i) q->p[i] = B.at(kBody[i][0], kBody[i][1]);
  }
  addWing(ps, B, 1.f, phi, phi_out, 1.0f);   // the near wing, over the body

  disc(ps, B.at(kHead.x, kHead.y), kHeadR);
  if (Prim* q = push(ps, PRIM_POLY, ACTOR)) {
    q->n = 7;
    for (int i = 0; i < 7; ++i) q->p[i] = B.at(kBeak[i][0], kBeak[i][1]);
  }
  disc(ps, B.at(kEye.x, kEye.y), 0.95f, ACTOR_LIGHT);
}


// ---- paths ---------------------------------------------------------------------------------

// Cubic Hermite: position and dp/ds at s in 0..1.
void hermite(Vec2 p0, Vec2 m0, Vec2 p1, Vec2 m1, float s, Vec2& p, Vec2& d) {
  const float s2 = s * s, s3 = s2 * s;
  const float h00 = 2.f * s3 - 3.f * s2 + 1.f, h10 = s3 - 2.f * s2 + s;
  const float h01 = -2.f * s3 + 3.f * s2, h11 = s3 - s2;
  const float d00 = 6.f * s2 - 6.f * s, d10 = 3.f * s2 - 4.f * s + 1.f;
  const float d01 = -d00, d11 = 3.f * s2 - 2.f * s;
  p = Vec2{h00 * p0.x + h10 * m0.x + h01 * p1.x + h11 * m1.x, h00 * p0.y + h10 * m0.y + h01 * p1.y + h11 * m1.y};
  d = Vec2{d00 * p0.x + d10 * m0.x + d01 * p1.x + d11 * m1.x, d00 * p0.y + d10 * m0.y + d01 * p1.y + d11 * m1.y};
}

Vec2 knotTangent(const BoomPlan& pl, int i) {
  if (i <= 0) return sub(pl.k[1], pl.k[0]);
  if (i >= pl.nk - 1) return sub(pl.k[pl.nk - 1], pl.k[pl.nk - 2]);
  return mul(sub(pl.k[i + 1], pl.k[i - 1]), 0.5f);
}

// The loop at `tau` 0..1 of its duration: position, and d/dtau.
void loopAt(const BoomPlan& pl, float tau, Vec2& p, Vec2& d) {
  tau = clamp01(tau);
  float w = tau, dw = 1.f;
  if (pl.ease) {   // 1 - (1 - tau)^1.5: fast off the hand, slowing to a stop at the end
    const float q = 1.f - tau;
    const float rq = sqrtf(q);
    w = 1.f - q * rq;
    dw = 1.5f * rq;
  }
  const int segs = pl.nk - 1;
  const float u = w * (float)segs;
  int i = (int)u;
  if (i > segs - 1) i = segs - 1;
  const float sl = u - (float)i;
  Vec2 dp;
  hermite(pl.k[i], knotTangent(pl, i), pl.k[i + 1], knotTangent(pl, i + 1), sl, p, dp);
  d = mul(dp, (float)segs * dw);
}

// Spin angle, turns: fixed backward from the angle at the end so it arrives
// exactly. `slow` seconds before t_land the rate falls linearly to nothing.
float spinDist(float s, float slow) {
  if (slow <= 0.f) return s;
  return s < slow ? s * s / (2.f * slow) : s - 0.5f * slow;
}
float spinAt(const BoomPlan& pl, float t) {
  const float s = maxf(0.f, pl.t_land - maxf(t, pl.t_throw));
  return pl.rot_land - pl.spin * spinDist(s, pl.slow);
}

// A talon-path piece from a to b over T seconds: position and velocity (px/s).
void segAt(Vec2 a, Vec2 va, Vec2 b, Vec2 vb, float T, float s, Vec2& p, Vec2& v) {
  Vec2 d;
  hermite(a, mul(va, T), b, mul(vb, T), clamp01(s), p, d);
  v = mul(d, 1.f / T);
}

// Whether the eagle's glide in crosses x on its way, and the talon height there.
bool crossesX(const BoomPlan& pl, float x, float t_in, float& y) {
  const float fx = (float)pl.facing;
  Vec2 prev, v;
  segAt(pl.e_a, pl.e_va, pl.e_m, pl.e_vm, t_in, 0.f, prev, v);
  for (int i = 1; i <= 32; ++i) {
    Vec2 p;
    segAt(pl.e_a, pl.e_va, pl.e_m, pl.e_vm, t_in, (float)i / 32.f, p, v);
    if ((prev.x - x) * fx < 0.f && (p.x - x) * fx >= 0.f) {
      const float k = (x - prev.x) / (p.x - prev.x);
      y = prev.y + k * (p.y - prev.y);
      return true;
    }
    prev = p;
  }
  return false;
}

// The eagle at clock time t, from the plan (B). Off outside [t_e0, t_e1).
void eagleAt(const BoomPlan& pl, float t, Eagle& e) {
  if (!(t >= pl.t_e0 && t < pl.t_e1)) {
    e = Eagle{};
    return;
  }
  const float tm = pl.t_land;
  Vec2 tal, vel;
  if (t < tm) segAt(pl.e_a, pl.e_va, pl.e_m, pl.e_vm, tm - pl.t_e0, (t - pl.t_e0) / (tm - pl.t_e0), tal, vel);
  else segAt(pl.e_m, pl.e_vm, pl.e_b, pl.e_vb, pl.t_e1 - tm, (t - tm) / (pl.t_e1 - tm), tal, vel);

  const float u = t - tm;   // seconds from the grab
  e.on = true;
  e.facing = pl.facing;
  e.flap = 0.25f + kBeatHz * u;   // wings at the top of the stroke at the grab, the power stroke after it
  // The approach: a lazy glide with shallow beats, deeper as it closes; after the grab, full strokes.
  e.beat = u < 0.f ? 0.35f + 0.55f * easeInOut(window(t, pl.t_e0 + 0.2f, tm - kGrabFlareLead)) : 1.f;
  // The brake: wings thrown up and held, tail spread, feet reaching; let go once it has the prize.
  const float up = easeInOut(window(u, -kGrabFlareLead, -0.15f));
  const float down = easeInOut(window(u, 0.1f, 0.55f));
  e.flare = up * (1.f - down);
  e.fan = 0.30f + 0.70f * up - 0.40f * down;
  e.reach = easeInOut(window(u, -0.5f, -0.05f)) * (1.f - 0.25f * easeInOut(window(u, 0.7f, 1.4f)));
  // Nose along the path, bounded: a steep climb tips it 40 degrees, a stoop 30.
  const float fwd = (float)pl.facing * vel.x;
  e.pitch = clampf(0.9f * (-vel.y) / (fwd + 60.f), -0.5f, 0.7f);
  // Body from the talons: undo the pose's offset to the feet.
  const EagleXf B{Vec2{0.f, 0.f}, pl.facing < 0 ? -1.f : 1.f, cosT(e.pitch * kInvTwoPi), sinT(e.pitch * kInvTwoPi)};
  const Vec2 g = talonLocal(e.reach);
  const Vec2 off = B.at(g.x, g.y);
  e.pos = sub(tal, off);
}

}  // namespace

// ---- boomerang API ---------------------------------------------------------------------------

Rect boomerangBounds(const Boomerang& b) {
  Prims ps;
  buildBoom(b, ps);
  return boundsOf(ps);
}

void boomerangDraw(const Boomerang& b, Surface& s, const Rect& clip, const Palette& pal) {
  Prims ps;
  buildBoom(b, ps);
  drawPrims(ps, s, clip, pal);
}

bool boomerangSame(const Boomerang& a, const Boomerang& b) {
  return a.on == b.on && a.flip == b.flip && sameV(a.pos, b.pos) && a.rot == b.rot;
}

int boomerangPrims(const Boomerang& b) {
  Prims ps;
  buildBoom(b, ps);
  return ps.n;
}

Vec2 boomerangTip(const Boomerang& b, bool free_arm) {
  const BoomGeom g = boomGeom(b.flip < 0 ? -1 : 1);
  const Vec2 r = rotTurns(free_arm ? g.tip_a : g.tip_b, b.rot);
  return add(b.pos, r);
}

// ---- eagle API ---------------------------------------------------------------------------------

Rect eagleBounds(const Eagle& e) {
  Prims ps;
  buildEagle(e, ps);
  return boundsOf(ps);
}

void eagleDraw(const Eagle& e, Surface& s, const Rect& clip, const Palette& pal) {
  Prims ps;
  buildEagle(e, ps);
  drawPrims(ps, s, clip, pal);
}

bool eagleSame(const Eagle& a, const Eagle& b) {
  return a.on == b.on && a.facing == b.facing && sameV(a.pos, b.pos) && a.pitch == b.pitch && a.flap == b.flap &&
         a.beat == b.beat && a.flare == b.flare && a.fan == b.fan && a.reach == b.reach;
}

int eaglePrims(const Eagle& e) {
  Prims ps;
  buildEagle(e, ps);
  return ps.n;
}

Vec2 eagleTalons(const Eagle& e) {
  const EagleXf B = eagleXf(e);
  const Vec2 g = talonLocal(clamp01(e.reach));
  return B.at(g.x, g.y);
}

// ---- the flight ----------------------------------------------------------------------------------

void boomPlan(BoomFlight& f, const BoomSpec& s, Rng& rng) {
  // Ten draws, always, one per statement.
  const float r_flip = rng.uniform();
  const float r_rate = rng.uniform();
  const float r_band = rng.uniform();
  const float r_frac = rng.uniform();
  const float r_tin = rng.uniform();
  const float r_dxin = rng.uniform();
  const float r_dxout = rng.uniform();
  const float r_loop = rng.uniform();
  const float r_rise = rng.uniform();
  const float r_side = rng.uniform();

  f = BoomFlight{};
  BoomPlan& pl = f.plan;
  pl.ending = s.ending > BOOM_STICK ? (uint8_t)BOOM_CATCH : s.ending;
  const bool stick = pl.ending == BOOM_STICK;
  pl.ease = !stick;
  pl.hand_turns = s.hand_turns;

  // Chirality. For the trunk it is the cut that lets the free arm hang lower.
  int flip = r_flip < 0.5f ? 1 : -1;
  if (stick) {
    const float yp = sinT(s.trunk_turns - 0.5f + 0.3f);    // free arm's drop with flip +1
    const float ym = sinT(s.trunk_turns - 0.5f - 0.3f);    // with flip -1
    flip = yp >= ym ? 1 : -1;
  }
  pl.flip = (int8_t)flip;

  // The loop.
  const float R = s.reach * (0.94f + 0.12f * r_loop);
  const float Hh = s.rise * (0.92f + 0.16f * r_rise);
  const Vec2 h = s.hand;
  pl.k[0] = h;
  pl.k[1] = Vec2{h.x + 0.38f * R, h.y - Hh};
  pl.k[2] = Vec2{h.x + R, h.y - 0.30f * Hh};
  pl.k[3] = Vec2{h.x + 0.60f * R, h.y + s.dip};
  pl.dur = maxf(s.seconds, 1.5f);
  pl.t_throw = maxf(s.wind_up, 0.f);

  // Spin: rate and sign from the roll, angle at the end from the ending.
  float rot_land = pl.ending == BOOM_EAGLE ? kCarryTurns : s.hand_turns;
  if (stick) {
    float rest = s.trunk_turns - baseToElbow(flip);
    rest -= floorf(rest);
    rot_land = rest;
    const Vec2 tb = boomGeom(flip).tip_b;
    const Vec2 hit = sub(s.trunk, rotTurns(tb, rest));   // where the V's centre is when the tip is in
    pl.stick = s.trunk;
    // It flies over the hand, `miss` px above catch_pt (the spline passes the
    // knot exactly, so that is the closest it comes), then on to the trunk. The
    // return leg is lifted to about hand height so the curve cannot sag back
    // down onto the hand on the way to that knot.
    pl.k[3].y = lerp(h.y + s.dip, s.catch_pt.y - s.miss, 0.6f);
    pl.k[4] = Vec2{s.catch_pt.x + 0.08f * (hit.x - s.catch_pt.x), s.catch_pt.y - s.miss};
    pl.k[5] = hit;
    pl.nk = 6;
  } else {
    pl.k[4] = s.catch_pt;
    pl.nk = 5;
  }
  pl.t_land = pl.t_throw + pl.dur;
  pl.slow = pl.ending == BOOM_CATCH ? kSlowEnd : 0.f;

  // The eagle, planned back from the meeting.
  float frac = 1.f;
  if (pl.ending == BOOM_EAGLE) {
    // Where: out in the open, away from him, and on the way OUT. Candidates
    // are every 0.02 of the flight from 0.2 to 0.76 where the boomerang is at
    // least 0.6 of the reach from the catch point. On the way back it is at
    // its lowest and the eagle would carry it off toward him, through him, so
    // only the outbound leg is used (the way back only if the loop has no far
    // outbound part). r_band takes the earlier or the later half of the
    // candidates and r_frac one of that half, so the same ten draws serve
    // whichever way the loop falls.
    float cand[2][32];
    int nc[2] = {0, 0};
    const float far_dx = 0.6f * fabsf(R);
    for (int i = 0; i < 29; ++i) {
      const float tau = 0.20f + 0.02f * (float)i;
      Vec2 cp, cd;
      loopAt(pl, tau, cp, cd);
      if (fabsf(cp.x - s.catch_pt.x) < far_dx) continue;
      const int lg = cd.x * R >= 0.f ? 0 : 1;   // 0 moving away from the hand, 1 coming back
      cand[lg][nc[lg]++] = tau;
    }
    const int leg = nc[0] > 0 ? 0 : 1;
    frac = 0.45f;   // a loop too small to have a far part: the middle of it
    if (nc[leg] > 0) {
      const int half = (nc[leg] + 1) / 2;
      const int lo = r_band < 0.5f ? 0 : nc[leg] - half;
      int pick = lo + (int)(r_frac * (float)half);
      if (pick > nc[leg] - 1) pick = nc[leg] - 1;
      frac = cand[leg][pick];
    }
    Vec2 pm, dm;
    loopAt(pl, frac, pm, dm);
    const Vec2 vb = mul(dm, 1.f / pl.dur);   // the boomerang's velocity at the meeting, px/s
    int fx = vb.x >= 0.f ? 1 : -1;           // the eagle comes from behind it, flying its way
    if (fabsf(vb.x) < 30.f) fx = r_side < 0.5f ? 1 : -1;
    pl.facing = (int8_t)fx;
    const float fxf = (float)fx;

    // Entry: over the side edge it flies in from (behind the boomerang), up
    // high, a long glide that drops 200..290 px and flattens into the grab.
    // The time is the distance at 280..340 px/s, so the lead before the throw
    // (the eagle may be on screen while he is still winding up) is whatever
    // that needs beyond the boomerang's own time to the meeting.
    const float edge = fx > 0 ? -kEdgePad : (float)SS_WIDTH + kEdgePad;
    const float t_in = clampf(fabsf(pm.x - edge) / (280.f + 60.f * r_tin), 1.3f, 3.0f);
    pl.t_throw = maxf(pl.t_throw, t_in - frac * pl.dur);
    pl.t_land = pl.t_throw + frac * pl.dur;
    pl.t_e0 = maxf(0.f, pl.t_land - t_in);   // (rounding can leave it a hair under)
    pl.e_m = pm;   // the talons close here, on the boomerang's centre
    pl.e_vm = Vec2{fxf * maxf(fabsf(vb.x) * 1.05f + 35.f, 170.f), clampf(vb.y * 0.6f + 50.f, 10.f, 55.f)};
    float lift = 0.f;   // extra height at the entry, grown below until it clears him
    for (int it = 0; it < 6; ++it) {
      pl.e_a = Vec2{edge, pm.y - (200.f + 90.f * r_dxin) - lift};
      pl.e_va = mul(sub(pm, pl.e_a), 0.85f / t_in);
      pl.e_va.y *= 0.3f;   // stays high longer, then the stoop
      // Where the glide passes over the catch point's x (the eagle is lowest when its
      // tail leaves him, a half-length on), he is standing under it:
      // keep the talons kOverHim px above the catch point (his hat is about 40 px above
      // the hand, and the near wing hangs about 20 px under the talons).
      float yc = 0.f;
      const float limit = s.catch_pt.y - kOverHim;
      if (!crossesX(pl, s.catch_pt.x + fxf * kEagleHalfLen, t_in, yc) || yc <= limit) break;
      lift += 1.6f * (yc - limit);
    }

    // Exit: carries on the way it was going, climbing out. Its run is about
    // half what the speed through the grab would cover in that time, so the
    // curve turns up without an overshoot in x (monotone: it never backs up).
    const float t_out = clampf((pm.y - kEntryY) / 150.f, 1.8f, 3.0f);
    const float dx_out = fabsf(pl.e_vm.x) * t_out * (0.45f + 0.15f * r_dxout);
    pl.e_b = Vec2{pm.x + fxf * dx_out, kEntryY};
    pl.e_vb = Vec2{fxf * 170.f, -240.f};
    pl.t_e1 = pl.t_land + t_out;
  }

  // Spin angle at launch is the hand pose; the rate sets how many turns it takes.
  const float span = spinDist(pl.t_land - pl.t_throw, pl.slow);
  const float sign = flip > 0 ? 1.f : -1.f;
  const float want = sign * (kSpinRate + 0.5f * r_rate) * span;
  const float frac_turns = rot_land - s.hand_turns;
  const float n = floorf(want - frac_turns + 0.5f);
  pl.rot_land = rot_land;
  pl.spin = (frac_turns + n) / span;

  f.t = 0.f;
  f.phase = BP_WAIT;
  f.boom = Boomerang{true, (int8_t)flip, s.hand, s.hand_turns};
}

namespace {

// C: the pose swings about the embedded tip.
void stuckPose(const BoomPlan& pl, float since, Boomerang& b) {
  float env = clamp01(1.f - since / kWobbleT);
  env *= env;
  const float dir = pl.spin >= 0.f ? 1.f : -1.f;
  b.rot = pl.rot_land + dir * kWobbleAmp * env * sinT(kWobbleHz * since);
  const Vec2 tb = boomGeom(pl.flip).tip_b;
  b.pos = sub(pl.stick, rotTurns(tb, b.rot));
}

}  // namespace

void boomStep(BoomFlight& f, float dt) {
  f.events = 0;
  if (!(dt >= 0.f)) dt = 0.f;
  const BoomPlan& pl = f.plan;
  f.t += dt;
  const float t = f.t;

  if (pl.ending == BOOM_EAGLE && f.phase != BP_GONE) eagleAt(pl, t, f.eagle);

  if (f.phase == BP_WAIT && t >= pl.t_throw) {
    f.phase = BP_FLY;
    f.events = (uint8_t)(f.events | BE_THROWN);
  }
  if (f.phase == BP_FLY) {
    if (t >= pl.t_land) {
      if (pl.ending == BOOM_CATCH) {
        f.phase = BP_HAND;
        f.events = (uint8_t)(f.events | BE_CAUGHT);
      } else if (pl.ending == BOOM_EAGLE) {
        f.phase = BP_CARRIED;
        f.events = (uint8_t)(f.events | BE_SNATCHED);
      } else {
        f.phase = BP_STUCK;
        f.events = (uint8_t)(f.events | BE_STUCK);
      }
    } else {
      Vec2 p, d;
      loopAt(pl, (t - pl.t_throw) / pl.dur, p, d);
      f.boom.pos = p;
      f.boom.rot = spinAt(pl, t);
    }
  }
  if (f.phase == BP_HAND) {
    f.boom.pos = pl.k[pl.nk - 1];
    f.boom.rot = pl.hand_turns;
  } else if (f.phase == BP_STUCK) {
    stuckPose(pl, t - pl.t_land, f.boom);
  } else if (f.phase == BP_CARRIED) {
    if (t >= pl.t_e1) {   // out of frame: both are gone for good
      f.phase = BP_GONE;
      f.events = (uint8_t)(f.events | BE_EAGLE_GONE);
      f.boom.on = false;
      f.eagle = Eagle{};
    } else {
      f.boom.pos = eagleTalons(f.eagle);
      f.boom.rot = pl.rot_land + kSwayAmp * (1.f - sinT(f.eagle.flap));
    }
  }
}

bool boomDone(const BoomFlight& f) {
  switch (f.phase) {
    case BP_HAND: return true;
    case BP_GONE: return true;
    case BP_STUCK: return f.t >= f.plan.t_land + kWobbleT;
    default: return false;
  }
}

void boomDraw(const BoomFlight& f, Surface& s, const Rect& clip, const Palette& pal) {
  boomerangDraw(f.boom, s, clip, pal);
  eagleDraw(f.eagle, s, clip, pal);
}

}  // namespace ss
