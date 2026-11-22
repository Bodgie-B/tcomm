// Long-range shooting at steel on the mountainsides as a surprise major (majors.h id MJ_LONGSHOT, name
// "longshot"; midday only, kOut) (LONGRANGE_PLAN.md, as INTEGRATION_PLAN.md and REVIEW_FIXES.md changed
// it; Mike's words: "long range shooting with target out on the mountain sides and far trees").
//
// It runs on DAY_PLINK with the plinking's rifle (plink.h, day_plink.h): w.day.plink.p for everything in
// his hands, w.day.plink.far (FarState, day.h) for the far side. Far things are glyphs; the two new
// shapes SH_GONG and SH_FLAG (majors.h) are drawn here.
//
// OWNERSHIP: skeleton made by the foundation step (A1). Step B5 (Opus) filled in longshotTargets and the
// shapes (longshotShapeReach, longshotShapeDraw) in day_longshot.cpp, beside its plink.* and day_plink.*
// work (the prone bend, the scope, the shooting bag, plinkFar and the stay: plink.h); step C3 (the
// integrator) fills in the script, the strings, the results and the no-shows. Read REVIEW_FIXES F13 (walk
// speeds and beat seconds) and F14 (heard from the clock) first.
//
// Where the foundation wired it (all stubs until B5 and C3):
//   majors.cpp pool row:            longshotMajorBegin, longshotMajorTick (weight 0 until step C0)
//   majors.cpp scriptOf, majorSeconds, majorEnd:   longshotScript, longshotSeconds, longshotMajorEnd
//   majors.cpp shapeReach, drawShape (SH_GONG, SH_FLAG):   longshotShapeReach, longshotShapeDraw
// Declared for step C0 (stub now): longshotPlanVar. The shared breeze is weatherBreeze (weather.h).
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"
#include "day.h"
#include "majors.h"
#include "script.h"

namespace ss {

struct World;
struct Landscape;

// ---- the far targets (step B5) ----
//
// FarTarget::kind. His standing range on the hills right of the camp pine (LONGRANGE_PLAN 4.1; Mike,
// 2026-11-18: "target out on the mountain sides and far trees"):
//   FT_TREE  a steel plate hung in a tooth of the far tree line (a pale dot)
//   FT_ROCK  a painted rock on the middle ridge, where it shows above the tree line (a pale dot)
//   FT_CONE  the last fallback, when the middle ridge does not show and no second tooth fits: a pale tin
//            hung in a mid pine's top tier, below the tip (a dot; the plan's cone)
//   FT_POST  a plate on a post on the far mountainside (SH_GONG, small)
//   FT_SKY   the gong on the skyline, the highest point of the window (SH_GONG; a dot when that point is a
//            tooth of the tree line)
enum FarKind : uint8_t { FT_TREE = 0, FT_ROCK, FT_CONE, FT_POST, FT_SKY };

// The far targets for this landscape, a pure function of it (no Rng, nothing stored), from the same crest
// samples, teeth and pines the cache draws. Sorted nearest first (by flight, which grows with distance).
// FarTarget::layer is the palette Slot of the layer each sits on (RIDGE_FAR, RIDGE_MID, TREES_FAR,
// TREES_MID), glyph and shown are 0, the swing 0. Returns how many: 3 or 4 (2 only on a landscape no
// generated seed makes; test_longshot counts).
int longshotTargets(const Landscape& ls, FarTarget out[4]);
// The x range the targets are looked for in: right of the camp pine's bounds, left of the frame's edge.
void longshotWindow(const Landscape& ls, float& x0, float& x1);
// The glyph a target is drawn as (kind, shape, slot, pos, scale, base_y; nothing else set): a GL_DOT or
// an SH_GONG at its rest. Pure; C3 copies it into a glyph slot, the stills and the tests draw it.
void longshotTargetGlyph(const FarTarget& t, Glyph& g);

// ---- the shapes (majors.cpp shapeReach and drawShape) ----

// Local radius that holds SH_GONG or SH_FLAG at scale 1, measured from the glyph's pos (the foot).
float longshotShapeReach(uint8_t shape);
// Draws SH_GONG or SH_FLAG from the glyph's fields only.
//   SH_GONG  pos the foot of its two legs; k the plate's swing (rad, a pendulum from the top bar); t > 0 the
//            glint (the plate brighter and bigger). 4 primitives. 7 px tall at scale 1.
//   SH_FLAG  pos the stick's foot; k the breeze 0..1 (weatherBreeze); t the flutter phase in turns; slot the
//            ribbon's colour (MUSHROOM: red). The ribbon always streams toward -x, whichever way the glyph
//            faces (the clouds and the smoke go that way). 4 primitives. 20 px stick.
void longshotShapeDraw(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal);

// ---- the major (majors.cpp) ----

// What one shot does out there (FarState::result): a hit, the string's last hit (the quiet fist), a miss in
// the wind (a puff downwind of it), over the top (nothing to see), low (a puff under it), C's late one (the
// gong rings while he looks away).
enum FarResult : uint8_t { FR_HIT = 0, FR_LAST, FR_WIND, FR_OVER, FR_LOW, FR_LATE };

// The plan's byte from the plan's own draw (0 A, 1 B, 2 C; the begin hook turns B and A by the wind). Pure.
uint8_t longshotPlanVar(uint32_t r_var);
// Seconds the beat lasts for a variant byte (REVIEW_FIXES F13: beats 109, 121, 126).
int longshotSeconds(uint8_t var);
// The hiker's script for a variant byte (the rain and fog no-shows by the bits the begin hook sets).
const Step* longshotScript(uint8_t var, uint8_t& n);
void longshotMajorBegin(World& w);
void longshotMajorTick(World& w, float dt);
void longshotMajorEnd(World& w);

}  // namespace ss
