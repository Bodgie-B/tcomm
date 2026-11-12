// The visiting couple (stage 15; Mike, 11 Nov).
//
// Mike: "the occasional couple to pass by the camp, sit with the hiker around the fire and drink and
// laugh, play frisbee during the day, pull out telescopes and watch stars at night."
//
// Two hikers, Ann and Ben (the hiker rig's LOOK_ANN and LOOK_BEN: hiker.h), walk in together along
// the trail from the right with their packs, now and then: about one camp loop in seven (kChance,
// never two loops running: Memory::visit_wait), never in the Lookout or the sunrise, never on a rain,
// storm or snow loop, never while fire and ice or a doorbell reaction runs. What they do depends on
// the hour:
//   fireside  (the evening camp, the fire lit) they wave from the trail, he waves them over, they drop
//             their packs and sit by the fire, cups come out, they talk with small gestures, Ben tells
//             a story with his hands, all three lean back laughing, a toast, a while longer; then they
//             get up, wave and walk on
//   frisbee   (the morning and midday camp) the three spread out in the meadow and throw a disc round
//             a triangle with real arcs: a fumbled catch, a long run, a dive; if a house dog is out it
//             intercepts the disc once and is chased, then gives it back; then they wave and walk on
//   scopes    (the evening camp at night, the sky clear or part cloud) two telescopes on tripods,
//             turns at the eyepieces, Ann points up at the real moon (or the brightest star), the
//             hiker has a look and steps back impressed; on a moonless night a shooting star crosses
//             once and all three follow it with their heads; then they pack up and walk on
// An overcast night by the fire is a fireside visit.
//
// It takes his story over the way fire and ice does (fireice.h): it starts at a calm moment (he is in
// frame, standing, sitting or walking in his script, hands empty, no major, minor, story critter,
// weather gag, doorbell or fire and ice going, a minute or more of the loop left), the story holds
// still while they are here (the loop's clock waits; nothing is skipped), and at the end he walks
// back to where he was and the story carries on. So no loop's plan changes, and a loop without them
// is exactly what it was. The house dogs lie out of the way meanwhile (but the frisbee's dog), the
// mail bird stays.
//
// Memory: the couple lives in World's set-piece union (world.h) in the Lookout's place (a camp loop
// never uses it), so it adds nothing to the scene block. Its random stream is its own (from the loop
// seed): the loop rng never sees it. With Inputs::visitors off, or no visit this loop, nothing here
// runs, draws or marks anything.
// Tuning marked PROPOSED is a guess; how it looks is UNVERIFIED on the panel.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"
#include "hiker.h"

namespace ss {

struct World;
struct Renderer;
struct Memory;
struct Inputs;
struct SkyDraw;

enum VisitKind : uint8_t { VK_NONE = 0, VK_FIRESIDE, VK_FRISBEE, VK_SCOPES, VK_COUNT };

// One visitor: the rig and what lies over its pose this tick (as HikerFx does for him).
struct Visitor {
  Hiker h;
  float lean, head, lift;              // added to the composed pose
  float arm_w[2], arm_sh[2], arm_el[2];
  bool path_on;                        // feet.y follows (x0, y0)-(x1, y1) while walking
  float path_x0, path_y0, path_x1, path_y1;
  bool leg2;                           // a second leg to walk once this one arrives
  Vec2 leg2_to;
  float leg2_v;
  bool pack_down;                      // its pack on the ground at pack_at
  Vec2 pack_at;
  Rect rect;                           // what it covers this tick (clipped; empty when out of frame)
  Rect d_rect, d_pack;                 // dirty tracking
};

struct Visitors {
  Visitor v[2];                        // 0 Ann, 1 Ben
  // Every tick from the app (visitorsSetInputs).
  bool enabled;                        // Inputs::visitors
  bool night;                          // the scene's night (as the dogs read it)
  bool moon, dark;                     // the moon is drawn; no moon in the sky (more stars)
  int16_t moon_x, moon_y;
  uint8_t cover;                       // cloud cover in eighths
  // The loop.
  bool planned;                        // rolled at loop start: they may come this loop
  bool on;                             // they are here: his story holds still
  bool done;                           // they came and went this loop
  uint8_t kind;                        // VisitKind
  uint8_t stage;                       // visitors.cpp Stage
  uint8_t step;                        // the kind's own step
  uint8_t n;                           // its counter (throws, turns)
  float st;                            // seconds in the stage
  float ft;                            // seconds in the step
  float t;                             // seconds since they came
  uint32_t rng;
  // Where he was and how (put back exactly at the end), as fire and ice keeps it.
  Vec2 home;
  int8_t home_face;
  uint8_t home_pose;
  bool home_path;
  float home_path_v[4];
  float home_reach_to;
  bool home_walk;
  float home_walk_x, home_walk_v;
  bool hero_leg2;                      // his second leg (round the fire)
  Vec2 hero_leg2_to;
  float hero_leg2_v;
  Vec2 spot[3];                        // the places this visit uses: 0 his, 1 Ann's, 2 Ben's
  // The frisbee.
  uint8_t disc_mode;                   // visitors.cpp DiscMode
  uint8_t holder;                      // who has it (0 him, 1 Ann, 2 Ben, 3 the dog)
  uint8_t catcher;
  Vec2 disc, disc_a, disc_b;           // where it is; a flight's start and end
  float disc_t, disc_dur, disc_arc;
  int8_t dog;                          // the lent house dog's critter slot, -1 none
  // The telescopes.
  Vec2 scope[2];                       // where each stands
  float scope_k[2];                    // 0 packed .. 1 set up
  Vec2 aim;                            // what they look at: the moon or the brightest star
  // The shooting star.
  float meteor_t;                      // seconds into it, < 0 none
  bool meteor_done;
  Vec2 meteor_a, meteor_b;
  // Dirty tracking.
  Rect d_disc, d_scope[2], d_meteor;
};

// Loop start, after the weather is planned (app.cpp beginLoop): counts Memory::visit_wait down and,
// in a camp loop with them enabled and dry weather, rolls whether they may come.
void visitorsBeginLoop(World& w, uint64_t loop_seed, const Inputs& in);
// The roll itself (tests): whether a loop with this seed would have them, wait aside.
bool visitorsRoll(uint64_t loop_seed);
// A visit began: none in the next loop (visitorsBegin calls it; tests too).
void visitorsMarkVisited(Memory& m);
// Every tick before worldTick (app.cpp): the flag, the scene's night and the sky (the moon's place).
void visitorsSetInputs(World& w, bool enabled, bool night, const SkyDraw& sky);
// They should come now (a calm moment; see the top).
bool visitorsCanStart(const World& w);
void visitorsBegin(World& w);
// The story holds still while this is true.
bool visitorsHolds(const World& w);
// One tick while they are here (worldTick, in place of the story's critters and minors).
void visitorsStep(World& w);
// The visit's step by name, for tests, stills and logs ("" when none).
const char* visitorsStepName(const World& w);
// Dirty marks (worldTick, last).
void visitorsTrack(World& w, Renderer& r);
// The actors it adds to the sort (VIS_ACTORS): 0, 1 Ann and Ben; 2, 3 their packs on the ground;
// 4 the disc; 5, 6 the telescopes. True, with the base y, when that one is drawn now.
#define VIS_ACTORS 7
bool visitorsActorY(const World& w, int idx, int16_t& y);
void visitorsDrawActor(const World& w, int idx, Surface& s, const Rect& clip, const Palette& pal);
// What visitor k covers now (for the foreground pines drawn over it); empty when not shown.
Rect visitorsRect(const World& w, int k);
// The shooting star, with the sky's things (after the clouds).
void visitorsDrawSky(const World& w, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
