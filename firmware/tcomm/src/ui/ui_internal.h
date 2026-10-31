// Shared pieces of the interface: LVGL helpers, the pressed-button look, and the sheet plumbing.
// Internal to ui/; the rest of the firmware only sees ui.h.
#pragma once
#include "art.h"
#include "fonts/fonts.h"
#include "lvgl.h"
#include "theme.h"
#include "ui.h"
#include <atomic>
#include <string.h>

namespace ui {
namespace detail {

extern Actions *g_act;
extern Frame g_f;
extern tc::Settings g_set;
extern tc::Mode g_mode;
extern Sheet g_sheet;
extern lv_obj_t *g_sheet_root;
extern lv_obj_t *g_banner;

extern const lv_font_t *J64, *J54, *J50, *J44, *J36, *J30, *J28, *J24;
extern const lv_font_t *B14, *B16, *B18, *B20, *B22, *B23, *B24, *B26;
// The calendar's (J76N, J22N, J21N: digits only).
extern const lv_font_t *J27, *J23, *J76N, *J22N, *J21N, *B11, *B12, *B13, *B15, *B19, *B21;

inline lv_color_t C(gfx::Color c) { return lv_color_make(c.r, c.g, c.b); }

// An art canvas shown through an lv_img.
struct Art {
  gfx::Canvas cv;
  lv_img_dsc_t dsc = {};
  bool make(int w, int h, bool alpha);
  void show(lv_obj_t *img);
};

// One piece of artwork to draw off the interface task (see run_art_job). Its canvases must not be
// on screen until the job is done.
enum class ArtKind : uint8_t { Sky, Floor, Outside, Bell };
struct ArtJob {
  ArtKind kind = ArtKind::Sky;
  gfx::Canvas *cv = nullptr;
  gfx::Canvas *cv2 = nullptr;  // Bell: the pressed version
  tc::Phase phase = tc::Phase::Midday;
  float disc_p = 0;
  bool moon = false;
  tc::Floor floor = tc::Floor::Upstairs;
  int status = 0;              // a Status, or theme::kNoSignal
  tc::Mode mode = tc::Mode::Day;
  int key = 0;                 // what the interface asked for, to recognise the result
};

// A job slot shared by the interface task (submit, take) and the drawing task (run). The atomic
// state hands the job and its canvases over in one direction at a time.
class ArtSlot {
 public:
  bool idle() const { return state_.load(std::memory_order_acquire) == kIdle; }
  void submit(const ArtJob &j);  // only when idle
  bool take(ArtJob &out);        // the finished job, once; the slot is idle again afterwards
  bool run();                    // drawing task: draws the waiting job, if there is one

 private:
  enum : uint8_t { kIdle, kWaiting, kRunning, kDone };
  std::atomic<uint8_t> state_{kIdle};
  ArtJob job_;
};

// Tiles first: they answer a tap. Then the sky, then what only changes with day and night.
enum ArtSlotId { kSlotTileUp, kSlotTileDown, kSlotSky, kSlotOutside, kSlotBell, kSlotCount };
extern ArtSlot g_art[kSlotCount];

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, gfx::Color bg, int radius);
lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, gfx::Color col, int x, int y, const char *text = "");
int text_width(const char *t, const lv_font_t *font, int letter_space = 0);
// The label y that puts a text's baseline where a board has it, given the top of the text's line
// box on the board (its CSS content area).
int board_y(const lv_font_t *font, float css_top);
lv_obj_t *image(lv_obj_t *parent, int x, int y);
void pressable(lv_obj_t *b, gfx::Color face, int h);
// Setters that leave LVGL alone when nothing changed: an unchanged frame must not redraw anything.
void set_text(lv_obj_t *label, const char *text);
void set_x(lv_obj_t *o, int x);
void set_text_color(lv_obj_t *o, gfx::Color c);
lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, gfx::Color face, gfx::Color text, const char *txt,
                 const lv_font_t *font, lv_event_cb_t cb, void *user);
void outline(lv_obj_t *o, gfx::Color c);
void anim(lv_obj_t *o, int32_t from, int32_t to, uint32_t ms, lv_anim_exec_xcb_t exec, lv_anim_ready_cb_t ready = nullptr);
void set_opa(void *o, int32_t v);
void set_y(void *o, int32_t v);
void set_w(void *o, int32_t v);
void show(lv_obj_t *o, bool on);

gfx::Color ink();   // dark text on sheets by day, #E4D8C0 at night
gfx::Color chip();  // neutral button face on sheets

// Sheets
void close_sheet();
lv_obj_t *new_sheet(Sheet s, bool dim_overlay);
bool settings_family_open();
void open_status_sheet();
void open_timer_sheet(tc::Status s);
void open_settings();
void open_time_date();
void open_test_panel();
void open_ringtones();
void start_clean_screen();
void refresh_sheet();  // live values while a sheet is open (called from render)
void open_zambretti_demo();
// While the Zambretti demo is open: the sample to show on the outside tile instead of the real
// reading (false when it's closed).
bool zambretti_preview(tc::OutsideView &ov, bool &night);

// The calendar roll-down (ui_calendar.cpp)
bool cal_is_open();
bool cal_covering();  // rolled all the way down: the tiles are hidden under it
void cal_open(bool animate);
void cal_close(bool animate);
void cal_toggle();
void report_anim(const char *what, int frames, uint32_t ms);  // for anim_report
void cal_render(bool mode_changed);
// ui.cpp, for the calendar: put an object under the glow and the icicles; hide or show the tiles.
void under_overlays(lv_obj_t *o);
void main_tiles_visible(bool on);

}  // namespace detail
}  // namespace ui
