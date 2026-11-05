// The mountain bike. See bike.h.
#include "bike.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"

namespace ss {

namespace {

const Rect kEmpty{0, 0, 0, 0};

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
constexpr float kInvTwoPi = 1.f / kTwoPi;

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float absf(float v) { return v < 0.f ? -v : v; }
inline bool finite(float v) { return v - v == 0.f; }   // false for NaN and infinity
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }
inline float ease1(float dt, float tau) { return dt / (tau + dt); }   // how far a first-order lag closes in dt
inline float frac(float x) { return x - floorf(x); }

inline Vec2 add(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
inline Vec2 sub(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }
inline Vec2 mul(Vec2 a, float k) { return Vec2{a.x * k, a.y * k}; }
inline float len2(Vec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
// From straight down, + forward, in radians, as the rig measures limbs.
inline Vec2 dirA(float a) { return Vec2{sinT(a * kInvTwoPi), cosT(a * kInvTwoPi)}; }

inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
              a.y1 > b.y1 ? a.y1 : b.y1};
}
inline int64_t area(const Rect& r) { return r.empty() ? 0 : (int64_t)(r.x1 - r.x0) * (int64_t)(r.y1 - r.y0); }
inline bool touches(const Rect& a, const Rect& b) { return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1; }

// Running float extents, turned into an integer rect with a margin for the antialiased fringe.
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

// The inverse tangent without atan2f (A&S 4.4.49, error under 1e-5), so PC and board agree.
float atanPoly(float x) {   // |x| <= 1
  const float x2 = x * x;
  return x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f + x2 * 0.0208351f))));
}
float atan2T(float y, float x) {
  const float ax = absf(x), ay = absf(y);
  if (ax < 1.0e-9f && ay < 1.0e-9f) return 0.f;
  float a = ay <= ax ? atanPoly(ay / ax) : kPi * 0.5f - atanPoly(ax / ay);
  if (x < 0.f) a = kPi - a;
  return y < 0.f ? -a : a;
}
float acosT(float c) {
  c = clampf(c, -1.f, 1.f);
  return atan2T(sqrtf(maxf(0.f, 1.f - c * c)), c);
}

// ---- tuning (all PROPOSED; how it looks is UNVERIFIED) ------------------------------------

constexpr float kMaxDt = 0.1f;
constexpr float kRideSpeed = 92.f;       // px/s on dry ground (his run is 78)
constexpr float kProudSpeed = 80.f;      // the way back
constexpr float kWaterDrag = 0.78f;      // the stream slows him to this share
constexpr float kStartX = 80.f;          // the rear tyre starts this far off the edge (and ends this far past the other)
constexpr float kGear = 0.7f;            // crank turns per wheel turn
constexpr float kCrankLen = 8.5f;
constexpr float kLaySquash = 0.55f;      // lying on its side, seen from the side: height falls by this much
constexpr float kGravity = 950.f;        // px/s^2 for a flying rider
constexpr float kBitGravity = 640.f;     // px/s^2 for a droplet

// B: the rock is 19 px wide and 11 tall, standing on the lane.
constexpr float kRockHalfW = 9.5f, kRockH = 11.f;
constexpr float kRockFaceReach = 14.5f;  // how far ahead of the front axle the tyre meets a rock face as tall as the rock
constexpr float kEndoPitch = -0.115f;    // turns: the tail lifts as the bike noses over
constexpr float kEndoSecs = 1.35f;       // stop, nose over, fall on its side

// C: the wheelie, in seconds since the pop began.
constexpr float kWhPre = 0.3f;           // the crouch before it
constexpr float kWhPop = 0.75f;          // up to the hold angle (and a little past it)
constexpr float kWhHoldEnd = 3.7f;       // the hold ends: it begins to go
constexpr float kWhTip = 2.0f;           // slow tip to the point of no return
constexpr float kWhHold = 0.118f;        // turns: 42 degrees nose up
constexpr float kWhGone = 0.2f;          // turns: past this he comes off the saddle
constexpr float kFallSecs = 1.0f;        // the fall itself
constexpr float kFallRate = 0.06f;       // turns/s it is already going at when the fall begins

// ---- the hiker rig's lengths (hiker.cpp; test_bike holds this copy to hikerHandPos) ---------

constexpr float kThigh = 0.235f, kShin = 0.235f;
constexpr float kUpper = 0.16f, kFore = 0.15f;
constexpr float kTorsoTop = 0.235f, kShoulder = 0.25f, kNeck = 0.29f;
constexpr float kHeadUp = 0.06f, kHeadFwd = 0.012f, kHeadR = 0.062f;
constexpr float kTorsoW = 0.15f, kShinW = 0.075f, kFootW = 0.058f;
constexpr float kHeel = 0.02f, kToe = 0.07f, kFootFollow = 0.75f;
constexpr float kBreathAmp = 0.011f;

struct RigGeo {
  Vec2 shoulder, head;
  Vec2 knee[2], ankle[2], heel[2], toe[2], elbow[2], hand[2];
  float low;   // how far below the hip the rig grounds the figure
};

// The rig's local frame: x forward, y down, the hip at (dx * H, 0).
RigGeo rigGeo(const HikerPose& P, float H, float breath) {
  RigGeo g;
  const Vec2 hip{P.dx * H, 0.f};
  const float lt = P.lean * kInvTwoPi;
  const Vec2 up{sinT(lt), -cosT(lt)};
  const Vec2 chest = add(hip, mul(up, kTorsoTop * H + breath));
  g.shoulder = add(hip, mul(up, kShoulder * H + breath));
  const Vec2 neck = add(hip, mul(up, kNeck * H + breath));
  const float ht = (P.lean + P.head) * kInvTwoPi;
  const Vec2 hu{sinT(ht), -cosT(ht)};
  const Vec2 hf{cosT(ht), sinT(ht)};
  g.head = add(neck, add(mul(hu, kHeadUp * H), mul(hf, kHeadFwd * H)));
  float low = maxf(hip.y, chest.y) + 0.5f * kTorsoW * H;
  for (int i = 0; i < 2; ++i) {
    g.knee[i] = add(hip, mul(dirA(P.thigh[i]), kThigh * H));
    const float shin = P.thigh[i] - P.knee[i];
    g.ankle[i] = add(g.knee[i], mul(dirA(shin), kShin * H));
    const Vec2 fd = dirA(0.5f * kPi + kFootFollow * shin);
    g.heel[i] = sub(g.ankle[i], mul(fd, kHeel * H));
    g.toe[i] = add(g.ankle[i], mul(fd, kToe * H));
    g.elbow[i] = add(g.shoulder, mul(dirA(P.shoulder[i]), kUpper * H));
    g.hand[i] = add(g.elbow[i], mul(dirA(P.shoulder[i] + P.elbow[i]), kFore * H));
    low = maxf(low, g.knee[i].y + 0.5f * kShinW * H);
    low = maxf(low, maxf(g.heel[i].y, maxf(g.toe[i].y, g.ankle[i].y)) + 0.5f * kFootW * H);
  }
  low = maxf(low, g.head.y + kHeadR * H);
  g.low = low;
  return g;
}

// feet.x and feet.y that put the pose's hip joint at `hip` (frame space) for a rider facing `dir`.
Vec2 feetFor(const HikerPose& P, Vec2 hip, int dir, float H, float breath) {
  const RigGeo g = rigGeo(P, H, breath);
  return Vec2{hip.x - (float)dir * P.dx * H, hip.y + g.low + P.lift * H};
}
Vec2 hipFor(const HikerPose& P, Vec2 feet, int dir, float H, float breath) {
  const RigGeo g = rigGeo(P, H, breath);
  return Vec2{feet.x + (float)dir * P.dx * H, feet.y - g.low - P.lift * H};
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

// ---- two-bone inverse kinematics in the rig's frame (x forward, y down) --------------------------------

// The thigh and knee that put the ankle at (dx, dy) from the hip, the knee pointing forward.
void legIK(float dx, float dy, float l, float& thigh, float& knee) {
  const float reach = sqrtf(dx * dx + dy * dy);
  const float base = atan2T(dx, dy);
  const float a = acosT(clampf(reach, 0.3f * l, 1.998f * l) / (2.f * l));
  thigh = base + a;
  knee = 2.f * a;
}

// The shoulder and elbow angles that put the hand at (dx, dy) from the shoulder, the elbow below the line.
void armIK(float dx, float dy, float u, float f, float& sh, float& el) {
  const float reach = clampf(sqrtf(dx * dx + dy * dy), absf(u - f) + 0.05f, (u + f) * 0.999f);
  const float base = atan2T(dx, dy);
  const float al = acosT((u * u + reach * reach - f * f) / (2.f * u * reach));
  const float be = acosT((f * f + reach * reach - u * u) / (2.f * f * reach));
  sh = base - al;
  el = al + be;
}

// The torso lean (absolute, radians from upright, + forward) that puts the shoulder `reach` from the grip.
float solveLean(Vec2 grip, float shoulder_len, float reach) {
  auto dist = [&](float lean) {
    const Vec2 sh{shoulder_len * sinT(lean * kInvTwoPi), -shoulder_len * cosT(lean * kInvTwoPi)};
    return len2(sub(grip, sh));
  };
  float lo = 0.05f;
  if (dist(lo) <= reach) return lo;
  float best = lo, best_d = dist(lo);
  for (float l = 0.15f; l <= 1.5001f; l += 0.1f) {
    const float dd = dist(l);
    if (dd < best_d) {
      best_d = dd;
      best = l;
    }
    if (dd <= reach) {
      float hi = l;
      for (int it = 0; it < 9; ++it) {
        const float mid = 0.5f * (lo + hi);
        if (dist(mid) <= reach) hi = mid;
        else lo = mid;
      }
      return hi;
    }
    lo = l;
  }
  return best;
}

// ---- the bike's frame of reference ---------------------------------------------------------------------------

// Local coordinates (u forward along the bike, w up it), origin at the rear tyre's contact point. The
// transform scales, squashes w (lying on its side), pitches (nose up positive, turns) about the origin,
// mirrors for dir -1 and moves to the pivot.
struct Xf {
  float px, py, c, s, dir, k, sq;
  Vec2 pt(float u, float w) const {
    const float uu = u * k, ww = w * k * sq;
    return Vec2{px + dir * (uu * c - ww * s), py - (uu * s + ww * c)};
  }
  Vec2 vec(float u, float w) const {
    const float uu = u * k, ww = w * k * sq;
    return Vec2{dir * (uu * c - ww * s), -(uu * s + ww * c)};
  }
};
Xf makeXf(Vec2 pivot, float pitch, int dir, float k, float lay) {
  const float p = pitch - floorf(pitch);
  return Xf{pivot.x, pivot.y, cosT(p), sinT(p), (float)dir, k, 1.f - kLaySquash * lay};
}

// The bike, local (u, w).
constexpr float kR = kBikeWheelR;
constexpr float kWB = kBikeWheelbase;
constexpr float kBBu = 18.f, kBBw = 12.f;
constexpr float kSeatTopU = 9.5f, kSeatTopW = 40.5f;
constexpr float kHeadTopU = 35.5f, kHeadTopW = 43.5f, kHeadBotU = 37.2f, kHeadBotW = 37.5f;
constexpr float kTopTubeHu = 36.f, kTopTubeHw = 41.5f, kTopTubeSu = 10.f, kTopTubeSw = 40.f;
constexpr float kDownU = 37.f, kDownW = 38.5f;
constexpr float kSaddleRearU = 3.5f, kSaddleRearW = 42.6f, kSaddleNoseU = 13.5f, kSaddleNoseW = 41.9f;
constexpr float kStemU = 39.f, kStemW = 45.8f, kBarU = 43.f, kBarW = 47.f;
constexpr float kGripU = 41.5f, kGripW = 46.6f;
constexpr float kHipU = 8.2f, kHipW = 47.8f;     // the rider's hip joint
constexpr float kTyreOut = 15.5f, kTyreIn = 13.2f;

// ---- the riding poses ------------------------------------------------------------------------------------------

enum SeatStyle : int { SEAT_RIDE = 0, SEAT_WHEELIE = 1, SEAT_PROUD = 2 };

// Sitting on the saddle, hands on the grips, feet on the pedals, for the bike transform `xf` and crank
// angle `crank` (turns). Returns the pose and where its hip is (frame space).
void seatedPose(const Xf& xf, float crank, int style, float t, float H, HikerPose& P, Vec2& hip) {
  const float k = xf.k;
  hip = xf.pt(kHipU, kHipW);
  const float d = xf.dir;
  P.dx = 0.f;
  P.lift = 0.f;
  // Legs: the ankle goes where the foot sits on its pedal (the ball of the foot over it).
  for (int i = 0; i < 2; ++i) {
    const float psi = crank + 0.5f * (float)i;
    const Vec2 pw = xf.pt(kBBu + kCrankLen * sinT(psi), kBBw + kCrankLen * cosT(psi));
    const Vec2 pedal{d * (pw.x - hip.x), pw.y - hip.y};
    Vec2 target{pedal.x - 4.2f * k, pedal.y - 3.8f * k};
    float thigh = 0.f, knee = 0.f;
    for (int it = 0; it < 2; ++it) {
      legIK(target.x, target.y, kThigh * H, thigh, knee);
      const float f = kFootFollow * (thigh - knee);
      target = Vec2{pedal.x - cosT(f * kInvTwoPi) * 0.055f * H, pedal.y + sinT(f * kInvTwoPi) * 0.055f * H - 3.6f * k};
    }
    P.thigh[i] = thigh;
    P.knee[i] = knee;
  }
  // Torso: leans until his arms reach the grips with the elbows a little bent (straight in a wheelie).
  const Vec2 gw = xf.pt(kGripU, kGripW);
  const Vec2 grip{d * (gw.x - hip.x), gw.y - hip.y};
  const float arm = (kUpper + kFore) * H;
  const float reach = arm * (style == SEAT_WHEELIE ? 0.995f : (style == SEAT_PROUD ? 0.975f : 0.93f));
  const float lean = solveLean(grip, kShoulder * H, reach);
  P.lean = lean;
  const float head_abs = style == SEAT_WHEELIE ? -0.02f : (style == SEAT_PROUD ? -0.14f : 0.12f);
  P.head = head_abs - lean;
  const Vec2 sh{kShoulder * H * sinT(lean * kInvTwoPi), -kShoulder * H * cosT(lean * kInvTwoPi)};
  for (int i = 0; i < 2; ++i) {
    const Vec2 tg = i == 0 ? grip : Vec2{grip.x - 1.5f * k, grip.y + 0.5f * k};
    armIK(tg.x - sh.x, tg.y - sh.y, kUpper * H, kFore * H, P.shoulder[i], P.elbow[i]);
  }
  if (style == SEAT_PROUD) {   // the far arm up, waving
    P.shoulder[1] = 2.55f + 0.18f * sinT(t * 1.3f);
    P.elbow[1] = 0.3f + 0.3f * sinT(t * 1.3f + 0.25f);
  }
}

// Head first, arms stretched ahead, legs trailing: torso lean `lean` (radians, 0 up, + forward, past 1.57
// it points down).
HikerPose divePose(float lean) {
  HikerPose P{};
  P.lean = lean;
  P.head = -0.3f;
  P.shoulder[0] = kPi - lean + 0.12f;
  P.shoulder[1] = kPi - lean - 0.10f;
  P.elbow[0] = 0.06f;
  P.elbow[1] = 0.10f;
  P.thigh[0] = -lean + 0.40f;
  P.thigh[1] = -lean - 0.10f;
  P.knee[0] = 0.45f;
  P.knee[1] = 0.15f;
  return P;
}

// Sitting in the stream, a bit stunned, hands propped behind him.
HikerPose sitWaterPose() {
  HikerPose P = hikerPoseDef(POSE_SIT);
  P.lean = -0.16f;
  P.head = 0.38f;
  P.shoulder[0] = -0.35f;
  P.shoulder[1] = -0.6f;
  P.elbow[0] = 0.12f;
  P.elbow[1] = 0.1f;
  return P;
}

// On his back, one arm flung up at the sky, a knee up.
HikerPose lieBackPose() {
  HikerPose P = hikerPoseDef(POSE_LIE);
  P.shoulder[0] = 2.75f;
  P.shoulder[1] = 3.7f;
  P.elbow[0] = 0.3f;
  P.elbow[1] = 0.45f;
  P.thigh[0] = 1.5f;
  P.thigh[1] = 1.9f;
  P.knee[0] = 0.12f;
  P.knee[1] = 0.95f;
  return P;
}

// ---- the lane --------------------------------------------------------------------------------------------------------

inline bool inWater(const BikeCourse& c, float x) { return c.water_x1 > c.water_x0 && x >= c.water_x0 && x <= c.water_x1; }

}  // namespace

float bikeLaneY(const BikeCourse& c, float x) {
  const int n = c.n < 2 ? 0 : (c.n > BIKE_LANE_MAX ? BIKE_LANE_MAX : c.n);
  if (n == 0) return 380.f;
  if (!(x > c.x[0])) return c.y[0];
  for (int i = 0; i + 1 < n; ++i) {
    if (x <= c.x[i + 1]) {
      const float span = c.x[i + 1] - c.x[i];
      const float t = span > 0.f ? (x - c.x[i]) / span : 0.f;
      return c.y[i] + (c.y[i + 1] - c.y[i]) * t;
    }
  }
  return c.y[n - 1];
}

void bikeCourseFit(BikeCourse& c, const Landscape& ls, const Vec2* pts, int n, int dir) {
  memset(&c, 0, sizeof c);
  if (n < 2) n = 2;
  if (n > BIKE_LANE_MAX) n = BIKE_LANE_MAX;
  c.n = n;
  for (int i = 0; i < n; ++i) {
    c.x[i] = pts[i].x;
    c.y[i] = pts[i].y;
  }
  // The longest stretch of the lane that is inside the water by at least 2 px.
  int best0 = 0, best1 = 0, run0 = -1000;
  bool in_run = false;
  for (int x = -100; x <= 900; ++x) {
    float cy = 0.f, half = 0.f;
    const bool in = landscapeStreamSpan(ls, (float)x, cy, half) && absf(bikeLaneY(c, (float)x) - cy) <= half - 2.f;
    if (in && !in_run) {
      in_run = true;
      run0 = x;
    }
    if ((!in || x == 900) && in_run) {
      in_run = false;
      const int end = in ? x : x - 1;
      if (end - run0 > best1 - best0) {
        best0 = run0;
        best1 = end;
      }
    }
  }
  if (best1 > best0) {
    c.water_x0 = (float)best0;
    c.water_x1 = (float)best1;
    const float len = (float)(best1 - best0);
    c.rock_x = dir >= 0 ? (float)best0 + 0.35f * len : (float)best1 - 0.35f * len;
  }
  c.wheelie_x = dir >= 0 ? 250.f : 550.f;
}

// ---- the look: everything the bike's draw reads, quantized -------------------------------------------------------------

namespace {

// Whole pixels for the pivot, 1/1024 turn for the pitch, and a few steps for the wheels and cranks. Two states
// with the same Look draw the same pixels, so a tick that changes nothing visible changes nothing on screen.
struct Look {
  uint8_t vis;        // the bike is drawn
  uint8_t rock;       // the rock is drawn
  int8_t dir;
  uint8_t lay;        // 0..255
  uint8_t scale;      // scale * 64
  uint8_t wr, wf;     // wheel angle in 1/48 turn, 0..11 (the cross repeats every quarter turn)
  uint8_t crank;      // 1/36 turn
  uint8_t pad;
  uint16_t pitch;     // 1/1024 turn
  int16_t px, py;     // rear tyre contact
  int16_t rx, ry;     // the rock's base centre
};

int16_t qpx(float v) {
  if (!finite(v)) return 0;
  return (int16_t)floorf(clampf(v, -16000.f, 16000.f) + 0.5f);
}

void makeLook(const BikeState& b, Look& lk) {
  memset(&lk, 0, sizeof lk);
  const bool vis = b.mode != BIKE_HIDDEN && b.mode != BIKE_AWAY && b.mode < BIKE_MODES;
  lk.dir = b.dir < 0 ? (int8_t)-1 : (int8_t)1;
  lk.scale = (uint8_t)clampf(b.scale * 64.f + 0.5f, 1.f, 255.f);
  if ((b.flags & 16u) && b.course.rock_x > 0.f) {
    lk.rock = 1;
    lk.rx = qpx(b.course.rock_x);
    lk.ry = qpx(bikeLaneY(b.course, b.course.rock_x));
  }
  if (!vis) return;
  lk.vis = 1;
  lk.lay = (uint8_t)(clampf(b.lay, 0.f, 1.f) * 255.f + 0.5f);
  lk.wr = (uint8_t)((int)floorf(frac(finite(b.wr) ? b.wr : 0.f) * 4.f * 12.f) % 12);
  lk.wf = (uint8_t)((int)floorf(frac(finite(b.wf) ? b.wf : 0.f) * 4.f * 12.f) % 12);
  lk.crank = (uint8_t)((int)floorf(frac(finite(b.crank) ? b.crank : 0.f) * 36.f) % 36);
  lk.pitch = (uint16_t)((int)floorf(frac(finite(b.pitch) ? b.pitch : 0.f) * 1024.f) & 1023);
  lk.px = qpx(b.pivot.x);
  lk.py = qpx(b.pivot.y);
}

// ---- the geometry, from a Look alone -----------------------------------------------------------------------------------

enum PrimKind : uint8_t { PK_CAP, PK_CIRCLE, PK_RING };

struct Prim {
  uint8_t kind;
  Slot slot;
  float w;       // CAP: width; CIRCLE: radius; RING: inner radius as a share of the outer
  Vec2 p[3];     // CAP: ends; CIRCLE: centre; RING: centre, the two axes of the ellipse
};

constexpr int kMaxPrims = 32;
constexpr int kRingN = 28;

struct Geom {
  int n;
  Prim v[kMaxPrims];
  bool rock;
  Vec2 rock_pts[7];
};

void cap(Geom& g, const Xf& xf, float u0, float w0, float u1, float w1, float width, Slot slot) {
  if (g.n >= kMaxPrims) return;
  Prim& p = g.v[g.n++];
  p.kind = PK_CAP;
  p.slot = slot;
  p.w = width * xf.k;
  p.p[0] = xf.pt(u0, w0);
  p.p[1] = xf.pt(u1, w1);
}
void capW(Geom& g, Vec2 a, Vec2 b, float width, Slot slot) {
  if (g.n >= kMaxPrims) return;
  Prim& p = g.v[g.n++];
  p.kind = PK_CAP;
  p.slot = slot;
  p.w = width;
  p.p[0] = a;
  p.p[1] = b;
}
void circle(Geom& g, const Xf& xf, float u, float w, float r, Slot slot) {
  if (g.n >= kMaxPrims) return;
  Prim& p = g.v[g.n++];
  p.kind = PK_CIRCLE;
  p.slot = slot;
  p.w = r * xf.k;
  p.p[0] = xf.pt(u, w);
}

void wheel(Geom& g, const Xf& xf, float axle_u, float turns) {
  if (g.n + 6 > kMaxPrims) return;
  const Vec2 c = xf.pt(axle_u, kR);
  const Vec2 au = xf.vec(kTyreOut, 0.f), aw = xf.vec(0.f, kTyreOut);
  Prim& r = g.v[g.n++];
  r.kind = PK_RING;
  r.slot = ACTOR;
  r.w = kTyreIn / kTyreOut;
  r.p[0] = c;
  r.p[1] = au;
  r.p[2] = aw;
  // The cross of spokes: two lines through the hub, turned by the wheel's angle.
  const float sr = (kTyreIn - 0.3f) / kTyreOut;
  for (int i = 0; i < 2; ++i) {
    const float a = turns + 0.25f * (float)i;
    const float ca = cosT(a) * sr, sa = sinT(a) * sr;
    const Vec2 v{au.x * ca + aw.x * sa, au.y * ca + aw.y * sa};
    capW(g, sub(c, v), add(c, v), 0.95f, ACTOR_LIGHT);
  }
  if (g.n < kMaxPrims) {
    Prim& h = g.v[g.n++];
    h.kind = PK_CIRCLE;
    h.slot = ACTOR_LIGHT;
    h.w = 1.7f * xf.k;
    h.p[0] = c;
  }
}

void makeGeom(const Look& lk, Geom& g) {
  g.n = 0;
  g.rock = lk.rock != 0;
  const float k = (float)lk.scale / 64.f;
  if (g.rock) {
    static const float kRock[7][2] = {{-9.5f, 0.f}, {-9.8f, -4.8f}, {-5.5f, -9.6f}, {1.f, -11.f},
                                      {7.f, -8.4f}, {10.f, -3.6f}, {9.5f, 0.f}};
    for (int i = 0; i < 7; ++i) g.rock_pts[i] = Vec2{(float)lk.rx + kRock[i][0] * k, (float)lk.ry + 1.f + kRock[i][1] * k};
  }
  if (!lk.vis) return;
  const Xf xf = makeXf(Vec2{(float)lk.px, (float)lk.py}, (float)lk.pitch / 1024.f, lk.dir, k, (float)lk.lay / 255.f);
  const float crank = (float)lk.crank / 36.f;
  // The far crank and pedal (behind the frame), the wheels, the frame, then the near crank and pedal.
  for (int i = 1; i >= 0; --i) {
    if (i == 0) break;
    const float psi = crank + 0.5f;
    const float pu = kBBu + kCrankLen * sinT(psi), pw = kBBw + kCrankLen * cosT(psi);
    cap(g, xf, kBBu, kBBw, pu, pw, 1.8f, ACTOR);
    cap(g, xf, pu - 2.6f, pw, pu + 2.6f, pw, 1.7f, ACTOR);
  }
  wheel(g, xf, 0.f, (float)lk.wr / 48.f);
  wheel(g, xf, kWB, (float)lk.wf / 48.f);
  cap(g, xf, 0.f, kR, kBBu, kBBw, 1.6f, MUSHROOM);                 // chain stay
  cap(g, xf, 0.f, kR, kSeatTopU, kSeatTopW - 1.5f, 1.5f, MUSHROOM);   // seat stay
  cap(g, xf, kDownU, kDownW, kBBu, kBBw, 2.6f, MUSHROOM);          // down tube
  cap(g, xf, kBBu, kBBw, kSeatTopU, kSeatTopW, 2.4f, MUSHROOM);    // seat tube
  cap(g, xf, kTopTubeHu, kTopTubeHw, kTopTubeSu, kTopTubeSw, 2.4f, MUSHROOM);   // top tube
  cap(g, xf, kHeadTopU, kHeadTopW, kHeadBotU, kHeadBotW, 2.8f, MUSHROOM);       // head tube
  cap(g, xf, kHeadBotU, kHeadBotW, kWB, kR, 2.0f, ACTOR);          // fork
  cap(g, xf, kHeadTopU, kHeadTopW, kStemU, kStemW, 2.0f, ACTOR);   // stem
  cap(g, xf, kStemU, kStemW, kBarU, kBarW, 2.5f, ACTOR);           // bars
  cap(g, xf, kSaddleRearU, kSaddleRearW, kSaddleNoseU, kSaddleNoseW, 3.4f, ACTOR);   // saddle
  circle(g, xf, kBBu, kBBw, 3.3f, ACTOR);                           // chainring
  {
    const float psi = crank;
    const float pu = kBBu + kCrankLen * sinT(psi), pw = kBBw + kCrankLen * cosT(psi);
    cap(g, xf, kBBu, kBBw, pu, pw, 1.9f, ACTOR);
    cap(g, xf, pu - 2.6f, pw, pu + 2.6f, pw, 1.8f, ACTOR);
  }
}

// ---- rects of the parts ---------------------------------------------------------------------------------------------------

Rect primRect(const Prim& p) {
  Ext e;
  switch (p.kind) {
    case PK_CAP:
      e.pt(p.p[0], 0.5f * p.w + 0.5f);
      e.pt(p.p[1], 0.5f * p.w + 0.5f);
      break;
    case PK_CIRCLE: e.pt(p.p[0], p.w + 0.5f); break;
    default: {
      const float hx = sqrtf(p.p[1].x * p.p[1].x + p.p[2].x * p.p[2].x);
      const float hy = sqrtf(p.p[1].y * p.p[1].y + p.p[2].y * p.p[2].y);
      e.pt(Vec2{p.p[0].x - hx, p.p[0].y - hy}, 0.5f);
      e.pt(Vec2{p.p[0].x + hx, p.p[0].y + hy}, 0.5f);
      break;
    }
  }
  return e.rect();
}

Rect rockRect(const Geom& g) {
  if (!g.rock) return kEmpty;
  Ext e;
  for (const Vec2& p : g.rock_pts) e.pt(p, 0.5f);
  return e.rect();
}

Rect geomBounds(const Geom& g) {
  Rect r = rockRect(g);
  for (int i = 0; i < g.n; ++i) r = unite(r, primRect(g.v[i]));
  return r;
}

}  // namespace

// ---- the splash: stepping ----------------------------------------------------------------------------------------------------

namespace {

void spawnBit(SplashState& sp, const BikeCourse& c, Rng& rng, float x, float y0, float side, float a_lo, float a_hi, float v_lo,
              float v_hi) {
  // Three draws, always, so the stream does not depend on whether there was room.
  const float ra = rng.uniform();
  const float rs = rng.uniform();
  const float rq = rng.uniform();
  SplashBit* s = nullptr;
  for (int i = 0; i < SPLASH_BITS; ++i) {
    if (sp.bit[i].kind == 0) {
      s = &sp.bit[i];
      break;
    }
  }
  if (!s) return;
  const float a = lerp(a_lo, a_hi, ra) * kInvTwoPi;      // radians to turns
  const float v = lerp(v_lo, v_hi, rs);
  const float vx = side * cosT(a) * v, vy = -sinT(a) * v;
  const float yl = y0 + 1.f + 3.f * rq;
  const float tf = (-vy + sqrtf(vy * vy + 2.f * kBitGravity * (yl - y0))) / kBitGravity;
  const float xl = x + vx * tf;
  const bool lands = xl >= c.water_x0 + 2.f && xl <= c.water_x1 - 2.f && yl < (float)SS_HEIGHT - 3.f;
  s->x = x;
  s->y = y0;
  s->vx = vx;
  s->vy = vy;
  s->y_land = yl;
  s->life = lands ? tf + 0.5f : tf * 0.8f;
  s->r8 = (uint8_t)(8.f * (1.2f + 1.1f * rq) + 0.5f);
  s->kind = lands ? (uint8_t)1 : (uint8_t)2;
}

void spawnRing(SplashState& sp, float x, float y, float radius, float life) {
  int slot = -1;
  float oldest = -1.f;
  for (int i = 0; i < SPLASH_RINGS; ++i) {
    if (!sp.ring[i].on) {
      slot = i;
      oldest = 1.0e9f;
      break;
    }
    if (sp.ring[i].age > oldest) {
      oldest = sp.ring[i].age;
      slot = i;
    }
  }
  if (slot < 0) return;
  SplashRing& r = sp.ring[slot];
  r.x = x;
  r.y = y;
  r.age = 0.f;
  r.life = life;
  r.r8 = (uint8_t)clampf(radius * 8.f, 8.f, 255.f);
  r.on = 1;
}

// A crown draws two values (its size and lean vary a little).
void spawnCrown(SplashState& sp, Rng& rng, float x, float y, float size, float tilt) {
  const float rz = rng.uniform();
  const float rt = rng.uniform();
  int slot = 0;
  float oldest = -1.f;
  for (int i = 0; i < SPLASH_CROWNS; ++i) {
    if (!sp.crown[i].on) {
      slot = i;
      oldest = 1.0e9f;
      break;
    }
    if (sp.crown[i].age > oldest) {
      oldest = sp.crown[i].age;
      slot = i;
    }
  }
  SplashCrown& c = sp.crown[slot];
  c.x = x;
  c.y = y;
  c.age = 0.f;
  c.size = size * (0.9f + 0.2f * rz);
  c.tilt = (int8_t)clampf((tilt + 0.012f * (rt - 0.5f)) * 64.f * 8.f, -60.f, 60.f);   // 1/64 turn is 8 units here
  c.on = 1;
}

constexpr float kCrownLife = 0.34f;

void splashAdvance(SplashState& sp, float dt) {
  for (int i = 0; i < SPLASH_BITS; ++i) {
    SplashBit& s = sp.bit[i];
    if (s.kind == 0) continue;
    if (!finite(s.x) || !finite(s.y) || !finite(s.vx) || !finite(s.vy)) {
      s.kind = 0;
      continue;
    }
    s.vy += kBitGravity * dt;
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    s.life -= dt;
    if (s.kind == 1 && s.y >= s.y_land && s.vy > 0.f) {
      // It comes down in the stream: a small ring where it lands.
      spawnRing(sp, s.x, s.y_land, 2.2f + 0.5f * (float)s.r8 * 0.125f, 0.4f);
      ++sp.landed;
      s.kind = 0;
    } else if (s.kind == 2 && s.life <= 0.f) {
      ++sp.faded;
      s.kind = 0;
    } else if (s.y > (float)SS_HEIGHT - 2.f || s.life < -0.6f) {   // never expected
      s.kind = 0;
    }
  }
  for (int i = 0; i < SPLASH_RINGS; ++i) {
    SplashRing& r = sp.ring[i];
    if (!r.on) continue;
    r.age += dt;
    if (r.age >= r.life) r.on = 0;
  }
  for (int i = 0; i < SPLASH_CROWNS; ++i) {
    SplashCrown& c = sp.crown[i];
    if (!c.on) continue;
    c.age += dt;
    if (c.age >= kCrownLife) c.on = 0;
  }
}

// A burst of `n` droplets thrown `side` (+1 toward +x, -1 toward -x) from (x, y).
void burst(SplashState& sp, const BikeCourse& c, Rng& rng, float x, float y, float side, int n, float a_lo, float a_hi, float v_lo,
           float v_hi) {
  for (int i = 0; i < n; ++i) spawnBit(sp, c, rng, x, y, side, a_lo, a_hi, v_lo, v_hi);
}

}  // namespace

bool splashActive(const SplashState& sp) {
  for (int i = 0; i < SPLASH_BITS; ++i)
    if (sp.bit[i].kind) return true;
  for (int i = 0; i < SPLASH_RINGS; ++i)
    if (sp.ring[i].on) return true;
  for (int i = 0; i < SPLASH_CROWNS; ++i)
    if (sp.crown[i].on) return true;
  for (int i = 0; i < SPLASH_COVERS; ++i)
    if (sp.cover[i].on) return true;
  return false;
}

// ---- commands -------------------------------------------------------------------------------------------------------------------

void bikeInit(BikeState& b, SplashState& sp, float rider_height) {
  memset(&b, 0, sizeof b);
  memset(&sp, 0, sizeof sp);
  b.mode = BIKE_HIDDEN;
  b.dir = 1;
  b.scale = finite(rider_height) ? clampf(rider_height / 90.f, 0.3f, 3.f) : 1.f;
}

void bikeSetCourse(BikeState& b, const BikeCourse& c) { b.course = c; }

namespace {

void laneUnder(BikeState& b, float dt, bool snap) {
  const float k = b.scale;
  const float d = (float)b.dir;
  b.pivot.y = bikeLaneY(b.course, b.pivot.x);
  const float yf = bikeLaneY(b.course, b.pivot.x + d * kWB * k);
  const float want = atan2T(b.pivot.y - yf, kWB * k) * kInvTwoPi;
  b.lane_pitch = snap ? want : b.lane_pitch + (want - b.lane_pitch) * ease1(dt, 0.07f);
}

}  // namespace

void bikeRide(BikeState& b, int dir, BikeVariant v, bool proud) {
  const BikeCourse crs = b.course;
  const float scale = b.scale;
  const uint8_t rock = (uint8_t)(b.flags & 16u);
  memset(&b, 0, sizeof b);
  b.course = crs;
  b.scale = scale;
  b.flags = rock;
  b.dir = dir < 0 ? (int8_t)-1 : (int8_t)1;
  b.variant = v < BIKE_VARIANTS ? (uint8_t)v : (uint8_t)BIKE_VAR_PASS;
  b.proud = proud ? 1 : 0;
  b.mode = BIKE_RIDE;
  b.rider = RIDER_SEAT;
  b.pivot.x = b.dir > 0 ? -kStartX : (float)SS_WIDTH + kStartX;
  b.speed = (proud ? kProudSpeed : kRideSpeed) * scale;
  if (v == BIKE_VAR_ROCK) b.flags |= 16u;
  laneUnder(b, 0.f, true);
  b.pitch = b.lane_pitch;
}

void bikeFreeRider(BikeState& b) { b.rider = RIDER_FREE; }

void bikeRock(BikeState& b, bool on) { b.flags = on ? (uint8_t)(b.flags | 16u) : (uint8_t)(b.flags & ~16u); }

void bikeHide(BikeState& b) {
  b.mode = BIKE_HIDDEN;
  b.rider = RIDER_FREE;
  b.flags = (uint8_t)(b.flags & ~16u);
}

bool bikeDone(const BikeState& b) {
  if (b.mode == BIKE_AWAY) return true;
  return b.mode == BIKE_DOWN && b.rider != RIDER_SEAT && b.rider != RIDER_FLY && b.rider != RIDER_SPLASH && b.rider != RIDER_FALL;
}

bool bikeInFront(const BikeState& b) {
  return b.variant == BIKE_VAR_WHEELIE && (b.mode == BIKE_TIP || b.mode == BIKE_DOWN);
}

// ---- the rider, from the state -----------------------------------------------------------------------------------------------------

namespace {

struct RiderOut {
  HikerPose pose;
  Vec2 feet;
  Vec2 hip;
};

constexpr float kDiveLean = 2.0f;        // radians: head first and a little down when he reaches the water
constexpr float kSplashUp0 = 0.75f;      // he begins to sit up this long after going in
constexpr float kSplashUp1 = 1.75f;      // and is sitting
constexpr float kSplashDone = 1.8f;      // RIDER_DOWN

// C's wheelie angle in turns, `t` seconds after the pop began (up to the point of no return).
float wheelieAngle(float t) {
  if (t < kWhPre) return -0.014f * easeInOut(t / kWhPre);
  if (t < kWhPre + kWhPop) return lerp(-0.014f, kWhHold, easeOutBack((t - kWhPre) / kWhPop));
  const float th = t - (kWhPre + kWhPop);
  const float wob = 0.006f * sinT(0.55f * th) + 0.0035f * sinT(1.6f * th + 0.3f);
  const float t_tip = kWhHoldEnd - kWhPre - kWhPop;   // seconds into the hold when it begins to go
  if (th < t_tip) return kWhHold + wob;
  const float u = clamp01((th - t_tip) / kWhTip);
  return kWhHold + wob * (1.f - u) + (kWhGone - kWhHold) * u * sqrtf(u);
}

// The bike transform for the rider's purposes.
Xf bikeXf(const BikeState& b) { return makeXf(b.pivot, b.pitch, b.dir, b.scale, 0.f); }

void riderFor(const BikeState& b, float H, float breath, RiderOut& out) {
  const float d = (float)b.dir;
  const float k = b.scale;
  Vec2 hip{0.f, 0.f};
  switch (b.rider) {
    case RIDER_SEAT: {
      const int style = b.proud ? SEAT_PROUD : ((b.mode == BIKE_WHEELIE) ? SEAT_WHEELIE : SEAT_RIDE);
      seatedPose(bikeXf(b), frac(b.crank), style, b.t, H, out.pose, hip);
      out.hip = hip;
      out.feet = feetFor(out.pose, hip, b.dir, H, breath);
      return;
    }
    case RIDER_FLY: {
      const float L = lerp(b.lean0, kDiveLean, easeInOut(window(b.rt, 0.f, 0.55f)));
      out.pose = mixPose(b.from, divePose(L), easeInOut(window(b.rt, 0.f, 0.28f)));
      out.hip = b.hip;
      out.feet = feetFor(out.pose, b.hip, b.dir, H, breath);
      return;
    }
    case RIDER_SPLASH: {
      // In: he goes in head first, belly-flops flat on the water, then comes up to sitting.
      const float sink = easeOutCubic(window(b.rt, 0.f, 0.35f));
      const Vec2 hip_in{b.hip.x + d * 16.f * k * sink, b.hip.y + 9.f * k * sink};
      const HikerPose under = divePose(lerp(b.lean0, 1.72f, easeInOut(window(b.rt, 0.f, 0.35f))));
      const HikerPose sit = sitWaterPose();
      const Vec2 feet_sit{b.vel.x + d * 16.f * k, b.vel.y + 3.f * k};
      const float e = easeInOut(window(b.rt, kSplashUp0, kSplashUp1));
      out.pose = mixPose(mixPose(b.from, under, easeInOut(window(b.rt, 0.f, 0.3f))), sit, e);
      const Vec2 hip_sit = hipFor(sit, feet_sit, b.dir, H, breath);
      hip = Vec2{lerp(hip_in.x, hip_sit.x, e), lerp(hip_in.y, hip_sit.y, e)};
      out.hip = hip;
      out.feet = feetFor(out.pose, hip, b.dir, H, breath);
      return;
    }
    case RIDER_FALL: {
      const HikerPose lie = lieBackPose();
      const float x_end = b.mark.x - d * 52.f * k;
      const float y_ground = bikeLaneY(b.course, x_end);
      const Vec2 hip_end = hipFor(lie, Vec2{x_end, y_ground}, b.dir, H, breath);
      const float e = clamp01(b.rt / 0.6f);
      out.pose = mixPose(b.from, lie, easeInOut(window(b.rt, 0.f, 0.5f)));
      hip = Vec2{lerp(b.hip.x, hip_end.x, easeOutCubic(e)), lerp(b.hip.y, hip_end.y, e * e)};
      out.hip = hip;
      out.feet = feetFor(out.pose, hip, b.dir, H, breath);
      return;
    }
    case RIDER_DOWN:
    default: {
      if (b.variant == BIKE_VAR_ROCK) {
        out.pose = sitWaterPose();
        out.feet = Vec2{b.vel.x + d * 16.f * k, b.vel.y + 3.f * k};
      } else {
        out.pose = lieBackPose();
        const float x_end = b.mark.x - d * 52.f * k;
        out.feet = Vec2{x_end, bikeLaneY(b.course, x_end)};
      }
      out.hip = hipFor(out.pose, out.feet, b.dir, H, breath);
      return;
    }
  }
}

float breathOf(const Hiker& h) {
  return (1.f - h.walk_w) * kBreathAmp * h.height * h.breath_scale * (0.5f + 0.5f * sinT(h.breath_phase));
}

}  // namespace

bool bikeApplyRider(const BikeState& b, Hiker& h) {
  if (b.rider == RIDER_FREE || b.rider >= RIDER_MODES) return false;
  RiderOut o;
  riderFor(b, h.height, breathOf(h), o);
  h.pose = o.pose;
  h.feet = o.feet;
  h.facing = b.dir < 0 ? -1 : 1;
  return true;
}

Vec2 bikeRigHand(const Hiker& h, int which) {
  const RigGeo g = rigGeo(h.pose, h.height, breathOf(h));
  const float sx = h.facing < 0 ? -1.f : 1.f;
  const Vec2 p = g.hand[which ? 1 : 0];
  return Vec2{h.feet.x + sx * p.x, h.feet.y - g.low - h.pose.lift * h.height + p.y};
}

Vec2 bikeRigHip(const Hiker& h) {
  const RigGeo g = rigGeo(h.pose, h.height, breathOf(h));
  const float sx = h.facing < 0 ? -1.f : 1.f;
  return Vec2{h.feet.x + sx * h.pose.dx * h.height, h.feet.y - g.low - h.pose.lift * h.height};
}

// ---- the step --------------------------------------------------------------------------------------------------------------------------

namespace {

// Lowest drawn y of the bike under `xf` (a lying or upside-down bike rests on this).
float lowestY(const Xf& xf) {
  float m = -1.0e9f;
  auto at = [&](float u, float w, float r) { m = maxf(m, xf.pt(u, w).y + r); };
  at(kSaddleRearU, kSaddleRearW, 2.2f);
  at(kSaddleNoseU, kSaddleNoseW, 2.2f);
  at(kBarU, kBarW, 2.0f);
  at(kStemU, kStemW, 1.5f);
  for (int i = 0; i < 2; ++i) {
    const Vec2 c = xf.pt(i == 0 ? 0.f : kWB, kR);
    const Vec2 au = xf.vec(kTyreOut, 0.f), aw = xf.vec(0.f, kTyreOut);
    m = maxf(m, c.y + sqrtf(au.y * au.y + aw.y * aw.y) + 0.5f);
  }
  return m;
}

// The rider's seat for the pose of the moment, from the bike.
void seatNow(const BikeState& b, float H, HikerPose& P, Vec2& hip) {
  const int style = b.proud ? SEAT_PROUD : ((b.mode == BIKE_WHEELIE) ? SEAT_WHEELIE : SEAT_RIDE);
  seatedPose(bikeXf(b), frac(b.crank), style, b.t, H, P, hip);
}

// The wheels turn at their rates; the cranks follow the rear wheel (a chain).
void turnWheels(BikeState& b, float dt) {
  b.wr = frac(b.wr + b.wrw * dt);
  b.wf = frac(b.wf + b.wfw * dt);
  b.crank = frac(b.crank + b.wrw * kGear * dt);
}

void rollWheels(BikeState& b, float dt) {
  const float rate = b.speed / (kTwoPi * kR * b.scale);   // turns/s
  b.wrw = rate;
  b.wfw = rate;
  turnWheels(b, dt);
}

// Wheels spinning down on their own.
void spinDown(BikeState& b, float dt, float tau_r, float tau_f) {
  b.wrw *= 1.f - ease1(dt, tau_r);
  b.wfw *= 1.f - ease1(dt, tau_f);
  turnWheels(b, dt);
}

// The stream: sprays as the wheels go in and out, rings at the contacts, the water laid over the tyres.
void waterWork(BikeState& b, SplashState& sp, Rng& rng, float dt, bool rear_down, bool front_down) {
  const BikeCourse& c = b.course;
  const float k = b.scale;
  const float d = (float)b.dir;
  const Xf xf = makeXf(b.pivot, b.pitch, b.dir, k, b.lay);
  const Vec2 rc = b.pivot, fc = xf.pt(kWB, 0.f);
  const bool rw = rear_down && inWater(c, rc.x), fw = front_down && inWater(c, fc.x);
  const bool rw0 = (b.flags & 1u) != 0, fw0 = (b.flags & 2u) != 0;
  b.flags = (uint8_t)((b.flags & ~3u) | (rw ? 1u : 0u) | (fw ? 2u : 0u));
  const bool rolling = b.speed > 15.f * k;

  // Entering: the front wheel throws a big fan ahead and up; the rear wheel a smaller one behind.
  if (fw && !fw0) {
    spawnCrown(sp, rng, fc.x, fc.y, 26.f * k, 0.10f * d);
    burst(sp, c, rng, fc.x, fc.y - 2.f * k, d, 18, 0.4f, 1.3f, 130.f * k, 270.f * k);
    spawnRing(sp, fc.x, fc.y, 18.f * k, 0.7f);
  }
  if (rw && !rw0) {
    spawnCrown(sp, rng, rc.x, rc.y, 18.f * k, -0.10f * d);
    burst(sp, c, rng, rc.x, rc.y - 2.f * k, -d, 12, 0.5f, 1.4f, 110.f * k, 230.f * k);
    spawnRing(sp, rc.x, rc.y, 14.f * k, 0.7f);
  }
  // Leaving: a smaller fan.
  if (!fw && fw0 && front_down) burst(sp, c, rng, fc.x, fc.y - 2.f * k, d, 8, 0.5f, 1.2f, 100.f * k, 200.f * k);
  if (!rw && rw0 && rear_down) burst(sp, c, rng, rc.x, rc.y - 2.f * k, -d, 6, 0.6f, 1.3f, 90.f * k, 170.f * k);

  // Rolling through it: a thin spray off each wheel and rings behind them.
  if (rolling) {
    if (fw) {
      b.spawn[1] += dt;
      while (b.spawn[1] >= 0.045f) {
        b.spawn[1] -= 0.045f;
        spawnBit(sp, c, rng, fc.x, fc.y - 2.f * k, d, 0.5f, 1.25f, 80.f * k, 175.f * k);
      }
      b.ring[1] += dt;
      if (b.ring[1] >= 0.2f) {
        b.ring[1] = 0.f;
        spawnRing(sp, fc.x - d * 2.f * k, fc.y, 9.f * k, 0.5f);
      }
      b.crown[1] += dt;   // a standing fan at the bow
      if (b.crown[1] >= 0.16f) {
        b.crown[1] = 0.f;
        spawnCrown(sp, rng, fc.x + d * 3.f * k, fc.y, 14.f * k, 0.12f * d);
      }
    }
    if (rw) {
      b.spawn[0] += dt;
      while (b.spawn[0] >= 0.05f) {
        b.spawn[0] -= 0.05f;
        spawnBit(sp, c, rng, rc.x, rc.y - 2.f * k, -d, 0.7f, 1.35f, 70.f * k, 160.f * k);
      }
      b.ring[0] += dt;
      if (b.ring[0] >= 0.2f) {
        b.ring[0] = 0.f;
        spawnRing(sp, rc.x - d * 3.f * k, rc.y, 9.f * k, 0.5f);
      }
      b.crown[0] += dt;   // and a rooster tail behind
      if (b.crown[0] >= 0.2f) {
        b.crown[0] = 0.f;
        spawnCrown(sp, rng, rc.x - d * 3.f * k, rc.y, 10.f * k, -0.12f * d);
      }
    }
  }

  // The water laid over the bottom of each tyre; over the whole length of a bike lying on its side.
  for (int i = 0; i < SPLASH_COVERS; ++i) sp.cover[i].on = 0;
  if (b.lay > 0.5f && (inWater(c, rc.x) || inWater(c, xf.pt(kWB, 0.f).x))) {
    const Vec2 m = xf.pt(0.5f * kWB, 0.f);
    sp.cover[0] = SplashCover{qpx(m.x), qpx(m.y - 1.5f * k), qpx(34.f * k), qpx(3.2f * k), 1, 0};
  } else {
    if (rw) sp.cover[0] = SplashCover{qpx(rc.x), qpx(rc.y - 1.2f * k), qpx(11.f * k), qpx(3.6f * k), 1, 0};
    if (fw) sp.cover[1] = SplashCover{qpx(fc.x), qpx(fc.y - 1.2f * k), qpx(11.f * k), qpx(3.6f * k), 1, 0};
  }
}

// Water laid over the sitting rider: from the hips down.
void riderCover(const BikeState& b, SplashState& sp, float H) {
  if (b.variant != BIKE_VAR_ROCK) return;
  if (b.rider != RIDER_SPLASH && b.rider != RIDER_DOWN) return;
  RiderOut o;
  riderFor(b, H, 0.f, o);
  const float k = b.scale;
  const float d = (float)b.dir;
  // Flat in the water the cover lies along his body from the hips; sitting, it is the stream round his hips and legs.
  const float e = b.rider == RIDER_DOWN ? 1.f : easeInOut(window(b.rt, kSplashUp0, kSplashUp1));
  const float in = b.rider == RIDER_DOWN ? 1.f : easeInOut(window(b.rt, 0.f, 0.3f));
  if (in <= 0.f) return;
  const Vec2 flat{o.hip.x + d * 10.f * k, o.hip.y + 5.f * k};
  const Vec2 sit{o.feet.x + d * 6.f * k, o.feet.y - 4.5f * k};
  const float cx = lerp(flat.x, sit.x, e), cy = lerp(flat.y, sit.y, e);
  const float rx = lerp(38.f, 26.f, e) * k, ry = lerp(3.8f, 6.5f, e) * k * in;
  sp.cover[2] = SplashCover{qpx(cx), qpx(cy), qpx(rx), qpx(ry), 1, 0};
}

// B: the rider goes into the water: a big splash.
void bigSplash(BikeState& b, SplashState& sp, Rng& rng, Vec2 at) {
  const float k = b.scale;
  const float d = (float)b.dir;
  const BikeCourse& c = b.course;
  spawnCrown(sp, rng, at.x, at.y, 30.f * k, 0.f);
  spawnCrown(sp, rng, at.x + d * 7.f * k, at.y, 20.f * k, 0.05f * d);
  burst(sp, c, rng, at.x, at.y - 3.f * k, d, 11, 0.5f, 1.5f, 120.f * k, 270.f * k);
  burst(sp, c, rng, at.x, at.y - 3.f * k, -d, 9, 0.7f, 1.5f, 110.f * k, 240.f * k);
  burst(sp, c, rng, at.x, at.y - 3.f * k, d, 6, 1.3f, 1.55f, 150.f * k, 250.f * k);
  spawnRing(sp, at.x, at.y, 24.f * k, 0.8f);
  spawnRing(sp, at.x + d * 4.f * k, at.y, 16.f * k, 0.6f);
}

void stepRide(BikeState& b, SplashState& sp, Rng& rng, float dt) {
  const BikeCourse& c = b.course;
  const float k = b.scale;
  const float d = (float)b.dir;
  const float xf0 = b.pivot.x + d * kWB * k;
  const bool wet = inWater(c, b.pivot.x) || inWater(c, xf0);
  const float want = (b.proud ? kProudSpeed : kRideSpeed) * k * (wet ? kWaterDrag : 1.f);
  b.speed += (want - b.speed) * ease1(dt, 0.35f);
  b.pivot.x += d * b.speed * dt;
  laneUnder(b, dt, false);
  b.pitch = b.lane_pitch;
  rollWheels(b, dt);

  const float H = 90.f * k;

  // B: the front tyre meets the rock.
  if (b.variant == BIKE_VAR_ROCK && c.rock_x > 0.f) {
    const float face = c.rock_x - d * kRockHalfW * k;
    const float lead = b.pivot.x + d * (kWB + kRockFaceReach) * k;   // the front tyre's leading edge at rock height
    if (d * (lead - face) >= 0.f) {
      b.pivot.x -= d * (lead - face) * 1.f;   // exactly touching
      laneUnder(b, 0.f, true);
      b.flags |= 4u;
      // The bike: stops dead, noses over about the front tyre's contact.
      const Xf xf = makeXf(b.pivot, b.pitch, b.dir, k, 0.f);
      b.mark = xf.pt(kWB, 0.f);
      b.mode = BIKE_ENDO;
      b.t = 0.f;
      b.speed = 0.f;
      b.wfw = 0.f;
      // The rider: leaves the saddle at the bike's old speed, a little up.
      HikerPose P{};
      Vec2 hip{0.f, 0.f};
      seatNow(b, H, P, hip);
      b.from = P;
      b.hip = hip;
      b.lean0 = P.lean;
      b.vel = Vec2{d * kRideSpeed * 1.35f * k, -95.f * k};
      b.rider = RIDER_FLY;
      b.rt = 0.f;
      // The tyre hitting the rock: a little spray ahead.
      spawnCrown(sp, rng, b.mark.x, b.mark.y, 11.f * k, 0.f);
      burst(sp, c, rng, b.mark.x, b.mark.y - 3.f * k, d, 6, 0.6f, 1.4f, 90.f * k, 190.f * k);
      return;
    }
  }
  // C: where the wheelie begins.
  if (b.variant == BIKE_VAR_WHEELIE && c.wheelie_x > 0.f && d * (b.pivot.x - c.wheelie_x) >= 0.f) {
    b.mode = BIKE_WHEELIE;
    b.t = 0.f;
    b.v0 = b.speed;
    return;
  }
  // The far edge.
  if ((b.dir > 0 && b.pivot.x > (float)SS_WIDTH + kStartX + 6.f) || (b.dir < 0 && b.pivot.x < -kStartX + 6.f - 44.f)) {
    b.mode = BIKE_AWAY;
    b.rider = RIDER_FREE;
    b.t = 0.f;
  }
}

void stepWheelie(BikeState& b, float dt) {
  const float k = b.scale;
  const float d = (float)b.dir;
  const float t = b.t;
  const float t_end = kWhPre + kWhPop + (kWhHoldEnd - kWhPre - kWhPop) + kWhTip;
  // He eases off the pedals through the hold and the tip; the bike rolls slower.
  const float v_hold = 58.f * k;
  b.speed = t < kWhHoldEnd ? lerp(b.v0, v_hold, window(t, 0.f, kWhHoldEnd)) : lerp(v_hold, 20.f * k, window(t, kWhHoldEnd, t_end));
  b.pivot.x += d * b.speed * dt;
  laneUnder(b, dt, false);
  b.pitch = b.lane_pitch + wheelieAngle(t);
  rollWheels(b, dt);
  if (t >= t_end) {
    // Past the point of no return: he comes off the back, the bike goes over.
    const float H = 90.f * k;
    HikerPose P{};
    Vec2 hip{0.f, 0.f};
    seatNow(b, H, P, hip);
    b.from = P;
    b.hip = hip;
    b.lean0 = P.lean;
    b.mark = b.pivot;
    b.mode = BIKE_TIP;
    b.t = 0.f;
    b.rider = RIDER_FALL;
    b.rt = 0.f;
    b.wrw = b.wfw = maxf(b.wrw, 1.3f);   // the wheels spin freely now
  }
}

void stepTip(BikeState& b, float dt) {
  const float k = b.scale;
  const float d = (float)b.dir;
  const float u = clamp01(b.t / kFallSecs);
  b.pitch = b.lane_pitch + kWhGone + kFallRate * u * kFallSecs + (0.3f - kFallRate * kFallSecs) * u * u;
  // The pivot (the rear tyre) is carried back a little as it goes over, and raised as the bike lies down on the
  // ground on its saddle and bars; at the end it rests on top of him, 13 px above the ground.
  b.rest_lift = 13.f * k * easeInOut(window(u, 0.55f, 1.f));
  const float xp = b.mark.x + d * 14.f * k * easeInOut(u);
  b.pivot.x = xp;
  b.pivot.y = bikeLaneY(b.course, xp);
  const float ground = b.pivot.y - b.rest_lift;
  const Xf xf = makeXf(b.pivot, b.pitch, b.dir, k, 0.f);
  const float low = lowestY(xf);
  float shift = 0.f;
  if (low > ground) shift = ground - low;                                   // never through the ground
  const float settle = window(u, 0.7f, 1.f);
  if (low < ground && settle > 0.f) shift = (ground - low) * easeInOut(settle);   // and down onto him at the end
  b.pivot.y += shift;
  spinDown(b, dt, 2.4f, 2.4f);
  if (b.rider == RIDER_FALL && b.rt >= 0.6f) b.rider = RIDER_DOWN;
  if (u >= 1.f) {
    b.mode = BIKE_DOWN;
    b.t = 0.f;
  }
}

void stepEndo(BikeState& b, SplashState& sp, Rng& rng, float dt) {
  const float k = b.scale;
  const float d = (float)b.dir;
  const float t = b.t;
  // About the front tyre's contact: the tail comes up, drops back, and the bike lies down on its side.
  float th;
  if (t < 0.22f) th = kEndoPitch * easeOutCubic(t / 0.22f);
  else th = lerp(kEndoPitch, -0.025f, easeInOut(window(t, 0.22f, 0.8f)));
  b.pitch = th;
  b.lay = easeOutBounce(window(t, 0.35f, 1.15f));
  const float cs = cosT(frac(th)), sn = sinT(frac(th));
  b.pivot = Vec2{b.mark.x - d * kWB * k * cs, b.mark.y + kWB * k * sn + 3.f * k * b.lay};
  spinDown(b, dt, 0.5f, 0.1f);
  if (t >= kEndoSecs) {
    b.mode = BIKE_DOWN;
    b.t = 0.f;
  }
  (void)sp;
  (void)rng;
}

// The rider's own motion: flying, then in the water.
void stepRider(BikeState& b, SplashState& sp, Rng& rng, float dt) {
  const float k = b.scale;
  const float d = (float)b.dir;
  const float H = 90.f * k;
  if (b.rider == RIDER_FLY) {
    b.vel.y += kGravity * k * dt;
    b.hip = add(b.hip, mul(b.vel, dt));
    // The hands (or the head) reach the water.
    HikerPose P;
    {
      RiderOut o;
      riderFor(b, H, 0.f, o);
      P = o.pose;
    }
    const RigGeo g = rigGeo(P, H, 0.f);
    const float y_hand = b.hip.y + minf(g.hand[0].y, g.head.y + kHeadR * H) + 2.f * k;   // the lowest of hand and head
    const float x_hand = b.hip.x + d * g.hand[0].x;
    const float water = bikeLaneY(b.course, x_hand);
    if (y_hand >= water || b.rt > 1.4f) {
      const Vec2 at{x_hand, water};
      b.from = P;
      b.lean0 = lerp(b.lean0, kDiveLean, easeInOut(window(b.rt, 0.f, 0.55f)));
      b.vel = at;
      b.rider = RIDER_SPLASH;
      b.rt = 0.f;
      b.flags |= 8u;
      bigSplash(b, sp, rng, at);
    }
    return;
  }
  if (b.rider == RIDER_SPLASH) {
    const float rt0 = b.rt - dt;
    if (rt0 < 0.14f && b.rt >= 0.14f) {   // the second heave as he goes under
      spawnCrown(sp, rng, b.vel.x + d * 12.f * k, b.vel.y, 22.f * k, 0.f);
      burst(sp, b.course, rng, b.vel.x + d * 10.f * k, b.vel.y - 3.f * k, d, 7, 0.9f, 1.55f, 100.f * k, 200.f * k);
      spawnRing(sp, b.vel.x + d * 10.f * k, b.vel.y, 20.f * k, 0.7f);
    }
    if (b.rt >= kSplashDone) b.rider = RIDER_DOWN;
  }
}

}  // namespace

void bikeStep(BikeState& b, SplashState& sp, Rng& rng, float dt) {
  if (!finite(dt) || dt < 0.f) dt = 0.f;
  if (dt > kMaxDt) dt = kMaxDt;
  splashAdvance(sp, dt);
  for (int i = 0; i < SPLASH_COVERS; ++i) sp.cover[i].on = 0;
  if (b.mode == BIKE_HIDDEN || b.mode == BIKE_AWAY || b.mode >= BIKE_MODES) return;

  b.t += dt;
  if (b.rider != RIDER_FREE) b.rt += dt;
  const float H = 90.f * b.scale;
  bool rear_down = false, front_down = false;
  switch (b.mode) {
    case BIKE_RIDE:
      stepRide(b, sp, rng, dt);
      rear_down = front_down = true;
      break;
    case BIKE_WHEELIE:
      stepWheelie(b, dt);
      rear_down = true;
      break;
    case BIKE_TIP: stepTip(b, dt); break;
    case BIKE_ENDO:
      stepEndo(b, sp, rng, dt);
      front_down = b.t < 0.5f;
      rear_down = b.t > 0.3f;
      break;
    case BIKE_DOWN:
      if (b.variant == BIKE_VAR_WHEELIE) {
        spinDown(b, dt, 2.4f, 2.4f);
      } else {
        spinDown(b, dt, 0.5f, 0.1f);
        rear_down = front_down = true;
      }
      break;
    default: break;
  }
  if (b.mode == BIKE_HIDDEN || b.mode == BIKE_AWAY) return;   // the ride just ended
  if (b.variant == BIKE_VAR_ROCK || b.variant == BIKE_VAR_PASS) waterWork(b, sp, rng, dt, rear_down, front_down);
  stepRider(b, sp, rng, dt);
  riderCover(b, sp, H);

  if (!finite(b.pivot.x) || !finite(b.pivot.y) || !finite(b.pitch) || !finite(b.speed) || !finite(b.hip.x) || !finite(b.hip.y)) {
    bikeHide(b);   // never expected
  }
}

// ---- bounds, dirty, draw (the bike) -------------------------------------------------------------------------------------------------------------

Rect bikeBounds(const BikeState& b) {
  Look lk;
  makeLook(b, lk);
  Geom g;
  makeGeom(lk, g);
  return geomBounds(g);
}

bool bikeSame(const BikeState& a, const BikeState& b) {
  Rect none[BIKE_DIRTY_MAX];
  return bikeDirty(a, b, none) == 0;
}

int bikeDirty(const BikeState& before, const BikeState& after, Rect out[BIKE_DIRTY_MAX]) {
  Look la, lb;
  makeLook(before, la);
  makeLook(after, lb);
  if (memcmp(&la, &lb, sizeof la) == 0) return 0;
  Geom ga, gb;
  makeGeom(la, ga);
  makeGeom(lb, gb);
  const Rect ra = geomBounds(ga), rb = geomBounds(gb);
  if (ra.empty() && rb.empty()) return 0;
  if (ra.empty() || rb.empty() || touches(ra, rb)) {
    out[0] = unite(ra, rb);
    return 1;
  }
  out[0] = ra;
  out[1] = rb;
  return 2;
}

namespace {

void drawRing(Surface& s, const Prim& p, Pixel col, const Rect& clip) {
  const Vec2 c = p.p[0], au = p.p[1], aw = p.p[2];
  Vec2 pts[2 * kRingN + 2];
  int n = 0;
  for (int i = 0; i < kRingN; ++i) {
    const float a = (float)i / (float)kRingN;
    const float ca = cosT(a), sa = sinT(a);
    pts[n++] = Vec2{c.x + au.x * ca + aw.x * sa, c.y + au.y * ca + aw.y * sa};
  }
  pts[n++] = pts[0];
  for (int i = 0; i < kRingN; ++i) {   // the inner edge, the other way round: a hole
    const float a = (float)((kRingN - i) % kRingN) / (float)kRingN;
    const float ca = cosT(a) * p.w, sa = sinT(a) * p.w;
    pts[n++] = Vec2{c.x + au.x * ca + aw.x * sa, c.y + au.y * ca + aw.y * sa};
  }
  pts[n++] = pts[kRingN + 1];
  fillPolygon(s, pts, n, col, clip);
}

}  // namespace

void bikeDraw(const BikeState& b, Surface& strip, const Rect& clip, const Palette& pal) {
  Look lk;
  makeLook(b, lk);
  if (!lk.vis && !lk.rock) return;
  Geom g;
  makeGeom(lk, g);
  if (g.rock && overlaps(rockRect(g), clip)) fillPolygon(strip, g.rock_pts, 7, pal[ROCK], clip);
  for (int i = 0; i < g.n; ++i) {
    const Prim& p = g.v[i];
    if (!overlaps(primRect(p), clip)) continue;
    const Pixel col = pal[p.slot];
    switch (p.kind) {
      case PK_CAP: strokeCapsule(strip, p.p[0], p.p[1], p.w, col, clip); break;
      case PK_CIRCLE: fillCircle(strip, p.p[0], p.w, col, clip); break;
      default: drawRing(strip, p, col, clip); break;
    }
  }
}

// ---- bounds, dirty, draw (the splash) -------------------------------------------------------------------------------------------------------------

namespace {

struct BitLook {
  int16_t x, y;
  uint8_t r4;   // radius in 1/4 px, 0: not drawn
};
struct RingLook {
  int16_t x, y;
  uint8_t rx2;   // half-width in 1/2 px, 0: not drawn
  uint8_t a;     // opacity, 1..8
};
struct CrownLook {
  int16_t x, y;
  uint8_t age;   // 0..16, 255: not drawn
  uint8_t size;  // px
  int8_t tilt;
};

BitLook bitLook(const SplashBit& s) {
  BitLook l{0, 0, 0};
  if (s.kind == 0 || !finite(s.x) || !finite(s.y)) return l;
  float r = (float)s.r8 / 8.f;
  if (s.kind == 2) r *= clamp01(s.life * 4.f);
  l.x = qpx(s.x);
  l.y = qpx(s.y);
  l.r4 = (uint8_t)clampf(r * 4.f + 0.5f, 0.f, 12.f);
  return l;
}
RingLook ringLook(const SplashRing& r) {
  RingLook l{0, 0, 0, 0};
  if (!r.on || !finite(r.x) || !finite(r.y)) return l;
  const float u = clamp01(r.age / r.life);
  const float rx = maxf(1.f, ((float)r.r8 / 8.f) * easeOutCubic(u));
  l.x = qpx(r.x);
  l.y = qpx(r.y);
  l.rx2 = (uint8_t)clampf(rx * 2.f + 0.5f, 1.f, 120.f);
  l.a = (uint8_t)clampf(8.f * (1.f - u) + 0.5f, 1.f, 8.f);
  return l;
}
CrownLook crownLook(const SplashCrown& c) {
  CrownLook l{0, 0, 255, 0, 0};
  if (!c.on || !finite(c.x) || !finite(c.y)) return l;
  l.x = qpx(c.x);
  l.y = qpx(c.y);
  l.age = (uint8_t)clampf(c.age / kCrownLife * 16.f, 0.f, 16.f);
  l.size = (uint8_t)clampf(c.size + 0.5f, 1.f, 120.f);
  l.tilt = c.tilt;
  return l;
}

Rect bitRect(const BitLook& l) {
  if (l.r4 == 0) return kEmpty;
  const int r = (int)(l.r4 + 3) / 4 + 2;
  return Rect{l.x - r, l.y - r, l.x + r + 1, l.y + r + 1};
}
// Ring ellipse: ry is a third of rx, at least 0.8 px.
void ringRadii(const RingLook& l, float& rx, float& ry) {
  rx = (float)l.rx2 * 0.5f;
  ry = maxf(0.8f, rx * 0.32f);
}
Rect ringRect(const RingLook& l) {
  if (l.rx2 == 0) return kEmpty;
  float rx, ry;
  ringRadii(l, rx, ry);
  return Rect{l.x - (int)ceilf(rx) - 2, l.y - (int)ceilf(ry) - 2, l.x + (int)ceilf(rx) + 3, l.y + (int)ceilf(ry) + 3};
}

constexpr int kCrownPts = 11;   // two base corners, five tips, four valleys
// The crown: a row of five uneven spikes standing on a base line, leaning with `tilt`.
void crownPoly(const CrownLook& l, Vec2 out[kCrownPts]) {
  static const float kTip[5] = {0.7f, 1.f, 0.85f, 1.f, 0.65f};
  const float u = (float)l.age / 16.f;                       // 0..1 of its life
  const float grow = easeOutCubic(clamp01(u / 0.42f));
  const float fade = 1.f - easeInOut(window(u, 0.5f, 1.f));
  const float h = (float)l.size * 1.05f * grow * fade;
  const float w = (float)l.size * (0.5f + 0.6f * u);
  const float lean = (float)l.tilt / 512.f * (float)l.size * 6.f;
  const float x = (float)l.x, y = (float)l.y;
  int n = 0;
  out[n++] = Vec2{x - 0.5f * w, y + 0.5f};
  for (int i = 0; i < 5; ++i) {
    const float f = ((float)i - 2.f) / 2.f;   // -1..1
    const float bx = x + f * 0.5f * w;
    const float th = h * kTip[i];
    out[n++] = Vec2{bx + lean * th / maxf(1.f, h) + f * 0.25f * w * grow, y - th};
    if (i < 4) {
      const float vx = x + (f + 0.5f) * 0.5f * w;
      out[n++] = Vec2{vx + lean * 0.3f, y - 0.32f * th};
    }
  }
  out[n++] = Vec2{x + 0.5f * w, y + 0.5f};
}
Rect crownRect(const CrownLook& l) {
  if (l.age == 255) return kEmpty;
  Vec2 p[kCrownPts];
  crownPoly(l, p);
  Ext e;
  for (const Vec2& q : p) e.pt(q, 0.5f);
  return e.rect();
}
Rect coverRect(const SplashCover& c) {
  if (!c.on) return kEmpty;
  return Rect{c.x - c.rx - 2, c.y - c.ry - 2, c.x + c.rx + 3, c.y + c.ry + 3};
}

// What the draw reads of the whole splash, slot by slot.
struct SplashLook {
  BitLook bit[SPLASH_BITS];
  RingLook ring[SPLASH_RINGS];
  CrownLook crown[SPLASH_CROWNS];
  SplashCover cover[SPLASH_COVERS];
};
void makeSplashLook(const SplashState& sp, SplashLook& l) {
  memset(&l, 0, sizeof l);
  for (int i = 0; i < SPLASH_BITS; ++i) l.bit[i] = bitLook(sp.bit[i]);
  for (int i = 0; i < SPLASH_RINGS; ++i) l.ring[i] = ringLook(sp.ring[i]);
  for (int i = 0; i < SPLASH_CROWNS; ++i) l.crown[i] = crownLook(sp.crown[i]);
  for (int i = 0; i < SPLASH_COVERS; ++i) {
    l.cover[i] = sp.cover[i];
    if (!l.cover[i].on) l.cover[i] = SplashCover{0, 0, 0, 0, 0, 0};
  }
}

// A set of at most SPLASH_DIRTY_MAX rects that grows by merging.
struct RectSet {
  Rect r[SPLASH_DIRTY_MAX];
  int n = 0;
  void add(const Rect& a) {
    if (a.empty()) return;
    Rect m = a;
    // Fold in everything it touches (possibly in a chain).
    bool again = true;
    while (again) {
      again = false;
      for (int i = 0; i < n; ++i) {
        if (touches(m, r[i])) {
          m = unite(m, r[i]);
          r[i] = r[--n];
          again = true;
          break;
        }
      }
    }
    if (n < SPLASH_DIRTY_MAX) {
      r[n++] = m;
      return;
    }
    // Full: merge into the rect it enlarges the least.
    int best = 0;
    int64_t best_cost = -1;
    for (int i = 0; i < n; ++i) {
      const int64_t cost = area(unite(m, r[i])) - area(r[i]);
      if (best_cost < 0 || cost < best_cost) {
        best_cost = cost;
        best = i;
      }
    }
    r[best] = unite(m, r[best]);
  }
};

}  // namespace

Rect splashBounds(const SplashState& sp) {
  SplashLook l;
  makeSplashLook(sp, l);
  Rect r = kEmpty;
  for (int i = 0; i < SPLASH_BITS; ++i) r = unite(r, bitRect(l.bit[i]));
  for (int i = 0; i < SPLASH_RINGS; ++i) r = unite(r, ringRect(l.ring[i]));
  for (int i = 0; i < SPLASH_CROWNS; ++i) r = unite(r, crownRect(l.crown[i]));
  for (int i = 0; i < SPLASH_COVERS; ++i) r = unite(r, coverRect(l.cover[i]));
  return r;
}

bool splashSame(const SplashState& a, const SplashState& b) {
  SplashLook la, lb;
  makeSplashLook(a, la);
  makeSplashLook(b, lb);
  return memcmp(&la, &lb, sizeof la) == 0;
}

int splashDirty(const SplashState& before, const SplashState& after, Rect out[SPLASH_DIRTY_MAX]) {
  SplashLook la, lb;
  makeSplashLook(before, la);
  makeSplashLook(after, lb);
  RectSet set;
  for (int i = 0; i < SPLASH_BITS; ++i) {
    if (memcmp(&la.bit[i], &lb.bit[i], sizeof la.bit[i]) == 0) continue;
    set.add(bitRect(la.bit[i]));
    set.add(bitRect(lb.bit[i]));
  }
  for (int i = 0; i < SPLASH_RINGS; ++i) {
    if (memcmp(&la.ring[i], &lb.ring[i], sizeof la.ring[i]) == 0) continue;
    set.add(ringRect(la.ring[i]));
    set.add(ringRect(lb.ring[i]));
  }
  for (int i = 0; i < SPLASH_CROWNS; ++i) {
    if (memcmp(&la.crown[i], &lb.crown[i], sizeof la.crown[i]) == 0) continue;
    set.add(crownRect(la.crown[i]));
    set.add(crownRect(lb.crown[i]));
  }
  for (int i = 0; i < SPLASH_COVERS; ++i) {
    if (memcmp(&la.cover[i], &lb.cover[i], sizeof la.cover[i]) == 0) continue;
    set.add(coverRect(la.cover[i]));
    set.add(coverRect(lb.cover[i]));
  }
  for (int i = 0; i < set.n; ++i) out[i] = set.r[i];
  return set.n;
}

void splashDraw(const SplashState& sp, Surface& strip, const Rect& clip, const Palette& pal) {
  SplashLook l;
  makeSplashLook(sp, l);
  const Pixel foam = pal[WATER_LIGHT], water = pal[WATER];
  // Rings lie flat on the water, under everything.
  for (int i = 0; i < SPLASH_RINGS; ++i) {
    const RingLook& r = l.ring[i];
    if (r.rx2 == 0 || !overlaps(ringRect(r), clip)) continue;
    float rx, ry;
    ringRadii(r, rx, ry);
    const uint8_t alpha = (uint8_t)(r.a * 28);
    if (rx < 3.f) continue;   // too small to be a ring
    constexpr int n = 14;
    Vec2 pts[2 * n + 2];
    int m = 0;
    for (int j = 0; j < n; ++j) {
      const float a = (float)j / (float)n;
      pts[m++] = Vec2{(float)r.x + rx * cosT(a), (float)r.y + ry * sinT(a)};
    }
    pts[m++] = pts[0];
    const float rxi = maxf(0.2f, rx - 1.2f), ryi = maxf(0.1f, ry - 0.8f);
    for (int j = 0; j < n; ++j) {
      const float a = (float)((n - j) % n) / (float)n;
      pts[m++] = Vec2{(float)r.x + rxi * cosT(a), (float)r.y + ryi * sinT(a)};
    }
    pts[m++] = pts[n + 1];
    fillPolygonAlpha(strip, pts, m, foam, alpha, clip);
  }
  for (int i = 0; i < SPLASH_COVERS; ++i) {
    const SplashCover& c = l.cover[i];
    if (!c.on || !overlaps(coverRect(c), clip)) continue;
    fillEllipse(strip, Vec2{(float)c.x, (float)c.y}, (float)c.rx, (float)c.ry, water, clip);
  }
  for (int i = 0; i < SPLASH_CROWNS; ++i) {
    const CrownLook& c = l.crown[i];
    if (c.age == 255 || !overlaps(crownRect(c), clip)) continue;
    Vec2 p[kCrownPts];
    crownPoly(c, p);
    fillPolygon(strip, p, kCrownPts, foam, clip);
  }
  for (int i = 0; i < SPLASH_BITS; ++i) {
    const BitLook& t = l.bit[i];
    if (t.r4 == 0 || !overlaps(bitRect(t), clip)) continue;
    fillCircle(strip, Vec2{(float)t.x + 0.5f, (float)t.y + 0.5f}, (float)t.r4 * 0.25f, foam, clip);
  }
}

}  // namespace ss
