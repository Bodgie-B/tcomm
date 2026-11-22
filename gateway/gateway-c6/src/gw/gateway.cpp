#include "gateway.h"
#include "core/voice_proto.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

namespace gw {

using tc::Msg;
using tc::MsgType;
using tc::RetryQueue;

const char *panel_name(int p) { return p == kDownstairs ? "downstairs" : "upstairs"; }

Gateway::Gateway(GatewayIO &io, const char *mac, uint16_t first_seq) : io_(io), seq_(first_seq ? first_seq : 1) {
  snprintf(mac_, sizeof(mac_), "%s", mac);
  vs_[1].via = kViaUpstairs;  // downstairs is reached through upstairs until it has been heard directly
}

uint16_t Gateway::next_seq() {
  uint16_t s = seq_;
  seq_ = (uint16_t)(seq_ + 1);
  if (!seq_) seq_ = 1;  // never 0: a panel's own ACK header carries seq 0
  return s;
}

int Gateway::in_flight() const {
  int n = 0;
  for (int k = 0; k < kKinds; k++) n += p_[k].used ? 1 : 0;
  return n;
}

void Gateway::hello(bool boot) {
  // From 0.5 the hello also says the C6 takes voice frames, how many radio frames its receive queue
  // dropped, and whether downstairs is an ESP-NOW peer (the reach test's down_peer, check finding 1).
  char line[200];
  snprintf(line, sizeof(line),
           "{\"boot\":%s,\"mac\":\"%s\",\"channel\":%d,\"fw\":\"%s\",\"voice\":1,\"drops\":%lu,\"down_peer\":%s}",
           boot ? "true" : "false", mac_, kChannel, kFirmware, (unsigned long)io_.rx_drops(),
           io_.down_peer() ? "true" : "false");
  io_.reply(line);
}

void Gateway::boot() { hello(true); }

void Gateway::finish(Kind k, const char *line) {
  p_[k].used = false;
  io_.reply(line);
}

void Gateway::send_try(Pending &p) {
  io_.send_frame(p.to, p.frame, p.len);
  p.last = io_.now_ms();
  if (p.tries == 0) p.first = p.last;
  p.tries++;
}

namespace {

// The checks on a request's fields. Each returns the reason it fails, or null.
const char *whole(const JField *f, int64_t lo, int64_t hi, const char *why, int64_t &v) {
  if (!f || f->type != JType::Int || f->i < lo || f->i > hi) return why;
  v = f->i;
  return nullptr;
}

}  // namespace

void Gateway::on_line(const char *line, size_t len) {
  st_.pi_seen = true;
  static JObject o;  // about 1 KB: kept off the loop task's stack
  if (len > kMaxLine || !json_parse_object(line, len, o)) {
    io_.reply("{\"error\":\"bad_json\"}");
    return;
  }
  const JField *cmd = o.get("cmd"), *type = o.get("type");
  if (cmd && cmd->type == JType::String && !strcmp(cmd->s, "hello")) {
    const JField *v = o.get("voice");  // the bridge's hello says it can take voice events
    pi_voice_ = v && v->type == JType::Int && v->i == 1;
    hello(false);
    return;
  }
  Kind k;
  if (type && type->type == JType::String && !strcmp(type->s, "outside")) k = kOutside;
  else if (type && type->type == JType::String && !strcmp(type->s, "lucy")) k = kLucy;
  else if (type && type->type == JType::String && !strcmp(type->s, "update")) k = kUpdate;
  else if (type && type->type == JType::String && !strcmp(type->s, "answer")) k = kAnswerUp;  // floor below
  else if (type && type->type == JType::String && !strcmp(type->s, "mail_ack")) k = kMailAck;
  else if (type && type->type == JType::String && !strcmp(type->s, tc::vline::kReqText)) k = kVoiceTextUp;  // floor below
  else if (type && type->type == JType::String && !strcmp(type->s, tc::vline::kReqAck)) k = kVoiceAckUp;     // floor below
  else {
    io_.reply("{\"error\":\"unknown_type\"}");
    return;
  }
  char out[160];
  const char *why = nullptr;
  int64_t id = 0;
  if ((why = whole(o.get("id"), 1, INT64_C(9007199254740991), "id must be a positive whole number", id))) {
    st_.rejected++;
    snprintf(out, sizeof(out), "{\"type\":\"%s\",\"rejected\":\"%s\"}", kind_name(k), why);
    io_.reply(out);
    return;
  }

  if (is_voice_kind(k)) {
    voice_request(k, o, id);
    return;
  }

  Msg m;
  if (k == kUpdate) {
    // Only the id matters. One update at a time: a repeat while one is in flight is answered
    // without a second send (and is not "latest wins": the one in flight keeps its reply).
    if (p_[kUpdate].used) {
      snprintf(out, sizeof(out), "{\"type\":\"update\",\"id\":%" PRId64 ",\"busy\":true,\"seq\":%u}", id,
               (unsigned)p_[kUpdate].seq);
      io_.reply(out);
      return;
    }
    m.type = MsgType::Update;  // its one reserved byte is always 0 (the encoder writes it)
  } else if (k == kMailAck) {
    m.type = MsgType::MailAck;  // its one reserved byte is always 0 (the encoder writes it)
  } else if (k == kAnswerUp) {
    // A phone's answer to a ring (Mike, 2026-11-15): floor is the RUNG floor, knock the ring's id.
    // Each floor has its own slot, so latest wins only among answers for the same floor.
    int64_t kn = 0, co = 0;
    const JField *fl = o.get("floor");
    int floor = -1;
    if (fl && fl->type == JType::String) {
      if (!strcmp(fl->s, "upstairs")) floor = 0;
      else if (!strcmp(fl->s, "downstairs")) floor = 1;
    }
    if (floor < 0) why = "floor must be upstairs or downstairs";
    if (!why) why = whole(o.get("knock"), 1, 65535, "knock must be 1 to 65535", kn);
    if (!why) why = whole(o.get("come_on"), 0, 1, "come_on must be 0 or 1", co);
    if (!why) {
      m.type = MsgType::PhoneAnswer;
      m.u.phone.floor = (uint8_t)floor;
      m.u.phone.id = (uint16_t)kn;
      m.u.phone.come_on = (uint8_t)co;
      if (floor == 1) k = kAnswerDown;
    }
  } else if (k == kOutside) {
    int64_t t = 0, h = 0, fc = 0, fl = 0, tm = 0;
    const JField *ph = o.get("phrase");
    if (!why) why = whole(o.get("temp_tenths_f"), -32768, 32767, "temp_tenths_f must fit an int16", t);
    if (!why) why = whole(o.get("humidity"), 0, 100, "humidity must be 0 to 100", h);
    if (!why && (whole(o.get("forecast"), 0, 9, "x", fc) || fc == 6)) why = "forecast must be 0 to 9, not 6";
    if (!why) why = whole(o.get("flags"), 0, 7, "flags must be 0 to 7", fl);
    if (!why) why = whole(o.get("time"), 0, UINT32_MAX, "time must fit a u32", tm);
    if (!why && (!ph || ph->type != JType::String)) why = "phrase must be a string";
    if (!why) {
      m.type = MsgType::Outside;
      m.u.outside.flags = (uint8_t)fl;
      m.u.outside.temp_tenths_f = (int16_t)t;
      m.u.outside.humidity = (uint8_t)h;
      m.u.outside.forecast = (uint8_t)fc;
      m.u.outside.time = (uint32_t)tm;
      // Cut to 48 characters; the parser has already made everything printable ASCII.
      size_t n = strlen(ph->s);
      if (n > tc::kMaxPhrase) n = tc::kMaxPhrase;
      memcpy(m.u.outside.phrase, ph->s, n);
      m.u.outside.phrase[n] = 0;
      m.u.outside.phrase_len = (uint8_t)n;
    }
  } else {
    int64_t out_v = 0, since = 0;
    if (!why) why = whole(o.get("out"), 0, 1, "out must be 0 or 1", out_v);
    if (!why) why = whole(o.get("since"), 0, UINT32_MAX, "since must fit a u32", since);
    if (!why) {
      m.type = MsgType::Lucy;
      m.u.lucy.out = (uint8_t)out_v;
      m.u.lucy.since = (uint32_t)since;
    }
  }
  if (why) {
    st_.rejected++;
    snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"rejected\":\"%s\"}", kind_name(k), id, why);
    snprintf(st_.last, sizeof(st_.last), "%s rejected", screen_name(k));
    io_.reply(out);
    return;
  }

  // Within a type (for an answer, within its floor) only the latest matters: a request still waiting
  // for its ACK gives way.
  Pending &p = p_[k];
  if (p.used) {
    snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"superseded\":true,\"seq\":%u}", kind_name(k), p.id,
             (unsigned)p.seq);
    finish(k, out);
  }
  m.sender = tc::kSenderGateway;
  m.seq = next_seq();
  size_t n = tc::encode(m, p.frame, sizeof(p.frame));
  if (!n) {  // can't happen with the checks above; a backstop all the same
    st_.rejected++;
    snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"rejected\":\"could not encode\"}", kind_name(k), id);
    io_.reply(out);
    return;
  }
  p.used = true;
  p.id = id;
  p.seq = m.seq;
  p.len = n;
  p.tries = 0;
  st_.requests++;
  send_try(p);
}

namespace {

const char *notice_kind_name(uint8_t k) {
  switch ((tc::NoticeKind)k) {
    case tc::NoticeKind::Knock: return "knock";
    case tc::NoticeKind::ArrowUrgent: return "arrow_urgent";
    case tc::NoticeKind::ArrowMild: return "arrow_mild";
    case tc::NoticeKind::Mail: return "mail";
    case tc::NoticeKind::DogOut: return "dog_out";
    case tc::NoticeKind::Status: return "status";
    case tc::NoticeKind::Note: return "note";
    case tc::NoticeKind::Ring: return "ring";
  }
  return "unknown";  // can't happen: decode only takes kinds 1 to 8
}

}  // namespace

// A house event from the upstairs panel. ACKed every time (our first ACK may be what was lost),
// printed once. The ACK goes the way every gateway frame goes, to the upstairs panel, so a NOTICE
// from any other sender is ignored.
void Gateway::on_notice(const Msg &m) {
  Msg a;
  a.type = MsgType::Ack;
  a.sender = tc::kSenderGateway;  // seq stays 0, as in a panel's own ACK
  a.u.ack.seq = m.seq;
  a.u.ack.type = (uint8_t)MsgType::Notice;
  uint8_t buf[16];
  size_t n = tc::encode(a, buf, sizeof(buf));
  if (n) io_.send_frame(kUpstairs, buf, n);
  if (notices_.seen(m.sender, m.seq, io_.now_ms())) return;
  // The text is printable ASCII already (decode); only the quote and the backslash need escaping.
  char text[2 * tc::kNoticeTextMax + 1];
  size_t t = 0;
  for (uint8_t i = 0; i < m.u.notice.text_len; i++) {
    char c = m.u.notice.text[i];
    if (c == '"' || c == '\\') text[t++] = '\\';
    text[t++] = c;
  }
  text[t] = 0;
  // A ring's value is its knock id, a u16 carried in the i16: printed unsigned (1 to 65535) so the Pi
  // gets the id itself. Every other kind keeps its signed value.
  const int value = m.u.notice.kind == (uint8_t)tc::NoticeKind::Ring ? (int)(uint16_t)m.u.notice.value : (int)m.u.notice.value;
  char out[256];
  snprintf(out, sizeof(out), "{\"event\":\"notice\",\"kind\":\"%s\",\"floor\":\"%s\",\"value\":%d,\"time\":%lu,\"text\":\"%s\"}",
           notice_kind_name(m.u.notice.kind), panel_name(m.u.notice.floor), value,
           (unsigned long)m.u.notice.time, text);
  io_.reply(out);
}

namespace {

// Tenths as a JSON number with one decimal, or null (rooms line, 0.7). Integer math, so no float
// printf on the C6: -15 is "-1.5" and -5 is "-0.5".
void put_tenths(char *out, size_t cap, bool valid, int32_t tenths) {
  if (!valid) {
    snprintf(out, cap, "null");
    return;
  }
  const bool neg = tenths < 0;
  const uint32_t a = (uint32_t)(neg ? -tenths : tenths);
  snprintf(out, cap, "%s%lu.%lu", neg ? "-" : "", (unsigned long)(a / 10), (unsigned long)(a % 10));
}

}  // namespace

// Both floors' indoor readings and upstairs occupancy, from the upstairs panel (Mike, 2026-11-22),
// for the Pi's "TComm Rooms" HomeKit bridge. ACKed every time like a NOTICE (ack.type ROOMS, ack.seq
// the frame's seq), printed once per seq. on_frame takes it from the upstairs panel only.
void Gateway::on_rooms(const Msg &m) {
  Msg a;
  a.type = MsgType::Ack;
  a.sender = tc::kSenderGateway;
  a.u.ack.seq = m.seq;
  a.u.ack.type = (uint8_t)MsgType::Rooms;
  uint8_t buf[16];
  size_t n = tc::encode(a, buf, sizeof(buf));
  if (n) io_.send_frame(kUpstairs, buf, n);
  if (rooms_.seen(m.sender, m.seq, io_.now_ms())) return;
  const uint8_t f = m.u.rooms.flags;
  const bool up = f & tc::kRoomsUpValid, down = f & tc::kRoomsDownValid;
  char ut[12], uh[12], dt[12], dh[12];
  put_tenths(ut, sizeof(ut), up, m.u.rooms.up_t);
  put_tenths(uh, sizeof(uh), up, m.u.rooms.up_h);
  put_tenths(dt, sizeof(dt), down, m.u.rooms.down_t);
  put_tenths(dh, sizeof(dh), down, m.u.rooms.down_h);
  const char *occ = !(f & tc::kRoomsOccKnown) ? "null" : (f & tc::kRoomsOccupied) ? "1" : "0";
  char out[160];
  snprintf(out, sizeof(out), "{\"event\":\"rooms\",\"up_t\":%s,\"up_h\":%s,\"down_t\":%s,\"down_h\":%s,\"occ\":%s,\"time\":%lu}",
           ut, uh, dt, dh, occ, (unsigned long)m.u.rooms.time);
  io_.reply(out);
}

void Gateway::on_frame(int from, const uint8_t *data, size_t len) {
  // The same check the panels make: a known MAC, and the sender byte that goes with it.
  if (from != kUpstairs && from != kDownstairs) return;
  if (len < 3 || data[2] != (uint8_t)from) return;
  if (tc::is_voice_frame(data, len)) {  // types 20 to 30 have their own codec (core/voice_proto.h)
    on_voice_frame(from, data, len);
    return;
  }
  Msg m;
  if (!tc::decode(data, len, m)) return;
  if (m.type == MsgType::Notice) {
    if (from == kUpstairs) on_notice(m);
    return;
  }
  if (m.type == MsgType::Rooms) {
    if (from == kUpstairs) on_rooms(m);
    return;
  }
  if (m.type != MsgType::Ack) return;
  Kind k;
  if (m.u.ack.type == (uint8_t)MsgType::Outside) k = kOutside;
  else if (m.u.ack.type == (uint8_t)MsgType::Update) k = kUpdate;
  else if (m.u.ack.type == (uint8_t)MsgType::Lucy) k = kLucy;
  else if (m.u.ack.type == (uint8_t)MsgType::PhoneAnswer) k = p_[kAnswerDown].used && p_[kAnswerDown].seq == m.u.ack.seq ? kAnswerDown : kAnswerUp;
  else if (m.u.ack.type == (uint8_t)MsgType::MailAck) k = kMailAck;
  else return;
  Pending &p = p_[k];
  if (!p.used || p.seq != m.u.ack.seq) return;  // late, duplicate, or for one that gave way
  char out[160];
  uint32_t ms = io_.now_ms() - p.first;
  snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"acked\":\"%s\",\"seq\":%u,\"ms\":%lu}", kind_name(k), p.id,
           panel_name(from), (unsigned)p.seq, (unsigned long)ms);
  st_.acked++;
  snprintf(st_.last, sizeof(st_.last), "%s acked %lums", screen_name(k), (unsigned long)ms);
  finish(k, out);
}

void Gateway::tick() {
  // TComm's schedule (RetryQueue): tries at 0, 150, 400, 750 and 1150 ms, each timed from the one
  // before it, then give up 350 ms after the last (1.5 s from the first without stalls).
  const uint32_t now = io_.now_ms();
  for (int k = 0; k < kKinds; k++) {
    Pending &p = p_[k];
    if (!p.used) continue;
    if (p.tries < RetryQueue::kTries) {
      uint32_t gap = RetryQueue::kOffsetsMs[p.tries] - RetryQueue::kOffsetsMs[p.tries - 1];
      if (now - p.last >= gap) send_try(p);
    } else if (now - p.last >= RetryQueue::kGiveUpMs - RetryQueue::kOffsetsMs[RetryQueue::kTries - 1]) {
      char out[160];
      snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"failed\":\"%s\",\"seq\":%u}", kind_name((Kind)k), p.id,
               panel_name(p.floor), (unsigned)p.seq);
      st_.failed++;
      snprintf(st_.last, sizeof(st_.last), "%s failed", screen_name((Kind)k));
      finish((Kind)k, out);
    }
  }
  if (!hello_sent_ || now - hello_at_ >= tc::kVoiceHelloEveryMs) voice_hello(now);
  voice_tick(now);
}

// ---------------------------------------------------------------------------------------------------
// Voice notes (firmware/docs/voice-notes/DESIGN.md, sections 4 and 5)
//
// The panel's recording arrives as VOICE_DATA frames and leaves as one "voice" line per chunk (cut
// through, no clip buffer). A chunk counts as received only once its line was written whole to the
// serial port; one that could not be written (no room, or a request waiting for its ACK) is simply
// a gap in the next VOICE_ACK, and the panel resends it.
// ---------------------------------------------------------------------------------------------------

using tc::VoiceMsg;
using tc::VType;

namespace {

// Voice lines are built here, not on the loop task's stack (the C6 runs one thread).
char g_vline[kMaxLine + 1];

const char *floor_short(int f) { return tc::vline::kFloorShort[f ? 1 : 0]; }

}  // namespace

bool Gateway::request_waiting() const {
  for (int k = 0; k < kVoiceTextUp; k++)
    if (p_[k].used) return true;
  return false;
}

// One line to the Pi if the port can take it and still leave `reserve` bytes free.
bool Gateway::write_line(const char *line, int reserve) {
  const int n = (int)strlen(line);
  if (io_.tx_room() < n + 1 + reserve) return false;
  io_.reply(line);
  return true;
}

void Gateway::send_voice_to(int floor, uint8_t via, VoiceMsg &m) {
  if (!vcap_.take(io_.now_ms())) {  // the air cap: the panel's resend logic covers a frame held here
    st_.voice_capped++;
    return;
  }
  uint8_t buf[tc::kMaxMsg];
  m.sender = tc::kSenderGateway;
  m.seq = next_seq();
  m.floor = (uint8_t)floor;
  size_t n = tc::voice_encode(m, buf, sizeof(buf));
  if (n) io_.send_frame(route_to(floor, via), buf, n);
}

// A VOICE_ACK for the floor's session (state 1 open, 2 complete).
void Gateway::send_ack(int floor, uint8_t state) {
  VoiceSession &s = vs_[floor];
  VoiceMsg m;
  m.type = VType::Ack;
  m.u.ack.xfer = s.xfer;
  m.u.ack.base = s.base();
  m.u.ack.bits = s.bits();
  m.u.ack.state = state;
  s.ack_due = false;
  s.dup = false;
  s.new_since_ack = 0;
  send_voice_to(floor, s.via, m);
}

// The voice hello (field fix 2026-11-20): tells upstairs this C6 takes voice frames. Upstairs passes it
// on to downstairs in its STATE (kStateGwVoice), so this C6 never sends downstairs a frame it did not ask
// for, and a panel never sends a C6 0.4 a voice frame. About one frame every 2 minutes. Header seq 0
// (trace only), so the requests' seq numbers are not moved by it. 0.6 (Mike, 2026-11-21): key 1
// (tc::kVoiceHelloKeyMulaw) says this C6 takes mu-law and 255-chunk clips as well, so the panels record
// in mu-law; 0.5 sent key 0 and the panels keep to ADPCM with it (upstairs passes it on as kStateGwMulaw).
void Gateway::voice_hello(uint32_t now) {
  hello_sent_ = true;
  hello_at_ = now;
  if (!vcap_.take(now)) {
    st_.voice_capped++;
    return;
  }
  VoiceMsg m;
  m.type = VType::Got;
  m.sender = tc::kSenderGateway;
  m.seq = 0;
  m.floor = 0;
  m.u.got.kind = (uint8_t)VType::Ping;
  m.u.got.key = tc::kVoiceHelloKeyMulaw;
  m.u.got.result = (uint8_t)tc::VoiceGotResult::Ok;
  uint8_t buf[tc::kVoiceGotLen];
  size_t n = tc::voice_encode(m, buf, sizeof(buf));
  if (!n) return;
  st_.voice_hellos++;
  io_.send_frame(kUpstairs, buf, n);
}

void Gateway::send_got(int floor, uint8_t via, uint8_t kind, uint32_t key, uint8_t result) {
  VoiceMsg m;
  m.type = VType::Got;
  m.u.got.kind = kind;
  m.u.got.key = key;
  m.u.got.result = result;
  send_voice_to(floor, via, m);
}

// A VOICE_ABORT to a panel, sent three times (0, 100 and 250 ms): it has no ACK of its own.
void Gateway::send_abort(int floor, uint8_t via, uint8_t xfer, uint8_t reason) {
  VoiceMsg m;
  m.type = VType::Abort;
  m.u.abort.xfer = xfer;
  m.u.abort.reason = reason;
  m.sender = tc::kSenderGateway;
  m.seq = next_seq();
  m.floor = (uint8_t)floor;
  AbortOut &a = ab_[floor];
  size_t n = tc::voice_encode(m, a.frame, sizeof(a.frame));
  if (!n) return;
  a.used = true;
  a.len = (uint8_t)n;
  a.to = route_to(floor, via);
  a.sent = 1;
  a.first = io_.now_ms();
  io_.send_frame(a.to, a.frame, a.len);
}

bool Gateway::print_begin(int floor) {
  VoiceSession &s = vs_[floor];
  if (s.begin_out) return true;
  const uint8_t pu = s.purpose <= 2 ? s.purpose : 0;
  snprintf(g_vline, sizeof(g_vline), tc::vline::kEvBegin, (unsigned)s.xfer, floor_short(floor), tc::vline::kPurpose[pu],
           (unsigned)s.codec, (unsigned)s.chunk_samples, tc::vline::kVia[s.via ? 1 : 0]);
  if (!write_line(g_vline, kVoiceCtlReserve)) return false;
  s.begin_out = true;
  return true;
}

bool Gateway::print_abort(int floor, uint8_t reason) {
  VoiceSession &s = vs_[floor];
  snprintf(g_vline, sizeof(g_vline), tc::vline::kEvAbort, (unsigned)s.xfer, floor_short(floor),
           tc::vline::kAbortWord[reason <= 7 ? reason : 0]);
  return write_line(g_vline, kVoiceCtlReserve);
}

// When END was seen and every chunk is in: voice_end to the Pi, then VOICE_ACK state 2. False while
// something is missing or the line could not be written (tick and the panel's resent END try again).
bool Gateway::try_finish(int floor) {
  VoiceSession &s = vs_[floor];
  if (s.state != VoiceSession::Open || s.pi_abort || !s.all_received()) return false;
  if (!print_begin(floor)) return false;
  snprintf(g_vline, sizeof(g_vline), tc::vline::kEvEnd, (unsigned)s.xfer, floor_short(floor), (unsigned)s.n_chunks,
           (unsigned)s.n_samples, (unsigned)s.speech);
  if (!write_line(g_vline, kVoiceCtlReserve)) return false;
  s.state = VoiceSession::Done;
  send_ack(floor, (uint8_t)tc::VoiceAckState::Complete);
  return true;
}

void Gateway::voice_begin_frame(int floor, uint8_t via, const VoiceMsg &m, uint32_t now) {
  const auto &b = m.u.begin;
  VoiceSession &s = vs_[floor];
  if (!pi_voice_) {
    // No Pi with voice since the bridge connected: say so (once per burst of resent BEGINs).
    const AbortOut &a = ab_[floor];
    const bool same = a.used && a.len > 6 && a.frame[6] == b.xfer;
    if (!same) send_abort(floor, via, b.xfer, (uint8_t)tc::VoiceAbort::NoPi);
    return;
  }
  if (s.state != VoiceSession::Idle && s.xfer == b.xfer) {  // a resent BEGIN: it never starts over
    s.via = via;
    s.last_rx = now;
    if (s.state == VoiceSession::Done) {
      send_ack(floor, (uint8_t)tc::VoiceAckState::Complete);
    } else {
      print_begin(floor);
      send_ack(floor, (uint8_t)tc::VoiceAckState::Open);
    }
    return;
  }
  // Another xfer replaces the floor's session; the Pi is told the old one is over.
  if (s.state == VoiceSession::Open && s.begin_out) print_abort(floor, (uint8_t)tc::VoiceAbort::Lost);
  s.open(b.xfer, b.purpose, b.codec, b.chunk_samples, b.max_chunks, via, now);
  print_begin(floor);  // if the port has no room now, tick and the first chunk try again
  send_ack(floor, (uint8_t)tc::VoiceAckState::Open);
}

void Gateway::voice_data_frame(int floor, uint8_t via, const VoiceMsg &m, uint32_t now) {
  const auto &d = m.u.data;
  VoiceSession &s = vs_[floor];
  if (s.state != VoiceSession::Open || s.xfer != d.xfer || s.pi_abort) return;  // no session: dropped
  if (d.idx >= s.max_chunks || (s.codec == (uint8_t)tc::VoiceCodec::Mulaw && (d.pred != 0 || d.step != 0))) {
    st_.voice_bad++;
    return;
  }
  s.via = via;
  s.last_rx = now;
  const bool fills_gap = d.idx < s.max_seen;  // a higher chunk came first: this one closes (or narrows) a gap
  if (d.idx > s.max_seen) s.max_seen = d.idx;
  // A gap or a duplicate still there 80 ms after the last DATA gets a VOICE_ACK (see voice_tick), but
  // never later than kVoiceAckMaxMs after the first DATA since the last ACK, so a trickle of resends
  // cannot hold it back for ever (0.6, field fix 2026-11-21: mu-law's 32-chunk window is only 0.47 s).
  if (!s.ack_due) s.ack_first = now;
  s.ack_at = now + 80;
  if ((int32_t)(s.ack_at - (s.ack_first + kVoiceAckMaxMs)) > 0) s.ack_at = s.ack_first + kVoiceAckMaxMs;
  s.ack_due = true;
  if (s.has(d.idx)) {
    s.dup = true;
    return;
  }
  if (!s.begin_out && !print_begin(floor)) {
    st_.voice_held++;
    return;
  }
  // The write gate. Nothing goes out while a request waits for its ACK (its reply must not queue
  // behind voice lines), and a chunk needs room for itself plus the control-line reserve.
  char b64[4 * ((tc::kVoiceChunkBytes + 2) / 3) + 1];
  voice_base64(d.data, d.len, b64);
  const int n = snprintf(g_vline, sizeof(g_vline), tc::vline::kEvData, (unsigned)s.xfer, floor_short(floor), (unsigned)d.idx,
                         (int)d.pred, (unsigned)d.step, b64);
  if (n <= 0 || n >= (int)sizeof(g_vline) || request_waiting() || io_.tx_room() < n + 1 + kVoiceDataReserve) {
    st_.voice_held++;
    return;
  }
  io_.reply(g_vline);
  s.mark(d.idx);
  st_.voice_lines++;
  if (fills_gap) s.dup = true;  // like a duplicate: the panel hears at once that base moved (0.6)
  if (++s.new_since_ack >= 8) send_ack(floor, (uint8_t)tc::VoiceAckState::Open);
  if (s.end_seen) try_finish(floor);
}

void Gateway::voice_end_frame(int floor, const VoiceMsg &m, uint32_t now) {
  const auto &e = m.u.end;
  VoiceSession &s = vs_[floor];
  if (s.state == VoiceSession::Idle || s.xfer != e.xfer) {  // never heard of it (or aborted): state 3
    VoiceMsg a;
    a.type = VType::Ack;
    a.u.ack.xfer = e.xfer;
    a.u.ack.state = (uint8_t)tc::VoiceAckState::Unknown;
    send_voice_to(floor, s.via, a);
    return;
  }
  s.last_rx = now;
  if (s.state == VoiceSession::Done) {  // our state 2 was lost: say it again, print nothing
    send_ack(floor, (uint8_t)tc::VoiceAckState::Complete);
    return;
  }
  if (e.n_chunks > s.max_chunks) {
    st_.voice_bad++;
    return;
  }
  if (!s.end_seen) {
    s.end_seen = true;
    s.n_chunks = e.n_chunks;
    s.n_samples = e.n_samples;
    s.speech = e.speech;
  }
  if (!try_finish(floor)) send_ack(floor, (uint8_t)tc::VoiceAckState::Open);
}

void Gateway::voice_abort_frame(int floor, const VoiceMsg &m) {
  const auto &a = m.u.abort;
  VoiceSession &s = vs_[floor];
  if (s.state == VoiceSession::Idle || s.xfer != a.xfer) return;  // the second and third copies land here
  if (s.state == VoiceSession::Open) {
    if (!s.begin_out) {  // the Pi never heard of it
      s.clear();
      return;
    }
    s.pi_abort = a.reason;
    if (print_abort(floor, a.reason)) s.clear();  // else voice_tick tries again
    return;
  }
  // Done: the Pi may be recognising it, so it is told, once (the recording was already sent whole).
  if (!s.abort_out && print_abort(floor, a.reason)) s.abort_out = true;
}

void Gateway::voice_got_frame(int floor, const VoiceMsg &m) {
  const auto &g = m.u.got;
  Kind k;
  if (g.kind == (uint8_t)VType::Text) k = floor ? kVoiceTextDown : kVoiceTextUp;
  else if (g.kind == (uint8_t)VType::Dismiss) k = floor ? kVoiceAckDown : kVoiceAckUp;
  else return;  // GOTs for SAY, PUSH and PING are not ours to match
  Pending &p = p_[k];
  if (!p.used || p.got_kind != g.kind || p.key != g.key) return;  // late, duplicate, or for one that gave way
  char out[176];
  const uint32_t ms = io_.now_ms() - p.first;
  if (k == kVoiceAckUp || k == kVoiceAckDown) {
    snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"acked\":\"%s\",\"result\":\"%s\",\"seq\":%u,\"ms\":%lu}",
             kind_name(k), p.id, panel_name(p.floor),
             g.result == (uint8_t)tc::VoiceGotResult::Ok ? tc::vline::kResultRemoved : tc::vline::kResultGone,
             (unsigned)p.seq, (unsigned long)ms);
  } else {
    snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"acked\":\"%s\",\"seq\":%u,\"ms\":%lu}", kind_name(k),
             p.id, panel_name(p.floor), (unsigned)p.seq, (unsigned long)ms);
  }
  st_.acked++;
  snprintf(st_.last, sizeof(st_.last), "%s acked %lums", screen_name(k), (unsigned long)ms);
  finish(k, out);
}

// A message for the other floor's phones (the receiving panel decides whether to send it): printed
// once as a voice_msg line, confirmed with VOICE_GOT by the route it came in.
void Gateway::voice_push_frame(int floor, uint8_t via, const VoiceMsg &m) {
  const auto &p = m.u.push;
  if (!pi_voice_) {
    send_got(floor, via, (uint8_t)VType::Push, p.id, (uint8_t)tc::VoiceGotResult::Refused);
    return;
  }
  for (int i = 0; i < tc::kVoiceSeenIds; i++) {
    if (seen_push_[i] == p.id) {  // our GOT was lost: confirm again, print nothing
      send_got(floor, via, (uint8_t)VType::Push, p.id, (uint8_t)tc::VoiceGotResult::Ok);
      return;
    }
  }
  char text[2 * tc::kVoiceTextMax + 1];
  size_t t = 0;
  for (uint8_t i = 0; i < p.len; i++) {  // 0x20 to 0x7E already (voice_decode): only " and \ need escaping
    if (p.text[i] == '"' || p.text[i] == '\\') text[t++] = '\\';
    text[t++] = p.text[i];
  }
  text[t] = 0;
  snprintf(g_vline, sizeof(g_vline), tc::vline::kEvMsg, (unsigned long)p.id, panel_name(floor),
           tc::vline::kFloorLong[p.from ? 1 : 0], (unsigned long)p.time, text);
  if (!write_line(g_vline, kVoiceCtlReserve)) return;  // no room: no GOT, the panel sends it again
  seen_push_[seen_next_] = p.id;
  seen_next_ = (uint8_t)((seen_next_ + 1) % tc::kVoiceSeenIds);
  send_got(floor, via, (uint8_t)VType::Push, p.id, (uint8_t)tc::VoiceGotResult::Ok);
}

void Gateway::on_voice_frame(int from, const uint8_t *data, size_t len) {
  VoiceMsg m;
  if (!tc::voice_decode(data, len, m)) {
    st_.voice_bad++;
    return;
  }
  const int floor = m.floor;
  uint8_t via;
  if (from == kDownstairs) {
    if (floor != 1) {  // the downstairs panel only speaks for its own floor
      st_.voice_bad++;
      return;
    }
    via = kViaDirect;
  } else {  // upstairs: its own frames (floor 0), or a relayed downstairs frame (floor 1)
    via = floor == 1 ? kViaUpstairs : kViaDirect;
  }
  const uint32_t now = io_.now_ms();
  switch (m.type) {
    case VType::Ping:
      if (from != kDownstairs) return;  // direct only: a ping is how downstairs tests that link
      down_via_ = kViaDirect;
      if (ping_answered_ && now - ping_answer_at_ < kVoicePingAnswerGapMs) return;  // a panel pinging too fast
      ping_answered_ = true;
      ping_answer_at_ = now;
      send_got(1, kViaDirect, (uint8_t)VType::Ping, m.u.ping.nonce, (uint8_t)tc::VoiceGotResult::Ok);
      return;
    case VType::Begin: case VType::Data: case VType::End: case VType::Abort: case VType::Got: case VType::Push:
      break;
    default:
      return;  // ACK, TEXT, SAY, DISMISS are not for the C6
  }
  if (floor == 1) down_via_ = via;
  switch (m.type) {
    case VType::Begin: voice_begin_frame(floor, via, m, now); break;
    case VType::Data: voice_data_frame(floor, via, m, now); break;
    case VType::End: voice_end_frame(floor, m, now); break;
    case VType::Abort: voice_abort_frame(floor, m); break;
    case VType::Got: voice_got_frame(floor, m); break;
    case VType::Push: voice_push_frame(floor, via, m); break;
    default: break;
  }
}

// The Pi's voice_text and voice_ack requests (DESIGN 5.2), each answered once like the others.
void Gateway::voice_request(Kind k, const JObject &o, int64_t id) {
  const bool text = k == kVoiceTextUp;
  char out[176];
  const char *why = nullptr;
  int floor = -1;
  const JField *fl = o.get("floor");
  if (fl && fl->type == JType::String) {
    if (!strcmp(fl->s, "upstairs")) floor = 0;
    else if (!strcmp(fl->s, "downstairs")) floor = 1;
  }
  if (floor < 0) why = "floor must be upstairs or downstairs";
  VoiceMsg m;
  uint32_t key = 0;
  if (text) {
    int64_t x = 0, status = 0, flags = 0;
    char joined[tc::kVoiceTextMax + 1] = "";
    if (!why) why = whole(o.get("x"), 1, 255, "x must be 1 to 255", x);
    if (!why) why = whole(o.get("status"), 0, tc::kVoiceStatusMax, "status must be 0 to 6", status);
    if (!why && o.get("fl")) why = whole(o.get("fl"), 0, tc::kVoiceTextFlagsKnown, "fl must be 0 or 1", flags);
    if (!why) {
      const JField *t0 = o.get("t0"), *t1 = o.get("t1");
      if ((t0 && t0->type != JType::String) || (t1 && t1->type != JType::String)) why = "t0 and t1 must be strings";
      else if ((t0 && strlen(t0->s) > tc::vline::kTextPart) || (t1 && strlen(t1->s) > tc::vline::kTextPart))
        why = "t0 and t1 must be 60 characters or fewer";
      else {
        snprintf(joined, sizeof(joined), "%s%s", t0 ? t0->s : "", t1 ? t1->s : "");
        if (status == 0 && !joined[0]) why = "status 0 needs text";
        else if (status != 0 && joined[0]) why = "text must be empty unless status is 0";
      }
    }
    if (!why) {
      m.type = VType::Text;
      m.u.text.xfer = (uint8_t)x;
      m.u.text.status = (uint8_t)status;
      m.u.text.flags = (uint8_t)flags;
      m.u.text.len = (uint8_t)strlen(joined);
      memcpy(m.u.text.text, joined, m.u.text.len);
      key = tc::voice_text_key((uint8_t)x, (uint8_t)status);
    }
  } else {
    int64_t msg = 0;
    if (!why) why = whole(o.get("msg"), 1, UINT32_MAX, "msg must fit a u32 and not be 0", msg);
    if (!why) {
      m.type = VType::Dismiss;
      m.u.dismiss.id = (uint32_t)msg;
      key = (uint32_t)msg;
    }
  }
  if (why) {
    st_.rejected++;
    snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"rejected\":\"%s\"}", kind_name(k), id, why);
    snprintf(st_.last, sizeof(st_.last), "%s rejected", screen_name(k));
    io_.reply(out);
    return;
  }
  const Kind slot = text ? (floor ? kVoiceTextDown : kVoiceTextUp) : (floor ? kVoiceAckDown : kVoiceAckUp);
  Pending &p = p_[slot];
  if (p.used) {
    if (!text) {  // two Got its for one floor inside 1.5 s: the first keeps its slot, the bridge asks again
      snprintf(out, sizeof(out), "{\"type\":\"voice_ack\",\"id\":%" PRId64 ",\"rejected\":\"busy\"}", id);
      io_.reply(out);
      return;
    }
    snprintf(out, sizeof(out), "{\"type\":\"voice_text\",\"id\":%" PRId64 ",\"superseded\":true,\"seq\":%u}", p.id,
             (unsigned)p.seq);
    finish(slot, out);  // an interim "queued" gives way to the final text
  }
  // The route: the way the floor's session came (text), or the last frame heard from downstairs
  // (a dismiss the C6 starts itself). Upstairs is always direct.
  const uint8_t via = floor == 0 ? (uint8_t)kViaDirect : text ? vs_[1].via : down_via_;
  m.sender = tc::kSenderGateway;
  m.seq = next_seq();
  m.floor = (uint8_t)floor;
  size_t n = tc::voice_encode(m, p.frame, sizeof(p.frame));
  if (!n) {  // can't happen with the checks above; a backstop all the same
    st_.rejected++;
    snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"rejected\":\"could not encode\"}", kind_name(k), id);
    io_.reply(out);
    return;
  }
  p.used = true;
  p.id = id;
  p.seq = m.seq;
  p.len = n;
  p.tries = 0;
  p.to = route_to(floor, via);
  p.floor = (uint8_t)floor;
  p.got_kind = text ? (uint8_t)VType::Text : (uint8_t)VType::Dismiss;
  p.key = key;
  st_.requests++;
  send_try(p);
}

// Timers: the abort frames' second and third copies, the sessions' idle timeout and owed lines, and
// the VOICE_ACK that follows a gap.
void Gateway::voice_tick(uint32_t now) {
  for (int f = 0; f < 2; f++) {
    AbortOut &a = ab_[f];
    if (a.used) {
      const uint32_t age = now - a.first;
      if ((a.sent == 1 && age >= 100) || (a.sent == 2 && age >= 250)) {
        io_.send_frame(a.to, a.frame, a.len);
        if (++a.sent >= 3) a.used = false;
      }
    }
    VoiceSession &s = vs_[f];
    if (s.state != VoiceSession::Open) continue;
    if (s.pi_abort) {  // a voice_abort line is owed to the Pi
      if (print_abort(f, s.pi_abort)) s.clear();
      continue;
    }
    if (now - s.last_rx >= kVoiceIdleMs) {  // nothing for 2 s: both ends are told it is over
      const uint8_t x = s.xfer, via = s.via;
      if (!s.begin_out) {
        s.clear();
      } else {
        s.pi_abort = (uint8_t)tc::VoiceAbort::Timeout;
        if (print_abort(f, s.pi_abort)) s.clear();
      }
      send_abort(f, via, x, (uint8_t)tc::VoiceAbort::Timeout);
      continue;
    }
    if (!s.begin_out) print_begin(f);
    if (s.end_seen && try_finish(f)) continue;
    if (s.ack_due && (int32_t)(now - s.ack_at) >= 0) {
      if (s.gap() || s.dup) send_ack(f, (uint8_t)tc::VoiceAckState::Open);
      else s.ack_due = false;  // nothing to report: the next DATA starts a new 80 ms
    }
  }
}

}  // namespace gw
