// The kite. See kite.h.
#include "kite.h"

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
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }
inline Vec2 mixV(Vec2 a, Vec2 b, float t) { return Vec2{lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }

// Turns folded into [-0.5, 0.5).
inline float wrapTurns(float x) { return x - floorf(x + 0.5f); }
// How far a first-order lag with time constant `tau` closes in dt.
inline float ease1(float dt, float tau) { return dt / (tau + dt); }

// Running float extents, turned into an integer rect with a margin for the
// antialiased fringe (as props.cpp does).
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

inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
              a.y1 > b.y1 ? a.y1 : b.y1};
}

// ---- tuning (all PROPOSED; how it looks is UNVERIFIED) ---------------------------------

// The diamond: the horizontal spar crosses the long one a third of the way down.
constexpr float kHalfW = kKiteW * 0.5f;
constexpr float kTopV = kKiteH * 0.33f;     // spar crossing to the top corner
constexpr float kBotV = kKiteH - kTopV;     // ... to the bottom corner, where the tail hangs
constexpr float kFoldNarrow = 0.75f;        // folded: width falls by this much
constexpr float kLaySquash = 0.58f;         // lying flat: height falls by this much (seen from the side)
constexpr int kTailSegs = 9;                // bows sit at 3, 6 and 9
constexpr int kLineSegs = 8;                // the line is drawn and marked in at most this many pieces
constexpr float kLineSegPx = 22.f;          // about this long each (fewer for a short line)

// Motion.
constexpr float kMaxDt = 0.1f;
constexpr float kWindEase = 0.35f;          // s: how fast the felt wind follows the input
constexpr float kLaunchSecs = 1.2f;         // LAUNCH becomes FLY
constexpr float kPayout = 55.f;             // px/s the line runs out
constexpr float kReel = 60.f;               // px/s it is wound in
constexpr float kOmegaFly = 3.2f;           // rad/s of the critically damped spring that follows the wind
constexpr float kPullAngle = 0.19f;         // turns from vertical a pulling kite lies at (about 22 deg up)
constexpr float kPullRun = 90.f;            // px/s the line drags him at full pull
constexpr float kPullSkid = 26.f;           // px/s of the first skid
constexpr float kTrail = 56.f;              // px the loose line trails behind a walking hand
constexpr float kFall = 760.f;              // px/s^2 of the nosedive
constexpr float kGravity = 900.f;           // px/s^2 once it is on the ground

// ---- the look: everything the draw reads, quantized ---------------------------------------

// Whole pixels for every position, 1/256 turn for the tilt, a few steps for
// the tail. Two states with the same Look draw the same pixels, so a tick that
// changes nothing visible marks nothing. Zeroed first, so memcmp is exact.
struct Look {
  uint8_t sprite;     // the diamond and tail are drawn
  uint8_t line;       // 0 none, 1 hand to kite, 2 loose line hanging from the hand
  int16_t cx, cy;     // where the spars cross
  uint8_t rot;        // 1/128 turn
  uint8_t fold, lay;  // 0..255
  uint8_t tph;        // tail ripple phase, 1/256 turn in steps of 1/16
  int8_t tdx, tdy;    // tail direction x 64
  uint8_t tamp;       // tail ripple amplitude, 1/16 px
  uint8_t tlen;       // tail length, 0..255 of kKiteTail
  int16_t hx, hy;     // the hand
  int16_t ex, ey;     // line 2: the loose end
  int16_t bow;        // line 1: how far the line bellies out, 1/4 px
  int16_t gy;         // the ground row: the line lies on it, never below
};

int16_t qpx(float v) {
  if (!finite(v)) return 0;
  return (int16_t)floorf(clampf(v, -16000.f, 16000.f) + 0.5f);
}
uint8_t q8(float v) { return finite(v) ? (uint8_t)(clampf(v, 0.f, 1.f) * 255.f + 0.5f) : (uint8_t)0; }

// The corner offsets of the diamond from the spar crossing, in the kite's own
// frame: u across (+ right), v along (+ down).
constexpr float kCornerU[4] = {0.f, -kHalfW, 0.f, kHalfW};    // top, left, bottom, right
constexpr float kCornerV[4] = {-kTopV, 0.f, kBotV, 0.f};

// A point of the kite's frame in the picture, relative to the crossing: tilted
// by rot, narrowed by fold, flattened by lay.
inline Vec2 mapKite(float u, float v, float c, float s, float fold, float lay) {
  const float uu = u * (1.f - kFoldNarrow * fold);
  return Vec2{c * uu - s * v, (s * uu + c * v) * (1.f - kLaySquash * lay)};
}

// How far below the crossing the kite reaches, for resting on the ground.
float lowestOffset(float rot, float fold, float lay) {
  const float c = cosT(rot), s = sinT(rot);
  float m = -1.0e9f;
  for (int i = 0; i < 4; ++i) m = maxf(m, mapKite(kCornerU[i], kCornerV[i], c, s, fold, lay).y);
  return m + 1.f;   // the outline
}

void makeLookRaw(const KiteState& k, Look& lk) {
  memset(&lk, 0, sizeof lk);
  const int m = k.mode;
  const float w = clamp01(finite(k.wind) ? k.wind : 0.f);
  lk.gy = qpx(k.ground_y);
  lk.hx = qpx(k.hand.x);
  lk.hy = qpx(k.hand.y);
  if (m == KITE_GONE) {
    lk.line = 2;
    lk.ex = qpx(k.end.x);
    lk.ey = qpx(k.end.y);
    return;
  }
  if (m == KITE_HIDDEN || m >= KITE_MODES) return;

  lk.sprite = 1;
  lk.cx = qpx(k.pos.x);
  lk.cy = qpx(k.pos.y);
  lk.rot = finite(k.rot) ? (uint8_t)((int)floorf(wrapTurns(k.rot) * 128.f + 0.5f) & 127) : (uint8_t)0;
  lk.fold = q8(k.fold);
  lk.lay = q8(k.lay);
  lk.tlen = q8(1.f - clamp01(k.fold));
  const float d = (float)k.dir;
  const float lay = clamp01(k.lay);

  // The tail's ripple: four waves to the sway clock, in eighth turns (a ripple that steps
  // ten times a second or so keeps the kite from redrawing on every tick).
  lk.tph = (uint8_t)(((int)floorf(k.phase * 4.f * 8.f) & 7) * 32);

  // Which way it hangs, and how much it ripples.
  float dx = d * 0.3f * w, dy = 1.f;
  float amp = 2.6f + 2.4f * w;
  switch (m) {
    case KITE_HELD:
      dx = 0.f;
      amp = 0.6f + 0.8f * w;
      break;
    case KITE_PULL: {   // streams out downwind, flapping
      const float p = easeInOut(window(k.t, 0.f, 1.5f));
      dx = d * (0.3f * w + 0.7f * p);
      dy = 1.f - 0.55f * p;
      amp *= 1.f + 0.5f * p;
      break;
    }
    case KITE_NOSE:
    case KITE_SPIN:   // trails behind the fall
      dx = -k.vel.x * 0.008f;
      dy = 0.6f - k.vel.y * 0.008f;
      amp = 3.6f;
      break;
    case KITE_DROP:
      dx = d * 0.15f - k.vel.x * 0.005f;
      dy = 1.f - k.vel.y * 0.004f;
      amp = 2.f;
      break;
    case KITE_GROUND:
      amp = 0.3f + 0.5f * w;
      break;
    default: break;
  }
  dx = lerp(dx, -d * 0.95f, lay);   // lying down, the tail lies along the ground behind it
  dy = lerp(dy, 0.f, lay);
  const float dn = sqrtf(dx * dx + dy * dy);
  if (dn > 1.0e-3f) {
    dx /= dn;
    dy /= dn;
  } else {
    dx = 0.f;
    dy = 1.f;
  }
  lk.tdx = (int8_t)floorf(dx * 64.f + 0.5f);
  lk.tdy = (int8_t)floorf(dy * 64.f + 0.5f);
  lk.tamp = (uint8_t)clampf(amp * 16.f + 0.5f, 0.f, 255.f);

  // The line, from the hand to the spars' crossing.
  if (m == KITE_HELD) return;
  lk.line = 1;
  const float ddx = k.pos.x - k.hand.x, ddy = k.pos.y - k.hand.y;
  const float dist = sqrtf(ddx * ddx + ddy * ddy);
  float bow;
  if (m == KITE_NOSE || m == KITE_SPIN || m == KITE_DROP || m == KITE_GROUND) {
    // Slack: a parabola whose extra length is what is left of the line.
    const float slack = k.r - dist;
    bow = slack > 0.5f ? minf(sqrtf(0.375f * dist * slack), 0.6f * dist) : 0.01f * dist;
  } else {
    // In the air a taut line barely bellies; a lazy one a little more.
    bow = dist * (0.012f + 0.05f * (1.f - clamp01(w * 1.25f)));
    if (m == KITE_PULL) bow *= 1.f - clamp01(k.t);
    if (m == KITE_REEL) bow *= clamp01(dist / 90.f);
  }
  lk.bow = qpx(bow * 4.f);
}

// What the draw cannot see is zeroed, so two states that draw alike have the same Look.
void canonLook(Look& lk) {
  if ((float)lk.tlen * kKiteTail < 4.f * 255.f) lk.tlen = lk.tph = lk.tamp = 0, lk.tdx = lk.tdy = 0;   // wound away
  if (lk.line == 0) lk.hx = lk.hy = lk.ex = lk.ey = lk.bow = 0;
  if (!lk.sprite && lk.line == 0) lk.gy = 0;
  if (lk.line == 1) lk.ex = lk.ey = 0;
  if (lk.line == 2) lk.bow = 0;
}

void makeLook(const KiteState& k, Look& lk) {
  makeLookRaw(k, lk);
  canonLook(lk);
}

// ---- the geometry, from a Look alone --------------------------------------------------

struct Geom {
  Vec2 outline[4];             // the diamond a little inflated: top, left, bottom, right
  Vec2 top, left, bottom, right;
  Vec2 tail[kTailSegs + 1];
  int n_tail;                  // 0 when the tail is wound away
  Vec2 bow_c[3], bow_t[3];     // the tail's bows: centre and unit tangent
  Vec2 line[kLineSegs + 1];
  int n_line;                  // segments, 0 for none
  bool sprite;
};

void makeSprite(const Look& lk, Geom& g) {
  g.sprite = lk.sprite != 0;
  g.n_tail = 0;
  if (!g.sprite) return;
  const float rot = (float)lk.rot / 128.f;
  const float c = cosT(rot), s = sinT(rot);
  const float fold = (float)lk.fold / 255.f, lay = (float)lk.lay / 255.f;
  const Vec2 o{(float)lk.cx, (float)lk.cy};
  auto at = [&](float u, float v) {
    const Vec2 p = mapKite(u, v, c, s, fold, lay);
    return Vec2{o.x + p.x, o.y + p.y};
  };
  g.top = at(0.f, -kTopV);
  g.left = at(-kHalfW, 0.f);
  g.bottom = at(0.f, kBotV);
  g.right = at(kHalfW, 0.f);
  g.outline[0] = at(0.f, -kTopV - 1.5f);
  g.outline[1] = at(-kHalfW - 1.1f, 0.f);
  g.outline[2] = at(0.f, kBotV + 1.5f);
  g.outline[3] = at(kHalfW + 1.1f, 0.f);

  const float len = kKiteTail * (float)lk.tlen / 255.f;
  if (len < 4.f) return;
  Vec2 d{(float)lk.tdx / 64.f, (float)lk.tdy / 64.f};
  const float dn = sqrtf(d.x * d.x + d.y * d.y);
  d = dn > 1.0e-3f ? Vec2{d.x / dn, d.y / dn} : Vec2{0.f, 1.f};
  const Vec2 perp{-d.y, d.x};
  const float amp = (float)lk.tamp / 16.f;
  const float ph = (float)lk.tph / 256.f;
  g.n_tail = kTailSegs + 1;
  // Whatever hangs below the ground piles up along it instead.
  const float ground = (float)lk.gy - 0.5f;
  const float along = d.x < -0.02f ? -1.f : 1.f;
  for (int i = 0; i <= kTailSegs; ++i) {
    const float f = (float)i / (float)kTailSegs;
    const float wave = amp * f * sinT(ph - 1.35f * f);
    Vec2 p{g.bottom.x + d.x * len * f + perp.x * wave, g.bottom.y + d.y * len * f + perp.y * wave};
    if (p.y > ground) {
      p.x += along * (p.y - ground);
      p.y = ground;
    }
    g.tail[i] = p;
  }
  for (int b = 0; b < 3; ++b) {
    const int i = 3 * (b + 1);
    const Vec2 a = g.tail[i - 1], z = g.tail[i == kTailSegs ? i : i + 1];
    Vec2 t{z.x - a.x, z.y - a.y};
    const float tn = sqrtf(t.x * t.x + t.y * t.y);
    t = tn > 1.0e-3f ? Vec2{t.x / tn, t.y / tn} : d;
    g.bow_c[b] = g.tail[i];
    g.bow_t[b] = t;
  }
}

void makeLine(const Look& lk, Geom& g) {
  g.n_line = 0;
  if (!lk.line) return;
  const Vec2 a{(float)lk.hx + 0.5f, (float)lk.hy + 0.5f};
  const Vec2 b = lk.line == 1 ? Vec2{(float)lk.cx + 0.5f, (float)lk.cy + 0.5f}
                              : Vec2{(float)lk.ex + 0.5f, (float)lk.ey + 0.5f};
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 2.f) return;
  int n = lk.line == 2 ? 6 : (int)(len / kLineSegPx);
  if (n < 1) n = 1;
  if (n > kLineSegs) n = kLineSegs;
  const float ground = (float)lk.gy - 0.5f;
  Vec2 p{0.f, 0.f};   // the side the belly goes: down, and downwind
  if (lk.line == 1) {
    p = Vec2{-dy / len, dx / len};
    if (p.y < 0.f || (p.y == 0.f && p.x < 0.f)) p = Vec2{-p.x, -p.y};
  }
  const float bow = (float)lk.bow * 0.25f;
  g.n_line = n;
  for (int i = 0; i <= n; ++i) {
    const float f = (float)i / (float)n;
    Vec2 q;
    if (lk.line == 1) {
      const float belly = bow * 4.f * f * (1.f - f);
      q = Vec2{a.x + dx * f + p.x * belly, a.y + dy * f + p.y * belly};
    } else {   // loose: drops from the hand and lies along the ground to its end
      const Vec2 ctl{a.x + dx * 0.3f, (float)lk.gy + 8.f};
      const float u = 1.f - f;
      q = Vec2{u * u * a.x + 2.f * u * f * ctl.x + f * f * b.x, u * u * a.y + 2.f * u * f * ctl.y + f * f * b.y};
    }
    if (i == 0) {
      q = a;
    } else if (i == n) {
      q = b;
    } else {   // whole pixels in between, so a line that has barely moved is the same line
      q.y = minf(q.y, ground);
      q = Vec2{floorf(q.x) + 0.5f, floorf(q.y) + 0.5f};
    }
    g.line[i] = q;
  }
}

void makeGeom(const Look& lk, Geom& g) {
  makeSprite(lk, g);
  makeLine(lk, g);
}

// ---- rects of the parts -----------------------------------------------------------------

Rect segRect(Vec2 a, Vec2 b) {
  Ext e;
  e.pt(a, 1.f);
  e.pt(b, 1.f);
  return e.rect();
}

Rect bodyRect(const Geom& g) {
  if (!g.sprite) return kEmpty;
  Ext e;
  for (const Vec2& p : g.outline) e.pt(p, 1.f);
  return e.rect();
}

Rect tailRect(const Geom& g) {
  if (!g.sprite || g.n_tail == 0) return kEmpty;
  Ext e;
  for (int i = 0; i < g.n_tail; ++i) e.pt(g.tail[i], 1.f);
  for (int b = 0; b < 3; ++b) e.pt(g.bow_c[b], 5.f);
  return e.rect();
}

Rect lineRect(const Geom& g) {
  Rect r = kEmpty;
  for (int i = 0; i < g.n_line; ++i) r = unite(r, segRect(g.line[i], g.line[i + 1]));
  return r;
}

bool sameV(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

bool bodySame(const Look& a, const Look& b) {
  return a.sprite == b.sprite && a.cx == b.cx && a.cy == b.cy && a.rot == b.rot && a.fold == b.fold && a.lay == b.lay;
}
bool tailSame(const Look& a, const Look& b) {
  return a.tph == b.tph && a.tdx == b.tdx && a.tdy == b.tdy && a.tamp == b.tamp && a.tlen == b.tlen;
}

// ---- the motion ---------------------------------------------------------------------------

// A critically damped spring on the position: smooth, and it keeps the velocity
// across mode changes.
void spring(KiteState& k, Vec2 to, float w, float dt) {
  // Semi-implicit Euler is only stable while w * h stays well under 1, so a long
  // dt (the kite stepped every second tick) is split.
  int n = (int)(w * dt / 0.35f) + 1;
  if (n > 6) n = 6;
  const float h = dt / (float)n;
  for (int i = 0; i < n; ++i) {
    const float ax = w * w * (to.x - k.pos.x) - 2.f * w * k.vel.x;
    const float ay = w * w * (to.y - k.pos.y) - 2.f * w * k.vel.y;
    k.vel.x += ax * h;
    k.vel.y += ay * h;
    k.pos.x += k.vel.x * h;
    k.pos.y += k.vel.y * h;
  }
}

// The line holds the kite no farther than `len` from the hand.
void tether(KiteState& k, float len) {
  const float dx = k.pos.x - k.hand.x, dy = k.pos.y - k.hand.y;
  const float d2 = dx * dx + dy * dy;
  if (d2 <= len * len) return;
  const float d = sqrtf(d2);
  const float nx = dx / d, ny = dy / d;
  k.pos.x = k.hand.x + nx * len;
  k.pos.y = k.hand.y + ny * len;
  const float out = k.vel.x * nx + k.vel.y * ny;
  if (out > 0.f) {
    k.vel.x -= out * nx;
    k.vel.y -= out * ny;
  }
}

float distToHand(const KiteState& k) {
  const float dx = k.pos.x - k.hand.x, dy = k.pos.y - k.hand.y;
  return sqrtf(dx * dx + dy * dy);
}

void setMode(KiteState& k, uint8_t mode) {
  k.mode = mode;
  k.t = 0.f;
}

// In his hands.
void stepHeld(KiteState& k, float dt) {
  k.fold += (clamp01((float)k.fold_to / 255.f) - k.fold) * ease1(dt, 0.1f);
  k.lay += (clamp01((float)k.lay_to / 255.f) - k.lay) * ease1(dt, 0.1f);
  const float d = (float)k.dir;
  k.rot += wrapTurns(d * (0.03f + 0.19f * k.lay) - k.rot) * ease1(dt, 0.12f);
  // Upright, his hand grips the long spar below the crossing; in his lap, at the crossing.
  const float grip = lerp(10.f, 0.f, k.lay);
  const Vec2 to{k.hand.x + sinT(k.rot) * grip + d * 4.f * k.lay, k.hand.y - cosT(k.rot) * grip - k.lay};
  const float dx = to.x - k.pos.x, dy = to.y - k.pos.y;
  if (dx * dx + dy * dy < 6.f) {   // carried: exactly in the hand, no lag
    k.pos = to;
    k.vel = Vec2{0.f, 0.f};
  } else {
    spring(k, to, 16.f, dt);   // picked up: snaps in
  }
}

// LAUNCH, FLY, REEL and PULL: the kite sits at the end of the line, in the
// direction the wind lays it, and a spring carries it there.
void stepFly(KiteState& k, float dt) {
  const bool launching = k.mode == KITE_LAUNCH;
  const float d = (float)k.dir;
  const float w = k.wind;
  const float up = launching ? easeInOut(k.t / kLaunchSecs) : 1.f;
  const float lift = clamp01((w - 0.04f) / 0.26f);   // a dead calm lets it sag
  const float amp = (0.55f + 0.9f * w) * lift * (0.35f + 0.65f * up);

  // The line's angle from vertical, in turns: more wind lays it lower; it sways.
  float a = lerp(0.03f, 0.05f + 0.095f * w, up);
  a += amp * (0.0065f * sinT(k.phase) + 0.0035f * sinT(2.f * k.phase + 0.31f));
  float pull = 0.f;
  if (k.mode == KITE_PULL) {
    pull = easeInOut(window(k.t, 0.25f, 1.8f));
    a = lerp(a, kPullAngle, pull);
  }

  // The line out.
  if (k.mode == KITE_REEL) {
    k.r = maxf(0.f, k.r - kReel * dt);
  } else if (k.r < k.line) {
    k.r = minf(k.line, k.r + kPayout * dt);
  } else {
    k.r = maxf(k.line, k.r - kReel * dt);
  }
  if (launching) k.r = maxf(k.r, distToHand(k));   // thrown, it takes line with it

  const float bob = 0.5f + 0.5f * sinT(2.f * k.phase + 0.2f);
  float re = k.r * (0.55f + 0.45f * lift);
  if (k.mode != KITE_PULL) re *= 1.f - 0.03f * bob * amp;
  const Vec2 to{k.hand.x + d * re * sinT(a), k.hand.y - re * cosT(a)};

  float omega = kOmegaFly;
  if (launching) omega = lerp(1.8f, kOmegaFly, up);
  if (k.mode == KITE_PULL) omega = 4.5f + 5.f * pull;
  spring(k, to, omega, dt);
  if (!launching) tether(k, k.r);
  if (k.mode == KITE_PULL && pull > 0.f) {   // the spring trails a hand that is being dragged: the line is dead taut
    const float dx = k.pos.x - k.hand.x, dy = k.pos.y - k.hand.y;
    const float dist = sqrtf(dx * dx + dy * dy);
    if (dist > 1.f) {
      const float s = lerp(dist, k.r, pull) / dist;
      k.pos = Vec2{k.hand.x + dx * s, k.hand.y + dy * s};
    }
  }

  // It lies along the line, rocking a little.
  float rot_to = d * 0.85f * a + amp * (0.02f * sinT(k.phase + 0.17f) + 0.01f * sinT(3.f * k.phase));
  if (k.mode == KITE_PULL) rot_to += d * 0.03f * pull;
  k.rot += wrapTurns(rot_to - k.rot) * ease1(dt, 0.2f);
  k.fold = maxf(0.f, k.fold - 2.5f * dt);
  k.lay = maxf(0.f, k.lay - 2.5f * dt);

  if (launching && k.t >= kLaunchSecs) setMode(k, KITE_FLY);
  if (k.mode == KITE_REEL && (distToHand(k) < 18.f || k.r < 6.f)) {   // in his hands again
    setMode(k, KITE_HELD);
    k.fold_to = 0;
    k.lay_to = 0;
    k.fold = k.lay = 0.f;
    k.vel = Vec2{0.f, 0.f};
  }
}

// It comes down.
void land(KiteState& k, float low) {
  k.pos.y = k.ground_y - low;
  if (k.mode == KITE_DROP) {
    k.vel = Vec2{0.f, 0.f};
  } else {
    k.vel = Vec2{k.vel.x * 0.25f, -k.vel.y * 0.18f};   // one small bounce
  }
  setMode(k, KITE_GROUND);
}

// NOSE, SPIN and DROP.
void stepFall(KiteState& k, float dt) {
  const float d = (float)k.dir;
  switch (k.mode) {
    case KITE_NOSE: {   // flips head down toward its side, then drops
      const float goal = 0.5f * d + 0.03f * sinT(k.t * 2.f);
      k.spin = clampf(wrapTurns(goal - k.rot) * 6.f, -1.6f, 1.6f);
      k.rot += k.spin * dt;
      k.vel.y = minf(k.vel.y + kFall * dt, 520.f);
      k.vel.x += (d * k.wind * 70.f - k.vel.x) * ease1(dt, 0.5f);
      break;
    }
    case KITE_SPIN:   // round and round, swinging from side to side as it falls
      k.spin = clampf(k.spin + d * 1.5f * dt, -2.6f, 2.6f);
      k.rot += k.spin * dt;
      k.vel.y = minf(k.vel.y + 240.f * dt, 170.f);
      k.vel.x += (d * (40.f + 55.f * sinT(0.5f * k.t)) - k.vel.x) * ease1(dt, 0.3f);
      break;
    default:   // DROP: the wind goes out of it
      k.rot += wrapTurns(d * 0.07f + 0.11f * sinT(0.75f * k.t) - k.rot) * ease1(dt, 0.15f);
      k.vel.y += (80.f - k.vel.y) * ease1(dt, 0.5f);
      k.vel.x += (d * k.wind * 35.f - k.vel.x) * ease1(dt, 0.6f);
      break;
  }
  k.pos.x += k.vel.x * dt;
  k.pos.y += k.vel.y * dt;
  tether(k, k.r);
  const float low = lowestOffset(k.rot, k.fold, k.lay);
  if (k.pos.y + low >= k.ground_y) land(k, low);
}

// Lying where it came down.
void stepGround(KiteState& k, float dt) {
  const float d = (float)k.dir;
  k.lay += (1.f - k.lay) * ease1(dt, 0.15f);
  k.fold = maxf(0.f, k.fold - 2.f * dt);
  k.rot += k.spin * dt;
  k.spin -= k.spin * minf(1.f, 8.f * dt);
  const float rest = d * (0.14f + 0.08f * (k.phase - floorf(k.phase)));
  k.rot += wrapTurns(rest - k.rot) * ease1(dt, 0.12f);
  k.vel.y += kGravity * dt;
  k.pos.x += k.vel.x * dt;
  k.pos.y += k.vel.y * dt;
  k.vel.x -= k.vel.x * minf(1.f, 9.f * dt);
  const float floor_y = k.ground_y - lowestOffset(k.rot, k.fold, k.lay);
  if (k.pos.y > floor_y) {
    k.pos.y = floor_y;
    k.vel.y = k.vel.y > 45.f ? -k.vel.y * 0.28f : 0.f;
  }
  // He gathers the slack in as he comes; walking away drags it along.
  k.r = maxf(distToHand(k) + 1.5f, k.r - 40.f * dt);
  tether(k, k.r);
}

// The kite is away: the loose end of the line lies behind his hand.
void stepGone(KiteState& k, Vec2 hand_before, float dt) {
  const float moved = k.hand.x - hand_before.x;
  if (moved > 0.15f) k.dir = -1;
  if (moved < -0.15f) k.dir = 1;
  const Vec2 to{k.hand.x + (float)k.dir * kTrail, k.ground_y - 1.f};
  k.end = mixV(k.end, to, ease1(dt, 0.25f));
  k.pos = k.hand;
  k.vel = Vec2{0.f, 0.f};
}

}  // namespace

// ---- commands ------------------------------------------------------------------------------------

void kiteInit(KiteState& k, float ground_y, int side, uint32_t seed) {
  memset(&k, 0, sizeof k);
  k.mode = KITE_HIDDEN;
  k.dir = side < 0 ? (int8_t)-1 : (int8_t)1;
  k.ground_y = ground_y;
  k.line = 200.f;
  k.phase = (float)(seed & 255u) / 256.f;
}

void kiteInit(KiteState& k, float ground_y, int side, Rng& rng) {
  const uint32_t seed = rng.next();
  kiteInit(k, ground_y, side, seed);
}

void kiteHide(KiteState& k) { setMode(k, KITE_HIDDEN); }

void kiteHold(KiteState& k, float fold, float lay) {
  if (k.mode != KITE_HELD) {
    // Out of nowhere it appears in his hand; from the ground or the air it is carried there.
    if (k.mode == KITE_HIDDEN || k.mode == KITE_GONE) k.pos = k.hand;
    setMode(k, KITE_HELD);
    k.vel = Vec2{0.f, 0.f};
  }
  k.fold_to = q8(fold);
  k.lay_to = q8(lay);
}

void kiteLaunch(KiteState& k, float toss) {
  toss = finite(toss) ? clamp01(toss) : 1.f;
  const float d = (float)k.dir;
  if (k.mode == KITE_HIDDEN || k.mode == KITE_GONE) k.pos = Vec2{k.hand.x + d * 4.f, k.hand.y - 14.f};
  k.r = maxf(12.f, distToHand(k));
  setMode(k, KITE_LAUNCH);
  k.vel = Vec2{d * (18.f + 40.f * toss), -(26.f + 180.f * toss)};
  k.spin = 0.f;
}

void kiteFly(KiteState& k, float line) {
  k.line = finite(line) ? clampf(line, 20.f, 400.f) : k.line;
  switch (k.mode) {
    case KITE_LAUNCH:
    case KITE_FLY: break;
    case KITE_HIDDEN:
    case KITE_HELD:
    case KITE_GONE: kiteLaunch(k, 0.6f); break;
    default:
      k.r = maxf(k.r, distToHand(k));
      setMode(k, KITE_FLY);
      break;
  }
}

void kiteReel(KiteState& k) {
  if (k.mode == KITE_LAUNCH || k.mode == KITE_FLY || k.mode == KITE_PULL) setMode(k, KITE_REEL);
}

namespace {
inline bool airborne(const KiteState& k) {
  return k.mode == KITE_LAUNCH || k.mode == KITE_FLY || k.mode == KITE_REEL || k.mode == KITE_PULL;
}
}  // namespace

void kiteNosedive(KiteState& k) {
  if (!airborne(k)) return;
  setMode(k, KITE_NOSE);
  k.spin = (float)k.dir;
  k.r = maxf(k.r, distToHand(k));
}

void kiteSpinCrash(KiteState& k) {
  if (!airborne(k)) return;
  setMode(k, KITE_SPIN);
  k.spin = (float)k.dir * 0.8f;
  k.r = maxf(k.r, distToHand(k));
}

void kiteDrop(KiteState& k) {
  if (!airborne(k)) return;
  setMode(k, KITE_DROP);
  k.spin = 0.f;
  k.r = maxf(k.r, distToHand(k));
}

void kitePull(KiteState& k) {
  if (k.mode == KITE_LAUNCH || k.mode == KITE_FLY || k.mode == KITE_REEL) setMode(k, KITE_PULL);
}

void kiteLose(KiteState& k) {
  setMode(k, KITE_GONE);
  k.dir = (int8_t)-1;   // the line trails behind a hand that is running to the right
  k.end = Vec2{k.hand.x, k.ground_y - 1.f};
  k.fold = k.lay = 0.f;
  k.vel = Vec2{0.f, 0.f};
}

float kitePullSpeed(const KiteState& k) {
  if (k.mode != KITE_PULL) return 0.f;
  const float skid = easeInOut(window(k.t, 0.5f, 2.f)) * kPullSkid;
  const float run = easeInCubic(window(k.t, 2.f, 4.5f)) * (kPullRun - kPullSkid);
  return (float)k.dir * (skid + run);
}

// ---- the step ---------------------------------------------------------------------------------------

void kiteStep(KiteState& k, Vec2 hand, float wind, float dt) {
  if (!finite(dt) || dt < 0.f) dt = 0.f;
  if (dt > kMaxDt) dt = kMaxDt;
  if (!finite(hand.x) || !finite(hand.y)) hand = k.hand;
  const Vec2 hand_before = k.hand;
  k.hand = hand;
  k.t += dt;

  // The wind it feels follows the input; a pulling kite is in a gust whatever the weather says.
  float want = finite(wind) ? clamp01(wind) : k.wind;
  if (k.mode == KITE_PULL) want = maxf(want, window(k.t, 0.f, 0.8f));
  k.wind += (want - k.wind) * ease1(dt, want > k.wind ? kWindEase * 0.7f : kWindEase * 1.6f);
  k.phase += dt * (0.2f + 0.18f * k.wind);
  k.phase -= floorf(k.phase);

  switch (k.mode) {
    case KITE_HELD: stepHeld(k, dt); break;
    case KITE_LAUNCH:
    case KITE_FLY:
    case KITE_REEL:
    case KITE_PULL: stepFly(k, dt); break;
    case KITE_NOSE:
    case KITE_SPIN:
    case KITE_DROP: stepFall(k, dt); break;
    case KITE_GROUND: stepGround(k, dt); break;
    case KITE_GONE: stepGone(k, hand_before, dt); break;
    default:   // HIDDEN: waits in his hand
      k.pos = hand;
      k.vel = Vec2{0.f, 0.f};
      break;
  }

  if (!finite(k.pos.x) || !finite(k.pos.y) || !finite(k.vel.x) || !finite(k.vel.y)) {   // never expected
    k.pos = hand;
    k.vel = Vec2{0.f, 0.f};
  }
}

// ---- bounds, draw ------------------------------------------------------------------------------------

Rect kiteBounds(const KiteState& k) {
  Look lk;
  makeLook(k, lk);
  Geom g;
  makeGeom(lk, g);
  return unite(unite(bodyRect(g), tailRect(g)), lineRect(g));
}

bool kiteSame(const KiteState& a, const KiteState& b) {
  Rect none[KITE_DIRTY_MAX];
  return kiteDirty(a, b, none) == 0;   // (the Looks may differ in what no pixel shows, such as a line's belly under 1 px)
}

int kiteDirty(const KiteState& before, const KiteState& after, Rect out[KITE_DIRTY_MAX]) {
  Look la, lb;
  makeLook(before, la);
  makeLook(after, lb);
  if (memcmp(&la, &lb, sizeof la) == 0) return 0;
  Geom ga, gb;
  makeGeom(la, ga);
  makeGeom(lb, gb);
  int n = 0;

  // The diamond, then the tail (which hangs from it, so it moves with it).
  const bool body = !bodySame(la, lb);
  if (body) out[n++] = unite(bodyRect(ga), bodyRect(gb));
  if (body || !tailSame(la, lb)) {
    const Rect t = unite(tailRect(ga), tailRect(gb));
    if (!t.empty()) out[n++] = t;
  }

  // The line: each run of segments that differ, old and new together.
  if (ga.n_line != gb.n_line) {
    const Rect r = unite(lineRect(ga), lineRect(gb));
    if (!r.empty()) out[n++] = r;
    return n;
  }
  int i = 0;
  while (i < ga.n_line) {
    if (sameV(ga.line[i], gb.line[i]) && sameV(ga.line[i + 1], gb.line[i + 1])) {
      ++i;
      continue;
    }
    Rect run = kEmpty;
    while (i < ga.n_line && !(sameV(ga.line[i], gb.line[i]) && sameV(ga.line[i + 1], gb.line[i + 1]))) {
      run = unite(run, unite(segRect(ga.line[i], ga.line[i + 1]), segRect(gb.line[i], gb.line[i + 1])));
      ++i;
    }
    if (n < KITE_DIRTY_MAX) {
      out[n++] = run;
    } else {   // never expected (8 segments make at most 4 runs): fold into the last rect
      out[n - 1] = unite(out[n - 1], run);
    }
  }
  return n;
}

namespace {

// A ribbon `hw` either side of a polyline, as one polygon (out has 2 * n points).
void ribbon(const Vec2* p, int n, float hw, Vec2* out) {
  for (int i = 0; i < n; ++i) {
    const Vec2 a = p[i > 0 ? i - 1 : 0], z = p[i < n - 1 ? i + 1 : n - 1];
    float tx = z.x - a.x, ty = z.y - a.y;
    const float tn = sqrtf(tx * tx + ty * ty);
    if (tn > 1.0e-3f) {
      tx /= tn;
      ty /= tn;
    } else {
      tx = 0.f;
      ty = 1.f;
    }
    out[i] = Vec2{p[i].x - ty * hw, p[i].y + tx * hw};
    out[2 * n - 1 - i] = Vec2{p[i].x + ty * hw, p[i].y - tx * hw};
  }
}

// One segment of the line: a quad 2 * hw px wide, square ended so neighbours butt.
void drawSeg(Surface& s, Vec2 a, Vec2 b, float hw, Pixel color, const Rect& clip) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f) return;
  const float nx = -dy / len * hw, ny = dx / len * hw;
  const Vec2 q[4] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
  fillPolygon(s, q, 4, color, clip);
}

}  // namespace

void kiteDraw(const KiteState& k, Surface& strip, const Rect& clip, const Palette& pal) {
  Look lk;
  makeLook(k, lk);
  if (!lk.sprite && !lk.line) return;
  Geom g;
  makeGeom(lk, g);
  const Pixel ink = pal[ACTOR];

  for (int i = 0; i < g.n_line; ++i) {
    if (overlaps(segRect(g.line[i], g.line[i + 1]), clip)) drawSeg(strip, g.line[i], g.line[i + 1], lk.line == 2 ? 0.75f : 0.55f, ink, clip);
  }
  if (!lk.sprite) return;

  // The tail: a thin wavy ribbon with a bow at every third knot.
  if (g.n_tail > 0 && overlaps(tailRect(g), clip)) {
    Vec2 poly[2 * (kTailSegs + 1)];
    ribbon(g.tail, g.n_tail, 0.55f, poly);
    fillPolygon(strip, poly, 2 * g.n_tail, ink, clip);
    static const Slot kBowSlot[3] = {MUSHROOM, TENT_LIT, MUSHROOM};
    for (int b = 0; b < 3; ++b) {
      const Vec2 c = g.bow_c[b], t = g.bow_t[b];
      const Vec2 p{-t.y, t.x};
      constexpr float w = 3.4f, h = 2.2f;   // each loop: w out from the knot, 2h along the tail
      const Vec2 q[6] = {c,
                         {c.x - p.x * w - t.x * h, c.y - p.y * w - t.y * h},
                         {c.x - p.x * w + t.x * h, c.y - p.y * w + t.y * h},
                         c,
                         {c.x + p.x * w - t.x * h, c.y + p.y * w - t.y * h},
                         {c.x + p.x * w + t.x * h, c.y + p.y * w + t.y * h}};
      fillPolygon(strip, q, 6, pal[kBowSlot[b]], clip);
    }
  }

  // The diamond: an ink outline, the two halves of the sail, the cross spar.
  if (overlaps(bodyRect(g), clip)) {
    fillPolygon(strip, g.outline, 4, ink, clip);
    const Vec2 lh[3] = {g.top, g.left, g.bottom};
    const Vec2 rh[3] = {g.top, g.right, g.bottom};
    fillPolygon(strip, lh, 3, pal[MUSHROOM], clip);
    fillPolygon(strip, rh, 3, pal[TENT_LIT], clip);
    strokeCapsule(strip, g.top, g.bottom, 1.1f, ink, clip);
    strokeCapsule(strip, g.left, g.right, 1.1f, ink, clip);
  }
}

}  // namespace ss
