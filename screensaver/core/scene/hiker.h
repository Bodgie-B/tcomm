// The hiker rig: a jointed 2D silhouette, pose table and blending, a
// procedural walk cycle, breathing, and carried props.
//
// Everything is drawn in the ACTOR slot (one flat black shape) with ACTOR_LIGHT
// only for an optional fire-side rim and the jar lid. The brimmed hat and the
// tall pack are the two features that must read at any size.
//
// Geometry scales from `height` (feet to hat top when standing, about 90 px).
// Angles are radians measured from straight down, positive toward the facing
// direction, and absolute (not relative to the parent joint), so a pose reads
// the same whatever the torso does. Knees bend backward: shin = thigh - knee.
// Elbows bend forward: forearm = upper arm + elbow. Index 0 is the near limb.
//
// `feet.x` anchors the hips (the body stands over it, sits on it, lies with
// its hips on it); `feet.y` is the ground line. The rig grounds itself every
// frame: the lowest foot, knee, seat or head contact sits on feet.y, so sitting
// and the walk bob need no hand-tuned hip heights.
//
// The struct is plain data so it lives by value inside World; fields below the
// public block are owned by hiker.cpp.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"

namespace ss {

enum PoseId : uint8_t {
  POSE_STAND, POSE_LOOK_AROUND, POSE_CROUCH, POSE_SIT, POSE_REACH_UP, POSE_CARRY,
  POSE_BLOW, POSE_YELP, POSE_LIE, POSE_WAVE, POSE_HANDS_ON_HIPS, POSE_SCRATCH_HEAD,
  POSE_HOLD_MUG, POSE_READ, POSE_COUNT
};

enum CarryId : uint8_t {
  CARRY_NONE, CARRY_WOOD, CARRY_MUG, CARRY_BOOK, CARRY_BAG, CARRY_STICK, CARRY_ROCK, CARRY_JAR,
  CARRY_COUNT
};

// One pose: joint angles plus body placement. Lengths are in units of height.
struct HikerPose {
  float lean;          // torso from vertical, + forward
  float head;          // head tilt relative to the torso, + nods down
  float lift;          // hips raised off the grounded position (jumps), units of height
  float dx;            // hips shifted from feet.x, + forward, units of height
  float thigh[2];      // absolute
  float knee[2];       // bend, shin = thigh - knee
  float shoulder[2];   // upper arm, absolute
  float elbow[2];      // bend, forearm = shoulder + elbow
};

struct Hiker {
  Vec2 feet;           // ground point under the hips
  int facing;          // +1 right, -1 left
  float height;        // about 90 px
  bool visible;
  bool pack_on;        // false once the pack is set down (stage 4 draws it as a prop)
  int lit_from;        // -1 fire to the left, +1 to the right, 0 no rim

  // ---- owned by hiker.cpp ----
  HikerPose pose;      // composed pose this tick (blend + walk + idle motion), no breathing
  HikerPose from;      // pose at the start of the current blend
  PoseId target;       // pose being blended to, or held
  uint8_t blend_kind;  // easing shape for the current blend
  bool blending;
  float blend_t, blend_dur;   // seconds
  float idle_w;        // 0..1 weight of the target's idle motion (wave, scratch), ramps in after the blend
  float idle_phase;    // turns

  bool walking;
  float walk_x, walk_speed;   // target x, px/s
  float walk_w;        // 0..1 weight of the walk over the base pose
  float walk_phase;    // turns; one turn is two steps
  PoseId walk_rest;    // pose to settle into on arrival

  CarryId carry;
  float breath_phase;  // turns
};

void hikerInit(Hiker& h, Vec2 feet, int facing);
void hikerSetPose(Hiker& h, PoseId p, float blend_seconds);   // ease into pose; stops walking
void hikerWalkTo(Hiker& h, float x, float px_per_s);          // walk cycle until arrival, then POSE_STAND
bool hikerArrived(const Hiker& h);
void hikerCarry(Hiker& h, CarryId c);
void hikerTick(Hiker& h, float dt);                           // advances blend, walk, breathing; dt 0 recomposes
Rect hikerBounds(const Hiker& h);                             // every pixel hikerDraw can touch, plus 2 px
void hikerDraw(const Hiker& h, Surface& s, const Palette& pal, const Rect& clip);

// The pose table entry for `p` (what a finished blend lands on exactly).
const HikerPose& hikerPoseDef(PoseId p);
const char* hikerPoseName(PoseId p);

// Rasterizer calls issued by the most recent hikerDraw (budget checks).
int hikerLastDrawPrimitives();

}  // namespace ss
