// Critter rigs: the animals and their small action animations.
//
// Each kind is a handful of ACTOR primitives (ACTOR_LIGHT only for the
// raccoon's tail rings and the owl's eyes) built around one silhouette feature:
// the chipmunk's upright tail, the squirrel's curled bushy tail, the frog's big
// back legs, the deer's legs and ears, the raccoon's ringed tail, the owl's ear
// tufts and eye discs, the bird's V wings, the bear's hump and round ears, the
// mouse's round ear and thin tail.
//
// Conventions follow the hiker rig (hiker.h): plain data held by value, `feet`
// is the ground point under the body (the talons on the branch for the owl,
// the hind feet on the trunk when climbing), angles are radians and absolute,
// `facing` mirrors the figure about feet.x, every action blends in from the
// figure on screen, and critterBounds is folded from the same primitive list
// critterDraw rasterizes, so they cannot disagree.
//
// Actions. `seconds` in critterSetAction is how long the action takes to land:
// the pose blend for pose actions, the whole move for one-shots (an in-place
// hop, the owl's head turn, a startle). Loop actions (idle, move, run, blink,
// eat, sniff, shake) then repeat at the kind's own rate until changed; the rest
// hold their end pose. A finished blend lands on critterPoseDef(kind, action)
// bit-exactly, and loop motion fades in only after that.
//
// Climbing: the critter turns nose-up with its belly and paws on the vertical
// line x = feet.x (the face of the trunk), body on the -facing side, hind feet
// at feet.y. It steps onto that line during the blend into CA_CLIMB.
//
// The struct is plain data so it lives by value inside World; fields below the
// public block are owned by critters.cpp.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"

namespace ss {

enum CritterKind : uint8_t {
  CR_CHIPMUNK, CR_SQUIRREL, CR_FROG, CR_DEER, CR_RACCOON, CR_OWL, CR_BIRD, CR_BEAR, CR_MOUSE, CR_COUNT
};

enum CritterAction : uint8_t {
  CA_IDLE, CA_MOVE, CA_RUN, CA_SIT, CA_LOOK, CA_SNIFF, CA_REACH, CA_CLIMB, CA_SHAKE, CA_HOP, CA_BLINK,
  CA_HEADTURN, CA_STARTLE, CA_EAT, CA_CARRY, CA_REAR, CA_COUNT
};

// Items a critter can hold (small ACTOR shapes at the mouth or paws).
enum CritterCarry : uint8_t { CC_NONE, CC_NUT, CC_CRUMB, CC_SHOE, CC_BAR, CC_COUNT };

// The blended body state. Lengths in px at scale 1, angles in radians.
// fore[]/hind[] are the near (0) and far (1) limbs, absolute from straight down,
// + forward; the bird's fore[] are its wings (0 folded, + raised).
struct CritterPose {
  float lift;      // body raised off the ground (hops, flight)
  float pitch;     // whole body nose-up
  float head;      // head raised relative to the body, + up
  float look;      // owl: head turn (turns); deer: extra nose lift (rad); others: head pushed forward, px
  float squash;    // vertical stretch (+ taller and thinner, - flatter)
  float tail;      // tail swing, + up/forward
  float rear;      // 0..1 up on the hind legs
  float crouch;    // 0..1 body lowered toward the ground
  float climb;     // 0..1 turned onto a trunk
  float fore[2];
  float hind[2];
};

struct Critter {
  CritterKind kind;
  Vec2 feet;           // ground point under the body
  int facing;          // +1 right, -1 left
  float scale;         // 1 = nominal size
  bool visible;
  float speed_dial;    // 0..1 per loop: timid slow mover .. quick; scales move speed and gait rate
  float jitter_dial;   // 0..1 per loop: calm .. fidgety; scales idle fidget size and rate
  uint8_t carry;       // CritterCarry
  // Offsets the caller lays over the drawn pose (not the blend, so a new action
  // never starts from them): radians added to the near fore limb (a bear's paw
  // reaching from the trunk) and to the head (+ up). Ease them back to 0.
  float fore_add;
  float head_add;

  // ---- owned by critters.cpp ----
  CritterAction action;
  uint8_t blend_kind;
  bool blending;
  bool moving;         // x move under way (critterMoveTo)
  bool climbing;       // y move under way (critterClimbTo)
  float action_t;      // seconds since the action started
  float action_dur;    // seconds the action takes to land
  CritterPose from;    // composed pose when the action started
  CritterPose pose;    // base pose this tick (the blend), without loop motion
  float loop_w;        // 0..1 weight of the loop motion (fades in after the blend)
  float gait_w;        // 0..1 weight of the gait while moving or climbing
  float phase;         // turns: gait, loop or one-shot progress
  float idle_phase;    // turns: slow fidget clock, runs always
  float move_to;       // target x (moving) or y (climbing)
  float move_from;     // where the move started (flight arc)
  float move_speed;    // px/s
  float turn_t;        // seconds into a turn-around; < 0 when not turning
};

void critterInit(Critter& c, CritterKind k, Vec2 feet, int facing);
void critterSetAction(Critter& c, CritterAction a, float seconds);   // unsupported -> CA_IDLE
bool critterSupports(CritterKind k, CritterAction a);
// Walk to x with the kind's gait (CA_HOP for squirrel, frog and bird, CA_MOVE
// for the rest), or with the current action if it is already CA_MOVE, CA_RUN
// or CA_HOP (so CA_MOVE first makes the bird fly). Turns around first when x is
// behind (unless `back`: a short step back, still facing the same way, legs
// stepping backward). Arrival -> CA_IDLE. The owl does not move.
void critterMoveTo(Critter& c, float x, float px_per_s, bool back = false);
// Squirrel, raccoon, bear: climb along the trunk at feet.x to feet.y = y.
// Holds CA_CLIMB on arrival.
void critterClimbTo(Critter& c, float y, float px_per_s);
bool critterArrived(const Critter& c);
void critterTick(Critter& c, float dt);   // dt 0 recomposes
Rect critterBounds(const Critter& c);     // every pixel critterDraw can touch, plus 2 px
// Frame-space anchors from the drawn figure (offsets included): the end of the
// near fore limb (bear, chipmunk, squirrel, raccoon, mouse; `feet` for the
// others) and the top of the head (bear; the top centre of the bounds for the
// others).
Vec2 critterPaw(const Critter& c);
Vec2 critterHead(const Critter& c);
// The mouth (bear and the small quadrupeds; critterHead for the others).
Vec2 critterMouth(const Critter& c);
// The drawn x scale of the figure: 1, or down to 0.15 mid turn-around (facing
// flips at the narrowest point). What it holds squeezes with it.
float critterTurnScale(const Critter& c);
void critterDraw(const Critter& c, Surface& s, const Palette& pal, const Rect& clip);

// The pose an action's blend lands on.
const CritterPose& critterPoseDef(CritterKind k, CritterAction a);
bool critterActionLoops(CritterAction a);
const char* critterName(CritterKind k);
const char* critterActionName(CritterAction a);

// Rasterizer calls issued by the most recent critterDraw (budget checks).
int critterLastDrawPrimitives();
// Primitive budget per frame for a kind (7, the bear 9).
int critterBudget(CritterKind k);
// Standing (CA_IDLE) height in px at scale 1, feet to top; the nominal size
// from the brief is the larger of height and length (see the stage 5 notes).
float critterHeight(CritterKind k);

}  // namespace ss
