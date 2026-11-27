// The Christmas season in camp (stage 18's drawings, scheduled: Mike, 2026-11-26).
//
// The season: Nov 26 through Jan 1 (Inputs::month and Inputs::mday, the panel's local date; no clock, no
// Christmas). The plan carries it in LoopPlan::xmas (xmasPlanByte). Outside it nothing here draws, marks
// a strip, adds an actor, a beat or a plan draw, so every plan and frame is what it would be without it.
//
// In season the camp pine is decorated whenever the tent is up, in every camp family (not the Lookout or
// the sunrise): the evening from the tent's pitching (the beat after setup), the morning until break camp
// packs the tent, midday all loop. Multicolour lights on five wraps, gold garland, seven ornaments, the
// star from Dec 11, presents under it (more each week, the full pile with Lucy's and Copper's bones on
// Dec 25), the stocking on the tent's pole. The lights burn only while the little water wheel generator
// turns in the stream by the boulder (wire to the pine), with sparks off its box now and then.
//
// The story (xmas_story.cpp): twice an evening (once early, once late; Memory::taichi_next bits 4 and 5,
// carried between runs) the "decorate" beat: he fetches the generator, sets it in the stream, lifts the
// ladder, strings the lights wrap by wrap, two cardinals fly the garland in (a tangle, then draped), the
// ornaments, the star (from Dec 11), the ladder down, the wheel lowered and the lights on. In every other
// evening loop "lights_on": he lowers the wheel and the lights come on. The morning's "lights_off" before
// break camp. The major "bauble_thief": a raccoon takes the low ornament to its bush by the stream; the
// bush keeps count over the season (Memory::xmas_bush).
//
// Memory: XmasState shares the Lookout's running state (world.h, a union): a camp loop never touches
// LookoutState and a set-piece loop never draws or tracks this, so it costs no App bytes.
//
// Dirty strips: four groups (the tree's pieces, the generator, the critters and garland in flight with the
// coil's run to his hand, the extras), each marked where it was and where it is whenever the state
// changes; the generator's every tick while he carries it or the wheel turns, the critters' while the
// run of lights hangs from his hand or the raccoon carries the ornament.
#pragma once

#include "xmas_switch.h"

#include <stdint.h>

namespace ss {

// The season's day from the local date: Nov 26 is 0, Dec 1 is 5, Dec 11 is 15, Dec 25 is 29, Jan 1 is
// 36; -1 outside the season or with no date (month or mday 0). Compiled in either way (no state).
int xmasSeasonDay(uint8_t month, uint8_t mday);
// LoopPlan::xmas: 0 outside the season (or a set piece, or SS_XMAS 0); else bit 7 set, bits 0-5 the season
// day, bit 6 this evening loop decorates.
constexpr uint8_t XMAS_PLAN_ON = 0x80, XMAS_PLAN_DECK = 0x40, XMAS_PLAN_DAY = 0x3F;
constexpr int XMAS_STAR_DAY = 15;      // Dec 11: the star goes on
constexpr int XMAS_DAY_CHRISTMAS = 29; // Dec 25: the whole pile of presents
// Memory::taichi_next bits (carried between runs in Inputs::carry's high byte): this evening's early and
// late decorating have played. Cleared by a morning or midday loop, and by the panel's host at noon.
constexpr uint8_t XMAS_DECK_EARLY = 0x10, XMAS_DECK_LATE = 0x20;

}  // namespace ss

#if SS_XMAS

#include "../gfx/surface.h"
#include "../palette.h"
#include "../render/renderer.h"
#include "../anim/scheduler.h"
#include "landscape.h"

namespace ss {

struct World;
struct LoopPlan;
struct Inputs;
struct Memory;
struct Step;

#define XMAS_WRAPS 5        // light strings: one round each tier, top down
#define XMAS_ORNS 7         // ornament places on the tree (bit i of XmasMock::orns)
#define XMAS_LOW_ORN 6      // the low one at the bottom tier's left tip: the raccoon's
#define XMAS_ACTORS 7       // ACT_XMAS idx: 0 box, 1 wheel, 2 presents, 3 the tent's (stocking, snow),
                            // 4 critters, 5 bush, 6 the generator in his arms (the mocks' boulder cap)
#define XMAS_GIFTS_ALL 7    // XmasMock::gifts: boxes shown (1..6); 7 all six and the two bones
#define XMAS_BUSH_MAX 6     // ornaments the bush can hold

enum XmasPower : uint8_t { XP_OFF = 0, XP_ON, XP_HALF_DEAD, XP_DIM };
enum XmasGarland : uint8_t { XG_NONE = 0, XG_FLYING, XG_TANGLED, XG_PART, XG_DRAPED, XG_LEAVING };
// XR_HELD: the story's raccoon (a real critter in slot coon_slot - 1) has the ornament in its mouth.
enum XmasCoon : uint8_t { XR_NONE = 0, XR_REACH, XR_CARRY, XR_PROUD, XR_HELD };
enum XmasExtra : uint8_t {
  XE_STAR = 1,
  XE_RUN = 2,       // with XE_COIL: the run of lights from the coil up to his far hand (he is stringing)
  XE_STOCKING = 4,
  XE_LADDER = 16,   // the ladder: lying in the grass by the pine (ladder 0) .. leaning on it (255)
  XE_COIL = 32,     // the unstrung lights in a coil at the ladder's foot, a run up to his far hand
  XE_GAP = 64,      // the empty hook where the low ornament was
  XE_SNOW = 128     // (SS_XMAS_MOCKS) the snow look
};

// What is posed now. All zero: nothing. The story sets it every tick it changes; the stills and tests pose
// it by hand.
struct XmasMock {
  uint8_t gen;       // 1: the wheel, the box and the wire are in place
  uint8_t wheel;     // the wheel's turn, 64ths
  uint8_t spark;     // 0 none; 1..8 the step of a spark burst off the box (0.06 s each)
  uint8_t wraps;     // light strings strung, 0..XMAS_WRAPS, from the top
  uint8_t power;     // XmasPower (the strings strung are lit unless XP_OFF)
  uint8_t garland;   // XmasGarland
  uint8_t fly;       // XG_FLYING / XG_LEAVING: how far along the flight, 0..255
  uint8_t orns;      // bit i: ornament i hangs on the tree
  uint8_t coon;      // XmasCoon
  uint8_t bush;      // the raccoon's bush: 0 none; 1 + the stolen ornaments on it
  uint8_t extras;    // XmasExtra bits
  uint8_t lift;      // the wheel raised out of the water, 0 (turning in it) .. 255
  uint8_t carry;     // 1: the generator is in his arms (not in place)
  uint8_t ladder;    // with XE_LADDER: 0 lying .. 255 leaning
  uint8_t gifts;     // presents under the tree: 0 none, 1..6 boxes, XMAS_GIFTS_ALL the full pile
  uint8_t coon_slot; // XR_HELD: the raccoon's critter slot + 1
#if SS_XMAS_MOCKS
  uint8_t north;     // the north star: 0 none; 1..4 its twinkle step
#endif
};

struct XmasState {
  XmasMock m;        // what is posed now
  XmasMock d;        // what was drawn last tick
  Rect d_tree, d_gen, d_crit, d_extra;
};

// True when anything is posed.
bool xmasOn(const World& w);
// Ground actors: idx 0..XMAS_ACTORS-1; false when that piece is not shown.
bool xmasActorY(const World& w, int idx, int16_t& y);
void xmasDrawActor(const World& w, int idx, Surface& s, const Rect& clip, const Palette& pal);
// The tree's layer (world.cpp, after the fog, before the ground actors).
void xmasDrawTree(const World& w, Surface& s, const Rect& clip, const Palette& pal);
// The sky (world.cpp, after the distant bird): the mocks' north star. Nothing without SS_XMAS_MOCKS.
void xmasDrawSky(const World& w, Surface& s, const Rect& clip, const Palette& pal);
// The snow look's static part (SS_XMAS_MOCKS and XE_SNOW only).
void xmasDrawStatic(const World& w, Surface& frame, const Palette& pal);
void xmasDrawFgPineSnow(const World& w, int k, Surface& s, const Rect& clip, const Palette& pal);
const Palette& xmasTuftPal(const World& w, const Palette& pal, Palette& tmp);
// Marks what changed (end of worldTick, camp loops only).
void xmasTrack(World& w, Renderer& r);

// Bounds of each group (frame space, empty when nothing in it is shown). For tests.
Rect xmasTreeBounds(const World& w);
Rect xmasGenBounds(const World& w);
Rect xmasCritBounds(const World& w);
Rect xmasExtraBounds(const World& w);

// Geometry the story and the tests share.
Vec2 xmasWheelHub(const Landscape& ls);   // in place, lowered
Vec2 xmasBoxAt(const Landscape& ls);      // the box's middle at its base
float xmasBankY(const Landscape& ls, float x);
Vec2 xmasLadderRung(const Landscape& ls, int k);   // leaning: rung k's middle (0 the lowest, 6 the top)
Vec2 xmasLadderFoot(const Landscape& ls);          // where he stands to lift it, and at its foot
Vec2 xmasOrnament(const Landscape& ls, int i);
Vec2 xmasBushAt(const Landscape& ls);
Vec2 xmasStarAt(const Landscape& ls);
float xmasTreeX(const Landscape& ls);
float xmasTreeBase(const Landscape& ls);

// ---- the story (xmas_story.cpp) -------------------------------------------------------------------

// The plan byte for an evening, morning or midday loop (0 outside the season). An evening loop decorates
// when its window (early: golden and dusk, mood 3.5 to 5.5 outside night mode; late: night) has not
// decorated yet this evening (Memory::taichi_next XMAS_DECK_*). A morning or midday loop clears those bits.
// No rng.
uint8_t xmasPlanByte(const Inputs& in, Memory& mem, uint8_t family);
// The pieces the season has today, on the tree as it stands finished (star from Dec 11, presents by the
// week, the stocking), with the generator lowered and the lights on. The bush keeps its count.
void xmasPoseFinished(World& w);
// Loop start (after the plan, the layout and memoryBeginLoop): the bush's count (Memory::xmas_bush) and
// what the family starts with.
void xmasBeginLoop(World& w);
// Every tick from spineCritterScenes (frozen by the doorbell): the morning's and midday's tree follows the
// tent.
void xmasStoryTick(World& w);
// Every tick, always (worldTick, before xmasTrack; camp loops): the wheel's turn and the sparks. Ambient.
void xmasAmbient(World& w);
// Loop end: the bush's count back into memory (memoryEndLoop).
void xmasEndLoop(World& w);
// The evening's beat after setup (decorate or lights_on) and its seconds; the morning's lights_off before
// break camp.
Beat xmasEveningBeat(const LoopPlan& p);
int xmasEveningSeconds(const LoopPlan& p);
Beat xmasMorningBeat();
int xmasMorningSeconds();
// The bauble_thief major (majors.cpp's pool): begin, tick, script and seconds; var 1 in the tent slot.
void xmasTheftBegin(World& w);
void xmasTheftTick(World& w, float dt);
const Step* xmasTheftScript(uint8_t var, uint8_t& n);
int xmasTheftSeconds(uint8_t var);
// For tests: a scene's doorbell freeze is respected (the decorate beat's own clock).
bool xmasTreeShown(const World& w);

}  // namespace ss

#endif  // SS_XMAS
