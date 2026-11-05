// Strip renderer and layer cache. See renderer.h.
#include "renderer.h"

#include <string.h>

namespace ss {

bool Renderer::init(const HostAlloc& alloc, Pixel* strip) {
  cache = nullptr;
  strip_buf = strip;
  if (alloc.alloc_large == nullptr || strip == nullptr) return false;
  void* mem = alloc.alloc_large((size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
  if (mem == nullptr) return false;
  cache = static_cast<Pixel*>(mem);
  memset(cache, 0, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
  memset(&palette, 0, sizeof palette);
  cache_valid = false;
  spread_next = false;
  spread_mask = 0;
  spread_pos = 0;
  markAllDirty();
  return true;
}

void Renderer::shutdown(const HostAlloc& alloc) {
  if (cache != nullptr && alloc.free_large != nullptr) alloc.free_large(cache);
  cache = nullptr;
  cache_valid = false;
  dirty_mask = 0;
}

void Renderer::setPalette(const Palette& p, int spread_bands) {
  palette = p;
  if (spread_bands > 0 && cache_valid) {
    // The strips keep the old colours until the cache rebuild hands them to the spread. A step
    // during a spread goes on at the faster of the two speeds.
    const uint8_t bands = (uint8_t)(spread_bands > SS_STRIP_COUNT ? SS_STRIP_COUNT : spread_bands);
    spread_per_frame = spread_mask && spread_per_frame > bands ? spread_per_frame : bands;
    cache_valid = false;
    spread_next = true;
    return;
  }
  cache_valid = false;
  spread_next = false;
  spread_mask = 0;
  markAllDirty();
}

// Columns marked dirty are widened by this much each side: a few elements draw a pixel or two past
// the bounds they mark (anti-aliased fringes, the hat fanned at arm's length), which never showed
// while every dirty strip was redrawn full width.
constexpr int kColPad = 4;

void Renderer::markDirtyCols(int y0, int y1, int x0, int x1) {
  x0 -= kColPad;
  x1 += kColPad;
  if (y0 < 0) y0 = 0;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
  if (x0 < 0) x0 = 0;
  if (x1 > SS_WIDTH) x1 = SS_WIDTH;
  if (y1 <= y0 || x1 <= x0) return;
  const int s0 = y0 / SS_STRIP_ROWS;
  const int s1 = (y1 - 1) / SS_STRIP_ROWS;   // strip holding the last dirty row
  for (int s = s0; s <= s1; ++s) {
    if (!(dirty_mask & (1u << s))) {
      dirty_x0[s] = (int16_t)x0;
      dirty_x1[s] = (int16_t)x1;
    } else {
      if (x0 < dirty_x0[s]) dirty_x0[s] = (int16_t)x0;
      if (x1 > dirty_x1[s]) dirty_x1[s] = (int16_t)x1;
    }
    dirty_mask = (uint16_t)(dirty_mask | (1u << s));
  }
}

void Renderer::fullWidthRows(int y0, int y1) {
  if (y0 < 0) y0 = 0;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
  if (y1 <= y0) return;
  for (int s = y0 / SS_STRIP_ROWS; s <= (y1 - 1) / SS_STRIP_ROWS; ++s) full_mask = (uint16_t)(full_mask | (1u << s));
}

int Renderer::render(Surface& fb, const SceneDraw& scene) {
  if (cache == nullptr) return 0;
  if (fb.pixels == nullptr || fb.width < SS_WIDTH || fb.height < SS_HEIGHT || fb.origin_y != 0 || fb.stride < SS_WIDTH) return 0;

  uint32_t t = clock_us ? clock_us() : 0;
  auto lap = [&](uint32_t& acc) {
    if (!clock_us) return;
    const uint32_t now = clock_us();
    acc += now - t;
    t = now;
  };

  if (!cache_valid) {
    if (spread_next) {
      // A spread step: the cache keeps the old colours and is repainted band by band below.
      spread_mask = (uint16_t)((1u << SS_STRIP_COUNT) - 1);
      spread_next = false;
    } else {
      Surface full{cache, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
      if (scene.draw_static) scene.draw_static(scene.user, full, palette);
      markAllDirty();
      lap(prof.static_us);
    }
    cache_valid = true;
  }

  // The spread: this frame's share of the bands still in the old colours, going on round the frame
  // from where it got to (top first for a step on its own). Each band of the cache is repainted (the
  // static layers clip to the band, as the build-up's do to a strip) and its strip composited full
  // width. Other dirty strips copy only their changed columns, as always, from cache bands that may
  // still be in the old colours: a fraction of a mood step off, for a few frames.
  if (spread_mask) {
    int budget = spread_per_frame > 0 ? spread_per_frame : SS_STRIP_COUNT;
    for (int k = 0; k < SS_STRIP_COUNT && budget > 0; ++k) {
      const int i = (spread_pos + k) % SS_STRIP_COUNT;
      const uint16_t bit = (uint16_t)(1u << i);
      if (!(spread_mask & bit)) continue;
      const int y0 = i * SS_STRIP_ROWS;
      Surface band{cache + (size_t)y0 * SS_WIDTH, SS_WIDTH, SS_STRIP_ROWS, SS_WIDTH, y0};
      if (scene.draw_static) scene.draw_static(scene.user, band, palette);
      markDirtyRows(y0, y0 + SS_STRIP_ROWS);
      full_mask = (uint16_t)(full_mask | bit);
      spread_mask = (uint16_t)(spread_mask & ~bit);
      spread_pos = (uint8_t)((i + 1) % SS_STRIP_COUNT);
      --budget;
    }
    lap(prof.static_us);
  }

  int written = 0;
  for (int i = 0; i < SS_STRIP_COUNT; ++i) {
    if (!(dirty_mask & (1u << i))) continue;
    const int y0 = i * SS_STRIP_ROWS;
    const bool full = (full_mask & (1u << i)) != 0;
    const int x0 = full ? 0 : dirty_x0[i], x1 = full ? SS_WIDTH : dirty_x1[i];
    const size_t run = (size_t)(x1 - x0) * sizeof(Pixel);
    Surface strip{strip_buf, SS_WIDTH, SS_STRIP_ROWS, SS_WIDTH, y0};
    // Only the changed columns come in from the cache; the rest of the strip buffer holds whatever
    // was there and is never copied out. A full-width strip is one run (cache stride = frame width).
    if (x0 == 0 && x1 == SS_WIDTH) {
      memcpy(strip_buf, cache + (size_t)y0 * SS_WIDTH, (size_t)SS_WIDTH * SS_STRIP_ROWS * sizeof(Pixel));
    } else {
      for (int r = 0; r < SS_STRIP_ROWS; ++r)
        memcpy(strip_buf + (size_t)r * SS_WIDTH + x0, cache + (size_t)(y0 + r) * SS_WIDTH + x0, run);
    }
    lap(prof.copy_in_us);
    const Rect clip{0, y0, SS_WIDTH, y0 + SS_STRIP_ROWS};
    if (scene.draw_dynamic) scene.draw_dynamic(scene.user, strip, clip, palette);
    lap(prof.draw_us);
    // fb may have a different stride, so copy row by row.
    for (int r = 0; r < SS_STRIP_ROWS; ++r)
      memcpy(fb.row(y0 + r) + x0, strip_buf + (size_t)r * SS_WIDTH + x0, run);
    lap(prof.copy_out_us);
    prof.strips++;
    prof.px += (uint32_t)(x1 - x0) * SS_STRIP_ROWS;
    ++written;
  }
  dirty_mask = 0;
  full_mask = 0;
  return written;
}

}  // namespace ss
