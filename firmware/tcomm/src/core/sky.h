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

// The same equation for a given solar zenith in degrees (90.833 is sunrise and sunset; 84 is the sun
// 6 degrees above the horizon): rise is the morning crossing, set the evening one. ok is false when
// the sun never reaches or never leaves that zenith that day.
SunTimes sun_times_at_zenith(int local_y, int local_m, int local_d, int32_t utc_offset_s, float lat, float lon,
                             double zenith_deg);
constexpr double kGoldenZenithDeg = 84.0;  // Mike, 2026-11-07: golden hour starts 6 degrees up

struct Sky {
  Phase phase = Phase::Midday;
  Mode mode = Mode::Day;
  float p = 0.5f;     // position along the arc, 0 = rising, 1 = setting
  bool moon = false;  // night shows the moon on the same arc
  int64_t sunrise = 0, sunset = 0;  // today's, UTC
};

// Phase windows (minutes):
//   night  sunset+60 .. sunrise-40      dawn   sunrise-40 .. sunrise+60
//   morning  .. 11:00 local             midday 11:00 .. golden start
//   golden   golden start .. sunset-10  dusk   sunset-10 .. sunset+60
// Golden starts when the sun is 6 degrees above the horizon in the afternoon (Mike, 2026-11-07), at
// 11:00 local if that comes earlier or the sun never gets that high.
Sky sky_at(int64_t utc, int32_t utc_offset_s, float lat, float lon);

// When today's golden phase starts, UTC seconds (see above).
int64_t golden_start(int64_t utc, int32_t utc_offset_s, float lat, float lon);

// The screensaver's Lookout plays only around the real sunset (screensaver brief section 7):
// PROPOSED window from 30 minutes before today's sunset to 15 minutes after. False with no real
// sunset (polar day or night: the 6 PM fallback is not a sunset).
constexpr int kLookoutBeforeMin = 30;
constexpr int kLookoutAfterMin = 15;
bool in_lookout_window(int64_t utc, int32_t utc_offset_s, float lat, float lon);
// Today's sunset in UTC seconds, or 0 when there is none.
int64_t sunset_today(int64_t utc, int32_t utc_offset_s, float lat, float lon);

// Where the sun and the moon really are, for the screensaver's sky (Mike, 2026-11-11). Degrees:
// az clockwise from north (90 east, 180 south, 270 west), alt above the horizon (negative below).
// Low-precision almanac series: the sun within about a tenth of a degree, the moon within a degree
// or two, which is plenty for a drawn sky. Only the time and the place go in; no zone.
struct SkyPos {
  float az = 180.f;
  float alt = 0.f;
};
SkyPos sun_position(int64_t utc, float lat, float lon);

struct MoonSky {
  SkyPos pos;
  // The moon's age as a fraction of its month: 0 new, 0.25 first quarter, 0.5 full, 0.75 last
  // quarter. Under 0.5 it is waxing (in the northern hemisphere the lit side is on the right).
  float phase = 0.f;
  float lit = 0.f;  // the lit fraction of the disc, 0..1
};
MoonSky moon_sky(int64_t utc, float lat, float lon);

}  // namespace tc
