// Critter rigs. See critters.h.
//
// As in the hiker rig, one code path builds each critter as a short list of
// primitives; critterDraw rasterizes it and critterBounds folds its extents.
// Each kind has a builder working in a local frame (px at scale 1, x toward
// the head, y down) around a body frame that pivots at the hip: rearing,
// sitting and climbing all rotate the body nose-up about the hip, so the hind
// legs stay planted. The builder records ground contacts (feet, rump, chin);
// `finish` grounds the lowest one on y = 0, applies squash, lift, shiver and
// the turn-around squeeze, then scales and mirrors about feet.x.
//
// State is split like the hiker's: `pose` is the blended base pose (exactly a
// table entry once a blend lands); loop and gait motion are layered on top at
// draw time by `compose`, so critterTick(c, 0) only recomposes.
#include "critters.h"

#include <math.h>

#include "../anim/ease.h"

namespace ss {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kInvTwoPi = 1.f / (2.f * kPi);
constexpr float kHalfPi = 0.5f * kPi;

// ---- timing ----------------------------------------------------------------
constexpr float kLoopRamp = 0.3f;      // loop motion fades in after the blend lands
constexpr float kGaitIn = 0.2f;        // blend into a gait, and the gait's speed ramp
constexpr float kArriveBlend = 0.22f;  // settle into idle (or the climb hold) on arrival
constexpr float kTurnDur = 0.24f;      // turn-around squeeze; facing flips halfway
constexpr float kAnticT = 0.2f, kAnticAmt = 0.15f;
constexpr float kDoneEps = 1.0e-4f;
constexpr float kBearTurnAt = 0.45f;   // share of a bear startle before it turns

enum BlendKind : uint8_t { BLEND_PLAIN, BLEND_ACTION, BLEND_LAND };

// ---- kinds -------------------------------------------------------------------

constexpr uint32_t bit(CritterAction a) { return 1u << a; }
constexpr uint32_t kAlways = bit(CA_IDLE);

struct KindInfo {
  const char* name;
  float size;          // brief's nominal size: larger of idle height and length (mouse: without the tail)
  float height;        // idle height, feet to top, px at scale 1 (test checks it against the drawing)
  uint32_t acts;
  CritterAction gait;  // critterMoveTo default
  int budget;
  float stride;        // px per gait cycle: MOVE (or HOP length)
  float run_stride;    // px per RUN cycle
  float climb_stride;
  float hop_h;         // hop height px
};

const KindInfo kKinds[CR_COUNT] = {
    {"chipmunk", 20.f, 19.f,
     kAlways | bit(CA_MOVE) | bit(CA_RUN) | bit(CA_SIT) | bit(CA_LOOK) | bit(CA_SNIFF) | bit(CA_EAT) |
         bit(CA_CARRY) | bit(CA_STARTLE),
     CA_MOVE, 7, 9.f, 15.f, 0.f, 0.f},
    {"squirrel", 28.f, 29.f,
     kAlways | bit(CA_RUN) | bit(CA_SIT) | bit(CA_LOOK) | bit(CA_CLIMB) | bit(CA_SHAKE) | bit(CA_HOP) |
         bit(CA_EAT) | bit(CA_CARRY) | bit(CA_STARTLE),
     CA_HOP, 7, 18.f, 20.f, 10.f, 7.f},
    {"frog", 18.f, 14.f, kAlways | bit(CA_HOP) | bit(CA_BLINK) | bit(CA_LOOK) | bit(CA_STARTLE), CA_HOP, 7, 18.f,
     18.f, 0.f, 9.f},
    {"deer", 50.f, 48.f,
     kAlways | bit(CA_MOVE) | bit(CA_RUN) | bit(CA_LOOK) | bit(CA_SNIFF) | bit(CA_HEADTURN) | bit(CA_STARTLE),
     CA_MOVE, 7, 26.f, 56.f, 0.f, 0.f},
    {"raccoon", 30.f, 18.f,
     kAlways | bit(CA_MOVE) | bit(CA_RUN) | bit(CA_SIT) | bit(CA_LOOK) | bit(CA_SNIFF) | bit(CA_REACH) |
         bit(CA_CLIMB) | bit(CA_EAT) | bit(CA_CARRY) | bit(CA_STARTLE),
     CA_MOVE, 7, 13.f, 22.f, 12.f, 0.f},
    {"owl", 24.f, 26.f, kAlways | bit(CA_LOOK) | bit(CA_BLINK) | bit(CA_HEADTURN) | bit(CA_STARTLE), CA_IDLE, 7,
     0.f, 0.f, 0.f, 0.f},
    {"bird", 14.f, 10.f,
     kAlways | bit(CA_MOVE) | bit(CA_HOP) | bit(CA_LOOK) | bit(CA_EAT) | bit(CA_CARRY) | bit(CA_STARTLE), CA_HOP,
     7, 6.f, 14.f, 0.f, 2.5f},
    {"bear", 60.f, 59.f,
     kAlways | bit(CA_MOVE) | bit(CA_RUN) | bit(CA_SIT) | bit(CA_LOOK) | bit(CA_SNIFF) | bit(CA_REACH) |
         bit(CA_CLIMB) | bit(CA_SHAKE) | bit(CA_STARTLE) | bit(CA_REAR),
     CA_MOVE, 9, 34.f, 64.f, 20.f, 0.f},
    {"mouse", 8.f, 8.f,
     kAlways | bit(CA_MOVE) | bit(CA_RUN) | bit(CA_SIT) | bit(CA_LOOK) | bit(CA_SNIFF) | bit(CA_EAT) |
         bit(CA_STARTLE),
     CA_MOVE, 7, 5.f, 8.f, 0.f, 0.f},
};

const char* const kActionNames[CA_COUNT] = {"idle",  "move",  "run",      "sit",     "look", "sniff",
                                            "reach", "climb", "shake",    "hop",     "blink", "headturn",
                                            "startle", "eat", "carry",    "rear"};

// Nose-up rotation of the body when fully reared (sit, stand) and climbing.
constexpr float kRearAng[CR_COUNT] = {1.15f, 1.2f, 0.f, 0.f, 1.1f, 0.f, 0.f, 1.5f, 1.0f};

bool isGait(CritterAction a) { return a == CA_MOVE || a == CA_RUN || a == CA_HOP; }
bool oneShot(CritterAction a) { return a == CA_HOP || a == CA_HEADTURN || a == CA_STARTLE; }

// ---- pose table ----------------------------------------------------------------
// Built at compile time from each kind's idle stance plus per-action overrides.

constexpr CritterPose idleOf(int k) {
  CritterPose p{};
  switch (k) {
    case CR_CHIPMUNK: p.hind[0] = p.hind[1] = 0.55f; p.fore[0] = p.fore[1] = 0.05f; break;
    case CR_SQUIRREL: p.hind[0] = p.hind[1] = 0.6f; p.fore[0] = p.fore[1] = 0.05f; break;
    case CR_FROG: p.hind[0] = 1.25f; p.hind[1] = -1.45f; p.fore[0] = p.fore[1] = 0.1f; break;
    case CR_DEER: p.fore[0] = 0.04f; p.fore[1] = -0.06f; p.hind[0] = -0.04f; p.hind[1] = 0.06f; break;
    case CR_RACCOON: p.hind[0] = p.hind[1] = 0.1f; p.fore[0] = p.fore[1] = 0.f; p.head = -0.1f; break;
    case CR_BIRD: p.hind[0] = p.hind[1] = 0.15f; break;
    case CR_BEAR: p.fore[0] = 0.04f; p.fore[1] = -0.06f; p.hind[0] = -0.04f; p.hind[1] = 0.06f; break;
    case CR_MOUSE: p.hind[0] = p.hind[1] = 0.4f; p.fore[0] = p.fore[1] = 0.1f; break;
    default: break;
  }
  return p;
}

constexpr void sitRodent(CritterPose& p, float head, float tail, float fore) {
  p.rear = 1.f;
  p.hind[0] = p.hind[1] = 1.5f;
  p.fore[0] = fore;
  p.fore[1] = fore - 0.15f;
  p.head = head;
  p.tail = tail;
}

constexpr CritterPose poseFor(int k, int a) {
  CritterPose p = idleOf(k);
  switch (k) {
    case CR_CHIPMUNK:
    case CR_SQUIRREL:
    case CR_MOUSE: {
      const bool sq = k == CR_SQUIRREL;
      switch (a) {
        // The squirrel's plume swings back off the upright body; the
        // chipmunk's thin tail stays straight up.
        case CA_SIT: sitRodent(p, sq ? -1.0f : -0.95f, sq ? 0.82f : 1.05f, 2.3f); break;
        case CA_EAT: sitRodent(p, sq ? -0.9f : -0.85f, sq ? 0.82f : 1.05f, 2.6f); break;
        case CA_LOOK:
          p.rear = 0.55f; p.head = -0.4f; p.fore[0] = p.fore[1] = 1.2f; p.tail = sq ? 0.45f : 0.6f;
          break;
        case CA_SNIFF: p.crouch = 0.4f; p.head = -0.3f; p.pitch = -0.08f; break;
        case CA_CARRY: p.head = 0.15f; p.tail = 0.1f; break;
        case CA_STARTLE:
          p.rear = 0.75f; p.head = -0.6f; p.fore[0] = p.fore[1] = 1.6f; p.tail = sq ? 0.62f : 0.85f; p.squash = 0.06f;
          break;
        case CA_SHAKE: p.crouch = 0.15f; p.tail = 0.2f; break;
        case CA_CLIMB:
          p.climb = 1.f; p.fore[0] = 2.05f; p.fore[1] = 1.8f; p.hind[0] = 1.25f; p.hind[1] = 1.45f;
          p.head = -0.25f; p.tail = sq ? 0.5f : 1.5f;
          break;
        default: break;
      }
      break;
    }
    case CR_FROG:
      if (a == CA_LOOK) { p.pitch = 0.16f; p.fore[0] = p.fore[1] = 0.f; }
      if (a == CA_STARTLE) { p.squash = -0.14f; p.pitch = -0.04f; }
      break;
    case CR_DEER:
      switch (a) {
        case CA_LOOK: p.head = 0.3f; p.look = -0.15f; break;
        case CA_SNIFF:
          p.head = -1.75f; p.look = -0.5f; p.crouch = 0.12f; p.fore[0] = 0.3f; p.fore[1] = 0.18f;
          break;
        case CA_HEADTURN: p.head = 0.45f; p.look = 0.45f; p.pitch = 0.03f; break;
        case CA_STARTLE:
          p.head = 0.4f; p.look = -0.25f; p.crouch = 0.1f; p.hind[0] = -0.16f; p.hind[1] = 0.04f;
          p.fore[0] = 0.12f; p.squash = 0.02f;
          break;
        default: break;
      }
      break;
    case CR_RACCOON:
      switch (a) {
        case CA_SIT: p.rear = 1.f; p.hind[0] = p.hind[1] = 1.4f; p.fore[0] = 2.2f; p.fore[1] = 2.0f; p.head = -0.9f;
          p.tail = 1.4f; break;
        case CA_EAT: p.rear = 1.f; p.hind[0] = p.hind[1] = 1.4f; p.fore[0] = 2.55f; p.fore[1] = 2.4f;
          p.head = -0.75f; p.tail = 1.4f; break;
        case CA_LOOK: p.rear = 0.45f; p.head = -0.3f; p.fore[0] = p.fore[1] = 0.9f; p.tail = 0.3f; break;
        case CA_SNIFF: p.crouch = 0.3f; p.head = -0.45f; break;
        case CA_REACH: p.rear = 1.f; p.hind[0] = p.hind[1] = 0.1f; p.fore[0] = 2.45f; p.fore[1] = 2.25f;
          p.head = -0.65f; p.tail = 1.4f; break;
        case CA_CLIMB: p.climb = 1.f; p.fore[0] = 2.05f; p.fore[1] = 1.8f; p.hind[0] = 1.3f; p.hind[1] = 1.5f;
          p.head = -0.35f; p.tail = 0.6f; break;
        case CA_CARRY: p.head = 0.2f; break;
        case CA_STARTLE: p.rear = 0.35f; p.head = 0.1f; p.tail = 0.5f; p.squash = 0.05f;
          p.fore[0] = p.fore[1] = 0.5f; break;
        default: break;
      }
      break;
    case CR_OWL:
      if (a == CA_LOOK) p.head = 0.32f;
      if (a == CA_STARTLE) p.squash = 0.2f;
      break;
    case CR_BIRD:
      switch (a) {
        case CA_MOVE: p.fore[0] = 0.9f; p.hind[0] = p.hind[1] = -1.3f; p.pitch = 0.05f; break;
        case CA_LOOK: p.head = 0.35f; p.pitch = 0.12f; break;
        case CA_EAT: p.head = -0.3f; p.pitch = -0.15f; break;
        case CA_CARRY: p.head = 0.1f; break;
        case CA_STARTLE: p.pitch = 0.25f; p.head = 0.2f; p.squash = 0.1f; p.fore[0] = 0.35f; break;
        default: break;
      }
      break;
    case CR_BEAR:
      switch (a) {
        case CA_SIT: p.rear = 0.72f; p.hind[0] = 1.45f; p.hind[1] = 1.3f; p.fore[0] = 0.25f; p.fore[1] = 0.15f;
          p.head = -0.5f; break;
        case CA_LOOK: p.head = 0.35f; break;
        case CA_SNIFF: p.head = -0.5f; p.crouch = 0.12f; break;
        case CA_REACH: p.rear = 1.f; p.fore[0] = 2.55f; p.fore[1] = 0.7f; p.hind[0] = 0.f; p.hind[1] = 0.05f;
          p.head = -0.85f; break;
        case CA_CLIMB: p.climb = 1.f; p.fore[0] = 1.95f; p.fore[1] = 1.75f; p.hind[0] = 1.3f; p.hind[1] = 1.1f;
          p.head = -0.3f; break;
        case CA_SHAKE: p.rear = 1.f; p.fore[0] = 1.75f; p.fore[1] = 1.6f; p.hind[0] = 0.f; p.hind[1] = 0.05f;
          p.head = -1.0f; break;
        case CA_STARTLE: p.crouch = 0.25f; p.head = 0.25f; p.squash = -0.04f; break;
        case CA_REAR: p.rear = 1.f; p.fore[0] = 0.55f; p.fore[1] = 0.35f; p.hind[0] = 0.f; p.hind[1] = 0.04f;
          p.head = -1.3f; break;
        default: break;
      }
      break;
    default: break;
  }
  return p;
}

struct PoseTable {
  CritterPose v[CR_COUNT][CA_COUNT];
};
constexpr PoseTable makeTable() {
  PoseTable t{};
  for (int k = 0; k < CR_COUNT; ++k)
    for (int a = 0; a < CA_COUNT; ++a) t.v[k][a] = poseFor(k, a);
  return t;
}
constexpr PoseTable kPoses = makeTable();

BlendKind blendKindFor(CritterAction a) {
  switch (a) {
    case CA_STARTLE: case CA_REAR: case CA_REACH: return BLEND_ACTION;
    case CA_SIT: return BLEND_LAND;
    default: return BLEND_PLAIN;
  }
}

float blendCurve(uint8_t kind, float t) {
  if (kind == BLEND_ACTION) {
    if (t < kAnticT) return -kAnticAmt * easeOutCubic(t / kAnticT);
    return lerp(-kAnticAmt, 1.f, easeInOutCubic((t - kAnticT) / (1.f - kAnticT)));
  }
  if (kind == BLEND_LAND) return easeOutBack(easeInOut(t));
  return easeInOutCubic(t);
}

CritterPose mix(const CritterPose& a, const CritterPose& b, float t) {
  CritterPose r;
  r.lift = lerp(a.lift, b.lift, t);
  r.pitch = lerp(a.pitch, b.pitch, t);
  r.head = lerp(a.head, b.head, t);
  r.look = lerp(a.look, b.look, t);
  r.squash = lerp(a.squash, b.squash, t);
  r.tail = lerp(a.tail, b.tail, t);
  r.rear = lerp(a.rear, b.rear, t);
  r.crouch = lerp(a.crouch, b.crouch, t);
  r.climb = lerp(a.climb, b.climb, t);
  for (int i = 0; i < 2; ++i) {
    r.fore[i] = lerp(a.fore[i], b.fore[i], t);
    r.hind[i] = lerp(a.hind[i], b.hind[i], t);
  }
  return r;
}

inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float pos(float v) { return v > 0.f ? v : 0.f; }
inline float wrap1(float v) { return v - floorf(v); }
// sin/cos of an angle in radians through the table.
inline float sinR(float a) { return sinT(a * kInvTwoPi); }
inline float cosR(float a) { return cosT(a * kInvTwoPi); }

// ---- composition: base pose + loop, gait and one-shot motion -----------------

struct Look {
  CritterPose p;
  float eye;        // 1 open .. 0 shut
  float throat;     // frog throat pulse 0..1
  float shiver;     // px, x offset
  float lift_f[2], lift_h[2];   // gait foot lift, px
  float yaw;        // owl head turn, turns
  float sx;         // turn-around squeeze 1..~0.15
};

// Loop rate in cycles per second for an action held in place.
float loopRate(const Critter& c, CritterAction a) {
  const float jit = 0.7f + 0.6f * c.jitter_dial;
  const float spd = 0.8f + 0.4f * c.speed_dial;
  switch (a) {
    case CA_IDLE: return 0.45f * jit;
    case CA_MOVE: return (c.kind == CR_BIRD ? 5.f : 1.4f) * spd;
    case CA_RUN: return 2.6f * spd;
    case CA_EAT: return c.kind == CR_BIRD ? 1.1f : (c.kind == CR_MOUSE ? 4.5f : 2.8f);
    case CA_SNIFF: return c.kind == CR_BEAR ? 0.45f : 0.75f;
    case CA_SHAKE: return c.kind == CR_BEAR ? 4.5f : 7.f;
    case CA_BLINK: return (c.kind == CR_OWL ? 0.32f : 0.45f) * jit;
    case CA_CLIMB: return 1.2f * spd;
    default: return 0.f;
  }
}

// Hop: crouch (anticipation), arc, land with a squash. u in 0..1.
void addHop(Look& L, CritterKind k, float u, float h, float w) {
  if (u <= 0.f || u >= 1.f || w <= 0.f) return;
  float sq = 0.f, cr = 0.f, lf = 0.f, pt = 0.f, ext = 0.f, reach = 0.f;
  if (u < 0.2f) {
    const float a = easeInOut(u / 0.2f);
    sq = -0.16f * a;
    cr = 0.3f * a;
    pt = -0.1f * a;
  } else if (u < 0.8f) {
    const float s = (u - 0.2f) / 0.6f;
    const float e = easeOutCubic(minf(1.f, s / 0.25f));
    const float arc = sinT(0.5f * s);
    lf = h * arc;
    sq = -0.16f * (1.f - e) + 0.14f * arc * (1.f - s);
    cr = 0.3f * (1.f - e);
    pt = -0.1f * (1.f - e) + 0.35f * cosT(0.5f * s) * e;
    ext = s < 0.15f ? easeOutCubic(s / 0.15f) : 1.f - easeInOut(window(s, 0.5f, 0.95f));
    reach = window(s, 0.35f, 0.85f) * (1.f - window(s, 0.9f, 1.f));
  } else {
    const float v = (u - 0.8f) / 0.2f;
    const float b = sinT(0.5f * v);
    sq = -0.15f * b;
    cr = 0.25f * b;
  }
  CritterPose& p = L.p;
  p.lift += w * lf;
  p.squash += w * sq;
  p.crouch += w * cr;
  if (k == CR_FROG) {
    p.pitch += w * 0.6f * pt;
    p.hind[0] += w * ext * -2.05f;
    p.hind[1] += w * ext * 0.6f;
    p.fore[0] += w * (0.9f * reach - 0.3f * cr);
    p.fore[1] = p.fore[0];
  } else {
    p.pitch += w * pt;
    p.hind[0] += w * ext * -1.1f;
    p.hind[1] += w * ext * -1.1f;
    p.fore[0] += w * 1.0f * reach;
    p.fore[1] += w * 0.9f * reach;
    p.tail += w * 0.3f * (ext - reach);
  }
}

// Gait cycle (walk, scurry, bound, flight, climb) at phase ph, weight w.
void addGait(Look& L, const Critter& c, CritterAction a, float ph, float w) {
  if (w <= 0.f) return;
  CritterPose& p = L.p;
  const CritterKind k = c.kind;
  if (a == CA_HOP) {
    addHop(L, k, ph, kKinds[k].hop_h, w);
    return;
  }
  if (a == CA_CLIMB) {   // diagonal pairs reach up the trunk
    const float s = sinT(ph);
    p.fore[0] += w * 0.35f * s;
    p.hind[1] += w * 0.3f * s;
    p.fore[1] -= w * 0.35f * s;
    p.hind[0] -= w * 0.3f * s;
    p.head += w * 0.06f * sinT(2.f * ph);
    return;
  }
  if (k == CR_BIRD) {   // flight: wings flap; lift comes from the move arc or a hover
    p.fore[0] += w * 0.75f * sinT(ph);
    p.pitch += w * 0.04f * sinT(ph + 0.25f);
    if (!c.moving) p.lift += w * 6.f;
    return;
  }
  const bool four = k == CR_DEER || k == CR_BEAR;
  if (a == CA_MOVE && four) {   // four-beat walk: hind, fore, hind, fore
    const float amp = k == CR_DEER ? 0.34f : 0.3f;
    const float lift = k == CR_DEER ? 3.f : 4.f;
    const float off[4] = {0.f, 0.25f, 0.5f, 0.75f};   // hind0, fore0, hind1, fore1
    float* legs[4] = {&p.hind[0], &p.fore[0], &p.hind[1], &p.fore[1]};
    float* lifts[4] = {&L.lift_h[0], &L.lift_f[0], &L.lift_h[1], &L.lift_f[1]};
    for (int i = 0; i < 4; ++i) {
      *legs[i] += w * amp * sinT(ph - off[i]);
      *lifts[i] += w * lift * pos(cosT(ph - off[i]));
    }
    p.head += w * (k == CR_BEAR ? 0.06f : 0.05f) * sinT(2.f * ph);
    p.pitch += w * 0.015f * sinT(2.f * ph + 0.25f);
    if (k == CR_BEAR) {   // heavy: the body sinks on each step
      p.look += w * 1.2f * sinT(ph);
      p.crouch += w * 0.16f * (0.5f + 0.5f * sinT(2.f * ph));
    }
    return;
  }
  if (a == CA_MOVE && k == CR_RACCOON) {   // waddle: near legs opposite, body rocks
    p.hind[0] += w * 0.45f * sinT(ph);
    p.hind[1] -= w * 0.45f * sinT(ph);
    p.fore[0] -= w * 0.45f * sinT(ph);
    p.fore[1] += w * 0.45f * sinT(ph);
    L.lift_h[0] += w * 1.5f * pos(cosT(ph));
    L.lift_f[0] += w * 1.5f * pos(-cosT(ph));
    p.pitch += w * 0.05f * sinT(2.f * ph);
    p.tail += w * 0.12f * sinT(ph);
    p.head += w * 0.05f * sinT(2.f * ph + 0.3f);
    return;
  }
  // Bound / gallop / scurry: fore pair and hind pair half a cycle apart, a
  // short flight with the legs stretched, gathered on landing.
  float amp = 0.6f, air = 1.f, pitchAmp = 0.07f;
  switch (k) {
    case CR_CHIPMUNK: amp = a == CA_RUN ? 0.85f : 0.6f; air = a == CA_RUN ? 2.f : 0.7f; break;
    case CR_SQUIRREL: amp = 0.9f; air = 3.f; break;
    case CR_MOUSE: amp = a == CA_RUN ? 0.8f : 0.6f; air = a == CA_RUN ? 1.f : 0.4f; break;
    case CR_RACCOON: amp = 0.7f; air = 3.f; break;
    case CR_DEER: amp = 0.95f; air = 10.f; pitchAmp = 0.12f; break;
    case CR_BEAR: amp = 0.6f; air = 5.f; pitchAmp = 0.06f; break;
    default: break;
  }
  const float s = sinT(ph), cc = cosT(ph);
  const float fly = pos(s);   // airborne half
  p.lift += w * air * fly * fly;
  p.fore[0] += w * amp * (0.75f * s + 0.25f);
  p.fore[1] += w * amp * (0.75f * sinT(ph - 0.06f) + 0.25f);
  p.hind[0] += w * amp * (-0.75f * s - 0.1f);
  p.hind[1] += w * amp * (-0.75f * sinT(ph - 0.06f) - 0.1f);
  L.lift_f[0] += w * 0.12f * amp * air * pos(-cc);
  L.lift_h[0] += w * 0.12f * amp * air * pos(cc);
  L.lift_f[1] = L.lift_f[0];
  L.lift_h[1] = L.lift_h[0];
  p.pitch += w * pitchAmp * cc;
  p.squash += w * 0.05f * s;
  p.tail += w * 0.25f * cc;
  p.head -= w * 0.5f * pitchAmp * cc;
}

float blinkEye(float ph, bool slow) {
  if (slow) {   // owl: lids close, hold, open
    if (ph < 0.12f) return 1.f - easeInOut(ph / 0.12f);
    if (ph < 0.22f) return 0.f;
    if (ph < 0.36f) return easeInOut((ph - 0.22f) / 0.14f);
    return 1.f;
  }
  if (ph < 0.04f) return 1.f - easeInOut(ph / 0.04f);
  if (ph < 0.1f) return easeInOut((ph - 0.04f) / 0.06f);
  return 1.f;
}

// Held loop motion (idle fidget, eat, sniff, shake, blink).
void addLoop(Look& L, const Critter& c, CritterAction a, float ph, float w) {
  if (w <= 0.f) return;
  CritterPose& p = L.p;
  const CritterKind k = c.kind;
  const float amp = 0.5f + c.jitter_dial;
  switch (a) {
    case CA_IDLE: {
      const float n1 = sumSines(ph * 2.f, 2), n2 = sumSines(ph * 3.f + 0.4f, 2);
      switch (k) {
        case CR_FROG: L.throat = w * (0.5f + 0.5f * sinT(ph * 3.f)); break;
        case CR_OWL: p.squash += w * 0.025f * sinT(ph); p.head += w * amp * 0.06f * n1; break;
        case CR_BEAR: p.squash += w * 0.015f * sinT(ph); p.look += w * amp * 1.0f * n1; break;
        case CR_DEER: p.head += w * amp * 0.06f * n1; p.look += w * amp * 0.08f * n2; break;
        default: p.head += w * amp * 0.12f * n1; p.tail += w * amp * 0.2f * n2; break;
      }
      break;
    }
    case CA_EAT: {
      if (k == CR_BIRD) {   // peck
        const float d = pos(sinT(ph));
        p.head -= w * 0.55f * d * d;
        p.pitch -= w * 0.4f * d * d;
      } else {              // nibble: head bobs to the paws
        const float s = sinT(ph);
        p.head -= w * 0.1f * s;
        p.fore[0] += w * 0.1f * s;
        p.fore[1] += w * 0.1f * s;
        if (k == CR_MOUSE) p.pitch -= w * 0.06f * s;
      }
      break;
    }
    case CA_SNIFF: {
      p.head += w * 0.12f * sinT(ph * 2.f);
      p.look += w * (k == CR_BEAR ? 3.f : 1.f) * sinT(ph);
      p.crouch += w * 0.04f * sinT(ph * 2.f + 0.25f);
      break;
    }
    case CA_SHAKE: {
      const float s = sinT(ph);
      L.shiver = w * (k == CR_BEAR ? 1.5f : 0.9f) * s;
      p.squash += w * 0.03f * sinT(2.f * ph);
      p.tail += w * 0.35f * s;
      p.head += w * 0.08f * s;
      break;
    }
    case CA_BLINK: {
      const float e = blinkEye(ph, k == CR_OWL);
      L.eye = 1.f - w * (1.f - e);
      break;
    }
    default: break;
  }
}

float turnScale(float turn_t) {
  if (turn_t < 0.f) return 1.f;
  const float u = minf(1.f, turn_t / kTurnDur);
  return maxf(0.15f, fabsf(cosT(0.5f * u)));
}

Look compose(const Critter& c) {
  Look L;
  L.p = c.pose;
  L.eye = 1.f;
  L.throat = 0.f;
  L.shiver = 0.f;
  L.lift_f[0] = L.lift_f[1] = L.lift_h[0] = L.lift_h[1] = 0.f;
  L.yaw = 0.f;
  L.sx = turnScale(c.turn_t);
  const CritterAction a = c.action;
  const float lw = easeInOut(c.loop_w);
  if (c.moving || c.climbing) {
    addGait(L, c, a, c.phase, easeInOut(c.gait_w));
    if (c.kind == CR_BIRD && a == CA_MOVE) {   // flight arc over the whole move
      const float span = fabsf(c.move_to - c.move_from);
      const float prog = span > 0.f ? clamp01(fabsf(c.feet.x - c.move_from) / span) : 1.f;
      L.p.lift += minf(14.f, 0.3f * span + 4.f) * sinT(0.5f * prog);
    }
  } else if (a == CA_HOP) {
    addHop(L, c.kind, c.phase, kKinds[c.kind].hop_h, 1.f);
  } else if (a == CA_HEADTURN && c.kind == CR_OWL) {
    // A full turn in three snaps, each a quick turn then a hold.
    if (c.phase < 1.f) {
      const float s = c.phase * 3.f;
      const float step = floorf(s);
      L.yaw = (step + easeInOutCubic(window(s - step, 0.f, 0.4f))) / 3.f;
    }
  } else if (isGait(a)) {
    addGait(L, c, a, c.phase, lw);
  } else if (a == CA_CLIMB) {
    // held on the trunk: no motion
  } else {
    addLoop(L, c, a, c.phase, lw);
  }
  return L;
}

// ---- primitive list --------------------------------------------------------

enum PrimKind : uint8_t { PRIM_CAP, PRIM_ELLIPSE, PRIM_POLY };

constexpr int kMaxPrims = 12;
constexpr int kMaxPolyPts = 16;

struct Prim {
  uint8_t kind;
  Slot slot;
  uint8_t n;
  float w, h;   // capsule width; ellipse radii
  Vec2 p[kMaxPolyPts];
};

struct Prims {
  int n;
  Prim v[kMaxPrims];
  float low;      // lowest ground contact, local y
  float belly;    // climbing: x of the belly line, local
  bool has_paw, has_head, has_mouth;
  Vec2 paw, head, mouth; // anchors (critterPaw, critterHead, critterMouth), local, then frame space
};

inline Vec2 add(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
inline Vec2 mul(Vec2 a, float k) { return Vec2{a.x * k, a.y * k}; }
inline Vec2 dirA(float a) { return Vec2{sinR(a), cosR(a)}; }   // from straight down, + forward

// Rotation nose-up by `ang` about `o`, then local (x, y) offsets.
struct Xf {
  Vec2 o;
  float c, s;
};
Xf xf(Vec2 o, float ang) { return Xf{o, cosR(ang), sinR(ang)}; }
Vec2 at(const Xf& f, float x, float y) { return Vec2{f.o.x + x * f.c + y * f.s, f.o.y - x * f.s + y * f.c}; }

Prim* push(Prims& ps, PrimKind k, Slot slot) {
  if (ps.n >= kMaxPrims) return nullptr;   // cannot happen: the largest rig issues 8
  Prim* p = &ps.v[ps.n++];
  p->kind = k;
  p->slot = slot;
  p->n = 0;
  p->w = p->h = 0.f;
  return p;
}
void cap(Prims& ps, Vec2 a, Vec2 b, float w, Slot slot = ACTOR) {
  if (Prim* p = push(ps, PRIM_CAP, slot)) { p->p[0] = a; p->p[1] = b; p->n = 2; p->w = w; }
}
void ellipse(Prims& ps, Vec2 c, float rx, float ry, Slot slot = ACTOR) {
  if (Prim* p = push(ps, PRIM_ELLIPSE, slot)) { p->p[0] = c; p->n = 1; p->w = rx; p->h = ry; }
}
void contact(Prims& ps, float y) { ps.low = maxf(ps.low, y); }
// Polygon from (x, y) pairs in frame f; optionally the points are contacts.
void polyIn(Prims& ps, const Xf& f, const float (*pts)[2], int n, bool touch, Slot slot = ACTOR) {
  Prim* p = push(ps, PRIM_POLY, slot);
  if (!p) return;
  if (n > kMaxPolyPts) n = kMaxPolyPts;
  p->n = (uint8_t)n;
  for (int i = 0; i < n; ++i) {
    p->p[i] = at(f, pts[i][0], pts[i][1]);
    if (touch) contact(ps, p->p[i].y);
  }
}
float maxX(const Prim& p) {
  float m = -3.0e38f;
  for (int i = 0; i < p.n; ++i) m = maxf(m, p.p[i].x);
  return m;
}
Vec2 leg(Prims& ps, Vec2 root, float ang, float len, float w, float footLift) {
  Vec2 foot = add(root, mul(dirA(ang), len));
  foot.y -= footLift;
  cap(ps, root, foot, w);
  contact(ps, foot.y + 0.5f * w);
  return foot;
}

// Carried item at `m` (mouth or paws), tipped by `ang` (nose-up radians).
void carryItem(Prims& ps, uint8_t item, Vec2 m, float ang) {
  const Xf f = xf(m, ang);
  switch (item) {
    case CC_NUT: ellipse(ps, at(f, 0.6f, 1.2f), 1.5f, 1.8f); break;
    case CC_CRUMB: ellipse(ps, at(f, 0.4f, 0.9f), 0.9f, 0.9f); break;
    case CC_SHOE: {   // a boot held by its collar, sole toward the ground
      static const float shoe[8][2] = {{-0.5f, -0.6f}, {2.0f, -0.8f}, {2.4f, 2.6f}, {7.4f, 3.6f},
                                       {7.8f, 5.6f},  {-0.6f, 5.6f}, {-1.2f, 3.0f}, {-1.0f, 0.8f}};
      polyIn(ps, f, shoe, 8, false);
      break;
    }
    case CC_BAR: cap(ps, at(f, -0.5f, 0.9f), at(f, 5.5f, 0.9f), 2.2f); break;
    default: break;
  }
}

// ---- small quadrupeds: chipmunk, squirrel, raccoon, mouse --------------------

struct QuadDims {
  float hipX, Lh, Lf, wh, wf;
  float sx, sy;       // shoulder (fore leg root), body frame
  float nx, ny;       // neck joint, body frame
  float tx, ty;       // tail root, body frame
  float mx, my;       // mouth, head frame
  const float (*body)[2];
  int nb;
  const float (*head)[2];
  int nh;
};

// Chipmunk: compact, round rump, small round-eared head, tail straight up.
const float kChipBody[9][2] = {{-2.2f, 0.6f}, {-3.4f, -2.6f}, {-2.0f, -5.4f}, {1.0f, -6.8f}, {5.0f, -6.6f},
                               {8.4f, -4.8f}, {9.4f, -2.0f},  {7.6f, 0.4f},   {2.5f, 1.0f}};
const float kChipHead[10][2] = {{-1.2f, 1.6f}, {-1.5f, -1.4f}, {-0.4f, -3.4f}, {0.2f, -5.3f}, {1.6f, -3.9f},
                                {3.2f, -3.4f}, {5.0f, -2.0f},  {6.2f, -0.2f},  {5.0f, 1.4f},  {2.0f, 2.2f}};
const QuadDims kChip = {-4.5f, 2.6f, 3.4f, 2.0f, 1.5f, 7.0f, -0.8f, 8.4f, -5.0f, -2.6f, -4.6f,
                        5.6f, 0.8f, kChipBody, 9, kChipHead, 10};

// Squirrel: hunched, taller ear; the plume tail is drawn separately.
const float kSqBody[9][2] = {{-2.6f, 0.8f}, {-4.0f, -3.0f}, {-2.6f, -6.6f}, {1.0f, -8.4f}, {5.0f, -8.0f},
                             {8.6f, -5.8f}, {10.0f, -2.8f}, {8.2f, 0.2f},   {3.0f, 1.2f}};
const float kSqHead[10][2] = {{-1.4f, 1.8f}, {-1.8f, -1.6f}, {-0.6f, -3.8f}, {0.0f, -6.2f}, {1.6f, -4.4f},
                              {3.6f, -4.0f}, {5.8f, -2.4f},  {7.0f, -0.4f},  {5.8f, 1.6f},  {2.4f, 2.6f}};
const QuadDims kSquirrel = {-5.0f, 3.0f, 4.2f, 2.6f, 1.8f, 7.6f, -1.0f, 9.6f, -5.4f, -3.2f, -4.0f,
                            6.4f, 1.0f, kSqBody, 9, kSqHead, 10};

// Raccoon: body and head in one outline (arched back, pointed snout and ear).
// The first 6 points are the body, the rest the head (head frame).
const float kRacBody[8][2] = {{8.0f, 0.6f},  {1.0f, 1.4f},   {-2.6f, 0.6f}, {-4.0f, -3.4f},
                              {-2.4f, -8.6f}, {2.0f, -10.6f}, {8.0f, -8.4f}, {11.0f, -6.2f}};
const float kRacHead[7][2] = {{-0.2f, -4.4f}, {0.8f, -6.8f}, {2.4f, -4.2f}, {3.8f, -3.6f},
                              {8.2f, -0.2f},  {5.2f, 1.4f},  {0.8f, 2.6f}};
const QuadDims kRaccoon = {-6.0f, 5.5f, 5.0f, 3.0f, 2.6f, 7.4f, -1.0f, 10.6f, -3.6f, -3.6f, -4.4f,
                           7.0f, 0.6f, kRacBody, 8, kRacHead, 7};

// Mouse: one teardrop for body and head, round ear, thin tail.
const float kMouseBody[9][2] = {{-1.2f, 0.5f}, {-1.8f, -1.6f}, {-0.6f, -3.3f}, {1.6f, -3.8f}, {3.8f, -3.0f},
                                {5.6f, -1.6f}, {7.0f, -0.2f},  {5.4f, 0.5f},   {2.0f, 0.8f}};
const QuadDims kMouse = {-2.0f, 1.2f, 1.2f, 1.1f, 0.9f, 4.6f, 0.f, 0.f, 0.f, -1.6f, -1.0f,
                         6.6f, 0.2f, kMouseBody, 9, nullptr, 0};

const QuadDims& quadDims(CritterKind k) {
  switch (k) {
    case CR_SQUIRREL: return kSquirrel;
    case CR_RACCOON: return kRaccoon;
    case CR_MOUSE: return kMouse;
    default: return kChip;
  }
}

void buildQuad(const Critter& c, const Look& L, Prims& ps) {
  const CritterKind k = c.kind;
  const QuadDims& d = quadDims(k);
  const CritterPose& P = L.p;
  const float th = P.pitch + P.rear * kRearAng[k] + P.climb * kHalfPi;
  const float fold = 1.f - 0.5f * clamp01(P.crouch);
  const Xf B = xf(Vec2{d.hipX, -d.Lh * fold}, th);
  const Vec2 shoulder = at(B, d.sx, d.sy);
  const Vec2 hip = at(B, 0.3f, -0.4f);
  const Xf H = xf(at(B, d.nx + P.look, d.ny), th + P.head);
  const Vec2 mouth = at(H, d.mx, d.my);
  ps.mouth = mouth;
  ps.has_mouth = true;

  // Tail first (behind the body).
  const Vec2 root = at(B, d.tx, d.ty);
  switch (k) {
    case CR_CHIPMUNK: {
      const float a = th + kPi + 0.12f - P.tail;
      cap(ps, root, add(root, mul(dirA(a), 9.5f)), 2.6f);
      break;
    }
    case CR_SQUIRREL: {   // root sweeps back and up, the plume curls over the back
      const Xf T = xf(root, th - P.tail);
      cap(ps, root, at(T, -4.0f, -5.0f), 4.2f);
      float pts[12][2];
      const float cx = -2.6f, cy = -12.0f, rx = 5.8f, ry = 8.6f, tilt = 0.32f;
      for (int i = 0; i < 12; ++i) {
        const float u = (float)i / 12.f;
        const float ex = rx * cosT(u), ey = ry * sinT(u);
        pts[i][0] = cx + ex * cosR(tilt) + ey * sinR(tilt);
        pts[i][1] = cy - ex * sinR(tilt) + ey * cosR(tilt);
      }
      polyIn(ps, T, pts, 12, false);
      break;
    }
    case CR_RACCOON: {   // thick tail held low, two light rings
      const float a = th + 5.03f - P.tail;
      const Vec2 dir = dirA(a);
      const Vec2 tip = add(root, mul(dir, 9.5f));
      const float w = 5.0f;
      cap(ps, root, tip, w);
      const Vec2 nrm{-dir.y, dir.x};
      const float half = 0.5f * w - 1.1f;
      for (int r = 0; r < 2; ++r) {
        const Vec2 m = add(root, mul(dir, r == 0 ? 4.6f : 7.6f));
        cap(ps, add(m, mul(nrm, half)), add(m, mul(nrm, -half)), 1.5f, ACTOR_LIGHT);
      }
      break;
    }
    case CR_MOUSE: {   // thin tail in two segments, dragging near the ground
      const float a = th + 4.85f - P.tail;
      const Vec2 m = add(root, mul(dirA(a), 4.2f));
      cap(ps, root, m, 0.8f);
      const float a2 = 4.71f + 0.5f * sinR(P.tail * 2.f) - 0.25f;
      cap(ps, m, add(m, mul(dirA(a2), 4.5f)), 0.7f);
      break;
    }
    default: break;
  }

  // Only the near legs are drawn: the far ones hide behind them at this size,
  // and the budget goes to the tail.
  // Body (and head).
  if (k == CR_RACCOON) {
    float pts[15][2];
    int n = 0;
    for (int i = 0; i < 8; ++i) {
      pts[n][0] = d.body[i][0];
      pts[n][1] = d.body[i][1];
      ++n;
    }
    // Head points into the body frame, so one outline holds both.
    for (int i = 0; i < 7; ++i) {
      const Vec2 w = at(H, d.head[i][0], d.head[i][1]);
      // inverse of B: local = R^-1 (w - o)
      const float dx = w.x - B.o.x, dy = w.y - B.o.y;
      pts[n][0] = dx * B.c - dy * B.s;
      pts[n][1] = dx * B.s + dy * B.c;
      ++n;
    }
    polyIn(ps, B, pts, n, true);
    ps.belly = maxX(ps.v[ps.n - 1]);
  } else {
    polyIn(ps, B, d.body, d.nb, true);
    ps.belly = maxX(ps.v[ps.n - 1]);
    if (d.head) polyIn(ps, H, d.head, d.nh, true);
    if (k == CR_MOUSE) {
      const Vec2 e = at(B, 3.0f, -4.0f);
      ellipse(ps, e, 1.55f, 1.55f);
    }
  }

  // Legs: near hind and near fore.
  const float hl = d.Lh * fold, fl = d.Lf * fold;
  leg(ps, hip, P.hind[0], hl, d.wh, L.lift_h[0]);
  const Vec2 paw = leg(ps, shoulder, P.fore[0], fl, d.wf, L.lift_f[0]);
  ps.paw = paw;
  ps.has_paw = true;

  // Item: in the paws when sitting up, else in the mouth.
  if (c.carry != CC_NONE) {
    if (P.rear > 0.5f && P.climb < 0.5f && P.fore[0] > 1.8f) carryItem(ps, c.carry, paw, th + P.head);
    else carryItem(ps, c.carry, mouth, th + P.head);
  }
}

// ---- deer and bear: four jointed legs, neck and head ----------------------

// A two-segment tapered limb as one polygon: root -> joint -> foot, widths
// w0, w1, w2, and an optional toe pointing forward at the foot (hoof, paw).
Vec2 limb(Prims& ps, Vec2 root, float a1, float l1, float a2, float l2, float w0, float w1, float w2, float toe) {
  const Vec2 d1 = dirA(a1), d2 = dirA(a2);
  const Vec2 joint = add(root, mul(d1, l1));
  const Vec2 foot = add(joint, mul(d2, l2));
  const Vec2 n0{-d1.y, d1.x}, n2{-d2.y, d2.x};
  Vec2 nj{n0.x + n2.x, n0.y + n2.y};
  const float nl = sqrtf(nj.x * nj.x + nj.y * nj.y);
  nj = nl > 1.0e-3f ? mul(nj, 1.f / nl) : n0;
  Prim* p = push(ps, PRIM_POLY, ACTOR);
  if (!p) return foot;
  const float h0 = 0.5f * w0, h1 = 0.5f * w1, h2 = 0.5f * w2;
  int n = 0;
  p->p[n++] = add(root, mul(n0, h0));
  p->p[n++] = add(joint, mul(nj, h1));
  p->p[n++] = add(foot, mul(n2, h2));
  if (toe > 0.f) {   // toe forward along the ground, whichever side is forward
    const Vec2 f0 = add(foot, mul(n2, h2)), f1 = add(foot, mul(n2, -h2));
    p->p[n++] = Vec2{maxf(f0.x, f1.x) + toe, maxf(f0.y, f1.y) - 0.2f * w2};
  }
  p->p[n++] = add(foot, mul(n2, -h2));
  p->p[n++] = add(joint, mul(nj, -h1));
  p->p[n++] = add(root, mul(n0, -h0));
  p->n = (uint8_t)n;
  for (int i = 0; i < n; ++i) contact(ps, p->p[i].y);
  return foot;
}

const float kDeerBody[12][2] = {{-2.6f, -1.6f}, {-3.6f, -6.4f}, {-5.0f, -10.8f}, {-1.4f, -10.0f},
                                {5.0f, -10.0f}, {12.0f, -10.6f}, {18.0f, -10.8f}, {21.8f, -6.4f},
                                {21.4f, -1.0f}, {15.0f, 1.2f},   {7.0f, -0.2f},   {1.0f, 0.4f}};
// Head outline in the head frame (origin at the top of the neck, muzzle along
// +x), from the back of the head over both ears to the nose and the throat.
const float kDeerHead[11][2] = {{-3.0f, -1.2f}, {-5.2f, -7.8f}, {-1.8f, -2.8f}, {-1.4f, -8.4f},
                                {0.6f, -3.0f},  {3.4f, -2.2f},  {7.8f, 0.4f},   {8.6f, 2.2f},
                                {7.0f, 3.4f},   {2.0f, 3.4f},   {-1.4f, 3.2f}};

// Deer legs: forearm and cannon in front; gaskin slanting back to the hock
// behind. Lifting a foot folds the lower segment back.
void deerLeg(Prims& ps, Vec2 root, float a, bool hind, float L, float lift, float near) {
  const float fold = 0.11f * lift;
  if (hind) limb(ps, root, a - 0.42f, 0.5f * L, a + 0.12f - fold, 0.52f * L, 5.0f * near, 2.4f, 1.7f, 0.f);
  else limb(ps, root, a + 0.02f, 0.5f * L, a - 0.04f - fold, 0.5f * L, 4.0f * near, 2.2f, 1.7f, 0.f);
}

void buildDeer(const Critter& c, const Look& L, Prims& ps) {
  (void)c;
  const CritterPose& P = L.p;
  const float th = P.pitch;
  const float fold = 1.f - 0.3f * clamp01(P.crouch);
  const float Lg = 22.f * fold;
  const Xf B = xf(Vec2{-10.f, -Lg}, th);
  deerLeg(ps, at(B, 1.6f, -1.8f), P.hind[1], true, Lg, L.lift_h[1], 0.9f);
  deerLeg(ps, at(B, 16.6f, -1.6f), P.fore[1], false, Lg, L.lift_f[1], 0.9f);
  polyIn(ps, B, kDeerBody, 12, true);
  // Neck and head in one outline: two base points on the body, the head ring
  // in the head frame, so the neck tapers from the shoulders to the jaw.
  const Vec2 nb = at(B, 18.6f, -7.0f);
  const float na = th + kPi - 0.66f + P.head;
  const Vec2 nt = add(nb, mul(dirA(na), 14.5f));
  const Xf H = xf(nt, th + 0.5f * P.head + P.look - 0.25f);
  {
    Prim* p = push(ps, PRIM_POLY, ACTOR);
    if (p) {
      int n = 0;
      p->p[n++] = at(B, 15.6f, -9.6f);
      for (int i = 0; i < 11; ++i) p->p[n++] = at(H, kDeerHead[i][0], kDeerHead[i][1]);
      p->p[n++] = at(B, 21.6f, -3.6f);
      p->n = (uint8_t)n;
      for (int i = 0; i < n; ++i) contact(ps, p->p[i].y);
    }
  }
  deerLeg(ps, at(B, 0.6f, -1.8f), P.hind[0], true, Lg, L.lift_h[0], 1.f);
  deerLeg(ps, at(B, 18.0f, -1.6f), P.fore[0], false, Lg, L.lift_f[0], 1.f);
}

// The bear is drawn 9% over its rig units, so it stands about 60 px on all
// fours and about 100 px reared.
constexpr float kBearK = 1.09f;
const float kBearBody[16][2] = {{-6.0f, 3.0f},   {-9.5f, -5.0f},  {-9.5f, -13.0f}, {-6.0f, -20.0f},
                                {2.0f, -24.0f},  {12.0f, -25.5f}, {20.0f, -30.0f}, {26.0f, -32.0f},
                                {32.0f, -29.0f}, {37.0f, -22.0f}, {39.0f, -12.0f}, {37.0f, -3.0f},
                                {31.0f, 3.5f},   {20.0f, 6.0f},   {8.0f, 6.5f},    {0.0f, 5.5f}};
const float kBearHead[11][2] = {{-3.0f, 7.0f},  {-4.5f, -2.0f}, {-1.5f, -8.0f}, {4.0f, -9.5f},
                                {8.5f, -6.5f},  {16.0f, -4.2f}, {18.5f, -1.5f}, {17.5f, 1.5f},
                                {15.0f, 3.0f},  {8.0f, 5.5f},   {2.0f, 9.0f}};

// Bear legs: thick tapered columns with a paw. Standing up straightens and
// lengthens the hind legs.
Vec2 bearLeg(Prims& ps, Vec2 root, float a, bool hind, float L, float lift, float near, float up) {
  const float fold = 0.06f * lift;
  if (hind)
    return limb(ps, root, a - 0.3f + 0.25f * up, 0.55f * L, a + 0.15f - 0.1f * up - fold, 0.5f * L,
         (14.f + 6.f * up) * near, (9.5f + 3.5f * up) * near, (8.f + 2.f * up) * near, 3.5f + 1.5f * up);
  return limb(ps, root, a + 0.04f, 0.55f * L, a - 0.04f - fold, 0.47f * L, 11.5f * near, 8.5f * near, 7.5f * near,
              3.5f);
}

void buildBear(const Critter& c, const Look& L, Prims& ps) {
  (void)c;
  const CritterPose& P = L.p;
  const float th = P.pitch + P.rear * kRearAng[CR_BEAR] + P.climb * kHalfPi;
  const float fold = 1.f - 0.35f * clamp01(P.crouch);
  const float up = clamp01(P.rear);
  const float Lh = 28.f * fold * (1.f + 0.5f * up);
  const float Lf = 28.f * fold;
  // The body pivots about a point in the middle of the rump, so a standing
  // bear's weight sits over its hind legs.
  const float pr = cosR(th), ps_ = sinR(th);
  const Vec2 pv{4.f, -10.f};
  const Vec2 q{-16.f, -27.f * fold};
  const Xf B{Vec2{q.x - (pv.x * pr + pv.y * ps_), q.y - (-pv.x * ps_ + pv.y * pr)}, pr, ps_};
  bearLeg(ps, at(B, 6.0f, -8.0f - 7.f * up), P.hind[1], true, Lh, L.lift_h[1], 0.92f, up);
  bearLeg(ps, at(B, 24.0f, -9.0f), P.fore[1], false, Lf, L.lift_f[1], 0.92f, up);
  polyIn(ps, B, kBearBody, 16, true);
  ps.belly = maxX(ps.v[ps.n - 1]);
  const Xf H = xf(at(B, 36.0f + P.look, -15.0f), th + P.head - 0.35f);
  polyIn(ps, H, kBearHead, 11, true);
  ellipse(ps, at(H, 0.6f, -8.6f), 3.7f, 3.7f);
  ps.head = at(H, 4.0f, -9.5f);
  ps.has_head = true;
  ps.mouth = at(H, 16.5f, 1.5f);
  ps.has_mouth = true;
  bearLeg(ps, at(B, 1.0f, -8.0f + 3.f * up), P.hind[0], true, Lh, L.lift_h[0], 1.f, up);
  ps.paw = bearLeg(ps, at(B, 29.0f, -9.0f), P.fore[0], false, Lf, L.lift_f[0], 1.f, up);
  ps.has_paw = true;
}

// ---- frog -------------------------------------------------------------------

// Seated frog: body sloping up to a flat head, eye bump on top, front leg
// propping the chest, the big folded hind leg as a knee on the flank and a
// long foot behind. Geometry is in units scaled by kFrogK.
constexpr float kFrogK = 1.25f;
const float kFrogBody[11][2] = {{-1.5f, -0.5f}, {-2.0f, -2.4f}, {0.5f, -4.2f}, {4.5f, -5.4f},
                                {7.5f, -6.0f},  {10.0f, -5.6f}, {12.0f, -4.0f}, {12.0f, -2.8f},
                                {9.0f, -2.2f},  {5.0f, -1.6f},  {1.0f, -0.4f}};

void buildFrog(const Critter& c, const Look& L, Prims& ps) {
  (void)c;
  const CritterPose& P = L.p;
  const float K = kFrogK;
  const float fold = 1.f - 0.5f * clamp01(P.crouch);
  const Xf B0 = xf(Vec2{-5.0f * K, -3.4f * K * fold}, P.pitch + 0.1f);
  const Xf B{B0.o, B0.c * K, B0.s * K};   // scaled frame
  const float tr = 1.4f + 0.5f * L.throat;
  ellipse(ps, at(B, 10.0f, -2.2f), tr * K, tr * K);
  polyIn(ps, B, kFrogBody, 11, true);
  const float er = 2.1f * K;
  ellipse(ps, at(B, 8.0f, -6.4f + 1.2f * (1.f - L.eye)), er, maxf(0.6f, er * (0.45f + 0.55f * L.eye)));
  leg(ps, at(B, 9.6f, -2.6f), P.fore[0], 4.6f * K * fold, 1.6f * K, 0.f);
  // hind[0] is the thigh, hind[1] the shin-and-foot: a Z under the body with
  // the knee forward and the long foot back past the rump.
  const Vec2 hip = at(B, 0.6f, -1.8f);
  const Vec2 knee = add(hip, mul(dirA(P.hind[0]), 5.6f * K));
  cap(ps, hip, knee, 3.8f * K);
  contact(ps, knee.y + 1.9f * K);
  leg(ps, knee, P.hind[1], 8.2f * K, 2.0f * K, 0.f);
}

// ---- owl (front view, perched) -------------------------------------------------

// One egg-and-shoulders outline for body and head, the tufts as a second
// outline that slides round the crown as the head turns, two eye discs with
// pupils, and a small beak.
const float kOwlBody[14][2] = {{0.0f, 1.5f},   {-2.6f, -0.2f}, {-6.0f, -4.0f}, {-7.2f, -9.5f},
                               {-6.8f, -15.0f}, {-6.2f, -19.0f}, {-3.5f, -21.6f}, {0.0f, -22.0f},
                               {3.5f, -21.6f},  {6.2f, -19.0f},  {6.8f, -15.0f}, {7.2f, -9.5f},
                               {6.0f, -4.0f},   {2.6f, -0.2f}};

void buildOwl(const Critter& c, const Look& L, Prims& ps) {
  (void)c;
  const CritterPose& P = L.p;
  const Xf I = xf(Vec2{0.f, 0.f}, 0.f);
  polyIn(ps, I, kOwlBody, 14, false);
  contact(ps, 0.f);
  const Vec2 hc{0.f, -16.8f};
  const float r = 6.0f;
  const Xf H = xf(hc, -P.head);   // tilt toward facing
  const float yaw = L.yaw;
  {
    // Tufts sit at +-0.12 turn round the crown; seen from behind they swap
    // sides, so the outline takes them left to right.
    const float x0 = r * 0.78f * sinT(yaw - 0.12f), x1 = r * 0.78f * sinT(yaw + 0.12f);
    const float xa = minf(x0, x1), xb = maxf(x0, x1);
    const float mid = 0.5f * (xa + xb);
    const float tuft[6][2] = {{xa - 1.4f, -0.55f * r}, {xa - 1.7f, -1.45f * r}, {mid, -0.78f * r},
                              {xb + 1.7f, -1.45f * r}, {xb + 1.4f, -0.55f * r}, {mid, -0.4f * r}};
    polyIn(ps, H, tuft, 6, false);
  }
  const float er = 2.3f * (P.squash > 0.1f ? 1.12f : 1.f);
  for (int i = 0; i < 2; ++i) {
    const float a = yaw + (i == 0 ? 0.1f : -0.1f);
    const float face = cosT(a);
    if (face < 0.2f) continue;
    const Vec2 e = at(H, 0.95f * r * sinT(a), -0.08f * r);
    const float rx = er * face;
    if (L.eye < 0.3f) {
      cap(ps, Vec2{e.x - rx, e.y + 0.4f}, Vec2{e.x + rx, e.y + 0.4f}, 1.0f, ACTOR_LIGHT);
    } else {
      ellipse(ps, e, rx, er * L.eye, ACTOR_LIGHT);
      if (L.eye > 0.6f) ellipse(ps, Vec2{e.x + 0.5f * face, e.y + 0.1f}, 1.0f * face, 1.0f * L.eye);
    }
  }
  if (cosT(yaw) > 0.3f) {   // beak between the eyes, on the face only
    const float bx = 0.95f * r * sinT(yaw);
    const float beak[3][2] = {{bx - 0.9f, 0.05f * r}, {bx + 0.9f, 0.05f * r}, {bx, 0.42f * r}};
    polyIn(ps, H, beak, 3, false);
  }
}

// ---- bird -------------------------------------------------------------------

const float kBirdBody[9][2] = {{3.6f, -1.0f},  {1.0f, 0.8f},   {-2.4f, 0.2f}, {-5.6f, -1.0f}, {-6.6f, -2.4f},
                               {-5.4f, -3.0f}, {-2.0f, -3.8f}, {2.0f, -4.6f}, {4.2f, -3.4f}};

void buildBird(const Critter& c, const Look& L, Prims& ps) {
  const CritterPose& P = L.p;
  const float fold = 1.f - 0.5f * clamp01(P.crouch);
  const Xf B = xf(Vec2{0.f, -2.6f * fold}, P.pitch);
  const float wing = P.fore[0];
  const bool flying = wing > 0.3f;
  if (!flying || P.lift < 1.f) leg(ps, at(B, 0.2f, 0.f), P.hind[0], 2.6f * fold, 0.9f, 0.f);
  else contact(ps, B.o.y + 0.5f);
  polyIn(ps, B, kBirdBody, 9, true);
  const Xf H = xf(at(B, 3.4f, -3.2f), P.pitch * -0.5f + P.head);
  const Vec2 hc = at(H, 0.9f, -1.3f);
  ellipse(ps, hc, 2.3f, 2.3f);
  const float beak[3][2] = {{2.8f, -1.9f}, {5.6f, -0.9f}, {2.8f, -0.1f}};
  polyIn(ps, H, beak, 3, false);
  if (wing > 0.05f) {   // the V glyph: near wing up and back, far wing up and forward
    const Vec2 sh = at(B, 0.0f, -3.4f);
    const float e = wing - 0.6f;   // elevation above level, radians
    const float rise = 6.5f * sinR(e);   // the V's depth flaps; its width holds
    cap(ps, sh, add(sh, Vec2{-6.5f, -rise - 1.0f}), 1.6f);
    cap(ps, sh, add(sh, Vec2{4.5f, -rise - 0.5f}), 1.5f);
  }
  if (c.carry != CC_NONE) carryItem(ps, c.carry == CC_SHOE ? CC_CRUMB : c.carry, at(H, 4.6f, -0.8f), P.head);
}

// ---- build + finish ----------------------------------------------------------

void build(const Critter& c, Prims& ps) {
  ps.n = 0;
  ps.low = -3.0e38f;
  ps.belly = 0.f;
  ps.has_paw = ps.has_head = ps.has_mouth = false;
  ps.paw = ps.head = ps.mouth = Vec2{0.f, 0.f};
  Look L = compose(c);
  L.p.fore[0] += c.fore_add;   // the caller's offsets, on the drawn pose only
  L.p.head += c.head_add;
  switch (c.kind) {
    case CR_DEER: buildDeer(c, L, ps); break;
    case CR_BEAR: buildBear(c, L, ps); break;
    case CR_FROG: buildFrog(c, L, ps); break;
    case CR_OWL: buildOwl(c, L, ps); break;
    case CR_BIRD: buildBird(c, L, ps); break;
    default: buildQuad(c, L, ps); break;
  }
  // Ground, climb onto the trunk line, squash about the ground, lift, shiver,
  // turn squeeze; then scale and mirror into frame space.
  const CritterPose& P = L.p;
  const float gy = -ps.low;
  const float gx = -clamp01(P.climb) * ps.belly;
  const float ys = 1.f + P.squash, xs = (1.f - 0.5f * P.squash) * L.sx;
  const float S = c.scale * (c.kind == CR_BEAR ? kBearK : 1.f);
  const float fx = (c.facing < 0 ? -S : S);
  auto place = [&](Vec2& v) {
    const float x = (v.x + gx + L.shiver) * xs;
    const float y = (v.y + gy) * ys - P.lift;
    v.x = c.feet.x + fx * x;
    v.y = c.feet.y + S * y;
  };
  if (ps.has_paw) place(ps.paw);
  if (ps.has_head) place(ps.head);
  if (ps.has_mouth) place(ps.mouth);
  for (int k = 0; k < ps.n; ++k) {
    Prim& p = ps.v[k];
    for (int i = 0; i < p.n; ++i) place(p.p[i]);
    if (p.kind == PRIM_ELLIPSE) {
      p.w *= S * xs;
      p.h *= S * ys;
    } else {
      p.w *= S;
    }
  }
}

void primExtent(const Prim& p, float& x0, float& y0, float& x1, float& y1) {
  float px = 1.f, py = 1.f;
  if (p.kind == PRIM_CAP) px = py = 0.5f * p.w + 1.f;
  if (p.kind == PRIM_ELLIPSE) {
    px = p.w * 1.03f + 1.f;
    py = p.h * 1.03f + 1.f;
  }
  x0 = y0 = 3.0e38f;
  x1 = y1 = -3.0e38f;
  for (int i = 0; i < p.n; ++i) {
    x0 = minf(x0, p.p[i].x);
    x1 = maxf(x1, p.p[i].x);
    y0 = minf(y0, p.p[i].y);
    y1 = maxf(y1, p.p[i].y);
  }
  x0 -= px;
  y0 -= py;
  x1 += px;
  y1 += py;
}

int g_last_prims = 0;

void startAction(Critter& c, CritterAction a, float seconds) {
  c.from = compose(c).p;   // the figure on screen, motion included
  c.action = a;
  c.action_t = 0.f;
  c.action_dur = seconds > 0.f ? seconds : 0.f;
  c.loop_w = 0.f;
  c.phase = 0.f;
  c.blend_kind = blendKindFor(a);
  if (seconds <= 0.f) {
    c.pose = kPoses.v[c.kind][a];
    c.blending = false;
  } else {
    c.blending = true;
  }
}

void startTurn(Critter& c) { c.turn_t = 0.f; }

}  // namespace

void critterInit(Critter& c, CritterKind k, Vec2 feet, int facing) {
  if (k >= CR_COUNT) k = CR_CHIPMUNK;
  c.kind = k;
  c.feet = feet;
  c.facing = facing < 0 ? -1 : 1;
  c.scale = 1.f;
  c.visible = true;
  c.speed_dial = 0.5f;
  c.jitter_dial = 0.5f;
  c.carry = CC_NONE;
  c.fore_add = 0.f;
  c.head_add = 0.f;
  c.action = CA_IDLE;
  c.blend_kind = BLEND_PLAIN;
  c.blending = false;
  c.moving = false;
  c.climbing = false;
  c.action_t = 0.f;
  c.action_dur = 0.f;
  c.pose = kPoses.v[k][CA_IDLE];
  c.from = c.pose;
  c.loop_w = 0.f;
  c.gait_w = 0.f;
  c.phase = 0.f;
  c.idle_phase = 0.f;
  c.move_to = feet.x;
  c.move_from = feet.x;
  c.move_speed = 0.f;
  c.turn_t = -1.f;
}

bool critterSupports(CritterKind k, CritterAction a) {
  if (k >= CR_COUNT || a >= CA_COUNT) return false;
  return (kKinds[k].acts & bit(a)) != 0;
}

bool critterActionLoops(CritterAction a) {
  return a == CA_IDLE || a == CA_MOVE || a == CA_RUN || a == CA_BLINK || a == CA_EAT || a == CA_SNIFF ||
         a == CA_SHAKE;
}

void critterSetAction(Critter& c, CritterAction a, float seconds) {
  if (!critterSupports(c.kind, a)) a = CA_IDLE;
  const bool keepMoving = c.moving && isGait(a);
  const float phase = c.phase;
  startAction(c, a, seconds);
  if (keepMoving) c.phase = phase;   // a gait change mid-move keeps the legs where they are
  if (!keepMoving) {
    c.moving = false;
    c.gait_w = 0.f;
  }
  if (a != CA_CLIMB) c.climbing = false;
}

void critterMoveTo(Critter& c, float x, float px_per_s, bool back) {
  if (!(px_per_s > 0.f) || c.kind == CR_OWL) return;
  CritterAction g = kKinds[c.kind].gait;
  if (isGait(c.action) && critterSupports(c.kind, c.action)) g = c.action;
  c.move_to = x;
  c.move_from = c.feet.x;
  c.move_speed = px_per_s * (0.8f + 0.4f * c.speed_dial);
  if (x == c.feet.x) return;
  const int dir = x > c.feet.x ? 1 : -1;
  const int heading = c.turn_t >= 0.f && c.turn_t < 0.5f * kTurnDur ? -c.facing : c.facing;
  if (dir != heading && !back) {
    if (c.turn_t >= 0.f) c.turn_t = kTurnDur - c.turn_t;   // reverse a turn in progress
    else startTurn(c);
  }
  if (c.moving && c.action == g) return;   // retarget mid-move: keep the stride
  const float phase = c.phase;
  const bool was_moving = c.moving;
  startAction(c, g, kGaitIn);
  if (was_moving) c.phase = phase;
  c.climbing = false;
  c.moving = true;
  c.gait_w = 0.f;
}

void critterClimbTo(Critter& c, float y, float px_per_s) {
  if (!(px_per_s > 0.f) || !critterSupports(c.kind, CA_CLIMB)) return;
  c.move_to = y;
  c.move_speed = px_per_s * (0.8f + 0.4f * c.speed_dial);
  if (c.action != CA_CLIMB) {
    startAction(c, CA_CLIMB, 0.4f);
    c.moving = false;
  }
  if (y == c.feet.y) return;
  if (!c.climbing) c.gait_w = 0.f;
  c.climbing = true;
}

bool critterArrived(const Critter& c) { return !c.moving && !c.climbing; }

void critterTick(Critter& c, float dt) {
  if (dt < 0.f) dt = 0.f;
  c.idle_phase = wrap1(c.idle_phase + dt * 0.13f);
  const float t0 = c.action_t;
  c.action_t += dt;

  // Turn-around: facing flips at the narrowest point.
  if (c.turn_t >= 0.f && dt > 0.f) {
    const float a = c.turn_t;
    c.turn_t += dt;
    if (a < 0.5f * kTurnDur && c.turn_t >= 0.5f * kTurnDur) c.facing = -c.facing;
    if (c.turn_t >= kTurnDur) c.turn_t = -1.f;
  }
  if (c.kind == CR_BEAR && c.action == CA_STARTLE && c.action_dur > 0.f) {
    const float at_t = kBearTurnAt * c.action_dur;
    if (t0 < at_t && c.action_t >= at_t) startTurn(c);
  }

  // Base pose: the blend, or the table entry exactly.
  const CritterPose& def = kPoses.v[c.kind][c.action];
  const bool settled = !c.blending;
  if (c.blending) {
    if (c.action_t + kDoneEps >= c.action_dur) {
      c.blending = false;
      c.pose = def;
    } else {
      c.pose = mix(c.from, def, blendCurve(c.blend_kind, c.action_t / c.action_dur));
    }
  } else {
    c.pose = def;
  }

  // One-shots run on the action clock; loops fade in after the blend.
  if (oneShot(c.action) && !c.moving) {
    c.phase = c.action_dur > 0.f ? minf(1.f, c.action_t / c.action_dur) : 1.f;
    if (c.action_t + kDoneEps >= c.action_dur) c.phase = 1.f;
  } else if (!c.moving && !c.climbing) {
    const float rate = loopRate(c, c.action);
    if (settled && rate > 0.f && dt > 0.f) {
      c.loop_w = minf(1.f, c.loop_w + dt / kLoopRamp);
      c.phase = wrap1(c.phase + dt * rate);
    }
  }

  if (c.moving) {
    const KindInfo& ki = kKinds[c.kind];
    c.gait_w = minf(1.f, c.gait_w + dt / kGaitIn);
    const float speed = c.move_speed * easeInOut(c.gait_w);
    const float remain = c.move_to - c.feet.x;
    float step = speed * dt;
    if (c.action == CA_HOP) {   // ground time is spent crouching and landing
      const float hop = ki.stride > 0.f ? ki.stride : 10.f;
      c.phase = wrap1(c.phase + speed * dt / hop);
      step = (c.phase > 0.2f && c.phase < 0.8f) ? step / 0.6f : 0.f;
    } else if (c.kind == CR_BIRD && c.action == CA_MOVE) {
      c.phase = wrap1(c.phase + dt * 5.f);
    } else {
      const float stride = c.action == CA_RUN ? ki.run_stride : ki.stride;
      // Backing up (moving against its facing, not turning): the legs step backward.
      const bool backing = c.turn_t < 0.f && remain * (float)c.facing < 0.f;
      if (stride > 0.f) c.phase = wrap1(c.phase + (backing ? -step : step) / stride);
    }
    bool arrived = false;
    if (fabsf(remain) <= step) {
      c.feet.x = c.move_to;
      arrived = true;
    } else if (step > 0.f) {
      c.feet.x += remain > 0.f ? step : -step;
    }
    if (arrived) {
      startAction(c, CA_IDLE, kArriveBlend);
      c.blend_kind = BLEND_LAND;
      c.moving = false;
      c.gait_w = 0.f;
      c.pose = c.from;   // this tick still shows the gait pose; the blend starts from it next tick
    }
  }

  if (c.climbing) {
    const KindInfo& ki = kKinds[c.kind];
    c.gait_w = minf(1.f, c.gait_w + dt / kGaitIn);
    const float step = c.move_speed * easeInOut(c.gait_w) * dt;
    const float remain = c.move_to - c.feet.y;
    if (ki.climb_stride > 0.f) c.phase = wrap1(c.phase + step / ki.climb_stride);
    if (fabsf(remain) <= step) {
      c.feet.y = c.move_to;
      startAction(c, CA_CLIMB, kArriveBlend);
      c.climbing = false;
      c.gait_w = 0.f;
      c.pose = c.from;   // as above: no one-tick pop to the neutral pose
    } else if (step > 0.f) {
      c.feet.y += remain > 0.f ? step : -step;
    }
  }
}

Rect critterBounds(const Critter& c) {
  Prims ps;
  build(c, ps);
  if (ps.n == 0) return Rect{0, 0, 0, 0};
  float bx0 = 3.0e38f, by0 = 3.0e38f, bx1 = -3.0e38f, by1 = -3.0e38f;
  for (int k = 0; k < ps.n; ++k) {
    float x0, y0, x1, y1;
    primExtent(ps.v[k], x0, y0, x1, y1);
    bx0 = minf(bx0, x0);
    by0 = minf(by0, y0);
    bx1 = maxf(bx1, x1);
    by1 = maxf(by1, y1);
  }
  return Rect{(int)floorf(bx0) - 2, (int)floorf(by0) - 2, (int)ceilf(bx1) + 2, (int)ceilf(by1) + 2};
}

Vec2 critterPaw(const Critter& c) {
  Prims ps;
  build(c, ps);
  if (ps.has_paw) return ps.paw;
  return c.feet;
}

Vec2 critterMouth(const Critter& c) {
  Prims ps;
  build(c, ps);
  if (ps.has_mouth) return ps.mouth;
  return critterHead(c);
}

float critterTurnScale(const Critter& c) { return turnScale(c.turn_t); }

Vec2 critterHead(const Critter& c) {
  Prims ps;
  build(c, ps);
  if (ps.has_head) return ps.head;
  const Rect r = critterBounds(c);
  return Vec2{0.5f * (float)(r.x0 + r.x1), (float)r.y0 + 2.f};
}

void critterDraw(const Critter& c, Surface& s, const Palette& pal, const Rect& clip) {
  g_last_prims = 0;
  if (!c.visible) return;
  Prims ps;
  build(c, ps);
  for (int k = 0; k < ps.n; ++k) {
    const Prim& p = ps.v[k];
    float x0, y0, x1, y1;
    primExtent(p, x0, y0, x1, y1);
    if (x1 < (float)clip.x0 || x0 > (float)clip.x1 || y1 < (float)clip.y0 || y0 > (float)clip.y1) continue;
    const Pixel col = pal[p.slot];
    switch (p.kind) {
      case PRIM_CAP: strokeCapsule(s, p.p[0], p.p[1], p.w, col, clip); break;
      case PRIM_ELLIPSE:
        if (p.w == p.h) fillCircle(s, p.p[0], p.w, col, clip);
        else fillEllipse(s, p.p[0], p.w, p.h, col, clip);
        break;
      default: fillPolygon(s, p.p, p.n, col, clip); break;
    }
    ++g_last_prims;
  }
}

const CritterPose& critterPoseDef(CritterKind k, CritterAction a) {
  if (k >= CR_COUNT) k = CR_CHIPMUNK;
  if (a >= CA_COUNT) a = CA_IDLE;
  return kPoses.v[k][a];
}
const char* critterName(CritterKind k) { return k < CR_COUNT ? kKinds[k].name : "?"; }
const char* critterActionName(CritterAction a) { return a < CA_COUNT ? kActionNames[a] : "?"; }
int critterLastDrawPrimitives() { return g_last_prims; }
int critterBudget(CritterKind k) { return k < CR_COUNT ? kKinds[k].budget : 7; }
float critterHeight(CritterKind k) { return k < CR_COUNT ? kKinds[k].height : 0.f; }

}  // namespace ss
