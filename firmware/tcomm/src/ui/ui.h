// The LVGL interface (LVGL 8.4). It draws a tc::View and turns touches into Actions; it holds no
// application state of its own beyond which sheet is open. Call everything with the LVGL lock held.
#pragma once
#include "../app/controller.h"

namespace ui {

struct DemoInfo {
  bool available = false;
  bool unplugged = false;
  int auto_answer = 0;  // 0 Come on, 1 Not now, 2 Off
  int phase = 6;        // 0-5 forced phase, 6 = clock
  char display[64] = "";  // the display test's current choice; empty where there is none
  bool strips = false;    // artwork drawn through internal RAM
};

enum DemoAction {
  kDemoPeerStatus,
  kDemoPeerPlug,
  kDemoPeerRing,
  kDemoPeerDog,
  kDemoLucy,
  kDemoOutside,
  kDemoAutoAnswer,
  kDemoPhase,
  kDemoDisplay,  // display tests: next clock and buffer choice
  kDemoStrips,   // display tests: strip drawing on or off
  kDemoPeerMail, // the other floor taps its mail badge
};

class Actions {
 public:
  virtual ~Actions() {}
  virtual void choose_status(tc::Status s) = 0;
  virtual void start_timer(tc::Status s, uint32_t seconds, tc::Status fallback) = 0;
  virtual void tap_bell() = 0;
  virtual void tap_dog() = 0;
  virtual void tap_mail() = 0;
  virtual void answer(bool come_on) = 0;
  virtual void dismiss_banner() = 0;
  virtual void apply_settings(const tc::Settings &s) = 0;
  virtual void set_clock_local(int64_t local_seconds) = 0;  // the platform applies the time-zone rules
  virtual void clean_screen(bool on) = 0;
  virtual void preview(tc::Sound s, uint8_t volume) = 0;
  virtual bool floor_locked() { return false; }  // Settings hides the floor swap when true
  virtual bool updater_installed() { return false; }  // Settings > Update over Wi-Fi
  virtual void start_updater() {}
  virtual DemoInfo demo_info() = 0;
  virtual void demo(DemoAction a) = 0;
  // The calendar. save_event returns the event's id, or 0 if it couldn't be kept (the calendar is
  // full, or the event was deleted meanwhile).
  virtual uint32_t save_event(const tc::Event &e) = 0;
  virtual bool delete_event(uint32_t id) = 0;
  virtual bool restore_event(uint32_t id) = 0;
  // One occurrence of a series on its own ("just this one"): see tc::Controller.
  virtual uint32_t save_occurrence(uint32_t series, int32_t day, const tc::Event &e) = 0;
  virtual bool delete_occurrence(uint32_t series, int32_t day) = 0;
  virtual bool restore_occurrence(uint32_t series, int32_t day) = 0;
};

struct Frame {
  const tc::View *view;
  const tc::Settings *settings;
  tc::OwnState own;
  tc::WallTime wall;
  int32_t utc_offset;
  uint32_t mono_ms;
  tc::CalendarSource *events;  // the calendar
};

void init(Actions &actions);
void render(const Frame &f);

// The large artwork (sky, floor tiles, outside, doorbell) is drawn off the interface task, so a
// redraw never freezes touch. The panel calls this from its own drawing task; the simulator calls
// it after each step. Draws one waiting piece and returns true, or returns false if none waits.
// It touches no LVGL objects, so it doesn't need the LVGL lock.
bool run_art_job();
// Memory the jobs draw in, a strip of rows at a time (the panel gives it fast internal RAM).
// Without it they draw straight into their PSRAM canvases.
void set_art_scratch(uint8_t *buf, size_t bytes);
// Called between the few-KB pieces of a finished picture's copy into its place on screen, to give
// the screen's own PSRAM reads room (the panel waits a few microseconds).
void set_copy_pause(void (*pause)());

// For the simulator: open sheets the way a touch would, and scroll the open one's list.
enum class Sheet { None, Status, Timer, Settings, TimeDate, Test, CleanScreen, Ringtone, Zambretti };
void open_sheet(Sheet s, tc::Status timed = tc::Status::Private);
void scroll_sheet(int y);
void tap_gear();  // as a tap on the gear (Settings rolls down or up), for testing over serial
// For the simulator: the Zambretti demo with sample i chosen (0-25 the letters A-Z, then snow,
// danger heat, danger cold), by day or by night.
void zambretti_pick(int i, bool night);
// For the simulator: the calendar rolled down (at once) in a view with a day selected, or its editor
// for an event (edit_id 0: a new one on that day).
enum class CalView { Closed, Month, Week, Day, Edit, PickDate, PickTime, PickLength, PickRepeat, AskSeries };
void show_calendar(CalView v, int32_t day, uint32_t edit_id = 0);
// The last animation of the calendar or the Settings roll-down, for the panel's log (calendar spec:
// one under about 20 fps goes): serial counts them, frames is how many steps LVGL drew in ms.
struct AnimReport {
  uint32_t serial;
  const char *what;
  int frames;
  uint32_t ms;
};
AnimReport anim_report();

}  // namespace ui
