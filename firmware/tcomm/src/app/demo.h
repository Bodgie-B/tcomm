// Demo mode for phase 2 ("interface alone, with fake data"): a simulated other panel (a second
// Controller with the same code), a loopback radio between the two, and a fake WeatherGate /
// LucyGate gateway. Lets one board, the host tests and the screenshot simulator exercise the
// doorbell, No signal, the dog value and the boot exchange end to end.
#pragma once
#include "controller.h"
#include "mem_files.h"

namespace tc {

class Demo;

// Platform for the simulated panel: shares the real panel's clocks, has no screen or speaker,
// and keeps its "flash" in memory so an unplug/replug restores it like a real reboot.
class DemoPeerPlatform : public Platform {
 public:
  DemoPeerPlatform(Platform &host, Demo &demo) : host_(host), demo_(demo) {}
  uint32_t mono_ms() override { return host_.mono_ms(); }
  WallTime wall() override { return host_.wall(); }
  void set_wall(uint32_t, uint8_t) override {}  // shares the real panel's clock
  int32_t utc_offset(uint32_t utc) override { return host_.utc_offset(utc); }
  void radio_send(uint8_t to, const uint8_t *data, size_t len) override;
  void play(Sound, uint8_t) override {}
  void set_backlight(uint8_t) override {}
  void save_own(const OwnState &s) override { own = s; }
  void save_dog(const DogState &d) override { dog = d; }
  void save_mail(const MailState &m) override { mail = m; }
  uint32_t random32() override { return host_.random32(); }
  void *alloc_big(size_t n) override { return host_.alloc_big(n); }
  void free_big(void *p) override { host_.free_big(p); }
  uint8_t *read_file(const char *name, size_t &len) override { return files.read(*this, name, len); }
  void write_file(const char *name, uint8_t *buf, size_t len) override { files.write(*this, name, buf, len); }
  MemFiles files;  // kept over "plug in", like the real panel's SD card
  OwnState own;
  DogState dog;
  MailState mail;

 private:
  Platform &host_;
  Demo &demo_;
};

enum class AutoAnswer : uint8_t { ComeOn, NotNow, Off };

class Demo {
 public:
  explicit Demo(Platform &host);
  ~Demo();
  Demo(const Demo &) = delete;
  Demo &operator=(const Demo &) = delete;

  // Wire the real panel's controller in. The real panel's Platform::radio_send must call
  // from_panel() while the demo is on.
  void begin(Controller &panel);
  void tick();  // delivers queued frames, ticks the simulated panel, answers its banner

  void from_panel(uint8_t to, const uint8_t *data, size_t len);  // the real panel transmitted
  void from_peer(uint8_t to, const uint8_t *data, size_t len);   // the simulated panel transmitted

  // Test panel actions
  void peer_next_status();
  void peer_toggle_unplugged();           // unplug, or plug back in (which reboots it)
  void peer_ring();                        // the other floor presses DOORBELL
  void peer_toggle_dog();                  // the other floor taps its dog button
  void peer_tap_mail();                    // the other floor taps its mail badge
  void gateway_toggle_lucy();              // LucyGate in/out button
  void gateway_next_outside();             // a WeatherGate reading (the board samples, then Zambretti ones)
  void next_auto_answer();
  int next_phase();                        // cycles the six phases, then back to the real clock

  // For screenshots and tests: put the simulated panel in a given state (reboots it), and send
  // a specific WeatherGate reading.
  void set_peer(const OwnState &s, bool has_reading, int16_t temp_tenths_f, uint16_t hum_tenths);
  // warn: kOutsideDangerHeat and/or kOutsideDangerCold.
  void gateway_outside(int16_t temp_tenths_f, uint8_t hum, Forecast f, const char *phrase, uint8_t warn = 0);

  bool unplugged() const { return cut_; }
  AutoAnswer auto_answer() const { return auto_; }
  int phase() const { return phase_; }
  Controller &peer() { return *peer_; }

 private:
  struct Frame { uint8_t data[kMaxMsg]; uint8_t len; bool to_panel; };
  void queue(bool to_panel, const uint8_t *data, size_t len);
  void gateway_send(Msg m);
  void boot_peer();

  Platform &host_;
  DemoPeerPlatform pp_;
  Controller *peer_ = nullptr;  // rebuilt on "plug in" so it really forgets everything but its flash
  Controller *panel_ = nullptr;
  Settings peer_settings_;
  Frame q_[16];
  Frame batch_[16];  // not on the stack: the LVGL task's stack is small
  int q_n_ = 0;
  bool cut_ = false;
  AutoAnswer auto_ = AutoAnswer::ComeOn;
  uint32_t banner_seen_ = 0;
  bool banner_pending_ = false;
  uint32_t mail_seen_ = 0;  // when the simulated tenant's mail badge came up
  bool mail_pending_ = false;
  int status_i_ = 0;
  int outside_i_ = 0;
  bool lucy_out_ = false;
  int phase_ = 6;
  uint16_t gw_seq_ = 1;
  bool peer_reading_ = true;
  int16_t peer_temp_ = 690;
  uint16_t peer_hum_ = 440;
};

}  // namespace tc
