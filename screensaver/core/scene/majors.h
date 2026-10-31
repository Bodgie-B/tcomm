// Surprise majors: the memorable moments (brief sections 4, 5, 8 and 13).
//
// 2 to 4 per loop, drawn by majorsPlan from a weighted pool with a ring buffer
// of recent picks (Memory::ring), gated by mood and by where they can go:
//
//   slot after setup, fire, dinner: the hiker is outside (most of the pool)
//   slot after foodbag:             he is in the lit tent (shadow_theater, raccoon_pack asleep)
//
// One major per slot, each wrapped in a 4 to 8 s "still" beat before and after
// (spineBuildTimeline). A major is a script for the hiker (script.h) plus a
// tick that drives up to 3 critters through the director and a few glyphs:
// small drawn things only majors need (ripple rings, dots, shapes, sound arcs,
// an ant line). Majors start wherever he was left (sitting by the fire, or
// standing after setup, or in the tent) and leave him there: outside ones end
// sitting at camp.sit facing the fire, tent ones with him in the lit tent.
//
// While a major runs World::busy is set (as it is for dinner, bear and close),
// so the minors defer. A major that ends with its script unfinished or one of
// its critters still in the scene counts a spine overrun.
#pragma once

#include "../anim/rng.h"
#include "../anim/scheduler.h"
#include "../gfx/raster.h"
#include "../palette.h"

namespace ss {

struct World;
struct LoopPlan;
struct Inputs;
struct Memory;
struct Renderer;
struct SceneCtx;

enum MajorId : uint8_t {
  MJ_SQUIRREL_SPLASH = 0, MJ_DEER_STICK, MJ_PINECONE, MJ_MAP_CHASE, MJ_FISH_LEAP, MJ_RACCOON_PACK,
  MJ_ANTS_COOKIE, MJ_SHADOW_THEATER, MJ_RANGER_RIDGE, MJ_ECHO, MJ_FIREFLY_JAR, MJ_STARGAZE,
  // The stream vignettes (Mike's idea): he fishes (three ways) or bathes.
  MJ_FISHING_SMALL, MJ_FISHING_LOG, MJ_FISHING_GOOD, MJ_BATHE,
  // The storm's (weather.h): only when a storm comes from the OUTSIDE data.
  MJ_RAIN_SHELTER,
  MJ_COUNT
};
// Memory keeps a loop's majors as a 16-bit set (memory.h sets, spine.cpp):
// ids 0..15 fit. rain_shelter (16) falls outside it, so the recent-set rule
// does not see it (it is gated by the weather, not by variety); widening the
// sets to 32 bits is an ask for memory.h's owner.
static_assert(MJ_COUNT <= 32, "past 32 majors, Memory's sets must widen");

// ---- glyphs -----------------------------------------------------------------------

enum GlyphKind : uint8_t {
  GL_NONE = 0,
  GL_RING,    // a ripple ring on the water: `t` seconds old, `scale` size
  GL_DOT,     // a dot of palette `slot`, radius `scale` px (fireflies, stars, lanterns, drops)
  GL_SHAPE,   // shape `shape` at pos, turned `rot` turns, `scale`, phase `t`, extra `k`
  GL_ARCS,    // 3 nested sound arcs facing `rot` turns, radius `scale`, thickness `k`
  GL_ANTS,    // an ant line from pos (the nest) to q (the cookie); see majors.cpp
  GL_WATER    // the stream's water redrawn over the rect pos..q (a bird sitting low on the
              // water); World draws it (worldDrawWater), sorted at base_y
};

enum ShapeId : uint8_t {
  SH_TWIG = 0,   // k 0 whole, 1 snapped
  SH_MAP,        // k 0 folded .. 1 open; t tumble phase
  SH_FISH,       // rot along its flight
  SH_COOKIE,     // scale = radius
  SH_RANGER,     // the tiny figure on the ridge
  SH_CONE,       // a pine cone (TRUNK); t its tumble
  SH_RABBIT, SH_BIRD, SH_DOG, SH_BEAR,   // hand shadows on the lit tent; t the wiggle phase
  SH_ROD,        // the fishing rod tumbling through the air (or the stick); k 1: the stick
  SH_LOG,        // a log afloat (TRUNK), rot along the stream
  SH_DEADFISH,   // a small fish floating belly-up; scale grows from 0 as it surfaces
  SH_COUNT
};

struct Glyph {
  uint8_t kind;     // GlyphKind
  uint8_t shape;    // ShapeId
  uint8_t slot;     // palette Slot (dots and shapes)
  int8_t facing;
  uint8_t n;        // ants: how many
  bool keep;        // stays after its major ends (the counted stars)
  Vec2 pos;
  Vec2 q;           // second point (ants: the cookie; fireflies: home)
  float rot, scale, t, k;
  int16_t base_y;   // where it sorts among the ground actors
};

#define GLYPH_MAX 28   // the counted stars a stargaze keeps, and a firefly_jar's 20 after it

// The running major: its points (the AT_M0..AT_M3 anchors), critter slots,
// stage machines, cues between the script and the tick, and its glyphs with
// their dirty tracking.
struct MajorState {
  bool on;
  uint8_t id, var;
  Vec2 p[4];
  int8_t c[3];        // critter slots, -1 none
  uint8_t stage[3];
  float st[3];        // seconds in each critter's stage
  uint16_t cue;       // bits the script sets and the tick reads (and back)
  float t;            // seconds into the major
  float tc;           // the time a cue fired (the tick's own clock for a sequence)
  float u0, u1;       // scratch
  uint8_t count;      // scratch count
  uint8_t mode;       // what the fishing line (or a filler's birds) is doing now
  float mt;           // seconds in that mode
  Vec2 v0;            // scratch point
  bool stick;         // fishing: a stick for a rod (the rod was lost last loop)
  Glyph g[GLYPH_MAX];
  Glyph g_s[GLYPH_MAX];   // last tick's glyphs, and their bounds
  Rect g_r[GLYPH_MAX];
};

// ---- planning -----------------------------------------------------------------------

const char* majorName(int id);
// Seconds a major's beat lasts with variant `var`.
int majorSeconds(int id, int var);
// Where a family's majors can go: up to 4 slots, the spine beat each one
// follows (LoopPlan::major_after), and which are open (bit k: slot k). The
// pool's slot bits (majors.cpp kMajors) are slot indices, so they mean the
// evening's setup / fire / dinner / foodbag, or the morning's wake / coffee /
// bag_down; a family's mask (kMajors families) says which majors it plays.
struct MajorSlots {
  uint8_t family;    // SpineFamily
  uint8_t after[4];
  uint8_t open;
};
// Pick this loop's majors (2..4) and their slots, variants and stillness into
// the plan, and push them into the memory's ring. Called by spinePlan before
// the loop-length trim (which may drop the last ones; see spinePlan).
void majorsPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem, const MajorSlots& slots);
// The beat for the plan's major i.
Beat majorBeat(const LoopPlan& p, int i);

// ---- per tick, from World ------------------------------------------------------------

Rect glyphBounds(const Glyph& g);
void glyphDraw(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal);
// Mark where glyphs were and are (after the beat moved them).
void majorsTrack(MajorState& m, Renderer& r);
// Clear every glyph (loop start: they never cross a loop).
void majorsReset(MajorState& m);

// ---- the birds_on_water filler ------------------------------------------------------
// A filler (spine.h FILL_BIRDS), not a major: it does not set World::busy,
// but its name is not one the minors count as calm, so they stay away. It
// borrows MajorState for its birds (critters tagged CT_MAJOR), rings and the
// water patches that sit them low on the water. Its beat hooks:
void birdsFillerBegin(SceneCtx& ctx);
void birdsFillerUpdate(SceneCtx& ctx, float t, float dt);
void birdsFillerEnd(SceneCtx& ctx);
// The filler's script and the seconds it lasts.
int birdsFillerSeconds();

}  // namespace ss
