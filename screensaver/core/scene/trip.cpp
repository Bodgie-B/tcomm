// The mushroom trip's own pieces. See trip.h.
//
// OWNERSHIP: skeleton from the foundation step (A1); step C5c filled it in.
//
// Every number marked PROPOSED is a starting value to tune by eye; how it looks is unverified on the panel.
#include "trip.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../gfx/raster.h"
#include "skytrip.h"
#include "world.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
const Rect kNone{0, 0, 0, 0};

Rect boxOf(float x0, float y0, float x1, float y1) {
  return Rect{(int)floorf(x0), (int)floorf(y0), (int)ceilf(x1) + 1, (int)ceilf(y1) + 1};
}
Rect join(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}

// ---- the caps ---------------------------------------------------------------------------------------------

constexpr float kHandScale = 0.70f;   // a picked cap in his hand, of the mother's 14 px (PROPOSED)

// A lifted pickable as it stands: the first rocks under his finger, the one being picked leans to the hand
// and comes free with a 1 px hop (plan 5.1 step 5, 5.2 step 6).
bool groundLook(const TripState& t, int k, ShroomLook& m) {
  if (!t.lifted || k >= (int)t.n_pick || k < (int)t.picked) return false;
  m = t.look[k];
  if (k == 0) m.tilt += t.rock;
  if (k == (int)t.pick_i) {
    const float u = t.pick_u;
    if (u >= 0.62f) return false;   // in his hand: the bunch has it
    const float lean = easeInOut(clamp01((u - 0.30f) / 0.22f));
    m.tilt -= 0.045f * lean;        // toward him (he crouches on its left)
    if (u > 0.54f) m.y -= 1.f;
  }
  return true;
}

// Cap j of n fanned in a hand at p.
ShroomLook fanLook(Vec2 p, int j, int n, float scale) {
  ShroomLook m = {};
  m.x = p.x;
  m.y = p.y + 1.f;
  m.scale = scale;
  m.step = 4;
  m.tilt = (fl(j) - 0.5f * fl(n - 1)) * 0.065f;
  m.tone = j == 1 ? 0.3f : (j == 2 ? -0.3f : 0.f);
  return m;
}
// The one he sniffs and eats: a third smaller with each bite, then only the stem.
ShroomLook nearLook(const TripState& t) {
  ShroomLook m = {};
  m.x = t.hand[0].x;
  m.y = t.hand[0].y + 1.f;
  m.step = t.bite >= 3 ? 1 : 4;
  m.scale = kHandScale * (t.bite >= 3 ? 0.55f : 1.f - 0.22f * fl(t.bite));
  return m;
}
// The bunch laid in the grass: on their sides, heads apart.
ShroomLook downLook(const TripState& t, int j) {
  ShroomLook m = {};
  m.x = t.bunch_at.x + (j == 0 ? -4.f : 5.f);
  m.y = t.bunch_at.y + (j == 0 ? 0.f : 1.f);
  m.scale = kHandScale;
  m.step = 4;
  m.tilt = j == 0 ? -0.21f : 0.23f;
  m.tone = j == 0 ? 0.f : 0.3f;
  return m;
}
ShroomLook capLook(const TripProp& p) {
  ShroomLook m = {};
  m.x = p.pos.x;
  m.y = p.pos.y;
  m.scale = kHandScale * (p.mode ? 0.9f : 1.f);
  m.step = 4;
  m.tilt = p.rot;
  return m;
}

// ---- the tree on the deer's head (plan 5.5 set 0, cut down by REVIEW_FIXES F3 to one strip's height) -------

// Two stems that fork twice: 14 limbs, 28 px tall and about 40 across at full (PROPOSED). Limb k of a side:
// 0 the stem, 1-2 its forks, 3-6 theirs. Level l grows in the l-th third of the growth, so the tips come last.
struct Limb { Vec2 a, b; float w; int level; };
constexpr int kLimbs = 14;
constexpr float kStem = 13.f, kFork = 0.68f, kSpread = 0.105f, kStemOut = 0.065f;

int antlerLimbs(const TripState& t, Limb out[kLimbs]) {
  if (t.antler_q == 0) return 0;
  const float g = fl(t.antler_q) * (1.f / 64.f);
  const Vec2 root{fl(t.antler_x), fl(t.antler_y)};
  int n = 0;
  for (int side = -1; side <= 1; side += 2) {
    Vec2 base[7];
    float ang[7];
    const int parent[7] = {-1, 0, 0, 1, 1, 2, 2};
    const int level[7] = {0, 1, 1, 2, 2, 2, 2};
    const float turn[7] = {0.f, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f};
    Vec2 tip[7];
    for (int k = 0; k < 7; ++k) {
      const int l = level[k];
      const float grow = clamp01(g * 3.f - fl(l));
      float len = kStem;
      for (int q = 0; q < l; ++q) len *= kFork;
      if (k == 0) {
        base[k] = Vec2{root.x + fl(side) * 2.f, root.y};
        ang[k] = fl(side) * kStemOut;
      } else {
        base[k] = tip[parent[k]];
        ang[k] = ang[parent[k]] + turn[k] * kSpread * (l == 2 ? 0.9f : 1.f) + fl(side) * 0.015f;
      }
      // Turns from straight up, + to the right.
      tip[k] = Vec2{base[k].x + len * grow * sinT(ang[k]), base[k].y - len * grow * cosT(ang[k])};
      if (grow <= 0.f) continue;
      out[n].a = base[k];
      out[n].b = tip[k];
      out[n].w = l == 0 ? 2.2f : (l == 1 ? 1.7f : 1.2f);
      out[n].level = l;
      ++n;
    }
  }
  return n;
}

Rect antlerBounds(const TripState& t) {
  if (t.antler_q == 0) return kNone;
  return boxOf(fl(t.antler_x) - 27.f, fl(t.antler_y) - 33.f, fl(t.antler_x) + 27.f, fl(t.antler_y) + 3.f);
}

// ---- props --------------------------------------------------------------------------------------------------

Rect propBounds(const TripProp& p) {
  switch (p.kind) {
    case TP_CAP: return patchShroomBounds(capLook(p));
    case TP_FISH: return boxOf(p.pos.x - 34.f, p.pos.y - 22.f, p.pos.x + 34.f, p.pos.y + 22.f);
    case TP_RING: {
      const float rx = 4.f + 9.f * p.k + 3.f;
      return boxOf(p.pos.x - rx, p.pos.y - 0.4f * rx - 3.f, p.pos.x + rx, p.pos.y + 0.4f * rx + 3.f);
    }
    case TP_LEAF: return boxOf(p.pos.x - 4.f, p.pos.y - 4.f, p.pos.x + 4.f, p.pos.y + 4.f);
    // The beating wing's tip reaches pos.y - 9.5 and its antialiased edge one row more (C5 review: the old
    // top at - 9 left a pixel behind as the bird fluttered down to the tree).
    case TP_BIRD: return boxOf(p.pos.x - 10.f, p.pos.y - 11.f, p.pos.x + 10.f, p.pos.y + 6.f);
    default: return kNone;
  }
}
int propPart(const TripProp& p) { return p.kind == TP_CAP ? 0 : 2; }

void propDraw(const TripProp& p, Surface& s, const Rect& clip, const Palette& pal) {
  switch (p.kind) {
    case TP_CAP:
      patchShroomDraw(capLook(p), s, pal, clip);
      break;
    case TP_FISH: {
      // A slim fish along its flight, nose first, in its pastel, with four dots trailing and thinning.
      const float vl = sqrtf(p.vel.x * p.vel.x + p.vel.y * p.vel.y);
      const Vec2 d = vl > 0.01f ? Vec2{p.vel.x / vl, p.vel.y / vl} : Vec2{-1.f, 0.f};
      const Pixel col = skyTripLit(pal, skyTripPastel(p.tint));
      for (int j = 4; j >= 1; --j) {
        const float back = 7.f + 5.5f * fl(j);
        fillCircle(s, Vec2{p.pos.x - d.x * back, p.pos.y - d.y * back + 0.6f * fl(j)}, 1.9f - 0.35f * fl(j),
                   mixRGB(pal[WATER_LIGHT], col, 0.6f), clip);
      }
      strokeCapsule(s, Vec2{p.pos.x - d.x * 5.f, p.pos.y - d.y * 5.f}, Vec2{p.pos.x + d.x * 4.f, p.pos.y + d.y * 4.f}, 5.f, col, clip);
      const Vec2 tail{p.pos.x - d.x * 8.f, p.pos.y - d.y * 8.f};
      strokeCapsule(s, Vec2{tail.x - d.y * 3.f, tail.y + d.x * 3.f}, Vec2{tail.x + d.y * 3.f, tail.y - d.x * 3.f}, 2.f, col, clip);
      break;
    }
    case TP_RING: {
      // A flat smoke ring: eight short strokes round an ellipse that widens and thins as it climbs.
      const float rx = 4.f + 9.f * p.k, ry = 0.4f * rx;
      const float wd = 2.0f - 1.4f * p.k;
      const Pixel col = mixRGB(pal[SMOKE], pal[CLOUD_LIT], 0.5f);
      Vec2 q[9];
      for (int j = 0; j <= 8; ++j) q[j] = Vec2{p.pos.x + rx * cosT(fl(j) / 8.f), p.pos.y + ry * sinT(fl(j) / 8.f)};
      for (int j = 0; j < 8; ++j) strokeCapsule(s, q[j], q[j + 1], wd, col, clip);
      break;
    }
    case TP_LEAF:
      fillCircle(s, p.pos, 2.0f * p.k, pal[FOLIAGE], clip);
      break;
    case TP_BIRD: {
      // The perch look: a small body and one wing (2 primitives). Flying, the wing beats.
      const float f = p.vel.x < 0.f ? -1.f : 1.f;
      fillEllipse(s, Vec2{p.pos.x, p.pos.y - 3.f}, 4.2f, 2.8f, pal[ACTOR], clip);
      const float up = p.mode ? 0.f : 4.5f * sinT(p.t * 5.f);
      strokeCapsule(s, Vec2{p.pos.x - f * 0.5f, p.pos.y - 3.5f}, Vec2{p.pos.x - f * 4.5f, p.pos.y - 4.f - up}, 2.f, pal[ACTOR], clip);
      break;
    }
    default: break;
  }
}

uint32_t fnv(uint32_t h, const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}
template <typename T>
uint32_t add(uint32_t h, const T& v) { return fnv(h, &v, sizeof v); }

// Everything part `part`'s draw reads, hashed.
uint32_t partKey(const TripState& t, int part) {
  uint32_t h = 2166136261u;
  if (part == 0) {
    for (int k = 0; k < 3; ++k) {
      ShroomLook m;
      const bool on = groundLook(t, k, m);
      h = add(h, on);
      if (on) {
        h = add(h, m.x); h = add(h, m.y); h = add(h, m.scale); h = add(h, m.step); h = add(h, m.wilt);
        h = add(h, m.lean); h = add(h, m.tilt); h = add(h, m.tone);
      }
    }
    h = add(h, t.bunch_down);
    if (t.bunch_down) {
      h = add(h, t.in_far);
      h = add(h, t.bunch_at.x);
      h = add(h, t.bunch_at.y);
    }
  } else if (part == 1) {
    const uint8_t far_n = t.bunch_down ? (uint8_t)0 : t.in_far;
    h = add(h, far_n);
    h = add(h, t.in_near);
    if (far_n) { h = add(h, t.hand[1].x); h = add(h, t.hand[1].y); }
    if (t.in_near) { h = add(h, t.hand[0].x); h = add(h, t.hand[0].y); h = add(h, t.bite); }
  } else {
    h = add(h, t.antler_q);
    if (t.antler_q) { h = add(h, t.antler_x); h = add(h, t.antler_y); h = add(h, t.leaves); }
  }
  for (int i = 0; i < TRIP_PROPS; ++i) {
    const TripProp& p = t.prop[i];
    if (p.kind == TP_NONE || propPart(p) != part) continue;
    h = add(h, i);
    h = add(h, p.kind); h = add(h, p.mode); h = add(h, p.tint);
    h = add(h, p.pos.x); h = add(h, p.pos.y); h = add(h, p.rot); h = add(h, p.k);
    if (p.kind == TP_FISH || p.kind == TP_BIRD) { h = add(h, p.vel.x); h = add(h, p.vel.y); }
    if (p.kind == TP_BIRD && p.mode == 0) h = add(h, p.t);
  }
  return h;
}

}  // namespace

int tripPropNew(TripState& t, uint8_t kind) {
  for (int i = 0; i < TRIP_PROPS; ++i) {
    if (t.prop[i].kind != TP_NONE) continue;
    memset(&t.prop[i], 0, sizeof(TripProp));
    t.prop[i].kind = kind;
    t.prop[i].k = 1.f;
    return i;
  }
  return -1;
}

void tripStep(TripState& t, float dt) {
  for (int i = 0; i < TRIP_PROPS; ++i) {
    TripProp& p = t.prop[i];
    if (p.kind == TP_NONE) continue;
    p.t += dt;
    if (p.kind == TP_LEAF) {
      if (p.t < 0.f) continue;   // still on its tip: they let go one by one
      // Lets go and drifts down slowly with a small side-to-side, thinning away over its last second.
      p.pos.y += p.vel.y * dt;
      p.pos.x += (p.vel.x + 5.f * cosT(p.t * 0.45f + p.rot)) * dt;
      p.k = clamp01((3.6f - p.t) / 1.2f);
      if (p.t >= 3.6f) p.kind = TP_NONE;
    }
  }
}

Vec2 tripAntlerTip(const TripState& t, int j) {
  Limb lm[kLimbs];
  const int n = antlerLimbs(t, lm);
  int seen = 0;
  for (int k = 0; k < n; ++k) {
    if (lm[k].level != 2) continue;
    if (seen == j) return lm[k].b;
    ++seen;
  }
  return Vec2{fl(t.antler_x), fl(t.antler_y) - 24.f};
}

Rect tripPartBounds(const TripState& t, int part) {
  Rect r = kNone;
  if (part == 0) {
    for (int k = 0; k < 3; ++k) {
      ShroomLook m;
      if (groundLook(t, k, m)) r = join(r, patchShroomBounds(m));
    }
    if (t.bunch_down)
      for (int j = 0; j < (int)t.in_far && j < 2; ++j) r = join(r, patchShroomBounds(downLook(t, j)));
  } else if (part == 1) {
    if (!t.bunch_down)
      for (int j = 0; j < (int)t.in_far; ++j) r = join(r, patchShroomBounds(fanLook(t.hand[1], j, t.in_far, kHandScale)));
    if (t.in_near) r = join(r, patchShroomBounds(nearLook(t)));
  } else {
    r = antlerBounds(t);
    if (t.antler_q && t.leaves) r = join(r, boxOf(fl(t.antler_x) - 30.f, fl(t.antler_y) - 36.f, fl(t.antler_x) + 30.f, fl(t.antler_y)));
  }
  for (int i = 0; i < TRIP_PROPS; ++i)
    if (t.prop[i].kind != TP_NONE && propPart(t.prop[i]) == part) r = join(r, propBounds(t.prop[i]));
  return r;
}

Rect tripBounds(const TripState& t) {
  Rect r = kNone;
  for (int part = 0; part < TRIP_PARTS; ++part) r = join(r, tripPartBounds(t, part));
  return r;
}

int tripDirty(const TripState& a, const TripState& b, Rect out[TRIP_DIRTY_MAX]) {
  int n = 0;
  for (int part = 0; part < TRIP_PARTS; ++part) {
    const Rect ra = tripPartBounds(a, part), rb = tripPartBounds(b, part);
    if (ra.empty() && rb.empty()) continue;
    const bool same_box = ra.x0 == rb.x0 && ra.y0 == rb.y0 && ra.x1 == rb.x1 && ra.y1 == rb.y1;
    if (same_box && partKey(a, part) == partKey(b, part)) continue;
    if (!ra.empty()) out[n++] = ra;
    if (!rb.empty()) out[n++] = rb;
  }
  return n;
}

void tripDraw(const TripState& t, int part, Surface& s, const Rect& clip, const Palette& pal) {
  if (part == 0) {
    for (int k = 0; k < 3; ++k) {
      ShroomLook m;
      if (groundLook(t, k, m)) patchShroomDraw(m, s, pal, clip);
    }
    if (t.bunch_down)
      for (int j = 0; j < (int)t.in_far && j < 2; ++j) patchShroomDraw(downLook(t, j), s, pal, clip);
  } else if (part == 1) {
    if (!t.bunch_down)
      for (int j = 0; j < (int)t.in_far; ++j) patchShroomDraw(fanLook(t.hand[1], j, t.in_far, kHandScale), s, pal, clip);
    if (t.in_near) patchShroomDraw(nearLook(t), s, pal, clip);
  } else if (t.antler_q) {
    Limb lm[kLimbs];
    const int n = antlerLimbs(t, lm);
    const Pixel wood = mixRGB(pal[TRUNK], pal[ACTOR], 0.3f);
    for (int k = 0; k < n; ++k) strokeCapsule(s, lm[k].a, lm[k].b, lm[k].w, wood, clip);
    int tip = 0;
    for (int k = 0; k < n; ++k) {
      if (lm[k].level != 2) continue;
      if (tip < (int)t.leaves) fillCircle(s, lm[k].b, 2.2f, pal[FOLIAGE], clip);
      ++tip;
    }
  }
  for (int i = 0; i < TRIP_PROPS; ++i)
    if (t.prop[i].kind != TP_NONE && propPart(t.prop[i]) == part) propDraw(t.prop[i], s, clip, pal);
}

// ---- the free layers ------------------------------------------------------------------------------------------

float tripSway(const World& w, float x, float plain) {
  if (w.day.kind != DAY_TRIP) return plain;
  const float ks = w.day.trip.t.ks;
  if (ks <= 0.f) return plain;
  // One slow wave the whole field leans in, crossing left to right every 5 s (plan 6.5). It is ambient: it
  // runs on the world's tick, so the grass goes on swaying through a doorbell like all the grass.
  const float wave = 2.4f * sinT((float)w.tick * (1.f / (5.f * (float)SS_TICK_HZ)) - x * (1.f / 800.f));
  return lerp(plain, wave, ks);
}

Pixel tripGlintColor(const World& w, int i, const Palette& pal) {
  const Pixel plain = pal[WATER_LIGHT];
  if (w.day.kind != DAY_TRIP) return plain;
  const TripState& t = w.day.trip.t;
  // One glint at a time takes its colour as kg comes up, and gives it back the same way.
  const float k = clamp01(t.kg * 9.f - fl(i % 8));
  if (k <= 0.f) return plain;
  const float c = t.sky_t * (1.f / 3.f);
  const int n = (int)floorf(c);
  const float frac = easeInOut(clamp01((c - fl(n) - 0.7f) / 0.3f));   // a cross-fade over the last 0.9 s of each 3
  const Pixel pa = skyTripLit(pal, skyTripPastel(i + n)), pb = skyTripLit(pal, skyTripPastel(i + n + 1));
  return mixRGB(plain, mixRGB(pa, pb, frac), 0.85f * k);
}

}  // namespace ss
