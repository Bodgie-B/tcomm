// Landscape generation and drawing. See landscape.h.
//
// Everything is flat silhouettes in palette slots. Ridge crests are stored as
// samples every 16 px and every other layer (tree line, pines) reads the same
// samples through landscapeCrestAt, so trees stand exactly on the drawn edge.
#include "landscape.h"

#include <math.h>

#include "../anim/ease.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
inline int16_t toI16(float v) { return (int16_t)floorf(v + 0.5f); }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline int maxi(int a, int b) { return a > b ? a : b; }

// Clearing kept free of props for the tent and fire (later stages).
constexpr int kClearX0 = 200, kClearY0 = 335, kClearX1 = 460, kClearY1 = 425;

bool inClearing(float x, float y, float pad) {
  return x > fl(kClearX0) - pad && x < fl(kClearX1) + pad && y > fl(kClearY0) - pad &&
         y < fl(kClearY1) + pad;
}

// ---- stream --------------------------------------------------------------

// True when (x, y) lies inside the water polygon grown by `margin` px.
bool streamContains(const Landscape& ls, float x, float y, float margin) {
  for (int i = 0; i + 1 < LS_STREAM_PTS; ++i) {
    const LsStreamPt& a = ls.stream[i];
    const LsStreamPt& b = ls.stream[i + 1];
    if (x <= a.x && x >= b.x) {
      const float span = a.x - b.x;
      const float t = span > 0.f ? (a.x - x) / span : 0.f;
      const float cy = a.y + (b.y - a.y) * t;
      const float hw = a.hw + (b.hw - a.hw) * t;
      return fabsf(y - cy) < hw * 1.06f + margin;
    }
  }
  return false;
}

// Catmull-Rom point and derivative on segment p1..p2 with neighbours p0, p3.
void catmull(const float* p0, const float* p1, const float* p2, const float* p3, float t,
             float* pos, float* der) {
  const float t2 = t * t, t3 = t2 * t;
  for (int k = 0; k < 2; ++k) {
    const float a = 2.f * p1[k];
    const float b = p2[k] - p0[k];
    const float c = 2.f * p0[k] - 5.f * p1[k] + 4.f * p2[k] - p3[k];
    const float d = -p0[k] + 3.f * p1[k] - 3.f * p2[k] + p3[k];
    pos[k] = 0.5f * (a + b * t + c * t2 + d * t3);
    der[k] = 0.5f * (b + 2.f * c * t + 3.f * d * t2);
  }
}

void generateStream(Landscape& ls, Rng& rng) {
  // Control points follow the brief; the first and last sit outside the frame
  // so the water polygon always reaches both edges. Interior y is jittered.
  float cp[8][2] = {{860.f, 370.f}, {800.f, 372.f}, {640.f, 386.f}, {470.f, 420.f},
                    {300.f, 448.f}, {120.f, 476.f}, {-20.f, 504.f}, {-80.f, 519.f}};
  for (int i = 2; i <= 5; ++i) cp[i][1] += rng.range(-5.f, 5.f);

  for (int i = 0; i < LS_STREAM_PTS; ++i) {
    const float u = fl(i) / fl(LS_STREAM_PTS - 1) * 7.f;
    int seg = (int)u;
    if (seg > 6) seg = 6;
    const float t = u - fl(seg);
    const float* p0 = cp[seg > 0 ? seg - 1 : 0];
    const float* p3 = cp[seg + 2 < 8 ? seg + 2 : 7];
    float pos[2], der[2];
    catmull(p0, cp[seg], cp[seg + 1], p3, t, pos, der);
    float len = sqrtf(der[0] * der[0] + der[1] * der[1]);
    if (len < 1e-3f) len = 1.f;
    float nx = der[1] / len, ny = -der[0] / len;
    if (ny > 0.f) { nx = -nx; ny = -ny; }   // normal points up, toward the far bank
    LsStreamPt& s = ls.stream[i];
    s.x = pos[0];
    s.y = pos[1];
    s.nx = nx;
    s.ny = ny;
    const float wt = clamp01((pos[0] + 20.f) / 820.f);   // 0 at the left, 1 at the right
    s.hw = 0.5f * lerp(64.f, 40.f, wt);
  }
  const float ph = rng.uniform();
  for (int i = 0; i < LS_STREAM_PTS; ++i) ls.bank_w[i] = 11.f + 3.f * sinT(fl(i) * 0.11f + ph);

  ls.n_ripples = rng.rangeInt(10, LS_MAX_RIPPLES);
  for (int r = 0; r < ls.n_ripples; ++r) {
    const float u = rng.range(0.8f, 12.2f);
    const int i = (int)u;
    const float f = u - fl(i);
    const LsStreamPt& a = ls.stream[i];
    const LsStreamPt& b = ls.stream[i + 1];
    float dx = b.x - a.x, dy = b.y - a.y;
    float dl = sqrtf(dx * dx + dy * dy);
    if (dl < 1e-3f) dl = 1.f;
    dx /= dl;
    dy /= dl;
    const float hw = lerp(a.hw, b.hw, f);
    const float off = rng.range(-0.45f, 0.45f) * hw;
    const float cx = lerp(a.x, b.x, f) + lerp(a.nx, b.nx, f) * off;
    const float cy = lerp(a.y, b.y, f) + lerp(a.ny, b.ny, f) * off;
    const float half = 0.5f * rng.range(10.f, 24.f);
    ls.ripples[r].a = Vec2{cx - dx * half, cy - dy * half};
    ls.ripples[r].b = Vec2{cx + dx * half, cy + dy * half};
  }
}

void generateRocks(Landscape& ls, Rng& rng) {
  static const float kShape[6][2] = {{-1.0f, 0.5f},  {-0.8f, -0.35f}, {-0.25f, -0.85f},
                                     {0.45f, -0.75f}, {0.95f, -0.1f},  {0.85f, 0.5f}};
  const int want = rng.rangeInt(4, LS_MAX_ROCKS - 1);
  ls.n_rocks = 0;
  for (int tries = 0; tries < 400 && ls.n_rocks < want; ++tries) {
    const float u = rng.range(2.f, 11.5f);
    const int i = (int)u;
    const float f = u - fl(i);
    const LsStreamPt& a = ls.stream[i];
    const LsStreamPt& b = ls.stream[i + 1];
    const float w = rng.range(10.f, 26.f);
    const float h = w * 0.5f;
    const float side = rng.chance(0.5f) ? 1.f : -1.f;
    const float hw = lerp(a.hw, b.hw, f);
    const float d = hw + w * 0.25f + rng.range(2.f, 8.f);
    const float px = lerp(a.x, b.x, f) + lerp(a.nx, b.nx, f) * side * d;
    const float py = lerp(a.y, b.y, f) + lerp(a.ny, b.ny, f) * side * d;   // rock base line
    if (px - w * 0.5f < 4.f || px + w * 0.5f > fl(SS_WIDTH - 4)) continue;
    if (py > 476.f || py - h < fl(LS_HORIZON)) continue;
    if (inClearing(px, py, w * 0.5f + 2.f)) continue;
    if (px + w * 0.5f + 2.f >= fl(LS_QUIET_X0) && py + 2.f >= fl(LS_QUIET_Y0) - h) continue;
    LsRock& r = ls.rocks[ls.n_rocks++];
    for (int k = 0; k < 6; ++k) {
      r.p[k].x = px + (kShape[k][0] + rng.range(-0.08f, 0.08f)) * w * 0.5f;
      r.p[k].y = py + (kShape[k][1] + rng.range(-0.08f, 0.08f)) * h - h * 0.5f;
    }
  }
}

void generateTufts(Landscape& ls, Rng& rng) {
  const int want = rng.rangeInt(40, 60);
  ls.n_tufts = 0;
  for (int tries = 0; tries < 800 && ls.n_tufts < want; ++tries) {
    float x, y;
    if (rng.chance(0.55f)) {   // along the bottom of the frame
      x = rng.range(4.f, 796.f);
      y = rng.range(436.f, 478.f);
    } else {                   // just outside the clearing edge
      const int edge = rng.rangeInt(0, 2);
      if (edge == 0) { x = rng.range(200.f, 460.f); y = rng.range(324.f, 333.f); }
      else if (edge == 1) { x = rng.range(186.f, 198.f); y = rng.range(340.f, 424.f); }
      else { x = rng.range(462.f, 474.f); y = rng.range(340.f, 424.f); }
    }
    if (streamContains(ls, x, y, 5.f)) continue;
    if (x + 10.f >= fl(LS_QUIET_X0) && y >= fl(LS_QUIET_Y0)) continue;   // quiet corner
    LsTuft& t = ls.tufts[ls.n_tufts++];
    t.dyn = 0;
    t.x = toI16(x);
    t.y = toI16(y);
    t.n = (uint8_t)rng.rangeInt(2, 3);
    for (int k = 0; k < 3; ++k) {
      t.h[k] = (uint8_t)rng.rangeInt(8, 18);
      t.off[k] = (int8_t)((k - 1) * 3 + rng.rangeInt(-1, 1));
      t.lean[k] = (int8_t)rng.rangeInt(-4, 4);
    }
  }
}

void generateStars(Landscape& ls, Rng& rng) {
  for (int i = 0; i < LS_STAR_COUNT; ++i) {
    int x = 0, y = 0;
    for (int tries = 0; tries < 20; ++tries) {
      x = rng.rangeInt(10, SS_WIDTH - 10);
      y = rng.rangeInt(10, 195);
      const float dx = fl(x - 640), dy = fl(y - 110);
      if (dx * dx + dy * dy > 40.f * 40.f) break;   // keep clear of the moon
    }
    ls.stars[i].x = (int16_t)x;
    ls.stars[i].y = (int16_t)y;
    ls.stars[i].r = rng.range(1.2f, 2.0f);
  }
}

// The mushroom: in the foreground right of center, on dry ground, clear of
// rocks and of the reserved corner. Drawn ~14 px wide, ~14 px tall.
void generateMushroom(Landscape& ls, Rng& rng) {
  float x = 560.f, y = 462.f;
  for (int tries = 0; tries < 60; ++tries) {
    x = rng.range(470.f, 630.f);
    y = rng.range(450.f, 470.f);
    if (streamContains(ls, x, y - 7.f, 10.f)) continue;
    bool clear = true;
    for (int r = 0; r < ls.n_rocks && clear; ++r) {
      const float dx = ls.rocks[r].p[2].x - x, dy = ls.rocks[r].p[0].y - y;
      if (dx * dx + dy * dy < 24.f * 24.f) clear = false;
    }
    if (clear) break;
  }
  ls.mush_x = toI16(x);
  ls.mush_y = toI16(y);
}

// ---- pines ---------------------------------------------------------------

struct Tier { float apex_y, base_y, half_w; };

// Tier i of n, 0 = top. Tiers share one height, their apexes step down evenly
// and the lowest base sits at `fbot`. Tier widths follow the brief:
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

void drawTierTriangle(Surface& s, float cx, const Tier& t, Pixel col, const Rect& clip) {
  const Vec2 p[3] = {{cx, t.apex_y}, {cx - t.half_w, t.base_y}, {cx + t.half_w, t.base_y}};
  fillPolygon(s, p, 3, col, clip);
}

// Width reference while a pine stands up: a little narrow while it grows, full
// width from the moment it reaches its height (the overshoot only stretches).
inline float standWidth(float h, float hs) { return h * (hs < 1.f ? 0.8f + 0.2f * hs : 1.f); }

// Single-colour pine: trunk capsule plus stacked triangles, lowest tier first
// so upper tiers sit in front. `hs` scales the height about the base (build-up).
void drawPine(Surface& s, const LsPine& p, Pixel foliage, Pixel trunk, float trunk_w,
              float trunk_frac, const Rect& clip, float hs = 1.f) {
  if (hs <= 0.01f) return;
  const float h = fl(p.h) * hs;
  const float top = fl(p.base_y) - h;
  const float fbot = fl(p.base_y) - maxf(4.f, h * trunk_frac);
  const float cx = fl(p.x);
  const float wh = standWidth(fl(p.h), hs);
  strokeCapsule(s, Vec2{cx, fbot - 3.f}, Vec2{cx, fl(p.base_y)}, trunk_w, trunk, clip);
  for (int i = p.tiers - 1; i >= 0; --i) {
    drawTierTriangle(s, cx, tierGeom(i, p.tiers, top, fbot, wh, fl(p.width_pct)), foliage, clip);
  }
}

// The bag-hanging pine: bare lower trunk, lit and dark foliage, one branch.
// `hs` scales every height about the base (build-up).
void drawCampPine(Surface& s, const Landscape& ls, const Palette& pal, const Rect& clip, float hs) {
  if (hs <= 0.01f) return;
  const LsPine& p = ls.camp;
  const float cx = fl(p.x);
  const float base = fl(p.base_y);
  const float h = fl(p.h);
  const float top = base - h * hs;
  const float fbot = base - (base - 318.f) * hs;   // lowest tier base; the trunk shows only below it
  const float wh = standWidth(h, hs);
  strokeCapsule(s, Vec2{cx, top + 30.f * hs}, Vec2{cx, base}, 9.f, pal[TRUNK], clip);
  // The bag branch leaves the trunk inside the upper tiers, so its root is
  // hidden by foliage, and pokes out past the canopy to the left, rising
  // 10 px over 82 px (landscape.h). The food bag hangs from its tip.
  strokeCapsule(s, Vec2{cx, base - (base - (float)LS_BRANCH_ROOT_Y) * hs},
                Vec2{cx - (float)LS_BRANCH_LEN, base - (base - (float)LS_BRANCH_TIP_Y) * hs}, 5.f, pal[TRUNK], clip);
  for (int i = p.tiers - 1; i >= 0; --i) {
    const Tier t = tierGeom(i, p.tiers, top, fbot, wh, fl(p.width_pct));
    drawTierTriangle(s, cx, t, pal[FOLIAGE], clip);
    // Dark wedge on the lower left half: the lit side is the right.
    const float th = t.base_y - t.apex_y;
    const Vec2 q[4] = {{cx - t.half_w * 0.35f, t.apex_y + th * 0.35f},
                       {cx - t.half_w, t.base_y},
                       {cx, t.base_y},
                       {cx, t.apex_y + th * 0.55f}};
    fillPolygon(s, q, 4, pal[FOLIAGE_DARK], clip);
  }
}

// ---- sun and moon --------------------------------------------------------

// Sky body path over the mood axis. Segment [m0, m1) interpolates linearly from
// the first to the second position. The sun sinks out of sight between 5 and 5.5
// and the moon rises between 5.5 and 6; at night the moon sinks between 0 and 0.5
// and the sun rises between 0.5 and 1.
struct BodySeg { float m0, m1, x0, y0, r0, x1, y1, r1; };
const BodySeg kBody[8] = {
    {0.0f, 0.5f, 640.f, 110.f, 16.f, 640.f, 300.f, 16.f},   // moon sinks
    {0.5f, 1.0f, 180.f, 300.f, 30.f, 180.f, 225.f, 30.f},   // sun rises (dawn)
    {1.0f, 2.0f, 180.f, 225.f, 30.f, 520.f, 170.f, 30.f},   // dawn to morning
    {2.0f, 3.0f, 520.f, 170.f, 30.f, 600.f, 120.f, 30.f},   // morning to day
    {3.0f, 4.0f, 600.f, 120.f, 30.f, 640.f, 215.f, 36.f},   // day to golden
    {4.0f, 5.0f, 640.f, 215.f, 36.f, 660.f, 245.f, 36.f},   // golden to dusk
    {5.0f, 5.5f, 660.f, 245.f, 36.f, 660.f, 330.f, 36.f},   // sun sinks behind the ridges
    {5.5f, 6.0f, 640.f, 300.f, 16.f, 640.f, 110.f, 16.f},   // moon rises
};

void drawSkyBody(const Landscape& ls, Surface& s, const Palette& pal, float mood, const Rect& clip) {
  const float m = mood - 6.f * floorf(mood / 6.f);   // wrap into [0, 6)
  int k = 0;
  while (k < 7 && m >= kBody[k].m1) ++k;
  const BodySeg& g = kBody[k];
  const float f = clamp01((m - g.m0) / (g.m1 - g.m0));
  const Vec2 c{lerp(g.x0, g.x1, f), lerp(g.y0, g.y1, f)};
  const float r = lerp(g.r0, g.r1, f);

  // Stars fade with distance from night: full at mood 0 / 6, gone at 0.5 / 5.5.
  const float dn = m < 3.f ? m : 6.f - m;
  const float fade = clamp01(1.f - 2.f * dn);
  if (fade > 0.f) {
    for (int i = 0; i < LS_STAR_COUNT; ++i) {
      const int y = ls.stars[i].y;
      const int band = y < 58 ? 0 : (y < 118 ? 1 : (y < 178 ? 2 : (y < 228 ? 3 : 4)));
      const Pixel col = mixRGB(pal[(Slot)(SKY0 + band)], pal[STAR], fade);
      fillCircle(s, Vec2{fl(ls.stars[i].x), fl(y)}, ls.stars[i].r, col, clip);
    }
  }
  fillCircle(s, c, r, pal[SUN], clip);
}

// ---- ridges and trees ----------------------------------------------------

// `drop` 0..1 sinks the ridge until its highest point is just under the ground
// line (build-up); 0 is in place.
void drawRidge(const Landscape& ls, Surface& s, int layer, Pixel col, const Rect& clip, float drop) {
  float dy = 0.f;
  if (drop > 0.f) {
    float top = ls.crest[layer][0];
    for (int i = 1; i < LS_CREST_N; ++i) top = minf(top, ls.crest[layer][i]);
    dy = drop * (fl(LS_GROUND_Y) + 2.f - top);
    if (dy >= fl(LS_GROUND_Y) + 2.f - top) return;   // fully hidden behind the ground
  }
  Vec2 p[LS_CREST_N + 2];
  for (int i = 0; i < LS_CREST_N; ++i) {
    p[i] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * i), ls.crest[layer][i] + dy};
  }
  p[LS_CREST_N] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * (LS_CREST_N - 1)), fl(SS_HEIGHT + 2)};
  p[LS_CREST_N + 1] = Vec2{fl(LS_CREST_X0), fl(SS_HEIGHT + 2)};
  fillPolygon(s, p, LS_CREST_N + 2, col, clip);
}

// Serrated line along the near ridge. A polygon of m teeth has 2m+1 points on
// top and m+1 along a lower edge 4 px under the crest: 3m+2 <= 64, so m <= 20.
// During the build-up each tooth stands up with buildTreeScale at its x; the
// lower edge grows with it so nothing shows before the first tooth rises.
void drawFarTrees(const Landscape& ls, Surface& s, Pixel col, const Rect& clip, const BuildState& b) {
  const bool anim = !(b.built & BL_BIT(BL_FAR_TREES));
  const int kMaxTeeth = 20;
  int i = 0;
  while (i < ls.n_spikes) {
    int m = ls.n_spikes - i;
    if (m > kMaxTeeth) m = kMaxTeeth;
    Vec2 p[SS_MAX_POLY_POINTS];
    int n = 0;
    for (int j = 0; j < m; ++j) {
      const float x0 = fl(ls.spikes[i + j].x);
      const float x1 = fl(ls.spikes[i + j + 1].x);
      const float xm = 0.5f * (x0 + x1);
      const float hs = anim ? buildTreeScale(b, xm) : 1.f;
      p[n++] = Vec2{x0, landscapeCrestAt(ls, 2, x0)};
      p[n++] = Vec2{xm, landscapeCrestAt(ls, 2, xm) - fl(ls.spikes[i + j].h) * hs};
    }
    const float xe = fl(ls.spikes[i + m].x);
    p[n++] = Vec2{xe, landscapeCrestAt(ls, 2, xe)};
    for (int j = m; j >= 0; --j) {
      const float x = fl(ls.spikes[i + j].x);
      const float depth = anim ? 4.f * minf(1.f, buildTreeScale(b, x)) : 4.f;
      p[n++] = Vec2{x, landscapeCrestAt(ls, 2, x) + depth};
    }
    fillPolygon(s, p, n, col, clip);
    i += m;
  }
}

}  // namespace

float landscapeCrestAt(const Landscape& ls, int layer, float x) {
  float f = (x - fl(LS_CREST_X0)) / fl(LS_CREST_STEP);
  if (f <= 0.f) return ls.crest[layer][0];
  if (f >= fl(LS_CREST_N - 1)) return ls.crest[layer][LS_CREST_N - 1];
  const int i = (int)f;
  f -= fl(i);
  return lerp(ls.crest[layer][i], ls.crest[layer][i + 1], f);
}

void landscapeGenerate(Landscape& ls, Rng& rng) {
  // Ridges: base plus three seeded sines.
  static const float kBase[3] = {238.f, 262.f, 290.f};
  static const float kAmp[3][3] = {{14.f, 7.f, 3.f}, {18.f, 9.f, 4.f}, {22.f, 11.f, 5.f}};
  static const float kWave[3][3] = {{330.f, 140.f, 55.f}, {260.f, 110.f, 45.f}, {210.f, 90.f, 40.f}};
  for (int L = 0; L < 3; ++L) {
    float ph[3];
    for (int j = 0; j < 3; ++j) ph[j] = rng.uniform();
    for (int i = 0; i < LS_CREST_N; ++i) {
      const float x = fl(LS_CREST_X0 + LS_CREST_STEP * i);
      float y = kBase[L];
      for (int j = 0; j < 3; ++j) y += kAmp[L][j] * sinT(x / kWave[L][j] + ph[j]);
      // The near ridge never dips under the ground line (y 318), so the flat
      // ground edge never cuts through the silhouette.
      if (L == 2 && y > 314.f) y = 314.f;
      ls.crest[L][i] = y;
    }
  }

  // Far tree line teeth: x of each tooth start; the last entry only closes the polygon.
  ls.n_spikes = 0;
  int x = LS_CREST_X0;
  while (ls.n_spikes < LS_MAX_SPIKES - 1 && x < SS_WIDTH + LS_CREST_STEP) {
    ls.spikes[ls.n_spikes].x = (int16_t)x;
    ls.spikes[ls.n_spikes].h = (uint8_t)rng.rangeInt(8, 22);
    ++ls.n_spikes;
    x += rng.rangeInt(12, 18);
  }
  ls.spikes[ls.n_spikes].x = (int16_t)x;
  ls.spikes[ls.n_spikes].h = 0;

  // Mid pines, stratified across the width, then sorted shortest first.
  const int want = rng.rangeInt(14, LS_MAX_MID_PINE);
  ls.n_mid = 0;
  for (int i = 0; i < want; ++i) {
    const float px = (fl(i) + rng.uniform()) * fl(SS_WIDTH) / fl(want);
    LsPine p;
    p.x = toI16(px);
    p.h = (int16_t)rng.rangeInt(36, 80);
    int base = rng.rangeInt(300, 340);
    // Never let a trunk start above the near ridge crest, or the tree floats.
    base = maxi(base, (int)ceilf(landscapeCrestAt(ls, 2, px)) + 3);
    p.base_y = (int16_t)base;
    p.tiers = (uint8_t)rng.rangeInt(3, 4);
    p.width_pct = (uint8_t)rng.rangeInt(55, 75);
    if (px + 0.5f * fl(p.width_pct) * 0.01f * fl(p.h) >= fl(LS_QUIET_X0) && base >= LS_QUIET_Y0) continue;
    ls.mid[ls.n_mid++] = p;
  }
  for (int i = 1; i < ls.n_mid; ++i) {   // insertion sort by height
    const LsPine key = ls.mid[i];
    int j = i - 1;
    while (j >= 0 && ls.mid[j].h > key.h) { ls.mid[j + 1] = ls.mid[j]; --j; }
    ls.mid[j + 1] = key;
  }

  generateStream(ls, rng);

  // Campsite pine and the two foreground pines.
  ls.camp_x = (int16_t)(560 + rng.rangeInt(-10, 10));
  ls.camp = LsPine{ls.camp_x, 340, 170, 5, (uint8_t)rng.rangeInt(62, 70)};
  ls.fg[0] = LsPine{(int16_t)(20 + rng.rangeInt(-6, 6)), 440, 220, 5, (uint8_t)rng.rangeInt(55, 62)};
  ls.fg[1] = LsPine{(int16_t)(70 + rng.rangeInt(-6, 6)), 470, 260, 5, (uint8_t)rng.rangeInt(55, 62)};

  // Keep the foreground trunks on dry ground: the stream runs under the left
  // edge, so lift a base that would stand in the water to just above the bank.
  for (int k = 0; k < 2; ++k) {
    LsPine& p = ls.fg[k];
    for (int i = 0; i + 1 < LS_STREAM_PTS; ++i) {
      const LsStreamPt& a = ls.stream[i];
      const LsStreamPt& b = ls.stream[i + 1];
      const float fx = fl(p.x);
      if (fx <= a.x && fx >= b.x) {
        const float t = (a.x - fx) / (a.x - b.x);
        const float top = lerp(a.y, b.y, t) - lerp(a.hw, b.hw, t) * 1.06f;
        if (fl(p.base_y) > top - 2.f) p.base_y = (int16_t)floorf(top - 2.f);
        break;
      }
    }
  }

  generateRocks(ls, rng);
  generateTufts(ls, rng);
  generateStars(ls, rng);
  generateMushroom(ls, rng);   // last, so earlier draws keep the stage 1 places per seed
}

bool landscapeStreamSpan(const Landscape& ls, float x, float& cy, float& half) {
  for (int i = 0; i + 1 < LS_STREAM_PTS; ++i) {
    const LsStreamPt& a = ls.stream[i];
    const LsStreamPt& b = ls.stream[i + 1];
    if (x <= a.x && x >= b.x) {
      const float span = a.x - b.x;
      const float t = span > 0.f ? (a.x - x) / span : 0.f;
      cy = a.y + (b.y - a.y) * t;
      half = (a.hw + (b.hw - a.hw) * t) * 1.06f;
      return true;
    }
  }
  return false;
}

Rect landscapeFgPineBounds(const Landscape& ls, int k) {
  const LsPine& p = ls.fg[k ? 1 : 0];
  const int hw = (int)ceilf(0.5f * fl(p.width_pct) * 0.01f * fl(p.h)) + 2;
  return Rect{p.x - hw, p.base_y - p.h - 2, p.x + hw, p.base_y + 6};
}

void landscapeDrawFgPine(const Landscape& ls, int k, Surface& s, const Rect& clip, const Palette& pal) {
  if (k) drawPine(s, ls.fg[1], pal[FOLIAGE_DARK], pal[TRUNK], 8.f, 0.28f, clip);
  else drawPine(s, ls.fg[0], pal[FOLIAGE_DARK], pal[TRUNK], 7.f, 0.28f, clip);
}

LsStreamPt landscapeStreamAt(const Landscape& ls, float s) {
  const float last = fl(LS_STREAM_PTS - 1);
  s = s < 0.f ? 0.f : (s > last ? last : s);
  int i = (int)s;
  if (i > LS_STREAM_PTS - 2) i = LS_STREAM_PTS - 2;
  const float f = s - fl(i);
  const LsStreamPt& a = ls.stream[i];
  const LsStreamPt& b = ls.stream[i + 1];
  LsStreamPt r;
  r.x = lerp(a.x, b.x, f);
  r.y = lerp(a.y, b.y, f);
  r.hw = lerp(a.hw, b.hw, f);
  const float nx = lerp(a.nx, b.nx, f), ny = lerp(a.ny, b.ny, f);
  float len = sqrtf(nx * nx + ny * ny);
  if (len < 1e-4f) len = 1.f;
  r.nx = nx / len;
  r.ny = ny / len;
  return r;
}

float landscapeStreamTip(float reveal) {
  return clamp01(reveal) * (fl(LS_STREAM_PTS - 1) + LS_STREAM_TAPER);
}

namespace {

float bankAt(const Landscape& ls, float s) {
  const float last = fl(LS_STREAM_PTS - 1);
  s = s < 0.f ? 0.f : (s > last ? last : s);
  int i = (int)s;
  if (i > LS_STREAM_PTS - 2) i = LS_STREAM_PTS - 2;
  return lerp(ls.bank_w[i], ls.bank_w[i + 1], s - fl(i));
}

// A band along the stream between normal offsets lo = klo*hw + blo*bank and
// hi = khi*hw + bhi*bank, revealed from the right edge up to point index `tip`.
// Over the last LS_STREAM_TAPER units both offsets shrink to the centerline
// along a square-root profile, so the leading edge is a rounded tongue.
void drawStreamBand(const Landscape& ls, Surface& s, float tip, float klo, float blo, float khi,
                    float bhi, Pixel col, const Rect& clip) {
  const int kMax = LS_STREAM_PTS + 5;
  float sv[kMax], fv[kMax];
  int ns = 0;
  const float last = fl(LS_STREAM_PTS - 1);
  const float full_end = tip - LS_STREAM_TAPER;
  for (int i = 0; i < LS_STREAM_PTS && fl(i) < full_end; ++i) {
    sv[ns] = fl(i);
    fv[ns] = 1.f;
    ++ns;
  }
  for (int j = 0; j <= 4; ++j) {
    const float sj = full_end + LS_STREAM_TAPER * fl(j) * 0.25f;
    if (sj < 0.f || sj > last) continue;
    sv[ns] = sj;
    fv[ns] = sqrtf(1.f - fl(j) * 0.25f);
    ++ns;
  }
  if (ns < 2) return;
  Vec2 p[2 * kMax];
  int n = 0;
  for (int k = 0; k < ns; ++k) {
    const LsStreamPt q = landscapeStreamAt(ls, sv[k]);
    const float off = (khi * q.hw + bhi * bankAt(ls, sv[k])) * fv[k];
    p[n++] = Vec2{q.x + q.nx * off, q.y + q.ny * off};
  }
  for (int k = ns - 1; k >= 0; --k) {
    const LsStreamPt q = landscapeStreamAt(ls, sv[k]);
    const float off = (klo * q.hw + blo * bankAt(ls, sv[k])) * fv[k];
    p[n++] = Vec2{q.x + q.nx * off, q.y + q.ny * off};
  }
  fillPolygon(s, p, n, col, clip);
}

// Far bank strip and water body. `reveal` < 1 rolls the stream in from the right.
void drawStream(const Landscape& ls, Surface& frame, const Palette& pal, const Rect& clip, float reveal) {
  if (reveal <= 0.f) return;
  if (reveal < 1.f) {
    const float tip = landscapeStreamTip(reveal);
    drawStreamBand(ls, frame, tip, 0.8f, 0.f, 1.f, 1.f, pal[GROUND_DARK], clip);
    drawStreamBand(ls, frame, tip, -1.f, 0.f, 1.f, 0.f, pal[WATER], clip);
    return;
  }
  {
    Vec2 p[2 * LS_STREAM_PTS];
    int n = 0;
    for (int i = 0; i < LS_STREAM_PTS; ++i) {
      const LsStreamPt& s = ls.stream[i];
      p[n++] = Vec2{s.x + s.nx * s.hw * 0.8f, s.y + s.ny * s.hw * 0.8f};
    }
    for (int i = LS_STREAM_PTS - 1; i >= 0; --i) {
      const LsStreamPt& s = ls.stream[i];
      const float d = s.hw + ls.bank_w[i];
      p[n++] = Vec2{s.x + s.nx * d, s.y + s.ny * d};
    }
    fillPolygon(frame, p, n, pal[GROUND_DARK], clip);
  }
  {
    Vec2 p[2 * LS_STREAM_PTS];
    int n = 0;
    for (int i = 0; i < LS_STREAM_PTS; ++i) {
      const LsStreamPt& s = ls.stream[i];
      p[n++] = Vec2{s.x + s.nx * s.hw, s.y + s.ny * s.hw};
    }
    for (int i = LS_STREAM_PTS - 1; i >= 0; --i) {
      const LsStreamPt& s = ls.stream[i];
      p[n++] = Vec2{s.x - s.nx * s.hw, s.y - s.ny * s.hw};
    }
    fillPolygon(frame, p, n, pal[WATER], clip);
  }
}

// Sky bands, stars and sun or moon. `fade` < 1 pulls every sky slot toward the
// horizon color (the build-up starts from a flat SKY4 field). The lowest band
// runs down to the ground line so nothing shows behind the rising ridges.
void drawSky(const Landscape& ls, Surface& frame, const Palette& pal0, float mood, float fade, const Rect& clip) {
  Palette pal = pal0;
  if (fade < 1.f) {
    static const Slot kFade[6] = {SKY0, SKY1, SKY2, SKY3, SUN, STAR};
    for (Slot k : kFade) pal.c[k] = mixRGB(pal0[SKY4], pal0[k], fade);
  }
  static const int kBand[6] = {0, 58, 118, 178, 228, LS_GROUND_Y};
  for (int b = 0; b < 5; ++b) {
    fillRect(frame, Rect{0, kBand[b], SS_WIDTH, kBand[b + 1]}, pal[(Slot)(SKY0 + b)], clip);
  }
  drawSkyBody(ls, frame, pal, mood, clip);
}

// Rock scaled about its base center (build-up pop).
void drawRock(const LsRock& r, Surface& s, Pixel col, const Rect& clip, float sc) {
  if (sc <= 0.01f) return;
  if (sc == 1.f) {
    fillPolygon(s, r.p, 6, col, clip);
    return;
  }
  float cx = 0.f, by = r.p[0].y;
  for (int k = 0; k < 6; ++k) {
    cx += r.p[k].x;
    by = maxf(by, r.p[k].y);
  }
  cx *= 1.f / 6.f;
  Vec2 q[6];
  for (int k = 0; k < 6; ++k) q[k] = Vec2{cx + (r.p[k].x - cx) * sc, by + (r.p[k].y - by) * sc};
  fillPolygon(s, q, 6, col, clip);
}

void drawTuft(const LsTuft& t, Surface& s, Pixel col, const Rect& clip, float hs) {
  if (hs <= 0.01f) return;
  if (fl(t.y) < fl(clip.y0) || fl(t.y) - 22.f > fl(clip.y1)) return;
  for (int k = 0; k < t.n; ++k) {
    const float bx = fl(t.x + t.off[k]);
    const Vec2 tri[3] = {{bx - 1.5f, fl(t.y)}, {bx + 1.5f, fl(t.y)},
                         {bx + fl(t.lean[k]) * hs, fl(t.y) - fl(t.h[k]) * hs}};
    fillPolygon(s, tri, 3, col, clip);
  }
}

// The mushroom's entrance, p 0..1: the cap peeks out, holds a beat
// (anticipation), then the whole thing pops with an overshoot and the cap
// wobbles to rest. Pale stem, warm cap with two light spots.
void drawMushroom(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, float p) {
  if (p <= 0.f) return;
  float hs;
  if (p < 0.2f) hs = 0.3f * easeOutCubic(p / 0.2f);
  else if (p < 0.38f) hs = 0.3f;
  else hs = 0.3f + 0.7f * easeOutBack((p - 0.38f) / 0.62f);
  const float q = window(p, 0.38f, 1.f);
  const float wob = 1.6f * sinT(q * 2.f) * (1.f - q);
  const float x = fl(ls.mush_x), y = fl(ls.mush_y);
  if (y + 2.f < fl(clip.y0) || y - 20.f > fl(clip.y1)) return;
  const float cs = sqrtf(hs);
  const float sx = x + wob, sy = y - 8.f * hs;   // stem top
  strokeCapsule(s, Vec2{x, y}, Vec2{sx, sy}, 4.f, pal[WATER_LIGHT], clip);
  const float rx = 7.f * cs, ry = 5.5f * cs, cy = sy + 1.f;
  Vec2 cap[11];
  for (int k = 0; k <= 8; ++k) {
    const float a = fl(k) / 16.f;   // half a turn, right to left over the top
    cap[k] = Vec2{sx + rx * cosT(a), cy - ry * sinT(a)};
  }
  cap[9] = Vec2{sx - rx * 0.7f, cy + 1.2f * cs};
  cap[10] = Vec2{sx + rx * 0.7f, cy + 1.2f * cs};
  fillPolygon(s, cap, 11, pal[FIRE], clip);
  fillCircle(s, Vec2{sx - 2.5f * cs, cy - 2.4f * cs}, 1.1f * cs, pal[FIRE_CORE], clip);
  fillCircle(s, Vec2{sx + 2.2f * cs, cy - 3.6f * cs}, 1.0f * cs, pal[FIRE_CORE], clip);
}

void drawLayer(const Landscape& ls, int layer, Surface& s, const Palette& pal, float mood,
               const BuildState& b, const Rect& clip) {
  switch (layer) {
    case BL_SKY:
      drawSky(ls, s, pal, mood, buildSkyFade(b), clip);
      break;
    case BL_RIDGES:
      for (int k = 0; k < 3; ++k) drawRidge(ls, s, k, pal[(Slot)(RIDGE_FAR + k)], clip, buildRidgeDrop(b, k));
      break;
    case BL_FAR_TREES:
      drawFarTrees(ls, s, pal[TREES_FAR], clip, b);
      break;
    case BL_MID_PINES:
      for (int i = 0; i < ls.n_mid; ++i) {
        drawPine(s, ls.mid[i], pal[TREES_MID], pal[TREES_MID], 3.f, 0.12f, clip,
                 buildTreeScale(b, fl(ls.mid[i].x)));
      }
      break;
    case BL_GROUND:
      fillRect(s, Rect{0, LS_GROUND_Y, SS_WIDTH, SS_HEIGHT}, pal[GROUND], clip);
      break;
    case BL_STREAM:
      drawStream(ls, s, pal, clip, buildStreamReveal(b));
      break;
    case BL_CAMP_PINE:
      drawCampPine(s, ls, pal, clip, buildNearTreeScale(b, 0));
      break;
    case BL_ROCKS:
      for (int i = 0; i < ls.n_rocks; ++i) {
        const LsRock& r = ls.rocks[i];
        drawRock(r, s, pal[ROCK], clip, buildPlantScale(b, r.p[0].x, r.p[0].y));
      }
      break;
    case BL_FG_PINES:   // the taller one first
      drawPine(s, ls.fg[1], pal[FOLIAGE_DARK], pal[TRUNK], 8.f, 0.28f, clip, buildNearTreeScale(b, 2));
      drawPine(s, ls.fg[0], pal[FOLIAGE_DARK], pal[TRUNK], 7.f, 0.28f, clip, buildNearTreeScale(b, 1));
      break;
    case BL_TUFTS:
      for (int i = 0; i < ls.n_tufts; ++i) {
        const LsTuft& t = ls.tufts[i];
        if (t.dyn) continue;   // the swaying ones are drawn by ambient.cpp
        drawTuft(t, s, pal[GRASS], clip, buildPlantScale(b, fl(t.x), fl(t.y)));
      }
      break;
    case BL_MUSHROOM:
      drawMushroom(ls, s, pal, clip, buildMushroomT(b));
      break;
    default:
      break;
  }
}

}  // namespace

void landscapeDrawStatic(const Landscape& ls, Surface& frame, const Palette& pal, float mood,
                         const BuildState& b) {
  const Rect clip = frame.bounds();
  // Before the sky is in the cache the build-up draws everything over it; a
  // flat fill keeps the cache contents defined.
  if (!buildCached(b, BL_SKY)) fillRect(frame, clip, pal[SKY4], clip);
  for (int L = 0; L < BL_COUNT; ++L) {
    if (buildCached(b, L)) drawLayer(ls, L, frame, pal, mood, b, clip);
  }
}

void landscapeDrawLayers(const Landscape& ls, Surface& s, const Rect& clip, const Palette& pal,
                         float mood, const BuildState& b, int first, int last) {
  for (int L = first; L <= last && L < BL_COUNT; ++L) {
    if (buildDrawn(b, L) && !buildCached(b, L)) drawLayer(ls, L, s, pal, mood, b, clip);
  }
}

// ---- clouds --------------------------------------------------------------

void cloudGenerate(Cloud& c, Rng& rng, float x, float y, float speed_px_per_s) {
  c.x = x;
  c.y = y;
  c.speed = speed_px_per_s * SS_TICK_DT;
  c.ear = 0.f;
  c.n = rng.rangeInt(4, CLOUD_MAX_LIT);
  const float span = rng.range(90.f, 140.f);
  for (int i = 0; i < c.n; ++i) {
    const float u = c.n > 1 ? fl(i) / fl(c.n - 1) : 0.5f;
    const float env = 1.f - fabsf(2.f * u - 1.f);   // bigger puffs in the middle
    c.dx[i] = (u - 0.5f) * span + rng.range(-6.f, 6.f);
    const float base_rx = rng.range(20.f, 34.f);   // one rng call per statement (evaluation order)
    c.rx[i] = base_rx + env * rng.range(10.f, 26.f);
    c.ry[i] = minf(24.f, maxf(10.f, c.rx[i] * rng.range(0.38f, 0.5f)));
    c.dy[i] = -c.ry[i] * rng.range(0.6f, 1.0f);      // bottoms land within a few px of y
  }
  c.sdx = 0.f;
  c.sdy = -2.f;
  c.srx = 0.5f * span + 14.f;
  c.sry = 8.f;

  float x0 = c.sdx - c.srx, x1 = c.sdx + c.srx, y0 = c.sdy - c.sry, y1 = c.sdy + c.sry;
  for (int i = 0; i < c.n; ++i) {
    x0 = minf(x0, c.dx[i] - c.rx[i]);
    x1 = maxf(x1, c.dx[i] + c.rx[i]);
    y0 = minf(y0, c.dy[i] - c.ry[i]);
    y1 = maxf(y1, c.dy[i] + c.ry[i]);
  }
  c.bx0 = x0 - 1.f;
  c.by0 = y0 - 1.f;
  c.bx1 = x1 + 1.f;
  c.by1 = y1 + 1.f;
}

void cloudStep(Cloud& c) {
  c.x -= c.speed;
  if (c.x < CLOUD_WRAP_LEFT) c.x += CLOUD_WRAP_RIGHT - CLOUD_WRAP_LEFT;
}

namespace {
// The ear: a tapered lobe rising from the top of the leading (leftmost) puff,
// leaning forward. Six points, base inside the puff, tip ear * 15 px above it.
void cloudEar(const Cloud& c, float xq, Vec2 p[6]) {
  int k = 0;
  for (int i = 1; i < c.n; ++i)
    if (c.dx[i] < c.dx[k]) k = i;
  // The leading puff is often small: the ear sits on the next one in if that is taller.
  int j = -1;
  for (int i = 0; i < c.n; ++i)
    if (i != k && (j < 0 || c.dx[i] < c.dx[j])) j = i;
  if (j >= 0 && c.dy[j] - c.ry[j] < c.dy[k] - c.ry[k] - 4.f) k = j;
  const float bx = xq + c.dx[k] - 0.25f * c.rx[k], by = c.y + c.dy[k] - 0.8f * c.ry[k];
  const float h = 15.f * c.ear, w = 6.5f;
  const float lean = -0.45f * h;   // forward is left: clouds drift left
  p[0] = Vec2{bx - w, by + 3.f};
  p[1] = Vec2{bx - 0.85f * w + 0.5f * lean, by - 0.55f * h};
  p[2] = Vec2{bx - 0.2f * w + lean, by - h};
  p[3] = Vec2{bx + 0.35f * w + 0.9f * lean, by - 0.9f * h};
  p[4] = Vec2{bx + 0.8f * w + 0.4f * lean, by - 0.4f * h};
  p[5] = Vec2{bx + w, by + 3.f};
}
}  // namespace

Rect cloudBounds(const Cloud& c) {
  const float xq = floorf(c.x);
  Rect r{(int)floorf(xq + c.bx0), (int)floorf(c.y + c.by0), (int)ceilf(xq + c.bx1),
         (int)ceilf(c.y + c.by1)};
  if (c.ear > 0.f) {
    Vec2 p[6];
    cloudEar(c, xq, p);
    for (const Vec2& q : p) {
      if ((int)floorf(q.x) - 1 < r.x0) r.x0 = (int)floorf(q.x) - 1;
      if ((int)floorf(q.y) - 1 < r.y0) r.y0 = (int)floorf(q.y) - 1;
      if ((int)ceilf(q.x) + 1 > r.x1) r.x1 = (int)ceilf(q.x) + 1;
    }
  }
  return r.clipped(Rect{-100000, 0, 100000, LS_HORIZON});
}

void cloudDraw(const Cloud& c, Surface& s, const Palette& pal, const Rect& clip) {
  // Clouds never cover the ridges: the cache holds them, so stop at the horizon.
  const Rect lim = clip.clipped(Rect{0, 0, SS_WIDTH, LS_HORIZON});
  if (lim.empty()) return;
  if (cloudBounds(c).clipped(lim).empty()) return;
  const float xq = floorf(c.x);
  for (int i = 0; i < c.n; ++i) {
    fillEllipse(s, Vec2{xq + c.dx[i], c.y + c.dy[i]}, c.rx[i], c.ry[i], pal[CLOUD_LIT], lim);
  }
  if (c.ear > 0.f) {
    Vec2 p[6];
    cloudEar(c, xq, p);
    fillPolygon(s, p, 6, pal[CLOUD_LIT], lim);
  }
  fillEllipse(s, Vec2{xq + c.sdx, c.y + c.sdy}, c.srx, c.sry, pal[CLOUD_SHADE], lim);
}

}  // namespace ss
