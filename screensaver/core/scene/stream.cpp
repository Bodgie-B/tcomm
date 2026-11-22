// Life in the stream. See stream.h.
//
// Geometry works in the stream's own frame. At a point along it, T is the way the water flows
// (downstream: left and a little down on screen) and D the way across it toward the near bank (the
// viewer, down on screen). The water is seen from low down, so a length across the current shows at
// kFore of a length along it. Shapes are flat palette-slot mixes; nothing reads the clock.
#include "stream.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../gfx/raster.h"
#include "director.h"
#include "world.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }

const Rect kEmpty{0, 0, 0, 0};

// ---- the look (change these to change it) ---------------------------------------------------

constexpr float kFore = 0.5f;               // across the current shows this much of along it
constexpr float kFootDepth = 6.f;           // the boulder's foot, half its depth across the current, px
constexpr float kPartUp = 36.f;             // the water starts to part this far upstream of its middle, px
constexpr float kPartDown = 52.f;           // and has closed again this far downstream
constexpr float kFlowPxPerSec = 26.f;       // as ambient.cpp: the glints' speed at a 20 px half width
constexpr float kFloatSpeed = 0.8f;         // floaters ride a little slower than the glints
constexpr float kFloatGapMin = 18.f, kFloatGapMax = 42.f;   // seconds between floaters (mean 30)
constexpr float kRainGap = 0.45f;           // in rain the gaps shrink to this
constexpr float kLaneMin = -0.25f, kLaneMax = 0.55f;          // floaters keep off the near bank (the chip's corner)
constexpr float kFishGapMin = 20.f, kFishGapMax = 60.f;     // seconds between fish events (mean 40)
constexpr float kTwilightGap = 0.5f;        // at dawn and dusk the gaps halve
constexpr float kJumpChance = 0.22f;        // a fish event is a jump (else a rise ring): a jump every ~3 min
constexpr float kJumpS = 0.95f;             // the jump's flight, seconds
constexpr float kJumpLen = 30.f, kJumpH = 22.f;   // its reach along the water and its height, px
constexpr float kFishLen = 12.f;            // the fish, nose to tail fork, px
constexpr float kSplashS = 0.5f;            // the splash as it goes back in
constexpr float kRingS = 2.4f;              // a ring's life
constexpr float kFishXMin = 80.f, kFishXMax = 500.f;    // where fish come up (clear of the chip's corner)
constexpr float kFishLaneMin = -0.32f, kFishLaneMax = -0.09f;   // our side of the middle: the arc stays over water
constexpr float kSwingS = 1.6f;             // a snagged floater swinging free
constexpr int kFoamStepTicks = 4;           // the foam and the wake move in steps of this many ticks
constexpr float kWakeSpread = 0.30f;        // the wake's arms open across the current this much per px
constexpr float kWakeGap = 11.f;            // px between its ripples

// ---- the stream's frame ----------------------------------------------------------------------

float segLen(const Landscape& ls, float s) {
  int i = (int)s;
  if (i < 0) i = 0;
  if (i > LS_STREAM_PTS - 2) i = LS_STREAM_PTS - 2;
  const LsStreamPt& a = ls.stream[i];
  const LsStreamPt& b = ls.stream[i + 1];
  return maxf(1.f, sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)));
}

struct Frame {
  Vec2 p;    // the point
  Vec2 t;    // downstream unit vector
  Vec2 d;    // across toward the near bank (down on screen), unit
  float hw;  // the half width there
};

Frame frameAt(const Landscape& ls, float s, float lane) {
  const LsStreamPt q = landscapeStreamAt(ls, s);
  const LsStreamPt q2 = landscapeStreamAt(ls, s + 0.05f);
  float tx = q2.x - q.x, ty = q2.y - q.y;
  float tl = sqrtf(tx * tx + ty * ty);
  if (tl < 1e-4f) tl = 1.f;
  Frame f;
  f.t = Vec2{tx / tl, ty / tl};
  f.d = Vec2{-q.nx, -q.ny};
  f.hw = q.hw;
  const float off = lane * q.hw;
  f.p = Vec2{q.x + q.nx * off, q.y + q.ny * off};
  return f;
}

inline Vec2 at(const Vec2& o, const Vec2& t, const Vec2& d, float u, float v) {
  return Vec2{o.x + t.x * u + d.x * v, o.y + t.y * u + d.y * v};
}

struct Ext {
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  void pt(Vec2 p, float r = 0.f) {
    x0 = minf(x0, p.x - r);
    y0 = minf(y0, p.y - r);
    x1 = maxf(x1, p.x + r);
    y1 = maxf(y1, p.y + r);
  }
  Rect rect(float pad = 2.f) const {
    if (x1 < x0) return kEmpty;
    return Rect{(int)floorf(x0 - pad), (int)floorf(y0 - pad), (int)ceilf(x1 + pad) + 1, (int)ceilf(y1 + pad) + 1};
  }
};

inline void unite(Rect& r, const Rect& b) {
  if (b.empty()) return;
  if (r.empty()) {
    r = b;
    return;
  }
  r = Rect{r.x0 < b.x0 ? r.x0 : b.x0, r.y0 < b.y0 ? r.y0 : b.y0, r.x1 > b.x1 ? r.x1 : b.x1, r.y1 > b.y1 ? r.y1 : b.y1};
}

inline uint32_t mix32(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return h;
}

// The boulder's own frame: its foot's middle and the flow there.
Frame boulderFrame(const Landscape& ls) {
  float bs;
  const Vec2 o = landscapeBoulder(ls, &bs);
  Frame f = frameAt(ls, bs, LS_BOULDER_LANE);
  f.p = o;
  return f;
}

// ---- the boulder: outline and shading, px from the middle of its foot (x right, y down) ----------

// Flat facets, as the rest of the scene is drawn: a weathered block, its top tilted to the light
// (from the right, as on the camp pine), a sunlit right face, a shaded front and a dark left edge,
// moss on the top's shady end, a crack, wet and dark where the water meets it. Units: LS_BOULDER_R
// is 16 across, LS_BOULDER_H is 18 up.
const float kRock[16][2] = {{-16.0f, 1.0f},  {-16.6f, -3.0f}, {-15.4f, -8.0f}, {-12.6f, -12.6f}, {-7.0f, -16.4f},
                            {0.5f, -18.0f},  {6.8f, -17.6f},  {11.2f, -15.2f}, {14.0f, -11.2f},  {13.4f, -9.6f},
                            {16.0f, -6.6f},  {16.9f, -2.2f},  {16.3f, 1.4f},   {9.0f, 2.6f},     {0.0f, 2.8f},
                            {-9.0f, 2.4f}};
const float kTop[8][2] = {{-12.6f, -12.6f}, {-7.0f, -16.4f}, {0.5f, -18.0f}, {6.8f, -17.6f},
                          {11.2f, -15.2f},  {8.0f, -12.4f},  {1.5f, -11.4f}, {-5.0f, -11.6f}};
const float kRight[9][2] = {{11.2f, -15.2f}, {14.0f, -11.2f}, {13.4f, -9.6f}, {16.0f, -6.6f}, {16.9f, -2.2f},
                            {16.3f, 1.4f},   {9.0f, 2.6f},    {8.6f, -4.5f},  {8.0f, -12.4f}};
const float kLeft[7][2] = {{-16.0f, 1.0f}, {-16.6f, -3.0f}, {-15.4f, -8.0f}, {-12.6f, -12.6f},
                           {-10.6f, -9.0f}, {-11.4f, -3.4f}, {-9.6f, 2.4f}};
const float kMoss[9][2] = {{-12.6f, -12.6f}, {-7.0f, -16.4f}, {-1.6f, -17.6f}, {-2.6f, -15.4f}, {-5.4f, -14.2f},
                           {-7.4f, -12.0f},  {-9.8f, -11.2f}, {-11.0f, -8.4f}, {-12.2f, -10.4f}};
const float kWet[8][2] = {{-16.4f, -1.6f}, {-8.0f, -1.0f}, {0.0f, -0.8f}, {8.6f, -1.0f},
                          {16.8f, -1.4f},  {16.3f, 1.4f},  {0.0f, 2.8f},  {-16.0f, 1.0f}};

struct RockCols {
  Pixel front, left, top, right, moss, moss_lt, wet, lap, crack;
};

RockCols rockCols(const Palette& pal) {
  RockCols c;
  c.right = mixRGB(pal[ROCK], pal[CLOUD_LIT], 0.2f);
  c.top = mixRGB(pal[ROCK], pal[CLOUD_LIT], 0.55f);
  c.front = mixRGB(pal[ROCK], pal[ACTOR], 0.12f);
  c.left = mixRGB(pal[ROCK], pal[ACTOR], 0.4f);
  c.moss = mixRGB(pal[GRASS], pal[FOLIAGE_DARK], 0.2f);
  c.moss_lt = mixRGB(pal[GRASS], pal[CLOUD_LIT], 0.2f);
  c.wet = mixRGB(mixRGB(pal[ROCK], pal[ACTOR], 0.42f), pal[WATER], 0.2f);
  c.lap = mixRGB(pal[WATER], pal[WATER_LIGHT], 0.6f);
  c.crack = mixRGB(pal[ROCK], pal[ACTOR], 0.3f);
  return c;
}

inline Vec2 rp(Vec2 o, float sc, float x, float y) {
  return Vec2{o.x + x * sc * fl(LS_BOULDER_R) / 16.f, o.y + y * sc * fl(LS_BOULDER_H) / 18.f};
}

template <int N>
void poly(Surface& s, const float (&pts)[N][2], Vec2 o, float sc, Pixel col, const Rect& clip) {
  Vec2 p[N];
  for (int i = 0; i < N; ++i) p[i] = rp(o, sc, pts[i][0], pts[i][1]);
  fillPolygon(s, p, N, col, clip);
}

void drawBoulder(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, float sc) {
  const Vec2 o = landscapeBoulder(ls);
  const RockCols c = rockCols(pal);
  poly(s, kRock, o, sc, c.front, clip);
  poly(s, kRight, o, sc, c.right, clip);
  poly(s, kLeft, o, sc, c.left, clip);
  poly(s, kTop, o, sc, c.top, clip);
  poly(s, kMoss, o, sc, c.moss, clip);
  fillEllipse(s, rp(o, sc, -13.4f, -4.6f), 2.0f * sc, 1.4f * sc, c.moss, clip);   // a cushion low on the shady side
  fillCircle(s, rp(o, sc, -7.2f, -14.6f), 0.9f * sc, c.moss_lt, clip);
  fillCircle(s, rp(o, sc, -3.8f, -16.2f), 0.8f * sc, c.moss_lt, clip);
  // Weathering: a crack down the front and a chip off the top's edge.
  strokeCapsule(s, rp(o, sc, -1.0f, -11.4f), rp(o, sc, -2.6f, -6.4f), 0.8f * sc, c.crack, clip);
  strokeCapsule(s, rp(o, sc, 8.0f, -12.4f), rp(o, sc, 8.6f, -4.5f), 0.8f * sc, mixRGB(c.front, c.right, 0.5f), clip);
  // Wet and dark at the waterline, the water lapping its foot.
  poly(s, kWet, o, sc, c.wet, clip);
  const Vec2 l0 = rp(o, sc, -15.4f, 1.6f), l1 = rp(o, sc, -6.0f, 2.9f), l2 = rp(o, sc, 6.0f, 2.9f), l3 = rp(o, sc, 15.6f, 1.8f);
  strokeCapsule(s, l0, l1, 1.1f, c.lap, clip);
  strokeCapsule(s, l1, l2, 1.1f, c.lap, clip);
  strokeCapsule(s, l2, l3, 1.1f, c.lap, clip);
}

// The three small stones by the near bank, upstream of the boulder: x past the boulder, lane, radius.
const float kStones[3][3] = {{30.f, -0.62f, 3.6f}, {46.f, -0.95f, 4.8f}, {60.f, -1.06f, 3.2f}};

void stoneAt(const Landscape& ls, int k, Vec2& c, float& r) {
  float cy, half;
  const Vec2 o = landscapeBoulder(ls);
  const float x = o.x + kStones[k][0];
  if (!landscapeStreamSpan(ls, x, cy, half)) cy = o.y, half = 24.f;
  c = Vec2{floorf(x + 0.5f), floorf(cy - kStones[k][1] * half / 1.06f + 0.5f)};
  r = kStones[k][2];
}

void drawStones(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, float sc) {
  const RockCols c = rockCols(pal);
  for (int k = 0; k < 3; ++k) {
    Vec2 p;
    float r;
    stoneAt(ls, k, p, r);
    r *= sc;
    if (r < 0.3f) continue;
    const Vec2 body[7] = {{p.x - r * 1.25f, p.y + 0.6f}, {p.x - r * 1.1f, p.y - r * 0.55f}, {p.x - r * 0.45f, p.y - r * 1.0f},
                          {p.x + r * 0.35f, p.y - r * 1.05f}, {p.x + r * 1.05f, p.y - r * 0.6f}, {p.x + r * 1.3f, p.y + 0.6f},
                          {p.x, p.y + 1.2f}};
    fillPolygon(s, body, 7, c.front, clip);
    const Vec2 lit[4] = {{p.x - r * 0.1f, p.y - r * 0.95f}, {p.x + r * 0.35f, p.y - r * 1.05f},
                         {p.x + r * 1.05f, p.y - r * 0.6f}, {p.x + r * 0.5f, p.y - r * 0.35f}};
    fillPolygon(s, lit, 4, c.top, clip);
    if (k == 1) fillEllipse(s, Vec2{p.x - r * 0.6f, p.y - r * 0.7f}, r * 0.5f, r * 0.28f, c.moss, clip);
    strokeCapsule(s, Vec2{p.x - r * 1.15f, p.y + 0.8f}, Vec2{p.x + r * 1.2f, p.y + 0.8f}, 1.0f, c.wet, clip);
    if (k == 0) strokeCapsule(s, Vec2{p.x + r * 1.3f, p.y + 1.6f}, Vec2{p.x + r * 2.3f, p.y + 0.2f}, 1.0f, c.lap, clip);
  }
}

// ---- floaters ---------------------------------------------------------------------------------

// Half its reach along itself, px.
inline float floaterReach(const Floater& f) { return f.kind == FK_CONE ? 3.8f : 0.5f * f.len; }

// Where it is drawn: centre and heading (screen vectors along it and across it, unit plan lengths).
struct FloatPose {
  Vec2 c, h, p;
};

FloatPose floatPose(const World& w, const Floater& f) {
  const Landscape& ls = w.land;
  float lane = f.lane;
  if (f.phase == FP_FREE || f.hold <= 0.f) lane = streamPartLane(ls, f.s, f.lane, 1.f + kFore * floaterReach(f));
  const Frame fr = frameAt(ls, f.s, lane);
  FloatPose P;
  float ang = f.ang;
  if (f.phase == FP_HELD) ang += 0.012f * sinT(f.t * 1.7f);   // quivers in the push of the water
  const float ca = cosT(ang), sa = sinT(ang);
  P.h = Vec2{fr.t.x * ca + fr.d.x * sa * kFore, fr.t.y * ca + fr.d.y * sa * kFore};
  P.p = Vec2{-fr.t.x * sa + fr.d.x * ca * kFore, -fr.t.y * sa + fr.d.y * ca * kFore};
  // Bobbing: from where it came in while drifting; from still (0) after it swings free of a snag.
  const float bob = f.phase == FP_HELD ? 0.f
                    : 0.55f * sinT(f.t * 0.37f + (f.phase == FP_DRIFT ? 0.25f * fl(f.tint) : 0.f));
  P.c = Vec2{fr.p.x, fr.p.y + bob};
  return P;
}

Rect floaterRect(const World& w, const Floater& f) {
  if (f.kind == FK_NONE) return kEmpty;
  const FloatPose P = floatPose(w, f);
  const float r = floaterReach(f) + 2.5f;
  Ext e;
  e.pt(P.c, r);
  Rect b = e.rect(1.f);
  if (b.x0 >= SS_WIDTH || b.x1 <= 0) return kEmpty;
  return b;
}

Pixel leafCol(const Palette& pal, int tint) {
  switch (tint) {
    case 4: return mixRGB(mixRGB(pal[TRUNK], pal[GROUND], 0.4f), pal[ROCK], 0.15f);     // a winter brown
    case 0: return mixRGB(pal[FIRE], pal[TRUNK], 0.15f);                                // orange
    case 1: return mixRGB(pal[FIRE], pal[TRUNK], 0.45f);                                // russet
    case 2: return mixRGB(mixRGB(pal[FIRE], pal[FIRE_CORE], 0.45f), pal[GROUND], 0.15f); // gold
    default: return mixRGB(pal[GRASS], pal[FIRE_CORE], 0.32f);                          // still green
  }
}

void drawFloat(const World& w, const Floater& f, Surface& s, const Rect& clip, const Palette& pal) {
  if (f.kind == FK_NONE || !overlaps(floaterRect(w, f), clip)) return;
  const FloatPose P = floatPose(w, f);
  const Vec2 c = P.c, h = P.h, q = P.p;
  const Pixel under = mixRGB(pal[WATER], pal[ACTOR], 0.35f);
  if (f.kind == FK_LEAF) {
    const float L = f.len, W = 0.52f * f.len;
    Vec2 pts[6] = {at(c, h, q, 0.5f * L, 0.f),         at(c, h, q, 0.12f * L, 0.5f * W),
                   at(c, h, q, -0.28f * L, 0.42f * W), at(c, h, q, -0.5f * L, 0.f),
                   at(c, h, q, -0.28f * L, -0.42f * W), at(c, h, q, 0.12f * L, -0.5f * W)};
    Vec2 sh[6];
    for (int i = 0; i < 6; ++i) sh[i] = Vec2{pts[i].x + 0.4f, pts[i].y + 0.9f};
    fillPolygon(s, sh, 6, under, clip);
    const Pixel col = leafCol(pal, f.tint);
    fillPolygon(s, pts, 6, col, clip);
    strokeCapsule(s, at(c, h, q, -0.5f * L, 0.f), at(c, h, q, -0.66f * L, 0.f), 0.9f, mixRGB(col, pal[TRUNK], 0.6f), clip);
  } else if (f.kind == FK_STICK) {
    const float L = f.len;
    const Vec2 a = at(c, h, q, -0.5f * L, 0.f), b = at(c, h, q, 0.5f * L, 0.f);
    strokeCapsule(s, Vec2{a.x + 0.3f, a.y + 1.0f}, Vec2{b.x + 0.3f, b.y + 1.0f}, 2.0f, under, clip);
    const Pixel wood = mixRGB(pal[TRUNK], pal[ROCK], 0.42f);
    strokeCapsule(s, a, b, 2.6f, wood, clip);
    if (f.tint & 1) strokeCapsule(s, at(c, h, q, 0.12f * L, 0.f), at(c, h, q, 0.32f * L, 0.22f * L), 1.4f, wood, clip);
    strokeCapsule(s, at(c, h, q, -0.3f * L, -0.4f), at(c, h, q, 0.25f * L, -0.4f), 0.8f, mixRGB(wood, pal[ROCK], 0.55f), clip);
  } else {   // a pine cone, scales across it
    Vec2 pts[8];
    for (int i = 0; i < 8; ++i) pts[i] = at(c, h, q, 3.6f * cosT(fl(i) / 8.f), 2.5f * sinT(fl(i) / 8.f));
    Vec2 sh[8];
    for (int i = 0; i < 8; ++i) sh[i] = Vec2{pts[i].x + 0.4f, pts[i].y + 0.9f};
    fillPolygon(s, sh, 8, under, clip);
    const Pixel cone = mixRGB(pal[TRUNK], pal[ROCK], 0.32f);
    fillPolygon(s, pts, 8, cone, clip);
    const Pixel dk = mixRGB(cone, pal[ACTOR], 0.35f);
    for (int k = -1; k <= 1; ++k)
      strokeCapsule(s, at(c, h, q, 1.1f * fl(k), -1.6f), at(c, h, q, 1.1f * fl(k) + 0.6f, 1.6f), 0.7f, dk, clip);
  }
}

// ---- fish -------------------------------------------------------------------------------------

struct Ring {
  Vec2 c;
  float r, th;   // radius and rim thickness; r <= 0: none
  float k;       // how bright the rim, 0..1
};

// A ring born at `born`, `life` long, growing to `rmax`.
Ring ringAt(Vec2 c, float t, float born, float life, float rmax) {
  Ring g{c, 0.f, 0.f, 0.f};
  const float age = t - born;
  if (age < 0.f || age >= life) return g;
  const float u = age / life;
  g.r = 2.f + (rmax - 2.f) * easeOutCubic(u);
  g.th = 0.6f + 1.4f * (1.f - u);
  g.k = 0.85f * (1.f - u) + 0.15f;
  return g;
}

int fishRings(const StreamFish& f, Ring out[3]) {
  const Vec2 o{(float)f.x, (float)f.y};
  if (f.kind == FISH_JUMP) {
    const Vec2 land{(float)f.x - (float)f.dir * kJumpLen, (float)f.y};
    out[0] = ringAt(o, f.t, 0.f, 1.6f, 10.f);
    out[1] = ringAt(land, f.t, kJumpS, kRingS, 19.f);
    out[2] = ringAt(land, f.t, kJumpS + 0.45f, kRingS - 0.4f, 11.f);
    return 3;
  }
  if (f.kind == FISH_RISE) {
    out[0] = ringAt(o, f.t, 0.15f, kRingS + 0.2f, 12.f);
    out[1] = ringAt(o, f.t, 0.75f, kRingS - 0.3f, 7.f);
    return 2;
  }
  return 0;
}

inline float fishEnd(const StreamFish& f) { return f.kind == FISH_JUMP ? kJumpS + 0.45f + kRingS - 0.4f : 0.75f + kRingS; }

// The jumping fish in the air: where its middle is, which way it points (unit, nose first) and which
// way its belly faces (unit, the down side of the arc).
bool fishPose(const StreamFish& f, Vec2& c, Vec2& u, Vec2& n) {
  if (f.kind != FISH_JUMP || f.t >= kJumpS) return false;
  const float k = f.t / kJumpS;
  const float dx = -(float)f.dir;
  c = Vec2{(float)f.x + dx * kJumpLen * k, (float)f.y - kJumpH * 4.f * k * (1.f - k)};
  const float tx = dx * kJumpLen, ty = -kJumpH * 4.f * (1.f - 2.f * k);
  const float tl = sqrtf(tx * tx + ty * ty);
  u = Vec2{tx / tl, ty / tl};
  n = Vec2{-u.y, u.x};
  if (n.y < 0.f) n = Vec2{-n.x, -n.y};
  return true;
}

// The fish in its own frame (x along it, nose +, y toward its belly), in kFishLen units.
const float kFishBody[10][2] = {{0.5f, 0.02f},    {0.34f, 0.2f},   {0.08f, 0.26f}, {-0.2f, 0.19f}, {-0.34f, 0.08f},
                                {-0.34f, -0.08f}, {-0.2f, -0.19f}, {0.08f, -0.26f}, {0.34f, -0.21f}, {0.47f, -0.09f}};
const float kFishBelly[5][2] = {{0.36f, 0.12f}, {0.08f, 0.24f}, {-0.22f, 0.17f}, {-0.18f, 0.11f}, {0.1f, 0.15f}};
const float kFishTail[4][2] = {{-0.3f, 0.f}, {-0.56f, 0.3f}, {-0.46f, 0.f}, {-0.56f, -0.3f}};

inline Vec2 fishPt(const Vec2& c, const Vec2& u, const Vec2& n, float x, float y) {
  return Vec2{c.x + (u.x * x + n.x * y) * kFishLen, c.y + (u.y * x + n.y * y) * kFishLen};
}

// The splash's drop i of 5 as it goes back in; false once they have fallen back.
bool splashDrop(const StreamFish& f, int i, Vec2& p) {
  if (f.kind != FISH_JUMP || f.t < kJumpS || f.t >= kJumpS + kSplashS) return false;
  const float k = (f.t - kJumpS) / kSplashS;
  static const float kOut[5] = {-1.f, -0.5f, 0.f, 0.5f, 1.f};
  static const float kUp[5] = {6.f, 9.f, 11.f, 9.f, 6.f};
  const float lx = (float)f.x - (float)f.dir * kJumpLen;
  p = Vec2{lx + kOut[i] * (2.f + 6.f * k), (float)f.y - kUp[i] * 4.f * k * (1.f - k) + 0.5f};
  return true;
}

Rect fishRect(const StreamFish& f) {
  if (f.kind == FISH_NONE) return kEmpty;
  Ext e;
  Ring g[3];
  const int n = fishRings(f, g);
  for (int i = 0; i < n; ++i)
    if (g[i].r > 0.f) {
      e.pt(Vec2{g[i].c.x - g[i].r, g[i].c.y - 0.34f * g[i].r});
      e.pt(Vec2{g[i].c.x + g[i].r, g[i].c.y + 0.34f * g[i].r});
    }
  Vec2 c, u, nb;
  if (fishPose(f, c, u, nb)) {
    for (const auto& q : kFishBody) e.pt(fishPt(c, u, nb, q[0], q[1]), 1.f);
    for (const auto& q : kFishTail) e.pt(fishPt(c, u, nb, q[0], q[1]), 1.f);
  }
  if (f.kind == FISH_RISE && f.t < 0.35f) e.pt(Vec2{(float)f.x, (float)f.y}, 2.4f);
  if (f.kind == FISH_JUMP && f.t < 0.3f) e.pt(Vec2{(float)f.x, (float)f.y - 4.f}, 3.f);
  for (int i = 0; i < 5; ++i) {
    Vec2 p;
    if (splashDrop(f, i, p)) e.pt(p, 1.2f);
  }
  if (f.kind == FISH_JUMP && f.t >= kJumpS && f.t < kJumpS + kSplashS)
    e.pt(Vec2{(float)f.x - (float)f.dir * kJumpLen, (float)f.y - 1.f}, 5.f);
  return e.rect(2.f);
}

void drawFish(const StreamFish& f, Surface& s, const Rect& clip, const Palette& pal) {
  if (f.kind == FISH_NONE || !overlaps(fishRect(f), clip)) return;
  Ring g[3];
  const int n = fishRings(f, g);
  for (int i = 0; i < n; ++i) {
    if (g[i].r <= 0.f) continue;
    fillEllipse(s, g[i].c, g[i].r, 0.34f * g[i].r, mixRGB(pal[WATER], pal[WATER_LIGHT], g[i].k), clip);
    const float ri = g[i].r - g[i].th, ryi = 0.34f * g[i].r - 0.6f * g[i].th;
    if (ri > 0.5f && ryi > 0.4f) fillEllipse(s, g[i].c, ri, ryi, pal[WATER], clip);
  }
  if (f.kind == FISH_RISE && f.t < 0.35f)   // the dimple where it took a fly
    fillEllipse(s, Vec2{(float)f.x, (float)f.y}, 2.2f, 0.9f, mixRGB(pal[WATER], pal[ACTOR], 0.3f), clip);
  const Pixel drop = mixRGB(pal[WATER_LIGHT], pal[CLOUD_LIT], 0.3f);
  if (f.kind == FISH_JUMP && f.t < 0.3f) {   // water coming off it as it leaves
    const float k = f.t / 0.3f;
    fillCircle(s, Vec2{(float)f.x - (float)f.dir * 2.f, (float)f.y - 1.f - 4.f * k}, 1.0f, drop, clip);
    fillCircle(s, Vec2{(float)f.x + (float)f.dir * 1.5f, (float)f.y - 2.5f * k}, 0.9f, drop, clip);
  }
  if (f.kind == FISH_JUMP && f.t >= kJumpS && f.t < kJumpS + kSplashS) {   // the splash as it goes back in
    const float k = (f.t - kJumpS) / kSplashS;
    const Vec2 lc{(float)f.x - (float)f.dir * kJumpLen, (float)f.y - 1.f};
    fillEllipse(s, lc, 4.f * (1.f - 0.5f * k), 2.2f * (1.f - k) + 0.4f, mixRGB(pal[WATER_LIGHT], pal[WATER], 0.15f * k), clip);
    for (int i = 0; i < 5; ++i) {
      Vec2 p;
      if (splashDrop(f, i, p)) fillCircle(s, p, 1.1f, drop, clip);
    }
  }
  Vec2 c, u, nb;
  if (fishPose(f, c, u, nb)) {
    const Pixel back = mixRGB(pal[ACTOR], pal[WATER], 0.28f);
    Vec2 tail[4], body[10], belly[5];
    for (int i = 0; i < 4; ++i) tail[i] = fishPt(c, u, nb, kFishTail[i][0], kFishTail[i][1]);
    for (int i = 0; i < 10; ++i) body[i] = fishPt(c, u, nb, kFishBody[i][0], kFishBody[i][1]);
    for (int i = 0; i < 5; ++i) belly[i] = fishPt(c, u, nb, kFishBelly[i][0], kFishBelly[i][1]);
    fillPolygon(s, tail, 4, back, clip);
    fillPolygon(s, body, 10, back, clip);
    fillPolygon(s, belly, 5, mixRGB(pal[WATER_LIGHT], pal[ROCK], 0.25f), clip);
    fillCircle(s, fishPt(c, u, nb, 0.34f, -0.04f), 0.6f, pal[ACTOR], clip);   // its eye
    // The glint along its back: a short bright stroke and a brighter point in it.
    strokeCapsule(s, fishPt(c, u, nb, 0.22f, -0.15f), fishPt(c, u, nb, -0.08f, -0.16f), 1.1f, pal[WATER_LIGHT], clip);
    fillCircle(s, fishPt(c, u, nb, 0.12f, -0.16f), 0.7f, mixRGB(pal[WATER_LIGHT], pal[CLOUD_LIT], 0.7f), clip);
  }
}

// ---- the foam, the wake, the moon glint ---------------------------------------------------------

// Seconds of the foam's slow clock: whole steps of kFoamStepTicks.
inline float foamClock(const StreamLife& L) { return fl(L.step) * fl(kFoamStepTicks) / fl(SS_TICK_HZ); }

void wakeDash(const Frame& B, int arm, int k, float clock, Vec2& a, Vec2& b, float& fade) {
  const float sgn = arm ? 1.f : -1.f;
  float ph = clock / 1.9f + 0.37f * fl(arm);
  ph -= floorf(ph);
  const float d = (fl(k) + ph) * kWakeGap;   // px down the arm
  const float L = 6.f * (1.f - d / 40.f);
  const float u0 = fl(LS_BOULDER_R) + 5.f + d;
  const float v0 = sgn * (3.5f + kWakeSpread * (d + 4.f));
  const float wob = 0.4f * sinT(clock * 0.31f + fl(k) * 0.4f + fl(arm) * 0.5f);
  const Vec2 m = at(B.p, B.t, B.d, u0, v0 + wob);
  // Along the arm: down the current and out to its side.
  const Vec2 dir{B.t.x + B.d.x * sgn * kWakeSpread, B.t.y + B.d.y * sgn * kWakeSpread};
  a = Vec2{m.x - dir.x * 0.5f * L, m.y - dir.y * 0.5f * L};
  b = Vec2{m.x + dir.x * 0.5f * L, m.y + dir.y * 0.5f * L};
  fade = clamp01(1.f - d / 38.f);
}

// The foam flecks: born at the front of its foot and sliding back round it on our side (three), one
// staying near the front on the far side (the rest of that side is hidden behind the rock).
void foamFleck(const Frame& B, int k, float clock, Vec2& a, Vec2& b, float& fade) {
  float ph = clock / 2.3f + 0.29f * fl(k);
  ph -= floorf(ph);
  const bool far = k == 3;
  const float sgn = far ? -1.f : 1.f;
  const float th = far ? 0.045f * ph : 0.01f + 0.23f * ph;   // turns round the foot from its upstream point
  const float rx = fl(LS_BOULDER_R) + 1.2f, ry = 3.6f;
  const Vec2 m = at(B.p, B.t, B.d, -rx * cosT(th), sgn * ry * sinT(th) + 0.6f);
  const Vec2 tg{B.t.x * sinT(th) + B.d.x * sgn * 0.3f * cosT(th), B.t.y * sinT(th) + B.d.y * sgn * 0.3f * cosT(th)};
  const float L = 3.4f * (1.f - 0.5f * ph);
  a = Vec2{m.x - tg.x * 0.5f * L, m.y - tg.y * 0.5f * L};
  b = Vec2{m.x + tg.x * 0.5f * L, m.y + tg.y * 0.5f * L};
  fade = 1.f - ph;
}

Rect foamRect(const World& w) {
  if (!buildDone(w.build)) return kEmpty;
  const Frame B = boulderFrame(w.land);
  Ext e;
  const float R = fl(LS_BOULDER_R);
  const float u[2] = {-R - 6.f, R + 5.f + 3.f * kWakeGap + 4.f};
  const float vmax = 3.5f + kWakeSpread * (3.f * kWakeGap + 4.f) + 5.f;
  for (float uu : u)
    for (int sg = -1; sg <= 1; sg += 2) e.pt(at(B.p, B.t, B.d, uu, (float)sg * vmax), 1.f);
  e.pt(Vec2{B.p.x, B.p.y - 6.f});
  return e.rect(2.f);
}

void drawWake(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Frame B = boulderFrame(w.land);
  const float clock = foamClock(w.stream);
  for (int arm = 0; arm < 2; ++arm)
    for (int k = 0; k < 3; ++k) {
      Vec2 a, b;
      float fade;
      wakeDash(B, arm, k, clock, a, b, fade);
      strokeCapsule(s, a, b, 1.4f, mixRGB(pal[WATER], pal[WATER_LIGHT], 0.62f * fade), clip);
    }
  if (w.stream.moon) {   // moonlight caught on the wake: three short glints that come and go
    for (int k = 0; k < 3; ++k) {
      const uint32_t h = mix32((uint32_t)(w.stream.moon - 1) * 977u + (uint32_t)k * 131u);
      if ((h & 3u) == 0u) continue;
      const float u = fl(LS_BOULDER_R) + 7.f + 9.f * fl(k) + fl((int)((h >> 4) & 3u));
      const float v = (fl((int)((h >> 8) & 7u)) - 3.5f) * 0.9f;
      const Vec2 m = at(B.p, B.t, B.d, u, v);
      const float L = 3.f + fl((int)((h >> 12) & 3u));
      strokeCapsule(s, Vec2{m.x - B.t.x * 0.5f * L, m.y - B.t.y * 0.5f * L}, Vec2{m.x + B.t.x * 0.5f * L, m.y + B.t.y * 0.5f * L}, 1.6f,
                    mixRGB(pal[WATER_LIGHT], pal[SUN], 0.65f), clip);
    }
  }
}

void drawFoam(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Frame B = boulderFrame(w.land);
  const float clock = foamClock(w.stream);
  for (int k = 0; k < 4; ++k) {
    Vec2 a, b;
    float fade;
    foamFleck(B, k, clock, a, b, fade);
    strokeCapsule(s, a, b, 1.5f, mixRGB(pal[WATER], pal[WATER_LIGHT], 0.35f + 0.6f * fade), clip);
  }
}

// ---- the mock visitors (stills only) -----------------------------------------------------------

Vec2 mockAt(const World& w) {
  const Landscape& ls = w.land;
  float cy, half;
  switch (w.stream.mock) {
    case SM_DUCK: {
      const float x = 470.f;
      if (!landscapeStreamSpan(ls, x, cy, half)) cy = 420.f, half = 26.f;
      return Vec2{x, floorf(cy - 0.12f * half)};
    }
    case SM_HERON: {
      const float x = 506.f;
      if (!landscapeStreamSpan(ls, x, cy, half)) cy = 414.f, half = 26.f;
      return Vec2{x, floorf(cy + 0.62f * half)};
    }
    case SM_DRAGONFLY: {
      const Vec2 o = landscapeBoulder(ls);
      return Vec2{o.x - 44.f, o.y - 9.f};
    }
    default: return Vec2{0.f, 0.f};
  }
}

Rect mockRect(const World& w) {
  if (w.stream.mock == SM_NONE) return kEmpty;
  const Vec2 p = mockAt(w);
  switch (w.stream.mock) {
    case SM_DUCK: return Rect{(int)p.x - 16, (int)p.y - 14, (int)p.x + 30, (int)p.y + 6};
    case SM_HERON: return Rect{(int)p.x - 20, (int)p.y - 46, (int)p.x + 16, (int)p.y + 7};
    case SM_DRAGONFLY: return Rect{(int)p.x - 16, (int)p.y - 14, (int)p.x + 18, (int)p.y + 16};
    default: return kEmpty;
  }
}

void drawMock(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(mockRect(w), clip)) return;
  const Vec2 p = mockAt(w);
  const float t = (float)w.tick * SS_TICK_DT;
  const Pixel ripple = mixRGB(pal[WATER], pal[WATER_LIGHT], 0.6f);
  const Pixel refl = mixRGB(pal[WATER], pal[ACTOR], 0.3f);
  if (w.stream.mock == SM_DUCK) {   // a mallard drifting down, facing the way it goes
    const float bob = 0.4f * sinT(t * 0.6f);
    const float y = p.y + bob;
    fillEllipse(s, Vec2{p.x + 1.f, y + 2.f}, 9.5f, 1.8f, refl, clip);
    strokeCapsule(s, Vec2{p.x + 10.f, y + 1.f}, Vec2{p.x + 24.f, y - 3.f}, 1.1f, ripple, clip);   // the V behind it
    strokeCapsule(s, Vec2{p.x + 10.f, y + 2.f}, Vec2{p.x + 25.f, y + 4.5f}, 1.1f, ripple, clip);
    const Pixel body = mixRGB(pal[TRUNK], pal[ROCK], 0.45f);
    const Vec2 hull[7] = {{p.x - 8.f, y - 2.f}, {p.x - 6.f, y - 5.5f}, {p.x + 3.f, y - 6.5f}, {p.x + 9.f, y - 6.f},
                          {p.x + 11.5f, y - 8.f}, {p.x + 10.5f, y + 0.5f}, {p.x - 6.f, y + 0.8f}};
    fillPolygon(s, hull, 7, body, clip);
    const Vec2 wing[4] = {{p.x - 2.f, y - 5.5f}, {p.x + 7.f, y - 5.8f}, {p.x + 9.f, y - 3.f}, {p.x, y - 2.5f}};
    fillPolygon(s, wing, 4, mixRGB(body, pal[ACTOR], 0.3f), clip);
    strokeCapsule(s, Vec2{p.x - 6.f, y - 5.f}, Vec2{p.x - 7.5f, y - 10.f}, 3.2f, mixRGB(pal[FOLIAGE_DARK], pal[ACTOR], 0.2f), clip);
    fillCircle(s, Vec2{p.x - 8.f, y - 11.f}, 2.9f, mixRGB(pal[FOLIAGE_DARK], pal[ACTOR], 0.2f), clip);
    strokeCapsule(s, Vec2{p.x - 7.f, y - 7.f}, Vec2{p.x - 5.f, y - 7.f}, 1.0f, pal[WATER_LIGHT], clip);   // the neck ring
    strokeCapsule(s, Vec2{p.x - 10.5f, y - 10.5f}, Vec2{p.x - 13.5f, y - 10.f}, 1.6f, mixRGB(pal[FIRE_CORE], pal[GROUND], 0.4f), clip);
  } else if (w.stream.mock == SM_HERON) {   // standing in the shallows, neck folded, looking upstream
    const Pixel grey = mixRGB(pal[ROCK], pal[CLOUD_SHADE], 0.35f);
    const Pixel dark = mixRGB(grey, pal[ACTOR], 0.45f);
    fillEllipse(s, Vec2{p.x - 1.f, p.y + 3.f}, 6.f, 1.6f, refl, clip);
    fillEllipse(s, p, 5.5f, 1.8f, ripple, clip);
    fillEllipse(s, p, 3.5f, 1.0f, pal[WATER], clip);
    strokeCapsule(s, Vec2{p.x - 1.5f, p.y}, Vec2{p.x - 1.f, p.y - 16.f}, 1.2f, dark, clip);
    strokeCapsule(s, Vec2{p.x + 1.5f, p.y}, Vec2{p.x + 1.f, p.y - 16.f}, 1.2f, dark, clip);
    const Vec2 body[6] = {{p.x - 10.f, p.y - 22.f}, {p.x - 5.f, p.y - 27.f}, {p.x + 6.f, p.y - 26.f},
                          {p.x + 9.f, p.y - 20.f},  {p.x + 4.f, p.y - 15.f}, {p.x - 6.f, p.y - 16.5f}};
    fillPolygon(s, body, 6, grey, clip);
    const Vec2 back[4] = {{p.x - 9.f, p.y - 21.f}, {p.x - 3.f, p.y - 25.f}, {p.x + 7.f, p.y - 22.f}, {p.x + 4.f, p.y - 17.f}};
    fillPolygon(s, back, 4, dark, clip);
    strokeCapsule(s, Vec2{p.x + 6.f, p.y - 24.f}, Vec2{p.x + 9.5f, p.y - 31.f}, 2.6f, grey, clip);
    strokeCapsule(s, Vec2{p.x + 9.5f, p.y - 31.f}, Vec2{p.x + 7.f, p.y - 37.f}, 2.4f, grey, clip);
    fillCircle(s, Vec2{p.x + 8.f, p.y - 38.5f}, 2.4f, grey, clip);
    strokeCapsule(s, Vec2{p.x + 8.f, p.y - 39.5f}, Vec2{p.x + 2.f, p.y - 41.5f}, 1.0f, dark, clip);   // the plume
    strokeCapsule(s, Vec2{p.x + 10.f, p.y - 38.f}, Vec2{p.x + 16.5f, p.y - 36.5f}, 1.4f, mixRGB(pal[FIRE_CORE], pal[GROUND], 0.45f), clip);
    strokeCapsule(s, Vec2{p.x - 9.f, p.y - 19.f}, Vec2{p.x - 13.f, p.y - 16.f}, 2.0f, dark, clip);   // the tail
  } else if (w.stream.mock == SM_DRAGONFLY) {   // hovering low over the water, wings a shimmer
    const float hov = 1.2f * sinT(t * 0.5f);
    const Vec2 c{p.x, p.y + hov};
    fillEllipse(s, Vec2{p.x + 2.f, p.y + 11.f}, 3.5f, 1.0f, mixRGB(pal[WATER], pal[WATER_LIGHT], 0.5f), clip);   // its dimple below
    const float fl8 = 0.5f + 0.5f * sinT(t * 9.f);
    const Pixel wing = mixRGB(pal[WATER_LIGHT], pal[CLOUD_LIT], 0.4f);
    for (int k = -1; k <= 1; k += 2) {
      const float kk = (float)k;
      const Vec2 w1[4] = {{c.x - 0.5f, c.y}, {c.x - 3.5f, c.y - kk * (7.5f + fl8)}, {c.x - 0.5f, c.y - kk * (8.5f + fl8)}, {c.x + 1.2f, c.y - kk * 1.2f}};
      fillPolygon(s, w1, 4, wing, clip);
      const Vec2 w2[4] = {{c.x + 2.f, c.y}, {c.x + 2.5f, c.y - kk * (7.f - fl8)}, {c.x + 5.5f, c.y - kk * (7.f - fl8)}, {c.x + 4.5f, c.y - kk * 1.f}};
      fillPolygon(s, w2, 4, wing, clip);
    }
    const Pixel body = mixRGB(pal[FOLIAGE_DARK], pal[WATER], 0.35f);
    strokeCapsule(s, Vec2{c.x - 1.f, c.y}, Vec2{c.x + 13.f, c.y + 1.2f}, 1.6f, body, clip);
    fillCircle(s, Vec2{c.x - 2.2f, c.y}, 2.0f, body, clip);
    fillCircle(s, Vec2{c.x - 3.0f, c.y - 0.4f}, 0.9f, pal[ACTOR_LIGHT], clip);
  }
}

}  // namespace

// ---- the visitors: a heron or a deer (rare; see stream.h) ----------------------------------------

namespace {

constexpr float kHeronChance = 0.008f, kDeerChance = 0.010f;   // a loop's chance of each (by day)
constexpr float kTwilightVisit = 3.f;     // times as likely at dawn and dusk (the heron: all morning too)
constexpr float kVisitEarliest = 20.f, kVisitLatest = 300.f;   // when it may come, s after the build
constexpr float kVisitWindow = 90.f;      // s it waits for a quiet moment after its time, then gives up
constexpr float kVisitNeed = 150.f;       // s of the loop that must be left for it to come
constexpr float kVisitEndMargin = 40.f;   // s before the loop's end it leaves
constexpr float kHeronSpook = 140.f, kDeerSpook = 120.f, kDogSpook = 130.f;   // px: too close, it goes
constexpr float kQuietRadius = 220.f;     // px: nobody this near the spot for it to come
constexpr float kHeronIn = 5.f, kHeronOut = 5.f, kHeronOutHurry = 3.4f;   // its flights, s
constexpr float kHeronS = 1.15f;          // the heron's size (1: the first mockup's)
constexpr float kHeronX0 = 400.f, kHeronX1 = 480.f;   // where it stands: the near side's shallows, clear of
                                                      // the raccoon bush's bank (x 330-375) and the chip's corner
constexpr float kHeronLane = -0.68f;
constexpr float kHeronStayMin = 30.f, kHeronStayMax = 90.f;
constexpr float kDeerX0 = 668.f, kDeerX1 = 740.f;     // where it drinks: the camp-side bank, right of the wheel's
constexpr float kDeerWalk = 26.f, kDeerStep = 9.f, kDeerLeave = 32.f, kDeerRun = 110.f;   // px/s

enum HeronAct : uint8_t { HA_IDLE = 0, HA_ALERT, HA_STEP, HA_LOOKBACK, HA_STRIKE };
constexpr float kStepS = 1.4f, kLookS = 2.6f, kStrikeS = 1.4f;
enum DeerStep : uint8_t { DS_WALK = 0, DS_DOWN, DS_DRINK, DS_LOOK, DS_DRINK2 };

const char* const kStartBeats[] = {"pause", "read", "mug_sunset", "cloud_watch"};
// Beats that use the water or the banks, or end the loop: a visitor leaves as one begins.
const char* const kWaterBeats[] = {"skip_stone", "stream_listen", "cairn", "birds_on_water", "wade", "return",
                                   "leave", "douse", "wash", "day_hike", "afternoon", "close", "bear", "dinner",
                                   "build"};

// The majors that use the water or its banks: the heron leaves as one begins (it stays through the
// others: it is no critter, and the hiker coming near sends it off anyway).
const char* const kWaterMajors[] = {"squirrel_splash", "fish_leap", "fishing_small", "fishing_log", "fishing_good",
                                    "bathe", "fetch", "bike"};

template <int N>
bool beatIn(const World& w, const char* const (&list)[N]) {
  const char* b = w.minors.beat;
  if (!b) return false;
  for (const char* k : list)
    if (strcmp(b, k) == 0) return true;
  return false;
}

inline float dist(Vec2 a, Vec2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

float secondsLeft(const World& w) {
  const uint32_t end = spineLoopTicks(w.plan), now = storyTick(w);
  return now >= end ? 0.f : fl((int)(end - now)) * SS_TICK_DT;
}

// Nothing in the story wants the place or the quiet: it may stay. The deer (a critter) leaves for any
// beat that owns the critters; the heron only for one that uses the water.
bool quietNow(const World& w, bool heron = false) {
  const StreamLife& L = w.stream;
  const bool busy = heron ? beatIn(w, kWaterMajors) : w.busy;
  return !busy && w.fade <= 0.f && !w.fi.on && !visitorsHolds(w) && w.weather.rain < 0.2f &&
         w.weather.kind != WX_RAIN && w.weather.kind != WX_STORM && !L.night && !beatIn(w, kWaterBeats) &&
         secondsLeft(w) > kVisitEndMargin;
}

// Who is too close to `p` (a visible hiker, a house dog in the scene), within `r` (dogs kDogSpook).
bool threatNear(const World& w, Vec2 p, float r, Vec2& from) {
  for (int k = 0; k < HD_COUNT; ++k) {   // a house dog running at it (housedogs.cpp HA_CHASE): at any distance
    const HouseDog& d = w.dogs.d[k];
    const int s = d.slot;
    if (d.act == HA_CHASE && d.step >= 1 && s >= 0 && w.crit[s].in_use && w.critters[s].visible) {
      from = w.critters[s].feet;
      return true;
    }
  }
  if (w.hiker.visible && dist(w.hiker.feet, p) < r) {
    from = w.hiker.feet;
    return true;
  }
  for (int k = 0; k < HD_COUNT; ++k) {
    const int s = w.dogs.d[k].slot;
    if (s >= 0 && w.crit[s].in_use && w.critters[s].visible && dist(w.critters[s].feet, p) < kDogSpook) {
      from = w.critters[s].feet;
      return true;
    }
  }
  return false;
}

// A house dog too close to `p`, or running at it: what still sends tai chi's guest heron up.
bool dogNear(const World& w, Vec2 p, Vec2& from) {
  for (int k = 0; k < HD_COUNT; ++k) {
    const HouseDog& d = w.dogs.d[k];
    const int s = d.slot;
    if (s < 0 || !w.crit[s].in_use || !w.critters[s].visible) continue;
    if ((d.act == HA_CHASE && d.step >= 1) || dist(w.critters[s].feet, p) < kDogSpook) {
      from = w.critters[s].feet;
      return true;
    }
  }
  return false;
}

// A quiet moment to come in: a pause or a calm filler, no minor running, no other story critter, nobody
// near the spot, time left.
bool mayStart(const World& w, Vec2 spot) {
  if (!quietNow(w) || !beatIn(w, kStartBeats) || w.minors.cur != MN_NONE || secondsLeft(w) < kVisitNeed) return false;
  for (int i = 0; i < CRITTER_STORY; ++i)
    if (w.crit[i].in_use) return false;
  Vec2 from;
  return !threatNear(w, spot, kQuietRadius, from) && !(w.fx.on && dist(w.hiker.feet, spot) < kQuietRadius);
}

float bankY(const World& w, float x) {   // the camp side's water edge at x
  float cy, half;
  return landscapeStreamSpan(w.land, x, cy, half) ? cy - half : 360.f;
}

// ---- the heron's figure ----

struct HPose {
  uint8_t mode;     // 0 standing, 1 flying, 2 landing, 3 crouched for take-off
  int8_t f;         // facing
  Vec2 o;           // standing: its feet (at the water); flying: its body's middle
  float ext;        // the neck up, 0 folded .. 1 stretched up (alert)
  float strike;     // 0 .. 1 the neck shot out down to the water
  bool back;        // looking back over its shoulder
  float lift[2];    // a leg lifted (a step), 0..1
  float wing;       // flying: the wings' beat, -1 down .. 1 up
  float spread;     // standing: the near wing half open, 0 .. 1 (tai chi's guest mirrors White Crane)
};

enum HCol : uint8_t { HC_GREY = 0, HC_DARK, HC_PALE, HC_BILL, HC_LEG, HC_BLACK, HC_RIPPLE, HC_WATER, HC_REFL };

// Draws, or (e set) only measures, the heron: the same calls both ways, so its bounds are its drawing.
struct HSink {
  Surface* s;
  const Rect* clip;
  const Palette* pal;
  Ext* e;
  Pixel col(uint8_t c) const {
    const Palette& p = *pal;
    const Pixel grey = mixRGB(p[ROCK], p[CLOUD_SHADE], 0.35f);
    switch (c) {
      case HC_GREY: return grey;
      case HC_DARK: return mixRGB(grey, p[ACTOR], 0.45f);
      case HC_PALE: return mixRGB(p[CLOUD_LIT], p[ROCK], 0.2f);
      case HC_BILL: return mixRGB(p[FIRE_CORE], p[GROUND], 0.45f);
      case HC_LEG: return mixRGB(p[ACTOR], p[ROCK], 0.3f);
      case HC_BLACK: return mixRGB(p[ACTOR], p[ROCK], 0.1f);
      case HC_RIPPLE: return mixRGB(p[WATER], p[WATER_LIGHT], 0.6f);
      case HC_WATER: return p[WATER];
      default: return mixRGB(p[WATER], p[ACTOR], 0.3f);
    }
  }
  void poly(const Vec2* q, int n, uint8_t c) {
    if (e) {
      for (int i = 0; i < n; ++i) e->pt(q[i], 1.5f);
    } else {
      fillPolygon(*s, q, n, col(c), *clip);
    }
  }
  void cap(Vec2 a, Vec2 b, float wd, uint8_t c) {
    if (e) {
      e->pt(a, 0.5f * wd + 1.5f);
      e->pt(b, 0.5f * wd + 1.5f);
    } else {
      strokeCapsule(*s, a, b, wd, col(c), *clip);
    }
  }
  void circ(Vec2 a, float r, uint8_t c) {
    if (e) e->pt(a, r + 1.5f);
    else fillCircle(*s, a, r, col(c), *clip);
  }
  void ell(Vec2 a, float rx, float ry, uint8_t c) {
    if (e) {
      e->pt(Vec2{a.x - rx, a.y - ry}, 1.5f);
      e->pt(Vec2{a.x + rx, a.y + ry}, 1.5f);
    } else {
      fillEllipse(*s, a, rx, ry, col(c), *clip);
    }
  }
};

inline Vec2 hp(const HPose& P, float dx, float dy) { return Vec2{P.o.x + (float)P.f * dx * kHeronS, P.o.y + dy * kHeronS}; }

template <int N>
void hpoly(HSink& k, const HPose& P, const float (&q)[N][2], float dy, uint8_t c) {
  Vec2 v[N];
  for (int i = 0; i < N; ++i) v[i] = hp(P, q[i][0], q[i][1] + dy);
  k.poly(v, N, c);
}

const float kHBody[6][2] = {{-12.f, -26.f}, {-6.f, -32.f}, {7.f, -31.f}, {11.f, -24.f}, {5.f, -18.f}, {-7.f, -19.5f}};
const float kHBack[4][2] = {{-11.f, -25.f}, {-4.f, -30.5f}, {8.f, -27.f}, {4.f, -20.5f}};
const float kHFly[6][2] = {{-11.f, 0.f}, {-5.f, -4.f}, {8.f, -3.5f}, {12.f, 0.f}, {6.f, 3.5f}, {-8.f, 3.f}};

void heronStand(HSink& k, const HPose& P) {
  const float down = P.mode == 3 ? 5.f : 0.f;   // crouched for the jump
  k.ell(Vec2{P.o.x - (float)P.f, P.o.y + 3.f}, 7.f * kHeronS, 1.8f, HC_REFL);
  k.ell(P.o, 6.5f * kHeronS, 2.0f, HC_RIPPLE);
  k.ell(P.o, 4.4f * kHeronS, 1.2f, HC_WATER);
  for (int l = 0; l < 2; ++l) {   // the far leg, then the near one
    const float side = l ? -1.6f : 1.6f;
    const float lf = P.lift[l];
    k.cap(hp(P, side + 3.f * lf, -4.f * lf), hp(P, side * 0.75f, -19.f + down), 1.3f, HC_LEG);
  }
  hpoly(k, P, kHBody, down, HC_GREY);
  hpoly(k, P, kHBack, down, HC_DARK);
  if (P.mode == 3) {   // the wings half open, ready to go
    const float wv[4][2] = {{-9.f, -27.f}, {6.f, -29.f}, {2.f, -40.f}, {-12.f, -37.f}};
    hpoly(k, P, wv, down, HC_DARK);
  } else if (P.spread > 0.02f) {   // one wing lifted off its back and half open
    const float sp = P.spread;
    const float wv[4][2] = {{-9.f, -27.f}, {6.f, -29.f}, {lerp(6.f, 1.f, sp), -29.f - 13.f * sp}, {lerp(-9.f, -13.f, sp), -27.f - 12.f * sp}};
    hpoly(k, P, wv, down, HC_DARK);
  }
  k.cap(hp(P, -11.f, -23.f + down), hp(P, -16.f, -19.f + down), 2.2f, HC_DARK);   // the tail
  k.cap(hp(P, -6.f, -20.f + down), hp(P, 6.f, -19.f + down), 1.0f, HC_PALE);       // the pale underside
  // The neck: folded, stretched up (alert) or shot out down to the water (the strike).
  const Vec2 B = hp(P, 7.f, -29.f + down);
  const float e = P.ext, s = P.strike;
  const float mx = lerp(lerp(11.f, 10.5f, e), 15.f, s), my = lerp(lerp(-37.f, -40.f, e), -20.f, s) + down * (1.f - s);
  const float hx = lerp(lerp(8.5f, 10.f, e), 21.f, s), hy = lerp(lerp(-44.5f, -51.f, e), -7.f, s) + down * (1.f - s);
  const Vec2 M = hp(P, mx, my), H = hp(P, hx, hy);
  k.cap(B, M, 3.2f * kHeronS, HC_GREY);
  k.cap(M, H, 2.8f * kHeronS, HC_GREY);
  k.cap(hp(P, mx + 1.f, my + 1.f), hp(P, hx + 1.f, hy + 2.f), 1.0f, HC_PALE);   // the white of its throat
  k.circ(H, 2.6f * kHeronS, HC_GREY);
  // The bill: forward, down at the water in the strike, or back over its shoulder.
  const float bdir = P.back ? -1.f : 1.f;
  const float bx = lerp(9.f * bdir, 6.f, s), by = lerp(1.5f, 7.f, s);
  k.cap(hp(P, hx + 1.8f * bdir, hy), hp(P, hx + bx, hy + by), 1.6f, HC_BILL);
  k.cap(hp(P, hx + 0.5f, hy - 1.f), hp(P, hx - 6.f * bdir, hy - 2.5f), 1.0f, HC_BLACK);   // the black crest
}

// A wing seen from the side: from the shoulder up (wy 1) or down (wy -1), swept back to a long tip.
void heronWing(HSink& k, const HPose& P, float dx, float dy, float wy, uint8_t c, uint8_t tipc) {
  const float w[6][2] = {{4.f + dx, -1.f + dy},          {1.f + dx, -9.f * wy - 1.f + dy}, {-6.f + dx, -17.f * wy - 1.f + dy},
                         {-17.f + dx, -19.f * wy + dy},  {-11.f + dx, -9.f * wy + dy},     {-6.f + dx, -1.f + dy}};
  hpoly(k, P, w, 0.f, c);
  const float tip[4][2] = {{-6.f + dx, -17.f * wy - 1.f + dy}, {-17.f + dx, -19.f * wy + dy}, {-13.f + dx, -13.f * wy + dy},
                           {-4.f + dx, -12.f * wy + dy}};
  hpoly(k, P, tip, 0.f, tipc);   // the dark flight feathers
}

void heronFly(HSink& k, const HPose& P) {
  const float wy = P.wing;
  heronWing(k, P, 3.f, -1.f, wy * 0.9f, HC_DARK, HC_BLACK);   // the far wing, a little ahead
  if (P.mode == 2) {   // landing: the legs down for the water
    k.cap(hp(P, -2.f, 2.f), hp(P, -4.f, 17.f), 1.3f, HC_LEG);
    k.cap(hp(P, 1.f, 2.f), hp(P, -1.f, 17.f), 1.3f, HC_LEG);
  } else {             // flying: trailing behind
    k.cap(hp(P, -10.f, 1.5f), hp(P, -25.f, 3.f), 1.3f, HC_LEG);
  }
  hpoly(k, P, kHFly, 0.f, HC_GREY);
  k.cap(hp(P, -2.f, 2.6f), hp(P, 8.f, 2.f), 1.0f, HC_PALE);   // its pale underside
  k.cap(hp(P, 9.f, -1.f), hp(P, 12.f, -3.f), 3.f * kHeronS, HC_GREY);   // the neck drawn in
  k.circ(hp(P, 12.f, -3.5f), 2.6f * kHeronS, HC_GREY);
  k.cap(hp(P, 14.f, -3.f), hp(P, 22.f, -1.5f), 1.6f, HC_BILL);
  k.cap(hp(P, 12.f, -5.f), hp(P, 6.f, -6.5f), 1.0f, HC_BLACK);
  heronWing(k, P, 0.f, 0.f, wy, HC_GREY, HC_DARK);   // the near wing
}

inline float envelope(float t, float len, float in) {   // 0 -> 1 -> 0 over len, in s each side
  return clamp01(t / in) * clamp01((len - t) / in);
}

// The heron's pose now (false: not in the scene).
bool heronPose(const World& w, HPose& P) {
  const StreamVisit& V = w.stream.visit;
  if (V.kind != SV_HERON || V.stage == GS_WAIT || V.stage == GS_DONE) return false;
  P = HPose{};
  P.f = V.dir;
  if (V.stage == GS_IN) {
    const float k = V.t / kHeronIn;
    const Vec2 above{V.spot.x - (float)V.dir * 12.f, V.spot.y - 30.f};
    if (k < 0.8f) {   // across high, then down: over the chip's corner it is still well up
      const float u = k / 0.8f;
      P.mode = 1;
      P.o = Vec2{lerp(V.from.x, above.x, easeOutCubic(u)), lerp(V.from.y, above.y, easeInCubic(u))};
      P.wing = sinT(V.t * 1.6f);
    } else {
      const float u = easeOutCubic((k - 0.8f) / 0.2f);
      P.mode = 2;
      P.o = Vec2{lerp(above.x, V.spot.x, u), lerp(above.y, V.spot.y - 24.f * kHeronS, u)};
      P.wing = 0.85f;
    }
    P.o = Vec2{floorf(P.o.x + 0.5f), floorf(P.o.y + 0.5f)};
    return true;
  }
  if (V.stage == GS_OUT) {
    const float crouch = V.spooked ? 0.2f : 0.5f;
    if (V.t < crouch) {
      P.mode = 3;
      P.o = V.spot;
      return true;
    }
    const float len = V.spooked ? kHeronOutHurry : kHeronOut;
    const float u = clamp01((V.t - crouch) / len);   // up first, then away
    const Vec2 start{V.spot.x, V.spot.y - 24.f * kHeronS};
    P.mode = 1;
    P.f = V.to.x > V.spot.x ? 1 : -1;
    P.o = Vec2{floorf(lerp(start.x, V.to.x, easeInCubic(u)) + 0.5f), floorf(lerp(start.y, V.to.y, easeOutCubic(u)) + 0.5f)};
    P.wing = sinT((V.t - crouch) * (V.spooked ? 2.4f : 1.7f) + 0.25f);
    return true;
  }
  // Standing, at its small action.
  P.mode = 0;
  P.o = V.spot;
  const float t = V.act_t, len = V.act_len;
  switch (V.act) {
    case HA_ALERT: P.ext = envelope(t, len, 0.6f); break;
    case HA_STEP: {
      const float k = clamp01(t / kStepS);
      P.o.x = floorf(V.spot.x + (float)V.dir * 7.f * kHeronS * easeInOut(k) + 0.5f);
      P.lift[k < 0.5f ? 1 : 0] = sinT(k < 0.5f ? k : k - 0.5f);
      P.ext = 0.3f;
      break;
    }
    case HA_LOOKBACK:
      P.back = t > 0.3f && t < kLookS - 0.3f;
      P.ext = 0.4f;
      break;
    case HA_STRIKE:
      P.ext = t < 0.3f ? 0.3f * t / 0.3f : 0.3f;
      P.strike = t < 0.3f ? 0.f : (t < 0.45f ? (t - 0.3f) / 0.15f : (t < 0.9f ? 1.f : clamp01(1.f - (t - 0.9f) / 0.5f)));
      break;
    default: break;
  }
  if (V.mirror > 0.f) {   // his guest, mirroring the crane: the near leg tucked up, the neck up, a wing half open
    const float m = easeInOut(clamp01(V.mirror));
    P.spread = m;
    P.lift[1] = P.lift[1] > 2.f * m ? P.lift[1] : 2.f * m;
    P.ext = P.ext > 0.8f * m ? P.ext : 0.8f * m;
  }
  return true;
}

Rect visitRing(const StreamVisit& V) {
  if (V.ring_t < 0.f) return kEmpty;
  const float rmax = V.kind == SV_HERON ? 10.f : 8.f;
  const Ring g = ringAt(Vec2{(float)V.ring_x, (float)V.ring_y}, V.ring_t, 0.f, kRingS - 0.2f, rmax);
  if (g.r <= 0.f) return kEmpty;
  return Rect{(int)floorf(g.c.x - g.r) - 2, (int)floorf(g.c.y - 0.34f * g.r) - 2, (int)ceilf(g.c.x + g.r) + 3,
              (int)ceilf(g.c.y + 0.34f * g.r) + 3};
}

void drawVisitRing(const StreamVisit& V, Surface& s, const Rect& clip, const Palette& pal) {
  if (V.ring_t < 0.f || !overlaps(visitRing(V), clip)) return;
  const float rmax = V.kind == SV_HERON ? 10.f : 8.f;
  const Ring g = ringAt(Vec2{(float)V.ring_x, (float)V.ring_y}, V.ring_t, 0.f, kRingS - 0.2f, rmax);
  fillEllipse(s, g.c, g.r, 0.34f * g.r, mixRGB(pal[WATER], pal[WATER_LIGHT], g.k), clip);
  const float ri = g.r - g.th, ryi = 0.34f * g.r - 0.6f * g.th;
  if (ri > 0.5f && ryi > 0.4f) fillEllipse(s, g.c, ri, ryi, pal[WATER], clip);
}

Rect heronRect(const World& w) {
  HPose P;
  if (!heronPose(w, P)) return kEmpty;
  Ext e;
  HSink k{nullptr, nullptr, nullptr, &e};
  if (P.mode == 1 || P.mode == 2) heronFly(k, P);
  else heronStand(k, P);
  return e.rect(1.f);
}

void ring(StreamVisit& V, float x, float y) {
  V.ring_x = (int16_t)floorf(x + 0.5f);
  V.ring_y = (int16_t)floorf(y + 0.5f);
  V.ring_t = 0.f;
}

// ---- the heron's days ----

void heronNextAct(StreamVisit& V, Rng& r) {
  const float roll = r.uniform();
  const float u = r.uniform();
  V.act_t = 0.f;
  if (roll < 0.35f) {
    V.act = HA_IDLE;
    V.act_len = 4.f + 5.f * u;
  } else if (roll < 0.55f) {
    V.act = HA_ALERT;
    V.act_len = 3.f + 3.f * u;
  } else if (roll < 0.75f) {
    V.act = HA_STEP;
    V.act_len = kStepS;
    const float nx = V.spot.x + (float)V.dir * 7.f * kHeronS;
    if (nx < kHeronX0 || nx > kHeronX1) V.dir = (int8_t)-V.dir;   // turns round first
  } else if (roll < 0.9f) {
    V.act = HA_LOOKBACK;
    V.act_len = kLookS;
  } else {
    V.act = HA_STRIKE;
    V.act_len = kStrikeS;
  }
}

void heronLeave(World& w, bool hurry, Vec2 away_from) {
  StreamVisit& V = w.stream.visit;
  V.stage = GS_OUT;
  V.t = 0.f;
  V.guest = 0;
  V.mirror = 0.f;
  V.spooked = hurry ? 1 : 0;
  const float side = hurry ? (away_from.x < V.spot.x ? 1.f : -1.f) : (float)V.dir;
  V.to = Vec2{side > 0.f ? 860.f : -60.f, 150.f};
}

void heronStep(World& w) {
  StreamVisit& V = w.stream.visit;
  const float dt = SS_TICK_DT;
  V.t += dt;
  Vec2 from;
  switch (V.stage) {
    case GS_IN:
      if (V.t >= kHeronIn) {
        V.stage = GS_STAY;
        V.t = 0.f;
        heronNextAct(V, w.stream.vrng);
      }
      break;
    case GS_STAY: {
      // Tai chi's guest stays while he moves beside it (he is slow and calm); a dog still sends it up.
      if (V.guest ? dogNear(w, V.spot, from) : threatNear(w, V.spot, kHeronSpook, from)) {
        heronLeave(w, true, from);
        break;
      }
      if (V.guest) V.t = 0.f;   // its stay is not counted while it is his guest
      if (!quietNow(w, true) || V.t >= V.stay) {
        heronLeave(w, false, V.spot);
        break;
      }
      // The mirror: it finishes what it was at (an alert look is cut short), then lifts a leg and a wing
      // and holds; no new small action until it has folded again.
      const bool want = (V.guest & 2) != 0;
      const bool hold = want || V.mirror > 0.f;
      if (want && V.act == HA_ALERT && V.act_len > V.act_t + 0.6f) V.act_len = V.act_t + 0.6f;
      if (V.act == HA_IDLE) {
        if (want) V.mirror = V.mirror + dt / 2.5f < 1.f ? V.mirror + dt / 2.5f : 1.f;
        else if (V.mirror > 0.f) V.mirror = V.mirror - dt / 1.5f > 0.f ? V.mirror - dt / 1.5f : 0.f;
        if (hold) break;
      }
      const float t0 = V.act_t;
      V.act_t += dt;
      if (V.act == HA_STRIKE && t0 < 0.42f && V.act_t >= 0.42f)
        ring(V, V.spot.x + (float)V.dir * 27.f * kHeronS, V.spot.y - 1.f);
      if (V.act_t >= V.act_len) {
        if (V.act == HA_STEP) V.spot.x = floorf(V.spot.x + (float)V.dir * 7.f * kHeronS + 0.5f);
        if (hold) {   // stands calm for the mirror (no draw from its random stream)
          V.act = HA_IDLE;
          V.act_t = 0.f;
          V.act_len = 3.f;
        } else {
          heronNextAct(V, w.stream.vrng);
        }
      }
      break;
    }
    case GS_OUT: {
      const float len = (V.spooked ? kHeronOutHurry : kHeronOut) + (V.spooked ? 0.2f : 0.5f);
      if (V.t >= len) V.stage = GS_DONE;
      break;
    }
    default: break;
  }
}

// ---- the deer's ----

Critter* deerOf(World& w) {
  StreamVisit& V = w.stream.visit;
  if (V.slot < 0 || !directorOwns(w, V.slot, CT_STREAM)) return nullptr;
  return &w.critters[V.slot];
}

void deerLeave(World& w, bool hurry, Vec2 away_from) {
  StreamVisit& V = w.stream.visit;
  Critter* c = deerOf(w);
  V.stage = GS_OUT;
  V.t = 0.f;
  V.spooked = hurry ? 1 : 0;
  if (!c) return;
  if (hurry) {
    critterSetAction(*c, CA_STARTLE, 0.15f);
    directorExit(w, V.slot, away_from.x < c->feet.x ? 1 : -1, kDeerRun, CA_RUN);
  } else {
    critterSetAction(*c, CA_IDLE, 0.5f);
    directorExit(w, V.slot, 1, kDeerLeave, CA_MOVE);   // back the way it came
  }
}

void deerStep(World& w) {
  StreamVisit& V = w.stream.visit;
  const float dt = SS_TICK_DT;
  V.t += dt;
  Critter* c = deerOf(w);
  if (V.stage == GS_OUT) {
    if (!c) {   // out of sight and released (or the director has it)
      V.stage = GS_DONE;
      V.slot = -1;
    }
    return;
  }
  if (!c) {   // the director scattered it: it runs out on its own
    V.stage = GS_OUT;
    V.spooked = 1;
    return;
  }
  Vec2 from;
  if (threatNear(w, c->feet, kDeerSpook, from)) {
    deerLeave(w, true, from);
    return;
  }
  if (!quietNow(w)) {
    deerLeave(w, false, c->feet);
    return;
  }
  const float t0 = V.act_t;
  V.act_t += dt;
  switch (V.act) {
    case DS_WALK:
      if (critterArrived(*c)) {
        V.act = DS_DOWN;
        V.act_t = 0.f;
        directorWalk(w, V.slot, V.spot, kDeerStep, CA_MOVE);
      }
      break;
    case DS_DOWN:
      if (critterArrived(*c)) {
        V.act = DS_DRINK;
        V.act_t = 0.f;
        V.act_len = w.stream.vrng.range(4.f, 7.f);
        V.stage = GS_STAY;
        critterSetAction(*c, CA_SNIFF, 0.8f);
      }
      break;
    case DS_DRINK:
    case DS_DRINK2: {
      // Rings where its muzzle meets the water, one a little over every second.
      const float k0 = floorf((t0 - 0.9f) / 1.2f), k1 = floorf((V.act_t - 0.9f) / 1.2f);
      if (V.act_t >= 0.9f && k1 != k0) {
        const Vec2 m = critterMouth(*c);
        ring(V, m.x, bankY(w, m.x) + 2.f);
      }
      if (V.act_t >= V.act_len) {
        if (V.act == DS_DRINK) {
          V.act = DS_LOOK;
          V.act_t = 0.f;
          V.act_len = w.stream.vrng.range(2.5f, 4.f);
          critterSetAction(*c, CA_LOOK, 0.6f);
        } else {
          deerLeave(w, false, c->feet);
        }
      }
      break;
    }
    case DS_LOOK:
      if (V.act_t >= V.act_len) {
        V.act = DS_DRINK2;
        V.act_t = 0.f;
        V.act_len = w.stream.vrng.range(3.f, 6.f);
        critterSetAction(*c, CA_SNIFF, 0.8f);
      }
      break;
    default: break;
  }
}

// ---- coming ----

void visitPlan(World& w) {
  StreamLife& L = w.stream;
  StreamVisit& V = L.visit;
  V.planned = 1;
  // A fixed number of draws whatever comes of them (one per statement).
  const float roll = L.vrng.uniform();
  const float when = L.vrng.range(kVisitEarliest, kVisitLatest);
  const float stay = L.vrng.range(kHeronStayMin, kHeronStayMax);
  const float where = L.vrng.uniform();
  const bool right = L.vrng.chance(0.5f);
  const bool face = L.vrng.chance(0.5f);
  const float hk = L.twilight || w.plan.family == SF_MORNING ? kTwilightVisit : 1.f;
  const float dk = L.twilight ? kTwilightVisit : 1.f;
  const float ph = kHeronChance * hk, pd = kDeerChance * dk;
  V.kind = V.force ? V.force : (uint8_t)(roll < ph ? SV_HERON : (roll < ph + pd ? SV_DEER : SV_NONE));
  if (V.kind == SV_NONE) return;
  V.stage = GS_WAIT;
  V.at = storyTick(w) + (uint32_t)((V.force ? 2.f : when) * fl(SS_TICK_HZ));
  V.stay = stay;
  V.dir = (int8_t)(face ? 1 : -1);
  if (V.kind == SV_HERON) {
    const float x = lerp(kHeronX0, kHeronX1, where);
    float cy, half;
    if (!landscapeStreamSpan(w.land, x, cy, half)) cy = 430.f, half = 27.f;
    V.spot = Vec2{floorf(x + 0.5f), floorf(cy - kHeronLane * half / 1.06f + 0.5f)};
    V.from = Vec2{right ? 860.f : -60.f, 205.f};
  } else {
    const float x = lerp(kDeerX0, kDeerX1, where);
    V.spot = Vec2{floorf(x + 0.5f), floorf(bankY(w, x) + 6.f + 0.5f)};
    V.dir = -1;
  }
}

void visitBegin(World& w) {
  StreamVisit& V = w.stream.visit;
  V.t = 0.f;
  V.act = 0;
  V.act_t = 0.f;
  if (V.kind == SV_HERON) {
    V.stage = GS_IN;
    return;
  }
  const float x = 850.f;
  const int s = directorSpawn(w, CR_DEER, Vec2{x, bankY(w, (float)SS_WIDTH) - 12.f}, -1);
  if (s < 0) {
    V.stage = GS_DONE;
    return;
  }
  V.slot = (int8_t)s;
  w.crit[s].tag = CT_STREAM;
  V.stage = GS_IN;
  V.act = DS_WALK;
  directorWalk(w, s, Vec2{V.spot.x + 10.f, bankY(w, V.spot.x + 10.f) - 12.f}, kDeerWalk, CA_MOVE);
}

void visitStep(World& w) {
  StreamLife& L = w.stream;
  StreamVisit& V = L.visit;
  if (V.ring_t >= 0.f) {
    V.ring_t += SS_TICK_DT;
    if (V.ring_t >= kRingS - 0.2f) V.ring_t = -1.f;
  }
  if (!V.planned) visitPlan(w);
  switch (V.stage) {
    case GS_WAIT:
      if (V.kind == SV_NONE) break;
      if (storyTick(w) >= V.at) {
        if (mayStart(w, V.spot)) visitBegin(w);
        else if (storyTick(w) > V.at + (uint32_t)(kVisitWindow * fl(SS_TICK_HZ))) V.stage = GS_DONE;
      }
      break;
    case GS_DONE: break;
    default:
      if (V.kind == SV_HERON) heronStep(w);
      else deerStep(w);
      break;
  }
}

}  // namespace

bool streamGuestAt(const World& w, Vec2& at) {
  const StreamVisit& V = w.stream.visit;
  if (V.spooked || !buildDone(w.build)) return false;
  if (V.kind == SV_HERON) {
    if (V.stage == GS_STAY || (V.stage == GS_IN && V.t >= 0.7f * kHeronIn)) {
      at = V.spot;
      return true;
    }
    return false;
  }
  if (V.kind == SV_DEER && (V.stage == GS_IN || V.stage == GS_STAY) && V.slot >= 0) {
    const int s = V.slot;
    if (!w.crit[s].in_use || w.crit[s].tag != CT_STREAM || !w.critters[s].visible) return false;
    const Vec2 f = w.critters[s].feet;
    if (V.stage != GS_STAY && f.x > (float)SS_WIDTH - 80.f) return false;   // well in: it has somewhere to bolt
    at = f;
    return true;
  }
  return false;
}

bool streamHeronGuest(World& w, float x, int facing) {
  StreamLife& L = w.stream;
  StreamVisit& V = L.visit;
  if (!buildDone(w.build) || L.night) return false;
  if (!V.planned) visitPlan(w);
  if (V.kind == SV_DEER) return false;   // one visitor a loop
  if (V.kind == SV_HERON && (V.stage == GS_IN || V.stage == GS_STAY)) {   // already here: taken as the guest
    V.guest = 1;
    return true;
  }
  if (V.kind == SV_HERON && V.stage != GS_WAIT) return false;   // come and gone
  if (!quietNow(w, true)) return false;
  if (V.kind != SV_HERON) V.stay = kHeronStayMin;
  V.kind = SV_HERON;
  V.dir = (int8_t)(facing < 0 ? -1 : 1);
  V.spooked = 0;
  V.mirror = 0.f;
  V.guest = 1;
  const float hx = x < kHeronX0 ? kHeronX0 : (x > kHeronX1 ? kHeronX1 : x);
  float cy, half;
  if (!landscapeStreamSpan(w.land, hx, cy, half)) cy = 430.f, half = 27.f;
  V.spot = Vec2{floorf(hx + 0.5f), floorf(cy - kHeronLane * half / 1.06f + 0.5f)};
  V.from = Vec2{860.f, 205.f};   // from upstream, the way he faces: high over the chip's corner, then down
  visitBegin(w);
  return true;
}

void streamHeronMirror(World& w, bool on) {
  StreamVisit& V = w.stream.visit;
  if (V.kind == SV_HERON && V.guest) V.guest = on ? 3 : 1;
}

void streamHeronRelease(World& w) {
  StreamVisit& V = w.stream.visit;
  if (V.kind != SV_HERON || !V.guest) return;
  V.guest = 0;
  if (V.stage == GS_STAY) heronLeave(w, false, V.spot);
}

void streamHeronHandBack(World& w) { w.stream.visit.guest = 0; }

bool streamHeronIsGuest(const World& w) {
  const StreamVisit& V = w.stream.visit;
  return V.kind == SV_HERON && V.guest != 0 && (V.stage == GS_IN || V.stage == GS_STAY);
}

float streamHeronMirrorNow(const World& w) {
  const StreamVisit& V = w.stream.visit;
  return V.kind == SV_HERON && V.stage == GS_STAY ? V.mirror : 0.f;
}

bool streamVisitY(const World& w, int16_t& y) {
  HPose P;
  if (!buildDone(w.build) || !heronPose(w, P)) return false;
  y = (int16_t)(P.mode == 0 || P.mode == 3 ? P.o.y : 478);
  return true;
}

void streamDrawVisit(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  HPose P;
  if (!buildDone(w.build) || !heronPose(w, P) || !overlaps(heronRect(w), clip)) return;
  HSink k{&s, &clip, &pal, nullptr};
  if (P.mode == 1 || P.mode == 2) heronFly(k, P);
  else heronStand(k, P);
}

Rect streamVisitBounds(const World& w) {
  if (!buildDone(w.build)) return kEmpty;
  Rect r = heronRect(w);
  const Rect g = visitRing(w.stream.visit);
  if (!g.empty()) {
    if (r.empty()) r = g;
    else r = Rect{r.x0 < g.x0 ? r.x0 : g.x0, r.y0 < g.y0 ? r.y0 : g.y0, r.x1 > g.x1 ? r.x1 : g.x1, r.y1 > g.y1 ? r.y1 : g.y1};
  }
  return r;
}

// ---- public -----------------------------------------------------------------------------------

float streamPartLane(const Landscape& ls, float s, float lane, float margin) {
  float bs;
  landscapeBoulder(ls, &bs);
  const float du = (s - bs) * segLen(ls, bs);
  const float R = fl(LS_BOULDER_R);
  float e;
  if (du <= -kPartUp || du >= kPartDown) return lane;
  if (du < -R) e = easeInOut((du + kPartUp) / (kPartUp - R));
  else if (du <= R) e = 1.f;
  else e = 1.f - easeInOut((du - R) / (kPartDown - R));
  const float hw = maxf(8.f, landscapeStreamAt(ls, bs).hw);
  const float W = (kFootDepth + margin) / hw;
  const float dl = lane - LS_BOULDER_LANE;
  if (absf(dl) >= W) return lane;
  const float target = LS_BOULDER_LANE + (dl >= 0.f ? W : -W);
  return lane + (target - lane) * e;
}

float streamFlowMul(const World& w) { return 1.f + 0.6f * clamp01(w.weather.rain); }

void streamInit(World& w, const Rng& loop_rng) {
  StreamLife& L = w.stream;
  L = StreamLife{};
  Rng copy = loop_rng;   // the loop RNG is not advanced
  const uint32_t a = copy.next();
  const uint32_t b = copy.next();
  L.rng.seed(((uint64_t)b << 32) ^ (uint64_t)a ^ 0x73747265616D0000ull, 0x53545245414D0001ull);
  L.vrng.seed(((uint64_t)a << 32) ^ (uint64_t)b ^ 0x7669736974000000ull, 0x5649534954530001ull);   // the visitors'
  L.visit.slot = -1;
  L.visit.ring_t = -1.f;
  const float first_float = L.rng.range(16.f, 34.f);
  const float first_fish = L.rng.range(20.f, 50.f);
  L.next_float = (uint32_t)(first_float * fl(SS_TICK_HZ));
  L.next_fish = (uint32_t)(first_fish * fl(SS_TICK_HZ));
  for (Floater& f : L.f) f.kind = FK_NONE;
  for (Rect& r : L.d_f) r = kEmpty;
  L.d_fish = L.d_foam = L.d_mock = L.d_visit = kEmpty;
}

void streamSetInputs(World& w, bool moon_up, bool twilight, bool night, uint8_t month) {
  w.stream.moon_up = moon_up;
  w.stream.twilight = twilight;
  w.stream.night = night;
  w.stream.month = month <= 12 ? month : 0;
}

namespace {

// The season's floaters (Inputs::month): the share that are leaves, their colours (cumulative shares of
// tints 0 orange, 1 russet, 2 gold, 3 green, 4 brown) and how often anything comes (times the gap).
struct SeasonMix {
  float leaf_p;
  float cum[4];
  float gap;
};
const SeasonMix kSeasons[5] = {
    {0.56f, {0.25f, 0.5f, 0.75f, 1.f}, 1.f},      // month 0, no clock: the year-round mix
    {0.18f, {0.f, 0.25f, 0.25f, 0.25f}, 1.3f},    // winter: very few, brown (a russet one now and then)
    {0.30f, {0.f, 0.f, 0.08f, 1.f}, 1.1f},        // spring and summer: a few, green
    {0.55f, {0.18f, 0.32f, 0.58f, 1.f}, 0.9f},    // early autumn: mixed
    {0.74f, {0.36f, 0.66f, 0.95f, 1.f}, 0.65f},   // mid and late autumn: many, orange, russet, gold
};
const SeasonMix& seasonMix(uint8_t month) {
  static const uint8_t kOf[13] = {0, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 4, 1};
  return kSeasons[kOf[month <= 12 ? month : 0]];
}

void spawnFloater(World& w, Floater& f) {
  StreamLife& L = w.stream;
  const bool rain = w.weather.rain > 0.3f;
  // A fixed number of draws whatever is decided (one per statement).
  const float kind_roll = L.rng.uniform();
  const float lane = L.rng.range(kLaneMin, kLaneMax);
  const float snag_roll = L.rng.uniform();
  const float snag_lane = L.rng.range(-0.06f, 0.06f);
  const float hold = L.rng.range(3.f, 7.f);
  const bool side = L.rng.chance(0.5f);
  const float ang = L.rng.uniform();
  const float spin = L.rng.range(-1.f, 1.f);
  const float len = L.rng.uniform();
  const int tint = L.rng.rangeInt(0, 3);
  const SeasonMix& m = seasonMix(L.month);
  float leaf_p = rain ? 0.42f : 0.56f, stick_p = rain ? 0.48f : 0.34f;
  if (L.month) {   // the season's share of leaves; in rain a little less (more sticks come down)
    leaf_p = m.leaf_p * (rain ? 0.8f : 1.f);
    stick_p = (1.f - leaf_p) * 0.75f;
  }
  f = Floater{};
  f.kind = kind_roll < leaf_p ? FK_LEAF : (kind_roll < leaf_p + stick_p ? FK_STICK : FK_CONE);
  const float snag_p = (f.kind == FK_STICK ? 0.45f : (f.kind == FK_CONE ? 0.25f : 0.18f)) + (rain ? 0.1f : 0.f);
  const bool snag = snag_roll < snag_p;
  f.phase = FP_DRIFT;
  f.tint = (uint8_t)tint;
  if (L.month) {   // the season's colours, from the draws already made (tint and spin are independent)
    const float u = (fl(tint) + 0.5f * (spin + 1.f)) * 0.25f;
    uint8_t k = 0;
    while (k < 4 && u >= m.cum[k]) ++k;
    f.tint = k;
  }
  f.side = (int8_t)(side ? 1 : -1);
  f.s = 0.25f;
  f.lane = snag ? LS_BOULDER_LANE - 0.18f + snag_lane : lane;   // into its face, a little on our side
  f.hold = snag ? hold : 0.f;
  f.ang = ang;
  f.spin = (f.kind == FK_STICK ? 0.035f : 0.07f) * spin;   // sticks turn slowly
  f.len = f.kind == FK_STICK ? 13.f + 7.f * len : (f.kind == FK_LEAF ? 10.f + 3.f * len : 6.8f);
}

}  // namespace

bool streamAddStick(World& w, Vec2 at) {
  StreamLife& L = w.stream;
  int free_i = -1;
  for (int i = 0; i < STREAM_FLOATERS && free_i < 0; ++i)
    if (L.f[i].kind == FK_NONE) free_i = i;
  if (free_i < 0) return false;
  float best = 3.0e38f, bs = 0.f;
  for (int i = 5; i <= 256; ++i) {   // s from 0.25 to 12.8, every 0.05
    const float s = 0.05f * fl(i);
    const LsStreamPt p = landscapeStreamAt(w.land, s);
    const float d = (at.x - p.x) * (at.x - p.x) + (at.y - p.y) * (at.y - p.y);
    if (d < best) {
      best = d;
      bs = s;
    }
  }
  const LsStreamPt p = landscapeStreamAt(w.land, bs);
  const float hw = maxf(10.f, p.hw);
  const float across = ((at.x - p.x) * p.nx + (at.y - p.y) * p.ny) / hw;
  if (best > (hw + 2.f) * (hw + 2.f)) return false;
  Floater& f = L.f[free_i];
  f = Floater{};
  f.kind = FK_STICK;
  f.phase = FP_DRIFT;
  f.side = 1;
  f.s = bs;
  f.lane = across < kLaneMin ? kLaneMin : (across > kLaneMax ? kLaneMax : across);
  f.ang = 0.1f;
  f.spin = 0.02f;
  f.len = 9.f;
  return true;
}

namespace {

void stepFloater(World& w, Floater& f, float flow) {
  const Landscape& ls = w.land;
  const float dt = SS_TICK_DT;
  const float hw = maxf(10.f, landscapeStreamAt(ls, f.s).hw);
  const float px = kFlowPxPerSec * kFloatSpeed * (20.f / hw) * flow * dt;
  const float seg = segLen(ls, f.s);
  const float R = fl(LS_BOULDER_R);
  f.t += dt;
  switch (f.phase) {
    case FP_DRIFT: {
      f.s += px / seg;
      f.ang += f.spin * dt;
      if (f.hold > 0.f) {
        // Pressed onto the boulder's upstream face: it stops there, turned half across the current.
        float bs;
        landscapeBoulder(ls, &bs);
        const float stop = bs - (R + 1.f + 0.3f * floaterReach(f)) / segLen(ls, bs);
        if (f.s >= stop) {
          f.s = stop;
          f.phase = FP_HELD;
          f.t = 0.f;
          f.ang -= floorf(f.ang);
        }
      }
      break;
    }
    case FP_HELD: {
      const float want = f.kind == FK_STICK ? 0.07f : 0.1f;   // turned a little across the current
      f.ang += (want - f.ang) * minf(1.f, dt * 2.5f);
      if (f.t >= f.hold) {
        f.phase = FP_SWING;
        f.t = 0.f;
      }
      break;
    }
    case FP_SWING: {
      // Swings round on the side it chose and slides past, taking up speed.
      const float k0 = easeInOut(clamp01((f.t - dt) / kSwingS));
      const float k1 = easeInOut(clamp01(f.t / kSwingS));
      float bs;
      landscapeBoulder(ls, &bs);
      const float hwb = maxf(8.f, landscapeStreamAt(ls, bs).hw);
      const float out = LS_BOULDER_LANE + (float)f.side * (kFootDepth + 2.f + kFore * floaterReach(f)) / hwb;
      f.lane += (out - f.lane) * minf(1.f, (k1 - k0) / maxf(1e-3f, 1.f - k0));
      f.ang += ((float)f.side * -0.08f - f.ang) * (k1 - k0) * 1.5f;
      f.s += px / seg * k1;
      if (f.t >= kSwingS) {
        f.phase = FP_FREE;
        f.lane = out;
      }
      break;
    }
    default:
      f.s += px / seg;
      f.ang += f.spin * dt;
      break;
  }
  if (f.ang >= 1.f) f.ang -= 1.f;
  if (f.ang < 0.f) f.ang += 1.f;
  if (f.s > 12.9f) f.kind = FK_NONE;   // past the left edge
}

void startFish(World& w) {
  StreamLife& L = w.stream;
  const float kind_roll = L.rng.uniform();
  const float x = L.rng.range(kFishXMin, kFishXMax);
  const float lane = L.rng.range(kFishLaneMin, kFishLaneMax);
  const bool dir = L.rng.chance(0.5f);
  float cy, half;
  if (!landscapeStreamSpan(w.land, x, cy, half)) return;
  StreamFish& f = L.fish;
  f.kind = kind_roll < kJumpChance ? FISH_JUMP : FISH_RISE;
  f.dir = (int8_t)(dir ? 1 : -1);
  f.t = 0.f;
  f.x = (int16_t)floorf(x + 0.5f);
  f.y = (int16_t)floorf(cy - lane * half / 1.06f + 0.5f);
}

}  // namespace

void streamStep(World& w) {
  StreamLife& L = w.stream;
  if (!buildDone(w.build)) return;
  L.step = (uint16_t)(w.tick / (uint32_t)kFoamStepTicks);
  L.moon = (uint8_t)(L.moon_up ? 1u + ((L.step >> 1) & 15u) : 0u);
  const float flow = streamFlowMul(w);
  for (Floater& f : L.f)
    if (f.kind != FK_NONE) stepFloater(w, f, flow);
  if (w.tick >= L.next_float) {
    int free_i = -1;
    for (int i = 0; i < STREAM_FLOATERS && free_i < 0; ++i)
      if (L.f[i].kind == FK_NONE) free_i = i;
    if (free_i < 0) {
      L.next_float = w.tick + 2u * SS_TICK_HZ;   // both afloat: try again shortly
    } else {
      spawnFloater(w, L.f[free_i]);
      const float gap = L.rng.range(kFloatGapMin, kFloatGapMax) * (w.weather.rain > 0.3f ? kRainGap : 1.f) * seasonMix(L.month).gap;
      L.next_float = w.tick + (uint32_t)(gap * fl(SS_TICK_HZ));
    }
  }
  if (!w.react.active) visitStep(w);   // the visitors are animals: they stand still through the doorbell
  StreamFish& fish = L.fish;
  if (fish.kind != FISH_NONE) {
    fish.t += SS_TICK_DT;
    if (fish.t >= fishEnd(fish)) fish.kind = FISH_NONE;
  }
  if (w.tick >= L.next_fish) {
    if (fish.kind == FISH_NONE) startFish(w);
    const float gap = L.rng.range(kFishGapMin, kFishGapMax) * (L.twilight ? kTwilightGap : 1.f);
    L.next_fish = w.tick + (uint32_t)(gap * fl(SS_TICK_HZ));
  }
}

namespace {
void track(Renderer& r, Rect& prev, const Rect& cur, bool changed) {
  const bool same = prev.x0 == cur.x0 && prev.y0 == cur.y0 && prev.x1 == cur.x1 && prev.y1 == cur.y1;
  if (!changed && same) return;
  if (!prev.empty()) r.markDirtyRect(prev);
  if (!cur.empty()) r.markDirtyRect(cur);
  prev = cur;
}
}  // namespace

void streamTrack(World& w, Renderer& r) {
  StreamLife& L = w.stream;
  for (int i = 0; i < STREAM_FLOATERS; ++i) track(r, L.d_f[i], streamFloaterBounds(w, i), L.f[i].kind != FK_NONE);
  track(r, L.d_fish, streamFishBounds(w), L.fish.kind != FISH_NONE);
  track(r, L.d_foam, streamFoamBounds(w), L.step != L.d_step || L.moon != L.d_moon);
  track(r, L.d_mock, streamMockBounds(w), L.mock != SM_NONE || L.mock != L.d_mock_k);
  const StreamVisit& V = L.visit;
  track(r, L.d_visit, streamVisitBounds(w), (V.kind == SV_HERON && V.stage >= GS_IN && V.stage <= GS_OUT) || V.ring_t >= 0.f);
  L.d_step = L.step;
  L.d_moon = L.moon;
  L.d_mock_k = L.mock;
}

bool streamActorY(const World& w, int16_t& y) {
  if (!buildDone(w.build)) return false;
  y = (int16_t)landscapeBoulder(w.land).y;
  return true;
}

void streamDrawStaticWater(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip) {
  const Frame B = boulderFrame(ls);
  if (!overlaps(Rect{(int)B.p.x - 40, (int)B.p.y - 22, (int)B.p.x + 70, (int)B.p.y + 22}, clip)) return;
  const float R = fl(LS_BOULDER_R);
  // The calm water behind it, darker, tapering away downstream.
  const float eu[7] = {R - 4.f, R + 10.f, R + 24.f, R + 42.f, R + 24.f, R + 10.f, R - 4.f};
  const float ev[7] = {-5.f, -5.5f, -3.5f, 0.f, 3.8f, 5.6f, 5.f};
  Vec2 eddy[7];
  for (int i = 0; i < 7; ++i) eddy[i] = at(B.p, B.t, B.d, eu[i], ev[i]);
  fillPolygon(s, eddy, 7, mixRGB(pal[WATER], pal[GROUND_DARK], 0.09f), clip);
  // Its reflection: the rock's outline turned over and squashed, dark in the water under it.
  Vec2 ref[13];
  for (int i = 0; i < 13; ++i)
    ref[i] = Vec2{B.p.x + kRock[i][0] * 0.95f * fl(LS_BOULDER_R) / 16.f, B.p.y + 1.5f - kRock[i][1] * 0.28f * fl(LS_BOULDER_H) / 18.f};
  fillPolygon(s, ref, 13, mixRGB(pal[WATER], mixRGB(pal[ROCK], pal[ACTOR], 0.5f), 0.3f), clip);
  // The pillow of water heaped up in front of it.
  fillEllipse(s, at(B.p, B.t, B.d, -R - 1.f, 0.6f), 6.f, 4.2f, mixRGB(pal[WATER], pal[WATER_LIGHT], 0.3f), clip);
  const Pixel line = mixRGB(pal[WATER], pal[WATER_LIGHT], 0.7f);
  Vec2 arc[4];
  for (int i = 0; i < 4; ++i) {
    const float th = -0.16f + 0.107f * fl(i);
    arc[i] = at(B.p, B.t, B.d, -(R + 3.5f) * cosT(th), 4.6f * sinT(th));
  }
  for (int i = 0; i < 3; ++i) strokeCapsule(s, arc[i], arc[i + 1], 1.2f, line, clip);
}

void streamDrawStaticRock(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, float scale) {
  if (scale <= 0.01f) return;
  const Rect r = streamRockBounds(ls);
  Rect all = r;
  all.x1 += 80;   // the stones upstream
  all.y1 += 16;
  if (!overlaps(all, clip)) return;
  drawStones(ls, s, pal, clip, scale);
  drawBoulder(ls, s, pal, clip, scale);
}

void streamDrawWater(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!buildDone(w.build)) return;
  const StreamLife& L = w.stream;
  if (overlaps(streamFoamBounds(w), clip)) drawWake(w, s, clip, pal);
  drawFish(L.fish, s, clip, pal);
  drawVisitRing(L.visit, s, clip, pal);   // the heron's strike, the deer drinking
  for (const Floater& f : L.f)
    if (f.phase == FP_DRIFT || f.phase == FP_FREE) drawFloat(w, f, s, clip, pal);
}

void streamDrawNear(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!buildDone(w.build)) return;
  const StreamLife& L = w.stream;
  const Rect rock = streamRockBounds(w.land).clipped(clip);
  if (!rock.empty()) {
    // The rock again over whatever farther away crossed it this frame (it stands in front of them).
    const int by = (int)landscapeBoulder(w.land).y;
    Rect cover = kEmpty;
    for (int i = 0; i < AMB_GLINT_MAX; ++i) unite(cover, ambientGlintBounds(w, i).clipped(rock));
    for (int i = 0; i < STREAM_FLOATERS; ++i)
      if (L.f[i].phase == FP_DRIFT || L.f[i].phase == FP_FREE) unite(cover, streamFloaterBounds(w, i).clipped(rock));
    unite(cover, fishRect(L.fish).clipped(rock));
    unite(cover, skipBounds(w.skip).clipped(rock));
    for (int i = 0; i < WX_BITS; ++i)
      if (w.weather.bit[i].kind != BIT_NONE && w.weather.bit[i].land_y < (float)by) unite(cover, bitBounds(w.weather.bit[i]).clipped(rock));
    for (int i = 0; i < BITS_MAX; ++i)
      if (w.bits[i].kind != BIT_NONE && w.bits[i].land_y < (float)by) unite(cover, bitBounds(w.bits[i]).clipped(rock));
    for (int i = 0; i < GLYPH_MAX; ++i)
      if (w.major.g[i].kind != GL_NONE && w.major.g[i].base_y < by) unite(cover, glyphBounds(w.major.g[i]).clipped(rock));
    if (w.hiker.visible && w.hiker.feet.y < (float)by) unite(cover, w.hiker_rect.clipped(rock));
    for (int i = 0; i < CRITTER_MAX; ++i)
      if (!w.critter_rect[i].empty() && w.critters[i].feet.y < (float)by) unite(cover, w.critter_rect[i].clipped(rock));
    if (!cover.empty()) drawBoulder(w.land, s, pal, cover.clipped(clip), 1.f);
  }
  for (const Floater& f : L.f)
    if (f.phase == FP_HELD || f.phase == FP_SWING) drawFloat(w, f, s, clip, pal);
  if (overlaps(streamFoamBounds(w), clip)) drawFoam(w, s, clip, pal);
  drawMock(w, s, clip, pal);
}

Rect streamFloaterBounds(const World& w, int i) {
  if (!buildDone(w.build)) return kEmpty;
  return floaterRect(w, w.stream.f[i]);
}

Rect streamFishBounds(const World& w) { return fishRect(w.stream.fish); }

Rect streamFoamBounds(const World& w) { return foamRect(w); }

Rect streamRockBounds(const Landscape& ls) {
  const Vec2 o = landscapeBoulder(ls);
  const int R = LS_BOULDER_R, H = LS_BOULDER_H;
  // The rock's outline reaches 15 px out and 16.4 px up at scale 1; its lap line 2.5 px down.
  return Rect{(int)o.x - R - 3, (int)o.y - H - 3, (int)o.x + R + 4, (int)o.y + 5};
}

Rect streamMockBounds(const World& w) { return mockRect(w); }

void streamDrawFloater(const World& w, int i, Surface& s, const Rect& clip, const Palette& pal) {
  drawFloat(w, w.stream.f[i], s, clip, pal);
}

}  // namespace ss
