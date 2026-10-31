// Host tests for the TComm core and controller. Build and run (Git Bash, from the firmware folder):
//   g++ -std=c++17 -O1 -Wall -Wextra -I tcomm/src tests/test_core.cpp
//       tcomm/src/core/*.cpp tcomm/src/app/*.cpp -o "$LOCALAPPDATA/tcomm/host/tests.exe"
//   (one command; split here for reading)
//   "$LOCALAPPDATA/tcomm/host/tests.exe" docs/sounds
// The optional argument is a folder for WAV exports of the four synthesized sounds.
#include "app/controller.h"
#include "app/demo.h"
#include "core/civil.h"
#include "core/events.h"
#include "core/holidays.h"
#include "app/mem_files.h"
#include "core/brightness.h"
#include "core/sht4x.h"
#include "core/sky.h"
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
  DogState saved_dog;
  MailState saved_mail;
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
  void save_dog(const DogState &d) override { saved_dog = d; }
  void save_mail(const MailState &m) override { saved_mail = m; }
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
        if (s.to == kSenderDownstairs) b.on_radio(s.bytes.data(), s.bytes.size());
      }
      if (cut_ba) continue;
      for (auto &s : ob) {
        Msg m;
        if (drop && decode(s.bytes.data(), s.bytes.size(), m) && drop(false, m)) continue;
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
  CHECK(sky_at(day + 17 * 3600, -4 * 3600, 38.9f, -77.0f).phase == Phase::Golden);
  CHECK(sky_at(day + 18 * 3600 + 30 * 60, -4 * 3600, 38.9f, -77.0f).phase == Phase::Dusk);
  CHECK(sky_at(day + 21 * 3600, -4 * 3600, 38.9f, -77.0f).phase == Phase::Night);
  Sky noon = sky_at(day + 12 * 3600 + 45 * 60, -4 * 3600, 38.9f, -77.0f);
  CHECK(noon.p > 0.4f && noon.p < 0.6f && !noon.moon);
  // Far north in midsummer: no sunset. Falls back instead of failing.
  CHECK(!sun_times(2026, 6, 21, 0, 78.0f, 15.0f).ok);
  CHECK(sky_at(seconds_from_civil(2026, 6, 21, 12, 0, 0), 0, 78.0f, 15.0f).phase != Phase::Night);
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

  // Nobody answers: "No answer yet" after 60 s, and the receiver's banner times out into a missed note.
  p.run(11000);
  p.a.tap_bell();
  p.run(61000);
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

  // The message: 14 bytes; waiting is 0 or 1.
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
  buf[5] = 2;
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
  CHECK(backlight_for(ss, bi) == kBacklightOff);
  bi.mail_wake = true;
  CHECK(backlight_for(ss, bi) == night_backlight(ss.night_level));

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
  // Unanswered: still red after the banner has gone (60 s), then plain at 2 minutes.
  p.run(BellSender::kRepeatIgnoreMs);
  p.a.tap_bell();
  p.run(200);
  CHECK(p.b.view().bell.alert && p.b.view().bell.shakes == (uint16_t)(b0 + 2));
  p.run(70000);
  CHECK(p.b.view().banner.kind == BannerKind::None && p.b.view().bell.alert);
  p.run(50000);
  CHECK(!p.b.view().bell.alert);
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
  // reading's own time, so a reading stamped 20 minutes ago (a Pi clock running behind) still shows,
  // and so does one with a time of 0 (the Pi not synced yet), for 15 minutes from its arrival.
  o.seq = 78;
  o.u.outside.time = p.pa.w.utc - 20 * 60;
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
  // After a power cut (no coin cell, so both clocks lost), upstairs takes the Pi's time from the
  // gateway's weather message (Mike, 2026-10-31); never from a time of 0, and a set clock stays.
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
    CHECK(p.pa.w.quality == kClockFromPeer);  // below a clock set by hand
    o.seq = 82;
    o.u.outside.time = real + 4000;
    n = encode(o, buf, sizeof(buf));
    p.a.on_radio(buf, n);
    p.run(200);
    CHECK(p.pa.w.utc < real + 1000);
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

int main(int argc, char **argv) {
  const char *wav_dir = argc > 1 ? argv[1] : nullptr;
  test_sht4x();
  test_civil();
  test_status();
  test_dog();
  test_both_dogs();
  test_mail();
  test_bell_shake_and_red();
  test_protocol();
  test_sky();
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
  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
