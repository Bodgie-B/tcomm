// Quick check of the artwork without LVGL: composes sky, strip and tiles for one phase into a
// raw RGB file (800 x 480 x 3). tools/raw2png.py turns it into a PNG.
//   art_preview.exe <phase 0-5> <out.rgb>
#include "core/types.h"
#include "gfx/canvas.h"
#include "ui/art.h"
#include "ui/theme.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace gfx;

static void paste(std::vector<uint8_t> &rgb, const Canvas &c, int ox, int oy) {
  for (int y = 0; y < c.height(); y++)
    for (int x = 0; x < c.width(); x++) {
      uint8_t a;
      Color s = c.get(x, y, &a);
      int X = ox + x, Y = oy + y;
      if (X < 0 || Y < 0 || X >= 800 || Y >= 480 || a == 0) continue;
      uint8_t *d = &rgb[(Y * 800 + X) * 3];
      float t = a / 255.0f;
      d[0] = (uint8_t)(d[0] + (s.r - d[0]) * t);
      d[1] = (uint8_t)(d[1] + (s.g - d[1]) * t);
      d[2] = (uint8_t)(d[2] + (s.b - d[2]) * t);
    }
}

int main(int argc, char **argv) {
  int ph = argc > 1 ? atoi(argv[1]) : 3;
  const char *out = argc > 2 ? argv[2] : "preview.rgb";
  static const float kP[6] = {0.35f, 0.03f, 0.22f, 0.48f, 0.88f, 0.985f};
  tc::Phase phase = (tc::Phase)ph;
  tc::Mode m = tc::mode_for(phase);
  std::vector<uint8_t> rgb(800 * 480 * 3);
  Canvas sky;
  sky.create(800, 480, false);
  art::sky(sky, phase, kP[ph], phase == tc::Phase::Night);
  for (int y = 0; y < 480; y++)
    for (int x = 0; x < 800; x++) {
      Color c = sky.get(x, y);
      uint8_t *d = &rgb[(y * 800 + x) * 3];
      d[0] = c.r; d[1] = c.g; d[2] = c.b;
    }
  Canvas strip;
  strip.create(644, 56, true);
  art::strip(strip, phase, kP[ph], phase == tc::Phase::Night, false, 0);
  paste(rgb, strip, 78, 6);
  Canvas up, dn, outside, bell, b1, b2;
  up.create(514, 193, true);
  dn.create(514, 193, true);
  art::floor_tile(up, tc::Floor::Upstairs, ph % 6, m);
  art::floor_tile(dn, tc::Floor::Downstairs, (ph + 2) % 6, m);
  paste(rgb, up, 12, 74);
  paste(rgb, dn, 12, 279);
  outside.create(250, 193, true);
  art::outside_tile(outside, m);
  paste(rgb, outside, 538, 74);
  bell.create(250, 193, true);
  art::bell_tile(bell, m, false);
  paste(rgb, bell, 538, 279);
  b1.create(80, 80, true);
  theme::StatusColors s1 = theme::status_colors(ph % 6, m);
  art::badge(b1, ph % 6, s1.text, s1.tile);
  paste(rgb, b1, 12 + 289, 74 + 105);
  b2.create(80, 80, true);
  theme::StatusColors s2 = theme::status_colors((ph + 2) % 6, m);
  art::badge(b2, (ph + 2) % 6, s2.text, s2.tile);
  paste(rgb, b2, 12 + 289, 279 + 105);
  Canvas fc;
  fc.create(88, 88, true);
  art::forecast(fc, (tc::Forecast)(ph + 1), phase == tc::Phase::Night, theme::palette(m).outside_text);
  paste(rgb, fc, 538 + 20, 74 + 42);
  Canvas paw, gear;
  paw.create(36, 36, true);
  art::icon(paw, art::Icon::PawIn, theme::palette(m).dog);
  gear.create(32, 32, true);
  art::icon(gear, art::Icon::Gear, theme::palette(m).gear_ink);
  paste(rgb, paw, 12 + 10, 6 + 10);
  paste(rgb, gear, 732 + 12, 6 + 12);
  FILE *f = std::fopen(out, "wb");
  if (!f) return 1;
  std::fwrite(rgb.data(), 1, rgb.size(), f);
  std::fclose(f);
  return 0;
}
