// The doorbell, both directions. HANDOFF: one tap sends a normal chime, no hold gesture,
// repeat taps ignored for 10 s, sender sees Ringing -> Delivered -> the reply, "No answer yet"
// after 60 s, "Couldn't reach" when every retry fails.
#pragma once
#include "types.h"
#include "settings.h"

namespace tc {

enum class BellOut : uint8_t { Idle, Ringing, Delivered, ComeOn, NotNow, NoAnswer, CouldntReach };

class BellSender {
 public:
  static constexpr uint32_t kRepeatIgnoreMs = 10000;   // HANDOFF
  static constexpr uint32_t kNoAnswerMs = 60000;       // HANDOFF
  static constexpr uint32_t kReplyShowMs = 30000;      // PROPOSED: how long the answer stays on the tile
  static constexpr uint32_t kFailShowMs = 15000;       // PROPOSED
  static constexpr uint32_t kNoAnswerShowMs = 120000;  // PROPOSED: then the tile goes back to idle

  // Returns the knock id to send, or 0 when the tap is ignored.
  uint16_t tap(uint32_t mono_ms, uint16_t next_id);
  void on_delivered(uint16_t knock_id, uint32_t mono_ms);
  void on_failed(uint16_t knock_id, uint32_t mono_ms);
  void on_reply(uint16_t knock_id, bool come_on, uint32_t mono_ms);
  void tick(uint32_t mono_ms);

  BellOut state() const { return state_; }
  uint16_t knock_id() const { return id_; }
  // True while taps are being ignored (the tile can show that it's busy).
  bool ignoring(uint32_t mono_ms) const;
  static constexpr uint32_t kRingingMaxMs = 5000;  // safety net: retries resolve within 1.5 s

 private:
  void enter(BellOut s, uint32_t mono_ms) { state_ = s; entered_ = mono_ms; }
  BellOut state_ = BellOut::Idle;
  uint16_t id_ = 0;
  bool tapped_ = false;
  uint32_t last_tap_ = 0;
  uint32_t entered_ = 0;
};

// What the receiving panel should do with an incoming knock.
struct KnockAction {
  bool banner = false;
  Chime chime = Chime::None;
  bool save = false;
  bool wake = false;  // HANDOFF: a doorbell wakes the screen at the current brightness level
};

KnockAction knock_action(const Settings &s, Status own);

enum class BannerKind : uint8_t { None, Knock, MissedWhileAway };

class BellReceiver {
 public:
  static constexpr uint32_t kBannerMs = 60000;  // PROPOSED: matches the sender's 60 s "No answer yet"

  // Returns the action; duplicates of the knock being shown return an empty action.
  KnockAction on_knock(uint16_t knock_id, Floor from, const Settings &s, Status own, uint32_t mono_ms, uint32_t utc);
  // The owner answered. Returns the knock id to reply to, or 0.
  uint16_t answer(uint32_t mono_ms);
  void dismiss();
  // The owner's status changed; leaving Away brings back a saved knock.
  void status_changed(Status from, Status to, uint32_t mono_ms);
  void tick(uint32_t mono_ms);

  BannerKind banner() const { return banner_; }
  Floor from() const { return from_; }
  uint32_t knock_utc() const { return utc_; }  // when it rang, for "while you were away" texts
  bool missed() const { return missed_; }         // an unanswered knock to mention on the tile
  uint32_t missed_utc() const { return missed_utc_; }  // when, or 0 if the clock wasn't set
  void clear_missed() {
    missed_ = false;
    missed_utc_ = 0;
  }

 private:
  BannerKind banner_ = BannerKind::None;
  uint16_t id_ = 0;
  Floor from_ = Floor::Upstairs;
  uint32_t shown_ = 0;
  uint32_t utc_ = 0;
  bool saved_ = false;
  uint16_t saved_id_ = 0;
  uint32_t saved_utc_ = 0;
  Floor saved_from_ = Floor::Upstairs;
  bool missed_ = false;
  uint32_t missed_utc_ = 0;
};

}  // namespace tc
