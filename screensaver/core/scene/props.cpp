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
inline Vec2 add(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
inline Vec2 sub(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }
inline Vec2 mul(Vec2 a, float k) { return Vec2{a.x * k, a.y * k}; }
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
  Vec2 body[5];   // back corner, back apex, ridge middle, front apex, front corner
  Vec2 door[3];
  bool has_door;
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
  const Vec2 ba{x + kTentBackX * spread + 0.6f * t.wobble, y - hb};
  const Vec2 fa{x + kTentFrontX * spread + t.wobble, y - hf};
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

  const Vec2 fl{x + kTentFaceX * spread, y};
  g.has_door = hf > 16.f;
  g.door[0] = mixV(fa, mixV(fl, fr, 0.5f), 0.22f);
  g.door[1] = mixV(fl, fr, 0.3f);
  g.door[2] = mixV(fl, fr, 0.8f);

  g.has_guy = p >= 0.85f && t.sag < 0.5f;
  g.guy0 = fa;
  g.guy1 = Vec2{fr.x + kGuyOut, y + 1.f};

  g.has_shadow = t.lit && t.shadow && hb > 30.f && hf > 30.f;
  for (int i = 0; i < 12; ++i) g.shadow[i] = Vec2{x + t.shadow_x + kShadow[i][0], y - 3.f + kShadow[i][1]};
}

// ---- hiker forward kinematics (mirrors hiker.cpp build()) ---------------------

constexpr float kThigh = 0.235f, kShin = 0.235f, kUpper = 0.16f, kFore = 0.15f;
constexpr float kTorsoTop = 0.235f, kShoulder = 0.25f, kNeck = 0.29f;
constexpr float kHeadUp = 0.06f, kHeadFwd = 0.012f, kHeadR = 0.062f;
constexpr float kTorsoW = 0.15f, kShinW = 0.075f, kFootW = 0.058f;
constexpr float kHeel = 0.02f, kToe = 0.07f, kFootFollow = 0.75f;
constexpr float kBreathAmp = 0.011f;

inline Vec2 dirA(float a) { return Vec2{sinT(a * kInvTwoPi), cosT(a * kInvTwoPi)}; }

// ---- pack --------------------------------------------------------------------

// The hiker's pack outline, (behind the spine, up the spine) in units of a
// 90 px figure; standing on the ground it keeps the same shape.
const float kPack[8][2] = {{0.035f, 0.045f}, {0.13f, 0.035f}, {0.175f, 0.06f}, {0.19f, 0.10f},
                           {0.19f, 0.27f},   {0.17f, 0.30f},  {0.10f, 0.32f}, {0.04f, 0.31f}};
constexpr float kPackH = 90.f, kPackMid = 0.11f, kPackFoot = 0.035f;
constexpr float kRollB = 0.15f, kRollU = 0.315f, kRollR = 0.05f;

inline Vec2 packPt(const PackProp& p, float b, float u) {
  const float sx = p.facing < 0 ? -1.f : 1.f;
  return Vec2{p.pos.x - sx * (b - kPackMid) * kPackH, p.pos.y - (u - kPackFoot) * kPackH};
}

// ---- food bag ------------------------------------------------------------------

// Sack below the neck point, from the hiker's CARRY_BAG at 90 px.
const float kSack[8][2] = {{-1.8f, 2.7f},  {1.8f, 2.7f},   {5.4f, 7.2f},   {5.9f, 11.7f},
                           {3.2f, 14.9f},  {-3.2f, 14.9f}, {-5.9f, 11.7f}, {-5.4f, 7.2f}};

void sackPts(const FoodBag& b, Vec2 out[8], Vec2& neck) {
  const float a = b.swing * kInvTwoPi;
  const float s = sinT(a), c = cosT(a);
  neck = Vec2{b.tip.x + s * b.line, b.tip.y + c * b.line};
  for (int i = 0; i < 8; ++i) {   // rotate with the line
    const float dx = kSack[i][0], dy = kSack[i][1];
    out[i] = Vec2{neck.x + c * dx + s * dy, neck.y - s * dx + c * dy};
  }
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

constexpr float kStickLen = 62.f;

// Item outline points in local px (x mirrored by facing, scaled by size), so
// draw and bounds share one table. Up to 6 points per shape.
struct Shape { int n; float p[6][2]; };
const Shape kMug = {4, {{-3.5f, 0.f}, {3.5f, 0.f}, {3.8f, -8.f}, {-3.8f, -8.f}}};
const Shape kBook = {4, {{-7.f, 0.f}, {7.f, 0.f}, {8.f, -3.5f}, {-6.f, -3.5f}}};
const Shape kPot = {4, {{-7.f, 0.f}, {7.f, 0.f}, {7.5f, -9.f}, {-7.5f, -9.f}}};
const Shape kShoe = {6, {{-6.f, 0.f}, {7.f, 0.f}, {7.f, -2.5f}, {1.f, -4.f}, {0.f, -9.f}, {-6.f, -9.f}}};

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
}

Rect tentBounds(const Tent& t) {
  if (t.state == TENT_PACKED) return kEmpty;
  TentGeom g;
  tentGeom(t, g);
  Ext e;
  for (const Vec2& p : g.body) e.pt(p);
  if (g.has_guy) e.pt(g.guy1, 1.f);
  if (g.has_shadow)
    for (const Vec2& p : g.shadow) e.pt(p);
  return e.rect();
}

void tentDraw(const Tent& t, Surface& s, const Rect& clip, const Palette& pal) {
  if (t.state == TENT_PACKED || !overlaps(tentBounds(t), clip)) return;
  TentGeom g;
  tentGeom(t, g);
  const Pixel body = t.lit ? pal[TENT_LIT] : pal[TENT];
  if (g.has_guy) strokeCapsule(s, g.guy0, g.guy1, 1.2f, mixRGB(pal[TENT], pal[ACTOR], 0.6f), clip);
  fillPolygon(s, g.body, 5, body, clip);
  if (g.has_door) fillPolygon(s, g.door, 3, mixRGB(body, pal[ACTOR], 0.45f), clip);
  if (g.has_shadow) fillPolygon(s, g.shadow, 12, pal[ACTOR], clip);
}

bool tentSame(const Tent& a, const Tent& b) {
  return sameV(a.pos, b.pos) && a.state == b.state && a.progress == b.progress && a.sag == b.sag &&
         a.bump == b.bump && a.wobble == b.wobble && a.lit == b.lit && a.shadow == b.shadow &&
         a.shadow_x == b.shadow_x;
}

// ---- pack ------------------------------------------------------------------------

Rect packBounds(const PackProp& p) {
  if (!p.shown) return kEmpty;
  Ext e;
  for (const auto& k : kPack) e.pt(packPt(p, k[0], k[1]));
  e.pt(packPt(p, kRollB, kRollU), kRollR * kPackH + 1.f);
  return e.rect();
}

void packDraw(const PackProp& p, Surface& s, const Rect& clip, const Palette& pal) {
  if (!p.shown || !overlaps(packBounds(p), clip)) return;
  Vec2 pts[8];
  for (int i = 0; i < 8; ++i) pts[i] = packPt(p, kPack[i][0], kPack[i][1]);
  fillPolygon(s, pts, 8, pal[ACTOR], clip);
  fillCircle(s, packPt(p, kRollB, kRollU), kRollR * kPackH, pal[ACTOR], clip);
}

bool packSame(const PackProp& a, const PackProp& b) {
  return sameV(a.pos, b.pos) && a.facing == b.facing && a.shown == b.shown && a.held == b.held;
}

// ---- food bag ------------------------------------------------------------------------

void bagInit(FoodBag& b, Vec2 tip) {
  b.state = BAG_NONE;
  b.tip = tip;
  b.line = 16.f;
  b.swing = 0.f;
  b.pos = tip;
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
  }
}

bool bagSame(const FoodBag& a, const FoodBag& b) {
  return a.state == b.state && sameV(a.tip, b.tip) && a.line == b.line && a.swing == b.swing && sameV(a.pos, b.pos);
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
      if (it.held) {
        const float sx = it.facing < 0 ? -1.f : 1.f;
        e.pt(it.pos, 2.f);
        e.pt(Vec2{it.pos.x - sx * kStickLen * it.size, it.ground_y - 1.f}, 2.f);
      } else {
        e.pt(itemPt(it, -0.5f * kStickLen, -1.f), 2.f);
        e.pt(itemPt(it, 0.5f * kStickLen, -2.5f), 2.f);
      }
      break;
    case ITEM_SHOE:
      e.pt(itemPt(it, -6.f, -9.f));
      e.pt(itemPt(it, 7.f, 0.f));
      break;
    case ITEM_WOOD:
      for (const auto& l : kLogs) {
        e.pt(itemPt(it, l[0], l[1]), 0.5f * kLogW * it.size);
        e.pt(itemPt(it, l[2], l[3]), 0.5f * kLogW * it.size);
      }
      break;
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
      if (it.held) {   // dragged: hands to the ground behind
        const float sx = it.facing < 0 ? -1.f : 1.f;
        strokeCapsule(s, it.pos, Vec2{it.pos.x - sx * kStickLen * it.size, it.ground_y - 1.f}, 2.6f, pal[TRUNK],
                      clip);
      } else {
        capL(it, -0.5f * kStickLen, -1.f, 0.5f * kStickLen, -2.5f, 2.6f, s, pal[TRUNK], clip);
      }
      break;
    case ITEM_SHOE:
      fillShape(it, kShoe, s, pal[ACTOR], clip);
      break;
    case ITEM_WOOD:
      for (const auto& l : kLogs) capL(it, l[0], l[1], l[2], l[3], kLogW, s, pal[TRUNK], clip);
      break;
    default:
      break;
  }
}

bool itemSame(const Item& a, const Item& b) {
  return sameV(a.pos, b.pos) && a.down == b.down && a.held == b.held && a.facing == b.facing && a.size == b.size &&
         a.ground_y == b.ground_y;
}

const char* itemName(ItemId id) {
  static const char* const kNames[ITEM_COUNT] = {"mug", "book", "pot", "stick", "shoe", "wood"};
  return id < ITEM_COUNT ? kNames[id] : "?";
}

// ---- hands and armload ------------------------------------------------------------------

Vec2 hikerHandsMid(const Hiker& h) {
  const float H = h.height;
  const HikerPose& P = h.pose;
  const float breath = (1.f - h.walk_w) * kBreathAmp * H * (0.5f + 0.5f * sinT(h.breath_phase));
  const Vec2 hip{P.dx * H, 0.f};
  const float lt = P.lean * kInvTwoPi;
  const Vec2 up{sinT(lt), -cosT(lt)};
  const Vec2 chest = add(hip, mul(up, kTorsoTop * H + breath));
  const Vec2 shoulder = add(hip, mul(up, kShoulder * H + breath));
  const Vec2 neck = add(hip, mul(up, kNeck * H + breath));
  const float ht = (P.lean + P.head) * kInvTwoPi;
  const Vec2 hu{sinT(ht), -cosT(ht)};
  const Vec2 hf{cosT(ht), sinT(ht)};
  const Vec2 head = add(neck, add(mul(hu, kHeadUp * H), mul(hf, kHeadFwd * H)));

  float low = maxf(hip.y, chest.y) + 0.5f * kTorsoW * H;
  Vec2 mid{0.f, 0.f};
  for (int i = 0; i < 2; ++i) {
    const Vec2 knee = add(hip, mul(dirA(P.thigh[i]), kThigh * H));
    const float shin = P.thigh[i] - P.knee[i];
    const Vec2 ankle = add(knee, mul(dirA(shin), kShin * H));
    const Vec2 fd = dirA(0.5f * kPi + kFootFollow * shin);
    const Vec2 heel = sub(ankle, mul(fd, kHeel * H));
    const Vec2 toe = add(ankle, mul(fd, kToe * H));
    low = maxf(low, knee.y + 0.5f * kShinW * H);
    low = maxf(low, maxf(heel.y, maxf(toe.y, ankle.y)) + 0.5f * kFootW * H);
    const Vec2 elbow = add(shoulder, mul(dirA(P.shoulder[i]), kUpper * H));
    const Vec2 hand = add(elbow, mul(dirA(P.shoulder[i] + P.elbow[i]), kFore * H));
    mid = add(mid, mul(hand, 0.5f));
  }
  low = maxf(low, head.y + kHeadR * H);
  const float sx = h.facing < 0 ? -1.f : 1.f;
  return Vec2{h.feet.x + sx * mid.x, h.feet.y - low - P.lift * H + mid.y};
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

}  // namespace ss
