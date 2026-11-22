// The plinking (Mike, 2026-11-17): cans of five sizes along the food bag's branch, a takedown .22 that
// comes out of the pack, a kneeling hiker at the lower left who shoots them off one by one. A standalone
// module in the archery's style: plain data, commands, a step function, a pose bend, bounds, dirty marks
// and draws. It owns no World state, calls no platform code, makes no sound (Mike dislikes ambient
// sound) and draws random numbers only through the Rng the step is handed; the caller supplies his two
// hands, his facing and his height every tick.
//
// What the module draws, in two layers (the caller sorts them apart):
//   tree layer   the cans on the branch, in the air and on the ground, the bark chips and dust, the food
//                spilled from the bag, the cut rope's frayed end, the stick and the sack laid on the
//                ground
//   held layer   the rifle (two halves, joining, or whole) and its magazine, the stick in his near hand
//                with a can on its fork, the sack in his far hand, a can in his hand, the muzzle flash
//                and the smoke puff
//
// The shot machine. plinkQueue adds entries ({kind, can}); plinkStart runs them:
//   SK_HIT          raise the rifle (first shot 0.8 s), swing to the can (0.4 s), hold 0.9 to 1.3 s
//                   (1.6 s the first), fire; the bullet arrives 0.12 s later and the can pops
//   SK_MISS_U/L/R   the same, but the can stays: chips and dust jump off the branch under, left or
//                   right of it and the can rocks
//   SK_WEAK         a hit that only lifts the can a little: it lands back on the branch, teeters and falls
//                   by itself (variant A's gag)
//   SK_BAG          as a hit aimed at can 0, but the bullet cuts the bag's line (event PEV_BAG; the
//                   caller drops the bag and calls plinkSpill)
//   SK_CHECK        the rifle at eye level, his head bent to the sights, two small turns of the wrist
//   SK_SHRUG        the rifle lowered to the low carry, a shrug
//   SK_SCRATCH      the rifle lowered to the low carry, a scratch of the head
//   SK_FAR          the long shot's (day_longshot.h; Mike, 2026-11-18): swing to far_aim, hold far_hold (plus up
//                   to 0.5 s), fire; the bullet flies far_flight seconds and arrives as PEV_FAR. Nothing out
//                   there is the module's: the caller shows the hit or the miss
// The long shot's additions (LONGRANGE_PLAN.md section 7, step B5): `stay` keeps the rifle shouldered when
// the queue runs out (so a far string can be fed one shot at a time while he lies still), `rest` lies him
// down prone instead of kneeling (plinkBend, in three stages over 2.4 s), `scope` puts a small scope on
// the stock half, `sack_big` draws the sack as the stuffed shooting bag (1.7 times), `head_off` lifts his
// cheek off the stock, `settle` lets the cocked near heel down flat. All zero is the plinking, pixel for
// pixel and byte for byte.
//
// Each fired shot draws exactly two values from the Rng (how long he holds is drawn when the entry
// begins, how hard the can pops when it fires; both at the entry's start, so a doorbell freeze, which
// steps with dt 0, draws none).
//
// Safe handling the module keeps (and test_plink.cpp checks every tick): the rifle's axis always points
// the way he faces and never more than 0.2 rad above the highest can's line; it is loaded only at the
// firing spot (the caller loads it kneeling, facing downrange) and the low carry has the muzzle forward
// and 50 degrees down; plinkStart lowers it to the low carry when the string is done.
//
// Numbers marked PROPOSED are guesses; how it looks is UNVERIFIED until Mike has seen it (and on the
// panel).
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "hiker.h"

namespace ss {

#define PLINK_CANS 6
#define PLINK_BITS 9
#define PLINK_QUEUE 12
#define PLINK_PAIRS 3
// Owners of what is drawn (the dirty marks compare per owner): the cans, the bits, then these.
#define PLINK_OWN_ROPE (PLINK_CANS + PLINK_BITS)
#define PLINK_OWN_LYING (PLINK_OWN_ROPE + 1)
#define PLINK_OWN_RIFLE (PLINK_OWN_ROPE + 2)
#define PLINK_OWN_HELD (PLINK_OWN_ROPE + 3)
#define PLINK_OWN_FLASH (PLINK_OWN_ROPE + 4)
#define PLINK_OWN_PUFF (PLINK_OWN_ROPE + 5)
#define PLINK_OWNERS (PLINK_OWN_ROPE + 6)
#define PLINK_DIRTY_MAX PLINK_OWNERS

enum CanMode : uint8_t { CAN_OFF = 0, CAN_TIP, CAN_ON, CAN_FLY, CAN_DOWN, CAN_HELD };
enum PlinkBitKind : uint8_t { PB_NONE = 0, PB_CHIP, PB_DUST, FOOD_LOAF, FOOD_APPLE, FOOD_PACKET, FOOD_JAR, FOOD_BAR };
enum PlinkShot : uint8_t { SK_HIT = 0, SK_WEAK, SK_MISS_U, SK_MISS_L, SK_MISS_R, SK_BAG, SK_CHECK, SK_SHRUG, SK_SCRATCH, SK_FAR, SK_KINDS };
enum PlinkPhase : uint8_t { PP_IDLE = 0, PP_RAISE, PP_SWING, PP_HOLD, PP_FOLLOW, PP_GESTURE, PP_LOWER };
enum PlinkMag : uint8_t { MAG_NONE = 0, MAG_HAND, MAG_IN };
enum PlinkEvent : uint8_t { PEV_SHOT = 1, PEV_FIRST = 2, PEV_BAG = 4, PEV_DONE = 8, PEV_FAR = 16 };
enum PlinkItem : uint8_t { PI_NONE = 0, PI_HAND, PI_DOWN };   // the stick and the sack
enum PlinkRest : uint8_t { REST_KNEEL = 0, REST_PRONE };       // how plinkKneel puts him down

constexpr float kPlinkH = 90.f;        // his height, px (the rig's)
constexpr float kPlinkRifleLen = 50.f;   // the whole rifle, butt to muzzle (about 0.55 of his height)
constexpr float kPlinkStickLen = 72.f;   // the stick, and how far its end is from his hand
constexpr float kPlinkStickReach = 62.f;
constexpr float kPlinkCarryElev = -0.87f;   // the low carry: the muzzle 50 degrees below level

struct PlinkCan {
  uint8_t mode;       // CanMode
  uint8_t w, h;       // px, upright
  uint8_t style;      // 0 plain, 2 flat spin, 5 slow tumble
  uint8_t bounces;
  uint8_t aux;        // 1 the weak pop that lands back on the branch, 2 teetering on it
  uint8_t hand;       // CAN_HELD: in his far hand (0 near, 1 far)
  uint8_t pad;
  float x, y;         // the centre
  float vx, vy, rot, spin;
  float sq;           // flat spin phase, turns
  float land_x, land_y;   // where it comes to rest (its lowest point)
  float rot_land;
  float t;            // seconds in this mode
  float wob;          // rocking amplitude, rad
};

struct PlinkBit {
  uint8_t kind;       // PlinkBitKind
  uint8_t rest;       // lying still
  uint8_t bounces;
  uint8_t pad;
  float x, y, vx, vy, rot, land_y, t;
};

struct PlinkState {
  PlinkCan can[PLINK_CANS];
  PlinkBit bit[PLINK_BITS];
  uint8_t queue_kind[PLINK_QUEUE], queue_can[PLINK_QUEUE];
  uint8_t n_queue, cur, phase, gest;     // the machine; the gesture playing (an SK_ kind) or 0
  uint8_t rifle, mag, rounds, chambered; // the rifle is out of the pack; PlinkMag; 0..8
  uint8_t stick, sack, sack_n, rope_cut; // PlinkItem; cans in the sack
  uint8_t events, first_done, kneel_on, run;
  uint8_t bullet_kind, bullet_can;
  uint8_t stay;       // the long shot: the rifle stays up when the queue ends (PP_IDLE, `up` kept)
  uint8_t rest;       // PlinkRest
  int8_t dir;
  uint8_t scope;      // a scope on the stock half
  uint8_t sack_big;   // the sack is the shooting bag: 1.7 times bigger, soft where the rifle rests on it
  uint8_t pad1;
  float t, hold_len, up, kneel, elev, join, recoil, flash, gw;
  float pop_r, bullet_t, puff_t, stick_ang, bag_vy;
  float up_from;
  Vec2 puff, aim, aim_from, aim_to, near_hand, far_hand, down_at;
  Vec2 branch_root, branch_tip;
  float H;                       // his height, as of the last step
  float pair_x[PLINK_PAIRS];     // where the cans fall: three pairs
  float pair_y[PLINK_PAIRS];     // the ground they rest on
  // The long shot's (appended so the plinking's bytes above keep their places).
  Vec2 far_aim;                  // SK_FAR: what he aims at
  float far_flight;              // SK_FAR: the bullet's flight, s (instead of the plinking's 0.12 s)
  float far_hold;                // SK_FAR: the hold before the shot, s (plus up to 0.5 s from the Rng)
  float head_off;                // 0..1: his cheek lifted off the stock (prone)
  float settle;                  // 0..1: the near heel, cocked up as he lies down, let down flat
};

// An empty range for a branch from `root` (at the trunk) to `tip`, with the cans in their sack (CAN_OFF),
// nothing out of the pack. `pair_x`/`pair_y`: where the three pairs of cans come to rest.
void plinkInit(PlinkState& s, Vec2 root, Vec2 tip, int dir, const float pair_x[PLINK_PAIRS], const float pair_y[PLINK_PAIRS]);
void plinkFace(PlinkState& s, int dir);

// Angle of the vector (x, y) from straight down: 0 down, + toward +x (a table-free atan2 so the PC and the board agree).
float plinkAngle(float x, float y);
// The centre of can i standing on the branch, and its base.
Vec2 plinkCanSpot(const PlinkState& s, int i);
// The branch's centre line height at x.
float plinkBranchY(const PlinkState& s, float x);
// Where the can's centre is now.
Vec2 plinkCanPos(const PlinkState& s, int i);

// ---- commands (the script's) ----
void plinkSetCan(PlinkState& s, int i, uint8_t mode);       // CAN_OFF (in the sack), CAN_HELD, CAN_TIP
void plinkSetCanOn(PlinkState& s, int i, float u);          // u 0..1: from the stick's tip onto the branch
void plinkStick(PlinkState& s, uint8_t mode, float ang);    // in his near hand pointing `ang` (rig angle, from straight down, + toward his facing), or on the ground
void plinkSack(PlinkState& s, uint8_t mode);
void plinkLay(PlinkState& s, Vec2 at);                      // where the stick and the sack lie
void plinkRifle(PlinkState& s, bool out, float join);      // out of the pack; 0 two halves .. 1 one rifle
void plinkMag(PlinkState& s, uint8_t mag);
void plinkLoad(PlinkState& s);                              // the magazine in and a round chambered
void plinkUnload(PlinkState& s);                            // the magazine out, the chamber clear
void plinkKneel(PlinkState& s, bool on);
// The long shot's settings (all off in the plinking). See the note at the top.
void plinkStay(PlinkState& s, bool on);
void plinkRest(PlinkState& s, uint8_t rest);                // REST_KNEEL or REST_PRONE; set before plinkKneel
void plinkScope(PlinkState& s, bool on);
void plinkSackBig(PlinkState& s, bool on);
void plinkHeadOff(PlinkState& s, float k);
void plinkSettle(PlinkState& s, float k);
// Queue one far shot at `aim` (frame px): the bullet flies `flight_s`, he holds `hold_s` first. False when full.
bool plinkFar(PlinkState& s, Vec2 aim, float flight_s, float hold_s);
// Cans down within `r` px of x go into the sack. How many.
int plinkSweep(PlinkState& s, float x, float r);
// Food within `r` px of x is picked up. How many.
int plinkTakeFood(PlinkState& s, float x, float r);
int plinkFoodLeft(const PlinkState& s);
// A poke with the stick: can i leaves the branch and falls at `land_x`, weakly.
void plinkPoke(PlinkState& s, int i, float land_x);
// The bag lands at `at`: five items spill out of its neck, to the right. Draws exactly 5 values.
void plinkSpill(PlinkState& s, Vec2 at, float land_y, Rng& rng);

// Queue entries; false when full. plinkStart runs them (he must be kneeling, loaded and facing downrange).
bool plinkQueue(PlinkState& s, uint8_t kind, uint8_t can);
void plinkQueueClear(PlinkState& s);
void plinkStart(PlinkState& s);
// The string has played to its end (nothing queued or running) and nothing is in the air.
bool plinkStringDone(const PlinkState& s);
// Events since the caller last took them (PlinkEvent bits), cleared.
uint8_t plinkTakeEvents(PlinkState& s);
// A can, a chip or a bit is moving.
bool plinkSettled(const PlinkState& s);
// The rifle is out of the pack (in any form) or something else is in his hands.
bool plinkRifleOut(const PlinkState& s);
bool plinkHandsFull(const PlinkState& s);
bool plinkLoaded(const PlinkState& s);
// Everything the rifle shows: where its muzzle is now.
Vec2 plinkMuzzle(const PlinkState& s);
// The rifle's axis as drawn (unit, from the butt to the muzzle).
Vec2 plinkAxis(const PlinkState& s);
// Where the rifle's butt is (the rifle's anchor).
Vec2 plinkButt(const PlinkState& s);

// One tick; any dt up to 0.1 s works, 0 only follows his hands. `forearm` is unused for now.
void plinkStep(PlinkState& s, Vec2 near_hand, Vec2 far_hand, int dir, float height, float dt, Rng& rng);
// The doorbell froze the story: a muzzle flash showing now is switched off.
void plinkFreeze(PlinkState& s);

// ---- his pose ----
// Kneel, the rifle to the shoulder, the cheek on the stock, a shrug or a scratch. A no-op when none is on.
void plinkBend(const PlinkState& s, HikerPose& p);
bool plinkBends(const PlinkState& s);

// ---- bounds, dirty, draw ----
Rect plinkBounds(const PlinkState& s);
Rect plinkTreeBounds(const PlinkState& s);
Rect plinkHeldBounds(const PlinkState& s);
bool plinkSame(const PlinkState& a, const PlinkState& b);
// The areas that differ between two states (one per owner that changed). At most PLINK_DIRTY_MAX.
int plinkDirty(const PlinkState& before, const PlinkState& after, Rect out[PLINK_DIRTY_MAX]);
void plinkTreeDraw(const PlinkState& s, Surface& strip, const Rect& clip, const Palette& pal);
void plinkHeldDraw(const PlinkState& s, Surface& strip, const Rect& clip, const Palette& pal);
// Rasterizer calls the last draw issued (budget checks).
int plinkLastDrawPrimitives();

}  // namespace ss
