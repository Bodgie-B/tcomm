// Camp props. See props.h.
#include "props.h"

#include <math.h>

#include "../anim/ease.h"

namespace ss {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kInvTwoPi = 1.f / (2.f * kPi);

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline Vec2 mixV(Vec2 a, Vec2 b, float t) { return Vec2{lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
inline bool sameV(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }

const Rect kEmpty{0, 0, 0, 0};

// Running float extents, turned into an integer rect with a margin for the
// antialiased fringe.
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

// ---- tent ------------------------------------------------------------------

// Half width of the footprint, pole heights and apex x offsets, px.
constexpr float kTentHalfW = 38.f;
constexpr float kTentBackH = 44.f, kTentFrontH = 50.f;
constexpr float kTentBackX = -22.f, kTentFrontX = 14.f, kTentFaceX = -4.f;
constexpr float kGuyOut = 16.f;   // guy line peg beyond the front corner

struct TentGeom {
  Vec2 body[9];   // back corner, back apex, ridge middle, front apex, front corner (+ a bite)
  int n_body;     // 5, or 9 with the mouse hole
  Vec2 door[3];
  bool has_door;
  Vec2 flap[3];   // the open flap (door > 0): over the dark opening `door`
  bool has_flap;
  Vec2 guy0, guy1;
  bool has_guy;
  Vec2 shadow[12];
  bool has_shadow;
};

// The hiker's head and shoulders, about 28 px tall, base at y 0.
const float kShadow[12][2] = {{-10.f, 0.f},  {-9.f, -11.f}, {-4.f, -15.f}, {-3.f, -18.f},
                              {-5.f, -22.f}, {-3.f, -27.f}, {2.f, -28.f},  {5.f, -24.f},
                              {4.f, -18.f},  {5.f, -15.f},  {10.f, -11.f}, {10.f, 0.f}};

void tentGeom(const Tent& t, TentGeom& g) {
  const float p = t.progress;
  const float spread = 0.55f + 0.45f * easeOutCubic(window(p, 0.f, 0.15f));
  float back = easeInOut(window(p, 0.12f, 0.5f));
  float front = easeInOut(window(p, 0.42f, 0.85f));
  back *= (1.f - 0.85f * t.sag) * (1.f + t.bump);
  front *= (1.f - 0.92f * t.sag) * (1.f + t.bump);
  const float x = t.pos.x, y = t.pos.y;
  const float hb = maxf(3.f, kTentBackH * back), hf = maxf(4.f, kTentFrontH * front);
  const Vec2 bl{x - kTentHalfW * spread, y};
  const float lean = t.wobble + t.sway;
  const Vec2 ba{x + kTentBackX * spread + 0.6f * lean, y - hb};
  const Vec2 fa{x + kTentFrontX * spread + lean, y - hf};
  const Vec2 fr{x + kTentHalfW * spread, y};
  // The ridge droops between the poles while slack: sagging, or front pole not up yet.
  Vec2 mid = mixV(ba, fa, 0.5f);
  mid.y += 12.f * t.sag + 5.f * (1.f - minf(1.f, front)) * minf(1.f, back);
  mid.y = minf(mid.y, y - 2.5f);   // a slumped tent is a low heap, never a dent below the ground
  g.body[0] = bl;
  g.body[1] = ba;
  g.body[2] = mid;
  g.body[3] = fa;
  g.body[4] = fr;
  g.n_body = 5;
  if (t.bite && p >= 0.85f) {   // a ragged arch out of the base of the back panel
    const float bx = x - 0.3f * kTentHalfW * spread;
    g.body[5] = Vec2{bx + 4.5f, y};
    g.body[6] = Vec2{bx + 2.5f, y - 5.5f};
    g.body[7] = Vec2{bx - 2.f, y - 6.f};
    g.body[8] = Vec2{bx - 4.5f, y};
    g.n_body = 9;
  }

  const Vec2 fl{x + kTentFaceX * spread, y};
  g.has_door = hf > 16.f;
  g.door[0] = mixV(fa, mixV(fl, fr, 0.5f), 0.22f);
  g.door[1] = mixV(fl, fr, 0.3f);
  g.door[2] = mixV(fl, fr, 0.8f);
  // Unzipped: the flap's free corner rolls from the bottom left up toward the
  // right side of the opening (it stays inside the door triangle, so inside
  // the body's bounds).
  g.has_flap = g.has_door && t.door > 0.f;
  if (g.has_flap) {
    const float k = t.door > 1.f ? 1.f : t.door;
    const Vec2 roll = mixV(g.door[2], g.door[0], 0.42f);
    g.flap[0] = g.door[0];
    g.flap[1] = mixV(g.door[1], mixV(roll, g.door[2], 0.35f), k);
    g.flap[2] = g.door[2];
  }

  g.has_guy = p >= 0.85f && t.sag < 0.5f;
  g.guy0 = fa;
  g.guy1 = Vec2{fr.x + kGuyOut, y + 1.f};

  g.has_shadow = t.lit && t.shadow && hb > 30.f && hf > 30.f;
  for (int i = 0; i < 12; ++i)
    g.shadow[i] = Vec2{x + t.shadow_x + t.shadow_shake + kShadow[i][0], y - 3.f + t.shadow_y + kShadow[i][1]};
}

// ---- pack --------------------------------------------------------------------

// The hiker's pack outline, (behind the spine, up the spine) in units of a
// 90 px figure; standing on the ground it keeps the same shape.
const float kPack[8][2] = {{0.035f, 0.045f}, {0.13f, 0.035f}, {0.175f, 0.06f}, {0.19f, 0.10f},
                           {0.19f, 0.27f},   {0.17f, 0.30f},  {0.10f, 0.32f}, {0.04f, 0.31f}};
constexpr float kPackH = 90.f, kPackMid = 0.11f, kPackFoot = 0.035f;
constexpr float kRollB = 0.15f, kRollU = 0.315f, kRollR = 0.05f;

inline Vec2 packPt(const PackProp& p, float b, float u) {
  const float sx = p.facing < 0 ? -1.f : 1.f;
  const float H = kPackH * p.pop;   // (exact at pop 1: the evening's pack is what it was)
  return Vec2{p.pos.x - sx * (b - kPackMid) * H, p.pos.y - (u - kPackFoot) * H};
}

// ---- food bag ------------------------------------------------------------------

// Sack below the neck point, from the hiker's CARRY_BAG at 90 px.
const float kSack[8][2] = {{-1.8f, 2.7f},  {1.8f, 2.7f},   {5.4f, 7.2f},   {5.9f, 11.7f},
                           {3.2f, 14.9f},  {-3.2f, 14.9f}, {-5.9f, 11.7f}, {-5.4f, 7.2f}};

// The sack below `neck`, turned by `swing` radians.
void sackAt(Vec2 neck, float swing, Vec2 out[8]) {
  const float a = swing * kInvTwoPi;
  const float s = sinT(a), c = cosT(a);
  for (int i = 0; i < 8; ++i) {
    const float dx = kSack[i][0], dy = kSack[i][1];
    out[i] = Vec2{neck.x + c * dx + s * dy, neck.y - s * dx + c * dy};
  }
}

void sackPts(const FoodBag& b, Vec2 out[8], Vec2& neck) {
  const float a = b.swing * kInvTwoPi;
  neck = Vec2{b.tip.x + sinT(a) * b.line, b.tip.y + cosT(a) * b.line};
  sackAt(neck, b.swing, out);
}

// ---- cairn ----------------------------------------------------------------------

const float kCairnDx[CAIRN_MAX] = {0.f, 1.f, -1.2f, 1.4f, -0.6f, 0.9f, -0.2f};

// Stone k's center and half sizes; the newest stone drops in with `pop`.
void cairnStone(const Cairn& c, int k, Vec2& ctr, float& rx, float& ry) {
  float y = c.pos.y;
  for (int j = 0; j < k; ++j) y -= 2.f * (2.7f - 0.15f * (float)j) - 0.8f;
  rx = 8.f - 0.85f * (float)k;
  ry = 2.7f - 0.15f * (float)k;
  float drop = 0.f;
  if (k == c.n - 1 && c.pop < 1.f) drop = -12.f * (1.f - easeOutBack(c.pop));
  ctr = Vec2{c.pos.x + kCairnDx[k], y - ry + drop};
}

// ---- items ------------------------------------------------------------------------

constexpr float kStickLen = kItemStickLen;

// A stick a critter holds: along `angle` turns from straight up through pos,
// `grip` of it ahead of pos (behind the line's direction), x squeezed with the
// holder's turn.
void stickEnds(const Item& it, Vec2& a, Vec2& b) {
  const float sx = (it.facing < 0 ? -1.f : 1.f) * it.squeeze;
  const Vec2 d{sx * kStickLen * sinT(it.angle), -kStickLen * cosT(it.angle)};
  a = Vec2{it.pos.x - it.grip * d.x, it.pos.y - it.grip * d.y};
  b = Vec2{a.x + d.x, a.y + d.y};
}

// Item outline points in local px (x mirrored by facing, scaled by size), so
// draw and bounds share one table. Up to 6 points per shape.
struct Shape { int n; float p[6][2]; };
const Shape kMug = {4, {{-3.5f, 0.f}, {3.5f, 0.f}, {3.8f, -8.f}, {-3.8f, -8.f}}};
const Shape kBook = {4, {{-7.f, 0.f}, {7.f, 0.f}, {8.f, -3.5f}, {-6.f, -3.5f}}};
const Shape kPot = {4, {{-7.f, 0.f}, {7.f, 0.f}, {7.5f, -9.f}, {-7.5f, -9.f}}};
const Shape kShoe = {6, {{-6.f, 0.f}, {7.f, 0.f}, {7.f, -2.5f}, {1.f, -4.f}, {0.f, -9.f}, {-6.f, -9.f}}};

// Fork: handle and two tines (x0, y0, x1, y1, width), standing in a hand or lying flat.
const float kForkUp[3][5] = {{0.f, 1.f, 0.f, -7.f, 1.5f}, {-1.1f, -7.f, -1.3f, -10.5f, 1.0f}, {1.1f, -7.f, 1.3f, -10.5f, 1.0f}};
const float kForkFlat[3][5] = {{-5.f, -1.f, 3.f, -1.f, 1.5f}, {3.f, -2.1f, 6.5f, -2.3f, 1.0f}, {3.f, 0.1f, 6.5f, 0.3f, 1.0f}};

inline Vec2 itemPt(const Item& it, float x, float y) {
  const float sx = it.facing < 0 ? -1.f : 1.f;
  return Vec2{it.pos.x + sx * x * it.size, it.pos.y + y * it.size};
}

void fillShape(const Item& it, const Shape& sh, Surface& s, Pixel c, const Rect& clip) {
  Vec2 p[6];
  for (int i = 0; i < sh.n; ++i) p[i] = itemPt(it, sh.p[i][0], sh.p[i][1]);
  fillPolygon(s, p, sh.n, c, clip);
}

void capL(const Item& it, float x0, float y0, float x1, float y1, float w, Surface& s, Pixel c,
          const Rect& clip) {
  strokeCapsule(s, itemPt(it, x0, y0), itemPt(it, x1, y1), w * it.size, c, clip);
}

// ---- the morning stay's props (ItemId PAN, CAKE, BRUSH, SACK; look UNVERIFIED) ----------

// Pan: the bowl centred at x 0 (22 px across, 3.6 high), the handle out to +x, tilted by `angle`
// turns about the handle's end so the bowl lifts. A pancake in it is a flat ellipse in TENT_LIT.
constexpr float kPanGripX = 26.f, kPanGripY = -3.f;
Vec2 panPt(const Item& it, float x, float y) {
  const float dx = x - kPanGripX, dy = y - kPanGripY;
  const float c = cosT(it.angle), s = sinT(it.angle);
  return itemPt(it, kPanGripX + dx * c - dy * s, kPanGripY + dy * c + dx * s);
}
constexpr int kEllN = 10;
// A flat ellipse of half sizes rx, ry at local (cx, cy), n points.
void ellipsePts(const Item& it, bool pan, float cx, float cy, float rx, float ry, Vec2 out[kEllN]) {
  for (int i = 0; i < kEllN; ++i) {
    const float a = (float)i / (float)kEllN;
    const float x = cx + rx * cosT(a), y = cy + ry * sinT(a);
    out[i] = pan ? panPt(it, x, y) : itemPt(it, x, y);
  }
}
// A free pancake: turned `angle` turns, `fill` how flat-on to us (the flip squeezes it edge-on).
void cakePts(const Item& it, float rx, float ry, Vec2 out[kEllN]) {
  const float c = cosT(it.angle), s = sinT(it.angle);
  for (int i = 0; i < kEllN; ++i) {
    const float a = (float)i / (float)kEllN;
    const float x = rx * cosT(a), y = ry * sinT(a);
    out[i] = Vec2{it.pos.x + x * c - y * s, it.pos.y + x * s + y * c};
  }
}
inline float cakeRx(const Item& it) { return 7.5f * it.size; }
inline float cakeRy(const Item& it) { return (0.7f + 2.4f * it.fill) * it.size; }

// The stuff sack standing on the ground at pos (local px, base at y 0): its body, its mouth, and
// up to three ellipses of sleeping bag coming out of the mouth.
struct SackG {
  float w, h;          // half width and height of the body
  int n;               // bag ellipses
  float cx[3], cy[3], rx[3], ry[3];
};
void sackGeom(const Item& it, SackG& g) {
  const float fill = it.fill < 0.f ? 0.f : (it.fill > 1.f ? 1.f : it.fill);
  const float sq = it.squash < 0.f ? 0.f : (it.squash > 1.f ? 1.f : it.squash);
  const float pf = it.puff < 0.f ? 0.f : (it.puff > 1.f ? 1.f : it.puff);
  g.h = (6.f + 15.f * fill) * (1.f - 0.5f * sq);
  g.w = 6.5f + 2.5f * fill + 3.5f * sq;
  g.n = 0;
  if (pf > 0.02f) {   // the bulge, then all of it
    g.cx[0] = 0.f;
    g.cy[0] = -g.h - 2.f - 6.f * pf;
    g.rx[0] = 3.5f + 8.f * pf;
    g.ry[0] = 2.5f + 6.f * pf;
    g.n = 1;
    if (pf > 0.4f) {
      g.cx[1] = -5.f * pf;
      g.cy[1] = -g.h - 1.f - 3.f * pf;
      g.rx[1] = 1.f + 5.f * pf;
      g.ry[1] = 1.f + 3.5f * pf;
      g.cx[2] = 5.5f * pf;
      g.cy[2] = -g.h - 1.f - 2.f * pf;
      g.rx[2] = 1.f + 5.f * pf;
      g.ry[2] = 1.f + 3.f * pf;
      g.n = 3;
    }
  }
}

// The hat off his head, standing on its brim (local px, base at y 0): the
// brim, and the crown with its dent, as on the rig at 90 px.
constexpr float kHatBrim[4] = {-9.f, -1.5f, 12.f, -1.5f};
constexpr float kHatBrimW = 3.1f;
const Shape kHatCrown = {5, {{-6.f, -2.f}, {6.f, -2.f}, {5.f, -9.2f}, {0.f, -8.2f}, {-5.f, -9.2f}}};

// The fish on its stick, in the stick's frame (x out along it from the far
// end, y across), at size 1: about 14 px, nose outward.
const float kFishBody[8][2] = {{7.f, 0.f}, {4.8f, -2.4f}, {0.f, -3.f}, {-4.3f, -1.9f},
                               {-5.8f, 0.f}, {-4.3f, 1.7f}, {0.f, 2.7f}, {4.8f, 2.1f}};
const float kFishTail[3][2] = {{-5.f, 0.f}, {-9.6f, -3.6f}, {-9.1f, 3.6f}};

// Stick end points: from pos along `angle` turns from up, + toward facing.
void fishStick(const Item& it, Vec2& a, Vec2& b, Vec2& d) {
  const float sx = it.facing < 0 ? -1.f : 1.f;
  d = Vec2{sx * sinT(it.angle), -cosT(it.angle)};
  a = it.pos;
  b = Vec2{a.x + d.x * kItemFishStick, a.y + d.y * kItemFishStick};
}
// A point of the fish (fx along the stick from its tip, fy across), scaled.
inline Vec2 fishPt(Vec2 tip, Vec2 d, float k, float fx, float fy) {
  return Vec2{tip.x + d.x * fx * k - d.y * fy * k, tip.y + d.y * fx * k + d.x * fy * k};
}
// The rod lying: centred on pos, turned `angle` turns off flat.
void rodEnds(const Item& it, Vec2& a, Vec2& b) {
  const float sx = it.facing < 0 ? -1.f : 1.f;
  const float h = 0.5f * kItemRodLen * it.size;
  const Vec2 d{sx * cosT(it.angle), -sinT(it.angle)};
  a = Vec2{it.pos.x - d.x * h, it.pos.y - 1.5f - d.y * h};
  b = Vec2{it.pos.x + d.x * h, it.pos.y - 1.5f + d.y * h};
}

// Wood pile logs: (x0, y0, x1, y1) at size 1, width 5.
const float kLogs[3][4] = {{-11.f, -2.6f, 11.f, -3.4f}, {-9.f, -7.2f, 12.f, -6.6f}, {-6.f, -11.6f, 8.f, -11.8f}};
constexpr float kLogW = 5.f;

}  // namespace

// ---- tent ------------------------------------------------------------------

void tentInit(Tent& t, Vec2 pos) {
  t.pos = pos;
  t.state = TENT_PACKED;
  t.progress = 0.f;
  t.sag = 0.f;
  t.bump = 0.f;
  t.wobble = 0.f;
  t.lit = false;
  t.shadow = false;
  t.shadow_x = 0.f;
  t.shadow_y = 0.f;
  t.shadow_jitter = 0.f;
  t.shadow_shake = 0.f;
  t.bite = false;
  t.door = 0.f;
  t.sway = 0.f;
  t.snow = 0.f;
}

namespace {
// Snow on the tent: drawn only on a pitched tent, over the ridge and the
// upper part of the near face (down to snow / 3 of the way to the base).
bool tentSnowy(const Tent& t) { return t.snow > 0.01f && t.progress >= 0.85f && t.state != TENT_PACKED; }
}  // namespace

Rect tentBounds(const Tent& t) {
  if (t.state == TENT_PACKED) return kEmpty;
  TentGeom g;
  tentGeom(t, g);
  Ext e;
  for (int i = 0; i < g.n_body; ++i) e.pt(g.body[i]);
  if (g.has_guy) e.pt(g.guy1, 1.f);
  if (g.has_shadow)
    for (const Vec2& p : g.shadow) e.pt(p);
  if (tentSnowy(t))
    for (int i = 1; i <= 3; ++i) e.pt(g.body[i], 2.f);
  return e.rect();
}

void tentDraw(const Tent& t, Surface& s, const Rect& clip, const Palette& pal) {
  if (t.state == TENT_PACKED || !overlaps(tentBounds(t), clip)) return;
  TentGeom g;
  tentGeom(t, g);
  const Pixel body = t.lit ? pal[TENT_LIT] : pal[TENT];
  if (g.has_guy) strokeCapsule(s, g.guy0, g.guy1, 1.2f, mixRGB(pal[TENT], pal[ACTOR], 0.6f), clip);
  fillPolygon(s, g.body, g.n_body, body, clip);
  if (g.has_flap) {   // the dark inside, then the flap rolled back over its right side
    fillPolygon(s, g.door, 3, mixRGB(body, pal[ACTOR], 0.85f), clip);
    fillPolygon(s, g.flap, 3, mixRGB(body, pal[ACTOR], 0.3f), clip);
  } else if (g.has_door) {
    fillPolygon(s, g.door, 3, mixRGB(body, pal[ACTOR], 0.45f), clip);
  }
  if (g.has_shadow) fillPolygon(s, g.shadow, 12, pal[ACTOR], clip);
  if (tentSnowy(t)) {
    const float d = 0.33f * (t.snow > 1.f ? 1.f : t.snow);
    const Vec2 ba = g.body[1], mid = g.body[2], fa = g.body[3];
    const Vec2 cap[6] = {ba, mid, fa, mixV(fa, g.body[4], d), Vec2{mid.x, mid.y + d * (g.body[4].y - fa.y)},
                         mixV(ba, g.body[0], d)};
    const Pixel snow = pal[WATER_LIGHT];
    fillPolygon(s, cap, 6, mixRGB(body, snow, 0.55f + 0.3f * d), clip);
    const float wd = 1.f + 1.6f * d;
    strokeCapsule(s, ba, mid, wd, snow, clip);
    strokeCapsule(s, mid, fa, wd, snow, clip);
  }
}

Vec2 tentRidgeEnd(const Tent& t, int k) {
  TentGeom g;
  tentGeom(t, g);
  return k == 0 ? g.body[1] : g.body[3];
}

bool tentSame(const Tent& a, const Tent& b) {
  return sameV(a.pos, b.pos) && a.state == b.state && a.progress == b.progress && a.sag == b.sag &&
         a.bump == b.bump && a.wobble == b.wobble && a.lit == b.lit && a.shadow == b.shadow &&
         a.shadow_x == b.shadow_x && a.shadow_y == b.shadow_y && a.shadow_shake == b.shadow_shake && a.bite == b.bite &&
         a.door == b.door && a.sway == b.sway && a.snow == b.snow;
}

// ---- pack ------------------------------------------------------------------------

Rect packBounds(const PackProp& p) {
  if (!p.shown || p.pop <= 0.02f) return kEmpty;
  Ext e;
  for (const auto& k : kPack) e.pt(packPt(p, k[0], k[1]));
  e.pt(packPt(p, kRollB, kRollU), kRollR * kPackH * p.pop + 1.f);
  return e.rect();
}

void packDraw(const PackProp& p, Surface& s, const Rect& clip, const Palette& pal) {
  if (!p.shown || !overlaps(packBounds(p), clip)) return;
  Vec2 pts[8];
  for (int i = 0; i < 8; ++i) pts[i] = packPt(p, kPack[i][0], kPack[i][1]);
  fillPolygon(s, pts, 8, pal[ACTOR], clip);
  fillCircle(s, packPt(p, kRollB, kRollU), kRollR * kPackH * p.pop, pal[ACTOR], clip);
}

bool packSame(const PackProp& a, const PackProp& b) {
  return sameV(a.pos, b.pos) && a.facing == b.facing && a.shown == b.shown && a.held == b.held && a.pop == b.pop;
}

// ---- food bag ------------------------------------------------------------------------

void bagInit(FoodBag& b, Vec2 tip) {
  b.state = BAG_NONE;
  b.tip = tip;
  b.line = 16.f;
  b.swing = 0.f;
  b.pos = tip;
  b.rope = tip;
}

Rect bagBounds(const FoodBag& b) {
  Ext e;
  if (b.state == BAG_HUNG) {
    Vec2 sack[8], neck;
    sackPts(b, sack, neck);
    e.pt(b.tip, 1.f);
    for (const Vec2& p : sack) e.pt(p);
  } else if (b.state == BAG_DROPPED) {
    e.pt(Vec2{b.pos.x - 8.f, b.pos.y - 11.f});
    e.pt(Vec2{b.pos.x + 14.f, b.pos.y + 1.f});
  } else if (b.state == BAG_FLYING) {
    Vec2 sack[8];
    sackAt(b.pos, b.swing, sack);
    e.pt(b.pos, 1.f);
    e.pt(b.rope, 1.f);
    for (const Vec2& p : sack) e.pt(p);
  }
  return e.rect();
}

void bagDraw(const FoodBag& b, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(bagBounds(b), clip)) return;
  if (b.state == BAG_HUNG) {
    Vec2 sack[8], neck;
    sackPts(b, sack, neck);
    strokeCapsule(s, b.tip, neck, 1.2f, pal[ACTOR], clip);
    fillPolygon(s, sack, 8, pal[ACTOR], clip);
  } else if (b.state == BAG_DROPPED) {   // on its side, neck to the right
    fillEllipse(s, Vec2{b.pos.x, b.pos.y - 5.f}, 7.5f, 5.f, pal[ACTOR], clip);
    strokeCapsule(s, Vec2{b.pos.x + 6.f, b.pos.y - 5.5f}, Vec2{b.pos.x + 11.f, b.pos.y - 3.5f}, 2.6f, pal[ACTOR],
                  clip);
  } else if (b.state == BAG_FLYING) {
    Vec2 sack[8];
    sackAt(b.pos, b.swing, sack);
    strokeCapsule(s, b.rope, b.pos, 1.2f, pal[ACTOR], clip);
    fillPolygon(s, sack, 8, pal[ACTOR], clip);
  }
}

bool bagSame(const FoodBag& a, const FoodBag& b) {
  return a.state == b.state && sameV(a.tip, b.tip) && a.line == b.line && a.swing == b.swing && sameV(a.pos, b.pos) &&
         sameV(a.rope, b.rope);
}

// ---- cairn ----------------------------------------------------------------------------

Rect cairnBounds(const Cairn& c) {
  if (c.n == 0) return kEmpty;
  Ext e;
  for (int k = 0; k < c.n && k < CAIRN_MAX; ++k) {
    Vec2 ctr;
    float rx, ry;
    cairnStone(c, k, ctr, rx, ry);
    e.pt(Vec2{ctr.x - rx * 1.1f, ctr.y - ry * 1.1f});
    e.pt(Vec2{ctr.x + rx * 1.1f, ctr.y + ry * 1.1f});
  }
  return e.rect();
}

void cairnDraw(const Cairn& c, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(cairnBounds(c), clip)) return;
  for (int k = 0; k < c.n && k < CAIRN_MAX; ++k) {
    Vec2 ctr;
    float rx, ry;
    cairnStone(c, k, ctr, rx, ry);
    fillEllipse(s, ctr, rx, ry, pal[ROCK], clip);
  }
}

bool cairnSame(const Cairn& a, const Cairn& b) { return sameV(a.pos, b.pos) && a.n == b.n && a.pop == b.pop; }

// ---- items -------------------------------------------------------------------------------

Rect itemBounds(ItemId id, const Item& it) {
  if (!it.down && !it.held) return kEmpty;
  Ext e;
  switch (id) {
    case ITEM_MUG:
      e.pt(itemPt(it, -3.8f, -8.f));
      e.pt(itemPt(it, 5.6f, 0.f));
      break;
    case ITEM_BOOK:
      e.pt(itemPt(it, -7.f, -3.5f));
      e.pt(itemPt(it, 8.f, 0.f));
      break;
    case ITEM_POT:
      e.pt(itemPt(it, -9.5f, -10.f));
      e.pt(itemPt(it, 12.8f, 0.f));
      break;
    case ITEM_STICK:
      if (it.held && it.critter) {
        Vec2 a, b;
        stickEnds(it, a, b);
        e.pt(a, 2.f);
        e.pt(b, 2.f);
      } else if (it.held) {
        const float sx = it.facing < 0 ? -1.f : 1.f;
        e.pt(it.pos, 2.f);
        e.pt(Vec2{it.pos.x - sx * kStickLen * it.size, it.ground_y - 1.f}, 2.f);
      } else {
        e.pt(itemPt(it, -0.5f * kStickLen, -1.f), 2.f);
        e.pt(itemPt(it, 0.5f * kStickLen, -2.5f), 2.f);
      }
      break;
    case ITEM_SHOE:
      e.pt(itemPt(it, it.alt ? -20.f : -6.f, -9.f));
      e.pt(itemPt(it, 7.f, 0.f));
      break;
    case ITEM_PAN:
      e.pt(panPt(it, -12.f, -3.6f), 1.5f);
      e.pt(panPt(it, 12.f, 0.f), 1.5f);
      e.pt(panPt(it, 26.f, -3.f), 1.5f);
      if (it.fill > 0.5f) e.pt(panPt(it, 0.f, -6.f), 1.5f);
      break;
    case ITEM_CAKE:
      if (it.alt) {
        e.pt(itemPt(it, -14.f, -6.f));
        e.pt(itemPt(it, 14.f, 10.f));
      } else {
        e.pt(it.pos, cakeRx(it) + 1.f);
      }
      break;
    case ITEM_BRUSH:
      e.pt(itemPt(it, -3.f, -5.f));
      e.pt(itemPt(it, 11.f, 2.f));
      break;
    case ITEM_SACK: {
      SackG g;
      sackGeom(it, g);
      e.pt(itemPt(it, -g.w - 1.f, -g.h - 4.f));
      e.pt(itemPt(it, g.w + 1.f, 1.f));
      for (int k = 0; k < g.n; ++k) {
        e.pt(itemPt(it, g.cx[k] - g.rx[k], g.cy[k] - g.ry[k]));
        e.pt(itemPt(it, g.cx[k] + g.rx[k], g.cy[k] + g.ry[k]));
      }
      break;
    }
    case ITEM_WOOD:
      for (const auto& l : kLogs) {
        e.pt(itemPt(it, l[0], l[1]), 0.5f * kLogW * it.size);
        e.pt(itemPt(it, l[2], l[3]), 0.5f * kLogW * it.size);
      }
      break;
    case ITEM_FORK:
      for (const auto& l : (it.held ? kForkUp : kForkFlat)) {
        e.pt(itemPt(it, l[0], l[1]), 0.5f * l[4] * it.size);
        e.pt(itemPt(it, l[2], l[3]), 0.5f * l[4] * it.size);
      }
      break;
    case ITEM_ROD: {
      Vec2 a, b;
      rodEnds(it, a, b);
      e.pt(a, 2.5f);
      e.pt(b, 2.5f);
      break;
    }
    case ITEM_HAT:
      e.pt(itemPt(it, kHatBrim[0], -9.2f), 0.5f * kHatBrimW);
      e.pt(itemPt(it, kHatBrim[2], 0.f), 0.5f * kHatBrimW);
      break;
    case ITEM_FISH: {
      Vec2 a, b, d;
      fishStick(it, a, b, d);
      e.pt(a, 1.5f);
      e.pt(b, 1.5f);
      e.pt(b, 10.f * (it.size > 0.05f ? it.size : 0.75f) + 1.f);
      break;
    }
    default:
      break;
  }
  return e.rect();
}

void itemDraw(ItemId id, const Item& it, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(itemBounds(id, it), clip)) return;
  switch (id) {
    case ITEM_MUG:
      fillShape(it, kMug, s, pal[ACTOR], clip);
      capL(it, 4.6f, -6.4f, 4.6f, -2.6f, 2.2f, s, pal[ACTOR], clip);
      break;
    case ITEM_BOOK:
      fillShape(it, kBook, s, pal[ACTOR], clip);
      capL(it, -5.f, -1.7f, 6.f, -1.7f, 1.1f, s, pal[ACTOR_LIGHT], clip);
      break;
    case ITEM_POT:
      fillShape(it, kPot, s, pal[ACTOR], clip);
      capL(it, -8.5f, -9.f, 8.5f, -9.f, 2.f, s, pal[ACTOR], clip);
      capL(it, 7.5f, -7.f, 12.f, -8.f, 1.6f, s, pal[ACTOR], clip);
      break;
    case ITEM_STICK:
      if (it.held && it.critter) {   // held by a critter
        Vec2 a, b;
        stickEnds(it, a, b);
        strokeCapsule(s, a, b, 2.6f, pal[TRUNK], clip);
      } else if (it.held) {   // dragged: hands to the ground behind
        const float sx = it.facing < 0 ? -1.f : 1.f;
        strokeCapsule(s, it.pos, Vec2{it.pos.x - sx * kStickLen * it.size, it.ground_y - 1.f}, 2.6f, pal[TRUNK],
                      clip);
      } else {
        capL(it, -0.5f * kStickLen, -1.f, 0.5f * kStickLen, -2.5f, 2.6f, s, pal[TRUNK], clip);
      }
      break;
    case ITEM_SHOE:
      fillShape(it, kShoe, s, pal[ACTOR], clip);
      if (it.alt) {   // the pair: the other one lies beside it, a little behind
        Item o = it;
        o.pos = itemPt(it, -13.f, -1.f);
        fillShape(o, kShoe, s, pal[ACTOR], clip);
      }
      break;
    case ITEM_PAN: {
      const Vec2 bowl[4] = {panPt(it, -10.f, 0.f), panPt(it, 10.f, 0.f), panPt(it, 12.f, -3.6f), panPt(it, -12.f, -3.6f)};
      fillPolygon(s, bowl, 4, pal[ACTOR], clip);
      strokeCapsule(s, panPt(it, 12.f, -2.6f), panPt(it, 26.f, -3.f), 2.2f * it.size, pal[ACTOR], clip);
      if (it.fill > 0.5f) {
        Vec2 p[kEllN];
        ellipsePts(it, true, 0.f, -3.9f, 8.5f, 1.7f, p);
        fillPolygon(s, p, kEllN, pal[TENT_LIT], clip);
      }
      break;
    }
    case ITEM_CAKE: {
      Vec2 p[kEllN];
      if (it.alt) {   // draped over his hat: the top, and the two edges hanging down
        ellipsePts(it, false, 0.f, 0.f, 10.f, 2.6f, p);
        fillPolygon(s, p, kEllN, pal[TENT_LIT], clip);
        fillEllipse(s, itemPt(it, -9.5f, 3.5f), 2.6f * it.size, 5.f * it.size, pal[TENT_LIT], clip);
        fillEllipse(s, itemPt(it, 9.5f, 3.5f), 2.6f * it.size, 5.f * it.size, pal[TENT_LIT], clip);
      } else {
        cakePts(it, cakeRx(it), cakeRy(it), p);
        if (it.fill > 0.2f) {   // its browned underside shows below the top
          Vec2 q[kEllN];
          for (int i = 0; i < kEllN; ++i) q[i] = Vec2{p[i].x, p[i].y + 1.2f * it.size};
          fillPolygon(s, q, kEllN, mixRGB(pal[TENT], pal[ACTOR], 0.4f), clip);
        }
        fillPolygon(s, p, kEllN, pal[TENT_LIT], clip);
      }
      break;
    }
    case ITEM_BRUSH:
      strokeCapsule(s, itemPt(it, -2.f, 0.f), itemPt(it, 8.f, -2.f), 1.5f * it.size, pal[ACTOR_LIGHT], clip);
      strokeCapsule(s, itemPt(it, 6.5f, -2.1f), itemPt(it, 9.5f, -2.6f), 3.f * it.size, pal[ACTOR_LIGHT], clip);
      break;
    case ITEM_SACK: {
      SackG g;
      sackGeom(it, g);
      const Vec2 body[6] = {itemPt(it, -g.w, 0.f),          itemPt(it, g.w, 0.f),           itemPt(it, 0.95f * g.w, -0.55f * g.h),
                            itemPt(it, 0.6f * g.w, -g.h),   itemPt(it, -0.6f * g.w, -g.h),  itemPt(it, -0.95f * g.w, -0.55f * g.h)};
      fillPolygon(s, body, 6, pal[ACTOR], clip);
      for (int k = 0; k < g.n; ++k)
        fillEllipse(s, itemPt(it, g.cx[k], g.cy[k]), g.rx[k] * it.size, g.ry[k] * it.size, pal[TENT], clip);
      strokeCapsule(s, itemPt(it, -0.62f * g.w, -g.h), itemPt(it, 0.62f * g.w, -g.h), 2.2f * it.size, pal[ACTOR], clip);
      if (it.alt && g.n == 0) fillCircle(s, itemPt(it, 0.f, -g.h - 1.5f), 2.2f * it.size, pal[ACTOR], clip);   // the knot
      break;
    }
    case ITEM_WOOD:
      for (const auto& l : kLogs) capL(it, l[0], l[1], l[2], l[3], kLogW, s, pal[TRUNK], clip);
      break;
    case ITEM_FORK:
      for (const auto& l : (it.held ? kForkUp : kForkFlat)) capL(it, l[0], l[1], l[2], l[3], l[4], s, pal[ACTOR], clip);
      break;
    case ITEM_ROD: {   // a thin line with the reel near its butt; the stick is thicker, TRUNK
      Vec2 a, b;
      rodEnds(it, a, b);
      if (it.alt) {
        strokeCapsule(s, a, b, 2.6f, pal[TRUNK], clip);
      } else {
        strokeCapsule(s, a, b, 1.6f, pal[ACTOR], clip);
        fillCircle(s, mixV(a, b, 0.14f), 2.f, pal[ACTOR], clip);
      }
      break;
    }
    case ITEM_HAT:
      capL(it, kHatBrim[0], kHatBrim[1], kHatBrim[2], kHatBrim[3], kHatBrimW, s, pal[ACTOR], clip);
      fillShape(it, kHatCrown, s, pal[ACTOR], clip);
      break;
    case ITEM_FISH: {   // the stick, and the fish skewered on its far end (or what is left of it)
      Vec2 a, b, d;
      fishStick(it, a, b, d);
      strokeCapsule(s, a, b, 1.8f, pal[TRUNK], clip);
      const Pixel c = pal[ACTOR];
      if (it.size > 0.05f) {
        const float k = it.size;
        // Skewered lengthwise: the body centred a little in from the tip.
        const Vec2 m = fishPt(b, d, 1.f, -2.f, 0.f);
        Vec2 p[8];
        for (int i = 0; i < 8; ++i) p[i] = fishPt(m, d, k, kFishBody[i][0], kFishBody[i][1]);
        fillPolygon(s, p, 8, c, clip);
        for (int i = 0; i < 3; ++i) p[i] = fishPt(m, d, k, kFishTail[i][0], kFishTail[i][1]);
        fillPolygon(s, p, 3, c, clip);
      } else {   // the skeleton: head, spine, two ribs and the tail
        const Vec2 m = fishPt(b, d, 1.f, -2.f, 0.f);
        const float k = 0.75f;
        fillCircle(s, fishPt(m, d, k, 5.5f, 0.f), 2.2f, c, clip);
        strokeCapsule(s, fishPt(m, d, k, 4.f, 0.f), fishPt(m, d, k, -6.f, 0.f), 1.1f, c, clip);
        strokeCapsule(s, fishPt(m, d, k, 1.5f, -3.f), fishPt(m, d, k, 1.5f, 3.f), 1.f, c, clip);
        strokeCapsule(s, fishPt(m, d, k, -2.f, -2.6f), fishPt(m, d, k, -2.f, 2.6f), 1.f, c, clip);
        strokeCapsule(s, fishPt(m, d, k, -6.f, 0.f), fishPt(m, d, k, -9.f, -2.8f), 1.f, c, clip);
        strokeCapsule(s, fishPt(m, d, k, -6.f, 0.f), fishPt(m, d, k, -9.f, 2.8f), 1.f, c, clip);
      }
      break;
    }
    default:
      break;
  }
}

bool itemSame(const Item& a, const Item& b) {
  return sameV(a.pos, b.pos) && a.down == b.down && a.held == b.held && a.facing == b.facing && a.size == b.size &&
         a.ground_y == b.ground_y && a.on_fire == b.on_fire && a.critter == b.critter && a.angle == b.angle && a.grip == b.grip &&
         a.squeeze == b.squeeze && a.alt == b.alt && a.fill == b.fill && a.puff == b.puff && a.squash == b.squash;
}

const char* itemName(ItemId id) {
  static const char* const kNames[ITEM_COUNT] = {"mug", "book", "pot", "stick", "shoe", "wood", "fork",
                                                  "rod", "hat", "fish", "pan", "cake", "brush", "sack"};
  return id < ITEM_COUNT ? kNames[id] : "?";
}

// ---- hands and armload ------------------------------------------------------------------

// ---- small effects -------------------------------------------------------------------

namespace {

// Steam: puffs leave the pot kSteamGap apart and each lives kSteamLife,
// rising kSteamRise px (fast, then slowing) and fanning out side to side.
constexpr float kSteamGap = 0.16f, kSteamLife = 0.85f, kSteamRise = 34.f;

bool steamOn(const Steam& s) { return s.t >= 0.f && s.t < kSteamGap * (float)s.n + kSteamLife; }

bool steamPuff(const Steam& s, int i, Vec2& c, float& r) {
  const float a = s.t - kSteamGap * (float)i;
  if (s.t < 0.f || a <= 0.f || a >= kSteamLife) return false;
  const float u = a / kSteamLife;
  r = (1.5f + 3.5f * minf(1.f, u * 4.f)) * (1.f - u * u);
  if (r < 0.6f) return false;
  const float side = (i & 1) ? 1.f : -1.f;
  c = Vec2{s.at.x + side * (1.5f + 6.f * u) + 1.5f * (float)(i - 2), s.at.y - kSteamRise * easeOutCubic(u)};
  return true;
}

// Skip: the first flight is longer; each hop after it is shorter and lower.
constexpr float kSkipFlight = 0.45f, kSkipHop = 0.3f, kSkipDecay = 0.8f, kRingLife = 1.6f;

void skipStone(const Skip& k, Vec2& c, bool& flying) {
  flying = false;
  c = k.p[0];
  for (int i = 1; i <= k.n && i <= SKIP_MAX; ++i) {
    const float t0 = skipHitTime(k, i - 1), t1 = skipHitTime(k, i);
    if (k.t >= t0 && k.t < t1) {
      const float u = (k.t - t0) / (t1 - t0);
      const float h = i == 1 ? 8.f : 6.f * powf(0.7f, (float)(i - 2));
      c = Vec2{lerp(k.p[i - 1].x, k.p[i].x, u), lerp(k.p[i - 1].y, k.p[i].y, u) - 4.f * h * u * (1.f - u)};
      flying = true;
      return;
    }
  }
}

// Leaf outline along +x from the stem end, and the stem.
const float kLeaf[6][2] = {{0.f, 0.f}, {1.6f, -1.7f}, {4.2f, -2.1f}, {6.8f, 0.f}, {4.2f, 1.9f}, {1.6f, 1.5f}};

void leafPts(const Leaf& l, Vec2 out[6], Vec2& stem) {
  const float c = cosT(l.rot), sn = sinT(l.rot);
  for (int i = 0; i < 6; ++i)
    out[i] = Vec2{l.pos.x + c * kLeaf[i][0] - sn * kLeaf[i][1], l.pos.y + sn * kLeaf[i][0] + c * kLeaf[i][1]};
  stem = Vec2{l.pos.x - c * 2.f, l.pos.y - sn * 2.f};
}

}  // namespace

Rect steamBounds(const Steam& s) {
  if (!steamOn(s)) return kEmpty;
  Ext e;
  e.pt(Vec2{s.at.x - 16.f, s.at.y - kSteamRise - 6.f});
  e.pt(Vec2{s.at.x + 16.f, s.at.y + 6.f});
  return e.rect();
}

void steamDraw(const Steam& s, Surface& dst, const Rect& clip, const Palette& pal) {
  if (!overlaps(steamBounds(s), clip)) return;
  for (int i = 0; i < s.n; ++i) {
    Vec2 c;
    float r;
    if (steamPuff(s, i, c, r)) fillEllipse(dst, c, r, r * 0.85f, pal[SMOKE], clip);
  }
}

bool steamSame(const Steam& a, const Steam& b) { return sameV(a.at, b.at) && a.t == b.t && a.n == b.n; }

float skipHitTime(const Skip& k, int i) {
  if (i <= 0) return 0.f;
  float t = kSkipFlight, hop = kSkipHop;
  for (int j = 2; j <= i && j <= SKIP_MAX; ++j) {
    t += hop;
    hop *= kSkipDecay;
  }
  (void)k;
  return t;
}

float skipEndTime(const Skip& k) { return skipHitTime(k, k.n) + kRingLife; }

Rect skipBounds(const Skip& k) {
  if (k.t < 0.f || k.t >= skipEndTime(k) || k.n == 0) return kEmpty;
  Ext e;
  for (int i = 0; i <= k.n && i <= SKIP_MAX; ++i) {
    e.pt(Vec2{k.p[i].x - 15.f, k.p[i].y - 10.f});
    e.pt(Vec2{k.p[i].x + 15.f, k.p[i].y + 6.f});
  }
  return e.rect();
}

void skipDraw(const Skip& k, Surface& dst, const Rect& clip, const Palette& pal) {
  if (!overlaps(skipBounds(k), clip)) return;
  for (int i = 1; i <= k.n && i <= SKIP_MAX; ++i) {
    const float age = k.t - skipHitTime(k, i);
    if (age < 0.f || age >= kRingLife) continue;
    const float u = age / kRingLife;
    const float r = 3.f + 11.f * easeOutCubic(u);
    const float th = 0.6f + 1.6f * (1.f - u);
    fillEllipse(dst, k.p[i], r, 0.32f * r, pal[WATER_LIGHT], clip);
    const float ri = r - th, ryi = 0.32f * r - 0.6f * th;
    if (ri > 0.5f && ryi > 0.4f) fillEllipse(dst, k.p[i], ri, ryi, pal[WATER], clip);
  }
  Vec2 c;
  bool flying;
  skipStone(k, c, flying);
  if (flying) fillEllipse(dst, c, 1.8f, 1.3f, pal[ACTOR], clip);
}

bool skipSame(const Skip& a, const Skip& b) {
  if (a.t != b.t || a.n != b.n) return false;
  for (int i = 0; i <= SKIP_MAX; ++i)
    if (!sameV(a.p[i], b.p[i])) return false;
  return true;
}

Rect leafBounds(const Leaf& l) {
  if (!l.on) return kEmpty;
  Ext e;
  e.pt(l.pos, 7.5f);
  return e.rect();
}

void leafDraw(const Leaf& l, Surface& dst, const Rect& clip, const Palette& pal) {
  if (!overlaps(leafBounds(l), clip)) return;
  Vec2 p[6], stem;
  leafPts(l, p, stem);
  fillPolygon(dst, p, 6, pal[ACTOR], clip);
  strokeCapsule(dst, stem, l.pos, 0.9f, pal[ACTOR], clip);
}

bool leafSame(const Leaf& a, const Leaf& b) {
  return a.on == b.on && sameV(a.pos, b.pos) && a.rot == b.rot && a.base_y == b.base_y;
}

// ---- falling and puffing bits ---------------------------------------------------------

namespace {
constexpr float kConeG = 900.f;   // px/s^2: quick, cartoon gravity
constexpr float kConeRx = 3.2f, kConeRy = 4.4f;

float dustRadius(const Bit& b) {
  const float u = b.t / BIT_DUST_S;
  return u >= 1.f ? 0.f : 7.f * sinT(0.5f * u) * (0.6f + 0.6f * u);
}

// A ring on the water: 3 px spreading to 14 px (the skipped stone's), times its size.
float ringRadius(const Bit& b) {
  const float u = minf(1.f, b.t / BIT_RING_S);
  return (3.f + 11.f * easeOutCubic(u)) * b.spin;
}
}  // namespace

void bitStep(Bit& b, float dt) {
  if (b.kind == BIT_NONE) return;
  b.t += dt;
  switch (b.kind) {
    case BIT_LEAF:
    case BIT_MOTH:
    case BIT_BEE: return;   // placed by their owner
    case BIT_RING:
      if (b.t >= BIT_RING_S) b.kind = BIT_NONE;
      return;
    case BIT_DIRT:
    case BIT_DROP:
      b.vel.y += (b.kind == BIT_DIRT ? 520.f : 700.f) * dt;
      b.pos.x += b.vel.x * dt;
      b.pos.y += b.vel.y * dt;
      if (b.vel.y > 0.f && b.pos.y >= b.ground_y) {
        if (b.kind == BIT_DIRT) {
          b.kind = BIT_NONE;
        } else {   // into the water: a small ring where it went in
          b.kind = BIT_RING;
          b.pos.y = b.ground_y;
          b.vel = Vec2{0.f, 0.f};
          b.t = 0.f;
          b.spin = 0.5f;
          b.land_y = b.ground_y;
        }
      }
      return;
    default: break;
  }
  if (b.kind == BIT_DUST) {
    b.pos.x += b.vel.x * dt;
    b.pos.y += b.vel.y * dt;
    if (b.t >= BIT_DUST_S) b.kind = BIT_NONE;
    return;
  }
  if (b.resting) return;
  b.vel.y += kConeG * dt;
  b.pos.x += b.vel.x * dt;
  b.pos.y += b.vel.y * dt;
  b.spin += dt * (b.vel.x >= 0.f ? 1.6f : -1.6f);
  if (b.pos.y < b.ground_y) return;
  b.pos.y = b.ground_y;
  if (b.ground_y < b.land_y) {   // off a head: away to one side and down to the ground
    b.vel.y = -0.4f * b.vel.y;
    if (b.vel.x == 0.f) b.vel.x = 45.f;
    b.ground_y = b.land_y;
    ++b.bounces;
    return;
  }
  if (b.bounces >= 2 || b.vel.y < 140.f) {   // comes to rest on its side
    b.resting = true;
    b.vel = Vec2{0.f, 0.f};
    b.spin = 0.25f;
    return;
  }
  ++b.bounces;
  b.vel.y = -0.3f * b.vel.y;
  b.vel.x *= 0.5f;
}

Rect bitBounds(const Bit& b) {
  Ext e;
  switch (b.kind) {
    case BIT_CONE: e.pt(Vec2{b.pos.x, b.pos.y - kConeRy}, kConeRy + 0.5f); break;
    case BIT_DUST: {
      const float r = dustRadius(b);
      if (r > 0.3f) e.pt(b.pos, 1.3f * r + 0.5f);
      break;
    }
    case BIT_DIRT:
    case BIT_DROP: e.pt(b.pos, 1.5f); break;
    case BIT_RING: {
      const float r = ringRadius(b);
      e.pt(Vec2{b.pos.x - r, b.pos.y - 0.32f * r});
      e.pt(Vec2{b.pos.x + r, b.pos.y + 0.32f * r});
      break;
    }
    case BIT_LEAF: e.pt(b.pos, 7.5f); break;
    case BIT_MOTH: e.pt(b.pos, 4.f); break;
    case BIT_BEE: e.pt(b.pos, 3.f); break;
    default: break;
  }
  return e.rect();
}

void bitDraw(const Bit& b, Surface& dst, const Rect& clip, const Palette& pal) {
  if (!overlaps(bitBounds(b), clip)) return;
  if (b.kind == BIT_CONE) {   // an upright or tumbling oval resting on pos
    const float a = b.spin - floorf(b.spin);
    const float k = fabsf(cosT(a));
    const float rx = kConeRx + (kConeRy - kConeRx) * (1.f - k), ry = kConeRy - (kConeRy - kConeRx) * (1.f - k);
    fillEllipse(dst, Vec2{b.pos.x, b.pos.y - ry}, rx, ry, pal[TRUNK], clip);
  } else if (b.kind == BIT_DUST) {
    const float r = dustRadius(b);
    if (r > 0.3f) fillEllipse(dst, b.pos, r * 1.3f, r * 0.8f, pal[SMOKE], clip);
  } else if (b.kind == BIT_DIRT) {
    fillCircle(dst, b.pos, 1.2f, pal[ROCK], clip);
  } else if (b.kind == BIT_DROP) {
    fillCircle(dst, b.pos, 1.2f, pal[ACTOR], clip);
  } else if (b.kind == BIT_RING) {   // as the skipped stone's rings, sized by spin
    const float u = b.t / BIT_RING_S;
    const float r = ringRadius(b);
    const float th = (0.6f + 1.6f * (1.f - u)) * (0.5f + 0.5f * b.spin);
    fillEllipse(dst, b.pos, r, 0.32f * r, pal[WATER_LIGHT], clip);
    const float ri = r - th, ryi = 0.32f * r - 0.6f * th;
    if (ri > 0.5f && ryi > 0.4f) fillEllipse(dst, b.pos, ri, ryi, pal[WATER], clip);
  } else if (b.kind == BIT_LEAF) {
    leafDraw(Leaf{true, b.pos, b.spin, b.land_y}, dst, clip, pal);
  } else if (b.kind == BIT_MOTH || b.kind == BIT_BEE) {
    // A body and two wings beating (moth ~8 Hz, slow enough to read; bee a blur).
    const bool moth = b.kind == BIT_MOTH;
    const float f = sinT(b.t * (moth ? 8.f : 21.f));
    const float span = moth ? 1.6f + 1.2f * f : 1.3f + 0.4f * f;
    const float up = moth ? 1.0f + 0.8f * f : 1.2f;
    fillEllipse(dst, Vec2{b.pos.x - span, b.pos.y - up}, moth ? 1.5f : 0.9f, moth ? 1.1f : 0.7f,
                moth ? pal[ACTOR] : pal[CLOUD_LIT], clip);
    fillEllipse(dst, Vec2{b.pos.x + span, b.pos.y - up}, moth ? 1.5f : 0.9f, moth ? 1.1f : 0.7f,
                moth ? pal[ACTOR] : pal[CLOUD_LIT], clip);
    fillEllipse(dst, b.pos, moth ? 1.0f : 1.3f, moth ? 1.5f : 1.0f, pal[ACTOR], clip);
  }
}

bool bitSame(const Bit& a, const Bit& b) {
  if (a.kind != b.kind) return false;
  if (a.kind == BIT_NONE) return true;
  if (a.kind == BIT_DUST || a.kind == BIT_RING || a.kind == BIT_MOTH || a.kind == BIT_BEE)
    return a.t == b.t && sameV(a.pos, b.pos) && a.spin == b.spin;
  return sameV(a.pos, b.pos) && a.spin == b.spin && a.resting == b.resting;
}

Vec2 hikerHandsMid(const Hiker& h) {
  const Vec2 a = hikerHandPos(h, 0), b = hikerHandPos(h, 1);
  return Vec2{0.5f * (a.x + b.x), 0.5f * (a.y + b.y)};
}

namespace {
// Extra logs above the carried three, (x0, y0, x1, y1) in units of height from
// the hands' midpoint, x forward.
const float kArmload[4][4] = {{-0.13f, -0.115f, 0.10f, -0.125f},
                              {-0.10f, -0.155f, 0.14f, -0.150f},
                              {-0.15f, -0.195f, 0.08f, -0.205f},
                              {-0.06f, -0.240f, 0.11f, -0.228f}};
constexpr float kArmloadW = 0.05f;

void armloadLog(const Hiker& h, Vec2 m, int i, float k, Vec2& a, Vec2& b) {
  const float sx = h.facing < 0 ? -1.f : 1.f;
  const float H = h.height;
  // Logs settle onto the pile from above as k rises.
  const float dy = -(1.f - k) * 0.08f * H;
  a = Vec2{m.x + sx * kArmload[i][0] * H, m.y + kArmload[i][1] * H + dy};
  b = Vec2{m.x + sx * kArmload[i][2] * H, m.y + kArmload[i][3] * H + dy};
}
}  // namespace

Rect armloadBounds(const Hiker& h, float k) {
  if (k <= 0.f || !h.visible) return kEmpty;
  const Vec2 m = hikerHandsMid(h);
  Ext e;
  for (int i = 0; i < 4; ++i) {
    Vec2 a, b;
    armloadLog(h, m, i, k, a, b);
    e.pt(a, 0.5f * kArmloadW * h.height);
    e.pt(b, 0.5f * kArmloadW * h.height);
  }
  return e.rect();
}

void armloadDraw(const Hiker& h, float k, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(armloadBounds(h, k), clip)) return;
  const Vec2 m = hikerHandsMid(h);
  for (int i = 0; i < 4; ++i) {
    Vec2 a, b;
    armloadLog(h, m, i, k, a, b);
    strokeCapsule(s, a, b, kArmloadW * h.height, pal[ACTOR], clip);
  }
}

// ---- the rustle ---------------------------------------------------------------------------

namespace {
constexpr int kSprays = 6, kFlecks = 3;
// Where each needle spray sits against the pine's edge point (dx, dy), and each lit fleck inside it.
const float kSprayAt[kSprays][2] = {{-1.f, -21.f}, {2.f, -12.f}, {0.f, -3.f}, {3.f, 6.f}, {0.f, 14.f}, {2.f, 21.f}};
const float kFleckAt[kFlecks][2] = {{-9.f, -15.f}, {-6.f, -2.f}, {-10.f, 12.f}};

// How hard it shakes: in over a third of a second, out over the last second.
float rustleAmp(const Rustle& r) { return easeOutCubic(window(r.t, 0.f, 0.35f)) * (1.f - easeInOut(window(r.t, RUSTLE_S - 1.2f, RUSTLE_S))); }
Vec2 rustleJit(const Rustle& r, float amp, int i, float sx, float sy) {
  return Vec2{amp * sx * sinT(r.t * 6.1f + 0.37f * (float)i), amp * sy * sinT(r.t * 4.7f + 0.61f * (float)i)};
}
}  // namespace

Rect rustleBounds(const Rustle& r) {
  if (r.t < 0.f || r.t >= RUSTLE_S) return kEmpty;
  return Rect{(int)floorf(r.at.x) - 16, (int)floorf(r.at.y) - 30, (int)ceilf(r.at.x) + 20, (int)ceilf(r.at.y) + 30};
}

void rustleDraw(const Rustle& r, Surface& dst, const Rect& clip, const Palette& pal) {
  if (!overlaps(rustleBounds(r), clip)) return;
  const float amp = rustleAmp(r);
  for (int i = 0; i < kSprays; ++i) {   // twig sprays: a needle triangle pointing out of the edge
    const Vec2 j = rustleJit(r, amp, i, 1.6f, 1.1f);
    const Vec2 o{r.at.x + kSprayAt[i][0] + j.x, r.at.y + kSprayAt[i][1] + j.y};
    const Vec2 tri[3] = {Vec2{o.x - 5.f, o.y - 3.2f}, Vec2{o.x + 7.f + 1.5f * j.x, o.y + 0.8f * j.y}, Vec2{o.x - 5.f, o.y + 3.2f}};
    fillPolygon(dst, tri, 3, pal[FOLIAGE_DARK], clip);
  }
  for (int i = 0; i < kFlecks; ++i) {   // lit flecks inside the dark foliage, flickering
    const Vec2 j = rustleJit(r, amp, i + kSprays, 1.4f, 1.f);
    fillEllipse(dst, Vec2{r.at.x + kFleckAt[i][0] + j.x, r.at.y + kFleckAt[i][1] + j.y}, 2.3f, 1.3f, pal[FOLIAGE], clip);
  }
}

bool rustleSame(const Rustle& a, const Rustle& b) { return a.at.x == b.at.x && a.at.y == b.at.y && a.t == b.t; }

}  // namespace ss
