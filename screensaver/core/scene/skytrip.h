// The mushroom trip's sky: "the sky lets go" (MUSHROOM_PLAN.md section 6.2; Mike, 2026-11-18: "having
// mushrooms grow near the one mushroom after a rain storm, having hiker pick one, eat one, lay down and
// have the sky go psychodelic for a couple minutes").
//
// OWNERSHIP: skeleton made by the foundation step (A1); step B4 (the sky) filled in the ramps, the band
// draw and the pastel table, driven from its stills tool (tools/trip_stills.cpp). Step C5b wires it into
// drawSky and drawRidge (landscape.cpp) and the band repaint (day.cpp dayTrack); C5c runs the clock.
//
// The picture. At rest it is exactly the sky as it is. Then its own four band edges start to wave,
// slowly, like the edge of a tide, each with its own wavelength and its own direction, and the five
// bands take soft sherbet colours that climb the sky one band every 16 s. The ridges, the pines and the
// camp stay the dark natural silhouettes they are (they are drawn over the sky in the cache), so the
// flat paper sky comes alive behind a still world. Nine day stars come out one by one and each turns a
// small slow circle with a short tail; the sun grows a soft double halo that breathes. Nothing starts
// or stops at once: every ramp is an ease of 12 s or more.
//
// How it reaches the screen (plan 6.2, REVIEW_FIXES F1, F2, F6, F7, F28). The cache holds the sky.
// Every tick of the trip one band of SKYTRIP_BAND_ROWS rows (0..9 in turn) is repainted with the sky
// clock latched for it in Landscape::trip.tq (skyTripLatch). drawSky (C5b) draws each band that meets
// its clip from that band's own latched clock, with the clip cut to the band:
//   tq 0      the old path, bit for bit (fillRect bands, stars, body, deck);
//   tq != 0   skyTripBand(... skyTripSeconds(tq) ...), then the sun or moon as now, then the deck with
//             skyTripDeckPal.
// So every pixel is a pure function of state and a full rebuild equals the rolling one. From t = 102 s
// skyTripLatch returns 0, so the ordinary rolling repaint puts every band back on the old path (F2).
// The halo reaches past landscapeSunBounds: C5b widens the app's sun repaint by skyTripHaloReach (F1).
//
// This header is included by landscape.h, so it must not include landscape.h or world.h.
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"

namespace ss {

struct Landscape;
struct SkyDraw;

// The sky is repainted one cache band of 32 rows a tick: rows 0..319 are ten bands.
#define SKYTRIP_BANDS 10
#define SKYTRIP_BAND_ROWS 32
// The trip's length on the sky clock (plan 5.4; PROPOSED kSkyS).
#define SKYTRIP_SECONDS 104

// The sky clock each band was last painted at, in ticks (16 bits); 0 natural.
struct LsTrip {
  uint16_t tq[SKYTRIP_BANDS];
};

// Any band away from the natural sky.
bool skyTripOn(const LsTrip& t);

// Repaint one band every this many ticks (plan 6.4 kTripBandEvery, PROPOSED 1). 2 halves the cost and
// each band is then repainted every 0.8 s; the trip runs at 2 while a vision reaches above row 320
// (REVIEW_FIXES F3; day_trip.cpp tripDayStep).
constexpr int kTripBandEvery = 1;
// The sky clock moves by this much for every band repainted, whatever the cadence (F3): one tick's worth.
// So at one band every second tick the clock runs at half speed, every edge and star at half its speed,
// and nothing travels further between two repaints of its band (ten band turns, 0.4 s of the sky clock)
// than at one band a tick. That is the plan's 1.5 px rule (6.2), kept at any cadence; test_skytrip checks
// it through the app.
constexpr float kTripClockStep = 1.f / (float)SS_TICK_HZ;

// The clock to latch into a band repainted now, at sky time t seconds: whole ticks, at least 1 while
// the trip shows; 0 before it and from 102 s on (every ramp is flat by then), which sends the band
// back to the natural path (F2).
uint16_t skyTripLatch(float t);
// A latched clock back in seconds.
inline float skyTripSeconds(uint16_t tq) { return (float)tq * (1.f / (float)SS_TICK_HZ); }

// The ramps, functions of the sky clock alone (seconds).
float skyTripWave(float t);     // kw: the edges' wave, 0 .. 1 by 14 s, back to 0 from 90 to 102
float skyTripColour(float t);   // kc: the pastels, a third by 14 s, full by 30, gone from 86 to 100
float skyTripStarK(int i, float t);   // star i's presence 0..1 (out of the sky's way near the sun: skyTripStar)

// Edge j (0..3, the joins of the five bands) at frame x, sky time t: y in frame px.
float skyTripEdgeY(int j, float x, float t);
// Its rest y and its largest swing (the edge stays inside rest +- amp).
int skyTripEdgeRest(int j);
float skyTripEdgeAmp(int j);
// Which of the five bands (0 top .. 4 bottom) frame point (x, y) is in at t.
int skyTripBandAt(float x, float y, float t);

// The five pastels as 0xRRGGBB: lilac, rose, peach, butter, mint (PROPOSED, plan 6.2).
uint32_t skyTripPastel(int k);
// A colour given as 0xRRGGBB, scaled by how bright the palette's sky is now against the day key's
// (the method of critterLitAs, critters.cpp), capped at 1: so it dims with the mood, the overcast and
// the night rule like the sky it sits on. The glints (trip.cpp, C5c) use it too.
Pixel skyTripLit(const Palette& pal, uint32_t hex);
// Band b's colour at t: the sky's own slot drawn toward the pastel climbing through it, never brighter
// than the brightest of the palette's own sky slots.
Pixel skyTripBandColour(const Palette& pal, int b, float t);

// The sun as the halo needs it. C5b fills it from the real sky (skyTripSunOf) or the parked body.
struct SkyTripSun {
  bool on;             // drawn, and not behind a cloud deck
  float x, y, r;
  Pixel col;           // the disc's colour (landscapeSunColor)
};
SkyTripSun skyTripSunOf(const SkyDraw& sky, const Palette& pal);
// How far the halo reaches from the sun's centre, with the fringe: the app's sun repaint and its dirty
// mark must cover this while the trip is on (F1).
int skyTripHaloReach(int sun_r);

// Star i at t: its three dots (itself and two behind it on its circle), their radii and their weight
// (how far each is drawn toward the star colour). False when it is not out (or sits near the sun).
struct SkyTripStar {
  Vec2 p[3];
  float r[3];
  float k[3];
};
bool skyTripStar(const Landscape& ls, const SkyTripSun& sun, int i, float t, SkyTripStar& out);
// Star i is left out because it sits too near this sun (the test skyTripStar makes). When the real sun
// moves a pixel that answer can change for a star well outside the sun's own rows, so the app repaints
// skyTripStarBounds(i) too when it does (F1). The bounds hold the star's whole circle at any time.
bool skyTripStarHidden(const Landscape& ls, const SkyTripSun& sun, int i);
Rect skyTripStarBounds(const Landscape& ls, int i);

// One band of the trip's sky into `s`, clipped to `clip` (normally one band of rows, but any clip
// gives the same pixels): the waving bands, the halo behind the sun, the wheeling stars. Every pixel
// in clip's sky rows (above LS_GROUND_Y) is written. The caller then draws the sun or moon and the
// deck over it, as drawSky does now. Primitives: one rectangle, the band fills that meet the clip (two
// for most 32-row bands, three for rows 192..224, where the fills under edges 1, 2 and 3 overlap), the
// two halo discs, three dots a star.
void skyTripBand(const Landscape& ls, Surface& s, const Palette& pal, const SkyTripSun& sun, float t,
                 const Rect& clip);
// The cloud deck (cover 4 and up) takes the top bands' colours: CLOUD_SHADE and CLOUD_LIT drawn half
// way toward bands 0 and 1 by kc.
void skyTripDeckPal(const Palette& pal, float t, Palette& out);
// The far ridge's lift (plan 6.2, PROPOSED on): drawn up to 2 px higher, breathing over 9 s.
float skyTripRidgeLift(float t);

}  // namespace ss
