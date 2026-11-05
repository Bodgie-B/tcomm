// The ropes course (morning and daytime plan, section 8): four wooden posts that
// stand up out of the ground at the back of the clearing, one after another with
// an overshoot (the way the build-up stands trees up), and between them
//   (1) a rope bridge: two hand ropes, a walkway of short planks that sags and
//       sways under him, a boarding ramp and a landing;
//   (2) a tyre swing: a tyre (a ring) on a rope from a beam between two posts;
//   (3) a cargo net: a grid of rope lines, 50 px wide, hanging from a beam.
// A standalone module like the kite: plain data, a step function, bounds, a
// dirty helper and a draw. It owns no World state, calls no platform code and
// draws no random numbers after ropesStart.
//
// It also owns the hiker for the length of the sequence (ropesHikerBusy): it
// walks him from item to item, and moves his feet, sets his poses and facing
// through the public rig calls, then lays a pose overlay over the composed pose
// (ropesPose) that grips the hand ropes, sits him in the tyre, climbs the net,
// hangs him upside down by one foot and puts his feet on the sagging planks.
//
// What the scene does each tick, in this order:
//     ropesStep(r, hiker, SS_TICK_DT);     // time, props, and his commands
//     hikerTick(hiker, SS_TICK_DT);        // the rig composes its pose
//     ropesPose(r, hiker);                 // the overlay (a no-op while idle)
//   then marks dirty rows: hikerBounds(hiker), and for the props
//   ropesDirty(before, r, rects) with `before` a copy of the state taken before
//   ropesStep; then draws ropesDraw (behind him) and hikerDraw.
//
// Scripting a variant. There is nothing to script inside it: the scene
//   1. ropesInit(r, x0, ground_y) once per loop (hidden; x0 is the course's left
//      foot, the ramp's toe; the course reaches x0 + kRopesWidth; ground_y is
//      the line the posts stand on, 330 suggests itself: keep it at or under 385
//      so that nothing reaches the status chip's corner);
//   2. stands him still, facing the course, at the left of it (about x0 - 40);
//   3. ropesStart(r, ROPES_A / ROPES_B / ROPES_C, rng): draws exactly two values;
//   4. runs the tick above until ropesDone(r). During the build and the tear-down
//      (ropesHikerBusy false) the module leaves him alone: the scene may have him
//      watch (look around, hands on hips). When ropesDone is true everything is
//      gone from the picture, he stands at rest where the last item left him,
//      facing right, hands empty, the hat on.
//   The upside-down gag needs nothing from the scene beyond ropesPose: the
//   rotation about the hips is applied there (ropesRotation reads it, the pose
//   helper ropesRotatePose does the arithmetic) and ropesPose also places the
//   hips (pose.lift and pose.dx) so that the rotation pivots about the caught
//   ankle. Do not move his feet or set his pose while ropesHikerBusy.
//
// Variants (the loop plan picks one):
//   A  all three, the net with the mishap (foot caught, upside down, the drop);
//   B  the bridge and the swing only;
//   C  the swing and the net only (the net with the mishap).
// What the seed changes: the wobble, how long he hesitates, two or three spins,
// how far the tyre swings (0.38 to 0.42 rad), which rung catches his foot and how
// long he hangs. Lengths over seeds 1..40 (test_ropes): A 83 to 87 s, B 61 to 63 s,
// C 75 to 77 s, counting from a standing start 40 px left of the ramp.
//
// Everything marked PROPOSED is a guess; how it looks is UNVERIFIED until Mike
// has seen it (and on the panel).
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "hiker.h"

namespace ss {

enum RopesVariant : uint8_t { ROPES_A = 0, ROPES_B, ROPES_C, ROPES_VARIANTS };
enum RopesItem : uint8_t { ROPES_BRIDGE = 1, ROPES_SWING = 2, ROPES_NET = 4 };
enum RopesPhase : uint8_t { ROPES_IDLE = 0, ROPES_RUN, ROPES_DONE };

// The script, in order, of one run (names via ropesSegName). The plan is the
// segments of the items in the variant, between SG_BUILD and SG_TEAR.
enum RopesSeg : uint8_t {
  SG_BUILD = 0,
  SG_B_WALK, SG_B_LOOK, SG_B_UP, SG_B_CROSS1, SG_B_BRACE, SG_B_CROSS2, SG_B_DOWN, SG_B_PROUD,
  SG_S_WALK, SG_S_LOOK, SG_S_BOARD, SG_S_PUMP, SG_S_SPIN, SG_S_SLOW, SG_S_OFF, SG_S_DIZZY, SG_S_RECOVER,
  SG_N_WALK, SG_N_LOOK, SG_N_CLIMB, SG_N_CATCH, SG_N_FLIP, SG_N_HANG, SG_N_DROP, SG_N_LAND, SG_N_SITUP, SG_N_STAND,
  SG_HOLD, SG_TEAR,
  SG_COUNT
};

// The course, in px from its left foot (x0) and the ground line (ground_y). PROPOSED.
constexpr float kRopesWidth = 301.f;       // x0 .. x0 + this holds every prop
constexpr float kRopesHeight = 176.f;      // the tallest post, plus its overshoot
constexpr float kRopesPostX[4] = {38.f, 114.f, 226.f, 298.f};   // the bridge's two posts, the swing's two, the net's two share B and D
constexpr float kRopesRampX = 30.f;        // the boarding ramp runs from x0 to here, rising to the walkway
constexpr float kRopesDeckH = 30.f;        // the walkway's ends stand this high
constexpr float kRopesSwingX = 170.f;      // the tyre hangs here
constexpr float kRopesNetX0 = 242.f;       // the net's first column; six columns 10 px apart
constexpr float kRopesNetX = 267.f;        // where he stands to climb it

#define ROPES_PLAN_MAX 32
// Rects ropesDirty can return: one per part (four posts, the bridge's fixed
// parts, its walkway, the beams, the swing, the net).
#define ROPES_DIRTY_MAX 9

// POD, 136 bytes. Lives by value in the World (and once more in WorldDirty
// for ropesDirty).
struct RopesState {
  uint8_t variant;
  uint8_t items;                 // RopesItem bits present
  uint8_t phase;                 // RopesPhase
  uint8_t seg;                   // index into plan
  uint8_t n_seg;
  uint8_t entered;               // the current segment's entry actions have run
  uint8_t k[8];                  // the seeded parameters (ropesStart)
  uint8_t plan[ROPES_PLAN_MAX];  // RopesSeg ids
  float x0, gy;                  // layout (ropesInit)
  float t;                       // seconds since ropesStart
  float st;                      // seconds in this segment
  float bt;                      // the build clock, seconds: rises in SG_BUILD, falls in SG_TEAR
  // The bridge.
  float bw;                      // 0..1 how much of him the walkway carries
  float bx;                      // frame x of him on it
  float dep;                     // px the walkway is pressed down under him
  float bnc, bnv;                // the whole span's bounce, px and px/s
  float bph;                     // wobble clock, turns
  float bidx;                    // last half-stride seen (a footfall kicks the walkway)
  // The swing.
  float th, om;                  // the tyre's pendulum, rad and rad/s (0 hangs straight down, + toward +x)
  float pump;                    // 0..1 how hard he is pumping
  float spin;                    // turns the tyre has twisted
  float tmax;                    // the swing's amplitude cap, rad
  // The net.
  float nsw, nsv;                // the net's sway at its foot, px and px/s
  // His choreography.
  float ax, ay;                  // his hips when the current segment began (frame px)
  float rot;                     // the rig's rotation about the hips, radians, + tips him toward his facing
};

// ---- setup ----

// A hidden course whose left foot is x0 and whose posts stand on ground_y.
void ropesInit(RopesState& r, float x0, float ground_y);
// Begin variant `variant` (ROPES_A..C; others become A). Draws exactly two
// values from `rng` and never again. Needs ropesInit first.
void ropesStart(RopesState& r, int variant, Rng& rng);

// ---- the tick ----

// Advance by dt seconds (any dt up to 0.1 s works): the build clock, the
// walkway, the pendulum, the net, and, while the module has him, his commands
// (walk, pose, facing, feet). Call before hikerTick.
void ropesStep(RopesState& r, Hiker& h, float dt);
// The overlay on the pose hikerTick has just composed: grip, sit, climb, hang,
// rotation, and his hips on the planks or in the tyre. Call it once per
// hikerTick (it bends the composed pose, so a second call would bend it twice).
// A no-op unless ropesHikerBusy.
void ropesPose(const RopesState& r, Hiker& h);

bool ropesDone(const RopesState& r);          // everything is gone and he is free
bool ropesHikerBusy(const RopesState& r);     // the module moves him (not in SG_BUILD, SG_TEAR, or outside the run)
int ropesSegment(const RopesState& r);        // the RopesSeg now, or -1 outside the run
const char* ropesSegName(int seg);
// How long the run lasts if he starts at distance 0 from the first item: the
// sum of the plan's fixed times (the walks take what the walks take).
float ropesFixedSeconds(const RopesState& r);

// The rotation the scene may read (radians, + tips him toward his facing): 0
// except while he is upside down. ropesPose has applied it already.
float ropesRotation(const RopesState& r);
// What ropesPose does with it, for a scene that wants to rotate a pose of its
// own: adds `rot` to the lean, both thighs and both upper arms (they are
// absolute angles; the knees, elbows and the head are relative to their
// parents and stay), which turns the whole rig about its hips.
void ropesRotatePose(HikerPose& p, float rot);

// The rig's joints in frame space as ropes.cpp's copy of the rig's kinematics
// computes them from h.pose (any of the pointers may be null): hips, head
// centre, near and far hand, and the near ankle. test_ropes compares them with
// hikerHeadPos and hikerHandPos on every pose, so a change to hiker.cpp that
// this copy has not followed fails there.
void ropesRigJoints(const Hiker& h, Vec2* hips, Vec2* head, Vec2* hand_near, Vec2* hand_far, Vec2* ankle_near);

// The floor under him on the bridge side of the course: the ramp, the planks
// (their upper face, so the sag and the bounce included) and the landing; the
// ground elsewhere. Frame y at frame x.
float ropesFloorY(const RopesState& r, float x);
// The tyre's centre and where the rope meets it, frame px (the quantized
// position the draw uses), and the net's node at column 0..5 and the y a
// row sits at (row 0 top .. 13 bottom; the foot of the net is row 13).
Vec2 ropesTyre(const RopesState& r, Vec2* rope_end);
Vec2 ropesNetNode(const RopesState& r, int col, int row);

// ---- bounds, dirty, draw ----

// Everything the props cover now, to their antialiased fringe. Empty when
// nothing is drawn.
Rect ropesBounds(const RopesState& r);
// True when the two states draw exactly the same pixels.
bool ropesSame(const RopesState& a, const RopesState& b);
// The areas to mark dirty when the props go from `before` to `after`. Returns how
// many rects (0 when nothing visible changed, at most ROPES_DIRTY_MAX). Together
// they cover every pixel that differs between the two draws.
int ropesDirty(const RopesState& before, const RopesState& after, Rect out[ROPES_DIRTY_MAX]);
// Draw onto `strip` (a strip or a whole frame; frame coordinates), touching only
// pixels inside `clip` and inside ropesBounds. Needs the rasterizer scratch
// (rasterSetScratch). Colors: TRUNK for the posts, beams, planks and ramp; TENT_LIT
// for every rope and the net; ACTOR for the tyre.
void ropesDraw(const RopesState& r, Surface& strip, const Rect& clip, const Palette& pal);

}  // namespace ss
