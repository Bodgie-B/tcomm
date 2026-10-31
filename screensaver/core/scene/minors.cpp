// The minor-effect layer. See minors.h for the firing policy.
#include "minors.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../ss_app.h"
#include "director.h"
#include "world.h"

namespace ss {

namespace {

const char* const kNames[MN_COUNT] = {"chipmunk_cross", "squirrel_nut", "frog_splash", "bird_chip",
                                      "deer_edge",      "owl_watch",    "mouse_tent",  "leaf_fall",
                                      "moth_lantern",   "bee_visit",    "cloud_animal"};
// Base weights; each loop scales them by 0.6..1.4.
const float kBaseWeight[MN_COUNT] = {1.4f, 1.0f, 1.0f, 1.0f, 0.8f, 1.2f, 0.9f, 1.2f, 2.0f, 1.0f, 0.8f};

constexpr float kHz = (float)SS_TICK_HZ;
constexpr float kDt = SS_TICK_DT;
constexpr float kGapMin = 10.f, kGapMax = 30.f;   // start to start
constexpr float kBeatHold = 6.f;                   // after a beat (not a pause) ends
constexpr float kAfterMinor = 5.f;                 // quiet after a minor lets go
constexpr float kMaxAge = 30.f;                    // a minor that has not let go by now is made to
constexpr float kQuitSpeed = 120.f;                // px/s: our critters leaving for a beat that owns them
constexpr float kLaneMaxY = 412.f;                 // feet y on the right stay out of the corner band (417)

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline uint32_t ticksOf(float s) { return (uint32_t)(s * kHz + 0.5f); }
inline bool is(const char* a, const char* b) { return a && strcmp(a, b) == 0; }

// 0 -> 1 over `a`, hold, 1 -> 0 over the last `b` (u 0..1).
float envelope(float u, float a, float b) {
  if (u <= 0.f || u >= 1.f) return 0.f;
  if (u < a) return easeInOut(u / a);
  if (u > 1.f - b) return easeInOut((1.f - u) / b);
  return 1.f;
}

Vec2 bez(Vec2 a, Vec2 c, Vec2 b, float u) {
  const float v = 1.f - u;
  return Vec2{v * v * a.x + 2.f * v * u * c.x + u * u * b.x, v * v * a.y + 2.f * v * u * c.y + u * u * b.y};
}

// ---- the scene ---------------------------------------------------------------------

bool dark(const Minors& m) { return m.night || m.mood >= 4.5f || m.mood < 0.75f; }

bool isPause(const Minors& m) { return is(m.beat, "pause"); }

// Top of the water at x (the far bank strip lies just above it); 1e9 off the stream.
float waterTop(const World& w, float x) {
  float cy, half;
  if (!landscapeStreamSpan(w.land, x, cy, half)) return 1.0e9f;
  return cy - 1.06f * half;
}
float waterMid(const World& w, float x) {
  float cy, half;
  if (!landscapeStreamSpan(w.land, x, cy, half)) return 1.0e9f;
  return cy;
}

// Lowest feet y on the clearing at x: `margin` px above the water, out of
// the corner band, on the ground (y 332 is the back of the clearing).
float laneY(const World& w, float x, float margin) {
  return maxf(332.f, minf(kLaneMaxY, waterTop(w, x) - margin));
}

// A straight horizontal run at y over [x0, x1] stays on the ground.
bool runClear(const World& w, float x0, float x1, float y) {
  const float a = minf(x0, x1), b = maxf(x0, x1);
  for (float x = a; x <= b; x += 20.f)
    if (laneY(w, x, 4.f) < y) return false;
  return laneY(w, b, 4.f) >= y;
}

const Hiker* hikerIn(const World& w) { return w.fx.on && w.hiker.visible ? &w.hiker : nullptr; }

bool hikerWalking(const World& w) { return w.fx.on && w.hiker.walking; }

// Sitting still (by the fire, reading): a leaf can land on his hat, a bee on his nose.
bool hikerSitting(const World& w) {
  const Hiker* h = hikerIn(w);
  return h && !h->walking && !h->blending && (h->target == POSE_SIT || h->target == POSE_READ);
}

// In a pause, standing or sitting still: no script moves him, so his head and
// arm are free for a reaction for `need` seconds.
bool hikerIdle(const World& w, float need) {
  const Hiker* h = hikerIn(w);
  return h && isPause(w.minors) && w.minors.beat_left > need + 0.5f && !h->walking && !h->blending &&
         (h->target == POSE_SIT || h->target == POSE_STAND || h->target == POSE_READ);
}

// A critter `h` px tall running at y over [x0, x1] stays clear of him (40 px
// in x, 6 in y beyond his bounds), so if he starts walking it is never over him.
bool clearOfHiker(const World& w, float x0, float x1, float y, float h) {
  if (!hikerIn(w)) return true;
  const Rect& r = w.hiker_full;
  const float a = minf(x0, x1) - 40.f, b = maxf(x0, x1) + 40.f;
  if (b < (float)r.x0 || a > (float)r.x1) return true;
  return y + 3.f < (float)r.y0 - 6.f || y - h - 2.f > (float)r.y1 + 6.f;
}

float distToHiker(const World& w, float x) {
  const Hiker* h = hikerIn(w);
  return h ? fabsf(h->feet.x - x) : 1.0e9f;
}

// ---- critters and bits we hold -----------------------------------------------------------

int spawn(World& w, CritterKind k, Vec2 feet, int facing, uint8_t flags) {
  const int s = directorSpawn(w, k, feet, facing);
  if (s < 0) return -1;
  w.crit[s].tag = CT_MINOR;
  w.crit[s].flags = flags;
  return s;
}

Critter* own(World& w, int k) {
  const int s = w.minors.slot[k];
  return s >= 0 && directorOwns(w, s, CT_MINOR) ? &w.critters[s] : nullptr;
}

// A bit slot from the World's one allocator (a free one, else the cone or
// leaf that has lain longest).
int takeBit(World& w, uint8_t kind) { return worldTakeBit(w, kind); }

Bit* bitOf(World& w, int k) {
  const int s = w.minors.bit[k];
  return s >= 0 ? &w.bits[s] : nullptr;
}

void freeBit(World& w, int k) {
  Bit* b = bitOf(w, k);
  if (b) *b = Bit{};
  w.minors.bit[k] = -1;
}

void ring(World& w, Vec2 at, float size) {
  const int s = takeBit(w, BIT_RING);
  if (s < 0) return;
  Bit& b = w.bits[s];
  b.pos = at;
  b.spin = size;
  b.land_y = at.y;
}

// A fleck of earth thrown up behind a digging critter.
void dirt(World& w, const Critter& c) {
  Minors& m = w.minors;
  const int s = takeBit(w, BIT_DIRT);
  if (s < 0) return;
  Bit& b = w.bits[s];
  const float vx = m.rng.range(25.f, 50.f);
  const float vy = m.rng.range(70.f, 105.f);
  b.pos = Vec2{c.feet.x - (float)c.facing * 5.f, c.feet.y - 2.f};
  b.vel = Vec2{-(float)c.facing * vx, -vy};
  b.ground_y = c.feet.y + 1.f;
  b.land_y = c.feet.y + 1.f;
}

// Nearest frame edge for x: -1 left, +1 right.
int nearSide(float x) { return x < 0.5f * (float)SS_WIDTH ? -1 : 1; }

void next(Minors& m, uint8_t st) {
  m.stage = st;
  m.t = 0.f;
}

// Let go: the minor's work is done; it ends when its critters are gone.
void letGo(Minors& m) { next(m, MINOR_ENDING); }

// Off toward `side` at y `y`: a free walk to just inside that edge first (so
// the hiker rule still scatters it if he walks its way), then the director's
// exit for the last half body (minorsStep sees to it once it has arrived).
void leaveAt(World& w, int k, int side, float speed, CritterAction gait, float y) {
  Minors& m = w.minors;
  if (!own(w, k)) return;
  m.leave_side[k] = (int8_t)side;
  m.leave_speed[k] = speed;
  m.leave_gait[k] = gait;
  directorWalk(w, m.slot[k], Vec2{side < 0 ? -200.f : (float)SS_WIDTH + 200.f, y}, speed, gait);
}
void leave(World& w, int k, int side, float speed, CritterAction gait) {
  const Critter* c = own(w, k);
  if (c) leaveAt(w, k, side, speed, gait, directorDryY(w, c->feet, side < 0 ? 0.f : (float)SS_WIDTH));
}

// Every critter we still own leaves by the nearest edge, quickly.
void exitAll(World& w, float speed) {
  for (int k = 0; k < 2; ++k) {
    const Critter* c = own(w, k);
    if (!c) continue;
    int side = nearSide(c->feet.x);
    if (!clearOfHiker(w, c->feet.x, side < 0 ? 0.f : (float)SS_WIDTH, c->feet.y, 30.f)) side = -side;
    leave(w, k, side, speed, CA_RUN);
  }
}

// A beat that owns the critters began: whatever of ours is still walking off
// slowly (a deer strolling away, an exit already under way) goes quickly. The
// bird and the owl are flown out by their own steps.
void hurryOff(World& w) {
  Minors& m = w.minors;
  for (int k = 0; k < 2; ++k) {
    const int s = m.slot[k];
    if (s < 0 || !w.crit[s].in_use || w.crit[s].tag != CT_MINOR) continue;
    const Critter& c = w.critters[s];
    const CritterSlot& cs = w.crit[s];
    if (c.kind == CR_BIRD || c.kind == CR_OWL || !c.moving) continue;
    const float y = cs.path_on ? cs.path_y1 : c.feet.y;
    if (cs.mode == CM_FREE && m.leave_side[k] != 0 && m.leave_speed[k] < kQuitSpeed)
      leaveAt(w, k, m.leave_side[k], kQuitSpeed, CA_RUN, y);
    else if (cs.mode == CM_FLEE)
      directorWalk(w, s, Vec2{cs.flee_x, y}, kQuitSpeed, CA_RUN);
  }
}

// Leaves the gust has taken (not resting, not the running leaf_fall's own):
// up off the ground and away, quicker and quicker, wobbling; gone past the edge.
void gustStep(World& w) {
  const Minors& m = w.minors;
  for (int i = 0; i < BITS_MAX; ++i) {
    Bit& b = w.bits[i];
    if (b.kind != BIT_LEAF || b.resting) continue;
    if (m.cur == MN_LEAF_FALL && m.bit[0] == i) continue;
    const float side = b.vel.x < 0.f ? -1.f : 1.f;
    b.vel.x += side * 70.f * kDt;
    b.pos.x += b.vel.x * kDt;
    b.pos.y = b.ground_y - 16.f * easeOutCubic(minf(1.f, b.t / 1.2f)) - 3.f * sinT(b.t * 0.9f);
    b.spin = 0.1f * sinT(b.t * 1.3f) + 0.15f * side;
    if (b.pos.x < -12.f || b.pos.x > (float)SS_WIDTH + 12.f) b = Bit{};
  }
}

// ---- the minors ----------------------------------------------------------------------------
// Each start function returns false when the scene cannot take it now; each
// step function runs one tick and ends with letGo.

// chipmunk_cross: runs across the foreground with something in its mouth (a
// crumb, then a nut, then a granola bar on later picks), stops half way to sit
// up and look, runs on.
const uint8_t kChipCarry[3] = {CC_CRUMB, CC_NUT, CC_BAR};

bool startChip(World& w) {
  Minors& m = w.minors;
  m.dir = m.rng.chance(0.5f) ? 1 : -1;   // +1: in from the left
  if (hikerIn(w)) m.dir = w.hiker.feet.x > 0.5f * (float)SS_WIDTH ? 1 : -1;   // from the side away from him
  const float x0 = m.dir > 0 ? -20.f : (float)SS_WIDTH + 20.f;
  const float x2 = -x0 + (float)SS_WIDTH;
  // Stop somewhere in the middle third, away from him and the fire.
  float mx = 0.f;
  bool ok = false;
  for (int tries = 0; tries < 8 && !ok; ++tries) {
    mx = m.rng.range(260.f, 560.f);
    ok = distToHiker(w, mx) > 100.f && fabsf(mx - w.fire.pos.x) > 40.f;
  }
  if (!ok) return false;
  // Two legs, each a straight line just above the water (in front of the fire).
  const float y0 = laneY(w, x0 < 0.f ? 10.f : (float)SS_WIDTH - 10.f, 12.f);
  float my = laneY(w, mx, 12.f);
  if (fabsf(mx - w.fire.pos.x) < 80.f) my = maxf(my, minf(kLaneMaxY, w.fire.pos.y + 12.f));
  const float y2 = laneY(w, x2 < 0.f ? 10.f : (float)SS_WIDTH - 10.f, 12.f);
  // Raise a leg where its middle would dip into the water.
  float lift = 0.f;
  for (int k = 1; k < 10; ++k) {
    const float u = (float)k / 10.f;
    const float xa = lerp(x0, mx, u), xb = lerp(mx, x2, u);
    lift = maxf(lift, lerp(y0, my, u) - laneY(w, xa, 4.f));
    lift = maxf(lift, lerp(my, y2, u) - laneY(w, xb, 4.f));
  }
  m.p0 = Vec2{x0, y0 - lift};
  m.p1 = Vec2{mx, my - lift};
  m.p2 = Vec2{x2, y2 - lift};
  if (!clearOfHiker(w, x0, mx, maxf(m.p0.y, m.p1.y), 22.f)) return false;
  const int s = spawn(w, CR_CHIPMUNK, m.p0, m.dir, 0);
  if (s < 0) return false;
  m.slot[0] = (int8_t)s;
  w.critters[s].carry = kChipCarry[m.chip_n < 2 ? m.chip_n : 2];
  ++m.chip_n;
  directorWalk(w, s, m.p1, 100.f, CA_RUN);
  return true;
}

void stepChip(World& w) {
  Minors& m = w.minors;
  Critter* c = own(w, 0);
  if (!c) return letGo(m);
  if (m.quit && m.stage < 2) {
    exitAll(w, 120.f);
    return letGo(m);
  }
  switch (m.stage) {
    case 0:
      if (critterArrived(*c)) {
        critterSetAction(*c, CA_SIT, 0.3f);
        next(m, 1);
      }
      break;
    case 1:
      if (m.t > 0.5f && c->action == CA_SIT) critterSetAction(*c, CA_LOOK, 0.3f);
      if (m.t > 2.4f) {
        if (clearOfHiker(w, c->feet.x, m.p2.x, maxf(c->feet.y, m.p2.y), 22.f)) {
          leaveAt(w, 0, m.dir, 100.f, CA_RUN, m.p2.y);   // on across; the second leg keeps to the bank
        } else {
          leaveAt(w, 0, -m.dir, 100.f, CA_RUN, m.p0.y);   // he is in the way: back the way it came
        }
        letGo(m);
      }
      break;
    default: break;
  }
}

// squirrel_nut: runs in with a nut, digs, buries it, pats it down, sits up and
// forgets; digs at three other spots (nothing, nothing, a nut) and runs off.
bool startSquirrel(World& w) {
  Minors& m = w.minors;
  const Rect tb = w.tent.state != TENT_PACKED ? tentBounds(w.tent) : Rect{0, 0, 0, 0};
  for (int tries = 0; tries < 10; ++tries) {
    const float x = m.rng.range(330.f, 540.f);
    const float lo = minf(400.f, laneY(w, x, 20.f));
    const float y = m.rng.range(344.f, maxf(345.f, lo));
    if (distToHiker(w, x) < 130.f || fabsf(x - w.fire.pos.x) < 50.f) continue;
    if (!tb.empty() && x > (float)tb.x0 - 30.f && x < (float)tb.x1 + 30.f) continue;
    if (fabsf(x - (float)w.land.camp_x) < 24.f) continue;
    // In along a level line from an edge where the ground holds that y.
    int side = 0;
    if (runClear(w, -20.f, x + 50.f, y) && clearOfHiker(w, -20.f, x + 90.f, y, 30.f)) side = -1;
    else if (runClear(w, x - 50.f, (float)SS_WIDTH + 20.f, y) && clearOfHiker(w, x - 90.f, (float)SS_WIDTH, y, 30.f))
      side = 1;
    if (side == 0) continue;
    const int s = spawn(w, CR_SQUIRREL, Vec2{side < 0 ? -24.f : (float)SS_WIDTH + 24.f, y}, -side, 0);
    if (s < 0) return false;
    m.slot[0] = (int8_t)s;
    m.dir = (int8_t)side;
    m.p0 = Vec2{x, y};
    m.n = 0;
    w.critters[s].carry = CC_NUT;
    directorWalk(w, s, m.p0, 95.f, CA_RUN);
    return true;
  }
  return false;
}

// Scratching at the ground: head down, the fore paw going, earth flying.
void digPose(World& w, Critter& c, float u) {
  Minors& m = w.minors;
  const float k = envelope(u, 0.12f, 0.15f);
  c.head_add = -0.4f * k;
  c.fore_add = k * 0.7f * sinT(m.t * 5.5f);
  const float prev = m.t - kDt;
  if (u < 0.85f && floorf(m.t / 0.3f) != floorf(prev / 0.3f)) dirt(w, c);
}

void stepSquirrel(World& w) {
  Minors& m = w.minors;
  Critter* c = own(w, 0);
  if (!c) return letGo(m);
  if (m.quit) {
    c->head_add = c->fore_add = 0.f;
    exitAll(w, 110.f);
    return letGo(m);
  }
  constexpr float kDigS = 1.4f, kDig2S = 1.0f, kPatS = 1.0f;
  switch (m.stage) {
    case 0:   // run in
      if (critterArrived(*c)) {
        critterSetAction(*c, CA_IDLE, 0.2f);
        next(m, 1);
      }
      break;
    case 1:   // dig and drop the nut in
      digPose(w, *c, m.t / kDigS);
      if (m.t >= kDigS) {
        c->carry = CC_NONE;
        c->head_add = c->fore_add = 0.f;
        next(m, 2);
      }
      break;
    case 2: {   // two pats
      const float s = m.t / kPatS;
      c->head_add = -0.3f * envelope(s, 0.1f, 0.2f);
      const float pat = sinT(minf(1.f, s) * 2.f);
      c->fore_add = 0.45f * (pat > 0.f ? pat : 0.f);
      if (m.t >= kPatS) {
        c->head_add = c->fore_add = 0.f;
        critterSetAction(*c, CA_SIT, 0.3f);
        next(m, 3);
      }
      break;
    }
    case 3:   // sits up, looks round: where was it?
      if (m.t > 0.6f && c->action == CA_SIT) critterSetAction(*c, CA_LOOK, 0.3f);
      if (m.t > 2.2f) {
        ++m.n;
        float side = (m.n & 1) ? 1.f : -1.f;
        const float d = m.rng.range(26.f, 42.f);
        const float dy = m.rng.range(-6.f, 6.f);
        float x = m.p0.x + side * d * (float)(1 + (m.n - 1) / 2);
        if (!clearOfHiker(w, c->feet.x, x, c->feet.y, 30.f)) {   // not toward him: the other way
          side = -side;
          x = m.p0.x + side * d * (float)(1 + (m.n - 1) / 2);
        }
        if (!clearOfHiker(w, c->feet.x, x, c->feet.y, 30.f)) {   // hemmed in: off it goes
          m.n = 3;
          c->carry = CC_NONE;
          next(m, 6);
          m.t = 1.3f;
          break;
        }
        directorWalk(w, m.slot[0], Vec2{x, minf(laneY(w, x, 20.f), m.p0.y + dy)}, 60.f, CA_HOP);
        next(m, 4);
      }
      break;
    case 4:   // hop to the next spot
      if (critterArrived(*c) && m.t > 0.2f) {
        critterSetAction(*c, CA_IDLE, 0.15f);
        next(m, 5);
      }
      break;
    case 5:   // dig: nothing, nothing, a nut
      digPose(w, *c, m.t / kDig2S);
      if (m.t >= kDig2S) {
        c->head_add = c->fore_add = 0.f;
        if (m.n >= 3) c->carry = CC_NUT;
        critterSetAction(*c, CA_SIT, 0.3f);
        next(m, 6);
      }
      break;
    case 6:
      if (m.n < 3) {   // puzzled look, then on to the next
        if (m.t > 0.35f && c->action == CA_SIT) critterSetAction(*c, CA_LOOK, 0.25f);
        if (m.t > 1.1f) {
          m.t = 2.3f;
          m.stage = 3;
        }
      } else if (m.t > 1.3f) {   // found one: off with it, the way clear of him
        const int side = clearOfHiker(w, c->feet.x, m.dir < 0 ? 0.f : (float)SS_WIDTH, c->feet.y, 30.f) ? m.dir : -m.dir;
        leave(w, 0, side, 100.f, CA_RUN);
        letGo(m);
      }
      break;
    default: break;
  }
}

// frog_splash: hops in along the far bank from the right edge to the water's
// edge, sits and blinks, hops in: a ring on the water and the frog is gone.
bool startFrog(World& w) {
  Minors& m = w.minors;
  const float xs = m.rng.range(680.f, 740.f);
  if (distToHiker(w, xs) < 130.f || !clearOfHiker(w, xs - 16.f, (float)SS_WIDTH, waterTop(w, xs), 16.f)) return false;
  const float ys = waterTop(w, xs) - 3.f, y0 = waterTop(w, (float)SS_WIDTH - 4.f) - 3.f;
  if (ys > kLaneMaxY || y0 > kLaneMaxY || ys < 300.f) return false;
  const int s = spawn(w, CR_FROG, Vec2{(float)SS_WIDTH + 16.f, y0}, -1, 0);
  if (s < 0) return false;
  m.slot[0] = (int8_t)s;
  m.p0 = Vec2{xs, ys};
  directorWalk(w, s, m.p0, 42.f, CA_HOP);
  return true;
}

void stepFrog(World& w) {
  Minors& m = w.minors;
  Critter* c = own(w, 0);
  if (!c) return letGo(m);
  if (m.quit && m.stage < 2) {
    exitAll(w, 70.f);
    return letGo(m);
  }
  switch (m.stage) {
    case 0:
      if (critterArrived(*c) && m.t > 0.2f) {
        critterSetAction(*c, CA_IDLE, 0.2f);
        next(m, 1);
      }
      break;
    case 1:
      if (m.t > 0.8f && m.t - kDt <= 0.8f) critterSetAction(*c, CA_LOOK, 0.3f);
      if (m.t > 2.0f && m.t - kDt <= 2.0f) critterSetAction(*c, CA_BLINK, 0.3f);
      if (m.t > 3.1f && m.t - kDt <= 3.1f) critterSetAction(*c, CA_IDLE, 0.25f);
      if (m.t > 3.6f) {   // in: the next hop lands in the middle of the water
        const float x = c->feet.x - 14.f;
        m.p1 = Vec2{x, minf(waterMid(w, x), kLaneMaxY)};
        directorWalk(w, m.slot[0], m.p1, 36.f, CA_HOP);
        next(m, 2);
      }
      break;
    case 2:
      if (c->feet.y >= m.p1.y - 1.5f || (critterArrived(*c) && m.t > 0.2f)) {
        ring(w, Vec2{c->feet.x - 2.f, m.p1.y}, 1.f);
        directorRelease(w, m.slot[0]);
        m.slot[0] = -1;
        letGo(m);
      }
      break;
    default: break;
  }
}

// bird_chip: swoops in from an edge and lands by the fire pit, pecks up a
// crumb, flies off over the stream and drops it: a tiny ring.
bool startBird(World& w) {
  Minors& m = w.minors;
  if (w.fire.stones < 1.f) return false;
  const Hiker* h = hikerIn(w);
  int side = m.rng.chance(0.5f) ? 1 : -1;
  if (h) side = h->feet.x < w.fire.pos.x ? 1 : -1;
  const Vec2 land{w.fire.pos.x + (float)side * 44.f, w.fire.pos.y - 3.f};
  if (distToHiker(w, land.x) < 90.f) return false;
  const float sy = m.rng.range(120.f, 170.f);
  m.p0 = Vec2{side > 0 ? (float)SS_WIDTH + 24.f : -24.f, sy};
  m.p1 = land;
  m.p2 = Vec2{land.x + (float)side * 150.f, land.y - 30.f};   // swoop: down, then level in
  m.dir = (int8_t)side;
  const int s = spawn(w, CR_BIRD, m.p0, -side, 0);
  if (s < 0) return false;
  m.slot[0] = (int8_t)s;
  critterSetAction(w.critters[s], CA_MOVE, 0.1f);
  m.u0 = 2.4f;   // flight seconds
  return true;
}

// Flight along a curve under our hand (the rig flaps in place in CA_MOVE).
void flyAlong(Critter& c, Vec2 a, Vec2 ctl, Vec2 b, float u) {
  const Vec2 p = bez(a, ctl, b, u);
  if (fabsf(p.x - c.feet.x) > 0.05f) c.facing = p.x < c.feet.x ? -1 : 1;
  c.feet = p;
}

void stepBird(World& w) {
  Minors& m = w.minors;
  Critter* c = own(w, 0);
  if (!c) return letGo(m);
  if (m.quit && m.stage < 3) {
    // Straight up and out the way it came.
    m.p1 = c->feet;
    m.p3 = Vec2{m.dir > 0 ? (float)SS_WIDTH + 30.f : -30.f, 130.f};
    m.p2 = Vec2{lerp(m.p1.x, m.p3.x, 0.3f), 140.f};
    critterSetAction(*c, CA_MOVE, 0.15f);
    next(m, 4);
    return;
  }
  switch (m.stage) {
    case 0: {   // the swoop in
      const float u = clamp01(m.t / m.u0);
      flyAlong(*c, m.p0, m.p2, m.p1, easeOutCubic(u));
      if (u >= 1.f) {
        critterSetAction(*c, CA_IDLE, 0.25f);
        next(m, 1);
      }
      break;
    }
    case 1:   // a hop closer, then pecks
      if (m.t > 0.5f && m.t - kDt <= 0.5f) {
        directorWalk(w, m.slot[0], Vec2{c->feet.x - (float)m.dir * 10.f, c->feet.y + 1.f}, 30.f, CA_HOP);
      }
      if (m.t > 1.3f && m.t - kDt <= 1.3f) critterSetAction(*c, CA_EAT, 0.25f);
      if (m.t > 2.6f && m.t - kDt <= 2.6f) {
        c->carry = CC_CRUMB;
        critterSetAction(*c, CA_LOOK, 0.25f);
      }
      if (m.t > 3.4f) {   // off over the water, away from him
        const float x = c->feet.x + (float)m.dir * 90.f;
        const float wy = waterMid(w, x);
        m.p1 = c->feet;
        m.p3 = Vec2{x, minf(wy, 470.f) - 46.f};
        m.p2 = Vec2{lerp(m.p1.x, m.p3.x, 0.4f), m.p1.y - 34.f};
        m.u0 = wy;
        critterSetAction(*c, CA_MOVE, 0.15f);
        next(m, 2);
      }
      break;
    case 2: {
      const float u = clamp01(m.t / 1.4f);
      flyAlong(*c, m.p1, m.p2, m.p3, easeInOut(u));
      if (u >= 1.f) {   // drop it
        c->carry = CC_NONE;
        const int s = takeBit(w, BIT_DROP);
        if (s >= 0) {
          Bit& b = w.bits[s];
          b.pos = Vec2{c->feet.x + (float)m.dir * 3.f, c->feet.y - 5.f};
          b.vel = Vec2{(float)m.dir * 18.f, 0.f};
          b.ground_y = m.u0;
          b.land_y = m.u0;
        }
        m.p1 = c->feet;
        m.p3 = Vec2{m.dir > 0 ? (float)SS_WIDTH + 30.f : -30.f, m.rng.range(150.f, 200.f)};
        m.p2 = Vec2{lerp(m.p1.x, m.p3.x, 0.5f), minf(m.p1.y, m.p3.y) - 30.f};
        next(m, 4);
      }
      break;
    }
    case 4: {   // up and away out of the frame
      const float dist = fabsf(m.p3.x - m.p1.x) + 60.f;
      const float u = clamp01(m.t / maxf(1.2f, dist / 170.f));
      flyAlong(*c, m.p1, m.p2, m.p3, u * (0.4f + 0.6f * u));   // gathering speed
      if (u >= 1.f || !c->visible) {
        directorRelease(w, m.slot[0]);
        m.slot[0] = -1;
        letGo(m);
      }
      break;
    }
    default: break;
  }
}

// deer_edge: walks out at the right edge of camp, freezes, takes one step
// closer, freezes; leaves when he moves (it is shy) or after about 20 s.
bool startDeer(World& w) {
  Minors& m = w.minors;
  const float x = (float)w.land.camp_x + 64.f + m.rng.range(0.f, 18.f);
  if (distToHiker(w, x) < 160.f) return false;
  const float y = minf(350.f, laneY(w, x, 14.f));
  if (!clearOfHiker(w, x - 14.f, (float)SS_WIDTH, y, 50.f)) return false;
  const int s = spawn(w, CR_DEER, Vec2{(float)SS_WIDTH + 40.f, minf(y, laneY(w, (float)SS_WIDTH - 20.f, 14.f))}, -1,
                      CF_SHY);
  if (s < 0) return false;
  m.slot[0] = (int8_t)s;
  m.p0 = Vec2{x, y};
  directorWalk(w, s, m.p0, 34.f, CA_MOVE);
  return true;
}

void stepDeer(World& w) {
  Minors& m = w.minors;
  Critter* c = own(w, 0);
  if (!c) return letGo(m);
  if (m.quit && m.stage < 4) {
    leave(w, 0, 1, 60.f, CA_RUN);
    return letGo(m);
  }
  switch (m.stage) {
    case 0:
      if (critterArrived(*c)) next(m, 1);
      break;
    case 1:   // frozen, head up, looking at camp
      if (m.t > 0.25f && c->action == CA_IDLE) critterSetAction(*c, CA_LOOK, 0.4f);
      if (m.t > 4.5f) {
        critterSetAction(*c, CA_MOVE, 0.2f);
        directorWalk(w, m.slot[0], Vec2{c->feet.x - 14.f, c->feet.y + 1.f}, 16.f, CA_MOVE);
        next(m, 2);
      }
      break;
    case 2:
      if (critterArrived(*c) && m.t > 0.3f) next(m, 3);
      break;
    case 3:   // frozen again; a long look, a turn of the head, then it goes
      if (m.t > 0.25f && c->action == CA_IDLE) critterSetAction(*c, CA_LOOK, 0.4f);
      if (m.t > 6.0f && m.t - kDt <= 6.0f) critterSetAction(*c, CA_HEADTURN, 0.6f);
      if (m.age > 19.f && m.t > 7.0f) {
        leave(w, 0, 1, 34.f, CA_MOVE);
        letGo(m);
      }
      break;
    default: break;
  }
}

// owl_watch: at dusk or night, glides onto the camp pine's branch: a slow
// blink, a full head turn, a look, and away.
float branchY(const World& w, float x) {
  const float u = clamp01(((float)w.land.camp_x - x) / (float)LS_BRANCH_LEN);
  return lerp((float)LS_BRANCH_ROOT_Y, (float)LS_BRANCH_TIP_Y, u);
}

bool startOwl(World& w) {
  Minors& m = w.minors;
  if (w.bag.state == BAG_HUNG && w.bag.swing != 0.f) return false;
  const float x = (float)w.land.camp_x - m.rng.range(18.f, 30.f);
  m.p1 = Vec2{x, branchY(w, x) + 1.f};
  m.p0 = Vec2{-30.f, 150.f};
  if (m.rng.chance(0.5f)) m.p0.x = (float)SS_WIDTH + 30.f;
  m.dir = m.p0.x < 0.f ? 1 : -1;
  m.p2 = Vec2{lerp(m.p0.x, m.p1.x, 0.6f), m.p1.y - 6.f};
  const int s = spawn(w, CR_OWL, m.p0, m.dir, 0);
  if (s < 0) return false;
  m.slot[0] = (int8_t)s;
  critterSetAction(w.critters[s], CA_STARTLE, 0.3f);   // tall and thin in flight
  m.u0 = fabsf(m.p1.x - m.p0.x) / 150.f;
  return true;
}

void stepOwl(World& w) {
  Minors& m = w.minors;
  Critter* c = own(w, 0);
  if (!c) return letGo(m);
  if (m.quit && m.stage < 3) next(m, 3);
  switch (m.stage) {
    case 0: {
      const float u = clamp01(m.t / m.u0);
      flyAlong(*c, m.p0, m.p2, m.p1, easeOutCubic(u));
      c->facing = 1;
      if (u >= 1.f) {
        critterSetAction(*c, CA_IDLE, 0.35f);
        next(m, 1);
      }
      break;
    }
    case 1: {
      static const float kAt[5] = {1.4f, 4.4f, 7.6f, 9.0f, 11.2f};
      static const CritterAction kDo[5] = {CA_BLINK, CA_HEADTURN, CA_LOOK, CA_BLINK, CA_IDLE};
      static const float kS[5] = {0.4f, 3.0f, 0.4f, 0.4f, 0.3f};
      for (int i = 0; i < 5; ++i)
        if (m.t > kAt[i] && m.t - kDt <= kAt[i]) critterSetAction(*c, kDo[i], kS[i]);
      if (m.t > 11.8f) next(m, 3);
      break;
    }
    case 3:   // away: drops off the branch and glides out the other side
      critterSetAction(*c, CA_STARTLE, 0.25f);
      m.p0 = c->feet;
      m.p1 = Vec2{m.dir > 0 ? (float)SS_WIDTH + 40.f : -40.f, 120.f};
      m.p2 = Vec2{lerp(m.p0.x, m.p1.x, 0.3f), m.p0.y + 30.f};
      m.u0 = fabsf(m.p1.x - m.p0.x) / 170.f;
      next(m, 4);
      break;
    case 4: {
      const float u = clamp01(m.t / m.u0);
      flyAlong(*c, m.p0, m.p2, m.p1, u * (0.35f + 0.65f * u));
      c->facing = 1;
      if (u >= 1.f || (!c->visible && m.t > 0.5f)) {
        directorRelease(w, m.slot[0]);
        m.slot[0] = -1;
        letGo(m);
      }
      break;
    }
    default: break;
  }
}

// mouse_tent: out from behind the pitched tent at its door side, scurries
// along the front of its base, sits up, sniffs, and is off past the back.
bool startMouse(World& w) {
  Minors& m = w.minors;
  const Tent& t = w.tent;
  if (t.state != TENT_UP || t.sag > 0.05f) return false;
  const Rect tb = tentBounds(t);
  const float cx = 0.5f * (float)(tb.x0 + tb.x1);
  const Hiker* h = hikerIn(w);
  if (h && h->feet.x > (float)tb.x0 - 90.f && h->feet.x < (float)tb.x1 + 90.f) return false;
  if (!clearOfHiker(w, 0.f, (float)tb.x1 + 12.f, t.pos.y + 2.f, 10.f)) return false;
  m.p0 = Vec2{(float)tb.x1 + 10.f, t.pos.y - 2.f};   // behind, past the door
  m.p1 = Vec2{(float)tb.x1 + 10.f, t.pos.y + 2.f};
  m.p2 = Vec2{(float)tb.x0 + 16.f, t.pos.y + 2.f};
  const int s = spawn(w, CR_MOUSE, Vec2{cx, t.pos.y - 2.f}, 1, 0);
  if (s < 0) return false;
  m.slot[0] = (int8_t)s;
  memoryNoteMouseTent(w.memory);   // twice in a loop: a hole in the tent next loop (stage 6 memory)
  directorWalk(w, s, m.p0, 55.f, CA_RUN);
  return true;
}

void stepMouse(World& w) {
  Minors& m = w.minors;
  Critter* c = own(w, 0);
  if (!c) return letGo(m);
  if (m.quit) {
    exitAll(w, 90.f);
    return letGo(m);
  }
  switch (m.stage) {
    case 0:
      if (critterArrived(*c) && m.t > 0.1f) {
        critterSetAction(*c, CA_LOOK, 0.2f);
        next(m, 1);
      }
      break;
    case 1:
      if (m.t > 0.7f) {
        c->feet.y = m.p1.y;   // out past the tent's edge: now in front of its base
        directorWalk(w, m.slot[0], m.p2, 55.f, CA_RUN);
        next(m, 2);
      }
      break;
    case 2:
      if (critterArrived(*c) && m.t > 0.1f) {
        critterSetAction(*c, CA_SIT, 0.25f);
        next(m, 3);
      }
      break;
    case 3:
      if (m.t > 0.6f && m.t - kDt <= 0.6f) critterSetAction(*c, CA_LOOK, 0.2f);
      if (m.t > 1.5f && m.t - kDt <= 1.5f) critterSetAction(*c, CA_SNIFF, 0.25f);
      if (m.t > 2.6f) {
        leave(w, 0, -1, 85.f, CA_RUN);
        letGo(m);
      }
      break;
    default: break;
  }
}

// leaf_fall: one leaf drifts down on a sine and lands on the ground, or on
// his hat if he sits; it slides off when he moves.
bool startLeaf(World& w) {
  Minors& m = w.minors;
  const int s = takeBit(w, BIT_LEAF);
  if (s < 0) return false;
  m.bit[0] = (int8_t)s;
  m.n = hikerSitting(w) ? 1 : 0;   // 1: aims for the hat
  float tx;
  if (m.n) {
    tx = w.hiker.feet.x;
    m.p1 = Vec2{tx, 0.f};
  } else {
    tx = m.rng.range(190.f, 600.f);
    if (fabsf(tx - w.fire.pos.x) < 30.f) tx += 60.f;
    const float lo = minf(405.f, laneY(w, tx, 8.f));
    m.p1 = Vec2{tx, m.rng.range(336.f, maxf(337.f, lo))};
  }
  const float dx = m.rng.range(-40.f, 40.f);
  m.p0 = Vec2{tx + dx, -8.f};
  m.u0 = m.rng.range(0.f, 1.f);   // sway phase
  Bit& b = w.bits[s];
  b.pos = m.p0;
  b.land_y = m.p1.y;
  return true;
}

// A gust takes a leaf lying on the ground off the nearest edge (gustStep).
void blowLeaf(Bit& b) {
  b.resting = false;
  b.t = 0.f;
  b.vel = Vec2{nearSide(b.pos.x) < 0 ? -30.f : 30.f, 0.f};
  b.ground_y = b.pos.y;
}

// A leaf down on the ground stays there, unless a beat that owns the scene
// has begun meanwhile: then it goes with the gust.
void settleLeaf(World& w, Bit& b) {
  if (w.minors.quit) blowLeaf(b);
  else b.resting = true;
}

// Where the leaf rests on his hat: the crown's top, over the head.
Vec2 hatTop(const World& w) {
  float r;
  const Vec2 hd = hikerHeadPos(w.hiker, &r);
  // Stem on the crown, the blade sticking out past its edge so it shows in silhouette.
  return Vec2{hd.x + 1.f, hd.y - r - 7.f};
}

void stepLeaf(World& w) {
  Minors& m = w.minors;
  Bit* b = bitOf(w, 0);
  if (!b || b->kind != BIT_LEAF) return letGo(m);
  if (m.quit && m.stage == 0) {   // still in the air when a beat that owns the scene began: the gust takes it
    blowLeaf(*b);
    return letGo(m);
  }
  constexpr float kFall = 30.f;   // px/s
  switch (m.stage) {
    case 0: {   // drifting down
      if (m.n && !hikerSitting(w)) {   // he got up: aim for the ground by him
        m.n = 0;
        const float gx = w.fx.on ? w.hiker.feet.x + 14.f : m.p1.x;
        // By his feet, but never on the water (he may be on the bank).
        m.p1 = Vec2{gx, w.fx.on ? minf(w.hiker.feet.y + 2.f, laneY(w, gx, 8.f)) : laneY(w, gx, 10.f)};
      }
      const Vec2 goal = m.n ? hatTop(w) : m.p1;
      const float y = m.p0.y + kFall * m.t;
      const float total = maxf(1.f, goal.y - m.p0.y);
      const float u = clamp01((y - m.p0.y) / total);
      const float sway = sinT(m.u0 + m.t / 2.8f);
      b->pos = Vec2{lerp(m.p0.x, goal.x, easeInOut(u)) + 15.f * sway * (1.f - u * u), y};
      b->spin = 0.08f * cosT(m.u0 + m.t / 2.8f) + 0.02f;
      b->land_y = m.n ? w.hiker.feet.y + 1.f : m.p1.y;
      if (y >= goal.y) {
        b->pos = goal;
        b->spin = 0.f;
        if (m.n) {
          next(m, 1);
        } else {
          settleLeaf(w, *b);
          letGo(m);
        }
      }
      break;
    }
    case 1:   // on his hat until he moves (or a while)
      if (hikerSitting(w) && !m.quit && m.age < 22.f) {
        b->pos = hatTop(w);
        b->spin = -0.05f;   // tip up a little
        b->land_y = w.hiker.feet.y + 1.f;
      } else {
        m.p0 = b->pos;
        const float x = b->pos.x + 10.f;
        m.p1 = Vec2{x, minf((w.fx.on ? w.hiker.feet.y : m.p0.y + 50.f) + 3.f, laneY(w, x, 8.f))};   // not into the water
        next(m, 2);
      }
      break;
    case 2: {   // slides off to the ground
      const float u = clamp01(m.t / 0.9f);
      b->pos = Vec2{lerp(m.p0.x, m.p1.x, u) + 4.f * sinT(u), lerp(m.p0.y, m.p1.y, easeInCubic(u))};
      b->spin = 0.25f * u;
      b->land_y = m.p1.y;
      if (u >= 1.f) {
        settleLeaf(w, *b);
        letGo(m);
      }
      break;
    }
    default: break;
  }
}

// moth_lantern: while the tent is lit, a moth flutters in and circles the
// light on a wobbling loop for 15 s, then away up into the dark.
Vec2 tentLight(const World& w) {
  const Rect tb = tentBounds(w.tent);
  // Over the upper middle of the lit wall, left of where the smoke drifts past the tent.
  return Vec2{0.5f * (float)(tb.x0 + tb.x1) - 5.f, (float)tb.y0 + 0.4f * (float)(tb.y1 - tb.y0)};
}

bool startMoth(World& w) {
  Minors& m = w.minors;
  if (!w.tent.lit || w.tent.state != TENT_UP) return false;
  const int s = takeBit(w, BIT_MOTH);
  if (s < 0) return false;
  m.bit[0] = (int8_t)s;
  const Vec2 c = tentLight(w);
  m.p0 = Vec2{c.x - 70.f, c.y - 60.f};
  m.u0 = m.rng.range(0.f, 1.f);
  w.bits[s].pos = m.p0;
  w.bits[s].land_y = w.tent.pos.y + 3.f;
  return true;
}

Vec2 mothLoop(const World& w, float t, float ph) {
  const Vec2 c = tentLight(w);
  const float a = ph + t / 2.3f;
  const float rx = 12.f + 3.f * sinT(t * 0.21f), ry = 9.f + 3.f * sinT(t * 0.15f + 0.3f);
  return Vec2{c.x + rx * cosT(a) + 2.5f * sinT(t * 1.13f), c.y + ry * sinT(2.f * a) * 0.7f - 4.f + 2.f * sinT(t * 0.87f)};
}

void stepMoth(World& w) {
  Minors& m = w.minors;
  Bit* b = bitOf(w, 0);
  if (!b || b->kind != BIT_MOTH) return letGo(m);
  constexpr float kInS = 1.6f, kLoopS = 15.f, kOutS = 3.f;
  const float t = m.age;
  if (m.stage == 0 && (t >= kInS + kLoopS || !w.tent.lit || m.quit)) {   // (a beat that owns the scene: away now)
    m.p1 = b->pos;
    m.p2 = Vec2{b->pos.x + 140.f, -12.f};
    next(m, 1);
  }
  if (m.stage == 0) {
    const Vec2 l = mothLoop(w, t, m.u0);
    const float k = easeInOut(clamp01(t / kInS));
    b->pos = Vec2{lerp(m.p0.x, l.x, k), lerp(m.p0.y, l.y, k)};
  } else {
    const float u = clamp01(m.t / kOutS);
    const Vec2 ctl{m.p1.x + 20.f, m.p1.y - 10.f};
    b->pos = bez(m.p1, ctl, m.p2, easeInCubic(u));
    b->pos.x += 3.f * sinT(m.t * 1.7f);
    if (u >= 1.f) {
      freeBit(w, 0);
      letGo(m);
    }
  }
}

// bee_visit: buzzes in to the mushroom, hovers, lands on the cap; if he sits
// still in a pause it goes for his nose, lands, he swats, it is gone.
Vec2 noseOf(const World& w) {
  float r;
  const Vec2 hd = hikerHeadPos(w.hiker, &r);
  return Vec2{hd.x + (float)w.hiker.facing * (r + 1.5f), hd.y + 3.f};   // under the brim
}

bool startBee(World& w) {
  Minors& m = w.minors;
  if (w.land.mush_x <= 0) return false;
  const int s = takeBit(w, BIT_BEE);
  if (s < 0) return false;
  m.bit[0] = (int8_t)s;
  m.p1 = Vec2{(float)w.land.mush_x, (float)w.land.mush_y - 15.f};   // over the cap
  m.p0 = Vec2{m.p1.x - 70.f, (float)SS_HEIGHT + 6.f};
  w.bits[s].pos = m.p0;
  w.bits[s].land_y = (float)w.land.mush_y + 1.f;
  return true;
}

void stepBee(World& w) {
  Minors& m = w.minors;
  Bit* b = bitOf(w, 0);
  if (!b || b->kind != BIT_BEE) return letGo(m);
  HikerFx& fx = w.fx;
  const float buzz = 1.6f * sinT(m.age * 3.7f) + 1.0f * sinT(m.age * 5.3f + 0.2f);
  if (m.quit && m.stage >= 4 && m.stage <= 6) next(m, 7);
  switch (m.stage) {
    case 0: {   // in from below, zigzagging
      const float u = clamp01(m.t / 2.2f);
      const Vec2 ctl{m.p0.x + 10.f, m.p1.y + 10.f};
      b->pos = bez(m.p0, ctl, m.p1, easeOutCubic(u));
      b->pos.x += 4.f * sinT(m.t * 2.3f) * (1.f - u);
      b->pos.y += buzz * (1.f - u);
      if (u >= 1.f) next(m, 1);
      break;
    }
    case 1:   // hovers over the cap
      b->pos = Vec2{m.p1.x + 5.f * sinT(m.t * 0.55f) + 0.6f * buzz, m.p1.y - 3.f + 2.f * sinT(m.t * 0.8f)};
      if (m.t > 2.4f) next(m, 2);
      break;
    case 2: {   // lands on the cap a moment
      const Vec2 cap{(float)w.land.mush_x + 1.f, (float)w.land.mush_y - 12.f};
      const float u = clamp01(m.t / 0.5f);
      b->pos = Vec2{lerp(b->pos.x, cap.x, u), lerp(b->pos.y, cap.y, u)};
      if (m.t > 1.8f) {
        m.p2 = b->pos;
        if (hikerSitting(w) && hikerIdle(w, 6.f) && !m.quit) {
          next(m, 3);
        } else {
          next(m, 7);
        }
      }
      break;
    }
    case 3: {   // to his face
      const Vec2 nose = noseOf(w);
      const float u = clamp01(m.t / 2.0f);
      const Vec2 ctl{lerp(m.p2.x, nose.x, 0.5f), minf(m.p2.y, nose.y) - 40.f};
      b->pos = bez(m.p2, ctl, Vec2{nose.x + (float)w.hiker.facing * 8.f, nose.y - 4.f}, easeInOut(u));
      b->pos.y += buzz * 0.6f;
      b->land_y = w.hiker.feet.y + 2.f;
      if (u >= 1.f) next(m, 4);
      if (!hikerSitting(w)) next(m, 7);
      break;
    }
    case 4: {   // buzzes about his face, then lands on his nose
      const Vec2 nose = noseOf(w);
      const float k = clamp01((m.t - 0.9f) / 0.4f);
      const Vec2 hov{nose.x + (float)w.hiker.facing * (7.f + 3.f * sinT(m.t * 0.9f)) + buzz, nose.y - 4.f + buzz};
      b->pos = Vec2{lerp(hov.x, nose.x, k), lerp(hov.y, nose.y - 1.f, k)};
      if (m.t > 2.6f) next(m, 5);
      if (!hikerSitting(w)) next(m, 7);
      break;
    }
    case 5: {   // the swat: hand up to the face fast; the bee is off as it comes
      b->pos = Vec2{noseOf(w).x, noseOf(w).y - 1.f};
      m.fx_arm = true;
      const float u = clamp01(m.t / 0.22f);
      fx.arm_w[0] = easeOutCubic(u);
      fx.arm_sh[0] = 1.5f;   // upper arm out level, forearm back up to the face
      fx.arm_el[0] = 2.1f;
      if (u >= 1.f) {
        m.p2 = b->pos;
        next(m, 6);
      }
      break;
    }
    case 6: {   // the hand flicks out and drops; the bee shoots off up and away
      const float u = clamp01(m.t / 0.6f);
      fx.arm_sh[0] = lerp(1.5f, 2.3f, minf(1.f, u * 2.f));
      fx.arm_el[0] = lerp(2.1f, 0.3f, minf(1.f, u * 2.f));
      fx.arm_w[0] = u < 0.5f ? 1.f : 1.f - easeInOut((u - 0.5f) * 2.f);
      const float v = clamp01(m.t / 1.4f);
      b->pos = Vec2{m.p2.x - (float)w.hiker.facing * 90.f * v + 3.f * sinT(m.t * 4.f), m.p2.y - 200.f * v * v};
      if (u >= 1.f) {
        fx.arm_w[0] = 0.f;
        m.fx_arm = false;
      }
      if (v >= 1.f) {
        fx.arm_w[0] = 0.f;
        m.fx_arm = false;
        freeBit(w, 0);
        letGo(m);
      }
      break;
    }
    case 7: {   // off and up out of the frame
      if (m.t <= kDt) m.p2 = b->pos;
      const float v = clamp01(m.t / 2.6f);
      b->pos = Vec2{m.p2.x - 80.f * v + 5.f * sinT(m.t * 1.9f), m.p2.y - (m.p2.y + 12.f) * easeInOut(v)};
      if (v >= 1.f) {
        freeBit(w, 0);
        letGo(m);
      }
      break;
    }
    default: break;
  }
}

// cloud_animal: one of the clouds grows a lobe that reads as an ear for a few
// seconds; he looks up at it if he is idle.
bool startCloud(World& w) {
  Minors& m = w.minors;
  if (buildSkyFade(w.build) < 1.f) return false;
  int best = -1;
  for (int i = 0; i < 2; ++i) {
    const Cloud& c = w.clouds[i];
    const float cx = c.x + 0.5f * (c.bx0 + c.bx1);
    if (cx > 200.f && cx < 700.f && (best < 0 || m.rng.chance(0.5f))) best = i;
  }
  if (best < 0) return false;
  m.cloud = (int8_t)best;
  m.fx_head = false;
  m.n = 0;   // he has not looked yet
  return true;
}

void stepCloud(World& w, Renderer& r) {
  Minors& m = w.minors;
  Cloud& c = w.clouds[m.cloud];
  constexpr float kGrow = 2.6f, kHold = 6.0f, kShrink = 2.6f;
  const float t = m.age;
  const float before = c.ear;
  if (t < kGrow) c.ear = easeInOut(t / kGrow);
  else if (t < kGrow + kHold) c.ear = 1.f;
  else c.ear = easeInOut(maxf(0.f, 1.f - (t - kGrow - kHold) / kShrink));
  if (c.ear != before) {
    const Rect b = cloudBounds(c).clipped(Rect{0, 0, SS_WIDTH, SS_HEIGHT});
    if (!b.empty()) r.markDirtyRect(b);
  }
  // He looks up at it, a while after it appears, if nothing else has him.
  // (Once, starting while the ear is up, and only in a pause with time left in it.)
  constexpr float kLookS = 3.2f;
  if (!m.fx_head && m.n == 0 && t >= 1.5f && t <= kGrow + kHold - kLookS + 1.f && hikerIdle(w, kLookS)) {
    m.fx_head = true;
    m.n = 1;
    m.u0 = t;
  }
  if (m.fx_head) {
    const float u = (t - m.u0) / kLookS;
    if (!isPause(m) || !hikerIn(w) || w.hiker.walking || u >= 1.f) {
      w.fx.head = 0.f;
      m.fx_head = false;
    } else {
      w.fx.head = -0.42f * envelope(u, 0.25f, 0.3f);
    }
  }
  if (t >= kGrow + kHold + kShrink) {
    c.ear = 0.f;
    if (m.fx_head) w.fx.head = 0.f;
    m.fx_head = false;
    m.cloud = -1;
    letGo(m);
  }
}

// ---- the pool ------------------------------------------------------------------------------

// Can this minor play in the scene as it is? (Gates; start may still decline.)
bool gate(const World& w, int id) {
  const Minors& m = w.minors;
  switch (id) {
    case MN_OWL_WATCH: return dark(m);
    case MN_MOTH_LANTERN: return dark(m) && w.tent.lit && w.tent.state == TENT_UP;
    case MN_MOUSE_TENT: return w.tent.state == TENT_UP;
    case MN_BIRD_CHIP: return w.fire.stones >= 1.f;
    default: return true;
  }
}

bool startMinor(World& w, int id) {
  switch (id) {
    case MN_CHIPMUNK_CROSS: return startChip(w);
    case MN_SQUIRREL_NUT: return startSquirrel(w);
    case MN_FROG_SPLASH: return startFrog(w);
    case MN_BIRD_CHIP: return startBird(w);
    case MN_DEER_EDGE: return startDeer(w);
    case MN_OWL_WATCH: return startOwl(w);
    case MN_MOUSE_TENT: return startMouse(w);
    case MN_LEAF_FALL: return startLeaf(w);
    case MN_MOTH_LANTERN: return startMoth(w);
    case MN_BEE_VISIT: return startBee(w);
    case MN_CLOUD_ANIMAL: return startCloud(w);
    default: return false;
  }
}

void stepMinor(World& w, Renderer& r) {
  switch (w.minors.cur) {
    case MN_CHIPMUNK_CROSS: stepChip(w); break;
    case MN_SQUIRREL_NUT: stepSquirrel(w); break;
    case MN_FROG_SPLASH: stepFrog(w); break;
    case MN_BIRD_CHIP: stepBird(w); break;
    case MN_DEER_EDGE: stepDeer(w); break;
    case MN_OWL_WATCH: stepOwl(w); break;
    case MN_MOUSE_TENT: stepMouse(w); break;
    case MN_LEAF_FALL: stepLeaf(w); break;
    case MN_MOTH_LANTERN: stepMoth(w); break;
    case MN_BEE_VISIT: stepBee(w); break;
    case MN_CLOUD_ANIMAL: stepCloud(w, r); break;
    default: break;
  }
}

bool inRing(const Minors& m, int id) {
  for (int i = 0; i < m.ring_n; ++i)
    if (m.ring[i] == id) return true;
  return false;
}

uint8_t lastPick(const Minors& m) {
  if (m.ring_n == 0) return MN_NONE;
  return m.ring[(m.ring_i + MINOR_RING - 1) % MINOR_RING];
}

// Our critters still in the scene.
int heldCritters(const World& w) {
  int n = 0;
  for (int k = 0; k < 2; ++k) {
    const int s = w.minors.slot[k];
    if (s >= 0 && w.crit[s].in_use && w.crit[s].tag == CT_MINOR) ++n;
  }
  return n;
}

// Anyone else's critter in the scene (a dinner guest still fleeing, a major's).
bool othersCritters(const World& w) {
  for (const CritterSlot& s : w.crit)
    if (s.in_use && s.tag != CT_MINOR) return true;
  return false;
}

void finish(World& w) {
  Minors& m = w.minors;
  if (m.fx_head) w.fx.head = 0.f;
  if (m.fx_arm) w.fx.arm_w[0] = 0.f;
  m.fx_head = m.fx_arm = false;
  if (m.cloud >= 0) w.clouds[m.cloud].ear = 0.f;
  m.cloud = -1;
  // A moth or bee still flying is cut (only a forced end gets here with one).
  for (int k = 0; k < 2; ++k) {
    const Bit* b = bitOf(w, k);
    if (b && (b->kind == BIT_MOTH || b->kind == BIT_BEE)) freeBit(w, k);
    m.bit[k] = -1;
  }
  m.slot[0] = m.slot[1] = -1;
  m.cur = MN_NONE;
  const uint32_t q = w.tick + ticksOf(kAfterMinor);
  if (m.hold_tick < q) m.hold_tick = q;
}

void pickAndStart(World& w) {
  Minors& m = w.minors;
  float wt[MN_COUNT];
  float sum = 0.f;
  // The moth has one short window a loop (the lit tent before the bear): when
  // it is open and the moth has not been, it goes first.
  const bool moth_now = gate(w, MN_MOTH_LANTERN) && m.fired_by[MN_MOTH_LANTERN] == 0;
  for (int pass = 0; pass < 2 && sum <= 0.f; ++pass) {
    sum = 0.f;
    for (int i = 0; i < MN_COUNT; ++i) {
      const bool ringed = pass == 0 ? inRing(m, i) : i == lastPick(m);
      wt[i] = gate(w, i) && !ringed ? m.weight[i] : 0.f;
      if (moth_now && i == MN_MOTH_LANTERN) wt[i] = 1000.f;
      sum += wt[i];
    }
  }
  if (sum <= 0.f) {
    m.next_tick = w.tick + ticksOf(1.f);
    return;
  }
  const int id = m.rng.weighted(wt, MN_COUNT);
  m.slot[0] = m.slot[1] = -1;
  m.bit[0] = m.bit[1] = -1;
  m.leave_side[0] = m.leave_side[1] = 0;
  m.cloud = -1;
  m.stage = 0;
  m.t = m.age = 0.f;
  m.quit = false;
  m.fx_head = m.fx_arm = false;
  if (!startMinor(w, id)) {   // the scene declined (he is in the way): try again in a second
    for (int k = 0; k < 2; ++k) {
      if (m.slot[k] >= 0) directorRelease(w, m.slot[k]);
      if (m.bit[k] >= 0) freeBit(w, k);
    }
    m.slot[0] = m.slot[1] = -1;
    m.next_tick = w.tick + ticksOf(1.f);
    return;
  }
  m.cur = (uint8_t)id;
  m.last_start = w.tick;
  ++m.fired;
  if (m.fired_by[id] < 255) ++m.fired_by[id];
  m.ring[m.ring_i] = (uint8_t)id;
  m.ring_i = (uint8_t)((m.ring_i + 1) % MINOR_RING);
  if (m.ring_n < MINOR_RING) ++m.ring_n;
  const float gap = m.rng.range(kGapMin, kGapMax);
  m.next_tick = w.tick + ticksOf(gap);
}

}  // namespace

// ---- public --------------------------------------------------------------------------------

void minorsInit(World& w, const Rng& rng) {
  Minors& m = w.minors;
  memset(static_cast<void*>(&m), 0, sizeof m);
  Rng copy = rng;   // the loop RNG is not advanced
  const uint32_t a = copy.next();
  // From the loop RNG alone (not the loop count), so `--seed <a loop's seed>`
  // replays that loop's minors too.
  m.rng.seed(((uint64_t)a << 32) ^ 0x6D696E6F72730000ull, 0x4D494E4F52530001ull);
  for (int i = 0; i < MN_COUNT; ++i) {
    const float k = m.rng.range(0.6f, 1.4f);
    m.weight[i] = kBaseWeight[i] * k;
  }
  m.cur = MN_NONE;
  m.slot[0] = m.slot[1] = -1;
  m.bit[0] = m.bit[1] = -1;
  m.cloud = -1;
  const float first = m.rng.range(kGapMin, kGapMax);
  m.next_tick = ticksOf(first);
  m.beat = "";
  m.beat_idx = m.prev_idx = -1;
  m.mood = 3.f;
}

void minorsSync(World& w, const Timeline& tl, const Inputs& in) {
  Minors& m = w.minors;
  const Beat* b = tl.currentBeat();
  m.beat = b ? b->name : "";
  m.beat_idx = tl.current;
  m.beat_left = b ? b->duration - tl.elapsed : 0.f;
  m.mood = in.mood;
  m.night = in.night_mode;
}

bool worldBeatOwnsCritters(const World& w) {
  static const char* const kCalm[] = {"pause",      "arrival",     "setup",       "firewood", "fire",
                                      "foodbag",    "mug_sunset",  "read",        "cairn",    "skip_stone",
                                      "cloud_watch", "stream_listen"};
  if (w.busy) return true;   // a major, dinner, bear or close says so itself
  const char* b = w.minors.beat;
  if (!b || !*b) return true;
  for (const char* k : kCalm)
    if (strcmp(b, k) == 0) return false;
  return true;   // dinner, bear, close, build, and whatever this file does not know (majors)
}

void minorsStep(World& w, Renderer& r) {
  Minors& m = w.minors;
  // A beat other than a pause just ended: a breath before the next minor.
  if (m.beat_idx != m.prev_idx) {
    if (m.prev_idx >= 0 && !m.prev_pause) {
      const uint32_t q = w.tick + ticksOf(kBeatHold);
      if (m.hold_tick < q) m.hold_tick = q;
    }
    m.prev_idx = m.beat_idx;
    m.prev_pause = isPause(m);
    // The hiker's head and arm are the new beat's.
    if (m.fx_head) w.fx.head = 0.f;
    if (m.fx_arm) w.fx.arm_w[0] = 0.f;
    m.fx_head = m.fx_arm = false;
  }
  const bool owned = worldBeatOwnsCritters(w);
  // The beat that owns the critters has just begun: a gust clears the leaves.
  if (owned && !m.owned_prev)
    for (Bit& b : w.bits)
      if (b.kind == BIT_LEAF && b.resting) blowLeaf(b);
  m.owned_prev = owned;
  gustStep(w);

  if (m.cur != MN_NONE) {
    m.t += kDt;
    m.age += kDt;
    const bool wrap = owned && !m.quit;
    if (owned) m.quit = true;
    if (m.stage != MINOR_ENDING) stepMinor(w, r);   // its own wrap-up (m.quit) first
    if (wrap) hurryOff(w);
    if (m.stage != MINOR_ENDING) {
      if (m.age > kMaxAge && m.stage != MINOR_ENDING) {   // never expected: let go of everything
        exitAll(w, 120.f);
        letGo(m);
        if (m.forced < 0xFFFF) ++m.forced;
      }
    }
    if (m.cur != MN_NONE && m.stage == MINOR_ENDING) {
      for (int k = 0; k < 2; ++k) {   // at the edge: the director takes it out
        Critter* c = own(w, k);
        if (c && m.leave_side[k] != 0 && critterArrived(*c)) {
          directorExit(w, m.slot[k], m.leave_side[k], m.leave_speed[k], (CritterAction)m.leave_gait[k]);
          m.leave_side[k] = 0;
        }
      }
      if (heldCritters(w) == 0) {
        finish(w);
      } else if (m.t > 12.f) {   // an exit that never got out of sight
        // One still on its way (scattered by him across the whole frame, say)
        // is left to get out; only one that has stopped in sight is released.
        bool moving = false;
        for (int k = 0; k < 2; ++k)
          if (m.slot[k] >= 0 && w.crit[m.slot[k]].tag == CT_MINOR && w.critters[m.slot[k]].moving) moving = true;
        if (!moving) {
          for (int k = 0; k < 2; ++k)
            if (m.slot[k] >= 0 && w.crit[m.slot[k]].tag == CT_MINOR) directorRelease(w, m.slot[k]);
          finish(w);
          if (m.forced < 0xFFFF) ++m.forced;
        }
      }
    }
    return;
  }

  // Nothing running: may one start now?
  if (owned || w.fade > 0.f || hikerWalking(w) || othersCritters(w)) return;
  // The weather has him (weather.h: the wind took his hat, or he is fanning
  // himself with it): wait until he has it back on.
  if (w.weather.gag == WG_HAT || w.weather.gag == WG_FAN) return;
  if (w.tick < m.hold_tick) return;
  const bool moth_early = gate(w, MN_MOTH_LANTERN) && m.fired_by[MN_MOTH_LANTERN] == 0 &&
                          (m.fired == 0 || w.tick >= m.last_start + ticksOf(kGapMin));
  if (w.tick < m.next_tick && !moth_early) return;
  pickAndStart(w);
}

const char* minorName(int id) { return id >= 0 && id < MN_COUNT ? kNames[id] : ""; }

const char* minorsName(const World& w) { return w.minors.cur < MN_COUNT ? kNames[w.minors.cur] : ""; }

int minorsForced(const World& w) { return w.minors.forced; }

int minorsCritters(const World& w) {
  int n = 0;
  for (const CritterSlot& s : w.crit) n += s.in_use && s.tag == CT_MINOR ? 1 : 0;
  return n;
}

}  // namespace ss
