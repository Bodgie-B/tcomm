#include "bell.h"

namespace tc {

uint16_t BellSender::tap(uint32_t mono_ms, uint16_t next_id) {
  if (ignoring(mono_ms)) return 0;
  tapped_ = true;
  last_tap_ = mono_ms;
  id_ = next_id ? next_id : 1;
  phone_ = false;
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

void BellSender::on_reply(uint16_t knock_id, bool come_on, uint32_t mono_ms, bool phone) {
  if (knock_id != id_) return;
  // A reply can beat its ACK if the ACK was lost, and a late reply still beats "No answer yet".
  // It may also come after "Couldn't reach" has cleared (every ACK lost, the knock delivered):
  // the receiver's banner stays up 2 minutes, so accept it that long after the tap.
  int32_t since_tap = ms_diff(mono_ms, last_tap_);
  bool late = state_ == BellOut::Idle && since_tap >= 0 && since_tap < (int32_t)(BellReceiver::kBannerMs + 5000);
  if (state_ == BellOut::Ringing || state_ == BellOut::Delivered || state_ == BellOut::NoAnswer ||
      state_ == BellOut::CouldntReach || late) {
    enter(come_on ? BellOut::ComeOn : BellOut::NotNow, mono_ms);
    phone_ = phone;
  }
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

KnockAction ring_rule(Status own, Sub sub) {
  KnockAction a;
  auto rings = [&a](Chime c, uint8_t pct) {
    a.chime = c;
    a.volume_pct = pct;
    a.banner = a.wake = a.push = true;
  };
  switch (own) {
    case Status::Open:
      if (sub == Sub::Free || sub == Sub::Quiet) a.bell_off = true;
      else rings(Chime::Normal, 100);
      break;
    case Status::Busy:
      if (sub == Sub::Quiet) a.bell_off = true;
      else if (sub == Sub::PreferNo) rings(Chime::Soft, 50);
      else rings(Chime::Soft, 75);  // Courtesy ring: 25 % below the volume (was the volume less 15)
      break;
    case Status::Sleeping:
      // Mike, 2026-11-26: "I'm sleeping and can't see it": no sound, banner, wake or phones. Prefer
      // no passage lets the other floor ring anyway, and the ring is kept for when they're back.
      if (sub == Sub::PreferNo) a.save = true;
      else a.bell_off = true;
      break;
    case Status::Away:
      if (sub == Sub::OneOut1 || sub == Sub::OneOut2) rings(Chime::Normal, 100);  // someone is home
      else a.save = true;
      break;
    default:  // Private: "I've chosen no contact in private"
      a.bell_off = true;
      break;
  }
  return a;
}

const char *bell_words(Status st, Sub sub) {
  if (st == Status::Private) return "DO NOT DISTURB";
  if (st == Status::Open && sub == Sub::Free) return "COME ON IN";
  if (sub == Sub::Quiet && st != Status::Away) return "COME IN QUIETLY";
  if (sub == Sub::PreferNo && (st == Status::Busy || st == Status::Sleeping)) return "ONLY IF NEEDED";
  return nullptr;
}

KnockAction BellReceiver::on_knock(uint16_t knock_id, Floor from, const Settings &s, Status own, uint32_t mono_ms,
                                   uint32_t utc) {
  return on_knock(knock_id, from, knock_action(s, own), own, mono_ms, utc);
}

KnockAction BellReceiver::on_knock(uint16_t knock_id, Floor from, const KnockAction &rule, Status own, uint32_t mono_ms,
                                   uint32_t utc) {
  if (banner_ == BannerKind::Knock && knock_id == id_ && from == from_) return KnockAction{};
  if (saved_ && knock_id == saved_id_ && from == saved_from_) return KnockAction{};
  KnockAction a = rule;
  if (banner_ == BannerKind::MissedWhileAway && a.save) {  // still away: keep the earlier one for later
    saved_ = true;
    saved_id_ = id_;
    saved_utc_ = utc_;
    saved_from_ = from_;
    saved_why_ = why_;
    banner_ = BannerKind::None;
  }
  if (a.save) {
    saved_ = true;
    saved_id_ = knock_id;
    saved_utc_ = utc;
    saved_from_ = from;
    saved_why_ = own == Status::Sleeping ? Status::Sleeping : Status::Away;
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
  saving_changed(from == Status::Away, to == Status::Away, mono_ms);
}

void BellReceiver::saving_changed(bool was_saving, bool now_saving, uint32_t mono_ms) {
  if (now_saving && banner_ == BannerKind::MissedWhileAway) {  // e.g. Away again from the status menu
    saved_ = true;
    saved_id_ = id_;
    saved_utc_ = utc_;
    saved_from_ = from_;
    saved_why_ = why_;
    banner_ = BannerKind::None;
    return;
  }
  if (was_saving && !now_saving && saved_) {
    if (banner_ == BannerKind::Knock) {  // a live knock is on screen: the saved one becomes the missed note
      missed_ = true;
      missed_utc_ = saved_utc_;
      saved_ = false;
      return;
    }
    banner_ = BannerKind::MissedWhileAway;
    from_ = saved_from_;
    utc_ = saved_utc_;
    why_ = saved_why_;
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
