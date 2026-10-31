// The calendar roll-down (calendar roll-down spec, boards V1 to V7): a tap on the date strip rolls a
// panel down over the tiles with Month, Week and Day views of the household calendar, and an editor
// with an on-screen keyboard. Positions are the spec's, relative to the panel (screen 12, 70) unless
// noted; colours come from theme::cal_colors for the mode.
//
// Taps on objects inside a view never rebuild it directly (that would delete the object whose tap
// LVGL is still handling): rebuild() runs the rebuild from LVGL's async queue instead.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../core/civil.h"
#include "../core/holidays.h"
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;
using tc::Event;
using tc::Floor;

namespace {

constexpr int kCW = 108;                  // column width, month and week
constexpr int kRootY = 62;                // the roll-down's clip region starts under the strip
constexpr int kPanelY = 70 - kRootY;      // the panel's top, screen 70
constexpr int kDlgY = 68 - kRootY;        // the editor dialog's top, screen 68
constexpr int kKbdY = 288 - kRootY;       // the keyboard's top, screen 288
constexpr int kKbdHidden = 480 - kRootY;  // below the screen
constexpr uint32_t kAutoCloseMs = 90000;  // PROPOSED (spec 10)
// The roll and the keyboard's slide (spec: an animation under about 20 fps goes). Measured on the
// panel 2026-10-25: the roll 3 frames in 584 ms (5 fps), the keyboard 2 in 232 ms (8 fps); each
// frame redraws most of the screen. Off, so both appear at once; on again if drawing gets faster.
constexpr bool kAnimate = false;
constexpr int kMaxShown = 64;

enum View : uint8_t { kMonth, kWeek, kDay };
enum Field : uint8_t { kNone, kTitle, kNotes };
enum KeyKind : uint8_t { kChar, kShift, kBack, kSym, kSpace, kDone };

struct Key {
  KeyKind kind;
  char ch;  // kChar: the key's place in its row's letters (row * 16 + column)
  int16_t w;  // LVGL's own width isn't there until its next layout
  lv_obj_t *obj, *lbl;
};

struct Cal {
  bool open = false;
  bool rolling = false;
  bool rebuild_queued = false;
  View view = kMonth;
  int32_t sel = 0, today = 0;
  tc::Mode mode = tc::Mode::Day;
  uint32_t rev = 0;
  lv_obj_t *root = nullptr, *panel = nullptr, *body = nullptr;
  lv_obj_t *title = nullptr, *seg[3] = {}, *seg_lbl[3] = {};
  lv_obj_t *sel_box = nullptr, *add = nullptr;  // the selected day's outline and add button
  // editor
  lv_obj_t *dlg = nullptr, *kbd = nullptr;
  lv_obj_t *f_box[2] = {}, *f_val[2] = {}, *cursor = nullptr;
  lv_obj_t *v_date = nullptr, *v_time = nullptr, *v_len = nullptr, *unit_seg[2] = {}, *unit_lbl[2] = {};
  Key keys[34] = {};
  int nkeys = 0;
  Event draft;
  bool is_new = false;
  // The occurrence being edited: its day, and for a series where it started and what it was.
  int32_t occ_day = 0, orig_start = 0;
  bool orig_repeats = false;
  lv_obj_t *f_name = nullptr, *rep_btn = nullptr, *rep_lbl = nullptr, *rep_icon = nullptr;
  lv_obj_t *picker = nullptr, *prompt = nullptr;  // over the editor
  Field focus = kNone;
  bool shift = false, sym = false;
  int16_t timed_start = 9 * 60;  // kept while the length unit is days, for switching back
  // toast
  lv_obj_t *toast = nullptr;
  lv_timer_t *toast_timer = nullptr;
  uint32_t undo_id = 0;
  int32_t undo_day = 0;  // nonzero: Undo puts back one occurrence of the series undo_id
} g_cal;

// ---- Text placement -----------------------------------------------------------------------------

// The label y for text centred in a CSS line box (top, height) at size px: the content area (Jost
// 1.445 em, Atkinson 1.24 em) sits in the middle of the line box.
int ty(const lv_font_t *f, float px, bool jost, float top, float line_h) {
  return board_y(f, top + (line_h - px * (jost ? 1.445f : 1.24f)) / 2);
}

lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, int px, bool jost, Color col, int x, float top, float line_h,
               const char *t, int ls = 0) {
  lv_obj_t *l = label(parent, f, col, x, ty(f, (float)px, jost, top, line_h), t);
  if (ls) lv_obj_set_style_text_letter_space(l, ls, 0);
  return l;
}

// Centred across [x, x + w).
lv_obj_t *text_c(lv_obj_t *parent, const lv_font_t *f, int px, bool jost, Color col, int x, int w, float top,
                 float line_h, const char *t, int ls = 0) {
  lv_obj_t *l = text(parent, f, px, jost, col, 0, top, line_h, t, ls);
  lv_obj_set_x(l, x + (w - text_width(t, f, ls)) / 2);
  return l;
}

const char *const kEllipsis = "\xE2\x80\xA6";

// One line, cut short with an ellipsis when wider than w (as the boards' CSS does: mid-word, not
// at a space as LVGL's own dots would).
const char *cut(const char *t, const lv_font_t *f, int w, char *buf, size_t cap) {
  if (text_width(t, f) <= w) return t;
  size_t n = strlen(t);
  if (n > cap - 4) n = cap - 4;
  do {
    while (n && ((unsigned char)t[n - 1] & 0xC0) == 0x80) n--;  // a whole character at a time
    if (n) n--;
    size_t k = n;
    while (k && t[k - 1] == ' ') k--;
    memcpy(buf, t, k);
    memcpy(buf + k, kEllipsis, 4);
  } while (n && text_width(buf, f) > w);
  return buf;
}

lv_obj_t *text_cut(lv_obj_t *parent, const lv_font_t *f, int px, Color col, int x, float top, float line_h, int w,
                   const char *t) {
  char buf[208];
  return text(parent, f, px, false, col, x, top, line_h, cut(t, f, w, buf, sizeof(buf)));
}

lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, Color c, lv_opa_t opa, int radius = 0) {
  lv_obj_t *o = box(parent, x, y, w, h, c, radius);
  lv_obj_set_style_bg_opa(o, opa, 0);
  return o;
}

lv_obj_t *outlined(lv_obj_t *parent, int x, int y, int w, int h, Color border, int bw, int radius) {
  lv_obj_t *o = plain(parent, x, y, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_border_width(o, bw, 0);
  lv_obj_set_style_border_color(o, C(border), 0);
  lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
  return o;
}

void on_click(lv_obj_t *o, lv_event_cb_t cb, intptr_t user, int ext = 0) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  if (ext) lv_obj_set_ext_click_area(o, ext);
  lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void *)user);
}

// A pressed look for buttons drawn as outlines: a faint fill of the ink.
void press_tint(lv_obj_t *o, Color ink) {
  lv_obj_set_style_bg_color(o, C(ink), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(o, 40, LV_STATE_PRESSED);
}

// ---- Date and time words ------------------------------------------------------------------------

const char *const kMonths[12] = {"January", "February", "March",     "April",   "May",      "June",
                                 "July",    "August",   "September", "October", "November", "December"};
const char *const kMon3[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *const kDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const kDay3[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const kDayCaps[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *const kDash = " \xE2\x80\x93 ";    // en dash with spaces
const char *const kDot = "  \xC2\xB7  ";        // middle dot with two spaces each side

tc::Civil civ(int32_t day) { return tc::civil_from_seconds((int64_t)day * 86400); }
int32_t day_of(int y, int m, int d) { return (int32_t)tc::days_from_civil(y, m, d); }
int days_in_month(int y, int m) {
  return (int)(tc::days_from_civil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - tc::days_from_civil(y, m, 1));
}
int32_t week_start(int32_t d) { return d - tc::weekday_of(d); }

void upper(char *s) {
  for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

// "6:30 PM"
void time12(char *out, size_t n, int min) {
  int h = (min / 60) % 24, m = min % 60;
  snprintf(out, n, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
}

// "6:30p", "2p": the month view's short time.
void time_short(char *out, size_t n, int min) {
  int h = (min / 60) % 24, m = min % 60;
  if (m) snprintf(out, n, "%d:%02d%c", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? 'a' : 'p');
  else snprintf(out, n, "%d%c", h % 12 == 0 ? 12 : h % 12, h < 12 ? 'a' : 'p');
}

// "3:30 – 4:30 PM", "11:30 AM – 12:30 PM"
void time_range(char *out, size_t n, int a, int b) {
  char ta[16], tb[16];
  time12(ta, sizeof(ta), a);
  time12(tb, sizeof(tb), b % 1440);
  bool same = (a < 720) == ((b % 1440) < 720) && b < 1440;
  if (same) ta[strlen(ta) - 3] = 0;  // drop the first " AM"
  snprintf(out, n, "%s%s%s", ta, kDash, tb);
}

// ---- Graphics, drawn for the mode and kept ------------------------------------------------------

const int kMarkSizes[7] = {18, 20, 22, 24, 26, 30, 52};
Art g_marks[2][7];
Art g_add[3];  // 20, 24 and 52, for this panel's floor
Art g_icon_prev, g_icon_next, g_icon_trash, g_icon_shift, g_icon_back;
Art g_icon_up, g_icon_down, g_icon_repeat, g_icon_repeat_on, g_icon_minus, g_icon_plus;
// Birthday and anniversary markers [kind - 1][floor][size: 18, 22, 26, 30].
const int kKindSizes[4] = {18, 22, 26, 30};
Art g_kmarks[2][2][4];
Art g_pills[40];
int g_npills = 0;
int g_art_mode = -1, g_art_floor = -1;

const theme::CalColors &cc() { return theme::cal_colors(g_cal.mode); }

void marker_colors(bool up, Color &fill, Color &ink, Color &edge) {
  const theme::CalColors &k = cc();
  fill = up ? k.up_fill : k.down_fill;
  ink = up ? k.up_ink : k.down_ink;
  edge = up ? k.up_ink : k.down_edge;
}

void draw_graphics() {
  int floor = (int)g_set.floor;
  if (g_art_mode == (int)g_cal.mode && g_art_floor == floor) return;
  const theme::CalColors &k = cc();
  for (int f = 0; f < 2; f++)
    for (int i = 0; i < 7; i++) {
      int s = kMarkSizes[i];
      if (!g_marks[f][i].make(s, s, true)) continue;
      Color fill, ink, edge;
      marker_colors(f == (int)Floor::Upstairs, fill, ink, edge);
      g_marks[f][i].cv.clear(fill, 0);
      art::cal_marker(g_marks[f][i].cv, f == (int)Floor::Upstairs, 0, 0, (float)s, fill, ink, edge);
    }
  const int adds[3] = {20, 24, 52};
  for (int i = 0; i < 3; i++) {
    if (!g_add[i].make(adds[i] + 14, adds[i] + 14, true)) continue;
    Color fill, ink, edge;
    bool up = g_set.floor == Floor::Upstairs;
    marker_colors(up, fill, ink, edge);
    art::cal_add_button(g_add[i].cv, up, (float)adds[i], fill, ink, edge, k.accent, k.badge_plus);
  }
  if (g_icon_prev.make(18, 18, true)) art::cal_icon(g_icon_prev.cv, art::CalIcon::Prev, k.ink);
  if (g_icon_next.make(18, 18, true)) art::cal_icon(g_icon_next.cv, art::CalIcon::Next, k.ink);
  if (g_icon_trash.make(16, 16, true)) art::cal_icon(g_icon_trash.cv, art::CalIcon::Trash, k.danger);
  if (g_icon_shift.make(22, 22, true)) art::cal_icon(g_icon_shift.cv, art::CalIcon::Shift, k.ink);
  if (g_icon_back.make(26, 22, true)) art::cal_icon(g_icon_back.cv, art::CalIcon::Backspace, k.ink);
  if (g_icon_up.make(18, 18, true)) art::cal_icon(g_icon_up.cv, art::CalIcon::Up, k.ink);
  if (g_icon_down.make(18, 18, true)) art::cal_icon(g_icon_down.cv, art::CalIcon::Down, k.ink);
  if (g_icon_repeat.make(18, 18, true)) art::cal_icon(g_icon_repeat.cv, art::CalIcon::Repeat, k.ink);
  if (g_icon_repeat_on.make(18, 18, true)) art::cal_icon(g_icon_repeat_on.cv, art::CalIcon::Repeat, k.accent_text);
  if (g_icon_minus.make(30, 30, true)) art::cal_icon(g_icon_minus.cv, art::CalIcon::Minus, k.ink);
  if (g_icon_plus.make(30, 30, true)) art::cal_icon(g_icon_plus.cv, art::CalIcon::Plus, k.ink);
  for (int kind = 0; kind < 2; kind++)
    for (int f = 0; f < 2; f++)
      for (int i = 0; i < 4; i++) {
        int sz = kKindSizes[i];
        Art &a = g_kmarks[kind][f][i];
        if (!a.make(sz, sz, true)) continue;
        Color fill, ink, edge;
        marker_colors(f == (int)Floor::Upstairs, fill, ink, edge);
        a.cv.clear(fill, 0);
        art::cal_kind_marker(a.cv, kind ? tc::EventKind::Anniversary : tc::EventKind::Birthday, f == (int)Floor::Upstairs,
                             0, 0, (float)sz, fill, ink, edge);
      }
  g_art_mode = (int)g_cal.mode;
  g_art_floor = floor;
}

lv_obj_t *marker(lv_obj_t *parent, Floor f, int size, int x, int y) {
  int i = 0;
  while (i < 6 && kMarkSizes[i] != size) i++;
  lv_obj_t *img = image(parent, x, y);
  g_marks[(int)f][i].show(img);
  return img;
}

// An event's marker: its floor's, with a cake or rings for a birthday or an anniversary.
lv_obj_t *marker(lv_obj_t *parent, const Event &e, int size, int x, int y) {
  int i = 0;
  while (i < 3 && kKindSizes[i] != size) i++;
  if (e.kind == tc::EventKind::Plain || kKindSizes[i] != size) return marker(parent, e.floor, size, x, y);
  lv_obj_t *img = image(parent, x, y);
  g_kmarks[e.kind == tc::EventKind::Anniversary][(int)e.floor][i].show(img);
  return img;
}

// A bubble (pill) as an image: rounded at the ends where the event starts and ends.
lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, bool up, bool rl, bool rr, float radius) {
  if (g_npills >= (int)(sizeof(g_pills) / sizeof(g_pills[0])) || w < 4) return nullptr;
  Art &a = g_pills[g_npills++];
  if (!a.make(w, h, true)) return nullptr;
  const theme::CalColors &k = cc();
  art::cal_pill(a.cv, up ? k.up_fill : k.down_fill, up ? k.up_ink : k.down_edge, 1.5f, radius, rl, rr);
  lv_obj_t *img = image(parent, x, y);
  a.show(img);
  return img;
}

// ---- Deleting from inside a tap -----------------------------------------------------------------

// The dialog, the keyboard and the toast are deleted after LVGL finishes the tap that closed them.
// Closing the whole calendar deletes them with it at once, so their deletes still waiting are
// cancelled first (or LVGL would delete them a second time).
lv_obj_t *g_dying[4];
int g_ndying = 0;

void dying_cb(void *o) {
  for (int i = 0; i < g_ndying; i++)
    if (g_dying[i] == o) g_dying[i] = g_dying[--g_ndying];
  lv_obj_del((lv_obj_t *)o);
}

void del_later(lv_obj_t *o) {
  show(o, false);
  if (g_ndying == (int)(sizeof(g_dying) / sizeof(g_dying[0]))) dying_cb(g_dying[0]);  // the oldest, now
  g_dying[g_ndying++] = o;
  lv_async_call(dying_cb, o);
}

void del_root() {
  for (int i = 0; i < g_ndying; i++) lv_async_call_cancel(dying_cb, g_dying[i]);
  g_ndying = 0;
  lv_obj_del(g_cal.root);
}

// ---- Toast --------------------------------------------------------------------------------------

void undo_clicked(lv_event_t *);

void toast_hide(lv_timer_t *) {
  if (g_cal.toast) show(g_cal.toast, false);
  g_cal.undo_id = 0;
  g_cal.undo_day = 0;
  if (g_cal.toast_timer) lv_timer_pause(g_cal.toast_timer);
}

void toast(const char *msg, uint32_t undo_id, int32_t undo_day = 0) {
  if (!g_cal.root) return;
  if (g_cal.toast) del_later(g_cal.toast);
  const theme::Palette &pal = theme::palette(g_cal.mode);
  int tw = text_width(msg, B20), w = 44 + tw + (undo_id ? 16 + 96 : 0);
  // At the foot of the screen; with the editor up, over the dialog's last row, clear of the keys.
  // It takes no touches itself (only its Undo does): a tap on it reaches what's under it.
  int y = (g_cal.dlg ? 232 : 418) - kRootY;
  g_cal.toast = box(g_cal.root, 400 - w / 2, y, w, 48, pal.doorbell, 24);
  label(g_cal.toast, B20, pal.cream, 22, 12, msg);
  g_cal.undo_id = undo_id;
  g_cal.undo_day = undo_day;
  if (undo_id) {
    lv_obj_t *b = outlined(g_cal.toast, 22 + tw + 16, 6, 96, 36, pal.cream, 2, 18);
    lv_obj_t *l = label(b, B16, pal.cream, 0, 0, "Undo");
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    press_tint(b, pal.cream);
    on_click(b, undo_clicked, 0, 8);
  }
  // Never a repeat count: a timer whose count runs out deletes itself. This one is paused instead.
  if (!g_cal.toast_timer) g_cal.toast_timer = lv_timer_create(toast_hide, 5000, nullptr);
  lv_timer_reset(g_cal.toast_timer);
  lv_timer_resume(g_cal.toast_timer);
}

// ---- Shared view pieces -------------------------------------------------------------------------

void build_view_now();
void rebuild();
void open_editor(const Event *e, int32_t day, int start_min);

void add_clicked(lv_event_t *) { open_editor(nullptr, g_cal.sel, -1); }

struct Hit {
  uint32_t id;
  int32_t day;
};
Hit g_hits[160];
int g_nhits = 0;

intptr_t hit(const tc::Occ &o) {
  if (g_nhits >= (int)(sizeof(g_hits) / sizeof(g_hits[0]))) return -1;
  g_hits[g_nhits] = Hit{o.e->id, o.day};
  return g_nhits++;
}

void event_clicked(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= g_nhits) return;
  const Event *ev = g_f.events ? g_f.events->find(g_hits[i].id) : nullptr;
  if (ev && !ev->deleted) open_editor(ev, g_hits[i].day, -1);
}

void to_day_view(int32_t d) {
  g_cal.sel = d;
  g_cal.view = kDay;
  rebuild();
}

void more_clicked(lv_event_t *e) { to_day_view((int32_t)(intptr_t)lv_event_get_user_data(e)); }

// The add button for this panel's floor: a marker of size s with its top left at (x, y), a touch area
// of at least 44 x 44 (spec 4, PROPOSED).
lv_obj_t *add_button(lv_obj_t *parent, int s, int x, int y) {
  int i = s == 20 ? 0 : s == 24 ? 1 : 2;
  int hit = s + 14 > 44 ? s + 14 : 44;
  int ox = x + s / 2 - hit / 2, oy = y + s / 2 - hit / 2;
  lv_obj_t *o = plain(parent, ox, oy, hit, hit);
  lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_t *img = image(o, x - 7 - ox, y - 7 - oy);
  g_add[i].show(img);
  on_click(o, add_clicked, 0);
  return o;
}

// -- Month (spec 6) -----------------------------------------------------------------------------

int g_rows = 5;
int32_t g_grid0 = 0;
// A federal holiday's green pill beside a day's numeral (Mike, 2026-10-25); shorter on the
// selected day, where the add button sits.
struct HolCell {
  lv_obj_t *box, *lbl;
  const char *text;
};
HolCell g_hol[42];
int g_hol_sel = -1;

void hol_fit(int cell, bool selected) {
  if (cell < 0 || cell >= 42 || !g_hol[cell].box) return;
  int w = kCW - 27 - (selected ? 35 : 3);
  char buf[48];
  lv_obj_set_width(g_hol[cell].box, w);
  set_text(g_hol[cell].lbl, cut(g_hol[cell].text, B11, w - 8, buf, sizeof(buf)));
}

int row_top(int r) { return 76 + (int)lroundf(r * (315.0f / g_rows)); }

void place_month_selection() {
  int idx = (int)(g_cal.sel - g_grid0), r = idx / 7, c = idx % 7;
  int x = 10 + c * kCW, y = row_top(r), h = row_top(r + 1) - y;
  lv_obj_set_pos(g_cal.sel_box, x + 2, y + 2);
  lv_obj_set_size(g_cal.sel_box, kCW - 3, h - 3);
  hol_fit(g_hol_sel, false);
  g_hol_sel = idx;
  hol_fit(g_hol_sel, true);
  if (g_cal.add) lv_obj_del(g_cal.add);
  g_cal.add = add_button(g_cal.body, 20, x + kCW - 31, y + 5);
}

void month_grid_clicked(lv_event_t *e) {
  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);
  lv_area_t a;
  lv_obj_get_coords(lv_event_get_target(e), &a);
  int c = (p.x - a.x1) / kCW, rel_y = p.y - a.y1, r = 0;
  if (c < 0 || c > 6 || rel_y < 0) return;
  while (r < g_rows - 1 && row_top(r + 1) - 76 <= rel_y) r++;
  int32_t d = g_grid0 + r * 7 + c;
  // A tap on the numeral of the day already selected opens that day (spec 6, PROPOSED).
  bool on_numeral = (p.x - a.x1) - c * kCW < 36 && rel_y - (row_top(r) - 76) < 24;
  if (d == g_cal.sel && on_numeral) {
    to_day_view(d);
    return;
  }
  bool other_month = civ(d).month != civ(g_cal.sel).month;
  g_cal.sel = d;
  if (other_month) rebuild();  // a day of the month before or after: show that month
  else place_month_selection();
}

void build_month(lv_obj_t *body) {
  const theme::CalColors &k = cc();
  tc::Civil c = civ(g_cal.sel);
  int32_t first = day_of(c.year, c.month, 1);
  int dim = days_in_month(c.year, c.month), wd = tc::weekday_of(first);
  g_rows = (wd + dim + 6) / 7;
  if (g_rows < 5) g_rows = 5;
  g_grid0 = first - wd;
  const int lanes = g_rows == 6 ? 1 : 2;  // PROPOSED: six-week months keep one lane
  memset(g_hol, 0, sizeof(g_hol));
  g_hol_sel = -1;

  for (int i = 0; i < 7; i++) text_c(body, B12, 12, false, k.muted, 10 + i * kCW, kCW, 56, 18, kDayCaps[i], 2);
  rect(body, 10, 76, kCW, 315, k.ink, k.weekend_opa);
  rect(body, 10 + 6 * kCW, 76, kCW, 315, k.ink, k.weekend_opa);
  for (int r = 0; r <= g_rows; r++) rect(body, 10, row_top(r), 756, 1, k.ink, k.hair_opa);
  for (int i = 0; i <= 7; i++) rect(body, 10 + i * kCW, 76, 1, 315, k.ink, k.hair_opa);
  lv_obj_t *grid = plain(body, 10, 76, 756, 315);
  on_click(grid, month_grid_clicked, 0);

  char buf[96];
  static tc::Occ evs[kMaxShown];
  for (int r = 0; r < g_rows; r++) {
    int y = row_top(r), h = row_top(r + 1) - y;
    int32_t r0 = g_grid0 + r * 7;
    for (int col = 0; col < 7; col++) {
      int32_t d = r0 + col;
      int x = 10 + col * kCW;
      snprintf(buf, sizeof(buf), "%d", civ(d).day);
      if (d == g_cal.today) {
        lv_obj_t *dot = box(body, x + 4, y + 2, 22, 22, k.ink, LV_RADIUS_CIRCLE);
        lv_obj_t *n = label(dot, B14, k.on_ink, 0, 0, buf);
        lv_obj_set_pos(n, (22 - text_width(buf, B14)) / 2, ty(B14, 14, false, 0, 22));
      } else {
        lv_obj_t *n = text(body, B15, 15, false, k.ink, x + 7, (float)(y + 3), 18, buf);
        if (civ(d).month != c.month) lv_obj_set_style_text_opa(n, 97, 0);  // 38 percent
      }
      if (const tc::Holiday *hd = tc::federal_holiday(d)) {
        HolCell &hc = g_hol[r * 7 + col];
        hc.box = rect(body, x + 27, y + 4, kCW - 30, 17, k.accent_fill, LV_OPA_COVER, 8);
        hc.lbl = text(hc.box, B11, 11, false, k.cream, 4, 0, 17, "");
        hc.text = hd->short_name;
        hol_fit(r * 7 + col, false);
      }
    }
    // Lanes: multi-day bubbles first (earliest first), then single days, each in the first free lane.
    int n = g_f.events ? g_f.events->range(r0, r0 + 6, evs, kMaxShown) : 0;
    bool used[7][2] = {};
    int more[7] = {};
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i < n; i++) {
        const tc::Occ &o = evs[i];
        const Event &e = *o.e;
        if (o.multi_day() != (pass == 0)) continue;
        int c0 = o.day < r0 ? 0 : (int)(o.day - r0), c1 = o.last_day() > r0 + 6 ? 6 : (int)(o.last_day() - r0);
        int lane = -1;
        for (int l = 0; l < lanes && lane < 0; l++) {
          bool free = true;
          for (int q = c0; q <= c1; q++) free = free && !used[q][l];
          if (free) lane = l;
        }
        if (lane < 0) {
          for (int q = c0; q <= c1; q++) more[q]++;
          continue;
        }
        for (int q = c0; q <= c1; q++) used[q][lane] = true;
        int ly = y + 21 + lane * 22;
        bool up = e.floor == Floor::Upstairs;
        if (pass == 0) {
          bool head = o.day >= r0, tail = o.last_day() <= r0 + 6;
          int x0 = 10 + c0 * kCW + (head ? 3 : 0), x1 = 10 + (c1 + 1) * kCW - (tail ? 3 : 0);
          lv_obj_t *p = pill(body, x0, ly, x1 - x0, 18, up, head, tail, 9);
          if (!p) continue;
          on_click(p, event_clicked, hit(o));
          if (head) marker(p, e, 18, 0, 0);
          int tx = head ? 22 : 10;
          text_cut(p, B12, 12, up ? k.up_ink : k.down_ink, tx, 1.5f, 15, x1 - x0 - tx - 5, e.title);
        } else {
          int x = 10 + c0 * kCW;
          lv_obj_t *hitbox = plain(body, x + 3, ly - 2, kCW - 6, 22);
          on_click(hitbox, event_clicked, hit(o));
          marker(body, e, 18, x + 5, ly);
          if (e.all_day) {
            snprintf(buf, sizeof(buf), "%s", e.title);
          } else {
            char t[12];
            time_short(t, sizeof(t), e.start_min);
            snprintf(buf, sizeof(buf), "%s %s", e.title, t);  // the boards add a short time
          }
          text_cut(body, B12, 12, k.ink, x + 27, (float)(ly + 1), 16, 77, buf);
        }
      }
    for (int col = 0; col < 7; col++) {
      if (!more[col]) continue;
      snprintf(buf, sizeof(buf), "+%d", more[col]);
      int x = 10 + (col + 1) * kCW - 6 - text_width(buf, B11);
      lv_obj_t *m = text(body, B11, 11, false, k.ink, x, (float)(y + h - 16), 14, buf);
      on_click(m, more_clicked, (intptr_t)(r0 + col), 10);
    }
  }
  g_cal.sel_box = outlined(body, 0, 0, 10, 10, k.accent, 3, 8);
  place_month_selection();
}

// -- Week (spec 7) ------------------------------------------------------------------------------

void week_header_clicked(lv_event_t *e) {
  int32_t d = (int32_t)(intptr_t)lv_event_get_user_data(e);
  if (d == g_cal.sel) {
    to_day_view(d);  // PROPOSED
    return;
  }
  g_cal.sel = d;
  int c = tc::weekday_of(d);
  lv_obj_set_x(g_cal.sel_box, 10 + c * kCW + 2);
  if (g_cal.add) lv_obj_del(g_cal.add);
  g_cal.add = add_button(g_cal.body, 24, 10 + c * kCW + kCW - 38, 66);
}

void build_week(lv_obj_t *body) {
  const theme::CalColors &k = cc();
  int32_t w0 = week_start(g_cal.sel);
  char buf[96];
  rect(body, 10, 56, kCW, 334, k.ink, k.weekend_opa);
  rect(body, 10 + 6 * kCW, 56, kCW, 334, k.ink, k.weekend_opa);
  for (int i = 0; i <= 7; i++) rect(body, 10 + i * kCW, 56, 1, 334, k.ink, k.hair_opa);
  rect(body, 10, 56, 756, 1, k.ink, k.hair_opa);
  rect(body, 10, 100, 756, 1, k.ink, k.hair_opa);
  for (int c = 0; c < 7; c++) {
    int32_t d = w0 + c;
    int x = 10 + c * kCW;
    text(body, B12, 12, false, k.muted, x + 8, 60, 14, kDayCaps[c], 2);
    snprintf(buf, sizeof(buf), "%d", civ(d).day);
    if (d == g_cal.today) {
      lv_obj_t *p = box(body, x + 6, 73, 28, 26, k.ink, 13);
      lv_obj_t *n = label(p, J21N, k.on_ink, 0, 0, buf);
      lv_obj_set_pos(n, (28 - text_width(buf, J21N)) / 2, ty(J21N, 21, true, 0, 26));
    } else {
      text(body, J22N, 22, true, k.ink, x + 8, 73, 26, buf);
    }
    lv_obj_t *hitbox = plain(body, x, 56, kCW, 44);
    on_click(hitbox, week_header_clicked, (intptr_t)d);
  }
  // The all-day band: a federal holiday first, in its day's top box (Mike, 2026-10-25), then
  // multi-day events, stacked 32 px apart (PROPOSED beyond one), four at most.
  static tc::Occ evs[kMaxShown];
  int n = g_f.events ? g_f.events->range(w0, w0 + 6, evs, kMaxShown) : 0;
  bool used[7][4] = {};
  int lanes = 0;
  for (int c = 0; c < 7; c++) {
    const tc::Holiday *hd = tc::federal_holiday(w0 + c);
    if (!hd) continue;
    used[c][0] = true;
    lanes = 1;
    lv_obj_t *hb = rect(body, 10 + c * kCW + 3, 106, kCW - 6, 26, k.accent_fill, LV_OPA_COVER, 9);
    text_cut(hb, B13, 13, k.cream, 8, 0, 26, kCW - 6 - 14, hd->short_name);
  }
  for (int i = 0; i < n; i++) {
    const tc::Occ &o = evs[i];
    const Event &e = *o.e;
    if (!o.multi_day()) continue;
    int c0 = o.day < w0 ? 0 : (int)(o.day - w0), c1 = o.last_day() > w0 + 6 ? 6 : (int)(o.last_day() - w0);
    int lane = -1;
    for (int l = 0; l < 4 && lane < 0; l++) {
      bool free = true;
      for (int q = c0; q <= c1; q++) free = free && !used[q][l];
      if (free) lane = l;
    }
    if (lane < 0) continue;  // PROPOSED: four lanes at most
    for (int q = c0; q <= c1; q++) used[q][lane] = true;
    if (lane + 1 > lanes) lanes = lane + 1;
    bool head = o.day >= w0, tail = o.last_day() <= w0 + 6, up = e.floor == Floor::Upstairs;
    int x0 = 10 + c0 * kCW + (head ? 3 : 0), x1 = 10 + (c1 + 1) * kCW - (tail ? 3 : 0);
    lv_obj_t *p = pill(body, x0, 106 + lane * 32, x1 - x0, 26, up, head, tail, 9);
    if (!p) continue;
    on_click(p, event_clicked, hit(o));
    if (head) marker(p, e, 26, 0, 0);
    if (e.length <= 7) {
      snprintf(buf, sizeof(buf), "%s%s%s\xE2\x80\x93%s", e.title, kDot, kDay3[tc::weekday_of(o.day)],
               kDay3[tc::weekday_of(o.last_day())]);
    } else {
      tc::Civil a = civ(o.day), b = civ(o.last_day());
      snprintf(buf, sizeof(buf), "%s%s%s %d%s%s %d", e.title, kDot, kMon3[a.month - 1], a.day, kDash, kMon3[b.month - 1], b.day);
    }
    int tx = head ? 33 : 10;
    text_cut(p, B14, 14, up ? k.up_ink : k.down_ink, tx, 0, 26, x1 - x0 - tx - 8, buf);
  }
  int div = 138 + 32 * (lanes > 1 ? lanes - 1 : 0), top = div + 8;
  rect(body, 10, div, 756, 1, k.ink, k.hair_opa);
  // Cards: everything else, a stack per day; a column that overflows scrolls (PROPOSED).
  for (int c = 0; c < 7; c++) {
    int32_t d = w0 + c;
    const tc::Occ *day_evs[24];
    int m = 0;
    for (int i = 0; i < n && m < 24; i++)
      if (!evs[i].multi_day() && evs[i].covers(d)) day_evs[m++] = &evs[i];
    if (!m) continue;
    lv_obj_t *col = body;
    int y0 = top, x = 10 + c * kCW + 4;
    if (top + m * 76 - 6 > 390) {
      col = plain(body, 10 + c * kCW, top, kCW, 390 - top);
      lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
      lv_obj_set_scroll_dir(col, LV_DIR_VER);
      y0 = 0;
      x = 4;
    }
    for (int i = 0; i < m; i++) {
      const tc::Occ &o = *day_evs[i];
      const Event &e = *o.e;
      bool up = e.floor == Floor::Upstairs;
      Color fill = up ? k.up_fill : k.down_fill, edge = up ? k.up_ink : k.down_edge, fg = up ? k.up_ink : k.down_ink;
      lv_obj_t *card = box(col, x, y0 + i * 76, 100, 70, fill, 12);
      lv_obj_set_style_border_width(card, 2, 0);
      lv_obj_set_style_border_color(card, C(edge), 0);
      lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
      on_click(card, event_clicked, hit(o));
      marker(card, e, 22, 3, 3);
      if (e.all_day) snprintf(buf, sizeof(buf), "All day");  // PROPOSED
      else time12(buf, sizeof(buf), e.start_min);
      text(card, B13, 13, false, fg, 30, 3, 15, buf);
      lv_obj_t *t = text(card, B15, 15, false, fg, 6, 30, 17, e.title);
      lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(t, 100 - 8 - 6);
      lv_obj_set_style_text_line_space(t, 17 - B15->line_height, 0);
      lv_obj_set_height(t, 17 + B15->line_height);  // two lines at most; the card clips the rest
    }
  }
  int c = tc::weekday_of(g_cal.sel);
  g_cal.sel_box = outlined(body, 10 + c * kCW + 2, 58, 105, 331, k.accent, 3, 12);
  g_cal.add = add_button(body, 24, 10 + c * kCW + kCW - 38, 66);
}

// -- Day (spec 8) -------------------------------------------------------------------------------

lv_obj_t *g_timeline = nullptr;

void timeline_clicked(lv_event_t *) {
  // An empty hour starts a new event at that hour (spec 8, PROPOSED).
  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);
  lv_area_t a;
  lv_obj_get_coords(g_timeline, &a);
  int y = p.y - a.y1 + lv_obj_get_scroll_y(g_timeline) - 8;
  int hour = y < 0 ? 0 : y / 24;
  if (hour > 23) hour = 23;
  open_editor(nullptr, g_cal.sel, hour * 60);
}

void build_day(lv_obj_t *body) {
  const theme::CalColors &k = cc();
  tc::Civil c = civ(g_cal.sel);
  char buf[200], caps[16];
  lv_obj_t *dbox = rect(body, 10, 56, 130, 334, k.ink, 20, 22);  // 8 percent
  snprintf(caps, sizeof(caps), "%s", kDays[c.weekday]);
  upper(caps);
  text_c(dbox, B14, 14, false, k.muted, 0, 130, 12, 18, caps, 2);
  snprintf(buf, sizeof(buf), "%d", c.day);
  text_c(dbox, J76N, 76, true, k.ink, 0, 130, 32, 80, buf);
  snprintf(caps, sizeof(caps), "%s", kMonths[c.month - 1]);
  upper(caps);
  text_c(dbox, B14, 14, false, k.muted, 0, 130, 116, 18, caps, 2);
  rect(dbox, 14, 150, 102, 1, k.ink, 64);

  static tc::Occ evs[kMaxShown];
  int n = g_f.events ? g_f.events->range(g_cal.sel, g_cal.sel, evs, kMaxShown) : 0;
  int count = 0, first_min = -1;
  const tc::Occ *allday[8];
  int na = 0;
  for (int i = 0; i < n; i++) {
    if (evs[i].e->all_day) {
      if (na < 8) allday[na++] = &evs[i];
      if (!evs[i].multi_day()) count++;  // a trip that started earlier isn't counted (the boards)
    } else {
      count++;
      if (first_min < 0) first_min = evs[i].e->start_min;
    }
  }
  snprintf(buf, sizeof(buf), count == 1 ? "1 event" : "%d events", count);
  text_c(dbox, B14, 14, false, k.ink, 0, 130, 160, 20, buf);
  // A federal holiday, in green under the count (Mike, 2026-10-25).
  if (const tc::Holiday *hd = tc::federal_holiday(g_cal.sel)) {
    lv_obj_t *hb = rect(dbox, 8, 188, 114, 26, k.accent_fill, LV_OPA_COVER, 9);
    const char *name = text_width(hd->name, B13) <= 114 - 12 ? hd->name : hd->short_name;
    char cb[48];
    text_c(hb, B13, 13, false, k.cream, 0, 114, 0, 26, cut(name, B13, 114 - 12, cb, sizeof(cb)));
  }
  add_button(dbox, 52, 39, 248);
  text_c(dbox, B12, 12, false, k.accent_text, 0, 130, 314, 18, "ADD EVENT", 1);

  // The all-day row: the first all-day event covering the day, and how many more.
  if (na) {
    const tc::Occ &o = *allday[0];
    const Event &e = *o.e;
    bool up = e.floor == Floor::Upstairs;
    lv_obj_t *p = pill(body, 152, 56, 614, 30, up, true, true, 9);
    if (p) {
      on_click(p, event_clicked, hit(o));
      marker(p, e, 30, 0, 0);
      int len;
      if (e.multi_day())
        len = snprintf(buf, sizeof(buf), "%s%sday %d of %d", e.title, kDot, (int)(g_cal.sel - o.day) + 1, e.length);
      else
        len = snprintf(buf, sizeof(buf), "%s%sall day", e.title, kDot);
      if (na > 1 && len > 0 && len < (int)sizeof(buf)) snprintf(buf + len, sizeof(buf) - len, "   +%d more", na - 1);
      text_cut(p, B15, 15, up ? k.up_ink : k.down_ink, 39, 0, 30, 614 - 39 - 12, buf);
    }
  }
  // The timeline: every hour, 24 px each, scrolled so 9 AM sits at panel y 94.
  g_timeline = plain(body, 152, 86, 614, 306);
  lv_obj_add_flag(g_timeline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(g_timeline, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(g_timeline, LV_DIR_VER);
  on_click(g_timeline, timeline_clicked, 0);
  for (int h = 0; h <= 24; h++) {
    int y = 8 + h * 24;
    snprintf(buf, sizeof(buf), "%d %s", h % 12 == 0 ? 12 : h % 12, (h % 24) < 12 ? "AM" : "PM");
    lv_obj_t *l = text(g_timeline, B12, 12, false, k.muted, 0, (float)(y - 7), 14, buf);
    lv_obj_set_x(l, 48 - text_width(buf, B12));
    rect(g_timeline, 56, y, 558, 1, k.ink, k.hair_opa);
  }
  rect(g_timeline, 0, 8 + 24 * 24 + 1, 1, 8, k.ink, LV_OPA_TRANSP);  // room below midnight
  // Events that overlap go side by side: each takes the first column free when it starts, and a
  // run of events overlapping one another shares the width between the columns it needed. An
  // event shows at least an hour tall, so that's its extent here.
  static int8_t col[kMaxShown], ncol[kMaxShown];
  {
    int run = 0, cols = 0, run_end = -1, col_end[4] = {};
    auto close_run = [&](int upto) {
      for (int j = run; j < upto; j++) ncol[j] = (int8_t)(cols ? cols : 1);
    };
    for (int i = 0; i < n; i++) {
      const Event &e = *evs[i].e;
      col[i] = 0;
      if (e.all_day) continue;
      int a = e.start_min, z = e.end_min() > a + 60 ? e.end_min() : a + 60;
      if (a >= run_end) {
        close_run(i);
        run = i;
        cols = 0;
        for (int &c : col_end) c = -1;
      }
      int c = 0;
      while (c < 3 && col_end[c] > a) c++;
      col[i] = (int8_t)c;
      col_end[c] = z;
      if (c + 1 > cols) cols = c + 1;
      if (z > run_end) run_end = z;
    }
    close_run(n);
  }
  for (int i = 0; i < n; i++) {
    const Event &e = *evs[i].e;
    if (e.all_day) continue;
    bool up = e.floor == Floor::Upstairs;
    Color fill = up ? k.up_fill : k.down_fill, edge = up ? k.up_ink : k.down_edge, fg = up ? k.up_ink : k.down_ink;
    int dur = e.end_min() - e.start_min, hh = dur * 24 / 60;
    int top = 8 + e.start_min * 24 / 60 + 1, h = (hh > 24 ? hh : 24) - 1;
    const int bw = 554 / ncol[i] - (ncol[i] > 1 ? 3 : 0), bx = 60 + col[i] * (554 / ncol[i]);
    lv_obj_t *b = box(g_timeline, bx, top, bw, h, fill, 11);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, C(edge), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
    on_click(b, event_clicked, hit(evs[i]));
    char range[40];
    time_range(range, sizeof(range), e.start_min, e.end_min());
    if (e.notes[0]) snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", range, e.notes);
    else snprintf(buf, sizeof(buf), "%s", range);
    const int room = bw - 4 - 28 - 8;  // inside the border, after the marker
    if (h < 30) {
      marker(b, e, 18, 2, (h - 4 - 18) / 2);
      int cap = room * 6 / 10, tw = text_width(e.title, B14);
      if (tw > cap) tw = cap;
      text_cut(b, B14, 14, fg, 28, 0, (float)(h - 4), cap, e.title);
      if (room - tw - 10 > 40) text_cut(b, B13, 13, fg, 28 + tw + 10, 0, (float)(h - 4), room - tw - 10, buf);
    } else {
      marker(b, e, 18, 2, 3);
      text_cut(b, B14, 14, fg, 28, 3, 17, room, e.title);
      text_cut(b, B13, 13, fg, 28, 20, 15, room, buf);
    }
  }
  // Today: a line at the current time (PROPOSED).
  int now_min = -1;
  if (g_cal.sel == g_cal.today && g_f.wall.valid) now_min = tc::minutes_of_day((int64_t)g_f.wall.utc + g_f.utc_offset);
  if (now_min >= 0) rect(g_timeline, 56, 8 + now_min * 24 / 60 - 1, 558, 2, k.danger, LV_OPA_COVER);
  // Scroll: 9 AM at the top, unless the first event or the current hour would be out of sight.
  int want = 9 * 60;
  if (first_min >= 0 && (first_min < 9 * 60 || first_min > 20 * 60)) want = first_min - 60;
  if (now_min >= 0 && (now_min < 9 * 60 || now_min > 20 * 60)) want = now_min - 60;
  if (want < 0) want = 0;
  lv_obj_update_layout(g_timeline);
  lv_obj_scroll_to_y(g_timeline, want * 24 / 60, LV_ANIM_OFF);
}

// ---- Toolbar and the panel ----------------------------------------------------------------------

// The toolbar's title, in the longest form that fits before the Today button: "Wednesday, September
// 30" can't, so it becomes "Wednesday, Sep 30"; a week across the new year drops its first year.
void set_title() {
  tc::Civil c = civ(g_cal.sel);
  const int room = 438 - 110 - 12;
  char buf[3][64];
  int n = 0;
  if (g_cal.view == kMonth) {
    snprintf(buf[n++], 64, "%s %d", kMonths[c.month - 1], c.year);
  } else if (g_cal.view == kWeek) {
    int32_t w0 = week_start(g_cal.sel);
    tc::Civil a = civ(w0), b = civ(w0 + 6);
    if (a.year != b.year)
      snprintf(buf[n++], 64, "%s %d, %d%s%s %d, %d", kMon3[a.month - 1], a.day, a.year, kDash, kMon3[b.month - 1], b.day,
               b.year);
    if (a.month != b.month)
      snprintf(buf[n++], 64, "%s %d%s%s %d, %d", kMon3[a.month - 1], a.day, kDash, kMon3[b.month - 1], b.day, b.year);
    else
      snprintf(buf[n++], 64, "%s %d%s%d, %d", kMon3[a.month - 1], a.day, kDash, b.day, b.year);
  } else {
    snprintf(buf[n++], 64, "%s, %s %d", kDays[c.weekday], kMonths[c.month - 1], c.day);
    snprintf(buf[n++], 64, "%s, %s %d", kDays[c.weekday], kMon3[c.month - 1], c.day);
    snprintf(buf[n++], 64, "%s, %s %d", kDay3[c.weekday], kMon3[c.month - 1], c.day);
  }
  int i = 0;
  while (i < n - 1 && text_width(buf[i], J27) > room) i++;
  set_text(g_cal.title, buf[i]);
}

void set_segments() {
  const theme::CalColors &k = cc();
  for (int i = 0; i < 3; i++) {
    bool on = i == (int)g_cal.view;
    lv_obj_set_style_bg_opa(g_cal.seg[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    set_text_color(g_cal.seg_lbl[i], on ? k.on_ink : k.ink);
  }
}

void build_view_now() {
  g_cal.rebuild_queued = false;
  if (!g_cal.panel) return;
  if (g_cal.body) lv_obj_del(g_cal.body);
  g_npills = 0;
  g_nhits = 0;
  g_cal.add = nullptr;
  g_cal.sel_box = nullptr;
  g_timeline = nullptr;
  g_cal.body = plain(g_cal.panel, 0, 0, 776, 402);
  if (g_cal.view == kMonth) build_month(g_cal.body);
  else if (g_cal.view == kWeek) build_week(g_cal.body);
  else build_day(g_cal.body);
  set_title();
  set_segments();
  if (g_f.events) g_cal.rev = g_f.events->rev();
}

void rebuild_async(void *) {
  if (g_cal.rebuild_queued) build_view_now();
}

void rebuild() {
  if (g_cal.rebuild_queued) return;
  g_cal.rebuild_queued = true;
  lv_async_call(rebuild_async, nullptr);
}

void step(int dir) {
  if (g_cal.view == kDay) {
    g_cal.sel += dir;
  } else if (g_cal.view == kWeek) {
    g_cal.sel += 7 * dir;
  } else {
    tc::Civil c = civ(g_cal.sel);
    int m = c.month + dir, y = c.year;
    if (m < 1) m = 12, y--;
    if (m > 12) m = 1, y++;
    int d = c.day > days_in_month(y, m) ? days_in_month(y, m) : c.day;
    g_cal.sel = day_of(y, m, d);
  }
  rebuild();
}

void prev_clicked(lv_event_t *) { step(-1); }
void next_clicked(lv_event_t *) { step(1); }
void today_clicked(lv_event_t *) {
  g_cal.sel = g_cal.today;
  rebuild();
}
void seg_clicked(lv_event_t *e) {
  View v = (View)(intptr_t)lv_event_get_user_data(e);
  if (v == g_cal.view) return;
  g_cal.view = v;
  rebuild();
}

lv_obj_t *round_button(lv_obj_t *parent, int x, Art &icon, lv_event_cb_t cb) {
  const theme::CalColors &k = cc();
  lv_obj_t *b = outlined(parent, x, 10, 40, 40, k.ink, 2, LV_RADIUS_CIRCLE);
  lv_obj_t *img = image(b, 9, 9);
  icon.show(img);
  press_tint(b, k.ink);
  on_click(b, cb, 0, 4);
  return b;
}

void build_panel() {
  const theme::CalColors &k = cc();
  draw_graphics();
  if (g_cal.panel) lv_obj_del(g_cal.panel);
  g_cal.body = nullptr;
  g_cal.panel = box(g_cal.root, 12, kPanelY, 776, 402, k.paper, 28);
  lv_obj_set_style_bg_opa(g_cal.panel, 230, 0);         // 90 percent: the sky shows through
  lv_obj_add_flag(g_cal.panel, LV_OBJ_FLAG_CLICKABLE);  // no touch reaches the tiles under it
  round_button(g_cal.panel, 10, g_icon_prev, prev_clicked);
  round_button(g_cal.panel, 58, g_icon_next, next_clicked);
  g_cal.title = text(g_cal.panel, J27, 27, true, k.ink, 110, 10, 40, "");
  lv_obj_t *today = outlined(g_cal.panel, 438, 12, 84, 36, k.ink, 2, 18);
  text_c(today, B15, 15, false, k.ink, -2, 84, -2, 36, "Today");  // inside the 2 px border
  press_tint(today, k.ink);
  on_click(today, today_clicked, 0, 6);
  lv_obj_t *track = rect(g_cal.panel, 532, 12, 234, 36, k.ink, 23, 18);  // 9 percent
  const char *names[3] = {"Month", "Week", "Day"};
  for (int i = 0; i < 3; i++) {
    g_cal.seg[i] = box(track, i * 78, 0, 78, 36, k.ink, 18);
    g_cal.seg_lbl[i] = text_c(g_cal.seg[i], B15, 15, false, k.ink, 0, 78, 0, 36, names[i]);
    on_click(g_cal.seg[i], seg_clicked, i, 6);
  }
  build_view_now();
}

// ---- Animations, counted -----------------------------------------------------------------------

AnimReport g_report = {0, "", 0, 0};
struct Counting {
  const char *what;
  uint32_t start;
  int frames;
} g_count_roll, g_count_kbd;

void count_start(Counting &c, const char *what) {
  c.what = what;
  c.start = lv_tick_get();
  c.frames = 0;
}

void count_done(Counting &c) { report_anim(c.what, c.frames, lv_tick_elaps(c.start)); }

void roll_exec(void *o, int32_t v) {
  g_count_roll.frames++;
  set_y(o, v);
}

void kbd_exec(void *o, int32_t v) {
  g_count_kbd.frames++;
  set_y(o, v);
}

void kbd_done(lv_anim_t *) { count_done(g_count_kbd); }

void roll_done(lv_anim_t *a) {
  if (a) count_done(g_count_roll);
  g_cal.rolling = false;
  if (g_cal.open) {
    main_tiles_visible(false);  // fully covered: the sky alone shows through, as on the boards
  } else if (g_cal.root) {
    del_root();
    g_cal.root = g_cal.panel = g_cal.body = g_cal.toast = nullptr;
    g_cal.dlg = g_cal.kbd = nullptr;
    for (Art &a : g_pills) a.cv.destroy();  // the bubbles' PSRAM back while it's rolled up
  }
}

void roll(int32_t from, int32_t to, uint32_t ms, lv_anim_path_cb_t path) {
  g_cal.rolling = true;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g_cal.panel);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, ms);
  lv_anim_set_exec_cb(&a, roll_exec);
  lv_anim_set_path_cb(&a, path);
  lv_anim_set_ready_cb(&a, roll_done);
  count_start(g_count_roll, to > from ? "calendar roll down" : "calendar roll up");
  lv_anim_start(&a);
}

// ---- Editor and keyboard (spec 9) ---------------------------------------------------------------

const char *const kRowsAbc[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
const char *const kRowsSym[3] = {"1234567890", "-/:;()&@'", "+=?!#%*"};

char *field_buf(Field f) { return f == kTitle ? g_cal.draft.title : g_cal.draft.notes; }
size_t field_max(Field f) { return f == kTitle ? tc::kEventTitleMax : tc::kEventNotesMax; }

// The repeat bubble's words: "Repeating Event" when it doesn't, else the rule ("Weekly, until
// Dec 31", "Monthly, 10 times", "Birthday, every year").
void rep_summary(const Event &e, char *out, size_t n) {
  if (e.kind == tc::EventKind::Birthday) {
    snprintf(out, n, "Birthday, every year");
    return;
  }
  if (e.kind == tc::EventKind::Anniversary) {
    snprintf(out, n, "Anniversary, every year");
    return;
  }
  static const char *const names[5] = {"Repeating Event", "Daily", "Weekly", "Monthly", "Yearly"};
  int len = snprintf(out, n, "%s", names[(int)e.repeat]);
  if (!e.repeats() || len < 0 || (size_t)len >= n) return;
  if (e.until) {
    tc::Civil u = civ(e.until);
    snprintf(out + len, n - len, ", until %s %d", kMon3[u.month - 1], u.day);
  } else if (e.count) {
    snprintf(out + len, n - len, ", %d times", e.count);
  }
}

// The end of a value too wide for its field: an ellipsis and as much of the end as fits.
void fit_tail(const char *t, const lv_font_t *f, int w, char *out, size_t n) {
  if (text_width(t, f) <= w) {
    snprintf(out, n, "%s", t);
    return;
  }
  for (size_t i = 0, len = strlen(t); i < len; i++) {
    snprintf(out, n, "%s%s", kEllipsis, t + i);
    if (text_width(out, f) <= w) return;
  }
  snprintf(out, n, "%s", kEllipsis);
}

void refresh_fields() {
  if (!g_cal.dlg) return;
  const theme::CalColors &k = cc();
  char buf[160];
  for (int i = 0; i < 2; i++) {
    Field f = i == 0 ? kTitle : kNotes;
    bool on = g_cal.focus == f;
    lv_obj_set_style_border_width(g_cal.f_box[i], on ? 3 : 2, 0);
    lv_obj_set_style_border_color(g_cal.f_box[i], C(on ? k.accent : k.ink), 0);
    if (on) {
      fit_tail(field_buf(f), B19, 668 - 6, buf, sizeof(buf));
      lv_label_set_long_mode(g_cal.f_val[i], LV_LABEL_LONG_CLIP);
    } else {
      snprintf(buf, sizeof(buf), "%s", field_buf(f));
      lv_label_set_long_mode(g_cal.f_val[i], LV_LABEL_LONG_DOT);
    }
    lv_obj_set_width(g_cal.f_val[i], 668);
    set_text(g_cal.f_val[i], buf);
    if (on) {
      lv_obj_set_parent(g_cal.cursor, g_cal.f_box[i]);
      lv_obj_set_pos(g_cal.cursor, 74 + text_width(buf, B19) + 2, 5);
    }
  }
  show(g_cal.cursor, g_cal.focus != kNone);
  const Event &e = g_cal.draft;
  set_text(g_cal.f_name, e.kind != tc::EventKind::Plain ? "NAME" : "TITLE");  // a birthday's is a name
  if (g_cal.rep_btn) {
    bool on = e.repeats() || e.kind != tc::EventKind::Plain;
    char rb[48], cb[48];
    rep_summary(e, rb, sizeof(rb));
    lv_obj_set_style_border_color(g_cal.rep_btn, C(on ? k.accent : k.ink), 0);
    set_text_color(g_cal.rep_lbl, on ? k.accent_text : k.ink);
    set_text(g_cal.rep_lbl, cut(rb, B15, 200 - 4 - 34 - 10, cb, sizeof(cb)));
    (on ? g_icon_repeat_on : g_icon_repeat).show(g_cal.rep_icon);
  }
  tc::Civil c = civ(e.day);
  snprintf(buf, sizeof(buf), "%s, %s %d", kDay3[c.weekday], kMon3[c.month - 1], c.day);
  set_text(g_cal.v_date, buf);
  if (e.all_day) snprintf(buf, sizeof(buf), "All day");  // PROPOSED, greyed
  else time12(buf, sizeof(buf), e.start_min);
  set_text(g_cal.v_time, buf);
  set_text_color(g_cal.v_time, e.all_day ? k.muted : k.ink);
  if (e.all_day) snprintf(buf, sizeof(buf), "%d", e.length);
  else if (e.length % 60 == 0) snprintf(buf, sizeof(buf), "%d", e.length / 60);
  else snprintf(buf, sizeof(buf), "%.1f", e.length / 60.0f);
  set_text(g_cal.v_len, buf);
  for (int i = 0; i < 2; i++) {
    bool on = (i == 1) == e.all_day;
    lv_obj_set_style_bg_opa(g_cal.unit_seg[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    set_text_color(g_cal.unit_lbl[i], on ? k.on_ink : k.ink);
  }
}

// A character key's character: its place is row * 16 + column; row 3 holds the comma and the
// period, the same in both layouts.
char key_char(const Key &key) {
  if (key.ch / 16 == 3) return key.ch % 16 == 0 ? ',' : '.';
  return (g_cal.sym ? kRowsSym : kRowsAbc)[key.ch / 16][key.ch % 16];
}

void refresh_keys() {
  const theme::CalColors &k = cc();
  for (int i = 0; i < g_cal.nkeys; i++) {
    Key &key = g_cal.keys[i];
    if (key.kind == kChar) {
      char t[2] = {key_char(key), 0};
      if (!g_cal.sym && g_cal.shift) t[0] = (char)toupper((unsigned char)t[0]);
      set_text(key.lbl, t);
      lv_obj_set_x(key.lbl, (key.w - text_width(t, B21)) / 2);
    } else if (key.kind == kSym) {
      const char *t = g_cal.sym ? "ABC" : "?123";
      set_text(key.lbl, t);
      lv_obj_set_x(key.lbl, (key.w - text_width(t, B21)) / 2);
    } else if (key.kind == kShift) {
      // Shift on: the letter key's light face, so it reads as pressed in.
      Color face = g_cal.shift && !g_cal.sym ? k.key : k.key_special;
      lv_obj_set_style_bg_color(key.obj, C(face), 0);
      lv_obj_set_style_bg_color(key.obj, C(gfx::scale(face, 0.88f)), LV_STATE_PRESSED);
    }
  }
}

void set_focus(Field f) {
  g_cal.focus = f;
  if (f != kNone) g_cal.shift = field_buf(f)[0] == 0 && f == kTitle;  // the title starts with a capital
  if (g_cal.kbd) {
    int32_t to = f != kNone ? kKbdY : kKbdHidden, from = lv_obj_get_y(g_cal.kbd);
    if (from != to && kAnimate) {
      lv_anim_del(g_cal.kbd, kbd_exec);
      count_start(g_count_kbd, f != kNone ? "calendar keyboard up" : "calendar keyboard down");
      anim(g_cal.kbd, from, to, f != kNone ? 220 : 180, kbd_exec, kbd_done);
    } else if (from != to) {
      lv_obj_set_y(g_cal.kbd, to);
    }
  }
  refresh_fields();
  refresh_keys();
}

void type_char(char ch) {
  if (g_cal.focus == kNone) return;
  char *b = field_buf(g_cal.focus);
  size_t n = strlen(b);
  if (n >= field_max(g_cal.focus)) return;
  if (g_cal.shift && !g_cal.sym) ch = (char)toupper((unsigned char)ch);
  b[n] = ch;
  b[n + 1] = 0;
  if (g_cal.shift && isalpha((unsigned char)ch)) g_cal.shift = false;  // one letter at a time
  refresh_fields();
  refresh_keys();
}

void key_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  Key &key = g_cal.keys[(intptr_t)lv_event_get_user_data(e)];
  if (code == LV_EVENT_LONG_PRESSED_REPEAT && key.kind != kBack) return;
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
  switch (key.kind) {
    case kChar: type_char(key_char(key)); break;
    case kSpace: type_char(' '); break;
    case kShift:
      if (!g_cal.sym) g_cal.shift = !g_cal.shift;
      refresh_keys();
      break;
    case kSym:
      g_cal.sym = !g_cal.sym;
      refresh_keys();
      break;
    case kBack: {
      if (g_cal.focus == kNone) break;
      char *b = field_buf(g_cal.focus);
      size_t n = strlen(b);
      if (n) b[n - 1] = 0;
      refresh_fields();
      break;
    }
    case kDone: set_focus(kNone); break;  // the keyboard goes; the dialog stays
  }
}

void add_key(lv_obj_t *kbd, float x, int row, float w, KeyKind kind, int ch, Color face, Color ink) {
  if (g_cal.nkeys >= (int)(sizeof(g_cal.keys) / sizeof(g_cal.keys[0]))) return;
  static const int kTops[4] = {8, 53, 98, 143};
  int xi = (int)lroundf(x), wi = (int)lroundf(x + w) - xi;
  lv_obj_t *b = box(kbd, xi, kTops[row], wi, 40, face, 9);
  lv_obj_set_style_bg_color(b, C(gfx::scale(face, 0.88f)), LV_STATE_PRESSED);  // about 12 percent darker
  int idx = g_cal.nkeys++;
  Key &key = g_cal.keys[idx];
  key = Key{kind, (char)ch, (int16_t)wi, b, nullptr};
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, key_event, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
  if (kind == kBack) lv_obj_add_event_cb(b, key_event, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)idx);
  switch (kind) {
    case kShift: {
      lv_obj_t *img = image(b, (wi - 22) / 2, 9);
      g_icon_shift.show(img);
      break;
    }
    case kBack: {
      lv_obj_t *img = image(b, (wi - 26) / 2, 9);
      g_icon_back.show(img);
      break;
    }
    case kSpace: text_c(b, B14, 14, false, cc().muted, 0, wi, 0, 40, "SPACE", 2); break;
    case kDone: key.lbl = text_c(b, B21, 21, false, ink, 0, wi, 0, 40, "Done"); break;
    default: key.lbl = text(b, B21, 21, false, ink, 0, 0, 40, "?"); break;
  }
}

void build_keyboard(int32_t y) {
  const theme::CalColors &k = cc();
  // 24 px taller than the 192 it shows: its lower corners, rounded like the upper ones, stay off screen.
  g_cal.kbd = rect(g_cal.root, 0, y, 800, 192 + 24, k.kbd_bg, k.kbd_opa, 24);
  lv_obj_add_flag(g_cal.kbd, LV_OBJ_FLAG_CLICKABLE);  // a tap between keys goes nowhere
  g_cal.nkeys = 0;
  const float kw = 73.9f, gap = 5;
  for (int i = 0; i < 10; i++) add_key(g_cal.kbd, 8 + i * (kw + gap), 0, kw, kChar, 0 * 16 + i, k.key, k.ink);
  for (int i = 0; i < 9; i++) add_key(g_cal.kbd, 48 + i * (kw + gap), 1, kw, kChar, 1 * 16 + i, k.key, k.ink);
  add_key(g_cal.kbd, 8, 2, 1.5f * kw, kShift, 0, k.key_special, k.ink);
  for (int i = 0; i < 7; i++) add_key(g_cal.kbd, 8 + 1.5f * kw + gap + i * (kw + gap), 2, kw, kChar, 2 * 16 + i, k.key, k.ink);
  add_key(g_cal.kbd, 800 - 8 - 1.5f * kw, 2, 1.5f * kw, kBack, 0, k.key_special, k.ink);
  // Row 4: ?123, comma, space (whatever is left), period, Done (2.4 keys wide).
  float x = 8;
  add_key(g_cal.kbd, x, 3, 1.5f * kw, kSym, 0, k.key_special, k.ink);
  x += 1.5f * kw + gap;
  add_key(g_cal.kbd, x, 3, kw, kChar, 3 * 16 + 0, k.key, k.ink);
  x += kw + gap;
  float space_w = (800 - 8 - 2.4f * kw - gap - kw - gap) - x;
  add_key(g_cal.kbd, x, 3, space_w, kSpace, 0, k.key, k.ink);
  x += space_w + gap;
  add_key(g_cal.kbd, x, 3, kw, kChar, 3 * 16 + 1, k.key, k.ink);
  x += kw + gap;
  add_key(g_cal.kbd, x, 3, 2.4f * kw, kDone, 0, k.accent_fill, k.cream);
  refresh_keys();
}

void close_editor_now() {
  if (g_cal.dlg) del_later(g_cal.dlg);  // usually from one of its own buttons
  if (g_cal.kbd) del_later(g_cal.kbd);
  if (g_cal.picker) del_later(g_cal.picker);
  if (g_cal.prompt) del_later(g_cal.prompt);
  g_cal.dlg = g_cal.kbd = g_cal.picker = g_cal.prompt = nullptr;
  g_cal.rep_btn = nullptr;
  g_cal.cursor = nullptr;
  g_cal.focus = kNone;
  g_cal.nkeys = 0;
  if (g_cal.panel) show(g_cal.panel, true);
}

void cancel_clicked(lv_event_t *) {
  close_editor_now();
  if (g_f.events && g_f.events->rev() != g_cal.rev) rebuild();
}

// ---- Pickers and menus over the editor (Mike, 2026-10-25) ----------------------------------------
// Full-size panels over the dialog with buttons at least 90 px tall: a month grid for the date,
// steppers for the time and the length, radio choices for repeating. Cancel leaves the draft as it
// was; Done (or a day, in the date grid) puts the choice in it.

enum PickKind : uint8_t { kPickDate, kPickUntil, kPickTime, kPickLength, kPickRepeat };
PickKind g_pick_kind = kPickDate;
Event g_pick;              // the draft as a time or length picker changes it
Event g_rep;               // the draft as the repeat menu changes it (its until picker too)
int32_t g_pick_month = 0;  // the date grid's month: its first day
lv_obj_t *g_pick_val[2] = {}, *g_pick_sub = nullptr;

void open_repeat_menu(bool fresh);
void build_date_picker();

void close_picker() {
  if (g_cal.picker) del_later(g_cal.picker);
  g_cal.picker = nullptr;
}

// style: 0 plain, 1 the main choice (Done), 2 chosen (a radio choice, AM or PM).
lv_obj_t *big_button(lv_obj_t *parent, int x, int y, int w, int h, const char *words, lv_event_cb_t cb, intptr_t user,
                     int style, bool repeat = false) {
  const theme::CalColors &k = cc();
  Color fill = style == 1 ? k.accent_fill : style == 2 ? k.ink : k.field;
  Color fg = style == 1 ? k.cream : style == 2 ? k.on_ink : k.ink;
  lv_obj_t *b = box(parent, x, y, w, h, fill, 18);
  int in = 0;
  if (style == 0) {
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, C(k.ink), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
    in = 2;
  }
  lv_obj_set_style_bg_color(b, C(gfx::scale(fill, 0.88f)), LV_STATE_PRESSED);
  if (words) text_c(b, B21, 21, false, fg, -in, w, (float)-in, (float)h, words);
  on_click(b, cb, user);
  if (repeat) lv_obj_add_event_cb(b, cb, LV_EVENT_LONG_PRESSED_REPEAT, (void *)user);
  return b;
}

void icon_in(lv_obj_t *b, Art &a, int w, int h, int bw = 2) {
  lv_obj_t *img = image(b, (w - 2 * bw - a.cv.width()) / 2, (h - 2 * bw - a.cv.height()) / 2);
  a.show(img);
}

void picker_cancel(lv_event_t *) {
  close_picker();
  if (g_pick_kind == kPickUntil) open_repeat_menu(false);  // back to the menu it came from
}

void picker_done(lv_event_t *) {
  Event &d = g_cal.draft;
  if (g_pick_kind == kPickTime || g_pick_kind == kPickLength) {
    d.all_day = g_pick.all_day;
    d.start_min = g_pick.start_min;
    d.length = g_pick.length;
    if (!d.all_day) g_cal.timed_start = d.start_min;
  } else if (g_pick_kind == kPickRepeat) {
    d.repeat = g_rep.repeat;
    d.kind = g_rep.kind;
    d.until = g_rep.until;
    d.count = g_rep.count;
    if (d.kind != tc::EventKind::Plain && !d.all_day) {  // a birthday is a day, not a time
      g_cal.timed_start = d.start_min;
      d.all_day = true;
      d.length = 1;
    }
  }
  close_picker();
  refresh_fields();
}

// The panel, its title, a line under it, Cancel on the left and (if done) Done on the right.
lv_obj_t *picker_frame(const char *title, bool done, int title_w = 776 - 312) {
  const theme::CalColors &k = cc();
  close_picker();
  if (g_cal.focus != kNone) set_focus(kNone);  // the keyboard goes down
  lv_obj_t *p = rect(g_cal.root, 12, kDlgY, 776, 406, k.paper, LV_OPA_COVER, 24);
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);  // nothing reaches the dialog under it
  g_cal.picker = p;
  big_button(p, 8, 8, 140, 90, "Cancel", picker_cancel, 0, 0);
  if (done) big_button(p, 776 - 8 - 140, 8, 140, 90, "Done", picker_done, 0, 1);
  text_c(p, J23, 23, true, k.ink, 156, title_w, 16, 32, title);
  g_pick_sub = text_c(p, B15, 15, false, k.muted, 156, title_w, 56, 20, "");
  return p;
}

void set_sub(const char *t, int x0 = 156, int w = 776 - 312) {
  set_text(g_pick_sub, t);
  lv_obj_set_x(g_pick_sub, x0 + (w - text_width(t, B15)) / 2);
}

// -- Date: a month grid; a tap on a day picks it.

void date_tapped(lv_event_t *e) {
  int32_t d = (int32_t)(intptr_t)lv_event_get_user_data(e);
  close_picker();
  if (g_pick_kind == kPickUntil) {
    g_rep.until = d < g_rep.day ? g_rep.day : d;
    g_rep.count = 0;
    open_repeat_menu(false);
    return;
  }
  g_cal.draft.day = d;
  refresh_fields();
}

void date_nav(lv_event_t *e) {
  int dir = (int)(intptr_t)lv_event_get_user_data(e);
  tc::Civil c = civ(g_pick_month);
  int m = c.month + dir, y = c.year;
  if (m < 1) m = 12, y--;
  if (m > 12) m = 1, y++;
  g_pick_month = day_of(y, m, 1);
  build_date_picker();
}

void build_date_picker() {
  const theme::CalColors &k = cc();
  tc::Civil mc = civ(g_pick_month);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %d", kMonths[mc.month - 1], mc.year);
  const int tw = 776 - 156 - 8 - 90 - 8 - 90 - 16;
  lv_obj_t *p = picker_frame(buf, false, tw);
  set_sub(g_pick_kind == kPickUntil ? "The last day it repeats" : "The event's day", 156, tw);
  lv_obj_t *prev = big_button(p, 776 - 8 - 90 - 8 - 90, 8, 90, 90, nullptr, date_nav, -1, 0);
  icon_in(prev, g_icon_prev, 90, 90);
  lv_obj_t *next = big_button(p, 776 - 8 - 90, 8, 90, 90, nullptr, date_nav, 1, 0);
  icon_in(next, g_icon_next, 90, 90);
  for (int c = 0; c < 7; c++) text_c(p, B12, 12, false, k.muted, 10 + c * kCW, kCW, 104, 18, kDayCaps[c], 2);
  int32_t chosen = g_pick_kind == kPickUntil ? (g_rep.until ? g_rep.until : g_rep.day) : g_cal.draft.day;
  int32_t grid0 = g_pick_month - tc::weekday_of(g_pick_month);
  for (int r = 0; r < 6; r++)
    for (int c = 0; c < 7; c++) {
      int32_t d = grid0 + r * 7 + c;
      bool on = d == chosen;
      lv_obj_t *cell = box(p, 10 + c * kCW + 2, 126 + r * 46 + 2, kCW - 4, 42, on ? k.accent_fill : k.field, 10);
      if (!on) lv_obj_set_style_bg_opa(cell, 0, 0);
      int in = 0;
      if (d == g_cal.today && !on) {
        lv_obj_set_style_border_width(cell, 2, 0);
        lv_obj_set_style_border_color(cell, C(k.ink), 0);
        lv_obj_set_style_border_opa(cell, LV_OPA_COVER, 0);
        in = 2;
      }
      lv_obj_set_style_bg_color(cell, C(k.ink), LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(cell, 40, LV_STATE_PRESSED);
      snprintf(buf, sizeof(buf), "%d", civ(d).day);
      lv_obj_t *n = text_c(cell, B19, 19, false, on ? k.cream : k.ink, -in, kCW - 4, (float)-in, 42, buf);
      if (civ(d).month != mc.month && !on) lv_obj_set_style_text_opa(n, 97, 0);
      on_click(cell, date_tapped, (intptr_t)d);
    }
}

void open_date_picker(bool until) {
  g_pick_kind = until ? kPickUntil : kPickDate;
  tc::Civil c = civ(until && g_rep.until ? g_rep.until : g_cal.draft.day);
  g_pick_month = day_of(c.year, c.month, 1);
  build_date_picker();
}

// -- Time: hour, minute (5-minute steps) and AM / PM.

void time_refresh() {
  const theme::CalColors &k = cc();
  char buf[40];
  int h = g_pick.start_min / 60, m = g_pick.start_min % 60;
  snprintf(buf, sizeof(buf), "%d", h % 12 == 0 ? 12 : h % 12);
  set_text(g_pick_val[0], buf);
  lv_obj_set_x(g_pick_val[0], 130 + (140 - text_width(buf, J44)) / 2);
  snprintf(buf, sizeof(buf), "%02d", m);
  set_text(g_pick_val[1], buf);
  lv_obj_set_x(g_pick_val[1], 340 + (140 - text_width(buf, J44)) / 2);
  for (int i = 0; i < 2; i++) {
    lv_obj_t *b = lv_obj_get_child(g_cal.picker, 4 + i);  // AM, PM (made right after the frame's four)
    bool on = (h >= 12) == (i == 1);
    lv_obj_set_style_bg_color(b, C(on ? k.ink : k.field), 0);
    lv_obj_set_style_border_width(b, on ? 0 : 2, 0);
    set_text_color(lv_obj_get_child(b, 0), on ? k.on_ink : k.ink);
  }
  time_range(buf, sizeof(buf), g_pick.start_min, g_pick.end_min());
  set_sub(buf);
}

void time_step(lv_event_t *e) {
  int which = (int)(intptr_t)lv_event_get_user_data(e);
  int h = g_pick.start_min / 60, m = g_pick.start_min % 60;
  if (which <= 1) {  // the hour, round the clock face; AM or PM stays
    int dir = which == 0 ? 1 : -1, h12 = (h % 12 + dir + 12) % 12;
    h = h12 + (h >= 12 ? 12 : 0);
  } else {
    int dir = which == 2 ? 1 : -1;
    m = dir > 0 ? (m / 5 + 1) * 5 : (m % 5 ? m / 5 * 5 : m - 5);
    m = (m + 60) % 60;
  }
  g_pick.start_min = (int16_t)(h * 60 + m);
  time_refresh();
}

void ampm_tapped(lv_event_t *e) {
  bool pm = (intptr_t)lv_event_get_user_data(e) == 1;
  int h = g_pick.start_min / 60, m = g_pick.start_min % 60;
  h = h % 12 + (pm ? 12 : 0);
  g_pick.start_min = (int16_t)(h * 60 + m);
  time_refresh();
}

void open_time_picker() {
  const theme::CalColors &k = cc();
  g_pick = g_cal.draft;
  g_pick_kind = kPickTime;
  if (g_pick.all_day) {  // a time makes it a timed event, an hour long
    g_pick.all_day = false;
    g_pick.start_min = g_cal.timed_start;
    g_pick.length = 60;
  }
  lv_obj_t *p = picker_frame("Start time", true);
  big_button(p, 560, 108, 150, 90, "AM", ampm_tapped, 0, 0);
  big_button(p, 560, 270, 150, 90, "PM", ampm_tapped, 1, 0);
  const int cols[2] = {130, 340};
  for (int c = 0; c < 2; c++) {
    lv_obj_t *up = big_button(p, cols[c], 108, 140, 90, nullptr, time_step, c * 2, 0, true);
    icon_in(up, g_icon_up, 140, 90);
    lv_obj_t *dn = big_button(p, cols[c], 270, 140, 90, nullptr, time_step, c * 2 + 1, 0, true);
    icon_in(dn, g_icon_down, 140, 90);
    g_pick_val[c] = text(p, J44, 44, true, k.ink, cols[c], 202, 64, "");
  }
  text(p, J44, 44, true, k.ink, 302, 198, 64, ":");
  time_refresh();
}

// -- Length: minus and plus (15 minutes, or a day), and the usual lengths.

void len_words(const Event &e, char *out, size_t n) {
  if (e.all_day) {
    snprintf(out, n, e.length == 1 ? "1 day" : "%d days", e.length);
  } else if (e.length < 60) {
    snprintf(out, n, "%d min", e.length);
  } else if (e.length % 60) {
    snprintf(out, n, "%d hr %d min", e.length / 60, e.length % 60);
  } else {
    snprintf(out, n, "%d hr", e.length / 60);
  }
}

void len_refresh() {
  char buf[48];
  len_words(g_pick, buf, sizeof(buf));
  set_text(g_pick_val[0], buf);
  lv_obj_set_x(g_pick_val[0], 180 + (416 - text_width(buf, J44)) / 2);
  if (g_pick.all_day) {
    tc::Civil a = civ(g_pick.day), b = civ(g_pick.day + g_pick.length - 1);
    if (g_pick.length == 1) snprintf(buf, sizeof(buf), "%s, %s %d", kDay3[a.weekday], kMon3[a.month - 1], a.day);
    else
      snprintf(buf, sizeof(buf), "%s, %s %d%s%s, %s %d", kDay3[a.weekday], kMon3[a.month - 1], a.day, kDash, kDay3[b.weekday],
               kMon3[b.month - 1], b.day);
  } else {
    time_range(buf, sizeof(buf), g_pick.start_min, g_pick.end_min());
  }
  set_sub(buf);
}

void len_step(lv_event_t *e) {
  int dir = (intptr_t)lv_event_get_user_data(e) == 1 ? 1 : -1;
  int v = g_pick.length;
  if (g_pick.all_day) v += dir;
  else v = dir > 0 ? (v / 15 + 1) * 15 : (v % 15 ? v / 15 * 15 : v - 15);
  int lo = g_pick.all_day ? 1 : 15, hi = g_pick.all_day ? tc::kMaxEventDays : tc::kMaxEventMinutes;
  g_pick.length = (uint16_t)(v < lo ? lo : v > hi ? hi : v);
  len_refresh();
}

void build_length_picker();

void len_chip(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  bool was_all_day = g_pick.all_day;
  if (!was_all_day) {
    static const int mins[4] = {30, 60, 120, 180};
    if (i < 4) {
      g_pick.length = (uint16_t)mins[i];
    } else {
      g_pick.all_day = true;
      g_pick.length = 1;
    }
  } else {
    static const int days[4] = {1, 2, 3, 7};
    if (i == 0) {
      g_pick.all_day = false;
      g_pick.start_min = g_cal.timed_start;
      g_pick.length = 60;
    } else {
      g_pick.length = (uint16_t)days[i - 1];
    }
  }
  if (g_pick.all_day != was_all_day) build_length_picker();  // the other chips
  else len_refresh();
}

void build_length_picker() {
  const theme::CalColors &k = cc();
  lv_obj_t *p = picker_frame("Length", true);
  lv_obj_t *minus = big_button(p, 40, 108, 130, 90, nullptr, len_step, 0, 0, true);
  icon_in(minus, g_icon_minus, 130, 90);
  lv_obj_t *plus = big_button(p, 606, 108, 130, 90, nullptr, len_step, 1, 0, true);
  icon_in(plus, g_icon_plus, 130, 90);
  g_pick_val[0] = text(p, J44, 44, true, k.ink, 180, 120, 64, "");
  static const char *const timed[5] = {"30 min", "1 hr", "2 hr", "3 hr", "All day"};
  static const char *const days[5] = {"Timed", "1 day", "2 days", "3 days", "1 week"};
  for (int i = 0; i < 5; i++)
    big_button(p, 16 + i * 150, 214, 142, 90, (g_pick.all_day ? days : timed)[i], len_chip, i, 0);
  len_refresh();
}

void open_length_picker() {
  g_pick = g_cal.draft;
  g_pick_kind = kPickLength;
  build_length_picker();
}

// -- Repeating: how often (and birthdays, anniversaries), and when it ends.

void rep_choice(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i <= 4) {
    g_rep.repeat = (tc::Repeat)i;
    g_rep.kind = tc::EventKind::Plain;
  } else {
    g_rep.repeat = tc::Repeat::Yearly;
    g_rep.kind = i == 5 ? tc::EventKind::Birthday : tc::EventKind::Anniversary;
  }
  open_repeat_menu(false);
}

void rep_end(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i == 0) {
    g_rep.until = 0;
    g_rep.count = 0;
  } else if (i == 1) {
    close_picker();
    open_date_picker(true);
    return;
  } else if (i == 2) {
    g_rep.until = 0;
    if (!g_rep.count) g_rep.count = 10;
  } else {
    int c = (g_rep.count ? g_rep.count : 10) + (i == 4 ? 1 : -1);
    g_rep.until = 0;
    g_rep.count = (uint16_t)(c < 2 ? 2 : c > 999 ? 999 : c);
  }
  open_repeat_menu(false);
}

// A radio choice: a ring (a dot in it when chosen) and the words.
lv_obj_t *radio(lv_obj_t *p, int x, int y, int w, const char *words, bool on, lv_event_cb_t cb, intptr_t user) {
  const theme::CalColors &k = cc();
  lv_obj_t *b = big_button(p, x, y, w, 90, nullptr, cb, user, on ? 2 : 0);
  int in = on ? 0 : 2;
  Color fg = on ? k.on_ink : k.ink;
  lv_obj_t *ring = outlined(b, 16 - in, 35 - in, 20, 20, fg, 2, LV_RADIUS_CIRCLE);
  if (on) box(ring, 3, 3, 10, 10, fg, LV_RADIUS_CIRCLE);
  text(b, B19, 19, false, fg, 46 - in, (float)-in, 90, words);
  return b;
}

void open_repeat_menu(bool fresh) {
  const theme::CalColors &k = cc();
  if (fresh) g_rep = g_cal.draft;
  g_pick_kind = kPickRepeat;
  lv_obj_t *p = picker_frame("Repeating event", true);
  set_sub(g_rep.repeats() || g_rep.kind != tc::EventKind::Plain ? "Changes apply when you save the event" : "Doesn't repeat");
  int sel = g_rep.kind == tc::EventKind::Birthday      ? 5
            : g_rep.kind == tc::EventKind::Anniversary ? 6
                                                         : (int)g_rep.repeat;
  static const char *const names[7] = {"Doesn't repeat", "Daily", "Weekly", "Monthly", "Yearly", "Birthday", "Anniversary"};
  const int wa[4] = {212, 168, 176, 180}, wb[3] = {176, 196, 236};
  int x = 8;
  for (int i = 0; i < 4; i++) {
    radio(p, x, 106, wa[i], names[i], sel == i, rep_choice, i);
    x += wa[i] + 8;
  }
  x = 8;
  for (int i = 0; i < 3; i++) {
    radio(p, x, 204, wb[i], names[4 + i], sel == 4 + i, rep_choice, 4 + i);
    x += wb[i] + 8;
  }
  // Ends: never, on a day, or after so many times.
  bool repeats = g_rep.repeats();
  char on_date[32], after[24];
  if (g_rep.until) {
    tc::Civil u = civ(g_rep.until);
    snprintf(on_date, sizeof(on_date), "On %s %d, %d", kMon3[u.month - 1], u.day, u.year);
  } else {
    snprintf(on_date, sizeof(on_date), "On a date");
  }
  snprintf(after, sizeof(after), "%d times", g_rep.count ? g_rep.count : 10);
  int end = g_rep.until ? 1 : g_rep.count ? 2 : 0;
  lv_obj_t *row[5];
  row[0] = radio(p, 8, 302, 156, "Never", end == 0, rep_end, 0);
  row[1] = radio(p, 172, 302, 236, on_date, end == 1, rep_end, 1);
  row[2] = radio(p, 416, 302, 164, after, end == 2, rep_end, 2);
  row[3] = big_button(p, 588, 302, 86, 90, nullptr, rep_end, 3, 0, true);
  icon_in(row[3], g_icon_minus, 86, 90);
  row[4] = big_button(p, 682, 302, 86, 90, nullptr, rep_end, 4, 0, true);
  icon_in(row[4], g_icon_plus, 86, 90);
  if (!repeats)
    for (lv_obj_t *o : row) {  // nothing to end
      lv_obj_set_style_opa(o, 90, 0);
      lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
  (void)k;
}

// -- A repeating event: just this one, or all of them?

bool g_ask_delete = false;

void do_save(bool just_this) {
  Event e = g_cal.draft;
  tc::event_normalize(e);
  uint32_t id = 0;
  if (g_act) {
    if (just_this) {
      id = g_act->save_occurrence(e.id, g_cal.occ_day, e);
    } else {
      if (g_cal.orig_repeats) e.day = g_cal.orig_start + (e.day - g_cal.occ_day);  // the series moves with it
      id = g_act->save_event(e);
    }
  }
  if (!id) {
    // Just this one fails when the series has had too many single changes (24).
    toast(just_this ? "Couldn't change just this one" : g_cal.is_new ? "The calendar is full" : "That event was deleted", 0);
    return;
  }
  g_cal.sel = g_cal.draft.day;
  close_editor_now();
  rebuild();
}

void do_delete(bool just_this) {
  uint32_t id = g_cal.draft.id;
  int32_t day = g_cal.occ_day;
  close_editor_now();
  if (just_this) {
    if (g_act && g_act->delete_occurrence(id, day)) toast("Event deleted", id, day);
  } else if (g_act && g_act->delete_event(id)) {
    toast("Event deleted", id);  // PROPOSED: Undo for 5 seconds
  }
  rebuild();
}

void ask_choice(lv_event_t *e) {
  int which = (int)(intptr_t)lv_event_get_user_data(e);
  if (g_cal.prompt) del_later(g_cal.prompt);
  g_cal.prompt = nullptr;
  if (which == 2) return;
  if (g_ask_delete) do_delete(which == 0);
  else do_save(which == 0);
}

void ask_series(bool del) {
  const theme::CalColors &k = cc();
  g_ask_delete = del;
  if (g_cal.focus != kNone) set_focus(kNone);
  lv_obj_t *veil = plain(g_cal.root, 0, 0, 800, 480 - kRootY);
  lv_obj_add_flag(veil, LV_OBJ_FLAG_CLICKABLE);  // only the prompt's buttons work meanwhile
  g_cal.prompt = veil;
  lv_obj_t *b = rect(veil, 130, 34, 540, 334, k.paper, LV_OPA_COVER, 24);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C(k.ink), 0);
  lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
  text_c(b, J23, 23, true, k.ink, -2, 540, 16, 32, del ? "Delete a repeating event" : "Change a repeating event");
  tc::Civil c = civ(g_cal.occ_day);
  char buf[80];
  snprintf(buf, sizeof(buf), "Just this one (%s, %s %d), or all of them?", kDay3[c.weekday], kMon3[c.month - 1], c.day);
  text_c(b, B15, 15, false, k.muted, -2, 540, 56, 20, buf);
  big_button(b, 18, 116, 246, 90, "Just this one", ask_choice, 0, 0);
  big_button(b, 272, 116, 246, 90, "All of them", ask_choice, 1, 1);
  big_button(b, 18, 220, 500, 90, "Cancel", ask_choice, 2, 0);
}

void save_clicked(lv_event_t *) {
  Event e = g_cal.draft;
  tc::event_normalize(e);
  if (!e.title[0]) {
    toast(e.kind != tc::EventKind::Plain ? "Add a name first" : "Add a title first", 0);
    return;
  }
  if (!g_cal.is_new && g_cal.orig_repeats) ask_series(false);
  else do_save(false);
}

void delete_clicked(lv_event_t *) {
  if (g_cal.orig_repeats) ask_series(true);
  else do_delete(false);
}

void undo_clicked(lv_event_t *) {
  if (g_cal.undo_id && g_act) {
    if (g_cal.undo_day) g_act->restore_occurrence(g_cal.undo_id, g_cal.undo_day);
    else g_act->restore_event(g_cal.undo_id);
  }
  toast_hide(nullptr);
  rebuild();
}

void repeat_clicked(lv_event_t *) { open_repeat_menu(true); }

void field_clicked(lv_event_t *e) {
  int which = (int)(intptr_t)lv_event_get_user_data(e);
  if (which == 0 || which == 1) set_focus(which == 0 ? kTitle : kNotes);
  else if (which == 2) open_date_picker(false);
  else if (which == 3) open_time_picker();
  else open_length_picker();
}

void unit_clicked(lv_event_t *e) {
  bool days = (intptr_t)lv_event_get_user_data(e) == 1;
  Event &d = g_cal.draft;
  if (days == d.all_day) return;
  if (days) {
    g_cal.timed_start = d.start_min;
    d.all_day = true;
    d.length = 1;
  } else {
    d.all_day = false;
    d.start_min = g_cal.timed_start;
    d.length = 60;
  }
  refresh_fields();
}

lv_obj_t *field(lv_obj_t *dlg, int x, int y, int w, const char *name, int label_w, int which) {
  const theme::CalColors &k = cc();
  lv_obj_t *f = box(dlg, x, y, w, 38, k.field, 11);
  lv_obj_set_style_border_width(f, 2, 0);
  lv_obj_set_style_border_color(f, C(k.ink), 0);
  lv_obj_set_style_border_opa(f, LV_OPA_COVER, 0);
  lv_obj_t *l = text(f, B11, 11, false, k.muted, 10, 0, 32, name, 1);
  if (which == 0) g_cal.f_name = l;
  (void)label_w;
  on_click(f, field_clicked, which);
  return f;
}

void build_dialog() {
  const theme::CalColors &k = cc();
  g_cal.dlg = rect(g_cal.root, 12, kDlgY, 776, 214, k.paper, 230, 24);
  lv_obj_add_flag(g_cal.dlg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *d = g_cal.dlg;
  marker(d, g_set.floor, 30, 10, 8);  // spec 1.2: this panel's floor
  text(d, J23, 23, true, k.ink, 48, 8, 32, g_cal.is_new ? "New event" : "Edit event");
  text(d, B14, 14, false, k.muted, 190, 12, 24, g_set.floor == Floor::Upstairs ? "UPSTAIRS EVENT" : "DOWNSTAIRS EVENT", 1);
  lv_obj_t *cancel = outlined(d, 564, 8, 96, 32, k.ink, 2, 16);
  text_c(cancel, B16, 16, false, k.ink, -2, 96, -2, 32, "Cancel");
  press_tint(cancel, k.ink);
  on_click(cancel, cancel_clicked, 0, 6);
  lv_obj_t *save = box(d, 670, 8, 96, 32, k.accent_fill, 16);
  text_c(save, B16, 16, false, k.cream, 0, 96, 0, 32, "Save");
  lv_obj_set_style_bg_color(save, C(gfx::scale(k.accent_fill, 0.88f)), LV_STATE_PRESSED);
  on_click(save, save_clicked, 0, 6);

  g_cal.f_box[0] = field(d, 10, 46, 756, "TITLE", 64, 0);
  g_cal.f_box[1] = field(d, 10, 89, 756, "NOTES", 64, 1);
  for (int i = 0; i < 2; i++) g_cal.f_val[i] = text(g_cal.f_box[i], B19, 19, false, k.ink, 74, 0, 32, "");
  lv_obj_t *fd = field(d, 10, 132, 247, "DATE", 52, 2);
  g_cal.v_date = text(fd, B19, 19, false, k.ink, 62, 0, 32, "");
  lv_obj_t *ft = field(d, 265, 132, 246, "TIME", 52, 3);
  g_cal.v_time = text(ft, B19, 19, false, k.ink, 62, 0, 32, "");
  lv_obj_t *fl = field(d, 519, 132, 247, "LENGTH", 64, 4);
  g_cal.v_len = text(fl, B19, 19, false, k.ink, 76, 0, 32, "");
  lv_obj_t *track = rect(fl, 138, 4, 92, 24, k.ink, 23, 12);
  const char *units[2] = {"hr", "days"};
  for (int i = 0; i < 2; i++) {
    g_cal.unit_seg[i] = box(track, i * 46, 0, 46, 24, k.ink, 12);
    g_cal.unit_lbl[i] = text_c(g_cal.unit_seg[i], B14, 14, false, k.ink, 0, 46, 0, 24, units[i]);
    on_click(g_cal.unit_seg[i], unit_clicked, i, 6);
  }
  g_cal.cursor = box(g_cal.f_box[0], 74, 5, 2, 22, k.accent, 0);
  if (!g_cal.is_new) {  // PROPOSED: no Delete while creating
    lv_obj_t *del = outlined(d, 10, 176, 170, 28, k.danger, 2, 14);
    int tw = text_width("Delete event", B15), x0 = (170 - 4 - (16 + 8 + tw)) / 2;
    lv_obj_t *img = image(del, x0, (28 - 4 - 16) / 2);
    g_icon_trash.show(img);
    text(del, B15, 15, false, k.danger, x0 + 24, -2, 28, "Delete event");
    press_tint(del, k.danger);
    on_click(del, delete_clicked, 0, 6);
  }
  // Repeating (Mike, 2026-10-25): two arrows chasing round and the words, under LENGTH on the right.
  g_cal.rep_btn = outlined(d, 766 - 200, 176, 200, 28, k.ink, 2, 14);
  g_cal.rep_icon = image(g_cal.rep_btn, 10, (28 - 4 - 18) / 2);
  g_cal.rep_lbl = text(g_cal.rep_btn, B15, 15, false, k.ink, 34, -2, 28, "");
  press_tint(g_cal.rep_btn, k.ink);
  on_click(g_cal.rep_btn, repeat_clicked, 0, 6);
}

}  // namespace

// Opens the editor: an existing event, or a new one on day (at start_min, or a default hour).
void open_editor_impl(const Event *e, int32_t day, int start_min, bool animate) {
  if (!g_cal.root) return;
  close_editor_now();
  g_cal.is_new = e == nullptr;
  g_cal.occ_day = day;
  g_cal.orig_start = e ? e->day : day;
  g_cal.orig_repeats = e && e->repeats();
  if (e) {
    g_cal.draft = *e;
    g_cal.draft.day = day;  // the occurrence's date shows (and moves the series by as much)
  } else {
    g_cal.draft = Event();
    g_cal.draft.day = day;
    g_cal.draft.floor = g_set.floor;
    g_cal.draft.length = 60;
    if (start_min < 0) {
      // PROPOSED: the next whole hour today, else 9 AM.
      start_min = 9 * 60;
      if (day == g_cal.today && g_f.wall.valid) {
        int now = tc::minutes_of_day((int64_t)g_f.wall.utc + g_f.utc_offset);
        start_min = (now / 60 + 1) * 60;
        if (start_min > 23 * 60) start_min = 23 * 60;
      }
    }
    g_cal.draft.start_min = (int16_t)start_min;
  }
  g_cal.timed_start = g_cal.draft.all_day ? 9 * 60 : g_cal.draft.start_min;
  g_cal.sym = false;
  if (g_cal.panel) show(g_cal.panel, false);  // spec 9: the editor replaces the panel
  build_dialog();
  build_keyboard(animate ? kKbdHidden : kKbdY);
  set_focus(kTitle);
}

namespace {
void open_editor(const Event *e, int32_t day, int start_min) { open_editor_impl(e, day, start_min, kAnimate); }
}  // namespace

bool cal_is_open() { return g_cal.open; }
bool cal_covering() { return g_cal.open && !g_cal.rolling; }

void report_anim(const char *what, int frames, uint32_t ms) {
  g_report = AnimReport{g_report.serial + 1, what, frames, ms};
}

void cal_open(bool animate) {
  if (g_cal.open || g_cal.rolling || !g_f.wall.valid) return;
  g_cal.open = true;
  g_cal.mode = g_mode;
  g_cal.today = tc::local_day(g_f.wall.utc, g_f.utc_offset);
  g_cal.sel = g_cal.today;  // PROPOSED: today's month, today selected, nothing remembered
  g_cal.view = kMonth;
  if (!g_cal.root) {
    g_cal.root = plain(lv_scr_act(), 0, kRootY, 800, 480 - kRootY);
    under_overlays(g_cal.root);
  }
  build_panel();
  if (animate) {
    lv_obj_set_y(g_cal.panel, -332 - kRootY);
    roll(-332 - kRootY, kPanelY, 360, lv_anim_path_ease_out);
  } else {
    main_tiles_visible(false);
  }
}

void cal_close(bool animate) {
  if (!g_cal.open) return;
  g_cal.open = false;
  close_editor_now();
  main_tiles_visible(true);
  if (g_cal.toast) show(g_cal.toast, false);
  if (animate && g_cal.panel) roll(lv_obj_get_y(g_cal.panel), -332 - kRootY, 300, lv_anim_path_ease_in);
  else roll_done(nullptr);
}

void cal_toggle() {
  if (g_cal.rolling) return;
  if (g_cal.open) cal_close(kAnimate);
  else cal_open(kAnimate);
}

void cal_render(bool) {
  if (!g_cal.open || g_cal.rolling) return;  // a new light waits for the roll: the panel is moving
  if (g_mode != g_cal.mode) {
    // New colours: rebuild what's open; an editor keeps what was typed.
    g_cal.mode = g_mode;
    bool editing = g_cal.dlg != nullptr;
    Field focus = g_cal.focus;
    Event draft = g_cal.draft;
    bool is_new = g_cal.is_new;
    int32_t occ = g_cal.occ_day, orig = g_cal.orig_start;
    bool orig_rep = g_cal.orig_repeats;
    if (editing) close_editor_now();
    build_panel();
    if (editing) {
      g_cal.is_new = is_new;
      g_cal.draft = draft;
      g_cal.occ_day = occ;
      g_cal.orig_start = orig;
      g_cal.orig_repeats = orig_rep;
      show(g_cal.panel, false);
      build_dialog();
      build_keyboard(focus != kNone ? kKbdY : kKbdHidden);
      g_cal.focus = focus;
      refresh_fields();
      refresh_keys();
    }
    return;
  }
  if (g_f.wall.valid) {
    int32_t today = tc::local_day(g_f.wall.utc, g_f.utc_offset);
    if (today != g_cal.today) {
      g_cal.today = today;
      if (!g_cal.dlg) rebuild();
    }
  }
  // Changes from the other panel (or a sync) show at once, unless the editor is up.
  if (!g_cal.dlg && g_f.events && g_f.events->rev() != g_cal.rev) rebuild();
  // Auto close (PROPOSED): 90 s without a touch, except with the editor up (a draft isn't lost).
  if (!g_cal.dlg && lv_disp_get_inactive_time(nullptr) > kAutoCloseMs) cal_close(kAnimate);
}

}  // namespace detail

AnimReport anim_report() { return detail::g_report; }

void show_calendar(CalView v, int32_t day, uint32_t edit_id) {
  using namespace detail;
  if (v == CalView::Closed) {
    cal_close(false);
    return;
  }
  cal_open(false);
  if (!g_cal.open) return;
  g_cal.sel = day;
  g_cal.view = v == CalView::Week ? kWeek : v == CalView::Day ? kDay : kMonth;
  build_view_now();
  if (v >= CalView::Edit) {
    const tc::Event *e = g_f.events ? g_f.events->find(edit_id) : nullptr;
    open_editor_impl(e, day, -1, false);
    if (v == CalView::PickDate) open_date_picker(false);
    else if (v == CalView::PickTime) open_time_picker();
    else if (v == CalView::PickLength) open_length_picker();
    else if (v == CalView::PickRepeat) open_repeat_menu(true);
    else if (v == CalView::AskSeries) ask_series(false);
  }
}

}  // namespace ui
