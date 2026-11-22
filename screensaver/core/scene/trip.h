// The mushroom trip's own pieces: what he holds and what he sees, and the two free layers (the grass's
// slow wave, the glints' colours) (MUSHROOM_PLAN.md sections 5, 6.5 and 8.4; Mike's words: "having
// mushrooms grow near the one mushroom after a rain storm, having hiker pick one, eat one, lay down and
// have the sky go psychodelic for a couple minutes").
//
// OWNERSHIP: skeleton made by the foundation step (A1); step C5c (the major) filled it in. The module
// knows no World: day_trip.cpp writes TripState, this file steps the loose things, says where they are
// and draws them. The two calls ambient.cpp makes (tripSway, tripGlintColor) are the exception and read
// the World's day union.
//
// What is drawn, in three parts that sort among the ground actors (day.h DAY_PARTS):
//   part 0  things on the ground: the three pickables while they are his but still standing, the bunch
//           laid in the grass while he lies, the caps he tossed riding the stream away;
//   part 1  what is in his hands: the bunch in the far hand, the one he sniffs and eats in the near hand;
//   part 2  what he sees: the tree that grows from the deer's head (antlers, leaves, the small bird, the
//           leaves letting go), and set 1's fish and smoke rings.
// The three never need two sort lines at once inside a part (the pickables, the laid bunch and the
// floating caps come one after the other), which is why three parts are enough.
//
// Dirty marking: tripDirty compares every field a part's draw reads (hashed) and returns the old and new
// bounds of each part that changed. The antlers follow the deer's crown in whole pixels and grow in 64
// steps, so a breathing deer marks nothing for them (REVIEW_FIXES F3).
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"
#include "patch.h"

namespace ss {

struct World;

#define TRIP_PROPS 8
#define TRIP_PARTS 3
#define TRIP_DIRTY_MAX (2 * TRIP_PARTS)

enum TripPropKind : uint8_t {
  TP_NONE = 0,
  TP_CAP,     // a tossed cap: mode 0 in the air, 1 riding the stream (day_trip.cpp moves it along the water)
  TP_FISH,    // set 1: a fish in a slow high arc; tint its pastel; k its arc's progress
  TP_RING,    // set 1: a smoke ring rising from the cold pit; k its progress
  TP_LEAF,    // set 0: a leaf letting go of the tree
  TP_BIRD     // set 0: the small bird; mode 0 flying, 1 perched
};

struct TripProp {            // 28 bytes
  uint8_t kind;
  uint8_t mode, tint, pad;
  Vec2 pos, vel;
  float t, rot, k;
};

struct TripState {
  uint8_t set, pocket, sky_on, lifted;      // the variant; the sky is running; the pickables are the module's
  uint8_t in_far, in_near, bite, bunch_down;   // caps in the far hand's bunch; one in the near hand; bites taken; the bunch lies in the grass
  uint8_t band, due, dog, vis;              // next sky band; a band is due this tick; 1 + the lent dog's slot; the vision's stage
  uint8_t slow, phase, solo, picked;        // one band every second tick (F3); the tick's parity; the sky with no story (tests); picked so far
  uint8_t n_pick, pick_i, antler_q, leaves; // pickables that stand; the one being picked (3 none); antlers 0..64; leaves open 0..8
  uint8_t dog_stage, hello, wading, v_over; // the dog's stage; his hello to the chipmunk ran; the wade's own pacing; visions finished
  float sky_t;                              // the sky clock, s (kTripClockStep per band repainted)
  float T;                                  // seconds since the sky began, on the beat's clock
  float onset_t;                            // seconds since it came on (the grass)
  float ks, kg;                             // the grass's and the glints' weights
  float vt, dog_t, gap_t;                   // seconds in the vision's stage, in the dog's, since the last vision left
  float pick_u, rock;                       // the pick's progress 0..1; the first pickable's rock (turns)
  float trace;                              // the arm that traces the sky, 0..1 of its nine seconds
  Vec2 hand[2];                             // his hands this tick (0 near, 1 far): the held things are drawn there
  Vec2 bunch_at;                            // the bunch in the grass
  int16_t antler_x, antler_y;               // the deer's crown, whole px
  int16_t ground_y, vis_y;                  // part 0's and part 2's sort lines (day_trip.cpp sets them)
  ShroomLook look[3];                       // the lifted pickables as the cache drew them
  TripProp prop[TRIP_PROPS];
};

// ---- the module (no World) ----

// Free prop slot, or -1.
int tripPropNew(TripState& t, uint8_t kind);
// One tick of the loose things that move by themselves (caps in the air, leaves, the bird's wings).
void tripStep(TripState& t, float dt);
// Bounds of one part (empty when it shows nothing) and of everything.
Rect tripPartBounds(const TripState& t, int part);
Rect tripBounds(const TripState& t);
// The parts whose drawing differs between a and b: their old and new bounds. Returns the count.
int tripDirty(const TripState& a, const TripState& b, Rect out[TRIP_DIRTY_MAX]);
void tripDraw(const TripState& t, int part, Surface& s, const Rect& clip, const Palette& pal);
// The tip of the tree where the bird lands, and tip j (0..7) for the leaves.
Vec2 tripAntlerTip(const TripState& t, int j);

// ---- the free layers (ambient.cpp) ----

// The grass's (and the reeds') sway at x during the trip: `plain` blended toward the shared slow wave by
// the trip's weight ks. Called only from ambientWindAt, and only while w.day.kind == DAY_TRIP.
float tripSway(const World& w, float x, float plain);
// Glint i's colour: WATER_LIGHT, or during the trip a pastel that changes every 3 s, one glint at a time.
Pixel tripGlintColor(const World& w, int i, const Palette& pal);

}  // namespace ss
