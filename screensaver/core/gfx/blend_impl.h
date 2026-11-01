// Private to core/gfx: the RGB565 blend kernel shared by surface.cpp and
// raster.cpp. Not part of the public API; include only from gfx/*.cpp.
//
// Two-plane trick: the 5-bit red and blue fields are blended together in the
// 0xF81F plane (6 spare bits between them hold the fractional part of blue's
// product, so red and blue never interfere), and the 6-bit green field is
// blended on its own in the 0x07E0 plane. Red/blue use a 6-bit alpha (0..64),
// green an 8-bit-plus-one alpha (0..256): more precision than the channels
// carry, so translucent fills (haze, fog) do not band. This costs two
// multiplies instead of the one a single 32-bit plane would; the single-plane
// form only has room for a 5-bit alpha (33 levels), which visibly quantizes
// low-alpha fills.
//
// Each plane rounds to nearest (the + half constants), so the blend is not
// biased toward dst or src. alpha 0 gives dst and full alpha gives src,
// exactly. Negative differences wrap in uint32 arithmetic; the logical shift
// then sets a stray bit far above the field (bit 26 for red/blue, bit 24 for
// green), which the final mask removes.
#pragma once

#include "surface.h"

namespace ss {
namespace gfx_detail {

// Map an 8-bit alpha (0..255) to the per-plane alphas, rounding to nearest.
inline uint32_t alphaRB(uint32_t a8) { return (a8 * 257u + 512u) >> 10; }   // 0..64
inline uint32_t alphaG(uint32_t a8) { return a8 + (a8 >> 7); }               // 0..256

// Blend with the source already split into planes and alphas already mapped.
inline Pixel blendPlanes(Pixel dst, uint32_t s_rb, uint32_t s_g, uint32_t a_rb, uint32_t a_g) {
  uint32_t d_rb = dst & 0xF81Fu;
  uint32_t d_g = dst & 0x07E0u;
  // Rounding: half of 64 at blue's slot (0x20) and at red's slot (0x20 << 11).
  uint32_t rb = (d_rb + (((s_rb - d_rb) * a_rb + 0x10020u) >> 6)) & 0xF81Fu;
  // Rounding: half of 256 at green's slot (0x80 << 5).
  uint32_t g = (d_g + (((s_g - d_g) * a_g + 0x1000u) >> 8)) & 0x07E0u;
  return (Pixel)(rb | g);
}

}  // namespace gfx_detail
}  // namespace ss
