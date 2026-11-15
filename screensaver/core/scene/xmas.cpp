// The Christmas set's mockup drawings. See xmas.h. Everything here is PROPOSED and unverified on the panel.
#include "xmas.h"

namespace ss {
int ssXmasSwitch() { return SS_XMAS; }
}  // namespace ss

#if SS_XMAS   // xmas_switch.h: the panel's build compiles none of this

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "critters.h"
#include "props.h"
#include "world.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline Vec2 V(float x, float y) { return Vec2{x, y}; }
inline Vec2 addV(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
inline Vec2 mixV(Vec2 a, Vec2 b, float t) { return Vec2{lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
inline float frac(float x) { return x - floorf(x); }

const Rect kNone{0, 0, 0, 0};

inline void unite(Rect& r, const Rect& b) {
  if (b.empty()) return;
  if (r.empty()) {
    r = b;
    return;
  }
  r = Rect{r.x0 < b.x0 ? r.x0 : b.x0, r.y0 < b.y0 ? r.y0 : b.y0, r.x1 > b.x1 ? r.x1 : b.x1, r.y1 > b.y1 ? r.y1 : b.y1};
}
inline Rect box(float x0, float y0, float x1, float y1, float pad = 2.f) {
  return Rect{(int)floorf(x0 - pad), (int)floorf(y0 - pad), (int)ceilf(x1 + pad), (int)ceilf(y1 + pad)};
}
inline Rect around(Vec2 c, float rx, float ry) { return box(c.x - rx, c.y - ry, c.x + rx, c.y + ry); }
inline bool hits(const Rect& a, const Rect& clip) { return !a.clipped(clip).empty(); }

// ---- constants (the look: tune by eye) ------------------------------------------------------------

// The lights. Warm white, or the classic five colours in turn.
constexpr uint32_t kWarmCore = 0xFFE4A6, kWarmHalo = 0xFFB259;
constexpr uint32_t kMulti[5] = {0xF0323C, 0xFFC23A, 0x3CCB5A, 0x4C8CFF, 0xFF7A2A};   // red, gold, green, blue, orange
constexpr float kBulbR = 1.5f;        // a bulb's core, px
constexpr float kHaloR0 = 2.0f;       // its halo's radius by day ... (none is drawn by day)
constexpr float kHaloR1 = 4.2f;       // ... and at full dark
constexpr float kHaloA = 105.f;       // the halo's opacity at full dark, of 255
constexpr float kDimLevel = 0.35f;    // XP_DIM: how much light is left while the generator sparks
constexpr float kGroundGlowA = 15.f;  // the light on the ground under the tree at full dark, each of three rings
constexpr float kBulbStep = 4.5f;     // bulbs per wrap: 3 + the tier's half width over this
constexpr int kBulbsMax = 16;
// The garland: gold tinsel in two scallops across each tier from the second down.
constexpr uint32_t kTinsel = 0xC8962E, kTinselHi = 0xF8DC7C, kTinselLit = 0xFFD27A;
constexpr float kTinselW = 3.0f, kTinselSag = 5.f;
// The ornaments: red and gold balls.
constexpr uint32_t kOrnRed = 0xC8202C, kOrnGold = 0xE8B040;
constexpr float kOrnR = 3.4f;
// The star on top.
constexpr uint32_t kStarGold = 0xF4C64A;
constexpr float kStarR = 8.5f, kStarIn = 3.6f;
// The wheel: rim radius, paddles beyond it, seen a little from downstream (the rim an ellipse).
constexpr float kWheelR = 17.f, kPaddle = 7.f, kWheelSquash = 0.8f;   // x 601-640 at hub x 620: its bank
constexpr float kWheelDip = 5.f;   // how far the lowest paddle reaches under the waterline (above the boulder's top)
constexpr int kSpokes = 8;      // four crossed sticks
constexpr int kPaddles = 12;
constexpr float kDepthX = -5.5f, kDepthY = -1.2f;   // the boards' width, back along the axle toward the bank
constexpr float kBoxW = 17.f, kBoxH = 11.f;
// The birds and the raccoon.
constexpr float kBirdScale = 1.5f;   // as the mail bird
constexpr float kCoonScale = 1.4f;   // over the story's raccoon, so the mask and rings read across the room
// Wrapping paper.
constexpr uint32_t kPaperRed = 0xB82434, kPaperGreen = 0x2E7A4A, kPaperGold = 0xD9A640, kRibbonGold = 0xF2CC66;
constexpr uint32_t kStockRed = 0xC02430;

// ---- the camp pine (landscape.cpp drawCampPine and tierGeom, full grown: keep in step) ----------

struct TierG {
  float apex, base, hw;
};
inline float treeX(const Landscape& ls) { return fl(ls.camp.x); }
inline float treeTop(const Landscape& ls) { return fl(ls.camp.base_y) - fl(ls.camp.h); }
inline float treeBase(const Landscape& ls) { return fl(ls.camp.base_y); }
inline int tierCount(const Landscape& ls) { return ls.camp.tiers < XMAS_WRAPS ? ls.camp.tiers : XMAS_WRAPS; }
TierG tierOf(const Landscape& ls, int i) {
  const LsPine& p = ls.camp;
  const float h = fl(p.h), top = fl(p.base_y) - h, fbot = 318.f;
  const float F = fbot - top, th = 0.42f * F;
  const int n = p.tiers;
  const float sp = n > 1 ? (F - th) / fl(n - 1) : 0.f;
  TierG t;
  t.apex = top + fl(i) * sp;
  t.base = t.apex + th;
  t.hw = 0.5f * (fl(p.width_pct) * 0.01f) * h * (1.f - 0.2f * fl(n - 1 - i));
  return t;
}
inline float tierHalfAt(const TierG& t, float y) { return t.hw * clamp01((y - t.apex) / (t.base - t.apex)); }

// A point on the front of tier t at `a` (-1 the left edge .. 1 the right, the angle round the cone
// over a quarter turn), at `dy` above the tier's base on the edges; the front of the round sits
// `drop` lower (seen from a little above). x bunches toward the edges as on a cone.
Vec2 onTier(float cx, const TierG& t, float a, float dy_l, float dy_r, float drop) {
  const float sn = sinT(a * 0.25f), cs = cosT(a * 0.25f);
  const float y = t.base - lerp(dy_l, dy_r, 0.5f * (sn + 1.f)) + drop * cs;
  return V(cx + 0.92f * tierHalfAt(t, y) * sn, y);
}

// The light string's wrap k (on tier k): it runs down to the right, as a spiral does.
Vec2 cordAt(const Landscape& ls, int k, float a) {
  const TierG t = tierOf(ls, k);
  return k == 0 ? onTier(treeX(ls), t, a, 17.f, 6.f, 2.f) : onTier(treeX(ls), t, a, 12.f, 3.5f, 2.5f);
}
int bulbCount(const Landscape& ls, int k) {
  const int n = (int)(tierOf(ls, k).hw / kBulbStep + 0.5f) + 3;
  return n > kBulbsMax ? kBulbsMax : n;
}
inline float bulbA(int j, int n) { return -1.f + (2.f * fl(j) + 1.f) / fl(n); }

// ---- small drawing helpers -----------------------------------------------------------------------

Pixel hexPx(uint32_t h) { return rgb565_hex(h); }

// How dark it is, 0 by day .. 1 at night, from the ground's colour (so dusk and the night rule count).
float darkOf(const Palette& pal) {
  const Pixel g = pal[GROUND];
  const float l = (0.2126f * (float)red8(g) + 0.7152f * (float)green8(g) + 0.0722f * (float)blue8(g)) / 255.f;
  return clamp01((0.30f - l) / 0.2f);
}

void softDisc(Surface& sf, Vec2 c, float rx, float ry, Pixel col, float a, const Rect& clip) {
  if (a < 0.5f || rx <= 0.f || !hits(around(c, rx, ry), clip)) return;
  const int A = (int)(a + 0.5f);
  Vec2 p[16];
  for (int i = 0; i < 16; ++i) {
    const float u = fl(i) / 16.f;
    p[i] = V(c.x + rx * cosT(u), c.y + ry * sinT(u));
  }
  fillPolygonAlpha(sf, p, 16, col, (uint8_t)(A > 255 ? 255 : A), clip);
}

void polyline(Surface& sf, const Vec2* p, int n, float w, Pixel col, const Rect& clip) {
  for (int i = 0; i + 1 < n; ++i) strokeCapsule(sf, p[i], p[i + 1], w, col, clip);
}

// A point on a hanging run from a to b sagging `sag` px in the middle.
Vec2 sagAt(Vec2 a, Vec2 b, float sag, float u) {
  const Vec2 p = mixV(a, b, u);
  return V(p.x, p.y + 4.f * sag * u * (1.f - u));
}

// ---- the lights -----------------------------------------------------------------------------------

struct LightLook {
  float dark;    // 0 day .. 1 night
  float level;   // 1 on, kDimLevel dimmed, 0 off
  bool multi;
};

uint32_t bulbHex(const LightLook& L, int k, int j) { return L.multi ? kMulti[(j + 2 * k) % 5] : kWarmCore; }

// Whether wrap k is lit: strung, powered, and not in the dead lower half.
bool wrapLit(const XmasMock& m, int k) {
  if (m.power == XP_OFF || k >= m.wraps) return false;
  if (m.power == XP_HALF_DEAD) return k < (m.wraps + 1) / 2;
  return true;
}
float powerLevel(const XmasMock& m) { return m.power == XP_DIM ? kDimLevel : (m.power == XP_OFF ? 0.f : 1.f); }

void drawBulb(Surface& sf, Vec2 p, uint32_t hex, bool lit, float level, float dark, const Palette& pal, const Rect& clip,
              float vary = 1.f) {
  const Pixel unlit = mixRGB(critterLitAs(pal, hex, MUSHROOM), pal[ACTOR], 0.45f);
  if (!lit || level <= 0.f) {
    fillCircle(sf, p, kBulbR * 0.85f, unlit, clip);
    return;
  }
  const Pixel bright = hexPx(hex);
  if (dark > 0.f) {
    const bool warm = hex == kWarmCore;
    const float r = lerp(kHaloR0, warm ? kHaloR1 : kHaloR1 - 0.8f, dark);
    softDisc(sf, p, r, r, hexPx(warm ? kWarmHalo : hex), kHaloA * dark * level * vary, clip);
  }
  // By day a lit bulb is a bright bead; in the dark its middle goes pale with the glow.
  Pixel core = mixRGB(bright, hexPx(0xFFF6DC), 0.35f * dark);
  if (level < 1.f) core = mixRGB(unlit, core, level + 0.2f);
  fillCircle(sf, p, kBulbR, core, clip);
}

void drawWrap(const World& w, int k, const LightLook& L, bool lit, Surface& sf, const Rect& clip, const Palette& pal) {
  const Landscape& ls = w.land;
  {   // only the strips its tier's band crosses (the cord stays within the tier, the halos 5 px round it)
    const TierG t = tierOf(ls, k);
    if (!hits(box(fl(ls.camp.x) - t.hw, t.base - 26.f, fl(ls.camp.x) + t.hw, t.base + 4.f, 6.f), clip)) return;
  }
  Vec2 c[17];
  for (int i = 0; i <= 16; ++i) c[i] = cordAt(ls, k, -1.f + fl(i) / 8.f);
  // The cord: a dark green wire, seen by day, gone in the dark.
  const Pixel cord = mixRGB(pal[ACTOR], pal[FOLIAGE_DARK], 0.35f);
  polyline(sf, c, 17, 0.9f, cord, clip);
  const int n = bulbCount(ls, k);
  for (int j = 0; j < n; ++j) {
    const float vary = 0.75f + 0.25f * frac(0.618f * fl(j + 7 * k) + 0.3f);   // no two quite alike
    drawBulb(sf, cordAt(ls, k, bulbA(j, n)), bulbHex(L, k, j), lit, L.level, L.dark, pal, clip, vary);
  }
}

// ---- the garland ----------------------------------------------------------------------------------

Pixel tinselCol(const Palette& pal, float lit, bool hi) {
  const Pixel base = critterLitAs(pal, hi ? kTinselHi : kTinsel, MUSHROOM);
  return lit > 0.f ? mixRGB(base, hexPx(kTinselLit), (hi ? 0.6f : 0.4f) * lit) : base;
}
void drawTinsel(Surface& sf, const Vec2* p, int n, float lit, const Palette& pal, const Rect& clip) {
  const Pixel base = tinselCol(pal, lit, false), hi = tinselCol(pal, lit, true);
  polyline(sf, p, n, kTinselW, base, clip);
  for (int i = 0; i + 1 < n; ++i) strokeCapsule(sf, V(p[i].x, p[i].y - 0.6f), V(p[i + 1].x, p[i + 1].y - 0.6f), 1.0f, hi, clip);
  for (int i = 1; i < n; i += 3) fillCircle(sf, V(p[i].x + 0.5f, p[i].y + 0.6f), 0.8f, hi, clip);
}
// Scallop j (0 left, 1 right) on tier k: 7 points.
void scallop(const Landscape& ls, int k, int j, Vec2* p) {
  const TierG t = tierOf(ls, k);
  const float a0 = j == 0 ? -0.94f : 0.f, a1 = j == 0 ? 0.f : 0.94f;
  for (int i = 0; i < 7; ++i) {
    const float u = fl(i) / 6.f;
    const Vec2 q = onTier(treeX(ls), t, lerp(a0, a1, u), 17.f, 15.f, 2.f);
    p[i] = V(q.x, q.y + kTinselSag * sinT(0.5f * u));
  }
}
// The scallop's ends: where the garland is caught on the boughs.
Vec2 scallopEnd(const Landscape& ls, int k, float a) { return onTier(treeX(ls), tierOf(ls, k), a, 17.f, 15.f, 2.f); }

// ---- the ornaments --------------------------------------------------------------------------------

struct OrnPlace {
  uint8_t tier;
  float a;
  uint8_t gold;
  float hook;   // the hook's length below the tier's edge
};
const OrnPlace kOrn[XMAS_ORNS] = {{1, -0.74f, 0, 2.f}, {1, 0.58f, 1, 3.f}, {2, -0.22f, 0, 2.f}, {2, 0.8f, 1, 3.f},
                                  {3, -0.62f, 1, 2.f}, {3, 0.36f, 0, 3.f}, {4, -0.76f, 0, 2.5f}};

Vec2 ornHook(const Landscape& ls, int i) {
  const OrnPlace& o = kOrn[i];
  const TierG t = tierOf(ls, o.tier);
  const float sn = sinT(o.a * 0.25f);
  return V(treeX(ls) + 0.92f * t.hw * sn, t.base - 2.5f);
}
Vec2 ornBall(const Landscape& ls, int i) {
  const Vec2 h = ornHook(ls, i);
  return V(h.x, h.y + 2.5f + kOrn[i].hook + kOrnR);
}

void drawBall(Surface& sf, Vec2 c, bool gold, float lit, const Palette& pal, const Rect& clip) {
  const uint32_t hex = gold ? kOrnGold : kOrnRed;
  Pixel col = critterLitAs(pal, hex, MUSHROOM);
  if (lit > 0.f) col = mixRGB(col, hexPx(hex), 0.5f * lit);
  fillCircle(sf, c, kOrnR, col, clip);
  fillCircle(sf, V(c.x + 0.9f, c.y + 1.1f), kOrnR * 0.55f, mixRGB(col, pal[ACTOR], 0.25f), clip);   // shade low right
  fillCircle(sf, V(c.x - 0.6f, c.y - 0.4f), kOrnR * 0.62f, col, clip);
  const Pixel shine = lit > 0.f ? mixRGB(pal[CLOUD_LIT], hexPx(0xFFF0C8), lit) : mixRGB(pal[CLOUD_LIT], pal[STAR], 0.5f);
  fillCircle(sf, V(c.x - 1.2f, c.y - 1.3f), 0.9f, shine, clip);
  // The cap.
  fillRectF(sf, c.x - 1.1f, c.y - kOrnR - 1.2f, c.x + 1.1f, c.y - kOrnR + 0.4f, mixRGB(pal[ROCK], pal[CLOUD_LIT], 0.4f), clip);
}
void drawHook(Surface& sf, Vec2 a, Vec2 b, const Palette& pal, const Rect& clip) {
  strokeCapsule(sf, a, b, 0.7f, mixRGB(pal[ROCK], pal[CLOUD_LIT], 0.5f), clip);
}

// ---- the star -------------------------------------------------------------------------------------

Vec2 starAt(const Landscape& ls) { return V(treeX(ls), treeTop(ls) - 2.f); }
void starPts(Vec2 c, float R, float r, Vec2* p) {
  for (int i = 0; i < 10; ++i) {
    const float u = fl(i) / 10.f - 0.25f;   // a point straight up
    const float rr = (i & 1) ? r : R;
    p[i] = V(c.x + rr * cosT(u), c.y + rr * sinT(u));
  }
}
void drawStar(Surface& sf, const Landscape& ls, float lit, const Palette& pal, const Rect& clip) {
  const Vec2 c = starAt(ls);
  if (lit > 0.f) {
    softDisc(sf, c, 12.f, 12.f, hexPx(kWarmHalo), 20.f * lit, clip);
    softDisc(sf, c, 8.5f, 8.5f, hexPx(kWarmHalo), 30.f * lit, clip);
    softDisc(sf, c, 5.5f, 5.5f, hexPx(kWarmHalo), 45.f * lit, clip);
  }
  Vec2 p[10];
  starPts(c, kStarR + 0.9f, kStarIn + 0.6f, p);
  const Pixel gold = critterLitAs(pal, kStarGold, MUSHROOM);
  fillPolygon(sf, p, 10, mixRGB(gold, pal[ACTOR], 0.45f), clip);   // a darker rim, so it reads on the sky by day
  starPts(c, kStarR, kStarIn, p);
  fillPolygon(sf, p, 10, lit > 0.f ? mixRGB(gold, hexPx(0xFFF0B8), lit) : gold, clip);
}

// ---- the ladder, the coil ---------------------------------------------------------------------------

// The two rails, foot to top: leaning on the tree's right side.
void ladderRails(const Landscape& ls, Vec2 rail[2][2]) {
  const float cx = treeX(ls), base = treeBase(ls);
  rail[0][0] = V(cx + 34.f, base + 7.f);
  rail[0][1] = V(cx + 12.f, base - 88.f);
  rail[1][0] = V(cx + 43.f, base + 6.f);
  rail[1][1] = V(cx + 20.f, base - 88.f);
}
constexpr int kRungs = 7;
inline float rungU(int k) { return 0.1f + 0.125f * fl(k); }
Vec2 rungEnd(const Vec2 rail[2][2], int side, int k) { return mixV(rail[side][0], rail[side][1], rungU(k)); }
Pixel ladderWood(const Palette& pal) { return mixRGB(pal[TRUNK], pal[ROCK], 0.45f); }
void drawLadder(Surface& sf, const Landscape& ls, const Palette& pal, const Rect& clip) {
  Vec2 r[2][2];
  ladderRails(ls, r);
  const Pixel wood = ladderWood(pal), lash = mixRGB(pal[ACTOR], pal[TRUNK], 0.3f);
  // The far rail first, then the rungs, then the near rail over their ends; the poles poke past the top rung.
  strokeCapsule(sf, r[1][0], r[1][1], 2.2f, mixRGB(wood, pal[ACTOR], 0.25f), clip);
  for (int k = 0; k < kRungs; ++k) strokeCapsule(sf, rungEnd(r, 0, k), rungEnd(r, 1, k), 1.8f, wood, clip);
  strokeCapsule(sf, r[0][0], r[0][1], 2.4f, wood, clip);
  for (int k = 0; k < kRungs; ++k)
    for (int side = 0; side < 2; ++side) fillCircle(sf, rungEnd(r, side, k), 1.1f, lash, clip);
}
Vec2 coilAt(const Landscape& ls) { return V(treeX(ls) + 56.f, treeBase(ls) + 10.f); }
void drawCoil(const World& w, Surface& sf, const LightLook& L, const Palette& pal, const Rect& clip) {
  const Vec2 c = coilAt(w.land);
  const Pixel cord = mixRGB(pal[ACTOR], pal[FOLIAGE_DARK], 0.35f);
  for (int ring = 0; ring < 3; ++ring) {
    const float rx = 8.f - 2.2f * fl(ring), ry = 2.6f - 0.6f * fl(ring);
    Vec2 p[13];
    for (int i = 0; i <= 12; ++i) {
      const float u = fl(i) / 12.f;
      p[i] = V(c.x + rx * cosT(u) + 0.7f * fl(ring), c.y - 1.2f * fl(ring) + ry * sinT(u));
    }
    polyline(sf, p, 13, 1.0f, cord, clip);
    for (int i = 0; i < 12; i += 3)
      drawBulb(sf, p[i + (ring & 1)], bulbHex(L, ring, i), false, 0.f, L.dark, pal, clip);
  }
  // The run up to his far hand.
  if (!w.hiker.visible) return;
  const Vec2 hand = hikerHandPos(w.hiker, 1);
  Vec2 p[9];
  for (int i = 0; i <= 8; ++i) p[i] = sagAt(V(c.x - 4.f, c.y - 3.f), hand, 6.f, fl(i) / 8.f);
  polyline(sf, p, 9, 0.9f, cord, clip);
  for (int i = 1; i < 8; i += 2) drawBulb(sf, p[i], bulbHex(L, 3, i), false, 0.f, L.dark, pal, clip);
}
Rect coilBounds(const World& w) {
  const Vec2 c = coilAt(w.land);
  Rect r = box(c.x - 9.f, c.y - 6.f, c.x + 11.f, c.y + 4.f, 2.f);
  if (w.hiker.visible) {
    const Vec2 hand = hikerHandPos(w.hiker, 1);
    unite(r, box(minf(hand.x, c.x - 4.f), minf(hand.y, c.y - 3.f), maxf(hand.x, c.x - 4.f), maxf(hand.y, c.y - 3.f) + 7.f, 3.f));
  }
  return r;
}

// ---- the generator ----------------------------------------------------------------------------------

float bankYAt(const Landscape& ls, float x) {
  float cy = 0.f, half = 0.f;
  if (!landscapeStreamSpan(ls, x, cy, half)) return fl(LS_GROUND_Y) + 40.f;
  return cy - half;
}
Vec2 hubOf(const Landscape& ls) {
  const float x = fl(LS_BOULDER_X) + 8.f;
  return V(x, floorf(bankYAt(ls, x)) + kWheelDip - kWheelR - kPaddle);
}
// The box on the bank, up the axle from the wheel: its middle and its base line.
Vec2 boxOf(const Landscape& ls) {
  const Vec2 h = hubOf(ls);
  const float x = h.x - 28.f;
  return V(x, floorf(bankYAt(ls, x)) - 7.f);
}
Vec2 wheelPt(Vec2 hub, float r, float turn) { return V(hub.x + kWheelSquash * r * cosT(turn), hub.y + r * sinT(turn)); }
Vec2 sparkFrom(const Landscape& ls) {
  const Vec2 b = boxOf(ls);
  return V(b.x + 0.5f * kBoxW - 2.f, b.y - kBoxH + 1.f);
}

void drawBox(const World& w, Surface& sf, const Rect& clip, const Palette& pal) {
  const Vec2 b = boxOf(w.land), h = hubOf(w.land);
  const float x0 = b.x - 0.5f * kBoxW, x1 = b.x + 0.5f * kBoxW, y1 = b.y, y0 = b.y - kBoxH;
  const Pixel wood = mixRGB(pal[TRUNK], pal[TENT], 0.18f), dark = mixRGB(wood, pal[ACTOR], 0.45f);
  // The axle from the box's side down to the hub (the wheel draws over its end).
  strokeCapsule(sf, V(x1 + 1.f, y0 + 4.f), h, 2.6f, mixRGB(pal[TRUNK], pal[ACTOR], 0.35f), clip);
  // A plank box with a lid, a darker side, a round housing where the axle goes in.
  const Vec2 side[4] = {V(x1, y0), V(x1 + 3.f, y0 - 2.5f), V(x1 + 3.f, y1 - 2.5f), V(x1, y1)};
  fillPolygon(sf, side, 4, dark, clip);
  const Vec2 lid[4] = {V(x0, y0), V(x0 + 3.f, y0 - 2.5f), V(x1 + 3.f, y0 - 2.5f), V(x1, y0)};
  fillPolygon(sf, lid, 4, mixRGB(wood, pal[CLOUD_LIT], 0.15f), clip);
  fillRectF(sf, x0, y0, x1, y1, wood, clip);
  strokeCapsule(sf, V(x0 + 0.5f, y0 + 0.5f * kBoxH), V(x1 - 0.5f, y0 + 0.5f * kBoxH), 0.7f, dark, clip);   // the planks
  fillCircle(sf, V(x1 + 1.5f, y0 + 4.f), 2.6f, mixRGB(pal[ROCK], pal[ACTOR], 0.35f), clip);
  // The wire leaves the far end along the ground (the rest is on the tree's layer).
  strokeCapsule(sf, V(x0 + 1.f, y1 - 2.f), V(x0 - 3.f, y1 - 1.f), 1.3f, mixRGB(pal[ACTOR], pal[TRUNK], 0.2f), clip);
}

void drawWheel(const World& w, Surface& sf, const Rect& clip, const Palette& pal) {
  const XmasMock& m = w.xmas.m;
  const Landscape& ls = w.land;
  const Vec2 h = hubOf(ls);
  const float turn = fl(m.wheel) / 64.f;
  const Pixel wood = mixRGB(pal[TRUNK], pal[ROCK], 0.35f);
  const Pixel board = mixRGB(mixRGB(pal[TRUNK], pal[TENT_LIT], 0.6f), pal[CLOUD_LIT], 0.12f);   // fresh-cut boards
  const Pixel lash = mixRGB(pal[ACTOR], pal[TRUNK], 0.3f);
  // Two rims of bent sticks with boards between them (seen a little from the side, so each board
  // shows its width back toward the bank); the near rim's spokes are four crossed sticks, lashed.
  const Vec2 D = V(kDepthX, kDepthY);
  const Pixel back = mixRGB(wood, pal[ACTOR], 0.4f), board_side = mixRGB(board, pal[TRUNK], 0.3f);
  Vec2 rim[21];
  for (int i = 0; i <= 20; ++i) rim[i] = addV(wheelPt(h, kWheelR, fl(i) / 20.f), D);
  polyline(sf, rim, 21, 1.8f, back, clip);
  for (int i = 0; i < kPaddles; ++i) {
    const float u = turn + fl(i) / fl(kPaddles);
    const Vec2 a = wheelPt(h, kWheelR - 2.f, u), b = wheelPt(h, kWheelR + kPaddle, u);
    const Vec2 q[4] = {a, b, addV(b, D), addV(a, D)};
    fillPolygon(sf, q, 4, board_side, clip);
    strokeCapsule(sf, a, b, 1.8f, mixRGB(board, pal[CLOUD_LIT], 0.2f), clip);   // its near edge catches the light
  }
  for (int i = 0; i < kSpokes / 2; ++i) {
    const float u = turn + 0.5f / fl(kPaddles) + fl(i) / fl(kSpokes);
    strokeCapsule(sf, wheelPt(h, kWheelR, u), wheelPt(h, kWheelR, u + 0.5f), 1.6f, wood, clip);
  }
  for (int i = 0; i <= 20; ++i) rim[i] = wheelPt(h, kWheelR, fl(i) / 20.f);
  polyline(sf, rim, 21, 2.0f, wood, clip);
  for (int i = 0; i < kSpokes; ++i) fillCircle(sf, wheelPt(h, kWheelR, turn + 0.5f / fl(kPaddles) + fl(i) / fl(kSpokes)), 1.1f, lash, clip);
  fillCircle(sf, h, 3.0f, mixRGB(pal[TRUNK], pal[ACTOR], 0.3f), clip);
  fillCircle(sf, h, 1.2f, lash, clip);
  // The forked stick in front that carries the axle's near end, standing in the water.
  const float wy0 = bankYAt(ls, h.x);
  const Pixel post = mixRGB(pal[TRUNK], pal[ACTOR], 0.15f);
  strokeCapsule(sf, V(h.x + 1.5f, wy0 + 7.f), V(h.x + 1.f, h.y + 4.5f), 2.4f, post, clip);
  strokeCapsule(sf, V(h.x + 1.f, h.y + 4.5f), V(h.x - 2.2f, h.y + 0.5f), 1.6f, post, clip);
  strokeCapsule(sf, V(h.x + 1.f, h.y + 4.5f), V(h.x + 4.2f, h.y + 0.8f), 1.6f, post, clip);
  // The water over the part under it, then the splash where the paddles go in (upstream, right) and
  // come up (downstream, left, throwing drops).
  const float wy = bankYAt(ls, h.x);
  const Rect under{(int)floorf(h.x - kWheelR - kPaddle - 2.f), (int)ceilf(wy) + 1, (int)ceilf(h.x + kWheelR + kPaddle + 2.f),
                   (int)ceilf(h.y + kWheelR + kPaddle + 2.f)};
  worldDrawWater(w, sf, clip.clipped(under), pal);
  const Pixel foam = pal[WATER_LIGHT];
  const float ex = kWheelSquash * (kWheelR + 1.f);
  fillEllipse(sf, V(h.x + ex, wy + 2.6f), 3.2f, 1.2f, foam, clip);
  fillEllipse(sf, V(h.x - ex, wy + 2.4f), 4.2f, 1.5f, foam, clip);
  strokeCapsule(sf, V(h.x - ex - 6.f, wy + 4.5f), V(h.x - ex - 14.f, wy + 5.5f), 1.0f, mixRGB(foam, pal[WATER], 0.4f), clip);
  for (int i = 0; i < 3; ++i) {
    const float u = frac(turn * 4.f + 0.33f * fl(i));
    const Vec2 d = V(h.x - ex - 1.f - 3.f * u - fl(i), wy - 1.f - 7.f * sinT(0.5f * u) - fl(i));
    fillCircle(sf, d, 0.8f, foam, clip);
  }
}

// A spark burst off the box (the blaze sparks' look, ambient.cpp blazeSpark): step 1..8, 0.06 s each.
constexpr int kSparks = 10;
bool sparkAt(const Landscape& ls, int step, int i, Vec2& c, float& r, bool& hot) {
  const float t = 0.06f * fl(step);
  const float life = 0.3f + 0.22f * frac(0.71f * fl(i) + 0.13f);
  if (t > life) return false;
  const float ang = -0.18f + 0.36f * frac(0.618f * fl(i) + 0.05f);   // turns from straight up
  const float v = 40.f + 32.f * frac(0.37f * fl(i) + 0.2f);
  const Vec2 o = sparkFrom(ls);
  c = V(o.x + sinT(ang) * v * t, o.y - cosT(ang) * v * t + 0.5f * 110.f * t * t);
  hot = t < 0.55f * life;
  r = hot ? 1.7f : 1.2f;
  return true;
}
void drawSparks(const World& w, Surface& sf, const Rect& clip, const Palette& pal) {
  const int step = w.xmas.m.spark;
  if (step <= 0) return;
  const Vec2 o = sparkFrom(w.land);
  if (step <= 2) {   // the crack: a flash at the box
    softDisc(sf, o, 7.f, 5.f, pal[FIRE], 60.f, clip);
    fillCircle(sf, o, 2.8f - 0.8f * fl(step), pal[FIRE_CORE], clip);
  }
  for (int i = 0; i < kSparks; ++i) {
    Vec2 c;
    float r;
    bool hot;
    if (sparkAt(w.land, step, i, c, r, hot)) fillCircle(sf, c, r, hot ? pal[FIRE_CORE] : pal[FIRE], clip);
  }
}

// The wire on the ground from the box to the trunk's foot, then up the trunk into the boughs.
void wirePts(const Landscape& ls, Vec2* p) {
  const Vec2 b = boxOf(ls);
  const Vec2 a = V(b.x - 0.5f * kBoxW - 3.f, b.y - 1.f);
  const Vec2 t = V(treeX(ls) + 4.f, treeBase(ls) + 1.f);
  for (int i = 0; i < 8; ++i) {
    const float u = fl(i) / 7.f;
    const Vec2 q = mixV(a, t, u);
    p[i] = V(q.x, q.y + 3.5f * sinT(0.5f * u) + 1.2f * sinT(1.5f * u));   // lying in a lazy curve
  }
  p[8] = V(treeX(ls) + 4.f, 320.f);
}
void drawWire(const World& w, Surface& sf, const Rect& clip, const Palette& pal) {
  Vec2 p[9];
  wirePts(w.land, p);
  polyline(sf, p, 9, 1.3f, mixRGB(pal[ACTOR], pal[TRUNK], 0.2f), clip);
}

// ---- the critters (built on the stack from the mock: nothing of theirs is stored) ----------------

Critter bird(Vec2 feet, int facing, float wing, float pitch, bool flying) {
  Critter c;
  critterInit(c, CR_BIRD, feet, facing);
  c.tone = kToneCardinal;
  c.scale = kBirdScale;
  c.pose = critterPoseDef(CR_BIRD, flying ? CA_MOVE : CA_IDLE);
  if (flying) {
    c.pose.fore[0] = wing;
    c.pose.lift = 1.f;
  }
  c.pose.pitch = pitch;
  return c;
}
// Its beak tip (buildBird's head frame, unpitched: a few px either way does not show).
Vec2 beakOf(const Critter& c, bool flying) {
  const float S = c.scale;
  return V(c.feet.x + fl(c.facing) * S * 9.0f, c.feet.y + S * (flying ? -5.9f : -4.4f));
}

Critter raccoon(Vec2 feet, int facing, CritterAction a) {
  Critter c;
  critterInit(c, CR_RACCOON, feet, facing);
  c.tone = kToneMasked;
  c.scale = kCoonScale;
  c.pose = critterPoseDef(CR_RACCOON, a);
  return c;
}

Vec2 bushAt(const Landscape& ls) {
  const float x = 352.f;
  float cy = 0.f, half = 0.f;
  if (!landscapeStreamSpan(ls, x, cy, half)) return V(x, 462.f);
  return V(x, minf(cy + half + 30.f, 474.f));
}

// The posed critters and what they hold. `out` gets each critter (up to 2) for drawing or bounds.
struct Cast {
  int n;
  Critter c[2];
  // A garland run held between beaks or hanging (up to 2 runs of 9 points), a carried ornament.
  int runs;
  Vec2 run[2][9];
  bool ball;
  Vec2 ball_at;
  bool ball_gold;
};

Vec2 bez(Vec2 a, Vec2 c, Vec2 b, float u) {
  const float v = 1.f - u;
  return V(v * v * a.x + 2.f * v * u * c.x + u * u * b.x, v * v * a.y + 2.f * v * u * c.y + u * u * b.y);
}

void castOf(const World& w, Cast& k) {
  const XmasMock& m = w.xmas.m;
  const Landscape& ls = w.land;
  k.n = 0;
  k.runs = 0;
  k.ball = false;
  const float cx = treeX(ls);
  if (m.garland == XG_FLYING) {   // the pair flying in from the right, the garland slung between their beaks
    const Vec2 a = V(fl(SS_WIDTH) - 30.f, 112.f), b = V(cx + 34.f, treeTop(ls) + 40.f);
    const Vec2 ctl = V(lerp(a.x, b.x, 0.45f), minf(a.y, b.y) - 24.f);
    const float u = fl(m.fly) / 255.f;
    const Vec2 p0 = bez(a, ctl, b, u), p1 = bez(a, ctl, b, minf(1.f, u + 0.22f));
    k.c[0] = bird(p1, -1, 1.35f, 0.08f, true);   // the leader, wings up
    k.c[1] = bird(p0, -1, 0.35f, 0.02f, true);   // wings down
    k.n = 2;
    const Vec2 e0 = beakOf(k.c[1], true), e1 = beakOf(k.c[0], true);
    for (int i = 0; i <= 8; ++i) k.run[0][i] = sagAt(e0, V(e1.x + 4.f, e1.y + 1.f), 16.f, fl(i) / 8.f);
    // The rest trails from the back bird's beak in a loose hang.
    for (int i = 0; i <= 8; ++i) {
      const float t = fl(i) / 8.f;
      k.run[1][i] = V(e0.x + 5.f * t + 2.f * sinT(0.5f * t), e0.y + 15.f * t - 4.f * t * t);
    }
    k.runs = 2;
  } else if (m.garland == XG_TANGLED) {   // one wound up in it off the right of the tree, one perched watching
    const TierG t2 = tierOf(ls, 2);
    const Vec2 f = V(cx + t2.hw + 9.f, t2.base + 7.f);
    k.c[0] = bird(f, -1, 0.5f, 0.55f, true);
    k.c[0].pose.lift = 0.f;
    k.c[0].pose.head = 0.4f;
    const TierG t0 = tierOf(ls, 0);
    k.c[1] = bird(V(cx - 6.f, t0.base + 2.f), 1, 0.f, 0.f, false);
    k.c[1].pose = critterPoseDef(CR_BIRD, CA_LOOK);
    k.n = 2;
    // The garland from the first tier's right bough round and round the bird.
    const Vec2 a = scallopEnd(ls, 1, 0.94f);
    for (int i = 0; i <= 8; ++i) {
      const float u = fl(i) / 8.f;
      const Vec2 q = sagAt(a, V(f.x - 4.f, f.y - 9.f), 5.f, u);
      k.run[0][i] = q;
    }
    for (int i = 0; i <= 8; ++i) {   // the loop round its middle, ending in a dangling end
      const float u = fl(i) / 8.f;
      if (i < 6) k.run[1][i] = V(f.x + 7.f * cosT(0.1f + 0.95f * u * 1.2f) - 1.f, f.y - 7.f + 4.5f * sinT(0.1f + 0.95f * u * 1.2f));
      else k.run[1][i] = V(f.x - 2.f + 1.5f * fl(i - 6), f.y - 2.f + 6.f * fl(i - 5));
    }
    k.run[1][0] = k.run[0][8];
    k.runs = 2;
  } else if (m.garland == XG_PART) {   // two tiers done; one bird pulls the free end round to the third
    const TierG t3 = tierOf(ls, 3);
    const Vec2 f = V(cx + t3.hw + 16.f, t3.base - 6.f);
    k.c[0] = bird(f, -1, 1.2f, 0.12f, true);
    k.n = 1;
    const Vec2 a = scallopEnd(ls, 2, 0.94f), e = beakOf(k.c[0], true);
    for (int i = 0; i <= 8; ++i) k.run[0][i] = sagAt(a, e, 6.f, fl(i) / 8.f);
    k.runs = 1;
  }
  if (m.coon == XR_REACH) {   // up on its hind legs under the low ornament, a paw to it
    const Vec2 o = ornBall(ls, XMAS_LOW_ORN);
    Critter c = raccoon(V(o.x - 8.f, treeBase(ls) + 7.f), 1, CA_REACH);
    const Vec2 paw = critterPaw(c);
    c.feet.x += o.x - 1.f - paw.x;
    k.c[k.n++] = c;
  } else if (m.coon == XR_CARRY) {   // waddling off toward the stream, the ball in its mouth
    Critter c = raccoon(V(cx - 92.f, treeBase(ls) + 30.f), -1, CA_MOVE);
    c.pose.fore[0] = 0.45f;
    c.pose.hind[0] = -0.4f;
    c.pose.head = 0.1f;
    c.pose.tail = 0.25f;
    k.c[k.n++] = c;
    const Vec2 mo = critterMouth(c);
    k.ball = true;
    k.ball_at = V(mo.x - 1.5f, mo.y + 2.4f);
    k.ball_gold = kOrn[XMAS_LOW_ORN].gold != 0;
  } else if (m.coon == XR_PROUD) {   // sitting up by its own bush, admiring it
    const Vec2 b = bushAt(ls);
    k.c[k.n++] = raccoon(V(b.x + 34.f, b.y - 2.f), -1, CA_SIT);
  }
}

int16_t castSortY(const World& w) {
  const XmasMock& m = w.xmas.m;
  if (m.coon != XR_NONE) {
    Cast k;
    castOf(w, k);
    return (int16_t)floorf(k.c[k.n - 1].feet.y);
  }
  return (int16_t)(w.land.camp.base_y + 1);
}

// ---- the bush -------------------------------------------------------------------------------------

void drawBush(const World& w, Surface& sf, const Rect& clip, const Palette& pal, float lit) {
  const Vec2 b = bushAt(w.land);
  const Pixel dark = pal[FOLIAGE_DARK], mid = pal[FOLIAGE], hi = mixRGB(pal[FOLIAGE], pal[GRASS], 0.5f);
  fillEllipse(sf, V(b.x, b.y - 11.f), 20.f, 12.f, dark, clip);
  fillEllipse(sf, V(b.x - 8.f, b.y - 16.f), 11.f, 9.f, mid, clip);
  fillEllipse(sf, V(b.x + 8.f, b.y - 18.f), 11.f, 10.f, mid, clip);
  fillEllipse(sf, V(b.x + 1.f, b.y - 23.f), 8.f, 6.f, hi, clip);
  fillEllipse(sf, V(b.x, b.y - 2.f), 19.f, 2.8f, mixRGB(dark, pal[ACTOR], 0.3f), clip);
  static const float kAt[4][2] = {{-10.f, -12.f}, {6.f, -20.f}, {11.f, -9.f}, {-3.f, -25.f}};
  const int n = w.xmas.m.bush > 4 ? 4 : w.xmas.m.bush;
  for (int i = 0; i < n; ++i) drawBall(sf, V(b.x + kAt[i][0], b.y + kAt[i][1]), (i & 1) != 0, lit, pal, clip);
}
Rect bushBounds(const World& w) {
  if (w.xmas.m.bush == 0) return kNone;
  const Vec2 b = bushAt(w.land);
  return box(b.x - 21.f, b.y - 31.f, b.x + 21.f, b.y + 1.f);
}

// ---- the presents, the stocking, the sleigh ------------------------------------------------------

void giftBox(Surface& sf, float x, float y, float wd, float ht, Pixel paper, Pixel ribbon, const Palette& pal, const Rect& clip) {
  const Pixel side = mixRGB(paper, pal[ACTOR], 0.4f), top = mixRGB(paper, pal[CLOUD_LIT], 0.12f);
  const float x0 = x - 0.5f * wd, x1 = x + 0.5f * wd, y0 = y - ht;
  const Vec2 sp[4] = {V(x1, y0), V(x1 + 3.f, y0 - 2.f), V(x1 + 3.f, y - 2.f), V(x1, y)};
  fillPolygon(sf, sp, 4, side, clip);
  const Vec2 tp[4] = {V(x0, y0), V(x0 + 3.f, y0 - 2.f), V(x1 + 3.f, y0 - 2.f), V(x1, y0)};
  fillPolygon(sf, tp, 4, top, clip);
  fillRectF(sf, x0, y0, x1, y, paper, clip);
  fillRectF(sf, x - 0.9f, y0, x + 0.9f, y, ribbon, clip);
  strokeCapsule(sf, V(x + 0.5f, y0 - 1.f), V(x + 2.5f, y0 - 1.f), 1.4f, ribbon, clip);
  fillEllipse(sf, V(x - 0.6f, y0 - 1.9f), 2.1f, 1.3f, ribbon, clip);   // the bow
  fillEllipse(sf, V(x + 3.4f, y0 - 1.9f), 2.1f, 1.3f, ribbon, clip);
}
// A bone-shaped parcel: a shaft with two knobs at each end, a ribbon round the middle and a bow.
void boneGift(Surface& sf, Vec2 c, float len, float th, Pixel paper, Pixel ribbon, const Rect& clip) {
  const float hl = 0.5f * len;
  strokeCapsule(sf, V(c.x - hl, c.y), V(c.x + hl, c.y), th, paper, clip);
  for (int e = -1; e <= 1; e += 2)
    for (int k = -1; k <= 1; k += 2) fillCircle(sf, V(c.x + fl(e) * hl, c.y + fl(k) * 0.42f * th), 0.58f * th, paper, clip);
  fillRectF(sf, c.x - 0.9f, c.y - 0.5f * th - 0.3f, c.x + 0.9f, c.y + 0.5f * th + 0.3f, ribbon, clip);
  fillEllipse(sf, V(c.x - 2.1f, c.y - 0.5f * th - 1.1f), 2.0f, 1.2f, ribbon, clip);
  fillEllipse(sf, V(c.x + 2.1f, c.y - 0.5f * th - 1.1f), 2.0f, 1.2f, ribbon, clip);
}
Vec2 giftsAt(const Landscape& ls) { return V(treeX(ls), treeBase(ls) + 10.f); }
void drawPresents(const World& w, Surface& sf, const Rect& clip, const Palette& pal) {
  const Vec2 g = giftsAt(w.land);
  const Pixel gold = critterLitAs(pal, kRibbonGold, MUSHROOM);
  giftBox(sf, g.x - 26.f, g.y - 3.f, 15.f, 11.f, critterLitAs(pal, kPaperRed, MUSHROOM), gold, pal, clip);
  giftBox(sf, g.x + 14.f, g.y - 4.f, 12.f, 13.f, critterLitAs(pal, kPaperGreen, MUSHROOM), critterLitAs(pal, kPaperRed, MUSHROOM), pal, clip);
  giftBox(sf, g.x - 8.f, g.y + 1.f, 11.f, 8.f, critterLitAs(pal, kPaperGold, MUSHROOM), critterLitAs(pal, kPaperRed, MUSHROOM), pal, clip);
  // Lucy's: a big bone in her black with a white ribbon, in front. Copper's: a small one in his golden copper.
  const Pixel lucy = mixRGB(pal[ACTOR], pal[TRUNK], 0.3f), white = mixRGB(pal[WATER_LIGHT], pal[TENT_LIT], 0.15f);
  boneGift(sf, V(g.x + 4.f, g.y + 9.f), 21.f, 5.4f, lucy, white, clip);
  const Pixel copper = mixRGB(mixRGB(pal[TENT], pal[TENT_LIT], 0.34f), pal[TRUNK], 0.25f);
  boneGift(sf, V(g.x - 42.f, g.y + 6.f), 13.f, 3.8f, copper, critterLitAs(pal, kPaperRed, MUSHROOM), clip);
}
Rect presentsBounds(const World& w) {
  const Vec2 g = giftsAt(w.land);
  return box(g.x - 53.f, g.y - 19.f, g.x + 29.f, g.y + 15.f);
}

bool stockingShown(const World& w) {
  return (w.xmas.m.extras & XE_STOCKING) && w.tent.state == TENT_UP && w.tent.progress >= 1.f;
}
// Hung by a loop from the front pole's top, over the door: leg, heel, toe toward the door's middle.
void stockingPts(const World& w, Vec2* p, Vec2* cuff, Vec2& hook) {
  const Vec2 fa = tentRidgeEnd(w.tent, 1);
  hook = V(fa.x + 1.5f, fa.y + 2.f);
  const float x = hook.x + 1.f, y = hook.y + 4.f;
  static const float kSock[7][2] = {{-2.8f, 0.f}, {2.8f, 0.f}, {2.8f, 8.f}, {6.8f, 9.4f}, {6.8f, 12.6f}, {1.0f, 13.f}, {-2.8f, 10.6f}};
  constexpr float k = 1.3f;
  for (int i = 0; i < 7; ++i) p[i] = V(x + k * kSock[i][0], y + k * kSock[i][1]);
  cuff[0] = V(x - k * 3.3f, y - k * 1.f);
  cuff[1] = V(x + k * 3.3f, y + k * 2.6f);
}
void drawStocking(const World& w, Surface& sf, const Rect& clip, const Palette& pal) {
  Vec2 p[7], c[2], hook;
  stockingPts(w, p, c, hook);
  strokeCapsule(sf, hook, V(p[0].x + 2.8f, p[0].y - 1.f), 0.8f, mixRGB(pal[ACTOR], pal[TRUNK], 0.3f), clip);
  fillPolygon(sf, p, 7, critterLitAs(pal, kStockRed, MUSHROOM), clip);
  fillRectF(sf, c[0].x, c[0].y, c[1].x, c[1].y, mixRGB(pal[WATER_LIGHT], pal[TENT_LIT], 0.15f), clip);
}
Rect stockingBounds(const World& w) {
  Vec2 p[7], c[2], hook;
  stockingPts(w, p, c, hook);
  return box(c[0].x, hook.y, p[3].x, p[5].y);
}

// The sleigh and three reindeer, a silhouette climbing left across the moon.
constexpr float kMoonX = 640.f, kMoonY = 110.f;
constexpr float kSleighK = 1.4f;   // the sleigh and team's size (1: the first mockup's)
Vec2 sleighXf(float x, float y) {   // local (forward +x, y down) to frame: facing left, climbing a little
  const float c = 0.985f * kSleighK, s = 0.17f * kSleighK;
  const float lx = x - 9.f, ly = y + 4.f;
  return V(kMoonX - (lx * c + ly * s), kMoonY + (ly * c - lx * s));
}
void drawSleigh(Surface& sf, const Rect& clip, const Palette& pal) {
  const Pixel ink = pal[ACTOR];
  // The sleigh: a curled runner, the box with its high back, him and the sack.
  Vec2 runner[6] = {sleighXf(-7.f, 0.f), sleighXf(2.f, 0.4f), sleighXf(6.5f, 0.f), sleighXf(8.6f, -1.6f), sleighXf(8.4f, -3.2f), sleighXf(7.f, -3.4f)};
  polyline(sf, runner, 6, 0.9f, ink, clip);
  const Vec2 body[6] = {sleighXf(-6.f, -1.5f), sleighXf(5.f, -1.5f), sleighXf(6.4f, -4.4f), sleighXf(-2.f, -4.4f), sleighXf(-4.4f, -8.6f), sleighXf(-7.2f, -8.4f)};
  fillPolygon(sf, body, 6, ink, clip);
  fillCircle(sf, sleighXf(0.6f, -6.6f), 2.4f, ink, clip);   // him
  fillCircle(sf, sleighXf(1.0f, -9.6f), 1.5f, ink, clip);
  const Vec2 hat[3] = {sleighXf(-0.5f, -10.5f), sleighXf(2.4f, -10.6f), sleighXf(-1.2f, -12.8f)};
  fillPolygon(sf, hat, 3, ink, clip);
  fillCircle(sf, sleighXf(-3.6f, -6.8f), 2.3f, ink, clip);   // the sack
  // The reins, then the team, leaping.
  strokeCapsule(sf, sleighXf(4.f, -6.f), sleighXf(39.f, -8.4f), 0.5f, ink, clip);
  for (int r = 0; r < 3; ++r) {
    const float x = 14.f + 9.5f * fl(r), y = -2.f - 1.2f * fl(r);
    auto P = [&](float dx, float dy) { return sleighXf(x + dx, y + dy); };
    fillEllipse(sf, P(0.f, -4.6f), 3.4f, 1.7f, ink, clip);
    strokeCapsule(sf, P(2.2f, -4.2f), P(4.8f, -2.6f), 0.8f, ink, clip);    // fore legs reaching
    strokeCapsule(sf, P(1.4f, -4.0f), P(3.4f, -1.8f), 0.8f, ink, clip);
    strokeCapsule(sf, P(-2.2f, -4.2f), P(-5.0f, -3.0f), 0.8f, ink, clip);  // hind legs kicked back
    strokeCapsule(sf, P(-1.6f, -4.0f), P(-4.2f, -2.2f), 0.8f, ink, clip);
    strokeCapsule(sf, P(2.4f, -5.4f), P(3.8f, -8.0f), 1.3f, ink, clip);    // neck
    fillEllipse(sf, P(4.6f, -8.2f), 1.4f, 0.9f, ink, clip);
    strokeCapsule(sf, P(3.6f, -8.8f), P(2.8f, -11.2f), 0.6f, ink, clip);   // antlers
    strokeCapsule(sf, P(3.1f, -10.4f), P(4.4f, -11.4f), 0.6f, ink, clip);
    strokeCapsule(sf, P(3.0f, -10.0f), P(1.7f, -10.6f), 0.6f, ink, clip);
  }
}
Rect sleighBounds() {
  Rect r = kNone;
  const float pts[4][2] = {{-8.f, 1.f}, {-8.f, -14.f}, {44.f, 1.f}, {44.f, -16.f}};
  for (const auto& q : pts) {
    const Vec2 v = sleighXf(q[0], q[1]);
    unite(r, box(v.x, v.y, v.x, v.y, 3.f));
  }
  return r;
}

bool treeOn(const XmasMock& m) {
  return m.wraps || m.garland == XG_TANGLED || m.garland == XG_PART || m.garland == XG_DRAPED || m.orns || m.gen ||
         (m.extras & (XE_STAR | XE_LADDER | XE_COIL | XE_GAP));
}
bool castOn(const XmasMock& m) {
  return m.garland == XG_FLYING || m.garland == XG_TANGLED || m.garland == XG_PART || m.coon != XR_NONE;
}

LightLook lookOf(const World& w, const Palette& pal) {
  const XmasMock& m = w.xmas.m;
  return LightLook{darkOf(pal), powerLevel(m), m.look == XL_MULTI};
}
// How much light from the strings falls on things near the tree (the ornaments, the garland, the star).
float treeLight(const World& w, const LightLook& L) {
  int lit = 0;
  for (int k = 0; k < XMAS_WRAPS; ++k)
    if (wrapLit(w.xmas.m, k)) ++lit;
  return L.dark * L.level * (fl(lit) / fl(XMAS_WRAPS));
}


// ---- the snow (XE_SNOW) ------------------------------------------------------------------------------
//
// Fire and ice's frost pass (fireice.cpp fireIceDrawStatic) recolours the cached ground pixel by pixel
// where they are exactly the ground's slots; the snow does the same, whiter and softer and everywhere,
// then lays snow on the pines' tiers (the shelf along each tier's lower edge, a cap on the top), on the
// far tree line's teeth, caps on the rocks and drifts at their feet and at the camp pine's. All of it in
// the static cache: no cost per frame. The pieces that are drawn per frame anyway (the boulder, the
// tent, the generator's box, a foreground pine over an actor) get theirs where they are drawn.

Pixel snowCol(const Palette& pal) { return mixRGB(pal[WATER_LIGHT], pal[CLOUD_LIT], 0.15f); }
Pixel snowShade(const Palette& pal) { return mixRGB(snowCol(pal), pal[CLOUD_SHADE], 0.4f); }
Pixel frostCol(const Palette& pal) { return mixRGB(pal[GRASS], snowCol(pal), 0.5f); }   // grass standing up through it

inline uint32_t hashXY(int x, int y) {
  uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u;
  h ^= h >> 13;
  h *= 0x9E3779B1u;
  return h ^ (h >> 16);
}

// A pine as drawPine (landscape.cpp) draws it grown: keep in step.
struct PineG {
  float cx, top, fbot, wh, pct;
  int n;
};
PineG pineOf(const LsPine& p, float trunk_frac) {
  const float h = fl(p.h);
  return PineG{fl(p.x), fl(p.base_y) - h, fl(p.base_y) - maxf(4.f, h * trunk_frac), h, fl(p.width_pct), p.tiers};
}
PineG fgPine(const LsPine& p) { return pineOf(p, 0.28f); }
TierG pineTier(const PineG& g, int i) {
  const float F = g.fbot - g.top, th = 0.42f * F;
  const float sp = g.n > 1 ? (F - th) / fl(g.n - 1) : 0.f;
  TierG t;
  t.apex = g.top + fl(i) * sp;
  t.base = t.apex + th;
  t.hw = 0.5f * (g.pct * 0.01f) * g.wh * (1.f - 0.2f * fl(g.n - 1 - i));
  return t;
}
// Snow on each tier: a shelf along its lower edge with lumps hanging a little over, and a cap on top.
void pineSnow(Surface& s, const PineG& g, Pixel snow, const Rect& clip) {
  for (int i = 0; i < g.n; ++i) {
    const TierG t = pineTier(g, i);
    const float th = t.base - t.apex;
    const float tt = maxf(1.3f, 0.1f * th);
    const float y0 = t.base - tt, w0 = t.hw * (1.f - tt / th) * 0.98f;
    Vec2 p[12];
    int n = 0;
    p[n++] = V(g.cx - w0, y0);
    p[n++] = V(g.cx + w0, y0);
    const float lump = maxf(0.6f, 0.035f * th);
    for (int j = 0; j <= 8; ++j) {
      const float u = fl(j) / 8.f;
      p[n++] = V(g.cx + t.hw * 0.99f * (1.f - 2.f * u), t.base + ((j & 1) ? lump : -0.2f * lump));
    }
    fillPolygon(s, p, n, snow, clip);
    if (i == 0) {   // the top: a cap with a wavy lower edge
      const float cy = t.apex + 0.28f * th, cw = t.hw * 0.28f;
      const Vec2 q[6] = {V(g.cx, t.apex - 0.3f), V(g.cx + cw + 0.4f, cy), V(g.cx + 0.4f * cw, cy - 0.8f),
                         V(g.cx, cy + 0.6f), V(g.cx - 0.4f * cw, cy - 0.8f), V(g.cx - cw - 0.4f, cy)};
      fillPolygon(s, q, 6, snow, clip);
    }
  }
}

// A rock's cap in vertical slices, each cut at its own depth, so the edge is uneven; a drift at its foot.
void rockSnow(Surface& s, const LsRock& r, int seed, Pixel snow, const Rect& clip) {
  float x0 = r.p[0].x, x1 = x0, y0 = r.p[0].y, y1 = y0;
  for (const Vec2& q : r.p) {
    x0 = minf(x0, q.x);
    x1 = maxf(x1, q.x);
    y0 = minf(y0, q.y);
    y1 = maxf(y1, q.y);
  }
  const int slices = 5;
  for (int k = 0; k < slices; ++k) {
    const float a = lerp(x0, x1, fl(k) / fl(slices)), b = lerp(x0, x1, fl(k + 1) / fl(slices));
    const float cut = y0 + (0.34f + 0.1f * fl((int)(hashXY(seed, k) & 3u))) * (y1 - y0);
    const Rect c = clip.clipped(Rect{(int)floorf(a), (int)floorf(y0) - 1, (int)ceilf(b), (int)ceilf(cut)});
    if (!c.empty()) fillPolygon(s, r.p, 6, snow, c);
  }
  fillEllipse(s, V(0.5f * (x0 + x1), y1 - 0.5f), 0.62f * (x1 - x0), 2.4f, snow, clip);
}

// The antialiased edge between two of the ground's colours (a, b): if `p` is a blend of them, the same
// blend of their snow colours (sa, sb) in `out`. Keeps the bank's edges from showing as a dotted line.
bool edgeBlend(Pixel p, Pixel a, Pixel b, Pixel sa, Pixel sb, Pixel& out) {
  const int pr = red8(p), pg = green8(p), pb = blue8(p);
  const int ar = red8(a), ag = green8(a), ab = blue8(a), br = red8(b), bg = green8(b), bb = blue8(b);
  const int dr = br - ar, dg = bg - ag, db = bb - ab;
  const int len2 = dr * dr + dg * dg + db * db;
  if (len2 < 48) return false;
  const float t = (float)((pr - ar) * dr + (pg - ag) * dg + (pb - ab) * db) / (float)len2;
  if (t <= 0.f || t >= 1.f) return false;
  const float er = (float)pr - ((float)ar + t * (float)dr), eg = (float)pg - ((float)ag + t * (float)dg),
              eb = (float)pb - ((float)ab + t * (float)db);
  if (er * er + eg * eg + eb * eb > 24.f) return false;
  out = mixRGB(sa, sb, t);
  return true;
}

void snowStatic(const World& w, Surface& frame, const Palette& pal) {
  const Landscape& ls = w.land;
  const int y0 = frame.origin_y, y1 = frame.origin_y + frame.height;
  const Rect clip{0, y0, SS_WIDTH, y1};
  const Pixel snow = snowCol(pal), shade = snowShade(pal);
  const Pixel streak = mixRGB(snow, shade, 0.35f), sparkle = mixRGB(snow, pal[STAR], 0.45f);
  // The ground: GROUND to snow, its shadow side to the snow's shadow, faint drift lines, a glint here
  // and there; the cached tufts frosted (the swaying ones too: xmasTuftPal).
  const Pixel ground = pal[GROUND], dark = pal[GROUND_DARK], grass = pal[GRASS], frost = frostCol(pal), water = pal[WATER];
  const int ya = y0 > LS_GROUND_Y ? y0 : LS_GROUND_Y;
  for (int y = ya; y < y1; ++y) {
    Pixel* row = frame.row(y);
    for (int x = 0; x < SS_WIDTH; ++x) {
      const Pixel p = row[x];
      // Near the stream's banks only (the bank strip's edges and the waterline) are blends recoloured.
      float cy = 0.f, half = 0.f;
      const bool bank = landscapeStreamSpan(ls, (float)x, cy, half) && (float)y > cy - half - 20.f && (float)y < cy + half + 3.f;
      if (p == ground) {
        const uint32_t h = hashXY(x / 14, y / 3);
        row[x] = (hashXY(x, y) & 127u) == 0 ? sparkle : ((h & 7u) == 0 ? streak : snow);
      } else if (p == dark) {
        row[x] = shade;
      } else if (p == grass) {
        row[x] = frost;
      } else if (bank) {
        Pixel q;
        if (edgeBlend(p, ground, dark, snow, shade, q) || edgeBlend(p, ground, water, snow, water, q) ||
            edgeBlend(p, dark, water, shade, water, q) || edgeBlend(p, ground, grass, snow, frost, q))
          row[x] = q;
      }
    }
  }
  // The far tree line's teeth: white tips.
  const Pixel far = mixRGB(snow, pal[TREES_FAR], 0.35f);
  for (int j = 0; j + 1 < ls.n_spikes; ++j) {
    if (ls.spikes[j].h == 0) continue;
    const float xa = fl(ls.spikes[j].x), xb = fl(ls.spikes[j + 1].x), xm = 0.5f * (xa + xb);
    const Vec2 tip = V(xm, landscapeCrestAt(ls, 2, xm) - fl(ls.spikes[j].h));
    const Vec2 a = mixV(tip, V(xa, landscapeCrestAt(ls, 2, xa)), 0.36f), b = mixV(tip, V(xb, landscapeCrestAt(ls, 2, xb)), 0.36f);
    const Vec2 q[4] = {tip, b, V(xm, lerp(tip.y, landscapeCrestAt(ls, 2, xm), 0.24f)), a};
    fillPolygon(frame, q, 4, far, clip);
  }
  // The mid pines, a little hazed with distance; the camp pine; the foreground pines.
  const Pixel mid = mixRGB(snow, pal[TREES_MID], 0.22f);
  for (int i = 0; i < ls.n_mid; ++i) pineSnow(frame, pineOf(ls.mid[i], 0.12f), mid, clip);
  {
    const LsPine& p = ls.camp;   // landscape.cpp drawCampPine: its lowest tier's base is the ground's line
    pineSnow(frame, PineG{fl(p.x), fl(p.base_y) - fl(p.h), (float)LS_GROUND_Y, fl(p.h), fl(p.width_pct), p.tiers}, snow, clip);
    fillEllipse(frame, V(fl(p.x), fl(p.base_y)), 17.f, 3.4f, snow, clip);   // drifted at its foot
  }
  for (int k = 0; k < 2; ++k) pineSnow(frame, fgPine(ls.fg[k]), snow, clip);
  for (int i = 0; i < ls.n_rocks; ++i) rockSnow(frame, ls.rocks[i], i + 1, snow, clip);
}

// The boulder's cap: the boulder drawn again in snow, in slices cut a third of the way down.
Rect boulderCapBounds(const World& w) {
  if (!(w.xmas.m.extras & XE_SNOW)) return kNone;
  const Vec2 b = landscapeBoulder(w.land);
  return box(b.x - fl(LS_BOULDER_R) - 3.f, b.y - fl(LS_BOULDER_H) - 3.f, b.x + fl(LS_BOULDER_R) + 3.f, b.y - 2.f);
}
void drawBoulderCap(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  Palette sp;
  const Pixel snow = snowCol(pal);
  for (Pixel& p : sp.c) p = snow;
  const Vec2 b = landscapeBoulder(w.land);
  const float top = b.y - fl(LS_BOULDER_H), x0 = b.x - fl(LS_BOULDER_R) - 2.f, x1 = b.x + fl(LS_BOULDER_R) + 2.f;
  for (int k = 0; k < 6; ++k) {
    const float a = lerp(x0, x1, fl(k) / 6.f), c = lerp(x0, x1, fl(k + 1) / 6.f);
    const float cut = top + 5.f + 1.5f * fl((int)(hashXY(k, 11) & 3u));
    const Rect r = clip.clipped(Rect{(int)floorf(a), (int)floorf(top) - 2, (int)ceilf(c), (int)ceilf(cut)});
    if (!r.empty()) streamDrawStaticRock(w.land, s, sp, r, 1.f);
  }
}

// The tent's: a cap along the ridge and down the upper part of both panels, drifts at its corners.
bool tentPieces(const World& w) {
  const XmasMock& m = w.xmas.m;
  return (m.extras & (XE_STOCKING | XE_SNOW)) && w.tent.state == TENT_UP && w.tent.progress >= 1.f;
}
void drawTentSnow(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Tent& t = w.tent;
  const Pixel snow = snowCol(pal), body = t.lit ? pal[TENT_LIT] : pal[TENT];
  const Vec2 ba = tentRidgeEnd(t, 0), fa = tentRidgeEnd(t, 1);
  const Vec2 bl = V(t.pos.x - 38.f, t.pos.y), fr = V(t.pos.x + 38.f, t.pos.y);
  const Vec2 mid = mixV(ba, fa, 0.5f);
  const float d = 0.24f;
  const Vec2 cap[6] = {ba, fa, mixV(fa, fr, d), V(mid.x, mid.y + d * (fr.y - fa.y)), mixV(ba, bl, d), ba};
  fillPolygon(s, cap, 5, mixRGB(body, snow, 0.82f), clip);
  strokeCapsule(s, ba, fa, 2.4f, snow, clip);
  fillEllipse(s, V(bl.x + 4.f, bl.y), 9.f, 2.8f, snow, clip);
  fillEllipse(s, V(fr.x - 4.f, fr.y), 9.f, 2.8f, snow, clip);
}
// The generator box's: a cap on the lid, a little over its edges.
void drawBoxSnow(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Vec2 b = boxOf(w.land);
  const float x0 = b.x - 0.5f * kBoxW, x1 = b.x + 0.5f * kBoxW, y0 = b.y - kBoxH;
  const Vec2 q[6] = {V(x0 - 0.8f, y0 + 1.2f), V(x0 + 2.6f, y0 - 3.6f), V(x1 + 3.6f, y0 - 3.6f),
                     V(x1 + 0.8f, y0 + 1.4f), V(b.x + 2.f, y0 + 0.4f), V(b.x - 3.f, y0 + 1.6f)};
  fillPolygon(s, q, 6, snowCol(pal), clip);
}

Rect tentXBounds(const World& w) {
  if (!tentPieces(w)) return kNone;
  const Tent& t = w.tent;
  Rect r = box(t.pos.x - 48.f, t.pos.y - 58.f, t.pos.x + 48.f, t.pos.y + 4.f);
  if (stockingShown(w)) unite(r, stockingBounds(w));
  return r;
}

// ---- the north star ----------------------------------------------------------------------------------
//
// Upper left, above the clouds' band (the clouds ride at y 96 and 158, landscape.cpp; their puffs reach
// up to about y 70), well left of the old sky's moon (x 640) and nowhere near the status chip (bottom
// right). Its sparkle stays inside rows 32-63, so a twinkle repaints one strip. A real build should not
// draw it where the real moon (SkyDraw) comes within about 40 px.
constexpr float kNorthX = 100.f, kNorthY = 47.f;
const float kNorthLen[4] = {11.f, 8.5f, 6.f, 8.f};    // the sparkle's arms, by twinkle step
const float kNorthLvl[4] = {1.f, 0.82f, 0.55f, 0.75f};
Rect northBounds() { return box(kNorthX - 13.f, kNorthY - 13.f, kNorthX + 13.f, kNorthY + 13.f, 1.f); }
void drawNorth(uint8_t step, Surface& s, const Rect& clip, const Palette& pal) {
  const float dark = darkOf(pal);
  if (dark <= 0.3f) return;   // a night thing
  const int i = (step - 1) & 3;
  const float L = kNorthLen[i], lv = kNorthLvl[i] * dark;
  const Vec2 c = V(kNorthX, kNorthY);
  const Pixel star = pal[STAR], ray = mixRGB(pal[SKY0], star, 0.25f + 0.65f * lv);
  softDisc(s, c, 6.5f, 6.5f, star, 38.f * lv, clip);
  const Vec2 v[4] = {V(c.x, c.y - L), V(c.x + 1.1f, c.y), V(c.x, c.y + L), V(c.x - 1.1f, c.y)};
  fillPolygon(s, v, 4, ray, clip);
  const float H = 0.8f * L;
  const Vec2 h[4] = {V(c.x - H, c.y), V(c.x, c.y - 1.1f), V(c.x + H, c.y), V(c.x, c.y + 1.1f)};
  fillPolygon(s, h, 4, ray, clip);
  fillCircle(s, c, 1.3f + 0.6f * lv, mixRGB(star, hexPx(0xFFFFFF), 0.25f * lv), clip);
}

// ---- the raccoon's rim of light from the tree ------------------------------------------------------

Critter rimOf(const World& w, const Critter& c) {
  Critter r = c;
  const float dir = treeX(w.land) >= c.feet.x ? 1.f : -1.f;
  r.feet = V(c.feet.x + 1.3f * dir, c.feet.y - 0.9f);
  return r;
}
Palette rimPal(const Palette& pal, const LightLook& L, float lit) {
  Palette p;
  const Pixel rim = mixRGB(pal[GROUND], hexPx(L.multi ? 0xFFC08A : kWarmHalo), clamp01(0.35f + 0.9f * lit));
  for (Pixel& q : p.c) q = rim;
  return p;
}

}  // namespace

// ---- public ---------------------------------------------------------------------------------------

bool xmasOn(const World& w) {
  static const XmasMock kZero{};
  return memcmp(&w.xmas.m, &kZero, sizeof(XmasMock)) != 0;
}

Vec2 xmasWheelHub(const Landscape& ls) { return hubOf(ls); }
float xmasBankY(const Landscape& ls, float x) { return bankYAt(ls, x); }
Vec2 xmasLadderRung(const Landscape& ls, int k) {
  Vec2 r[2][2];
  ladderRails(ls, r);
  return mixV(rungEnd(r, 0, k), rungEnd(r, 1, k), 0.5f);
}
Vec2 xmasOrnament(const Landscape& ls, int i) { return ornBall(ls, i < 0 ? 0 : (i >= XMAS_ORNS ? XMAS_ORNS - 1 : i)); }
Vec2 xmasBushAt(const Landscape& ls) { return bushAt(ls); }

bool xmasActorY(const World& w, int idx, int16_t& y) {
  const XmasMock& m = w.xmas.m;
  switch (idx) {
    case 0:
      if (!m.gen) return false;
      y = (int16_t)floorf(boxOf(w.land).y);
      return true;
    case 1:
      if (!m.gen) return false;
      y = (int16_t)floorf(bankYAt(w.land, hubOf(w.land).x));
      return true;
    case 2:
      if (!(m.extras & XE_PRESENTS)) return false;
      y = (int16_t)floorf(giftsAt(w.land).y + 9.f);
      return true;
    case 3:
      if (!tentPieces(w)) return false;
      y = (int16_t)floorf(w.tent.pos.y);
      return true;
    case 4:
      if (!castOn(m)) return false;
      y = castSortY(w);
      return true;
    case 5:
      if (m.bush == 0) return false;
      y = (int16_t)floorf(bushAt(w.land).y);
      return true;
    case 6:   // after the boulder (it is added after the stream's actors at the same y)
      return (m.extras & XE_SNOW) && streamActorY(w, y);
    default: return false;
  }
}

void xmasDrawActor(const World& w, int idx, Surface& s, const Rect& clip, const Palette& pal) {
  const XmasMock& m = w.xmas.m;
  switch (idx) {
    case 0:
      if (hits(xmasGenBounds(w), clip)) {
        drawBox(w, s, clip, pal);
        if (m.extras & XE_SNOW) drawBoxSnow(w, s, clip, pal);
        drawSparks(w, s, clip, pal);
      }
      break;
    case 1:
      if (hits(xmasGenBounds(w), clip)) {
        drawWheel(w, s, clip, pal);
        drawSparks(w, s, clip, pal);   // over the wheel too: they fly in front of it
      }
      break;
    case 2:
      if (hits(presentsBounds(w), clip)) drawPresents(w, s, clip, pal);
      break;
    case 3:
      if (!hits(tentXBounds(w), clip)) break;
      if (m.extras & XE_SNOW) drawTentSnow(w, s, clip, pal);
      if (stockingShown(w)) drawStocking(w, s, clip, pal);
      break;
    case 4: {
      if (!hits(xmasCritBounds(w), clip)) break;
      Cast k;
      castOf(w, k);
      const LightLook L = lookOf(w, pal);
      const float lit = treeLight(w, L);
      for (int r = 0; r < k.runs; ++r) drawTinsel(s, k.run[r], 9, lit, pal, clip);
      for (int i = 0; i < k.n; ++i) {
        // The raccoon by the lit tree: a warm rim of the lights' glow on its tree side first.
        if (k.c[i].kind == CR_RACCOON && lit > 0.05f) critterDraw(rimOf(w, k.c[i]), s, rimPal(pal, L, lit), clip);
        critterDraw(k.c[i], s, pal, clip);
      }
      if (m.garland == XG_TANGLED && k.runs > 1) drawTinsel(s, k.run[1], 5, lit, pal, clip);   // the loop over it
      if (k.ball) {
        drawBall(s, k.ball_at, k.ball_gold, lit, pal, clip);
      }
      break;
    }
    case 5:
      if (hits(bushBounds(w), clip)) drawBush(w, s, clip, pal, 0.f);
      break;
    case 6:
      if (hits(boulderCapBounds(w), clip)) drawBoulderCap(w, s, clip, pal);
      break;
    default: break;
  }
}

void xmasDrawTree(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const XmasMock& m = w.xmas.m;
  if (!treeOn(m) || !hits(xmasTreeBounds(w), clip)) return;
  const Landscape& ls = w.land;
  const LightLook L = lookOf(w, pal);
  const float lit = treeLight(w, L);
  const float cx = treeX(ls), base = treeBase(ls);
  // The light on the ground under the tree, and a little on the tree itself.
  if (lit > 0.f) {
    const Pixel gc = hexPx(L.multi ? 0xF0A07A : 0xFF9C52);
    for (int ring = 0; ring < 3; ++ring)
      softDisc(s, V(cx, base + 4.f), 60.f - 15.f * fl(ring), 9.f - 2.2f * fl(ring), gc, kGroundGlowA * lit, clip);
  }
  if (m.gen) drawWire(w, s, clip, pal);
  const int n = tierCount(ls);
  for (int k = 0; k < n && k < m.wraps; ++k) drawWrap(w, k, L, wrapLit(m, k), s, clip, pal);
  // The garland: scallops on the tiers it is draped on.
  int draped = 0;
  if (m.garland == XG_DRAPED) draped = n - 1;
  else if (m.garland == XG_PART) draped = 2;
  else if (m.garland == XG_TANGLED) draped = 1;
  for (int k = 1; k <= draped && k < n; ++k)
    for (int j = 0; j < 2; ++j) {
      if (m.garland == XG_TANGLED && j == 1) continue;   // the right half is the one round the bird
      Vec2 p[7];
      scallop(ls, k, j, p);
      if ((m.extras & XE_GAP) && k == n - 1 && j == 0) {   // yanked loose with the ornament: it trails to the ground
        const Vec2 b = ornBall(ls, XMAS_LOW_ORN), top = p[6];
        const Vec2 g = V(b.x + 5.f, base + 7.f);
        for (int i = 0; i < 7; ++i) {
          const float u = fl(i) / 6.f;
          p[i] = V(lerp(top.x, g.x, u) - 6.f * sinT(0.5f * u), lerp(top.y, g.y, u * u));
        }
      }
      drawTinsel(s, p, 7, lit, pal, clip);
    }
  for (int i = 0; i < XMAS_ORNS; ++i) {
    const bool on = (m.orns >> i) & 1;
    const bool gap = i == XMAS_LOW_ORN && (m.extras & XE_GAP);
    if (!on && !gap) continue;
    const Vec2 h = ornHook(ls, i), b = ornBall(ls, i);
    drawHook(s, h, V(b.x, b.y - kOrnR - (gap ? 0.f : 1.f)), pal, clip);
    if (gap) {   // the empty hook, bent where it was pulled
      drawHook(s, V(b.x, b.y - kOrnR), V(b.x + 1.6f, b.y - kOrnR + 1.8f), pal, clip);
      continue;
    }
    drawBall(s, b, kOrn[i].gold != 0, lit, pal, clip);
  }
  if (m.extras & XE_STAR) drawStar(s, ls, m.power != XP_OFF && m.wraps > 0 ? L.dark * L.level : 0.f, pal, clip);
  if (m.extras & XE_LADDER) drawLadder(s, ls, pal, clip);
  if (m.extras & XE_COIL) drawCoil(w, s, L, pal, clip);
}

void xmasDrawSky(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if ((w.xmas.m.extras & XE_SLEIGH) && hits(sleighBounds(), clip)) drawSleigh(s, clip, pal);
  if (w.xmas.m.north && hits(northBounds(), clip)) drawNorth(w.xmas.m.north, s, clip, pal);
}

Vec2 xmasNorthStar() { return V(kNorthX, kNorthY); }

const Palette& xmasTuftPal(const World& w, const Palette& pal, Palette& tmp) {
  if (!(w.xmas.m.extras & XE_SNOW)) return pal;
  tmp = pal;
  tmp.c[GRASS] = frostCol(pal);
  return tmp;
}

void xmasDrawStatic(const World& w, Surface& frame, const Palette& pal) {
  if (!(w.xmas.m.extras & XE_SNOW) || !buildDone(w.build)) return;
  snowStatic(w, frame, pal);
}

void xmasDrawFgPineSnow(const World& w, int k, Surface& s, const Rect& clip, const Palette& pal) {
  if (!(w.xmas.m.extras & XE_SNOW) || !buildDone(w.build)) return;
  pineSnow(s, fgPine(w.land.fg[k ? 1 : 0]), snowCol(pal), clip);
}

Rect xmasTreeBounds(const World& w) {
  const XmasMock& m = w.xmas.m;
  if (!treeOn(m)) return kNone;
  const Landscape& ls = w.land;
  const float cx = treeX(ls), top = treeTop(ls), base = treeBase(ls);
  Rect r = box(cx - 62.f, top - 16.f, cx + 62.f, base + 14.f);   // the star's glow, the light on the ground
  if (m.gen) {
    Vec2 p[9];
    wirePts(ls, p);   // (and the box's snow cap is in xmasGenBounds' box)
    for (const Vec2& q : p) unite(r, around(q, 2.f, 2.f));
  }
  if (m.extras & XE_LADDER) {
    Vec2 rl[2][2];
    ladderRails(ls, rl);
    unite(r, box(rl[0][1].x, rl[0][1].y, rl[1][0].x, rl[1][0].y, 3.f));
  }
  if (m.extras & XE_COIL) unite(r, coilBounds(w));
  return r;
}

Rect xmasGenBounds(const World& w) {
  if (!w.xmas.m.gen) return kNone;
  const Landscape& ls = w.land;
  const Vec2 h = hubOf(ls), b = boxOf(ls);
  const float rr = kWheelR + kPaddle;
  Rect r = box(h.x - kWheelSquash * rr - 16.f, h.y - rr + kDepthY, h.x + rr + 3.f, h.y + rr + 2.f, 3.f);   // the wheel, its splash and wake
  unite(r, box(b.x - 0.5f * kBoxW - 4.f, b.y - kBoxH - 5.f, b.x + 0.5f * kBoxW + 5.f, b.y + 1.f));
  if (w.xmas.m.spark) {
    const Vec2 o = sparkFrom(ls);
    unite(r, box(o.x - 10.f, o.y - 8.f, o.x + 10.f, o.y + 8.f));
    for (int i = 0; i < kSparks; ++i) {
      Vec2 c;
      float rad;
      bool hot;
      if (sparkAt(ls, w.xmas.m.spark, i, c, rad, hot)) unite(r, around(c, rad, rad));
    }
  }
  return r;
}

Rect xmasCritBounds(const World& w) {
  Rect r = kNone;
  if (castOn(w.xmas.m)) {
    Cast k;
    castOf(w, k);
    for (int i = 0; i < k.n; ++i) unite(r, critterBounds(k.c[i]));
    for (int j = 0; j < k.runs; ++j)
      for (int i = 0; i < 9; ++i) unite(r, around(k.run[j][i], 0.5f * kTinselW + 1.f, 0.5f * kTinselW + 1.f));
    if (k.ball) unite(r, around(k.ball_at, kOrnR + 1.f, kOrnR + 2.f));
  }
  unite(r, bushBounds(w));
  return r;
}

Rect xmasExtraBounds(const World& w) {
  const XmasMock& m = w.xmas.m;
  Rect r = kNone;
  if (m.extras & XE_PRESENTS) unite(r, presentsBounds(w));
  if (stockingShown(w)) unite(r, stockingBounds(w));
  if (m.extras & XE_SLEIGH) unite(r, sleighBounds());
  if (m.north) unite(r, northBounds());
  if (tentPieces(w)) unite(r, tentXBounds(w));
  if (m.extras & XE_SNOW) unite(r, boulderCapBounds(w));
  return r;
}

void xmasTrack(World& w, Renderer& r) {
  XmasState& x = w.xmas;
  const bool changed = memcmp(&x.m, &x.d, sizeof(XmasMock)) != 0;
  const bool hand = (x.m.extras & XE_COIL) != 0;   // the run up to his hand moves as he breathes
  if (!changed && !hand && x.d_tree.empty() && x.d_gen.empty() && x.d_crit.empty() && x.d_extra.empty()) return;
  auto mark = [&r](Rect& prev, const Rect& cur, bool ch) {
    if (!ch && prev.x0 == cur.x0 && prev.y0 == cur.y0 && prev.x1 == cur.x1 && prev.y1 == cur.y1) return;
    if (!prev.empty()) r.markDirtyRect(prev.clipped(Rect{0, 0, SS_WIDTH, SS_HEIGHT}));
    if (!cur.empty()) r.markDirtyRect(cur.clipped(Rect{0, 0, SS_WIDTH, SS_HEIGHT}));
    prev = cur;
  };
  if ((x.m.extras ^ x.d.extras) & XE_SNOW) r.invalidateCache();   // the snow lives in the static cache
  mark(x.d_tree, xmasTreeBounds(w), changed || hand);
  mark(x.d_gen, xmasGenBounds(w), changed);
  mark(x.d_crit, xmasCritBounds(w), changed);
  mark(x.d_extra, xmasExtraBounds(w), changed || tentPieces(w) || ((x.d.extras & (XE_STOCKING | XE_SNOW)) != 0));
  x.d = x.m;
}

}  // namespace ss

#endif  // SS_XMAS
