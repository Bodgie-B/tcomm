// Stills of the real sky for Mike's review (stage 12: the sun's arc, the moon's phases, the rain).
//
// It does what the panel does: the firmware's own sky maths (firmware/tcomm/src/core/sky.cpp) for
// the settings' placeholder place, the mood from the same sky phases blended the way ss_host.cpp
// blends them, the weather from the forecast's words through house_weather. Then it runs the
// scene to a settled moment and writes one frame. Nothing here knows the house's real place.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I screensaver/core -I firmware/tcomm/src
//     screensaver/tools/sky_stills.cpp firmware/tcomm/src/core/sky.cpp firmware/tcomm/src/core/civil.cpp
//     firmware/tcomm/src/core/zambretti.cpp BUILD/libss_core.a -o BUILD/sky_stills
//   BUILD/sky_stills OUT_DIR          the stills, as OUT_DIR/NN_name.ppm, and OUT_DIR/tiles.txt
//   BUILD/sky_stills --cost           what following the sun costs: a simulated half hour
//   python -I screensaver/tools/sky_sheet.py OUT_DIR docs/screensaver/reviews/stage12-sky
//
// tiles.txt has one line per still, "file|label"; the labels are generic ("mid-morning"), never
// clock times.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>

#include "core/civil.h"
#include "core/settings.h"
#include "core/sky.h"
#include "core/zambretti.h"
#include "house_weather.h"
#include "ss_app.h"

using namespace ss;

namespace {

const tc::Settings kPlace;                  // the placeholder place every PC build uses
constexpr int32_t kOff = -4 * 3600;         // its clock offset on the day below (as tests/test_core.cpp)
constexpr int kDay[3] = {2026, 3, 20};      // the clear day: an equinox, so the arc is the middle one
constexpr uint64_t kSeed = 7;
constexpr int kSettleTicks = 2600;          // past the build-up, the camp in place

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }

// ss_host.cpp phaseMood: the sky phase, averaged over 12 minutes either side of a change.
float moodAt(int64_t utc) {
  const int c = (int)tc::sky_at(utc, kOff, kPlace.latitude, kPlace.longitude).phase;
  float sum = 0.f;
  int n = 0;
  for (int k = -12; k <= 12; ++k) {
    int p = (int)tc::sky_at(utc + 60 * k, kOff, kPlace.latitude, kPlace.longitude).phase;
    if (p - c > 3) p -= 6;
    else if (c - p > 3) p += 6;
    sum += (float)p;
    ++n;
  }
  float m = sum / (float)n;
  if (m < 0.f) m += 6.f;
  if (m >= 6.f) m -= 6.f;
  return m;
}

SkyInputs skyAt(int64_t utc) {
  const tc::SkyPos sun = tc::sun_position(utc, kPlace.latitude, kPlace.longitude);
  const tc::MoonSky moon = tc::moon_sky(utc, kPlace.latitude, kPlace.longitude);
  SkyInputs s;
  memset(&s, 0, sizeof s);
  s.have = true;
  s.sun_az = sun.az;
  s.sun_alt = sun.alt;
  s.moon_az = moon.pos.az;
  s.moon_alt = moon.pos.alt;
  s.moon_phase = moon.phase;
  return s;
}

// What the panel would hand the scene at that moment, with a forecast text (nullptr: fine weather).
Inputs inputsAt(int64_t utc, const char* phrase, float rain_dark) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = moodAt(utc);
  in.sky = skyAt(utc);
  OutsideReading r = {};
  r.valid = true;
  r.phrase = tc::zambretti_letter(phrase ? phrase : "Settled Fine");
  r.forecast = (int)tc::zambretti_symbol(r.phrase);
  r.temp_f = 62.f;
  r.humidity = 55.f;
  const SceneWeather sw = sceneWeatherFromOutside(r);
  in.weather_code = sw.code;
  in.have_outside = sw.have_outside;
  in.weather.overcast = sw.overcast;
  in.weather.fog = sw.fog;
  in.weather.rain_dark = rain_dark;
  in.weather.night_gain = 1.6f;   // the panel setting (ss_host.cpp g_night_gain): the PC default is too dark to judge a moon by
  in.night_mode = tc::sky_at(utc, kOff, kPlace.latitude, kPlace.longitude).mode == tc::Mode::Night;
  in.chip = true;
  return in;
}

Pixel g_fb[SS_WIDTH * SS_HEIGHT];

bool writePPM(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  static uint8_t rgb[SS_WIDTH * SS_HEIGHT * 3];
  const size_t n = (size_t)SS_WIDTH * SS_HEIGHT;
  for (size_t i = 0; i < n; i++) {
    rgb[i * 3 + 0] = red8(g_fb[i]);
    rgb[i * 3 + 1] = green8(g_fb[i]);
    rgb[i * 3 + 2] = blue8(g_fb[i]);
  }
  bool ok = fprintf(f, "P6\n%d %d\n255\n", SS_WIDTH, SS_HEIGHT) > 0 && fwrite(rgb, 1, n * 3, f) == n * 3;
  return fclose(f) == 0 && ok;
}

struct Host {
  HostAlloc alloc;
  void* mem;
  App* app;
  Surface fb;
};

bool hostStart(Host& h, const Inputs& in) {
  h.alloc.alloc_large = hostAlloc;
  h.alloc.free_large = hostFree;
  h.alloc.fast = nullptr;
  h.mem = malloc(SS_APP_BYTES);
  h.app = h.mem ? appStart(h.mem, h.alloc, kSeed, in) : nullptr;
  h.fb = Surface{g_fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  return h.app != nullptr;
}

void hostStop(Host& h) {
  if (h.app) appStop(h.app, h.alloc);
  free(h.mem);
}

int g_n = 0;
FILE* g_tiles = nullptr;
const char* g_out = ".";

void still(const char* name, const char* label, const Inputs& in, int ticks = kSettleTicks) {
  Host h;
  if (!hostStart(h, in)) {
    printf("could not start the scene\n");
    exit(1);
  }
  for (int i = 0; i < ticks; ++i) appTick(h.app);
  appRender(h.app, h.fb);
  char file[64], path[512];
  snprintf(file, sizeof file, "%02d_%s.ppm", ++g_n, name);
  snprintf(path, sizeof path, "%s/%s", g_out, file);
  if (!writePPM(path)) {
    printf("could not write %s\n", path);
    exit(1);
  }
  fprintf(g_tiles, "%s|%s\n", file, label);
  printf("%s: %s (sun %.0f up, moon %.0f up, mood %.2f)\n", file, label, (double)in.sky.sun_alt,
         (double)in.sky.moon_alt, (double)in.mood);
  hostStop(h);
}

int64_t localMidnight(int y, int m, int d) { return tc::seconds_from_civil(y, m, d, 0, 0, 0) - kOff; }

// A full-night moment (the palette at full night) with the moon near `phase`, at least `min_alt`
// degrees up and nearest the compass bearing `want_az`. Looks through the months after the clear
// day; 0 when there is none.
int64_t findMoonNight(float phase, float min_alt, float want_az) {
  int64_t best = 0;
  float best_d = 1e9f;
  const int64_t t0 = localMidnight(kDay[0], kDay[1], kDay[2]);
  for (int64_t t = t0; t < t0 + 120LL * 86400; t += 600) {
    const tc::MoonSky m = tc::moon_sky(t, kPlace.latitude, kPlace.longitude);
    float dp = fabsf(m.phase - phase);
    if (dp > 0.5f) dp = 1.f - dp;
    if (dp > 0.012f || m.pos.alt < min_alt) continue;
    if (tc::sun_position(t, kPlace.latitude, kPlace.longitude).alt > -14.f) continue;
    if (moodAt(t) != 0.f) continue;
    const float d = fabsf(m.pos.az - want_az);
    if (d < best_d) {
      best_d = d;
      best = t;
    }
  }
  return best;
}

int runStills(const char* out) {
  g_out = out;
  char path[512];
  snprintf(path, sizeof path, "%s/tiles.txt", out);
  g_tiles = fopen(path, "w");
  if (!g_tiles) {
    printf("could not write %s (does the folder exist?)\n", path);
    return 1;
  }
  const tc::SunTimes st = tc::sun_times(kDay[0], kDay[1], kDay[2], kOff, kPlace.latitude, kPlace.longitude);
  const int64_t len = st.set - st.rise;
  auto at = [&](double f) { return st.rise + (int64_t)((double)len * f); };

  // The sun through a clear day.
  still("sun_sunrise", "sunrise", inputsAt(st.rise + 18 * 60, nullptr, 0.f));
  still("sun_early_morning", "early morning", inputsAt(at(0.13), nullptr, 0.f));
  still("sun_mid_morning", "mid-morning", inputsAt(at(0.30), nullptr, 0.f));
  still("sun_midday", "midday", inputsAt(at(0.50), nullptr, 0.f));
  still("sun_mid_afternoon", "mid-afternoon", inputsAt(at(0.68), nullptr, 0.f));
  still("sun_late_afternoon", "late afternoon", inputsAt(at(0.83), nullptr, 0.f));
  still("sun_golden_hour", "golden hour",
        inputsAt(tc::golden_start(st.rise + len / 2, kOff, kPlace.latitude, kPlace.longitude) + 8 * 60, nullptr, 0.f));
  still("sun_sunset", "sunset", inputsAt(st.set - 6 * 60, nullptr, 0.f));

  // The moon through a month of clear nights, in a different part of the sky each time.
  struct MoonTile {
    const char* name;
    const char* label;
    float phase, min_alt, want_az;
  };
  const MoonTile moons[] = {
      {"moon_none", "no moon (new): more stars", 0.00f, -90.f, 0.f},
      {"moon_waxing_crescent", "waxing crescent, in the west", 0.11f, 9.f, 270.f},
      {"moon_first_quarter", "first quarter, south-west", 0.25f, 20.f, 230.f},
      {"moon_waxing_gibbous", "waxing gibbous, south-east", 0.38f, 20.f, 140.f},
      {"moon_full", "full moon, in the south", 0.50f, 20.f, 180.f},
      {"moon_last_quarter", "last quarter, rising in the east", 0.75f, 12.f, 100.f},
      {"moon_waning_crescent", "waning crescent, before dawn", 0.89f, 9.f, 110.f},
  };
  for (const MoonTile& m : moons) {
    const int64_t t = findMoonNight(m.phase, m.min_alt, m.want_az);
    if (!t) {
      printf("no night found for %s\n", m.name);
      return 1;
    }
    still(m.name, m.label, inputsAt(t, nullptr, 0.f));
  }

  // The weather by the forecast's words, mid-morning on the same day. The three rain levels are
  // the choices for kRainDark (core/scene/weather.cpp).
  const int64_t wx = at(0.30);
  still("gray_dry", "gray and dry (\"Unsettled, Rain at Times\")", inputsAt(wx, "Unsettled, Rain at Times", 0.f));
  still("rain_light", "rain: light (0.25)", inputsAt(wx, "Rain at Frequent Intervals", 0.25f));
  still("rain_medium", "rain: medium (0.45)", inputsAt(wx, "Rain at Frequent Intervals", 0.45f));
  still("rain_dark", "rain: dark (0.65)", inputsAt(wx, "Rain at Frequent Intervals", 0.65f));
  still("storm", "storm (\"Stormy, Much Rain\")", inputsAt(wx, "Stormy, Much Rain", 0.f));
  fclose(g_tiles);
  return 0;
}

// What following the sun costs: half an hour of a clear afternoon, the inputs refreshed every 30
// seconds as the panel does, a frame drawn every tick; then the same half hour with the old
// parked sky. The time is this PC's, so only the comparison means anything.
uint32_t clockUs() {
  using namespace std::chrono;
  return (uint32_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

void costRun(bool real, int64_t t0) {
  Inputs in = inputsAt(t0, nullptr, 0.f);
  if (!real) memset(&in.sky, 0, sizeof in.sky);
  Host h;
  if (!hostStart(h, in)) exit(1);
  appSetClock(h.app, clockUs);
  for (int i = 0; i < kSettleTicks; ++i) {
    appTick(h.app);
    appRender(h.app, h.fb);
  }
  appProfile(h.app);
  const int rebuilds0 = appPaletteRebuilds(h.app);
  uint32_t worst_static = 0, static_frames = 0;
  uint64_t static_us = 0, total_us = 0, strips = 0;
  const int ticks = 30 * 60 * SS_TICK_HZ;
  for (int i = 0; i < ticks; ++i) {
    if (i % (30 * SS_TICK_HZ) == 0) {
      in.mood = moodAt(t0 + i / SS_TICK_HZ);
      if (real) in.sky = skyAt(t0 + i / SS_TICK_HZ);
      appSetInputs(h.app, in);
    }
    if (appTick(h.app)) {   // the loop ended: stop, a new loop's build-up is another matter
      printf("(the loop ended after %d s)\n", i / SS_TICK_HZ);
      break;
    }
    appRender(h.app, h.fb);
    const RenderProfile p = appProfile(h.app);
    if (p.static_us) {
      ++static_frames;
      static_us += p.static_us;
      if (p.static_us > worst_static) worst_static = p.static_us;
    }
    total_us += p.static_us + p.copy_in_us + p.draw_us + p.copy_out_us;
    strips += p.strips;
  }
  printf("%s sky: frames with a cache repaint %u, repaint time %.1f ms in all (worst %.2f ms), "
         "all drawing %.1f ms, strips %llu, palette rebuilds %d\n",
         real ? "real  " : "parked", (unsigned)static_frames, (double)static_us / 1000.0, (double)worst_static / 1000.0,
         (double)total_us / 1000.0, (unsigned long long)strips, appPaletteRebuilds(h.app) - rebuilds0);
  hostStop(h);
}

int runCost() {
  const tc::SunTimes st = tc::sun_times(kDay[0], kDay[1], kDay[2], kOff, kPlace.latitude, kPlace.longitude);
  const int64_t t0 = st.rise + (st.set - st.rise) * 6 / 10;   // early afternoon: no palette change in the half hour
  // A whole-cache rebuild on this PC, to set the repaints against.
  {
    Host h;
    Inputs in = inputsAt(t0, nullptr, 0.f);
    if (!hostStart(h, in)) return 1;
    appSetClock(h.app, clockUs);
    for (int i = 0; i < kSettleTicks; ++i) appTick(h.app);
    appRender(h.app, h.fb);
    appProfile(h.app);
    uint64_t sum = 0;
    for (int i = 0; i < 20; ++i) {
      appInvalidate(h.app);
      in.weather.night_gain = 0.6f + 0.01f * (float)i;   // a host setting: forces a palette pass
      in.weather.overcast = 0.05f * (float)(i % 2);
      appSetInputs(h.app, in);
      appTick(h.app);
      appRender(h.app, h.fb);
      sum += appProfile(h.app).static_us;
    }
    printf("a whole-cache rebuild here: about %.2f ms\n", (double)sum / 20000.0);
    hostStop(h);
  }
  // Each twice, turn about: the first run of a process is slower whichever it is.
  for (int k = 0; k < 2; ++k) {
    costRun(true, t0);
    costRun(false, t0);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && !strcmp(argv[1], "--cost")) return runCost();
  if (argc != 2) {
    printf("usage: sky_stills OUT_DIR | --cost\n");
    return 2;
  }
  return runStills(argv[1]);
}
