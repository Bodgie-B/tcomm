// The comfort survey (Mike, 2026-11-06; docs/temperature-requests.md, "Asking how they feel"): the
// downstairs tenant taps INSIDE on the DOWNSTAIRS tile and a column slides in from the left asking
// how it feels. JUST RIGHT saves and closes at once. Any other answer picks an arrow in a second
// column on the right ("ASK UPSTAIRS") and a middle card asks "SEND TO UPSTAIRS?". Each visit saves
// ONE answer, when the visit ends: SEND, CANCEL, or any other close (tap outside, 30 s idle).
#include <stdio.h>
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;
using namespace tc;

namespace {

lv_obj_t *g_reading = nullptr;  // "Downstairs now 67°F  47%", kept live

// The visit's one answer. Kept here, not in the widgets, so a day/night rebuild keeps it, and so a
// close from anywhere (even another sheet taking over) is saved once: comfort_refresh notices.
bool g_have = false;
Feel g_feel = Feel::JustRight;
Ask g_ask = Ask::None;

lv_obj_t *g_survey_btn[kFeelCount];
lv_obj_t *g_col = nullptr, *g_card = nullptr, *g_req_btn[4], *g_req_ring[4], *g_card_img = nullptr;
Art g_req_art[4];  // the four tenant arrows on the column's buttons, 62 px
Art g_card_art;    // the picked arrow on the middle card, 100 px

const Ask kReqAsk[4] = {Ask::UrgentHeat, Ask::MildHeat, Ask::MildCool, Ask::UrgentCool};
const int kColX = 538, kPickD = 62, kCardD = 100;

Color gold() { return hex(0xF5A623); }
Color plum() { return theme::palette(Mode::Day).plum; }

// The visit ends: save the one answer (if any) and close.
void finish(AskSent sent) {
  if (g_have) {
    g_have = false;
    if (g_act) g_act->comfort_answer(g_feel, g_ask, sent);
  }
  close_sheet();
}

void overlay_clicked(lv_event_t *e) {
  if (lv_event_get_target(e) == g_sheet_root) finish(AskSent::Blank);
}

void set_panel_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, (lv_coord_t)v); }

void reading_text(char *out, size_t n) {
  const FloorView &fv = g_f.view->floor[(int)Floor::Downstairs];
  snprintf(out, n, "Downstairs now %s  %s", fv.temp, fv.hum);
}

// Shows what the visit holds: the chosen survey button gets an outline, the picked arrow a plum
// button with a gold outline and a cream ring, and the card's arrow follows the pick.
void mark() {
  for (int i = 0; i < kFeelCount; i++) {
    bool on = g_have && i == (int)g_feel;
    lv_obj_set_style_outline_width(g_survey_btn[i], on ? 5 : 0, 0);
    if (on) {
      lv_obj_set_style_outline_color(g_survey_btn[i], C(ink()), 0);
      lv_obj_set_style_outline_opa(g_survey_btn[i], LV_OPA_COVER, 0);
    }
  }
  if (!g_col) return;
  for (int i = 0; i < 4; i++) {
    bool on = kReqAsk[i] == g_ask;
    Color face = on ? plum() : chip();
    lv_obj_set_style_bg_color(g_req_btn[i], C(face), 0);
    lv_obj_set_style_border_color(g_req_btn[i], C(gfx::scale(face, 0.62f)), 0);
    lv_obj_set_style_outline_width(g_req_btn[i], on ? 5 : 0, 0);
    if (on) {
      lv_obj_set_style_outline_color(g_req_btn[i], C(gold()), 0);
      lv_obj_set_style_outline_opa(g_req_btn[i], LV_OPA_COVER, 0);
    }
    show(g_req_ring[i], on);
  }
  if (g_card_img && g_card_art.cv.ok()) {
    art::request_arrow(g_card_art.cv, g_ask, ReqSource::Tenant);
    g_card_art.show(g_card_img);
  }
}

void sent_clicked(lv_event_t *e) { finish((AskSent)(intptr_t)lv_event_get_user_data(e)); }

void pick_clicked(lv_event_t *e) {
  g_ask = kReqAsk[(int)(intptr_t)lv_event_get_user_data(e)];
  mark();
}

// The right-hand column and the middle card. animate: the column slides in about 200 ms.
void open_request_column(bool animate) {
  lv_obj_t *root = g_sheet_root;
  Color k = ink();
  g_col = box(root, kColX, 0, 292, 480, theme::sheet_bg(g_mode), 22);
  lv_obj_add_flag(g_col, LV_OBJ_FLAG_CLICKABLE);
  label(g_col, J23, k, 16, 14, "ASK UPSTAIRS");
  for (int i = 0; i < 4; i++) {
    g_req_btn[i] = button(g_col, 16, 74 + i * 98, 230, 86, chip(), k, nullptr, J24, pick_clicked, (void *)(intptr_t)i);
    g_req_ring[i] = box(g_req_btn[i], 115 - 34, 43 - 34, 68, 68, hex(0xFBF1DC), LV_RADIUS_CIRCLE);
    show(g_req_ring[i], false);
    if (g_req_art[i].make(kPickD, kPickD, true)) {
      art::request_arrow(g_req_art[i].cv, kReqAsk[i], ReqSource::Tenant);
      lv_obj_t *img = image(g_req_btn[i], 115 - kPickD / 2, 43 - kPickD / 2);
      g_req_art[i].show(img);
    }
  }
  if (animate) anim(g_col, kColX + 292, kColX, 200, set_panel_x);

  g_card = box(root, 280, 92, 240, 306, theme::sheet_bg(g_mode), 22);
  lv_obj_add_flag(g_card, LV_OBJ_FLAG_CLICKABLE);
  const char *title = "SEND TO UPSTAIRS?";
  const lv_font_t *tf = text_width(title, J23) <= 220 ? J23 : B20;
  lv_obj_t *tl = label(g_card, tf, k, 0, 12, title);
  lv_obj_set_width(tl, 240);
  lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
  g_card_img = nullptr;
  if (g_card_art.make(kCardD, kCardD, true)) g_card_img = image(g_card, 120 - kCardD / 2, 104 - kCardD / 2);
  button(g_card, 16, 178, 208, 54, hex(0x1B6B3A), hex(0xFBF1DC), "SEND", J24, sent_clicked,
         (void *)(intptr_t)AskSent::Send);
  button(g_card, 16, 246, 208, 48, chip(), k, "CANCEL", J24, sent_clicked, (void *)(intptr_t)AskSent::Cancel);
}

void answered(lv_event_t *e) {
  Feel f = (Feel)(intptr_t)lv_event_get_user_data(e);
  Ask a = ask_for_feel(f);
  if (a == Ask::None) {  // JUST RIGHT: a replaced answer is dropped; this one saves now
    g_have = false;
    if (g_act) g_act->comfort_answer(f, Ask::None, AskSent::Blank);
    close_sheet();
    return;
  }
  g_have = true;  // one answer per visit: a later tap replaces it and re-picks the arrow
  g_feel = f;
  g_ask = a;
  if (!g_col) open_request_column(true);
  mark();
}

}  // namespace

void open_comfort_survey() {
  bool rebuild = g_sheet == Sheet::Comfort && g_sheet_root;  // day/night change: same visit, new colours
  if (!rebuild && g_have) finish(AskSent::Blank);            // an earlier visit that was closed from elsewhere
  g_col = g_card = g_card_img = nullptr;
  lv_obj_t *root = new_sheet(Sheet::Comfort, true);
  lv_obj_add_event_cb(root, overlay_clicked, LV_EVENT_CLICKED, nullptr);
  Color k = ink();
  // 30 px of it hangs off the left edge, so only the right corners show as rounded.
  const int kPanelX = -30;
  lv_obj_t *panel = box(root, kPanelX, 0, 292, 480, theme::sheet_bg(g_mode), 22);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);  // a tap on the panel's own background doesn't close it
  label(panel, J23, k, 48, 16, "HOW DOES IT FEEL?");
  char now[48];
  reading_text(now, sizeof(now));
  g_reading = label(panel, B16, k, 48, 44, now);
  // The faces match the dots of the comfort chart to come, so they're the same by day and by night.
  struct Face {
    const char *word;
    uint32_t face;
    bool dark;
  };
  static const Face kFaces[kFeelCount] = {{"MUCH TOO HOT", 0x8B1E1E, true}, {"TOO WARM", 0xF4A7B9, false},
                                          {"JUST RIGHT", 0x4D4D4D, true},   {"TOO COOL", 0xA8D0F0, false},
                                          {"MUCH TOO COLD", 0x1E3F8C, true}};
  Color cream = hex(0xFBF1DC);
  for (int i = 0; i < kFeelCount; i++)
    g_survey_btn[i] = button(panel, 46, 74 + i * 78, 230, 66, hex(kFaces[i].face), kFaces[i].dark ? cream : plum(),
                             kFaces[i].word, J24, answered, (void *)(intptr_t)i);
  anim(panel, kPanelX - 292, kPanelX, 200, set_panel_x);  // slides in from the left, about 200 ms
  if (rebuild && g_have) {
    open_request_column(false);
    mark();
  }
}

void comfort_refresh() {
  static int closed_frames = 0;
  if (g_sheet != Sheet::Comfort || !g_sheet_root) {
    if (g_have) {  // closed by something else (a doorbell, another sheet): the answer still saves
      g_have = false;
      if (g_act) g_act->comfort_answer(g_feel, g_ask, AskSent::Blank);
    }
    // The arrows' pictures (about 77 KB of PSRAM) go a few frames after the sheet closes, once its
    // images are surely deleted (close_sheet deletes them asynchronously), so the screensaver has
    // that memory back. They're drawn again on the next visit.
    if (closed_frames < 3 && ++closed_frames == 3) {
      for (Art &a : g_req_art) a.cv.destroy();
      g_card_art.cv.destroy();
    }
    return;
  }
  closed_frames = 0;
  if (lv_disp_get_inactive_time(nullptr) > 30000) {  // nobody answered: close it like the other sheets
    finish(AskSent::Blank);
    return;
  }
  char now[48];
  reading_text(now, sizeof(now));
  if (g_reading) set_text(g_reading, now);
}

}  // namespace detail
}  // namespace ui
