// Fire and ice. See fireice.h.
#include "fireice.h"

#include <math.h>

#include "../anim/ease.h"
#include "../render/renderer.h"
#include "../ss_app.h"
#include "ambient.h"
#include "director.h"
#include "script.h"
#include "world.h"

namespace ss {

namespace {

constexpr float kDt = SS_TICK_DT;

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }

// ---- tuning (PROPOSED) ---------------------------------------------------------------------------

// Too hot. Seconds between new fires: mild about one per bucket run (he keeps even), urgent several.
constexpr float kSpawnMild = 10.f, kSpawnUrgent = 2.2f;
constexpr float kGrowMild = 5.f, kGrowUrgent = 3.f;   // seconds to reach size 1
constexpr float kBigUrgent = 1.35f;                   // urgent fires keep growing to this
constexpr float kDieDown = 0.06f;                     // size a second they lose once the arrows go
constexpr float kRunMild = 74.f, kRunUrgent = 104.f, kStrollHome = 46.f;

// Too cold: the warm circle's size U (1 reaches the frame's edges, kIceNone and up: no ice at all).
constexpr float kIceNone = 1.48f;
constexpr float kIceStep = 0.04f;                    // the cache is repainted at each step
constexpr uint8_t kIceNoneQ = 0;      // a cleared World (all zero) has no ice
constexpr float kIceMild = 0.92f, kIceUrgent = 0.38f;   // and a little smaller per extra arrow
constexpr float kCreepMild = 0.035f, kCreepUrgent = 0.06f, kMelt = 0.07f;   // U a second
constexpr float kBlazeMild = 1.25f, kBlazeUrgent = 1.6f;   // the campfire's size (1 its usual best)
constexpr float kWalkCold = 50.f, kWalkColdUrgent = 60.f;
// The big blaze is tall (ambient.cpp kBlazeTall), so in the cold he and the dogs keep this much
// farther back from it than from a good fire (px): he feeds it a log from arm's length a third of
// the way back, then steps back the whole way to warm himself (Mike, 2026-11-12: "looks like he's
// gonna burn").
constexpr float kClearMild = 22.f, kClearUrgent = 50.f;
float blazeClear(const FireIce& f) { return f.mode == FI_COLD ? (f.urgent ? kClearUrgent : kClearMild) : 0.f; }

constexpr int kWarmSteps = 3;   // urgent heat: the light this many steps warmer
// The foreground pines' icicles, times the camp pine's (they are nearer): the far one and the near one.
constexpr float kFgIcicleFar = 1.35f, kFgIcicleNear = 1.6f;
constexpr int kFgIcicleMax = 14;   // px the longest hangs below a tier (the repaint's reach)

// ---- stages ---------------------------------------------------------------------------------------

enum Stage : uint8_t {
  S_START = 0,     // takes it in: a start at the fire, a shiver at the cold
  H_GET,           // to his pack for the bucket
  H_GET_CROUCH,    // the bucket comes out
  H_TO_WATER, H_FILL, H_TO_FIRE, H_DOUSE,
  H_GUARD,         // nothing burning: waits with a full bucket, looking round
  H_SETDOWN, H_WIPE, H_PICKUP, H_STOW, H_STOW_CROUCH,
  C_TO_WOOD, C_LOAD, C_TO_FIRE, C_FEED, C_WARM,
  C_DONE,          // the ice gone: a stretch
  S_HOME,          // back to where he was
  S_SETTLE         // into the pose he had
};

uint32_t nextR(FireIce& f) {
  uint32_t x = f.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  f.rng = x ? x : 0x9E3779B9u;
  return f.rng;
}
float rnd(FireIce& f, float a, float b) { return a + (b - a) * (float)(nextR(f) >> 8) * (1.f / 16777216.f); }

inline FiBox box(const Rect& r) { return FiBox{(int16_t)r.x0, (int16_t)r.y0, (int16_t)r.x1, (int16_t)r.y1}; }
inline Rect rectOf(const FiBox& b) { return Rect{b.x0, b.y0, b.x1, b.y1}; }
inline bool boxEmpty(const FiBox& b) { return b.x1 <= b.x0 || b.y1 <= b.y0; }
const Rect kEmpty{0, 0, 0, 0};

inline void unite(Rect& r, const Rect& b) {
  if (b.empty()) return;
  if (r.empty()) {
    r = b;
    return;
  }
  r = Rect{r.x0 < b.x0 ? r.x0 : b.x0, r.y0 < b.y0 ? r.y0 : b.y0, r.x1 > b.x1 ? r.x1 : b.x1, r.y1 > b.y1 ? r.y1 : b.y1};
}
inline Rect boundsOf(float x0, float y0, float x1, float y1) {
  return Rect{(int)floorf(x0), (int)floorf(y0), (int)ceilf(x1), (int)ceilf(y1)};
}
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }

// Where it was and where it is, if either shows (fires flicker, so they mark every tick).
void mark(Renderer& r, FiBox& prev, const Rect& cur, bool changed) {
  const Rect p = rectOf(prev);
  const bool same = p.x0 == cur.x0 && p.y0 == cur.y0 && p.x1 == cur.x1 && p.y1 == cur.y1;
  if (!changed && same) return;
  if (!p.empty()) r.markDirtyRect(p);
  if (!cur.empty()) r.markDirtyRect(cur);
  prev = cur.empty() ? FiBox{0, 0, 0, 0} : box(cur);
}

// ---- the hiker's walks ------------------------------------------------------------------------------

// The pit is in the way of the straight line from p to q (he goes round the back of it).
bool pitInWay(const World& w, Vec2 p, Vec2 q) {
  const FirePit& F = w.fire;
  if (F.stones < 0.5f && F.intensity <= 0.f) return false;
  const float x0 = minf(p.x, q.x), x1 = maxf(p.x, q.x);
  if (F.pos.x < x0 - 2.f || F.pos.x > x1 + 2.f || x1 - x0 < 1.f) return false;
  const float y = lerp(p.y, q.y, (F.pos.x - p.x) / (q.x - p.x));
  return y > F.pos.y - 16.f && y < F.pos.y + 14.f;
}

void leg(World& w, Vec2 to) {
  Hiker& h = w.hiker;
  HikerFx& fx = w.fx;
  // A walk only moves x: a target straight above or below him is stepped a little to the side (one
  // almost where he is, he just steps onto).
  if (absf(to.x - h.feet.x) < 6.f) {
    if (absf(to.y - h.feet.y) < 3.f) {
      h.feet = to;
      return;
    }
    to.x = h.feet.x + (to.x >= h.feet.x ? 6.f : -6.f);
  }
  fx.path_on = true;
  fx.path_x0 = h.feet.x;
  fx.path_y0 = h.feet.y;
  fx.path_x1 = to.x;
  fx.path_y1 = to.y;
  hikerWalkTo(h, to.x, w.fi.speed);
}

void walkTo(World& w, Vec2 to, float speed) {
  FireIce& f = w.fi;
  to.x = to.x < 24.f ? 24.f : (to.x > 636.f ? 636.f : to.x);
  f.go = to;
  f.speed = speed;
  f.use_via = pitInWay(w, w.hiker.feet, to);
  f.via = Vec2{w.fire.pos.x, w.fire.pos.y - 20.f - 0.6f * blazeClear(f)};   // well behind a big blaze
  leg(w, f.use_via ? f.via : to);
}

// Arrived where walkTo sent him (the second leg starts here when there is one).
bool walked(World& w) {
  FireIce& f = w.fi;
  if (w.hiker.walking) return false;
  if (f.use_via) {
    f.use_via = false;
    leg(w, f.go);
    return false;
  }
  return true;
}

void faceX(World& w, float x) { w.hiker.facing = x < w.hiker.feet.x ? -1 : 1; }

void stage(FireIce& f, uint8_t s) {
  f.stage = s;
  f.st = 0.f;
}

// Crossed `at` seconds into the stage this tick.
inline bool at(const FireIce& f, float s) { return f.st >= s && f.st - kDt < s; }

// 0 at both ends, 1 between a+in and b-out.
float env(float t, float a, float b, float in, float out) {
  if (t <= a || t >= b) return 0.f;
  return easeInOut(minf(1.f, minf((t - a) / in, (b - t) / out)));
}

void fxRest(World& w) {
  HikerFx& fx = w.fx;
  fx.lean = fx.head = fx.lift = fx.reach = fx.armload = fx.stomp = 0.f;
  for (int i = 0; i < 2; ++i) fx.arm_w[i] = fx.arm_sh[i] = fx.arm_el[i] = 0.f;
}

// ---- too hot ----------------------------------------------------------------------------------------

int burning(const FireIce& f) {
  int n = 0;
  for (const FiFlame& q : f.f) n += q.k > 0.f ? 1 : 0;
  return n;
}

// The biggest fire (the oldest of equals); -1 when none burns.
int pickTarget(const FireIce& f) {
  int best = -1;
  for (int i = 0; i < FI_FIRES; ++i)
    if (f.f[i].k > 0.f && (best < 0 || f.f[i].k > f.f[best].k)) best = i;
  return best;
}

bool spotFree(const World& w, float x, float y) {
  if (x < 40.f || x > 630.f || y < 334.f || y > 466.f) return false;
  if (x > 620.f && y > 400.f) return false;   // the chip's corner
  float cy, half;
  if (landscapeStreamSpan(w.land, x, cy, half) && absf(y - cy) < 1.06f * half + 9.f) return false;
  const Vec2 F = w.fire.pos;
  if (absf(x - F.x) < 38.f && absf(y - F.y) < 16.f) return false;   // the campfire is its own
  const Hiker& h = w.hiker;
  if (absf(x - h.feet.x) < 26.f && absf(y - h.feet.y) < 14.f) return false;
  for (int k = 0; k < HD_COUNT; ++k) {   // never near a house dog, nor where they lie out of the way
    const int s = w.dogs.d[k].slot;
    if (s >= 0 && w.crit[s].in_use) {
      const Vec2 p = w.critters[s].feet;
      if (absf(x - p.x) < 46.f && absf(y - p.y) < 22.f) return false;
    }
  }
  if (w.dogs.want && x < w.spine.camp.tent.x - 10.f && x > w.spine.camp.tent.x - 110.f && absf(y - w.spine.camp.tent.y - 12.f) < 24.f)
    return false;
  for (const FiFlame& q : w.fi.f)
    if (q.k > 0.f && absf(x - (float)q.x) < 34.f && absf(y - (float)q.y) < 16.f) return false;
  return true;
}

// A new fire on the grass, by the wood pile or near the tent.
void spawnFire(World& w) {
  FireIce& f = w.fi;
  int slot = -1;
  for (int i = 0; i < FI_FIRES && slot < 0; ++i)
    if (f.f[i].k <= 0.f) slot = i;
  if (slot < 0) return;
  const CampLayout& c = w.spine.camp;
  const Vec2 F = w.fire.pos;
  const Vec2 spots[10] = {{c.tent.x + 48.f, c.tent.y + 12.f}, {c.wood.x + 2.f, c.wood.y + 4.f}, {F.x + 72.f, F.y - 14.f},
                          {F.x - 112.f, F.y + 22.f},          {F.x + 150.f, F.y - 26.f},        {F.x - 60.f, F.y + 44.f},
                          {F.x + 205.f, F.y + 8.f},           {F.x + 112.f, F.y + 46.f},        {F.x - 150.f, F.y - 22.f},
                          {F.x + 58.f, F.y + 34.f}};
  for (int tries = 0; tries < 14; ++tries) {
    const int k = (int)(nextR(f) % 10u);
    const float x = spots[k].x + rnd(f, -12.f, 12.f);
    const float y = spots[k].y + rnd(f, -6.f, 6.f);
    if (!spotFree(w, x, y)) continue;
    f.f[slot] = FiFlame{(int16_t)x, (int16_t)y, 0.08f};
    return;
  }
}

void stepFires(World& w) {
  FireIce& f = w.fi;
  if (f.mode != FI_HOT) return;
  const float lv = (float)f.level;
  const float grow = f.urgent ? kGrowUrgent : kGrowMild;
  const float big = f.urgent ? kBigUrgent : 1.f;
  for (FiFlame& q : f.f) {
    if (q.k <= 0.f) continue;
    if (f.ending) {
      q.k -= kDieDown * kDt;
      if (q.k <= 0.05f) q.k = 0.f;
    } else {
      q.k = minf(big, q.k + kDt / grow);
    }
  }
  if (f.ending) return;
  f.spawn -= kDt;
  const int cap = f.urgent ? (int)minf(6.f, 3.f + lv) : (int)minf(4.f, 1.f + lv);
  if (f.spawn <= 0.f) {
    if (burning(f) < cap) spawnFire(w);
    const float gap = f.urgent ? kSpawnUrgent * 2.f / maxf(2.f, lv) : kSpawnMild / (0.7f + 0.3f * lv);
    f.spawn = gap * rnd(f, 0.7f, 1.3f);
  }
}

// The bucket hangs from his near hand by its bail; `tilt` pours it toward the way he faces.
// Local shape (hand at 0,0, y down), turned by phi.
struct BucketPts {
  Vec2 body[4], water[4], bail[3];
};
void bucketPts(Vec2 o, float phi, BucketPts& b) {
  const float c = cosT(phi * 0.15915494f), s = sinT(phi * 0.15915494f);   // table sines: the PC and the board agree
  auto P = [&](float x, float y) { return Vec2{o.x + x * c - y * s, o.y + x * s + y * c}; };
  b.body[0] = P(-6.2f, 7.f);
  b.body[1] = P(6.2f, 7.f);
  b.body[2] = P(4.8f, 18.f);
  b.body[3] = P(-4.8f, 18.f);
  b.water[0] = P(-5.5f, 8.2f);
  b.water[1] = P(5.5f, 8.2f);
  b.water[2] = P(5.3f, 10.f);
  b.water[3] = P(-5.3f, 10.f);
  b.bail[0] = P(-5.8f, 7.1f);
  b.bail[1] = P(0.f, 0.f);
  b.bail[2] = P(5.8f, 7.1f);
}
void drawBucket(Vec2 o, float phi, float water, Surface& s, const Rect& clip, const Palette& pal) {
  BucketPts b;
  bucketPts(o, phi, b);
  strokeCapsule(s, b.bail[0], b.bail[1], 1.3f, pal[ACTOR], clip);
  strokeCapsule(s, b.bail[1], b.bail[2], 1.3f, pal[ACTOR], clip);
  fillPolygon(s, b.body, 4, pal[ACTOR], clip);
  if (water > 0.15f && absf(phi) < 0.5f) fillPolygon(s, b.water, 4, pal[WATER_LIGHT], clip);
}
Rect bucketBox(Vec2 o) { return boundsOf(o.x - 21.f, o.y - 21.f, o.x + 22.f, o.y + 22.f); }
// The bucket's mouth (where the pour leaves it).
Vec2 bucketMouth(Vec2 o, float phi, int facing) {
  BucketPts b;
  bucketPts(o, phi, b);
  return facing > 0 ? b.body[1] : b.body[0];
}

// ---- too cold ----------------------------------------------------------------------------------------

float iceTarget(const FireIce& f) {
  if (f.ending || f.mode != FI_COLD) return kIceNone;
  const float lv = (float)f.level;
  return f.urgent ? maxf(0.26f, kIceUrgent - 0.04f * (lv - 2.f)) : maxf(0.62f, kIceMild - 0.1f * (lv - 1.f));
}

// ice 0 (a cleared World) is none too; a real circle is never smaller than 0.26, so its step is 7 or more.
uint8_t iceQ(float ice) { return ice <= 0.f || ice >= kIceNone - 1e-4f ? kIceNoneQ : (uint8_t)(ice / kIceStep + 0.5f); }
uint8_t icicleQ(float ice) { return ice <= 0.f ? 0 : (ice < 0.6f ? 2 : (ice < 1.15f ? 1 : 0)); }

void stepIce(World& w) {
  FireIce& f = w.fi;
  const float to = iceTarget(f);
  const float v = to > f.ice ? kMelt : (f.urgent ? kCreepUrgent : kCreepMild);
  if (f.ice < to) f.ice = minf(to, f.ice + v * kDt);
  else if (f.ice > to) f.ice = maxf(to, f.ice - v * kDt);
  const uint8_t tq = icicleQ(f.ice);
  if (tq != f.tent_q) {
    f.tent_q = tq;
    // A frost cap on the ridge with the icicles (the snow's own drawing), unless it is snowing.
    if (w.weather.kind != WX_SNOW) w.tent.snow = w.tent.state == TENT_UP ? 0.5f * (float)tq : 0.f;
  }
  if (f.mode == FI_COLD) {
    // The campfire: a big blaze while the cold lasts, flaring with each armload; back to what it was after.
    FirePit& p = w.fire;
    if (p.state == FIRE_LIT && !f.ending) {
      const float base = f.urgent ? kBlazeUrgent : kBlazeMild;
      p.fuel = base + 0.25f * maxf(0.f, 1.f - f.feed_t / 2.5f);
    }
    f.feed_t += kDt;
  }
}

// The fire pit back as it was (the blaze dies down to it on its own).
void pitBack(World& w) {
  FireIce& f = w.fi;
  w.fire.state = f.pit_state;
  w.fire.fuel = f.pit_fuel;
  w.amb.smoke_k_to = f.pit_smoke;
}

// ---- modes -------------------------------------------------------------------------------------------

void beginMode(World& w, uint8_t mode) {
  FireIce& f = w.fi;
  f.mode = mode;
  f.ending = false;
  f.t = 0.f;
  f.target = -1;
  f.spawn = 0.4f;   // the first fire at once
  f.feed_t = 9.f;
  f.breath_t = -1.f;
  stage(f, S_START);
  if (mode == FI_HOT) hikerSetPose(w.hiker, POSE_YELP, 0.25f);
  else hikerSetPose(w.hiker, POSE_HUG, 0.45f);
}

// Wound down: the next mode, or home.
void modeDone(World& w) {
  FireIce& f = w.fi;
  f.mode = FI_NONE;
  f.ending = false;
  fxRest(w);
  if (f.want != FI_NONE) {
    beginMode(w, f.want);
    return;
  }
  hikerSetPose(w.hiker, POSE_STAND, 0.3f);
  walkTo(w, f.home, kStrollHome);
  stage(f, S_HOME);
}

void finish(World& w) {
  FireIce& f = w.fi;
  HikerFx& fx = w.fx;
  fxRest(w);
  fx.path_on = f.home_path;
  fx.path_x0 = f.home_path_v[0];
  fx.path_y0 = f.home_path_v[1];
  fx.path_x1 = f.home_path_v[2];
  fx.path_y1 = f.home_path_v[3];
  fx.reach_to = f.home_reach_to;
  w.hiker.feet = f.home;
  w.hiker.facing = f.home_face;
  if (f.home_walk) hikerWalkTo(w.hiker, f.home_walk_x, f.home_walk_v);   // and on his way again
  f.on = false;
  w.busy = false;
}

float runSpeed(const FireIce& f) { return f.urgent && !f.ending ? kRunUrgent : kRunMild; }
float coldSpeed(const FireIce& f) { return f.urgent ? kWalkColdUrgent : kWalkCold; }

void stepHot(World& w) {
  FireIce& f = w.fi;
  Hiker& h = w.hiker;
  HikerFx& fx = w.fx;
  const CampLayout& c = w.spine.camp;
  // Urgent: flat out, arms going (the stamping walk's arms), leaning into it.
  const bool hurry = f.urgent && !f.ending && h.walking;
  fx.stomp = hurry ? 0.55f : 0.f;
  switch (f.stage) {
    case S_START:
      if (at(f, 0.7f)) hikerSetPose(h, POSE_STAND, 0.25f);
      if (f.st >= 0.95f) {
        if (w.pack.shown && !w.pack.held) walkTo(w, c.pack_work, runSpeed(f));
        stage(f, H_GET);
      }
      break;
    case H_GET:
      if (!walked(w)) break;
      if (w.pack.shown && !w.pack.held) faceX(w, w.pack.pos.x);
      hikerSetPose(h, POSE_CROUCH, 0.3f);
      stage(f, H_GET_CROUCH);
      break;
    case H_GET_CROUCH:
      if (at(f, 0.45f)) {
        f.bucket = 1;
        f.water = 0.f;
      }
      if (at(f, 0.6f)) hikerSetPose(h, POSE_STAND, 0.25f);
      if (f.st >= 0.85f) {
        walkTo(w, c.cool, runSpeed(f));
        stage(f, H_TO_WATER);
      }
      break;
    case H_TO_WATER:
      if (!walked(w)) break;
      hikerSetPose(h, POSE_CROUCH, 0.25f);
      fx.reach_to = 0.25f;   // the near arm straight down into the water
      stage(f, H_FILL);
      break;
    case H_FILL:
      fx.reach = 0.8f * env(f.st, 0.1f, 1.0f, 0.2f, 0.2f);
      f.water = clamp01((f.st - 0.35f) / 0.4f);
      if (f.st >= 1.0f) {
        fx.reach = 0.f;
        hikerSetPose(h, POSE_STAND, 0.25f);
        f.target = (int8_t)pickTarget(f);
        if (f.target >= 0) {
          const FiFlame& q = f.f[f.target];
          const float side = h.feet.x < (float)q.x ? -1.f : 1.f;
          walkTo(w, Vec2{(float)q.x + side * 24.f, (float)q.y - 1.f}, runSpeed(f));
          stage(f, H_TO_FIRE);
        } else if (f.ending) {
          stage(f, H_SETDOWN);
          hikerSetPose(h, POSE_CROUCH, 0.3f);
        } else {
          walkTo(w, c.spot, runSpeed(f));
          stage(f, H_GUARD);
        }
      }
      break;
    case H_GUARD:   // a full bucket and nothing burning: he waits, looking round
      if (walked(w) && h.target != POSE_LOOK_AROUND && !h.walking) hikerSetPose(h, POSE_LOOK_AROUND, 0.5f);
      f.target = (int8_t)pickTarget(f);
      if (f.target >= 0) {
        const FiFlame& q = f.f[f.target];
        const float side = h.feet.x < (float)q.x ? -1.f : 1.f;
        hikerSetPose(h, POSE_STAND, 0.2f);
        walkTo(w, Vec2{(float)q.x + side * 24.f, (float)q.y - 1.f}, runSpeed(f));
        stage(f, H_TO_FIRE);
      } else if (f.ending && !h.walking) {
        hikerSetPose(h, POSE_CROUCH, 0.3f);
        stage(f, H_SETDOWN);
      }
      break;
    case H_TO_FIRE:
      if (!walked(w)) break;
      if (f.target < 0 || f.f[f.target].k <= 0.f) {   // it died down on its own meanwhile
        f.target = (int8_t)pickTarget(f);
        if (f.target < 0) {
          walkTo(w, c.spot, kRunMild);
          stage(f, H_GUARD);
          break;
        }
        const FiFlame& q = f.f[f.target];
        walkTo(w, Vec2{(float)q.x + (h.feet.x < (float)q.x ? -24.f : 24.f), (float)q.y - 1.f}, runSpeed(f));
        break;
      }
      faceX(w, (float)f.f[f.target].x);
      stage(f, H_DOUSE);
      break;
    case H_DOUSE: {
      const float e = env(f.st, 0.f, 1.2f, 0.3f, 0.35f);
      fx.reach_to = 1.25f;
      fx.reach = e;
      fx.lean = 0.12f * e;
      f.tilt = 1.9f * env(f.st, 0.2f, 1.15f, 0.25f, 0.3f);
      if (f.target >= 0 && at(f, 0.3f)) {
        const FiFlame& q = f.f[f.target];
        const Vec2 o = hikerHandPos(h, 0);
        f.splash_a = bucketMouth(o, (float)h.facing * f.tilt, h.facing);
        f.splash_b = Vec2{(float)q.x, (float)q.y - 3.f};
        f.splash_t = 0.f;
      }
      if (f.st > 0.3f) f.water = maxf(0.f, 1.f - (f.st - 0.3f) / 0.3f);
      if (f.target >= 0 && at(f, 0.6f)) {   // out, with a hiss of steam
        FiFlame& q = f.f[f.target];
        f.steam = Steam{Vec2{(float)q.x, (float)q.y - 5.f}, 0.f, 4};
        q.k = 0.f;
        f.target = -1;
      }
      if (f.st >= 1.2f) {
        fx.reach = fx.lean = 0.f;
        f.tilt = 0.f;
        if (f.ending && burning(f) == 0) {
          hikerSetPose(h, POSE_CROUCH, 0.3f);
          stage(f, H_SETDOWN);
        } else {
          walkTo(w, c.cool, runSpeed(f));
          stage(f, H_TO_WATER);
        }
      }
      break;
    }
    case H_SETDOWN:
      if (at(f, 0.4f)) {
        f.bucket = 2;
        f.bucket_at = Vec2{h.feet.x + (float)h.facing * 13.f, h.feet.y + 1.f};
      }
      if (at(f, 0.6f)) hikerSetPose(h, POSE_STAND, 0.3f);
      if (f.st >= 0.9f) stage(f, H_WIPE);
      break;
    case H_WIPE: {   // the back of his near hand across his brow, and a big breath out
      const float e = env(f.st, 0.f, 2.2f, 0.35f, 0.4f);
      fx.arm_w[0] = e;
      fx.arm_sh[0] = 2.55f;
      fx.arm_el[0] = 1.55f + 0.35f * sinT(f.st * 1.4f);
      fx.head = 0.12f * e;
      fx.lean = -0.06f * env(f.st, 1.2f, 2.2f, 0.3f, 0.4f);
      if (f.st >= 2.2f) {
        fxRest(w);
        hikerSetPose(h, POSE_CROUCH, 0.3f);
        stage(f, H_PICKUP);
      }
      break;
    }
    case H_PICKUP:
      if (at(f, 0.4f)) f.bucket = 1;
      if (at(f, 0.6f)) hikerSetPose(h, POSE_STAND, 0.25f);
      if (f.st >= 0.85f) {
        if (w.pack.shown && !w.pack.held) walkTo(w, c.pack_work, kStrollHome);
        stage(f, H_STOW);
      }
      break;
    case H_STOW:
      if (!walked(w)) break;
      if (w.pack.shown && !w.pack.held) faceX(w, w.pack.pos.x);
      hikerSetPose(h, POSE_CROUCH, 0.3f);
      stage(f, H_STOW_CROUCH);
      break;
    case H_STOW_CROUCH:
      if (at(f, 0.45f)) f.bucket = 0;   // back on his pack
      if (at(f, 0.6f)) hikerSetPose(h, POSE_STAND, 0.25f);
      if (f.st >= 0.85f) modeDone(w);
      break;
    default: break;
  }
}

void stepCold(World& w) {
  FireIce& f = w.fi;
  Hiker& h = w.hiker;
  HikerFx& fx = w.fx;
  const CampLayout& c = w.spine.camp;
  const Vec2 F = w.fire.pos;
  fx.lean = f.urgent && !f.ending && h.walking ? 0.08f : (f.stage == C_WARM ? fx.lean : 0.f);   // hunched into the cold
  // Breath shows in the hard cold.
  if (f.urgent && !f.ending) {
    if (f.breath_t >= 0.f) f.breath_t += kDt;
    if (f.breath_t > 1.1f) f.breath_t = -1.f;
    if (f.breath_t < 0.f && fmodf(f.t, 1.9f) < kDt) f.breath_t = 0.f;
  } else if (f.breath_t >= 0.f) {
    f.breath_t += kDt;
    if (f.breath_t > 1.1f) f.breath_t = -1.f;
  }
  switch (f.stage) {
    case S_START:
      fx.lean = 0.03f * sinT(f.st * 9.f) * env(f.st, 0.2f, 1.2f, 0.2f, 0.2f);   // brr
      if (f.st >= 1.3f) {
        hikerSetPose(h, POSE_STAND, 0.3f);
        walkTo(w, c.wood_work, coldSpeed(f));
        stage(f, C_TO_WOOD);
      }
      break;
    case C_TO_WOOD:
      if (!walked(w)) break;
      faceX(w, c.wood.x);
      hikerSetPose(h, POSE_CROUCH, 0.3f);
      stage(f, C_LOAD);
      break;
    case C_LOAD:
      if (at(f, 0.5f)) {
        hikerCarry(h, CARRY_WOOD);
        hikerSetPose(h, POSE_CARRY, 0.3f);
      }
      if (f.st >= 0.8f) {
        walkTo(w, Vec2{c.blow.x + 0.35f * blazeClear(f), c.blow.y}, coldSpeed(f));   // arm's length
        stage(f, C_TO_FIRE);
      }
      break;
    case C_TO_FIRE:
      if (!walked(w)) break;
      faceX(w, F.x);
      hikerSetPose(h, POSE_CROUCH, 0.3f);
      stage(f, C_FEED);
      break;
    case C_FEED:
      fx.reach_to = 1.3f;
      fx.reach = 0.85f * env(f.st, 0.2f, 1.0f, 0.2f, 0.2f);
      if (at(f, 0.55f)) {   // on it goes
        hikerCarry(h, CARRY_NONE);
        if (w.fire.state != FIRE_LIT) {
          w.fire.state = FIRE_LIT;
          w.amb.smoke_k_to = 1.f;
        }
        f.feed_t = 0.f;
      }
      if (f.st >= 1.0f) {
        fx.reach = 0.f;
        hikerSetPose(h, f.urgent && !f.ending ? POSE_HUG : POSE_STAND, 0.4f);
        // Back from the blaze to warm himself (no step for a good fire's size).
        if (blazeClear(f) > 0.f) walkTo(w, Vec2{c.blow.x + blazeClear(f), c.blow.y + 2.f}, coldSpeed(f));
        stage(f, C_WARM);
      }
      break;
    case C_WARM: {   // mild: hands out to the blaze; urgent: arms hugged tight, shivering
      if (!walked(w)) {   // still stepping back: the warming starts once he is there, facing the fire
        f.st = 0.f;
        break;
      }
      faceX(w, F.x);
      const bool hug = f.urgent && !f.ending;
      const float hold = hug ? 2.4f : 3.6f;
      if (hug) {
        fx.lean = 0.025f * sinT(f.st * 8.5f) + 0.05f;
      } else {
        const float e = env(f.st, 0.2f, 1e6f, 0.4f, 0.4f);
        for (int i = 0; i < 2; ++i) {
          fx.arm_w[i] = 0.85f * e;
          fx.arm_sh[i] = 1.2f + 0.06f * sinT(f.st * 0.9f + 0.5f * (float)i);
          fx.arm_el[i] = 0.3f;
        }
        fx.lean = 0.04f * e;
      }
      if (f.ending) {
        if (f.ice >= kIceNone && f.ice_q == kIceNoneQ && f.st >= 1.f) {   // all melted: a stretch, and the fire settles
          fxRest(w);
          pitBack(w);
          hikerSetPose(h, POSE_REACH_UP, 0.5f);
          stage(f, C_DONE);
        }
      } else if (f.st >= hold) {
        fxRest(w);
        hikerSetPose(h, POSE_STAND, 0.3f);
        walkTo(w, c.wood_work, coldSpeed(f));
        stage(f, C_TO_WOOD);
      }
      break;
    }
    case C_DONE:
      if (at(f, 1.3f)) hikerSetPose(h, POSE_STAND, 0.4f);
      if (f.st >= 1.8f) modeDone(w);
      break;
    default: break;
  }
}

}  // namespace

// ---- the public face ---------------------------------------------------------------------------------

void fireIceSetInputs(World& w, const uint8_t ask[3]) {
  FireIce& f = w.fi;
  // The newest arrow decides the kind; the level counts that kind's arrows.
  uint8_t want = FI_NONE;
  for (int i = 0; i < 3; ++i) {
    const uint8_t a = ask[i];
    if (!a || (a & SS_REQ_PREDICTED)) continue;
    if (a == 1 || a == 2) want = FI_COLD;
    else if (a == 3 || a == 4) want = FI_HOT;
  }
  uint8_t level = 0;
  bool urgent = false;
  for (int i = 0; i < 3; ++i) {
    const uint8_t a = ask[i];
    if (!a || (a & SS_REQ_PREDICTED)) continue;
    const bool cold = a == 1 || a == 2, hot = a == 3 || a == 4;
    if ((want == FI_COLD && cold) || (want == FI_HOT && hot)) {
      const bool u = a == 1 || a == 4;
      level = (uint8_t)(level + (u ? 2 : 1));
      urgent = urgent || u;
    }
  }
  f.want = want;
  f.level = level;
  f.urgent = urgent;
  if (f.on && f.mode != FI_NONE && f.want != f.mode) f.ending = true;
}

bool fireIceHolds(const World& w) { return w.fi.on; }

float fireIceClearance(const World& w) { return w.fi.on ? blazeClear(w.fi) : 0.f; }

const char* fireIceStageName(const World& w) {
  static const char* const kNames[] = {"start", "get",     "get_crouch", "to_water", "fill",    "to_fire",
                                       "douse", "guard",   "setdown",    "wipe",     "pickup",  "stow",
                                       "stow_crouch", "to_wood", "load", "carry_wood", "feed", "warm",
                                       "stretch", "home", "settle"};
  if (!w.fi.on) return "";
  return w.fi.stage < sizeof kNames / sizeof kNames[0] ? kNames[w.fi.stage] : "?";
}

bool fireIceCanStart(const World& w) {
  const FireIce& f = w.fi;
  if (f.on || f.want == FI_NONE || visitorsHolds(w)) return false;
  return fireIceCalm(w);
}

bool fireIceCalm(const World& w) {
  if (spineSetPiece(w.plan.family)) return false;
  if (!buildDone(w.build) || w.fade > 0.f || !w.fx.on) return false;
  const Hiker& h = w.hiker;
  if (!h.visible || h.carry != CARRY_NONE) return false;
  if (w.hiker_full.x0 < 8 || w.hiker_full.x1 > SS_WIDTH - 150) return false;   // well in the frame, clear of the chip
  if (h.walking && !w.fx.path_on) return false;   // a walk the story placed by a path only
  if (h.target != POSE_STAND && h.target != POSE_SIT && h.target != POSE_LOOK_AROUND && h.target != POSE_HANDS_ON_HIPS &&
      h.target != POSE_SCRATCH_HEAD && h.target != POSE_CROUCH)
    return false;
  if (w.busy || w.react.active || w.major.on || w.day.kind != DAY_NONE) return false;
  // A minor may be under way only if it holds nothing that would hang in mid-air meanwhile (a critter,
  // a falling leaf) and leaves him alone.
  const Minors& mn = w.minors;
  if (mn.fx_head || mn.fx_arm) return false;
  if (mn.cur != MN_NONE && (mn.slot[0] >= 0 || mn.slot[1] >= 0 || mn.bit[0] >= 0 || mn.bit[1] >= 0)) return false;
  if (directorCount(w) > 0) return false;   // no story critter about
  if (w.pack.held || w.bag.state == BAG_FLYING || w.bag.state == BAG_CARRIED) return false;
  for (const Item& it : w.items)
    if (it.held && !it.critter) return false;
  if (w.boom.boom.on && w.boom.phase != BP_STUCK) return false;
  const Weather& x = w.weather;
  if (x.gag != WG_NONE || x.hug_w > 0.f || x.ov_w > 0.f || x.shiver > 0.f) return false;
  const HikerFx& fx = w.fx;
  if (fx.wade || fx.lean != 0.f || fx.head != 0.f || fx.lift != 0.f || fx.reach != 0.f || fx.armload != 0.f ||
      fx.stomp != 0.f || fx.arm_w[0] != 0.f || fx.arm_w[1] != 0.f)
    return false;
  // Holding still or walking in his script (or between scripts, in a pause).
  const ScriptRun& r = w.spine.run;
  if (r.steps && r.i < r.n) {
    const uint8_t op = r.steps[r.i].op;
    if (op != OP_WAIT && op != OP_POSE && op != OP_WALK && op != OP_VIA && op != OP_GO) return false;
    if (h.walking != (op == OP_WALK || op == OP_VIA || op == OP_GO)) return false;
  }
  return true;
}

void fireIceBegin(World& w, uint64_t seed) {
  FireIce& f = w.fi;
  const Hiker& h = w.hiker;
  const HikerFx& fx = w.fx;
  f.on = true;
  f.rng = (uint32_t)(seed ^ (seed >> 32)) | 1u;
  f.home = h.feet;
  f.home_face = (int8_t)h.facing;
  f.home_pose = (uint8_t)h.target;
  f.home_path = fx.path_on;
  f.home_path_v[0] = fx.path_x0;
  f.home_path_v[1] = fx.path_y0;
  f.home_path_v[2] = fx.path_x1;
  f.home_path_v[3] = fx.path_y1;
  f.home_reach_to = fx.reach_to;
  f.home_walk = h.walking;
  f.home_walk_x = h.walk_x;
  f.home_walk_v = h.walk_speed;
  f.pit_state = w.fire.state;
  f.pit_fuel = w.fire.fuel;
  f.pit_smoke = w.amb.smoke_k_to;
  f.bucket = 0;
  f.water = f.tilt = 0.f;
  f.splash_t = -1.f;
  f.steam = Steam{Vec2{0.f, 0.f}, -1.f, 0};
  if (f.ice <= 0.f) f.ice = kIceNone;
  w.busy = true;   // the house dogs lie out of the way; no pats, gags or minors meanwhile
  beginMode(w, f.want);
}

void fireIceStep(World& w) {
  FireIce& f = w.fi;
  if (!f.on) return;
  f.st += kDt;
  f.t += kDt;
  if (f.steam.t >= 0.f) {
    f.steam.t += kDt;
    if (f.steam.t > 2.f) f.steam.t = -1.f;
  }
  if (f.splash_t >= 0.f) {
    f.splash_t += kDt;
    if (f.splash_t > 0.6f) f.splash_t = -1.f;
  }
  stepFires(w);
  stepIce(w);
  // Urgent heat warms the light a step at a time.
  const float warm_to = f.mode == FI_HOT && f.urgent && !f.ending ? (float)kWarmSteps : 0.f;
  if (f.warm < warm_to) f.warm = minf(warm_to, f.warm + 0.8f * kDt);
  else if (f.warm > warm_to) f.warm = maxf(warm_to, f.warm - 0.8f * kDt);
  f.warm_q = (uint8_t)floorf(f.warm + 1e-4f);

  if (f.mode == FI_HOT) stepHot(w);
  else if (f.mode == FI_COLD) stepCold(w);
  else if (f.stage == S_HOME) {
    if (f.want != FI_NONE) {   // the arrows are back before he got home
      beginMode(w, f.want);
    } else if (walked(w)) {
      w.hiker.facing = f.home_face;
      hikerSetPose(w.hiker, (PoseId)f.home_pose, 0.5f);
      stage(f, S_SETTLE);
    }
  } else if (f.stage == S_SETTLE) {
    if (f.want != FI_NONE) beginMode(w, f.want);
    else if (f.st >= 0.6f && !w.hiker.blending && f.warm_q == 0 && f.ice_q == kIceNoneQ && f.pine_q == 0 && f.tent_q == 0 &&
             f.steam.t < 0.f && f.splash_t < 0.f && f.breath_t < 0.f)
      finish(w);
  }
}

bool fireIceWaveOk(const World& w) {
  const FireIce& f = w.fi;
  if (!f.on) return true;
  if (f.urgent && !f.ending) return false;
  const Hiker& h = w.hiker;
  return f.bucket == 0 && h.carry == CARRY_NONE && !h.walking &&
         (f.stage == H_GUARD || f.stage == C_WARM || f.stage == S_SETTLE || f.stage == C_DONE);
}

Rect fireIceHeldBounds(const World& w) {
  if (!w.fi.on || w.fi.bucket != 1 || !w.hiker.visible) return kEmpty;
  return bucketBox(hikerHandPos(w.hiker, 0));
}

bool fireIceActorY(const World& w, int idx, int16_t& y) {
  const FireIce& f = w.fi;
  if (!f.on) return false;
  if (idx < FI_FIRES) {
    if (f.f[idx].k <= 0.f) return false;
    y = f.f[idx].y;
    return true;
  }
  if (idx == FI_FIRES && f.bucket == 2) {
    y = (int16_t)floorf(f.bucket_at.y);
    return true;
  }
  return false;
}

namespace {

Rect nearBounds(const World& w) {
  const FireIce& f = w.fi;
  Rect r = kEmpty;
  unite(r, steamBounds(f.steam));
  if (f.splash_t >= 0.f)
    unite(r, boundsOf(minf(f.splash_a.x, f.splash_b.x) - 4.f, minf(f.splash_a.y, f.splash_b.y) - 22.f,
                      maxf(f.splash_a.x, f.splash_b.x) + 4.f, maxf(f.splash_a.y, f.splash_b.y) + 4.f));
  if (f.breath_t >= 0.f && w.hiker.visible) {
    float hr = 0.f;
    const Vec2 hp = hikerHeadPos(w.hiker, &hr);
    unite(r, boundsOf(hp.x - hr - 16.f, hp.y - hr - 8.f, hp.x + hr + 16.f, hp.y + hr + 8.f));
  }
  return r;
}

// The warm circle's shape for size U at row dy from the fire: 1 / half extents on each side.
struct Circle {
  float fx, fy, ix_l, ix_r, iy_u, iy_d;
};
Circle circleFor(const World& w, float U) {
  const Vec2 F = w.fire.pos;
  // Each side reaches the frame's edge at U 1; small circles are rounder (the same both sides).
  const float s = clamp01((U - 0.3f) / 0.7f);
  const float axl = lerp(240.f, maxf(60.f, F.x), s), axr = lerp(240.f, maxf(60.f, (float)SS_WIDTH - F.x), s);
  const float ayu = lerp(80.f, maxf(20.f, F.y - (float)LS_GROUND_Y), s), ayd = lerp(80.f, maxf(20.f, (float)SS_HEIGHT - F.y), s);
  return Circle{F.x, F.y, 1.f / (U * axl), 1.f / (U * axr), 1.f / (U * ayu), 1.f / (U * ayd)};
}
// Rows and columns the circle of size U covers (in the ground band).
Rect circleBox(const World& w, float U) {
  const Circle c = circleFor(w, U);
  const float pad = 1.25f;   // the ragged edge and the light band reach past it
  return boundsOf(c.fx - pad / c.ix_l, maxf((float)LS_GROUND_Y, c.fy - pad / c.iy_u), c.fx + pad / c.ix_r,
                  minf((float)SS_HEIGHT, c.fy + pad / c.iy_d));
}

// The camp pine's tiers as the landscape draws them grown (landscape.cpp drawCampPine): the icicles
// hang from each tier's lower edge.
Rect pineBox(const World& w) {
  const LsPine& p = w.land.camp;
  return Rect{p.x - 70, p.base_y - p.h, p.x + 71, LS_GROUND_Y + 12};
}

inline uint32_t hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u;
  h ^= h >> 13;
  h *= 0x9E3779B1u;
  return h ^ (h >> 16);
}

}  // namespace

void fireIceTrack(World& w, Renderer& r) {
  FireIce& f = w.fi;
  // Idle: one look at the boxes, all empty once a spell has gone.
  if (!f.on && boxEmpty(f.d_near) && boxEmpty(f.d_bucket) && f.ice_q == kIceNoneQ && f.pine_q == 0) {
    bool any = false;
    for (const FiBox& b : f.d_f) any = any || !boxEmpty(b);
    if (!any) return;
  }
  for (int i = 0; i < FI_FIRES; ++i) {
    const FiFlame& q = f.f[i];
    const Rect cur = f.on && q.k > 0.f ? smallFireBounds(Vec2{(float)q.x, (float)q.y}, q.k) : kEmpty;
    mark(r, f.d_f[i], cur, true);
  }
  mark(r, f.d_near, f.on ? nearBounds(w) : kEmpty, true);
  const Rect bk = f.on && f.bucket == 2 ? bucketBox(Vec2{f.bucket_at.x, f.bucket_at.y - 18.5f}) : kEmpty;
  mark(r, f.d_bucket, bk, false);

  // The ice in the cache: repaint the rows the circle's edge crosses, before and after a step.
  const uint8_t q = iceQ(f.ice);
  if (q != f.ice_q) {
    Rect a = f.ice_q == kIceNoneQ ? circleBox(w, kIceNone + 0.1f) : circleBox(w, (float)f.ice_q * kIceStep);
    unite(a, q == kIceNoneQ ? circleBox(w, kIceNone + 0.1f) : circleBox(w, (float)q * kIceStep));
    // Outside every circle bigger than the frame the frost is all or nothing: the whole ground band.
    if (f.ice_q == kIceNoneQ || q == kIceNoneQ) a = Rect{0, LS_GROUND_Y, SS_WIDTH, SS_HEIGHT};
    r.repaintRows(a.y0, a.y1);
    r.markDirtyRect(a);
    f.ice_q = q;
  }
  const uint8_t pq = icicleQ(f.ice);
  if (pq != f.pine_q) {   // the camp pine's icicles and the two foreground pines'
    Rect p = pineBox(w);
    for (int k = 0; k < 2; ++k) {
      const Rect g = landscapeFgPineBounds(w.land, k);
      unite(p, Rect{g.x0, g.y0, g.x1, g.y1 + kFgIcicleMax});
    }
    r.repaintRows(p.y0, p.y1 < SS_HEIGHT ? p.y1 : SS_HEIGHT);
    r.markDirtyRect(p);
    f.pine_q = pq;
  }
}

int fireIceWarmStep(const World& w) { return w.fi.warm_q; }

void fireIcePaletteTweak(int step, Palette& p, bool night) {
  if (step <= 0 || night) return;   // at night the fires are the warm light; the night caps stay as they are
  // The sky, the clouds, the far ridges and the ground a little of the way toward the golden hour's
  // own colours (palette.h moodKey), the sky most.
  const Palette& gold = moodKey(MOOD_GOLDEN);
  const float k = 0.1f * (float)step;
  static const Slot kSky[] = {SKY0, SKY1, SKY2, SKY3, SKY4, CLOUD_LIT, CLOUD_SHADE};
  static const Slot kLand[] = {RIDGE_FAR, RIDGE_MID, GROUND, GROUND_DARK, GRASS};
  for (Slot s : kSky) p.c[s] = mixRGB(p.c[s], gold.c[s], k);
  for (Slot s : kLand) p.c[s] = mixRGB(p.c[s], gold.c[s], 0.5f * k);
}

void fireIceDrawStatic(const World& w, Surface& frame, const Palette& pal) {
  const FireIce& f = w.fi;
  if ((f.ice_q == kIceNoneQ && f.pine_q == 0) || !buildDone(w.build)) return;
  const Pixel ice = mixRGB(pal[WATER_LIGHT], pal[CLOUD_LIT], 0.55f);
  const Pixel spark = mixRGB(ice, pal[CLOUD_LIT], 0.5f);
  const int y0 = frame.origin_y, y1 = frame.origin_y + frame.height;

  if (f.ice_q != kIceNoneQ) {
    const float U = (float)f.ice_q * kIceStep;
    const Circle c = circleFor(w, U);
    // Frost on the ground's own colours, lighter along the circle's edge; the stream iced at its edges.
    const Pixel src[5] = {pal[GROUND], pal[GROUND_DARK], pal[GRASS], pal[ROCK], pal[WATER]};
    const float amt[5] = {0.70f, 0.60f, 0.60f, 0.50f, 0.62f};
    Pixel full[5], light[5];
    for (int i = 0; i < 5; ++i) {
      full[i] = mixRGB(src[i], ice, amt[i]);
      light[i] = mixRGB(src[i], ice, 0.38f * amt[i]);
    }
    // How far in from each bank the stream freezes: the same at every size, so a step changes nothing
    // outside the rows it repaints.
    const float edge_ice = 0.45f;
    const int ya = y0 > LS_GROUND_Y ? y0 : LS_GROUND_Y;
    for (int y = ya; y < y1; ++y) {
      Pixel* row = frame.row(y);
      const float dy = (float)y - c.fy;
      const float dyn = dy * (dy < 0.f ? c.iy_u : c.iy_d);
      const float dy2 = dyn * dyn;
      for (int x = 0; x < SS_WIDTH; ++x) {
        const Pixel p = row[x];
        int k = 0;
        while (k < 5 && p != src[k]) ++k;
        if (k == 5) continue;
        const float dx = (float)x - c.fx;
        const float dxn = dx * (dx < 0.f ? c.ix_l : c.ix_r);
        // A ragged edge: the circle's size wavers by a few percent in 4 x 3 px cells.
        const float j = 1.f + 0.07f * ((float)(hash2(x >> 2, y / 3) & 255u) * (1.f / 255.f) - 0.5f);
        const float d2 = (dxn * dxn + dy2) * j * j;
        if (d2 < 1.f) continue;                      // inside the warm circle
        if (k == 4) {                                // the stream: only near its banks
          float cy, half;
          if (!landscapeStreamSpan(w.land, (float)x, cy, half) || absf((float)y - cy) < edge_ice * 1.06f * half) continue;
        }
        if (d2 < 1.17f) {
          row[x] = light[k];
        } else {
          row[x] = (hash2(x, y) & 63u) == 0 && k != 4 ? spark : full[k];
        }
      }
    }
  }

  if (f.pine_q > 0) fireIceDrawIcicles(w, frame, Rect{0, y0, SS_WIDTH, y1}, pal, 7);
}

void fireIceDrawIcicles(const World& w, Surface& s, const Rect& clip, const Palette& pal, unsigned which) {
  const FireIce& f = w.fi;
  if (f.pine_q == 0 || !buildDone(w.build)) return;
  const Pixel ice = mixRGB(pal[WATER_LIGHT], pal[CLOUD_LIT], 0.55f);
  if (which & 1u) {   // under the camp pine's tiers
    const LsPine& p = w.land.camp;
    const float cx = (float)p.x, h = (float)p.h, base = (float)p.base_y;
    const float top = base - h, fbot = (float)LS_GROUND_Y;
    const float F = fbot - top, th = 0.42f * F, sp = p.tiers > 1 ? (F - th) / (float)(p.tiers - 1) : 0.f;
    static const float kAt[5] = {-0.9f, -0.55f, -0.2f, 0.35f, 0.75f};
    for (int i = 0; i < p.tiers; ++i) {
      const float by = top + (float)i * sp + th;
      const float hw = 0.5f * ((float)p.width_pct * 0.01f) * h * (1.f - 0.2f * (float)(p.tiers - 1 - i));
      for (int k = 0; k < 5; ++k) {
        const float x = cx + kAt[k] * hw;
        const float len = (f.pine_q == 1 ? 3.5f : 7.5f) * (0.6f + 0.4f * (float)(hash2(i, k) & 255u) * (1.f / 255.f));
        const Vec2 tri[3] = {{x - 1.3f, by - 1.f}, {x + 1.3f, by - 1.f}, {x, by + len}};
        fillPolygon(s, tri, 3, ice, clip);
      }
    }
  }
  // The foreground pines (left edge): the same icicles, bigger, as they are nearer; the nearer
  // (taller) pine's the biggest.
  static const float kAtFg[6] = {-0.92f, -0.62f, -0.3f, 0.08f, 0.42f, 0.78f};
  for (int t = 0; t < 2; ++t) {
    if (!(which & (2u << t))) continue;
    const LsPine& p = w.land.fg[t];
    const float scale = t == 1 ? kFgIcicleNear : kFgIcicleFar;
    for (int i = 0; i < p.tiers; ++i) {
      float by, hw;
      landscapeFgPineTier(w.land, t, i, by, hw);
      for (int k = 0; k < 6; ++k) {
        const float x = (float)p.x + kAtFg[k] * hw;
        const float len = scale * (f.pine_q == 1 ? 3.5f : 7.5f) * (0.6f + 0.4f * (float)(hash2(i + 7 * t, k + 3) & 255u) * (1.f / 255.f));
        const float hwid = 1.3f * (0.5f + 0.5f * scale);
        const Vec2 tri[3] = {{x - hwid, by - 1.f}, {x + hwid, by - 1.f}, {x, by + len}};
        fillPolygon(s, tri, 3, ice, clip);
      }
    }
  }
}

void fireIceDrawActor(const World& w, int idx, Surface& s, const Rect& clip, const Palette& pal) {
  const FireIce& f = w.fi;
  if (idx < FI_FIRES) {
    const FiFlame& q = f.f[idx];
    smallFireDraw(Vec2{(float)q.x, (float)q.y}, q.k, (float)w.tick * kDt + 1.37f * (float)idx, s, clip, pal);
  } else if (idx == FI_FIRES && f.bucket == 2) {
    const Vec2 o{f.bucket_at.x, f.bucket_at.y - 18.5f};
    if (overlaps(bucketBox(o), clip)) drawBucket(o, 0.f, 0.f, s, clip, pal);
  }
}

void fireIceDrawHeld(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const FireIce& f = w.fi;
  if (!f.on || f.bucket != 1) return;
  const Vec2 o = hikerHandPos(w.hiker, 0);
  if (!overlaps(bucketBox(o), clip)) return;
  drawBucket(o, (float)w.hiker.facing * f.tilt, f.water, s, clip, pal);
}

void fireIceDrawTentIce(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const FireIce& f = w.fi;
  const Tent& t = w.tent;
  if (f.tent_q == 0 || t.state != TENT_UP || t.progress < 1.f) return;
  const Pixel ice = mixRGB(pal[WATER_LIGHT], pal[CLOUD_LIT], 0.55f);
  // Hanging from the upper half of the fly's back and front edges.
  const Vec2 ends[2][2] = {{tentRidgeEnd(t, 0), Vec2{t.pos.x - 38.f, t.pos.y}},
                           {tentRidgeEnd(t, 1), Vec2{t.pos.x + 38.f, t.pos.y}}};
  for (int e = 0; e < 2; ++e)
    for (int k = 0; k < 4; ++k) {
      const float u = 0.12f + 0.12f * (float)k;
      const Vec2 p{lerp(ends[e][0].x, ends[e][1].x, u) + (e ? 0.6f : -0.6f), lerp(ends[e][0].y, ends[e][1].y, u)};
      const float len = (f.tent_q == 1 ? 3.f : 6.5f) * (0.65f + 0.35f * (float)((k * 7 + e * 3) % 5) * 0.25f);
      const Vec2 tri[3] = {{p.x - 1.1f, p.y}, {p.x + 1.1f, p.y}, {p.x, p.y + len}};
      fillPolygon(s, tri, 3, ice, clip);
    }
}

void fireIceDrawNear(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const FireIce& f = w.fi;
  if (!f.on) return;
  steamDraw(f.steam, s, clip, pal);
  if (f.splash_t >= 0.f) {   // the water's arc from the bucket to the fire
    for (int i = 0; i < 6; ++i) {
      const float u = (f.splash_t - 0.045f * (float)i) / 0.3f;
      if (u <= 0.f || u >= 1.f) continue;
      const Vec2 p{lerp(f.splash_a.x, f.splash_b.x, u), lerp(f.splash_a.y, f.splash_b.y, u) - 16.f * 4.f * u * (1.f - u)};
      fillCircle(s, p, 1.8f - 0.12f * (float)i, pal[WATER_LIGHT], clip);
    }
  }
  if (f.breath_t >= 0.f && w.hiker.visible) {   // a breath puff in the hard cold
    float hr = 0.f;
    const Vec2 hp = hikerHeadPos(w.hiker, &hr);
    const float u = f.breath_t / 1.1f;
    const float r = (1.5f + 3.f * minf(1.f, u * 3.f)) * (1.f - u * u);
    if (r > 0.6f)
      fillEllipse(s, Vec2{hp.x + (float)w.hiker.facing * (hr + 3.f + 7.f * u), hp.y + 1.f - 4.f * u}, r, r * 0.8f,
                  mixRGB(pal[SMOKE], pal[CLOUD_LIT], 0.4f), clip);
  }
}

}  // namespace ss
