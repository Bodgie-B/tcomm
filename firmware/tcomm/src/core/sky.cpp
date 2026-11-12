#include "sky.h"
#include "civil.h"
#include <math.h>

namespace tc {

static constexpr double kDeg = 3.14159265358979323846 / 180.0;

// The sunrise equation for any sun altitude (degrees, negative below the horizon): the morning and
// evening times the sun crosses it. sun_times is altitude -0.833; golden hour (Mike, 2026-11-07)
// is altitude 6, a zenith of 84.
static SunTimes sun_at_altitude(int y, int m, int d, int32_t off, float lat, float lon, double alt_deg) {
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
  double cos_w = (sin(alt_deg * kDeg) - sin(lat * kDeg) * sin_dec) / (cos(lat * kDeg) * cos_dec);
  if (cos_w < -1.0 || cos_w > 1.0) return t;
  double w = acos(cos_w) / kDeg;
  t.rise = (int64_t)llround((jtransit - w / 360.0 - 2440587.5) * 86400.0);
  t.set = (int64_t)llround((jtransit + w / 360.0 - 2440587.5) * 86400.0);
  t.ok = true;
  return t;
}

SunTimes sun_times(int y, int m, int d, int32_t off, float lat, float lon) {
  return sun_at_altitude(y, m, d, off, lat, lon, -0.833);
}

SunTimes sun_times_at_zenith(int y, int m, int d, int32_t off, float lat, float lon, double zenith_deg) {
  return sun_at_altitude(y, m, d, off, lat, lon, 90.0 - zenith_deg);
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

// Mike, 2026-11-07: the golden phase starts when the sun drops to 6 degrees above the horizon in the
// afternoon (zenith 84), where it used to start a fixed 120 minutes before sunset. Never before
// 11:00 local; at 11:00 too when the sun never gets that high that day. Never after dusk begins.
static int64_t golden_start_for(int64_t midnight_utc, int32_t off, float lat, float lon, int64_t sunset) {
  Civil c = civil_from_seconds(midnight_utc + off + 12 * 3600);
  SunTimes g = sun_times_at_zenith(c.year, c.month, c.day, off, lat, lon, kGoldenZenithDeg);
  int64_t eleven = midnight_utc + 11 * 3600;
  int64_t start = g.ok && g.set >= eleven ? g.set : eleven;
  return start < sunset - 10 * 60 ? start : sunset - 10 * 60;
}

int64_t golden_start(int64_t utc, int32_t off, float lat, float lon) {
  Civil c = civil_from_seconds(utc + off);
  int64_t midnight_utc = seconds_from_civil(c.year, c.month, c.day, 0, 0, 0) - off;
  SunTimes today = sun_for_day(midnight_utc + 12 * 3600, off, lat, lon);
  return golden_start_for(midnight_utc, off, lat, lon, today.set);
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
  int64_t golden = golden_start_for(midnight_utc, off, lat, lon, today.set);

  if (utc >= today.rise - 40 * M && utc < today.rise + 60 * M) s.phase = Phase::Dawn;
  else if (utc >= today.rise + 60 * M && utc < eleven && utc < golden) s.phase = Phase::Morning;
  else if (utc >= today.rise + 60 * M && utc < golden) s.phase = Phase::Midday;
  else if (utc >= golden && utc < today.set - 10 * M) s.phase = Phase::Golden;
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

int64_t sunset_today(int64_t utc, int32_t off, float lat, float lon) {
  Civil c = civil_from_seconds(utc + off);
  SunTimes t = sun_times(c.year, c.month, c.day, off, lat, lon);
  return t.ok ? t.set : 0;
}

bool in_lookout_window(int64_t utc, int32_t off, float lat, float lon) {
  const int64_t set = sunset_today(utc, off, lat, lon);
  return set != 0 && utc >= set - kLookoutBeforeMin * 60 && utc < set + kLookoutAfterMin * 60;
}

// ---- Where the sun and the moon are ----

static double wrap360(double d) {
  d = fmod(d, 360.0);
  return d < 0 ? d + 360.0 : d;
}

// The sun's ecliptic longitude (degrees) and its mean anomaly (radians), n days after 2000-01-01
// 12:00 UTC. The usual low-precision almanac series.
static double sun_longitude(double n, double *anomaly) {
  double g = wrap360(357.528 + 0.9856003 * n) * kDeg;
  if (anomaly) *anomaly = g;
  return wrap360(280.460 + 0.9856474 * n + 1.915 * sin(g) + 0.020 * sin(2 * g));
}

// A point on the sky given by its ecliptic longitude and latitude (degrees), as seen from the
// place: to right ascension and declination, then with the local star time to az and alt.
static SkyPos horizon_from_ecliptic(double n, double lam_deg, double beta_deg, float lat, float lon) {
  double eps = (23.439 - 0.0000004 * n) * kDeg;
  double lam = lam_deg * kDeg, beta = beta_deg * kDeg;
  double ra = atan2(sin(lam) * cos(eps) - tan(beta) * sin(eps), cos(lam));
  double dec = asin(sin(beta) * cos(eps) + cos(beta) * sin(eps) * sin(lam));
  double star_time = wrap360(280.46061837 + 360.98564736629 * n + lon) * kDeg;
  double h = star_time - ra;  // hour angle: how far west of due south
  double phi = lat * kDeg;
  double sin_alt = sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(h);
  if (sin_alt > 1.0) sin_alt = 1.0;
  if (sin_alt < -1.0) sin_alt = -1.0;
  double az = atan2(-cos(dec) * sin(h), sin(dec) * cos(phi) - cos(dec) * sin(phi) * cos(h));
  SkyPos p;
  p.alt = (float)(asin(sin_alt) / kDeg);
  p.az = (float)wrap360(az / kDeg);
  return p;
}

SkyPos sun_position(int64_t utc, float lat, float lon) {
  double n = (double)utc / 86400.0 + 2440587.5 - 2451545.0;
  return horizon_from_ecliptic(n, sun_longitude(n, nullptr), 0.0, lat, lon);
}

MoonSky moon_sky(int64_t utc, float lat, float lon) {
  double n = (double)utc / 86400.0 + 2440587.5 - 2451545.0;
  double sun_anom = 0;
  double sun_lam = sun_longitude(n, &sun_anom);
  // The moon's mean longitude, mean anomaly, distance from its node and mean distance from the
  // sun, then the five biggest wobbles of its longitude and the biggest of its latitude.
  double L = 218.316 + 13.176396 * n;
  double M = wrap360(134.963 + 13.064993 * n) * kDeg;
  double F = wrap360(93.272 + 13.229350 * n) * kDeg;
  double D = wrap360(297.850 + 12.190749 * n) * kDeg;
  double lam = wrap360(L + 6.289 * sin(M) - 1.274 * sin(M - 2 * D) + 0.658 * sin(2 * D) - 0.186 * sin(sun_anom) -
                       0.114 * sin(2 * F));
  double beta = 5.128 * sin(F);
  MoonSky m;
  m.pos = horizon_from_ecliptic(n, lam, beta, lat, lon);
  // The moon is close enough that standing on the Earth's surface lowers it, by up to a degree at
  // the horizon.
  m.pos.alt -= (float)(0.95 * cos(m.pos.alt * kDeg));
  double elong = wrap360(lam - sun_lam);
  m.phase = (float)(elong / 360.0);
  m.lit = (float)((1.0 - cos(elong * kDeg)) / 2.0);
  return m;
}

}  // namespace tc
