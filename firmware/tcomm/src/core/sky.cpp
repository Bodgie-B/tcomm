#include "sky.h"
#include "civil.h"
#include <math.h>

namespace tc {

static constexpr double kDeg = 3.14159265358979323846 / 180.0;

SunTimes sun_times(int y, int m, int d, int32_t off, float lat, float lon) {
  SunTimes t;
  // Julian day number of local noon on that date.
  double noon_utc = (double)(seconds_from_civil(y, m, d, 12, 0, 0) - off);
  double jd = noon_utc / 86400.0 + 2440587.5;
  double n = floor(jd - 2451545.0 + 0.0008 + 0.5);
  double jstar = n - lon / 360.0;  // longitude east-positive
  double M = fmod(357.5291 + 0.98560028 * jstar, 360.0);
  double C = 1.9148 * sin(M * kDeg) + 0.0200 * sin(2 * M * kDeg) + 0.0003 * sin(3 * M * kDeg);
  double lambda = fmod(M + C + 180.0 + 102.9372, 360.0);
  double jtransit = 2451545.0 + jstar + 0.0053 * sin(M * kDeg) - 0.0069 * sin(2 * lambda * kDeg);
  double sin_dec = sin(lambda * kDeg) * sin(23.4397 * kDeg);
  double cos_dec = cos(asin(sin_dec));
  double cos_w = (sin(-0.833 * kDeg) - sin(lat * kDeg) * sin_dec) / (cos(lat * kDeg) * cos_dec);
  if (cos_w < -1.0 || cos_w > 1.0) return t;
  double w = acos(cos_w) / kDeg;
  t.rise = (int64_t)llround((jtransit - w / 360.0 - 2440587.5) * 86400.0);
  t.set = (int64_t)llround((jtransit + w / 360.0 - 2440587.5) * 86400.0);
  t.ok = true;
  return t;
}

static SunTimes sun_for_day(int64_t local_day_start_utc, int32_t off, float lat, float lon) {
  Civil c = civil_from_seconds(local_day_start_utc + off);
  SunTimes t = sun_times(c.year, c.month, c.day, off, lat, lon);
  if (!t.ok) {  // polar fallback
    int64_t midnight_utc = seconds_from_civil(c.year, c.month, c.day, 0, 0, 0) - off;
    t.rise = midnight_utc + 6 * 3600;
    t.set = midnight_utc + 18 * 3600;
  }
  return t;
}

Sky sky_at(int64_t utc, int32_t off, float lat, float lon) {
  Sky s;
  int64_t local = utc + off;
  Civil c = civil_from_seconds(local);
  int64_t midnight_utc = seconds_from_civil(c.year, c.month, c.day, 0, 0, 0) - off;
  SunTimes today = sun_for_day(midnight_utc + 12 * 3600, off, lat, lon);
  s.sunrise = today.rise;
  s.sunset = today.set;
  const int64_t M = 60;
  int64_t eleven = midnight_utc + 11 * 3600;

  if (utc >= today.rise - 40 * M && utc < today.rise + 60 * M) s.phase = Phase::Dawn;
  else if (utc >= today.rise + 60 * M && utc < eleven && eleven < today.set - 120 * M) s.phase = Phase::Morning;
  else if (utc >= today.rise + 60 * M && utc < today.set - 120 * M) s.phase = Phase::Midday;
  else if (utc >= today.set - 120 * M && utc < today.set - 10 * M) s.phase = Phase::Golden;
  else if (utc >= today.set - 10 * M && utc < today.set + 60 * M) s.phase = Phase::Dusk;
  else s.phase = Phase::Night;
  s.mode = mode_for(s.phase);

  if (s.phase == Phase::Night) {
    // Moon across the dark hours, from the last sunset to the next sunrise.
    int64_t from, to;
    if (utc < today.rise) {
      SunTimes yday = sun_for_day(midnight_utc - 12 * 3600, off, lat, lon);
      from = yday.set;
      to = today.rise;
    } else {
      SunTimes tmrw = sun_for_day(midnight_utc + 36 * 3600, off, lat, lon);
      from = today.set;
      to = tmrw.rise;
    }
    s.moon = true;
    s.p = to > from ? (float)(utc - from) / (float)(to - from) : 0.5f;
  } else {
    s.p = today.set > today.rise ? (float)(utc - today.rise) / (float)(today.set - today.rise) : 0.5f;
  }
  if (s.p < 0) s.p = 0;
  if (s.p > 1) s.p = 1;
  return s;
}

}  // namespace tc
