// Screenshot simulator: LVGL 8.4 compiled for the PC, drawing into memory, driven by the same
// controller, demo and interface code as the panel. Writes one raw RGB file per scenario; the
// build script turns them into PNGs in docs/screens.
//   sim.exe <output folder>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "app/controller.h"
#include "app/demo.h"
#include "app/mem_files.h"
#include "app/panel_app.h"
#include "core/civil.h"
#include "lvgl.h"
#include "ui/fonts/fonts.h"
#include "ui/ui.h"

using namespace tc;

static lv_color_t g_fb[800 * 480];
static lv_color_t g_draw[800 * 480];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px) {
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) g_fb[y * 800 + x] = *px++;
  lv_disp_flush_ready(d);
}

class SimPlatform : public Platform {
 public:
  uint32_t mono = 1000;
  WallTime w;
  int32_t offset = -4 * 3600;
  Demo *demo = nullptr;
  uint32_t r = 7;
  uint32_t mono_ms() override { return mono; }
  WallTime wall() override { return w; }
  void set_wall(uint32_t u, uint8_t q) override { w = WallTime{true, u, q}; }
  int32_t utc_offset(uint32_t) override { return offset; }
  void radio_send(uint8_t to, const uint8_t *d, size_t n) override {
    if (demo) demo->from_panel(to, d, n);
  }
  void play(Sound, uint8_t) override {}
  void set_backlight(uint8_t) override {}
  uint32_t random32() override { return r = r * 1103515245u + 12345u; }
  uint8_t *read_file(const char *name, size_t &len) override { return files.read(*this, name, len); }
  void write_file(const char *name, uint8_t *buf, size_t len) override { files.write(*this, name, buf, len); }
  MemFiles files;
};

struct Harness {
  SimPlatform host;
  Controller panel{host};
  Demo demo{host};
  PanelApp app{panel, host, &demo};
  Harness() { host.demo = &demo; }
};

static std::unique_ptr<Harness> g_h;

// The interface keeps one Actions pointer; this forwards to whichever harness is current.
class Proxy : public ui::Actions {
 public:
  void choose_status(Status s) override { g_h->app.choose_status(s); }
  void start_timer(Status s, uint32_t t, Status f) override { g_h->app.start_timer(s, t, f); }
  void tap_bell() override { g_h->app.tap_bell(); }
  void tap_dog() override { g_h->app.tap_dog(); }
  void tap_mail() override { g_h->app.tap_mail(); }
  void answer(bool y) override { g_h->app.answer(y); }
  void dismiss_banner() override { g_h->app.dismiss_banner(); }
  void apply_settings(const Settings &s) override { g_h->app.apply_settings(s); }
  void set_clock_local(int64_t l) override { g_h->app.set_clock_local(l); }
  void clean_screen(bool on) override { g_h->app.clean_screen(on); }
  void preview(Sound s, uint8_t v) override { g_h->app.preview(s, v); }
  ui::DemoInfo demo_info() override { return g_h->app.demo_info(); }
  void demo(ui::DemoAction a) override { g_h->app.demo(a); }
  uint32_t save_event(const Event &e) override { return g_h->app.save_event(e); }
  bool delete_event(uint32_t id) override { return g_h->app.delete_event(id); }
  bool restore_event(uint32_t id) override { return g_h->app.restore_event(id); }
  uint32_t save_occurrence(uint32_t s, int32_t d, const Event &e) override { return g_h->app.save_occurrence(s, d, e); }
  bool delete_occurrence(uint32_t s, int32_t d) override { return g_h->app.delete_occurrence(s, d); }
  bool restore_occurrence(uint32_t s, int32_t d) override { return g_h->app.restore_occurrence(s, d); }
} g_proxy;

// A finger: tap() presses for 100 ms at a point, then lets go.
static lv_point_t g_touch = {0, 0};
static bool g_pressed = false;
static bool g_awake = true;  // false: let the idle timers run (the calendar's auto close)

static void touch_read(lv_indev_drv_t *, lv_indev_data_t *d) {
  d->point = g_touch;
  d->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void pump(int ms);

static void tap(int x, int y, int after = 250) {
  g_touch = {(lv_coord_t)x, (lv_coord_t)y};
  g_pressed = true;
  pump(100);
  g_pressed = false;
  pump(after);
}

static void pump(int ms) {
  if (g_awake) lv_disp_trig_activity(nullptr);  // keep the sheets' idle timers from firing
  for (int t = 0; t < ms; t += 20) {
    Harness &h = *g_h;
    h.host.mono += 20;
    h.panel.tick();
    h.demo.tick();
    ui::Frame f{&h.panel.view(), &h.panel.settings(), h.panel.own(), h.host.wall(), h.host.offset, h.host.mono,
                &h.panel};
    ui::render(f);
    while (ui::run_art_job()) {}  // the panel draws these on its other core; here, at once
    lv_tick_inc(20);
    lv_timer_handler();
  }
}

static std::string g_out;

static void capture(const char *name) {
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);
  std::string path = g_out + "/" + name + ".rgb";
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  for (int i = 0; i < 800 * 480; i++) {
    uint16_t v = g_fb[i].full;
    // 5 and 6 bits to 8 as a display shows them: the top bits repeated, so 31 is 255, not 248.
    uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    uint8_t rgb[3] = {(uint8_t)(r << 3 | r >> 2), (uint8_t)(g << 2 | g >> 4), (uint8_t)(b << 3 | b >> 2)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
  std::printf("%s\n", name);
}

static uint32_t at(int y, int mo, int d, int hh, int mm, int32_t off) {
  return (uint32_t)(seconds_from_civil(y, mo, d, hh, mm, 0) - off);
}

struct Setup {
  int phase;
  uint32_t now;
  int32_t off = -4 * 3600;
  Floor me = Floor::Downstairs;
  Status mine;
  uint32_t mine_since;
  Status peer;
  uint32_t peer_since;
  int16_t my_t, my_h, peer_t, peer_h;
  Dog dog = Dog::None;
  int16_t out_t = 0;
  uint8_t out_h = 0;
  Forecast out_code = Forecast::Unknown;
  const char *phrase = nullptr;
  uint8_t warn = 0;  // WeatherGate's danger heat / danger cold flags
};

static void start(const Setup &s) {
  ui::open_sheet(ui::Sheet::None);
  ui::show_calendar(ui::CalView::Closed, 0);
  g_h.reset(new Harness());
  Harness &h = *g_h;
  h.host.w = WallTime{s.now != 0, s.now, kClockManual};
  h.host.offset = s.off;
  Settings st;
  st.floor = s.me;
  OwnState own;
  own.status = s.mine;
  own.since = s.mine_since;
  DogState dog;
  for (int i = 0; i < 2; i++)
    if ((uint8_t)s.dog & (1 << i)) dog.dog[i] = DogOne{true, s.now - 600, 1};
  h.panel.begin(st, own, dog);
  h.panel.on_local_reading(true, s.my_t, (uint16_t)s.my_h);
  h.panel.force_phase(s.phase);
  h.demo.begin(h.panel);
  OwnState peer;
  peer.status = s.peer;
  peer.since = s.peer_since;
  h.demo.set_peer(peer, true, s.peer_t, (uint16_t)s.peer_h);
  pump(400);
  if (s.phrase) h.demo.gateway_outside(s.out_t, s.out_h, s.out_code, s.phrase, s.warn);
  pump(700);
}

// The calendar boards' sample events (October 2026). start -1: all day, length in days; else
// minutes. Each is saved on the panel of its floor, so the other one gets it by the radio.
struct CalFx {
  int day, start, len;
  bool up;
  const char *title, *notes;
};
const CalFx kMonthFx[] = {
    {3, 9 * 60, 60, true, "Yard sale", ""},
    {8, -1, 1, false, "Furnace filter", ""},
    {12, 18 * 60 + 30, 90, false, "Dinner", ""},
    {13, 14 * 60, 60, true, "Dentist", ""},
    {14, -1, 3, true, "Work trip, Denver", ""},
    {15, 15 * 60 + 30, 60, false, "Parcel", ""},
    {17, -1, 1, true, "Farmers mkt", ""},
    {20, 19 * 60, 90, false, "Book club", ""},
    {22, -1, 4, false, "Camping, Shenandoah", ""},
    {24, -1, 1, true, "Leaf pickup", ""},
    {28, 9 * 60, 120, true, "Furnace svc", ""},
    {31, 18 * 60, 120, true, "Trick-or-treat", ""},
};
const CalFx kWeekFx[] = {
    {12, 18 * 60 + 30, 90, false, "Dinner out", ""},
    {13, 14 * 60 + 30, 60, true, "Dentist", ""},
    {14, -1, 3, true, "Work trip, Denver", ""},
    {15, 15 * 60 + 30, 60, false, "Parcel pickup", "Ask neighbor, bring ID"},
    {17, 9 * 60, 180, true, "Farmers market", ""},
};
const CalFx kDayFx[] = {
    {14, -1, 3, true, "Work trip, Denver", ""},
    {15, 10 * 60, 30, true, "Call with contractor", ""},
    {15, 15 * 60 + 30, 60, false, "Parcel pickup", "ask neighbor, bring ID"},
    {15, 18 * 60 + 30, 120, true, "Dinner at Sam's", "bring dessert"},
};

template <size_t N>
static void load_events(const CalFx (&fx)[N]) {
  Harness &h = *g_h;
  bool me_up = h.panel.settings().floor == Floor::Upstairs;
  for (const CalFx &f : fx) {
    Event e;
    e.day = (int32_t)days_from_civil(2026, 10, f.day);
    e.all_day = f.start < 0;
    e.start_min = (int16_t)(f.start < 0 ? 0 : f.start);
    e.length = (uint16_t)f.len;
    std::snprintf(e.title, sizeof(e.title), "%s", f.title);
    std::snprintf(e.notes, sizeof(e.notes), "%s", f.notes);
    if (!(f.up == me_up ? h.panel.save_event(e) : h.demo.peer().save_event(e))) std::printf("  couldn't save %s\n", f.title);
    pump(100);  // the radio carries it across
  }
  pump(500);
  if (h.panel.events().count() != (int)N) std::printf("  calendar has %d events, not %d\n", h.panel.events().count(), (int)N);
}

// A series or a birthday, saved on this panel.
static uint32_t add_event(const char *title, int y, int m, int d, int start, int len, Repeat rep = Repeat::None,
                          EventKind kind = EventKind::Plain) {
  Event e;
  e.day = (int32_t)days_from_civil(y, m, d);
  e.all_day = start < 0;
  e.start_min = (int16_t)(start < 0 ? 0 : start);
  e.length = (uint16_t)len;
  e.repeat = rep;
  e.kind = kind;
  std::snprintf(e.title, sizeof(e.title), "%s", title);
  uint32_t id = g_h->panel.save_event(e);
  pump(60);
  return id;
}

// The top 70 rows of the screen, for a sheet of date strips.
static std::vector<uint8_t> g_strips;
static void strip_row() {
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);
  for (int i = 0; i < 800 * 70; i++) {
    uint16_t v = g_fb[i].full;
    uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    g_strips.push_back((uint8_t)(r << 3 | r >> 2));
    g_strips.push_back((uint8_t)(g << 2 | g >> 4));
    g_strips.push_back((uint8_t)(b << 3 | b >> 2));
  }
}

// Types on the calendar's keyboard (letters and spaces), by tapping its keys.
static void type_keys(const char *s) {
  const char *rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const float x0[3] = {8, 48, 8 + 1.5f * 73.9f + 5};
  const int ys[3] = {288 + 8 + 20, 288 + 53 + 20, 288 + 98 + 20};
  for (const char *s0 = s; *s; s++) {
    if (std::isupper((unsigned char)*s) && s != s0) tap(63, 288 + 98 + 20, 60);  // shift (the first letter has it)
    if (*s == ' ') {
      tap(366, 288 + 143 + 20, 60);
      continue;
    }
    for (int r = 0; r < 3; r++) {
      const char *p = std::strchr(rows[r], std::tolower((unsigned char)*s));
      if (p) tap((int)(x0[r] + (p - rows[r]) * 78.9f + 37), ys[r], 60);
    }
  }
}

static int g_fails = 0;
static void check(bool ok, const char *what) {
  std::printf("  %s: %s\n", ok ? "ok" : "FAILED", what);
  if (!ok) g_fails++;
}

static uint32_t event_named(const char *title, bool repeating = false) {
  const EventStore &s = g_h->panel.events();
  for (int i = 0; i < s.count(); i++)
    if (!std::strcmp(s.at(i).title, title) && s.at(i).repeats() == repeating && !s.at(i).deleted) return s.at(i).id;
  return 0;
}

// The status word and temperature fonts hold only the characters those labels can show (2026-11-01,
// tools/make_fonts.sh). Every status word, NO SIGNAL and every temperature character must be there.
static int check_trimmed_fonts() {
  int missing = 0;
  auto has = [&](const lv_font_t *f, uint32_t c, const char *what) {
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(f, &g, c, 0)) {
      std::printf("  MISSING: U+%04X in %s\n", (unsigned)c, what);
      missing++;
    }
  };
  std::vector<std::string> words = {"NO SIGNAL"};
  for (int s = 0; s <= (int)Status::Away; s++) words.push_back(status_word((Status)s));
  for (const std::string &w : words)
    for (char c : w) {
      has(&font_jost_800_64, (uint8_t)c, "font_jost_800_64");
      has(&font_jost_800_54, (uint8_t)c, "font_jost_800_54");
    }
  for (uint32_t c : {0x30u, 0x31u, 0x32u, 0x33u, 0x34u, 0x35u, 0x36u, 0x37u, 0x38u, 0x39u, 0x2Du, 0x46u, 0xB0u}) {
    has(&font_jost_800_50, c, "font_jost_800_50");
    has(&font_jost_800_36, c, "font_jost_800_36");
  }
  return missing;
}

int main(int argc, char **argv) {
  g_out = argc > 1 ? argv[1] : ".";
  lv_init();
  std::printf("trimmed fonts: %d missing\n", check_trimmed_fonts());
  static lv_disp_draw_buf_t buf;
  lv_disp_draw_buf_init(&buf, g_draw, nullptr, 800 * 480);
  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res = 800;
  drv.ver_res = 480;
  drv.flush_cb = flush;
  drv.draw_buf = &buf;
  drv.full_refresh = 1;
  lv_disp_drv_register(&drv);
  static lv_indev_drv_t idrv;
  lv_indev_drv_init(&idrv);
  idrv.type = LV_INDEV_TYPE_POINTER;
  idrv.read_cb = touch_read;
  lv_indev_drv_register(&idrv);
  g_h.reset(new Harness());
  static uint8_t scratch[32 * 1024];  // the same size the panel gives it: every screen goes through strips
  ui::set_art_scratch(scratch, sizeof(scratch));
  ui::init(g_proxy);

  const int32_t EDT = -4 * 3600, EST = -5 * 3600;
  // The six boards. Times, statuses, readings and forecasts are the boards' sample data.
  Setup p1{0, at(2026, 10, 12, 23, 26, EDT), EDT, Floor::Downstairs, Status::Private, at(2026, 10, 12, 23, 5, EDT),
           Status::Sleeping, at(2026, 10, 12, 22, 15, EDT), 650, 510, 690, 440, Dog::None, 480, 71, Forecast::Moon,
           "Clear, cooler"};
  Setup p2{1, at(2026, 10, 13, 6, 20, EDT), EDT, Floor::Downstairs, Status::Open, at(2026, 10, 13, 6, 5, EDT),
           Status::Sleeping, at(2026, 10, 12, 22, 40, EDT), 640, 520, 680, 430, Dog::Copper, 440, 80, Forecast::CloudSun,
           "Fair, mild"};
  Setup p3{2, at(2026, 12, 25, 9, 0, EST), EST, Floor::Downstairs, Status::Busy, at(2026, 12, 25, 8, 45, EST),
           Status::Open, at(2026, 12, 25, 7, 10, EST), 660, 380, 700, 310, Dog::None, 310, 78, Forecast::Snow,
           "Snow likely"};  // Christmas: the date strip's "Merry Christmas" comes from the holiday table
  Setup p4{3, at(2026, 10, 12, 12, 30, EDT), EDT, Floor::Downstairs, Status::Busy, at(2026, 10, 12, 12, 5, EDT),
           Status::Away, at(2026, 10, 12, 11, 50, EDT), 670, 470, 720, 400, Dog::Copper, 630, 52, Forecast::Rain,
           "Rain likely"};
  Setup p5{4, at(2026, 10, 12, 18, 10, EDT), EDT, Floor::Downstairs, Status::Open, at(2026, 10, 12, 16, 10, EDT),
           Status::Private, at(2026, 10, 12, 17, 30, EDT), 680, 460, 730, 410, Dog::None, 590, 58, Forecast::Sun,
           "Settled, fine"};
  Setup p6{5, at(2026, 10, 12, 19, 25, EDT), EDT, Floor::Downstairs, Status::Open, at(2026, 10, 12, 18, 40, EDT),
           Status::Busy, at(2026, 10, 12, 18, 55, EDT), 650, 480, 700, 420, Dog::Copper, 540, 63, Forecast::CloudSun,
           "Showery, bright"};

  start(p1); capture("01_p1_night");
  start(p2); capture("02_p2_dawn");
  start(p3); capture("03_p3_morning_christmas");
  start(p4); capture("04_p4_midday");
  start(p5); capture("05_p5_golden");

  // P6: upstairs went quiet at 7:08 PM; at 7:25 this panel sends something, gets no ACK: NO SIGNAL.
  {
    Setup s = p6;
    s.now = at(2026, 10, 12, 19, 8, EDT);
    start(s);
    g_h->demo.peer_toggle_unplugged();
    g_h->host.w.utc = at(2026, 10, 12, 19, 25, EDT);
    g_h->panel.on_local_reading(true, 670, 480);  // a 2 F move to the board's 67 F: sends READING, which fails
    pump(2200);
    capture("06_p6_dusk_no_signal");
  }

  start(p4); ui::open_sheet(ui::Sheet::Status); pump(400); capture("07_status_sheet_day");
  start(p4); ui::open_sheet(ui::Sheet::Timer, Status::Private); pump(300); capture("08_timer_sheet_day");
  start(p4); g_h->panel.choose_status(Status::Away); pump(400); capture("09_status_changed");
  start(p5); g_h->panel.tap_bell(); pump(300); capture("10_doorbell_delivered");
  start(p5); g_h->panel.tap_bell(); pump(3500); capture("11_doorbell_come_on");
  start(p4); g_h->demo.peer_ring(); pump(500); capture("12_banner_day");
  start(p1); g_h->demo.peer_ring(); pump(500); capture("13_banner_night");
  {
    Setup s = p4;
    s.mine = Status::Away;
    start(s);
    g_h->demo.peer_ring();
    pump(400);
    g_h->panel.choose_status(Status::Open);
    pump(400);
    capture("14_missed_while_away");
  }
  {
    Setup s = p5;
    start(s);
    g_h->demo.gateway_toggle_lucy();  // LucyGate says Lucy is out; this downstairs panel can't tap
    pump(600);
    capture("15_lucy_out_downstairs");
  }
  start(p4); ui::open_sheet(ui::Sheet::Settings); pump(300); capture("16_settings_day");
  start(p1); ui::open_sheet(ui::Sheet::Settings); pump(300); capture("17_settings_night");
  start(p4); ui::open_sheet(ui::Sheet::Test); pump(300); capture("18_test_panel");
  start(p1); ui::open_sheet(ui::Sheet::CleanScreen); pump(300); capture("19_clean_screen");
  start(p1); ui::open_sheet(ui::Sheet::Status); pump(400); capture("20_status_sheet_night");
  start(p1); ui::open_sheet(ui::Sheet::Timer, Status::Sleeping); pump(300); capture("21_timer_sheet_night");
  {
    Setup s = p4;
    s.now = 0;  // both panels lost power and there's no coin cell
    start(s);
    capture("22_clock_not_set");
  }
  {
    Setup s = p4;
    s.me = Floor::Upstairs;
    s.mine = Status::Open;
    s.peer = Status::Busy;
    start(s);
    g_h->panel.start_timer(Status::Private, 3600, Status::Busy);
    pump(6000);
    capture("23_upstairs_unit_timer");
  }
  {
    start(p5);
    g_h->demo.peer_toggle_unplugged();
    g_h->panel.tap_bell();
    pump(2000);
    capture("24_couldnt_reach");
  }
  start(p4); ui::open_sheet(ui::Sheet::TimeDate); pump(300); capture("25_time_and_date");
  start(p2); ui::open_sheet(ui::Sheet::Status); pump(400); capture("26_status_sheet_evening");
  // The ringtone list: the top with Westminster chosen, then the nature ones with Chickadee chosen.
  start(p4); ui::open_sheet(ui::Sheet::Ringtone); pump(300); capture("27_ringtones");
  {
    start(p4);
    Settings st = g_h->panel.settings();
    st.ringtone = 7;
    g_h->panel.apply_settings(st);
    ui::open_sheet(ui::Sheet::Ringtone);
    ui::scroll_sheet(380);
    pump(300);
    capture("28_ringtones_nature");
  }
  start(p1); ui::open_sheet(ui::Sheet::Ringtone); ui::scroll_sheet(800); pump(300); capture("29_ringtones_night");

  // Both dogs out (Mike, 2026-10-25: it happens now).
  {
    Setup s = p4;
    s.dog = Dog::Both;
    start(s);
    capture("34_both_dogs_out");
  }

  // Mail for downstairs (Mike, 2026-10-26). Upstairs: the faint outline at the right end of the
  // DOWNSTAIRS line; tapped (by touch here), solid. Downstairs: nothing until mail is marked, then
  // solid, fading to the outline and back until the tenant taps it.
  {
    Setup s = p4;
    s.me = Floor::Upstairs;
    start(s);
    capture("35_mail_outline_upstairs");
    tap(12 + 362, 279 + 24);  // the badge's centre, on screen
    pump(300);
    capture("36_mail_marked_upstairs");
  }
  {
    Setup s = p4;  // downstairs
    start(s);
    g_h->demo.peer_tap_mail();  // upstairs marks it
    pump(160);
    capture("37_mail_new_downstairs");
    pump(1800);
    capture("38_mail_fading_downstairs");
  }
  {
    Setup s = p1;  // night
    s.me = Floor::Upstairs;
    start(s);
    capture("39_mail_outline_upstairs_night");
  }

  // The doorbell (Mike, 2026-10-26): the bell shakes for 3 s on a ring sent or received, and an
  // incoming ring turns it red until it's answered, for 2 minutes at most.
  {
    start(p4);
    tap(538 + 125, 279 + 82);  // DOORBELL
    pump(240);
    capture("40_bell_shaking_sent");
    pump(4000);
    g_h->demo.peer_ring();
    pump(300);
    capture("41_bell_red_incoming");
    pump(62000);  // the banner has gone; the bell is still red
    capture("42_bell_red_after_banner");
  }

  // The date and time at 26 px, or smaller for a long date that would reach the scenery (Mike,
  // 2026-10-26: about 20 percent bigger).
  {
    Setup s = p4;
    s.now = at(2026, 9, 30, 12, 30, EDT);  // "Wednesday, Sept. 30", "12:30 PM"
    start(s);
    capture("46_strip_long_date");
  }

  // Settings rolls down from under the header like a scroll, the gear turning counterclockwise,
  // and back up, the gear turning clockwise (Mike, 2026-10-26).
  {
    start(p4);
    tap(760, 34, 0);  // the gear: the roll starts 100 ms after, and takes 520
    pump(260);
    capture("43_settings_unrolling");
    pump(800);
    capture("44_settings_open");
    tap(760, 34, 0);  // and up, in 420
    pump(100);
    capture("45_settings_rolling_up");
    pump(600);
    ui::AnimReport ar = ui::anim_report();
    if (ar.serial) std::printf("  %s: %d frames in %u ms (the simulator's clock: 20 ms steps)\n", ar.what, ar.frames, (unsigned)ar.ms);
  }

  // WeatherGate's Zambretti forecasts: its text wraps beside the icon (its longest, at a smaller
  // size, in four lines); at night the moon for the sun; danger cold hangs icicles over the top of
  // the screen; danger heat puts a bigger sun with pulsing rays in place of the icon. (Made-up
  // readings.)
  {
    Setup s = p4;
    s.out_t = 540, s.out_h = 88, s.out_code = Forecast::HeavyRain, s.phrase = "Rain at Times, Becoming Very Unsettled";
    start(s);
    capture("30_zambretti_heavy_rain");
  }
  {
    Setup s = p1;
    s.out_code = Forecast::Clearing, s.phrase = "Fairly Fine, Improving";
    start(s);
    capture("31_zambretti_clearing_night");
  }
  {
    Setup s = p3;
    s.now = at(2026, 12, 26, 9, 0, EST);  // the day after Christmas: no message on the strip
    s.out_t = 180, s.out_h = 70, s.out_code = Forecast::Snow, s.phrase = "Snow at Times, Worse Later";
    s.warn = kOutsideDangerCold;
    start(s);
    capture("32_danger_cold");
  }
  {
    Setup s = p5;
    s.out_t = 1010, s.out_h = 48, s.out_code = Forecast::Sun, s.phrase = "Settled Fine Weather";
    s.warn = kOutsideDangerHeat;
    start(s);
    capture("33_danger_heat");
  }

  // The calendar roll-down (boards V1 to V7) in the six lights, all on Monday, Oct. 12: the boards'
  // times and dogs, each view with its board's events, Thursday the 15th selected.
  {
    Setup cal[6] = {p1, p2, p3, p4, p5, p6};
    cal[1].now = at(2026, 10, 12, 6, 20, EDT);
    cal[2].now = at(2026, 10, 12, 9, 0, EDT), cal[2].off = EDT, cal[2].dog = Dog::None;
    cal[2].mine_since = cal[2].now - 900, cal[2].peer_since = cal[2].now - 7200;
    const char *names[6] = {"night", "dawn", "morning", "midday", "golden", "dusk"};
    const int32_t thu = (int32_t)days_from_civil(2026, 10, 15);
    char name[48];
    for (int i = 0; i < 6; i++) {
      start(cal[i]); load_events(kMonthFx); ui::show_calendar(ui::CalView::Month, thu); pump(300);
      std::snprintf(name, sizeof(name), "cal_month_%d_%s", i + 1, names[i]); capture(name);
      start(cal[i]); load_events(kWeekFx); ui::show_calendar(ui::CalView::Week, thu); pump(300);
      std::snprintf(name, sizeof(name), "cal_week_%d_%s", i + 1, names[i]); capture(name);
      start(cal[i]); load_events(kDayFx); ui::show_calendar(ui::CalView::Day, thu); pump(300);
      std::snprintf(name, sizeof(name), "cal_day_%d_%s", i + 1, names[i]); capture(name);
      start(cal[i]); load_events(kWeekFx); ui::show_calendar(ui::CalView::Edit, thu, event_named("Parcel pickup"));
      pump(300);
      std::snprintf(name, sizeof(name), "cal_edit_%d_%s", i + 1, names[i]); capture(name);
    }
    // The upstairs unit (V7): its add buttons and the editor's marker are the up caret.
    Setup up = p5;
    up.me = Floor::Upstairs;
    start(up); load_events(kMonthFx); ui::show_calendar(ui::CalView::Month, thu); pump(300); capture("cal_month_up_golden");
    start(up); load_events(kWeekFx); ui::show_calendar(ui::CalView::Edit, thu, event_named("Parcel pickup")); pump(300);
    capture("cal_edit_up_golden");
    // A new event takes the unit's floor with no prompt: the editor on each unit, then what a save keeps.
    for (int u = 0; u < 2; u++) {
      Setup s = p5;
      s.me = u ? Floor::Upstairs : Floor::Downstairs;
      start(s);
      ui::show_calendar(ui::CalView::Edit, thu);
      pump(300);
      capture(u ? "cal_new_up" : "cal_new_down");
      Event e;
      e.day = thu;
      std::snprintf(e.title, sizeof(e.title), "Test");
      uint32_t id = g_h->panel.save_event(e);
      const Event *got = g_h->panel.events().find(id);
      std::printf("  new event on the %s unit: %s\n", u ? "upstairs" : "downstairs",
                  got && got->floor == s.me ? "kept its floor" : "WRONG FLOOR");
    }
    ui::show_calendar(ui::CalView::Closed, 0);
  }

  // The calendar by touch: open, pick a day, add an event, a toast twice, delete and Undo, close
  // with the editor up, a change of light while it rolls, and the auto close.
  {
    std::printf("calendar by touch:\n");
    start(p5);
    load_events(kMonthFx);
    const EventStore &st = g_h->panel.events();
    tap(200, 34, 0);  // the date strip
    pump(160);
    capture("cal_t1_rolling");
    pump(400);
    capture("cal_t2_open");
    ui::AnimReport ar = ui::anim_report();
    if (ar.serial) std::printf("  %s: %d frames in %u ms (the simulator's clock: 20 ms steps)\n", ar.what, ar.frames, (unsigned)ar.ms);
    tap(22 + 2 * 108 + 60, 146 + 3 * 63 + 57);  // Tuesday the 20th, below its event: selected
    tap(22 + 2 * 108 + 14, 146 + 3 * 63 + 10);  // its numeral again: that day
    capture("cal_t3_day_20");
    tap(87, 400);  // ADD EVENT
    capture("cal_t4_new");
    tap(730, 90);  // Save with no title
    capture("cal_t5_no_title");
    pump(5500);    // the toast goes
    type_keys("Lunch with Al");
    capture("cal_t6_typed");
    tap(730, 90);  // Save
    uint32_t id = event_named("Lunch with Al");
    const Event *e = st.find(id);
    check(e && e->floor == Floor::Downstairs && e->day == (int32_t)days_from_civil(2026, 10, 20) && e->start_min == 9 * 60,
          "a new event: this unit's floor, the day picked, 9 AM");
    capture("cal_t7_saved");
    tap(400, 126 + 30 + 9 * 24 + 12 - 9 * 24 + 0);  // its block: 9 AM sits at the timeline's top
    capture("cal_t8_edit");
    tap(107, 258);  // Delete event
    check(e && e->deleted, "Delete event deletes it");
    capture("cal_t9_deleted");
    tap(470, 442);  // Undo, on the toast
    check(e && !e->deleted, "Undo brings it back");
    pump(5500);
    tap(87, 400);  // ADD EVENT again, then Save with no title: a second toast
    tap(730, 90);
    pump(300);
    check(true, "a second toast after the first has gone");
    tap(200, 34, 600);  // the strip, with the editor up: closes
    capture("cal_t10_closed");
    // A change of light while it rolls down.
    tap(200, 34, 0);
    pump(100);
    g_h->panel.force_phase(5);
    pump(800);
    capture("cal_t11_dusk_after_roll");
    tap(200, 34, 600);
    capture("cal_t12_closed_again");
    // The auto close: 90 s without a touch.
    tap(200, 34, 600);
    g_awake = false;
    pump(91000);
    g_awake = true;
    pump(600);
    capture("cal_t13_auto_closed");
    // A change of light with the editor up and a title half typed: it stays.
    tap(200, 34, 600);
    tap(22 + 4 * 108 + 60, 146 + 2 * 63 + 8);  // Thursday the 15th, by its numeral (its lanes are full)
    tap(22 + 4 * 108 + 108 - 21, 146 + 2 * 63 + 15);  // its add button
    type_keys("Vet");
    g_h->panel.force_phase(0);
    pump(600);
    capture("cal_t14_night_draft_kept");
    tap(730, 90);  // Save
    check(event_named("Vet") != 0, "a light change while typing keeps the draft");
    std::printf("calendar by touch: %d failed\n", g_fails);
  }

  // A second run by touch (Mike, 2026-10-25): the time picker, the repeat menu, and one week of a
  // series changed on its own.
  {
    std::printf("pickers by touch:\n");
    start(p5);
    const EventStore &st = g_h->panel.events();
    tap(200, 34, 600);                          // open
    tap(22 + 2 * 108 + 60, 146 + 3 * 63 + 57);  // Tuesday the 20th
    tap(22 + 2 * 108 + 14, 146 + 3 * 63 + 10);  // its day
    tap(87, 400);                               // ADD EVENT
    type_keys("Piano");
    tap(400, 219);  // TIME
    capture("cal_p1_time_picker");
    tap(212, 221);  // the hour, up: 10
    tap(710, 121);  // Done
    tap(678, 258);  // the repeat bubble
    tap(504, 219);  // Weekly
    capture("cal_p2_repeat_menu");
    tap(710, 121);  // Done
    capture("cal_p3_editor_weekly");
    tap(730, 90);   // Save
    uint32_t sid = event_named("Piano", true);
    const Event *ser = st.find(sid);
    check(ser && ser->repeat == Repeat::Weekly && ser->start_min == 10 * 60 && ser->day == (int32_t)days_from_civil(2026, 10, 20),
          "a weekly event at 10 AM from the pickers");
    for (int i = 0; i < 7; i++) tap(90, 100, 120);  // a week on
    capture("cal_p4_next_week");
    tap(400, 200);  // its 10 AM block
    tap(400, 219);  // TIME
    tap(212, 221);  // 11
    tap(710, 121);  // Done
    tap(730, 90);   // Save: asks
    capture("cal_p5_just_this_one");
    tap(283, 257);  // Just this one
    int32_t d27 = (int32_t)days_from_civil(2026, 10, 27);
    uint32_t one = event_named("Piano");
    check(ser->skips(d27) && one && st.find(one)->day == d27 && st.find(one)->start_min == 11 * 60 && !st.find(one)->repeats(),
          "just this one: the series skips the 27th, an 11 AM event of its own takes its place");
    capture("cal_p6_after");
    std::printf("pickers by touch: %d failed\n", g_fails);
  }

  // Repeating events, birthdays and holidays in the views; today is Sam's birthday.
  {
    Setup s = p5;
    s.now = at(2026, 11, 3, 18, 10, EDT);
    start(s);
    add_event("Piano", 2026, 10, 1, 16 * 60, 45, Repeat::Weekly);
    add_event("Sam", 1988, 11, 3, -1, 1, Repeat::Yearly, EventKind::Birthday);
    add_event("Mike & Jo", 2001, 11, 14, -1, 1, Repeat::Yearly, EventKind::Anniversary);
    add_event("Book club", 2026, 9, 17, 19 * 60, 90, Repeat::Monthly);
    add_event("Turkey dinner", 2026, 11, 26, 15 * 60, 180);
    add_event("Cabin", 2026, 11, 27, -1, 3);
    pump(400);
    const int32_t nov3 = (int32_t)days_from_civil(2026, 11, 3), nov26 = (int32_t)days_from_civil(2026, 11, 26);
    ui::show_calendar(ui::CalView::Month, nov3); pump(300); capture("cal_r1_month_repeats");
    ui::show_calendar(ui::CalView::Week, nov26); pump(300); capture("cal_r2_week_thanksgiving");
    ui::show_calendar(ui::CalView::Day, nov26); pump(300); capture("cal_r3_day_thanksgiving");
    ui::show_calendar(ui::CalView::Day, nov3); pump(300); capture("cal_r4_day_birthday");
    ui::show_calendar(ui::CalView::Edit, nov3, event_named("Sam", true)); pump(300); capture("cal_r5_edit_birthday");
    ui::show_calendar(ui::CalView::PickDate, nov3, event_named("Sam", true)); pump(300); capture("cal_r6_date_picker");
    ui::show_calendar(ui::CalView::PickLength, nov26, event_named("Turkey dinner")); pump(300); capture("cal_r7_length_picker");
    ui::show_calendar(ui::CalView::PickRepeat, nov3, event_named("Sam", true)); pump(300); capture("cal_r8_repeat_birthday");
    ui::show_calendar(ui::CalView::AskSeries, nov26, event_named("Piano", true)); pump(300); capture("cal_r9_ask_series");
    ui::show_calendar(ui::CalView::Closed, 0);
    pump(300);
    capture("cal_r10_strip_birthday");
  }

  // The date strip on each federal holiday and on a birthday and an anniversary (sheets/strips):
  // the dog in, then out (a narrower strip: the longer words step down or shorten).
  {
    struct D {
      int y, m, d;
      bool dog;
    } days[] = {{2027, 1, 1, false},  {2027, 1, 18, false}, {2027, 2, 15, false}, {2027, 5, 31, false},
                {2027, 6, 19, false}, {2027, 7, 4, false},  {2027, 9, 6, false},  {2027, 10, 11, false},
                {2027, 11, 11, false}, {2027, 11, 25, false}, {2026, 12, 25, false}, {2027, 11, 25, true},
                {2027, 4, 2, false},  {2027, 3, 9, false},  {2027, 4, 2, true},   {2026, 12, 25, true}};
    for (const D &d : days) {
      Setup s = p5;
      s.now = at(d.y, d.m, d.d, 18, 10, EDT);
      s.dog = d.dog ? Dog::Copper : Dog::None;
      start(s);
      if (d.m == 3) add_event("Sam", 1988, 3, 9, -1, 1, Repeat::Yearly, EventKind::Birthday);
      if (d.m == 4) add_event("Mike & Jo", 2001, 4, 2, -1, 1, Repeat::Yearly, EventKind::Anniversary);
      if (d.m == 12 && d.dog) add_event("Sam", 1988, 12, 25, -1, 1, Repeat::Yearly, EventKind::Birthday);
      pump(300);
      strip_row();
      if (d.m == 12 && d.dog) {  // two messages: they take turns every 8 s
        pump(8000);
        strip_row();
      }
    }
    std::string path = g_out + "/sheets/strips.rgb";
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f) {
      std::fwrite(g_strips.data(), 1, g_strips.size(), f);
      std::fclose(f);
      std::printf("strips %d\n", (int)(g_strips.size() / (800 * 3)));
    }
  }

  // The Zambretti demo (Settings): the list, and the outside tile showing the one chosen.
  {
    start(p4);
    const int picks[6] = {0, 7, 18, 21, 27, 28};
    const char *names[6] = {"zdemo_a_day", "zdemo_h_day", "zdemo_s_night", "zdemo_v_night", "zdemo_heat", "zdemo_cold"};
    for (int i = 0; i < 6; i++) {
      ui::zambretti_pick(picks[i], i == 2 || i == 3);
      pump(400);
      capture(names[i]);
    }
    ui::open_sheet(ui::Sheet::None);
    pump(200);
  }

  // The longest titles (a long day name, a week across the new year) and a six-week month, whose
  // days keep one lane: three events on one day show one and "+2".
  {
    start(p5);
    const int32_t jan15 = (int32_t)days_from_civil(2027, 1, 15);
    const char *names[3] = {"Dentist", "Book club", "Pick up Al"};
    for (int i = 0; i < 3; i++) {
      Event e;
      e.day = jan15;
      e.start_min = (int16_t)(9 * 60 + i * 120);
      std::snprintf(e.title, sizeof(e.title), "%s", names[i]);
      g_h->panel.save_event(e);
    }
    pump(300);
    ui::show_calendar(ui::CalView::Day, (int32_t)days_from_civil(2026, 9, 30)); pump(300); capture("cal_x_day_long_title");
    ui::show_calendar(ui::CalView::Week, (int32_t)days_from_civil(2026, 12, 31)); pump(300); capture("cal_x_week_new_year");
    ui::show_calendar(ui::CalView::Month, jan15); pump(300); capture("cal_x_month_six_weeks");
  }
  return 0;
}
