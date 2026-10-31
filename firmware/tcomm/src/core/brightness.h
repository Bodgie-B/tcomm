// Backlight policy. HANDOFF: at night the screen stays on at the dim night level; a touch brings
// it to full light, back to dim 30 s after the last touch; "screen off overnight" is an option.
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
};

constexpr uint8_t kBacklightOff = 255;

// Register value for the expander's PWM (lower is brighter).
inline uint8_t backlight_for(const Settings &s, const BacklightInputs &in) {
  switch (in.clock_unknown ? Mode::Night : in.mode) {
    case Mode::Day: return s.backlight_day;
    case Mode::Evening: return s.backlight_evening;
    case Mode::Night:
    default:
      // PROPOSED reading of "full light": the day level, while the colors stay on the night palette.
      if (in.touched_recently || in.clean_screen) return s.backlight_day;
      if (s.screen_off_overnight && !in.doorbell_wake && !in.mail_wake) return kBacklightOff;
      return night_backlight(s.night_level);  // a doorbell or mail wakes it at the current (night) level
  }
}

}  // namespace tc
