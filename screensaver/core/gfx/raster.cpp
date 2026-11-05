// Exact-area anti-aliased scanline rasterizer for RGB565 surfaces.
//
// Algorithm (signed-area accumulation, as in font-rs and stb_truetype v2):
//   For each output row, every polygon edge crossing the row adds its exact
//   signed area into a one-row float accumulator: the pixel the edge passes
//   through gets the area of that pixel to the right of the edge, and the next
//   pixel(s) get the remainder ("cover"), so a prefix sum along the row yields
//   the signed coverage of every pixel. Winding is non-zero: coverage is
//   min(|sum|, 1). Pixels at full coverage are collected into runs and written
//   as plain spans; partial pixels are blended by their coverage.
//
// Clipping:
//   - Rows: only rows inside the clip are visited, and an edge is evaluated at
//     the row's clamped y range, so huge y coordinates cost nothing.
//   - Columns: before rasterizing, every edge is split where it crosses the
//     clip's left and right boundaries and its x is clamped into [x0, x1]. A
//     piece left of the clip becomes a vertical edge on the left boundary,
//     which deposits its whole cover into accumulator cell 0, so interior
//     coverage stays right when a shape extends past the left edge. Pieces on
//     the right boundary only affect pixels at or past x1 and are dropped.
//     Clamping the outline in x this way preserves the winding number of every
//     point strictly inside the clip, so coverage there is unchanged.
//
// Scratch memory: one static accumulation row (SS_WIDTH + 2 floats), a static
// edge table and a static point buffer for generated outlines. NOT reentrant:
// all drawing must happen from one task/thread at a time.
//
// Determinism: each row is computed only from the edges and that row's y, with
// edges accumulated in a fixed order (stable sort by top y), so drawing the
// same shape into a full frame or into strips gives bit-identical pixels.
#include "raster.h"
#include "blend_impl.h"

#include <math.h>
#include <string.h>

namespace ss {

namespace {

using gfx_detail::alphaG;
using gfx_detail::alphaRB;
using gfx_detail::blendPlanes;

// Each polygon edge splits into at most three pieces at the clip's left and
// right boundaries.
constexpr int kMaxEdges = 3 * SS_MAX_POLY_POINTS;

// Coverage at or above this rounds to alpha 255: treat as full (plain span).
constexpr float kFullCoverage = 254.5f / 255.0f;

// Coordinates beyond this (or NaN/inf) are rejected; float precision there is
// already far coarser than a pixel, and it keeps all arithmetic finite.
constexpr float kMaxCoord = 1.0e12f;

// Edges whose vertical extent is below this are dropped. They carry at most
// this much area per pixel (invisible), and dropping them keeps dx/dy finite.
constexpr float kMinEdgeDy = 1.0e-6f;

struct Edge {
  float x0, y0;   // top end (local x: frame x minus clip x0)
  float x1, y1;   // bottom end, y1 > y0
  float dxdy;
  float xmin, xmax;
  float dir;      // +1 if the original edge pointed down, -1 if up
};

// Static scratch (see non-reentrancy note above).
float g_acc[SS_WIDTH + 2];
Edge g_edges[kMaxEdges];
int16_t g_order[kMaxEdges];
int16_t g_active[kMaxEdges];
Vec2 g_shape[SS_MAX_POLY_POINTS];
int16_t g_ivlo[kMaxEdges];   // per-row touched cell ranges
int16_t g_ivhi[kMaxEdges];

struct EdgeBuild {
  int count;
  float ymin, ymax;
  float w;          // clip width as float
  float cy0, cy1;   // clip rows as float
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void addEdge(EdgeBuild& eb, float xa, float ya, float xb, float yb) {
  float dir = 1.0f;
  if (yb < ya) {
    float t = xa; xa = xb; xb = t;
    t = ya; ya = yb; yb = t;
    dir = -1.0f;
  }
  float dy = yb - ya;
  if (!(dy >= kMinEdgeDy)) return;
  float xmin = xa < xb ? xa : xb;
  float xmax = xa < xb ? xb : xa;
  if (xmin >= eb.w) return;                         // on the right boundary: no effect inside
  if (yb <= eb.cy0 || ya >= eb.cy1) return;         // entirely above or below the clip rows
  if (eb.count >= kMaxEdges) return;                // cannot happen for n <= SS_MAX_POLY_POINTS
  Edge& e = g_edges[eb.count++];
  e.x0 = xa; e.y0 = ya; e.x1 = xb; e.y1 = yb;
  e.dxdy = (xb - xa) / dy;
  e.xmin = xmin; e.xmax = xmax;
  e.dir = dir;
  if (ya < eb.ymin) eb.ymin = ya;
  if (yb > eb.ymax) eb.ymax = yb;
}

// Split edge p->q (local x) at x = 0 and x = w, clamp x into [0, w], add pieces.
void addClippedEdge(EdgeBuild& eb, float px, float py, float qx, float qy) {
  if (py == qy) return;
  const float w = eb.w;
  float ts[2], xs[2];
  int nt = 0;
  float dx = qx - px;
  if ((px < 0.0f) != (qx < 0.0f)) { ts[nt] = (0.0f - px) / dx; xs[nt] = 0.0f; ++nt; }
  if ((px < w) != (qx < w)) { ts[nt] = (w - px) / dx; xs[nt] = w; ++nt; }
  if (nt == 2 && ts[1] < ts[0]) {
    float t = ts[0]; ts[0] = ts[1]; ts[1] = t;
    t = xs[0]; xs[0] = xs[1]; xs[1] = t;
  }
  float ax = clampf(px, 0.0f, w), ay = py;
  for (int i = 0; i < nt; ++i) {
    float t = clampf(ts[i], 0.0f, 1.0f);
    float by = py + t * (qy - py);
    addEdge(eb, ax, ay, xs[i], by);
    ax = xs[i];
    ay = by;
  }
  addEdge(eb, ax, ay, clampf(qx, 0.0f, w), qy);
}

// Deposit one edge piece lying within a single row. xa is x at the piece's top,
// xb at its bottom, both in [0, w]. d = signed vertical extent (dy * dir).
// Exact area: the pixel(s) the piece crosses get the area to the right of the
// piece within the row band, and the cell after gets the rest of d.
// [plo, phi] receives the range of cells touched.
inline void accumulate(float xa, float xb, float d, int& plo, int& phi) {
  float x0 = xa < xb ? xa : xb;
  float x1 = xa < xb ? xb : xa;
  float x0floor = floorf(x0);
  int x0i = (int)x0floor;
  float x1ceil = ceilf(x1);
  int x1i = (int)x1ceil;
  plo = x0i;
  if (x1i <= x0i + 1) {
    // Piece stays within one pixel column.
    float xmf = 0.5f * (xa + xb) - x0floor;
    g_acc[x0i] += d - d * xmf;
    g_acc[x0i + 1] += d * xmf;
    phi = x0i + 1;
    return;
  }
  float s = 1.0f / (x1 - x0);
  float x0f = x0 - x0floor;
  float a0 = 0.5f * s * (1.0f - x0f) * (1.0f - x0f);
  float x1f = x1 - x1ceil + 1.0f;
  float am = 0.5f * s * x1f * x1f;
  g_acc[x0i] += d * a0;
  if (x1i == x0i + 2) {
    g_acc[x0i + 1] += d * (1.0f - a0 - am);
  } else {
    float a1 = s * (1.5f - x0f);
    g_acc[x0i + 1] += d * (a1 - a0);
    float ds = d * s;
    for (int xi = x0i + 2; xi < x1i - 1; ++xi) g_acc[xi] += ds;
    float a2 = a1 + (float)(x1i - x0i - 3) * s;
    g_acc[x1i - 1] += d * (1.0f - a2 - am);
  }
  g_acc[x1i] += d * am;
  phi = x1i;
}

inline void spanFill(Pixel* p, int n, Pixel color) {
  for (int i = 0; i < n; ++i) p[i] = color;
}

inline void spanBlend(Pixel* p, int n, uint32_t s_rb, uint32_t s_g, uint32_t a_rb, uint32_t a_g) {
  for (int i = 0; i < n; ++i) p[i] = blendPlanes(p[i], s_rb, s_g, a_rb, a_g);
}

bool coordOk(float v) { return fabsf(v) <= kMaxCoord; }   // false for NaN and inf

Rect effectiveClip(const Surface& s, const Rect& clip) {
  Rect c = clip.clipped(s.bounds());
  // The accumulator is one SS_WIDTH row; a wider surface is clipped to the
  // first SS_WIDTH columns of the clip.
  if (c.x1 - c.x0 > SS_WIDTH) c.x1 = c.x0 + SS_WIDTH;
  return c;
}

// Emits one row's pixels left to right, merging fully covered pixels into
// runs written as plain spans (or one uniform blend when a global alpha is set).
struct RowWriter {
  Pixel* line;
  Pixel color;
  uint32_t s_rb, s_g, full_rb, full_g;
  float ga;       // global alpha as float, 0..255
  bool opaque;    // global alpha is 255
  int run;        // start of the open full-coverage run, or -1

  void flush(int at) {
    if (run < 0) return;
    if (opaque) spanFill(line + run, at - run, color);
    else spanBlend(line + run, at - run, s_rb, s_g, full_rb, full_g);
    run = -1;
  }
  void pixel(int i, float cov) {
    if (cov >= kFullCoverage) {
      if (run < 0) run = i;
      return;
    }
    flush(i);
    uint32_t a = (uint32_t)(cov * ga + 0.5f);
    if (a != 0) line[i] = blendPlanes(line[i], s_rb, s_g, alphaRB(a), alphaG(a));
  }
  void constant(int x0, int x1, float cov) {   // pixels [x0, x1) share one coverage
    if (x1 <= x0) return;
    if (cov >= kFullCoverage) {
      if (run < 0) run = x0;
      return;
    }
    flush(x0);
    uint32_t a = (uint32_t)(cov * ga + 0.5f);
    if (a != 0) spanBlend(line + x0, x1 - x0, s_rb, s_g, alphaRB(a), alphaG(a));
  }
};

void rasterPolygon(Surface& s, const Vec2* pts, int n, Pixel color, uint8_t alpha, const Rect& clip) {
  if (pts == nullptr || n < 3 || n > SS_MAX_POLY_POINTS || alpha == 0) return;
  Rect c = effectiveClip(s, clip);
  if (c.empty()) return;
  for (int i = 0; i < n; ++i) {
    if (!coordOk(pts[i].x) || !coordOk(pts[i].y)) return;
  }

  EdgeBuild eb;
  eb.count = 0;
  eb.ymin = 3.0e38f;
  eb.ymax = -3.0e38f;
  eb.w = (float)(c.x1 - c.x0);
  eb.cy0 = (float)c.y0;
  eb.cy1 = (float)c.y1;
  const float cx0 = (float)c.x0;
  for (int i = 0; i < n; ++i) {
    const Vec2& p = pts[i];
    const Vec2& q = pts[i + 1 < n ? i + 1 : 0];
    addClippedEdge(eb, p.x - cx0, p.y, q.x - cx0, q.y);
  }
  const int ne = eb.count;
  if (ne == 0) return;

  // Stable insertion sort of edge indices by top y.
  for (int i = 0; i < ne; ++i) {
    int16_t idx = (int16_t)i;
    int j = i;
    while (j > 0 && g_edges[g_order[j - 1]].y0 > g_edges[idx].y0) {
      g_order[j] = g_order[j - 1];
      --j;
    }
    g_order[j] = idx;
  }

  int row0 = (int)floorf(clampf(eb.ymin, eb.cy0, eb.cy1));
  int row1 = (int)ceilf(clampf(eb.ymax, eb.cy0, eb.cy1));
  if (row0 < c.y0) row0 = c.y0;
  if (row1 > c.y1) row1 = c.y1;

  const int w = c.x1 - c.x0;
  const uint32_t s_rb = color & 0xF81Fu;
  const uint32_t s_g = color & 0x07E0u;
  const float ga = (float)alpha;
  const uint32_t full_rb = alphaRB(alpha), full_g = alphaG(alpha);

  int next = 0, nactive = 0;
  for (int y = row0; y < row1; ++y) {
    const float fy0 = (float)y, fy1 = fy0 + 1.0f;
    while (next < ne && g_edges[g_order[next]].y0 < fy1) g_active[nactive++] = g_order[next++];

    int niv = 0;
    int keep = 0;
    for (int k = 0; k < nactive; ++k) {
      const Edge& e = g_edges[g_active[k]];
      float ya = e.y0 > fy0 ? e.y0 : fy0;
      float yb = e.y1 < fy1 ? e.y1 : fy1;
      if (yb > ya) {
        float xa = clampf(e.x0 + (ya - e.y0) * e.dxdy, e.xmin, e.xmax);
        float xb = clampf(e.x0 + (yb - e.y0) * e.dxdy, e.xmin, e.xmax);
        int plo, phi;
        accumulate(xa, xb, (yb - ya) * e.dir, plo, phi);
        // Insert the touched range into the row's interval list, sorted by start.
        int j = niv++;
        while (j > 0 && g_ivlo[j - 1] > plo) {
          g_ivlo[j] = g_ivlo[j - 1];
          g_ivhi[j] = g_ivhi[j - 1];
          --j;
        }
        g_ivlo[j] = (int16_t)plo;
        g_ivhi[j] = (int16_t)phi;
      }
      if (e.y1 > fy1) g_active[keep++] = g_active[k];   // still live next row
    }
    nactive = keep;
    if (niv == 0) continue;

    // Prefix-sum only the touched cells. Between touched ranges the
    // accumulator is zero, so coverage is constant there and the gap is
    // written as one span (plain fill when fully covered). Past the last
    // range the coverage is zero, unless the shape runs off the clip's right
    // edge (those edge pieces were dropped), in which case it extends to the
    // clip's right edge.
    RowWriter rw{s.row(y) + c.x0, color, s_rb, s_g, full_rb, full_g, ga, alpha == 255, -1};
    float sum = 0.0f;
    int pos = -1;   // first pixel not yet emitted, -1 before the first range
    int k = 0;
    while (k < niv) {
      int a = g_ivlo[k], b = g_ivhi[k];
      for (++k; k < niv && g_ivlo[k] <= b + 1; ++k) {
        if (g_ivhi[k] > b) b = g_ivhi[k];
      }
      if (pos >= 0) rw.constant(pos, a < w ? a : w, fabsf(sum));
      const int e = b < w - 1 ? b : w - 1;
      for (int i = a; i <= e; ++i) {
        sum += g_acc[i];
        g_acc[i] = 0.0f;
        rw.pixel(i, fabsf(sum));
      }
      for (int i = (a > e + 1 ? a : e + 1); i <= b; ++i) g_acc[i] = 0.0f;   // cells past the clip
      if (e + 1 > pos) pos = e + 1;
    }
    if (pos < w) {
      rw.constant(pos, w, fabsf(sum));
      pos = w;
    }
    rw.flush(pos);
  }
}

// Segment count for a closed curve of radius r. With vertices on radius r*k
// (k chosen so the polygon area equals pi r^2, see areaMatchScale) the polygon
// alternately bulges past and cuts inside the true curve; the worst deviation
// is about r*(pi/N)^2/3. N is chosen for a deviation under kCurveTol (0.03 px,
// tighter than the 0.1 px brief: at 0.1 px a r=40 circle showed facet errors
// of ~0.08 coverage on edge pixels, at 0.03 px about 0.03), so
// N >= pi*sqrt(r / (3*kCurveTol)). Rounded up to a multiple of 4 for symmetry
// and capped at SS_MAX_POLY_POINTS: at the cap the deviation is 0.03 px at
// r = 37, 0.1 px at r = 124 and 0.2 px at r = 249.
constexpr float kCurveTol = 0.03f;
int circleSegments(float r) {
  float nf = 3.14159265f * sqrtf(r / (3.0f * kCurveTol));
  int n = (int)ceilf(nf < 64.0f ? nf : 64.0f);
  n = (n + 3) & ~3;
  if (n < 8) n = 8;
  if (n > SS_MAX_POLY_POINTS) n = SS_MAX_POLY_POINTS;
  return n;
}

// Radius scale making a regular N-gon's area equal to the circle's.
float areaMatchScale(int n) {
  float a = 6.28318531f / (float)n;
  return sqrtf(a / sinf(a));
}

}  // namespace

void fillPolygon(Surface& s, const Vec2* pts, int n, Pixel color, const Rect& clip) {
  rasterPolygon(s, pts, n, color, 255, clip);
}

void fillPolygonAlpha(Surface& s, const Vec2* pts, int n, Pixel color, uint8_t alpha, const Rect& clip) {
  rasterPolygon(s, pts, n, color, alpha, clip);
}

void fillEllipse(Surface& s, Vec2 c, float rx, float ry, Pixel color, const Rect& clip) {
  if (!(rx > 0.0f) || !(ry > 0.0f)) return;
  if (!coordOk(c.x) || !coordOk(c.y) || !coordOk(rx) || !coordOk(ry)) return;
  const int n = circleSegments(rx > ry ? rx : ry);
  const float k = areaMatchScale(n);
  const float step = 6.28318531f / (float)n;
  const float cstep = cosf(step), sstep = sinf(step);
  const float kx = rx * k, ky = ry * k;
  float cs = 1.0f, sn = 0.0f;   // rotation recurrence: one sinf/cosf per shape
  for (int i = 0; i < n; ++i) {
    g_shape[i].x = c.x + kx * cs;
    g_shape[i].y = c.y + ky * sn;
    float t = cs * cstep - sn * sstep;
    sn = sn * cstep + cs * sstep;
    cs = t;
  }
  rasterPolygon(s, g_shape, n, color, 255, clip);
}

void fillCircle(Surface& s, Vec2 c, float r, Pixel color, const Rect& clip) {
  fillEllipse(s, c, r, r, color, clip);
}

void strokeCapsule(Surface& s, Vec2 a, Vec2 b, float width, Pixel color, const Rect& clip) {
  const float r = 0.5f * width;
  if (!(r > 0.0f)) return;
  if (!coordOk(a.x) || !coordOk(a.y) || !coordOk(b.x) || !coordOk(b.y) || !coordOk(r)) return;
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0e-4f) {
    fillCircle(s, Vec2{0.5f * (a.x + b.x), 0.5f * (a.y + b.y)}, r, color, clip);
    return;
  }
  const float ux = dx / len, uy = dy / len;   // along the axis
  const float nx = -uy, ny = ux;              // normal
  // m segments per semicircle; 2*(m+1) points must fit SS_MAX_POLY_POINTS.
  int m = circleSegments(r) / 2;
  if (m > SS_MAX_POLY_POINTS / 2 - 1) m = SS_MAX_POLY_POINTS / 2 - 1;
  // Arc end points sit exactly at radius r so the straight sides keep the true
  // width; interior arc vertices are area-matched like a circle.
  const float k = areaMatchScale(2 * m);
  const float step = 3.14159265f / (float)m;
  const float cstep = cosf(step), sstep = sinf(step);
  // Both caps use the same angle sequence (rotation recurrence, one sinf/cosf
  // per shape). Cap around b runs +n -> +u -> -n at indices 0..m; cap around
  // a runs -n -> -u -> +n at indices m+1..2m+1.
  float cs = 1.0f, sn = 0.0f;
  for (int i = 0; i <= m; ++i) {
    if (i == m) { cs = -1.0f; sn = 0.0f; }   // land exactly on the far side
    float rr = (i == 0 || i == m) ? r : r * k;
    float ox = rr * (nx * cs + ux * sn);
    float oy = rr * (ny * cs + uy * sn);
    g_shape[i].x = b.x + ox;
    g_shape[i].y = b.y + oy;
    g_shape[m + 1 + i].x = a.x - ox;
    g_shape[m + 1 + i].y = a.y - oy;
    float t = cs * cstep - sn * sstep;
    sn = sn * cstep + cs * sstep;
    cs = t;
  }
  const int n = 2 * (m + 1);
  rasterPolygon(s, g_shape, n, color, 255, clip);
}

void fillRect(Surface& s, const Rect& r, Pixel color, const Rect& clip) {
  Rect c = r.clipped(clip).clipped(s.bounds());
  if (c.empty()) return;
  for (int y = c.y0; y < c.y1; ++y) spanFill(s.row(y) + c.x0, c.x1 - c.x0, color);
}

void fillRectF(Surface& s, float x0, float y0, float x1, float y1, Pixel color, const Rect& clip) {
  if (!coordOk(x0) || !coordOk(y0) || !coordOk(x1) || !coordOk(y1)) return;
  if (x1 < x0) { float t = x0; x0 = x1; x1 = t; }
  if (y1 < y0) { float t = y0; y0 = y1; y1 = t; }
  Rect c = clip.clipped(s.bounds());
  if (c.empty()) return;
  // Clamp to the clip in float first so int conversion is always safe; the
  // coverage of a pixel inside the clip is unchanged by this.
  const float fx0 = clampf(x0, (float)c.x0, (float)c.x1);
  const float fx1 = clampf(x1, (float)c.x0, (float)c.x1);
  const float fy0 = clampf(y0, (float)c.y0, (float)c.y1);
  const float fy1 = clampf(y1, (float)c.y0, (float)c.y1);
  if (!(fx1 > fx0) || !(fy1 > fy0)) return;

  const int ix0 = (int)floorf(fx0);           // first touched column
  const int ixe = (int)ceilf(fx1);            // one past last touched column
  const int in0 = (int)ceilf(fx0);            // first fully covered column
  const int in1 = (int)floorf(fx1);           // one past last fully covered column
  const int iy0 = (int)floorf(fy0);
  const int iy1 = (int)ceilf(fy1);
  const uint32_t s_rb = color & 0xF81Fu;
  const uint32_t s_g = color & 0x07E0u;

  for (int y = iy0; y < iy1; ++y) {
    float top = fy0 > (float)y ? fy0 : (float)y;
    float bot = fy1 < (float)(y + 1) ? fy1 : (float)(y + 1);
    float cy = bot - top;
    if (!(cy > 0.0f)) continue;
    Pixel* line = s.row(y);
    if (ixe - ix0 == 1) {   // the whole rectangle is inside one column
      uint32_t a = (uint32_t)((fx1 - fx0) * cy * 255.0f + 0.5f);
      if (a >= 255) line[ix0] = color;
      else if (a) line[ix0] = blendPlanes(line[ix0], s_rb, s_g, alphaRB(a), alphaG(a));
      continue;
    }
    uint32_t ay = (uint32_t)(cy * 255.0f + 0.5f);
    if (ix0 < in0) {   // left partial column
      uint32_t a = (uint32_t)(((float)in0 - fx0) * cy * 255.0f + 0.5f);
      if (a) line[ix0] = blendPlanes(line[ix0], s_rb, s_g, alphaRB(a), alphaG(a));
    }
    if (in1 > in0 && ay) {
      if (ay >= 255) spanFill(line + in0, in1 - in0, color);
      else spanBlend(line + in0, in1 - in0, s_rb, s_g, alphaRB(ay), alphaG(ay));
    }
    if (in1 < ixe) {   // right partial column
      uint32_t a = (uint32_t)((fx1 - (float)in1) * cy * 255.0f + 0.5f);
      if (a) line[in1] = blendPlanes(line[in1], s_rb, s_g, alphaRB(a), alphaG(a));
    }
  }
}

void fillSpan(Surface& s, int y, int x0, int x1, Pixel color) {
  if (y < s.origin_y || y >= s.origin_y + s.height) return;
  if (x0 < 0) x0 = 0;
  if (x1 > s.width) x1 = s.width;
  if (x1 <= x0) return;
  spanFill(s.row(y) + x0, x1 - x0, color);
}

void blendSpan(Surface& s, int y, int x0, int x1, Pixel color, uint8_t alpha) {
  if (alpha == 0) return;
  if (alpha == 255) {
    fillSpan(s, y, x0, x1, color);
    return;
  }
  if (y < s.origin_y || y >= s.origin_y + s.height) return;
  if (x0 < 0) x0 = 0;
  if (x1 > s.width) x1 = s.width;
  if (x1 <= x0) return;
  spanBlend(s.row(y) + x0, x1 - x0, color & 0xF81Fu, color & 0x07E0u, alphaRB(alpha), alphaG(alpha));
}

void copyRect(Surface& dst, const Surface& src, const Rect& r) {
  Rect c = r.clipped(dst.bounds()).clipped(src.bounds());
  if (c.empty()) return;
  const size_t bytes = (size_t)(c.x1 - c.x0) * sizeof(Pixel);
  for (int y = c.y0; y < c.y1; ++y) memmove(dst.row(y) + c.x0, src.row(y) + c.x0, bytes);
}

}  // namespace ss
