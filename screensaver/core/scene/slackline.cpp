// The slackline. See slackline.h.
#include "slackline.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"

namespace ss {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kHalfPi = 0.5f * kPi;
constexpr float kInvTwoPi = 1.f / (2.f * kPi);

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool finite(float v) { return v - v == 0.f; }   // false for NaN and infinity
// How far a first-order lag with time constant `tau` closes in dt.
inline float ease1(float dt, float tau) { return dt / (tau + dt); }
inline float rise(float t, float a, float b) { return easeInOut(window(t, a, b)); }

inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
              a.y1 > b.y1 ? a.y1 : b.y1};
}
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }

// ---- tuning (all PROPOSED; how it looks is UNVERIFIED) ---------------------------------

constexpr float kMaxDt = 0.1f;

// The line.
constexpr float kRestSag = 2.5f;       // px of sag at mid-span with nothing on it
constexpr float kSagFull = 15.f;       // px more at mid-span under weight 1 (the walker)
constexpr float kRound = 22.f;         // radius of the rounded V under the load
constexpr float kOmega = 9.f;          // rad/s: the spring (about 1.4 Hz)
constexpr float kZeta = 0.15f;         // damping ratio: about 3 or 4 visible bounces
constexpr float kLoadRise = 0.30f;     // s: weight arrives over this
constexpr float kLoadFall = 0.06f;     // s: and leaves over this (so a step-off bounces)
constexpr float kNodePx = 10.f;        // polyline node spacing
constexpr float kGroundGap = 3.f;      // the line never sags lower than this above the ground
constexpr float kHalfThick = 1.f;      // the line is 2 px

// The walker.
constexpr float kMountSecs = 1.5f;
constexpr float kStepSecs = 0.50f;     // one step, calm
constexpr float kStride = 11.f;        // the feet sit +-kStride about the hips at the end of a step: hips gain kStride a step
constexpr float kStepLift = 6.5f;      // px the swinging foot rises
constexpr float kFlourishSecs = 4.8f;
constexpr float kBounceHz = 1.3f;      // the knee bounce of the flourish, near the line's own 1.4 Hz
constexpr float kStandSep = 8.f;       // feet +- this about the hips after a mount
constexpr float kEndMargin = 36.f;     // a walk to the end stops this far from the anchor

// Rig dimensions (units of height), kept in step with hiker.cpp.
constexpr float kLeg = 0.235f;         // kThigh and kShin
constexpr float kFootHalf = 0.029f;    // half of kFootW
constexpr float kHeel = 0.02f, kToe = 0.07f;
constexpr float kFootFollow = 0.75f;

// The critter.
constexpr float kAccel = 130.f;        // px/s^2: about 0.45 s to speed and to a stop
constexpr float kLookSecs = 0.8f;
constexpr float kEndInset = 10.f;      // it starts and turns this far in from each anchor

// =========================================================================================
// The line
// =========================================================================================

inline float q4(float v) { return floorf(v * 4.f + 0.5f) * 0.25f; }

// The rounded V: 1 at the load, 0 at the anchor, a parabola within `kRound` of the load and straight beyond.
float profile(float t, float arm) {
  if (arm < 1.f) return 0.f;
  const float r = minf(kRound, arm);
  const float la = arm - 0.5f * r;
  const float p = t < r ? 1.f - t * t / (2.f * r * la) : 1.f - (t - 0.5f * r) / la;
  return p < 0.f ? 0.f : p;
}

// Sag below the chord at x: the resting belly and the load's V.
float sagAt(const Slackline& s, float x) {
  const float len = s.b.x - s.a.x;
  const float u = clamp01((x - s.a.x) / len);
  float y = kRestSag * 4.f * u * (1.f - u);
  if (s.d != 0.f) {
    const float lu = clamp01((s.lx - s.a.x) / len);
    const float vertex = s.d * 4.f * lu * (1.f - lu);   // deepest mid-span, slight near an anchor
    const float arm = x < s.lx ? s.lx - s.a.x : s.b.x - s.lx;
    y += vertex * profile(fabsf(x - s.lx), arm);
  }
  return y;
}

// The line is never lower than a few px above the ground, however hard it is kicked (it would lie on it).
float exactY(const Slackline& s, float x) {
  const float u = clamp01((x - s.a.x) / (s.b.x - s.a.x));
  const float y = s.a.y + (s.b.y - s.a.y) * u + sagAt(s, x);
  const float floor_y = maxf(s.ground_y - kGroundGap, maxf(s.a.y, s.b.y) + 2.f);
  return y < floor_y ? y : floor_y;
}

// The polyline the draw, the bounds and the dirty rects all come from: nodes
// every ~10 px, y in quarter pixels, so two states that draw alike compare equal.
struct Poly {
  int n;                              // segments
  float x[SLACK_MAX_SEG + 1];
  int16_t y4[SLACK_MAX_SEG + 1];
};

void makePoly(const Slackline& s, Poly& p) {
  const float len = s.b.x - s.a.x;
  int n = (int)ceilf(len / kNodePx);
  if (n < 2) n = 2;
  if (n > SLACK_MAX_SEG) n = SLACK_MAX_SEG;
  p.n = n;
  for (int i = 0; i <= n; ++i) {
    const float f = (float)i / (float)n;
    // Interior nodes sit on whole pixels, so neighbouring segments meet on a pixel edge and tile
    // exactly (no seam where two antialiased edges would blend twice).
    float x = floorf(s.a.x + len * f + 0.5f);
    float y;
    if (i == 0) {
      y = s.a.y;
    } else if (i == n) {
      x = s.b.x;
      y = s.b.y;
    } else {
      y = exactY(s, x);
    }
    p.x[i] = x;
    p.y4[i] = (int16_t)floorf(clampf(y, -7000.f, 7000.f) * 4.f + 0.5f);
  }
}

inline float nodeY(const Poly& p, int i) { return (float)p.y4[i] * 0.25f; }

// Nodes i0..i1 inclusive, to the antialiased fringe plus a pixel.
Rect runRect(const Poly& p, int i0, int i1) {
  float lo = 1.0e9f, hi = -1.0e9f;
  for (int i = i0; i <= i1; ++i) {
    lo = minf(lo, nodeY(p, i));
    hi = maxf(hi, nodeY(p, i));
  }
  return Rect{(int)floorf(p.x[i0]) - 1, (int)floorf(lo - kHalfThick) - 1, (int)ceilf(p.x[i1]) + 1,
              (int)ceilf(hi + kHalfThick) + 1};
}

// The wraps and knot at an anchor: three short bands across the trunk or stake and a small knot where the line leaves.
Rect knotRect(Vec2 p) {
  return Rect{(int)floorf(p.x) - 7, (int)floorf(p.y) - 5, (int)ceilf(p.x) + 7, (int)ceilf(p.y) + 8};
}

void drawKnot(Surface& s, Vec2 p, float side, const Palette& pal, const Rect& clip) {
  const Pixel wrap = pal[TENT_LIT];
  for (int k = 0; k < 3; ++k) {
    const float y = p.y + ((float)k - 1.f) * 2.2f;
    strokeCapsule(s, Vec2{p.x - 4.5f, y - 0.4f}, Vec2{p.x + 4.5f, y + 0.4f}, 1.5f, wrap, clip);
  }
  fillCircle(s, Vec2{p.x + side * 3.2f, p.y + 2.6f}, 2.2f, pal[MUSHROOM], clip);
}

}  // namespace

void slacklineInit(Slackline& s, Vec2 a, Vec2 b, float ground_y) {
  if (!finite(a.x) || !finite(a.y)) a = Vec2{100.f, 300.f};
  if (!finite(b.x) || !finite(b.y)) b = Vec2{a.x + 200.f, a.y};
  if (a.x > b.x) {
    const Vec2 t = a;
    a = b;
    b = t;
  }
  if (b.x - a.x < 16.f) b.x = a.x + 16.f;
  s.a = Vec2{q4(a.x), q4(a.y)};
  s.b = Vec2{q4(b.x), q4(b.y)};
  s.ground_y = finite(ground_y) ? ground_y : s.a.y + 30.f;
  s.lx = 0.5f * (s.a.x + s.b.x);
  s.load = s.d = s.v = 0.f;
  s.acc_w = s.acc_wx = 0.f;
}

void slacklineLoad(Slackline& s, float x, float weight) {
  if (!finite(x) || !finite(weight) || weight <= 0.f) return;
  x = clampf(x, s.a.x, s.b.x);
  s.acc_w += weight;
  s.acc_wx += weight * x;
}

void slacklineKick(Slackline& s, float px_per_s) {
  if (finite(px_per_s)) s.v += px_per_s;
}

void slacklineStep(Slackline& s, float dt) {
  if (!finite(dt) || dt < 0.f) dt = 0.f;
  if (dt > kMaxDt) dt = kMaxDt;
  float to = 0.f;
  if (s.acc_w > 0.f) {
    s.lx = clampf(s.acc_wx / s.acc_w, s.a.x, s.b.x);
    to = minf(s.acc_w, 1.5f);
  }
  s.acc_w = s.acc_wx = 0.f;
  s.load += (to - s.load) * ease1(dt, to > s.load ? kLoadRise : kLoadFall);
  if (to == 0.f && s.load < 0.004f) s.load = 0.f;

  // A damped spring on the sag, sub-stepped so a long dt stays stable.
  const float target = s.load * kSagFull;
  int n = (int)(kOmega * dt / 0.3f) + 1;
  if (n > 4) n = 4;
  const float h = dt / (float)n;
  for (int i = 0; i < n; ++i) {
    const float acc = kOmega * kOmega * (target - s.d) - 2.f * kZeta * kOmega * s.v;
    s.v += acc * h;
    s.d += s.v * h;
  }
  if (s.load == 0.f && fabsf(s.d) < 0.02f && fabsf(s.v) < 0.1f) s.d = s.v = 0.f;   // it has settled
  if (!finite(s.d) || !finite(s.v)) s.d = s.v = 0.f;                                // never expected
}

float slacklineY(const Slackline& s, float x) { return exactY(s, x); }
float slacklineTopY(const Slackline& s, float x) { return exactY(s, x) - kHalfThick; }
bool slacklineSettled(const Slackline& s) { return s.load == 0.f && s.d == 0.f && s.v == 0.f && s.acc_w == 0.f; }
float slacklineMidX(const Slackline& s) { return 0.5f * (s.a.x + s.b.x); }

Rect slacklineBounds(const Slackline& s) {
  Poly p;
  makePoly(s, p);
  return unite(unite(runRect(p, 0, p.n), knotRect(s.a)), knotRect(s.b));
}

int slacklineDirty(const Slackline& before, const Slackline& after, Rect out[SLACK_DIRTY_MAX]) {
  Poly pa, pb;
  makePoly(before, pa);
  makePoly(after, pb);
  const bool anchors = before.a.x != after.a.x || before.a.y != after.a.y || before.b.x != after.b.x || before.b.y != after.b.y;
  if (anchors || pa.n != pb.n) {   // never in a scene: the anchors are fixed
    out[0] = unite(slacklineBounds(before), slacklineBounds(after));
    return 1;
  }
  // A segment differs when either of its nodes does; each run of them is one rect, old and new together.
  int n = 0;
  int i = 0;
  while (i < pa.n) {
    if (pa.y4[i] == pb.y4[i] && pa.y4[i + 1] == pb.y4[i + 1]) {
      ++i;
      continue;
    }
    const int i0 = i;
    while (i < pa.n && !(pa.y4[i] == pb.y4[i] && pa.y4[i + 1] == pb.y4[i + 1])) ++i;
    const Rect r = unite(runRect(pa, i0, i), runRect(pb, i0, i));
    if (n < SLACK_DIRTY_MAX) out[n++] = r;
    else out[n - 1] = unite(out[n - 1], r);   // never expected: fold into the last
  }
  return n;
}

bool slacklineSame(const Slackline& a, const Slackline& b) {
  Rect none[SLACK_DIRTY_MAX];
  return slacklineDirty(a, b, none) == 0;
}

void slacklineDraw(const Slackline& s, Surface& strip, const Rect& clip, const Palette& pal) {
  Poly p;
  makePoly(s, p);
  // The line is a chain of quads, 2 px thick with vertical ends, so neighbours share their end edge
  // exactly. Each is its own polygon: the rasterizer sums coverage along a row, and one long polygon
  // would change pixels far from an edge that moved (by float rounding), which the dirty rects
  // could not cover.
  const Pixel color = pal[TENT_LIT];
  for (int i = 0; i < p.n; ++i) {
    if (!overlaps(runRect(p, i, i + 1), clip)) continue;
    const Vec2 q[4] = {{p.x[i], nodeY(p, i) - kHalfThick},
                       {p.x[i + 1], nodeY(p, i + 1) - kHalfThick},
                       {p.x[i + 1], nodeY(p, i + 1) + kHalfThick},
                       {p.x[i], nodeY(p, i) + kHalfThick}};
    fillPolygon(strip, q, 4, color, clip);
  }
  if (overlaps(knotRect(s.a), clip)) drawKnot(strip, s.a, 1.f, pal, clip);
  if (overlaps(knotRect(s.b), clip)) drawKnot(strip, s.b, -1.f, pal, clip);
}

// =========================================================================================
// The walker
// =========================================================================================

namespace {

// atan2 from a polynomial and square roots only, so the legs come out the same on the PC
// and the board (libm's atan2f can differ in its last bit). Error about 1e-5 rad.
float atanPoly(float z) {   // |z| <= 1
  const float z2 = z * z;
  return z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + 0.0208351f * z2))));
}
float atan2a(float y, float x) {
  const float ax = fabsf(x), ay = fabsf(y);
  if (ax < 1.0e-9f && ay < 1.0e-9f) return 0.f;
  float a = ay <= ax ? atanPoly(ay / ax) : kHalfPi - atanPoly(ax / ay);
  if (x < 0.f) a = kPi - a;
  return y < 0.f ? -a : a;
}

// Two equal links of length L from the hip at the origin to the ankle at (ax, ay), y down:
// the knee goes forward (+x), as a person's does. Angles are the rig's: from straight
// down, + forward. Out of reach, the leg straightens toward the ankle.
void leg2(float L, float ax, float ay, float& thigh, float& shin) {
  float d = sqrtf(ax * ax + ay * ay);
  const float dmax = 1.995f * L;
  if (d > dmax) {
    ax *= dmax / d;
    ay *= dmax / d;
    d = dmax;
  }
  if (d < 0.2f * L) {   // folded too tight (or no length): keep a sane direction
    ax = 0.f;
    ay = 0.2f * L;
    d = ay;
  }
  const float h2 = L * L - 0.25f * d * d;
  const float h = sqrtf(h2 > 0.f ? h2 : 0.f);
  const float kx = 0.5f * ax + ay / d * h;
  const float ky = 0.5f * ay - ax / d * h;
  thigh = atan2a(kx, ky);
  shin = atan2a(ax - kx, ay - ky);
}

// How far the lowest point of the rig's foot capsule hangs below the ankle for a shin angle
// (the foot pitches with the shin, hiker.cpp build()).
float soleBelowAnkle(float shin, float H) {
  const float s = sinT(kFootFollow * shin * kInvTwoPi);
  float m = 0.f;
  m = maxf(m, s * kHeel * H);
  m = maxf(m, -s * kToe * H);
  return m + kFootHalf * H;
}

// His unsteady sway, -1..1: two sines that never quite repeat.
float swayOf(const SlackWalker& w) { return 0.7f * sinT(w.ph) + 0.3f * sinT(w.ph * 2.3f + 0.2f); }

float flourishEnv(float t) { return window(t, 1.7f, 2.0f) * (1.f - window(t, 3.0f, 3.5f)); }
float flourishBounce(float t) { return flourishEnv(t) * sinT(kBounceHz * (t - 1.7f)); }

struct FallT {
  float off;    // the feet leave the line
  float land;   // the feet reach the ground
  float rec;    // begins to stand up and hand the body back to the rig
  float end;    // done
};
FallT fallTimes(bool hard) {
  return hard ? FallT{0.62f, 0.90f, 1.40f, 1.95f} : FallT{0.38f, 0.60f, 0.98f, 1.38f};
}

// Piecewise eased through (T[i], V[i]).
float keys(float t, const float* T, const float* V, int n) {
  if (t <= T[0]) return V[0];
  for (int i = 1; i < n; ++i)
    if (t <= T[i]) return lerp(V[i - 1], V[i], easeInOut((t - T[i - 1]) / (T[i] - T[i - 1])));
  return V[n - 1];
}

// His figure for the state: the body angles, the arms and how far the hips are above the lowest
// foot (D, as a share of the leg's reach). The legs come from the feet in slackWalkerPose.
struct Fig {
  float lean, head, dx, D;
  float sh[2], el[2];   // arms by rig index (0 near)
};

void figOf(const SlackWalker& w, Fig& f) {
  const float wob = clamp01(w.wob), w2 = wob * wob;
  const float sway = swayOf(w);
  const float fl = sinT(w.ph * 1.7f + 0.15f), fl2 = sinT(w.ph * 1.3f + 0.6f);
  const float amp = 0.05f + 0.55f * w2;   // the arms' flapping grows with the unease

  float lean = 0.03f + (0.03f + 0.20f * w2) * sway;
  float head = -0.05f - 0.6f * (lean - 0.03f);   // he keeps his eyes level
  float dx = 0.012f * sway * (0.5f + wob);
  float D = 0.90f - 0.03f * wob;
  // Out wide, as in profile: one arm forward and level, one back.
  float fs = 1.38f + 0.08f * wob + amp * fl, fe = 0.25f + 0.25f * wob * (0.5f + 0.5f * fl);
  float bs = -1.30f - 0.08f * wob - amp * fl2, be = -0.05f + 0.15f * wob * (0.5f + 0.5f * fl2);   // (the back arm stays straight, not drooping)

  float s0 = 0.f, e0 = 0.f, s1 = 0.f, e1 = 0.f;   // by rig index, set below (the flourish places them itself)
  bool placed = false;

  switch (w.mode) {
    case SW_MOUNT: {
      const float u = clamp01(w.t / kMountSecs);
      const float k = rise(u, 0.20f, 0.85f);   // the arms come up and out as he rises
      fs = lerp(0.35f, fs, k);
      bs = lerp(-0.25f, bs, k);
      fe = lerp(0.30f, fe, k);
      be = lerp(0.20f, be, k);
      D = lerp(0.97f, D, rise(u, 0.40f, 1.f));
      lean += 0.14f * sinT(0.5f * window(u, 0.f, 0.9f));   // leans into the step up
      break;
    }
    case SW_WALK:
      D -= 0.03f * sinT(0.5f * clamp01(w.step_u / 0.8f));
      break;
    case SW_FLOURISH: {
      const float t = w.t;
      const float up = rise(t, 0.8f, 1.7f) * (1.f - rise(t, 3.9f, 4.6f));
      const float bow = rise(t, 2.9f, 3.5f) * (1.f - rise(t, 3.9f, 4.5f));
      const float env = flourishEnv(t);
      const float bounce = flourishBounce(t);
      // The near arm goes up; the far one swings back and low, then forward across him in the bow.
      const bool near_front = w.flip == 0;
      const float ns = near_front ? fs : bs, ne = near_front ? fe : be;
      const float fs2 = near_front ? bs : fs, fe2 = near_front ? be : fe;
      s0 = lerp(ns, lerp(2.75f, 2.35f, bow), up);
      e0 = lerp(ne, 0.10f, up);
      s1 = lerp(fs2, lerp(-1.0f, 0.85f, bow), up);
      e1 = lerp(fe2, lerp(0.15f, 0.90f, bow), up);
      placed = true;
      lean += 0.52f * bow - 0.04f * up;
      head += 0.30f * bow - 0.10f * up;
      D = lerp(D, 0.94f, up) - 0.06f * env * (0.5f + 0.5f * bounce);
      break;
    }
    case SW_FALL: {
      const bool hard = w.hard != 0;
      const FallT ft = fallTimes(hard);
      const float T[6] = {0.f, ft.off, ft.land, ft.land + 0.14f, ft.rec, ft.end};
      const float tip = (float)w.tip;
      const float tipk = rise(w.t, 0.05f, ft.off);
      if (hard) {
        const float vl[6] = {0.03f, 0.03f + 0.55f * tip, 0.15f, 0.34f, 0.30f, 0.03f};
        const float vd[6] = {0.88f, 0.78f, 0.985f, 0.68f, 0.82f, 0.985f};
        const float vf[6] = {1.38f, 2.35f, 1.90f, 1.15f, 0.90f, 0.25f};
        const float vb[6] = {-1.30f, -2.25f, -1.80f, -0.95f, -0.60f, -0.15f};
        lean = keys(w.t, T, vl, 6);
        D = keys(w.t, T, vd, 6);
        const float osc = 0.45f * sinT(w.t * 3.3f) * tipk * (1.f - rise(w.t, ft.off, ft.land));   // flailing
        fs = keys(w.t, T, vf, 6) + osc;
        bs = keys(w.t, T, vb, 6) + 0.8f * osc;
        fe = 0.45f;
        be = 0.10f;
        dx = 0.045f * tip * tipk * (1.f - rise(w.t, ft.off, ft.land));
      } else {
        const float vl[6] = {0.03f, 0.14f, 0.14f, 0.22f, 0.14f, 0.03f};
        const float vd[6] = {0.90f, 0.92f, 0.985f, 0.80f, 0.90f, 0.985f};
        const float vf[6] = {1.38f, 1.00f, 0.80f, 0.60f, 0.35f, 0.20f};
        const float vb[6] = {-1.30f, -0.90f, -0.60f, -0.40f, -0.25f, -0.12f};
        lean = keys(w.t, T, vl, 6);
        D = keys(w.t, T, vd, 6);
        fs = keys(w.t, T, vf, 6);
        bs = keys(w.t, T, vb, 6);
        fe = 0.30f;
        be = 0.05f;
        dx = 0.f;
      }
      head = -0.05f - 0.6f * (lean - 0.03f);
      break;
    }
    default: break;
  }

  if (!placed) {
    const bool near_front = w.flip == 0;
    s0 = near_front ? fs : bs;
    e0 = near_front ? fe : be;
    s1 = near_front ? bs : fs;
    e1 = near_front ? be : fe;
  }
  f.lean = lean;
  f.head = head;
  f.dx = dx;
  f.D = D;
  f.sh[0] = s0;
  f.sh[1] = s1;
  f.el[0] = e0;
  f.el[1] = e1;
}

HikerPose mixPose(const HikerPose& a, const HikerPose& b, float t) {
  HikerPose r;
  r.lean = lerp(a.lean, b.lean, t);
  r.head = lerp(a.head, b.head, t);
  r.lift = lerp(a.lift, b.lift, t);
  r.dx = lerp(a.dx, b.dx, t);
  for (int i = 0; i < 2; ++i) {
    r.thigh[i] = lerp(a.thigh[i], b.thigh[i], t);
    r.knee[i] = lerp(a.knee[i], b.knee[i], t);
    r.shoulder[i] = lerp(a.shoulder[i], b.shoulder[i], t);
    r.elbow[i] = lerp(a.elbow[i], b.elbow[i], t);
  }
  return r;
}

// On the line (or still stepping up): weight the walker puts on it.
bool onLineNow(const SlackWalker& w) {
  if (w.mode == SW_OFF || w.mode >= SW_MODES) return false;
  if (w.mode == SW_FALL) return w.t < fallTimes(w.hard != 0).off;
  return true;
}

void toStand(SlackWalker& w) {
  w.mode = SW_STAND;
  w.t = 0.f;
  w.step_u = -1.f;
  w.blend = 1.f;
  w.on[0] = w.on[1] = 1.f;
  w.lift[0] = w.lift[1] = 0.f;
  w.steps_left = 0;
  w.want = SW_WANT_NONE;
  w.x = 0.5f * (w.fx[0] + w.fx[1]);
}

}  // namespace

void slackWalkerInit(SlackWalker& w, Rng& rng) {
  memset(&w, 0, sizeof w);
  const uint32_t r = rng.next();
  w.ph = (float)(r & 0xFFFFu) / 65536.f;
  w.flip = (uint8_t)((r >> 16) & 1u);
  w.dir = 1;
  w.step_u = -1.f;
}

void slackWalkerMount(SlackWalker& w, float feet_x, float ground_y, int dir) {
  if (w.mode != SW_OFF || !finite(feet_x) || !finite(ground_y)) return;
  w.mode = SW_MOUNT;
  w.t = 0.f;
  w.dir = (int8_t)(dir < 0 ? -1 : 1);
  const float d = (float)w.dir;
  w.x = w.x0 = feet_x;
  w.ground_y = ground_y;
  w.fx[0] = w.fx0[0] = feet_x + d * 3.f;
  w.fx[1] = w.fx0[1] = feet_x - d * 3.f;
  w.on[0] = w.on[1] = 0.f;
  w.lift[0] = w.lift[1] = 0.f;
  w.swing = 1;
  w.step_u = -1.f;
  w.steps_left = 0;
  w.want = SW_WANT_NONE;
  w.blend = 0.f;
  w.wob = w.wob_to = 0.f;
  w.load = 0.f;
}

void slackWalkerWalk(SlackWalker& w, const Slackline& line, int steps) {
  if (w.mode != SW_STAND && w.mode != SW_WALK) return;
  const float d = (float)w.dir;
  w.far_x = w.dir > 0 ? line.b.x - kEndMargin : line.a.x + kEndMargin;
  w.steps_left = (int16_t)(steps > 0 ? (steps > 200 ? 200 : steps) : -1);
  if (w.mode == SW_STAND) {
    w.mode = SW_WALK;
    w.t = 0.f;
    w.step_u = -1.f;
    // The foot that is behind steps first.
    if (d * (w.fx[w.swing] - w.fx[1 - w.swing]) > 0.f) w.swing = (uint8_t)(1 - w.swing);
  }
}

void slackWalkerWobble(SlackWalker& w, float level) {
  if (finite(level)) w.wob_to = clamp01(level);
}

static void beginFlourish(SlackWalker& w) {
  w.mode = SW_FLOURISH;
  w.t = 0.f;
  w.step_u = -1.f;
  w.want = SW_WANT_NONE;
  w.wob_to = 0.f;
  for (int i = 0; i < 2; ++i) w.lift[i] = 0.f;
  w.x = 0.5f * (w.fx[0] + w.fx[1]);
}

static void beginFall(SlackWalker& w, bool hard, int tip) {
  w.mode = SW_FALL;
  w.t = 0.f;
  w.hard = hard ? 1 : 0;
  w.tip = (int8_t)(tip < 0 ? -1 : 1);
  w.kicked = 0;
  w.step_u = -1.f;
  w.want = SW_WANT_NONE;
  w.x0 = w.x;
  w.fx0[0] = w.fx[0];
  w.fx0[1] = w.fx[1];
  w.on[0] = w.on[1] = 1.f;
  w.lift[0] = w.lift[1] = 0.f;
  w.wob_to = hard ? 1.f : 0.f;
  w.blend = 1.f;
}

// A command for a walker on the line: at once if he is standing, at the end of the step if walking.
static void command(SlackWalker& w, SlackWant what, int tip) {
  if (w.mode == SW_WALK) {
    w.want = what;
    w.tip = (int8_t)(tip < 0 ? -1 : 1);
    return;
  }
  if (w.mode != SW_STAND) return;
  switch (what) {
    case SW_WANT_FLOURISH: beginFlourish(w); break;
    case SW_WANT_FALL: beginFall(w, true, tip); break;
    case SW_WANT_DISMOUNT: beginFall(w, false, 1); break;
    default: break;
  }
}

void slackWalkerStand(SlackWalker& w) {
  if (w.mode == SW_WALK) w.want = SW_WANT_STAND;
}
void slackWalkerFlourish(SlackWalker& w) { command(w, SW_WANT_FLOURISH, 1); }
void slackWalkerFall(SlackWalker& w, Rng& rng) {
  const uint32_t r = rng.next();   // one draw, whether or not it takes
  command(w, SW_WANT_FALL, (r & 1u) ? 1 : -1);
}
void slackWalkerDismount(SlackWalker& w) { command(w, SW_WANT_DISMOUNT, 1); }

void slackWalkerStep(SlackWalker& w, Slackline& line, float dt) {
  if (!finite(dt) || dt < 0.f) dt = 0.f;
  if (dt > kMaxDt) dt = kMaxDt;
  w.load = 0.f;
  if (w.mode == SW_OFF || w.mode >= SW_MODES) return;
  w.t += dt;
  w.wob += (w.wob_to - w.wob) * ease1(dt, 0.5f);
  w.ph += dt * (0.45f + 0.8f * w.wob);
  w.ph -= floorf(w.ph);
  const float d = (float)w.dir;
  const float sway = swayOf(w);

  switch (w.mode) {
    case SW_MOUNT: {
      const float u = clamp01(w.t / kMountSecs);
      w.on[0] = rise(u, 0.08f, 0.40f);
      w.on[1] = rise(u, 0.50f, 0.86f);
      w.fx[0] = lerp(w.fx0[0], w.x0 + d * kStandSep, w.on[0]);
      w.fx[1] = lerp(w.fx0[1], w.x0 - d * kStandSep, w.on[1]);
      w.lift[0] = 7.f * sinT(0.5f * window(u, 0.04f, 0.46f));   // a high step up onto the line
      w.lift[1] = 6.f * sinT(0.5f * window(u, 0.46f, 0.90f));
      w.x = 0.5f * (w.fx[0] + w.fx[1]);
      w.blend = rise(w.t, 0.f, 0.30f);
      w.load = rise(u, 0.30f, 0.92f);
      if (w.t >= kMountSecs) {
        toStand(w);
        w.load = 1.f;
      }
      break;
    }
    case SW_STAND:
      w.x = 0.5f * (w.fx[0] + w.fx[1]);
      w.load = 1.f + 0.10f * w.wob * sway;
      break;
    case SW_WALK: {
      const float T = kStepSecs * (1.f + 0.35f * w.wob) * 1.25f;   // the last fifth of it is a pause
      const int sw = w.swing, st = 1 - w.swing;
      if (w.step_u < 0.f) {
        w.step_u = 0.f;
        w.step_x0 = w.fx[sw];
      }
      w.step_u += dt / T;
      const float q = clamp01(w.step_u / 0.8f);
      const float target = w.fx[st] + d * kStride;
      w.fx[sw] = lerp(w.step_x0, target, easeInOut(q));
      w.lift[sw] = kStepLift * sinT(0.5f * q);
      w.x = 0.5f * (w.fx[0] + w.fx[1]);
      w.load = 1.f + 0.12f * w.wob * sway;
      if (w.step_u >= 1.f) {
        w.fx[sw] = target;
        w.lift[sw] = 0.f;
        w.swing = (uint8_t)st;
        w.step_u = -1.f;
        w.x = 0.5f * (w.fx[0] + w.fx[1]);
        bool stop = d * (w.far_x - w.x) <= kStride;   // no room for another step
        if (w.steps_left > 0 && --w.steps_left == 0) stop = true;
        const SlackWant want = (SlackWant)w.want;
        w.want = SW_WANT_NONE;
        if (want == SW_WANT_FLOURISH) beginFlourish(w);
        else if (want == SW_WANT_FALL) beginFall(w, true, w.tip);
        else if (want == SW_WANT_DISMOUNT) beginFall(w, false, 1);
        else if (stop || want == SW_WANT_STAND) toStand(w);
      }
      break;
    }
    case SW_FLOURISH:
      w.x = 0.5f * (w.fx[0] + w.fx[1]);
      w.load = 1.f + 0.30f * flourishBounce(w.t);
      if (w.t >= kFlourishSecs) toStand(w);
      break;
    case SW_FALL: {
      const bool hard = w.hard != 0;
      const FallT ft = fallTimes(hard);
      const float tipk = rise(w.t, 0.05f, ft.off);
      const float drift = hard ? 9.f : 16.f;
      w.x = w.x0 + d * drift * rise(w.t, ft.off - 0.12f, ft.land + 0.10f);
      // The feet stay planted until the step-off, then follow his hips to either side of where he lands.
      const float k = rise(w.t, ft.off - 0.10f, ft.land);
      const int sw = w.swing;
      for (int i = 0; i < 2; ++i) {
        const float plant = w.fx0[i] + (hard && i == sw ? d * 14.f * tipk : 0.f);
        const float spread = i == 0 ? 6.f : -6.f;
        w.fx[i] = lerp(plant, w.x + d * spread, k);
      }
      w.lift[0] = w.lift[1] = 0.f;
      if (hard) w.lift[sw] = 10.f * sinT(0.5f * window(w.t, 0.12f, ft.off + 0.10f));   // one foot kicks out
      const float fall = window(w.t, ft.off, ft.land);
      w.on[0] = w.on[1] = 1.f - fall * fall;   // accelerating down onto the ground
      w.blend = 1.f - rise(w.t, ft.rec, ft.end);
      if (w.t < ft.off) {
        w.load = 1.f - 0.15f * tipk;
      } else if (!w.kicked) {
        w.kicked = 1;
        slacklineKick(line, hard ? 38.f : 12.f);   // the push-off
      }
      if (w.t >= ft.end) {
        w.mode = SW_OFF;
        w.blend = 0.f;
        w.on[0] = w.on[1] = 0.f;
        w.lift[0] = w.lift[1] = 0.f;
        w.wob = w.wob_to = 0.f;
        w.load = 0.f;
      }
      break;
    }
    default: break;
  }
  if (!finite(w.x) || !finite(w.fx[0]) || !finite(w.fx[1])) {   // never expected
    w.mode = SW_OFF;
    w.blend = 0.f;
    w.load = 0.f;
    return;
  }
  if (w.load > 0.001f) slacklineLoad(line, w.x, w.load);
}

bool slackWalkerOnLine(const SlackWalker& w) { return onLineNow(w); }

void slackWalkerPose(const SlackWalker& w, const Slackline& line, float H, HikerPose& pose, Vec2& feet) {
  if (w.mode == SW_OFF || w.mode >= SW_MODES) return;
  Fig f;
  figOf(w, f);

  // What each foot stands on: the ground, the line's upper edge at its x, or a step above either.
  float s[2];
  for (int i = 0; i < 2; ++i) s[i] = lerp(w.ground_y, slacklineTopY(line, w.fx[i]), w.on[i]) - w.lift[i];
  const float low = maxf(s[0], s[1]);
  feet = Vec2{w.x, low};

  // The legs reach each foot from the hips: the lowest sole hangs D of a leg length below them.
  const float L = kLeg * H;
  const float sole_low = f.D * 2.f * L + kFootHalf * H;
  const float sd = (float)w.dir;
  HikerPose q;
  q.lean = f.lean;
  q.head = f.head;
  q.lift = 0.f;
  q.dx = f.dx;
  for (int i = 0; i < 2; ++i) {
    const float ax = sd * (w.fx[i] - w.x) - f.dx * H;
    float thigh = 0.f, shin = 0.f;
    for (int pass = 0; pass < 3; ++pass) {   // the foot's tilt moves its sole; a couple of corrections settle it
      const float ay = sole_low - (low - s[i]) - soleBelowAnkle(shin, H);
      leg2(L, ax, ay, thigh, shin);
    }
    q.thigh[i] = thigh;
    q.knee[i] = thigh - shin;
    q.shoulder[i] = f.sh[i];
    q.elbow[i] = f.el[i];
  }
  pose = mixPose(pose, q, clamp01(w.blend));
}

void slackWalkerApply(const SlackWalker& w, const Slackline& line, Hiker& h) {
  if (w.mode == SW_OFF || w.mode >= SW_MODES) return;
  Vec2 feet = h.feet;
  slackWalkerPose(w, line, h.height, h.pose, feet);
  h.feet = feet;
  h.facing = w.dir < 0 ? -1 : 1;
}

// =========================================================================================
// The critter
// =========================================================================================

namespace {

// The plan's legs: where each goes and what it does on arriving.
void legOf(const SlackCritter& s, int leg, float& goal, uint8_t& after) {
  if (s.plan == SLACK_CROSS_HALF) {
    if (leg == 0) {
      goal = s.mid_x;
      after = SC_SIT;
    } else {
      goal = s.start_x;
      after = SC_DONE;
    }
    return;
  }
  if (leg == 0) {
    goal = s.far_x;
    after = SC_LOOK;
  } else if (leg == 1) {
    goal = s.mid_x;
    after = SC_SIT;
  } else {
    goal = s.start_x;
    after = SC_DONE;
  }
}

void beginLeg(SlackCritter& s, int leg) {
  uint8_t after = 0;
  s.leg = (uint8_t)leg;
  legOf(s, leg, s.goal, after);
  s.state = SC_TROT;
  s.t = 0.f;
  s.dir = (int8_t)(s.goal >= s.x ? 1 : -1);
}

}  // namespace

void slackCritterInit(SlackCritter& s, const Slackline& line, CritterKind kind, int from_end, SlackCross plan, Rng& rng) {
  memset(&s, 0, sizeof s);
  const float r = rng.uniform();   // the one draw
  const bool squirrel = kind == CR_SQUIRREL;
  s.kind = squirrel ? (uint8_t)CR_SQUIRREL : (uint8_t)CR_CHIPMUNK;
  s.plan = plan < SLACK_CROSS_PLANS ? (uint8_t)plan : (uint8_t)SLACK_CROSS_BOTH;
  const bool left = from_end < 0;
  s.start_x = left ? line.a.x + kEndInset : line.b.x - kEndInset;
  s.far_x = left ? line.b.x - kEndInset : line.a.x + kEndInset;
  s.mid_x = slacklineMidX(line);
  s.x = s.start_x;
  s.vmax = (squirrel ? 58.f : 46.f) * (0.94f + 0.12f * r);
  s.weight = squirrel ? 0.10f : 0.06f;
  s.sit_s = 4.f + 2.f * r;
  beginLeg(s, 0);
}

void slackCritterStep(SlackCritter& s, Slackline& line, float dt) {
  if (!finite(dt) || dt < 0.f) dt = 0.f;
  if (dt > kMaxDt) dt = kMaxDt;
  if (s.state == SC_DONE) return;
  s.t += dt;
  if (s.state == SC_TROT) {
    const float remain = s.goal - s.x;
    const float sgn = remain >= 0.f ? 1.f : -1.f;
    if (fabsf(remain) > 0.5f) s.dir = (int8_t)(sgn > 0.f ? 1 : -1);
    // Cruise, then ease to a stop: the speed the brakes can still bring to zero in the distance left.
    const float want = sgn * minf(s.vmax, sqrtf(2.f * kAccel * fabsf(remain)));
    const float lim = kAccel * dt;
    s.v += clampf(want - s.v, -lim, lim);
    s.x += s.v * dt;
    if ((s.goal - s.x) * sgn <= 0.15f || (fabsf(remain) < 0.4f && fabsf(s.v) < 6.f)) {
      s.x = s.goal;
      s.v = 0.f;
      uint8_t after = 0;
      float goal = 0.f;
      legOf(s, s.leg, goal, after);
      s.state = after;
      s.t = 0.f;
    }
  } else if (s.state == SC_LOOK || s.state == SC_SIT) {
    const float dur = s.state == SC_LOOK ? kLookSecs : s.sit_s;
    if (s.t >= dur) beginLeg(s, s.leg + 1);
  }
  if (s.state != SC_DONE) slacklineLoad(line, s.x, s.weight);
}

bool slackCritterDone(const SlackCritter& s) { return s.state == SC_DONE; }
bool slackCritterSitting(const SlackCritter& s) { return s.state == SC_SIT; }

Vec2 slackCritterFeet(const SlackCritter& s, const Slackline& line) { return Vec2{s.x, slacklineTopY(line, s.x)}; }

void slackCritterDrive(const SlackCritter& s, Critter& c) {
  switch (s.state) {
    case SC_TROT: {
      const CritterAction gait = s.kind == CR_SQUIRREL ? CA_RUN : CA_MOVE;
      if (!(c.moving && c.action == gait)) critterSetAction(c, gait, 0.2f);
      critterMoveTo(c, s.goal, fabsf(s.v) + 1.f);
      break;
    }
    case SC_LOOK:
      if (c.action != CA_LOOK) critterSetAction(c, CA_LOOK, 0.4f);
      break;
    case SC_SIT:
      if (c.action != CA_SIT) critterSetAction(c, CA_SIT, 0.45f);
      break;
    default:
      if (c.action != CA_IDLE) critterSetAction(c, CA_IDLE, 0.3f);
      break;
  }
}

void slackCritterPlace(const SlackCritter& s, const Slackline& line, Critter& c) {
  c.feet.x = s.x;
  c.feet.y = slacklineTopY(line, s.x);
}

}  // namespace ss
