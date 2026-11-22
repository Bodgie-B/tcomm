// The real sky (stage 12): the sun and moon where the host says they are.
//   - landscapeSky: east is left and west is right, higher is higher, a set body is under the
//     ridge line, no sun in the night's colours, no moon near new or (by day) when it is thin,
//     waxing and waning told apart, a moonless night marked dark, bad numbers give the old sky;
//   - the cloud cover in eighths;
//   - a scene whose sun and moon moved step by step shows exactly the frame of a scene that had
//     them there from the start (the small cache repaints equal a whole rebuild), by day and by
//     night, and with the palette steps spread;
//   - a zeroed Inputs::sky leaves the old parked sky.
// Plain executable; returns non-zero on failure.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene/landscape.h"
#include "ss_app.h"

using namespace ss;

static int g_fail = 0;
static long g_checks = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static SkyInputs sky(float saz, float salt, float maz, float malt, float phase) {
  SkyInputs s;
  memset(&s, 0, sizeof s);
  s.have = true;
  s.sun_az = saz;
  s.sun_alt = salt;
  s.moon_az = maz;
  s.moon_alt = malt;
  s.moon_phase = phase;
  return s;
}

static void testMapping() {
  // The sun through a day: left to right, up then down.
  int prev_x = -1000;
  for (int az = 70; az <= 290; az += 5) {
    const SkyDraw d = landscapeSky(sky((float)az, 30.f, 0.f, -50.f, 0.f), 3.f, 0.f);
    CHECK(d.real && d.sun && !d.moon, "sun at az %d not drawn", az);
    CHECK(d.sun_x > prev_x, "sun x went back at az %d", az);
    CHECK(d.sun_x >= 40 && d.sun_x <= 760, "sun off the frame at az %d: %d", az, d.sun_x);
    prev_x = d.sun_x;
  }
  CHECK(landscapeSky(sky(180.f, 30.f, 0.f, -50.f, 0.f), 3.f, 0.f).sun_x == 400, "due south is not the middle");
  CHECK(landscapeSky(sky(90.f, 30.f, 0.f, -50.f, 0.f), 3.f, 0.f).sun_x < 200, "east is not on the left");
  CHECK(landscapeSky(sky(270.f, 30.f, 0.f, -50.f, 0.f), 3.f, 0.f).sun_x > 600, "west is not on the right");
  int prev_y = 1000;
  for (int alt = -5; alt <= 90; ++alt) {
    const SkyDraw d = landscapeSky(sky(180.f, (float)alt, 0.f, -50.f, 0.f), 3.f, 0.f);
    CHECK(d.sun && d.sun_y <= prev_y && (alt > 60 || d.sun_y < prev_y), "sun not higher at alt %d (y %d, was %d)", alt, d.sun_y, prev_y);
    prev_y = d.sun_y;
    if (alt >= 20) CHECK(d.sun_y + d.sun_r < 190, "a sun %d degrees up is not clear of the ridges: y %d", alt, d.sun_y);
    if (alt <= 0) CHECK(d.sun_y >= LS_HORIZON - 6, "a set sun is above the horizon line: y %d", d.sun_y);
    CHECK(d.sun_y - d.sun_r >= 10, "the sun leaves the top of the frame at alt %d", alt);
  }
  // Well under the horizon, or in the night's colours: no sun.
  CHECK(!landscapeSky(sky(280.f, -7.f, 0.f, -50.f, 0.f), 5.2f, 0.f).sun, "a sun 7 degrees down was drawn");
  CHECK(!landscapeSky(sky(180.f, 20.f, 0.f, -50.f, 0.f), 0.f, 0.f).sun, "a sun at full night");
  CHECK(!landscapeSky(sky(180.f, 20.f, 0.f, -50.f, 0.f), 5.8f, 0.f).sun, "a sun in the moon's colours");
  // Low in the west it is bigger, as the old sunset sun was.
  CHECK(landscapeSky(sky(260.f, 2.f, 0.f, -50.f, 0.f), 4.f, 0.f).sun_r > landscapeSky(sky(100.f, 2.f, 0.f, -50.f, 0.f), 1.f, 0.f).sun_r,
        "the setting sun is no bigger than the rising one");

  // The moon at night: every phase but the days round new.
  for (int q = 0; q < 64; ++q) {
    const float ph = (float)q / 64.f;
    const SkyDraw d = landscapeSky(sky(0.f, -40.f, 200.f, 35.f, ph), 0.f, 0.f);
    const bool thin = q <= 4 || q >= 60;   // under 4 percent lit
    CHECK(d.moon == !thin, "phase %d/64: moon %d", q, (int)d.moon);
    CHECK(d.dark == thin, "phase %d/64: dark %d", q, (int)d.dark);
    if (d.moon) CHECK(d.moon_phase == q, "phase %d/64 drawn as %d", q, d.moon_phase);
    CHECK(!d.sun, "sun at night");
  }
  // Set: no moon, a dark sky. Just under the horizon it is still drawn (the ridges hide it) but the sky is dark.
  CHECK(!landscapeSky(sky(0.f, -40.f, 200.f, -10.f, 0.5f), 0.f, 0.f).moon, "a set moon was drawn");
  CHECK(landscapeSky(sky(0.f, -40.f, 200.f, -10.f, 0.5f), 0.f, 0.f).dark, "a set moon: sky not dark");
  CHECK(!landscapeSky(sky(0.f, -40.f, 200.f, 30.f, 0.5f), 0.f, 0.f).dark, "a full moon up: sky dark");
  // By day: only a moon a quarter lit or more, and never a dark sky.
  CHECK(!landscapeSky(sky(180.f, 40.f, 230.f, 30.f, 0.10f), 3.f, 0.f).moon, "a thin moon by day");
  CHECK(landscapeSky(sky(180.f, 40.f, 100.f, 30.f, 0.30f), 3.f, 0.f).moon, "no half moon by day");
  CHECK(!landscapeSky(sky(180.f, 40.f, 100.f, -30.f, 0.30f), 3.f, 0.f).dark, "dark by day");
  // Where it is relative to the sun: the same mapping.
  {
    const SkyDraw d = landscapeSky(sky(240.f, 20.f, 150.f, 20.f, 0.35f), 3.f, 0.f);
    // The moon keeps its low track, so at the same height it is lower than the sun now (Mike, 2026-11-17).
    CHECK(d.sun && d.moon && d.moon_x < d.sun_x && d.moon_y == 157 && d.sun_y < d.moon_y,
          "moon east of the sun is not left of it, and lower (moon y %d, sun y %d)", d.moon_y, d.sun_y);
  }
  // Not numbers, or no real sky: the old one.
  SkyInputs bad = sky(180.f, 30.f, 180.f, 30.f, 0.5f);
  bad.sun_alt = 1e30f;
  bad.sun_alt *= 1e30f;   // infinite
  CHECK(!landscapeSky(bad, 3.f, 0.f).real, "an infinite height was taken");
  SkyInputs none;
  memset(&none, 0, sizeof none);
  CHECK(!landscapeSky(none, 3.f, 0.f).real && !landscapeSky(none, 3.f, 0.f).sun, "a zeroed sky is real");
  // Cloud cover in eighths.
  CHECK(landscapeSky(none, 3.f, 0.f).cover == 0 && landscapeSky(none, 3.f, 0.3f).cover == 2 &&
            landscapeSky(none, 3.f, 0.7f).cover == 6 && landscapeSky(none, 3.f, 0.9f).cover == 7 &&
            landscapeSky(none, 3.f, 1.f).cover == 8 && landscapeSky(none, 3.f, 7.f).cover == 8,
        "cover steps");
  // Bounds hold the body.
  {
    const SkyDraw d = landscapeSky(sky(200.f, 40.f, 120.f, 25.f, 0.4f), 3.f, 0.f);
    const Rect s = landscapeSunBounds(d), m = landscapeMoonBounds(d);
    CHECK(s.x0 <= d.sun_x - d.sun_r && s.x1 > d.sun_x + d.sun_r && s.y0 <= d.sun_y - d.sun_r && s.y1 > d.sun_y + d.sun_r, "sun bounds");
    CHECK(m.x0 <= d.moon_x - LS_MOON_R && m.x1 > d.moon_x + LS_MOON_R && m.y0 <= d.moon_y - LS_MOON_R, "moon bounds");
    CHECK(landscapeSkySame(d, d), "same");
  }
}

// ---- the scene ----

static void* hostAlloc(size_t n) { return calloc(1, n); }
static void hostFree(void* p) { free(p); }

struct Run {
  HostAlloc alloc;
  void* mem;
  App* app;
  Pixel* px;
  Surface fb;
};

static void start(Run& r, const Inputs& in, int spread) {
  r.alloc.alloc_large = hostAlloc;
  r.alloc.free_large = hostFree;
  r.alloc.fast = nullptr;
  r.mem = malloc(SS_APP_BYTES);
  r.px = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
  r.fb = Surface{r.px, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  r.app = appStart(r.mem, r.alloc, 7, in);
  if (spread) appSetPaletteSpread(r.app, spread);
}

static void stop(Run& r) {
  appStop(r.app, r.alloc);
  free(r.mem);
  free(r.px);
}

static Inputs base(float mood, bool night) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.have_outside = true;   // a clear sky from data: no random weather
  in.night_mode = night;
  in.chip = true;
  return in;
}

static long diffPixels(const Run& a, const Run& b) {
  long n = 0;
  for (size_t i = 0; i < (size_t)SS_WIDTH * SS_HEIGHT; ++i) n += a.px[i] != b.px[i] ? 1 : 0;
  return n;
}

// One scene has the sun and moon at their end places all along; the other gets there in `steps`
// steps while it runs. The scene itself never looks at the sky, so the frames must end the same.
static void testMovingEqualsRebuilt(const char* name, float mood, bool night, const SkyInputs& from, const SkyInputs& to,
                                    int steps, int spread, float overcast) {
  Inputs in_to = base(mood, night);
  in_to.weather.overcast = overcast;
  in_to.sky = to;
  Inputs in_from = in_to;
  in_from.sky = from;
  Run fixed, moving;
  start(fixed, in_to, spread);
  start(moving, in_from, spread);
  const int kTicks = 2200, kEach = 40;
  int moved = 0;
  for (int t = 0; t < kTicks; ++t) {
    if (t >= 400 && t % kEach == 0 && moved < steps) {
      ++moved;
      const float f = (float)moved / (float)steps;
      Inputs in = in_to;
      in.sky.sun_az = from.sun_az + (to.sun_az - from.sun_az) * f;
      in.sky.sun_alt = from.sun_alt + (to.sun_alt - from.sun_alt) * f;
      in.sky.moon_az = from.moon_az + (to.moon_az - from.moon_az) * f;
      in.sky.moon_alt = from.moon_alt + (to.moon_alt - from.moon_alt) * f;
      in.sky.moon_phase = from.moon_phase + (to.moon_phase - from.moon_phase) * f;
      appSetInputs(moving.app, in);
    }
    appTick(fixed.app);
    appTick(moving.app);
    appRender(fixed.app, fixed.fb);
    appRender(moving.app, moving.fb);
    // Part-way through, the two differ only where a body is (or was): never anywhere near the ground.
    if (t == 1000 && moved < steps) {
      long low = 0;
      for (int y = LS_GROUND_Y; y < SS_HEIGHT; ++y)
        for (int x = 0; x < SS_WIDTH; ++x) low += fixed.px[(size_t)y * SS_WIDTH + x] != moving.px[(size_t)y * SS_WIDTH + x];
      // (the full moon's light on the stream may differ while the moon is on its way)
      if (!night) CHECK(low == 0, "%s: %ld ground pixels differ part-way", name, low);
    }
  }
  CHECK(moved == steps, "%s: only %d of %d steps", name, moved, steps);
  CHECK(appPaletteSettled(fixed.app) && appPaletteSettled(moving.app), "%s: a spread still running", name);
  const long d = diffPixels(fixed, moving);
  CHECK(d == 0, "%s: %ld pixels differ after the sky moved", name, d);
  stop(fixed);
  stop(moving);
}

// The old sky still there for a host that gives no positions: the body where the mood parks it.
static void testParkedSky() {
  Run r;
  start(r, base(3.f, false), 0);
  for (int t = 0; t < 1500; ++t) appTick(r.app);
  appRender(r.app, r.fb);
  const Pixel sun = (*appPalette(r.app))[SUN];
  const Palette parkedPal = *appPalette(r.app);
  CHECK(r.px[(size_t)120 * SS_WIDTH + 600] == sun, "the parked day sun is not at 600,120");
  stop(r);
  // And with a real sky it is where the host says, not there.
  Inputs in = base(3.f, false);
  in.sky = sky(120.f, 45.f, 0.f, -50.f, 0.f);
  const SkyDraw d = landscapeSky(in.sky, 3.f, 0.f);
  start(r, in, 0);
  for (int t = 0; t < 1500; ++t) appTick(r.app);
  appRender(r.app, r.fb);
  CHECK(r.px[(size_t)d.sun_y * SS_WIDTH + d.sun_x] == landscapeSunColor(d, *appPalette(r.app)),
        "the real sun is not where landscapeSky put it, in its colour for that height");
  (void)parkedPal;
  CHECK(d.sun_x < 300 && r.px[(size_t)120 * SS_WIDTH + 600] != sun, "the parked sun is still drawn");
  stop(r);
}

// ---- the sun's higher arc, its whiter colour, and the moon left alone (Mike, 2026-11-17) ----

// What the sun's height was before the arc was raised: 268 - 212 * sin(alt)^0.6.
static float oldSkyY(float alt) {
  if (alt < 0.f) return 268.f - alt * 7.f;
  return 268.f - 212.f * powf(sinf(alt * 3.14159265f / 180.f), 0.6f);
}

// The moon's place, phase and darkness for 15 inputs, recorded from the code before the sun's arc was
// changed. They must stay equal to the pixel: the moon keeps its low track.
struct MoonPin { float az, alt, phase, mood; int moon, x, y, ph, dark; };
static const MoonPin kMoonPins[] = {
  {60.f, -3.f, 0.25f, 0.f, 0, 0, 0, 0, 1},
  {95.f, -1.f, 0.30f, 3.f, 1, 128, 275, 19, 0},
  {130.f, 0.f, 0.50f, 0.f, 1, 240, 268, 32, 1},
  {180.f, 5.f, 0.40f, 3.f, 1, 400, 219, 26, 0},
  {215.f, 12.f, 0.75f, 0.f, 1, 512, 185, 48, 0},
  {250.f, 20.f, 0.60f, 3.f, 1, 624, 157, 38, 0},
  {300.f, 30.f, 0.35f, 3.f, 1, 760, 128, 22, 0},
  {100.f, 45.f, 0.45f, 0.f, 1, 144, 96, 29, 0},
  {180.f, 60.f, 0.55f, 3.f, 1, 400, 74, 35, 0},
  {270.f, 70.f, 0.80f, 0.f, 1, 688, 64, 51, 0},
  {140.f, 8.f, 0.30f, 3.f, 1, 272, 203, 19, 0},
  {200.f, 35.f, 0.50f, 0.f, 1, 464, 116, 32, 0},
  {75.f, 15.f, 0.65f, 3.f, 1, 64, 174, 42, 0},
  {285.f, 2.f, 0.20f, 3.f, 1, 736, 240, 13, 0},
  {165.f, 25.f, 0.90f, 0.f, 1, 352, 142, 58, 0},
};

static void testMoonUnchanged() {
  for (const MoonPin& m : kMoonPins) {
    const SkyDraw d = landscapeSky(sky(180.f, -40.f, m.az, m.alt, m.phase), m.mood, 0.f);
    CHECK((int)d.moon == m.moon && d.moon_x == m.x && d.moon_y == m.y && d.moon_phase == m.ph && (int)d.dark == m.dark,
          "moon at az %.0f alt %.0f moved: moon %d x %d y %d phase %d dark %d (was %d %d %d %d %d)", (double)m.az,
          (double)m.alt, (int)d.moon, d.moon_x, d.moon_y, d.moon_phase, (int)d.dark, m.moon, m.x, m.y, m.ph, m.dark);
  }
}

static void testSunArc() {
  const float knotAlt[9] = {0.f, 5.f, 10.f, 20.f, 30.f, 45.f, 60.f, 75.f, 90.f};
  const int knotY[9] = {268, 213, 180, 132, 98, 70, 52, 45, 42};
  for (int k = 0; k < 9; ++k) {
    const SkyDraw d = landscapeSky(sky(180.f, knotAlt[k], 0.f, -50.f, 0.f), 3.f, 0.f);
    CHECK(d.sun && d.sun_y >= knotY[k] - 1 && d.sun_y <= knotY[k] + 1, "alt %.0f: sun y %d, want %d", (double)knotAlt[k], d.sun_y, knotY[k]);
  }
  // Noon at four times of year at the placeholder latitude: 21 Dec, 17 Nov, 20 Mar, 21 Jun.
  const float noonAlt[4] = {27.7f, 31.9f, 51.1f, 74.6f};
  const int noonY[4] = {106, 94, 63, 45};
  for (int i = 0; i < 4; ++i) {
    const SkyDraw d = landscapeSky(sky(180.f, noonAlt[i], 0.f, -50.f, 0.f), 3.f, 0.f);
    CHECK(d.sun_y >= noonY[i] - 1 && d.sun_y <= noonY[i] + 1, "noon alt %.1f: y %d, want %d", (double)noonAlt[i], d.sun_y, noonY[i]);
    CHECK(d.sun_y - d.sun_r >= 12, "noon alt %.1f: disc top %d", (double)noonAlt[i], d.sun_y - d.sun_r);
  }
  for (int alt = 6; alt <= 90; ++alt) {
    const SkyDraw d = landscapeSky(sky(180.f, (float)alt, 0.f, -50.f, 0.f), 3.f, 0.f);
    CHECK((float)d.sun_y < oldSkyY((float)alt) + 0.5f && (float)d.sun_y < oldSkyY((float)alt) - 3.f, "alt %d: the sun is not higher than before (y %d, was %.1f)", alt, d.sun_y,
          (double)oldSkyY((float)alt));
  }
  for (int alt = -5; alt <= 0; ++alt) {
    const SkyDraw d = landscapeSky(sky(180.f, (float)alt, 0.f, -50.f, 0.f), 3.f, 0.f);
    CHECK(d.sun_y == (int)(oldSkyY((float)alt) + 0.5f) || d.sun_y == (int)oldSkyY((float)alt), "alt %d: the setting sun moved (y %d)", alt, d.sun_y);
  }
  for (int az = 60; az <= 300; az += 20) {
    const SkyDraw d = landscapeSky(sky((float)az, 30.f, (float)az, 30.f, 0.5f), 3.f, 0.f);
    CHECK(d.sun_x == d.moon_x, "az %d: sun x %d is not the moon's %d", az, d.sun_x, d.moon_x);
  }
}

static int lum(Pixel p) { return 2126 * red8(p) + 7152 * green8(p) + 722 * blue8(p); }

static void testSunColor() {
  Palette pal;
  WeatherTint w;
  memset(&w, 0, sizeof w);
  computePalette(3.f, w, pal);
  Pixel prev = 0;
  int prevLum = -1;
  for (int alt = 0; alt <= 90; ++alt) {
    const SkyDraw d = landscapeSky(sky(180.f, (float)alt, 0.f, -50.f, 0.f), 3.f, 0.f);
    const Pixel c = landscapeSunColor(d, pal);
    if (alt <= 8) CHECK(c == pal[SUN], "alt %d: the low sun is not the SUN colour", alt);
    CHECK(lum(c) >= prevLum, "alt %d: the sun got darker as it climbed", alt);
    CHECK(c != (Pixel)0xFFFF, "alt %d: the sun is pure white", alt);
    prevLum = lum(c);
    prev = c;
  }
  const Pixel top = mixRGB(pal[STAR], (Pixel)0xFFFF, 0.5f);
  for (int alt = 55; alt <= 90; alt += 5) {
    const SkyDraw d = landscapeSky(sky(180.f, (float)alt, 0.f, -50.f, 0.f), 3.f, 0.f);
    CHECK(landscapeSunColor(d, pal) == top, "alt %d: not the warm white", alt);
  }
  CHECK(prev == top, "the highest sun is not the warm white");
  CHECK(lum(top) > lum(pal[SUN]), "the warm white is not whiter than the low sun");
  // Light high cloud (cover 2) makes it softer.
  const SkyDraw clear = landscapeSky(sky(180.f, 60.f, 0.f, -50.f, 0.f), 3.f, 0.f);
  const SkyDraw hazy = landscapeSky(sky(180.f, 60.f, 0.f, -50.f, 0.f), 3.f, 0.25f);
  CHECK(hazy.cover == 2 && lum(landscapeSunColor(hazy, pal)) < lum(landscapeSunColor(clear, pal)), "hazy sun is not softer");
  // The colour is a function of the SkyDraw alone: the same picture, the same colour.
  CHECK(landscapeSunColor(clear, pal) == landscapeSunColor(clear, pal), "colour not repeatable");
}

int main() {
  testMapping();
  testMoonUnchanged();
  testSunArc();
  testSunColor();
  testParkedSky();
  // An afternoon: the sun down the western sky, a half moon climbing in the east.
  testMovingEqualsRebuilt("afternoon", 3.f, false, sky(190.f, 50.f, 95.f, 5.f, 0.30f), sky(235.f, 30.f, 120.f, 28.f, 0.31f), 40, 0, 0.f);
  // The same with the palette steps spread, as the panel runs.
  testMovingEqualsRebuilt("afternoon, spread", 3.f, false, sky(190.f, 50.f, 95.f, 5.f, 0.30f), sky(235.f, 30.f, 120.f, 28.f, 0.31f), 40, 2, 0.f);
  // The sun climbing from 12 to 50 degrees: its colour changes with its height all the way.
  testMovingEqualsRebuilt("climbing, spread", 3.f, false, sky(110.f, 12.f, 0.f, -40.f, 0.f), sky(160.f, 50.f, 0.f, -40.f, 0.f), 40, 2, 0.f);
  // Under a cloud deck (the sun passes behind it).
  testMovingEqualsRebuilt("gray afternoon", 3.f, false, sky(150.f, 20.f, 0.f, -40.f, 0.f), sky(200.f, 60.f, 0.f, -40.f, 0.f), 30, 0, 0.7f);
  // A night: the moon across the south, past the stars; then a moon that sets (the stars come out).
  testMovingEqualsRebuilt("night", 0.f, true, sky(0.f, -40.f, 130.f, 20.f, 0.40f), sky(0.f, -40.f, 215.f, 42.f, 0.42f), 40, 0, 0.f);
  testMovingEqualsRebuilt("moonset", 0.f, true, sky(0.f, -40.f, 255.f, 9.f, 0.5f), sky(0.f, -40.f, 268.f, -6.f, 0.5f), 30, 2, 0.f);
  if (g_fail) {
    printf("test_sky: %d FAILED of %ld checks\n", g_fail, g_checks);
    return 1;
  }
  printf("test_sky: ok (%ld checks)\n", g_checks);
  return 0;
}
