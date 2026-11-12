// The house's weather as the scene's weather. See house_weather.h.
#include "house_weather.h"

namespace ss {

namespace {

// ---------------------------------------------------------------------------
// The mapping; change it here and nothing else needs to know.
//
// First, by the forecast's words (Mike approved these groups, 2026-11-11). One
// letter per Zambretti text, A to Z, in firmware core/zambretti.cpp's order:
//   t storm (rain, lightning, the darkest sky):  S "Stormy, Much Rain"
//   r rain (a darker sky, heavy cloud):          G "Rain at Times, Worse Later", H "Rain at Times,
//       Becoming Very Unsettled", I and R "Very Unsettled, Rain", Q "Rain at Frequent Intervals"
//   g gray and dry (heavy cloud, no rain):       E "Showery, Becoming More Unsettled", F "Unsettled,
//       Rain Later", N "Showery, Bright Intervals", O "Changeable, Some Rain", P "Unsettled, Rain at Times"
//   s some cloud, dry:                           C "Fine, Becoming Less Settled", D "Fairly Fine,
//       Showery Later", L "Fine, Possibly Showers", M "Fairly Fine, Showers Likely", X "Fairly Fine,
//       Possibly Showers", Z "Changeable, Mending"
//   f fine or clearing: the forecast code's row below, as before (A B J K T U V W Y)
//                          ABCDEFGHIJKLMNOPQRSTUVWXYZ
const char kPhraseGroup[] = "ffssggrrrffssgggrrtffffsfs";

struct PhraseRow {
  char group;
  int code;         // SceneWeatherCode
  float overcast;
  bool dry;
};

const PhraseRow kPhrases[] = {
    {'t', SWC_STORM, 1.0f, false},
    {'r', SWC_RAIN, 0.9f, false},
    {'g', SWC_CLEAR, 0.7f, true},
    {'s', SWC_CLEAR, 0.3f, true},
};

// Then, for a text that is not one of the 26 (or a fine one), by the icon.
// One row per forecast code: the scene code it plays, the overcast it sets
// (0..1), and whether the sky is dry (the temperature and humidity may then
// override it, see kOverrides). A forecast code with no row is treated as none.
// ---------------------------------------------------------------------------
struct ForecastRow {
  int forecast;     // OutsideForecastCode
  int code;         // SceneWeatherCode
  float overcast;
  bool dry;
};

const ForecastRow kForecasts[] = {
    {OFC_SUNNY, SWC_CLEAR, 0.0f, true},
    {OFC_PARTLY_CLOUDY, SWC_CLEAR, 0.3f, true},
    {OFC_CLEARING, SWC_CLEAR, 0.2f, true},
    {OFC_CLOUDY, SWC_CLEAR, 0.6f, true},
    {OFC_RAIN, SWC_RAIN, 0.7f, false},
    {OFC_HEAVY_RAIN, SWC_RAIN, 0.9f, false},
    {OFC_STORM, SWC_STORM, 1.0f, false},
    {OFC_SNOW, SWC_SNOW, 0.6f, false},   // but rain above kSnowTurnsToRainF, with the same overcast
};

// Snow forecast and warmer than this (degrees F, strictly): it is rain.
const float kSnowTurnsToRainF = 38.0f;

// For a dry forecast, the first of these whose test passes wins (so fog before
// heat before chill). Temperatures in degrees F, humidity in percent, all
// limits inclusive; kAny is "no limit". `fog` is the fog tint it sets (the
// overcast stays the forecast's).
const float kAny = 1000.0f;

struct Override {
  float temp_min, temp_max;
  float humidity_min;
  int code;
  float fog;
};

const Override kOverrides[] = {
    {-kAny, 60.0f, 95.0f, SWC_FOG, 0.6f},    // damp and not hot: fog
    {88.0f, kAny, 0.0f, SWC_HEAT, 0.0f},     // hot: heat
    {-kAny, 40.0f, 0.0f, SWC_CHILL, 0.0f},   // cold: chill
};

// There is no wind row: an OUTSIDE reading has no wind (temperature, humidity
// and the forecast code only), so SWC_WIND never comes from data. The core
// plays it only when it picks its own weather.

}  // namespace

SceneWeather sceneWeatherFromOutside(const OutsideReading& r) {
  const SceneWeather none = {SWC_CLEAR, 0.0f, 0.0f, false};
  if (!r.valid) return none;

  // A reading we can't place (no forecast, the boards' night, an unknown code and
  // unknown words) is still a reading: a clear, dry sky, never random weather.
  SceneWeather out = {SWC_CLEAR, 0.0f, 0.0f, true};
  bool dry = true;
  const char group = r.phrase >= 'A' && r.phrase <= 'Z' ? kPhraseGroup[r.phrase - 'A'] : 'f';
  const PhraseRow* words = nullptr;
  for (const PhraseRow& p : kPhrases) {
    if (p.group == group) words = &p;
  }
  if (words) {
    out.code = words->code;
    out.overcast = words->overcast;
    dry = words->dry;
  } else {
    for (const ForecastRow& f : kForecasts) {
      if (f.forecast != r.forecast) continue;
      out.code = f.code;
      out.overcast = f.overcast;
      dry = f.dry;
      if (f.forecast == OFC_SNOW && r.temp_f > kSnowTurnsToRainF) out.code = SWC_RAIN;
      break;
    }
  }
  if (dry) {
    bool matched = false;
    for (const Override& o : kOverrides) {
      // Written as "inside", so a temperature or humidity that is not a number matches nothing.
      if (!(r.temp_f >= o.temp_min && r.temp_f <= o.temp_max && r.humidity >= o.humidity_min)) continue;
      out.code = o.code;
      out.fog = o.fog;
      matched = true;
      break;
    }
    // The gateway's heat warning (heat index 100 F or more) can come below 88 F
    // on a humid day: it plays heat too. Its cold warning (20 F or less) is
    // always under the chill row's 40 F, so it adds nothing.
    if (!matched && r.danger_heat) out.code = SWC_HEAT;
  }
  return out;
}

}  // namespace ss
