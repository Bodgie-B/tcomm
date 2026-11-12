// Fire and ice (stage 14, Mike 11 Nov): the downstairs tenant's temperature arrows played out in camp.
//
// Mike: "If the downstairs indicates too cold or too hot then the scene shows ice or fire build up
// and the hiker has to douse the fires with buckets from the stream, or build a big fire to stave
// off the ice forming all over the ground."
//
// Inputs::temp_ask holds the arrows (both panels; the firmware's Ask: 1 urgent heat, 2 mild heat,
// 3 mild cooling, 4 urgent cooling; predicted ones are left out). Cooling wanted means too hot:
// fires. Heat wanted means too cold: ice. With both kinds showing, the newest arrow decides.
//
//   too hot   small fires start round the camp (the grass, the wood pile, near the tent), more and
//             bigger with more arrows and with urgent ones. He takes a bucket from his pack, runs to
//             the stream, fills it, runs back and douses one (a puff of steam), and again. Mild: he
//             keeps about even. Urgent: they start faster than he can douse them, the light goes a
//             little warmer and he runs flat out, arms going.
//   too cold  frost and ice creep in over the ground from the edges, the stream's edges freeze,
//             icicles hang from the tent and the camp pine. He hauls wood and builds the campfire up
//             into a big blaze; a warm circle round it holds the ice back. Mild: the circle holds
//             most of the camp. Urgent: the ice presses in to a small circle, he huddles by the fire
//             between armloads, breath showing.
//   cleared   the fires die down and he douses the last of them, sets the bucket down, wipes his
//             brow and puts it away; or the ice melts back to the edges and the fire settles to what
//             it was. Then he goes back to where he was and the story carries on. Nothing is left
//             behind: no scorch, no ice, no bucket.
//
// It takes over his story like the doorbell does (react.h): the story holds still while it runs (no
// beat, script, minor or story critter advances; the loop's clock waits), so it all resumes exactly
// where it stopped. It begins only at a calm moment: he is in frame, standing, sitting or walking in
// his script (a walk carries on afterwards), hands empty, no major, minor, story critter, weather gag
// or doorbell going. The ground, the ice
// and the fires only start when he does. While it runs the house dogs lie out of the way (by the
// tent's back corner, which the fires keep clear of; by the fire in the cold, inside the warm
// circle) and the mail bird stays where it is.
//
// The Lookout: nothing (the cheapest safe thing: the ledge has no camp, no stream and no fire pit).
// The arrows take over the next camp loop.
//
// Drawing. The ice is in the cached background: worldDrawStatic frosts the ground's own colours
// (ground, grass, rock, and the stream's edges) outside the warm circle, and icicles on the camp
// pine. The circle's size moves in steps (kIceStep); each step repaints only the cache rows the
// circle covers (Renderer::repaintRows) and composites those strips. The fires, the bucket, the
// splash, the steam, the breath and the tent's icicles are small dynamic actors. With no arrows none
// of this runs, draws or marks anything: the per-frame cost is one test.
//
// Tuning marked PROPOSED is a guess; how it looks is UNVERIFIED on the panel.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"
#include "props.h"

namespace ss {

struct World;
struct Renderer;

#define FI_FIRES 6

enum FireIceMode : uint8_t { FI_NONE = 0, FI_HOT, FI_COLD };

// One small fire on the ground. k: its size, 0 out .. 1 a good blaze (urgent ones grow to 1.35).
struct FiFlame {
  int16_t x, y;
  float k;
};

// A rectangle in 16-bit ints (the previous bounds kept for dirty marks: half a Rect's size).
struct FiBox {
  int16_t x0, y0, x1, y1;
};

struct FireIce {
  // What the arrows ask for (fireIceSetInputs, every tick).
  uint8_t want;      // FireIceMode
  uint8_t level;     // arrows of the wanted kind: mild 1, urgent 2 each (1..6)
  bool urgent;       // any urgent one among them

  bool on;           // he is ours: the story holds still
  uint8_t mode;      // what the scene shows now (FireIceMode)
  uint8_t stage;     // his step (fireice.cpp Stage)
  bool ending;       // the arrows went (or changed kind): wind this mode down
  float st;          // seconds in the stage
  float t;           // seconds this mode has run
  float spawn;       // too hot: seconds to the next fire
  float feed_t;      // too cold: seconds since the last armload went on (the flare)
  uint32_t rng;      // its own stream (from the loop seed and the tick it began)

  // Where he was and how (put back exactly at the end).
  Vec2 home;
  int8_t home_face;
  uint8_t home_pose;
  bool home_path;
  float home_path_v[4];
  float home_reach_to;
  bool home_walk;    // he was on his way somewhere: he sets off again at the end
  float home_walk_x, home_walk_v;
  // The fire pit as it was.
  uint8_t pit_state;
  float pit_fuel;
  float pit_smoke;

  // The walk he is on: `via` first when the straight line would cross the fire pit.
  Vec2 go, via;
  bool use_via;
  float speed;

  // Too hot.
  FiFlame f[FI_FIRES];
  int8_t target;     // the fire he is going for, -1 none
  float water;       // 0..1 in the bucket
  uint8_t bucket;    // 0 none, 1 in his hand, 2 set down on the ground at bucket_at
  Vec2 bucket_at;
  float tilt;        // the pour, radians
  Vec2 splash_a, splash_b;   // the pour's water, from the bucket's mouth to the fire
  float splash_t;    // seconds into the splash, < 0 none
  Steam steam;       // the puff off a doused fire

  // Too cold.
  float ice;         // the warm circle's size (kIceNone and up: no ice anywhere)
  uint8_t ice_q;     // the step the cache is drawn with (kIceNoneQ: none)
  uint8_t pine_q;    // icicles on the camp pine in the cache: 0 none, 1 short, 2 long
  uint8_t tent_q;    // on the tent (dynamic): the same
  float breath_t;    // seconds into a breath puff, < 0 none

  // Urgent heat: the light a little warmer, in steps (each a palette rebuild).
  float warm;
  uint8_t warm_q;

  // Dirty tracking.
  FiBox d_f[FI_FIRES];
  FiBox d_near, d_bucket;
};

// The arrows as they are this tick (app.cpp, every tick): Inputs::temp_ask.
void fireIceSetInputs(World& w, const uint8_t ask[3]);
// His step now, by name ("douse", "fill", "warm", ...; "" when idle), for tests, stills and logs.
const char* fireIceStageName(const World& w);
// It should start now: arrows showing, not running yet, a camp loop, and a calm moment (see the top).
bool fireIceCanStart(const World& w);
// The calm moment itself (the visitors start at one too): a camp loop, the camp built, him in frame,
// standing, sitting or walking in his script with empty hands, nothing else going on.
bool fireIceCalm(const World& w);
// Takes him over; `seed` starts its own random stream.
void fireIceBegin(World& w, uint64_t seed);
// The story holds still while this is true (app.cpp).
bool fireIceHolds(const World& w);
// How much farther back (px) than by a good fire he and the dogs keep from the campfire now: the
// cold's big blaze, 0 otherwise.
float fireIceClearance(const World& w);
// One tick while it runs (worldTick, in place of the story's critters and minors).
void fireIceStep(World& w);
// The doorbell and the passer-by wave may interrupt it now: a mild spell, him standing still with
// nothing in his hands.
bool fireIceWaveOk(const World& w);

// Its bucket in his hand, for his bounds (World's coverHiker); empty when he has none.
Rect fireIceHeldBounds(const World& w);
// Dirty marks for everything it draws, and the cache repaints for the ice (worldTick, last).
void fireIceTrack(World& w, Renderer& r);
// The palette's warm step for urgent heat (0 none .. kWarmSteps) and the tweak itself (app.cpp).
int fireIceWarmStep(const World& w);
void fireIcePaletteTweak(int warm_step, Palette& p, bool night);

// Drawing. The frost and the pine's icicles over the cached landscape (worldDrawStatic, after the
// landscape: `frame` may be a band of rows); a fire or the set-down bucket (ACT_FX, sorted among the
// ground actors); the bucket in his hand (after him); the tent's icicles (after the tent); the
// splash, the steam and the breath in front of everything (after the smoke).
void fireIceDrawStatic(const World& w, Surface& frame, const Palette& pal);
// The cold's icicles: `which` bit 0 the camp pine's, bit 1 and bit 2 the foreground pines' (fg[0],
// fg[1]). In the cache with the ice; drawn again wherever a pine is redrawn over something.
void fireIceDrawIcicles(const World& w, Surface& s, const Rect& clip, const Palette& pal, unsigned which);
void fireIceDrawActor(const World& w, int idx, Surface& s, const Rect& clip, const Palette& pal);
void fireIceDrawHeld(const World& w, Surface& s, const Rect& clip, const Palette& pal);
void fireIceDrawTentIce(const World& w, Surface& s, const Rect& clip, const Palette& pal);
void fireIceDrawNear(const World& w, Surface& s, const Rect& clip, const Palette& pal);
// The actors it adds to the sort (FI_ACTORS of them): idx 0..FI_FIRES-1 a fire, FI_FIRES the
// set-down bucket. True, with the base y to sort by, when that one is drawn now.
#define FI_ACTORS (FI_FIRES + 1)
bool fireIceActorY(const World& w, int idx, int16_t& y);

}  // namespace ss
