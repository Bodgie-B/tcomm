// Voice notes: the panel's upload logic and the SAY / PUSH retries. See voice_tx.h.
#include "voice_tx.h"
#include <string.h>

namespace tc {

namespace {

VoiceErr err_for_abort(uint8_t reason) {
  switch ((VoiceAbort)reason) {
    case VoiceAbort::NoPi: return VoiceErr::NoPi;
    case VoiceAbort::Busy: return VoiceErr::Busy;
    case VoiceAbort::Lost: return VoiceErr::Lost;
    case VoiceAbort::Silent: return VoiceErr::Nothing;
    case VoiceAbort::Bell: return VoiceErr::Bell;
    default: return VoiceErr::SendFailed;  // Timeout, User (never sent by the C6)
  }
}

VoiceErr err_for_status(uint8_t status) {
  switch ((VoiceStatus)status) {
    case VoiceStatus::Nothing: return VoiceErr::Nothing;
    case VoiceStatus::Failed: return VoiceErr::NotCaught;
    case VoiceStatus::Busy: return VoiceErr::Busy;
    case VoiceStatus::Lost: return VoiceErr::Lost;
    default: return VoiceErr::NoPi;  // Timeout
  }
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// VoiceTx

void VoiceTx::start(VoiceRec *rec, VoicePurpose purpose, uint8_t to, int fallback, uint32_t now, VoiceLink &l) {
  rec_ = rec;
  phase_ = VoiceTxPhase::Begin;
  err_ = VoiceErr::None;
  purpose_ = purpose;
  xfer_ = rec ? rec->xfer : 0;
  me_ = rec ? rec->sender : 0;
  floor_ = rec ? rec->floor : 0;
  to_ = to;
  fallback_ = fallback;
  fell_back_ = direct_failed_ = false;
  start_ms_ = now;
  base_ = 0;
  bits_ = 0;
  base_moved_ = now;
  end_first_ = end_last_ = 0;
  text_deadline_ = 0;
  capture_end_ms_ = 0;
  queued_ = extended_ = final_ = false;
  memset(resent_, 0, sizeof(resent_));
  memset(last_ms_, 0, sizeof(last_ms_));
  resends_total_ = 0;
  round_ = false;
  round_next_ = round_sent_ = 0;
  round_end_ = probe_last_ = 0;
  text_[0] = 0;
  flags_ = 0;
  stats_ = Stats();
  if (!rec) {
    fail(VoiceErr::NoMemory, false, VoiceAbort::User, now);
    return;
  }
  send_begin(now, l);
}

void VoiceTx::reset() {
  rec_ = nullptr;
  phase_ = VoiceTxPhase::Idle;
  err_ = VoiceErr::None;
  queued_ = false;
  text_[0] = 0;
  flags_ = 0;
}

bool VoiceTx::send(VoiceLink &l, VoiceMsg &m) {
  m.sender = me_;
  m.floor = floor_;
  m.seq = ++seq_;
  uint8_t buf[kMaxMsg];
  size_t n = voice_encode(m, buf, sizeof(buf));
  if (!n) return false;
  bool ok = l.voice_send(to_, buf, n);
  if (!ok) stats_.refused++;
  return ok;
}

void VoiceTx::send_begin(uint32_t now, VoiceLink &l) {
  VoiceMsg m;
  m.type = VType::Begin;
  m.u.begin.xfer = xfer_;
  m.u.begin.purpose = (uint8_t)purpose_;
  m.u.begin.codec = rec_->codec;
  m.u.begin.chunk_samples = rec_->chunk_samples;
  m.u.begin.max_chunks = rec_->max_chunks;
  send(l, m);
  begin_last_ = now;
  stats_.begin_sends++;
}

void VoiceTx::send_end(uint32_t now, VoiceLink &l) {
  VoiceMsg m;
  m.type = VType::End;
  m.u.end.xfer = xfer_;
  m.u.end.n_chunks = rec_->first_sent.load(std::memory_order_acquire);
  uint32_t ns = rec_->n_samples.load(std::memory_order_acquire);
  uint32_t cap = (uint32_t)m.u.end.n_chunks * rec_->chunk_samples;
  m.u.end.n_samples = (uint16_t)(ns > cap ? cap : ns);
  m.u.end.speech = rec_->speech.load(std::memory_order_acquire) ? 1 : 0;
  send(l, m);
  end_last_ = now;
  stats_.end_sends++;
}

bool VoiceTx::acked(int idx) const {
  if (idx < base_) return true;
  int k = idx - base_;
  return k < 32 && ((bits_ >> k) & 1u);
}

void VoiceTx::resend(uint32_t now, VoiceLink &l) {
  if (!rec_) return;
  const int fs = rec_->first_sent.load(std::memory_order_acquire);  // the chunks [0, fs) are out and complete
  const uint32_t cap = 2u * (uint32_t)fs;  // with the first sends, 3 x the chunks at most
  // A round is one pass over the owed chunks, oldest first, at most kVoiceResendsPerTick a tick. Between
  // rounds that sent anything the air stays quiet for kVoiceResendQuietMs, so the C6's 80 ms ACK goes out.
  if (!round_) {
    if (round_end_ && ms_diff(now, round_end_) < (int32_t)kVoiceResendQuietMs) return;
    round_next_ = base_;
    round_sent_ = 0;
  }
  int done = 0;
  bool capped = false;
  int i = round_next_ < base_ ? base_ : round_next_;
  for (; i < fs && done < kVoiceResendsPerTick; i++) {
    if (acked(i) || resent_[i] >= kVoiceSendsPerChunk - 1) continue;
    uint32_t last = last_ms_[i] ? last_ms_[i] : rec_->first_ms[i];
    if (ms_diff(now, last) < (int32_t)kVoiceResendAfterMs) continue;
    if (resends_total_ >= cap) {
      capped = true;
      break;
    }
    done++;
    resend_one(i, now, l);
  }
  round_next_ = i;
  round_sent_ += done;
  if (i >= fs || capped) {  // the pass reached the newest chunk: the round is over
    round_ = false;
    if (round_sent_) round_end_ = now ? now : 1;
  } else {
    round_ = true;  // more chunks to look at next tick
  }
  // The probe (field fix 2026-11-21): base has not moved for kVoiceProbeAfterMs while chunks are owed and
  // no resend is due (the missing chunk may have used its sends, or its last one got through and the
  // C6's answer was lost). One chunk the latest bitmap shows received goes again: the C6 (0.5) answers a
  // duplicate with a VOICE_ACK of where it really is.
  if (!done && !round_ && fs > base_ && !capped && resends_total_ < cap &&
      ms_diff(now, base_moved_) >= (int32_t)kVoiceProbeAfterMs &&
      (!probe_last_ || ms_diff(now, probe_last_) >= (int32_t)kVoiceProbeEveryMs) &&
      (!round_end_ || ms_diff(now, round_end_) >= (int32_t)kVoiceResendQuietMs)) {
    for (int k = base_ + 1; k < fs && k < base_ + kVoiceWindow; k++) {
      if (!acked(k) || resent_[k] >= kVoiceSendsPerChunk - 1) continue;
      probe_last_ = now ? now : 1;
      round_end_ = probe_last_;  // the quiet after it, as after a round
      resend_one(k, now, l);
      stats_.probes++;
      break;
    }
  }
}

void VoiceTx::resend_one(int i, uint32_t now, VoiceLink &l) {
  const VoiceChunk &c = rec_->chunks[i];
  VoiceMsg m;
  m.type = VType::Data;
  m.u.data.xfer = xfer_;
  m.u.data.idx = (uint8_t)i;
  m.u.data.pred = c.pred;
  m.u.data.step = c.step;
  m.u.data.len = c.len;
  memcpy(m.u.data.data, c.data, c.len);
  last_ms_[i] = now ? now : 1;  // 0 means "use first_ms"
  if (!send(l, m)) return;      // refused by the radio: tried again later, not counted
  resent_[i]++;
  resends_total_++;
  stats_.resends++;
}

void VoiceTx::fail(VoiceErr e, bool abort, VoiceAbort why, uint32_t now) {
  if (rec_) stats_.chunks = rec_->first_sent.load(std::memory_order_acquire);
  phase_ = VoiceTxPhase::Failed;
  err_ = e;
  if (abort && xfer_) {
    abort_xfer_ = xfer_;
    abort_to_ = to_;
    abort_reason_ = (uint8_t)why;
    abort_floor_ = floor_;
    abort_n_ = 0;
    abort_t0_ = now;
  }
}

void VoiceTx::tick_aborts(uint32_t now, VoiceLink &l) {
  // One send a call at most: a late tick never sends a burst; the next goes at a later tick.
  if (abort_n_ < kVoiceAbortSends && ms_diff(now, abort_t0_) >= (int32_t)kVoiceAbortAtMs[abort_n_]) {
    VoiceMsg m;
    m.type = VType::Abort;
    m.sender = me_;
    m.floor = abort_floor_;
    m.seq = ++seq_;
    m.u.abort.xfer = abort_xfer_;
    m.u.abort.reason = abort_reason_;
    uint8_t buf[16];
    size_t n = voice_encode(m, buf, sizeof(buf));
    if (n) l.voice_send(abort_to_, buf, n);
    abort_n_++;
  }
}

void VoiceTx::tick(uint32_t now, VoiceLink &l) {
  tick_aborts(now, l);
  switch (phase_) {
    case VoiceTxPhase::Begin: {
      int32_t age = ms_diff(now, start_ms_);
      if (age >= (int32_t)kVoiceBeginGiveUpMs) {
        if (!fell_back_ && to_ == kSenderGateway) direct_failed_ = true;  // the direct route never answered
        fail(VoiceErr::GatewayOld, true, VoiceAbort::Timeout, now);
        tick_aborts(now, l);
        return;
      }
      if (fallback_ >= 0 && !fell_back_ && age >= (int32_t)kVoiceBeginFallbackMs) {
        to_ = (uint8_t)fallback_;
        fell_back_ = direct_failed_ = true;
        send_begin(now, l);
        return;
      }
      if (ms_diff(now, begin_last_) >= (int32_t)kVoiceBeginEveryMs) send_begin(now, l);
      return;
    }
    case VoiceTxPhase::Stream:
    case VoiceTxPhase::Ending: {
      const int fs = rec_ ? rec_->first_sent.load(std::memory_order_acquire) : 0;
      if (fs <= base_) {
        base_moved_ = now;  // nothing owed
      } else if (ms_diff(now, base_moved_) >= (int32_t)kVoiceStallMs) {
        fail(VoiceErr::SendFailed, true, VoiceAbort::Timeout, now);
        tick_aborts(now, l);
        return;
      }
      resend(now, l);
      if (phase_ == VoiceTxPhase::Stream && rec_ && rec_->done.load(std::memory_order_acquire)) {
        phase_ = VoiceTxPhase::Ending;
        end_first_ = end_last_ = now;  // the mic task sent the first END
        if (!capture_end_ms_) capture_end_ms_ = now;
      }
      if (phase_ == VoiceTxPhase::Ending) {
        if (ms_diff(now, end_first_) >= (int32_t)kVoiceEndGiveUpMs) {
          fail(VoiceErr::SendFailed, true, VoiceAbort::Timeout, now);
          tick_aborts(now, l);
        } else if (ms_diff(now, end_last_) >= (int32_t)kVoiceEndEveryMs) {
          send_end(now, l);
        }
      }
      return;
    }
    case VoiceTxPhase::WaitText:
      if (ms_reached(now, text_deadline_)) {
        fail(VoiceErr::NoPi, true, VoiceAbort::Timeout, now);
        tick_aborts(now, l);
      }
      return;
    default:
      return;
  }
}

void VoiceTx::on_ack(const VoiceMsg &m, uint32_t now) {
  if (!active() || m.u.ack.xfer != xfer_) return;
  const auto &a = m.u.ack;
  if (phase_ == VoiceTxPhase::Begin) {
    if (a.state == (uint8_t)VoiceAckState::Unknown) return;  // DATA ahead of the BEGIN: keep sending BEGIN
    phase_ = VoiceTxPhase::Stream;
    base_moved_ = now;
  }
  if (a.state == (uint8_t)VoiceAckState::Unknown) {
    if (phase_ != VoiceTxPhase::WaitText) fail(VoiceErr::SendFailed, false, VoiceAbort::Timeout, now);  // the C6 lost it
    return;
  }
  if (a.base > base_) {
    base_ = a.base > kVoiceMaxChunks ? (uint8_t)kVoiceMaxChunks : a.base;
    bits_ = a.bits;
    base_moved_ = now;
    if (rec_) rec_->base.store(base_, std::memory_order_release);
  } else if (a.base == base_) {
    bits_ |= a.bits;  // ACKs can arrive out of order; for one base the set only grows
  }
  if (a.state == (uint8_t)VoiceAckState::Complete &&
      (phase_ == VoiceTxPhase::Stream || phase_ == VoiceTxPhase::Ending)) {
    if (rec_) stats_.chunks = rec_->first_sent.load(std::memory_order_acquire);
    phase_ = VoiceTxPhase::WaitText;
    if (!capture_end_ms_) capture_end_ms_ = now;
    text_deadline_ = now + (queued_ ? kVoiceQueuedWaitMs : kVoiceTextWaitMs);
    if (queued_) extended_ = true;
  }
}

void VoiceTx::on_abort(const VoiceMsg &m, uint32_t now) {
  if (!active() || m.u.abort.xfer != xfer_) return;
  fail(err_for_abort(m.u.abort.reason), false, VoiceAbort::User, now);
}

VoiceTextIs VoiceTx::on_text(const VoiceMsg &m, uint32_t now) {
  const auto &t = m.u.text;
  if (!xfer_ || t.xfer != xfer_) return VoiceTextIs::NotMine;
  if (final_) return VoiceTextIs::Duplicate;
  if (!active()) return VoiceTextIs::NotMine;  // cancelled or given up: the caller answers "gone"
  if (t.status == (uint8_t)VoiceStatus::Queued) {
    // Interim: the other floor's message is ahead at the Pi. It can come while chunks still flow (the
    // Pi says so at once), so the longer wait starts at state 2, or now if that has already come.
    if (!queued_) {
      queued_ = true;
      if (phase_ == VoiceTxPhase::WaitText && !extended_) {
        extended_ = true;
        text_deadline_ = now + kVoiceQueuedWaitMs;
      }
    }
    return VoiceTextIs::Interim;
  }
  final_ = true;
  if (rec_) stats_.chunks = rec_->first_sent.load(std::memory_order_acquire);
  if (capture_end_ms_) stats_.ms_to_text = (uint32_t)ms_diff(now, capture_end_ms_);
  if (t.status == (uint8_t)VoiceStatus::Ok) {
    memcpy(text_, t.text, t.len);
    text_[t.len] = 0;
    flags_ = t.flags;
    phase_ = VoiceTxPhase::Done;
    err_ = VoiceErr::None;
  } else {
    fail(err_for_status(t.status), false, VoiceAbort::User, now);
  }
  return VoiceTextIs::Final;
}

void VoiceTx::cancel(VoiceAbort why, uint32_t now, VoiceLink &l) {
  if (!active()) return;
  fail(VoiceErr::None, true, why, now);
  tick_aborts(now, l);
}

// ---------------------------------------------------------------------------------------------
// VoiceOut

bool VoiceOut::add(uint8_t to, uint8_t kind, uint32_t key, const uint8_t *frame, size_t n, uint32_t now) {
  if (!frame || n == 0 || n > sizeof(s_[0].frame)) return false;
  for (Slot &s : s_) {
    if (s.used) continue;
    s.used = true;
    s.to = to;
    s.kind = kind;
    s.key = key;
    s.t0 = now;
    s.tries = 0;
    s.len = (uint8_t)n;
    memcpy(s.frame, frame, n);
    return true;
  }
  return false;
}

void VoiceOut::tick(uint32_t now, VoiceLink &l) {
  for (Slot &s : s_) {
    if (!s.used || s.tries >= kVoiceOutTries) continue;
    if (ms_diff(now, s.t0) < (int32_t)kVoiceOutAtMs[s.tries]) continue;
    l.voice_send(s.to, s.frame, s.len);
    s.tries++;
    // Late ticks never send a burst: skip the tries whose time has already passed.
    while (s.tries < kVoiceOutTries && ms_diff(now, s.t0) >= (int32_t)kVoiceOutAtMs[s.tries]) s.tries++;
  }
}

bool VoiceOut::on_got(uint8_t kind, uint32_t key) {
  for (Slot &s : s_) {
    if (!s.used || s.kind != kind || s.key != key) continue;
    s.used = false;
    return true;
  }
  return false;
}

bool VoiceOut::expired(uint32_t now, uint8_t &kind, uint32_t &key) {
  for (Slot &s : s_) {
    if (!s.used || ms_diff(now, s.t0) < (int32_t)kVoiceOutGiveUpMs) continue;
    s.used = false;
    kind = s.kind;
    key = s.key;
    return true;
  }
  return false;
}

bool VoiceOut::pending(uint8_t kind, uint32_t key) const {
  for (const Slot &s : s_)
    if (s.used && s.kind == kind && s.key == key) return true;
  return false;
}

int VoiceOut::used() const {
  int n = 0;
  for (const Slot &s : s_) n += s.used ? 1 : 0;
  return n;
}

void VoiceOut::clear() {
  for (Slot &s : s_) s.used = false;
}

size_t voice_make_got(uint8_t me, uint8_t floor, uint16_t seq, uint8_t kind, uint32_t key, VoiceGotResult r,
                      uint8_t *out, size_t cap) {
  VoiceMsg m;
  m.type = VType::Got;
  m.sender = me;
  m.floor = floor;
  m.seq = seq;
  m.u.got.kind = kind;
  m.u.got.key = key;
  m.u.got.result = (uint8_t)r;
  return voice_encode(m, out, cap);
}

}  // namespace tc
