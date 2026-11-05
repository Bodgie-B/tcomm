// The archery (morning and daytime plan, section 7): a straw target on a stand,
// a bow with a 1 px string, up to four arrows that fly a shallow arc and stick,
// and the archer's pose overlay for a real hiker rig. A standalone module like
// the kite: plain data, commands, a step function, bounds and draws. It owns no
// World state, calls no platform code and draws no random numbers except
// through the Rng a command is handed; the caller supplies the two hands every
// tick.
//
// Parts and what draws them:
//   target   a 34 px straw disc (darker straw edge, three rings in MUSHROOM and
//            TENT_LIT) on two TRUNK leg capsules, placed by the caller
//   bow      a tapered arc of wood (TRUNK, so it shows against his black
//            silhouette when it hangs at his side) in the near hand, a 1 px
//            ACTOR string that pulls
//            back to the far hand with the draw and buzzes after the release,
//            and the nocked arrow
//   arrows   ACTOR shaft, 28 px, with a small fletch; up to ARCHERY_ARROWS in
//            flight or stuck at once; they quiver for half a second when they
//            land (a split one splays into two halves)
//   archer   archeryPose bends the composed HikerPose into the draw
//
// What the caller does each tick, in order (the order matters: the hands it
// reads must be the ones the rig draws):
//   hikerTick(h, dt);                           // his pose, as always
//   archeryPose(a, h.pose);                     // lift the bow, draw, hold, release
//   archeryStep(a, hikerHandPos(h, 0), hikerHandPos(h, 1), dt);   // near hand: bow; far hand: string
//   mark dirty (archeryDirty), draw (hikerDraw, then archeryDraw)
// `a.dir` must be his facing (archeryInit takes it; archeryFace changes it).
// archeryPose is a no-op while he is not in a shot, so it is safe to call every
// tick. The rig keeps his own pose between ticks, so nothing accumulates.
//
// Shots. A shot is queued with archeryShoot(a, kind, rng) (at most 4 in the
// queue; the queue restarts when the archer is idle and it has all been played)
// and the module plays them back to back, each with the same rhythm (PROPOSED):
//   RAISE 0.9 s   the bow comes up, the nocked arrow appears (the anticipation)
//   DRAW 1.3 s    the string hand goes back to the cheek
//   HOLD 1.5..2.2 s   a long hold, with a tiny tremble that grows
//   RELEASE 0.7 s the arrow leaves; the string hand flicks back, the bow arm
//                 follows through, the string buzzes
//   FOLLOW        he watches until the arrow has landed
//   REACT 1.6 s   (2.4 s after the last shot) lowers the bow; proud after a hit
//                 (chest out, chin up, a fist pump), a head-scratch after a miss
//   PAUSE 0.5 s   then the next RAISE, or idle
// The kinds:
//   SHOT_SHORT     the arrow comes down in the ground short of the target
//   SHOT_PAST      it flies past the target (behind it, so the target hides it
//                  for a moment), lands beyond it and slides to a stop
//   SHOT_TREE      it clears the target and sticks in the trunk at the caller's
//                  `trunk` point, for good
//   SHOT_BULLSEYE  in the inner ring (within ARCHERY_R_INNER of the centre)
//   SHOT_RING      in the middle or outer ring
//   SHOT_SPLIT     lands on the last bullseye arrow; its shaft splits in two
//                  halves that splay apart. With no bullseye to split it is a
//                  bullseye.
// Each archeryShoot draws exactly three values from the Rng, whatever the kind
// (where it lands within its zone, the angle, how long he holds).
//
// Scripting the three variants of the plan (a scene's job; archeryPlay does the
// queueing for you and also draws a fixed twelve values):
//   A  two misses, then a bullseye, then a small proud pose:
//        archeryEquip(a, true); archeryShoot(a, SHOT_SHORT, rng); archeryShoot(a, SHOT_PAST, rng);
//        archeryShoot(a, SHOT_BULLSEYE, rng);   then run archeryStep until archeryDone(a).
//   B  an arrow over the target into the tree, for good:
//        archeryShoot(a, SHOT_TREE, rng); the arrow sticks at a.trunk and never moves again.
//   C  a bullseye, then a second arrow that splits the first:
//        archeryShoot(a, SHOT_BULLSEYE, rng); archeryShoot(a, SHOT_SPLIT, rng).
// The events a step raises (a.events, cleared at the start of each step) let a
// scene cue things: AEV_RELEASED when the arrow leaves, AEV_LANDED when one
// sticks (a.last_end and a.last_ring say where), AEV_SPLIT, AEV_DONE once when
// everything has settled. After DONE the arrows stay; archeryClear removes them.
//
// Drawing and dirty marking. Every archery draw is a pure function of the state,
// the palette and the clip; everything it reads is quantized to half pixels and
// small angle steps, so a tick that changes nothing visible changes nothing on
// screen. archeryBounds is everything drawn; archeryDirty is cheaper: the areas
// that differ between last tick's state and this tick's, one per part (bow,
// target, each arrow).
//
// Rules kept (HOW_TO_ADD_SCENES.md sections 1.4, 1.5, 8): plain data by value,
// no allocation, no clock but the caller's dt, single precision, table sines,
// no libm trig. Needs the rasterizer scratch (rasterSetScratch) to draw.
//
// Numbers marked PROPOSED in archery.cpp are guesses; everything about how it
// looks is UNVERIFIED until Mike has seen it (and on the panel).
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "hiker.h"

namespace ss {

#define ARCHERY_ARROWS     4
#define ARCHERY_DIRTY_MAX  (ARCHERY_ARROWS + 2)

// The target's size, px (PROPOSED). The ring radii are what archeryRingAt tests.
constexpr float kArcheryTargetR = 17.f;      // the straw disc
constexpr float ARCHERY_R_INNER = 4.6f;      // the bullseye
constexpr float ARCHERY_R_MID = 8.6f;
constexpr float ARCHERY_R_OUTER = 12.8f;
constexpr float kArcheryArrowLen = 28.f;     // PROPOSED: the rig's draw length is about 24 px

enum ArcheryVariant : uint8_t { ARCH_A = 0, ARCH_B, ARCH_C, ARCH_VARIANTS };

enum ArcheryShot : uint8_t { SHOT_SHORT = 0, SHOT_PAST, SHOT_TREE, SHOT_BULLSEYE, SHOT_RING, SHOT_SPLIT, SHOT_KINDS };

enum ArcheryPhase : uint8_t { AP_IDLE = 0, AP_RAISE, AP_DRAW, AP_HOLD, AP_RELEASE, AP_FOLLOW, AP_REACT, AP_PAUSE };

enum ArcheryEnd : uint8_t { AEND_NONE = 0, AEND_TARGET, AEND_GROUND, AEND_PAST, AEND_TREE };

enum ArrowMode : uint8_t { ARROW_FREE = 0, ARROW_FLY, ARROW_STUCK, ARROW_SPLIT };

enum ArcheryEvent : uint8_t { AEV_RELEASED = 1, AEV_LANDED = 2, AEV_SPLIT = 4, AEV_DONE = 8 };

// One arrow. Plain data; the flight is a function of `t`: tip(s) is the chord
// from `from` to `to` lifted by a parabola `arc` px high at the middle.
struct ArcheryArrow {
  uint8_t mode;       // ArrowMode
  uint8_t end;        // ArcheryEnd it flies to, or ended in
  uint8_t ring;       // AEND_TARGET: 0 inner .. 3 straw face (archeryRingAt)
  int8_t victim;      // SHOT_SPLIT: the arrow it splits, -1 none
  float t;            // seconds since release
  float flight;       // seconds the flight takes
  float arc;          // px
  float slide;        // AEND_PAST: px it slides along the ground after landing
  float amp;          // quiver amplitude, rad
  float split_t;      // seconds since it was split (ARROW_SPLIT)
  float spread;       // ARROW_SPLIT: the halves' half-angle now, rad
  Vec2 from, to;      // tip at release, tip at rest (before any slide)
  Vec2 dir_end;       // unit, tail to tip, at the landing
  Vec2 tip, dir;      // now: tip, and unit tail-to-tip
};

// POD, 440 bytes. Lives by value in the World (and once more in
// WorldDirty for archeryDirty).
struct ArcheryState {
  uint8_t phase;      // ArcheryPhase
  int8_t dir;         // +1 the target is on his right, -1 on his left (= his facing)
  uint8_t bow_on;     // the bow is in his hand
  uint8_t nocked;     // an arrow is on the string
  uint8_t target_on;
  uint8_t n_queue;    // shots queued (this run)
  uint8_t shot;       // the shot being played (n_queue when none)
  uint8_t react;      // 0 none, 1 proud, 2 shrug, for the reaction in progress
  uint8_t cur;        // the arrow slot of the shot in the air
  uint8_t events;     // ArcheryEvent bits from the last archeryStep
  uint8_t last_end;   // ArcheryEnd of the last arrow to land
  uint8_t last_ring;  // and its ring (AEND_TARGET)
  uint8_t was_done;   // AEV_DONE already raised
  uint8_t kinds[ARCHERY_ARROWS];     // ArcheryShot queue
  float jit[ARCHERY_ARROWS][3];      // the three Rng values of each queued shot
  float t;            // seconds in this phase
  float aim, draw;    // 0..1: the bow up; the string hand back
  float flick;        // 0..1: the release's flick and follow-through, decaying
  float buzz;         // 0..1: the string's vibration, decaying
  float shake;        // 0..1: the hold's tremble
  float proud, slump; // 0..1 reaction envelopes
  float elev;         // rad: how high the bow arm points for this shot
  float hold_len;     // seconds of HOLD for this shot
  float ph_a, ph_b;   // tremble phases, turns
  Vec2 target;        // the stand's ground point (centre of its feet)
  Vec2 trunk;         // the pine trunk point SHOT_TREE sticks in
  float ground_y;     // frame y where the ground arrows come down
  Vec2 bow_hand, string_hand;   // as of the last step
  ArcheryArrow arrows[ARCHERY_ARROWS];
};

// What archeryBend needs (all 0..1 unless noted); archeryPose fills it from the state.
struct ArcheryPoseIn {
  float aim = 0.f;      // 0 as the rig has him .. 1 the full archer's stance
  float draw = 0.f;     // 0 string hand at the string .. 1 at the cheek
  float flick = 0.f;    // the release's flick (string hand back, bow arm up)
  float shake = 0.f;    // hold tremble
  float elev = 0.f;     // rad above level the bow arm points
  float ph_a = 0.f, ph_b = 0.f;   // tremble phases, turns
  float proud = 0.f, slump = 0.f;
};

// An empty archer for a stand at `target` (its ground point), facing `dir`
// (+1 right, -1 left), whose SHOT_TREE arrows stick at `trunk`. Ground arrows
// land at y = target.y.
void archeryInit(ArcheryState& a, int dir, Vec2 target, Vec2 trunk);
void archeryFace(ArcheryState& a, int dir);   // his facing changed
void archeryEquip(ArcheryState& a, bool on);  // the bow in his hand, hanging at his side (or not)
void archeryShowTarget(ArcheryState& a, bool on);
// Remove every arrow, drop the queue, lower the bow (the target and the bow stay).
void archeryClear(ArcheryState& a);

// Queue one shot (plays at once if he is idle). False when the queue or the
// arrow slots are full. Equips the bow. Draws exactly 3 values from `rng`.
bool archeryShoot(ArcheryState& a, uint8_t kind, Rng& rng);
// Queue variant A, B or C (see above): clears first, then draws exactly 12
// values from `rng` whatever the variant.
bool archeryPlay(ArcheryState& a, uint8_t variant, Rng& rng);

// One tick. `bow_hand` is his near hand (hikerHandPos(h, 0)), `string_hand` the
// far one; both after archeryPose. Any dt up to 0.1 s works.
void archeryStep(ArcheryState& a, Vec2 bow_hand, Vec2 string_hand, float dt);

// Nothing will change any more: no shot queued or playing, every arrow still.
bool archeryDone(const ArcheryState& a);
// A shot is in progress or queued.
bool archeryBusy(const ArcheryState& a);

// The ring a point is in: 0 inner, 1 middle, 2 outer, 3 the straw face, 4 off the target.
int archeryRingAt(const ArcheryState& a, Vec2 p);
// The target's centre as drawn.
Vec2 archeryTargetCenter(const ArcheryState& a);

// ---- the archer's pose ----

// Bend a composed pose (feet apart, lean back a touch, bow arm out toward the
// target, string hand from the string to the cheek, release, proud or shrug).
// Only lean, head, legs and arms change. Angles follow the rig (hiker.h:
// absolute from straight down, + toward the facing), so it is facing independent.
void archeryBend(HikerPose& p, const ArcheryPoseIn& in);
// The same from a state: a no-op when aim, proud and slump are all 0.
void archeryPose(const ArcheryState& a, HikerPose& p);

// ---- bounds, draw ----

// Every pixel archeryDraw can touch, to the antialiased fringe: the union of
// the parts. Empty when nothing is drawn. Not clamped to the frame.
Rect archeryBounds(const ArcheryState& a);
Rect archeryTargetBounds(const ArcheryState& a);
Rect archeryBowBounds(const ArcheryState& a);          // the bow, string and nocked arrow
Rect archeryArrowBounds(const ArcheryState& a, int i);

// True when the two states draw exactly the same pixels.
bool archerySame(const ArcheryState& a, const ArcheryState& b);

// The areas to mark dirty when the archery goes from `before` to `after`: where
// the bow, the target and each arrow were and are. Returns how many rects (0
// when nothing visible changed, at most ARCHERY_DIRTY_MAX). Together they cover
// every pixel that differs between the two draws.
int archeryDirty(const ArcheryState& before, const ArcheryState& after, Rect out[ARCHERY_DIRTY_MAX]);

// Draw onto `strip` (a strip or a whole frame; frame coordinates), touching
// only pixels inside `clip` and inside archeryBounds. Needs the rasterizer
// scratch. archeryDraw is the lot in order: the arrows that fly behind the
// target, the target, the other arrows, the bow (draw him before it). The parts
// are separate so a scene can sort the target among its ground actors.
// Colors: TRUNK for the bow limb and the target's legs; ACTOR for the string and
// the shafts; MUSHROOM, TENT_LIT and a straw (and a darker straw edge) mixed from
// TENT_LIT and TRUNK for the target; TENT_LIT for the fletch.
void archeryDraw(const ArcheryState& a, Surface& strip, const Rect& clip, const Palette& pal);
void archeryTargetDraw(const ArcheryState& a, Surface& strip, const Rect& clip, const Palette& pal);
void archeryArrowsDraw(const ArcheryState& a, Surface& strip, const Rect& clip, const Palette& pal, bool behind_target);
void archeryBowDraw(const ArcheryState& a, Surface& strip, const Rect& clip, const Palette& pal);

}  // namespace ss
