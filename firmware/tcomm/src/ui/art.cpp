// The Arduino build uses -Os; the artwork is drawn pixel by pixel on every redraw.
#pragma GCC optimize("O2")
#include "art.h"
#include <math.h>
#include "board_art.h"
#include "theme.h"

namespace art {

using boardart::Paint;
using gfx::Canvas;
using gfx::Color;
using gfx::hex;
using gfx::RRect;

static constexpr float kPi = 3.14159265f;

// ---------------------------------------------------------------------------------------------
// Sky (spec section 5): bands, rings around the sun or moon, the disc and two hills. The faint
// rings the boards lay over everything, tiles and text included, are LVGL objects on top of the
// main screen (ui.cpp), so no tile has to be redrawn as the sun moves.

void sky(Canvas &c, tc::Phase ph, float p, bool moon) {
  const boardart::SkyPhase &s = boardart::kSky[(int)ph];
  c.clear(hex(s.bands[0]));
  c.fill_rect(0, 190, kScreenW, kScreenH, hex(s.bands[1]));
  c.fill_rect(0, 290, kScreenW, kScreenH, hex(s.bands[2]));
  c.fill_rect(0, 380, kScreenW, kScreenH, hex(s.bands[3]));
  if (moon)
    for (const auto &st : boardart::kSkyStars) c.fill_circle(st[0], st[1], st[2], hex(0xCDBFA0));
  float x = 20 + p * 760, y = 360 - 300 * sinf(kPi * p);
  for (int i = 0; i < 4; i++) c.fill_circle(x, y, s.ring_r[i], hex(s.ring_c[i]));
  for (int i = 0; i < boardart::kSkyHills.n; i++)
    boardart::draw_shape(c, boardart::kSkyHills.shapes[i], hex(s.hills[i]));
}

// ---------------------------------------------------------------------------------------------
// Date strip (spec section 4e): a disc on its arc, hills, a tower and pines in a region at the
// right end (118 px with the dog out, 299 with it in), or with a holiday message only the disc
// and a holly sprig, in the 49 px before the message. All of it at 90 percent.

// The fire tower on the strip (Mike, 2026-10-26): legs a pixel thinner than the board's and nearly
// upright, braced, with a catwalk and a band of windows round the cab, so it reads as a fire
// lookout. The board's own legs (shape kTowerLegs of kStripIn and kStripOut) are left out; cx is the
// tower's centre (the cab's), col the tower's colour, sky the strip's.
constexpr int kTowerLegs = 2;
static void fire_tower(Canvas &c, float cx, Color col, Color sky) {
  const float top = 15.5f, foot = 26.0f, w = 1.25f, half_top = 3.4f, half_foot = 4.2f;
  auto half = [&](float y) { return half_top + (half_foot - half_top) * (y - top) / (foot - top); };
  c.line(cx - half_top, top, cx - half_foot, foot, w, col);
  c.line(cx + half_top, top, cx + half_foot, foot, w, col);
  const float ys[3] = {top + 0.6f, (top + foot) / 2, foot - 0.6f};  // two X braces, thin
  for (int i = 0; i < 2; i++) {
    c.line(cx - half(ys[i]), ys[i], cx + half(ys[i + 1]), ys[i + 1], 0.7f, col);
    c.line(cx + half(ys[i]), ys[i], cx - half(ys[i + 1]), ys[i + 1], 0.7f, col);
  }
  c.line(cx - 5.5f, 15.3f, cx + 5.5f, 15.3f, 0.9f, col);  // the catwalk
  c.fill_rect(cx - 3.2f, 11.4f, cx + 3.2f, 13.0f, sky);    // the windows
}

void strip(Canvas &c, tc::Phase ph, float p, bool moon, bool dog_out, float holiday_x, tc::DayIcon icon) {
  Color base = theme::sky(ph).strip;
  float w = (float)c.width(), h = (float)c.height();
  c.clear(base, 0);
  RRect rr{0, 0, w, h, h / 2, h / 2, h / 2, h / 2};
  c.fill_rrect(rr, base);
  c.set_clip(&rr);
  bool holiday = holiday_x > 0;
  float W = holiday ? 49.0f : (float)(dog_out ? kStripSceneryOut : kStripSceneryIn);
  float left = holiday ? holiday_x - 55.3f : w - W;  // the board's region ends 6 px before the message
  if (holiday && icon != tc::DayIcon::Holly) {
    // Another day's icon in the holly's place, without the disc (they'd overlap).
    day_icon(c, icon, left + 4, 6, 44);
  } else if (moon) {
    // A crescent: the moon, then a disc of the strip's colour over it, 5 right and 3 up.
    float k = W / 299.0f;  // placed on the Night board's 299 px region; stretched to this one
    const float(*m)[4] = boardart::kCrescent;
    c.fill_circle(left + m[0][0] * k, m[0][1], m[0][2], hex(boardart::kCrescentColour));
    c.fill_circle(left + m[0][0] * k + (m[1][0] - m[0][0]), m[1][1], m[1][2], base);
    if (!holiday)
      for (const auto &st : boardart::kStripStars) c.fill_circle(left + st[0] * W, st[1], st[2], hex(boardart::kCrescentColour));
  } else {
    c.fill_circle(left + 14 + p * (W - 28), 42 - 34 * sinf(kPi * p), boardart::kStripDiscR,
                  hex(boardart::kStripDisc[(int)ph]));
  }
  Paint pt{};
  pt.strip = base;
  if (holiday) {
    if (icon == tc::DayIcon::Holly) boardart::draw(c, boardart::kStripHolly, pt, left, 0, &rr);
  }
  else {
    const boardart::Group &g = dog_out ? boardart::kStripOut : boardart::kStripIn;
    c.set_clip(&rr);
    for (int i = 0; i < g.n; i++)
      if (i != kTowerLegs) boardart::draw_shape(c, g.shapes[i], boardart::colour(g.shapes[i], pt), left, 0);
    fire_tower(c, left + (dog_out ? 85.0f : 215.0f), boardart::colour(g.shapes[kTowerLegs], pt), base);
  }
  c.set_clip(nullptr);
  c.multiply_alpha(0.9f);
}

// ---------------------------------------------------------------------------------------------
// Tiles

void floor_tile(Canvas &c, tc::Floor f, int st, tc::Mode m) {
  theme::StatusColors sc = theme::status_colors(st, m);
  RRect tile{0, 0, (float)kFloorW, (float)kFloorH, 60, 24, 60, 24};
  // The status zone (M0 0H362A24 24 0 0 1 386 24V133A60 60 0 0 1 326 193H0Z) inside the tile.
  RRect zone{0, 0, (float)kStatusW, (float)kFloorH, 60, 24, 60, 24};
  c.clear(sc.tint, 0);
  Paint pt{};
  pt.zone_w = sc.tint;
  pt.zone_s = sc.tile;
  pt.mode = (int)m;
  // BUSY is the one light tile (dark text): its scenery lightens where the others' darkens.
  pt.cls = m == tc::Mode::Night ? boardart::kNight : (st == (int)tc::Status::Busy ? boardart::kLight : boardart::kDark);
  boardart::draw(c, f == tc::Floor::Upstairs ? boardart::kUpTile : boardart::kDownTile, pt, 0, 0, &tile, &zone);
  c.multiply_alpha(0.9f);
}

// A little light green in the Outside tile's trees and their reflections, which the boards have in
// taupe (Mike, 2026-10-31). Found by the boards' own day colour, so a regenerated
// board_art_data.h keeps the change. Day, evening, night.
struct Recolour {
  uint32_t board, rgb[3];
};
static const Recolour kOutsideTrees[] = {
    {0xA89C84, {0x9CA886, 0x8B9675, 0x324540}},  // the trees
    {0xBBAE93, {0xB3BD9E, 0x9FA888, 0x35473F}},  // their reflections in the lake
};

void outside_tile(Canvas &c, tc::Mode m) {
  const theme::Palette &pal = theme::palette(m);
  RRect tile{0, 0, (float)kSideW, (float)kFloorH, 24, 60, 24, 60};
  c.clear(pal.outside, 0);
  c.fill_rrect(tile, pal.outside);
  Paint pt{};
  pt.mode = (int)m;
  const boardart::Group &g = boardart::kOutsideTile;
  c.set_clip(&tile);
  for (int i = 0; i < g.n; i++) {
    const boardart::Shape &s = g.shapes[i];
    Color col = boardart::colour(s, pt);
    for (const Recolour &r : kOutsideTrees)
      if (s.rgb[0] == r.board) col = hex(r.rgb[pt.mode < 0 || pt.mode > 2 ? 0 : pt.mode]);
    boardart::draw_shape(c, s, col);
  }
  c.set_clip(nullptr);
  c.multiply_alpha(0.9f);
}

// The doorbell's outline: radius 125 on top (a half circle over the 250 px width), 24 below. A
// rounded rectangle can't do this (its radius is capped at half the height), so it's two shapes,
// each clipped to its half.
static void arch(Canvas &c, float inset, Color col) {
  const float W = (float)kSideW, H = (float)kFloorH;
  RRect upper{0, 0, W, 125, 0, 0, 0, 0}, lower{0, 125, W, H, 0, 0, 0, 0};
  c.set_clip(&upper);
  c.fill_circle(125, 125, 125 - inset, col);
  c.set_clip(&lower);
  c.fill_rrect(RRect{inset, -100, W - inset, H - inset, 0, 0, 24 - inset, 24 - inset}, col);
  c.set_clip(nullptr);
}

void bell_tile(Canvas &c, tc::Mode m, bool pressed) {
  const theme::Palette &pal = theme::palette(m);
  c.clear(pal.doorbell, 0);
  if (pressed) {  // brief 10.1: gold outline on the doorbell when pressed
    arch(c, 0, pal.gold);
    arch(c, 5, pal.doorbell);
  } else {
    arch(c, 0, pal.doorbell);
  }
  c.multiply_alpha(0.8f);  // spec section 2: the doorbell's background at 80 percent
  // The ring, solid: a 92 px block at tile (79, 36.5), the ring's centre in it. The bell in it is
  // its own picture (bell).
  c.ring(125, 82.5f, 44, 36, pal.ring);
}

void bell(Canvas &c, Color col) {
  c.clear(col, 0);
  Paint pt{};
  pt.fg = col;
  boardart::draw(c, boardart::kBell, pt, 98 - kBellPicX, 55.5f - kBellPicY);
}

// ---------------------------------------------------------------------------------------------
// Icons and badges, from the boards at their design sizes.

void icon(Canvas &c, Icon i, Color fg) {
  float s = (float)c.width();
  c.clear(fg, 0);
  Paint pt{};
  pt.fg = fg;
  switch (i) {
    case Icon::PawIn: boardart::draw(c, boardart::kPawIn, pt); break;
    case Icon::PawOut: boardart::draw(c, boardart::kPawOut, pt); break;
    case Icon::Gear: boardart::draw(c, boardart::kGear, pt); break;
    case Icon::Caret: boardart::draw(c, boardart::kCaret, pt); break;
    case Icon::DropIn: boardart::draw(c, boardart::kDropIn, pt); break;
    case Icon::DropOut: boardart::draw(c, boardart::kDropOut, pt); break;
    case Icon::Close:  // Settings open: the gear turns into an X (not on the boards)
      c.line(s * 0.28f, s * 0.28f, s * 0.72f, s * 0.72f, s * 0.12f, fg);
      c.line(s * 0.72f, s * 0.28f, s * 0.28f, s * 0.72f, s * 0.12f, fg);
      break;
  }
}

void mail(Canvas &c, bool solid, Color fg, Color bg) {
  float s = (float)c.width(), m = s / 2;
  c.clear(fg, 0);
  if (solid) c.fill_circle(m, m, m, fg);
  else c.ring(m, m, m, m - s * 0.06f, fg);
  // The envelope, 16 x 12 on the 34 px badge, its flap meeting a little below the middle.
  Color ink = solid ? bg : fg;
  float x0 = s * 0.265f, x1 = s * 0.735f, y0 = s * 0.33f, y1 = s * 0.67f, yf = s * 0.53f, w = s * 0.055f;
  c.line(x0, y0, x1, y0, w, ink);
  c.line(x1, y0, x1, y1, w, ink);
  c.line(x1, y1, x0, y1, w, ink);
  c.line(x0, y1, x0, y0, w, ink);
  c.line(x0, y0, m, yf, w, ink);
  c.line(m, yf, x1, y0, w, ink);
}

void badge(Canvas &c, int st, Color fg, Color bg) {
  static const boardart::Group *const kBadges[6] = {&boardart::kBadgeOpen,     &boardart::kBadgeBusy,
                                                    &boardart::kBadgePrivate,  &boardart::kBadgeSleeping,
                                                    &boardart::kBadgeAway,     &boardart::kBadgeNoSignal};
  c.clear(fg, 0);
  Paint pt{};
  pt.fg = fg;
  pt.bg = bg;
  boardart::draw(c, *kBadges[st >= 0 && st < 6 ? st : 5], pt);
}

void forecast(Canvas &c, tc::Forecast f, bool night, Color ink) {
  using tc::Forecast;
  c.clear(ink, 0);
  Paint pt{};
  pt.fg = ink;
  const boardart::Group *g = nullptr;
  switch (f) {
    case Forecast::Sun: g = night ? &boardart::kFcMoon : &boardart::kFcSun; break;
    case Forecast::Moon: g = &boardart::kFcMoon; break;
    case Forecast::CloudSun: g = night ? &boardart::kFcCloudMoon : &boardart::kFcCloudSun; break;
    case Forecast::Clearing: g = night ? &boardart::kFcClearingMoon : &boardart::kFcClearing; break;
    case Forecast::Cloud: g = &boardart::kFcCloud; break;
    case Forecast::Rain: g = &boardart::kFcRain; break;
    case Forecast::HeavyRain: g = &boardart::kFcHeavyRain; break;
    case Forecast::Snow: g = &boardart::kFcSnow; break;
    case Forecast::Thunder: g = &boardart::kFcStorm; break;
    case Forecast::Unknown: break;
  }
  if (g) {
    boardart::draw(c, *g, pt);
    return;
  }
  const float u = kForecast / 48.0f;  // no forecast: a dash, in the icons' 48-unit drawing and stroke
  c.line(16 * u, 24 * u, 32 * u, 24 * u, 3.5f * u, ink);
}

void heat_sun(Canvas &c, bool rays, Color sun) {
  c.clear(sun, 0);
  Paint pt{};
  pt.fg = sun;
  boardart::draw(c, rays ? boardart::kFcHeatRays : boardart::kFcHeatDisc, pt);
}

// ---------------------------------------------------------------------------------------------
// Icicles: a thin ledge of ice along the top edge and icicles of varied length hanging from it,
// each a pale body in a deeper ice-blue rim (so it shows over the pale sky too) with a white
// streak down it; then the whole layer at 50 percent. The pattern is fixed (no randomness), as
// WeatherGate's is: it never shimmers.

// One side of an icicle at t (0 at the ledge, 1 at the tip): how far it is from the middle.
static float ice_half(float w, float t) { return 0.5f * w * powf(1 - t, 0.85f); }

static void ice_shape(Canvas &c, float x, float w, float len, float lean, float inset, float top, float upto, Color col) {
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

static void icicle(Canvas &c, float x, float w, float len, float lean, Color rim, Color body, Color shine) {
  ice_shape(c, x, w, len, lean, 0, 0, 1, rim);
  c.fill_circle(x + lean, len - 1.2f, 1.6f, rim);  // a rounded tip
  ice_shape(c, x, w, len, lean, 1.5f, 0, 0.92f, body);
  // The streak: left of the middle, two thirds of the way down.
  ice_shape(c, x - 0.2f * w, 0.28f * w, len * 0.7f, lean * 0.5f, 0, 3, 1, shine);
}

void icicles(Canvas &c) {
  const Color rim = hex(0x8CC0DC), body = hex(0xDDF1FA), shine = hex(0xFFFFFF);
  c.clear(body, 0);
  // The ledge: 6 px with a gently wavy lower edge, rimmed like the icicles.
  for (int pass = 0; pass < 2; pass++) {
    gfx::Pt band[2 + 81];
    int n = 0;
    band[n++] = gfx::Pt{0, 0};
    for (int i = 0; i <= 80; i++) {
      float x = kScreenW * i / 80.0f;
      band[n++] = gfx::Pt{x, (pass ? 4.5f : 6.0f) + 1.2f * sinf(x / 23.0f) + 0.8f * sinf(x / 9.0f)};
    }
    band[n++] = gfx::Pt{(float)kScreenW, 0};
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
  for (float x = 6; x < kScreenW - 6;) {
    float r = next(), w = 10 + 8 * next(), lean = (next() - 0.5f) * 3;
    float len = 16 + 34 * r * r;
    if (r > 0.8f) len = 52 + 26 * next(), w += 3;
    if (x > 150 && x < 380 && r > 0.55f) len = 78 + 14 * next(), w = 15 + 6 * next();
    if (len > kIciclesH - 2) len = kIciclesH - 2;
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

// ---------------------------------------------------------------------------------------------
// Calendar: the spec's 24-unit marker drawings and 18/16/22-unit icons, strokes with round ends.

void cal_marker(Canvas &c, bool up, float x, float y, float size, Color fill, Color ink, Color edge) {
  const float k = size / 24.0f, cx = x + 12 * k, cy = y + 12 * k;
  auto seg = [&](float x0, float y0, float x1, float y1, float w) {
    c.line(x + x0 * k, y + y0 * k, x + x1 * k, y + y1 * k, w * k, ink);
  };
  if (up) {
    c.fill_circle(cx, cy, 11 * k, fill);
    c.ring(cx, cy, 12 * k, 10 * k, ink);  // r 11, stroke 2
    seg(7, 14.5f, 12, 9.5f, 2.6f);
    seg(12, 9.5f, 17, 14.5f, 2.6f);
  } else {
    c.fill_circle(cx, cy, 11.25f * k, fill);
    c.ring(cx, cy, 12 * k, 10.5f * k, edge);   // r 11.25, stroke 1.5
    c.ring(cx, cy, 9.75f * k, 8.25f * k, ink);  // r 9, stroke 1.5
    seg(7.5f, 10, 12, 14.5f, 2.4f);
    seg(12, 14.5f, 16.5f, 10, 2.4f);
  }
}

void cal_kind_marker(Canvas &c, tc::EventKind kind, bool up, float x, float y, float size, Color fill, Color ink,
                     Color edge) {
  const float k = size / 24.0f, cx = x + 12 * k, cy = y + 12 * k;
  if (up) {
    c.fill_circle(cx, cy, 11 * k, fill);
    c.ring(cx, cy, 12 * k, 10 * k, ink);
  } else {
    c.fill_circle(cx, cy, 11.25f * k, fill);
    c.ring(cx, cy, 12 * k, 10.5f * k, edge);
    c.ring(cx, cy, 9.75f * k, 8.25f * k, ink);
  }
  auto P = [&](float u) { return u * k; };
  if (kind == tc::EventKind::Birthday) {
    // A cake on a plate, one candle.
    c.fill_rrect(RRect{x + P(7), y + P(11.5f), x + P(17), y + P(16.5f), P(1), P(1), 0, 0}, ink);
    c.line(x + P(5.5f), y + P(17.6f), x + P(18.5f), y + P(17.6f), P(1.6f), ink);
    c.line(x + P(12), y + P(7.8f), x + P(12), y + P(10.6f), P(1.7f), ink);
    c.fill_circle(x + P(12), y + P(5.6f), P(1.3f), ink);
  } else {
    // Two rings, linked.
    c.ring(x + P(9.6f), y + P(13), P(4.6f), P(3.1f), ink);
    c.ring(x + P(14.4f), y + P(13), P(4.6f), P(3.1f), ink);
    c.fill_circle(x + P(9.6f), y + P(7.3f), P(1.2f), ink);  // the stone
  }
}

void cal_add_button(Canvas &c, bool up, float s, Color fill, Color ink, Color edge, Color accent, Color plus) {
  c.clear(accent, 0);
  // The ring: a 1.5 px dashed circle in a (s + 5) box 4 px up and left of the marker's.
  const float rc = 7 - 4 + (s + 5) / 2, r = (s + 5) / 2 - 0.75f;
  const float circ = 2 * kPi * r;
  const int n = (int)(circ / 5.5f + 0.5f);  // dashes about 3 px, gaps about 2.5
  for (int i = 0; i < n; i++) {
    float a0 = 2 * kPi * i / n, a1 = a0 + (3.0f - 1.5f) / r;  // the round ends add the other 1.5 px
    c.line(rc + r * cosf(a0), rc + r * sinf(a0), rc + r * cosf(a1), rc + r * sinf(a1), 1.5f, accent);
  }
  cal_marker(c, up, 7, 7, s, fill, ink, edge);
  // The badge: 13 px, centred 0.5 px outside the marker's top right corner, with a plus.
  const float bx = 7 + s + 0.5f, by = 7 - 0.5f;
  c.fill_circle(bx, by, 6.5f, accent);
  c.line(bx, by - 3.5f, bx, by + 3.5f, 1.8f, plus);
  c.line(bx - 3.5f, by, bx + 3.5f, by, 1.8f, plus);
}

void cal_pill(Canvas &c, Color fill, Color border, float bw, float radius, bool rl, bool rr) {
  const float w = (float)c.width(), h = (float)c.height();
  c.clear(fill, 0);
  float l = rl ? radius : 0, r = rr ? radius : 0;
  c.fill_rrect(RRect{0, 0, w, h, l, r, r, l}, border);
  float il = rl ? radius - bw : 0, ir = rr ? radius - bw : 0;
  c.fill_rrect(RRect{bw, bw, w - bw, h - bw, il, ir, ir, il}, fill);
}

void cal_icon(Canvas &c, CalIcon i, Color col) {
  c.clear(col, 0);
  auto poly = [&](const float (*p)[2], int n, float w, bool closed) {
    for (int k = 1; k < n; k++) c.line(p[k - 1][0], p[k - 1][1], p[k][0], p[k][1], w, col);
    if (closed) c.line(p[n - 1][0], p[n - 1][1], p[0][0], p[0][1], w, col);
  };
  switch (i) {
    case CalIcon::Prev: { const float p[3][2] = {{11, 3}, {5, 9}, {11, 15}}; poly(p, 3, 3, false); break; }
    case CalIcon::Next: { const float p[3][2] = {{7, 3}, {13, 9}, {7, 15}}; poly(p, 3, 3, false); break; }
    case CalIcon::Up: { const float p[3][2] = {{3, 12}, {9, 6}, {15, 12}}; poly(p, 3, 3, false); break; }
    case CalIcon::Down: { const float p[3][2] = {{3, 6}, {9, 12}, {15, 6}}; poly(p, 3, 3, false); break; }
    case CalIcon::Trash: {
      const float lid[2][2] = {{2.5f, 4}, {13.5f, 4}}, handle[4][2] = {{6, 4}, {6, 2.5f}, {10, 2.5f}, {10, 4}};
      const float can[4][2] = {{4, 4}, {4.7f, 13.5f}, {11.3f, 13.5f}, {12, 4}};
      poly(lid, 2, 1.8f, false);
      poly(handle, 4, 1.8f, false);
      poly(can, 4, 1.8f, false);
      c.line(6.5f, 6.5f, 6.5f, 11.5f, 1.8f, col);
      c.line(9.5f, 6.5f, 9.5f, 11.5f, 1.8f, col);
      break;
    }
    case CalIcon::Shift: {
      const float p[7][2] = {{11, 3}, {3, 12}, {8, 12}, {8, 18}, {14, 18}, {14, 12}, {19, 12}};
      poly(p, 7, 2.2f, true);
      break;
    }
    case CalIcon::Backspace: {
      const float p[5][2] = {{9, 3}, {23, 3}, {23, 19}, {9, 19}, {2, 11}};
      poly(p, 5, 2.2f, true);
      c.line(12.5f, 7.5f, 19.5f, 14.5f, 2.2f, col);
      c.line(19.5f, 7.5f, 12.5f, 14.5f, 2.2f, col);
      break;
    }
    case CalIcon::Repeat: {
      // Two arcs round the middle, each with an arrowhead at its end.
      const float w = (float)c.width(), cx = w / 2, cy = (float)c.height() / 2, r = w * 0.33f, sw = w / 9;
      for (int half = 0; half < 2; half++) {
        float a0 = kPi * (half ? 0.10f : 1.10f), a1 = a0 + kPi * 0.72f;
        float px = cx + r * cosf(a0), py = cy + r * sinf(a0);
        for (int i = 1; i <= 8; i++) {
          float a = a0 + (a1 - a0) * i / 8, qx = cx + r * cosf(a), qy = cy + r * sinf(a);
          c.line(px, py, qx, qy, sw, col);
          px = qx;
          py = qy;
        }
        // The arrowhead, along the circle's direction at the end.
        float tx = -sinf(a1), ty = cosf(a1), nx = cosf(a1), ny = sinf(a1), L = w * 0.22f;
        c.line(px, py, px - tx * L + nx * L * 0.7f, py - ty * L + ny * L * 0.7f, sw, col);
        c.line(px, py, px - tx * L - nx * L * 0.7f, py - ty * L - ny * L * 0.7f, sw, col);
      }
      break;
    }
    case CalIcon::Minus:
    case CalIcon::Plus: {
      const float w = (float)c.width(), h = (float)c.height(), sw = w / 7;
      c.line(w * 0.2f, h / 2, w * 0.8f, h / 2, sw, col);
      if (i == CalIcon::Plus) c.line(w / 2, h * 0.2f, w / 2, h * 0.8f, sw, col);
      break;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// The date strip's day icons (Mike, 2026-10-25): drawn in a 44-unit box, their own colours.

void day_icon(Canvas &c, tc::DayIcon icon, float x, float y, float size) {
  const float k = size / 44.0f;
  auto X = [&](float u) { return x + u * k; };
  auto Y = [&](float u) { return y + u * k; };
  auto L = [&](float x0, float y0, float x1, float y1, float w, Color col) { c.line(X(x0), Y(y0), X(x1), Y(y1), w * k, col); };
  auto D = [&](float cx, float cy, float r, Color col) { c.fill_circle(X(cx), Y(cy), r * k, col); };
  auto POLY = [&](const float (*p)[2], int n, Color col) {
    gfx::Pt pts[24];
    for (int i = 0; i < n && i < 24; i++) pts[i] = gfx::Pt{X(p[i][0]), Y(p[i][1])};
    c.fill_polygon(pts, n < 24 ? n : 24, col);
  };
  const Color cream = hex(0xFBF1DC), red = hex(0xC8372D), blue = hex(0x2F4A8A), gold = hex(0xE8B23A);
  const Color green = hex(0x2F7A44), brown = hex(0x8A5A34), steel = hex(0xB9C2C9), pink = hex(0xE9A0B4);
  switch (icon) {
    case tc::DayIcon::Holly: {
      Paint pt{};
      boardart::draw(c, boardart::kStripHolly, pt, x - 4 * k, y - 6 * k);
      break;
    }
    case tc::DayIcon::Fireworks: {
      struct Burst {
        float cx, cy, r;
        Color col;
      } bursts[3] = {{16, 17, 12, gold}, {32, 13, 8, red}, {30, 32, 8, cream}};
      for (const Burst &b : bursts) {
        for (int i = 0; i < 10; i++) {
          float a = 2 * kPi * i / 10;
          L(b.cx + cosf(a) * b.r * 0.35f, b.cy + sinf(a) * b.r * 0.35f, b.cx + cosf(a) * b.r, b.cy + sinf(a) * b.r, 2.2f, b.col);
        }
        D(b.cx, b.cy, 1.8f, b.col);
      }
      break;
    }
    case tc::DayIcon::Flag: {
      L(7, 5, 7, 41, 2.6f, cream);  // the pole
      D(7, 4.5f, 2.2f, gold);
      // Thirteen stripes, red first, and the blue canton over the top seven.
      const float top = 8, sh = 22.0f / 13;
      c.fill_rect(X(8.5f), Y(top), X(39), Y(top + 13 * sh), cream);
      for (int i = 0; i < 13; i += 2) c.fill_rect(X(8.5f), Y(top + i * sh), X(39), Y(top + (i + 1) * sh), red);
      c.fill_rect(X(8.5f), Y(top), X(22), Y(top + 7 * sh), blue);
      for (int r = 0; r < 3; r++)
        for (int q = 0; q < 3; q++) D(11 + q * 4.3f, 10.6f + r * 3.6f, 0.9f, cream);
      break;
    }
    case tc::DayIcon::Dove: {
      const float body[][2] = {{4, 27}, {12, 23}, {20, 22}, {27, 17}, {31, 12}, {35, 11}, {37, 14}, {34, 18},
                               {32, 24}, {26, 29}, {17, 31}, {10, 31}};
      POLY(body, 12, cream);
      const float wing[][2] = {{14, 23}, {17, 11}, {23, 6}, {25, 14}, {24, 21}};
      POLY(wing, 5, hex(0xE6DCC6));
      const float beak[][2] = {{36.5f, 11.5f}, {41, 13.5f}, {36.5f, 14.5f}};
      POLY(beak, 3, gold);
      D(33.5f, 13, 0.9f, hex(0x2A2A2A));
      L(38, 16, 43, 23, 1.6f, green);  // the olive branch
      D(41, 18.5f, 1.6f, green);
      D(42.5f, 21.5f, 1.4f, green);
      break;
    }
    case tc::DayIcon::Tools: {
      L(10, 36, 27, 19, 3.2f, brown);  // the hammer's handle
      const float head[][2] = {{21, 11}, {27, 6}, {37, 16}, {31, 21}};
      POLY(head, 4, steel);
      L(34, 36, 17, 19, 3.2f, steel);  // the wrench
      c.ring(X(14), Y(15.5f), 6.2f * k, 3.4f * k, steel);
      break;
    }
    case tc::DayIcon::Ship: {
      const float hull[][2] = {{5, 28}, {39, 28}, {34, 36}, {10, 36}};
      POLY(hull, 4, brown);
      L(22, 6, 22, 28, 1.8f, cream);
      const float sail1[][2] = {{20.5f, 9}, {20.5f, 26}, {9, 26}};
      const float sail2[][2] = {{23.5f, 11}, {23.5f, 26}, {34, 26}};
      POLY(sail1, 3, cream);
      POLY(sail2, 3, cream);
      c.fill_rect(X(22.5f), Y(5), X(29), Y(9), red);  // a pennant
      for (int i = 0; i < 4; i++) L(4 + i * 10, 39.5f, 9 + i * 10, 39.5f, 2, hex(0x5B8FC9));
      break;
    }
    case tc::DayIcon::Leaf: {
      const float leaf[][2] = {{22, 4},  {25, 12}, {31, 8},  {30, 17}, {39, 15}, {35, 22}, {40, 26}, {31, 28},
                               {32, 34}, {24, 31}, {22, 38}, {20, 31}, {12, 34}, {13, 28}, {4, 26},  {9, 22},
                               {5, 15},  {14, 17}, {13, 8},  {19, 12}};
      POLY(leaf, 20, hex(0xD9692A));
      L(22, 30, 22, 42, 2, brown);  // the stem
      L(22, 30, 22, 10, 1.2f, hex(0xA9471B));
      L(22, 25, 31, 17, 1.0f, hex(0xA9471B));
      L(22, 25, 13, 17, 1.0f, hex(0xA9471B));
      break;
    }
    case tc::DayIcon::Cake: {
      c.fill_rrect(RRect{X(7), Y(24), X(37), Y(37), 2 * k, 2 * k, 1 * k, 1 * k}, pink);
      c.fill_rrect(RRect{X(11), Y(16), X(33), Y(24.5f), 2 * k, 2 * k, 0, 0}, cream);
      L(5, 38.5f, 39, 38.5f, 2.2f, cream);  // the plate
      for (int i = 0; i < 4; i++) D(9.5f + i * 8.3f, 25, 2.2f, cream);  // icing over the bottom tier
      for (int i = 0; i < 3; i++) {
        float cx = 15 + i * 7;
        L(cx, 10, cx, 15.5f, 2, i == 1 ? pink : hex(0x7FB7E0));
        D(cx, 7.5f, 1.8f, gold);
      }
      break;
    }
    case tc::DayIcon::Rings: {
      c.ring(X(17), Y(27), 9.5f * k, 6.8f * k, gold);
      c.ring(X(27), Y(27), 9.5f * k, 6.8f * k, hex(0xF2CF6E));
      const float stone[][2] = {{17, 12}, {20.5f, 15.5f}, {17, 19.5f}, {13.5f, 15.5f}};
      POLY(stone, 4, hex(0xBFE4F2));
      break;
    }
    default: break;
  }
}

}  // namespace art
