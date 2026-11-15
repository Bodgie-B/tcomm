// Life in the stream (stage 17): the boulder in the current, things drifting by, fish.
//
// Passive and always on in the camp families (evening, morning, midday), never in the Lookout or the
// sunrise. No story beats: nothing here moves the hiker or a critter, and the loop RNG is never drawn
// from (the stream has its own Rng, seeded from a copy of the loop RNG at loop start).
//
// - The boulder: one mossy rock a little off centre in the current, upstream of everything the story
//   does in the water (landscape.h LS_BOULDER_*), with three small stones by the near bank. Rock,
//   stones, its reflection, the calm water behind it and the pillow in front sit in the static cache
//   (streamDrawStatic, called from landscape.cpp's layers). Per frame only a little foam on its
//   upstream face and a V of lighter ripples trailing downstream move (slowly, in steps).
// - Floaters: a leaf, a stick that turns slowly or a pine cone every 20 to 40 s on average, riding
//   the current on the stream's own path and bobbing a little. The water parts round the boulder for
//   them (and for the ambient glints, ambient.cpp); some snag on its upstream face for a few seconds,
//   then swing free.
// - Fish: every few minutes one jumps in an arc (a fish about 11 px long, a pale belly, a forked tail,
//   a glint) and splashes back in with rings; more often a rise ring with no fish. Both about twice as
//   often at dawn and dusk.
// - Visitors (stage 17, round 2), rare and passive, never both in one loop: a heron that flies in,
//   stands in the shallows on the near side for 30 to 90 s (a slow step, a look back, now and then a
//   strike at the water) and flies off; or a deer (critters.cpp CR_DEER, in a story slot tagged
//   CT_STREAM) that walks in along the camp-side bank, drinks with rings at its muzzle, looks round,
//   drinks again and walks off. Mostly at dawn and dusk (the heron in the morning too), never at full
//   night or in rain. They come only in a quiet moment (a pause or a calm filler, no major, no minor
//   running, no other story critter, nobody near the spot) and leave at once, calmly, when a beat that
//   owns the critters or uses the water begins, fire and ice or the visiting couple start, or the loop
//   nears its end; in a hurry when the hiker or a dog comes close. Their own Rng (vrng), so a visit
//   leaves the floaters and fish as they were.
// - Weather: in rain the water runs faster (glints and floaters), a little browner (palette.cpp under
//   the gloom) and carries more debris, more of it sticks. At night with the moon up, a glint of
//   moonlight on the boulder's wake.
//
// Leaves follow Inputs::month (ss_app.h): spring and summer a few green ones, early autumn a mix, mid
// and late autumn many orange, russet and gold, winter a very few brown. Month 0 (no clock) keeps the
// year-round mix of autumn colours and green. There is no outside temperature, so no freeze variant.
//
// Memory: StreamLife shares the Lookout's running state (world.h, a union with the Christmas mockups):
// nothing here runs, marks or draws in a Lookout or sunrise loop.
//
// Dirty strips: each floater and the fish mark their old and new bounds every tick while they move;
// the foam and wake mark theirs only when their step changes (every kFoamStepTicks); each reports
// empty bounds once when it goes. The boulder is a ground actor at its waterline (ACT_STREAM): drawn
// there, it paints the rock again over anything farther away that crossed it this frame (a glint, a
// floater, the fishing log, a towed hiker), then the snagged floater, the foam and the moon glint.
#pragma once

#include "../anim/rng.h"
#include "../gfx/surface.h"
#include "../palette.h"
#include "../render/renderer.h"

namespace ss {

struct World;
struct Landscape;

#define STREAM_FLOATERS 2

enum FloatKind : uint8_t { FK_NONE = 0, FK_LEAF, FK_STICK, FK_CONE };
enum FloatPhase : uint8_t { FP_DRIFT = 0, FP_HELD, FP_SWING, FP_FREE };

// One thing on the water. Position in the stream's own terms: `s` along it in point-index units
// (landscape.h LsStreamPt, 0 off the right edge), `lane` across it as a fraction of the half width
// (+ toward the camp bank). `ang` is its heading on the water in turns (0 along the current); it is
// drawn foreshortened, so a stick across the current looks short.
struct Floater {
  uint8_t kind;      // FloatKind
  uint8_t phase;     // FloatPhase
  uint8_t tint;      // a leaf's colour 0..3; a stick's twig (bit 0)
  int8_t side;       // which side of the boulder it swings free on (+1 camp, -1 near)
  float s, lane;
  float ang, spin;   // turns, turns per second
  float len;         // px: a stick's length, a leaf's
  float t;           // seconds afloat (the bob's clock); from 0 again when it snags and when it swings free
  float hold;        // seconds it stays snagged (0: it never snags)
};

enum FishKind : uint8_t { FISH_NONE = 0, FISH_JUMP, FISH_RISE };

struct StreamFish {
  uint8_t kind;      // FishKind
  int8_t dir;        // +1 jumps downstream (left), -1 upstream
  int16_t x, y;      // where it leaves the water (frame px)
  float t;           // seconds since it began
};

// A posed visitor for review stills only (never scheduled): stream_stills.cpp sets `mock`. The duck
// and the dragonfly stay mockups; the heron's mock is the old posed one (the live heron is StreamVisit).
enum StreamMock : uint8_t { SM_NONE = 0, SM_DUCK, SM_HERON, SM_DRAGONFLY };

enum GuestKind : uint8_t { SV_NONE = 0, SV_HERON, SV_DEER };
enum GuestStage : uint8_t { GS_WAIT = 0, GS_IN, GS_STAY, GS_OUT, GS_DONE };

// The loop's visitor (at most one). Rolled once, at the first tick after the build, from vrng.
struct StreamVisit {
  uint8_t kind;      // GuestKind: what may come this loop
  uint8_t stage;     // GuestStage
  uint8_t act;       // the heron's small action now; the deer's step
  uint8_t planned;   // the roll is done
  uint8_t force;     // stills and tests: this GuestKind, for sure and soon (set before the build ends)
  uint8_t spooked;   // left in a hurry
  int8_t dir;        // which way it faces (+1 right)
  int8_t slot;       // the deer's critter slot, -1 none
  uint32_t at;       // GS_WAIT: storyTick it may come from; it gives up kVisitWindow later
  float t;           // seconds in the stage
  float act_t, act_len;   // the small action's clock and length, seconds
  float stay;        // seconds it stays
  Vec2 spot;         // where it stands: the heron's feet, the deer's feet while it drinks
  Vec2 from;         // the heron's flight: where it started (in) or took off (out)
  Vec2 to;           // and where it goes (out)
  int16_t ring_x, ring_y;   // a ring on the water (the heron's strike, the deer's drinking)
  float ring_t;      // its age, seconds; < 0 none
};

struct StreamLife {
  Rng rng;                              // the stream's own stream (see top)
  Rng vrng;                             // the visitors' own
  Floater f[STREAM_FLOATERS];
  StreamFish fish;
  uint32_t next_float, next_fish;      // World::tick of the next floater and the next fish event
  uint16_t step;                       // the foam and wake's animation step (World::tick / kFoamStepTicks)
  uint8_t moon;                        // the moon glint shows (streamSetInputs), and its flicker step + 1
  bool moon_up, twilight;              // inputs: the moon is up at night; dawn or dusk
  bool night;                          // input: full night (no visitors)
  uint8_t month;                       // input: Inputs::month, 1-12, 0 unknown
  uint8_t mock;                        // StreamMock (stills only; it moves on World::tick)
  StreamVisit visit;
  // Dirty tracking: last tick's bounds and what the foam and the mock were drawn with.
  Rect d_f[STREAM_FLOATERS], d_fish, d_foam, d_mock, d_visit;
  uint16_t d_step;
  uint8_t d_moon, d_mock_k;
};

// Loop start (worldBeginLoop): clears the state and seeds the stream's Rng from a copy of the loop
// RNG (which is not advanced). Schedules the first floater and fish.
void streamInit(World& w, const Rng& loop_rng);
// The app's inputs, every tick, camp loops only: the moon is up in a night sky (and not hidden by a
// cloud deck), it is dawn or dusk (fish rise more, visitors come), full night (no visitors), and the
// month (Inputs::month: the leaves).
void streamSetInputs(World& w, bool moon_up, bool twilight, bool night, uint8_t month);
// One tick (camp families only; also during the doorbell and fire and ice: it is ambient).
void streamStep(World& w);
// Marks what changed (after streamStep).
void streamTrack(World& w, Renderer& r);

// How the water parts round the boulder: `lane` at `s` pushed aside to pass it (no change away from
// it). `margin` widens the boulder by that many px (a floater's own half width).
float streamPartLane(const Landscape& ls, float s, float lane, float margin);
// Glint and floater speed factor: 1 dry, up to 1.6 in heavy rain.
float streamFlowMul(const World& w);

// The boulder as a ground actor: true with its sort y (the waterline at its foot) once the build is done.
bool streamActorY(const World& w, int16_t& y);
// A visitor a house dog would go for (housedogs.cpp HA_CHASE): the heron landing or standing in the
// shallows, the deer in sight on the bank, not already leaving; `at` where it is.
bool streamGuestAt(const World& w, Vec2& at);
// The heron as a ground actor (ACT_STREAM idx 1): its feet y standing, the front while it flies.
bool streamVisitY(const World& w, int16_t& y);

// The static parts, from landscape.cpp's layers: the water's marks (reflection, the calm behind it,
// the pillow in front) with the stream layer, `scale` 1 drawn; the rock and the small stones with the
// rocks, `scale` their build pop.
void streamDrawStaticWater(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip);
void streamDrawStaticRock(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, float scale);

// Per strip: the wake, the rings and the fish, the floaters (worldDrawDynamic step 4, on the water).
void streamDrawWater(const World& w, Surface& s, const Rect& clip, const Palette& pal);
// Per strip, as the ACT_STREAM actor: the rock over what crossed behind it, the snagged floater, the
// foam, the moon glint, a mock visitor.
void streamDrawNear(const World& w, Surface& s, const Rect& clip, const Palette& pal);
// The heron (ACT_STREAM idx 1).
void streamDrawVisit(const World& w, Surface& s, const Rect& clip, const Palette& pal);

// Bounds, frame space; empty when not shown. For tests.
Rect streamFloaterBounds(const World& w, int i);
Rect streamFishBounds(const World& w);
Rect streamFoamBounds(const World& w);   // foam, wake and moon glint
Rect streamRockBounds(const Landscape& ls);
Rect streamMockBounds(const World& w);
// The heron and the visitor's ring on the water (the deer is a critter: World::critter_rect).
Rect streamVisitBounds(const World& w);
// Test hook: draw floater i alone (as streamDrawWater or streamDrawNear would).
void streamDrawFloater(const World& w, int i, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
