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

// Single-colour pine: trunk capsule plus stacked triangles, lowest tier first
// so upper tiers sit in front.
void drawPine(Surface& s, const LsPine& p, Pixel foliage, Pixel trunk, float trunk_w,
              float trunk_frac, const Rect& clip) {
  const float top = fl(p.base_y - p.h);
  const float h = fl(p.h);
  const float fbot = fl(p.base_y) - maxf(4.f, h * trunk_frac);
  const float cx = fl(p.x);
  strokeCapsule(s, Vec2{cx, fbot - 3.f}, Vec2{cx, fl(p.base_y)}, trunk_w, trunk, clip);
  for (int i = p.tiers - 1; i >= 0; --i) {
    drawTierTriangle(s, cx, tierGeom(i, p.tiers, top, fbot, h, fl(p.width_pct)), foliage, clip);
  }
}

// The bag-hanging pine: bare lower trunk, lit and dark foliage, one branch.
void drawCampPine(Surface& s, const Landscape& ls, const Palette& pal, const Rect& clip) {
  const LsPine& p = ls.camp;
  const float cx = fl(p.x);
  const float top = fl(p.base_y - p.h);
  const float fbot = 318.f;   // lowest tier base; the trunk shows only below it
  const float h = fl(p.h);
  strokeCapsule(s, Vec2{cx, top + 30.f}, Vec2{cx, fl(p.base_y)}, 9.f, pal[TRUNK], clip);
  // The bag branch leaves the trunk inside the lowest tier (y 302), so its root
  // is hidden by foliage, and pokes out past the canopy to the left, rising
  // 8 px over 82 px. The food bag hangs from its tip in later stages.
  strokeCapsule(s, Vec2{cx, 302.f}, Vec2{cx - 82.f, 294.f}, 5.f, pal[TRUNK], clip);
  for (int i = p.tiers - 1; i >= 0; --i) {
    const Tier t = tierGeom(i, p.tiers, top, fbot, h, fl(p.width_pct));
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

void drawRidge(const Landscape& ls, Surface& s, int layer, Pixel col, const Rect& clip) {
  Vec2 p[LS_CREST_N + 2];
  for (int i = 0; i < LS_CREST_N; ++i) {
    p[i] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * i), ls.crest[layer][i]};
  }
  p[LS_CREST_N] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * (LS_CREST_N - 1)), fl(SS_HEIGHT + 2)};
  p[LS_CREST_N + 1] = Vec2{fl(LS_CREST_X0), fl(SS_HEIGHT + 2)};
  fillPolygon(s, p, LS_CREST_N + 2, col, clip);
}

// Serrated line along the near ridge. A polygon of m teeth has 2m+1 points on
// top and m+1 along a lower edge 4 px under the crest: 3m+2 <= 64, so m <= 20.
void drawFarTrees(const Landscape& ls, Surface& s, Pixel col, const Rect& clip) {
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
      p[n++] = Vec2{x0, landscapeCrestAt(ls, 2, x0)};
      p[n++] = Vec2{xm, landscapeCrestAt(ls, 2, xm) - fl(ls.spikes[i + j].h)};
    }
    const float xe = fl(ls.spikes[i + m].x);
    p[n++] = Vec2{xe, landscapeCrestAt(ls, 2, xe)};
    for (int j = m; j >= 0; --j) {
      const float x = fl(ls.spikes[i + j].x);
      p[n++] = Vec2{x, landscapeCrestAt(ls, 2, x) + 4.f};
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
}

void landscapeDrawStatic(const Landscape& ls, Surface& frame, const Palette& pal, float mood) {
  const Rect clip = frame.bounds();

  // Sky: five flat bands.
  static const int kBand[6] = {0, 58, 118, 178, 228, LS_HORIZON};
  for (int b = 0; b < 5; ++b) {
    fillRect(frame, Rect{0, kBand[b], SS_WIDTH, kBand[b + 1]}, pal[(Slot)(SKY0 + b)], clip);
  }

  drawSkyBody(ls, frame, pal, mood, clip);

  drawRidge(ls, frame, 0, pal[RIDGE_FAR], clip);
  drawRidge(ls, frame, 1, pal[RIDGE_MID], clip);
  drawRidge(ls, frame, 2, pal[RIDGE_NEAR], clip);
  drawFarTrees(ls, frame, pal[TREES_FAR], clip);

  for (int i = 0; i < ls.n_mid; ++i) {
    drawPine(frame, ls.mid[i], pal[TREES_MID], pal[TREES_MID], 3.f, 0.12f, clip);
  }

  // Ground and the dark strip along the stream's far bank.
  fillRect(frame, Rect{0, 318, SS_WIDTH, SS_HEIGHT}, pal[GROUND], clip);
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

  // Stream body, then ripples.
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
  for (int i = 0; i < ls.n_ripples; ++i) {
    strokeCapsule(frame, ls.ripples[i].a, ls.ripples[i].b, 2.f, pal[WATER_LIGHT], clip);
  }

  drawCampPine(frame, ls, pal, clip);

  for (int i = 0; i < ls.n_rocks; ++i) fillPolygon(frame, ls.rocks[i].p, 6, pal[ROCK], clip);

  // Foreground pines at the left edge: the taller one first.
  drawPine(frame, ls.fg[1], pal[FOLIAGE_DARK], pal[TRUNK], 8.f, 0.28f, clip);
  drawPine(frame, ls.fg[0], pal[FOLIAGE_DARK], pal[TRUNK], 7.f, 0.28f, clip);

  for (int i = 0; i < ls.n_tufts; ++i) {
    const LsTuft& t = ls.tufts[i];
    for (int k = 0; k < t.n; ++k) {
      const float bx = fl(t.x + t.off[k]);
      const Vec2 tri[3] = {{bx - 1.5f, fl(t.y)}, {bx + 1.5f, fl(t.y)},
                           {bx + fl(t.lean[k]), fl(t.y) - fl(t.h[k])}};
      fillPolygon(frame, tri, 3, pal[GRASS], clip);
    }
  }
}

// ---- clouds --------------------------------------------------------------

void cloudGenerate(Cloud& c, Rng& rng, float x, float y, float speed_px_per_s) {
  c.x = x;
  c.y = y;
  c.speed = speed_px_per_s * SS_TICK_DT;
  c.n = rng.rangeInt(4, CLOUD_MAX_LIT);
  const float span = rng.range(90.f, 140.f);
  for (int i = 0; i < c.n; ++i) {
    const float u = c.n > 1 ? fl(i) / fl(c.n - 1) : 0.5f;
    const float env = 1.f - fabsf(2.f * u - 1.f);   // bigger puffs in the middle
    c.dx[i] = (u - 0.5f) * span + rng.range(-6.f, 6.f);
    c.rx[i] = rng.range(20.f, 34.f) + env * rng.range(10.f, 26.f);
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

Rect cloudBounds(const Cloud& c) {
  Rect r{(int)floorf(c.x + c.bx0), (int)floorf(c.y + c.by0), (int)ceilf(c.x + c.bx1),
         (int)ceilf(c.y + c.by1)};
  return r.clipped(Rect{-100000, 0, 100000, LS_HORIZON});
}

void cloudDraw(const Cloud& c, Surface& s, const Palette& pal, const Rect& clip) {
  // Clouds never cover the ridges: the cache holds them, so stop at the horizon.
  const Rect lim = clip.clipped(Rect{0, 0, SS_WIDTH, LS_HORIZON});
  if (lim.empty()) return;
  if (cloudBounds(c).clipped(lim).empty()) return;
  for (int i = 0; i < c.n; ++i) {
    fillEllipse(s, Vec2{c.x + c.dx[i], c.y + c.dy[i]}, c.rx[i], c.ry[i], pal[CLOUD_LIT], lim);
  }
  fillEllipse(s, Vec2{c.x + c.sdx, c.y + c.sdy}, c.srx, c.sry, pal[CLOUD_SHADE], lim);
}

}  // namespace ss
