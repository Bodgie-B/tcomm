// Palette tests. Plain executable: prints failures, returns non-zero if any.
// Also writes palette_swatches.ppm in the current directory (run from build/).
#include "palette.h"
#include "anim/ease.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

static const char* kMoodName[SS_MOOD_COUNT] = {"night", "dawn", "morning", "day", "golden", "dusk"};

static float lum(Pixel p) {   // 0..1, Rec.709 weights on the 8-bit expansion
  return (0.2126f * (float)red8(p) + 0.7152f * (float)green8(p) + 0.0722f * (float)blue8(p)) / 255.f;
}

static bool between(int v, int a, int b) { return v >= (a < b ? a : b) && v <= (a < b ? b : a); }

static bool channelsBetween(Pixel v, Pixel a, Pixel b) {
  return between(red8(v), red8(a), red8(b)) && between(green8(v), green8(a), green8(b)) &&
         between(blue8(v), blue8(a), blue8(b));
}

static const WeatherTint kClear = {0.f, 0.f, 0.f};

static void testAtmosphericRule() {
  for (int m = 0; m < SS_MOOD_COUNT; ++m) {
    const Palette& k = moodKey((Mood)m);
    const float sky = lum(k[SKY4]);
    const float dFar = fabsf(lum(k[RIDGE_FAR]) - sky);
    const float dMid = fabsf(lum(k[RIDGE_MID]) - sky);
    const float dNear = fabsf(lum(k[RIDGE_NEAR]) - sky);
    CHECK(dFar < dMid && dMid < dNear, "%s: ridge distance to SKY4 far %.3f mid %.3f near %.3f",
          kMoodName[m], (double)dFar, (double)dMid, (double)dNear);
    // Ridges get darker toward the viewer.
    CHECK(lum(k[RIDGE_FAR]) > lum(k[RIDGE_MID]) && lum(k[RIDGE_MID]) > lum(k[RIDGE_NEAR]),
          "%s: ridge luminance order", kMoodName[m]);
    CHECK(lum(k[TREES_FAR]) < lum(k[RIDGE_MID]) && lum(k[TREES_FAR]) > lum(k[RIDGE_NEAR]),
          "%s: TREES_FAR not between RIDGE_MID and RIDGE_NEAR", kMoodName[m]);
    CHECK(lum(k[TREES_MID]) < lum(k[RIDGE_NEAR]), "%s: TREES_MID not darker than RIDGE_NEAR", kMoodName[m]);
    CHECK(lum(k[TRUNK]) < lum(k[RIDGE_NEAR]) && lum(k[FOLIAGE]) < lum(k[RIDGE_NEAR]) &&
              lum(k[FOLIAGE_DARK]) < lum(k[RIDGE_NEAR]),
          "%s: near trees not darker than RIDGE_NEAR", kMoodName[m]);
    // Sky bands monotone toward the horizon (lighter), and actors read darkest.
    CHECK(lum(k[SKY0]) <= lum(k[SKY1]) && lum(k[SKY1]) <= lum(k[SKY2]) &&
              lum(k[SKY2]) <= lum(k[SKY3]) && lum(k[SKY3]) <= lum(k[SKY4]),
          "%s: sky bands not monotone", kMoodName[m]);
    CHECK(lum(k[ACTOR]) < lum(k[GROUND_DARK]) && lum(k[ACTOR_LIGHT]) > lum(k[ACTOR]),
          "%s: actor silhouette not darker than ground", kMoodName[m]);
    CHECK(lum(k[CLOUD_LIT]) >= lum(k[SKY1]) - 1e-4f && lum(k[CLOUD_LIT]) <= lum(k[SKY3]) + 1e-4f &&
              lum(k[CLOUD_SHADE]) >= lum(k[SKY1]) - 1e-4f && lum(k[CLOUD_SHADE]) <= lum(k[SKY3]) + 1e-4f,
          "%s: clouds not between SKY1 and SKY3", kMoodName[m]);
  }
}

static void testNightRule() {
  const Palette& k = moodKey(MOOD_NIGHT);
  for (int s = 0; s < SLOT_COUNT; ++s) {
    if (s == FIRE_CORE || s == STAR || s == TENT_LIT || s == SUN) continue;
    CHECK(lum(k.c[s]) <= 0.60f, "night slot %d luminance %.3f above 0.60", s, (double)lum(k.c[s]));
  }
}

// --- Mike's review (stage 6): contrast and night limits ----------------------
// Gaps are luminance differences on the 0..255 scale (Rec.709, 8-bit expansion).
static float lum255(Pixel p) { return lum(p) * 255.f; }

static const float kLightGapDay = 50.f;    // ACTOR_LIGHT over ACTOR, dawn..dusk
static const float kLightGapNight = 30.f;  // ... at night and under night_dim
static const float kTreeGap = 25.f;        // TRUNK/FOLIAGE/FOLIAGE_DARK over ACTOR, every mood
// night_dim scales the whole scene by 0.55 (testWeather asks for >= 40% off
// SKY2), so absolute gaps shrink with it; under night_dim the tree gap must keep
// the same contrast ratio: 25 * 0.55.
static const float kTreeGapDim = 25.f * 0.55f;

static void testMikeReview() {
  static const Slot kTrees[3] = {TRUNK, FOLIAGE, FOLIAGE_DARK};
  const Palette& day = moodKey(MOOD_DAY);
  printf("contrast (luminance 0..255)        ACTOR  LIGHT  gap  | TRUNK  gap  FOL  gap  FOLD  gap | TENT  LIT  MUSH\n");
  for (int pass = 0; pass < 2; ++pass) {
    const bool dim = pass == 1;
    for (int m = 0; m < SS_MOOD_COUNT; ++m) {
      Palette p;
      computePalette((float)m, WeatherTint{0.f, 0.f, dim ? 1.f : 0.f}, p);
      const float a = lum255(p[ACTOR]);
      const float gl = lum255(p[ACTOR_LIGHT]) - a;
      printf("  %-8s%-24s %5.1f  %5.1f  %4.1f |", kMoodName[m], dim ? " night_dim=1" : "", (double)a,
             (double)lum255(p[ACTOR_LIGHT]), (double)gl);
      for (Slot t : kTrees) printf(" %5.1f %4.1f", (double)lum255(p[t]), (double)(lum255(p[t]) - a));
      printf(" | %5.1f %5.1f %5.1f\n", (double)lum255(p[TENT]), (double)lum255(p[TENT_LIT]), (double)lum255(p[MUSHROOM]));

      const float needL = (dim || m == MOOD_NIGHT) ? kLightGapNight : kLightGapDay;
      CHECK(gl >= needL, "%s%s: ACTOR_LIGHT only %.1f over ACTOR (need %.0f)", kMoodName[m], dim ? " dim" : "",
            (double)gl, (double)needL);
      const float needT = dim ? kTreeGapDim : kTreeGap;
      for (Slot t : kTrees) {
        const float g = lum255(p[t]) - a;
        CHECK(g >= needT, "%s%s: near-tree slot %d only %.1f over ACTOR (need %.1f)", kMoodName[m],
              dim ? " dim" : "", (int)t, (double)g, (double)needT);
      }
      if (dim) {
        for (int s = 0; s < SLOT_COUNT; ++s) {
          const float cap = isNightHighlight(s) ? NIGHT_HIGHLIGHT_MAX : NIGHT_LUM_MAX;
          CHECK(lum(p.c[s]) <= cap + 1e-4f, "%s night_dim: slot %d luminance %.3f above %.2f", kMoodName[m], s,
                (double)lum(p.c[s]), (double)cap);
        }
      }
    }
  }
  // The tent is keyed per mood: off the day orange at golden, dusk and night,
  // and darker than day when unlit at dusk and night.
  static const Mood kTentMoods[3] = {MOOD_GOLDEN, MOOD_DUSK, MOOD_NIGHT};
  for (Mood m : kTentMoods) {
    const Palette& k = moodKey(m);
    CHECK(k[TENT] != day[TENT], "%s: TENT still the day value", kMoodName[m]);
  }
  CHECK(lum(moodKey(MOOD_DUSK)[TENT]) < lum(day[TENT]) && lum(moodKey(MOOD_NIGHT)[TENT]) < lum(moodKey(MOOD_DUSK)[TENT]),
        "TENT should darken day > dusk > night");
  CHECK(lum(moodKey(MOOD_NIGHT)[TENT_LIT]) > 2.f * lum(moodKey(MOOD_NIGHT)[TENT]), "night lit tent should glow");
  // The mushroom is dim at night (no fire-orange cap in the dark).
  CHECK(lum(moodKey(MOOD_NIGHT)[MUSHROOM]) < 0.5f * lum(day[MUSHROOM]), "night MUSHROOM not dim");
  CHECK(lum(moodKey(MOOD_NIGHT)[MUSHROOM]) < lum(moodKey(MOOD_NIGHT)[FIRE]) * 0.5f, "night MUSHROOM as bright as fire");
}

static void testIntegerMoodsEqualKeys() {
  for (int m = 0; m < SS_MOOD_COUNT; ++m) {
    Palette p;
    computePalette((float)m, kClear, p);
    const Palette& k = moodKey((Mood)m);
    for (int s = 0; s < SLOT_COUNT; ++s)
      CHECK(p.c[s] == k.c[s], "%s slot %d: 0x%04x != key 0x%04x", kMoodName[m], s, p.c[s], k.c[s]);
  }
  // Wrapping.
  Palette a, b, c, d;
  computePalette(6.0f, kClear, a);
  computePalette(0.0f, kClear, b);
  computePalette(-0.5f, kClear, c);
  computePalette(5.5f, kClear, d);
  for (int s = 0; s < SLOT_COUNT; ++s) {
    CHECK(a.c[s] == b.c[s], "mood 6.0 should equal mood 0 (slot %d)", s);
    CHECK(c.c[s] == d.c[s], "mood -0.5 should equal mood 5.5 (slot %d)", s);
  }
  computePalette(12.25f, kClear, a);
  computePalette(0.25f, kClear, b);
  for (int s = 0; s < SLOT_COUNT; ++s) CHECK(a.c[s] == b.c[s], "mood 12.25 should equal 0.25 (slot %d)", s);
}

static void testInterpolation() {
  Palette p;
  computePalette(3.5f, kClear, p);
  const Palette& day = moodKey(MOOD_DAY);
  const Palette& gold = moodKey(MOOD_GOLDEN);
  int differs = 0;
  for (int s = 0; s < SLOT_COUNT; ++s) {
    CHECK(channelsBetween(p.c[s], day.c[s], gold.c[s]), "mood 3.5 slot %d not between Day and Golden", s);
    if (p.c[s] != day.c[s] && p.c[s] != gold.c[s]) ++differs;
  }
  CHECK(differs > SLOT_COUNT / 2, "mood 3.5 looks like a key, not a blend (%d slots differ)", differs);

  computePalette(5.5f, kClear, p);
  const Palette& dusk = moodKey(MOOD_DUSK);
  const Palette& night = moodKey(MOOD_NIGHT);
  int toward = 0;
  for (int s = 0; s < SLOT_COUNT; ++s) {
    CHECK(channelsBetween(p.c[s], dusk.c[s], night.c[s]), "mood 5.5 slot %d not between Dusk and Night", s);
    if (p.c[s] != dusk.c[s]) ++toward;
  }
  CHECK(toward > SLOT_COUNT / 2, "mood 5.5 did not move off Dusk (%d slots)", toward);
  CHECK(lum(p[SKY0]) < lum(dusk[SKY0]) && lum(p[SKY0]) > lum(night[SKY0]) - 1e-4f,
        "mood 5.5 SKY0 should sit between dusk and night");
}

static void testWeather() {
  for (int m = 0; m < SS_MOOD_COUNT; ++m) {
    Palette base, dim;
    computePalette((float)m, kClear, base);
    computePalette((float)m, WeatherTint{0.f, 0.f, 1.f}, dim);
    const float ratio = lum(dim[SKY2]) / lum(base[SKY2]);
    CHECK(ratio <= 0.60f, "%s: night_dim=1 SKY2 only %.0f%% darker", kMoodName[m], (double)((1.f - ratio) * 100.f));
    const float star = lum(dim[STAR]) / lum(base[STAR]);
    CHECK(star >= 0.80f, "%s: night_dim=1 STAR dimmed to %.2f", kMoodName[m], (double)star);
    const float fire = lum(dim[FIRE]) / lum(base[FIRE]);
    CHECK(fire >= 0.80f, "%s: night_dim=1 FIRE dimmed to %.2f", kMoodName[m], (double)fire);
  }

  for (int m = 0; m < SS_MOOD_COUNT; ++m) {
    Palette base, fog;
    computePalette((float)m, kClear, base);
    computePalette((float)m, WeatherTint{0.f, 1.f, 0.f}, fog);
    // Progress toward SKY4 as a projection in 8-bit RGB (luminance alone is too
    // coarse for night, where the whole ramp spans ~13 luminance levels).
    const float vr = (float)red8(base[SKY4]) - (float)red8(base[RIDGE_FAR]);
    const float vg = (float)green8(base[SKY4]) - (float)green8(base[RIDGE_FAR]);
    const float vb = (float)blue8(base[SKY4]) - (float)blue8(base[RIDGE_FAR]);
    const float dr = (float)red8(fog[RIDGE_FAR]) - (float)red8(base[RIDGE_FAR]);
    const float dg = (float)green8(fog[RIDGE_FAR]) - (float)green8(base[RIDGE_FAR]);
    const float db = (float)blue8(fog[RIDGE_FAR]) - (float)blue8(base[RIDGE_FAR]);
    const float progress = (dr * vr + dg * vg + db * vb) / (vr * vr + vg * vg + vb * vb);
    // 565 quantization (steps of 8/4/8) is a large fraction of the night ramp, so
    // night gets a looser floor; the exact blend factor is 0.7 everywhere.
    const float floorP = (m == MOOD_NIGHT) ? 0.50f : 0.60f;
    CHECK(progress >= floorP, "%s: fog=1 moved RIDGE_FAR only %.0f%% of the way to SKY4", kMoodName[m],
          (double)(progress * 100.f));
    CHECK(fog[SKY4] == base[SKY4], "%s: fog must not change the sky", kMoodName[m]);
    CHECK(fog[TREES_MID] == base[TREES_MID], "%s: fog must not touch TREES_MID", kMoodName[m]);
    // Fog order: far layers move more than near ones.
    const float mFar = fabsf(lum(fog[RIDGE_FAR]) - lum(base[RIDGE_FAR]));
    const float mNear = fabsf(lum(fog[RIDGE_NEAR]) - lum(base[RIDGE_NEAR]));
    CHECK(mFar >= mNear, "%s: fog moved RIDGE_NEAR more than RIDGE_FAR", kMoodName[m]);
  }

  // Overcast: day sky loses saturation, sun is darkened, ground untouched.
  Palette base, oc;
  computePalette(3.f, kClear, base);
  computePalette(3.f, WeatherTint{1.f, 0.f, 0.f}, oc);
  CHECK(lum(oc[SUN]) < lum(base[SUN]), "overcast should darken the sun");
  CHECK(oc[GROUND] == base[GROUND] && oc[ACTOR] == base[ACTOR], "overcast must not touch ground or actors");
  const int sat0 = abs((int)red8(base[SKY0]) - (int)blue8(base[SKY0]));
  const int sat1 = abs((int)red8(oc[SKY0]) - (int)blue8(oc[SKY0]));
  CHECK(sat1 < sat0, "overcast should desaturate SKY0 (%d -> %d)", sat0, sat1);

  // Determinism and out-of-range weather inputs.
  Palette a, b;
  computePalette(2.37f, WeatherTint{0.4f, 0.3f, 0.2f}, a);
  computePalette(2.37f, WeatherTint{0.4f, 0.3f, 0.2f}, b);
  bool same = true;
  for (int s = 0; s < 32; ++s) same = same && a.c[s] == b.c[s];
  CHECK(same, "computePalette not deterministic");
  computePalette(1.f, WeatherTint{9.f, -3.f, 9.f}, a);   // must clamp, not blow up
  computePalette(1.f, WeatherTint{1.f, 0.f, 1.f}, b);
  for (int s = 0; s < SLOT_COUNT; ++s) CHECK(a.c[s] == b.c[s], "weather inputs not clamped (slot %d)", s);
}

static void testHelpers() {
  const Pixel a = rgb565_hex(0x204060), b = rgb565_hex(0xE0C0A0);
  CHECK(mixRGB(a, b, 0.f) == a, "mixRGB t=0");
  CHECK(mixRGB(a, b, 1.f) == b, "mixRGB t=1");
  CHECK(mixRGB(a, b, -1.f) == a && mixRGB(a, b, 2.f) == b, "mixRGB clamps t");
  const Pixel mid = mixRGB(a, b, 0.5f);
  CHECK(channelsBetween(mid, a, b), "mixRGB midpoint outside endpoints");
  CHECK(depthRamp(a, b, 0.3f) == mixRGB(a, b, 0.3f), "depthRamp != mixRGB");
  CHECK(depthRamp(a, b, 5.f) == b && depthRamp(a, b, -5.f) == a, "depthRamp clamps depth");
}

// --- PPM swatch sheet -------------------------------------------------------

static void putPx(unsigned char* img, int w, int x, int y, Pixel p) {
  unsigned char* d = img + ((size_t)y * (size_t)w + (size_t)x) * 3u;
  d[0] = red8(p);
  d[1] = green8(p);
  d[2] = blue8(p);
}

static void writeSwatches(const char* path) {
  const int cell = 24;
  const int W = 800;
  const int H = cell * 7;
  unsigned char* img = (unsigned char*)calloc((size_t)W * (size_t)H * 3u, 1);   // test code, not core
  if (!img) {
    printf("FAIL: swatch alloc\n");
    ++g_fail;
    return;
  }
  for (int m = 0; m < SS_MOOD_COUNT; ++m) {
    const Palette& k = moodKey((Mood)m);
    for (int s = 0; s < SLOT_COUNT; ++s)
      for (int y = 0; y < cell; ++y)
        for (int x = 0; x < cell; ++x) putPx(img, W, s * cell + x, m * cell + y, k.c[s]);
  }
  for (int x = 0; x < W; ++x) {   // row 7: SKY2 as mood sweeps 0..6
    Palette p;
    computePalette(6.f * (float)x / (float)W, kClear, p);
    for (int y = 0; y < cell; ++y) putPx(img, W, x, 6 * cell + y, p[SKY2]);
  }
  FILE* f = fopen(path, "wb");
  if (!f) {
    printf("FAIL: cannot open %s\n", path);
    ++g_fail;
    free(img);
    return;
  }
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  fwrite(img, 3, (size_t)W * (size_t)H, f);
  fclose(f);
  free(img);
  printf("wrote %s (%dx%d)\n", path, W, H);
}


// Lightning (scene/weather.h): at flash 1 under night_dim 1 (and night mode's
// full dim) every slot still holds the night caps, at every mood and at the
// overcast a storm brings; the flash lifts the sky (it shows) but never to
// white; flash 0 is exactly the plain palette.
static void testFlash() {
  int bad = 0;
  float lift_min = 1.f;
  for (int i = 0; i <= 24; ++i) {
    const float m = (float)i * 0.25f;
    for (int oc = 0; oc < 2; ++oc) {
      const WeatherTint w{oc ? 0.8f : 0.f, 0.f, 1.f};
      Palette p, q, z;
      computePalette(m, w, 1.f, p);
      computePalette(m, w, 0.f, q);
      computePalette(m, w, z);
      CHECK(memcmp(&q, &z, sizeof q) == 0, "mood %.2f: flash 0 differs from the plain palette", (double)m);
      for (int s = 0; s < SLOT_COUNT; ++s) {
        const float cap = isNightHighlight(s) ? NIGHT_HIGHLIGHT_MAX : NIGHT_LUM_MAX;
        if (lum(p.c[s]) > cap + 1e-4f) {
          if (bad < 4) printf("flash 1, night: mood %.2f slot %d lum %.3f > %.2f\n", (double)m, s, (double)lum(p.c[s]), (double)cap);
          ++bad;
        }
        CHECK(p.c[s] != 0xFFFF, "mood %.2f slot %d pure white at flash 1", (double)m, s);
      }
      const float lift = lum(p[SKY2]) - lum(q[SKY2]);
      if ((m <= 1.f || m >= 5.f) && lift < lift_min) lift_min = lift;   // the night's own moods (a bright sky sits at the cap)
    }
  }
  CHECK(bad == 0, "%d slots over the night caps at flash 1", bad);
  // A palette shift, not a flash (brief section 11): small, but it must be there.
  CHECK(lift_min > 0.03f, "the night flash barely shows (SKY2 +%.3f)", (double)lift_min);
  {
    Palette n0, n1;
    computePalette(0.f, WeatherTint{0.8f, 0.f, 1.f}, 0.f, n0);
    computePalette(0.f, WeatherTint{0.8f, 0.f, 1.f}, 1.f, n1);
    printf("night storm (mood 0, overcast 0.8): SKY2 lum %.3f -> %.3f at flash 1, RIDGE_FAR %.3f -> %.3f\n",
           (double)lum(n0[SKY2]), (double)lum(n1[SKY2]), (double)lum(n0[RIDGE_FAR]), (double)lum(n1[RIDGE_FAR]));
  }
  // By day the flash lifts the sky further, and the near silhouettes stay put.
  Palette d, f;
  computePalette(3.f, WeatherTint{0.8f, 0.f, 0.f}, 0.f, d);
  computePalette(3.f, WeatherTint{0.8f, 0.f, 0.f}, 1.f, f);
  CHECK(lum(f[SKY2]) > lum(d[SKY2]) + 0.05f && f[ACTOR] == d[ACTOR] && f[FOLIAGE_DARK] == d[FOLIAGE_DARK],
        "day flash: sky %.3f -> %.3f", (double)lum(d[SKY2]), (double)lum(f[SKY2]));
  printf("flash 1 under the night dim: caps hold at 25 moods x 2 overcasts; night SKY2 lifts at least %.3f\n", (double)lift_min);
}

int main() {
  testAtmosphericRule();
  testNightRule();
  testMikeReview();
  testIntegerMoodsEqualKeys();
  testInterpolation();
  testWeather();
  testFlash();
  testHelpers();
  writeSwatches("palette_swatches.ppm");
  if (g_fail) {
    printf("test_palette: %d FAILURE(S)\n", g_fail);
    return 1;
  }
  printf("test_palette: OK\n");
  return 0;
}
