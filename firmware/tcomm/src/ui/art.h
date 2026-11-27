// The Lookout artwork, drawn with gfx into canvases that LVGL shows as images. Text is not drawn
// here: labels sit on top as LVGL objects. The shapes come from the Pd boards (board_art); this
// file places them, picks their colours and sets each layer's opacity (GUI spec section 2).
#pragma once
#include "../core/events.h"
#include "../core/holidays.h"
#include "../core/requests.h"
#include "../core/status.h"
#include "../core/types.h"
#include "../gfx/canvas.h"

namespace art {

constexpr int kScreenW = 800, kScreenH = 480;
constexpr int kFloorW = 514, kFloorH = 193, kStatusW = 386, kSideW = 250;
constexpr int kStripH = 56;
constexpr int kBadge = 80, kForecast = 88, kCaret = 16;
constexpr int kIciclesH = 100;  // the danger-cold icicles: the screen's width by this

void sky(gfx::Canvas &c, tc::Phase ph, float p, bool moon);  // 800 x 480, opaque
// The date strip, any width x 56, at 90 percent: its colour and its small scenery at the right
// end. holiday_x: where a day's message starts (strip coordinates), or 0 for none; icon goes in
// the 49 px before it.
// The scenery's width at the strip's right end, with the dog in or out (spec section 4e).
constexpr int kStripSceneryIn = 299, kStripSceneryOut = 118;
void strip(gfx::Canvas &c, tc::Phase ph, float p, bool moon, bool dog_out, float holiday_x,
           tc::DayIcon icon = tc::DayIcon::Tree);
// A day's icon for the date strip (holidays, birthdays, anniversaries), in its own colours, in a
// size x size box at (x, y). Holly is the Christmas board's sprig (49 x 56).
void day_icon(gfx::Canvas &c, tc::DayIcon i, float x, float y, float size);
void floor_tile(gfx::Canvas &c, tc::Floor f, int status_or_nosignal, tc::Mode m);  // 514 x 193, 90 %
void outside_tile(gfx::Canvas &c, tc::Mode m);                                    // 250 x 193, 90 %
void bell_tile(gfx::Canvas &c, tc::Mode m, bool pressed);  // 250 x 193: 80 % behind, the ring solid
// The doorbell's bell, its own picture so it can shake and turn red (Mike, 2026-10-26): kBellPic
// square, placed at tile (kBellPicX, kBellPicY) it sits where the boards have it.
constexpr int kBellPic = 56, kBellPicX = 97, kBellPicY = 54;
void bell(gfx::Canvas &c, gfx::Color col);

// Icons at their design sizes: PawIn 36, PawOut 34, Gear 32, Close any, Caret 16, DropIn 15 x 19,
// DropOut 14 x 18.
enum class Icon { PawIn, PawOut, Gear, Close, Caret, DropIn, DropOut };
void icon(gfx::Canvas &c, Icon i, gfx::Color fg);
// The mail badge on the DOWNSTAIRS tile, kMail across: a ring and an envelope in fg, or (solid) a
// disc in fg with the envelope in bg.
constexpr int kMail = 34;
void mail(gfx::Canvas &c, bool solid, gfx::Color fg, gfx::Color bg);
// The "no mail" badge (Mike, 2026-11-18), the same kMail size: the mail badge's envelope inside a ring
// with a diagonal slash through both, all in ink. A thin gap in `gap` (the tile's colour, or the disc's)
// keeps the slash clear of the envelope. With a disc colour the badge sits on a filled disc of it,
// which is how the red one stays readable on any tile colour.
void nomail(gfx::Canvas &c, gfx::Color ink, gfx::Color gap, const gfx::Color *disc);
// The status badge, 80 x 80 on the tiles and the timer sheet; a smaller square canvas draws it scaled
// (the substatus menu's previews). Mike, 2026-11-26, the substatus marks:
//   Ring     Open's Free passage: only the outer ring is left, hollow, in fg (no disc, no check)
//   BellOff  Quiet passage: a doorbell with a slash in place of the check or hourglass, in bg
//   BellRed  Prefer no passage: a red doorbell (red) in place of the hourglass
enum class BadgeMark : uint8_t { Plain, Ring, BellOff, BellRed };
BadgeMark badge_mark(tc::Status st, tc::Sub sub);
void badge(gfx::Canvas &c, int status_or_nosignal, gfx::Color fg, gfx::Color bg, BadgeMark mark = BadgeMark::Plain,
           gfx::Color red = gfx::Color{0xE0, 0x48, 0x3C});
// A heat or cooling arrow (requests.h), filling a square canvas of any size (kArrow on the tile,
// larger on the survey). The tenant's: a dark plum disc with the arrow in its colour (mild heat
// light red, urgent heat red, mild cooling light blue, urgent cooling blue; urgent ones are two
// overlapping arrowheads). A prediction's: the disc filled with the colour (light or dark orange
// for heat, light or dark purple for cooling) and the arrow plum on the light ones, cream on the
// dark ones. arrow_opa (0-255) dims the arrow alone, for the dim half of an urgent arrow's flash.
// Colours: docs/temperature-requests.md and tools/temperature_mockups.py.
constexpr int kArrow = 34;
void request_arrow(gfx::Canvas &c, tc::Ask a, tc::ReqSource s, uint8_t arrow_opa = 255);
// Forecast icons, 88 x 88, one colour. At night the moon takes the sun's place (sunny, partly
// cloudy, clearing).
void forecast(gfx::Canvas &c, tc::Forecast f, bool night, gfx::Color ink);
// WeatherGate's danger heat: a bigger sun in place of the forecast, drawn as its disc or its rays
// (88 x 88 each), so the rays can pulse over the disc.
void heat_sun(gfx::Canvas &c, bool rays, gfx::Color c_sun);
// WeatherGate's danger cold: icicles hanging from the top of the screen, at 50 percent
// (800 x kIciclesH). The screen draws them straight into each area it redraws, a strip of rows at
// a time, from a small list of their shapes (memory audit 2026-11-21: about 14 KB in place of an
// 800 x 100 picture, 240,000 bytes).
// One icicle: its middle at the ledge, its width there, its length and its lean (how far its tip
// is to the side). icicle_layout() lists them in drawing order (each cluster's small ones, then its
// main one), at most max of them, and returns how many the layout has (icicle_layout(nullptr, 0)
// just counts). The pattern is fixed, as WeatherGate's is, so it never shimmers.
struct Icicle {
  float x, w, len, lean;
};
constexpr int kIciclesMax = 128;  // the layout has 47
int icicle_layout(Icicle *out, int max);
// The rows canvas c can draw (the open strip's, or all of them), columns xa..xb only: those columns
// cleared, then the ledge and every icicle that reaches them, solid. Shown at kIciclesOpa. Strip by
// strip it makes exactly the pixels icicles() makes in those columns (before the opacity).
constexpr uint8_t kIciclesOpa = 128;  // 50 percent, as multiply_alpha(0.5f) has it
void icicles_cols(gfx::Canvas &c, const Icicle *ice, int n, int xa, int xb);
// The whole layer in one go, at 50 percent: tests and previews (the screen uses icicles_cols).
void icicles(gfx::Canvas &c);

// ---- Calendar (calendar roll-down spec, section 4) ----
// A floor marker in a size x size box at (x, y): up, a light circle with a dark outline and an up
// caret; down, a dark circle with a light ring and a down caret, and an outer edge (visible at night).
void cal_marker(gfx::Canvas &c, bool up, float x, float y, float size, gfx::Color fill, gfx::Color ink,
                gfx::Color edge);
// The marker with a cake (a birthday) or two rings (an anniversary) in place of the caret, in the
// floor's colours, so the floor still shows.
void cal_kind_marker(gfx::Canvas &c, tc::EventKind k, bool up, float x, float y, float size, gfx::Color fill,
                     gfx::Color ink, gfx::Color edge);
// The add button in a (size + 14) square canvas: the marker at (7, 7), its dashed ring up and to the
// left, and the "+" badge on its top right corner.
void cal_add_button(gfx::Canvas &c, bool up, float size, gfx::Color fill, gfx::Color ink, gfx::Color edge,
                    gfx::Color accent, gfx::Color plus);
// A pill (the month and week views' stretched bubbles, the day view's all-day row): fill with a
// border, each end rounded or square (an event carried on from the row before or into the next).
void cal_pill(gfx::Canvas &c, gfx::Color fill, gfx::Color border, float border_w, float radius, bool round_left,
              bool round_right);
// Calendar icons, drawn to fill the canvas: chevrons 18 x 18 (stroke 3), trash 16 x 16, shift
// 22 x 22, backspace 26 x 22, repeat (two arrows chasing round) 18 x 18, minus and plus any size, and
// the keyboard's microphone 22 x 22 (voice notes: a capsule, the U under it, a stem and a base).
enum class CalIcon { Prev, Next, Up, Down, Trash, Shift, Backspace, Repeat, Minus, Plus, Mic };
void cal_icon(gfx::Canvas &c, CalIcon i, gfx::Color col);

}  // namespace art
