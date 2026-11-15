// The campsite screensaver on the panel (screensaver/core, built in as the ss_core library).
// Stage 8 of docs/screensaver/START_HERE.md: it starts on its own after 10 minutes with no touch,
// plays the panel's real inputs, reacts to the doorbell, and a touch reveals the main screen.
//
// While it runs: LVGL draws the main screen's changes (a slice after each scene frame) into a copy of the
// main screen in PSRAM (768,000 bytes; LVGL's direct mode), not onto the panel; LVGL's 64,000-byte
// draw buffer (internal RAM) is lent to the scene as its fast block; the scene's layer cache
// (768 KB) is the interface's spare picture, lent for the run (art change-overs wait until it
// stops; ss_host.cpp sceneAlloc, core/spare_loan.h), or its own PSRAM when the spare isn't free;
// its App goes in PSRAM; an LVGL timer ticks it at 25 Hz and writes the strips that
// changed straight into the panel's frame buffer. LVGL's other timers (the controller, the radio,
// the interface's updates) keep running behind it, so the main screen underneath stays current.
// Without room for the copy it still runs: LVGL then draws nothing (its invalidations are dropped,
// since in LVGL 8.4 any change would resume a paused redraw) and a touch draws the main screen
// first (244 ms on this panel).
//
// A touch (only a touch: brief section 11) runs the reveal: the copy goes into the scene's cache,
// a circle grows from the finger showing it through the scene (0.35 s), then the copy goes onto the
// panel and LVGL has the screen back. The touch presses nothing; the finger is kept from LVGL until
// it lifts. It also counts as a touch for the backlight (full light at night, as any touch).
//
// It doesn't start while the screen is dark ("screen off overnight") and stops, without a reveal,
// if the screen goes dark. The doorbell: the controller still plays the chime and keeps the banner
// for the main screen; the hiker turns and waves and the status chip pulses.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tc {
struct View;
struct Settings;
struct WallTime;
}  // namespace tc

namespace ui {

void screensaver_set_frame_buffer(void* fb);   // the RGB panel's frame buffer, 800x480 RGB565

// All below with the LVGL lock held (the LVGL task, or a serial command).

// From app_tick after the controller's tick (every 50 ms): starts it after the idle time with no
// touch, keeps its inputs current, passes a new doorbell on, stops it if the screen goes dark.
// idle_ms: LVGL's time since the last touch (lv_disp_get_inactive_time).
void screensaver_service(const tc::View& v, const tc::Settings& s, const tc::WallTime& w, int32_t utc_offset,
                         uint32_t idle_ms);

bool screensaver_start(uint32_t seed);     // now, whatever the idle time
void screensaver_stop();                   // now, no reveal (LVGL repaints the main screen)
void screensaver_touch(int x, int y);      // what a touch at (x, y) does: the reveal
bool screensaver_running();
void screensaver_doorbell();               // the hiker's reaction and the chip's pulse (no chime)

// Test settings from serial; not saved, back to the defaults at each start-up.
void screensaver_set_auto(bool on);        // start on its own after the idle time (default on)
bool screensaver_auto();
// The wait before it starts: Settings > SCREENSAVER (screensaver_min, 10 minutes to begin with);
// set_idle replaces it until restart (seconds; 0 goes back to the Settings value).
void screensaver_set_idle(uint32_t seconds);
uint32_t screensaver_idle();
void screensaver_set_mood(float mood);     // 0 night .. 5 dusk (palette.h) in place of the clock's; < 0: the clock's again
void screensaver_set_scene(int family);    // the next loop's: 0 its own pick, 1 evening, 2 morning, 3 midday, 4 lookout, 5 sunrise
// The scene weather by hand (0 clear, 1 rain, 2 snow, 3 wind, 4 heat, 5 fog, 6 chill, 7 storm), with
// the tint such a reading brings; -1 back to the outside reading. The loop's weather is planned at
// its start, so start a new one (ss seed N) to see it.
void screensaver_set_weather(int code);
// The night scene's brightness (palette.h WeatherTint::night_gain): 1.6 to begin with; 0 is the PC
// preview's look (0.55), which on the panel's night backlight leaves only the stream readable.
void screensaver_set_night_gain(float gain);
// The set pieces (stage 10, stage 15): 0 plays the Lookout in the window round the real sunset and the
// sunrise overlook in the window round the real sunrise, each once a day, counted once its key moment
// has played (the default); 1 the Lookout now, every loop; 2 both off; 3 the sunrise now, every loop.
// Until restart. (Serial: ss lookout N.)
void screensaver_set_lookout(int mode);
// How fast a medium palette step (the Lookout's sunset) washes down the screen: bands a frame; 0, at
// once, is the default (Mike's choice). Until restart.
void screensaver_set_spread_medium(int bands);
int screensaver_spread_medium();
float screensaver_night_gain();

// One line each, for serial: frame stats since the last call; the inputs in use. Starts, stops and
// each reveal's timing go to the panel's log (board::elog).
int screensaver_stats(char* buf, size_t n);
int screensaver_inputs(char* buf, size_t n);

}  // namespace ui
