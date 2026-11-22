// Morning Tai Chi, the module. See taichi.h.
//
// OWNERSHIP: step B2 (the module). Nothing in a shared file calls into it yet (day_taichi.cpp will, in
// step C2). Every look number here is PROPOSED until Mike has seen the stills; nothing is checked on the
// panel.
#include "taichi.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../gfx/raster.h"

namespace ss {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
constexpr float kHalfPi = 1.57079633f;
constexpr float kInvTwoPi = 1.f / kTwoPi;

// kTaiPace (TAICHI_PLAN 2): 1.15 would stretch every move and hold. Mike, 2026-11-18: "not be in a hurry
// to finish"; 1 is the plan's default until he has seen it.
constexpr float kPace = 1.0f;

// ---- the rig's own dimensions (hiker.cpp is frozen after A1; test_taichi checks this copy of its
// forward kinematics against hikerHandPos every tick; that function leaves out breath_scale, which the drawing
// uses, so the guests must read TaichiState::hand, which follows the drawing) ----
constexpr float kBone = 0.235f;                 // thigh and shin
constexpr float kUpper = 0.16f, kFore = 0.15f;
constexpr float kTorsoTop = 0.235f, kShoulderUp = 0.25f, kNeck = 0.29f;
constexpr float kHeadUp = 0.06f, kHeadFwd = 0.012f, kHeadR = 0.062f;
constexpr float kTorsoW = 0.15f, kShinW = 0.075f, kFootW = 0.058f;
constexpr float kHeel = 0.02f, kToe = 0.07f, kTurnFoot = 0.035f, kFootFollow = 0.75f;
constexpr float kBreathAmp = 0.011f;

// ---- the module's tuning (units of H, seconds, radians) ----
constexpr float kLegMax = 0.99f * 2.f * kBone;   // the longest a leg is let go: no knee ever locks
constexpr float kStand = 0.47f;                  // hip-to-ankle depth at sink 0 (then capped by kLegMax)
constexpr float kRigStand = 0.4695f;             // POSE_STAND's hip-to-ankle depth (thighs 0.06, knees 0.04)
constexpr float kReachMin = 0.06f, kReachMax = 0.305f;   // no arm dead straight (0.31 is full length)
constexpr float kLowHand = 0.12f;                // a hand lower than this below the shoulder keeps a forward elbow
constexpr float kLagLean = 0.10f, kLagHands = 0.25f, kLagHead = 0.40f;   // the waist leads, the head last
constexpr uint8_t kLongHold = 10;                // tenths: a hold this long is a full stop
constexpr float kStepArc = 0.035f;               // a stepping foot clears the ground by this much
constexpr float kLeave = 0.30f, kLand = 0.70f;   // a planted foot steps in this part of its move
constexpr float kLandB = 0.45f;                  // a foot already in the air lands by here
constexpr float kLeaveC = 0.40f;                 // a planted foot that ends in the air leaves here
constexpr float kDwell = 0.03f;                  // hips this near the standing foot as the other leaves
constexpr float kDwellOn = 0.06f;                // and as it lands, toward where they go (a foot is 0.09 H long)
constexpr float kReachDip = 0.03f;               // how much lower than the stance the hips may go to reach a step
constexpr float kInSeconds = 1.5f, kOutSeconds = 1.5f;
constexpr float kTurnRamp = 1.2f;                // the feet go end-on over this before and after a turn
// Through a turn the bare head tips back by atan(kHeadFwd / kHeadUp), which puts its centre straight over the
// neck: the rig sets the head 0.012 H ahead of the neck, so at the flip it jumped 2 px and about 85 pixels
// changed. With the hat off the head is a plain disc and the tilt itself cannot be seen (review, 2026-11-18).
constexpr float kHeadLevel = -0.19740f;
constexpr float kHoldSink = 0.006f;              // a hair deeper in every real stop
constexpr float kBreathRate = 0.6f, kBreathScale = 1.5f;   // a 6 s breath, about 1.5 px
constexpr float kHipDrift = 0.004f, kArmDrift = 0.02f;     // never a dead frame
constexpr float kVarySink = 0.005f / 3.f, kVaryHand = 0.01f / 3.f;
constexpr float kBumpWait = 3.4f;                // how long he holds the bump key waiting for Copper
constexpr float kWobbleAt = 1.5f, kItchAt = 1.0f, kBumpAt = 0.8f;   // seconds into the hold a gag starts
constexpr float kGagSeconds[4] = {0.f, 8.f, 6.f, 12.f};             // none, wobble, itch, bump
constexpr float kHop = 0.02f;                    // the wobble's one small hop
constexpr float kSitLean = 0.2f, kSitFree = 0.3f, kSitLeanMax = 0.12f;   // lean per radian of knee bend

// ---- the hand postures (TAICHI_PLAN 4.4), 0.005 H from the shoulder joint: hand 0, hand 1 ----
const TaiHandsDef kHands[TH_COUNT] = {
    {12, 60, 0, 61},      // HANG: arms at his sides
    {0, 61, 0, 61},       // DOWN: both straight down inside the body (the turns)
    {58, 4, 56, 8},       // RAISE: floated up to shoulder height
    {40, 44, 36, 46},     // LOWER: pressed down in front of the hips
    {38, -4, 38, 40},     // BALL: one hand over the other before the chest
    {60, -8, -4, 60},     // MANE: one forward at eye level, one pressed down by the hip
    {22, -56, 20, 56},    // CRANE: one above the head, one low
    {44, 6, -42, -8},     // WIND: one across the chest, one drawn back and up behind
    {60, 0, 22, 56},      // BRUSH: a push at shoulder height, the other brushed past the knee
    {60, -8, 42, 18},     // LUTE: both forward, one high and long, one by its elbow
    {60, 0, -42, -8},     // MONK_OPEN: one out ahead, one back and up
    {32, 48, 24, -12},    // MONK_PASS: the front hand to the hip, the back hand by the ear (clear of the head)
    {56, 10, 14, 58},     // WARD: a level forearm ahead of the chest
    {44, 30, 32, 42},     // ROLL: both drawn down toward the back hip
    {60, 6, 56, 8},       // PRESS: both forward together
    {38, 14, 36, 18},     // WITHDRAW: both back to the chest, elbows down
    {60, 2, 58, 6},       // PUSH: both palms out
    {44, 6, -44, 4},      // WHIP_GATHER: the hook hand going out behind
    {60, -2, -58, -10},   // WHIP: a palm ahead, the hook straight out behind, a little high
    {48, -12, 24, 46},    // CLOUD: one out before the face, one close in at the belly (the wheel's top and bottom)
    {48, 36, -54, -18},   // SNAKE: the front hand down along the leg, the hook high behind
    {56, 12, -20, 54},    // RISE: coming up out of the snake
    {34, -10, 12, 58},    // ROOSTER: a forearm upright over the raised knee, the other pressed down
    {60, -12, 30, 32},    // HIGHPAT: a palm out at eye level, a palm up at the waist
    {38, 4, 34, 8},       // CROSS: wrists crossed before the chest
    {60, -4, -58, -8},    // KICK: both arms out level, ahead and behind
    {24, -14, 10, 56},    // NEEDLE_UP: a hand by the ear before it goes down
    {28, 54, -8, 58},     // NEEDLE: the hand pointing at the ground ahead
    {60, 0, 22, -52},     // FAN: a push ahead, the other arm arched over the head
    {52, -12, -52, -8},   // ARMSOUT: flung out for balance
    {-6, -22, 0, 61},     // SCRATCH: a hand to the back of his head
    {6, -12, 0, 61},      // SLAP: at his neck (hand 0 only)
    {20, 60, 0, 61},      // PAT: down to the dog's head (hand 0 only)
    {20, 16, 56, 22},     // CLOUD_MID: hand 0 coming back and down close to the chest, hand 1 going out and up
};

// ---- the chapters (TAICHI_PLAN 5.2; generated from its tables, then tuned on the stills) ----
// Changed from the plan on the first posture sheet (B2, 2026-11-18): every sink is about 60% of the plan's.
// A 2-bone leg needs a knee bend near 1 rad to lose the plan's 0.05 to 0.06 H, so both knees folded far in
// front of his hips and an upright torso read as leaning back. The snake is 0.27 H (not 0.25 with the hips
// over the middle: his seat nearly touched the ground) with the hips over the back foot, so the front leg
// reaches out long; the needle is 0.07 H (it read as a squat); the rise out of the snake 0.06 H. The crane's
// empty foot touches 0.13 H ahead of the standing one (the plan had 0.25: it sat him back on his heel), so
// the brush knee after it makes the real step forward. Every chapter's closing step leaves leg 0 (the near
// leg) in front, as the rig's own stand has it, so handing him back to the rig never swaps his feet: the
// stepping foot lands just behind the other (or passes it, in chapters 2 and 4). Chapter 4's high pat
// touches 0.18 H ahead, not 0.22 (out of reach: the knee locked); its half step leaves the hips halfway back
// (0.16, not 0.07) and the needle's 5 s move finishes the sit (all of it in 1.2 s was the fastest hip move);
// that half step brings the back foot to 0.09 (not 0.05), so the fan's stance is 0.28 H and his hips can
// reach over the front foot before the gather step without a squat.
// MONK_PASS's back hand is 0.12 H ahead (not 0.10: it touched the head) and WHIP's hook 0.05 up (not 0.07:
// its elbow rose over the shoulder).
// Step C2 (2026-11-19, the B2 reviewer's note): Wave Hands Like Clouds read almost static in silhouette (two
// hands going straight up and down in front of him). Each hand now wheels: out and up before the face, back
// and down close to the chest, with a way-point key (TH_CLOUD_MID) between each top and bottom. The clouds
// last the same 21.6 s, so the chapter's length and every later key's time are unchanged.
// a, b, hip (0.01 H); la, lb (0.004 H); sink (0.005 H); lean, head (0.01 rad); hands; face; move, hold
// (0.1 s); cue_in, cue_out; flags; extra (cue_in's delay into the hold, 0.1 s).
// clang-format off
const TaiKey kCh0[] = {   // the opening: 105.8 s. Guest: the heron.
    {   2,  -2,   0,   0,  0,   0,    2,   0, TH_HANG,  1,   0, 50, 0, 0, 0, 0},   // stillness (wuji)
    {   2,  -2,   0,   0,  0,   0,    0,  -3, TH_RAISE,  1,  50, 12, 0, 0, 0, 0},   // commencing: the arms float up
    {   2,  -2,   0,   0,  0,   4,    2,   3, TH_LOWER,  1,  50, 20, 0, 0, 0, 0},   // ... and press down as he sinks
    {   4,  -2,  -2,   3,  0,   6,    0,   5, TH_BALL,  1,  35,  4, TE_GUEST, 0, 0, 0},   // hold the ball
    {  27,  -2,  18,   0,  0,   6,    4,  -3, TH_MANE,  1,  55, 24, 0, 0, 0, 0},   // part the wild horse's mane, 1
    {  27,  -2,  10,   0,  0,   7,    0,   5, TH_BALL | TH_SWAP,  1,  30,  3, 0, 0, 0, 0},   // sit back, the ball
    {  27,  54,  45,   0,  0,   6,    4,  -3, TH_MANE | TH_SWAP,  1,  55, 24, 0, 0, 0, 0},   // part the wild horse's mane, 2
    {  44,  54,  44,   0,  0,   6,    0,   5, TH_BALL,  1,  35,  3, 0, 0, 0, 0},   // half step up, the ball
    {  44,  54,  41,   0,  3,   7,   -2, -10, TH_CRANE,  1,  55, 50, TE_SIGN, TE_SIGN_END, TK_BALANCE | TK_LONGEST, 0},   // white crane spreads its wings
    {  44,  54,  41,   0,  3,   7,    0,   0, TH_WIND,  1,  35,  3, 0, 0, 0, 0},   // wind up
    {  44,  66,  56,   0,  0,   6,    4,   0, TH_BRUSH | TH_SWAP,  1,  55, 24, 0, 0, TK_BUMP, 0},   // brush knee and push
    {  44,  66,  43,   0,  3,   7,   -2,   0, TH_LUTE | TH_SWAP,  1,  45, 24, 0, 0, 0, 0},   // playing the lute
    {  44,  66,  43,   0,  3,   7,    0,   0, TH_MONK_OPEN | TH_SWAP,  1,  35,  3, 0, 0, 0, 0},   // repulse monkey: open
    {  44,  43,  40,   0,  8,   6,    0,   0, TH_MONK_PASS | TH_SWAP,  1,  25,  0, 0, 0, 0, 0},   // ... the foot passes back
    {  44,  20,  23,   3,  0,   7,   -2,   0, TH_MONK_OPEN,  1,  30, 12, 0, 0, 0, 0},   // repulse monkey, 1
    {  21,  20,  19,   8,  0,   6,    0,   0, TH_MONK_PASS,  1,  25,  0, 0, 0, 0, 0},   // ... the foot passes back
    {  -4,  20,  -1,   0,  3,   7,   -2,   0, TH_MONK_OPEN | TH_SWAP,  1,  30, 12, 0, 0, 0, 0},   // repulse monkey, 2
    {  -4,  -8,  -6,   0,  0,   4,    0,   3, TH_LOWER,  1,  45,  5, TE_CLOSING, 0, 0, 0},   // closing: feet together, hands down
    {  -4,  -8,  -6,   0,  0,   0,    2,   0, TH_HANG,  1,  45, 50, 0, 0, 0, 0},   // ... he stands; three breaths
};
const TaiKey kCh1[] = {   // the sparrow's tail: 115.3 s. Guest: the bird on the hook hand.
    {   2,  -2,   0,   0,  0,   0,    2,   0, TH_HANG,  1,   0, 40, 0, 0, 0, 0},   // stillness
    {   2,  -2,   0,   0,  0,   0,    0,  -3, TH_RAISE,  1,  50, 10, 0, 0, 0, 0},   // commencing: up
    {   2,  -2,   0,   0,  0,   4,    2,   3, TH_LOWER,  1,  50, 15, 0, 0, 0, 0},   // ... and down
    {   4,  -2,  -2,   3,  0,   6,    0,   0, TH_WIND,  1,  35,  3, TE_GUEST, 0, 0, 0},   // wind up
    {  27,  -2,  18,   0,  0,   6,    4,   0, TH_BRUSH | TH_SWAP,  1,  55, 24, 0, 0, 0, 0},   // brush knee and push, 1
    {  27,  -2,  12,   0,  0,   7,    0,   0, TH_WIND | TH_SWAP,  1,  30,  3, 0, 0, 0, 0},   // sit back, wind up
    {  27,  54,  45,   0,  0,   6,    4,   0, TH_BRUSH,  1,  55, 24, 0, 0, 0, 0},   // brush knee and push, 2
    {  50,  54,  52,   0,  0,   4,    0,   0, TH_DOWN,  1,  45, 10, 0, 0, TK_TURN, 0},   // gather and turn round
    {  27,  54,  36,   0,  0,   6,    4,   0, TH_WARD, -1,  55, 15, 0, 0, 0, 0},   // grasp the sparrow's tail: ward off
    {  27,  54,  49,   0,  0,   7,   -4,   3, TH_ROLL, -1,  45,  6, 0, 0, 0, 0},   // ... roll back
    {  27,  54,  36,   0,  0,   6,    5,   0, TH_PRESS, -1,  45, 10, 0, 0, 0, 0},   // ... press
    {  27,  54,  49,   0,  0,   7,   -3,   0, TH_WITHDRAW, -1,  40,  5, 0, 0, 0, 0},   // ... withdraw
    {  27,  54,  36,   0,  0,   6,    5,   0, TH_PUSH, -1,  45, 24, 0, 0, TK_BUMP, 0},   // ... push
    {  27,  54,  47,   0,  0,   7,    0,   0, TH_WHIP_GATHER, -1,  40,  3, 0, 0, 0, 0},   // the hook goes out
    {  10,  54,  28,   0,  0,   7,    2,   0, TH_WHIP, -1,  60, 70, TE_SIGN, TE_SIGN_END, TK_LONGEST, 15},   // single whip
    {  10,   6,   8,   0,  0,   4,    0,   0, TH_DOWN, -1,  50, 10, 0, 0, TK_TURN, 0},   // gather and turn back
    {  10,   6,   8,   0,  0,   4,    0,   0, TH_CROSS,  1,  35,  8, 0, 0, 0, 0},   // cross hands
    {  10,   6,   8,   0,  0,   4,    0,   3, TH_LOWER,  1,  40,  3, TE_CLOSING, 0, 0, 0},   // closing: hands down
    {  10,   6,   8,   0,  0,   0,    2,   0, TH_HANG,  1,  45, 50, 0, 0, 0, 0},   // ... he stands; three breaths
};
const TaiKey kCh2[] = {   // clouds and the rooster: 107.7 s. Guest: the chipmunk.
    {   2,  -2,   0,   0,  0,   0,    2,   0, TH_HANG,  1,   0, 40, 0, 0, 0, 0},   // stillness
    {   2,  -2,   0,   0,  0,   0,    0,  -3, TH_RAISE,  1,  50, 10, 0, 0, 0, 0},   // commencing: up
    {   2,  -2,   0,   0,  0,   4,    2,   3, TH_LOWER,  1,  50, 15, 0, 0, 0, 0},   // ... and down
    {   2,  -2,   2,   0,  4,   5,    0,  -3, TH_CLOUD,  1,  36,  0, TE_GUEST, 0, 0, 0},   // wave hands like clouds
    {   2,  -2,   0,   0,  0,   5,    0,   0, TH_CLOUD_MID,  1,  18,  0, 0, 0, 0, 0},   // ... it comes back and down, the other goes out and up
    {   2,  -2,  -2,   4,  0,   5,    0,   3, TH_CLOUD | TH_SWAP,  1,  18,  0, 0, 0, 0, 0},   // ... the other hand before the face
    {   2,  -2,   0,   0,  0,   5,    0,   0, TH_CLOUD_MID | TH_SWAP,  1,  18,  0, 0, 0, 0, 0},   // ... and round again
    {   2,  -2,   2,   0,  4,   5,    0,  -3, TH_CLOUD,  1,  18,  0, 0, 0, 0, 0},   // wave hands like clouds
    {   2,  -2,   0,   0,  0,   5,    0,   0, TH_CLOUD_MID,  1,  18,  0, 0, 0, 0, 0},   // ... it comes back and down, the other goes out and up
    {   2,  -2,  -2,   4,  0,   5,    0,   3, TH_CLOUD | TH_SWAP,  1,  18,  0, 0, 0, 0, 0},   // ... the other hand before the face
    {   2,  -2,   0,   0,  0,   5,    0,   0, TH_CLOUD_MID | TH_SWAP,  1,  18,  0, 0, 0, 0, 0},   // ... and round again
    {   2,  -2,   2,   0,  4,   5,    0,  -3, TH_CLOUD,  1,  18,  0, 0, 0, 0, 0},   // wave hands like clouds
    {   2,  -2,   0,   0,  0,   5,    0,   0, TH_CLOUD_MID,  1,  18,  0, 0, 0, 0, 0},   // ... it comes back and down, the other goes out and up
    {   2,  -2,  -2,   4,  0,   5,    0,   3, TH_CLOUD | TH_SWAP,  1,  18,  0, 0, 0, 0, 0},   // ... the other hand before the face
    {  40,  -2,  20,   0,  0,   7,    2,   0, TH_WHIP,  1,  55, 25, 0, 0, 0, 0},   // single whip
    {  40,  -2,  -2,   0,  0,  54,   30, -12, TH_SNAKE,  1,  60, 20, TE_SIGN, 0, 0, 0},   // snake creeps down
    {  40,  -2,  24,   0,  0,  12,   10,   0, TH_RISE,  1,  45,  0, 0, 0, 0, 0},   // ... and up
    {  40,  50,  40,   0, 60,   2,    0,   0, TH_ROOSTER | TH_SWAP,  1,  50, 50, TE_SIGN2, TE_SIGN2_END, TK_BALANCE | TK_LONGEST | TK_BUMP, 0},   // golden rooster stands on one leg
    {  40,  62,  39,   0,  3,   7,    0,  -3, TH_HIGHPAT,  1,  50, 24, 0, 0, 0, 0},   // high pat on horse
    {  40,  62,  39,   0,  3,   7,    0,   0, TH_MONK_OPEN,  1,  35,  3, 0, 0, 0, 0},   // repulse monkey: open
    {  40,  40,  36,   0,  8,   6,    0,   0, TH_MONK_PASS,  1,  25,  0, 0, 0, 0, 0},   // ... the foot passes back
    {  40,  16,  19,   3,  0,   7,   -2,   0, TH_MONK_OPEN | TH_SWAP,  1,  30, 12, 0, 0, 0, 0},   // repulse monkey, 1
    {  17,  16,  15,   8,  0,   6,    0,   0, TH_MONK_PASS | TH_SWAP,  1,  25,  0, 0, 0, 0, 0},   // ... the foot passes back
    {  -8,  16,  -5,   0,  3,   7,   -2,   0, TH_MONK_OPEN,  1,  30, 12, 0, 0, 0, 0},   // repulse monkey, 2
    {  -8, -12, -10,   0,  0,   4,    0,   3, TH_LOWER,  1,  45,  5, TE_CLOSING, 0, 0, 0},   // closing
    {  -8, -12, -10,   0,  0,   0,    2,   0, TH_HANG,  1,  45, 50, 0, 0, 0, 0},   // ... he stands; three breaths
};
const TaiKey kCh3[] = {   // the kick and the close: 107.6 s. Guest: the deer at the tree line.
    {   2,  -2,   0,   0,  0,   0,    2,   0, TH_HANG,  1,   0, 40, 0, 0, 0, 0},   // stillness
    {   2,  -2,   0,   0,  0,   0,    0,  -3, TH_RAISE,  1,  50, 10, 0, 0, 0, 0},   // commencing: up
    {   2,  -2,   0,   0,  0,   4,    2,   3, TH_LOWER,  1,  50, 15, 0, 0, 0, 0},   // ... and down
    {  18,  -2,  -3,   3,  0,   7,    0,  -3, TH_HIGHPAT,  1,  50, 20, TE_GUEST, 0, 0, 0},   // high pat on horse
    {   6,  -2,  -2,  10,  0,   6,    0,   0, TH_CROSS,  1,  40,  4, 0, 0, 0, 0},   // the wrists cross, the foot draws in
    {  34,  -2,  -4,  75,  0,   2,   -8,  -3, TH_KICK,  1,  45, 30, TE_SIGN, TE_SIGN_END, TK_BALANCE, 0},   // heel kick
    {  12,  -2,  -2,  25,  0,   6,    0,   0, TH_WIND,  1,  35,  0, 0, 0, 0, 0},   // the knee folds, wind up
    {  27,  -2,  18,   0,  0,   6,    4,   0, TH_BRUSH | TH_SWAP,  1,  50, 24, 0, 0, 0, 0},   // brush knee and push
    {  27,   9,  16,   3,  0,   7,    0,   0, TH_NEEDLE_UP,  1,  40,  3, 0, 0, 0, 0},   // half step up, the hand by the ear
    {  27,   9,  10,   3,  0,  14,   55,  10, TH_NEEDLE,  1,  50, 25, TE_SIGN2, 0, TK_LONGEST | TK_BUMP, 0},   // needle at sea bottom
    {  37,   9,  26,   0,  0,   6,    3,  -3, TH_FAN,  1,  55, 26, 0, 0, 0, 0},   // fan through the back
    {  37,  33,  35,   0,  0,   4,    0,   0, TH_DOWN,  1,  45, 10, 0, 0, TK_TURN, 0},   // gather and turn round
    {  37,   9,  30,   0,  0,   7,   -3,   0, TH_WITHDRAW, -1,  45,  5, 0, 0, 0, 0},   // apparent close-up: withdraw
    {  37,   9,  18,   0,  0,   6,    5,   0, TH_PUSH, -1,  45, 24, 0, 0, 0, 0},   // ... push
    {   5,   9,   7,   0,  0,   4,    0,   0, TH_CROSS, -1,  50, 15, 0, 0, 0, 0},   // cross hands
    {   5,   9,   7,   0,  0,   4,    0,   3, TH_LOWER, -1,  50,  5, TE_CLOSING, 0, 0, 0},   // closing: hands down
    {   5,   9,   7,   0,  0,   0,    2,   0, TH_HANG, -1,  50, 70, 0, 0, TK_BOW, 0},   // ... he stands; the long close
};
// clang-format on

struct Table {
  const TaiKey* k;
  int n;
};

Table chapterTable(int c) {
  switch (c & 3) {
    case 0: return Table{kCh0, (int)(sizeof kCh0 / sizeof kCh0[0])};
    case 1: return Table{kCh1, (int)(sizeof kCh1 / sizeof kCh1[0])};
    case 2: return Table{kCh2, (int)(sizeof kCh2 / sizeof kCh2[0])};
    default: return Table{kCh3, (int)(sizeof kCh3 / sizeof kCh3[0])};
  }
}

inline float moveS(const TaiKey& k) { return 0.1f * kPace * (float)k.move; }
inline float holdS(const TaiKey& k) { return 0.1f * kPace * (float)k.hold; }
inline bool stopKey(const TaiKey& k) { return k.hold >= kLongHold || (k.flags & TK_STOP) != 0; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float sinR(float a) { return sinT(a * kInvTwoPi); }   // radians through the table
inline float cosR(float a) { return cosT(a * kInvTwoPi); }
inline float wrapPi(float a) {
  while (a > kPi) a -= kTwoPi;
  while (a < -kPi) a += kTwoPi;
  return a;
}

float tableSeconds(const Table& T) {
  float t = 0.f;
  for (int i = 0; i < T.n; ++i) t += moveS(T.k[i]) + holdS(T.k[i]);
  return t;
}

// ---- where a moment falls among a table's knots ----
// Each key gives one knot in the middle of a short hold (passed through at speed) or two, at both ends of
// a long one (a real stop: zero speed at both, the value held between).
struct Knot {
  float t;
  uint8_t key;
  bool flat;
};
constexpr int kMaxKnots = 2 * TAICHI_MAX_KEYS + 2;

int buildKnots(const Table& T, Knot* kn) {
  float t = 0.f;
  int n = 0;
  for (int i = 0; i < T.n && n + 2 <= kMaxKnots; ++i) {
    const float a = t + moveS(T.k[i]);
    const float e = a + holdS(T.k[i]);
    if (stopKey(T.k[i])) {
      kn[n++] = Knot{a, (uint8_t)i, true};
      kn[n++] = Knot{e, (uint8_t)i, true};
    } else {
      kn[n++] = Knot{0.5f * (a + e), (uint8_t)i, false};
    }
    t = e;
  }
  return n;
}

struct Seg {
  bool hold;          // between two knots of one key (or outside the table): key k0's values
  int km, k0, k1, kp; // the keys of the knots before, at the start, at the end, after
  float tm, t0, t1, tp;
  bool f0, f1;        // zero speed at the start or end knot
  float u;            // 0..1 from t0 to t1
};

Seg locate(const Table& T, float t) {
  Knot kn[kMaxKnots];
  const int n = buildKnots(T, kn);
  Seg s;
  s.hold = true;
  s.km = s.k0 = s.k1 = s.kp = 0;
  s.tm = s.t0 = s.t1 = s.tp = 0.f;
  s.f0 = s.f1 = true;
  s.u = 0.f;
  if (n == 0) return s;
  if (t <= kn[0].t) {
    s.km = s.k0 = s.k1 = s.kp = kn[0].key;
    return s;
  }
  if (t >= kn[n - 1].t) {
    s.km = s.k0 = s.k1 = s.kp = kn[n - 1].key;
    return s;
  }
  int j = 0;
  while (j + 1 < n && kn[j + 1].t <= t) ++j;
  s.k0 = kn[j].key;
  s.k1 = kn[j + 1].key;
  if (s.k0 == s.k1) {
    s.km = s.kp = s.k0;
    return s;
  }
  s.hold = false;
  s.t0 = kn[j].t;
  s.t1 = kn[j + 1].t;
  s.f0 = kn[j].flat;
  s.f1 = kn[j + 1].flat;
  s.km = j > 0 ? kn[j - 1].key : s.k0;
  s.tm = j > 0 ? kn[j - 1].t : s.t0 - 1.f;
  s.kp = j + 2 < n ? kn[j + 2].key : s.k1;
  s.tp = j + 2 < n ? kn[j + 2].t : s.t1 + 1.f;
  const float d = s.t1 - s.t0;
  s.u = d > 1e-5f ? clamp01((t - s.t0) / d) : 1.f;
  return s;
}

// The speed at a pass-through knot (value per second): the average of the two sides, zero at a reversal,
// and never more than three times the slower side, so a curve through uneven keys does not overshoot.
float mono(float tm, float vm, float t0, float v0, float t1, float v1) {
  const float h0 = t0 - tm, h1 = t1 - t0;
  if (h0 < 1e-3f || h1 < 1e-3f) return 0.f;
  const float d0 = (v0 - vm) / h0, d1 = (v1 - v0) / h1;
  if (d0 * d1 <= 0.f) return 0.f;
  float m = 0.5f * (d0 + d1);
  const float lim = 3.f * minf(fabsf(d0), fabsf(d1));
  if (m > lim) m = lim;
  if (m < -lim) m = -lim;
  return m;
}

float herm(float p0, float p1, float m0, float m1, float u) {
  const float u2 = u * u, u3 = u2 * u;
  return (2.f * u3 - 3.f * u2 + 1.f) * p0 + (u3 - 2.f * u2 + u) * m0 + (3.f * u2 - 2.f * u3) * p1 + (u3 - u2) * m1;
}

// ---- the keyed values in units of H and radians ----
struct Vary {
  float sink, hand;
};
enum Chan1 : uint8_t { CH_HIP, CH_SINK, CH_LEAN, CH_HEAD, CH_FX0, CH_FX1, CH_FL0, CH_FL1 };

float keyVal(const TaiKey& k, int ch, const Vary& v) {
  switch (ch) {
    case CH_HIP: return 0.01f * (float)k.hip;
    case CH_SINK: return 0.005f * (float)k.sink + v.sink;
    case CH_LEAN: return 0.01f * (float)k.lean;
    case CH_HEAD: return 0.01f * (float)k.head;
    case CH_FX0: return 0.01f * (float)k.a;
    case CH_FX1: return 0.01f * (float)k.b;
    case CH_FL0: return 0.004f * (float)k.la;
    default: return 0.004f * (float)k.lb;
  }
}
inline bool lifted(const TaiKey& k, int foot) { return (foot ? k.lb : k.la) != 0; }

float evalCh(const Table& T, const Seg& s, int ch, const Vary& v) {
  const float v0 = keyVal(T.k[s.k0], ch, v);
  if (s.hold) return v0;
  const float vm = keyVal(T.k[s.km], ch, v), v1 = keyVal(T.k[s.k1], ch, v), vp = keyVal(T.k[s.kp], ch, v);
  const float m0 = s.f0 ? 0.f : mono(s.tm, vm, s.t0, v0, s.t1, v1);
  const float m1 = s.f1 ? 0.f : mono(s.t0, v0, s.t1, v1, s.tp, vp);
  const float d = s.t1 - s.t0;
  return herm(v0, v1, m0 * d, m1 * d, s.u);
}

// A foot's lift where its place does not change: speed zero at any key where it is planted.
float evalLift(const Table& T, const Seg& s, int foot, const Vary& v) {
  const int ch = foot ? CH_FL1 : CH_FL0;
  const float v0 = keyVal(T.k[s.k0], ch, v);
  if (s.hold) return v0;
  const float vm = keyVal(T.k[s.km], ch, v), v1 = keyVal(T.k[s.k1], ch, v), vp = keyVal(T.k[s.kp], ch, v);
  const float m0 = (s.f0 || !lifted(T.k[s.k0], foot)) ? 0.f : mono(s.tm, vm, s.t0, v0, s.t1, v1);
  const float m1 = (s.f1 || !lifted(T.k[s.k1], foot)) ? 0.f : mono(s.t0, v0, s.t1, v1, s.tp, vp);
  const float d = s.t1 - s.t0;
  return herm(v0, v1, m0 * d, m1 * d, s.u);
}

// The hips and both feet at one moment. A step: the hips go over the standing foot first, the other foot
// travels in the middle of the move on a low arc, and the weight goes on to its new place only after it has
// landed (TAICHI_PLAN 4.3 step 4 and 5.4: "the weight is over the standing foot before the other one leaves
// the ground").
void legsAt(const Table& T, const Seg& s, const Vary& v, float& hip, float fx[2], float fl[2]) {
  const TaiKey& K0 = T.k[s.k0];
  if (s.hold) {
    hip = keyVal(K0, CH_HIP, v);
    for (int i = 0; i < 2; ++i) {
      fx[i] = keyVal(K0, CH_FX0 + i, v);
      fl[i] = keyVal(K0, CH_FL0 + i, v);
    }
    return;
  }
  const TaiKey& K1 = T.k[s.k1];
  int mover = -1;
  if (K0.a != K1.a) mover = 0;
  if (K0.b != K1.b) mover = 1;   // the tables move one foot a segment (test_taichi checks)
  for (int i = 0; i < 2; ++i) {
    if (i == mover) continue;
    fx[i] = keyVal(K0, CH_FX0 + i, v);
    fl[i] = evalLift(T, s, i, v);
  }
  const float plain = evalCh(T, s, CH_HIP, v);
  if (mover < 0) {
    hip = plain;
    return;
  }
  const float d = s.t1 - s.t0;
  const float S = keyVal(K0, CH_FX0 + (1 - mover), v);
  const float x0 = keyVal(K0, CH_FX0 + mover, v), x1 = keyVal(K1, CH_FX0 + mover, v);
  const float l0 = keyVal(K0, CH_FL0 + mover, v), l1 = keyVal(K1, CH_FL0 + mover, v);
  const bool up0 = lifted(K0, mover), up1 = lifted(K1, mover);
  const float ua = up0 ? 0.f : (up1 ? kLeaveC : kLeave);
  const float ub = up1 ? 1.f : (up0 ? kLandB : kLand);
  const float sw = window(s.u, ua, ub);
  const float span = d * (ub - ua);
  // Speed through a key only where the foot is in the air there (a planted foot is still at its key).
  const float xm = keyVal(T.k[s.km], CH_FX0 + mover, v), xp = keyVal(T.k[s.kp], CH_FX0 + mover, v);
  const float mx0 = (up0 && !s.f0) ? mono(s.tm, xm, s.t0, x0, s.t1, x1) : 0.f;
  const float mx1 = (up1 && !s.f1) ? mono(s.t0, x0, s.t1, x1, s.tp, xp) : 0.f;
  fx[mover] = herm(x0, x1, mx0 * span, mx1 * span, sw);
  const float lm = keyVal(T.k[s.km], CH_FL0 + mover, v), lp = keyVal(T.k[s.kp], CH_FL0 + mover, v);
  const float ml0 = (up0 && !s.f0) ? mono(s.tm, lm, s.t0, l0, s.t1, l1) : 0.f;
  const float ml1 = (up1 && !s.f1) ? mono(s.t0, l0, s.t1, l1, s.tp, lp) : 0.f;
  float lift = herm(l0, l1, ml0 * span, ml1 * span, sw);
  const float p = clamp01((fx[mover] - x0) / (x1 - x0));
  const float sp = sinR(kPi * p);
  lift += maxf(0.f, kStepArc - maxf(l0, l1)) * sp * sp;
  fl[mover] = lift;

  if (up0 && up1) {   // a foot already in the air goes on: the hips simply flow
    hip = plain;
    return;
  }
  const float h0 = keyVal(K0, CH_HIP, v), h1 = keyVal(K1, CH_HIP, v);
  const float hm = keyVal(T.k[s.km], CH_HIP, v), hp = keyVal(T.k[s.kp], CH_HIP, v);
  const float mh0 = s.f0 ? 0.f : mono(s.tm, hm, s.t0, h0, s.t1, h1);
  const float mh1 = s.f1 ? 0.f : mono(s.t0, h0, s.t1, h1, s.tp, hp);
  // The hips go toward the standing foot only as far as the stepping leg still reaches with them at most
  // kReachDip lower than the stance asks: in a wide stance (the single whip's 0.44 H) going all the way
  // dropped him into a deep squat just before the back foot left, and he sprang up as it lifted.
  const float hh_n = kStand - keyVal(K0, CH_SINK, v) - kReachDip;
  const float reach = sqrtf(maxf(0.f, kLegMax * kLegMax - hh_n * hh_n));
  float toward = S + clampf(h1 - S, -kDwellOn, kDwellOn);   // over the standing foot, a hair on
  toward = up0 ? clampf(toward, x1 - reach, x1 + reach) : clampf(toward, x0 - reach, x0 + reach);
  const float u = s.u;
  if (!up0 && !up1) {   // planted to planted: shift on, step, shift on again
    const float hA = clampf(S + clampf(h0 - S, -kDwell, kDwell), x0 - reach, x0 + reach);
    const float hB = clampf(S + clampf(h1 - S, -kDwellOn, kDwellOn), x1 - reach, x1 + reach);
    const float vmid = (hB - hA) / maxf(span, 1e-3f);
    if (u < ua) {
      const float T1 = d * ua;
      hip = herm(h0, hA, mh0 * T1, vmid * T1, u / ua);
    } else if (u < ub) {
      hip = hA + (hB - hA) * (u - ua) / (ub - ua);
    } else {
      const float T3 = d * (1.f - ub);
      hip = herm(hB, h1, vmid * T3, mh1 * T3, (u - ub) / (1.f - ub));
    }
    return;
  }
  // One end in the air: the hips stay over the standing foot until the foot lands (or from the moment it
  // leaves), then go on.
  const float uj = up0 ? ub : ua;
  const float T1 = d * uj, T3 = d * (1.f - uj);
  const float vj = mono(s.t0, h0, s.t0 + T1, toward, s.t1, h1);
  if (u < uj) hip = herm(h0, toward, mh0 * T1, vj * T1, u / uj);
  else hip = herm(toward, h1, vj * T3, mh1 * T3, (u - uj) / (1.f - uj));
}

// ---- arms: IK at the keys, joint angles between ----
struct Arm {
  float up, bend;   // the rig's shoulder (absolute) and elbow (relative) angles
};

inline Vec2 dirA(float a) { return Vec2{sinR(a), cosR(a)}; }   // from straight down, + forward

Vec2 armHand(const Arm& a) {
  const Vec2 e = dirA(a.up), f = dirA(a.up + a.bend);
  return Vec2{kUpper * e.x + kFore * f.x, kUpper * e.y + kFore * f.y};
}

// Hand target (from the shoulder, H, x forward, y down) to joint angles. The elbow takes the low side
// (REVIEW_FIXES F20): forward of the shoulder, or low, a forward bend; a hand behind and high a negative
// bend, so the elbow hangs instead of winging up behind him.
Arm armIK(float hx, float hy) {
  const float r0 = sqrtf(hx * hx + hy * hy);
  const float phi = taiAtan2(hx, hy);
  const float r = clampf(r0, kReachMin, kReachMax);
  const float c = clampf((kUpper * kUpper + r * r - kFore * kFore) / (2.f * kUpper * r), -1.f, 1.f);
  const float alpha = taiAtan2(sqrtf(maxf(0.f, 1.f - c * c)), c);
  // A low hand straightens its elbow as it comes under the shoulder (the arms hang), so the turns' arms are
  // the same from both sides and a hand just behind the hip does not bend backward; the hand then sits at
  // most 0.02 H further out than asked.
  const float side = hy > kLowHand ? clampf(hx / 0.03f, 0.f, 1.f) : (hx >= 0.f ? 1.f : -1.f);
  Arm a;
  a.up = phi - side * alpha;
  const Vec2 e = dirA(a.up);
  const Vec2 tgt = dirA(phi);
  const float fx = r * tgt.x - kUpper * e.x, fy = r * tgt.y - kUpper * e.y;
  a.bend = wrapPi(taiAtan2(fx, fy) - a.up);
  return a;
}

Vec2 keyHand(const TaiKey& k, int arm, const Vary& v) {
  const TaiHandsDef& d = kHands[(k.hands & 0x7F) < TH_COUNT ? (k.hands & 0x7F) : 0];
  const bool swap = (k.hands & TH_SWAP) != 0;
  int which = swap ? 1 - arm : arm;
  float x = 0.005f * (float)(which ? d.x1 : d.x0);
  float y = 0.005f * (float)(which ? d.y1 : d.y0);
  if (arm == 0 && (k.flags & TK_H0) && k.extra < TH_COUNT) {   // a gag key moves hand 0 alone
    const TaiHandsDef& g = kHands[k.extra];
    const float gx = 0.005f * (float)g.x0, gy = 0.005f * (float)g.y0;
    if (k.flags & TK_HMID) {   // a way point halfway along the straight line from the key's own hand
      x = 0.5f * (x + gx);
      y = 0.5f * (y + gy);
    } else {
      x = gx;
      y = gy;
    }
  }
  if (y < 0.29f) y += v.hand;   // the hanging hands stay where gravity puts them
  return Vec2{x, y};
}

Arm keyArm(const TaiKey& k, int arm, const Vary& v) {
  const Vec2 h = keyHand(k, arm, v);
  return armIK(h.x, h.y);
}

// Move b's angles by whole turns so that going from a to b is the natural way round: the one whose halfway
// hand is nearest the straight halfway point (a hand coming from behind to a push folds in past the
// shoulder, it does not swing down past the hip), with a small charge on the size of the turn.
Arm unwrapFrom(const Arm& a, Arm b) {
  b.up = a.up + wrapPi(b.up - a.up);
  b.bend = a.bend + wrapPi(b.bend - a.bend);
  const Vec2 ha = armHand(a), hb = armHand(b);
  const Vec2 mid{0.5f * (ha.x + hb.x), 0.5f * (ha.y + hb.y)};
  Arm best = b;
  float best_c = 3.0e38f;
  for (int i = -1; i <= 1; ++i) {
    for (int j = -1; j <= 1; ++j) {
      Arm c{b.up + (float)i * kTwoPi, b.bend + (float)j * kTwoPi};
      const Arm m{0.5f * (a.up + c.up), 0.5f * (a.bend + c.bend)};
      const Vec2 hm = armHand(m);
      const float du = c.up - a.up, db = c.bend - a.bend;
      const float cost = (hm.x - mid.x) * (hm.x - mid.x) + (hm.y - mid.y) * (hm.y - mid.y) + 0.0002f * (du * du + db * db);
      if (cost < best_c) {
        best_c = cost;
        best = c;
      }
    }
  }
  return best;
}

void armsAt(const Table& T, const Seg& s, const Vary& v, Arm out[2]) {
  for (int i = 0; i < 2; ++i) {
    const Arm a0 = keyArm(T.k[s.k0], i, v);
    if (s.hold) {
      out[i] = a0;
      continue;
    }
    const Arm a1 = unwrapFrom(a0, keyArm(T.k[s.k1], i, v));
    const Arm am = s.km == s.k0 ? a0 : unwrapFrom(a0, keyArm(T.k[s.km], i, v));
    const Arm ap = s.kp == s.k1 ? a1 : unwrapFrom(a1, keyArm(T.k[s.kp], i, v));
    const float d = s.t1 - s.t0;
    const float mu0 = s.f0 ? 0.f : mono(s.tm, am.up, s.t0, a0.up, s.t1, a1.up);
    const float mu1 = s.f1 ? 0.f : mono(s.t0, a0.up, s.t1, a1.up, s.tp, ap.up);
    const float mb0 = s.f0 ? 0.f : mono(s.tm, am.bend, s.t0, a0.bend, s.t1, a1.bend);
    const float mb1 = s.f1 ? 0.f : mono(s.t0, a0.bend, s.t1, a1.bend, s.tp, ap.bend);
    out[i].up = herm(a0.up, a1.up, mu0 * d, mu1 * d, s.u);
    out[i].bend = herm(a0.bend, a1.bend, mb0 * d, mb1 * d, s.u);
  }
}

// ---- one table evaluated at one moment, every channel at its own lag ----
struct Chans {
  float hip, sink, lean, head, lift;
  float fx[2], fl[2];
  Arm arm[2];
  int face;
  float ft;
};

void evalTable(const Table& T, float t, const Vary& v, Chans& c) {
  const Seg s0 = locate(T, t);
  legsAt(T, s0, v, c.hip, c.fx, c.fl);
  c.sink = evalCh(T, s0, CH_SINK, v);
  c.lean = evalCh(T, locate(T, t - kLagLean), CH_LEAN, v);
  armsAt(T, locate(T, t - kLagHands), v, c.arm);
  c.head = evalCh(T, locate(T, t - kLagHead), CH_HEAD, v);
  c.lift = 0.f;
}

// The facing and the feet's end-on amount at form time t: a turn key flips the facing in the middle of its
// hold, when he is gathered into a shape that is the same from both sides (TAICHI_PLAN 4.5).
void faceAt(const Table& T, float t, int& face, float& ft) {
  float e_prev = 0.f;
  face = T.k[T.n - 1].face;
  ft = 0.f;
  bool found = false;
  for (int i = 0; i < T.n; ++i) {
    const TaiKey& k = T.k[i];
    const float a = e_prev + moveS(k), e = a + holdS(k);
    if (k.flags & TK_TURN) {
      float r = 0.f;
      if (t >= a && t <= e) r = 1.f;
      else if (t < a && t > a - kTurnRamp) r = easeInOut((t - (a - kTurnRamp)) / kTurnRamp);
      else if (t > e && t < e + kTurnRamp) r = 1.f - easeInOut((t - e) / kTurnRamp);
      ft = maxf(ft, r);
    }
    if (!found && t <= e) {
      found = true;
      face = k.face;
      if ((k.flags & TK_TURN) && t > a + 0.5f * holdS(k)) face = -k.face;
    }
    e_prev = e;
  }
}

// A hair more sink in every real stop, eased in and let go as the next move starts; and the last close's
// bow of the head (the head channel's own lag).
void holdExtras(const Table& T, float t, float& sink, float& head, float& lean) {
  sink = head = lean = 0.f;
  float e_prev = 0.f;
  for (int i = 0; i < T.n; ++i) {
    const TaiKey& k = T.k[i];
    const float a = e_prev + moveS(k), e = a + holdS(k);
    if (k.hold >= kLongHold && t >= a) {
      if (t < e) sink = kHoldSink * easeInOut((t - a) / minf(holdS(k), 2.f));
      else if (t < e + 1.f) sink = kHoldSink * (1.f - easeInOut(t - e));
      else sink = 0.f;
    }
    if ((k.flags & TK_BOW) && t >= a) {   // TAICHI_PLAN 5.2: +0.14 rad from 2.0 to 3.4 s into the last hold
      const float th = t - kLagHead - a;
      const float env = easeInOut(window(th, 2.0f, 2.6f)) * (1.f - easeInOut(window(th, 3.0f, 3.6f)));
      head = 0.14f * env;
      lean = 0.04f * env;
    }
    e_prev = e;
  }
}

// ---- the gags (TAICHI_PLAN 6.2), as short key tables built from the key they play in ----
inline int8_t i8(int v) { return (int8_t)(v < -127 ? -127 : (v > 127 ? 127 : v)); }
inline uint8_t u8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

TaiKey stopAt(const TaiKey& b, int move, int hold) {
  TaiKey k = b;
  k.move = u8(move);
  k.hold = u8(hold);
  k.cue_in = k.cue_out = 0;
  k.flags = TK_STOP;
  k.extra = 0;
  return k;
}
TaiKey flowAt(const TaiKey& b, int move) {
  TaiKey k = stopAt(b, move, 0);
  k.flags = 0;
  return k;
}
void setFoot(TaiKey& k, int foot, int x, int lift) {
  if (foot) {
    k.b = i8(x);
    k.lb = u8(lift);
  } else {
    k.a = i8(x);
    k.la = u8(lift);
  }
}

int gagTable(uint8_t kind, const TaiKey& B, int side, TaiKey* g) {
  const int fd = B.face < 0 ? -1 : 1;
  const int L = B.lb > B.la ? 1 : (B.la > 0 ? 0 : -1);   // the lifted foot, if any
  int n = 0;
  g[n++] = stopAt(B, 0, 0);
  if (kind == TG_WOBBLE) {
    // 0-1.2 s a slow drift too far; 1.2-2.0 the arms fly out, the lifted foot comes down ahead, one small
    // hop; 2.0-3.4 he stands, the shoulders fall with a long breath out; 3.4-4.8 a hand to the back of his
    // head; 4.8-8.0 hands down, he sinks and goes back into the form slowly.
    TaiKey k1 = flowAt(B, 12);
    k1.lean = i8(B.lean + 10);
    k1.hip = i8(B.hip + 3 * fd);
    g[n++] = k1;
    TaiKey k2 = flowAt(k1, 8);
    k2.hip = i8(B.hip + 6 * fd);
    k2.lean = i8(B.lean + 2);
    k2.hands = TH_ARMSOUT;
    const int caught = B.hip + 6 * fd + 18 * fd;
    if (L >= 0) setFoot(k2, L, caught, 0);
    g[n++] = k2;
    TaiKey k3 = flowAt(k2, 14);
    k3.hip = i8(((int)k2.a + (int)k2.b) / 2);
    k3.sink = 0;
    k3.lean = 6;
    k3.head = 15;
    k3.hands = TH_HANG;
    g[n++] = k3;
    TaiKey k4 = stopAt(k3, 10, 4);
    k4.hands = TH_SCRATCH;
    k4.head = 10;
    k4.lean = 3;
    g[n++] = k4;
    TaiKey k5 = stopAt(k4, 12, 4);
    k5.hands = TH_HANG;
    k5.sink = 6;
    k5.head = 0;
    k5.lean = 2;
    g[n++] = k5;
    g[n++] = stopAt(B, 11, 5);
  } else if (kind == TG_ITCH) {
    // 1.0 s of perfect stillness (in the form's own hold), the near hand slaps his neck, stays while his
    // head tips toward it, then goes back along the same path, slower than anything else he does.
    // Review (2026-11-18): from a hand held far out at shoulder height (the single whip) the joint angles
    // alone carried the hand up over his head and out, so the guilty way back read as a wave. A way point
    // halfway along the straight line keeps the elbow low and the hand in front of his face, both ways.
    // From a hand overhead, close in or low (the crane, the rooster, the needle) the plain path is already
    // in front of him and a way point would swing the elbow back first, so they go without one.
    const Vec2 h0 = keyHand(B, 0, Vary{0.f, 0.f});
    const bool way = h0.x >= 0.25f && h0.y <= 0.10f;
    TaiKey mid = flowAt(B, 0);
    mid.flags = TK_H0 | TK_HMID;
    mid.extra = TH_SLAP;
    mid.head = i8(B.head + 5);
    if (way) {
      mid.move = 1;
      g[n++] = mid;
    }
    TaiKey k1 = stopAt(B, way ? 2 : 3, 6);
    k1.flags = TK_STOP | TK_H0;
    k1.extra = TH_SLAP;
    k1.head = i8(B.head + 10);
    g[n++] = k1;
    if (way) {
      mid.move = 15;
      g[n++] = mid;
    }
    g[n++] = stopAt(B, way ? 15 : 30, 21);
  } else {   // TG_BUMP: Copper shoulders past his legs
    const int sd = side < 0 ? -1 : 1;
    TaiKey k1 = flowAt(B, 6);   // the stumble the way he was pushed
    k1.hip = i8(B.hip + 5 * sd);
    k1.lean = i8(B.lean + 12 * sd * fd);
    k1.hands = TH_ARMSOUT;
    if (L >= 0) setFoot(k1, L, B.hip + 5 * sd + 12 * sd, 0);
    g[n++] = k1;
    TaiKey k2 = flowAt(k1, 12);   // he catches it
    k2.hip = i8(B.hip + 2 * sd);
    k2.lean = B.lean;
    k2.hands = TH_LOWER;
    g[n++] = k2;
    TaiKey k3 = stopAt(k2, 12, 4);   // stands and looks down at the dog
    k3.hip = i8(((int)k2.a + (int)k2.b) / 2);
    k3.sink = 0;
    k3.lean = 2;
    k3.head = 20;
    k3.hands = TH_HANG;
    g[n++] = k3;
    TaiKey k4 = stopAt(k3, 7, 13);   // a hand down to Copper's head
    k4.lean = 25;
    k4.head = 25;
    k4.flags = TK_STOP | TK_H0;
    k4.extra = TH_PAT;
    g[n++] = k4;
    TaiKey k5 = stopAt(k3, 12, 4);   // straightens, a breath
    k5.head = 0;
    g[n++] = k5;
    g[n++] = stopAt(B, 16, 0);   // and back into the form
  }
  return n;
}

uint8_t gagKind(int chapter, uint8_t gag) {
  if (gag == TG_WOBBLE) {
    const Table T = chapterTable(chapter);
    for (int i = 0; i < T.n; ++i)
      if (T.k[i].flags & TK_BALANCE) return TG_WOBBLE;
    return TG_ITCH;   // the sparrow's tail has no balance form: the itch instead (TAICHI_PLAN 6.2)
  }
  return gag <= TG_BUMP ? gag : (uint8_t)TG_NONE;
}

int gagKey(int chapter, uint8_t kind) {
  const uint8_t flag = kind == TG_WOBBLE ? TK_BALANCE : (kind == TG_ITCH ? TK_LONGEST : TK_BUMP);
  const Table T = chapterTable(chapter);
  for (int i = 0; i < T.n; ++i)
    if (T.k[i].flags & flag) return i;
  return -1;
}

float keyArriveT(const Table& T, int k) {
  float t = 0.f;
  for (int i = 0; i <= k && i < T.n; ++i) {
    t += moveS(T.k[i]);
    if (i < k) t += holdS(T.k[i]);
  }
  return t;
}

float gagStart(int chapter, uint8_t kind) {
  const int k = gagKey(chapter, kind);
  if (k < 0) return -1.f;
  const float at = kind == TG_WOBBLE ? kWobbleAt : (kind == TG_ITCH ? kItchAt : kBumpAt);
  return keyArriveT(chapterTable(chapter), k) + at * kPace;
}

// ---- the solve: channels to the rig's joint angles ----
inline float heelLen(float ft) { return kHeel + (kTurnFoot - kHeel) * ft; }
inline float toeLen(float ft) { return kToe + (kTurnFoot - kToe) * ft; }

// How far the rig's foot reaches below its ankle for a shin angle (the rig pitches the foot with the shin).
float footDip(float shin, float ft) {
  const float s = sinR(kFootFollow * shin);
  return maxf(0.f, maxf(s * heelLen(ft), -s * toeLen(ft)));
}

void solve(const Chans& c, float g, bool turn_aid, float w, HikerPose& P) {
  const float ft = turn_aid ? c.ft : 0.f;
  const float face = c.face < 0 ? -1.f : 1.f;
  // Coming in and going out the legs stay on the IK (so no foot slides), only as long as the rig's own
  // straight stand at w 0: the knees soften as the weight comes in.
  const float leg_max = lerp(kRigStand, kLegMax, w);
  float ax[2], lift[2], dip[2] = {0.f, 0.f}, phi[2] = {0.f, 0.f}, beta[2] = {0.f, 0.f};
  for (int i = 0; i < 2; ++i) {
    ax[i] = face * (c.fx[i] - c.hip) * g;
    lift[i] = c.fl[i];
  }
  // The rig tilts each foot with its shin (hiker.cpp kFootFollow): a slanted shin puts the toe or the heel
  // below the ankle. Each pass raises the ankle by that much and lets the hip height count it, so both
  // planted feet end on the ground together and a straight back leg stays straight; four passes settle it
  // to well under a tenth of a pixel.
  for (int pass = 0; pass < 4; ++pass) {
    float hh = minf(kStand - c.sink, leg_max);
    for (int i = 0; i < 2; ++i) {
      // A planted leg is never longer than leg_max: the hips come down as far as the stance needs. A foot
      // in the air lets go of the rule by degrees (no jump as it leaves).
      const float load = 1.f - clamp01(lift[i] / kStepArc);
      const float lim = lift[i] + dip[i] + sqrtf(maxf(0.f, leg_max * leg_max - ax[i] * ax[i]));
      hh = lerp(hh, minf(hh, lim), load);
    }
    for (int i = 0; i < 2; ++i) {
      const float ay = hh - lift[i] - dip[i];
      float d = sqrtf(ax[i] * ax[i] + ay * ay);
      d = clampf(d, 0.08f, leg_max);
      phi[i] = taiAtan2(ax[i], ay);
      const float half = 0.5f * d;
      beta[i] = taiAtan2(sqrtf(maxf(0.f, kBone * kBone - half * half)), half);
      // Through a turn the knees point at the viewer: in profile the leg is straight (TAICHI_PLAN 4.5).
      beta[i] *= 1.f - ft;
      dip[i] = footDip(phi[i] - beta[i], ft);
    }
  }
  for (int i = 0; i < 2; ++i) {
    P.thigh[i] = phi[i] + beta[i];
    P.knee[i] = 2.f * beta[i];
  }
  // A body folds at the hips over bent knees: the rig has no pelvis, so the torso leans a little with the
  // bend of the knees that carry him. Without it the hips sat behind his feet and he looked about to sit
  // down backward (B2's first posture sheet).
  float wsum = 0.f, bend = 0.f;
  for (int i = 0; i < 2; ++i) {
    const float load = 1.f - clamp01(lift[i] / kStepArc);
    wsum += load;
    bend += load * P.knee[i];
  }
  const float sit = wsum > 0.f ? bend / wsum : 0.f;
  P.lean = c.lean + minf(kSitLeanMax, kSitLean * maxf(0.f, sit - kSitFree));
  P.head = c.head;
  P.lift = c.lift;
  P.dx = 0.f;
  for (int i = 0; i < 2; ++i) {
    P.shoulder[i] = c.arm[i].up;
    P.elbow[i] = c.arm[i].bend;
  }
}

HikerPose mixPose(const HikerPose& a, const HikerPose& b, float t) {
  HikerPose r;
  r.lean = lerp(a.lean, b.lean, t);
  r.head = lerp(a.head, b.head, t);
  r.lift = lerp(a.lift, b.lift, t);
  r.dx = lerp(a.dx, b.dx, t);
  for (int i = 0; i < 2; ++i) {
    r.thigh[i] = lerp(a.thigh[i], b.thigh[i], t);
    r.knee[i] = lerp(a.knee[i], b.knee[i], t);
    // The rig's own arms hang near zero; the module's angles may sit a whole turn away after a fold.
    r.shoulder[i] = lerp(a.shoulder[i], a.shoulder[i] + wrapPi(b.shoulder[i] - a.shoulder[i]), t);
    r.elbow[i] = lerp(a.elbow[i], a.elbow[i] + wrapPi(b.elbow[i] - a.elbow[i]), t);
  }
  return r;
}

// The rig's forward kinematics and grounding (hiker.cpp build(), frozen), for the frame positions the
// guests and the tests read.
struct Fk {
  Vec2 shoulder, perch, hand[2], ankle[2];
  float low[2];
};

void fk(const Hiker& h, Fk& o) {
  const float H = h.height;
  const HikerPose& P = h.pose;
  const float breath = (1.f - h.walk_w) * kBreathAmp * H * h.breath_scale * (0.5f + 0.5f * sinT(h.breath_phase));
  const Vec2 hip{P.dx * H, 0.f};
  const float lt = P.lean * kInvTwoPi;
  const Vec2 up{sinT(lt), -cosT(lt)};
  const Vec2 chest{hip.x + up.x * (kTorsoTop * H + breath), hip.y + up.y * (kTorsoTop * H + breath)};
  const Vec2 sh{hip.x + up.x * (kShoulderUp * H + breath), hip.y + up.y * (kShoulderUp * H + breath)};
  const Vec2 neck{hip.x + up.x * (kNeck * H + breath), hip.y + up.y * (kNeck * H + breath)};
  // The butterfly's perch: on the round of the torso's top, toward his back, so it sits on the outline
  // behind the head. The shoulder joint itself is inside the silhouette, under the chin: a pale speck there
  // read as an eye on his face (review, 2026-11-18).
  const float tr = 0.5f * kTorsoW * H;
  const Vec2 perch{chest.x + up.y * 0.95f * tr + up.x * 0.45f * tr, chest.y - up.x * 0.95f * tr + up.y * 0.45f * tr};
  const float ht = (P.lean + P.head) * kInvTwoPi;
  const Vec2 hu{sinT(ht), -cosT(ht)}, hf{cosT(ht), sinT(ht)};
  const Vec2 head{neck.x + hu.x * kHeadUp * H + hf.x * kHeadFwd * H, neck.y + hu.y * kHeadUp * H + hf.y * kHeadFwd * H};
  float low = maxf(hip.y, chest.y) + 0.5f * kTorsoW * H;
  Vec2 ankle[2], hand[2];
  float flow[2];
  for (int i = 0; i < 2; ++i) {
    const Vec2 kd = dirA(P.thigh[i]);
    const Vec2 knee{hip.x + kd.x * kBone * H, hip.y + kd.y * kBone * H};
    const float shin = P.thigh[i] - P.knee[i];
    const Vec2 sd = dirA(shin);
    ankle[i] = Vec2{knee.x + sd.x * kBone * H, knee.y + sd.y * kBone * H};
    const Vec2 fd = dirA(0.5f * kPi + kFootFollow * shin);
    const float heel_y = ankle[i].y - fd.y * heelLen(h.foot_turn) * H;
    const float toe_y = ankle[i].y + fd.y * toeLen(h.foot_turn) * H;
    flow[i] = maxf(heel_y, maxf(toe_y, ankle[i].y)) + 0.5f * kFootW * H;
    low = maxf(low, knee.y + 0.5f * kShinW * H);
    low = maxf(low, flow[i]);
    const Vec2 ed = dirA(P.shoulder[i]), fo = dirA(P.shoulder[i] + P.elbow[i]);
    hand[i] = Vec2{sh.x + ed.x * kUpper * H + fo.x * kFore * H, sh.y + ed.y * kUpper * H + fo.y * kFore * H};
  }
  low = maxf(low, head.y + kHeadR * H);
  const float oy = h.feet.y - low - P.lift * H;
  const float sx = h.facing < 0 ? -1.f : 1.f;
  o.shoulder = Vec2{h.feet.x + sx * sh.x, oy + sh.y};
  o.perch = Vec2{h.feet.x + sx * perch.x, oy + perch.y};
  for (int i = 0; i < 2; ++i) {
    o.hand[i] = Vec2{h.feet.x + sx * hand[i].x, oy + hand[i].y};
    o.ankle[i] = Vec2{h.feet.x + sx * ankle[i].x, oy + ankle[i].y};
    o.low[i] = oy + flow[i];
  }
}

Vary varyOf(const TaichiState& s) { return Vary{kVarySink * (float)s.vary_sink, kVaryHand * (float)s.vary_hand}; }

// Every channel of the present moment: the form, or the gag that has stopped its clock.
void chansNow(const TaichiState& s, Chans& c) {
  const Table F = chapterTable(s.chapter);
  const Vary v = varyOf(s);
  const float tf = minf(s.t, tableSeconds(F));
  float extra_sink = 0.f, bow_head = 0.f, bow_lean = 0.f;
  if (s.phase == TP_GAG) {
    TaiKey gk[8];
    const int n = gagTable(s.gag_on, F.k[s.key], s.bump_side, gk);
    evalTable(Table{gk, n}, s.gag_t, v, c);
    extra_sink = s.hold_sink;
    if (s.gag_on == TG_WOBBLE) {   // the one small hop as the caught foot takes him
      const float hp = sinR(kPi * window(s.gag_t, 1.62f, 1.98f));
      c.lift = kHop * hp * hp;
    }
  } else {
    evalTable(F, tf, v, c);
    holdExtras(F, tf, extra_sink, bow_head, bow_lean);
  }
  faceAt(F, tf, c.face, c.ft);
  c.sink += extra_sink;
  c.head += bow_head;
  c.lean += bow_lean;
  // Never a dead frame: two slow drifts under everything (TAICHI_PLAN 4.3 step 7). The arms' drift fades
  // through a turn, so the arms hang the same from both sides when the facing flips; the hips' too (review,
  // 2026-11-18: a hip 0.3 px off the middle of the gathered feet showed as a lean that swapped sides).
  const float arm_drift = kArmDrift * (1.f - c.ft);
  c.hip += kHipDrift * (1.f - c.ft) * sinT(s.clk * 0.11f);
  c.arm[0].up += arm_drift * sinT(s.clk * 0.07f);
  c.arm[1].up += arm_drift * sinT(s.clk * 0.09f + 0.37f);
}

int keyAt(const Table& T, float t) {
  float e_prev = 0.f;
  for (int i = 0; i < T.n; ++i) {
    const float e = e_prev + moveS(T.k[i]) + holdS(T.k[i]);
    if (t <= e) return i;
    e_prev = e;
  }
  return T.n - 1;
}

void fireCues(TaichiState& s, float ta, float tb) {
  const Table T = chapterTable(s.chapter);
  float e_prev = 0.f;
  for (int i = 0; i < T.n; ++i) {
    const TaiKey& k = T.k[i];
    const float a = e_prev + moveS(k), e = a + holdS(k);
    const float ci = a + 0.1f * kPace * (float)k.extra;
    if (k.cue_in && ta < ci && ci <= tb) s.events = (uint16_t)(s.events | k.cue_in);
    if (k.cue_out && ta < e && e <= tb) s.events = (uint16_t)(s.events | k.cue_out);
    const float flip = a + 0.5f * holdS(k);
    if ((k.flags & TK_TURN) && ta < flip && flip <= tb) s.events = (uint16_t)(s.events | TE_TURN);
    e_prev = e;
  }
}

// ---- the butterfly ----
enum FlyMode : uint8_t { FM_OFF, FM_WAIT, FM_WANDER, FM_SETTLE, FM_SIT, FM_LEAVE, FM_GONE };
constexpr float kFlySpeed = 40.f;      // px/s at most: a butterfly drifts, it does not dart
constexpr float kFlyLag = 2.0f;        // seconds it lags the hand it follows
constexpr float kFlyAbove = 14.f;      // px over the leading hand
constexpr float kFlyRiseX = 26.f;      // px ahead of his hips: where it rises out of the grass
constexpr float kFlyRest = 0.9f;       // seconds it opens its wings there before it lifts

}  // namespace

// ---- public ----

float taiAtan2(float y, float x) {
  const float ax = fabsf(x), ay = fabsf(y);
  if (ax < 1e-20f && ay < 1e-20f) return 0.f;
  const bool steep = ay > ax;
  const float z = steep ? ax / ay : ay / ax;
  const float z2 = z * z;
  float a = z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
  if (steep) a = kHalfPi - a;
  if (x < 0.f) a = kPi - a;
  if (y < 0.f) a = -a;
  return a;
}

int taichiKeyCount(int chapter) { return chapterTable(chapter).n; }
const TaiKey& taichiKey(int chapter, int k) {
  const Table T = chapterTable(chapter);
  return T.k[k < 0 ? 0 : (k >= T.n ? T.n - 1 : k)];
}
float taichiKeyArrive(int chapter, int k) { return keyArriveT(chapterTable(chapter), k); }
float taichiKeyHoldEnd(int chapter, int k) { return taichiKeyArrive(chapter, k) + holdS(taichiKey(chapter, k)); }
const TaiHandsDef& taichiHandsDef(int posture) { return kHands[posture >= 0 && posture < TH_COUNT ? posture : 0]; }
float taichiFormSeconds(int chapter) { return tableSeconds(chapterTable(chapter)); }
float taichiGagSeconds(int chapter, uint8_t gag) { return kGagSeconds[gagKind(chapter, gag) & 3] * kPace; }
float taichiOutSeconds() { return kOutSeconds; }
float taichiPace() { return kPace; }

void taichiInit(TaichiState& s, uint8_t chapter, uint8_t gag, float x0, float y0, float g, int dir, uint32_t vary) {
  memset(&s, 0, sizeof s);
  s.chapter = (uint8_t)(chapter & 3);
  s.gag = gagKind(s.chapter, gag);
  s.phase = TP_FORM;
  s.dir = (int8_t)(dir < 0 ? -1 : 1);
  s.facing = s.dir;
  s.x0 = x0;
  s.y0 = y0;
  s.g = g > 0.f ? g : 1.f;
  s.vary_sink = (int8_t)((int)(vary % 7u) - 3);
  s.vary_hand = (int8_t)((int)((vary >> 8) % 7u) - 3);
  s.hip_x = x0;
  s.ground = y0;
  s.bump_side = 1;
}

void taichiStep(TaichiState& s, float dt) {
  if (!(dt > 0.f) || s.phase == TP_OFF || s.phase == TP_DONE) return;
  s.clk += dt;
  const float T = taichiFormSeconds(s.chapter) + s.tail;
  float rem = dt;
  for (int guard = 0; guard < 6 && rem > 0.f; ++guard) {
    if (s.phase == TP_FORM) {
      const float tn = s.t + rem;
      if (!s.gag_done && s.gag != TG_NONE) {
        const float at = gagStart(s.chapter, s.gag);
        if (at >= 0.f && s.t < at && tn >= at) {
          fireCues(s, s.t, at);
          rem = tn - at;
          s.t = at;
          s.key = (uint8_t)gagKey(s.chapter, s.gag);
          float dummy_h = 0.f, dummy_l = 0.f;
          holdExtras(chapterTable(s.chapter), s.t, s.hold_sink, dummy_h, dummy_l);
          s.gag_t = 0.f;
          if (s.gag == TG_BUMP) {
            s.phase = TP_WAIT;
            s.events = (uint16_t)(s.events | TE_BUMP_CALL);
          } else {
            s.phase = TP_GAG;
            s.gag_on = s.gag;
            s.gag_len = kGagSeconds[s.gag] * kPace;
            s.events = (uint16_t)(s.events | TE_GAG);
          }
          continue;
        }
      }
      if (tn >= T) {
        fireCues(s, s.t, T);
        rem = tn - T;
        s.t = T;
        s.key = (uint8_t)(taichiKeyCount(s.chapter) - 1);
        s.events = (uint16_t)(s.events | TE_DONE);
        s.phase = TP_OUT;
        s.out_t = 0.f;
        continue;
      }
      fireCues(s, s.t, tn);
      s.t = tn;
      s.key = (uint8_t)keyAt(chapterTable(s.chapter), s.t);
      rem = 0.f;
    } else if (s.phase == TP_WAIT) {
      s.gag_t += rem;
      rem = 0.f;
      if (s.gag_t >= kBumpWait * kPace) {   // Copper never came: the bump's seconds become stillness at the end
        rem = s.gag_t - kBumpWait * kPace;
        s.tail += (kGagSeconds[TG_BUMP] - kBumpWait) * kPace;
        s.gag_done = 1;
        s.gag_t = 0.f;
        s.phase = TP_FORM;
      }
    } else if (s.phase == TP_GAG) {
      s.gag_t += rem;
      rem = 0.f;
      if (s.gag_t >= s.gag_len) {
        rem = s.gag_t - s.gag_len;
        s.gag_t = 0.f;
        s.gag_on = 0;
        s.gag_done = 1;
        s.phase = TP_FORM;
        s.events = (uint16_t)(s.events | TE_GAG_END);
      }
    } else if (s.phase == TP_OUT) {
      s.out_t += rem;
      rem = 0.f;
      if (s.out_t >= kOutSeconds) {
        s.out_t = kOutSeconds;
        s.phase = TP_DONE;
      }
    } else {
      break;
    }
  }
  if (s.phase == TP_OUT || s.phase == TP_DONE) s.w = 1.f - easeInOut(s.out_t / kOutSeconds);
  else s.w = easeInOut(s.clk / kInSeconds);
}

bool taichiGag(TaichiState& s, uint8_t kind, int side) {
  if (kind != TG_BUMP || s.phase != TP_WAIT) return false;
  const float waited = s.gag_t;
  s.bump_side = (int8_t)(side < 0 ? -1 : 1);
  s.phase = TP_GAG;
  s.gag_on = TG_BUMP;
  s.gag_t = 0.f;
  s.gag_len = kGagSeconds[TG_BUMP] * kPace - waited;   // the bump's 12 s count from his call, wait included
  s.events = (uint16_t)(s.events | TE_GAG);
  return true;
}

void taichiPose(TaichiState& s, Hiker& h) {
  if (s.phase == TP_OFF) return;
  Chans c;
  chansNow(s, c);
  HikerPose P;
  const bool aid = s.turn_aid_off == 0;
  const float w = s.w;
  solve(c, s.g, aid, w, P);
  if (aid && !h.hat_on) P.head += kHeadLevel * c.ft;
  s.out = P;
  if (w < 1.f) {   // the torso and arms ease from the rig's own; the legs are the IK's throughout
    h.pose = mixPose(h.pose, P, w);
    for (int i = 0; i < 2; ++i) {
      h.pose.thigh[i] = P.thigh[i];
      h.pose.knee[i] = P.knee[i];
    }
  } else {
    h.pose = P;
  }
  const float H = h.height;
  s.hip_x = s.x0 + (float)s.dir * c.hip * s.g * H;
  h.feet = Vec2{s.hip_x, s.y0};
  h.facing = s.dir * (c.face < 0 ? -1 : 1);
  s.facing = (int8_t)h.facing;
  s.foot_turn = aid ? c.ft * w : 0.f;
  h.foot_turn = s.foot_turn;
  h.breath_rate = lerp(1.f, kBreathRate, w);
  h.breath_scale = lerp(1.f, kBreathScale, w);
  Fk f;
  fk(h, f);
  s.ground = s.y0;
  s.shoulder = f.shoulder;
  s.perch = f.perch;
  for (int i = 0; i < 2; ++i) {
    s.hand[i] = f.hand[i];
    s.foot[i] = Vec2{f.ankle[i].x, f.low[i]};
  }
}

void taichiFinish(TaichiState& s, Hiker& h) {
  h.foot_turn = 0.f;
  h.breath_rate = h.breath_scale = 1.f;
  s.foot_turn = 0.f;
  s.w = 0.f;
  s.phase = TP_DONE;
}

uint16_t taichiEvents(TaichiState& s) {
  const uint16_t e = s.events;
  s.events = 0;
  return e;
}

bool taichiInForm(const TaichiState& s) { return s.phase != TP_OFF && s.phase != TP_DONE; }
bool taichiDone(const TaichiState& s) { return s.phase == TP_DONE; }
bool taichiHolding(const TaichiState& s, float min_hold) {
  if (s.phase == TP_WAIT) return true;
  if (s.phase != TP_FORM) return false;
  const Table T = chapterTable(s.chapter);
  const int k = keyAt(T, s.t);
  const float a = keyArriveT(T, k), hold = holdS(T.k[k]);
  // From the moment the slowest channel (the head) has arrived until the hold ends.
  return hold >= min_hold && s.t >= a + kLagHead && s.t < a + hold;
}
Vec2 taichiFootFrame(const TaichiState& s, int i) { return s.foot[i ? 1 : 0]; }

// ---- the butterfly ----

void taichiFlyEnable(TaichiState& s, bool on) {
  s.fly_on = on ? 1 : 0;
  if (!on) s.fly_mode = FM_OFF;
  else if (s.fly_mode == FM_OFF) s.fly_mode = FM_WAIT;
}

void taichiFlyLeave(TaichiState& s) {
  if (s.fly_mode >= FM_WANDER && s.fly_mode <= FM_SIT) {
    s.fly_mode = FM_LEAVE;
    s.fly_t = 0.f;
  }
}

bool taichiFlyShown(const TaichiState& s) { return s.fly_on && s.fly_mode >= FM_WANDER && s.fly_mode <= FM_LEAVE; }

void taichiFlyStep(TaichiState& s, uint32_t tick) {
  if (!s.fly_on || s.fly_mode == FM_OFF || s.fly_mode == FM_GONE) return;
  const float dt = SS_TICK_DT;
  const float clk = (float)tick * dt;
  s.fly_t += dt;
  // The leading hand: the one further along his facing.
  const int facing = s.facing < 0 ? -1 : 1;
  const Vec2 lead = ((s.hand[0].x - s.hand[1].x) * (float)facing >= 0.f) ? s.hand[0] : s.hand[1];
  Vec2 target{0.f, 0.f};
  float lag = kFlyLag, speed = kFlySpeed;
  switch (s.fly_mode) {
    case FM_WAIT:
      if (s.phase != TP_OFF && s.t >= taichiKeyArrive(s.chapter, 1)) {   // from the commencing's top
        // It rises out of the grass a little ahead of him (the B2 reviewer's note: it used to appear in
        // mid-air): wings flat at first, a sliver in the grass, then a few slow beats, then up.
        s.fly_mode = FM_WANDER;
        s.fly_t = 0.f;
        s.fly = Vec2{s.hip_x + kFlyRiseX * (float)facing, s.y0 + 1.f};
        s.fly_open = 1.f;
        s.fly_dir = (float)facing;
      }
      return;
    case FM_WANDER:
      target = Vec2{lead.x + 9.f * sinT(clk * 0.13f), lead.y - kFlyAbove + 5.f * sinT(clk * 0.21f + 0.3f)};
      if (s.t >= taichiKeyArrive(s.chapter, taichiKeyCount(s.chapter) - 1) || s.phase >= TP_OUT) {
        s.fly_mode = FM_SETTLE;   // it settles on his shoulder for the closing's stand
        s.fly_t = 0.f;
      }
      break;
    case FM_SETTLE:
    case FM_SIT:
      target = Vec2{s.perch.x, s.perch.y - 1.5f};   // its body on the outline, the wings up against the sky
      lag = 0.8f;
      speed = 30.f;
      break;
    default: break;
  }
  const bool rising = s.fly_mode == FM_WANDER && s.fly_t < kFlyRest;
  if (rising) {
    // still in the grass
  } else if (s.fly_mode == FM_LEAVE) {
    s.fly.y -= (26.f + 40.f * s.fly_t) * dt;   // up and away, quicker as it goes
    s.fly.x += -12.f * s.fly_dir * dt + 0.3f * sinT(clk * 0.4f);
    if (s.fly.y < -14.f) s.fly_mode = FM_GONE;
  } else if (s.fly_mode == FM_SIT) {
    s.fly = target;
    if (s.phase == TP_DONE && s.fly_t > 3.0f) {   // he has finished: it lifts off and goes
      s.fly_mode = FM_LEAVE;
      s.fly_t = 0.f;
    }
  } else {
    float vx = (target.x - s.fly.x) / lag, vy = (target.y - s.fly.y) / lag;
    const float sp = sqrtf(vx * vx + vy * vy);
    if (sp > speed) {
      vx *= speed / sp;
      vy *= speed / sp;
    }
    s.fly.x += vx * dt;
    s.fly.y += vy * dt;
    if (vx > 2.f) s.fly_dir = 1.f;
    else if (vx < -2.f) s.fly_dir = -1.f;
    if (s.fly_mode == FM_SETTLE && fabsf(target.x - s.fly.x) < 0.8f && fabsf(target.y - s.fly.y) < 0.8f) {
      s.fly_mode = FM_SIT;
      s.fly_t = 0.f;
      s.fly_dir = -(float)facing;   // its wings lean back, away from his head
    }
  }
  // Wings: quick flutters in the air (about 6 a second), a slow open and close at rest.
  if (s.fly_mode == FM_SIT) s.fly_open = 0.2f + 0.6f * (0.5f + 0.5f * sinT(clk * 0.35f));
  else if (rising) s.fly_open = 1.f - 0.85f * (s.fly_t / kFlyRest) * fabsf(sinT(clk * 1.2f));
  else s.fly_open = 0.15f + 0.85f * fabsf(sinT(clk * 3.f));
}

Rect taichiFlyBounds(const TaichiState& s) {
  if (!taichiFlyShown(s)) return Rect{0, 0, 0, 0};
  const Vec2 p = s.fly;
  return Rect{(int)floorf(p.x - 5.f), (int)floorf(p.y - 7.f), (int)ceilf(p.x + 5.f), (int)ceilf(p.y + 4.f)};
}

void taichiFlyDraw(const TaichiState& s, Surface& surf, const Rect& clip, const Palette& pal) {
  if (!taichiFlyShown(s)) return;
  const Vec2 p = s.fly;
  const float d = s.fly_dir < 0.f ? -1.f : 1.f;
  // Seen from the side, wings up are tall and narrow, wings flat are a sliver.
  const float hgt = 1.0f + 3.6f * (1.f - s.fly_open);
  const Vec2 fore[3] = {p, Vec2{p.x - 0.9f * d, p.y - hgt - 0.6f}, Vec2{p.x + 2.4f * d, p.y - 0.8f * hgt - 0.3f}};
  const Vec2 hind[3] = {Vec2{p.x + 0.3f * d, p.y + 0.4f}, Vec2{p.x - 2.3f * d, p.y - 0.5f * hgt + 0.6f},
                        Vec2{p.x + 0.6f * d, p.y + 1.6f}};
  fillPolygon(surf, fore, 3, pal[ACTOR_LIGHT], clip);
  fillPolygon(surf, hind, 3, pal[ACTOR_LIGHT], clip);
}

int taichiDirty(const TaichiState& prev, const TaichiState& cur, Rect* out) {
  const bool a = taichiFlyShown(prev), b = taichiFlyShown(cur);
  if (!a && !b) return 0;
  if (a == b && prev.fly.x == cur.fly.x && prev.fly.y == cur.fly.y && prev.fly_open == cur.fly_open &&
      prev.fly_dir == cur.fly_dir)
    return 0;
  int n = 0;
  if (a) out[n++] = taichiFlyBounds(prev);
  if (b) out[n++] = taichiFlyBounds(cur);
  return n;
}

}  // namespace ss
