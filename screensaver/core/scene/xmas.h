// The Christmas set (stage 18): MOCKUP DRAWINGS ONLY, for Mike to pick from. PROPOSED, nothing scheduled.
//
// What Mike asked for: the camp pine (the central tree with the food-bag branch) gets lights put on by
// the hiker, garland flown in by birds, an ornament stolen by a raccoon, all powered by a little
// water wheel generator on the camp-side bank by the boulder (paddles turn it), with sparks now and
// then. The offered idea is an advent build-up over December: the generator first, then the lights a
// spiral at a time, the garland, the ornaments and the raccoon, the star on Christmas Eve, presents on
// the day.
//
// This module only DRAWS those pieces, posed from a mock state (XmasMock) that the stills tool
// (tools/xmas_stills.cpp) and the tests set by hand. There are no story scripts, no schedule, no date
// input and no setting: with the mock all zero (every loop: worldBeginLoop zeroes the World) nothing
// here draws, marks a strip or adds an actor, and every frame is what it was (tests/test_xmas.cpp
// proves it against frame hashes taken before this module existed).
//
// Memory: XmasState shares the Lookout's running state (world.h, a union): a camp loop never touches
// LookoutState and a Lookout loop never draws or tracks this, so it costs no App bytes.
//
// Pieces and where they draw (world.cpp):
// - The generator (XmasMock::gen): a hand-built wheel of lashed sticks with board paddles, standing in
//   the current at the camp-side bank beside the boulder (landscape.h LS_BOULDER_*), seen a little from
//   downstream so its axle runs back up the bank into a small wooden box; a wire from the box along the
//   ground to the foot of the camp pine and up the trunk. Splash where the paddles meet the water; a
//   spark burst (`spark` 1..8, the blaze sparks' look, ambient.cpp) off the box. Two ground actors
//   (ACT_XMAS 0 the box, 1 the wheel at its waterline, so the boulder draws over its foot).
// - On the tree, drawn on the tree's layer before every ground actor (the cached pine is behind
//   everything anyway): the light strings, one wrap per tier across its lower skirt, bulbs bunched
//   toward the edges as on a cone; the garland of gold tinsel in scallops above them; the ornaments
//   (red and gold balls on hooks under the tier tips); the star; the soft light on the ground under the
//   tree at dusk and night; the ladder leaning on the right; the coil of unstrung lights at its foot and
//   the loose run up to his hand.
// - Ground actors (ACT_XMAS 2..5): the presents under the tree (two bone-shaped ones for the dogs:
//   Lucy's big one in black with a white ribbon, Copper's small one in his golden copper), the stocking
//   on the tent's front pole, the raccoon (critters.h kToneMasked) with its ornament, the raccoon's
//   bush by the stream, the two cardinals (kToneCardinal) with the garland.
// - In the sky (after the distant bird): the sleigh and reindeer across the moon.
//
// Dirty strips: four groups (the tree's pieces, the generator, the critters and garland in flight, the
// extras), each marked where it was and where it is whenever the mock changes; the critters' group
// every tick while the run of lights hangs from his hand (he breathes).
#pragma once

#include "xmas_switch.h"

#if SS_XMAS

#include "../gfx/surface.h"
#include "../palette.h"
#include "../render/renderer.h"
#include "landscape.h"

namespace ss {

struct World;

#define XMAS_WRAPS 5        // light strings: one round each tier, top down
#define XMAS_ORNS 7         // ornament places on the tree (bit i of XmasMock::orns)
#define XMAS_LOW_ORN 6      // the low one at the bottom tier's left tip: the raccoon's
#define XMAS_ACTORS 7       // ACT_XMAS idx: 0 box, 1 wheel, 2 presents, 3 the tent's (stocking, snow),
                            // 4 critters, 5 bush, 6 the boulder's snow cap
#define XMAS_LOOK XL_MULTI  // the lights' look: Mike picked multi-colour; XL_WARM is the warm white

enum XmasLook : uint8_t { XL_WARM = 0, XL_MULTI };
enum XmasPower : uint8_t { XP_OFF = 0, XP_ON, XP_HALF_DEAD, XP_DIM };
enum XmasGarland : uint8_t { XG_NONE = 0, XG_FLYING, XG_TANGLED, XG_PART, XG_DRAPED };
enum XmasCoon : uint8_t { XR_NONE = 0, XR_REACH, XR_CARRY, XR_PROUD };
enum XmasExtra : uint8_t {
  XE_STAR = 1, XE_PRESENTS = 2, XE_STOCKING = 4, XE_SLEIGH = 8,
  XE_LADDER = 16,   // the ladder leaning on the tree
  XE_COIL = 32,     // the unstrung lights in a coil at the ladder's foot, a run up to his far hand
  XE_GAP = 64,      // the empty hook where the low ornament was
  XE_SNOW = 128     // the snow look: snow on the ground and the boughs, caps on the rocks, the box, the tent
};

// The posed state (stills and tests only). All zero: nothing.
struct XmasMock {
  uint8_t gen;       // 1: the wheel, the box and the wire are there
  uint8_t wheel;     // the wheel's turn, 64ths
  uint8_t spark;     // 0 none; 1..8 the step of a spark burst off the box (0.06 s each)
  uint8_t wraps;     // light strings strung, 0..XMAS_WRAPS, from the top
  uint8_t look;      // XmasLook
  uint8_t power;     // XmasPower (the strings strung are lit unless XP_OFF)
  uint8_t garland;   // XmasGarland
  uint8_t fly;       // XG_FLYING: how far along the flight in, 0..255
  uint8_t orns;      // bit i: ornament i hangs on the tree
  uint8_t coon;      // XmasCoon
  uint8_t bush;      // the raccoon's bush: how many stolen ornaments on it (0 no bush)
  uint8_t extras;    // XmasExtra bits
  uint8_t north;     // the north star: 0 none; 1..4 its twinkle step (1 the brightest, 3 the dimmest)
};

struct XmasState {
  XmasMock m;        // what is posed now
  XmasMock d;        // what was drawn last tick
  Rect d_tree, d_gen, d_crit, d_extra;
};

// True when anything is posed.
bool xmasOn(const World& w);
// Ground actors: idx 0..XMAS_ACTORS-1; false when that piece is not shown.
bool xmasActorY(const World& w, int idx, int16_t& y);
void xmasDrawActor(const World& w, int idx, Surface& s, const Rect& clip, const Palette& pal);
// The tree's layer (world.cpp, after the fog, before the ground actors).
void xmasDrawTree(const World& w, Surface& s, const Rect& clip, const Palette& pal);
// The sky (world.cpp, after the distant bird): the sleigh across the moon. The mock puts it on the
// old sky's night moon (landscape.cpp kBody: x 640, y 110 at mood 0); a real build would follow
// SkyDraw's moon.
void xmasDrawSky(const World& w, Surface& s, const Rect& clip, const Palette& pal);
// The snow look's static part (world.cpp worldDrawStatic, into the cache or a band of its rows): the
// ground recoloured to snow (fire and ice's frost pass, whiter), drifts at the rocks and the camp pine's
// foot, snow on the boughs of the camp pine, the mid and far pines and the foreground pines, caps on the
// rocks. Nothing unless XE_SNOW is posed.
void xmasDrawStatic(const World& w, Surface& frame, const Palette& pal);
// Foreground pine k's snow again over an actor that passed behind it (world.cpp ACT_FGPINE).
void xmasDrawFgPineSnow(const World& w, int k, Surface& s, const Rect& clip, const Palette& pal);
// The palette the swaying tufts are drawn with: `pal` itself, or in the snow a copy in `tmp` with the
// grass frosted (as the snow pass frosts the cached tufts).
const Palette& xmasTuftPal(const World& w, const Palette& pal, Palette& tmp);
// Where the north star sits (the middle of its sparkle).
Vec2 xmasNorthStar();
// Marks what changed (end of worldTick, camp loops only).
void xmasTrack(World& w, Renderer& r);

// Bounds of each group (frame space, empty when nothing in it is shown). For tests.
Rect xmasTreeBounds(const World& w);
Rect xmasGenBounds(const World& w);
Rect xmasCritBounds(const World& w);
Rect xmasExtraBounds(const World& w);

// Geometry for the stills tool and the notes: where the wheel's hub is, the bank's waterline there,
// the ladder's rung k (0 the lowest) where he stands, the low ornament's ball.
Vec2 xmasWheelHub(const Landscape& ls);
float xmasBankY(const Landscape& ls, float x);
Vec2 xmasLadderRung(const Landscape& ls, int k);
Vec2 xmasOrnament(const Landscape& ls, int i);
Vec2 xmasBushAt(const Landscape& ls);

}  // namespace ss

#endif  // SS_XMAS
