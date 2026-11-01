// The Arduino build uses -Os; this file is the hot loop of every redraw.
#pragma GCC optimize("O2")
#include "canvas.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gfx {

static Allocator g_alloc = {malloc, free};
void set_allocator(Allocator a) { g_alloc = a; }
void *alloc_bytes(size_t n) { return g_alloc.alloc(n); }
void release_bytes(void *p) { g_alloc.release(p); }

static RowHook g_row_hook = nullptr;
void set_row_hook(RowHook h) { g_row_hook = h; }
static inline void row_done() {
  if (g_row_hook) g_row_hook();
}

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }

// a toward b by t/256, rounded: what mix() does, without a float round per channel.
static inline uint8_t lerp8(uint8_t a, uint8_t b, int t) { return (uint8_t)((a * (256 - t) + b * t + 128) >> 8); }
static inline uint16_t mix565(uint16_t dst, Color c, float t) {
  uint8_t r = (dst >> 11) & 0x1F, g = (dst >> 5) & 0x3F, b = dst & 0x1F;
  Color d{(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)), (uint8_t)((b << 3) | (b >> 2))};
  int k = (int)(t * 256.0f + 0.5f);
  return to565(Color{lerp8(d.r, c.r, k), lerp8(d.g, c.g, k), lerp8(d.b, c.b, k)});
}

Color mix(Color a, Color b, float t) {
  t = clamp01(t);
  return Color{(uint8_t)lrintf(a.r + (b.r - a.r) * t), (uint8_t)lrintf(a.g + (b.g - a.g) * t),
               (uint8_t)lrintf(a.b + (b.b - a.b) * t)};
}

Color scale(Color c, float k) {
  auto ch = [k](uint8_t v) { float r = v * k; return (uint8_t)(r > 255 ? 255 : r < 0 ? 0 : lrintf(r)); };
  return Color{ch(c.r), ch(c.g), ch(c.b)};
}

uint16_t to565(Color c) { return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3)); }

static inline Color from565(uint16_t v) {
  uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
  return Color{(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)), (uint8_t)((b << 3) | (b >> 2))};
}

// Signed distance to a rounded box whose radius depends on the quadrant.
float RRect::coverage(float px, float py) const {
  // Quick answers away from the edges: most pixels of a tile are well inside it.
  if (px < x0 - 1 || px > x1 + 1 || py < y0 - 1 || py > y1 + 1) return 0;
  float rmax = tl > tr ? tl : tr;
  if (br > rmax) rmax = br;
  if (bl > rmax) rmax = bl;
  if (px >= x0 + 0.5f && px <= x1 - 0.5f && py >= y0 + 0.5f && py <= y1 - 0.5f &&
      ((px >= x0 + rmax && px <= x1 - rmax) || (py >= y0 + rmax && py <= y1 - rmax)))
    return 1;
  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
  float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
  float dx = px - cx, dy = py - cy;
  float r = dy < 0 ? (dx < 0 ? tl : tr) : (dx < 0 ? bl : br);
  float lim = hx < hy ? hx : hy;
  if (r > lim) r = lim;
  float qx = fabsf(dx) - hx + r, qy = fabsf(dy) - hy + r;
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float inside = qx > qy ? qx : qy;
  float sd = (inside < 0 ? inside : 0) + sqrtf(ox * ox + oy * oy) - r;
  return clamp01(0.5f - sd);
}

Canvas::~Canvas() { destroy(); }

// The scanline buffers in one block: row_ (w floats), run_ (w + 1), ev_ (w + 1), mark_ (w + 1).
size_t Canvas::scanline_bytes(int w) { return sizeof(float) * (2 * (size_t)w + 1) + 3 * ((size_t)w + 1); }

bool Canvas::create(int w, int h, bool alpha) {
  // Allocate first: if memory runs short, the old buffer (which an lv_img may still show) survives.
  size_t n = (size_t)w * h * (alpha ? 3 : 2);
  uint8_t *data = (uint8_t *)g_alloc.alloc(n);
  float *row = (float *)g_alloc.alloc(scanline_bytes(w));
  if (!data || !row) {
    if (data) g_alloc.release(data);
    if (row) g_alloc.release(row);
    return false;
  }
  destroy();
  view(data, row, w, h, alpha);
  owned_ = true;
  return true;
}

void Canvas::view(uint8_t *data, float *row, int w, int h, bool alpha) {
  destroy();
  owned_ = false;
  data_ = data;
  row_ = row;
  w_ = w;
  h_ = h;
  alpha_ = alpha;
  memset(data_, 0, bytes());
  run_ = row_ + w;
  ev_ = (uint16_t *)(run_ + w + 1);
  mark_ = (uint8_t *)(ev_ + w + 1);
  nev_ = 0;
  memset(row_, 0, scanline_bytes(w));
}

void Canvas::destroy() {
  if (owned_ && data_) g_alloc.release(data_);
  if (owned_ && row_) g_alloc.release(row_);
  owned_ = true;
  data_ = nullptr;
  row_ = run_ = nullptr;
  ev_ = nullptr;
  mark_ = nullptr;
  w_ = h_ = 0;
}

void Canvas::begin_strip(uint8_t *buf, int y0, int n) {
  strip_ = buf;
  sy0_ = y0;
  sy1_ = y0 + n;
  // Each strip replays the whole drawing, so it starts from the same state.
  atop_ = false;
  opacity_ = 1.0f;
  has_clip_ = false;
}

void Canvas::end_strip() {
  if (!strip_) return;
  memcpy(data_ + (size_t)sy0_ * row_bytes(), strip_, (size_t)(sy1_ - sy0_) * row_bytes());
  strip_ = nullptr;
}

void Canvas::clear(Color c, uint8_t a) {
  uint16_t v = to565(c);
  int px = w_ * (y_hi() - y_lo());
  uint8_t *d = at(0, y_lo());
  if (alpha_) {
    for (int i = 0; i < px; i++) {
      d[i * 3] = v & 0xFF;
      d[i * 3 + 1] = v >> 8;
      d[i * 3 + 2] = a;
    }
  } else {
    for (int i = 0; i < px; i++) {
      d[i * 2] = v & 0xFF;
      d[i * 2 + 1] = v >> 8;
    }
  }
}

void Canvas::set_clip(const RRect *r) {
  has_clip_ = r != nullptr;
  if (r) clip_ = *r;
}

Color Canvas::get(int x, int y, uint8_t *alpha) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return Color{0, 0, 0};
  size_t i = (size_t)y * w_ + x;
  if (alpha_) {
    if (alpha) *alpha = data_[i * 3 + 2];
    return from565((uint16_t)(data_[i * 3] | (data_[i * 3 + 1] << 8)));
  }
  if (alpha) *alpha = 255;
  return from565((uint16_t)(data_[i * 2] | (data_[i * 2 + 1] << 8)));
}

// Straight-alpha "source over" (or "source atop") with the coverage as source alpha.
void Canvas::blend(int x, int y, Color c, float cov) {
  if (x < 0 || y < y_lo() || x >= w_ || y >= y_hi()) return;
  if (has_clip_) cov *= clip_.coverage(x + 0.5f, y + 0.5f);
  cov *= opacity_;
  if (cov <= 0.002f) return;
  uint8_t *p = at(x, y);
  // Fully covered pixels (most of every shape) are a plain store.
  bool solid = cov >= 0.998f;
  if (!alpha_) {
    uint16_t v = solid ? to565(c) : mix565((uint16_t)(p[0] | (p[1] << 8)), c, cov);
    p[0] = v & 0xFF;
    p[1] = v >> 8;
    return;
  }
  if (atop_) {  // keep the destination's alpha; mix the color where there is something to paint on
    if (p[2] == 0) return;
    // The source covers cov of the pixel and lies inside the destination's da: it covers cov/da of
    // what's there. On an edge both shapes share, that's all of it, so no lighter halo.
    float rel = p[2] == 255 ? cov : cov * 255.0f / p[2];
    uint16_t v = rel >= 0.998f ? to565(c) : mix565((uint16_t)(p[0] | (p[1] << 8)), c, rel);
    p[0] = v & 0xFF;
    p[1] = v >> 8;
    return;
  }
  if (solid) {
    uint16_t v = to565(c);
    p[0] = v & 0xFF;
    p[1] = v >> 8;
    p[2] = 255;
    return;
  }
  float da = p[2] / 255.0f;
  Color d = from565((uint16_t)(p[0] | (p[1] << 8)));
  float oa = cov + da * (1 - cov);
  Color out;
  if (oa <= 0) out = c;
  else {
    float wd = da * (1 - cov) / oa, ws = cov / oa;
    out = Color{(uint8_t)lrintf(c.r * ws + d.r * wd), (uint8_t)lrintf(c.g * ws + d.g * wd),
                (uint8_t)lrintf(c.b * ws + d.b * wd)};
  }
  uint16_t v = to565(out);
  p[0] = v & 0xFF;
  p[1] = v >> 8;
  p[2] = (uint8_t)lrintf(oa * 255.0f);
}

template <class F>
void Canvas::shade(float x0, float y0, float x1, float y1, Color c, F coverage) {
  int ix0 = (int)floorf(x0) - 1, iy0 = (int)floorf(y0) - 1, ix1 = (int)ceilf(x1) + 1, iy1 = (int)ceilf(y1) + 1;
  if (ix0 < 0) ix0 = 0;
  if (iy0 < y_lo()) iy0 = y_lo();
  if (ix1 > w_) ix1 = w_;
  if (iy1 > y_hi()) iy1 = y_hi();
  for (int y = iy0; y < iy1; y++) {
    for (int x = ix0; x < ix1; x++) {
      float cov = coverage(x + 0.5f, y + 0.5f);
      if (cov > 0) blend(x, y, c, cov);
    }
    row_done();
  }
}

// Coverage is the overlap of the pixel with the rectangle, row share times column share.
void Canvas::fill_rect(float x0, float y0, float x1, float y1, Color c) {
  int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
  if (ix0 < 0) ix0 = 0;
  if (iy0 < y_lo()) iy0 = y_lo();
  if (ix1 > w_) ix1 = w_;
  if (iy1 > y_hi()) iy1 = y_hi();
  // Columns whose share is whole: x0 <= x and x + 1 <= x1.
  int fx0 = (int)ceilf(x0), fx1 = (int)floorf(x1) - 1;
  for (int y = iy0; y < iy1; y++) {
    float cy = clamp01(minf(y + 1.0f, y1) - maxf((float)y, y0));
    int a = ix1, b = ix1 - 1;  // the whole pixels of this row, if the row itself is whole
    if (cy >= 1.0f) {
      a = fx0 > ix0 ? fx0 : ix0;
      b = fx1 < ix1 - 1 ? fx1 : ix1 - 1;
    }
    for (int x = ix0; x < ix1; x++) {
      if (x == a && a <= b) {
        int sa, sb;
        store_span(y, sa, sb);
        solid_run(y, a, b, c, sa, sb);
        x = b;
        continue;
      }
      float cov = cy * clamp01(minf(x + 1.0f, x1) - maxf((float)x, x0));
      if (cov > 0) blend(x, y, c, cov);
    }
    row_done();
  }
}

void Canvas::fill_rrect(const RRect &r, Color c) {
  shade(r.x0, r.y0, r.x1, r.y1, c, [&](float px, float py) { return r.coverage(px, py); });
}

// Circles go row by row: inside r - 0.5 the coverage is 1, so only the edge pixels need a sqrt.
void Canvas::fill_circle(float cx, float cy, float r, Color c) {
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  if (y0 < y_lo()) y0 = y_lo();
  if (y1 > y_hi()) y1 = y_hi();
  float ro = r + 0.5f, ri = r - 0.5f;
  for (int y = y0; y < y1; y++) {
    float dy = y + 0.5f - cy, dy2 = dy * dy;
    if (dy2 >= ro * ro) continue;
    float xo = sqrtf(ro * ro - dy2);
    float xi = (ri > 0 && dy2 < ri * ri) ? sqrtf(ri * ri - dy2) : -1.0f;
    int xa = (int)floorf(cx - xo), xb = (int)ceilf(cx + xo);
    if (xa < 0) xa = 0;
    if (xb > w_) xb = w_;
    // The pixels whose centres are within xi of cx are whole.
    int ia = xi >= 0 ? (int)ceilf(cx - xi - 0.5f) : xb, ib = xi >= 0 ? (int)floorf(cx + xi - 0.5f) : xb - 1;
    if (ia < xa) ia = xa;
    if (ib > xb - 1) ib = xb - 1;
    for (int x = xa; x < xb; x++) {
      if (x == ia && ia <= ib) {
        int sa, sb;
        store_span(y, sa, sb);
        solid_run(y, ia, ib, c, sa, sb);
        x = ib;
        continue;
      }
      float dx = x + 0.5f - cx;
      float cov = (dx >= -xi && dx <= xi) ? 1.0f : clamp01(r - sqrtf(dx * dx + dy2) + 0.5f);
      if (cov > 0) blend(x, y, c, cov);
    }
    row_done();
  }
}

void Canvas::ring(float cx, float cy, float ro, float ri, Color c) {
  shade(cx - ro, cy - ro, cx + ro, cy + ro, c, [=](float px, float py) {
    float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
    return clamp01(ro - d + 0.5f) * clamp01(d - ri + 0.5f);
  });
}

void Canvas::line(float x0, float y0, float x1, float y1, float width, Color c) {
  float hw = width * 0.5f;
  float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
  float inv = len2 > 0 ? 1.0f / len2 : 0;
  shade(minf(x0, x1) - hw, minf(y0, y1) - hw, maxf(x0, x1) + hw, maxf(y0, y1) + hw, c, [=](float px, float py) {
    float t = ((px - x0) * vx + (py - y0) * vy) * inv;
    t = clamp01(t);
    float dx = px - (x0 + vx * t), dy = py - (y0 + vy * t);
    return clamp01(hw - sqrtf(dx * dx + dy * dy) + 0.5f);
  });
}

// Where row y is solid inside the clip: RRect::coverage is 1 exactly where its signed distance is
// -0.5 or less. Inside the corner's band that ends on the circle of radius r - 0.5, which gives
// each side's reach from the centre in closed form.
void Canvas::solid_span(int y, int &xa, int &xb) const {
  xa = 0;
  xb = w_ - 1;
  if (!has_clip_) return;
  const RRect &r = clip_;
  float cx = (r.x0 + r.x1) * 0.5f, cy = (r.y0 + r.y1) * 0.5f;
  float hx = (r.x1 - r.x0) * 0.5f, hy = (r.y1 - r.y0) * 0.5f, lim = minf(hx, hy);
  float dy = y + 0.5f - cy, ady = fabsf(dy);
  if (ady > hy - 0.5f) {
    xb = -1;
    return;
  }
  auto reach = [&](float rad) {
    if (rad > lim) rad = lim;
    if (rad < 0.5f) return hx - 0.5f;
    float qy = ady - hy + rad, k = (rad - 0.5f) * (rad - 0.5f) - (qy > 0 ? qy * qy : 0);
    return hx - rad + sqrtf(k > 0 ? k : 0);
  };
  float left = reach(dy < 0 ? r.tl : r.bl), right = reach(dy < 0 ? r.tr : r.br);
  // Pixel x is centred at x + 0.5. The 0.001 keeps a pixel right on the line in blend()'s hands.
  xa = (int)ceilf(cx - left - 0.5f + 0.001f);
  xb = (int)floorf(cx + right - 0.5f - 0.001f);
  if (xa < 0) xa = 0;
  if (xb > w_ - 1) xb = w_ - 1;
}

void Canvas::solid_run(int y, int a, int b, Color c, int sa, int sb) {
  int fa = a > sa ? a : sa, fb = b < sb ? b : sb;
  if (fa > fb) {
    for (int x = a; x <= b; x++) blend(x, y, c, 1);
    return;
  }
  for (int x = a; x < fa; x++) blend(x, y, c, 1);
  const uint16_t v = to565(c);
  const uint8_t v0 = v & 0xFF, v1 = v >> 8;
  uint8_t *line = at(0, y);
  if (alpha_) {
    for (uint8_t *p = line + fa * 3, *e = line + fb * 3; p <= e; p += 3) {
      p[0] = v0;
      p[1] = v1;
      p[2] = 255;
    }
  } else {
    for (uint8_t *p = line + fa * 2, *e = line + fb * 2; p <= e; p += 2) {
      p[0] = v0;
      p[1] = v1;
    }
  }
  for (int x = fb + 1; x <= b; x++) blend(x, y, c, 1);
}

// Only the marked pixels hold anything in row_ or run_; between two of them the coverage is the
// running sum, flat. So the row goes marked pixel by marked pixel, and the stretches between are
// filled whole: most of a shape is solid, and that is a plain store, inside the clip's solid part.
void Canvas::put_row(int y, int lo, int hi, Color c) {
  if (hi > w_ - 1) hi = w_ - 1;
  int sa, sb;
  store_span(y, sa, sb);
  auto stretch = [&](int a, int b, float cov) {  // pixels a..b, all at cov
    if (b < a || cov <= 0.0001f) return;
    if (cov >= 0.998f) {
      solid_run(y, a, b, c, sa, sb);
      return;
    }
    for (int x = a; x <= b; x++) blend(x, y, c, cov);
  };
  for (int i = 1; i < nev_; i++)  // insertion sort: a row seldom has more than a few dozen
    for (int j = i; j > 0 && ev_[j - 1] > ev_[j]; j--) {
      uint16_t t = ev_[j];
      ev_[j] = ev_[j - 1];
      ev_[j - 1] = t;
    }
  float run = 0;
  int x = lo;
  for (int i = 0; i < nev_; i++) {
    int e = ev_[i];
    mark_[e] = 0;
    if (e > hi) {  // only w_: where a run that reaches the right edge ends (row_ has no w_)
      run_[e] = 0;
      continue;
    }
    stretch(x, e - 1, run);
    run += run_[e];
    stretch(e, e, row_[e] + run);
    row_[e] = run_[e] = 0;
    x = e + 1;
  }
  stretch(x, hi, run);
  nev_ = 0;
}

// Scanline fill with 5 sub-rows per pixel row and exact horizontal span coverage.
void Canvas::fill_polygon(const Pt *pts, int n, Color c) {
  if (n < 3) return;
  float miny = pts[0].y, maxy = pts[0].y;
  for (int i = 1; i < n; i++) {
    miny = fminf(miny, pts[i].y);
    maxy = fmaxf(maxy, pts[i].y);
  }
  int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
  if (y0 < y_lo()) y0 = y_lo();
  if (y1 > y_hi()) y1 = y_hi();
  const int kSub = 5;
  float xs[64];
  for (int y = y0; y < y1; y++) {
    int lo = w_, hi = -1;
    for (int s = 0; s < kSub; s++) {
      float sy = y + (s + 0.5f) / kSub;
      int k = 0;
      for (int i = 0, j = n - 1; i < n && k < 64; j = i++) {
        const Pt &a = pts[i], &b = pts[j];
        if ((a.y > sy) != (b.y > sy)) xs[k++] = a.x + (sy - a.y) * (b.x - a.x) / (b.y - a.y);
      }
      for (int i = 1; i < k; i++)  // insertion sort, k is small
        for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
      for (int i = 0; i + 1 < k; i += 2) {
        float xa = maxf(xs[i], 0.0f), xb = minf(xs[i + 1], (float)w_);
        if (xb <= xa) continue;
        int ia = (int)xa, ib = (int)xb;
        if (ia < lo) lo = ia;
        if (ib > hi) hi = ib < w_ ? ib : w_ - 1;
        mark(ia);
        if (ia == ib) { row_[ia] += (xb - xa) / kSub; continue; }
        row_[ia] += (ia + 1 - xa) / kSub;
        run_[ia + 1] += 1.0f / kSub;  // pixels ia + 1 .. ib - 1, whole
        run_[ib] -= 1.0f / kSub;
        if (ib < w_) row_[ib] += (xb - ib) / kSub;
        mark(ia + 1);
        mark(ib);
      }
    }
    if (lo <= hi) put_row(y, lo, hi, c);
    row_done();
  }
}

// As fill_polygon, over all the outlines at once, with each crossing's direction counted.
void Canvas::fill_path(const Pt *pts, const int *lens, int n_outlines, Color c) {
  int total = 0;
  for (int i = 0; i < n_outlines; i++) total += lens[i];
  if (total < 3) return;
  float miny = pts[0].y, maxy = pts[0].y;
  for (int i = 1; i < total; i++) {
    miny = fminf(miny, pts[i].y);
    maxy = fmaxf(maxy, pts[i].y);
  }
  int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
  if (y0 < y_lo()) y0 = y_lo();
  if (y1 > y_hi()) y1 = y_hi();
  const int kSub = 5, kMax = 128, kSpans = 64;
  float xs[kMax];
  int8_t dir[kMax];
  // Each outline's rows: a tree line is many small outlines, and most rows meet few of them.
  float oy0[kSpans], oy1[kSpans];
  const Pt *ring = pts;
  for (int r = 0; r < n_outlines && r < kSpans; ring += lens[r], r++) {
    oy0[r] = oy1[r] = ring[0].y;
    for (int i = 1; i < lens[r]; i++) {
      oy0[r] = fminf(oy0[r], ring[i].y);
      oy1[r] = fmaxf(oy1[r], ring[i].y);
    }
  }
  for (int y = y0; y < y1; y++) {
    int lo = w_, hi = -1;
    for (int s = 0; s < kSub; s++) {
      float sy = y + (s + 0.5f) / kSub;
      int k = 0;
      ring = pts;
      for (int r = 0; r < n_outlines; ring += lens[r], r++) {
        if (r < kSpans && (sy < oy0[r] || sy > oy1[r])) continue;
        int n = lens[r];
        for (int i = 0, j = n - 1; i < n && k < kMax; j = i++) {
          const Pt &a = ring[j], &b = ring[i];  // the edge a -> b
          if ((a.y > sy) != (b.y > sy)) {
            xs[k] = a.x + (sy - a.y) * (b.x - a.x) / (b.y - a.y);
            dir[k++] = b.y > a.y ? 1 : -1;
          }
        }
      }
      for (int i = 1; i < k; i++)  // insertion sort, k is small
        for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
          float t = xs[j];
          xs[j] = xs[j - 1];
          xs[j - 1] = t;
          int8_t d = dir[j];
          dir[j] = dir[j - 1];
          dir[j - 1] = d;
        }
      int wind = 0;
      for (int i = 0; i + 1 < k; i++) {
        wind += dir[i];
        if (wind == 0) continue;  // outside between these two crossings
        float xa = maxf(xs[i], 0.0f), xb = minf(xs[i + 1], (float)w_);
        if (xb <= xa) continue;
        int ia = (int)xa, ib = (int)xb;
        if (ia < lo) lo = ia;
        if (ib > hi) hi = ib < w_ ? ib : w_ - 1;
        mark(ia);
        if (ia == ib) { row_[ia] += (xb - xa) / kSub; continue; }
        row_[ia] += (ia + 1 - xa) / kSub;
        run_[ia + 1] += 1.0f / kSub;  // pixels ia + 1 .. ib - 1, whole
        run_[ib] -= 1.0f / kSub;
        if (ib < w_) row_[ib] += (xb - ib) / kSub;
        mark(ia + 1);
        mark(ib);
      }
    }
    if (lo <= hi) put_row(y, lo, hi, c);
    row_done();
  }
}

void Canvas::multiply_alpha(float k) {
  if (!alpha_) return;
  int m = (int)(k * 256.0f + 0.5f);
  for (int y = y_lo(); y < y_hi(); y++) {
    uint8_t *p = at(0, y);
    for (int x = 0; x < w_; x++, p += 3) p[2] = (uint8_t)((p[2] * m + 128) >> 8);
  }
}

}  // namespace gfx
