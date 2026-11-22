// Reeds and cattails along the stream. See reeds.h.
//
// OWNERSHIP: step B1 (the plants) filled in the sites, the geometry, the wind, the seasons, the snow, the
// actor, the sedge fringe, push, bend, take, blast and the brush. Step C1 adds the pick, the stem and the
// small life (the functions still marked "C1" below are the foundation's stubs).
//
// How a clump moves, in one place (Mike, 2026-11-18: natural, unhurried, nothing in step):
//   - the leaves take the wind at the clump as it is (ambientWindAt, the grass's own field), so they lead;
//   - the heads ride the same wind through a damped spring (Reeds::push, push_v): heavier, they answer a
//     moment later, smooth away the grass's quick flutter and swing back once after a gust. A body brushing
//     through, a push or a blast kicks the same spring, so it rings down the same way;
//   - each stalk reads its own fixed blend of the two, scaled by the square root of its height (F17: so a
//     tall reed does not swing past its dirty box) and softly limited to kTipSoft px;
//   - a held stalk (reedsBend) follows its holder and, let go, rings back to the wind on its own clock.
// Everything the draw reads is quantized in Reeds (sway_q in quarter units, holds in half px, gone bits), so
// the tracker marks a clump only when one of those changed (REVIEW_FIXES F10).
#include "reeds.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../gfx/raster.h"
#include "ambient.h"
#include "landscape.h"
#include "script.h"
#include "stage22_switch.h"
#include "world.h"

namespace ss {

namespace {

const Rect kNone{0, 0, 0, 0};
const Rect kFrame{0, 0, SS_WIDTH, SS_HEIGHT};

inline float fl(int v) { return (float)v; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }
inline int floorI(float v) { return (int)floorf(v); }
inline int ceilI(float v) { return (int)ceilf(v); }

// ---- tuning (PROPOSED: Mike judges the look; Mike, 2026-11-18) ----------------------------------------

constexpr uint8_t kPresent = 1, kCampBank = 2;
constexpr float kReedOverCat = 1.12f;     // a reed stands this much taller than the clump's tallest cattail
constexpr float kLeafOverCat = 0.88f;     // the leaves' tallest blade, of the tallest cattail
constexpr float kTipSoft = 12.f;          // the soft limit on any tip's travel, px (F17: inside the box's pad)
constexpr float kHoldSide = 12.f, kHoldUp = 6.f, kHoldDown = 8.f;   // how far a holder may take a tip, px
constexpr float kPlume = 9.f;             // a reed's plume, px (times the clump's plume scale, up to 1.3)
constexpr float kPadX = kTipSoft + 0.95f * kPlume * 1.3f + 2.1f + 2.f;   // tip travel, plume, its width, fringe
constexpr float kPopMax = 1.12f;          // buildPlantScale's overshoot, rounded up (easeOutBack)
constexpr float kFarShare = 0.7f;         // RS_FAR is there in about this share of loops
// The heads' spring: 1.2 Hz with a damping ratio of 0.3. It follows the wind with a lag of about a fifth
// of a second and rings three or four times after a brush (about 1.5 s to fade).
constexpr float kSpringW = 6.2831853f * 1.2f * SS_TICK_DT;
constexpr float kSpringK = kSpringW * kSpringW;
constexpr float kSpringD = 2.f * 0.3f * kSpringW;
constexpr float kLeafFollow = 0.35f;      // how much of the heads' swing (and of a brush) the leaves share
constexpr float kBrushTipPxPerS = 40.f;   // a body brushing through sets the heads moving at this tip speed
constexpr int kReleaseTicks = 40;         // a stalk let go rings back to the wind over 1.6 s
constexpr uint8_t kHeld = 255;            // ReedHold::ttl when refreshed this tick; 254 still held; 253 let go

// The stalks of a clump. Cattails first, then reeds (reeds.h). Offsets in clump scale units from its base x.
const float kCatOff[3] = {-5.f, 3.f, 8.5f};
constexpr float kHead0 = 0.70f, kHead1 = 0.92f;   // where a cattail's head starts and ends along its stalk
const float kCatH[3] = {1.f, 0.86f, 0.74f};
const float kCatLean[3] = {-0.05f, 0.08f, 0.14f};   // standing lean, as dx over height
const float kCatBlend[3] = {0.9f, 0.72f, 1.f};      // 0 the leaves' wind .. 1 the heads' spring
const float kReedOff[2] = {-9.f, 11.f};
const float kReedLean[2] = {-0.08f, 0.1f};
const float kReedBlend[2] = {0.3f, 0.45f};          // reeds are light: mostly the quick wind, a little lag
constexpr float kCatStiff = 0.55f, kReedStiff = 1.0f, kLeafStiff = 1.0f;

// ---- small helpers ------------------------------------------------------------------------------------

uint32_t mix32(uint32_t a, uint32_t b) {
  uint32_t x = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
  x ^= x >> 15;
  x *= 0x2C1B3C6Du;
  x ^= x >> 12;
  x *= 0x297A2D39u;
  x ^= x >> 15;
  return x;
}
float h01(uint32_t a, uint32_t b) { return (float)(mix32(a, b) >> 8) * (1.f / 16777216.f); }

inline bool camp(const World& w) { return !spineSetPiece(w.plan.family); }
inline bool present(const ReedSite& S) { return (S.flags & kPresent) != 0; }
inline uint32_t siteSeed(const ReedSite& S, int c) {
  return mix32((uint32_t)((int)S.x * 131 + (int)S.y), (uint32_t)c + 0x5EEDu);
}
inline float scaleOf(const ReedSite& S) { return clampf(fl(S.h_cat) / 28.f, 0.75f, 1.7f); }

// A soft limit: about d for small d, never past m.
inline float soft(float d, float m) { return d / sqrtf(1.f + (d / m) * (d / m)); }

// The waterline at x: the camp side (top, toward the camp) or the near side (bot), as the water is drawn.
bool bankY(const Landscape& ls, float x, bool near_bank, float& y) {
  float cy, half;
  if (!landscapeStreamSpan(ls, x, cy, half)) return false;
  y = near_bank ? cy + half : cy - half;
  return true;
}

// ---- the seasons (Inputs::month, latched at the loop's first tick) -------------------------------------

enum HeadKind : uint8_t { HK_FULL = 0, HK_SLIM, HK_RAGGED };

struct Look {
  float straw;     // leaves and stems toward straw, 0..1
  float fluff;     // the heads bursting, 0..1 (October, November)
  uint8_t head;    // HeadKind
  uint8_t bent;    // blades bent over in each fan
  bool plume;      // the reeds carry plumes
  bool spring;     // new growth: shorter, fresh green, no heads but last year's straw stalk
};

Look lookFor(uint8_t month) {
  Look L{0.f, 0.f, HK_FULL, 0, true, false};
  switch (month) {
    case 3: case 4: case 5: L.spring = true; L.plume = false; break;
    case 6: L.head = HK_SLIM; break;
    case 9: L.straw = 0.4f; break;
    case 10: L.straw = 0.7f; L.fluff = 0.55f; L.bent = 1; break;
    case 11: L.straw = 0.9f; L.fluff = 1.f; L.bent = 1; break;
    case 12: case 1: case 2: L.straw = 1.f; L.head = HK_RAGGED; L.bent = 2; L.plume = false; break;
    default: break;   // 0 (no clock), 7, 8: late summer
  }
  return L;
}

struct Cols { Pixel leaf, back, stem, straw, head, old_head, plume, fluff, cap; };

// Mixes of existing slots at draw time, so they follow the mood, the night rule and the weather (all 32
// slots are taken: no new slot, no palette rebuild).
Cols colsFor(const Palette& pal, const Look& L, bool warm, bool frost) {
  const Pixel summer = mixRGB(pal[GRASS], pal[FOLIAGE_DARK], 0.45f);   // deeper than the grass tufts
  const Pixel straw = mixRGB(mixRGB(pal[GRASS], pal[TRUNK], 0.55f), pal[CLOUD_LIT], 0.25f);
  const Pixel fresh = mixRGB(pal[GRASS], pal[WATER_LIGHT], 0.2f);
  Cols c;
  c.straw = straw;
  c.leaf = L.spring ? mixRGB(summer, fresh, 0.55f) : mixRGB(summer, straw, L.straw);
  c.back = mixRGB(c.leaf, pal[ACTOR], 0.3f);
  const Pixel stem = mixRGB(pal[GRASS], pal[FOLIAGE], 0.3f);
  c.stem = L.spring ? fresh : mixRGB(stem, straw, L.straw);
  Pixel head = mixRGB(pal[TRUNK], pal[ACTOR], 0.35f);   // dark brown: reads on water, ground and at night
  if (L.head == HK_SLIM) head = mixRGB(head, pal[GRASS], 0.35f);   // June: slim and green-brown
  if (L.head == HK_RAGGED) head = mixRGB(head, straw, 0.45f);      // winter: ragged and pale
  c.head = head;
  c.old_head = mixRGB(mixRGB(pal[TRUNK], pal[ACTOR], 0.35f), straw, 0.5f);   // last year's, in spring
  // A reed's plume: a soft buff, so it reads against the water and the grass alike.
  c.plume = mixRGB(mixRGB(pal[CLOUD_LIT], pal[TRUNK], 0.45f), straw, 0.5f * L.straw);
  c.fluff = mixRGB(pal[TRUNK], pal[CLOUD_LIT], 0.6f);
  c.cap = pal[WATER_LIGHT];   // the tent's snow colour: pale, never white
  if (frost) {
    c.head = mixRGB(c.head, pal[WATER_LIGHT], 0.25f);
    c.old_head = mixRGB(c.old_head, pal[WATER_LIGHT], 0.25f);
    c.leaf = mixRGB(c.leaf, pal[WATER_LIGHT], 0.12f);
    c.back = mixRGB(c.back, pal[WATER_LIGHT], 0.12f);
  }
  if (warm) {   // the fire's light on the clump behind his seat
    Pixel* all[9] = {&c.leaf, &c.back, &c.stem, &c.straw, &c.head, &c.old_head, &c.plume, &c.fluff, &c.cap};
    for (Pixel* p : all) *p = mixRGB(*p, pal[FIRE], 0.18f);
  }
  return c;
}

inline bool frosty(const World& w) { return w.weather.kind == WX_CHILL || w.weather.kind == WX_SNOW; }
inline bool warmOn(const World& w, int c) { return c == RS_FIRESIDE && w.fire.intensity > 0.3f; }

// Snow on the heads in quarters (from the value that whitens the tent), 0 under 0.15.
uint8_t snowQ(const World& w) {
  const float s = w.weather.snow_roof;
  if (s < 0.15f) return 0;
  const int q = (int)(s * 4.f + 0.5f);
  return (uint8_t)(q < 1 ? 1 : (q > 4 ? 4 : q));
}

// ---- the stalks ---------------------------------------------------------------------------------------

struct StalkDef {
  float fx;        // foot x
  float h;         // height, px
  float lean;      // standing lean at the tip, px
  float blend;     // 0 the leaves' wind .. 1 the heads' spring
  float stiff;     // bend for the wind, times sqrt(h / 18)
  float w0, w1;    // stem width at the foot and at the head
  float hw;        // a cattail's head width; a reed's plume scale
  bool cat;
  bool old;        // spring: last year's straw stalk with a ragged head
  bool headless;   // spring: new growth
};

bool stalkDef(const ReedSite& S, int c, int k, const Look& L, StalkDef& d) {
  if (k < 0 || k >= S.n_cat + S.n_reed) return false;
  const uint32_t seed = siteSeed(S, c);
  const float s = scaleOf(S);
  const float sw = clampf(s, 0.8f, 1.4f);
  const float jit = 1.f + 0.08f * (h01(seed, 40u + (uint32_t)k) - 0.5f);
  d = StalkDef{};
  if (k < S.n_cat) {
    d.cat = true;
    d.fx = fl(S.x) + kCatOff[k] * s;
    d.h = fl(S.h_cat) * kCatH[k] * jit;
    d.lean = kCatLean[k] * d.h;
    d.blend = kCatBlend[k];
    d.stiff = kCatStiff;
    d.w0 = 1.6f * sw;
    d.w1 = 1.1f * sw;
    d.hw = (L.head == HK_SLIM ? 2.7f : 3.9f) * s;
    if (L.head == HK_RAGGED) d.hw *= 0.75f;
    if (L.spring) {
      if (k == 0) {   // last year's stalk stands on, straw with a ragged pale head
        d.old = true;
        d.hw = 3.9f * s * 0.75f;
      } else {        // the new growth: 80 % and 60 % of full height, no heads yet
        d.headless = true;
        d.h *= k == 1 ? 0.8f : 0.6f;
        d.lean *= k == 1 ? 0.8f : 0.6f;
      }
    }
    return true;
  }
  const int r = k - S.n_cat;
  d.cat = false;
  if (c == RS_FIRESIDE) {
    // REVIEW_FIXES F24: the pick reed stands at the clump's own x, cut to reach camp.sit.y - 4, so its tip is
    // 14 px behind his hips when he sits facing the fire (camp.sit.x = F.x + 40, the clump at F.x + 54).
    d.fx = fl(S.x) + (r == 0 ? 0.f : 11.f * s);
    d.h = r == 0 ? fl(S.h_reed) : 0.88f * fl(S.h_reed) * jit;
    d.lean = r == 0 ? 0.f : 0.1f * d.h;
  } else {
    d.fx = fl(S.x) + kReedOff[r & 1] * s;
    d.h = fl(S.h_reed) * jit;
    d.lean = kReedLean[r & 1] * d.h;
  }
  if (L.spring) {
    d.h *= 0.8f;
    d.lean *= 0.8f;
  }
  d.blend = kReedBlend[r & 1];
  d.stiff = kReedStiff;
  d.w0 = 1.4f * sw;
  d.w1 = 1.0f * sw;
  d.hw = clampf(s, 0.8f, 1.3f);
  return true;
}

// A point on a stalk's centre line, u 0 the foot .. 1 the tip, with its tip `dx` px aside and `dy` px down.
// It bends like a rod, not a hinge (the plan's stations: 0.30, 0.72 and 1.0 of the offset at 50, 78 and 100 %),
// and a tip carried aside comes down a little (the stalk keeps its length).
Vec2 stalkPt(const StalkDef& d, float by, float dx, float dy, float u) {
  const float g = u * u * (1.6f - 0.6f * u);
  const float drop = (0.4f * dx * dx / maxf(d.h, 8.f) + dy) * g;
  return Vec2{d.fx + d.lean * u + dx * g, by - d.h * u + drop};
}

// A cattail whose head was bitten or picked off: the lower 60 % of the same stalk, leaning and swaying in
// proportion, so the stub stands where the whole stalk stood. The draw and reedsStalk both use this, so the
// tip a caller is told about is the top of the stub on screen.
constexpr float kCatStub = 0.6f;
inline StalkDef catStub(const StalkDef& d) {
  StalkDef t = d;
  t.h *= kCatStub;
  t.lean *= kCatStub;
  t.headless = true;
  return t;
}

// Built up from the base with the plants (buildPlantScale): every point scaled toward the clump's root.
inline Vec2 popAt(Vec2 p, float bx, float by, float hs) { return Vec2{bx + (p.x - bx) * hs, by + (p.y - by) * hs}; }

// Draw-time culling that cannot change a pixel: the rasterizer drops every edge that lies wholly above or
// below the clip's rows (raster.cpp addEdge: yb <= cy0 or ya >= cy1), so a shape whose points all lie on one
// side of the strip writes nothing, and skipping the call is exact. A capsule's outline stays within its
// radius times the area-match scale (under 1.06) of its two ends: 1.1 is a safe pad.
inline bool rowsMiss(const Vec2* p, int n, const Rect& clip) {
  float y0 = p[0].y, y1 = p[0].y;
  for (int i = 1; i < n; ++i) {
    y0 = minf(y0, p[i].y);
    y1 = maxf(y1, p[i].y);
  }
  return y1 <= fl(clip.y0) || y0 >= fl(clip.y1);
}
inline bool capsuleMiss(Vec2 a, Vec2 b, float width, const Rect& clip) {
  const float pad = 0.55f * width + 0.01f;
  return maxf(a.y, b.y) + pad <= fl(clip.y0) || minf(a.y, b.y) - pad >= fl(clip.y1);
}
inline void polyIn(Surface& s, const Vec2* p, int n, Pixel c, const Rect& clip) {
  if (!rowsMiss(p, n, clip)) fillPolygon(s, p, n, c, clip);
}
inline void capsuleIn(Surface& s, Vec2 a, Vec2 b, float width, Pixel c, const Rect& clip) {
  if (!capsuleMiss(a, b, width, clip)) strokeCapsule(s, a, b, width, c, clip);
}

// The hold on stalk (c, k), if any: its offset (px) and how much of it shows (1 held, a ring while let go).
bool holdOf(const Reeds& R, int c, int k, float& hdx, float& hdy, float& ring) {
  for (const ReedHold& h : R.hold) {
    if (h.ttl == 0 || h.clump != c || h.stalk != k) continue;
    hdx = 0.5f * fl(h.dx);
    hdy = 0.5f * fl(h.dy);
    if (h.ttl >= kHeld - 1) {
      ring = 1.f;
    } else {
      const float a = fl(kHeld - 2 - h.ttl);   // ticks since it was let go
      const float e = 1.f - a / fl(kReleaseTicks);
      ring = cosT(a * 1.6f * SS_TICK_DT) * e * e;
    }
    return true;
  }
  return false;
}

// Where stalk k's tip is carried now (px aside, px down), from the quantized wind and any hold.
void stalkOffset(const Reeds& R, int c, int k, const StalkDef& d, float& dx, float& dy) {
  const float s0 = 0.25f * fl(R.sway_q[c][0]), s1 = 0.25f * fl(R.sway_q[c][1]);
  const float wind = soft(lerp(s0, s1, d.blend) * sqrtf(d.h / 18.f) * d.stiff, kTipSoft);
  dx = wind;
  dy = 0.f;
  float hdx, hdy, ring;
  if (holdOf(R, c, k, hdx, hdy, ring)) {
    dx = wind + (hdx - wind) * ring;
    dy = hdy * ring;
  }
  dx = clampf(dx, -kHoldSide, kHoldSide);
  dy = clampf(dy, -kHoldUp, kHoldDown);
}

// ---- the leaves --------------------------------------------------------------------------------------

struct Blade { float fx, h, lean, arch; };

Blade bladeOf(const ReedSite& S, uint32_t seed, int j, int n, float hl, float s) {
  const float cpos = fl(j) - 0.5f * fl(n - 1);
  const uint32_t key = (uint32_t)(j + 16 * n);
  Blade b;
  b.fx = fl(S.x) + cpos * 2.6f * s;
  b.h = hl * (0.62f + 0.38f * h01(seed, 100u + key));
  b.lean = (cpos / 2.5f * 0.42f + 0.12f * (h01(seed, 200u + key) - 0.5f)) * b.h;
  b.arch = clamp01((b.h / hl - 0.75f) * 4.f);   // only the tallest blades droop
  return b;
}

inline bool bladeBent(int j, int n, int bent) { return (bent >= 1 && j == 0) || (bent >= 2 && j == n - 1); }

// One leaf fan as one polygon: blades with their valleys at 22 % of the height, so the foot reads as one
// tussock and the tops as blades. Each blade arcs (its middle carries less of the lean than its tip) and the
// wind moves its top most. Returns the point count (6 n + 1).
int fanPts(const ReedSite& S, uint32_t seed, int n, float hl, float spread, float sway, int bent, float hs, Vec2* p) {
  const float s = scaleOf(S) * spread;
  const float by = fl(S.y);
  const float w55 = 1.25f * s, w82 = 0.7f * s;
  int k = 0;
  const Blade first = bladeOf(S, seed, 0, n, hl, s);
  p[k++] = Vec2{first.fx - 1.4f * s, by + 1.f};
  for (int j = 0; j < n; ++j) {
    const Blade b = bladeOf(S, seed, j, n, hl, s);
    const float sw = soft(sway * sqrtf(b.h / 18.f) * kLeafStiff, kTipSoft);
    const Vec2 c55{b.fx + b.lean * 0.449f + sw * 0.30f, by - 0.55f * b.h};
    if (!bladeBent(j, n, bent)) {   // a strap that arches out, the tallest drooping a little at the tip
      const float arch = b.arch;
      const Vec2 c82{b.fx + b.lean * 0.767f + sw * 0.67f, by - 0.82f * b.h};
      const Vec2 tip{b.fx + b.lean * (1.f + 0.3f * arch) + sw, by - b.h * (1.f - 0.1f * arch) + 0.3f * sw * sw / b.h};
      p[k++] = Vec2{c55.x - w55, c55.y};
      p[k++] = Vec2{c82.x - w82, c82.y};
      p[k++] = tip;
      p[k++] = Vec2{c82.x + w82, c82.y};
      p[k++] = Vec2{c55.x + w55, c55.y};
    } else {   // folded over at 60 % of its height, the top hanging outward (autumn, winter)
      const float out = j == 0 ? -1.f : 1.f;
      const Vec2 knee{b.fx + b.lean * 0.6f + sw * 0.3f, by - 0.62f * b.h};
      const Vec2 tip{knee.x + out * 0.38f * b.h + sw * 0.5f, by - 0.36f * b.h};
      p[k++] = Vec2{c55.x - w55, c55.y};
      if (out < 0.f) {
        p[k++] = Vec2{knee.x - 0.2f * w55, knee.y + 1.4f * w55};
        p[k++] = tip;
        p[k++] = Vec2{knee.x + 0.4f * w55, knee.y - 0.9f * w55};
      } else {
        p[k++] = Vec2{knee.x - 0.4f * w55, knee.y - 0.9f * w55};
        p[k++] = tip;
        p[k++] = Vec2{knee.x + 0.2f * w55, knee.y + 1.4f * w55};
      }
      p[k++] = Vec2{c55.x + w55, c55.y};
    }
    if (j + 1 < n) {
      const Blade nb = bladeOf(S, seed, j + 1, n, hl, s);
      p[k++] = Vec2{0.5f * (b.fx + nb.fx) + 0.1f * (b.lean + nb.lean), by - 0.22f * hl};
    }
  }
  const Blade last = bladeOf(S, seed, n - 1, n, hl, s);
  p[k++] = Vec2{last.fx + 1.4f * s, by + 1.f};
  for (int i = 0; i < k; ++i) p[i] = popAt(p[i], fl(S.x), by, hs);
  return k;
}

inline float leafHeight(const ReedSite& S, const Look& L) {
  return fl(S.h_cat) * kLeafOverCat * (L.spring ? 0.75f : 1.f);
}
inline bool hasBackFan(int c) { return c == RS_FORE; }
constexpr uint32_t kBackSalt = 0xBAC0u;
constexpr float kBackTall = 1.05f, kBackSpread = 1.15f;   // RS_FORE's back fan: a little taller and wider

// ---- the box ------------------------------------------------------------------------------------------

// Everything the clump can draw at pop scale hs (1, or kPopMax while the build runs): the rest geometry of
// every blade and stalk, folded, then padded by the most any of it can travel (kTipSoft for the wind, the
// holds' limits, a plume hanging off a tip, the widest head) and the antialiasing fringe.
Rect clumpBox(const ReedSite& S, int c, float hs) {
  if (!present(S)) return kNone;
  const float s = scaleOf(S);
  const uint32_t seed = siteSeed(S, c);
  const Look L = lookFor(0);   // full growth: the tallest the clump ever stands
  float xl = 0.f, xr = 0.f, top = 0.f;
  auto fold = [&](float dx, float h) {
    xl = minf(xl, dx);
    xr = maxf(xr, dx);
    top = maxf(top, h);
  };
  const float hl = leafHeight(S, L);
  for (int fan = 0; fan < (hasBackFan(c) ? 2 : 1); ++fan) {
    const int n = fan ? 6 : 5;
    const uint32_t sd = fan ? seed ^ kBackSalt : seed;
    const float fh = fan ? hl * kBackTall : hl;
    const float bs = fan ? s * kBackSpread : s;
    for (int j = 0; j < n; ++j) {
      const Blade b = bladeOf(S, sd, j, n, fh, bs);
      const float o = b.fx - fl(S.x);
      fold(o - 1.6f * bs, b.h);
      fold(o + 1.6f * bs, b.h);
      fold(o + b.lean * 1.3f, b.h);
      fold(o + 0.6f * b.lean - 0.38f * b.h, b.h);   // bent over either way
      fold(o + 0.6f * b.lean + 0.38f * b.h, b.h);
    }
  }
  for (int k = 0; k < S.n_cat + S.n_reed; ++k) {
    StalkDef d;
    if (!stalkDef(S, c, k, L, d)) continue;
    const float o = d.fx - fl(S.x);
    fold(o, d.h);
    fold(o + d.lean, d.h);
  }
  const float m = maxf(hs, 1.f);
  const float pad = kPadX * m + 0.6f * 3.9f * s;   // and a head's half width with its autumn fluff
  return Rect{floorI(fl(S.x) + xl * hs - pad), floorI(fl(S.y) - top * hs - (kHoldUp + 2.f) * m),
              ceilI(fl(S.x) + xr * hs + pad), S.y + 3};
}

// ---- sites ---------------------------------------------------------------------------------------------

Rect rockBox(const LsRock& r) {
  float x0 = r.p[0].x, x1 = r.p[0].x, y0 = r.p[0].y, y1 = r.p[0].y;
  for (const Vec2& q : r.p) {
    x0 = minf(x0, q.x);
    x1 = maxf(x1, q.x);
    y0 = minf(y0, q.y);
    y1 = maxf(y1, q.y);
  }
  return Rect{floorI(x0), floorI(y0), ceilI(x1), ceilI(y1)};
}

// Fill in one site: heights from the tallest reed, shortened if its box would climb past `cap_y` (the
// strips it is meant to stay in). Absent when there is no room for a clump worth the name.
void makeSite(ReedSite& S, int c, float x, float y, float h_reed, uint8_t n_cat, uint8_t n_reed, bool camp_bank,
              int cap_y, bool exact_reed) {
  S = ReedSite{};
  S.x = (int16_t)floorI(x);
  S.y = (int16_t)floorI(y);
  S.n_cat = n_cat;
  S.n_reed = n_reed;
  S.flags = (uint8_t)(kPresent | (camp_bank ? kCampBank : 0));
  for (int tries = 0; tries < 4; ++tries) {
    if (h_reed < 15.f) {
      S.flags = 0;
      return;
    }
    S.h_reed = (uint8_t)floorI(h_reed + 0.5f);
    S.h_cat = (uint8_t)floorI(h_reed / (exact_reed ? 1.18f : kReedOverCat) + 0.5f);
    const Rect b = clumpBox(S, c, 1.f);
    if (b.y0 >= cap_y) return;
    if (exact_reed) {   // the fireside reed's height is his seat's: never cut it, lose the clump instead
      S.flags = 0;
      return;
    }
    h_reed -= fl(cap_y - b.y0);
  }
  S.flags = 0;
}

// Within `d` px of any landscape rock's outline box.
bool nearRock(const Landscape& ls, float x, float y, float d) {
  for (int i = 0; i < ls.n_rocks; ++i) {
    const Rect r = rockBox(ls.rocks[i]);
    if (x > fl(r.x0) - d && x < fl(r.x1) + d && y > fl(r.y0) - d && y < fl(r.y1) + d) return true;
  }
  return false;
}

// RS_UPSTREAM's rock rule: a rock whose x extent is within 16 px and whose foot is within 14 px of the
// waterline pushes the clump to its right (12 px clear, at most x 540), or failing that to its left (12 px
// clear, no further left than F.x + 254: the tai chi line ends at F.x + 245, INTEGRATION 3.1 point 2).
int rockInWay(const Landscape& ls, float x, float y) {
  for (int i = 0; i < ls.n_rocks; ++i) {
    const Rect r = rockBox(ls.rocks[i]);
    if (x > fl(r.x0) - 16.f && x < fl(r.x1) + 16.f && absf(fl(r.y1) - y) < 14.f) return i;
  }
  return -1;
}

void layoutSites(World& w) {
  Reeds& R = w.reeds;
  const Landscape& ls = w.land;
  const Vec2 F = w.fire.pos;
  float y;
  // RS_FORE: the foreground stand in the near shallows, rooted at the frame's bottom edge.
  {
    const float x = clampf(floorf(F.x - 22.f), 212.f, 244.f);
    if (bankY(ls, x, true, y)) makeSite(R.site[RS_FORE], RS_FORE, x, minf(y + 2.f, 488.f), 54.f, 3, 1, false, 416, false);
  }
  // RS_NEAR: the near bank, in front of the water, clear of the bather (F.x + 126) and of where the mushroom
  // picker crouches (Nx: INTEGRATION 3.1 point 2). Absent when it cannot clear both.
  {
    const float nx = minf(fl(ls.mush_x) - 44.f, 501.f);
    const float x = clampf(floorf(minf(F.x + 176.f, nx - 36.f)), 404.f, 440.f);
    if (nx - 36.f >= F.x + 166.f && bankY(ls, x, true, y))
      makeSite(R.site[RS_NEAR], RS_NEAR, x, y + 2.f, 38.f, 2, 1, false, 416, false);
  }
  // RS_UPSTREAM: the camp bank between the skip-stone spot and the boulder.
  {
    float x = clampf(floorf(F.x + 262.f), 500.f, 528.f);
    bool ok = bankY(ls, x, false, y);
    int ri = ok ? rockInWay(ls, x, y + 1.f) : -1;
    if (ri >= 0) {
      const Rect r = rockBox(ls.rocks[ri]);
      float xr = fl(r.x1) + 12.f, yr = 0.f;
      if (xr <= 540.f && bankY(ls, xr, false, yr) && rockInWay(ls, xr, yr + 1.f) < 0) {
        x = xr;
        y = yr;
      } else {
        const float xl = fl(r.x0) - 12.f;
        float yl = 0.f;
        ok = xl >= F.x + 254.f && bankY(ls, xl, false, yl) && rockInWay(ls, xl, yl + 1.f) < 0;
        x = xl;
        y = yl;
      }
    }
    if (ok) makeSite(R.site[RS_UPSTREAM], RS_UPSTREAM, floorf(x), y + 1.f, 33.f, 2, 2, true, 352, false);
  }
  // RS_FIRESIDE: just behind his seat by the fire; only when the bank there is 20 to 36 px below the seat
  // and no rock is within 12 px. Its pick reed reaches camp.sit.y - 4.
  {
    const float x = floorf(F.x + 54.f);
    const Vec2 sit = w.spine.camp.sit;
    if (bankY(ls, x, false, y)) {
      const float by = floorf(y + 1.f);
      const float drop = by - sit.y;
      if (drop >= 20.f && drop <= 36.f && !nearRock(ls, x, by, 12.f))
        makeSite(R.site[RS_FIRESIDE], RS_FIRESIDE, x, by, by - (floorf(sit.y) - 4.f), 1, 2, true, 352, true);
    }
  }
  // RS_FAR: the camp bank far right, where the frog and the stream's deer come in; in about 70 % of loops.
  {
    const float x = 770.f;
    if (h01((uint32_t)(int)ls.camp_x, (uint32_t)(int)ls.mush_x + 0x5A5Au) < kFarShare && bankY(ls, x, false, y))
      makeSite(R.site[RS_FAR], RS_FAR, x, y + 1.f, 25.f, 1, 1, true, 320, false);
  }
}

// ---- the sedge fringe (in the static cache) -----------------------------------------------------------
// One byte per spike cluster in Landscape::reed_fringe[0..14]: bits 0-6 x / 8 + 1, bit 7 the near bank.
// Byte 15 is the month + 1 the loop latched (0 unknown), so the cache draws the season's colour too.

constexpr int kFringeSlots = LS_REED_FRINGE - 1;
constexpr int kFringeMonth = LS_REED_FRINGE - 1;

bool clearOf(float x, const float* spots, int n, float d) {
  for (int i = 0; i < n; ++i)
    if (absf(x - spots[i]) < d) return false;
  return true;
}

void layoutFringe(World& w) {
  const Landscape& ls0 = w.land;
  const Reeds& R = w.reeds;
  const Vec2 F = w.fire.pos;
  const CampLayout& cl = w.spine.camp;
  uint8_t* fr = w.land.reed_fringe;
  for (int i = 0; i < LS_REED_FRINGE; ++i) fr[i] = 0;
  // Story spots on the camp bank (CATTAILS_PLAN 2.2): his seat, the fishing spot, the cool spot, the wade, the
  // listen seat, the cairn and its stones, the fetch's bank point, the skip spot; the clumps themselves.
  float spots[16];
  int ns = 0;
  spots[ns++] = cl.sit.x;
  spots[ns++] = F.x + 76.f;
  spots[ns++] = F.x + 95.f;
  spots[ns++] = F.x + 102.f;
  spots[ns++] = F.x + 112.f;
  spots[ns++] = cl.cairn.x;
  spots[ns++] = cl.cairn.x - 20.f;
  spots[ns++] = cl.cairn.x + 20.f;
  spots[ns++] = F.x + 163.f;
  spots[ns++] = F.x + 205.f;
  for (int c = RS_UPSTREAM; c <= RS_FAR; ++c)
    if (present(R.site[c])) spots[ns++] = fl(R.site[c].x);
  int n = 0;
  for (int x = 16; x < 744 && n < 10;) {
    const float fx = fl(x);
    float y;
    const bool ok = clearOf(fx, spots, ns, 18.f) && !(fx > 140.f && fx < 192.f) &&   // the plinking spot
                    !(fx > 580.f && fx < 740.f) &&                                    // the boulder's bank, the ford
                    bankY(ls0, fx, false, y) && y < 472.f;
    if (ok) fr[n++] = (uint8_t)(x / 8 + 1);
    x += 8 * (4 + (int)(3.f * h01((uint32_t)x, 0xF1u)));   // 32 to 48 px apart
  }
  // The near bank where it is in frame (right of about x 310), clear of the foreground stand, the near clump,
  // the bather, and the stretch left free for the mushroom piece (RS_NEAR.x + 24 to x 540).
  const float free0 = present(R.site[RS_NEAR]) ? fl(R.site[RS_NEAR].x) + 24.f : fl(ls0.mush_x) - 80.f;
  float near_spots[3];
  int nn = 0;
  near_spots[nn++] = F.x + 126.f;
  if (present(R.site[RS_NEAR])) near_spots[nn++] = fl(R.site[RS_NEAR].x);
  if (present(R.site[RS_FORE])) near_spots[nn++] = fl(R.site[RS_FORE].x);
  for (int x = 296; x < 536 && n < kFringeSlots;) {
    const float fx = fl(x);
    float y;
    const bool ok = clearOf(fx, near_spots, nn, 22.f) && !(fx > free0 && fx < 540.f) && bankY(ls0, fx, true, y) &&
                    y < 470.f;
    if (ok) fr[n++] = (uint8_t)((x / 8 + 1) | 0x80);
    x += 8 * (4 + (int)(3.f * h01((uint32_t)x, 0xF2u)));
  }
}

// A cluster's three spikes as one polygon: camp-bank ones 6 to 12 px, near-bank ones 4 to 7 (they stand up
// in front of the water, so they stay short of the glints' lanes).
int fringePts(uint8_t v, float x, float y, float hs, Vec2* p) {
  const bool near_bank = (v & 0x80) != 0;
  const float h0 = near_bank ? 4.f : 6.f, hr = near_bank ? 3.f : 6.f;
  const float ha = h0 + hr * h01(v, 1u), hb = h0 + hr * h01(v, 2u), hc = h0 + hr * h01(v, 3u);
  const float la = -1.5f - 1.5f * h01(v, 4u), lb = 1.2f * (h01(v, 5u) - 0.5f), lc = 1.5f + 1.5f * h01(v, 6u);
  p[0] = Vec2{x - 3.5f, y + 1.f};
  p[1] = Vec2{x - 2.2f + la * hs, y - ha * hs};
  p[2] = Vec2{x - 0.8f, y - 0.3f * ha * hs};
  p[3] = Vec2{x + lb * hs, y - hb * hs};
  p[4] = Vec2{x + 0.8f, y - 0.3f * hc * hs};
  p[5] = Vec2{x + 2.2f + lc * hs, y - hc * hs};
  p[6] = Vec2{x + 3.5f, y + 1.f};
  return 7;
}

// ---- drawing a clump ----------------------------------------------------------------------------------

void drawCattail(const StalkDef& d, const ReedSite& S, float dx, float dy, float hs, uint8_t snow, const Look& L,
                 const Cols& C, Surface& s, const Rect& clip) {
  const float bx = fl(S.x), by = fl(S.y);
  const Vec2 p50 = popAt(stalkPt(d, by, dx, dy, 0.5f), bx, by, hs);
  const Vec2 p78 = popAt(stalkPt(d, by, dx, dy, 0.78f), bx, by, hs);
  const Vec2 tip = popAt(stalkPt(d, by, dx, dy, 1.f), bx, by, hs);
  const float w0 = 0.5f * d.w0, w1 = 0.5f * d.w1, wm = 0.25f * (d.w0 + d.w1);
  const float fx = bx + (d.fx - bx) * hs;
  const Vec2 stem[7] = {{fx - w0, by + 1.f}, {p50.x - wm, p50.y}, {p78.x - w1, p78.y}, tip,
                        {p78.x + w1, p78.y}, {p50.x + wm, p50.y}, {fx + w0, by + 1.f}};
  polyIn(s, stem, 7, d.old ? C.straw : C.stem, clip);
  if (d.headless) return;
  // The head: a long brown cylinder from 70 to 92 % of the stalk, the bare spike above it.
  const Vec2 h0 = popAt(stalkPt(d, by, dx, dy, kHead0), bx, by, hs);
  const Vec2 h1 = popAt(stalkPt(d, by, dx, dy, kHead1), bx, by, hs);
  const float hw = d.hw * hs;
  // The head, its fluff and its snow cap lie within 0.8 hw of the stretch from 70 to 92 % (the cap is raised
  // 0.3 hw and is at most 0.93 hw wide); 2 px more for a stalk bent hard enough to bow that stretch. When
  // that misses the strip, none of them draws a pixel.
  if (capsuleMiss(h0, h1, hw * 1.6f + 4.f, clip)) return;
  capsuleIn(s, h0, h1, hw, d.old ? C.old_head : C.head, clip);
  if (L.fluff > 0.f && !d.old) {   // autumn: the head bursting, a pale patch on its upper half, downwind
    const Vec2 f0 = popAt(stalkPt(d, by, dx, dy, 0.8f), bx, by, hs);
    capsuleIn(s, Vec2{f0.x - 0.25f * hw, f0.y}, Vec2{h1.x - 0.3f * hw, h1.y}, hw * (0.55f + 0.45f * L.fluff), C.fluff, clip);
  }
  if (snow) {   // a cap along the top of the head, a quarter thicker for each quarter of snow
    const Vec2 c0 = popAt(stalkPt(d, by, dx, dy, 0.8f), bx, by, hs);
    const float up = 0.3f * hw;
    capsuleIn(s, Vec2{c0.x, c0.y - up}, Vec2{h1.x, h1.y - up}, hw * (0.45f + 0.12f * fl(snow)), C.cap, clip);
  }
}

void drawReed(const StalkDef& d, const ReedSite& S, bool taken, float dx, float dy, float hs, const Look& L,
              const Cols& C, Surface& s, const Rect& clip) {
  const float bx = fl(S.x), by = fl(S.y);
  const float fx = bx + (d.fx - bx) * hs;
  const float w0 = 0.5f * d.w0, w1 = 0.5f * d.w1;
  if (taken) {   // only the sheath: the lower 55 %, cut on a slant
    const Vec2 p30 = popAt(stalkPt(d, by, dx, dy, 0.3f), bx, by, hs);
    const Vec2 p55 = popAt(stalkPt(d, by, dx, dy, 0.55f), bx, by, hs);
    const Vec2 stub[6] = {{fx - w0, by + 1.f}, {p30.x - w0, p30.y}, {p55.x - w1, p55.y + 0.4f},
                          {p55.x + w1, p55.y - 1.f}, {p30.x + w0, p30.y}, {fx + w0, by + 1.f}};
    polyIn(s, stub, 6, C.stem, clip);
    return;
  }
  const Vec2 p55 = popAt(stalkPt(d, by, dx, dy, 0.55f), bx, by, hs);
  const Vec2 tip = popAt(stalkPt(d, by, dx, dy, 1.f), bx, by, hs);
  const Vec2 stem[5] = {{fx - w0, by + 1.f}, {p55.x - w1, p55.y}, tip, {p55.x + w1, p55.y}, {fx + w0, by + 1.f}};
  polyIn(s, stem, 5, C.stem, clip);
  if (!L.plume) return;
  // The plume hangs downwind off the tip, more sideways as the reed leans: a narrow, feathery teardrop.
  const float px = clampf(-0.55f + 0.08f * dx, -0.95f, 0.45f);
  const float py = sqrtf(1.f - px * px);
  const float len = kPlume * d.hw * hs;
  const Vec2 end{tip.x + px * len, tip.y + 0.75f * py * len};
  const Vec2 mid{tip.x + 0.45f * px * len, tip.y + 0.34f * py * len};
  const float nw = 1.6f * d.hw * hs;
  const Vec2 plume[4] = {tip, {mid.x - py * nw, mid.y + px * nw}, end, {mid.x + py * nw, mid.y - px * nw}};
  polyIn(s, plume, 4, C.plume, clip);
}

void drawClump(const World& w, int c, Surface& s, const Rect& clip, const Palette& pal) {
  const Reeds& R = w.reeds;
  const ReedSite& S = R.site[c];
  const float hs = buildPlantScale(w.build, fl(S.x), fl(S.y));
  if (hs <= 0.01f) return;
  const Look L = lookFor(R.month);
  const Cols C = colsFor(pal, L, warmOn(w, c), frosty(w));
  const uint32_t seed = siteSeed(S, c);
  const float s0 = 0.25f * fl(R.sway_q[c][0]), s1 = 0.25f * fl(R.sway_q[c][1]);
  const float hl = leafHeight(S, L);
  Vec2 p[6 * 6 + 2];
  if (hasBackFan(c)) {   // a darker fan behind, a little taller and wider: depth in the foreground stand
    const int n = fanPts(S, seed ^ kBackSalt, 6, hl * kBackTall, kBackSpread, lerp(s0, s1, 0.3f), L.bent, hs, p);
    polyIn(s, p, n, C.back, clip);
  }
  const uint8_t snow = snowQ(w);
  for (int k = S.n_cat + S.n_reed - 1; k >= 0; --k) {   // the reeds behind, the tallest cattail in front
    StalkDef d;
    if (!stalkDef(S, c, k, L, d)) continue;
    float dx, dy;
    stalkOffset(R, c, k, d, dx, dy);
    const bool taken = (S.gone >> k) & 1u;
    if (d.cat) {
      if (taken) {   // a headless stem: the upper 40 % gone
        drawCattail(catStub(d), S, dx * kCatStub, dy * kCatStub, hs, 0, L, C, s, clip);
      } else {
        drawCattail(d, S, dx, dy, hs, snow, L, C, s, clip);
      }
    } else {
      drawReed(d, S, taken, dx, dy, hs, L, C, s, clip);
    }
  }
  const int n = fanPts(S, seed, 5, hl, 1.f, s0, L.bent, hs, p);
  polyIn(s, p, n, C.leaf, clip);
}

// ---- the brush ----------------------------------------------------------------------------------------

inline float headGain(const ReedSite& S) { return maxf(0.3f, sqrtf(fl(S.h_cat) / 18.f) * kCatStiff); }

// Which way a body walking through the clump's footprint is going: +1, -1, or 0 for nobody. The hiker while
// he walks, every critter in the scene while it moves (the house dogs and the stream's deer are critters).
int brushDir(const World& w, const ReedSite& S) {
  const float half = maxf(14.f, 9.f * scaleOf(S));
  auto inFoot = [&](Vec2 f) { return absf(f.x - fl(S.x)) <= half && f.y >= fl(S.y) - 8.f && f.y <= fl(S.y) + 6.f; };
  const Hiker& h = w.hiker;
  if (w.fx.on && h.visible && h.walking && inFoot(h.feet)) return h.facing >= 0 ? 1 : -1;
  for (int i = 0; i < CRITTER_MAX; ++i) {
    if (!w.crit[i].in_use) continue;
    const Critter& cr = w.critters[i];
    if (!cr.visible || !cr.moving || !inFoot(cr.feet)) continue;
    return cr.move_to >= cr.feet.x ? 1 : -1;
  }
  return 0;
}

void kick(Reeds& R, int c, float tip_px_per_s) {
  const float v = fl(R.push_v[c]) / 1024.f + tip_px_per_s * SS_TICK_DT / headGain(R.site[c]);
  R.push_v[c] = (int16_t)floorI(clampf(v, -6.f, 6.f) * 1024.f + 0.5f);
}

inline int8_t q4(float v) {
  const int q = floorI(v * 4.f + 0.5f);
  return (int8_t)(q < -127 ? -127 : (q > 127 ? 127 : q));
}

// Step C1's parts, defined at the end of the file.
void c1Init(World& w);
void c1Ambient(World& w);
void c1Track(World& w, Renderer& r);

}  // namespace

// ---- the interface the other pieces use ----

bool reedsClump(const World& w, int clump, ReedClumpInfo& out) {
  out = ReedClumpInfo{};
  if (!camp(w) || clump < 0 || clump >= REED_CLUMPS) return false;
  const ReedSite& S = w.reeds.site[clump];
  if (!present(S)) return false;
  out.present = true;
  out.camp_bank = (S.flags & kCampBank) != 0;
  out.base = Vec2{fl(S.x), fl(S.y)};
  const float s = scaleOf(S);
  const uint32_t seed = siteSeed(S, clump);
  float half = 0.f;
  for (int j = 0; j < 5; ++j) {
    const Blade b = bladeOf(S, seed, j, 5, leafHeight(S, lookFor(0)), s);
    half = maxf(half, absf(b.fx - fl(S.x)) + 1.4f * s);
  }
  out.half_w = half;
  out.height = fl(S.n_reed ? S.h_reed : S.h_cat);
  out.n_cat = S.n_cat;
  out.n_reed = S.n_reed;
  return true;
}

bool reedsStalk(const World& w, int clump, int stalk, ReedStalkInfo& out) {
  out = ReedStalkInfo{};
  if (!camp(w) || clump < 0 || clump >= REED_CLUMPS) return false;
  const Reeds& R = w.reeds;
  const ReedSite& S = R.site[clump];
  StalkDef d;
  if (!present(S) || !stalkDef(S, clump, stalk, lookFor(R.month), d)) return false;
  const float hs = maxf(buildPlantScale(w.build, fl(S.x), fl(S.y)), 0.f);
  float dx, dy;
  stalkOffset(R, clump, stalk, d, dx, dy);
  const float bx = fl(S.x), by = fl(S.y);
  out.present = ((S.gone >> stalk) & 1u) == 0;
  out.cattail = d.cat;
  out.foot = Vec2{bx + (d.fx - bx) * hs, by};
  if (out.present) {
    out.tip = popAt(stalkPt(d, by, dx, dy, 1.f), bx, by, hs);
  } else if (d.cat) {   // the top of the headless stub, as drawCattail draws it
    out.tip = popAt(stalkPt(catStub(d), by, dx * kCatStub, dy * kCatStub, 1.f), bx, by, hs);
  } else {              // the cut top of a reed's sheath, as drawReed draws it
    out.tip = popAt(stalkPt(d, by, dx, dy, 0.55f), bx, by, hs);
  }
  out.head = d.cat ? popAt(stalkPt(d, by, dx, dy, 0.5f * (kHead0 + kHead1)), bx, by, hs) : out.tip;
  return true;
}

Vec2 reedsTip(const World& w, int clump, int stalk) {
  ReedStalkInfo s;
  return reedsStalk(w, clump, stalk, s) ? s.tip : Vec2{0.f, 0.f};
}

int reedsNearest(const World& w, Vec2 p, float max_d, bool camp_only) {
  if (!camp(w)) return -1;
  int best = -1;
  float bd = max_d * max_d;
  for (int c = 0; c < REED_CLUMPS; ++c) {
    const ReedSite& S = w.reeds.site[c];
    if (!present(S) || (camp_only && !(S.flags & kCampBank))) continue;
    const float dx = fl(S.x) - p.x, dy = fl(S.y) - p.y;
    const float d2 = dx * dx + dy * dy;
    if (d2 <= bd) {
      bd = d2;
      best = c;
    }
  }
  return best;
}

int reedsNearestStalk(const World& w, int clump, Vec2 p, bool reeds_only) {
  if (!camp(w) || clump < 0 || clump >= REED_CLUMPS) return -1;
  const ReedSite& S = w.reeds.site[clump];
  if (!present(S)) return -1;
  int best = -1;
  float bd = 3.0e38f;
  for (int k = 0; k < S.n_cat + S.n_reed; ++k) {
    ReedStalkInfo s;
    if (!reedsStalk(w, clump, k, s) || !s.present || (reeds_only && s.cattail)) continue;
    const float dx = s.tip.x - p.x, dy = s.tip.y - p.y;
    if (dx * dx + dy * dy < bd) {
      bd = dx * dx + dy * dy;
      best = k;
    }
  }
  return best;
}

bool reedsStandPoint(const World& w, int clump, int side, Vec2& feet) {
  ReedClumpInfo ci;
  if (!reedsClump(w, clump, ci) || !ci.camp_bank) return false;
  const float x = ci.base.x + (side < 0 ? -1.f : 1.f) * (ci.half_w + 8.f);
  float y;
  if (!bankY(w.land, x, false, y)) return false;
  feet = Vec2{x, minf(floorf(y) - 5.f, 412.f)};
  return true;
}

Rect reedsBounds(const World& w, int clump) {
  if (!camp(w) || clump < 0 || clump >= REED_CLUMPS) return kNone;
  const ReedSite& S = w.reeds.site[clump];
  if (!present(S)) return kNone;
  const float hs = buildPlantScale(w.build, fl(S.x), fl(S.y));
  if (hs <= 0.01f) return kNone;
  return clumpBox(S, clump, buildDone(w.build) ? 1.f : kPopMax);
}

int reedsPrimitives(const World& w, int clump) {
  if (reedsBounds(w, clump).empty()) return 0;
  const Reeds& R = w.reeds;
  const ReedSite& S = R.site[clump];
  const Look L = lookFor(R.month);
  const uint8_t snow = snowQ(w);
  int n = hasBackFan(clump) ? 2 : 1;
  for (int k = 0; k < S.n_cat + S.n_reed; ++k) {
    StalkDef d;
    if (!stalkDef(S, clump, k, L, d)) continue;
    const bool taken = (S.gone >> k) & 1u;
    n += 1;
    if (d.cat && !taken && !d.headless) n += 1 + (L.fluff > 0.f && !d.old ? 1 : 0) + (snow ? 1 : 0);
    if (!d.cat && !taken && L.plume) n += 1;
  }
  return n;
}

void reedsPush(World& w, int clump, float dir, float strength) {
  if (!camp(w) || clump < 0 || clump >= REED_CLUMPS || !present(w.reeds.site[clump])) return;
  kick(w.reeds, clump, (dir < 0.f ? -1.f : 1.f) * clamp01(strength) * 70.f);
}

void reedsBlast(World& w, Vec2 at, float radius, float strength) {
  if (!camp(w) || radius <= 0.f) return;
  for (int c = 0; c < REED_CLUMPS; ++c) {
    const ReedSite& S = w.reeds.site[c];
    if (!present(S)) continue;
    const float dx = fl(S.x) - at.x, dy = fl(S.y) - 0.5f * fl(S.h_cat) - at.y;
    const float d = sqrtf(dx * dx + dy * dy);
    if (d >= radius) continue;
    kick(w.reeds, c, (dx < 0.f ? -1.f : 1.f) * clamp01(strength) * (1.f - d / radius) * 110.f);
  }
}

void reedsBend(World& w, int clump, int stalk, Vec2 tip_offset) {
  if (!camp(w) || clump < 0 || clump >= REED_CLUMPS) return;
  Reeds& R = w.reeds;
  const ReedSite& S = R.site[clump];
  if (!present(S) || stalk < 0 || stalk >= S.n_cat + S.n_reed) return;
  // A taken stalk has nothing up there to hold: reedsTake freed its hold and the stub rings on the clump's
  // spring, so a caller that bends it once more in the same tick does not pin the stub.
  if ((S.gone >> stalk) & 1u) return;
  ReedHold* slot = nullptr;
  for (ReedHold& h : R.hold)
    if (h.ttl != 0 && h.clump == clump && h.stalk == stalk) slot = &h;
  for (ReedHold& h : R.hold)   // else a free slot, else one only ringing back after it was let go
    if (!slot && h.ttl == 0) slot = &h;
  for (ReedHold& h : R.hold)
    if (!slot && h.ttl < kHeld - 1) slot = &h;
  if (!slot) return;   // two stalks held already
  slot->clump = (int8_t)clump;
  slot->stalk = (int8_t)stalk;
  slot->dx = (int8_t)floorI(clampf(tip_offset.x, -kHoldSide, kHoldSide) * 2.f + 0.5f);
  slot->dy = (int8_t)floorI(clampf(tip_offset.y, -kHoldUp, kHoldDown) * 2.f + 0.5f);
  slot->ttl = kHeld;
}

bool reedsTake(World& w, int clump, int stalk, bool allow_cattail) {
  if (!camp(w) || clump < 0 || clump >= REED_CLUMPS) return false;
  Reeds& R = w.reeds;
  ReedSite& S = R.site[clump];
  if (!present(S) || stalk < 0 || stalk >= S.n_cat + S.n_reed) return false;
  if ((S.gone >> stalk) & 1u) return false;
  if (stalk < S.n_cat && !allow_cattail) return false;
  S.gone = (uint8_t)(S.gone | (1u << stalk));
  for (ReedHold& h : R.hold)   // nothing left up there to hold
    if (h.clump == clump && h.stalk == stalk) h = ReedHold{-1, -1, 0, 0, 0, 0};
  kick(R, clump, stalk < S.n_cat ? 30.f : -18.f);   // the stub springs; the neighbours shiver
  return true;
}

// Not a stub: the one wind (INTEGRATION_PLAN 3.1 point 1).
float reedsWind(const World& w, float x) { return ambientWindAt(w, x); }


// ---- what World, the landscape and the app call ----

void reedsInit(World& w, const Rng& loop_rng) {
  Reeds& R = w.reeds;
  memset(static_cast<void*>(&R), 0, sizeof(Reeds));
  Rng copy = loop_rng;   // the loop RNG is not advanced (HOW_TO 1.5 rule 3)
  const uint32_t a = copy.next();
  const uint32_t b = copy.next();
  const uint32_t c3 = copy.next();
  R.rng = mix32(a ^ 0x52454544u, b ^ (c3 << 7)) | 1u;   // the module's own stream (C1 draws from it)
  for (ReedHold& h : R.hold) h = ReedHold{-1, -1, 0, 0, 0, 0};
  layoutSites(w);
  layoutFringe(w);
  for (int c = 0; c < REED_CLUMPS; ++c) {   // the heads start where the wind has them: no first swing
    const ReedSite& S = R.site[c];
    if (!present(S)) continue;
    const float target = ambientWindAt(w, fl(S.x));
    R.push[c] = (int16_t)floorI(target * 256.f + 0.5f);
    R.push_v[c] = 0;
    R.sway_q[c][0] = R.sway_q[c][1] = q4(target);
    R.d_sway[c][0] = R.sway_q[c][0];
    R.d_sway[c][1] = R.sway_q[c][1];
  }
  c1Init(w);
}

void reedsSetInputs(World& w, float mood, bool night, uint8_t month) {
  if (!camp(w)) return;
  Reeds& R = w.reeds;
  if (!R.season) {   // the month is read once, at the loop's first tick: a change shows at the next loop
    R.month = month <= 12 ? month : 0;
    R.season = 1;
    w.land.reed_fringe[kFringeMonth] = (uint8_t)(R.month + 1);   // before the plants' layer is ever cached
  }
  R.day = (uint8_t)(!night && mood >= 1.5f && mood <= 4.5f);
  R.dusk = (uint8_t)(night || mood < 0.6f || mood > 4.6f);
}

void reedsStep(World& w) {
  if (!camp(w)) return;
  Reeds& R = w.reeds;
  const bool frozen = w.react.active;   // the doorbell: holds wait, nobody brushes (F9, F23)
  for (ReedHold& h : R.hold) {
    if (h.ttl == 0 || frozen) continue;
    --h.ttl;
    if (h.ttl <= kHeld - 2 - kReleaseTicks) h = ReedHold{-1, -1, 0, 0, 0, 0};
  }
  // The heron lands beside the near clumps and leaves again: one push each time (CATTAILS_PLAN 3.2).
  Vec2 heron{0.f, 0.f};
  const bool heron_in = w.stream.visit.kind == SV_HERON && streamGuestAt(w, heron);
  for (int c = 0; c < REED_CLUMPS; ++c) {
    ReedSite& S = R.site[c];
    if (!present(S)) continue;
    const bool near = heron_in && absf(heron.x - fl(S.x)) < 30.f && absf(heron.y - fl(S.y)) < 24.f;
    const bool was = (R.guest_near >> c) & 1u;
    if (near != was) {
      kick(R, c, (fl(S.x) >= heron.x ? 1.f : -1.f) * 35.f);
      R.guest_near = (uint8_t)(near ? (R.guest_near | (1u << c)) : (R.guest_near & ~(1u << c)));
    }
    const float target = ambientWindAt(w, fl(S.x));
    float x = fl(R.push[c]) / 256.f, v = fl(R.push_v[c]) / 1024.f;
    if (!frozen) {
      const int dir = brushDir(w, S);
      if (dir != 0) {   // walking through: the heads are carried the way the body goes, then ring back
        const float vb = kBrushTipPxPerS * SS_TICK_DT / headGain(S);
        if (v * fl(dir) < vb) v = fl(dir) * vb;
      }
    }
    v += kSpringK * (target - x) - kSpringD * v;
    x += v;
    x = clampf(x, -24.f, 24.f);
    v = clampf(v, -6.f, 6.f);
    R.push[c] = (int16_t)floorI(x * 256.f + 0.5f);
    R.push_v[c] = (int16_t)floorI(v * 1024.f + 0.5f);
    const float heads = fl(R.push[c]) / 256.f;
    R.sway_q[c][0] = q4(target + kLeafFollow * (heads - target));
    R.sway_q[c][1] = q4(heads);
  }
  c1Ambient(w);   // the dragonfly, the seeds, a falling stem (after the holds' clocks: a perch holds this tick)
}


void reedsTrack(World& w, Renderer& r) {
  if (!camp(w)) return;
  Reeds& R = w.reeds;
  const bool building = !buildDone(w.build);   // popping in: marked every tick, at the overshoot's box
  const uint8_t sq = snowQ(w);
  const bool snow_moved = sq != R.snow_q;
  for (int c = 0; c < REED_CLUMPS; ++c) {
    const ReedSite& S = R.site[c];
    if (!present(S)) continue;
    uint8_t look = (uint8_t)(S.gone & 0x1Fu);
    if (warmOn(w, c)) look = (uint8_t)(look | 0x20u);
    for (const ReedHold& h : R.hold)
      if (h.ttl != 0 && h.clump == c) look = (uint8_t)(look | 0x40u);
    const bool changed = building || snow_moved || look != R.d_look[c] || (look & 0x40u) != 0 ||
                         R.sway_q[c][0] != R.d_sway[c][0] || R.sway_q[c][1] != R.d_sway[c][1];
    if (changed) {
      const Rect b = clumpBox(S, c, building ? kPopMax : 1.f).clipped(kFrame);
      if (!b.empty()) r.markDirtyRect(b);
    }
    R.d_sway[c][0] = R.sway_q[c][0];
    R.d_sway[c][1] = R.sway_q[c][1];
    R.d_look[c] = look;
  }
  R.snow_q = sq;
  c1Track(w, r);
}

bool reedsActorY(const World& w, int clump, int16_t& y) {
  if (reedsBounds(w, clump).empty()) return false;
  y = w.reeds.site[clump].y;
  return true;
}

void reedsDraw(const World& w, int clump, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(reedsBounds(w, clump), clip)) return;
  drawClump(w, clump, s, clip, pal);
}

void reedsDrawStatic(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, const BuildState& b) {
  const uint8_t mc = ls.reed_fringe[kFringeMonth];
  const Pixel col = colsFor(pal, lookFor(mc ? (uint8_t)(mc - 1) : 0), false, false).leaf;
  Vec2 p[7];
  for (int i = 0; i < kFringeSlots; ++i) {
    const uint8_t v = ls.reed_fringe[i];
    if (!v) continue;
    const float x = fl(((v & 0x7F) - 1) * 8);
    float y;
    if (!bankY(ls, x, (v & 0x80) != 0, y)) continue;
    y = floorf(y) + 1.f;
    if (y + 2.f < fl(clip.y0) || y - 14.f > fl(clip.y1)) continue;
    const float hs = buildPlantScale(b, x, y);
    if (hs <= 0.01f) continue;
    fillPolygon(s, p, fringePts(v, x, y, hs, p), col, clip);
  }
}


// =========================================================================================================
// Step C1: the pick, the stem in his mouth and the small life (CATTAILS_PLAN 3.5 and 4.1 to 4.4, as
// INTEGRATION_PLAN 4.4 and REVIEW_FIXES F11, F16 and F22 changed them; Mike, 2026-11-17: "having the hiker
// sometimes pick one and put in his mouth like huckleberry finn").
// =========================================================================================================

namespace {

// The pick and the blackbird change when the story and the minors play (the minors wait for a pick, a pick
// waits for a minor), so they wait for the stage 22 switch as the pieces that change a plan do: with it off
// every baseline log (the --loops lines, the beat and minor logs) stays as it was. A stem a caller hands
// him (reedsGiveChew), its endings and the small life change nothing but pixels and do not wait.
constexpr bool kStoryLive = SS_S22 != 0;

enum PickStage : uint8_t {
  PK_NONE = 0, PK_A, PK_A_ABORT, PK_B_STAND, PK_B_WALK, PK_B_LOOK, PK_B_REACH, PK_B_UP, PK_B_HOME, PK_B_SIT, PK_TOSS
};
enum ChewState : uint8_t { CH_NONE = 0, CH_HAND, CH_MOUTH };
enum FallKind : uint8_t { FALL_NONE = 0, FALL_DROP, FALL_TOSS_R, FALL_TOSS_L };
enum FlyState : uint8_t { FL_AWAY = 0, FL_DART, FL_HOVER, FL_PERCH, FL_LEAVE };
constexpr uint8_t kFormSeated = 1, kFormStroll = 2, kFormFailed = 0x80, kFlyPerchBit = 0x80;
constexpr int16_t kNoPos = -32768;

constexpr float kHz = (float)SS_TICK_HZ;
inline uint32_t ticksOf(float s) { return (uint32_t)(s * kHz + 0.5f); }
inline float secs(uint32_t t) { return (float)t * SS_TICK_DT; }
inline float dist(Vec2 a, Vec2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

// ---- tuning (PROPOSED: Mike judges the look; Mike, 2026-11-18) ----
constexpr float kPickChance = 0.45f;                            // camp loops that want a pick
constexpr float kPickAfterMin = 60.f, kPickAfterMax = 200.f;    // story seconds before the first try
constexpr float kRetry = 45.f;                                  // a reach that failed: the next try waits this
constexpr float kChewMin = 150.f, kChewMax = 260.f;             // seconds in his mouth
constexpr float kChewGrace = 40.f;    // past its time with no pause to toss it in: it drops by itself
constexpr float kWilt = 30.f;         // its last seconds: the outer third droops, the plume falls off
constexpr float kFormA = 6.4f, kGraspA = 2.1f, kTakeA = 3.0f, kMouthA = 5.1f;
constexpr float kReachOk = 14.f;      // the hand must come this near the tip at the grasp, or nothing is taken
constexpr float kStrollSpeed = 30.f;  // px/s, slower than the stroll's 34
constexpr float kStrollReach = 90.f;  // a camp-bank clump this near (to its stand point)
constexpr float kStemLen = 13.f, kStemW = 1.2f, kStemPlume = 3.f;   // in his mouth
constexpr float kHandStem = 16.f;     // the upper stem with its plume, in his hand
constexpr float kRest = 0.35f, kRoll = 0.10f;   // its angle above his face's line, and at the other corner
constexpr float kMouthBox = 20.f, kHandBox = 22.f;   // reedsMouthBounds: half sides round the mouth, the hand
constexpr int kFallTicks = 12;        // a dropped or tossed stem tumbles 0.5 s and is gone
constexpr float kTossDist = 34.f;

// The beats that use his mouth: the stem drops as one begins (CATTAILS_PLAN 4.4; tai chi and the mushroom
// call reedsDropChew themselves, INTEGRATION_PLAN 4.4).
const char* const kMouthBeats[] = {"fire", "dinner", "bear", "close", "coffee", "breakfast", "wash", "lunch", "echo",
                                   "bathe", "ants_cookie", "rain_shelter", "shadow_theater", "raccoon_pack", "leave",
                                   "day_hike"};

// The module's own stream (CATTAILS_PLAN 7): a 32 bit xorshift, as housedogs.cpp uses. One draw a statement.
uint32_t nextR(Reeds& R) {
  uint32_t x = R.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  R.rng = x ? x : 0x9E3779B9u;
  return R.rng;
}
float rnd(Reeds& R, float a, float b) { return a + (b - a) * (float)(nextR(R) >> 8) * (1.f / 16777216.f); }

bool isPauseW(const World& w) { return w.minors.beat && strcmp(w.minors.beat, "pause") == 0; }
bool wetNow(const World& w) {
  const uint8_t k = w.weather.kind;
  return k == WX_RAIN || k == WX_SNOW || k == WX_STORM || w.weather.rain > 0.05f;
}
bool mouthBeat(const World& w) {
  const char* b = w.minors.beat;
  if (!b) return false;
  for (const char* n : kMouthBeats)
    if (strcmp(b, n) == 0) return true;
  return false;
}
bool handsFree(const World& w) {
  if (w.hiker.carry != CARRY_NONE || !w.hiker.hat_on || w.pack.held) return false;
  for (const Item& it : w.items)
    if (it.held && !it.critter) return false;
  return true;
}

// He is the overlay's for `need` seconds: the weather's hikerFree conditions (in a pause with that long left,
// nothing holding him, still, standing or sitting, no minor), no weather gag or its traces, no dog being
// patted, no day piece, no doorbell, fire and ice or visitors (CATTAILS_PLAN 8, 6.3).
bool storyFree(const World& w, float need) {
  const Hiker& h = w.hiker;
  if (!w.fx.on || !h.visible || w.busy || w.major.on || !scriptDone(w) || w.day.kind != DAY_NONE) return false;
  if (!isPauseW(w) || w.minors.beat_left < need + 1.f) return false;
  if (h.walking || h.blending || (h.target != POSE_SIT && h.target != POSE_STAND)) return false;
  if (w.minors.cur != MN_NONE || w.minors.fx_head || w.minors.fx_arm) return false;
  const Weather& x = w.weather;
  if (x.gag != WG_NONE || x.hug_w > 0.f || x.ov_w > 0.f || x.shiver > 0.f) return false;
  if (w.dogs.pat || w.react.active || w.fi.on || visitorsHolds(w)) return false;
  return true;
}

// ---- the overlay's keyframes: his near arm (shoulder, elbow, weight over the pose), head and lean ----

struct Key { float t, sh, el, w, head, lean; };

// Form A, seated (CATTAILS_PLAN 4.1): the head first, the arm back behind his hip, the grasp, the draw up,
// round in front, a look, up to his mouth (the mug sip's elbow), the arm back to rest.
const Key kKeysA[] = {{0.0f, -1.15f, 0.35f, 0.f, 0.f, 0.f},       {0.9f, -1.15f, 0.35f, 0.f, -0.08f, 0.f},
                      {2.1f, -1.15f, 0.35f, 1.f, -0.04f, -0.10f}, {2.6f, -1.15f, 0.35f, 1.f, -0.04f, -0.10f},
                      {3.3f, -0.40f, 0.90f, 1.f, 0.f, -0.10f},    {4.1f, 0.90f, 1.90f, 1.f, 0.10f, 0.f},
                      {4.6f, 0.90f, 1.90f, 1.f, 0.10f, 0.f},      {5.3f, 0.90f, 2.45f, 1.f, 0.f, 0.f},
                      {5.5f, 0.90f, 2.45f, 1.f, 0.f, 0.f},        {6.4f, 0.90f, 2.45f, 0.f, 0.f, 0.f}};
// Form B, crouched at the clump: the reach down and forward, a hold, the pull (from the stage's start; the
// crouch blends in over its first 0.45 s).
const Key kKeysReach[] = {{0.45f, 1.0f, 0.25f, 0.f, 0.f, 0.f}, {1.25f, 1.0f, 0.25f, 1.f, 0.15f, 0.f},
                          {1.65f, 1.0f, 0.25f, 1.f, 0.15f, 0.f}, {2.25f, 0.5f, 0.90f, 1.f, 0.10f, 0.f}};
// Form B, standing again: the stem at his chest, a look at it, up to his mouth, a while with it, the arm down.
const Key kKeysUp[] = {{0.0f, 0.5f, 0.90f, 1.f, 0.10f, 0.f}, {0.5f, 0.6f, 1.70f, 1.f, 0.10f, 0.f},
                       {1.0f, 0.6f, 1.70f, 1.f, 0.10f, 0.f}, {1.7f, 0.9f, 2.45f, 1.f, 0.f, 0.f},
                       {2.5f, 0.9f, 2.45f, 1.f, 0.f, 0.f},   {3.1f, 0.9f, 2.45f, 0.f, 0.f, 0.f}};
// The toss: the hand up to his mouth, takes it, flicks it away low to his front.
constexpr float kTossTake = 0.55f, kTossLet = 0.68f, kTossEnd = 1.6f;
const Key kKeysToss[] = {{0.0f, 0.9f, 2.45f, 0.f, 0.f, 0.f},   {0.6f, 0.9f, 2.45f, 1.f, 0.f, 0.f},
                         {0.75f, 1.5f, 0.50f, 1.f, 0.05f, 0.f}, {1.0f, 1.2f, 0.40f, 1.f, 0.f, 0.f},
                         {1.6f, 1.2f, 0.40f, 0.f, 0.f, 0.f}};

Key keyAtN(const Key* k, int N, float t) {
  if (t <= k[0].t) return k[0];
  if (t >= k[N - 1].t) return k[N - 1];
  int i = 0;
  while (i + 1 < N && t >= k[i + 1].t) ++i;
  const float u = easeInOut((t - k[i].t) / (k[i + 1].t - k[i].t));
  return Key{t, lerp(k[i].sh, k[i + 1].sh, u), lerp(k[i].el, k[i + 1].el, u), lerp(k[i].w, k[i + 1].w, u),
             lerp(k[i].head, k[i + 1].head, u), lerp(k[i].lean, k[i + 1].lean, u)};
}
template <int N>
Key keyAt(const Key (&k)[N], float t) {
  return keyAtN(k, N, t);
}

// The reach for the reed: the near arm's angles worked out when the reach begins (reachFor), in place of the
// plan's fixed ones, so his hand meets the stalk's tip wherever the layout put it.
inline float reachSh(const Reeds& R) { return 0.001f * fl(R.reach_sh); }
inline float reachEl(const Reeds& R) { return 0.001f * fl(R.reach_el); }

bool overlayOf(const Reeds& R, Key& o) {
  const float t = secs(R.pick_t);
  switch (R.pick) {
    case PK_A: {
      Key k[sizeof kKeysA / sizeof kKeysA[0]];
      memcpy(k, kKeysA, sizeof k);
      for (int i = 0; i < 4; ++i) {   // the reach back and its hold
        k[i].sh = reachSh(R);
        k[i].el = reachEl(R);
      }
      o = keyAtN(k, (int)(sizeof k / sizeof k[0]), t);
      if (t >= kGraspA && t < 2.6f) o.sh += 0.04f * sinT(0.5f * (t - kGraspA) / 0.5f);   // one small tug
      return true;
    }
    case PK_A_ABORT: {   // the hand never came near: the arm goes back to rest, nothing taken
      o = keyAt(kKeysA, kGraspA);
      o.sh = reachSh(R);
      o.el = reachEl(R);
      const float k = 1.f - easeInOut(clamp01(t / 0.8f));
      o.w *= k;
      o.head *= k;
      o.lean *= k;
      return true;
    }
    case PK_B_REACH:
    case PK_B_UP: {
      Key kr[sizeof kKeysReach / sizeof kKeysReach[0]], ku[sizeof kKeysUp / sizeof kKeysUp[0]];
      memcpy(kr, kKeysReach, sizeof kr);
      memcpy(ku, kKeysUp, sizeof ku);
      for (int i = 0; i < 3; ++i) {   // down to the reed and the hold; the pull draws it up and toward him
        kr[i].sh = reachSh(R);
        kr[i].el = reachEl(R);
      }
      kr[3].sh = ku[0].sh = reachSh(R) - 0.5f;
      kr[3].el = ku[0].el = reachEl(R) + 0.65f;
      const int nr = (int)(sizeof kr / sizeof kr[0]), nu = (int)(sizeof ku / sizeof ku[0]);
      if (R.pick == PK_B_REACH) {
        o = keyAtN(kr, nr, t);
      } else if (R.pick_form & kFormFailed) {
        o = keyAtN(kr, nr, 2.25f);
        const float k = 1.f - easeInOut(clamp01(t / 0.6f));
        o.w *= k;
        o.head *= k;
      } else {
        o = keyAtN(ku, nu, t);
      }
      return true;
    }
    case PK_TOSS: o = keyAt(kKeysToss, t); return true;
    default: return false;
  }
}

// ---- stalks under a hand, a beak or a dragonfly ----

// The wind's part of stalk k's tip offset (what stalkOffset gives it with no hold).
float windDx(const Reeds& R, int c, const StalkDef& d) {
  const float s0 = 0.25f * fl(R.sway_q[c][0]), s1 = 0.25f * fl(R.sway_q[c][1]);
  return clampf(soft(lerp(s0, s1, d.blend) * sqrtf(d.h / 18.f) * d.stiff, kTipSoft), -kHoldSide, kHoldSide);
}

// Hold stalk k's tip at `ease` of the way from where the wind has it to `to` (as far as a hold may take it).
void bendTo(World& w, int c, int k, Vec2 to, float ease) {
  const Reeds& R = w.reeds;
  const ReedSite& S = R.site[c];
  StalkDef d;
  if (!stalkDef(S, c, k, lookFor(R.month), d)) return;
  const float wdx = windDx(R, c, d);
  const float by = fl(S.y);
  const Vec2 free_tip = stalkPt(d, by, wdx, 0.f, 1.f);
  const Vec2 t{lerp(free_tip.x, to.x, ease), lerp(free_tip.y, to.y, ease)};
  const float dx = t.x - d.fx - d.lean;
  const float dy = t.y - (by - d.h) - 0.4f * dx * dx / maxf(d.h, 8.f);
  reedsBend(w, c, k, Vec2{dx, dy});
}

// Hold stalk k bent by (ox, oy) px from where the wind has it (a beak's weight, a dragonfly's).
void bendBy(World& w, int c, int k, float ox, float oy) {
  const Reeds& R = w.reeds;
  StalkDef d;
  if (!stalkDef(R.site[c], c, k, lookFor(R.month), d)) return;
  reedsBend(w, c, k, Vec2{windDx(R, c, d) + ox, oy});
}

bool stalkGone(const Reeds& R, int c, int k) { return ((R.site[c].gone >> k) & 1u) != 0; }

// The near arm's shoulder and elbow (radians, a grid search) that put his hand nearest `to`, the rest of pose
// `base` as it is: a straighter arm wins a near tie. Kept in milliradians in Reeds::reach_sh, reach_el.
void reachFor(World& w, const HikerPose& base, Vec2 to, float sh0, float sh1, float el0, float el1) {
  Reeds& R = w.reeds;
  Hiker c = w.hiker;
  c.pose = base;
  float best = 3.0e38f, bs = sh0, be = el0;
  for (int i = 0; i <= 28; ++i)
    for (int j = 0; j <= 16; ++j) {
      const float s = lerp(sh0, sh1, fl(i) / 28.f), e = lerp(el0, el1, fl(j) / 16.f);
      c.pose.shoulder[0] = s;
      c.pose.elbow[0] = e;
      const Vec2 p = hikerHandPos(c, 0);
      const float score = (p.x - to.x) * (p.x - to.x) + (p.y - to.y) * (p.y - to.y) + 2.f * e * e;
      if (score < best) {
        best = score;
        bs = s;
        be = e;
      }
    }
  R.reach_sh = (int16_t)floorI(bs * 1000.f + 0.5f);
  R.reach_el = (int16_t)floorI(be * 1000.f + 0.5f);
}

// Where the wind has stalk k's tip now, with no hold: where the hand reaches for.
Vec2 windTip(const World& w, int c, int k) {
  const Reeds& R = w.reeds;
  const ReedSite& S = R.site[c];
  StalkDef d;
  if (!stalkDef(S, c, k, lookFor(R.month), d)) return Vec2{fl(S.x), fl(S.y)};
  return stalkPt(d, fl(S.y), windDx(R, c, d), 0.f, 1.f);
}

// ---- the stem in his mouth ----

void giveChew(World& w, float seconds) {
  Reeds& R = w.reeds;
  R.chew = CH_MOUTH;
  R.chew_until = storyTick(w) + ticksOf(seconds);
}
float chewSeconds(const World& w) {   // 150 to 260 s, from the layout and the clock (no draw)
  const Reeds& R = w.reeds;
  return kChewMin + (kChewMax - kChewMin) * h01(mix32((uint32_t)(int)R.site[RS_FORE].x, R.pick_clump), storyTick(w));
}

// The face's axes as hikerMouthPos turns them: out through the face and up through the crown.
void faceAxes(const Hiker& h, Vec2& hf, Vec2& hu) {
  const float ht = (h.pose.lean + h.pose.head) * (1.f / 6.2831853f);
  const float sx = h.facing < 0 ? -1.f : 1.f;
  hu = Vec2{sx * sinT(ht), -cosT(ht)};
  hf = Vec2{sx * cosT(ht), sinT(ht)};
}

// The stem's angle above his face's line now (radians): the slow roll to the other corner of his mouth and
// back, two quick chews now and then, a bob a little behind his steps. All from the story clock and his walk
// phase (CATTAILS_PLAN 4.4), so a doorbell delays them instead of using them up.
float stemAngle(const World& w) {
  const uint32_t st = storyTick(w);
  // The roll: one in each 6 s slot, at a hashed moment in it: 3.5 to 8.5 s apart, never even.
  const uint32_t slot_len = ticksOf(6.f);
  const uint32_t n = st / slot_len, in = st % slot_len;
  const uint32_t at = (uint32_t)(h01(n, 0x5011u) * 2.5f * kHz);
  const uint32_t rolls = n + (in >= at ? 1u : 0u);
  const uint32_t prev_at = (uint32_t)(h01(n - 1u, 0x5011u) * 2.5f * kHz);
  const float since = in >= at ? secs(in - at) : secs(in + slot_len - prev_at);
  const bool other = (rolls & 1u) != 0;
  const float u = easeInOut(clamp01(since / 0.4f));
  float a = other ? lerp(kRest, kRoll, u) : lerp(kRoll, kRest, u);
  // The chew: every 9 s slot, two quick nods of 0.08 rad at a hashed moment.
  const uint32_t cl = ticksOf(9.f);
  const uint32_t cn = st / cl, ci = st % cl;
  const float cat = h01(cn, 0xC4E3u) * 2.f;
  const float ct = secs(ci) - cat;
  if (ct >= 0.f && ct < 0.5f) a -= 0.08f * fabsf(sinT(ct / 0.5f));
  const Hiker& h = w.hiker;
  if (h.walking) a += 0.06f * h.walk_w * sinT(2.f * h.walk_phase - 0.1f);
  return a;
}

struct StemGeom {
  Vec2 a, k, c;     // the near end, the knee at two thirds, the outer end
  Vec2 pd;          // the plume's direction (from c)
  float plume;      // the plume's length, 0 none
};

// Where the stem is drawn this tick, in his mouth or his hand. False when there is none to draw.
bool stemGeom(const World& w, StemGeom& g) {
  const Reeds& R = w.reeds;
  const Hiker& h = w.hiker;
  if (!w.fx.on || R.chew == CH_NONE) return false;
  Vec2 hf, hu;
  faceAxes(h, hf, hu);
  const float a = stemAngle(w);
  const Vec2 md{hf.x * cosf(a) + hu.x * sinf(a), hf.y * cosf(a) + hu.y * sinf(a)};
  const float sx = h.facing < 0 ? -1.f : 1.f;
  if (R.chew == CH_HAND) {
    const Vec2 grip = hikerHandPos(h, 0);
    Vec2 d{sx * 0.5f, -0.866f};   // held up and forward, the plume end out
    if (R.pick == PK_A) {          // turned to his mouth's line as it goes up to it
      const float u = easeInOut(clamp01((secs(R.pick_t) - 4.6f) / 0.5f));
      d = Vec2{lerp(d.x, md.x, u), lerp(d.y, md.y, u)};
    } else if (R.pick == PK_B_UP) {
      const float u = easeInOut(clamp01((secs(R.pick_t) - 1.0f) / 0.7f));
      d = Vec2{lerp(d.x, md.x, u), lerp(d.y, md.y, u)};
    } else if (R.pick == PK_TOSS) {
      d = md;
    }
    const float l = sqrtf(d.x * d.x + d.y * d.y);
    d = Vec2{d.x / l, d.y / l};
    g.a = Vec2{grip.x - 3.f * d.x, grip.y - 3.f * d.y};
    g.k = Vec2{grip.x + 6.f * d.x, grip.y + 6.f * d.y};
    g.c = Vec2{grip.x + (kHandStem - 3.f) * d.x, grip.y + (kHandStem - 3.f) * d.y};
    g.pd = d;
    g.plume = 4.f;
    return true;
  }
  const Vec2 m = hikerMouthPos(h);
  g.a = Vec2{m.x - md.x, m.y - md.y};
  g.k = Vec2{m.x + md.x * (kStemLen * 2.f / 3.f), m.y + md.y * (kStemLen * 2.f / 3.f)};
  // Wilting: over its last kWilt seconds the outer third droops (turned the way that points it down).
  const uint32_t st = storyTick(w);
  float wilt = 0.f;
  if (st + ticksOf(kWilt) > R.chew_until) wilt = clamp01(1.f - secs(R.chew_until > st ? R.chew_until - st : 0u) / kWilt);
  const float droop = 0.5f * wilt;
  const float c = cosf(droop), s = sinf(droop);
  const Vec2 r1{md.x * c - md.y * s, md.x * s + md.y * c}, r2{md.x * c + md.y * s, -md.x * s + md.y * c};
  const Vec2 od = r1.y >= r2.y ? r1 : r2;
  g.c = Vec2{g.k.x + od.x * (kStemLen / 3.f), g.k.y + od.y * (kStemLen / 3.f)};
  g.pd = od;
  g.plume = wilt < 0.6f ? kStemPlume : 0.f;
  return true;
}

void drawStem(const StemGeom& g, Pixel stem, Pixel plume, Surface& s, const Rect& clip) {
  strokeCapsule(s, g.a, g.k, kStemW, stem, clip);
  strokeCapsule(s, g.k, g.c, kStemW * 0.9f, stem, clip);
  if (g.plume > 0.f)
    strokeCapsule(s, g.c, Vec2{g.c.x + g.pd.x * g.plume, g.c.y + g.pd.y * g.plume}, g.plume > 3.5f ? 2.4f : 1.9f, plume,
                  clip);
}

// A dropped or tossed stem: from where it left him to its ground, tumbling.
bool fallGeom(const Reeds& R, int t, Vec2& a, Vec2& b) {
  if (R.fall_kind == FALL_NONE || t < 0 || t > kFallTicks) return false;
  const float u = fl(t) / fl(kFallTicks);
  const Vec2 p0{fl(R.fall_x), fl(R.fall_y)};
  const float ground = fl(R.fall_g);
  Vec2 p;
  float ang;
  if (R.fall_kind == FALL_DROP) {
    p = Vec2{p0.x + 3.f * u, lerp(p0.y, ground, u * u)};
    ang = -0.35f + 2.2f * u;
  } else {
    const float sx = R.fall_kind == FALL_TOSS_R ? 1.f : -1.f;
    float lx = p0.x + sx * kTossDist;
    const float ly = ground + 3.f;
    if (lx > 530.f && ly > 404.f) lx = 530.f;   // never into the status chip's corner
    p = Vec2{lerp(p0.x, lx, u), lerp(p0.y, ly, u) - 10.f * 4.f * u * (1.f - u)};
    ang = sx * 5.f * u;
  }
  p = Vec2{floorf(p.x) + 0.5f, floorf(p.y) + 0.5f};   // whole pixels: what the tracker marks is what is drawn
  const float c = cosf(ang), s = sinf(ang);
  a = Vec2{p.x - 6.f * c, p.y - 6.f * s};
  b = Vec2{p.x + 6.f * c, p.y + 6.f * s};
  return true;
}

void dropChew(World& w) {
  Reeds& R = w.reeds;
  if (R.chew == CH_NONE) return;
  StemGeom g;
  if (w.fx.on && stemGeom(w, g)) {
    R.fall_kind = FALL_DROP;
    R.fall_x = (int16_t)floorI(0.5f * (g.a.x + g.c.x));
    R.fall_y = (int16_t)floorI(0.5f * (g.a.y + g.c.y));
    R.fall_g = (int16_t)floorI(w.hiker.feet.y + 1.f);
    R.fall_t = 0;
  }
  R.chew = CH_NONE;
}

// ---- the pick ----

void nextStage(Reeds& R, uint8_t st) {
  R.pick = st;
  R.pick_t = 0;
}

// The overlay is over. A pick that took its reed is the loop's one; one that failed may try once more later.
void endPick(World& w, bool took) {
  Reeds& R = w.reeds;
  R.pick = PK_NONE;
  R.pick_t = 0;
  if (took) {
    R.want_pick = 0;
  } else {
    R.pick_after = storyTick(w) + ticksOf(kRetry);
    R.pick_form = 0;
  }
}

// Form B cut short (a beat began under it; it should not): him back where it found him, at rest, as the hat
// gag's gagHome does, with the stem in his mouth if he had it in his hand.
void strollHome(World& w) {
  Reeds& R = w.reeds;
  Hiker& h = w.hiker;
  h.feet = Vec2{fl(R.home_x), fl(R.home_y)};
  h.facing = R.home_face < 0 ? -1 : 1;
  hikerSetPose(h, (PoseId)R.home_pose, 0.05f);
  w.fx.path_on = R.home_path != 0;
  if (R.chew == CH_HAND) giveChew(w, chewSeconds(w));
  endPick(w, R.chew != CH_NONE);
}

// At the grasp: is the hand near enough the tip it bent toward it?
bool handAtTip(const World& w, int c, int k) { return dist(hikerHandPos(w.hiker, 0), reedsTip(w, c, k)) <= kReachOk; }

void stepFormA(World& w) {
  Reeds& R = w.reeds;
  Hiker& h = w.hiker;
  const float t = secs(R.pick_t);
  const int c = R.pick_clump, k = R.pick_stalk;
  if (R.pick == PK_A_ABORT) {
    if (t >= 0.8f) {
      endPick(w, false);
      return;
    }
    ++R.pick_t;
    return;
  }
  if (w.major.on || !scriptDone(w) || !w.fx.on || !isPauseW(w) || h.target != POSE_SIT || h.walking) {
    if (R.chew == CH_HAND) giveChew(w, chewSeconds(w));
    endPick(w, R.chew != CH_NONE);
    return;
  }
  if (t >= 1.7f && t < kTakeA && !stalkGone(R, c, k)) {   // it bends to meet his hand, then follows it
    bendTo(w, c, k, hikerHandPos(h, 0), easeInOut(clamp01((t - 1.7f) / 0.4f)));
  }
  if (R.pick_t == ticksOf(kGraspA)) {
    if (!handAtTip(w, c, k)) {   // an odd layout: the arm goes back to rest, nothing taken this pause
      R.pick_form |= kFormFailed;
      nextStage(R, PK_A_ABORT);
      return;
    }
    kick(R, c, 6.f);   // the neighbours shiver
  }
  if (R.pick_t == ticksOf(kTakeA) && reedsTake(w, c, k)) R.chew = CH_HAND;
  if (R.pick_t == ticksOf(kMouthA) && R.chew == CH_HAND) giveChew(w, chewSeconds(w));
  if (t >= kFormA) {
    endPick(w, R.chew != CH_NONE);
    return;
  }
  ++R.pick_t;
}

bool standPointOf(const World& w, Vec2& st) {
  const Reeds& R = w.reeds;
  const int side = R.home_x < R.site[R.pick_clump].x ? -1 : 1;
  return reedsStandPoint(w, R.pick_clump, side, st);
}

void finishStroll(World& w) {
  Reeds& R = w.reeds;
  w.fx.path_on = R.home_path != 0;
  endPick(w, (R.pick_form & kFormFailed) == 0);
}

void stepFormB(World& w) {
  Reeds& R = w.reeds;
  Hiker& h = w.hiker;
  const float t = secs(R.pick_t);
  if (w.major.on || !scriptDone(w) || !w.fx.on || !isPauseW(w)) {
    strollHome(w);
    return;
  }
  const int c = R.pick_clump, k = R.pick_stalk;
  Vec2 st;
  if (!standPointOf(w, st)) st = Vec2{fl(R.home_x), fl(R.home_y)};
  const float hx = fl(R.home_x), hy = fl(R.home_y);
  switch (R.pick) {
    case PK_B_STAND:
      if (R.pick_t == 0 && R.home_pose == POSE_SIT) hikerSetPose(h, POSE_STAND, 0.8f);
      if (t >= (R.home_pose == POSE_SIT ? 0.9f : 0.2f)) {
        hikerWalkTo(h, st.x, kStrollSpeed);
        nextStage(R, PK_B_WALK);
        return;
      }
      break;
    case PK_B_WALK: {   // x is the walk's; y slides along with it, as a script's walk path does
      const float span = st.x - hx;
      const float u = fabsf(span) > 0.5f ? clamp01((h.feet.x - hx) / span) : 1.f;
      h.feet.y = lerp(hy, st.y, u);
      if (!h.walking) {
        h.feet = st;
        h.facing = fl(R.site[c].x) >= h.feet.x ? 1 : -1;   // he faces it and stands a moment, looking
        nextStage(R, PK_B_LOOK);
        return;
      }
      break;
    }
    case PK_B_LOOK:
      if (t >= 0.6f) {
        hikerSetPose(h, POSE_CROUCH, 0.45f);
        reachFor(w, hikerPoseDef(POSE_CROUCH), windTip(w, c, k), -0.2f, 2.2f, -0.2f, 1.4f);
        nextStage(R, PK_B_REACH);
        return;
      }
      break;
    case PK_B_REACH:
      if (t >= 0.95f && !stalkGone(R, c, k)) bendTo(w, c, k, hikerHandPos(h, 0), easeInOut(clamp01((t - 0.95f) / 0.4f)));
      if (R.pick_t == ticksOf(1.65f)) {
        if (!handAtTip(w, c, k)) {
          R.pick_form |= kFormFailed;
          nextStage(R, PK_B_UP);
          return;
        }
        kick(R, c, 6.f);
      }
      if (R.pick_t == ticksOf(2.25f)) {
        if (reedsTake(w, c, k)) R.chew = CH_HAND;
        else R.pick_form |= kFormFailed;
        nextStage(R, PK_B_UP);
        return;
      }
      break;
    case PK_B_UP:
      if (R.pick_t == 0) hikerSetPose(h, POSE_STAND, 0.5f);
      if (R.chew == CH_HAND && R.pick_t == ticksOf(1.7f)) giveChew(w, chewSeconds(w));
      if (t >= ((R.pick_form & kFormFailed) ? 1.0f : 3.1f)) {
        hikerWalkTo(h, hx, kStrollSpeed);
        nextStage(R, PK_B_HOME);
        return;
      }
      break;
    case PK_B_HOME: {
      const float span = hx - st.x;
      const float u = fabsf(span) > 0.5f ? clamp01((h.feet.x - st.x) / span) : 1.f;
      h.feet.y = lerp(st.y, hy, u);
      if (!h.walking) {
        h.feet = Vec2{hx, hy};
        h.facing = R.home_face < 0 ? -1 : 1;
        if (R.home_pose == POSE_SIT) {
          hikerSetPose(h, POSE_SIT, 0.9f);
          nextStage(R, PK_B_SIT);
          return;
        }
        finishStroll(w);
        return;
      }
      break;
    }
    case PK_B_SIT:
      if (t >= 0.9f) {
        finishStroll(w);
        return;
      }
      break;
    default: break;
  }
  ++R.pick_t;
}

// The toss (CATTAILS_PLAN 4.4): the near hand up to his mouth, takes it, flicks it away low to his front.
void stepToss(World& w) {
  Reeds& R = w.reeds;
  Hiker& h = w.hiker;
  if (!w.fx.on) {
    R.chew = CH_NONE;
    nextStage(R, PK_NONE);
    return;
  }
  if (w.major.on || !scriptDone(w) || !isPauseW(w)) {   // should not happen inside a pause: it just drops
    dropChew(w);
    nextStage(R, PK_NONE);
    return;
  }
  if (R.pick_t == ticksOf(kTossTake) && R.chew == CH_MOUTH) R.chew = CH_HAND;
  if (R.pick_t == ticksOf(kTossLet) && R.chew == CH_HAND) {
    const Vec2 g = hikerHandPos(h, 0);
    R.fall_kind = h.facing < 0 ? FALL_TOSS_L : FALL_TOSS_R;
    R.fall_x = (int16_t)floorI(g.x);
    R.fall_y = (int16_t)floorI(g.y);
    R.fall_g = (int16_t)floorI(h.feet.y);
    R.fall_t = 0;
    R.chew = CH_NONE;
  }
  if (secs(R.pick_t) >= kTossEnd) {
    nextStage(R, PK_NONE);
    return;
  }
  ++R.pick_t;
}

// May a pick start now? Form A when he sits at his place by the fire facing it with the fireside reed behind
// him; otherwise form B to a camp-bank clump within 90 px.
bool tryFormA(World& w) {
  Reeds& R = w.reeds;
  const Hiker& h = w.hiker;
  const ReedSite& S = R.site[RS_FIRESIDE];
  if (!present(S) || S.n_reed == 0 || h.target != POSE_SIT) return false;
  const int k = S.n_cat;   // the pick reed (REVIEW_FIXES F24), at the clump's own x
  if (stalkGone(R, RS_FIRESIDE, k)) return false;
  const Vec2 sit = w.spine.camp.sit;
  if (fabsf(h.feet.x - sit.x) > 3.f || fabsf(h.feet.y - sit.y) > 3.f) return false;
  if ((w.fire.pos.x - h.feet.x) * fl(h.facing) <= 0.f) return false;   // facing the fire, his back to the clump
  if (!storyFree(w, kFormA + 0.1f)) return false;   // 7.5 s of the pause left (storyFree asks 1 s more)
  R.pick_form = kFormSeated;
  R.pick_clump = RS_FIRESIDE;
  R.pick_stalk = (uint8_t)k;
  HikerPose base = h.pose;   // as he will be at the grasp: leaning back 0.10
  base.lean -= 0.10f;
  reachFor(w, base, windTip(w, RS_FIRESIDE, k), -2.6f, -0.3f, -0.4f, 1.2f);
  nextStage(R, PK_A);
  return true;
}

// The walk from his feet to a stand point keeps to the camp's dry ground and off the fire pit and the tent.
bool walkClear(const World& w, Vec2 a, Vec2 b) {
  const Vec2 F = w.fire.pos;
  const Vec2 T = w.spine.camp.tent;
  for (int i = 0; i <= 16; ++i) {
    const float u = fl(i) / 16.f;
    const Vec2 p{lerp(a.x, b.x, u), lerp(a.y, b.y, u)};
    float y;
    if (bankY(w.land, p.x, false, y) && p.y > y - 3.f) return false;   // at or below the camp waterline
    if (fabsf(p.x - F.x) < 18.f && fabsf(p.y - F.y) < 18.f) return false;
    if (w.tent.state != TENT_PACKED && fabsf(p.x - T.x) < 40.f && fabsf(p.y - T.y) < 16.f) return false;
  }
  return true;
}

bool tryFormB(World& w) {
  Reeds& R = w.reeds;
  Hiker& h = w.hiker;
  static const int kCamp[3] = {RS_UPSTREAM, RS_FIRESIDE, RS_FAR};
  int best = -1, best_k = -1;
  float best_d = 1.0e9f;
  for (int c : kCamp) {
    const ReedSite& S = R.site[c];
    if (!present(S) || !(S.flags & kCampBank)) continue;
    const int k = reedsNearestStalk(w, c, h.feet, true);
    if (k < 0) continue;
    const int side = h.feet.x < fl(S.x) ? -1 : 1;
    Vec2 st;
    if (!reedsStandPoint(w, c, side, st) || st.x < 24.f || st.x > 776.f) continue;
    const float d = dist(h.feet, st);
    if (d > kStrollReach || d < 2.f || !walkClear(w, h.feet, st)) continue;
    // Its seconds as stepFormB plays them (the plan's 8.05 + distance / 15); storyFree asks 1 s more.
    const float need = 5.95f + (h.target == POSE_SIT ? 1.8f : 0.2f) + fabsf(st.x - h.feet.x) / 15.f;
    if (!storyFree(w, need)) continue;
    if (d < best_d) {
      best_d = d;
      best = c;
      best_k = k;
    }
  }
  if (best < 0) return false;
  R.pick_form = kFormStroll;
  R.pick_clump = (uint8_t)best;
  R.pick_stalk = (uint8_t)best_k;
  R.home_x = (int16_t)floorI(h.feet.x + 0.5f);
  R.home_y = (int16_t)floorI(h.feet.y + 0.5f);
  R.home_face = (int8_t)(h.facing < 0 ? -1 : 1);
  R.home_pose = (uint8_t)h.target;
  R.home_path = w.fx.path_on ? 1 : 0;
  w.fx.path_on = false;
  h.feet = Vec2{fl(R.home_x), fl(R.home_y)};   // he starts from exactly the whole-pixel place he is put back to
  nextStage(R, PK_B_STAND);
  return true;
}

// ---- the small life ----

inline bool warmMonth(uint8_t m) { return m == 0 || (m >= 5 && m <= 9); }

// The dragonfly (CATTAILS_PLAN 3.5): by day, in clear or heat weather, May to September (or no clock), round
// RS_NEAR and RS_FORE; not while the blackbird is on a stalk.
bool flyAllowed(const World& w) {
  const Reeds& R = w.reeds;
  const uint8_t k = w.weather.kind;
  if (!R.day || !(k == WX_CLEAR || k == WX_HEAT) || w.weather.rain > 0.05f || !warmMonth(R.month)) return false;
  if (!buildDone(w.build) || w.minors.cur == MN_REED_BIRD) return false;
  return present(R.site[RS_NEAR]) || present(R.site[RS_FORE]);
}

int flyClump(const Reeds& R, float roll) {
  const bool n = present(R.site[RS_NEAR]), f = present(R.site[RS_FORE]);
  if (n && f) return roll < 0.5f ? RS_NEAR : RS_FORE;
  return n ? RS_NEAR : RS_FORE;
}

// A point beside a head of RS_NEAR or RS_FORE, in rows 384 to 470 and left of x 520 (five draws always).
Vec2 flyHoverPoint(World& w, Vec2 from, bool limit) {
  Reeds& R = w.reeds;
  const float roll = rnd(R, 0.f, 1.f);
  const float side = rnd(R, 0.f, 1.f);
  const float dx = rnd(R, 6.f, 14.f);
  const float dy = rnd(R, -4.f, 4.f);
  const float pick = rnd(R, 0.f, 1.f);
  const int c = flyClump(R, roll);
  const ReedSite& S = R.site[c];
  int k = (int)floorf(pick * fl(S.n_cat));
  if (k > S.n_cat - 1) k = S.n_cat - 1;
  if (k < 0) k = 0;
  ReedStalkInfo si;
  Vec2 p{fl(S.x), fl(S.y) - 20.f};
  if (reedsStalk(w, c, k, si)) p = si.head;
  p = Vec2{p.x + (side < 0.5f ? -dx : dx), p.y + dy};
  if (limit) {   // a dart goes 60 px at most
    const float d = dist(from, p);
    if (d > 60.f) p = Vec2{from.x + (p.x - from.x) * 60.f / d, from.y + (p.y - from.y) * 60.f / d};
  }
  return Vec2{clampf(p.x, 8.f, 518.f), clampf(p.y, 384.f, 470.f)};
}

Vec2 flyFrom(const Reeds& R) { return Vec2{fl(R.fly_from[0]), fl(R.fly_from[1])}; }
Vec2 flyTo(const Reeds& R) { return Vec2{fl(R.fly_to[0]), fl(R.fly_to[1])}; }
void setFly(Reeds& R, uint8_t st, int darts, Vec2 from, Vec2 to, float seconds) {
  R.fly = (uint8_t)(st | (darts << 4));
  R.fly_from[0] = (int16_t)floorI(from.x);
  R.fly_from[1] = (int16_t)floorI(from.y);
  R.fly_to[0] = (int16_t)floorI(to.x);
  R.fly_to[1] = (int16_t)floorI(to.y);
  R.fly_t = 0;
  const uint32_t n = ticksOf(seconds);
  R.fly_len = (uint16_t)(n < 1u ? 1u : n);
}

// Where it is now, in whole pixels, and its wings: 0 or 1 a beat apart in flight, 2 still. False: away.
bool flyPos(const World& w, int& qx, int& qy, int& wing) {
  const Reeds& R = w.reeds;
  const uint8_t st = R.fly & 15u;
  Vec2 p;
  wing = (int)(w.tick & 1u);
  switch (st) {
    case FL_DART:
    case FL_LEAVE: {
      const float u = easeInOut(clamp01(fl(R.fly_t) / fl(R.fly_len)));
      const Vec2 a = flyFrom(R), b = flyTo(R);
      p = Vec2{lerp(a.x, b.x, u), lerp(a.y, b.y, u)};
      break;
    }
    case FL_HOVER: p = Vec2{fl(R.fly_to[0]), fl(R.fly_to[1]) + (sinT(fl((int)(w.tick % 100000u)) * 0.03f) > 0.f ? 1.f : 0.f)}; break;
    case FL_PERCH: {
      const int c = (R.fly_at & 0x7F) >> 3, k = R.fly_at & 7;
      p = reedsTip(w, c, k);
      p.y -= 1.f;
      wing = 2;
      break;
    }
    default: return false;
  }
  qx = floorI(p.x);
  qy = floorI(p.y);
  return true;
}

void flyLeave(World& w) {
  Reeds& R = w.reeds;
  int qx, qy, wing;
  if (!flyPos(w, qx, qy, wing)) return;
  const Vec2 from{fl(qx), fl(qy)};
  const Vec2 to{-16.f, clampf(from.y + 10.f, 400.f, 470.f)};
  setFly(R, FL_LEAVE, 0, from, to, maxf(0.6f, dist(from, to) / 180.f));
}

void stepFly(World& w) {
  Reeds& R = w.reeds;
  const uint8_t st = R.fly & 15u;
  const int darts = R.fly >> 4;
  const bool ok = flyAllowed(w);
  switch (st) {
    case FL_AWAY: {
      if (!ok || w.tick < R.fly_next) return;
      const float y = rnd(R, 410.f, 455.f);
      const float nd = rnd(R, 2.f, 4.99f);
      const Vec2 from{-12.f, y};
      const Vec2 to = flyHoverPoint(w, from, false);
      setFly(R, FL_DART, (int)nd, from, to, clampf(dist(from, to) / 170.f, 0.5f, 3.f));
      R.fly_at = 0;
      return;
    }
    case FL_DART:
      if (!ok) {
        flyLeave(w);
        return;
      }
      if (++R.fly_t < R.fly_len) return;
      if (R.fly_at & kFlyPerchBit) {
        const float s = rnd(R, 4.f, 9.f);
        setFly(R, FL_PERCH, 0, flyTo(R), flyTo(R), s);
      } else {
        const float s = rnd(R, 1.5f, 4.f);
        setFly(R, FL_HOVER, darts, flyTo(R), flyTo(R), s);
      }
      return;
    case FL_HOVER: {
      if (!ok) {
        flyLeave(w);
        return;
      }
      if (++R.fly_t < R.fly_len) return;
      const Vec2 from = flyTo(R);
      if (darts > 0) {
        const Vec2 to = flyHoverPoint(w, from, true);
        setFly(R, FL_DART, darts - 1, from, to, 0.35f);
        return;
      }
      // Two to four darts, then down on a head's tip for a while, wings still.
      const float roll = rnd(R, 0.f, 1.f);
      const int c = flyClump(R, roll);
      int k = 0;
      while (k + 1 < R.site[c].n_cat && stalkGone(R, c, k)) ++k;
      if (stalkGone(R, c, k)) {
        flyLeave(w);
        return;
      }
      setFly(R, FL_DART, 0, from, reedsTip(w, c, k), 0.5f);
      R.fly_at = (uint8_t)(kFlyPerchBit | (c << 3) | k);
      return;
    }
    case FL_PERCH: {
      const int c = (R.fly_at & 0x7F) >> 3, k = R.fly_at & 7;
      if (!ok || stalkGone(R, c, k) || ++R.fly_t >= R.fly_len) {
        flyLeave(w);
        return;
      }
      bendBy(w, c, k, 0.f, 1.f);   // the stalk 1 px down under it
      return;
    }
    case FL_LEAVE:
      if (++R.fly_t < R.fly_len) return;
      {
        const float s = rnd(R, 20.f, 60.f);
        R.fly = FL_AWAY;
        R.fly_next = w.tick + ticksOf(s);
      }
      return;
    default: R.fly = FL_AWAY; return;
  }
}

// Fireflies (CATTAILS_PLAN 3.5, REVIEW_FIXES F11): a pure function of World::tick. Three, over RS_NEAR,
// RS_UPSTREAM and RS_FIRESIDE; at dusk and night, not in rain, snow, chill or storm, May to September or no
// clock. A slow figure 14 px wide and 8 tall round a point above the leaves (4 px over RS_UPSTREAM, never above
// row 356), lit 0.5 s every 3.1 to 4.7 s: in over 4 ticks and out over 8, in two radius steps.
const int kFlyClumps[3] = {RS_NEAR, RS_UPSTREAM, RS_FIRESIDE};

bool ffAllowed(const World& w) {
  const Reeds& R = w.reeds;
  const uint8_t k = w.weather.kind;
  return R.dusk && warmMonth(R.month) && buildDone(w.build) && k != WX_RAIN && k != WX_SNOW && k != WX_CHILL &&
         k != WX_STORM && w.weather.rain <= 0.05f;
}

// Firefly i at tick t: 0 dark, 1 the small dot, 2 the full one; its whole-pixel place.
int ffAt(const World& w, int i, uint32_t t, int& qx, int& qy) {
  const ReedSite& S = w.reeds.site[kFlyClumps[i]];
  if (!present(S)) return 0;
  const uint32_t cyc = ticksOf(3.1f + 1.6f * h01((uint32_t)i, 0xF1F1u));
  const uint32_t off = (uint32_t)(h01((uint32_t)i, 0xF1F2u) * fl((int)cyc));
  const uint32_t in = (t + off) % cyc;
  const int level = in < 4u ? 1 : (in < 13u ? 2 : (in < 21u ? 1 : 0));
  if (!level) return 0;
  const float period = (7.f + 4.f * h01((uint32_t)i, 0xF1F3u)) * kHz;
  const float ph = fl((int)(t % 100000u)) / period + h01((uint32_t)i, 0xF1F4u);
  const float top = fl(S.y) - leafHeight(S, lookFor(w.reeds.month));
  float cy = top - (kFlyClumps[i] == RS_UPSTREAM ? 4.f : 10.f);
  if (cy < 360.f) cy = 360.f;
  qx = floorI(fl(S.x) + 7.f * sinT(ph));
  qy = floorI(cy + 4.f * sinT(2.f * ph));
  return level;
}

// Fluff seeds (CATTAILS_PLAN 3.5): October and November (a rare one in September), up to three, from the
// whole heads of clumps 0 to 3, drifting left low over the camp, rows 352 and below.
bool seedAllowed(const World& w) {
  const uint8_t m = w.reeds.month;
  return (m >= 9 && m <= 11) && buildDone(w.build) && !wetNow(w);
}
inline float seedLife(const ReedSeed& s) { return 3.f + 2.f * h01((uint32_t)(int)s.x0, (uint32_t)(int)s.y0); }
bool seedPos(const ReedSeed& s, uint32_t age, int& qx, int& qy) {
  const float t = secs(age);
  if (t > seedLife(s)) return false;
  const float x = fl(s.x0) - fl(s.speed) * t;
  float y = fl(s.y0) - 3.f * minf(1.f, t) - 6.f * sinT(t / 2.5f) - 1.5f * t;
  if (y < 352.f) y = 352.f;
  if (x < -6.f) return false;
  qx = floorI(x);
  qy = floorI(y);
  return true;
}

void stepSeeds(World& w) {
  Reeds& R = w.reeds;
  for (ReedSeed& s : R.seed) {
    if (s.on != 1) continue;
    ++s.age;
    int qx, qy;
    if (!seedPos(s, s.age, qx, qy)) s.on = 2;   // gone: the tracker clears where it last was
  }
  if (!seedAllowed(w) || w.tick < R.seed_next) return;
  const float wait = w.weather.gust > 0.5f ? rnd(R, 6.f, 14.f) : rnd(R, 25.f, 50.f);
  const float pick = rnd(R, 0.f, 1.f);
  const float speed = rnd(R, 30.f, 60.f);
  R.seed_next = w.tick + ticksOf(wait * (R.month == 9 ? 3.f : 1.f));
  int free_i = -1;
  for (int i = 0; i < 3; ++i)
    if (R.seed[i].on == 0 && free_i < 0) free_i = i;
  if (free_i < 0) return;
  int heads[REED_CLUMPS * 3][2];
  int nh = 0;
  for (int c = RS_FORE; c <= RS_FIRESIDE; ++c) {
    const ReedSite& S = R.site[c];
    if (!present(S)) continue;
    for (int k = 0; k < S.n_cat; ++k)
      if (!stalkGone(R, c, k)) {
        heads[nh][0] = c;
        heads[nh][1] = k;
        ++nh;
      }
  }
  if (nh == 0) return;
  int j = (int)floorf(pick * fl(nh));
  if (j > nh - 1) j = nh - 1;
  ReedStalkInfo si;
  if (!reedsStalk(w, heads[j][0], heads[j][1], si) || si.head.x > 545.f) return;
  ReedSeed& s = R.seed[free_i];
  s.x0 = (int16_t)floorI(si.head.x);
  s.y0 = (int16_t)floorI(maxf(si.head.y, 356.f));
  s.age = 0;
  s.on = 1;
  s.speed = (uint8_t)floorI(speed);
}

// ---- dirty boxes for the small life and the falling stem ----

inline Rect around(int x, int y, int r) { return Rect{x - r, y - r, x + r + 1, y + r + 1}; }
inline void markIn(Renderer& r, const Rect& b) {
  const Rect c = b.clipped(kFrame);
  if (!c.empty()) r.markDirtyRect(c);
}
Rect segBox(Vec2 a, Vec2 b, float pad) {
  return Rect{floorI(minf(a.x, b.x) - pad), floorI(minf(a.y, b.y) - pad), ceilI(maxf(a.x, b.x) + pad),
              ceilI(maxf(a.y, b.y) + pad)};
}

}  // namespace

// ---- the C1 interface ----

bool reedsChewing(const World& w) { return camp(w) && w.reeds.chew != CH_NONE; }

void reedsGiveChew(World& w, float seconds) {
  if (!camp(w) || !w.fx.on) return;
  giveChew(w, seconds > 1.f ? seconds : 1.f);
}

void reedsDropChew(World& w) {
  if (!camp(w)) return;
  dropChew(w);
}

bool reedsPickActive(const World& w) { return camp(w) && w.reeds.pick != PK_NONE; }

void reedsBirdHold(World& w, int clump, float ox, float oy) {
  if (!camp(w) || clump < 0 || clump >= REED_CLUMPS || !present(w.reeds.site[clump])) return;
  bendBy(w, clump, 0, ox, oy);
}

int reedsBirdPerch(const World& w) {
  if (!camp(w) || !buildDone(w.build)) return -1;
  const Reeds& R = w.reeds;
  if (!R.day || wetNow(w) || (R.month != 0 && (R.month < 3 || R.month > 10))) return -1;
  int best = -1;
  float best_d = -1.f;
  static const int kPerch[2] = {RS_NEAR, RS_UPSTREAM};
  for (int c : kPerch) {
    const ReedSite& S = R.site[c];
    if (!present(S) || S.n_cat == 0 || stalkGone(R, c, 0)) continue;
    const float d = w.fx.on && w.hiker.visible ? fabsf(w.hiker.feet.x - fl(S.x)) : 1.0e6f;
    if (d < 130.f) continue;
    if (d > best_d) {
      best_d = d;
      best = c;
    }
  }
  return best;
}

void reedsStoryStep(World& w) {
  if (!camp(w)) return;
  if (w.react.active || w.fi.on || visitorsHolds(w)) return;   // the story is held: so are the pick and the chew
  Reeds& R = w.reeds;
  // The stem's endings (CATTAILS_PLAN 4.4).
  if (R.chew != CH_NONE && R.pick == PK_NONE) {
    if (!w.fx.on) {
      R.chew = CH_NONE;   // he left the scene: it is simply gone
    } else if (mouthBeat(w) || w.hiker.target == POSE_BLOW || w.hiker.target == POSE_YELP || wetNow(w) ||
               storyTick(w) >= R.chew_until + ticksOf(kChewGrace)) {
      dropChew(w);
    } else if (storyTick(w) >= R.chew_until && R.chew == CH_MOUTH && handsFree(w) && storyFree(w, 3.f)) {
      nextStage(R, PK_TOSS);
    }
  }
  switch (R.pick) {
    case PK_A:
    case PK_A_ABORT: stepFormA(w); return;
    case PK_B_STAND:
    case PK_B_WALK:
    case PK_B_LOOK:
    case PK_B_REACH:
    case PK_B_UP:
    case PK_B_HOME:
    case PK_B_SIT: stepFormB(w); return;
    case PK_TOSS: stepToss(w); return;
    default: break;
  }
  // A new pick? (the gate of CATTAILS_PLAN 8)
  if (!kStoryLive || !R.want_pick || R.chew != CH_NONE || storyTick(w) < R.pick_after) return;
  if (wetNow(w) || !handsFree(w) || !isPauseW(w) || !buildDone(w.build)) return;
  if (!tryFormA(w)) tryFormB(w);
}

void reedsApplyPose(const World& w, HikerPose& P) {
  if (!camp(w) || !w.fx.on) return;
  Key o;
  if (!overlayOf(w.reeds, o)) return;
  if (o.w > 0.f) {
    P.shoulder[0] = lerp(P.shoulder[0], o.sh, o.w);
    P.elbow[0] = lerp(P.elbow[0], o.el, o.w);
  }
  P.head += o.head;
  P.lean += o.lean;
}

void reedsDrawMouth(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!camp(w)) return;
  StemGeom g;
  if (!stemGeom(w, g)) return;
  const Cols C = colsFor(pal, lookFor(w.reeds.month), false, frosty(w));
  drawStem(g, C.stem, C.plume, s, clip);
}

Rect reedsMouthBounds(const World& w) {
  if (!camp(w) || !w.fx.on || w.reeds.chew == CH_NONE) return kNone;
  const bool hand = w.reeds.chew == CH_HAND;
  const Vec2 p = hand ? hikerHandPos(w.hiker, 0) : hikerMouthPos(w.hiker);
  const float r = hand ? kHandBox : kMouthBox;
  return Rect{floorI(p.x - r), floorI(p.y - r), ceilI(p.x + r), ceilI(p.y + r)};
}

void reedsDrawAir(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!camp(w)) return;
  const Reeds& R = w.reeds;
  const Look L = lookFor(R.month);
  // Fluff seeds: a pale dot and a wisp.
  for (const ReedSeed& sd : R.seed) {
    int qx, qy;
    if (sd.on != 1 || !seedPos(sd, sd.age, qx, qy)) continue;
    if (qy + 4 < clip.y0 || qy - 4 >= clip.y1) continue;
    const Pixel fluff = colsFor(pal, L, false, false).fluff;
    const Vec2 p{fl(qx) + 0.5f, fl(qy) + 0.5f};
    fillCircle(s, p, 1.2f, fluff, clip);
    strokeCapsule(s, p, Vec2{p.x + 2.2f, p.y - 0.8f}, 0.8f, fluff, clip);
  }
  // The dragonfly: the stream's mockup at 0.65 size, about 9 px: body, head, two wing pairs.
  int qx, qy, wing;
  if (flyPos(w, qx, qy, wing) && qy + 8 >= clip.y0 && qy - 8 < clip.y1) {
    const uint8_t st = R.fly & 15u;
    const float hd = st == FL_PERCH || st == FL_HOVER ? (R.fly_from[0] <= 260 ? 1.f : -1.f)
                                                      : (R.fly_to[0] >= R.fly_from[0] ? 1.f : -1.f);
    const Vec2 p{fl(qx) + 0.5f, fl(qy) + 0.5f};
    const Pixel body = mixRGB(pal[FOLIAGE_DARK], pal[WATER], 0.35f);   // dark blue-green: reads on water and bank
    const Pixel wingc = mixRGB(pal[CLOUD_LIT], pal[WATER_LIGHT], 0.5f);
    const float up = wing == 0 ? -3.6f : (wing == 1 ? -1.2f : -2.4f);
    const float rear = wing == 2 ? 3.2f : 2.4f;
    const Vec2 w1[4] = {{p.x, p.y - 0.4f}, {p.x - hd * rear, p.y + up - 0.6f}, {p.x - hd * (rear + 1.2f), p.y + up},
                        {p.x - hd * 0.6f, p.y + 0.3f}};
    const Vec2 w2[4] = {{p.x + hd * 0.4f, p.y - 0.4f}, {p.x + hd * rear, p.y + up},
                        {p.x + hd * (rear + 1.0f), p.y + up + 0.6f}, {p.x + hd * 0.8f, p.y + 0.3f}};
    fillPolygonAlpha(s, w1, 4, wingc, 150, clip);
    fillPolygonAlpha(s, w2, 4, wingc, 150, clip);
    strokeCapsule(s, Vec2{p.x - hd * 1.2f, p.y}, Vec2{p.x - hd * 5.6f, p.y + 0.4f}, 1.0f, body, clip);
    fillCircle(s, p, 1.25f, body, clip);
    fillCircle(s, Vec2{p.x + hd * 1.7f, p.y - 0.2f}, 0.95f, body, clip);
  }
  // Fireflies.
  if (ffAllowed(w)) {
    const Pixel glow = mixRGB(pal[STAR], pal[FIRE_CORE], 0.35f);
    for (int i = 0; i < 3; ++i) {
      int fx, fy;
      const int lv = ffAt(w, i, w.tick, fx, fy);
      if (!lv) continue;
      const Vec2 c{fl(fx) + 0.5f, fl(fy) + 0.5f};
      if (lv == 2) {   // a faint glow round the full dot, so it reads as a light and not a speck
        Vec2 o[8];
        for (int j = 0; j < 8; ++j) o[j] = Vec2{c.x + 3.4f * cosT(fl(j) / 8.f), c.y + 3.4f * sinT(fl(j) / 8.f)};
        fillPolygonAlpha(s, o, 8, glow, 70, clip);
      }
      fillCircle(s, c, lv == 2 ? 1.3f : 0.6f, glow, clip);
    }
  }
  // A dropped or tossed stem.
  Vec2 fa, fb;
  if (fallGeom(R, R.fall_t, fa, fb)) {
    const Cols C = colsFor(pal, L, false, frosty(w));
    strokeCapsule(s, fa, fb, kStemW, C.stem, clip);
  }
}

namespace {

void c1Init(World& w) {
  Reeds& R = w.reeds;
  // A fixed number of draws whatever is decided (CATTAILS_PLAN 8).
  const float want = rnd(R, 0.f, 1.f);
  const float after = rnd(R, kPickAfterMin, kPickAfterMax);
  const float fly0 = rnd(R, 8.f, 40.f);
  const float seed0 = rnd(R, 4.f, 20.f);
  R.want_pick = want < kPickChance ? 1 : 0;
  R.pick_after = storyTick(w) + ticksOf(after);
  R.fly_next = w.tick + ticksOf(fly0);
  R.seed_next = w.tick + ticksOf(seed0);
  R.d_fly[0] = R.d_fly[1] = kNoPos;
  R.d_fall[0] = R.d_fall[1] = kNoPos;
}

void c1Ambient(World& w) {
  Reeds& R = w.reeds;
  stepFly(w);
  stepSeeds(w);
  if (R.fall_kind != FALL_NONE) {
    if (R.fall_t == kFallTicks && R.fall_kind != FALL_DROP) {   // it comes down: on the water it rides away
      Vec2 a, b;
      if (fallGeom(R, kFallTicks, a, b)) {
        float cy, half;
        const Vec2 m{0.5f * (a.x + b.x), 0.5f * (a.y + b.y)};
        if (landscapeStreamSpan(w.land, m.x, cy, half) && fabsf(m.y - cy) < half - 2.f) streamAddStick(w, m);
      }
    }
    if (++R.fall_t > kFallTicks) R.fall_kind = FALL_NONE;
  }
}

void c1Track(World& w, Renderer& r) {
  Reeds& R = w.reeds;
  // The dragonfly: its old and new boxes whenever it moved or its wings beat.
  int qx, qy, wing;
  const bool on = flyPos(w, qx, qy, wing);
  const bool was = R.d_fly[0] != kNoPos;
  if (on != was || (on && (qx != R.d_fly[0] || qy != R.d_fly[1] || wing != 2))) {
    if (was) markIn(r, around(R.d_fly[0], R.d_fly[1], 9));
    if (on) markIn(r, around(qx, qy, 9));
  }
  R.d_fly[0] = on ? (int16_t)qx : kNoPos;
  R.d_fly[1] = on ? (int16_t)qy : kNoPos;
  // Fireflies: a pure function of the tick, so last tick's dot is recomputed (marked only on a change).
  const bool ff_now = ffAllowed(w);
  for (int i = 0; i < 3; ++i) {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const int l1 = ff_now ? ffAt(w, i, w.tick, x1, y1) : 0;
    const int l0 = ff_now && w.tick > 0 ? ffAt(w, i, w.tick - 1u, x0, y0) : 0;
    if (l0 == l1 && (l1 == 0 || (x0 == x1 && y0 == y1))) continue;
    if (l0) markIn(r, around(x0, y0, 5));
    if (l1) markIn(r, around(x1, y1, 5));
  }
  if (ff_now != (R.d_ff != 0)) {   // fireflies switched on or off (dusk came, rain began): every dot's place
    for (int i = 0; i < 3; ++i) {
      const ReedSite& S = R.site[kFlyClumps[i]];
      if (present(S)) markIn(r, Rect{S.x - 12, S.y - 60, S.x + 13, S.y + 4});
    }
  }
  R.d_ff = ff_now ? 1 : 0;
  // Seeds: where each was last tick and where it is now.
  for (ReedSeed& s : R.seed) {
    if (s.on == 0) continue;
    int x0, y0, x1, y1;
    if (s.age > 0 && seedPos(s, s.age - 1u, x0, y0)) markIn(r, around(x0, y0, 4));
    if (s.on == 1 && seedPos(s, s.age, x1, y1)) markIn(r, around(x1, y1, 4));
    if (s.on == 2) s.on = 0;
  }
  // The falling stem: its last box, and its box now.
  if (R.d_fall[0] != kNoPos) markIn(r, around(R.d_fall[0], R.d_fall[1], 10));
  Vec2 fa, fb;
  if (fallGeom(R, R.fall_t, fa, fb)) {
    const Rect b = segBox(fa, fb, 2.f);
    markIn(r, b);
    R.d_fall[0] = (int16_t)((b.x0 + b.x1) / 2);
    R.d_fall[1] = (int16_t)((b.y0 + b.y1) / 2);
  } else {
    R.d_fall[0] = R.d_fall[1] = kNoPos;
  }
}

}  // namespace

}  // namespace ss
