// Everything Mike can change in Settings, with starting values. Values Mike chose are marked MIKE;
// values from the handoff are HANDOFF; values I picked to fill a gap are PROPOSED.
#pragma once
#include "types.h"

namespace tc {

// What a doorbell does on the receiving panel, by that panel's own status.
enum class Chime : uint8_t { None = 0, Normal = 1, Soft = 2 };
struct KnockRule {
  Chime chime;
  bool banner;  // show "Doorbell from upstairs" with Come on / Not now
  bool save;    // Away: keep it and show it when the person is back
};

enum class NightLevel : uint8_t { Dim = 0, Dimmer = 1, Dimmest = 2 };

struct Settings {
  Floor floor = Floor::Downstairs;  // "THIS UNIT IS"; the boards show the downstairs unit

  // Sound. MIKE (2026-10-21): Westminster an octave up at 90 by day; 80 is right at night.
  uint8_t volume = 90;
  uint8_t quiet_volume_cap = 80;
  uint16_t quiet_start_min = 22 * 60;  // PROPOSED quiet hours 10 PM to 7 AM
  uint16_t quiet_end_min = 7 * 60;
  uint8_t soft_chime_drop = 15;        // PROPOSED: Busy's soft chime plays this much below the volume
  uint8_t ringtone = 0;                // 0 = Westminster (MIKE), 1 = two-note ding-dong

  // HANDOFF behavior table, changeable in Settings later.
  KnockRule knock[kStatusCount] = {
      {Chime::Normal, true, false},  // Open: chime at the set volume, banner
      {Chime::Soft, true, false},    // Busy: soft chime, banner
      {Chime::None, true, false},    // Private: banner only, screen wakes
      {Chime::None, true, false},    // Sleeping: banner only, screen wakes at night level
      {Chime::None, false, true},    // Away: saved, shown on return
  };

  // Backlight register values (lower is brighter; 255 is black). MIKE: 224 is right for midnight.
  uint8_t backlight_day = 0;      // PROPOSED full brightness
  uint8_t backlight_evening = 128;  // PROPOSED, tune on the panel
  NightLevel night_level = NightLevel::Dimmest;
  bool screen_off_overnight = false;  // HANDOFF: option exists, off by default
  uint32_t touch_wake_ms = 30000;     // HANDOFF: back to dim 30 s after the last touch

  int16_t temp_offset_tenths_f = 0;  // local sensor correction, 1 F steps in the UI

  // Status rules
  Status timer_fallback_default = Status::Busy;  // HANDOFF starting value
  // No longer used: both dogs can be out (Mike, 2026-10-25), so each button always works. Kept so
  // saved settings keep their layout.
  bool dog_tap_blocked_when_other_out = true;

  // Sky. A placeholder in the Eastern time zone (Washington, DC). The panels' own location goes in
  // the untracked secrets.h as TCOMM_LATITUDE and TCOMM_LONGITUDE, and tcomm.ino puts it here.
  float latitude = 38.9f;
  float longitude = -77.0f;
  char tz[48] = "EST5EDT,M3.2.0,M11.1.0";

  uint16_t outside_stale_min = 15;  // HANDOFF: dashes after 15 minutes without an OUTSIDE reading
};

inline uint8_t night_backlight(NightLevel n) {
  // PROPOSED steps toward Mike's 224. Names and steps are still open.
  switch (n) {
    case NightLevel::Dim: return 200;
    case NightLevel::Dimmer: return 212;
    default: return 224;
  }
}

inline const char *night_level_name(NightLevel n) {
  switch (n) {
    case NightLevel::Dim: return "Dim";
    case NightLevel::Dimmer: return "Dimmer";
    default: return "Dimmest";
  }
}

inline bool in_quiet_hours(const Settings &s, int minute_of_day) {
  if (s.quiet_start_min == s.quiet_end_min) return false;
  if (s.quiet_start_min < s.quiet_end_min) return minute_of_day >= s.quiet_start_min && minute_of_day < s.quiet_end_min;
  return minute_of_day >= s.quiet_start_min || minute_of_day < s.quiet_end_min;  // wraps midnight
}

}  // namespace tc
