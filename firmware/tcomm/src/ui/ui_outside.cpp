// The outside history chart (Mike, 2026-11-07; core/outside_log.h). A long press on the Outside tile,
// on either panel, opens a day's outside temperature and humidity on ONE chart with two scales:
// temperature against the left axis, humidity against the right, each line and its labels in its own
// colour. Midnight to midnight, hours still to come left blank. Above each hour sits the forecast
// seen most often that hour (night hours show the moon versions). The arrows by the title step back
// through the days saved on the SD card, and forward again up to today.
//
// Memory: the day's file is read when the chart opens (and again when an arrow is tapped, or once a
// minute while today is shown), parsed into one block of points with their screen positions worked
// out once (PSRAM on the panel), and the file's own buffer is freed at once. The small forecast
// icons (22 px, one per forecast actually needed, about 1.5 KB each) are drawn at load too. All of
// it goes when the sheet closes, however it closes (a tick notices). No picture of the plot is
// kept: it is drawn straight from the points by the card's draw event, and only when something
// changes.
#include <math.h>
#include <stdio.h>
#include "../core/civil.h"
#include "../core/outside_log.h"
#include "../core/sky.h"
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;
using namespace tc;

namespace {

constexpr int kCardX = 8, kCardY = 8, kCardW = 784, kCardH = 464;
// The plot, screen pixels: 00:00 to 24:00 across, the temperature scale up the left, humidity up the
// right. The icon row sits above it (centres kIconCy).
constexpr int kPL = 76, kPR = 716, kPT = 114, kPB = 392;
constexpr int kIconCy = 87, kIcon = 22;
constexpr int kMaxIcons = 18;  // nine forecast codes, day and night
constexpr int kGapMin = 30;    // a gap longer than this is not drawn across
constexpr uint32_t kReloadMs = 60000;
const char kDeg[] = "\xC2\xB0";  // the degree sign
const char *const kMonth[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *const kDay[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const kHourWord[9] = {"12 AM", "3 AM", "6 AM", "9 AM", "12 PM", "3 PM", "6 PM", "9 PM", "12 AM"};

struct State {
  OutsidePoint *pts = nullptr;
  int16_t *xy = nullptr;  // per point: x, temperature y, humidity y (screen), worked out once at load
  int n = 0;
  bool err = false;       // the points could not be stored
  int32_t day = 0;        // the day shown, local days since 1970-01-01
  int32_t today = 0;
  uint32_t loaded_ms = 0;
  int t_lo = 40, t_hi = 70, t_step = 5;  // the temperature axis, whole degrees F
  int hi_t = 0, lo_t = 0;                // the day's extremes, tenths of a degree F
  Art icon[kMaxIcons];
  int icon_key[kMaxIcons] = {};          // forecast * 2 + night
  int nicon = 0;
  int8_t hour_icon[24] = {};             // each hour's icon (index into icon), -1 for none
  lv_obj_t *card = nullptr, *prev = nullptr, *next = nullptr;
  lv_timer_t *timer = nullptr;
  Mode built = Mode::Day;                // the mode the objects were made for
  int ax = 0;                            // the left arrow's left edge, screen x
};
State s;

struct Pal {
  Color sheet, ink, grid, axis, temp, hum, dim, cream;
};
Pal pal() {
  Pal p;
  p.sheet = theme::sheet_bg(g_mode);
  p.ink = ink();
  p.cream = hex(0xFBF1DC);
  if (g_mode == Mode::Night) {
    p.grid = gfx::mix(p.sheet, p.ink, 0.13f);
    p.axis = gfx::mix(p.sheet, p.ink, 0.62f);
    p.dim = gfx::mix(p.sheet, p.ink, 0.3f);
    p.temp = hex(0xFF8F6B);  // light enough for the dark sheet
    p.hum = hex(0x6FB0FF);
    p.cream = p.sheet;
  } else {
    p.grid = hex(0xE4D8C4);
    p.axis = gfx::scale(p.ink, 1.7f);
    p.dim = hex(0xCEC4B2);
    p.temp = hex(0xD2432A);  // warm coral red
    p.hum = hex(0x2A6FCF);   // blue
  }
  return p;
}

// ---- the data -------------------------------------------------------------------------------

int32_t floor_div(int64_t a, int32_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return (int32_t)q;
}

int64_t local_now() { return (int64_t)g_f.wall.utc + g_f.utc_offset; }
int32_t today_now() { return floor_div(local_now(), 86400); }

void free_data() {
  if (s.pts) gfx::release_bytes(s.pts);
  s.pts = nullptr;
  s.xy = nullptr;
  s.n = 0;
  for (int i = 0; i < kMaxIcons; i++) {
    lv_img_cache_invalidate_src(&s.icon[i].dsc);
    s.icon[i].cv.destroy();
  }
  s.nicon = 0;
  for (int h = 0; h < 24; h++) s.hour_icon[h] = -1;
}

// Rounds down or up to a multiple of m.
int floor_to(int v, int m) { return (int)floorf((float)v / m) * m; }
int ceil_to(int v, int m) { return (int)ceilf((float)v / m) * m; }

void fit_axis() {
  if (s.n == 0) {
    s.t_lo = 40;
    s.t_hi = 70;
    s.t_step = 5;
    s.hi_t = s.lo_t = 0;
    return;
  }
  int mn = s.pts[0].t, mx = s.pts[0].t;
  for (int i = 1; i < s.n; i++) {
    if (s.pts[i].t < mn) mn = s.pts[i].t;
    if (s.pts[i].t > mx) mx = s.pts[i].t;
  }
  s.lo_t = mn;
  s.hi_t = mx;
  // Rounded to 5 F, at least 15 F tall (Mike, 2026-11-07).
  int lo = floor_to((int)floorf(mn / 10.0f), 5), hi = ceil_to((int)ceilf(mx / 10.0f), 5);
  for (bool up = true; hi - lo < 15; up = !up) {
    if (up) hi += 5;
    else lo -= 5;
  }
  s.t_step = 5;
  if (hi - lo > 30) {  // a wide day: labels every 10 so they stay apart
    s.t_step = 10;
    lo = floor_to(lo, 10);
    hi = ceil_to(hi, 10);
  }
  s.t_lo = lo;
  s.t_hi = hi;
}

float x_of_minute(float minute) { return kPL + minute / 1440.0f * (kPR - kPL); }
float y_of_temp(float t_f) { return kPB - (t_f - s.t_lo) / (float)(s.t_hi - s.t_lo) * (kPB - kPT); }
float y_of_hum(float h) { return kPB - h / 100.0f * (kPB - kPT); }

int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

void place_all() {
  for (int i = 0; i < s.n; i++) {
    const OutsidePoint &p = s.pts[i];
    s.xy[3 * i] = (int16_t)lroundf(x_of_minute((float)p.minute));
    s.xy[3 * i + 1] = (int16_t)clampi((int)lroundf(y_of_temp(p.t / 10.0f)), kPT, kPB);
    s.xy[3 * i + 2] = (int16_t)clampi((int)lroundf(y_of_hum((float)p.h)), kPT, kPB);
  }
}

// One small icon: the 88 px board art drawn once, then averaged down 4 x 4 into 22 px with alpha
// (the art is a single colour, so only its coverage matters).
bool make_icon(gfx::Canvas &big, int code, bool night, Color ink_c) {
  if (s.nicon >= kMaxIcons) return false;
  Art &a = s.icon[s.nicon];
  if (!a.make(kIcon, kIcon, true)) return false;
  if (!big.ok() && !big.create(88, 88, true)) return false;
  art::forecast(big, (Forecast)code, night, ink_c);
  a.cv.clear(ink_c, 0);
  const int k = 88 / kIcon;
  for (int y = 0; y < kIcon; y++)
    for (int x = 0; x < kIcon; x++) {
      int sum = 0;
      for (int dy = 0; dy < k; dy++)
        for (int dx = 0; dx < k; dx++) {
          uint8_t al = 0;
          big.get(x * k + dx, y * k + dy, &al);
          sum += al;
        }
      int al = sum / (k * k);
      if (al <= 0) continue;
      a.cv.set_opacity(al / 255.0f);
      a.cv.fill_rect((float)x, (float)y, (float)x + 1, (float)y + 1, ink_c);
    }
  a.cv.set_opacity(1.0f);
  lv_img_cache_invalidate_src(&a.dsc);
  s.icon_key[s.nicon] = code * 2 + (night ? 1 : 0);
  return true;
}

// The icons above the hours: that hour's most frequent forecast, the moon version when the hour
// lies between the day's sunset and sunrise.
void make_icons(const Pal &p) {
  uint8_t fc[24];
  outside_hourly_forecast(s.pts, s.n, fc);
  Civil cv = civil_from_seconds((int64_t)s.day * 86400);
  SunTimes st = sun_times(cv.year, cv.month, cv.day, g_f.utc_offset, g_set.latitude, g_set.longitude);
  gfx::Canvas big;
  for (int h = 0; h < 24; h++) {
    s.hour_icon[h] = -1;
    if (fc[h] == 0 || !forecast_valid(fc[h])) continue;
    bool night;
    if (st.ok) {
      int64_t mid = (int64_t)s.day * 86400 + h * 3600 + 1800 - g_f.utc_offset;
      night = !(mid >= st.rise && mid < st.set);
    } else {
      night = h < 6 || h >= 18;  // no real sunrise or sunset that day: 6 AM and 6 PM
    }
    int key = fc[h] * 2 + (night ? 1 : 0), idx = -1;
    for (int i = 0; i < s.nicon; i++)
      if (s.icon_key[i] == key) idx = i;
    if (idx < 0 && make_icon(big, fc[h], night, p.ink)) idx = s.nicon++;
    s.hour_icon[h] = (int8_t)idx;
  }
}

void load_day() {
  free_data();
  s.err = false;
  s.loaded_ms = g_f.mono_ms;
  if (g_act) {
    char name[kOutsideNameLen];
    outside_file_name_day(s.day, name);
    size_t len = 0;
    uint8_t *buf = g_act->read_file(name, len);
    if (buf) {
      int cap = 0;
      for (size_t i = 0; i < len; i++)
        if (buf[i] == '\n') cap++;
      cap++;  // a last line without its newline
      s.pts = (OutsidePoint *)gfx::alloc_bytes((size_t)cap * (sizeof(OutsidePoint) + 3 * sizeof(int16_t)));
      if (s.pts) {
        s.xy = (int16_t *)(s.pts + cap);
        s.n = outside_parse((const char *)buf, len, s.pts, cap);
      } else {
        s.err = true;
      }
      g_act->free_file(buf);  // the points are all the chart keeps
    }
  }
  if (s.n == 0 && s.pts) {
    gfx::release_bytes(s.pts);
    s.pts = nullptr;
    s.xy = nullptr;
  }
  fit_axis();
  if (s.n > 0) {
    place_all();
    make_icons(pal());
  }
  if (s.card) lv_obj_invalidate(s.card);
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

void rrect(lv_draw_ctx_t *dc, int x0, int y0, int x1, int y1, int r, Color fill) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = r;
  d.bg_color = C(fill);
  d.bg_opa = LV_OPA_COVER;
  d.border_width = 0;
  lv_area_t a = area(x0, y0, x1, y1);
  lv_draw_rect(dc, &d, &a);
}

void line(lv_draw_ctx_t *dc, float x0, float y0, float x1, float y1, int w, Color c, bool round_caps = false) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = C(c);
  d.width = w;
  d.opa = LV_OPA_COVER;
  d.round_start = d.round_end = round_caps;
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

// The day's name for the title: "Today", or "Fri, Nov 7".
void day_label(char *out, size_t n) {
  if (s.day == s.today) {
    snprintf(out, n, "Today");
    return;
  }
  Civil c = civil_from_seconds((int64_t)s.day * 86400);
  snprintf(out, n, "%s, %s %d", kDay[c.weekday % 7], kMonth[(c.month - 1) % 12], c.day);
}

bool joined(int i) {  // is point i joined to point i + 1 by a line
  if (i + 1 >= s.n) return false;
  int dm = (int)s.pts[i + 1].minute - (int)s.pts[i].minute;
  return dm > 0 && dm <= kGapMin;
}

// One line of the chart: the y of point i comes from `col` (1 temperature, 2 humidity).
void draw_series(lv_draw_ctx_t *dc, int col, Color c) {
  for (int i = 0; i < s.n; i++) {
    bool to_next = joined(i), from_prev = i > 0 && joined(i - 1);
    int x = s.xy[3 * i], y = s.xy[3 * i + col];
    if (to_next) line(dc, x, y, s.xy[3 * (i + 1)], s.xy[3 * (i + 1) + col], 3, c, true);
    else if (!from_prev) rrect(dc, x - 3, y - 3, x + 3, y + 3, LV_RADIUS_CIRCLE, c);  // a reading on its own
  }
}

void chevron(lv_draw_ctx_t *dc, int cx, int cy, bool left, Color c) {
  int d = left ? 1 : -1;
  line(dc, cx + 5 * d, cy - 10, cx - 5 * d, cy, 3, c, true);
  line(dc, cx - 5 * d, cy, cx + 5 * d, cy + 10, 3, c, true);
}

void draw_card(lv_event_t *e) {
  lv_draw_ctx_t *dc = lv_event_get_draw_ctx(e);
  if (!dc) return;
  Pal p = pal();
  char buf[48];
  // The close X's cross (its ring is an object).
  line(dc, 749, 30, 763, 44, 3, p.ink);
  line(dc, 763, 30, 749, 44, 3, p.ink);
  // The arrows' chevrons (their rings are objects); the forward one is faint on today.
  chevron(dc, s.ax + 24, 37, true, p.ink);
  chevron(dc, s.ax + 24 + 264, 37, false, s.day >= s.today ? p.dim : p.ink);
  day_label(buf, sizeof(buf));
  text(dc, B24, p.ink, s.ax + 56 + 100, 37, buf, kCenter);

  // The latest reading now (today), or the day's high and low.
  if (s.n > 0) {
    char t[48];
    if (s.day == s.today) {
      const OutsidePoint &l = s.pts[s.n - 1];
      snprintf(t, sizeof(t), "Now %d%sF  %d%%", (int)lroundf(l.t / 10.0f), kDeg, l.h);
    } else {
      snprintf(t, sizeof(t), "High %d%sF  Low %d%sF", (int)lroundf(s.hi_t / 10.0f), kDeg, (int)lroundf(s.lo_t / 10.0f), kDeg);
    }
    text(dc, B20, p.ink, 722, 37, t, kRight);
  }

  // The grid: temperature's rungs across, every 3 hours down.
  for (int t = s.t_lo; t <= s.t_hi; t += s.t_step) {
    float y = y_of_temp((float)t);
    line(dc, kPL, y, kPR, y, 1, p.grid);
    line(dc, kPL - 6, y, kPL, y, 2, p.temp);
    snprintf(buf, sizeof(buf), "%d", t);
    text(dc, B14, p.temp, kPL - 10, y, buf, kRight);
  }
  for (int v = 0; v <= 100; v += 20) {
    float y = y_of_hum((float)v);
    line(dc, kPR, y, kPR + 6, y, 2, p.hum);
    snprintf(buf, sizeof(buf), "%d", v);
    text(dc, B14, p.hum, kPR + 10, y, buf, kLeft);
  }
  snprintf(buf, sizeof(buf), "%sF", kDeg);
  text(dc, B13, p.temp, kPL - 10, kPT - 16, buf, kRight);
  text(dc, B13, p.hum, kPR + 10, kPT - 16, "%", kLeft);
  for (int h = 0; h <= 24; h++) {
    float x = x_of_minute(h * 60.0f);
    if (h % 3 == 0) {
      line(dc, x, kPT, x, kPB, 1, p.grid);
      text(dc, B13, p.axis, x, kPB + 17, kHourWord[h / 3], kCenter);
    }
    line(dc, x, kPB, x, kPB + 5, 1, p.axis);
  }
  line(dc, kPL, kPB, kPR, kPB, 2, p.axis);

  // The forecast icons, one per hour with a reading.
  for (int h = 0; h < 24; h++) {
    int ix = s.hour_icon[h];
    if (ix < 0) continue;
    int cx = (int)lroundf(x_of_minute(h * 60.0f + 30.0f));
    lv_draw_img_dsc_t d;
    lv_draw_img_dsc_init(&d);
    d.opa = LV_OPA_COVER;
    lv_area_t a = area(cx - kIcon / 2, kIconCy - kIcon / 2, cx - kIcon / 2 + kIcon - 1, kIconCy - kIcon / 2 + kIcon - 1);
    lv_draw_img(dc, &d, &a, &s.icon[ix].dsc);
  }

  // The lines: humidity behind, temperature on top.
  draw_series(dc, 2, p.hum);
  draw_series(dc, 1, p.temp);
  if (s.n == 0 && !s.err) text(dc, B22, p.axis, (kPL + kPR) / 2.0f, (kPT + kPB) / 2.0f, "No readings saved this day", kCenter);
  if (s.err) text(dc, B22, p.axis, (kPL + kPR) / 2.0f, (kPT + kPB) / 2.0f, "Not enough memory to draw the chart", kCenter);

  // Where the day has got to.
  if (s.day == s.today) {
    int m = (int)((local_now() % 86400 + 86400) % 86400 / 60);
    float x = x_of_minute((float)m);
    for (int y = kPT; y < kPB; y += 12) line(dc, x, y, x, (y + 7 < kPB) ? y + 7 : kPB, 2, p.ink);
  }

  // The legend along the bottom.
  const int ly = 446;
  line(dc, 28, ly, 56, ly, 3, p.temp, true);
  text(dc, B14, p.temp, 64, ly, "Temperature (left)", kLeft);
  int x2 = 64 + text_width("Temperature (left)", B14) + 26;
  line(dc, x2, ly, x2 + 28, ly, 3, p.hum, true);
  text(dc, B14, p.hum, x2 + 36, ly, "Humidity (right)", kLeft);
  int x3 = x2 + 36 + text_width("Humidity (right)", B14) + 26;
  text(dc, B14, p.axis, x3, ly, "Icons: each hour's forecast", kLeft);
}

void redraw() {
  if (s.card) lv_obj_invalidate(s.card);
}

// ---- arrows, close, tick --------------------------------------------------------------------

void style_arrows() {
  Pal p = pal();
  bool can_next = s.day < s.today;
  if (s.prev) {
    lv_obj_set_style_border_color(s.prev, C(p.ink), 0);
  }
  if (s.next) {
    lv_obj_set_style_border_color(s.next, C(can_next ? p.ink : p.dim), 0);
    if (can_next) lv_obj_add_flag(s.next, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_clear_flag(s.next, LV_OBJ_FLAG_CLICKABLE);
  }
}

void step_day(int d) {
  int32_t nd = s.day + d;
  if (nd > s.today) return;
  s.day = nd;
  load_day();
  style_arrows();
}

void prev_clicked(lv_event_t *) { step_day(-1); }
void next_clicked(lv_event_t *) { step_day(1); }
void close_clicked(lv_event_t *) { close_sheet(); }

void overlay_clicked(lv_event_t *e) {
  if (lv_event_get_target(e) == g_sheet_root) close_sheet();
}

void stop_tick() {
  if (s.timer) lv_timer_del(s.timer);
  s.timer = nullptr;
}

// Every quarter second: free the data once the sheet is gone (it can be closed from anywhere, and
// close_sheet deletes its objects a moment later), close after 60 s without a touch, follow a
// day/night change, move today on at midnight, and take in the newest readings while today shows.
void tick(lv_timer_t *) {
  if (g_sheet != Sheet::Outside || !g_sheet_root) {
    stop_tick();
    free_data();
    s.card = s.prev = s.next = nullptr;
    return;
  }
  if (lv_disp_get_inactive_time(nullptr) > 60000) {
    close_sheet();
    return;
  }
  if (g_mode != s.built) {
    open_outside_chart();  // same visit, new colours
    return;
  }
  int32_t t = today_now();
  if (t != s.today) {
    bool was = s.day == s.today;
    s.today = t;
    if (was) s.day = t;
    load_day();
    style_arrows();
  } else if (s.day == s.today && (uint32_t)(g_f.mono_ms - s.loaded_ms) >= kReloadMs) {
    load_day();
  }
}

lv_obj_t *ring_button(lv_obj_t *parent, int sx, int sy, int w, int h, const Pal &p, lv_event_cb_t cb) {
  lv_obj_t *b = box(parent, sx - kCardX, sy - kCardY, w, h, p.sheet, h / 2);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);  // the chevron is drawn with the plot
  lv_obj_set_style_border_width(b, 3, 0);
  lv_obj_set_style_border_color(b, C(p.ink), 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(b, 6);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

}  // namespace

void open_outside_chart() {
  bool rebuild = g_sheet == Sheet::Outside && g_sheet_root && s.timer;  // day/night change: same visit
  if (!rebuild) {
    stop_tick();
    s.today = today_now();
    s.day = s.today;
  }
  s.built = g_mode;
  lv_obj_t *root = new_sheet(Sheet::Outside, true);
  lv_obj_add_event_cb(root, overlay_clicked, LV_EVENT_CLICKED, nullptr);
  Pal p = pal();
  s.card = box(root, kCardX, kCardY, kCardW, kCardH, p.sheet, 24);
  lv_obj_add_flag(s.card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s.card, draw_card, LV_EVENT_DRAW_MAIN_END, nullptr);

  label(s.card, J23, p.ink, 20, 29 - J23->line_height / 2, "OUTSIDE");
  s.ax = 28 + text_width("OUTSIDE", J23) + 22;
  s.prev = ring_button(s.card, s.ax, 20, 48, 34, p, prev_clicked);
  s.next = ring_button(s.card, s.ax + 264, 20, 48, 34, p, next_clicked);
  style_arrows();

  // The close X: a ring with a cross.
  lv_obj_t *cb = box(s.card, 756 - 19 - kCardX, 37 - 19 - kCardY, 38, 38, p.sheet, LV_RADIUS_CIRCLE);
  lv_obj_set_style_bg_opa(cb, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cb, 3, 0);
  lv_obj_set_style_border_color(cb, C(p.ink), 0);
  lv_obj_add_flag(cb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(cb, 8);
  lv_obj_add_event_cb(cb, close_clicked, LV_EVENT_CLICKED, nullptr);

  load_day();
  if (!s.timer) s.timer = lv_timer_create(tick, 250, nullptr);
}

}  // namespace detail
}  // namespace ui
