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
#include "voice_session.h"

namespace gw {

// 0.7 (Mike, 2026-11-22): the ROOMS frame from the upstairs panel, printed as a "rooms" event line.
constexpr const char *kFirmware = "gateway-c6 0.7";
constexpr size_t kMaxLine = 512;  // the bridge's lines are far shorter; anything longer is dropped
constexpr int kChannel = 1;       // TCOMM_ESPNOW_CHANNEL
// The serial write gate (DESIGN 5.1, check H2). A voice chunk line is written only if the port can
// take it and kVoiceDataReserve more bytes (two full control lines plus a margin); the other voice
// lines (begin, end, abort, voice_msg) leave kVoiceCtlReserve free, which is more than a notice
// event plus a reply (about 420 bytes). So a doorbell notice or a request's reply is never cut or
// lost behind a voice stream.
//
// 0.6 (mu-law, 255 chunks): a mu-law chunk line is the same size as an ADPCM one (237 data bytes, 316
// base64 characters, pred 0 and step 0, about 390 bytes at most 400), so the line limit and the two
// reserves are unchanged, but a stream is 68 lines a second (26 KB/s) instead of 34.
constexpr int kVoiceDataReserve = 1100;
constexpr int kVoiceCtlReserve = 600;
constexpr uint32_t kVoiceAckMaxMs = 300;   // a due gap/duplicate ACK waits at most this long after the first DATA
constexpr uint32_t kVoiceIdleMs = 2000;  // a session with no frame for this long is aborted

// The panels, by their sender byte.
enum Panel : int { kUpstairs = tc::kSenderUpstairs, kDownstairs = tc::kSenderDownstairs };
const char *panel_name(int p);  // "upstairs", "downstairs"

class GatewayIO {
 public:
  virtual ~GatewayIO() = default;
  virtual uint32_t now_ms() = 0;
  // One try of a frame over ESP-NOW to a panel: to is kUpstairs or kDownstairs (its sender byte).
  virtual void send_frame(int to, const uint8_t *data, size_t len) = 0;
  // One line to the Pi, without its newline.
  virtual void reply(const char *line) = 0;
  // Bytes the serial port can take now without waiting (HWCDC availableForWrite). Voice lines are
  // written only when this leaves room for the control lines (the write gate, DESIGN 5.1).
  virtual int tx_room() { return 1 << 20; }
  // Radio frames the receive queue had to drop since the reset (the hello and the screen show it).
  virtual uint32_t rx_drops() { return 0; }
  // The downstairs panel is registered as an ESP-NOW peer with its key (the hello's down_peer).
  virtual bool down_peer() { return true; }
};

// What the LCD shows.
struct GwStatus {
  bool pi_seen = false;           // a line has come from the bridge since the reset
  uint32_t requests = 0, acked = 0, failed = 0, rejected = 0;
  char last[40] = "no requests yet";  // the last result, for the screen (20 characters fit)
  uint32_t voice_lines = 0;  // chunk lines written to the Pi
  uint32_t voice_held = 0;   // chunks not written (no room, or a request waiting), left for the panel to resend
  uint32_t voice_bad = 0;    // voice frames that did not decode or did not belong
  uint32_t voice_capped = 0; // own voice frames not sent: over the air cap (field fix 2026-11-20)
  uint32_t voice_hellos = 0; // voice hellos sent to upstairs
};

class Gateway {
 public:
  // mac: this board's Wi-Fi station MAC as text ("aa:bb:..."). first_seq: where the ESP-NOW seq
  // counter starts (random at each boot on the C6; fixed in the tests).
  Gateway(GatewayIO &io, const char *mac, uint16_t first_seq);

  void boot();                                 // the first line after a reset: boot true
  void on_line(const char *line, size_t len);  // one line from the Pi, without its newline
  // A frame that came in over ESP-NOW. from: the sender's role by its MAC (kUpstairs,
  // kDownstairs), or -1 for any other MAC. ACKs answer the requests; a NOTICE from the upstairs
  // panel (a house event for the Pi, Mike 2026-11-11) is ACKed and printed as one "event" line,
  // once however many times it is sent; anything else is ignored.
  void on_frame(int from, const uint8_t *data, size_t len);
  void tick();  // retries, and the give-ups; call often (every few ms)

  const GwStatus &status() const { return st_; }
  int in_flight() const;  // requests waiting for an ACK or a VOICE_GOT (0 to 10)
  const VoiceSession &voice_session(int floor) const { return vs_[floor ? 1 : 0]; }

 private:
  // kUpdate: the Pi's "start both panels' Wi-Fi updaters" (Mike, 2026-11-07; firmware/docs/remote-update.md).
  // kAnswerUp, kAnswerDown: a phone's answer to a ring, one slot per RUNG floor, so an answer for
  // one floor never cuts off an answer for the other (both are "answer" on the serial line).
  // kMailAck: a phone's "Got it" on the mail (Mike, 2026-11-15).
  // Voice notes (DESIGN 5.2): kVoiceTextUp/Down carry the Pi's recognised text to a floor (VOICE_TEXT),
  // kVoiceAckUp/Down a phone's Got it (VOICE_DISMISS). They are matched by the panel's VOICE_GOT
  // (kind, key), not by an ACK seq. The text slots are latest-wins per floor; the dismiss slots refuse
  // while busy.
  enum Kind {
    kOutside = 0, kLucy = 1, kUpdate = 2, kAnswerUp = 3, kAnswerDown = 4, kMailAck = 5,
    kVoiceTextUp = 6, kVoiceTextDown = 7, kVoiceAckUp = 8, kVoiceAckDown = 9
  };
  static constexpr int kKinds = 10;
  static bool is_voice_kind(int k) { return k >= kVoiceTextUp; }
  struct Pending {
    bool used = false;
    int64_t id = 0;
    uint16_t seq = 0;
    uint8_t frame[tc::kMaxMsg];
    size_t len = 0;
    int tries = 0;
    uint32_t first = 0, last = 0;
    uint8_t to = kUpstairs;     // the panel the frame is sent to (a voice frame for downstairs may go via upstairs)
    uint8_t floor = kUpstairs;  // the panel the reply names (the floor, for a voice frame)
    uint8_t got_kind = 0;       // voice kinds: the VOICE_GOT kind that confirms it (25 or 29)
    uint32_t key = 0;           // and its key
  };

  void hello(bool boot);
  void send_try(Pending &p);
  void finish(Kind k, const char *line);
  void on_notice(const tc::Msg &m);
  void on_rooms(const tc::Msg &m);
  // ---- Voice notes (DESIGN 4 and 5; the code is in gateway.cpp, the session state in voice_session.h) ----
  void on_voice_frame(int from, const uint8_t *data, size_t len);
  void voice_hello(uint32_t now);  // VOICE_GOT kind 30, key tc::kVoiceHelloKeyMulaw (0.6: mu-law too), to upstairs
  void voice_request(Kind k, const JObject &o, int64_t id);
  void voice_tick(uint32_t now);
  void voice_begin_frame(int floor, uint8_t via, const tc::VoiceMsg &m, uint32_t now);
  void voice_data_frame(int floor, uint8_t via, const tc::VoiceMsg &m, uint32_t now);
  void voice_end_frame(int floor, const tc::VoiceMsg &m, uint32_t now);
  void voice_abort_frame(int floor, const tc::VoiceMsg &m);
  void voice_got_frame(int floor, const tc::VoiceMsg &m);
  void voice_push_frame(int floor, uint8_t via, const tc::VoiceMsg &m);
  bool print_begin(int floor);
  bool print_abort(int floor, uint8_t reason);
  bool try_finish(int floor);
  void send_ack(int floor, uint8_t state);  // for the floor's session (1 open, 2 complete)
  void send_got(int floor, uint8_t via, uint8_t kind, uint32_t key, uint8_t result);
  void send_abort(int floor, uint8_t via, uint8_t xfer, uint8_t reason);
  void send_voice_to(int floor, uint8_t via, tc::VoiceMsg &m);
  bool write_line(const char *line, int reserve);
  bool request_waiting() const;  // a request of the first six kinds is waiting for its ACK
  uint8_t route_to(int floor, uint8_t via) const { return floor == 0 ? (uint8_t)kUpstairs : via == kViaDirect ? (uint8_t)kDownstairs : (uint8_t)kUpstairs; }
  uint16_t next_seq();
  static const char *kind_name(Kind k) {
    switch (k) {
      case kOutside: return "outside";
      case kLucy: return "lucy";
      case kUpdate: return "update";
      case kAnswerUp:
      case kAnswerDown: return "answer";
      case kMailAck: return "mail_ack";
      case kVoiceTextUp:
      case kVoiceTextDown: return "voice_text";
      case kVoiceAckUp:
      case kVoiceAckDown: return "voice_ack";
    }
    return "update";
  }
  // The name on the screen's last-result row, which fits 20 characters: "mail_ack acked 1150ms"
  // would be 21, so that kind shows as "mail". The serial replies keep kind_name.
  static const char *screen_name(Kind k) {
    switch (k) {
      case kMailAck: return "mail";
      case kVoiceTextUp:
      case kVoiceTextDown: return "vtext";
      case kVoiceAckUp:
      case kVoiceAckDown: return "vack";
      default: return kind_name(k);
    }
  }

  GatewayIO &io_;
  char mac_[18];
  uint16_t seq_;
  Pending p_[kKinds];  // one per type: OUTSIDE, LUCY, UPDATE, PHONE_ANSWER (one per floor), MAIL_ACK, and
                       // the voice text and dismiss slots (one per floor each)
  tc::Dedup notices_;  // a NOTICE the panel sends again (our ACK was lost) is printed once
  tc::Dedup rooms_;    // and a ROOMS likewise (0.7), kept apart so a burst of ROOMS retries never pushes out a notice
  GwStatus st_;
  // Voice notes. vs_: one session per floor. down_via_: the route of the last frame heard from
  // downstairs (upstairs until it has been heard directly), for the C6's own frames (VOICE_DISMISS).
  VoiceSession vs_[2];
  uint8_t down_via_ = kViaUpstairs;
  bool pi_voice_ = false;  // the bridge's hello said "voice":1 since the reset
  uint32_t seen_push_[tc::kVoiceSeenIds] = {};  // the last 8 voice_msg ids printed
  uint8_t seen_next_ = 0;
  // A VOICE_ABORT to a panel is sent three times, at 0, 100 and 250 ms (DESIGN 4.3); one per floor.
  struct AbortOut {
    bool used = false;
    uint8_t to = 0, len = 0, sent = 0;
    uint8_t frame[tc::kVoiceAbortLen];
    uint32_t first = 0;
  };
  AbortOut ab_[2];
  // Field fix 2026-11-20 (firmware/docs/voice-notes/WORKLOG.md). The panels send this C6 nothing voice-related
  // until they hear its hello, which goes to upstairs only (the proven link) at start-up and every
  // tc::kVoiceHelloEveryMs. Downstairs' pings are answered at most once per kVoicePingAnswerGapMs, and
  // every voice frame this C6 makes (not the 3 copies of an abort) goes through vcap_, so no panel
  // bug can make it flood the air.
  static constexpr uint32_t kVoicePingAnswerGapMs = 250;
  bool hello_sent_ = false;
  uint32_t hello_at_ = 0;
  bool ping_answered_ = false;
  uint32_t ping_answer_at_ = 0;
  tc::VoiceRateCap vcap_{tc::kVoiceGwPerS, tc::kVoiceGwBurst};
};

}  // namespace gw
