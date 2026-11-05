// Standalone stills of the Lookout's static landscape (lookout_land.h).
// Not part of the CMake build; compile by hand against core sources, e.g.
//
//   clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Wconversion
//     -Wdouble-promotion -Wshadow -Werror -ffp-contract=off -I core
//     tools/lookout_stills.cpp core/scene/lookout_land.cpp core/palette.cpp
//     core/gfx/raster.cpp core/gfx/surface.cpp core/anim/ease.cpp core/anim/rng.cpp
//     -o lookout_stills
//   (one line; the same flags build clean under /opt/homebrew/bin/g++-16)
//   ./lookout_stills OUT_DIR
//
// Writes OUT_DIR/lk_s{seed}_m{mood*10}_r{reveal*10}.ppm for seeds 1..4, moods
// 3.5 4.0 4.5 5.0 5.5 5.9 and reveals 0 0.5 1, plus a marked-up debug still
// per seed (trail, stair, seat, cab points, tower bounds, hiker-height bars),
// and prints the checks: quiet zone, night-cap luminance, tower contrast.
#include <stdio.h>
#include <stdlib.h>

#include "anim/rng.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/lookout_land.h"

using namespace ss;

static Pixel g_fb[SS_WIDTH * SS_HEIGHT];

static bool writePPM(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  static uint8_t rgb[SS_WIDTH * SS_HEIGHT * 3];
  const size_t n = (size_t)SS_WIDTH * SS_HEIGHT;
  for (size_t i = 0; i < n; i++) {
    const Pixel p = g_fb[i];
    rgb[i * 3 + 0] = red8(p);
    rgb[i * 3 + 1] = green8(p);
    rgb[i * 3 + 2] = blue8(p);
  }
  bool ok = fprintf(f, "P6\n%d %d\n255\n", SS_WIDTH, SS_HEIGHT) > 0 && fwrite(rgb, 1, n * 3, f) == n * 3;
  ok = (fclose(f) == 0) && ok;
  return ok;
}

static float lum(Pixel p) {
  return 0.2126f * (float)red8(p) + 0.7152f * (float)green8(p) + 0.0722f * (float)blue8(p);
}

// True when p lies (within 565 rounding) on the RGB segment a..b.
static bool onSegment(Pixel p, Pixel a, Pixel b) {
  const float pr = red8(p), pg = green8(p), pb = blue8(p);
  const float ar = red8(a), ag = green8(a), ab = blue8(a);
  const float dr = (float)red8(b) - ar, dg = (float)green8(b) - ag, db = (float)blue8(b) - ab;
  const float l2 = dr * dr + dg * dg + db * db;
  float t = l2 > 0.f ? ((pr - ar) * dr + (pg - ag) * dg + (pb - ab) * db) / l2 : 0.f;
  t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
  const float er = ar + dr * t - pr, eg = ag + dg * t - pg, eb = ab + db * t - pb;
  return er * er + eg * eg + eb * eb < 8.f * 8.f * 3.f;
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : ".";
  Surface s{g_fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  const Rect full = s.bounds();
  const float moods[6] = {3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 5.9f};
  const float reveals[3] = {0.f, 0.5f, 1.f};
  printf("sizeof(LookoutLand) = %u bytes\n", (unsigned)sizeof(LookoutLand));
  int fails = 0;
  for (int seed = 1; seed <= 4; ++seed) {
    LookoutLand lk;
    Rng rng;
    rng.seed((uint64_t)seed);
    lookoutLandGenerate(lk, rng);
    const Rect tb = lookoutTowerBounds(lk);
    printf("seed %d: tower x %d gy %d, trail %d legs, %d pines, %d tufts, bounds [%d,%d)-[%d,%d)\n", seed,
           lk.tower_x, lk.tower_gy, lk.trail.n - 1, lk.n_pines, lk.n_tufts, tb.x0, tb.y0, tb.x1, tb.y1);
    if (tb.x1 > LS_QUIET_X0 && tb.y1 > LS_QUIET_Y0) { printf("  FAIL tower bounds in quiet zone\n"); ++fails; }
    for (float m : moods) {
      Palette pal;
      computePalette(m, WeatherTint{0.f, 0.f, 0.f}, pal);
      for (float r : reveals) {
        lookoutLandDrawStatic(lk, s, pal, m, r);
        char path[512];
        snprintf(path, sizeof path, "%s/lk_s%d_m%02d_r%02d.ppm", out, seed, (int)(m * 10.f + 0.5f),
                 (int)(r * 10.f + 0.5f));
        if (!writePPM(path)) { printf("  FAIL write %s\n", path); ++fails; }
        // Quiet zone: only GROUND / GROUND_DARK (and their AA blend).
        int bad = 0;
        for (int y = LS_QUIET_Y0; y < SS_HEIGHT; ++y)
          for (int x = LS_QUIET_X0; x < SS_WIDTH; ++x)
            if (!onSegment(g_fb[y * SS_WIDTH + x], pal[GROUND], pal[GROUND_DARK])) ++bad;
        if (bad) { printf("  FAIL quiet zone m %.1f r %.1f: %d px\n", (double)m, (double)r, bad); ++fails; }
        // Frame-wide max luminance (night rule at the dark end).
        float mx = 0.f;
        for (int i = 0; i < SS_WIDTH * SS_HEIGHT; ++i) mx = lum(g_fb[i]) > mx ? lum(g_fb[i]) : mx;
        if (seed == 1 && r == 1.f)
          printf("  mood %.1f: frame max lum %.1f/255 (%.2f)\n", (double)m, (double)mx, (double)(mx / 255.f));
      }
    }
    // Reveal stages for seed 1 at mood 4: rising silhouette, then the detail.
    if (seed == 1) {
      const float extra[5] = {0.2f, 0.35f, 0.6f, 0.75f, 0.9f};
      Palette pal;
      computePalette(4.f, WeatherTint{0.f, 0.f, 0.f}, pal);
      for (float r : extra) {
        lookoutLandDrawStatic(lk, s, pal, 4.f, r);
        char path[512];
        snprintf(path, sizeof path, "%s/lk_s1_m40_r%02d.ppm", out, (int)(r * 100.f + 0.5f));
        if (!writePPM(path)) ++fails;
      }
    }
    // Debug still at mood 4: accessors marked.
    {
      Palette pal;
      computePalette(4.f, WeatherTint{0.f, 0.f, 0.f}, pal);
      lookoutLandDrawStatic(lk, s, pal, 4.f, 1.f);
      const Pixel red = rgb565(255, 40, 40), cyan = rgb565(40, 230, 255), yel = rgb565(255, 240, 60),
                  mag = rgb565(255, 60, 255), wht = rgb565(255, 255, 255);
      for (int k = 0; k <= 10; ++k) {
        const float u = (float)k / 10.f;
        const Vec2 p = lookoutTrailAt(lk, u);
        const float h = 90.f * lookoutTrailScale(u);
        strokeCapsule(s, Vec2{p.x, p.y}, Vec2{p.x, p.y - h}, 3.f, red, full);   // hiker height bar
      }
      for (int k = 0; k <= 12; ++k) {
        const Vec2 p = lookoutStairAt(lk, (float)k / 12.f);
        fillCircle(s, p, 1.6f, cyan, full);
      }
      strokeCapsule(s, lookoutStairAt(lk, 0.5f), Vec2{lookoutStairAt(lk, 0.5f).x, lookoutStairAt(lk, 0.5f).y - 44.f},
                    2.f, cyan, full);
      const Vec2 seat = lookoutRailSeat(lk);
      fillCircle(s, seat, 2.5f, yel, full);
      LookoutCabPoints cp;
      lookoutCabPoints(lk, cp);
      for (int i = 0; i < 3; ++i) {
        fillCircle(s, cp.peek_feet[i], 1.8f, mag, full);
        fillCircle(s, cp.peek_eye[i], 1.8f, mag, full);
      }
      fillCircle(s, cp.finder, 1.8f, wht, full);
      fillCircle(s, cp.compass, 1.8f, yel, full);
      strokeCapsule(s, cp.compass_feet, Vec2{cp.compass_feet.x, cp.compass_feet.y - 44.f}, 2.f, yel, full);
      const Rect b = lookoutTowerBounds(lk);
      const float bx0 = (float)b.x0, by0 = (float)b.y0, bx1 = (float)b.x1, by1 = (float)b.y1;
      strokeCapsule(s, Vec2{bx0, by0}, Vec2{bx1, by0}, 1.f, wht, full);
      strokeCapsule(s, Vec2{bx1, by0}, Vec2{bx1, by1}, 1.f, wht, full);
      strokeCapsule(s, Vec2{bx1, by1}, Vec2{bx0, by1}, 1.f, wht, full);
      strokeCapsule(s, Vec2{bx0, by1}, Vec2{bx0, by0}, 1.f, wht, full);
      strokeCapsule(s, Vec2{660.f, 420.f}, Vec2{800.f, 420.f}, 1.f, mag, full);
      strokeCapsule(s, Vec2{660.f, 420.f}, Vec2{660.f, 480.f}, 1.f, mag, full);
      char path[512];
      snprintf(path, sizeof path, "%s/lk_s%d_debug.ppm", out, seed);
      if (!writePPM(path)) ++fails;
    }
  }

  // Slot luminance used by the tower and the scene at each mood, and the
  // tower-against-sky contrast (timber colours vs SKY1/SKY2 behind the cab).
  printf("\nmood | SKY0 SKY1 SKY2 SKY4 | TRUNK ACTOR | GROUND FOL_D GRASS | timber(lit) vs SKY1 | glint\n");
  for (float m : moods) {
    Palette pal;
    computePalette(m, WeatherTint{0.f, 0.f, 0.f}, pal);
    printf("%.1f | %5.1f %5.1f %5.1f %5.1f | %5.1f %5.1f | %5.1f %5.1f %5.1f\n", (double)m, (double)lum(pal[SKY0]),
           (double)lum(pal[SKY1]), (double)lum(pal[SKY2]), (double)lum(pal[SKY4]), (double)lum(pal[TRUNK]),
           (double)lum(pal[ACTOR]), (double)lum(pal[GROUND]), (double)lum(pal[FOLIAGE_DARK]),
           (double)lum(pal[GRASS]));
  }
  // Night rule with night_dim = 1 at the dark end of the scene (5.9 and the fade's 0.1).
  const float darks[2] = {5.9f, 0.1f};
  for (float m : darks) {
    Palette pal;
    computePalette(m, WeatherTint{0.f, 0.f, 1.f}, pal);
    LookoutLand lk;
    Rng rng;
    rng.seed(1u);
    lookoutLandGenerate(lk, rng);
    lookoutLandDrawStatic(lk, s, pal, m, 1.f);
    float mx = 0.f;
    for (int i = 0; i < SS_WIDTH * SS_HEIGHT; ++i) mx = lum(g_fb[i]) > mx ? lum(g_fb[i]) : mx;
    const bool ok = mx / 255.f <= NIGHT_LUM_MAX + 1e-3f;
    printf("night_dim 1, mood %.1f: frame max lum %.3f (cap %.2f) %s\n", (double)m, (double)(mx / 255.f),
           (double)NIGHT_LUM_MAX, ok ? "ok" : "FAIL");
    if (!ok) ++fails;
  }
  printf("%s (%d failures)\n", fails ? "FAILED" : "all checks passed", fails);
  return fails ? 1 : 0;
}
