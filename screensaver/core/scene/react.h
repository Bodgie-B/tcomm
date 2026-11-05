// The hiker's doorbell reaction (brief section 11, "Radio events while the
// screensaver runs"): he jumps, turns toward the status chip's side (bottom
// right), waves, points at the chip and goes back to what he was doing. With
// his hands full he only hops and looks. PROPOSED choreography: the brief
// leaves the details to Mike's review at stage 8.
//
// While it runs the story holds still: no beat, script, critter, minor or
// director step advances (app.cpp, world.cpp), so everything resumes exactly
// where it stopped and the loop's timing is unchanged. The whole camp freezes
// to listen, the animals included, while the ambient layer (clouds, stream,
// fire, weather, the tent's breathing shadow) keeps moving. His facing and
// pose offsets go back exactly as they were.
//
// With the bow up in the archery major he only looks round. Not in a Lookout
// loop, and not while he is out of frame or in the tent: then the chime and the
// chip's pulse answer the bell on their own.
#pragma once

#include "world.h"

namespace ss {

constexpr float kReactSeconds = 3.4f;   // PROPOSED
constexpr int kReactTicks = 85;         // kReactSeconds at SS_TICK_HZ, counted in whole ticks

// He can react now: in the scene and in frame, not a Lookout loop, not faded out.
bool reactCanStart(const World& w);
void reactStart(World& w);
// One tick while active: sets his pose offsets and facing for the reaction;
// after kReactTicks puts both back and ends it.
void reactStep(World& w);
// The wave and the point, blended over his composed pose after the weather's
// overlay (stepHiker), so a cold-weather hug can't hide it. Nothing when inactive.
void reactPose(const World& w, HikerPose& p);

}  // namespace ss
