// The mushroom patch. See patch.h.
//
// OWNERSHIP: skeleton from the foundation step (A1); step B4 (the patch) filled it in and owns it.
#include "patch.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../mush_patch.h"
#include "landscape.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

// Each role's full size against the mother's 14 px (plan 3.1, PROPOSED), and how far its cap leans
// away from its nearest neighbour, px (plan 3.1: 1 to 2 px, so the clump does not look stamped).
const float kFull[MUSH_PATCH] = {0.70f, 0.55f, 0.80f, 0.65f, 0.75f, 0.50f};
const float kLean[MUSH_PATCH] = {1.5f, 2.0f, 1.5f, 1.0f, 1.5f, 1.2f};
// Each role's cap a shade apart (PROPOSED): real clumps are never one colour; older caps fade paler.
const float kTone[MUSH_PATCH] = {0.35f, -0.4f, -0.25f, 0.5f, 0.1f, -0.15f};

// The young shape by size step 1..4 (index 0 unused). A button is mostly cap on a stub of stem; the
// stem catches up and the cap opens flatter as it grows, as real ones do. Cap sizes run a little over
// the plan's quarter steps (0.25 .. 1) so a button still reads as a mushroom at panel size, not a dot
// (Mike, 2026-11-18: natural look first).
const float kCapK[5] = {0.f, 0.32f, 0.56f, 0.80f, 1.f};
const float kStemK[5] = {0.f, 0.40f, 0.66f, 0.86f, 1.f};
const float kDome[5] = {1.f, 1.00f, 0.92f, 0.84f, 0.78f};

// The farthest right a companion's foot may stand: its cap and fringe then end short of the status
// chip's capsule at x 624 (the plan's 616 let a full companion's fringe reach 625).
constexpr int kCompanionMaxX = 612;

// The bank rule (plan 3.1): a foot stays 9 px clear of the water's lower edge and at y 474 or above.
float bankY(const Landscape& ls, float x, float y) {
  float cy = 0.f, half = 0.f;
  if (landscapeStreamSpan(ls, x, cy, half)) y = maxf(y, cy + half + 9.f);
  return minf(y, 474.f);
}

// A foot on a rock, or just behind one (the cap would be drawn over the stone, the patch being the
// last layer), cannot stand there.
bool onRock(const Landscape& ls, float x, float y, float capr) {
  for (int r = 0; r < ls.n_rocks; ++r) {
    float x0 = ls.rocks[r].p[0].x, x1 = x0, y0 = ls.rocks[r].p[0].y, y1 = y0;
    for (int k = 1; k < 6; ++k) {
      x0 = minf(x0, ls.rocks[r].p[k].x);
      x1 = maxf(x1, ls.rocks[r].p[k].x);
      y0 = minf(y0, ls.rocks[r].p[k].y);
      y1 = maxf(y1, ls.rocks[r].p[k].y);
    }
    if (x > x0 - capr - 2.f && x < x1 + capr + 2.f && y > y0 - 3.f && y < y1 + 2.f) return true;
  }
  return false;
}

// The geometry patchShroomDraw draws and patchShroomBounds folds, so the two cannot disagree.
struct Geom {
  Vec2 foot, top;      // the stem
  float stem_w;
  Vec2 cap[11];
  Vec2 spot;
  float spot_r;
  bool has_spot;
  Vec2 shade;          // the small shadow on the ground at the foot
  float shade_rx, shade_ry;
  bool has_shade;
};

// Turn p about the foot f by `turns`.
Vec2 turn(Vec2 p, Vec2 f, float turns) {
  if (turns == 0.f) return p;
  const float c = cosT(turns), s = sinT(turns);
  const float dx = p.x - f.x, dy = p.y - f.y;
  return Vec2{f.x + dx * c - dy * s, f.y + dx * s + dy * c};
}

void geom(const ShroomLook& m, Geom& g) {
  const int st = m.step < 1 ? 1 : (m.step > 4 ? 4 : m.step);
  const float sc = maxf(m.scale, 0.f);
  const float wilt = m.wilt ? 1.f : 0.f;
  const float cap_k = sc * kCapK[st];
  const float rx = maxf(7.f * cap_k, 1.6f);
  const float ry = maxf(5.5f * cap_k * kDome[st] * (1.f - 0.18f * wilt), 1.4f);
  const float stem_h = 8.f * sc * kStemK[st] * (1.f - 0.15f * wilt);
  g.foot = Vec2{m.x, m.y};
  g.stem_w = maxf(4.f * sc * (0.55f + 0.45f * kStemK[st]), 1.6f);
  // A wilting cap hangs a little over to the side it leans.
  const float hang = wilt * (m.lean < 0.f ? -0.05f : 0.05f);
  const Vec2 top0{m.x + m.lean, m.y - stem_h};
  const float cy = top0.y + 1.f;
  Vec2 cap[11];
  for (int k = 0; k <= 8; ++k) {
    const float a = fl(k) / 16.f;   // half a turn, right to left over the top
    cap[k] = Vec2{top0.x + rx * cosT(a), cy - ry * sinT(a)};
  }
  const float under = 1.2f * sqrtf(maxf(cap_k, 0.05f));
  cap[9] = Vec2{top0.x - rx * 0.7f, cy + under};
  cap[10] = Vec2{top0.x + rx * 0.7f, cy + under};
  for (int k = 0; k < 11; ++k) g.cap[k] = turn(turn(cap[k], top0, hang), g.foot, m.tilt);
  g.top = turn(top0, g.foot, m.tilt);
  g.has_spot = st >= 3 && rx >= 3.5f;
  g.spot = turn(turn(Vec2{top0.x - 0.32f * rx, cy - 0.5f * ry}, top0, hang), g.foot, m.tilt);
  g.spot_r = 0.16f * rx;
  // The shadow only for one standing up (not one lying in a hand or tipped over).
  g.has_shade = st >= 2 && fabsf(m.tilt) < 0.06f;
  g.shade = Vec2{m.x + 0.8f, m.y + 0.3f};
  g.shade_rx = 0.75f * rx + 0.8f;
  g.shade_ry = 0.8f + 0.6f * sc;
}

// The build pop of the mother (landscape.cpp drawMushroom) at one mushroom's own delay: the cap
// peeks, holds a beat, then the whole thing pops with an overshoot and wobbles to rest. Exactly 1 and
// no wobble once p reaches 1, so the cached patch is the grown one.
void pop(float p, float& hs, float& wob) {
  if (p < 0.2f) hs = 0.3f * easeOutCubic(p / 0.2f);
  else if (p < 0.38f) hs = 0.3f;
  else hs = 0.3f + 0.7f * easeOutBack((p - 0.38f) / 0.62f);
  const float q = window(p, 0.38f, 1.f);
  wob = 1.2f * sinT(q * 2.f) * (1.f - q);
}

}  // namespace

void patchLayout(const Landscape& ls, LsPatch& out) {
  memset(&out, 0, sizeof out);
  const uint8_t b = ls.patch_stage;
  const float mx = fl(ls.mush_x), my = fl(ls.mush_y);
  // The companions stand right of the mother whenever both fit there clear of the chip's capsule (the
  // plan switched at x 545, which put a companion on top of the last pickable for mothers at 546 to
  // 560); further right they stand on her left, beyond the pickables.
  const bool right_of_her = ls.mush_x + 24 <= kCompanionMaxX;
  const int nx = ls.mush_x - 44 < 501 ? ls.mush_x - 44 : 501;
  out.nx = (int16_t)nx;
  out.wilt = mushWilting(b) ? 1 : 0;
  float x[MUSH_PATCH], y[MUSH_PATCH];
  bool ok[MUSH_PATCH];
  x[0] = right_of_her ? mx + 13.f : mx - 13.f;
  y[0] = my + 2.f;
  x[1] = right_of_her ? mx + 24.f : mx - 25.f;
  y[1] = my - 5.f;
  x[2] = fl(nx + 12);
  y[2] = my - 4.f;
  x[3] = fl(nx + 21);
  y[3] = my + 2.f;
  x[4] = fl(nx + 29);
  y[4] = my - 3.f;
  // The straggler fills a long gap between the last pickable and the patch round the mother: halfway
  // to the nearer companion (the plan said to the mother; with the companions on her left that put it
  // within 3 px of one).
  const float gap_to = right_of_her ? mx : minf(x[0], x[1]);
  x[5] = 0.5f * (x[4] + gap_to);
  y[5] = my + 1.f;
  for (int i = 0; i < MUSH_PATCH; ++i) ok[i] = true;
  ok[5] = mx - x[4] > 50.f;
  for (int i = 0; i < 2; ++i) ok[i] = x[i] <= fl(kCompanionMaxX);   // never under the chip's capsule
  int ny = 0;
  for (int i = 0; i < MUSH_PATCH; ++i) {
    y[i] = bankY(ls, x[i], y[i]);
    if (ok[i] && onRock(ls, x[i], y[i], 7.f * kFull[i])) ok[i] = false;
    LsShroom& m = out.m[i];
    m.x = (int16_t)floorf(x[i] + 0.5f);
    m.y = (int16_t)floorf(y[i] + 0.5f);
    m.step = ok[i] ? mushStep(b, i) : 0;
    if (i >= MUSH_PICK0 && i < MUSH_PICK0 + 3) {
      m.lifted = (uint8_t)((ls.patch_lifted >> (i - MUSH_PICK0)) & 1u);
      if (m.y > ny) ny = m.y;
    }
    if (m.step) ++out.n;
  }
  out.ny = (int16_t)(ny + 2);
}

ShroomLook patchLook(const LsPatch& p, int i) {
  const LsShroom& m = p.m[i];
  // Lean away from the nearest neighbour that stands at this stage, lifted or not, so the cache's look
  // does not change when the major takes the pickables. The mother is not in LsPatch and does not count.
  float best = 1e9f, dir = (i & 1) ? 1.f : -1.f;
  for (int j = 0; j < MUSH_PATCH; ++j) {
    if (j == i || p.m[j].step == 0) continue;
    const float dx = fl(m.x - p.m[j].x), dy = fl(m.y - p.m[j].y);
    const float d = dx * dx + dy * dy;
    if (d < best) {
      best = d;
      dir = dx < 0.f ? -1.f : 1.f;
    }
  }
  ShroomLook k;
  k.x = fl(m.x);
  k.y = fl(m.y);
  k.scale = kFull[i];
  k.step = m.step;
  k.wilt = (p.wilt && m.step) ? 1 : 0;
  k.lean = dir * kLean[i] * (m.step >= 3 ? 1.f : 0.5f) * (k.wilt ? 1.6f : 1.f);
  k.tilt = 0.f;
  k.tone = kTone[i];
  return k;
}

void patchShroomDraw(const ShroomLook& m, Surface& s, const Palette& pal, const Rect& clip) {
  if (m.step == 0 || m.scale <= 0.01f) return;
  Geom g;
  geom(m, g);
  Pixel cap = pal[MUSHROOM], stem = pal[WATER_LIGHT];
  if (m.tone > 0.f) cap = mixRGB(cap, stem, 0.22f * clamp01(m.tone));
  else if (m.tone < 0.f) cap = mixRGB(cap, pal[GROUND_DARK], 0.18f * clamp01(-m.tone));
  if (m.wilt) {   // browned and dull, the way old ones go
    cap = mixRGB(cap, pal[GROUND_DARK], 0.42f);
    stem = mixRGB(stem, pal[GROUND], 0.35f);
  }
  if (g.has_shade) fillEllipse(s, g.shade, g.shade_rx, g.shade_ry, pal[GROUND_DARK], clip);
  strokeCapsule(s, g.foot, g.top, g.stem_w, stem, clip);
  fillPolygon(s, g.cap, 11, cap, clip);
  // The spot as on the mother: the cap toward the pale stem, so it dims with the mood too.
  if (g.has_spot) fillCircle(s, g.spot, g.spot_r, mixRGB(cap, stem, 0.65f), clip);
}

Rect patchShroomBounds(const ShroomLook& m) {
  if (m.step == 0 || m.scale <= 0.01f) return Rect{0, 0, 0, 0};
  Geom g;
  geom(m, g);
  const float hw = 0.5f * g.stem_w;
  float x0 = minf(g.foot.x, g.top.x) - hw, x1 = maxf(g.foot.x, g.top.x) + hw;
  float y0 = minf(g.foot.y, g.top.y) - hw, y1 = maxf(g.foot.y, g.top.y) + hw;
  for (int k = 0; k < 11; ++k) {
    x0 = minf(x0, g.cap[k].x);
    x1 = maxf(x1, g.cap[k].x);
    y0 = minf(y0, g.cap[k].y);
    y1 = maxf(y1, g.cap[k].y);
  }
  if (g.has_shade) {
    x0 = minf(x0, g.shade.x - g.shade_rx);
    x1 = maxf(x1, g.shade.x + g.shade_rx);
    y1 = maxf(y1, g.shade.y + g.shade_ry);
  }
  // The antialiasing fringe, then whole pixels outward.
  return Rect{(int)floorf(x0) - 2, (int)floorf(y0) - 2, (int)floorf(x1) + 3, (int)floorf(y1) + 3};
}

void patchDrawStatic(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, float t) {
  if (mushStage(ls.patch_stage) == 0 || t <= 0.f) return;   // stage 0: nothing, not even a layout
  // Every foot is at mush_y - 5 or lower and no mushroom here is 20 px tall, so most strips leave now.
  if (clip.y1 <= ls.mush_y - 26 || clip.y0 >= SS_HEIGHT) return;
  LsPatch p;
  patchLayout(ls, p);
  // Pop order: the nearest to the mother first, so the patch spreads out from her.
  int order[MUSH_PATCH];
  float dist[MUSH_PATCH];
  for (int i = 0; i < MUSH_PATCH; ++i) {
    const float dx = fl(p.m[i].x - ls.mush_x), dy = fl(p.m[i].y - ls.mush_y);
    dist[i] = dx * dx + dy * dy;
    order[i] = i;
  }
  for (int i = 1; i < MUSH_PATCH; ++i)
    for (int j = i; j > 0 && dist[order[j]] < dist[order[j - 1]]; --j) {
      const int o = order[j];
      order[j] = order[j - 1];
      order[j - 1] = o;
    }
  int rank[MUSH_PATCH];
  for (int r = 0; r < MUSH_PATCH; ++r) rank[order[r]] = r;
  // Back to front by foot, so a nearer one overlaps a farther one.
  int draw[MUSH_PATCH];
  for (int i = 0; i < MUSH_PATCH; ++i) draw[i] = i;
  for (int i = 1; i < MUSH_PATCH; ++i)
    for (int j = i; j > 0 && p.m[draw[j]].y < p.m[draw[j - 1]].y; --j) {
      const int o = draw[j];
      draw[j] = draw[j - 1];
      draw[j - 1] = o;
    }
  for (int k = 0; k < MUSH_PATCH; ++k) {
    const int i = draw[k];
    if (p.m[i].step == 0 || p.m[i].lifted) continue;
    ShroomLook m = patchLook(p, i);
    if (t < 1.f) {   // the build-up: each 0.08 of the mother's growth later than the one before
      const float d = 0.08f * fl(rank[i] + 1);
      const float pi = clamp01((t - d) / (1.f - d));
      if (pi <= 0.f) continue;
      float hs, wob;
      pop(pi, hs, wob);
      m.scale *= hs;
      m.lean += wob;
    }
    patchShroomDraw(m, s, pal, clip);
  }
}

bool patchNear(const Landscape& ls, float x, float y, float r) {
  if (mushStage(ls.patch_stage) == 0) return false;
  LsPatch p;
  patchLayout(ls, p);
  for (int i = 0; i < MUSH_PATCH; ++i) {
    if (p.m[i].step == 0) continue;
    const float dx = fl(p.m[i].x) - x, dy = fl(p.m[i].y) - y;
    if (dx * dx + dy * dy < r * r) return true;
  }
  return false;
}

Rect patchBounds(const Landscape& ls, bool with_lifted) {
  Rect u{0, 0, 0, 0};
  if (mushStage(ls.patch_stage) == 0) return u;
  LsPatch p;
  patchLayout(ls, p);
  for (int i = 0; i < MUSH_PATCH; ++i) {
    if (p.m[i].step == 0 || (p.m[i].lifted && !with_lifted)) continue;
    const Rect r = patchShroomBounds(patchLook(p, i));
    if (u.empty()) u = r;
    else u = Rect{r.x0 < u.x0 ? r.x0 : u.x0, r.y0 < u.y0 ? r.y0 : u.y0, r.x1 > u.x1 ? r.x1 : u.x1,
                  r.y1 > u.y1 ? r.y1 : u.y1};
  }
  return u;
}

Vec2 patchSpot(const Landscape& ls) {
  Vec2 c{fl(ls.mush_x), fl(ls.mush_y)};
  if (mushStage(ls.patch_stage) == 0) return c;
  LsPatch p;
  patchLayout(ls, p);
  int n = 1;   // the mother is part of it
  for (int i = 0; i < MUSH_PATCH; ++i) {
    if (p.m[i].step == 0) continue;
    c.x += fl(p.m[i].x);
    c.y += fl(p.m[i].y);
    ++n;
  }
  return Vec2{c.x / fl(n), c.y / fl(n)};
}

}  // namespace ss
