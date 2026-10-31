// A small anti-aliased rasterizer for the panel's flat artwork: per-corner rounded rectangles,
// circles, rings, polygons and thick lines. Flat colors only (design rule 1); coverage is used
// for edge smoothing, never for gradients or glow.
//
// Pixels are stored in LVGL 8's formats for LV_COLOR_DEPTH 16 without byte swap, so a canvas can
// be handed to an lv_img as is: 2 bytes per pixel (TRUE_COLOR) or 3 (TRUE_COLOR_ALPHA: RGB565 low
// byte, high byte, alpha). Plain C++, no LVGL: the same drawing runs on the panel and on the PC.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace gfx {

struct Color {
  uint8_t r, g, b;
};
inline Color hex(uint32_t v) { return Color{(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; }
// a moved toward b by t (0..1)
Color mix(Color a, Color b, float t);
// every channel multiplied by k ("darkened 14%" is scale(c, 0.86))
Color scale(Color c, float k);
uint16_t to565(Color c);

struct Pt {
  float x, y;
};

// Memory for canvases. The panel points this at PSRAM; the default is malloc/free.
struct Allocator {
  void *(*alloc)(size_t);
  void (*release)(void *);
};
void set_allocator(Allocator a);

// Called after each row of the larger fills. The panel uses it to keep the task watchdog fed, so a
// slow redraw makes the screen lag instead of rebooting it.
using RowHook = void (*)();
void set_row_hook(RowHook h);

// A rounded rectangle with its own radius per corner, used both to draw and to clip.
struct RRect {
  float x0, y0, x1, y1;
  float tl, tr, br, bl;
  float coverage(float px, float py) const;  // 0..1 for the pixel centred at (px, py)
};

class Canvas {
 public:
  Canvas() {}
  ~Canvas();
  Canvas(const Canvas &) = delete;
  Canvas &operator=(const Canvas &) = delete;

  bool create(int w, int h, bool alpha);
  void destroy();
  bool ok() const { return data_ != nullptr; }
  int width() const { return w_; }
  int height() const { return h_; }
  bool has_alpha() const { return alpha_; }
  uint8_t *data() const { return data_; }
  size_t bytes() const { return (size_t)w_ * h_ * (alpha_ ? 3 : 2); }
  size_t row_bytes() const { return (size_t)w_ * (alpha_ ? 3 : 2); }

  // Strip drawing (see draw_in_strips): until end_strip(), drawing touches rows y0 .. y0 + n - 1
  // only, held in buf (n * row_bytes()); end_strip() copies them into the canvas. Each strip is
  // drawn from scratch, so the drawing must paint every pixel (start with clear()).
  void begin_strip(uint8_t *buf, int y0, int n);
  void end_strip();

  void clear(Color c, uint8_t a = 255);
  // Optional clip: everything drawn is multiplied by this shape's coverage.
  void set_clip(const RRect *r);
  // Paint only where the canvas already has alpha (source-atop): decorations stay inside a tile.
  void set_atop(bool on) { atop_ = on; }
  // Flat strength for everything drawn next (1 = solid). Used for the sky's stepped rings: each
  // ring is one flat color over each band, never a gradient.
  void set_opacity(float o) { opacity_ = o; }

  void fill_rect(float x0, float y0, float x1, float y1, Color c);
  void fill_rrect(const RRect &r, Color c);
  void fill_circle(float cx, float cy, float r, Color c);
  void ring(float cx, float cy, float r_out, float r_in, Color c);
  void line(float x0, float y0, float x1, float y1, float width, Color c);  // round caps
  void fill_polygon(const Pt *pts, int n, Color c);                          // even-odd
  // Several outlines filled as one shape, nonzero winding (SVG's default): an outline running the
  // other way round cuts a hole. lens[i] points per outline, one after another in pts.
  void fill_path(const Pt *pts, const int *lens, int n_outlines, Color c);
  // Multiplies every pixel's alpha by k (0..1): a layer drawn solid, then shown at 90 percent.
  void multiply_alpha(float k);

  // Reads one pixel back (tests and screenshot tools).
  Color get(int x, int y, uint8_t *alpha = nullptr) const;

 private:
  void blend(int x, int y, Color c, float cov);
  template <class F> void shade(float x0, float y0, float x1, float y1, Color c, F coverage);
  // The pixels xa..xb of row y where the clip's coverage is 1 (the whole row without a clip).
  void solid_span(int y, int &xa, int &xb) const;
  // Where a fully covered pixel of row y can simply be stored (none while atop or translucent).
  void store_span(int y, int &xa, int &xb) const {
    xa = 0;
    xb = -1;
    if (!atop_ && opacity_ >= 1.0f) solid_span(y, xa, xb);
  }
  // Pixels a..b of row y, fully covered: stored inside sa..sb, through blend() outside it.
  void solid_run(int y, int a, int b, Color c, int sa, int sb);
  // Paints row y from the scanline coverage in row_ and run_ (lo..hi), and leaves both zeroed.
  void put_row(int y, int lo, int hi, Color c);
  // Notes a pixel where row_ or run_ was changed: between two such pixels the coverage is flat.
  void mark(int x) {
    if (!mark_[x]) {
      mark_[x] = 1;
      ev_[nev_++] = (uint16_t)x;
    }
  }
  // The rows drawing may touch, and where pixel (x, y) is stored: the strip while one is open.
  int y_lo() const { return strip_ ? sy0_ : 0; }
  int y_hi() const { return strip_ ? sy1_ : h_; }
  uint8_t *at(int x, int y) const {
    size_t bpp = alpha_ ? 3 : 2;
    return strip_ ? strip_ + ((size_t)(y - sy0_) * w_ + x) * bpp : data_ + ((size_t)y * w_ + x) * bpp;
  }

  uint8_t *data_ = nullptr;
  int w_ = 0, h_ = 0;
  bool alpha_ = false;
  bool atop_ = false;
  float opacity_ = 1.0f;
  bool has_clip_ = false;
  RRect clip_ = {};
  // Scanline coverage, all zero between rows: row_ holds the partly covered pixels at the ends of
  // each span, run_ (w + 1) the full ones between, as a difference array (+ where a run starts, -
  // where it ends), so a span costs the same however long it is.
  float *row_ = nullptr, *run_ = nullptr;
  uint16_t *ev_ = nullptr;   // the marked pixels of this row, in the order marked (w + 1)
  uint8_t *mark_ = nullptr;  // 1 where a pixel is in ev_ (w + 1)
  int nev_ = 0;
  uint8_t *strip_ = nullptr;
  int sy0_ = 0, sy1_ = 0;
};

// Runs draw(c) once per strip of rows through buf, so the pixel-by-pixel work happens in buf
// (on the panel, fast internal RAM) and the canvas (PSRAM) gets one write per row, instead of a
// read and a write per pixel per layer: that traffic starves the display's own reads from PSRAM.
// Without a buffer, or one too small for a row, it draws straight into the canvas.
template <class F>
void draw_in_strips(Canvas &c, uint8_t *buf, size_t buf_bytes, F draw) {
  size_t rb = c.row_bytes();
  int rows = buf && rb ? (int)(buf_bytes / rb) : 0;
  if (rows < 1) {
    draw(c);
    return;
  }
  for (int y = 0; y < c.height(); y += rows) {
    c.begin_strip(buf, y, rows < c.height() - y ? rows : c.height() - y);
    draw(c);
    c.end_strip();
  }
}

}  // namespace gfx
