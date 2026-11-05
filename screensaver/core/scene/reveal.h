// The touch reveal (brief section 11, "The touch reveal"): a circle grows from
// the touch point and copies the main TComm screen over the frozen scene, with
// a thin ring in a palette colour at its edge. Ease-out, so it starts fast and
// settles.
//
// The main screen comes from the host as a full frame (the layer cache, which
// the frozen scene no longer needs; see appRevealBegin). Each step copies only
// what the circle newly covers, row by row, so the work per frame is the band
// between the old radius and the new one plus the ring. The last step copies
// the whole frame, so the screen ends exactly as the main screen, pixel for
// pixel (brief: it must look as it would have without the screensaver).
#pragma once

#include "../gfx/surface.h"
#include "../ss_platform.h"

namespace ss {

// PROPOSED (brief: about 300 to 400 ms; ring width not given). UNVERIFIED on
// the panel: how fast the panel can show the band each frame.
constexpr float kRevealSeconds = 0.35f;
constexpr float kRevealRing = 3.0f;   // px

struct RevealState {
  bool active;
  bool done;
  float cx, cy;        // the circle's centre (the touch, at a pixel centre)
  float t;             // seconds since the touch
  float r_max;         // radius at which the ring has left the farthest corner
  // Per row, the columns already showing the main screen: [lo, hi), none when lo >= hi.
  int16_t lo[SS_HEIGHT];
  int16_t hi[SS_HEIGHT];
};

void revealBegin(RevealState& r, int x, int y);

// Advance by dt seconds and draw into fb: the main screen (`main`, a full frame
// with stride SS_WIDTH) wherever the circle now covers, then the ring just
// outside it. Returns true once fb is all main screen.
bool revealStep(RevealState& r, Surface& fb, const Pixel* main, Pixel ring, float dt);

}  // namespace ss
