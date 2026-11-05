// Strip renderer and layer cache. See renderer.h.
#include "renderer.h"

#include <string.h>

namespace ss {

bool Renderer::init(const HostAlloc& alloc) {
  cache = nullptr;
  if (alloc.alloc_large == nullptr) return false;
  void* mem = alloc.alloc_large((size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
  if (mem == nullptr) return false;
  cache = static_cast<Pixel*>(mem);
  memset(cache, 0, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
  memset(&palette, 0, sizeof palette);
  cache_valid = false;
  markAllDirty();
  return true;
}

void Renderer::shutdown(const HostAlloc& alloc) {
  if (cache != nullptr && alloc.free_large != nullptr) alloc.free_large(cache);
  cache = nullptr;
  cache_valid = false;
  dirty_mask = 0;
}

void Renderer::setPalette(const Palette& p) {
  palette = p;
  cache_valid = false;
  markAllDirty();
}

void Renderer::markDirtyRows(int y0, int y1) {
  if (y0 < 0) y0 = 0;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
  if (y1 <= y0) return;
  const int s0 = y0 / SS_STRIP_ROWS;
  const int s1 = (y1 - 1) / SS_STRIP_ROWS;   // strip holding the last dirty row
  for (int s = s0; s <= s1; ++s) dirty_mask = (uint16_t)(dirty_mask | (1u << s));
}

int Renderer::render(Surface& fb, const SceneDraw& scene) {
  if (cache == nullptr) return 0;
  if (fb.pixels == nullptr || fb.width < SS_WIDTH || fb.height < SS_HEIGHT || fb.origin_y != 0 || fb.stride < SS_WIDTH) return 0;

  if (!cache_valid) {
    Surface full{cache, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    if (scene.draw_static) scene.draw_static(scene.user, full, palette);
    cache_valid = true;
    markAllDirty();
  }

  int written = 0;
  for (int i = 0; i < SS_STRIP_COUNT; ++i) {
    if (!(dirty_mask & (1u << i))) continue;
    const int y0 = i * SS_STRIP_ROWS;
    Surface strip{strip_buf, SS_WIDTH, SS_STRIP_ROWS, SS_WIDTH, y0};
    // Cache stride equals the frame width, so the strip is one contiguous run.
    memcpy(strip_buf, cache + (size_t)y0 * SS_WIDTH,
           (size_t)SS_WIDTH * SS_STRIP_ROWS * sizeof(Pixel));
    const Rect clip{0, y0, SS_WIDTH, y0 + SS_STRIP_ROWS};
    if (scene.draw_dynamic) scene.draw_dynamic(scene.user, strip, clip, palette);
    // fb may have a different stride, so copy row by row.
    for (int r = 0; r < SS_STRIP_ROWS; ++r) {
      memcpy(fb.row(y0 + r), strip_buf + (size_t)r * SS_WIDTH, (size_t)SS_WIDTH * sizeof(Pixel));
    }
    ++written;
  }
  dirty_mask = 0;
  return written;
}

}  // namespace ss
