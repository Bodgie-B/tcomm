// Settings (board F11 layout, scrolling for the tiles the board doesn't have room for), the
// Time & Date editor, the ringtone list, the demo Test panel, and clean-screen mode (brief 10.8).
#include <stdio.h>
#include "../core/civil.h"
#include "../core/synth.h"
#include "../core/zambretti.h"
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;
using gfx::scale;
using namespace tc;

namespace {

// Board F11's Wi-Fi and Status presets tiles are gone: the panels never join a network, and Mike
// doesn't need presets (2026-10-31). Each tile keeps the colour it had beside them (kTileColour).
enum TileId { kTime, kHoliday, kTemp, kUnit, kRing, kVol, kNight, kScreen, kQuiet, kZambretti, kTest, kTiles };
const char *const kTileLabel[kTiles] = {"TIME & DATE",  "HOLIDAY WORDS",   "TEMP OFFSET",    "THIS UNIT IS",
                                        "RINGTONE",     "VOLUME",          "NIGHT LIGHT",    "SCREEN AT NIGHT",
                                        "QUIET HOURS",  "ZAMBRETTI DEMO",  "TEST PANEL"};
const char *const kTileSym[kTiles] = {LV_SYMBOL_LOOP,      LV_SYMBOL_EDIT,  LV_SYMBOL_TINT, LV_SYMBOL_HOME,
                                      LV_SYMBOL_BELL,      LV_SYMBOL_VOLUME_MAX, LV_SYMBOL_EYE_CLOSE, LV_SYMBOL_POWER,
                                      LV_SYMBOL_MUTE,      LV_SYMBOL_IMAGE, LV_SYMBOL_SETTINGS};
const int kTileColour[kTiles] = {1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};  // theme::settings_tile
lv_obj_t *g_val[kTiles] = {};
lv_obj_t *g_confirm = nullptr;
int64_t g_edit_local = 0;
lv_obj_t *g_edit_date = nullptr, *g_edit_time = nullptr;
lv_obj_t *g_test_lbl[11] = {};
uint32_t g_clean_start = 0;
lv_obj_t *g_clean_lbl = nullptr;
uint32_t g_done_down = 0;

void apply(const Settings &s) {
  g_set = s;
  if (g_act) g_act->apply_settings(s);
}

void tile_value(int id, char *out, size_t n) {
  switch (id) {
    case kTime:
      if (g_f.wall.valid) {
        char t[16];
        int64_t local = (int64_t)g_f.wall.utc + g_f.utc_offset;
        Civil c = civil_from_seconds(local);
        static const char *const kMon[] = {"Jan.", "Feb.", "March", "April", "May", "June",
                                           "July", "Aug.", "Sept.", "Oct.", "Nov.", "Dec."};
        format_time12(t, sizeof(t), local);
        snprintf(out, n, "%s %d, %s", kMon[c.month - 1], c.day, t);
      } else {
        snprintf(out, n, "Not set");
      }
      break;
    case kHoliday: snprintf(out, n, "Not built yet"); break;
    case kTemp: {
      int t = g_set.temp_offset_tenths_f;
      int whole = t >= 0 ? (t + 5) / 10 : -((-t + 5) / 10);
      snprintf(out, n, "%+d\xC2\xB0" "F", whole);
      break;
    }
    case kUnit:
      if (g_act && g_act->floor_locked()) snprintf(out, n, "%s (by MAC)", floor_word(g_set.floor));
      else snprintf(out, n, "%s", floor_word(g_set.floor));
      break;
    case kRing: snprintf(out, n, "%s", ringtone_name(g_set.ringtone)); break;
    case kZambretti: snprintf(out, n, "All the forecasts"); break;
    case kVol: {  // 60, 70, 80, 90, 100 are levels 1 to 5; anything stored below 60 shows as level 1
      int lvl = g_set.volume < 60 ? 1 : (g_set.volume - 50 + 5) / 10;
      snprintf(out, n, "%d of 5", lvl > 5 ? 5 : lvl);
      break;
    }
    case kNight: snprintf(out, n, "%s (%u)", night_level_name(g_set.night_level), (unsigned)night_backlight(g_set.night_level)); break;
    case kScreen: snprintf(out, n, "%s", g_set.screen_off_overnight ? "Off overnight" : "Stays on, dim"); break;
    case kQuiet: {
      char a[16], b[16];
      format_time12(a, sizeof(a), g_set.quiet_start_min * 60);
      format_time12(b, sizeof(b), g_set.quiet_end_min * 60);
      snprintf(out, n, "%s-%s, max %u", a, b, (unsigned)g_set.quiet_volume_cap);
      break;
    }
    default: snprintf(out, n, "Demo controls"); break;
  }
}

void step(lv_event_t *e) {
  intptr_t v = (intptr_t)lv_event_get_user_data(e);
  int id = (int)(v / 10), dir = (int)(v % 10) == 1 ? 1 : -1;
  Settings s = g_set;
  switch (id) {
    case kTemp: {
      int t = s.temp_offset_tenths_f + dir * 10;
      if (t < -100) t = -100;
      if (t > 100) t = 100;
      s.temp_offset_tenths_f = (int16_t)t;
      break;
    }
    case kVol: {  // brief F11: five levels. Mike's levels 80 and 90 are two of them.
      int vol = (s.volume / 10) * 10 + dir * 10;
      if (vol < 60) vol = 60;
      if (vol > 100) vol = 100;
      s.volume = (uint8_t)vol;
      apply(s);
      if (g_act) g_act->preview(Sound::SoftChime, s.volume);  // F11: plays a sample chime on each change
      return;
    }
    case kNight: {
      int lvl = (int)s.night_level + dir;
      if (lvl < 0) lvl = 0;
      if (lvl > 2) lvl = 2;
      s.night_level = (NightLevel)lvl;
      break;
    }
    default: return;
  }
  apply(s);
}

void confirm_answer(lv_event_t *e) {
  bool yes = (intptr_t)lv_event_get_user_data(e) == 1;
  if (g_confirm) {  // the dim overlay holding the dialog
    lv_obj_add_flag(g_confirm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_del_async(g_confirm);
    g_confirm = nullptr;
  }
  if (!yes) return;
  Settings s = g_set;
  s.floor = other_floor(s.floor);
  apply(s);
}

void ask_swap() {
  // PROPOSED: confirm before swapping, it changes which floor and dog this panel speaks for.
  // Modal: a full-screen dim layer under the dialog takes every other touch.
  if (g_confirm || (g_act && g_act->floor_locked())) return;
  const theme::Palette &pal = theme::palette(g_mode);
  g_confirm = box(g_sheet_root, 0, 0, 800, 480, hex(0), 0);
  lv_obj_set_style_bg_opa(g_confirm, LV_OPA_50, 0);
  lv_obj_add_flag(g_confirm, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *d = box(g_confirm, 100, 120, 600, 260, pal.doorbell, 24);
  lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
  char q[64];
  Floor to = other_floor(g_set.floor);
  snprintf(q, sizeof(q), "Make this unit %s?", floor_word(to));
  label(d, J30, pal.cream, 28, 24, q);
  char sub[64];
  snprintf(sub, sizeof(sub), "Its dog becomes %s.", to == Floor::Upstairs ? "Lucy" : "Copper");
  label(d, B20, pal.cream, 30, 72, sub);
  button(d, 24, 146, 268, 90, pal.ring, pal.cream, "Cancel", J30, confirm_answer, (void *)0);
  theme::StatusColors open = theme::status_colors(0, g_mode);
  button(d, 308, 146, 268, 90, open.tile, open.text, "Swap", J30, confirm_answer, (void *)1);
}

void tile_clicked(lv_event_t *e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  Settings s = g_set;
  switch (id) {
    case kTime: open_time_date(); return;
    case kTest: open_test_panel(); return;
    case kUnit: ask_swap(); return;
    case kScreen: s.screen_off_overnight = !s.screen_off_overnight; apply(s); return;
    case kRing: open_ringtones(); return;
    case kZambretti: open_zambretti_demo(); return;
    default: return;  // holiday words and quiet hours aren't editable yet
  }
}

void build_settings(lv_obj_t *root) {
  lv_obj_t *bg = box(root, 0, 74, 800, 406, theme::sheet_bg(g_mode), 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(bg, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(bg, LV_SCROLLBAR_MODE_OFF);
  bool demo = g_act && g_act->demo_info().available;
  int count = demo ? kTiles : kTiles - 1;
  const theme::Palette &pal = theme::palette(g_mode);
  for (int id = 0; id < count; id++) {
    int x = 12 + (id % 2) * 394, y = 12 + (id / 2) * 102;
    Color face = theme::settings_tile(kTileColour[id], g_mode);
    Color text = g_mode == Mode::Night ? hex(0xE4D8C0) : hex(0xFBF1DC);
    lv_obj_t *t = box(bg, x, y, 382, 90, face, 24);
    // Buttons at the tile's right edge follow its rounded corners. Only there: clipping to the
    // corners draws the whole tile through a mask, and drawing Settings is what limits its roll-down
    // (2026-10-26).
    bool edge_buttons = id == kTemp || id == kVol || id == kNight || (id == kUnit && !(g_act && g_act->floor_locked()));
    if (edge_buttons) lv_obj_set_style_clip_corner(t, true, 0);
    bool tappable = id == kTime || id == kTest || id == kScreen || id == kRing || id == kZambretti;
    if (tappable) {
      pressable(t, face, 90);
      lv_obj_add_event_cb(t, tile_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    }
    lv_obj_t *ic = box(t, 14, 17, 56, 56, g_mode == Mode::Night ? scale(text, 0.7f) : pal.paper, LV_RADIUS_CIRCLE);
    lv_obj_t *sym = label(ic, &lv_font_montserrat_26, face, 0, 0, kTileSym[id]);
    lv_obj_center(sym);
    lv_obj_t *l = label(t, B16, text, 82, 16, kTileLabel[id]);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    g_val[id] = label(t, B24, text, 82, 42, "");
    lv_obj_set_height(g_val[id], 26);  // one line; long values end in dots instead of wrapping
    bool stepper = id == kTemp || id == kVol || id == kNight;
    if (stepper) {
      Color dark = scale(face, 0.75f);
      button(t, 382 - 172, 0, 84, 90, dark, text, "–", J30, step, (void *)(intptr_t)(id * 10 + 0));
      button(t, 382 - 84, 0, 84, 90, dark, text, "+", J30, step, (void *)(intptr_t)(id * 10 + 1));
      lv_obj_set_width(g_val[id], 382 - 172 - 82 - 6);
      lv_label_set_long_mode(g_val[id], LV_LABEL_LONG_DOT);
    } else if (id == kUnit && !(g_act && g_act->floor_locked())) {
      Color dark = scale(face, 0.75f);
      lv_obj_t *sw = button(t, 382 - 90, 0, 90, 90, dark, text, "", J30, tile_clicked, (void *)(intptr_t)kUnit);
      lv_obj_t *s = label(sw, &lv_font_montserrat_26, text, 0, 0, LV_SYMBOL_SHUFFLE);
      lv_obj_center(s);
      lv_obj_set_width(g_val[id], 382 - 90 - 82 - 6);
      lv_label_set_long_mode(g_val[id], LV_LABEL_LONG_DOT);
    } else {
      lv_obj_set_width(g_val[id], 382 - 82 - 16);
      lv_label_set_long_mode(g_val[id], LV_LABEL_LONG_DOT);
    }
  }
  for (int id = count; id < kTiles; id++) g_val[id] = nullptr;
}

// --- Time & Date ---

// Steps in seconds; +1 / -1 mean one calendar month (the day is clamped to the month's length).
void edit_step(lv_event_t *e) {
  int64_t v = (int64_t)(intptr_t)lv_event_get_user_data(e);
  if (v == 1 || v == -1) {
    Civil c = civil_from_seconds(g_edit_local);
    int m = c.month + (int)v, y = c.year;
    if (m < 1) { m = 12; y--; }
    if (m > 12) { m = 1; y++; }
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = kDays[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
    g_edit_local = seconds_from_civil(y, m, c.day > dim ? dim : c.day, c.hour, c.minute, 0);
  } else {
    g_edit_local += v;
  }
  g_edit_local -= g_edit_local % 60;
}

// The date this firmware was built, at noon: a much better start than January when the clock is unset.
int64_t build_date_noon() {
  static const char *const kMon = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *d = __DATE__;  // "Oct 21 2026"
  int m = 1;
  for (int i = 0; i < 12; i++)
    if (strncmp(d, kMon + i * 3, 3) == 0) m = i + 1;
  int day = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0');
  int y = (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + (d[10] - '0');
  return seconds_from_civil(y, m, day, 12, 0, 0);
}

void edit_done(lv_event_t *e) {
  bool save = (intptr_t)lv_event_get_user_data(e) == 1;
  if (save && g_act) g_act->set_clock_local(g_edit_local);
  open_settings();
}

// --- Test panel ---

void test_action(lv_event_t *e) {
  if (g_act) g_act->demo((DemoAction)(intptr_t)lv_event_get_user_data(e));
}

void back_to_settings(lv_event_t *) { open_settings(); }

// --- The Zambretti demo (Mike, 2026-10-25) ---
// Every forecast WeatherGate can send, with a reading to suit it, and its two warnings: the list
// where the floor tiles are, the real outside tile showing the one chosen, and the time of day
// (the moon for the sun at night) and Back where the doorbell is.

struct ZSample {
  char letter;        // A-Z, or 0 for the extras below
  int16_t temp;       // whole degrees F
  uint8_t hum;        // percent
  Forecast code = Forecast::Unknown;  // the extras' (the letters' come from the table)
  const char *text = nullptr;         // the extras'
  uint8_t warn = 0;                   // kOutsideDangerHeat, kOutsideDangerCold
  const char *name = nullptr;         // the extras' button words
};
const ZSample kZ[] = {
    {'A', 74, 38}, {'B', 72, 42}, {'C', 70, 50}, {'D', 68, 58}, {'E', 63, 72}, {'F', 61, 76}, {'G', 57, 86},
    {'H', 55, 90}, {'I', 53, 94}, {'J', 76, 36}, {'K', 73, 40}, {'L', 69, 55}, {'M', 66, 64}, {'N', 64, 68},
    {'O', 60, 74}, {'P', 58, 80}, {'Q', 56, 88}, {'R', 52, 95}, {'S', 62, 97}, {'T', 78, 34}, {'U', 75, 39},
    {'V', 67, 52}, {'W', 65, 56}, {'X', 66, 60}, {'Y', 62, 66}, {'Z', 63, 62},
    {0, 29, 86, Forecast::Snow, "Snow at Times, Worse Later", 0, "Snow (rain below freezing)"},
    {0, 101, 48, Forecast::Sun, "Settled Fine Weather", kOutsideDangerHeat, "Danger: heat"},
    {0, 14, 70, Forecast::Snow, "Snow at Times, Worse Later", kOutsideDangerCold, "Danger: cold"},
};
constexpr int kZCount = sizeof(kZ) / sizeof(kZ[0]);
int g_z = 0;
bool g_z_night = false;
lv_obj_t *g_z_btn[kZCount] = {};
lv_obj_t *g_z_time[2] = {};

void z_fill(int i, OutsideView &ov) {
  const ZSample &z = kZ[i];
  ov = OutsideView();
  ov.valid = true;
  ov.code = z.letter ? zambretti_symbol(z.letter) : z.code;
  ov.heat = z.warn & kOutsideDangerHeat;
  ov.cold = z.warn & kOutsideDangerCold;
  snprintf(ov.temp, sizeof(ov.temp), "%d\xC2\xB0" "F", z.temp);
  snprintf(ov.hum, sizeof(ov.hum), "%u%%", (unsigned)z.hum);
  snprintf(ov.phrase, sizeof(ov.phrase), "%s", z.letter ? zambretti_text(z.letter) : z.text);
}

void z_style() {
  Color k = ink();
  for (int i = 0; i < kZCount; i++) {
    if (!g_z_btn[i]) continue;
    bool on = i == g_z;
    lv_obj_set_style_bg_color(g_z_btn[i], C(on ? k : chip()), 0);
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(g_z_btn[i]); c++)
      set_text_color(lv_obj_get_child(g_z_btn[i], c), on ? theme::sheet_bg(g_mode) : k);
  }
  theme::StatusColors open = theme::status_colors(0, g_mode);
  for (int i = 0; i < 2; i++) {
    bool on = (i == 1) == g_z_night;
    lv_obj_set_style_bg_color(g_z_time[i], C(on ? open.tile : chip()), 0);
    set_text_color(lv_obj_get_child(g_z_time[i], 0), on ? open.text : k);
  }
}

void z_pick(lv_event_t *e) {
  g_z = (int)(intptr_t)lv_event_get_user_data(e);
  z_style();
}

void z_time(lv_event_t *e) {
  g_z_night = (intptr_t)lv_event_get_user_data(e) == 1;
  z_style();
}

// --- Ringtones ---

lv_obj_t *g_ring_btn[kRingtoneCount] = {};  // by list position
int g_ring_shown = -1;                       // the id shown as chosen

// The chosen ringtone wears the RINGTONE tile's color and a check mark.
void ring_style(int id, bool chosen) {
  int i = id >= 0 ? ringtone_index((uint8_t)id) : -1;
  lv_obj_t *b = i >= 0 ? g_ring_btn[i] : nullptr;
  if (!b) return;
  Color face = chosen ? theme::settings_tile(kRing, g_mode) : chip();
  Color text = !chosen ? ink() : (g_mode == Mode::Night ? hex(0xE4D8C0) : hex(0xFBF1DC));
  lv_obj_set_style_bg_color(b, C(face), 0);
  pressable(b, face, 90);
  set_text_color(lv_obj_get_child(b, 0), text);
  lv_obj_t *check = lv_obj_get_child(b, 1);
  set_text_color(check, text);
  show(check, chosen);
}

// A tap chooses the ringtone and plays it (cutting short the one before).
void ring_pick(lv_event_t *e) {
  Settings s = g_set;
  s.ringtone = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  apply(s);
  if (g_act) g_act->preview(Sound::Chime, s.volume);
}

// Cleaning ends early only on a deliberate 1.5 s hold of Done: a wipe across it must not end it.
void clean_done(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) g_done_down = lv_tick_get();
  else if (code == LV_EVENT_PRESSING && lv_tick_elaps(g_done_down) >= 1500) close_sheet();
}

}  // namespace

void open_settings() {
  lv_obj_t *root = new_sheet(Sheet::Settings, false);
  g_confirm = nullptr;
  build_settings(root);
  refresh_sheet();
}

void open_time_date() {
  lv_obj_t *root = new_sheet(Sheet::TimeDate, false);
  Color k = ink();
  lv_obj_t *bg = box(root, 0, 74, 800, 406, theme::sheet_bg(g_mode), 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  g_edit_local = g_f.wall.valid ? (int64_t)g_f.wall.utc + g_f.utc_offset : build_date_noon();
  g_edit_local -= g_edit_local % 60;
  label(bg, B16, k, 16, 14, "SET THE CLOCK BY HAND");
  g_edit_date = label(bg, J30, k, 16, 38, "");
  g_edit_time = label(bg, J44, k, 520, 30, "");
  const int64_t steps[8] = {-1, 1, -86400, 86400, -3600, 3600, -60, 60};
  const char *labels[8] = {"- month", "+ month", "- day", "+ day", "- hour", "+ hour", "- min", "+ min"};
  for (int i = 0; i < 8; i++) {  // columns: month, day, hour, minute; minus above plus; hold to repeat
    lv_obj_t *b = button(bg, 12 + (i / 2) * 194, 96 + (i % 2) * 102, 182, 90, chip(), k, labels[i], B24, edit_step,
                         (void *)(intptr_t)steps[i]);
    lv_obj_add_event_cb(b, edit_step, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)steps[i]);
  }
  theme::StatusColors open = theme::status_colors(0, g_mode);
  button(bg, 12, 306, 250, 90, chip(), k, "Cancel", J30, edit_done, (void *)0);
  button(bg, 274, 306, 514, 90, open.tile, open.text, "Save", J30, edit_done, (void *)1);
  refresh_sheet();
}

// MIKE (2026-10-24): RINGTONE opens a scrolling list of every ringtone, grouped by kind.
void open_ringtones() {
  lv_obj_t *root = new_sheet(Sheet::Ringtone, false);
  Color k = ink();
  lv_obj_t *bg = box(root, 0, 74, 800, 406, theme::sheet_bg(g_mode), 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(bg, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(bg, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_bottom(bg, 12, 0);
  label(bg, B20, k, 16, 12, "Tap one to hear it. The one you tap rings the doorbell.");
  int y = 50;
  const ToneGroup groups[2] = {ToneGroup::Standard, ToneGroup::Nature};
  for (ToneGroup g : groups) {
    char head[16];
    snprintf(head, sizeof(head), "%s", tone_group_name(g));
    for (char *c = head; *c; c++)
      if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    lv_obj_t *h = label(bg, B16, k, 16, y, head);
    lv_obj_set_style_text_letter_space(h, 2, 0);
    y += 28;
    int col = 0;
    for (int i = 0; i < kRingtoneCount; i++) {
      uint8_t id = ringtone_id(i);
      if (ringtone_group(id) != g) continue;
      lv_obj_t *b = button(bg, 12 + col * 394, y, 382, 90, chip(), k, ringtone_name(id), B24, ring_pick,
                           (void *)(intptr_t)id);
      lv_obj_t *check = label(b, &lv_font_montserrat_26, k, 0, 0, LV_SYMBOL_OK);
      lv_obj_align(check, LV_ALIGN_LEFT_MID, 24, -3);
      show(check, false);
      g_ring_btn[i] = b;
      if (++col == 2) {
        col = 0;
        y += 102;
      }
    }
    if (col) y += 102;
    y += 8;
  }
  button(bg, 12, y, 776, 90, chip(), k, "Back to Settings", J30, back_to_settings, nullptr);
  g_ring_shown = -1;
  refresh_sheet();
}

void open_zambretti_demo() {
  lv_obj_t *root = new_sheet(Sheet::Zambretti, false);
  Color k = ink();
  memset(g_z_btn, 0, sizeof(g_z_btn));
  // The list, over the floor tiles.
  lv_obj_t *list = box(root, 12, 74, 514, 396, theme::sheet_bg(g_mode), 24);
  lv_obj_add_flag(list, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_bottom(list, 12, 0);
  label(list, B20, k, 16, 12, "Zambretti forecasts");
  label(list, B16, k, 16, 40, "Tap one: the outside tile shows it.");
  for (int i = 0; i < kZCount; i++) {
    const ZSample &z = kZ[i];
    int x = 12 + (i % 2) * 249, y = 70 + (i / 2) * 98;
    lv_obj_t *b = button(list, x, y, 241, 90, chip(), k, "", B16, z_pick, (void *)(intptr_t)i);
    char head[40], sub[48];
    if (z.letter) snprintf(head, sizeof(head), "%c", z.letter);
    else snprintf(head, sizeof(head), "%s", z.name);
    snprintf(sub, sizeof(sub), "%d\xC2\xB0" "F  %u%%", z.temp, (unsigned)z.hum);
    lv_obj_t *h = label(b, z.letter ? J30 : B16, k, 12, z.letter ? 6 : 8, head);
    lv_obj_t *t = label(b, B14, k, z.letter ? 48 : 12, z.letter ? 10 : 32, z.letter ? zambretti_text(z.letter) : sub);
    if (z.letter) {
      lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(t, 241 - 48 - 10);
      label(b, B14, k, 12, 62, sub);
    }
    (void)h;
    g_z_btn[i] = b;
  }
  // Day or night, and back, over the doorbell.
  lv_obj_t *ctl = box(root, 538, 278, 250, 192, theme::sheet_bg(g_mode), 24);
  lv_obj_add_flag(ctl, LV_OBJ_FLAG_CLICKABLE);
  g_z_time[0] = button(ctl, 4, 4, 119, 90, chip(), k, "Day", B24, z_time, (void *)0);
  g_z_time[1] = button(ctl, 127, 4, 119, 90, chip(), k, "Night", B24, z_time, (void *)1);
  button(ctl, 4, 98, 242, 90, chip(), k, "Back", J30, back_to_settings, nullptr);
  z_style();
}

void open_test_panel() {
  lv_obj_t *root = new_sheet(Sheet::Test, false);
  Color k = ink();
  lv_obj_t *bg = box(root, 0, 74, 800, 406, theme::sheet_bg(g_mode), 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(bg, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(bg, LV_SCROLLBAR_MODE_OFF);
  // The two display tests on top (Mike, 2026-10-22: streaks on redraws), then two columns of demo
  // actions; the mail one and Back are below them, a short scroll down.
  for (int i = 0; i < 11; i++) {
    int row = i == 10 ? 5 : i >= 8 ? 0 : i / 2 + 1;
    lv_obj_t *b = button(bg, 12 + (i % 2) * 394, 12 + row * 102, 382, 90, chip(), k, "", B20, test_action,
                         (void *)(intptr_t)i);
    g_test_lbl[i] = label(b, B20, k, 0, 0, "");
    lv_obj_set_width(g_test_lbl[i], 350);
    lv_obj_set_style_text_align(g_test_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_test_lbl[i], LV_LABEL_LONG_WRAP);
    lv_obj_align(g_test_lbl[i], LV_ALIGN_CENTER, 0, -3);
  }
  button(bg, 406, 12 + 5 * 102, 382, 90, chip(), k, "Back to Settings", J30, back_to_settings, nullptr);
  refresh_sheet();
}

void start_clean_screen() {
  lv_obj_t *root = new_sheet(Sheet::CleanScreen, false);
  // A flat dark panel on the night palette; every touch lands here and does nothing.
  lv_obj_t *bg = box(root, 0, 0, 800, 480, hex(0x1B2620), 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  label(bg, J44, hex(0xE4D8C0), 0, 150, "Cleaning mode");
  lv_obj_t *t = lv_obj_get_child(bg, 0);
  lv_obj_set_x(t, 400 - text_width("Cleaning mode", J44) / 2);
  g_clean_lbl = label(bg, B24, hex(0x9AA89A), 0, 214, "");
  lv_obj_t *done = button(bg, 250, 372, 300, 90, hex(0x2F5A3A), hex(0xE4D8C0), "Hold to finish", J30, nullptr, nullptr);
  lv_obj_add_event_cb(done, clean_done, LV_EVENT_ALL, nullptr);
  g_done_down = lv_tick_get();
  g_clean_start = lv_tick_get();
  if (g_act) g_act->clean_screen(true);
  refresh_sheet();
}

bool zambretti_preview(OutsideView &ov, bool &night) {
  if (g_sheet != Sheet::Zambretti) return false;
  z_fill(g_z, ov);
  night = g_z_night;
  return true;
}

void refresh_sheet() {
  uint32_t idle = lv_disp_get_inactive_time(nullptr);
  switch (g_sheet) {
    case Sheet::Status:
      if (idle > 15000) close_sheet();  // PROPOSED: auto-close after 15 s
      break;
    case Sheet::Timer:
      if (idle > 30000) close_sheet();
      break;
    case Sheet::Settings:
      if (idle > 30000) { close_sheet(); break; }  // PROPOSED: back to the main view after 30 s
      for (int id = 0; id < kTiles; id++) {
        if (!g_val[id]) continue;
        char v[64];
        tile_value(id, v, sizeof(v));
        set_text(g_val[id], v);
      }
      break;
    case Sheet::TimeDate: {
      if (idle > 60000) { close_sheet(); break; }
      char d[32], t[16];
      format_date_long(d, sizeof(d), g_edit_local);
      format_time12(t, sizeof(t), g_edit_local);
      Civil c = civil_from_seconds(g_edit_local);
      char dy[48];
      snprintf(dy, sizeof(dy), "%s, %d", d, c.year);
      set_text(g_edit_date, dy);
      set_text(g_edit_time, t);
      set_x(g_edit_time, 788 - text_width(t, J44));
      break;
    }
    case Sheet::Test: {
      if (idle > 60000) { close_sheet(); break; }
      DemoInfo d = g_act ? g_act->demo_info() : DemoInfo();
      static const char *const kAnswer[] = {"Come on", "Not now", "Off"};
      static const char *const kPhase[] = {"Night", "Dawn", "Morning", "Midday", "Golden hour", "Dusk", "follow the clock"};
      char b[11][64];
      snprintf(b[0], 64, "Other floor: next status");
      snprintf(b[1], 64, d.unplugged ? "Plug the other panel back in" : "Unplug the other panel");
      snprintf(b[2], 64, "Other floor rings this panel");
      snprintf(b[3], 64, "Other floor's dog in or out");
      snprintf(b[4], 64, "LucyGate: Lucy in or out");
      snprintf(b[5], 64, "WeatherGate: next reading");
      snprintf(b[6], 64, "Other floor answers: %s", kAnswer[d.auto_answer % 3]);
      snprintf(b[7], 64, "Sky: %s", kPhase[d.phase < 0 || d.phase > 6 ? 6 : d.phase]);
      snprintf(b[8], 64, "%s", d.display[0] ? d.display : "Display test (panel only)");
      snprintf(b[9], 64, "Strip drawing: %s", d.strips ? "on" : "off");
      snprintf(b[10], 64, "Other floor taps the mail badge");
      for (int i = 0; i < 11; i++)
        if (g_test_lbl[i]) set_text(g_test_lbl[i], b[i]);
      break;
    }
    case Sheet::Ringtone:
      if (idle > 60000) { close_sheet(); break; }
      if (g_set.ringtone != g_ring_shown) {
        ring_style(g_ring_shown, false);
        g_ring_shown = g_set.ringtone;
        ring_style(g_ring_shown, true);
      }
      break;
    case Sheet::CleanScreen: {
      uint32_t left_ms = 30000 - (lv_tick_elaps(g_clean_start) > 30000 ? 30000 : lv_tick_elaps(g_clean_start));
      if (left_ms == 0) { close_sheet(); break; }
      char t[48];
      snprintf(t, sizeof(t), "Wipe away: touch is off for %u s", (unsigned)((left_ms + 999) / 1000));
      set_text(g_clean_lbl, t);
      set_x(g_clean_lbl, 400 - text_width(t, B24) / 2);
      break;
    }
    default:
      break;
  }
}

}  // namespace detail

void zambretti_pick(int i, bool night) {
  detail::open_zambretti_demo();
  detail::g_z = i < 0 || i >= detail::kZCount ? 0 : i;
  detail::g_z_night = night;
  detail::z_style();
}
}  // namespace ui
