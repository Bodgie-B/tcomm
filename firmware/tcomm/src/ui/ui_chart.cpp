// The tenant's comfort chart (Mike, 2026-11-06; docs/temperature-requests.md, "The comfort chart"). A
// long press on the downstairs INSIDE area opens it on either panel: every answer the survey ever
// kept, as a small dot in its answer's colour on a psychrometric chart, with the two standard
// comfort zones and a ring for the reading now. Chips pick a part of the day. On the downstairs
// panel a tap near a dot opens a card that can remove the answer (a mistake); upstairs the same
// card only shows it.
//
// The zoom (Mike, 2026-11-18): the chart opens whole, as it always did, holds for a second, then
// zooms in smoothly to the dots: an area half as big again as they take up (comfort_chart.h has the
// rule and the pace). The grid lines and their labels follow, with finer steps as the view closes
// in. Every open starts over. Choosing a chip or removing a dot glides to the new set of dots.
// Everything on the plot is placed through the view being shown (s.view); the whole chart is just
// the widest view.
//
// Memory: the file is read when the chart opens, parsed into a block of points (PSRAM on the
// panel), and the file's own buffer is freed at once. The points go when the sheet closes, however
// it closes (a tick notices). No picture of the plot is kept: it's drawn straight from the points
// by the card's draw event, and only when something changes (a chip, a pick, a removal, the
// reading, the view), and only the part of the card that changed.
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include "../core/comfort_chart.h"
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;
using namespace tc;

namespace {

// The plot (the approved mockups'): 55-85 F across, 0-120 gr/lb up, when the whole chart shows.
constexpr int kPL = 64, kPR = 712, kPT = 84, kPB = 404;
constexpr int kCardX = 8, kCardY = 8, kCardW = 784, kCardH = 464;
constexpr int kPickR = 24;      // a tap picks the nearest dot within this many pixels
constexpr int kChips = 5;       // ALL, NIGHT, MORNING, MIDDAY, EVENING
// What's drawn on the plot may spill this far past its edges and no further: a dot's radius, half
// a curve's width. Zones, grid lines and curves are cut at the edge itself.
constexpr int kPad = 4;
constexpr int kAnimMs = 16;     // how often the zoom looks at the clock; the panel draws as fast as it can
constexpr int kRhPts = 64;      // points kept for one humidity curve (61 samples, an entry and an exit)
constexpr int kRhMax = 16;      // curves kept; a view shows 2 to 8
const char kDeg[] = "\xC2\xB0";  // the degree sign

enum Phase : uint8_t {
  kRest,  // nothing moving
  kHold,  // just opened: the whole chart, for kChartHoldMs
  kMove,  // gliding from s.from to s.to
};

// One relative-humidity curve as it crosses the view shown, worked out once for each view. The
// panel draws the plot in bands of 48 rows or so and calls the draw event for each band; without
// this every band would work the curves out again (about 300 exponentials a time).
struct RhLine {
  uint8_t rh;        // percent
  uint8_t n;         // points
  uint8_t label;     // 0 none; 1 its right end at (lx, ly); 2 its middle there
  int16_t lx, ly;
  int16_t pt[2 * kRhPts];
};

struct State {
  ComfortPoint *pts = nullptr;
  float *gr = nullptr;    // each point's gr/lb, worked out once at load; below 0 when it isn't on the chart
  int16_t *xy = nullptr;  // each point's screen x, y in the view shown (x = kOff when it isn't on the chart)
  bool err = false;       // the points could not be stored
  int n = 0;
  int chip = 0;      // 0 = ALL, else a DayPart + 1
  int pick = -1;     // index of the picked point
  lv_obj_t *card = nullptr, *callout = nullptr, *chip_box[kChips] = {}, *chip_lab[kChips] = {};
  lv_timer_t *timer = nullptr;
  Mode built = Mode::Day;  // the mode the objects were made for
  bool now_ok = false;
  int now_t = 0, now_h = 0;  // whole degrees F and percent
  // The zoom (Mike, 2026-11-18).
  ChartView view = kChartFull;                    // what the plot shows now
  ChartView from = kChartFull, to = kChartFull;   // the glide's ends
  Phase phase = kRest;
  bool seen = false;        // the card has been drawn since it opened: the hold counts from then
  bool hold_run = false;    // the hold's clock is running
  uint32_t t0 = 0;          // lv tick when the hold or the glide began
  lv_timer_t *anim = nullptr;
  // Labelled steps: the set shown before the glide (a) gives way to the set for its end (b).
  int t_a = 5, t_b = 5, g_a = 20, g_b = 20, rh_a = 20, rh_b = 20;
  float fade = 0;    // how far the glide has gone, 0..1: all a at 0, all b at 1 (step_strength)
  float motion = 0;  // 0 at rest, up to 1 in the middle of a glide: softens labels that would pop
  lv_area_t now_box = {0, 0, -1, -1};  // where the "now" marker was last drawn
  RhLine *rh = nullptr;  // the curves for the view shown (4 KB, PSRAM on the panel); gone when the chart closes
  int rh_n = 0;
  bool rh_ok = false;    // they match the view and the steps
};
State s;

struct FeelLook {
  const char *word;
  uint32_t face;
  bool dark;
};
// The same faces as the survey's buttons (Mike, 2026-11-06): they don't change at night.
const FeelLook kLook[kFeelCount] = {{"Much too hot", 0x8B1E1E, true}, {"Too warm", 0xF4A7B9, false},
                                    {"Just right", 0x4D4D4D, true},   {"Too cool", 0xA8D0F0, false},
                                    {"Much too cold", 0x1E3F8C, true}};

const char *const kChipWord[kChips] = {"ALL", "NIGHT", "MORNING", "MIDDAY", "EVENING"};
const char *const kMonth[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *const kDay[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Colours for the plot, per mode. Night: dark sheet, so the grid and zones are light lines at low
// strength; the dots keep their colours.
struct Pal {
  Color sheet, ink, grid, axis, rh, rh_text, zone, zone2, zone_line, zone_text, cream, gold, ring;
};
Pal pal() {
  Pal p;
  p.sheet = theme::sheet_bg(g_mode);
  p.ink = ink();
  p.gold = hex(0xF5A623);
  p.ring = hex(0x2B1E24);
  if (g_mode == Mode::Night) {
    p.grid = gfx::mix(p.sheet, p.ink, 0.13f);
    p.axis = gfx::mix(p.sheet, p.ink, 0.62f);
    p.rh = gfx::mix(p.sheet, p.ink, 0.28f);
    p.rh_text = gfx::mix(p.sheet, p.ink, 0.5f);
    p.zone = gfx::mix(p.sheet, p.ink, 0.17f);
    p.zone2 = gfx::mix(p.zone, p.ink, 0.17f);
    p.zone_line = gfx::mix(p.sheet, p.ink, 0.42f);
    p.zone_text = gfx::mix(p.sheet, p.ink, 0.6f);
    p.cream = p.sheet;
  } else {
    p.grid = hex(0xE4D8C4);
    p.axis = gfx::scale(p.ink, 1.7f);
    p.rh = hex(0xCEC4B2);
    p.rh_text = hex(0xA09684);
    // The mockup's gray at about 43 % over the sheet, flattened so the grid and curves can go on top.
    p.zone = gfx::mix(p.sheet, hex(0xC8C4BC), 0.43f);
    p.zone2 = gfx::mix(p.zone, hex(0xC8C4BC), 0.43f);  // where the two zones overlap: darker
    p.zone_line = hex(0xB0ACA4);
    p.zone_text = hex(0x96928A);
    p.cream = hex(0xFBF1DC);
  }
  return p;
}

// ---- the data -------------------------------------------------------------------------------

constexpr int16_t kOff = -32768;  // xy x for a point that isn't on the chart

void free_points() {
  if (s.pts) gfx::release_bytes(s.pts);
  s.pts = nullptr;
  s.gr = nullptr;
  s.xy = nullptr;
  s.n = 0;
  if (s.rh) gfx::release_bytes(s.rh);
  s.rh = nullptr;
  s.rh_n = 0;
  s.rh_ok = false;
}

// Where a temperature and a moisture fall on the plot in the view shown. With the whole chart
// showing these are the chart's first formulas exactly.
float px_of(float t) { return chart_x(s.view, t, kPL, kPR); }
float py_of(float g) { return chart_y(s.view, g, kPT, kPB); }

// Each dot's place on the plot in the view shown: at load, and again whenever the view moves (a
// multiply and an add per dot; the gr/lb were worked out at load). kOff when the point is outside
// the whole chart's range (never drawn, never counted) or its answer is not a known one.
void place_all() {
  for (int i = 0; i < s.n; i++) {
    if (s.gr[i] < 0) {
      s.xy[2 * i] = kOff;
      s.xy[2 * i + 1] = 0;
      continue;
    }
    s.xy[2 * i] = (int16_t)lroundf(px_of(s.pts[i].t / 10.0f));
    s.xy[2 * i + 1] = (int16_t)lroundf(py_of(s.gr[i]));
  }
}

void load_points() {
  free_points();
  s.err = false;
  s.rh = (RhLine *)gfx::alloc_bytes(kRhMax * sizeof(RhLine));  // without it the curves are worked out as they're drawn
  if (!g_act) return;
  size_t len = 0;
  uint8_t *buf = g_act->read_file(kComfortFile, len);
  if (!buf) return;  // no file: no answers yet
  int cap = comfort_count_lines((const char *)buf, len);
  if (cap > 0) {
    // One block: the points, then a float and a pair of int16 for each.
    s.pts = (ComfortPoint *)gfx::alloc_bytes((size_t)cap * (sizeof(ComfortPoint) + sizeof(float) + 2 * sizeof(int16_t)));
    if (s.pts) {
      s.gr = (float *)(s.pts + cap);
      s.xy = (int16_t *)(s.gr + cap);
      s.n = comfort_parse((const char *)buf, len, s.pts, cap);
      for (int i = 0; i < s.n; i++) {
        const ComfortPoint &p = s.pts[i];
        float t = p.t / 10.0f, g = grains_per_lb(t, p.h / 10.0f);
        bool on = feel_valid(p.feel) && t >= kChartTMin && t <= kChartTMax && g >= 0 && g <= kChartGrMax;
        s.gr[i] = on ? g : -1;
      }
      place_all();
    } else {
      s.err = true;
    }
  }
  g_act->free_file(buf);  // the points are all the chart keeps
  if (s.n == 0 && s.pts) {
    gfx::release_bytes(s.pts);
    s.pts = nullptr;
    s.gr = nullptr;
    s.xy = nullptr;
  }
}

// The dot's place on the plot; false when it isn't on the chart.
bool placed(int i, int &x, int &y) {
  if (!s.xy || s.xy[2 * i] == kOff) return false;
  x = s.xy[2 * i];
  y = s.xy[2 * i + 1];
  return true;
}

bool chosen(const ComfortPoint &p) { return !p.removed && (s.chip == 0 || p.part == s.chip - 1); }

// "67°F" -> 67. False for "--°F" and the like.
bool leading_number(const char *t, int &out) {
  if (!t || !(*t == '-' || (*t >= '0' && *t <= '9'))) return false;
  if (*t == '-' && !(t[1] >= '0' && t[1] <= '9')) return false;
  out = atoi(t);
  return true;
}

void read_now() {
  s.now_ok = false;
  if (!g_f.view) return;
  const FloorView &fv = g_f.view->floor[(int)Floor::Downstairs];
  int t, h;
  if (leading_number(fv.temp, t) && leading_number(fv.hum, h)) {
    s.now_ok = true;
    s.now_t = t;
    s.now_h = h;
  }
}

// ---- drawing --------------------------------------------------------------------------------

lv_area_t area(int x0, int y0, int x1, int y1) {
  lv_area_t a;
  a.x1 = (lv_coord_t)x0;
  a.y1 = (lv_coord_t)y0;
  a.x2 = (lv_coord_t)x1;
  a.y2 = (lv_coord_t)y1;
  return a;
}

lv_opa_t opa_of(float strength) {
  if (strength >= 1) return LV_OPA_COVER;
  return strength <= 0 ? (lv_opa_t)LV_OPA_TRANSP : (lv_opa_t)lroundf(strength * 255);
}

void rrect(lv_draw_ctx_t *dc, int x0, int y0, int x1, int y1, int r, Color fill, bool filled, Color edge, int bw) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = r;
  d.bg_color = C(fill);
  d.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
  d.border_width = bw;
  d.border_color = C(edge);
  d.border_opa = bw ? LV_OPA_COVER : LV_OPA_TRANSP;
  lv_area_t a = area(x0, y0, x1, y1);
  lv_draw_rect(dc, &d, &a);
}

void line(lv_draw_ctx_t *dc, float x0, float y0, float x1, float y1, int w, Color c, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = C(c);
  d.width = w;
  d.opa = opa;
  lv_point_t a = {(lv_coord_t)lroundf(x0), (lv_coord_t)lroundf(y0)}, b = {(lv_coord_t)lroundf(x1), (lv_coord_t)lroundf(y1)};
  lv_draw_line(dc, &d, &a, &b);
}

// A line on the plot, cut where it leaves the plot's rectangle (worked out, not masked). A long
// slanted one is drawn in short pieces: LVGL pays for a slanted line's whole bounding box, which is
// small for a short piece and very large for a long line zoomed in.
void plot_line(lv_draw_ctx_t *dc, float x0, float y0, float x1, float y1, int w, Color c, lv_opa_t opa = LV_OPA_COVER) {
  float u0 = 0, u1 = 1, dx = x1 - x0, dy = y1 - y0;
  const float pp[4] = {-dx, dx, -dy, dy}, qq[4] = {x0 - kPL, kPR - x0, y0 - kPT, kPB - y0};
  for (int i = 0; i < 4; i++) {
    if (pp[i] == 0) {
      if (qq[i] < 0) return;  // alongside the plot, outside it
      continue;
    }
    float r = qq[i] / pp[i];
    if (pp[i] < 0) {
      if (r > u1) return;
      if (r > u0) u0 = r;
    } else {
      if (r < u0) return;
      if (r < u1) u1 = r;
    }
  }
  float ax = u0 > 0 ? x0 + dx * u0 : x0, ay = u0 > 0 ? y0 + dy * u0 : y0;
  float bx = u1 < 1 ? x0 + dx * u1 : x1, by = u1 < 1 ? y0 + dy * u1 : y1;
  float lx = fabsf(bx - ax), ly = fabsf(by - ay), longest = lx > ly ? lx : ly;
  int pieces = 1;
  if (lx >= 1 && ly >= 1 && longest > 80) pieces = (int)ceilf(longest / 32);
  for (int i = 0; i < pieces; i++) {
    float f0 = (float)i / pieces, f1 = (float)(i + 1) / pieces;
    line(dc, ax + (bx - ax) * f0, ay + (by - ay) * f0, ax + (bx - ax) * f1, ay + (by - ay) * f1, w, c, opa);
  }
}

enum Anchor { kLeft, kCenter, kRight };
// Text with its vertical middle at cy; x is its left, middle or right.
void text(lv_draw_ctx_t *dc, const lv_font_t *f, Color c, float x, float cy, const char *t, Anchor an,
          lv_opa_t opa = LV_OPA_COVER) {
  int w = text_width(t, f), h = f->line_height;
  int x0 = (int)lroundf(x) - (an == kCenter ? w / 2 : an == kRight ? w : 0);
  int y0 = (int)lroundf(cy) - h / 2;
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = f;
  d.color = C(c);
  d.opa = opa;
  lv_area_t a = area(x0, y0, x0 + w + 2, y0 + h);
  lv_draw_label(dc, &d, &a, t, nullptr);
}

// A zone's cool and warm edges (F) at a moisture between its rows.
void zone_at(float g, float &w_lo, float &w_hi, float &s_lo, float &s_hi) {
  int i = 0;
  while (i + 2 < kZoneRows && g > kComfortZones[i + 1].gr) i++;
  const ZoneRow &a = kComfortZones[i], &b = kComfortZones[i + 1];
  float f = (g - a.gr) / (b.gr - a.gr);
  w_lo = a.winter_lo + (b.winter_lo - a.winter_lo) * f;
  w_hi = a.winter_hi + (b.winter_hi - a.winter_hi) * f;
  s_lo = a.summer_lo + (b.summer_lo - a.summer_lo) * f;
  s_hi = a.summer_hi + (b.summer_hi - a.summer_hi) * f;
}

// The two comfort zones' fills (winter and summer clothing bands), flat, and darker where both
// cover. Worked out a pixel row at a time and drawn as plain rectangles, each pixel once: the rows
// stop at the plot's edges and at the area being redrawn, so nothing needs a mask (a masked polygon
// costs about three times as much on the panel) and nothing can land outside the plot.
void zone_fills(lv_draw_ctx_t *dc, const Pal &p, const lv_area_t &clip) {
  lv_draw_rect_dsc_t d1, d2;
  lv_draw_rect_dsc_init(&d1);
  d1.bg_color = C(p.zone);
  d1.bg_opa = LV_OPA_COVER;
  d2 = d1;
  d2.bg_color = C(p.zone2);
  const float g_top = kComfortZones[kZoneRows - 1].gr;
  int ya = clip.y1 > kPT ? clip.y1 : kPT, yb = clip.y2 < kPB ? clip.y2 : kPB;
  int xa = clip.x1 > kPL ? clip.x1 : kPL, xb = clip.x2 < kPR ? clip.x2 : kPR;
  const auto span = [&](lv_draw_rect_dsc_t &d, int x0, int x1, int y) {
    if (x0 < xa) x0 = xa;
    if (x1 > xb) x1 = xb;
    if (x1 < x0) return;
    lv_area_t a = area(x0, y, x1, y);
    lv_draw_rect(dc, &d, &a);
  };
  for (int y = ya; y <= yb; y++) {
    float g = s.view.g0 + (float)(kPB - y) / (kPB - kPT) * (s.view.g1 - s.view.g0);
    if (g < 0 || g > g_top) continue;
    float wl, wh, sl, sh;
    zone_at(g, wl, wh, sl, sh);
    int x_wl = (int)lroundf(px_of(wl)), x_wh = (int)lroundf(px_of(wh)), x_sl = (int)lroundf(px_of(sl)),
        x_sh = (int)lroundf(px_of(sh));
    if (x_sl > x_wh) {  // no overlap at this row
      span(d1, x_wl, x_wh, y);
      span(d1, x_sl, x_sh, y);
    } else {
      span(d1, x_wl, x_sl - 1, y);
      span(d2, x_sl, x_wh, y);
      span(d1, x_wh + 1, x_sh, y);
    }
  }
}

void zone_outline(lv_draw_ctx_t *dc, const Pal &p, bool summer) {
  auto lo = [&](int i) { return summer ? kComfortZones[i].summer_lo : kComfortZones[i].winter_lo; };
  auto hi = [&](int i) { return summer ? kComfortZones[i].summer_hi : kComfortZones[i].winter_hi; };
  for (int i = 0; i + 1 < kZoneRows; i++) {
    plot_line(dc, px_of(lo(i)), py_of(kComfortZones[i].gr), px_of(lo(i + 1)), py_of(kComfortZones[i + 1].gr), 2, p.zone_line);
    plot_line(dc, px_of(hi(i)), py_of(kComfortZones[i].gr), px_of(hi(i + 1)), py_of(kComfortZones[i + 1].gr), 2, p.zone_line);
  }
  int top = kZoneRows - 1;
  plot_line(dc, px_of(lo(top)), py_of(kComfortZones[top].gr), px_of(hi(top)), py_of(kComfortZones[top].gr), 2, p.zone_line);
  plot_line(dc, px_of(lo(0)), py_of(0), px_of(hi(0)), py_of(0), 2, p.zone_line);
}

// A zone's name: above the zone where the chart has it; when that spot is off the plot it stays
// inside the plot's edges, as long as the zone itself is in view.
void zone_names(lv_draw_ctx_t *dc, const Pal &p) {
  const float g_top = kComfortZones[kZoneRows - 1].gr;
  if (s.view.g0 >= g_top) return;  // the view is above both zones
  const struct {
    const char *word;
    float t, g, lo, hi;  // its place, and the zone's widest reach (F)
  } names[2] = {{"WINTER CLOTHING", 67.4f, 92, kComfortZones[kZoneRows - 1].winter_lo, kComfortZones[0].winter_hi},
                {"SUMMER CLOTHING", 76.6f, 100, kComfortZones[kZoneRows - 1].summer_lo, kComfortZones[0].summer_hi}};
  int last_x = 0, last_w = 0;
  float last_y = 0;
  bool have = false;
  for (const auto &nm : names) {
    if (nm.hi <= s.view.t0 || nm.lo >= s.view.t1) continue;  // this zone is out of view
    int w = text_width(nm.word, B11);
    float x = px_of(nm.t), y = py_of(nm.g);
    if (x > kPR - 6 - w) x = (float)(kPR - 6 - w);
    if (x < kPL + 6) x = kPL + 6;
    if (y < kPT + 24) y = kPT + 24;  // the top row is left for the humidity curves' labels
    // Both pushed to the same edge: the second goes a line lower.
    if (have && fabsf(y - last_y) < 13 && x < last_x + last_w + 8 && x + w + 8 > last_x) y = last_y + 14;
    text(dc, B11, p.zone_text, x, y, nm.word, kLeft);
    last_x = (int)lroundf(x);
    last_w = w;
    last_y = y;
    have = true;
  }
}

// How strongly a labelled value shows while the view glides from one set of steps to another. A
// value in both sets stays at full strength the whole way, so the eye has something to follow.
// One only in the old set fades out over the first half of the way; one only in the new set fades
// in over the second half. The two are never shown together: side by side they crowd each other
// (65 and 66 degrees, say) and read as clutter. 0 when the value is in neither set.
float step_strength(int v, int a, int b) {
  bool in_a = v % a == 0, in_b = v % b == 0;
  if (in_a && in_b) return 1;
  float k = in_a ? 1 - 2 * s.fade : in_b ? 2 * s.fade - 1 : 0;
  return k < 0 ? 0 : k;
}

// A grid line's label must not pop in at the plot's edge in the middle of a glide: within 20 px of
// the edge it is fainter the more the view is moving. At rest it is always full strength.
float edge_strength(float px_from_edge) {
  if (s.motion <= 0) return 1;
  float f = px_from_edge / 20;
  if (f < 0) f = 0;
  return f >= 1 ? 1 : 1 - s.motion * (1 - f);
}

// The grid: a line and a label for each temperature and each moisture on a labelled step that is
// in view. Labels sit where they always did: temperatures under the plot, gr/lb to its right.
void grid(lv_draw_ctx_t *dc, const Pal &p, bool lines, const lv_area_t &clip) {
  char buf[16];
  const ChartView &v = s.view;
  bool t_on = lines || clip.y2 >= kPB, g_on = lines || clip.x2 >= kPR;  // the labels' strips, or not in this band
  for (int t = (int)ceilf(v.t0 - 0.001f); t_on && t <= (int)floorf(v.t1 + 0.001f); t++) {
    float k = step_strength(t, s.t_a, s.t_b);
    if (k <= 0) continue;
    float x = px_of((float)t);
    k *= edge_strength(x - kPL < kPR - x ? x - kPL : kPR - x);
    lv_opa_t o = opa_of(k);
    if (o < 8) continue;
    if (lines) {
      line(dc, x, kPT, x, kPB, 1, p.grid, o);
    } else {
      snprintf(buf, sizeof(buf), "%d%sF", t, kDeg);
      text(dc, B13, p.axis, x, kPB + 16, buf, kCenter, o);
    }
  }
  for (int g = (int)ceilf(v.g0 - 0.001f); g_on && g <= (int)floorf(v.g1 + 0.001f); g++) {
    float k = step_strength(g, s.g_a, s.g_b);
    if (k <= 0) continue;
    float y = py_of((float)g);
    k *= edge_strength(y - kPT < kPB - y ? y - kPT : kPB - y);
    lv_opa_t o = opa_of(k);
    if (o < 8) continue;
    if (lines) {
      line(dc, kPL, y, kPR, y, 1, p.grid, o);
    } else {
      snprintf(buf, sizeof(buf), "%d", g);
      text(dc, B13, p.axis, kPR + 8, y, buf, kLeft, o);
    }
  }
}

// One relative-humidity curve across the view shown, cut where it leaves by the top or the bottom,
// and where its label goes: beside the point where it leaves by the top, or just above it near the
// right edge.
void rh_build(int rh_pct, RhLine &o) {
  const ChartView &v = s.view;
  o.rh = (uint8_t)rh_pct;
  o.n = 0;
  o.label = 0;
  o.lx = o.ly = 0;
  const auto add = [&o](float x, float y) {
    if (o.n >= kRhPts) return;
    o.pt[2 * o.n] = (int16_t)lroundf(x);
    o.pt[2 * o.n + 1] = (int16_t)lroundf(y);
    o.n++;
  };
  const float rh = (float)rh_pct, dt = (v.t1 - v.t0) / 60;  // 60 pieces: half a degree each on the whole chart
  float last_t = 0, last_g = 0, prev_t = 0, prev_g = 0;
  bool have = false;
  for (int i = 0; i <= 60; i++) {
    float t = v.t0 + i * dt, g = grains_per_lb(t, rh);
    if (g < v.g0) {  // still under the view
      prev_t = t;
      prev_g = g;
      continue;
    }
    if (!have && i > 0 && prev_g < v.g0 && g > v.g0) {  // comes up through the bottom edge: start there
      float f = (v.g0 - prev_g) / (g - prev_g);
      last_t = prev_t + dt * f;
      last_g = v.g0;
      add(px_of(last_t), py_of(v.g0));
      have = true;
    }
    if (g > v.g1) {  // runs off the top: end it exactly at the top edge
      if (have) {
        float f = (v.g1 - last_g) / (g - last_g);
        last_t = last_t + (t - last_t) * f;
        last_g = v.g1;
        add(px_of(last_t), py_of(v.g1));
      }
      break;
    }
    add(px_of(t), py_of(g));
    last_t = t;
    last_g = g;
    have = true;
  }
  if (o.n < 2) {
    o.n = 0;
    return;
  }
  char lab[8];
  snprintf(lab, sizeof(lab), "%d%%", rh_pct);
  int w = text_width(lab, B12);
  if (last_g > v.g1 - 6 * (v.g1 - v.g0) / kChartGrMax) {  // it ends at the top (within 16 px of it)
    float x = px_of(last_t) - 6;
    if (x - w >= kPL + 2) {
      o.label = 1;
      o.lx = (int16_t)lroundf(x);
      o.ly = kPT + 10;
    }
  } else {
    float tl = v.t1 - 1.2f * ((v.t1 - v.t0) / (kChartTMax - kChartTMin)), gl = grains_per_lb(tl, rh);
    if (gl > v.g0) {
      o.label = 2;
      o.lx = (int16_t)lroundf(px_of(tl));
      o.ly = (int16_t)lroundf(py_of(gl) - 10);
    }
  }
}

void rh_draw(lv_draw_ctx_t *dc, const Pal &p, const RhLine &o, float strength) {
  lv_opa_t op = opa_of(strength);
  if (op < 8 || o.n < 2) return;
  for (int i = 1; i < o.n; i++) line(dc, o.pt[2 * i - 2], o.pt[2 * i - 1], o.pt[2 * i], o.pt[2 * i + 1], 2, p.rh, op);
  if (!o.label) return;
  // A label can change places as the view moves, so it is fainter in the middle of a glide.
  lv_opa_t lo = opa_of(strength * (1 - 0.75f * s.motion));
  if (lo < 8) return;
  char lab[8];
  snprintf(lab, sizeof(lab), "%d%%", o.rh);
  text(dc, B12, p.rh_text, o.lx, o.ly, lab, o.label == 1 ? kRight : kCenter, lo);
}

// The curves on the old step fade into the curves on the new one, like the grid.
void rh_curves(lv_draw_ctx_t *dc, const Pal &p) {
  int step = s.rh_a < s.rh_b ? s.rh_a : s.rh_b;
  if (s.rh_a % step || s.rh_b % step) step = 1;
  if (!s.rh) {  // no room for the kept curves: work each out as it is drawn
    RhLine one;
    for (int rh = step; rh <= 100; rh += step) {
      float k = step_strength(rh, s.rh_a, s.rh_b);
      if (k <= 0) continue;
      rh_build(rh, one);
      rh_draw(dc, p, one, k);
    }
    return;
  }
  if (!s.rh_ok) {
    s.rh_n = 0;
    for (int rh = step; rh <= 100 && s.rh_n < kRhMax; rh += step) {
      if (step_strength(rh, s.rh_a, s.rh_b) <= 0) continue;
      rh_build(rh, s.rh[s.rh_n]);
      if (s.rh[s.rh_n].n) s.rh_n++;
    }
    s.rh_ok = true;
  }
  for (int i = 0; i < s.rh_n; i++) rh_draw(dc, p, s.rh[i], step_strength(s.rh[i].rh, s.rh_a, s.rh_b));
}

// The "now" marker: a ring at the reading, a pill with the reading beside it. When the reading is
// outside the view (the dots are elsewhere, or mid-zoom) the pill stays at the plot's edge nearest
// the reading, its tail pointing at the edge, so the reading can always be read; the ring slides
// out under the edge.
struct NowGeo {
  bool ok = false;      // there is a reading and it is on the whole chart
  bool inside = false;  // and in the view shown
  int cx = 0, cy = 0;   // the reading's place: the ring
  int ax = 0, ay = 0;   // the same held inside the plot: what the pill and its tail go by
  int x0 = 0, y0 = 0, w = 0, dir = 0;
  bool below = false;
  char txt[40];
  lv_area_t box;  // everything it draws
};
NowGeo now_geo() {
  NowGeo n;
  n.box = area(0, 0, -1, -1);
  if (!s.now_ok || !g_f.view) return n;
  float t = (float)s.now_t, g = grains_per_lb(t, (float)s.now_h);
  if (t < kChartTMin || t > kChartTMax || g < 0 || g > kChartGrMax) return n;
  const FloorView &fv = g_f.view->floor[(int)Floor::Downstairs];
  snprintf(n.txt, sizeof(n.txt), "NOW %s  %s", fv.temp, fv.hum);
  float fx = px_of(t), fy = py_of(g);
  n.ok = true;
  n.inside = t >= s.view.t0 && t <= s.view.t1 && g >= s.view.g0 && g <= s.view.g1;
  // Far out of view the ring is never drawn; its place only has to stay a sane number.
  fx = fx < -2000 ? -2000 : fx > 2800 ? 2800 : fx;
  fy = fy < -2000 ? -2000 : fy > 2500 ? 2500 : fy;
  n.cx = (int)lroundf(fx);
  n.cy = (int)lroundf(fy);
  n.ax = n.cx < kPL ? kPL : n.cx > kPR ? kPR : n.cx;
  n.ay = n.cy < kPT ? kPT : n.cy > kPB ? kPB : n.cy;
  n.w = text_width(n.txt, B13);
  // The mockup puts the pill up and to the left; near the left edge it goes to the right.
  bool left = n.ax - 26 - n.w - 12 >= kPL - 40;
  n.dir = left ? -1 : 1;
  n.x0 = left ? n.ax - 26 - n.w - 12 : n.ax + 14;
  n.y0 = n.ay - 36 < kPT - 10 ? n.ay + 14 : n.ay - 36;
  n.below = n.y0 > n.ay;
  int bx0 = n.x0 < n.ax - 11 ? n.x0 : n.ax - 11, bx1 = n.x0 + n.w + 12 > n.ax + 11 ? n.x0 + n.w + 12 : n.ax + 11;
  int by0 = n.y0 < n.ay - 11 ? n.y0 : n.ay - 11, by1 = n.y0 + 22 > n.ay + 11 ? n.y0 + 22 : n.ay + 11;
  n.box = area(bx0 - 2, by0 - 2, bx1 + 2, by1 + 2);
  return n;
}

// The ring, where the reading is. Outside the view it is drawn with the plot, cut at the plot's edge.
void draw_now_ring(lv_draw_ctx_t *dc, const Pal &p, const NowGeo &n) {
  rrect(dc, n.cx - 9, n.cy - 9, n.cx + 9, n.cy + 9, LV_RADIUS_CIRCLE, p.ink, false, p.ink, 3);
}

// The pill and its tail.
void draw_now_pill(lv_draw_ctx_t *dc, const Pal &p, const NowGeo &n) {
  // plum ink at night is the cream ink, with the text on it in the sheet colour
  line(dc, n.ax + n.dir * 7, n.ay + (n.below ? 7 : -7), n.ax + n.dir * 18, n.ay + (n.below ? 24 : -24), 2, p.ink);
  rrect(dc, n.x0, n.y0, n.x0 + n.w + 12, n.y0 + 22, 11, p.ink, true, p.ink, 0);
  text(dc, B13, p.cream, n.x0 + 6 + n.w / 2.0f, n.y0 + 11, n.txt, kCenter);
}

void draw_card(lv_event_t *e) {
  lv_draw_ctx_t *dc = lv_event_get_draw_ctx(e);
  if (!dc) return;
  s.seen = true;
  Pal p = pal();
  // The close X's cross (its ring is an object).
  line(dc, 749, 30, 763, 44, 3, p.ink);
  line(dc, 763, 30, 749, 44, 3, p.ink);

  // The plot. While it is drawn, the area being redrawn is narrowed to the plot (and kPad round
  // it), so a dot or a name sliding out during a zoom is cut at the edge. That is the redraw
  // rectangle itself made smaller, not a mask, so it costs nothing.
  const lv_area_t *clip0 = dc->clip_area;
  lv_area_t clip = clip0 ? *clip0 : area(0, 0, 799, 479);
  lv_area_t pad = area(kPL - kPad, kPT - kPad, kPR + kPad, kPB + kPad), plot_clip;
  bool plot_on = _lv_area_intersect(&plot_clip, &clip, &pad);
  NowGeo now = now_geo();
  int count[kFeelCount] = {}, total = 0;
  if (plot_on) {
    dc->clip_area = &plot_clip;
    // The zones first, so the grid and curves lie over them.
    zone_fills(dc, p, plot_clip);
    grid(dc, p, true, plot_clip);
    zone_outline(dc, p, false);
    zone_outline(dc, p, true);
    rh_curves(dc, p);
    zone_names(dc, p);
  }
  // The dots, oldest first so the newest lie on top; the counts come from the same pass.
  for (int i = 0; i < s.n; i++) {
    const ComfortPoint &pt = s.pts[i];
    int cx, cy;
    if (!chosen(pt) || !placed(i, cx, cy)) continue;
    count[pt.feel]++;
    total++;
    // Only the dots inside the area being redrawn cost anything.
    if (!plot_on || cx + 4 < plot_clip.x1 || cx - 4 > plot_clip.x2 || cy + 4 < plot_clip.y1 || cy - 4 > plot_clip.y2) continue;
    rrect(dc, cx - 3, cy - 3, cx + 3, cy + 3, LV_RADIUS_CIRCLE, hex(kLook[pt.feel].face), true, p.ring, 1);
  }
  // A "now" reading outside the view: its ring is cut at the plot's edge.
  if (plot_on && now.ok && !now.inside) draw_now_ring(dc, p, now);
  dc->clip_area = clip0;

  grid(dc, p, false, clip);
  text(dc, B12, p.axis, kPR + 8, kPT - 14, "gr/lb", kLeft);
  if (now.ok) {
    draw_now_pill(dc, p, now);
    if (now.inside) draw_now_ring(dc, p, now);
  }
  if (s.pick >= 0 && s.pick < s.n) {
    int cx, cy;
    if (placed(s.pick, cx, cy)) {
      rrect(dc, cx - 10, cy - 10, cx + 10, cy + 10, LV_RADIUS_CIRCLE, p.gold, false, p.gold, 3);
    }
  }

  // The legend and the title's line are worked out only for the bands of the card they are in.
  if (clip.y2 < 452 - 14 && clip.y1 > 58 + 14 && !s.err) return;
  // The legend along the bottom: each answer's colour, name and count for the chosen part. It
  // has to end before the card's right edge at any count, so it steps down in size and spacing.
  char buf[48];
  char leg[kFeelCount][32];
  for (int f = 0; f < kFeelCount; f++) snprintf(leg[f], sizeof(leg[f]), "%s  %d", kLook[f].word, count[f]);
  struct LegStep {
    const lv_font_t *font;
    int gap, dot, pad;
  };
  const LegStep steps[] = {{B14, 22, 12, 17}, {B13, 14, 10, 14}, {B12, 8, 8, 11}, {B12, 4, 8, 11}};
  const LegStep *ls = &steps[3];
  for (const LegStep &st : steps) {
    int w = 28 - st.gap;
    for (int f = 0; f < kFeelCount; f++) w += st.pad + text_width(leg[f], st.font) + st.gap;
    if (w <= 772) {
      ls = &st;
      break;
    }
  }
  float x = 28;
  for (int f = 0; f < kFeelCount; f++) {
    int dy = 452 - ls->dot / 2;
    rrect(dc, (int)x, dy, (int)x + ls->dot, dy + ls->dot, LV_RADIUS_CIRCLE, hex(kLook[f].face), true, p.ring, 1);
    text(dc, ls->font, p.ink, x + ls->pad, 452, leg[f], kLeft);
    x += ls->pad + text_width(leg[f], ls->font) + ls->gap;
  }
  if (s.err) text(dc, B16, p.ink, (kPL + kPR) / 2.0f, (kPT + kPB) / 2.0f, "Not enough memory to draw the chart", kCenter);
  // The title's line: how many answers, since when.
  const char *since = "";
  char sbuf[16] = "";
  for (int i = 0; i < s.n; i++)
    if (s.pts[i].year && s.pts[i].month >= 1 && s.pts[i].month <= 12) {
      snprintf(sbuf, sizeof(sbuf), " since %s %d", kMonth[s.pts[i].month - 1], s.pts[i].day);
      since = sbuf;
      break;
    }
  snprintf(buf, sizeof(buf), "%d answer%s%s", total, total == 1 ? "" : "s", since);
  text(dc, B13, p.axis, 28, 52 + 6, buf, kLeft);
}

// ---- what to redraw -------------------------------------------------------------------------
// The panel's drawing time is set by how many pixels are redrawn, so each change marks only its own
// part of the card.

// LVGL 8.4 itself grows every area an object marks by 5 px each way (the last line of
// lv_obj_get_transformed_area), so the area is made that much smaller first. Without this each
// frame of the zoom would redraw about 30,000 pixels it has no need to. If a later LVGL stops
// growing them, set this to 0.
constexpr int kLvGrow = 5;

void mark(int x0, int y0, int x1, int y1) {
  if (!s.card || x1 < x0 || y1 < y0) return;
  x0 += kLvGrow;
  x1 -= kLvGrow;
  y0 += kLvGrow;
  y1 -= kLvGrow;
  if (x1 < x0) x0 = x1 = (x0 + x1) / 2;
  if (y1 < y0) y0 = y1 = (y0 + y1) / 2;
  lv_area_t a = area(x0, y0, x1, y1);
  lv_obj_invalidate_area(s.card, &a);
}

// The "now" marker: where it was and where it is.
void mark_now() {
  if (s.now_box.x2 >= s.now_box.x1) mark(s.now_box.x1, s.now_box.y1, s.now_box.x2, s.now_box.y2);
  NowGeo n = now_geo();
  s.now_box = n.box;
  if (n.ok) mark(n.box.x1, n.box.y1, n.box.x2, n.box.y2);
}

// The view moved: the plot, the strip of temperatures under it and the strip of gr/lb to its
// right. Not the title, the chips, the legend or the card's edges.
void mark_view() {
  mark(kPL - kPad, kPT - kPad, kPR + kPad, kPB + kPad);
  int h = B13->line_height, half = text_width("88\xC2\xB0""F", B13) / 2 + 2;
  mark(kPL - half, kPB + 16 - h / 2, kPR + half, kPB + 16 - h / 2 + h);
  mark(kPR + 8, kPT - h / 2, kPR + 8 + text_width("120", B13) + 2, kPB - h / 2 + h);
  mark_now();
}

// The counts changed (a chip, a removal): the legend and the title's line.
void mark_counts() {
  mark(kCardX, 452 - 14, kCardX + kCardW - 1, 452 + 14);
  mark(28, 58 - B13->line_height / 2, 400, 58 - B13->line_height / 2 + B13->line_height);
}

// One dot and the gold ring round it.
void mark_dot(int i) {
  int cx, cy;
  if (i >= 0 && i < s.n && placed(i, cx, cy)) mark(cx - 12, cy - 12, cx + 12, cy + 12);
}

// ---- the zoom (Mike, 2026-11-18) ------------------------------------------------------------

int whole_step(float span) {
  float st = chart_nice_step(span);
  return st < 1 ? 1 : (int)lroundf(st);
}

void show_view(const ChartView &v) {
  s.view = v;
  s.rh_ok = false;
  if (s.xy) place_all();
  mark_view();
}

void stop_anim() {
  if (s.anim) lv_timer_del(s.anim);
  s.anim = nullptr;
}

void anim_tick(lv_timer_t *);

// Glide from the view shown to the view for the dots now chosen, with the zoom's ease. Nothing
// happens when the plot already shows that view, or is already on its way there.
void glide_to_dots() {
  ChartView tgt = s.pts ? chart_target_view(s.pts, s.n, s.chip) : kChartFull;
  if (s.phase == kMove && chart_view_same(tgt, s.to)) return;
  if (chart_view_same(tgt, s.view)) {
    if (s.phase == kMove) {  // stopped where it is: settle the labels on the steps for this view
      s.t_a = s.t_b;
      s.g_a = s.g_b;
      s.rh_a = s.rh_b;
      s.fade = 0;
      s.motion = 0;
      show_view(tgt);
    }
    s.phase = kRest;
    stop_anim();
    return;
  }
  // The steps showing now are the old set; a glide cut short keeps whichever set it was nearer.
  if (s.phase == kMove && s.fade >= 0.5f) {
    s.t_a = s.t_b;
    s.g_a = s.g_b;
    s.rh_a = s.rh_b;
  }
  s.t_b = whole_step(tgt.t1 - tgt.t0);
  s.g_b = whole_step(tgt.g1 - tgt.g0);
  s.rh_b = chart_rh_step(tgt);
  s.fade = 0;
  s.motion = 0;
  s.rh_ok = false;
  s.from = s.view;
  s.to = tgt;
  s.t0 = lv_tick_get();
  s.phase = kMove;
  if (!s.anim) s.anim = lv_timer_create(anim_tick, kAnimMs, nullptr);
}

// The zoom's clock. The view is worked out from the time gone, never from a count of frames, so
// the glide ends on time however long the panel takes over each frame.
void anim_tick(lv_timer_t *) {
  if (g_sheet != Sheet::Chart || !g_sheet_root) {
    stop_anim();
    return;
  }
  if (s.phase == kHold) {
    if (!s.hold_run) {  // the hold counts from when the whole chart has been drawn and shown
      if (!s.seen) return;
      s.hold_run = true;
      s.t0 = lv_tick_get();
      return;
    }
    // A dot picked during the hold keeps the chart still until its card is closed.
    if (lv_tick_elaps(s.t0) < kChartHoldMs || s.callout) return;
    s.phase = kRest;
    glide_to_dots();  // starts the glide, or stops this clock when there is nowhere to go
    if (s.phase == kRest) stop_anim();
    return;
  }
  if (s.phase != kMove) {
    stop_anim();
    return;
  }
  float u = (float)lv_tick_elaps(s.t0) / kChartZoomMs;
  if (u >= 1) {  // arrived: the end view exactly, on its own steps
    s.t_a = s.t_b;
    s.g_a = s.g_b;
    s.rh_a = s.rh_b;
    s.fade = 0;
    s.motion = 0;
    s.phase = kRest;
    show_view(s.to);
    stop_anim();
    return;
  }
  float e = chart_ease(u);
  ChartView v = chart_view_lerp(s.from, s.to, e);
  // Under a third of a pixel since the last frame (the ease's slow tail): not worth a redraw yet.
  float dx = fabsf(v.t0 - s.view.t0) > fabsf(v.t1 - s.view.t1) ? fabsf(v.t0 - s.view.t0) : fabsf(v.t1 - s.view.t1);
  float dy = fabsf(v.g0 - s.view.g0) > fabsf(v.g1 - s.view.g1) ? fabsf(v.g0 - s.view.g0) : fabsf(v.g1 - s.view.g1);
  if (dx / (v.t1 - v.t0) * (kPR - kPL) < 0.34f && dy / (v.g1 - v.g0) * (kPB - kPT) < 0.34f) return;
  s.fade = e;
  float m = 6 * e * (1 - e);
  s.motion = m > 1 ? 1 : m;
  show_view(v);
}

// ---- the callout ----------------------------------------------------------------------------

void hide_callout() {
  if (s.callout) lv_obj_del_async(s.callout);
  s.callout = nullptr;
  if (s.pick >= 0) {
    mark_dot(s.pick);
    s.pick = -1;
  }
}

void remove_clicked(lv_event_t *) {
  // The dot goes only if the controller took the removal (it marks the file).
  bool gone = s.pick >= 0 && s.pick < s.n && g_act && g_act->comfort_remove(s.pts[s.pick].n);
  if (gone) s.pts[s.pick].removed = true;
  hide_callout();
  if (!gone) return;
  mark_counts();
  // The dots that are left may take up less room: glide to them (Mike, 2026-11-18).
  if (s.phase == kHold) return;  // still on the opening hold: its zoom goes to the dots that are left
  glide_to_dots();
}

void show_callout(int idx) {
  hide_callout();
  int cx, cy;
  const ComfortPoint &pt = s.pts[idx];
  if (!placed(idx, cx, cy)) return;
  s.pick = idx;
  Pal p = pal();
  bool can_remove = g_set.floor == Floor::Downstairs;  // Mike, 2026-11-06: only downstairs removes
  const int cw = 250, ch = can_remove ? 140 : 96;
  int bx = cx + 22, by = cy - 120;
  if (bx + cw + 6 > kCardX + kCardW - 6) bx = cx - 22 - cw;
  if (by < 60) by = 60;
  if (by + ch + 6 > kCardY + kCardH - 6) by = kCardY + kCardH - 6 - ch - 6;
  lv_obj_t *wrap = plain(g_sheet_root, bx, by, cw + 6, ch + 6);
  lv_obj_add_flag(wrap, LV_OBJ_FLAG_CLICKABLE);  // a tap on the card itself doesn't reach the plot
  s.callout = wrap;
  Color paper = g_mode == Mode::Night ? gfx::mix(p.sheet, p.ink, 0.14f) : hex(0xFFFAF0);
  box(wrap, 0, 5, cw, ch, gfx::scale(p.sheet, 0.72f), 16);
  lv_obj_t *c = box(wrap, 0, 0, cw, ch, paper, 16);
  lv_obj_set_style_border_width(c, 2, 0);
  lv_obj_set_style_border_color(c, C(p.ink), 0);
  const FeelLook &lk = kLook[pt.feel];
  char word[24];  // in capitals, like the survey's buttons
  size_t wl = 0;
  for (; lk.word[wl] && wl + 1 < sizeof(word); wl++) word[wl] = (char)toupper((unsigned char)lk.word[wl]);
  word[wl] = 0;
  int tw = text_width(word, B14);
  lv_obj_t *chipbox = box(c, 12, 12, tw + 24, 26, hex(lk.face), 13);
  label(chipbox, B14, lk.dark ? hex(0xFBF1DC) : hex(0x3B1D2B), 12, 3, word);

  char when[64], rd[40];
  if (pt.year && pt.weekday < 7 && pt.month >= 1 && pt.month <= 12) {
    int h12 = pt.hour % 12 == 0 ? 12 : pt.hour % 12;
    snprintf(when, sizeof(when), "%s, %s %d   %d:%02d %s", kDay[pt.weekday], kMonth[pt.month - 1], pt.day, h12,
             pt.minute, pt.hour < 12 ? "AM" : "PM");
    if (pt.part <= (uint8_t)DayPart::Evening) {
      size_t n = strlen(when);
      snprintf(when + n, sizeof(when) - n, "   %s", day_part_word((DayPart)pt.part));
    }
  } else {
    snprintf(when, sizeof(when), "time not known");
  }
  snprintf(rd, sizeof(rd), "%d.%d%sF   %d%%", pt.t / 10, abs(pt.t % 10), kDeg, (int)lroundf(pt.h / 10.0f));
  const lv_font_t *wf = text_width(when, B14) <= cw - 24 ? B14 : B13;
  label(c, wf, p.ink, 12, 46, when);
  label(c, B14, p.ink, 12, 64, rd);
  if (can_remove) button(c, 12, 84, cw - 24, 44, hex(0xC4452B), p.cream, "REMOVE", J23, remove_clicked, nullptr);
  mark_dot(idx);
}

// The nearest chosen dot within kPickR pixels of (x, y); -1 if none.
int nearest(int x, int y) {
  int best = -1;
  float bd = (float)kPickR * kPickR;
  for (int i = 0; i < s.n; i++) {
    int dx, dy;
    if (!chosen(s.pts[i]) || !placed(i, dx, dy)) continue;
    if (dx < kPL - kPad || dx > kPR + kPad || dy < kPT - kPad || dy > kPB + kPad) continue;  // not in view
    float d = (float)((dx - x) * (dx - x) + (dy - y) * (dy - y));
    if (d <= bd) {
      bd = d;
      best = i;
    }
  }
  return best;
}

void card_clicked(lv_event_t *) {
  lv_indev_t *in = lv_indev_get_act();
  if (!in) return;
  lv_point_t pt;
  lv_indev_get_point(in, &pt);
  if (s.callout) {  // a tap anywhere else closes the card (Mike, 2026-11-06)
    hide_callout();
    return;
  }
  // While the view is gliding the dots are on the move, so a tap would pick from where one was a
  // moment ago: taps on the plot wait until it has settled (about a second and a half at most).
  if (s.phase == kMove) return;
  if (pt.x < kPL - 12 || pt.x > kPR + 12 || pt.y < kPT - 12 || pt.y > kPB + 12 || s.n == 0) return;
  int i = nearest(pt.x, pt.y);
  if (i >= 0) show_callout(i);
}

// ---- chips, close, tick ---------------------------------------------------------------------

void style_chips() {
  Pal p = pal();
  for (int i = 0; i < kChips; i++) {
    bool on = i == s.chip;
    lv_obj_t *b = s.chip_box[i];
    lv_obj_set_style_bg_color(b, C(on ? p.ink : p.sheet), 0);
    lv_obj_set_style_outline_width(b, on ? 2 : 0, 0);
    lv_obj_set_style_outline_pad(b, 2, 0);
    lv_obj_set_style_outline_color(b, C(p.ink), 0);
    lv_obj_set_style_outline_opa(b, LV_OPA_COVER, 0);
    set_text_color(s.chip_lab[i], on ? p.cream : p.ink);
  }
}

void chip_clicked(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i == s.chip) return;
  s.chip = i;
  hide_callout();  // its dot may not be drawn any more
  style_chips();
  mark_view();     // other dots
  mark_counts();
  // Glide to the dots of this part of the day, with no hold (Mike, 2026-11-18). A chip tapped
  // during the opening hold ends the hold.
  glide_to_dots();
}

void close_clicked(lv_event_t *) { close_sheet(); }

void overlay_clicked(lv_event_t *e) {
  if (lv_event_get_target(e) != g_sheet_root) return;
  if (s.callout) hide_callout();  // the dimmed area closes the callout first, then the chart
  else close_sheet();
}

void stop_tick() {
  if (s.timer) lv_timer_del(s.timer);
  s.timer = nullptr;
}

// Every quarter second: free the points once the sheet is gone (it can be closed from anywhere,
// and close_sheet deletes its objects a moment later), close after 60 s without a touch, follow a
// day/night change, and redraw when the reading now has moved.
void tick(lv_timer_t *) {
  if (g_sheet != Sheet::Chart || !g_sheet_root) {
    stop_tick();
    stop_anim();
    free_points();
    s.card = s.callout = nullptr;
    return;
  }
  if (lv_disp_get_inactive_time(nullptr) > 60000) {
    close_sheet();
    return;
  }
  if (g_mode != s.built) {
    open_comfort_chart();  // same visit, new colours
    return;
  }
  bool ok = s.now_ok;
  int t = s.now_t, h = s.now_h;
  read_now();
  if (ok != s.now_ok || t != s.now_t || h != s.now_h) mark_now();
}

}  // namespace

void open_comfort_chart() {
  bool rebuild = g_sheet == Sheet::Chart && g_sheet_root && s.timer;  // day/night change: same visit
  if (!rebuild) {
    stop_tick();
    stop_anim();
    s.chip = 0;
    s.pick = -1;
    // Every open starts over (Mike, 2026-11-18): the whole chart, a second's hold, then the zoom.
    s.view = s.from = s.to = kChartFull;
    s.t_a = s.t_b = whole_step(kChartTMax - kChartTMin);
    s.g_a = s.g_b = whole_step(kChartGrMax);
    s.rh_a = s.rh_b = chart_rh_step(kChartFull);
    s.fade = s.motion = 0;
    s.phase = kHold;
    s.seen = s.hold_run = false;
    load_points();
  }
  s.callout = nullptr;
  s.built = g_mode;
  read_now();
  lv_obj_t *root = new_sheet(Sheet::Chart, true);
  lv_obj_add_event_cb(root, overlay_clicked, LV_EVENT_CLICKED, nullptr);
  Pal p = pal();
  s.card = box(root, kCardX, kCardY, kCardW, kCardH, p.sheet, 24);
  lv_obj_add_flag(s.card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s.card, draw_card, LV_EVENT_DRAW_MAIN_END, nullptr);
  lv_obj_add_event_cb(s.card, card_clicked, LV_EVENT_SHORT_CLICKED, nullptr);
  s.now_box = now_geo().box;

  label(s.card, J23, p.ink, 20, 29 - J23->line_height / 2, "DOWNSTAIRS COMFORT");
  int x = 28 + text_width("DOWNSTAIRS COMFORT", J23) + 18 - kCardX;
  for (int i = 0; i < kChips; i++) {
    int w = text_width(kChipWord[i], B13) + 18;
    lv_obj_t *b = box(s.card, x, 14, w, 30, p.sheet, 15);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, C(p.ink), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 3);  // the chips are 7 px apart: their areas must not meet
    lv_obj_add_event_cb(b, chip_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    s.chip_box[i] = b;
    s.chip_lab[i] = label(b, B13, p.ink, 0, 0, kChipWord[i]);
    lv_obj_align(s.chip_lab[i], LV_ALIGN_CENTER, 0, 0);
    x += w + 7;
  }
  style_chips();

  // The close X: a ring with a cross.
  lv_obj_t *cb = box(s.card, 756 - 19 - kCardX, 37 - 19 - kCardY, 38, 38, p.sheet, LV_RADIUS_CIRCLE);
  lv_obj_set_style_bg_opa(cb, LV_OPA_TRANSP, 0);  // the cross is drawn with the plot
  lv_obj_set_style_border_width(cb, 3, 0);
  lv_obj_set_style_border_color(cb, C(p.ink), 0);
  lv_obj_add_flag(cb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(cb, 8);
  lv_obj_add_event_cb(cb, close_clicked, LV_EVENT_CLICKED, nullptr);
  if (!s.timer) s.timer = lv_timer_create(tick, 250, nullptr);
  if (!rebuild && !s.anim) s.anim = lv_timer_create(anim_tick, kAnimMs, nullptr);
  if (rebuild && s.pick >= 0 && s.pick < s.n) show_callout(s.pick);
}

// For the simulator's shots and touch tests: the view the plot shows (t0, t1, g0, g1) and whether
// it is at rest (0), on the opening hold (1) or gliding (2).
int comfort_chart_peek(float view[4]) {
  if (view) {
    view[0] = s.view.t0;
    view[1] = s.view.t1;
    view[2] = s.view.g0;
    view[3] = s.view.g1;
  }
  return (int)s.phase;
}

}  // namespace detail
}  // namespace ui
