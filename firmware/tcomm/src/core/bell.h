// The doorbell, both directions. HANDOFF: one tap sends a normal chime, no hold gesture,
// repeat taps ignored for 10 s, sender sees Ringing -> Delivered -> the reply, "No answer yet"
// after 2 minutes (Mike, 2026-11-15: was 60 s; the phones need the time), "Couldn't reach" when
// every retry fails.
#pragma once
#include "types.h"
#include "settings.h"
#include "status.h"

namespace tc {

enum class BellOut : uint8_t { Idle, Ringing, Delivered, ComeOn, NotNow, NoAnswer, CouldntReach };

class BellSender {
 public:
  static constexpr uint32_t kRepeatIgnoreMs = 10000;   // HANDOFF
  static constexpr uint32_t kNoAnswerMs = 120000;      // Mike, 2026-11-15: a 2-minute ring (was 60 s)
  static constexpr uint32_t kReplyShowMs = 30000;      // PROPOSED: how long the answer stays on the tile
  static constexpr uint32_t kFailShowMs = 15000;       // PROPOSED
  static constexpr uint32_t kNoAnswerShowMs = 120000;  // PROPOSED: then the tile goes back to idle

  // Returns the knock id to send, or 0 when the tap is ignored.
  uint16_t tap(uint32_t mono_ms, uint16_t next_id);
  void on_delivered(uint16_t knock_id, uint32_t mono_ms);
  void on_failed(uint16_t knock_id, uint32_t mono_ms);
  // phone: the answer came from a phone (REPLY bit 1), so the tile can say "(from phone)".
  void on_reply(uint16_t knock_id, bool come_on, uint32_t mono_ms, bool phone = false);
  void tick(uint32_t mono_ms);

  BellOut state() const { return state_; }
  uint16_t knock_id() const { return id_; }
  bool from_phone() const { return phone_; }  // the accepted reply was answered from a phone
  // Still waiting on an answer to this knock (a reply would be taken now).
  bool awaiting(uint16_t knock_id) const {
    return knock_id == id_ && (state_ == BellOut::Ringing || state_ == BellOut::Delivered ||
                               state_ == BellOut::NoAnswer || state_ == BellOut::CouldntReach);
  }
  // True while taps are being ignored (the tile can show that it's busy).
  bool ignoring(uint32_t mono_ms) const;
  static constexpr uint32_t kRingingMaxMs = 5000;  // safety net: retries resolve within 1.5 s

 private:
  void enter(BellOut s, uint32_t mono_ms) { state_ = s; entered_ = mono_ms; }
  BellOut state_ = BellOut::Idle;
  uint16_t id_ = 0;
  bool tapped_ = false;
  bool phone_ = false;
  uint32_t last_tap_ = 0;
  uint32_t entered_ = 0;
};

// What the receiving panel should do with an incoming knock.
struct KnockAction {
  bool banner = false;
  Chime chime = Chime::None;
  bool save = false;
  bool wake = false;  // HANDOFF: a doorbell wakes the screen at the current brightness level
  // From the substatus rules (ring_rule, Mike 2026-11-26); knock_action leaves them as they were.
  uint8_t volume_pct = 100;  // the chime at this share of the volume (after the quiet-hours cap)
  bool push = false;         // the rung floor's phones are pushed (quiet hours and the clock still decide)
  bool bell_off = false;     // the ringer's panel can't ring at all (its DOORBELL shows words instead)
};

// The table in Settings, by status alone. Since 2026-11-26 the doorbell itself goes by ring_rule;
// the mail chime and the dog's dings still ask this one where a sound is allowed.
KnockAction knock_action(const Settings &s, Status own);

// Mike, 2026-11-26: what a ring does on the RUNG panel, by its status and substatus.
//   Open    Courtesy ring    chime at the volume, banner, wake, phones
//           Free / Quiet     bell off: nothing at all (a ring from older firmware is taken silently)
//   Busy    Courtesy ring    soft chime at 75 % of the volume, banner, wake, phones
//           Prefer no        soft chime at 50 %, banner, wake, phones
//           Quiet passage    bell off
//   Sleeping Quiet passage   bell off
//           Prefer no        silent: no chime, banner, wake or phones; kept and shown when back
//   Private                  bell off ("I've chosen no contact in private")
//   Away    All out          kept and shown on return, as Away always was
//           <name> out       as Open's courtesy ring: someone is home
KnockAction ring_rule(Status own, Sub sub);

// The words the ringer's DOORBELL tile shows for the rung floor's state (Mike, 2026-11-26), or
// null for plain DOORBELL. With bell_off the tile can't ring; ONLY IF NEEDED still rings.
const char *bell_words(Status st, Sub sub);

enum class BannerKind : uint8_t { None, Knock, MissedWhileAway };

class BellReceiver {
 public:
  static constexpr uint32_t kBannerMs = 120000;  // Mike, 2026-11-15: 2 minutes, matching "No answer yet"

  // Returns the action; duplicates of the knock being shown return an empty action.
  KnockAction on_knock(uint16_t knock_id, Floor from, const Settings &s, Status own, uint32_t mono_ms, uint32_t utc);
  // The same with the action already worked out (ring_rule); own says why a saved one was kept
  // (Away or Sleeping), for the banner's words when it comes back.
  KnockAction on_knock(uint16_t knock_id, Floor from, const KnockAction &a, Status own, uint32_t mono_ms, uint32_t utc);
  // The owner answered. Returns the knock id to reply to, or 0.
  uint16_t answer(uint32_t mono_ms);
  void dismiss();
  // The owner's status changed; leaving Away brings back a saved knock.
  void status_changed(Status from, Status to, uint32_t mono_ms);
  // The same by the rule: was_saving and now_saving say whether a ring was kept for later before and
  // after the change (Away's All out, Sleeping's Prefer no passage; Mike, 2026-11-26). Leaving a
  // keeping state brings back the saved knock, as leaving Away always did.
  void saving_changed(bool was_saving, bool now_saving, uint32_t mono_ms);
  // Why the knock on a MissedWhileAway banner was kept: Away, or Sleeping (its words differ).
  Status saved_why() const { return why_; }
  void tick(uint32_t mono_ms);

  BannerKind banner() const { return banner_; }
  uint16_t id() const { return id_; }  // the knock id of the banner shown (meaningful while it's Knock)
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
  Status saved_why_ = Status::Away;
  Status why_ = Status::Away;
  bool missed_ = false;
  uint32_t missed_utc_ = 0;
};

}  // namespace tc
