// Lookout theme: every color from the design in one place (brief section 13: "keep all colors and
// thresholds in one theme header so a different theme can swap in"). Plain C++.
// Values from the GUI build spec of 2026-10-24, section 6, which the Pd boards render.
#pragma once
#include "../core/types.h"
#include "../gfx/canvas.h"

namespace theme {

using gfx::Color;
using gfx::hex;

// Status tile colors. Index 0-4 = tc::Status, 5 = No signal.
struct StatusColors {
  Color tile;  // bg: the status zone
  Color text;  // fg: floor label, state word, since line, badge shape
  Color tint;  // wx: the weather zone's base, light by day and evening, near black at night
  Color ink;   // INSIDE and the temperature on the weather zone
};
constexpr int kNoSignal = 5;

// Per-mode palette (spec section 6, mode palette).
struct Palette {
  Color paper, gold, outside, doorbell, ring, dog, gear_border;
  Color cream;         // light text: cream by day, #E4D8C0 at night (no white at night)
  Color plum;          // dark text on light surfaces
  Color outside_text, outside_sub;
  Color humidity;      // humidity figures: plum by day and evening, pale at night
  Color door_text;     // dfg: DOORBELL and the bell
  Color gear_ink;      // the gear icon
  Color dog_text;      // COPPER OUT and the circle behind the paw, dog out
  Color heat;          // PROPOSED: WeatherGate's danger-heat sun, hotter than the tile's ink
  Color bell_red;      // the bell while an incoming ring waits (Mike, 2026-10-26)
};

// Sky phase: the date strip's colour (the sky itself is in board_art).
struct SkyColors {
  Color strip;
};

StatusColors status_colors(int status_or_nosignal, tc::Mode m);
const Palette &palette(tc::Mode m);
const SkyColors &sky(tc::Phase p);

// The calendar's tokens (calendar roll-down spec, section 3), per mode. The opacities are 0-255.
struct CalColors {
  Color paper, ink, on_ink, muted, field, accent, accent_text, accent_fill, badge_plus, cream, danger;
  Color up_fill, up_ink, down_fill, down_ink, down_edge, key, key_special, kbd_bg;
  uint8_t kbd_opa, hair_opa, weekend_opa;
};
const CalColors &cal_colors(tc::Mode m);

// Surface colors for things the brief doesn't color (sheet buttons, settings tiles), derived
// from the palette so night stays dim.
Color sheet_bg(tc::Mode m);
Color settings_tile(int i, tc::Mode m);

}  // namespace theme
