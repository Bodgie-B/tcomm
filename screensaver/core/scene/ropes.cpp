// The ropes course. See ropes.h.
//
// Three layers, bottom up:
//   1. The props. Every draw reads a Look: the state quantized to whole pixels
//      and small steps, so two states with the same Look draw the same pixels and
//      a tick that changes nothing visible marks nothing. A Look is turned into
//      primitives (capsules and one ring) per part; bounds, dirty rects and the
//      draw are all folded from that one list, so they cannot disagree.
//   2. A copy of the hiker rig's kinematics (kin), to put his hips where a floor,
//      a tyre or a caught ankle says they are (the rig only grounds its lowest
//      contact on feet.y). Keep it in step with hiker.cpp; test_ropes compares it
//      with hikerHeadPos and hikerHandPos on every pose.
//   3. The script: a plan of segments, each entered once (commands to the rig)
//      and ticked (props), and the overlay on the composed pose (ropesPose).
#include "ropes.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"

namespace ss {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;
constexpr float kInvTwoPi = 1.f / kTwoPi;

const Rect kEmpty{0, 0, 0, 0};

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool finite(float v) { return v - v == 0.f; }   // false for NaN and infinity
inline float fl(int v) { return (float)v; }
inline int iround(float v) { return (int)floorf(v + 0.5f); }
inline Vec2 V(float x, float y) { return Vec2{x, y}; }
inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
              a.y1 > b.y1 ? a.y1 : b.y1};
}
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }
// Angles folded to the branch nearest `ref`.
inline float unwrapNear(float a, float ref) { return a + kTwoPi * floorf((ref - a) * kInvTwoPi + 0.5f); }

// ---- the course (all PROPOSED; how it looks is UNVERIFIED) -----------------------------------------

constexpr float kPostH[4] = {94.f, 124.f, 156.f, 156.f};   // full heights
constexpr float kPostW = 5.f;
constexpr float kBrA = kRopesRampX;          // the walkway starts here (x from the left foot) ...
constexpr float kBrB = 122.f;                // ... and ends here
constexpr float kPlatEnd = 134.f;            // the landing past it
constexpr float kHandH = 54.f;               // the hand ropes above the deck ends
constexpr float kHandSag = 4.f;
constexpr float kHandFar = 7.f;              // the far rope stands this much higher
constexpr float kSag = 9.f;                  // the slack of the walkway
constexpr int kPlanks = 13;
constexpr float kBeam1 = 118.f;              // the swing's beam above the ground
constexpr float kBeam2 = 144.f;              // the net's beam
constexpr float kRopeL = 82.f;               // the swing's rope
constexpr float kTyreD = 9.f;                // from the rope's end to the tyre's centre
constexpr float kRingRx = 14.f, kRingRy = 6.f, kRingIn = 0.55f;
constexpr float kNetCell = 10.f;
constexpr int kNetCols = 6, kNetRows = 14;   // rows 0 (top) .. 13 (the foot), 10 px apart, the foot 10 px off the ground
constexpr float kNetLen = kBeam2 - 2.f - 8.f;   // from under the beam to the foot of the net's columns

// ---- the look: everything the draw reads, quantized -----------------------------------------------------

struct Look {
  int16_t ox, gy;
  int16_t post[4];     // heights, px (an overshoot stands taller than the final)
  uint8_t items;       // 0: nothing is drawn
  uint8_t d;           // bridge dressing, 0..32
  uint8_t sl;          // the walkway's slack, 1/16 (0..~22)
  uint8_t b1, b2;      // the two beams laid, 0..32
  uint8_t ty;          // the tyre let down on its rope, 1/32 of the rope (overshoots)
  uint8_t nd;          // the net let down, 1/32 of its length (overshoots)
  int8_t dep;          // the walkway pressed down under him, 1/2 px
  int8_t bnc;          // the whole span's bounce, 1/2 px
  int16_t bx;          // where he stands on it
  int8_t thq;          // the pendulum, 1/128 rad
  uint8_t spq;         // the tyre's twist, 1/64 turn
  int8_t nsw;          // the net's sway at its foot, 1/2 px
};

int8_t qi8(float v) { return finite(v) ? (int8_t)clampf(floorf(v + 0.5f), -127.f, 127.f) : (int8_t)0; }
uint8_t qu8(float v, float hi) { return finite(v) ? (uint8_t)clampf(floorf(v + 0.5f), 0.f, hi) : (uint8_t)0; }
int16_t qi16(float v) { return finite(v) ? (int16_t)clampf(floorf(v + 0.5f), -16000.f, 16000.f) : (int16_t)0; }

int popcount3(uint8_t m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1); }

// Which of the four posts a set of items needs (bit k: post k).
int postsOf(uint8_t items) {
  int m = 0;
  if (items & ROPES_BRIDGE) m |= 1 | 2;
  if (items & ROPES_SWING) m |= 2 | 4;
  if (items & ROPES_NET) m |= 4 | 8;
  return m;
}

// The build, in seconds of the build clock: the posts stand up 0.55 s apart (0.95 s
// each, with an overshoot), and the items are dressed one after another from 1.6 s,
// 1.4 s apart, 1.7 s each. The tear-down plays the same clock backwards.
float buildSeconds(uint8_t items) {
  const int n = popcount3(items);
  return 1.6f + 1.4f * (float)(n > 0 ? n - 1 : 0) + 1.7f + 0.9f;
}

struct Bp {
  float post[4];
  float d, b1, b2, ty, nd;
};

void buildAt(float bt, uint8_t items, Bp& b) {
  memset(&b, 0, sizeof b);
  const int mask = postsOf(items);
  int rank = 0;
  for (int k = 0; k < 4; ++k) {
    if (!(mask & (1 << k))) continue;
    const float t0 = 0.55f * (float)rank;
    b.post[k] = easeOutBack(window(bt, t0, t0 + 0.95f));
    ++rank;
  }
  int r = 0;
  for (int it = 0; it < 3; ++it) {
    if (!(items & (1 << it))) continue;
    const float t0 = 1.6f + 1.4f * (float)r;
    if (it == 0) b.d = window(bt, t0, t0 + 1.7f);
    if (it == 1) {
      b.b1 = window(bt, t0, t0 + 0.6f);
      b.ty = easeOutBack(window(bt, t0 + 0.5f, t0 + 1.7f));
    }
    if (it == 2) {
      b.b2 = window(bt, t0, t0 + 0.6f);
      b.nd = easeOutBack(window(bt, t0 + 0.5f, t0 + 1.7f));
    }
    ++r;
  }
}

void lookOf(const RopesState& r, Look& L) {
  memset(&L, 0, sizeof L);
  if (r.phase == ROPES_IDLE) return;
  L.ox = qi16(r.x0);
  L.gy = qi16(r.gy);
  L.items = r.items;
  Bp b;
  buildAt(r.bt, r.items, b);
  for (int k = 0; k < 4; ++k) L.post[k] = qi16(b.post[k] * kPostH[k]);
  L.d = qu8(b.d * 32.f, 32.f);
  L.sl = qu8(easeOutBack(window(b.d, 0.7f, 1.f)) * 16.f, 24.f);
  L.b1 = qu8(b.b1 * 32.f, 32.f);
  L.b2 = qu8(b.b2 * 32.f, 32.f);
  L.ty = qu8(b.ty * 32.f, 40.f);
  L.nd = qu8(b.nd * 32.f, 40.f);
  L.dep = qi8(r.dep * 2.f);
  L.bnc = qi8(r.bnc * 2.f);
  L.bx = qi16(r.bx);
  L.thq = qi8(r.th * 128.f);
  L.spq = (uint8_t)((int)floorf((r.spin - floorf(r.spin)) * 64.f) & 63);
  L.nsw = qi8(r.nsw * 2.f);
  // What nothing shows is zeroed, so two states that draw alike have the same Look.
  if (!(L.items & ROPES_BRIDGE)) L.d = L.sl = 0;
  if (L.d < 28 || L.dep == 0) L.dep = 0, L.bx = 0;
  if (L.d < 28) L.bnc = 0;
  if (!(L.items & ROPES_SWING)) L.b1 = L.ty = 0;
  if (L.ty == 0) L.thq = 0, L.spq = 0;
  if (!(L.items & ROPES_NET)) L.b2 = L.nd = 0;
  if (L.nd == 0) L.nsw = 0;
}

// ---- geometry from a Look -------------------------------------------------------------------------------

// The walkway's centre line at frame x.
float deckYL(const Look& L, float x) {
  const float xa = fl(L.ox) + kBrA, xb = fl(L.ox) + kBrB;
  const float u = clamp01((x - xa) / (xb - xa));
  const float s = sinT(0.5f * u);   // sin(pi u): the first mode, 0 at the posts
  float y = fl(L.gy) - kRopesDeckH + kSag * (fl(L.sl) * (1.f / 16.f)) * 4.f * u * (1.f - u);
  if (L.dep != 0) {
    const float dd = x - fl(L.bx);
    if (dd > -36.f && dd < 36.f) y += fl(L.dep) * 0.5f * 0.5f * (1.f + cosT(dd * (1.f / 72.f))) * s;
  }
  return y + fl(L.bnc) * 0.5f * s;
}

float handYL(const Look& L, float x, bool far_rope) {
  const float a = fl(L.ox) + kRopesPostX[0] + 0.5f * kPostW, b = fl(L.ox) + kRopesPostX[1] - 0.5f * kPostW;
  const float v = clamp01((x - a) / (b - a));
  float y = fl(L.gy) - kRopesDeckH - kHandH + kHandSag * (fl(L.sl) * (1.f / 16.f)) * 4.f * v * (1.f - v);
  if (far_rope) y -= kHandFar;
  return y;
}

// The top face of whatever he stands on, on the bridge side of the course.
float floorOfL(const Look& L, float x) {
  const float gy = fl(L.gy);
  if (!(L.items & ROPES_BRIDGE) || L.d < 8) return gy;
  const float rx = x - fl(L.ox);
  if (rx <= 0.f) return gy;
  if (rx < kBrA) return gy - (kRopesDeckH + 1.5f) * rx / kBrA;
  if (rx <= kBrB) return deckYL(L, x) - 1.5f;
  if (rx <= kPlatEnd) return gy - kRopesDeckH - 1.5f;
  return gy;
}

// The tyre: rope end E and centre C.
void tyreOf(const Look& L, Vec2& E, Vec2& C) {
  const float th = fl(L.thq) * (1.f / 128.f);
  const float s = sinT(th * kInvTwoPi), c = cosT(th * kInvTwoPi);
  const float p = fl(L.ty) * (1.f / 32.f);
  const Vec2 att = V(fl(L.ox) + kRopesSwingX, fl(L.gy) - kBeam1 + 2.f);
  const float len = kRopeL * p;
  E = V(att.x + len * s, att.y + len * c);
  C = V(att.x + (len + kTyreD * clamp01(p)) * s, att.y + (len + kTyreD * clamp01(p)) * c);
}

float netTop(const Look& L) { return fl(L.gy) - kBeam2 + 2.f; }
float netShear(const Look& L, float y) {
  const float v = clamp01((y - netTop(L)) / kNetLen);
  return fl(L.nsw) * 0.5f * v * v;
}
float netRowY(const Look& L, int row) { return fl(L.gy) - 140.f + 10.f * fl(row); }

// ---- primitives -----------------------------------------------------------------------------------------

enum PrimKind : uint8_t { PK_CAP, PK_RING };

struct Prim {
  uint8_t kind;
  uint8_t slot;
  float w;       // capsule width
  Vec2 a, b;     // capsule ends; the ring: a the centre, b (rx, ry)
  float tilt;    // ring: radians
};

constexpr int kPartMax = 48;
struct PL {
  int n;
  Prim v[kPartMax];
};

void cap(PL& pl, Vec2 a, Vec2 b, float w, Slot s) {
  if (pl.n < kPartMax) pl.v[pl.n++] = Prim{PK_CAP, (uint8_t)s, w, a, b, 0.f};
}
void ring(PL& pl, Vec2 c, float rx, float ry, float tilt, Slot s) {
  if (pl.n < kPartMax) pl.v[pl.n++] = Prim{PK_RING, (uint8_t)s, 0.f, c, V(rx, ry), tilt};
}

enum Part : uint8_t { PT_POST0 = 0, PT_BR_FIX = 4, PT_BR_DECK, PT_BEAMS, PT_SWING, PT_NET, PT_COUNT };
static_assert(PT_COUNT == ROPES_DIRTY_MAX, "one dirty rect per part");

void partPost(const Look& L, int k, PL& pl) {
  if (!(postsOf(L.items) & (1 << k)) || L.post[k] < 3) return;
  const float x = fl(L.ox) + kRopesPostX[k], y = fl(L.gy);
  cap(pl, V(x, y), V(x, y - fl(L.post[k])), kPostW, TRUNK);
}

void partBridgeFix(const Look& L, PL& pl) {
  if (!(L.items & ROPES_BRIDGE) || L.d == 0) return;
  const float ox = fl(L.ox), gy = fl(L.gy), dk = fl(L.d) * (1.f / 32.f);
  if (dk > 0.04f) {
    // The boarding ramp (its top face is floorOfL's line) and the landing, each on a leg.
    cap(pl, V(ox, gy + 3.1f), V(ox + kBrA, gy - kRopesDeckH - 1.5f + 3.1f), 4.5f, TRUNK);
    cap(pl, V(ox + kBrA - 3.f, gy - kRopesDeckH + 2.f), V(ox + kBrA - 3.f, gy), 2.6f, TRUNK);
    cap(pl, V(ox + kBrB, gy - kRopesDeckH + 0.75f), V(ox + kPlatEnd, gy - kRopesDeckH + 0.75f), 4.5f, TRUNK);
    cap(pl, V(ox + kPlatEnd - 3.f, gy - kRopesDeckH + 2.f), V(ox + kPlatEnd - 3.f, gy), 2.6f, TRUNK);
  }
  // The hand ropes are strung from the first post toward the second.
  const float prog = easeInOut(window(dk, 0.05f, 0.4f));
  if (prog > 0.01f) {
    const float a = ox + kRopesPostX[0] + 0.5f * kPostW, b = ox + kRopesPostX[1] - 0.5f * kPostW;
    for (int far_rope = 0; far_rope < 2; ++far_rope) {
      Vec2 prev = V(a, handYL(L, a, far_rope != 0));
      for (int i = 1; i <= 8; ++i) {
        const float x = a + (b - a) * prog * (fl(i) * 0.125f);
        const Vec2 cur = V(x, handYL(L, x, far_rope != 0));
        cap(pl, prev, cur, 1.3f, TENT_LIT);
        prev = cur;
      }
    }
  }
}

void partBridgeDeck(const Look& L, PL& pl) {
  if (!(L.items & ROPES_BRIDGE) || L.d < 8) return;
  const float dk = fl(L.d) * (1.f / 32.f);
  const int nvis = (int)floorf(fl(kPlanks) * window(dk, 0.25f, 0.85f) + 0.001f);
  if (nvis < 1) return;
  const float xa = fl(L.ox) + kBrA, xb = fl(L.ox) + kBrB;
  const float pitch = (xb - xa) / fl(kPlanks), hl = pitch * 0.5f - 0.6f;
  for (int i = 0; i < nvis; ++i) {
    const float xc = xa + (fl(i) + 0.5f) * pitch;
    const float y = deckYL(L, xc);
    const float tx0 = pitch, ty0 = deckYL(L, xc + 0.5f * pitch) - deckYL(L, xc - 0.5f * pitch);
    const float tn = sqrtf(tx0 * tx0 + ty0 * ty0);
    const float tx = tx0 / tn, ty = ty0 / tn;
    cap(pl, V(xc - tx * hl, y - ty * hl), V(xc + tx * hl, y + ty * hl), 3.f, TRUNK);
  }
  Vec2 prev = V(xa, deckYL(L, xa) + 2.2f);
  for (int i = 1; i <= nvis; ++i) {
    const float x = xa + fl(i) * pitch;
    const Vec2 cur = V(x, deckYL(L, x) + 2.2f);
    cap(pl, prev, cur, 1.1f, TENT_LIT);
    prev = cur;
  }
  if (nvis == kPlanks && dk >= 0.95f) {
    static const int kHang[4] = {2, 5, 8, 11};
    for (int i = 0; i < 4; ++i) {
      const float x = xa + (fl(kHang[i]) + 0.5f) * pitch;
      cap(pl, V(x, handYL(L, x, false) + 0.5f), V(x, deckYL(L, x) - 1.6f), 0.9f, TENT_LIT);
    }
  }
}

void partBeams(const Look& L, PL& pl) {
  const float ox = fl(L.ox), gy = fl(L.gy);
  if ((L.items & ROPES_SWING) && L.b1 > 0) {
    const float e = fl(L.b1) * (1.f / 32.f);
    const float x0 = ox + kRopesPostX[1], x1 = ox + kRopesPostX[2];
    cap(pl, V(x0, gy - kBeam1), V(lerp(x0, x1, e), gy - kBeam1), 4.5f, TRUNK);
  }
  if ((L.items & ROPES_NET) && L.b2 > 0) {
    const float e = fl(L.b2) * (1.f / 32.f);
    const float x0 = ox + kRopesPostX[2], x1 = ox + kRopesPostX[3];
    cap(pl, V(x0, gy - kBeam2), V(lerp(x0, x1, e), gy - kBeam2), 4.5f, TRUNK);
  }
}

void partSwing(const Look& L, PL& pl) {
  if (!(L.items & ROPES_SWING) || L.ty == 0) return;
  Vec2 E, C;
  tyreOf(L, E, C);
  const float th = fl(L.thq) * (1.f / 128.f);
  const Vec2 att = V(fl(L.ox) + kRopesSwingX, fl(L.gy) - kBeam1 + 2.f);
  cap(pl, att, E, 1.5f, TENT_LIT);
  // The bridle: two lines from the rope's end to the tyre's rim, which swap sides as it twists.
  const float sp = cosT(fl(L.spq) * (1.f / 64.f));
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    const Vec2 rim = V(C.x + fl(sgn) * kRingRx * 0.8f * sp, C.y - kRingRy * 0.35f);
    cap(pl, E, rim, 1.f, TENT_LIT);
  }
  ring(pl, C, kRingRx, kRingRy, th * 0.35f, ACTOR);
}

void partNet(const Look& L, PL& pl) {
  if (!(L.items & ROPES_NET) || L.nd == 0) return;
  const float top = netTop(L);
  const float len = kNetLen * fl(L.nd) * (1.f / 32.f);
  const float yEnd = top + minf(len, kNetLen);
  const float x0 = fl(L.ox) + kRopesNetX0;
  for (int i = 0; i < kNetCols; ++i) {
    const float x = x0 + kNetCell * fl(i);
    cap(pl, V(x + netShear(L, top), top), V(x + netShear(L, yEnd), yEnd), 1.1f, TENT_LIT);
  }
  const float x5 = x0 + kNetCell * fl(kNetCols - 1);
  for (int j = 0; j < kNetRows; ++j) {
    const float y = netRowY(L, j);
    if (y - top > len) break;
    cap(pl, V(x0 + netShear(L, y), y), V(x5 + netShear(L, y), y), 1.1f, TENT_LIT);
  }
}

void buildPart(const Look& L, int part, PL& pl) {
  pl.n = 0;
  if (!L.items) return;
  switch (part) {
    case PT_POST0: case PT_POST0 + 1: case PT_POST0 + 2: case PT_POST0 + 3: partPost(L, part, pl); break;
    case PT_BR_FIX: partBridgeFix(L, pl); break;
    case PT_BR_DECK: partBridgeDeck(L, pl); break;
    case PT_BEAMS: partBeams(L, pl); break;
    case PT_SWING: partSwing(L, pl); break;
    default: partNet(L, pl); break;
  }
}

Rect primRect(const Prim& p) {
  float x0, y0, x1, y1;
  if (p.kind == PK_CAP) {
    const float pad = 0.5f * p.w + 1.f;
    x0 = minf(p.a.x, p.b.x) - pad;
    x1 = maxf(p.a.x, p.b.x) + pad;
    y0 = minf(p.a.y, p.b.y) - pad;
    y1 = maxf(p.a.y, p.b.y) + pad;
  } else {
    const float rr = maxf(p.b.x, p.b.y) + 2.f;
    x0 = p.a.x - rr;
    x1 = p.a.x + rr;
    y0 = p.a.y - rr;
    y1 = p.a.y + rr;
  }
  return Rect{(int)floorf(x0) - 1, (int)floorf(y0) - 1, (int)ceilf(x1) + 1, (int)ceilf(y1) + 1};
}

bool primEq(const Prim& a, const Prim& b) {
  return a.kind == b.kind && a.slot == b.slot && a.w == b.w && a.a.x == b.a.x && a.a.y == b.a.y && a.b.x == b.b.x &&
         a.b.y == b.b.y && a.tilt == b.tilt;
}

Rect partRect(const PL& pl) {
  Rect r = kEmpty;
  for (int i = 0; i < pl.n; ++i) r = unite(r, primRect(pl.v[i]));
  return r;
}

void drawRing(Surface& s, const Prim& p, Pixel col, const Rect& clip) {
  Vec2 pts[50];
  Vec2 outer[24], inner[24];
  const float ct = cosT(p.tilt * kInvTwoPi), st = sinT(p.tilt * kInvTwoPi);
  for (int i = 0; i < 24; ++i) {
    const float a = fl(i) * (1.f / 24.f);
    const float ux = cosT(a), uy = sinT(a);
    const float ox = p.b.x * ux, oy = p.b.y * uy, ix = ox * kRingIn, iy = oy * kRingIn;
    outer[i] = V(p.a.x + ox * ct - oy * st, p.a.y + ox * st + oy * ct);
    inner[i] = V(p.a.x + ix * ct - iy * st, p.a.y + ix * st + iy * ct);
  }
  // One path: the outer loop, a cut in to the inner loop, the inner loop the other way round, the cut back.
  int n = 0;
  for (int i = 0; i < 24; ++i) pts[n++] = outer[i];
  pts[n++] = outer[0];
  pts[n++] = inner[0];
  for (int i = 23; i >= 1; --i) pts[n++] = inner[i];
  pts[n++] = inner[0];
  fillPolygon(s, pts, n, col, clip);
}

// Back to front.
constexpr uint8_t kDrawOrder[PT_COUNT] = {PT_BR_FIX, PT_BR_DECK, PT_NET, PT_POST0, PT_POST0 + 1, PT_POST0 + 2,
                                          PT_POST0 + 3, PT_BEAMS, PT_SWING};

// ---- the hiker rig's kinematics (a copy of hiker.cpp's; keep in step) ---------------------------------------

constexpr float kThigh = 0.235f, kShin = 0.235f, kUpper = 0.16f, kFore = 0.15f;
constexpr float kTorsoTop = 0.235f, kShoulder = 0.25f, kNeck = 0.29f;
constexpr float kHeadUp = 0.06f, kHeadFwd = 0.012f, kHeadR = 0.062f;
constexpr float kTorsoW = 0.15f, kShinW = 0.075f, kFootW = 0.058f;
constexpr float kHeel = 0.02f, kToe = 0.07f, kFootFollow = 0.75f, kBreathAmp = 0.011f;

struct Kin {
  Vec2 knee[2], ankle[2], heel[2], toe[2], elbow[2], hand[2];
  Vec2 shoulder, head;
  float low;   // the lowest contact below the hips
};

inline Vec2 dirA(float a) { return V(sinT(a * kInvTwoPi), cosT(a * kInvTwoPi)); }   // from straight down, + forward

// Joint positions relative to the hips, local frame (x forward, y down).
void kin(const Hiker& h, const HikerPose& P, Kin& k) {
  const float H = h.height;
  const float breath = (1.f - h.walk_w) * kBreathAmp * H * h.breath_scale * (0.5f + 0.5f * sinT(h.breath_phase));
  const float lt = P.lean * kInvTwoPi;
  const Vec2 up = V(sinT(lt), -cosT(lt));
  const Vec2 chest = V(up.x * (kTorsoTop * H + breath), up.y * (kTorsoTop * H + breath));
  k.shoulder = V(up.x * (kShoulder * H + breath), up.y * (kShoulder * H + breath));
  const Vec2 neck = V(up.x * (kNeck * H + breath), up.y * (kNeck * H + breath));
  const float ht = (P.lean + P.head) * kInvTwoPi;
  const Vec2 hu = V(sinT(ht), -cosT(ht)), hf = V(cosT(ht), sinT(ht));
  k.head = V(neck.x + hu.x * kHeadUp * H + hf.x * kHeadFwd * H, neck.y + hu.y * kHeadUp * H + hf.y * kHeadFwd * H);
  float low = maxf(0.f, chest.y) + 0.5f * kTorsoW * H;
  for (int i = 0; i < 2; ++i) {
    const Vec2 d0 = dirA(P.thigh[i]);
    k.knee[i] = V(d0.x * kThigh * H, d0.y * kThigh * H);
    const float shin = P.thigh[i] - P.knee[i];
    const Vec2 d1 = dirA(shin);
    k.ankle[i] = V(k.knee[i].x + d1.x * kShin * H, k.knee[i].y + d1.y * kShin * H);
    const Vec2 fd = dirA(0.5f * kPi + kFootFollow * shin);
    k.heel[i] = V(k.ankle[i].x - fd.x * kHeel * H, k.ankle[i].y - fd.y * kHeel * H);
    k.toe[i] = V(k.ankle[i].x + fd.x * kToe * H, k.ankle[i].y + fd.y * kToe * H);
    const Vec2 d2 = dirA(P.shoulder[i]);
    k.elbow[i] = V(k.shoulder.x + d2.x * kUpper * H, k.shoulder.y + d2.y * kUpper * H);
    const Vec2 d3 = dirA(P.shoulder[i] + P.elbow[i]);
    k.hand[i] = V(k.elbow[i].x + d3.x * kFore * H, k.elbow[i].y + d3.y * kFore * H);
    low = maxf(low, k.knee[i].y + 0.5f * kShinW * H);
    low = maxf(low, maxf(k.heel[i].y, maxf(k.toe[i].y, k.ankle[i].y)) + 0.5f * kFootW * H);
  }
  k.low = maxf(low, k.head.y + kHeadR * H);
}

inline float facingSign(const Hiker& h) { return h.facing < 0 ? -1.f : 1.f; }

// The hips in frame space for the pose `P` (the rig's own placement).
Vec2 hipsOf(const Hiker& h, const HikerPose& P, const Kin& k) {
  return V(h.feet.x + facingSign(h) * P.dx * h.height, h.feet.y - k.low - P.lift * h.height);
}
Vec2 toFrame(const Hiker& h, Vec2 hips, Vec2 rel) { return V(hips.x + facingSign(h) * rel.x, hips.y + rel.y); }

// Put the hips at frame height hy, and (placeHipsX) at frame x hx.
void placeHips(Hiker& h, const Kin& k, float hy) { h.pose.lift = (h.feet.y - k.low - hy) / h.height; }
void placeHipsX(Hiker& h, float hx) { h.feet.x = hx - facingSign(h) * h.pose.dx * h.height; }

// ---- the overlay -----------------------------------------------------------------------------------------

struct Ov {
  HikerPose p;
  float wt, wl, wa;   // weights of the trunk (lean, head), legs and arms
};

void applyOv(HikerPose& d, const Ov& o) {
  d.lean = lerp(d.lean, o.p.lean, o.wt);
  d.head = lerp(d.head, o.p.head, o.wt);
  for (int i = 0; i < 2; ++i) {
    d.thigh[i] = lerp(d.thigh[i], o.p.thigh[i], o.wl);
    d.knee[i] = lerp(d.knee[i], o.p.knee[i], o.wl);
    d.shoulder[i] = lerp(d.shoulder[i], o.p.shoulder[i], o.wa);
    d.elbow[i] = lerp(d.elbow[i], o.p.elbow[i], o.wa);
  }
}

// The hand's drop below the shoulder for an upper arm at angle `a` and an elbow bend `e`.
inline float handDrop(float a, float e) {
  return kUpper * 90.f * cosT(a * kInvTwoPi) + kFore * 90.f * cosT((a + e) * kInvTwoPi);
}

// The upper-arm angle that puts the hand `dy` below the shoulder (bisection; the drop falls as the arm rises).
float armForDrop(float dy, float e, float scale) {
  float lo = 0.1f, hi = 2.3f;
  for (int i = 0; i < 9; ++i) {
    const float mid = 0.5f * (lo + hi);
    if (handDrop(mid, e) * scale > dy) lo = mid;
    else hi = mid;
  }
  return 0.5f * (lo + hi);
}

}  // namespace

// ---- the state -------------------------------------------------------------------------------------------------

namespace {

constexpr float kStroll = 34.f;
constexpr float kRampV = 20.f;
constexpr float kBridgeV = 13.f;
constexpr float kStepT = 0.78f;    // one climbing step
constexpr float kTearRate = 1.5f;  // the tear-down plays the build clock backwards this much faster

int climbSteps(const RopesState& r) { return 14 - (4 + (r.k[4] & 1)); }   // 10 or 9 rungs up; the foot is caught on the last

// A course of two items has more time for each (he takes his time): the variants stay within 60..90 s.
float segDur(const RopesState& r, int id) {
  const float two = popcount3(r.items) == 2 ? 1.f : 0.f;
  switch (id) {
    case SG_B_LOOK: return 1.6f + 1.4f * (float)r.k[1] * (1.f / 255.f);
    case SG_B_BRACE: return 2.2f;
    case SG_B_DOWN: return 1.1f;
    case SG_B_PROUD: return 1.8f + 1.2f * two;
    case SG_S_LOOK: return 1.5f + 0.7f * two;
    case SG_S_BOARD: return 1.5f;
    case SG_S_PUMP: return 6.f + 1.5f * two;
    case SG_S_SPIN: return 4.4f;
    case SG_S_SLOW: return 3.2f + 0.8f * two;
    case SG_S_OFF: return 1.1f;
    case SG_S_DIZZY: return 5.4f + 1.f * two;
    case SG_S_RECOVER: return 2.2f;
    case SG_N_LOOK: return 1.8f;
    case SG_N_CLIMB: return 0.5f + (float)climbSteps(r) * kStepT;
    case SG_N_CATCH: return 1.6f;
    case SG_N_FLIP: return 1.1f;
    case SG_N_HANG: return 3.2f + 1.6f * (float)r.k[5] * (1.f / 255.f);
    case SG_N_DROP: return 0.8f;
    case SG_N_LAND: return 1.8f;
    case SG_N_SITUP: return 3.f;
    case SG_N_STAND: return 2.2f;
    case SG_HOLD: return 1.4f + 2.f * two;
    default: return 0.f;   // the walks end on arrival; the build and the tear-down on the clock
  }
}

bool isWalkSeg(int id) {
  return id == SG_B_WALK || id == SG_B_UP || id == SG_B_CROSS1 || id == SG_B_CROSS2 || id == SG_S_WALK || id == SG_N_WALK;
}

void addSeg(RopesState& r, int id) {
  if (r.n_seg < ROPES_PLAN_MAX) r.plan[r.n_seg++] = (uint8_t)id;
}

}  // namespace

const char* ropesSegName(int seg) {
  static const char* const kNames[SG_COUNT] = {
      "build",   "b_walk",  "b_look",  "b_up",    "b_cross1", "b_brace", "b_cross2", "b_down",   "b_proud",   "s_walk",
      "s_look",  "s_board", "s_pump",  "s_spin",  "s_slow",   "s_off",   "s_dizzy",  "s_recover", "n_walk",   "n_look",
      "n_climb", "n_catch", "n_flip",  "n_hang",  "n_drop",   "n_land",  "n_situp",  "n_stand",   "hold",     "tear"};
  return seg >= 0 && seg < SG_COUNT ? kNames[seg] : "?";
}

void ropesInit(RopesState& r, float x0, float ground_y) {
  memset(&r, 0, sizeof r);
  r.x0 = finite(x0) ? x0 : 0.f;
  r.gy = finite(ground_y) ? ground_y : 330.f;
  r.phase = ROPES_IDLE;
}

void ropesStart(RopesState& r, int variant, Rng& rng) {
  const float x0 = r.x0, gy = r.gy;
  memset(&r, 0, sizeof r);
  r.x0 = x0;
  r.gy = gy;
  const uint32_t a = rng.next();
  const uint32_t b = rng.next();
  r.k[0] = (uint8_t)(a & 255u);
  r.k[1] = (uint8_t)((a >> 8) & 255u);
  r.k[2] = (uint8_t)((a >> 16) & 255u);
  r.k[3] = (uint8_t)((a >> 24) & 255u);
  r.k[4] = (uint8_t)(b & 255u);
  r.k[5] = (uint8_t)((b >> 8) & 255u);
  r.k[6] = (uint8_t)((b >> 16) & 255u);
  r.k[7] = (uint8_t)((b >> 24) & 255u);
  if (variant < 0 || variant >= ROPES_VARIANTS) variant = ROPES_A;
  r.variant = (uint8_t)variant;
  r.items = variant == ROPES_A ? (uint8_t)(ROPES_BRIDGE | ROPES_SWING | ROPES_NET)
                               : (variant == ROPES_B ? (uint8_t)(ROPES_BRIDGE | ROPES_SWING) : (uint8_t)(ROPES_SWING | ROPES_NET));
  r.tmax = 0.38f + 0.04f * (float)r.k[3] * (1.f / 255.f);
  r.phase = ROPES_RUN;
  addSeg(r, SG_BUILD);
  if (r.items & ROPES_BRIDGE) for (int id = SG_B_WALK; id <= SG_B_PROUD; ++id) addSeg(r, id);
  if (r.items & ROPES_SWING) for (int id = SG_S_WALK; id <= SG_S_RECOVER; ++id) addSeg(r, id);
  if (r.items & ROPES_NET) for (int id = SG_N_WALK; id <= SG_N_STAND; ++id) addSeg(r, id);
  addSeg(r, SG_HOLD);
  addSeg(r, SG_TEAR);
}

bool ropesDone(const RopesState& r) { return r.phase == ROPES_DONE; }

bool ropesHikerBusy(const RopesState& r) {
  if (r.phase != ROPES_RUN || r.seg >= r.n_seg) return false;
  const int id = r.plan[r.seg];
  return id != SG_BUILD && id != SG_TEAR;
}

int ropesSegment(const RopesState& r) { return r.phase == ROPES_RUN && r.seg < r.n_seg ? (int)r.plan[r.seg] : -1; }

float ropesFixedSeconds(const RopesState& r) {
  float t = buildSeconds(r.items) * (1.f + 1.f / kTearRate);
  for (int i = 0; i < r.n_seg; ++i) t += segDur(r, r.plan[i]);
  return t;
}

float ropesRotation(const RopesState& r) { return r.rot; }

void ropesRotatePose(HikerPose& p, float rot) {
  p.lean += rot;
  p.thigh[0] += rot;
  p.thigh[1] += rot;
  p.shoulder[0] += rot;
  p.shoulder[1] += rot;
}

void ropesRigJoints(const Hiker& h, Vec2* hips, Vec2* head, Vec2* hand_near, Vec2* hand_far, Vec2* ankle_near) {
  Kin k;
  kin(h, h.pose, k);
  const Vec2 hp = hipsOf(h, h.pose, k);
  if (hips) *hips = hp;
  if (head) *head = toFrame(h, hp, k.head);
  if (hand_near) *hand_near = toFrame(h, hp, k.hand[0]);
  if (hand_far) *hand_far = toFrame(h, hp, k.hand[1]);
  if (ankle_near) *ankle_near = toFrame(h, hp, k.ankle[0]);
}

float ropesFloorY(const RopesState& r, float x) {
  Look L;
  lookOf(r, L);
  return floorOfL(L, x);
}

Vec2 ropesTyre(const RopesState& r, Vec2* rope_end) {
  Look L;
  lookOf(r, L);
  Vec2 E, C;
  tyreOf(L, E, C);
  if (rope_end) *rope_end = E;
  return C;
}

Vec2 ropesNetNode(const RopesState& r, int col, int row) {
  Look L;
  lookOf(r, L);
  col = col < 0 ? 0 : (col >= kNetCols ? kNetCols - 1 : col);
  row = row < 0 ? 0 : (row >= kNetRows ? kNetRows - 1 : row);
  const float y = netRowY(L, row);
  return V(fl(L.ox) + kRopesNetX0 + kNetCell * fl(col) + netShear(L, y), y);
}

// ---- bounds, dirty, draw ---------------------------------------------------------------------------------------------

Rect ropesBounds(const RopesState& r) {
  Look L;
  lookOf(r, L);
  Rect out = kEmpty;
  PL pl;
  for (int p = 0; p < PT_COUNT; ++p) {
    buildPart(L, p, pl);
    out = unite(out, partRect(pl));
  }
  return out;
}

bool ropesSame(const RopesState& a, const RopesState& b) {
  Rect none[ROPES_DIRTY_MAX];
  return ropesDirty(a, b, none) == 0;
}

int ropesDirty(const RopesState& before, const RopesState& after, Rect out[ROPES_DIRTY_MAX]) {
  Look la, lb;
  lookOf(before, la);
  lookOf(after, lb);
  if (memcmp(&la, &lb, sizeof la) == 0) return 0;
  int n = 0;
  PL pa, pb;
  for (int p = 0; p < PT_COUNT; ++p) {
    buildPart(la, p, pa);
    buildPart(lb, p, pb);
    Rect r = kEmpty;
    const int common = pa.n < pb.n ? pa.n : pb.n;
    for (int i = 0; i < common; ++i)
      if (!primEq(pa.v[i], pb.v[i])) r = unite(r, unite(primRect(pa.v[i]), primRect(pb.v[i])));
    for (int i = common; i < pa.n; ++i) r = unite(r, primRect(pa.v[i]));
    for (int i = common; i < pb.n; ++i) r = unite(r, primRect(pb.v[i]));
    if (!r.empty()) out[n++] = r;
  }
  return n;
}

void ropesDraw(const RopesState& r, Surface& strip, const Rect& clip, const Palette& pal) {
  Look L;
  lookOf(r, L);
  if (!L.items) return;
  PL pl;
  for (int o = 0; o < PT_COUNT; ++o) {
    buildPart(L, kDrawOrder[o], pl);
    for (int i = 0; i < pl.n; ++i) {
      const Prim& p = pl.v[i];
      if (!overlaps(primRect(p), clip)) continue;
      const Pixel c = pal.c[p.slot];
      if (p.kind == PK_CAP) strokeCapsule(strip, p.a, p.b, p.w, c, clip);
      else drawRing(strip, p, c, clip);
    }
  }
}

// ---- the step ----------------------------------------------------------------------------------------------------------------

namespace {

// The bridge: the walkway carries him while he is on it, bounces when he steps or braces.
void stepBridge(RopesState& r, const Hiker& h, int id, float dt) {
  if (!(r.items & ROPES_BRIDGE)) return;
  const float rx = h.feet.x - r.x0;
  const bool on = id == SG_B_CROSS1 || id == SG_B_BRACE || id == SG_B_CROSS2 || (id == SG_B_UP && rx >= kBrA + 2.f);
  r.bx = h.feet.x;
  const float want = on ? 1.f : 0.f;
  r.bw += (want - r.bw) * (dt / (0.25f + dt));
  r.dep += (7.f * r.bw - r.dep) * (dt / (0.3f + dt));
  // A footfall (each half stride of the rig's walk) kicks the span.
  if (h.walking && on) {
    const float idx = floorf(h.walk_phase * 2.f);
    if (idx != r.bidx) r.bnv += 15.f;
    r.bidx = idx;
  }
  const float w0 = kTwoPi * 1.25f;
  for (int i = 0; i < 2; ++i) {
    const float hh = dt * 0.5f;
    r.bnv += (-w0 * w0 * r.bnc - 1.6f * r.bnv) * hh;
    r.bnc += r.bnv * hh;
  }
  r.bnc = clampf(r.bnc, -6.f, 6.f);
  if (fabsf(r.bnc) < 0.02f && fabsf(r.bnv) < 0.2f) r.bnc = r.bnv = 0.f;
  r.bph += dt * 0.9f;
  r.bph -= floorf(r.bph);
}

// The tyre's pendulum, pumped when he is on it.
void stepSwing(RopesState& r, int id, float dt) {
  if (!(r.items & ROPES_SWING)) return;
  float pump_to = 0.f;
  if (id == SG_S_PUMP) pump_to = clamp01(r.st * (1.f / 1.5f));
  else if (id == SG_S_SPIN) pump_to = 0.7f;
  r.pump += (pump_to - r.pump) * (dt / (0.4f + dt));
  const float w0 = 2.35f;   // rad/s: a 2.7 s swing
  const float damp = id == SG_S_SLOW ? 1.1f : (id == SG_S_OFF ? 0.8f : 0.5f);   // he drags his feet to slow it
  for (int i = 0; i < 2; ++i) {
    const float hh = dt * 0.5f;
    const float x = r.th / (0.64f * r.tmax);   // the limit cycle of this self-limiting drive settles at about 1.55 times its scale: tmax
    const float acc = -w0 * w0 * sinT(r.th * kInvTwoPi) - damp * r.om + 1.3f * r.pump * r.om * (1.f - x * x);
    r.om += acc * hh;
    r.th += r.om * hh;
  }
  r.th = clampf(r.th, -0.9f, 0.9f);
  r.om = clampf(r.om, -3.f, 3.f);
  if (r.pump < 0.01f && fabsf(r.th) < 0.004f && fabsf(r.om) < 0.03f) r.th = r.om = 0.f;   // at rest: draws and marks nothing
  if (id == SG_S_SPIN) r.spin = (float)(2 + (r.k[2] & 1)) * easeInOut(window(r.st, 0.f, 4.2f));
}

void stepNet(RopesState& r, int id, float dt) {
  if (!(r.items & ROPES_NET)) return;
  if (id == SG_N_CLIMB) {
    const float t = r.st - 0.5f, tp = t - dt;
    if (t > 0.f && (int)floorf(t / kStepT) != (int)floorf(tp / kStepT)) r.nsv += 4.f;   // each step jolts the net
  }
  const float w0 = kTwoPi * 0.45f;
  for (int i = 0; i < 2; ++i) {
    const float hh = dt * 0.5f;
    r.nsv += (-w0 * w0 * r.nsw - 1.0f * r.nsv) * hh;
    r.nsw += r.nsv * hh;
  }
  r.nsw = clampf(r.nsw, -14.f, 14.f);
  if (fabsf(r.nsw) < 0.03f && fabsf(r.nsv) < 0.3f) r.nsw = r.nsv = 0.f;
}

// The rotation of the rig about its hips for the segment (information; ropesPose builds the pose).
float rotFor(const RopesState& r, int id) {
  const float u = clamp01(r.st / (segDur(r, id) > 0.f ? segDur(r, id) : 1.f));
  switch (id) {
    case SG_N_FLIP: return kPi * easeOutBack(u);
    case SG_N_HANG: return kPi + 0.22f * (1.f - u) * (1.f - u) * sinT(r.st * 0.55f);
    case SG_N_DROP: return kPi + 0.5f * kPi * easeInOut(window(u, 0.05f, 0.75f));
    default: return 0.f;
  }
}

void enterSeg(RopesState& r, Hiker& h, int id) {
  const float ox = r.x0;
  Kin k;
  kin(h, h.pose, k);
  const Vec2 hp = hipsOf(h, h.pose, k);
  r.ax = hp.x;
  r.ay = hp.y;
  switch (id) {
    case SG_B_WALK: hikerWalkTo(h, ox - 8.f, kStroll); break;
    case SG_B_LOOK: h.facing = 1; hikerSetPose(h, POSE_HANDS_ON_HIPS, 0.6f); break;
    case SG_B_UP: hikerWalkTo(h, ox + kBrA + 12.f, kRampV); break;
    case SG_B_CROSS1: hikerWalkTo(h, ox + 70.f, kBridgeV); break;
    case SG_B_BRACE: hikerSetPose(h, POSE_STAND, 0.25f); r.bnv += 55.f; break;
    case SG_B_CROSS2: hikerWalkTo(h, ox + 126.f, kBridgeV); break;
    case SG_B_DOWN: h.facing = 1; hikerSetPose(h, POSE_STAND, 0.6f); break;
    case SG_B_PROUD: h.facing = 1; hikerSetPose(h, POSE_HANDS_ON_HIPS, 0.5f); break;
    case SG_S_WALK: hikerWalkTo(h, ox + kRopesSwingX - 30.f, kStroll); break;
    case SG_S_LOOK: h.facing = 1; hikerSetPose(h, POSE_LOOK_AROUND, 0.7f); break;
    case SG_S_BOARD: h.facing = 1; hikerSetPose(h, POSE_PERCH, 1.3f); break;
    case SG_S_PUMP: r.om = 0.45f; break;
    case SG_S_OFF: hikerSetPose(h, POSE_STAND, 0.9f); break;
    case SG_S_DIZZY: hikerWalkTo(h, h.feet.x + ((r.k[6] & 1) ? -40.f : 40.f), 15.f); break;
    case SG_S_RECOVER: hikerSetPose(h, POSE_SCRATCH_HEAD, 0.5f); break;
    case SG_N_WALK: hikerWalkTo(h, ox + kRopesNetX, kStroll); break;
    case SG_N_LOOK: h.facing = 1; hikerSetPose(h, POSE_LOOK_AROUND, 0.8f); break;
    case SG_N_CLIMB: h.facing = 1; hikerSetPose(h, POSE_CLIMB, 0.3f); break;
    case SG_N_FLIP: r.nsv += 9.f; break;
    case SG_N_LAND: hikerSetPose(h, POSE_LIE, 0.f); break;
    case SG_N_SITUP: hikerSetPose(h, POSE_SIT, 1.3f); break;
    case SG_N_STAND: hikerSetPose(h, POSE_STAND, 1.2f); break;
    case SG_HOLD: h.facing = 1; hikerSetPose(h, POSE_STAND, 0.8f); break;
    default: break;
  }
}

// Per-tick commands of the segment, before the rig ticks.
void tickSeg(RopesState& r, Hiker& h, int id, float dt) {
  if (id == SG_S_SPIN) h.facing = cosT(r.spin) >= 0.f ? 1 : -1;   // twisting round: a profile, then the other profile
  if (id == SG_S_PUMP || id == SG_S_SLOW || id == SG_S_BOARD) h.facing = 1;
  if (id == SG_S_DIZZY) {   // he weaves: a new direction every 1.15 s, never arriving
    const int n = (int)floorf(r.st / 1.15f), np = (int)floorf((r.st - dt) / 1.15f);
    if (n != np || r.st <= dt) {
      const float dir = ((n + (r.k[6] & 1)) & 1) ? -1.f : 1.f;
      hikerWalkTo(h, h.feet.x + dir * 40.f, 15.f);
    }
  }
}

bool segDone(const RopesState& r, const Hiker& h, int id, float build_end) {
  if (id == SG_BUILD) return r.bt >= build_end;
  if (id == SG_TEAR) return r.bt <= 0.f;
  if (isWalkSeg(id)) return r.st > 0.1f && hikerArrived(h);
  return r.st >= segDur(r, id);
}

}  // namespace

void ropesStep(RopesState& r, Hiker& h, float dt) {
  if (r.phase != ROPES_RUN || r.seg >= r.n_seg) return;
  if (!finite(dt) || dt < 0.f) dt = 0.f;
  if (dt > 0.1f) dt = 0.1f;
  r.t += dt;
  r.st += dt;
  const float build_end = buildSeconds(r.items);
  int id = r.plan[r.seg];
  if (!r.entered) {
    if (id != SG_BUILD && id != SG_TEAR) enterSeg(r, h, id);
    r.entered = 1;
  }

  // The build clock.
  if (id == SG_BUILD) r.bt = minf(build_end, r.bt + dt);
  else if (id == SG_TEAR) r.bt = maxf(0.f, r.bt - dt * kTearRate);
  else r.bt = build_end;

  const bool busy = id != SG_BUILD && id != SG_TEAR;
  if (busy) {   // he settles onto the course's ground line (a glide, not a jump)
    const float dy = r.gy - h.feet.y;
    h.feet.y += clampf(dy, -30.f * dt, 30.f * dt);
    h.visible = true;
  }
  stepBridge(r, h, id, dt);
  stepSwing(r, id, dt);
  stepNet(r, id, dt);
  if (busy) tickSeg(r, h, id, dt);
  r.rot = rotFor(r, id);

  if (segDone(r, h, id, build_end)) {
    ++r.seg;
    r.st = 0.f;
    r.entered = 0;
    if (r.seg >= r.n_seg) {
      r.phase = ROPES_DONE;
      r.bt = 0.f;
      r.rot = 0.f;
      return;
    }
    id = r.plan[r.seg];
    if (id != SG_BUILD && id != SG_TEAR) enterSeg(r, h, id);
    r.entered = 1;
    r.rot = rotFor(r, id);
  }
}

// ---- the overlay ---------------------------------------------------------------------------------------------------------------

namespace {

// The hips' height at which no foot point is below the floor.
float hipsOnFloor(const Look& L, const Hiker& h, const Kin& k, float hipx) {
  const float sx = facingSign(h), H = h.height, r = 0.5f * kFootW * H;
  float hy = 1.0e9f;
  for (int i = 0; i < 2; ++i) {
    const Vec2 pts[3] = {k.heel[i], V(0.5f * (k.heel[i].x + k.toe[i].x), 0.5f * (k.heel[i].y + k.toe[i].y)), k.toe[i]};
    for (int j = 0; j < 3; ++j) {
      // The sole's round end is a circle of radius r: it must clear the floor over its whole width (the floor may slope).
      for (int m = -2; m <= 2; ++m) {
        const float dx = r * 0.5f * (float)m;
        const float rise = sqrtf(maxf(0.f, r * r - dx * dx));
        hy = minf(hy, floorOfL(L, hipx + sx * pts[j].x + dx) - rise - pts[j].y);
      }
    }
  }
  return hy;
}

// The hips never go below where the rig would ground them on the ground line.
float aboveGround(const Hiker& h, const Kin& k, float hy) { return minf(hy, h.feet.y - k.low); }

// Both hands on the hand ropes: the near arm on the near rope, the far arm on the far one.
void armsOnRopes(const Look& L, Hiker& h, const Kin& k, Vec2 hips) {
  const float sx = facingSign(h), H = h.height, sc = H / 90.f;
  const float el[2] = {0.45f, 0.6f};
  for (int i = 0; i < 2; ++i) {
    const Vec2 sh = toFrame(h, hips, k.shoulder);
    float a = 1.2f;
    for (int it = 0; it < 2; ++it) {   // the rope's height where the hand will be
      const float hx = sh.x + sx * (kUpper * H * sinT(a * kInvTwoPi) + kFore * H * sinT((a + el[i]) * kInvTwoPi));
      a = armForDrop(handYL(L, hx, i == 1) - sh.y, el[i], sc);
    }
    h.pose.shoulder[i] = a;
    h.pose.elbow[i] = el[i];
  }
}

void poseBridge(const RopesState& r, const Look& L, Hiker& h, int id) {
  HikerPose& P = h.pose;
  if (id == SG_B_UP) hikerApplyClimb(h, 0.55f);
  const bool on = id == SG_B_CROSS1 || id == SG_B_BRACE || id == SG_B_CROSS2;
  const float ws = 0.7f + 0.6f * (float)r.k[0] * (1.f / 255.f);
  if (on) {
    const float prog = clamp01((h.feet.x - r.x0 - kBrA) / (kBrB - kBrA));
    const float amp = (0.05f + 0.05f * ws) * (0.6f + 0.4f * sinT(0.5f * prog));
    P.lean += amp * sinT(r.bph);
    P.head += 0.07f * sinT(r.bph * 1.7f + 0.3f);
  }
  if (id == SG_B_BRACE) {   // knees bent, feet apart, leaning in
    const float u = clamp01(r.st * (1.f / 2.2f));
    const float e = sinT(0.5f * clamp01(u * 1.3f)) * (1.f - easeInOut(window(u, 0.8f, 1.f)));
    Ov o;
    o.p = P;
    o.p.lean = 0.16f;
    o.p.thigh[0] = 0.5f;
    o.p.thigh[1] = -0.4f;
    o.p.knee[0] = 0.95f;
    o.p.knee[1] = 0.85f;
    o.wt = 0.8f * e;
    o.wl = e;
    o.wa = 0.f;
    applyOv(P, o);
  }
  Kin k;
  kin(h, P, k);
  const Vec2 hp0 = hipsOf(h, P, k);
  const float hy = hipsOnFloor(L, h, k, hp0.x);
  placeHips(h, k, hy);
  if (on) armsOnRopes(L, h, k, V(hp0.x, hy));
}


// The hops of the bridge's end and the swing: the hips fly a short arc from (ax, ay) to (bx, by) as w goes 0..1.
Vec2 hopPath(float ax, float ay, float bx, float by, float w, float arc) {
  const float e = easeInOut(clamp01(w));
  return V(lerp(ax, bx, e), lerp(ay, by, e) - arc * sinT(0.5f * clamp01(w)));
}

// Hips where the rig would put them for pose `p` standing on the course's ground line.
float hipsStandingY(const Hiker& h, PoseId p) {
  Kin k;
  kin(h, hikerPoseDef(p), k);
  return h.feet.y - k.low;
}

// Put the hips at `want`, blending in from where the rig has them by `win` 0..1.
void pinHips(Hiker& h, const Kin& k, Vec2 want, float win) {
  const Vec2 now = hipsOf(h, h.pose, k);
  placeHips(h, k, lerp(now.y, want.y, win));
  placeHipsX(h, lerp(now.x, want.x, win));
}

// Put leg `leg`'s ankle on `node`, blending in by `win`.
void pinAnkle(Hiker& h, const Kin& k, int leg, Vec2 node, float win) {
  const float sx = facingSign(h);
  pinHips(h, k, V(node.x - sx * k.ankle[leg].x, node.y - k.ankle[leg].y), win);
}

void poseBridgeDown(const RopesState& r, const Look& L, Hiker& h) {
  (void)L;
  const float u = clamp01(r.st * (1.f / 1.1f));
  const float w = window(u, 0.1f, 0.9f);
  const Vec2 to = V(r.x0 + 142.f, hipsStandingY(h, POSE_STAND));
  const Vec2 hp = hopPath(r.ax, r.ay, to.x, to.y, w, 10.f);
  Ov o;
  o.p = h.pose;   // in the air he tucks his knees and throws his arms out
  o.p.thigh[0] = 0.8f;
  o.p.thigh[1] = 0.6f;
  o.p.knee[0] = 1.3f;
  o.p.knee[1] = 1.1f;
  o.p.shoulder[0] = 1.3f;
  o.p.shoulder[1] = -0.5f;
  o.p.elbow[0] = 0.3f;
  o.p.elbow[1] = 0.3f;
  o.wt = 0.f;
  o.wl = o.wa = sinT(0.5f * w);
  applyOv(h.pose, o);
  Kin k;
  kin(h, h.pose, k);
  placeHips(h, k, aboveGround(h, k, hp.y));
  placeHipsX(h, hp.x);
}

// ---- the swing -------------------------------------------------------------------------------------------------

Vec2 seatOf(const Look& L) {
  Vec2 E, C;
  tyreOf(L, E, C);
  return V(C.x - 3.f, C.y - 9.f);
}

// Sitting in the tyre: his pump (legs out and back on the swing) and the twist. The arms come after the hips (ropeArms).
void seatedOv(const RopesState& r, const HikerPose& base, float wt, float wl, Ov& o) {
  const float omn = clampf(r.om * (1.f / 0.9f), -1.f, 1.f);
  const float pa = 0.4f + 0.6f * clamp01(r.pump * 1.3f);
  const float a = fabsf(sinT(r.spin));   // 0 in profile .. 1 facing us
  const float stretch = 0.35f * omn * pa;
  o.p = base;
  float lean = -0.04f - 0.2f * omn * pa + r.th * 0.6f;
  const float head = 0.05f - 0.1f * omn;
  float t0 = 1.45f + stretch, t1 = 1.35f + 0.9f * stretch;
  float s0 = 0.1f + 1.2f * stretch, s1 = 0.3f + stretch;
  t0 = lerp(t0, 0.45f, a);
  t1 = lerp(t1, -0.45f, a);
  s0 = lerp(s0, 0.05f, a);
  s1 = lerp(s1, -0.05f, a);
  lean = lerp(lean, 0.f, a);
  o.p.lean = lean;
  o.p.head = head;
  o.p.thigh[0] = t0;
  o.p.thigh[1] = t1;
  o.p.knee[0] = t0 - s0;
  o.p.knee[1] = t1 - s1;
  o.wt = wt;
  o.wl = wl;
  o.wa = 0.f;
}

// The upper arm and elbow that put the hand at `T` (hip-local x forward, y down) from the shoulder (the elbow bends forward).
void solveArm(Vec2 T, float H, float& a, float& e) {
  const float l1 = kUpper * H, l2 = kFore * H;
  const float len = sqrtf(T.x * T.x + T.y * T.y);
  const float d = clampf(len, 6.f, l1 + l2 - 0.3f);
  const float c = clampf((d * d - l1 * l1 - l2 * l2) / (2.f * l1 * l2), -1.f, 1.f);
  float lo = 0.f, hi = 2.8f;
  for (int i = 0; i < 9; ++i) {
    const float mid = 0.5f * (lo + hi);
    if (cosT(mid * kInvTwoPi) > c) lo = mid;
    else hi = mid;
  }
  e = 0.5f * (lo + hi);
  const float tx = len > 0.001f ? T.x / len : 0.f, ty = len > 0.001f ? T.y / len : -1.f;
  lo = 1.2f;
  hi = 4.4f;
  for (int i = 0; i < 10; ++i) {   // sin(a - alpha) changes sign at the direction alpha of T
    const float mid = 0.5f * (lo + hi);
    if (sinT(mid * kInvTwoPi) * ty - cosT(mid * kInvTwoPi) * tx < 0.f) lo = mid;
    else hi = mid;
  }
  const float x = l2 * sinT(e * kInvTwoPi) / (l1 + l2 * cosT(e * kInvTwoPi));
  a = 0.5f * (lo + hi) - x / (1.f + 0.28f * x * x);
}

// Both hands on the tyre's rope, a little apart (or, twisting round with his back to us, in a V up it).
void ropeArms(const RopesState& r, const Look& L, Hiker& h, const Kin& k, Vec2 hips, float w) {
  if (w <= 0.f) return;
  const float sx = facingSign(h);
  Vec2 E, C;
  tyreOf(L, E, C);
  const Vec2 att = V(fl(L.ox) + kRopesSwingX, fl(L.gy) - kBeam1 + 2.f);
  float ux = att.x - E.x, uy = att.y - E.y;
  const float un = sqrtf(ux * ux + uy * uy);
  if (un < 1.f) return;
  ux /= un;
  uy /= un;
  const Vec2 S = toFrame(h, hips, k.shoulder);
  const float wx = S.x - C.x, wy = S.y - C.y;
  const float along = wx * ux + wy * uy;
  const float perp2 = wx * wx + wy * wy - along * along;
  const float a_spin = fabsf(sinT(r.spin));
  for (int i = 0; i < 2; ++i) {
    const float reach = 26.f - 1.5f * (float)i;
    const float s = along + sqrtf(maxf(0.f, reach * reach - perp2)) - 4.f * (float)i;
    const Vec2 Q = V(C.x + ux * s, C.y + uy * s);
    float ta, te;
    solveArm(V(sx * (Q.x - S.x), Q.y - S.y), h.height, ta, te);
    ta = lerp(ta, i == 0 ? 2.89f : 3.39f, a_spin);   // 3.39 is -2.89 a turn on: the mirror of the near arm
    te = lerp(te, i == 0 ? 0.1f : -0.1f, a_spin);
    h.pose.shoulder[i] = lerp(h.pose.shoulder[i], ta, w);
    h.pose.elbow[i] = lerp(h.pose.elbow[i], te, w);
  }
}

void poseSwing(const RopesState& r, const Look& L, Hiker& h, int id) {
  HikerPose& P = h.pose;
  const float dur = segDur(r, id);
  const float u = dur > 0.f ? clamp01(r.st / dur) : 0.f;
  const Vec2 seat = seatOf(L);
  Vec2 hips{0.f, 0.f};
  float arm_w = 0.f;
  bool pin = true;
  Ov o;
  switch (id) {
    case SG_S_BOARD: {
      const float w = window(u, 0.15f, 1.f);
      hips = hopPath(r.ax, r.ay, seat.x, seat.y, w, 14.f);
      seatedOv(r, P, easeInOut(window(u, 0.5f, 1.f)), easeInOut(window(u, 0.5f, 1.f)), o);
      applyOv(P, o);
      arm_w = easeInOut(window(u, 0.3f, 0.9f));
      break;
    }
    case SG_S_PUMP:
    case SG_S_SPIN:
    case SG_S_SLOW:
      hips = seat;
      seatedOv(r, P, 1.f, 1.f, o);
      applyOv(P, o);
      arm_w = 1.f;
      break;
    case SG_S_OFF: {
      const float w = window(u, 0.1f, 1.f);
      hips = hopPath(r.ax, r.ay, r.x0 + kRopesSwingX - 30.f, hipsStandingY(h, POSE_STAND), w, 16.f);
      seatedOv(r, P, 1.f - easeInOut(window(u, 0.f, 0.7f)), 1.f - easeInOut(window(u, 0.f, 0.7f)), o);
      applyOv(P, o);
      arm_w = 1.f - easeInOut(window(u, 0.f, 0.5f));
      break;
    }
    case SG_S_DIZZY: {   // swaying, head rolling, arms out for balance
      pin = false;
      const float w = easeInOut(window(r.st, 0.f, 0.5f)) * (1.f - easeInOut(window(r.st, dur - 1.3f, dur)));
      P.lean += w * 0.16f * sinT(r.st * 0.85f);
      P.head += w * 0.5f * sinT(r.st * 1.55f + 0.2f);
      o.p = P;
      o.p.shoulder[0] = 1.25f + 0.4f * sinT(r.st * 0.9f);
      o.p.shoulder[1] = -0.8f + 0.4f * sinT(r.st * 0.9f + 0.5f);
      o.p.elbow[0] = 0.4f;
      o.p.elbow[1] = 0.6f;
      o.wt = o.wl = 0.f;
      o.wa = 0.8f * w;
      applyOv(P, o);
      break;
    }
    case SG_S_RECOVER: {
      pin = false;
      const float w = 1.f - window(r.st, 0.f, 1.6f);
      P.head += w * 0.4f * sinT(r.st * 1.5f);
      break;
    }
    default: pin = false; break;
  }
  if (pin) {
    Kin k;
    kin(h, P, k);
    const float hy = aboveGround(h, k, hips.y);
    placeHips(h, k, hy);
    placeHipsX(h, hips.x);
    ropeArms(r, L, h, k, V(hips.x, hy), arm_w);
  }
}

// ---- the net -------------------------------------------------------------------------------------------------------

constexpr float kPbT = 0.95f, kPbK = 1.5f;   // a leg with the foot on the rung beside the hips: bent (hips 30 px over it)
constexpr float kPsT = 0.45f, kPsK = 0.5f;   // straightened (hips 40 px over it), 10 px more: one rung

// The rung a foot stands on: y of rung m (0 the ground, 1 the foot of the net, up 10 px each), the sole on it.
float rungY(const Look& L, int m) { return fl(L.gy) - 10.f * fl(m) - 2.6f; }
float netX(const Look& L, float y) { return fl(L.ox) + kRopesNetX + netShear(L, y); }

// Step j (1..K) of the climb at c 0..1: one foot planted, the other swinging up two rungs.
void climbPose(HikerPose& P, int j, float c, int K) {
  const int mover = (K - j) & 1, planted = 1 - mover;
  const float e = easeInOut(window(c, 0.08f, 0.92f)), arc = sinT(0.5f * e);
  P.thigh[planted] = lerp(kPbT, kPsT, e);
  P.knee[planted] = lerp(kPbK, kPsK, e);
  P.thigh[mover] = lerp(kPsT, kPbT, e) + 0.35f * arc;
  P.knee[mover] = lerp(kPsK, kPbK, e) + 0.5f * arc;
  P.lean = 0.22f + 0.03f * sinT(c);
  P.head = -0.35f;
  const int rise = j & 1;   // the arm going up this step
  P.shoulder[rise] = lerp(1.9f, 2.65f, e);
  P.elbow[rise] = lerp(1.1f, 0.25f, e);
  P.shoulder[1 - rise] = lerp(2.65f, 1.9f, e);
  P.elbow[1 - rise] = lerp(0.25f, 1.1f, e);
}

// The foot is caught (leg 0 on the last rung): the other leg gropes for the next, he strains, his grip goes.
void catchPose(HikerPose& P, float u) {
  P.thigh[0] = kPbT;
  P.knee[0] = kPbK;
  const float e1 = easeInOut(window(u, 0.f, 0.3f)), arc = sinT(0.5f * e1);
  const float f = window(u, 0.3f, 1.f);
  P.thigh[1] = lerp(kPsT, kPbT, e1) + 0.35f * arc + f * 0.4f * sinT(f * 3.f);
  P.knee[1] = lerp(kPsK, kPbK, e1) + 0.5f * arc + f * 0.4f * sinT(f * 3.f + 0.25f);
  P.lean = 0.22f + 0.18f * window(u, 0.f, 0.4f) * sinT(u * 2.2f);
  P.head = -0.35f;
  const float slip = easeInOut(window(u, 0.7f, 1.f));
  P.shoulder[0] = lerp(2.65f + 0.3f * sinT(u * 4.f), 1.5f, slip);
  P.elbow[0] = 0.3f;
  P.shoulder[1] = lerp(2.3f + 0.4f * sinT(u * 4.f + 1.f), 1.0f, slip);
  P.elbow[1] = 0.5f;
}

// Upside down, rotated `rot` about the hips from the caught pose `q`; `fu` 0..1 how far the flip has gone (the arms fall, the free leg kicks).
void hangPose(HikerPose& P, const HikerPose& q, float rot, float fu, float t, float sway) {
  P = q;
  P.lean = q.lean + rot;
  P.head = lerp(q.head, 0.1f, fu);
  P.thigh[0] = q.thigh[0] + rot;
  P.thigh[1] = q.thigh[1] + rot + 0.5f * fu + 0.2f * fu * sinT(t * 0.9f);
  P.knee[1] = lerp(q.knee[1], 1.0f, fu);
  const float g = easeInOut(window(fu, 0.15f, 0.85f));
  for (int i = 0; i < 2; ++i) {
    const float hang = 0.15f + 0.12f * (float)i + 0.3f * sway * sinT(t * 0.55f - 0.15f * (float)i);
    P.shoulder[i] = lerp(q.shoulder[i] + rot, hang, g);
    P.elbow[i] = lerp(q.elbow[i], 0.25f + 0.1f * (float)i, g);
  }
}

void poseNet(const RopesState& r, const Look& L, Hiker& h, int id) {
  HikerPose& P = h.pose;
  const int K = climbSteps(r);
  const float dur = segDur(r, id);
  const float u = dur > 0.f ? clamp01(r.st / dur) : 0.f;
  Kin k;
  switch (id) {
    case SG_N_CLIMB: {
      const float t = maxf(0.f, r.st - 0.5f);
      int j = (int)floorf(t / kStepT) + 1;
      if (j > K) j = K;
      const float c = clamp01((t - fl(j - 1) * kStepT) / kStepT);
      const float win = easeInOut(clamp01(r.st * 2.f));
      Ov o;
      o.p = P;
      climbPose(o.p, j, c, K);
      o.wt = o.wl = o.wa = win;
      applyOv(P, o);
      kin(h, P, k);
      const int planted = ((K - j) & 1) ^ 1;
      const float y = rungY(L, j - 1);
      pinAnkle(h, k, planted, V(netX(L, y), y), win);
      break;
    }
    case SG_N_CATCH:
    case SG_N_FLIP:
    case SG_N_HANG: {
      HikerPose q = P;
      climbPose(q, K, 1.f, K);
      catchPose(q, id == SG_N_CATCH ? u : 1.f);
      if (id == SG_N_CATCH) hangPose(P, q, 0.f, 0.f, 0.f, 0.f);
      else if (id == SG_N_FLIP) hangPose(P, q, r.rot, u, 0.f, 0.f);
      else hangPose(P, q, r.rot, 1.f, r.st, (1.f - u) * (1.f - u));
      kin(h, P, k);
      const float y = rungY(L, K) + 1.5f;
      pinAnkle(h, k, 0, V(netX(L, y), y), 1.f);
      break;
    }
    case SG_N_DROP: {   // the foot comes free: he falls the rest of the way over onto his back
      HikerPose q = P, from;
      climbPose(q, K, 1.f, K);
      catchPose(q, 1.f);
      hangPose(from, q, kPi, 1.f, 0.f, 0.f);
      const HikerPose& lie = hikerPoseDef(POSE_LIE);
      const float w = easeInOut(window(u, 0.05f, 0.75f));
      P.lean = lerp(from.lean, unwrapNear(lie.lean, from.lean), w);
      P.head = lerp(from.head, lie.head, w);
      for (int i = 0; i < 2; ++i) {
        P.thigh[i] = lerp(from.thigh[i], unwrapNear(lie.thigh[i], from.thigh[i]), w);
        P.knee[i] = lerp(from.knee[i], lie.knee[i], w);
        P.shoulder[i] = lerp(from.shoulder[i], unwrapNear(lie.shoulder[i], from.shoulder[i]), w);
        P.elbow[i] = lerp(from.elbow[i], lie.elbow[i], w);
      }
      P.dx = 0.f;
      kin(h, P, k);
      const float lie_y = hipsStandingY(h, POSE_LIE);
      placeHips(h, k, aboveGround(h, k, lerp(r.ay, lie_y, easeOutBounce(u))));
      placeHipsX(h, r.ax);
      break;
    }
    default: break;
  }
}

}  // namespace

void ropesPose(const RopesState& r, Hiker& h) {
  if (!ropesHikerBusy(r)) return;
  Look L;
  lookOf(r, L);
  const int id = r.plan[r.seg];
  switch (id) {
    case SG_B_UP: case SG_B_CROSS1: case SG_B_BRACE: case SG_B_CROSS2: poseBridge(r, L, h, id); break;
    case SG_B_DOWN: poseBridgeDown(r, L, h); break;
    case SG_S_BOARD: case SG_S_PUMP: case SG_S_SPIN: case SG_S_SLOW: case SG_S_OFF: case SG_S_DIZZY: case SG_S_RECOVER:
      poseSwing(r, L, h, id);
      break;
    case SG_N_CLIMB: case SG_N_CATCH: case SG_N_FLIP: case SG_N_HANG: case SG_N_DROP: poseNet(r, L, h, id); break;
    case SG_N_SITUP: {   // dazed
      h.pose.head += 0.25f * sinT(r.st * 0.8f);
      break;
    }
    default: break;
  }
}

}  // namespace ss
