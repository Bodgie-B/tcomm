// The tile message editor (Mike, 2026-11-07; core/note.h): a tap on the line under this panel's own
// status word opens it. A text field that shows the message wrapped exactly as the tile will show it
// (B20, note_line_w() wide, two lines at most), how long it shows (15 min to 4 hr, 4 hr picked to
// start), SEND and CANCEL, and the calendar's keyboard (Keypad, built in ui_calendar.cpp).
//
// Memory: the keyboard and everything else live under the sheet's root, so they exist only while
// the sheet is open. The draft is kept only while it is open. A tick closes the sheet after 60 s
// without a touch, like the other sheets.
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "../core/note.h"
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;

// The width of text[a, b) in the line font with its trailing spaces ignored (a line that ends in a
// space still fits: the space is where it breaks).
static int span_w(const char *t, int a, int b) {
  while (b > a && t[b - 1] == ' ') b--;
  char buf[tc::kNoteTextMax + 2];
  int n = b - a;
  if (n > tc::kNoteTextMax) n = tc::kNoteTextMax;
  memcpy(buf, t + a, (size_t)n);
  buf[n] = 0;
  return text_width(buf, B20);
}

int note_wrap(const char *t, int start[3], int end[3]) {
  int len = (int)strlen(t), w = note_line_w(), pos = 0, n = 0;
  while (pos < len && n < 3) {
    start[n] = pos;
    int fit = pos;  // the most characters that fit on this line
    for (int i = pos + 1; i <= len; i++) {
      if (span_w(t, pos, i) > w) break;
      fit = i;
    }
    if (fit == pos) fit = pos + 1;  // never stuck on one wide character
    int brk = fit, next = fit;
    if (fit < len && t[fit] != ' ') {
      int sp = fit - 1;
      while (sp >= pos && t[sp] != ' ') sp--;
      if (sp >= pos) brk = sp, next = sp + 1;  // break at the last space; no space: mid-word
    }
    end[n] = brk;
    while (brk > pos && end[n] > pos && t[end[n] - 1] == ' ') end[n]--;
    while (next < len && t[next] == ' ') next++;
    pos = next;
    n++;
  }
  return n;
}

namespace {

constexpr int kCardX = 8, kCardY = 8, kCardW = 784, kCardH = 272;  // above the keyboard (screen y 288)
constexpr int kFieldX = 24, kFieldY = 52, kPad = 12;
const char *const kDurWord[tc::kNoteDurations] = {"15 min", "30 min", "1 hr", "2 hr", "4 hr"};

struct Editor {
  char text[tc::kNoteTextMax + 1];
  int dur = tc::kNoteDurations - 1;  // 4 hr to start
  lv_obj_t *field = nullptr, *line[2] = {}, *cursor = nullptr, *count = nullptr;
  lv_obj_t *chip[tc::kNoteDurations] = {}, *chip_lab[tc::kNoteDurations] = {};
  lv_timer_t *timer = nullptr;
  Keypad kp;
};
Editor ed;

Color sheet_col() { return theme::sheet_bg(g_mode); }
const theme::CalColors &cal() { return theme::cal_colors(g_mode); }
Color muted() { return gfx::mix(sheet_col(), ink(), 0.62f); }

bool open_now() { return g_sheet == Sheet::Note && g_sheet_root && ed.field; }

void refresh() {
  if (!open_now()) return;
  int st[3] = {}, en[3] = {};
  int n = note_wrap(ed.text, st, en);
  int lh = B20->line_height;
  for (int i = 0; i < 2; i++) {
    char buf[tc::kNoteTextMax + 2] = "";
    if (i < n) {
      memcpy(buf, ed.text + st[i], (size_t)(en[i] - st[i]));
      buf[en[i] - st[i]] = 0;
    }
    set_text(ed.line[i], buf);
  }
  // The cursor sits after the last character, trailing spaces included.
  int last = n > 0 ? (n > 2 ? 1 : n - 1) : 0;
  char tail[tc::kNoteTextMax + 2] = "";
  if (n > 0) {
    int len = (int)strlen(ed.text);
    memcpy(tail, ed.text + st[last], (size_t)(len - st[last]));
    tail[len - st[last]] = 0;
  }
  lv_obj_set_pos(ed.cursor, kPad + text_width(tail, B20) + 2, 8 + last * lh + 2);
  char c[24];
  snprintf(c, sizeof(c), "%d / %d", (int)strlen(ed.text), tc::kNoteTextMax);
  set_text(ed.count, c);
}

// A key that doesn't fit: the field's border goes red for a moment.
void flash_end(lv_timer_t *) {
  if (!open_now()) return;
  lv_obj_set_style_border_color(ed.field, C(ink()), 0);
}

void flash() {
  if (!open_now()) return;
  lv_obj_set_style_border_color(ed.field, C(cal().danger), 0);
  lv_timer_t *t = lv_timer_create(flash_end, 220, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

bool on_char(char ch) {
  int len = (int)strlen(ed.text);
  if (len >= tc::kNoteTextMax) {
    flash();
    return false;
  }
  if (ch == ' ' && (len == 0 || ed.text[len - 1] == ' ')) return false;  // no leading or doubled spaces
  ed.text[len] = ch;
  ed.text[len + 1] = 0;
  int st[3], en[3];
  if (note_wrap(ed.text, st, en) > 2) {  // it would need a third line
    ed.text[len] = 0;
    flash();
    return false;
  }
  refresh();
  return true;
}

void on_back() {
  int len = (int)strlen(ed.text);
  if (len) ed.text[len - 1] = 0;
  refresh();
}

void stop_tick() {
  if (ed.timer) lv_timer_del(ed.timer);
  ed.timer = nullptr;
}

// Every quarter second: forget the sheet's objects once it is gone (close_sheet deletes them a
// moment after it, with the keyboard) and close after 60 s without a touch.
void tick(lv_timer_t *) {
  if (g_sheet != Sheet::Note || !g_sheet_root) {
    stop_tick();
    ed.field = ed.cursor = ed.count = ed.line[0] = ed.line[1] = nullptr;
    ed.kp.nkeys = 0;
    ed.kp.obj = nullptr;
    return;
  }
  if (lv_disp_get_inactive_time(nullptr) > 60000) close_sheet();
}

void style_chips() {
  Color sheet = sheet_col(), k = ink();
  for (int i = 0; i < tc::kNoteDurations; i++) {
    bool on = i == ed.dur;
    lv_obj_t *b = ed.chip[i];
    lv_obj_set_style_bg_color(b, C(on ? k : sheet), 0);
    set_text_color(ed.chip_lab[i], on ? cal().cream : k);
  }
}

void chip_clicked(lv_event_t *e) {
  ed.dur = (int)(intptr_t)lv_event_get_user_data(e);
  style_chips();
}

void do_send() {
  if (!ed.text[0]) return;  // nothing to send
  char text[tc::kNoteTextMax + 1];
  memcpy(text, ed.text, sizeof(text));
  uint32_t secs = tc::kNoteDurationS[ed.dur];
  close_sheet();
  if (g_act) g_act->set_note(text, secs);
}

void send_clicked(lv_event_t *) { do_send(); }

// Mike, 2026-11-07: the keyboard's Done key does what SEND does.
void on_done() { do_send(); }

void cancel_clicked(lv_event_t *) { close_sheet(); }

}  // namespace

void open_note_editor() {
  ed.text[0] = 0;
  ed.dur = tc::kNoteDurations - 1;
  lv_obj_t *root = new_sheet(Sheet::Note, true);
  Color sheet = sheet_col(), k = ink();
  const theme::CalColors &cc = cal();
  lv_obj_t *card = box(root, kCardX, kCardY, kCardW, kCardH, sheet, 24);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  label(card, J23, k, 24, 14, "MESSAGE ON YOUR TILE");

  // The field: two lines of the tile's font, as wide as the tile's line.
  int lh = B20->line_height, fw = note_line_w() + 2 * kPad, fh = 2 * lh + 16;
  ed.field = box(card, kFieldX, kFieldY, fw, fh, cc.field, 12);
  lv_obj_set_style_border_width(ed.field, 3, 0);
  lv_obj_set_style_border_color(ed.field, C(k), 0);
  lv_obj_set_style_border_opa(ed.field, LV_OPA_COVER, 0);
  for (int i = 0; i < 2; i++) ed.line[i] = label(ed.field, B20, k, kPad, 8 + i * lh, "");
  ed.cursor = box(ed.field, kPad, 10, 2, lh - 4, k, 0);
  ed.count = label(card, B16, muted(), kFieldX + 4, kFieldY + fh + 6, "");

  // SEND and CANCEL to the right of the field.
  const int bx = kCardW - 24 - 180;
  button(card, bx, kFieldY, 180, 64, hex(0x1F6B3A), cc.cream, "SEND", J24, send_clicked, nullptr);
  button(card, bx, kFieldY + 76, 180, 64, chip(), k, "CANCEL", J24, cancel_clicked, nullptr);

  // How long it shows: the chosen one filled dark, like the chart's chips.
  label(card, B16, muted(), kFieldX + 4, 178, "SHOW FOR");
  int x = kFieldX;
  for (int i = 0; i < tc::kNoteDurations; i++) {
    int w = text_width(kDurWord[i], B20) + 34;
    lv_obj_t *b = box(card, x, 204, w, 46, sheet, 23);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, C(k), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, chip_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    ed.chip[i] = b;
    ed.chip_lab[i] = label(b, B20, k, 0, 0, kDurWord[i]);
    lv_obj_align(ed.chip_lab[i], LV_ALIGN_CENTER, 0, -2);
    x += w + 10;
  }
  style_chips();

  // The calendar's keyboard, bound to this field: a capital to start.
  ed.kp.shift = true;
  ed.kp.sym = false;
  ed.kp.on_char = on_char;
  ed.kp.on_back = on_back;
  ed.kp.on_done = on_done;
  keypad_build(ed.kp, root, 288, g_mode);
  if (!ed.timer) ed.timer = lv_timer_create(tick, 250, nullptr);
  refresh();
}

}  // namespace detail
}  // namespace ui
