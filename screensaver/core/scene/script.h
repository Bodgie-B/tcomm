// The step interpreter's vocabulary: shared by the spine (spine.cpp) and the
// surprise majors (majors.cpp). Internal to core/scene.
//
// A script is a const table of Steps. The interpreter runs the current step
// each tick; instant steps (face, carry, do) chain within one tick, waiting
// steps (walk, pose hold, timed action) hold the script until they finish.
// Positions are anchors (CampLayout points, or a major's own points) plus an
// offset, so one table fits every seed. The interpreter itself lives in
// spine.cpp; scriptStart / scriptRun below are its public face.
#pragma once

#include "hiker.h"
#include "props.h"

namespace ss {

struct World;

enum StepOp : uint8_t {
  OP_PLACE,      // feet at the anchor, facing a (0 left, 1 right), in the scene
  OP_WALK,       // walk to the anchor at s px/s; waits for arrival
  OP_VIA,        // walk toward the anchor; done within 4 px, so the next walk keeps the stride
  OP_POSE,       // ease into pose a over x seconds, then hold until s seconds from the start
  OP_WAIT,       // s seconds of nothing new
  OP_WAIT_AWAY,  // the plan's away_s seconds (firewood, out of frame)
  OP_FACE,       // a: 0 left, 1 right, 2 toward the fire, 3 toward anchor `at`
  OP_CARRY,      // hikerCarry(a)
  OP_ACT,        // fn(w, u) every tick for s seconds, u 0..1 (exactly 1 on the last tick)
  OP_DO,         // fn(w, 1) once
  OP_GO,         // walk to the anchor at s px/s by a route: around the back of the fire pit,
                 // and in two legs when the move is steeper than it is wide (walks only move x)
  OP_REPEAT,     // while pred(w): go back a steps
  OP_UNTIL,      // wait until pred(w) (a critter arrived, the bear is gone)
  OP_SKIP,       // unless pred(w): skip the next a steps (a step run only when it applies)
};

enum Anchor : uint8_t {
  AT_HERE = 0, AT_SPOT, AT_PAST, AT_TENT_WORK, AT_PACK, AT_PEG_ROCK, AT_WOOD_WORK, AT_FIRE,
  AT_BLOW, AT_SIT, AT_COOL, AT_ROCK_SIT, AT_FORD0, AT_FORD1, AT_EDGE_L, AT_EDGE_R,
  AT_PACK_WORK, AT_BAG_WORK, AT_DOOR, AT_VIEW, AT_READ, AT_LIE, AT_LISTEN, AT_SKIP,
  AT_MUG,          // the mug where it lies (its right side), or the pack if it is packed
  AT_STONE,        // cairn: the next flat stone, alternating sides
  AT_CAIRN_WORK,   // cairn: beside it, on the side he comes from
  AT_CAIRN,
  AT_M0, AT_M1, AT_M2, AT_M3   // the running major's own points (MajorState::p)
};

typedef void (*ActFn)(World& w, float u);
typedef bool (*PredFn)(const World& w);

struct Step {
  uint8_t op;
  uint8_t a;       // pose, carry, facing; OP_REPEAT: steps back; OP_SKIP: steps skipped
  uint8_t at;      // anchor
  float x, y;      // offset from the anchor (OP_POSE: x is the blend seconds)
  float s;         // seconds, or px/s for walks
  ActFn fn;
  PredFn pred;
};

// Step constructors (keep the tables readable).
namespace sc {

constexpr int LEFT = 0, RIGHT = 1, TO_FIRE = 2;

// Normal stroll, careful wading, a run.
constexpr float kStroll = 34.f, kWade = 22.f, kRun = 78.f;

constexpr Step place(uint8_t at, int face) {
  return Step{OP_PLACE, (uint8_t)face, at, 0.f, 0.f, 0.f, nullptr, nullptr};
}
constexpr Step walk(uint8_t at, float v, float x = 0.f, float y = 0.f) {
  return Step{OP_WALK, 0, at, x, y, v, nullptr, nullptr};
}
constexpr Step via(uint8_t at, float v, float x = 0.f, float y = 0.f) {
  return Step{OP_VIA, 0, at, x, y, v, nullptr, nullptr};
}
constexpr Step go(uint8_t at, float v, float x = 0.f, float y = 0.f) {
  return Step{OP_GO, 0, at, x, y, v, nullptr, nullptr};
}
constexpr Step pose(PoseId p, float blend, float hold) {
  return Step{OP_POSE, (uint8_t)p, 0, blend, 0.f, blend + hold, nullptr, nullptr};
}
constexpr Step still(float s) { return Step{OP_WAIT, 0, 0, 0.f, 0.f, s, nullptr, nullptr}; }
constexpr Step waitAway() { return Step{OP_WAIT_AWAY, 0, 0, 0.f, 0.f, 0.f, nullptr, nullptr}; }
constexpr Step face(int dir) { return Step{OP_FACE, (uint8_t)dir, 0, 0.f, 0.f, 0.f, nullptr, nullptr}; }
constexpr Step faceTo(uint8_t at) { return Step{OP_FACE, 3, at, 0.f, 0.f, 0.f, nullptr, nullptr}; }
constexpr Step carry(CarryId c) { return Step{OP_CARRY, (uint8_t)c, 0, 0.f, 0.f, 0.f, nullptr, nullptr}; }
constexpr Step act(ActFn fn, float s) { return Step{OP_ACT, 0, 0, 0.f, 0.f, s, fn, nullptr}; }
constexpr Step doit(ActFn fn) { return Step{OP_DO, 0, 0, 0.f, 0.f, 0.f, fn, nullptr}; }
constexpr Step repeatWhile(PredFn p, int back) {
  return Step{OP_REPEAT, (uint8_t)back, 0, 0.f, 0.f, 0.f, nullptr, p};
}
constexpr Step until(PredFn p) { return Step{OP_UNTIL, 0, 0, 0.f, 0.f, 0.f, nullptr, p}; }
constexpr Step skipUnless(PredFn p, int n) { return Step{OP_SKIP, (uint8_t)n, 0, 0.f, 0.f, 0.f, nullptr, p}; }

}  // namespace sc

// ---- the interpreter (spine.cpp) ---------------------------------------------------

// Load a script into World::spine.run (one script runs at a time: a spine
// beat, a filler or a major) and run one tick of it.
void scriptStart(World& w, const Step* steps, uint8_t n);
void scriptRun(World& w);
// The loaded script has run its last step (or none is loaded).
bool scriptDone(const World& w);
// Where an anchor is now.
Vec2 scriptAnchor(const World& w, uint8_t at);

// Helpers the scripts' actions share.
// Pose change that takes the fx offsets out of the blend's start first.
void scriptSetPose(World& w, PoseId p, float seconds);
// Every fx pose offset back to 0 (an action's last tick).
void scriptFxRest(World& w);
// Arm i blended by k toward absolute shoulder and elbow angles.
void scriptSetArm(World& w, int i, float k, float sh, float el);
// 0 at both ends of u: eased in over the first `a`, out over the last `b`.
float scriptEnvelope(float u, float a, float b);
// Ground a critter (or a prop) can stand on at x: `want`, or higher up the
// bank if the stream comes closer.
float scriptGroundAt(const World& w, float x, float want);
// An item put down on the ground at `at`, facing the hiker's way.
void scriptSetDown(World& w, ItemId id, Vec2 at);

}  // namespace ss
