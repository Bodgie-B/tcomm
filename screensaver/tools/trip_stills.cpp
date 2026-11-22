// Stills for Mike's stage 22 review of the mushrooms (MUSHROOM_PLAN.md section 12, items 1 to 4; step B4 of
// INTEGRATION_PLAN.md): the patch and the trip's sky, before either is wired into a story.
//
//   P_*   the patch at every growth stage (1 .. 6 ripe, 7 picked over, and the wilting last loops), with the
//         mother at the left, the middle and the right of her range; stage 6 at dusk, at night and in rain;
//         the right-hand mother with the chip and both dog badges; the build-up pop in six frames.
//   S_*   the sky alone at ten moments of its 104 s (t 0, 8, 14, 22, 30, 50, 70, 86, 94, 102).
//   R_*   a strip of ten frames 0.4 s apart at t 50 (each band is repainted once between two of them), and
//   K_*   ten consecutive ticks at t 50 (one band moves a tick: where the seam between the newest band and
//         the next one lies), both cropped round a wheeling star and a moving edge.
//   V_*   the sky at t 50 under cover 3 and cover 6, at mood 2.6 and 3.4, with the sun low, and at night.
//
// How the sky is shown before C5b wires it into drawSky: the tool rolls the band clocks as dayTrack will
// (band i holds the clock of the last tick whose number ends in i; skyTripLatch), draws each band with
// skyTripBand or, at clock 0, the natural sky, then the sun, then every other static layer over it with
// landscapeDrawLayers. The camp's moving things (hiker, tent, fire, swaying grass, clouds) come from the
// app's own frame wherever that frame differs from its static cache. The cloud deck of cover 6 is laid in
// from a natural draw with the trip's deck palette (skyTripDeckPal), so its edge has a 1 px natural fringe
// the real cache will not have.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD (the same
// -DSS_XMAS and -DSS_S22 as the library):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -DSS_S22=1 -I screensaver/core screensaver/tools/trip_stills.cpp
//     BUILD/libss_core.a -o BUILD/trip_stills
//   BUILD/trip_stills OUT_DIR    writes OUT_DIR/<tag>_NN_name.ppm (800 x 480) and OUT_DIR/manifest.txt
//
// trip_sheet.py (zlib only) turns the PPMs and the manifest (file|label|x0|y0|x1|y1|scale, the crop and its
// enlargement) into the sheets.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "mush_patch.h"
#include "scene/landscape.h"
#include "scene/patch.h"
#include "scene/skytrip.h"
#include "scene/world.h"
#include "ss_app.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

typedef std::vector<Pixel> Frame;

Surface surfOf(Frame& f) { return Surface{f.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0}; }

bool writePPM(const char* path, const Frame& px) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", SS_WIDTH, SS_HEIGHT);
  std::vector<unsigned char> row((size_t)SS_WIDTH * 3);
  for (int y = 0; y < SS_HEIGHT; ++y) {
    for (int x = 0; x < SS_WIDTH; ++x) {
      const Pixel p = px[(size_t)y * SS_WIDTH + x];
      row[(size_t)x * 3] = red8(p);
      row[(size_t)x * 3 + 1] = green8(p);
      row[(size_t)x * 3 + 2] = blue8(p);
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  return fclose(f) == 0;
}

Inputs inputsDay(float mood) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.have_outside = true;   // clear from the data: no random rain over the stills
  in.weather_code = 0;
  return in;
}

const char* g_out = ".";
FILE* g_manifest = nullptr;
int g_n = 0;

void emit(const char* tag, const char* name, const char* label, const Frame& f, int x0, int y0, int x1, int y1,
          int scale) {
  char file[256], path[900];
  snprintf(file, sizeof file, "%s_%02d_%s.ppm", tag, g_n++, name);
  snprintf(path, sizeof path, "%s/%s", g_out, file);
  writePPM(path, f);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > SS_WIDTH) x1 = SS_WIDTH;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
  fprintf(g_manifest, "%s|%s|%d|%d|%d|%d|%d\n", file, label, x0, y0, x1, y1, scale);
}

struct Run {
  void* mem = nullptr;
  App* app = nullptr;
  Inputs in;
  const World* w() const { return appWorld(app); }
  void start(uint64_t seed, const Inputs& i) {
    in = i;
    mem = calloc(1, SS_APP_BYTES);
    app = appStart(mem, kAlloc, seed, in);
  }
  void stop() {
    appStop(app, kAlloc);
    free(mem);
    app = nullptr;
  }
  void ticks(int n) {
    for (int k = 0; k < n; ++k) appTick(app);
  }
  void render(Frame& f) {
    Surface s = surfOf(f);
    appInvalidate(app);
    appRender(app, s);
  }
};

// A seed whose landscape puts the mother within 5 px of x `want` (the landscape does not depend on carry).
uint64_t seedForMother(int want, const Inputs& in) {
  int best_d = 1 << 30;
  uint64_t best = 1;
  for (uint64_t seed = 1; seed <= 1500; ++seed) {
    Run r;
    r.start(seed, in);
    const int d = abs((int)r.w()->land.mush_x - want);
    r.stop();
    if (d < best_d) {
      best_d = d;
      best = seed;
    }
    if (d <= 2) break;
  }
  return best;
}

// The crop round the patch: 220 x 80 centred on it, enlarged 3 times.
void patchCrop(const Landscape& ls, int& x0, int& y0, int& x1, int& y1) {
  const Rect b = patchBounds(ls);
  int cx = ls.mush_x - 20;
  if (!b.empty()) cx = ((b.x0 < ls.mush_x - 8 ? b.x0 : ls.mush_x - 8) + (b.x1 > ls.mush_x + 8 ? b.x1 : ls.mush_x + 8)) / 2;
  x0 = cx - 110;
  x1 = cx + 110;
  if (x1 > SS_WIDTH) {
    x0 -= x1 - SS_WIDTH;
    x1 = SS_WIDTH;
  }
  y0 = 400;
  y1 = 480;
}

// ---- the patch ----------------------------------------------------------

void patchStill(const char* name, const char* label, uint64_t seed, Inputs in, uint8_t mush, bool whole) {
  in.carry = mush;
  Run r;
  r.start(seed, in);
  r.ticks(420);   // the build is over (13 s) and the scene has settled
  Frame f((size_t)SS_WIDTH * SS_HEIGHT);
  r.render(f);
  int x0, y0, x1, y1;
  patchCrop(r.w()->land, x0, y0, x1, y1);
  char lab[300];
  snprintf(lab, sizeof lab, "%s (mother x %d)", label, (int)r.w()->land.mush_x);
  if (whole) emit("P", name, lab, f, 0, 0, SS_WIDTH, SS_HEIGHT, 1);
  else emit("P", name, lab, f, x0, y0, x1, y1, 3);
  r.stop();
}

// The picked-over patch in its last loops: world.cpp copies only the stage today, so the tool redraws the
// static layers with the whole byte in the landscape (no swaying grass, no actors in this one).
void wiltStill(uint64_t seed, Inputs in) {
  in.carry = mushPack(MUSH_PICKED, 2, true);
  Run r;
  r.start(seed, in);
  r.ticks(420);
  Landscape ls = r.w()->land;
  ls.patch_stage = mushPack(MUSH_PICKED, 2, true);
  Frame f((size_t)SS_WIDTH * SS_HEIGHT);
  Surface s = surfOf(f);
  landscapeDrawStatic(ls, s, *appPalette(r.app), in.mood, r.w()->build, nullptr);
  int x0, y0, x1, y1;
  patchCrop(ls, x0, y0, x1, y1);
  char lab[300];
  snprintf(lab, sizeof lab, "7 picked over, last 2 loops: drooping (static layers only; mother x %d)", (int)ls.mush_x);
  emit("P", "wilt", lab, f, x0, y0, x1, y1, 3);
  r.stop();
}

void buildPop(uint64_t seed, Inputs in) {
  in.carry = MUSH_RIPE;
  Run r;
  r.start(seed, in);
  const float want[6] = {0.10f, 0.22f, 0.34f, 0.5f, 0.68f, 1.0f};
  int k = 0;
  Frame f((size_t)SS_WIDTH * SS_HEIGHT);
  for (int tick = 0; tick < 500 && k < 6; ++tick) {
    appTick(r.app);
    const float t = buildMushroomT(r.w()->build);
    if (t >= want[k]) {
      r.render(f);
      int x0, y0, x1, y1;
      patchCrop(r.w()->land, x0, y0, x1, y1);
      char name[64], lab[200];
      snprintf(name, sizeof name, "pop%d", k);
      snprintf(lab, sizeof lab, "build-up pop: mother's growth %.2f", (double)t);
      emit("P", name, lab, f, x0, y0, x1, y1, 3);
      ++k;
    }
  }
  r.stop();
}

// ---- the sky ------------------------------------------------------------

// The band clocks at tick T of the sky (T = t * 25), rolled as dayTrack will: band i was last painted on
// the latest tick <= T whose number ends in i.
void rollClocks(int T, LsTrip& trip) {
  for (int i = 0; i < SKYTRIP_BANDS; ++i) {
    int last = T - (((T - i) % SKYTRIP_BANDS) + SKYTRIP_BANDS) % SKYTRIP_BANDS;
    trip.tq[i] = last < 0 ? 0 : skyTripLatch((float)last / (float)SS_TICK_HZ);
  }
}

struct SkyScene {
  Run r;
  Frame dyn, stat;   // the app's frame and its static cache as drawn here
  SkyDraw sky;
  bool has_sky = false;
};

void skySceneStart(SkyScene& sc, uint64_t seed, const Inputs& in) {
  sc.r.start(seed, in);
  sc.r.ticks(420);
  sc.dyn.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
  sc.stat.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
  sc.r.render(sc.dyn);
  const World& w = *sc.r.w();
  sc.has_sky = in.sky.have;
  sc.sky = landscapeSky(in.sky, in.mood, (float)w.sky_cover / 8.f);
  Surface s = surfOf(sc.stat);
  landscapeDrawStatic(w.land, s, *appPalette(sc.r.app), in.mood, w.build, &sc.sky);
}

// The whole frame with the trip's sky at band clocks `trip`.
void skyFrame(SkyScene& sc, const LsTrip& trip, Frame& out) {
  const World& w = *sc.r.w();
  const Palette& pal = *appPalette(sc.r.app);
  const float mood = sc.r.in.mood;
  out.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
  Surface s = surfOf(out);
  BuildState drawn = w.build;   // every layer drawn, none cached: landscapeDrawLayers draws them all
  drawn.cached = 0;
  const SkyTripSun sun = skyTripSunOf(sc.sky, pal);
  for (int i = 0; i < SKYTRIP_BANDS; ++i) {
    const Rect band{0, i * SKYTRIP_BAND_ROWS, SS_WIDTH, (i + 1) * SKYTRIP_BAND_ROWS};
    if (trip.tq[i] == 0) {
      landscapeDrawLayers(w.land, s, band, pal, mood, drawn, BL_SKY, BL_SKY, &sc.sky);
      continue;
    }
    const float t = skyTripSeconds(trip.tq[i]);
    skyTripBand(w.land, s, pal, sun, t, band);
    if (sc.sky.sun) fillCircle(s, Vec2{(float)sc.sky.sun_x, (float)sc.sky.sun_y}, (float)sc.sky.sun_r, landscapeSunColor(sc.sky, pal), band);
    if (sc.sky.cover >= 4) {   // the deck, from a natural draw in the trip's deck palette
      Palette dp;
      skyTripDeckPal(pal, t, dp);
      Frame a((size_t)SS_WIDTH * SKYTRIP_BAND_ROWS), b((size_t)SS_WIDTH * SKYTRIP_BAND_ROWS);
      Surface sa{a.data(), SS_WIDTH, SKYTRIP_BAND_ROWS, SS_WIDTH, band.y0};
      Surface sb{b.data(), SS_WIDTH, SKYTRIP_BAND_ROWS, SS_WIDTH, band.y0};
      SkyDraw bare = sc.sky;
      bare.cover = 0;
      landscapeDrawLayers(w.land, sa, band, dp, mood, drawn, BL_SKY, BL_SKY, &sc.sky);
      landscapeDrawLayers(w.land, sb, band, dp, mood, drawn, BL_SKY, BL_SKY, &bare);
      for (int y = band.y0; y < band.y1 && y < LS_GROUND_Y; ++y)
        for (int x = 0; x < SS_WIDTH; ++x) {
          const size_t k = (size_t)(y - band.y0) * SS_WIDTH + (size_t)x;
          if (a[k] != b[k]) out[(size_t)y * SS_WIDTH + (size_t)x] = a[k];
        }
    }
  }
  landscapeDrawLayers(w.land, s, s.bounds(), pal, mood, drawn, BL_RIDGES, BL_COUNT - 1, &sc.sky);
  // The camp's moving things from the app's own frame (where it differs from its cache).
  for (size_t k = 0; k < out.size(); ++k)
    if (sc.dyn[k] != sc.stat[k]) out[k] = sc.dyn[k];
}

void skyAt(SkyScene& sc, float t, Frame& out) {
  LsTrip trip;
  rollClocks((int)(t * (float)SS_TICK_HZ + 0.5f), trip);
  skyFrame(sc, trip, out);
}

SkyInputs sunAt(float az, float alt) {
  SkyInputs s;
  memset(&s, 0, sizeof s);
  s.have = true;
  s.sun_az = az;
  s.sun_alt = alt;
  s.moon_az = 90.f;
  s.moon_alt = -40.f;
  s.moon_phase = 0.1f;
  return s;
}

// A crop round a wheeling star that is out at t 50, away from the sun, with an edge running near it.
void starCrop(const SkyScene& sc, int& x0, int& y0, int& x1, int& y1) {
  const Landscape& ls = sc.r.w()->land;
  const SkyTripSun sun = skyTripSunOf(sc.sky, *appPalette(sc.r.app));
  int best = -1;
  float best_d = 1e9f;
  for (int i = 0; i < LS_STAR_COUNT; ++i) {
    SkyTripStar st;
    if (!skyTripStar(ls, sun, i, 50.f, st)) continue;
    if (ls.stars[i].x < 110 || ls.stars[i].x > 690) continue;
    // nearest to an edge's rest line, so the strip shows both
    float d = 1e9f;
    for (int j = 0; j < 4; ++j) d = fminf(d, fabsf((float)(ls.stars[i].y - skyTripEdgeRest(j))));
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  const int cx = best >= 0 ? ls.stars[best].x : 300, cy = best >= 0 ? ls.stars[best].y : 118;
  x0 = cx - 100;
  x1 = cx + 100;
  y0 = cy - 50;
  y1 = cy + 50;
  if (y0 < 0) {
    y1 -= y0;
    y0 = 0;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: trip_stills OUT_DIR\n");
    return 2;
  }
  g_out = argv[1];
  if (ssXmasSwitch() != SS_XMAS) {
    fprintf(stderr, "compile with -DSS_XMAS=%d to match the library\n", ssXmasSwitch());
    return 2;
  }
  rasterSetScratch(calloc(1, rasterScratchBytes()));   // for the tool's own drawing; appRender lends its own
  char mp[700];
  snprintf(mp, sizeof mp, "%s/manifest.txt", g_out);
  g_manifest = fopen(mp, "wb");
  if (!g_manifest) return 2;

  // The patch, by day, the mother at the left, the middle and the right of her range.
  const Inputs day = inputsDay(3.f);
  const int mothers[3] = {475, 540, 625};
  uint64_t seeds[3];
  for (int m = 0; m < 3; ++m) {
    seeds[m] = seedForMother(mothers[m], day);
    printf("mother near x %d: seed %llu\n", mothers[m], (unsigned long long)seeds[m]);
  }
  static const char* const kStage[8] = {"", "1 two buttons", "2", "3", "4", "5", "6 ripe", "7 picked over"};
  for (int m = 0; m < 3; ++m) {
    for (int st = 1; st <= 7; ++st) {
      char name[32];
      snprintf(name, sizeof name, "m%d_stage%d", m, st);
      const uint8_t mush = st == 7 ? mushPack(MUSH_PICKED, MUSH_PICKED_LOOPS, true) : mushPack((uint8_t)st, 0, true);
      patchStill(name, kStage[st], seeds[m], day, mush, false);
    }
    wiltStill(seeds[m], day);
  }
  patchStill("m1_ripe_full", "6 ripe, whole frame", seeds[1], day, MUSH_RIPE, true);
  {
    Inputs dusk = inputsDay(5.f);
    patchStill("m1_dusk", "6 ripe at dusk", seeds[1], dusk, MUSH_RIPE, false);
    Inputs night = inputsDay(0.f);
    night.night_mode = true;
    night.weather.night_dim = 1.f;
    patchStill("m1_night", "6 ripe at night (night rule)", seeds[1], night, MUSH_RIPE, false);
    Inputs rain = inputsDay(3.f);
    rain.weather_code = 1;
    patchStill("m1_rain", "6 ripe in rain", seeds[1], rain, MUSH_RIPE, false);
    Inputs chip = day;
    chip.chip = true;
    chip.dog_out = 3;
    patchStill("m2_chip", "6 ripe, mother right, chip and both dogs", seeds[2], chip, MUSH_RIPE, false);
  }
  buildPop(seeds[1], day);

  // The sky alone: a clear midday with the real sun fairly high.
  SkyScene sc;
  Inputs sky_in = inputsDay(3.f);
  sky_in.sky = sunAt(200.f, 32.f);
  skySceneStart(sc, seeds[1], sky_in);
  Frame f;
  g_n = 0;
  const float moments[10] = {0.f, 8.f, 14.f, 22.f, 30.f, 50.f, 70.f, 86.f, 94.f, 102.f};
  for (float t : moments) {
    skyAt(sc, t, f);
    char name[32], lab[160];
    snprintf(name, sizeof name, "t%03d", (int)t);
    snprintf(lab, sizeof lab, "sky at t %.0f s (wave %.2f, colour %.2f)", (double)t, (double)skyTripWave(t),
             (double)skyTripColour(t));
    emit("S", name, lab, f, 0, 0, SS_WIDTH, SS_HEIGHT, 1);
  }
  int x0, y0, x1, y1;
  starCrop(sc, x0, y0, x1, y1);
  g_n = 0;
  for (int k = 0; k < 10; ++k) {
    LsTrip trip;
    const int T = 1250 + 10 * k;
    rollClocks(T, trip);
    skyFrame(sc, trip, f);
    char name[32], lab[160];
    snprintf(name, sizeof name, "step%d", k);
    snprintf(lab, sizeof lab, "t %.1f s (0.4 s steps)", (double)T / (double)SS_TICK_HZ);
    emit("R", name, lab, f, x0, y0, x1, y1, 3);
  }
  g_n = 0;
  for (int k = 0; k < 10; ++k) {
    LsTrip trip;
    const int T = 1250 + k;
    rollClocks(T, trip);
    skyFrame(sc, trip, f);
    char name[32], lab[160];
    snprintf(name, sizeof name, "tick%d", k);
    snprintf(lab, sizeof lab, "tick %d: band %d repainted", T, T % SKYTRIP_BANDS);
    emit("K", name, lab, f, x0, y0, x1, y1, 3);
  }
  sc.r.stop();

  // Variants at t 50.
  g_n = 0;
  struct Var { const char* name; const char* label; float mood, overcast, az, alt; bool night; };
  const Var vars[6] = {
      {"cover3", "t 50, cover 3 (high clouds)", 3.f, 0.375f, 200.f, 32.f, false},
      {"cover6", "t 50, cover 6 (the deck takes the tint)", 3.f, 0.75f, 200.f, 32.f, false},
      {"mood26", "t 50, mood 2.6", 2.6f, 0.f, 150.f, 25.f, false},
      {"mood34", "t 50, mood 3.4", 3.4f, 0.f, 215.f, 22.f, false},
      {"sunlow", "t 50, the sun low (8 degrees)", 3.f, 0.f, 235.f, 8.f, false},
      {"night", "t 50 at night (the night rule; never planned)", 0.f, 0.f, 0.f, -40.f, true},
  };
  for (const Var& v : vars) {
    SkyScene vs;
    Inputs in = inputsDay(v.mood);
    in.weather.overcast = v.overcast;
    in.sky = sunAt(v.az, v.alt);
    if (v.night) {
      in.night_mode = true;
      in.weather.night_dim = 1.f;
    }
    skySceneStart(vs, seeds[1], in);
    skyAt(vs, 50.f, f);
    emit("V", v.name, v.label, f, 0, 0, SS_WIDTH, SS_HEIGHT, 1);
    vs.r.stop();
  }
  fclose(g_manifest);
  printf("wrote stills to %s\n", g_out);
  return 0;
}
