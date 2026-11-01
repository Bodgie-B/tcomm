// Main screen (GUI build spec of 2026-10-24 and the Pd boards), the doorbell banner and notices,
// and the render loop.
// render() runs every 50 ms: everything here changes LVGL objects only when a value actually
// changed, because in LVGL even setting the same text or flag redraws the area.
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include "../core/brightness.h"
#include "board_art.h"
#include "ui_internal.h"

namespace ui {

using namespace detail;
using namespace tc;
using gfx::Color;
using gfx::hex;

namespace {

View g_prev = {};
bool g_have_prev = false;

struct FloorTile {
  Floor floor;
  int slot;  // its ArtSlot
  lv_obj_t *cont, *img_bot, *img_top, *floor_lbl, *word, *line, *chev, *chev_img, *badge, *inside, *temp, *hum, *drop, *zone;
  Art art;  // on screen; a new one is drawn into the spare, then faded in (see Spare)
  Art badge_art, chev_art, drop_art;
  // The mail badge, on the DOWNSTAIRS tile only: its touch area, the outline and the solid one.
  lv_obj_t *mail = nullptr, *mail_ghost = nullptr, *mail_solid = nullptr;
  Art mail_ghost_art, mail_solid_art;
  int mail_mode = -1;  // see mail_render
  int key = -1;      // what the front art shows (status * 4 + mode); -1 = nothing yet
  FloorView shown = {};  // the words that go with it, kept until the new art is ready
  int font_ns = -1;  // which state-word font is set: 1 = the 54 px one, for words over 8 letters
  int mine = -1;
};

FloorTile g_tile[2];  // [0] UPSTAIRS on top, [1] DOWNSTAIRS below, whichever unit this is
// The big pictures, one copy each; the doorbell's [0] is plain and [1] pressed.
Art g_sky, g_bell_art[2], g_outside_art;

// One spare picture for all of them (2026-11-01). The drawing task paints whichever is changing
// into the spare; then the sky trades memory with it and the others are copied in (a tile's fade
// shows the spare itself until it's over). Each used to keep a second copy of its own for this:
// 1.8 MB of PSRAM, against 0.77 MB for the spare.
struct Spare {
  uint8_t *mem = nullptr;  // the sky's size, enough for any of the others
  float *row[2] = {};      // scanline buffers, one per view
  gfx::Canvas view[2];     // what's being drawn (the doorbell draws its two pictures at once)
  lv_img_dsc_t dsc = {};   // view[0] as an image, for a tile's fade
  int owner = -1;          // the ArtSlot using it, or -1
};
Spare g_spare;
void (*g_copy_pause)() = nullptr;

bool spare_begin() {
  g_spare.mem = (uint8_t *)gfx::alloc_bytes((size_t)art::kScreenW * art::kScreenH * 2);
  for (float *&r : g_spare.row) r = (float *)gfx::alloc_bytes(gfx::Canvas::scanline_bytes(art::kScreenW));
  return g_spare.mem && g_spare.row[0] && g_spare.row[1];
}

// The spare for slot, its view 0 (and for the doorbell view 1, after it) set to w x h and cleared;
// null while another picture has it.
gfx::Canvas *spare_claim(int slot, int w, int h, bool alpha) {
  if (g_spare.owner >= 0 || !g_spare.mem || !g_spare.row[0] || !g_spare.row[1]) return nullptr;
  g_spare.owner = slot;
  g_spare.view[0].view(g_spare.mem, g_spare.row[0], w, h, alpha);
  if (slot == kSlotBell) g_spare.view[1].view(g_spare.mem + g_spare.view[0].bytes(), g_spare.row[1], w, h, alpha);
  return &g_spare.view[0];
}

void spare_release() { g_spare.owner = -1; }

// Copies view k into a picture's own memory a few KB at a time, pausing between, so the screen's
// own reads from PSRAM aren't starved (that shows as streaks); then shows it in img, if given.
void spare_copy(Art &a, int k, lv_obj_t *img) {
  const uint8_t *src = g_spare.view[k].data();
  uint8_t *dst = a.cv.data();
  size_t n = a.cv.bytes();
  for (size_t i = 0; i < n; i += 4096) {
    memcpy(dst + i, src + i, n - i < 4096 ? n - i : 4096);
    if (g_copy_pause) g_copy_pause();
  }
  if (img) a.show(img);
}

// View 0 shown in img, for a tile's fade.
void spare_show(lv_obj_t *img) {
  const gfx::Canvas &v = g_spare.view[0];
  lv_img_dsc_t &d = g_spare.dsc;
  memset(&d, 0, sizeof(d));
  d.header.w = (uint32_t)v.width();
  d.header.h = (uint32_t)v.height();
  d.header.cf = v.has_alpha() ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;
  d.data_size = (uint32_t)v.bytes();
  d.data = v.data();
  lv_img_cache_invalidate_src(&d);
  lv_img_set_src(img, &d);
  lv_obj_invalidate(img);
}
Art g_strip, g_paw_in, g_paw_out, g_gear_art, g_close_art, g_fc_art, g_rays_art, g_odrop_art, g_ice_art;
lv_obj_t *g_sky_img, *g_strip_img, *g_date, *g_time, *g_hol, *g_dog, *g_dog_disc, *g_dog_paw, *g_dog_lbl;
lv_obj_t *g_gear, *g_gear_img, *g_gear_arc;
lv_obj_t *g_out_cont, *g_out_img, *g_out_title, *g_out_fc, *g_out_rays, *g_out_phrase, *g_out_temp, *g_out_hum, *g_out_drop;
lv_obj_t *g_ice_img;  // WeatherGate's danger cold: icicles over the top of the screen
// The calendar's chevron on the date strip (calendar spec 10): down while closed, up while open.
lv_obj_t *g_cal_chev, *g_cal_chev_img;
Art g_chev_up;
int g_cal_chev_state = -1;
bool g_ice_drawn = false;
lv_obj_t *g_bell_cont, *g_bell_img, *g_bell_word, *g_bell_line;
// The bell itself, over the tile's picture: it shakes for each ring sent or received, and it's red
// while an incoming ring waits (Mike, 2026-10-26).
lv_obj_t *g_bell_pic;
Art g_bell_pic_art;
int g_bell_pic_key = -1;
uint16_t g_bell_shakes = 0;
lv_obj_t *g_toast, *g_toast_lbl, *g_wake;
int g_sky_phase = -1;
// The boards lay three faint rings around the sun or moon over everything on the main screen, 1.5
// to 5 percent each. As LVGL objects they would be lost: its 16-bit blending rounds a step that
// small away over the tiles and doubles it over the sky. So they are blended here, the three as
// one, into each area LVGL redraws, through tables made in 8 bits per channel.
struct Glow {
  bool on = false;
  int cx = 0, cy = 0, r[3] = {};    // outer to inner
  uint8_t lut[3][3][64];            // [rings over the pixel - 1][red, green, blue][565 value]
} g_glow;
lv_obj_t *g_glow_obj;
bool g_sky_moon = false;
float g_sky_p = -1;
uint32_t g_sky_at = 0;
int g_strip_w = 0, g_strip_key = -1, g_strip_icon = -1, g_gear_icon = -1;
int g_out_mode = -1, g_fc_key = -1, g_bell_mode = -1, g_bell_warn = -1;
constexpr int kPhraseW = 124;  // the outside tile's text column, beside the icon
char g_phrase_src[kMaxPhrase + 1] = "";  // the forecast text shown, before wrapping
bool g_phrase_set = false;
BannerKind g_banner_kind = BannerKind::None;
bool g_gear_consumed = false;
uint32_t g_gear_down = 0;
int g_dog_from = 56;
int g_dog_out = -1;

void center_x(lv_obj_t *o, int cx, int w) { set_x(o, cx - w / 2); }

// ---------------------------------------------------------------------------------------------
// Floor tiles

void zone_clicked(lv_event_t *) { open_status_sheet(); }

void mail_clicked(lv_event_t *) {
  if (g_act) g_act->tap_mail();
}

// The mail badge (Mike, 2026-10-26): in the status zone's top right corner, on the DOWNSTAIRS
// word's line, as far from the top as from the zone's right edge (Mike, the same day). That makes
// it concentric with the corner's 24 px rounding (art.cpp), 7 px in all round, and keeps it clear
// of the weather zone. Tile coordinates of its centre.
constexpr int kMailInset = 7;
constexpr int kMailCx = art::kStatusW - kMailInset - art::kMail / 2, kMailCy = kMailInset + art::kMail / 2;
// Its touch area: 90 x 90, like the gear and dog button (design brief 4), in the zone's top right
// corner. Downstairs, while mail waits, that corner of the own status zone takes the mail tap.
constexpr int kMailHitW = 90, kMailHitH = 90, kMailHitX = art::kStatusW - kMailHitW;
constexpr lv_opa_t kMailGhostOpa = 102;        // the faint outline upstairs: 40 percent

void tile_build(FloorTile &t, Floor f, int x, int y) {
  t.floor = f;
  t.slot = f == Floor::Upstairs ? kSlotTileUp : kSlotTileDown;
  t.cont = plain(lv_scr_act(), x, y, art::kFloorW, art::kFloorH);
  show(t.cont, false);  // until its first art is drawn
  t.art.make(art::kFloorW, art::kFloorH, true);
  t.img_bot = image(t.cont, 0, 0);
  t.img_top = image(t.cont, 0, 0);
  show(t.img_top, false);
  // Spec 4a, tile coordinates. The y values are the tops of the boards' line boxes; board_y moves
  // each to where LVGL needs the label for the same baseline.
  t.floor_lbl = label(t.cont, B16, hex(0xFBF1DC), 24, board_y(B16, 14.4f), floor_word(f));
  lv_obj_set_style_text_letter_space(t.floor_lbl, 3, 0);  // 0.18 em (2.88 px)
  t.word = label(t.cont, J64, hex(0xFBF1DC), 24, board_y(J64, 25.6f));
  t.line = label(t.cont, B20, hex(0xFBF1DC), 24, board_y(B20, 153.4f));
  // The own tile's chevron: a 40 px circle of black at 22 percent with the caret in it, 14 px after
  // the since line, centred on it (tile y 166). The whole status zone is the touch target.
  t.chev = box(t.cont, 0, 146, 40, 40, hex(0), LV_RADIUS_CIRCLE);
  lv_obj_set_style_bg_opa(t.chev, 56, 0);
  t.chev_art.make(art::kCaret, art::kCaret, true);
  t.chev_img = image(t.chev, 12, 12);
  t.badge_art.make(art::kBadge, art::kBadge, true);
  t.badge = image(t.cont, 289, 105);
  // Spec 4b: the weather zone, centred on tile x 450 (screen 462).
  t.inside = label(t.cont, B14, hex(0), 0, board_y(B14, 21.2f), "INSIDE");
  lv_obj_set_style_text_letter_space(t.inside, 2, 0);  // 0.16 em (2.24 px)
  center_x(t.inside, 450, text_width("INSIDE", B14, 2));
  t.temp = label(t.cont, J50, hex(0), 0, board_y(J50, 51.6f));
  t.drop_art.make(15, 19, true);
  t.drop = image(t.cont, 0, 144);  // centred on the humidity figure's line box
  t.hum = label(t.cont, B26, hex(0), 0, board_y(B26, 137));
  // On the own tile the whole status zone is the touch target (brief 10.2), not just the chevron.
  t.zone = plain(t.cont, 0, 0, art::kStatusW, art::kFloorH);
  lv_obj_add_event_cb(t.zone, zone_clicked, LV_EVENT_CLICKED, nullptr);
  // After the zone, so a tap on the badge is the badge's (downstairs, the zone is the own tile's).
  if (f == Floor::Downstairs) {
    t.mail = plain(t.cont, kMailHitX, 0, kMailHitW, kMailHitH);
    lv_obj_add_flag(t.mail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t.mail, mail_clicked, LV_EVENT_CLICKED, nullptr);
    t.mail_ghost_art.make(art::kMail, art::kMail, true);
    t.mail_solid_art.make(art::kMail, art::kMail, true);
    t.mail_ghost = image(t.mail, kMailCx - kMailHitX - art::kMail / 2, kMailCy - art::kMail / 2);
    t.mail_solid = image(t.mail, kMailCx - kMailHitX - art::kMail / 2, kMailCy - art::kMail / 2);
    show(t.mail, false);
  }
}

void tile_fade_done(lv_anim_t *a) {
  lv_obj_t *top = (lv_obj_t *)a->var;
  for (FloorTile &t : g_tile) {
    if (t.img_top != top) continue;
    spare_copy(t.art, 0, t.img_bot);  // the new picture into the tile's own, under the faded-in spare
    show(t.img_top, false);
    lv_obj_set_style_opa(t.img_top, LV_OPA_COVER, 0);
    spare_release();
  }
}

// The background drawing of a tile is done: show it with its badge, icons and text colors.
void tile_apply(FloorTile &t, int key) {
  int st = key / 4;
  Mode m = (Mode)(key % 4);
  theme::StatusColors sc = theme::status_colors(st, m);
  const theme::Palette &pal = theme::palette(m);
  bool first = t.key < 0;
  if (first || t.key % 4 != key % 4) {  // first time, or day/night changed: no fade
    spare_copy(t.art, 0, t.img_bot);
    spare_release();
  } else {  // brief 10.1: the tile fades to the new color over about 250 ms; the spare is the new one
    spare_show(t.img_top);
    lv_obj_set_style_opa(t.img_top, LV_OPA_TRANSP, 0);
    show(t.img_top, true);
    anim(t.img_top, 0, 255, 250, set_opa, tile_fade_done);
  }
  {
    art::badge(t.badge_art.cv, st, sc.text, sc.tile);  // shape in the text colour, glyph in the tile's
    t.badge_art.show(t.badge);
    art::icon(t.chev_art.cv, art::Icon::Caret, sc.text);
    t.chev_art.show(t.chev_img);
    art::icon(t.drop_art.cv, art::Icon::DropIn, pal.humidity);
    t.drop_art.show(t.drop);
    if (t.mail) {  // in the tile's text colour, like the status badge, the envelope in the tile's
      art::mail(t.mail_ghost_art.cv, false, sc.text, sc.tile);
      t.mail_ghost_art.show(t.mail_ghost);
      art::mail(t.mail_solid_art.cv, true, sc.text, sc.tile);
      t.mail_solid_art.show(t.mail_solid);
    }
    set_text_color(t.floor_lbl, sc.text);
    set_text_color(t.word, sc.text);
    set_text_color(t.line, sc.text);
    set_text_color(t.inside, sc.ink);
    set_text_color(t.temp, sc.ink);
    set_text_color(t.hum, pal.humidity);
  }
  t.key = key;
  if (first) show(t.cont, true);
}

void tile_render(FloorTile &t, const FloorView &fv) {
  int st = fv.no_signal ? theme::kNoSignal : (int)fv.status;
  int key = st * 4 + (int)g_mode;
  ArtSlot &slot = g_art[t.slot];
  ArtJob done;
  if (slot.take(done)) {
    if (done.key == key) tile_apply(t, key);  // it lets the spare go once the picture is copied in
    else spare_release();                     // a result that's out of date is dropped
  }
  // A fade cut short never calls tile_fade_done: finish its work here, or the spare stays taken.
  if (g_spare.owner == t.slot && slot.idle() && !lv_anim_get(t.img_top, nullptr)) {
    spare_copy(t.art, 0, t.img_bot);
    show(t.img_top, false);
    lv_obj_set_style_opa(t.img_top, LV_OPA_COVER, 0);
    spare_release();
  }
  // Ask for the new tile in the background. Not while a fade shows both canvases.
  if (key != t.key && slot.idle() && !lv_anim_get(t.img_top, nullptr)) {
    if (gfx::Canvas *cv = spare_claim(t.slot, art::kFloorW, art::kFloorH, true)) {
      ArtJob j;
      j.kind = ArtKind::Floor;
      j.cv = cv;
      j.floor = t.floor;
      j.status = st;
      j.mode = g_mode;
      j.key = key;
      slot.submit(j);
    }
  }
  // Until the new art is ready the tile keeps its old words, so words and colors change together.
  if (key == t.key) t.shown = fv;
  const FloorView &u = t.shown;
  // The other floor's tile and the weather zones don't respond to touch (handoff).
  if (t.mine != (int)fv.mine) {
    if (fv.mine) lv_obj_add_flag(t.zone, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_clear_flag(t.zone, LV_OBJ_FLAG_CLICKABLE);
    t.mine = fv.mine;
  }
  // Spec 4a: the state word at 64 px; one longer than 8 letters (NO SIGNAL) at 54 px so it fits.
  int ns = strlen(u.word) > 8 ? 1 : 0;
  if (t.font_ns != ns) {
    lv_obj_set_style_text_font(t.word, ns ? J54 : J64, 0);
    lv_obj_set_y(t.word, ns ? board_y(J54, 35) : board_y(J64, 25.6f));
    t.font_ns = ns;
  }
  set_text(t.word, u.word);
  bool line_changed = strcmp(lv_label_get_text(t.line), u.line) != 0;
  set_text(t.line, u.line);
  show(t.chev, u.mine);
  if (u.mine && line_changed) set_x(t.chev, 24 + text_width(u.line, B20) + 14);
  if (strcmp(lv_label_get_text(t.temp), u.temp) != 0) {
    set_text(t.temp, u.temp);
    center_x(t.temp, 450, text_width(u.temp, J50));
  }
  if (strcmp(lv_label_get_text(t.hum), u.hum) != 0) {  // the drop, 3 px, the figure: centred as one
    set_text(t.hum, u.hum);
    int row = 15 + 3 + text_width(u.hum, B26);
    set_x(t.drop, 450 - row / 2);
    set_x(t.hum, 450 - row / 2 + 18);
  }
}

void mail_opa(void *o, int32_t v) { lv_obj_set_style_img_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

// The mail badge: 0 hidden; 1 the faint outline (upstairs, nothing waiting); 2 solid (upstairs,
// waiting); 3 downstairs, waiting: the solid one fading to the outline and back, slowly, until the
// tenant taps it (Mike, 2026-10-26).
void mail_render(const MailView &mv) {
  FloorTile &t = g_tile[1];
  if (!t.mail) return;
  int mode = !mv.show ? 0 : !mv.solid ? 1 : mv.pulse ? 3 : 2;
  if (mode == t.mail_mode) return;
  t.mail_mode = mode;
  lv_anim_del(t.mail_solid, mail_opa);
  show(t.mail, mode != 0);
  show(t.mail_ghost, mode == 1 || mode == 3);
  show(t.mail_solid, mode >= 2);
  mail_opa(t.mail_ghost, mode == 1 ? kMailGhostOpa : (lv_opa_t)LV_OPA_COVER);
  mail_opa(t.mail_solid, LV_OPA_COVER);
  if (mode != 3) return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, t.mail_solid);
  lv_anim_set_exec_cb(&a, mail_opa);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_time(&a, 1800);
  lv_anim_set_playback_time(&a, 1800);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

// ---------------------------------------------------------------------------------------------
// Header, Outside, Doorbell

void dog_clicked(lv_event_t *) {
  if (g_act) g_act->tap_dog();
}

void bell_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) g_bell_art[1].show(g_bell_img);
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) g_bell_art[0].show(g_bell_img);
  else if (code == LV_EVENT_CLICKED && g_act) g_act->tap_bell();
}

// Gear: tap opens Settings (or closes it, as an X); hold 2 s for clean-screen mode with a ring
// filling around the gear (brief 10.8, PROPOSED trigger).
void gear_tick(lv_timer_t *) {
  if (!lv_obj_has_state(g_gear, LV_STATE_PRESSED) || g_gear_consumed) {
    show(g_gear_arc, false);
    return;
  }
  uint32_t held = lv_tick_elaps(g_gear_down);
  if (held > 250) {
    show(g_gear_arc, true);
    lv_arc_set_value(g_gear_arc, (int16_t)(held >= 2000 ? 100 : held / 20));
  }
  if (held >= 2000) {
    g_gear_consumed = true;
    show(g_gear_arc, false);
    start_clean_screen();
  }
}

// The Settings roll-down (Mike, 2026-10-26): the gear opens Settings by unrolling it from under the
// header like a scroll, the gear turning counterclockwise as if cranking it down, and closes it by
// rolling it back up, turning clockwise. The sheet is built in place at its full height with
// LVGL's redraws paused (gear_tapped), then its root's bottom edge is pulled up to the header, so
// building it changes nothing on screen. The edge then moves down a band at a time, with a paper
// roller riding it: LVGL clips the sheet to its root, and each frame redraws only the band
// uncovered or covered since the last. The edge is set on the root's coordinates directly (setting
// its height would redraw the whole root every frame); the root keeps its full height as its
// style, so at the bottom the two agree and nothing more is redrawn. The panel draws about 1.3
// pixels a microsecond whatever they are, so pixels drawn is the cost. Measured through
// anim_report (2026-10-26: the calendar's roll moved the whole panel, 5 fps; a draw mask, 6 fps; a
// root grown from short, 12 fps and a 300 ms frame at the end; built full but redrawn whole, 8 fps).
constexpr int kRollTop = 74, kRollBottom = 480, kRollerH = 16;
constexpr uint32_t kUnrollMs = 520, kRollUpMs = 420;
constexpr int kCrank = 4500;  // the gear's turn over a roll: 1.25 turns, in tenths of a degree

struct Unroll {
  bool active = false, down = false;
  lv_obj_t *root = nullptr, *roller = nullptr;
  int edge = kRollTop;  // the screen y the sheet shows down to
  uint32_t start = 0;
  int frames = 0;
} g_sheet_roll;

void sheet_roll_end() {
  if (g_sheet_roll.roller) lv_obj_del(g_sheet_roll.roller);
  if (g_sheet_roll.active) report_anim(g_sheet_roll.down ? "settings roll down" : "settings roll up", g_sheet_roll.frames, lv_tick_elaps(g_sheet_roll.start));
  g_sheet_roll.roller = nullptr;
  g_sheet_roll.root = nullptr;
  g_sheet_roll.active = false;
  lv_img_set_angle(g_gear_img, 0);
}

void sheet_roll_exec(void *var, int32_t v) {  // v: how far through, 0 to 1000
  lv_obj_t *root = (lv_obj_t *)var;
  if (root != g_sheet_roll.root) return;
  int span = (kRollBottom - kRollTop) * v / 1000;
  int edge = g_sheet_roll.down ? kRollTop + span : kRollBottom - span;
  if (edge != g_sheet_roll.edge) {
    lv_area_t band = {0, (lv_coord_t)(LV_MIN(edge, g_sheet_roll.edge) - 1), 799, (lv_coord_t)(LV_MAX(edge, g_sheet_roll.edge) + 1)};
    root->coords.y2 = (lv_coord_t)(edge - 1);
    g_sheet_roll.edge = edge;
    lv_obj_invalidate_area(lv_layer_top(), &band);  // the layer: the band may be outside the root now
    lv_obj_set_y(g_sheet_roll.roller, edge - kRollerH / 2);
  }
  lv_img_set_angle(g_gear_img, (int16_t)((g_sheet_roll.down ? -kCrank : kCrank) * v / 1000));
  g_sheet_roll.frames++;
}

void sheet_roll_done(lv_anim_t *) {
  bool down = g_sheet_roll.down;
  lv_obj_t *root = g_sheet_roll.root;
  sheet_roll_end();
  if (!down) close_sheet();
  else if (root) root->coords.y2 = kRollBottom - 1;  // all the way down: its full height, as its style says
}

void sheet_roll_start(bool down) {
  lv_obj_t *root = g_sheet_root;
  if (!root) return;
  g_sheet_roll.active = true;
  g_sheet_roll.down = down;
  g_sheet_roll.root = root;
  g_sheet_roll.edge = down ? kRollTop : kRollBottom;
  g_sheet_roll.frames = 0;
  g_sheet_roll.start = lv_tick_get();
  // The roller: a rounded bar the width of the sheet, shaded like a paper roll lit from above.
  const theme::Palette &pal = theme::palette(g_mode);
  g_sheet_roll.roller = box(lv_layer_top(), 4, g_sheet_roll.edge - kRollerH / 2, 792, kRollerH, scale(pal.paper, 0.9f), kRollerH / 2);
  lv_obj_set_style_bg_grad_color(g_sheet_roll.roller, C(scale(pal.paper, 0.62f)), 0);
  lv_obj_set_style_bg_grad_dir(g_sheet_roll.roller, LV_GRAD_DIR_VER, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, root);
  lv_anim_set_exec_cb(&a, sheet_roll_exec);
  lv_anim_set_values(&a, 0, 1000);
  lv_anim_set_time(&a, down ? kUnrollMs : kRollUpMs);
  lv_anim_set_path_cb(&a, down ? lv_anim_path_ease_out : lv_anim_path_ease_in);
  lv_anim_set_ready_cb(&a, sheet_roll_done);
  lv_anim_start(&a);
}

void strip_clicked(lv_event_t *) {
  if (g_f.wall.valid) cal_toggle();  // the calendar needs to know what day it is
}

void gear_tapped() {
  if (g_sheet_roll.active) return;  // mid-roll: it finishes first
  if (settings_family_open()) {
    sheet_roll_start(false);
    return;
  }
  // Built with redraws paused, then clipped to the header (see sheet_roll_start): otherwise building
  // it redraws the whole screen once, about 300 ms on the panel, for no change you can see. LVGL
  // works out a new object's size in its next layout pass, which would undo the clip: run it first.
  lv_disp_enable_invalidation(nullptr, false);
  open_settings();
  if (g_sheet_root) {
    lv_obj_update_layout(g_sheet_root);
    g_sheet_root->coords.y2 = kRollTop - 1;
  }
  lv_disp_enable_invalidation(nullptr, true);
  sheet_roll_start(true);
}

void gear_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    g_gear_down = lv_tick_get();
    g_gear_consumed = false;
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    show(g_gear_arc, false);
  } else if (code == LV_EVENT_CLICKED) {
    if (g_gear_consumed) {
      g_gear_consumed = false;
      return;
    }
    gear_tapped();
  }
}

void glow_draw(lv_event_t *e) {
  if (!g_glow.on) return;
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  const lv_area_t &clip = *ctx->clip_area, &ba = *ctx->buf_area;
  lv_color_t *buf = (lv_color_t *)ctx->buf;
  const int bw = lv_area_get_width(&ba), cx = g_glow.cx, cy = g_glow.cy, R = g_glow.r[0];
  const int y0 = LV_MAX(clip.y1, cy - R), y1 = LV_MIN(clip.y2, cy + R);
  for (int y = y0; y <= y1; y++) {
    int lo[3], hi[3];  // each ring's span on this row; an inner one lies inside the outer
    for (int i = 0; i < 3; i++) {
      int rem = g_glow.r[i] * g_glow.r[i] - (y - cy) * (y - cy);
      int h = rem >= 0 ? (int)sqrtf((float)rem) : -1;
      lo[i] = h >= 0 ? cx - h : INT32_MAX;
      hi[i] = h >= 0 ? cx + h : INT32_MIN;
    }
    lv_color_t *row = buf + (y - ba.y1) * bw - ba.x1;
    for (int x = LV_MAX(clip.x1, lo[0]), xe = LV_MIN(clip.x2, hi[0]); x <= xe; x++) {
      const uint8_t(*t)[64] = g_glow.lut[x >= lo[2] && x <= hi[2] ? 2 : x >= lo[1] && x <= hi[1] ? 1 : 0];
      uint16_t v = row[x].full;
      row[x].full = (uint16_t)(t[0][v >> 11] << 11 | t[1][(v >> 5) & 0x3F] << 5 | t[2][v & 0x1F]);
    }
  }
}

void build_main() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  // The overlay layers must never scroll: a drag on a sheet would otherwise scroll the whole layer
  // (anything parked off-screen gives it range) and leave every later sheet and banner shifted.
  lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(lv_layer_sys(), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  g_sky.make(art::kScreenW, art::kScreenH, false);
  spare_begin();
  g_sky_img = image(scr, 0, 0);

  g_strip_img = image(scr, 78, 6);
  // Spec 4e: date and time 22 px, 22 px into the strip, the time 16 px after the date; a holiday
  // message right-aligned 22 px from the strip's end.
  g_date = label(scr, B22, hex(0xFBF1DC), 100, board_y(B22, 20.2f));  // their size: render_header
  g_time = label(scr, B22, hex(0xF6C24E), 300, board_y(B22, 20.2f));
  g_hol = label(scr, J28, hex(0xFBF1DC), 400, board_y(J28, 14));
  show(g_hol, false);
  // A tap anywhere on the strip rolls the calendar down, or back up (calendar spec 10).
  lv_obj_add_flag(g_strip_img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_strip_img, strip_clicked, LV_EVENT_CLICKED, nullptr);
  g_cal_chev = box(scr, 0, 16, 36, 36, hex(0), LV_RADIUS_CIRCLE);
  lv_obj_set_style_bg_opa(g_cal_chev, 56, 0);  // black at 22 percent
  g_chev_up.make(18, 18, true);
  g_cal_chev_img = image(g_cal_chev, 9, 9);
  show(g_cal_chev, false);

  // Dog in: a 56 px paper circle ringed 4 px in the dog colour, the paw in it. Dog out: a pill of the
  // dog colour, a 44 px white circle holding the paw, then COPPER OUT.
  g_dog = box(scr, 12, 6, 56, 56, hex(0xF7EAD2), LV_RADIUS_CIRCLE);
  lv_obj_set_style_border_opa(g_dog, LV_OPA_COVER, 0);
  lv_obj_add_flag(g_dog, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(g_dog, 17);  // draws at 56 px, touches like 90 px
  lv_obj_set_style_outline_width(g_dog, 4, LV_STATE_PRESSED);
  lv_obj_set_style_outline_opa(g_dog, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_event_cb(g_dog, dog_clicked, LV_EVENT_CLICKED, nullptr);
  g_dog_disc = box(g_dog, 6, 6, 44, 44, hex(0xFFFFFF), LV_RADIUS_CIRCLE);
  show(g_dog_disc, false);
  g_paw_in.make(36, 36, true);
  g_paw_out.make(34, 34, true);
  g_dog_paw = image(g_dog, 6, 6);
  g_dog_lbl = label(g_dog, J24, hex(0xFFFFFF), 60, board_y(J24, 11.4f));
  show(g_dog_lbl, false);

  g_gear = box(scr, 732, 6, 56, 56, hex(0xF7EAD2), LV_RADIUS_CIRCLE);
  lv_obj_set_style_border_width(g_gear, 4, 0);
  lv_obj_set_style_border_opa(g_gear, LV_OPA_COVER, 0);
  // Pressed: the ring thickens outward. (A wider border would push the icon 3 px down and right:
  // LVGL places children inside the border. Mike, 2026-10-22.)
  lv_obj_set_style_outline_width(g_gear, 3, LV_STATE_PRESSED);
  lv_obj_set_style_outline_opa(g_gear, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_flag(g_gear, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(g_gear, 17);
  lv_obj_add_event_cb(g_gear, gear_event, LV_EVENT_ALL, nullptr);
  g_gear_art.make(32, 32, true);
  g_close_art.make(30, 30, true);
  g_gear_img = image(g_gear, 8, 8);
  g_gear_arc = lv_arc_create(scr);
  lv_obj_set_size(g_gear_arc, 70, 70);
  lv_obj_set_pos(g_gear_arc, 725, -1);
  lv_arc_set_rotation(g_gear_arc, 270);
  lv_arc_set_bg_angles(g_gear_arc, 0, 360);
  lv_arc_set_range(g_gear_arc, 0, 100);
  lv_obj_remove_style(g_gear_arc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(g_gear_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_opa(g_gear_arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(g_gear_arc, 5, LV_PART_INDICATOR);  // flat, 5 px (brief 10.1)
  lv_obj_set_style_arc_rounded(g_gear_arc, false, LV_PART_INDICATOR);
  show(g_gear_arc, false);
  lv_timer_create(gear_tick, 50, nullptr);

  tile_build(g_tile[0], Floor::Upstairs, 12, 74);
  tile_build(g_tile[1], Floor::Downstairs, 12, 279);

  // Spec 4c, tile coordinates.
  lv_obj_t *out = g_out_cont = plain(scr, 538, 74, art::kSideW, art::kFloorH);
  show(out, false);  // until its art is drawn
  g_outside_art.make(art::kSideW, art::kFloorH, true);
  g_out_img = image(out, 0, 0);
  g_out_title = label(out, B14, hex(0), 0, board_y(B14, 9.2f), "OUTSIDE");
  lv_obj_set_style_text_letter_space(g_out_title, 2, 0);  // 0.16 em (2.24 px)
  center_x(g_out_title, art::kSideW / 2, text_width("OUTSIDE", B14, 2));
  g_fc_art.make(art::kForecast, art::kForecast, true);
  g_out_fc = image(out, 20, 42);
  g_rays_art.make(art::kForecast, art::kForecast, true);
  g_out_rays = image(out, 20, 42);  // danger heat: the sun's rays, pulsing over its disc
  show(g_out_rays, false);
  // The phrase: WeatherGate's forecast text, wrapped in the 124 px beside the icon and centred on
  // tile y 86 as the boards centre theirs (see phrase_fit for its size).
  g_out_phrase = label(out, B23, hex(0), 112, 72);
  lv_label_set_long_mode(g_out_phrase, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_out_phrase, kPhraseW + 8);  // its lines are broken already (phrase_fit)
  g_out_temp = label(out, J36, hex(0), 39, board_y(J36, 138.6f));
  g_odrop_art.make(14, 18, true);
  g_out_drop = image(out, 139, 156);
  g_out_hum = label(out, B24, hex(0), 156, board_y(B24, 149.8f));

  // Spec 4d: the bell block and DOORBELL as one column (the bell and ring are in the art).
  g_bell_cont = plain(scr, 538, 279, art::kSideW, art::kFloorH);
  lv_obj_add_flag(g_bell_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_translate_y(g_bell_cont, 6, LV_STATE_PRESSED);  // the face drops when pressed
  lv_obj_add_event_cb(g_bell_cont, bell_event, LV_EVENT_ALL, nullptr);
  show(g_bell_cont, false);  // until its art is drawn
  for (Art &a : g_bell_art) a.make(art::kSideW, art::kFloorH, true);
  g_bell_img = image(g_bell_cont, 0, 0);
  g_bell_pic_art.make(art::kBellPic, art::kBellPic, true);
  g_bell_pic = image(g_bell_cont, art::kBellPicX, art::kBellPicY);
  g_bell_word = label(g_bell_cont, J30, hex(0xFBF1DC), 0, board_y(J30, 125.3f), "DOORBELL");
  center_x(g_bell_word, art::kSideW / 2, text_width("DOORBELL", J30));
  g_bell_line = label(g_bell_cont, B20, hex(0xFBF1DC), 0, board_y(B20, 163));  // the doorbell's progress, under the word

  // Last, so the glow lies over everything on the main screen (sheets and banners are on the top
  // layer, above it). It follows the sky: placed when the sky's new drawing goes up.
  g_glow_obj = plain(scr, 0, 0, art::kScreenW, art::kScreenH);
  lv_obj_add_event_cb(g_glow_obj, glow_draw, LV_EVENT_DRAW_MAIN, nullptr);
  // Over everything on the main screen, and an image, so touches go through to what's under it.
  g_ice_img = image(scr, 0, 0);
  show(g_ice_img, false);
}

void glow_place(Phase ph, float p) {
  const boardart::SkyPhase &s = boardart::kSky[(int)ph];
  g_glow.cx = (int)lroundf(20 + p * 760);
  g_glow.cy = (int)lroundf(360 - 300 * sinf(3.14159265f * p));
  const Color c = hex(s.overlay);
  const int glow[3] = {c.r, c.g, c.b}, top[3] = {31, 63, 31};
  float keep = 1;
  for (int i = 0; i < 3; i++) {
    g_glow.r[i] = (int)s.ring_r[i];
    keep *= 1 - s.overlay_a[i];  // inside ring i, it and the rings outside it
    for (int ch = 0; ch < 3; ch++)
      for (int v = 0; v <= top[ch]; v++) {
        float d = v * 255.0f / top[ch], o = d + (glow[ch] - d) * (1 - keep);
        g_glow.lut[i][ch][v] = (uint8_t)lroundf(o * top[ch] / 255.0f);
      }
  }
  g_glow.on = true;
  lv_obj_invalidate(g_glow_obj);
}

// A tile still waiting for its new picture. It answers a tap, so it has the spare before the sky.
bool tile_waiting(const FloorTile &t, const FloorView &fv) {
  int st = fv.no_signal ? theme::kNoSignal : (int)fv.status;
  return st * 4 + (int)g_mode != t.key;
}

void render_sky(const View &v) {
  ArtJob done;
  if (g_art[kSlotSky].take(done)) {
    g_sky.cv.trade_pixels(g_spare.mem);  // the new sky on screen; the old one's memory is the spare now
    g_sky.dsc.data = g_sky.cv.data();
    spare_release();
    g_sky.show(g_sky_img);
    g_sky_phase = (int)done.phase;
    g_sky_moon = done.moon;
    g_sky_p = done.disc_p;
    g_sky_at = lv_tick_get();
    glow_place(done.phase, done.disc_p);
  }
  bool due = g_sky_phase != (int)v.phase || g_sky_moon != v.moon;
  float dp = v.disc_p - g_sky_p;
  if (dp < 0) dp = -dp;
  // Redrawn on a phase change, or once a minute at most while the disc creeps along (brief section 7).
  if (!due && dp > 0.004f && lv_tick_elaps(g_sky_at) > 60000) due = true;
  if (!due || !g_art[kSlotSky].idle()) return;
  if (tile_waiting(g_tile[0], v.floor[(int)Floor::Upstairs]) || tile_waiting(g_tile[1], v.floor[(int)Floor::Downstairs]))
    return;
  gfx::Canvas *cv = spare_claim(kSlotSky, art::kScreenW, art::kScreenH, false);
  if (!cv) return;
  ArtJob j;
  j.kind = ArtKind::Sky;
  j.cv = cv;
  j.phase = v.phase;
  j.disc_p = v.disc_p;
  j.moon = v.moon;
  g_art[kSlotSky].submit(j);
}

void render_header(const View &v, bool mode_changed) {
  const theme::Palette &pal = theme::palette(g_mode);
  bool out = v.dog.out != Dog::None;
  int dog_w = out ? 6 + 44 + 10 + text_width(v.dog.pill, J24) + 14 : 56;
  // HANDOFF: while the other floor's dog is out, this panel can't change it, so the pill is only a
  // display: no press look for a tap that would do nothing.
  if (v.dog.can_tap != lv_obj_has_flag(g_dog, LV_OBJ_FLAG_CLICKABLE)) {
    if (v.dog.can_tap) lv_obj_add_flag(g_dog, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_clear_flag(g_dog, LV_OBJ_FLAG_CLICKABLE);
  }
  if (mode_changed || g_dog_out != (int)out) {
    lv_obj_set_style_bg_color(g_dog, C(out ? pal.dog : pal.paper), 0);
    lv_obj_set_style_border_width(g_dog, out ? 0 : 4, 0);
    lv_obj_set_style_border_color(g_dog, C(pal.dog), 0);
    lv_obj_set_style_outline_color(g_dog, C(out ? pal.cream : pal.dog), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(g_dog_disc, C(pal.dog_text), 0);
    show(g_dog_disc, out);
    Art &paw = out ? g_paw_out : g_paw_in;
    art::icon(paw.cv, out ? art::Icon::PawOut : art::Icon::PawIn, pal.dog);
    paw.show(g_dog_paw);
    lv_obj_set_pos(g_dog_paw, out ? 11 : 6, out ? 11 : 6);  // inside the 4 px ring, or the white circle
    set_text_color(g_dog_lbl, pal.dog_text);
    show(g_dog_lbl, out);
    g_dog_out = out;
  }
  if (mode_changed) {
    lv_obj_set_style_bg_color(g_gear, C(pal.paper), 0);
    lv_obj_set_style_border_color(g_gear, C(pal.gear_border), 0);
    lv_obj_set_style_outline_color(g_gear, C(pal.gear_border), LV_STATE_PRESSED);
    art::icon(g_gear_art.cv, art::Icon::Gear, pal.gear_ink);
    art::icon(g_close_art.cv, art::Icon::Close, pal.gear_ink);
    lv_obj_set_style_arc_color(g_gear_arc, C(pal.gold), LV_PART_INDICATOR);
    set_text_color(g_date, pal.cream);
    set_text_color(g_time, pal.gold);
    set_text_color(g_hol, pal.cream);
    art::cal_icon(g_chev_up.cv, art::CalIcon::Up, theme::cal_colors(g_mode).cream);
    g_cal_chev_state = -1;
    g_gear_icon = -1;
  }
  // The gear turns while Settings rolls down or up (sheet_roll_start); the X shows once it's down. A roll
  // whose sheet has gone (closed some other way, or rebuilt) is over.
  if (g_sheet_roll.active && g_sheet_roll.root != g_sheet_root) sheet_roll_end();
  int want = settings_family_open() && !g_sheet_roll.active ? 1 : 0;
  if (want != g_gear_icon) {
    if (want) {
      g_close_art.show(g_gear_img);
      lv_obj_set_pos(g_gear_img, 9, 9);
    } else {
      g_gear_art.show(g_gear_img);
      lv_obj_set_pos(g_gear_img, 8, 8);  // 32 px, inside the 4 px ring: centred
    }
    g_gear_icon = want;
  }

  if (!g_have_prev || strcmp(v.dog.pill, g_prev.dog.pill) != 0) {
    set_text(g_dog_lbl, v.dog.pill);
    g_dog_from = lv_obj_get_width(g_dog);
    if (g_have_prev) anim(g_dog, g_dog_from, dog_w, 200, set_w);  // brief 10.6: a slide, no bounce
    else lv_obj_set_width(g_dog, dog_w);
  }
  // The date strip runs from 10 px after the dog to 10 px before the gear (brief 10.6). While the
  // pill slides, the strip keeps to the wider of the two widths so they never overlap.
  int pill_w = lv_anim_get(g_dog, nullptr) ? (g_dog_from > dog_w ? g_dog_from : dog_w) : dog_w;
  int strip_x = 12 + pill_w + 10, strip_w = 722 - strip_x;
  // The date and the time at 26 px (Mike, 2026-10-26: about 20 percent bigger than the boards'
  // 22), or 24, or 22 where a long date would otherwise run into the scenery at the strip's right
  // end ("Wednesday, March 30" and the like). Each size centred on the strip as the boards' 22 is.
  static const lv_font_t *const kDateFonts[3] = {B26, B24, B22};
  static const int kDateGaps[3] = {19, 17, 16}, kDateSizes[3] = {26, 24, 22};
  const int scenery = strip_w - (out ? art::kStripSceneryOut : art::kStripSceneryIn);
  int df = 2;
  for (int i = 0; i < 2 && df == 2; i++)
    if (22 + text_width(v.date, kDateFonts[i]) + kDateGaps[i] + text_width(v.time, kDateFonts[i]) <= scenery) df = i;
  const lv_font_t *dfont = kDateFonts[df];
  if (lv_obj_get_style_text_font(g_date, 0) != dfont) {
    float top = 33.6f - 0.61f * kDateSizes[df];  // the capitals' middle stays at the strip's
    lv_obj_t *const both[2] = {g_date, g_time};
    for (lv_obj_t *o : both) {
      lv_obj_set_style_text_font(o, dfont, 0);
      lv_obj_set_y(o, board_y(dfont, top));
    }
  }
  int date_w = text_width(v.date, dfont), date_gap = kDateGaps[df];
  // The calendar's up chevron, 14 px after the time, while it's rolled down (Mike, 2026-10-25: no
  // down chevron; a tap anywhere on the strip opens it). A holiday message that would run into it
  // isn't shown meanwhile.
  int time_end = 22 + date_w + date_gap + text_width(v.time, dfont);  // strip coordinates
  int chev_x = strip_x + time_end + 14;
  bool chev = v.clock_valid && cal_is_open();
  // Today's message at the right end, a federal holiday's, a birthday's or an anniversary's (Mike,
  // 2026-10-25), with its icon in the 49 px before it, taking turns every 8 s when there's more
  // than one: the full words at 28 px, else at 24, else the short words; none if nothing fits.
  const int from = (chev ? chev_x + 36 + 8 - strip_x : time_end + 16) + 55;  // where the words may start
  const char *msg = nullptr;
  const lv_font_t *mf = J28;
  tc::DayIcon icon = tc::DayIcon::None;
  if (v.nstrip) {
    const View::StripMsg &m = v.strip[(g_f.mono_ms / 8000) % v.nstrip];
    const char *words[2] = {m.text, m.short_text};
    const lv_font_t *fonts[2] = {J28, J24};
    for (int i = 0; i < 4 && !msg; i++)
      if (words[i / 2][0] && strip_w - 22 - text_width(words[i / 2], fonts[i % 2]) >= from) {
        msg = words[i / 2];
        mf = fonts[i % 2];
      }
    icon = m.icon;
    if (!msg && strip_w - 22 >= from) msg = "";  // no room for words: the icon alone, at the end
  }
  bool hol = msg != nullptr;
  int hol_x = hol ? strip_w - 22 - text_width(msg, mf) : 0;
  int key = (int)v.phase * 1000000 + (int)(v.disc_p * 200) * 2000 + (v.moon ? 1 : 0) * 500000 + (out ? 1000 : 0) + hol_x;
  if (strip_w != g_strip_w || key != g_strip_key || (int)icon != g_strip_icon) {
    if (g_strip.make(strip_w, art::kStripH, true)) {
      art::strip(g_strip.cv, v.phase, v.disc_p, v.moon, out, (float)hol_x, icon);
      g_strip.show(g_strip_img);
    }
    set_x(g_strip_img, strip_x);
    g_strip_w = strip_w;
    g_strip_key = key;
    g_strip_icon = (int)icon;
  }
  set_text(g_date, v.date);
  set_text(g_time, v.time);
  set_x(g_date, strip_x + 22);
  set_x(g_time, strip_x + 22 + date_w + date_gap);
  show(g_hol, hol);
  if (hol) {
    lv_obj_set_style_text_font(g_hol, mf, 0);
    lv_obj_set_y(g_hol, board_y(mf, mf == J28 ? 14 : 16));  // centred on the strip
    set_text(g_hol, msg);
    set_x(g_hol, strip_x + hol_x);
  }
  show(g_cal_chev, chev);
  set_x(g_cal_chev, chev_x);
  if (g_cal_chev_state != 1) {
    g_chev_up.show(g_cal_chev_img);
    g_cal_chev_state = 1;
  }
}

// The outside tile and the doorbell change only between day, evening and night; the new art is
// drawn in the background and the colors switch with it.
// The forecast text in at most four lines (Mike, 2026-10-24: three to four lines, no scrolling),
// wrapped here at spaces by measured width, so no line is wider than the column: LVGL's own
// wrapping counts the space after a word as part of it. At the boards' 23 px when it fits; a few
// of WeatherGate's longest ("Rain at Times, Becoming Very Unsettled") need 20 or 18 px, and
// "Changeable," alone is too wide for the column at 23. A word is never broken.
struct PhraseFit {
  const lv_font_t *font;
  int px, lines;
  char text[kMaxPhrase + 8];  // with a newline at each break
};

// Lines at font f, or 0 if a word alone is too wide.
int wrap_phrase(const char *t, const lv_font_t *f, char *out, size_t cap) {
  char line[kMaxPhrase + 8] = "", cand[kMaxPhrase + 8];
  size_t used = 0;
  int lines = 0;
  out[0] = 0;
  auto flush = [&]() {
    if (!line[0]) return;
    used += (size_t)snprintf(out + used, cap - used, "%s%s", lines ? "\n" : "", line);
    if (used >= cap) used = cap - 1;
    lines++;
  };
  for (const char *p = t + strspn(t, " "); *p;) {
    size_t n = strcspn(p, " ");
    char word[kMaxPhrase + 1];
    if (n > kMaxPhrase) n = kMaxPhrase;
    memcpy(word, p, n);
    word[n] = 0;
    p += n;
    p += strspn(p, " ");
    if (text_width(word, f) > kPhraseW) return 0;
    snprintf(cand, sizeof(cand), "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && text_width(cand, f) > kPhraseW) {
      flush();
      snprintf(line, sizeof(line), "%s", word);
    } else {
      snprintf(line, sizeof(line), "%s", cand);
    }
  }
  flush();
  return lines;
}

void phrase_fit(const char *t, PhraseFit &fit) {
  const struct {
    const lv_font_t *f;
    int px;
  } kSizes[] = {{B23, 23}, {B20, 20}, {B18, 18}};
  for (const auto &s : kSizes) {
    fit.font = s.f;
    fit.px = s.px;
    fit.lines = wrap_phrase(t, s.f, fit.text, sizeof(fit.text));
    if (fit.lines > 0 && fit.lines <= 4) return;
  }
  // Longer than any WeatherGate text: 18 px, past four lines; or a word too wide even at 18 px,
  // which LVGL then breaks.
  if (fit.lines == 0) {
    snprintf(fit.text, sizeof(fit.text), "%s", t);
    fit.lines = 4;
  }
}

void rays_opa(void *o, int32_t v) { lv_obj_set_style_img_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

// Danger heat: the rays fade out and back once a second, the disc stays (as WeatherGate's do,
// there switched on and off).
void rays_pulse(bool on) {
  lv_anim_del(g_out_rays, rays_opa);
  show(g_out_rays, on);
  if (!on) return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g_out_rays);
  lv_anim_set_exec_cb(&a, rays_opa);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_time(&a, 500);
  lv_anim_set_playback_time(&a, 500);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

void render_outside(const View &v) {
  ArtJob done;
  if (g_art[kSlotOutside].take(done)) {
    const theme::Palette &pal = theme::palette(done.mode);
    spare_copy(g_outside_art, 0, g_out_img);
    spare_release();
    set_text_color(g_out_title, pal.outside_text);
    set_text_color(g_out_phrase, pal.outside_text);
    set_text_color(g_out_temp, pal.outside_text);
    set_text_color(g_out_hum, pal.humidity);
    art::icon(g_odrop_art.cv, art::Icon::DropOut, pal.humidity);
    g_odrop_art.show(g_out_drop);
    g_fc_key = -1;
    if (g_out_mode < 0) show(g_out_cont, true);
    g_out_mode = (int)done.mode;
  }
  if (g_out_mode != (int)g_mode && g_art[kSlotOutside].idle()) {
    if (gfx::Canvas *cv = spare_claim(kSlotOutside, art::kSideW, art::kFloorH, true)) {
      ArtJob j;
      j.kind = ArtKind::Outside;
      j.cv = cv;
      j.mode = g_mode;
      g_art[kSlotOutside].submit(j);
    }
  }
  if (g_out_mode < 0) return;
  Mode shown = (Mode)g_out_mode;
  const theme::Palette &pal = theme::palette(shown);
  // The icon: the forecast, with the moon for the sun at night; danger heat replaces it.
  bool night = v.moon, heat = v.outside.valid && v.outside.heat;
  int fk = (((int)v.outside.code * 2 + night) * 2 + heat) * 4 + (int)shown;
  if (fk != g_fc_key) {
    if (heat) {
      art::heat_sun(g_fc_art.cv, false, pal.heat);
      art::heat_sun(g_rays_art.cv, true, pal.heat);
      g_rays_art.show(g_out_rays);
    } else {
      art::forecast(g_fc_art.cv, v.outside.code, night, pal.outside_text);
    }
    g_fc_art.show(g_out_fc);
    rays_pulse(heat);
    g_fc_key = fk;
  }
  if (!g_phrase_set || strcmp(g_phrase_src, v.outside.phrase) != 0) {
    g_phrase_set = true;
    snprintf(g_phrase_src, sizeof(g_phrase_src), "%s", v.outside.phrase);
    static PhraseFit fit;  // not on the interface task's stack
    phrase_fit(v.outside.phrase, fit);
    float pitch = fit.px * 1.15f;  // the boards' line height; each line's box is 1.24 em
    lv_obj_set_style_text_font(g_out_phrase, fit.font, 0);
    lv_obj_set_style_text_line_space(g_out_phrase, (int)lroundf(pitch) - fit.font->line_height, 0);
    set_text(g_out_phrase, fit.text);
    lv_obj_set_y(g_out_phrase, board_y(fit.font, 86 - (fit.px * 1.24f + (fit.lines - 1) * pitch) / 2));
  }
  // Danger cold: icicles, drawn the first time they're needed and kept.
  bool cold = v.outside.valid && v.outside.cold;
  if (cold && !g_ice_drawn && g_ice_art.make(art::kScreenW, art::kIciclesH, true)) {
    art::icicles(g_ice_art.cv);
    g_ice_art.show(g_ice_img);
    g_ice_drawn = true;
  }
  show(g_ice_img, cold && g_ice_drawn);
  // Spec 4c: the temperature, 16 px, the drop, 3 px, the humidity, centred on the tile as one.
  if (strcmp(lv_label_get_text(g_out_temp), v.outside.temp) != 0 || strcmp(lv_label_get_text(g_out_hum), v.outside.hum) != 0) {
    set_text(g_out_temp, v.outside.temp);
    set_text(g_out_hum, v.outside.hum);
    int tw = text_width(v.outside.temp, J36), row = tw + 16 + 14 + 3 + text_width(v.outside.hum, B24);
    int x0 = art::kSideW / 2 - row / 2;
    set_x(g_out_temp, x0);
    set_x(g_out_drop, x0 + tw + 16);
    set_x(g_out_hum, x0 + tw + 16 + 17);
  }
}

constexpr uint32_t kShakeMs = 3000;  // about as long as the doorbell's sounds
constexpr int kBellPivotX = art::kBellPic / 2, kBellPivotY = 5;  // the top of the bell: it swings from there

// A ringing bell: three swings a second, 14 degrees each way, easing off over the last 0.6 s. t is
// the time into the shake, in ms.
void bell_swing(void *o, int32_t t) {
  float ease = t > (int32_t)kShakeMs - 600 ? (int32_t)(kShakeMs - t) / 600.0f : 1.0f;
  float deg = 14.0f * ease * sinf((float)t * (2 * 3.14159265f * 3 / 1000.0f));
  lv_img_set_angle((lv_obj_t *)o, (int16_t)lroundf(deg * 10));
}

void bell_shake() {
  lv_anim_del(g_bell_pic, bell_swing);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g_bell_pic);
  lv_anim_set_exec_cb(&a, bell_swing);
  lv_anim_set_values(&a, 0, kShakeMs);
  lv_anim_set_time(&a, kShakeMs);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
}

void render_bell(const View &v) {
  ArtJob done;
  if (g_art[kSlotBell].take(done)) {
    bool pressed = lv_obj_has_state(g_bell_cont, LV_STATE_PRESSED);
    spare_copy(g_bell_art[0], 0, pressed ? nullptr : g_bell_img);
    spare_copy(g_bell_art[1], 1, pressed ? g_bell_img : nullptr);
    spare_release();
    set_text_color(g_bell_word, theme::palette(done.mode).door_text);
    if (g_bell_mode < 0) show(g_bell_cont, true);
    g_bell_mode = (int)done.mode;
    g_bell_warn = -1;
  }
  if (g_bell_mode != (int)g_mode && g_art[kSlotBell].idle()) {
    if (gfx::Canvas *cv = spare_claim(kSlotBell, art::kSideW, art::kFloorH, true)) {
      ArtJob j;
      j.kind = ArtKind::Bell;
      j.cv = cv;
      j.cv2 = &g_spare.view[1];
      j.mode = g_mode;
      g_art[kSlotBell].submit(j);
    }
  }
  if (g_bell_mode < 0) return;
  const theme::Palette &pal = theme::palette((Mode)g_bell_mode);
  // No subtitle at rest (brief): the line only shows the doorbell's progress or a missed ring.
  const char *line = v.bell.line[0] ? v.bell.line : v.bell.missed;
  if (strcmp(lv_label_get_text(g_bell_line), line) != 0) {
    set_text(g_bell_line, line);
    center_x(g_bell_line, art::kSideW / 2, text_width(line, B20));
  }
  int warn = v.bell.out == BellOut::CouldntReach || v.bell.out == BellOut::NoAnswer || (!v.bell.line[0] && v.bell.missed[0]);
  if (warn != g_bell_warn) {
    set_text_color(g_bell_line, warn ? pal.gold : pal.cream);
    g_bell_warn = warn;
  }
  int key = g_bell_mode * 2 + (v.bell.alert ? 1 : 0);
  if (key != g_bell_pic_key) {
    art::bell(g_bell_pic_art.cv, v.bell.alert ? pal.bell_red : pal.door_text);
    g_bell_pic_art.show(g_bell_pic);
    lv_img_set_pivot(g_bell_pic, kBellPivotX, kBellPivotY);  // setting the picture resets it
    g_bell_pic_key = key;
  }
  // A few more rings: shake. (A big jump is a new controller, as in the simulator's scenes.)
  uint16_t more = (uint16_t)(v.bell.shakes - g_bell_shakes);
  g_bell_shakes = v.bell.shakes;
  if (more >= 1 && more <= 8) bell_shake();
}

// ---------------------------------------------------------------------------------------------
// Top layer: doorbell banner, notices, wake catcher

void banner_answer(lv_event_t *e) {
  intptr_t which = (intptr_t)lv_event_get_user_data(e);
  if (!g_act) return;
  if (which == 2) g_act->dismiss_banner();
  else g_act->answer(which == 1);
}

void render_banner(const View &v, bool mode_changed) {
  bool same = v.banner.kind == g_banner_kind && g_have_prev && strcmp(v.banner.title, g_prev.banner.title) == 0 &&
              strcmp(v.banner.sub, g_prev.banner.sub) == 0;
  if (same && !mode_changed) return;
  if (g_banner) {
    lv_obj_add_flag(g_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_del_async(g_banner);  // may be called right after one of its own buttons
    g_banner = nullptr;
  }
  g_banner_kind = v.banner.kind;
  if (v.banner.kind == BannerKind::None) return;
  const theme::Palette &pal = theme::palette(g_mode);
  // HANDOFF: "Doorbell from upstairs" with Come on and Not now.
  g_banner = box(lv_layer_top(), 12, 74, 776, 236, pal.doorbell, 24);
  lv_obj_add_flag(g_banner, LV_OBJ_FLAG_CLICKABLE);  // swallow touches meant for tiles underneath
  lv_obj_set_style_border_width(g_banner, 5, 0);
  lv_obj_set_style_border_color(g_banner, C(pal.gold), 0);
  lv_obj_set_style_border_opa(g_banner, LV_OPA_COVER, 0);
  label(g_banner, J44, pal.cream, 26, 18, v.banner.title);
  label(g_banner, B20, pal.cream, 28, 78, v.banner.sub);
  if (v.banner.kind == BannerKind::Knock) {
    theme::StatusColors open = theme::status_colors((int)Status::Open, g_mode);
    theme::StatusColors busy = theme::status_colors((int)Status::Busy, g_mode);
    button(g_banner, 20, 120, 360, 90, open.tile, open.text, "Come on", J30, banner_answer, (void *)1);
    button(g_banner, 391, 120, 360, 90, busy.tile, busy.text, "Not now", J30, banner_answer, (void *)0);
  } else {
    button(g_banner, 20, 120, 731, 90, pal.ring, pal.cream, "OK", J30, banner_answer, (void *)2);
  }
  lv_obj_move_foreground(g_banner);
  if (!mode_changed) {
    lv_obj_set_style_opa(g_banner, LV_OPA_TRANSP, 0);
    anim(g_banner, 0, 255, 200, set_opa);
  }
}

void render_toast(const View &v) {
  const theme::Palette &pal = theme::palette(g_mode);
  if (!g_toast) {
    g_toast = box(lv_layer_top(), 0, 80, 10, 44, pal.doorbell, 22);
    g_toast_lbl = label(g_toast, B20, pal.cream, 22, 11);
    show(g_toast, false);
  }
  bool on = v.toast[0] != 0;
  bool was = g_have_prev && g_prev.toast[0] != 0;
  if (on && (!was || strcmp(v.toast, g_prev.toast) != 0)) {
    set_text(g_toast_lbl, v.toast);
    int w = text_width(v.toast, B20) + 44;
    lv_obj_set_width(g_toast, w);
    set_x(g_toast, 400 - w / 2);
    lv_obj_set_style_bg_color(g_toast, C(pal.doorbell), 0);
    set_text_color(g_toast_lbl, pal.cream);
    lv_obj_move_foreground(g_toast);
  }
  show(g_toast, on);
}

// PROPOSED: with "screen off overnight", the first touch only wakes the screen, so nothing gets
// pressed blind in the dark.
void wake_pressed(lv_event_t *) { show(g_wake, false); }

void render_wake(const View &v) {
  if (!g_wake) {
    g_wake = plain(lv_layer_sys(), 0, 0, art::kScreenW, art::kScreenH);
    lv_obj_add_flag(g_wake, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_wake, wake_pressed, LV_EVENT_PRESSED, nullptr);
    show(g_wake, false);
  }
  bool off = v.backlight == kBacklightOff;
  bool was_off = g_have_prev && g_prev.backlight == kBacklightOff;
  if (off != was_off) show(g_wake, off);  // only on a change: the catcher hides itself on a touch
}

void keep_clean_screen_on_top() {
  if (g_sheet != Sheet::CleanScreen || !g_sheet_root) return;
  uint32_t n = lv_obj_get_child_cnt(lv_layer_top());
  if (n && lv_obj_get_index(g_sheet_root) != n - 1) lv_obj_move_foreground(g_sheet_root);
}

}  // namespace

void init(Actions &actions) {
  g_act = &actions;
  build_main();
}

namespace detail {

void under_overlays(lv_obj_t *o) { lv_obj_move_to_index(o, lv_obj_get_index(g_glow_obj)); }

void main_tiles_visible(bool on) {
  for (FloorTile &t : g_tile) show(t.cont, on && t.key >= 0);
  show(g_out_cont, on && g_out_mode >= 0);
  show(g_bell_cont, on && g_bell_mode >= 0);
}

}  // namespace detail

void render(const Frame &f) {
  g_f = f;
  g_set = *f.settings;
  const View &v = *f.view;
  bool mode_changed = !g_have_prev || g_prev.mode != v.mode;
  g_mode = v.mode;
  if (mode_changed && g_have_prev) {
    // Rebuild what's open in the new colors. The timer sheet and the clock editor hold choices in
    // progress, so they keep their old colors until closed.
    if (g_sheet == Sheet::Status) open_status_sheet();
    else if (g_sheet == Sheet::Settings) open_settings();
    else if (g_sheet == Sheet::Test) open_test_panel();
    else if (g_sheet == Sheet::Ringtone) open_ringtones();
    else if (g_sheet == Sheet::Zambretti) open_zambretti_demo();
  }

  render_sky(v);
  render_header(v, mode_changed);
  tile_render(g_tile[0], v.floor[(int)Floor::Upstairs]);
  tile_render(g_tile[1], v.floor[(int)Floor::Downstairs]);
  mail_render(v.mail);
  {
    // The Zambretti demo (Settings) shows its chosen sample on the real outside tile.
    static View pv;
    bool night = false;
    if (zambretti_preview(pv.outside, night)) {
      OutsideView ov = pv.outside;
      pv = v;
      pv.outside = ov;
      pv.moon = night;
      render_outside(pv);
    } else {
      render_outside(v);
    }
  }
  render_bell(v);
  cal_render(mode_changed);
  if (cal_covering()) main_tiles_visible(false);  // a tile whose first art just arrived stays hidden
  render_banner(v, mode_changed);
  render_toast(v);
  render_wake(v);
  keep_clean_screen_on_top();  // a doorbell still chimes, but the glass stays safe to wipe
  refresh_sheet();
  g_prev = v;
  g_have_prev = true;
}

void tap_gear() { gear_tapped(); }

void set_copy_pause(void (*pause)()) { g_copy_pause = pause; }

void scroll_sheet(int y) {
  if (g_sheet_root && lv_obj_get_child_cnt(g_sheet_root)) lv_obj_scroll_to_y(lv_obj_get_child(g_sheet_root, 0), y, LV_ANIM_OFF);
}

void open_sheet(Sheet s, Status timed) {
  switch (s) {
    case Sheet::Status: open_status_sheet(); break;
    case Sheet::Timer: open_timer_sheet(timed); break;
    case Sheet::Settings: open_settings(); break;
    case Sheet::TimeDate: open_time_date(); break;
    case Sheet::Test: open_test_panel(); break;
    case Sheet::Ringtone: open_ringtones(); break;
    case Sheet::Zambretti: open_zambretti_demo(); break;
    case Sheet::CleanScreen: start_clean_screen(); break;
    default: close_sheet(); break;
  }
}

}  // namespace ui
