// Stills of the sun's higher, whiter arc for Mike's review (stage 21, 2026-11-17).
//
// It does what the panel does: the firmware's own sky maths (firmware/tcomm/src/core/sky.cpp) for
// the settings' placeholder place, the mood from the same sky phases blended the way ss_host.cpp
// blends them, the weather from the forecast's words through house_weather. Then it runs the
// scene to a settled moment and writes one frame. Nothing here knows the house's real place.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I screensaver/core -I firmware/tcomm/src
//     screensaver/tools/sun_stills.cpp firmware/tcomm/src/core/sky.cpp firmware/tcomm/src/core/civil.cpp
//     firmware/tcomm/src/core/zambretti.cpp BUILD/libss_core.a -o BUILD/sun_stills
//   BUILD/sun_stills OUT_DIR          the stills, as OUT_DIR/NN_name.ppm, and OUT_DIR/tiles.txt
//   python -I screensaver/tools/sky_sheet.py OUT_DIR docs/screensaver/reviews/stage21-sun
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
#include "scene/landscape.h"
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

// The sun's height before the arc was raised (Mike, 2026-11-17): 268 - 212 * sin(alt)^0.6.
float oldY(float alt) {
  if (alt < 0.f) return 268.f - alt * 7.f;
  return 268.f - 212.f * powf(sinf(alt * 3.14159265f / 180.f), 0.6f);
}

void dot(float cx, float cy, int r, Pixel c) {
  for (int y = (int)cy - r; y <= (int)cy + r; ++y)
    for (int x = (int)cx - r; x <= (int)cx + r; ++x)
      if (x >= 0 && x < SS_WIDTH && y >= 0 && y < SS_HEIGHT && (x - (int)cx) * (x - (int)cx) + (y - (int)cy) * (y - (int)cy) <= r * r)
        g_fb[y * SS_WIDTH + x] = c;
}

Pixel rgb(int r, int g, int b) { return (Pixel)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

// One scene settled at noon of the given day, then the sun's track for that day drawn over it as dots every
// 20 minutes: the old mapping dim, the new one bright, and for the November chart the moon's night track.
void arcChart(const char* name, const char* label, int y, int m, int d, bool moonTrack) {
  const tc::SunTimes st = tc::sun_times(y, m, d, kOff, kPlace.latitude, kPlace.longitude);
  const int64_t noon = (st.rise + st.set) / 2;
  Inputs in = inputsAt(noon, nullptr, 0.f);
  Host h;
  if (!hostStart(h, in)) exit(1);
  for (int i = 0; i < kSettleTicks; ++i) appTick(h.app);
  appRender(h.app, h.fb);
  for (int64_t t = st.rise; t <= st.set; t += 20 * 60) {
    const SkyInputs s = skyAt(t);
    if (s.sun_alt < 0.f) continue;
    const SkyDraw dn = landscapeSky(s, 3.f, 0.f);
    const float ox = (float)dn.sun_x;
    dot(ox, oldY(s.sun_alt), 3, rgb(120, 130, 150));   // old track: dim blue-grey
    dot(ox, (float)dn.sun_y, 4, rgb(255, 40, 40));      // new track: bright red
  }
  if (moonTrack) {
    const int64_t t0 = localMidnight(y, m, d) + 17 * 3600;
    for (int64_t t = t0; t < t0 + 15 * 3600; t += 20 * 60) {
      const tc::MoonSky ms = tc::moon_sky(t, kPlace.latitude, kPlace.longitude);
      if (ms.pos.alt < 0.f) continue;
      SkyInputs si = skyAt(t);
      const SkyDraw dm = landscapeSky(si, 0.f, 0.f);
      if (!dm.moon) continue;
      dot((float)dm.moon_x, (float)dm.moon_y, 4, rgb(60, 220, 90));   // moon track: green, unchanged
    }
  }
  char file[64], path[512];
  snprintf(file, sizeof file, "%02d_%s.ppm", ++g_n, name);
  snprintf(path, sizeof path, "%s/%s", g_out, file);
  if (!writePPM(path)) exit(1);
  fprintf(g_tiles, "%s|%s\n", file, label);
  printf("%s: %s\n", file, label);
  hostStop(h);
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
  // 1. Arc charts: old track grey dots, new track red dots, every 20 minutes.
  arcChart("arc_dec", "21 Dec: grey old arc, red new", 2026, 12, 21, false);
  arcChart("arc_nov", "17 Nov: green = moon track (unchanged)", 2026, 11, 17, true);
  arcChart("arc_mar", "20 Mar: grey old, red new", 2026, 3, 20, false);
  arcChart("arc_jun", "21 Jun: grey old, red new", 2026, 6, 21, false);

  // 2. The colour ramp on 17 Nov, and noon on 21 Dec and 21 Jun, and noon with high cloud.
  struct Day { int y, m, d; const char* tag; };
  const Day days[] = {{2026, 11, 17, "17 Nov"}, {2026, 12, 21, "21 Dec"}, {2026, 6, 21, "21 Jun"}};
  const double fr[5] = {0.06, 0.26, 0.50, 0.74, 0.94};
  const char* fn[5] = {"an hour after sunrise", "mid-morning", "noon", "mid-afternoon", "an hour before sunset"};
  for (int k = 0; k < 5; ++k) {
    const tc::SunTimes st = tc::sun_times(2026, 11, 17, kOff, kPlace.latitude, kPlace.longitude);
    const int64_t t = st.rise + (int64_t)((double)(st.set - st.rise) * fr[k]);
    char nm[48], lb[96];
    snprintf(nm, sizeof nm, "nov_%d", k);
    snprintf(lb, sizeof lb, "17 Nov, %s", fn[k]);
    still(nm, lb, inputsAt(t, nullptr, 0.f));
  }
  for (int k = 1; k < 3; ++k) {
    const tc::SunTimes st = tc::sun_times(days[k].y, days[k].m, days[k].d, kOff, kPlace.latitude, kPlace.longitude);
    const int64_t t = (st.rise + st.set) / 2;
    char nm[48], lb[96];
    snprintf(nm, sizeof nm, "noon_%d", k);
    snprintf(lb, sizeof lb, "%s noon", days[k].tag);
    still(nm, lb, inputsAt(t, nullptr, 0.f));
  }
  {
    const tc::SunTimes st = tc::sun_times(2026, 3, 20, kOff, kPlace.latitude, kPlace.longitude);
    still("noon_hazy", "20 Mar noon, fair weather (sun 51 deg)", inputsAt((st.rise + st.set) / 2, nullptr, 0.f));
  }
  // 3. A day moon (a half moon up in the afternoon): same place before and after this change.
  {
    int64_t best = 0;
    float bd = 1e9f;
    const int64_t t0 = localMidnight(2026, 11, 17);
    for (int64_t t = t0; t < t0 + 30LL * 86400; t += 1800) {
      const tc::MoonSky m = tc::moon_sky(t, kPlace.latitude, kPlace.longitude);
      const tc::SkyPos sp = tc::sun_position(t, kPlace.latitude, kPlace.longitude);
      float dp = fabsf(m.phase - 0.27f);
      if (m.pos.alt < 15.f || sp.alt < 15.f || dp > 0.05f) continue;
      if (dp < bd) { bd = dp; best = t; }
    }
    if (best) still("day_moon", "a day moon, on its low track (same place as before)", inputsAt(best, nullptr, 0.f));
  }
  fclose(g_tiles);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("usage: sun_stills OUT_DIR\n");
    return 2;
  }
  return runStills(argv[1]);
}
