// The tenant's comfort chart (Mike, 2026-11-06; docs/temperature-requests.md, "The comfort chart"). A
// long press on the downstairs INSIDE area opens it on either panel: every answer the survey ever
// kept, as a small dot in its answer's colour on a psychrometric chart, with the two standard
// comfort zones and a ring for the reading now. Chips pick a part of the day. On the downstairs
// panel a tap near a dot opens a card that can remove the answer (a mistake); upstairs the same
// card only shows it.
//
// Memory: the file is read when the chart opens, parsed into a block of points (PSRAM on the
// panel), and the file's own buffer is freed at once. The points go when the sheet closes, however
// it closes (a tick notices). No picture of the plot is kept: it's drawn straight from the points
// by the card's draw event, and only when something changes (a chip, a pick, a removal, the
// reading).
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

// The plot (the approved mockups'): 55-85 F across, 0-120 gr/lb up.
constexpr int kPL = 64, kPR = 712, kPT = 84, kPB = 404;
constexpr int kCardX = 8, kCardY = 8, kCardW = 784, kCardH = 464;
constexpr int kPickR = 24;      // a tap picks the nearest dot within this many pixels
constexpr int kChips = 5;       // ALL, NIGHT, MORNING, MIDDAY, EVENING
const char kDeg[] = "\xC2\xB0";  // the degree sign

struct State {
  ComfortPoint *pts = nullptr;
  int16_t *xy = nullptr;  // each point's screen x, y (x = kOff when it isn't drawn), worked out once at load
  bool err = false;       // the points could not be stored
  int n = 0;
  int chip = 0;      // 0 = ALL, else a DayPart + 1
  int pick = -1;     // index of the picked point
  lv_obj_t *card = nullptr, *callout = nullptr, *chip_box[kChips] = {}, *chip_lab[kChips] = {};
  lv_timer_t *timer = nullptr;
  Mode built = Mode::Day;  // the mode the objects were made for
  bool now_ok = false;
  int now_t = 0, now_h = 0;  // whole degrees F and percent
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
  s.xy = nullptr;
  s.n = 0;
}

float px_of(float t);
float py_of(float g);

// The dot's place on the plot, once per point at load; kOff when it's outside the chart's range
// (not drawn, not counted) or its answer is not a known one.
void place_all() {
  for (int i = 0; i < s.n; i++) {
    const ComfortPoint &p = s.pts[i];
    float t = p.t / 10.0f, g = grains_per_lb(t, p.h / 10.0f);
    if (!feel_valid(p.feel) || t < kChartTMin || t > kChartTMax || g < 0 || g > kChartGrMax) {
      s.xy[2 * i] = kOff;
      s.xy[2 * i + 1] = 0;
      continue;
    }
    s.xy[2 * i] = (int16_t)lroundf(px_of(t));
    s.xy[2 * i + 1] = (int16_t)lroundf(py_of(g));
  }
}

void load_points() {
  free_points();
  s.err = false;
  if (!g_act) return;
  size_t len = 0;
  uint8_t *buf = g_act->read_file(kComfortFile, len);
  if (!buf) return;  // no file: no answers yet
  int cap = comfort_count_lines((const char *)buf, len);
  if (cap > 0) {
    // One block: the points, then a pair of int16 for each.
    s.pts = (ComfortPoint *)gfx::alloc_bytes((size_t)cap * (sizeof(ComfortPoint) + 2 * sizeof(int16_t)));
    if (s.pts) {
      s.xy = (int16_t *)(s.pts + cap);
      s.n = comfort_parse((const char *)buf, len, s.pts, cap);
      place_all();
    } else {
      s.err = true;
    }
  }
  g_act->free_file(buf);  // the points are all the chart keeps
  if (s.n == 0) free_points();
}

// The dot's place on the plot; false when it isn't drawn.
bool placed(int i, int &x, int &y) {
  if (!s.xy || s.xy[2 * i] == kOff) return false;
  x = s.xy[2 * i];
  y = s.xy[2 * i + 1];
  return true;
}

float px_of(float t) { return kPL + (t - kChartTMin) / (kChartTMax - kChartTMin) * (kPR - kPL); }
float py_of(float g) { return kPB - g / kChartGrMax * (kPB - kPT); }

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

void line(lv_draw_ctx_t *dc, float x0, float y0, float x1, float y1, int w, Color c) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = C(c);
  d.width = w;
  d.opa = LV_OPA_COVER;
  lv_point_t a = {(lv_coord_t)lroundf(x0), (lv_coord_t)lroundf(y0)}, b = {(lv_coord_t)lroundf(x1), (lv_coord_t)lroundf(y1)};
  lv_draw_line(dc, &d, &a, &b);
}

enum Anchor { kLeft, kCenter, kRight };
// Text with its vertical middle at cy; x is its left, middle or right.
void text(lv_draw_ctx_t *dc, const lv_font_t *f, Color c, float x, float cy, const char *t, Anchor an) {
  int w = text_width(t, f), h = f->line_height;
  int x0 = (int)lroundf(x) - (an == kCenter ? w / 2 : an == kRight ? w : 0);
  int y0 = (int)lroundf(cy) - h / 2;
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = f;
  d.color = C(c);
  lv_area_t a = area(x0, y0, x0 + w + 2, y0 + h);
  lv_draw_label(dc, &d, &a, t, nullptr);
}

// One comfort zone (a winter or summer clothing band): flat fill in the strips between the rows,
// then its outline. The fill is solid so each strip is a plain convex shape.
void zone_fill(lv_draw_ctx_t *dc, const Pal &p, bool summer) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = C(p.zone);
  d.bg_opa = LV_OPA_COVER;
  for (int i = 0; i + 1 < kZoneRows; i++) {
    const ZoneRow &a = kComfortZones[i], &b = kComfortZones[i + 1];
    float al = summer ? a.summer_lo : a.winter_lo, ah = summer ? a.summer_hi : a.winter_hi;
    float bl = summer ? b.summer_lo : b.winter_lo, bh = summer ? b.summer_hi : b.winter_hi;
    lv_point_t q[4] = {{(lv_coord_t)lroundf(px_of(al)), (lv_coord_t)lroundf(py_of(a.gr))},
                       {(lv_coord_t)lroundf(px_of(ah)), (lv_coord_t)lroundf(py_of(a.gr))},
                       {(lv_coord_t)lroundf(px_of(bh)), (lv_coord_t)lroundf(py_of(b.gr))},
                       {(lv_coord_t)lroundf(px_of(bl)), (lv_coord_t)lroundf(py_of(b.gr))}};
    lv_draw_polygon(dc, &d, q, 4);
  }
}

// The band both zones cover (the summer zone's cool edge to the winter zone's warm edge), darker.
void zone_overlap(lv_draw_ctx_t *dc, const Pal &p) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = C(p.zone2);
  d.bg_opa = LV_OPA_COVER;
  for (int i = 0; i + 1 < kZoneRows; i++) {
    const ZoneRow &a = kComfortZones[i], &b = kComfortZones[i + 1];
    float al = a.summer_lo, ah = a.winter_hi < al ? al : a.winter_hi;
    float bl = b.summer_lo, bh = b.winter_hi < bl ? bl : b.winter_hi;
    if (ah <= al && bh <= bl) continue;
    lv_point_t q[4] = {{(lv_coord_t)lroundf(px_of(al)), (lv_coord_t)lroundf(py_of(a.gr))},
                       {(lv_coord_t)lroundf(px_of(ah)), (lv_coord_t)lroundf(py_of(a.gr))},
                       {(lv_coord_t)lroundf(px_of(bh)), (lv_coord_t)lroundf(py_of(b.gr))},
                       {(lv_coord_t)lroundf(px_of(bl)), (lv_coord_t)lroundf(py_of(b.gr))}};
    lv_draw_polygon(dc, &d, q, 4);
  }
}

void zone_outline(lv_draw_ctx_t *dc, const Pal &p, bool summer) {
  auto lo = [&](int i) { return summer ? kComfortZones[i].summer_lo : kComfortZones[i].winter_lo; };
  auto hi = [&](int i) { return summer ? kComfortZones[i].summer_hi : kComfortZones[i].winter_hi; };
  for (int i = 0; i + 1 < kZoneRows; i++) {
    line(dc, px_of(lo(i)), py_of(kComfortZones[i].gr), px_of(lo(i + 1)), py_of(kComfortZones[i + 1].gr), 2, p.zone_line);
    line(dc, px_of(hi(i)), py_of(kComfortZones[i].gr), px_of(hi(i + 1)), py_of(kComfortZones[i + 1].gr), 2, p.zone_line);
  }
  int top = kZoneRows - 1;
  line(dc, px_of(lo(top)), py_of(kComfortZones[top].gr), px_of(hi(top)), py_of(kComfortZones[top].gr), 2, p.zone_line);
  line(dc, px_of(lo(0)), py_of(0), px_of(hi(0)), py_of(0), 2, p.zone_line);
}

void rh_curves(lv_draw_ctx_t *dc, const Pal &p) {
  char lab[8];
  for (int k = 1; k <= 5; k++) {
    float rh = k * 20.0f, px0 = 0, py0 = 0, last_t = 0, last_g = 0;
    bool have = false;
    for (float t = kChartTMin; t <= kChartTMax + 0.01f; t += 0.5f) {
      float g = grains_per_lb(t, rh);
      float x = px_of(t), y = py_of(g);
      if (g > kChartGrMax) {  // runs off the top: end it exactly at the top edge
        if (have) {
          float f = (kChartGrMax - last_g) / (g - last_g);
          line(dc, px0, py0, px_of(last_t + 0.5f * f), py_of(kChartGrMax), 2, p.rh);
          last_t += 0.5f * f;
          last_g = kChartGrMax;
        }
        break;
      }
      if (have) line(dc, px0, py0, x, y, 2, p.rh);
      px0 = x;
      py0 = y;
      last_t = t;
      last_g = g;
      have = true;
    }
    if (!have) continue;
    snprintf(lab, sizeof(lab), "%d%%", (int)rh);
    if (last_g > kChartGrMax - 6) text(dc, B12, p.rh_text, px_of(last_t) - 6, kPT + 10, lab, kRight);
    else {
      float tl = kChartTMax - 1.2f;
      text(dc, B12, p.rh_text, px_of(tl), py_of(grains_per_lb(tl, rh)) - 10, lab, kCenter);
    }
  }
}

void draw_now(lv_draw_ctx_t *dc, const Pal &p) {
  if (!s.now_ok || !g_f.view) return;
  float t = (float)s.now_t, g = grains_per_lb(t, (float)s.now_h);
  if (t < kChartTMin || t > kChartTMax || g < 0 || g > kChartGrMax) return;
  const FloorView &fv = g_f.view->floor[(int)Floor::Downstairs];
  char txt[40];
  snprintf(txt, sizeof(txt), "NOW %s  %s", fv.temp, fv.hum);
  int cx = (int)lroundf(px_of(t)), cy = (int)lroundf(py_of(g)), w = text_width(txt, B13);
  // The mockup puts the pill up and to the left; near the left edge it goes to the right.
  bool left = cx - 26 - w - 12 >= kPL - 40;
  int dir = left ? -1 : 1, x0 = left ? cx - 26 - w - 12 : cx + 14, y0 = cy - 36 < kPT - 10 ? cy + 14 : cy - 36;
  bool below = y0 > cy;
  // plum ink at night is the cream ink, with the text on it in the sheet colour
  line(dc, cx + dir * 7, cy + (below ? 7 : -7), cx + dir * 18, cy + (below ? 24 : -24), 2, p.ink);
  rrect(dc, cx - 9, cy - 9, cx + 9, cy + 9, LV_RADIUS_CIRCLE, p.ink, false, p.ink, 3);
  rrect(dc, x0, y0, x0 + w + 12, y0 + 22, 11, p.ink, true, p.ink, 0);
  text(dc, B13, p.cream, x0 + 6 + w / 2.0f, y0 + 11, txt, kCenter);
}

void draw_card(lv_event_t *e) {
  lv_draw_ctx_t *dc = lv_event_get_draw_ctx(e);
  if (!dc) return;
  Pal p = pal();
  // The close X's cross (its ring is an object).
  line(dc, 749, 30, 763, 44, 3, p.ink);
  line(dc, 763, 30, 749, 44, 3, p.ink);
  // The zones first, so the grid and curves lie over them.
  zone_fill(dc, p, false);
  zone_fill(dc, p, true);
  zone_overlap(dc, p);
  char buf[48];
  for (int t = 55; t <= 85; t += 5) {
    float x = px_of((float)t);
    line(dc, x, kPT, x, kPB, 1, p.grid);
    snprintf(buf, sizeof(buf), "%d%sF", t, kDeg);
    text(dc, B13, p.axis, x, kPB + 16, buf, kCenter);
  }
  for (int g = 0; g <= 120; g += 20) {
    float y = py_of((float)g);
    line(dc, kPL, y, kPR, y, 1, p.grid);
    snprintf(buf, sizeof(buf), "%d", g);
    text(dc, B13, p.axis, kPR + 8, y, buf, kLeft);
  }
  text(dc, B12, p.axis, kPR + 8, kPT - 14, "gr/lb", kLeft);
  zone_outline(dc, p, false);
  zone_outline(dc, p, true);
  rh_curves(dc, p);
  text(dc, B11, p.zone_text, px_of(67.4f), py_of(92), "WINTER CLOTHING", kLeft);
  text(dc, B11, p.zone_text, px_of(76.6f), py_of(100), "SUMMER CLOTHING", kLeft);

  // The dots, oldest first so the newest lie on top; the counts come from the same pass.
  int count[kFeelCount] = {}, total = 0;
  lv_area_t clip = dc->clip_area ? *dc->clip_area : area(0, 0, 799, 479);
  for (int i = 0; i < s.n; i++) {
    const ComfortPoint &pt = s.pts[i];
    int cx, cy;
    if (!chosen(pt) || !placed(i, cx, cy)) continue;
    count[pt.feel]++;
    total++;
    // Only the dots inside the area being redrawn cost anything.
    if (cx + 4 < clip.x1 || cx - 4 > clip.x2 || cy + 4 < clip.y1 || cy - 4 > clip.y2) continue;
    rrect(dc, cx - 3, cy - 3, cx + 3, cy + 3, LV_RADIUS_CIRCLE, hex(kLook[pt.feel].face), true, p.ring, 1);
  }
  draw_now(dc, p);
  if (s.pick >= 0 && s.pick < s.n) {
    int cx, cy;
    if (placed(s.pick, cx, cy)) {
      rrect(dc, cx - 10, cy - 10, cx + 10, cy + 10, LV_RADIUS_CIRCLE, p.gold, false, p.gold, 3);
    }
  }

  // The legend along the bottom: each answer's colour, name and count for the chosen part. It
  // has to end before the card's right edge at any count, so it steps down in size and spacing.
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

void redraw() {
  if (s.card) lv_obj_invalidate(s.card);
}

// ---- the callout ----------------------------------------------------------------------------

void hide_callout() {
  if (s.callout) lv_obj_del_async(s.callout);
  s.callout = nullptr;
  if (s.pick >= 0) {
    s.pick = -1;
    redraw();
  }
}

void remove_clicked(lv_event_t *) {
  // The dot goes only if the controller took the removal (it marks the file).
  if (s.pick >= 0 && s.pick < s.n && g_act && g_act->comfort_remove(s.pts[s.pick].n)) s.pts[s.pick].removed = true;
  hide_callout();
  redraw();
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
  redraw();
}

// The nearest chosen dot within kPickR pixels of (x, y); -1 if none.
int nearest(int x, int y) {
  int best = -1;
  float bd = (float)kPickR * kPickR;
  for (int i = 0; i < s.n; i++) {
    int dx, dy;
    if (!chosen(s.pts[i]) || !placed(i, dx, dy)) continue;
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
  redraw();
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
  if (ok != s.now_ok || t != s.now_t || h != s.now_h) redraw();
}

}  // namespace

void open_comfort_chart() {
  bool rebuild = g_sheet == Sheet::Chart && g_sheet_root && s.timer;  // day/night change: same visit
  if (!rebuild) {
    stop_tick();
    s.chip = 0;
    s.pick = -1;
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
  if (rebuild && s.pick >= 0 && s.pick < s.n) show_callout(s.pick);
}

}  // namespace detail
}  // namespace ui
