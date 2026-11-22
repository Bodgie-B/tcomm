// Mushrooms after rain: the patch's growth rule on one packed byte (MUSHROOM_PLAN.md section 8.1, as
// INTEGRATION_PLAN.md 3.1 points 3 and 6 changed it; Mike, 2026-11-18).
//
// OWNERSHIP: skeleton made by the foundation step (A1). Step B4 (the mushroom patch builder) filled in
// the rule (mushAfterLoop, mushAfterMinutes, mushWillPlay, the size table) and owns this file. The
// packing accessors are the layout the foundation fixed: Memory::mush, the low byte of Inputs::carry and
// the host's carry word all hold this byte, so do not change the bit layout without the integrator.
//
// Header only and plain C++, so the panel's host (firmware, step C8) and the PC tests share the same
// rule, like set_piece_day.h.
//
// The byte: bits 0-2 the stage (0 none, 1..6 growth steps, 6 = MUSH_RIPE, 7 = MUSH_PICKED), bits 3-5
// how many loops a picked-over patch still stands (6 down to 1), bit 6 "it has rained since the patch
// began" (it may grow). Bit 7 is zero.
//
// What the viewer sees over a day (plan section 4): rain at the house (the forecast's rain or storm,
// never a random one, so the pinned plans stay as they are) puts up the first two buttons at the end of
// the first wet loop; each dry camp loop after that is one step; six steps and the patch is ripe and
// waits there, in every family, until a dry midday loop by day plays the mushroom major. Once he has
// picked, the three that are left stand MUSH_PICKED_LOOPS more loops, drooping in the last two, and the
// bank is bare until the next rain.
//
// Live or not: memory.cpp calls mushAfterLoop only once its kMushLive is true (step C5a). Until then
// the byte is carried unchanged, so the patch never appears and every frame is what it was.
#pragma once

#include <stdint.h>

#include "scene/stage22_switch.h"

namespace ss {

constexpr uint8_t MUSH_RIPE = 6;
constexpr uint8_t MUSH_PICKED = 7;
// Loops the picked-over patch still stands (PROPOSED, plan 4 point 5), and the last loops of those in
// which the three that remain droop (plan 8.1).
constexpr uint8_t MUSH_PICKED_LOOPS = 6;
constexpr uint8_t MUSH_WILT_LOOPS = 2;
// The host's growth while the screensaver is off: one step per this many dry minutes (PROPOSED). Six
// steps is an hour, about what six loops take on the screen, so a patch ripens at the same pace
// whether the panel shows it or not.
constexpr uint32_t MUSH_MINUTES_PER_STEP = 10;

inline uint8_t mushStage(uint8_t b) { return (uint8_t)(b & 7u); }
inline uint8_t mushLeft(uint8_t b) { return (uint8_t)((b >> 3) & 7u); }
inline bool mushSoak(uint8_t b) { return ((b >> 6) & 1u) != 0; }
inline uint8_t mushPack(uint8_t stage, uint8_t left, bool soak) {
  return (uint8_t)((stage & 7u) | ((left & 7u) << 3) | (soak ? 0x40u : 0u));
}

// One camp loop has ended. `wet`: the loop's weather came from the OUTSIDE data and was rain or a
// storm. Returns the new byte.
//   wet:  the ground is soaked; a bare bank (or a picked-over patch) gets new buttons, stage 1. A
//         growing or ripe patch is left as it is: nothing comes up while it rains.
//   dry:  a growing, soaked patch takes one step; a ripe one waits to be picked; a picked-over one
//         counts its loops down and is gone at 0, soak and all.
inline uint8_t mushAfterLoop(uint8_t b, bool wet) {
  const uint8_t stage = mushStage(b);
  const uint8_t left = mushLeft(b);
  const bool soak = mushSoak(b);
  if (wet) {
    if (stage == 0 || stage == MUSH_PICKED) return mushPack(1, 0, true);
    return mushPack(stage, left, true);
  }
  if (stage >= 1 && stage < MUSH_RIPE) return soak ? mushPack((uint8_t)(stage + 1), 0, soak) : b;
  if (stage == MUSH_PICKED) {
    if (left <= 1) return 0;
    return mushPack(MUSH_PICKED, (uint8_t)(left - 1), soak);
  }
  return b;   // bare, or ripe and waiting
}

// The major's first pick (step C5c): the patch is picked over and the rest stand their loops.
inline uint8_t mushAfterPick(uint8_t b) { return mushPack(MUSH_PICKED, MUSH_PICKED_LOOPS, mushSoak(b)); }

// The host's step while no run is on (step C8). `dry_minutes`: whole minutes since the host last
// stepped the byte, all of them dry; the host keeps the remainder (dry_minutes % MUSH_MINUTES_PER_STEP)
// for next time. `raining`: the forecast says rain or a storm now, which counts as one wet loop and
// nothing else. So mushAfterMinutes(b, 10 * k, false) is exactly k dry loops.
inline uint8_t mushAfterMinutes(uint8_t b, uint32_t dry_minutes, bool raining) {
  if (raining) return mushAfterLoop(b, true);
  uint32_t steps = dry_minutes / MUSH_MINUTES_PER_STEP;
  // Seven dry steps take any byte to where more steps change nothing (ripe, or bare after picking),
  // so a host back from a long night does not loop for hours.
  if (steps > 8u) steps = 8u;
  for (uint32_t i = 0; i < steps; ++i) b = mushAfterLoop(b, false);
  return b;
}

// The size step of patch mushroom `i` (patch.h's role index 0..5) for byte `b`: 0 not there, 1 a
// button .. 4 full grown. Growing: stage minus the role's first stage, so the two nearest the mother
// come up first and the straggler and the farthest pickable last. Picked over: the companions and the
// straggler stay full grown (they droop in the last MUSH_WILT_LOOPS, mushWilting) and the pickables
// are gone.
inline uint8_t mushStep(uint8_t b, int i) {
  static const uint8_t kFirst[6] = {0, 1, 0, 1, 2, 2};
  if (i < 0 || i >= 6) return 0;
  const uint8_t stage = mushStage(b);
  if (stage == 0) return 0;
  if (stage == MUSH_PICKED) return (i == 0 || i == 1 || i == 5) ? 4 : 0;
  const int s = (int)stage - (int)kFirst[i];
  return (uint8_t)(s < 0 ? 0 : (s > 4 ? 4 : s));
}

// The three that are left are wilting: the last MUSH_WILT_LOOPS loops of a picked-over patch.
inline bool mushWilting(uint8_t b) {
  return mushStage(b) == MUSH_PICKED && mushLeft(b) >= 1 && mushLeft(b) <= MUSH_WILT_LOOPS;
}

// The plan's question (INTEGRATION_PLAN 3.1 point 9): will this loop play the mushroom major? A ripe
// patch, a midday loop, not night, and the data's weather not rain, snow or storm. Plain values so the
// header stays free of the scene's types; majorsPlan (step C0) passes mem.mush, its family test, its
// night flag and its data weather test. Always false with SS_S22 off, so the pins cannot move.
inline bool mushWillPlay(uint8_t mush, bool midday, bool night, bool data_wet) {
  return SS_S22 != 0 && mushStage(mush) == MUSH_RIPE && midday && !night && !data_wet;
}

}  // namespace ss
