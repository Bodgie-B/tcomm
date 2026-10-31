#include "ui_internal.h"
#include <math.h>

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;
using gfx::scale;

Actions *g_act = nullptr;
Frame g_f = {};
tc::Settings g_set;
tc::Mode g_mode = tc::Mode::Day;
Sheet g_sheet = Sheet::None;
lv_obj_t *g_sheet_root = nullptr;
lv_obj_t *g_banner = nullptr;

const lv_font_t *J64 = &font_jost_800_64, *J54 = &font_jost_800_54, *J50 = &font_jost_800_50,
                *J44 = &font_jost_800_44, *J36 = &font_jost_800_36, *J30 = &font_jost_800_30,
                *J28 = &font_jost_800_28, *J24 = &font_jost_800_24;
// Atkinson Hyperlegible has no 800 weight: the boards' "800" renders as its Bold, so these are Bold.
const lv_font_t *J27 = &font_jost_800_27, *J23 = &font_jost_800_23, *J76N = &font_jost_800_76_num,
                *J22N = &font_jost_800_22_num, *J21N = &font_jost_800_21_num, *B11 = &font_atk_bold_11,
                *B12 = &font_atk_bold_12, *B13 = &font_atk_bold_13, *B15 = &font_atk_bold_15, *B19 = &font_atk_bold_19,
                *B21 = &font_atk_bold_21;
const lv_font_t *B14 = &font_atk_bold_14, *B16 = &font_atk_bold_16, *B18 = &font_atk_bold_18, *B20 = &font_atk_bold_20,
                *B22 = &font_atk_bold_22, *B23 = &font_atk_bold_23, *B24 = &font_atk_bold_24,
                *B26 = &font_atk_bold_26;

bool Art::make(int w, int h, bool alpha) {
  if (cv.ok() && cv.width() == w && cv.height() == h && cv.has_alpha() == alpha) return true;
  if (!cv.create(w, h, alpha)) return false;
  memset(&dsc, 0, sizeof(dsc));
  dsc.header.w = (uint32_t)w;
  dsc.header.h = (uint32_t)h;
  dsc.header.cf = alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;
  dsc.data_size = (uint32_t)cv.bytes();
  dsc.data = cv.data();
  return true;
}

void Art::show(lv_obj_t *img) {
  lv_img_cache_invalidate_src(&dsc);
  lv_img_set_src(img, &dsc);
  lv_obj_invalidate(img);
}

ArtSlot g_art[kSlotCount];
static uint8_t *g_scratch = nullptr;
static size_t g_scratch_bytes = 0;

void ArtSlot::submit(const ArtJob &j) {
  job_ = j;
  state_.store(kWaiting, std::memory_order_release);
}

bool ArtSlot::take(ArtJob &out) {
  if (state_.load(std::memory_order_acquire) != kDone) return false;
  out = job_;
  state_.store(kIdle, std::memory_order_release);
  return true;
}

bool ArtSlot::run() {
  uint8_t waiting = kWaiting;
  if (!state_.compare_exchange_strong(waiting, kRunning, std::memory_order_acq_rel)) return false;
  const ArtJob &j = job_;
  auto strips = [](gfx::Canvas &c, auto draw) { gfx::draw_in_strips(c, g_scratch, g_scratch_bytes, draw); };
  switch (j.kind) {
    case ArtKind::Sky: strips(*j.cv, [&](gfx::Canvas &c) { art::sky(c, j.phase, j.disc_p, j.moon); }); break;
    case ArtKind::Floor: strips(*j.cv, [&](gfx::Canvas &c) { art::floor_tile(c, j.floor, j.status, j.mode); }); break;
    case ArtKind::Outside: strips(*j.cv, [&](gfx::Canvas &c) { art::outside_tile(c, j.mode); }); break;
    case ArtKind::Bell:
      strips(*j.cv, [&](gfx::Canvas &c) { art::bell_tile(c, j.mode, false); });
      strips(*j.cv2, [&](gfx::Canvas &c) { art::bell_tile(c, j.mode, true); });
      break;
  }
  state_.store(kDone, std::memory_order_release);
  return true;
}

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, Color bg, int radius) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, C(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = box(parent, x, y, w, h, hex(0), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, Color col, int x, int y, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, C(col), 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, text);
  return l;
}

int text_width(const char *t, const lv_font_t *font, int letter_space) {
  lv_point_t p;
  lv_txt_get_size(&p, t, font, (lv_coord_t)letter_space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return p.x;
}

// The browser puts the baseline the font's ascent below the top of the line box (Jost 1.07 em,
// Atkinson Hyperlegible 0.95 em: their hhea and typo ascenders). lv_font_conv sizes a label's line
// from the glyphs it holds, so its baseline sits higher, by 2 px at 14 px up to 15 px at 64 px.
int board_y(const lv_font_t *font, float css_top) {
  static const struct {
    const lv_font_t *f;
    float ascent;
  } kAscent[] = {
      {&font_jost_800_64, 64 * 1.07f}, {&font_jost_800_54, 54 * 1.07f}, {&font_jost_800_50, 50 * 1.07f},
      {&font_jost_800_44, 44 * 1.07f}, {&font_jost_800_36, 36 * 1.07f}, {&font_jost_800_30, 30 * 1.07f},
      {&font_jost_800_28, 28 * 1.07f}, {&font_jost_800_24, 24 * 1.07f}, {&font_jost_800_27, 27 * 1.07f},
      {&font_jost_800_23, 23 * 1.07f}, {&font_jost_800_76_num, 76 * 1.07f}, {&font_jost_800_22_num, 22 * 1.07f},
      {&font_jost_800_21_num, 21 * 1.07f}, {&font_atk_bold_11, 11 * 0.95f}, {&font_atk_bold_12, 12 * 0.95f},
      {&font_atk_bold_13, 13 * 0.95f}, {&font_atk_bold_15, 15 * 0.95f}, {&font_atk_bold_19, 19 * 0.95f},
      {&font_atk_bold_21, 21 * 0.95f}, {&font_atk_bold_14, 14 * 0.95f},
      {&font_atk_bold_16, 16 * 0.95f}, {&font_atk_bold_18, 18 * 0.95f}, {&font_atk_bold_20, 20 * 0.95f},
      {&font_atk_bold_22, 22 * 0.95f},
      {&font_atk_bold_23, 23 * 0.95f}, {&font_atk_bold_24, 24 * 0.95f}, {&font_atk_bold_26, 26 * 0.95f},
  };
  float lv_ascent = (float)(font->line_height - font->base_line), ascent = lv_ascent;
  for (const auto &k : kAscent)
    if (k.f == font) ascent = k.ascent;
  return (int)lroundf(css_top + ascent - lv_ascent);
}

lv_obj_t *image(lv_obj_t *parent, int x, int y) {
  lv_obj_t *i = lv_img_create(parent);
  lv_obj_set_pos(i, x, y);
  lv_obj_clear_flag(i, LV_OBJ_FLAG_CLICKABLE);
  return i;
}

static bool is_light(Color c) { return c.r * 299 + c.g * 587 + c.b * 114 > 140000; }

Color ink() { return g_mode == tc::Mode::Night ? hex(0xE4D8C0) : theme::palette(g_mode).plum; }
Color chip() { return g_mode == tc::Mode::Night ? hex(0x2A3A30) : hex(0xE6D5B6); }

void outline(lv_obj_t *o, Color c) {
  lv_obj_set_style_outline_width(o, 5, 0);
  lv_obj_set_style_outline_color(o, C(c), 0);
  lv_obj_set_style_outline_opa(o, LV_OPA_COVER, 0);
}

// Brief 10.1 pressed look: a solid face on a 6 px darker ledge; pressing drops the face onto the
// ledge, the ledge disappears and a 5 px outline appears (dark on light buttons, light on dark).
void pressable(lv_obj_t *b, Color face, int h) {
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(b, 6, 0);
  lv_obj_set_style_border_color(b, C(scale(face, 0.62f)), 0);
  lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
  // Pressed: the face drops 6 px onto where the ledge was, so it ends at the same bottom edge.
  lv_obj_set_style_translate_y(b, 6, LV_STATE_PRESSED);
  lv_obj_set_style_height(b, h - 6, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 0, LV_STATE_PRESSED);
  lv_obj_set_style_outline_width(b, 5, LV_STATE_PRESSED);
  lv_obj_set_style_outline_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  Color ol = is_light(face) ? hex(0x3A1F2B) : (g_mode == tc::Mode::Night ? hex(0xE4D8C0) : hex(0xFBF1DC));
  lv_obj_set_style_outline_color(b, C(ol), LV_STATE_PRESSED);
}

lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, Color face, Color text, const char *txt,
                 const lv_font_t *font, lv_event_cb_t cb, void *user) {
  lv_obj_t *b = box(parent, x, y, w, h, face, 18);
  pressable(b, face, h);
  if (txt && *txt) {
    lv_obj_t *l = label(b, font, text, 0, 0, txt);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -3);
  }
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

void anim(lv_obj_t *o, int32_t from, int32_t to, uint32_t ms, lv_anim_exec_xcb_t exec, lv_anim_ready_cb_t ready) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, ms);
  lv_anim_set_exec_cb(&a, exec);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  if (ready) lv_anim_set_ready_cb(&a, ready);
  lv_anim_start(&a);
}

void set_opa(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
void set_y(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, (lv_coord_t)v); }
void set_w(void *o, int32_t v) { lv_obj_set_width((lv_obj_t *)o, (lv_coord_t)v); }

void show(lv_obj_t *o, bool on) {
  bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (on && hidden) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else if (!on && !hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void set_text(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void set_x(lv_obj_t *o, int x) {
  if (lv_obj_get_x(o) != x) lv_obj_set_x(o, (lv_coord_t)x);
}

void set_text_color(lv_obj_t *o, Color c) {
  lv_color_t want = C(c);
  if (lv_obj_get_style_text_color(o, 0).full != want.full) lv_obj_set_style_text_color(o, want, 0);
}

// ---------------------------------------------------------------------------------------------
// Sheet plumbing

void close_sheet() {
  if (g_sheet_root) {
    // Usually called from a button inside the sheet: hide now, delete after the event finishes.
    lv_obj_add_flag(g_sheet_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_del_async(g_sheet_root);
  }
  g_sheet_root = nullptr;
  if (g_sheet == Sheet::CleanScreen && g_act) g_act->clean_screen(false);
  g_sheet = Sheet::None;
}

lv_obj_t *new_sheet(Sheet s, bool dim_overlay) {
  close_sheet();
  g_sheet = s;
  if (dim_overlay) {
    // Brief 10.2: the screen behind dims (a flat dark overlay at about 50 percent).
    g_sheet_root = box(lv_layer_top(), 0, 0, 800, 480, hex(0), 0);
    lv_obj_set_style_bg_opa(g_sheet_root, LV_OPA_50, 0);
    lv_obj_add_flag(g_sheet_root, LV_OBJ_FLAG_CLICKABLE);
  } else {
    g_sheet_root = plain(lv_layer_top(), 0, 0, 800, 480);
  }
  if (g_banner) lv_obj_move_foreground(g_banner);  // a doorbell stays on top of any sheet
  return g_sheet_root;
}

bool settings_family_open() {
  return g_sheet == Sheet::Settings || g_sheet == Sheet::TimeDate || g_sheet == Sheet::Test ||
         g_sheet == Sheet::Ringtone || g_sheet == Sheet::Zambretti;
}

}  // namespace detail

bool run_art_job() {
  for (detail::ArtSlot &s : detail::g_art)
    if (s.run()) return true;
  return false;
}

void set_art_scratch(uint8_t *buf, size_t bytes) {
  detail::g_scratch = buf;
  detail::g_scratch_bytes = bytes;
}

}  // namespace ui
