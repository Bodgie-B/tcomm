// Hiker rig. See hiker.h.
//
// One code path builds the figure as a short list of primitives in frame
// space; hikerDraw rasterizes that list and hikerBounds folds its extents, so
// the bounds can never disagree with what is drawn. The list is built in a
// local frame (x forward, y down, hips at the origin), grounded, then mirrored
// about feet.x for facing -1.
#include "hiker.h"

#include <math.h>

#include "../anim/ease.h"

namespace ss {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kInvTwoPi = 1.f / (2.f * kPi);

// ---- rig dimensions, in units of height -----------------------------------
constexpr float kThigh = 0.235f, kShin = 0.235f;
constexpr float kUpper = 0.16f, kFore = 0.15f;
constexpr float kTorsoTop = 0.235f;   // torso capsule axis, hip to chest
constexpr float kShoulder = 0.25f;    // shoulder joint up the torso axis
constexpr float kNeck = 0.29f;
constexpr float kHeadUp = 0.06f, kHeadFwd = 0.012f, kHeadR = 0.062f;
constexpr float kTorsoW = 0.15f, kThighW = 0.095f, kShinW = 0.075f, kFootW = 0.058f;
constexpr float kUpperW = 0.065f, kForeW = 0.055f;
constexpr float kHeel = 0.02f, kToe = 0.07f;
constexpr float kFootFollow = 0.75f;  // how much the foot pitches with the shin (flat in stance, toes up lying)

// Hat: brim is a capsule across the head, crown a dented trapezoid on top.
constexpr float kBrimUp = 0.035f, kBrimBack = 0.095f, kBrimFront = 0.14f, kBrimW = 0.035f;
constexpr float kCrownBase = 0.068f, kCrownTop = 0.056f, kCrownH = 0.08f, kCrownDent = 0.012f;

// Pack outline as (behind the spine, up the spine) from the hips. The back edge
// is straight so a rim light can sit on it; the top stands above the shoulders
// but well under the brim, so hat and pack stay two separate shapes.
constexpr float kPack[8][2] = {{0.035f, 0.045f}, {0.13f, 0.035f}, {0.175f, 0.06f}, {0.19f, 0.10f},
                               {0.19f, 0.27f},   {0.17f, 0.30f},  {0.10f, 0.32f}, {0.04f, 0.31f}};
constexpr float kPackBack = 0.19f;
// Bedroll strapped across the top, so seen end-on: a disc at the back corner,
// clear of the brim.
constexpr float kRollB = 0.15f, kRollU = 0.315f, kRollR = 0.05f;

constexpr float kRimW = 0.018f, kRimMinPx = 1.2f;

// The rod: off the near forearm by kRodRel (so a raised arm raises it), its
// butt kRodButt behind the near hand; the outer kRodOuter of it bows with
// rod_bend by up to kRodBow radians. The stick is thicker and kinked.
constexpr float kRodRel = 0.55f, kRodButt = 0.13f, kRodOuter = 0.45f, kRodBow = 0.75f;
constexpr float kRodW = 0.015f, kStickW = 0.03f, kLineW = 1.0f, kFloatR = 1.7f;

// ---- motion ---------------------------------------------------------------
constexpr float kBreathPeriod = 3.6f;    // seconds per breath
constexpr float kBreathAmp = 0.011f;     // chest rise, about 1 px at 90 px
constexpr float kPackBob = 0.012f;       // pack bounce while walking, out of phase with the hips
constexpr float kWalkIn = 0.3f;          // seconds to blend into the walk
constexpr float kArriveBlend = 0.3f;     // seconds to settle into the rest pose on arrival
constexpr float kIdleRamp = 0.5f;        // seconds for an idle motion (wave) to fade in
constexpr float kAnticT = 0.15f, kAnticAmt = 0.12f;   // wind-up share of the blend and its depth
constexpr float kDoneEps = 1.0e-4f;      // blend timer slack so dt sums land on the end tick

enum BlendKind : uint8_t { BLEND_PLAIN, BLEND_ACTION, BLEND_LAND };

// Pose table. Angles absolute from straight down, + forward; see hiker.h.
//                 lean    head   lift   dx      thigh          knee           shoulder       elbow
const HikerPose kPoses[POSE_COUNT] = {
    /* STAND */  { 0.03f,  0.00f, 0.f,   0.f,   { 0.06f,-0.06f},{0.04f,0.04f}, { 0.10f,-0.12f},{0.18f,0.22f}},
    /* LOOK  */  { 0.00f, -0.12f, 0.f,   0.f,   { 0.10f,-0.10f},{0.04f,0.04f}, { 1.60f,-0.10f},{2.18f,0.20f}},
    /* CROUCH */ { 0.45f, -0.25f, 0.f,  -0.05f, { 1.95f, 1.75f},{2.25f,2.00f}, { 0.50f, 0.30f},{0.20f,0.30f}},
    /* SIT */    {-0.12f,  0.10f, 0.f,   0.f,   { 2.25f, 2.10f},{2.87f,2.90f}, { 0.60f, 0.55f},{0.87f,0.90f}},
    /* REACH */  {-0.06f, -0.45f, 0.015f,0.f,   { 0.03f,-0.03f},{0.00f,0.00f}, { 3.00f, 2.75f},{0.05f,0.25f}},
    /* CARRY */  {-0.08f,  0.05f, 0.f,   0.f,   { 0.06f,-0.06f},{0.05f,0.05f}, { 0.35f, 0.25f},{1.25f,1.35f}},
    /* BLOW */   { 1.05f, -0.35f, 0.f,   0.f,   { 0.15f, 1.55f},{1.75f,1.60f}, { 0.60f, 0.40f},{0.90f,0.80f}},
    /* YELP */   {-0.22f, -0.30f, 0.07f, 0.f,   { 0.45f,-0.25f},{0.90f,0.70f}, { 2.50f, 2.10f},{0.50f,0.70f}},
    /* LIE */    {-1.32f,  0.55f, 0.f,   0.f,   { 1.50f, 1.95f},{0.05f,1.10f}, { 3.90f, 3.70f},{2.30f,2.20f}},
    /* WAVE */   {-0.03f, -0.10f, 0.f,   0.f,   { 0.06f,-0.06f},{0.04f,0.04f}, { 2.20f,-0.10f},{0.95f,0.20f}},
    /* HIPS */   {-0.06f, -0.08f, 0.f,   0.f,   { 0.12f,-0.12f},{0.03f,0.03f}, {-0.75f, 0.95f},{2.00f,-2.30f}},
    /* SCRATCH */{ 0.02f,  0.20f, 0.f,   0.f,   { 0.06f,-0.06f},{0.04f,0.04f}, { 2.40f,-0.10f},{2.43f,0.20f}},
    /* MUG */    { 0.00f,  0.05f, 0.f,   0.f,   { 0.06f,-0.06f},{0.04f,0.04f}, { 0.10f,-0.15f},{2.20f,0.50f}},
    /* READ */   {-0.05f,  0.45f, 0.f,   0.f,   { 2.00f, 1.85f},{2.95f,3.00f}, { 0.45f, 0.40f},{1.52f,1.55f}},
    /* FISH_ST */{ 0.04f,  0.12f, 0.f,   0.f,   { 0.10f,-0.10f},{0.06f,0.06f}, { 0.75f, 0.35f},{0.90f,0.90f}},
    /* FISH_SIT*/{-0.05f,  0.15f, 0.f,   0.f,   { 2.25f, 2.10f},{2.87f,2.90f}, { 0.90f, 0.55f},{0.75f,1.00f}},
    /* CAST */   {-0.14f, -0.12f, 0.f,   0.f,   { 0.16f,-0.14f},{0.06f,0.08f}, { 2.70f, 2.40f},{0.35f,0.50f}},
    /* HUG */    { 0.12f,  0.22f, 0.f,   0.f,   { 0.08f,-0.04f},{0.05f,0.05f}, { 0.22f, 0.14f},{2.45f,2.55f}},
};

const char* const kPoseNames[POSE_COUNT] = {
    "stand", "look_around", "crouch", "sit", "reach_up", "carry", "blow",
    "yelp", "lie", "wave", "hands_on_hips", "scratch_head", "hold_mug", "read", "fish_stand", "fish_sit", "cast", "hug"};

BlendKind blendKindFor(PoseId p) {
  switch (p) {
    case POSE_REACH_UP: case POSE_YELP: case POSE_WAVE: case POSE_CAST: return BLEND_ACTION;
    case POSE_SIT: case POSE_CROUCH: case POSE_LIE: case POSE_FISH_SIT: return BLEND_LAND;
    default: return BLEND_PLAIN;
  }
}

// Blend progress for t in 0..1. Action poses wind up against the move first;
// landing poses overshoot a little and settle.
float blendCurve(uint8_t kind, float t) {
  if (kind == BLEND_ACTION) {
    if (t < kAnticT) return -kAnticAmt * easeOutCubic(t / kAnticT);
    return lerp(-kAnticAmt, 1.f, easeInOutCubic((t - kAnticT) / (1.f - kAnticT)));
  }
  if (kind == BLEND_LAND) return easeOutBack(easeInOut(t));   // eased start, ~10% past, settles at rest
  return easeInOutCubic(t);
}

HikerPose mix(const HikerPose& a, const HikerPose& b, float t) {
  HikerPose r;
  r.lean = lerp(a.lean, b.lean, t);
  r.head = lerp(a.head, b.head, t);
  r.lift = lerp(a.lift, b.lift, t);
  r.dx = lerp(a.dx, b.dx, t);
  for (int i = 0; i < 2; ++i) {
    r.thigh[i] = lerp(a.thigh[i], b.thigh[i], t);
    r.knee[i] = lerp(a.knee[i], b.knee[i], t);
    r.shoulder[i] = lerp(a.shoulder[i], b.shoulder[i], t);
    r.elbow[i] = lerp(a.elbow[i], b.elbow[i], t);
  }
  return r;
}

bool groundPose(PoseId p) {
  return p == POSE_SIT || p == POSE_LIE || p == POSE_CROUCH || p == POSE_BLOW || p == POSE_READ || p == POSE_FISH_SIT;
}

// Small looping motion layered on a held pose; rate in Hz.
float idleRate(PoseId p) {
  switch (p) {
    case POSE_WAVE: return 1.6f;
    case POSE_SCRATCH_HEAD: return 3.f;
    case POSE_BLOW: return 0.7f;
    default: return 0.f;
  }
}

void addIdle(HikerPose& q, PoseId p, float w, float phase) {
  if (w <= 0.f) return;
  const float s = sinT(phase);
  switch (p) {
    case POSE_WAVE: q.elbow[0] += w * 0.45f * s; q.shoulder[0] += w * 0.08f * s; break;
    case POSE_SCRATCH_HEAD: q.elbow[0] += w * 0.18f * s; break;
    case POSE_BLOW: q.lean += w * 0.05f * s; q.head -= w * 0.06f * s; break;   // puffs
    default: break;
  }
}

// Stride amplitude (thigh swing, radians) for a speed: faster walks stride longer.
float strideAmp(float speed, float height) {
  float a = 0.12f + 0.45f * speed / (height > 1.f ? height : 1.f);
  return a < 0.18f ? 0.18f : (a > 0.5f ? 0.5f : a);
}

HikerPose walkPose(const HikerPose& base, float phase, float amp, bool arms_free) {
  HikerPose w = base;
  w.lean = 0.07f;
  w.head = -0.02f;
  w.lift = 0.f;
  w.dx = 0.f;
  const float s = sinT(phase), c = cosT(phase);
  w.thigh[0] = 0.04f + amp * s;
  w.thigh[1] = 0.04f - amp * s;
  // The leg swinging forward (thigh angle rising) lifts its foot by bending.
  w.knee[0] = 0.1f + 2.2f * amp * (c > 0.f ? c : 0.f);
  w.knee[1] = 0.1f + 2.2f * amp * (c < 0.f ? -c : 0.f);
  if (arms_free) {
    w.shoulder[0] = -0.85f * amp * s;
    w.shoulder[1] = 0.85f * amp * s;
    w.elbow[0] = 0.25f + (w.shoulder[0] > 0.f ? 1.2f * w.shoulder[0] : 0.f);
    w.elbow[1] = 0.25f + (w.shoulder[1] > 0.f ? 1.2f * w.shoulder[1] : 0.f);
  }
  return w;
}

void startBlend(Hiker& h, PoseId p, float seconds, uint8_t kind) {
  h.from = h.pose;
  h.target = p;
  h.idle_w = 0.f;
  if (seconds <= 0.f) {
    h.pose = kPoses[p];
    h.blending = false;
    return;
  }
  h.blending = true;
  h.blend_kind = kind;
  h.blend_t = 0.f;
  h.blend_dur = seconds;
}

// ---- primitive list --------------------------------------------------------

enum PrimKind : uint8_t { PRIM_CAP, PRIM_CIRCLE, PRIM_POLY };

constexpr int kMaxPrims = 24;
constexpr int kMaxPolyPts = 8;

struct Prim {
  uint8_t kind;
  Slot slot;
  uint8_t n;     // points used
  float w;       // capsule width or circle radius
  Vec2 p[kMaxPolyPts];
};

struct Prims {
  int n;
  Prim v[kMaxPrims];
};

inline Vec2 add(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
inline Vec2 sub(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }
inline Vec2 mul(Vec2 a, float k) { return Vec2{a.x * k, a.y * k}; }
inline Vec2 dirA(float a) { return Vec2{sinT(a * kInvTwoPi), cosT(a * kInvTwoPi)}; }   // from straight down, + forward
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

Prim* push(Prims& ps, PrimKind k, Slot slot) {
  if (ps.n >= kMaxPrims) return nullptr;   // cannot happen: the rig issues at most 22
  Prim* p = &ps.v[ps.n++];
  p->kind = k;
  p->slot = slot;
  p->n = 0;
  p->w = 0.f;
  return p;
}
void cap(Prims& ps, Vec2 a, Vec2 b, float w, Slot slot = ACTOR) {
  if (Prim* p = push(ps, PRIM_CAP, slot)) { p->p[0] = a; p->p[1] = b; p->n = 2; p->w = w; }
}
void circle(Prims& ps, Vec2 c, float r, Slot slot = ACTOR) {
  if (Prim* p = push(ps, PRIM_CIRCLE, slot)) { p->p[0] = c; p->n = 1; p->w = r; }
}
void poly(Prims& ps, const Vec2* pts, int n) {
  if (Prim* p = push(ps, PRIM_POLY, ACTOR)) {
    p->n = (uint8_t)(n > kMaxPolyPts ? kMaxPolyPts : n);
    for (int i = 0; i < p->n; ++i) p->p[i] = pts[i];
  }
}
// Polygon from offsets (in units of height) around an anchor.
void polyAt(Prims& ps, Vec2 at, const float (*off)[2], int n, float H) {
  Vec2 pts[kMaxPolyPts];
  if (n > kMaxPolyPts) n = kMaxPolyPts;
  for (int i = 0; i < n; ++i) pts[i] = Vec2{at.x + off[i][0] * H, at.y + off[i][1] * H};
  poly(ps, pts, n);
}

// Carried props, anchored at the near hand (both hands for wood), local frame.
void buildCarry(Prims& ps, CarryId c, Vec2 hand0, Vec2 hand1, float H, float rim) {
  switch (c) {
    case CARRY_WOOD: {   // three logs resting on the forearms
      const Vec2 m = Vec2{0.5f * (hand0.x + hand1.x), 0.5f * (hand0.y + hand1.y) - 0.03f * H};
      const float w = 0.045f * H, l = 0.105f * H;
      cap(ps, Vec2{m.x - l, m.y + 0.008f * H}, Vec2{m.x + l, m.y - 0.004f * H}, w);
      cap(ps, Vec2{m.x - l + 0.012f * H, m.y - 0.04f * H}, Vec2{m.x + l + 0.01f * H, m.y - 0.038f * H}, w);
      cap(ps, Vec2{m.x - l - 0.01f * H, m.y - 0.074f * H}, Vec2{m.x + l - 0.02f * H, m.y - 0.082f * H}, w);
      break;
    }
    case CARRY_MUG: {
      static const float body[4][2] = {{-0.008f, -0.068f}, {0.05f, -0.068f}, {0.048f, 0.004f}, {-0.006f, 0.004f}};
      polyAt(ps, hand0, body, 4, H);
      cap(ps, Vec2{hand0.x - 0.02f * H, hand0.y - 0.052f * H}, Vec2{hand0.x - 0.02f * H, hand0.y - 0.018f * H},
          maxf(0.02f * H, 1.2f));
      break;
    }
    case CARRY_BOOK: {   // open book tipped toward the face
      static const float book[5][2] = {{-0.02f, 0.f}, {0.045f, 0.012f}, {0.11f, -0.01f}, {0.095f, -0.085f}, {-0.03f, -0.07f}};
      polyAt(ps, hand0, book, 5, H);
      break;
    }
    case CARRY_BAG: {   // food sack hanging from a gathered neck
      static const float sack[8][2] = {{-0.02f, 0.03f},  {0.02f, 0.03f},   {0.06f, 0.08f},  {0.065f, 0.13f},
                                       {0.035f, 0.165f}, {-0.035f, 0.165f}, {-0.065f, 0.13f}, {-0.06f, 0.08f}};
      cap(ps, hand0, Vec2{hand0.x, hand0.y + 0.04f * H}, 0.03f * H);
      polyAt(ps, hand0, sack, 8, H);
      break;
    }
    case CARRY_STICK: {   // angled down ahead (poking, roasting); held level it reads as a rifle
      const Vec2 d{0.77f, 0.64f};
      cap(ps, sub(hand0, mul(d, 0.1f * H)), add(hand0, mul(d, 0.4f * H)), maxf(0.026f * H, 1.2f));
      break;
    }
    case CARRY_ROCK: {
      static const float rock[6][2] = {{-0.055f, 0.005f}, {-0.035f, -0.045f}, {0.02f, -0.058f},
                                       {0.062f, -0.02f}, {0.05f, 0.028f}, {-0.012f, 0.038f}};
      polyAt(ps, Vec2{hand0.x + 0.01f * H, hand0.y - 0.01f * H}, rock, 6, H);
      break;
    }
    case CARRY_JAR: {   // glass jar; the lid line is the one lit detail
      static const float jar[4][2] = {{-0.02f, -0.095f}, {0.05f, -0.095f}, {0.05f, 0.006f}, {-0.02f, 0.006f}};
      polyAt(ps, hand0, jar, 4, H);
      const float y = hand0.y - 0.084f * H;
      cap(ps, Vec2{hand0.x - 0.008f * H, y}, Vec2{hand0.x + 0.038f * H, y}, maxf(0.016f * H, rim), ACTOR_LIGHT);
      break;
    }
    default: break;
  }
}

// The rod in the local frame: from its butt behind the near hand, out along
// the near forearm plus kRodRel, bowed down at the outer end by rod_bend; the
// line from the tip to rod_end (given in the local frame) and its float.
// Returns the tip.
Vec2 buildRod(const Hiker& h, Prims& ps, Vec2 hand0, float forearm, Vec2 end_local) {
  const float H = h.height;
  const float a = forearm + kRodRel + h.rod_tilt;
  const Vec2 d = dirA(a);
  const float L = h.rod_len * H;
  const Vec2 butt = sub(hand0, mul(d, kRodButt * H));
  const Slot slot = h.rod_stick ? TRUNK : ACTOR;
  const float w = maxf((h.rod_stick ? kStickW : kRodW) * H, h.rod_stick ? 2.f : 1.2f);
  // Inner part straight to the knee of the bow; the outer part turned down.
  const Vec2 knee = add(hand0, mul(d, (1.f - kRodOuter) * L));
  // A stick has a kink a little way out whether or not anything pulls on it.
  const float bow = kRodBow * h.rod_bend + (h.rod_stick ? 0.22f : 0.f);
  const Vec2 tip = add(knee, mul(dirA(a - bow), kRodOuter * L));
  // Straight, it is one capsule. Bowed (or a stick) it is two, and then no
  // float is drawn: a bowed rod has something on the line that pulled the
  // float under, and a stick has a bare string. That keeps the rig at 22
  // primitives at most.
  const bool bowed = bow > 0.f;
  if (bowed) {
    cap(ps, butt, knee, w, slot);
    cap(ps, knee, tip, h.rod_stick ? w : maxf(0.8f * w, 1.f), slot);
  } else {
    cap(ps, butt, tip, w, slot);
  }
  if (h.rod_line) {
    cap(ps, tip, end_local, kLineW, ACTOR);
    if (h.rod_float && !bowed) circle(ps, end_local, kFloatR, FIRE);
  }
  return tip;
}

// Build the figure for the current state into frame-space primitives (and
// the rod tip, frame space, if asked).
// Where the fire-side rim looks for the silhouette's edge (frame space).
struct RimAxis {
  int side;            // screen direction toward the fire: -1 left, +1 right, 0 no fire
  Vec2 hip, chest, head;
  float head_r, reach, width;
};

void build(const Hiker& h, Prims& ps, Vec2* rod_tip = nullptr, RimAxis* rim_out = nullptr) {
  ps.n = 0;
  const float H = h.height;
  const HikerPose& P = h.pose;
  const float still = 1.f - h.walk_w;
  const float breath = still * kBreathAmp * H * (0.5f + 0.5f * sinT(h.breath_phase));
  const float bob = h.walk_w * kPackBob * H * sinT(2.f * h.walk_phase - 0.2f);

  // Local frame: hips at (dx, 0), y down; grounded below.
  const Vec2 hip{P.dx * H, 0.f};
  const float lt = P.lean * kInvTwoPi;
  const Vec2 up{sinT(lt), -cosT(lt)};
  const Vec2 bk{-cosT(lt), -sinT(lt)};   // behind the spine
  const Vec2 chest = add(hip, mul(up, kTorsoTop * H + breath));
  const Vec2 shoulder = add(hip, mul(up, kShoulder * H + breath));
  const Vec2 neck = add(hip, mul(up, kNeck * H + breath));
  const float ht = (P.lean + P.head) * kInvTwoPi;
  const Vec2 hu{sinT(ht), -cosT(ht)};
  const Vec2 hf{cosT(ht), sinT(ht)};
  const Vec2 head = add(neck, add(mul(hu, kHeadUp * H), mul(hf, kHeadFwd * H)));
  const float headR = kHeadR * H;
  const float torsoR = 0.5f * kTorsoW * H;

  Vec2 knee[2], ankle[2], heel[2], toe[2], elbow[2], hand[2];
  for (int i = 0; i < 2; ++i) {
    knee[i] = add(hip, mul(dirA(P.thigh[i]), kThigh * H));
    const float shin = P.thigh[i] - P.knee[i];
    ankle[i] = add(knee[i], mul(dirA(shin), kShin * H));
    const Vec2 fd = dirA(0.5f * kPi + kFootFollow * shin);
    heel[i] = sub(ankle[i], mul(fd, kHeel * H));
    toe[i] = add(ankle[i], mul(fd, kToe * H));
    elbow[i] = add(shoulder, mul(dirA(P.shoulder[i]), kUpper * H));
    hand[i] = add(elbow[i], mul(dirA(P.shoulder[i] + P.elbow[i]), kFore * H));
  }

  // Ground: the lowest seat, knee, foot or head contact sits on y = 0. The pack
  // is left out so lying on it reads as reclining on the ground, not floating.
  float low = maxf(hip.y, chest.y) + torsoR;
  for (int i = 0; i < 2; ++i) {
    low = maxf(low, knee[i].y + 0.5f * kShinW * H);
    low = maxf(low, maxf(heel[i].y, maxf(toe[i].y, ankle[i].y)) + 0.5f * kFootW * H);
  }
  low = maxf(low, head.y + headR);
  const float shift = -low - P.lift * H;

  // Pack outline (and its rim edge) in the local frame.
  Vec2 pack[8];
  for (int i = 0; i < 8; ++i)
    pack[i] = add(hip, add(mul(bk, kPack[i][0] * H), mul(up, kPack[i][1] * H + breath + bob)));

  const float rim = maxf(kRimW * H, kRimMinPx);
  const int lit = h.lit_from == 0 ? 0 : (h.lit_from * h.facing > 0 ? 1 : -1);   // +1 front lit, -1 back lit

  // Far limbs, pack, torso, near leg, head, hat, near arm, prop: one color, so
  // order only matters for the light details (an arm crossing the rim covers it).
  cap(ps, shoulder, elbow[1], kUpperW * H);
  cap(ps, elbow[1], hand[1], kForeW * H);
  cap(ps, hip, knee[1], kThighW * H);
  cap(ps, knee[1], ankle[1], kShinW * H);
  cap(ps, heel[1], toe[1], kFootW * H);
  if (h.pack_on) {
    poly(ps, pack, 8);
    circle(ps, add(hip, add(mul(bk, kRollB * H), mul(up, kRollU * H + breath + bob))), kRollR * H);
  }
  cap(ps, hip, chest, kTorsoW * H);
  cap(ps, hip, knee[0], kThighW * H);
  cap(ps, knee[0], ankle[0], kShinW * H);
  cap(ps, heel[0], toe[0], kFootW * H);
  circle(ps, head, headR);
  const Vec2 brim = add(head, mul(hu, kBrimUp * H));
  if (h.hat_on) cap(ps, sub(brim, mul(hf, kBrimBack * H)), add(brim, mul(hf, kBrimFront * H)), kBrimW * H);
  if (h.hat_on) {
    const Vec2 b0 = add(brim, mul(hu, 0.01f * H));
    const Vec2 t0 = add(b0, mul(hu, kCrownH * H));
    const Vec2 crown[5] = {sub(b0, mul(hf, kCrownBase * H)), add(b0, mul(hf, kCrownBase * H)),
                           add(t0, mul(hf, kCrownTop * H)), sub(t0, mul(hu, kCrownDent * H)),
                           sub(t0, mul(hf, kCrownTop * H))};
    poly(ps, crown, 5);
  }
  cap(ps, shoulder, elbow[0], kUpperW * H);
  cap(ps, elbow[0], hand[0], kForeW * H);
  buildCarry(ps, h.carry, hand[0], hand[1], H, rim);

  // To frame space: ground, then mirror about feet.x for facing -1.
  const float sx = h.facing < 0 ? -1.f : 1.f;
  const float oy = h.feet.y + shift;
  Vec2 tip = hand[0];
  if (h.carry == CARRY_ROD) {
    const Vec2 end_local{sx * (h.rod_end.x - h.feet.x), h.rod_end.y - oy};
    tip = buildRod(h, ps, hand[0], P.shoulder[0] + P.elbow[0], end_local);
  }
  if (rod_tip) *rod_tip = Vec2{h.feet.x + sx * tip.x, oy + tip.y};
  if (rim_out) {   // the fire-side rim's guide: the body's axis in frame space (see rimPass)
    RimAxis& r = *rim_out;
    r.side = h.lit_from < 0 ? -1 : (h.lit_from > 0 ? 1 : 0);
    r.hip = Vec2{h.feet.x + sx * hip.x, oy + hip.y + 0.04f * H};
    r.chest = Vec2{h.feet.x + sx * chest.x, oy + chest.y};
    r.head = Vec2{h.feet.x + sx * head.x, oy + head.y};
    r.head_r = headR;
    // From the axis to the lit edge: the torso's half width, or through the
    // pack when it is his back that faces the fire.
    r.reach = (lit < 0 && h.pack_on ? kPackBack * H : torsoR) + 2.5f;
    r.width = rim;
  }
  for (int k = 0; k < ps.n; ++k) {
    Prim& p = ps.v[k];
    for (int i = 0; i < p.n; ++i) {
      p.p[i].x = h.feet.x + sx * p.p[i].x;
      p.p[i].y = oy + p.p[i].y;
    }
  }
}

// Float extents of one primitive, including its antialiased fringe.
void primExtent(const Prim& p, float& x0, float& y0, float& x1, float& y1) {
  const float pad = (p.kind == PRIM_POLY ? 0.f : (p.kind == PRIM_CAP ? 0.5f * p.w : p.w * 1.03f)) + 1.f;
  x0 = y0 = 3.0e38f;
  x1 = y1 = -3.0e38f;
  for (int i = 0; i < p.n; ++i) {
    x0 = minf(x0, p.p[i].x);
    x1 = maxf(x1, p.p[i].x);
    y0 = minf(y0, p.p[i].y);
    y1 = maxf(y1, p.p[i].y);
  }
  x0 -= pad;
  y0 -= pad;
  x1 += pad;
  y1 += pad;
}

int g_last_prims = 0;

}  // namespace

void hikerInit(Hiker& h, Vec2 feet, int facing) {
  h.feet = feet;
  h.facing = facing < 0 ? -1 : 1;
  h.height = 90.f;
  h.visible = true;
  h.pack_on = true;
  h.lit_from = 0;
  h.hat_on = true;
  h.rod_len = 0.75f;
  h.rod_bend = h.rod_tilt = 0.f;
  h.rod_stick = h.rod_line = h.rod_float = false;
  h.rod_end = feet;
  h.pose = kPoses[POSE_STAND];
  h.from = h.pose;
  h.target = POSE_STAND;
  h.blend_kind = BLEND_PLAIN;
  h.blending = false;
  h.blend_t = h.blend_dur = 0.f;
  h.idle_w = 0.f;
  h.idle_phase = 0.f;
  h.walking = false;
  h.walk_x = feet.x;
  h.walk_speed = 0.f;
  h.walk_w = 0.f;
  h.walk_phase = 0.f;
  h.walk_rest = POSE_STAND;
  h.carry = CARRY_NONE;
  h.breath_phase = 0.f;
}

void hikerSetPose(Hiker& h, PoseId p, float blend_seconds) {
  if (p >= POSE_COUNT) return;
  // The composed pose (walk included) is the blend start, so nothing jumps.
  h.walking = false;
  h.walk_w = 0.f;
  startBlend(h, p, blend_seconds, blendKindFor(p));
}

void hikerWalkTo(Hiker& h, float x, float px_per_s) {
  if (!(px_per_s > 0.f)) return;
  h.walk_x = x;
  h.walk_speed = px_per_s;
  if (x == h.feet.x) return;
  h.facing = x > h.feet.x ? 1 : -1;
  if (h.walking) return;   // retarget mid-walk: keep the stride going
  h.walking = true;
  // Empty hands swing; full hands keep their carrying pose for the walk and after.
  PoseId rest = POSE_STAND;
  if (h.carry != CARRY_NONE) rest = groundPose(h.target) ? POSE_CARRY : h.target;
  h.walk_rest = rest;
  if (h.target != rest || h.blending) startBlend(h, rest, kWalkIn, BLEND_PLAIN);
  h.idle_w = 0.f;
}

bool hikerArrived(const Hiker& h) { return !h.walking; }

void hikerCarry(Hiker& h, CarryId c) {
  if (c < CARRY_COUNT) h.carry = c;
}

void hikerTick(Hiker& h, float dt) {
  if (dt < 0.f) dt = 0.f;
  h.breath_phase += dt / kBreathPeriod;
  h.breath_phase -= floorf(h.breath_phase);

  // Base pose: the blend, or the held target exactly.
  const bool settled = !h.blending;
  HikerPose base;
  if (h.blending) {
    h.blend_t += dt;
    if (h.blend_t + kDoneEps >= h.blend_dur) {
      h.blending = false;
      base = kPoses[h.target];
    } else {
      base = mix(h.from, kPoses[h.target], blendCurve(h.blend_kind, h.blend_t / h.blend_dur));
    }
  } else {
    base = kPoses[h.target];
  }

  // Idle motion fades in only after a blend has finished, so a pose is exact
  // on the tick its blend ends.
  const float rate = idleRate(h.target);
  if (settled && !h.walking && rate > 0.f) {
    h.idle_w = minf(1.f, h.idle_w + dt / kIdleRamp);
    h.idle_phase += dt * rate;
    h.idle_phase -= floorf(h.idle_phase);
  }
  if (rate > 0.f) addIdle(base, h.target, easeInOut(h.idle_w), h.idle_phase);

  if (!h.walking) {
    h.pose = base;
    return;
  }

  // Walk: phase advances with distance covered, so the stance foot keeps pace.
  h.walk_w = minf(1.f, h.walk_w + dt / kWalkIn);
  const float amp = strideAmp(h.walk_speed, h.height);
  const float cycle = 4.f * (kThigh + kShin) * h.height * sinT(amp * kInvTwoPi);   // two steps; table sine so PC and board agree
  const float remain = h.walk_x - h.feet.x;
  const float step = h.walk_speed * easeInOut(h.walk_w) * dt;
  bool arrived = false;
  if (fabsf(remain) <= step) {
    h.feet.x = h.walk_x;
    arrived = true;
  } else {
    h.feet.x += remain > 0.f ? step : -step;
  }
  if (cycle > 0.f) {
    h.walk_phase += step / cycle;
    h.walk_phase -= floorf(h.walk_phase);
  }
  h.pose = mix(base, walkPose(base, h.walk_phase, amp, h.carry == CARRY_NONE), easeInOut(h.walk_w));

  if (arrived) {   // settle from mid-stride into the rest pose
    h.walking = false;
    h.walk_w = 0.f;
    startBlend(h, h.walk_rest, kArriveBlend, BLEND_PLAIN);
  }
}

Rect hikerBounds(const Hiker& h) {
  Prims ps;
  build(h, ps);
  float bx0 = 3.0e38f, by0 = 3.0e38f, bx1 = -3.0e38f, by1 = -3.0e38f;
  for (int k = 0; k < ps.n; ++k) {
    float x0, y0, x1, y1;
    primExtent(ps.v[k], x0, y0, x1, y1);
    bx0 = minf(bx0, x0);
    by0 = minf(by0, y0);
    bx1 = maxf(bx1, x1);
    by1 = maxf(by1, y1);
  }
  if (ps.n == 0) return Rect{0, 0, 0, 0};
  return Rect{(int)floorf(bx0) - 2, (int)floorf(by0) - 2, (int)ceilf(bx1) + 2, (int)ceilf(by1) + 2};
}

// The fire-side rim (ACTOR_LIGHT), drawn on the pixels the figure has just
// put down: on each row of the head (below the brim) and the torso, walk from
// the body's axis toward the fire through the silhouette; where it ends
// within reach, that is the edge facing the fire, and its last pixel (and
// half the one inside it) lights up. Rows where an arm, a knee or the hat
// brim stands out past the body are left dark: the lit edge there is not the
// torso's. Row by row, so a strip draws exactly what a full frame does.
// Solid figure: the ACTOR fill, or an edge pixel all but covered (so a
// coverage step either way of the mirror does not move the edge).
inline bool solid(Pixel p, Pixel a) {
  const int dr = (int)(p >> 11) - (int)(a >> 11), dg = (int)((p >> 5) & 63) - (int)((a >> 5) & 63);
  const int db = (int)(p & 31) - (int)(a & 31);
  return dr * dr <= 1 && dg * dg <= 4 && db * db <= 1;
}

void rimPass(const RimAxis& r, Surface& s, const Palette& pal, const Rect& clip) {
  if (r.side == 0) return;
  const Pixel A = pal[ACTOR], L = pal[ACTOR_LIGHT];
  if (A == L) return;
  const Pixel half = lerp565(A, L, (uint8_t)(r.width >= 1.5f ? 200 : 120));
  const int top = (int)ceilf(r.head.y - 0.15f * r.head_r);   // under the brim
  const int bot = (int)floorf(r.hip.y);
  const int y0 = top > clip.y0 ? top : clip.y0, y1 = bot < clip.y1 ? bot : clip.y1;
  for (int y = y0; y < y1; ++y) {
    const float fy = (float)y + 0.5f;
    float ax, reach;
    if (fy <= r.head.y + r.head_r) {
      ax = r.head.x;
      reach = r.head_r + 2.5f;
    } else if (fy < r.chest.y) {   // the neck: from the head down to the chest
      const float t = (fy - r.head.y) / maxf(1.f, r.chest.y - r.head.y);
      ax = r.head.x + (r.chest.x - r.head.x) * t;
      reach = r.reach;
    } else {
      const float t = (fy - r.chest.y) / maxf(1.f, r.hip.y - r.chest.y);
      ax = r.chest.x + (r.hip.x - r.chest.x) * t;
      reach = r.reach;
    }
    // The pixel at the axis on the far side from the fire (so it mirrors exactly).
    const int x = r.side > 0 ? (int)ceilf(ax) - 1 : (int)floorf(ax);
    if (x < clip.x0 || x >= clip.x1) continue;
    Pixel* row = s.row(y);
    if (!solid(row[x], A)) continue;
    const int n = (int)ceilf(reach);
    int edge = -1;
    for (int d = 1; d <= n; ++d) {
      const int xx = x + r.side * d;
      if (xx < clip.x0 || xx >= clip.x1) break;
      if (!solid(row[xx], A)) {
        edge = xx - r.side;
        break;
      }
    }
    if (edge < 0 || edge == x) continue;
    row[edge] = L;
    const int in = edge - r.side;
    if (in != x) row[in] = half;
  }
}

void hikerDraw(const Hiker& h, Surface& s, const Palette& pal, const Rect& clip) {
  g_last_prims = 0;
  if (!h.visible) return;
  Prims ps;
  RimAxis rim;
  build(h, ps, nullptr, &rim);
  for (int k = 0; k < ps.n; ++k) {
    const Prim& p = ps.v[k];
    float x0, y0, x1, y1;
    primExtent(p, x0, y0, x1, y1);
    if (x1 < (float)clip.x0 || x0 > (float)clip.x1 || y1 < (float)clip.y0 || y0 > (float)clip.y1) continue;
    const Pixel c = pal[p.slot];
    switch (p.kind) {
      case PRIM_CAP: strokeCapsule(s, p.p[0], p.p[1], p.w, c, clip); break;
      case PRIM_CIRCLE: fillCircle(s, p.p[0], p.w, c, clip); break;
      default: fillPolygon(s, p.p, p.n, c, clip); break;
    }
    ++g_last_prims;
  }
  rimPass(rim, s, pal, clip);
}

// Same kinematics and grounding as build(), without the primitive list.
Vec2 hikerHandPos(const Hiker& h, int which) {
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
  Vec2 hand{0.f, 0.f};
  for (int i = 0; i < 2; ++i) {
    const Vec2 knee = add(hip, mul(dirA(P.thigh[i]), kThigh * H));
    const float shin = P.thigh[i] - P.knee[i];
    const Vec2 ankle = add(knee, mul(dirA(shin), kShin * H));
    const Vec2 fd = dirA(0.5f * kPi + kFootFollow * shin);
    const Vec2 heel = sub(ankle, mul(fd, kHeel * H));
    const Vec2 toe = add(ankle, mul(fd, kToe * H));
    low = maxf(low, knee.y + 0.5f * kShinW * H);
    low = maxf(low, maxf(heel.y, maxf(toe.y, ankle.y)) + 0.5f * kFootW * H);
    if (i == (which ? 1 : 0)) {
      const Vec2 elbow = add(shoulder, mul(dirA(P.shoulder[i]), kUpper * H));
      hand = add(elbow, mul(dirA(P.shoulder[i] + P.elbow[i]), kFore * H));
    }
  }
  low = maxf(low, head.y + kHeadR * H);
  const float sx = h.facing < 0 ? -1.f : 1.f;
  return Vec2{h.feet.x + sx * hand.x, h.feet.y - low - P.lift * H + hand.y};
}

// Same kinematics and grounding again, for the head.
Vec2 hikerHeadPos(const Hiker& h, float* radius) {
  const float H = h.height;
  const HikerPose& P = h.pose;
  const float breath = (1.f - h.walk_w) * kBreathAmp * H * (0.5f + 0.5f * sinT(h.breath_phase));
  const Vec2 hip{P.dx * H, 0.f};
  const float lt = P.lean * kInvTwoPi;
  const Vec2 up{sinT(lt), -cosT(lt)};
  const Vec2 chest = add(hip, mul(up, kTorsoTop * H + breath));
  const Vec2 neck = add(hip, mul(up, kNeck * H + breath));
  const float ht = (P.lean + P.head) * kInvTwoPi;
  const Vec2 hu{sinT(ht), -cosT(ht)};
  const Vec2 hf{cosT(ht), sinT(ht)};
  const Vec2 head = add(neck, add(mul(hu, kHeadUp * H), mul(hf, kHeadFwd * H)));
  float low = maxf(hip.y, chest.y) + 0.5f * kTorsoW * H;
  for (int i = 0; i < 2; ++i) {
    const Vec2 knee = add(hip, mul(dirA(P.thigh[i]), kThigh * H));
    const float shin = P.thigh[i] - P.knee[i];
    const Vec2 ankle = add(knee, mul(dirA(shin), kShin * H));
    const Vec2 fd = dirA(0.5f * kPi + kFootFollow * shin);
    const Vec2 heel = sub(ankle, mul(fd, kHeel * H));
    const Vec2 toe = add(ankle, mul(fd, kToe * H));
    low = maxf(low, knee.y + 0.5f * kShinW * H);
    low = maxf(low, maxf(heel.y, maxf(toe.y, ankle.y)) + 0.5f * kFootW * H);
  }
  low = maxf(low, head.y + kHeadR * H);
  if (radius) *radius = kHeadR * H;
  const float sx = h.facing < 0 ? -1.f : 1.f;
  return Vec2{h.feet.x + sx * head.x, h.feet.y - low - P.lift * H + head.y};
}

Vec2 hikerRodTip(const Hiker& h) {
  if (h.carry != CARRY_ROD) return hikerHandPos(h, 0);
  Prims ps;
  Vec2 tip;
  build(h, ps, &tip);
  return tip;
}

const HikerPose& hikerPoseDef(PoseId p) { return kPoses[p < POSE_COUNT ? p : POSE_STAND]; }
const char* hikerPoseName(PoseId p) { return p < POSE_COUNT ? kPoseNames[p] : "?"; }
int hikerLastDrawPrimitives() { return g_last_prims; }

}  // namespace ss
