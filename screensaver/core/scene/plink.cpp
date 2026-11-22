// The plinking. See plink.h.
//
// Everything that is drawn is first turned into a short list of shapes (Sh) in half pixels, one
// builder for both layers; the draw, the bounds and the dirty marks all read that list, so they cannot
// disagree (the hiker rig does the same). The dirty marks hash the shapes of each owner (a can, a bit,
// the rifle...) before and after and mark the union of the owner's old and new bounds when they differ.
#include "plink.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"

namespace ss {

namespace {

const Rect kEmpty{0, 0, 0, 0};

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool finite(float v) { return v - v == 0.f; }
inline float fin(float v, float otherwise) { return finite(v) ? v : otherwise; }
inline float absf(float v) { return v < 0.f ? -v : v; }

constexpr float kPi = 3.14159265f;
constexpr float kHalfPi = 1.57079633f;
constexpr float kInvTwoPi = 1.f / (2.f * kPi);
constexpr float kMaxDt = 0.1f;

inline float sinA(float a) { return sinT(a * kInvTwoPi); }
inline float cosA(float a) { return cosT(a * kInvTwoPi); }

inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }

// atan2 without libm (as archery.cpp: the PC and the board agree).
float atanUnit(float x) {
  const float x2 = x * x;
  return x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f + x2 * 0.0208351f))));
}
float atan2A(float y, float x) {
  const float ax = absf(x), ay = absf(y);
  if (ax < 1.0e-9f && ay < 1.0e-9f) return 0.f;
  float r = ay <= ax ? atanUnit(ay / ax) : kHalfPi - atanUnit(ax / ay);
  if (x < 0.f) r = kPi - r;
  return y < 0.f ? -r : r;
}
float wrapPi(float a) { return a - 2.f * kPi * floorf((a + kPi) * kInvTwoPi); }
float lerpA(float a, float b, float t) { return a + wrapPi(b - a) * t; }

inline Vec2 add(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
inline Vec2 mul(Vec2 a, float k) { return Vec2{a.x * k, a.y * k}; }
inline Vec2 lerpV(Vec2 a, Vec2 b, float t) { return Vec2{lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }

// ---- tuning (PROPOSED; how it looks is UNVERIFIED) ---------------------------------------------------------

constexpr float kGravity = 650.f;        // cartoon quick (the archery's cone uses 900)
constexpr float kBulletS = 0.12f;        // the bullet's flight, 3 ticks
constexpr float kFlashS = 0.08f;         // 2 ticks
constexpr float kFollowS = 0.8f;
constexpr float kSwingS = 0.4f;
constexpr float kRaiseFirstS = 0.8f, kRaiseS = 0.5f;
constexpr float kHoldFirstS = 1.6f, kHoldMin = 0.9f, kHoldVar = 0.4f;
constexpr float kLowerS = 0.6f;
constexpr float kKneelS = 0.8f;
// The long shot (Mike, 2026-11-18): lying down takes three times as long as kneeling, in three stages
// (plinkBend), and prone the rifle in the low carry comes up to lie nearly level, never muzzle into the
// ground.
constexpr float kProneS = 2.4f;
constexpr float kProneCarryElev = 0.10f;
constexpr float kTeeterS = 0.9f;

// The cans, tip to trunk: offset from the branch's root x, size, how it pops.
struct CanDef {
  float dx;
  uint8_t w, h, style;
};
const CanDef kCans[PLINK_CANS] = {
    {-76.f, 5, 7, 0},    // a small paste tin: the last shot, B's stubborn one
    {-64.f, 7, 11, 0},   // a soda can
    {-51.f, 9, 6, 2},    // a squat tuna tin: it spins flat like a frisbee
    {-39.f, 8, 12, 0},   // a tall soup can: A's gag
    {-27.f, 7, 9, 0},    // a bean can
    {-15.f, 10, 13, 5},  // the big coffee can: it tumbles slowly end over end
};

// The rifle (px). Stock and action show on his black silhouette in TRUNK and ACTOR.
constexpr float kButtToCarryHand = 22.f;    // low carry: the near hand holds it at its balance point
constexpr float kButtToGrip = 10.f;         // shouldered: the trigger hand at the grip
constexpr float kPieceStock = 27.f, kPieceBarrel = 23.f;   // the two halves
constexpr float kStockW = 3.4f, kActionW = 3.0f, kBarrelW = 1.8f, kMagW = 1.7f;
// The long shot's scope (PROPOSED): a capsule over the action, from 48 % to 88 % of the stock-and-action
// piece, its axis this far above the rifle's.
constexpr float kScopeA = 0.48f, kScopeB = 0.88f, kScopeUp = 2.5f, kScopeW = 2.4f;
// The shooting bag is the sack drawn this much bigger in his hand (Mike, 2026-11-18: a stuffed bag he rests
// the rifle on); lying, it is a soft pillow 23 px wide and 15 px tall, wider than tall so it reads as a bag
// and not a ball (review of step B5), laid about 46 px ahead of his hips so the forend and his forearm
// rest on it.
constexpr float kBagScale = 1.7f;
constexpr float kBagRx = 11.5f, kBagRy = 7.5f;
// Prone, between shots, the rifle rests on the bag with his near hand on the wrist of the stock, this far
// from the butt (the low carry holds it at its balance point).
constexpr float kButtToWrist = 9.f;

// His arm (units of height; mirrors hiker.cpp kUpper, kFore: keep in step).
constexpr float kUpperL = 0.16f, kForeL = 0.15f;

// ---- small helpers ------------------------------------------------------------------------------------------

float branchSlope(const PlinkState& s) {
  const float run = s.branch_root.x - s.branch_tip.x;
  return run > 1.f ? (s.branch_tip.y - s.branch_root.y) / run : 0.f;
}

// The branch's top face at x, a half pixel into the bark.
float branchTop(const PlinkState& s, float x) { return plinkBranchY(s, x) - 2.0f; }

float canVext(const PlinkCan& c) { return absf(cosA(c.rot)) * 0.5f * (float)c.h + absf(sinA(c.rot)) * 0.5f * (float)c.w; }

Vec2 stickDir(const PlinkState& s) { return Vec2{(float)s.dir * sinA(s.stick_ang), cosA(s.stick_ang)}; }
Vec2 stickTip(const PlinkState& s) { return add(s.near_hand, mul(stickDir(s), kPlinkStickReach)); }

// Where can i is to be aimed at: its centre (a can that left the branch aims where it is).
Vec2 aimPoint(const PlinkState& s, int i) { return plinkCanPos(s, i); }

float restRot(const PlinkCan& c) {
  const bool tall = (float)c.h > 1.25f * (float)c.w;
  if (tall) {   // on its side: the nearest odd quarter turn
    const float q = (c.rot / kHalfPi - 1.f) * 0.5f;
    return kHalfPi * (2.f * floorf(q + 0.5f) + 1.f);
  }
  return kPi * floorf(c.rot / kPi + 0.5f);   // squat: upright (or flipped)
}

// Launch can i from where it is: up at vy0, arriving at land_x on `land_y` (the resting point of its bottom).
void launch(PlinkCan& c, float vy0, float turns, float land_x, float land_y, float sign) {
  const float top = c.y + 0.5f * (float)c.h;   // (its centre now, its bottom about to leave)
  const float fall = maxf(1.f, land_y - top);
  const float tf = (-vy0 + sqrtf(vy0 * vy0 + 2.f * kGravity * fall)) / kGravity;
  c.mode = CAN_FLY;
  c.vy = vy0;
  c.vx = (land_x - c.x) / maxf(0.2f, tf);
  c.spin = sign * turns * 2.f * kPi;
  c.land_x = land_x;
  c.land_y = land_y;
  c.bounces = 0;
  c.t = 0.f;
  c.wob = 0.f;
  c.aux = 0;
}

void pairSpot(const PlinkState& s, int i, float& x, float& y) {
  const int p = i / 2;
  x = s.pair_x[p] + ((i & 1) ? 7.f : -7.f);
  y = s.pair_y[p] + ((i & 1) ? 2.f : 0.f);
}

void popCan(PlinkState& s, int i, float r) {
  PlinkCan& c = s.can[i];
  if (c.mode != CAN_ON) return;
  float lx, ly;
  pairSpot(s, i, lx, ly);
  const float sign = (i & 1) ? -1.f : 1.f;
  float turns = 1.2f + 1.2f * r;
  if (c.style == 5) turns = 0.6f;
  launch(c, -(150.f + 60.f * r), turns, lx, ly, sign);
  c.rot = 0.f;
}

void weakPop(PlinkCan& c) {
  c.mode = CAN_FLY;
  c.aux = 1;
  c.vx = 0.f;
  c.vy = -90.f;
  c.spin = 0.f;
  c.bounces = 0;
  c.t = 0.f;
  c.wob = 0.f;
}

void rockCan(PlinkCan& c, float amp) {
  if (c.mode != CAN_ON) return;
  c.wob = amp;
  c.t = 0.f;
}

int freeBit(PlinkState& s) {
  for (int i = 0; i < PLINK_BITS; ++i)
    if (s.bit[i].kind == PB_NONE) return i;
  return -1;
}

// ---- the can's step --------------------------------------------------------------------------------------

void stepCan(PlinkState& s, int i, float dt) {
  PlinkCan& c = s.can[i];
  switch (c.mode) {
    case CAN_ON:
      if (c.aux == 2) {   // the gag: it teeters on the branch, then topples off by itself
        c.t += dt;
        c.rot = 0.18f * sinT(c.t * 2.2f) * (1.f - clampf(c.t / kTeeterS, 0.f, 1.f));
        if (c.t >= kTeeterS) {
          float lx, ly;
          pairSpot(s, i, lx, ly);
          const float y0 = c.y;
          launch(c, -25.f, 0.5f, lx, ly, 1.f);
          c.y = y0;
          c.rot = 0.f;
        }
      } else if (c.wob > 0.f) {
        c.t += dt;
        const float dur = 0.6f + 3.f * c.wob;
        if (c.t >= dur) {
          c.wob = 0.f;
          c.rot = 0.f;
        } else {
          c.rot = c.wob * sinT(c.t * 3.f) * (1.f - c.t / dur);
        }
      }
      break;
    case CAN_FLY: {
      c.t += dt;
      c.vy += kGravity * dt;
      c.x += c.vx * dt;
      c.y += c.vy * dt;
      c.rot += c.spin * dt;
      if (c.style == 2 && c.bounces == 0) {
        c.sq += 2.5f * dt;
        c.sq -= floorf(c.sq);
      }
      if (c.aux == 1) {   // the weak pop: it comes back down onto the branch
        const float top = branchTop(s, c.x);
        if (c.vy > 0.f && c.y + 0.5f * (float)c.h >= top) {
          c.y = top - 0.5f * (float)c.h;
          c.mode = CAN_ON;
          c.aux = 2;
          c.t = 0.f;
          c.vx = c.vy = 0.f;
          c.rot = 0.f;
        }
        break;
      }
      const float ext = canVext(c);
      if (c.vy > 0.f && c.y + ext >= c.land_y) {
        c.y = c.land_y - ext;
        if (c.bounces == 0 && c.vy > 40.f) {   // one small bounce: a fifth of the speed, a quarter of the sideways, slower spin
          c.bounces = 1;
          c.vy = -c.vy * 0.2f;
          c.vx *= 0.25f;
          c.spin *= 0.4f;
          c.sq = 0.f;
        } else {   // down for good: it eases to rest on its side or upright
          c.mode = CAN_DOWN;
          c.t = 0.f;
          c.vx = c.vy = c.spin = 0.f;
          c.rot_land = restRot(c);
          c.sq = 0.f;
        }
      }
      break;
    }
    case CAN_DOWN:
      if (c.t < 0.15f) {
        c.t += dt;
        const float k = easeInOut(clampf(c.t / 0.15f, 0.f, 1.f));
        const float r0 = c.rot_land;
        // (rot was the landing angle; ease from it to the rest angle, written back as the state so it stays)
        c.rot = lerp(c.rot, r0, k);
        if (c.t >= 0.15f) c.rot = r0;
        c.y = c.land_y - canVext(c);
      }
      break;
    default: break;
  }
}

// ---- the bits -----------------------------------------------------------------------------------------------

void stepBit(PlinkBit& b, float dt) {
  if (b.kind == PB_NONE) return;
  b.t += dt;
  if (b.kind == PB_DUST) {
    b.y -= 14.f * dt;
    if (b.t > 0.5f) b.kind = PB_NONE;
    return;
  }
  if (b.kind == PB_CHIP) {
    b.vy += 500.f * dt;
    b.x += b.vx * dt;
    b.y += b.vy * dt;
    if (b.t > 0.6f) b.kind = PB_NONE;
    return;
  }
  if (b.rest) return;
  b.vy += kGravity * dt;
  b.x += b.vx * dt;
  b.y += b.vy * dt;
  b.rot += b.vx * dt * 0.05f;
  if (b.vy > 0.f && b.y >= b.land_y) {
    b.y = b.land_y;
    if (b.bounces == 0 && b.vy > 60.f) {
      b.bounces = 1;
      b.vy = -b.vy * 0.3f;
      b.vx *= 0.5f;
    } else {
      b.rest = 1;
      b.vx = b.vy = 0.f;
    }
  }
}

// ---- the shot machine ----------------------------------------------------------------------------------------

bool isGesture(uint8_t k) { return k >= SK_CHECK && k <= SK_SCRATCH; }

// What the current entry aims at: a far shot's point, or the can.
Vec2 entryAim(const PlinkState& s) {
  if (s.queue_kind[s.cur] == SK_FAR) return s.far_aim;
  return aimPoint(s, s.queue_can[s.cur] < PLINK_CANS ? s.queue_can[s.cur] : 0);
}
float gestureLen(uint8_t k) { return k == SK_SCRATCH ? 1.8f : 1.6f; }

void beginEntry(PlinkState& s, Rng& rng) {
  const uint8_t k = s.queue_kind[s.cur];
  s.t = 0.f;
  s.up_from = s.up;
  if (isGesture(k)) {
    s.phase = PP_GESTURE;
    s.gest = k;
    s.gw = 0.f;
    return;
  }
  const uint8_t ci = s.queue_can[s.cur] < PLINK_CANS ? s.queue_can[s.cur] : (uint8_t)0;
  const float r1 = rng.uniform();
  const float r2 = rng.uniform();
  s.hold_len = s.first_done ? kHoldMin + kHoldVar * r1 : kHoldFirstS;
  if (k == SK_FAR) s.hold_len = s.far_hold + 0.5f * r1;   // the long breath: the caller says how long
  s.pop_r = r2;
  s.aim_to = k == SK_FAR ? s.far_aim : aimPoint(s, ci);
  s.aim_from = s.aim;
  if (s.up < 0.99f) {   // the rifle comes up already pointing at it
    s.phase = PP_RAISE;
    s.aim = s.aim_from = s.aim_to;
  } else {
    s.phase = PP_SWING;
  }
}

void nextEntry(PlinkState& s, Rng& rng) {
  ++s.cur;
  s.gest = 0;
  s.gw = 0.f;
  if (s.cur >= s.n_queue) {
    if (s.stay) {   // the long shot: he stays on the scope, the rifle up, for the next shot the caller queues
      s.phase = PP_IDLE;
      s.run = 0;
      s.events = (uint8_t)(s.events | PEV_DONE);
      return;
    }
    s.phase = PP_LOWER;
    s.t = 0.f;
    s.up_from = s.up;
    return;
  }
  beginEntry(s, rng);
}

void fire(PlinkState& s) {
  const uint8_t k = s.queue_kind[s.cur];
  if (s.rounds > 0) --s.rounds;
  if (s.rounds == 0) s.chambered = 0;
  s.flash = kFlashS;
  s.recoil = 1.f;
  s.bullet_t = k == SK_FAR ? maxf(s.far_flight, kBulletS) : kBulletS;
  s.bullet_kind = k;
  s.bullet_can = s.queue_can[s.cur];
  s.events = (uint8_t)(s.events | PEV_SHOT | (s.first_done ? 0 : PEV_FIRST));
  s.first_done = 1;
  s.puff_t = 0.f;
  s.puff = plinkMuzzle(s);
  s.phase = PP_FOLLOW;
  s.t = 0.f;
}

void chips(PlinkState& s, const PlinkCan& c, int where, float r) {
  // Three flecks and a puff of dust jump off the branch 3 to 8 px beside or under the can.
  const float top = branchTop(s, c.x);
  float ox = 0.f, oy = 0.f;
  if (where == 0) { ox = 1.f; oy = 5.f; }
  if (where == 1) { ox = -(0.5f * (float)c.w + 4.f); oy = 1.f; }
  if (where == 2) { ox = 0.5f * (float)c.w + 4.f; oy = 1.f; }
  for (int k = 0; k < 3; ++k) {
    const int slot = freeBit(s);
    if (slot < 0) break;
    PlinkBit& b = s.bit[slot];
    memset(&b, 0, sizeof b);
    b.kind = PB_CHIP;
    const float f = r * (3.7f + (float)k * 2.3f);
    const float fr = f - floorf(f);
    b.x = c.x + ox + (float)(k - 1) * 1.6f;
    b.y = top + oy;
    b.vx = (k == 0 ? -1.f : (k == 1 ? 0.4f : 1.f)) * (25.f + 20.f * fr);
    b.vy = -(60.f + 50.f * fr);
  }
  const int slot = freeBit(s);
  if (slot >= 0) {
    PlinkBit& b = s.bit[slot];
    memset(&b, 0, sizeof b);
    b.kind = PB_DUST;
    b.x = c.x + ox;
    b.y = top + oy - 1.f;
  }
}

void bulletArrives(PlinkState& s) {
  const uint8_t k = s.bullet_kind;
  const int i = s.bullet_can < PLINK_CANS ? s.bullet_can : 0;
  PlinkCan& c = s.can[i];
  switch (k) {
    case SK_HIT: popCan(s, i, s.pop_r); break;
    case SK_WEAK:
      if (c.mode == CAN_ON) weakPop(c);
      break;
    case SK_MISS_U: case SK_MISS_L: case SK_MISS_R: {
      const int where = k == SK_MISS_U ? 0 : (k == SK_MISS_L ? 1 : 2);
      chips(s, c, where, s.pop_r);
      rockCan(c, k == SK_MISS_L ? 0.2f : (k == SK_MISS_R ? 0.12f : 0.06f));
      break;
    }
    case SK_BAG:
      s.events = (uint8_t)(s.events | PEV_BAG);
      break;
    case SK_FAR:   // out there: the caller shows what happens
      s.events = (uint8_t)(s.events | PEV_FAR);
      break;
    default: break;
  }
}

void stepMachine(PlinkState& s, float dt, Rng& rng) {
  s.t += dt;
  switch (s.phase) {
    case PP_RAISE: {
      const float dur = s.first_done ? kRaiseS : kRaiseFirstS;
      s.aim = s.aim_to = entryAim(s);
      s.up = lerp(s.up_from, 1.f, clampf(s.t / dur, 0.f, 1.f));
      if (s.t >= dur) {
        s.up = 1.f;
        s.phase = PP_HOLD;
        s.t = 0.f;
      }
      break;
    }
    case PP_SWING: {
      s.up = 1.f;
      s.aim = lerpV(s.aim_from, s.aim_to, easeInOut(clampf(s.t / kSwingS, 0.f, 1.f)));
      if (s.t >= kSwingS) {
        s.aim = s.aim_to;
        s.phase = PP_HOLD;
        s.t = 0.f;
      }
      break;
    }
    case PP_HOLD:
      s.up = 1.f;
      s.aim = s.aim_to;
      if (s.t >= s.hold_len) fire(s);
      break;
    case PP_FOLLOW:
      if (s.t >= kFollowS) nextEntry(s, rng);
      break;
    case PP_GESTURE: {
      const float len = gestureLen(s.gest);
      const float u = clampf(s.t / len, 0.f, 1.f);
      s.gw = easeInOut(clampf(u / 0.2f, 0.f, 1.f)) * (1.f - easeInOut(clampf((u - 0.8f) / 0.2f, 0.f, 1.f)));
      if (s.gest == SK_CHECK) s.up = lerp(s.up_from, 1.f, clampf(s.t / 0.4f, 0.f, 1.f));
      else s.up = s.up_from * (1.f - easeInOut(clampf(s.t / 0.5f, 0.f, 1.f)));
      if (s.t >= len) nextEntry(s, rng);
      break;
    }
    case PP_LOWER:
      s.up = s.up_from * (1.f - easeInOut(clampf(s.t / kLowerS, 0.f, 1.f)));
      if (s.t >= kLowerS) {
        s.up = 0.f;
        s.phase = PP_IDLE;
        s.run = 0;
        s.events = (uint8_t)(s.events | PEV_DONE);
      }
      break;
    default: break;
  }
}

// ---- the shapes --------------------------------------------------------------------------------------------------

enum ShKind : uint8_t { PS_CAP = 0, PS_POLY, PS_CIRC, PS_ELL };
enum PlinkCol : uint8_t {
  PC_ACTOR = 0, PC_TRUNK, PC_TIN, PC_FLASH_CORE, PC_FLASH, PC_SMOKE, PC_DUST, PC_LOAF, PC_APPLE, PC_PACKET, PC_JAR_LID, PC_BAR,
  PC_BAG,
  PC_COUNT
};

struct Sh {
  uint8_t kind, col, owner, layer;
  uint8_t w;          // quarter px: a capsule's width, a circle's radius
  int16_t x[4], y[4];   // half px (an ellipse: x[1], y[1] its radii)
};

int16_t qh(float v) {
  if (!finite(v)) return 0;
  return (int16_t)floorf(clampf(v, -15000.f, 15000.f) * 2.f + 0.5f);
}
inline float fromH(int16_t v) { return (float)v * 0.5f; }
uint8_t qw(float v) { return (uint8_t)clampf(v * 4.f + 0.5f, 0.f, 255.f); }

typedef void (*ShSink)(void* ctx, const Sh& s);
struct Em {
  ShSink fn;
  void* ctx;
  int layer;   // 0 tree, 1 held, 2 both
  void put(const Sh& s) const {
    if (layer == 2 || layer == (int)s.layer) fn(ctx, s);
  }
};

Sh blank(uint8_t kind, uint8_t col, uint8_t owner, uint8_t layer) {
  Sh s;
  memset(&s, 0, sizeof s);
  s.kind = kind;
  s.col = col;
  s.owner = owner;
  s.layer = layer;
  return s;
}
void cap(const Em& e, int owner, int layer, int col, Vec2 a, Vec2 b, float w) {
  Sh s = blank(PS_CAP, (uint8_t)col, (uint8_t)owner, (uint8_t)layer);
  s.w = qw(w);
  s.x[0] = qh(a.x);
  s.y[0] = qh(a.y);
  s.x[1] = qh(b.x);
  s.y[1] = qh(b.y);
  e.put(s);
}
void poly4(const Em& e, int owner, int layer, int col, const Vec2 q[4]) {
  Sh s = blank(PS_POLY, (uint8_t)col, (uint8_t)owner, (uint8_t)layer);
  for (int i = 0; i < 4; ++i) {
    s.x[i] = qh(q[i].x);
    s.y[i] = qh(q[i].y);
  }
  e.put(s);
}
void circ(const Em& e, int owner, int layer, int col, Vec2 c, float r) {
  Sh s = blank(PS_CIRC, (uint8_t)col, (uint8_t)owner, (uint8_t)layer);
  s.w = qw(r);
  s.x[0] = qh(c.x);
  s.y[0] = qh(c.y);
  e.put(s);
}
void ell(const Em& e, int owner, int layer, int col, Vec2 c, float rx, float ry) {
  Sh s = blank(PS_ELL, (uint8_t)col, (uint8_t)owner, (uint8_t)layer);
  s.x[0] = qh(c.x);
  s.y[0] = qh(c.y);
  s.x[1] = qh(rx);
  s.y[1] = qh(ry);
  e.put(s);
}

// A rectangle w x h about `c`, turned by `rot`, its x squeezed by `sq`: its four corners.
void rectPts(Vec2 c, float w, float h, float rot, float sq, Vec2 q[4]) {
  const float cs = cosA(rot), sn = sinA(rot);
  static const float kX[4] = {-1.f, 1.f, 1.f, -1.f}, kY[4] = {-1.f, -1.f, 1.f, 1.f};
  for (int i = 0; i < 4; ++i) {
    const float lx = kX[i] * 0.5f * w * sq, ly = kY[i] * 0.5f * h;
    q[i] = Vec2{c.x + lx * cs - ly * sn, c.y + lx * sn + ly * cs};
  }
}

struct RifleGeom {
  Vec2 d;         // unit, butt to muzzle
  Vec2 B, J, M;   // butt, joint (stock-and-action end), muzzle when whole
  Vec2 S0, S1, B0, B1;   // the stock-and-action piece, the barrel piece, as drawn
  float join;
};

void rifleGeom(const PlinkState& s, RifleGeom& g) {
  const float dir = (float)s.dir;
  g.d = Vec2{dir * cosA(s.elev), -sinA(s.elev)};
  const float up = easeInOut(clamp01(s.up));
  const float carry_len = s.rest == REST_PRONE ? lerp(kButtToCarryHand, kButtToWrist, easeInOut(window(s.kneel, 0.55f, 1.f))) : kButtToCarryHand;
  const Vec2 Bc = add(s.near_hand, mul(g.d, -carry_len));
  const Vec2 Bs = add(s.far_hand, mul(g.d, -kButtToGrip));
  g.B = lerpV(Bc, Bs, up);
  g.J = add(g.B, mul(g.d, kPieceStock));
  g.M = add(g.B, mul(g.d, kPlinkRifleLen));
  g.join = clamp01(s.join);
  const Vec2 down{0.1f * dir, 0.995f};
  const float j = g.join;
  g.S0 = lerpV(s.far_hand, g.B, j);
  g.S1 = lerpV(add(s.far_hand, mul(down, kPieceStock)), g.J, j);
  g.B0 = lerpV(s.near_hand, g.J, j);
  g.B1 = lerpV(add(s.near_hand, mul(down, kPieceBarrel)), g.M, j);
}

// The sack's centre in his far hand or on the ground.
Vec2 sackHeld(const PlinkState& s) { return Vec2{s.far_hand.x + (float)s.dir * 0.5f, s.far_hand.y + 6.5f}; }
Vec2 sackLying(const PlinkState& s) { return Vec2{s.down_at.x + 22.f, s.down_at.y - 4.f}; }

// The shooting bag lying down: its height, squashed where the rifle rests on it (it is soft: the forend
// sinks into it a little and the bag gives as the muzzle comes up onto a farther target). Full height when
// nothing is on it.
float bagRy(const PlinkState& s) {
  const float full = kBagRy;
  if (!s.rifle || s.join < 1.f || s.rest != REST_PRONE) return full;
  RifleGeom g;
  rifleGeom(s, g);
  const float x = s.down_at.x;
  const float lo = minf(g.B.x, g.M.x), hi = maxf(g.B.x, g.M.x);
  if (x < lo || x > hi || absf(g.M.x - g.B.x) < 1.f) return full;
  const float t = (x - g.B.x) / (g.M.x - g.B.x);
  const float under = lerp(g.B.y, g.M.y, t) + 0.5f * kStockW;   // the underside of the rifle over the bag
  return clampf(0.5f * (s.down_at.y + 0.8f - under), 0.55f * full, full);   // (never flatter than 55 %)
}

void buildCans(const PlinkState& s, const Em& e) {
  for (int i = 0; i < PLINK_CANS; ++i) {
    const PlinkCan& c = s.can[i];
    if (c.mode == CAN_OFF) continue;
    Vec2 ctr{c.x, c.y};
    float rot = c.rot;
    int layer = 0;
    if (c.mode == CAN_TIP) {
      const Vec2 t = stickTip(s);
      ctr = Vec2{t.x, t.y - 0.5f * (float)c.h - 1.f};
      rot = 0.f;
      layer = 1;
    } else if (c.mode == CAN_HELD) {
      const Vec2 hnd = c.hand ? s.far_hand : s.near_hand;
      ctr = Vec2{hnd.x, hnd.y - 0.5f * (float)c.h + 2.f};
      rot = 0.f;
      layer = 1;
    }
    float sq = 1.f;
    if (c.style == 2 && c.mode == CAN_FLY && c.bounces == 0) sq = maxf(0.18f, absf(cosA(c.sq * 2.f * kPi)));
    Vec2 q[4];
    rectPts(ctr, (float)c.w, (float)c.h, rot, sq, q);
    poly4(e, i, layer, PC_ACTOR, q);
    // the tin rim across the top (1.4 px)
    Vec2 r[4];
    const float cs = cosA(rot), sn = sinA(rot);
    const float hw = 0.5f * (float)c.w * sq, ty = -0.5f * (float)c.h;
    static const float kRX[4] = {-1.f, 1.f, 1.f, -1.f};
    const float ry[4] = {ty, ty, ty + 1.4f, ty + 1.4f};
    for (int k = 0; k < 4; ++k) {
      const float lx = kRX[k] * hw;
      r[k] = Vec2{ctr.x + lx * cs - ry[k] * sn, ctr.y + lx * sn + ry[k] * cs};
    }
    poly4(e, i, layer, PC_TIN, r);
  }
}

void buildBits(const PlinkState& s, const Em& e) {
  for (int i = 0; i < PLINK_BITS; ++i) {
    const PlinkBit& b = s.bit[i];
    const int o = PLINK_CANS + i;
    const Vec2 p{b.x, b.y};
    switch (b.kind) {
      case PB_CHIP: circ(e, o, 0, PC_TRUNK, p, 1.1f); break;
      case PB_DUST: circ(e, o, 0, PC_DUST, p, 1.5f + 4.f * b.t); break;
      case FOOD_LOAF: ell(e, o, 0, PC_LOAF, Vec2{b.x, b.y - 2.4f}, 4.6f, 2.6f); break;
      case FOOD_APPLE: circ(e, o, 0, PC_APPLE, Vec2{b.x, b.y - 2.4f}, 2.5f); break;
      case FOOD_PACKET: {
        Vec2 q[4];
        rectPts(Vec2{b.x, b.y - 2.f}, 6.f, 4.f, b.rest ? 0.2f : b.rot, 1.f, q);
        poly4(e, o, 0, PC_PACKET, q);
        break;
      }
      case FOOD_JAR:
        cap(e, o, 0, PC_ACTOR, Vec2{b.x, b.y - 4.f}, Vec2{b.x, b.y - 1.2f}, 4.2f);
        circ(e, o, 0, PC_JAR_LID, Vec2{b.x, b.y - 5.2f}, 1.7f);
        break;
      case FOOD_BAR: {
        Vec2 q[4];
        rectPts(Vec2{b.x, b.y - 1.4f}, 7.f, 2.6f, b.rest ? -0.15f : b.rot, 1.f, q);
        poly4(e, o, 0, PC_BAR, q);
        break;
      }
      default: break;
    }
  }
}

void buildRifle(const PlinkState& s, const Em& e) {
  if (!s.rifle) return;
  RifleGeom g;
  rifleGeom(s, g);
  const int o = PLINK_OWN_RIFLE;
  // the stock-and-action piece: the stock (TRUNK) for its first 78 %, the action (ACTOR) from 55 %
  const Vec2 sa{g.S1.x - g.S0.x, g.S1.y - g.S0.y};
  cap(e, o, 1, PC_TRUNK, g.S0, add(g.S0, mul(sa, 0.78f)), kStockW);
  cap(e, o, 1, PC_ACTOR, add(g.S0, mul(sa, 0.55f)), g.S1, kActionW);
  cap(e, o, 1, PC_ACTOR, g.B0, g.B1, kBarrelW);
  if (s.scope) {   // the long shot's scope rides on the stock half, so it shows on the halves too
    const float L = sqrtf(sa.x * sa.x + sa.y * sa.y);
    const Vec2 u = L > 1.0e-3f ? Vec2{sa.x / L, sa.y / L} : g.d;
    // the side of the piece away from the ground, whichever way he faces (it is up when the rifle is level)
    const Vec2 n = s.dir > 0 ? Vec2{u.y, -u.x} : Vec2{-u.y, u.x};
    cap(e, o, 1, PC_ACTOR, add(add(g.S0, mul(sa, kScopeA)), mul(n, kScopeUp)), add(add(g.S0, mul(sa, kScopeB)), mul(n, kScopeUp)), kScopeW);
  }
  if (s.mag == MAG_IN) {
    const Vec2 m = add(g.S0, mul(sa, 0.62f));
    cap(e, o, 1, PC_ACTOR, m, Vec2{m.x - 0.4f * (float)s.dir, m.y + 5.5f}, kMagW);
  } else if (s.mag == MAG_HAND) {
    cap(e, o, 1, PC_ACTOR, Vec2{s.far_hand.x, s.far_hand.y - 2.5f}, Vec2{s.far_hand.x, s.far_hand.y + 2.5f}, kMagW + 0.4f);
  }
}

void buildHeld(const PlinkState& s, const Em& e) {
  const int o = PLINK_OWN_HELD;
  if (s.stick == PI_HAND) {
    const Vec2 d = stickDir(s);
    const Vec2 a = add(s.near_hand, mul(d, -(kPlinkStickLen - kPlinkStickReach)));
    const Vec2 b = add(s.near_hand, mul(d, kPlinkStickReach));
    cap(e, o, 1, PC_TRUNK, a, b, 2.2f);
    // a small fork at the end
    const float ang = s.stick_ang;
    for (int k = -1; k <= 1; k += 2) {
      const float a2 = ang + 0.55f * (float)k * (float)s.dir;
      const Vec2 fd{(float)s.dir * sinA(a2), cosA(a2)};
      cap(e, o, 1, PC_TRUNK, b, add(b, mul(fd, 4.5f)), 1.6f);
    }
  }
  if (s.sack == PI_HAND && s.sack_big) {   // the shooting bag by its neck, hanging a little forward of his hand
    const Vec2 c{s.far_hand.x + (float)s.dir * 0.8f, s.far_hand.y + 6.5f * kBagScale};
    ell(e, o, 1, PC_BAG, c, 4.6f * kBagScale, 6.f * kBagScale);
    cap(e, o, 1, PC_BAG, Vec2{c.x, c.y - 5.f * kBagScale}, Vec2{c.x, s.far_hand.y - 0.5f}, 2.f);
  } else if (s.sack == PI_HAND) {
    const Vec2 c = sackHeld(s);
    ell(e, o, 1, PC_ACTOR, c, 4.6f, 6.f);
    cap(e, o, 1, PC_ACTOR, Vec2{c.x, c.y - 5.f}, Vec2{c.x, s.far_hand.y - 0.5f}, 1.2f);
    if (s.sack_n > 0) {
      circ(e, o, 1, PC_TIN, Vec2{c.x - 1.4f, c.y - 5.6f}, 1.5f);
      if (s.sack_n > 2) circ(e, o, 1, PC_TIN, Vec2{c.x + 1.6f, c.y - 5.2f}, 1.5f);
    }
  }
}

void buildLying(const PlinkState& s, const Em& e) {
  const int o = PLINK_OWN_LYING;
  if (s.stick == PI_DOWN) cap(e, o, 0, PC_TRUNK, Vec2{s.down_at.x - 14.f, s.down_at.y - 0.5f}, Vec2{s.down_at.x + 48.f, s.down_at.y - 1.5f}, 2.2f);
  if (s.sack == PI_DOWN && s.sack_big) {   // the shooting bag lying at down_at, its foot on the ground there
    const float ry = bagRy(s);
    ell(e, o, 0, PC_BAG, Vec2{s.down_at.x, s.down_at.y + 0.8f - ry}, kBagRx, ry);
  } else if (s.sack == PI_DOWN) {
    const Vec2 c = sackLying(s);
    ell(e, o, 0, PC_ACTOR, Vec2{c.x, c.y + 1.f}, 6.f, 3.8f);
    if (s.sack_n > 0) circ(e, o, 0, PC_TIN, Vec2{c.x + 1.5f, c.y - 2.2f}, 1.5f);
  }
}

void buildFlash(const PlinkState& s, const Em& e) {
  if (s.rifle && s.flash > 0.f) {
    RifleGeom g;
    rifleGeom(s, g);
    const int o = PLINK_OWN_FLASH;
    const Vec2 m = g.M;
    cap(e, o, 1, PC_FLASH_CORE, m, add(m, mul(g.d, 8.f)), 2.2f);
    for (int k = -1; k <= 1; k += 2) {
      const float ang = s.elev + 0.7f * (float)k;
      const Vec2 sd{(float)s.dir * cosA(ang), -sinA(ang)};
      cap(e, o, 1, PC_FLASH, m, add(m, mul(sd, 5.f)), 1.6f);
    }
    circ(e, o, 1, PC_FLASH, add(m, mul(g.d, 2.f)), 2.2f);
  }
  if (s.puff_t >= 0.f && s.puff_t < 0.7f && s.first_done) {
    const float u = s.puff_t / 0.7f;
    circ(e, PLINK_OWN_PUFF, 1, PC_SMOKE, Vec2{s.puff.x + (float)s.dir * 3.f * u, s.puff.y - 10.f * u}, 2.f + 2.5f * u);
  }
}

void buildRope(const PlinkState& s, const Em& e) {
  if (!s.rope_cut) return;
  cap(e, PLINK_OWN_ROPE, 0, PC_ACTOR, s.branch_tip, Vec2{s.branch_tip.x + 0.6f, s.branch_tip.y + 6.f}, 1.2f);
}

void buildAll(const PlinkState& s, const Em& e) {
  buildCans(s, e);
  buildBits(s, e);
  buildRope(s, e);
  buildLying(s, e);
  buildRifle(s, e);
  buildHeld(s, e);
  buildFlash(s, e);
}

Rect shRect(const Sh& s) {
  int n = 4;
  float pad = 0.f;
  if (s.kind == PS_CAP) {
    n = 2;
    pad = (float)s.w * 0.125f;
  } else if (s.kind == PS_CIRC) {
    n = 1;
    pad = (float)s.w * 0.25f;
  } else if (s.kind == PS_ELL) {
    n = 1;
    pad = maxf(fromH(s.x[1]), fromH(s.y[1]));
  }
  float x0 = 3.0e38f, y0 = 3.0e38f, x1 = -3.0e38f, y1 = -3.0e38f;
  for (int i = 0; i < n; ++i) {
    x0 = minf(x0, fromH(s.x[i]));
    y0 = minf(y0, fromH(s.y[i]));
    x1 = maxf(x1, fromH(s.x[i]));
    y1 = maxf(y1, fromH(s.y[i]));
  }
  return Rect{(int)floorf(x0 - pad) - 2, (int)floorf(y0 - pad) - 2, (int)ceilf(x1 + pad) + 2, (int)ceilf(y1 + pad) + 2};
}

struct Sig {
  uint32_t h[PLINK_OWNERS];
  Rect b[PLINK_OWNERS];
};
void sigInit(Sig& g) {
  for (int i = 0; i < PLINK_OWNERS; ++i) {
    g.h[i] = 2166136261u;
    g.b[i] = kEmpty;
  }
}
inline uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
void sigSink(void* ctx, const Sh& s) {
  Sig& g = *static_cast<Sig*>(ctx);
  if (s.owner >= PLINK_OWNERS) return;
  uint32_t h = g.h[s.owner];
  h = fnv(h, s.kind);
  h = fnv(h, s.col);
  h = fnv(h, s.layer);
  h = fnv(h, s.w);
  for (int i = 0; i < 4; ++i) {
    h = fnv(h, (uint32_t)(uint16_t)s.x[i]);
    h = fnv(h, (uint32_t)(uint16_t)s.y[i]);
  }
  g.h[s.owner] = h;
  g.b[s.owner] = unite(g.b[s.owner], shRect(s));
}

struct BoundsAcc {
  Rect r;
};
void boundsSink(void* ctx, const Sh& s) {
  BoundsAcc& a = *static_cast<BoundsAcc*>(ctx);
  a.r = unite(a.r, shRect(s));
}

Pixel colorOf(int c, const Palette& pal) {
  switch (c) {
    case PC_ACTOR: return pal[ACTOR];
    case PC_TRUNK: return pal[TRUNK];
    case PC_TIN: return mixRGB(pal[STAR], pal[CLOUD_LIT], 0.4f);
    case PC_FLASH_CORE: return pal[FIRE_CORE];
    case PC_FLASH: return pal[FIRE];
    case PC_SMOKE: return mixRGB(pal[SMOKE], pal[CLOUD_LIT], 0.6f);
    case PC_DUST: return mixRGB(pal[SMOKE], pal[CLOUD_LIT], 0.5f);
    case PC_LOAF: return mixRGB(pal[TRUNK], pal[TENT_LIT], 0.45f);
    case PC_APPLE: return pal[MUSHROOM];
    case PC_PACKET: return pal[TENT_LIT];
    case PC_JAR_LID: return pal[STAR];
    case PC_BAR: return mixRGB(pal[TENT_LIT], pal[MUSHROOM], 0.5f);
    // The shooting bag is canvas: in his own dark it merged with him, his arms and the rifle into one blob
    // (review of step B5, Mike 2026-11-18).
    case PC_BAG: return mixRGB(pal[TRUNK], pal[CLOUD_LIT], 0.25f);
    default: return pal[ACTOR];
  }
}

struct DrawCtx {
  Surface* s;
  const Rect* clip;
  const Palette* pal;
  int prims;
};
void drawSink(void* ctx, const Sh& sh) {
  DrawCtx& d = *static_cast<DrawCtx*>(ctx);
  if (!overlaps(shRect(sh), *d.clip)) return;
  const Pixel col = colorOf(sh.col, *d.pal);
  ++d.prims;
  switch (sh.kind) {
    case PS_CAP:
      strokeCapsule(*d.s, Vec2{fromH(sh.x[0]), fromH(sh.y[0])}, Vec2{fromH(sh.x[1]), fromH(sh.y[1])}, (float)sh.w * 0.25f, col, *d.clip);
      break;
    case PS_POLY: {
      Vec2 q[4];
      for (int i = 0; i < 4; ++i) q[i] = Vec2{fromH(sh.x[i]), fromH(sh.y[i])};
      fillPolygon(*d.s, q, 4, col, *d.clip);
      break;
    }
    case PS_CIRC: fillCircle(*d.s, Vec2{fromH(sh.x[0]), fromH(sh.y[0])}, (float)sh.w * 0.25f, col, *d.clip); break;
    default: fillEllipse(*d.s, Vec2{fromH(sh.x[0]), fromH(sh.y[0])}, fromH(sh.x[1]), fromH(sh.y[1]), col, *d.clip); break;
  }
}

int g_last_prims = 0;

}  // namespace

// ---- state helpers ------------------------------------------------------------------------------------------

float plinkAngle(float x, float y) { return atan2A(x, y); }

float plinkBranchY(const PlinkState& s, float x) { return s.branch_root.y + branchSlope(s) * (s.branch_root.x - x); }

Vec2 plinkCanSpot(const PlinkState& s, int i) {
  const int k = i < 0 ? 0 : (i >= PLINK_CANS ? PLINK_CANS - 1 : i);
  // (a branch that reaches to the right, as a mirrored layout or a test may have it, puts the cans on the other side of its root)
  const float side = s.branch_tip.x < s.branch_root.x ? 1.f : -1.f;
  const float x = s.branch_root.x + side * kCans[k].dx;
  return Vec2{x, branchTop(s, x) - 0.5f * (float)kCans[k].h};
}

Vec2 plinkCanPos(const PlinkState& s, int i) {
  const int k = i < 0 ? 0 : (i >= PLINK_CANS ? PLINK_CANS - 1 : i);
  const PlinkCan& c = s.can[k];
  if (c.mode == CAN_OFF) return plinkCanSpot(s, k);
  if (c.mode == CAN_ON || c.mode == CAN_FLY || c.mode == CAN_DOWN) return Vec2{c.x, c.y};
  return plinkCanSpot(s, k);
}

void plinkInit(PlinkState& s, Vec2 root, Vec2 tip, int dir, const float pair_x[PLINK_PAIRS], const float pair_y[PLINK_PAIRS]) {
  memset(&s, 0, sizeof s);
  s.branch_root = root;
  s.branch_tip = tip;
  s.dir = dir < 0 ? (int8_t)-1 : (int8_t)1;
  s.H = kPlinkH;
  for (int i = 0; i < PLINK_PAIRS; ++i) {
    s.pair_x[i] = pair_x[i];
    s.pair_y[i] = pair_y[i];
  }
  for (int i = 0; i < PLINK_CANS; ++i) {
    PlinkCan& c = s.can[i];
    c.mode = CAN_OFF;
    c.w = kCans[i].w;
    c.h = kCans[i].h;
    c.style = kCans[i].style;
    const Vec2 p = plinkCanSpot(s, i);
    c.x = p.x;
    c.y = p.y;
  }
  s.stick_ang = kPi - 0.25f;
  s.elev = kPlinkCarryElev;
  s.puff_t = 1.f;
  s.aim = plinkCanSpot(s, 0);
  s.down_at = root;
}

void plinkFace(PlinkState& s, int dir) { s.dir = dir < 0 ? (int8_t)-1 : (int8_t)1; }

void plinkSetCan(PlinkState& s, int i, uint8_t mode) {
  if (i < 0 || i >= PLINK_CANS) return;
  PlinkCan& c = s.can[i];
  c.mode = mode;
  c.rot = 0.f;
  c.wob = 0.f;
  c.aux = 0;
  c.hand = 1;
  const Vec2 p = plinkCanSpot(s, i);
  if (mode == CAN_ON) {
    c.x = p.x;
    c.y = p.y;
  }
}

void plinkSetCanOn(PlinkState& s, int i, float u) {
  if (i < 0 || i >= PLINK_CANS) return;
  PlinkCan& c = s.can[i];
  const Vec2 p = plinkCanSpot(s, i);
  if (u >= 1.f) {
    c.mode = CAN_ON;
    c.x = p.x;
    c.y = p.y;
    c.rot = 0.f;
    c.wob = 0.06f;
    c.t = 0.f;
    return;
  }
  // sliding off the fork onto the branch: it stays on the stick until it is down
  c.mode = CAN_TIP;
}

void plinkStick(PlinkState& s, uint8_t mode, float ang) {
  s.stick = mode;
  s.stick_ang = fin(ang, s.stick_ang);
}
void plinkSack(PlinkState& s, uint8_t mode) { s.sack = mode; }
void plinkLay(PlinkState& s, Vec2 at) { s.down_at = at; }
void plinkRifle(PlinkState& s, bool out, float join) {
  s.rifle = out ? 1 : 0;
  s.join = clamp01(join);
}
void plinkMag(PlinkState& s, uint8_t mag) { s.mag = mag; }
void plinkLoad(PlinkState& s) {
  s.mag = MAG_IN;
  s.rounds = 8;
  s.chambered = 1;
}
void plinkUnload(PlinkState& s) {
  s.mag = MAG_NONE;
  s.rounds = 0;
  s.chambered = 0;
}
void plinkKneel(PlinkState& s, bool on) { s.kneel_on = on ? 1 : 0; }
void plinkStay(PlinkState& s, bool on) { s.stay = on ? 1 : 0; }
void plinkRest(PlinkState& s, uint8_t rest) { s.rest = rest == REST_PRONE ? (uint8_t)REST_PRONE : (uint8_t)REST_KNEEL; }
void plinkScope(PlinkState& s, bool on) { s.scope = on ? 1 : 0; }
void plinkSackBig(PlinkState& s, bool on) { s.sack_big = on ? 1 : 0; }
void plinkHeadOff(PlinkState& s, float k) { s.head_off = clamp01(fin(k, 0.f)); }
void plinkSettle(PlinkState& s, float k) { s.settle = clamp01(fin(k, 0.f)); }
bool plinkFar(PlinkState& s, Vec2 aim, float flight_s, float hold_s) {
  if (!finite(aim.x) || !finite(aim.y) || !plinkQueue(s, SK_FAR, 0)) return false;
  s.far_aim = aim;
  s.far_flight = clampf(fin(flight_s, kBulletS), kBulletS, 5.f);
  s.far_hold = clampf(fin(hold_s, kHoldMin), 0.2f, 10.f);
  return true;
}

int plinkSweep(PlinkState& s, float x, float r) {
  int n = 0;
  for (PlinkCan& c : s.can) {
    if (c.mode != CAN_DOWN || absf(c.x - x) > r) continue;
    c.mode = CAN_OFF;
    ++n;
    if (s.sack_n < 255) ++s.sack_n;
  }
  return n;
}

int plinkTakeFood(PlinkState& s, float x, float r) {
  int n = 0;
  for (PlinkBit& b : s.bit) {
    if (b.kind < FOOD_LOAF || !b.rest || absf(b.x - x) > r) continue;
    b.kind = PB_NONE;
    ++n;
  }
  return n;
}

int plinkFoodLeft(const PlinkState& s) {
  int n = 0;
  for (const PlinkBit& b : s.bit) n += b.kind >= FOOD_LOAF ? 1 : 0;
  return n;
}

void plinkPoke(PlinkState& s, int i, float land_x) {
  if (i < 0 || i >= PLINK_CANS) return;
  PlinkCan& c = s.can[i];
  if (c.mode != CAN_ON) return;
  launch(c, -30.f, 0.6f, land_x, s.pair_y[0] + 4.f, 1.f);
}

void plinkSpill(PlinkState& s, Vec2 at, float land_y, Rng& rng) {
  static const uint8_t kKinds[5] = {FOOD_LOAF, FOOD_APPLE, FOOD_PACKET, FOOD_JAR, FOOD_BAR};
  for (int k = 0; k < 5; ++k) {
    const float r = rng.uniform();
    const int slot = freeBit(s);
    if (slot < 0) continue;
    PlinkBit& b = s.bit[slot];
    memset(&b, 0, sizeof b);
    b.kind = kKinds[k];
    b.x = at.x + 6.f;
    b.y = at.y - 5.f;
    b.vx = 40.f + 70.f * r;
    b.vy = -(60.f + 60.f * (1.f - r));
    b.land_y = land_y + (float)(k & 1) * 1.5f;
    b.rot = 0.4f * (float)k;
  }
}

bool plinkQueue(PlinkState& s, uint8_t kind, uint8_t can) {
  if (s.n_queue >= PLINK_QUEUE || kind >= SK_KINDS) return false;
  s.queue_kind[s.n_queue] = kind;
  s.queue_can[s.n_queue] = can;
  ++s.n_queue;
  return true;
}
void plinkQueueClear(PlinkState& s) {
  s.n_queue = s.cur = 0;
  s.gest = 0;
}
void plinkStart(PlinkState& s) {
  s.cur = 0;
  s.run = 1;
  s.phase = PP_IDLE;
  // the first entry begins on the next step (it needs the Rng)
}

bool plinkSettled(const PlinkState& s) {
  for (const PlinkCan& c : s.can) {
    if (c.mode == CAN_FLY) return false;
    if (c.mode == CAN_ON && (c.aux != 0 || c.wob > 0.f)) return false;
    if (c.mode == CAN_DOWN && c.t < 0.15f) return false;
  }
  for (const PlinkBit& b : s.bit) {
    if (b.kind == PB_NONE) continue;
    if (b.kind == PB_CHIP || b.kind == PB_DUST || !b.rest) return false;
  }
  return s.bullet_t <= 0.f && s.bag_vy == 0.f;
}

bool plinkStringDone(const PlinkState& s) { return !s.run && s.phase == PP_IDLE && plinkSettled(s); }

uint8_t plinkTakeEvents(PlinkState& s) {
  const uint8_t e = s.events;
  s.events = 0;
  return e;
}

bool plinkRifleOut(const PlinkState& s) { return s.rifle != 0; }
bool plinkHandsFull(const PlinkState& s) {
  if (s.rifle || s.stick == PI_HAND || s.sack == PI_HAND) return true;
  for (const PlinkCan& c : s.can)
    if (c.mode == CAN_HELD || c.mode == CAN_TIP) return true;
  return false;
}
bool plinkLoaded(const PlinkState& s) { return s.mag == MAG_IN || s.chambered != 0; }

Vec2 plinkAxis(const PlinkState& s) {
  RifleGeom g;
  rifleGeom(s, g);
  return g.d;
}
Vec2 plinkButt(const PlinkState& s) {
  RifleGeom g;
  rifleGeom(s, g);
  return g.B;
}
Vec2 plinkMuzzle(const PlinkState& s) {
  RifleGeom g;
  rifleGeom(s, g);
  return g.M;
}

// ---- the step ----------------------------------------------------------------------------------------------------

void plinkStep(PlinkState& s, Vec2 near_hand, Vec2 far_hand, int dir, float height, float dt, Rng& rng) {
  if (!finite(dt) || dt < 0.f) dt = 0.f;
  if (dt > kMaxDt) dt = kMaxDt;
  if (finite(near_hand.x) && finite(near_hand.y)) s.near_hand = near_hand;
  if (finite(far_hand.x) && finite(far_hand.y)) s.far_hand = far_hand;
  s.dir = dir < 0 ? (int8_t)-1 : (int8_t)1;
  s.H = finite(height) && height > 10.f ? height : kPlinkH;
  if (dt > 0.f) {
    for (int i = 0; i < PLINK_CANS; ++i) stepCan(s, i, dt);
    for (PlinkBit& b : s.bit) stepBit(b, dt);
    if (s.flash > 0.f) s.flash = maxf(0.f, s.flash - dt);
    if (s.puff_t < 1.f) s.puff_t += dt;
    if (s.recoil > 0.f) s.recoil = maxf(0.f, s.recoil - dt / 0.35f);
    if (s.bullet_t > 0.f) {
      s.bullet_t -= dt;
      if (s.bullet_t <= 0.f) {
        s.bullet_t = 0.f;
        bulletArrives(s);
      }
    }
    const float kneel_s = s.rest == REST_PRONE ? kProneS : kKneelS;
    s.kneel = s.kneel_on ? minf(1.f, s.kneel + dt / kneel_s) : maxf(0.f, s.kneel - dt / kneel_s);
    if (s.run) {
      if (s.phase == PP_IDLE) {
        if (s.cur < s.n_queue) beginEntry(s, rng);
        else if (s.stay) {   // nothing queued: he stays as he is
          s.run = 0;
          s.events = (uint8_t)(s.events | PEV_DONE);
        } else {
          s.phase = PP_LOWER;
          s.t = 0.f;
          s.up_from = s.up;
        }
      }
      stepMachine(s, dt, rng);
    }
  }
  // The axis: the low carry, or the line from his trigger hand to what he aims at (plus the kick). Lying
  // down, the carry comes up to nearly level as he gets low (the rifle laid forward, never into the ground).
  float carry = kPlinkCarryElev;
  if (s.rest == REST_PRONE) {
    // Onto the bag when it lies ahead of his hand: the axis through his near hand and just over the bag's top.
    float rest_e = kProneCarryElev;
    const float ex = (s.down_at.x - s.near_hand.x) * (float)s.dir;
    if (s.sack_big && s.sack == PI_DOWN && ex > 6.f) {
      const float top = s.down_at.y + 0.8f - 2.f * kBagRy - 0.5f * kStockW;
      rest_e = clampf(atan2A(s.near_hand.y - top, ex), -0.1f, 0.45f);
    }
    carry = lerp(kPlinkCarryElev, rest_e, easeInOut(window(s.kneel, 0.05f, 0.6f)));
  }
  float e_aim = carry;
  const float u = easeInOut(clamp01(s.up));
  if (u > 0.f) {
    if (s.phase == PP_GESTURE && s.gest == SK_CHECK) {
      e_aim = 0.05f + 0.07f * sinT(s.t * 0.9f) * s.gw;
    } else {
      const float ex = (s.aim.x - s.far_hand.x) * (float)s.dir, ey = s.far_hand.y - s.aim.y;
      e_aim = clampf(atan2A(ey, ex), -0.4f, 0.9f);
    }
  }
  s.elev = lerp(carry, e_aim, u) + 0.14f * s.recoil;
}

void plinkFreeze(PlinkState& s) { s.flash = 0.f; }

// ---- his pose ------------------------------------------------------------------------------------------------------

namespace {

// Where a hand is from its shoulder, in units of height, local frame (x forward, y down).
Vec2 armHand(float sh, float el) {
  return Vec2{kUpperL * sinA(sh) + kForeL * sinA(sh + el), kUpperL * cosA(sh) + kForeL * cosA(sh + el)};
}

// The shoulder and elbow angles that put the hand at (hx, hy) with the elbow below the line (bent forward).
void solveDown(float hx, float hy, float& sh, float& el) {
  float d = sqrtf(hx * hx + hy * hy);
  Vec2 u{1.f, 0.f};
  if (d > 1.0e-6f) u = Vec2{hx / d, hy / d};
  d = clampf(d, 0.03f, kUpperL + kForeL - 0.002f);
  const float a = (kUpperL * kUpperL - kForeL * kForeL + d * d) / (2.f * d);
  const float h = sqrtf(maxf(0.f, kUpperL * kUpperL - a * a));
  const Vec2 n{-u.y, u.x};
  const Vec2 e{a * u.x + h * n.x, a * u.y + h * n.y};
  const Vec2 hand{u.x * d, u.y * d};
  sh = atan2A(e.x, e.y);
  el = wrapPi(atan2A(hand.x - e.x, hand.y - e.y) - sh);
}

// ---- lying down (the long shot; LONGRANGE_PLAN 5.2 as the stills tuned it, Mike 2026-11-18) ----
//
// Three stages over the 2.4 s ramp, overlapping so it flows instead of clicking from key to key: down onto
// one knee (the plinking's kneel), forward onto the far hand (all fours; the near hand keeps the rifle off
// the ground), then the legs slide back by way of the knees until he lies on his elbows. The knee is the
// lowest contact all the way down, so the rig's grounding never lifts him.
struct ProneKey {
  float lean, dx, head;
  float thigh[2], knee[2], shoulder[2], elbow[2];
};
// (the kneel's arms and head are whatever he had: the carry)
const ProneKey kFours = {1.05f, 0.f, -0.60f, {0.35f, 0.20f}, {1.92f, 1.95f}, {0.85f, 0.10f}, {0.45f, 0.05f}};
// Lying: the hips flat, the legs flat behind him with the toes dug in, the far leg a little straighter; the
// near lower leg cocked up until he settles (kSettleKnee0 to the flat bend); propped on both forearms.
// The near forearm is raised, its hand on the wrist of the stock where the rifle rests on the bag; the far
// forearm lies flat on the ground.
const ProneKey kLying = {1.38f, 0.f, -0.95f, {-1.41f, -1.47f}, {0.41f, 0.32f}, {0.95f, 0.95f}, {1.75f, 0.62f}};
constexpr float kSettleKnee0 = 1.15f;
constexpr float kTuck = 0.5f;   // rad of extra front knee bend half way onto all fours
// The stages' windows on the raw ramp 0..1.
constexpr float kKneelA0 = 0.f, kKneelA1 = 0.40f, kFoursA0 = 0.28f, kFoursA1 = 0.70f, kLieA0 = 0.55f, kLieA1 = 1.f;
// Shouldered, prone: the cheek on the stock, or lifted off it (head relative to the torso, as the pose's).
constexpr float kHeadOnScope = -0.80f, kHeadOffScope = -1.38f;   // (off: -1.10 in B5; C3 made it larger so it reads)
// Shouldered prone the butt sits this much higher than the kneeling hold (units of height): up in the
// shoulder with his cheek on the comb. With the plinking's offset the butt sat under his chest and at the
// long shot's 19 degrees the rifle read as held at the belly (review of step B5, Mike 2026-11-18). The bag
// then goes nearer him (FP.x + 46) so the forend still rests on it.
constexpr float kProneButtDrop = -0.03f;

void keyTo(HikerPose& p, const ProneKey& k, float w, bool arms, float knee0) {
  if (w <= 0.f) return;
  p.lean = lerp(p.lean, k.lean, w);
  p.dx = lerp(p.dx, k.dx, w);
  p.head = lerp(p.head, k.head, w);
  p.thigh[0] = lerpA(p.thigh[0], k.thigh[0], w);
  p.thigh[1] = lerpA(p.thigh[1], k.thigh[1], w);
  p.knee[0] = lerpA(p.knee[0], knee0, w);
  p.knee[1] = lerpA(p.knee[1], k.knee[1], w);
  if (!arms) return;
  for (int i = 0; i < 2; ++i) {
    p.shoulder[i] = lerpA(p.shoulder[i], k.shoulder[i], w);
    p.elbow[i] = lerpA(p.elbow[i], k.elbow[i], w);
  }
}

void proneBend(const PlinkState& s, HikerPose& p) {
  const float r = clamp01(s.kneel);
  const float a = easeInOut(window(r, kKneelA0, kKneelA1));
  const float b = easeInOut(window(r, kFoursA0, kFoursA1));
  const float c = easeInOut(window(r, kLieA0, kLieA1));
  if (a > 0.f) {   // one knee: the plinking's kneel
    p.lean = lerp(p.lean, 0.10f, a);
    p.dx = lerp(p.dx, -0.015f, a);
    p.thigh[0] = lerpA(p.thigh[0], 1.6f, a);
    p.knee[0] = lerpA(p.knee[0], 1.6f, a);
    p.thigh[1] = lerpA(p.thigh[1], 0.12f, a);
    p.knee[1] = lerpA(p.knee[1], 1.95f, a);
  }
  keyTo(p, kFours, b, true, kFours.knee[0]);
  // The front foot tucks up as it passes under him, so it never props his hips up on its way to the knee.
  p.knee[0] += kTuck * 4.f * b * (1.f - b);
  keyTo(p, kLying, c, true, lerp(kSettleKnee0, kLying.knee[0], easeInOut(clamp01(s.settle))));
}

}  // namespace

bool plinkBends(const PlinkState& s) { return s.kneel > 0.f || s.up > 0.f || s.gw > 0.f; }

void plinkBend(const PlinkState& s, HikerPose& p) {
  if (!plinkBends(s)) return;
  const float k = easeInOut(clamp01(s.kneel));
  const float u = easeInOut(clamp01(s.up));
  const bool prone = s.rest == REST_PRONE;
  if (prone) {
    if (s.kneel > 0.f) proneBend(s, p);
  } else if (k > 0.f) {   // kneeling: the front knee up, the shin upright, the back knee down, the shin along the ground
    p.lean = lerp(p.lean, 0.10f, k);
    p.dx = lerp(p.dx, -0.015f, k);
    p.thigh[0] = lerpA(p.thigh[0], 1.6f, k);
    p.knee[0] = lerpA(p.knee[0], 1.6f, k);
    p.thigh[1] = lerpA(p.thigh[1], 0.12f, k);
    p.knee[1] = lerpA(p.knee[1], 1.95f, k);
  }
  if (u > 0.f) {
    const float e = s.elev;   // (with the kick)
    const float ce = cosA(e), se = sinA(e);
    // Where his hands are now (the carry) and where the shouldered rifle wants them.
    const Vec2 cn = armHand(p.shoulder[0], p.elbow[0]);
    const Vec2 cf = armHand(p.shoulder[1], p.elbow[1]);
    const Vec2 sn{0.03f + 0.26f * ce, -0.26f * se + 0.02f};
    Vec2 sf{0.03f + 0.12f * ce, -0.12f * se + 0.035f};
    // Prone the butt sits up in his shoulder, the cheek on the comb (kProneButtDrop).
    if (prone) sf.y += kProneButtDrop;
    float sh, el;
    solveDown(lerp(cn.x, sn.x, u), lerp(cn.y, sn.y, u), sh, el);
    p.shoulder[0] = sh;
    p.elbow[0] = el;
    solveDown(lerp(cf.x, sf.x, u), lerp(cf.y, sf.y, u), sh, el);
    p.shoulder[1] = sh;
    p.elbow[1] = el;
    if (prone) p.head = lerp(p.head, lerp(kHeadOnScope, kHeadOffScope, easeInOut(clamp01(s.head_off))) - 0.05f * s.recoil, u);
    else p.head = lerp(p.head, 0.20f + (s.phase == PP_GESTURE && s.gest == SK_CHECK ? 0.15f * s.gw : 0.f) - 0.05f * s.recoil, u);
    p.lean = lerp(p.lean, p.lean - 0.05f - 0.035f * s.recoil, u);
    for (const PlinkCan& c : s.can)   // the gag's can teeters: he lifts his cheek off the stock to look
      if (c.mode == CAN_ON && c.aux == 2) p.head -= 0.2f * u;
  }
  if (s.gw > 0.f && s.phase == PP_GESTURE) {
    if (s.gest == SK_SHRUG) {
      p.shoulder[1] = lerpA(p.shoulder[1], 0.9f, s.gw);
      p.elbow[1] = lerpA(p.elbow[1], 1.7f, s.gw);
      p.head += 0.2f * s.gw;
      p.lift += 0.012f * s.gw;
    } else if (s.gest == SK_SCRATCH) {
      p.shoulder[1] = lerpA(p.shoulder[1], 2.4f, s.gw);
      p.elbow[1] = lerpA(p.elbow[1], 2.43f + 0.1f * sinT(s.t * 3.f), s.gw);
      p.head += 0.12f * s.gw * sinT(s.t * 2.f);
    }
  }
}

// ---- bounds, same, dirty, draw -----------------------------------------------------------------------------------------

Rect plinkBounds(const PlinkState& s) {
  BoundsAcc a{kEmpty};
  Em e{boundsSink, &a, 2};
  buildAll(s, e);
  return a.r;
}
Rect plinkTreeBounds(const PlinkState& s) {
  BoundsAcc a{kEmpty};
  Em e{boundsSink, &a, 0};
  buildAll(s, e);
  return a.r;
}
Rect plinkHeldBounds(const PlinkState& s) {
  BoundsAcc a{kEmpty};
  Em e{boundsSink, &a, 1};
  buildAll(s, e);
  return a.r;
}

int plinkDirty(const PlinkState& before, const PlinkState& after, Rect out[PLINK_DIRTY_MAX]) {
  Sig a, b;
  sigInit(a);
  sigInit(b);
  Em ea{sigSink, &a, 2}, eb{sigSink, &b, 2};
  buildAll(before, ea);
  buildAll(after, eb);
  int n = 0;
  for (int i = 0; i < PLINK_OWNERS; ++i) {
    if (a.h[i] == b.h[i]) continue;
    const Rect r = unite(a.b[i], b.b[i]);
    if (!r.empty()) out[n++] = r;
  }
  return n;
}

bool plinkSame(const PlinkState& a, const PlinkState& b) {
  Rect none[PLINK_DIRTY_MAX];
  return plinkDirty(a, b, none) == 0;
}

void plinkTreeDraw(const PlinkState& s, Surface& strip, const Rect& clip, const Palette& pal) {
  DrawCtx d{&strip, &clip, &pal, 0};
  Em e{drawSink, &d, 0};
  buildAll(s, e);
  g_last_prims = d.prims;
}
void plinkHeldDraw(const PlinkState& s, Surface& strip, const Rect& clip, const Palette& pal) {
  DrawCtx d{&strip, &clip, &pal, 0};
  Em e{drawSink, &d, 1};
  buildAll(s, e);
  g_last_prims = d.prims;
}
int plinkLastDrawPrimitives() { return g_last_prims; }

}  // namespace ss
