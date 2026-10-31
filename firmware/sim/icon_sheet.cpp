// The forecast icons as the panel draws them, on the outside tile's colour in each light: one
// row each for day, evening and night (night with the moon for the sun), one column per
// WeatherGate symbol, then danger heat (disc and rays) and no forecast. Writes raw RGB;
// tools/raw2png.py turns it into a PNG.
//   icon_sheet.exe <out.rgb>     (kW x kH, printed)
#include "gfx/canvas.h"
#include "ui/art.h"
#include "ui/theme.h"
#include <cstdio>
#include <vector>

using gfx::Canvas;
using gfx::Color;
using tc::Forecast;

static const int kCell = 104, kCols = 10, kRows = 3, kW = kCell * kCols, kH = kCell * kRows;

static void paste(std::vector<uint8_t> &rgb, const Canvas &c, int ox, int oy) {
  for (int y = 0; y < c.height(); y++)
    for (int x = 0; x < c.width(); x++) {
      uint8_t a;
      Color s = c.get(x, y, &a);
      uint8_t *d = &rgb[((oy + y) * kW + ox + x) * 3];
      float t = a / 255.0f;
      d[0] = (uint8_t)(d[0] + (s.r - d[0]) * t);
      d[1] = (uint8_t)(d[1] + (s.g - d[1]) * t);
      d[2] = (uint8_t)(d[2] + (s.b - d[2]) * t);
    }
}

int main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : "icons.rgb";
  static const Forecast kCodes[] = {Forecast::Sun,  Forecast::CloudSun,  Forecast::Cloud, Forecast::Clearing,
                                    Forecast::Rain, Forecast::HeavyRain, Forecast::Snow,  Forecast::Thunder};
  static const tc::Mode kModes[kRows] = {tc::Mode::Day, tc::Mode::Evening, tc::Mode::Night};
  std::vector<uint8_t> rgb(kW * kH * 3);
  Canvas icon;
  icon.create(art::kForecast, art::kForecast, true);
  for (int r = 0; r < kRows; r++) {
    const theme::Palette &pal = theme::palette(kModes[r]);
    bool night = kModes[r] == tc::Mode::Night;
    for (int i = 0; i < kW * kCell; i++) {
      uint8_t *d = &rgb[(r * kCell * kW + i) * 3];
      d[0] = pal.outside.r;
      d[1] = pal.outside.g;
      d[2] = pal.outside.b;
    }
    for (int k = 0; k < kCols; k++) {
      int ox = k * kCell + (kCell - art::kForecast) / 2, oy = r * kCell + (kCell - art::kForecast) / 2;
      if (k < 8) {
        art::forecast(icon, kCodes[k], night, pal.outside_text);
        paste(rgb, icon, ox, oy);
      } else if (k == 8) {
        art::heat_sun(icon, false, pal.heat);
        paste(rgb, icon, ox, oy);
        art::heat_sun(icon, true, pal.heat);
        paste(rgb, icon, ox, oy);
      } else {
        art::forecast(icon, Forecast::Unknown, night, pal.outside_text);
        paste(rgb, icon, ox, oy);
      }
    }
  }
  FILE *f = std::fopen(out, "wb");
  if (!f) return 1;
  std::fwrite(rgb.data(), 1, rgb.size(), f);
  std::fclose(f);
  std::printf("%d %d\n", kW, kH);
  return 0;
}
