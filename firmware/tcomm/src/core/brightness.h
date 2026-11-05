// Backlight policy. HANDOFF: at night the screen stays on at the dim night level; a touch brings
// it to full light, back to dim 30 s after the last touch; "screen off overnight" is an option.
// MIKE (2026-11-05): with that option on, the screen is dim at night until 10 PM, dark from 10 PM to
// 6 AM by the clock, then as usual (Settings::screen_off_start_min, screen_off_end_min).
#pragma once
#include "settings.h"

namespace tc {

struct BacklightInputs {
  Mode mode = Mode::Day;
  bool touched_recently = false;   // within Settings::touch_wake_ms
  bool doorbell_wake = false;      // a doorbell banner is showing
  bool mail_wake = false;          // new mail came for this panel a moment ago (downstairs)
  bool clean_screen = false;       // glass-wiping mode keeps the screen lit
  bool clock_unknown = false;      // no time of day: be as dim as night (a power cut is often at night)
  int minute_of_day = -1;          // local time, 0-1439; -1 without a clock (then never dark: dim)
};

constexpr uint8_t kBacklightOff = 255;

// Register value for the expander's PWM (lower is brighter).
inline uint8_t backlight_for(const Settings &s, const BacklightInputs &in) {
  const bool lit = in.touched_recently || in.clean_screen;
  // The dark hours go by the clock, whatever the sky (in summer 6 AM can be after dawn). A touch
  // lights it as usual; a doorbell or mail wakes it at the level of the moment.
  if (s.screen_off_overnight && in.minute_of_day >= 0 && in_screen_off_hours(s, in.minute_of_day) && !lit &&
      !in.doorbell_wake && !in.mail_wake)
    return kBacklightOff;
  switch (in.clock_unknown ? Mode::Night : in.mode) {
    case Mode::Day: return s.backlight_day;
    case Mode::Evening: return s.backlight_evening;
    case Mode::Night:
    default:
      // PROPOSED reading of "full light": the day level, while the colors stay on the night palette.
      if (lit) return s.backlight_day;
      return night_backlight(s.night_level);
  }
}

}  // namespace tc
