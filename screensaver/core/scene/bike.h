// The mountain bike (morning and daytime plan, section 6): a bike drawn from a
// few primitives (two spoked wheels that turn as it rolls, a three-capsule
// frame, a fork, bars, a saddle, cranks that turn with the rear wheel), the
// riding poses that put the real hiker rig on it, and the water that flies
// when it goes through the stream. A standalone module like the kite: plain
// data, a step function, bounds and a draw. It owns no World state, calls no
// platform code and draws random numbers only from the Rng it is handed.
//
// Three things live here and are stepped together by bikeStep:
//
//   BikeState    the bike, the rider's motion, and the course they follow
//   SplashState  droplets, rings, crowns and the water laid over wheels and rider
//   (the rider)  not a state of its own: bikeApplyRider() turns BikeState into a
//                pose and a feet position for the hiker rig
//
// Variants (the plan's three), chosen when the ride is started (bikeRide):
//
//   BIKE_VAR_PASS    A. Rides in from off one edge, through the stream with a big
//                    splash fan at each wheel, out the other bank and off the far
//                    edge (BIKE_AWAY). The scene waits "a moment" and rides it
//                    back the other way with `proud` set: more upright, one arm up.
//   BIKE_VAR_ROCK    B. As A, until the front wheel meets the rock in the stream
//                    (course.rock_x): the bike stops dead and noses over, he goes
//                    over the bars in a dive arc into the water (a big splash), the
//                    bike falls on its side against the rock. Ends BIKE_DOWN with
//                    the rider sitting in the water (RIDER_DOWN).
//   BIKE_VAR_WHEELIE C. Rides in, and at course.wheelie_x pops a wheelie, holds it
//                    (it comes up in about a second, holds near 2.7 s, then tips for
//                    2 s and falls in 1: about 7 s from the pop), tips over backward slowly; he ends on his back
//                    with the bike upside down on top of him and both wheels still
//                    spinning down. Ends BIKE_DOWN with RIDER_DOWN.
//
// WHO MOVES THE HIKER: this module does, through bikeApplyRider(). Each tick,
// after hikerTick() and after whatever pose overlays the scene applies, the
// scene calls
//
//     if (bikeDriving(bike)) bikeApplyRider(bike, hiker);
//
// which overwrites hiker.pose (every joint), hiker.feet (the rig grounds itself,
// so the module works out where the feet must be for the hips to land on the
// saddle, in the air, or on the ground) and hiker.facing. The module reads
// hiker.height and the breathing phase and nothing else. The scene must
//   - give bikeInit the same height the hiker has (90 px), and set pack_on =
//     false for the ride (a pack on a leaning rider reads wrong; PROPOSED);
//   - not walk him or script poses on him while bikeDriving();
//   - when it stops driving him (bikeFreeRider, or the ride ends in BIKE_AWAY) take
//     him back: hikerSetPose(h, p, blend) before the next hikerTick starts the
//     blend from the pose the module left, so he stands up out of the water
//     (or off his back) smoothly. feet and facing are left where they were.
// The module never touches the hiker's carry, hat, pack or fx. The doorbell
// reaction freezes the whole thing for free if the scene calls bikeStep only
// where its other per-tick story code runs.
//
// Draw order (back to front): hiker or bike (bikeInFront says which), then
// splashDraw last. The bike goes under him when he rides (his legs and hands
// cover the frame and the grips); over him when it lies on top of him (C). The
// splash goes over both: it lays water over the wheels' lowest few px and over
// the sitting rider's hips, and sprays in front of everything.
//
// What the scene does each tick, in order: hikerTick, bikeStep (with an Rng that
// the effect owns, never the loop RNG: seed it from a copy at loop start),
// bikeApplyRider, mark dirty (bikeDirty, splashDirty and the hiker's bounds),
// draw. A scene that wants the bike at 12.5 Hz may pass twice the dt (up to 0.1 s).
//
// Scripting each variant: see the end of this header.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED
// until Mike has seen it (and on the panel).
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "hiker.h"
#include "landscape.h"

namespace ss {

// ---- size (PROPOSED; UNVERIFIED) -----------------------------------------------------------
//
// The brief's "about 46 px long" is read as the wheelbase, axle to axle. With
// 15 px wheels the whole bike is about 76 px from rear tyre to front tyre,
// which is what a 90 px rider needs (a 46 px bike would put his knees at his
// ears). Saddle 43 px above the ground, bars 47 px. Everything scales with the
// rider height given to bikeInit.
constexpr float kBikeWheelR = 15.f;
constexpr float kBikeWheelbase = 46.f;

// ---- the course ---------------------------------------------------------------------------------

#define BIKE_LANE_MAX 8   // 8: the midday lane follows the stream's far bank in four points (day_bike.cpp)

// Where the bike rides. The lane is the line the wheels touch, a polyline over
// x (increasing); beyond its ends it continues flat. It must start at or beyond
// x = -100 and end at or beyond x = 900 so the bike rides in from, and out to,
// off the frame. The water is the stretch of x where that line is inside the
// stream (bikeCourseFit works it out from the Landscape).
struct BikeCourse {
  int n;                           // lane points, 2..BIKE_LANE_MAX
  float x[BIKE_LANE_MAX];
  float y[BIKE_LANE_MAX];
  float water_x0, water_x1;        // wheels touching the lane here are in the water; x0 >= x1: no water
  float rock_x;                    // B: the rock's centre on the lane (in the water); 0: no rock
  float wheelie_x;                 // C: where the wheelie pops (dry ground)
};

// y of the lane at x.
float bikeLaneY(const BikeCourse& c, float x);

// Make a course from the lane points `pts` (n of them) for a ride in direction
// `dir` (+1 left to right, -1 right to left): finds the longest stretch of the
// lane inside the Landscape's stream (at least 2 px in from its edge), puts the
// rock 40% of the way along it in the direction of travel, and wheelie_x at x =
// 250 (dir +1) or 550 (dir -1).
void bikeCourseFit(BikeCourse& c, const Landscape& ls, const Vec2* pts, int n, int dir);

// ---- the bike -------------------------------------------------------------------------------------

enum BikeMode : uint8_t {
  BIKE_HIDDEN = 0,   // nothing drawn
  BIKE_RIDE,         // rolling along the lane (A, and the approach in B and C)
  BIKE_WHEELIE,      // C: the pop, the hold
  BIKE_TIP,          // C: tipping over backward
  BIKE_ENDO,         // B: stopped dead by the rock, noses over, falls on its side
  BIKE_DOWN,         // B, C: lying where it fell (the end)
  BIKE_AWAY,         // A: rode off the far edge; nothing drawn
  BIKE_MODES
};

enum BikeVariant : uint8_t { BIKE_VAR_PASS = 0, BIKE_VAR_ROCK, BIKE_VAR_WHEELIE, BIKE_VARIANTS };

// What the module is doing with the hiker.
enum RiderMode : uint8_t {
  RIDER_FREE = 0,   // the scene has him
  RIDER_SEAT,       // sitting on the saddle, hands on the bars, legs on the pedals
  RIDER_FLY,        // B: over the handlebars
  RIDER_SPLASH,     // B: into the water and up to sitting
  RIDER_FALL,       // C: sliding off the back onto the ground
  RIDER_DOWN,       // B: sitting in the water; C: lying on his back
  RIDER_MODES
};

// POD, 240 bytes. Lives by value in the World (and once more in
// WorldDirty for bikeDirty).
struct BikeState {
  uint8_t mode;        // BikeMode
  uint8_t variant;     // BikeVariant of the ride in progress
  uint8_t rider;       // RiderMode
  int8_t dir;          // +1 rides toward +x, -1 toward -x
  uint8_t proud;       // 1: upright, one arm in the air (A's way back)
  uint8_t flags;       // bit 0 rear wheel in the water, bit 1 front wheel, bit 2 rock hit, bit 3 splashed, bit 4 rock shown
  uint8_t pad[2];
  float t;             // seconds in this mode
  float rt;            // seconds in this rider mode
  float scale;         // rider height / 90
  BikeCourse course;
  Vec2 pivot;          // the rear tyre's contact point: the bike's frame of reference
  float pitch;         // turns, + nose up (a wheelie, the lane's slope), 0.5 upside down
  float lay;           // 0..1 fallen on its side (B)
  float speed;         // px/s along `dir`
  float wr, wf;        // rear, front wheel angle, turns
  float wrw, wfw;      // their rates, turns/s
  float crank;         // the cranks' angle, turns (0 = near crank up), turning with the rear wheel
  float rest_lift;     // px the bike rests above the ground, on top of him (C)
  float lane_pitch;    // the lane's own slope under the bike, turns, smoothed (the wheelie adds to it)
  float v0;            // C: the speed the wheelie began at
  Vec2 hip;            // the rider's hip when he is not on the saddle (world)
  Vec2 vel;            // and his velocity, px/s; once he is in the water, where he went in
  Vec2 mark;           // B: the front tyre's contact on the rock; C: the rear tyre's contact when the tip began
  float lean0;         // the lean he left the saddle with, radians
  HikerPose from;      // the pose he left the saddle in (RIDER_FLY, RIDER_FALL)
  float spawn[2];      // spray timers, rear and front wheel, s
  float ring[2];       // ripple timers
  float crown[2];      // crown timers
};

// POD for the water, 1632 bytes. A scene that keeps the splash after the
// bike is gone keeps calling bikeStep until splashActive() is false.
#define SPLASH_BITS 48
#define SPLASH_RINGS 8
#define SPLASH_CROWNS 4
#define SPLASH_COVERS 4

struct SplashBit {
  float x, y, vx, vy;
  float y_land;        // the row it comes down on
  float life;          // s until it lands, or until a fading one is gone
  uint8_t r8;          // radius, 1/8 px, at full size
  uint8_t kind;        // 0 free, 1 flies and lands in the water, 2 fades out in the air (it would land off the water)
  uint8_t pad[2];
};
struct SplashRing { float x, y, age, life; uint8_t r8; uint8_t on; uint8_t pad[2]; };   // r8: final half-width, 1/8 px
struct SplashCrown { float x, y, age, size; int8_t tilt; uint8_t on; uint8_t pad[2]; }; // tilt: lean, 1/64 turn
struct SplashCover { int16_t x, y, rx, ry; uint8_t on; uint8_t pad; };                  // a flat patch of stream water laid over the low part of a wheel or a sitting rider

struct SplashState {
  SplashBit bit[SPLASH_BITS];
  SplashRing ring[SPLASH_RINGS];
  SplashCrown crown[SPLASH_CROWNS];
  SplashCover cover[SPLASH_COVERS];
  uint32_t landed, faded;    // counts since bikeInit (the tests read them)
};

// A hidden bike for a hiker `rider_height` px tall (90). Zeroes the splash.
void bikeInit(BikeState& b, SplashState& sp, float rider_height);
void bikeSetCourse(BikeState& b, const BikeCourse& c);

// Start a ride in direction `dir`, variant `v`; `proud` for A's way back. The bike
// appears off the starting edge already rolling, the rider on the saddle. Any ride
// in progress is dropped; the splash carries on.
void bikeRide(BikeState& b, int dir, BikeVariant v, bool proud);

// One tick (SS_TICK_DT; any dt up to 0.1 s). `rng` is drawn from only when spray
// is thrown (3 values per droplet, 2 per crown, 1 per ring), so the same commands
// and the same Rng give the same bike and the same water, byte for byte.
void bikeStep(BikeState& b, SplashState& sp, Rng& rng, float dt);

// The scene takes the hiker back (after B or C ends, or to abandon a ride): the bike
// stays where it is, drawn, until bikeHide.
void bikeFreeRider(BikeState& b);
void bikeHide(BikeState& b);
// Show (or take away) the rock in the stream at course.rock_x, which bikeRide with
// BIKE_VAR_ROCK shows by itself. A scene that wants it already there when B begins
// (so it does not pop in) calls this some seconds earlier; bikeHide takes it away.
void bikeRock(BikeState& b, bool on);

inline bool bikeDriving(const BikeState& b) { return b.rider != RIDER_FREE; }
// True when the ride has reached its ending (A: AWAY; B, C: DOWN) and, for B and C,
// the settling is over (about a second after the fall).
bool bikeDone(const BikeState& b);
// True when the bike should be drawn after the hiker (it lies on top of him).
bool bikeInFront(const BikeState& b);

// Overwrite the hiker's pose, feet and facing with the riding, flying or fallen
// ones (see above). Returns false, touching nothing, when the module is not
// driving him. A pure function of the state: calling it twice does the same.
bool bikeApplyRider(const BikeState& b, Hiker& h);

// ---- bounds, dirty, draw ------------------------------------------------------------------------

// Everything the bike covers now (rock included), to its antialiased fringe.
// Empty when nothing is drawn. Not clamped to the frame.
Rect bikeBounds(const BikeState& b);
// True when the two states draw exactly the same pixels.
bool bikeSame(const BikeState& a, const BikeState& b);
// The areas to mark dirty when the bike goes from `before` to `after`: 0 when
// nothing visible changed, else one rect (old and new together) or two when the
// two positions do not touch. Covers every pixel that differs between the draws.
#define BIKE_DIRTY_MAX 2
int bikeDirty(const BikeState& before, const BikeState& after, Rect out[BIKE_DIRTY_MAX]);
// Colors: MUSHROOM for the frame, ACTOR for tyres, bars, saddle and cranks,
// ACTOR_LIGHT for spokes and hubs, ROCK for the rock (B only).
void bikeDraw(const BikeState& b, Surface& strip, const Rect& clip, const Palette& pal);

bool splashActive(const SplashState& sp);
Rect splashBounds(const SplashState& sp);
bool splashSame(const SplashState& a, const SplashState& b);
// As bikeDirty: where the droplets, rings, crowns and covers were and are. At most
// SPLASH_DIRTY_MAX rects (near ones are merged), covering every changed pixel.
#define SPLASH_DIRTY_MAX 6
int splashDirty(const SplashState& before, const SplashState& after, Rect out[SPLASH_DIRTY_MAX]);
// Colors: WATER for the covers, WATER_LIGHT for droplets, rings and crowns.
void splashDraw(const SplashState& sp, Surface& strip, const Rect& clip, const Palette& pal);

// ---- the hiker rig, as this module sees it (for the tests) ------------------------------------------
//
// The module keeps its own copy of the rig's lengths to place the feet. This returns
// where the rig will put the hand `which` (0 near, 1 far) and the head for a pose,
// feet and facing, the way hikerHandPos does, so a test can hold the copy to the
// rig. (hikerHandPos is the truth; the two must agree to a fraction of a px.)
Vec2 bikeRigHand(const Hiker& h, int which);
// Where the rig puts the hip joint in the frame (feet.y minus its grounding).
Vec2 bikeRigHip(const Hiker& h);

// ---- scripting the variants -------------------------------------------------------------------------------
//
//   Setup (once per loop): bikeInit(bike, splash, 90); bikeCourseFit(course, land, lane, n, dir);
//     bikeSetCourse(bike, course); an Rng for the effect, seeded from a copy of the loop RNG;
//     hiker.pack_on = false.
//   A:  the hiker stands, still. bikeRide(bike, +1, BIKE_VAR_PASS, false). He is on the bike at once
//       (bikeApplyRider moves him to the edge; the scene need not park him). Wait for
//       bike.mode == BIKE_AWAY (about 11 s on the test lane). A stillness (the test waits 1.5 s), then
//       bikeRide(bike, -1, BIKE_VAR_PASS, true) over the same course (the rock and wheelie are not
//       used by A); proud is slower, about 13 s. Wait for AWAY. Then bikeFreeRider(bike) and put the
//       hiker back where he was (feet, facing, hikerSetPose); the splash drains by itself (keep
//       calling bikeStep for about 2 s: splashActive says when it is done).
//   B:  bikeCourseFit with the ride's dir (it puts the rock 35% of the way through the water);
//       optionally bikeRock(bike, true) some seconds early so the rock is not new when he hits it;
//       bikeRide(bike, dir, BIKE_VAR_ROCK, false). The rock is hit about 4.3 s in on the test lane;
//       wait for BIKE_DOWN and bikeDone() (about 6.6 s from the start): he sits in the stream, the
//       bike lies on its side against the rock. The scene lets him sit a beat, then sets fx.wade
//       (World's waterline: the module's own water cover over his hips stops the moment he is
//       freed), bikeFreeRider(bike) and hikerSetPose(h, POSE_STAND, 1.2): he stands up, dripping.
//       His feet are where the module left them. The bike stays lying in the stream until bikeHide.
//   C:  courseC-style lane on dry ground (wheelie_x dry, no water needed); bikeRide(bike, dir,
//       BIKE_VAR_WHEELIE, false). The pop is at wheelie_x (2.7 s in for x = 170 on the test lane).
//       Wait for DOWN and bikeDone() (about 9.5 s from the start): he is on his back, the bike upside
//       down over him, the wheels spinning down over several seconds (a second after bikeDone they
//       turn at 0.5 turns/s). Hold the picture, then bikeFreeRider(bike), hikerSetPose(h, POSE_SIT,
//       1.0) and later POSE_STAND, and bikeHide(bike) when he has got up and the bike is to go (or
//       leave it lying: it is drawn until bikeHide).
//   Draw: if (bikeInFront(bike)) { hikerDraw; bikeDraw; } else { bikeDraw; hikerDraw; } then splashDraw.
//   Dirty: bikeDirty(prev, bike, ...) and splashDirty(prev_sp, splash, ...) every tick, besides the
//       hiker's bounds as always.
//   The lane is the scene's to choose. The stream runs from the right edge (y about 372) to the left
//       (y about 500), so a lane that crosses it left to right goes down the screen; the test lane
//       (-110,380) (250,392) (520,452) (920,470) is such a crossing and passes through the status
//       chip's corner (x 538..796, y 412..476) on its way off the right edge. A lane that keeps out
//       of the corner has to stay above y 412 for x > 538, which inside the stream means riding along
//       it rather than across it. UNVERIFIED which Mike prefers.

}  // namespace ss
