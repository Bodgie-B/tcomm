// The Lookout's static landscape and tower. See lookout_land.h.
//
// Everything is flat silhouettes in palette slots (or fixed mixes of two
// slots, which are still one flat colour per shape). The hill and the valley
// crests are sampled every 16 px like the camp's ridges, and every other shape
// reads the same samples, so trees and the tower stand exactly on the drawn
// edge. All tower geometry is a fixed table relative to (tower_x, tower_gy).
#include "lookout_land.h"

#include <math.h>

#include "../anim/ease.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
inline int16_t toI16(float v) { return (int16_t)floorf(v + 0.5f); }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

// Sky bands as the camp draws them; the lowest runs down under the valley.
const int kBand[6] = {0, 58, 118, 178, 228, SS_HEIGHT};   // the lowest runs down under the valley (and the hill, in the build)

float crestAt(const float* c, float x) {
  float f = (x - fl(LS_CREST_X0)) / fl(LS_CREST_STEP);
  if (f <= 0.f) return c[0];
  if (f >= fl(LS_CREST_N - 1)) return c[LS_CREST_N - 1];
  const int i = (int)f;
  f -= fl(i);
  return lerp(c[i], c[i + 1], f);
}

// Smallest x where the hill skyline is at or above `y` (the hill rises to the
// right). Off the left edge returns LS_CREST_X0, never higher than the plateau
// start.
float hillXAt(const LookoutLand& lk, float y) {
  for (int i = 0; i < LS_CREST_N; ++i) {
    if (lk.hill[i] <= y) {
      if (i == 0) return fl(LS_CREST_X0);
      const float y0 = lk.hill[i - 1], y1 = lk.hill[i];
      const float t = y0 > y1 ? (y0 - y) / (y0 - y1) : 0.f;
      return fl(LS_CREST_X0 + LS_CREST_STEP * (i - 1)) + t * fl(LS_CREST_STEP);
    }
  }
  return fl(LS_CREST_X0 + LS_CREST_STEP * (LS_CREST_N - 1));
}

float distToSeg(float px, float py, Vec2 a, Vec2 b) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float l2 = dx * dx + dy * dy;
  float t = l2 > 1e-6f ? ((px - a.x) * dx + (py - a.y) * dy) / l2 : 0.f;
  t = clamp01(t);
  const float ex = a.x + dx * t - px, ey = a.y + dy * t - py;
  return sqrtf(ex * ex + ey * ey);
}

float trailDist(const LookoutLand& lk, float x, float y) {
  float d = 1e9f;
  for (int i = 0; i + 1 < lk.trail.n; ++i) d = minf(d, distToSeg(x, y, lk.trail.p[i], lk.trail.p[i + 1]));
  return d;
}

// True when a pine standing at (x, base) with half width hw and height h in
// front of a trail leg would put its crown across that leg (the hiker walks
// on the leg and is drawn over the cache, so a nearer crown would be wrong).
bool pineCrossesTrail(const LookoutLand& lk, float x, float base, float hw, float h) {
  for (int i = 0; i + 1 < lk.trail.n; ++i) {
    const Vec2 a = lk.trail.p[i], b = lk.trail.p[i + 1];
    const float lo = minf(a.x, b.x), hi = maxf(a.x, b.x);
    for (int k = -1; k <= 1; ++k) {
      const float xs = x + fl(k) * hw;
      if (xs < lo || xs > hi || hi - lo < 1e-3f) continue;
      const float ty = lerp(a.y, b.y, (xs - a.x) / (b.x - a.x));
      if (base > ty - 5.f && base - h < ty + 4.f) return true;
    }
  }
  return false;
}

// ---- generation ------------------------------------------------------------

void generateTrail(LookoutLand& lk, Rng& rng) {
  const float cx = fl(lk.tower_x), gy = fl(lk.tower_gy);
  const int legs = 2 * rng.rangeInt(2, 3) + 1;   // 5 or 7: the last turn is a left end
  const float foot_y = rng.range(446.f, 454.f);
  LkTrail& t = lk.trail;
  t.n = legs + 1;
  t.p[0] = Vec2{-24.f, foot_y};
  for (int k = 1; k < legs; ++k) {
    const float f = fl(k) / fl(legs);
    // Lower legs climb more on screen (they are nearer).
    const float g = 1.f - (1.f - f) * (1.f - f) * 0.6f - (1.f - f) * 0.4f;
    const float jy = rng.range(-5.f, 5.f);
    const float y = foot_y + (gy - foot_y) * g + jy;
    const float jx = rng.range(-18.f, 18.f);
    float x;
    if (k & 1) {   // right end
      x = lerp(500.f, cx - 34.f, g) + jx;
    } else {       // left end: on the face, under the skyline with a margin
      const float margin = 26.f * (1.f - 0.5f * g);
      x = hillXAt(lk, y - margin) + 22.f + jx;
      x = minf(x, t.p[k - 1].x - 70.f);
      x = maxf(x, 40.f);
    }
    t.p[k] = Vec2{x, y};
  }
  t.p[legs] = Vec2{cx - 22.f, gy};   // the stair foot
  t.cum[0] = 0.f;
  for (int i = 1; i < t.n; ++i) {
    const float dx = t.p[i].x - t.p[i - 1].x, dy = t.p[i].y - t.p[i - 1].y;
    t.cum[i] = t.cum[i - 1] + sqrtf(dx * dx + dy * dy);
  }
}

void generatePines(LookoutLand& lk, Rng& rng) {
  const float cx = fl(lk.tower_x), gy = fl(lk.tower_gy);
  const float foot_y = lk.trail.p[0].y;
  const int want = rng.rangeInt(LK_MAX_PINES - 3, LK_MAX_PINES);
  lk.n_pines = 0;
  for (int tries = 0; tries < 400 && lk.n_pines < want; ++tries) {
    const bool foot = rng.chance(0.5f);              // half the tries cluster toward the foot
    const float x = foot ? rng.range(-10.f, 300.f) : rng.range(-10.f, 650.f);
    const float top = lookoutHillAt(lk, x) + 6.f;
    if (top >= 476.f) continue;
    const float y = rng.range(top, 476.f);
    const float d = clamp01((foot_y - y) / (foot_y - gy));   // 0 at the foot, 1 on the ridge
    const float u = rng.uniform();
    if (u > 1.f - 0.8f * d) continue;                         // thinning up the slope
    const float h0 = rng.range(52.f, 86.f);
    const float h = h0 * (1.f - 0.62f * d);
    const int tiers = rng.rangeInt(3, 4);
    const int wpct = rng.rangeInt(55, 72);
    const float hw = 0.5f * fl(wpct) * 0.01f * h;
    if (x + hw > cx - 100.f && y < gy + 50.f) continue;       // the ridge top by the tower stays bare
    if (x + hw + 2.f >= fl(LS_QUIET_X0) && y + 3.f >= fl(LS_QUIET_Y0)) continue;
    if (trailDist(lk, x, y) < 9.f) continue;
    if (pineCrossesTrail(lk, x, y, hw, h)) continue;
    bool clear = true;
    for (int j = 0; j < lk.n_pines && clear; ++j) {
      const LsPine& q = lk.pines[j];
      const float qhw = 0.5f * fl(q.width_pct) * 0.01f * fl(q.h);
      if (fabsf(fl(q.x) - x) < 0.6f * (hw + qhw) && fabsf(fl(q.base_y) - y) < 16.f) clear = false;
    }
    if (!clear) continue;
    LsPine p;
    p.x = toI16(x);
    p.base_y = toI16(y);
    p.h = toI16(h);
    p.tiers = (uint8_t)tiers;
    p.width_pct = (uint8_t)wpct;
    lk.pines[lk.n_pines++] = p;
  }
  for (int i = 1; i < lk.n_pines; ++i) {   // insertion sort by base, farthest first
    const LsPine key = lk.pines[i];
    int j = i - 1;
    while (j >= 0 && lk.pines[j].base_y > key.base_y) { lk.pines[j + 1] = lk.pines[j]; --j; }
    lk.pines[j + 1] = key;
  }
}

void generateTufts(LookoutLand& lk, Rng& rng) {
  const int want = rng.rangeInt(18, 28);
  lk.n_tufts = 0;
  for (int tries = 0; tries < 400 && lk.n_tufts < want; ++tries) {
    const float x = rng.range(4.f, 650.f);
    const float y = rng.range(396.f, 478.f);
    if (y < lookoutHillAt(lk, x) + 12.f) continue;
    if (x + 10.f >= fl(LS_QUIET_X0) && y >= fl(LS_QUIET_Y0)) continue;   // quiet corner
    if (trailDist(lk, x, y) < 7.f) continue;
    LsTuft& t = lk.tufts[lk.n_tufts++];
    t.dyn = 0;
    t.x = toI16(x);
    t.y = toI16(y);
    t.n = (uint8_t)rng.rangeInt(2, 3);
    for (int k = 0; k < 3; ++k) {
      t.h[k] = (uint8_t)rng.rangeInt(7, 15);
      t.off[k] = (int8_t)((k - 1) * 3 + rng.rangeInt(-1, 1));
      t.lean[k] = (int8_t)rng.rangeInt(-4, 4);
    }
  }
}

// ---- pines -------------------------------------------------------------------

struct Tier { float apex_y, base_y, half_w; };

// As landscape.cpp: tiers share one height, apexes step down evenly, widths
// (pct/100 * h) * (1 - 0.2 k) with k = 0 at the bottom tier.
Tier tierGeom(int i, int n, float top, float fbot, float h, float pct) {
  const float F = fbot - top;
  const float th = 0.42f * F;
  const float sp = n > 1 ? (F - th) / fl(n - 1) : 0.f;
  Tier t;
  t.apex_y = top + fl(i) * sp;
  t.base_y = t.apex_y + th;
  t.half_w = 0.5f * (pct * 0.01f) * h * (1.f - 0.2f * fl(n - 1 - i));
  return t;
}

// Hillside pine: trunk, tiers in `body`, a shade wedge on the east (right)
// half: the low sun is in the west.
void drawHillPine(Surface& s, const LsPine& p, Pixel body, Pixel shade, Pixel trunk, const Rect& clip, float k) {
  if (k <= 0.01f) return;
  const float h = fl(p.h) * k;
  const float base = fl(p.base_y);
  const float top = base - h;
  const float fbot = base - maxf(3.f, h * 0.16f);
  const float cx = fl(p.x);
  strokeCapsule(s, Vec2{cx, fbot - 3.f}, Vec2{cx, base}, maxf(2.f, h * 0.06f), trunk, clip);
  for (int i = p.tiers - 1; i >= 0; --i) {
    const Tier t = tierGeom(i, p.tiers, top, fbot, h, fl(p.width_pct));
    const Vec2 tri[3] = {{cx, t.apex_y}, {cx - t.half_w, t.base_y}, {cx + t.half_w, t.base_y}};
    fillPolygon(s, tri, 3, body, clip);
    const float th = t.base_y - t.apex_y;
    const Vec2 q[4] = {{cx + t.half_w * 0.35f, t.apex_y + th * 0.35f},
                       {cx + t.half_w, t.base_y},
                       {cx, t.base_y},
                       {cx, t.apex_y + th * 0.55f}};
    fillPolygon(s, q, 4, shade, clip);
  }
}

void drawTuft(const LsTuft& t, Surface& s, Pixel col, const Rect& clip, float sc) {
  if (sc <= 0.01f) return;
  for (int k = 0; k < t.n; ++k) {
    const float bx = fl(t.x + t.off[k]);
    const Vec2 tri[3] = {{bx - 1.4f, fl(t.y)}, {bx + 1.4f, fl(t.y)},
                         {bx + fl(t.lean[k]) * sc, fl(t.y) - fl(t.h[k]) * sc}};
    fillPolygon(s, tri, 3, col, clip);
  }
}

// ---- terrain -----------------------------------------------------------------

void drawCrestFill(Surface& s, const float* crest, Pixel col, const Rect& clip, float dy = 0.f) {
  Vec2 p[LS_CREST_N + 2];
  for (int i = 0; i < LS_CREST_N; ++i) p[i] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * i), crest[i] + dy};
  p[LS_CREST_N] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * (LS_CREST_N - 1)), fl(SS_HEIGHT + 2)};
  p[LS_CREST_N + 1] = Vec2{fl(LS_CREST_X0), fl(SS_HEIGHT + 2)};
  fillPolygon(s, p, LS_CREST_N + 2, col, clip);
}

// Serrated tree line along the near valley crest (as the camp's far trees).
void drawValleyTrees(const LookoutLand& lk, Surface& s, Pixel col, const Rect& clip, float dy) {
  const int kMaxTeeth = 20;
  const float* c = lk.valley[2];
  int i = 0;
  while (i < lk.n_spikes) {
    int m = lk.n_spikes - i;
    if (m > kMaxTeeth) m = kMaxTeeth;
    Vec2 p[SS_MAX_POLY_POINTS];
    int n = 0;
    for (int j = 0; j < m; ++j) {
      const float x0 = fl(lk.spikes[i + j].x);
      const float x1 = fl(lk.spikes[i + j + 1].x);
      const float xm = 0.5f * (x0 + x1);
      p[n++] = Vec2{x0, crestAt(c, x0) + dy};
      p[n++] = Vec2{xm, crestAt(c, xm) - fl(lk.spikes[i + j].h) + dy};
    }
    const float xe = fl(lk.spikes[i + m].x);
    p[n++] = Vec2{xe, crestAt(c, xe) + dy};
    for (int j = m; j >= 0; --j) {
      const float x = fl(lk.spikes[i + j].x);
      p[n++] = Vec2{x, crestAt(c, x) + 4.f + dy};
    }
    fillPolygon(s, p, n, col, clip);
    i += m;
  }
}

// The near shoulder: from the skyline at the left it falls away below the
// farther upper slope toward the right edge, so the hillside reads in two
// depths. The line never rises above the skyline. Sampled on the crest grid.
float shoulderSample(const LookoutLand& lk, int i) {
  const float x = fl(LS_CREST_X0 + LS_CREST_STEP * i);
  const float y = lerp(lk.sh_y[0], lk.sh_y[1], x / fl(SS_WIDTH)) + 7.f * sinT(x / 240.f + lk.sh_ph);
  return maxf(y, lk.hill[i]);
}

// Shoulder line y at frame x, linear between samples (matches the polygon edge).
float shoulderAt(const LookoutLand& lk, float x) {
  float f = (x - fl(LS_CREST_X0)) / fl(LS_CREST_STEP);
  if (f <= 0.f) return shoulderSample(lk, 0);
  if (f >= fl(LS_CREST_N - 1)) return shoulderSample(lk, LS_CREST_N - 1);
  const int i = (int)f;
  return lerp(shoulderSample(lk, i), shoulderSample(lk, i + 1), f - fl(i));
}

void drawShoulder(const LookoutLand& lk, Surface& s, Pixel col, const Rect& clip) {
  Vec2 p[LS_CREST_N + 2];
  for (int i = 0; i < LS_CREST_N; ++i) p[i] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * i), shoulderSample(lk, i)};
  p[LS_CREST_N] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * (LS_CREST_N - 1)), fl(SS_HEIGHT + 2)};
  p[LS_CREST_N + 1] = Vec2{fl(LS_CREST_X0), fl(SS_HEIGHT + 2)};
  fillPolygon(s, p, LS_CREST_N + 2, col, clip);
}

// The trail, coloured by the tone it lies on: `lo` on the near shoulder, `hi`
// on the farther upper slope. Each leg is split where it crosses the shoulder
// line (found by sampling, then bisection), so the colour change sits on the edge.
void drawTrail(const LookoutLand& lk, Surface& s, Pixel lo, Pixel hi, const Rect& clip, float upto) {
  const LkTrail& t = lk.trail;
  const float total = t.cum[t.n - 1];
  const float dmax = clamp01(upto) * total;
  for (int i = 0; i + 1 < t.n; ++i) {
    if (t.cum[i] >= dmax) break;
    const Vec2 a = t.p[i];
    Vec2 b = t.p[i + 1];
    if (t.cum[i + 1] > dmax) {   // the leg being drawn in (the build): up to dmax
      const float seg = t.cum[i + 1] - t.cum[i];
      const float f = seg > 1e-4f ? (dmax - t.cum[i]) / seg : 1.f;
      b = Vec2{lerp(a.x, b.x, f), lerp(a.y, b.y, f)};
    }
    const float w = lerp(2.6f, 1.6f, 0.5f * (t.cum[i] + t.cum[i + 1]) / total);
    const int kSub = 16;
    float u0 = 0.f;
    bool below0 = a.y >= shoulderAt(lk, a.x);
    for (int k = 1; k <= kSub; ++k) {
      float u1 = fl(k) / fl(kSub);
      const Vec2 q{lerp(a.x, b.x, u1), lerp(a.y, b.y, u1)};
      const bool below1 = q.y >= shoulderAt(lk, q.x);
      if (below1 != below0 || k == kSub) {
        if (below1 != below0) {
          float l = fl(k - 1) / fl(kSub), r = u1;
          for (int it = 0; it < 12; ++it) {
            const float m = 0.5f * (l + r);
            const Vec2 qm{lerp(a.x, b.x, m), lerp(a.y, b.y, m)};
            if ((qm.y >= shoulderAt(lk, qm.x)) == below0) l = m; else r = m;
          }
          u1 = 0.5f * (l + r);
        }
        strokeCapsule(s, Vec2{lerp(a.x, b.x, u0), lerp(a.y, b.y, u0)}, Vec2{lerp(a.x, b.x, u1), lerp(a.y, b.y, u1)},
                      w, below0 ? lo : hi, clip);
        u0 = u1;
        below0 = below1;
      }
    }
  }
}

void drawForeground(const LookoutLand& lk, Surface& s, Pixel col, const Rect& clip) {
  Vec2 p[28];
  int n = 0;
  for (int x = -16; x <= 816; x += 32) {
    const float t = fl(x) / fl(SS_WIDTH);
    p[n++] = Vec2{fl(x), lerp(lk.fg_y[0], lk.fg_y[1], t) + 3.f * sinT(fl(x) / 150.f + lk.fg_ph)};
  }
  p[n++] = Vec2{816.f, fl(SS_HEIGHT + 2)};
  p[n++] = Vec2{-16.f, fl(SS_HEIGHT + 2)};
  fillPolygon(s, p, n, col, clip);
}

// ---- tower -------------------------------------------------------------------

constexpr float kDX = (float)LK_DEPTH_DX, kDY = (float)LK_DEPTH_DY;
constexpr float kDeck = -(float)LK_LEG_H;                  // deck surface, relative to the base
constexpr float kEave = kDeck - (float)LK_CAB_H;
constexpr float kWin0 = kDeck - 40.f, kWin1 = kDeck - 15.f; // window band
constexpr float kCabHW = (float)LK_LEG_TOP_HW;             // 30
constexpr float kWalkHW = kCabHW + (float)LK_CATWALK;      // 39
constexpr float kRail = kDeck - (float)LK_RAIL_H;
constexpr float kGap0 = 12.f, kGap1 = 24.f;                // stair gap in the front rail

// Stair knots, relative to the base: flight 1 up-right, landing 1, flight 2
// up-left, landing 2, flight 3 up-right to the deck.
constexpr float kRise = (float)LK_LEG_H / 3.f;
const float kStair[6][2] = {{-22.f, 0.f},          {18.f, -kRise},        {28.f, -kRise},
                            {-12.f, -2.f * kRise}, {-22.f, -2.f * kRise}, {18.f, -3.f * kRise}};

struct TowerCols {
  Pixel leg, back, brace, stair, deck, under;
  Pixel wall_front, wall_west, roof_front, roof_west;
  Pixel rail_front, rail_west;
  Pixel glass, glass_lit, glint, finder, through;
  bool windows;   // through-view, finder and glints shown
};

struct T {   // placement
  float ox, oy;
  Vec2 p(float x, float y) const { return Vec2{ox + x, oy + y}; }
};

// Front leg half spread at height y (y <= 0, relative).
inline float legHW(float y) {
  return fl(LK_LEG_BASE_HW) + (fl(LK_LEG_TOP_HW) - fl(LK_LEG_BASE_HW)) * (-y / fl(LK_LEG_H));
}

void cap(Surface& s, const T& t, float x0, float y0, float x1, float y1, float w, Pixel c, const Rect& clip) {
  strokeCapsule(s, t.p(x0, y0), t.p(x1, y1), w, c, clip);
}

void quad(Surface& s, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Pixel col, const Rect& clip) {
  const Vec2 q[4] = {a, b, c, d};
  fillPolygon(s, q, 4, col, clip);
}

// Point at (u, v) in a quad TL, TR, BR, BL (u across, v down).
Vec2 quadAt(const Vec2* q, float u, float v) {
  const Vec2 top{lerp(q[0].x, q[1].x, u), lerp(q[0].y, q[1].y, u)};
  const Vec2 bot{lerp(q[3].x, q[2].x, u), lerp(q[3].y, q[2].y, u)};
  return Vec2{lerp(top.x, bot.x, v), lerp(top.y, bot.y, v)};
}

// A pane that catches the sun: lit glass and two slanted glint stripes.
void drawLitPane(Surface& s, const Vec2* q, const TowerCols& c, const Rect& clip) {
  fillPolygon(s, q, 4, c.glass_lit, clip);
  quad(s, quadAt(q, 0.40f, 0.f), quadAt(q, 0.70f, 0.f), quadAt(q, 0.36f, 1.f), quadAt(q, 0.06f, 1.f), c.glint, clip);
  quad(s, quadAt(q, 0.80f, 0.f), quadAt(q, 0.90f, 0.f), quadAt(q, 0.56f, 1.f), quadAt(q, 0.46f, 1.f), c.glint, clip);
}

constexpr float kHand = 16.f;   // stair handrail height above the tread line (about a 44 px hiker's hand)

// A flight's treads: a sawtooth polygon (LKL_TOWER).
void drawFlight(Surface& s, const T& t, const float* a, const float* b, const TowerCols& c, const Rect& clip) {
  const int n = 5;
  const float w = (b[0] - a[0]) / fl(n), h = (b[1] - a[1]) / fl(n);
  Vec2 p[2 * n + 4];
  int k = 0;
  const float off = -0.5f * h;   // the feet line runs through the tread middles
  p[k++] = t.p(a[0], a[1] + off);
  for (int j = 0; j < n; ++j) {
    p[k++] = t.p(a[0] + fl(j) * w, a[1] + fl(j + 1) * h + off);
    p[k++] = t.p(a[0] + fl(j + 1) * w, a[1] + fl(j + 1) * h + off);
  }
  p[k++] = t.p(b[0], b[1] + off + 3.f);
  p[k++] = t.p(a[0], a[1] + off + 3.f);
  fillPolygon(s, p, k, c.stair, clip);
}

// A flight's handrail on posts (LKL_RAILS).
void drawFlightRail(Surface& s, const T& t, const float* a, const float* b, const TowerCols& c, const Rect& clip) {
  cap(s, t, a[0], a[1] - kHand, b[0], b[1] - kHand, 1.3f, c.stair, clip);
  cap(s, t, a[0], a[1], a[0], a[1] - kHand, 1.1f, c.stair, clip);
}

// The see-through holes in the cab's front (the far windows against the
// sky): integer rects, relative to the base. LKL_TOWER draws the sky and the
// finder in them; LKL_CAB draws the wall and glass round them.
const float kHole[2][4] = {{-7.f, kWin0 + kDY, 8.f, kWin1}, {16.f, kWin0 + 2.f, 26.f, kWin1}};

// The cab's front: wall, glass, west pane, glints, roof, flagpole. Drawn
// clipped to one piece of the area outside the holes (see drawCab).
void drawCabPiece(const T& t, Surface& s, const TowerCols& c, const Rect& clip) {
  fillRectF(s, t.ox - kCabHW, t.oy + kEave, t.ox + kCabHW, t.oy + kDeck, c.wall_front, clip);
  Vec2 west[4];
  {
    const float u0 = 0.2f, u1 = 0.8f;
    west[0] = t.p(-kCabHW + kDX * u0, kWin0 + kDY * u0);
    west[1] = t.p(-kCabHW + kDX * u1, kWin0 + kDY * u1);
    west[2] = t.p(-kCabHW + kDX * u1, kWin1 + kDY * u1);
    west[3] = t.p(-kCabHW + kDX * u0, kWin1 + kDY * u0);
  }
  Vec2 pane[3][4];
  for (int i = 0; i < 3; ++i) {
    const float x0 = -27.f + 19.f * fl(i), x1 = x0 + 16.f;
    pane[i][0] = t.p(x0, kWin0);
    pane[i][1] = t.p(x1, kWin0);
    pane[i][2] = t.p(x1, kWin1);
    pane[i][3] = t.p(x0, kWin1);
  }
  for (int i = 0; i < 3; ++i) fillPolygon(s, pane[i], 4, c.glass, clip);
  fillPolygon(s, west, 4, c.glass, clip);
  if (c.windows) {   // the near two catch the sun: west pane and front-left pane
    drawLitPane(s, west, c, clip);
    drawLitPane(s, pane[0], c, clip);
  }

  // Roof: soffit on the west, slopes, fascia, flagpole.
  const Vec2 wFT = t.p(-kCabHW, kEave);
  const Vec2 wBT = t.p(-kCabHW + kDX, kEave + kDY);
  const float o = fl(LK_ROOF_OVER);
  const float rs = (2.f * kCabHW + 2.f * o) / (2.f * kCabHW);   // roof depth scale
  const Vec2 eFL = t.p(-kCabHW - o, kEave), eFR = t.p(kCabHW + o, kEave);
  const Vec2 eBL = t.p(-kCabHW - o + kDX * rs, kEave + kDY * rs);
  const Vec2 apex = t.p(0.5f * kDX * rs, kEave + 0.5f * kDY * rs - fl(LK_ROOF_H));   // over the cab's centre
  quad(s, eFL, eBL, wBT, wFT, c.under, clip);
  {
    const Vec2 w3[3] = {eFL, eBL, apex};
    fillPolygon(s, w3, 3, c.roof_west, clip);
    const Vec2 f3[3] = {eFL, eFR, apex};
    fillPolygon(s, f3, 3, c.roof_front, clip);
  }
  fillRectF(s, eFL.x, eFL.y - 0.5f, eFR.x, eFR.y + 1.8f, c.roof_front, clip);
  quad(s, Vec2{eFL.x, eFL.y - 0.5f}, Vec2{eBL.x, eBL.y - 0.5f}, Vec2{eBL.x, eBL.y + 1.8f},
       Vec2{eFL.x, eFL.y + 1.8f}, c.roof_west, clip);
  strokeCapsule(s, apex, Vec2{apex.x, apex.y - fl(LK_POLE_H)}, 1.4f, c.leg, clip);
  fillCircle(s, Vec2{apex.x, apex.y - fl(LK_POLE_H)}, 1.3f, c.leg, clip);
}

// The cab's front, round the see-through holes when the windows show (so
// whatever is inside the cab, drawn before it, shows through them): the clip
// cut into bands of rows, each band into the spans between the holes.
void drawCab(const T& t, Surface& s, const TowerCols& c, const Rect& clip) {
  if (!c.windows) {
    drawCabPiece(t, s, c, clip);
    return;
  }
  Rect h[2];
  for (int i = 0; i < 2; ++i)
    h[i] = Rect{(int)(t.ox + kHole[i][0]), (int)(t.oy + kHole[i][1]), (int)(t.ox + kHole[i][2]), (int)(t.oy + kHole[i][3])};
  int ys[6] = {clip.y0, clip.y1, h[0].y0, h[0].y1, h[1].y0, h[1].y1};
  for (int i = 1; i < 6; ++i)   // sort
    for (int j = i; j > 0 && ys[j] < ys[j - 1]; --j) {
      const int tmp = ys[j];
      ys[j] = ys[j - 1];
      ys[j - 1] = tmp;
    }
  for (int k = 0; k + 1 < 6; ++k) {
    const int ya = ys[k] < clip.y0 ? clip.y0 : ys[k];
    const int yb = ys[k + 1] > clip.y1 ? clip.y1 : ys[k + 1];
    if (yb <= ya) continue;
    int x = clip.x0;
    for (int i = 0; i < 2; ++i) {   // h[0] is left of h[1]
      if (h[i].y0 > ya || h[i].y1 < yb) continue;   // the hole does not cover this band
      if (h[i].x0 > x) drawCabPiece(t, s, c, Rect{x, ya, h[i].x0 < clip.x1 ? h[i].x0 : clip.x1, yb});
      if (h[i].x1 > x) x = h[i].x1;
    }
    if (x < clip.x1) drawCabPiece(t, s, c, Rect{x, ya, clip.x1, yb});
  }
}

void drawTower(const LookoutLand& lk, Surface& s, const TowerCols& c, float rise, const Rect& clip_in, uint16_t mask) {
  const T t{fl(lk.tower_x), fl(lk.tower_gy) + rise};
  // Everything stands behind the ridge edge: nothing below the crest line.
  const Rect clip = clip_in.clipped(Rect{0, 0, SS_WIDTH, lk.tower_gy});
  if (clip.empty()) return;
  const float bl = -fl(LK_LEG_BASE_HW), tl = -kCabHW;
  const float lv[3] = {-3.f, -0.5f * fl(LK_LEG_H), kDeck + 3.f};

  if (mask & LKL_BIT(LKL_TOWER)) {
    // -- back legs (north side), darker.
    cap(s, t, bl + kDX, 2.f + kDY, tl + kDX, kDeck + kDY, 3.0f, c.back, clip);
    cap(s, t, -bl + kDX, 2.f + kDY, -tl + kDX, kDeck + kDY, 3.0f, c.back, clip);
    // Back face girts, seen between the front legs.
    for (int g = 1; g <= 2; ++g) {
      const float y = g == 1 ? -0.5f * fl(LK_LEG_H) : kDeck + 3.f;
      const float hw = legHW(y);
      cap(s, t, -hw + kDX, y + kDY, hw + kDX, y + kDY, 1.4f, c.back, clip);
    }
    // -- west face bracing (front-left leg to back-left leg), two tiers.
    for (int i = 0; i < 3; ++i) {
      const float x = -legHW(lv[i]);
      cap(s, t, x, lv[i], x + kDX, lv[i] + kDY, 1.7f, c.brace, clip);
    }
    for (int i = 0; i < 2; ++i) {
      const float xa = -legHW(lv[i]), xb = -legHW(lv[i + 1]);
      cap(s, t, xa, lv[i], xb + kDX, lv[i + 1] + kDY, 1.4f, c.brace, clip);
      cap(s, t, xa + kDX, lv[i] + kDY, xb, lv[i + 1], 1.4f, c.brace, clip);
    }
    // -- front legs, girts and X bracing.
    cap(s, t, bl, 2.f, tl, kDeck, 3.4f, c.leg, clip);
    cap(s, t, -bl, 2.f, -tl, kDeck, 3.4f, c.leg, clip);
    for (int i = 0; i < 3; ++i) {
      const float hw = legHW(lv[i]);
      cap(s, t, -hw, lv[i], hw, lv[i], 1.8f, c.brace, clip);
    }
    for (int i = 0; i < 2; ++i) {
      const float ha = legHW(lv[i]), hb = legHW(lv[i + 1]);
      cap(s, t, -ha, lv[i], hb, lv[i + 1], 1.5f, c.brace, clip);
      cap(s, t, ha, lv[i], -hb, lv[i + 1], 1.5f, c.brace, clip);
    }
    // -- deck: underside sliver, then the front and west edges.
    quad(s, t.p(-kWalkHW, kDeck + 3.f), t.p(kWalkHW, kDeck + 3.f), t.p(kWalkHW + kDX, kDeck + 3.f + kDY),
         t.p(-kWalkHW + kDX, kDeck + 3.f + kDY), c.under, clip);
    quad(s, t.p(-kWalkHW, kDeck), t.p(-kWalkHW + kDX, kDeck + kDY), t.p(-kWalkHW + kDX, kDeck + 3.f + kDY),
         t.p(-kWalkHW, kDeck + 3.f), c.deck, clip);
    fillRectF(s, t.ox - kWalkHW, t.oy + kDeck, t.ox + kWalkHW, t.oy + kDeck + 3.f, c.deck, clip);
    // -- stair: three flights' treads and two landings' floors, in front of the frame.
    drawFlight(s, t, kStair[0], kStair[1], c, clip);
    drawFlight(s, t, kStair[2], kStair[3], c, clip);
    drawFlight(s, t, kStair[4], kStair[5], c, clip);
    for (int l = 0; l < 2; ++l) {
      const float* a = kStair[1 + 2 * l];
      const float* b = kStair[2 + 2 * l];
      const float x0 = minf(a[0], b[0]), x1 = maxf(a[0], b[0]);
      fillRectF(s, t.ox + x0 - 1.f, t.oy + a[1], t.ox + x1 + 1.f, t.oy + a[1] + 3.f, c.stair, clip);
    }
    // -- rails behind the cab: north and east sides.
    {
      const float yb = kRail + kDY;
      cap(s, t, -kWalkHW + kDX, yb, kWalkHW + kDX, yb, 1.4f, c.back, clip);
      cap(s, t, kWalkHW, kRail, kWalkHW + kDX, yb, 1.4f, c.back, clip);
      cap(s, t, kWalkHW + kDX, kDeck + kDY, kWalkHW + kDX, yb, 1.2f, c.back, clip);
    }
    // -- the west wall, and inside the cab: the far windows against the sky
    // through the middle and right panes, and the fire-finder table.
    const Vec2 wFT = t.p(-kCabHW, kEave), wFB = t.p(-kCabHW, kDeck);
    const Vec2 wBT = t.p(-kCabHW + kDX, kEave + kDY), wBB = t.p(-kCabHW + kDX, kDeck + kDY);
    quad(s, wFT, wBT, wBB, wFB, c.wall_west, clip);
    if (c.windows) {
      for (int i = 0; i < 2; ++i)
        fillRectF(s, t.ox + kHole[i][0], t.oy + kHole[i][1], t.ox + kHole[i][2], t.oy + kHole[i][3], c.through, clip);
      const float fy = kDeck - 24.f;   // finder table top, about waist high on a 44 px hiker
      fillRectF(s, t.ox - 1.2f, t.oy + fy, t.ox + 1.2f, t.oy + kWin1, c.finder, clip);
      fillEllipse(s, t.p(0.f, fy), 6.5f, 1.6f, c.finder, clip);
      fillRectF(s, t.ox - 6.f, t.oy + fy - 4.f, t.ox - 4.8f, t.oy + fy, c.finder, clip);   // sight vanes
      fillRectF(s, t.ox + 4.8f, t.oy + fy - 3.f, t.ox + 6.f, t.oy + fy, c.finder, clip);
      cap(s, t, -5.4f, fy - 2.5f, 5.4f, fy - 1.6f, 0.9f, c.finder, clip);                  // the sighting wire
    }
  }

  if (mask & LKL_BIT(LKL_CAB)) drawCab(t, s, c, clip);

  if (mask & LKL_BIT(LKL_RAILS)) {
    // -- stair handrails, landing posts and rails.
    drawFlightRail(s, t, kStair[0], kStair[1], c, clip);
    drawFlightRail(s, t, kStair[2], kStair[3], c, clip);
    drawFlightRail(s, t, kStair[4], kStair[5], c, clip);
    for (int l = 0; l < 2; ++l) {
      const float* a = kStair[1 + 2 * l];
      const float* b = kStair[2 + 2 * l];
      const float x0 = minf(a[0], b[0]), x1 = maxf(a[0], b[0]);
      const float xo = l == 0 ? x1 : x0;   // outer end: post and short rail
      cap(s, t, xo, a[1], xo, a[1] - kHand, 1.1f, c.stair, clip);
      cap(s, t, x0, a[1] - kHand, x1, a[1] - kHand, 1.3f, c.stair, clip);
    }
    // -- rails in front of the cab: west side and front (gap for the stair).
    const float xw0 = -kWalkHW, xw1 = -kWalkHW + kDX;
    cap(s, t, xw0, kRail, xw1, kRail + kDY, 1.5f, c.rail_west, clip);
    cap(s, t, xw0, kRail + 10.f, xw1, kRail + 10.f + kDY, 1.1f, c.rail_west, clip);
    cap(s, t, xw1, kDeck + kDY, xw1, kRail + kDY, 1.3f, c.rail_west, clip);
    const float posts[5] = {-kWalkHW, -13.f, kGap0, kGap1, kWalkHW};
    for (float px : posts) cap(s, t, px, kDeck, px, kRail, 1.3f, c.rail_front, clip);
    cap(s, t, -kWalkHW, kRail, kGap0, kRail, 1.5f, c.rail_front, clip);
    cap(s, t, kGap1, kRail, kWalkHW, kRail, 1.5f, c.rail_front, clip);
    cap(s, t, -kWalkHW, kRail + 10.f, kGap0, kRail + 10.f, 1.1f, c.rail_front, clip);
    cap(s, t, kGap1, kRail + 10.f, kWalkHW, kRail + 10.f, 1.1f, c.rail_front, clip);
  }
}

// Mood folded into [0, 6).
float foldMood(float mood) {
  float m = mood - 6.f * floorf(mood / 6.f);
  if (!(m >= 0.f && m < 6.f)) m = 0.f;
  return m;
}

// How far the timber has gone to silhouette: 0 by day and golden hour, 1 from
// mood 5.8 through night to dawn.
float silhouetteAmount(float m) {
  if (m >= 5.f) return window(m, 5.f, 5.8f);
  if (m < 1.f) return 1.f - window(m, 0.6f, 1.f);
  return 0.f;
}

// Window glint: rises through the afternoon, full 4.0 to 4.6, gone by 5.4.
float glintAmount(float m) { return window(m, 3.2f, 4.f) * (1.f - window(m, 4.6f, 5.4f)); }

void towerColours(const Palette& pal, float mood, float reveal, TowerCols& c) {
  const float m = foldMood(mood);
  const float sil = silhouetteAmount(m);
  const float gl = glintAmount(m);
  const Pixel A = pal[ACTOR], W = pal[TRUNK];
  const Pixel lit = mixRGB(W, A, sil);
  const Pixel front = mixRGB(W, A, 0.3f + 0.7f * sil);
  const Pixel back = mixRGB(W, A, 0.55f + 0.45f * sil);
  const Pixel roof_f = mixRGB(W, A, 0.5f + 0.5f * sil);

  // Staged reveal: below 0.5 everything is the flat ACTOR silhouette; then the
  // frame (legs, bracing, stair), then the cab, deck, rails and roof, then the
  // windows switch from their slots, one stage at a time.
  const float k = reveal < 0.5f ? -1.f : (reveal - 0.5f) * 2.f;
  const bool frame = k >= 0.2f, cab = k >= 0.5f, win = k >= 0.8f;
  c.leg = frame ? lit : A;
  c.brace = frame ? front : A;
  c.back = frame ? back : A;
  c.stair = frame ? lit : A;
  c.deck = cab ? lit : A;
  c.under = cab ? back : A;
  c.wall_front = cab ? front : A;
  c.wall_west = cab ? lit : A;
  c.roof_front = cab ? roof_f : A;
  c.roof_west = cab ? lit : A;
  c.rail_front = cab ? front : A;
  c.rail_west = cab ? lit : A;
  c.glass = A;
  c.windows = win;
  c.glass_lit = mixRGB(A, pal[SKY4], 0.3f * gl);
  c.glint = mixRGB(A, pal[SKY4], gl);
  c.finder = A;
  // The sky seen through the cab: the band behind the window line.
  c.through = pal[SKY1];
}

// Stable 0..1 per position (as buildup.cpp's), so a plant pops at the same
// moment in every strip.
float hash01(float x, float y) {
  uint32_t h = (uint32_t)(int32_t)x * 73856093u ^ (uint32_t)(int32_t)y * 19349663u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return (float)(h & 0xFFFFu) / 65536.f;
}

// A plant's scale in the build's plants phase u: popcorn order, overshoot.
float lookoutPlantScale(float u, float x, float y) {
  if (u >= 2.f) return 1.f;
  const float d = 0.62f * hash01(x, y);
  return easeOutBack(window(u, d, d + 0.12f));
}

}  // namespace

// ---- public ------------------------------------------------------------------

float lookoutHillAt(const LookoutLand& lk, float x) { return crestAt(lk.hill, x); }

float lookoutSkylineAt(const LookoutLand& lk, float x) {
  float y = crestAt(lk.hill, x);
  for (int l = 0; l < 3; ++l) y = minf(y, crestAt(lk.valley[l], x));
  // The near valley's tree line: tooth i rises from (x_i, crest) to its tip
  // over the middle of its span and back down (drawValleyTrees).
  const float* c = lk.valley[2];
  for (int i = 0; i < lk.n_spikes; ++i) {
    const float x0 = fl(lk.spikes[i].x), x1 = fl(lk.spikes[i + 1].x);
    if (x < x0 || x > x1) continue;
    const float xm = 0.5f * (x0 + x1);
    const float tip = crestAt(c, xm) - fl(lk.spikes[i].h);
    const float t = x < xm ? (x1 > x0 ? (x - x0) / (xm - x0) : 0.f) : (x1 > xm ? (x - xm) / (x1 - xm) : 0.f);
    y = minf(y, x < xm ? lerp(crestAt(c, x0), tip, t) : lerp(tip, crestAt(c, x1), t));
  }
  // The hill pines' tiers (drawHillPine at full size).
  for (int i = 0; i < lk.n_pines; ++i) {
    const LsPine& p = lk.pines[i];
    const float h = fl(p.h), base = fl(p.base_y);
    const float top = base - h;
    const float fbot = base - maxf(3.f, h * 0.16f);
    const float dx = fabsf(x - fl(p.x));
    for (int k = 0; k < p.tiers; ++k) {
      const Tier t = tierGeom(k, p.tiers, top, fbot, h, fl(p.width_pct));
      if (dx > t.half_w || t.half_w <= 0.f) continue;
      y = minf(y, t.apex_y + (t.base_y - t.apex_y) * dx / t.half_w);
    }
  }
  return y;
}

int lookoutSkyBand(int y) {
  int b = 0;
  while (b < 4 && y >= kBand[b + 1]) ++b;
  return b;
}

int lookoutSkyTop(const LookoutLand& lk, int x) {
  if (x < 0) x = 0;
  if (x >= SS_WIDTH) x = SS_WIDTH - 1;
  return lk.sky_top[x];
}

void lookoutLandGenerate(LookoutLand& lk, Rng& rng) {
  // Valley: three ridge layers like the camp's, a little lower so the
  // hillside's skyline stands clear above them.
  static const float kBase[3] = {236.f, 258.f, 286.f};
  static const float kAmp[3][3] = {{13.f, 6.f, 3.f}, {16.f, 8.f, 4.f}, {18.f, 9.f, 4.f}};
  static const float kWave[3][3] = {{330.f, 140.f, 55.f}, {260.f, 110.f, 45.f}, {210.f, 90.f, 40.f}};
  for (int L = 0; L < 3; ++L) {
    float ph[3];
    for (int j = 0; j < 3; ++j) ph[j] = rng.uniform();
    for (int i = 0; i < LS_CREST_N; ++i) {
      const float x = fl(LS_CREST_X0 + LS_CREST_STEP * i);
      float y = kBase[L];
      for (int j = 0; j < 3; ++j) y += kAmp[L][j] * sinT(x / kWave[L][j] + ph[j]);
      lk.valley[L][i] = y;
    }
  }

  // Tower spot and the ridge.
  lk.tower_x = (int16_t)rng.rangeInt(668, 700);
  lk.tower_gy = (int16_t)rng.rangeInt(180, 194);
  const float cx = fl(lk.tower_x), gy = fl(lk.tower_gy);
  const float y0 = rng.range(372.f, 384.f);
  const float xr_pick = rng.range(548.f, 588.f);
  const float xr = minf(xr_pick, cx - 84.f);   // plateau start, well left of the tower
  const float ph1 = rng.uniform();
  const float ph2 = rng.uniform();
  const float ph3 = rng.uniform();
  const float x0 = fl(LS_CREST_X0);
  for (int i = 0; i < LS_CREST_N; ++i) {
    const float x = fl(LS_CREST_X0 + LS_CREST_STEP * i);
    float y;
    if (x < xr) {
      const float t = (x - x0) / (xr - x0);
      const float sm = t * t * (3.f - 2.f * t);
      const float env = 4.f * t * (1.f - t);
      y = y0 + (gy - y0) * sm + env * (7.f * sinT(x / 170.f + ph1) + 3.f * sinT(x / 63.f + ph2));
    } else {
      // Flat under and beside the tower, a gentle roll elsewhere on the top.
      const float flat = clamp01((fabsf(x - (cx - 8.f)) - 76.f) / 40.f);
      y = gy + 2.5f * sinT(x / 110.f + ph3) * flat;
    }
    lk.hill[i] = y;
  }

  // Tree line along the near valley crest.
  lk.n_spikes = 0;
  int x = LS_CREST_X0;
  while (lk.n_spikes < LK_MAX_SPIKES - 1 && x < SS_WIDTH + LS_CREST_STEP) {
    lk.spikes[lk.n_spikes].x = (int16_t)x;
    lk.spikes[lk.n_spikes].h = (uint8_t)rng.rangeInt(6, 16);
    ++lk.n_spikes;
    x += rng.rangeInt(10, 16);
  }
  lk.spikes[lk.n_spikes].x = (int16_t)x;
  lk.spikes[lk.n_spikes].h = 0;

  generateTrail(lk, rng);
  generatePines(lk, rng);
  generateTufts(lk, rng);

  lk.fg_y[0] = rng.range(460.f, 468.f);
  lk.fg_y[1] = rng.range(452.f, 462.f);
  lk.fg_ph = rng.uniform();
  lk.sh_y[0] = rng.range(352.f, 366.f);
  lk.sh_y[1] = rng.range(300.f, 326.f);
  lk.sh_ph = rng.uniform();

  // The skyline per column: every knot of the crests, the hill and the tree
  // line sits on a whole or half pixel x and a pine tier's apex on a whole
  // one, so the column's lowest y is at one of x, x + 0.5, x + 1.
  for (int col = 0; col < SS_WIDTH; ++col) {
    const float fx = fl(col);
    const float sy = minf(lookoutSkylineAt(lk, fx), minf(lookoutSkylineAt(lk, fx + 0.5f), lookoutSkylineAt(lk, fx + 1.f)));
    const float yc = sy < 0.f ? 0.f : (sy > fl(SS_HEIGHT) ? fl(SS_HEIGHT) : sy);
    lk.sky_top[col] = (int16_t)floorf(yc);
  }
}

LkDraw lookoutDrawDone(float mood, float reveal, uint16_t layers) {
  LkDraw d;
  d.layers = layers;
  d.sky = 1.f;
  d.valley_drop[0] = d.valley_drop[1] = d.valley_drop[2] = 0.f;
  d.trail = 1.f;
  d.plants_u = 2.f;
  d.reveal = reveal;
  d.mood = mood;
  return d;
}

void lookoutLandDraw(const LookoutLand& lk, Surface& s, const Rect& clip_in, const Palette& pal, const LkDraw& d) {
  const Rect clip = clip_in.clipped(s.bounds());
  if (clip.empty()) return;
  const uint16_t L = d.layers;
  if (L & LKL_BIT(LKL_SKY)) {
    const float k = clamp01(d.sky);
    for (int b = 0; b < 5; ++b) {
      const Pixel c = k >= 1.f ? pal[(Slot)(SKY0 + b)] : mixRGB(pal[SKY4], pal[(Slot)(SKY0 + b)], k);
      fillRect(s, Rect{0, kBand[b], SS_WIDTH, kBand[b + 1]}, c, clip);
    }
  }
  if (L & LKL_BIT(LKL_VALLEY)) {
    for (int l = 0; l < 3; ++l) drawCrestFill(s, lk.valley[l], pal[(Slot)(RIDGE_FAR + l)], clip, d.valley_drop[l]);
    drawValleyTrees(lk, s, pal[TREES_FAR], clip, d.valley_drop[2]);
  }
  const Pixel upper = mixRGB(pal[GROUND], pal[RIDGE_NEAR], 0.3f);   // the farther slope
  if (L & LKL_BIT(LKL_HILL)) {
    drawCrestFill(s, lk.hill, upper, clip);
    drawShoulder(lk, s, pal[GROUND], clip);
    drawForeground(lk, s, pal[GROUND_DARK], clip);
  }
  // Trail: a lighter ground path on either tone, a little narrower as it climbs.
  if ((L & LKL_BIT(LKL_TRAIL)) && d.trail > 0.f)
    drawTrail(lk, s, mixRGB(pal[GROUND], pal[GRASS], 0.55f), mixRGB(upper, pal[GRASS], 0.55f), clip, d.trail);
  if ((L & LKL_BIT(LKL_PLANTS)) && d.plants_u >= 0.f) {
    for (int i = 0; i < lk.n_tufts; ++i) {
      const LsTuft& t = lk.tufts[i];
      drawTuft(t, s, pal[GRASS], clip, lookoutPlantScale(d.plants_u, fl(t.x), fl(t.y)));
    }
    const Pixel shade = mixRGB(pal[FOLIAGE_DARK], pal[ACTOR], 0.45f);
    for (int i = 0; i < lk.n_pines; ++i) {
      const LsPine& p = lk.pines[i];
      drawHillPine(s, p, pal[FOLIAGE_DARK], shade, pal[TRUNK], clip, lookoutPlantScale(d.plants_u, fl(p.x), fl(p.base_y)));
    }
  }
  const uint16_t tower = (uint16_t)(L & (LKL_BIT(LKL_TOWER) | LKL_BIT(LKL_CAB) | LKL_BIT(LKL_RAILS)));
  const float r = clamp01(d.reveal);
  if (tower && r > 0.f) {
    TowerCols c;
    towerColours(pal, d.mood, r, c);
    // Rising from behind the ridge: the whole tower plus a margin below the crest.
    const float h = fl(LK_LEG_H + LK_CAB_H + LK_ROOF_H + LK_POLE_H) + 10.f;
    const float rise = r < 0.5f ? (1.f - easeOutCubic(r * 2.f)) * h : 0.f;
    drawTower(lk, s, c, rise, clip, tower);
  }
}

void lookoutLandDrawStatic(const LookoutLand& lk, Surface& frame, const Palette& pal, float mood,
                           float tower_reveal) {
  lookoutLandDraw(lk, frame, frame.bounds(), pal, lookoutDrawDone(mood, tower_reveal, LKL_ALL));
}

float lookoutHeightAt(const LookoutLand& lk, float y) {
  const float y0 = lk.trail.p[0].y, y1 = fl(lk.tower_gy);
  return lerp(44.f, 90.f, clamp01((y - y1) / (y0 - y1)));
}

Rect lookoutCabBounds(const LookoutLand& lk) {
  const int ox = lk.tower_x, oy = lk.tower_gy;
  const int top = (int)floorf(kEave + kDY * 0.6f - fl(LK_ROOF_H) - fl(LK_POLE_H)) - 3;
  return Rect{ox + (int)floorf(-kCabHW - fl(LK_ROOF_OVER) + kDX) - 3, oy + top, ox + (int)kCabHW + LK_ROOF_OVER + 3,
              oy + (int)kDeck + 2};
}

Vec2 lookoutTrailAt(const LookoutLand& lk, float s) {
  const LkTrail& t = lk.trail;
  const float d = clamp01(s) * t.cum[t.n - 1];
  int i = 1;
  while (i < t.n - 1 && t.cum[i] < d) ++i;
  const float seg = t.cum[i] - t.cum[i - 1];
  const float f = seg > 1e-4f ? clamp01((d - t.cum[i - 1]) / seg) : 0.f;
  return Vec2{lerp(t.p[i - 1].x, t.p[i].x, f), lerp(t.p[i - 1].y, t.p[i].y, f)};
}

float lookoutTrailScale(float s) { return lerp(1.f, 0.49f, clamp01(s)); }

void lookoutStairKnots(const LookoutLand& lk, Vec2 knots[6], float kn_s[6]) {
  const float ox = fl(lk.tower_x), oy = fl(lk.tower_gy);
  float cum[6];
  cum[0] = 0.f;
  for (int i = 0; i < 6; ++i) {
    knots[i] = Vec2{ox + kStair[i][0], oy + kStair[i][1]};
    if (i > 0) {
      const float dx = kStair[i][0] - kStair[i - 1][0], dy = kStair[i][1] - kStair[i - 1][1];
      cum[i] = cum[i - 1] + sqrtf(dx * dx + dy * dy);
    }
  }
  for (int i = 0; i < 6; ++i) kn_s[i] = cum[i] / cum[5];
}

Vec2 lookoutStairAt(const LookoutLand& lk, float s) {
  Vec2 k[6];
  float ks[6];
  lookoutStairKnots(lk, k, ks);
  const float u = clamp01(s);
  int i = 1;
  while (i < 5 && ks[i] < u) ++i;
  const float seg = ks[i] - ks[i - 1];
  const float f = seg > 1e-6f ? clamp01((u - ks[i - 1]) / seg) : 0.f;
  return Vec2{lerp(k[i - 1].x, k[i].x, f), lerp(k[i - 1].y, k[i].y, f)};
}

Vec2 lookoutRailSeat(const LookoutLand& lk) {
  return Vec2{fl(lk.tower_x) - kWalkHW + 0.5f * kDX, fl(lk.tower_gy) + kDeck + 0.5f * kDY};
}

void lookoutCabPoints(const LookoutLand& lk, LookoutCabPoints& o) {
  const float ox = fl(lk.tower_x), oy = fl(lk.tower_gy);
  const float wy = 0.5f * (kWin0 + kWin1);
  o.peek_feet[0] = Vec2{ox - 19.f, oy + kDeck};
  o.peek_eye[0] = Vec2{ox - 19.f, oy + wy};
  o.peek_feet[1] = Vec2{ox + 19.f, oy + kDeck};
  o.peek_eye[1] = Vec2{ox + 19.f, oy + wy};
  // West catwalk, mid depth, between the wall and the rail.
  o.peek_feet[2] = Vec2{ox - 0.5f * (kCabHW + kWalkHW) + 0.5f * kDX, oy + kDeck + 0.5f * kDY};
  o.peek_eye[2] = Vec2{ox - kCabHW + 0.5f * kDX, oy + wy + 0.5f * kDY};
  o.finder = Vec2{ox, oy + kDeck - 24.f};
  o.finder_feet = Vec2{ox - 4.f, oy + kDeck + 0.5f * kDY};
  o.compass = Vec2{ox + 31.f, oy + kRail};
  o.compass_feet = Vec2{ox + 31.f, oy + kDeck};
  o.stair_head = Vec2{ox + kStair[5][0], oy + kStair[5][1]};
  o.deck_y = oy + kDeck;
  o.rail_y = oy + kRail;
}

Rect lookoutTowerBounds(const LookoutLand& lk) {
  const int ox = lk.tower_x, oy = lk.tower_gy;
  const int top = (int)floorf(kEave + kDY * 0.6f - fl(LK_ROOF_H) - fl(LK_POLE_H)) - 3;
  const int left = (int)floorf(-kWalkHW + kDX - 3.f);
  const int right = LK_LEG_BASE_HW + 3;
  return Rect{ox + left, oy + top, ox + right, oy};
}

}  // namespace ss
