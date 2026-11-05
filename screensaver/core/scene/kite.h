// The kite (morning and daytime plan, section 2): a diamond kite with a cross
// spar and a ribbon tail of three bows, flown on a 1 px line from the hiker's
// hand. A standalone module like the chip: plain data, a step function, bounds
// and a draw. It owns no World state, calls no platform code and draws no
// random numbers; the caller supplies the hand every tick and the wind.
//
// The caller (a script, a major) commands the modes; the step does the motion:
//
//   HELD     in his hands, folded or flat, upright or lying in his lap (kiteHold)
//   LAUNCH   thrown up from the hand, or lifted off his lap by a breeze (kiteLaunch);
//            it climbs on the wind while the line pays out, then becomes FLY by itself
//   FLY      rides the wind at the end of `line` px of line, swaying and bobbing,
//            the tail rippling; the wind (and gusts) lower it toward the horizontal
//            and make it sway and ripple more (kiteFly)
//   REEL     flying while the line is wound in; becomes HELD at the hand (kiteReel)
//   NOSE     nosedive: it flips head down and drops, then lands (kiteNosedive)
//   SPIN     spins and falls, then crashes (kiteSpinCrash)
//   DROP     loses the wind and sinks, fluttering, to a soft landing (kiteDrop)
//   GROUND   lying where it came down, the slack line gathered in; reached by
//            itself from NOSE, SPIN and DROP. kiteHold picks it up.
//   PULL     the kite wins: a gust yanks it toward the horizontal and it drags the
//            hand toward the kite's side; the caller moves the hiker by
//            kitePullSpeed each tick (kitePull)
//   GONE     the kite is away; only the loose line trails from the hand (kiteLose)
//   HIDDEN   nothing drawn
//
// What the caller does each tick, in order: move the hiker, read his hand
// (hikerHandPos), kiteStep(k, hand, wind, dt), mark dirty rows, draw. `wind`
// is 0..1 (kiteWindFrom turns the weather's fields into it): 0.3 is a gentle
// breeze, 1 a hard gust. Below about 0.1 the kite will not hold its height.
//
// Scripting the three plan variants (section 2):
//   A serene:   kiteHold, walk backward, kiteLaunch(1) + kiteFly(220), watch
//               for a long calm stretch, kiteReel, wait for HELD.
//   B kite wins: as A, then kitePull; move him by kitePullSpeed each tick until
//               he is off the right edge; kiteLose; he walks back in, line dangling.
//   C won't fly: kiteLaunch(1) + kiteNosedive; wait for GROUND; he walks over and
//               kiteHold; kiteLaunch(1) + kiteSpinCrash; kiteHold; kiteLaunch(0.7)
//               then kiteDrop after about a second; he sits with kiteHold(0, 1);
//               kiteLaunch(0) + kiteFly(200): the breeze lifts it out of his lap.
//
// Drawing and dirty marking. kiteDraw is a pure function of the state, the
// palette and the clip; everything it reads is quantized to whole pixels and
// small steps, so a tick that changes nothing visible changes nothing on
// screen. kiteBounds is everything the kite covers, the whole line included
// (a long line crosses several strips). For marking, kiteDirty is cheaper: it
// returns only the areas that differ between last tick's state and this
// tick's, with the line split into short segments so a swaying kite does not
// dirty the whole length of its line every tick.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED
// until Mike has seen it (and on the panel).
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"

namespace ss {

enum KiteMode : uint8_t {
  KITE_HIDDEN = 0, KITE_HELD, KITE_LAUNCH, KITE_FLY, KITE_REEL, KITE_NOSE, KITE_SPIN, KITE_DROP,
  KITE_GROUND, KITE_PULL, KITE_GONE,
  KITE_MODES
};

// The kite's size in px (PROPOSED): the diamond, and the tail hanging from its
// bottom corner. The line is a 1 px line.
constexpr float kKiteW = 26.f;
constexpr float kKiteH = 34.f;
constexpr float kKiteTail = 36.f;

// Rects kiteDirty can return.
#define KITE_DIRTY_MAX 6

// POD, about 90 bytes. Lives by value in the World (and once more in
// WorldDirty for kiteDirty).
struct KiteState {
  uint8_t mode;      // KiteMode
  int8_t dir;        // +1 the kite flies on the +x side of the hand, -1 the -x side (GONE: the side the line trails)
  uint8_t fold_to;   // HELD: the fold the caller asked for, 0..255
  uint8_t lay_to;    // HELD: the lie the caller asked for, 0..255
  float t;           // seconds in this mode
  Vec2 hand;         // the hand, as of the last step
  Vec2 pos;          // the kite: where its spars cross
  Vec2 vel;          // px/s
  float rot;         // turns; 0 upright, + tips the top toward +x
  float spin;        // turns/s (NOSE's flip, SPIN)
  float r;           // line paid out, px
  float line;        // line wanted, px
  float ground_y;    // frame y where it comes down
  float phase;       // sway clock, turns (wraps)
  float wind;        // the wind it feels, 0..1, eased
  float fold, lay;   // 0 open .. 1 folded narrow; 0 upright .. 1 lying flat on the ground or in his lap
  Vec2 end;          // GONE: where the loose end of the line lies
};

// A hidden kite for a camp whose ground line is `ground_y` (where it lands),
// flying on the `side` of the hand (+1 or -1). `seed` sets its sway and tail
// phases; the Rng form draws exactly one value. Neither draws again later.
void kiteInit(KiteState& k, float ground_y, int side, uint32_t seed);
void kiteInit(KiteState& k, float ground_y, int side, Rng& rng);

// One tick (SS_TICK_DT; any dt up to 0.1 s works, so a scene that wants the
// kite to move at 12.5 Hz can step it every second tick with twice the dt).
// `hand` is where the line leaves his hand this tick; `wind` is 0..1.
void kiteStep(KiteState& k, Vec2 hand, float wind, float dt);

// The weather's wind and gust (Weather::wind, -1..0, and Weather::gust, 0..1)
// as the 0..1 wind kiteStep wants: a gentle 0.3 on a calm day, up to 1 in a
// gust of a windy one.
inline float kiteWindFrom(float weather_wind, float weather_gust) {
  const float a = weather_wind < 0.f ? -weather_wind : weather_wind;
  const float g = weather_gust < 0.f ? 0.f : (weather_gust > 1.f ? 1.f : weather_gust);
  const float w = 0.3f + 0.7f * a * (0.25f + 0.75f * g);
  return w > 1.f ? 1.f : w;
}

// ---- commands (each sets the mode; call it once, except kiteHold, which may be called every tick) ----

void kiteHide(KiteState& k);
// In his hands. `fold` 0 open .. 1 folded narrow with the tail wound away;
// `lay` 0 held up .. 1 lying in his lap. Both ease toward what is asked
// (about 0.1 s), so a script may set them from its own 0..1 progress.
void kiteHold(KiteState& k, float fold, float lay);
// Thrown or lifted from where it is. `toss` 1 a hard throw up, 0 a breeze
// lifting it out of his hands. Then FLY by itself after about 1.2 s.
void kiteLaunch(KiteState& k, float toss);
// Ride the wind at the end of `line` px of line (20..400). Pays out at about
// 55 px/s and winds in at 70. On a held or hidden kite it launches it first.
void kiteFly(KiteState& k, float line);
void kiteReel(KiteState& k);        // flying kite only; ends HELD at the hand
void kiteNosedive(KiteState& k);    // airborne kite only; ends GROUND
void kiteSpinCrash(KiteState& k);   // airborne kite only; ends GROUND
void kiteDrop(KiteState& k);        // airborne kite only; ends GROUND
void kitePull(KiteState& k);        // airborne kite only; stays PULL until commanded
void kiteLose(KiteState& k);        // the kite is gone, the loose line trails from the hand

// During PULL, how fast the line drags the hand, px/s, signed toward the
// kite's side (+x for side +1): nothing for half a second (he digs in), a
// skid, then a run (PROPOSED: up to 90 px/s, faster than his 78 px/s run).
// 0 in every other mode. The caller moves his feet by this each tick.
float kitePullSpeed(const KiteState& k);

// ---- bounds, draw ----

// Everything the kite covers now: the diamond, the tail and the line, to
// their antialiased fringe. Empty when nothing is drawn. Not clamped to the
// frame (a pulled kite is far off the edge).
Rect kiteBounds(const KiteState& k);

// True when the two states draw exactly the same pixels.
bool kiteSame(const KiteState& a, const KiteState& b);

// The areas to mark dirty when the kite goes from `before` to `after`: where
// the diamond, the tail and each changed stretch of line were and are. Returns
// how many rects (0 when nothing visible changed, at most KITE_DIRTY_MAX).
// Together they cover every pixel that differs between the two draws.
int kiteDirty(const KiteState& before, const KiteState& after, Rect out[KITE_DIRTY_MAX]);

// Draw onto `strip` (a strip or a whole frame; frame coordinates), touching
// only pixels inside `clip` and inside kiteBounds. Needs the rasterizer
// scratch (rasterSetScratch). Colors: MUSHROOM and TENT_LIT for the sail and
// the bows, ACTOR for the outline, the spars, the tail and the line.
void kiteDraw(const KiteState& k, Surface& strip, const Rect& clip, const Palette& pal);

}  // namespace ss
