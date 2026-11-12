#include "gateway.h"
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
}

uint16_t Gateway::next_seq() {
  uint16_t s = seq_;
  seq_ = (uint16_t)(seq_ + 1);
  if (!seq_) seq_ = 1;  // never 0: a panel's own ACK header carries seq 0
  return s;
}

int Gateway::in_flight() const { return (p_[0].used ? 1 : 0) + (p_[1].used ? 1 : 0) + (p_[2].used ? 1 : 0); }

void Gateway::hello(bool boot) {
  char line[128];
  snprintf(line, sizeof(line), "{\"boot\":%s,\"mac\":\"%s\",\"channel\":%d,\"fw\":\"%s\"}", boot ? "true" : "false", mac_,
           kChannel, kFirmware);
  io_.reply(line);
}

void Gateway::boot() { hello(true); }

void Gateway::finish(Kind k, const char *line) {
  p_[k].used = false;
  io_.reply(line);
}

void Gateway::send_try(Pending &p) {
  io_.send_frame(p.frame, p.len);
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
    hello(false);
    return;
  }
  Kind k;
  if (type && type->type == JType::String && !strcmp(type->s, "outside")) k = kOutside;
  else if (type && type->type == JType::String && !strcmp(type->s, "lucy")) k = kLucy;
  else if (type && type->type == JType::String && !strcmp(type->s, "update")) k = kUpdate;
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
    snprintf(st_.last, sizeof(st_.last), "%s rejected", kind_name(k));
    io_.reply(out);
    return;
  }

  // Within a type only the latest matters: a request still waiting for its ACK gives way.
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
  }
  return "unknown";  // can't happen: decode only takes kinds 1 to 7
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
  if (n) io_.send_frame(buf, n);
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
  char out[256];
  snprintf(out, sizeof(out), "{\"event\":\"notice\",\"kind\":\"%s\",\"floor\":\"%s\",\"value\":%d,\"time\":%lu,\"text\":\"%s\"}",
           notice_kind_name(m.u.notice.kind), panel_name(m.u.notice.floor), (int)m.u.notice.value,
           (unsigned long)m.u.notice.time, text);
  io_.reply(out);
}

void Gateway::on_frame(int from, const uint8_t *data, size_t len) {
  // The same check the panels make: a known MAC, and the sender byte that goes with it.
  if (from != kUpstairs && from != kDownstairs) return;
  if (len < 3 || data[2] != (uint8_t)from) return;
  Msg m;
  if (!tc::decode(data, len, m)) return;
  if (m.type == MsgType::Notice) {
    if (from == kUpstairs) on_notice(m);
    return;
  }
  if (m.type != MsgType::Ack) return;
  Kind k;
  if (m.u.ack.type == (uint8_t)MsgType::Outside) k = kOutside;
  else if (m.u.ack.type == (uint8_t)MsgType::Update) k = kUpdate;
  else if (m.u.ack.type == (uint8_t)MsgType::Lucy) k = kLucy;
  else return;
  Pending &p = p_[k];
  if (!p.used || p.seq != m.u.ack.seq) return;  // late, duplicate, or for one that gave way
  char out[160];
  uint32_t ms = io_.now_ms() - p.first;
  snprintf(out, sizeof(out), "{\"type\":\"%s\",\"id\":%" PRId64 ",\"acked\":\"%s\",\"seq\":%u,\"ms\":%lu}", kind_name(k), p.id,
           panel_name(from), (unsigned)p.seq, (unsigned long)ms);
  st_.acked++;
  snprintf(st_.last, sizeof(st_.last), "%s acked %lums", kind_name(k), (unsigned long)ms);
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
               panel_name(kUpstairs), (unsigned)p.seq);
      st_.failed++;
      snprintf(st_.last, sizeof(st_.last), "%s failed", kind_name((Kind)k));
      finish((Kind)k, out);
    }
  }
}

}  // namespace gw
