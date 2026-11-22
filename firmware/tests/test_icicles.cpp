// Host tests for the danger-cold icicles (memory audit 2026-11-21). The screen used to keep the
// layer as an 800 x 100 picture (240,000 bytes of PSRAM); now it draws it straight into each area it
// redraws, two rows at a time, from art::icicle_layout's list. These check that the strips give the
// very pixels the old picture had, in any columns, at any strip height, and time both ways.
// Build and run (Git Bash, from the firmware folder):
//   g++ -std=c++17 -O1 -Wall -Wextra -I tcomm/src tests/test_icicles.cpp
//       tcomm/src/ui/art.cpp tcomm/src/ui/board_art.cpp tcomm/src/gfx/canvas.cpp
//       tcomm/src/ui/theme.cpp tcomm/src/core/*.cpp -o "$LOCALAPPDATA/tcomm/host/test_icicles.exe"
//   (one command; split here for reading)
#include "gfx/canvas.h"
#include "ui/art.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using gfx::Canvas;
using gfx::Color;
using gfx::hex;
using gfx::Pt;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) g_pass++;                                                      \
    else {                                                                   \
      g_fail++;                                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                        \
  } while (0)

// ---- The drawing as it was until 2026-11-21 (art.cpp then), word for word: the reference ----
namespace old {
// Icicles: a thin ledge of ice along the top edge and icicles of varied length hanging from it,
// each a pale body in a deeper ice-blue rim (so it shows over the pale sky too) with a white
// streak down it; then the whole layer at 50 percent. The pattern is fixed (no randomness), as
// WeatherGate's is: it never shimmers.

// One side of an icicle at t (0 at the ledge, 1 at the tip): how far it is from the middle.
float ice_half(float w, float t) { return 0.5f * w * powf(1 - t, 0.85f); }

void ice_shape(Canvas &c, float x, float w, float len, float lean, float inset, float top, float upto, Color col) {
  const int kN = 16;
  gfx::Pt pts[2 * kN + 2];
  int n = 0;
  for (int side = -1; side <= 1; side += 2)
    for (int k = 0; k <= kN; k++) {
      int i = side < 0 ? k : kN - k;
      float t = upto * i / kN, half = ice_half(w, t) - inset, cx = x + lean * t * t;
      if (half < 0) half = 0;
      pts[n++] = gfx::Pt{cx + side * half, top + (len - top - inset) * t};
    }
  c.fill_polygon(pts, n, col);
}

void icicle(Canvas &c, float x, float w, float len, float lean, Color rim, Color body, Color shine) {
  ice_shape(c, x, w, len, lean, 0, 0, 1, rim);
  c.fill_circle(x + lean, len - 1.2f, 1.6f, rim);  // a rounded tip
  ice_shape(c, x, w, len, lean, 1.5f, 0, 0.92f, body);
  // The streak: left of the middle, two thirds of the way down.
  ice_shape(c, x - 0.2f * w, 0.28f * w, len * 0.7f, lean * 0.5f, 0, 3, 1, shine);
}

void old_icicles(Canvas &c) {
  const Color rim = hex(0x8CC0DC), body = hex(0xDDF1FA), shine = hex(0xFFFFFF);
  c.clear(body, 0);
  // The ledge: 6 px with a gently wavy lower edge, rimmed like the icicles.
  for (int pass = 0; pass < 2; pass++) {
    gfx::Pt band[2 + 81];
    int n = 0;
    band[n++] = gfx::Pt{0, 0};
    for (int i = 0; i <= 80; i++) {
      float x = art::kScreenW * i / 80.0f;
      band[n++] = gfx::Pt{x, (pass ? 4.5f : 6.0f) + 1.2f * sinf(x / 23.0f) + 0.8f * sinf(x / 9.0f)};
    }
    band[n++] = gfx::Pt{(float)art::kScreenW, 0};
    c.fill_polygon(band, n, pass ? body : rim);
  }
  // Icicles grow in clusters, so the rhythm along the edge is uneven: a main icicle 10 to 18 px
  // wide with up to two smaller ones close beside it, then a gap, mostly short, now and then
  // long. Main icicles mostly 16 to 50 px long, about one in five longer, and the longest (up to
  // 92 px, and broader) over the upstairs tile's status side, x 150 to 380, reaching its top.
  uint32_t h = 2166136261u;
  auto next = [&h]() {
    h = (h ^ 0x9E3779B9u) * 16777619u;
    h ^= h >> 13;
    return (h & 0xFFFF) / 65535.0f;
  };
  for (float x = 6; x < art::kScreenW - 6;) {
    float r = next(), w = 10 + 8 * next(), lean = (next() - 0.5f) * 3;
    float len = 16 + 34 * r * r;
    if (r > 0.8f) len = 52 + 26 * next(), w += 3;
    if (x > 150 && x < 380 && r > 0.55f) len = 78 + 14 * next(), w = 15 + 6 * next();
    if (len > art::kIciclesH - 2) len = art::kIciclesH - 2;
    // The small ones first, so the main one is in front.
    float pick = next();
    int small = pick < 0.35f ? 0 : (pick < 0.75f ? 1 : 2);
    float side = next() < 0.5f ? -1.0f : 1.0f;
    for (int k = 0; k < small; k++, side = -side) {
      float sw = 5 + 4 * next(), sl = 10 + (0.2f + 0.3f * next()) * len;
      icicle(c, x + side * (0.5f * w + 0.3f * sw + 2 * next()), sw, sl, lean * 0.5f, rim, body, shine);
    }
    icicle(c, x, w, len, lean, rim, body, shine);
    float gap = next();
    x += 0.5f * w + 10 + (gap < 0.7f ? 16 * next() : 28 + 26 * next());
  }
  c.multiply_alpha(0.5f);
}

}  // namespace old

constexpr int W = art::kScreenW, H = art::kIciclesH;

// A 3-byte pixel: RGB565 and alpha.
static uint32_t px(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }

// The layer drawn the screen's way: a strips-only canvas, rows rows at a time, columns xa..xb, with
// the opacity applied as the screen does (the same sum as multiply_alpha). Into out (W x H x 3).
static void by_strips(const art::Icicle *ice, int n, int rows, int xa, int xb, std::vector<uint8_t> &out) {
  std::vector<float> scratch(Canvas::scanline_bytes(W) / sizeof(float) + 1);
  std::vector<uint8_t> strip((size_t)W * rows * 3);
  Canvas c;
  c.strips_only(scratch.data(), W, H, true);
  for (int y = 0; y < H; y += rows) {
    int m = rows < H - y ? rows : H - y;
    c.begin_strip(strip.data(), y, m);
    art::icicles_cols(c, ice, n, xa, xb);
    c.end_strip();
    for (int r = 0; r < m; r++)
      for (int x = xa; x <= xb; x++) {
        const uint8_t *s = &strip[((size_t)r * W + x) * 3];
        uint8_t *d = &out[((size_t)(y + r) * W + x) * 3];
        d[0] = s[0];
        d[1] = s[1];
        d[2] = (uint8_t)((s[2] * art::kIciclesOpa + 128) >> 8);
      }
  }
}

// Pixels in columns xa..xb where a and b differ (two clear pixels match whatever their colour).
static int differ(const std::vector<uint8_t> &a, const uint8_t *b, int xa, int xb) {
  int bad = 0;
  for (int y = 0; y < H; y++)
    for (int x = xa; x <= xb; x++) {
      uint32_t u = px(&a[((size_t)y * W + x) * 3]), v = px(&b[((size_t)y * W + x) * 3]);
      if ((u >> 16) == 0 && (v >> 16) == 0) continue;
      if (u != v) bad++;
    }
  return bad;
}

int main() {
  Canvas ref;
  CHECK(ref.create(W, H, true));
  old::old_icicles(ref);
  const uint8_t *R = ref.data();

  // The layout: a short list, the same every time.
  art::Icicle ice[art::kIciclesMax];
  const int n = art::icicle_layout(ice, art::kIciclesMax);
  art::Icicle again[art::kIciclesMax];
  CHECK(n > 20 && n < art::kIciclesMax);
  CHECK(art::icicle_layout(again, art::kIciclesMax) == n && memcmp(ice, again, sizeof(art::Icicle) * n) == 0);
  memset(again, 0, sizeof(again));
  CHECK(art::icicle_layout(again, 5) == n && memcmp(ice, again, sizeof(art::Icicle) * 5) == 0);  // a short list stops
  CHECK(art::icicle_layout(nullptr, 0) == n);  // or just counts
  int covered = 0, longest = 0;
  for (int i = 0; i < W * H; i++) covered += R[i * 3 + 2] != 0;
  for (int i = 0; i < n; i++) longest = ice[i].len > longest ? (int)ice[i].len : longest;
  std::printf("layout: %d icicles (%u bytes), the longest %d px; %d of %d pixels show (%.1f%%)\n", n,
              (unsigned)(n * sizeof(art::Icicle)), longest, covered, W * H, 100.0 * covered / (W * H));
  CHECK(longest <= H - 2);

  // The whole layer at once, the new way: the same picture.
  {
    Canvas c;
    CHECK(c.create(W, H, true));
    art::icicles(c);
    CHECK(memcmp(c.data(), R, (size_t)W * H * 3) == 0);
  }

  // Strip by strip, any height, any columns: the same pixels in those columns.
  const int heights[] = {1, 2, 3, 5, 7, 16, 100};
  const int cols[][2] = {{0, 799}, {150, 380}, {37, 41}, {790, 799}, {0, 0}, {399, 400}, {612, 700}};
  int worst = 0;
  for (int h : heights)
    for (auto &cx : cols) {
      std::vector<uint8_t> out((size_t)W * H * 3, 0);
      by_strips(ice, n, h, cx[0], cx[1], out);
      int bad = differ(out, R, cx[0], cx[1]);
      if (bad > worst) worst = bad;
      if (bad) std::printf("  strips of %d rows, columns %d-%d: %d pixels differ\n", h, cx[0], cx[1], bad);
    }
  CHECK(worst == 0);
  // Random redraw areas, as LVGL hands them over, two rows at a time.
  uint32_t seed = 12345;
  auto rnd = [&seed](int m) {
    seed = seed * 1103515245u + 12345u;
    return (int)((seed >> 16) % (uint32_t)m);
  };
  int bad_rand = 0;
  for (int k = 0; k < 40; k++) {
    int a = rnd(W), b = a + rnd(W - a);
    std::vector<uint8_t> out((size_t)W * H * 3, 0);
    by_strips(ice, n, 2, a, b, out);
    bad_rand += differ(out, R, a, b);
  }
  CHECK(bad_rand == 0);

  // A strips-only canvas never touches a picture: end_strip copies nothing, get() reads black.
  {
    std::vector<float> scratch(Canvas::scanline_bytes(W) / sizeof(float) + 1);
    Canvas c;
    c.strips_only(scratch.data(), W, H, true);
    CHECK(!c.ok() && c.width() == W && c.height() == H);
    static uint8_t strip[W * 2 * 3];
    c.begin_strip(strip, 10, 2);
    CHECK(c.draw_y0() == 10 && c.draw_y1() == 12);
    art::icicles_cols(c, ice, n, 0, W - 1);
    c.end_strip();
    CHECK(c.draw_y0() == 0 && c.draw_y1() == H);
    uint8_t a = 1;
    Color k = c.get(5, 5, &a);
    CHECK(k.r == 0 && k.g == 0 && k.b == 0);
    c.destroy();  // leaves the scratch alone (the vector frees it)
  }

  // What the screen holds now (ui.cpp ice_make): the scanline scratch, the list and a two-row strip.
  const size_t held = ((Canvas::scanline_bytes(W) + 7) & ~(size_t)7) + n * sizeof(art::Icicle) + (size_t)W * 2 * 3;
  std::printf("memory: %u bytes held while the icicles show (was %u)\n", (unsigned)held, (unsigned)(W * H * 3));
  CHECK(held < 16 * 1024);

  // Time (this PC): the whole layer the old way, and redrawn in strips of two rows.
  {
    const int reps = 50;
    Canvas c;
    c.create(W, H, true);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; i++) old::old_icicles(c);
    auto t1 = std::chrono::steady_clock::now();
    std::vector<uint8_t> out((size_t)W * H * 3, 0);
    for (int i = 0; i < reps; i++) by_strips(ice, n, 2, 0, W - 1, out);
    auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; i++) by_strips(ice, n, 2, 150, 249, out);
    auto t3 = std::chrono::steady_clock::now();
    auto ms = [reps](std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count() / reps; };
    std::printf("time on this PC: the old picture drawn once %.3f ms; strips over the whole layer %.3f ms, over 100 columns %.3f ms\n",
                ms(t1 - t0), ms(t2 - t1), ms(t3 - t2));
  }

  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
