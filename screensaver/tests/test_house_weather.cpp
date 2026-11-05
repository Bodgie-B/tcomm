// The house's weather as the scene's (core/house_weather.*): the proposed
// mapping from an OUTSIDE reading to Inputs' weather code, overcast and fog.
//   - a table over every forecast code (0 to 9, the unknown 6, 42, -1) at mild
//     weather, then the temperature and humidity edges: snow turns to rain
//     above 38 F, a dry forecast turns to heat at 88 F, chill at 40 F, fog at
//     95 percent humidity and 60 F, in that order of precedence;
//   - wet forecasts (rain, heavy rain, storm, snow) are never overridden;
//   - a sweep: wind never comes from data, every tint is 0..1, have_outside is
//     exactly "valid and a known forecast";
//   - every code it produces is one the scene maps to the kind it means
//     (scene/weather.cpp kCodeKinds).
// Plain executable; returns non-zero on failure.
#include "house_weather.h"
#include "scene/weather.h"

#include <math.h>
#include <stdio.h>

using namespace ss;

static int g_fail = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

struct Case {
  const char* name;
  bool valid;
  int forecast;
  float temp_f, humidity;
  // expected
  bool have;
  int code;
  float overcast, fog;
};

// Mild and middling: no override applies.
static const float kT = 70.0f;
static const float kH = 50.0f;

static const Case kCases[] = {
    // No reading, no forecast, or a forecast we do not know: the core picks its own weather.
    {"not valid, rain", false, OFC_RAIN, kT, kH, false, SWC_CLEAR, 0.0f, 0.0f},
    {"not valid, storm, cold and damp", false, OFC_STORM, 20.0f, 99.0f, false, SWC_CLEAR, 0.0f, 0.0f},
    {"forecast 0 (none)", true, 0, kT, kH, false, SWC_CLEAR, 0.0f, 0.0f},
    {"forecast 0, hot", true, 0, 100.0f, kH, false, SWC_CLEAR, 0.0f, 0.0f},
    {"unknown 6 (the boards' night)", true, 6, kT, kH, false, SWC_CLEAR, 0.0f, 0.0f},
    {"unknown 42", true, 42, kT, kH, false, SWC_CLEAR, 0.0f, 0.0f},
    {"unknown 10", true, 10, kT, kH, false, SWC_CLEAR, 0.0f, 0.0f},
    {"unknown -1", true, -1, kT, kH, false, SWC_CLEAR, 0.0f, 0.0f},
    {"unknown 6, cold", true, 6, 20.0f, kH, false, SWC_CLEAR, 0.0f, 0.0f},

    // Every forecast code at mild weather.
    {"1 sunny", true, OFC_SUNNY, kT, kH, true, SWC_CLEAR, 0.0f, 0.0f},
    {"2 partly_cloudy", true, OFC_PARTLY_CLOUDY, kT, kH, true, SWC_CLEAR, 0.3f, 0.0f},
    {"3 cloudy", true, OFC_CLOUDY, kT, kH, true, SWC_CLEAR, 0.6f, 0.0f},
    {"4 rain", true, OFC_RAIN, kT, kH, true, SWC_RAIN, 0.7f, 0.0f},
    {"5 snow, cold", true, OFC_SNOW, 30.0f, kH, true, SWC_SNOW, 0.6f, 0.0f},
    {"7 storm", true, OFC_STORM, kT, kH, true, SWC_STORM, 1.0f, 0.0f},
    {"8 clearing", true, OFC_CLEARING, kT, kH, true, SWC_CLEAR, 0.2f, 0.0f},
    {"9 heavy_rain", true, OFC_HEAVY_RAIN, kT, kH, true, SWC_RAIN, 0.9f, 0.0f},

    // Snow turns to rain above 38 F (strictly), keeping the snow forecast's overcast.
    {"snow at 38", true, OFC_SNOW, 38.0f, kH, true, SWC_SNOW, 0.6f, 0.0f},
    {"snow at 38.1", true, OFC_SNOW, 38.1f, kH, true, SWC_RAIN, 0.6f, 0.0f},
    {"snow at 39", true, OFC_SNOW, 39.0f, kH, true, SWC_RAIN, 0.6f, 0.0f},
    {"snow at 37", true, OFC_SNOW, 37.0f, kH, true, SWC_SNOW, 0.6f, 0.0f},
    {"snow at 5", true, OFC_SNOW, 5.0f, kH, true, SWC_SNOW, 0.6f, 0.0f},
    {"snow at 70, humid", true, OFC_SNOW, 70.0f, 99.0f, true, SWC_RAIN, 0.6f, 0.0f},

    // Heat: a dry forecast at 88 F and up.
    {"sunny at 87.9", true, OFC_SUNNY, 87.9f, kH, true, SWC_CLEAR, 0.0f, 0.0f},
    {"sunny at 88", true, OFC_SUNNY, 88.0f, kH, true, SWC_HEAT, 0.0f, 0.0f},
    {"sunny at 100", true, OFC_SUNNY, 100.0f, kH, true, SWC_HEAT, 0.0f, 0.0f},
    {"partly_cloudy at 88 keeps its overcast", true, OFC_PARTLY_CLOUDY, 88.0f, kH, true, SWC_HEAT, 0.3f, 0.0f},
    {"cloudy at 90, humid (fog wants 60 F or less)", true, OFC_CLOUDY, 90.0f, 99.0f, true, SWC_HEAT, 0.6f, 0.0f},
    {"clearing at 88", true, OFC_CLEARING, 88.0f, kH, true, SWC_HEAT, 0.2f, 0.0f},

    // Chill: a dry forecast at 40 F and under.
    {"sunny at 40.1", true, OFC_SUNNY, 40.1f, kH, true, SWC_CLEAR, 0.0f, 0.0f},
    {"sunny at 40", true, OFC_SUNNY, 40.0f, kH, true, SWC_CHILL, 0.0f, 0.0f},
    {"sunny at 39.9", true, OFC_SUNNY, 39.9f, kH, true, SWC_CHILL, 0.0f, 0.0f},
    {"sunny at 10", true, OFC_SUNNY, 10.0f, kH, true, SWC_CHILL, 0.0f, 0.0f},
    {"cloudy at 30 keeps its overcast", true, OFC_CLOUDY, 30.0f, kH, true, SWC_CHILL, 0.6f, 0.0f},
    {"clearing at 40", true, OFC_CLEARING, 40.0f, kH, true, SWC_CHILL, 0.2f, 0.0f},

    // Fog: a dry forecast, humidity 95 percent and up, 60 F and under.
    {"sunny at 55, humidity 94.9", true, OFC_SUNNY, 55.0f, 94.9f, true, SWC_CLEAR, 0.0f, 0.0f},
    {"sunny at 55, humidity 95", true, OFC_SUNNY, 55.0f, 95.0f, true, SWC_FOG, 0.0f, 0.6f},
    {"sunny at 55, humidity 100", true, OFC_SUNNY, 55.0f, 100.0f, true, SWC_FOG, 0.0f, 0.6f},
    {"sunny at 60, humidity 95", true, OFC_SUNNY, 60.0f, 95.0f, true, SWC_FOG, 0.0f, 0.6f},
    {"sunny at 60.1, humidity 95", true, OFC_SUNNY, 60.1f, 95.0f, true, SWC_CLEAR, 0.0f, 0.0f},
    {"cloudy at 50, humidity 96 keeps its overcast", true, OFC_CLOUDY, 50.0f, 96.0f, true, SWC_FOG, 0.6f, 0.6f},
    {"partly_cloudy at 50, humidity 99", true, OFC_PARTLY_CLOUDY, 50.0f, 99.0f, true, SWC_FOG, 0.3f, 0.6f},

    // Precedence: fog before chill (cold and damp is fog), and where fog's
    // humidity is not met the cold wins.
    {"sunny at 38, humidity 97: fog before chill", true, OFC_SUNNY, 38.0f, 97.0f, true, SWC_FOG, 0.0f, 0.6f},
    {"sunny at 40, humidity 94.9: chill", true, OFC_SUNNY, 40.0f, 94.9f, true, SWC_CHILL, 0.0f, 0.0f},

    // Wet forecasts are never overridden, and a wet forecast has no fog.
    {"rain at 95, humidity 99", true, OFC_RAIN, 95.0f, 99.0f, true, SWC_RAIN, 0.7f, 0.0f},
    {"rain at 35", true, OFC_RAIN, 35.0f, kH, true, SWC_RAIN, 0.7f, 0.0f},
    {"rain at 50, humidity 99", true, OFC_RAIN, 50.0f, 99.0f, true, SWC_RAIN, 0.7f, 0.0f},
    {"heavy_rain at 90", true, OFC_HEAVY_RAIN, 90.0f, kH, true, SWC_RAIN, 0.9f, 0.0f},
    {"storm at 25", true, OFC_STORM, 25.0f, kH, true, SWC_STORM, 1.0f, 0.0f},
    {"storm at 95, humidity 99", true, OFC_STORM, 95.0f, 99.0f, true, SWC_STORM, 1.0f, 0.0f},
    {"snow at 20, humidity 99", true, OFC_SNOW, 20.0f, 99.0f, true, SWC_SNOW, 0.6f, 0.0f},

    // Dry forecasts at the same hot, cold and damp readings.
    {"sunny hot", true, OFC_SUNNY, 95.0f, 20.0f, true, SWC_HEAT, 0.0f, 0.0f},
    {"partly_cloudy cold", true, OFC_PARTLY_CLOUDY, 25.0f, 20.0f, true, SWC_CHILL, 0.3f, 0.0f},
    {"clearing damp", true, OFC_CLEARING, 45.0f, 97.0f, true, SWC_FOG, 0.2f, 0.6f},
    {"cloudy hot", true, OFC_CLOUDY, 92.0f, 20.0f, true, SWC_HEAT, 0.6f, 0.0f},
};

static bool near(float a, float b) { return fabsf(a - b) < 1e-6f; }

static void testTable() {
  for (const Case& c : kCases) {
    OutsideReading r = {};
    r.valid = c.valid;
    r.forecast = c.forecast;
    r.temp_f = c.temp_f;
    r.humidity = c.humidity;
    const SceneWeather w = sceneWeatherFromOutside(r);
    CHECK(w.have_outside == c.have, "%s: have_outside %d, want %d", c.name, (int)w.have_outside, (int)c.have);
    CHECK(w.code == c.code, "%s: code %d, want %d", c.name, w.code, c.code);
    CHECK(near(w.overcast, c.overcast), "%s: overcast %.2f, want %.2f", c.name, (double)w.overcast, (double)c.overcast);
    CHECK(near(w.fog, c.fog), "%s: fog %.2f, want %.2f", c.name, (double)w.fog, (double)c.fog);
  }
  printf("house weather: %d table cases\n", (int)(sizeof kCases / sizeof kCases[0]));
}

// The heat warning plays heat on a dry sky with no other override; the cold
// warning changes nothing (the chill row already covers 20 F and under).
static void testFlags() {
  OutsideReading r = {};
  r.valid = true;
  r.forecast = OFC_SUNNY;
  r.temp_f = kT;
  r.humidity = kH;
  const SceneWeather base = sceneWeatherFromOutside(r);
  r.danger_cold = true;
  const SceneWeather cold = sceneWeatherFromOutside(r);
  CHECK(base.code == cold.code && near(base.overcast, cold.overcast) && near(base.fog, cold.fog) &&
            base.have_outside == cold.have_outside,
        "the cold warning changed the mapping");
  r.danger_cold = false;
  r.danger_heat = true;
  r.temp_f = 86.0f;   // under the heat row, humid enough for a heat index of 100
  CHECK(sceneWeatherFromOutside(r).code == SWC_HEAT, "the heat warning on a dry sky didn't play heat");
  r.forecast = OFC_RAIN;
  CHECK(sceneWeatherFromOutside(r).code == SWC_RAIN, "the heat warning overrode rain");
}

// Over every forecast code from -2 to 12, -20 to 120 F and 0 to 100 percent:
// no wind, tints in range, have_outside exactly "valid and known", and the
// scene reads each code as the kind it names.
static void testSweep() {
  int n = 0, wind = 0, range = 0, flag = 0, kind = 0;
  for (int valid = 0; valid < 2; ++valid)
    for (int f = -2; f <= 12; ++f)
      for (int t = -40; t <= 240; ++t)           // half degrees: -20 .. 120 F
        for (int h = 0; h <= 20; ++h) {          // 5 percent steps
          OutsideReading r = {};
          r.valid = valid != 0;
          r.forecast = f;
          r.temp_f = (float)t * 0.5f;
          r.humidity = (float)h * 5.0f;
          const SceneWeather w = sceneWeatherFromOutside(r);
          ++n;
          const bool known = f == 1 || f == 2 || f == 3 || f == 4 || f == 5 || f == 7 || f == 8 || f == 9;
          if (w.code == SWC_WIND) ++wind;
          if (!(w.overcast >= 0.0f && w.overcast <= 1.0f && w.fog >= 0.0f && w.fog <= 1.0f)) ++range;
          if (w.have_outside != (valid && known)) ++flag;
          if (!w.have_outside && (w.code != SWC_CLEAR || w.overcast != 0.0f || w.fog != 0.0f)) ++flag;
          static const uint8_t kWant[8] = {WX_CLEAR, WX_RAIN, WX_SNOW, WX_WIND, WX_HEAT, WX_FOG, WX_CHILL, WX_STORM};
          if (w.code < 0 || w.code > 7 || weatherKindForCode(w.code) != kWant[w.code]) ++kind;
        }
  printf("house weather: %d readings swept\n", n);
  CHECK(wind == 0, "%d readings gave wind: an OUTSIDE reading has no wind", wind);
  CHECK(range == 0, "%d readings gave a tint outside 0..1", range);
  CHECK(flag == 0, "%d readings had the wrong have_outside, or a tint without data", flag);
  CHECK(kind == 0, "%d readings gave a code the scene reads as another kind", kind);
}

int main() {
  testTable();
  testFlags();
  testSweep();
  if (g_fail) {
    printf("test_house_weather: %d FAILED\n", g_fail);
    return 1;
  }
  printf("test_house_weather: ok\n");
  return 0;
}
