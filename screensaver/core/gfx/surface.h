// RGB565 surface, rectangles, vectors and color helpers.
#pragma once

#include "../ss_platform.h"

namespace ss {

struct Vec2 {
  float x, y;
};

struct Rect {
  int x0, y0, x1, y1;   // half-open: pixels x0..x1-1, y0..y1-1

  bool empty() const { return x1 <= x0 || y1 <= y0; }
  Rect clipped(const Rect& o) const {
    Rect r{x0 > o.x0 ? x0 : o.x0, y0 > o.y0 ? y0 : o.y0,
           x1 < o.x1 ? x1 : o.x1, y1 < o.y1 ? y1 : o.y1};
    return r;
  }
};

// A view onto a pixel buffer. `pixels` points at row 0 of the view. `stride` is
// in pixels (not bytes). A strip buffer is a Surface whose rows map to a band of
// the frame; `origin_y` records where row 0 sits in frame coordinates so shapes
// in frame space can be drawn into it without the caller translating.
struct Surface {
  Pixel* pixels;
  int width;
  int height;
  int stride;
  int origin_y;   // frame y of row 0; 0 for a full frame

  Rect bounds() const { return Rect{0, origin_y, width, origin_y + height}; }
  Pixel* row(int frame_y) { return pixels + (frame_y - origin_y) * stride; }
  const Pixel* row(int frame_y) const { return pixels + (frame_y - origin_y) * stride; }
};

// Color helpers. RGB565: rrrrr gggggg bbbbb.
inline constexpr Pixel rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (Pixel)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
inline constexpr Pixel rgb565_hex(uint32_t rgb) {   // 0xRRGGBB
  return rgb565((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}
inline uint8_t red8(Pixel p)   { uint8_t v = (uint8_t)((p >> 8) & 0xF8); return (uint8_t)(v | (v >> 5)); }
inline uint8_t green8(Pixel p) { uint8_t v = (uint8_t)((p >> 3) & 0xFC); return (uint8_t)(v | (v >> 6)); }
inline uint8_t blue8(Pixel p)  { uint8_t v = (uint8_t)((p << 3) & 0xF8); return (uint8_t)(v | (v >> 5)); }

// Blend `src` over `dst` with coverage alpha in 0..255 (255 = opaque src).
// Implementations should use the two-plane RGB565 trick so one multiply blends
// all three channels. alpha 0 returns dst, alpha 255 returns src, exactly.
Pixel blend565(Pixel dst, Pixel src, uint8_t alpha);

// Linear interpolation between two colors, t in 0..255 (0 = a, 255 = b).
Pixel lerp565(Pixel a, Pixel b, uint8_t t);

}  // namespace ss
