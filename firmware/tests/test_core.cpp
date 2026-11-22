// Host tests for the TComm core and controller. Build and run (Git Bash, from the firmware folder):
//   g++ -std=c++17 -O1 -Wall -Wextra -I tcomm/src tests/test_core.cpp
//       tcomm/src/core/*.cpp tcomm/src/app/*.cpp -o "$LOCALAPPDATA/tcomm/host/tests.exe"
//   (one command; split here for reading)
//   "$LOCALAPPDATA/tcomm/host/tests.exe" docs/sounds
// The optional argument is a folder for WAV exports of the four synthesized sounds.
#include "app/controller.h"
#include "app/demo.h"
#include "core/civil.h"
#include "core/comfort.h"
#include "core/requests.h"
#include "core/events.h"
#include "core/holidays.h"
#include "core/outside_log.h"
#include "app/mem_files.h"
#include "core/brightness.h"
#include "core/presence.h"
#include "core/settings_migrate.h"
#include "core/sht4x.h"
#include "core/sky.h"
#include "core/spare_loan.h"
#include "core/synth.h"
#include "core/zambretti.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace tc;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) g_pass++;                                                      \
    else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define CHECK_STR(a, b) CHECK(std::string(a) == std::string(b))

// ---------------------------------------------------------------------------------------------
// Fake platform

struct Sent { uint8_t to; std::vector<uint8_t> bytes; };

class FakePlatform : public Platform {
 public:
  uint32_t mono = 1000;
  WallTime w;
  int32_t offset = -4 * 3600;  // EDT
  std::vector<Sent> out;
  std::vector<std::pair<Sound, uint8_t>> played;
  uint8_t backlight = 0;
  int set_wall_calls = 0;
  uint32_t rnd = 12345;
  OwnState saved_own;
  NoteState saved_notes[2] = {};  // the last save_notes (Mike, 2026-11-07)
  int note_saves = 0;
  DogState saved_dog;
  MailState saved_mail;
  std::vector<std::string> lines;  // append_line's lines (comfort.csv)
  std::string line_header;
  std::vector<std::string> req_lines;  // append_line's lines for requests.csv
  std::vector<std::string> outside_lines, outside_names;  // the outside history's day files (oYYYYMMDD.csv)
  std::string outside_header;
  RequestState saved_req = {};
  int req_saves = 0;
  uint32_t saved_comfort = 0;
  std::vector<ComfortPending> saved_pending;  // the last save_comfort_pending (Mike, 2026-11-07)
  int pending_saves = 0;
  bool has_updater = true;  // updater_installed()
  int updater_starts = 0;   // start_updater() calls
  std::vector<std::string> csv_sets;  // set_csv_field calls as "name|n|column|value"
  MemFiles files;  // the calendar's files (the SD card)
  int event_saves() { return files.writes["calendar"]; }

  uint32_t mono_ms() override { return mono; }
  WallTime wall() override { return w; }
  void set_wall(uint32_t utc, uint8_t q) override { w.valid = true; w.utc = utc; w.quality = q; set_wall_calls++; }
  int32_t utc_offset(uint32_t) override { return offset; }
  void radio_send(uint8_t to, const uint8_t *d, size_t n) override { out.push_back({to, std::vector<uint8_t>(d, d + n)}); }
  void play(Sound s, uint8_t v) override { played.push_back({s, v}); }
  void set_backlight(uint8_t r) override { backlight = r; }
  void save_own(const OwnState &s) override { saved_own = s; }
  void save_notes(const NoteState *n) override { saved_notes[0] = n[0]; saved_notes[1] = n[1]; note_saves++; }
  void save_dog(const DogState &d) override { saved_dog = d; }
  void save_mail(const MailState &m) override { saved_mail = m; }
  void save_comfort_count(uint32_t n) override { saved_comfort = n; }
  void save_comfort_pending(const ComfortPending *p, int n) override {
    saved_pending.assign(p, p + n);
    pending_saves++;
  }
  bool updater_installed() override { return has_updater; }
  void start_updater() override { updater_starts++; }
  bool pres = false;  // the presence sensor's debounced state (presence())
  bool presence() override { return pres; }
  Settings saved_settings;  // the last save_settings
  int settings_saves = 0;
  void save_settings(const Settings &s) override { saved_settings = s; settings_saves++; }
  void save_requests(const RequestState &r) override { saved_req = r; req_saves++; }
  void append_line(const char *name, const char *header, const char *line) override {
    if (!strcmp(name, kRequestsFile)) {
      CHECK_STR(header, kRequestsCsvHeader);
      req_lines.push_back(line);
      return;
    }
    if (!strcmp(header, kOutsideCsvHeader)) {
      outside_header = header;
      outside_names.push_back(name);
      outside_lines.push_back(line);
      return;
    }
    if (lines.empty()) line_header = header;
    lines.push_back(line);
  }
  void set_csv_field(const char *name, uint32_t n, int column, const char *value) override {
    csv_sets.push_back(std::string(name) + "|" + std::to_string(n) + "|" + std::to_string(column) + "|" + value);
  }
  uint32_t random32() override { rnd = rnd * 1103515245u + 12345u; return rnd; }
  uint8_t *read_file(const char *name, size_t &len) override { return files.read(*this, name, len); }
  void write_file(const char *name, uint8_t *buf, size_t len) override { files.write(*this, name, buf, len); }
};

// Two panels with a radio between them that can be cut.
struct Pair {
  FakePlatform pa, pb;
  Controller a{pa}, b{pb};
  bool cut = false;           // both directions
  bool cut_ba = false;        // only frames from b to a
  std::function<bool(bool from_a, const Msg &)> drop;  // return true to lose that one frame
  Settings sa, sb;
  // What went to the gateway (house events, Mike 2026-11-11): every NOTICE frame upstairs sent, and a
  // count of frames the downstairs panel sent there. gw_acks: answer each one the way the gateway does.
  std::vector<Msg> gw;
  int gw_from_b = 0;
  bool gw_acks = false;
  // ROOMS (Mike, 2026-11-22): every ROOMS frame upstairs sent the gateway (tries included), ACKed as the
  // gateway does when gw_acks is set; and how many ROOMS the downstairs panel sent anywhere.
  std::vector<Msg> rooms;
  int rooms_from_b = 0;

  Pair(uint32_t utc = 1792627200u /* 2026-10-22 00:00 UTC */) {
    pa.w = {true, utc, kClockManual};
    pb.w = {true, utc, kClockManual};
    sa.floor = Floor::Upstairs;
    sb.floor = Floor::Downstairs;
    pb.rnd = 777;  // real panels draw ids from a hardware RNG; two equal seeds would collide
  }
  void begin() {
    a.begin(sa, OwnState(), DogState());
    b.begin(sb, OwnState(), DogState());
  }
  // Moves time forward in 20 ms steps, delivering radio frames and ticking both panels.
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
      pa.mono += 20; pb.mono += 20;
      if ((pa.mono / 20) % 50 == 0) { pa.w.utc += 1; pb.w.utc += 1; }
      a.tick(); b.tick();
      pump();
    }
  }
  void pump() {
    for (int round = 0; round < 4; round++) {
      std::vector<Sent> oa, ob;
      oa.swap(pa.out); ob.swap(pb.out);
      if (cut) continue;
      for (auto &s : oa) {
        Msg m;
        if (drop && decode(s.bytes.data(), s.bytes.size(), m) && drop(true, m)) continue;
        if (s.to == kSenderGateway) {
          if (decode(s.bytes.data(), s.bytes.size(), m) && m.type == MsgType::Rooms) {
            rooms.push_back(m);
            if (gw_acks) {
              Msg k;
              k.type = MsgType::Ack;
              k.sender = kSenderGateway;
              k.u.ack.seq = m.seq;
              k.u.ack.type = (uint8_t)MsgType::Rooms;
              uint8_t kb[16];
              a.on_radio(kb, encode(k, kb, sizeof(kb)));
            }
            continue;
          }
          if (decode(s.bytes.data(), s.bytes.size(), m) && m.type == MsgType::Notice) {
            gw.push_back(m);
            if (gw_acks) {
              Msg k;
              k.type = MsgType::Ack;
              k.sender = kSenderGateway;
              k.u.ack.seq = m.seq;
              k.u.ack.type = (uint8_t)MsgType::Notice;
              uint8_t kb[16];
              a.on_radio(kb, encode(k, kb, sizeof(kb)));
            }
          }
          continue;
        }
        if (s.to == kSenderDownstairs) b.on_radio(s.bytes.data(), s.bytes.size());
      }
      if (cut_ba) continue;
      for (auto &s : ob) {
        Msg m;
        if (drop && decode(s.bytes.data(), s.bytes.size(), m) && drop(false, m)) continue;
        if (decode(s.bytes.data(), s.bytes.size(), m) && m.type == MsgType::Rooms) rooms_from_b++;
        if (s.to == kSenderGateway) gw_from_b++;
        if (s.to == kSenderUpstairs) a.on_radio(s.bytes.data(), s.bytes.size());
      }
    }
  }
};

// ---------------------------------------------------------------------------------------------

static void test_civil() {
  int64_t s = seconds_from_civil(2026, 10, 12, 18, 10, 0);
  Civil c = civil_from_seconds(s);
  CHECK(c.year == 2026 && c.month == 10 && c.day == 12 && c.hour == 18 && c.minute == 10);
  CHECK(c.weekday == 1);  // Monday, as on board P5
  char buf[40];
  format_date_long(buf, sizeof(buf), s);
  CHECK_STR(buf, "Monday, Oct. 12");
  format_time12(buf, sizeof(buf), s);
  CHECK_STR(buf, "6:10 PM");
  format_time12(buf, sizeof(buf), seconds_from_civil(2026, 10, 12, 0, 5, 0));
  CHECK_STR(buf, "12:05 AM");
  format_time12(buf, sizeof(buf), seconds_from_civil(2026, 10, 12, 12, 0, 0));
  CHECK_STR(buf, "12:00 PM");
  format_date_long(buf, sizeof(buf), seconds_from_civil(2026, 12, 25, 9, 0, 0));
  CHECK_STR(buf, "Friday, Dec. 25");  // board P3
  CHECK(civil_from_seconds(seconds_from_civil(2028, 2, 29, 23, 59, 59)).day == 29);
  CHECK(civil_from_seconds(0).weekday == 4);
}

static void test_status() {
  StatusModel m;
  WallTime w{true, 100000, kClockManual};
  CHECK(!m.choose(Status::Open, w, 0));  // already Open, no timer
  CHECK(m.choose(Status::Busy, w, 0));
  CHECK(m.state().status == Status::Busy && m.state().since == 100000);

  // Timer: Private for an hour, then Busy. Survives as data; expiry sets since to the expiry time.
  CHECK(m.choose_timed(Status::Private, 3600, Status::Busy, w));
  CHECK(m.state().until == 103600);
  CHECK(!m.tick({true, 103599, kClockManual}));
  CHECK(m.tick({true, 103700, kClockManual}));
  CHECK(m.state().status == Status::Busy && m.state().since == 103600 && m.state().until == 0);

  // No timers without a clock; no timer whose fallback is itself.
  CHECK(!m.choose_timed(Status::Sleeping, 600, Status::Busy, WallTime()));
  CHECK(!m.choose_timed(Status::Sleeping, 600, Status::Sleeping, w));
  // Choosing the same status while a timer runs cancels the timer but keeps "since".
  CHECK(m.choose_timed(Status::Sleeping, 600, Status::Open, w));
  uint32_t since = m.state().since;
  CHECK(m.choose(Status::Sleeping, {true, 100300, kClockManual}, 21000));
  CHECK(m.state().until == 0 && m.state().since == since);

  // Unknown clock: since stays 0 until the clock arrives.
  StatusModel n;
  CHECK(n.choose(Status::Away, WallTime(), 1000));
  CHECK(n.state().since == 0);
  CHECK(n.clock_became_valid(500000, 61000));  // the clock arrives a minute after the choice
  CHECK(n.state().since == 500000 - 60);       // back-dated by that minute
  CHECK(!n.clock_became_valid(600000, 70000));
}

static void test_dog() {
  DogModel up, down;
  WallTime w{true, 1000, kClockManual};
  CHECK(up.tap(Floor::Upstairs, w));
  CHECK(up.state().out() == Dog::Lucy);
  CHECK(down.apply_remote(up.state()));
  // Both out (Mike, 2026-10-25): Copper's button works while Lucy is out.
  CHECK(down.tap(Floor::Downstairs, {true, 1001, kClockManual}));
  CHECK(down.state().out() == Dog::Both);
  CHECK(up.apply_remote(down.state()) && up.state().out() == Dog::Both);
  // Lucy back in leaves Copper out.
  CHECK(up.tap(Floor::Upstairs, {true, 1010, kClockManual}));
  CHECK(up.state().out() == Dog::Copper);
  CHECK(down.apply_remote(up.state()) && down.state().out() == Dog::Copper);
  // An older change loses.
  DogState old = up.state();
  old.dog[0] = DogOne{true, 900, 1};
  CHECK(!down.apply_remote(old) && down.state().out() == Dog::Copper);
  // The two panels change different dogs at once: neither change is lost.
  DogModel x, y;
  x.tap(Floor::Upstairs, {true, 2000, kClockManual});
  y.tap(Floor::Downstairs, {true, 2000, kClockManual});
  DogState xs = x.state(), ys = y.state();
  x.apply_remote(ys);
  y.apply_remote(xs);
  CHECK(x.state().out() == Dog::Both && y.state().out() == Dog::Both);
  // The same dog changed both ways at once, same second: both panels end the same.
  DogModel m1, m2;
  DogState a, c;
  a.dog[0] = DogOne{true, 3000, 5};
  c.dog[0] = DogOne{false, 3000, 5};
  m1.restore(a);
  m2.restore(c);
  m1.apply_remote(c);
  m2.apply_remote(a);
  CHECK(m1.state().out() == m2.state().out());
  // LucyGate: "Lucy in" leaves Copper alone, "Lucy out" joins him, a repeat changes nothing.
  DogModel g;
  DogState cop;
  cop.dog[1] = DogOne{true, 3000, 1};
  g.restore(cop);
  CHECK(!g.apply_lucy(false, 3100));
  CHECK(g.state().out() == Dog::Copper);
  CHECK(g.apply_lucy(true, 3200));
  CHECK(g.state().out() == Dog::Both);
  CHECK(!g.apply_lucy(true, 3300));
  CHECK(g.apply_lucy(false, 3400) && g.state().out() == Dog::Copper);
}

static void test_protocol() {
  uint8_t buf[kMaxMsg];
  Msg m, d;
  m.type = MsgType::State;
  m.sender = kSenderUpstairs;
  m.seq = 513;
  m.u.state = {3, kStateClockValid, 111, 222, 1, 333, kClockManual};
  size_t n = encode(m, buf, sizeof(buf));
  CHECK(n == 21);
  CHECK(decode(buf, n, d));
  CHECK(d.type == MsgType::State && d.seq == 513 && d.u.state.status == 3 && d.u.state.until == 222 && d.u.state.clock == 333);
  CHECK(!decode(buf, n - 1, d));  // truncated
  buf[0] = 2;
  CHECK(!decode(buf, n, d));      // wrong version
  buf[0] = kProtoVersion;
  buf[5] = 9;
  CHECK(!decode(buf, n, d));      // status out of range

  Msg o;
  o.type = MsgType::Outside;
  o.u.outside.flags = kOutsideValid;
  o.u.outside.temp_tenths_f = -125;
  o.u.outside.humidity = 71;
  o.u.outside.forecast = (uint8_t)Forecast::Snow;
  o.u.outside.time = 999;
  std::strcpy(o.u.outside.phrase, "Snow likely");
  n = encode(o, buf, sizeof(buf));
  CHECK(n > 0 && n < kMaxMsg);
  CHECK(decode(buf, n, d));
  CHECK(d.u.outside.temp_tenths_f == -125 && std::string(d.u.outside.phrase) == "Snow likely");
  buf[n - 1] = 0x07;  // control character in the phrase is replaced, not trusted
  CHECK(decode(buf, n, d) && d.u.outside.phrase[d.u.outside.phrase_len - 1] == '?');
  // WeatherGate's warnings come through; a forecast code from a newer gateway keeps the reading.
  o.u.outside.flags = kOutsideValid | kOutsideDangerHeat | kOutsideDangerCold;
  o.u.outside.forecast = 42;
  n = encode(o, buf, sizeof(buf));
  CHECK(decode(buf, n, d) && d.u.outside.flags == (kOutsideValid | kOutsideDangerHeat | kOutsideDangerCold));
  CHECK(d.u.outside.forecast == 42 && d.u.outside.temp_tenths_f == -125);

  // Every message type round-trips and stays under the ESP-NOW limit.
  for (int t = 1; t <= 8; t++) {
    Msg x;
    x.type = (MsgType)t;
    if (x.type == MsgType::Knock) x.u.knock.id = 7;
    if (x.type == MsgType::Reply) x.u.reply.id = 7;
    n = encode(x, buf, sizeof(buf));
    CHECK(n > 0 && n <= kMaxMsg);
    CHECK(decode(buf, n, d) && d.type == x.type);
  }
  // Valid headers with random payloads of plausible lengths: whatever decodes stays in range.
  uint32_t r = 1;
  int accepted = 0, bad = 0;
  const size_t kLens[9] = {0, 21, 7, 8, 8, 14, 10, 10, 18};
  for (int i = 0; i < 40000; i++) {
    r = r * 1664525u + 1013904223u;
    int type = 1 + (int)((r >> 8) % 8);
    size_t len = kLens[type] + ((r >> 20) % 3 == 0 ? (r >> 24) % 40 : 0);
    if (len > kMaxMsg) len = kMaxMsg;
    for (size_t k = 0; k < len; k++) { r = r * 1664525u + 1013904223u; buf[k] = (uint8_t)(r >> 24); }
    buf[0] = kProtoVersion;
    buf[1] = (uint8_t)type;
    buf[2] = (uint8_t)(r % 3);
    if (!decode(buf, len, d)) continue;
    accepted++;
    if (d.type == MsgType::State && (!status_valid(d.u.state.status) || !status_valid(d.u.state.fallback))) bad++;
    if (d.type == MsgType::DogMsg && (d.u.dog.out[0] > 1 || d.u.dog.out[1] > 1)) bad++;
    if (d.type == MsgType::Outside && (d.u.outside.phrase_len > kMaxPhrase || d.u.outside.phrase[d.u.outside.phrase_len] != 0)) bad++;
  }
  CHECK(accepted > 1000);  // the loop really exercises decode
  CHECK(bad == 0);

  Dedup dd;
  CHECK(!dd.seen(0, 5, 0));
  CHECK(dd.seen(0, 5, 100));
  CHECK(!dd.seen(1, 5, 100));        // other sender
  CHECK(!dd.seen(0, 5, 10100));      // expired: a rebooted sender may reuse numbers
  CHECK(!dd.seen(0, 6, 0xFFFFFF00u));
  CHECK(dd.seen(0, 6, 0x00000100u)); // still remembered across the millisecond wrap

  RetryQueue q;
  q.set_seq(65535);
  Msg k;
  k.type = MsgType::Knock;
  k.u.knock.id = 42;
  uint16_t s1 = q.push(k, 0, 0xFFFFFF00u);  // starts just before the wrap
  CHECK(s1 == 65535);
  uint32_t now = 0xFFFFFF00u;
  int sends = 0;
  RetryQueue::Result res;
  bool failed = false;
  for (int i = 0; i < 200 && !failed; i++, now += 10) {
    while (const RetryQueue::Frame *f = q.due(now)) { sends++; q.sent(f->seq, now); }
    if (q.expired(now, res)) failed = true;
  }
  CHECK(sends == RetryQueue::kTries);
  CHECK(failed && res.failed && res.knock_id == 42 && res.type == MsgType::Knock);
  CHECK(q.pending() == 0);
  // Next seq skipped 0.
  uint16_t s2 = q.push(k, 0, 0);
  CHECK(s2 == 1);
  CHECK(q.ack(s2, res) && !res.failed);
  // Latest STATE replaces an older pending one.
  Msg st;
  st.type = MsgType::State;
  q.push(st, 0, 0);
  q.push(st, 0, 0);
  CHECK(q.pending() == 1);

  // A caller that stalls (the panel's first frame took 2.4 s on the board) sends one try when it
  // wakes, then spaces the rest out, and gives up 1.5 s after that, not at once.
  RetryQueue q2;
  q2.push(st, 0, 1000);
  now = 1000 + 2400;
  sends = 0;
  while (const RetryQueue::Frame *f = q2.due(now)) { sends++; q2.sent(f->seq, now); }
  CHECK(sends == 1);
  CHECK(!q2.expired(now, res));
  CHECK(q2.due(now + 149) == nullptr && q2.due(now + 150) != nullptr);
  failed = false;
  uint32_t gave_up = 0;
  for (uint32_t t = now; t < now + 3000 && !failed; t += 10) {
    while (const RetryQueue::Frame *f = q2.due(t)) { sends++; q2.sent(f->seq, t); }
    if (q2.expired(t, res)) { failed = true; gave_up = t - now; }
  }
  CHECK(sends == RetryQueue::kTries);
  CHECK(failed && gave_up == 1500);
}

static void test_sky() {
  // Washington, DC (38.9 N, 77.0 W, the settings' placeholder), 2026-10-21, EDT: sunrise is about
  // 7:20 AM, sunset about 6:15 PM; allow 10 minutes.
  SunTimes t = sun_times(2026, 10, 21, -4 * 3600, 38.9f, -77.0f);
  CHECK(t.ok);
  int rise = minutes_of_day(t.rise - 4 * 3600), set = minutes_of_day(t.set - 4 * 3600);
  CHECK(std::abs(rise - (7 * 60 + 20)) <= 10);
  CHECK(std::abs(set - (18 * 60 + 15)) <= 10);
  int64_t day = seconds_from_civil(2026, 10, 21, 0, 0, 0) + 4 * 3600;  // local midnight in UTC
  CHECK(sky_at(day + 3 * 3600, -4 * 3600, 38.9f, -77.0f).phase == Phase::Night);
  CHECK(sky_at(day + 3 * 3600, -4 * 3600, 38.9f, -77.0f).moon);
  CHECK(sky_at(day + 7 * 3600, -4 * 3600, 38.9f, -77.0f).phase == Phase::Dawn);
  CHECK(sky_at(day + 9 * 3600, -4 * 3600, 38.9f, -77.0f).phase == Phase::Morning);
  CHECK(sky_at(day + 13 * 3600, -4 * 3600, 38.9f, -77.0f).phase == Phase::Midday);
  // Golden starts when the sun is 6 degrees up (Mike, 2026-11-07), not a fixed time before sunset.
  int64_t gold = golden_start(day + 12 * 3600, -4 * 3600, 38.9f, -77.0f);
  CHECK(sky_at(gold - 60, -4 * 3600, 38.9f, -77.0f).phase == Phase::Midday);
  CHECK(sky_at(gold, -4 * 3600, 38.9f, -77.0f).phase == Phase::Golden);
  CHECK(sky_at(t.set - 15 * 60, -4 * 3600, 38.9f, -77.0f).phase == Phase::Golden);
  CHECK(sky_at(day + 18 * 3600 + 30 * 60, -4 * 3600, 38.9f, -77.0f).phase == Phase::Dusk);
  CHECK(sky_at(day + 21 * 3600, -4 * 3600, 38.9f, -77.0f).phase == Phase::Night);
  Sky noon = sky_at(day + 12 * 3600 + 45 * 60, -4 * 3600, 38.9f, -77.0f);
  CHECK(noon.p > 0.4f && noon.p < 0.6f && !noon.moon);
  // The screensaver's Lookout window: 30 minutes before that sunset to 15 minutes after.
  CHECK(sunset_today(day + 12 * 3600, -4 * 3600, 38.9f, -77.0f) == t.set);
  CHECK(!in_lookout_window(t.set - 31 * 60, -4 * 3600, 38.9f, -77.0f));
  CHECK(in_lookout_window(t.set - 30 * 60, -4 * 3600, 38.9f, -77.0f));
  CHECK(in_lookout_window(t.set, -4 * 3600, 38.9f, -77.0f));
  CHECK(in_lookout_window(t.set + 14 * 60 + 59, -4 * 3600, 38.9f, -77.0f));
  CHECK(!in_lookout_window(t.set + 15 * 60, -4 * 3600, 38.9f, -77.0f));
  CHECK(!in_lookout_window(day + 12 * 3600, -4 * 3600, 38.9f, -77.0f));
  CHECK(!in_lookout_window(day + 3 * 3600, -4 * 3600, 38.9f, -77.0f));
  // Far north in midsummer: no sunset. Falls back instead of failing.
  CHECK(!sun_times(2026, 6, 21, 0, 78.0f, 15.0f).ok);
  CHECK(sky_at(seconds_from_civil(2026, 6, 21, 12, 0, 0), 0, 78.0f, 15.0f).phase != Phase::Night);
  CHECK(!in_lookout_window(seconds_from_civil(2026, 6, 21, 18, 0, 0), 0, 78.0f, 15.0f));  // no sunset, no Lookout
}

// Independent of sky.cpp: the sun's elevation (degrees) from the usual low-precision almanac series
// (mean longitude and anomaly, ecliptic longitude, then declination, right ascension and sidereal time).
static double sun_elevation_deg(int64_t utc, double lat, double lon) {
  const double r = 3.14159265358979323846 / 180.0;
  double n = (double)utc / 86400.0 + 2440587.5 - 2451545.0;
  double L = std::fmod(280.460 + 0.9856474 * n, 360.0);
  double g = std::fmod(357.528 + 0.9856003 * n, 360.0) * r;
  double lam = (L + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g)) * r;
  double eps = (23.439 - 0.0000004 * n) * r;
  double ra = std::atan2(std::cos(eps) * std::sin(lam), std::cos(lam));
  double dec = std::asin(std::sin(eps) * std::sin(lam));
  double gmst = std::fmod(280.46061837 + 360.98564736629 * n, 360.0) * r;
  double ha = gmst + lon * r - ra;
  double s = std::sin(lat * r) * std::sin(dec) + std::cos(lat * r) * std::cos(dec) * std::cos(ha);
  return std::asin(s) / r;
}

// The screensaver's real sky (Mike, 2026-11-11): where the sun and the moon are, and the moon's phase.
static void test_sun_and_moon_position() {
  const float lat = 38.9f, lon = -77.0f;  // the settings' placeholder location
  auto apart = [](float a, float b) {     // two directions round the compass, degrees apart
    float d = std::fabs(a - b);
    return d > 180.f ? 360.f - d : d;
  };
  const int dates[3][4] = {{2026, 12, 21, -5 * 3600}, {2026, 6, 21, -4 * 3600}, {2026, 3, 20, -4 * 3600}};
  for (auto &dt : dates) {
    const int32_t off = dt[3];
    SunTimes st = sun_times(dt[0], dt[1], dt[2], off, lat, lon);
    CHECK(st.ok);
    // At sunrise and sunset the sun is on the horizon (the two sets of formulas agree within half
    // a degree), in the east and then in the west; it never jumps: a minute moves it a degree at most.
    SkyPos r = sun_position(st.rise, lat, lon), s = sun_position(st.set, lat, lon);
    CHECK(std::fabs(r.alt + 0.833f) < 0.5f && std::fabs(s.alt + 0.833f) < 0.5f);
    CHECK(r.az > 50.f && r.az < 130.f && s.az > 230.f && s.az < 310.f);
    float top = -90.f, top_az = 0.f;
    SkyPos prev = r;
    for (int64_t t = st.rise; t <= st.set; t += 60) {
      SkyPos p = sun_position(t, lat, lon);
      CHECK(std::fabs(p.alt - (float)sun_elevation_deg(t, lat, lon)) < 0.1f);  // the independent series above
      CHECK(apart(p.az, prev.az) < 1.2f && std::fabs(p.alt - prev.alt) < 0.3f);
      CHECK(p.az >= prev.az - 0.01f);  // always on round to the west
      if (p.alt > top) {
        top = p.alt;
        top_az = p.az;
      }
      prev = p;
    }
    // Highest in the south, at the height the season gives (90 - latitude + the sun's declination).
    CHECK(std::fabs(top_az - 180.f) < 2.f);
    const float want = dt[1] == 12 ? 27.7f : dt[1] == 6 ? 74.5f : 51.1f;
    CHECK(std::fabs(top - want) < 1.0f);
    // The dark half: below the horizon an hour after sunset and at local midnight.
    CHECK(sun_position(st.set + 3600, lat, lon).alt < -8.f);
    CHECK(sun_position(st.set + 6 * 3600, lat, lon).alt < -15.f);
  }

  // The moon's phase on known dates (UTC): new 2024-04-08 18:21 (the solar eclipse), full
  // 2024-04-23 23:49, first quarter 2024-04-15 19:13, last quarter 2024-05-01 11:27, new 2000-01-06
  // 18:14, full 2025-03-14 06:55 (a lunar eclipse). Within a hundredth of a month (about 7 hours).
  auto phase_at = [&](int y, int mo, int d, int h, int mi) {
    return moon_sky(seconds_from_civil(y, mo, d, h, mi, 0), lat, lon);
  };
  auto near_phase = [](float p, float want) {
    float d = std::fabs(p - want);
    if (d > 0.5f) d = 1.f - d;
    return d < 0.01f;
  };
  CHECK(near_phase(phase_at(2024, 4, 8, 18, 21).phase, 0.f) && phase_at(2024, 4, 8, 18, 21).lit < 0.01f);
  CHECK(near_phase(phase_at(2024, 4, 23, 23, 49).phase, 0.5f) && phase_at(2024, 4, 23, 23, 49).lit > 0.99f);
  CHECK(near_phase(phase_at(2024, 4, 15, 19, 13).phase, 0.25f));
  CHECK(std::fabs(phase_at(2024, 4, 15, 19, 13).lit - 0.5f) < 0.04f);
  CHECK(near_phase(phase_at(2024, 5, 1, 11, 27).phase, 0.75f));
  CHECK(near_phase(phase_at(2000, 1, 6, 18, 14).phase, 0.f));
  CHECK(near_phase(phase_at(2025, 3, 14, 6, 55).phase, 0.5f));
  // The phase runs forward all month, about 1/29.5 a day.
  {
    int64_t t0 = seconds_from_civil(2026, 11, 1, 0, 0, 0);
    float prev = moon_sky(t0, lat, lon).phase;
    for (int d = 1; d <= 60; ++d) {
      float p = moon_sky(t0 + (int64_t)d * 86400, lat, lon).phase;
      float step = p - prev;
      if (step < 0) step += 1.f;
      CHECK(step > 0.028f && step < 0.040f);
      prev = p;
    }
  }

  // The moon's place, checked against the sun's with no table: at a solar eclipse it covers the sun
  // (2024-04-08, greatest at 18:17 UTC near 25.3 N 104.1 W, both high in the sky), and at a lunar
  // eclipse it is straight across the sky from the sun (2025-03-14 06:59 UTC, seen from the placeholder).
  {
    int64_t t = seconds_from_civil(2024, 4, 8, 18, 17, 0);
    SkyPos s = sun_position(t, 25.3f, -104.1f);
    MoonSky m = moon_sky(t, 25.3f, -104.1f);
    CHECK(s.alt > 60.f && std::fabs(m.pos.alt - s.alt) < 1.5f);
    CHECK(apart(m.pos.az, s.az) * std::cos(s.alt * 3.14159265f / 180.f) < 1.5f);
    t = seconds_from_civil(2025, 3, 14, 6, 59, 0);
    s = sun_position(t, lat, lon);
    m = moon_sky(t, lat, lon);
    CHECK(m.pos.alt > 20.f && std::fabs(m.pos.alt + s.alt) < 2.0f);
    CHECK(std::fabs(apart(m.pos.az, s.az) - 180.f) < 2.5f);
  }
  // A full moon rises about when the sun sets and is up all night; a new moon keeps the sun's
  // hours; a first-quarter moon is high in the south at sunset. Believable, to a couple of hours.
  {
    SunTimes st = sun_times(2024, 4, 23, -4 * 3600, lat, lon);  // full that night
    CHECK(std::fabs(moon_sky(st.set, lat, lon).pos.alt) < 12.f);
    CHECK(moon_sky(st.set, lat, lon).pos.az < 180.f);           // coming up in the east
    CHECK(moon_sky(st.set + 5 * 3600, lat, lon).pos.alt > 15.f);
    st = sun_times(2024, 4, 8, -4 * 3600, lat, lon);            // new that day
    CHECK(moon_sky(st.set + 2 * 3600, lat, lon).pos.alt < 0.f);
    CHECK(moon_sky(st.set + 6 * 3600, lat, lon).pos.alt < 0.f);
    st = sun_times(2024, 4, 15, -4 * 3600, lat, lon);           // first quarter
    MoonSky q = moon_sky(st.set, lat, lon);
    CHECK(q.pos.alt > 40.f && q.pos.az > 130.f && q.pos.az < 250.f);
  }
}

// Mike, 2026-11-07: golden hour starts when the sun is 6 degrees above the horizon in the afternoon.
static void test_golden_hour() {
  const float lat = 38.9f, lon = -77.0f;  // the settings' placeholder location
  // Each date with the placeholder's own zone (the same one the other sky test uses).
  const int dates[3][4] = {{2026, 12, 21, -5 * 3600}, {2026, 6, 21, -4 * 3600}, {2026, 3, 20, -4 * 3600}};
  for (auto &dt : dates) {
    const int32_t off = dt[3];
    int64_t midnight = seconds_from_civil(dt[0], dt[1], dt[2], 0, 0, 0) - off;
    int64_t noon = midnight + 12 * 3600;
    int64_t g = golden_start(noon, off, lat, lon);
    SunTimes st = sun_times(dt[0], dt[1], dt[2], off, lat, lon);
    SunTimes z = sun_times_at_zenith(dt[0], dt[1], dt[2], off, lat, lon, 84.0);
    CHECK(st.ok && z.ok && z.set == g && z.rise < z.set);
    // The sun is 6 degrees up at that time (the two sets of formulas differ in small terms: within
    // half a degree) and going down, and it is later than the 11:00 floor but before dusk.
    CHECK(std::fabs(sun_elevation_deg(g, lat, lon) - 6.0) < 0.5);
    CHECK(sun_elevation_deg(g - 600, lat, lon) > sun_elevation_deg(g, lat, lon));
    CHECK(g >= midnight + 11 * 3600 && g < st.set - 10 * 60);
    CHECK(st.set - g > 20 * 60 && st.set - g < 120 * 60);
    // The phases around it: midday just before, golden from the second, dusk where it always began.
    CHECK(sky_at(g - 60, off, lat, lon).phase == Phase::Midday);
    CHECK(sky_at(g, off, lat, lon).phase == Phase::Golden);
    CHECK(sky_at(st.set - 10 * 60 - 1, off, lat, lon).phase == Phase::Golden);
    CHECK(sky_at(st.set - 10 * 60, off, lat, lon).phase == Phase::Dusk);
    CHECK(sky_at(st.set + 60 * 60 - 1, off, lat, lon).phase == Phase::Dusk);
    CHECK(sky_at(st.set + 60 * 60, off, lat, lon).phase == Phase::Night);
    // 11:00 local still starts midday; just before it is morning.
    CHECK(sky_at(midnight + 11 * 3600 - 1, off, lat, lon).phase == Phase::Morning);
    CHECK(sky_at(midnight + 11 * 3600, off, lat, lon).phase == Phase::Midday);
    // The old start (sunset - 120 min) no longer begins it: still midday then.
    CHECK(sky_at(st.set - 120 * 60, off, lat, lon).phase == Phase::Midday);
  }
  // The same time worked out with other zone offsets: the helper depends on the date and place, and
  // the clock offset only picks which calendar day it is (offsets 0 and +1 hour).
  for (int32_t off : {0, 3600}) {
    int64_t noon = seconds_from_civil(2026, 12, 21, 12, 0, 0) - off;
    int64_t g = golden_start(noon, off, lat, lon);
    CHECK(std::fabs(sun_elevation_deg(g, lat, lon) - 6.0) < 0.5);
    CHECK(sun_elevation_deg(g - 600, lat, lon) > sun_elevation_deg(g, lat, lon));
  }
  // The sun never gets 6 degrees up (polar night): golden starts at 11:00 local, and the day has no
  // real sunset, so the fallback 6 PM sunset still gives it a dusk.
  {
    const float plat = 89.0f, plon = 0.0f;
    int64_t midnight = seconds_from_civil(2026, 12, 21, 0, 0, 0);
    CHECK(!sun_times_at_zenith(2026, 12, 21, 0, plat, plon, 84.0).ok);
    CHECK(golden_start(midnight + 12 * 3600, 0, plat, plon) == midnight + 11 * 3600);
    CHECK(sky_at(midnight + 11 * 3600 - 1, 0, plat, plon).phase != Phase::Golden);
    CHECK(sky_at(midnight + 11 * 3600, 0, plat, plon).phase == Phase::Golden);
  }
}

static void write_wav(const std::string &path, const int16_t *s, size_t n) {
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t data = (uint32_t)(n * 2), riff = 36 + data, rate = kSynthRate, byterate = rate * 2, fmt = 16;
  uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
  std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
  std::fwrite(&fmt, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&rate, 4, 1, f);
  std::fwrite(&byterate, 4, 1, f); std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f);
  std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f); std::fwrite(s, 2, n, f);
  std::fclose(f);
}

// Renders one sound and checks its level and its ending; writes it as a WAV when path is given.
static void check_sound(Sound snd, uint8_t ringtone, const std::string &path) {
  CHECK(synth_voices(snd, ringtone) <= kMaxVoices);  // none dropped
  size_t n = synth_frames(snd, ringtone);
  CHECK(n > 0 && n <= (size_t)(10 * kSynthRate));
  std::vector<int16_t> s(n);
  CHECK(synth(snd, ringtone, s.data(), n));
  int peak = 0;
  for (int16_t v : s) peak = std::max(peak, std::abs((int)v));
  CHECK(peak > 21000 && peak <= 21900);  // -3.5 dBFS is 21889
  CHECK(std::abs(s.back()) < 50);        // fades out, no click at the end
  if (!path.empty()) write_wav(path, s.data(), n);
}

static void test_synth(const char *wav_dir) {
  const char *names[] = {"", "soft-chime", "reply-come-on", "reply-not-now"};
  std::string dir = wav_dir ? wav_dir : "";
  for (int i = 1; i < 4; i++) check_sound((Sound)i, 0, wav_dir ? dir + "/" + names[i] + ".wav" : "");

  // The ringtones: names and ids are unique, the groups hold 5 and 6, and each is a proper sound.
  int in_group[2] = {};
  size_t longest = 0;
  for (int i = 0; i < kRingtoneCount; i++) {
    uint8_t id = ringtone_id(i);
    CHECK(ringtone_index(id) == i);
    std::string name = ringtone_name(id);
    CHECK(!name.empty());
    for (int q = 0; q < i; q++) CHECK(name != ringtone_name(ringtone_id(q)) && id != ringtone_id(q));
    in_group[(int)ringtone_group(id)]++;
    longest = std::max(longest, synth_frames(Sound::Chime, id));
    std::string file;
    if (wav_dir) {
      char num[8];
      snprintf(num, sizeof(num), "%02d-", i);
      file = dir + "/ringtones/" + num;
      for (char c : name) file += c == ' ' ? '-' : (char)std::tolower((unsigned char)c);
      file += ".wav";
    }
    check_sound(Sound::Chime, id, file);
  }
  CHECK(in_group[0] == 5 && in_group[1] == 6);
  CHECK(longest == synth_max_chime_frames());
  // Saved settings hold ids. Those of removed ringtones (and anything unknown) mean Westminster.
  CHECK_STR(ringtone_name(0), "Westminster");
  CHECK_STR(ringtone_name(1), "Ding-dong");
  for (uint8_t gone : {4, 6, 9, 14, 15, 16, 200}) CHECK(ringtone_index(gone) < 0 && ringtone_from_saved(3, gone) == 0);
  CHECK_STR(ringtone_name(ringtone_from_saved(3, 12)), "Woodpecker");
  CHECK_STR(ringtone_name(ringtone_from_saved(3, 13)), "Frogs");
  // Versions 1 and 2 numbered them differently.
  CHECK(ringtone_from_saved(1, 0) == 0 && ringtone_from_saved(1, 7) == 0);  // sci-fi
  CHECK_STR(ringtone_name(ringtone_from_saved(1, 12)), "Chickadee");
  CHECK_STR(ringtone_name(ringtone_from_saved(1, 15)), "Wind chimes");
  CHECK(ringtone_from_saved(1, 14) == 0 && ringtone_from_saved(1, 16) == 0);  // Robin, Water drops
  CHECK(ringtone_from_saved(2, 11) == 0 && ringtone_from_saved(2, 15) == 0);  // Water drops, Crickets
  CHECK_STR(ringtone_name(ringtone_from_saved(2, 12)), "Nightingale");
  CHECK(ringtone_from_saved(2, 16) == 0);  // Loon
  CHECK_STR(ringtone_name(200), "Westminster");  // out of range falls back to the first
}

// ---------------------------------------------------------------------------------------------
// Controller, end to end over the fake radio

static void test_boot_and_status() {
  Pair p;
  p.begin();
  p.run(200);
  CHECK(!p.a.view().floor[(int)Floor::Downstairs].no_signal);
  CHECK(!p.b.view().floor[(int)Floor::Upstairs].no_signal);
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].word, "OPEN");

  CHECK(p.a.choose_status(Status::Private));
  p.run(100);
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].word, "PRIVATE");
  CHECK(p.a.choose_status(Status::Open));  // changing back is just another choice
  p.run(100);
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].word, "OPEN");

  // Timer: Sleeping for an hour, then Busy (the handoff's starting fallback).
  CHECK(p.a.start_timer(Status::Sleeping, 3600, Status::Busy));
  p.run(100);
  CHECK(p.b.view().floor[(int)Floor::Upstairs].timer);
  CHECK(std::string(p.b.view().floor[(int)Floor::Upstairs].line).find("until ") == 0);
  p.pa.w.utc += 3600; p.pb.w.utc += 3600;
  p.run(100);
  CHECK(p.a.own().status == Status::Busy);
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].word, "BUSY");
  CHECK(std::string(p.a.view().toast).find("Timer ended") == 0);
  CHECK(p.pa.saved_own.status == Status::Busy);  // persisted for a reboot
}

static void test_doorbell() {
  Pair p;
  p.begin();
  p.run(200);
  p.a.tap_bell();
  p.a.tick();
  CHECK(p.a.view().bell.out == BellOut::Ringing);
  p.run(60);
  CHECK(p.a.view().bell.out == BellOut::Delivered);
  CHECK(p.b.view().banner.kind == BannerKind::Knock);
  CHECK_STR(p.b.view().banner.title, "Doorbell from upstairs");
  CHECK(!p.pb.played.empty() && p.pb.played.back().first == Sound::Chime);
  CHECK(p.pb.played.back().second == 90);  // Mike's daytime volume (the test clock reads 8 PM EDT)
  size_t knocks_before = p.pb.played.size();
  p.a.tap_bell();  // within 10 s: ignored
  p.run(100);
  CHECK(p.pb.played.size() == knocks_before);
  p.b.answer_banner(true);
  p.run(100);
  CHECK(p.a.view().bell.out == BellOut::ComeOn);
  CHECK_STR(p.a.view().bell.line, "Come on");
  CHECK(!p.pa.played.empty() && p.pa.played.back().first == Sound::ReplyYes);
  CHECK(p.b.view().banner.kind == BannerKind::None);

  // Nobody answers: "No answer yet" after 2 minutes (Mike, 2026-11-15: was 60 s), and the receiver's
  // banner times out into a missed note at the same time; not before.
  p.run(11000);
  p.a.tap_bell();
  p.run(110000);
  CHECK(p.a.view().bell.out == BellOut::Delivered);
  CHECK(p.b.view().banner.kind == BannerKind::Knock);
  p.run(11000);
  CHECK(p.a.view().bell.out == BellOut::NoAnswer);
  CHECK(p.b.view().banner.kind == BannerKind::None);
  CHECK(std::string(p.b.view().bell.missed).find("Missed ") == 0);
}

static void test_knock_rules() {
  struct Case { Status st; bool banner; bool chime; Sound snd; };
  const Case cases[] = {{Status::Busy, true, true, Sound::SoftChime},
                        {Status::Private, true, false, Sound::Chime},
                        {Status::Sleeping, true, false, Sound::Chime},
                        {Status::Away, false, false, Sound::Chime}};
  for (const Case &c : cases) {
    Pair p;
    p.begin();
    p.run(200);
    p.b.choose_status(c.st);
    p.run(6000);
    p.pb.played.clear();
    p.a.tap_bell();
    p.run(100);
    CHECK((p.b.view().banner.kind == BannerKind::Knock) == c.banner);
    CHECK(!p.pb.played.empty() == c.chime);
    if (c.chime) CHECK(p.pb.played.back().first == c.snd && p.pb.played.back().second == 75);
    if (c.st == Status::Away) {
      p.b.choose_status(Status::Open);  // back home: the saved knock appears
      p.run(100);
      CHECK(p.b.view().banner.kind == BannerKind::MissedWhileAway);
      CHECK(std::string(p.b.view().banner.sub).find("while you were away") != std::string::npos);
    }
    if (c.banner) CHECK(p.b.view().banner.kind == BannerKind::Knock);
  }
}

static void test_no_signal() {
  Pair p;
  p.begin();
  p.b.on_local_reading(true, 680, 450);
  p.run(200);
  CHECK_STR(p.a.view().floor[(int)Floor::Downstairs].temp, "68\xC2\xB0" "F");
  p.cut = true;  // the downstairs panel loses power quietly
  p.run(5000);
  CHECK(!p.a.peer_no_signal());  // HANDOFF: nothing is sent on a timer, so nobody notices yet
  p.a.choose_status(Status::Busy);
  p.run(1700);
  CHECK(p.a.peer_no_signal());
  const FloorView &fv = p.a.view().floor[(int)Floor::Downstairs];
  CHECK(fv.no_signal);
  CHECK_STR(fv.word, "NO SIGNAL");
  CHECK_STR(fv.sub, "was OPEN just now");  // short enough to stay clear of the badge
  CHECK(std::string(fv.line).find("last seen ") == 0);
  CHECK_STR(fv.temp, "--\xC2\xB0" "F");  // GUI spec, Dusk board: no stale readings
  CHECK_STR(fv.hum, "--%");
  p.a.tap_bell();
  p.run(1700);
  CHECK(p.a.view().bell.out == BellOut::CouldntReach);
  p.run(60000);
  CHECK_STR(p.a.view().floor[(int)Floor::Downstairs].sub, "was OPEN, 1 min ago");
  // It comes back (without rebooting) and says something: the flag clears and the missed change arrives.
  p.cut = false;
  p.b.choose_status(Status::Away);
  p.run(300);
  CHECK(!p.a.peer_no_signal());
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].word, "BUSY");
  CHECK_STR(p.a.view().floor[(int)Floor::Downstairs].temp, "68\xC2\xB0" "F");  // the last reading again
}

static void test_reboot_exchange() {
  Pair p;
  p.begin();
  p.run(200);
  p.a.choose_status(Status::Sleeping);
  p.a.tap_dog();
  p.a.on_local_reading(true, 690, 440);
  p.run(300);
  // Downstairs reboots: new controller, its flash kept. Upstairs answers its STATE.
  FakePlatform pb2;
  pb2.w = p.pb.w;
  pb2.mono = p.pb.mono;
  pb2.rnd = 999;  // different random start for seq
  Controller b2(pb2);
  p.b.choose_status(Status::Busy);  // downstairs' own status before it loses power
  p.run(200);
  b2.begin(p.sb, p.pb.saved_own, p.pb.saved_dog);  // its flash survives the reboot
  for (int i = 0; i < 20; i++) {
    p.pa.mono += 20; pb2.mono += 20;
    p.a.tick(); b2.tick();
    for (int r = 0; r < 4; r++) {
      std::vector<Sent> oa, ob;
      oa.swap(p.pa.out); ob.swap(pb2.out);
      for (auto &s : oa) b2.on_radio(s.bytes.data(), s.bytes.size());
      for (auto &s : ob) p.a.on_radio(s.bytes.data(), s.bytes.size());
    }
  }
  CHECK_STR(b2.view().floor[(int)Floor::Upstairs].word, "SLEEPING");
  CHECK(b2.dog().out() == Dog::Lucy);
  CHECK_STR(b2.view().floor[(int)Floor::Upstairs].temp, "69\xC2\xB0" "F");
  CHECK_STR(b2.view().dog.pill, "LUCY OUT");
  CHECK(b2.view().dog.can_tap);  // Copper's button works while Lucy is out (both can be)
  CHECK(b2.own().status == Status::Busy);
  CHECK_STR(p.a.view().floor[(int)Floor::Downstairs].word, "BUSY");  // and it re-announced itself
}

static void test_clock_copy() {
  Pair p;
  p.pb.w = WallTime();  // downstairs lost power with no coin cell: no clock
  p.begin();
  p.run(200);
  CHECK(p.pb.w.valid);
  CHECK(p.pb.w.quality == kClockFromPeer);
  CHECK(std::abs((long)p.pb.w.utc - (long)p.pa.w.utc) <= 1);
  int calls = p.pb.set_wall_calls;
  p.b.choose_status(Status::Busy);
  p.run(200);
  CHECK(p.pa.w.quality == kClockManual);   // upstairs never copies a worse clock
  CHECK(p.pa.set_wall_calls == 0);
  CHECK(p.pb.set_wall_calls == calls);     // clocks already agree: no needless re-copy
  // Without any clock, the panel says so instead of showing a wrong time.
  FakePlatform pc;
  Controller c(pc);
  Settings s;
  c.begin(s, OwnState(), DogState());
  c.tick();
  CHECK_STR(c.view().date, "Clock not set");
  CHECK(!c.start_timer(Status::Private, 600, Status::Busy));
}

static void test_quiet_hours_and_backlight() {
  Pair p(1792627200u + 3 * 3600);  // 03:00 UTC = 11 PM EDT
  p.begin();
  p.run(200);
  p.a.tap_bell();
  p.run(100);
  CHECK(!p.pb.played.empty() && p.pb.played.back().second == 80);  // quiet-hours cap (MIKE: 80 at night)
  // Night: dim at 224 (Mike's level), full light after a touch, back to dim 30 s later.
  Pair q(1792627200u + 7 * 3600);  // 3 AM EDT
  q.begin();
  q.run(200);
  CHECK(q.a.view().mode == Mode::Night);
  CHECK(q.pa.backlight == 224);
  q.a.touch();
  q.run(100);
  CHECK(q.pa.backlight == 0);
  q.run(30500);
  CHECK(q.pa.backlight == 224);
}

static void test_demo_loopback() {
  FakePlatform host;
  host.w = {true, 1792627200u + 17 * 3600, kClockManual};
  Controller panel(host);
  Demo demo(host);
  // The panel's radio goes into the demo instead of the air.
  struct Router : Platform {
    FakePlatform &h; Demo &d;
    Router(FakePlatform &h_, Demo &d_) : h(h_), d(d_) {}
    uint32_t mono_ms() override { return h.mono_ms(); }
    WallTime wall() override { return h.wall(); }
    void set_wall(uint32_t u, uint8_t q) override { h.set_wall(u, q); }
    int32_t utc_offset(uint32_t u) override { return h.utc_offset(u); }
    void radio_send(uint8_t to, const uint8_t *x, size_t n) override { d.from_panel(to, x, n); }
    void play(Sound s, uint8_t v) override { h.play(s, v); }
    void set_backlight(uint8_t r) override { h.set_backlight(r); }
    uint32_t random32() override { return h.random32(); }
  } router(host, demo);
  Controller real(router);
  Settings s;
  s.floor = Floor::Downstairs;
  real.begin(s, OwnState(), DogState());
  demo.begin(real);
  auto run = [&](int ms) { for (int t = 0; t < ms; t += 20) { host.mono += 20; real.tick(); demo.tick(); } };
  run(300);
  CHECK_STR(real.view().floor[(int)Floor::Upstairs].word, "BUSY");
  CHECK_STR(real.view().floor[(int)Floor::Upstairs].temp, "69\xC2\xB0" "F");
  real.tap_bell();
  run(200);
  CHECK(real.view().bell.out == BellOut::Delivered);
  run(3200);
  CHECK(real.view().bell.out == BellOut::ComeOn);
  demo.peer_ring();
  run(200);
  CHECK(real.view().banner.kind == BannerKind::Knock);
  demo.gateway_next_outside();
  run(300);
  CHECK(real.view().outside.valid);
  CHECK_STR(real.view().outside.phrase, "Clear, cooler");
  demo.gateway_toggle_lucy();
  run(300);
  CHECK(real.dog().out() == Dog::Lucy);
  demo.peer_toggle_unplugged();
  real.choose_status(Status::Private);
  run(2000);
  CHECK(real.view().floor[(int)Floor::Upstairs].no_signal);
  demo.peer_toggle_unplugged();  // plugged back in: reboots and asks for our STATE
  run(400);
  CHECK(!real.view().floor[(int)Floor::Upstairs].no_signal);
  CHECK(demo.peer().view().floor[(int)Floor::Downstairs].status == Status::Private);
  (void)panel;
}

static void test_wraparound() {
  // Start just before the millisecond counter wraps and keep going across it.
  Pair p;
  p.pa.mono = p.pb.mono = 0xFFFFF000u;
  p.begin();
  p.run(200);
  p.a.tap_bell();
  p.run(6000);  // crosses the wrap
  CHECK(p.a.view().bell.out == BellOut::Delivered);
  p.b.answer_banner(true);
  p.run(200);
  CHECK(p.a.view().bell.out == BellOut::ComeOn);
  // Then nothing happens for 25 days: the doorbell, toast and backlight must not wake up.
  Pair q(1792627200u + 7 * 3600);  // 3 AM EDT: night
  q.begin();
  q.run(200);
  q.a.choose_status(Status::Busy);
  q.a.tap_bell();
  q.run(40000);
  q.pa.mono += 0x80000000u;
  q.pb.mono += 0x80000000u;
  q.run(200);
  CHECK(q.pa.backlight == 224);  // still the night level, not full light
  q.pb.played.clear();
  q.a.tap_bell();
  q.run(200);
  CHECK(!q.pb.played.empty());   // the tap went through
}

// Both dogs out, end to end: each panel's button is its own dog's, and both panels agree.
static void test_both_dogs() {
  Pair p;
  p.begin();
  p.run(200);
  CHECK(p.a.tap_dog());  // Lucy out (upstairs)
  p.run(200);
  CHECK(p.b.tap_dog());  // Copper out too (downstairs)
  p.run(200);
  CHECK(p.a.dog().out() == Dog::Both && p.b.dog().out() == Dog::Both);
  CHECK_STR(p.a.view().dog.pill, "BOTH DOGS OUT");
  CHECK_STR(p.b.view().dog.pill, "BOTH DOGS OUT");
  // Both buttons pressed at the same moment: neither change is lost.
  p.a.tap_dog();  // Lucy in
  p.b.tap_dog();  // Copper in
  p.run(2000);
  CHECK(p.a.dog().out() == Dog::None && p.b.dog().out() == Dog::None);
  CHECK(p.pa.saved_dog.out() == Dog::None);
}

// Mail for downstairs (Mike, 2026-10-26): upstairs marks it, downstairs acknowledges it.
static ComfortAnswer make_answer() {
  ComfortAnswer a = {};
  a.n = 12;
  a.utc = (uint32_t)seconds_from_civil(2026, 11, 6, 15, 30, 0) - 3600;
  a.offset = 3600;
  a.clock_q = kClockNetwork;
  a.feel = (uint8_t)Feel::TooWarm;
  a.status = (uint8_t)Status::Busy;
  a.have = kHaveInside | kHaveUpstairs | kHaveOutside;
  a.in_t = 673; a.in_h = 472; a.up_t = 701; a.up_h = 455; a.out_t = 482; a.out_h = 61;
  return a;
}

static std::string csv(const ComfortAnswer &a) {
  char buf[200];
  size_t n = comfort_csv_line(a, buf, sizeof(buf));
  return n ? std::string(buf, n) : std::string("<too long>");
}

static void test_comfort() {
  // Parts of the day: night 10 PM-6 AM, morning to 10, midday to 4 PM, evening to 10 PM.
  CHECK(day_part(0) == DayPart::Night && day_part(359) == DayPart::Night);
  CHECK(day_part(360) == DayPart::Morning && day_part(599) == DayPart::Morning);
  CHECK(day_part(600) == DayPart::Midday && day_part(959) == DayPart::Midday);
  CHECK(day_part(960) == DayPart::Evening && day_part(1319) == DayPart::Evening);
  CHECK(day_part(1320) == DayPart::Night && day_part(1439) == DayPart::Night);
  CHECK_STR(day_part_word(DayPart::Night), "NIGHT");
  CHECK_STR(day_part_word(DayPart::Morning), "MORNING");
  CHECK_STR(day_part_word(DayPart::Midday), "MIDDAY");
  CHECK_STR(day_part_word(DayPart::Evening), "EVENING");
  const char *seasons[] = {"WINTER", "WINTER", "SPRING", "SPRING", "SPRING", "SUMMER", "SUMMER", "SUMMER", "FALL", "FALL", "FALL", "WINTER"};
  for (int m = 1; m <= 12; m++) CHECK_STR(season_word(m), seasons[m - 1]);
  CHECK_STR(season_word(0), "");
  CHECK_STR(season_word(13), "");

  CHECK_STR(feel_code(Feel::MuchTooHot), "MUCH_TOO_HOT");
  CHECK_STR(feel_code(Feel::TooWarm), "TOO_WARM");
  CHECK_STR(feel_code(Feel::JustRight), "JUST_RIGHT");
  CHECK_STR(feel_code(Feel::TooCool), "TOO_COOL");
  CHECK_STR(feel_code(Feel::MuchTooCold), "MUCH_TOO_COLD");
  CHECK_STR(feel_label(Feel::MuchTooHot), "MUCH TOO HOT");
  CHECK_STR(feel_label(Feel::JustRight), "JUST RIGHT");
  CHECK_STR(feel_label(Feel::MuchTooCold), "MUCH TOO COLD");
  CHECK_STR(feel_code((Feel)9), "");
  CHECK_STR(ask_code(Ask::None), "NONE");
  CHECK_STR(ask_code(Ask::UrgentHeat), "URGENT_HEAT");
  CHECK_STR(ask_code(Ask::MildHeat), "MILD_HEAT");
  CHECK_STR(ask_code(Ask::MildCool), "MILD_COOL");
  CHECK_STR(ask_code(Ask::UrgentCool), "URGENT_COOL");
  CHECK_STR(ask_code((Ask)9), "");
  CHECK_STR(ask_sent_code(AskSent::Blank), "");
  CHECK_STR(ask_sent_code(AskSent::Send), "SEND");
  CHECK_STR(ask_sent_code(AskSent::Cancel), "CANCEL");
  CHECK_STR(ask_sent_code((AskSent)9), "");

  // The file's lines.
  CHECK_STR(kComfortFile, "comfort.csv");
  ComfortAnswer a = make_answer();
  CHECK_STR(csv(a), "12,2026-11-06,15:30,1,MIDDAY,Fri,Nov,FALL,67.3,47.2,TOO_WARM,NONE,,BUSY,70.1,45.5,48.2,61,\n");
  char tiny[10];
  CHECK(comfort_csv_line(a, tiny, sizeof(tiny)) == 0);
  // The longest possible line, every field at its widest, fits the buffers that carry it
  // (controller and the panel's line queue), and so does the header.
  ComfortAnswer w = make_answer();
  w.n = 4294967295u; w.feel = (uint8_t)Feel::MuchTooCold; w.ask = (uint8_t)Ask::UrgentHeat;
  w.sent = (uint8_t)AskSent::Cancel; w.status = (uint8_t)Status::Sleeping; w.utc = 1788767400u + 9 * 3600;
  w.in_t = -32768; w.in_h = 65535; w.up_t = -32768; w.up_h = 65535; w.out_t = -32768; w.out_h = 255;
  w.removed = 1788767400u + 86400;
  char wide[kComfortLineMax];
  size_t wl = comfort_csv_line(w, wide, sizeof(wide));
  CHECK(wl > 0 && wl < kComfortLineMax);
  CHECK(strlen(kComfortCsvHeader) < kComfortLineMax);
  // No clock, only the inside reading: the time columns are blank.
  ComfortAnswer b = {};
  b.n = 1; b.feel = (uint8_t)Feel::JustRight; b.have = kHaveInside; b.in_t = 673; b.in_h = 472;
  CHECK_STR(csv(b), "1,,,0,,,,,67.3,47.2,JUST_RIGHT,NONE,,OPEN,,,,,\n");
  // Negative temperatures, the offset 0, a clock from the peer, stage 2 and 3 fields.
  ComfortAnswer c = {};
  c.n = 3;
  c.utc = (uint32_t)seconds_from_civil(2027, 1, 1, 0, 5, 0);
  c.clock_q = kClockFromPeer;
  c.feel = (uint8_t)Feel::MuchTooCold; c.ask = (uint8_t)Ask::UrgentCool; c.sent = (uint8_t)AskSent::Send;
  c.status = (uint8_t)Status::Sleeping;
  c.have = kHaveInside | kHaveUpstairs | kHaveOutside;
  c.in_t = -5; c.in_h = 305; c.up_t = -35; c.up_h = 1000; c.out_t = -125; c.out_h = 100;
  c.removed = (uint32_t)seconds_from_civil(2027, 1, 1, 0, 45, 0);
  CHECK_STR(csv(c), "3,2027-01-01,00:05,1,NIGHT,Fri,Jan,WINTER,-0.5,30.5,MUCH_TOO_COLD,URGENT_COOL,SEND,SLEEPING,-3.5,100.0,-12.5,100,2027-01-01 00:45\n");
  c.clock_q = kClockManual;
  CHECK(csv(c).find(",00:05,0,NIGHT") != std::string::npos);  // set by hand: not sure
  // The local day is the offset's: 23:30 UTC at +3600 is 00:30 the next day.
  a.utc = (uint32_t)seconds_from_civil(2026, 12, 31, 23, 30, 0);
  CHECK(csv(a).find(",2027-01-01,00:30,") != std::string::npos);

  // The wire: 5 + 33 bytes, little-endian.
  Msg m;
  m.type = MsgType::Comfort;
  m.sender = kSenderDownstairs;
  m.seq = 77;
  a = make_answer();
  a.offset = -3600;
  a.removed = 99;
  m.u.comfort = a;
  uint8_t buf[64];
  size_t n = encode(m, buf, sizeof(buf));
  CHECK(n == 38);
  CHECK(buf[5] == 12 && buf[6] == 0 && buf[13] == 0xF0 && buf[14] == 0xF1 && buf[16] == 0xFF);  // n, offset -3600
  Msg d;
  CHECK(decode(buf, n, d));
  const ComfortAnswer &e = d.u.comfort;
  CHECK(d.type == MsgType::Comfort && d.seq == 77 && d.sender == kSenderDownstairs);
  CHECK(e.n == 12 && e.utc == a.utc && e.offset == -3600 && e.clock_q == kClockNetwork && e.feel == 1 && e.status == 1);
  CHECK(e.have == 7 && e.in_t == 673 && e.in_h == 472 && e.up_t == 701 && e.up_h == 455 && e.out_t == 482 && e.out_h == 61 && e.removed == 99);
  CHECK_STR(msg_name(MsgType::Comfort), "COMFORT");
  CHECK(!decode(buf, n - 1, d));
  uint8_t longer[64];
  memcpy(longer, buf, n);
  longer[n] = 0;
  CHECK(!decode(longer, n + 1, d));
  const struct { int at; uint8_t v; } bad[] = {{18, 5}, {19, 5}, {20, 3}, {21, 5}};  // feel, ask, sent, status
  for (auto &x : bad) {
    memcpy(longer, buf, n);
    longer[x.at] = x.v;
    CHECK(!decode(longer, n, d));
  }
  memcpy(longer, buf, n);
  longer[19] = 4; longer[20] = 2; longer[21] = 4;  // the largest valid ones
  CHECK(decode(longer, n, d));
  // Every answer matters: a newer one never replaces one waiting.
  RetryQueue q;
  CHECK(q.push(m, kSenderDownstairs, 0) && q.push(m, kSenderDownstairs, 0));
  CHECK(q.pending(MsgType::Comfort) == 2);

  // Two panels: the answer reaches upstairs as the same line.
  {
    Pair p;
    p.pa.offset = 3600; p.pb.offset = 3600;
    p.begin();
    p.run(1000);
    p.b.on_local_reading(true, 673, 472);
    p.run(500);
    CHECK(!p.a.comfort_answer(Feel::TooWarm));  // upstairs doesn't answer
    CHECK(p.pa.lines.empty() && p.a.comfort_count() == 0);
    CHECK(p.b.comfort_answer(Feel::TooWarm));
    CHECK(p.pb.lines.size() == 1 && p.pb.saved_comfort == 1 && p.b.comfort_count() == 1);
    CHECK_STR(p.pb.line_header, kComfortCsvHeader);
    p.run(500);
    CHECK(p.pa.lines.size() == 1 && p.pa.lines[0] == p.pb.lines[0]);
    CHECK(p.a.comfort_count() == 1 && p.pa.saved_comfort == 1);
    CHECK_STR(p.pa.line_header, kComfortCsvHeader);
    CHECK(p.pb.lines[0].find("1,2026-10-22,01:") == 0);
    CHECK(p.pb.lines[0].find(",TOO_WARM,NONE,,OPEN,") != std::string::npos && p.pb.lines[0].find(",67.3,47.2,") != std::string::npos);
    CHECK(p.pb.lines[0].find(",0,NIGHT,Thu,Oct,FALL,") != std::string::npos);  // manual clock
    // A resend of the same number is ignored; a gap is noted and the later one kept.
    Msg r;
    r.type = MsgType::Comfort;
    r.sender = kSenderDownstairs;
    r.seq = 4000;
    r.u.comfort = make_answer();
    r.u.comfort.n = 1;
    uint8_t fb[64];
    size_t fn = encode(r, fb, sizeof(fb));
    p.a.on_radio(fb, fn);
    r.seq = 4001;
    fn = encode(r, fb, sizeof(fb));
    p.a.on_radio(fb, fn);
    CHECK(p.pa.lines.size() == 1 && p.a.comfort_count() == 1);
    r.seq = 4002;
    r.u.comfort.n = 3;
    fn = encode(r, fb, sizeof(fb));
    p.a.on_radio(fb, fn);
    CHECK(p.pa.lines.size() == 2 && p.a.comfort_count() == 3 && p.pa.saved_comfort == 3);
    // The downstairs panel ignores a COMFORT.
    p.b.on_radio(fb, fn);
    CHECK(p.pb.lines.size() == 1 && p.b.comfort_count() == 1);
  }
  // The other panel unreachable: the answers stay in the outbox and arrive, in order, once it is heard.
  {
    Pair p;
    p.pa.offset = 3600; p.pb.offset = 3600;
    p.begin();
    p.run(1000);
    p.cut = true;
    for (int i = 0; i < 5; i++) {
      CHECK(p.b.comfort_answer(i % 2 ? Feel::TooCool : Feel::TooWarm));
      p.run(400);
    }
    p.run(4000);
    CHECK(p.b.peer_no_signal());
    CHECK(p.pa.lines.empty() && p.pb.lines.size() == 5);
    p.cut = false;
    p.run(3000);
    CHECK(p.pa.lines.empty());                  // nobody has said anything yet
    p.a.choose_status(Status::Busy);            // upstairs is heard again
    p.run(3000);
    CHECK(!p.b.peer_no_signal());
    CHECK(p.pa.lines.size() == 5 && p.a.comfort_count() == 5);
    for (int i = 0; i < 5; i++) CHECK(p.pa.lines[i] == p.pb.lines[i]);
    CHECK(p.pa.lines[0].find("1,") == 0 && p.pa.lines[4].find("5,") == 0);
    p.run(5000);
    CHECK(p.pa.lines.size() == 5);              // all ACKed: nothing is sent again
    CHECK(p.b.comfort_count() == 5);
  }
  // Two answers made quickly while the first frame is lost once: with two in flight, answer 2 arrived
  // first, upstairs counted 2, and the retry of answer 1 was ACKed but ignored, so answer 1 was lost.
  {
    Pair p;
    p.pa.offset = 3600; p.pb.offset = 3600;
    p.begin();
    p.run(1000);
    int dropped = 0;
    p.drop = [&](bool from_a, const Msg &m) {
      if (from_a || m.type != MsgType::Comfort || m.u.comfort.n != 1 || dropped) return false;
      dropped++;
      return true;
    };
    CHECK(p.b.comfort_answer(Feel::TooWarm));
    CHECK(p.b.comfort_answer(Feel::TooCool));
    p.run(6000);
    CHECK(dropped == 1);
    CHECK(p.pa.lines.size() == 2 && p.a.comfort_count() == 2 && p.pa.saved_comfort == 2);
    if (p.pa.lines.size() == 2) {
      CHECK(p.pa.lines[0] == p.pb.lines[0] && p.pa.lines[1] == p.pb.lines[1]);
      CHECK(p.pa.lines[0].find("1,") == 0 && p.pa.lines[1].find("2,") == 0);
    }
    CHECK(p.pa.lines.size() == 2 && !p.b.peer_no_signal());
  }
}

// Stage 3: the comfort chart's file reader, units, the removal text and the removal over the radio.
static std::string chart_line(uint32_t n, Feel f, int t, int h, uint32_t removed = 0, bool inside = true) {
  ComfortAnswer a = make_answer();
  a.n = n; a.feel = (uint8_t)f; a.in_t = (int16_t)t; a.in_h = (uint16_t)h; a.removed = removed;
  if (!inside) a.have &= ~kHaveInside;
  return csv(a);
}

static void test_update_both_and_pending() {
  // The wire: 5 + 1 bytes, type 15; a wrong length is refused.
  Msg m;
  m.type = MsgType::Update;
  m.sender = kSenderUpstairs;
  m.seq = 7;
  uint8_t buf[16];
  size_t len = encode(m, buf, sizeof(buf));
  CHECK(len == 6 && buf[1] == 15 && buf[5] == 0);
  Msg d;
  CHECK(decode(buf, len, d) && d.type == MsgType::Update && d.seq == 7 && d.sender == kSenderUpstairs);
  CHECK_STR(msg_name(MsgType::Update), "UPDATE");
  CHECK(!decode(buf, len - 1, d));
  buf[len] = 0;
  CHECK(!decode(buf, len + 1, d));
  RetryQueue rq;
  CHECK(rq.push(m, kSenderUpstairs, 0) && rq.push(m, kSenderUpstairs, 0));
  CHECK(rq.pending(MsgType::Update) == 2);  // not latest-wins

  // update_both: UPDATE goes out and the updater starts once it is ACKed.
  {
    Pair p;
    p.begin();
    p.run(1000);
    CHECK(p.a.update_both());
    CHECK(p.pa.updater_starts == 0);
    p.run(100);
    CHECK(p.pa.updater_starts == 0);  // ACKed at once, but both wait about 700 ms so saves reach flash
    CHECK(p.pb.updater_starts == 0);
    p.run(1000);
    CHECK(p.pb.updater_starts == 1 && p.pa.updater_starts == 1);  // well before the 2 s no-answer wait
    p.run(5000);
    CHECK(p.pb.updater_starts == 1 && p.pa.updater_starts == 1);  // once each
  }
  // No ACK: this panel starts its own updater after 2 s, not before.
  {
    Pair p;
    p.begin();
    p.run(1000);
    p.cut = true;
    CHECK(p.b.update_both());
    p.run(1900);
    CHECK(p.pb.updater_starts == 0);
    p.run(200);
    CHECK(p.pb.updater_starts == 1 && p.pa.updater_starts == 0);
    p.run(3000);
    CHECK(p.pb.updater_starts == 1);
  }
  // Without an updater here: false, nothing sent. The other panel without one ignores UPDATE.
  {
    Pair p;
    p.begin();
    p.run(1000);
    p.pa.has_updater = false;
    p.pa.out.clear();
    CHECK(!p.a.update_both());
    CHECK(p.pa.out.empty() && p.pa.updater_starts == 0);
    p.pa.has_updater = true;
    p.pb.has_updater = false;
    CHECK(p.a.update_both());
    p.run(2000);
    CHECK(p.pa.updater_starts == 1 && p.pb.updater_starts == 0);
  }
  // A received UPDATE starts the updater after about 700 ms, and only from the peer.
  {
    Pair p;
    p.begin();
    p.run(1000);
    Msg u;
    u.type = MsgType::Update;
    u.sender = kSenderGateway;
    u.seq = 3000;
    uint8_t fb[16];
    size_t fn = encode(u, fb, sizeof(fb));
    p.b.on_radio(fb, fn);
    p.run(2000);
    CHECK(p.pb.updater_starts == 0);
    u.sender = kSenderUpstairs;
    u.seq = 3001;
    fn = encode(u, fb, sizeof(fb));
    p.pb.out.clear();
    p.b.on_radio(fb, fn);
    CHECK(!p.pb.out.empty());  // the ACK is out at once
    p.run(600);
    CHECK(p.pb.updater_starts == 0);
    p.run(200);
    CHECK(p.pb.updater_starts == 1);
    p.run(3000);
    CHECK(p.pb.updater_starts == 1);
  }

  // The comfort outbox is saved on every change; a restored one is delivered in order.
  {
    Pair p;
    p.pa.offset = 3600; p.pb.offset = 3600;
    p.begin();
    p.run(1000);
    p.b.on_local_reading(true, 673, 472);
    p.run(500);
    p.cut = true;
    int saves = p.pb.pending_saves;
    CHECK(p.b.comfort_answer(Feel::TooWarm));
    CHECK(p.pb.pending_saves == saves + 1 && p.pb.saved_pending.size() == 1 && p.pb.saved_pending[0].del == 0);
    CHECK(p.b.comfort_answer(Feel::TooCool));
    CHECK(p.pb.saved_pending.size() == 2);
    CHECK(p.b.comfort_remove(1));
    CHECK(p.pb.saved_pending.size() == 3 && p.pb.saved_pending[2].del == 1 && p.pb.saved_pending[2].a.n == 1);
    CHECK(p.pb.saved_pending[0].a.n == 1 && p.pb.saved_pending[1].a.n == 2);
    std::vector<ComfortPending> kept = p.pb.saved_pending;
    // A restart: a fresh downstairs panel, fed the saved list, with upstairs unreachable until heard.
    Pair q;
    q.pa.offset = 3600; q.pb.offset = 3600;
    q.begin();
    q.b.set_comfort_count(2);
    q.cut = true;
    q.b.restore_comfort_pending(kept.data(), (int)kept.size());
    q.a.set_comfort_count(0);
    q.run(5000);
    CHECK(q.pa.lines.empty());
    q.cut = false;
    q.a.choose_status(Status::Busy);
    q.run(5000);
    CHECK(q.pa.lines.size() == 2 && q.pa.csv_sets.size() == 1);  // both answers, then the removal
    CHECK(q.pb.saved_pending.empty());                          // all ACKed: saved as empty
    CHECK(q.pa.lines[0].find("1,") == 0 && q.pa.lines[1].find("2,") == 0);
  }
  // A restored list longer than the outbox keeps the newest.
  {
    Pair p;
    p.begin();
    p.run(500);
    p.cut = true;
    std::vector<ComfortPending> big(40);
    for (int i = 0; i < 40; i++) { big[i] = ComfortPending{}; big[i].a.n = (uint32_t)(i + 1); big[i].a.feel = (uint8_t)Feel::TooWarm; }
    p.b.restore_comfort_pending(big.data(), 40);
    CHECK(p.pb.saved_pending.size() == 32 && p.pb.saved_pending[0].a.n == 9 && p.pb.saved_pending[31].a.n == 40);
  }
}

// The chart's zoom (Mike, 2026-11-18): the view for the dots shown, the pace, the steps of the
// labelled lines, and that the whole chart still lands on the pixels it always did.
static void test_chart_view() {
  const auto near = [](float a, float b, float tol = 0.001f) { return fabsf(a - b) <= tol; };
  const auto inside = [](const ChartView &v) {
    return v.t0 >= kChartTMin && v.t1 <= kChartTMax && v.g0 >= 0 && v.g1 <= kChartGrMax && v.t0 < v.t1 && v.g0 < v.g1;
  };
  const auto pt = [](int t10, int h10, Feel f, DayPart part, bool removed = false) {
    ComfortPoint p = {};
    p.n = 1; p.t = (int16_t)t10; p.h = (uint16_t)h10; p.feel = (uint8_t)f; p.part = (uint8_t)part; p.removed = removed;
    return p;
  };

  // The box of the dots, 1.5 times as wide and as tall, about its middle.
  ChartView v = chart_view_around(66, 74, 40, 80);
  CHECK(near(v.t0, 64) && near(v.t1, 76) && near(v.g0, 30) && near(v.g1, 90));
  CHECK(near((v.t0 + v.t1) / 2, 70) && near((v.g0 + v.g1) / 2, 60));
  CHECK(near(v.t1 - v.t0, 1.5f * 8) && near(v.g1 - v.g0, 1.5f * 40));
  // Minimum spans: one dot, and a tight cluster, each about its own middle.
  v = chart_view_around(70, 70, 50, 50);
  CHECK(near(v.t0, 67) && near(v.t1, 73) && near(v.g0, 38) && near(v.g1, 62));
  v = chart_view_around(70, 71, 50, 52);
  CHECK(near(v.t1 - v.t0, kChartMinSpanT) && near(v.g1 - v.g0, kChartMinSpanG) && near((v.t0 + v.t1) / 2, 70.5f) &&
        near((v.g0 + v.g1) / 2, 51));
  // The minimums leave labelled lines: 6 F at 1 F steps, 24 gr/lb at 5.
  CHECK(chart_nice_step(kChartMinSpanT) == 1 && chart_nice_step(kChartMinSpanG) == 5);
  // Each edge of the chart: the view keeps its size and is moved inside.
  v = chart_view_around(55, 57, 50, 60);  // left
  CHECK(near(v.t0, 55) && near(v.t1, 61) && inside(v));
  v = chart_view_around(83, 85, 50, 60);  // right
  CHECK(near(v.t0, 79) && near(v.t1, 85) && inside(v));
  v = chart_view_around(68, 72, 0, 20);  // bottom
  CHECK(near(v.g0, 0) && near(v.g1, 30) && inside(v));
  v = chart_view_around(68, 72, 100, 120);  // top
  CHECK(near(v.g0, 90) && near(v.g1, 120) && inside(v));
  v = chart_view_around(55, 55, 120, 120);  // a single dot in a corner
  CHECK(near(v.t0, 55) && near(v.t1, 61) && near(v.g0, 96) && near(v.g1, 120));
  // Never larger than the chart: dots over more than two thirds of an axis give the whole axis.
  v = chart_view_around(56, 84, 50, 60);
  CHECK(v.t0 == kChartTMin && v.t1 == kChartTMax && near(v.g1 - v.g0, kChartMinSpanG));
  v = chart_view_around(55, 85, 0, 120);
  CHECK(chart_view_same(v, kChartFull));
  v = chart_view_around(60, 80.1f, 10, 95);
  CHECK(chart_view_same(v, kChartFull));

  // From the points: only those shown count.
  ComfortPoint ps[6] = {pt(680, 450, Feel::JustRight, DayPart::Night),  pt(720, 500, Feel::TooWarm, DayPart::Morning),
                        pt(740, 550, Feel::TooWarm, DayPart::Morning),  pt(600, 300, Feel::TooCool, DayPart::Night, true),
                        pt(900, 500, Feel::MuchTooHot, DayPart::Night), pt(700, 500, Feel::JustRight, DayPart::Evening)};
  ps[5].feel = 9;  // not a known answer
  CHECK(chart_point_shown(ps[0], 0) && chart_point_shown(ps[0], 1) && !chart_point_shown(ps[0], 2));
  CHECK(!chart_point_shown(ps[3], 0) && !chart_point_shown(ps[4], 0) && !chart_point_shown(ps[5], 0));
  float g0 = grains_per_lb(68, 45), g2 = grains_per_lb(74, 55), g1 = grains_per_lb(72, 50);
  v = chart_target_view(ps, 6, 0);  // ALL: the three kept, on-chart, known answers
  CHECK(near(v.t0, 71 - 4.5f) && near(v.t1, 71 + 4.5f));
  CHECK(near(v.g0, (g0 + g2) / 2 - 0.75f * (g2 - g0), 0.01f) && near(v.g1, (g0 + g2) / 2 + 0.75f * (g2 - g0), 0.01f));
  v = chart_target_view(ps, 6, 2);  // MORNING: two dots 2 F apart, so the minimum across
  CHECK(near(v.t0, 70) && near(v.t1, 76) && near((v.g0 + v.g1) / 2, (g1 + g2) / 2, 0.01f) && near(v.g1 - v.g0, kChartMinSpanG, 0.01f));
  v = chart_target_view(ps, 6, 1);  // NIGHT: one dot (the removed and the off-chart ones don't count)
  CHECK(near(v.t0, 65) && near(v.t1, 71) && near((v.g0 + v.g1) / 2, g0, 0.01f) && near(v.g1 - v.g0, kChartMinSpanG, 0.01f));
  // No dots shown: the whole chart, so nothing moves.
  CHECK(chart_view_same(chart_target_view(ps, 6, 3), kChartFull));
  CHECK(chart_view_same(chart_target_view(ps, 0, 0), kChartFull));
  CHECK(chart_view_same(chart_target_view(nullptr, 0, 0), kChartFull));
  CHECK(chart_view_same(chart_target_view(ps + 3, 3, 0), kChartFull));
  CHECK(!chart_view_same(chart_target_view(ps, 6, 0), kChartFull));

  // The pace: 0 at 0, 1 at 1, never backwards, gentle at the start, and much slower at the end
  // than in the middle.
  CHECK(chart_ease(0) == 0 && chart_ease(1) == 1 && chart_ease(-1) == 0 && chart_ease(2) == 1);
  bool rising = true;
  for (int i = 1; i <= 1000; i++)
    if (!(chart_ease(i / 1000.0f) >= chart_ease((i - 1) / 1000.0f))) rising = false;
  CHECK(rising);
  float first = chart_ease(0.1f) - chart_ease(0), mid = chart_ease(0.5f) - chart_ease(0.4f), last = chart_ease(1) - chart_ease(0.9f);
  CHECK(first < mid / 4);   // starts gently
  CHECK(last < mid / 20);   // the last tenth of the time: under a twentieth of the middle's pace
  CHECK(chart_ease(0.75f) > 0.9f && chart_ease(0.5f) > 0.5f && chart_ease(0.5f) < 0.65f);
  CHECK(chart_ease(1) - chart_ease(0.75f) < chart_ease(0.5f) - chart_ease(0.25f));

  // Interpolation: the ends exactly, in between inside both, the span shrinking in equal ratios.
  ChartView a = kChartFull, b = chart_view_around(70, 70, 50, 50);
  ChartView m = chart_view_lerp(a, b, 0);
  CHECK(m.t0 == a.t0 && m.t1 == a.t1 && m.g0 == a.g0 && m.g1 == a.g1);
  m = chart_view_lerp(a, b, 1);
  CHECK(m.t0 == b.t0 && m.t1 == b.t1 && m.g0 == b.g0 && m.g1 == b.g1);
  m = chart_view_lerp(a, b, 0.5f);
  CHECK(near(m.t1 - m.t0, sqrtf(30.0f * 6), 0.01f) && near(m.g1 - m.g0, sqrtf(120.0f * 24), 0.01f));
  CHECK(m.t0 > a.t0 && m.t0 < b.t0 && m.t1 < a.t1 && m.t1 > b.t1 && m.g0 > a.g0 && m.g0 < b.g0 && m.g1 < a.g1 && m.g1 > b.g1);
  bool ok = true;
  ChartView prev = a;
  for (int i = 1; i <= 100; i++) {
    ChartView s = chart_view_lerp(a, b, i / 100.0f);
    if (!inside(s) || s.t0 < prev.t0 || s.t1 > prev.t1 || s.g0 < prev.g0 || s.g1 > prev.g1) ok = false;
    prev = s;
  }
  CHECK(ok);
  // The same size both ends (a slide between two parts of the day): halfway is halfway.
  ChartView c = chart_view_around(60, 60, 30, 30), d = chart_view_around(78, 78, 90, 90);
  m = chart_view_lerp(c, d, 0.5f);
  CHECK(near(m.t0, (c.t0 + d.t0) / 2) && near(m.t1, (c.t1 + d.t1) / 2) && near(m.g0, (c.g0 + d.g0) / 2) && near(m.g1, (c.g1 + d.g1) / 2));
  // Back out to the whole chart is the same path the other way.
  ChartView out = chart_view_lerp(b, a, 0.25f), in = chart_view_lerp(a, b, 0.75f);
  CHECK(near(out.t0, in.t0, 0.01f) && near(out.t1, in.t1, 0.01f) && near(out.g0, in.g0, 0.05f) && near(out.g1, in.g1, 0.05f));
  CHECK(chart_view_same(a, a) && !chart_view_same(a, b));

  // The steps of the labelled lines: the whole chart's own (5 F, 20 gr/lb), then finer.
  CHECK(chart_nice_step(30) == 5 && chart_nice_step(120) == 20);
  CHECK(chart_nice_step(24.75f) == 5 && chart_nice_step(20) == 5 && chart_nice_step(19.9f) == 2);
  CHECK(chart_nice_step(12) == 2 && chart_nice_step(8) == 2 && chart_nice_step(7.9f) == 1 && chart_nice_step(6) == 1);
  CHECK(chart_nice_step(96) == 20 && chart_nice_step(79) == 10 && chart_nice_step(40) == 10 && chart_nice_step(39) == 5);
  CHECK(chart_nice_step(3) == 0.5f && chart_nice_step(0.01f) == 0.1f);
  bool count_ok = true;
  for (float span = 6; span <= 120; span += 0.37f) {
    float lines = span / chart_nice_step(span);
    if (lines < 4 || lines >= 10) count_ok = false;
  }
  CHECK(count_ok);
  // The humidity curves: every 20 % on the whole chart; closer in, every 10 or 5 so two still cross.
  CHECK(chart_rh_step(kChartFull) == 20);
  CHECK(chart_rh_step(ChartView{67, 73, 42.6f, 66.6f}) == 20);  // 40 % and 60 % both cross
  CHECK(chart_rh_step(ChartView{68, 74, 50, 74}) == 10);        // only 60 % of the 20s: 50 % joins it
  CHECK(chart_rh_step(ChartView{55, 61, 0, 24}) == 10);         // the cool dry corner: 10, 20, 30 %
  CHECK(chart_rh_step(ChartView{79, 85, 0, 24}) == 5);          // the warm dry corner: 5, 10, 15 %

  // The whole chart puts known points on the pixels the chart always used (plot 64..712 by 84..404).
  const auto old_x = [](float t) { return 64 + (t - kChartTMin) / (kChartTMax - kChartTMin) * (712 - 64); };
  const auto old_y = [](float g) { return 404 - g / kChartGrMax * (404 - 84); };
  const float ts[] = {55, 61.3f, 67.4f, 70, 72.5f, 76.6f, 82.9f, 85}, gs[] = {0, 28, 43.21f, 56, 58.3f, 84, 92, 100, 120};
  bool same_px = true;
  for (float t : ts)
    if (chart_x(kChartFull, t, 64, 712) != old_x(t)) same_px = false;
  for (float g : gs)
    if (chart_y(kChartFull, g, 84, 404) != old_y(g)) same_px = false;
  CHECK(same_px);
  CHECK(chart_x(kChartFull, 55, 64, 712) == 64 && chart_x(kChartFull, 85, 64, 712) == 712 && chart_x(kChartFull, 70, 64, 712) == 388);
  CHECK(chart_y(kChartFull, 0, 84, 404) == 404 && chart_y(kChartFull, 120, 84, 404) == 84 && chart_y(kChartFull, 60, 84, 404) == 244);
  // A zoomed view: its own edges on the plot's edges, its middle in the middle.
  v = chart_view_around(70, 70, 50, 50);
  CHECK(chart_x(v, 67, 64, 712) == 64 && chart_x(v, 73, 64, 712) == 712 && near(chart_x(v, 70, 64, 712), 388));
  CHECK(chart_y(v, 38, 84, 404) == 404 && chart_y(v, 62, 84, 404) == 84 && near(chart_y(v, 50, 84, 404), 244));
  CHECK(kChartHoldMs == 1000 && kChartZoomMs == 1400 && kChartZoomGrow == 1.5f);
}

static void test_comfort_chart() {
  std::string head = kComfortCsvHeader;
  std::string body = chart_line(1, Feel::MuchTooHot, 781, 612) + chart_line(2, Feel::TooCool, 655, 400, 5000) +
                     chart_line(3, Feel::JustRight, 700, 500, 0, false) + chart_line(4, Feel::MuchTooCold, 601, 355);
  std::string all = head + body;
  CHECK(comfort_count_lines(all.data(), all.size()) == 4);
  ComfortPoint pt[8];
  int n = comfort_parse(all.data(), all.size(), pt, 8);
  CHECK(n == 3);  // the line without the inside reading can't be placed
  CHECK(pt[0].n == 1 && pt[0].t == 781 && pt[0].h == 612 && pt[0].feel == (uint8_t)Feel::MuchTooHot && !pt[0].removed);
  CHECK(pt[1].n == 2 && pt[1].removed && pt[1].feel == (uint8_t)Feel::TooCool && pt[1].t == 655);
  CHECK(pt[2].n == 4 && pt[2].feel == (uint8_t)Feel::MuchTooCold);
  CHECK(pt[0].year == 2026 && pt[0].month == 11 && pt[0].day == 6 && pt[0].hour == 15 && pt[0].minute == 30);
  CHECK(pt[0].part == (uint8_t)DayPart::Midday && pt[0].weekday == 5);
  CHECK(comfort_parse(all.data(), all.size(), pt, 2) == 2);  // cap
  // A cut-off last line (a power cut), CRLF line ends and no header.
  std::string cut = all.substr(0, all.size() - 30);
  CHECK(comfort_parse(cut.data(), cut.size(), pt, 8) == 2);
  std::string crlf;
  for (char c : all) { if (c == '\n') crlf += '\r'; crlf += c; }
  CHECK(comfort_parse(crlf.data(), crlf.size(), pt, 8) == 3 && pt[2].n == 4 && !pt[2].removed);
  CHECK(comfort_parse(body.data(), body.size(), pt, 8) == 3 && pt[0].n == 1);
  CHECK(comfort_parse("", 0, pt, 8) == 0 && comfort_count_lines("", 0) == 0 && comfort_parse(head.data(), head.size(), pt, 8) == 0);
  // Bad lines: an unknown answer, too few columns.
  std::string bad = chart_line(1, Feel::TooWarm, 700, 500);
  bad.replace(bad.find("TOO_WARM"), 8, "MAYBE");
  CHECK(comfort_parse(bad.data(), bad.size(), pt, 8) == 0);
  std::string few = "5,2026-11-06,15:30,1,MIDDAY,Fri,Nov,FALL,70.0,50.0,TOO_WARM\n";
  CHECK(comfort_parse(few.data(), few.size(), pt, 8) == 0);
  // Blank date and time: year 0, and no part or weekday.
  ComfortAnswer nc = make_answer();
  nc.utc = 0; nc.offset = 0; nc.clock_q = kClockNone;
  std::string nt = csv(nc);
  CHECK(comfort_parse(nt.data(), nt.size(), pt, 8) == 1 && pt[0].year == 0 && pt[0].part == 0xFF && pt[0].weekday == 0xFF);
  // Negative and zero readings survive.
  std::string neg = chart_line(7, Feel::TooCool, -5, 0);
  CHECK(comfort_parse(neg.data(), neg.size(), pt, 8) == 1 && pt[0].t == -5 && pt[0].h == 0);

  // The chart's height.
  CHECK(fabsf(grains_per_lb(72, 50) - 58.3f) < 1.0f);
  CHECK(fabsf(grains_per_lb(70, 30) - 32.7f) < 1.0f);
  CHECK(grains_per_lb(60, 0) == 0);
  CHECK(grains_per_lb(80, 80) > grains_per_lb(80, 40) && grains_per_lb(80, 40) > grains_per_lb(60, 40));

  // The removed column's text.
  char t[32];
  uint32_t u = (uint32_t)seconds_from_civil(2026, 11, 6, 23, 30, 0);
  CHECK(comfort_removed_text(u, 0, t, sizeof(t)) == 16);
  CHECK_STR(t, "2026-11-06 23:30");
  CHECK(comfort_removed_text(u, 3600, t, sizeof(t)) == 16);
  CHECK_STR(t, "2026-11-07 00:30");
  CHECK(comfort_removed_text(0, 0, t, sizeof(t)) == 7);
  CHECK_STR(t, "REMOVED");
  char small[8];
  CHECK(comfort_removed_text(u, 0, small, sizeof(small)) == 0);
  CHECK(kComfortRemovedColumn == 18);

  // The wire: 5 + 12 bytes, little-endian.
  Msg m;
  m.type = MsgType::ComfortDel;
  m.sender = kSenderDownstairs;
  m.seq = 9;
  m.u.comfort_del = {258, 99, -3600};
  uint8_t buf[32];
  size_t len = encode(m, buf, sizeof(buf));
  CHECK(len == 17 && buf[1] == 14 && buf[5] == 2 && buf[6] == 1 && buf[9] == 99 && buf[13] == 0xF0 && buf[14] == 0xF1);
  Msg d;
  CHECK(decode(buf, len, d) && d.type == MsgType::ComfortDel);
  CHECK(d.u.comfort_del.n == 258 && d.u.comfort_del.removed == 99 && d.u.comfort_del.offset == -3600);
  CHECK_STR(msg_name(MsgType::ComfortDel), "COMFORT_DEL");
  CHECK(!decode(buf, len - 1, d));
  uint8_t longer[32];
  memcpy(longer, buf, len);
  longer[len] = 0;
  CHECK(!decode(longer, len + 1, d));
  m.u.comfort_del.n = 0;
  len = encode(m, buf, sizeof(buf));
  CHECK(len == 17 && !decode(buf, len, d));
  RetryQueue q;
  m.u.comfort_del.n = 1;
  CHECK(q.push(m, kSenderDownstairs, 0) && q.push(m, kSenderDownstairs, 0));
  CHECK(q.pending(MsgType::ComfortDel) == 2);  // none replaces another

  // Two panels: the removal is marked in both files with the same text.
  {
    Pair p;
    p.pa.offset = 3600; p.pb.offset = 3600;
    p.begin();
    p.run(1000);
    p.b.on_local_reading(true, 673, 472);
    p.run(500);
    CHECK(p.b.comfort_answer(Feel::TooWarm) && p.b.comfort_answer(Feel::TooCool));
    p.run(500);
    CHECK(!p.a.comfort_remove(1) && p.pa.csv_sets.empty());  // upstairs can't remove
    CHECK(!p.b.comfort_remove(0) && !p.b.comfort_remove(3) && p.pb.csv_sets.empty());
    CHECK(p.b.comfort_remove(2));
    CHECK(p.pb.csv_sets.size() == 1);
    p.run(500);
    CHECK(p.pa.csv_sets.size() == 1 && p.pa.csv_sets[0] == p.pb.csv_sets[0]);
    CHECK(p.pb.csv_sets[0].find("comfort.csv|2|18|") == 0);
    CHECK(p.pb.csv_sets[0].size() == strlen("comfort.csv|2|18|") + 16);
    p.run(5000);
    CHECK(p.pa.csv_sets.size() == 1);  // ACKed: not sent again
    // Upstairs ignores a removal of an answer it doesn't have; downstairs ignores any COMFORT_DEL.
    Msg r;
    r.type = MsgType::ComfortDel;
    r.sender = kSenderDownstairs;
    r.seq = 4000;
    r.u.comfort_del = {9, 5, 0};
    uint8_t fb[32];
    size_t fn = encode(r, fb, sizeof(fb));
    p.a.on_radio(fb, fn);
    CHECK(p.pa.csv_sets.size() == 1);
    r.u.comfort_del.n = 1;
    r.seq = 4001;
    fn = encode(r, fb, sizeof(fb));
    p.b.on_radio(fb, fn);
    CHECK(p.pb.csv_sets.size() == 1);
    // With no clock the column still says REMOVED.
    p.pb.w.valid = false;
    CHECK(p.b.comfort_remove(1));
    CHECK(p.pb.csv_sets[1] == "comfort.csv|1|18|REMOVED");
    p.run(500);
    CHECK(p.pa.csv_sets.size() == 2 && p.pa.csv_sets[1] == p.pb.csv_sets[1]);
  }
  // An answer and then its removal made while upstairs is unreachable arrive in order once it is back.
  {
    Pair p;
    p.pa.offset = 3600; p.pb.offset = 3600;
    p.begin();
    p.run(1000);
    p.b.on_local_reading(true, 673, 472);
    p.run(500);
    p.cut = true;
    CHECK(p.b.comfort_answer(Feel::TooWarm));
    p.run(400);
    CHECK(p.b.comfort_answer(Feel::TooCool));
    CHECK(p.b.comfort_remove(2));
    p.run(5000);
    CHECK(p.b.peer_no_signal());
    CHECK(p.pa.lines.empty() && p.pa.csv_sets.empty() && p.pb.csv_sets.size() == 1);
    p.cut = false;
    p.a.choose_status(Status::Busy);  // upstairs is heard again
    p.run(4000);
    CHECK(p.pa.lines.size() == 2 && p.pa.csv_sets.size() == 1);
    CHECK(p.pa.csv_sets[0] == p.pb.csv_sets[0]);
    p.run(5000);
    CHECK(p.pa.lines.size() == 2 && p.pa.csv_sets.size() == 1);
  }
}

static void test_mail() {
  // The model: the higher counter wins; a tie goes to the later time, then to "waiting".
  WallTime t1{true, 1000, kClockManual}, t2{true, 2000, kClockManual};
  MailModel up, down;
  up.toggle(t1);
  CHECK(up.state().waiting && up.state().ver == 1 && up.state().since == 1000);
  CHECK(down.apply_remote(up.state()) && down.state().waiting);
  CHECK(!down.apply_remote(up.state()));  // the same again changes nothing
  CHECK(down.acknowledge(t2) && !down.state().waiting && down.state().ver == 2);
  CHECK(!down.acknowledge(t2));            // nothing waiting
  CHECK(!down.apply_remote(up.state()));  // the older one doesn't come back
  CHECK(up.apply_remote(down.state()) && !up.state().waiting);
  MailState a, b;
  a.waiting = true; a.since = 5; a.ver = 3;
  b.waiting = false; b.since = 6; b.ver = 3;
  MailModel x;
  x.restore(a);
  CHECK(x.apply_remote(b) && !x.state().waiting);  // the same counter: the later change
  b.since = 5;
  MailModel y;
  y.restore(b);
  CHECK(y.apply_remote(a) && y.state().waiting);   // a true tie: waiting wins
  MailModel z;
  z.restore(a);
  CHECK(!z.apply_remote(b));                       // on both panels

  // The message: 14 bytes; waiting is 0 or 1 (and 2 for no mail, see test_nomail).
  Msg m;
  m.type = MsgType::Mail;
  m.sender = kSenderUpstairs;
  m.seq = 0x1234;
  m.u.mail.waiting = 1;
  m.u.mail.since = 0x6ADF03D0u;
  m.u.mail.ver = 7;
  uint8_t buf[32];
  size_t n = encode(m, buf, sizeof(buf));
  CHECK(n == 14);
  Msg d;
  CHECK(decode(buf, n, d) && d.type == MsgType::Mail && d.u.mail.waiting == 1 && d.u.mail.since == 0x6ADF03D0u &&
        d.u.mail.ver == 7);
  buf[5] = 2;  // Mike, 2026-11-18: 2 is "no mail", same size
  CHECK(decode(buf, n, d) && d.u.mail.waiting == 2 && d.u.mail.ver == 7);
  buf[5] = 3;
  CHECK(!decode(buf, n, d));
  CHECK_STR(msg_name(MsgType::Mail), "MAIL");

  // Two panels.
  Pair p;
  p.begin();
  p.run(200);
  CHECK(p.a.view().mail.show && !p.a.view().mail.solid);  // upstairs: the faint outline
  CHECK(!p.b.view().mail.show);                           // downstairs: nothing
  size_t played = p.pb.played.size();
  CHECK(p.a.tap_mail());
  p.run(200);
  CHECK(p.b.mail().waiting && p.pb.saved_mail.waiting && p.pa.saved_mail.waiting);
  CHECK(p.a.view().mail.show && p.a.view().mail.solid && !p.a.view().mail.pulse);  // solid, steady
  CHECK(p.b.view().mail.show && p.b.view().mail.solid && p.b.view().mail.pulse);   // solid, fading
  CHECK(p.pb.played.size() == played + 1 && p.pb.played.back().first == Sound::SoftChime);  // OPEN: a soft chime
  CHECK(p.b.tap_mail());  // the tenant has it
  p.run(200);
  CHECK(!p.a.mail().waiting && !p.b.mail().waiting && !p.pa.saved_mail.waiting);
  CHECK(p.a.view().mail.show && !p.a.view().mail.solid);  // back to the outline
  CHECK(!p.b.view().mail.show);
  CHECK(!p.b.tap_mail());  // nothing to acknowledge
  // Marked by mistake: a second tap upstairs takes it back, on both.
  p.a.tap_mail();
  p.run(200);
  CHECK(p.b.mail().waiting);
  p.a.tap_mail();
  p.run(200);
  CHECK(!p.b.mail().waiting && !p.b.view().mail.show);

  // Downstairs SLEEPING: no chime (its doorbell is silent then).
  p.b.choose_status(Status::Sleeping);
  p.run(200);
  played = p.pb.played.size();
  p.a.tap_mail();
  p.run(200);
  CHECK(p.b.mail().waiting && p.pb.played.size() == played);
  p.b.tap_mail();
  p.run(200);
  // At night with the screen off overnight, new mail wakes it at the night level, as a knock does.
  Settings ss;
  ss.screen_off_overnight = true;
  BacklightInputs bi;
  bi.mode = Mode::Night;
  bi.minute_of_day = 23 * 60;
  CHECK(backlight_for(ss, bi) == kBacklightOff);
  bi.mail_wake = true;
  CHECK(backlight_for(ss, bi) == night_backlight(ss.night_level));

  // MIKE (2026-11-05): off overnight means dim at night until 10 PM, dark 10 PM to 6 AM by the clock.
  {
    BacklightInputs n;
    n.mode = Mode::Night;
    const uint8_t dim = night_backlight(ss.night_level);
    n.minute_of_day = 21 * 60 + 30;
    CHECK(backlight_for(ss, n) == dim);           // 9:30 PM: dim
    n.minute_of_day = 22 * 60;
    CHECK(backlight_for(ss, n) == kBacklightOff);  // 10 PM: dark
    n.minute_of_day = 3 * 60;
    CHECK(backlight_for(ss, n) == kBacklightOff);  // 3 AM
    n.minute_of_day = 5 * 60 + 59;
    CHECK(backlight_for(ss, n) == kBacklightOff);  // 5:59 AM
    n.minute_of_day = 6 * 60;
    CHECK(backlight_for(ss, n) == dim);           // 6 AM, still night: dim again
    n.mode = Mode::Evening;                       // summer: dawn before 6 AM, still dark by the clock
    n.minute_of_day = 5 * 60 + 30;
    CHECK(backlight_for(ss, n) == kBacklightOff);
    n.mode = Mode::Night;
    n.minute_of_day = 23 * 60;
    n.touched_recently = true;                    // a touch: full light, as always
    CHECK(backlight_for(ss, n) == ss.backlight_day);
    n.touched_recently = false;
    n.doorbell_wake = true;                       // a knock wakes it at the night level
    CHECK(backlight_for(ss, n) == dim);
    n.doorbell_wake = false;
    n.minute_of_day = -1;                         // no clock: dim, never dark
    n.clock_unknown = true;
    CHECK(backlight_for(ss, n) == dim);
    Settings on = ss;
    on.screen_off_overnight = false;              // the option off: dim all night
    BacklightInputs m;
    m.mode = Mode::Night;
    m.minute_of_day = 23 * 60;
    CHECK(backlight_for(on, m) == dim);
  }

  // Marked while downstairs can't hear: it gets there once the panels hear each other again.
  p.cut = true;
  p.a.tap_mail();
  p.run(3000);
  CHECK(p.a.peer_no_signal() && !p.b.mail().waiting);
  p.cut = false;
  p.b.choose_status(Status::Open);  // downstairs says something, and upstairs catches it up
  p.run(500);
  CHECK(p.b.mail().waiting && p.a.mail().waiting);

  // Demo mode: the simulated tenant takes the mail after 20 s.
  FakePlatform host;
  host.w = {true, 1792627200u + 17 * 3600, kClockManual};
  Demo demo(host);
  struct Router : Platform {
    FakePlatform &h; Demo &d;
    Router(FakePlatform &h_, Demo &d_) : h(h_), d(d_) {}
    uint32_t mono_ms() override { return h.mono_ms(); }
    WallTime wall() override { return h.wall(); }
    void set_wall(uint32_t u, uint8_t q) override { h.set_wall(u, q); }
    int32_t utc_offset(uint32_t u) override { return h.utc_offset(u); }
    void radio_send(uint8_t to, const uint8_t *x, size_t len) override { d.from_panel(to, x, len); }
    void play(Sound s, uint8_t v) override { h.play(s, v); }
    void set_backlight(uint8_t r) override { h.set_backlight(r); }
    uint32_t random32() override { return h.random32(); }
  } router(host, demo);
  Controller real(router);
  Settings s;
  s.floor = Floor::Upstairs;
  real.begin(s, OwnState(), DogState());
  demo.begin(real);
  auto run = [&](int ms) { for (int t = 0; t < ms; t += 20) { host.mono += 20; real.tick(); demo.tick(); } };
  run(300);
  real.tap_mail();
  run(1000);
  CHECK(demo.peer().mail().waiting && real.view().mail.solid);
  run(15000);
  CHECK(real.view().mail.solid);  // not yet
  run(5000);
  CHECK(!demo.peer().mail().waiting && !real.mail().waiting && !real.view().mail.solid);
  // Mike, 2026-11-18: the simulated tenant acknowledges a "no mail" mark the same way, after 20 s.
  real.tap_nomail();
  run(1000);
  CHECK(demo.peer().mail().none && real.view().nomail.active);
  run(15000);
  CHECK(real.view().nomail.active);  // not yet
  run(5000);
  CHECK(!demo.peer().mail().none && !real.mail().none && !real.view().nomail.active && real.view().nomail.show);
}

// Mike, 2026-10-26: the bell shakes for each ring sent or received; an incoming ring turns it red
// until it's answered or the banner dismissed, for 2 minutes at most.
static void test_bell_shake_and_red() {
  Pair p;
  p.begin();
  p.run(200);
  uint16_t a0 = p.a.view().bell.shakes, b0 = p.b.view().bell.shakes;
  p.a.tap_bell();
  p.run(200);
  CHECK(p.a.view().bell.shakes == (uint16_t)(a0 + 1));  // sent
  CHECK(p.b.view().bell.shakes == (uint16_t)(b0 + 1));  // received
  CHECK(p.b.view().bell.alert && !p.a.view().bell.alert);
  p.a.tap_bell();  // a repeat while repeats are ignored: no ring, so no shake
  p.run(200);
  CHECK(p.a.view().bell.shakes == (uint16_t)(a0 + 1) && p.b.view().bell.shakes == (uint16_t)(b0 + 1));
  p.b.answer_banner(true);
  p.run(200);
  CHECK(!p.b.view().bell.alert);
  // Unanswered: red while the banner shows, and both end at 2 minutes (the banner is 2 minutes long
  // since 2026-11-15, like the red bell).
  p.run(BellSender::kRepeatIgnoreMs);
  p.a.tap_bell();
  p.run(200);
  CHECK(p.b.view().bell.alert && p.b.view().bell.shakes == (uint16_t)(b0 + 2));
  p.run(70000);
  CHECK(p.b.view().banner.kind == BannerKind::Knock && p.b.view().bell.alert);
  p.run(50000);
  CHECK(p.b.view().banner.kind == BannerKind::None && !p.b.view().bell.alert);
  // Dismissed: plain at once.
  p.a.tap_bell();
  p.run(200);
  CHECK(p.b.view().bell.alert);
  p.b.dismiss_banner();
  p.run(100);
  CHECK(!p.b.view().bell.alert);
}

static void test_dog_no_clock() {
  Pair p;
  p.pa.w = WallTime();
  p.pb.w = WallTime();  // a power cut and no coin cell: neither panel knows the time
  p.begin();
  p.run(200);
  CHECK(p.b.tap_dog());  // Copper out
  p.run(200);
  CHECK(p.a.dog().out() == Dog::Copper);
  CHECK(p.b.tap_dog());  // and back in
  p.run(200);
  CHECK(p.a.dog().out() == Dog::None && p.b.dog().out() == Dog::None);
  // A quick double tap within the same second ends where the last tap said.
  Pair q;
  q.begin();
  q.run(200);
  q.a.tap_dog();
  q.a.tap_dog();
  q.run(300);
  CHECK(q.a.dog().out() == Dog::None && q.b.dog().out() == Dog::None);
}

// WeatherGate's Zambretti table (ZAMBRETTI_REFERENCE.md section 2) and its symbol keys.
static void test_zambretti() {
  const char *symbols = "AsBsCpDpErFrGhHhIhJsKsLpMpNcOrPrQhRhStTsUsVlWlXpYlZc";  // s sunny, p partly cloudy...
  for (const char *q = symbols; *q; q += 2) {
    Forecast want = q[1] == 's' ? Forecast::Sun : q[1] == 'p' ? Forecast::CloudSun : q[1] == 'c' ? Forecast::Cloud
                  : q[1] == 'r' ? Forecast::Rain : q[1] == 'h' ? Forecast::HeavyRain : q[1] == 't' ? Forecast::Thunder
                  : Forecast::Clearing;
    CHECK(zambretti_symbol(q[0]) == want);
    CHECK(std::strlen(zambretti_text(q[0])) > 0 && std::strlen(zambretti_text(q[0])) <= kMaxPhrase);
  }
  CHECK(zambretti_symbol('?') == Forecast::Unknown && zambretti_symbol('a') == Forecast::Unknown);
  CHECK_STR(zambretti_text('G'), "Rain at Times, Worse Later");
  CHECK_STR(zambretti_text('S'), "Stormy, Much Rain");
  // And back from the text to a letter (the screensaver's weather goes by the words): every letter's
  // own text gives a letter with that same text; other texts give none.
  for (char c = 'A'; c <= 'Z'; ++c) {
    char back = zambretti_letter(zambretti_text(c));
    CHECK(back >= 'A' && back <= c);
    CHECK_STR(zambretti_text(back), zambretti_text(c));
  }
  CHECK(zambretti_letter("Stormy, Much Rain") == 'S' && zambretti_letter("stormy, much rain") == 'S');
  CHECK(zambretti_letter("Fine Weather") == 'B' && zambretti_letter("Changeable, Mending") == 'Z');
  CHECK(zambretti_letter("Unsettled, Rain Later") == 'F' && zambretti_letter("Very Unsettled, Rain") == 'I');
  CHECK(zambretti_letter("No Pressure Data") == 0 && zambretti_letter("Snow at Times, Worse Later") == 0);
  CHECK(zambretti_letter("") == 0 && zambretti_letter(nullptr) == 0 && zambretti_letter("Fine") == 0);
  CHECK(zambretti_letter("Fine Weather ") == 0 && zambretti_letter("Fine_Weather") == 0);
  const char *keys[] = {"sunny", "partly_cloudy", "cloudy", "clearing", "rain", "heavy_rain", "snow", "storm"};
  for (const char *k : keys) {
    Forecast f = forecast_from_key(k);
    CHECK(f != Forecast::Unknown && forecast_valid((uint8_t)f));
    CHECK_STR(forecast_key(f), k);
  }
  CHECK(forecast_from_key("hail") == Forecast::Unknown && forecast_from_key(nullptr) == Forecast::Unknown);
}

// ---------------------------------------------------------------------------------------------
// Calendar

static Event make_event(const char *title, int y, int mo, int d, int start_min, int length, bool all_day = false) {
  Event e;
  std::snprintf(e.title, sizeof(e.title), "%s", title);
  e.day = (int32_t)days_from_civil(y, mo, d);
  e.all_day = all_day;
  e.start_min = (int16_t)start_min;
  e.length = (uint16_t)length;
  return e;
}

static void test_event_store() {
  static std::vector<uint8_t> mem(EventStore::memory_needed());
  EventStore s;
  CHECK(s.init(mem.data()) && s.count() == 0);
  int32_t today = (int32_t)days_from_civil(2026, 10, 12);
  CHECK(weekday_of(today) == 1);  // Monday
  CHECK(local_day(1792627200u, 0) == (int32_t)days_from_civil(2026, 10, 22));
  CHECK(local_day(1792627200u, -4 * 3600) == (int32_t)days_from_civil(2026, 10, 21));

  Event trip = make_event("  Work trip, Denver ", 2026, 10, 14, 0, 3, true);
  trip.floor = Floor::Upstairs;
  const Event *t = s.put_local(trip, 101, kSenderUpstairs, 5000);
  CHECK(t && t->id == 101 && t->ver == 1 && std::string(t->title) == "Work trip, Denver");  // trimmed
  CHECK(t->last_day() == today + 4 && t->covers(today + 3) && !t->covers(today + 5) && t->multi_day());
  CHECK(t->end_day() == today + 4);
  Event parcel = make_event("Parcel pickup", 2026, 10, 15, 15 * 60 + 30, 60);
  parcel.floor = Floor::Downstairs;
  const Event *p = s.put_local(parcel, 102, kSenderDownstairs, 5001);
  CHECK(p && p->end_min() == 16 * 60 + 30 && !p->multi_day());
  CHECK(s.put_local(parcel, 102, kSenderDownstairs, 5001) == nullptr);  // id taken
  Event call = make_event("Call with contractor", 2026, 10, 15, 10 * 60, 30);
  s.put_local(call, 103, kSenderUpstairs, 5002);
  Occ list[8];
  int n = s.range(today + 3, today + 3, list, 8);  // Thursday 15th
  CHECK(n == 3 && list[0].e->id == 101 && list[1].e->id == 103 && list[2].e->id == 102);  // all-day, 10:00, 3:30
  CHECK(list[0].day == today + 2 && list[0].covers(today + 3));
  CHECK(s.range(today, today + 1, list, 8) == 0);

  // An edit keeps the floor (spec 1.4) and raises the version.
  Event ed = *p;
  ed.floor = Floor::Upstairs;
  std::snprintf(ed.notes, sizeof(ed.notes), "ask neighbor, bring ID");
  const Event *p2 = s.put_local(ed, 0, kSenderUpstairs, 5003);
  CHECK(p2 && p2->floor == Floor::Downstairs && p2->ver > 3 && std::string(p2->notes) == "ask neighbor, bring ID");

  // Delete, undo.
  uint32_t d0 = s.digest();
  CHECK(s.remove_local(103, kSenderUpstairs, 5004));
  CHECK(s.range(today + 3, today + 3, list, 8) == 2 && s.count() == 3 && s.digest() != d0);
  CHECK(!s.remove_local(103, kSenderUpstairs, 5004));
  CHECK(s.restore_local(103, kSenderUpstairs, 5005) && s.range(today + 3, today + 3, list, 8) == 3);

  // The file: round trip, then a corrupt byte is refused.
  std::vector<uint8_t> buf(s.serialized_size());
  size_t len = s.serialize(buf.data(), buf.size());
  CHECK(len == buf.size() && len > 16);
  CHECK(s.serialize(buf.data(), buf.size() - 1) == 0);  // too small: nothing
  static std::vector<uint8_t> mem2(EventStore::memory_needed());
  EventStore s2;
  s2.init(mem2.data());
  CHECK(s2.deserialize(buf.data(), len) && s2.count() == 3 && s2.digest() == s.digest());
  CHECK(s2.next_ver() == s.next_ver() && std::string(s2.find(102)->notes) == "ask neighbor, bring ID");
  buf[20] ^= 0x40;
  CHECK(!s2.deserialize(buf.data(), len) && s2.count() == 0);

  // Merging: newer wins, older and equal are ignored, a tie on the version goes to the higher sender.
  Event in = *s.find(102);
  in.ver = s.find(102)->ver - 1;
  std::snprintf(in.title, sizeof(in.title), "stale");
  CHECK(s.merge(in, today, 6000) == EventStore::kSame);
  in.ver = s.find(102)->ver;
  in.by = kSenderDownstairs;  // higher than the stored version's sender (upstairs)
  CHECK(s.merge(in, today, 6000) == EventStore::kChanged && std::string(s.find(102)->title) == "stale");
  in.ver += 5;
  in.floor = Floor::Upstairs;
  CHECK(s.merge(in, today, 6000) == EventStore::kChanged && s.find(102)->floor == Floor::Downstairs);  // the floor stays
  CHECK(s.next_ver() > in.ver);
  // An event long over, new to this panel, belongs in the archive; so does an old deletion.
  Event old = make_event("Old", 2026, 1, 2, 600, 60);
  old.id = 555;
  old.ver = 1;
  CHECK(s.merge(old, today, 6000) == EventStore::kArchive && !s.find(555));
  Event gone = old;
  gone.id = 556;
  gone.deleted = true;
  CHECK(s.merge(gone, today, 6000) == EventStore::kArchive && !s.find(556));
  Event recent = make_event("Recent", 2026, 9, 1, 600, 60);  // 41 days ago: kept here
  recent.id = 557;
  recent.ver = 1;
  CHECK(s.merge(recent, today, 6000) == EventStore::kChanged && s.find(557));

  // To the archive: events over for more than 92 days, and deletions made more than 92 days ago.
  Event past = make_event("Past", 2026, 4, 1, 600, 60);
  s.put_local(past, 104, kSenderUpstairs, 5006);
  s.remove_local(101, kSenderUpstairs, 1000);
  CHECK(s.count() == 5);
  Event out[8];
  CHECK(s.take_old(today, 5000, out, 8, false) == 1 && out[0].id == 104 && !s.find(104));  // the deletion is new
  CHECK(s.take_old(today, 1000 + 93 * 86400, out, 8, false) == 1 && out[0].id == 101 && out[0].deleted);
  CHECK(s.take_old(-1, 0, out, 8, true) == 0 && s.count() == 3);
  char name[8];
  archive_name(past, name, sizeof(name));
  CHECK_STR(name, "2026-04");

  // Wire: one event in a message, checked on the way in.
  Msg m;
  m.type = MsgType::EventMsg;
  m.u.event.len = (uint8_t)event_write(*s.find(102), m.u.event.data, sizeof(m.u.event.data));
  uint8_t wire[kMaxMsg];
  size_t wn = encode(m, wire, sizeof(wire));
  Msg back;
  CHECK(wn > 0 && wn <= kMaxMsg && decode(wire, wn, back) && back.type == MsgType::EventMsg);
  Event got;
  CHECK(event_read(back.u.event.data, back.u.event.len, got) && got.id == 102 && std::string(got.title) == "stale");
  wire[5 + 9] = 7;  // the floor byte
  CHECK(!decode(wire, wn, back));
  // The longest possible event (a series with every skip used) still fits a message.
  Event big = make_event("", 2026, 10, 20, 0, 60);
  std::memset(big.title, 'T', kEventTitleMax);
  std::memset(big.notes, 'n', kEventNotesMax);
  big.id = 9;
  big.repeat = Repeat::Weekly;
  big.until = big.day + 400;
  big.nskip = kMaxSkips;
  for (int i = 0; i < kMaxSkips; i++) big.skip[i] = (uint16_t)(7 * (i + 1));
  m.u.event.len = (uint8_t)event_write(big, m.u.event.data, sizeof(m.u.event.data));
  CHECK(m.u.event.len == kEventWireMax && encode(m, wire, sizeof(wire)) <= kMaxMsg);
  CHECK(event_read(m.u.event.data, m.u.event.len, got) && got.nskip == kMaxSkips && got.skip[23] == 168 &&
        got.repeat == Repeat::Weekly && got.until == big.until);
  m = Msg();
  m.type = MsgType::EventSum;
  m.u.sum.count = 3;
  m.u.sum.digest = 0xDEADBEEF;
  m.u.sum.flags = kSumPleaseReply;
  wn = encode(m, wire, sizeof(wire));
  CHECK(decode(wire, wn, back) && back.u.sum.digest == 0xDEADBEEF && back.u.sum.flags == kSumPleaseReply);

  // Texts are made printable.
  Event odd = make_event("tab	here", 2026, 10, 20, 0, 60);
  event_normalize(odd);
  CHECK(std::string(odd.title) == "tab?here");
}

static void test_repeats() {
  auto D = [](int y, int m, int d) { return (int32_t)days_from_civil(y, m, d); };
  int32_t st[16];
  // Weekly from Monday Oct. 12: the 12th, 19th and 26th in October; limited by a count or a date.
  Event w = make_event("Piano", 2026, 10, 12, 16 * 60, 45);
  w.repeat = Repeat::Weekly;
  CHECK(w.starts(D(2026, 10, 1), D(2026, 10, 31), st, 16) == 3 && st[0] == D(2026, 10, 12) && st[2] == D(2026, 10, 26));
  CHECK(w.end_day() == kForever && w.starts_on(D(2027, 3, 1)) && !w.starts_on(D(2027, 3, 2)));
  Event c = w;
  c.count = 2;
  CHECK(c.starts(D(2026, 10, 1), D(2026, 12, 31), st, 16) == 2 && c.end_day() == D(2026, 10, 19));
  Event u = w;
  u.until = D(2026, 10, 25);
  CHECK(u.starts(D(2026, 10, 1), D(2026, 12, 31), st, 16) == 2 && u.end_day() == D(2026, 10, 19));
  // One taken out ("just this one"): gone from the list, still counted.
  c.count = 3;
  c.nskip = 1;
  c.skip[0] = 7;
  CHECK(c.starts(D(2026, 10, 1), D(2026, 12, 31), st, 16) == 2 && st[1] == D(2026, 10, 26) && c.skips(D(2026, 10, 19)));
  // Monthly on the 31st: months without one are passed over, and don't count.
  Event mo = make_event("Rent", 2026, 1, 31, 9 * 60, 30);
  mo.repeat = Repeat::Monthly;
  mo.count = 4;
  CHECK(mo.starts(D(2026, 1, 1), D(2026, 12, 31), st, 16) == 4 && st[1] == D(2026, 3, 31) && st[3] == D(2026, 7, 31));
  CHECK(mo.end_day() == D(2026, 7, 31));
  CHECK(mo.starts(D(2026, 6, 1), D(2026, 12, 31), st, 16) == 1 && st[0] == D(2026, 7, 31));
  // Daily, from the middle of a range.
  Event dl = make_event("Pills", 2026, 10, 20, 8 * 60, 15);
  dl.repeat = Repeat::Daily;
  CHECK(dl.starts(D(2026, 10, 25), D(2026, 10, 27), st, 16) == 3 && st[0] == D(2026, 10, 25));
  // Yearly from February 29: the 28th in other years.
  Event bd = make_event("Sam", 2024, 2, 29, 0, 1, true);
  bd.kind = EventKind::Birthday;
  bd.repeat = Repeat::Yearly;
  CHECK(bd.starts_on(D(2025, 2, 28)) && bd.starts_on(D(2028, 2, 29)) && !bd.starts_on(D(2028, 2, 28)));
  // A weekly weekend (Friday to Sunday) touches Sunday from Friday's start.
  Event wk = make_event("Cabin", 2026, 10, 16, 0, 3, true);
  wk.repeat = Repeat::Weekly;
  CHECK(wk.starts(D(2026, 10, 25), D(2026, 10, 25), st, 16) == 1 && st[0] == D(2026, 10, 23));
  CHECK(wk.starts(D(2026, 10, 19), D(2026, 10, 22), st, 16) == 0);
  // Normalizing: a plain event loses repeat fields; an end before the start moves to the start.
  Event plain = make_event("x", 2026, 10, 20, 0, 60);
  plain.until = 5;
  plain.nskip = 3;
  event_normalize(plain);
  CHECK(plain.until == 0 && plain.nskip == 0);
  Event back = w;
  back.until = w.day - 5;
  event_normalize(back);
  CHECK(back.until == w.day);

  // In the store: a series shows each week; one occurrence taken out and put back.
  static std::vector<uint8_t> mem(EventStore::memory_needed());
  EventStore s;
  s.init(mem.data());
  const Event *ser = s.put_local(w, 201, kSenderUpstairs, 100);
  Occ o[16];
  CHECK(ser && s.range(D(2026, 10, 12), D(2026, 10, 25), o, 16) == 2 && o[1].day == D(2026, 10, 19));
  CHECK(!s.skip_local(201, D(2026, 10, 20), kSenderUpstairs, 101));  // not one of its days
  uint32_t v0 = ser->ver;
  CHECK(s.skip_local(201, D(2026, 10, 19), kSenderUpstairs, 101) && s.find(201)->ver > v0);
  CHECK(s.range(D(2026, 10, 19), D(2026, 10, 19), o, 16) == 0);
  CHECK(!s.skip_local(201, D(2026, 10, 19), kSenderUpstairs, 102));  // already out
  CHECK(s.unskip_local(201, D(2026, 10, 19), kSenderUpstairs, 103) && s.range(D(2026, 10, 19), D(2026, 10, 19), o, 16) == 1);
  // Its file round trip keeps the rule and the skips.
  s.skip_local(201, D(2026, 11, 2), kSenderUpstairs, 104);
  std::vector<uint8_t> buf(s.serialized_size());
  CHECK(s.serialize(buf.data(), buf.size()) == buf.size());
  static std::vector<uint8_t> mem2(EventStore::memory_needed());
  EventStore s2;
  s2.init(mem2.data());
  CHECK(s2.deserialize(buf.data(), buf.size()) && s2.find(201)->nskip == 1 && s2.find(201)->repeat == Repeat::Weekly);
  CHECK(!s2.find(201)->starts_on(D(2026, 11, 2)) && s2.find(201)->starts_on(D(2026, 11, 9)));
  // A series that never ends never goes to the archive.
  Event out[4];
  CHECK(s.take_old(D(2040, 1, 1), 2000000000u, out, 4, false) == 0);
}

static void test_holidays() {
  auto H = [](int y, int m, int d) {
    const Holiday *h = federal_holiday((int32_t)days_from_civil(y, m, d));
    return std::string(h ? h->short_name : "");
  };
  CHECK(H(2026, 1, 1) == "New Year's" && H(2026, 1, 19) == "MLK Day" && H(2026, 1, 12) == "");
  CHECK(H(2026, 2, 16) == "Presidents'" && H(2026, 5, 25) == "Memorial" && H(2026, 5, 18) == "");
  CHECK(H(2026, 6, 19) == "Juneteenth" && H(2020, 6, 19) == "" && H(2026, 7, 4) == "July 4th");
  CHECK(H(2026, 9, 7) == "Labor Day" && H(2026, 10, 12) == "Columbus" && H(2026, 10, 19) == "");
  CHECK(H(2026, 11, 11) == "Veterans" && H(2026, 11, 26) == "Thanksgiving" && H(2026, 11, 19) == "");
  CHECK(H(2026, 12, 25) == "Christmas" && H(2027, 5, 31) == "Memorial" && H(2027, 11, 25) == "Thanksgiving");
  const Holiday *x = federal_holiday((int32_t)days_from_civil(2026, 12, 25));
  CHECK(x && std::string(x->message) == "Merry Christmas" && x->icon == DayIcon::Tree);
}

static void test_calendar_sync() {
  Pair p;  // a upstairs, b downstairs; 2026-10-21 8 PM local
  p.begin();
  p.run(500);
  int32_t today = p.a.today();
  CHECK(today == (int32_t)days_from_civil(2026, 10, 21));
  // A new event takes the panel's floor, whatever the editor said (spec 1.1), and reaches the other.
  Event e = make_event("Dentist", 2026, 10, 22, 14 * 60, 60);
  e.floor = Floor::Downstairs;
  uint32_t id = p.a.save_event(e);
  CHECK(id != 0 && p.a.events().find(id)->floor == Floor::Upstairs);
  p.run(300);
  const Event *on_b = p.b.events().find(id);
  CHECK(on_b && on_b->floor == Floor::Upstairs && std::string(on_b->title) == "Dentist");
  // Either panel may edit the other floor's event (spec 1.5, PROPOSED); the floor stays.
  Event ed = *on_b;
  std::snprintf(ed.title, sizeof(ed.title), "Dentist (moved)");
  ed.start_min = 15 * 60;
  CHECK(p.b.save_event(ed) == id);
  p.run(300);
  CHECK(std::string(p.a.events().find(id)->title) == "Dentist (moved)" && p.a.events().find(id)->floor == Floor::Upstairs);
  // Saved a couple of seconds later, once for the burst.
  CHECK(p.pa.event_saves() == 0);
  p.run(2500);
  CHECK(p.pa.event_saves() == 1 && p.pb.event_saves() >= 1);
  // Delete and Undo travel too.
  CHECK(p.b.delete_event(id));
  p.run(300);
  CHECK(p.a.events().find(id)->deleted);
  CHECK(p.b.restore_event(id));
  p.run(300);
  CHECK(!p.a.events().find(id)->deleted);
  CHECK(!p.a.delete_event(424242));

  // A weekly series, and one week of it changed on its own: the series skips that week, a new
  // event (on the series' floor, whichever panel made the change) takes its place, on both panels.
  Event lesson = make_event("Piano", 2026, 10, 22, 16 * 60, 45);
  lesson.repeat = Repeat::Weekly;
  uint32_t sid = p.a.save_event(lesson);
  p.run(300);
  int32_t d29 = (int32_t)days_from_civil(2026, 10, 29);
  Event moved = *p.b.events().find(sid);
  moved.start_min = 17 * 60;
  uint32_t one = p.b.save_occurrence(sid, d29, moved);
  p.run(300);
  CHECK(one && p.a.events().find(one) && p.a.events().find(one)->floor == Floor::Upstairs &&
        !p.a.events().find(one)->repeats() && p.a.events().find(one)->start_min == 17 * 60);
  CHECK(p.a.events().find(sid)->skips(d29) && !p.a.events().find(sid)->skips(d29 + 7));
  CHECK(p.b.save_occurrence(sid, d29 + 1, moved) == 0);  // not one of its days
  CHECK(p.a.delete_occurrence(sid, d29 + 7));
  p.run(300);
  CHECK(p.b.events().find(sid)->skips(d29 + 7));
  CHECK(p.a.restore_occurrence(sid, d29 + 7));
  p.run(300);
  CHECK(!p.b.events().find(sid)->skips(d29 + 7));

  // A birthday today: the date strip's message, with the federal holiday's first on a holiday.
  Event bd = make_event("Sam", 1990, 10, 21, 0, 1, true);
  bd.kind = EventKind::Birthday;
  bd.repeat = Repeat::Yearly;
  p.a.save_event(bd);
  p.run(300);
  CHECK(p.b.view().nstrip == 1 && std::string(p.b.view().strip[0].text) == "Happy Birthday, Sam" &&
        p.b.view().strip[0].icon == DayIcon::Cake && std::string(p.b.view().strip[0].short_text) == "Sam");

  // Apart for a while: both add events, each marks the other No signal.
  p.cut = true;
  uint32_t ia = p.a.save_event(make_event("Upstairs only", 2026, 10, 25, 9 * 60, 60));
  uint32_t ib = p.b.save_event(make_event("Downstairs only", 2026, 10, 26, 0, 2, true));
  Event ed2 = *p.b.events().find(id);
  std::snprintf(ed2.notes, sizeof(ed2.notes), "edited while apart");
  p.b.save_event(ed2);
  p.run(3000);
  CHECK(p.a.peer_no_signal() && p.b.peer_no_signal());
  CHECK(p.a.events().digest() != p.b.events().digest());
  // Back together: the first message from the other side starts a comparison, and both catch up.
  p.cut = false;
  p.a.choose_status(Status::Busy);
  p.run(3000);
  CHECK(p.a.events().digest() == p.b.events().digest() && p.a.events().count() == p.b.events().count());
  CHECK(p.b.events().find(ia) && p.a.events().find(ib) && p.a.events().find(ib)->floor == Floor::Downstairs);
  CHECK(std::string(p.a.events().find(id)->notes) == "edited while apart");

  // A reboot reads the calendar back from its file.
  p.run(2500);
  FakePlatform fresh = p.pb;
  Controller again(fresh);
  again.begin(p.sb, OwnState(), DogState());
  CHECK(again.events().count() == p.b.events().count() && again.events().digest() == p.b.events().digest());

  // A full calendar refuses a new event rather than losing one, when nothing in it is past.
  for (int i = 0; i < kMaxEvents + 5; i++) p.a.save_event(make_event("Filler", 2026, 11, 1 + i % 28, 600, 60));
  CHECK(p.a.events().count() == kMaxEvents);
  CHECK(p.a.save_event(make_event("One too many", 2026, 11, 2, 600, 60)) == 0);
}

static void test_archive() {
  Pair p;  // 2026-10-21
  p.begin();
  p.run(300);
  auto D = [](int y, int m, int d) { return (int32_t)days_from_civil(y, m, d); };
  // An event from May: the other panel files it straight in its archive; this one at the next
  // day's start. It still shows, read back from the month's file.
  uint32_t may = p.a.save_event(make_event("Yard sale", 2026, 5, 2, 9 * 60, 120));
  p.run(300);
  CHECK(may && p.a.events().find(may) && !p.b.events().find(may) && p.pb.files.files.count("2026-05"));
  p.pa.w.utc += 86400;
  p.pb.w.utc += 86400;
  p.run(3000);
  CHECK(!p.a.events().find(may) && p.pa.files.files.count("2026-05"));
  Occ o[8];
  CHECK(p.a.range(D(2026, 5, 1), D(2026, 5, 31), o, 8) == 1 && o[0].e->id == may && o[0].day == D(2026, 5, 2));
  CHECK(p.a.find(may) && std::string(p.a.find(may)->title) == "Yard sale");
  // Changed from the archive: back in the store, shown once, and the other panel gets it.
  Event ed = *p.a.find(may);
  std::snprintf(ed.notes, sizeof(ed.notes), "rained out");
  CHECK(p.a.save_event(ed) == may && p.a.events().find(may));
  CHECK(p.a.range(D(2026, 5, 1), D(2026, 5, 31), o, 8) == 1 && std::string(o[0].e->notes) == "rained out");
  p.run(300);
  p.b.range(D(2026, 5, 1), D(2026, 5, 31), o, 8);  // b had it archived: the change goes into its file
  CHECK(p.b.find(may) && std::string(p.b.find(may)->notes) == "rained out");
  // An old event made on one panel goes straight into the other's archive.
  uint32_t mar = p.b.save_event(make_event("Dentist", 2026, 3, 9, 14 * 60, 60));
  p.run(300);
  CHECK(!p.a.events().find(mar) && p.pa.files.files.count("2026-03"));
  CHECK(p.a.range(D(2026, 3, 9), D(2026, 3, 9), o, 8) == 1 && o[0].e->id == mar);
  // Deleted from the archive: gone from the view.
  CHECK(p.a.delete_event(mar) && p.a.range(D(2026, 3, 9), D(2026, 3, 9), o, 8) == 0);
  // Full of recent events: the one that ended longest ago makes room for a new one.
  Pair q;
  q.begin();
  q.run(300);
  for (int i = 0; i < kMaxEvents; i++) q.a.save_event(make_event("Done", 2026, 9, 1 + i % 30, 600, 60));
  CHECK(q.a.events().count() == kMaxEvents);
  uint32_t fresh = q.a.save_event(make_event("New", 2026, 12, 1, 600, 60));
  CHECK(fresh && q.a.events().find(fresh) && q.pa.files.files.count("2026-09"));
  CHECK(q.a.range(D(2026, 9, 1), D(2026, 9, 1), o, 8) >= 1);
}

static void test_reading_and_outside() {
  Pair p;
  p.begin();
  p.run(200);
  p.a.on_local_reading(true, 700, 400);
  p.run(200);
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].temp, "70\xC2\xB0" "F");
  p.a.on_local_reading(false, 0, 0);  // one failed sensor read
  p.run(200);
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].temp, "--\xC2\xB0" "F");
  p.a.on_local_reading(true, 702, 401);  // back, at almost the same value
  p.run(200);
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].temp, "70\xC2\xB0" "F");
  // A new temperature offset shows at once on both panels, without waiting for the next reading.
  Settings s = p.a.settings();
  s.temp_offset_tenths_f = 20;
  p.a.apply_settings(s);
  p.run(200);
  CHECK_STR(p.a.view().floor[(int)Floor::Upstairs].temp, "72\xC2\xB0" "F");
  CHECK_STR(p.b.view().floor[(int)Floor::Upstairs].temp, "72\xC2\xB0" "F");
  s.temp_offset_tenths_f = 0;
  p.a.apply_settings(s);

  // OUTSIDE from the gateway reaches upstairs, is passed on, and goes to dashes after 15 minutes.
  uint8_t buf[kMaxMsg];
  Msg o;
  o.type = MsgType::Outside;
  o.sender = kSenderGateway;
  o.seq = 77;
  o.u.outside.flags = kOutsideValid;
  o.u.outside.temp_tenths_f = 480;
  o.u.outside.humidity = 71;
  o.u.outside.forecast = (uint8_t)Forecast::Moon;
  o.u.outside.time = p.pa.w.utc;
  std::strcpy(o.u.outside.phrase, "Clear, cooler");
  size_t n = encode(o, buf, sizeof(buf));
  p.a.on_radio(buf, n);
  CHECK(!p.pa.out.empty() && p.pa.out[0].to == kSenderGateway);  // ACKed back to the gateway
  p.run(200);
  CHECK(p.a.view().outside.valid && p.b.view().outside.valid);
  CHECK_STR(p.b.view().outside.phrase, "Clear, cooler");
  CHECK(!p.a.view().outside.heat && !p.a.view().outside.cold);
  // WeatherGate's danger cold reaches upstairs and is passed on downstairs with the reading.
  o.seq = 76;
  o.u.outside.flags = kOutsideValid | kOutsideDangerCold;
  o.u.outside.forecast = (uint8_t)Forecast::Snow;
  std::strcpy(o.u.outside.phrase, "Snow at Times, Worse Later");
  n = encode(o, buf, sizeof(buf));
  p.a.on_radio(buf, n);
  p.run(200);
  CHECK(p.a.view().outside.cold && p.b.view().outside.cold && !p.b.view().outside.heat);
  CHECK(p.b.view().outside.code == Forecast::Snow);
  CHECK_STR(p.b.view().outside.phrase, "Snow at Times, Worse Later");
  // A forecast code the panel doesn't know: the reading shows, with no forecast.
  o.seq = 75;
  o.u.outside.flags = kOutsideValid;
  o.u.outside.forecast = 42;
  n = encode(o, buf, sizeof(buf));
  p.a.on_radio(buf, n);
  p.run(200);
  CHECK(p.a.view().outside.valid && p.a.view().outside.code == Forecast::Unknown && !p.a.view().outside.cold);
  p.run(15 * 60 * 1000 + 1000);
  CHECK(!p.a.view().outside.valid && !p.b.view().outside.valid);
  CHECK_STR(p.a.view().outside.temp, "--\xC2\xB0" "F");
  // HANDOFF (2026-10-30): staleness counts from when the panel received the reading, not from the
  // reading's own time, so a reading with a time below the clock floor (a Pi clock not right yet) still
  // shows, and so does one with a time of 0 (the Pi not synced yet), for 15 minutes from its arrival.
  o.seq = 78;
  o.u.outside.time = 1000;
  n = encode(o, buf, sizeof(buf));
  p.a.on_radio(buf, n);
  p.run(200);
  CHECK(p.a.view().outside.valid);
  o.seq = 79;
  o.u.outside.time = 0;
  n = encode(o, buf, sizeof(buf));
  p.run(10 * 60 * 1000);
  p.a.on_radio(buf, n);
  p.run(14 * 60 * 1000);
  CHECK(p.a.view().outside.valid);
  CHECK(p.pa.w.valid && p.pa.w.utc > 1792627200u);  // its clock was not set from the 0
  p.run(60 * 1000 + 1000);
  CHECK(!p.a.view().outside.valid);
  // After a power cut (no coin cell, so both clocks lost), the panels take the WeatherGate's time from
  // its weather message (Mike, 2026-11-07, test_outside_clock): upstairs, and downstairs passed on.
  {
    uint32_t real = p.pa.w.utc;
    p.pa.w = WallTime();
    p.pb.w = WallTime();
    o.seq = 80;
    o.u.outside.time = 0;
    n = encode(o, buf, sizeof(buf));
    p.a.on_radio(buf, n);
    p.run(200);
    CHECK(!p.pa.w.valid);
    o.seq = 81;
    o.u.outside.time = real + 30;
    n = encode(o, buf, sizeof(buf));
    p.a.on_radio(buf, n);
    p.run(200);
    CHECK(p.pa.w.valid && p.pa.w.utc >= real + 30 && p.pa.w.utc < real + 35);
    CHECK(p.pa.w.quality == kClockNetwork);
    CHECK(p.pb.w.valid && p.pb.w.quality == kClockNetwork);
  }

  // LucyGate: out, then in.
  Msg l;
  l.type = MsgType::Lucy;
  l.sender = kSenderGateway;
  l.seq = 90;
  l.u.lucy.out = 1;
  l.u.lucy.since = 0;  // the gateway may not know the time: the panel stamps it
  n = encode(l, buf, sizeof(buf));
  p.a.on_radio(buf, n);
  p.run(200);
  CHECK(p.a.dog().out() == Dog::Lucy && p.b.dog().out() == Dog::Lucy);
  CHECK(p.a.dog().dog[0].since != 0);
  l.seq = 91;
  l.u.lucy.out = 0;
  n = encode(l, buf, sizeof(buf));
  p.a.on_radio(buf, n);
  p.run(200);
  CHECK(p.a.dog().out() == Dog::None && p.b.dog().out() == Dog::None);
}

static void test_lost_acks() {
  // The knock arrives but its ACK is lost; the reply still lands.
  Pair p;
  p.begin();
  p.run(200);
  int dropped = 0;
  p.drop = [&](bool from_a, const Msg &m) {
    if (!from_a && m.type == MsgType::Ack && m.u.ack.type == (uint8_t)MsgType::Knock && dropped == 0) {
      dropped++;
      return true;
    }
    return false;
  };
  p.a.tap_bell();
  p.run(400);
  CHECK(dropped == 1);
  CHECK(p.a.view().bell.out == BellOut::Delivered);  // the retry's ACK got through
  CHECK(p.pb.played.size() == 1);                    // and the knock chimed only once
  p.drop = nullptr;

  // Every ACK from downstairs is lost: Couldn't reach, then the answer still shows when it comes.
  Pair q;
  q.begin();
  q.run(200);
  q.cut_ba = true;
  q.a.tap_bell();
  q.run(2000);
  CHECK(q.a.view().bell.out == BellOut::CouldntReach);
  CHECK(q.b.view().banner.kind == BannerKind::Knock);
  q.run(16000);  // "Couldn't reach" clears after 15 s
  CHECK(q.a.view().bell.out == BellOut::Idle);
  q.cut_ba = false;
  q.b.answer_banner(true);
  q.run(200);
  CHECK(q.a.view().bell.out == BellOut::ComeOn);
}

static void test_floor_swap() {
  Pair p;
  p.begin();
  p.run(200);
  Settings s = p.sb;
  s.floor = Floor::Upstairs;  // a mistake: both panels now claim upstairs
  p.b.apply_settings(s);
  p.run(2000);
  CHECK(p.b.settings().floor == Floor::Upstairs);
  s.floor = Floor::Downstairs;  // put back
  p.b.apply_settings(s);
  p.run(400);
  CHECK(!p.a.view().floor[(int)Floor::Downstairs].no_signal);
  CHECK(!p.b.view().floor[(int)Floor::Upstairs].no_signal);
}

static void test_clock_unknown_is_quiet_and_dim() {
  Pair p;
  p.pa.w = WallTime();
  p.pb.w = WallTime();
  p.begin();
  p.run(200);
  p.a.tap_bell();
  p.run(200);
  CHECK(!p.pb.played.empty() && p.pb.played.back().second == 80);  // the quiet-hours cap
  CHECK(p.pb.backlight == 224);                                     // and the night level
}

static void test_sht4x() {
  using namespace tc::sht4x;
  const uint8_t beef[2] = {0xBE, 0xEF};
  CHECK(crc8(beef, 2) == 0x92);  // the datasheet's own example
  auto frame = [](uint16_t t, uint16_t h, uint8_t *f) {
    f[0] = (uint8_t)(t >> 8); f[1] = (uint8_t)t; f[2] = crc8(f, 2);
    f[3] = (uint8_t)(h >> 8); f[4] = (uint8_t)h; f[5] = crc8(f + 3, 2);
  };
  uint8_t f[6];
  frame(0x6666, 0x8000, f);  // 25.0 C (77.0 F), 56.5 %
  Reading r = decode(f);
  CHECK(r.ok && r.temp_tenths_f == 770 && r.hum_tenths == 565);
  frame(0, 0, f);  // -45 C (-49 F); humidity below 0 clamps to 0
  r = decode(f);
  CHECK(r.ok && r.temp_tenths_f == -490 && r.hum_tenths == 0);
  frame(0xFFFF, 0xFFFF, f);  // 130 C (266 F); humidity above 100 clamps to 100
  r = decode(f);
  CHECK(r.ok && r.temp_tenths_f == 2660 && r.hum_tenths == 1000);
  frame(0x6666, 0x8000, f);
  f[1] ^= 0x01;  // one bit flipped on the wire
  CHECK(!decode(f).ok);
  uint32_t sn = 0;
  frame(0x1234, 0x5678, f);
  CHECK(decode_serial(f, &sn) && sn == 0x12345678u);
  f[5] ^= 0xFF;
  CHECK(!decode_serial(f, &sn));
}

// The heat and cooling arrows (requests.h, Mike 2026-11-06). UTC offsets 0 and +3600 only.
static int soft_dings(FakePlatform &f) {
  int n = 0;
  for (auto &s : f.played) n += s.first == Sound::SoftChime;
  return n;
}

static void req_start(Pair &p) {
  p.pa.offset = p.pb.offset = 0;
  p.begin();
  p.run(400);
}

static void req_send(Pair &p, Feel f) {
  CHECK(p.b.comfort_answer(f, ask_for_feel(f), AskSent::Send));
  p.run(200);
}

static void req_jump(Pair &p, uint32_t s) {  // the clocks move on; the milliseconds don't matter here
  p.pa.w.utc += s;
  p.pb.w.utc += s;
  p.run(100);
}

// Review fixes (2026-11-06): a true tie settles the same way on both panels, and arrows sent with
// no clock get a time once the clock is set, so they age out too.
static void test_request_ties() {
  WallTime nc{false, 0, kClockNone};
  // Both panels change from the same counter with no clock: same ver, same since (0).
  RequestModel down, up;
  down.add(Ask::UrgentHeat, ReqSource::Tenant, nc);  // the tenant asks...
  RequestModel up2;
  up2.add(Ask::MildHeat, ReqSource::Tenant, nc);     // ...a different state at the same counter
  RequestState a = down.state(), b = up2.state();
  CHECK(a.ver == b.ver && a.since == b.since && !(a == b));
  RequestModel x, y;
  x.restore(a);
  y.restore(b);
  bool cx = x.apply_remote(b), cy = y.apply_remote(a);
  CHECK(cx != cy);                  // exactly one side gives way
  CHECK(x.state() == y.state());    // and they agree, so nothing bounces
  CHECK(!x.apply_remote(y.state()) && !y.apply_remote(x.state()));
  // More arrows wins a tie: better an extra arrow than a lost request.
  RequestState two = a;
  two.n = 2;
  two.a[1] = ReqArrow{(uint8_t)Ask::MildCool, (uint8_t)ReqSource::Tenant, 0};
  RequestModel z;
  z.restore(a);
  CHECK(z.apply_remote(two) && z.state().n == 2);
  RequestModel w;
  w.restore(two);
  CHECK(!w.apply_remote(a) && w.state().n == 2);

  // Stamping: an arrow sent with no clock gets the time when the clock is set, then ages out.
  RequestModel s;
  s.add(Ask::UrgentCool, ReqSource::Tenant, nc);
  CHECK(s.state().a[0].sent == 0 && s.any_urgent(1792670400u + 10 * kArrowLifeS));
  WallTime t{true, 1792670400u, kClockNetwork};
  CHECK(!s.stamp_unknown(nc));
  uint32_t v = s.state().ver;
  CHECK(s.stamp_unknown(t) && s.state().a[0].sent == t.utc && s.state().ver == v + 1);
  CHECK(!s.stamp_unknown(t));
  CHECK(s.any_urgent(t.utc + kArrowLifeS - 1) && !s.any_urgent(t.utc + kArrowLifeS));
}

static void test_requests() {
  // The arrow the survey picks.
  CHECK(ask_for_feel(Feel::MuchTooHot) == Ask::UrgentCool && ask_for_feel(Feel::TooWarm) == Ask::MildCool);
  CHECK(ask_for_feel(Feel::JustRight) == Ask::None);
  CHECK(ask_for_feel(Feel::TooCool) == Ask::MildHeat && ask_for_feel(Feel::MuchTooCold) == Ask::UrgentHeat);
  CHECK(ask_urgent(Ask::UrgentHeat) && ask_urgent(Ask::UrgentCool) && !ask_urgent(Ask::MildHeat) && !ask_urgent(Ask::MildCool));
  CHECK_STR(req_end_code(ReqEnd::Cleared), "CLEARED");
  CHECK_STR(req_end_code(ReqEnd::TakenBack), "TAKEN_BACK");
  CHECK_STR(req_end_code(ReqEnd::PushedOff), "PUSHED_OFF");
  CHECK_STR(req_end_code(ReqEnd::TimedOut), "TIMED_OUT");

  // The model: arrows are added on the right, the fourth pushes the oldest off.
  WallTime t0{true, 1792670400u, kClockManual};  // 2026-10-22 12:00 UTC
  RequestModel m;
  CHECK(!m.take_back(t0) && !m.clear(t0) && !m.dismiss_acted(t0));
  ReqArrow pushed = {};
  bool did = true;
  m.add(Ask::MildHeat, ReqSource::Tenant, t0, &pushed, &did);
  CHECK(!did && m.state().n == 1 && m.state().ver == 1 && m.state().since == t0.utc && m.state().a[0].sent == t0.utc);
  WallTime t1 = t0, t2 = t0, t3 = t0;
  t1.utc += 60; t2.utc += 120; t3.utc += 180;
  m.add(Ask::UrgentCool, ReqSource::Tenant, t1);
  CHECK(m.state().n == 2 && m.state().a[0].ask == (uint8_t)Ask::MildHeat && m.state().a[1].ask == (uint8_t)Ask::UrgentCool);
  CHECK(m.state().ver == 2 && m.state().since == t1.utc);
  m.add(Ask::MildCool, ReqSource::Predicted, t2);
  CHECK(m.state().n == 3 && m.state().a[2].source == (uint8_t)ReqSource::Predicted);
  m.add(Ask::UrgentHeat, ReqSource::Tenant, t3, &pushed, &did);
  CHECK(did && pushed.ask == (uint8_t)Ask::MildHeat && pushed.sent == t0.utc);
  CHECK(m.state().n == 3 && m.state().a[0].ask == (uint8_t)Ask::UrgentCool && m.state().a[1].ask == (uint8_t)Ask::MildCool &&
        m.state().a[2].ask == (uint8_t)Ask::UrgentHeat && m.state().ver == 4);
  ReqArrow sh[kMaxArrows];
  CHECK(m.showing(t3.utc, sh) == 3 && sh[0].ask == (uint8_t)Ask::UrgentCool && sh[2].ask == (uint8_t)Ask::UrgentHeat);
  CHECK(m.any_urgent(t3.utc));

  // Ages: an arrow shows until kArrowLifeS after it was sent; with no clock nothing ages.
  CHECK(m.showing(t1.utc + kArrowLifeS - 1, sh) == 3);
  CHECK(m.showing(t1.utc + kArrowLifeS, sh) == 2 && sh[0].ask == (uint8_t)Ask::MildCool);
  CHECK(m.showing(t3.utc + kArrowLifeS, sh) == 0 && !m.any_urgent(t3.utc + kArrowLifeS));
  CHECK(m.showing(0, sh) == 3);
  RequestModel nc;
  nc.add(Ask::MildCool, ReqSource::Tenant, WallTime());
  CHECK(nc.state().ver == 1 && nc.state().since == 0 && nc.state().a[0].sent == 0);
  CHECK(nc.showing(2000000000u, sh) == 1);  // sent with no clock: never times out
  RequestModel mild;
  mild.add(Ask::MildHeat, ReqSource::Tenant, t0);
  CHECK(!mild.any_urgent(t0.utc));
  // Adding drops arrows already past their life (they were never logged as pushed off).
  RequestModel o = m;
  WallTime late = t0;
  late.utc = t2.utc + kArrowLifeS;  // the first two have gone, the newest has not
  o.add(Ask::MildHeat, ReqSource::Tenant, late, &pushed, &did);
  CHECK(!did && o.state().n == 2 && o.state().a[0].ask == (uint8_t)Ask::UrgentHeat && o.state().a[1].ask == (uint8_t)Ask::MildHeat);

  // Taking back.
  RequestModel k = m;
  CHECK(k.take_back(t3) && k.state().n == 0 && k.state().ver == 5 && k.state().acted == 0);
  CHECK(k.state().a[0].ask == 0 && k.state().a[2].sent == 0);  // unused slots are zero
  CHECK(!k.take_back(t3) && k.state().ver == 5);
  CHECK(!k.clear(t3));
  RequestModel gone = m;
  CHECK(!gone.take_back(WallTime{true, t3.utc + kArrowLifeS * 2, kClockManual}));  // all aged out: nothing showing

  // Mike clears them: all go and the message shows for 30 minutes, or until the tenant taps it.
  RequestModel c = m;
  WallTime tc{true, t3.utc + 600, kClockManual};
  CHECK(c.clear(tc) && c.state().n == 0 && c.state().acted == tc.utc && !c.state().acted_dismissed && c.state().ver == 5);
  CHECK(c.acted_showing(tc.utc) && c.acted_showing(tc.utc + kActedShowS - 1) && !c.acted_showing(tc.utc + kActedShowS));
  CHECK(c.acted_showing(0));
  CHECK(!c.clear(tc));
  RequestModel dm = c;
  CHECK(dm.dismiss_acted(tc) && dm.state().acted_dismissed && !dm.acted_showing(tc.utc) && !dm.dismiss_acted(tc));
  c.add(Ask::MildHeat, ReqSource::Tenant, tc);  // a new request puts the message away
  CHECK(c.state().acted == 0 && !c.acted_showing(tc.utc));
  RequestModel ncl;
  ncl.add(Ask::UrgentHeat, ReqSource::Tenant, WallTime());
  CHECK(ncl.clear(WallTime()) && ncl.state().acted == 1 && ncl.acted_showing(3000000000u) && ncl.acted_showing(0));

  // apply_remote: the higher counter wins, a tie goes to the later change, a true tie keeps ours.
  RequestState hi = m.state(), lo = m.state();
  hi.ver++; hi.n = 1;
  lo.ver--;
  RequestModel r;
  r.restore(m.state());
  CHECK(!r.apply_remote(m.state()) && !r.apply_remote(lo));
  CHECK(r.apply_remote(hi) && r.state().ver == hi.ver && r.state().n == 1 && r.state().a[1].ask == 0);
  RequestState x = m.state(), y = x;
  y.since++;
  y.acted = 77;
  RequestModel r1, r2, r3, r4;
  r1.restore(x);
  CHECK(r1.apply_remote(y) && r1.state().acted == 77);
  r2.restore(y);
  CHECK(!r2.apply_remote(x) && r2.state().acted == 77);
  y.since = x.since;
  r3.restore(x);
  r4.restore(y);
  // A full tie (same counter, same time): both panels settle on the same one, here the later acted.
  CHECK(r3.apply_remote(y) && !r4.apply_remote(x) && r3.state().acted == 77 && r4.state().acted == 77);
  CHECK(x == m.state() && !(x == y));

  // The file's lines (local times; offset 0 here, and +3600).
  char line[kComfortLineMax];
  ReqArrow a1 = {(uint8_t)Ask::UrgentCool, (uint8_t)ReqSource::Tenant, 1792670400u};
  CHECK(request_csv_line(a1, ReqEnd::Cleared, 1792670400u + 1800, 0, line, sizeof(line)) > 0);
  CHECK_STR(line, "2026-10-22,12:00,MIDDAY,URGENT_COOL,TENANT,CLEARED,2026-10-22,12:30,30\n");
  CHECK(request_csv_line(a1, ReqEnd::Cleared, 1792670400u + 1800, 3600, line, sizeof(line)) > 0);
  CHECK_STR(line, "2026-10-22,13:00,MIDDAY,URGENT_COOL,TENANT,CLEARED,2026-10-22,13:30,30\n");
  ReqArrow a2 = {(uint8_t)Ask::MildHeat, (uint8_t)ReqSource::Predicted, 1792627200u + 23 * 3600};
  request_csv_line(a2, ReqEnd::TimedOut, a2.sent + 3 * 3600 + 59, 0, line, sizeof(line));
  CHECK_STR(line, "2026-10-22,23:00,NIGHT,MILD_HEAT,PREDICTED,TIMED_OUT,2026-10-23,02:00,180\n");
  ReqArrow a3 = {(uint8_t)Ask::UrgentHeat, (uint8_t)ReqSource::Tenant, 0};
  request_csv_line(a3, ReqEnd::PushedOff, 0, 0, line, sizeof(line));
  CHECK_STR(line, ",,,URGENT_HEAT,TENANT,PUSHED_OFF,,,\n");
  request_csv_line(a3, ReqEnd::TakenBack, 1792670400u, 0, line, sizeof(line));  // sent with no clock: no wait
  CHECK_STR(line, ",,,URGENT_HEAT,TENANT,TAKEN_BACK,2026-10-22,12:00,\n");
  request_csv_line(a1, ReqEnd::TakenBack, 0, 0, line, sizeof(line));
  CHECK_STR(line, "2026-10-22,12:00,MIDDAY,URGENT_COOL,TENANT,TAKEN_BACK,,,\n");
  CHECK(request_csv_line(a1, ReqEnd::Cleared, 1792670400u, 0, line, 10) == 0);
  CHECK_STR(kRequestsFile, "requests.csv");
  CHECK_STR(kRequestsCsvHeader, "sent_date,sent_time,part_of_day,arrow,source,ended,ended_date,ended_time,waited_min\n");
  CHECK(strlen(kRequestsCsvHeader) < kComfortLineMax);

  // The wire: 5 + 32 bytes, little-endian.
  Msg wm;
  wm.type = MsgType::Requests;
  wm.sender = kSenderDownstairs;
  wm.seq = 0x2345;
  RequestState &q = wm.u.req;
  q.n = 2;
  q.a[0] = ReqArrow{(uint8_t)Ask::MildHeat, (uint8_t)ReqSource::Tenant, 0x6ADF03D0u};
  q.a[1] = ReqArrow{(uint8_t)Ask::UrgentCool, (uint8_t)ReqSource::Predicted, 5};
  q.a[2] = ReqArrow{0, 0, 0};
  q.acted = 0x11223344u;
  q.acted_dismissed = true;
  q.since = 0x01020304u;
  q.ver = 9;
  uint8_t buf[64];
  size_t n = encode(wm, buf, sizeof(buf));
  CHECK(n == 37);
  CHECK(buf[1] == 13 && buf[5] == 2 && buf[6] == 2 && buf[7] == 0 && buf[8] == 0xD0 && buf[11] == 0x6A);
  CHECK(buf[12] == 4 && buf[13] == 1 && buf[14] == 5 && buf[18] == 0 && buf[24] == 0x44 && buf[27] == 0x11);
  CHECK(buf[28] == 1 && buf[29] == 4 && buf[32] == 1 && buf[33] == 9 && buf[36] == 0);
  Msg dm2;
  CHECK(decode(buf, n, dm2) && dm2.type == MsgType::Requests && dm2.sender == kSenderDownstairs && dm2.seq == 0x2345);
  CHECK(dm2.u.req == q && dm2.u.req.a[2].ask == 0);
  CHECK(encode(wm, buf, 20) == 0);
  CHECK(!decode(buf, n - 1, dm2));
  uint8_t longer[64];
  memcpy(longer, buf, n);
  longer[n] = 0;
  CHECK(!decode(longer, n + 1, dm2));
  auto bad = [&](size_t at, uint8_t v) {
    uint8_t c2[64];
    memcpy(c2, buf, n);
    c2[at] = v;
    return decode(c2, n, dm2);
  };
  CHECK(!bad(5, 4));    // more than 3 arrows
  CHECK(!bad(6, 0));    // a used slot with no arrow
  CHECK(!bad(6, 5));    // an arrow that doesn't exist
  CHECK(!bad(7, 2));    // a source that doesn't exist
  CHECK(!bad(28, 2));   // dismissed is 0 or 1
  CHECK(bad(18, 7));    // the unused third slot is ignored
  wm.u.req.n = 4;
  CHECK(encode(wm, buf, sizeof(buf)) == 0);
  wm.u.req.n = 0;
  q.a[0] = ReqArrow{0, 0, 0};
  q.a[1] = ReqArrow{0, 0, 0};
  CHECK(encode(wm, buf, sizeof(buf)) == 37 && decode(buf, 37, dm2) && dm2.u.req.n == 0);
  CHECK_STR(msg_name(MsgType::Requests), "REQUESTS");
  {  // latest wins, like MAIL: a second copy replaces the first in the queue
    RetryQueue rq;
    wm.u.req.n = 0;
    rq.push(wm, kSenderDownstairs, 0);
    rq.push(wm, kSenderDownstairs, 10);
    CHECK(rq.pending(MsgType::Requests) == 1);
  }

  // Two panels: SEND from downstairs. An urgent arrow shows on both, upstairs wakes and dings.
  {
    Pair p(1792670400u);
    req_start(p);
    p.a.choose_status(Status::Busy);  // it dings while Mike is Busy too
    p.run(200);
    int before = soft_dings(p.pa);
    req_send(p, Feel::MuchTooHot);
    CHECK(p.a.requests().n == 1 && p.b.requests().n == 1 && p.pa.saved_req.n == 1 && p.pb.saved_req.n == 1);
    const View &va = p.a.view(), &vb = p.b.view();
    CHECK(va.req.n == 1 && va.req.ask[0] == Ask::UrgentCool && va.req.source[0] == ReqSource::Tenant && va.req.flash && !va.req.acted);
    CHECK(vb.req.n == 1 && vb.req.ask[0] == Ask::UrgentCool && vb.req.flash && !vb.req.acted);
    CHECK(soft_dings(p.pa) == before + 1);  // at once, at Busy's soft chime level
    CHECK(p.pa.played.back().first == Sound::SoftChime && p.pa.played.back().second == p.sa.volume - p.sa.soft_chime_drop);
    CHECK(soft_dings(p.pb) == 0);
    p.run(14000);
    CHECK(soft_dings(p.pa) == before + 1);
    p.run(1500);
    CHECK(soft_dings(p.pa) == before + 2);  // every 15 s
    p.run(15000);
    CHECK(soft_dings(p.pa) == before + 3);
    CHECK(p.pb.lines.size() == 1 && p.pb.lines[0].find(",MUCH_TOO_HOT,URGENT_COOL,SEND,") != std::string::npos);
    CHECK(p.pa.req_lines.empty() && p.pb.req_lines.empty());  // nothing has ended yet; upstairs never writes it
    // Mike taps: the dinging stops, and downstairs says so.
    int d = soft_dings(p.pa);
    CHECK(p.a.tap_requests());
    p.run(300);
    CHECK(p.a.requests().n == 0 && p.b.requests().n == 0 && va.req.n == 0 && vb.req.n == 0 && !va.req.flash);
    CHECK(vb.req.acted && !va.req.acted);
    p.run(20000);
    CHECK(soft_dings(p.pa) == d);
    CHECK(p.pb.req_lines.size() == 1 && p.pb.req_lines[0].find("URGENT_COOL,TENANT,CLEARED,2026-10-22,12:") != std::string::npos);
    CHECK(p.pb.req_lines[0].find("2026-10-22,12:00,MIDDAY,URGENT_COOL") == 0);
    CHECK(!p.a.tap_requests());  // nothing left
    // The message stays 30 minutes.
    req_jump(p, 1700);
    CHECK(vb.req.acted);
    req_jump(p, 120);
    CHECK(!vb.req.acted);
  }
  // The tenant taps the message away.
  {
    Pair p(1792670400u);
    req_start(p);
    req_send(p, Feel::TooWarm);
    CHECK(p.a.tap_requests());
    p.run(300);
    CHECK(p.b.view().req.acted);
    CHECK(!p.a.tap_acted());  // upstairs has no message
    CHECK(p.b.tap_acted());
    p.run(300);
    CHECK(!p.b.view().req.acted && p.a.requests().acted_dismissed && p.pb.saved_req.acted_dismissed);
    CHECK(!p.b.tap_acted());
    CHECK(!p.a.view().req.acted);
  }
  // Mild arrows: no sound, and no flashing.
  {
    Pair p(1792670400u);
    req_start(p);
    req_send(p, Feel::TooCool);
    CHECK(p.a.view().req.n == 1 && p.a.view().req.ask[0] == Ask::MildHeat && !p.a.view().req.flash && !p.b.view().req.flash);
    p.run(40000);
    CHECK(soft_dings(p.pa) == 0 && p.pa.played.empty());
    req_send(p, Feel::TooWarm);
    p.run(20000);
    CHECK(p.a.view().req.n == 2 && p.a.view().req.ask[1] == Ask::MildCool && p.pa.played.empty());
  }
  // The tenant takes the arrows back: gone at once from both panels, nothing said.
  {
    Pair p(1792670400u);
    req_start(p);
    req_send(p, Feel::MuchTooCold);
    req_send(p, Feel::TooWarm);
    CHECK(p.a.view().req.n == 2 && p.a.view().req.flash);
    int d = soft_dings(p.pa);
    CHECK(p.b.tap_requests());
    p.run(300);
    CHECK(p.a.requests().n == 0 && p.b.requests().n == 0 && p.a.view().req.n == 0 && p.b.view().req.n == 0);
    CHECK(!p.a.view().req.acted && !p.b.view().req.acted && p.a.requests().acted == 0);
    CHECK(p.pb.req_lines.size() == 2 && p.pb.req_lines[0].find("URGENT_HEAT,TENANT,TAKEN_BACK,") != std::string::npos &&
          p.pb.req_lines[1].find("MILD_COOL,TENANT,TAKEN_BACK,") != std::string::npos);
    CHECK(!p.b.tap_requests());
    p.run(20000);
    CHECK(soft_dings(p.pa) == d);
  }
  // A fourth arrow pushes the oldest off.
  {
    Pair p(1792670400u);
    req_start(p);
    req_send(p, Feel::MuchTooHot);
    req_send(p, Feel::TooWarm);
    req_send(p, Feel::TooCool);
    CHECK(p.pb.req_lines.empty());
    req_send(p, Feel::MuchTooCold);
    for (const View *v : {&p.a.view(), &p.b.view()}) {
      CHECK(v->req.n == 3 && v->req.ask[0] == Ask::MildCool && v->req.ask[1] == Ask::MildHeat && v->req.ask[2] == Ask::UrgentHeat);
    }
    CHECK(p.pb.req_lines.size() == 1 && p.pb.req_lines[0].find("URGENT_COOL,TENANT,PUSHED_OFF,") != std::string::npos);
    CHECK(p.pb.lines.size() == 4 && p.pa.lines.size() == 4);  // every survey answer was kept
  }
  // Arrows nobody clears go after 4 hours, and downstairs logs each once.
  {
    Pair p(1792670400u);
    req_start(p);
    req_send(p, Feel::TooCool);
    req_jump(p, kArrowLifeS - 100);
    CHECK(p.a.view().req.n == 1 && p.b.view().req.n == 1 && p.pb.req_lines.empty());
    req_jump(p, 200);
    CHECK(p.a.view().req.n == 0 && p.b.view().req.n == 0 && !p.a.view().req.flash);
    CHECK(p.pb.req_lines.size() == 1);
    CHECK_STR(p.pb.req_lines.empty() ? "" : p.pb.req_lines[0].c_str(), "2026-10-22,12:00,MIDDAY,MILD_HEAT,TENANT,TIMED_OUT,2026-10-22,16:00,240\n");
    p.run(2000);
    CHECK(p.pb.req_lines.size() == 1 && p.pa.req_lines.empty());
    CHECK(!p.a.tap_requests() && !p.b.tap_requests());  // nothing showing
  }
  // Survey answers that send nothing: no arrow, nothing on the radio for it.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.b.comfort_answer(Feel::JustRight));
    CHECK(p.b.comfort_answer(Feel::TooWarm, Ask::MildCool, AskSent::Cancel));
    CHECK(p.b.comfort_answer(Feel::TooCool, Ask::MildHeat, AskSent::Blank));
    CHECK(p.b.comfort_answer(Feel::TooCool, Ask::None, AskSent::Send));
    p.run(300);
    CHECK(p.b.requests().ver == 0 && p.a.requests().ver == 0 && p.pb.lines.size() == 4 && p.pb.req_saves == 0);
    CHECK(!p.a.comfort_answer(Feel::TooWarm, Ask::MildCool, AskSent::Send) && p.a.requests().ver == 0);
  }
  // Night, with the screen off overnight: no ding, but the screen lights while an urgent arrow shows.
  {
    Pair p(1792627200u + 23 * 3600);  // 23:00
    p.sa.screen_off_overnight = true;
    p.sb.screen_off_overnight = true;
    req_start(p);
    CHECK(p.pa.backlight == kBacklightOff);
    req_send(p, Feel::MuchTooCold);
    CHECK(p.a.view().req.n == 1 && p.a.view().req.flash);
    CHECK(p.pa.backlight != kBacklightOff && soft_dings(p.pa) == 0);
    p.run(35000);
    CHECK(p.pa.backlight != kBacklightOff && soft_dings(p.pa) == 0 && p.pa.played.empty());
    // Mild: the screen stays dark.
    Pair q(1792627200u + 23 * 3600);
    q.sa.screen_off_overnight = true;
    q.sb.screen_off_overnight = true;
    req_start(q);
    req_send(q, Feel::TooWarm);
    q.run(1000);
    CHECK(q.a.view().req.n == 1 && q.pa.backlight == kBacklightOff && q.pa.played.empty());
  }
  // A restart: the other panel brings the arrows back, as for mail.
  {
    Pair p(1792670400u);
    req_start(p);
    req_send(p, Feel::MuchTooHot);
    Pair q(1792670400u);
    q.pa.offset = q.pb.offset = 0;
    q.b.restore_requests(p.b.requests());
    q.begin();
    q.run(500);
    CHECK(q.a.requests().n == 1 && q.a.view().req.n == 1 && q.a.view().req.ask[0] == Ask::UrgentCool);
    CHECK(q.pa.saved_req.n == 1 && q.pb.req_lines.empty());
  }
}

// ---------------------------------------------------------------------------------------------
// Tile messages (Mike, 2026-11-07): the model, the NOTE message, and two panels.

static NoteState note_state(const char *text, uint32_t set, uint32_t until, uint32_t since, uint32_t ver) {
  NoteState s = {};
  snprintf(s.text, sizeof(s.text), "%s", text);
  s.set = set; s.until = until; s.since = since; s.ver = ver;
  return s;
}

static void test_note() {
  const WallTime t0{true, 1792670400u, kClockManual};
  const WallTime none{false, 0, kClockNone};

  // The durations: 15 min to 4 hr, 4 hr last (the default), and nothing longer.
  CHECK(kNoteDurations == 5 && kNoteDurationS[0] == 900 && kNoteDurationS[1] == 1800 && kNoteDurationS[2] == 3600 &&
        kNoteDurationS[3] == 7200 && kNoteDurationS[4] == 14400 && kNoteMaxS == 14400);

  // set: trims, stamps, counts.
  NoteModel m;
  CHECK(!m.showing(0) && !m.clear(t0));
  m.set("  Need the washer by 3  ", 1800, t0);
  CHECK_STR(m.state().text, "Need the washer by 3");
  CHECK(m.state().set == t0.utc && m.state().until == t0.utc + 1800 && m.state().since == t0.utc && m.state().ver == 1);
  CHECK(m.showing(t0.utc) && m.showing(t0.utc + 1799) && !m.showing(t0.utc + 1800) && !m.showing(t0.utc + 99999));
  CHECK(m.showing(0));  // no clock: nothing ages out
  // A duration over the most is capped; zero is the most.
  m.set("a", 99 * 3600, t0);
  CHECK(m.state().until == t0.utc + kNoteMaxS && m.state().ver == 2);
  m.set("a", 0, t0);
  CHECK(m.state().until == t0.utc + kNoteMaxS);
  // No clock: until 0, stays until tapped away.
  m.set("b", 900, none);
  CHECK(m.state().set == 0 && m.state().until == 0 && m.state().since == 0 && m.showing(2000000000u));
  // Long text is cut at kNoteTextMax bytes.
  std::string longt(100, 'x');
  m.set(longt.c_str(), 900, t0);
  CHECK(strlen(m.state().text) == (size_t)kNoteTextMax);
  // A UTF-8 character is never cut: a 2-byte character that would straddle the limit is dropped whole.
  std::string u63(63, 'y');
  u63 += "\xC3\xA9tail";
  m.set(u63.c_str(), 900, t0);
  CHECK(strlen(m.state().text) == 63 && m.state().text[62] == 'y');
  std::string u62(62, 'y');
  u62 += "\xE2\x82\xAC!";  // a 3-byte character starting at byte 62 would end at 65
  m.set(u62.c_str(), 900, t0);
  CHECK(strlen(m.state().text) == 62);
  std::string u64(64, 'y');
  u64 += "\xC3\xA9";  // the character starts right at the limit: all 64 letters stay
  m.set(u64.c_str(), 900, t0);
  CHECK(strlen(m.state().text) == 64);
  std::string u60(60, 'y');
  u60 += "\xC3\xA9\xC3\xA9\xC3\xA9";  // three 2-byte characters: two fit (up to byte 64), the third doesn't
  m.set(u60.c_str(), 900, t0);
  CHECK(strlen(m.state().text) == 64 && (uint8_t)m.state().text[63] == 0xA9);
  // Spaces left at the cut are dropped too.
  std::string sp(60, 'z');
  sp += "     tail";
  m.set(sp.c_str(), 900, t0);
  CHECK(strlen(m.state().text) == 60);
  // Empty (or all spaces) clears.
  m.set("c", 900, t0);
  uint32_t v = m.state().ver;
  m.set("   ", 900, t0);
  CHECK(m.state().text[0] == 0 && m.state().ver == v + 1 && !m.showing(t0.utc));
  m.set("c", 900, t0);
  CHECK(m.clear(t0) && m.state().text[0] == 0 && !m.clear(t0));
  // An expired message is dropped quietly: clear reports false (nothing showed).
  m.set("d", 900, t0);
  WallTime late = t0;
  late.utc += 5000;
  v = m.state().ver;
  CHECK(!m.clear(late) && m.state().text[0] == 0 && m.state().ver == v);

  // apply_remote: the higher counter, then the later change, then the longer text, then the bigger bytes.
  NoteModel r;
  r.restore(note_state("one", 10, 100, 10, 5));
  CHECK(!r.apply_remote(note_state("one", 10, 100, 10, 5)));  // the same: nothing
  CHECK(!r.apply_remote(note_state("zzz", 10, 100, 99, 4)));  // a lower counter never wins
  CHECK(r.apply_remote(note_state("two", 20, 200, 1, 6)) && r.state().ver == 6);
  CHECK(r.apply_remote(note_state("three", 20, 200, 8, 6)) && !strcmp(r.state().text, "three"));  // the later change
  CHECK(!r.apply_remote(note_state("old", 20, 200, 7, 6)));
  CHECK(r.apply_remote(note_state("three4", 20, 200, 8, 6)));  // same counter and time: the longer text
  CHECK(!r.apply_remote(note_state("three", 20, 200, 8, 6)));
  CHECK(r.apply_remote(note_state("three5", 20, 200, 8, 6)));  // same length: the larger bytes
  CHECK(!r.apply_remote(note_state("three4", 20, 200, 8, 6)));
  NoteModel x, y;  // a true tie settles the same on both panels
  x.restore(note_state("aa", 1, 2, 3, 4));
  y.restore(note_state("ab", 1, 2, 3, 4));
  bool cx = x.apply_remote(y.state()), cy = y.apply_remote(x.state());
  CHECK(cx != cy && x.state() == y.state());
  NoteModel clr;  // a clear from the owner (higher counter, empty text) arrives like any change
  clr.restore(note_state("up", 10, 100, 10, 5));
  CHECK(clr.apply_remote(note_state("", 0, 0, 12, 6)) && !clr.showing(0));

  // The NOTE message.
  Msg wm;
  wm.type = MsgType::Note;
  wm.sender = kSenderUpstairs;
  wm.seq = 0x0102;
  wm.u.note.floor = 1;
  wm.u.note.st = note_state("Dinner at 6", 0x11223344u, 0x11224444u, 0x11223355u, 9);
  uint8_t buf[kMaxMsg];
  size_t n = encode(wm, buf, sizeof(buf));
  CHECK(n == 5 + 1 + 1 + 11 + 16);
  CHECK(buf[1] == 16 && buf[5] == 1 && buf[6] == 11);
  Msg d;
  CHECK(decode(buf, n, d) && d.type == MsgType::Note && d.u.note.floor == 1 && wm.u.note.st == d.u.note.st &&
        !strcmp(d.u.note.st.text, "Dinner at 6"));
  CHECK_STR(msg_name(MsgType::Note), "NOTE");
  wm.u.note.st = note_state("", 0, 0, 5, 10);  // an empty text is a clear and still travels
  n = encode(wm, buf, sizeof(buf));
  CHECK(n == 5 + 2 + 16 && decode(buf, n, d) && d.u.note.st.text[0] == 0 && d.u.note.st.ver == 10);
  std::string full(kNoteTextMax, 'q');
  wm.u.note.st = note_state(full.c_str(), 1, 2, 3, 4);
  n = encode(wm, buf, sizeof(buf));
  CHECK(n == 5 + 2 + kNoteTextMax + 16 && decode(buf, n, d) && strlen(d.u.note.st.text) == (size_t)kNoteTextMax);
  // Rejected: wrong floor, text too long, NUL in the text, wrong length.
  wm.u.note.st = note_state("Dinner at 6", 1, 2, 3, 4);
  n = encode(wm, buf, sizeof(buf));
  uint8_t bad[kMaxMsg];
  memcpy(bad, buf, n);
  bad[5] = 2;
  CHECK(!decode(bad, n, d));
  memcpy(bad, buf, n);
  bad[6] = kNoteTextMax + 1;
  CHECK(!decode(bad, n, d));
  memcpy(bad, buf, n);
  bad[8] = 0;
  CHECK(!decode(bad, n, d));
  CHECK(!decode(buf, n - 1, d) && !decode(buf, 5 + 1, d));
  memcpy(bad, buf, n);
  bad[n] = 0;
  CHECK(!decode(bad, n + 1, d));  // a byte too many
  wm.u.note.floor = 2;
  CHECK(encode(wm, buf, sizeof(buf)) == 0);
  // Latest wins per floor: a newer NOTE for a floor replaces a pending one, not the other floor's.
  {
    RetryQueue rq;
    Msg a1, a2, b1;
    a1.type = a2.type = b1.type = MsgType::Note;
    a1.u.note.floor = a2.u.note.floor = 0;
    b1.u.note.floor = 1;
    a1.u.note.st = note_state("one", 1, 2, 3, 1);
    a2.u.note.st = note_state("two", 1, 2, 3, 2);
    b1.u.note.st = note_state("other", 1, 2, 3, 1);
    rq.push(a1, kSenderUpstairs, 0);
    rq.push(a2, kSenderUpstairs, 10);
    CHECK(rq.pending(MsgType::Note) == 1);
    rq.push(b1, kSenderUpstairs, 20);
    CHECK(rq.pending(MsgType::Note) == 2);
  }

  const int up = (int)Floor::Upstairs, dn = (int)Floor::Downstairs;

  // Two panels: a message shows on its own floor's tile on both panels, and goes the same way.
  {
    Pair p(1792670400u);
    req_start(p);
    const View &va = p.a.view(), &vb = p.b.view();
    CHECK(!va.floor[up].note && !va.floor[dn].note && !vb.floor[up].note && !vb.floor[dn].note);
    int saves = p.pa.note_saves;
    CHECK(p.a.set_note("Need the washer by 3", 3600));
    p.run(200);
    CHECK(va.floor[up].note && !strcmp(va.floor[up].note_text, "Need the washer by 3") && !va.floor[dn].note);
    CHECK(vb.floor[up].note && !strcmp(vb.floor[up].note_text, "Need the washer by 3") && !vb.floor[dn].note);
    CHECK(p.pa.note_saves == saves + 1 && p.pb.note_saves >= 1);
    CHECK(!strcmp(p.pa.saved_notes[0].text, "Need the washer by 3") && !strcmp(p.pb.saved_notes[0].text, "Need the washer by 3"));
    CHECK(p.pa.saved_notes[1].text[0] == 0);
    CHECK(!strcmp(p.a.note(Floor::Upstairs).text, "Need the washer by 3") && !strcmp(p.b.note(Floor::Upstairs).text, "Need the washer by 3"));
    CHECK(p.a.set_note("Need the washer by 3", 3600) && p.a.note(Floor::Upstairs).ver == 2);  // a rewrite counts as a change
    p.run(200);
    // The other floor writes its own; each shows on its own tile on both panels.
    CHECK(p.b.set_note("Back at 5", 1800));
    p.run(200);
    CHECK(va.floor[dn].note && !strcmp(va.floor[dn].note_text, "Back at 5") && va.floor[up].note);
    CHECK(vb.floor[dn].note && !strcmp(vb.floor[dn].note_text, "Back at 5") && vb.floor[up].note);
    CHECK(p.b.clear_note());
    p.run(200);
    CHECK(!va.floor[dn].note && !vb.floor[dn].note && va.floor[up].note && vb.floor[up].note);  // only that floor
    CHECK(!p.b.clear_note());  // nothing to clear now
    // A tap on the line by the floor that wrote it clears it on both.
    CHECK(p.a.clear_note());
    p.run(200);
    CHECK(!va.floor[up].note && !vb.floor[up].note && p.pb.saved_notes[0].text[0] == 0 && p.pa.saved_notes[0].text[0] == 0);
    // Empty text clears too.
    CHECK(p.a.set_note("hello", 900));
    p.run(200);
    CHECK(vb.floor[up].note);
    CHECK(p.a.set_note("   ", 900));
    p.run(200);
    CHECK(!va.floor[up].note && !vb.floor[up].note);
    // (A NOTE from upstairs about downstairs' own floor is upstairs' copy of it: with a higher counter
    // it is taken back, see test_note_recovery_and_acted.)
    // Expiry hides it on both, each by its own clock (nothing is sent when it times out).
    CHECK(p.a.set_note("Short one", 900));
    p.run(200);
    CHECK(va.floor[up].note && vb.floor[up].note);
    req_jump(p, 880);
    CHECK(va.floor[up].note && vb.floor[up].note);
    req_jump(p, 30);
    CHECK(!va.floor[up].note && !vb.floor[up].note);
  }

  // Upstairs clearing the arrows ("Action taken on temperature") clears its message; downstairs
  // then shows the acted text in that spot.
  {
    Pair p(1792670400u);
    req_start(p);
    const View &va = p.a.view(), &vb = p.b.view();
    CHECK(p.a.set_note("Heat is on", 7200));
    CHECK(p.b.set_note("Out until 4", 7200));
    p.run(200);
    CHECK(vb.floor[up].note && va.floor[up].note);
    req_send(p, Feel::TooCool);
    CHECK(vb.floor[up].note && !vb.req.acted);  // the arrow alone changes nothing
    CHECK(p.a.tap_requests());
    p.run(300);
    CHECK(!va.floor[up].note && !vb.floor[up].note && vb.req.acted);
    CHECK(p.a.note(Floor::Upstairs).text[0] == 0 && p.b.note(Floor::Upstairs).text[0] == 0);
    CHECK(p.pa.saved_notes[0].text[0] == 0 && p.pb.saved_notes[0].text[0] == 0);
    CHECK(vb.floor[dn].note && va.floor[dn].note);  // downstairs' own message is untouched
    // Tapping with no arrows leaves a message alone.
    CHECK(p.a.set_note("Another", 3600));
    p.run(200);
    CHECK(!p.a.tap_requests());
    CHECK(va.floor[up].note);
  }

  // Downstairs: a message written after the arrows were cleared (Mike, 2026-11-07) shows in place of the
  // acted text, which stays for messages older than the acted time (test_note_recovery_and_acted).
  {
    Pair p(1792670400u);
    req_start(p);
    const View &vb = p.b.view();
    req_send(p, Feel::TooWarm);
    CHECK(p.a.tap_requests());
    p.run(200);
    CHECK(vb.req.acted && !vb.floor[up].note);
    req_jump(p, 5);
    CHECK(p.a.set_note("After the clear", 3600));
    p.run(200);
    CHECK(!vb.req.acted && vb.floor[up].note && !strcmp(vb.floor[up].note_text, "After the clear"));
    CHECK(p.b.tap_acted());
    p.run(200);
    CHECK(!vb.req.acted && vb.floor[up].note && !strcmp(vb.floor[up].note_text, "After the clear"));
  }

  // A restart keeps both messages (restore_notes), and a panel that restarts without them gets its
  // peer's at boot, as MAIL does.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.a.set_note("Kept", 7200));
    CHECK(p.b.set_note("Mine", 7200));
    p.run(200);
    FakePlatform pa2;
    pa2.w = p.pa.w;
    pa2.mono = p.pa.mono;
    pa2.offset = 0;
    pa2.rnd = 4242;
    Controller a2(pa2);
    a2.restore_notes(p.pa.saved_notes);
    a2.begin(p.sa, OwnState(), DogState());
    a2.tick();
    CHECK(a2.view().floor[up].note && !strcmp(a2.view().floor[up].note_text, "Kept"));
    CHECK(!strcmp(a2.note(Floor::Downstairs).text, "Mine"));  // kept; its tile shows it once downstairs is heard
    FakePlatform pb2;
    pb2.w = p.pb.w;
    pb2.mono = p.pb.mono;
    pb2.offset = 0;
    pb2.rnd = 999;
    Controller b2(pb2);
    NoteState lost[2] = {};
    b2.restore_notes(lost);  // its flash lost the messages
    b2.begin(p.sb, OwnState(), DogState());
    for (int i = 0; i < 60; i++) {
      p.pa.mono += 20; pb2.mono += 20;
      p.a.tick(); b2.tick();
      for (int r = 0; r < 4; r++) {
        std::vector<Sent> oa, ob;
        oa.swap(p.pa.out); ob.swap(pb2.out);
        for (auto &s : oa) b2.on_radio(s.bytes.data(), s.bytes.size());
        for (auto &s : ob) p.a.on_radio(s.bytes.data(), s.bytes.size());
      }
    }
    CHECK(b2.view().floor[up].note && !strcmp(b2.view().floor[up].note_text, "Kept"));
    CHECK(pb2.saved_notes[0].text[0] == 'K');
    CHECK(!strcmp(b2.note(Floor::Downstairs).text, "Mine") && pb2.saved_notes[1].text[0] == 'M');  // its own floor's, from upstairs' copy
  }

  // After No signal: a message changed during the outage arrives when the panel is heard again.
  {
    Pair p(1792670400u);
    req_start(p);
    p.cut = true;
    CHECK(p.a.set_note("During the outage", 7200));
    p.run(3000);
    p.a.choose_status(Status::Busy);
    p.run(1700);
    CHECK(p.a.peer_no_signal());
    CHECK(!p.b.view().floor[up].note);
    p.cut = false;
    p.b.choose_status(Status::Away);
    p.run(500);
    CHECK(!p.a.peer_no_signal());
    CHECK(p.b.view().floor[up].note && !strcmp(p.b.view().floor[up].note_text, "During the outage"));
  }

  // A floor that has no signal keeps its "last seen" line: no message over it.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.b.set_note("Out back", 7200));
    p.run(200);
    CHECK(p.a.view().floor[dn].note);
    p.cut = true;
    p.a.choose_status(Status::Busy);
    p.run(1700);
    CHECK(p.a.peer_no_signal() && !p.a.view().floor[dn].note && p.a.view().floor[dn].no_signal);
  }
}

// ---------------------------------------------------------------------------------------------
// The outside history (Mike, 2026-11-07): the file, parsing it back, the hourly forecast, the
// quarter-hour sampling, the WeatherGate's time, UPDATE from the gateway, and the note recovery.

static void test_outside_log() {
  char name[kOutsideNameLen];
  const uint32_t base = (uint32_t)seconds_from_civil(2026, 10, 22, 0, 0, 0);  // 2026-10-22 00:00 UTC
  outside_file_name(base, 0, name);
  CHECK_STR(name, "o20261022.csv");
  CHECK(strlen(name) == 13);
  outside_file_name(base - 1, 0, name);  // the last second of the day before
  CHECK_STR(name, "o20261021.csv");
  // The date is the local one: +1 h moves the day edge an hour earlier in UTC.
  outside_file_name(base - 3600, 3600, name);
  CHECK_STR(name, "o20261022.csv");
  outside_file_name(base - 3601, 3600, name);
  CHECK_STR(name, "o20261021.csv");
  outside_file_name((uint32_t)seconds_from_civil(2026, 12, 31, 23, 59, 59), 0, name);
  CHECK_STR(name, "o20261231.csv");
  outside_file_name((uint32_t)seconds_from_civil(2026, 12, 31, 23, 59, 59) + 1, 0, name);  // year edge
  CHECK_STR(name, "o20270101.csv");
  outside_file_name((uint32_t)seconds_from_civil(2028, 2, 29, 12, 0, 0), 3600, name);  // a leap day
  CHECK_STR(name, "o20280229.csv");
  outside_file_name_day((int32_t)days_from_civil(2026, 11, 7), name);
  CHECK_STR(name, "o20261107.csv");
  outside_file_name_day((int32_t)days_from_civil(2027, 1, 1) - 1, name);
  CHECK_STR(name, "o20261231.csv");

  // The CSV line: local date and time, the UTC second, one decimal.
  uint32_t t0 = (uint32_t)seconds_from_civil(2026, 11, 7, 13, 45, 0);
  OutsideSample s = {t0, 3600, 523, 64, 3, 0};
  char line[96];
  size_t n = outside_csv_line(s, line, sizeof(line));
  std::string want = "2026-11-07,14:45," + std::to_string(t0) + ",52.3,64,3,0\n";
  CHECK(n == want.size() && want == line);
  CHECK_STR(kOutsideCsvHeader, "date,time,utc,temp_f,rh,forecast,flags\n");
  s.offset = 0;
  s.t = -5;
  s.flags = kOutsideDangerCold;
  n = outside_csv_line(s, line, sizeof(line));
  CHECK(n > 0 && std::string(line).find(",-0.5,64,3,4\n") != std::string::npos && std::string(line).find("13:45") != std::string::npos);
  s.t = -123;
  outside_csv_line(s, line, sizeof(line));
  CHECK(std::string(line).find(",-12.3,") != std::string::npos);
  s.t = 0;
  outside_csv_line(s, line, sizeof(line));
  CHECK(std::string(line).find(",0.0,") != std::string::npos);
  s.t = 1005;
  outside_csv_line(s, line, sizeof(line));
  CHECK(std::string(line).find(",100.5,") != std::string::npos);
  CHECK(outside_csv_line(s, line, 10) == 0);  // too small for it

  // Parse it back: the header and bad lines skipped, CRLF, a cut-off last line.
  std::string text = kOutsideCsvHeader;
  OutsideSample a = {(uint32_t)seconds_from_civil(2026, 11, 7, 0, 0, 0), 0, 481, 70, 1, 0};
  OutsideSample b = {(uint32_t)seconds_from_civil(2026, 11, 7, 0, 15, 0), 0, -35, 100, 5, kOutsideDangerCold};
  OutsideSample c = {(uint32_t)seconds_from_civil(2026, 11, 7, 23, 45, 0), 3600, 999, 0, 9, kOutsideDangerHeat};
  for (const OutsideSample *x : {&a, &b, &c}) {
    outside_csv_line(*x, line, sizeof(line));
    text += line;
  }
  OutsidePoint pt[8];
  int cnt = outside_parse(text.data(), text.size(), pt, 8);
  CHECK(cnt == 3);
  CHECK(pt[0].minute == 0 && pt[0].t == 481 && pt[0].h == 70 && pt[0].forecast == 1 && pt[0].flags == 0);
  CHECK(pt[1].minute == 15 && pt[1].t == -35 && pt[1].h == 100 && pt[1].forecast == 5 && pt[1].flags == kOutsideDangerCold);
  // The third sample was taken at local 00:45 next day (utc 23:45 + 1 h): it reads back as minute 45.
  CHECK(pt[2].minute == 45 && pt[2].t == 999 && pt[2].h == 0 && pt[2].forecast == 9 && pt[2].flags == kOutsideDangerHeat);
  CHECK(outside_parse(text.data(), text.size(), pt, 2) == 2);  // at most cap
  CHECK(outside_parse(text.data(), 0, pt, 8) == 0 && outside_parse("", 0, pt, 8) == 0);
  // CRLF line ends.
  std::string crlf;
  for (char ch : text) {
    if (ch == '\n') crlf += '\r';
    crlf += ch;
  }
  CHECK(outside_parse(crlf.data(), crlf.size(), pt, 8) == 3 && pt[1].t == -35);
  // A power cut in the last line: cut at every length, never a wrong point, and never more than 3.
  std::string full2 = text;
  size_t last_start = full2.rfind('\n', full2.size() - 2) + 1;
  for (size_t cut = last_start; cut + 1 < full2.size(); cut++) {
    int k = outside_parse(full2.data(), cut, pt, 8);
    CHECK(k == 2);
  }
  // The whole last line without its line end is still a reading.
  CHECK(outside_parse(full2.data(), full2.size() - 1, pt, 8) == 3);
  // Damaged lines in the middle are skipped, the rest kept.
  std::string junk = std::string(kOutsideCsvHeader) + "garbage\n\n2026-11-07,25:00,1,50.0,50,1,0\n2026-11-07,01:00,1,50.0,50,1,0\n,,,,,,\n";
  CHECK(outside_parse(junk.data(), junk.size(), pt, 8) == 1 && pt[0].minute == 60);

  // The hourly forecast: the code seen most, ties to the later reading, unknown codes don't count.
  OutsidePoint h[16] = {};
  auto P = [](int minute, int fc) {
    OutsidePoint o = {};
    o.minute = (uint16_t)minute;
    o.forecast = (uint8_t)fc;
    return o;
  };
  h[0] = P(5 * 60, 1);
  h[1] = P(5 * 60 + 15, 1);
  h[2] = P(5 * 60 + 30, 3);
  h[3] = P(5 * 60 + 45, 0);  // unknown: not counted
  h[4] = P(6 * 60, 2);       // a tie between 2 and 3: the later one
  h[5] = P(6 * 60 + 15, 3);
  h[6] = P(7 * 60, 42);      // codes the panel doesn't know
  h[7] = P(7 * 60 + 15, 0);
  h[8] = P(8 * 60, 42);
  h[9] = P(8 * 60 + 15, 4);
  h[10] = P(9 * 60, 7);      // a tie the other way round: 7 then 4
  h[11] = P(9 * 60 + 15, 4);
  h[12] = P(9 * 60 + 30, 7);
  h[13] = P(9 * 60 + 45, 4);
  h[14] = P(23 * 60 + 59, 9);
  uint8_t hf[24];
  outside_hourly_forecast(h, 15, hf);
  CHECK(hf[5] == 1 && hf[6] == 3 && hf[7] == 0 && hf[8] == 4 && hf[9] == 4 && hf[23] == 9);
  CHECK(hf[0] == 0 && hf[4] == 0 && hf[10] == 0 && hf[22] == 0);  // hours with no readings
  outside_hourly_forecast(h, 0, hf);
  bool zero = true;
  for (int i = 0; i < 24; i++) zero = zero && hf[i] == 0;
  CHECK(zero);
}

static uint16_t g_gw_seq = 6000;

// An OUTSIDE message from the gateway to the upstairs panel.
static void gw_outside(Pair &p, uint32_t time, int16_t t = 480, uint8_t hum = 71, uint8_t fc = 3,
                       uint8_t flags = kOutsideValid) {
  Msg o;
  o.type = MsgType::Outside;
  o.sender = kSenderGateway;
  o.seq = g_gw_seq++;
  o.u.outside.flags = flags;
  o.u.outside.temp_tenths_f = t;
  o.u.outside.humidity = hum;
  o.u.outside.forecast = fc;
  o.u.outside.time = time;
  std::strcpy(o.u.outside.phrase, "Fair");
  uint8_t buf[kMaxMsg];
  size_t n = encode(o, buf, sizeof(buf));
  p.a.on_radio(buf, n);
}

static int outside_lines_with(const FakePlatform &f, const char *prefix) {
  int k = 0;
  for (const std::string &l : f.outside_lines) k += l.find(prefix) == 0;
  return k;
}

static void test_outside_sampling() {
  // One line per quarter hour on both panels while a reading is fresh, none while it isn't.
  for (int off : {0, 3600}) {
    Pair p(1792627200u + 7 * 60);  // 00:07 UTC
    p.pa.offset = p.pb.offset = off;
    p.begin();
    for (int i = 0; i < 8; i++) {  // readings from 00:07 to 00:14, one a minute
      gw_outside(p, p.pa.w.utc, 481, 71, (uint8_t)Forecast::Cloud, kOutsideValid | kOutsideDangerCold);
      p.run(60000);
    }
    CHECK(p.pa.outside_lines.size() == 1 && p.pb.outside_lines.size() == 1);
    p.run(70000);  // a minute and more into the quarter: no second line for it
    CHECK(p.pa.outside_lines.size() == 1 && p.pb.outside_lines.size() == 1);
    const char *hh = off ? "2026-10-22,01:15," : "2026-10-22,00:15,";
    CHECK(outside_lines_with(p.pa, hh) == 1 && outside_lines_with(p.pb, hh) == 1);
    CHECK(p.pa.outside_names[0] == "o20261022.csv" && p.pb.outside_names[0] == "o20261022.csv");
    CHECK_STR(p.pa.outside_header.c_str(), kOutsideCsvHeader);
    CHECK(p.pa.outside_lines[0].find(",48.1,71,3,4\n") != std::string::npos);
    CHECK(p.pa.outside_lines[0] == p.pb.outside_lines[0]);  // the same reading on both
    // No readings now: the last was at 00:14 and is stale from 00:29, so the 00:30 quarter has none.
    p.run(16 * 60000);  // to about 00:32
    CHECK(p.pa.outside_lines.size() == 1 && p.pb.outside_lines.size() == 1);
    // Readings again: the 00:45 quarter is written, only once.
    for (int i = 0; i < 16; i++) {
      gw_outside(p, p.pa.w.utc, 500, 60, (uint8_t)Forecast::Sun);
      p.run(60000);
    }
    CHECK(p.pa.outside_lines.size() == 2 && p.pb.outside_lines.size() == 2);
    const char *h45 = off ? "2026-10-22,01:45," : "2026-10-22,00:45,";
    CHECK(outside_lines_with(p.pa, h45) == 1 && p.pa.outside_lines[1].find(",50.0,60,1,0\n") != std::string::npos);
  }
  // No reading at all: nothing written. And nothing without a clock.
  {
    Pair p(1792627200u);
    p.pa.offset = p.pb.offset = 0;
    p.begin();
    p.run(35 * 60000);
    CHECK(p.pa.outside_lines.empty() && p.pb.outside_lines.empty());
  }
  {
    Pair p;
    p.pa.offset = p.pb.offset = 0;
    p.pa.w = WallTime();
    p.pb.w = WallTime();
    p.begin();
    p.run(500);
    gw_outside(p, 0);  // the Pi hasn't synced: no time, but the reading shows
    p.run(5000);
    CHECK(p.a.view().outside.valid && !p.pa.w.valid && p.pa.outside_lines.empty());
  }
  // A reading in the first minute of a quarter after a day edge: the line goes to the new day's file.
  {
    Pair p(1792627200u - 120);  // 23:58 UTC the day before
    p.pa.offset = p.pb.offset = 0;
    p.begin();
    for (int i = 0; i < 4; i++) {
      gw_outside(p, p.pa.w.utc);
      p.run(60000);
    }
    CHECK(p.pa.outside_lines.size() == 1 && p.pa.outside_names[0] == "o20261022.csv");
    CHECK(p.pa.outside_lines[0].find("2026-10-22,00:00,") == 0);
  }
}

static void test_outside_clock() {
  // The WeatherGate's time is the panels' network clock (Mike, 2026-11-07).
  Pair p;
  p.pa.w = WallTime();
  p.pb.w = WallTime();
  p.begin();
  p.run(200);
  CHECK(!p.pa.w.valid && !p.pb.w.valid);
  uint32_t T = 1792627200u + 5000;
  gw_outside(p, 0);  // the Pi before it syncs sends 0: never taken
  CHECK(!p.pa.w.valid);
  gw_outside(p, 1767225599u);  // below the floor: not a real time
  CHECK(!p.pa.w.valid);
  gw_outside(p, T);  // an unset clock takes it
  CHECK(p.pa.w.valid && p.pa.w.utc == T && p.pa.w.quality == kClockNetwork);
  p.run(200);  // and upstairs passes it on
  CHECK(p.pb.w.valid && p.pb.w.quality == kClockNetwork && std::abs((long)p.pb.w.utc - (long)T) <= 1);

  uint32_t U = p.pa.w.utc;
  int calls = p.pa.set_wall_calls;
  gw_outside(p, U + 3);  // 3 s ahead: corrected
  CHECK(p.pa.set_wall_calls == calls + 1 && p.pa.w.utc == U + 3);
  gw_outside(p, U);  // 3 s behind: corrected the other way too
  CHECK(p.pa.set_wall_calls == calls + 2 && p.pa.w.utc == U);
  gw_outside(p, U + 1);  // 1 s off: left
  gw_outside(p, U);      // 0 s off: ignored
  gw_outside(p, U + 2);  // 2 s: still within the allowance
  gw_outside(p, U - 2);
  CHECK(p.pa.set_wall_calls == calls + 2 && p.pa.w.utc == U);
  gw_outside(p, 0);
  gw_outside(p, 1767225599u);
  CHECK(p.pa.set_wall_calls == calls + 2 && p.pa.w.utc == U);  // nothing below the floor, over a set clock either
  // A big jump forward is taken; a big jump back on a clock already set from the WeatherGate is most
  // likely an old reading sent again, and is left (review, 2026-11-07); a small step back is taken.
  gw_outside(p, U + 7200);
  CHECK(p.pa.w.utc == U + 7200);
  gw_outside(p, U - 7200);
  CHECK(p.pa.w.utc == U + 7200);
  gw_outside(p, U + 7200 - 60);
  CHECK(p.pa.w.utc == U + 7200 - 60);
  p.pa.set_wall(U + 7200, kClockFromPeer);  // a copied clock gives way to any WeatherGate time
  gw_outside(p, U);
  CHECK(p.pa.w.utc == U && p.pa.w.quality == kClockNetwork);
  // The network time beats a clock set by hand.
  p.pa.set_wall(U, kClockManual);
  gw_outside(p, U + 100);
  CHECK(p.pa.w.utc == U + 100 && p.pa.w.quality == kClockNetwork);
  // An OUTSIDE reading marked not valid still carries the time.
  p.pa.set_wall(U, kClockManual);
  gw_outside(p, U + 50, 0, 0, 0, 0);
  CHECK(p.pa.w.utc == U + 50 && p.pa.w.quality == kClockNetwork);

  // Passed on again later (the other panel restarted), the time is brought up to date, not stale.
  Pair q;
  q.pa.offset = q.pb.offset = 0;
  q.begin();
  q.run(500);
  gw_outside(q, q.pa.w.utc);
  q.run(5 * 60000);
  FakePlatform pb2;
  pb2.w = WallTime();  // downstairs lost power: no clock
  pb2.mono = q.pb.mono;
  pb2.offset = 0;
  pb2.rnd = 4321;
  Controller b2(pb2);
  b2.begin(q.sb, OwnState(), DogState());
  uint32_t now_utc = q.pa.w.utc;
  for (int i = 0; i < 100; i++) {
    q.pa.mono += 20;
    pb2.mono += 20;
    q.a.tick();
    b2.tick();
    for (int r = 0; r < 4; r++) {
      std::vector<Sent> oa, ob;
      oa.swap(q.pa.out);
      ob.swap(pb2.out);
      for (auto &s : oa) b2.on_radio(s.bytes.data(), s.bytes.size());
      for (auto &s : ob) q.a.on_radio(s.bytes.data(), s.bytes.size());
    }
  }
  CHECK(pb2.w.valid && std::abs((long)pb2.w.utc - (long)now_utc) <= 5);  // not 5 minutes old
}


// Audit 2026-11-11: the catch-up after an outage queues 9 frames and the calendar comparison goes
// last. With 8 retry slots the comparison was dropped.
static void test_catch_up_burst() {
  Pair p;
  p.pa.offset = p.pb.offset = 0;
  p.begin();
  p.run(400);
  CHECK(p.a.tap_mail());                       // mail waiting
  CHECK(p.b.set_note("Back at 5", 1800));      // both tile messages
  CHECK(p.a.set_note("Washer by 3", 3600));
  p.run(300);
  CHECK(p.b.comfort_answer(Feel::MuchTooHot, ask_for_feel(Feel::MuchTooHot), AskSent::Send));  // arrows
  p.a.on_local_reading(true, 700, 450);        // a reading
  p.run(300);
  gw_outside(p, p.pa.w.utc);                   // an outside reading, passed on
  p.run(300);
  CHECK(p.a.mail().waiting && p.a.note(Floor::Downstairs).ver && p.a.note(Floor::Upstairs).ver);
  p.cut = true;
  p.a.choose_status(Status::Busy);
  p.run(1700);
  CHECK(p.a.peer_no_signal());
  p.cut = false;
  int sums = 0, types[32] = {};
  p.drop = [&](bool from_a, const Msg &m) {
    if (from_a) {
      types[(int)m.type]++;
      sums += m.type == MsgType::EventSum;
    }
    return false;
  };
  p.b.choose_status(Status::Away);  // the first thing a hears
  p.run(3000);
  CHECK(!p.a.peer_no_signal());
  CHECK(types[(int)MsgType::State] && types[(int)MsgType::DogMsg] && types[(int)MsgType::Mail]);
  CHECK(types[(int)MsgType::Note] >= 2 && types[(int)MsgType::Requests] && types[(int)MsgType::Reading]);
  CHECK(types[(int)MsgType::Outside]);
  CHECK(sums >= 1);
}

// Audit 2026-11-11: one wrong future time must not pin the clocks ahead for good.
static void test_outside_clock_recovery() {
  const uint32_t T0 = 1792627200u + 5000;
  // (a) a single old reading is left alone; a re-sent stale time never steps the clock back
  {
    Pair p;
    p.pa.offset = p.pb.offset = 0;
    p.pa.w = {true, T0, kClockNetwork};
    p.pb.w = {true, T0, kClockNetwork};
    p.begin();
    p.run(500);
    uint32_t stale = p.pa.w.utc - 3600;
    for (int i = 0; i < 12; i++) {  // the same time again and again, minutes apart
      gw_outside(p, stale);
      p.run(120000);
      CHECK(p.pa.w.utc > stale + 3000);
    }
    CHECK(p.pb.w.utc > stale + 3000);
  }
  // (b) after one bogus future time, three consistent real readings bring both clocks back
  {
    Pair p;
    p.pa.offset = p.pb.offset = 0;
    p.pa.w = {true, T0, kClockNetwork};
    p.pb.w = {true, T0, kClockNetwork};
    p.begin();
    p.run(500);
    uint32_t real = p.pa.w.utc;
    gw_outside(p, real + 86400);  // the Pi said tomorrow
    p.run(500);
    CHECK(p.pa.w.utc >= real + 86400 && p.pb.w.utc >= real + 86400 - 2);
    uint32_t truth = real + 1;  // what the gateway says from now on; each reading is real time
    p.run(60000);
    truth += 60;
    gw_outside(p, truth);  // 1st: left alone
    CHECK(p.pa.w.utc >= real + 86400);
    p.run(120000);
    truth += 120;
    gw_outside(p, truth);  // 2nd: still left alone
    CHECK(p.pa.w.utc >= real + 86400);
    p.run(180000);
    truth += 180;
    gw_outside(p, truth);  // 3rd: taken
    p.run(500);
    CHECK(std::abs((long)p.pa.w.utc - (long)(truth + 0)) <= 3);
    CHECK(p.pa.w.quality == kClockNetwork);
    CHECK(std::abs((long)p.pb.w.utc - (long)p.pa.w.utc) <= 3);  // and downstairs went with it
  }
  // (c) a stale time mixed with a real step back: an inconsistent one restarts the count
  {
    Pair p;
    p.pa.offset = p.pb.offset = 0;
    p.pa.w = {true, T0, kClockNetwork};
    p.pb.w = {true, T0, kClockNetwork};
    p.begin();
    p.run(500);
    uint32_t real = p.pa.w.utc;
    gw_outside(p, real + 86400);
    p.run(500);
    uint32_t truth = real + 1;
    for (int i = 0; i < 2; i++) {
      p.run(60000);
      truth += 60;
      gw_outside(p, truth);
    }
    p.run(60000);
    truth += 60;
    gw_outside(p, truth - 1000);  // a different old time: starts over
    CHECK(p.pa.w.utc >= real + 86400);
    p.run(60000);
    truth += 60;
    gw_outside(p, truth);  // only the first of a new run
    CHECK(p.pa.w.utc >= real + 86400);
  }
}

static void test_update_from_gateway() {
  auto gw_update = [](uint16_t seq, uint8_t *buf) {
    Msg u;
    u.type = MsgType::Update;
    u.sender = kSenderGateway;
    u.seq = seq;
    return encode(u, buf, 16);
  };
  uint8_t fb[16];
  // On the upstairs panel it does what the "both panels" button does: UPDATE goes to downstairs,
  // then this panel's updater follows, once each, with no touch.
  {
    Pair p;
    p.begin();
    p.run(1000);
    p.pa.out.clear();
    size_t fn = gw_update(7000, fb);
    p.a.on_radio(fb, fn);
    CHECK(!p.pa.out.empty() && p.pa.out[0].to == kSenderGateway);  // ACKed back to the gateway
    p.run(100);
    CHECK(p.pa.updater_starts == 0 && p.pb.updater_starts == 0);
    // The gateway's retry of the same message and a new try during the wait change nothing.
    p.a.on_radio(fb, fn);
    fn = gw_update(7001, fb);
    p.a.on_radio(fb, fn);
    p.run(1200);
    CHECK(p.pb.updater_starts == 1 && p.pa.updater_starts == 1);
    p.run(6000);
    CHECK(p.pb.updater_starts == 1 && p.pa.updater_starts == 1);
  }
  // Downstairs never starts from the gateway's UPDATE (and still ACKs it).
  {
    Pair p;
    p.begin();
    p.run(1000);
    p.pb.out.clear();
    size_t fn = gw_update(7002, fb);
    p.b.on_radio(fb, fn);
    CHECK(!p.pb.out.empty() && p.pb.out[0].to == kSenderGateway);
    p.run(3000);
    CHECK(p.pb.updater_starts == 0 && p.pa.updater_starts == 0);
  }
  // Downstairs out of reach: upstairs updates itself after 2 s.
  {
    Pair p;
    p.begin();
    p.run(1000);
    p.cut = true;
    size_t fn = gw_update(7003, fb);
    p.a.on_radio(fb, fn);
    p.run(1900);
    CHECK(p.pa.updater_starts == 0);
    p.run(300);
    CHECK(p.pa.updater_starts == 1 && p.pb.updater_starts == 0);
  }
  // No updater on upstairs: nothing starts and nothing is sent on.
  {
    Pair p;
    p.begin();
    p.run(1000);
    p.pa.has_updater = false;
    size_t fn = gw_update(7004, fb);
    p.a.on_radio(fb, fn);
    p.run(3000);
    CHECK(p.pa.updater_starts == 0 && p.pb.updater_starts == 0);
  }
}

static void test_note_recovery_and_acted() {
  const int up = (int)Floor::Upstairs, dn = (int)Floor::Downstairs;
  // A panel that lost its own floor's message takes it back from the other panel's copy at boot.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.a.set_note("Kept upstairs", 7200));
    CHECK(p.b.set_note("Kept downstairs", 7200));
    p.run(200);
    FakePlatform pa2;
    pa2.w = p.pa.w;
    pa2.mono = p.pa.mono;
    pa2.offset = 0;
    pa2.rnd = 5151;
    Controller a2(pa2);
    NoteState lost[2] = {};
    a2.restore_notes(lost);  // upstairs' flash lost both messages
    a2.begin(p.sa, OwnState(), DogState());
    for (int i = 0; i < 60; i++) {
      pa2.mono += 20;
      p.pb.mono += 20;
      a2.tick();
      p.b.tick();
      for (int r = 0; r < 4; r++) {
        std::vector<Sent> oa, ob;
        oa.swap(pa2.out);
        ob.swap(p.pb.out);
        for (auto &s : oa) p.b.on_radio(s.bytes.data(), s.bytes.size());
        for (auto &s : ob) a2.on_radio(s.bytes.data(), s.bytes.size());
      }
    }
    CHECK(!strcmp(a2.note(Floor::Upstairs).text, "Kept upstairs"));  // its own, back from downstairs' copy
    CHECK(!strcmp(a2.note(Floor::Downstairs).text, "Kept downstairs"));
    CHECK(pa2.saved_notes[0].text[0] == 'K' && pa2.saved_notes[1].text[0] == 'K');
    // And it goes on from the recovered counter: its next change wins over downstairs' copy.
    CHECK(a2.set_note("Changed", 3600) && a2.note(Floor::Upstairs).ver == p.a.note(Floor::Upstairs).ver + 1);
  }
  // A NOTE about this panel's own floor with a higher counter is adopted after an outage; when ours
  // is newer, ours goes back instead.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.b.set_note("Mine", 7200));
    p.run(200);
    uint32_t ver = p.b.note(Floor::Downstairs).ver;
    Msg w;
    w.type = MsgType::Note;
    w.sender = kSenderUpstairs;
    w.seq = 4100;
    w.u.note.floor = (uint8_t)Floor::Downstairs;  // upstairs' copy of downstairs' message, newer
    w.u.note.st = note_state("Recovered", p.pb.w.utc, p.pb.w.utc + 3000, p.pb.w.utc, ver + 5);
    uint8_t fb[kMaxMsg];
    size_t fn = encode(w, fb, sizeof(fb));
    p.b.on_radio(fb, fn);
    p.run(200);
    CHECK(!strcmp(p.b.note(Floor::Downstairs).text, "Recovered") && p.b.note(Floor::Downstairs).ver == ver + 5);
    CHECK(!strcmp(p.pb.saved_notes[1].text, "Recovered") && p.b.view().floor[dn].note);
    // An older copy: downstairs keeps its own and sends it back, so upstairs' copy follows.
    w.seq = 4101;
    w.u.note.st = note_state("Old copy", 1, 2, 3, ver);
    fn = encode(w, fb, sizeof(fb));
    p.b.on_radio(fb, fn);
    p.run(300);
    CHECK(!strcmp(p.b.note(Floor::Downstairs).text, "Recovered"));
    CHECK(!strcmp(p.a.note(Floor::Downstairs).text, "Recovered") && p.a.note(Floor::Downstairs).ver == ver + 5);
    // A NOTE with a floor number past 1 is not a message at all.
    uint8_t bad[kMaxMsg];
    size_t bn = encode(w, bad, sizeof(bad));
    bad[5] = 2;
    Msg dd;
    CHECK(!decode(bad, bn, dd));
  }
  // A change made during an outage arrives when the panel is heard again (both floors are resent).
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.a.set_note("Upstairs note", 7200));
    p.run(200);
    CHECK(p.b.note(Floor::Upstairs).ver == 1);
    p.cut = true;
    p.a.clear_note();  // ver 2, never delivered
    p.run(3000);
    p.a.choose_status(Status::Busy);
    p.run(1700);
    CHECK(p.a.peer_no_signal());
    p.cut = false;
    p.b.choose_status(Status::Away);
    p.run(800);
    CHECK(p.b.note(Floor::Upstairs).ver == 2 && p.b.note(Floor::Upstairs).text[0] == 0);  // caught up after No signal
  }
  // A NOTE from the other panel doesn't wake the screen (no touch): at night the backlight stays dim.
  {
    Pair q(1792627200u + 7 * 3600);  // 3 AM EDT
    q.begin();
    q.run(200);
    CHECK(q.pa.backlight == 224 && q.pb.backlight == 224);
    CHECK(q.a.set_note("Night note", 3600));
    q.run(300);
    CHECK(q.pa.backlight == 0);  // the writer touched its own screen
    CHECK(q.pb.backlight == 224);
    CHECK(!strcmp(q.b.note(Floor::Upstairs).text, "Night note"));
    CHECK(q.b.view().floor[(int)Floor::Upstairs].note);
    q.a.clear_note();
    q.run(300);
    CHECK(q.pb.backlight == 224 && !q.b.view().floor[(int)Floor::Upstairs].note);
  }
  // "Action taken on temperature" hides the upstairs message only while the message is older than
  // the acted time; one written after the arrows were cleared shows.
  {
    Pair p(1792670400u);
    req_start(p);
    const View &vb = p.b.view();
    req_send(p, Feel::TooWarm);
    CHECK(p.a.tap_requests());
    p.run(200);
    uint32_t acted = p.b.requests().acted;
    CHECK(acted > 1 && vb.req.acted && !vb.floor[up].note);
    Msg w;
    w.type = MsgType::Note;
    w.sender = kSenderUpstairs;
    w.seq = 4200;
    w.u.note.floor = (uint8_t)Floor::Upstairs;
    w.u.note.st = note_state("Older", acted - 10, acted + 3000, acted - 10, 90);
    uint8_t fb[kMaxMsg];
    size_t fn = encode(w, fb, sizeof(fb));
    p.b.on_radio(fb, fn);
    p.run(200);
    CHECK(vb.req.acted && !vb.floor[up].note);  // older than the acted time: the acted text keeps the spot
    w.seq = 4201;
    w.u.note.st = note_state("Same second", acted, acted + 3000, acted, 91);
    fn = encode(w, fb, sizeof(fb));
    p.b.on_radio(fb, fn);
    p.run(200);
    CHECK(vb.req.acted && !vb.floor[up].note);  // set == acted still counts as not after it
    w.seq = 4202;
    w.u.note.st = note_state("Newer", acted + 5, acted + 3000, acted + 5, 92);
    fn = encode(w, fb, sizeof(fb));
    p.b.on_radio(fb, fn);
    p.run(200);
    CHECK(!vb.req.acted && vb.floor[up].note && !strcmp(vb.floor[up].note_text, "Newer"));
    // When that message goes, the acted text is there again.
    w.seq = 4203;
    w.u.note.st = note_state("", 0, 0, acted + 6, 93);
    fn = encode(w, fb, sizeof(fb));
    p.b.on_radio(fb, fn);
    p.run(200);
    CHECK(vb.req.acted && !vb.floor[up].note);
  }
}

// ---------------------------------------------------------------------------------------------
// Today's next calendar item on each floor's tile, and a dog out too long (Mike, 2026-11-11).

static const int32_t kAgDay = (int32_t)days_from_civil(2026, 10, 22);  // the Pair's day; req_start puts it at noon

// An event on this panel's floor today (or `day`): start < 0 is all-day (length in days).
static uint32_t ag_add(Controller &c, const char *title, int start, int len = 60, int32_t day = kAgDay,
                       EventKind kind = EventKind::Plain) {
  Event e;
  e.day = day;
  e.all_day = start < 0;
  e.start_min = (int16_t)(start < 0 ? 0 : start);
  e.length = (uint16_t)len;
  e.kind = kind;
  e.repeat = kind == EventKind::Plain ? Repeat::None : Repeat::Yearly;
  std::snprintf(e.title, sizeof(e.title), "%s", title);
  return c.save_event(e);
}

static void test_tile_agenda() {
  const int up = (int)Floor::Upstairs, dn = (int)Floor::Downstairs;
  // The next item shows on its own floor's tile on both panels; the other floor's events don't.
  {
    Pair p(1792670400u);  // noon
    req_start(p);
    const View &va = p.a.view(), &vb = p.b.view();
    CHECK(!va.floor[up].agenda && !va.floor[dn].agenda && !vb.floor[up].agenda && !vb.floor[dn].agenda);
    CHECK(ag_add(p.a, "Dentist", 14 * 60 + 30, 45) != 0);
    p.run(300);
    CHECK(va.floor[up].agenda && vb.floor[up].agenda && !va.floor[dn].agenda && !vb.floor[dn].agenda);
    CHECK_STR(va.floor[up].agenda_title, "Dentist");
    CHECK_STR(va.floor[up].agenda_when, "2:30 PM");
    CHECK_STR(vb.floor[up].agenda_when, "2:30 PM");
    CHECK(std::string(va.floor[up].line).find("since") == 0);  // the since line is still built underneath
    CHECK(ag_add(p.b, "Parcel", 15 * 60) != 0);  // downstairs' own, not upstairs'
    p.run(300);
    CHECK(vb.floor[dn].agenda && va.floor[dn].agenda && !strcmp(va.floor[dn].agenda_title, "Parcel"));
    CHECK_STR(vb.floor[up].agenda_title, "Dentist");
    // An earlier item that has not ended wins; once it ends the next one takes over; then none.
    ag_add(p.a, "Call Sam", 12 * 60 + 20, 20);
    p.run(300);
    CHECK_STR(va.floor[up].agenda_title, "Call Sam");
    req_jump(p, 21 * 60);  // 12:21: 12:20 - 12:40 is in progress
    CHECK_STR(va.floor[up].agenda_title, "Call Sam");
    req_jump(p, 20 * 60);  // 12:41: ended
    CHECK_STR(va.floor[up].agenda_title, "Dentist");
    CHECK_STR(vb.floor[up].agenda_title, "Dentist");
    req_jump(p, 2 * 3600);  // 14:41: the 14:30 - 15:15 dentist is in progress
    CHECK_STR(va.floor[up].agenda_title, "Dentist");
    req_jump(p, 40 * 60);  // 15:21: over
    CHECK(!va.floor[up].agenda && !vb.floor[up].agenda);
    CHECK_STR(va.floor[dn].agenda_title, "Parcel");  // 15:00 - 16:00 still going
    req_jump(p, 45 * 60);  // 16:06
    CHECK(!va.floor[dn].agenda && !vb.floor[dn].agenda);
  }
  // In progress at the start; an item that ended exactly now is over.
  {
    Pair p(1792670400u);
    req_start(p);
    ag_add(p.a, "Meeting", 11 * 60 + 30, 60);  // 11:30 - 12:30
    p.run(200);
    CHECK(p.a.view().floor[up].agenda && !strcmp(p.a.view().floor[up].agenda_when, "11:30 AM"));
    ag_add(p.b, "Ended", 11 * 60, 60);  // 11:00 - 12:00, over at noon
    p.run(200);
    CHECK(!p.a.view().floor[dn].agenda && !p.b.view().floor[dn].agenda);
  }
  // All-day: just the title; a timed item still to come beats it; an all-day multi-day one that
  // began yesterday counts.
  {
    Pair p(1792670400u);
    req_start(p);
    ag_add(p.b, "Trash day", -1, 1);
    p.run(300);
    CHECK(p.a.view().floor[dn].agenda && p.b.view().floor[dn].agenda);
    CHECK_STR(p.a.view().floor[dn].agenda_title, "Trash day");
    CHECK_STR(p.a.view().floor[dn].agenda_when, "");
    ag_add(p.b, "Plumber", 16 * 60);
    p.run(300);
    CHECK_STR(p.a.view().floor[dn].agenda_title, "Plumber");
    CHECK_STR(p.a.view().floor[dn].agenda_when, "4:00 PM");
    ag_add(p.a, "Trip", -1, 3, kAgDay - 1);  // began yesterday, runs 3 days
    p.run(300);
    CHECK_STR(p.b.view().floor[up].agenda_title, "Trip");
    CHECK_STR(p.b.view().floor[up].agenda_when, "");
  }
  // None left today: other days' events and ones already over show nothing.
  {
    Pair p(1792670400u);
    req_start(p);
    ag_add(p.a, "Tomorrow", 9 * 60, 60, kAgDay + 1);
    ag_add(p.a, "Yesterday", 9 * 60, 60, kAgDay - 1);
    ag_add(p.a, "Earlier", 8 * 60, 60);  // over at noon
    p.run(300);
    CHECK(!p.a.view().floor[up].agenda && !p.b.view().floor[up].agenda);
  }
  // Birthdays and anniversaries stay on the date strip only.
  {
    Pair p(1792670400u);
    req_start(p);
    ag_add(p.a, "Sam", -1, 1, kAgDay, EventKind::Birthday);
    p.run(300);
    CHECK(!p.a.view().floor[up].agenda && !p.b.view().floor[up].agenda);
    CHECK(p.a.view().nstrip == 1 && p.b.view().nstrip == 1);
  }
  // Priority: the acted confirmation, then a tile message, then the calendar item, then "since".
  {
    Pair p(1792670400u);
    req_start(p);
    const View &va = p.a.view(), &vb = p.b.view();
    ag_add(p.a, "Dentist", 14 * 60 + 30, 45);
    p.run(300);
    CHECK(va.floor[up].agenda && vb.floor[up].agenda && !va.floor[up].note);
    CHECK(p.a.set_note("Heat is on", 7200));  // a typed message beats the calendar item, on both
    p.run(300);
    CHECK(va.floor[up].note && vb.floor[up].note && !va.floor[up].agenda && !vb.floor[up].agenda);
    CHECK(p.a.clear_note());
    p.run(300);
    CHECK(va.floor[up].agenda && vb.floor[up].agenda);  // the item comes back
    // The acted line beats it downstairs (the arrows were cleared upstairs).
    req_send(p, Feel::TooCool);
    CHECK(p.a.tap_requests());
    p.run(300);
    CHECK(vb.req.acted && !vb.floor[up].agenda);
    CHECK(va.floor[up].agenda);  // upstairs has no acted line
    // A message written after the clear replaces the acted line and still beats the calendar.
    req_jump(p, 5);
    CHECK(p.a.set_note("Back at 5", 3600));
    p.run(300);
    CHECK(vb.floor[up].note && !vb.floor[up].agenda);
    // A timer's "until" line is kept.
    CHECK(p.a.clear_note());
    p.run(300);
    CHECK(p.b.start_timer(Status::Private, 3600, Status::Busy));
    ag_add(p.b, "Parcel", 15 * 60);
    p.run(300);
    CHECK(vb.floor[dn].timer && !vb.floor[dn].agenda && !va.floor[dn].agenda);
  }
  // Touch is unchanged: the calendar line is not a tile message, so a tap still means "write one".
  {
    Pair p(1792670400u);
    req_start(p);
    ag_add(p.a, "Dentist", 14 * 60 + 30, 45);
    p.run(300);
    CHECK(p.a.view().floor[up].agenda && !p.a.view().floor[up].note);
    CHECK(!p.a.clear_note());  // nothing to take away: the UI opens the editor
  }
  // No clock: nothing.
  {
    Pair p(1792670400u);
    p.pa.w = WallTime();
    p.pb.w = WallTime();
    p.begin();
    p.run(200);
    ag_add(p.a, "Dentist", 14 * 60 + 30, 45);
    p.run(300);
    CHECK(!p.a.view().floor[up].agenda && !p.b.view().floor[up].agenda);
  }
}

// An OUTSIDE reading with the clock's own time, so it is fresh and the clocks agree.
static void dog_out_reading(Pair &p, int16_t tenths, uint8_t flags = kOutsideValid, uint8_t hum = 71) {
  gw_outside(p, p.pa.w.utc, tenths, hum, 3, flags);
  p.run(100);
}
static int dings(const FakePlatform &f) {  // the dog alert: five dings
  int n = 0;
  for (const auto &pl : f.played) n += pl.first == Sound::DogAlert;
  return n;
}
static int soft_chimes(const FakePlatform &f) {
  int n = 0;
  for (const auto &pl : f.played) n += pl.first == Sound::SoftChime;
  return n;
}

// One outing at this outside reading: the pill reads plain just before the limit and with minutes and
// temperature just after, on both panels. tenths -9999: no outside reading at all.
static void dog_limit_case(int16_t tenths, uint8_t flags, const char *want, int limit_min, uint8_t hum = 71) {
  Pair p(1792670400u);
  req_start(p);
  CHECK(p.b.tap_dog());  // Copper out
  p.run(200);
  if (tenths != -9999) dog_out_reading(p, tenths, flags, hum);
  req_jump(p, (uint32_t)(limit_min * 60 - 30));
  if (tenths != -9999) dog_out_reading(p, tenths, flags, hum);
  CHECK_STR(p.a.view().dog.pill, "COPPER OUT");
  CHECK(!p.a.view().dog.alert && !p.b.view().dog.alert);
  CHECK(dings(p.pa) == 0 && dings(p.pb) == 0);
  req_jump(p, 45);
  if (tenths != -9999) dog_out_reading(p, tenths, flags, hum);
  CHECK(p.a.view().dog.alert && p.b.view().dog.alert);
  CHECK_STR(p.a.view().dog.pill, want);
  CHECK_STR(p.b.view().dog.pill, want);
}

#define DEGF "\xC2\xB0" "F"

static void test_dog_nudge() {
  CHECK(kDogOutMinNormal == 30);
  // Mike's table (2026-11-22), by the feels-like temperature in whole degrees: Lucy (0), Copper (1).
  {
    struct Band { int16_t tenths; int lucy, copper; };
    const Band bands[] = {
        {1200, 15, 15}, {1050, 15, 15}, {1045, 15, 15},   // 104.5 rounds to 105
        {1044, 25, 30}, {1000, 25, 30}, {994, 40, 45},    // 99.4 rounds to 99
        {950, 40, 45}, {945, 40, 45}, {944, 60, 60}, {900, 60, 60},   // 94.5 rounds to 95
        {894, 0, 0}, {700, 0, 0}, {450, 0, 0}, {445, 0, 0},   // 45 to 89: no warning
        {444, 60, 60}, {350, 60, 60}, {344, 30, 30}, {250, 30, 30},
        {244, 20, 20}, {150, 20, 20}, {144, 10, 10}, {50, 10, 10},
        {44, 5, 5}, {0, 5, 5}, {-50, 5, 5}, {-400, 5, 5}};
    for (const Band &b : bands) {
      CHECK(dog_limit_min(0, b.tenths) == b.lucy);
      CHECK(dog_limit_min(1, b.tenths) == b.copper);
    }
    // Feels like: the air itself when it's cool; the NWS heat index when it's hot (its published
    // chart, to a degree); never cooler than the air.
    CHECK(feels_like_tenths(-123, 90) == -123 && feels_like_tenths(480, 71) == 480 && feels_like_tenths(690, 100) == 690);
    auto near = [](int16_t got, int want_f) { return got >= want_f * 10 - 10 && got <= want_f * 10 + 10; };
    CHECK(near(feels_like_tenths(900, 50), 95));
    CHECK(near(feels_like_tenths(900, 70), 106));
    CHECK(near(feels_like_tenths(1000, 40), 109));
    CHECK(near(feels_like_tenths(860, 85), 102));
    CHECK(feels_like_tenths(960, 5) == 960);    // dry heat: the heat index dips below the air, so the air
    CHECK(feels_like_tenths(800, 40) >= 800 && feels_like_tenths(800, 40) < 830);
  }
  // On the panels: Copper, at the outside temperature with the reading's humidity.
  dog_limit_case(300, kOutsideValid, "COPPER OUT 30 min, 30" DEGF, 30);
  dog_limit_case(400, kOutsideValid, "COPPER OUT 60 min, 40" DEGF, 60);
  dog_limit_case(100, kOutsideValid, "COPPER OUT 10 min, 10" DEGF, 10);
  dog_limit_case(-50, kOutsideValid, "COPPER OUT 5 min, -5" DEGF, 5);
  dog_limit_case(960, kOutsideValid, "COPPER OUT 45 min, 96" DEGF, 45, 5);   // dry 96: Copper 45
  dog_limit_case(900, kOutsideValid, "COPPER OUT 15 min, 90" DEGF, 15, 70);  // 90 at 70%: feels 106, 15
  dog_limit_case(-9999, 0, "COPPER OUT 30 min", 30);  // no outside reading: 30 minutes, no temperature
  // 45 to 89: no warning however long, and WeatherGate's danger flags no longer change the limit.
  for (const uint8_t flags : {(uint8_t)kOutsideValid, (uint8_t)(kOutsideValid | kOutsideDangerCold)}) {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.a.tap_dog());
    CHECK(p.b.tap_dog());
    p.run(200);
    for (int k = 0; k < 6; k++) {  // three hours at 60 F
      dog_out_reading(p, 600, flags);
      req_jump(p, 30 * 60);
    }
    dog_out_reading(p, 600, flags);
    CHECK_STR(p.a.view().dog.pill, "BOTH DOGS OUT");
    CHECK(!p.a.view().dog.alert && !p.b.view().dog.alert && dings(p.pa) == 0 && dings(p.pb) == 0);
  }
  // Each dog its own limit: dry 96 F, Lucy at 40 minutes and Copper at 45.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.a.tap_dog());
    CHECK(p.b.tap_dog());
    p.run(200);
    dog_out_reading(p, 960, kOutsideValid, 5);
    req_jump(p, 40 * 60 + 15);
    dog_out_reading(p, 960, kOutsideValid, 5);
    CHECK_STR(p.a.view().dog.pill, "LUCY OUT 40 min, 96" DEGF);
    CHECK(dings(p.pa) == 1 && dings(p.pb) == 0);
    req_jump(p, 5 * 60);
    dog_out_reading(p, 960, kOutsideValid, 5);
    CHECK_STR(p.a.view().dog.pill, "BOTH OUT 45 min, 96" DEGF);
    CHECK(dings(p.pb) == 1);
  }

  // The sound (Mike, 2026-11-13): five dings, never the soft chime (it was taken for a doorbell),
  // only on the floor the dog belongs to (Copper: downstairs, panel b), at the doorbell volume
  // (90 by day), and again every five minutes until the dog is in. The pill pulses on both panels.
  {
    Pair p(1792670400u);
    req_start(p);
    int soft_a = soft_chimes(p.pa), soft_b = soft_chimes(p.pb);
    CHECK(p.b.tap_dog());
    p.run(200);
    dog_out_reading(p, 300);
    req_jump(p, 31 * 60);
    dog_out_reading(p, 300);
    CHECK(dings(p.pa) == 0 && dings(p.pb) == 1);  // upstairs stays silent for Copper
    CHECK(p.pb.played.back().first == Sound::DogAlert && p.pb.played.back().second == 90);
    CHECK(p.a.view().dog.alert && p.b.view().dog.alert);  // the warning shows on both
    req_jump(p, 4 * 60);
    p.run(300);
    CHECK(dings(p.pb) == 1);  // 35 minutes: not yet
    req_jump(p, 60);
    p.run(300);
    CHECK(dings(p.pb) == 2);  // 36 minutes: five after the first
    p.run(2000);
    CHECK(dings(p.pb) == 2);
    req_jump(p, 5 * 60);
    p.run(300);
    CHECK(dings(p.pb) == 3);
    req_jump(p, 11 * 60);  // a long gap is one ring, not a burst
    p.run(300);
    CHECK(dings(p.pb) == 4 && dings(p.pa) == 0);
    CHECK(soft_chimes(p.pa) == soft_a && soft_chimes(p.pb) == soft_b);  // nothing doorbell-like, on either
    // In: the pill and the pulse clear, and the ringing stops. Out again: a new outing.
    CHECK(p.b.tap_dog());
    p.run(300);
    CHECK(!p.a.view().dog.alert && !p.b.view().dog.alert && p.a.view().dog.pill[0] == 0 && p.b.view().dog.pill[0] == 0);
    req_jump(p, 20 * 60);
    p.run(300);
    CHECK(dings(p.pb) == 4);
    CHECK(p.b.tap_dog());
    p.run(300);
    CHECK_STR(p.a.view().dog.pill, "COPPER OUT");
    req_jump(p, 31 * 60);
    p.run(300);
    CHECK(dings(p.pb) == 5 && dings(p.pa) == 0 && p.a.view().dog.alert);
  }
  // Lucy belongs upstairs (panel a): only upstairs rings.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.a.tap_dog());
    p.run(200);
    req_jump(p, 31 * 60);
    p.run(200);
    CHECK(dings(p.pa) == 1 && dings(p.pb) == 0);
    CHECK(p.a.view().dog.alert && p.b.view().dog.alert);
    CHECK_STR(p.b.view().dog.pill, "LUCY OUT 31 min");
    req_jump(p, 5 * 60);
    p.run(200);
    CHECK(dings(p.pa) == 2 && dings(p.pb) == 0);
  }

  // Status gates, as for the mail chime: Private (and Sleeping, Away) make no sound; the dog is
  // still out when the status opens up, so the dings come then.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.b.choose_status(Status::Private));
    p.run(300);
    CHECK(p.b.tap_dog());  // Copper out, downstairs Private
    p.run(200);
    req_jump(p, 31 * 60);
    p.run(200);
    CHECK(p.b.view().dog.alert && dings(p.pb) == 0);  // seen, not heard
    CHECK(dings(p.pa) == 0);                          // not the upstairs dog, whatever its status
    CHECK(p.b.choose_status(Status::Open));
    p.run(300);
    CHECK(dings(p.pb) == 1);
    p.run(2000);
    CHECK(dings(p.pb) == 1);
  }
  // Busy: it rings too, at the doorbell volume.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.b.choose_status(Status::Busy));
    CHECK(p.b.tap_dog());
    p.run(200);
    req_jump(p, 31 * 60);
    p.run(200);
    CHECK(dings(p.pb) == 1 && p.pb.played.back().second == 90);
  }

  // Quiet hours (10 PM to 7 AM): the pill shows and pulses, but no sound until they end.
  {
    Pair p(1792627200u + 23 * 3600);  // 23:00 (offset 0 after req_start)
    req_start(p);
    CHECK(p.a.tap_dog());
    p.run(200);
    req_jump(p, 31 * 60);
    p.run(200);
    CHECK(p.a.view().dog.alert && p.b.view().dog.alert);
    CHECK(dings(p.pa) == 0 && dings(p.pb) == 0);
    req_jump(p, 7 * 3600 + 30 * 60);  // 07:01 the next morning: the dog is still out
    p.run(200);
    CHECK(dings(p.pa) == 1 && dings(p.pb) == 0);
  }

  // Both dogs: each has its own clock; the pill names the dog(s) past the limit, and each floor
  // rings for its own dog only.
  {
    Pair p(1792670400u);
    req_start(p);
    CHECK(p.a.tap_dog());  // Lucy out
    p.run(200);
    req_jump(p, 10 * 60);
    CHECK(p.b.tap_dog());  // Copper out ten minutes later
    p.run(200);
    dog_out_reading(p, 300);
    CHECK_STR(p.a.view().dog.pill, "BOTH DOGS OUT");
    req_jump(p, 20 * 60 + 15);  // Lucy 30 min, Copper 20
    dog_out_reading(p, 300);
    CHECK_STR(p.a.view().dog.pill, "LUCY OUT 30 min, 30" DEGF);
    CHECK_STR(p.b.view().dog.pill, "LUCY OUT 30 min, 30" DEGF);
    CHECK(dings(p.pa) == 1 && dings(p.pb) == 0);
    req_jump(p, 10 * 60);  // Lucy 40, Copper 30
    dog_out_reading(p, 300);
    CHECK_STR(p.a.view().dog.pill, "BOTH OUT 40 min, 30" DEGF);
    CHECK(dings(p.pa) == 2 && dings(p.pb) == 1);  // a repeat upstairs for Lucy, the first downstairs for Copper
    CHECK(p.a.tap_dog());  // Lucy in: Copper alone
    p.run(300);
    dog_out_reading(p, 300);
    CHECK_STR(p.a.view().dog.pill, "COPPER OUT 30 min, 30" DEGF);
    CHECK(p.a.view().dog.alert);
  }
  // Both cross together: each floor rings once, for its own dog.
  {
    Pair p(1792670400u);
    req_start(p);
    p.a.tap_dog();
    p.b.tap_dog();
    p.run(300);
    req_jump(p, 31 * 60);
    p.run(200);
    CHECK_STR(p.a.view().dog.pill, "BOTH OUT 31 min");
    CHECK(dings(p.pa) == 1 && dings(p.pb) == 1);
  }

  // No clock: 30 minutes of elapsed time and no temperature, even with a reading.
  {
    Pair p(1792670400u);
    p.pa.w = WallTime();
    p.pb.w = WallTime();
    p.begin();
    p.run(200);
    CHECK(p.b.tap_dog());
    p.run(200);
    p.run(29 * 60 * 1000);
    CHECK(!p.a.view().dog.alert && !p.b.view().dog.alert);
    p.run(90 * 1000);
    gw_outside(p, 0, 100, 71, 3, kOutsideValid | kOutsideDangerCold);  // would be 10 minutes with a clock
    p.run(300);
    CHECK(p.a.view().dog.alert && p.b.view().dog.alert);
    CHECK_STR(p.b.view().dog.pill, "COPPER OUT 30 min");
    CHECK(dings(p.pb) == 1 && dings(p.pa) == 0);  // no clock, not known to be quiet: the dings at the capped volume
    p.run(5 * 60 * 1000 + 2000);                  // and again five minutes on, by elapsed time alone
    CHECK(dings(p.pb) == 2 && dings(p.pa) == 0);
  }
}


// ---------------------------------------------------------------------------------------------
// House events out to the Pi (Mike, 2026-11-11): the NOTICE message, the kinds, the mask, the queue.

// Distinct notices of a kind that went out (a retry repeats a seq).
static int notices_of(const Pair &p, NoticeKind k) {
  std::vector<uint16_t> seen;
  for (const Msg &m : p.gw) {
    if (m.u.notice.kind != (uint8_t)k) continue;
    bool dup = false;
    for (uint16_t q : seen) dup |= q == m.seq;
    if (!dup) seen.push_back(m.seq);
  }
  return (int)seen.size();
}
static const Msg *last_notice(const Pair &p, NoticeKind k) {
  const Msg *r = nullptr;
  for (const Msg &m : p.gw) if (m.u.notice.kind == (uint8_t)k) r = &m;
  return r;
}
static int notice_tries(const Pair &p, uint16_t seq) {
  int n = 0;
  for (const Msg &m : p.gw) n += m.seq == seq;
  return n;
}
static void notice_start(Pair &p, uint8_t mask) {
  req_start(p);
  // Local noon, so the phones' quiet hours (22:00 to 07:00, Mike 2026-11-15) don't hold back a ring
  // or mail notice; req_start's clock reads midnight.
  p.pa.offset = p.pb.offset = 12 * 3600;
  p.gw_acks = true;
  p.a.set_notice_kinds(mask);
}

static void test_notice_wire() {
  uint8_t buf[kMaxMsg];
  Msg m, d;
  m.type = MsgType::Notice;
  m.sender = kSenderUpstairs;
  m.seq = 77;
  m.u.notice.kind = (uint8_t)NoticeKind::DogOut;
  m.u.notice.floor = 1;
  m.u.notice.value = -300;
  m.u.notice.time = 1793000000u;
  std::strcpy(m.u.notice.text, "COPPER");
  size_t n = encode(m, buf, sizeof(buf));
  CHECK(n == 5 + 1 + 1 + 2 + 4 + 1 + 6);
  CHECK(buf[1] == 17 && buf[2] == kSenderUpstairs);
  CHECK(decode(buf, n, d) && d.type == MsgType::Notice && d.seq == 77);
  CHECK(d.u.notice.kind == 5 && d.u.notice.floor == 1 && d.u.notice.value == -300 && d.u.notice.time == 1793000000u);
  CHECK(d.u.notice.text_len == 6 && std::string(d.u.notice.text) == "COPPER");
  // Longest text: 40 bytes, 49 of body; the encoder cuts anything longer.
  std::memset(m.u.notice.text, 'x', kNoticeTextMax);
  m.u.notice.text[kNoticeTextMax] = 0;
  n = encode(m, buf, sizeof(buf));
  CHECK(n == 5 + 9 + 40 && decode(buf, n, d) && d.u.notice.text_len == 40);
  // What it won't take: a kind outside 1 to 8, a floor over 1, text over 40, a length that disagrees.
  Msg bad = m;
  bad.u.notice.kind = 0;
  CHECK(encode(bad, buf, sizeof(buf)) == 0);
  bad.u.notice.kind = 9;
  CHECK(encode(bad, buf, sizeof(buf)) == 0);
  bad.u.notice.kind = 8;  // Ring (Mike, 2026-11-15) is a kind now
  CHECK(encode(bad, buf, sizeof(buf)) != 0);
  bad = m;
  bad.u.notice.floor = 2;
  CHECK(encode(bad, buf, sizeof(buf)) == 0);
  n = encode(m, buf, sizeof(buf));
  buf[5] = 0;
  CHECK(!decode(buf, n, d));
  buf[5] = 9;
  CHECK(!decode(buf, n, d));
  buf[5] = 8;
  CHECK(decode(buf, n, d) && d.u.notice.kind == 8);
  buf[5] = 7;
  buf[6] = 2;
  CHECK(!decode(buf, n, d));
  buf[6] = 1;
  buf[13] = 41;
  CHECK(!decode(buf, n, d));
  buf[13] = 40;
  CHECK(decode(buf, n - 1, d) == false);  // truncated
  buf[14] = 0x07;                          // a control character is replaced, not trusted
  CHECK(decode(buf, n, d) && d.u.notice.text[0] == '?');
  buf[15] = 0xC3;
  CHECK(decode(buf, n, d) && d.u.notice.text[1] == '?');
  CHECK_STR(msg_name(MsgType::Notice), "NOTICE");
  // Mike, 2026-11-15: mail and ring are on from the start (they push the phones); Mike, 2026-11-18:
  // mild arrows and notes too. Only the status kind is off.
  CHECK(kNoticeDefaultMask == 0xDF && kNoticeDefaultMask == (0x9B | 0x44) && Settings().notice_mask == 0xDF);
  CHECK(!(kNoticeDefaultMask & notice_bit(NoticeKind::Status)));
  CHECK(kNoticeMaskAddedV8 == (notice_bit(NoticeKind::ArrowMild) | notice_bit(NoticeKind::Note)));
  CHECK( notice_bit(NoticeKind::Knock) == 1 && notice_bit(NoticeKind::Note) == 64);
  CHECK(notice_bit(NoticeKind::Ring) == 0x80 && notice_bit(NoticeKind::Mail) == 0x08 && kNoticeKinds == 8);
  CHECK(kNoticeMaskAddedV7 == (notice_bit(NoticeKind::Mail) | notice_bit(NoticeKind::Ring)));
}

static void test_notice_kinds() {
  const uint8_t all = 0x7F;
  // Knock, upstairs rang downstairs: nobody answers in the ring time (2 minutes since 2026-11-15).
  // Once, and not before.
  {
    Pair p;
    notice_start(p, all);
    p.a.tap_bell();
    p.run(110000);
    CHECK(p.gw.empty());
    p.run(11000);
    const Msg *m = last_notice(p, NoticeKind::Knock);
    CHECK(notices_of(p, NoticeKind::Knock) == 1 && m && m->u.notice.floor == 0 && m->u.notice.value == 0);
    CHECK(m && m->u.notice.time > 1792627200u && m->u.notice.text_len == 0);
    p.run(200000);
    CHECK(notices_of(p, NoticeKind::Knock) == 1);
    CHECK(notice_tries(p, m->seq) == 1);  // the gateway ACKed the first try
  }
  // Downstairs rang and the banner ran out unanswered (its 2 minutes): once, with the ring's time.
  {
    Pair p;
    notice_start(p, all);
    p.b.tap_bell();
    p.run(2000);
    CHECK(p.gw.empty());
    uint32_t rang = p.pa.w.utc;
    p.run(100000);
    CHECK(p.gw.empty());
    p.run(21000);
    const Msg *m = last_notice(p, NoticeKind::Knock);
    CHECK(notices_of(p, NoticeKind::Knock) == 1 && m && m->u.notice.floor == 1 && m->u.notice.value == 0);
    CHECK(m && m->u.notice.time + 3 >= rang && m->u.notice.time <= rang + 3);
    p.run(100000);
    CHECK(notices_of(p, NoticeKind::Knock) == 1);
  }
  // Answered, or dismissed, in time: nothing. (Both the ring from upstairs and the one from below.)
  {
    Pair p;
    notice_start(p, all);
    p.a.tap_bell();
    p.run(1000);
    p.b.answer_banner(true);
    p.run(130000);
    p.b.tap_bell();
    p.run(1000);
    p.a.answer_banner(false);
    p.run(130000);
    p.b.tap_bell();
    p.run(1000);
    p.a.dismiss_banner();
    p.run(130000);
    CHECK(p.gw.empty());
  }
  // A ring while upstairs is Away is kept for later: told at once, value 1.
  {
    Pair p;
    notice_start(p, all);
    CHECK(p.a.choose_status(Status::Away));
    p.run(300);
    p.gw.clear();
    p.b.tap_bell();
    p.run(2000);
    const Msg *m = last_notice(p, NoticeKind::Knock);
    CHECK(notices_of(p, NoticeKind::Knock) == 1 && m && m->u.notice.floor == 1 && m->u.notice.value == 1);
    p.run(130000);
    CHECK(notices_of(p, NoticeKind::Knock) == 1);
  }
  // Arrows from the tenant: urgent cooling is kind 2 with -1, mild heat kind 3 with +1, the arrow's
  // own sent time as the time. One notice per arrow.
  {
    Pair p;
    notice_start(p, all);
    uint32_t t0 = p.pb.w.utc;
    CHECK(p.b.comfort_answer(Feel::MuchTooHot, ask_for_feel(Feel::MuchTooHot), AskSent::Send));
    p.run(500);
    const Msg *m = last_notice(p, NoticeKind::ArrowUrgent);
    CHECK(notices_of(p, NoticeKind::ArrowUrgent) == 1 && m && m->u.notice.value == -1 && m->u.notice.floor == 1);
    CHECK(m && m->u.notice.time >= t0 && m->u.notice.time <= t0 + 2);
    CHECK(notices_of(p, NoticeKind::ArrowMild) == 0);
    p.run(20000);  // the urgent arrow dings upstairs again and again: still one notice
    CHECK(notices_of(p, NoticeKind::ArrowUrgent) == 1);
    CHECK(p.b.comfort_answer(Feel::TooCool, ask_for_feel(Feel::TooCool), AskSent::Send));
    p.run(500);
    m = last_notice(p, NoticeKind::ArrowMild);
    CHECK(notices_of(p, NoticeKind::ArrowMild) == 1 && m && m->u.notice.value == 1);
    CHECK(notices_of(p, NoticeKind::ArrowUrgent) == 1);
    CHECK(p.b.comfort_answer(Feel::MuchTooCold, ask_for_feel(Feel::MuchTooCold), AskSent::Send));
    p.run(500);
    m = last_notice(p, NoticeKind::ArrowUrgent);
    CHECK(notices_of(p, NoticeKind::ArrowUrgent) == 2 && m && m->u.notice.value == 1);
    // Mike clears them: no notice for that.
    CHECK(p.a.tap_requests());
    p.run(1000);
    CHECK(notices_of(p, NoticeKind::ArrowUrgent) == 2 && notices_of(p, NoticeKind::ArrowMild) == 1);
  }
  // Mail marked (kind 4): marking raises it, taking it back doesn't, marking again does.
  {
    Pair p;
    notice_start(p, all);
    CHECK(p.a.tap_mail());
    p.run(300);
    const Msg *m = last_notice(p, NoticeKind::Mail);
    CHECK(notices_of(p, NoticeKind::Mail) == 1 && m && m->u.notice.value == 1 && m->u.notice.floor == 1);
    CHECK(p.a.tap_mail());  // taken back
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Mail) == 1);
    CHECK(p.a.tap_mail());
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Mail) == 2);
    CHECK(p.b.tap_mail());  // downstairs acknowledging is not news
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Mail) == 2);
  }
  // A dog out too long (kind 5), whatever the status or quiet hours: COPPER, then BOTH once Lucy is
  // past the limit too; each outing once. The time is the moment the outing began.
  {
    Pair p(1792670400u);
    notice_start(p, all);
    CHECK(p.b.choose_status(Status::Private));  // no chime downstairs: the notice doesn't depend on one
    p.run(300);
    p.gw.clear();
    CHECK(p.b.tap_dog());
    p.run(200);
    uint32_t began = p.pa.w.utc;
    req_jump(p, 20 * 60);
    CHECK(p.gw.empty());
    req_jump(p, 11 * 60);
    const Msg *m = last_notice(p, NoticeKind::DogOut);
    CHECK(notices_of(p, NoticeKind::DogOut) == 1 && m && std::string(m->u.notice.text) == "COPPER");
    CHECK(m && m->u.notice.value >= 30 && m->u.notice.value <= 32 && m->u.notice.time + 2 >= began && m->u.notice.time <= began + 2);
    req_jump(p, 20 * 60);
    CHECK(notices_of(p, NoticeKind::DogOut) == 1);
    CHECK(p.a.tap_dog());  // Lucy out as well
    p.run(200);
    req_jump(p, 31 * 60);
    m = last_notice(p, NoticeKind::DogOut);
    CHECK(notices_of(p, NoticeKind::DogOut) == 2 && m && std::string(m->u.notice.text) == "BOTH");
    CHECK(m && m->u.notice.value >= 61);  // the longer of the two
    req_jump(p, 31 * 60);
    CHECK(notices_of(p, NoticeKind::DogOut) == 2);
    // Both in, Lucy out again: a new outing, told after its own 30 minutes.
    CHECK(p.b.tap_dog());
    CHECK(p.a.tap_dog());
    p.run(300);
    CHECK(p.a.tap_dog());
    p.run(200);
    req_jump(p, 31 * 60);
    m = last_notice(p, NoticeKind::DogOut);
    CHECK(notices_of(p, NoticeKind::DogOut) == 3 && m && std::string(m->u.notice.text) == "LUCY");
  }
  // Status change on either floor (kind 6): the word, whose floor; not at start-up, not for the same status.
  {
    Pair p;
    notice_start(p, all);
    CHECK(p.gw.empty());  // the first exchange at boot is not a change
    CHECK(p.b.choose_status(Status::Busy));
    p.run(300);
    const Msg *m = last_notice(p, NoticeKind::Status);
    CHECK(notices_of(p, NoticeKind::Status) == 1 && m && m->u.notice.floor == 1 && std::string(m->u.notice.text) == "BUSY");
    CHECK(p.a.choose_status(Status::Sleeping));
    p.run(300);
    m = last_notice(p, NoticeKind::Status);
    CHECK(notices_of(p, NoticeKind::Status) == 2 && m && m->u.notice.floor == 0 && std::string(m->u.notice.text) == "SLEEPING");
    CHECK(!p.a.choose_status(Status::Sleeping));
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Status) == 2);
    // A timer running out is a change too.
    CHECK(p.a.start_timer(Status::Private, 600, Status::Busy));
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Status) == 3);
    req_jump(p, 601);
    m = last_notice(p, NoticeKind::Status);
    CHECK(notices_of(p, NoticeKind::Status) == 4 && m && std::string(m->u.notice.text) == "BUSY");
  }
  // A tile message set on either floor (kind 7): the first 40 characters; taking it away is no notice.
  {
    Pair p;
    notice_start(p, all);
    CHECK(p.a.set_note("Back at 3", 3600));
    p.run(300);
    const Msg *m = last_notice(p, NoticeKind::Note);
    CHECK(notices_of(p, NoticeKind::Note) == 1 && m && m->u.notice.floor == 0 && std::string(m->u.notice.text) == "Back at 3");
    CHECK(m && m->u.notice.time >= 1792627200u);
    const char *longer = "Need to wash my clothes today before 3PM please, thank you very much";
    CHECK(p.b.set_note(longer, 3600));
    p.run(300);
    m = last_notice(p, NoticeKind::Note);
    CHECK(notices_of(p, NoticeKind::Note) == 2 && m && m->u.notice.floor == 1);
    CHECK(m && std::string(m->u.notice.text) == std::string(longer).substr(0, 40) && m->u.notice.text_len == 40);
    CHECK(p.b.clear_note());
    CHECK(p.a.clear_note());
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Note) == 2);
    // Upstairs' own message sent across and coming back is not news.
    CHECK(p.a.set_note("Hello", 3600));
    p.run(2000);
    CHECK(notices_of(p, NoticeKind::Note) == 3);
  }
  // Odd characters in a message are made printable on the way out.
  {
    Pair p;
    notice_start(p, all);
    CHECK(p.a.set_note("Caf\xC3\xA9 \"ok\"", 600));
    p.run(300);
    const Msg *m = last_notice(p, NoticeKind::Note);
    CHECK(m && std::string(m->u.notice.text) == "Caf?? \"ok\"");
  }
}

static void test_notice_mask_and_queue() {
  // The default: every kind but status is on (mail and ring since 2026-11-15, for the phones; mild
  // arrow and note since 2026-11-18, for Mike's phone).
  {
    Pair p;
    req_start(p);
    p.pa.offset = p.pb.offset = 12 * 3600;  // local noon: not quiet hours
    p.gw_acks = true;
    CHECK(p.a.notice_kinds() == kNoticeDefaultMask && p.b.notice_kinds() == kNoticeDefaultMask);
    CHECK(p.a.choose_status(Status::Busy));
    CHECK(p.a.set_note("hello", 600));
    CHECK(p.b.comfort_answer(Feel::TooCool, ask_for_feel(Feel::TooCool), AskSent::Send));
    p.run(2000);
    CHECK(notices_of(p, NoticeKind::Status) == 0);  // status is off to begin with
    CHECK(notices_of(p, NoticeKind::Note) == 1 && notices_of(p, NoticeKind::ArrowMild) == 1 && (int)p.gw.size() == 2);
    CHECK(p.a.tap_mail());
    p.run(500);
    CHECK(notices_of(p, NoticeKind::Mail) == 1 && (int)p.gw.size() == 3);
    CHECK(p.b.comfort_answer(Feel::MuchTooHot, ask_for_feel(Feel::MuchTooHot), AskSent::Send));
    p.run(500);
    CHECK(notices_of(p, NoticeKind::ArrowUrgent) == 1 && (int)p.gw.size() == 4);
    p.a.tap_bell();
    p.run(122000);
    CHECK(notices_of(p, NoticeKind::Ring) == 1 && notices_of(p, NoticeKind::Knock) == 1);
    // Switching a kind on or off takes effect at once; every bit of the byte is a kind now.
    p.a.set_notice_kinds(0xFF);
    CHECK(p.a.notice_kinds() == 0xFF);
    p.a.set_notice_kinds(0);
    CHECK(p.a.tap_mail());  // taken back
    CHECK(p.a.tap_mail());
    CHECK(p.a.choose_status(Status::Open));
    p.a.tap_bell();
    p.run(130000);
    CHECK(notices_of(p, NoticeKind::Mail) == 1 && notices_of(p, NoticeKind::Knock) == 1);
    CHECK(notices_of(p, NoticeKind::Ring) == 1);
  }
  // Downstairs never sends one, whatever its mask says.
  {
    Pair p;
    req_start(p);
    p.b.set_notice_kinds(0x7F);
    p.a.set_notice_kinds(0);  // upstairs' own would tell about the ring and the dog
    CHECK(p.b.choose_status(Status::Busy));
    CHECK(p.b.set_note("hi", 600));
    p.b.tap_bell();
    CHECK(p.b.tap_dog());
    p.run(70000);
    req_jump(p, 31 * 60);
    CHECK(p.gw.empty() && p.gw_from_b == 0);
  }
  // A newer notice of the same kind and floor replaces one still waiting; at most two wait at once;
  // panel traffic goes out first in the same tick.
  {
    Pair p;
    req_start(p);
    p.pa.offset = p.pb.offset = 12 * 3600;  // local noon: the mail notice isn't held for quiet hours
    p.a.set_notice_kinds(0x7F);
    p.pa.out.clear();
    CHECK(p.a.choose_status(Status::Busy));
    CHECK(p.a.choose_status(Status::Private));  // same kind, same floor: replaces the first
    p.a.tick();
    std::vector<uint8_t> kinds;
    std::vector<std::string> texts;
    int last_panel = -1, first_notice = -1, idx = 0;
    for (const Sent &s : p.pa.out) {
      Msg m;
      CHECK(decode(s.bytes.data(), s.bytes.size(), m));
      if (m.type == MsgType::Notice) {
        if (first_notice < 0) first_notice = idx;
        kinds.push_back(m.u.notice.kind);
        texts.push_back(m.u.notice.text);
        CHECK(s.to == kSenderGateway);
      } else {
        last_panel = idx;
        CHECK(s.to == kSenderDownstairs);
      }
      idx++;
    }
    CHECK(kinds.size() == 1 && texts[0] == "PRIVATE");
    CHECK(last_panel >= 0 && last_panel < first_notice);
    // Two kinds more: only one of them fits beside the status notice.
    p.pa.out.clear();
    CHECK(p.a.tap_mail());                  // kind 4: the second waiting
    CHECK(p.a.set_note("third", 600));      // kind 7: no room, dropped
    p.a.tick();
    int mail = 0, note = 0;
    for (const Sent &s : p.pa.out) {
      Msg m;
      if (decode(s.bytes.data(), s.bytes.size(), m) && m.type == MsgType::Notice) {
        mail += m.u.notice.kind == 4;
        note += m.u.notice.kind == 7;
      }
    }
    CHECK(mail >= 1 && note == 0);
  }
}

static void test_notice_no_gateway() {
  // Nobody answers: the notice is tried the usual five times, then dropped. Never No signal, and
  // panel-to-panel traffic is not held back (status changes right behind it get through).
  Pair p;
  req_start(p);
  p.a.set_notice_kinds(0x7F);
  p.a.tap_bell();
  p.run(123000);
  CHECK(notices_of(p, NoticeKind::Knock) == 1);
  uint16_t seq = last_notice(p, NoticeKind::Knock)->seq;
  CHECK(notice_tries(p, seq) == RetryQueue::kTries);
  p.run(60000);
  CHECK(notice_tries(p, seq) == RetryQueue::kTries);  // dropped, not retried again
  CHECK(!p.a.peer_no_signal() && !p.b.peer_no_signal());
  CHECK(!p.a.view().floor[(int)Floor::Downstairs].no_signal && !p.b.view().floor[(int)Floor::Upstairs].no_signal);
  // A burst of notices while the gateway is silent, with the panels talking.
  CHECK(p.a.choose_status(Status::Busy));
  CHECK(p.a.tap_mail());
  CHECK(p.a.set_note("x", 600));
  CHECK(p.b.choose_status(Status::Away));
  p.run(5000);
  CHECK(p.b.view().floor[(int)Floor::Upstairs].status == Status::Busy);
  CHECK(p.a.view().floor[(int)Floor::Downstairs].status == Status::Away);
  CHECK(!p.a.peer_no_signal() && !p.b.peer_no_signal());
  // An ACK that comes late (after the drop) changes nothing.
  Msg k;
  k.type = MsgType::Ack;
  k.sender = kSenderGateway;
  k.u.ack.seq = seq;
  k.u.ack.type = (uint8_t)MsgType::Notice;
  uint8_t kb[16];
  p.a.on_radio(kb, encode(k, kb, sizeof(kb)));
  p.run(1000);
  CHECK(!p.a.peer_no_signal());
}

// ---------------------------------------------------------------------------------------------
// The presence sensor (Mike, 2026-11-11): upstairs, one plain on/off input, off until Settings says
// it is fitted. Away from desk, knock routing, the night path, the passer-by flag.

// Remembers the flags of the last STATE upstairs sent, and how many went.
static void watch_flags(Pair &p, uint8_t &flags, int &states) {
  p.drop = [&flags, &states](bool from_a, const Msg &m) {
    if (from_a && m.type == MsgType::State) {
      flags = m.u.state.flags;
      states++;
    }
    return false;
  };
}

static void test_presence_filter() {
  PresenceFilter f;
  CHECK(!f.present() && !f.have_level());
  f.feed(true, true, true);   // one high read: not yet
  CHECK(!f.present() && f.have_level() && f.level());
  f.feed(true, true, true);   // two agree
  CHECK(f.present());
  f.feed(true, false, true);  // a single low read changes nothing
  CHECK(f.present());
  f.feed(true, true, true);   // and a high one after it keeps it
  CHECK(f.present());
  f.feed(true, false, true);
  f.feed(true, false, true);  // two low reads: nobody
  CHECK(!f.present() && !f.level());
  // Active low: the level that means "there" is low.
  PresenceFilter g;
  g.feed(true, false, false);
  g.feed(true, false, false);
  CHECK(g.present());
  g.feed(true, true, false);
  g.feed(true, true, false);
  CHECK(!g.present());
  // Failed reads change nothing, until a long run of them: then nobody.
  PresenceFilter h;
  h.feed(true, true, true);
  h.feed(true, true, true);
  CHECK(h.present());
  for (int i = 0; i < kPresenceErrorsToAbsent - 1; i++) h.feed(false, false, true);
  CHECK(h.present() && h.errors() == (uint32_t)kPresenceErrorsToAbsent - 1);
  h.feed(false, false, true);
  CHECK(!h.present());
  h.feed(true, true, true);  // back: needs two agreeing reads again
  CHECK(!h.present());
  h.feed(true, true, true);
  CHECK(h.present());
  h.reset();
  CHECK(!h.present() && !h.have_level() && h.errors() == 0);
}

static void test_presence_settings_migration() {
  CHECK(sizeof(Settings) > kSettingsV5Size);
  CHECK(kSettingsV3Size <= kSettingsV4Size && kSettingsV4Size <= kSettingsV5Size);  // rounding can make two equal
  // A version 5 blob is the first 104 bytes of today's struct, with whatever the new fields held
  // there (padding, here deliberately junk) ignored.
  Settings s;
  s.floor = Floor::Upstairs;
  s.volume = 70;
  s.quiet_volume_cap = 65;
  s.quiet_start_min = 21 * 60 + 30;
  s.quiet_end_min = 6 * 60 + 45;
  s.soft_chime_drop = 20;
  s.ringtone = 5;
  s.knock[(int)Status::Busy] = {Chime::None, true, true};
  s.backlight_day = 10;
  s.backlight_evening = 130;
  s.night_level = NightLevel::Dim;
  s.screen_off_overnight = true;
  s.touch_wake_ms = 45000;
  s.temp_offset_tenths_f = -30;
  s.timer_fallback_default = Status::Open;
  s.latitude = 12.5f;
  s.longitude = -75.25f;
  std::snprintf(s.tz, sizeof(s.tz), "%s", "CST6CDT,M3.2.0,M11.1.0");
  s.outside_stale_min = 20;
  s.screensaver_min = 25;
  s.screen_off_start_min = 23 * 60;
  s.screen_off_end_min = 5 * 60 + 30;
  s.presence_fitted = true;      // not in a version 5 blob: must come back as the starting value
  s.presence_active_high = false;
  s.presence_hold_s = 300;
  s.notice_mask = 0x7F;
  uint8_t blob[sizeof(Settings)];
  std::memcpy(blob, &s, sizeof(blob));
  const Settings d;
  Settings o;
  o.volume = 1;  // so a failed fill shows
  CHECK(settings_from_old(5, blob, kSettingsV5Size, o));
  CHECK(o.floor == Floor::Upstairs && o.volume == 70 && o.quiet_volume_cap == 65 && o.soft_chime_drop == 20);
  CHECK(o.quiet_start_min == 21 * 60 + 30 && o.quiet_end_min == 6 * 60 + 45 && o.ringtone == 5);
  CHECK(o.knock[(int)Status::Busy].chime == Chime::None && o.knock[(int)Status::Busy].save);
  CHECK(o.knock[(int)Status::Open].chime == Chime::Normal && o.knock[(int)Status::Away].save);
  CHECK(o.backlight_day == 10 && o.backlight_evening == 130 && o.night_level == NightLevel::Dim);
  CHECK(o.screen_off_overnight && o.touch_wake_ms == 45000 && o.temp_offset_tenths_f == -30);
  CHECK(o.timer_fallback_default == Status::Open && o.latitude == 12.5f && o.longitude == -75.25f);
  CHECK_STR(o.tz, "CST6CDT,M3.2.0,M11.1.0");
  CHECK(o.outside_stale_min == 20 && o.screensaver_min == 25);
  CHECK(o.screen_off_start_min == 23 * 60 && o.screen_off_end_min == 5 * 60 + 30);
  CHECK(!o.presence_fitted && o.presence_active_high == d.presence_active_high && o.presence_hold_s == 60);
  CHECK(o.notice_mask == kNoticeDefaultMask);
  // Version 4 had no dark hours; version 3 and earlier no screensaver wait either. Both come back at
  // their starting values, and everything before them is kept.
  Settings o4;
  CHECK(settings_from_old(4, blob, kSettingsV4Size, o4));
  CHECK(o4.screensaver_min == 25 && o4.screen_off_start_min == d.screen_off_start_min && o4.screen_off_end_min == d.screen_off_end_min);
  CHECK(o4.volume == 70 && o4.ringtone == 5 && !o4.presence_fitted && o4.notice_mask == kNoticeDefaultMask);
  for (uint16_t ver = 1; ver <= 3; ver++) {
    Settings o3;
    CHECK(settings_from_old(ver, blob, kSettingsV3Size, o3));
    CHECK(o3.screensaver_min == d.screensaver_min && o3.volume == 70 && o3.latitude == 12.5f && o3.outside_stale_min == 20);
  }
  // A blob of the wrong size or version is refused, never half read.
  Settings x;
  CHECK(!settings_from_old(5, blob, kSettingsV5Size - 4, x) && !settings_from_old(5, blob, kSettingsV4Size, x));
  CHECK(!settings_from_old(4, blob, kSettingsV5Size, x) && !settings_from_old(6, blob, kSettingsV5Size, x));
  CHECK(!settings_from_old(0, blob, kSettingsV5Size, x) && !settings_from_old(99, blob, kSettingsV5Size, x));
  CHECK(!settings_from_old(8, blob, sizeof(Settings), x));  // today's version is read as it is, not here
}

// Version 7 (Mike, 2026-11-15): the same layout as 6, and a saved mask gains mail and ring, once.
static void test_settings_mask_migration() {
  CHECK(kSettingsV6Size == sizeof(Settings));
  Settings s;
  s.volume = 64;
  s.presence_fitted = true;
  s.presence_hold_s = 120;
  s.notice_mask = 0x13;  // version 6's starting mask
  uint8_t blob[sizeof(Settings)];
  std::memcpy(blob, &s, sizeof(blob));
  Settings o;
  CHECK(settings_from_old(6, blob, kSettingsV6Size, o));
  CHECK(o.notice_mask == 0xDF && o.notice_mask == kNoticeDefaultMask);  // 0x13 gains 0x88 and 0x44
  CHECK(o.volume == 64 && o.presence_fitted && o.presence_hold_s == 120);  // the rest as saved
  // A mask Mike changed keeps his bits and gains the two.
  s.notice_mask = 0x05;
  std::memcpy(blob, &s, sizeof(blob));
  CHECK(settings_from_old(6, blob, kSettingsV6Size, o) && o.notice_mask == (0x05 | 0x08 | 0x80 | 0x04 | 0x40));
  // A version 6 blob gains 0xCC in all (0x88 and 0x44) from an empty mask.
  s.notice_mask = 0;
  std::memcpy(blob, &s, sizeof(blob));
  CHECK(settings_from_old(6, blob, kSettingsV6Size, o) && o.notice_mask == 0xCC);
  // Older versions start at the new default.
  CHECK(settings_from_old(5, blob, kSettingsV5Size, o) && o.notice_mask == 0xDF);
  // Version 6 must be its own size.
  CHECK(!settings_from_old(6, blob, kSettingsV6Size - 4, o));
}

// Version 8 (Mike, 2026-11-18): the same layout again, and a version 7 mask gains the mild arrow and
// note kinds, once.
static void test_settings_mask_migration_v8() {
  CHECK(kSettingsV7Size == sizeof(Settings) && settings_old_size(7) == sizeof(Settings));
  Settings s;
  s.volume = 41;
  s.presence_fitted = true;
  s.presence_hold_s = 90;
  s.notice_mask = 0x9B;  // version 7's starting mask
  uint8_t blob[sizeof(Settings)];
  std::memcpy(blob, &s, sizeof(blob));
  Settings o;
  CHECK(settings_from_old(7, blob, kSettingsV7Size, o));
  CHECK(o.notice_mask == 0xDF && o.volume == 41 && o.presence_fitted && o.presence_hold_s == 90);
  // Once only: a migrated mask carried through again changes nothing more, and a mask Mike turned
  // some of the new bits off in (saved as version 8, so never sent through here) is read as it is.
  Settings again = o;
  again.notice_mask = (uint8_t)(o.notice_mask & ~notice_bit(NoticeKind::ArrowMild));
  CHECK(again.notice_mask == 0xDB);
  CHECK(!settings_from_old(8, (const uint8_t *)&again, sizeof(Settings), o));  // not an old version: kept as saved
  CHECK(again.notice_mask == 0xDB);
  // A mask Mike had narrowed in version 7 keeps its bits and gains the two.
  s.notice_mask = 0x11;
  std::memcpy(blob, &s, sizeof(blob));
  CHECK(settings_from_old(7, blob, kSettingsV7Size, o) && o.notice_mask == (0x11 | 0x44));
  CHECK(!settings_from_old(7, blob, kSettingsV7Size - 4, o));
  // A version 7 blob with the status bit on keeps it.
  s.notice_mask = 0x9B | 0x20;
  std::memcpy(blob, &s, sizeof(blob));
  CHECK(settings_from_old(7, blob, kSettingsV7Size, o) && o.notice_mask == 0xFF);
}

// Mike, 2026-11-18: with the default settings, the tenant's mild arrow and a downstairs note reach
// the gateway from the upstairs panel, and a cleared note says nothing.
static void test_notice_defaults_mild_and_note() {
  Pair p;
  req_start(p);
  p.pa.offset = p.pb.offset = 12 * 3600;
  p.gw_acks = true;
  CHECK(p.a.notice_kinds() == 0xDF);
  CHECK(p.b.comfort_answer(Feel::TooWarm, ask_for_feel(Feel::TooWarm), AskSent::Send));
  p.run(500);
  const Msg *m = last_notice(p, NoticeKind::ArrowMild);
  CHECK(notices_of(p, NoticeKind::ArrowMild) == 1 && m && m->u.notice.floor == 1 && m->u.notice.value == -1);
  CHECK(p.b.comfort_answer(Feel::TooCool, ask_for_feel(Feel::TooCool), AskSent::Send));
  p.run(500);
  m = last_notice(p, NoticeKind::ArrowMild);
  CHECK(notices_of(p, NoticeKind::ArrowMild) == 2 && m && m->u.notice.floor == 1 && m->u.notice.value == 1);
  CHECK(p.b.set_note("Back at 3", 3600));
  p.run(500);
  m = last_notice(p, NoticeKind::Note);
  CHECK(notices_of(p, NoticeKind::Note) == 1 && m && m->u.notice.floor == 1 && std::string(m->u.notice.text) == "Back at 3");
  CHECK(p.b.clear_note());  // taken away
  p.run(500);
  CHECK(notices_of(p, NoticeKind::Note) == 1);
}

static void test_presence_notice_mask_saved() {
  Pair p;
  req_start(p);
  const int before = p.pa.settings_saves;
  p.a.set_notice_kinds(0xFF);  // all eight kinds (Ring is bit 0x80 since 2026-11-15)
  CHECK(p.a.notice_kinds() == 0xFF && p.pa.saved_settings.notice_mask == 0xFF && p.pa.settings_saves == before + 1);
  p.a.set_notice_kinds(0x05);
  CHECK(p.pa.saved_settings.notice_mask == 0x05);
  // A restart with what was saved: the mask comes back, and the rest of the settings with it.
  FakePlatform pf;
  pf.w = {true, 1792627200u, kClockManual};
  Controller c(pf);
  c.begin(p.pa.saved_settings, OwnState(), DogState());
  CHECK(c.notice_kinds() == 0x05 && c.settings().floor == Floor::Upstairs);
  // The Settings screen's copy (apply_settings) keeps it too; with eight kinds every bit is one.
  Settings s = c.settings();
  s.notice_mask = 0xFF;
  c.apply_settings(s);
  CHECK(c.notice_kinds() == 0xFF);
}

static void test_presence_wire() {
  Msg m;
  m.type = MsgType::State;
  m.sender = kSenderUpstairs;
  m.seq = 5;
  m.u.state.status = (uint8_t)Status::Busy;
  m.u.state.flags = (uint8_t)(kStateClockValid | kStateAwayDesk | kStateNobodyNear);
  m.u.state.clock_quality = kClockManual;
  uint8_t buf[kMaxMsg];
  size_t n = encode(m, buf, sizeof(buf));
  CHECK(n == 5 + 1 + 1 + 4 + 4 + 1 + 4 + 1);  // the STATE message did not grow
  Msg d;
  CHECK(decode(buf, n, d) && (d.u.state.flags & kStateAwayDesk) && (d.u.state.flags & kStateNobodyNear));
  CHECK((d.u.state.flags & kStateClockValid) && !(d.u.state.flags & kStatePleaseReply));
  m.u.state.flags = (uint8_t)(kStatePleaseReply | kStateClockValid);  // an older panel's STATE
  n = encode(m, buf, sizeof(buf));
  CHECK(decode(buf, n, d) && !(d.u.state.flags & (kStateAwayDesk | kStateNobodyNear)));
}

static void test_presence() {
  const int up = (int)Floor::Upstairs, dn = (int)Floor::Downstairs;
  // Not fitted (the starting setting): the sensor changes nothing, whatever it reads.
  {
    Pair p(1792670400u);  // noon
    uint8_t fl = 0;
    int states = 0;
    watch_flags(p, fl, states);
    req_start(p);
    const View &va = p.a.view(), &vb = p.b.view();
    p.pa.pres = true;
    CHECK(p.a.choose_status(Status::Busy));
    p.run(2000);
    CHECK(states >= 1 && (fl & (kStateAwayDesk | kStateNobodyNear)) == 0);
    CHECK(!va.floor[up].away_desk && !vb.floor[up].away_desk && !va.passer_by);
    CHECK(!va.presence.fitted && !va.presence.seen_now && va.presence.since_min == -1);
    CHECK(std::string(va.floor[up].line).find("since") == 0 && std::string(vb.floor[up].line).find("since") == 0);
    p.run(kNobodyNearMs + 60000);
    CHECK((fl & (kStateAwayDesk | kStateNobodyNear)) == 0);
    CHECK(p.b.settings().presence_fitted == false);
    p.b.tap_bell();
    p.run(500);
    CHECK(vb.toast[0] == 0);
    // The downstairs panel never uses it, even with the setting on.
    Pair q(1792670400u);
    q.sb.presence_fitted = true;
    req_start(q);
    q.pb.pres = true;
    q.run(500);
    CHECK(!q.b.view().passer_by && !q.b.view().presence.fitted);
  }
  // Away from desk: Busy and activity; ends after 5 minutes without; on both panels.
  {
    Pair p(1792670400u);
    p.sa.presence_fitted = true;
    uint8_t fl = 0;
    int states = 0;
    watch_flags(p, fl, states);
    req_start(p);
    const View &va = p.a.view(), &vb = p.b.view();
    CHECK(va.presence.fitted && !va.presence.seen_now && va.presence.since_min == -1);
    p.pa.pres = true;  // activity while Open: nothing
    p.run(500);
    CHECK(!va.floor[up].away_desk && !vb.floor[up].away_desk && !(fl & kStateAwayDesk));
    CHECK(va.presence.seen_now && va.presence.since_min == 0);
    p.pa.pres = false;
    CHECK(p.a.choose_status(Status::Busy));
    p.run(500);
    CHECK(!va.floor[up].away_desk);  // Busy, but no activity since: he is at his desk
    p.pa.pres = true;
    p.run(200);
    CHECK(va.floor[up].away_desk && vb.floor[up].away_desk && (fl & kStateAwayDesk));
    CHECK_STR(va.floor[up].line, "Away from desk");
    CHECK_STR(vb.floor[up].line, "Away from desk");
    CHECK(!va.floor[dn].away_desk && !vb.floor[dn].away_desk);
    p.pa.pres = false;
    p.run(kAwayClearMs - 10000);  // just under 5 minutes of nothing
    CHECK(va.floor[up].away_desk && vb.floor[up].away_desk);
    p.run(11000);
    CHECK(!va.floor[up].away_desk && !vb.floor[up].away_desk && !(fl & kStateAwayDesk));
    CHECK(std::string(va.floor[up].line).find("since") == 0 && std::string(vb.floor[up].line).find("since") == 0);
    // Activity brings it back; leaving Busy takes it away at once on both, and Busy again with
    // someone still there is activity while Busy.
    p.pa.pres = true;
    p.run(200);
    CHECK(vb.floor[up].away_desk);
    CHECK(p.a.choose_status(Status::Open));
    p.run(300);
    CHECK(!va.floor[up].away_desk && !vb.floor[up].away_desk && !(fl & kStateAwayDesk));
    CHECK(p.a.choose_status(Status::Busy));
    p.run(300);
    CHECK(va.floor[up].away_desk && vb.floor[up].away_desk);
    // Busy, then the sensor is turned off in Settings: gone on both, and its state forgotten.
    Settings s = p.a.settings();
    s.presence_fitted = false;
    p.a.apply_settings(s);
    p.run(300);
    CHECK(!va.floor[up].away_desk && !vb.floor[up].away_desk && !va.presence.fitted && !(fl & kStateAwayDesk));
  }
  // Priority on the line: a tile message and the acted confirmation beat it, it beats the calendar
  // item; a timer's "until" and No signal keep theirs.
  {
    Pair p(1792670400u);
    p.sa.presence_fitted = true;
    req_start(p);
    const View &va = p.a.view(), &vb = p.b.view();
    ag_add(p.a, "Dentist", 14 * 60 + 30, 45);
    CHECK(p.a.choose_status(Status::Busy));
    p.pa.pres = true;
    p.run(300);
    CHECK(va.floor[up].away_desk && vb.floor[up].away_desk && !va.floor[up].agenda && !vb.floor[up].agenda);
    CHECK(p.a.set_note("Heat is on", 7200));
    p.run(300);
    CHECK(va.floor[up].note && vb.floor[up].note && !va.floor[up].away_desk && !vb.floor[up].away_desk);
    CHECK(!va.floor[up].agenda && !vb.floor[up].agenda);
    CHECK(p.a.clear_note());
    p.run(300);
    CHECK(va.floor[up].away_desk && vb.floor[up].away_desk && !va.floor[up].agenda);
    // The acted line, downstairs (the arrows were cleared upstairs).
    req_send(p, Feel::TooCool);
    CHECK(p.a.tap_requests());
    p.run(300);
    CHECK(vb.req.acted && !vb.floor[up].away_desk && !vb.floor[up].agenda);
    CHECK(va.floor[up].away_desk);  // upstairs has no acted line
    req_jump(p, 5);
    CHECK(p.a.set_note("Back at 5", 3600));  // a message written after the clear shows in its place
    p.run(300);
    CHECK(vb.floor[up].note && !vb.floor[up].away_desk);
    CHECK(p.a.clear_note());
    p.run(300);
    // The calendar item comes back when he is at his desk again (5 minutes with no activity).
    p.pa.pres = false;
    p.run(kAwayClearMs + 5000);
    CHECK(!va.floor[up].away_desk && !vb.floor[up].away_desk && va.floor[up].agenda);
  }
  {
    Pair p(1792670400u);
    p.sa.presence_fitted = true;
    req_start(p);
    CHECK(p.a.start_timer(Status::Busy, 1800, Status::Open));
    p.pa.pres = true;
    p.run(400);
    CHECK(p.a.view().floor[up].timer && !p.a.view().floor[up].away_desk && !p.b.view().floor[up].away_desk);
    CHECK(std::string(p.b.view().floor[up].line).find("until") == 0);
    // No signal: the gray tile keeps "last seen".
    CHECK(p.a.choose_status(Status::Busy));
    p.run(300);
    CHECK(p.b.view().floor[up].away_desk);
    p.cut = true;
    p.b.tap_bell();  // something to send that never gets an answer
    p.run(3000);
    CHECK(p.b.peer_no_signal() && p.b.view().floor[up].no_signal && !p.b.view().floor[up].away_desk);
    CHECK(std::string(p.b.view().floor[up].line).find("last seen") == 0);
  }
  // Knock routing: the tenant's ring says so while it waits, when nobody has been near the upstairs
  // panel for 15 minutes (the sensor or a touch). Upstairs rings as before.
  {
    Pair p(1792670400u);
    p.sa.presence_fitted = true;
    uint8_t fl = 0;
    int states = 0;
    watch_flags(p, fl, states);
    req_start(p);
    const View &vb = p.b.view();
    p.run(kNobodyNearMs - 10000);
    CHECK(!(fl & kStateNobodyNear));
    p.b.tap_bell();  // near enough: no note
    p.run(300);
    CHECK(vb.toast[0] == 0 && vb.bell.out == BellOut::Delivered);
    p.run(11000);
    CHECK(fl & kStateNobodyNear);
    p.run(10000);
    p.b.tap_bell();
    p.run(300);
    CHECK_STR(vb.toast, "No one is near the panel");
    CHECK(vb.bell.out == BellOut::Delivered);
    CHECK(p.pa.played.size() >= 1);  // the ring's chime upstairs: no new sound for the note
    const size_t chimes = p.pa.played.size();
    p.run(14000);
    CHECK_STR(vb.toast, "No one is near the panel");  // still waiting
    p.run(2000);
    CHECK(vb.toast[0] == 0);  // 15 s at most
    CHECK(p.pa.played.size() == chimes);
    // A touch on the upstairs panel: someone is there.
    p.a.touch();
    p.run(300);
    CHECK(!(fl & kStateNobodyNear));
    p.run(12000);
    p.b.tap_bell();
    p.run(300);
    CHECK(vb.toast[0] == 0);
    // Activity at the sensor counts too, and the other way: 15 quiet minutes, then activity.
    p.run(kNobodyNearMs + 1000);
    CHECK(fl & kStateNobodyNear);
    p.pa.pres = true;
    p.run(300);
    CHECK(!(fl & kStateNobodyNear));
    // Answered: the note goes at once.
    p.pa.pres = false;
    p.run(kNobodyNearMs + 1000);
    CHECK(fl & kStateNobodyNear);
    p.b.tap_bell();
    p.run(300);
    CHECK_STR(vb.toast, "No one is near the panel");
    p.a.answer_banner(true);
    p.run(300);
    CHECK(vb.bell.out == BellOut::ComeOn && vb.toast[0] == 0);
  }
  // The night path: with the screen off overnight, activity brings it up to the night level for 30 s
  // after the last, with no sound; a touch still lights it fully; not fitted, nothing.
  {
    const uint32_t night = 1792648800u;  // 06:00 UTC, deep night in the test's zone; the dark hours are set round it below
    Pair p(night);
    p.sa.presence_fitted = true;
    p.sa.screen_off_overnight = true;
    p.sa.screen_off_start_min = 4 * 60;  // the test's local time is UTC: 6 AM
    p.sa.screen_off_end_min = 7 * 60;
    req_start(p);
    CHECK(p.pa.backlight == kBacklightOff);
    p.pa.pres = true;
    p.run(200);
    CHECK(p.pa.backlight == night_backlight(p.sa.night_level));
    p.pa.pres = false;
    p.run(kNightWakeMs - 2000);
    CHECK(p.pa.backlight == night_backlight(p.sa.night_level));
    p.run(3000);
    CHECK(p.pa.backlight == kBacklightOff);
    CHECK(p.pa.played.empty() && p.a.view().banner.kind == BannerKind::None);
    p.a.touch();
    p.run(100);
    CHECK(p.pa.backlight == p.sa.backlight_day);  // a touch is full light, as before
    p.run(p.sa.touch_wake_ms + 100);
    CHECK(p.pa.backlight == kBacklightOff);
    // Not fitted: the same sensor reading does nothing.
    Pair q(night);
    q.sa.screen_off_overnight = true;
    q.sa.screen_off_start_min = 4 * 60;
    q.sa.screen_off_end_min = 7 * 60;
    req_start(q);
    q.pa.pres = true;
    q.run(500);
    CHECK(q.pa.backlight == kBacklightOff);
    // Dim at night but not off: the level is the night level already, so nothing changes.
    Pair r(night);
    r.sa.presence_fitted = true;
    req_start(r);
    const uint8_t dim = r.pa.backlight;
    CHECK(dim == night_backlight(r.sa.night_level));
    r.pa.pres = true;
    r.run(500);
    CHECK(r.pa.backlight == dim);
    // By day the sensor does not touch the backlight.
    Pair d(1792670400u);
    d.sa.presence_fitted = true;
    req_start(d);
    const uint8_t day = d.pa.backlight;
    d.pa.pres = true;
    d.run(500);
    CHECK(d.pa.backlight == day);
  }
  // The passer-by flag: 3 s each time someone arrives after the place was empty, at most one in
  // 2 minutes; only the upstairs panel.
  {
    Pair p(1792670400u);
    p.sa.presence_fitted = true;
    req_start(p);
    const View &va = p.a.view();
    CHECK(!va.passer_by);
    p.pa.pres = true;
    p.run(100);
    CHECK(va.passer_by && va.presence.occupied);
    p.run(2800);
    CHECK(va.passer_by);  // still within the 3 s
    p.run(300);
    CHECK(!va.passer_by);  // gone, though he is still standing there
    p.run(kPasserByGapMs + 5000);
    CHECK(!va.passer_by);  // standing there is not arriving
    p.pa.pres = false;
    p.run(20000);
    CHECK(!va.passer_by && va.presence.occupied && !va.presence.seen_now);  // the hold time (60 s)
    CHECK(va.presence.since_min == 0);
    p.run(50000);
    CHECK(!va.presence.occupied && va.presence.since_min == 1);
    // Back too soon after the last flag? The last was well over 2 minutes ago here: it shows.
    p.pa.pres = true;
    p.run(100);
    CHECK(va.passer_by);
    p.pa.pres = false;
    p.run(65000);
    p.pa.pres = true;  // arrived again 65 s after leaving, 70 s after the last flag: held back
    p.run(100);
    CHECK(!va.passer_by);
    p.pa.pres = false;
    p.run(180000);
    p.pa.pres = true;
    p.run(100);
    CHECK(va.passer_by);
    CHECK(!p.b.view().passer_by);
  }
  // The hold time is a setting.
  {
    Pair p(1792670400u);
    p.sa.presence_fitted = true;
    p.sa.presence_hold_s = 20;
    req_start(p);
    p.pa.pres = true;
    p.run(100);
    p.pa.pres = false;
    p.run(15000);
    CHECK(p.a.view().presence.occupied);
    p.run(6000);
    CHECK(!p.a.view().presence.occupied);
  }
}

// ---------------------------------------------------------------------------------------------
// The spare picture lent to the screensaver (core/spare_loan.h; memory audit 2026-11-12, rec. 1)

// A stand-in for the panel's screensaver host (ss_host.cpp sceneAlloc, sceneFree, spareHome) and
// for the core's use of HostAlloc (appStart takes the fast block, then the frame-sized cache, and
// frees what it took if it fails; appStop frees both). Sizes are small; the rules don't care.
struct LoanHost {
  static constexpr size_t kFrame = 64, kFast = 16;
  SpareLoan loan;
  alignas(16) uint8_t block[kFrame] = {};
  bool cache_spare = false;
  int own_allocs = 0, own_frees = 0;
  bool psram_full = false;  // the scene's own allocations fail
  void *cache = nullptr, *fast = nullptr;
  bool running = false;

  void *scene_alloc(size_t n) {
    if (n == kFrame && !cache_spare) {
      if (void *p = loan.lend(block, sizeof block, n)) {
        cache_spare = true;
        return p;
      }
    }
    if (psram_full) return nullptr;
    own_allocs++;
    return std::malloc(n);
  }
  void scene_free(void *p) {
    if (cache_spare && loan.give_back(p)) {
      cache_spare = false;
      return;
    }
    own_frees++;
    std::free(p);
  }
  bool spare_home() {  // spareHome: true if it had to take it back
    bool was_out = loan.reclaim();
    cache_spare = false;
    return was_out;
  }
  // appStart: fail_after_cache is a core that fails after taking its cache (it must give it back).
  bool app_start(bool fail_after_cache = false) {
    fast = scene_alloc(kFast);
    if (!fast) return false;
    cache = scene_alloc(kFrame);
    if (!cache || fail_after_cache) {
      if (cache) scene_free(cache);
      scene_free(fast);
      cache = fast = nullptr;
      return false;
    }
    return true;
  }
  void app_stop() {
    scene_free(cache);
    scene_free(fast);
    cache = fast = nullptr;
  }
  // screensaver_start: with the main screen's copy, then without (the fallback), as the host does.
  bool start(bool first_try_fails = false) {
    for (int with_copy = 1; with_copy >= 0 && !running; --with_copy) {
      running = app_start(with_copy && first_try_fails);
      if (!running) spare_home();
    }
    return running;
  }
  // endRun: every way a run ends (a touch with or without the reveal, the screen going dark, a stop
  // from serial) comes through here.
  bool stop() {
    if (!running) return false;
    app_stop();
    running = false;
    return spare_home();
  }
};

static void test_spare_loan() {
  // The art's own use: one slot at a time.
  {
    SpareLoan l;
    CHECK(l.owner() == SpareLoan::kFree);
    CHECK(l.claim(2));
    CHECK(!l.claim(3));
    CHECK(l.owner() == 2);
    CHECK(l.release());
    CHECK(!l.release());
    CHECK(!l.claim(-1));
    CHECK(l.refused() == 0);  // an art slot holding it isn't a loan
  }
  // Lending: only while free, only the block's exact size, aligned, and once.
  {
    SpareLoan l;
    alignas(16) uint8_t b[64];
    CHECK(l.claim(0));
    CHECK(l.lend(b, sizeof b, sizeof b) == nullptr);  // an art slot has it: the screensaver allocates its own
    CHECK(l.owner() == 0);
    l.release();
    CHECK(l.lend(b, sizeof b, 32) == nullptr);        // the wrong size
    CHECK(l.lend(nullptr, sizeof b, sizeof b) == nullptr);
    CHECK(l.lend(b + 1, sizeof b - 1, sizeof b - 1) == nullptr);  // not 4-byte aligned
    CHECK(!l.lent());
    CHECK(l.lend(b, sizeof b, sizeof b) == b);
    CHECK(l.lent() && l.lends() == 1);
    CHECK(l.lend(b, sizeof b, sizeof b) == nullptr);  // a double borrow is refused
    CHECK(l.lends() == 1);
    // While lent: art claims are refused and counted, a release doesn't take it from the screensaver.
    CHECK(!l.claim(0));
    CHECK(!l.claim(2));
    CHECK(l.refused() == 2);
    CHECK(!l.release());
    CHECK(l.lent());
    // Only the lent pointer brings it home, and only once.
    uint8_t other[64];
    CHECK(!l.give_back(other));
    CHECK(!l.give_back(nullptr));
    CHECK(l.lent());
    CHECK(l.give_back(b));
    CHECK(!l.lent() && l.returns() == 1);
    CHECK(!l.give_back(b));
    CHECK(!l.reclaim());   // home already: nothing to take back
    // The deferred art is served now.
    CHECK(l.claim(0));
    CHECK(l.owner() == 0);
    l.release();
    // reclaim: a block still out after a stop (a bug) comes home and is counted.
    CHECK(l.lend(b, sizeof b, sizeof b) == b);
    CHECK(l.reclaim());
    CHECK(!l.lent() && l.reclaims() == 1);
    CHECK(!l.give_back(b));  // it was taken back: a late free isn't ours
    // A new loan starts its refusal count again.
    CHECK(l.lend(b, sizeof b, sizeof b) == b);
    CHECK(l.refused() == 0);
    CHECK(l.give_back(b));
  }
  // Every way a run ends gives it back, through the core's own free, never through reclaim.
  {
    const char *const kEnds[] = {"touch, reveal", "touch, reveal cut short", "touch, no reveal", "screen dark",
                                 "serial ss", "serial ss seed N"};
    for (const char *end : kEnds) {
      (void)end;
      LoanHost h;
      CHECK(h.start());
      CHECK(h.cache_spare && h.loan.lent() && h.cache == h.block);
      CHECK(h.own_allocs == 1);  // only the fast block came from "PSRAM"
      CHECK(!h.loan.claim(0));  // a tile change-over waits
      CHECK(!h.stop());          // false: it came home on its own
      CHECK(!h.loan.lent() && h.loan.returns() == 1 && h.loan.reclaims() == 0);
      CHECK(h.own_allocs == h.own_frees);
      CHECK(h.loan.claim(0));   // and is served after the stop
    }
  }
  // The doorbell doesn't end a run: the loan stays out until the stop.
  {
    LoanHost h;
    CHECK(h.start());
    CHECK(h.loan.lent());  // (a knock posts an event to the scene; nothing here changes)
    CHECK(!h.stop() && !h.loan.lent());
  }
  // The spare busy (a tile drawing or fading) at the start: the old way, its own cache.
  {
    LoanHost h;
    CHECK(h.loan.claim(1));
    CHECK(h.start());
    CHECK(!h.cache_spare && h.cache != nullptr && h.cache != h.block);
    CHECK(h.own_allocs == 2);
    CHECK(h.loan.owner() == 1);   // the tile keeps it
    CHECK(!h.stop());
    CHECK(h.own_allocs == h.own_frees && h.loan.owner() == 1);
    h.loan.release();
  }
  // A start that fails after taking the cache gives it back, and the fallback (without the main
  // screen's copy) borrows it again.
  {
    LoanHost h;
    CHECK(h.start(true));
    CHECK(h.cache_spare && h.loan.lent() && h.cache == h.block);
    CHECK(h.loan.lends() == 2 && h.loan.returns() == 1);
    CHECK(!h.stop());
    CHECK(!h.loan.lent() && h.loan.returns() == 2 && h.own_allocs == h.own_frees);
  }
  // A start that can't happen at all (no memory for the fast block): nothing borrowed, nothing kept.
  {
    LoanHost h;
    h.psram_full = true;
    CHECK(!h.start());
    CHECK(!h.loan.lent() && h.loan.lends() == 0);
    CHECK(h.loan.claim(0));
  }
  // The spare busy and no PSRAM for an own cache: no start, and the tile still has it.
  {
    LoanHost h;
    CHECK(h.loan.claim(4));
    h.psram_full = true;
    CHECK(!h.start());
    CHECK(h.loan.owner() == 4);
  }
}

// ---------------------------------------------------------------------------------------------
// The phones (Mike, 2026-11-15): a RING notice when a floor is rung (Open or Busy, not quiet hours),
// ANSWERED when a panel answered first, PHONE_ANSWER from the gateway (relayed to downstairs), the
// REPLY's phone bit, MAIL_ACK, and the 2-minute ring.

static uint16_t g_knock_seen = 0;
static void watch_knocks(Pair &p) {
  g_knock_seen = 0;
  p.drop = [](bool, const Msg &m) {
    if (m.type == MsgType::Knock) g_knock_seen = m.u.knock.id;
    return false;
  };
}
// The gateway (or another sender) radios a PHONE_ANSWER to a panel.
static void gw_phone(Controller &c, uint8_t floor, uint16_t id, bool come_on, uint8_t sender = kSenderGateway) {
  Msg m;
  m.type = MsgType::PhoneAnswer;
  m.sender = sender;
  m.seq = g_gw_seq++;
  m.u.phone.floor = floor;
  m.u.phone.id = id;
  m.u.phone.come_on = come_on ? 1 : 0;
  uint8_t buf[kMaxMsg];
  c.on_radio(buf, encode(m, buf, sizeof(buf)));
}
static void gw_mail_ack(Controller &c) {
  Msg m;
  m.type = MsgType::MailAck;
  m.sender = kSenderGateway;
  m.seq = g_gw_seq++;
  uint8_t buf[kMaxMsg];
  c.on_radio(buf, encode(m, buf, sizeof(buf)));
}
static int rings_with(const Pair &p, const char *text) {
  std::vector<uint16_t> seen;
  for (const Msg &m : p.gw) {
    if (m.u.notice.kind != (uint8_t)NoticeKind::Ring || std::string(m.u.notice.text) != text) continue;
    bool dup = false;
    for (uint16_t q : seen) dup |= q == m.seq;
    if (!dup) seen.push_back(m.seq);
  }
  return (int)seen.size();
}
static bool acked_to_gateway(const FakePlatform &f, MsgType t) {
  for (const Sent &s : f.out) {
    Msg m;
    if (s.to == kSenderGateway && decode(s.bytes.data(), s.bytes.size(), m) && m.type == MsgType::Ack &&
        m.u.ack.type == (uint8_t)t)
      return true;
  }
  return false;
}

// ROOMS (Mike, 2026-11-22): flags u8 | up_t i16 | up_h u16 | down_t i16 | down_h u16 | time u32, 18 bytes in all.
static void test_rooms_wire() {
  uint8_t buf[kMaxMsg];
  Msg m, d;
  m.type = MsgType::Rooms;
  m.sender = kSenderUpstairs;
  m.seq = 0x0102;
  m.u.rooms = {(uint8_t)(kRoomsUpValid | kRoomsDownValid | kRoomsOccKnown | kRoomsOccupied), 692, 570, -15, 455, 1795358600u};
  size_t n = encode(m, buf, sizeof(buf));
  const uint8_t want[] = {1, 32, 0, 0x02, 0x01, 0x0F, 0xB4, 0x02, 0x3A, 0x02, 0xF1, 0xFF, 0xC7, 0x01, 0x88, 0xFF, 0x02, 0x6B};
  CHECK(n == sizeof(want) && std::memcmp(buf, want, n) == 0);
  CHECK(decode(buf, n, d) && d.type == MsgType::Rooms && d.sender == kSenderUpstairs && d.seq == 0x0102);
  CHECK(d.u.rooms.flags == 0x0F && d.u.rooms.up_t == 692 && d.u.rooms.up_h == 570 && d.u.rooms.down_t == -15);
  CHECK(d.u.rooms.down_h == 455 && d.u.rooms.time == 1795358600u);
  CHECK_STR(msg_name(MsgType::Rooms), "ROOMS");
  CHECK(!decode(buf, n - 1, d));  // short
  uint8_t longer[kMaxMsg];
  std::memcpy(longer, buf, n);
  longer[n] = 0;
  CHECK(!decode(longer, n + 1, d));  // long
  buf[5] = 0x10;  // an unknown bit
  CHECK(!decode(buf, n, d));
  buf[5] = kRoomsOccupied;  // occupied without known
  CHECK(!decode(buf, n, d));
  buf[5] = 0;  // nothing valid is still a ROOMS
  CHECK(decode(buf, n, d) && d.u.rooms.flags == 0);
  buf[8] = 0x03; buf[9] = 0xE9;  // upstairs humidity 5965.1 %: refused
  CHECK(!decode(buf, n, d));
  buf[8] = 0xE9; buf[9] = 0x03;  // 1001 tenths: refused
  CHECK(!decode(buf, n, d));
  buf[8] = 0xE8;  // 1000: taken
  CHECK(decode(buf, n, d) && d.u.rooms.up_h == 1000);
  // encode refuses what decode would.
  Msg bad = m;
  bad.u.rooms.flags = kRoomsOccupied;
  CHECK(encode(bad, buf, sizeof(buf)) == 0);
  bad = m;
  bad.u.rooms.down_h = 1001;
  CHECK(encode(bad, buf, sizeof(buf)) == 0);

  // The retry queue: a ROOMS goes after panel traffic and after a NOTICE, is latest-wins, and is never
  // counted as pending for the other panel.
  RetryQueue q;
  Msg st;
  st.type = MsgType::State;
  Msg no;
  no.type = MsgType::Notice;
  no.u.notice.kind = (uint8_t)NoticeKind::Knock;
  uint16_t r1 = q.push(m, kSenderUpstairs, 0);
  uint16_t r2 = q.push(m, kSenderUpstairs, 0);
  CHECK(r1 && r2 && r1 != r2 && q.pending(MsgType::Rooms) == 1);  // the second replaced the first
  q.push(no, kSenderUpstairs, 0);
  q.push(st, kSenderUpstairs, 0);
  CHECK(q.pending() == 1);
  const RetryQueue::Frame *f = q.due(0);
  CHECK(f && f->type == MsgType::State);
  q.sent(f->seq, 0);
  f = q.due(0);
  CHECK(f && f->type == MsgType::Notice);
  q.sent(f->seq, 0);
  f = q.due(0);
  CHECK(f && f->type == MsgType::Rooms && f->seq == r2);
  CHECK(for_gateway(MsgType::Rooms) && for_gateway(MsgType::Notice) && !for_gateway(MsgType::Reading));
}

// ROOMS from the upstairs panel (Mike, 2026-11-22): when it goes, what it carries, retries and ACKs.
static void test_rooms() {
  const uint8_t both = kRoomsUpValid | kRoomsDownValid;
  {  // first one 20 s after start-up; the tiles' values (offset applied); thresholds; the 10 s floor; heartbeat
    Pair p;
    p.sa.temp_offset_tenths_f = 20;
    p.gw_acks = true;
    p.begin();
    p.a.on_local_reading(true, 690, 570);
    p.b.on_local_reading(true, 681, 455);
    p.run(18000);
    CHECK(p.rooms.empty());
    p.run(3000);
    CHECK(p.rooms.size() == 1);
    if (p.rooms.empty()) return;
    const Msg &m = p.rooms[0];
    CHECK(m.sender == kSenderUpstairs && m.u.rooms.flags == both);
    CHECK(m.u.rooms.up_t == 710 && m.u.rooms.up_h == 570 && m.u.rooms.down_t == 681 && m.u.rooms.down_h == 455);
    CHECK(m.u.rooms.time + 3 >= p.pa.w.utc && m.u.rooms.time <= p.pa.w.utc);
    CHECK(p.a.rooms_info().any && p.a.rooms_info().acked && !p.a.rooms_info().failed);
    p.run(12000);  // nothing changed
    CHECK(p.rooms.size() == 1);
    p.a.on_local_reading(true, 694, 589);  // 0.4 F and 1.9 points: not enough
    p.run(1000);
    CHECK(p.rooms.size() == 1);
    p.a.on_local_reading(true, 695, 589);  // 0.5 F
    p.run(100);
    CHECK(p.rooms.size() == 2 && p.rooms.back().u.rooms.up_t == 715);
    p.run(10000);
    p.a.on_local_reading(true, 695, 609);  // 2.0 points
    p.run(100);
    CHECK(p.rooms.size() == 3 && p.rooms.back().u.rooms.up_h == 609);
    p.a.on_local_reading(true, 720, 609);  // 2.5 F straight after: waits for the 10 s floor
    p.run(9000);
    CHECK(p.rooms.size() == 3);
    p.run(1200);
    CHECK(p.rooms.size() == 4 && p.rooms.back().u.rooms.up_t == 740);
    p.run(10000);
    p.b.on_local_reading(true, 691, 455);  // downstairs 1 F up: its READING, then a ROOMS
    p.run(200);
    const uint32_t down_at = p.pa.mono;
    CHECK(p.rooms.size() == 5 && p.rooms.back().u.rooms.down_t == 691);
    p.run(298000);  // the heartbeat, five minutes after the last
    CHECK(p.rooms.size() == 5);
    p.run(3000);
    CHECK(p.rooms.size() == 6 && p.rooms.back().u.rooms.flags == both);
    // A steady downstairs room sends no READING for a long time (no heartbeats): it still counts while
    // the link is up, so the Home tile doesn't flap (2026-11-22).
    p.run(down_at + 1800000 - p.pa.mono);
    CHECK(p.rooms.back().u.rooms.flags == both && p.rooms.back().u.rooms.down_t == 691);
    // Downstairs's own sensor drops out: its READING says so, and that goes out (a validity change).
    size_t k = p.rooms.size();
    p.b.on_local_reading(false, 0, 0);
    p.run(11000);
    CHECK(p.rooms.size() > k && p.rooms.back().u.rooms.flags == kRoomsUpValid);
    p.b.on_local_reading(true, 705, 455);  // back: valid again, after the 10 s floor
    p.run(11000);
    CHECK(p.rooms.back().u.rooms.flags == both && p.rooms.back().u.rooms.down_t == 705);
    p.a.on_local_reading(false, 0, 0);  // the upstairs sensor drops out
    p.run(11000);
    CHECK(p.rooms.back().u.rooms.flags == kRoomsDownValid);
    CHECK(p.rooms_from_b == 0);  // downstairs never sends one
  }
  {  // occupancy: only with the sensor fitted upstairs; a change goes at once, inside the 10 s floor
    Pair p;
    p.sa.presence_fitted = true;
    p.gw_acks = true;
    p.begin();
    p.a.on_local_reading(true, 690, 500);
    p.run(21000);
    CHECK(p.rooms.size() == 1 && p.rooms.back().u.rooms.flags == (kRoomsUpValid | kRoomsOccKnown));
    p.pa.pres = true;
    p.run(100);
    CHECK(p.rooms.size() == 2 && p.rooms.back().u.rooms.flags == (kRoomsUpValid | kRoomsOccKnown | kRoomsOccupied));
    p.pa.pres = false;
    p.run(58000);  // held for presence_hold_s (60 s)
    CHECK(p.rooms.size() == 2);
    p.run(3000);
    CHECK(p.rooms.size() == 3 && p.rooms.back().u.rooms.flags == (kRoomsUpValid | kRoomsOccKnown));
    CHECK(p.rooms_from_b == 0);
  }
  {  // a sensor fitted on the downstairs panel says nothing about upstairs
    Pair p;
    p.sb.presence_fitted = true;
    p.gw_acks = true;
    p.begin();
    p.pb.pres = true;
    p.run(22000);
    CHECK(p.rooms.size() == 1 && p.rooms.back().u.rooms.flags == 0);
    CHECK(p.rooms_from_b == 0);
  }
  {  // no gateway: five tries, never No signal, again a minute later; once ACKed, every five minutes
    Pair p;
    p.begin();
    p.a.on_local_reading(true, 690, 500);
    p.run(22000);
    CHECK(p.rooms.size() == (size_t)RetryQueue::kTries);
    bool same = true;
    for (const Msg &m : p.rooms) same = same && m.seq == p.rooms[0].seq;
    CHECK(same);
    CHECK(!p.a.rooms_info().acked && p.a.rooms_info().failed && !p.a.peer_no_signal());
    p.run(57000);
    CHECK(p.rooms.size() == (size_t)RetryQueue::kTries);
    p.gw_acks = true;
    p.run(4000);
    CHECK(p.rooms.size() == (size_t)RetryQueue::kTries + 1 && p.rooms.back().seq != p.rooms[0].seq);
    CHECK(p.a.rooms_info().acked && !p.a.rooms_info().failed);
    p.run(290000);
    CHECK(p.rooms.size() == (size_t)RetryQueue::kTries + 1);
    p.run(12000);
    CHECK(p.rooms.size() == (size_t)RetryQueue::kTries + 2);
  }
}

static void test_phone_wire() {
  uint8_t buf[kMaxMsg];
  Msg m, d;
  // PHONE_ANSWER: floor u8 | knock_id u16 | come_on u8, 9 bytes in all.
  m.type = MsgType::PhoneAnswer;
  m.sender = kSenderGateway;
  m.seq = 0x1234;
  m.u.phone.floor = 1;
  m.u.phone.id = 0xBEEF;
  m.u.phone.come_on = 1;
  size_t n = encode(m, buf, sizeof(buf));
  const uint8_t want_pa[] = {1, 18, 2, 0x34, 0x12, 1, 0xEF, 0xBE, 1};
  CHECK(n == sizeof(want_pa) && std::memcmp(buf, want_pa, n) == 0);
  CHECK(decode(buf, n, d) && d.type == MsgType::PhoneAnswer && d.sender == kSenderGateway && d.seq == 0x1234);
  CHECK(d.u.phone.floor == 1 && d.u.phone.id == 0xBEEF && d.u.phone.come_on == 1);
  buf[8] = 0;
  CHECK(decode(buf, n, d) && d.u.phone.come_on == 0);
  buf[5] = 2;  // floor 2
  CHECK(!decode(buf, n, d));
  buf[5] = 0;
  buf[8] = 2;  // come_on 2
  CHECK(!decode(buf, n, d));
  buf[8] = 1;
  buf[6] = buf[7] = 0;  // knock id 0
  CHECK(!decode(buf, n, d));
  buf[6] = 1;
  CHECK(decode(buf, n, d) && d.u.phone.floor == 0 && d.u.phone.id == 1);
  CHECK(!decode(buf, n - 1, d));  // truncated
  Msg bad = m;
  bad.u.phone.floor = 2;
  CHECK(encode(bad, buf, sizeof(buf)) == 0);
  bad = m;
  bad.u.phone.id = 0;
  CHECK(encode(bad, buf, sizeof(buf)) == 0);
  CHECK_STR(msg_name(MsgType::PhoneAnswer), "PHONE_ANSWER");
  // MAIL_ACK: one reserved byte, 6 in all; any value of it is accepted.
  Msg k;
  k.type = MsgType::MailAck;
  k.sender = kSenderGateway;
  k.seq = 0x0102;
  n = encode(k, buf, sizeof(buf));
  const uint8_t want_ma[] = {1, 19, 2, 0x02, 0x01, 0};
  CHECK(n == sizeof(want_ma) && std::memcmp(buf, want_ma, n) == 0);
  CHECK(decode(buf, n, d) && d.type == MsgType::MailAck && d.seq == 0x0102);
  buf[5] = 7;
  CHECK(decode(buf, n, d) && d.type == MsgType::MailAck);
  CHECK(!decode(buf, n - 1, d));
  CHECK_STR(msg_name(MsgType::MailAck), "MAIL_ACK");
  // REPLY's answer byte: bit 0 Come on, bit 1 from a phone; other bits refused.
  Msg r;
  r.type = MsgType::Reply;
  r.sender = kSenderDownstairs;
  r.seq = 5;
  r.u.reply.id = 0x0203;
  r.u.reply.come_on = 1;
  r.u.reply.phone = 1;
  n = encode(r, buf, sizeof(buf));
  const uint8_t want_r[] = {1, 3, 1, 5, 0, 0x03, 0x02, 3};
  CHECK(n == sizeof(want_r) && std::memcmp(buf, want_r, n) == 0);
  CHECK(decode(buf, n, d) && d.u.reply.come_on == 1 && d.u.reply.phone == 1 && d.u.reply.id == 0x0203);
  r.u.reply.come_on = 0;
  n = encode(r, buf, sizeof(buf));
  CHECK(buf[7] == 2 && decode(buf, n, d) && d.u.reply.come_on == 0 && d.u.reply.phone == 1);
  r.u.reply.phone = 0;
  r.u.reply.come_on = 1;
  n = encode(r, buf, sizeof(buf));
  CHECK(buf[7] == 1 && decode(buf, n, d) && d.u.reply.come_on == 1 && d.u.reply.phone == 0);  // as before
  // Bit 2 alone: the banner was dismissed (2026-11-15). Not with an answer bit.
  r.u.reply.come_on = 0;
  r.u.reply.dismissed = 1;
  n = encode(r, buf, sizeof(buf));
  CHECK(n == sizeof(want_r) && buf[7] == 4);
  CHECK(decode(buf, n, d) && d.u.reply.dismissed == 1 && d.u.reply.come_on == 0 && d.u.reply.phone == 0);
  r.u.reply.come_on = 1;
  CHECK(encode(r, buf, sizeof(buf)) == 0);
  for (uint8_t a : {5, 6, 7, 8, 0x80}) {
    buf[7] = a;
    CHECK(!decode(buf, n, d));
  }
  buf[7] = 1;
  CHECK(decode(buf, n, d) && d.u.reply.dismissed == 0);
  // The 2-minute ring (Mike, 2026-11-15).
  CHECK(BellReceiver::kBannerMs == 120000 && BellSender::kNoAnswerMs == 120000);
}

static void test_phone_ring_notice() {
  const uint8_t ring = notice_bit(NoticeKind::Ring);
  // Downstairs rings upstairs: RING for upstairs's phones while upstairs is Open or Busy.
  for (Status st : {Status::Open, Status::Busy, Status::Private, Status::Sleeping, Status::Away}) {
    Pair p;
    notice_start(p, ring);
    if (st != Status::Open) CHECK(p.a.choose_status(st));
    p.run(300);
    p.gw.clear();
    watch_knocks(p);
    uint32_t rang = p.pa.w.utc;
    p.b.tap_bell();
    p.run(500);
    bool want = st == Status::Open || st == Status::Busy;
    CHECK(g_knock_seen != 0);
    CHECK(rings_with(p, "RING") == (want ? 1 : 0));
    const Msg *m = last_notice(p, NoticeKind::Ring);
    if (want) {
      CHECK(m && m->u.notice.floor == 0 && (uint16_t)m->u.notice.value == g_knock_seen);
      CHECK(m && m->u.notice.time + 1 >= rang && m->u.notice.time <= rang + 1);
    }
    p.run(2000);  // the knock's retries and repeats don't push again
    CHECK(rings_with(p, "RING") == (want ? 1 : 0));
  }
  // Upstairs rings downstairs: by downstairs's status, as upstairs last heard it.
  for (Status st : {Status::Open, Status::Busy, Status::Private, Status::Sleeping, Status::Away}) {
    Pair p;
    notice_start(p, ring);
    if (st != Status::Open) CHECK(p.b.choose_status(st));
    p.run(300);
    p.gw.clear();
    watch_knocks(p);
    p.a.tap_bell();
    p.run(500);
    bool want = st == Status::Open || st == Status::Busy;
    CHECK(rings_with(p, "RING") == (want ? 1 : 0));
    const Msg *m = last_notice(p, NoticeKind::Ring);
    if (want) CHECK(m && m->u.notice.floor == 1 && (uint16_t)m->u.notice.value == g_knock_seen && m->u.notice.time != 0);
  }
  // Quiet hours (22:00 to 07:00 by upstairs's clock): no RING either way; the panels still ring.
  {
    Pair p;
    notice_start(p, ring);
    p.pa.offset = p.pb.offset = 0;  // midnight
    p.b.tap_bell();
    p.run(500);
    CHECK(p.a.view().banner.kind == BannerKind::Knock);
    p.a.answer_banner(true);
    p.run(BellSender::kRepeatIgnoreMs + 500);
    p.a.tap_bell();
    p.run(500);
    CHECK(p.b.view().banner.kind == BannerKind::Knock);
    CHECK(notices_of(p, NoticeKind::Ring) == 0);
    p.pa.offset = p.pb.offset = 6 * 3600 + 59 * 60;  // 06:59: still quiet
    p.b.answer_banner(true);
    p.run(BellSender::kRepeatIgnoreMs + 500);
    p.b.tap_bell();
    p.run(500);
    CHECK(notices_of(p, NoticeKind::Ring) == 0);
    p.a.dismiss_banner();
    p.pa.offset = p.pb.offset = 7 * 3600;  // 07:00: over
    p.run(BellSender::kRepeatIgnoreMs + 500);
    p.b.tap_bell();
    p.run(500);
    CHECK(rings_with(p, "RING") == 1);
  }
  // Without a clock upstairs can't tell quiet hours: no RING (it could be 3 AM), either way. The
  // panels still ring.
  {
    Pair p;
    notice_start(p, ring);
    p.pa.w.valid = false;
    p.b.tap_bell();
    p.run(500);
    CHECK(p.a.view().banner.kind == BannerKind::Knock);
    p.a.answer_banner(true);
    p.run(BellSender::kRepeatIgnoreMs + 500);
    p.a.tap_bell();
    p.run(500);
    CHECK(p.b.view().banner.kind == BannerKind::Knock);
    CHECK(notices_of(p, NoticeKind::Ring) == 0);
  }
  // The ring kind switched off: nothing.
  {
    Pair p;
    notice_start(p, 0x7F);
    p.b.tap_bell();
    p.run(500);
    CHECK(notices_of(p, NoticeKind::Ring) == 0);
  }
}

static void test_phone_answered_notice() {
  const uint8_t ring = notice_bit(NoticeKind::Ring);
  // A ring to upstairs answered at the upstairs panel: ANSWERED, same floor, id and time as the RING.
  {
    Pair p;
    notice_start(p, ring);
    p.b.tap_bell();
    p.run(3000);
    p.a.answer_banner(false);
    p.run(500);
    CHECK(rings_with(p, "RING") == 1 && rings_with(p, "ANSWERED") == 1);
    const Msg &r = p.gw.front();
    const Msg *m = last_notice(p, NoticeKind::Ring);
    CHECK(m && std::string(m->u.notice.text) == "ANSWERED" && m->u.notice.floor == 0);
    CHECK(m && m->u.notice.value == r.u.notice.value && m->u.notice.time == r.u.notice.time);
    CHECK(p.b.view().bell.out == BellOut::NotNow);
    CHECK_STR(p.b.view().bell.line, "Not now");  // a panel's answer: no "(from phone)"
  }
  // Dismissed at the panel: ANSWERED too (the phones stop asking).
  {
    Pair p;
    notice_start(p, ring);
    p.b.tap_bell();
    p.run(3000);
    p.a.dismiss_banner();
    p.run(500);
    CHECK(rings_with(p, "RING") == 1 && rings_with(p, "ANSWERED") == 1);
  }
  // A ring to downstairs dismissed at the downstairs panel: downstairs says so (REPLY, dismissed bit),
  // upstairs tells the phones, shows nothing for it, and passes no later phone answer on.
  {
    Pair p;
    notice_start(p, ring);
    watch_knocks(p);
    p.a.tap_bell();
    p.run(3000);
    uint16_t id = g_knock_seen;
    CHECK(p.b.view().banner.kind == BannerKind::Knock);
    int relayed = 0;
    p.drop = [&relayed](bool from_a, const Msg &m) {
      if (from_a && m.type == MsgType::PhoneAnswer) relayed++;
      if (m.type == MsgType::Knock) g_knock_seen = m.u.knock.id;
      return false;
    };
    p.b.dismiss_banner();
    p.run(500);
    CHECK(p.b.view().banner.kind == BannerKind::None);
    CHECK(rings_with(p, "RING") == 1 && rings_with(p, "ANSWERED") == 1);
    const Msg *m = last_notice(p, NoticeKind::Ring);
    CHECK(m && m->u.notice.floor == 1 && (uint16_t)m->u.notice.value == id);
    CHECK(p.a.view().bell.out == BellOut::Delivered);  // the dismiss stays silent to the ringer
    gw_phone(p.a, 1, id, true);
    p.run(300);
    CHECK(relayed == 0 && p.a.view().bell.out == BellOut::Delivered);
    p.run(BellSender::kNoAnswerMs);
    CHECK(p.a.view().bell.out == BellOut::NoAnswer);  // as before: "No answer yet" in time
    CHECK(rings_with(p, "ANSWERED") == 1);
    // The next ring is relayed again.
    p.run(BellSender::kRepeatIgnoreMs);
    p.a.tap_bell();
    p.run(3000);
    gw_phone(p.a, 1, g_knock_seen, true);
    p.run(300);
    CHECK(relayed == 1 && p.a.view().bell.out == BellOut::ComeOn);
  }
  // A ring to downstairs answered at the downstairs panel: upstairs sees the REPLY and tells the phones.
  {
    Pair p;
    notice_start(p, ring);
    p.a.tap_bell();
    p.run(3000);
    p.b.answer_banner(true);
    p.run(500);
    const Msg *m = last_notice(p, NoticeKind::Ring);
    CHECK(rings_with(p, "RING") == 1 && rings_with(p, "ANSWERED") == 1);
    CHECK(m && m->u.notice.floor == 1 && std::string(m->u.notice.text) == "ANSWERED");
    CHECK_STR(p.a.view().bell.line, "Come on");
  }
  // Answered from a phone: no ANSWERED (both directions).
  {
    Pair p;
    notice_start(p, ring);
    watch_knocks(p);
    p.b.tap_bell();
    p.run(3000);
    gw_phone(p.a, 0, g_knock_seen, true);
    p.run(500);
    p.run(BellSender::kRepeatIgnoreMs);
    p.a.tap_bell();
    p.run(3000);
    gw_phone(p.a, 1, g_knock_seen, true);
    p.run(500);
    CHECK(rings_with(p, "RING") == 2 && rings_with(p, "ANSWERED") == 0);
    CHECK(p.a.view().bell.out == BellOut::ComeOn && p.b.view().bell.out == BellOut::ComeOn);
  }
  // A ring that never pushed the phones (Private) gets no ANSWERED.
  {
    Pair p;
    notice_start(p, ring);
    CHECK(p.a.choose_status(Status::Private));
    p.run(300);
    p.b.tap_bell();
    p.run(3000);
    p.a.answer_banner(true);
    p.run(500);
    CHECK(notices_of(p, NoticeKind::Ring) == 0);
  }
  // The banner ran out: no ANSWERED, and nothing to answer afterwards.
  {
    Pair p;
    notice_start(p, ring);
    p.b.tap_bell();
    p.run(121000);
    CHECK(p.a.view().banner.kind == BannerKind::None);
    p.a.answer_banner(true);
    p.a.dismiss_banner();
    p.run(500);
    CHECK(rings_with(p, "RING") == 1 && rings_with(p, "ANSWERED") == 0);
  }
  // Answered downstairs just inside its 2 minutes, but the REPLY only gets through after them: upstairs
  // still shows the answer, and the phones (which have given up) get no ANSWERED.
  {
    Pair p;
    notice_start(p, ring);
    p.a.tap_bell();
    p.run(119500);
    p.cut_ba = true;
    p.b.answer_banner(true);
    p.run(600);
    p.cut_ba = false;
    p.run(1500);
    CHECK(p.a.view().bell.out == BellOut::ComeOn);
    CHECK(rings_with(p, "RING") == 1 && rings_with(p, "ANSWERED") == 0);
  }
}

static void test_phone_answer() {
  const uint8_t ring = notice_bit(NoticeKind::Ring);
  // A ring to upstairs answered from Mike's phone: upstairs's banner goes as if tapped, and the
  // tenant's panel shows the answer "(from phone)".
  {
    Pair p;
    notice_start(p, ring);
    watch_knocks(p);
    p.b.tap_bell();
    p.run(3000);
    CHECK(p.a.view().banner.kind == BannerKind::Knock && p.a.view().bell.alert);
    uint16_t id = g_knock_seen;
    gw_phone(p.a, 0, (uint16_t)(id + 1), true);  // another ring's id: ignored
    p.run(300);
    CHECK(p.a.view().banner.kind == BannerKind::Knock && p.b.view().bell.out == BellOut::Delivered);
    p.pa.out.clear();
    gw_phone(p.a, 0, id, true);
    CHECK(acked_to_gateway(p.pa, MsgType::PhoneAnswer));  // ACKed like any frame
    p.run(300);
    CHECK(p.a.view().banner.kind == BannerKind::None && !p.a.view().bell.alert);
    CHECK(p.b.view().bell.out == BellOut::ComeOn);
    CHECK_STR(p.b.view().bell.line, "Come on (from phone)");
    CHECK(!p.pb.played.empty() && p.pb.played.back().first == Sound::ReplyYes);
    // A second phone tapping later: nothing happens (the Pi says "Already answered").
    gw_phone(p.a, 0, id, false);
    p.run(300);
    CHECK(p.b.view().bell.out == BellOut::ComeOn);
    // No "nobody answered" Knock notice later: it was answered.
    p.a.set_notice_kinds(0xFF);
    p.run(130000);
    CHECK(notices_of(p, NoticeKind::Knock) == 0);
    CHECK_STR(p.b.view().bell.line, "");  // the answer has gone from the tile
  }
  // Not now from the phone.
  {
    Pair p;
    notice_start(p, ring);
    watch_knocks(p);
    p.b.tap_bell();
    p.run(3000);
    gw_phone(p.a, 0, g_knock_seen, false);
    p.run(300);
    CHECK(p.b.view().bell.out == BellOut::NotNow);
    CHECK_STR(p.b.view().bell.line, "Not now (from phone)");
  }
  // A ring to downstairs answered from a tenant's phone: upstairs passes it on, downstairs answers its
  // banner, and upstairs shows "(from phone)".
  {
    Pair p;
    notice_start(p, ring);
    watch_knocks(p);
    p.a.tap_bell();
    p.run(3000);
    uint16_t id = g_knock_seen;
    CHECK(p.b.view().banner.kind == BannerKind::Knock);
    int relayed = 0, replies = 0;
    p.drop = [&relayed, &replies, id](bool from_a, const Msg &m) {
      if (from_a && m.type == MsgType::PhoneAnswer && m.u.phone.floor == 1 && m.u.phone.id == id && m.sender == kSenderUpstairs)
        relayed++;
      if (!from_a && m.type == MsgType::Reply && m.u.reply.phone) replies++;
      return false;
    };
    gw_phone(p.a, 1, (uint16_t)(id + 1), true);  // not the ring upstairs is waiting on
    p.run(300);
    CHECK(relayed == 0 && p.b.view().banner.kind == BannerKind::Knock);
    gw_phone(p.a, 1, id, true);
    p.run(300);
    CHECK(relayed == 1 && replies == 1);
    CHECK(p.b.view().banner.kind == BannerKind::None && !p.b.view().bell.alert);
    CHECK(p.a.view().bell.out == BellOut::ComeOn);
    CHECK_STR(p.a.view().bell.line, "Come on (from phone)");
    gw_phone(p.a, 1, id, false);  // the other phone, later: upstairs isn't waiting any more
    p.run(300);
    CHECK(relayed == 1 && p.a.view().bell.out == BellOut::ComeOn);
  }
  // After the 2-minute window: ignored both ways.
  {
    Pair p;
    notice_start(p, ring);
    watch_knocks(p);
    p.b.tap_bell();
    p.run(121000);
    gw_phone(p.a, 0, g_knock_seen, true);
    p.run(300);
    CHECK(p.b.view().bell.out == BellOut::NoAnswer);
    p.run(BellSender::kRepeatIgnoreMs);
    p.a.tap_bell();
    p.run(121000);
    CHECK(p.b.view().banner.kind == BannerKind::None && p.a.view().bell.out == BellOut::NoAnswer);
    gw_phone(p.a, 1, g_knock_seen, true);  // upstairs still says "No answer yet" and passes it on;
    p.run(300);                            // downstairs's banner is gone, so nothing changes
    CHECK(p.a.view().bell.out == BellOut::NoAnswer && p.b.view().banner.kind == BannerKind::None);
  }
  // Downstairs takes a PHONE_ANSWER only from upstairs: one from the gateway's id is ignored. And
  // upstairs takes one only from the gateway.
  {
    Pair p;
    notice_start(p, ring);
    watch_knocks(p);
    p.a.tap_bell();
    p.run(3000);
    gw_phone(p.b, 1, g_knock_seen, true);  // the gateway straight to downstairs
    p.run(300);
    CHECK(p.b.view().banner.kind == BannerKind::Knock && p.a.view().bell.out == BellOut::Delivered);
    gw_phone(p.b, 0, g_knock_seen, true, kSenderUpstairs);  // upstairs's floor: not downstairs's ring
    p.run(300);
    CHECK(p.b.view().banner.kind == BannerKind::Knock);
    p.b.answer_banner(false);
    p.run(300);
    p.run(BellSender::kRepeatIgnoreMs);
    p.b.tap_bell();
    p.run(3000);
    gw_phone(p.a, 0, g_knock_seen, true, kSenderDownstairs);  // from the downstairs panel: ignored
    p.run(300);
    CHECK(p.a.view().banner.kind == BannerKind::Knock);
  }
  // Missed while Away: a phone can't answer that banner.
  {
    Pair p;
    notice_start(p, ring);
    watch_knocks(p);
    CHECK(p.a.choose_status(Status::Away));
    p.run(300);
    p.b.tap_bell();
    p.run(3000);
    CHECK(p.a.choose_status(Status::Open));
    p.run(300);
    CHECK(p.a.view().banner.kind == BannerKind::MissedWhileAway);
    gw_phone(p.a, 0, g_knock_seen, true);
    p.run(300);
    CHECK(p.a.view().banner.kind == BannerKind::MissedWhileAway && p.b.view().bell.out != BellOut::ComeOn);
  }
}

// "No mail" (Mike, 2026-11-18): the badge left of the mail badge. Quiet, carried in the mail state and
// message, mail and no-mail never together, one phone notice (Mail kind, value 3, text NOMAIL).
static void test_nomail() {
  WallTime t1{true, 1000, kClockManual}, t2{true, 2000, kClockManual};
  // The model: mark, undo, acknowledge, each clearing the other; every change is one counter more.
  {
    MailModel up, down;
    up.toggle_none(t1);
    CHECK(up.state().none && !up.state().waiting && up.state().ver == 1 && up.state().since == 1000);
    CHECK(down.apply_remote(up.state()) && down.state().none && !down.state().waiting);
    CHECK(!down.apply_remote(up.state()));
    CHECK(!down.acknowledge_mail(t2) && down.state().none && down.state().ver == 1);  // not the mail badge's to clear
    CHECK(down.acknowledge_none(t2) && !down.state().none && down.state().ver == 2);
    CHECK(!down.acknowledge_none(t2) && !down.acknowledge(t2));
    CHECK(up.apply_remote(down.state()) && !up.state().none);
    up.toggle_none(t1);  // marked, then taken back by a second tap
    up.toggle_none(t2);
    CHECK(!up.state().none && up.state().ver == 4);
    // Mail clears no-mail, and the reverse.
    up.toggle_none(t1);
    CHECK(up.state().none);
    up.toggle(t2);
    CHECK(up.state().waiting && !up.state().none);
    up.toggle_none(t2);
    CHECK(up.state().none && !up.state().waiting);
    CHECK(!up.acknowledge_mail(t2) && up.acknowledge(t2) && !up.state().none && !up.state().waiting);  // acknowledge clears either
    up.toggle(t1);
    CHECK(up.acknowledge(t2) && !up.state().waiting);
    // The counter rule: higher wins, then the later time, then the louder (mail over no mail).
    MailState a, b;
    a.none = true; a.since = 5; a.ver = 3;
    b.since = 6; b.ver = 3;
    MailModel x;
    x.restore(a);
    CHECK(x.apply_remote(b) && !x.state().none);  // the same counter: the later change
    b.since = 5;
    MailModel y;
    y.restore(b);
    CHECK(y.apply_remote(a) && y.state().none);   // a tie: a mark beats nothing
    MailModel z;
    z.restore(a);
    CHECK(!z.apply_remote(b));
    MailState m1;
    m1.waiting = true; m1.since = 5; m1.ver = 3;
    MailModel w;
    w.restore(a);
    CHECK(w.apply_remote(m1) && w.state().waiting && !w.state().none);  // a tie: mail beats no mail
    MailModel v;
    v.restore(m1);
    CHECK(!v.apply_remote(a) && v.state().waiting);                    // on both panels
    MailState hi = a;
    hi.ver = 9;
    MailModel u;
    u.restore(m1);
    CHECK(u.apply_remote(hi) && u.state().none && !u.state().waiting);  // the higher counter wins either way
  }
  // The wire: the same 14 bytes; the byte is 2 for no mail and a plain mail message is unchanged.
  {
    Msg m;
    m.type = MsgType::Mail;
    m.sender = kSenderUpstairs;
    m.seq = 0x1234;
    m.u.mail.waiting = 2;
    m.u.mail.since = 0x6ADF03D0u;
    m.u.mail.ver = 7;
    uint8_t buf[32];
    size_t n = encode(m, buf, sizeof(buf));
    CHECK(n == 14 && buf[5] == 2);
    Msg d;
    CHECK(decode(buf, n, d) && d.u.mail.waiting == 2 && d.u.mail.ver == 7);
    m.u.mail.waiting = 1;
    CHECK(encode(m, buf, sizeof(buf)) == 14 && buf[5] == 1);
    buf[5] = 255;
    CHECK(!decode(buf, 14, d));
  }
  // Two panels: marked upstairs, red on both, nothing heard or woken, acknowledged downstairs.
  {
    Pair p;
    notice_start(p, notice_bit(NoticeKind::Mail));
    p.run(200);
    CHECK(p.a.view().nomail.show && !p.a.view().nomail.active);  // upstairs: the faint outline
    CHECK(!p.b.view().nomail.show);                              // downstairs: nothing
    CHECK(!p.b.tap_nomail());                                    // nothing to acknowledge
    size_t played_a = p.pa.played.size(), played_b = p.pb.played.size();
    CHECK(p.a.tap_nomail());
    p.run(300);
    CHECK(p.a.mail().none && p.b.mail().none && !p.a.mail().waiting && !p.b.mail().waiting);
    CHECK(p.pa.saved_mail.none && p.pb.saved_mail.none);
    CHECK(p.a.view().nomail.show && p.a.view().nomail.active);
    CHECK(p.b.view().nomail.show && p.b.view().nomail.active);
    // Plain mail's badge stays out of it, on both panels, and so does the screensaver's (it reads mail.show).
    CHECK(p.a.view().mail.show && !p.a.view().mail.solid && !p.a.view().mail.pulse);
    CHECK(!p.b.view().mail.show && !p.b.view().mail.solid && !p.b.view().mail.pulse);
    CHECK(p.pa.played.size() == played_a && p.pb.played.size() == played_b);  // no sound anywhere
    // The one phone notice: Mail kind, downstairs, value 3, NOMAIL, the time it was marked.
    const Msg *m = last_notice(p, NoticeKind::Mail);
    CHECK(notices_of(p, NoticeKind::Mail) == 1 && m && m->u.notice.floor == 1 && m->u.notice.value == 3 &&
          std::string(m->u.notice.text) == "NOMAIL" && m->u.notice.text_len == 6 && m->u.notice.time == p.a.mail().since);
    p.run(20000);
    CHECK(notices_of(p, NoticeKind::Mail) == 1);
    // The tenant taps it: gone on both, nothing sent for that.
    CHECK(p.b.tap_nomail());
    p.run(300);
    CHECK(!p.a.mail().none && !p.b.mail().none && !p.pa.saved_mail.none && !p.pb.saved_mail.none);
    CHECK(p.a.view().nomail.show && !p.a.view().nomail.active && !p.b.view().nomail.show);
    CHECK(notices_of(p, NoticeKind::Mail) == 1);
    CHECK(p.pa.played.size() == played_a && p.pb.played.size() == played_b);
    // Marked by mistake: a second tap upstairs takes it back, on both, with no notice.
    CHECK(p.a.tap_nomail());
    p.run(300);
    CHECK(p.b.mail().none && notices_of(p, NoticeKind::Mail) == 2);
    CHECK(p.a.tap_nomail());
    p.run(300);
    CHECK(!p.b.mail().none && !p.b.view().nomail.show && notices_of(p, NoticeKind::Mail) == 2);
    // The mail badge's own tap downstairs does not clear a no-mail mark, nor the reverse.
    CHECK(p.a.tap_nomail());
    p.run(300);
    CHECK(!p.b.tap_mail() && p.b.mail().none);
    CHECK(p.a.tap_mail());  // mail clears no-mail, on both
    p.run(300);
    CHECK(p.a.mail().waiting && !p.a.mail().none && p.b.mail().waiting && !p.b.mail().none);
    CHECK(!p.b.view().nomail.show && !p.a.view().nomail.active && p.b.view().mail.pulse);
    CHECK(p.pb.played.size() == played_b + 1 && p.pb.played.back().first == Sound::SoftChime);  // mail still chimes
    CHECK(!p.b.tap_nomail());
    CHECK(p.a.tap_nomail());  // no mail clears mail, on both, and is quiet again
    p.run(300);
    CHECK(p.a.mail().none && !p.a.mail().waiting && p.b.mail().none && !p.b.mail().waiting);
    CHECK(!p.b.view().mail.show && p.b.view().nomail.active && !p.a.view().mail.solid);
    CHECK(p.pb.played.size() == played_b + 1);
    CHECK(notices_of(p, NoticeKind::Mail) == 5);  // no mail, no mail, no mail, mail, no mail: each marking once
  }
  // No wake: new mail wakes a dark downstairs screen, a no-mail mark does not.
  {
    Pair p;
    p.sb.screen_off_overnight = true;
    p.sa.screen_off_overnight = true;
    p.begin();
    p.pa.offset = p.pb.offset = 23 * 3600;  // local 23:00
    p.run(30000);
    uint8_t dark = p.b.view().backlight;
    CHECK(p.a.tap_nomail());
    p.run(500);
    CHECK(p.b.mail().none && p.b.view().backlight == dark);
    CHECK(p.b.tap_nomail());
    p.run(30000);
    dark = p.b.view().backlight;
    CHECK(p.a.tap_nomail());
    p.run(500);
    CHECK(p.b.mail().none && p.b.view().backlight == dark);
    CHECK(p.b.tap_nomail());
    p.run(30000);
    CHECK(p.b.view().backlight == dark);
    CHECK(p.a.tap_mail());  // the same setup does wake it for mail, so the checks above can fail
    p.run(500);
    CHECK(p.b.view().backlight != dark);
  }
  // The phones' rules, the same as the Mail notice: Open or Busy, not in quiet hours; the panel's own
  // badge shows either way.
  for (Status st : {Status::Open, Status::Busy, Status::Private, Status::Sleeping, Status::Away}) {
    Pair p;
    notice_start(p, notice_bit(NoticeKind::Mail));
    if (st != Status::Open) CHECK(p.b.choose_status(st));
    p.run(300);
    CHECK(p.a.tap_nomail());
    p.run(300);
    bool want = st == Status::Open || st == Status::Busy;
    CHECK(notices_of(p, NoticeKind::Mail) == (want ? 1 : 0));
    CHECK(p.b.mail().none && p.b.view().nomail.active);
  }
  {
    Pair p;
    notice_start(p, notice_bit(NoticeKind::Mail));
    p.pa.offset = p.pb.offset = 23 * 3600;  // quiet hours
    CHECK(p.a.tap_nomail());
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Mail) == 0 && p.b.mail().none);
  }
  {
    Pair p;  // the Mail kind switched off: no notice, the badge still works
    notice_start(p, 0);
    CHECK(p.a.tap_nomail());
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Mail) == 0 && p.b.mail().none);
  }
  // "Got it" on a phone: MAIL_ACK clears whichever is showing, on both panels, with no notice.
  {
    Pair p;
    notice_start(p, notice_bit(NoticeKind::Mail));
    CHECK(p.a.tap_nomail());
    p.run(300);
    CHECK(p.a.mail().none && p.b.mail().none);
    p.pa.out.clear();
    gw_mail_ack(p.a);
    CHECK(acked_to_gateway(p.pa, MsgType::MailAck));
    p.run(300);
    CHECK(!p.a.mail().none && !p.b.mail().none && !p.pa.saved_mail.none && !p.pb.saved_mail.none);
    CHECK(!p.b.view().nomail.show && !p.a.view().nomail.active);
    CHECK(notices_of(p, NoticeKind::Mail) == 1);
    uint32_t ver = p.a.mail().ver;
    gw_mail_ack(p.a);  // nothing showing: ignored
    p.run(300);
    CHECK(p.a.mail().ver == ver);
    CHECK(p.a.tap_nomail());
    p.run(300);
    gw_mail_ack(p.b);  // straight from the gateway to the downstairs panel: ignored, as for mail
    p.run(300);
    CHECK(p.a.mail().none && p.b.mail().none);
  }
  // A restart keeps it, on either panel, the way mail is kept.
  {
    Pair p;
    notice_start(p, notice_bit(NoticeKind::Mail));
    CHECK(p.a.tap_nomail());
    p.run(300);
    MailState saved = p.pa.saved_mail;
    CHECK(saved.none && !saved.waiting);
    FakePlatform fa;
    fa.w = p.pa.w;
    Controller a2(fa);
    a2.begin(p.sa, OwnState(), DogState(), saved);
    CHECK(a2.mail().none && a2.view().nomail.show && a2.view().nomail.active);
    FakePlatform fb;
    fb.w = p.pb.w;
    Controller b2(fb);
    b2.begin(p.sb, OwnState(), DogState(), p.pb.saved_mail);
    CHECK(b2.mail().none && b2.view().nomail.show && b2.view().nomail.active && !b2.view().mail.show);
  }
  // The sync both ways with the counter rule on two live panels: a change one panel missed comes
  // across when they hear each other again, and the later change wins.
  {
    Pair p;
    notice_start(p, notice_bit(NoticeKind::Mail));
    p.cut = true;
    CHECK(p.a.tap_nomail());
    p.run(3000);
    CHECK(p.a.mail().none && !p.b.mail().none);
    p.cut = false;
    p.b.choose_status(Status::Busy);  // downstairs says something, and upstairs catches it up
    p.run(500);
    CHECK(p.b.mail().none && p.a.mail().none);
    p.cut = true;
    CHECK(p.b.tap_nomail());  // acknowledged downstairs while upstairs can't hear
    p.run(3000);
    CHECK(!p.b.mail().none && p.a.mail().none);
    p.cut = false;
    p.b.choose_status(Status::Open);
    p.run(500);
    CHECK(!p.a.mail().none && !p.b.mail().none);
  }
}

static void test_phone_mail() {
  const uint8_t mail = notice_bit(NoticeKind::Mail);
  // The Mail notice pushes the downstairs phones: only while downstairs is Open or Busy.
  for (Status st : {Status::Open, Status::Busy, Status::Private, Status::Sleeping, Status::Away}) {
    Pair p;
    notice_start(p, mail);
    if (st != Status::Open) CHECK(p.b.choose_status(st));
    p.run(300);
    CHECK(p.a.tap_mail());
    p.run(300);
    bool want = st == Status::Open || st == Status::Busy;
    CHECK(notices_of(p, NoticeKind::Mail) == (want ? 1 : 0));
    CHECK(p.b.mail().waiting);  // the panel's badge either way
  }
  // Never in quiet hours.
  {
    Pair p;
    notice_start(p, mail);
    p.pa.offset = p.pb.offset = 23 * 3600;
    CHECK(p.a.tap_mail());
    p.run(300);
    CHECK(notices_of(p, NoticeKind::Mail) == 0 && p.b.mail().waiting);
  }
  // "Got it" on a phone: MAIL_ACK clears the mail on both panels, as the downstairs tap does.
  {
    Pair p;
    notice_start(p, mail);
    CHECK(p.a.tap_mail());
    p.run(300);
    CHECK(p.a.mail().waiting && p.b.mail().waiting && p.b.view().mail.pulse);
    p.pa.out.clear();
    gw_mail_ack(p.a);
    CHECK(acked_to_gateway(p.pa, MsgType::MailAck));
    p.run(300);
    CHECK(!p.a.mail().waiting && !p.b.mail().waiting && !p.pa.saved_mail.waiting && !p.pb.saved_mail.waiting);
    CHECK(!p.b.view().mail.solid && !p.b.view().mail.pulse);
    CHECK(notices_of(p, NoticeKind::Mail) == 1);  // no notice for the pick-up
    // Nothing waiting: ignored, no change at all.
    uint32_t ver = p.a.mail().ver;
    gw_mail_ack(p.a);
    p.run(300);
    CHECK(p.a.mail().ver == ver && !p.a.mail().waiting);
    // Mail again, and the downstairs panel given a MAIL_ACK straight from the gateway: ignored.
    CHECK(p.a.tap_mail());
    p.run(300);
    gw_mail_ack(p.b);
    p.run(300);
    CHECK(p.a.mail().waiting && p.b.mail().waiting);
  }
}

int main(int argc, char **argv) {
  const char *wav_dir = argc > 1 ? argv[1] : nullptr;
  test_sht4x();
  test_civil();
  test_status();
  test_dog();
  test_both_dogs();
  test_mail();
  test_comfort();
  test_comfort_chart();
  test_chart_view();
  test_update_both_and_pending();
  test_requests();
  test_request_ties();
  test_bell_shake_and_red();
  test_protocol();
  test_sky();
  test_golden_hour();
  test_sun_and_moon_position();
  test_note();
  test_outside_log();
  test_outside_sampling();
  test_outside_clock();
  test_outside_clock_recovery();
  test_catch_up_burst();
  test_update_from_gateway();
  test_note_recovery_and_acted();
  test_synth(wav_dir);
  test_boot_and_status();
  test_doorbell();
  test_knock_rules();
  test_no_signal();
  test_reboot_exchange();
  test_clock_copy();
  test_quiet_hours_and_backlight();
  test_demo_loopback();
  test_wraparound();
  test_dog_no_clock();
  test_reading_and_outside();
  test_zambretti();
  test_event_store();
  test_repeats();
  test_holidays();
  test_calendar_sync();
  test_archive();
  test_lost_acks();
  test_floor_swap();
  test_clock_unknown_is_quiet_and_dim();
  test_tile_agenda();
  test_dog_nudge();
  test_notice_wire();
  test_notice_kinds();
  test_settings_mask_migration();
  test_settings_mask_migration_v8();
  test_notice_defaults_mild_and_note();
  test_phone_wire();
  test_phone_ring_notice();
  test_phone_answered_notice();
  test_phone_answer();
  test_phone_mail();
  test_nomail();
  test_notice_mask_and_queue();
  test_notice_no_gateway();
  test_presence_filter();
  test_presence_settings_migration();
  test_presence_notice_mask_saved();
  test_presence_wire();
  test_presence();
  test_spare_loan();
  test_rooms_wire();
  test_rooms();
  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
