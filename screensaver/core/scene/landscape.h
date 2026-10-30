// Static landscape geometry and drawing, plus the drifting clouds.
//
// Geometry is generated once per loop from an Rng into fixed arrays, so a seed
// replays the same place. Nothing here allocates. Shapes refer to palette slots,
// never colors. Frame is 800x480, y down.
//
// Reserved: the bottom-right region x >= 660, y >= 420 holds only ground and
// grass (a status chip goes there later). Nothing in this file puts a tree,
// rock or stream there.
//
// The static scene is drawn layer by layer (BuildLayer order, buildup.h) so the
// build-up can animate one layer while the layers behind it sit in the cache.
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "buildup.h"

namespace ss {

// Ridge crests are sampled every 16 px over x = -16..816.
#define LS_CREST_N    52
#define LS_CREST_STEP 16
#define LS_CREST_X0   (-16)

#define LS_MAX_SPIKES   80
#define LS_MAX_MID_PINE 20
#define LS_STREAM_PTS   14
#define LS_MAX_RIPPLES  16
#define LS_MAX_ROCKS    8
#define LS_MAX_TUFTS    64
#define LS_STAR_COUNT   9

// Where things are fixed by the composition.
#define LS_HORIZON      272
#define LS_QUIET_X0     660
#define LS_QUIET_Y0     420
#define LS_GROUND_Y     318   // flat ground starts here; layers behind it rise from below it

// The stream's leading edge during the roll-in tapers to a point over this
// many point-index units (about 40 px).
#define LS_STREAM_TAPER 0.6f

struct LsSpike { int16_t x; uint8_t h; };                   // far tree line tooth
struct LsPine  { int16_t x, base_y, h; uint8_t tiers; uint8_t width_pct; };
struct LsStreamPt { float x, y, nx, ny, hw; };              // centerline, up-normal, half width
struct LsRipple { Vec2 a, b; };
struct LsRock { Vec2 p[6]; };
struct LsTuft { int16_t x, y; uint8_t n; uint8_t h[3]; int8_t off[3]; int8_t lean[3]; uint8_t dyn; };   // dyn: sways, drawn by ambient.cpp
struct LsStar { int16_t x, y; float r; };

struct Landscape {
  float crest[3][LS_CREST_N];            // 0 far, 1 mid, 2 near ridge crest y

  int n_spikes;                          // spike i spans x[i]..x[i+1]; one extra entry closes the last
  LsSpike spikes[LS_MAX_SPIKES];

  int n_mid;
  LsPine mid[LS_MAX_MID_PINE];           // sorted by height, shortest first

  LsStreamPt stream[LS_STREAM_PTS];
  float bank_w[LS_STREAM_PTS];           // far-bank strip width at each stream point, 8..14
  int n_ripples;                         // generated (keeps the stage 1 seeds) but no longer drawn:
  LsRipple ripples[LS_MAX_RIPPLES];      // the moving highlights in ambient.cpp replaced them

  int16_t camp_x;                        // bag-hanging pine trunk x
  LsPine camp;                           // geometry of its foliage (tiers, width)
  LsPine fg[2];                          // foreground pines at the left edge

  int n_rocks;
  LsRock rocks[LS_MAX_ROCKS];

  int n_tufts;
  LsTuft tufts[LS_MAX_TUFTS];

  LsStar stars[LS_STAR_COUNT];

  int16_t mush_x, mush_y;                // the build-up's last plant: base of its stem
};

void landscapeGenerate(Landscape& ls, Rng& rng);

// Draw the layers the build state says the cache holds into `frame` (full
// frame, origin_y 0). `mood` places the sun or moon and decides whether stars
// show. Before the sky is cached the frame is the flat horizon color.
void landscapeDrawStatic(const Landscape& ls, Surface& frame, const Palette& pal, float mood,
                         const BuildState& b);

// Draw, clipped to `clip`, the layers in [first, last] that are built or
// animating but not in the cache, each in its current build pose.
void landscapeDrawLayers(const Landscape& ls, Surface& s, const Rect& clip, const Palette& pal,
                         float mood, const BuildState& b, int first, int last);

// Stream centerline sample at fractional point index `s` (0 = off the right
// edge, LS_STREAM_PTS - 1 = off the left), linearly interpolated; the normal is
// renormalized.
LsStreamPt landscapeStreamAt(const Landscape& ls, float s);
// Point index of the stream's tip for a reveal fraction 0..1.
float landscapeStreamTip(float reveal);

// Stream centerline y and drawn half width at frame x (the water polygon is
// 1.06 x hw wide). False outside the stream's x range.
bool landscapeStreamSpan(const Landscape& ls, float x, float& cy, float& half);

// The two foreground pines stand in front of anything on the clearing. They
// sit in the cache; when an actor walks behind one, worldDrawDynamic draws it
// again over that actor, clipped to the actor's bounds.
Rect landscapeFgPineBounds(const Landscape& ls, int k);
void landscapeDrawFgPine(const Landscape& ls, int k, Surface& s, const Rect& clip, const Palette& pal);

// Ridge crest y at an arbitrary x, linearly interpolated between the 16 px
// samples (so it matches the drawn polygon edge exactly).
float landscapeCrestAt(const Landscape& ls, int layer, float x);

// ---- Clouds (dynamic) ----------------------------------------------------

#define CLOUD_MAX_LIT 6
#define CLOUD_WRAP_LEFT  (-140.f)
#define CLOUD_WRAP_RIGHT 940.f

struct Cloud {
  float x, y;                 // anchor; ellipse offsets are relative to it
  float speed;                // px per tick, leftward
  int n;                      // lit ellipses, 4..6
  float dx[CLOUD_MAX_LIT], dy[CLOUD_MAX_LIT], rx[CLOUD_MAX_LIT], ry[CLOUD_MAX_LIT];
  float sdx, sdy, srx, sry;   // flat shade ellipse along the bottom
  float bx0, by0, bx1, by1;   // bounds relative to (x, y), 1 px margin included
};

void cloudGenerate(Cloud& c, Rng& rng, float x, float y, float speed_px_per_s);
void cloudStep(Cloud& c);                       // one tick of drift and wrap
// Clouds are drawn at floor(x): the image changes only when the integer x does,
// so a 6 px/s cloud dirties its strips about every 4th tick, not every tick.
Rect cloudBounds(const Cloud& c);               // integer frame-space bounds, clipped to the sky
void cloudDraw(const Cloud& c, Surface& s, const Palette& pal, const Rect& clip);

}  // namespace ss
