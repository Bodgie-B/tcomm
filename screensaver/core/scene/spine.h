// The story spine: this loop's variant picks (LoopPlan), the camp layout the
// beats walk between, and the beats themselves (brief section 6).
//
// Each spine beat is a short script: a list of steps (walk to a place, take a
// pose and hold it, run a timed action on a prop) that a tiny interpreter in
// spine.cpp runs one tick at a time. A variant is just another step list. The
// beat's duration is fixed per variant and longer than its script, so every
// beat ends with the hiker at rest; a "pause" beat (8 to 20 s, only ambient
// moves) follows each one. If a script ever runs past its beat, the beat's end
// hook counts an overrun (tests require zero).
//
// Loop timeline (spineBuildTimeline): build, arrival, setup, firewood, fire,
// dinner, foodbag, bear, close, each but the last followed by a pause. Calm
// fillers (0 to 4, hiker only) sit in the pauses after fire and dinner (and
// after setup when there are 4), each with a pause of its own after it. Then
// the surprise majors (2 to 4, majors.h): at most one after each of setup,
// fire, dinner and foodbag, each as still / major / still. The loop ends when
// the close does (its last 2.5 s fade the frame to black), so a loop's length
// is the sum of its beats: 480 to 660 s. To land there the plan squeezes the
// pauses (8 to 20 s), then drops fillers down to one, majors down to two, the
// last filler, and shortens the still beats; then stretches the pauses.
//
// That is the evening family. A loop plays one family (SpineFamily below),
// picked from the inputs at loop start; the functions here dispatch on
// LoopPlan::family (spine_family.h; the morning is spine_morning.cpp, the
// midday spine_midday.cpp).
#pragma once

#include "../anim/rng.h"
#include "../anim/scheduler.h"
#include "../gfx/surface.h"

namespace ss {

struct Inputs;
struct World;
struct Memory;

// Which spine a loop plays (SPINES_WEATHER_LOOKOUT_SPEC.md section 1), picked
// at loop start from the inputs alone (spineFamilyFor: no rng). Evening is
// the stage 4 to 6 spine (arrival .. close); morning wakes in fog and breaks
// camp (spine_morning.cpp); midday comes back from a day hike, lunches,
// wades, naps (spine_midday.cpp: plays when forced; the mood does not pick it
// yet). The Lookout (lookout.cpp) is the sunset set piece on its own
// landscape, played in the host's sunset window.
enum SpineFamily : uint8_t { SF_EVENING = 0, SF_MORNING, SF_MIDDAY, SF_LOOKOUT, SF_COUNT };
const char* spineFamilyName(int family);
// The family this loop plays: in.spine_force when set, else the Lookout in
// its window, the evening at night, the morning at mood [0.5, 2.5), midday at
// [2.5, 3.5), the evening otherwise.
uint8_t spineFamilyFor(const Inputs& in);

enum SpineBeatId : uint8_t {
  SB_ARRIVAL = 0, SB_SETUP, SB_FIREWOOD, SB_FIRE, SB_DINNER, SB_FOODBAG, SB_BEAR, SB_CLOSE, SB_COUNT
};

// Calm fillers for the long gaps (brief section 8): the hiker alone, 30 to 60 s
// each, with real stillness in them.
enum FillerId : uint8_t {
  FILL_MUG_SUNSET = 0, FILL_READ, FILL_CAIRN, FILL_SKIP_STONE, FILL_CLOUD_WATCH, FILL_STREAM_LISTEN,
  FILL_BIRDS_ON_WATER,   // birds come down on the stream while he watches (majors.cpp drives them)
  FILL_COUNT
};

#define SPINE_FILLER_MAX 4
#define SPINE_MAJOR_MAX 4

struct LoopPlan {
  // Variant index per beat. Dinner 3 (fish_dinner) is never drawn at random:
  // the plan sets it whenever fishing_good (majors.h) is placed, since that
  // major leaves a fish on a stick over the fire for it.
  uint8_t arrival, setup, firewood, fire, dinner, foodbag, bear, close;
  uint8_t n_fillers;
  uint8_t filler[SPINE_FILLER_MAX];          // FillerId, in play order
  uint8_t filler_after[SPINE_FILLER_MAX];    // SpineBeatId each one follows
  uint8_t filler_pause_s[SPINE_FILLER_MAX];  // seconds of stillness after each filler
  uint8_t pause_s[SB_COUNT];    // seconds of stillness after each spine beat (none after close)
  uint8_t away_s;               // firewood: seconds out of frame on the first trip
  uint8_t cairn_add;            // cairn filler: stones he adds (4..6, capped by CAIRN_MAX)
  uint8_t skip_n;               // skip_stone filler: touches on the water (3, sometimes 4)
  uint8_t steam_n;              // dinner b: steam puffs when the pot boils over (3..5)
  // Surprise majors (majors.h), in play order; each follows its spine beat's
  // pause and fillers, with a still beat before and after it.
  uint8_t n_majors;
  uint8_t major[SPINE_MAJOR_MAX];          // MajorId
  uint8_t major_after[SPINE_MAJOR_MAX];    // SpineBeatId it follows: setup, fire, dinner or foodbag
  uint8_t major_var[SPINE_MAJOR_MAX];      // its variant (see majors.cpp)
  uint8_t major_pre_s[SPINE_MAJOR_MAX];    // seconds of stillness before it (4..8)
  uint8_t major_post_s[SPINE_MAJOR_MAX];   // and after it (4..8)
  // From memory (memory.h): the cairn's stones at loop start, and whether
  // setup ends with him patching the mouse hole in the tent.
  uint8_t cairn_start;
  bool patch;
  // The family (SpineFamily). The fields above are the evening's (the morning
  // and the midday reuse the pause, filler, major, cairn and skip fields, with
  // their own beat ids in filler_after, major_after and pause_s). The new families' variant per
  // beat, in the family's beat order, and how many beats it has.
  uint8_t family;
  uint8_t fvar[8];
  uint8_t n_fbeats;
  // Morning: the fog it opens in (fog_steps x 0.05 added to the weather's fog),
  // held until tick fog_t0 and lifted to 0 by tick fog_t1 (the end of the
  // coffee beat's pause), in 0.05 steps.
  uint8_t fog_steps;
  uint32_t fog_t0, fog_t1;
  // The Lookout (lookout.cpp): each beat's fitted seconds (LookoutBeatId
  // order; the pauses after reveal, climb and explore are in pause_s), its
  // sunset's palette steps and how many birds cross in it.
  uint8_t fsec[8];
  uint8_t fsteps, fbirds;
};

// Variants a beat draws from at random, and a plan's pick. (Dinner also has
// variant 3, fish_dinner, which only fishing_good brings: SPINE_DINNER_FISH.)
int spineVariantCount(int beat);
#define SPINE_DINNER_FISH 3
uint8_t spineVariant(const LoopPlan& p, int beat);
const char* spineBeatName(int beat);
const char* spineFillerName(int filler);
// Seconds a filler lasts in this plan (the cairn grows with its stones).
int spineFillerSeconds(const LoopPlan& p, int filler);

// Where things are this loop, from the fire pit and the landscape. Frame px;
// "feet" points are where the hiker stands to do the thing.
struct CampLayout {
  Vec2 spot;        // arrival: where he decides to camp (right of the fire)
  Vec2 past;        // arrival b: where he overshoots to
  Vec2 tent;        // tent base center (left of the fire, behind it)
  Vec2 tent_work;   // feet while pitching it, at its front corner
  Vec2 pack;        // pack's place beside the tent
  Vec2 peg_rock;    // setup a: where he finds a rock to hammer with
  Vec2 wood;        // wood pile base, behind and left of the pit
  Vec2 wood_work;   // feet while setting wood down
  Vec2 blow;        // feet while blowing on the fire
  Vec2 sit;         // feet while sitting by the fire
  Vec2 cool;        // feet at the stream bank, cooling his hand
  Vec2 rock_sit;    // arrival c: feet sitting by a rock across the stream
  Vec2 ford0, ford1;   // arrival c: near and far bank of the crossing
  Vec2 edge_l, edge_r; // just out of frame, left and right
  Vec2 cairn;       // cairn base, on the bank
  Vec2 pack_work;   // feet while getting things out of the pack (facing it, on its right)
  Vec2 bag_work;    // feet under the bag branch
  Vec2 door;        // feet at the tent door
  Vec2 pot_down;    // dinner b: where the pot is set down off the fire
  Vec2 view;        // mug_sunset: sits looking out over the valley
  Vec2 read;        // read: sits with his back to the reading rock
  Vec2 lie;         // cloud_watch: hips while lying in the grass
  Vec2 listen;      // stream_listen: sits on the bank
  Vec2 skip;        // skip_stone: crouches at the water
  Vec2 stones[2];   // cairn: where he picks the flat stones, either side of it
  // The morning's places (spine_morning.cpp sets them at loop start).
  Vec2 guy_peg;     // feet at the guy line's peg, in front of the door
};

struct Step;   // spine.cpp

struct ScriptRun {
  const Step* steps;
  uint8_t n;
  uint8_t i;           // step running
  bool entered;        // step i has started
  uint16_t ticks;      // ticks since step i started
  uint8_t leg;         // OP_GO: 0 walking to `via`, 1 to `goal`
  Vec2 via, goal;
  uint16_t beat_ticks; // ticks the beat has run
  uint16_t done_at;    // beat_ticks when the script finished (0 while running)
};

struct SpineState {
  CampLayout camp;
  ScriptRun run;
  uint8_t overruns;    // spine beats that ended before their script
  float u0;            // scratch value an action keeps across its ticks (start of a tween)
  Vec2 v0;             // scratch point (where a tween started)
  uint8_t target;      // scratch count (the cairn's stone count to reach)

  // Critter scenes (stage 5). Slots in World::critters, -1 when none.
  int8_t bear, deer, chip, thief;
  uint8_t bites;       // bites and sips so far (the dinner actions count them)
  bool bite_up;        // the current bite's hand is up (edge detector)
  uint8_t deer_steps;  // bites the deer has answered with a step
  uint8_t chip_stage, thief_stage;
  uint8_t thief_loot;  // ItemId + 1 the night thief goes for (shoe, else mug or book); 0: it only sniffs
  float deer_t, chip_t, thief_t;   // seconds in the current stage
  float shadow_v;      // the tent shadow's lagged x offset (the bear beat)
  float fear;          // what the shadow's jitter eases toward: 0 calm, ~0.05 frozen, 1 trembling
  float bag_v;         // the hung bag's swing speed, rad/s (a damped swing the bear sets off)
  bool bag_osc;        // the damped swing is running

  // The other families' scratch (spine_morning.cpp): two points for the
  // AT_FAM0/AT_FAM1 anchors, one critter slot (-1 none), its stage and clock.
  Vec2 fp[2];
  int8_t fcrit;
  uint8_t fstage;
  float ft;
  // The midday's (spine_midday.cpp): a bit it flies (the bee, the fly, the
  // leaf; -1 none), what his hand holds that World does not place (the hat,
  // the boot, the map), and a 0..1 its tick reads (the map in one hand).
  int8_t fbit;
  uint8_t fhold;
  float fk;
};

// Loop start: pick every variant, pause, filler and major from the loop RNG,
// with what memory carries over (the cairn, the mouse hole, the majors' ring
// buffer, which this pushes the loop's majors into).
void spinePlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem);
// Loop start, after the fire pit is placed: camp layout and the props' homes.
void spineLayout(World& w);
// Loop start, after the plan and memoryBeginLoop: what the family needs set
// up before the first tick (the morning: the camp already pitched, the hiker
// asleep in the tent, the fog; the Lookout: its landscape, from `rng` after
// every other draw of the loop start, and its fitted length). Nothing for the
// evening, and no other family draws from `rng` here.
void spineBeginLoop(World& w, Rng& rng);
// Add the build beat, then the spine beats, pauses and fillers in order. The
// loop ends with the close.
void spineBuildTimeline(Timeline& tl, const LoopPlan& p);
// Every tick from worldTick, before the director: the dinner critters (a deer
// edging in a step per bite, a chipmunk behind the fire), the night thief,
// and the bag's damped swing after the bear.
void spineCritterScenes(World& w);
// Ticks the timeline for `p` lasts (the loop's length).
uint32_t spineLoopTicks(const LoopPlan& p);
// "family=evening arrival=1 setup=0 ... fillers=cairn,read majors=echo,fish_leap"
// (none when empty); the morning and the midday list their own beats after
// family=morning / family=midday.
int spineFormatPlan(const LoopPlan& p, char* buf, size_t n);

// Loop length policy: the plan lands every loop in this range (seconds).
#define SS_LOOP_MIN_SECONDS 480
#define SS_LOOP_MAX_SECONDS 660

}  // namespace ss
