#include "bell.h"

namespace tc {

uint16_t BellSender::tap(uint32_t mono_ms, uint16_t next_id) {
  if (ignoring(mono_ms)) return 0;
  tapped_ = true;
  last_tap_ = mono_ms;
  id_ = next_id ? next_id : 1;
  enter(BellOut::Ringing, mono_ms);
  return id_;
}

bool BellSender::ignoring(uint32_t mono_ms) const {
  return tapped_ && ms_diff(mono_ms, last_tap_) < (int32_t)kRepeatIgnoreMs;
}

void BellSender::on_delivered(uint16_t knock_id, uint32_t mono_ms) {
  if (knock_id == id_ && state_ == BellOut::Ringing) enter(BellOut::Delivered, mono_ms);
}

void BellSender::on_failed(uint16_t knock_id, uint32_t mono_ms) {
  if (knock_id == id_ && state_ == BellOut::Ringing) enter(BellOut::CouldntReach, mono_ms);
}

void BellSender::on_reply(uint16_t knock_id, bool come_on, uint32_t mono_ms) {
  if (knock_id != id_) return;
  // A reply can beat its ACK if the ACK was lost, and a late reply still beats "No answer yet".
  // It may also come after "Couldn't reach" has cleared (every ACK lost, the knock delivered):
  // the receiver's banner stays up 60 s, so accept it that long after the tap.
  int32_t since_tap = ms_diff(mono_ms, last_tap_);
  bool late = state_ == BellOut::Idle && since_tap >= 0 && since_tap < (int32_t)(BellReceiver::kBannerMs + 5000);
  if (state_ == BellOut::Ringing || state_ == BellOut::Delivered || state_ == BellOut::NoAnswer ||
      state_ == BellOut::CouldntReach || late)
    enter(come_on ? BellOut::ComeOn : BellOut::NotNow, mono_ms);
}

void BellSender::tick(uint32_t mono_ms) {
  // Retire the repeat-tap window once it has passed, so it can't come back when the millisecond
  // counter wraps (about every 49.7 days; differences go negative after 24.8).
  if (tapped_ && ms_diff(mono_ms, last_tap_) >= (int32_t)kRepeatIgnoreMs) tapped_ = false;
  int32_t in_state = ms_diff(mono_ms, entered_);
  switch (state_) {
    case BellOut::Ringing:
      if (in_state >= (int32_t)kRingingMaxMs) enter(BellOut::CouldntReach, mono_ms);
      break;
    case BellOut::Delivered:
      if (ms_diff(mono_ms, last_tap_) >= (int32_t)kNoAnswerMs) enter(BellOut::NoAnswer, mono_ms);
      break;
    case BellOut::ComeOn:
    case BellOut::NotNow:
      if (in_state >= (int32_t)kReplyShowMs) enter(BellOut::Idle, mono_ms);
      break;
    case BellOut::CouldntReach:
      if (in_state >= (int32_t)kFailShowMs) enter(BellOut::Idle, mono_ms);
      break;
    case BellOut::NoAnswer:
      if (in_state >= (int32_t)kNoAnswerShowMs) enter(BellOut::Idle, mono_ms);
      break;
    default:
      break;
  }
}

KnockAction knock_action(const Settings &s, Status own) {
  const KnockRule &r = s.knock[(int)own];
  KnockAction a;
  a.banner = r.banner;
  a.chime = r.chime;
  a.save = r.save;
  a.wake = r.banner;
  return a;
}

KnockAction BellReceiver::on_knock(uint16_t knock_id, Floor from, const Settings &s, Status own, uint32_t mono_ms,
                                   uint32_t utc) {
  if (banner_ == BannerKind::Knock && knock_id == id_ && from == from_) return KnockAction{};
  if (saved_ && knock_id == saved_id_ && from == saved_from_) return KnockAction{};
  KnockAction a = knock_action(s, own);
  if (banner_ == BannerKind::MissedWhileAway && a.save) {  // still away: keep the earlier one for later
    saved_ = true;
    saved_id_ = id_;
    saved_utc_ = utc_;
    saved_from_ = from_;
    banner_ = BannerKind::None;
  }
  if (a.save) {
    saved_ = true;
    saved_id_ = knock_id;
    saved_utc_ = utc;
    saved_from_ = from;
    return a;
  }
  if (a.banner) {
    if (banner_ == BannerKind::MissedWhileAway) {
      missed_ = true;
      missed_utc_ = utc_;
    }
    banner_ = BannerKind::Knock;
    id_ = knock_id;
    from_ = from;
    shown_ = mono_ms;
    utc_ = utc;
  }
  return a;
}

uint16_t BellReceiver::answer(uint32_t) {
  uint16_t id = banner_ == BannerKind::Knock ? id_ : 0;
  banner_ = BannerKind::None;
  if (id) clear_missed();
  return id;
}

void BellReceiver::dismiss() { banner_ = BannerKind::None; }

void BellReceiver::status_changed(Status from, Status to, uint32_t mono_ms) {
  if (to == Status::Away && banner_ == BannerKind::MissedWhileAway) {  // e.g. Away again from the status menu
    saved_ = true;
    saved_id_ = id_;
    saved_utc_ = utc_;
    saved_from_ = from_;
    banner_ = BannerKind::None;
    return;
  }
  if (from == Status::Away && to != Status::Away && saved_) {
    if (banner_ == BannerKind::Knock) {  // a live knock is on screen: the saved one becomes the missed note
      missed_ = true;
      missed_utc_ = saved_utc_;
      saved_ = false;
      return;
    }
    banner_ = BannerKind::MissedWhileAway;
    from_ = saved_from_;
    utc_ = saved_utc_;
    shown_ = mono_ms;
    saved_ = false;
  }
}

void BellReceiver::tick(uint32_t mono_ms) {
  if (banner_ == BannerKind::Knock && ms_diff(mono_ms, shown_) >= (int32_t)kBannerMs) {
    banner_ = BannerKind::None;
    missed_ = true;
    missed_utc_ = utc_;
  }
}

}  // namespace tc
