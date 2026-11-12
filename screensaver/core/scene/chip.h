// Status chip: the other floor's status, kept on screen in the bottom right
// corner while the screensaver runs (brief section 11, "Status chip").
//
// A capsule filled with the status color, the status word in the panel's UI
// font, a thin outline in the scene palette (CHIP_OUTLINE). When a dog is out,
// a round badge with a dog silhouette sits just left of the chip, one badge
// per dog. On the upstairs panel the tenant's heat and cooling requests sit
// left of the badges as round arrow badges, drawn as the home screen's
// UPSTAIRS tile draws them (firmware art.cpp request_arrow: mild light red or
// light blue, urgent red or blue with a doubled head, flashing lit and dim
// half a second each), newest on the right. A doorbell pulses the chip: rings
// in the status color grow out from its edge and fade, and the chip brightens
// a little at each peak.
//
// The module is standalone: App owns a ChipState, feeds it the inputs each
// tick and draws it last (on top of everything) in every dirty strip.
//   chipUpdate steps the animation and returns the frame area that changed;
//   the caller marks those rows dirty.
//   chipDraw draws what intersects `clip`, so it can be called once per strip.
// Everything chipDraw reads sits in ChipLook, quantized, so "changed" is exact:
// a tick that leaves the look as it was returns an empty Rect and costs no
// redraw.
//
// Words and colors are the panel's own status tiles (firmware theme.cpp
// kStatus: bg for the fill, fg for the word), in its day row; night mode
// crossfades to its night row over about 2 s.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"

namespace ss {

// ---- layout (frame pixels) ---------------------------------------------------

constexpr int kChipStatusCount = 6;   // 0 Open, 1 Busy, 2 Private, 3 Sleeping, 4 Away, 5 No signal
constexpr int kChipNoSignal = 5;

// 160 wide, not the brief's 140: "NO SIGNAL" in the panel's 23 px Jost is 134 px
// wide, which 140 cannot hold with any padding (the other five words fit 140).
constexpr int kChipW = 160;
constexpr int kChipH = 40;
constexpr int kChipMargin = 16;                              // from the right and bottom edges
constexpr int kChipX0 = SS_WIDTH - kChipMargin - kChipW;     // 624
constexpr int kChipY0 = SS_HEIGHT - kChipMargin - kChipH;    // 424

constexpr int kChipBadgeD = 34;       // dog badge diameter
constexpr int kChipBadgeGap = 12;     // chip edge to the first badge (clear of the pulse ring)
constexpr int kChipBadgeStep = 6;     // between two badges
constexpr int kChipArrowD = 34;       // request arrow diameter (the tile's art::kArrow)
constexpr int kChipArrowStep = 38;    // centre to centre (the tile's kReqStep)
constexpr int kChipArrowGap = 10;     // the badges (or the chip) to the newest arrow
constexpr float kChipFlashHalf = 0.5f;   // an urgent arrow: lit half a second, dim half a second (the tile's kFlashHalfMs)

// ---- tuning (all PROPOSED; the look is UNVERIFIED on the panel) -------------

// PROPOSED: how long a doorbell pulses the chip, and how many rings it sends in that time.
constexpr float kChipPulseSeconds = 10.0f;
constexpr int kChipPulseCount = 4;
// PROPOSED: how far a ring grows beyond the chip's edge before it has faded out.
constexpr int kChipRingGrow = 10;

// ---- state --------------------------------------------------------------------

// Everything chipDraw reads, quantized. Plain bytes with no padding, so two
// looks are the same exactly when memcmp says so.
struct ChipLook {
  uint8_t status;   // 0..5: the word, and the color being faded to
  uint8_t from;     // the color being faded from (== status once settled)
  uint8_t mix;      // 0 = from .. 255 = status; the word switches at the halfway point
  uint8_t night;    // 0 = the panel's day row of colors .. 255 = its night row
  uint8_t dogs;     // badge slots shown: bit 0 Lucy, bit 1 Copper
  uint8_t pop[2];   // badge size while it pops in, 0..255 of full (Lucy, Copper)
  uint8_t glow;     // pulse brightening of the chip, 0..255
  uint8_t ring_r;   // pulse ring distance beyond the chip's edge, quarter pixels
  uint8_t ring_a;   // pulse ring opacity, 0..255 (0 = no ring)
  uint8_t req[3];   // the request arrows shown, oldest first (Inputs::req values, 0 none)
  uint8_t req_n;    // how many
  uint8_t req_dim;  // the urgent arrows are in the dim half of their flash (0 or 1)
};

// POD, lives by value in the App. About 50 bytes.
struct ChipState {
  ChipLook look;
  float mix_t;      // status fade progress 0..1
  float night_f;    // 0 day .. 1 night, eases toward the night flag
  float pop_f[2];   // badge pop-in progress 0..1
  float pulse_t;    // seconds since the doorbell; < 0 when not pulsing
  float flash_t;    // the urgent arrows' flash clock, seconds
  uint8_t req_want[3], req_want_n;   // the arrows asked for (chipRequests), taken at the next chipUpdate
  bool primed;      // the first chipUpdate has run (it sets the look with no fade)
};

void chipInit(ChipState& c);

// Per tick (SS_TICK_HZ). `status` 0..5 as Inputs::other_status (anything else
// reads as No signal), `dog_out` as Inputs::dog_out with 3 for both dogs, `night`
// as Inputs::night_mode. Returns the frame area that changed this tick (the old
// and the new bounds together, pulse ring included), or an empty Rect when the
// look is the same as last tick's. The first call sets the look at once, with
// no fade.
Rect chipUpdate(ChipState& c, int status, int dog_out, bool night, float dt);

// Start the pulse (or restart it if one is running). Takes effect at the next chipUpdate.
void chipDoorbell(ChipState& c);

// The request arrows to show (Inputs::req values, oldest first, no zeros; at most 3 are kept).
// Takes effect at the next chipUpdate, which then also runs their flash.
void chipRequests(ChipState& c, const uint8_t* req, int n);
// Where request arrow i (0 the oldest) of the look's arrows is centred; its radius is kChipArrowD / 2.
float chipArrowX(const ChipState& c, int i);

// Everything the chip covers now: the capsule, the badges shown, the request
// arrows, the pulse ring if one is visible. Always inside the frame.
Rect chipBounds(const ChipState& c);

// Draw onto `strip` (a strip or a whole frame; frame coordinates), touching
// only pixels inside `clip` and inside chipBounds. Needs the rasterizer scratch
// (rasterSetScratch), which must start zeroed: the rasterizer's accumulation
// row relies on it, and a scratch with stale bytes in it draws stray lines.
// Only CHIP_OUTLINE is taken from `pal`. A few hundred bytes of stack.
void chipDraw(const ChipState& c, Surface& strip, const Rect& clip, const Palette& pal);

// The word the chip shows for a status (0..5; anything else reads as No signal),
// and the width in pixels of its ink. Inspection hooks for tests and layout checks.
const char* chipLabel(int status);
int chipLabelWidth(int status);

}  // namespace ss
