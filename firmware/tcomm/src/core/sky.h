// Sunrise and sunset from latitude, longitude and date (no network needed), and the six sky phases.
// PROPOSED phase windows from the design brief; Mike hasn't set them.
#pragma once
#include "types.h"

namespace tc {

struct SunTimes {
  bool ok = false;     // false in polar day or night; callers then use 6 AM / 6 PM
  int64_t rise = 0;    // UTC seconds
  int64_t set = 0;
};

// Sunrise equation (Wikipedia / NOAA simplified form), good to a minute or two at our latitudes.
// local_y/m/d is the calendar date at the panel; utc_offset_s is the offset on that date.
SunTimes sun_times(int local_y, int local_m, int local_d, int32_t utc_offset_s, float lat, float lon);

struct Sky {
  Phase phase = Phase::Midday;
  Mode mode = Mode::Day;
  float p = 0.5f;     // position along the arc, 0 = rising, 1 = setting
  bool moon = false;  // night shows the moon on the same arc
  int64_t sunrise = 0, sunset = 0;  // today's, UTC
};

// Phase windows (minutes):
//   night  sunset+60 .. sunrise-40      dawn   sunrise-40 .. sunrise+60
//   morning  .. 11:00 local             midday 11:00 .. sunset-120
//   golden sunset-120 .. sunset-10      dusk   sunset-10 .. sunset+60
Sky sky_at(int64_t utc, int32_t utc_offset_s, float lat, float lon);

// The screensaver's Lookout plays only around the real sunset (screensaver brief section 7):
// PROPOSED window from 30 minutes before today's sunset to 15 minutes after. False with no real
// sunset (polar day or night: the 6 PM fallback is not a sunset).
constexpr int kLookoutBeforeMin = 30;
constexpr int kLookoutAfterMin = 15;
bool in_lookout_window(int64_t utc, int32_t utc_offset_s, float lat, float lon);
// Today's sunset in UTC seconds, or 0 when there is none.
int64_t sunset_today(int64_t utc, int32_t utc_offset_s, float lat, float lon);

}  // namespace tc
