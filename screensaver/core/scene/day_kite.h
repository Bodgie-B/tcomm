// The kite as a surprise major (majors.h, id MJ_KITE, name "kite"): midday only, by day, and
// never in rain, snow or storm. MORNING_DAYTIME_PLAN.md section 2. The kite itself is the
// standalone module in kite.h; this file is the scene around it, in its own file like fetch.h:
// the hiker's script, a begin and a per-tick function for the major table, and the World hooks
// that step the kite after him and put the line in his hand.
//
// Variants (the plan's major_var byte, see kitePlanVar):
//   0  A, serene: he walks backward, the kite lifts at the first try and climbs; he stands and
//      watches it for a long calm stretch (one puff of wind), then reels it in and folds it
//   1  B, the kite wins: a gust yanks it (the scripted gust, or the weather's own if it is
//      windier); he leans back, digs his heels in, skids, then is pulled off the right edge at a
//      run. A beat of empty scene. He walks back in from the right without it, the line trailing,
//      shrugs and gathers the line in
//   2  C, it won't fly: three launches (a nosedive, a spin and crash, a lift that drops), then he
//      sits with it in his lap and a breeze lifts it out of his hands; he scrambles up after the
//      line, flies it a little and reels it in
// The kite is on the +x side of his hand (the module takes either side; the plan says the right
// edge). The scene's wind blows toward -x, so a kite on his right is flown upwind of him: UNVERIFIED
// whether Mike minds, and mirrored (side -1, off the left edge) it would be a one-constant change.
//
// The weather. The wind the kite feels is kiteWindFrom(weather.wind, weather.gust), at least the
// scripted puff or gust (MajorState::u0). The plan keeps the kite out of a rain, snow or storm the
// OUTSIDE data says is there (majorsPlan, like rain_shelter); a random weather is drawn after the
// plan, so a kite planned before a random rain, snow or storm gets KITE_NOSHOW at the beat's begin
// and he only looks at the sky, holds out a hand for the first drops and puts it away.
//
// The doorbell. Every per-tick function here is called from the beat or from worldTick's running
// branch, so the reaction freezes the kite, the scripted wind and the beat's clocks with the story.
// During it only kiteWorldStep(w, true) runs, with dt 0: nothing advances, the line stays in his
// waving hand, and a kite held in his hand stays in it.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has
// seen it (and on the panel).
#pragma once

#include "script.h"

namespace ss {

struct World;

constexpr uint8_t KITE_VARIANTS = 3;
// Set by kiteMajorBegin on the byte it was given: a rain, snow or storm the plan could not know of.
constexpr uint8_t KITE_NOSHOW = 8;

// The plan's byte from the plan's own random draw `r` (already taken). Pure.
uint8_t kitePlanVar(uint32_t r);
// Seconds the beat lasts for a variant byte.
int kiteSeconds(uint8_t var);
// The hiker's script for a variant byte.
const Step* kiteScript(uint8_t var, uint8_t& n);
// Beat begin, tick and end (majors.cpp: majorBegin, majorUpdate, majorEnd).
void kiteMajorBegin(World& w);
void kiteMajorTick(World& w, float dt);
void kiteMajorEnd(World& w);

// From worldTick, after the hiker: the line leaves the hand he has now. frozen (the doorbell
// reaction): the kite stands still, the line follows his hand.
void kiteWorldStep(World& w, bool frozen);

}  // namespace ss
