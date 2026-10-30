// Strip renderer and layer cache.
//
// The static part of the scene (sky bands, ridges, far trees) is drawn once
// into the layer cache (a full RGB565 frame in host-provided large memory) and
// redrawn only when the palette changes. Each frame the renderer walks the
// SS_STRIP_COUNT horizontal strips. A strip that no dynamic element touches is
// skipped. A dirty strip is composited in a small strip buffer (internal SRAM
// on the board): copy the cache rows, draw every dynamic element clipped to
// the strip, then write the strip to the framebuffer in one sequential run.
//
// Drawing is expressed through a DrawList callback so the scene layer stays
// free of buffer management. The renderer never allocates after init().
#pragma once

#include "../gfx/surface.h"
#include "../palette.h"

namespace ss {

struct Renderer;

// Scene-side callbacks. `strip` is a Surface in frame space (origin_y set) and
// `clip` is the strip's rectangle; the scene draws whatever intersects it.
struct SceneDraw {
  void* user;
  // Draw the static layers into a full-frame surface. Called on palette change.
  void (*draw_static)(void* user, Surface& frame, const Palette& pal);
  // Draw the dynamic layers that intersect `clip`. Called per dirty strip.
  void (*draw_dynamic)(void* user, Surface& strip, const Rect& clip, const Palette& pal);
};

struct Renderer {
  Pixel* cache;                 // SS_WIDTH * SS_HEIGHT, large memory
  Pixel  strip_buf[SS_WIDTH * SS_STRIP_ROWS];   // 51,200 bytes, internal SRAM
  uint16_t dirty_mask;          // bit i = strip i needs compositing this frame
  bool cache_valid;
  Palette palette;

  // Allocates the cache through `alloc`. Returns false if allocation fails.
  bool init(const HostAlloc& alloc);
  void shutdown(const HostAlloc& alloc);

  // New palette: the cache is rebuilt on the next frame and every strip is
  // marked dirty.
  void setPalette(const Palette& p);

  // Mark the strips covering frame rows [y0, y1) dirty. Call for every
  // dynamic element's bounding box, both its previous and current position.
  void markDirtyRows(int y0, int y1);
  void markDirtyRect(const Rect& r) { markDirtyRows(r.y0, r.y1); }
  void markAllDirty() { dirty_mask = (uint16_t)((1u << SS_STRIP_COUNT) - 1); }

  // Composite dirty strips into `fb` (a full-frame surface, origin_y 0).
  // Rebuilds the cache first if needed. Clears the dirty mask afterwards.
  // Returns the number of strips written (for the frame-time log).
  int render(Surface& fb, const SceneDraw& scene);
};

}  // namespace ss
