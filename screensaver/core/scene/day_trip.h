// Mushrooms after rain as a surprise major (majors.h id MJ_MUSHROOM, name "mushroom"; midday only,
// slots 2 | 4, weight 0: forced when the patch is ripe) (MUSHROOM_PLAN.md sections 5 and 9, as
// INTEGRATION_PLAN.md changed them; Mike, 2026-11-18).
//
// OWNERSHIP: skeleton made by the foundation step (A1). Step C5c (the integrator) fills in
// day_trip.cpp, modelled on day_plink.cpp: the script, the sky schedule, the visions' stage machines,
// the dog, reedsDropChew at doLift. Read REVIEW_FIXES F2 to F5 and F27 first.
//
// Where the foundation wired it (all stubs until C5c):
//   majors.cpp pool row:            tripMajorBegin, tripMajorTick (weight 0 always)
//   majors.cpp scriptOf, majorSeconds, majorEnd:   tripScript, tripSeconds, tripMajorEnd
//   react.cpp:                      reactWaveOk refuses and reactStart's hands_full is set while
//                                   tripHandsFull
//   day.cpp, case DAY_TRIP:         the tripDay* hooks below
// Declared for step C0 (stub now): tripPlanVar.
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"
#include "day.h"
#include "script.h"

namespace ss {

struct World;
struct Renderer;

// ---- the major (majors.cpp) ----

// The plan's byte from the plan's own draw (MUSHROOM_PLAN 9). Pure. STUB: 0.
uint8_t tripPlanVar(uint32_t r_var);
// Seconds the beat lasts for a variant byte. STUB: 201 (196 + the wade's 5 s, REVIEW_FIXES F5).
int tripSeconds(uint8_t var);
// The hiker's script for a variant byte. STUB: a short stillness.
const Step* tripScript(uint8_t var, uint8_t& n);
void tripMajorBegin(World& w);
void tripMajorTick(World& w, float dt);
void tripMajorEnd(World& w);

// Mushrooms or a bunch of them in his hands, or the frog: no passer-by wave, the doorbell a hop and a look.
bool tripHandsFull(const World& w);

// ---- the frog he licks (majors.h MJ_FROG_LICK, name "frog_lick"; 2026-11-25) ----
//
// Mike: "the hiker goes over to the frog, picks it up, licks it, then sets the frog down, shakes his head
// due to the terrible taste, then goes to lay down. in a few seconds the psychedelic sky routine starts
// borrowed from the mushroom episode and runs." A frog (a director critter, CT_MAJOR, brave) hops in along
// the camp side from the right; he walks over, crouches, picks it up, holds it out and looks at it, licks it
// twice, sets it down (it startles and hops off out of frame) and shakes his head hard, spits and wipes his
// mouth; he lies down at the mushroom's spot, the grass leans, and the sky lets go exactly as in the
// mushroom's play (DAY_TRIP with TripState::frog set: no mushrooms, no visions; Lucy may come and lie by
// him as there). Midday after lunch or the wade, weight 1.0, by day and dry, never with a ripe patch.
// No-show (dayNoShowScript) when the begin hook finds rain or no free critter slot.
uint8_t frogPlanVar(uint32_t r_var);   // 0 (one variant); takes no draw
// Memory::taichi_next bit 3 (the carry word's 0x0800): the first midday loop that can play the frog (by day, the
// data dry, no ripe patch, SS_S22 on) plays it, as its first major (second after a dog's fetch); frogMajorBegin
// clears it. Zero in every ordinary run, so no plan moves. For Mike to see it on the panel: `ss carry 2048`.
constexpr uint8_t FROG_ASK = 8;
int frogSeconds(uint8_t var);
const Step* frogScript(uint8_t var, uint8_t& n);
void frogMajorBegin(World& w);
// (Its tick and end are tripMajorTick and tripMajorEnd.)

// ---- the sky's clock and its band repaint (step C5b) ----

// Every tick from dayTrack, whatever the day kind. While the trip's sky runs: the band that is due is
// latched with the sky clock (Landscape::trip) and its 32 rows of the cache are repainted and marked. A
// band waits a tick when the renderer already holds repaints it could only join by making one taller than
// 64 rows (a sun or moon move in the same tick, or ticks running ahead of renders: REVIEW_FIXES F6, F28).
// With no trip running, any band still away from natural goes back two a tick (F2).
void tripSkyTrack(World& w, Renderer& r);
// A test and stills hook: run the sky with no story (no major, nobody moved). `slow`: one band every
// second tick, as under a tall vision (F3). It ends by itself after its 104 s and clears the day union.
void tripSkyOnly(World& w, bool slow);
// The sky clock in seconds, or -1 when no trip is on.
float tripSkyClock(const World& w);

// ---- the day hooks (day.cpp, case DAY_TRIP) ----

Rect tripDayBounds(const DayTrip& d);
void tripDayPose(World& w);
bool tripDayHoldsFacing(const World& w);
void tripDayStep(World& w, bool frozen);
// The sky's band repaint is step C5b's (here, through the Renderer: repaintRows and markDirtyRows;
// REVIEW_FIXES F2, F6, F28).
void tripDayTrack(const DayTrip& prev, const DayTrip& cur, const World& w, Renderer& r);
bool tripDayPartY(const World& w, int part, int16_t& y);
void tripDayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
