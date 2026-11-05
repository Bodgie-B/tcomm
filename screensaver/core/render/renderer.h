// Strip renderer and layer cache.
//
// The static part of the scene (sky bands, ridges, far trees) is drawn once
// into the layer cache (a full RGB565 frame in host-provided large memory) and
// redrawn only when the palette changes. Each frame the renderer walks the
// SS_STRIP_COUNT horizontal strips. A strip that no dynamic element touches is
// skipped. A dirty strip is composited in a small strip buffer (the head of
// the host's fast block, internal SRAM on the board): copy the cache rows, draw every dynamic element clipped to
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

// Time spent in render(), for the host's frame log (appProfile). Microseconds; only counted when the
// host gives a clock (appSetClock), so the PC tests see zeros.
struct RenderProfile {
  uint32_t copy_in_us;     // cache rows into the strip buffer
  uint32_t draw_us;        // the dynamic layers, drawn into the strip
  uint32_t copy_out_us;    // the strip into the frame buffer
  uint32_t static_us;      // rebuilding the cache (palette or build-up changes)
  uint32_t strips;         // dirty strips composited
  uint32_t px;             // pixels copied out (a full strip is SS_WIDTH * SS_STRIP_ROWS)
};

struct Renderer {
  Pixel* cache;                 // SS_WIDTH * SS_HEIGHT, large memory
  Pixel* strip_buf;             // SS_STRIP_BYTES, the head of the fast block (ss_platform.h)
  uint16_t dirty_mask;          // bit i = strip i needs compositing this frame
  // The columns of each dirty strip that changed, [dirty_x0, dirty_x1): only those are copied in
  // from the cache and out to the frame buffer. The dynamic layers are still drawn across the whole
  // strip, so every copied pixel is exactly what a full redraw gives (the rasterizer's x clipping
  // isn't bit-exact at its edges, so the clip stays full width). Marks without columns
  // (markDirtyRows, markAllDirty) are full width.
  int16_t dirty_x0[SS_STRIP_COUNT];
  int16_t dirty_x1[SS_STRIP_COUNT];
  // Strips that copy in full width whenever they're dirty this frame (fullWidthRows): a layer that
  // reads its neighbours' pixels, like the heat shimmer shifting rows sideways, needs the whole row.
  uint16_t full_mask;
  // A palette step spread over frames (setPalette with spread_bands > 0): the cache is repainted in
  // the new colours spread_per_frame bands (strips) a frame, each band's strip composited as it's
  // done, instead of the whole cache in one frame (on the panel the whole cache is about 110 ms).
  // Until it finishes the frame is a mix of the two palettes, a fraction of a mood step apart.
  // spread_per_frame is the spread in progress's speed (the faster of its steps'). With no spread
  // asked for (the default, and the PC tests) everything is at once: a frame equals a full redraw.
  uint8_t spread_per_frame;
  bool spread_next;             // the next cache rebuild spreads instead of marking every strip
  uint16_t spread_mask;         // strips still showing the previous palette
  uint8_t spread_pos;           // the band the spread goes on from: round the frame, so a new step mid-spread
                                // still reaches every band within SS_STRIP_COUNT / spread_per_frame frames
  uint32_t (*clock_us)();       // the host's microsecond clock for the profile, or nullptr
  RenderProfile prof;
  bool cache_valid;
  Palette palette;

  // Allocates the cache through `alloc` and composites in `strip` (SS_STRIP_BYTES,
  // owned by the caller). Returns false if allocation fails.
  bool init(const HostAlloc& alloc, Pixel* strip);
  void shutdown(const HostAlloc& alloc);

  // New palette: the cache is rebuilt on the next frame and every strip is
  // marked dirty. spread_bands > 0: a step the eye won't follow band by band (a
  // mood or weather-tint step): spread it as above, that many bands a frame.
  void setPalette(const Palette& p, int spread_bands = 0);

  // Same palette, different static content (build-up phase boundary): rebuild
  // the cache on the next frame and mark every strip dirty.
  void invalidateCache() {
    cache_valid = false;
    spread_next = false;
    spread_mask = 0;
    markAllDirty();
  }

  // Mark the strips covering frame rows [y0, y1) dirty. Call for every
  // dynamic element's bounding box, both its previous and current position.
  void markDirtyRows(int y0, int y1) { markDirtyCols(y0, y1, 0, SS_WIDTH); }
  void markDirtyRect(const Rect& r) { markDirtyCols(r.y0, r.y1, r.x0, r.x1); }
  void markAllDirty() { markDirtyCols(0, SS_HEIGHT, 0, SS_WIDTH); }
  // Rows [y0, y1), columns [x0, x1) (clamped to the frame).
  void markDirtyCols(int y0, int y1, int x0, int x1);
  // Until the next render: any of rows [y0, y1) that is dirty composites full width.
  void fullWidthRows(int y0, int y1);

  // Composite dirty strips into `fb` (a full-frame surface, origin_y 0).
  // Rebuilds the cache first if needed. Clears the dirty mask afterwards.
  // Returns the number of strips written (for the frame-time log).
  int render(Surface& fb, const SceneDraw& scene);
};

}  // namespace ss
