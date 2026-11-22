#include "dog.h"

#include <cmath>

namespace tc {

int16_t feels_like_tenths(int16_t temp_tenths_f, uint8_t hum_pct) {
  const double t = temp_tenths_f / 10.0, rh = hum_pct > 100 ? 100.0 : (double)hum_pct;
  if (t < 70.0) return temp_tenths_f;   // the heat index only ever adds heat, from about 80 F up
  // NWS: the simple formula first; the full Rothfusz regression (with its two corrections) when
  // that comes out at 80 F or more.
  double hi = 0.5 * (t + 61.0 + (t - 68.0) * 1.2 + rh * 0.094);
  if ((hi + t) / 2.0 >= 80.0) {
    hi = -42.379 + 2.04901523 * t + 10.14333127 * rh - 0.22475541 * t * rh - 0.00683783 * t * t -
         0.05481717 * rh * rh + 0.00122874 * t * t * rh + 0.00085282 * t * rh * rh - 0.00000199 * t * t * rh * rh;
    if (rh < 13.0 && t >= 80.0 && t <= 112.0) hi -= (13.0 - rh) / 4.0 * std::sqrt((17.0 - std::fabs(t - 95.0)) / 17.0);
    else if (rh > 85.0 && t >= 80.0 && t <= 87.0) hi += (rh - 85.0) / 10.0 * (87.0 - t) / 5.0;
  } else {
    hi = (hi + t) / 2.0;
  }
  if (hi < t) hi = t;   // never "feels" cooler than the air in the heat
  const double r = std::floor(hi * 10.0 + 0.5);
  return (int16_t)(r > 32767.0 ? 32767.0 : r);
}

int dog_limit_min(int dog, int16_t feels_tenths_f) {
  // Whole degrees, rounded half away from zero (the pill rounds the same way).
  const int f = feels_tenths_f >= 0 ? (feels_tenths_f + 5) / 10 : -((-feels_tenths_f + 5) / 10);
  const bool lucy = dog == 0;
  if (f >= 105) return 15;
  if (f >= 100) return lucy ? 25 : 30;
  if (f >= 95) return lucy ? 40 : 45;
  if (f >= 90) return 60;
  if (f >= 45) return 0;
  if (f >= 35) return 60;
  if (f >= 25) return 30;
  if (f >= 15) return 20;
  if (f >= 5) return 10;
  return 5;
}

bool DogModel::tap(Floor me, const WallTime &now) {
  DogOne &d = s_.dog[dog_index(own_dog(me))];
  d.out = !d.out;
  d.since = now.valid ? now.utc : 0;
  d.ver++;
  return true;
}

static bool newer(const DogOne &in, const DogOne &cur) {
  if (in.ver != cur.ver) return in.ver > cur.ver;
  if (in.since != cur.since) return in.since > cur.since;
  return in.out && !cur.out;  // a true tie: both panels keep the same one
}

bool DogModel::apply_remote(const DogState &in) {
  bool changed = false;
  for (int i = 0; i < 2; i++)
    if (!(in.dog[i] == s_.dog[i]) && newer(in.dog[i], s_.dog[i])) {
      s_.dog[i] = in.dog[i];
      changed = true;
    }
  return changed;
}

bool DogModel::apply_lucy(bool out, uint32_t since) {
  DogOne &lucy = s_.dog[0];
  if (lucy.out == out) return false;  // already out, or already in
  lucy.out = out;
  lucy.since = since;
  lucy.ver++;
  return true;
}

}  // namespace tc
