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
#include "../core/text_edit.h"
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;

// The width of the first n characters of t in the line font (tc::WidthFn).
static int b20_w(const char *t, int n, void *) {
  char buf[tc::kNoteTextMax + 2];
  if (n > tc::kNoteTextMax) n = tc::kNoteTextMax;
  if (n < 0) n = 0;
  memcpy(buf, t, (size_t)n);
  buf[n] = 0;
  return text_width(buf, B20);
}

// The wrap itself is tc::te_wrap (core/text_edit.cpp, host-tested), moved there unchanged for tap to
// place the cursor (docs/voice-notes/DESIGN.md 8.2).
int note_wrap(const char *t, int start[3], int end[3]) { return tc::te_wrap(t, note_line_w(), b20_w, nullptr, start, end, 3); }

namespace {

constexpr int kCardX = 8, kCardY = 8, kCardW = 784, kCardH = kKpTop - 16;  // above the keyboard (kKpTop)
constexpr int kFieldX = 24, kFieldY = 52, kPad = 12;
const char *const kDurWord[tc::kNoteDurations] = {"15 min", "30 min", "1 hr", "2 hr", "4 hr"};

struct Editor {
  char text[tc::kNoteTextMax + 1];
  int dur = tc::kNoteDurations - 1;  // 4 hr to start
  lv_obj_t *field = nullptr, *line[2] = {}, *cursor = nullptr, *count = nullptr;
  lv_obj_t *chip[tc::kNoteDurations] = {}, *chip_lab[tc::kNoteDurations] = {};
  lv_timer_t *timer = nullptr;
  Keypad kp;
  // Tap to place the cursor (DESIGN 8): the keys and the voice text go in at te.cursor.
  tc::TextEdit te{text, tc::kNoteTextMax, 0};
  uint32_t press_ms = 0;  // the last cursor move by touch (a slide moves it at most 20 times a second)
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
  // The cursor on the line that holds it (one exactly at a break starts the next line), after the
  // characters before it on that line, spaces included; 3 px wide, centred on the gap.
  tc::te_set(ed.te, ed.te.cursor);
  int line = n > 0 ? tc::te_line_of(st, n, ed.te.cursor) : 0;
  if (line > 1) line = 1;
  int from = n > 0 ? st[line] : 0;
  int cx = kPad + b20_w(ed.text + from, ed.te.cursor - from, nullptr) - 1;
  lv_obj_set_pos(ed.cursor, cx, 8 + line * lh + 2);
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

// A key at the cursor (tap to place the cursor, DESIGN 8.1).
bool on_char(char ch) {
  tc::TextEdit &t = ed.te;
  tc::te_set(t, t.cursor);
  int len = (int)strlen(ed.text), c = t.cursor;
  if (len >= tc::kNoteTextMax) {
    flash();
    return false;
  }
  // No leading or doubled spaces: both neighbours count.
  if (ch == ' ' && (c == 0 || ed.text[c - 1] == ' ' || ed.text[c] == ' ')) return false;
  tc::te_insert(t, ch);
  int st[3], en[3];
  if (note_wrap(ed.text, st, en) > 2) {  // it would need a third line
    tc::te_backspace(t);
    flash();
    return false;
  }
  refresh();
  return true;
}

// Backspace at the cursor. A space left leading or doubled by it goes too, and in the rare case the
// joined words would need a third line nothing changes.
void on_back() {
  tc::TextEdit &t = ed.te;
  tc::te_set(t, t.cursor);
  if (t.cursor > 0) {
    char before[tc::kNoteTextMax + 1];
    memcpy(before, ed.text, sizeof(before));
    int was = t.cursor;
    tc::te_backspace(t);
    int c = t.cursor;
    if (ed.text[c] == ' ' && (c == 0 || ed.text[c - 1] == ' ')) tc::te_delete(t);
    int st[3], en[3];
    if (note_wrap(ed.text, st, en) > 2) {
      memcpy(ed.text, before, sizeof(before));
      t.cursor = was;
      flash();
    }
  }
  refresh();
}

// A press in the field puts the cursor at the nearest gap: the line by y, the gap by the midpoint
// rule; sliding moves it along, at most 20 times a second.
void field_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if ((code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) || !open_now()) return;
  if (code == LV_EVENT_PRESSING && lv_tick_elaps(ed.press_ms) < 50) return;
  ed.press_ms = lv_tick_get();
  lv_indev_t *in = lv_indev_get_act();
  if (!in) return;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  lv_area_t a;
  lv_obj_get_coords(ed.line[0], &a);  // the first line's left and top, in the field's own frame
  int st[3] = {}, en[3] = {};
  int n = note_wrap(ed.text, st, en), len = (int)strlen(ed.text), c = 0;
  if (n > 0) {
    int shown = n > 2 ? 2 : n, y = p.y - a.y1;
    int line = y < 0 ? 0 : y / B20->line_height;
    if (line > shown - 1) line = shown - 1;
    int hi = line == n - 1 ? len : en[line];  // a line above the last ends before its break space
    c = tc::te_hit(ed.text, st[line], hi, p.x - a.x1, b20_w, nullptr);
  }
  if (c == ed.te.cursor) return;
  tc::te_set(ed.te, c);
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

// Voice notes (DESIGN 2.1): the mic key's text goes in at the cursor, through on_char (the strip
// finds the cursor by tc::te_find on this text).
const char *on_mic() { return ed.text; }

void cancel_clicked(lv_event_t *) { close_sheet(); }

}  // namespace

void open_note_editor() {
  ed.text[0] = 0;
  ed.te.cursor = 0;
  tc::te_register(&ed.te);
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
  ed.cursor = box(ed.field, kPad, 10, 3, lh - 4, k, 0);
  // Tap to place the cursor; the press stays with the field while the finger slides.
  lv_obj_add_flag(ed.field, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ed.field, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(ed.field, field_event, LV_EVENT_ALL, nullptr);
  ed.count = label(card, B16, muted(), kFieldX + 4, kFieldY + fh + 6, "");

  // SEND and CANCEL to the right of the field.
  const int bx = kCardW - 24 - 180;
  const int bh = 60, bgap = 12;  // Mike, 2026-11-21: 60 px high (were 64) for the taller keyboard
  button(card, bx, kFieldY, 180, bh, hex(0x1F6B3A), cc.cream, "SEND", J24, send_clicked, nullptr);
  button(card, bx, kFieldY + bh + bgap, 180, bh, chip(), k, "CANCEL", J24, cancel_clicked, nullptr);

  // How long it shows: the chosen one filled dark, like the chart's chips. Layout B: SHOW FOR left of
  // the chips on one row.
  const int chip_y = 138;
  label(card, B16, muted(), kFieldX + 4, chip_y + 13, "SHOW FOR");
  int x = kFieldX + 4 + text_width("SHOW FOR", B16) + 14;
  for (int i = 0; i < tc::kNoteDurations; i++) {
    int w = text_width(kDurWord[i], B20) + 28;
    lv_obj_t *b = box(card, x, chip_y, w, 46, sheet, 23);
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
  ed.kp.on_mic = voice_offered() ? on_mic : nullptr;
  keypad_build(ed.kp, root, kKpTop, g_mode);
  if (!ed.timer) ed.timer = lv_timer_create(tick, 250, nullptr);
  refresh();
}

}  // namespace detail
}  // namespace ui
