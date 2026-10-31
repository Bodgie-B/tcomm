// Status sheet (brief 10.2) and timer sheet (brief 10.3), with the handoff's rules:
// tap applies Open, Busy and Away at once; Private and Sleeping continue to the timer sheet.
#include <stdio.h>
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;
using namespace tc;

namespace {

Status g_timed = Status::Private;
int g_dur_i = 3;  // 1 hr, the drawing's default
uint32_t g_custom_s = 90 * 60;
Status g_fallback = Status::Busy;
lv_obj_t *g_dyn = nullptr;  // the part of the timer sheet rebuilt on every choice
Art g_timer_badge;

// Presets per timed status; the last button is Custom. First choice "until changed" (the drawn
// sheet had no way to keep a status without a timer). Mike, 2026-10-22: Sleeping offers 2, 4, 6, 7
// and 8 hours; Private starts on "until changed", Sleeping on 7 hours.
const uint32_t kPrivateDur[] = {0, 15 * 60, 30 * 60, 3600, 2 * 3600, 4 * 3600, 0};
const char *const kPrivateLabels[] = {"UNTIL\nCHANGED", "15 min", "30 min", "1 hr", "2 hr", "4 hr", "Custom"};
const uint32_t kSleepDur[] = {0, 2 * 3600, 4 * 3600, 6 * 3600, 7 * 3600, 8 * 3600, 0};
const char *const kSleepLabels[] = {"UNTIL\nCHANGED", "2 hr", "4 hr", "6 hr", "7 hr", "8 hr", "Custom"};

const uint32_t *durations() { return g_timed == Status::Sleeping ? kSleepDur : kPrivateDur; }
const char *const *dur_labels() { return g_timed == Status::Sleeping ? kSleepLabels : kPrivateLabels; }
int default_dur() { return g_timed == Status::Sleeping ? 4 : 0; }

void overlay_clicked(lv_event_t *e) {
  if (lv_event_get_target(e) == g_sheet_root) close_sheet();
}

void status_chosen(lv_event_t *e) {
  intptr_t i = (intptr_t)lv_event_get_user_data(e);
  if (i < 0) {
    close_sheet();
    return;
  }
  Status s = (Status)i;
  if (s == Status::Private || s == Status::Sleeping) {
    open_timer_sheet(s);
    return;
  }
  close_sheet();
  if (g_act) g_act->choose_status(s);
}

void dur_text(char *out, size_t n, uint32_t s) {
  if (s % 3600 == 0) snprintf(out, n, "%u hr", (unsigned)(s / 3600));
  else if (s >= 3600) snprintf(out, n, "%u hr %u min", (unsigned)(s / 3600), (unsigned)(s % 3600 / 60));
  else snprintf(out, n, "%u min", (unsigned)(s / 60));
}

void timer_rebuild();

void pick_duration(lv_event_t *e) {
  g_dur_i = (int)(intptr_t)lv_event_get_user_data(e);
  timer_rebuild();
}

void pick_fallback(lv_event_t *e) {
  g_fallback = (Status)(intptr_t)lv_event_get_user_data(e);
  timer_rebuild();
}

void custom_step(lv_event_t *e) {
  int32_t v = (int32_t)g_custom_s + (int32_t)(intptr_t)lv_event_get_user_data(e);
  if (v < 10 * 60) v = 10 * 60;  // PROPOSED: 10 minutes to 12 hours, 5-minute steps
  if (v > 12 * 3600) v = 12 * 3600;
  g_custom_s = (uint32_t)v;
  timer_rebuild();
}

void timer_cancel(lv_event_t *) { close_sheet(); }

void timer_start(lv_event_t *) {
  uint32_t secs = g_dur_i == 6 ? g_custom_s : durations()[g_dur_i];
  if (secs && !g_f.wall.valid) return;  // the button explains: no timers without a clock
  Status s = g_timed, fb = g_fallback;
  close_sheet();
  if (!g_act) return;
  if (secs == 0) g_act->choose_status(s);
  else g_act->start_timer(s, secs, fb);
}

void timer_rebuild() {
  if (!g_sheet_root || g_sheet != Sheet::Timer) return;
  if (g_dyn) {  // called from a button inside it: hide now, delete after the event finishes
    lv_obj_add_flag(g_dyn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_del_async(g_dyn);
  }
  const theme::Palette &pal = theme::palette(g_mode);
  Color k = ink();
  g_dyn = plain(g_sheet_root, 0, 74, 800, 406);
  lv_obj_t *p = g_dyn;

  lv_obj_t *l1 = label(p, B16, k, 16, 60, "FOR HOW LONG");
  lv_obj_set_style_text_letter_space(l1, 2, 0);
  Color sel_face = g_mode == Mode::Night ? hex(0xE4D8C0) : pal.plum;
  Color sel_text = g_mode == Mode::Night ? hex(0x1B2620) : pal.cream;
  if (g_dur_i == 6) {
    // Custom (PROPOSED; not drawn): the duration row becomes steppers around the value, 10 min to
    // 12 h in 5-minute steps. Back returns to the presets.
    button(p, 12, 82, 104, 90, chip(), k, "Back", B20, pick_duration, (void *)(intptr_t)default_dur());
    const int32_t steps[4] = {-3600, -300, 300, 3600};
    const char *labels[4] = {"- 1 hr", "- 5 min", "+ 5 min", "+ 1 hr"};
    const int xs[4] = {124, 236, 572, 684};
    for (int i = 0; i < 4; i++)
      button(p, xs[i], 82, 104, 90, chip(), k, labels[i], B20, custom_step, (void *)(intptr_t)steps[i]);
    char buf[24];
    dur_text(buf, sizeof(buf), g_custom_s);
    lv_obj_t *v = box(p, 348, 82, 216, 90, sel_face, 18);
    lv_obj_t *vl = label(v, B24, sel_text, 0, 0, buf);
    lv_obj_center(vl);
  } else {
    for (int i = 0; i < 7; i++) {
      bool sel = i == g_dur_i;
      button(p, 12 + i * 112, 82, 104, 90, sel ? sel_face : chip(), sel ? sel_text : k, dur_labels()[i], i == 0 ? B16 : B20,
             pick_duration, (void *)(intptr_t)i);
    }
  }
  uint32_t secs = g_dur_i == 6 ? g_custom_s : durations()[g_dur_i];
  bool timed = secs != 0;

  {
    lv_obj_t *l2 = label(p, B16, k, 16, 182, "THEN SWITCH TO");
    lv_obj_set_style_text_letter_space(l2, 2, 0);
    int n = 0;
    for (int i = 0; i < kStatusCount; i++) {
      if ((Status)i == g_timed) continue;
      theme::StatusColors sc = theme::status_colors(i, g_mode);
      lv_obj_t *b = button(p, 12 + n * 196, 204, 188, 90, sc.tile, sc.text, status_word((Status)i), J30, pick_fallback,
                           (void *)(intptr_t)i);
      if ((Status)i == g_fallback) outline(b, k);
      if (!timed) {
        lv_obj_set_style_opa(b, LV_OPA_40, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
      }
      n++;
    }
  }

  button(p, 12, 314, 200, 90, chip(), k, "Cancel", J30, timer_cancel, nullptr);
  theme::StatusColors ts = theme::status_colors((int)g_timed, g_mode);
  lv_obj_t *start = button(p, 224, 314, 564, 90, ts.tile, ts.text, "", J30, timer_start, nullptr);
  lv_obj_t *sl = label(start, J30, ts.text, 0, 0, timed ? "START TIMER" : "SET STATUS");
  lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, 6);
  char sum[64], d[24];
  if (timed && !g_f.wall.valid) {
    snprintf(sum, sizeof(sum), "Set the clock first to use a timer");
    lv_obj_set_style_opa(start, LV_OPA_50, 0);
  } else if (timed) {
    dur_text(d, sizeof(d), secs);
    snprintf(sum, sizeof(sum), "%s for %s, then %s", status_title(g_timed), d, status_title(g_fallback));
  } else {
    snprintf(sum, sizeof(sum), "%s until you change it", status_title(g_timed));
  }
  lv_obj_t *sm = label(start, B16, ts.text, 0, 0, sum);
  lv_obj_align(sm, LV_ALIGN_BOTTOM_MID, 0, -12);
}

}  // namespace

void open_status_sheet() {
  lv_obj_t *root = new_sheet(Sheet::Status, true);
  lv_obj_add_event_cb(root, overlay_clicked, LV_EVENT_CLICKED, nullptr);
  Color k = ink();
  lv_obj_t *panel = box(root, 0, 480, 800, 310, theme::sheet_bg(g_mode), 24);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);  // a tap on the panel's own background doesn't close it
  char title[40];
  snprintf(title, sizeof(title), "%s status", floor_word(g_set.floor));
  label(panel, J30, k, 24, 16, title);
  char now[32];
  snprintf(now, sizeof(now), "NOW: %s", status_word(g_f.own.status));
  lv_obj_t *nl = label(panel, B20, k, 0, 24, now);
  lv_obj_set_x(nl, 800 - 24 - text_width(now, B20));
  // Six 96 px buttons in a 3 x 2 grid; the current status has a heavy outline.
  const int order[6] = {0, 1, 2, 3, 4, -1};
  for (int n = 0; n < 6; n++) {
    int i = order[n];
    int x = 16 + (n % 3) * 260, y = 76 + (n / 3) * 108;
    Color face, text;
    const char *word;
    if (i >= 0) {
      theme::StatusColors sc = theme::status_colors(i, g_mode);
      face = sc.tile;
      text = sc.text;
      word = status_word((Status)i);
    } else {
      face = chip();
      text = k;
      word = "CANCEL";
    }
    lv_obj_t *b = button(panel, x, y, 248, 96, face, text, "", J30, status_chosen, (void *)(intptr_t)i);
    bool timer = i == (int)Status::Private || i == (int)Status::Sleeping;
    lv_obj_t *l = label(b, J30, text, 0, 0, word);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, timer ? -13 : -3);
    if (timer) {  // the drawing's "+ timer" caption on the two statuses that continue to the timer
      lv_obj_t *cap = label(b, B16, text, 0, 0, "+ timer");
      lv_obj_align(cap, LV_ALIGN_CENTER, 0, 20);
    }
    if (i >= 0 && (Status)i == g_f.own.status) outline(b, k);
  }
  anim(panel, 480, 480 - 300, 200, set_y);  // slides up from the bottom, about 200 ms
}

void open_timer_sheet(Status s) {
  lv_obj_t *root = new_sheet(Sheet::Timer, false);
  g_dyn = nullptr;
  Color k = ink();
  // Full panel below the header: the date, time and gear stay visible (brief 10.3).
  lv_obj_t *bg = box(root, 0, 74, 800, 406, theme::sheet_bg(g_mode), 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  g_timed = s;
  g_dur_i = default_dur();
  // Custom starts from the default time (an hour when the default is "until changed").
  g_custom_s = durations()[g_dur_i] ? durations()[g_dur_i] : 3600;
  g_fallback = g_set.timer_fallback_default;  // HANDOFF: Busy
  if (g_fallback == s) g_fallback = Status::Busy;
  theme::StatusColors sc = theme::status_colors((int)s, g_mode);
  g_timer_badge.make(art::kBadge, art::kBadge, true);
  art::badge(g_timer_badge.cv, (int)s, g_mode == Mode::Night ? k : sc.tile, theme::sheet_bg(g_mode));
  // The 80 px badge drawn at 48 px: zoom about its centre, so the visible badge sits 16 px inside
  // the object's box. Visible at (14, 6).
  lv_obj_t *bimg = image(bg, 14 - 16, 6 - 16);
  g_timer_badge.show(bimg);
  lv_img_set_pivot(bimg, 40, 40);
  lv_img_set_zoom(bimg, 154);
  char title[40];
  snprintf(title, sizeof(title), "TIMER FOR %s", status_word(s));
  label(bg, J30, k, 72, 12, title);
  timer_rebuild();
}

}  // namespace detail
}  // namespace ui
