// Morning Tai Chi, the module: the form as key tables, solved into the hiker's pose every tick from one
// clock (TAICHI_PLAN.md sections 4 and 8; Mike, 2026-11-18: "I would like the Tai Chi to be a slow
// progression of many exercises and not be in a hurry to finish").
//
// OWNERSHIP: skeleton made by the foundation step (A1); filled in by step B2 (the module). Pure: no World,
// no RNG, no clock but its own. The World side (the major, the script, the guests, the hooks day.cpp calls)
// is day_taichi.h, step C2, and calls only what is declared here.
//
// How it moves (TAICHI_PLAN 4, with REVIEW_FIXES F20):
// - A chapter is a table of keys (TaiKey): where each foot stands along the practice line, how high it is
//   held, where the hips are, how low he sinks, the lean, the head, a hand posture, the facing, seconds to
//   arrive and seconds held. Feet are in line coordinates, so a planted foot is the same number in every
//   key it stays down for, and it does not move at all.
// - Every channel is a curve through the keys: a key held 1 s or more is a real stop (both ends of its hold
//   have zero speed); a shorter hold is passed through at speed (the movement flows through it). Speeds at
//   pass-through keys are limited so nothing overshoots (a reversal passes through zero speed, as a body does).
// - The waist leads and the rest follows: hips and legs at t, the lean 0.10 s later, the hands 0.25 s later,
//   the head 0.40 s later.
// - A step: the hips go over the standing foot first, the other foot travels in the middle of the move on a
//   low arc, and the weight goes on to its new place only after it has landed.
// - Legs by two-bone IK on the line (no foot ever slides); the hips come down as far as needed so no knee
//   locks. Arms by IK at the keys, with the elbow on the low side (F20: a hand behind the shoulder takes a
//   negative elbow bend), then interpolated as joint angles so a hand travelling from behind to a push folds
//   in past the shoulder instead of flipping its elbow. Nothing calls libm's trig: table sines, sqrtf and the
//   module's own atan2.
// - Never a dead frame: a slow drift on the hips and hands, a hair more sink in a hold, and a 6 s breath.
//
// Chapters are 0..3 here (the plan's 1..4): 0 the opening, 1 the sparrow's tail, 2 clouds and the rooster,
// 3 the kick and the close.
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"
#include "hiker.h"

namespace ss {

constexpr int TAICHI_CHAPTERS = 4;
constexpr int TAICHI_MAX_KEYS = 26;

// Hand postures (TAICHI_PLAN 4.4, hand positions from the shoulder joint in units of H, x forward, y down),
// then the four the gags use. TaiKey::hands may add TH_SWAP: the two hands trade places.
enum TaiHands : uint8_t {
  TH_HANG, TH_DOWN, TH_RAISE, TH_LOWER, TH_BALL, TH_MANE, TH_CRANE, TH_WIND, TH_BRUSH, TH_LUTE,
  TH_MONK_OPEN, TH_MONK_PASS, TH_WARD, TH_ROLL, TH_PRESS, TH_WITHDRAW, TH_PUSH, TH_WHIP_GATHER, TH_WHIP,
  TH_CLOUD, TH_SNAKE, TH_RISE, TH_ROOSTER, TH_HIGHPAT, TH_CROSS, TH_KICK, TH_NEEDLE_UP, TH_NEEDLE, TH_FAN,
  TH_ARMSOUT,   // the wobble and the bump: both arms flung out for balance
  TH_SCRATCH,   // the wobble: a hand to the back of his head
  TH_SLAP,      // the itch: hand 0 at his neck
  TH_PAT,       // Copper's bump: hand 0 down to the dog's head
  TH_CLOUD_MID, // the clouds' way point: hand 0 coming back and down close to him, hand 1 going out and up
  TH_COUNT
};
constexpr uint8_t TH_SWAP = 0x80;

// What the module tells the major (taichiEvents reads and clears them). The first six are what a key's
// cue_in and cue_out can carry (TAICHI_PLAN 5.2's cues, named by role so one set serves four chapters):
//   TE_GUEST     key 3 of every chapter: the guest may come (the heron, the chipmunk, the deer)
//   TE_SIGN      the chapter's signature form arrives: crane (0), single whip 1.5 s into its hold (1, the
//                bird), snake (2), kick (3)
//   TE_SIGN_END  its hold ends: crane, whip, kick
//   TE_SIGN2     the second: rooster (2), needle (3)
//   TE_SIGN2_END the rooster's hold ends
//   TE_CLOSING   the closing begins (the guests leave, the butterfly settles)
enum TaiEvent : uint16_t {
  TE_GUEST = 1, TE_SIGN = 2, TE_SIGN_END = 4, TE_SIGN2 = 8, TE_SIGN2_END = 16, TE_CLOSING = 32,
  TE_DONE = 64,         // the last hold is over; the weight eases back to the rig over 1.5 s
  TE_GAG = 128,         // a gag began (the guests do it better)
  TE_GAG_END = 256,     // and ended
  TE_BUMP_CALL = 512,   // Copper's bump: send the dog now; call taichiGag(TG_BUMP, side) when he arrives
  TE_TURN = 1024,       // he turned round (the facing flipped)
};

enum TaiKeyFlag : uint8_t {
  TK_TURN = 1,      // he turns round in this key's hold: the key lists the facing he arrives with
  TK_BALANCE = 2,   // the chapter's balance form: the wobble plays here
  TK_LONGEST = 4,   // the chapter's longest form hold: the itch plays here
  TK_BUMP = 8,      // where Copper bumps him
  TK_BOW = 16,      // the last close: a small bow of the head to the stream
  TK_STOP = 32,     // a full stop even if the hold is short (the gags' own keys)
  TK_H0 = 64,       // gag keys: extra holds a posture for hand 0 alone; hand 1 keeps the key's
  TK_HMID = 128,    // with TK_H0: hand 0 halfway between the key's own hand 0 and extra's (a way point)
};

enum TaiGag : uint8_t { TG_NONE, TG_WOBBLE, TG_ITCH, TG_BUMP };
enum TaiPhase : uint8_t { TP_OFF, TP_FORM, TP_GAG, TP_WAIT, TP_OUT, TP_DONE };

struct TaiKey {               // 16 bytes, const tables in flash
  int8_t a, b, hip;           // foot A (rig leg 0), foot B (leg 1), the hips: 0.01 H east of x0
  uint8_t la, lb;             // how high each foot is held, 0.004 H (0 planted)
  uint8_t sink;               // hips lowered from standing, 0.005 H
  int8_t lean, head;          // 0.01 rad (rig convention: + forward, + nods down)
  uint8_t hands;              // TaiHands, | TH_SWAP
  int8_t face;                // +1 east, -1 west
  uint8_t move, hold;         // 0.1 s
  uint8_t cue_in, cue_out;    // TaiEvent bits raised on arrival (after `extra` tenths) and at the hold's end
  uint8_t flags;              // TaiKeyFlag
  uint8_t extra;              // form keys: cue_in's delay into the hold, 0.1 s; gag keys: TK_H0's posture
};
static_assert(sizeof(TaiKey) == 16, "a key is 16 bytes");

struct TaiHandsDef { int8_t x0, y0, x1, y1; };   // hand 0 and hand 1, 0.005 H from the shoulder

struct TaichiState {
  uint8_t chapter, gag, phase, key;   // key: the key he is arriving at or holding (form time)
  uint8_t gag_on;                     // the gag playing (TaiGag), 0 none
  uint8_t gag_done;                   // the planned gag has had its turn
  int8_t dir;                         // +1: the line runs east from x0 (the plan); -1 mirrored
  int8_t bump_side;                   // the way Copper pushed, +1 east
  uint16_t events;                    // TaiEvent bits not yet read
  int8_t vary_sink, vary_hand;        // small per-loop variation, -3..3 (tenths of 0.005 H / 0.01 H)
  uint8_t fly_on, fly_mode;           // the butterfly
  uint8_t turn_aid_off;               // stills only: 1 draws the turn without foot_turn and the straight knees
  int8_t facing;                      // his facing this tick, frame (+1 right)
  float clk;                          // seconds since taichiInit (the drift's clock; stops when he does)
  float t;                            // form seconds (stand still through a gag and the bump's wait)
  float gag_t;                        // seconds into the gag, or into the bump's wait
  float gag_len;                      // seconds the playing gag lasts
  float tail;                         // stillness owed at the end (a bump that never came)
  float out_t;                        // seconds of the ease back to the rig
  float w;                            // the module's weight over the rig's own pose
  float x0, y0, g;                    // the line: start, ground, along-the-line scale (pine clearance)
  float hold_sink;                    // the hold's extra sink, frozen while a gag plays
  float foot_turn;
  float hip_x;                        // frame x of the hips this tick
  float ground;                       // frame y of the ground line the rig stood him on this tick
  Vec2 hand[2];                       // the hands this tick, frame space (the bird, the butterfly)
  Vec2 shoulder;                      // the shoulder joint, frame space
  Vec2 perch;                         // the top of his back behind the head, frame space (where the butterfly settles)
  Vec2 foot[2];                       // each foot: ankle x, lowest point y, frame space (the tests)
  HikerPose out;                      // this tick's solved pose (before the rig's own breathing)
  Vec2 fly;                           // the butterfly's body, frame space
  float fly_t;                        // seconds in its present mode
  float fly_open;                     // its wings, 0 shut .. 1 flat open
  float fly_dir;                      // +1 flying east, -1 west (which way its wings point)
};

// ---- the form ----

// Start a session: chapter 0..3, gag (TaiGag; TG_WOBBLE on chapter 1, which has no balance form, plays the
// itch), the line's start x0 and ground y0 (frame px), g (along-the-line scale, 0.93..1), dir (+1 the line
// runs east), vary (a hash of the layout and the loop count: small changes in sink and hand height, never
// in timing). The weight comes up over the first 1.5 s of the opening stillness.
void taichiInit(TaichiState& s, uint8_t chapter, uint8_t gag, float x0, float y0, float g, int dir = 1,
                uint32_t vary = 0);
// Advance the module's clock (the major's tick: so the doorbell freezes it). dt 0 changes nothing.
void taichiStep(TaichiState& s, float dt);
// Copper has arrived at his legs (after TE_BUMP_CALL): play the bump, side +1 when pushed east. False when
// he is not waiting for one (the wait is over: then the 12 s become stillness at the end).
bool taichiGag(TaichiState& s, uint8_t kind, int side);
// Write the hiker's whole pose, feet, facing, foot_turn and breathing from the state (after hikerTick). A pure
// function of the state and the rig's own pose (which it mixes in by w): call it as often as you like.
void taichiPose(TaichiState& s, Hiker& h);
// Put back what taichiPose took: foot_turn 0, breathing 1 and 1.
void taichiFinish(TaichiState& s, Hiker& h);
uint16_t taichiEvents(TaichiState& s);         // TaiEvent bits since the last call; clears them
bool taichiInForm(const TaichiState& s);       // from init until the weight is back with the rig
bool taichiDone(const TaichiState& s);         // the weight is back with the rig: the script may go on
// He is standing still in a hold of at least min_hold seconds (or waiting for Copper): REVIEW_FIXES F8, the
// morning's fog steps wait for one of 2 s or more, because stillness hides a palette rebuild's stutter.
bool taichiHolding(const TaichiState& s, float min_hold);

float taichiFormSeconds(int chapter);          // the form alone: 105.8, 115.3, 107.7, 107.6 at pace 1
float taichiGagSeconds(int chapter, uint8_t gag);   // what a gag adds: wobble 8, itch 6, bump 12
float taichiOutSeconds();                      // the ease back to the rig after TE_DONE: 1.5
float taichiPace();                            // kTaiPace (TAICHI_PLAN 2): 1 now

// Tables and geometry, for the tests, the tools and day_taichi.cpp.
int taichiKeyCount(int chapter);
const TaiKey& taichiKey(int chapter, int k);
float taichiKeyArrive(int chapter, int k);     // form seconds when key k is reached
float taichiKeyHoldEnd(int chapter, int k);    // and when its hold ends
const TaiHandsDef& taichiHandsDef(int posture);
Vec2 taichiFootFrame(const TaichiState& s, int i);   // foot i after the last taichiPose: ankle x, lowest y
// The module's own atan2 (polynomial, max error about 1e-5 rad), so no libm call can differ on the board.
float taiAtan2(float y, float x);

// ---- the butterfly (TAICHI_PLAN 6.1): the module's own two-triangle flier, drawn as day part 1 ----

void taichiFlyEnable(TaichiState& s, bool on);   // before the commencing: warm months, guests on
void taichiFlyLeave(TaichiState& s);             // off upward now (he walks off); it also goes by itself
// Ambient: called every tick from the day step, also while the doorbell freezes the story (it keeps
// circling round a frozen hand). `tick` is World::tick (its flutter's clock).
void taichiFlyStep(TaichiState& s, uint32_t tick);
bool taichiFlyShown(const TaichiState& s);
Rect taichiFlyBounds(const TaichiState& s);      // empty when not shown
void taichiFlyDraw(const TaichiState& s, Surface& surf, const Rect& clip, const Palette& pal);
constexpr int TAICHI_FLY_PRIMS = 2;
// The rows to mark between two states (the butterfly only: the World marks him every tick anyway).
#define TAICHI_DIRTY_MAX 2
int taichiDirty(const TaichiState& prev, const TaichiState& cur, Rect* out);

}  // namespace ss
