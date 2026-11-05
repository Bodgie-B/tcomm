// Tests for core/scene/chip: the font table and the six words, the chip's
// rectangle and margins, the exact status colors, the night rule, bounds that
// cover every drawn pixel (over every status, dog and night state, the pulse
// and the fades), clipping (a strip draw equals the frame draw and touches
// nothing outside the clip), chipUpdate's changed-area contract (empty when
// the look is the same, otherwise covering every changed pixel, over thousands
// of ticks of random inputs), the pulse (rings, the end), bad inputs, and the
// cost of a draw. Plain executable; returns non-zero on failure.
//
// Writes inspection sheets to the directory in argv[1] (default: the current
// directory, build/ under ctest): chip_sheet.ppm (every status x dog x day and
// night over a real frame, then the doorbell pulse) and chip_zoom.ppm (the
// same at 3x). Convert to PNG for review; see the notes in the final summary.
#include "gfx/raster.h"
#include "ss_app.h"
#include "scene/chip.h"
#include "scene/chip_font.h"

#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

static int g_fail = 0;
static int g_checks = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);  \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

// ---- helpers ------------------------------------------------------------------

static const int kW = SS_WIDTH, kH = SS_HEIGHT;
static Pixel g_bg[SS_WIDTH * SS_HEIGHT];    // a noise background: any stray write shows
static Pixel g_ref[SS_WIDTH * SS_HEIGHT];
static Pixel g_cur[SS_WIDTH * SS_HEIGHT];
static Pixel g_prev[SS_WIDTH * SS_HEIGHT];

static void fillNoise(Pixel* fb) {
  uint32_t h = 12345u;
  for (int i = 0; i < kW * kH; ++i) {
    h = h * 1664525u + 1013904223u;
    fb[i] = (Pixel)(h >> 16);
  }
}

static Surface frameSurface(Pixel* fb) { return Surface{fb, kW, kH, kW, 0}; }

static Palette dayPalette() {
  Palette p;
  computePalette(3.f, WeatherTint{0.f, 0.f, 0.f}, p);
  return p;
}
static Palette nightPalette() {
  Palette p;
  computePalette(0.f, WeatherTint{0.f, 0.f, 1.f}, p);
  return p;
}

static const Rect kFull{0, 0, kW, kH};
static void* g_scratch = nullptr;   // zeroed: the rasterizer's accumulation row must start at 0
static const float kDt = SS_TICK_DT;

static Rect step(ChipState& c, int st, int dog, bool night) { return chipUpdate(c, st, dog, night, kDt); }

static void run(ChipState& c, int st, int dog, bool night, int ticks) {
  for (int i = 0; i < ticks; ++i) step(c, st, dog, night);
}

// A settled chip: status/dogs/night reached and every fade done.
static ChipState settled(int st, int dog, bool night) {
  ChipState c;
  chipInit(c);
  run(c, st, dog, night, 150);
  return c;
}

static void renderOver(const ChipState& c, const Palette& pal, const Pixel* bg, Pixel* out) {
  memcpy(out, bg, sizeof(Pixel) * (size_t)kW * kH);
  Surface s = frameSurface(out);
  chipDraw(c, s, kFull, pal);
}

static bool inside(const Rect& outer, const Rect& r) {
  return r.x0 >= outer.x0 && r.y0 >= outer.y0 && r.x1 <= outer.x1 && r.y1 <= outer.y1;
}

// Bounding box of the pixels that differ between two frames (empty if none).
static Rect diffBox(const Pixel* a, const Pixel* b, const Rect& region) {
  Rect r{kW, kH, 0, 0};
  for (int y = region.y0; y < region.y1; ++y) {
    for (int x = region.x0; x < region.x1; ++x) {
      if (a[y * kW + x] != b[y * kW + x]) {
        if (x < r.x0) r.x0 = x;
        if (y < r.y0) r.y0 = y;
        if (x + 1 > r.x1) r.x1 = x + 1;
        if (y + 1 > r.y1) r.y1 = y + 1;
      }
    }
  }
  if (r.x1 <= r.x0) return Rect{0, 0, 0, 0};
  return r;
}

// Within one step in every channel's own bits. A clip that starts mid-row shifts the rasterizer's
// local x, which can move an edge pixel's coverage by one level; row strips (what the renderer
// uses) are exact.
static bool nearlySame(Pixel a, Pixel b) {
  const int dr = (int)(a >> 11) - (int)(b >> 11), dg = (int)((a >> 5) & 63) - (int)((b >> 5) & 63),
            db = (int)(a & 31) - (int)(b & 31);
  return dr >= -1 && dr <= 1 && dg >= -1 && dg <= 1 && db >= -1 && db <= 1;
}

static float lum(Pixel p) {
  return 0.2126f * (float)red8(p) / 255.f + 0.7152f * (float)green8(p) / 255.f + 0.0722f * (float)blue8(p) / 255.f;
}

static const Rect kFrameRect{0, 0, kW, kH};

// ---- (a) the font and the words ---------------------------------------------------

static void testWords() {
  printf("words and font\n");
  static const char* const kPanel[kChipStatusCount] = {"OPEN", "BUSY", "PRIVATE", "SLEEPING", "AWAY", "NO SIGNAL"};
  for (int s = 0; s < kChipStatusCount; ++s) {
    const char* w = chipLabel(s);
    CHECK(strcmp(w, kPanel[s]) == 0, "status %d reads '%s', the panel says '%s'", s, w, kPanel[s]);
    for (const char* p = w; *p; ++p) {
      bool found = false;
      for (int g = 0; g < kChipGlyphCount; ++g) found = found || kChipGlyphs[g].ch == *p;
      CHECK(found, "'%c' of '%s' is not in chip_font.h", *p, w);
    }
    const int width = chipLabelWidth(s);
    CHECK(width + 2 * 10 <= kChipW, "'%s' is %d px wide: under 10 px of padding in the %d px chip", w, width, kChipW);
    printf("  %-10s ink %3d px, %2d px each side\n", w, width, (kChipW - width) / 2);
  }
  CHECK(strcmp(chipLabel(-1), "NO SIGNAL") == 0 && strcmp(chipLabel(6), "NO SIGNAL") == 0 &&
            strcmp(chipLabel(1000), "NO SIGNAL") == 0,
        "out of range reads as No signal");
  // The font table: sorted, every bitmap inside the blob.
  for (int g = 0; g < kChipGlyphCount; ++g) {
    if (g > 0) CHECK(kChipGlyphs[g - 1].ch < kChipGlyphs[g].ch, "glyph table not sorted at %d", g);
    const int nibbles = kChipGlyphs[g].w * kChipGlyphs[g].h;
    CHECK(kChipGlyphs[g].bitmap + (nibbles + 1) / 2 <= (int)sizeof(kChipBitmaps), "glyph %c outside the bitmap blob",
          kChipGlyphs[g].ch);
  }
  printf("  font: %d glyphs, %d kerning pairs, %d bitmap bytes\n", kChipGlyphCount, kChipKernCount,
         (int)sizeof(kChipBitmaps));
}

// ---- (b) geometry ----------------------------------------------------------------

static void testGeometry() {
  printf("geometry\n");
  ChipState c;
  chipInit(c);
  const Rect b = chipBounds(c);
  CHECK(b.x0 == kChipX0 && b.y0 == kChipY0 && b.x1 == kChipX0 + kChipW && b.y1 == kChipY0 + kChipH, "bounds %d,%d,%d,%d",
        b.x0, b.y0, b.x1, b.y1);
  CHECK(kW - b.x1 == 16 && kH - b.y1 == 16, "margins %d right, %d bottom (want 16, 16)", kW - b.x1, kH - b.y1);
  CHECK(kChipW == 160 && kChipH == 40, "size %dx%d", kChipW, kChipH);
  printf("  chip rect x %d..%d, y %d..%d (%dx%d), margins 16/16\n", b.x0, b.x1, b.y0, b.y1, kChipW, kChipH);

  // The widest the chip ever gets: both badges, pulse ring at its farthest.
  ChipState w = settled(3, 3, false);
  chipDoorbell(w);
  Rect widest = chipBounds(w);
  for (int i = 0; i < 270; ++i) {
    step(w, 3, 3, false);
    const Rect r = chipBounds(w);
    CHECK(inside(kFrameRect, r), "bounds %d,%d,%d,%d leave the frame at tick %d", r.x0, r.y0, r.x1, r.y1, i);
    widest = Rect{r.x0 < widest.x0 ? r.x0 : widest.x0, r.y0 < widest.y0 ? r.y0 : widest.y0,
                  r.x1 > widest.x1 ? r.x1 : widest.x1, r.y1 > widest.y1 ? r.y1 : widest.y1};
  }
  printf("  widest (both dogs and a pulse ring): x %d..%d, y %d..%d\n", widest.x0, widest.x1, widest.y0, widest.y1);
  const ChipState one = settled(0, 1, false), two = settled(0, 3, false);
  printf("  one badge: x from %d; two badges: x from %d\n", chipBounds(one).x0, chipBounds(two).x0);
  printf("  sizeof(ChipState) = %d bytes, sizeof(ChipLook) = %d\n", (int)sizeof(ChipState), (int)sizeof(ChipLook));
  CHECK(sizeof(ChipState) <= 64, "ChipState is %d bytes (a few dozen expected)", (int)sizeof(ChipState));
}

// ---- (c) colors and the night rule -------------------------------------------------

// WCAG contrast of two colors as drawn (the 8-bit expansions of the RGB565 values).
static float relLum(Pixel p) {
  const uint8_t ch[3] = {red8(p), green8(p), blue8(p)};
  float l[3];
  for (int i = 0; i < 3; ++i) {
    const float c = (float)ch[i] / 255.f;
    l[i] = c <= 0.03928f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
  }
  return 0.2126f * l[0] + 0.7152f * l[1] + 0.0722f * l[2];
}
static float contrast(Pixel a, Pixel b) {
  const float la = relLum(a), lb = relLum(b);
  return (fmaxf(la, lb) + 0.05f) / (fminf(la, lb) + 0.05f);
}

// The panel's status tiles (firmware/tcomm/src/ui/theme.cpp kStatus), row 0 (day) and row 2 (night): bg and fg.
static const uint32_t kPanelBg[2][kChipStatusCount] = {
    {0x1F6B3A, 0xF2A71B, 0xB8442E, 0x5A4A8C, 0x66705C, 0x49535B},
    {0x10361D, 0x79540E, 0x5C2217, 0x2D2546, 0x33382E, 0x242A2E}};
static const uint32_t kPanelFg[2][kChipStatusCount] = {
    {0xFBF1DC, 0x3A1F2B, 0xFBF1DC, 0xFBF1DC, 0xFBF1DC, 0xFBF1DC},
    {0xE4D8C0, 0xE4D8C0, 0xE4D8C0, 0xE4D8C0, 0xE4D8C0, 0xE4D8C0}};

static void testColors() {
  printf("colors and night\n");
  const Palette pals[2] = {dayPalette(), nightPalette()};
  const int px = kChipX0 + 30, py = kChipY0 + 5;   // inside the fill, above the word
  const int bcx = kChipX0 - kChipBadgeGap - kChipBadgeD / 2, bcy = kChipY0 + kChipH / 2;   // the nearest badge
  float worst_text = 99.f, worst_dog = 99.f;
  for (int row = 0; row < 2; ++row) {
    for (int s = 0; s < kChipStatusCount; ++s) {
      const Pixel bg = rgb565_hex(kPanelBg[row][s]), fg = rgb565_hex(kPanelFg[row][s]);
      ChipState c = settled(s, 0, row == 1);
      renderOver(c, pals[row], g_bg, g_cur);
      CHECK(g_cur[py * kW + px] == bg, "%s status %d fill %04x, the panel's bg is %04x", row ? "night" : "day", s,
            g_cur[py * kW + px], bg);
      // The word's own color is on the chip (the solid middle of its strokes).
      int n = 0;
      for (int y = kChipY0 + 10; y < kChipY0 + 30; ++y) {
        for (int x = kChipX0 + 10; x < kChipX0 + kChipW - 10; ++x) n += g_cur[y * kW + x] == fg;
      }
      CHECK(n >= 40, "%s status %d: only %d pixels in the panel's fg %04x", row ? "night" : "day", s, n, fg);
      const float ct = contrast(fg, bg);
      if (ct < worst_text) worst_text = ct;
      CHECK(ct >= 3.0f, "%s status %d: word contrast %.2f", row ? "night" : "day", s, (double)ct);
      CHECK(g_cur[kChipY0 * kW + kChipX0 + kChipW / 2] != g_bg[kChipY0 * kW + kChipX0 + kChipW / 2], "no outline at the chip's top edge");

      // The badge: a dark fill with the cream dog on it, readable on both rows. Sampled above the dog's back.
      ChipState d = settled(s, 3, row == 1);
      renderOver(d, pals[row], g_bg, g_cur);
      const Pixel badge = g_cur[(bcy - 13) * kW + bcx];
      const Pixel ink = rgb565_hex(row ? 0xE4D8C0 : 0xFBF1DC);
      int dog = 0;
      for (int y = bcy - 15; y < bcy + 15; ++y) {
        for (int x = bcx - 15; x < bcx + 15; ++x) dog += g_cur[y * kW + x] == ink;
      }
      CHECK(dog >= 60, "%s status %d: %d dog pixels in the cream %04x", row ? "night" : "day", s, dog, ink);
      const float cd = contrast(ink, badge);
      if (cd < worst_dog) worst_dog = cd;
      CHECK(cd >= 7.0f, "%s: dog on its badge contrast %.2f (badge %04x)", row ? "night" : "day", (double)cd, badge);
    }
  }
  printf("  exact panel bg and fg in both rows; worst word contrast %.1f:1, worst dog on badge %.1f:1\n", (double)worst_text,
         (double)worst_dog);

  // The night crossfade: the fill and the word go from one row to the other over about 2 s, every channel
  // between its two ends and moving one way, and the Busy word (plum to cream) never loses all contrast.
  {
    float worst = 99.f;
    for (int s = 0; s < kChipStatusCount; ++s) {
      ChipState c = settled(s, 0, false);
      const Pixel from = rgb565_hex(kPanelBg[0][s]), to = rgb565_hex(kPanelBg[1][s]);
      int last_g = (int)green8(from), ticks = 0;
      for (int i = 0; i < 80; ++i) {
        step(c, s, 0, true);
        renderOver(c, pals[0], g_bg, g_cur);
        const Pixel p = g_cur[py * kW + px];
        const int lo = (int)green8(to) < (int)green8(from) ? (int)green8(to) : (int)green8(from);
        const int hi = (int)green8(to) < (int)green8(from) ? (int)green8(from) : (int)green8(to);
        CHECK((int)green8(p) >= lo - 2 && (int)green8(p) <= hi + 2, "status %d fade left its range at tick %d", s, i);
        const int dir = (int)green8(to) - (int)green8(from);
        CHECK((last_g - (int)green8(p)) * dir <= 2, "status %d fade moved back at tick %d", s, i);
        last_g = (int)green8(p);
        ticks += c.look.night != 0 && c.look.night != 255;
        // The word flips from its day color to its night one at the halfway point; sample the contrast at the fill.
        const Pixel word = rgb565_hex(c.look.night < 128 ? kPanelFg[0][s] : kPanelFg[1][s]);
        const float ct = contrast(word, p);
        if (ct < worst) worst = ct;
      }
      CHECK(g_cur[py * kW + px] == to, "status %d fill did not land on the night row", s);
      CHECK(ticks >= 40 && ticks <= 55, "status %d night crossfade took %d ticks (about 50 expected)", s, ticks);
    }
    printf("  night crossfade ~2 s; the word's contrast never falls below %.1f:1 on the way (Busy's plum to cream is the low point)\n",
           (double)worst);
    CHECK(worst >= 2.0f, "the word nearly vanishes mid-fade (%.2f:1)", (double)worst);
  }

  // The night rule: nothing whiter than the panel's own night word color, no pure white by day or night, and the
  // large areas (the fills) under NIGHT_LUM_MAX. Run through a pulse's peaks with both dogs out.
  const float word_night = lum(rgb565_hex(0xE4D8C0));
  const float word_day = lum(rgb565_hex(0xFBF1DC));
  for (int night_on = 0; night_on < 2; ++night_on) {
    float peak = 0.f, fill_peak = 0.f;
    int over = 0, total = 0, stray = 0;
    for (int s = 0; s < kChipStatusCount; ++s) {
      ChipState c = settled(s, 3, night_on != 0);
      chipDoorbell(c);
      for (int t = 0; t < 60; ++t) {
        step(c, s, 3, night_on != 0);
        for (int i = 0; i < kW * kH; ++i) g_cur[i] = 0;
        Surface surf = frameSurface(g_cur);
        chipDraw(c, surf, kFull, pals[night_on]);
        const float body = lum(g_cur[py * kW + px]);   // the fill, at the pulse's brightest too
        if (body > fill_peak) fill_peak = body;
        const Rect b = chipBounds(c);
        for (int y = b.y0; y < b.y1; ++y) {
          for (int x = b.x0; x < b.x1; ++x) {
            const Pixel p = g_cur[y * kW + x];
            if (p == 0) continue;
            ++total;
            const float l = lum(p);
            if (l > peak) peak = l;
            if (l > NIGHT_LUM_MAX) {
              ++over;
              bool ok = x >= kChipX0 + 8 && x < kChipX0 + kChipW - 8 && y >= kChipY0 + 10 && y < kChipY0 + 30;   // the word
              for (int k = 0; k < 2; ++k) {   // or a badge
                const int cx = bcx - k * (kChipBadgeD + kChipBadgeStep);
                ok = ok || ((x - cx) * (x - cx) + (y - bcy) * (y - bcy) <= 16 * 16);
              }
              if (!ok) ++stray;
            }
            CHECK(p != 0xFFFF, "pure white drawn");
          }
        }
      }
    }
    printf("  %s: brightest pixel luminance %.2f (the word's own color is %.2f), fills peak %.2f; %d of %d drawn pixels above NIGHT_LUM_MAX\n",
           night_on ? "night" : "day ", (double)peak, (double)(night_on ? word_night : word_day), (double)fill_peak, over, total);
    CHECK(peak <= (night_on ? word_night : word_day) + 0.005f, "a pixel brighter than the panel's word color: %.3f", (double)peak);
    if (night_on) {
      CHECK(fill_peak <= NIGHT_LUM_MAX, "night chip fill luminance %.2f over the %.2f cap", (double)fill_peak, (double)NIGHT_LUM_MAX);
      CHECK(stray == 0, "%d pixels above the night cap outside the word and the badges: a bright area", stray);
    }
  }
  printf("  note: the night word color E4D8C0 is %.2f luminance, over NIGHT_LUM_MAX %.2f and NIGHT_HIGHLIGHT_MAX %.2f; the strokes of the word and the dogs only\n",
         (double)word_night, (double)NIGHT_LUM_MAX, (double)NIGHT_HIGHLIGHT_MAX);
}

// ---- (d) bounds cover everything drawn; clipping --------------------------------------

// Scripted states, by name, for the bounds and clipping sweeps.
struct Scene {
  ChipState c;
  bool night;
  char name[48];
};

static int buildStates(Scene* out, int cap) {
  int n = 0;
  for (int st = 0; st < kChipStatusCount && n < cap; ++st) {
    for (int dog = 0; dog < 4; ++dog) {
      for (int night = 0; night < 2; ++night) {
        if (n >= cap) break;
        Scene& s = out[n++];
        s.night = night != 0;
        s.c = settled(st, dog, s.night);
        snprintf(s.name, sizeof(s.name), "settled st%d dog%d night%d", st, dog, night);
      }
    }
  }
  // Mid-animation states: a status fade, a badge popping in, the dim easing, the pulse at several times.
  static const int kPulseTicks[] = {0, 3, 8, 20, 40, 60, 70, 100, 140, 200, 245};
  for (int k = 0; k < (int)(sizeof(kPulseTicks) / sizeof(kPulseTicks[0])) && n < cap; ++k) {
    Scene& s = out[n++];
    s.night = (k & 1) != 0;
    s.c = settled(1, 3, s.night);
    chipDoorbell(s.c);
    run(s.c, 1, 3, s.night, kPulseTicks[k]);
    snprintf(s.name, sizeof(s.name), "pulse after %d ticks night%d", kPulseTicks[k], (int)s.night);
  }
  for (int k = 1; k <= 16 && n < cap; k += 3) {
    Scene& s = out[n++];
    s.night = false;
    s.c = settled(0, 1, false);
    run(s.c, 4, 3, false, k);
    snprintf(s.name, sizeof(s.name), "fade+pop %d ticks in", k);
  }
  for (int k = 1; k <= 60 && n < cap; k += 14) {
    Scene& s = out[n++];
    s.night = true;
    s.c = settled(2, 0, false);
    run(s.c, 2, 0, true, k);
    snprintf(s.name, sizeof(s.name), "dimming %d ticks in", k);
  }
  return n;
}

static void testBoundsAndClip() {
  printf("bounds and clipping\n");
  static Scene states[160];
  const int n = buildStates(states, 160);
  const Palette day = dayPalette(), night = nightPalette();
  fillNoise(g_bg);

  long drawn_px = 0;
  long strip_mismatch = 0, outside_touched = 0, clip_mismatch = 0, clip_offbyone = 0;
  static Pixel strip_buf[SS_WIDTH * SS_STRIP_ROWS];
  for (int i = 0; i < n; ++i) {
    const Scene& sc = states[i];
    const Palette& pal = sc.night ? night : day;
    const Rect b = chipBounds(sc.c);
    CHECK(inside(kFrameRect, b), "%s: bounds leave the frame", sc.name);
    renderOver(sc.c, pal, g_bg, g_ref);

    // Every changed pixel is inside the bounds.
    const Rect d = diffBox(g_ref, g_bg, kFrameRect);
    CHECK(d.empty() || inside(b, d), "%s: drew outside the bounds: changed %d,%d,%d,%d, bounds %d,%d,%d,%d", sc.name,
          d.x0, d.y0, d.x1, d.y1, b.x0, b.y0, b.x1, b.y1);
    CHECK(!d.empty(), "%s: drew nothing", sc.name);
    // chipDraw clamps to the bounds, so a too-small bound would cut the ring off rather than show up above.
    // The ring's edge pixels must sit inside the bounds with a pixel to spare.
    if (sc.c.look.ring_a != 0) {
      CHECK(d.y0 > b.y0 && d.y1 < b.y1 && d.x1 < b.x1, "%s: the ring reaches the edge of its bounds (changed %d,%d,%d,%d, bounds %d,%d,%d,%d)",
            sc.name, d.x0, d.y0, d.x1, d.y1, b.x0, b.y0, b.x1, b.y1);
    }
    for (int k = 0; k < kW * kH; ++k) drawn_px += g_ref[k] != g_bg[k];

    // Strip by strip, as the renderer calls it: the same pixels as the frame draw, nothing else touched.
    for (int sy = 0; sy < SS_STRIP_COUNT; ++sy) {
      Surface strip{strip_buf, kW, SS_STRIP_ROWS, kW, sy * SS_STRIP_ROWS};
      for (int y = 0; y < SS_STRIP_ROWS; ++y) memcpy(strip_buf + y * kW, g_bg + (sy * SS_STRIP_ROWS + y) * kW, kW * sizeof(Pixel));
      const Rect clip{0, sy * SS_STRIP_ROWS, kW, (sy + 1) * SS_STRIP_ROWS};
      chipDraw(sc.c, strip, clip, pal);
      for (int y = 0; y < SS_STRIP_ROWS; ++y) {
        const int fy = sy * SS_STRIP_ROWS + y;
        for (int x = 0; x < kW; ++x) {
          if (strip_buf[y * kW + x] != g_ref[fy * kW + x]) {
            ++strip_mismatch;
            if (strip_mismatch <= 3) printf("  strip %d (%s) differs at %d,%d: %04x vs %04x\n", sy, sc.name, x, fy, strip_buf[y * kW + x], g_ref[fy * kW + x]);
          }
        }
      }
    }

    // Odd clips on a whole frame: inside the clip equals the full draw, outside the clip equals the background.
    static const Rect kClips[] = {{600, 430, 700, 450}, {0, 0, kW, 440}, {540, 410, 620, 480},
                                  {700, 460, 800, 480}, {624, 424, 625, 464}, {0, 0, 10, 10}, {630, 440, 640, 441}};
    for (const Rect& cl : kClips) {
      memcpy(g_cur, g_bg, sizeof(Pixel) * (size_t)kW * kH);
      Surface s = frameSurface(g_cur);
      chipDraw(sc.c, s, cl, pal);
      for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
          const bool in = x >= cl.x0 && x < cl.x1 && y >= cl.y0 && y < cl.y1;
          const Pixel want = in ? g_ref[y * kW + x] : g_bg[y * kW + x];
          if (g_cur[y * kW + x] != want) {
            if (!in) {
              ++outside_touched;
            } else if (nearlySame(g_cur[y * kW + x], want)) {
              ++clip_offbyone;
            } else {
              ++clip_mismatch;
              if (clip_mismatch <= 3) printf("  clip %d,%d,%d,%d (%s) differs at %d,%d: %04x vs %04x\n", cl.x0, cl.y0, cl.x1, cl.y1, sc.name, x, y, g_cur[y * kW + x], want);
            }
          }
        }
      }
    }
  }
  CHECK(strip_mismatch == 0, "%ld pixels differ between a strip draw and the frame draw", strip_mismatch);
  CHECK(clip_mismatch == 0, "%ld pixels differ by more than one step between a clipped draw and the frame draw", clip_mismatch);
  CHECK(outside_touched == 0, "%ld pixels outside the clip were touched", outside_touched);
  printf("  %d states; %ld drawn pixels checked against bounds, strips and clips: strips %ld mismatches (exact); odd clips %ld off by one level (edge pixels), %ld worse; %ld touched outside a clip\n",
         n, drawn_px, strip_mismatch, clip_offbyone, clip_mismatch, outside_touched);

  // A clip that misses the chip leaves a strip alone and costs nothing.
  {
    ChipState c = settled(0, 3, false);
    Surface strip{strip_buf, kW, SS_STRIP_ROWS, kW, 0};
    for (int i = 0; i < kW * SS_STRIP_ROWS; ++i) strip_buf[i] = 0x1234;
    chipDraw(c, strip, Rect{0, 0, kW, SS_STRIP_ROWS}, day);
    bool clean = true;
    for (int i = 0; i < kW * SS_STRIP_ROWS; ++i) clean = clean && strip_buf[i] == 0x1234;
    CHECK(clean, "a strip far from the chip was touched");
  }
}

// ---- (e) the changed-area contract -------------------------------------------------

static void testUpdate() {
  printf("chipUpdate\n");
  const Palette day = dayPalette(), night = nightPalette();
  fillNoise(g_bg);

  // First call, then still.
  ChipState c;
  chipInit(c);
  Rect r = step(c, 0, 0, false);
  CHECK(!r.empty() && inside(r, chipBounds(c)), "the first update (No signal to Open) returns the chip");
  for (int i = 0; i < 5; ++i) step(c, 0, 0, false);
  for (int i = 0; i < 100; ++i) {
    r = step(c, 0, 0, false);
    CHECK(r.empty(), "an update with nothing changing returned %d,%d,%d,%d (tick %d)", r.x0, r.y0, r.x1, r.y1, i);
  }

  // A status change: the chip is in the returned area on the change and the fade keeps it dirty until settled.
  Rect before = chipBounds(c);
  r = step(c, 3, 0, false);
  CHECK(!r.empty() && inside(r, before) && inside(r, chipBounds(c)), "status change returns the chip area");
  int dirty_ticks = 1;
  for (int i = 0; i < 60; ++i) dirty_ticks += !step(c, 3, 0, false).empty();
  CHECK(dirty_ticks >= 10 && dirty_ticks <= 20, "status fade dirty for %d ticks (about 15 expected)", dirty_ticks);
  CHECK(step(c, 3, 0, false).empty(), "fade over, still returns an area");

  // A dog goes out: the area covers the new badge. It comes in: the area covers the old badge.
  before = chipBounds(c);
  r = step(c, 3, 1, false);
  Rect after = chipBounds(c);
  CHECK(after.x0 < before.x0 && inside(r, before) && inside(r, after), "dog out: area %d..%d, new bounds from %d", r.x0, r.x1, after.x0);
  run(c, 3, 1, false, 40);
  before = chipBounds(c);
  r = step(c, 3, 3, false);
  after = chipBounds(c);
  CHECK(after.x0 < before.x0 && inside(r, after), "second dog out");
  run(c, 3, 3, false, 40);
  before = chipBounds(c);
  r = step(c, 3, 0, false);
  after = chipBounds(c);
  CHECK(inside(r, before) && inside(r, after) && after.x0 > before.x0, "both dogs in: the area must cover the old badges (%d..%d vs %d)", r.x0, r.x1, before.x0);
  CHECK(step(c, 3, 0, false).empty(), "dogs gone and settled: returns an area");

  // Night: the dim eases over a couple of seconds, then holds.
  int night_ticks = 0;
  for (int i = 0; i < 200; ++i) night_ticks += !step(c, 3, 0, true).empty();
  CHECK(night_ticks > 5 && night_ticks <= 60, "night dim dirty for %d ticks", night_ticks);
  CHECK(step(c, 3, 0, true).empty(), "night settled still returns an area");

  // The strong form: random inputs for 4000 ticks, and every changed pixel lies inside the returned area.
  const Rect corner{480, 380, kW, kH};
  CHECK(inside(corner, Rect{kChipX0 - kChipBadgeGap - 2 * kChipBadgeD - kChipBadgeStep - 20, kChipY0 - 20, kW, kH}), "corner too small");
  ChipState s;
  chipInit(s);
  renderOver(s, day, g_bg, g_prev);
  uint32_t rng = 987654321u;
  auto rnd = [&rng](uint32_t n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % n; };
  int status = 5, dog = 0;
  bool nt = false;
  long changed_ticks = 0, empty_ticks = 0, doorbells = 0, bad = 0;
  for (int t = 0; t < 4000; ++t) {
    if (rnd(90) == 0) status = (int)rnd(6);
    if (rnd(110) == 0) dog = (int)rnd(4);
    if (rnd(320) == 0) nt = !nt;
    if (rnd(200) == 0) { chipDoorbell(s); ++doorbells; }
    const Rect ret = step(s, status, dog, nt);
    renderOver(s, nt ? night : day, g_bg, g_cur);
    const Rect dd = diffBox(g_cur, g_prev, corner);
    if (!dd.empty()) {
      ++changed_ticks;
      if (ret.empty() || !inside(ret, dd)) {
        ++bad;
        if (bad < 5) printf("  FAIL tick %d: changed %d,%d,%d,%d, returned %d,%d,%d,%d\n", t, dd.x0, dd.y0, dd.x1, dd.y1, ret.x0, ret.y0, ret.x1, ret.y1);
      }
    } else {
      if (ret.empty()) ++empty_ticks;
    }
    CHECK(inside(kFrameRect, ret), "returned area leaves the frame");
    memcpy(g_prev, g_cur, sizeof(Pixel) * (size_t)kW * kH);
  }
  CHECK(bad == 0, "%ld ticks changed pixels outside the returned area", bad);
  CHECK(changed_ticks > 500 && empty_ticks > 500, "the random run is not varied enough (%ld changed, %ld still)", changed_ticks, empty_ticks);
  printf("  random run: 4000 ticks, %ld changed the frame, %ld were still, %ld doorbells; %ld outside the returned area\n",
         changed_ticks, empty_ticks, doorbells, bad);
}

// ---- (f) the pulse -------------------------------------------------------------------

static void testPulse() {
  printf("pulse\n");
  ChipState c = settled(1, 0, false);
  CHECK(chipBounds(c).x0 == kChipX0 && c.look.ring_a == 0, "a settled chip has no ring");
  chipDoorbell(c);
  CHECK(c.look.ring_a == 0, "the doorbell shows at the next update, not before");
  const int kN = 300;
  static int ring_a[300], ring_r[300], glow[300];
  static Rect ret[300];
  int end_tick = -1;
  for (int i = 0; i < kN; ++i) {
    ret[i] = step(c, 1, 0, false);
    ring_a[i] = c.look.ring_a;
    ring_r[i] = c.look.ring_r;
    glow[i] = c.look.glow;
    if (end_tick < 0 && c.pulse_t < 0.f) end_tick = i;
  }
  int rings = 0, peaks = 0, max_r = 0, max_a = 0;
  for (int i = 0; i < kN; ++i) {
    if (ring_a[i] > (i ? ring_a[i - 1] : 0) + 50) ++rings;   // a ring starts at full opacity
    if (i > 0 && i + 1 < kN && glow[i] >= glow[i - 1] && glow[i] > glow[i + 1] && glow[i] > 100) ++peaks;
    if (ring_r[i] > max_r) max_r = ring_r[i];
    if (ring_a[i] > max_a) max_a = ring_a[i];
  }
  CHECK(end_tick > 0, "the pulse never ended");
  const float seconds = (float)end_tick * kDt;
  CHECK(fabsf(seconds - kChipPulseSeconds) < 0.2f, "pulse ended at %.2f s, want %.1f", (double)seconds, (double)kChipPulseSeconds);
  for (int i = end_tick; i < kN && end_tick > 0; ++i) {
    CHECK(ring_a[i] == 0 && ring_r[i] == 0 && glow[i] == 0, "the pulse is still in the look at tick %d, after it ended", i);
    if (i > end_tick) CHECK(ret[i].empty(), "the chip changed after the pulse ended (tick %d)", i);
  }
  CHECK(rings == kChipPulseCount, "%d rings, want %d", rings, kChipPulseCount);
  CHECK(peaks == kChipPulseCount, "%d glow peaks, want %d", peaks, kChipPulseCount);
  CHECK(max_r >= 4 * kChipRingGrow - 4 && max_r <= 4 * kChipRingGrow, "ring reached %d quarter px, want about %d", max_r, 4 * kChipRingGrow);
  CHECK(max_a > 150, "ring only reached opacity %d of 255", max_a);
  CHECK(c.look.ring_a == 0 && chipBounds(c).x0 == kChipX0 && chipBounds(c).y0 == kChipY0, "bounds back to the chip");
  printf("  ended at %.2f s, %d rings, ring grows to %.1f px, peak opacity %d/255, %d glow peaks\n", (double)seconds, rings,
         (double)max_r / 4.0, max_a, peaks);

  // What the pulse looks like: the chip's body brightens at each peak and comes back exactly; a ring is a band
  // that leaves the chip, not a filled halo.
  {
    const Palette day = dayPalette();
    const int bx = kChipX0 + 30, by = kChipY0 + 5;
    ChipState q = settled(1, 0, false);
    renderOver(q, day, g_bg, g_cur);
    const Pixel rest = g_cur[by * kW + bx];
    float best = lum(rest);
    chipDoorbell(q);
    for (int i = 0; i < 270; ++i) {
      step(q, 1, 0, false);
      renderOver(q, day, g_bg, g_cur);
      if (lum(g_cur[by * kW + bx]) > best) best = lum(g_cur[by * kW + bx]);
      if (i == 30) {   // u = 0.48: the ring is out about 8 px
        const int cx = kChipX0 + kChipW / 2;
        int band = 0;
        for (int y = kChipY0 - 14; y < kChipY0; ++y) band += g_cur[y * kW + cx] != g_bg[y * kW + cx];
        const int gap = g_cur[(kChipY0 - 2) * kW + cx] != g_bg[(kChipY0 - 2) * kW + cx];
        CHECK(band >= 2 && band <= 5, "the ring is %d px thick at the top, want 2 to 4 (with its soft edges)", band);
        CHECK(gap == 0, "the ring is a filled halo: the pixel just above the chip is changed");
      }
    }
    CHECK(best > lum(rest) + 0.02f, "the chip does not brighten during the pulse (%.3f against %.3f at rest)", (double)best, (double)lum(rest));
    renderOver(q, day, g_bg, g_cur);
    CHECK(g_cur[by * kW + bx] == rest, "the chip's color is not back to rest after the pulse");
  }

  // A doorbell during a pulse restarts it; it does not stack.
  chipDoorbell(c);
  run(c, 1, 0, false, 100);
  CHECK(c.pulse_t > 3.5f && c.pulse_t < 4.5f, "pulse at %.2f s after 100 ticks", (double)c.pulse_t);
  chipDoorbell(c);
  CHECK(c.pulse_t == 0.f, "a second doorbell restarts the pulse");
  run(c, 1, 0, false, 200);
  CHECK(c.pulse_t > 7.f, "the restarted pulse is still running at 8 s");
  // Status and dogs keep following their inputs during a pulse.
  run(c, 4, 3, false, 100);
  CHECK(c.look.status == 4 && c.look.dogs == 3, "state follows the inputs during a pulse");
}

// ---- (g) bad inputs ---------------------------------------------------------------------

static void testBadInputs() {
  printf("bad inputs\n");
  ChipState c;
  chipInit(c);
  step(c, -5, -3, false);
  CHECK(c.look.status == kChipNoSignal && c.look.dogs == 0, "negative inputs");
  step(c, 99, 7, false);
  CHECK(c.look.status == kChipNoSignal && c.look.dogs == 3, "oversize inputs: status %d dogs %d", c.look.status, c.look.dogs);
  chipUpdate(c, 1, 0, true, -1.f);
  chipUpdate(c, 1, 0, true, 1000.f);
  chipUpdate(c, 1, 0, true, NAN);
  CHECK(c.look.status == 1, "bad dt");
  fillNoise(g_bg);
  renderOver(c, dayPalette(), g_bg, g_cur);
  const Rect d = diffBox(g_cur, g_bg, kFrameRect);
  CHECK(!d.empty() && inside(chipBounds(c), d), "draw after bad inputs");
}

// ---- (h) draw cost ---------------------------------------------------------------------

static void testCost() {
  printf("cost\n");
  static Pixel strip_buf[SS_WIDTH * SS_STRIP_ROWS];
  const Palette day = dayPalette();
  struct Case { const char* name; ChipState c; };
  Case cases[3];
  cases[0] = {"chip alone, settled", settled(0, 0, false)};
  cases[1] = {"chip and two badges", settled(1, 3, false)};
  cases[2] = {"two badges and a ring", settled(1, 3, false)};
  chipDoorbell(cases[2].c);
  run(cases[2].c, 1, 3, false, 30);
  for (int k = 0; k < 3; ++k) {
    const int iters = 400;
    int strips = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < iters; ++it) {
      for (int sy = 0; sy < SS_STRIP_COUNT; ++sy) {
        Surface strip{strip_buf, kW, SS_STRIP_ROWS, kW, sy * SS_STRIP_ROWS};
        chipDraw(cases[k].c, strip, Rect{0, sy * SS_STRIP_ROWS, kW, (sy + 1) * SS_STRIP_ROWS}, day);
      }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
    const Rect b = chipBounds(cases[k].c);
    for (int sy = 0; sy < SS_STRIP_COUNT; ++sy) {
      if (b.y1 > sy * SS_STRIP_ROWS && b.y0 < (sy + 1) * SS_STRIP_ROWS) ++strips;
    }
    printf("  %-24s %6.0f us over the %d strips it touches (PC, -O%s; the panel is UNVERIFIED, expect several times slower)\n",
           cases[k].name, us, strips,
#ifdef __OPTIMIZE__
           "2"
#else
           "0"
#endif
    );
  }
}

// ---- (i) the contact sheet ----------------------------------------------------------------

// The sheet crops this part of the frame for every cell.
static const Rect kCrop{530, 408, 800, 480};

struct Sheet {
  int cols, rows, cw, ch;
  unsigned char* rgb;
  int w() const { return cols * cw; }
  int h() const { return rows * ch; }
  void cell(int col, int row, const Pixel* fb) {
    for (int y = 0; y < ch; ++y) {
      for (int x = 0; x < cw; ++x) {
        const Pixel p = fb[(kCrop.y0 + y) * kW + kCrop.x0 + x];
        unsigned char* o = rgb + (((size_t)row * ch + y) * (size_t)w() + (size_t)col * cw + x) * 3;
        o[0] = red8(p); o[1] = green8(p); o[2] = blue8(p);
      }
    }
  }
};

static bool writePPM(const char* path, const unsigned char* rgb, int w, int h) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  fwrite(rgb, 1, (size_t)w * h * 3, f);
  fclose(f);
  return true;
}

// A real frame of the scene (the app run past its build-up), for the sheet's background.
static bool sceneFrame(float mood, bool night, Pixel* out, Palette* pal) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.weather.night_dim = night ? 1.f : 0.f;
  in.night_mode = night;
  in.have_outside = true;
  void* mem = malloc(SS_APP_BYTES);
  Pixel* fb = (Pixel*)calloc((size_t)kW * kH, sizeof(Pixel));
  Surface surf = frameSurface(fb);
  App* app = (mem && fb) ? appStart(mem, kAlloc, 7, in) : nullptr;
  if (!app) {
    free(mem);
    free(fb);
    return false;
  }
  for (int t = 0; t < 700; ++t) {
    appTick(app);
    appRender(app, surf);
  }
  appInvalidate(app);
  appRender(app, surf);
  memcpy(out, fb, sizeof(Pixel) * (size_t)kW * kH);
  *pal = *appPalette(app);
  appStop(app, kAlloc);
  free(mem);
  free(fb);
  return true;
}

static void testSheet(const char* dir) {
  printf("contact sheet\n");
  static Pixel scene[2][SS_WIDTH * SS_HEIGHT];
  Palette pal[2];
  const bool ok_day = sceneFrame(3.f, false, scene[0], &pal[0]);
  const bool ok_night = sceneFrame(0.f, true, scene[1], &pal[1]);
  if (!ok_day || !ok_night) {
    printf("  scene frames unavailable: the sheet falls back to a flat background\n");
    for (int i = 0; i < kW * kH; ++i) { scene[0][i] = rgb565_hex(0x6F7F5F); scene[1][i] = rgb565_hex(0x1E2A22); }
    pal[0] = dayPalette();
    pal[1] = nightPalette();
  }
  rasterSetScratch(g_scratch);   // appStop took the app's out
  const int cw = kCrop.x1 - kCrop.x0, ch = kCrop.y1 - kCrop.y0;
  // Rows: per status, day then night; columns: no dog, Lucy, Copper, both. Then pulse rows (day, night).
  Sheet sh{4, 6 * 2 + 2, cw, ch, nullptr};
  sh.rgb = (unsigned char*)calloc((size_t)sh.w() * sh.h() * 3, 1);
  for (int st = 0; st < kChipStatusCount; ++st) {
    for (int night = 0; night < 2; ++night) {
      for (int dog = 0; dog < 4; ++dog) {
        ChipState c = settled(st, dog, night != 0);
        renderOver(c, pal[night], scene[night], g_cur);
        sh.cell(dog, st * 2 + night, g_cur);
      }
    }
  }
  // Pulse frames: Busy with both dogs, at the start of a ring, mid-ring, late, and the next peak.
  static const float kT[4] = {0.3f, 0.9f, 1.8f, 2.7f};
  for (int night = 0; night < 2; ++night) {
    for (int k = 0; k < 4; ++k) {
      ChipState c = settled(2, 3, night != 0);
      chipDoorbell(c);
      const int ticks = (int)(kT[k] / kDt + 0.5f);
      run(c, 2, 3, night != 0, ticks);
      renderOver(c, pal[night], scene[night], g_cur);
      sh.cell(k, 12 + night, g_cur);
    }
  }
  char path[512];
  snprintf(path, sizeof(path), "%s%schip_sheet.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, sh.rgb, sh.w(), sh.h()), "could not write %s", path);
  printf("  wrote %s (%dx%d)\n", path, sh.w(), sh.h());
  free(sh.rgb);

  // A 3x view of four of them: both dogs by day, both dogs at night, and two pulse frames.
  const int z = 3;
  const int zw = cw * z, zh = ch * z;
  unsigned char* zrgb = (unsigned char*)calloc((size_t)zw * zh * 4 * 3, 1);
  for (int k = 0; k < 4; ++k) {
    ChipState c;
    int night = k & 1;
    if (k < 2) {
      c = settled(k == 0 ? 0 : 2, 3, night != 0);
    } else {
      c = settled(k == 2 ? 1 : 4, 3, false);
      chipDoorbell(c);
      run(c, k == 2 ? 1 : 4, 3, false, k == 2 ? 14 : 55);
      night = 0;
    }
    renderOver(c, pal[night], scene[night], g_cur);
    for (int y = 0; y < zh; ++y) {
      for (int x = 0; x < zw; ++x) {
        const Pixel p = g_cur[(kCrop.y0 + y / z) * kW + kCrop.x0 + x / z];
        unsigned char* o = zrgb + (((size_t)k * zh + y) * zw + x) * 3;
        o[0] = red8(p); o[1] = green8(p); o[2] = blue8(p);
      }
    }
  }
  snprintf(path, sizeof(path), "%s%schip_zoom.ppm", dir, dir[0] ? "/" : "");
  CHECK(writePPM(path, zrgb, zw, zh * 4), "could not write %s", path);
  printf("  wrote %s (%dx%d)\n", path, zw, zh * 4);
  free(zrgb);
}

int main(int argc, char** argv) {
  g_scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(g_scratch);
  const char* dir = argc > 1 ? argv[1] : "";
  fillNoise(g_bg);

  testWords();
  testGeometry();
  testColors();
  testBoundsAndClip();
  testUpdate();
  testPulse();
  testBadInputs();
  testCost();
  testSheet(dir);

  printf("%d checks, %d failed\n", g_checks, g_fail);
  free(g_scratch);
  return g_fail ? 1 : 0;
}
