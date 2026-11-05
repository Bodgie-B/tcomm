// RGB565 color helpers. See blend_impl.h for the blend kernel.
#include "surface.h"
#include "blend_impl.h"

namespace ss {

Pixel blend565(Pixel dst, Pixel src, uint8_t alpha) {
  if (alpha == 0) return dst;
  if (alpha == 255) return src;
  return gfx_detail::blendPlanes(dst, src & 0xF81Fu, src & 0x07E0u,
                                 gfx_detail::alphaRB(alpha), gfx_detail::alphaG(alpha));
}

Pixel lerp565(Pixel a, Pixel b, uint8_t t) {
  return blend565(a, b, t);
}

}  // namespace ss
