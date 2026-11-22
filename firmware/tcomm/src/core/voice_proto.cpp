// Voice notes: frame encode and decode, the relay rule, the capital rule and the ADPCM decoder.
// See voice_proto.h and docs/voice-notes/DESIGN.md.
#include "voice_proto.h"
#include <new>
#include <string.h>

namespace tc {

namespace {

struct VWriter {
  uint8_t *p;
  size_t cap, n = 0;
  bool ok = true;
  VWriter(uint8_t *out, size_t c) : p(out), cap(c) {}
  void u8(uint8_t v) {
    if (n + 1 > cap) { ok = false; return; }
    p[n++] = v;
  }
  void u16(uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); }
  void u32(uint32_t v) { u16((uint16_t)v); u16((uint16_t)(v >> 16)); }
  void bytes(const void *d, size_t len) {
    if (n + len > cap) { ok = false; return; }
    memcpy(p + n, d, len);
    n += len;
  }
};

struct VReader {
  const uint8_t *p;
  size_t len, n = 0;
  VReader(const uint8_t *in, size_t l) : p(in), len(l) {}
  uint8_t u8() { return n < len ? p[n++] : 0; }
  uint16_t u16() { uint16_t a = u8(); return (uint16_t)(a | (uint16_t)(u8() << 8)); }
  uint32_t u32() { uint32_t a = u16(); return a | ((uint32_t)u16() << 16); }
};

bool text_ok(const char *t, size_t len) {
  for (size_t i = 0; i < len; i++)
    if ((uint8_t)t[i] < 0x20 || (uint8_t)t[i] > 0x7E) return false;
  return true;
}

bool got_kind_ok(uint8_t k) {
  return k == (uint8_t)VType::Text || k == (uint8_t)VType::Say || k == (uint8_t)VType::Push ||
         k == (uint8_t)VType::Dismiss || k == (uint8_t)VType::Ping;
}

// Body checks shared by encode and decode (header fields included).
bool fields_ok(const VoiceMsg &m) {
  if (!is_voice_type((uint8_t)m.type) || m.sender > kSenderGateway || m.floor > 1) return false;
  switch (m.type) {
    case VType::Begin: {
      const auto &b = m.u.begin;
      if (b.xfer == 0 || b.max_chunks == 0 || !voice_chunks_ok(b.max_chunks)) return false;
      if (b.purpose != (uint8_t)VoicePurpose::Dictation && b.purpose != (uint8_t)VoicePurpose::Message) return false;
      if (b.codec == (uint8_t)VoiceCodec::Adpcm) return b.chunk_samples == kVoiceChunkSamplesAdpcm;
      if (b.codec == (uint8_t)VoiceCodec::Mulaw) return b.chunk_samples == kVoiceChunkSamplesMulaw;
      return false;
    }
    case VType::Data: {
      const auto &d = m.u.data;
      return d.xfer != 0 && (int)d.idx < kVoiceMaxChunks && d.len >= 1 && d.len <= kVoiceChunkBytes && d.step <= kVoiceStepMax;
    }
    case VType::Ack: {
      const auto &a = m.u.ack;
      return a.xfer != 0 && a.state >= (uint8_t)VoiceAckState::Open && a.state <= (uint8_t)VoiceAckState::Unknown &&
             voice_chunks_ok(a.base);
    }
    case VType::End: {
      const auto &e = m.u.end;
      return e.xfer != 0 && voice_chunks_ok(e.n_chunks) && e.speech <= 1 &&
             e.n_samples <= (uint32_t)e.n_chunks * kVoiceChunkSamplesAdpcm;
    }
    case VType::Abort:
      return m.u.abort.xfer != 0 && m.u.abort.reason >= (uint8_t)VoiceAbort::User &&
             m.u.abort.reason <= (uint8_t)VoiceAbort::NoPi;
    case VType::Text: {
      const auto &t = m.u.text;
      if (t.xfer == 0 || t.status > kVoiceStatusMax || (t.flags & ~kVoiceTextFlagsKnown) || t.len > kVoiceTextMax) return false;
      if ((t.status == (uint8_t)VoiceStatus::Ok) != (t.len > 0)) return false;
      return text_ok(t.text, t.len);
    }
    case VType::Say:
      return m.u.say.id != 0 && m.u.say.len >= 1 && m.u.say.len <= kVoiceTextMax && text_ok(m.u.say.text, m.u.say.len);
    case VType::Got:
      return got_kind_ok(m.u.got.kind) && m.u.got.result <= (uint8_t)VoiceGotResult::Refused;
    case VType::Push: {
      const auto &p = m.u.push;
      return p.id != 0 && p.from <= 1 && p.from != m.floor && p.len >= 1 && p.len <= kVoiceTextMax && text_ok(p.text, p.len);
    }
    case VType::Dismiss: return m.u.dismiss.id != 0;
    case VType::Ping: return true;
  }
  return false;
}

}  // namespace

size_t voice_encode(const VoiceMsg &m, uint8_t *out, size_t cap) {
  if (!out || !fields_ok(m)) return 0;
  // mu-law DATA cannot be told from ADPCM here (no session); its pred and step must be 0 (the C6
  // and the Pi check them against the BEGIN's codec).
  VWriter w(out, cap);
  w.u8(kProtoVersion); w.u8((uint8_t)m.type); w.u8(m.sender); w.u16(m.seq); w.u8(m.floor);
  switch (m.type) {
    case VType::Begin: {
      const auto &b = m.u.begin;
      w.u8(b.xfer); w.u8(b.purpose); w.u8(b.codec); w.u16(b.chunk_samples); w.u8(b.max_chunks);
      break;
    }
    case VType::Data: {
      const auto &d = m.u.data;
      w.u8(d.xfer); w.u8(d.idx); w.u16((uint16_t)d.pred); w.u8(d.step); w.u8(d.len); w.bytes(d.data, d.len);
      break;
    }
    case VType::Ack: w.u8(m.u.ack.xfer); w.u8(m.u.ack.base); w.u32(m.u.ack.bits); w.u8(m.u.ack.state); break;
    case VType::End: w.u8(m.u.end.xfer); w.u8(m.u.end.n_chunks); w.u16(m.u.end.n_samples); w.u8(m.u.end.speech); break;
    case VType::Abort: w.u8(m.u.abort.xfer); w.u8(m.u.abort.reason); break;
    case VType::Text: {
      const auto &t = m.u.text;
      w.u8(t.xfer); w.u8(t.status); w.u8(t.flags); w.u8(t.len); w.bytes(t.text, t.len);
      break;
    }
    case VType::Say: w.u32(m.u.say.id); w.u32(m.u.say.time); w.u8(m.u.say.len); w.bytes(m.u.say.text, m.u.say.len); break;
    case VType::Got: w.u8(m.u.got.kind); w.u32(m.u.got.key); w.u8(m.u.got.result); break;
    case VType::Push: {
      const auto &p = m.u.push;
      w.u32(p.id); w.u8(p.from); w.u32(p.time); w.u8(p.len); w.bytes(p.text, p.len);
      break;
    }
    case VType::Dismiss: w.u32(m.u.dismiss.id); break;
    case VType::Ping: w.u32(m.u.ping.nonce); break;
  }
  return w.ok ? w.n : 0;
}

bool voice_decode(const uint8_t *in, size_t len, VoiceMsg &m) {
  if (!in || len < kVoiceHeader || len > kMaxMsg) return false;
  VReader r(in, len);
  if (r.u8() != kProtoVersion) return false;
  uint8_t type = r.u8();
  if (!is_voice_type(type)) return false;
  m = VoiceMsg();
  m.type = (VType)type;
  m.sender = r.u8();
  m.seq = r.u16();
  m.floor = r.u8();
  size_t want = 0;
  switch (m.type) {
    case VType::Begin: {
      auto &b = m.u.begin;
      b.xfer = r.u8(); b.purpose = r.u8(); b.codec = r.u8(); b.chunk_samples = r.u16(); b.max_chunks = r.u8();
      want = kVoiceBeginLen;
      break;
    }
    case VType::Data: {
      auto &d = m.u.data;
      d.xfer = r.u8(); d.idx = r.u8(); d.pred = (int16_t)r.u16(); d.step = r.u8(); d.len = r.u8();
      want = kVoiceDataBase + d.len;
      if (d.len > kVoiceChunkBytes || len != want) return false;
      memcpy(d.data, in + kVoiceDataBase, d.len);
      break;
    }
    case VType::Ack: {
      auto &a = m.u.ack;
      a.xfer = r.u8(); a.base = r.u8(); a.bits = r.u32(); a.state = r.u8();
      want = kVoiceAckLen;
      break;
    }
    case VType::End: {
      auto &e = m.u.end;
      e.xfer = r.u8(); e.n_chunks = r.u8(); e.n_samples = r.u16(); e.speech = r.u8();
      want = kVoiceEndLen;
      break;
    }
    case VType::Abort: m.u.abort.xfer = r.u8(); m.u.abort.reason = r.u8(); want = kVoiceAbortLen; break;
    case VType::Text: {
      auto &t = m.u.text;
      t.xfer = r.u8(); t.status = r.u8(); t.flags = r.u8(); t.len = r.u8();
      want = kVoiceTextBase + t.len;
      if (t.len > kVoiceTextMax || len != want) return false;
      memcpy(t.text, in + kVoiceTextBase, t.len);
      t.text[t.len] = 0;
      break;
    }
    case VType::Say: {
      auto &s = m.u.say;
      s.id = r.u32(); s.time = r.u32(); s.len = r.u8();
      want = kVoiceSayBase + s.len;
      if (s.len > kVoiceTextMax || len != want) return false;
      memcpy(s.text, in + kVoiceSayBase, s.len);
      s.text[s.len] = 0;
      break;
    }
    case VType::Got: m.u.got.kind = r.u8(); m.u.got.key = r.u32(); m.u.got.result = r.u8(); want = kVoiceGotLen; break;
    case VType::Push: {
      auto &p = m.u.push;
      p.id = r.u32(); p.from = r.u8(); p.time = r.u32(); p.len = r.u8();
      want = kVoicePushBase + p.len;
      if (p.len > kVoiceTextMax || len != want) return false;
      memcpy(p.text, in + kVoicePushBase, p.len);
      p.text[p.len] = 0;
      break;
    }
    case VType::Dismiss: m.u.dismiss.id = r.u32(); want = kVoiceDismissLen; break;
    case VType::Ping: m.u.ping.nonce = r.u32(); want = kVoicePingLen; break;
  }
  if (len != want) return false;
  return fields_ok(m);
}

const char *voice_type_name(uint8_t type) {
  switch (type) {
    case 20: return "VOICE_BEGIN";
    case 21: return "VOICE_DATA";
    case 22: return "VOICE_ACK";
    case 23: return "VOICE_END";
    case 24: return "VOICE_ABORT";
    case 25: return "VOICE_TEXT";
    case 26: return "SAY";
    case 27: return "VOICE_GOT";
    case 28: return "VOICE_PUSH";
    case 29: return "VOICE_DISMISS";
    case 30: return "VOICE_PING";
  }
  return "?";
}

bool voice_peek(const uint8_t *d, size_t len, VoicePeek &p) {
  if (!d || len < kVoiceHeader || len > kMaxMsg || d[0] != kProtoVersion || !is_voice_type(d[1])) return false;
  p.type = d[1];
  p.sender = d[2];
  p.floor = d[5];
  p.kind = 0;
  if (p.type == (uint8_t)VType::Got) {
    if (len != kVoiceGotLen) return false;
    p.kind = d[kVoiceHeader];
  }
  return p.sender <= kSenderGateway && p.floor <= 1;
}

int voice_relay_to(uint8_t type, uint8_t kind, uint8_t from, uint8_t floor, uint8_t me) {
  if (me != kSenderUpstairs || floor != 1) return -1;
  const bool got = type == (uint8_t)VType::Got;
  if (from == kSenderDownstairs) {
    switch ((VType)type) {
      case VType::Begin: case VType::Data: case VType::End: case VType::Abort: case VType::Push: return kSenderGateway;
      default: break;
    }
    if (got && (kind == (uint8_t)VType::Text || kind == (uint8_t)VType::Dismiss)) return kSenderGateway;
    return -1;
  }
  if (from == kSenderGateway) {
    switch ((VType)type) {
      case VType::Ack: case VType::Abort: case VType::Text: case VType::Dismiss: return kSenderDownstairs;
      default: break;
    }
    if (got && kind == (uint8_t)VType::Push) return kSenderDownstairs;
    return -1;
  }
  return -1;
}

int voice_relay_frame(const uint8_t *d, size_t len, uint8_t from, uint8_t me) {
  VoicePeek p;
  if (!voice_peek(d, len, p)) return -1;
  return voice_relay_to(p.type, p.kind, from, p.floor, me);
}

bool voice_keep_first_capital(const char *field, int cursor, uint8_t flags) {
  if (!(flags & kVoiceTextSentenceCap)) return true;
  if (!field || cursor <= 0) return true;
  int n = (int)strlen(field);
  int i = (cursor < n ? cursor : n) - 1;
  while (i >= 0 && field[i] == ' ') i--;
  if (i < 0) return true;
  return field[i] == '.' || field[i] == '?' || field[i] == '!';
}

void voice_apply_capital(char *text, const char *field, int cursor, uint8_t flags) {
  if (!text || !text[0]) return;
  if (voice_keep_first_capital(field, cursor, flags)) return;
  if (text[0] >= 'A' && text[0] <= 'Z') text[0] = (char)(text[0] - 'A' + 'a');
}

const int16_t kImaStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int8_t kImaIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

int voice_adpcm_decode(int16_t pred, uint8_t step, const uint8_t *data, int n, int16_t *out) {
  if (step > kVoiceStepMax || n < 0 || !out || (n > 0 && !data)) return 0;
  int p = pred, idx = step;
  for (int i = 0; i < n; i++) {
    uint8_t b = data[i >> 1];
    int code = (i & 1) ? (b >> 4) : (b & 0x0F);  // LOW nibble first
    int st = kImaStep[idx];
    int diff = st >> 3;
    if (code & 4) diff += st;
    if (code & 2) diff += st >> 1;
    if (code & 1) diff += st >> 2;
    p += (code & 8) ? -diff : diff;
    if (p > 32767) p = 32767;
    if (p < -32768) p = -32768;
    idx += kImaIndex[code];
    if (idx < 0) idx = 0;
    if (idx > kVoiceStepMax) idx = kVoiceStepMax;
    out[i] = (int16_t)p;
  }
  return n;
}

int16_t voice_mulaw_decode(uint8_t b) {
  b = (uint8_t)~b;
  int sign = b & 0x80, exponent = (b >> 4) & 7, mantissa = b & 0x0F;
  int s = (((mantissa << 3) + 0x84) << exponent) - 0x84;
  return (int16_t)(sign ? -s : s);
}

size_t voice_rec_bytes(uint8_t max_chunks) {
  size_t head = (sizeof(VoiceRec) + 7) & ~(size_t)7;
  size_t chunks = (sizeof(VoiceChunk) * max_chunks + 3) & ~(size_t)3;
  return head + chunks + sizeof(uint32_t) * max_chunks;
}

VoiceRec *voice_rec_init(void *mem, size_t n, uint8_t max_chunks) {
  if (!mem || max_chunks == 0 || !voice_chunks_ok(max_chunks) || n < voice_rec_bytes(max_chunks)) return nullptr;
  memset(mem, 0, voice_rec_bytes(max_chunks));
  VoiceRec *r = new (mem) VoiceRec();
  uint8_t *base = (uint8_t *)mem;
  size_t head = (sizeof(VoiceRec) + 7) & ~(size_t)7;
  size_t chunks = (sizeof(VoiceChunk) * max_chunks + 3) & ~(size_t)3;
  r->max_chunks = max_chunks;
  r->chunks = (VoiceChunk *)(base + head);
  r->first_ms = (uint32_t *)(base + head + chunks);
  return r;
}

void voice_rec_destroy(VoiceRec *r) {
  if (r) r->~VoiceRec();
}

bool voice_inbox_valid(const VoiceInboxData &d) {
  if (d.version != VoiceInboxData::kVersion || d.n > kVoiceInboxMax || d.seen_next >= kVoiceSeenIds) return false;
  for (int i = 0; i < d.n; i++) {
    const VoiceNote &v = d.msg[i];
    if (v.id == 0 || v.from > 1 || v.len == 0 || v.len > kVoiceTextMax || v.text[v.len] != 0) return false;
    if ((int)strlen(v.text) != v.len || !text_ok(v.text, v.len)) return false;
  }
  return true;
}

}  // namespace tc
