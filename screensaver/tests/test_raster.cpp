// Tests for core/gfx: blend565 and the anti-aliased rasterizer.
// Plain executable, no framework. Returns non-zero if any check fails.
//
// Usage: test_raster [output.ppm]   (default: aa_sample.ppm in the current
// directory, which is build/ when run from there or via ctest)
#include "gfx/raster.h"
#include "gfx/surface.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ss;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                   \
  do {                                                     \
    ++g_checks;                                            \
    if (!(cond)) {                                         \
      ++g_failures;                                        \
      std::printf("  FAIL %s:%d: ", __FILE__, __LINE__);   \
      std::printf(__VA_ARGS__);                            \
      std::printf("\n");                                   \
    }                                                      \
  } while (0)

static const Pixel kBlack = 0x0000;
static const Pixel kWhite = 0xFFFF;

struct Frame {
  std::vector<Pixel> px;
  Surface s;
  Frame(int w = SS_WIDTH, int h = SS_HEIGHT, int origin_y = 0, Pixel fill = kBlack)
      : px((size_t)w * h, fill) {
    s = Surface{px.data(), w, h, w, origin_y};
  }
  Pixel at(int x, int y) const { return s.row(y)[x]; }
};

static Rect fullRect() { return Rect{0, 0, SS_WIDTH, SS_HEIGHT}; }

// Coverage of a white-on-black pixel, from the 6-bit green channel.
static double cov(Pixel p) { return ((p >> 5) & 0x3F) / 63.0; }
static int r5(Pixel p) { return (p >> 11) & 0x1F; }
static int g6(Pixel p) { return (p >> 5) & 0x3F; }
static int b5(Pixel p) { return p & 0x1F; }

static double totalCoverage(const Frame& f) {
  double sum = 0;
  for (Pixel p : f.px) sum += cov(p);
  return sum;
}

static double shoelace(const Vec2* p, int n) {
  double a = 0;
  for (int i = 0; i < n; ++i) {
    const Vec2& u = p[i];
    const Vec2& v = p[(i + 1) % n];
    a += (double)u.x * v.y - (double)v.x * u.y;
  }
  return std::fabs(a) * 0.5;
}

// Sutherland-Hodgman clip of a polygon against an axis-aligned box (double).
// Works for concave input as far as area is concerned.
struct DPt { double x, y; };
static std::vector<DPt> clipBox(std::vector<DPt> poly, double x0, double y0, double x1, double y1) {
  auto clipEdge = [](const std::vector<DPt>& in, int axis, double v, bool keepGreater) {
    std::vector<DPt> out;
    size_t n = in.size();
    for (size_t i = 0; i < n; ++i) {
      DPt a = in[i], b = in[(i + 1) % n];
      double ca = axis == 0 ? a.x : a.y, cb = axis == 0 ? b.x : b.y;
      bool ina = keepGreater ? ca >= v : ca <= v;
      bool inb = keepGreater ? cb >= v : cb <= v;
      if (ina) out.push_back(a);
      if (ina != inb) {
        double t = (v - ca) / (cb - ca);
        out.push_back(DPt{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
      }
    }
    return out;
  };
  poly = clipEdge(poly, 0, x0, true);
  if (!poly.empty()) poly = clipEdge(poly, 0, x1, false);
  if (!poly.empty()) poly = clipEdge(poly, 1, y0, true);
  if (!poly.empty()) poly = clipEdge(poly, 1, y1, false);
  return poly;
}
static double areaD(const std::vector<DPt>& p) {
  double a = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    const DPt& u = p[i];
    const DPt& v = p[(i + 1) % p.size()];
    a += u.x * v.y - v.x * u.y;
  }
  return std::fabs(a) * 0.5;
}
static std::vector<DPt> toD(const Vec2* p, int n) {
  std::vector<DPt> v;
  for (int i = 0; i < n; ++i) v.push_back(DPt{p[i].x, p[i].y});
  return v;
}

static bool writePPM(const char* path, const Frame& f) {
  FILE* fp = std::fopen(path, "wb");
  if (!fp) return false;
  std::fprintf(fp, "P6\n%d %d\n255\n", f.s.width, f.s.height);
  std::vector<unsigned char> row((size_t)f.s.width * 3);
  for (int y = 0; y < f.s.height; ++y) {
    for (int x = 0; x < f.s.width; ++x) {
      Pixel p = f.at(x, y);
      row[(size_t)x * 3 + 0] = red8(p);
      row[(size_t)x * 3 + 1] = green8(p);
      row[(size_t)x * 3 + 2] = blue8(p);
    }
    std::fwrite(row.data(), 1, row.size(), fp);
  }
  return std::fclose(fp) == 0;
}

// Small deterministic RNG for the tests (xorshift32).
static uint32_t g_rng = 0x12345678u;
static uint32_t rnd() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }

// ---------------------------------------------------------------------------

static void testBlend() {
  std::printf("[1] blend565\n");
  int bad0 = 0, bad255 = 0;
  for (int i = 0; i < 2000000; ++i) {
    Pixel d = (Pixel)rnd(), s = (Pixel)rnd();
    if (blend565(d, s, 0) != d) ++bad0;
    if (blend565(d, s, 255) != s) ++bad255;
  }
  CHECK(bad0 == 0, "alpha 0 did not return dst in %d cases", bad0);
  CHECK(bad255 == 0, "alpha 255 did not return src in %d cases", bad255);

  Pixel m = blend565(kBlack, kWhite, 128);
  std::printf("  50%% black->white: r5=%d g6=%d b5=%d (ideal 15.5 / 31.5 / 15.5)\n", r5(m), g6(m), b5(m));
  CHECK(std::fabs(r5(m) - 15.5) <= 1.0 && std::fabs(g6(m) - 31.5) <= 1.0 && std::fabs(b5(m) - 15.5) <= 1.0,
        "50%% blend not mid gray: %d %d %d", r5(m), g6(m), b5(m));
  Pixel m2 = blend565(kWhite, kBlack, 128);
  CHECK(std::fabs(r5(m2) - 15.5) <= 1.0 && std::fabs(g6(m2) - 31.5) <= 1.0, "50%% white->black not mid gray");

  // Every channel lands within 1 step of the exact interpolation, for all
  // alphas, and the result is monotonic in alpha.
  int worst = 0;
  bool mono = true;
  for (int t = 0; t < 2000; ++t) {
    Pixel d = (Pixel)rnd(), s = (Pixel)rnd();
    int prev[3] = {r5(d), g6(d), b5(d)};
    for (int a = 0; a <= 255; ++a) {
      Pixel p = blend565(d, s, (uint8_t)a);
      double f = a / 255.0;
      double er = std::fabs(r5(p) - (r5(d) + (r5(s) - r5(d)) * f));
      double eg = std::fabs(g6(p) - (g6(d) + (g6(s) - g6(d)) * f));
      double eb = std::fabs(b5(p) - (b5(d) + (b5(s) - b5(d)) * f));
      int e = (int)std::ceil(std::max(er, std::max(eg, eb)) - 1e-9);
      if (e > worst) worst = e;
      int cur[3] = {r5(p), g6(p), b5(p)};
      int dirs[3] = {r5(s) - r5(d), g6(s) - g6(d), b5(s) - b5(d)};
      for (int c = 0; c < 3; ++c) {
        if ((dirs[c] > 0 && cur[c] < prev[c]) || (dirs[c] < 0 && cur[c] > prev[c])) mono = false;
        prev[c] = cur[c];
      }
    }
  }
  CHECK(worst <= 1, "blend error up to %d steps", worst);
  CHECK(mono, "blend not monotonic in alpha");
  CHECK(lerp565(0x1234, 0xABCD, 0) == 0x1234 && lerp565(0x1234, 0xABCD, 255) == 0xABCD, "lerp565 endpoints");
}

static void testSquare() {
  std::printf("[2] half-pixel square (fillPolygon and fillRectF)\n");
  for (int variant = 0; variant < 3; ++variant) {
    Frame f;
    if (variant == 0) {
      Vec2 sq[4] = {{10.5f, 10.5f}, {20.5f, 10.5f}, {20.5f, 20.5f}, {10.5f, 20.5f}};
      fillPolygon(f.s, sq, 4, kWhite, fullRect());
    } else if (variant == 1) {
      Vec2 sq[4] = {{10.5f, 10.5f}, {10.5f, 20.5f}, {20.5f, 20.5f}, {20.5f, 10.5f}};   // other winding
      fillPolygon(f.s, sq, 4, kWhite, fullRect());
    } else {
      fillRectF(f.s, 10.5f, 10.5f, 20.5f, 20.5f, kWhite, fullRect());
    }
    int badInterior = 0, badEdge = 0, badCorner = 0, badOutside = 0;
    for (int y = 0; y < 40; ++y) {
      for (int x = 0; x < 40; ++x) {
        Pixel p = f.at(x, y);
        bool ex = (x == 10 || x == 20), ey = (y == 10 || y == 20);
        bool ix = (x > 10 && x < 20), iy = (y > 10 && y < 20);
        double want;
        if (ix && iy) { if (p != kWhite) ++badInterior; continue; }
        if (ex && ey) want = 0.25;
        else if ((ex && iy) || (ey && ix)) want = 0.5;
        else { if (p != kBlack) ++badOutside; continue; }
        bool ok = std::fabs(r5(p) - 31 * want) <= 2 && std::fabs(g6(p) - 63 * want) <= 2 &&
                  std::fabs(b5(p) - 31 * want) <= 2;
        if (!ok) (want == 0.25 ? badCorner : badEdge)++;
      }
    }
    const char* name = variant == 0 ? "polygon cw" : variant == 1 ? "polygon ccw" : "fillRectF";
    std::printf("  %-12s edge(15,10) g6=%d corner(10,10) g6=%d\n", name, g6(f.at(15, 10)), g6(f.at(10, 10)));
    CHECK(badInterior == 0, "%s: %d interior pixels not exactly white", name, badInterior);
    CHECK(badEdge == 0, "%s: %d edge pixels not ~50%%", name, badEdge);
    CHECK(badCorner == 0, "%s: %d corner pixels not ~25%%", name, badCorner);
    CHECK(badOutside == 0, "%s: %d pixels changed outside", name, badOutside);
    int changed = 0;
    for (int y = 0; y < SS_HEIGHT; ++y)
      for (int x = 0; x < SS_WIDTH; ++x)
        if ((x < 10 || x > 20 || y < 10 || y > 20) && f.at(x, y) != kBlack) ++changed;
    CHECK(changed == 0, "%s: %d pixels changed elsewhere in the frame", name, changed);
  }
}

static void testConservation() {
  std::printf("[3] coverage conservation\n");
  Vec2 tri[3] = {{100.3f, 50.7f}, {400.9f, 120.2f}, {180.4f, 300.6f}};
  Vec2 star[10];
  for (int i = 0; i < 10; ++i) {
    float a = 0.37f + (float)i * 6.2831853f / 10.0f;
    float r = (i & 1) ? 45.0f : 120.0f;
    star[i] = Vec2{500.27f + r * std::cos(a), 240.61f + r * std::sin(a)};
  }
  struct Case { const char* name; const Vec2* p; int n; } cases[] = {{"triangle", tri, 3}, {"10-pt concave star", star, 10}};
  for (const Case& c : cases) {
    Frame f;
    fillPolygon(f.s, c.p, c.n, kWhite, fullRect());
    double got = totalCoverage(f), want = shoelace(c.p, c.n);
    double err = std::fabs(got - want) / want;
    std::printf("  %-20s area %.2f, coverage %.2f, error %.4f%%\n", c.name, want, got, err * 100);
    CHECK(err <= 0.005, "%s coverage off by %.3f%%", c.name, err * 100);
  }
}

// Per-pixel exactness: compare every pixel with the analytic area of
// polygon ∩ pixel square, for random triangles and random star polygons at
// arbitrary angles and sub-pixel offsets.
static void testPerPixelExact() {
  std::printf("[3b] per-pixel coverage vs analytic area (random shapes)\n");
  double worst = 0;
  int shapes = 0;
  for (int t = 0; t < 120; ++t) {
    int n = (t % 2) ? 3 : 7;
    Vec2 p[8];
    float cx = frand(40, 120), cy = frand(40, 120);
    if (n == 3) {
      for (int i = 0; i < 3; ++i) p[i] = Vec2{cx + frand(-35, 35), cy + frand(-35, 35)};
    } else {
      float a0 = frand(0, 6.28f);
      for (int i = 0; i < n; ++i) {
        float a = a0 + (float)i * 6.2831853f / (float)n;
        float r = frand(5, 35);
        p[i] = Vec2{cx + r * std::cos(a), cy + r * std::sin(a)};
      }
    }
    Frame f(200, 200);
    fillPolygon(f.s, p, n, kWhite, Rect{0, 0, 200, 200});
    auto poly = toD(p, n);
    for (int y = 0; y < 200; ++y) {
      for (int x = 0; x < 200; ++x) {
        double want = areaD(clipBox(poly, x, y, x + 1, y + 1));
        double e = std::fabs(cov(f.at(x, y)) - want);
        if (e > worst) worst = e;
      }
    }
    ++shapes;
  }
  // Quantization budget: 8-bit alpha rounding plus 6-bit green rounding.
  std::printf("  %d shapes, worst per-pixel coverage error %.4f (budget 0.02)\n", shapes, worst);
  CHECK(worst <= 0.02, "per-pixel coverage error %.4f exceeds 0.02", worst);
}

static void testCircle() {
  std::printf("[4] fillCircle r=40\n");
  Frame f;
  const float r = 40.0f;
  Vec2 c{200.37f, 150.81f};
  fillCircle(f.s, c, r, kWhite, fullRect());
  double got = totalCoverage(f), want = M_PI * r * r;
  double err = std::fabs(got - want) / want;
  int outside = 0;
  double maxRadialErr = 0;
  for (int y = 0; y < SS_HEIGHT; ++y) {
    for (int x = 0; x < SS_WIDTH; ++x) {
      double d = std::hypot(x + 0.5 - c.x, y + 0.5 - c.y);
      if (d > r + 1 && f.at(x, y) != kBlack) ++outside;
    }
  }
  // Compare against the exact circle's per-pixel area (16x16 supersampled).
  for (int y = (int)(c.y - r - 2); y <= (int)(c.y + r + 2); ++y) {
    for (int x = (int)(c.x - r - 2); x <= (int)(c.x + r + 2); ++x) {
      int inside = 0;
      for (int sy = 0; sy < 16; ++sy)
        for (int sx = 0; sx < 16; ++sx)
          if (std::hypot(x + (sx + 0.5) / 16 - c.x, y + (sy + 0.5) / 16 - c.y) < r) ++inside;
      double e = std::fabs(cov(f.at(x, y)) - inside / 256.0);
      if (e > maxRadialErr) maxRadialErr = e;
    }
  }
  std::printf("  area %.2f, coverage %.2f, error %.4f%%, worst pixel vs true circle %.3f\n", want, got, err * 100,
              maxRadialErr);
  CHECK(err <= 0.01, "circle coverage off by %.3f%%", err * 100);
  CHECK(outside == 0, "%d pixels changed beyond r+1", outside);
  CHECK(maxRadialErr <= 0.05, "circle edge deviates from true circle by %.3f coverage", maxRadialErr);
}

static void testClipping() {
  std::printf("[5] clipping with huge coordinates\n");
  const Rect clip{100, 50, 300, 200};
  // (a) Shape covering the whole clip, vertices thousands of px away.
  Vec2 big[3] = {{-5000.0f, -3000.0f}, {6000.0f, -2000.0f}, {-1000.0f, 7000.0f}};
  // (b) Shape with a slanted edge crossing the clip; extends far left/up/down.
  Vec2 cut[4] = {{-5000.0f, -5000.0f}, {170.3f, -5000.0f}, {260.7f, 5000.0f}, {-5000.0f, 5000.0f}};
  // (c) Same idea, mirrored: extends far to the right.
  Vec2 cutR[4] = {{5000.0f, -5000.0f}, {150.2f, -4000.0f}, {230.9f, 5000.0f}, {5000.0f, 5000.0f}};
  struct Case { const char* name; const Vec2* p; int n; } cases[] = {
      {"covering triangle", big, 3}, {"left slab", cut, 4}, {"right slab", cutR, 4}};
  for (const Case& cs : cases) {
    Frame f;
    fillPolygon(f.s, cs.p, cs.n, kWhite, clip);
    int outside = 0;
    for (int y = 0; y < SS_HEIGHT; ++y)
      for (int x = 0; x < SS_WIDTH; ++x)
        if ((x < clip.x0 || x >= clip.x1 || y < clip.y0 || y >= clip.y1) && f.at(x, y) != kBlack) ++outside;
    // Pixels fully inside the polygon must be exactly white; fully outside, untouched.
    auto poly = toD(cs.p, cs.n);
    int badFull = 0, badEmpty = 0;
    double sum = 0;
    for (int y = clip.y0; y < clip.y1; ++y) {
      for (int x = clip.x0; x < clip.x1; ++x) {
        double a = areaD(clipBox(poly, x, y, x + 1, y + 1));
        if (a > 1 - 1e-6 && f.at(x, y) != kWhite) ++badFull;
        if (a < 1e-6 && f.at(x, y) != kBlack) ++badEmpty;
        sum += cov(f.at(x, y));
      }
    }
    double want = areaD(clipBox(poly, clip.x0, clip.y0, clip.x1, clip.y1));
    std::printf("  %-18s coverage in clip %.2f (exact %.2f)\n", cs.name, sum, want);
    CHECK(outside == 0, "%s: %d pixels written outside the clip", cs.name, outside);
    CHECK(badFull == 0, "%s: %d fully covered pixels not exactly white", cs.name, badFull);
    CHECK(badEmpty == 0, "%s: %d uncovered pixels changed", cs.name, badEmpty);
    CHECK(std::fabs(sum - want) <= 0.001 * want + 1, "%s: coverage in clip %.2f vs %.2f", cs.name, sum, want);
  }
  // Robustness: absurd and non-finite input must not crash or write outside.
  Frame f;
  Vec2 huge[3] = {{-1e9f, -1e9f}, {1e9f, 0.0f}, {0.0f, 1e9f}};
  fillPolygon(f.s, huge, 3, kWhite, clip);
  Vec2 nan3[3] = {{NAN, 0.0f}, {10.0f, 10.0f}, {0.0f, 10.0f}};
  fillPolygon(f.s, nan3, 3, kWhite, clip);
  Vec2 inf3[3] = {{INFINITY, 0.0f}, {10.0f, 10.0f}, {0.0f, 10.0f}};
  fillPolygon(f.s, inf3, 3, kWhite, clip);
  fillCircle(f.s, Vec2{150.0f, 100.0f}, 1e20f, kWhite, clip);
  strokeCapsule(f.s, Vec2{-1e8f, 0.0f}, Vec2{1e8f, 300.0f}, 5.0f, kWhite, clip);
  fillRectF(f.s, -1e30f, -1e30f, 1e30f, 1e30f, kWhite, clip);
  int outside = 0;
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x)
      if ((x < clip.x0 || x >= clip.x1 || y < clip.y0 || y >= clip.y1) && f.at(x, y) != kBlack) ++outside;
  CHECK(outside == 0, "robustness: %d pixels written outside the clip", outside);
  std::printf("  absurd/NaN/inf inputs: no crash, %d writes outside clip\n", outside);
}

// Randomized stress: random polygons (3..64 vertices, many far off-surface,
// self-intersecting) into random strip surfaces with random clips. Nothing may
// be written outside the clip, and the static accumulator must be left clean
// (checked by drawing a known square afterwards).
static void testStress() {
  std::printf("[5b] randomized clip stress\n");
  // Reference drawing of a known shape, compared bit-for-bit after the stress.
  Vec2 sq[4] = {{10.5f, 10.3f}, {20.7f, 10.5f}, {20.5f, 20.6f}, {10.2f, 20.5f}};
  Frame ref;
  fillPolygon(ref.s, sq, 4, kWhite, fullRect());
  int outside = 0;
  for (int t = 0; t < 3000; ++t) {
    int origin = (int)(rnd() % SS_STRIP_COUNT) * SS_STRIP_ROWS;
    Frame f(SS_WIDTH, SS_STRIP_ROWS, origin, kBlack);
    Rect clip{(int)(rnd() % 900) - 50, origin + (int)(rnd() % 40) - 4, 0, 0};
    clip.x1 = clip.x0 + (int)(rnd() % 900);
    clip.y1 = clip.y0 + (int)(rnd() % 40);
    int n = 3 + (int)(rnd() % (SS_MAX_POLY_POINTS - 2));
    Vec2 p[SS_MAX_POLY_POINTS];
    float span = (t % 3 == 0) ? 20000.0f : 1200.0f;
    for (int i = 0; i < n; ++i) p[i] = Vec2{frand(-span / 2, span / 2 + 800), frand(-span / 2, span / 2 + 480)};
    if (t % 4 == 0) fillPolygonAlpha(f.s, p, n, kWhite, (uint8_t)(rnd() & 255), clip);
    else fillPolygon(f.s, p, n, kWhite, clip);
    strokeCapsule(f.s, p[0], p[1], frand(0.5f, 30.0f), kWhite, clip);
    fillCircle(f.s, p[2], frand(0.1f, 300.0f), kWhite, clip);
    fillRectF(f.s, p[0].x, p[0].y, p[1].x, p[1].y, kWhite, clip);
    Rect c = clip.clipped(f.s.bounds());
    for (int y = origin; y < origin + SS_STRIP_ROWS; ++y)
      for (int x = 0; x < SS_WIDTH; ++x)
        if ((c.empty() || x < c.x0 || x >= c.x1 || y < c.y0 || y >= c.y1) && f.at(x, y) != kBlack) ++outside;
  }
  CHECK(outside == 0, "stress: %d pixels written outside the clip", outside);
  Frame after;
  fillPolygon(after.s, sq, 4, kWhite, fullRect());
  int diff = 0;
  for (size_t i = 0; i < ref.px.size(); ++i) diff += ref.px[i] != after.px[i];
  CHECK(diff == 0, "scratch state leaked: reference shape differs in %d pixels after stress", diff);
  std::printf("  3000 rounds, %d writes outside clip, %d pixels differ in reference shape after stress\n", outside,
              diff);
}

static void drawScene(Surface& s, const Rect& clip) {
  Vec2 tri[3] = {{50.3f, 20.2f}, {300.7f, 61.9f}, {120.1f, 250.4f}};
  fillPolygon(s, tri, 3, rgb565_hex(0xE07A3F), clip);
  Vec2 star[10];
  for (int i = 0; i < 10; ++i) {
    float a = 0.2f + (float)i * 6.2831853f / 10.0f;
    float r = (i & 1) ? 60.0f : 170.0f;
    star[i] = Vec2{420.4f + r * std::cos(a), 230.6f + r * std::sin(a)};
  }
  fillPolygon(s, star, 10, rgb565_hex(0x3F8FE0), clip);
  fillPolygonAlpha(s, tri, 3, rgb565_hex(0xFFFFFF), 90, clip);
  fillCircle(s, Vec2{640.25f, 95.75f}, 70.0f, rgb565_hex(0xF2D16B), clip);
  fillEllipse(s, Vec2{200.5f, 400.3f}, 150.0f, 40.0f, rgb565_hex(0x6BA368), clip);
  strokeCapsule(s, Vec2{-30.0f, 470.0f}, Vec2{830.0f, 300.0f}, 9.0f, rgb565_hex(0x202020), clip);
  strokeCapsule(s, Vec2{700.0f, 200.0f}, Vec2{760.0f, 460.0f}, 3.0f, rgb565_hex(0xFF0000), clip);
  fillRectF(s, 520.3f, 330.6f, 780.8f, 470.1f, rgb565_hex(0x884488), clip);
  fillRect(s, Rect{10, 300, 40, 340}, rgb565_hex(0x00FF00), clip);
  Vec2 off[4] = {{-5000.0f, 100.0f}, {60.0f, 90.0f}, {80.0f, 130.0f}, {-5000.0f, 5000.0f}};
  fillPolygon(s, off, 4, rgb565_hex(0x90A0B0), clip);
}

static void testStrips() {
  std::printf("[6] strip surfaces vs full frame\n");
  Frame full(SS_WIDTH, SS_HEIGHT, 0, rgb565_hex(0x101830));
  drawScene(full.s, fullRect());
  Frame assembled(SS_WIDTH, SS_HEIGHT, 0, 0);
  for (int k = 0; k < SS_STRIP_COUNT; ++k) {
    Frame strip(SS_WIDTH, SS_STRIP_ROWS, k * SS_STRIP_ROWS, rgb565_hex(0x101830));
    drawScene(strip.s, strip.s.bounds());
    // Strip surfaces only ever touch their own rows (buffer is exactly strip-sized).
    copyRect(assembled.s, strip.s, strip.s.bounds());
  }
  int diff = 0;
  for (size_t i = 0; i < full.px.size(); ++i)
    if (full.px[i] != assembled.px[i]) ++diff;
  std::printf("  %d differing pixels across %d strips\n", diff, SS_STRIP_COUNT);
  CHECK(diff == 0, "strip rendering differs from full frame in %d pixels", diff);
}

static void testCapsule() {
  std::printf("[7] strokeCapsule\n");
  Frame f;
  strokeCapsule(f.s, Vec2{100, 100}, Vec2{300, 250}, 6.0f, kWhite, fullRect());
  double len = std::hypot(200.0, 150.0), w = 6.0;
  double want = len * w + M_PI * (w / 2) * (w / 2);
  double got = totalCoverage(f);
  double err = std::fabs(got - want) / want;
  std::printf("  expected %.2f, coverage %.2f, error %.3f%%\n", want, got, err * 100);
  CHECK(err <= 0.02, "capsule coverage off by %.3f%%", err * 100);
}

static void testSpansAndCopy() {
  std::printf("[6b] spans, fillRect, copyRect clipping\n");
  Frame f;
  fillSpan(f.s, -1, 0, 800, kWhite);
  fillSpan(f.s, 480, 0, 800, kWhite);
  fillSpan(f.s, 5, -100, 900, kWhite);
  blendSpan(f.s, 6, -100, 900, kWhite, 128);
  fillRect(f.s, Rect{-50, 470, 900, 600}, kWhite, fullRect());
  int row5 = 0, row6 = 0, bottom = 0, other = 0;
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x) {
      Pixel p = f.at(x, y);
      if (y == 5) row5 += p == kWhite;
      else if (y == 6) row6 += p == blend565(kBlack, kWhite, 128);
      else if (y >= 470) bottom += p == kWhite;
      else other += p != kBlack;
    }
  CHECK(row5 == 800 && row6 == 800 && bottom == 8000 && other == 0, "span/rect clipping wrong (%d %d %d %d)", row5,
        row6, bottom, other);
  Frame strip(SS_WIDTH, 32, 64, kBlack);
  copyRect(strip.s, f.s, Rect{0, 0, 800, 480});   // only rows 64..95 overlap
  CHECK(strip.at(0, 64) == f.at(0, 64), "copyRect strip");
}

static void testPerformance() {
  std::printf("[8] performance (informational)\n");
  Frame f(SS_WIDTH, SS_HEIGHT, 0, rgb565_hex(0x203040));
  const int NP = 500, NC = 2000;
  std::vector<Vec2> polys((size_t)NP * 8);
  for (int i = 0; i < NP; ++i) {
    float cx = frand(0, SS_WIDTH), cy = frand(0, SS_HEIGHT), a0 = frand(0, 6.28f);
    for (int k = 0; k < 8; ++k) {
      float a = a0 + (float)k * 6.2831853f / 8.0f + frand(-0.2f, 0.2f);
      float r = frand(45, 85);   // ~150 px across
      polys[(size_t)i * 8 + k] = Vec2{cx + r * std::cos(a), cy + r * std::sin(a)};
    }
  }
  std::vector<Vec2> caps((size_t)NC * 2);
  std::vector<float> widths(NC);
  for (int i = 0; i < NC; ++i) {
    Vec2 a{frand(0, SS_WIDTH), frand(0, SS_HEIGHT)};
    float ang = frand(0, 6.28f), len = frand(20, 120);
    caps[(size_t)i * 2] = a;
    caps[(size_t)i * 2 + 1] = Vec2{a.x + len * std::cos(ang), a.y + len * std::sin(ang)};
    widths[i] = frand(2, 10);
  }
  using clk = std::chrono::steady_clock;
  auto t0 = clk::now();
  for (int i = 0; i < NP; ++i) fillPolygon(f.s, &polys[(size_t)i * 8], 8, (Pixel)rnd(), fullRect());
  auto t1 = clk::now();
  for (int i = 0; i < NC; ++i)
    strokeCapsule(f.s, caps[(size_t)i * 2], caps[(size_t)i * 2 + 1], widths[i], (Pixel)rnd(), fullRect());
  auto t2 = clk::now();
  for (int i = 0; i < NP; ++i) {
    Vec2 c = polys[(size_t)i * 8];
    fillCircle(f.s, c, 60.0f, (Pixel)rnd(), fullRect());
  }
  auto t3 = clk::now();
  double up = std::chrono::duration<double, std::micro>(t1 - t0).count() / NP;
  double uc = std::chrono::duration<double, std::micro>(t2 - t1).count() / NC;
  double ui = std::chrono::duration<double, std::micro>(t3 - t2).count() / NP;
  std::printf("  8-vertex polygon ~150px: %.2f us/shape (%d shapes)\n", up, NP);
  std::printf("  capsule len 20-120 w 2-10: %.2f us/shape (%d shapes)\n", uc, NC);
  std::printf("  circle r=60:              %.2f us/shape (%d shapes)\n", ui, NP);
  std::printf("  (host PC timing; ESP32-S3 timing unverified)\n");
}

static void writeSample(const char* path) {
  Frame f;
  // Two-tone background split diagonally-ish: left dark, right light.
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x) f.s.row(y)[x] = x < 400 ? rgb565_hex(0x1B2A41) : rgb565_hex(0xE8DCC2);
  Pixel ink = rgb565_hex(0xD9643A);
  // Rotated triangle straddling the split.
  {
    Vec2 c{400.3f, 120.6f};
    Vec2 t[3];
    for (int i = 0; i < 3; ++i) {
      float a = 0.31f + (float)i * 2.0943951f;
      t[i] = Vec2{c.x + 95 * std::cos(a), c.y + 95 * std::sin(a)};
    }
    fillPolygon(f.s, t, 3, ink, fullRect());
  }
  fillCircle(f.s, Vec2{150.4f, 330.7f}, 70.0f, rgb565_hex(0xF2C14E), fullRect());
  fillCircle(f.s, Vec2{280.6f, 420.2f}, 4.3f, rgb565_hex(0xF2C14E), fullRect());
  strokeCapsule(f.s, Vec2{230.0f, 250.0f}, Vec2{620.0f, 330.0f}, 7.0f, rgb565_hex(0x3D6B4F), fullRect());
  strokeCapsule(f.s, Vec2{300.0f, 380.0f}, Vec2{520.0f, 470.0f}, 1.5f, rgb565_hex(0xA03030), fullRect());
  Vec2 sliver[3] = {{40.2f, 40.3f}, {760.7f, 52.9f}, {40.2f, 43.1f}};
  fillPolygon(f.s, sliver, 3, rgb565_hex(0x6FA3D8), fullRect());
  fillRectF(f.s, 560.3f, 360.6f, 740.8f, 450.2f, rgb565_hex(0x5B3F7A), fullRect());
  fillRectF(f.s, 380.25f, 360.75f, 420.5f, 361.25f, rgb565_hex(0x000000), fullRect());   // half-px tall
  fillEllipse(f.s, Vec2{640.5f, 160.2f}, 110.0f, 35.0f, rgb565_hex(0x2E5C8A), fullRect());
  if (writePPM(path, f)) std::printf("  wrote %s\n", path);
  else {
    std::printf("  could not write %s\n", path);
    ++g_failures;
  }
}

int main(int argc, char** argv) {
  testBlend();
  testSquare();
  testConservation();
  testPerPixelExact();
  testCircle();
  testClipping();
  testStress();
  testStrips();
  testSpansAndCopy();
  testCapsule();
  testPerformance();
  std::printf("[ppm] sample image\n");
  writeSample(argc > 1 ? argv[1] : "aa_sample.ppm");
  std::printf("%d checks, %d failures: %s\n", g_checks, g_failures, g_failures ? "FAIL" : "PASS");
  return g_failures ? 1 : 0;
}
