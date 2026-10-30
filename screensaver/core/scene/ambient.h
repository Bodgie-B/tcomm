// The ambient layer: always on, never asks for attention (brief section 4).
// Swaying grass, stream flow highlights, fire flicker and smoke, a distant bird.
// Clouds drift in landscape.cpp. Everything here is a sum of sines on a clock,
// so it costs no randomness except the bird's schedule.
//
// Sub-rates: grass and smoke advance every 2nd tick (12.5 Hz still reads as
// wind and costs half the strips); the stream highlights, the fire and the bird
// move every tick.
#pragma once

#include "../gfx/surface.h"
#include "../palette.h"

namespace ss {

struct World;
struct FirePit;
struct Rng;

#define AMB_TUFT_MAX  14   // swaying tufts picked from Landscape::tufts
#define AMB_GLINT_MAX 9    // stream highlight dashes
#define AMB_SMOKE_MAX 5    // smoke puffs

struct StreamGlint {
  float s;       // position along the stream in point-index units (0 = right edge)
  float lane;    // offset across the stream as a fraction of the half width
  float len;     // dash length, px
  float phase;   // shimmer phase, turns
};

struct Bird {
  bool active;
  float x, y;        // body point
  float y0;          // path center line
  float dir;         // +1 flies right, -1 left
  float speed;       // px per tick
  float path_ph;     // path sine phase, turns
  float flap;        // flap clock, turns
  uint32_t wait;     // ticks until the next crossing starts
};

struct Ambient {
  float wind_t;                     // grass and smoke clock, seconds (advances on sub-ticks)
  int n_tufts;
  uint8_t tuft[AMB_TUFT_MAX];       // indices into Landscape::tufts
  float sway[AMB_TUFT_MAX];         // current tip offset, px at an 18 px blade
  StreamGlint glint[AMB_GLINT_MAX];
  Bird bird;
};

// Loop start. Picks the swaying tufts (flags them in the landscape so the
// static draw skips them), seeds the glints and schedules the first bird.
void ambientInit(World& w, Rng& rng);
// One tick of every ambient clock. Also eases the fire toward its state.
void ambientStep(World& w, Rng& rng);
// True on ticks where grass and smoke moved.
bool ambientSubTick(const World& w);

// Bounds in frame space; empty when the element is not visible.
Rect ambientTuftBounds(const World& w, int i);
Rect ambientGlintBounds(const World& w, int i);
Rect ambientBirdBounds(const World& w);
Rect ambientSmokeBounds(const World& w, int i);
Rect fireBounds(const FirePit& f);

void ambientDrawTuft(const World& w, int i, Surface& s, const Rect& clip, const Palette& pal);
void ambientDrawGlints(const World& w, Surface& s, const Rect& clip, const Palette& pal);
void ambientDrawBird(const World& w, Surface& s, const Rect& clip, const Palette& pal);
void ambientDrawSmoke(const World& w, Surface& s, const Rect& clip, const Palette& pal);
void fireDraw(const FirePit& f, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
