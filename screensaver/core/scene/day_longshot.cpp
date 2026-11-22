// Long-range shooting as a surprise major. See day_longshot.h.
//
// OWNERSHIP: skeleton from the foundation step (A1). Step B5 filled in the targets and the shapes (this
// part, down to "the major"); step C3 filled in the major (2026-11-19).
//
// The targets are read off the terrain the cache draws, so they sit on it for every seed: the same crest
// samples (landscapeCrestAt), the same tooth polygons as drawFarTrees, the same mid pines. A target is only
// put where the layer it claims shows on every side of it by a margin (no antialiased edge under it, no
// pine in front of it). Numbers marked PROPOSED are starting values for Mike's review; how it looks is
// UNVERIFIED on the panel.
#include "day_longshot.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "day_plink.h"
#include "housedogs.h"
#include "landscape.h"
#include "plink.h"
#include "weather.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float v) { return v < 0.f ? -v : v; }
inline float fl(int v) { return (float)v; }

// ---- tuning (PROPOSED, Mike 2026-11-18) ------------------------------------------------------------------------

constexpr float kSpacing = 28.f;         // targets at least this far apart in x
constexpr float kScanStep = 2.f;         // px between the x samples of the window
constexpr float kEdgeMargin = 1.5f;      // a target's layer must show this far round the point checked
constexpr float kLsTargetScale = 1.0f;   // all four targets' size (1.3 if they do not read on the panel)
// Dot radii and gong scales by kind (before kLsTargetScale).
constexpr float kDotTree = 1.7f, kDotRock = 1.5f, kDotCone = 1.6f, kDotSkyTooth = 1.7f;
constexpr float kGongPost = 0.7f, kGongSky = 1.0f;
// The bullet's flight to each, seconds: it grows with the distance, so the strings' order is the range's.
constexpr float kFlightTree = 0.45f, kFlightTree2 = 0.5f, kFlightCone = 0.55f, kFlightRock = 0.65f;
constexpr float kFlightSkyTooth = 0.7f, kFlightSkyMid = 0.75f, kFlightPost = 0.9f, kFlightSkyFar = 1.2f;

// The gong (7 px tall at scale 1): legs, top bar, the plate hung 3.4 px under the bar's middle.
constexpr float kGongH = 7.f, kGongFoot = 2.5f, kGongTop = 1.5f, kGongBar = 2.f, kGongLegW = 0.9f;
constexpr float kGongHang = 3.4f, kGongPlateR = 1.9f, kGongGlintR = 2.6f;
// The flag: a 20 px stick, a ribbon of three 4.5 px pieces off its top.
constexpr float kFlagH = 20.f, kFlagStickW = 1.6f, kRibbonSeg = 4.5f, kRibbonW = 1.4f;

// ---- the terrain, as the cache draws it -----------------------------------------------------------------------

// The far tree line's top at x: the tooth polygon drawFarTrees fills (straight edges from the crest at each
// tooth's ends to its apex over the middle), or the near ridge's crest past the teeth. `tooth` gets the
// tooth's index (-1 none).
float toothTop(const Landscape& ls, float x, int* tooth = nullptr) {
  if (tooth) *tooth = -1;
  for (int i = 0; i < ls.n_spikes; ++i) {
    const float x0 = fl(ls.spikes[i].x), x1 = fl(ls.spikes[i + 1].x);
    if (x < x0 || x >= x1) continue;
    const float xm = 0.5f * (x0 + x1);
    const float apex = landscapeCrestAt(ls, 2, xm) - fl(ls.spikes[i].h);
    if (tooth) *tooth = i;
    if (x <= xm) return lerp(landscapeCrestAt(ls, 2, x0), apex, (x - x0) / maxf(xm - x0, 1e-3f));
    return lerp(apex, landscapeCrestAt(ls, 2, x1), (x - xm) / maxf(x1 - xm, 1e-3f));
  }
  return landscapeCrestAt(ls, 2, x);
}

// Mid pine j's top tier (drawPine with trunk_frac 0.12 at full height, landscape.cpp tierGeom): apex,
// base row and half width at the base.
void pineTop(const LsPine& p, float& apex, float& base, float& half_w) {
  const float h = fl(p.h);
  const float top = fl(p.base_y) - h;
  const float fbot = fl(p.base_y) - maxf(4.f, h * 0.12f);
  const float th = 0.42f * (fbot - top);
  apex = top;
  base = top + th;
  half_w = 0.5f * (fl(p.width_pct) * 0.01f) * h * (1.f - 0.2f * fl(p.tiers - 1));
}

// A mid pine is in front of (x, y) or near enough to crowd it (the test ridgeSpot uses, majors.cpp). Pines
// up to index `skip` are ignored (they are drawn before pine `skip`, so they stand behind it).
bool pineOver(const Landscape& ls, float x, float y, int skip = -1) {
  for (int j = skip + 1; j < ls.n_mid; ++j) {
    const LsPine& p = ls.mid[j];
    if (absf(fl(p.x) - x) < 0.5f * 0.01f * fl(p.width_pct) * fl(p.h) + 6.f && fl(p.base_y - p.h) < y + 4.f) return true;
  }
  return false;
}

// The far layer that shows at (x, y), as the palette slot it is drawn in; SKY0 for the sky, GROUND below
// the ground line. Mid pines are left out (pineOver keeps targets away from them), but for `pine` >= 0: the
// top tier of that pine counts as TREES_MID.
int layerAt(const Landscape& ls, float x, float y, int pine = -1) {
  if (y >= fl(LS_GROUND_Y)) return GROUND;
  if (pine >= 0) {
    float apex, base, hw;
    pineTop(ls.mid[pine], apex, base, hw);
    if (y >= apex && y <= base && absf(x - fl(ls.mid[pine].x)) <= hw * (y - apex) / maxf(base - apex, 1e-3f)) return TREES_MID;
  }
  const float c2 = landscapeCrestAt(ls, 2, x);
  if (y >= c2 + 4.f) return RIDGE_NEAR;   // under the tree line's lower edge
  if (y >= toothTop(ls, x)) return TREES_FAR;
  if (y >= landscapeCrestAt(ls, 1, x)) return RIDGE_MID;
  if (y >= landscapeCrestAt(ls, 0, x)) return RIDGE_FAR;
  return SKY0;
}

// The layer at (x, y) only when it also shows kEdgeMargin px above, below and to both sides: a point well
// inside one flat fill, never on an antialiased edge. -1 otherwise.
int layerSafe(const Landscape& ls, float x, float y, int pine = -1) {
  const int l = layerAt(ls, x, y, pine);
  if (layerAt(ls, x, y - kEdgeMargin, pine) != l || layerAt(ls, x, y + kEdgeMargin, pine) != l) return -1;
  if (layerAt(ls, x - kEdgeMargin, y, pine) != l || layerAt(ls, x + kEdgeMargin, y, pine) != l) return -1;
  return l;
}

// The layer under a target, where the test reads it: the pixel 2 px under its point (its foot for a gong,
// its centre for a dot), and the point itself for a dot (so a dot never straddles two layers).
bool sitsOn(const Landscape& ls, Vec2 p, int layer, bool dot, int pine = -1) {
  const float px = floorf(p.x) + 0.5f, py = floorf(p.y) + 0.5f;
  if (layerSafe(ls, px, py + 2.f, pine) != layer) return false;
  return !dot || layerSafe(ls, px, py, pine) == layer;
}

float skyline(const Landscape& ls, float x) {
  return minf(minf(landscapeCrestAt(ls, 0, x), landscapeCrestAt(ls, 1, x)), toothTop(ls, x));
}

struct Placed {
  FarTarget t[4];
  int n;
};

bool spaced(const Placed& pl, float x) {
  for (int i = 0; i < pl.n; ++i)
    if (absf(pl.t[i].pos.x - x) < kSpacing) return false;
  return true;
}

void put(Placed& pl, Vec2 pos, uint8_t kind, int layer, float flight) {
  if (pl.n >= 4) return;
  FarTarget& t = pl.t[pl.n++];
  memset(&t, 0, sizeof t);
  t.pos = pos;
  t.kind = kind;
  t.layer = (uint8_t)layer;
  t.flight = flight;
}

// Where the skyline target at x would stand, and on what: false when it cannot stand there (an edge under
// it, a pine in front).
bool skySpot(const Landscape& ls, float x, Vec2& p, int& layer, float& flight) {
  int tooth = -1;
  const float tt = toothTop(ls, x, &tooth);
  if (tooth >= 0 && tt <= minf(landscapeCrestAt(ls, 0, x), landscapeCrestAt(ls, 1, x))) {
    // A tooth of the tree line is the highest point here: a plate on its tip (3 px into the tooth).
    const float xm = 0.5f * (fl(ls.spikes[tooth].x) + fl(ls.spikes[tooth + 1].x));
    p = Vec2{xm, landscapeCrestAt(ls, 2, xm) - fl(ls.spikes[tooth].h) + 3.f};
    layer = TREES_FAR;
    flight = kFlightSkyTooth;
  } else {
    // On a ridge: the foot at the lower of its two feet's crest, so neither foot floats on a slope.
    const bool far = landscapeCrestAt(ls, 0, x) <= landscapeCrestAt(ls, 1, x);
    p = Vec2{x, maxf(skyline(ls, x - kGongFoot), skyline(ls, x + kGongFoot)) + 0.5f};
    layer = far ? RIDGE_FAR : RIDGE_MID;
    flight = far ? kFlightSkyFar : kFlightSkyMid;
  }
  return !pineOver(ls, p.x, p.y + 2.f) && sitsOn(ls, p, layer, false);
}

// The gong on the skyline: the highest point of the window it can stand on (or a dot on the tooth that is).
bool placeSky(const Landscape& ls, float x0, float x1, Placed& pl) {
  float best = 1e9f, bf = 0.f;
  Vec2 bp{-1.f, 0.f};
  int bl = 0;
  for (float x = x0; x <= x1; x += kScanStep) {
    const float y = skyline(ls, x);
    Vec2 p;
    int layer;
    float flight;
    if (y >= best || !skySpot(ls, x, p, layer, flight) || !spaced(pl, p.x)) continue;
    best = y;
    bp = p;
    bl = layer;
    bf = flight;
  }
  if (bp.x < 0.f) return false;
  put(pl, bp, FT_SKY, bl, bf);
  return true;
}

// A plate in a tooth of the far tree line: the tallest tooth of at least `min_h` px in the window.
bool placeTree(const Landscape& ls, float x0, float x1, Placed& pl, int min_h, float flight) {
  int best = -1, best_h = 0;
  Vec2 bp{0.f, 0.f};
  for (int i = 0; i < ls.n_spikes; ++i) {
    const int h = ls.spikes[i].h;
    if (h < min_h || h <= best_h) continue;
    const float xm = 0.5f * (fl(ls.spikes[i].x) + fl(ls.spikes[i + 1].x));
    if (xm < x0 || xm > x1 || !spaced(pl, xm)) continue;
    const Vec2 p{xm, landscapeCrestAt(ls, 2, xm) - 0.55f * fl(h)};
    if (pineOver(ls, xm, p.y + 2.f) || !sitsOn(ls, p, TREES_FAR, true)) continue;
    best = i;
    best_h = h;
    bp = p;
  }
  if (best < 0) return false;
  put(pl, bp, FT_TREE, TREES_FAR, flight);
  return true;
}

// A painted rock on the middle ridge where most of it shows above the tree line.
bool placeRock(const Landscape& ls, float x0, float x1, Placed& pl) {
  float best = 0.f;
  Vec2 bp{-1.f, 0.f};
  for (float x = x0; x <= x1; x += kScanStep) {
    if (!spaced(pl, x)) continue;
    float occ = 1e9f, c1 = -1e9f;   // over the dot's width and a margin: the lowest tree top, the highest crest
    for (float dx = -4.f; dx <= 4.f; dx += 2.f) {
      occ = minf(occ, toothTop(ls, x + dx));
      c1 = maxf(c1, landscapeCrestAt(ls, 1, x + dx));
    }
    const float g1 = occ - c1;
    if (g1 < 9.f || g1 <= best) continue;
    const Vec2 p{x, landscapeCrestAt(ls, 1, x) + minf(6.f, g1 - 5.f)};
    if (pineOver(ls, x, p.y + 2.f) || !sitsOn(ls, p, RIDGE_MID, true)) continue;
    best = g1;
    bp = p;
  }
  if (bp.x < 0.f) return false;
  put(pl, bp, FT_ROCK, RIDGE_MID, kFlightRock);
  return true;
}

// Where the tin hangs in mid pine p: in the boughs half way down its top tier (at least 7 px under the
// tip). At the tip a pale dot read as a star on a Christmas tree (review of step B5, Mike 2026-11-18).
Vec2 coneSpot(const LsPine& p) {
  float apex, base, hw;
  pineTop(p, apex, base, hw);
  return Vec2{fl(p.x), floorf(apex + maxf(7.f, 0.5f * (base - apex))) + 0.5f};
}

// The last fallback for the rock: a tin hung in the tallest mid pine in the window that nothing stands in
// front of.
bool placeCone(const Landscape& ls, float x0, float x1, Placed& pl) {
  int best = -1;
  for (int j = 0; j < ls.n_mid; ++j) {   // sorted shortest first: the last that fits is the tallest
    const LsPine& p = ls.mid[j];
    const float x = fl(p.x);
    if (x < x0 || x > x1 || !spaced(pl, x)) continue;
    const Vec2 c = coneSpot(p);
    if (pineOver(ls, x, c.y, j) || !sitsOn(ls, c, TREES_MID, true, j)) continue;
    best = j;
  }
  if (best < 0) return false;
  put(pl, coneSpot(ls.mid[best]), FT_CONE, TREES_MID, kFlightCone);
  return true;
}

// A plate on a post on the far mountainside, where most of the far ridge shows.
bool placePost(const Landscape& ls, float x0, float x1, Placed& pl) {
  float best = 0.f;
  Vec2 bp{-1.f, 0.f};
  for (float x = x0; x <= x1; x += kScanStep) {
    if (!spaced(pl, x)) continue;
    float occ = 1e9f, c0 = -1e9f;
    for (float dx = -4.f; dx <= 4.f; dx += 2.f) {
      occ = minf(occ, minf(landscapeCrestAt(ls, 1, x + dx), toothTop(ls, x + dx)));
      c0 = maxf(c0, landscapeCrestAt(ls, 0, x + dx));
    }
    const float g0 = occ - c0;
    if (g0 < 8.f || g0 <= best) continue;
    const Vec2 p{x, landscapeCrestAt(ls, 0, x) + minf(5.f, g0 - 4.f)};
    if (pineOver(ls, x, p.y + 2.f) || !sitsOn(ls, p, RIDGE_FAR, false)) continue;
    best = g0;
    bp = p;
  }
  if (bp.x < 0.f) return false;
  put(pl, bp, FT_POST, RIDGE_FAR, kFlightPost);
  return true;
}

}  // namespace

// ---- the far targets ------------------------------------------------------------------------------------------

void longshotWindow(const Landscape& ls, float& x0, float& x1) {
  // Right of everything the camp pine draws (its foliage and the bag branch), a gong's reach clear of it,
  // and a gong's reach in from the frame's right edge.
  x0 = fl(landscapeCampPineBounds(ls).x1) + 12.f;
  x1 = fl(SS_WIDTH) - 18.f;
}

int longshotTargets(const Landscape& ls, FarTarget out[4]) {
  float x0, x1;
  longshotWindow(ls, x0, x1);
  Placed pl;
  pl.n = 0;
  // The order of placing is the order of priority: the skyline gong first (the farthest, the story's last
  // shot), then the tree plate, the rock (or the cone, or a second plate), the post.
  placeSky(ls, x0, x1, pl);
  if (!placeTree(ls, x0, x1, pl, 14, kFlightTree)) placeTree(ls, x0, x1, pl, 8, kFlightTree);
  // (a second plate in the tree line before the tin in a pine: a pale dot in a pine reads as a Christmas
  // ornament, review of step B5)
  if (!placeRock(ls, x0, x1, pl) && !placeTree(ls, x0, x1, pl, 8, kFlightTree2)) placeCone(ls, x0, x1, pl);
  placePost(ls, x0, x1, pl);
  // Too few (a narrow window crowded by pines): a second post or rock where one fits, so a string has three.
  if (pl.n < 3) placePost(ls, x0, x1, pl);
  if (pl.n < 3) placeRock(ls, x0, x1, pl);
  if (pl.n < 3) placeCone(ls, x0, x1, pl);
  // Nearest first: by flight (insertion sort; equal flights keep the order of placing).
  for (int i = 1; i < pl.n; ++i) {
    const FarTarget key = pl.t[i];
    int j = i - 1;
    while (j >= 0 && pl.t[j].flight > key.flight) {
      pl.t[j + 1] = pl.t[j];
      --j;
    }
    pl.t[j + 1] = key;
  }
  for (int i = 0; i < pl.n; ++i) out[i] = pl.t[i];
  return pl.n;
}

void longshotTargetGlyph(const FarTarget& t, Glyph& g) {
  memset(&g, 0, sizeof g);
  g.pos = t.pos;
  g.facing = 1;
  g.base_y = (int16_t)floorf(t.pos.y);
  const bool gong = t.kind == FT_POST || (t.kind == FT_SKY && t.layer != TREES_FAR);
  if (gong) {
    g.kind = GL_SHAPE;
    g.shape = SH_GONG;
    g.slot = STAR;
    g.scale = (t.kind == FT_POST ? kGongPost : kGongSky) * kLsTargetScale;
    return;
  }
  g.kind = GL_DOT;
  // Every target pale (STAR): in the stills a TRUNK cone, and then a ROCK one, vanished on the mid pine's
  // green at panel size (Mike, 2026-11-18: the targets must read). The cone is a pale tin on the treetop.
  g.slot = STAR;
  float r = kDotTree;
  if (t.kind == FT_ROCK) r = kDotRock;
  else if (t.kind == FT_CONE) r = kDotCone;
  else if (t.kind == FT_SKY) r = kDotSkyTooth;
  g.scale = r * kLsTargetScale;
}

// ---- the shapes ---------------------------------------------------------------------------------------------------

float longshotShapeReach(uint8_t shape) {
  if (shape == SH_GONG) return 8.f;    // the top bar's ends (7.3 px) plus the line's half width, and the glint
  if (shape == SH_FLAG) return 26.f;   // the stick's top plus a ribbon streamed level (24.1 px), plus its width
  return 12.f;
}

void longshotShapeDraw(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal) {
  if (g.scale <= 0.05f) return;
  const float k = g.scale;
  const float f = g.facing < 0 ? -1.f : 1.f;
  const Vec2 o = g.pos;
  if (g.shape == SH_GONG) {
    const Pixel legs = pal[ACTOR];
    strokeCapsule(s, Vec2{o.x - f * kGongFoot * k, o.y}, Vec2{o.x - f * kGongTop * k, o.y - kGongH * k}, kGongLegW * k, legs, clip);
    strokeCapsule(s, Vec2{o.x + f * kGongFoot * k, o.y}, Vec2{o.x + f * kGongTop * k, o.y - kGongH * k}, kGongLegW * k, legs, clip);
    strokeCapsule(s, Vec2{o.x - kGongBar * k, o.y - kGongH * k}, Vec2{o.x + kGongBar * k, o.y - kGongH * k}, kGongLegW * k, legs, clip);
    // The plate swings as a pendulum from the bar's middle: it rises a little at the ends of each swing.
    // Held to +-1.1 rad, the most for which the glinting plate stays inside longshotShapeReach (the plan
    // swings it 0.6), so the bounds hold whatever the caller sets.
    const float a = minf(maxf(g.k, -1.1f), 1.1f) * (1.f / 6.2831853f);
    const Vec2 c{o.x + f * kGongHang * k * sinT(a), o.y - kGongH * k + kGongHang * k * cosT(a)};
    if (g.t > 0.f) fillCircle(s, c, kGongGlintR * k, pal[ACTOR_LIGHT], clip);
    else fillCircle(s, c, kGongPlateR * k, pal[(Slot)g.slot], clip);
    return;
  }
  if (g.shape == SH_FLAG) {
    const Vec2 top{o.x, o.y - kFlagH * k};
    strokeCapsule(s, o, top, kFlagStickW * k, pal[TRUNK], clip);
    // The ribbon: each piece hangs further from straight down the stronger the breeze, toward -x always,
    // and flutters on a wave that runs out along it (each piece a little later and a little wider).
    const float b = clamp01(g.k);
    Vec2 p = top;
    for (int i = 0; i < 3; ++i) {
      const float amp = (0.10f + 0.25f * b) * (0.75f + 0.25f * fl(i));
      const float ang = 0.15f + 1.25f * b + amp * sinT(g.t - 0.22f * fl(i));
      const float t = ang * (1.f / 6.2831853f);
      const Vec2 q{p.x - kRibbonSeg * k * sinT(t), p.y + kRibbonSeg * k * cosT(t)};
      strokeCapsule(s, p, q, kRibbonW * k, pal[(Slot)g.slot], clip);
      p = q;
    }
  }
}

// ---- the major (step C3, 2026-11-19) ----------------------------------------------------------------------------
//
// LONGRANGE_PLAN sections 5 and 6 as INTEGRATION_PLAN and REVIEW_FIXES F13 and F14 changed them, with the owner's
// decisions of 2026-11-18 (the stem stays in his mouth while he lies and shoots; the still sheets' looks pass:
// the aim line, the firing spot, the gentle look of the targets). The script is the hiker's half; the tick
// (longshotMajorTick) reads the module's events and shows everything out on the hills. No random draws here:
// the shots' two values each come from DayPlink::rng (the module's rule), everything else is the plan's byte,
// the layout and the beat's own clock.
//
// MajorState: p[0] the firing spot FP (his hips, prone), p[1] the bag's place (its foot), p[2] the flag's foot,
// p[3] unused; count: the shot (an entry of the string's table); cue kE1 the shot landed (late: at the report),
// kE2 C's late gong heard (from the clock, F14). c[0..2]: the camp startle's critters (plinkStartle).

namespace {

constexpr uint16_t kLanded = 256, kHeard = 512;   // (majors.cpp's kE1, kE2)
constexpr uint8_t kVarFog = 16;                   // the fog no-show (the begin hook sets it; DAY_NOSHOW is 8)

enum FarPre : uint8_t { PRE_FLAG = 1, PRE_DIAL = 2 };

// ---- tuning (PROPOSED; how it looks is UNVERIFIED on the panel) -------------------------------------------------

constexpr float kWalkOut = 30.f, kWalk = 34.f;   // REVIEW_FIXES F13: the walk out to the line, the other walks
constexpr float kFpDx = 86.f, kFpDy = -34.f;     // the firing spot from the fire (camp.view 6 px left)
constexpr float kBagDx = 46.f;                   // the bag's foot ahead of his hips (step B5: the forend rests on it)
constexpr float kFlagDx = 112.f;                 // the flag planted ahead of his hips
constexpr float kGlassAt[4] = {0.9f, 1.5f, 2.1f, 2.7f};   // the targets show, nearest first (s into the glassing)
constexpr float kGlassS = 3.4f;
constexpr float kGlintS = 0.12f;                 // 3 ticks
constexpr float kDotSwing = 1.2f, kDotSwingS = 1.2f;      // a hit dot shivers sideways
constexpr float kGongSwing = 0.6f, kGongSwingS = 1.6f, kGongSwingLateS = 4.f;
constexpr float kSwingHz = 2.2f;
constexpr float kPuffS = 0.8f, kPuffR0 = 1.5f, kPuffR1 = 3.5f;
constexpr float kArcsS = 1.1f, kArcsTravel = 46.f;
constexpr float kEchoAt = 1.6f, kEchoS = 1.3f;
constexpr float kBirdsS = 3.2f;
constexpr float kDropS = 0.8f, kDropG = 220.f;
constexpr float kHeardAfter = 1.1f;              // C's late shot: heard this long after the bullet's flight (F14)

MajorState& M(World& w) { return w.major; }
DayPlink& D(World& w) { return w.day.plink; }
PlinkState& P(World& w) { return w.day.plink.p; }
FarState& Fs(World& w) { return w.day.plink.far; }
bool live(const World& w) { return w.major.on && w.major.id == MJ_LONGSHOT && w.day.kind == DAY_PLINK; }
float ramp(float u, float a, float b) { return easeInOut(window(u, a, b)); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float wrap1(float t) { return t - floorf(t); }

// ---- the strings (LONGRANGE_PLAN 5.1) ----

struct Shot {
  uint8_t target, result, pre;
  float hold, wind;   // the hold before the shot (s; the module adds up to 0.5); a wind miss's share of the drift
};

uint8_t nShots(uint8_t var) { return (var & 3u) == 0u ? 4u : 5u; }

// Entry k of the variant's string over n targets (nearest first). Pure.
Shot shotOf(uint8_t var, int n, int k) {
  Shot s{0, FR_HIT, 0, 3.f, 0.f};
  const int last = n > 0 ? n - 1 : 0;
  const int second = n > 1 ? 1 : 0, third = n > 2 ? 2 : last;
  static const float kHoldA[4] = {3.8f, 2.9f, 3.1f, 3.3f};
  switch (var & 3u) {
    case 1:   // B, the wind beats him: nearest; the second twice in the wind; the flag, the dial, the second; the third
      switch (k) {
        case 0: s = Shot{0, FR_HIT, 0, 3.8f, 0.f}; break;
        case 1: s = Shot{(uint8_t)second, FR_WIND, 0, 3.0f, 1.f}; break;
        case 2: s = Shot{(uint8_t)second, FR_WIND, 0, 3.2f, 0.6f}; break;
        case 3: s = Shot{(uint8_t)second, FR_HIT, PRE_FLAG | PRE_DIAL, 3.0f, 0.f}; break;
        default: s = Shot{(uint8_t)third, FR_LAST, 0, 2.8f, 0.f}; break;
      }
      break;
    case 2: {   // C, the far one: nearest; third; farthest over the top; the dial, low; the flag, the long breath, late
      const int mid = n >= 4 ? 2 : second;
      switch (k) {
        case 0: s = Shot{0, FR_HIT, 0, 3.8f, 0.f}; break;
        case 1: s = Shot{(uint8_t)mid, FR_HIT, 0, 2.9f, 0.f}; break;
        case 2: s = Shot{(uint8_t)last, FR_OVER, 0, 3.3f, 0.f}; break;
        case 3: s = Shot{(uint8_t)last, FR_LOW, PRE_DIAL, 3.1f, 0.f}; break;
        default: s = Shot{(uint8_t)last, FR_LATE, PRE_FLAG, 4.6f, 0.f}; break;
      }
      break;
    }
    default: {   // A, walking them out: each target once, nearest first; the last brings the quiet fist
      const int kk = k < 3 ? k : 3;
      const int t = kk < n ? kk : last;
      s = Shot{(uint8_t)t, (k >= 3 || t == last) ? (uint8_t)FR_LAST : (uint8_t)FR_HIT, 0, kHoldA[kk], 0.f};
      break;
    }
  }
  return s;
}

int stringLength(uint8_t var, int n) {
  if ((var & 3u) == 0u) return n < 4 ? (n < 1 ? 1 : n) : 4;   // A: one shot a target
  return nShots(var);
}

// ---- glyphs (majors.cpp's glyphNew is private to it) ----

uint8_t glyphAdd(World& w, uint8_t kind, Vec2 pos, int base_y) {
  for (int i = 0; i < GLYPH_MAX; ++i) {
    Glyph& g = M(w).g[i];
    if (g.kind != GL_NONE) continue;
    memset(&g, 0, sizeof g);
    g.kind = kind;
    g.pos = g.q = pos;
    g.facing = 1;
    g.scale = 0.f;
    g.slot = ACTOR;
    g.base_y = (int16_t)base_y;
    return (uint8_t)(i + 1);
  }
  return 0;
}
Glyph* G(World& w, uint8_t i1) {
  if (i1 == 0 || i1 > GLYPH_MAX) return nullptr;
  Glyph& g = M(w).g[i1 - 1];
  return g.kind == GL_NONE ? nullptr : &g;
}
void glyphDrop(World& w, uint8_t& i1) {
  if (Glyph* g = G(w, i1)) g->kind = GL_NONE;
  i1 = 0;
}

bool isGong(const FarTarget& t) { return t.kind == FT_POST || (t.kind == FT_SKY && t.layer != TREES_FAR); }
// Where he aims: the plate's middle on a gong, the dot itself otherwise.
Vec2 aimOf(const FarTarget& t) {
  if (!isGong(t)) return t.pos;
  const float k = (t.kind == FT_POST ? kGongPost : kGongSky) * kLsTargetScale;
  return Vec2{t.pos.x, t.pos.y - (kGongH - kGongHang) * k};
}
// The rest glyph of target i, at its swing now.
void targetPose(World& w, int i) {
  FarTarget& t = Fs(w).tg[i];
  Glyph* g = G(w, t.glyph);
  if (!g) return;
  Glyph rest;
  longshotTargetGlyph(t, rest);
  if (!(t.shown & 1) || ((t.shown & 2) != 0 && Fs(w).drop_t < 0.f)) {   // not picked out yet, or the tin has fallen
    g->scale = 0.f;
    return;
  }
  g->scale = rest.scale;
  g->pos = rest.pos;
  g->k = 0.f;
  g->t = 0.f;
  if (t.sw_t >= 0.f && t.sw_amp > 0.f) {
    const float len = isGong(t) ? ((t.shown & 4) ? kGongSwingLateS : kGongSwingS) : kDotSwingS;
    const float u = t.sw_t / len;
    if (u >= 1.f) {
      t.sw_t = -1.f;
    } else {
      const float a = t.sw_amp * (1.f - u) * (1.f - u) * sinT(kSwingHz * t.sw_t);
      if (isGong(t)) g->k = a;
      else g->pos.x = rest.pos.x + a;
    }
  }
  if ((t.shown & 2) && Fs(w).drop_t >= 0.f) {   // the tin out of its pine, falling
    const float d = Fs(w).drop_t;
    g->pos.y = rest.pos.y + 0.5f * kDropG * d * d;
  }
  if (isGong(t) && Fs(w).glint_t > 0.f && Fs(w).target == (uint8_t)i && t.sw_t >= 0.f) g->t = 1.f;
}

// A glint (ACTOR_LIGHT) on a dot target for three ticks.
void glintAt(World& w, Vec2 at) {
  FarState& f = Fs(w);
  if (!f.g_glint) f.g_glint = glyphAdd(w, GL_DOT, at, (int)floorf(at.y));
  if (Glyph* g = G(w, f.g_glint)) {
    g->pos = at;
    g->slot = STAR;   // (ACTOR_LIGHT is the warm lit side of a figure: on a hazy ridge it read as a brown spot)
    g->scale = 2.5f * kLsTargetScale;
    g->base_y = (int16_t)floorf(at.y);
  }
  f.glint_t = kGlintS;
}

// The angle (turns, 0 along +x, toward +y) of the vector d, from the plinking's table-free atan2.
float turnsOf(Vec2 d) { return (0.5f * 3.14159265f - plinkAngle(d.x, d.y)) * (1.f / 6.2831853f); }

// One set of arcs on its way from `from` toward the firing spot: u 0..1 of its travel.
void arcsPose(Glyph& g, Vec2 from, Vec2 fp, float u, float s0, float s1, float travel) {
  Vec2 d{fp.x - from.x, fp.y - from.y};
  const float L = sqrtf(d.x * d.x + d.y * d.y);
  if (L > 1e-3f) d = Vec2{d.x / L, d.y / L};
  const float e = easeOutCubic(u);
  g.pos = Vec2{from.x + d.x * travel * e, from.y + d.y * travel * e};
  g.rot = turnsOf(d);
  g.scale = lerp(s0, s1, u);
  g.k = 1.4f * (u < 0.6f ? 1.f : 1.f - (u - 0.6f) / 0.4f);
  g.base_y = (int16_t)floorf(from.y);
}

// ---- the hiker's actions -------------------------------------------------------------------------------------

void crouchAt0(World& w, float u) {
  if (u <= 0.f) scriptSetPose(w, POSE_CROUCH, 0.6f);
}

// The glassing: the hand at the brim (POSE_LOOK_AROUND held by the step before); the targets show one by one,
// nearest first, each with its glint, and his head tips up a little more for each.
void actGlass(World& w, float u) {
  FarState& f = Fs(w);
  const float t = u * kGlassS;
  int shown = 0;
  for (int i = 0; i < f.n && i < 4; ++i) {
    if (t < kGlassAt[i]) continue;
    ++shown;
    FarTarget& tg = f.tg[i];
    if (!(tg.shown & 1)) {
      tg.shown = (uint8_t)(tg.shown | 1);
      f.target = (uint8_t)i;
      if (isGong(tg)) tg.sw_t = 0.f, tg.sw_amp = 0.f, f.glint_t = kGlintS;   // the plate's own glint
      else glintAt(w, tg.pos);
    }
  }
  w.fx.head = -0.03f * (float)shown * ramp(u, 0.f, 0.2f);
  if (u >= 1.f) w.fx.head = -0.03f * (float)shown;   // (held through the stillness after; the next pose rests it)
}
void actGlassEnd(World& w, float u) {
  w.fx.head = -0.03f * (float)Fs(w).n * (1.f - ramp(u, 0.f, 1.f));
  if (u >= 1.f) scriptFxRest(w);
}

// The two halves out of the pack, pointing at the ground (the plinking's, without its stick and sack lines).
void actRifleOut(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.6f) {
    plinkRifle(P(w), true, 0.f);
    const float k = ramp(u, 0.6f, 0.85f);
    scriptSetArm(w, 0, k, 0.55f, 0.35f);
    scriptSetArm(w, 1, k, 0.55f, 0.35f);
  }
  if (u >= 1.f) scriptFxRest(w);
}

// A second crouch: the bag in his far hand, then the flag in the same hand, upright.
void actKitOut(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.4f && P(w).sack != PI_HAND) plinkSack(P(w), PI_HAND);
  if (u >= 0.6f && !Fs(w).g_flag) {
    Fs(w).flag_held = 1;
    Fs(w).g_flag = glyphAdd(w, GL_SHAPE, w.hiker.feet, (int)floorf(w.hiker.feet.y) + 1);
    if (Glyph* g = G(w, Fs(w).g_flag)) {
      g->shape = SH_FLAG;
      g->slot = MUSHROOM;
      g->scale = 1.f;
      g->k = weatherBreeze(w);
    }
  }
  if (u >= 1.f) scriptFxRest(w);
}

// The stick goes into the ground at the flag's place.
void actPlant(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f && Fs(w).flag_held) {
    Fs(w).flag_held = 0;
    if (Glyph* g = G(w, Fs(w).g_flag)) {
      g->pos = M(w).p[2];
      g->base_y = (int16_t)floorf(M(w).p[2].y);
    }
  }
  if (u >= 1.f) scriptFxRest(w);
}
// He stands and watches the ribbon: the head follows it down. Pure stillness.
void actWatchFlag(World& w, float u) {
  w.fx.head = 0.14f * scriptEnvelope(u, 0.25f, 0.25f);
  if (u >= 1.f) scriptFxRest(w);
}
void actBagDown(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f && P(w).sack == PI_HAND) {
    plinkLay(P(w), M(w).p[1]);
    plinkSack(P(w), PI_DOWN);
  }
  if (u >= 1.f) scriptFxRest(w);
}
void actBagUp(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f && P(w).sack == PI_DOWN) plinkSack(P(w), PI_HAND);
  if (u >= 1.f) scriptFxRest(w);
}
void actPull(World& w, float u) {
  crouchAt0(w, u);
  if (u >= 0.5f && !Fs(w).flag_held && Fs(w).g_flag) Fs(w).flag_held = 1;
  if (u >= 1.f) scriptFxRest(w);
}
void actLookBack(World& w, float u) {
  w.fx.head = -0.16f * scriptEnvelope(u, 0.3f, 0.3f);
  if (u >= 1.f) scriptFxRest(w);
}

// Loading and unloading lying down (the plinking's acts swing the far arm down to a hip pocket, which prone
// put his arm through the ground): the far hand back along his side to the pocket, forward under the action,
// the tug of the handle. The same state changes at the same moments as plinkActLoad and plinkActUnload; the
// near hand keeps the rifle on the bag (step B5's note).
void actProneLoad(World& w, float u) {
  PlinkState& p = P(w);
  const float pocket = ramp(u, 0.f, 0.2f) * (1.f - ramp(u, 0.3f, 0.45f));
  const float under = ramp(u, 0.3f, 0.5f) * (1.f - ramp(u, 0.65f, 0.75f));
  const float tug = ramp(u, 0.72f, 0.82f) * (1.f - ramp(u, 0.86f, 0.96f));
  const float k = clampf(pocket + under + tug, 0.f, 1.f);
  if (pocket >= under + tug) scriptSetArm(w, 1, k, -1.35f, 0.25f);
  else if (tug > under) scriptSetArm(w, 1, k, 1.55f, 0.45f);
  else scriptSetArm(w, 1, k, 1.30f, 0.85f);
  if (u >= 0.3f && p.mag == MAG_NONE) plinkMag(p, MAG_HAND);
  if (u >= 0.6f && p.mag == MAG_HAND) {
    plinkMag(p, MAG_IN);
    p.rounds = 8;
  }
  if (u >= 0.9f) plinkLoad(p);
  if (u >= 1.f) scriptFxRest(w);
}
void actProneUnload(World& w, float u) {
  PlinkState& p = P(w);
  const float pocket = ramp(u, 0.05f, 0.25f) * (1.f - ramp(u, 0.45f, 0.55f));
  const float hold = ramp(u, 0.55f, 0.65f) * (1.f - ramp(u, 0.9f, 1.f));
  const float k = clampf(pocket + hold, 0.f, 1.f);
  if (pocket >= hold) scriptSetArm(w, 1, k, u < 0.3f ? 1.30f : -1.35f, u < 0.3f ? 0.85f : 0.25f);
  else scriptSetArm(w, 1, k, 1.50f, 0.55f);
  if (u >= 0.2f && p.mag == MAG_IN) plinkMag(p, MAG_HAND);   // the magazine leaves the rifle
  if (u >= 0.5f && p.mag == MAG_HAND) plinkMag(p, MAG_NONE);   // and goes into his pocket
  if (u >= 0.55f) plinkUnload(p);                              // the chamber is clear
  w.fx.head = 0.3f * ramp(u, 0.6f, 0.72f) * (1.f - ramp(u, 0.88f, 1.f));   // a look into the action
  if (u >= 1.f) scriptFxRest(w);
}

// Down: onto one knee, the hands, the legs slid back (plinkBend's prone ramp, 2.4 s).
void actDown(World& w, float u) {
  if (u <= 0.f) {
    plinkRest(P(w), REST_PRONE);
    plinkKneel(P(w), true);
  }
}
// The elbows shuffle once, the cocked near heel comes down flat (step B5's note: ramp the settle 0 to 1).
void actSettle(World& w, float u) {
  plinkSettle(P(w), ramp(u, 0.25f, 0.85f));
  w.fx.lean = 0.02f * sinT(window(u, 0.f, 0.5f)) * (u < 0.5f ? 1.f : 0.f);
  if (u >= 1.f) scriptFxRest(w);
}

// ---- one shot ----

void doShotBegin(World& w, float) {
  FarState& f = Fs(w);
  const Shot s = shotOf(M(w).var, f.n, M(w).count);
  f.target = s.target;
  f.result = s.result;
  f.pre = s.pre;
  M(w).cue = (uint16_t)(M(w).cue & ~(kLanded | kHeard));
}
bool preFlag(const World& w) { return (w.day.plink.far.pre & PRE_FLAG) != 0; }
bool preDial(const World& w) { return (w.day.plink.far.pre & PRE_DIAL) != 0; }
bool notLate(const World& w) { return w.day.plink.far.result != FR_LATE; }
bool isLate(const World& w) { return w.day.plink.far.result == FR_LATE; }

// The cheek off the stock, the head turned down toward the ribbon, held, back on.
void actReadFlag(World& w, float u) {
  plinkHeadOff(P(w), ramp(u, 0.f, 0.15f) * (1.f - ramp(u, 0.85f, 1.f)));
  w.fx.head = 0.18f * ramp(u, 0.12f, 0.3f) * (1.f - ramp(u, 0.75f, 0.9f));
  if (u >= 1.f) {
    plinkHeadOff(P(w), 0.f);
    scriptFxRest(w);
  }
}
// The near hand back from the forend to the scope's turret, two small turns of the wrist, back. (The far hand
// holds the grip, so the rifle stays where it is.)
void actDial(World& w, float u) {
  FarState& f = Fs(w);
  if (u <= 0.f) {
    f.arm_sh = w.hiker.pose.shoulder[0];
    f.arm_el = w.hiker.pose.elbow[0];
  }
  const float k = ramp(u, 0.f, 0.25f) * (1.f - ramp(u, 0.8f, 1.f));
  const float turn = 0.07f * sinT(2.f * window(u, 0.3f, 0.75f));
  scriptSetArm(w, 0, k, f.arm_sh - 0.12f, f.arm_el + 0.55f + turn);
  plinkHeadOff(P(w), 0.35f * k);
  if (u >= 1.f) {
    plinkHeadOff(P(w), 0.f);
    scriptFxRest(w);
  }
}
void doFire(World& w, float) {
  FarState& f = Fs(w);
  PlinkState& p = P(w);
  const Shot s = shotOf(M(w).var, f.n, M(w).count);
  const FarTarget& t = f.tg[f.target < f.n ? f.target : 0];
  plinkQueueClear(p);
  plinkFar(p, aimOf(t), t.flight, s.hold);
  plinkStart(p);
  f.in_hold = 0;
}
bool shotLanded(const World& w) { return (w.major.cue & kLanded) != 0; }
bool heard(const World& w) { return (w.major.cue & kHeard) != 0; }

void actWatch(World&, float) {}   // he stays on the scope: nothing moves

// The quiet fist with the near hand (the far hand keeps the grip): off the forend, the forearm up, down once.
// (armBase, at the act's first tick, keeps the arm as the bend has it: the fist starts from there.)
void armBase(World& w) {
  Fs(w).arm_sh = w.hiker.pose.shoulder[0];
  Fs(w).arm_el = w.hiker.pose.elbow[0];
}
void fist(World& w, float u) {
  const FarState& f = Fs(w);
  const float k = ramp(u, 0.f, 0.25f) * (1.f - ramp(u, 0.75f, 1.f));
  const float bump = 0.12f * sinT(0.5f * window(u, 0.35f, 0.65f));
  scriptSetArm(w, 0, k, f.arm_sh + 0.15f, f.arm_el + 0.35f - bump);
}

void actReact(World& w, float u) {
  FarState& f = Fs(w);
  PlinkState& p = P(w);
  if (u <= 0.f) armBase(w);
  switch (f.result) {
    case FR_HIT:   // a nod on the stock
      w.fx.head = 0.06f * scriptEnvelope(window(u, 0.f, 0.3f), 0.4f, 0.4f);
      break;
    case FR_LAST:   // the cheek comes off, the head up, a look; then the quiet fist
      plinkHeadOff(p, ramp(u, 0.f, 0.15f) * (1.f - ramp(u, 0.9f, 1.f)));
      w.fx.head = -0.06f * ramp(u, 0.05f, 0.2f) * (1.f - ramp(u, 0.85f, 1.f));
      if (u >= 0.5f) fist(w, window(u, 0.5f, 0.95f));
      break;
    case FR_WIND:
      if (f.wind_miss <= 1) w.fx.head = -0.1f * scriptEnvelope(window(u, 0.f, 0.7f), 0.3f, 0.4f);   // a lift, settles back
      else w.fx.head = 0.08f * sinT(2.f * window(u, 0.05f, 0.9f));   // the slow shake, two times
      break;
    case FR_OVER:   // where did that go: the head up, a tilt, held
      plinkHeadOff(p, 0.8f * ramp(u, 0.f, 0.15f) * (1.f - ramp(u, 0.9f, 1.f)));
      w.fx.head = -0.07f * ramp(u, 0.15f, 0.3f) * (1.f - ramp(u, 0.85f, 1.f));
      break;
    case FR_LOW:   // a breath out
      w.fx.lean = 0.02f * scriptEnvelope(u, 0.3f, 0.4f);
      break;
    default: {   // C's late shot: sure he pulled it, he comes off the scope, looks down at the action, shakes his head once
      plinkHeadOff(p, ramp(u, 0.25f, 0.4f));
      w.fx.head = 0.16f * ramp(u, 0.3f, 0.45f) + 0.07f * sinT(window(u, 0.5f, 0.95f));
      break;
    }
  }
  if (u >= 1.f) {
    if (f.result != FR_LATE) plinkHeadOff(p, 0.f);
    scriptFxRest(w);
    if (f.result == FR_LATE) w.fx.head = 0.16f;   // (still looking down: the next step brings it up)
  }
}
// C: the ring comes back off the hill and his head comes up fast; he holds, then the quiet fist.
void actLookUp(World& w, float u) {
  PlinkState& p = P(w);
  if (u <= 0.f) armBase(w);
  plinkHeadOff(p, 1.f - 0.4f * ramp(u, 0.8f, 1.f));
  w.fx.head = lerp(0.16f, -0.08f, ramp(u, 0.f, 0.16f)) * (1.f - ramp(u, 0.85f, 1.f));
  if (u >= 0.45f) fist(w, window(u, 0.45f, 0.95f));
  if (u >= 1.f) {
    plinkHeadOff(p, 0.f);
    scriptFxRest(w);
  }
}
void doShotEnd(World& w, float) {
  FarState& f = Fs(w);
  if (M(w).count < 255) ++M(w).count;
  f.in_hold = 0;
  w.hiker.breath_rate = 1.f;
  plinkHeadOff(P(w), 0.f);
}
bool moreShots(const World& w) {
  return w.major.count < stringLength(w.major.var, w.day.plink.far.n);
}

// The string is over: the rifle comes down onto the bag (step B5's note: the stay off, an empty queue run).
void doLower(World& w, float) {
  PlinkState& p = P(w);
  plinkStay(p, false);
  plinkQueueClear(p);
  plinkStart(p);
  plinkHeadOff(p, 0.f);
  w.hiker.breath_rate = 1.f;
}
void actUp(World& w, float u) {
  if (u <= 0.f) plinkKneel(P(w), false);
  if (u >= 1.f) plinkSettle(P(w), 0.f);
}

// At the pack: the bag and the flag in, then the rifle apart and both halves in (the plinking's arm work).
void actStowAll(World& w, float u) {
  PlinkState& p = P(w);
  FarState& f = Fs(w);
  crouchAt0(w, u);
  if (u >= 0.2f && p.sack == PI_HAND) {
    plinkSack(p, PI_NONE);
    f.flag_held = 0;
    glyphDrop(w, f.g_flag);
  }
  const float k = ramp(u, 0.3f, 0.45f) * (1.f - ramp(u, 0.7f, 0.8f));
  scriptSetArm(w, 0, k, 0.55f, 0.35f);
  scriptSetArm(w, 1, k, 0.55f, 0.35f);
  if (u >= 0.3f && u < 0.75f) plinkRifle(p, true, 1.f - ramp(u, 0.35f, 0.65f));
  if (u >= 0.75f) plinkRifle(p, false, 0.f);
  if (u >= 1.f) scriptFxRest(w);
}

bool dogsClear(const World& w) { return houseDogsClear(w); }
bool crittersGone(const World& w) {
  for (int i = 0; i < CRITTER_MAX; ++i)
    if (w.crit[i].in_use && w.crit[i].tag == CT_MAJOR) return false;
  return true;
}

// The fog no-show: hand to the brim, nothing to see, a slow shake of the head.
void actFogShake(World& w, float u) {
  w.fx.head = 0.07f * sinT(1.5f * window(u, 0.1f, 0.9f));
  if (u >= 1.f) scriptFxRest(w);
}

#define LS_ONE_SHOT                                                                                                  \
  doit(doShotBegin), skipUnless(preFlag, 1), act(actReadFlag, 2.4f), skipUnless(preDial, 1), act(actDial, 2.2f),   \
      doit(doFire), until(shotLanded), skipUnless(notLate, 1), act(actWatch, 1.3f), act(actReact, 1.8f),             \
      skipUnless(isLate, 2), until(heard), act(actLookUp, 1.6f), doit(doShotEnd), still(0.9f), repeatWhile(moreShots, 15)

const Step kLongshot[] = {
    // Reading the range
    pose(POSE_STAND, 0.8f, 0.3f), face(RIGHT), pose(POSE_LOOK_AROUND, 0.6f, 0.1f), act(actGlass, kGlassS), still(0.6f),
    pose(POSE_STAND, 0.6f, 0.1f), act(actGlassEnd, 0.4f),
    // The kit
    go(AT_PACK_WORK, kWalk), face(LEFT), act(actRifleOut, 2.0f), pose(POSE_STAND, 0.6f, 0.2f), act(plinkActAssemble, 2.0f),
    act(actKitOut, 1.6f), pose(POSE_STAND, 0.6f, 0.2f),
    // Out to the line: the flag, a look at it, the bag, the firing spot
    go(AT_M2, kWalkOut, -14.f, 0.f), face(RIGHT), act(actPlant, 1.4f), pose(POSE_STAND, 0.6f, 0.2f), act(actWatchFlag, 1.6f),
    walk(AT_M1, kWalk, -16.f, 0.f), face(RIGHT), act(actBagDown, 1.2f), pose(POSE_STAND, 0.6f, 0.2f),
    walk(AT_M0, kWalk), face(RIGHT), until(dogsClear),
    // Down and loaded
    act(actDown, 2.5f), act(actProneLoad, 2.2f), act(actSettle, 2.6f), still(1.0f),
    // The string
    LS_ONE_SHOT,
    // Unloaded, looked into, and only then up
    doit(doLower), still(1.2f), act(actProneUnload, 2.2f), act(actUp, 2.6f),
    // Clearing the line
    walk(AT_M1, kWalk, -16.f, 0.f), face(RIGHT), act(actBagUp, 1.2f), pose(POSE_STAND, 0.5f, 0.1f),
    walk(AT_M2, kWalk, -14.f, 0.f), face(RIGHT), act(actPull, 1.2f), pose(POSE_STAND, 0.5f, 0.1f), act(actLookBack, 1.2f),
    go(AT_PACK_WORK, kWalk), face(LEFT), act(actStowAll, 2.8f), pose(POSE_STAND, 0.6f, 0.2f),
    until(crittersGone), go(AT_SIT, kWalk), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f),
};
#undef LS_ONE_SHOT

const Step kFogNoShow[] = {
    pose(POSE_STAND, 0.8f, 0.3f), face(RIGHT), pose(POSE_LOOK_AROUND, 0.6f, 3.0f), pose(POSE_STAND, 0.6f, 0.2f),
    still(0.6f), act(actFogShake, 2.4f), still(0.8f), go(AT_SIT, kWalk), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f),
};

// ---- the far side ----

void farResult(World& w) {
  FarState& f = Fs(w);
  if (f.target >= f.n) return;
  FarTarget& t = f.tg[f.target];
  const float s = weatherBreeze(w);
  switch (f.result) {
    case FR_HIT: case FR_LAST: case FR_LATE:
      if (t.kind == FT_CONE) {   // the tin drops out of its pine
        t.shown = (uint8_t)(t.shown | 2);
        f.drop_t = 0.f;
      }
      t.sw_t = 0.f;
      t.sw_amp = isGong(t) ? kGongSwing : kDotSwing;
      if (f.result == FR_LATE) t.shown = (uint8_t)(t.shown | 4);
      f.glint_t = kGlintS;
      if (!isGong(t)) glintAt(w, t.pos);
      if (isGong(t)) {   // a gong rings: the ring comes back off the hill
        if (!f.g_arcs) f.g_arcs = glyphAdd(w, GL_ARCS, t.pos, (int)floorf(t.pos.y));
        f.arcs_t = 0.f;
      }
      if (!f.birds_done) {   // the first hit: three far birds lift from the tree line beside the nearest target
        f.birds_done = 1;
        f.bird_t = 0.f;
        for (int k = 0; k < 3; ++k) {
          const Vec2 at{f.tg[0].pos.x + (float)(k * 7 - 7), f.tg[0].pos.y - 3.f - (float)(k & 1) * 2.f};
          f.g_bird[k] = glyphAdd(w, GL_SHAPE, at, (int)floorf(at.y));
          if (Glyph* g = G(w, f.g_bird[k])) {
            g->shape = SH_PERCH;
            g->k = 1.f;
            g->scale = 0.5f;
            g->facing = (int8_t)(k == 0 ? -1 : 1);
            g->q = at;
            g->t = 0.25f * (float)k;
          }
        }
      }
      break;
    case FR_WIND: case FR_LOW: {
      if (f.result == FR_WIND && f.wind_miss < 255) ++f.wind_miss;
      const Shot sh = shotOf(M(w).var, f.n, M(w).count);
      f.puff_at = f.result == FR_WIND ? Vec2{t.pos.x - (4.f + 9.f * s) * sh.wind, t.pos.y} : Vec2{t.pos.x, t.pos.y + 7.f};
      if (!f.g_puff) f.g_puff = glyphAdd(w, GL_DOT, f.puff_at, (int)floorf(f.puff_at.y));
      f.puff_t = 0.f;
      break;
    }
    default: break;   // over the top: nothing at all to see
  }
}

void farTick(World& w, float dt) {
  FarState& f = Fs(w);
  const float s = weatherBreeze(w);
  // The flag: the scene's breeze, the flutter, in his hand or planted.
  if (Glyph* g = G(w, f.g_flag)) {
    f.flutter = wrap1(f.flutter + (1.2f + 2.5f * s) * dt);
    g->k = s;
    g->t = f.flutter;
    if (f.flag_held) {
      const Vec2 hd = hikerHandPos(w.hiker, 1);
      g->pos = Vec2{hd.x + 2.f * (float)w.hiker.facing, hd.y + 7.f};
      g->base_y = (int16_t)(floorf(w.hiker.feet.y) + 1.f);
    }
  }
  // The targets.
  for (int i = 0; i < f.n; ++i) {
    FarTarget& t = f.tg[i];
    if (t.sw_t >= 0.f && t.sw_amp > 0.f) t.sw_t += dt;
  }
  if (f.drop_t >= 0.f) {
    f.drop_t += dt;
    if (f.drop_t >= kDropS) f.drop_t = -1.f;   // (the tin is gone into the boughs)
  }
  if (f.glint_t > 0.f) {
    f.glint_t -= dt;
    if (f.glint_t <= 0.f) {
      f.glint_t = 0.f;
      glyphDrop(w, f.g_glint);
    }
  }
  for (int i = 0; i < f.n; ++i) targetPose(w, i);
  // A dust puff: it grows and drifts downwind (toward -x).
  if (f.puff_t >= 0.f) {
    f.puff_t += dt;
    Glyph* g = G(w, f.g_puff);
    if (!g || f.puff_t >= kPuffS) {
      f.puff_t = -1.f;
      glyphDrop(w, f.g_puff);
    } else {
      const float u = f.puff_t / kPuffS;
      g->slot = SMOKE;
      g->pos = Vec2{f.puff_at.x - 8.f * s * f.puff_t, f.puff_at.y - 1.5f * u};
      g->scale = lerp(kPuffR0, kPuffR1, easeOutCubic(u)) * (u < 0.7f ? 1.f : 1.f - 0.6f * (u - 0.7f) / 0.3f);
    }
  }
  // The ring coming back from a gong.
  if (f.arcs_t >= 0.f) {
    f.arcs_t += dt;
    Glyph* g = G(w, f.g_arcs);
    if (!g || f.arcs_t >= kArcsS) {
      f.arcs_t = -1.f;
      glyphDrop(w, f.g_arcs);
    } else {
      const FarTarget& t = f.tg[f.target < f.n ? f.target : 0];
      arcsPose(*g, aimOf(t), M(w).p[0], f.arcs_t / kArcsS, 5.f, 11.f, kArcsTravel);
    }
  }
  // The first report's echo, off the middle of the far ridge.
  if (f.echo_t >= 0.f) {
    f.echo_t += dt;
    const float u = (f.echo_t - kEchoAt) / kEchoS;
    if (u >= 1.f) {
      f.echo_t = -1.f;
      glyphDrop(w, f.g_echo);
    } else if (u >= 0.f) {
      if (!f.g_echo) f.g_echo = glyphAdd(w, GL_ARCS, f.echo_at, (int)floorf(f.echo_at.y));
      if (Glyph* g = G(w, f.g_echo)) arcsPose(*g, f.echo_at, M(w).p[0], u, 8.f, 15.f, kArcsTravel);
    }
  }
  // The far birds: out and up, smaller, flapping, gone.
  if (f.bird_t >= 0.f) {
    f.bird_t += dt;
    const bool over = f.bird_t >= kBirdsS;
    for (int k = 0; k < 3; ++k) {
      Glyph* g = G(w, f.g_bird[k]);
      if (!g) continue;
      if (over) {
        glyphDrop(w, f.g_bird[k]);
        continue;
      }
      const float u = f.bird_t / kBirdsS;
      g->pos = Vec2{g->q.x + (float)g->facing * 26.f * f.bird_t, g->q.y - 16.f * f.bird_t};
      g->scale = lerp(0.5f, 0.3f, u);
      g->t = wrap1(0.25f * (float)k + 3.5f * f.bird_t);
    }
    if (over) f.bird_t = -1.f;
  }
  // C's late shot: heard when the ring is back (F14: from the clock, whether or not the arcs glyph exists).
  if (f.late_t >= 0.f) {
    f.late_t += dt;
    const FarTarget& t = f.tg[f.target < f.n ? f.target : 0];
    if (f.late_t >= t.flight + kHeardAfter) {
      M(w).cue = (uint16_t)(M(w).cue | kHeard);
      f.late_t = -1.f;
    }
  }
}

// The hold's breath: one slow breath out to the bottom of the exhale over the first 70 % of the hold, then nothing
// moves at all (the phase stands) until the shot is away and watched. He breathes again at doShotEnd.
void breathTick(World& w) {
  FarState& f = Fs(w);
  const PlinkState& p = P(w);
  Hiker& h = w.hiker;
  if (p.phase != PP_HOLD) return;
  if (!f.in_hold) {
    f.in_hold = 1;
    f.breath0 = h.breath_phase;
    f.breath1 = floorf(f.breath0 + 0.6f - 0.75f) + 1.75f;   // the next exhale's bottom at least 0.6 of a breath on
    h.breath_rate = 0.f;
  }
  const float u = clamp01(p.t / maxf(0.7f * p.hold_len, 0.2f));
  h.breath_phase = wrap1(lerp(f.breath0, f.breath1, easeInOut(u)));
}

void initClocks(FarState& f) {
  f.puff_t = f.arcs_t = f.echo_t = f.bird_t = f.late_t = f.drop_t = -1.f;
  for (FarTarget& t : f.tg) t.sw_t = -1.f;
}

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------------

uint8_t longshotPlanVar(uint32_t r_var) { return (uint8_t)((r_var >> 13) % 3u); }

// REVIEW_FIXES F13: the script plus about 7 s of walking stillness (measured by test_longshot over many seeds).
int longshotSeconds(uint8_t var) {
  switch (var & 3u) {
    case 0: return 116;
    case 1: return 129;
    default: return 131;
  }
}

const Step* longshotScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  if (var & kVarFog) {
    n = (uint8_t)(sizeof(kFogNoShow) / sizeof(kFogNoShow[0]));
    return kFogNoShow;
  }
  n = (uint8_t)(sizeof(kLongshot) / sizeof(kLongshot[0]));
  return kLongshot;
}

void longshotMajorBegin(World& w) {
  MajorState& m = M(w);
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  // A rain, snow or storm the plan could not know of: he only looks at the sky (the beat keeps its planned
  // seconds, REVIEW_FIXES F27). A fog: he cannot see his targets, so he looks, shakes his head and sits.
  if (dayWet(w)) {
    m.var = (uint8_t)(m.var | DAY_NOSHOW);
    return;
  }
  if (w.weather.kind == WX_FOG) {
    m.var = (uint8_t)(m.var | kVarFog);
    return;
  }
  // The wind decides B with no draw: B only when it blows (the ribbon is never limp while the story says wind),
  // and in a windy loop A becomes B (the plinking turns C into A the same way).
  const bool windy = w.weather.kind == WX_WIND || w.weather.extra == WXE_GUST;
  if ((m.var & 3u) == 1u && !windy) m.var = (uint8_t)(m.var & ~3u);
  // (The plan also turned A into B in a windy loop: not built, because the beat's seconds come from the planned
  // byte before this hook runs, and B is 13 s longer than A: it would overrun. A in the wind is A with the ribbon
  // streaming.)
  w.day.kind = DAY_PLINK;
  const Vec2 F = w.fire.pos;
  const float fpx = F.x + kFpDx;
  m.p[0] = Vec2{fpx, scriptGroundAt(w, fpx, F.y + kFpDy)};
  m.p[1] = Vec2{fpx + kBagDx, scriptGroundAt(w, fpx + kBagDx, m.p[0].y + 1.f)};
  m.p[2] = Vec2{fpx + kFlagDx, scriptGroundAt(w, fpx + kFlagDx, m.p[0].y + 2.f)};
  m.p[3] = m.p[0];
  DayPlink& d = D(w);
  // The module needs a branch and the pairs' places; the long shot uses neither (no cans are ever set out).
  const float x = (float)w.land.camp_x;
  const float px[PLINK_PAIRS] = {x - 40.f, x + 6.f, x + 44.f}, py[PLINK_PAIRS] = {351.f, 352.f, 351.f};
  plinkInit(d.p, Vec2{x, (float)LS_BRANCH_ROOT_Y}, Vec2{x - (float)LS_BRANCH_LEN, (float)LS_BRANCH_TIP_Y}, 1, px, py);
  plinkScope(d.p, true);
  plinkSackBig(d.p, true);
  plinkStay(d.p, true);
  d.rng.seed(((uint64_t)(uint32_t)(F.x * 16.f) << 22) ^ (uint64_t)(uint32_t)(F.y * 16.f) ^ 0x4C0A6000ull);
  FarState& f = d.far;
  f.n = (uint8_t)longshotTargets(w.land, f.tg);
  initClocks(f);
  // His standing range: the targets are there all along, picked out one by one at the glassing (kept glyphs:
  // they stay out on the hills, still, for the rest of the loop).
  for (int i = 0; i < f.n; ++i) {
    Glyph rest;
    longshotTargetGlyph(f.tg[i], rest);
    f.tg[i].glyph = glyphAdd(w, rest.kind, rest.pos, rest.base_y);
    if (Glyph* g = G(w, f.tg[i].glyph)) {
      *g = rest;
      g->scale = 0.f;
      g->keep = true;
    }
  }
  float x0, x1;
  longshotWindow(w.land, x0, x1);
  const float ex = 0.5f * (x0 + x1);
  f.echo_at = Vec2{ex, landscapeCrestAt(w.land, 0, ex) + 4.f};
}

void longshotMajorTick(World& w, float dt) {
  if (!live(w)) return;
  FarState& f = Fs(w);
  const uint8_t ev = plinkTakeEvents(P(w));
  if (ev & PEV_FIRST) {
    plinkStartle(w);   // two birds out of the camp pine, a squirrel off, the rest scattered
    f.echo_t = 0.f;
  }
  if ((ev & PEV_SHOT) && f.result == FR_LATE) {   // C's last: he is sure he pulled it
    M(w).cue = (uint16_t)(M(w).cue | kLanded);
    f.late_t = 0.f;
  }
  if (ev & PEV_FAR) {
    farResult(w);
    if (f.result != FR_LATE) M(w).cue = (uint16_t)(M(w).cue | kLanded);
  }
  breathTick(w);
  farTick(w, dt);
}

void longshotMajorEnd(World& w) {
  w.hiker.breath_rate = w.hiker.breath_scale = 1.f;
  // An overrun (the beat ended mid-play): a nod, the fist or the dial must not stay on him into the next beat. A
  // finished script has already rested these (the C3 review, 2026-11-19), so this changes nothing then.
  if (w.fx.arm_w[0] != 0.f || w.fx.arm_w[1] != 0.f || w.fx.lean != 0.f || w.fx.head != 0.f || w.fx.lift != 0.f) scriptFxRest(w);
  if (w.day.kind == DAY_PLINK && w.major.id == MJ_LONGSHOT) {
    // The steel stays out there, still (kept glyphs); a tin that fell is gone; everything else ends with the major.
    FarState& f = Fs(w);
    for (int i = 0; i < f.n; ++i) {
      FarTarget& t = f.tg[i];
      Glyph* g = G(w, t.glyph);
      if (!g) continue;
      Glyph rest;
      longshotTargetGlyph(t, rest);
      const bool fallen = (t.shown & 2) != 0, shown = (t.shown & 1) != 0;
      *g = rest;
      g->keep = shown && !fallen;
      if (!g->keep) g->kind = GL_NONE;
    }
  }
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where it was)
}

}  // namespace ss
