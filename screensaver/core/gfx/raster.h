// Anti-aliased rasterizer.
//
// Every shape is filled with exact-area edge coverage: interior pixels are
// written as plain spans, edge pixels are blended by their fractional coverage.
// Coordinates are floats in frame space, with pixel (x, y) covering the square
// [x, x+1) x [y, y+1). All drawing is clipped to `clip` intersected with the
// surface bounds, and nothing outside is touched.
//
// There is no allocation. The rasterizer uses a fixed static scratch sized for
// SS_WIDTH, so it is not reentrant across threads (the screensaver draws from
// one task).
#pragma once

#include "surface.h"

namespace ss {

// Fill a polygon (non-zero winding). `pts` may be convex or concave, clockwise
// or counter-clockwise, self-intersecting. n <= SS_MAX_POLY_POINTS.
#define SS_MAX_POLY_POINTS 64
void fillPolygon(Surface& s, const Vec2* pts, int n, Pixel color, const Rect& clip);

// Same, with a global opacity 0..255 multiplied into the edge coverage.
void fillPolygonAlpha(Surface& s, const Vec2* pts, int n, Pixel color, uint8_t alpha, const Rect& clip);

// Filled circle and ellipse with anti-aliased edge.
void fillCircle(Surface& s, Vec2 c, float r, Pixel color, const Rect& clip);
void fillEllipse(Surface& s, Vec2 c, float rx, float ry, Pixel color, const Rect& clip);

// Thick line with round caps (a capsule). Used for limbs, branches, stems.
void strokeCapsule(Surface& s, Vec2 a, Vec2 b, float width, Pixel color, const Rect& clip);

// Axis-aligned rectangle with integer edges: a plain span fill, no blending.
void fillRect(Surface& s, const Rect& r, Pixel color, const Rect& clip);

// Axis-aligned rectangle with float edges: anti-aliased on all four sides.
void fillRectF(Surface& s, float x0, float y0, float x1, float y1, Pixel color, const Rect& clip);

// Horizontal span helpers used by the sky bands and the strip compositor.
void fillSpan(Surface& s, int y, int x0, int x1, Pixel color);               // clipped to surface
void blendSpan(Surface& s, int y, int x0, int x1, Pixel color, uint8_t alpha);

// Copy a rectangle of pixels from `src` to `dst` at the same frame coordinates.
// Both surfaces are in frame space (origin_y respected). Rows are memcpy'd.
void copyRect(Surface& dst, const Surface& src, const Rect& r);

}  // namespace ss
