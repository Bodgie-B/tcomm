#include "protocol.h"
#include <string.h>

namespace tc {

namespace {

struct Writer {
  uint8_t *p;
  size_t cap, n = 0;
  bool ok = true;
  Writer(uint8_t *out, size_t c) : p(out), cap(c) {}
  void u8(uint8_t v) {
    if (n + 1 > cap) { ok = false; return; }
    p[n++] = v;
  }
  void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
  void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
  void bytes(const void *src, size_t len) {
    if (n + len > cap) { ok = false; return; }
    memcpy(p + n, src, len);
    n += len;
  }
};

struct Reader {
  const uint8_t *p;
  size_t len, n = 0;
  bool ok = true;
  Reader(const uint8_t *in, size_t l) : p(in), len(l) {}
  uint8_t u8() {
    if (n + 1 > len) { ok = false; return 0; }
    return p[n++];
  }
  uint16_t u16() { uint16_t lo = u8(); return (uint16_t)(lo | (u8() << 8)); }
  uint32_t u32() { uint32_t lo = u16(); return lo | ((uint32_t)u16() << 16); }
  bool done() const { return ok && n == len; }
};

bool latest_wins(MsgType t) {
  return t == MsgType::State || t == MsgType::DogMsg || t == MsgType::Reading || t == MsgType::Outside ||
         t == MsgType::EventSum || t == MsgType::Mail;
}

}  // namespace

size_t encode(const Msg &m, uint8_t *out, size_t cap) {
  Writer w(out, cap);
  w.u8(kProtoVersion);
  w.u8((uint8_t)m.type);
  w.u8(m.sender);
  w.u16(m.seq);
  switch (m.type) {
    case MsgType::State:
      w.u8(m.u.state.status); w.u8(m.u.state.flags); w.u32(m.u.state.since); w.u32(m.u.state.until);
      w.u8(m.u.state.fallback); w.u32(m.u.state.clock); w.u8(m.u.state.clock_quality);
      break;
    case MsgType::Knock: w.u16(m.u.knock.id); break;
    case MsgType::Reply: w.u16(m.u.reply.id); w.u8(m.u.reply.come_on ? 1 : 0); break;
    case MsgType::Ack: w.u16(m.u.ack.seq); w.u8(m.u.ack.type); break;
    case MsgType::DogMsg:
      for (int i = 0; i < 2; i++) { w.u8(m.u.dog.out[i]); w.u32(m.u.dog.since[i]); w.u32(m.u.dog.ver[i]); }
      break;
    case MsgType::Reading:
      w.u8(m.u.reading.flags); w.u16((uint16_t)m.u.reading.temp_tenths_f); w.u16(m.u.reading.humidity_tenths);
      break;
    case MsgType::Lucy: w.u8(m.u.lucy.out ? 1 : 0); w.u32(m.u.lucy.since); break;
    case MsgType::Outside: {
      size_t len = strnlen(m.u.outside.phrase, kMaxPhrase);
      w.u8(m.u.outside.flags); w.u16((uint16_t)m.u.outside.temp_tenths_f); w.u8(m.u.outside.humidity);
      w.u8(m.u.outside.forecast); w.u32(m.u.outside.time); w.u8((uint8_t)len); w.bytes(m.u.outside.phrase, len);
      break;
    }
    case MsgType::EventMsg:
      if (m.u.event.len == 0 || m.u.event.len > kEventWireMax) return 0;
      w.bytes(m.u.event.data, m.u.event.len);
      break;
    case MsgType::EventSum: w.u16(m.u.sum.count); w.u32(m.u.sum.digest); w.u8(m.u.sum.flags); break;
    case MsgType::Mail: w.u8(m.u.mail.waiting ? 1 : 0); w.u32(m.u.mail.since); w.u32(m.u.mail.ver); break;
    default: return 0;
  }
  return w.ok ? w.n : 0;
}

bool decode(const uint8_t *in, size_t len, Msg &m) {
  if (!in || len < 5 || len > kMaxMsg) return false;
  Reader r(in, len);
  if (r.u8() != kProtoVersion) return false;
  uint8_t type = r.u8();
  m = Msg();
  m.sender = r.u8();
  m.seq = r.u16();
  if (m.sender > kSenderGateway) return false;
  switch ((MsgType)type) {
    case MsgType::State: {
      auto &s = m.u.state;
      s.status = r.u8(); s.flags = r.u8(); s.since = r.u32(); s.until = r.u32();
      s.fallback = r.u8(); s.clock = r.u32(); s.clock_quality = r.u8();
      if (!status_valid(s.status) || !status_valid(s.fallback) || s.clock_quality > kClockNetwork) return false;
      break;
    }
    case MsgType::Knock: m.u.knock.id = r.u16(); if (m.u.knock.id == 0) return false; break;
    case MsgType::Reply:
      m.u.reply.id = r.u16(); m.u.reply.come_on = r.u8();
      if (m.u.reply.come_on > 1 || m.u.reply.id == 0) return false;
      break;
    case MsgType::Ack: m.u.ack.seq = r.u16(); m.u.ack.type = r.u8(); break;
    case MsgType::DogMsg:
      for (int i = 0; i < 2; i++) {
        m.u.dog.out[i] = r.u8(); m.u.dog.since[i] = r.u32(); m.u.dog.ver[i] = r.u32();
        if (m.u.dog.out[i] > 1) return false;
      }
      break;
    case MsgType::Reading:
      m.u.reading.flags = r.u8(); m.u.reading.temp_tenths_f = (int16_t)r.u16(); m.u.reading.humidity_tenths = r.u16();
      if (m.u.reading.humidity_tenths > 1000) return false;
      break;
    case MsgType::Lucy: m.u.lucy.out = r.u8(); m.u.lucy.since = r.u32(); if (m.u.lucy.out > 1) return false; break;
    case MsgType::Outside: {
      auto &o = m.u.outside;
      o.flags = r.u8(); o.temp_tenths_f = (int16_t)r.u16(); o.humidity = r.u8(); o.forecast = r.u8();
      o.time = r.u32(); o.phrase_len = r.u8();
      if (o.phrase_len > kMaxPhrase || o.humidity > 100) return false;  // an unknown forecast still has a reading
      for (uint8_t i = 0; i < o.phrase_len; i++) {
        char c = (char)r.u8();
        if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = '?';  // fonts carry ASCII only
        o.phrase[i] = c;
      }
      o.phrase[o.phrase_len] = 0;
      break;
    }
    case MsgType::EventMsg: {
      size_t n = len - r.n;
      Event e;
      if (n == 0 || n > kEventWireMax || event_read(in + r.n, n, e) != n) return false;
      m.u.event.len = (uint8_t)n;
      memcpy(m.u.event.data, in + r.n, n);
      r.n = len;
      break;
    }
    case MsgType::EventSum:
      m.u.sum.count = r.u16(); m.u.sum.digest = r.u32(); m.u.sum.flags = r.u8();
      if (m.u.sum.count > kMaxEvents) return false;
      break;
    case MsgType::Mail:
      m.u.mail.waiting = r.u8(); m.u.mail.since = r.u32(); m.u.mail.ver = r.u32();
      if (m.u.mail.waiting > 1) return false;
      break;
    default: return false;
  }
  m.type = (MsgType)type;
  return r.done();
}

bool Dedup::seen(uint8_t sender, uint16_t seq, uint32_t mono_ms) {
  for (auto &e : e_) {
    int32_t age = ms_diff(mono_ms, e.at);
    if (e.used && (age >= (int32_t)kKeepMs || age < 0)) e.used = false;  // < 0: stored ~25 days ago, wrapped
    if (e.used && e.sender == sender && e.seq == seq) return true;
  }
  // Prefer a free slot; otherwise overwrite round-robin (the oldest, given steady traffic).
  for (auto &e : e_) {
    if (!e.used) { e = Entry{sender, seq, mono_ms, true}; return false; }
  }
  e_[next_] = Entry{sender, seq, mono_ms, true};
  next_ = (uint8_t)((next_ + 1) % (sizeof(e_) / sizeof(e_[0])));
  return false;
}

uint16_t RetryQueue::push(Msg m, uint8_t sender, uint32_t mono_ms) {
  Slot *slot = nullptr;
  if (latest_wins(m.type)) {
    for (auto &s : s_) if (s.used && s.f.type == m.type) { slot = &s; break; }
  }
  if (!slot) for (auto &s : s_) if (!s.used) { slot = &s; break; }
  if (!slot) return 0;
  m.sender = sender;
  m.seq = seq_;
  seq_ = (uint16_t)(seq_ + 1);
  if (seq_ == 0) seq_ = 1;
  size_t n = encode(m, slot->f.data, sizeof(slot->f.data));
  if (!n) { slot->used = false; return 0; }
  slot->used = true;
  slot->f.len = n;
  slot->f.seq = m.seq;
  slot->f.type = m.type;
  slot->tries = 0;
  slot->first = mono_ms;
  slot->knock_id = m.type == MsgType::Knock ? m.u.knock.id : m.type == MsgType::Reply ? m.u.reply.id : 0;
  return m.seq;
}

uint32_t RetryQueue::next_at(const Slot &s) {
  if (s.tries == 0) return s.first;
  return s.last + (kOffsetsMs[s.tries] - kOffsetsMs[s.tries - 1]);
}

const RetryQueue::Frame *RetryQueue::due(uint32_t mono_ms) {
  const Slot *best = nullptr;
  for (const auto &s : s_) {
    if (!s.used || s.tries >= kTries) continue;
    uint32_t at = next_at(s);
    if (!ms_reached(mono_ms, at)) continue;
    if (!best || ms_diff(at, next_at(*best)) < 0) best = &s;
  }
  return best ? &best->f : nullptr;
}

void RetryQueue::sent(uint16_t seq, uint32_t mono_ms) {
  for (auto &s : s_) {
    if (s.used && s.f.seq == seq) {
      s.tries++;
      s.last = mono_ms;
      return;
    }
  }
}

bool RetryQueue::ack(uint16_t seq, Result &r) {
  for (auto &s : s_) {
    if (s.used && s.f.seq == seq) {
      r = Result{false, seq, s.f.type, s.knock_id};
      s.used = false;
      return true;
    }
  }
  return false;
}

bool RetryQueue::expired(uint32_t mono_ms, Result &r) {
  for (auto &s : s_) {
    if (s.used && s.tries >= kTries &&
        ms_diff(mono_ms, s.last) >= (int32_t)(kGiveUpMs - kOffsetsMs[kTries - 1])) {
      r = Result{true, s.f.seq, s.f.type, s.knock_id};
      s.used = false;
      return true;
    }
  }
  return false;
}

int RetryQueue::pending() const {
  int n = 0;
  for (const auto &s : s_) n += s.used;
  return n;
}

int RetryQueue::pending(MsgType t) const {
  int n = 0;
  for (const auto &s : s_) n += s.used && s.f.type == t;
  return n;
}

const char *msg_name(MsgType t) {
  switch (t) {
    case MsgType::State: return "STATE";
    case MsgType::Knock: return "KNOCK";
    case MsgType::Reply: return "REPLY";
    case MsgType::Ack: return "ACK";
    case MsgType::DogMsg: return "DOG";
    case MsgType::Reading: return "READING";
    case MsgType::Lucy: return "LUCY";
    case MsgType::Outside: return "OUTSIDE";
    case MsgType::EventMsg: return "EVENT";
    case MsgType::EventSum: return "EVENT_SUM";
    case MsgType::Mail: return "MAIL";
  }
  return "?";
}

}  // namespace tc
