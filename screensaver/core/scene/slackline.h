// The slackline (morning and daytime plan, section 5): a 2 px line tied between
// two anchors, a walker who balances on it, and a squirrel or chipmunk who
// trots across it. A standalone module like the kite: plain data, step
// functions, bounds and a draw. It owns no World state, calls no platform code
// and draws no random numbers except from an Rng the caller passes (each such
// call says how many values it draws).
//
// Three parts, each a POD:
//
//   Slackline    the line. Anchors a and b (a.x < b.x; the caller gives the near
//                tree's trunk and a trunk or stake at the clearing's edge, about
//                180 to 260 px apart, about 30 px off the ground). It sags a
//                little by itself; a load makes a rounded V whose low point is at
//                the load's x and whose depth follows a damped spring, so a load
//                that steps off or jumps makes it bounce (about 3 to 4
//                oscillations). Depth is deepest mid-span and slight near an
//                anchor, as a real line is. The walker's weight is 1, a squirrel's
//                about 0.1.
//   SlackWalker  the hiker on the line: his feet's x positions, steps, balance
//                sway, wobble, the mount, the middle-of-the-line flourish, the
//                fall and a soft dismount. slackWalkerApply bends the composed
//                hiker pose (arms out wide, legs solved so each foot is on the
//                line, knees bent) and sets his feet and facing, so he stands on
//                the line as it sags and bounces, never above or below it.
//   SlackCritter a squirrel's or chipmunk's trip along the line: it trots with
//                ease, looks, sits in the middle, and goes back. The module gives
//                the feet position and the rig commands; the existing critter rig
//                (critters.h) draws it.
//
// What the caller does each tick, in this order:
//
//   slackWalkerStep(w, line, dt);       // moves his feet, loads the line
//   slackCritterStep(sc, line, dt);     // moves the critter, loads the line
//   slacklineStep(line, dt);            // the spring; takes the loads above
//   hikerTick(h, dt);                   // the composed pose
//   slackWalkerApply(w, line, h);       // bends it, sets h.feet and h.facing
//   slackCritterDrive(sc, c);           // rig commands (before critterTick)
//   critterTick(c, dt);
//   slackCritterPlace(sc, line, c);     // c.feet on the line
//   mark dirty (slacklineDirty, hikerBounds, critterBounds), draw
//
// Draw order: slacklineDraw, then the critter, then the hiker (his feet cover
// the line). The line goes behind anything that stands on it.
//
// Scripting (what each variant needs, section 5):
//   A  he stands on the ground a few px in from the near anchor; h.facing = dir;
//      slackWalkerMount(w, h.feet.x, h.feet.y, dir); wait for SW_STAND (1.5 s);
//      slackWalkerWobble(w, 0.15); about 1.2 s; slackWalkerWalk(w, line, 3) with
//      slackWalkerWobble(w, 0.45); wait for SW_STAND; slackWalkerWobble(w, 0.95) and
//      wait about 1.6 s (the arms grow); slackWalkerFall(w, rng); wait for SW_OFF (he is on
//      the ground, h.feet.y = his ground y); a beat (hikerSetPose SCRATCH_HEAD); hikerWalkTo
//      back to the mount x; face the line; mount again; slackWalkerWalk(w, line, 6) with
//      wobble 0.55, wobble 0.95, slackWalkerFall again.
//   B  mount, wobble 0.2, slackWalkerWalk(w, line, 0) (on toward the far anchor); each
//      tick, when his x reaches slacklineMidX: slackWalkerWobble(w, 0) and
//      slackWalkerFlourish (it begins when the step in hand ends; about 4.8 s; the line
//      bounces); when SW_STAND again: slackWalkerWalk(w, line, 0); it stops about 36 px
//      from the far anchor (SW_STAND); slackWalkerDismount; wait for SW_OFF.
//   after either: walk him to a watching spot on the ground with hikerWalkTo, give him
//      POSE_HANDS_ON_HIPS, and run the critter: slackCritterInit, then the per-tick calls
//      until slackCritterDone (it sits in the middle meanwhile).
// While the walker's mode is not SW_OFF the scene must not call hikerWalkTo,
// hikerSetPose or move h.feet itself: slackWalkerApply owns them.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED
// until Mike has seen it (and on the panel).
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "critters.h"
#include "hiker.h"

namespace ss {

// Rects slacklineDirty can return.
#define SLACK_DIRTY_MAX 4
// Most line segments (the drawn polyline has one more node), 10 px each.
#define SLACK_MAX_SEG 30

// ---- the line ----------------------------------------------------------------------------

// POD, about 80 bytes. Lives by value in the World (and once more in WorldDirty
// for slacklineDirty).
struct Slackline {
  Vec2 a, b;          // anchors, a.x < b.x, to 1/4 px
  float ground_y;     // the ground under it
  float lx;           // x of the low point: where the last load was
  float load;         // weight on it now, eased (0 none, 1 the walker)
  float d;            // spring: load sag at mid-span, px (+ down; negative while it bounces up)
  float v;            // spring velocity, px/s
  float acc_w, acc_wx;   // loads given since the last step
};

// Ties the line between the two anchors (swapped if a is on the right), over
// ground at `ground_y`. A line shorter than 16 px is stretched to 16.
void slacklineInit(Slackline& s, Vec2 a, Vec2 b, float ground_y);
// Someone of `weight` (walker 1, squirrel about 0.1, any >= 0) stands at x this
// tick. Several loads in one tick make one at their weighted mean x. A load
// that stops being given lets go (the line bounces).
void slacklineLoad(Slackline& s, float x, float weight);
// A jump or a drop: speed px/s on the sag (+ pushes the line down).
void slacklineKick(Slackline& s, float px_per_s);
// One tick (any dt up to 0.1 s): takes the loads given since the last step.
void slacklineStep(Slackline& s, float dt);
// Centre y of the drawn line at x (to within about 0.2 px), and the y a foot
// rests on there (the line's upper edge: one px above the centre).
float slacklineY(const Slackline& s, float x);
float slacklineTopY(const Slackline& s, float x);
// No load, no motion: the line lies at its resting sag and draws the same every tick.
bool slacklineSettled(const Slackline& s);
// The middle of the span.
float slacklineMidX(const Slackline& s);

// Everything the line draws: the polyline, the knots, their fringe. Not
// clamped to the frame.
Rect slacklineBounds(const Slackline& s);
// True when the two states draw exactly the same pixels.
bool slacklineSame(const Slackline& a, const Slackline& b);
// The areas to mark dirty when the line goes from `before` to `after`: each run
// of line that changed (old and new together), and the knots only if an anchor
// moved. Returns how many (0 when nothing visible changed). Together they cover
// every pixel that differs between the two draws. The line is long and nearly
// horizontal: its rects are as thin as the sag, so a bounce costs one or two
// strips.
int slacklineDirty(const Slackline& before, const Slackline& after, Rect out[SLACK_DIRTY_MAX]);
// Draw onto `strip` (a strip or a whole frame; frame coordinates), touching only
// pixels inside `clip` and inside slacklineBounds. Needs the rasterizer scratch.
// Colors: TENT_LIT for the line and the wraps, MUSHROOM for the knot.
void slacklineDraw(const Slackline& s, Surface& strip, const Rect& clip, const Palette& pal);

// ---- the walker ----------------------------------------------------------------------------

enum SlackWalkMode : uint8_t {
  SW_OFF = 0,    // not his: the scene owns the hiker
  SW_MOUNT,      // stepping up onto the line from the ground (about 1.5 s), then SW_STAND
  SW_STAND,      // balancing in place, swaying
  SW_WALK,       // taking steps (slackWalkerWalk), then SW_STAND
  SW_FLOURISH,   // the middle-of-the-line pose: one arm up, the line bounces, a little bow (4.8 s), then SW_STAND
  SW_FALL,       // a fall (about 1.7 s) or a soft dismount (about 1.2 s), then SW_OFF with him on the ground
  SW_MODES
};

enum SlackWant : uint8_t { SW_WANT_NONE = 0, SW_WANT_FLOURISH, SW_WANT_FALL, SW_WANT_DISMOUNT, SW_WANT_STAND };

// POD, about 150 bytes.
struct SlackWalker {
  uint8_t mode;        // SlackWalkMode
  uint8_t swing;       // the foot (0 near, 1 far) that steps next
  uint8_t flip;        // 0: the near arm is the forward one when his arms are out; 1: the back one
  uint8_t hard;        // SW_FALL: 1 a fall, 0 a controlled step off
  int8_t dir;          // +1 faces and walks toward +x, -1 toward -x
  int8_t tip;          // SW_FALL: +1 tips forward, -1 backward
  uint8_t kicked;      // SW_FALL: the push-off has been given to the line
  uint8_t want;        // SW_WALK: a command waiting for the step in hand to end (SW_WANT_*)
  int16_t steps_left;  // SW_WALK: steps still to take; -1 on toward the far end
  float t;             // seconds in this mode
  float step_u;        // 0..1 through the current step; < 0 between steps
  float step_x0;       // x the swinging foot started from
  float blend;         // 0..1 weight of his pose over the composed hiker pose
  float x;             // hips x: the rig's feet.x
  float fx[2];         // each foot's x
  float on[2];         // each foot: 0 on the ground .. 1 on the line (a mount or a fall passes through)
  float lift[2];       // each foot's height above what it stands on, px
  float fx0[2];        // feet at the start of the mode
  float x0;            // hips at the start of the mode
  float ground_y;      // the ground he steps up from and lands on
  float far_x;         // SW_WALK to the end: he stops stepping here
  float wob, wob_to;   // unease 0 calm .. 1 about to fall; eased toward wob_to
  float ph;            // sway clock, turns
  float load;          // the weight he put on the line this tick
};

// Not on the line, not his; `rng` is drawn once (the sway phase and which arm
// goes forward).
void slackWalkerInit(SlackWalker& w, Rng& rng);

// ---- commands (each sets the mode; call once) ----

// He is on the ground at `feet` (his ground y), by the line, facing `dir`
// (+1 toward +x): he steps up onto it and balances. ~1.5 s. Mount a few steps
// in from an anchor, where the line is low enough to step onto.
void slackWalkerMount(SlackWalker& w, float feet_x, float ground_y, int dir);
// On the line: take `steps` steps (about 0.5 s and 10 px of hips each, slower
// when he wobbles), then stand; steps <= 0: on toward the far anchor and stop
// about 36 px from it.
void slackWalkerWalk(SlackWalker& w, const Slackline& line, int steps);
// How unsteady: 0 calm balancing (small sways), about 0.5 a wobble, 1 flailing
// about to fall. Eases in about 1 s; a script raises it before a fall.
void slackWalkerWobble(SlackWalker& w, float level);
// The four commands below work on a walker who is standing or walking on the
// line (anything else ignores them). Walking, he finishes the step in hand
// first, so no foot is ever snapped down.
// Stop stepping and stand.
void slackWalkerStand(SlackWalker& w);
// The middle-of-the-line pose: one arm up, the line bounces, a little bow. 4.8 s.
void slackWalkerFlourish(SlackWalker& w);
// He tips sideways (in profile: leans over and flings his arms), steps off onto
// the ground and lands on his feet with a knee bend. The push-off kicks the
// line. Draws one value (which way he tips) whether or not it takes. About
// 2 s; ends SW_OFF, standing on the ground.
void slackWalkerFall(SlackWalker& w, Rng& rng);
// Step off forward onto the ground, calmly. About 1.4 s; ends SW_OFF.
void slackWalkerDismount(SlackWalker& w);

// One tick: moves his feet, puts his weight on the line (slacklineLoad) and
// kicks it at a push-off. Call before slacklineStep.
void slackWalkerStep(SlackWalker& w, Slackline& line, float dt);

bool slackWalkerOnLine(const SlackWalker& w);   // some weight on the line (mount ... before the step-off)
inline bool slackWalkerBusy(const SlackWalker& w) { return w.mode != SW_OFF; }

// Bend `pose` (the composed hiker pose, in and out) into the balancing figure
// for his state, and give the feet (the rig's feet.x and feet.y: the lowest foot
// sole on what it stands on) for a hiker of `height` px. Each foot stands on the
// line's top edge at its x (or on the ground, or lifted in a step) within a
// fraction of a px; the legs are solved to reach it, so knees bend as he sags.
// No-op in SW_OFF.
void slackWalkerPose(const SlackWalker& w, const Slackline& line, float height, HikerPose& pose, Vec2& feet);
// slackWalkerPose on h.pose, then h.feet and h.facing set. Call after hikerTick.
void slackWalkerApply(const SlackWalker& w, const Slackline& line, Hiker& h);

// ---- the critter ----------------------------------------------------------------------------

enum SlackCross : uint8_t {
  SLACK_CROSS_BOTH = 0,   // start end -> far end, a look, back to the middle, sit, back to the start end
  SLACK_CROSS_HALF,       // start end -> middle, sit, back to the start end
  SLACK_CROSS_PLANS
};
enum SlackCritState : uint8_t { SC_TROT = 0, SC_LOOK, SC_SIT, SC_DONE };

// POD, about 60 bytes.
struct SlackCritter {
  uint8_t kind;       // CR_SQUIRREL or CR_CHIPMUNK
  uint8_t plan;       // SlackCross
  uint8_t leg;        // which leg of the plan
  uint8_t state;      // SlackCritState
  int8_t dir;         // +1 trots toward +x, -1 toward -x (the way it faces)
  uint8_t pad[3];
  float t;            // seconds in this state
  float x, v;         // feet x, px/s (signed)
  float goal;         // x it is trotting to
  float vmax;         // px/s
  float weight;       // what it puts on the line
  float sit_s;        // how long it sits
  float start_x, far_x, mid_x;
};

// A critter that begins on the line at the `from_end` anchor (-1 the left
// anchor, +1 the right) and runs `plan`. `kind` is CR_SQUIRREL or CR_CHIPMUNK
// (anything else counts as a chipmunk). Draws one value: the sit time (4 to 6 s)
// and a few percent of speed. The scene spawns a rig critter at slackCritterFeet.
void slackCritterInit(SlackCritter& s, const Slackline& line, CritterKind kind, int from_end, SlackCross plan, Rng& rng);
// One tick: eased trotting (about 0.5 s to speed and to stop), a look at the
// far end, the sit in the middle; loads the line lightly. Before slacklineStep.
void slackCritterStep(SlackCritter& s, Slackline& line, float dt);
bool slackCritterDone(const SlackCritter& s);      // back at the start end, finished
bool slackCritterSitting(const SlackCritter& s);   // sitting in the middle (the walker watches now)
// Feet on the line's top edge at its x.
Vec2 slackCritterFeet(const SlackCritter& s, const Slackline& line);
// The rig commands (call before critterTick): the run or trot gait toward
// `goal` at the eased speed, a sit, a look. Uses only critterSetAction and
// critterMoveTo, and only when the wanted action changes.
void slackCritterDrive(const SlackCritter& s, Critter& c);
// After critterTick: c.feet.x to the module's x (so the ease and the line agree)
// and c.feet.y on the line.
void slackCritterPlace(const SlackCritter& s, const Slackline& line, Critter& c);

}  // namespace ss
