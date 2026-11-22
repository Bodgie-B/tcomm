// Voice notes: the panel's side of the link (docs/voice-notes/DESIGN.md 4.4, 4.5; piece B).
// Plain C++, host-tested (tests/test_voice_link.cpp). Runs on the LVGL task from the controller's
// 50 ms tick; the mic task makes every chunk's FIRST send and sends END itself (voice_proto.h,
// VoiceRec), so a long redraw never delays the stream.
//
// VoiceTx: one upload at a time. BEGIN (resent every 150 ms until any VOICE_ACK; on the downstairs
// auto route, sent via upstairs from 450 ms; given up at 1.5 s), resends of chunks the latest
// VOICE_ACK bitmap still misses (first sent 200 ms ago or more, oldest first, at most 4 a tick, 6
// sends a chunk, all resends at most 2 x the chunks sent, never at or past first_sent; in rounds with a
// quiet gap after each, and a probe when base is stuck: field fix 2026-11-21), END resent
// every 200 ms until state 2 (1.5 s), base stalled 1.5 s while chunks are owed, the text wait (8 s
// after state 2; a Queued status extends it once to 20 s), and VOICE_ABORT sent 3 times (0, 100,
// 250 ms) when a job is given up or cancelled.
//
// VoiceOut: 4 retry slots for SAY and VOICE_PUSH, tries at 0/150/400/750/1150 ms, given up at 1.5 s
// (the RetryQueue schedule), cleared by the matching VOICE_GOT.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "voice_proto.h"

namespace tc {

// The serial reach test's VOICE_PING nonces carry this in their top 16 bits ("RE"; piece R,
// board_radio.cpp), so the controller's own route pings never use it and never count for the test.
constexpr uint16_t kReachPingTag = 0x5245;

// Where voice frames go out. The controller passes its Platform::radio_send_voice through this.
class VoiceLink {
 public:
  virtual ~VoiceLink() {}
  virtual bool voice_send(uint8_t to, const uint8_t *d, size_t n) = 0;
};

// Timings (DESIGN 4.5), constants so they are cheap to change.
constexpr uint32_t kVoiceBeginEveryMs = 150;    // BEGIN resend spacing
constexpr uint32_t kVoiceBeginFallbackMs = 450; // downstairs auto: no answer direct -> via upstairs
constexpr uint32_t kVoiceBeginGiveUpMs = 1500;  // no answer at all: "Gateway needs an update"
constexpr uint32_t kVoiceResendAfterMs = 200;   // a chunk is resent once its last send is this old
constexpr int kVoiceResendsPerTick = 4;
// Resends go in rounds (field fix 2026-11-21, found with mu-law's 68 chunks a second): a round is one
// pass over the owed chunks, and after a round that sent anything the next waits this long. The C6 (0.5) sends its gap or duplicate VOICE_ACK only
// 80 ms after the LAST DATA it saw, so a steady trickle of resends (each chunk on its own 200 ms clock)
// could hold that ACK back until the chunks ran out of sends, with the window full and nothing new to go.
constexpr uint32_t kVoiceResendQuietMs = 160;
// The probe: base stuck this long with chunks owed and no resend due -> one received chunk goes again
// (at most every kVoiceProbeEveryMs) so the C6 says where it is. Before the 1.5 s stall gives up.
constexpr uint32_t kVoiceProbeAfterMs = 400, kVoiceProbeEveryMs = 300;
constexpr int kVoiceSendsPerChunk = 6;          // the first send included (5 until 2026-11-21)
constexpr uint32_t kVoiceEndEveryMs = 200;
constexpr uint32_t kVoiceEndGiveUpMs = 1500;
constexpr uint32_t kVoiceStallMs = 1500;        // base not moving while chunks are owed
constexpr uint32_t kVoiceTextWaitMs = 8000;     // after VOICE_ACK state 2
constexpr uint32_t kVoiceQueuedWaitMs = 20000;  // once, on a Queued status
constexpr int kVoiceAbortSends = 3;
constexpr uint32_t kVoiceAbortAtMs[kVoiceAbortSends] = {0, 100, 250};

enum class VoiceTxPhase : uint8_t {
  Idle = 0,
  Begin = 1,     // BEGIN out, no VOICE_ACK yet
  Stream = 2,    // the C6 has the session; chunks flowing (capture may still run)
  Ending = 3,    // END out (the mic task is done), waiting for VOICE_ACK state 2
  WaitText = 4,  // the C6 has every chunk; waiting for VOICE_TEXT
  Done = 5,      // text arrived (status Ok)
  Failed = 6     // err() says why (None: cancelled)
};

enum class VoiceTextIs : uint8_t { NotMine = 0, Interim = 1, Final = 2, Duplicate = 3 };

class VoiceTx {
 public:
  // A new job. rec: already built (voice_rec_init) and filled; the controller publishes it to the mic
  // task. to: the first hop now (the gateway, or upstairs on the relay route). fallback: the hop to try
  // from 450 ms when nothing has answered (upstairs on the downstairs auto route), or -1. Sends the
  // first BEGIN.
  void start(VoiceRec *rec, VoicePurpose purpose, uint8_t to, int fallback, uint32_t now, VoiceLink &l);
  void tick(uint32_t now, VoiceLink &l);
  // Frames for this panel's floor, already decoded (the controller checks floor and sender).
  void on_ack(const VoiceMsg &m, uint32_t now);
  void on_abort(const VoiceMsg &m, uint32_t now);
  VoiceTextIs on_text(const VoiceMsg &m, uint32_t now);
  // CANCEL, the bell, a closed recorder: VOICE_ABORT (3 sends) unless the job had already ended.
  void cancel(VoiceAbort why, uint32_t now, VoiceLink &l);
  // The clip is being freed: no more resends from it. Only after the job has ended or been cancelled.
  void forget_rec() { rec_ = nullptr; }
  // Back to Idle (after the result was taken). Pending ABORT sends carry on.
  void reset();

  VoiceTxPhase phase() const { return phase_; }
  bool active() const { return phase_ >= VoiceTxPhase::Begin && phase_ <= VoiceTxPhase::WaitText; }
  bool ended() const { return phase_ == VoiceTxPhase::Done || phase_ == VoiceTxPhase::Failed; }
  VoiceErr err() const { return err_; }
  bool waiting() const { return queued_; }  // a Queued status came: "Waiting for the other floor's message"
  uint8_t xfer() const { return xfer_; }
  VoicePurpose purpose() const { return purpose_; }
  const char *text() const { return text_; }
  uint8_t text_flags() const { return flags_; }
  uint8_t to() const { return to_; }
  bool fell_back() const { return fell_back_; }  // the BEGIN went via upstairs after the direct tries
  bool direct_failed() const { return direct_failed_; }

  // For the `voice` serial report and the host tests.
  struct Stats {
    uint32_t begin_sends, resends, end_sends, refused;
    uint32_t probes;     // resends of a received chunk to get the C6's VOICE_ACK (included in resends)
    uint8_t chunks;      // first_sent at the end
    uint32_t ms_to_text;  // from the moment capture ended (or state 2) to the text; 0 if none
  };
  const Stats &stats() const { return stats_; }

 private:
  void send_begin(uint32_t now, VoiceLink &l);
  void send_end(uint32_t now, VoiceLink &l);
  void resend(uint32_t now, VoiceLink &l);
  void resend_one(int idx, uint32_t now, VoiceLink &l);
  void fail(VoiceErr e, bool abort, VoiceAbort why, uint32_t now);
  void tick_aborts(uint32_t now, VoiceLink &l);
  bool acked(int idx) const;
  bool send(VoiceLink &l, VoiceMsg &m);

  VoiceRec *rec_ = nullptr;
  VoiceTxPhase phase_ = VoiceTxPhase::Idle;
  VoiceErr err_ = VoiceErr::None;
  VoicePurpose purpose_ = VoicePurpose::Dictation;
  uint8_t xfer_ = 0, me_ = 0, floor_ = 0, to_ = kSenderGateway;
  int fallback_ = -1;
  bool fell_back_ = false, direct_failed_ = false;
  uint32_t start_ms_ = 0, begin_last_ = 0;
  uint8_t base_ = 0;
  uint32_t bits_ = 0;
  uint32_t base_moved_ = 0;
  uint32_t end_first_ = 0, end_last_ = 0;
  uint32_t text_deadline_ = 0;
  uint32_t capture_end_ms_ = 0;
  bool queued_ = false, extended_ = false, final_ = false;
  uint16_t seq_ = 0;
  uint8_t resent_[kVoiceMaxChunks] = {};
  uint32_t last_ms_[kVoiceMaxChunks] = {};
  uint32_t resends_total_ = 0;
  bool round_ = false;          // a resend round (one pass, oldest first) is under way
  int round_next_ = 0;          // the next chunk the round looks at
  int round_sent_ = 0;          // resends in this round so far
  uint32_t round_end_ = 0;      // when the last round that sent anything ended (0: none yet)
  uint32_t probe_last_ = 0;     // the last probe (0: none)
  char text_[kVoiceTextMax + 1] = {};
  uint8_t flags_ = 0;
  Stats stats_ = {};
  // The ABORT sender: survives reset() so a cancelled job still tells the C6.
  uint8_t abort_xfer_ = 0, abort_to_ = 0, abort_reason_ = 0, abort_floor_ = 0;
  int abort_n_ = kVoiceAbortSends;  // sends done; kVoiceAbortSends = none pending
  uint32_t abort_t0_ = 0;
};

// SAY and VOICE_PUSH retries (DESIGN 4.5).
constexpr int kVoiceOutSlots = 4;
constexpr int kVoiceOutTries = 5;
constexpr uint32_t kVoiceOutAtMs[kVoiceOutTries] = {0, 150, 400, 750, 1150};
constexpr uint32_t kVoiceOutGiveUpMs = 1500;

class VoiceOut {
 public:
  // frame: the whole encoded SAY or PUSH. kind: its type (26 or 28), key: its id. False when every
  // slot is in use (nothing queued). The first try goes out at the next tick.
  bool add(uint8_t to, uint8_t kind, uint32_t key, const uint8_t *frame, size_t n, uint32_t now);
  void tick(uint32_t now, VoiceLink &l);
  // A VOICE_GOT: clears the matching slot; true if one matched. result is passed back for the caller.
  bool on_got(uint8_t kind, uint32_t key);
  // One slot whose tries ran out (1.5 s after it was added), removed; false when none.
  bool expired(uint32_t now, uint8_t &kind, uint32_t &key);
  bool pending(uint8_t kind, uint32_t key) const;
  int used() const;
  void clear();

 private:
  struct Slot {
    bool used;
    uint8_t to, kind, tries, len;
    uint32_t key, t0;
    uint8_t frame[kVoicePushBase + kVoiceTextMax];
  };
  Slot s_[kVoiceOutSlots] = {};
};

// The VOICE_GOT the panel sends back for a TEXT, SAY or DISMISS. Returns the encoded length (0 on a bad
// kind).
size_t voice_make_got(uint8_t me, uint8_t floor, uint16_t seq, uint8_t kind, uint32_t key, VoiceGotResult r,
                      uint8_t *out, size_t cap);

}  // namespace tc
