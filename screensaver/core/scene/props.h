// Camp props: the tent, the pack on the ground, the food bag, the cairn and
// small items. Plain data that lives by value in World; the spine beats set
// the fields, World tracks their bounds and draws them in the ground-actor
// sort. Every prop is drawn from its fields alone, and its bounds come from
// the same geometry, so a prop can never draw outside what it marks dirty.
//
// Silhouette language follows the hiker: things he owns are ACTOR (the pack
// is the same outline as the one on his back), wood is TRUNK, stones ROCK.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"
#include "hiker.h"

namespace ss {

// ---- tent ------------------------------------------------------------------

enum TentState : uint8_t { TENT_PACKED = 0, TENT_RAISING, TENT_UP, TENT_COLLAPSED };

// A ridge tent seen three-quarters on, door toward +x. The pole heights come
// from `progress` (spread on the ground, back pole, front pole, pegged taut),
// then `sag` slumps them and `bump` stretches them, so a beat can collapse a
// half-raised tent or overshoot a raised one without a jump. `state` is the
// story's view (PACKED draws nothing).
struct Tent {
  Vec2 pos;          // base center on the ground
  uint8_t state;     // TentState
  float progress;    // 0 fabric spread .. 1 pegged; UP is 1
  float sag;         // 0 taut .. 1 slumped flat
  float bump;        // extra pole height factor (overshoot), 0 at rest
  float wobble;      // apex lean in px, + toward the door
  bool lit;          // lantern inside: TENT_LIT body
  bool shadow;       // the hiker's shadow on the wall (drawn when lit)
  float shadow_x;    // shadow center offset from pos.x
  float shadow_y;    // shadow offset in y (World breathes it, whole px)
  // 0 calm .. 1 trembling (the bear beat). Above 0 he holds his breath (no
  // breathing: the shadow freezes); World shakes it by up to 2 px in x,
  // whole px, through shadow_shake, and hunches it down up to 3 px.
  float shadow_jitter;
  float shadow_shake;  // x offset World sets from shadow_jitter (whole px)
  bool bite;           // a mouse hole bitten out of the base, front right (memory, stage 6)
};

void tentInit(Tent& t, Vec2 pos);
Rect tentBounds(const Tent& t);
void tentDraw(const Tent& t, Surface& s, const Rect& clip, const Palette& pal);
bool tentSame(const Tent& a, const Tent& b);

// ---- pack on the ground ------------------------------------------------------

// Shown while the hiker's pack is off. `held`: World places it at his hands
// each tick (he is moving it); otherwise it stands upright at `pos`.
struct PackProp {
  Vec2 pos;          // base center
  int8_t facing;     // which way its back faces, as on the hiker
  bool shown;
  bool held;
};

Rect packBounds(const PackProp& p);
void packDraw(const PackProp& p, Surface& s, const Rect& clip, const Palette& pal);
bool packSame(const PackProp& a, const PackProp& b);

// ---- food bag ------------------------------------------------------------------

enum BagState : uint8_t { BAG_NONE = 0, BAG_CARRIED, BAG_HUNG, BAG_DROPPED, BAG_FLYING };

// HUNG: a sack on a line from the camp pine's branch tip; `line` and `swing`
// let a beat hoist it and the bear shake it. DROPPED: lying at `pos`.
// CARRIED: not drawn (the hiker draws CARRY_BAG). FLYING: thrown; the sack's
// neck is at `pos`, tilted by `swing`, with the rope trailing to `rope` (his
// hand; World keeps it there).
struct FoodBag {
  uint8_t state;     // BagState
  Vec2 tip;          // branch tip the line hangs from
  float line;        // px from the tip to the sack's neck
  float swing;       // radians from straight down, + toward +x
  Vec2 pos;          // DROPPED: where it lies; FLYING: the neck
  Vec2 rope;         // FLYING: the rope's other end
};

void bagInit(FoodBag& b, Vec2 tip);
Rect bagBounds(const FoodBag& b);
void bagDraw(const FoodBag& b, Surface& s, const Rect& clip, const Palette& pal);
bool bagSame(const FoodBag& a, const FoodBag& b);

// ---- cairn ----------------------------------------------------------------------

#define CAIRN_MAX 7

// Flat stones stacked by the stream. `pop` 0..1 is the newest stone landing
// (it drops in with an overshoot); 1 at rest.
struct Cairn {
  Vec2 pos;          // base center
  uint8_t n;         // stones, 0..CAIRN_MAX
  float pop;
};

Rect cairnBounds(const Cairn& c);
void cairnDraw(const Cairn& c, Surface& s, const Rect& clip, const Palette& pal);
bool cairnSame(const Cairn& a, const Cairn& b);

// ---- small items ------------------------------------------------------------------

// WOOD is the pile by the fire; `size` grows it (1 an armload, ~1.6 two).
// STICK held is dragged: from the hiker's hands to the ground behind him.
// FORK held stands upright in his near hand (the dinner c gag); down it lies flat.
enum ItemId : uint8_t {
  ITEM_MUG = 0, ITEM_BOOK, ITEM_POT, ITEM_STICK, ITEM_SHOE, ITEM_WOOD, ITEM_FORK, ITEM_COUNT
};

// A stick's length, and how much of it sticks out ahead of a critter's mouth
// when carried (Item::grip).
constexpr float kItemStickLen = 62.f;
constexpr float kItemMouthGrip = 0.3f;

struct Item {
  Vec2 pos;          // on the ground: base center; held: the hands (World sets it)
  bool down;         // lying on the ground, drawn
  bool held;         // carried in a way the rig does not draw (World sets pos, ground_y, facing)
  int8_t facing;
  float size;        // scale, 1 normal
  float ground_y;    // held: ground under the hiker
  bool on_fire;      // sits on the fire (the pot): drawn just in front of the flames
  // Held by a critter instead (with `held`): 1 + its World slot, 0 for the
  // hiker. The director keeps pos at that critter's mouth (on all fours) or
  // paw (reared), `facing` and `squeeze` with the critter's; a stick then lies
  // along `angle` turns from straight up (+ toward `facing`), `grip` of its
  // length ahead of pos and the rest behind (0: held by the end).
  uint8_t critter;
  float angle;
  float grip;
  float squeeze;     // critter-held: the holder's turn-around squeeze (x scale, 0.15..1)
  bool placed;       // critter-held, but its owner sets pos and grip (a pick-up or a drop under way)
};

Rect itemBounds(ItemId id, const Item& it);
void itemDraw(ItemId id, const Item& it, Surface& s, const Rect& clip, const Palette& pal);
bool itemSame(const Item& a, const Item& b);
const char* itemName(ItemId id);

// ---- small effects the spine drives ---------------------------------------------
// Each is plain data set by a script action every tick (no randomness and no
// clock of its own), drawn from its fields alone, with bounds from the same
// geometry.

// Steam boiling over from a pot: `n` (3..5) puffs burst from `at` one after
// another and rise fast. `t` seconds since the burst; < 0 is off.
struct Steam {
  Vec2 at;
  float t;
  uint8_t n;
};
Rect steamBounds(const Steam& s);
void steamDraw(const Steam& s, Surface& dst, const Rect& clip, const Palette& pal);
bool steamSame(const Steam& a, const Steam& b);

// A skipped stone: thrown from p[0], it touches the water at p[1..n] and sinks
// at p[n]; each touch leaves a ring that spreads and thins. `t` seconds since
// the throw; < 0 is off.
#define SKIP_MAX 4
struct Skip {
  Vec2 p[SKIP_MAX + 1];
  uint8_t n;
  float t;
};
Rect skipBounds(const Skip& k);
void skipDraw(const Skip& k, Surface& dst, const Rect& clip, const Palette& pal);
bool skipSame(const Skip& a, const Skip& b);
float skipHitTime(const Skip& k, int i);   // seconds after the throw the stone touches p[i]
float skipEndTime(const Skip& k);          // the last ring has gone

// One leaf (ACTOR, like every small silhouette), stem end at `pos`, turned by
// `rot` turns. `base_y` sorts it among the ground actors.
struct Leaf {
  bool on;
  Vec2 pos;
  float rot;
  float base_y;
};
Rect leafBounds(const Leaf& l);
void leafDraw(const Leaf& l, Surface& dst, const Rect& clip, const Palette& pal);
bool leafSame(const Leaf& a, const Leaf& b);

// Small things that fall or puff in the critter scenes. A pine cone (TRUNK)
// falls under gravity to `ground_y`, bounces off it, and if `land_y` is lower
// falls on to land there (a cone off the bear's head), then lies still. A dust
// puff (SMOKE) swells, drifts up and shrinks away over BIT_DUST_S seconds.
// bitStep moves them (World calls it every tick); drawn from the fields alone.
//
// The minor effects (minors.h) add: DIRT, a fleck of earth thrown up by a dig
// (falls to ground_y and is gone); DROP, a crumb falling to the water at
// ground_y, where it turns into a small RING; RING, a ring spreading on the
// water and thinning over BIT_RING_S (`spin` is its size, 1 a frog); LEAF, an
// ACTOR leaf (`spin` its turn, as Leaf::rot); MOTH and BEE, a few px of
// fluttering ACTOR. LEAF, MOTH and BEE are placed by their owner each tick.
// `land_y` is the base y every kind sorts by among the ground actors (the
// cone and dust set it too).
enum BitKind : uint8_t { BIT_NONE = 0, BIT_CONE, BIT_DUST, BIT_DIRT, BIT_DROP, BIT_RING, BIT_LEAF, BIT_MOTH, BIT_BEE };
// The bear beat writes slots 0..3 by index; the minors take free slots from the top.
#define BITS_MAX 6
#define BIT_DUST_S 0.9f
#define BIT_RING_S 1.6f
struct Bit {
  uint8_t kind;
  bool resting;      // cone: on the ground for good
  uint8_t bounces;
  Vec2 pos;
  Vec2 vel;          // px/s
  float ground_y;    // cone: first surface it meets
  float land_y;      // cone: where it ends up
  float t;           // seconds since it started
  float spin;        // cone: turns
};
void bitStep(Bit& b, float dt);
Rect bitBounds(const Bit& b);
void bitDraw(const Bit& b, Surface& dst, const Rect& clip, const Palette& pal);
bool bitSame(const Bit& a, const Bit& b);

// ---- what the hiker holds that his rig does not draw ---------------------------

// Midpoint of the hiker's hands in frame space (hikerHandPos for both).
Vec2 hikerHandsMid(const Hiker& h);

// An armload far too big: logs piled on top of the carried wood (firewood beat
// variant b). `k` 0..1 fades it in; 0 draws nothing.
Rect armloadBounds(const Hiker& h, float k);
void armloadDraw(const Hiker& h, float k, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
