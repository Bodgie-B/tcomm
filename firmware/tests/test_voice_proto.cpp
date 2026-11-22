// Voice-notes frames (core/voice_proto.{h,cpp}, piece P0): every type round trip, each bad length
// and bad field refused, the old decode() still refusing 20-30, the relay rule table, the capital
// rule, the ADPCM and mu-law decoders against the golden vectors, and the shared structs.
#include "voice_test.h"
#include <set>
#include <string.h>
#include "core/voice_proto.h"
#include "core/protocol.h"
#include "core/types.h"

using namespace tc;

namespace {

void set_text(char *dst, uint8_t &len, const char *s) {
  len = (uint8_t)strlen(s);
  memcpy(dst, s, len);
  dst[len] = 0;
}

std::string repeat_text(size_t n) {
  std::string s;
  for (size_t i = 0; i < n; i++) s += (char)(0x20 + (i * 7) % 95);  // every printable byte appears
  return s;
}

// One valid message of each type, with every field away from zero where it can be.
std::vector<VoiceMsg> samples() {
  std::vector<VoiceMsg> v;
  VoiceMsg m;
  m.sender = kSenderDownstairs; m.seq = 0xBEEF; m.floor = 1;

  m.type = VType::Begin;
  m.u.begin.xfer = 7; m.u.begin.purpose = 2; m.u.begin.codec = 2; m.u.begin.chunk_samples = 474;
  m.u.begin.max_chunks = (uint8_t)kVoiceClipChunks;
  v.push_back(m);
  m.u.begin.codec = 1; m.u.begin.chunk_samples = 237; m.u.begin.max_chunks = 255; m.u.begin.purpose = 1;
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderDownstairs; m.seq = 3; m.floor = 1;
  m.type = VType::Data;
  m.u.data.xfer = 255; m.u.data.idx = 254; m.u.data.pred = -32768; m.u.data.step = 88; m.u.data.len = 237;
  for (int i = 0; i < 237; i++) m.u.data.data[i] = (uint8_t)(i * 13 + 1);
  v.push_back(m);
  m.u.data.len = 1; m.u.data.idx = 0; m.u.data.pred = 32767; m.u.data.step = 0;
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderGateway; m.seq = 9; m.floor = 0;
  m.type = VType::Ack;
  m.u.ack.xfer = 7; m.u.ack.base = 33; m.u.ack.bits = 0xDEADBEEF; m.u.ack.state = 1;
  v.push_back(m);
  m.u.ack.state = 3; m.u.ack.base = 255; m.u.ack.bits = 0;
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderUpstairs; m.floor = 0;
  m.type = VType::End;
  m.u.end.xfer = 7; m.u.end.n_chunks = 96; m.u.end.n_samples = 45504; m.u.end.speech = 1;
  v.push_back(m);
  m.u.end.n_chunks = 0; m.u.end.n_samples = 0; m.u.end.speech = 0;
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderGateway; m.floor = 1;
  m.type = VType::Abort;
  m.u.abort.xfer = 1; m.u.abort.reason = 7;
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderGateway; m.floor = 1;
  m.type = VType::Text;
  m.u.text.xfer = 7; m.u.text.status = 0; m.u.text.flags = kVoiceTextSentenceCap;
  {
    std::string t = repeat_text(120);
    set_text(m.u.text.text, m.u.text.len, t.c_str());
  }
  v.push_back(m);
  m.u.text.status = 6; m.u.text.flags = 0; m.u.text.len = 0; m.u.text.text[0] = 0;
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderUpstairs; m.floor = 0; m.seq = 1;
  m.type = VType::Say;
  m.u.say.id = 0xDEADBEEF; m.u.say.time = 1763650000;
  set_text(m.u.say.text, m.u.say.len, "Dinner is ready");
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderDownstairs; m.floor = 1;
  m.type = VType::Got;
  m.u.got.kind = 25; m.u.got.key = voice_text_key(7, 6); m.u.got.result = 2;
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderDownstairs; m.floor = 1;
  m.type = VType::Push;
  m.u.push.id = 42; m.u.push.from = 0; m.u.push.time = 0;
  {
    std::string t = repeat_text(120);
    set_text(m.u.push.text, m.u.push.len, t.c_str());
  }
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderGateway; m.floor = 1;
  m.type = VType::Dismiss;
  m.u.dismiss.id = 0xFFFFFFFF;
  v.push_back(m);

  m = VoiceMsg(); m.sender = kSenderDownstairs; m.floor = 1;
  m.type = VType::Ping;
  m.u.ping.nonce = 0;
  v.push_back(m);
  return v;
}

bool same(const VoiceMsg &a, const VoiceMsg &b) {
  if (a.type != b.type || a.sender != b.sender || a.seq != b.seq || a.floor != b.floor) return false;
  switch (a.type) {
    case VType::Begin:
      return a.u.begin.xfer == b.u.begin.xfer && a.u.begin.purpose == b.u.begin.purpose && a.u.begin.codec == b.u.begin.codec &&
             a.u.begin.chunk_samples == b.u.begin.chunk_samples && a.u.begin.max_chunks == b.u.begin.max_chunks;
    case VType::Data:
      return a.u.data.xfer == b.u.data.xfer && a.u.data.idx == b.u.data.idx && a.u.data.pred == b.u.data.pred &&
             a.u.data.step == b.u.data.step && a.u.data.len == b.u.data.len && !memcmp(a.u.data.data, b.u.data.data, a.u.data.len);
    case VType::Ack:
      return a.u.ack.xfer == b.u.ack.xfer && a.u.ack.base == b.u.ack.base && a.u.ack.bits == b.u.ack.bits && a.u.ack.state == b.u.ack.state;
    case VType::End:
      return a.u.end.xfer == b.u.end.xfer && a.u.end.n_chunks == b.u.end.n_chunks && a.u.end.n_samples == b.u.end.n_samples &&
             a.u.end.speech == b.u.end.speech;
    case VType::Abort: return a.u.abort.xfer == b.u.abort.xfer && a.u.abort.reason == b.u.abort.reason;
    case VType::Text:
      return a.u.text.xfer == b.u.text.xfer && a.u.text.status == b.u.text.status && a.u.text.flags == b.u.text.flags &&
             a.u.text.len == b.u.text.len && !strcmp(a.u.text.text, b.u.text.text);
    case VType::Say:
      return a.u.say.id == b.u.say.id && a.u.say.time == b.u.say.time && a.u.say.len == b.u.say.len && !strcmp(a.u.say.text, b.u.say.text);
    case VType::Got: return a.u.got.kind == b.u.got.kind && a.u.got.key == b.u.got.key && a.u.got.result == b.u.got.result;
    case VType::Push:
      return a.u.push.id == b.u.push.id && a.u.push.from == b.u.push.from && a.u.push.time == b.u.push.time &&
             a.u.push.len == b.u.push.len && !strcmp(a.u.push.text, b.u.push.text);
    case VType::Dismiss: return a.u.dismiss.id == b.u.dismiss.id;
    case VType::Ping: return a.u.ping.nonce == b.u.ping.nonce;
  }
  return false;
}

size_t expected_len(const VoiceMsg &m) {
  switch (m.type) {
    case VType::Begin: return 12;
    case VType::Data: return 12 + m.u.data.len;
    case VType::Ack: return 13;
    case VType::End: return 11;
    case VType::Abort: return 8;
    case VType::Text: return 10 + m.u.text.len;
    case VType::Say: return 15 + m.u.say.len;
    case VType::Got: return 12;
    case VType::Push: return 16 + m.u.push.len;
    case VType::Dismiss: return 10;
    case VType::Ping: return 10;
  }
  return 0;
}

// Encodes m (must succeed), then patches byte `at` to `value`, and says whether voice_decode takes it.
bool decodes_with(const VoiceMsg &m, size_t at, uint8_t value) {
  uint8_t buf[kMaxMsg];
  size_t n = voice_encode(m, buf, sizeof(buf));
  if (!n || at >= n) return true;  // reported as accepted, so the caller's CHECK fails
  buf[at] = value;
  VoiceMsg out;
  return voice_decode(buf, n, out);
}

bool encodes(const VoiceMsg &m) {
  uint8_t buf[kMaxMsg];
  return voice_encode(m, buf, sizeof(buf)) != 0;
}

}  // namespace

VTEST(voice_round_trip_every_type) {
  std::set<int> types;
  for (const VoiceMsg &m : samples()) {
    uint8_t buf[kMaxMsg];
    size_t n = voice_encode(m, buf, sizeof(buf));
    CHECK(n == expected_len(m));
    CHECK(n <= kMaxMsg);
    CHECK(buf[0] == kProtoVersion && buf[1] == (uint8_t)m.type && buf[2] == m.sender && buf[5] == m.floor);
    CHECK(buf[3] == (uint8_t)m.seq && buf[4] == (uint8_t)(m.seq >> 8));
    VoiceMsg back;
    CHECK(voice_decode(buf, n, back));
    CHECK(same(m, back));
    CHECK(is_voice_frame(buf, n));
    types.insert((int)m.type);
    // Too small a buffer: refused, never a partial frame.
    CHECK(voice_encode(m, buf, n - 1) == 0);
  }
  CHECK(types.size() == 11);
}

VTEST(voice_layout_bytes) {
  // A few fields at their frozen byte offsets, little-endian.
  VoiceMsg m;
  m.type = VType::Data; m.sender = 1; m.seq = 0x0102; m.floor = 1;
  m.u.data.xfer = 9; m.u.data.idx = 4; m.u.data.pred = -2; m.u.data.step = 5; m.u.data.len = 2;
  m.u.data.data[0] = 0xAB; m.u.data.data[1] = 0xCD;
  uint8_t b[kMaxMsg];
  CHECK(voice_encode(m, b, sizeof(b)) == 14);
  const uint8_t want[14] = {1, 21, 1, 0x02, 0x01, 1, 9, 4, 0xFE, 0xFF, 5, 2, 0xAB, 0xCD};
  CHECK(!memcmp(b, want, 14));

  VoiceMsg t;
  t.type = VType::Text; t.sender = 2; t.floor = 1;
  t.u.text.xfer = 7; t.u.text.status = 0; t.u.text.flags = 1;
  set_text(t.u.text.text, t.u.text.len, "Hi");
  CHECK(voice_encode(t, b, sizeof(b)) == 12);
  const uint8_t want_t[12] = {1, 25, 2, 0, 0, 1, 7, 0, 1, 2, 'H', 'i'};
  CHECK(!memcmp(b, want_t, 12));

  VoiceMsg a;
  a.type = VType::Ack; a.sender = 2; a.floor = 0;
  a.u.ack.xfer = 3; a.u.ack.base = 8; a.u.ack.bits = 0x04030201; a.u.ack.state = 2;
  CHECK(voice_encode(a, b, sizeof(b)) == 13);
  const uint8_t want_a[13] = {1, 22, 2, 0, 0, 0, 3, 8, 1, 2, 3, 4, 2};
  CHECK(!memcmp(b, want_a, 13));
}

VTEST(voice_bad_lengths_refused) {
  for (const VoiceMsg &m : samples()) {
    uint8_t buf[kMaxMsg + 2] = {};
    size_t n = voice_encode(m, buf, sizeof(buf));
    CHECK(n > 0);
    VoiceMsg out;
    CHECK(!voice_decode(buf, n - 1, out));
    CHECK(!voice_decode(buf, n + 1, out));
    for (size_t k = 0; k < kVoiceHeader; k++) CHECK(!voice_decode(buf, k, out));
  }
  VoiceMsg out;
  CHECK(!voice_decode(nullptr, 10, out));
  uint8_t big[kMaxMsg + 1] = {1, 21};
  CHECK(!voice_decode(big, sizeof(big), out));
}

VTEST(voice_bad_header_refused) {
  for (const VoiceMsg &m : samples()) {
    CHECK(!decodes_with(m, 0, 2));        // version
    CHECK(!decodes_with(m, 0, 0));
    CHECK(!decodes_with(m, 1, 19));       // an old type
    CHECK(!decodes_with(m, 1, 31));       // the reach test's type
    CHECK(!decodes_with(m, 2, 3));        // sender
    CHECK(!decodes_with(m, 5, 2));        // floor
  }
}

VTEST(voice_bad_fields_refused) {
  std::vector<VoiceMsg> s = samples();
  VoiceMsg begin = s[0], data = s[2], ack = s[4], end = s[6], abort_ = s[8], text = s[9], queued = s[10], say = s[11],
           got = s[12], push = s[13], dismiss = s[14];
  // BEGIN: xfer, purpose, codec, chunk_samples per codec, max_chunks.
  CHECK(!decodes_with(begin, 6, 0));
  CHECK(!decodes_with(begin, 7, 0));
  CHECK(!decodes_with(begin, 7, 3));
  CHECK(!decodes_with(begin, 8, 0));
  CHECK(!decodes_with(begin, 8, 3));
  CHECK(!decodes_with(begin, 8, 1));     // codec 1 with 474 samples
  CHECK(!decodes_with(begin, 9, 0xED));  // ADPCM with chunk_samples 0x01ED = 493
  {
    VoiceMsg z = begin;
    z.u.begin.chunk_samples = 237;         // ADPCM with mu-law's chunk size
    CHECK(!encodes(z));
  }
  CHECK(!decodes_with(begin, 11, 0));
  CHECK(decodes_with(begin, 11, 255));  // 255 since 2026-11-21 (mu-law's 3 s clip is 210 chunks)
  CHECK(decodes_with(begin, 11, 129));
  CHECK(decodes_with(begin, 11, 1));
  // DATA: xfer, idx, step, len.
  CHECK(!decodes_with(data, 6, 0));
  CHECK(!decodes_with(data, 7, 255));    // idx < 255
  CHECK(decodes_with(data, 7, 254));
  CHECK(decodes_with(data, 7, 128));
  CHECK(!decodes_with(data, 10, 89));
  CHECK(decodes_with(data, 10, 88));
  {
    VoiceMsg d = data;
    d.u.data.len = 0;
    CHECK(!encodes(d));
    d.u.data.len = 238;
    CHECK(!encodes(d));
    uint8_t b[kMaxMsg];
    d = data;
    size_t n = voice_encode(d, b, sizeof(b));
    b[11] = 0;  // len 0 in a frame that is 12 bytes would need n = 12
    VoiceMsg out;
    CHECK(!voice_decode(b, 12, out));
    CHECK(!voice_decode(b, n, out));
  }
  // ACK: xfer, state, base.
  CHECK(!decodes_with(ack, 6, 0));
  CHECK(!decodes_with(ack, 12, 0));
  CHECK(!decodes_with(ack, 12, 4));
  CHECK(decodes_with(ack, 7, 255));      // base 0-255: every u8 value
  CHECK(decodes_with(ack, 7, 129));
  // END: xfer, n_chunks, speech, n_samples above n_chunks x 474.
  CHECK(!decodes_with(end, 6, 0));
  CHECK(decodes_with(end, 7, 255));      // n_chunks 0-255
  CHECK(decodes_with(end, 7, 129));
  CHECK(!decodes_with(end, 10, 2));
  CHECK(!decodes_with(end, 7, 1));       // 1 chunk cannot hold 45504 samples
  // ABORT: xfer, reason.
  CHECK(!decodes_with(abort_, 6, 0));
  CHECK(!decodes_with(abort_, 7, 0));
  CHECK(!decodes_with(abort_, 7, 8));
  // TEXT: xfer, status, flags, text bytes, status/len pairing.
  CHECK(!decodes_with(text, 6, 0));
  CHECK(!decodes_with(text, 7, 7));
  CHECK(!decodes_with(text, 7, 1));      // a "nothing heard" with text
  CHECK(!decodes_with(text, 8, 2));      // an unknown flag bit
  CHECK(!decodes_with(text, 8, 0x81));
  CHECK(decodes_with(text, 8, 0));
  CHECK(!decodes_with(text, 10, 0x7F));
  CHECK(!decodes_with(text, 10, 0x1F));
  CHECK(!decodes_with(text, 129, 0));    // a NUL inside
  CHECK(!decodes_with(queued, 7, 0));    // Ok with no text
  // SAY: id 0, empty, bad byte.
  {
    VoiceMsg z = say;
    z.u.say.id = 0;
    CHECK(!encodes(z));
    z = say;
    z.u.say.len = 0;
    CHECK(!encodes(z));
    z = say;
    z.u.say.text[3] = (char)0x80;
    CHECK(!encodes(z));
    z = say;
    z.u.say.len = 121;
    CHECK(!encodes(z));
  }
  CHECK(!decodes_with(say, 15, 0x7F));
  // GOT: kind, result.
  for (int k = 0; k < 256; k++) {
    bool ok = k == 25 || k == 26 || k == 28 || k == 29 || k == 30;
    CHECK(decodes_with(got, 6, (uint8_t)k) == ok);
  }
  CHECK(!decodes_with(got, 11, 3));
  // PUSH: id 0, from, from == floor.
  {
    VoiceMsg z = push;
    z.u.push.id = 0;
    CHECK(!encodes(z));
  }
  CHECK(!decodes_with(push, 10, 2));
  CHECK(!decodes_with(push, 10, 1));     // from downstairs on a frame whose floor is downstairs
  // DISMISS: id 0.
  {
    VoiceMsg z = dismiss;
    z.u.dismiss.id = 0;
    CHECK(!encodes(z));
  }
}

VTEST(voice_old_decode_refuses_voice_frames) {
  for (const VoiceMsg &m : samples()) {
    uint8_t buf[kMaxMsg];
    size_t n = voice_encode(m, buf, sizeof(buf));
    Msg old;
    CHECK(!decode(buf, n, old));
  }
  // And for every voice type at any short length.
  for (int t = kVoiceTypeFirst; t <= kVoiceTypeLast; t++)
    for (size_t n = 5; n <= 20; n++) {
      uint8_t buf[32] = {kProtoVersion, (uint8_t)t, 1};
      Msg old;
      CHECK(!decode(buf, n, old));
    }
  // voice_decode refuses every old type.
  Msg st;
  st.type = MsgType::State;
  st.u.state.status = 0;
  st.u.state.fallback = 0;
  uint8_t buf[kMaxMsg];
  size_t n = encode(st, buf, sizeof(buf));
  CHECK(n > 0);
  VoiceMsg v;
  CHECK(!voice_decode(buf, n, v));
  CHECK(!is_voice_frame(buf, n));
  for (int t = 1; t < kVoiceTypeFirst; t++) {
    buf[1] = (uint8_t)t;
    CHECK(!voice_decode(buf, n, v));
    CHECK(!is_voice_frame(buf, n));
  }
}

VTEST(voice_names) {
  CHECK(!strcmp(voice_type_name(20), "VOICE_BEGIN"));
  CHECK(!strcmp(voice_type_name(26), "SAY"));
  CHECK(!strcmp(voice_type_name(30), "VOICE_PING"));
  CHECK(!strcmp(voice_type_name(31), "?"));
  CHECK(!strcmp(voice_type_name(19), "?"));
}

// The relay table as DESIGN 4.7 writes it, kept separate from voice_relay_to's switch on purpose.
static int expected_relay(int type, int kind, int from, int floor, int me) {
  if (me != 0 || floor != 1) return -1;
  struct Row { int from, type, kind, to; };
  static const Row rows[] = {
      {1, 20, -1, 2}, {1, 21, -1, 2}, {1, 23, -1, 2}, {1, 24, -1, 2}, {1, 28, -1, 2}, {1, 27, 25, 2}, {1, 27, 29, 2},
      {2, 22, -1, 1}, {2, 24, -1, 1}, {2, 25, -1, 1}, {2, 29, -1, 1}, {2, 27, 28, 1},
  };
  for (const Row &r : rows)
    if (r.from == from && r.type == type && (r.kind < 0 || r.kind == kind)) return r.to;
  return -1;
}

VTEST(voice_relay_table) {
  int relayed = 0;
  for (int type = 0; type <= 40; type++)
    for (int kind = 0; kind <= 40; kind++)
      for (int from = 0; from <= 3; from++)
        for (int floor = 0; floor <= 2; floor++)
          for (int me = 0; me <= 2; me++) {
            // kind only matters for a GOT; any other type ignores it.
            int want = expected_relay(type, kind, from, floor, me);
            int got = voice_relay_to((uint8_t)type, (uint8_t)kind, (uint8_t)from, (uint8_t)floor, (uint8_t)me);
            CHECK(got == want);
            if (got >= 0) {
              if (type == 27 || kind == 0) relayed++;
              CHECK(got != from);  // never back where it came from
            }
          }
  CHECK(relayed == 12);  // the 12 rows, only for me upstairs and floor 1
  // Never relayed whatever the route: SAY, a GOT for SAY, PING, a GOT for PING.
  for (int from = 0; from <= 2; from++) {
    CHECK(voice_relay_to(26, 0, (uint8_t)from, 1, 0) < 0);
    CHECK(voice_relay_to(27, 26, (uint8_t)from, 1, 0) < 0);
    CHECK(voice_relay_to(30, 0, (uint8_t)from, 1, 0) < 0);
    CHECK(voice_relay_to(27, 30, (uint8_t)from, 1, 0) < 0);
  }
}

VTEST(voice_relay_from_frame_bytes) {
  for (const VoiceMsg &m : samples()) {
    uint8_t buf[kMaxMsg];
    size_t n = voice_encode(m, buf, sizeof(buf));
    VoicePeek p;
    CHECK(voice_peek(buf, n, p));
    CHECK(p.type == (uint8_t)m.type && p.sender == m.sender && p.floor == m.floor);
    CHECK(p.kind == (m.type == VType::Got ? m.u.got.kind : 0));
    for (int from = 0; from <= 2; from++)
      CHECK(voice_relay_frame(buf, n, (uint8_t)from, 0) ==
            voice_relay_to(p.type, p.kind, (uint8_t)from, p.floor, 0));
    // The relay rewrites only the sender byte; the frame still decodes and nothing else moved.
    uint8_t copy[kMaxMsg];
    memcpy(copy, buf, n);
    voice_set_sender(copy, kSenderUpstairs);
    VoiceMsg a, b;
    CHECK(voice_decode(buf, n, a) && voice_decode(copy, n, b));
    CHECK(b.sender == kSenderUpstairs);
    b.sender = a.sender;
    CHECK(same(a, b));
  }
  uint8_t junk[6] = {1, 21, 1, 0, 0, 3};
  VoicePeek p;
  CHECK(!voice_peek(junk, 6, p));  // floor 3
  CHECK(voice_relay_frame(junk, 6, 1, 0) == -1);
  uint8_t short_got[7] = {1, 27, 1, 0, 0, 1, 25};
  CHECK(!voice_peek(short_got, 7, p));
}

VTEST(voice_text_key_and_window) {
  CHECK(voice_text_key(7, 0) == 7);
  CHECK(voice_text_key(7, 6) == 7 + 6 * 256);
  CHECK(voice_text_key(7, 6) != voice_text_key(7, 0));
  CHECK(voice_in_window(0, 0));
  CHECK(voice_in_window(31, 0));
  CHECK(!voice_in_window(32, 0));   // check finding 6: 32 past base is never a first send
  CHECK(voice_in_window(127, 96));
  CHECK(!voice_in_window(128, 96));
  CHECK(!voice_in_window(-1, 0));
}

VTEST(voice_capital_rule) {
  const uint8_t S = kVoiceTextSentenceCap;
  struct Case { const char *field; int cursor; uint8_t flags; bool keep; };
  const Case cases[] = {
      {"", 0, S, true},                     // empty field
      {"hello", 0, S, true},                // cursor at 0
      {"   ", 3, S, true},                  // only spaces before
      {"Back at", 7, S, false},             // mid-sentence: lowercase
      {"Back at ", 8, S, false},
      {"Done. ", 6, S, true},               // after ". "
      {"Done? ", 6, S, true},
      {"Done! ", 6, S, true},
      {"Done.", 5, S, true},                // the insert adds the space
      {"Done,", 5, S, false},
      {"Back at", 7, 0, true},              // flag clear: a name or "I" stays
      {"Done. later", 6, S, true},          // cursor mid-field, right after ". "
      {"Done. later", 11, S, false},
      {"abc", 99, S, false},                // a cursor past the end counts as the end
  };
  for (const Case &c : cases) CHECK(voice_keep_first_capital(c.field, c.cursor, c.flags) == c.keep);
  char t[16] = "Dinner is ready";
  voice_apply_capital(t, "Back at", 7, S);
  CHECK(!strcmp(t, "dinner is ready"));
  char u[16] = "Dinner is ready";
  voice_apply_capital(u, "Done. ", 6, S);
  CHECK(!strcmp(u, "Dinner is ready"));
  char w[8] = "123 go";
  voice_apply_capital(w, "x", 1, S);
  CHECK(!strcmp(w, "123 go"));
}

VTEST(voice_adpcm_tables) {
  CHECK(kImaStep[0] == 7 && kImaStep[88] == 32767);
  for (int i = 1; i < 89; i++) CHECK(kImaStep[i] > kImaStep[i - 1]);
  for (int i = 0; i < 8; i++) CHECK(kImaIndex[i] == kImaIndex[i + 8]);
  int16_t out[4];
  uint8_t d[2] = {0, 0};
  CHECK(voice_adpcm_decode(0, 89, d, 4, out) == 0);
  CHECK(voice_adpcm_decode(0, 0, d, 4, out) == 4);
}

VTEST(voice_adpcm_golden_vectors) {
  const char *sets[] = {"sweep", "edge"};
  for (const char *name : sets) {
    std::vector<int16_t> input, decoded;
    std::vector<vt::Chunk> chunks;
    char f1[64], f2[64], f3[64];
    snprintf(f1, sizeof(f1), "voice_adpcm_%s_input.txt", name);
    snprintf(f2, sizeof(f2), "voice_adpcm_%s_chunks.txt", name);
    snprintf(f3, sizeof(f3), "voice_adpcm_%s_decoded.txt", name);
    CHECK(vt::read_samples(f1, input));
    CHECK(vt::read_chunks(f2, chunks));
    CHECK(vt::read_samples(f3, decoded));
    CHECK(!input.empty() && input.size() == decoded.size());
    CHECK(chunks.size() == (input.size() + 473) / 474);
    std::vector<int16_t> mine;
    size_t left = input.size();
    for (size_t i = 0; i < chunks.size(); i++) {
      const vt::Chunk &c = chunks[i];
      CHECK(c.idx == (int)i);
      int n = (int)(left < 474 ? left : 474);
      CHECK((int)c.data.size() == (n + 1) / 2);
      CHECK(c.step >= 0 && c.step <= 88);
      // Each chunk decodes alone from its own header.
      int16_t out[474];
      CHECK(voice_adpcm_decode((int16_t)c.pred, (uint8_t)c.step, c.data.data(), n, out) == n);
      mine.insert(mine.end(), out, out + n);
      // Each chunk fits a DATA frame and survives the round trip.
      VoiceMsg m;
      m.type = VType::Data; m.sender = 1; m.floor = 1;
      m.u.data.xfer = 1; m.u.data.idx = (uint8_t)i; m.u.data.pred = (int16_t)c.pred; m.u.data.step = (uint8_t)c.step;
      m.u.data.len = (uint8_t)c.data.size();
      memcpy(m.u.data.data, c.data.data(), c.data.size());
      uint8_t b[kMaxMsg];
      VoiceMsg back;
      size_t bn = voice_encode(m, b, sizeof(b));
      CHECK(bn > 0 && voice_decode(b, bn, back) && same(m, back));
      left -= (size_t)n;
    }
    CHECK(mine == decoded);
  }
}

VTEST(voice_mulaw_golden_vectors) {
  std::vector<vt::Chunk> chunks;
  std::vector<int16_t> decoded;
  CHECK(vt::read_chunks("voice_mulaw_sweep_chunks.txt", chunks));
  CHECK(vt::read_samples("voice_mulaw_sweep_decoded.txt", decoded));
  std::vector<int16_t> mine;
  for (const vt::Chunk &c : chunks) {
    CHECK(c.pred == 0 && c.step == 0 && c.data.size() <= 237);
    for (uint8_t b : c.data) mine.push_back(voice_mulaw_decode(b));
  }
  CHECK(mine == decoded);
  CHECK(voice_mulaw_decode(0xFF) == 0);
  CHECK(voice_mulaw_decode(0x00) == -32124);
  CHECK(voice_mulaw_decode(0x80) == 32124);
}

VTEST(voice_rec_block) {
  CHECK(kVoiceClipChunks == 104);
  size_t n = voice_rec_bytes((uint8_t)kVoiceClipChunks);
  CHECK(n <= 27 * 1024);  // DESIGN 3.3: about 27 KB of PSRAM per job
  std::vector<uint8_t> mem(n + 16);
  CHECK(voice_rec_init(mem.data(), n - 1, (uint8_t)kVoiceClipChunks) == nullptr);
  CHECK(voice_rec_init(mem.data(), n, 0) == nullptr);
  CHECK(voice_rec_init(mem.data(), n, 129) == nullptr);  // too small for 129 chunks
  CHECK(voice_rec_init(mem.data(), n, 255) == nullptr);
  VoiceRec *r = voice_rec_init(mem.data(), n, (uint8_t)kVoiceClipChunks);
  CHECK(r != nullptr);
  CHECK(r->max_chunks == kVoiceClipChunks && r->chunks_ready.load() == 0 && r->stop.load() == 0);
  CHECK((uint8_t *)r->chunks >= mem.data() + sizeof(VoiceRec));
  CHECK((uint8_t *)(r->first_ms + kVoiceClipChunks) <= mem.data() + n);
  CHECK((uint8_t *)(r->chunks + kVoiceClipChunks) <= (uint8_t *)r->first_ms);
  CHECK(((uintptr_t)r->first_ms & 3) == 0);
  r->chunks[kVoiceClipChunks - 1].data[kVoiceChunkBytes - 1] = 0x5A;  // the last byte is inside
  r->first_ms[kVoiceClipChunks - 1] = 0xFFFFFFFF;
  CHECK(r->chunks[kVoiceClipChunks - 1].data[kVoiceChunkBytes - 1] == 0x5A);
  voice_rec_destroy(r);
}

// Field fix 2026-11-21: mu-law by default, 255 chunks of index space.
VTEST(voice_rec_block_mulaw_255) {
  CHECK(kVoiceMaxChunks == 255);
  CHECK(kVoiceDefaultCodec == VoiceCodec::Mulaw);
  CHECK(kVoiceClipChunksMulaw == 210);
  CHECK(voice_clip_chunks(1) == 210 && voice_clip_chunks(2) == 104 && voice_clip_chunks(0) == 104);
  CHECK(voice_chunk_samples(1) == 237 && voice_chunk_samples(2) == 474);
  // 3.0 s plus the pre-roll lead and the last block fits: (3000 + 80) ms at 16 kHz.
  CHECK((uint32_t)(kVoiceClipChunksMulaw - 2) * kVoiceChunkSamplesMulaw >= (kVoiceRecMs + 80) * 16);
  CHECK((uint32_t)kVoiceClipChunksMulaw * kVoiceChunkSamplesMulaw <= 65535u);  // END's n_samples is u16
  CHECK(voice_chunks_ok(255) && !voice_chunks_ok(256) && !voice_chunks_ok(-1));
  for (int mc : {kVoiceClipChunksMulaw, 255}) {
    size_t n = voice_rec_bytes((uint8_t)mc);
    CHECK(n <= 64 * 1024);  // about 51 KB of PSRAM for mu-law's clip, 62 KB at the full 255
    std::vector<uint8_t> mem(n + 16, 0xEE);
    VoiceRec *r = voice_rec_init(mem.data(), n, (uint8_t)mc);
    CHECK(r != nullptr);
    if (!r) continue;
    CHECK(r->max_chunks == mc);
    CHECK((uint8_t *)(r->first_ms + mc) <= mem.data() + n);
    CHECK((uint8_t *)(r->chunks + mc) <= (uint8_t *)r->first_ms);
    r->chunks[mc - 1].data[kVoiceChunkBytes - 1] = 0x5A;
    r->first_ms[mc - 1] = 0xFFFFFFFF;
    CHECK(mem[n] == 0xEE);  // nothing written past the block
    // The counters hold the last chunk: chunks_ready, first_sent and base are u8.
    r->chunks_ready.store((uint8_t)mc);
    r->first_sent.store((uint8_t)mc);
    r->base.store((uint8_t)mc);
    CHECK(r->chunks_ready.load() == mc && r->first_sent.load() == mc && r->base.load() == mc);
    voice_rec_destroy(r);
  }
  // The window rule near the top of the index space.
  CHECK(voice_in_window(254, 223) && !voice_in_window(255, 223));
  // Frames at the top: DATA idx 254, ACK base 255 with state 2, END 255 chunks of mu-law.
  VoiceMsg m;
  m.sender = kSenderDownstairs;
  m.floor = 1;
  m.type = VType::Data;
  m.u.data.xfer = 3;
  m.u.data.idx = 254;
  m.u.data.len = 237;
  uint8_t b[kMaxMsg];
  VoiceMsg o;
  size_t n = voice_encode(m, b, sizeof b);
  CHECK(n == kVoiceDataBase + 237 && voice_decode(b, n, o) && o.u.data.idx == 254);
  m = VoiceMsg();
  m.sender = kSenderGateway;
  m.type = VType::Ack;
  m.u.ack.xfer = 3;
  m.u.ack.base = 255;
  m.u.ack.state = 2;
  n = voice_encode(m, b, sizeof b);
  CHECK(n == kVoiceAckLen && voice_decode(b, n, o) && o.u.ack.base == 255);
  m = VoiceMsg();
  m.sender = kSenderDownstairs;
  m.type = VType::End;
  m.u.end.xfer = 3;
  m.u.end.n_chunks = 255;
  m.u.end.n_samples = (uint16_t)(255 * 237);
  n = voice_encode(m, b, sizeof b);
  CHECK(n == kVoiceEndLen && voice_decode(b, n, o) && o.u.end.n_chunks == 255 && o.u.end.n_samples == 60435);
}

VTEST(voice_stub_platform_defaults) {
  vt::StubPlatform p;
  tc::Platform &base = p;
  std::vector<uint8_t> mem(voice_rec_bytes(8));
  VoiceRec *r = voice_rec_init(mem.data(), mem.size(), 8);
  CHECK(!base.voice_rec_begin(r));  // no microphone on a plain Platform
  base.voice_rec_stop(r, VoiceStop::StopKey);
  base.voice_rec_stop(r, VoiceStop::Cancel);  // the first stop asked for stays
  CHECK(r->stop.load() == (uint8_t)VoiceStop::StopKey);
  CHECK(base.voice_rec_release(r));
  voice_rec_destroy(r);
  uint8_t f[3] = {1, 30, 1};
  CHECK(base.radio_send_voice(2, f, 3));
  CHECK(p.voice_out.size() == 1 && p.voice_out[0].to == 2);
  VoiceInboxData d;
  CHECK(!base.load_voice_inbox(d));
  d.n = 1;
  d.msg[0].id = 5;
  d.msg[0].from = 0;
  set_text(d.msg[0].text, d.msg[0].len, "Back at six");
  CHECK(voice_inbox_valid(d));
  base.save_voice_inbox(d);
  VoiceInboxData e;
  CHECK(base.load_voice_inbox(e) && e.n == 1 && !strcmp(e.msg[0].text, "Back at six"));
}

VTEST(voice_inbox_validity) {
  VoiceInboxData d;
  CHECK(voice_inbox_valid(d));  // empty
  d.version = 2;
  CHECK(!voice_inbox_valid(d));
  d = VoiceInboxData();
  d.n = 5;
  CHECK(!voice_inbox_valid(d));
  d = VoiceInboxData();
  d.n = 1;
  CHECK(!voice_inbox_valid(d));  // slot 0 has id 0
  d.msg[0].id = 1;
  set_text(d.msg[0].text, d.msg[0].len, "ok");
  CHECK(voice_inbox_valid(d));
  d.msg[0].from = 2;
  CHECK(!voice_inbox_valid(d));
  d.msg[0].from = 1;
  d.msg[0].len = 3;  // len says 3, text is 2
  CHECK(!voice_inbox_valid(d));
  d.msg[0].len = 2;
  d.seen_next = 8;
  CHECK(!voice_inbox_valid(d));
}

VTEST(voice_shared_enums) {
  CHECK((int)Sound::Ding == 5 && (int)Sound::Count == 6 && kResidentSounds == 5);
  CHECK(kStateVoice == 0x10);
  CHECK((kStateVoice & (kStatePleaseReply | kStateClockValid | kStateAwayDesk | kStateNobodyNear)) == 0);
  CHECK(kVoiceRecMs == 3000 && kVoiceHoldMs == 600 && kVoiceEarlyStopMs == 800 && kVoiceEarlyStop);
  CHECK(kVoiceEditButton && kVoicePushOpenBusyOnly && kVoicePhoneTokenHours == 12 && !kVoiceDismissClearsPhone);
  CHECK(kVoiceWindow == 32);
  // The longest lines fit the C6's 512-byte serial limit (base64 of 237 bytes is 316 characters).
  char line[1024];
  std::string d64(316, 'A');
  int n = snprintf(line, sizeof(line), vline::kEvData, 255u, "down", 127u, -32768, 88u, d64.c_str());
  CHECK(n > 0 && n < 512);
  std::string esc(240, '"');  // 120 characters, every one escaped
  n = snprintf(line, sizeof(line), vline::kEvMsg, 4294967295ul, "downstairs", "upstairs", 4294967295ul, esc.c_str());
  CHECK(n > 0 && n < 512);
  CHECK(!strcmp(vline::kAbortWord[(int)VoiceAbort::NoPi], "nopi"));
  CHECK(!strcmp(vline::kPurpose[(int)VoicePurpose::Message], "message"));
}
