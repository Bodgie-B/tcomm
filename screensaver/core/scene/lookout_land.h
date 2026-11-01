// The Lookout's static landscape: a hillside rising from the lower left to a
// ridge on the right, a switchback trail up it, thinning pines, the valley
// behind in three atmospheric layers, and an original timber fire-lookout
// tower on the ridge (SPINES_WEATHER_LOOKOUT_SPEC.md section 5).
//
// Same idioms as landscape.h: geometry is generated once from an Rng into
// fixed arrays (a seed replays the place), nothing allocates, shapes refer to
// palette slots, and nothing is random at draw time. Frame is 800x480, y down.
//
// Reserved: the bottom-right region x >= 660, y >= 420 holds only ground and
// grass (the status chip). Nothing here puts a pine, the trail or the tower
// there.
//
// The tower is seen from the south-west in a simple oblique view: the front
// (south) face square to the viewer, the west face visible to its left. The
// depth axis (south to north) projects to LK_DEPTH_DX, LK_DEPTH_DY px. West is
// frame-left, where the valley and the sunset are.
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "landscape.h"

namespace ss {

#define LK_MAX_TRAIL     8      // trail vertices (5 or 7 legs)
#define LK_MAX_PINES     14
#define LK_MAX_TUFTS     32
#define LK_MAX_SPIKES    80

// Tower dimensions in px, relative to its base (tower_x, tower_gy).
#define LK_LEG_H         58     // ground to catwalk deck
#define LK_LEG_BASE_HW   40     // front legs' half spread at the ground
#define LK_LEG_TOP_HW    30     // ... at the deck (= the cab's half width)
#define LK_CATWALK       9      // catwalk beyond the cab walls
#define LK_CAB_H         44     // deck to eaves
#define LK_ROOF_H        10     // eaves to apex
#define LK_ROOF_OVER     5      // eave overhang
#define LK_RAIL_H        20     // top rail above the deck
#define LK_POLE_H        11     // flagpole above the apex
#define LK_DEPTH_DX      (-18)  // south-to-north depth of the cab and frame, projected
#define LK_DEPTH_DY      4

struct LkTrail {
  int n;                       // vertices, n - 1 legs
  Vec2 p[LK_MAX_TRAIL];        // feet positions; p[0] off the left edge, p[n-1] the stair foot
  float cum[LK_MAX_TRAIL];     // arc length from p[0] to p[i]
};

struct LookoutLand {
  float valley[3][LS_CREST_N];   // 0 far, 1 mid, 2 near valley crest y (RIDGE_FAR/MID/NEAR)
  float hill[LS_CREST_N];        // hillside skyline y, every LS_CREST_STEP px from LS_CREST_X0

  int n_spikes;                  // tree line along the near valley crest (TREES_FAR)
  LsSpike spikes[LK_MAX_SPIKES];

  LkTrail trail;

  int n_pines;                   // sorted by base_y, farthest (smallest y) first
  LsPine pines[LK_MAX_PINES];

  int n_tufts;
  LsTuft tufts[LK_MAX_TUFTS];

  float fg_y[2];                 // foreground band (GROUND_DARK) top at x = 0 and x = 800
  float fg_ph;                   // its wobble phase
  float sh_y[2];                 // near shoulder line (GROUND) at x = 0 and x = 800; above it, up to
  float sh_ph;                   // the skyline, the farther slope in a lighter tone (atmospheric)

  int16_t tower_x;               // centre of the cab's front (south) face
  int16_t tower_gy;              // ground y under the tower (the flat ridge crest)

  // The skyline per pixel column (lookoutSkylineAt over the column, floored):
  // the first row of the crests, the hill, the tree teeth and the pine tiers
  // (not the trail, whose top end draws one row above it in a few columns
  // inside the tower's bounds; not the tower). The sun and the stars
  // (lookout.cpp) draw only in the rows above it; where a crest's coverage in
  // the row rounds to 0 it can sit one row above the first drawn row. Filled
  // by lookoutLandGenerate (no rng).
  int16_t sky_top[SS_WIDTH];
};

void lookoutLandGenerate(LookoutLand& lk, Rng& rng);

// Draw the whole static scene into a full frame (origin_y 0): sky bands,
// valley layers, hillside, trail, pines, grass, tower. The sky is left plain
// (no sun, no clouds: the dynamic layer draws clouds). `mood` sets the
// window glint (catches the sun around 4.0 to 4.6, dark from about 5.4) and
// how far the timber goes to silhouette toward night. `tower_reveal` 0..1:
// 0 no tower; to 0.5 a flat ACTOR silhouette rising from behind the ridge; 0.5
// to 1 the detail switches in, in stages (frame, then cab and roof, then the
// windows). Flat slots throughout, no alpha.
void lookoutLandDrawStatic(const LookoutLand& lk, Surface& frame, const Palette& pal, float mood,
                           float tower_reveal);

// Layers of the Lookout's scene in draw order, far to near (a bit mask picks
// which ones a call draws). The static cache holds a prefix of them; the
// build beat animates SKY .. PLANTS (lookout.cpp), the reveal the tower, and
// once he is on the tower the cab's front and the rails are drawn after him
// (or the cab before him while he is outside it): LKL_TOWER is everything of
// the tower but those, including what is seen through the cab's windows.
enum LkLayer : uint8_t {
  LKL_SKY = 0, LKL_VALLEY, LKL_HILL, LKL_TRAIL, LKL_PLANTS,
  LKL_TOWER,   // legs, bracing, deck, stair treads and landings, the rails behind the cab, the west
               // wall, the sky and the fire-finder seen through the front panes
  LKL_CAB,     // the cab's front wall and glass (holes where the through-view is), the west pane, the
               // glints, the roof and flagpole
  LKL_RAILS,   // the stair handrails and posts, the landing rails, the deck's front and west rails
  LKL_COUNT
};
#define LKL_BIT(l) ((uint16_t)(1u << (l)))
#define LKL_LAND ((uint16_t)(LKL_BIT(LKL_SKY) | LKL_BIT(LKL_VALLEY) | LKL_BIT(LKL_HILL) | LKL_BIT(LKL_TRAIL) | LKL_BIT(LKL_PLANTS)))
#define LKL_ALL ((uint16_t)((1u << LKL_COUNT) - 1u))

// What a draw call shows: the layers, and the build's and reveal's state.
// The defaults (lookoutDrawDone) are the finished scene.
struct LkDraw {
  uint16_t layers;        // LKL_BIT mask
  float sky;              // 0 a flat horizon colour (SKY4) .. 1 the sky bands
  float valley_drop[3];   // px each valley layer sits below its place (rising in the build)
  float trail;            // 0..1 of the trail's length drawn (it draws itself up the hill)
  float plants_u;         // the plants phase: < 0 none yet, 0..1 popping (staggered, overshoot), >= 1 all
  float reveal;           // the tower, as lookoutLandDrawStatic's tower_reveal
  float mood;             // window glint and timber silhouette (see lookoutLandDrawStatic)
};
LkDraw lookoutDrawDone(float mood, float reveal, uint16_t layers);

// Draw `d.layers` into `s` (a full frame or a strip), clipped to `clip`.
void lookoutLandDraw(const LookoutLand& lk, Surface& s, const Rect& clip, const Palette& pal, const LkDraw& d);

// Hillside skyline y at frame x, linear between the samples (matches the drawn edge).
float lookoutHillAt(const LookoutLand& lk, float x);

// The skyline at frame x: the lowest y (highest on screen) of what the static
// landscape draws there, the tower aside: the three valley crests and the
// hill (SPINES_WEATHER_LOOKOUT_SPEC.md section 5b), and also the near valley's
// tree line and the hill pines' tiers where they stand above those (both are
// in the cache, so a sun or a star drawn over them would show on them).
float lookoutSkylineAt(const LookoutLand& lk, float x);
// The sky band (0..4, SKY0..SKY4) the static sky fills row y with.
int lookoutSkyBand(int y);
// The first landscape row in pixel column x (LookoutLand::sky_top; x clamped to the frame).
int lookoutSkyTop(const LookoutLand& lk, int x);

// Feet position along the trail, s 0..1 by arc length: 0 off the left edge at
// the foot, 1 the tower's stair foot (= lookoutStairAt(lk, 0)).
Vec2 lookoutTrailAt(const LookoutLand& lk, float s);

// Hiker height (px) for feet at frame y on the hillside: 90 at the trail's
// foot, 44 at the stair foot on the ridge, linear in y between them (clamped),
// so it follows how far up the slope he is on screen. Monotone in y.
float lookoutHeightAt(const LookoutLand& lk, float y);

// Bounds of the cab's front (LKL_CAB) and of the rails (LKL_RAILS) at full
// reveal, for dirty marks when their draw order changes.
Rect lookoutCabBounds(const LookoutLand& lk);

// Hiker height factor along the trail: 1.0 at the foot, 0.49 at the ridge
// (90 px to 44 px). Linear in s, so monotone.
float lookoutTrailScale(float s);

// Feet position on the zigzag stair, s 0..1 by arc length: flight 1 up to the
// right, landing 1 (walk to its end, turn), flight 2 up to the left, landing 2,
// flight 3 up to the right onto the catwalk deck at the stair gap in the rail.
Vec2 lookoutStairAt(const LookoutLand& lk, float s);
// The stair's polyline knots (6) and their s values, for scripts that pause on
// a landing: landing 1 is s in [kn_s[1], kn_s[2]], landing 2 [kn_s[3], kn_s[4]].
void lookoutStairKnots(const LookoutLand& lk, Vec2 knots[6], float kn_s[6]);

// Where he sits: the deck's west edge at mid depth, under the west rail. He
// faces west (frame-left), legs over the edge; the top rail is LK_RAIL_H above.
Vec2 lookoutRailSeat(const LookoutLand& lk);

// Spots on the catwalk for the explore beat. `*_feet` are feet positions on
// the deck; the others are hand or eye targets.
struct LookoutCabPoints {
  Vec2 peek_feet[3];   // 0 front-left pane, 1 front-right pane, 2 west pane (on the west catwalk)
  Vec2 peek_eye[3];    // the pane centre he looks into
  Vec2 finder;         // top centre of the fire-finder table (inside the cab, seen through the middle pane)
  Vec2 finder_feet;    // standing at the finder, inside the cab (behind the front wall)
  Vec2 compass;        // the compass plate: on the front top rail, right of the stair gap
  Vec2 compass_feet;
  Vec2 stair_head;     // top of the stair, on the deck (= lookoutStairAt(lk, 1))
  float deck_y;        // deck surface y
  float rail_y;        // top rail y on the front
};
void lookoutCabPoints(const LookoutLand& lk, LookoutCabPoints& out);

// Integer frame bounds of the whole tower at full reveal (flagpole, roof,
// rails, legs, stair), clipped to the ground line under it.
Rect lookoutTowerBounds(const LookoutLand& lk);

}  // namespace ss
