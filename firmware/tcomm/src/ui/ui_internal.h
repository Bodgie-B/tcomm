// Shared pieces of the interface: LVGL helpers, the pressed-button look, and the sheet plumbing.
// Internal to ui/; the rest of the firmware only sees ui.h.
#pragma once
#include "art.h"
#include "fonts/fonts.h"
#include "lvgl.h"
#include "theme.h"
#include "ui.h"
#include <atomic>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace ui {
namespace detail {

// One object for the life of the firmware, made where it is first named, in the LVGL heap: PSRAM on the
// panel (lv_conf.h), malloc on the PC. For interface state too big for internal RAM, which the Wi-Fi
// driver needs (field fix 2026-11-20). As a namespace-scope reference it is made while the statics are
// constructed: ESP-IDF has added PSRAM to the heap by then. Internal RAM only if PSRAM has no room.
template <class T>
T &ui_keep() {
  void *m = lv_mem_alloc(sizeof(T));
  if (!m) m = malloc(sizeof(T));
  if (!m) abort();
  return *new (m) T();
}

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
// The width a tile message's line may fill (note.h, Mike 2026-11-07): as wide as "Action taken on
// temperature" in the line's font, which just fits before the tile's badge. Two such lines at most.
inline int note_line_w() { return text_width("Action taken on temperature", B20); }
// How a tile message wraps (ui_note.cpp): greedy at spaces, a word wider than the line breaks
// mid-word, B20, note_line_w() wide. Returns the number of lines (3 means it needs a third: too
// long); line i is text[start[i], end[i]) with its trailing spaces dropped. start and end hold 3.
int note_wrap(const char *text, int start[3], int end[3]);
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
void open_comfort_survey();  // the downstairs tenant's "how does it feel?" column (ui_comfort.cpp)
void open_comfort_chart();   // the comfort chart (ui_chart.cpp), on either panel
// For the simulator: the view the chart's plot shows (t0, t1, g0, g1: degrees F across, gr/lb up) and
// whether it is at rest (0), on its opening hold (1) or gliding (2). Mike, 2026-11-18: the zoom.
int comfort_chart_peek(float view[4]);
void open_note_editor();     // this panel's tile message: the keyboard (ui_note.cpp)
void open_outside_chart();   // the outside history chart (ui_outside.cpp), on either panel
void comfort_refresh();      // its live reading, and its idle close (called from render)
void open_timer_sheet(tc::Status s);
// The substatus menu (Mike, 2026-11-26; ui_sheets.cpp): slides in over the right side, below the
// header, for the current status (none for Private). animate false: in place, for a change of light.
void open_sub_sheet(bool animate = true);
void sub_sheet_refresh();  // render: the outline follows the choice; closes on a status change or idle
void sub_sheet_closing();  // close_sheet's hook: the previews' pictures go
void open_settings();
void open_time_date();
void open_test_panel();
void open_ringtones();
void open_screensaver();  // Settings > SCREENSAVER: the minutes before it starts
void open_presence();     // Settings > PRESENCE SENSOR: fitted, active level, hold time, and the sensor live
void start_clean_screen();
void refresh_sheet();  // live values while a sheet is open (called from render)
void open_zambretti_demo();
// While the Zambretti demo is open: the sample to show on the outside tile instead of the real
// reading (false when it's closed).
bool zambretti_preview(tc::OutsideView &ov, bool &night);

// The on-screen keyboard (built in ui_calendar.cpp), shared by the calendar's event editor and the
// tile message editor (ui_note.cpp, Mike 2026-11-07). The owner gives three callbacks and keeps the
// field the text goes to: on_char gets the character already in its case and says whether it took it
// (shift only clears after a letter that went in), on_back removes one, on_done is the Done key.
// keypad_build makes the keys as children of `parent` at y (kKpShown px shown); free it by deleting
// kp.obj, and set kp.nkeys = 0 and kp.obj = nullptr.
// Its geometry (Mike, 2026-11-21: layout B, taller keys): 58 px keys, 5 px between the rows, 8 px over
// the first and 9 under the last, so 264 px shown, its top at screen y 216 (was 40 px keys at 288).
constexpr int kKeyH = 58;
constexpr int kKeyGap = 5;
constexpr int kKpShown = 8 + 4 * kKeyH + 3 * kKeyGap + 9;  // 264
constexpr int kKpTop = 480 - kKpShown;                     // 216
struct Keypad;
struct KeypadKey {
  uint8_t kind;
  char ch;    // a character key's place in its row's letters (row * 16 + column)
  int16_t w;  // LVGL's own width isn't there until its next layout
  lv_obj_t *obj, *lbl;
  Keypad *kp;
};
struct Keypad {
  lv_obj_t *obj = nullptr;
  KeypadKey keys[34] = {};  // 33 keys, 34 with the mic key (kp_build's static_assert)
  int nkeys = 0;
  bool shift = false, sym = false;
  tc::Mode mode = tc::Mode::Day;
  bool (*on_char)(char ch) = nullptr;
  void (*on_back)() = nullptr;
  void (*on_done)() = nullptr;
  // Voice notes (docs/voice-notes/DESIGN.md 2.1): set before keypad_build to get the blue mic key in
  // row 4. It returns the text of the field the keys go to (the voice text is inserted at its end
  // until tap-to-place-cursor lands), or null when no field takes text now. Null: no mic key.
  const char *(*on_mic)() = nullptr;
  int mic = -1;  // the mic key's index in keys, or -1 (kp_build)
};
void keypad_build(Keypad &kp, lv_obj_t *parent, int32_t y, tc::Mode mode);
void keypad_refresh(Keypad &kp);  // after the owner changes kp.shift or kp.sym

// Voice notes (ui_voice.cpp; docs/voice-notes/DESIGN.md 2). The Controller does the work through
// Actions::voice_*; View::voice says what to show.
// Called from render(): the badge recorder (Sheet::Voice), the keyboard's voice strip and the arrival
// dialog over the sender's tile.
void voice_render(const tc::View &v, uint32_t mono_ms, bool mode_changed);
void voice_badge_long(lv_event_t *e);   // the own badge's touch area: LONG_PRESSED opens the recorder
void open_voice_recorder();             // Sheet::Voice as the view stands (open_sheet, the simulator)
void voice_sheet_closing(Sheet s);      // close_sheet's hook, before the sheet s goes
void voice_key_event(lv_event_t *e);    // the keyboard's mic key (user data: its KeypadKey)
// An owner hides its keyboard (the calendar's Done or a picker): a voice job on it is cancelled and
// the strip goes. Deleting the keyboard does the same by itself.
void voice_keypad_gone(Keypad &kp);
bool voice_offered();                   // this panel can record now (View::voice.capable)

// The calendar roll-down (ui_calendar.cpp)
bool cal_is_open();  // rolled down at all (the arrival dialog waits while it is)
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
