// What the spine families share, and each family's entry points (internal to
// core/scene). spine.cpp holds the evening, the shared interpreter and fillers,
// and the dispatch in spine.h's functions; spine_morning.cpp the morning.
// See SPINES_WEATHER_LOOKOUT_SPEC.md section 1.
#pragma once

#include "../anim/scheduler.h"
#include "majors.h"
#include "spine.h"

namespace ss {

// ---- shared (spine.cpp) -------------------------------------------------------------

// Fillers each family may draw (bit = SpineFamily).
bool spineFillerAllowed(int family, int filler);
// The beat that plays filler `f` (its shared script, or birds_on_water's own hooks).
Beat spineFillerBeat(const LoopPlan& p, int f);

// The loop length policy (480 to 660 s), one copy for every family. A family
// passes its busy seconds (everything but the pauses), how many of its beats
// have a pause after them (pause_s[0 .. beat_pauses-1]; the filler pauses
// follow), and how it lays its fillers out after a filler is dropped.
struct FitSpec {
  int (*busy)(const LoopPlan& p);
  int beat_pauses;
  void (*filler_slots)(LoopPlan& p);
};
int spinePauseSeconds(const LoopPlan& p, int beat_pauses);
// Squeeze the pauses (round robin, a second at a time, down to 8 s); while
// still too long drop fillers down to one, majors down to two, the last
// filler, then shorten the still beats round the majors; then stretch the
// pauses (up to 20 s) until the loop is long enough. Then push the loop's
// majors into the memory's ring and recent sets.
void spineFitLength(LoopPlan& p, Memory& mem, const FitSpec& f);

// ---- the morning (spine_morning.cpp) -----------------------------------------------

enum MorningBeatId : uint8_t {
  MB_WAKE = 0, MB_COFFEE, MB_BAG_DOWN, MB_BREAK_CAMP, MB_DOUSE, MB_LEAVE, MB_COUNT
};

void morningPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem);
// After the plan and memoryBeginLoop: the morning's places, the camp as he
// left it last night (pitched, fire out, bag hung, pack by the tent, the
// cairn), him asleep in the tent, the fog.
void morningLayout(World& w);
void morningBuildTimeline(Timeline& tl, const LoopPlan& p);
uint32_t morningLoopTicks(const LoopPlan& p);
int morningFormatPlan(const LoopPlan& p, char* buf, size_t n);
const char* morningBeatName(int beat);
int morningVariantCount(int beat);
// Every tick (from spineCritterScenes): the fog lift and the morning's critters.
void morningTick(World& w);

// ---- the morning where the camp stays up (morning_stay.cpp) ---------------------------------
// The other morning ("the camp stays up", MORNING_DAYTIME_PLAN.md section 4), picked per loop by
// morningPlan (60 %, PROPOSED): LoopPlan::stay is 1. His wake and coffee, then the morning pee and
// the raid, breakfast with the pancake flip, the wash at the stream, the tidy with the stuff sack,
// and off on a day hike with the camp left standing. The morning's public functions above hand over
// to these when the plan's `stay` is set.

enum StayBeatId : uint8_t {
  SY_WAKE = 0, SY_COFFEE, SY_PEE, SY_BREAKFAST, SY_WASH, SY_TIDY, SY_DAYHIKE, SY_COUNT
};

// The plan, drawn from `rng` (the stay's own stream: morningPlan keeps the loop rng's draws as
// Mark's morning made them).
void stayPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem);
// After morningLayout: the shoes at the tent door, the places the stay's beats use.
void stayLayout(World& w);
void stayBuildTimeline(Timeline& tl, const LoopPlan& p);
uint32_t stayLoopTicks(const LoopPlan& p);
int stayFormatPlan(const LoopPlan& p, char* buf, size_t n);
const char* stayBeatName(int beat);
int stayVariantCount(int beat);
// Every tick, from morningTick: the squirrel and the raccoon at the pine, the bird at the pan, the
// animals back in the empty camp, and what his hand holds that World does not place.
void stayTick(World& w);

// What the stay takes from Mark's morning (spine_morning.cpp): his build beat, his wake (3
// variants) and coffee (4: d, the boil-over, is the stay's alone) as scripts, seconds and begin and
// end hooks, and a few of his actions.
const Beat& morningBuildBeat();
const Step* morningSharedScript(int beat, int variant, uint8_t& n);
int morningSharedSeconds(int beat, int variant);
void morningSharedBegin(SceneCtx& ctx, int beat);
void morningSharedEnd(SceneCtx& ctx);
void morningActRummage(World& w, float u);
void morningActPotOff(World& w, float u);
void morningActKick(World& w, float u);
void morningActLastPuff(World& w, float u);
void morningActPackLift(World& w, float u);
void morningActPackOn(World& w, float u);
void morningFireOut(World& w);

// ---- midday (spine_midday.cpp) --------------------------------------------------------

enum MiddayBeatId : uint8_t {
  MD_RETURN = 0, MD_LUNCH, MD_WADE, MD_NAP, MD_MAP, MD_AFTERNOON, MD_COUNT
};

void middayPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem);
// After the plan and memoryBeginLoop: the camp as the morning left it
// (pitched, fire out, bag hung, pack by the tent, the wood pile, the cairn),
// popped in by the build; him away on a day hike.
void middayLayout(World& w);
void middayBuildTimeline(Timeline& tl, const LoopPlan& p);
uint32_t middayLoopTicks(const LoopPlan& p);
int middayFormatPlan(const LoopPlan& p, char* buf, size_t n);
const char* middayBeatName(int beat);
int middayVariantCount(int beat);
// Every tick (from spineCritterScenes): the midday's critter, bit and glyph,
// and what his hand holds that World does not place.
void middayTick(World& w);

// ---- the Lookout (lookout.cpp) ---------------------------------------------------------

enum LookoutBeatId : uint8_t {
  LB_APPROACH = 0, LB_TRAIL_PAUSE, LB_REVEAL, LB_CLIMB, LB_EXPLORE, LB_SETTLE, LB_SUNSET, LB_FADE, LB_COUNT
};

// The plan's own draws (the sunset's steps, the birds); the beats' seconds
// are fitted in lookoutBeginLoop, once the landscape (and so the trail) is known.
void lookoutPlan(LoopPlan& p, Rng& rng, const Inputs& in);
// After the plan and memoryBeginLoop: the Lookout's landscape and clouds from
// `rng` (after every other draw of the loop start), its places, the birds'
// schedule, the beats' fitted seconds; the camp's things off.
void lookoutBeginLoop(World& w, Rng& rng);
void lookoutBuildTimeline(Timeline& tl, const LoopPlan& p);
uint32_t lookoutLoopTicks(const LoopPlan& p);
int lookoutFormatPlan(const LoopPlan& p, char* buf, size_t n);
const char* lookoutBeatName(int beat);

}  // namespace ss
