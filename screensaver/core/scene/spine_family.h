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

}  // namespace ss
