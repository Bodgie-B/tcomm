// The mushroom patch beside the one mushroom on the near bank (MUSHROOM_PLAN.md sections 3.1, 4 and
// 8.2, as INTEGRATION_PLAN.md 3.1 point 5 changed it; Mike, 2026-11-18).
//
// OWNERSHIP: skeleton made by the foundation step (A1); step B4 (the patch) filled it in and owns it.
// Step C5c's major uses patchLayout, patchShroomDraw, patchShroomBounds, patchBounds and patchSpot.
//
// The layout is NOT stored: it is a pure function of the Landscape (mush_x, mush_y, the stream, the
// rocks) and its stage (Landscape::patch_stage, copied from Memory::mush at loop start by
// worldBeginLoop), with Landscape::patch_lifted saying which pickables the major has taken. Every
// caller lays it out again from the same function, so the cache, the tufts and the major cannot
// disagree. patch_stage may hold just the stage (as world.cpp copies it today) or the whole mush byte:
// everything here reads it through mushStep and mushWilting (mush_patch.h), and only the whole byte
// can show the picked-over patch drooping in its last two loops.
//
// Where the six stand (plan 3.1; frame px): with the mother at x <= 545 the three pickables stand 15
// to 32 px left of it, between her and where he will crouch; with her further right they stop at x 530
// (he never reaches into the status chip's box) and, over a long gap, the straggler carries the patch
// on to her. The two companions stand on her right while both fit there (mother at x <= 588), else on
// her left beyond the pickables. Every one stands
// on dry ground 9 px clear of the water, at y 474 or above, and none is in the chip's capsule.
//
// Callers the foundation wired:
//   landscape.cpp drawLayer, BL_MUSHROOM, after the mother:  patchDrawStatic
//   ambient.cpp pickTufts (a swaying tuft keeps 12 px away):  patchNear
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"

namespace ss {

struct Landscape;

#define MUSH_PATCH 6   // the six mushrooms round the mother: three that stay, three he may pick
#define MUSH_PICK0 2   // m[MUSH_PICK0 + k] is pickable k (k 0..2); Landscape::patch_lifted bit k is its

struct LsShroom {
  int16_t x, y;        // the stem's foot
  uint8_t step;        // size step 0 (not there) .. 4 (full)
  uint8_t lifted;      // 1: the major draws it (picked or in his hand), the cache leaves it out
};
struct LsPatch {
  uint8_t n;           // how many stand (step > 0); m[] keeps each role at its index whatever n is
  uint8_t wilt;        // 1: the picked-over patch's last loops; the three that are left droop
  int16_t nx, ny;      // where he crouches to pick (the plan's Nx, and the pickables' lowest foot + 2)
  LsShroom m[MUSH_PATCH];
};

// The patch as it stands for this landscape and its stage. Pure. Roles by index: 0 and 1 the
// companions, 2 to 4 the pickables, 5 the straggler. A role that cannot stand (a companion past
// x 612, the straggler over a short gap, one whose foot would be on a rock) has step 0.
void patchLayout(const Landscape& ls, LsPatch& out);
// The patch into the static cache (the layer's draw), leaving out the lifted ones; `t` is the build's
// mushroom growth (buildMushroomT): each pops after the mother, the nearest first, with her peek, hold
// and overshoot at its own size. Nothing at all at stage 0.
void patchDrawStatic(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, float t);
// A standing patch mushroom's foot is within r px of (x, y). False at stage 0.
bool patchNear(const Landscape& ls, float x, float y, float r);

// One mushroom, anywhere (the major draws the picked ones in his hand and on the ground with it).
// `scale` 1 is the mother's full size (14 px); `step` 1..4 sets the young shape (a button is a round
// cap on almost no stem; the stem lengthens and the cap opens flatter as it grows); `lean` px moves the
// cap sideways; `tilt` turns the whole thing about its foot (0.25 lies it on its side, + to the right);
// `wilt` droops the cap and browns it; `tone` shades the cap a little. At most 4 primitives.
struct ShroomLook {
  float x, y;          // the stem's foot
  float scale;         // of the mother's full size
  uint8_t step;        // 1..4
  uint8_t wilt;        // 0 or 1
  float lean;          // px at the cap
  float tilt;          // turns about the foot
  float tone;          // -1..1: a deeper (-) or paler (+) cap, so a clump is not six of one colour
};
void patchShroomDraw(const ShroomLook& m, Surface& s, const Palette& pal, const Rect& clip);
// Its bounds, from the same geometry the draw uses, with the antialiasing fringe.
Rect patchShroomBounds(const ShroomLook& m);
// Patch mushroom i as the cache draws it (grown: build t 1).
ShroomLook patchLook(const LsPatch& p, int i);
// The union of the standing ones' bounds (empty at stage 0). `with_lifted`: count the lifted ones too
// (the rows to repaint when the major gives them back to the cache).
Rect patchBounds(const Landscape& ls, bool with_lifted = true);
// The middle of the patch (the mother when nothing stands): where a fawn across the water would look.
Vec2 patchSpot(const Landscape& ls);

}  // namespace ss
