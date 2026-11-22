// Morning Tai Chi as a surprise major (majors.h id MJ_TAICHI, name "taichi"; morning only, slot bit 1)
// and the World side of the module in taichi.h (TAICHI_PLAN.md sections 5, 6, 9 and 10.2, as
// INTEGRATION_PLAN.md changed them; Mike, 2026-11-18).
//
// OWNERSHIP: skeleton made by the foundation step (A1); filled in by step C2 (the integrator):
// the script, the acts, the guests, the gags, reedsDropChew at begin, the heron beside RS_NEAR, the
// grazing deer, Lucy's watch spot, the fog lift waiting for a hold (REVIEW_FIXES F8). F27: a no-show
// keeps the beat's planned seconds (he sits out the rain by the cold fire).
//
// Where it is wired:
//   majors.cpp pool row:            taichiMajorBegin, taichiMajorTick (planned once kTaichiLanded, SS_S22 on)
//   spine_morning.cpp morningTick:  taichiFogWaits (a fog step waits for a hold of 2 s or more)
//   housedogs.cpp HA_SETTLE:        taichiWatchSpot (Lucy lies down to watch)
//   majors.cpp scriptOf, majorSeconds, majorEnd:   taichiScript, taichiSeconds, taichiMajorEnd
//   react.cpp reactStart:           look_only also when taichiInForm; hands_full also when taichiHatInHand
//   day.cpp, case DAY_TAICHI:       the taichiDay* hooks below
// Declared for the later serial steps (stubs now): taichiPlanVar (C0, majorsPlan), taichiWatchSpot
// (C2, housedogs.cpp Lucy's settle spot).
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"
#include "day.h"
#include "script.h"

namespace ss {

struct World;
struct Renderer;
struct Memory;

// ---- the major (majors.cpp) ----

// The plan's byte from the plan's own draw r_var and the memory's taichi_next (INTEGRATION 3.1 point 7).
// Pure. Bits 0-1 the chapter (0..3), bit 3 DAY_NOSHOW (set by the begin hook), bits 4-5 the gag (taichi.h
// TaiGag: none, wobble, itch, Copper's bump), bit 6 the guests may come.
uint8_t taichiPlanVar(uint32_t r_var, const Memory& mem, int dog_out);
// Seconds the beat lasts for a variant byte: by chapter, plus the gag's 8, 6 or 12.
int taichiSeconds(uint8_t var);
// The hiker's script for a variant byte (one script for all chapters; the no-show's when the bit is set).
const Step* taichiScript(uint8_t var, uint8_t& n);
void taichiMajorBegin(World& w);
void taichiMajorTick(World& w, float dt);
void taichiMajorEnd(World& w);

// ---- questions the rest of the scene asks ----

bool taichiInForm(const World& w);                  // he is in the form: the doorbell only makes him look
bool taichiHatInHand(const World& w);               // the hat is off and in his hand
bool taichiWatchSpot(const World& w, Vec2& out);    // where Lucy lies to watch; false: her usual spot
// REVIEW_FIXES F8: he is in the form and not inside a hold of 2 s or more, so a step of the morning's fog
// lift (a whole-cache palette rebuild) waits: stillness hides the stutter.
bool taichiFogWaits(const World& w);

// ---- the day hooks (day.cpp, case DAY_TAICHI) ----

Rect taichiDayBounds(const DayTaichi& d);
void taichiDayPose(World& w);                       // after hikerTick: the module writes his whole pose
bool taichiDayHoldsFacing(const World& w);
void taichiDayStep(World& w, bool frozen);          // the butterfly (ambient, also while frozen)
void taichiDayTrack(const DayTaichi& prev, const DayTaichi& cur, const World& w, Renderer& r);
bool taichiDayPartY(const World& w, int part, int16_t& y);
void taichiDayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
