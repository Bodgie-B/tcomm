// The deer visit while the hiker is away, the standalone part (DEER_PLAN.md section 7.2 and 8; Mike's
// words, 2026-11-18: "when the hiker is away, a group of deer walk into the site, some stop to munch on
// grass, a couple investigate the tent, carefully, one fawn splashes in the stream, the momma deer lifts
// her head and tail alerts which the other deer alert to, and the Mom trots off the side of the screen").
//
// OWNERSHIP: skeleton by the foundation step (A1), filled in by step B3. No World in here: this file is a
// state machine the World side (day_herd.h, step C4) drives. Each tick the driver calls herdTick() with
// what it can see (which deer the director still has, which have arrived, where they stand, the doorbell,
// a house dog going out, a flash, a visitor at the panel) and gets back a short list of orders (spawn,
// walk, run, set an action, stamp, a water ring, a hoof print, the tent's twitch, the flap) plus, per deer,
// the continuous things to lay over its Critter every tick (herdView: the white flag, the ear, a sniff, a
// stamp). Everything random comes from the Rng the driver hands in (DayHerd::rng), one call per statement.
//
// The beat's clock is HerdState::t (seconds, advanced by herdTick); nothing reads w.tick.
//
// The driver's contract (step C4), per tick, in this order:
//   1. fill a HerdIn: dt (SS_TICK_DT), the HI_ flags, and per deer index in_use (the director still holds
//      its critter; true for a deer the driver has not spawned yet), arrived (critterArrived) and x;
//   2. herdTick(h, rng, in, out);
//   3. act on out.ev[0..n) in order: HC_ENTER spawns the critter (directorSpawn at x kHerdSpawnX, facing
//      right, scale herdScale(role), tone kDeerFawn for the fawn) and records its slot in HerdDeer::slot;
//      HC_WALK and HC_RUN look the spot up (herdSpot) and send it there (directorWalk with CA_MOVE or
//      CA_RUN; HS_OUT_L and HS_OUT_R leave the frame, released once out of sight); HC_ACT calls
//      critterSetAction; and so on, each as documented at HerdCmd;
//   4. for every deer in the scene lay herdView(d) over its Critter: tone = (fawn ? kDeerFawn : 0) | ear,
//      tail_add, head_add, fore_add;
//   5. an HE_HIT is a hoof coming down at the deer's feet: a puff of dust (a 1), a ring on the water (a 2).
// A deer the director scattered (directorOwns false) is the driver's to hand back as in_use = false.
//
// What a visit is (all numbers PROPOSED, UNVERIFIED on the panel; the pictures decide):
//  - Cue lists: for each role (the doe, her fawn, two yearlings, a second doe, a sixth) and each variant
//    (A the tent and the flap, B the false alarm at the tent, C the fawn copies the yearling) a list of
//    timed orders, from the walk in at the left edge to the settled picture. They are const tables built at
//    compile time from a few blocks (herdCues).
//  - The graze cycle (herdGrazeNext): head down 3.5 to 7 s, up and chewing 1.2 to 3 s, now and then one
//    step forward; no two changes within 0.3 s, never all heads down, the last head up is the doe's.
//  - The alarm: by the clock at kHerdSoftAt (or by a bell, a flash or a dog: a hard one). A soft alarm is
//    the wave: the doe's head, her flag, her stamps, then the others nearest first, then the doe wheels and
//    goes and each follower leaves when the one ahead is a body length past it (REVIEW_FIXES F12).
//  - The prints the herd leaves (herdPrintAdd and the rest): flat marks under everything, fading once he
//    has seen them.
#pragma once

#include <stdint.h>

#include "../anim/rng.h"
#include "../gfx/surface.h"
#include "../palette.h"

namespace ss {

#define HERD_MAX 6
#define HERD_PRINTS 14
#define HERD_EV_MAX 48

// REVIEW_FIXES F12 moved the soft alarm from 80 s to 77 and asks for the last stillness (the empty camp
// before he walks in) to stay about 8 s. The walk out as built (each follower a body length behind, the
// fawn's slip and look back) puts the last deer out of the frame near 98 s for a herd of five, so the beat
// was 107 s, F12's other figure, and not the plan's 104: about 9 s of empty camp for five deer, 7.5 for six
// (review of step B3, 2026-11-18). Step C4 (2026-11-19) moved the alarm to 80 s and the beat to 110 s: the
// settled picture before the alarm was only 4.4 s in variant A (the B3 reviewer's note), and the loop's fit
// has the room. A shy herd (Memory::brave for deer is clear) hears it 3 s sooner.
constexpr float kHerdSoftAt = 80.f;
constexpr float kHerdShyEarlier = 3.f;
constexpr float kHerdSoftOff = 6.0f;      // seconds after a soft alarm that the doe wheels and goes
constexpr float kHerdHardOff = 1.1f;      // after a hard one that everyone goes (the fawn 0.3 s later)
constexpr float kHerdSeconds = 110.f;            // the beat's length (day_herd.cpp's herdSeconds reads this)
constexpr float kHerdSpawnX = -44.f;             // a deer enters at this x, out of the frame
constexpr float kHerdExitLX = -80.f, kHerdExitRX = 880.f;

// The plan byte, LoopPlan::herd: 0 none; bits 0-1 the variant (1 A, 2 B, 3 C), bits 2-3 the size
// (0 five deer, 1 six, 2 four: no second doe).
inline int herdVariantOf(uint8_t plan) { return (plan & 3) - 1; }   // 0 A, 1 B, 2 C
inline int herdSizeOf(uint8_t plan) { return (plan >> 2) & 3; }

enum HerdRole : uint8_t {
  HR_DOE = 0,    // the mother: the watch, the first in, the first to see, the first out
  HR_FAWN,       // her fawn, 0.62 of her size: the splash
  HR_DOOR,       // a yearling at the tent's door (the one that reaches for the flap)
  HR_WALL,       // a yearling at the tent's wall (the bolder one)
  HR_GRAZER,     // the second doe, the one who eats
  HR_GRAZER2,    // a sixth deer, in front of the camp pine
  HR_COUNT
};
enum HerdMode : uint8_t { HM_OFF = 0, HM_CUES, HM_GRAZE, HM_ALERT, HM_OUT, HM_GONE };
enum HerdTrigger : uint8_t { HT_TIME = 0, HT_BELL, HT_DOG, HT_FLASH };

// ---- the orders a cue gives (and the events herdTick hands back) ------------------------------------------
// Frame px; speeds are true px/s on the screen: the driver divides by the critter's own (0.8 + 0.4 *
// speed_dial) factor (critterMoveTo multiplies by it), so a speed here is the speed seen whatever the
// loop's dials are.
enum HerdCmd : uint8_t {
  HC_ENTER = 0,   // a: lane y. Spawn the deer off the left edge (x kHerdSpawnX), facing right.
  HC_WALK,        // a: HerdSpot, b: px/s. Walk there (CA_MOVE). The deer's later cues marked `wait` hold
                  //    until it has arrived.
  HC_RUN,         // a: HerdSpot, b: px/s. The same at a canter (CA_RUN).
  HC_BACK,        // a: px, b: px/s. A short step back, still facing the same way (critterMoveTo back = true;
                  //    a canter when b >= 60).
  HC_FACE,        // a: +1 right, -1 left. Turn in place (a step of a pixel toward that side starts the turn).
  HC_ACT,         // a: CritterAction, b: blend in 1/100 s. (A walk in progress is dropped.)
  HC_GRAZE_ON,    // join the graze cycle (the cycle then gives HC_ACT orders of its own)
  HC_GRAZE_OFF,   // leave it
  HC_ALERT,       // a: 1 ears forward and held (an alert deer), 0 back to ordinary ears
  HC_TAIL,        // a: the white flag's target in 1/100 (0 to 100), b: the seconds to get there in 1/100.
                  //    herdView gives the flag now: lay it on Critter::tail_add every tick
  HC_STAMP,       // a: 0 a stamp, 1 a stamp with a puff of dust, 2 pawing the water. herdView gives the
                  //    lifted hoof (Critter::fore_add); the strike comes back as an HE_HIT event
  HC_EAR,         // one ear flick now (herdView gives the ear: Critter::tone)
  HC_NUDGE,       // a: Critter::head_add peak in 1/100 rad (signed), b: seconds in 1/100: a sniff, a bob
  HC_RING,        // a ring on the water at the deer's nose (BIT_RING; a: spin in 1/100)
  HC_DROPS,       // a: how many drops thrown up at the deer's feet (BIT_DROP)
  HC_PRINT,       // a: HP_WET or HP_SCUFF, b: x offset in px: a hoof print at the deer's feet (herdPrintAdd)
  HC_WOBBLE,      // a: 1 a flinch (+2, -1, 0 over 12 ticks), 2 a touch (+1 for 3 ticks), 3 the zip pulled, 4 a tug
  HC_DOOR,        // a: the flap's opening in 1/100 (0 to 35), b: seconds in 1/100 to get there
  HC_HOLD,        // a: seconds in 1/100. The deer's list waits this long (after its arrival, when `wait`)
  HC_NIBBLE,      // the second doe starts on the cattail (INTEGRATION_PLAN 4.1): the driver bends the stalk to her
                  //    nose, tugs it with her and bites the head off 2.8 s later, from its own story clock
  // events only (never in a table)
  HE_STEP,        // a: px forward, b: px/s. One step of the graze cycle (critterMoveTo(feet.x + facing * a))
  HE_HIT,         // a: 0 a stamp, 1 with dust, 2 a paw (ring): the hoof has come down at the deer's feet
  HE_GONE,        // the deer is out for good (the driver may release its slot)
  HC_COUNT
};
enum HerdPrintKind : uint8_t { HP_WET = 0, HP_SCUFF };

// Where a deer walks to. Points from the camp's layout (herdSpot); the way in and out are per role.
enum HerdSpot : uint8_t {
  HS_LOOKIN = 0,   // the first stop (per role): head and neck out past the foreground pines, the body behind them
  HS_WAY2,         // the doe's second stop, out in front of the pack and the tent
  HS_FSTOP,        // where the fawn stops behind her
  HS_G0,           // the doe's grazing place
  HS_GF,           // the fawn's, 54 px behind her
  HS_G4,           // the second doe's
  HS_G4F,          // a spot beside it (the second look's fawn)
  HS_G5,           // the sixth's, in front of the camp pine
  HS_T2WAIT,       // 24 px short of the tent's door
  HS_T2HALF,       // half way from there
  HS_T2,           // at the door
  HS_T2BACK,       // two steps back from it
  HS_D2OUT,        // where the door yearling ambles to and grazes
  HS_T3GRAZE,      // 20 px short of the wall
  HS_T3,           // at the wall
  HS_WATER,        // the fawn's place at the water's edge
  HS_SP26,         // 26 px along the shallows, 8 px back, 10 px along
  HS_SM8,
  HS_SP10,
  HS_FSLIP,        // 10 px toward where its mother stood (the wrong way)
  HS_FLOOK,        // in front of the pack, where it stops and looks back before it goes behind the pines
  HS_FTENT,        // beside the door yearling, the fawn's own nose at the flap
  HS_OUT_L,        // off the left edge, on the role's lane
  HS_OUT_R,        // off the right edge, along y 346
  HS_N4,           // the second doe's first place, at the fireside cattail (HerdLayout::nibble; step C4)
  HS_N4VIA,        // on her way there: 44 px short of it, up on the camp's ground (she goes round the pit)
  HS_COUNT
};

struct HerdDeer {              // 92 bytes on the host
  int8_t slot;                 // director slot (the driver's); -1 not in yet, or gone
  uint8_t role;                // HerdRole
  uint8_t mode;                // HerdMode
  uint8_t cue;                 // index of its next cue in its list
  uint8_t alert;               // 0 none, 1 head up and ears forward, 2 tail up (set by cues), 3 leaving
  uint8_t ear;                 // ticks left of an ear flick
  uint8_t head_down;           // grazing now (the watch rule reads it)
  uint8_t pad;
  float t;                     // seconds since it entered
  float next;                  // graze cycle: seconds to the next change
  float ear_next, flick_next;  // seconds to the next ear flick, tail flick
  float tail, tail_to;         // the flag now and where it is going (0..1)
  float stamp;                 // seconds into a stamp, < 0 none
  Vec2 spot;                   // its settled place
  // ---- added by the standalone part (step B3) ----
  float base;                  // the seconds its list's times count from
  float leave;                 // when it starts out (a soft or hard alarm), < 0 not set
  float tail_rate;             // flag units a second toward tail_to
  float nudge, nudge_t, nudge_d;   // head_add: peak (rad), seconds into it, how long it takes; nudge_d 0 none
  float look_t;                // a visitor at the panel: seconds of heads-up left, 0 none
  float lift;                  // the hoof, radians (Critter::fore_add)
  float hold_t;                // seconds its list is held (HC_HOLD), 0 none
  uint8_t list;                // HerdList: which cue list `cue` indexes
  uint8_t hard;                // 1: it leaves by the hard exit (a bell, a flash, a dog, or it was off the frame)
  uint8_t flick;               // ticks left of a tail flick
  uint8_t grazing;             // in the graze cycle
  uint8_t face;                // 1 right, 0 left (for the graze steps)
  uint8_t walk_ticks;          // ticks since its last walk order (an arrival counts only after two)
  uint8_t entered;             // ticks since it entered, saturating at 255; 0 not yet
  uint8_t stamp_kind;          // HC_STAMP's a
  uint8_t stepping;            // a graze step is under way
};
struct HerdPrint {             // 8 bytes
  int16_t x, y;                // the top left of the mark
  uint8_t a0, a1;              // alpha when made, alpha once dry (a wet print dries over 40 s)
  uint16_t made;               // when, in 1/10 s of the beat's clock
};
struct HerdState {             // 748 bytes on the host (the union's place is the bike's, 1,904)
  HerdDeer d[HERD_MAX];
  HerdPrint print[HERD_PRINTS];
  uint8_t n, variant, n_prints, block;
  uint8_t alarm;               // 0 none, 1 soft, 2 hard
  uint8_t trigger;             // HT_TIME, HT_BELL, HT_DOG, HT_FLASH
  int8_t flee;                 // -1 left, +1 right
  uint8_t looks;               // passer-by looks so far
  uint8_t events;              // set from outside: bit 0 the bell
  uint8_t prev_in;             // last tick: bit 0 passer_by, bit 1 a dog wanted out (starts 0: F21)
  uint8_t signs;               // bit 0 the flap is ajar, bit 1 prints lie, bit 2 he has seen them
  uint8_t door_step, print_q, second, noshow, shy;
  float t;                     // seconds into the beat
  float alarm_t, gone_t;       // when the alarm fired; when the last deer left the frame (< 0 not yet)
  float seen_t;                // seconds since he looked at the prints (the fade), < 0 not yet
  float wobble_t;              // seconds into the tent's twitch, < 0 none
  Vec2 tent, door, splash;     // layout, set at begin
  // ---- added by the standalone part (step B3) ----
  Vec2 fire;                   // the fire's place (the layout's)
  float camp_x;                // the camp pine's x
  float last_change;           // the beat's clock at the last graze head change (the 0.3 s rule)
  float second_t;              // when the second look began (the beat's clock), < 0 not
  uint8_t size;                // 0 five, 1 six, 2 four
  uint8_t second_done;         // the second look has been given (once)
  uint8_t alive;               // deer in the scene now (entered, not gone)
  uint8_t nibble_on;           // the second doe goes to the fireside cattail first (HerdLayout::nibble_on)
  // ---- added by the World side (step C4): the two meetings with the reeds (INTEGRATION_PLAN 4.1, 4.2)
  Vec2 nibble;                 // where the second doe stands to bite the cattail
  Vec2 fawn_reed;              // where the fawn's third dash ends, nose in the upstream clump (x 0: the plain dash)
  float edge_x;                // the foreground pines' right edge (HerdLayout::edge_x)
  float g0_max;                // the doe's grazing place no further right than this (HerdLayout::g0_max; 0 none)
};

// The camp as the beat sees it at its begin (frame px): the fire, the tent (camp.tent), the camp pine's x
// and the fawn's water point (about F.x + 190, the bank's top + 4: from the landscape's stream).
struct HerdLayout {
  Vec2 fire, tent, door, splash;
  float camp_x;
  // Step C4, the reeds (both off when zero): the second doe's place at the fireside cattail, and where the
  // fawn's third dash ends with its nose in the upstream clump.
  // The right edge of the foreground pines' foliage at the left of the frame (landscapeFgPineBounds): a deer
  // left of it is hidden, so the looks in are placed against it. 0: taken as 150.
  float edge_x = 0.f;
  bool nibble_on = false;
  Vec2 nibble = Vec2{0.f, 0.f};
  Vec2 fawn_reed = Vec2{0.f, 0.f};
  // Step C6 (test_stage22 [6], INTEGRATION_PLAN 8.6): the doe's grazing place G0 keeps 12 px from the upstream
  // clump's stalks and leaves; G0 and her fawn's GF step left by what it lacks. 0: no limit.
  float g0_max = 0.f;
};

// One step of a deer's cue list (const tables in herd.cpp): `at` the earliest time (1/100 s from the
// list's base), `wait` 1 when the deer must have arrived first, `cmd` a HerdCmd, a and b its arguments.
struct HerdCue {
  uint16_t at;
  uint8_t cmd;
  uint8_t wait;
  int16_t a, b;
};

enum HerdList : uint8_t {
  HL_MAIN = 0,    // the walk in, the grazing and the vignette, by variant (times from the beat's start)
  HL_SECOND,      // the second look (the doe and the fawn only; times from second_t)
  HL_WAVE_SOFT,   // the alarm's wave (times from the alarm)
  HL_WAVE_HARD,
  HL_EXIT_SOFT,   // out of the frame to the left, by role (times from the deer's own leave time)
  HL_EXIT_HARD_L, // a plain run out at 115 px/s, to the left or the right (a bell, a flash, a dog); any role
  HL_EXIT_HARD_R,
  HL_COUNT
};

// ---- the herd -------------------------------------------------------------------------------------------

// Who is in a herd of this size, in the order of HerdState::d (returns how many).
int herdRoles(uint8_t size, uint8_t roles[HERD_MAX]);
// How many cues the tables hold in all (for the capacity report).
int herdCueCount();
// The Critter::scale each role is drawn at (a doe 1, her fawn 0.62, the yearlings 0.90 and 0.86, the second
// doe 0.96, the sixth 0.92; DEER_PLAN section 2). A fawn also takes Critter::tone = kDeerFawn.
float herdScale(uint8_t role);
// How far a deer's drawn bounds reach to the left and to the right of its feet (frame px; the most of any
// pose: measured critterBounds of the idle, look, graze, stretch, startle, head turn, walk, canter, shake).
void herdReach(uint8_t role, bool facing_right, float& left, float& right);
// Index in HerdState::d of a role, or -1.
int herdIndexOf(const HerdState& h, uint8_t role);

// The first call: zero the state, set the layout, the variant, the size and the roles, each deer's own
// timers (ear, tail flick) from `rng`, and its settled place. `plan` is LoopPlan::herd (not 0). `shy`: the
// loop's deer are shy (Memory::brave clear): looks 30 % longer and the alarm 3 s sooner. The first deer's
// cue list starts at once (its ENTER is the first cue); nothing is in the scene until the driver acts on an
// HC_ENTER order.
void herdInit(HerdState& h, Rng& rng, const HerdLayout& layout, uint8_t plan, bool shy);

// A cue list: the const table for a list, a role and a variant (0 A, 1 B, 2 C); nullptr and n = 0 for none.
// `list` HL_SECOND, HL_WAVE_* and HL_EXIT_* ignore the variant, HL_EXIT_HARD_* the role too.
const HerdCue* herdCues(uint8_t list, uint8_t role, int variant, int& n);

// The point a spot id means for a role, from the layout kept in the state.
Vec2 herdSpot(const HerdState& h, uint8_t spot, uint8_t role);

// What the driver tells the machine each tick.
enum HerdInFlags : uint8_t {
  HI_BELL = 1,     // a doorbell rang since the last tick (a hard alarm, they leave to the left)
  HI_PASSER = 2,   // someone is at the upstairs panel now (the rising edge makes three of them look)
  HI_DOG = 4,      // a house dog wants out now (the rising edge is a hard alarm, they leave to the right)
  HI_FLASH = 8,    // a lightning strike is on (a hard alarm, to the left)
  HI_WIND = 16,    // gusty: ears flick twice as often
  HI_RAIN = 32,    // raining: grazing bouts 30 % shorter
};
struct HerdIn {
  float dt;
  uint8_t flags;
  bool in_use[HERD_MAX];     // the director still has the deer's critter
  bool arrived[HERD_MAX];    // critterArrived: its walk is over
  float x[HERD_MAX];         // its feet x
};
struct HerdEv {
  uint8_t deer;              // index in HerdState::d
  uint8_t cmd;               // HerdCmd
  int16_t a, b;
};
struct HerdOut {
  HerdEv ev[HERD_EV_MAX];
  uint8_t n;
};

// One tick of the visit (the beat's update, once a tick, not during the doorbell's freeze). Advances the
// beat's clock, runs every deer's cue list, the graze cycle, the alarm and the walk out, and hands back
// the orders. Orders are in deer order and cue order. The driver acts on them after the call.
void herdTick(HerdState& h, Rng& rng, const HerdIn& in, HerdOut& out);

// What to lay over a deer's Critter this tick (after the orders): the ear bits for Critter::tone (or
// them into the fawn's kDeerFawn), the white flag, a sniff, the lifted hoof.
struct HerdView {
  uint8_t ear;        // critters.h kDeerEarBack, kDeerEarMid, kDeerEarFwd bits (0 ears up)
  float tail;         // Critter::tail_add
  float head_add;     // Critter::head_add
  float fore_add;     // Critter::fore_add
};
HerdView herdView(const HerdDeer& d);

// The graze cycle's length for the bout that starts now: head down 3.5 to 7 s, head up 1.2 to 3 s, times
// `scale` (a shy herd 1.3, rain 0.7 for the down bouts). Two draws, one per statement.
float herdGrazeNext(Rng& rng, bool head_down_now, float scale);

// The watch rule: may deer `i` put its head down now? Not if every other deer in the scene has its head
// down already (so never all heads down, and the doe, when the others are all down, stays up).
bool herdMayLower(const HerdState& h, int i);

// The alarm by the driver's own cause (a doorbell reaches it through HI_BELL in herdTick; this is for the
// tests): `kind` HT_TIME (soft) or HT_BELL, HT_DOG, HT_FLASH (hard; HT_DOG leaves to the right).
void herdAlarm(HerdState& h, int kind, const HerdIn& in);
// When the soft alarm sounds for this herd.
float herdSoftAt(const HerdState& h);

// ---- the hoof prints --------------------------------------------------------------------------------------
// Flat marks on the ground under everything: two 2 px dashes side by side, 3 px long, in ACTOR with a
// quantized alpha. A wet one (the fawn out of the water) starts at 150 and dries to 70 over 40 s; a scuff is
// 90 and stays. Once he has seen them (herdPrintsSeen) they fade to nothing over 25 s in 8 steps.
bool herdPrintAdd(HerdState& h, int x, int y, bool wet);   // false when HERD_PRINTS are down
void herdPrintsSeen(HerdState& h);                          // he looked: the fade starts
void herdPrintsStep(HerdState& h, float dt);                // age the prints after the beat (not while frozen)
uint8_t herdPrintAlpha(const HerdState& h, int i);          // quantized (steps of 8), 0 when gone
bool herdPrintsLive(const HerdState& h);                    // any print still drawn
bool herdPrintsDone(const HerdState& h);                    // seen and faded: the driver may zero the union
Rect herdPrintsBounds(const HerdState& h);                  // every pixel the draw can touch (empty: none)
void herdPrintsDraw(const HerdState& h, Surface& s, const Palette& pal, const Rect& clip);
// True when a drawn pixel differs between two states; `out` is the rows to mark (old marks and new).
bool herdDirty(const HerdState& prev, const HerdState& cur, Rect& out);
bool herdSame(const HerdState& a, const HerdState& b);     // everything the prints' draw reads is equal

}  // namespace ss
