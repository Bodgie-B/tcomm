// The visiting couple. See visitors.h.
//
// A small stage machine, as fire and ice's: they walk in, greet, come to their places, play the
// kind's own part (the fireside's talk, story, laugh and toast; the frisbee's throws; the
// telescopes), part, walk off to the left, and he goes home. People are numbered 0 him, 1 Ann,
// 2 Ben; he is driven through World::fx as the scripts drive him, they through their own offsets.
// Every choice comes from the visit's own stream; all motion is tables and eases.
#include "visitors.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../render/renderer.h"
#include "../ss_app.h"
#include "director.h"
#include "fireice.h"
#include "housedogs.h"
#include "script.h"
#include "world.h"

namespace ss {

namespace {

constexpr float kDt = SS_TICK_DT;
constexpr float kChance = 0.17f;      // a loop they may come in: with the wait, about one camp loop in seven (PROPOSED)
constexpr float kStroll = 34.f, kRun = 92.f;
constexpr float kAnnH = 82.f, kBenH = 95.f;
constexpr float kLeftLoop = 60.f;     // s of the loop that must be left for them to start
const Rect kEmpty{0, 0, 0, 0};

enum Stage : uint8_t { ST_IN = 0, ST_GREET, ST_COME, ST_SETTLE, ST_MAIN, ST_PART, ST_GO, ST_HOME, ST_COUNT };
enum DiscMode : uint8_t { DS_NONE = 0, DS_HELD, DS_FLY, DS_GROUND, DS_DOG };

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }
inline float env(float t, float a, float d, float up, float down) {   // 0 -> 1 over [a, a+up], back over [d-down, d]
  return easeInOut(window(t, a, a + up)) * (1.f - easeInOut(window(t, d - down, d)));
}
inline bool at(float st, float s) { return st >= s && st - kDt < s; }   // the tick it crosses s seconds

uint32_t nextR(Visitors& V) {
  uint32_t x = V.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  V.rng = x ? x : 0x9E3779B9u;
  return V.rng;
}
float rnd(Visitors& V, float a, float b) { return a + (b - a) * (float)(nextR(V) >> 8) * (1.f / 16777216.f); }

// An atan2 good to a few thousandths of a radian, from plain float arithmetic (PC and panel agree).
float atan2A(float y, float x) {
  const float ax = absf(x), ay = absf(y);
  if (ax < 1e-6f && ay < 1e-6f) return 0.f;
  const float a = ax < ay ? ax / ay : ay / ax;
  const float s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0.f) r = 3.14159274f - r;
  return y < 0.f ? -r : r;
}

bool camp(const World& w) { return !spineSetPiece(w.plan.family); }
float groundY(const World& w, float x, float y) { return maxf((float)LS_GROUND_Y + 12.f, scriptGroundAt(w, x, y)); }

// ---- people ------------------------------------------------------------------------------------

Hiker& rig(World& w, int i) { return i == 0 ? w.hiker : w.vis.v[i - 1].h; }
const Hiker& rigC(const World& w, int i) { return i == 0 ? w.hiker : w.vis.v[i - 1].h; }

void setLean(World& w, int i, float l, float hd) {
  if (i == 0) {
    w.fx.lean = l;
    w.fx.head = hd;
  } else {
    w.vis.v[i - 1].lean = l;
    w.vis.v[i - 1].head = hd;
  }
}
void setLift(World& w, int i, float l) {
  if (i == 0) w.fx.lift = l;
  else w.vis.v[i - 1].lift = l;
}
void arm(World& w, int i, int a, float k, float sh, float el) {
  if (i == 0) {
    w.fx.arm_w[a] = k;
    w.fx.arm_sh[a] = sh;
    w.fx.arm_el[a] = el;
  } else {
    Visitor& v = w.vis.v[i - 1];
    v.arm_w[a] = k;
    v.arm_sh[a] = sh;
    v.arm_el[a] = el;
  }
}
void rest(World& w, int i) {
  setLean(w, i, 0.f, 0.f);
  setLift(w, i, 0.f);
  arm(w, i, 0, 0.f, 0.f, 0.f);
  arm(w, i, 1, 0.f, 0.f, 0.f);
}
void restAll(World& w) {
  for (int i = 0; i < 3; ++i) rest(w, i);
}
void pose(World& w, int i, PoseId p, float s) {
  Hiker& h = rig(w, i);
  if (i == 0) {   // the fx offsets out of the blend's start first (as the scripts do)
    h.pose.lean -= w.fx.lean;
    h.pose.head -= w.fx.head;
    h.pose.lift -= w.fx.lift;
  }
  hikerSetPose(h, p, s);
}
void faceX(World& w, int i, float x) {
  Hiker& h = rig(w, i);
  if (absf(x - h.feet.x) > 0.5f) h.facing = x < h.feet.x ? -1 : 1;
}

// Walks move x; feet.y follows the straight line to the target.
void walk(World& w, int i, Vec2 to, float v) {
  Hiker& h = rig(w, i);
  if (i == 0) {
    HikerFx& fx = w.fx;
    fx.path_on = true;
    fx.path_x0 = h.feet.x;
    fx.path_y0 = h.feet.y;
    fx.path_x1 = to.x;
    fx.path_y1 = to.y;
    w.vis.hero_leg2 = false;
  } else {
    Visitor& p = w.vis.v[i - 1];
    p.path_on = true;
    p.path_x0 = h.feet.x;
    p.path_y0 = h.feet.y;
    p.path_x1 = to.x;
    p.path_y1 = to.y;
    p.leg2 = false;
  }
  if (to.x == h.feet.x) h.feet.y = to.y;
  hikerWalkTo(h, to.x, v);
}
// Round the back of the fire pit when the straight line would cross it.
void go(World& w, int i, Vec2 to, float v) {
  const Hiker& h = rigC(w, i);
  const Vec2 F = w.fire.pos;
  const float x0 = minf(h.feet.x, to.x), x1 = maxf(h.feet.x, to.x);
  const float yl = lerp(h.feet.y, to.y, clamp01((F.x - h.feet.x) / (to.x - h.feet.x + (to.x == h.feet.x ? 1.f : 0.f))));
  if (F.x > x0 + 6.f && F.x < x1 - 6.f && absf(yl - F.y) < 26.f) {
    const Vec2 via{F.x + (to.x > h.feet.x ? -4.f : 4.f), F.y - 30.f};
    walk(w, i, via, v);
    if (i == 0) {
      w.vis.hero_leg2 = true;
      w.vis.hero_leg2_to = to;
      w.vis.hero_leg2_v = v;
    } else {
      Visitor& p = w.vis.v[i - 1];
      p.leg2 = true;
      p.leg2_to = to;
      p.leg2_v = v;
    }
    return;
  }
  walk(w, i, to, v);
}
bool walking(const World& w, int i) {
  if (rigC(w, i).walking) return true;
  return i == 0 ? w.vis.hero_leg2 : w.vis.v[i - 1].leg2;
}
bool allStill(const World& w) { return !walking(w, 0) && !walking(w, 1) && !walking(w, 2); }

Vec2 handOf(const World& w, int i) { return hikerHandPos(rigC(w, i), 0); }

// ---- places ------------------------------------------------------------------------------------

float trailY(const World& w, float x) { return groundY(w, x, (float)LS_GROUND_Y + 20.f); }

void placesFireside(World& w) {
  Visitors& V = w.vis;
  const Vec2 F = w.fire.pos;
  V.spot[0] = w.spine.camp.sit;
  V.spot[1] = Vec2{F.x - 42.f, groundY(w, F.x - 42.f, F.y + 4.f)};
  V.spot[2] = Vec2{F.x + 84.f, groundY(w, F.x + 84.f, F.y - 3.f)};
}
void placesFrisbee(World& w) {
  Visitors& V = w.vis;
  const float hx = minf(380.f, maxf(250.f, w.hiker.feet.x));
  V.spot[0] = Vec2{hx, groundY(w, hx, 372.f)};
  V.spot[1] = Vec2{hx + 250.f, groundY(w, hx + 250.f, 352.f)};
  V.spot[2] = Vec2{hx + 125.f, groundY(w, hx + 125.f, 336.f)};
}
void placesScopes(World& w) {
  Visitors& V = w.vis;
  V.scope[0] = Vec2{440.f, groundY(w, 440.f, 340.f)};
  V.scope[1] = Vec2{560.f, groundY(w, 560.f, 338.f)};
  // What they look at: the moon if it is up, else the brightest star.
  if (V.moon) {
    V.aim = Vec2{(float)V.moon_x, (float)V.moon_y};
  } else {
    const LsStar* best = &w.land.stars[0];
    for (const LsStar& s : w.land.stars)
      if (s.r > best->r) best = &s;
    V.aim = Vec2{(float)best->x, (float)best->y};
  }
  // Each at its scope's eyepiece end (the far side from where it points): Ann the second, Ben the first.
  for (int k = 0; k < 2; ++k) {
    const float side = V.aim.x >= V.scope[k].x ? -1.f : 1.f;
    V.spot[2 - k] = Vec2{V.scope[k].x + side * 19.f, V.scope[k].y + 1.f};
  }
  V.spot[0] = w.hiker.feet;
}

// The tripod's top and the tube's direction (toward the aim, at least a little up).
Vec2 scopeTop(const Visitors& V, int k) { return Vec2{V.scope[k].x, V.scope[k].y - 46.f * easeInOut(clamp01(V.scope_k[k]))}; }
Vec2 scopeDir(const Visitors& V, int k) {
  const Vec2 t = scopeTop(V, k);
  float dx = V.aim.x - t.x, dy = minf(V.aim.y - t.y, -40.f);
  const float n = sqrtf(dx * dx + dy * dy);
  return Vec2{dx / n, dy / n};
}
// Where a viewer's feet go to put an eye to scope k.
Vec2 eyepieceSpot(const Visitors& V, int k) {
  const float side = V.aim.x >= V.scope[k].x ? -1.f : 1.f;
  return Vec2{V.scope[k].x + side * 19.f, V.scope[k].y + 1.f};
}

// ---- the parts each kind plays -----------------------------------------------------------------

// Pointing up along `to` with the near arm (and the head up).
void pointAt(World& w, int i, Vec2 to, float k) {
  const Hiker& h = rigC(w, i);
  float r = 0.f;
  const Vec2 hd = hikerHeadPos(h, &r);
  const Vec2 sh{hd.x, hd.y + 3.f * r};
  const float a = atan2A((to.x - sh.x) * (float)h.facing, to.y - sh.y);
  arm(w, i, 0, k, a, 0.08f);
  setLean(w, i, -0.04f * k, -0.38f * k);
}

// Talking with small gestures (the near arm, a mug in it), or listening with a nod now and then.
void talk(World& w, int i, float ft, bool speaking, float ph) {
  if (speaking) arm(w, i, 1, 1.f, 0.75f + 0.28f * sinT(ft * 0.55f + ph), 1.1f + 0.35f * sinT(ft * 0.8f + ph));
  else arm(w, i, 1, 0.f, 0.f, 0.f);
  setLean(w, i, speaking ? 0.06f : 0.f, (speaking ? 0.f : 0.07f) * sinT(ft * 0.3f + ph));
}
void sip(World& w, int i, float k) {
  arm(w, i, 0, k, lerp(0.55f, 0.55f, k), lerp(1.4f, 2.35f, k));
  setLean(w, i, 0.f, -0.12f * k);
}
void holdCup(World& w, int i) { arm(w, i, 0, 1.f, 0.55f, 1.4f); }

void fireside(World& w) {
  Visitors& V = w.vis;
  const float t = V.st;
  for (int i = 0; i < 3; ++i) {
    rest(w, i);
    holdCup(w, i);
  }
  if (t < 18.f) {   // talk: the turn goes round every few seconds; sips now and then
    const int who = ((int)(t / 4.2f)) % 3;
    for (int i = 0; i < 3; ++i) talk(w, i, t, i == who, 0.9f * (float)i);
    const float s = window(t, 9.f, 11.6f);
    if (s > 0.f && s < 1.f) sip(w, 1, scriptEnvelope(s, 0.3f, 0.3f));
  } else if (t < 32.f) {   // Ben's story, told with his hands; the others watch him
    const float u = t - 18.f;
    arm(w, 2, 0, 1.f, 1.05f + 0.55f * sinT(u * 0.45f), 0.5f + 0.4f * sinT(u * 0.7f + 0.2f));
    arm(w, 2, 1, 1.f, 0.95f + 0.6f * sinT(u * 0.45f + 0.4f), 0.4f + 0.3f * sinT(u * 0.6f));
    setLean(w, 2, 0.14f + 0.05f * sinT(u * 0.25f), -0.05f);
    setLean(w, 0, 0.04f, 0.04f);
    setLean(w, 1, 0.04f, 0.04f);
  } else if (t < 39.f) {   // all three lean back laughing, shoulders shaking
    const float e = env(t, 32.f, 39.f, 0.5f, 1.2f);
    for (int i = 0; i < 3; ++i) {
      setLean(w, i, -0.24f * e, -0.34f * e);
      setLift(w, i, 0.012f * e * sinT(t * 3.4f + 0.3f * (float)i));
      arm(w, i, 1, e, 0.35f, 2.2f);
    }
    arm(w, 2, 0, e, 0.6f + 0.3f * sinT(t * 1.7f), 1.0f);   // Ben's hand on his knee
  } else if (t < 46.f) {
    const int who = ((int)((t - 39.f) / 3.5f)) % 2;
    talk(w, 0, t, who == 0, 0.f);
    talk(w, 1, t, who == 1, 1.f);
    const float s = window(t, 41.f, 43.6f);
    if (s > 0.f && s < 1.f) sip(w, 2, scriptEnvelope(s, 0.3f, 0.3f));
  } else if (t < 52.f) {   // a toast: cups up over the fire
    const float e = env(t, 46.f, 52.f, 0.8f, 0.8f);
    for (int i = 0; i < 3; ++i) arm(w, i, 0, 1.f, lerp(0.55f, 1.95f, e), lerp(1.4f, 0.55f, e));
    if (t > 50.2f) for (int i = 0; i < 3; ++i) sip(w, i, env(t, 50.2f, 52.f, 0.5f, 0.6f));
  } else {   // a while longer, looking into the fire
    const float s = window(t, 56.f, 58.6f);
    if (s > 0.f && s < 1.f) sip(w, 0, scriptEnvelope(s, 0.3f, 0.3f));
  }
}

// ---- the frisbee -------------------------------------------------------------------------------

// The throws: from, to, what happens (0 a catch, 1 a fumble, 2 a long one run down, 3 a dive, 4 the dog).
struct Throw {
  uint8_t from, to, what;
};
const Throw kThrows[] = {{0, 1, 0}, {1, 2, 0}, {2, 0, 1}, {0, 2, 0}, {2, 1, 2}, {1, 0, 0}, {0, 2, 3}, {2, 1, 4}, {1, 0, 0}};
constexpr int kThrowN = (int)(sizeof kThrows / sizeof kThrows[0]);
enum FrisStep : uint8_t { FS_AIM = 0, FS_FLY, FS_CATCH, FS_DROP, FS_PICK, FS_BACK, FS_DIVE, FS_UP, FS_DOG_RUN, FS_DOG_OFF, FS_DOG_DROP, FS_DONE };

Vec2 discHand(const World& w, int i) {
  const Vec2 h = handOf(w, i);
  return Vec2{h.x + (float)rigC(w, i).facing * 2.f, h.y - 1.f};
}

void startThrow(World& w) {
  Visitors& V = w.vis;
  const Throw& th = kThrows[V.n];
  const int a = th.from, b = th.to;
  faceX(w, a, rigC(w, b).feet.x);
  faceX(w, b, rigC(w, a).feet.x);
  V.catcher = (uint8_t)b;
  V.step = FS_AIM;
  V.ft = 0.f;
}

void launch(World& w) {
  Visitors& V = w.vis;
  const Throw& th = kThrows[V.n];
  const int b = th.to;
  const Hiker& hb = rigC(w, b);
  V.disc_a = V.disc;
  Vec2 to{hb.feet.x, hb.feet.y - 0.62f * hb.height};
  if (th.what == 2) to.x += (hb.feet.x > rigC(w, th.from).feet.x ? 1.f : -1.f) * rnd(V, 62.f, 78.f);   // over the catcher's head
  if (th.what == 3) to = Vec2{hb.feet.x + (float)hb.facing * 34.f, hb.feet.y - 0.25f * hb.height};       // low and wide: a dive
  if (th.what == 1) to.y = hb.feet.y - 0.5f * hb.height;
  to.x = minf(640.f, maxf(60.f, to.x));
  V.disc_b = to;
  const float d = sqrtf((to.x - V.disc.x) * (to.x - V.disc.x) + (to.y - V.disc.y) * (to.y - V.disc.y));
  V.disc_dur = 0.8f + d / 340.f + (th.what == 2 ? 0.5f : 0.f);
  V.disc_arc = 22.f + 0.1f * d + (th.what == 2 ? 26.f : 0.f) + rnd(V, 0.f, 10.f);
  V.disc_t = 0.f;
  V.disc_mode = DS_FLY;
  V.holder = 9;
  if (th.what == 2) walk(w, b, Vec2{to.x, groundY(w, to.x, hb.feet.y)}, kRun);   // off after it
}

Vec2 flightAt(const Visitors& V, float u) {
  return Vec2{lerp(V.disc_a.x, V.disc_b.x, u), lerp(V.disc_a.y, V.disc_b.y, u) - V.disc_arc * 4.f * u * (1.f - u)};
}

void frisbee(World& w) {
  Visitors& V = w.vis;
  V.ft += kDt;
  const Throw& th = kThrows[V.n < kThrowN ? V.n : kThrowN - 1];
  const int a = th.from, b = th.to;
  if (V.disc_mode == DS_HELD) V.disc = discHand(w, V.holder);
  switch (V.step) {
    case FS_AIM: {   // the backhand: arm across, then out
      const float u = clamp01(V.ft / 0.75f);
      const float back = easeInOut(window(u, 0.f, 0.7f));
      const float out = easeOutCubic(window(u, 0.7f, 1.f));
      arm(w, a, 0, 1.f, lerp(lerp(0.3f, -0.55f, back), 1.55f, out), lerp(lerp(0.6f, 1.9f, back), 0.15f, out));
      setLean(w, a, 0.06f * back - 0.04f * out, 0.f);
      arm(w, b, 0, easeInOut(u), 1.5f, 0.4f);   // the catcher ready
      if (u >= 1.f) launch(w);
      if (u >= 1.f) V.step = FS_FLY;
      if (u >= 1.f) V.ft = 0.f;
      break;
    }
    case FS_FLY: {
      V.disc_t += kDt;
      const float u = clamp01(V.disc_t / V.disc_dur);
      V.disc = flightAt(V, u);
      arm(w, a, 0, 1.f - easeInOut(clamp01(V.ft / 0.6f)), 1.55f, 0.15f);
      // The catcher's hands up toward it as it comes; the dive goes in late.
      const Hiker& hb = rigC(w, b);
      const float up = clamp01((hb.feet.y - 0.6f * hb.height - V.disc.y) / 40.f);
      arm(w, b, 0, 1.f, 1.4f + 0.9f * up, 0.35f);
      if (th.what == 3 && u > 0.55f && hb.target != POSE_LIE) pose(w, b, POSE_LIE, 0.35f);   // the dive
      if (th.what == 3 && u > 0.55f && u < 0.9f) rig(w, b).feet.x += (float)hb.facing * 70.f * kDt;   // flat out, sideways
      if (th.what == 4 && V.dog >= 0 && u > 0.2f && V.disc_mode == DS_FLY && V.step == FS_FLY) {   // the dog goes for it
        const Vec2 p = flightAt(V, 0.75f);
        directorWalk(w, V.dog, Vec2{p.x, groundY(w, p.x, minf(rigC(w, b).feet.y + 4.f, 372.f))}, 150.f, CA_RUN);
        V.step = FS_DOG_RUN;
        break;
      }
      if (u >= 1.f) {
        if (th.what == 1) {   // off the fingers: it drops and skids
          V.disc_a = V.disc;
          V.disc_b = Vec2{V.disc.x + (float)hb.facing * 12.f, hb.feet.y + 1.f};
          V.disc_t = 0.f;
          V.disc_dur = 0.55f;
          V.disc_arc = 8.f;
          V.step = FS_DROP;
        } else {
          V.disc_mode = DS_HELD;
          V.holder = (uint8_t)b;
          V.step = FS_CATCH;
        }
        V.ft = 0.f;
      }
      break;
    }
    case FS_DROP: {   // the fumble: a hop off the hands, down onto the grass
      V.disc_t += kDt;
      const float u = clamp01(V.disc_t / V.disc_dur);
      V.disc = flightAt(V, u);
      arm(w, b, 0, 1.f, 1.4f + 0.5f * sinT(V.ft * 2.f), 0.6f);
      if (u >= 1.f) {
        V.disc_mode = DS_GROUND;
        pose(w, b, POSE_CROUCH, 0.35f);
        walk(w, b, Vec2{V.disc.x - (float)rigC(w, b).facing * 8.f, rigC(w, b).feet.y}, kStroll);
        V.step = FS_PICK;
        V.ft = 0.f;
      }
      break;
    }
    case FS_PICK:   // walks to it (or just bends), picks it up
      if (walking(w, b)) break;
      if (rigC(w, b).target != POSE_CROUCH) pose(w, b, POSE_CROUCH, 0.35f);
      if (V.ft > 1.2f) {
        V.disc_mode = DS_HELD;
        V.holder = (uint8_t)b;
        pose(w, b, POSE_STAND, 0.4f);
        V.step = FS_BACK;
        V.ft = 0.f;
      }
      break;
    case FS_CATCH:   // caught: a moment, then back to the spot if he left it
      arm(w, b, 0, 1.f - clamp01(V.ft / 0.5f), 1.6f, 0.3f);
      if (V.ft > 0.6f) {
        V.step = rigC(w, b).target == POSE_LIE ? FS_UP : FS_BACK;
        V.ft = 0.f;
      }
      break;
    case FS_UP:   // up from the dive
      if (V.ft < kDt * 1.5f) pose(w, b, POSE_STAND, 0.8f);
      if (V.ft > 1.2f) {
        V.step = FS_BACK;
        V.ft = 0.f;
      }
      break;
    case FS_BACK: {   // back to the spot, then the next throw
      const Vec2 sp = V.spot[b];
      if (V.ft < kDt * 1.5f && absf(rigC(w, b).feet.x - sp.x) > 3.f) walk(w, b, sp, kStroll * 1.3f);
      if (walking(w, b) || V.ft < 0.7f) break;
      ++V.n;
      if (V.n >= kThrowN) {
        V.step = FS_DONE;
        break;
      }
      startThrow(w);
      break;
    }
    case FS_DOG_RUN: {   // the disc flies on; the dog leaps and takes it out of the air
      V.disc_t += kDt;
      const float u = clamp01(V.disc_t / V.disc_dur);
      V.disc = flightAt(V, u);
      Critter& c = w.critters[V.dog];
      if (u >= 0.75f || critterArrived(c)) {
        V.disc_mode = DS_DOG;
        V.holder = 3;
        const float x = minf(620.f, maxf(80.f, c.feet.x + (c.feet.x > 400.f ? -90.f : 90.f)));
        directorWalk(w, V.dog, Vec2{x, groundY(w, x, minf(rigC(w, b).feet.y + 6.f, 372.f))}, 115.f, CA_RUN);
        walk(w, b, Vec2{x - 20.f, groundY(w, x - 20.f, rigC(w, b).feet.y)}, kRun);   // after it
        V.step = FS_DOG_OFF;
        V.ft = 0.f;
      }
      break;
    }
    case FS_DOG_OFF:   // it runs off with it; the catcher gives chase
      V.disc = critterMouth(w.critters[V.dog]);
      if (V.ft > 1.f && critterArrived(w.critters[V.dog]) && !walking(w, b)) {
        critterSetAction(w.critters[V.dog], CA_SIT, 0.5f);
        pose(w, b, POSE_CROUCH, 0.4f);
        V.step = FS_DOG_DROP;
        V.ft = 0.f;
      }
      break;
    case FS_DOG_DROP:   // it lets go; picked up; a pat; back to the game
      if (V.ft < 0.6f) V.disc = critterMouth(w.critters[V.dog]);
      if (at(V.ft, 0.6f)) {
        V.disc_mode = DS_GROUND;
        V.disc.y = w.critters[V.dog].feet.y;
      }
      if (at(V.ft, 1.4f)) {
        V.disc_mode = DS_HELD;
        V.holder = (uint8_t)b;
      }
      if (V.ft > 2.2f) {
        pose(w, b, POSE_STAND, 0.4f);
        const Vec2 home = V.spot[0];
        directorWalk(w, V.dog, Vec2{home.x - 30.f, groundY(w, home.x - 30.f, home.y + 6.f)}, 60.f, CA_MOVE);
        V.step = FS_BACK;
        V.ft = 0.f;
      }
      break;
    default: break;
  }
}

// ---- the telescopes ----------------------------------------------------------------------------

enum ScopeStep : uint8_t { VS_LOOK = 0, VS_HERO_TO, VS_HERO_LOOK, VS_HERO_BACK, VS_METEOR, VS_TURNS, VS_DONE };

void lookInto(World& w, int i, float k) { setLean(w, i, 0.34f * k, 0.22f * k); }

void scopes(World& w) {
  Visitors& V = w.vis;
  V.ft += kDt;
  switch (V.step) {
    case VS_LOOK:   // Ben at the first eyepiece, Ann by the second pointing up at the moon (or the star)
      faceX(w, 2, V.scope[0].x);
      lookInto(w, 2, easeInOut(clamp01(V.ft / 0.8f)));
      faceX(w, 1, V.aim.x);
      pointAt(w, 1, V.aim, env(V.ft, 0.6f, 6.5f, 0.6f, 0.6f));
      setLean(w, 0, 0.f, -0.3f * env(V.ft, 1.f, 6.5f, 0.8f, 0.8f));   // he looks up too
      if (V.ft > 6.5f) {
        lookInto(w, 2, 0.f);
        walk(w, 2, Vec2{V.spot[2].x + 26.f * (V.aim.x >= V.scope[0].x ? -1.f : 1.f), V.spot[2].y + 6.f}, kStroll);   // Ben steps aside
        walk(w, 0, eyepieceSpot(V, 0), kStroll);
        V.step = VS_HERO_TO;
        V.ft = 0.f;
      }
      break;
    case VS_HERO_TO:
      if (walking(w, 0)) break;
      faceX(w, 0, V.scope[0].x);
      faceX(w, 2, V.scope[0].x);
      V.step = VS_HERO_LOOK;
      V.ft = 0.f;
      break;
    case VS_HERO_LOOK:   // his look; Ann at the other one
      lookInto(w, 0, env(V.ft, 0.f, 5.f, 0.7f, 0.6f));
      faceX(w, 1, V.scope[1].x);
      lookInto(w, 1, env(V.ft, 1.f, 5.f, 0.7f, 0.6f));
      if (V.ft > 5.f) {
        lookInto(w, 1, 0.f);
        const Hiker& h = w.hiker;
        walk(w, 0, Vec2{h.feet.x - (float)h.facing * 16.f, h.feet.y + 3.f}, 18.f);   // steps back
        V.step = VS_HERO_BACK;
        V.ft = 0.f;
      }
      break;
    case VS_HERO_BACK:   // impressed: hands on hips, head back, looking up
      if (walking(w, 0)) break;
      if (w.hiker.target != POSE_HANDS_ON_HIPS) {
        faceX(w, 0, V.aim.x);
        pose(w, 0, POSE_HANDS_ON_HIPS, 0.6f);
      }
      setLean(w, 0, -0.06f, -0.34f * easeInOut(clamp01(V.ft / 0.8f)));
      arm(w, 2, 1, env(V.ft, 0.5f, 3.4f, 0.5f, 0.5f), 2.5f, 0.3f);   // Ben gestures up
      if (V.ft > 3.5f) {
        setLean(w, 0, 0.f, 0.f);
        arm(w, 2, 1, 0.f, 0.f, 0.f);
        pose(w, 0, POSE_STAND, 0.5f);
        V.step = V.dark && !V.meteor_done ? VS_METEOR : VS_TURNS;
        if (V.step == VS_METEOR) {   // a shooting star across the top of the sky, once
          const float x = rnd(V, 110.f, 300.f);
          const float y = rnd(V, 34.f, 64.f);
          V.meteor_a = Vec2{x, y};
          V.meteor_b = Vec2{x + rnd(V, 230.f, 300.f), y + rnd(V, 50.f, 75.f)};
          V.meteor_t = 0.f;
        }
        V.ft = 0.f;
      }
      break;
    case VS_METEOR: {   // all three follow it with their heads
      V.meteor_t += kDt;
      const float u = clamp01(V.meteor_t / 1.2f);
      const Vec2 m{lerp(V.meteor_a.x, V.meteor_b.x, u), lerp(V.meteor_a.y, V.meteor_b.y, u)};
      for (int i = 0; i < 3; ++i) {
        faceX(w, i, m.x);
        setLean(w, i, -0.05f, -0.42f + 0.1f * u);
      }
      if (V.meteor_t > 1.2f) {
        V.meteor_done = true;
        V.meteor_t = -1.f;
      }
      if (V.ft > 3.f) {
        for (int i = 0; i < 3; ++i) setLean(w, i, 0.f, 0.f);
        arm(w, 1, 0, 0.f, 0.f, 0.f);
        V.step = VS_TURNS;
        V.ft = 0.f;
      }
      if (V.ft > 1.4f && V.ft < 3.f) pointAt(w, 1, V.meteor_b, env(V.ft, 1.4f, 3.f, 0.4f, 0.4f));   // Ann: "there!"
      break;
    }
    case VS_TURNS:   // another look each
      faceX(w, 1, V.scope[1].x);
      lookInto(w, 1, env(V.ft, 0.3f, 5.f, 0.6f, 0.6f));
      if (V.ft < kDt * 1.5f) walk(w, 2, eyepieceSpot(V, 0), kStroll);
      if (!walking(w, 2)) {
        faceX(w, 2, V.scope[0].x);
        lookInto(w, 2, env(V.ft, 1.f, 5.f, 0.6f, 0.6f));
      }
      if (V.ft < kDt * 1.5f) walk(w, 0, Vec2{V.scope[0].x - 60.f * (V.aim.x >= V.scope[0].x ? 1.f : -1.f), V.scope[0].y + 10.f}, kStroll);
      setLean(w, 0, 0.f, -0.25f * env(V.ft, 1.5f, 5.f, 0.6f, 0.6f));
      if (V.ft > 5.2f) {
        restAll(w);
        V.step = VS_DONE;
      }
      break;
    default: break;
  }
}

// ---- the stages --------------------------------------------------------------------------------

void stage(Visitors& V, uint8_t s) {
  V.stage = s;
  V.st = 0.f;
}

void finish(World& w) {
  Visitors& V = w.vis;
  HikerFx& fx = w.fx;
  scriptFxRest(w);
  fx.stomp = 0.f;
  fx.path_on = V.home_path;
  fx.path_x0 = V.home_path_v[0];
  fx.path_y0 = V.home_path_v[1];
  fx.path_x1 = V.home_path_v[2];
  fx.path_y1 = V.home_path_v[3];
  fx.reach_to = V.home_reach_to;
  w.hiker.feet = V.home;
  w.hiker.facing = V.home_face;
  hikerCarry(w.hiker, CARRY_NONE);
  if (V.home_walk) hikerWalkTo(w.hiker, V.home_walk_x, V.home_walk_v);
  V.on = false;
  V.done = true;
  V.disc_mode = DS_NONE;
  for (Visitor& v : V.v) {
    v.h.visible = false;
    v.rect = kEmpty;
    v.pack_down = false;
  }
  V.scope_k[0] = V.scope_k[1] = 0.f;
  houseDogsGiveBack(w);
  V.dog = -1;
  w.busy = false;
}

void stepVisitorRigs(World& w) {
  Visitors& V = w.vis;
  for (int k = 0; k < 2; ++k) {
    Visitor& v = V.v[k];
    Hiker& h = v.h;
    const bool was = h.walking;
    hikerTick(h, kDt);
    if (h.walking && v.path_on) {
      const float span = v.path_x1 - v.path_x0;
      const float u = span != 0.f ? clamp01((h.feet.x - v.path_x0) / span) : 1.f;
      h.feet.y = lerp(v.path_y0, v.path_y1, u);
    } else if (was && v.path_on && absf(h.feet.x - v.path_x1) < 0.5f) {
      h.feet.y = v.path_y1;
    }
    if (!h.walking && v.leg2) walk(w, k + 1, v.leg2_to, v.leg2_v);
    HikerPose& P = h.pose;
    P.lean += v.lean;
    P.head += v.head;
    P.lift += v.lift;
    for (int a = 0; a < 2; ++a) {
      if (v.arm_w[a] <= 0.f) continue;
      P.shoulder[a] = lerp(P.shoulder[a], v.arm_sh[a], v.arm_w[a]);
      P.elbow[a] = lerp(P.elbow[a], v.arm_el[a], v.arm_w[a]);
    }
    h.lit_from = w.fire.state == FIRE_LIT ? (w.fire.pos.x < h.feet.x ? -1 : 1) : 0;
    const Rect b = hikerBounds(h);
    h.visible = b.x1 > 0 && b.x0 < SS_WIDTH;
    v.rect = h.visible ? b.clipped(Rect{0, 0, SS_WIDTH, SS_HEIGHT}) : kEmpty;
  }
  if (!w.hiker.walking && V.hero_leg2) walk(w, 0, V.hero_leg2_to, V.hero_leg2_v);
}

void packDown(World& w, int k, float dx) {
  Visitor& v = w.vis.v[k];
  v.pack_down = true;
  v.h.pack_on = false;
  v.pack_at = Vec2{v.h.feet.x + dx, v.h.feet.y - 3.f};
}
void packUp(World& w, int k) {
  Visitor& v = w.vis.v[k];
  v.pack_down = false;
  v.h.pack_on = true;
}

Rect packRect(const Visitor& v) {
  if (!v.pack_down) return kEmpty;
  const float H = v.h.height * (v.h.look == LOOK_ANN ? 0.8f : 1.05f);
  return Rect{(int)floorf(v.pack_at.x - 0.13f * H) - 2, (int)floorf(v.pack_at.y - 0.36f * H) - 2,
              (int)ceilf(v.pack_at.x + 0.13f * H) + 2, (int)ceilf(v.pack_at.y) + 2};
}
Rect discRect(const Visitors& V) {
  if (V.disc_mode == DS_NONE) return kEmpty;
  return Rect{(int)floorf(V.disc.x) - 7, (int)floorf(V.disc.y) - 4, (int)ceilf(V.disc.x) + 8, (int)ceilf(V.disc.y) + 4};
}
Rect scopeRect(const Visitors& V, int k) {
  if (V.scope_k[k] <= 0.f) return kEmpty;
  const Vec2 s = V.scope[k];
  return Rect{(int)s.x - 40, (int)s.y - 84, (int)s.x + 41, (int)s.y + 3};
}
Rect meteorRect(const Visitors& V) {
  if (V.meteor_t < 0.f) return kEmpty;
  const float u = clamp01(V.meteor_t / 1.2f);
  const Vec2 h{lerp(V.meteor_a.x, V.meteor_b.x, u), lerp(V.meteor_a.y, V.meteor_b.y, u)};
  const Vec2 t{lerp(V.meteor_a.x, V.meteor_b.x, maxf(0.f, u - 0.16f)), lerp(V.meteor_a.y, V.meteor_b.y, maxf(0.f, u - 0.16f))};
  return Rect{(int)floorf(minf(h.x, t.x)) - 3, (int)floorf(minf(h.y, t.y)) - 3, (int)ceilf(maxf(h.x, t.x)) + 3, (int)ceilf(maxf(h.y, t.y)) + 3};
}

void track(Renderer& r, Rect& prev, const Rect& cur, bool changed) {
  if (!changed && prev.x0 == cur.x0 && prev.y0 == cur.y0 && prev.x1 == cur.x1 && prev.y1 == cur.y1) return;
  if (!prev.empty()) r.markDirtyRect(prev);
  if (!cur.empty()) r.markDirtyRect(cur);
  prev = cur;
}

const char* const kStageNames[ST_COUNT] = {"arrive", "greet", "come", "settle", "main", "part", "go", "home"};

}  // namespace

// ---- public ------------------------------------------------------------------------------------

bool visitorsRoll(uint64_t loop_seed) {
  const uint64_t x = loop_seed * 0x9E3779B97F4A7C15ull + 0x56495349ull;   // "VISI"
  uint32_t r = (uint32_t)(x >> 32) ^ (uint32_t)x;
  r ^= r << 13;
  r ^= r >> 17;
  r ^= r << 5;
  return (float)(r >> 8) * (1.f / 16777216.f) < kChance;
}

void visitorsMarkVisited(Memory& m) { m.visit_wait = 2; }

void visitorsBeginLoop(World& w, uint64_t loop_seed, const Inputs& in) {
  Memory& m = w.memory;
  if (m.visit_wait > 0) --m.visit_wait;
  if (!camp(w)) return;   // the set pieces keep the union for themselves
  Visitors& V = w.vis;
  memset(&V, 0, sizeof V);
  V.dog = -1;
  V.meteor_t = -1.f;
  V.enabled = in.visitors;
  const uint8_t wx = w.weather.kind;
  const bool wet = wx == WX_RAIN || wx == WX_STORM || wx == WX_SNOW;
  V.planned = in.visitors && !wet && m.visit_wait == 0 && visitorsRoll(loop_seed);
  const uint64_t x = loop_seed * 0xD1B54A32D192ED03ull + 0x434F55504C45ull;   // "COUPLE"
  V.rng = (uint32_t)(x >> 32) ^ (uint32_t)x;
  if (V.rng == 0) V.rng = 0x2545F491u;
}

void visitorsSetInputs(World& w, bool enabled, bool night, const SkyDraw& sky) {
  if (!camp(w)) return;
  Visitors& V = w.vis;
  V.enabled = enabled;
  V.night = night;
  V.moon = sky.moon;
  V.dark = !sky.moon;
  V.moon_x = sky.moon_x;
  V.moon_y = sky.moon_y;
  V.cover = sky.cover;
}

bool visitorsHolds(const World& w) { return camp(w) && w.vis.on; }

bool visitorsCanStart(const World& w) {
  if (!camp(w)) return false;
  const Visitors& V = w.vis;
  if (!V.planned || !V.enabled || V.on || V.done || w.fi.on || w.react.active) return false;
  // A minute or more of the loop left, and not in the beats that end it.
  const uint32_t left_t = spineLoopTicks(w.plan) - (storyTick(w) < spineLoopTicks(w.plan) ? storyTick(w) : spineLoopTicks(w.plan));
  if ((float)left_t < kLeftLoop * (float)SS_TICK_HZ) return false;
  const char* b = w.minors.beat;
  if (b && (strcmp(b, "close") == 0 || strcmp(b, "leave") == 0 || strcmp(b, "afternoon") == 0 || strcmp(b, "dayhike") == 0)) return false;
  if (w.plan.family == SF_EVENING && w.fire.state != FIRE_LIT && !(V.night && V.cover < 4)) return false;
  return fireIceCalm(w);
}

void visitorsBegin(World& w) {
  Visitors& V = w.vis;
  const Hiker& h = w.hiker;
  const HikerFx& fx = w.fx;
  V.on = true;
  V.home = h.feet;
  V.home_face = (int8_t)h.facing;
  V.home_pose = (uint8_t)h.target;
  V.home_path = fx.path_on;
  V.home_path_v[0] = fx.path_x0;
  V.home_path_v[1] = fx.path_y0;
  V.home_path_v[2] = fx.path_x1;
  V.home_path_v[3] = fx.path_y1;
  V.home_reach_to = fx.reach_to;
  V.home_walk = h.walking;
  V.home_walk_x = h.walk_x;
  V.home_walk_v = h.walk_speed;
  visitorsMarkVisited(w.memory);
  w.busy = true;
  if (w.plan.family != SF_EVENING) V.kind = VK_FRISBEE;
  else if (V.night && V.cover < 4) V.kind = VK_SCOPES;
  else V.kind = VK_FIRESIDE;
  if (h.walking) hikerSetPose(w.hiker, POSE_STAND, 0.3f);
  // In from the right along the trail, Ann leading.
  const float y = trailY(w, 780.f);
  for (int k = 0; k < 2; ++k) {
    Visitor& v = V.v[k];
    memset(&v, 0, sizeof v);
    const float x = 850.f + 34.f * (float)k;
    hikerInit(v.h, Vec2{x, y}, -1);
    v.h.look = (uint8_t)(k == 0 ? LOOK_ANN : LOOK_BEN);
    v.h.height = k == 0 ? kAnnH : kBenH;
    v.h.visible = false;
    walk(w, k + 1, Vec2{700.f + 36.f * (float)k, trailY(w, 700.f + 36.f * (float)k)}, kStroll);
  }
  stage(V, ST_IN);
  V.t = 0.f;
}

void visitorsStep(World& w) {
  Visitors& V = w.vis;
  if (!camp(w) || !V.on) return;
  V.st += kDt;
  V.t += kDt;
  switch (V.stage) {
    case ST_IN:
      if ((!walking(w, 1) && !walking(w, 2)) || V.st > 16.f) stage(V, ST_GREET);
      break;
    case ST_GREET:   // they wave from the trail; he turns and waves them over
      if (at(V.st, 0.05f)) pose(w, 1, POSE_WAVE, 0.4f);
      if (at(V.st, 0.35f)) pose(w, 2, POSE_WAVE, 0.4f);
      if (at(V.st, 0.6f)) {
        faceX(w, 0, 900.f);
        pose(w, 0, POSE_WAVE, 0.45f);
      }
      if (at(V.st, 2.4f)) {
        pose(w, 1, POSE_STAND, 0.4f);
        pose(w, 2, POSE_STAND, 0.4f);
      }
      if (at(V.st, 3.0f)) pose(w, 0, POSE_STAND, 0.4f);
      if (V.st >= 3.4f) {
        if (V.kind == VK_FIRESIDE) placesFireside(w);
        else if (V.kind == VK_FRISBEE) placesFrisbee(w);
        else placesScopes(w);
        stage(V, ST_COME);
        if (V.kind == VK_FIRESIDE) {
          go(w, 0, V.spot[0], kStroll);
          go(w, 1, V.spot[1], kStroll);
          go(w, 2, V.spot[2], kStroll);
        }
      }
      break;
    case ST_COME:
      if (V.kind == VK_FRISBEE || V.kind == VK_SCOPES) {   // packs down where they stand first
        if (at(V.st, 0.05f)) {
          pose(w, 1, POSE_CROUCH, 0.4f);
          pose(w, 2, POSE_CROUCH, 0.4f);
        }
        if (at(V.st, 0.6f)) {
          packDown(w, 0, 12.f);
          packDown(w, 1, 14.f);
        }
        if (at(V.st, 1.0f)) {
          pose(w, 1, POSE_STAND, 0.4f);
          pose(w, 2, POSE_STAND, 0.4f);
        }
        if (at(V.st, 1.4f)) {
          go(w, 1, V.kind == VK_FRISBEE ? V.spot[1] : V.spot[1], kStroll);
          go(w, 2, V.spot[2], kStroll);
          if (V.kind == VK_FRISBEE) go(w, 0, V.spot[0], kStroll);
        }
        if (V.st < 1.5f) break;
      }
      if (allStill(w) || V.st > 24.f) stage(V, ST_SETTLE);
      break;
    case ST_SETTLE:
      if (V.kind == VK_FIRESIDE) {
        if (at(V.st, 0.05f)) {
          faceX(w, 0, w.fire.pos.x);
          faceX(w, 1, w.fire.pos.x);
          faceX(w, 2, w.fire.pos.x);
          pose(w, 1, POSE_CROUCH, 0.4f);
          pose(w, 2, POSE_CROUCH, 0.4f);
          if (w.hiker.target != POSE_SIT) pose(w, 0, POSE_SIT, 0.7f);
        }
        if (at(V.st, 0.7f)) {
          packDown(w, 0, -16.f);
          packDown(w, 1, 20.f);
        }
        if (at(V.st, 1.2f)) {
          pose(w, 1, POSE_SIT, 0.7f);
          pose(w, 2, POSE_SIT, 0.7f);
        }
        if (at(V.st, 2.6f)) {   // cups out
          for (int i = 0; i < 3; ++i) hikerCarry(rig(w, i), CARRY_MUG);
        }
        if (V.st > 2.6f)
          for (int i = 0; i < 3; ++i) holdCup(w, i);
        if (V.st >= 3.6f) stage(V, ST_MAIN);
      } else if (V.kind == VK_FRISBEE) {
        if (at(V.st, 0.05f)) {
          const Vec2 c{(V.spot[0].x + V.spot[1].x + V.spot[2].x) / 3.f, 0.f};
          for (int i = 0; i < 3; ++i) faceX(w, i, c.x);
          V.disc_mode = DS_HELD;
          V.holder = 0;
          V.dog = (int8_t)houseDogsLend(w);
          if (V.dog >= 0) {   // the dog comes to watch beside him
            const Vec2 p = V.spot[0];
            directorWalk(w, V.dog, Vec2{p.x + 30.f, groundY(w, p.x + 30.f, p.y + 8.f)}, 70.f, CA_MOVE);
          }
        }
        if (V.st >= 1.2f) {
          V.n = 0;
          startThrow(w);
          stage(V, ST_MAIN);
        }
      } else {   // the telescopes go up
        if (at(V.st, 0.05f)) {
          faceX(w, 1, V.scope[1].x);
          faceX(w, 2, V.scope[0].x);
          pose(w, 1, POSE_CROUCH, 0.4f);
          pose(w, 2, POSE_CROUCH, 0.4f);
        }
        const float k = clamp01((V.st - 0.4f) / 3.f);
        V.scope_k[0] = V.scope_k[1] = k;
        if (at(V.st, 3.6f)) {
          pose(w, 1, POSE_STAND, 0.4f);
          pose(w, 2, POSE_STAND, 0.4f);
        }
        if (V.st >= 4.2f) {
          V.step = VS_LOOK;
          V.ft = 0.f;
          stage(V, ST_MAIN);
        }
      }
      break;
    case ST_MAIN:
      if (V.kind == VK_FIRESIDE) {
        fireside(w);
        if (V.st >= 64.f) {
          restAll(w);
          stage(V, ST_PART);
        }
      } else if (V.kind == VK_FRISBEE) {
        frisbee(w);
        if (V.step == FS_DONE) {
          restAll(w);
          V.disc_mode = DS_NONE;
          stage(V, ST_PART);
          go(w, 1, Vec2{V.v[0].pack_at.x - 12.f, V.v[0].pack_at.y + 3.f}, kStroll);
          go(w, 2, Vec2{V.v[1].pack_at.x - 14.f, V.v[1].pack_at.y + 3.f}, kStroll);
        }
      } else {
        scopes(w);
        if (V.step == VS_DONE) {
          restAll(w);
          stage(V, ST_PART);
          go(w, 1, Vec2{V.v[0].pack_at.x - 12.f, V.v[0].pack_at.y + 3.f}, kStroll);
          go(w, 2, Vec2{V.v[1].pack_at.x - 14.f, V.v[1].pack_at.y + 3.f}, kStroll);
        }
      }
      break;
    case ST_PART: {   // cups away, packs on, scopes down; waves
      if (walking(w, 1) || walking(w, 2)) {
        V.st = 0.f;
        break;
      }
      if (at(V.st, 0.05f)) {
        if (V.kind == VK_FIRESIDE) {
          pose(w, 1, POSE_STAND, 0.7f);
          pose(w, 2, POSE_STAND, 0.7f);
          hikerCarry(rig(w, 1), CARRY_NONE);
          hikerCarry(rig(w, 2), CARRY_NONE);
        }
        if (V.kind == VK_FRISBEE) houseDogsGiveBack(w);
      }
      if (at(V.st, 0.9f)) {
        pose(w, 1, POSE_CROUCH, 0.4f);
        pose(w, 2, POSE_CROUCH, 0.4f);
      }
      if (V.kind == VK_SCOPES) V.scope_k[0] = V.scope_k[1] = 1.f - clamp01((V.st - 0.9f) / 2.f);
      if (at(V.st, 1.5f)) {
        packUp(w, 0);
        packUp(w, 1);
      }
      if (at(V.st, 2.2f) || (V.kind == VK_SCOPES && at(V.st, 3.0f))) {
        pose(w, 1, POSE_STAND, 0.4f);
        pose(w, 2, POSE_STAND, 0.4f);
      }
      const float wave = V.kind == VK_SCOPES ? 3.6f : 2.8f;
      if (at(V.st, wave)) {
        faceX(w, 1, w.hiker.feet.x);
        faceX(w, 2, w.hiker.feet.x);
        faceX(w, 0, rigC(w, 1).feet.x);
        pose(w, 1, POSE_WAVE, 0.4f);
        pose(w, 2, POSE_WAVE, 0.4f);
        if (V.kind == VK_FIRESIDE) arm(w, 0, 1, 1.f, 2.4f, 0.6f);   // he waves from where he sits
        else pose(w, 0, POSE_WAVE, 0.45f);
      }
      if (at(V.st, wave + 2.2f)) {
        pose(w, 1, POSE_STAND, 0.4f);
        pose(w, 2, POSE_STAND, 0.4f);
        if (V.kind == VK_FIRESIDE) arm(w, 0, 1, 0.f, 0.f, 0.f);
        else pose(w, 0, POSE_STAND, 0.4f);
      }
      if (V.st >= wave + 2.6f) {   // off to the left along the trail
        stage(V, ST_GO);
        const float y = trailY(w, 40.f);
        go(w, 1, Vec2{-70.f, y + 4.f}, kStroll);
        go(w, 2, Vec2{-110.f, y}, kStroll);
      }
      break;
    }
    case ST_GO: {
      const bool gone = !rigC(w, 1).visible && !rigC(w, 2).visible && !walking(w, 1) && !walking(w, 2);
      if (V.kind == VK_FIRESIDE && at(V.st, 3.f)) {   // his cup away, then up and home
        hikerCarry(w.hiker, CARRY_NONE);
        w.fx.arm_w[0] = 0.f;
      }
      if (at(V.st, V.kind == VK_FIRESIDE ? 6.f : 1.f)) {
        if (w.hiker.target == POSE_SIT || w.hiker.target == POSE_HANDS_ON_HIPS) pose(w, 0, POSE_STAND, 0.6f);
      }
      if (at(V.st, V.kind == VK_FIRESIDE ? 7.f : 1.8f)) go(w, 0, V.home, kStroll);
      if (gone && V.st > 8.f && !walking(w, 0)) {
        w.hiker.facing = V.home_face;
        pose(w, 0, (PoseId)V.home_pose, 0.5f);
        stage(V, ST_HOME);
      }
      break;
    }
    case ST_HOME:
      if (V.st >= 0.6f && !w.hiker.blending) finish(w);
      break;
    default: break;
  }
  stepVisitorRigs(w);
}

const char* visitorsStepName(const World& w) {
  if (!camp(w) || !w.vis.on) return "";
  const Visitors& V = w.vis;
  if (V.stage == ST_MAIN && V.kind == VK_FIRESIDE) {
    const float t = V.st;
    return t < 18.f ? "talk" : (t < 32.f ? "story" : (t < 39.f ? "laugh" : (t < 46.f ? "talk2" : (t < 52.f ? "toast" : "quiet"))));
  }
  if (V.stage == ST_MAIN && V.kind == VK_FRISBEE) {
    static const char* const k[] = {"aim", "fly", "catch", "fumble", "pick", "back", "dive", "up", "dog_run", "dog_off", "dog_drop", "done"};
    return V.step < sizeof k / sizeof k[0] ? k[V.step] : "?";
  }
  if (V.stage == ST_MAIN && V.kind == VK_SCOPES) {
    static const char* const k[] = {"look", "hero_to", "hero_look", "impressed", "meteor", "turns", "done"};
    return V.step < sizeof k / sizeof k[0] ? k[V.step] : "?";
  }
  return V.stage < ST_COUNT ? kStageNames[V.stage] : "?";
}

void visitorsTrack(World& w, Renderer& r) {
  if (!camp(w)) return;
  Visitors& V = w.vis;
  if (!V.on) {   // gone: their last places once, then nothing
    bool any = !V.d_disc.empty() || !V.d_meteor.empty() || !V.d_scope[0].empty() || !V.d_scope[1].empty();
    for (const Visitor& v : V.v) any = any || !v.d_rect.empty() || !v.d_pack.empty();
    if (!any) return;
  }
  for (Visitor& v : V.v) {
    track(r, v.d_rect, v.rect, !v.rect.empty());
    track(r, v.d_pack, packRect(v), false);
  }
  track(r, V.d_disc, discRect(V), V.disc_mode != DS_NONE);
  for (int k = 0; k < 2; ++k) track(r, V.d_scope[k], scopeRect(V, k), V.scope_k[k] > 0.f && V.scope_k[k] < 1.f);
  track(r, V.d_meteor, meteorRect(V), V.meteor_t >= 0.f);
}

bool visitorsActorY(const World& w, int idx, int16_t& y) {
  if (!camp(w) || !w.vis.on) return false;
  const Visitors& V = w.vis;
  if (idx < 2) {
    if (V.v[idx].rect.empty()) return false;
    y = (int16_t)floorf(V.v[idx].h.feet.y);
    return true;
  }
  if (idx < 4) {
    if (!V.v[idx - 2].pack_down) return false;
    y = (int16_t)floorf(V.v[idx - 2].pack_at.y);
    return true;
  }
  if (idx == 4) {
    if (V.disc_mode == DS_NONE) return false;
    float g = V.disc.y + 30.f;   // in the air: in front of the people it flies between
    if (V.disc_mode == DS_GROUND) g = V.disc.y;
    if (V.disc_mode == DS_HELD) g = rigC(w, V.holder).feet.y + 1.f;
    if (V.disc_mode == DS_DOG && V.dog >= 0) g = w.critters[V.dog].feet.y + 1.f;
    if (V.disc_mode == DS_FLY) g = maxf(V.disc_a.y, V.disc_b.y) + 60.f;
    y = (int16_t)floorf(g);
    return true;
  }
  const int k = idx - 5;
  if (V.scope_k[k] <= 0.f) return false;
  y = (int16_t)floorf(V.scope[k].y);
  return true;
}

void visitorsDrawActor(const World& w, int idx, Surface& s, const Rect& clip, const Palette& pal) {
  const Visitors& V = w.vis;
  if (idx < 2) {
    hikerDraw(V.v[idx].h, s, pal, clip);
    return;
  }
  if (idx < 4) {   // a pack on the ground, in its owner's colour
    const Visitor& v = V.v[idx - 2];
    const Rect b = packRect(v);
    if (b.clipped(clip).empty()) return;
    const float H = v.h.height * (v.h.look == LOOK_ANN ? 0.8f : 1.05f);
    const Vec2 p = v.pack_at;
    const Vec2 body[6] = {{p.x - 0.1f * H, p.y},          {p.x + 0.1f * H, p.y},          {p.x + 0.11f * H, p.y - 0.21f * H},
                          {p.x + 0.07f * H, p.y - 0.28f * H}, {p.x - 0.07f * H, p.y - 0.28f * H}, {p.x - 0.11f * H, p.y - 0.21f * H}};
    fillPolygon(s, body, 6, pal[v.h.look == LOOK_ANN ? MUSHROOM : WATER], clip);
    if (v.h.look == LOOK_BEN) fillCircle(s, Vec2{p.x, p.y - 0.29f * H}, 0.05f * H, pal[ACTOR], clip);
    return;
  }
  if (idx == 4) {   // the disc: a flat orange oval
    if (discRect(V).clipped(clip).empty()) return;
    fillEllipse(s, V.disc, 4.6f, 1.9f, pal[FIRE], clip);
    fillEllipse(s, Vec2{V.disc.x, V.disc.y - 0.4f}, 2.4f, 0.8f, pal[FIRE_CORE], clip);
    return;
  }
  const int k = idx - 5;   // a telescope on its tripod
  if (scopeRect(V, k).clipped(clip).empty()) return;
  const float e = easeInOut(clamp01(V.scope_k[k]));
  const Vec2 S = V.scope[k], T = scopeTop(V, k);
  const Pixel leg = pal[TRUNK];
  strokeCapsule(s, T, Vec2{S.x - 12.f * e, S.y}, 1.6f, leg, clip);
  strokeCapsule(s, T, Vec2{S.x + 12.f * e, S.y}, 1.6f, leg, clip);
  strokeCapsule(s, T, Vec2{S.x + 2.f * e, S.y - 2.f}, 1.4f, leg, clip);
  if (V.scope_k[k] > 0.55f) {
    const Vec2 d = scopeDir(V, k);
    const float l = 26.f * clamp01((V.scope_k[k] - 0.55f) / 0.45f);
    const Vec2 a{T.x - d.x * 9.f, T.y - d.y * 9.f}, b{T.x + d.x * l, T.y + d.y * l};
    strokeCapsule(s, a, b, 5.f, mixRGB(pal[ACTOR], pal[CHIP_OUTLINE], 0.55f), clip);
    strokeCapsule(s, Vec2{b.x - d.x * 2.f, b.y - d.y * 2.f}, b, 6.f, pal[CLOUD_SHADE], clip);   // the dew cap's rim
  }
}

Rect visitorsRect(const World& w, int k) {
  if (!camp(w) || !w.vis.on || k < 0 || k > 1) return kEmpty;
  return w.vis.v[k].rect;
}

void visitorsDrawSky(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!camp(w) || !w.vis.on) return;
  const Visitors& V = w.vis;
  const Rect b = meteorRect(V);
  if (b.empty() || b.clipped(clip).empty()) return;
  const float u = clamp01(V.meteor_t / 1.2f);
  const Vec2 h{lerp(V.meteor_a.x, V.meteor_b.x, u), lerp(V.meteor_a.y, V.meteor_b.y, u)};
  const float um = maxf(0.f, u - 0.08f), ut = maxf(0.f, u - 0.16f);
  const Vec2 m{lerp(V.meteor_a.x, V.meteor_b.x, um), lerp(V.meteor_a.y, V.meteor_b.y, um)};
  const Vec2 t{lerp(V.meteor_a.x, V.meteor_b.x, ut), lerp(V.meteor_a.y, V.meteor_b.y, ut)};
  strokeCapsule(s, t, m, 1.f, mixRGB(pal[SKY0], pal[STAR], 0.45f), clip);
  strokeCapsule(s, m, h, 1.4f, pal[STAR], clip);
}

}  // namespace ss
