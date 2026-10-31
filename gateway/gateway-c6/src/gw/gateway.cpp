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

int Gateway::in_flight() const { return (p_[0].used ? 1 : 0) + (p_[1].used ? 1 : 0); }

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
  if (k == kOutside) {
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

void Gateway::on_frame(int from, const uint8_t *data, size_t len) {
  // The same check the panels make: a known MAC, and the sender byte that goes with it.
  if (from != kUpstairs && from != kDownstairs) return;
  if (len < 3 || data[2] != (uint8_t)from) return;
  Msg m;
  if (!tc::decode(data, len, m) || m.type != MsgType::Ack) return;
  Kind k;
  if (m.u.ack.type == (uint8_t)MsgType::Outside) k = kOutside;
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
  for (int k = 0; k < 2; k++) {
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
