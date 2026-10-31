// The gateway's logic: the Pi bridge's request lines in, TComm frames out to the upstairs panel,
// the panels' ACKs back, and one final reply per request. The radio side follows
// gateway/docs/gateway-brief.md; the serial lines follow the bridge (tcomm_bridge.py in WeatherGate).
// Plain C++: the C6 sketch supplies the clock, the radio and the serial port, and the host tests
// drive it the same way.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "core/protocol.h"
#include "json_line.h"

namespace gw {

constexpr const char *kFirmware = "gateway-c6 0.1";
constexpr size_t kMaxLine = 512;  // the bridge's lines are far shorter; anything longer is dropped
constexpr int kChannel = 1;       // TCOMM_ESPNOW_CHANNEL

// The panels, by their sender byte.
enum Panel : int { kUpstairs = tc::kSenderUpstairs, kDownstairs = tc::kSenderDownstairs };
const char *panel_name(int p);  // "upstairs", "downstairs"

class GatewayIO {
 public:
  virtual ~GatewayIO() = default;
  virtual uint32_t now_ms() = 0;
  // One try of a frame over ESP-NOW to the upstairs panel.
  virtual void send_frame(const uint8_t *data, size_t len) = 0;
  // One line to the Pi, without its newline.
  virtual void reply(const char *line) = 0;
};

// What the LCD shows.
struct GwStatus {
  bool pi_seen = false;           // a line has come from the bridge since the reset
  uint32_t requests = 0, acked = 0, failed = 0, rejected = 0;
  char last[40] = "no requests yet";  // the last result, for the screen (20 characters fit)
};

class Gateway {
 public:
  // mac: this board's Wi-Fi station MAC as text ("aa:bb:..."). first_seq: where the ESP-NOW seq
  // counter starts (random at each boot on the C6; fixed in the tests).
  Gateway(GatewayIO &io, const char *mac, uint16_t first_seq);

  void boot();                                 // the first line after a reset: boot true
  void on_line(const char *line, size_t len);  // one line from the Pi, without its newline
  // A frame that came in over ESP-NOW. from: the sender's role by its MAC (kUpstairs,
  // kDownstairs), or -1 for any other MAC. Only ACKs matter; anything else is ignored.
  void on_frame(int from, const uint8_t *data, size_t len);
  void tick();  // retries, and the give-ups; call often (every few ms)

  const GwStatus &status() const { return st_; }
  int in_flight() const;  // requests waiting for an ACK (0 to 2)

 private:
  enum Kind { kOutside = 0, kLucy = 1 };
  struct Pending {
    bool used = false;
    int64_t id = 0;
    uint16_t seq = 0;
    uint8_t frame[tc::kMaxMsg];
    size_t len = 0;
    int tries = 0;
    uint32_t first = 0, last = 0;
  };

  void hello(bool boot);
  void send_try(Pending &p);
  void finish(Kind k, const char *line);
  uint16_t next_seq();
  static const char *kind_name(Kind k) { return k == kOutside ? "outside" : "lucy"; }

  GatewayIO &io_;
  char mac_[18];
  uint16_t seq_;
  Pending p_[2];  // one per type: OUTSIDE, LUCY
  GwStatus st_;
};

}  // namespace gw
