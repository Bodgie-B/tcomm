// The build-up beat: at the start of every loop the empty scene assembles
// itself. The sky fades in from the horizon color, the ridges rise from behind
// the ground line, the trees stand up with an overshoot, the stream rolls in
// from the right, the plants pop (the mushroom last) and the fire pit is laid
// and lit (brief section 6, beat 1).
//
// Mechanism: the static scene is split into layers in cache draw order. A
// layer is either built (final pose), active (animating this phase) or not
// there yet. The cache holds only the built layers below the first active one;
// the active layers and every built layer in front of them are drawn
// dynamically, so depth stays right while something behind animates. The cache
// is rebuilt only when that set changes, once per phase boundary.
#pragma once

#include "../anim/scheduler.h"
#include "../ss_platform.h"

namespace ss {

// Static layers in cache draw order, far to near.
enum BuildLayer : uint8_t {
  BL_SKY = 0,    // bands, stars, sun or moon
  BL_RIDGES,
  BL_FAR_TREES,
  BL_MID_PINES,
  BL_GROUND,     // flat ground from the ground line down; there from the first tick
  BL_STREAM,     // far bank and water body
  BL_CAMP_PINE,
  BL_ROCKS,
  BL_FG_PINES,
  BL_TUFTS,      // the tufts that do not sway
  BL_MUSHROOM,
  BL_COUNT
};

#define BL_BIT(l) ((uint16_t)(1u << (l)))
#define BL_ALL ((uint16_t)((1u << BL_COUNT) - 1u))

enum BuildPhase : uint8_t { BP_SKY = 0, BP_RIDGES, BP_TREES, BP_STREAM, BP_PLANTS, BP_FIRE, BP_DONE };

struct BuildState {
  uint8_t phase;     // BuildPhase
  float u;           // linear progress within the phase, 0..1
  uint16_t built;    // layers in their final pose
  uint16_t active;   // layers animating in this phase
  uint16_t cached;   // layers the cache holds: built and below the first active layer
};

// Seconds the build beat lasts; the ambient beat fills the rest of the loop.
#define SS_BUILD_SECONDS 13.0f

// Loop start: only the ground is there and the sky phase is about to run.
void buildReset(BuildState& b);
// Set the state from the build beat's progress t (0..1).
void buildSetProgress(BuildState& b, float t);

inline bool buildDrawn(const BuildState& b, int layer) {
  return ((b.built | b.active) & BL_BIT(layer)) != 0;
}
inline bool buildCached(const BuildState& b, int layer) { return (b.cached & BL_BIT(layer)) != 0; }
inline bool buildDone(const BuildState& b) { return b.phase == BP_DONE; }

// Per-element animation values. Each returns the final value once its phase is
// over and the starting value before it begins, so drawing code can call them
// unconditionally.
float buildSkyFade(const BuildState& b);                      // 0 flat horizon color .. 1 full sky
float buildRidgeDrop(const BuildState& b, int ridge);         // 1 sunk below the ground line .. 0 in place
float buildTreeScale(const BuildState& b, float x);           // background tree height factor, overshoots
float buildNearTreeScale(const BuildState& b, int which);     // 0 camp pine, 1 and 2 the foreground pines
float buildStreamReveal(const BuildState& b);                 // 0..1 of the stream length, right to left
float buildPlantScale(const BuildState& b, float x, float y); // rocks and tufts, staggered, overshoots
float buildMushroomT(const BuildState& b);                    // 0..1 raw progress of the mushroom's entrance
float buildFireT(const BuildState& b);                        // 0..1 raw progress of the fire phase

// Frame rows [y0, y1) the active layers of this phase can touch, for dirty marking.
void buildPhaseRows(const BuildState& b, int& y0, int& y1);

// The beat itself ("build"). Drives World::build and lays and lights the fire pit.
extern const Beat kBuildBeat;

}  // namespace ss
