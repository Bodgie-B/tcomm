// Host tests for the ESP32-C6 gateway's logic (gateway/gateway-c6/src/gw): the brief's frames
// byte for byte, ACK matching, TComm's retry schedule, supersede, the two types apart, and one
// final reply per request id. Built and run by firmware/tools/build_sim.sh.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "core/voice_proto.h"
#include "gw/gateway.h"
#include "gw/json_line.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (c) g_pass++;                                                \
    else {                                                          \
      g_fail++;                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);      \
    }                                                               \
  } while (0)

struct Sent {
  uint32_t at;
  std::vector<uint8_t> bytes;
  int to = 0;  // the panel it was sent to (gw::kUpstairs, gw::kDownstairs)
};

struct FakeIO : gw::GatewayIO {
  uint32_t now = 1000;
  std::vector<Sent> frames;
  std::vector<std::string> lines;
  // The serial port: cap bytes of buffer, `used` of them waiting. Lines are always written whole (as the
  // sketch does); a line that does not fit sets overflow, which means one would have been cut or lost.
  int cap = 1 << 20;
  int used = 0;
  bool overflow = false;
  uint32_t drops = 0;
  bool dpeer = true;
  uint32_t now_ms() override { return now; }
  int tx_room() override { return cap - used; }
  uint32_t rx_drops() override { return drops; }
  bool down_peer() override { return dpeer; }
  // The voice hello (VOICE_GOT kind 30, key 1 from 0.6, key 0 in 0.5; field fix 2026-11-20) goes to
  // `hellos`, so the other tests count only the frames they are about.
  std::vector<Sent> hellos;
  static bool is_hello(const uint8_t *d, size_t n) {
    tc::VoiceMsg m;
    return n == tc::kVoiceGotLen && tc::voice_decode(d, n, m) && m.type == tc::VType::Got &&
           m.u.got.kind == (uint8_t)tc::VType::Ping && tc::voice_hello_key(m.u.got.key);
  }
  void send_frame(int to, const uint8_t *d, size_t n) override {
    (is_hello(d, n) ? hellos : frames).push_back({now, std::vector<uint8_t>(d, d + n), to});
  }
  void reply(const char *l) override {
    lines.push_back(l);
    used += (int)std::strlen(l) + 1;
    if (used > cap) overflow = true;
  }
  void clear() {
    frames.clear();
    lines.clear();
  }
};

static std::string hex(const std::vector<uint8_t> &b) {
  std::string s;
  char t[4];
  for (size_t i = 0; i < b.size(); i++) {
    std::snprintf(t, sizeof(t), i ? " %02X" : "%02X", b[i]);
    s += t;
  }
  return s;
}

static std::vector<uint8_t> bytes(const char *h) {
  std::vector<uint8_t> v;
  unsigned x;
  for (const char *p = h; std::sscanf(p, "%2x", &x) == 1;) {
    v.push_back((uint8_t)x);
    p += 2;
    while (*p == ' ') p++;
  }
  return v;
}

static void line(gw::Gateway &g, const char *s) { g.on_line(s, std::strlen(s)); }

static void run(gw::Gateway &g, FakeIO &io, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t++) {
    io.now++;
    g.tick();
  }
}

// The brief's examples (gateway/docs/gateway-brief.md), given the same seq.
static const char *kOut1 = "01 08 02 34 12 01 E5 01 47 01 40 02 DF 6A 0C 53 65 74 74 6C 65 64 20 46 69 6E 65";
static const char *kOut2 =
    "01 08 02 35 12 05 B4 00 46 05 6C 03 DF 6A 1A 53 6E 6F 77 20 61 74 20 54 69 6D 65 73 2C 20 57 6F 72 73 65 20 4C 61 74 "
    "65 72";
static const char *kLucyOut = "01 07 02 36 12 01 D0 03 DF 6A";
static const char *kLucyIn = "01 07 02 37 12 00 54 07 DF 6A";

static void test_frames() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1234);
  line(g, "{\"type\":\"outside\",\"id\":12,\"temp_tenths_f\":485,\"humidity\":71,\"forecast\":1,\"flags\":1,"
          "\"time\":1793000000,\"phrase\":\"Settled Fine\"}");
  CHECK(io.frames.size() == 1 && hex(io.frames[0].bytes) == kOut1);
  line(g, "{\"type\":\"outside\",\"id\":13,\"temp_tenths_f\":180,\"humidity\":70,\"forecast\":5,\"flags\":5,"
          "\"time\":1793000300,\"phrase\":\"Snow at Times, Worse Later\"}");
  CHECK(io.frames.size() == 2 && hex(io.frames[1].bytes) == kOut2);
  // The first gave way to the second, which had gone on air: one superseded reply, with its seq.
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"outside\",\"id\":12,\"superseded\":true,\"seq\":4660}");
  line(g, "{\"type\":\"lucy\",\"id\":14,\"out\":1,\"since\":1793000400}");
  CHECK(io.frames.size() == 3 && hex(io.frames[2].bytes) == kLucyOut);
  line(g, "{\"type\":\"lucy\",\"id\":15,\"out\":0,\"since\":1793001300}");
  CHECK(io.frames.size() == 4 && hex(io.frames[3].bytes) == kLucyIn);
  CHECK(g.in_flight() == 2);
}

static void test_acks() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1234);
  line(g, "{\"type\":\"outside\",\"id\":12,\"temp_tenths_f\":485,\"humidity\":71,\"forecast\":1,\"flags\":1,"
          "\"time\":1793000000,\"phrase\":\"Settled Fine\"}");
  std::vector<uint8_t> ack = bytes("01 04 00 00 00 34 12 08");  // the brief's ACK for the first example
  io.now += 9;
  g.on_frame(-1, ack.data(), ack.size());  // an unknown MAC
  ack[2] = 1;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());  // the upstairs MAC with the downstairs sender byte
  ack[2] = 0;
  ack[7] = 7;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());  // the right seq, but for a LUCY
  ack[7] = 8;
  ack[5] = 0x35;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());  // another seq
  CHECK(io.lines.empty() && g.in_flight() == 1);
  ack[5] = 0x34;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"outside\",\"id\":12,\"acked\":\"upstairs\",\"seq\":4660,\"ms\":9}");
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());  // a duplicate: nothing more
  run(g, io, 3000);
  CHECK(io.lines.size() == 1 && io.frames.size() == 1 && g.in_flight() == 0);
  // The Lucy example's ACK, from the downstairs panel (its sender byte 1).
  FakeIO io2;
  gw::Gateway g2(io2, "aa:bb:cc:dd:ee:ff", 0x1236);
  line(g2, "{\"type\":\"lucy\",\"id\":3,\"out\":1,\"since\":1793000400}");
  std::vector<uint8_t> lack = bytes("01 04 01 00 00 36 12 07");
  g2.on_frame(gw::kDownstairs, lack.data(), lack.size());
  CHECK(io2.lines.size() == 1 && io2.lines[0] == "{\"type\":\"lucy\",\"id\":3,\"acked\":\"downstairs\",\"seq\":4662,\"ms\":0}");
  // A frame that isn't an ACK is ignored.
  line(g2, "{\"type\":\"lucy\",\"id\":4,\"out\":0,\"since\":0}");
  std::vector<uint8_t> state = bytes(kLucyIn);
  state[2] = 0;
  g2.on_frame(gw::kUpstairs, state.data(), state.size());
  CHECK(io2.lines.size() == 1 && g2.in_flight() == 1);
}

static void test_retries() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 100);
  uint32_t t0 = io.now;
  line(g, "{\"type\":\"lucy\",\"id\":7,\"out\":1,\"since\":0}");
  run(g, io, 1499);
  CHECK(io.lines.empty());  // not given up yet
  run(g, io, 1);
  CHECK(io.frames.size() == 5);
  const uint32_t want[5] = {0, 150, 400, 750, 1150};
  for (int i = 0; i < 5 && i < (int)io.frames.size(); i++) {
    CHECK(io.frames[i].at - t0 == want[i]);
    CHECK(io.frames[i].bytes == io.frames[0].bytes);  // the same seq every try
  }
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"lucy\",\"id\":7,\"failed\":\"upstairs\",\"seq\":100}");
  run(g, io, 3000);
  CHECK(io.frames.size() == 5 && io.lines.size() == 1);
  // A stall (the loop held up for 600 ms after the second try): the tries spread out after it
  // rather than bunching, as on the panels.
  FakeIO io2;
  gw::Gateway g2(io2, "aa:bb:cc:dd:ee:ff", 200);
  t0 = io2.now;
  line(g2, "{\"type\":\"lucy\",\"id\":8,\"out\":1,\"since\":0}");
  run(g2, io2, 150);
  io2.now += 600;
  run(g2, io2, 1);
  CHECK(io2.frames.size() == 3 && io2.frames[2].at - t0 == 751);
}

static void test_types_apart() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 500);
  line(g, "{\"type\":\"outside\",\"id\":1,\"temp_tenths_f\":485,\"humidity\":71,\"forecast\":1,\"flags\":1,\"time\":0,\"phrase\":\"x\"}");
  run(g, io, 100);
  line(g, "{\"type\":\"lucy\",\"id\":2,\"out\":1,\"since\":0}");
  CHECK(io.lines.empty() && g.in_flight() == 2);  // a LUCY never replaces an OUTSIDE
  // The LUCY's ACK leaves the OUTSIDE retrying on its own clock.
  std::vector<uint8_t> ack = bytes("01 04 00 00 00 F5 01 07");  // seq 501
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 1 && io.lines[0].find("\"type\":\"lucy\",\"id\":2,\"acked\"") != std::string::npos);
  run(g, io, 1400);
  CHECK(io.lines.size() == 2 && io.lines[1] == "{\"type\":\"outside\",\"id\":1,\"failed\":\"upstairs\",\"seq\":500}");
  // Within a type a newer one replaces one in flight; a new seq, its tries from 0.
  line(g, "{\"type\":\"lucy\",\"id\":3,\"out\":1,\"since\":0}");
  run(g, io, 300);
  size_t before = io.frames.size();
  line(g, "{\"type\":\"lucy\",\"id\":4,\"out\":0,\"since\":0}");
  CHECK(io.lines.back() == "{\"type\":\"lucy\",\"id\":3,\"superseded\":true,\"seq\":502}");
  CHECK(io.frames.size() == before + 1 && io.frames.back().bytes[3] == 503 % 256);
  std::vector<uint8_t> late = bytes("01 04 00 00 00 F6 01 07");  // the old one's seq, too late
  g.on_frame(gw::kUpstairs, late.data(), late.size());
  CHECK(g.in_flight() == 1);
  run(g, io, 2000);
  CHECK(io.lines.back() == "{\"type\":\"lucy\",\"id\":4,\"failed\":\"upstairs\",\"seq\":503}");
}

static void test_one_reply_each() {
  // A run of mixed requests with acks, losses and replacements: every id gets exactly one final
  // reply.
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0xFFF0);
  std::map<int, int> finals;
  uint32_t r = 12345;
  int next_id = 1;
  for (int step = 0; step < 4000; step++) {
    r = r * 1103515245u + 12345u;
    int what = (r >> 16) % 100;
    char l[200];
    if (what < 3) {
      std::snprintf(l, sizeof(l), "{\"type\":\"lucy\",\"id\":%d,\"out\":%d,\"since\":0}", next_id++, (r >> 8) & 1);
      line(g, l);
    } else if (what < 6) {
      std::snprintf(l, sizeof(l),
                    "{\"type\":\"outside\",\"id\":%d,\"temp_tenths_f\":%d,\"humidity\":50,\"forecast\":%d,\"flags\":1,\"time\":0,"
                    "\"phrase\":\"x\"}",
                    next_id++, (int)((r >> 4) % 1000), (int)((r >> 8) % 10));  // forecast 6 now and then: rejected
      line(g, l);
    } else if (what < 9 && !io.frames.empty()) {
      // The panel answers the newest frame, or now and then an older one.
      const std::vector<uint8_t> &f = io.frames[(r >> 3) % 4 == 0 && io.frames.size() > 3 ? io.frames.size() - 3 : io.frames.size() - 1].bytes;
      uint8_t ack[8] = {1, 4, 0, 0, 0, f[3], f[4], f[1]};
      g.on_frame(gw::kUpstairs, ack, 8);
    }
    run(g, io, 20);
  }
  run(g, io, 3000);
  int bad = 0;
  for (const std::string &l : io.lines) {
    int id;
    const char *p = std::strstr(l.c_str(), "\"id\":");
    if (!p || std::sscanf(p + 5, "%d", &id) != 1) {
      bad++;
      continue;
    }
    finals[id]++;
  }
  int twice = 0;
  for (auto &e : finals)
    if (e.second != 1) twice++;
  CHECK(bad == 0 && twice == 0 && (int)finals.size() == next_id - 1);
  // The seq counter wrapped past 0xFFFF without using 0.
  bool zero = false;
  for (const Sent &s : io.frames) zero |= s.bytes[3] == 0 && s.bytes[4] == 0;
  CHECK(!zero);
}

static void test_checks() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 1);
  struct {
    const char *in, *out;
  } cases[] = {
      {"{\"type\":\"lucy\",\"id\":13,\"out\":2,\"since\":0}", "{\"type\":\"lucy\",\"id\":13,\"rejected\":\"out must be 0 or 1\"}"},
      {"{\"type\":\"lucy\",\"id\":13,\"out\":1,\"since\":-1}", "{\"type\":\"lucy\",\"id\":13,\"rejected\":\"since must fit a u32\"}"},
      {"{\"type\":\"lucy\",\"id\":13,\"out\":1,\"since\":4294967296}",
       "{\"type\":\"lucy\",\"id\":13,\"rejected\":\"since must fit a u32\"}"},
      {"{\"type\":\"lucy\",\"id\":13,\"out\":1}", "{\"type\":\"lucy\",\"id\":13,\"rejected\":\"since must fit a u32\"}"},
      {"{\"type\":\"lucy\",\"id\":0,\"out\":1,\"since\":0}", "{\"type\":\"lucy\",\"rejected\":\"id must be a positive whole number\"}"},
      {"{\"type\":\"lucy\",\"out\":1,\"since\":0}", "{\"type\":\"lucy\",\"rejected\":\"id must be a positive whole number\"}"},
      {"{\"type\":\"outside\",\"id\":5,\"temp_tenths_f\":40000,\"humidity\":1,\"forecast\":1,\"flags\":1,\"time\":0,\"phrase\":\"\"}",
       "{\"type\":\"outside\",\"id\":5,\"rejected\":\"temp_tenths_f must fit an int16\"}"},
      {"{\"type\":\"outside\",\"id\":5,\"temp_tenths_f\":48.5,\"humidity\":1,\"forecast\":1,\"flags\":1,\"time\":0,\"phrase\":\"\"}",
       "{\"type\":\"outside\",\"id\":5,\"rejected\":\"temp_tenths_f must fit an int16\"}"},
      {"{\"type\":\"outside\",\"id\":5,\"temp_tenths_f\":1,\"humidity\":101,\"forecast\":1,\"flags\":1,\"time\":0,\"phrase\":\"\"}",
       "{\"type\":\"outside\",\"id\":5,\"rejected\":\"humidity must be 0 to 100\"}"},
      {"{\"type\":\"outside\",\"id\":5,\"temp_tenths_f\":1,\"humidity\":1,\"forecast\":6,\"flags\":1,\"time\":0,\"phrase\":\"\"}",
       "{\"type\":\"outside\",\"id\":5,\"rejected\":\"forecast must be 0 to 9, not 6\"}"},
      {"{\"type\":\"outside\",\"id\":5,\"temp_tenths_f\":1,\"humidity\":1,\"forecast\":10,\"flags\":1,\"time\":0,\"phrase\":\"\"}",
       "{\"type\":\"outside\",\"id\":5,\"rejected\":\"forecast must be 0 to 9, not 6\"}"},
      {"{\"type\":\"outside\",\"id\":5,\"temp_tenths_f\":1,\"humidity\":1,\"forecast\":1,\"flags\":8,\"time\":0,\"phrase\":\"\"}",
       "{\"type\":\"outside\",\"id\":5,\"rejected\":\"flags must be 0 to 7\"}"},
      {"{\"type\":\"outside\",\"id\":5,\"temp_tenths_f\":1,\"humidity\":1,\"forecast\":1,\"flags\":1,\"time\":-5,\"phrase\":\"\"}",
       "{\"type\":\"outside\",\"id\":5,\"rejected\":\"time must fit a u32\"}"},
      {"{\"type\":\"outside\",\"id\":5,\"temp_tenths_f\":1,\"humidity\":1,\"forecast\":1,\"flags\":1,\"time\":0,\"phrase\":7}",
       "{\"type\":\"outside\",\"id\":5,\"rejected\":\"phrase must be a string\"}"},
      {"not json", "{\"error\":\"bad_json\"}"},
      {"{\"type\":\"lucy\",\"id\":1,", "{\"error\":\"bad_json\"}"},
      {"{\"type\":\"weather\",\"id\":1}", "{\"error\":\"unknown_type\"}"},
      {"{\"id\":1}", "{\"error\":\"unknown_type\"}"},
      {"{\"cmd\":\"reboot\"}", "{\"error\":\"unknown_type\"}"},
      {"{\"cmd\":\"hello\"}", "{\"boot\":false,\"mac\":\"aa:bb:cc:dd:ee:ff\",\"channel\":1,\"fw\":\"gateway-c6 0.7\",\"voice\":1,\"drops\":0,\"down_peer\":true}"},
  };
  for (auto &c : cases) {
    io.clear();
    line(g, c.in);
    bool ok = io.lines.size() == 1 && io.lines[0] == c.out && io.frames.empty();
    if (!ok) std::printf("  for %s got %s\n", c.in, io.lines.empty() ? "nothing" : io.lines[0].c_str());
    CHECK(ok);
  }
  // Too long: dropped as bad JSON, even if it would parse.
  std::string longline = "{\"type\":\"lucy\",\"id\":1,\"out\":1,\"since\":0,\"pad\":\"" + std::string(500, 'x') + "\"}";
  io.clear();
  line(g, longline.c_str());
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"error\":\"bad_json\"}" && io.frames.empty());
  // The phrase: cut to 48, and non-ASCII made "?" (one per character).
  io.clear();
  line(g, "{\"type\":\"outside\",\"id\":9,\"temp_tenths_f\":1,\"humidity\":1,\"forecast\":0,\"flags\":1,\"time\":0,"
          "\"phrase\":\"Rain 5\xC2\xB0\\u00b0 \\u2014 \\ud83d\\ude00 and then a great deal more text than fits\"}");
  CHECK(io.frames.size() == 1 && io.frames[0].bytes.size() == 15 + 48);
  std::string ph(io.frames[0].bytes.begin() + 15, io.frames[0].bytes.end());
  CHECK(ph == std::string("Rain 5?? ? ? and then a great deal more text than fits").substr(0, 48));
  // Unknown keys and nested values are fine; the boot line.
  io.clear();
  line(g, "{\"type\":\"lucy\",\"id\":10,\"out\":1,\"since\":5,\"extra\":{\"a\":[1,2,{\"b\":null}]},\"note\":\"hi\"}");
  CHECK(io.lines.empty() && io.frames.size() == 1);
  io.clear();
  g.boot();
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"boot\":true,\"mac\":\"aa:bb:cc:dd:ee:ff\",\"channel\":1,\"fw\":\"gateway-c6 0.7\",\"voice\":1,\"drops\":0,\"down_peer\":true}");
}

static void test_json() {
  gw::JObject o;
  const char *s = " { \"a\" : -12 , \"b\":\"x\\\"y\", \"c\":true, \"d\":1.5, \"e\":99999999999999999999, \"f\":null } ";
  CHECK(gw::json_parse_object(s, std::strlen(s), o) && o.n == 6);
  CHECK(o.get("a")->type == gw::JType::Int && o.get("a")->i == -12);
  CHECK(!std::strcmp(o.get("b")->s, "x\"y"));
  CHECK(o.get("c")->type == gw::JType::Bool && o.get("c")->b);
  CHECK(o.get("d")->type == gw::JType::Number && o.get("e")->type == gw::JType::Number);
  CHECK(o.get("f")->type == gw::JType::Null && !o.get("g"));
  const char *bad[] = {"", "[]", "{", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "{\"a\":1} x", "{\"a\":01x}", "{\"a\":\"\\q\"}"};
  for (const char *b : bad) CHECK(!gw::json_parse_object(b, std::strlen(b), o));
}

// Mike, 2026-11-07: the Pi's {"type":"update"} line becomes an UPDATE frame (type 15, one reserved
// byte 0) to the upstairs panel, with the usual retries and one final reply.
static void test_update() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x2000);
  line(g, "{\"type\":\"update\",\"id\":21}");
  CHECK(io.lines.empty() && io.frames.size() == 1 && g.in_flight() == 1);
  CHECK(hex(io.frames[0].bytes) == "01 0F 02 00 20 00");  // version, UPDATE, gateway, seq 0x2000, reserved 0
  tc::Msg m;
  CHECK(tc::decode(io.frames[0].bytes.data(), io.frames[0].bytes.size(), m) && m.type == tc::MsgType::Update &&
        m.sender == tc::kSenderGateway && m.seq == 0x2000);
  // A repeat while it waits: answered busy, nothing more sent, the first keeps its reply.
  run(g, io, 50);
  size_t sent = io.frames.size();
  line(g, "{\"type\":\"update\",\"id\":22}");
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"update\",\"id\":22,\"busy\":true,\"seq\":8192}");
  CHECK(io.frames.size() == sent && g.in_flight() == 1);
  // The ACK carries the UPDATE type (15) and the frame's seq; one for another type or seq is ignored.
  std::vector<uint8_t> ack = bytes("01 04 00 00 00 00 20 0F");
  ack[7] = 8;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  ack[7] = 15;
  ack[5] = 0x01;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 1 && g.in_flight() == 1);
  ack[5] = 0x00;
  io.now += 7;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 2 && io.lines[1] == "{\"type\":\"update\",\"id\":21,\"acked\":\"upstairs\",\"seq\":8192,\"ms\":57}");
  CHECK(g.in_flight() == 0 && !std::strcmp(g.status().last, "update acked 57ms"));
  // Now free again: a new request gets a new seq.
  line(g, "{\"type\":\"update\",\"id\":23}");
  CHECK(hex(io.frames.back().bytes) == "01 0F 02 01 20 00");
  // An UPDATE does not disturb an OUTSIDE or a LUCY in flight, and gives up on the usual schedule.
  FakeIO io2;
  gw::Gateway g2(io2, "aa:bb:cc:dd:ee:ff", 0x2100);
  line(g2, "{\"type\":\"lucy\",\"id\":1,\"out\":1,\"since\":0}");
  line(g2, "{\"type\":\"update\",\"id\":2}");
  CHECK(g2.in_flight() == 2 && io2.lines.empty());
  uint32_t t0 = io2.frames[1].at;
  run(g2, io2, 1600);
  CHECK(io2.lines.size() == 2);
  CHECK(io2.lines[0] == "{\"type\":\"lucy\",\"id\":1,\"failed\":\"upstairs\",\"seq\":8448}");
  CHECK(io2.lines[1] == "{\"type\":\"update\",\"id\":2,\"failed\":\"upstairs\",\"seq\":8449}");
  CHECK(io2.frames.size() == 10 && io2.frames[9].at - t0 >= 1100 && g2.in_flight() == 0);
  CHECK(!std::strcmp(g2.status().last, "update failed") && g2.status().failed == 2);
  // Bad ids are rejected the way the other types are; extra fields are ignored.
  FakeIO io3;
  gw::Gateway g3(io3, "aa:bb:cc:dd:ee:ff", 0x2200);
  line(g3, "{\"type\":\"update\"}");
  line(g3, "{\"type\":\"update\",\"id\":0}");
  CHECK(io3.lines.size() == 2 && io3.frames.empty() && g3.status().rejected == 2);
  CHECK(io3.lines[0] == "{\"type\":\"update\",\"rejected\":\"id must be a positive whole number\"}");
  line(g3, "{\"type\":\"update\",\"id\":5,\"whatever\":\"x\"}");
  CHECK(io3.frames.size() == 1 && g3.in_flight() == 1);
}


// A NOTICE from the upstairs panel: ACKed like a panel would, printed once as an "event" line.
static std::vector<uint8_t> notice_frame(uint8_t sender, uint16_t seq, uint8_t kind, uint8_t floor, int16_t value, uint32_t time,
                                         const char *text) {
  tc::Msg m;
  m.type = tc::MsgType::Notice;
  m.sender = sender;
  m.seq = seq;
  m.u.notice.kind = kind;
  m.u.notice.floor = floor;
  m.u.notice.value = value;
  m.u.notice.time = time;
  std::strncpy(m.u.notice.text, text, tc::kNoticeTextMax);
  m.u.notice.text[tc::kNoticeTextMax] = 0;
  uint8_t buf[tc::kMaxMsg];
  size_t n = tc::encode(m, buf, sizeof(buf));
  return std::vector<uint8_t>(buf, buf + n);
}

static void test_notices() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x3000);
  auto send = [&](int from, const std::vector<uint8_t> &f) { g.on_frame(from, f.data(), f.size()); };
  // The line, and the ACK the gateway sends back: ACK, from the gateway, seq 0, naming the NOTICE's seq and type.
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 0x1234, 1, 1, 0, 1793000000u, ""));
  CHECK(io.lines.size() == 1);
  CHECK(io.lines[0] == "{\"event\":\"notice\",\"kind\":\"knock\",\"floor\":\"downstairs\",\"value\":0,\"time\":1793000000,\"text\":\"\"}");
  CHECK(io.frames.size() == 1 && hex(io.frames[0].bytes) == "01 04 02 00 00 34 12 11");
  // A retry of the same frame (our ACK was lost): ACKed again, printed once.
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 0x1234, 1, 1, 0, 1793000000u, ""));
  CHECK(io.lines.size() == 1 && io.frames.size() == 2 && io.frames[1].bytes == io.frames[0].bytes);
  // Every kind, by its word; the value signed; the text as sent.
  const char *words[] = {"knock", "arrow_urgent", "arrow_mild", "mail", "dog_out", "status", "note", "ring"};
  for (int k = 1; k <= 8; k++) {
    io.clear();
    send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, (uint16_t)(100 + k), (uint8_t)k, 0, (int16_t)(k == 2 ? -1 : k), 5, "hi"));
    char want[200];
    std::snprintf(want, sizeof(want), "{\"event\":\"notice\",\"kind\":\"%s\",\"floor\":\"upstairs\",\"value\":%d,\"time\":5,\"text\":\"hi\"}",
                  words[k - 1], k == 2 ? -1 : k);
    CHECK(io.lines.size() == 1 && io.lines[0] == want);
  }
  // Escaping: quote and backslash, and the longest text (40 characters, all of them needing it) stays well inside a line.
  io.clear();
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 200, 7, 1, 0, 9, "say \"hi\" \\ bye"));
  CHECK(io.lines.size() == 1 && io.lines[0] ==
        "{\"event\":\"notice\",\"kind\":\"note\",\"floor\":\"downstairs\",\"value\":0,\"time\":9,\"text\":\"say \\\"hi\\\" \\\\ bye\"}");
  io.clear();
  std::string nasty = std::string(20, '"') + std::string(20, '\\');
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 201, 7, 1, 0, 9, nasty.c_str()));
  CHECK(io.lines.size() == 1 && io.lines[0].size() < gw::kMaxLine && io.lines[0].find("\"text\":\"\\\"\\\"") != std::string::npos);
  CHECK(io.lines[0].back() == '}');
  // A control character or non-ASCII byte in the text arrives as '?' (decode), never raw on the serial line.
  io.clear();
  {
    std::vector<uint8_t> f = notice_frame(tc::kSenderUpstairs, 202, 7, 0, 0, 9, "abc");
    f[f.size() - 2] = 0x0A;  // a newline inside the text would split the line
    f[f.size() - 1] = 0xC3;
    send(gw::kUpstairs, f);
    CHECK(io.lines.size() == 1 && io.lines[0].find("\"text\":\"a??\"") != std::string::npos);
    CHECK(io.lines[0].find('\n') == std::string::npos);
  }
  // The same seq long after is a new notice (the panel's counter came round, or it restarted).
  io.clear();
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 300, 4, 1, 1, 77, ""));
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 300, 4, 1, 1, 77, ""));
  CHECK(io.lines.size() == 1);
  io.now += 11000;
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 300, 4, 1, 1, 78, ""));
  CHECK(io.lines.size() == 2 && io.lines[1].find("\"time\":78") != std::string::npos);
  // Unknown MAC (-1), the downstairs panel, or a sender byte that does not match the MAC: no line, no ACK.
  io.clear();
  send(-1, notice_frame(tc::kSenderUpstairs, 400, 1, 0, 0, 1, ""));
  send(gw::kDownstairs, notice_frame(tc::kSenderDownstairs, 401, 1, 0, 0, 1, ""));
  send(gw::kUpstairs, notice_frame(tc::kSenderDownstairs, 402, 1, 0, 0, 1, ""));
  CHECK(io.lines.empty() && io.frames.empty());
  // Malformed: a kind of 0 or 9, a bad floor, a cut frame, trailing bytes: ignored, no ACK.
  std::vector<uint8_t> f = notice_frame(tc::kSenderUpstairs, 410, 1, 0, 0, 1, "ab");
  std::vector<uint8_t> bad = f;
  bad[5] = 0;
  send(gw::kUpstairs, bad);
  bad = f;
  bad[5] = 9;
  send(gw::kUpstairs, bad);
  bad = f;
  bad[6] = 2;
  send(gw::kUpstairs, bad);
  bad = f;
  bad.pop_back();
  send(gw::kUpstairs, bad);
  bad = f;
  bad.push_back(0);
  send(gw::kUpstairs, bad);
  CHECK(io.lines.empty() && io.frames.empty());
  // A notice does not touch the requests: nothing in flight, no counters, and an UPDATE still works.
  CHECK(g.in_flight() == 0 && g.status().acked == 0 && g.status().requests == 0);
  line(g, "{\"type\":\"update\",\"id\":1}");
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 500, 1, 0, 0, 1, ""));
  CHECK(g.in_flight() == 1 && io.lines.size() == 1);
}

// ROOMS from the upstairs panel (0.7, Mike, 2026-11-22): ACKed every time, printed once per seq as a "rooms" line.
static std::vector<uint8_t> rooms_frame(uint8_t sender, uint16_t seq, uint8_t flags, int16_t up_t, uint16_t up_h, int16_t down_t,
                                        uint16_t down_h, uint32_t time) {
  tc::Msg m;
  m.type = tc::MsgType::Rooms;
  m.sender = sender;
  m.seq = seq;
  m.u.rooms.flags = flags;
  m.u.rooms.up_t = up_t;
  m.u.rooms.up_h = up_h;
  m.u.rooms.down_t = down_t;
  m.u.rooms.down_h = down_h;
  m.u.rooms.time = time;
  uint8_t buf[tc::kMaxMsg];
  size_t n = tc::encode(m, buf, sizeof(buf));
  return std::vector<uint8_t>(buf, buf + n);
}

static void test_rooms() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x3100);
  auto send = [&](int from, const std::vector<uint8_t> &f) { g.on_frame(from, f.data(), f.size()); };
  CHECK(!std::strcmp(gw::kFirmware, "gateway-c6 0.7"));
  // The panel agent's wire example, byte for byte: the line, and the ACK naming ROOMS (32) and seq 0x0102.
  {
    const uint8_t ex[] = {0x01, 0x20, 0x00, 0x02, 0x01, 0x0F, 0xB4, 0x02, 0x3A, 0x02, 0xF1, 0xFF, 0xC7, 0x01, 0x88, 0xFF, 0x02, 0x6B};
    g.on_frame(gw::kUpstairs, ex, sizeof(ex));
    CHECK(io.lines.size() == 1);
    CHECK(io.lines[0] == "{\"event\":\"rooms\",\"up_t\":69.2,\"up_h\":57.0,\"down_t\":-1.5,\"down_h\":45.5,\"occ\":1,\"time\":1795358600}");
    CHECK(io.frames.size() == 1 && hex(io.frames[0].bytes) == "01 04 02 00 00 02 01 20");
    CHECK(io.frames[0].to == gw::kUpstairs);
  }
  // The contract's own example.
  io.clear();
  send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 10, 0x0F, 692, 570, 681, 455, 1795358600u));
  CHECK(io.lines.size() == 1 &&
        io.lines[0] == "{\"event\":\"rooms\",\"up_t\":69.2,\"up_h\":57.0,\"down_t\":68.1,\"down_h\":45.5,\"occ\":1,\"time\":1795358600}");
  // Retries of the same seq (our ACK was lost): ACKed every time, printed once.
  for (int i = 0; i < 4; i++) send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 10, 0x0F, 692, 570, 681, 455, 1795358600u));
  CHECK(io.lines.size() == 1 && io.frames.size() == 5);
  for (size_t i = 1; i < io.frames.size(); i++) CHECK(io.frames[i].bytes == io.frames[0].bytes);
  // A new seq prints again.
  send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 11, 0x0F, 692, 570, 681, 455, 1795358900u));
  CHECK(io.lines.size() == 2 && io.lines[1].find("\"time\":1795358900}") != std::string::npos);
  // Every flag combination: nulls for a missing floor, occ 1/0 only when known.
  const uint8_t combos[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F};
  uint16_t seq = 100;
  for (uint8_t fl : combos) {
    io.clear();
    send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, seq++, fl, -5, 1000, 0, 0, 0));
    const char *ut = (fl & 1) ? "-0.5" : "null";
    const char *uh = (fl & 1) ? "100.0" : "null";
    const char *dt = (fl & 2) ? "0.0" : "null";
    const char *dh = (fl & 2) ? "0.0" : "null";
    const char *oc = !(fl & 4) ? "null" : (fl & 8) ? "1" : "0";
    char want[200];
    std::snprintf(want, sizeof(want), "{\"event\":\"rooms\",\"up_t\":%s,\"up_h\":%s,\"down_t\":%s,\"down_h\":%s,\"occ\":%s,\"time\":0}",
                  ut, uh, dt, dh, oc);
    CHECK(io.lines.size() == 1 && io.lines[0] == want);
    CHECK(io.frames.size() == 1);
  }
  // Negative and extreme temperatures, whole numbers keep their ".0".
  io.clear();
  send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 200, 0x03, -1, 9, -32768, 1, 4294967295u));
  CHECK(io.lines.size() == 1 &&
        io.lines[0] == "{\"event\":\"rooms\",\"up_t\":-0.1,\"up_h\":0.9,\"down_t\":-3276.8,\"down_h\":0.1,\"occ\":null,\"time\":4294967295}");
  io.clear();
  send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 201, 0x03, 32767, 455, -120, 999, 1));
  CHECK(io.lines.size() == 1 &&
        io.lines[0] == "{\"event\":\"rooms\",\"up_t\":3276.7,\"up_h\":45.5,\"down_t\":-12.0,\"down_h\":99.9,\"occ\":null,\"time\":1}");
  // The same seq long after is new (the panel restarted).
  io.clear();
  send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 300, 0x01, 700, 500, 0, 0, 5));
  send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 300, 0x01, 700, 500, 0, 0, 5));
  CHECK(io.lines.size() == 1);
  io.now += 11000;
  send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 300, 0x01, 700, 500, 0, 0, 6));
  CHECK(io.lines.size() == 2 && io.frames.size() == 3);
  // ROOMS and NOTICE keep separate seen-lists: a ROOMS never makes a notice with the same seq look like a repeat.
  io.clear();
  send(gw::kUpstairs, rooms_frame(tc::kSenderUpstairs, 400, 0x01, 700, 500, 0, 0, 5));
  send(gw::kUpstairs, notice_frame(tc::kSenderUpstairs, 400, 1, 1, 0, 5, ""));
  CHECK(io.lines.size() == 2 && io.lines[1].find("\"event\":\"notice\"") != std::string::npos);
  // From downstairs, an unknown MAC, or a sender byte that does not match: no line, no ACK.
  io.clear();
  send(gw::kDownstairs, rooms_frame(tc::kSenderDownstairs, 500, 0x0F, 700, 500, 700, 500, 5));
  send(-1, rooms_frame(tc::kSenderUpstairs, 501, 0x0F, 700, 500, 700, 500, 5));
  send(gw::kUpstairs, rooms_frame(tc::kSenderDownstairs, 502, 0x0F, 700, 500, 700, 500, 5));
  send(gw::kDownstairs, rooms_frame(tc::kSenderUpstairs, 503, 0x0F, 700, 500, 700, 500, 5));
  CHECK(io.lines.empty() && io.frames.empty());
  // Malformed (decode refuses): cut, trailing byte, a flag above 0x0F, occupied without known, humidity over 1000.
  std::vector<uint8_t> f = rooms_frame(tc::kSenderUpstairs, 600, 0x0F, 700, 500, 700, 500, 5);
  CHECK(f.size() == 18);
  std::vector<uint8_t> bad = f;
  bad.pop_back();
  send(gw::kUpstairs, bad);
  bad = f;
  bad.push_back(0);
  send(gw::kUpstairs, bad);
  bad = f;
  bad[5] = 0x1F;
  send(gw::kUpstairs, bad);
  bad = f;
  bad[5] = 0x08;
  send(gw::kUpstairs, bad);
  bad = f;
  bad[9] = 0x03;  // up_h 500 (F4 01) becomes 0x03F4 = 1012
  send(gw::kUpstairs, bad);
  CHECK(io.lines.empty() && io.frames.empty());
  // Nothing in flight is touched, and the counters stay put.
  CHECK(g.in_flight() == 0 && g.status().acked == 0 && g.status().requests == 0);
}


static void test_phone() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x4000);
  // PHONE_ANSWER: version 1, type 18, sender 2, seq LE, floor, knock id LE, come_on.
  line(g, "{\"type\":\"answer\",\"id\":31,\"floor\":\"downstairs\",\"knock\":43981,\"come_on\":1}");
  CHECK(io.frames.size() == 1 && hex(io.frames[0].bytes) == "01 12 02 00 40 01 CD AB 01");
  tc::Msg m;
  CHECK(tc::decode(io.frames[0].bytes.data(), io.frames[0].bytes.size(), m) && m.type == tc::MsgType::PhoneAnswer &&
        m.u.phone.floor == 1 && m.u.phone.id == 43981 && m.u.phone.come_on == 1);
  // MAIL_ACK: type 19, one reserved byte.
  line(g, "{\"type\":\"mail_ack\",\"id\":32}");
  CHECK(io.frames.size() == 2 && hex(io.frames[1].bytes) == "01 13 02 01 40 00");
  CHECK(g.in_flight() == 2 && io.lines.empty());
  // ACKs by type: a wrong type or seq is ignored; the right ones give acked replies.
  std::vector<uint8_t> ack = bytes("01 04 00 00 00 00 40 12");
  ack[7] = 15;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  ack[7] = 19;  // MAIL_ACK type with the answer's seq
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.empty());
  ack[7] = 18;
  io.now += 6;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"answer\",\"id\":31,\"acked\":\"upstairs\",\"seq\":16384,\"ms\":6}");
  CHECK(!std::strcmp(g.status().last, "answer acked 6ms"));
  ack[7] = 19;
  ack[5] = 0x01;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 2 && io.lines[1] == "{\"type\":\"mail_ack\",\"id\":32,\"acked\":\"upstairs\",\"seq\":16385,\"ms\":6}");
  CHECK(g.in_flight() == 0);
  // Latest wins within a type; the other type is left alone.
  io.clear();
  line(g, "{\"type\":\"answer\",\"id\":33,\"floor\":\"upstairs\",\"knock\":1,\"come_on\":0}");
  CHECK(hex(io.frames.back().bytes) == "01 12 02 02 40 00 01 00 00");
  line(g, "{\"type\":\"mail_ack\",\"id\":34}");
  line(g, "{\"type\":\"answer\",\"id\":35,\"floor\":\"upstairs\",\"knock\":65535,\"come_on\":1}");
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"answer\",\"id\":33,\"superseded\":true,\"seq\":16386}");
  CHECK(hex(io.frames.back().bytes) == "01 12 02 04 40 00 FF FF 01" && g.in_flight() == 2);
  // Retries and give-up on the usual schedule, one final reply each.
  run(g, io, 1600);
  CHECK(io.lines.size() == 3 && g.in_flight() == 0);
  CHECK(io.lines[2] == "{\"type\":\"mail_ack\",\"id\":34,\"failed\":\"upstairs\",\"seq\":16387}");
  CHECK(io.lines[1] == "{\"type\":\"answer\",\"id\":35,\"failed\":\"upstairs\",\"seq\":16388}");
  CHECK(!std::strcmp(g.status().last, "mail failed"));
  // Answers for different rung floors each keep their own slot: neither supersedes the other, and
  // each ACK (same type, its own seq) finishes only its own (Mike, 2026-11-15).
  io.clear();
  line(g, "{\"type\":\"answer\",\"id\":40,\"floor\":\"upstairs\",\"knock\":7,\"come_on\":1}");
  line(g, "{\"type\":\"answer\",\"id\":41,\"floor\":\"downstairs\",\"knock\":9,\"come_on\":0}");
  CHECK(io.lines.empty() && g.in_flight() == 2);
  CHECK(hex(io.frames[0].bytes) == "01 12 02 05 40 00 07 00 01" && hex(io.frames[1].bytes) == "01 12 02 06 40 01 09 00 00");
  ack = bytes("01 04 00 00 00 00 40 12");
  ack[5] = 0x06;  // the downstairs answer's seq
  io.now += 9;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"answer\",\"id\":41,\"acked\":\"upstairs\",\"seq\":16390,\"ms\":9}");
  CHECK(g.in_flight() == 1);
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());  // a repeat of that ACK finishes nothing else
  CHECK(io.lines.size() == 1 && g.in_flight() == 1);
  ack[5] = 0x05;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 2 && io.lines[1] == "{\"type\":\"answer\",\"id\":40,\"acked\":\"upstairs\",\"seq\":16389,\"ms\":9}");
  CHECK(g.in_flight() == 0);
  // A newer answer for the same floor still supersedes; the other floor's is left alone.
  io.clear();
  line(g, "{\"type\":\"answer\",\"id\":42,\"floor\":\"downstairs\",\"knock\":9,\"come_on\":1}");
  line(g, "{\"type\":\"answer\",\"id\":43,\"floor\":\"upstairs\",\"knock\":8,\"come_on\":1}");
  line(g, "{\"type\":\"answer\",\"id\":44,\"floor\":\"downstairs\",\"knock\":10,\"come_on\":0}");
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"answer\",\"id\":42,\"superseded\":true,\"seq\":16391}");
  CHECK(g.in_flight() == 2);
  run(g, io, 1600);
  CHECK(io.lines.size() == 3 && g.in_flight() == 0);
  CHECK(io.lines[1] == "{\"type\":\"answer\",\"id\":43,\"failed\":\"upstairs\",\"seq\":16392}");
  CHECK(io.lines[2] == "{\"type\":\"answer\",\"id\":44,\"failed\":\"upstairs\",\"seq\":16393}");
  // The screen shows the mail kind as "mail", so a late ACK still fits its 20 characters.
  io.clear();
  line(g, "{\"type\":\"mail_ack\",\"id\":45}");
  ack[5] = 0x0A;
  ack[7] = 19;
  io.now += 1150;
  g.on_frame(gw::kUpstairs, ack.data(), ack.size());
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"mail_ack\",\"id\":45,\"acked\":\"upstairs\",\"seq\":16394,\"ms\":1150}");
  CHECK(!std::strcmp(g.status().last, "mail acked 1150ms") && std::strlen(g.status().last) <= 20);
  // Rejections.
  FakeIO io2;
  gw::Gateway g2(io2, "aa:bb:cc:dd:ee:ff", 0x4100);
  line(g2, "{\"type\":\"answer\",\"id\":1,\"floor\":\"attic\",\"knock\":5,\"come_on\":1}");
  line(g2, "{\"type\":\"answer\",\"id\":2,\"floor\":1,\"knock\":5,\"come_on\":1}");
  line(g2, "{\"type\":\"answer\",\"id\":3,\"floor\":\"upstairs\",\"knock\":0,\"come_on\":1}");
  line(g2, "{\"type\":\"answer\",\"id\":4,\"floor\":\"upstairs\",\"knock\":65536,\"come_on\":1}");
  line(g2, "{\"type\":\"answer\",\"id\":5,\"floor\":\"upstairs\",\"knock\":5,\"come_on\":2}");
  line(g2, "{\"type\":\"answer\",\"id\":6,\"floor\":\"upstairs\",\"knock\":5}");
  line(g2, "{\"type\":\"answer\",\"floor\":\"upstairs\",\"knock\":5,\"come_on\":1}");
  line(g2, "{\"type\":\"mail_ack\"}");
  line(g2, "{\"type\":\"mail_ack\",\"id\":0}");
  CHECK(io2.frames.empty() && io2.lines.size() == 9 && g2.status().rejected == 9);
  CHECK(io2.lines[0] == "{\"type\":\"answer\",\"id\":1,\"rejected\":\"floor must be upstairs or downstairs\"}");
  CHECK(io2.lines[2] == "{\"type\":\"answer\",\"id\":3,\"rejected\":\"knock must be 1 to 65535\"}");
  CHECK(io2.lines[4] == "{\"type\":\"answer\",\"id\":5,\"rejected\":\"come_on must be 0 or 1\"}");
  CHECK(io2.lines[6] == "{\"type\":\"answer\",\"rejected\":\"id must be a positive whole number\"}");
  CHECK(io2.lines[7] == "{\"type\":\"mail_ack\",\"rejected\":\"id must be a positive whole number\"}");
  // The ring notice: the knock id prints unsigned, even above 32767.
  FakeIO io3;
  gw::Gateway g3(io3, "aa:bb:cc:dd:ee:ff", 0x4200);
  std::vector<uint8_t> rg = notice_frame(tc::kSenderUpstairs, 7, 8, 1, (int16_t)(uint16_t)43981, 1793000000u, "RING");
  g3.on_frame(gw::kUpstairs, rg.data(), rg.size());
  CHECK(io3.lines.size() == 1 && io3.lines[0] ==
        "{\"event\":\"notice\",\"kind\":\"ring\",\"floor\":\"downstairs\",\"value\":43981,\"time\":1793000000,\"text\":\"RING\"}");
  io3.clear();
  std::vector<uint8_t> an = notice_frame(tc::kSenderUpstairs, 8, 8, 0, (int16_t)(uint16_t)65535, 0, "ANSWERED");
  g3.on_frame(gw::kUpstairs, an.data(), an.size());
  CHECK(io3.lines.size() == 1 && io3.lines[0] ==
        "{\"event\":\"notice\",\"kind\":\"ring\",\"floor\":\"upstairs\",\"value\":65535,\"time\":0,\"text\":\"ANSWERED\"}");
  // "No mail" (Mike, 2026-11-18) is a Mail notice with value 3 and text NOMAIL; a real mail notice keeps
  // value 1 and no text. The gateway prints both as they come: it needs no change for this.
  io3.clear();
  std::vector<uint8_t> nm = notice_frame(tc::kSenderUpstairs, 9, 4, 1, 3, 1793000000u, "NOMAIL");
  g3.on_frame(gw::kUpstairs, nm.data(), nm.size());
  CHECK(io3.lines.size() == 1 && io3.lines[0] ==
        "{\"event\":\"notice\",\"kind\":\"mail\",\"floor\":\"downstairs\",\"value\":3,\"time\":1793000000,\"text\":\"NOMAIL\"}");
  io3.clear();
  std::vector<uint8_t> ml = notice_frame(tc::kSenderUpstairs, 10, 4, 1, 1, 1793000000u, "");
  g3.on_frame(gw::kUpstairs, ml.data(), ml.size());
  CHECK(io3.lines.size() == 1 && io3.lines[0] ==
        "{\"event\":\"notice\",\"kind\":\"mail\",\"floor\":\"downstairs\",\"value\":1,\"time\":1793000000,\"text\":\"\"}");
}

// ---------------------------------------------------------------------------------------------------
// Voice notes (C6 gateway 0.5, firmware/docs/voice-notes/DESIGN.md sections 4.3 to 4.7 and 5)
// ---------------------------------------------------------------------------------------------------

static std::vector<uint8_t> vf(tc::VoiceMsg m, int sender, int floor) {
  m.sender = (uint8_t)sender;
  m.floor = (uint8_t)floor;
  m.seq = 1;
  uint8_t b[tc::kMaxMsg];
  size_t n = tc::voice_encode(m, b, sizeof(b));
  CHECK(n > 0);
  return std::vector<uint8_t>(b, b + n);
}

static std::vector<uint8_t> v_begin(int x, int sender, int floor, int codec = 2, int max_chunks = 104, int purpose = 2) {
  tc::VoiceMsg m;
  m.type = tc::VType::Begin;
  m.u.begin = {(uint8_t)x, (uint8_t)purpose, (uint8_t)codec, (uint16_t)(codec == 2 ? 474 : 237), (uint8_t)max_chunks};
  return vf(m, sender, floor);
}

static std::vector<uint8_t> chunk_bytes(int idx) {
  std::vector<uint8_t> d(237);
  for (size_t i = 0; i < d.size(); i++) d[i] = (uint8_t)(idx * 7 + (int)i * 3);
  return d;
}

static std::vector<uint8_t> v_data(int x, int idx, int sender, int floor, int pred = -311, int step = 41) {
  tc::VoiceMsg m;
  m.type = tc::VType::Data;
  m.u.data.xfer = (uint8_t)x;
  m.u.data.idx = (uint8_t)idx;
  m.u.data.pred = (int16_t)pred;
  m.u.data.step = (uint8_t)step;
  m.u.data.len = 237;
  std::vector<uint8_t> d = chunk_bytes(idx);
  std::memcpy(m.u.data.data, d.data(), 237);
  return vf(m, sender, floor);
}

static std::vector<uint8_t> v_end(int x, int n, int ns, int sender, int floor, int speech = 1) {
  tc::VoiceMsg m;
  m.type = tc::VType::End;
  m.u.end = {(uint8_t)x, (uint8_t)n, (uint16_t)ns, (uint8_t)speech};
  return vf(m, sender, floor);
}

static std::vector<uint8_t> v_abort(int x, int reason, int sender, int floor) {
  tc::VoiceMsg m;
  m.type = tc::VType::Abort;
  m.u.abort = {(uint8_t)x, (uint8_t)reason};
  return vf(m, sender, floor);
}

static std::vector<uint8_t> v_got(int kind, uint32_t key, int result, int sender, int floor) {
  tc::VoiceMsg m;
  m.type = tc::VType::Got;
  m.u.got = {(uint8_t)kind, key, (uint8_t)result};
  return vf(m, sender, floor);
}

static std::vector<uint8_t> v_ping(uint32_t nonce, int sender, int floor) {
  tc::VoiceMsg m;
  m.type = tc::VType::Ping;
  m.u.ping.nonce = nonce;
  return vf(m, sender, floor);
}

static std::vector<uint8_t> v_push(uint32_t id, int from, uint32_t time, const char *text, int sender, int floor) {
  tc::VoiceMsg m;
  m.type = tc::VType::Push;
  m.u.push.id = id;
  m.u.push.from = (uint8_t)from;
  m.u.push.time = time;
  m.u.push.len = (uint8_t)std::strlen(text);
  std::memcpy(m.u.push.text, text, m.u.push.len);
  return vf(m, sender, floor);
}

static void feed(gw::Gateway &g, int from, const std::vector<uint8_t> &f) { g.on_frame(from, f.data(), f.size()); }

static bool vdec(const Sent &s, tc::VoiceMsg &m) { return tc::voice_decode(s.bytes.data(), s.bytes.size(), m); }

// The frames of one voice type from index `from` on.
static std::vector<tc::VoiceMsg> vframes(const FakeIO &io, tc::VType t, size_t from = 0, std::vector<int> *to = nullptr) {
  std::vector<tc::VoiceMsg> v;
  for (size_t i = from; i < io.frames.size(); i++) {
    tc::VoiceMsg m;
    if (vdec(io.frames[i], m) && m.type == t) {
      v.push_back(m);
      if (to) to->push_back(io.frames[i].to);
    }
  }
  return v;
}

static int count_lines(const FakeIO &io, const char *sub, size_t from = 0) {
  int n = 0;
  for (size_t i = from; i < io.lines.size(); i++) n += io.lines[i].find(sub) != std::string::npos ? 1 : 0;
  return n;
}

static std::vector<uint8_t> unb64(const std::string &s) {
  std::vector<uint8_t> out;
  uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else continue;  // padding
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)(acc >> bits & 0xFF));
    }
  }
  return out;
}

static std::string field_d(const std::string &line) {
  size_t a = line.find("\"d\":\"");
  if (a == std::string::npos) return "";
  a += 5;
  return line.substr(a, line.find('"', a) - a);
}

static const char *kHelloVoice = "{\"cmd\":\"hello\",\"voice\":1}";

// The ACK a panel would send for a request frame the gateway sent (the old way, by seq and type).
static std::vector<uint8_t> ack_for(const Sent &s) {
  tc::Msg req;
  CHECK(tc::decode(s.bytes.data(), s.bytes.size(), req));
  tc::Msg a;
  a.type = tc::MsgType::Ack;
  a.sender = tc::kSenderUpstairs;
  a.u.ack.seq = req.seq;
  a.u.ack.type = (uint8_t)req.type;
  uint8_t buf[16];
  size_t n = tc::encode(a, buf, sizeof(buf));
  return std::vector<uint8_t>(buf, buf + n);
}

static void test_voice_hello() {
  FakeIO io;
  io.drops = 7;
  io.dpeer = false;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1000);
  line(g, "{\"cmd\":\"hello\"}");
  CHECK(io.lines.size() == 1 && io.lines[0] ==
        "{\"boot\":false,\"mac\":\"aa:bb:cc:dd:ee:ff\",\"channel\":1,\"fw\":\"gateway-c6 0.7\",\"voice\":1,\"drops\":7,\"down_peer\":false}");
  CHECK(!std::strcmp(gw::kFirmware, "gateway-c6 0.7"));
  // Without "voice":1 from the bridge, the C6 is "No Pi with voice": BEGIN gets VOICE_ABORT reason 7, three times
  // (0, 100, 250 ms) and the resent BEGINs inside that burst do not start another.
  io.clear();
  feed(g, gw::kUpstairs, v_begin(7, 0, 0));
  run(g, io, 150);
  feed(g, gw::kUpstairs, v_begin(7, 0, 0));
  run(g, io, 400);
  std::vector<int> to;
  auto ab = vframes(io, tc::VType::Abort, 0, &to);
  CHECK(ab.size() == 3 && ab[0].u.abort.xfer == 7 && ab[0].u.abort.reason == 7 && ab[0].floor == 0 && to[0] == gw::kUpstairs);
  CHECK(io.lines.empty() && g.voice_session(0).state == gw::VoiceSession::Idle);
  CHECK(io.frames[0].at == 1000 && io.frames[1].at == 1100 && io.frames[2].at == 1250);
  // The same from downstairs: the abort goes straight back to downstairs.
  io.clear();
  feed(g, gw::kDownstairs, v_begin(8, 1, 1));
  to.clear();
  ab = vframes(io, tc::VType::Abort, 0, &to);
  CHECK(ab.size() == 1 && ab[0].floor == 1 && to[0] == gw::kDownstairs && ab[0].u.abort.xfer == 8);
  run(g, io, 300);
  // A bridge hello with voice 1 turns voice on; a later hello without it turns it off again.
  io.clear();
  line(g, kHelloVoice);
  feed(g, gw::kUpstairs, v_begin(9, 0, 0));
  CHECK(count_lines(io, "voice_begin") == 1 && g.voice_session(0).state == gw::VoiceSession::Open);
  line(g, "{\"cmd\":\"hello\"}");
  io.clear();
  feed(g, gw::kUpstairs, v_begin(10, 0, 0));
  CHECK(count_lines(io, "voice_begin") == 0 && vframes(io, tc::VType::Abort).size() == 1);
}

static void test_voice_upload() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1100);
  line(g, kHelloVoice);
  io.clear();
  feed(g, gw::kUpstairs, v_begin(7, 0, 0));
  CHECK(io.lines.size() == 1 && io.lines[0] ==
        "{\"event\":\"voice_begin\",\"x\":7,\"f\":\"up\",\"purpose\":\"message\",\"codec\":2,\"cs\":474,\"via\":\"direct\"}");
  std::vector<int> to;
  auto acks = vframes(io, tc::VType::Ack, 0, &to);
  CHECK(acks.size() == 1 && acks[0].u.ack.xfer == 7 && acks[0].u.ack.state == 1 && acks[0].u.ack.base == 0 &&
        acks[0].u.ack.bits == 0 && acks[0].floor == 0 && to[0] == gw::kUpstairs);
  // A resent BEGIN answers with the ACK again and does not start over.
  feed(g, gw::kUpstairs, v_begin(7, 0, 0));
  CHECK(count_lines(io, "voice_begin") == 1 && vframes(io, tc::VType::Ack).size() == 2);
  // 20 chunks: one line each, under 512 bytes, the base64 is the chunk. A VOICE_ACK after every 8.
  size_t acks_before = vframes(io, tc::VType::Ack).size();
  for (int i = 0; i < 20; i++) {
    size_t l0 = io.lines.size();
    io.now += 30;
    feed(g, gw::kUpstairs, v_data(7, i, 0, 0, -311 + i, 41));
    CHECK(io.lines.size() == l0 + 1);
    const std::string &ln = io.lines.back();
    CHECK(ln.size() < 512 && ln.size() > 300);
    char head[96];
    std::snprintf(head, sizeof(head), "{\"event\":\"voice\",\"x\":7,\"f\":\"up\",\"i\":%d,\"p\":%d,\"s\":41,\"d\":\"", i, -311 + i);
    CHECK(ln.compare(0, std::strlen(head), head) == 0);
    CHECK(unb64(field_d(ln)) == chunk_bytes(i) && field_d(ln).size() == 316);
  }
  acks = vframes(io, tc::VType::Ack);
  CHECK(acks.size() == acks_before + 2);  // after chunks 8 and 16
  CHECK(acks[acks_before].u.ack.base == 8 && acks[acks_before].u.ack.bits == 0 && acks[acks_before].u.ack.state == 1);
  CHECK(acks[acks_before + 1].u.ack.base == 16);
  // END: voice_end, then VOICE_ACK state 2. A resent END gets state 2 again and no second line.
  size_t l0 = io.lines.size();
  feed(g, gw::kUpstairs, v_end(7, 20, 9480, 0, 0, 1));
  CHECK(io.lines.size() == l0 + 1 && io.lines.back() == "{\"event\":\"voice_end\",\"x\":7,\"f\":\"up\",\"n\":20,\"ns\":9480,\"sp\":1}");
  acks = vframes(io, tc::VType::Ack);
  CHECK(acks.back().u.ack.state == 2 && acks.back().u.ack.base == 20 && g.voice_session(0).state == gw::VoiceSession::Done);
  size_t na = acks.size();
  feed(g, gw::kUpstairs, v_end(7, 20, 9480, 0, 0, 1));
  CHECK(io.lines.size() == l0 + 1 && vframes(io, tc::VType::Ack).size() == na + 1 &&
        vframes(io, tc::VType::Ack).back().u.ack.state == 2);
  // A late DATA for the finished transfer prints nothing; an END for an xfer nobody knows is state 3.
  feed(g, gw::kUpstairs, v_data(7, 3, 0, 0));
  CHECK(io.lines.size() == l0 + 1);
  feed(g, gw::kUpstairs, v_end(9, 5, 100, 0, 0));
  CHECK(vframes(io, tc::VType::Ack).back().u.ack.xfer == 9 && vframes(io, tc::VType::Ack).back().u.ack.state == 3);
  CHECK(io.lines.size() == l0 + 1);
  // The 2 s idle timer does not run on a finished session.
  run(g, io, 3000);
  CHECK(io.lines.size() == l0 + 1);
  // A DATA for an xfer with no session at all is dropped without a word.
  size_t f0 = io.frames.size();
  feed(g, gw::kUpstairs, v_data(33, 0, 0, 0));
  CHECK(io.lines.size() == l0 + 1 && io.frames.size() == f0);
}

static void test_voice_gaps() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1200);
  line(g, kHelloVoice);
  feed(g, gw::kUpstairs, v_begin(4, 0, 0));
  io.clear();
  const int order[] = {0, 1, 2, 4, 5};
  for (int i : order) {
    io.now += 29;
    feed(g, gw::kUpstairs, v_data(4, i, 0, 0));
  }
  run(g, io, 79);
  CHECK(vframes(io, tc::VType::Ack).empty());  // not yet: 80 ms after the last DATA
  run(g, io, 2);
  auto acks = vframes(io, tc::VType::Ack);
  CHECK(acks.size() == 1 && acks[0].u.ack.base == 3 && acks[0].u.ack.bits == 6 && acks[0].u.ack.state == 1);
  run(g, io, 300);
  CHECK(vframes(io, tc::VType::Ack).size() == 1);  // one ACK per burst, not one per tick
  // END with the gap: state 1 with the same bitmap, no voice_end.
  feed(g, gw::kUpstairs, v_end(4, 6, 2844, 0, 0));
  acks = vframes(io, tc::VType::Ack);
  CHECK(acks.size() == 2 && acks[1].u.ack.state == 1 && acks[1].u.ack.base == 3 && count_lines(io, "voice_end") == 0);
  // The resent chunk fills it: voice_end and state 2 follow at once (no wait for the next END).
  feed(g, gw::kUpstairs, v_data(4, 3, 0, 0));
  CHECK(count_lines(io, "voice_end") == 1 && vframes(io, tc::VType::Ack).back().u.ack.state == 2);
  CHECK(g.voice_session(0).state == gw::VoiceSession::Done && g.voice_session(0).received() == 6);
  // A duplicate chunk prints nothing but the panel gets the bitmap again (it did not see it).
  FakeIO io2;
  gw::Gateway g2(io2, "aa:bb:cc:dd:ee:ff", 0x1210);
  line(g2, kHelloVoice);
  feed(g2, gw::kUpstairs, v_begin(5, 0, 0));
  feed(g2, gw::kUpstairs, v_data(5, 0, 0, 0));
  io2.clear();
  feed(g2, gw::kUpstairs, v_data(5, 0, 0, 0));
  CHECK(io2.lines.empty());
  run(g2, io2, 100);
  CHECK(vframes(io2, tc::VType::Ack).size() == 1 && vframes(io2, tc::VType::Ack)[0].u.ack.base == 1);
  // A chunk idx past BEGIN's max_chunks, and a mu-law chunk with pred != 0, are dropped and counted.
  feed(g2, gw::kUpstairs, v_data(5, 104, 0, 0));
  CHECK(io2.lines.empty() && g2.status().voice_bad == 1);
  FakeIO io3;
  gw::Gateway g3(io3, "aa:bb:cc:dd:ee:ff", 0x1220);
  line(g3, kHelloVoice);
  io3.clear();
  feed(g3, gw::kUpstairs, v_begin(6, 0, 0, 1, 128));
  CHECK(io3.lines.size() == 1 && count_lines(io3, "\"codec\":1,\"cs\":237") == 1);
  io3.clear();
  feed(g3, gw::kUpstairs, v_data(6, 0, 0, 0, 5, 0));
  feed(g3, gw::kUpstairs, v_data(6, 1, 0, 0, 0, 7));
  CHECK(io3.lines.empty() && g3.status().voice_bad == 2);
  feed(g3, gw::kUpstairs, v_data(6, 2, 0, 0, 0, 0));
  CHECK(io3.lines.size() == 1);
  // Bad frames: a downstairs frame claiming floor 0, a truncated frame.
  std::vector<uint8_t> bad = v_data(5, 7, 1, 1);
  bad[5] = 0;  // floor 0 from the downstairs MAC
  uint32_t vb = g2.status().voice_bad;
  feed(g2, gw::kDownstairs, bad);
  std::vector<uint8_t> shortf = v_end(5, 1, 1, 0, 0);
  shortf.pop_back();
  feed(g2, gw::kUpstairs, shortf);
  CHECK(g2.status().voice_bad == vb + 2);
}

static void test_voice_two_floors() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1300);
  line(g, kHelloVoice);
  io.clear();
  // Both floors at once, upstairs direct, downstairs direct, interleaved.
  feed(g, gw::kUpstairs, v_begin(7, 0, 0));
  feed(g, gw::kDownstairs, v_begin(7, 1, 1, 2, 104, 1));  // the same xfer number on the other floor: a different session
  CHECK(count_lines(io, "\"f\":\"up\",\"purpose\":\"message\"") == 1 && count_lines(io, "\"f\":\"down\",\"purpose\":\"dictation\"") == 1);
  CHECK(count_lines(io, "\"f\":\"down\",\"purpose\":\"dictation\",\"codec\":2,\"cs\":474,\"via\":\"direct\"") == 1);
  std::vector<int> to;
  auto acks = vframes(io, tc::VType::Ack, 0, &to);
  CHECK(acks.size() == 2 && to[0] == gw::kUpstairs && acks[0].floor == 0 && to[1] == gw::kDownstairs && acks[1].floor == 1);
  for (int i = 0; i < 12; i++) {
    feed(g, gw::kUpstairs, v_data(7, i, 0, 0));
    feed(g, gw::kDownstairs, v_data(7, i, 1, 1));
  }
  CHECK(count_lines(io, "\"f\":\"up\",\"i\"") == 12 && count_lines(io, "\"f\":\"down\",\"i\"") == 12);
  CHECK(g.voice_session(0).received() == 12 && g.voice_session(1).received() == 12);
  // The ACK after 8 chunks goes to the floor's own panel.
  to.clear();
  acks = vframes(io, tc::VType::Ack, 0, &to);
  int up8 = 0, down8 = 0;
  for (size_t i = 0; i < acks.size(); i++) {
    if (acks[i].u.ack.base == 8) {
      (acks[i].floor == 0 ? up8 : down8)++;
      CHECK(to[i] == (acks[i].floor == 0 ? gw::kUpstairs : gw::kDownstairs));
    }
  }
  CHECK(up8 == 1 && down8 == 1);
  // One floor finishing leaves the other floor's session alone.
  feed(g, gw::kDownstairs, v_end(7, 12, 5000, 1, 1));
  CHECK(g.voice_session(1).state == gw::VoiceSession::Done && g.voice_session(0).state == gw::VoiceSession::Open);
  feed(g, gw::kUpstairs, v_data(7, 12, 0, 0));
  CHECK(count_lines(io, "\"f\":\"up\",\"i\":12") == 1);
  // Downstairs relayed by upstairs: sender upstairs, header floor 1. Replies go to upstairs with floor 1; the
  // lines say via upstairs. A new BEGIN replaces the floor's old session (and the Pi hears the old one is over).
  io.clear();
  feed(g, gw::kDownstairs, v_begin(9, 1, 1));  // Done session: replaced quietly
  CHECK(count_lines(io, "voice_abort") == 0);
  io.clear();
  feed(g, gw::kUpstairs, v_begin(11, 0, 1));
  CHECK(count_lines(io, "\"event\":\"voice_abort\",\"x\":9,\"f\":\"down\",\"reason\":\"lost\"") == 1);
  CHECK(count_lines(io, "\"event\":\"voice_begin\",\"x\":11,\"f\":\"down\",\"purpose\":\"message\",\"codec\":2,\"cs\":474,\"via\":\"upstairs\"") == 1);
  to.clear();
  acks = vframes(io, tc::VType::Ack, 0, &to);
  CHECK(acks.size() == 1 && acks[0].floor == 1 && acks[0].u.ack.xfer == 11 && to[0] == gw::kUpstairs);
  CHECK(io.frames[0].bytes[2] == tc::kSenderGateway);
  feed(g, gw::kUpstairs, v_data(11, 0, 0, 1));
  CHECK(count_lines(io, "\"x\":11,\"f\":\"down\",\"i\":0") == 1);
  feed(g, gw::kUpstairs, v_end(11, 1, 400, 0, 1));
  CHECK(count_lines(io, "\"event\":\"voice_end\",\"x\":11,\"f\":\"down\",\"n\":1,\"ns\":400,\"sp\":1") == 1);
  to.clear();
  acks = vframes(io, tc::VType::Ack, 0, &to);
  CHECK(acks.back().u.ack.state == 2 && to.back() == gw::kUpstairs && acks.back().floor == 1);
  // The upstairs panel's own floor-0 session is untouched through all this.
  CHECK(g.voice_session(0).state == gw::VoiceSession::Open && g.voice_session(0).xfer == 7);
}

static void test_voice_timeout_abort() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1400);
  line(g, kHelloVoice);
  feed(g, gw::kDownstairs, v_begin(3, 1, 1));
  feed(g, gw::kDownstairs, v_data(3, 0, 1, 1));
  io.clear();
  run(g, io, 1990);
  CHECK(count_lines(io, "voice_abort") == 0 && vframes(io, tc::VType::Abort).empty());
  uint32_t t0 = io.now;
  run(g, io, 20);
  CHECK(io.lines.size() >= 1 && count_lines(io, "{\"event\":\"voice_abort\",\"x\":3,\"f\":\"down\",\"reason\":\"timeout\"}") == 1);
  run(g, io, 400);
  std::vector<int> to;
  auto ab = vframes(io, tc::VType::Abort, 0, &to);
  CHECK(ab.size() == 3 && ab[0].u.abort.reason == 4 && ab[0].u.abort.xfer == 3 && ab[0].floor == 1 && to[0] == gw::kDownstairs);
  CHECK(io.frames[0].at - t0 <= 20 && io.frames[1].at - io.frames[0].at == 100 && io.frames[2].at - io.frames[0].at == 250);
  CHECK(g.voice_session(1).state == gw::VoiceSession::Idle);
  // A late chunk is dropped; an END is state 3.
  size_t l0 = io.lines.size();
  feed(g, gw::kDownstairs, v_data(3, 1, 1, 1));
  CHECK(io.lines.size() == l0);
  feed(g, gw::kDownstairs, v_end(3, 2, 900, 1, 1));
  CHECK(vframes(io, tc::VType::Ack).back().u.ack.state == 3);
  // A session that is kept alive by its frames does not time out (a slow talker), and held chunks count as life.
  FakeIO io2;
  gw::Gateway g2(io2, "aa:bb:cc:dd:ee:ff", 0x1410);
  line(g2, kHelloVoice);
  feed(g2, gw::kUpstairs, v_begin(2, 0, 0));
  for (int i = 0; i < 100; i++) {
    run(g2, io2, 29);
    feed(g2, gw::kUpstairs, v_data(2, i, 0, 0));
  }
  CHECK(g2.voice_session(0).state == gw::VoiceSession::Open && count_lines(io2, "voice_abort") == 0);
  // The panel's own ABORT (three copies): the Pi hears once, then the session is gone.
  feed(g2, gw::kUpstairs, v_abort(2, 1, 0, 0));
  feed(g2, gw::kUpstairs, v_abort(2, 1, 0, 0));
  feed(g2, gw::kUpstairs, v_abort(2, 1, 0, 0));
  CHECK(count_lines(io2, "{\"event\":\"voice_abort\",\"x\":2,\"f\":\"up\",\"reason\":\"user\"}") == 1);
  CHECK(g2.voice_session(0).state == gw::VoiceSession::Idle);
  // An ABORT for a finished transfer (CANCEL while the Pi recognises it) still reaches the Pi, once.
  feed(g2, gw::kUpstairs, v_begin(3, 0, 0));
  feed(g2, gw::kUpstairs, v_data(3, 0, 0, 0));
  feed(g2, gw::kUpstairs, v_end(3, 1, 400, 0, 0));
  CHECK(g2.voice_session(0).state == gw::VoiceSession::Done);
  for (int i = 0; i < 3; i++) feed(g2, gw::kUpstairs, v_abort(3, 1, 0, 0));
  CHECK(count_lines(io2, "{\"event\":\"voice_abort\",\"x\":3,\"f\":\"up\",\"reason\":\"user\"}") == 1);
  // An ABORT for another xfer does nothing.
  feed(g2, gw::kUpstairs, v_begin(4, 0, 0));
  size_t l1 = io2.lines.size();
  feed(g2, gw::kUpstairs, v_abort(9, 1, 0, 0));
  CHECK(io2.lines.size() == l1 && g2.voice_session(0).state == gw::VoiceSession::Open);
}

// The write gate: a chunk is written only if the port can take it and 1100 bytes more; otherwise it is a gap.
static void test_voice_gate() {
  FakeIO io;
  io.cap = 4096;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1500);
  line(g, kHelloVoice);
  feed(g, gw::kUpstairs, v_begin(7, 0, 0));
  io.clear();
  // The port is short of room (the Pi is not reading): the chunk is held, not marked, nothing is written.
  io.used = io.cap - 1000;
  feed(g, gw::kUpstairs, v_data(7, 0, 0, 0));
  CHECK(io.lines.empty() && g.voice_session(0).received() == 0 && g.status().voice_held == 1);
  feed(g, gw::kUpstairs, v_data(7, 1, 0, 0));
  run(g, io, 85);
  auto acks = vframes(io, tc::VType::Ack);
  CHECK(acks.size() == 1 && acks[0].u.ack.base == 0 && acks[0].u.ack.bits == 0);  // both chunks show as gaps
  // Room comes back: the panel's resend goes through and is marked.
  io.used = 0;
  feed(g, gw::kUpstairs, v_data(7, 0, 0, 0));
  feed(g, gw::kUpstairs, v_data(7, 1, 0, 0));
  CHECK(io.lines.size() == 2 && g.voice_session(0).received() == 2);
  // Exactly at the line: room for the line and 1100 more passes, one byte less does not.
  io.clear();
  FakeIO probe;
  gw::Gateway pg(probe, "aa:bb:cc:dd:ee:ff", 0x1510);
  line(pg, kHelloVoice);
  feed(pg, gw::kUpstairs, v_begin(7, 0, 0));
  probe.clear();
  feed(pg, gw::kUpstairs, v_data(7, 0, 0, 0));
  const int linelen = (int)probe.lines.back().size() + 1;
  CHECK(linelen < 512 + 1);
  FakeIO e;
  e.cap = 1 << 20;
  gw::Gateway eg(e, "aa:bb:cc:dd:ee:ff", 0x1520);
  line(eg, kHelloVoice);
  feed(eg, gw::kUpstairs, v_begin(7, 0, 0));
  e.clear();
  e.used = e.cap - (linelen + 1100 - 1);
  feed(eg, gw::kUpstairs, v_data(7, 0, 0, 0));
  CHECK(e.lines.empty());
  e.used = e.cap - (linelen + 1100);
  feed(eg, gw::kUpstairs, v_data(7, 0, 0, 0));
  CHECK(e.lines.size() == 1);
  // A stream into a port nobody reads: voice stops while at least 600 bytes (the control reserve; in fact
  // 1100) are always free, and no line is ever cut.
  FakeIO s;
  s.cap = 4096;
  gw::Gateway sg(s, "aa:bb:cc:dd:ee:ff", 0x1530);
  line(sg, kHelloVoice);
  feed(sg, gw::kUpstairs, v_begin(7, 0, 0));
  int written = 0, min_free = 1 << 20;
  for (int i = 0; i < 100; i++) {
    s.now += 29;
    size_t before = s.lines.size();
    feed(sg, gw::kUpstairs, v_data(7, i, 0, 0));
    if (s.lines.size() > before) written++;
    sg.tick();
    min_free = std::min(min_free, s.cap - s.used);
  }
  CHECK(written >= 5 && written <= 8);  // "about 7 voice lines" fit in the 4096-byte buffer
  CHECK(min_free >= 1100 && !s.overflow);
  CHECK(sg.status().voice_held == (uint32_t)(100 - written));
  CHECK(sg.voice_session(0).received() == written);
}

// H2 (check finding): a doorbell notice and a Lucy reply arrive whole while voice streams.
static void test_voice_streaming_h2() {
  FakeIO io;
  io.cap = 4096;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1600);
  line(g, kHelloVoice);
  feed(g, gw::kUpstairs, v_begin(3, 0, 0));
  io.clear();
  int notices_sent = 0, lucy_sent = 0, lucy_acked = 0, idx = 0;
  int min_free = 1 << 20;
  size_t max_line = 0;
  // 3 s at the panel's pace (a chunk every 29 ms); the port drains 9 bytes a millisecond (9 KB/s, under the
  // 13 KB/s of the stream), so it backs up and the gate has to work.
  for (uint32_t t = 0; t < 3000; t++) {
    io.now++;
    g.tick();
    io.used = std::max(0, io.used - 9);
    if (t % 29 == 0 && idx < 100) feed(g, gw::kUpstairs, v_data(3, idx++, 0, 0));
    if (t % 400 == 100) {
      char txt[16];
      std::snprintf(txt, sizeof(txt), "KNOCK%d", notices_sent);
      std::vector<uint8_t> nf = notice_frame(tc::kSenderUpstairs, (uint16_t)(100 + notices_sent), 8, 1, (int16_t)(notices_sent + 1),
                                             1793000000u + notices_sent, txt);
      notices_sent++;
      g.on_frame(gw::kUpstairs, nf.data(), nf.size());
    }
    if (t % 700 == 250) {
      char l[96];
      std::snprintf(l, sizeof(l), "{\"type\":\"lucy\",\"id\":%d,\"out\":1,\"since\":%d}", 500 + lucy_sent, 1000 + lucy_sent);
      lucy_sent++;
      line(g, l);
    }
    // The panel ACKs a Lucy frame 12 ms after it was sent.
    if (!io.frames.empty()) {
      for (size_t i = 0; i < io.frames.size(); i++) {
        const Sent &f = io.frames[i];
        tc::Msg m;
        if (f.at + 12 == io.now && tc::decode(f.bytes.data(), f.bytes.size(), m) && m.type == tc::MsgType::Lucy) {
          std::vector<uint8_t> a = ack_for(f);
          g.on_frame(gw::kUpstairs, a.data(), a.size());
          lucy_acked++;
        }
      }
    }
    min_free = std::min(min_free, io.cap - io.used);
  }
  for (const std::string &l : io.lines) max_line = std::max(max_line, l.size());
  CHECK(!io.overflow && min_free >= 0);
  CHECK(max_line < 512);
  CHECK(notices_sent >= 7 && lucy_sent >= 4);
  // Every notice and every Lucy reply is there, whole (a complete JSON object on its own line).
  int notices = 0, lucy_ok = 0, bad_lines = 0;
  for (const std::string &l : io.lines) {
    gw::JObject o;
    if (!gw::json_parse_object(l.c_str(), l.size(), o)) {
      bad_lines++;
      continue;
    }
    const gw::JField *ev = o.get("event"), *ty = o.get("type");
    if (ev && !std::strcmp(ev->s, "notice")) notices++;
    if (ty && !std::strcmp(ty->s, "lucy") && o.get("acked")) lucy_ok++;
  }
  CHECK(bad_lines == 0);
  CHECK(notices == notices_sent);
  CHECK(lucy_ok == lucy_sent && lucy_acked == lucy_sent);
  CHECK(g.status().voice_lines > 20 && g.status().voice_held > 0);
  // The same into a port that has stopped altogether: the control lines still fit, whole.
  FakeIO st;
  st.cap = 4096;
  gw::Gateway sg(st, "aa:bb:cc:dd:ee:ff", 0x1610);
  line(sg, kHelloVoice);
  feed(sg, gw::kUpstairs, v_begin(3, 0, 0));
  for (int i = 0; i < 60; i++) {
    st.now += 29;
    feed(sg, gw::kUpstairs, v_data(3, i, 0, 0));
  }
  CHECK(st.cap - st.used >= 1100);
  std::vector<uint8_t> nf = notice_frame(tc::kSenderUpstairs, 900, 8, 1, 4321, 1793000000u, "RING");
  sg.on_frame(gw::kUpstairs, nf.data(), nf.size());
  line(sg, "{\"type\":\"lucy\",\"id\":900,\"out\":0,\"since\":5}");
  std::vector<uint8_t> a = ack_for(st.frames.back());
  sg.on_frame(gw::kUpstairs, a.data(), a.size());
  CHECK(!st.overflow && st.cap - st.used >= 0);
  CHECK(count_lines(st, "\"event\":\"notice\"") == 1 && count_lines(st, "\"type\":\"lucy\",\"id\":900,\"acked\":\"upstairs\"") == 1);
}

// Nothing is written to the Pi while a request waits for its ACK; the held chunks are gaps.
static void test_voice_hold_for_request() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1700);
  line(g, kHelloVoice);
  feed(g, gw::kUpstairs, v_begin(7, 0, 0));
  feed(g, gw::kUpstairs, v_data(7, 0, 0, 0));
  io.clear();
  line(g, "{\"type\":\"lucy\",\"id\":1,\"out\":1,\"since\":5}");
  Sent lucy = io.frames.back();
  CHECK(g.in_flight() == 1);
  feed(g, gw::kUpstairs, v_data(7, 1, 0, 0));
  CHECK(count_lines(io, "\"event\":\"voice\"") == 0 && g.voice_session(0).received() == 1 && g.status().voice_held == 1);
  std::vector<uint8_t> a = ack_for(lucy);
  g.on_frame(gw::kUpstairs, a.data(), a.size());
  CHECK(g.in_flight() == 0 && count_lines(io, "\"acked\":\"upstairs\"") == 1);
  feed(g, gw::kUpstairs, v_data(7, 1, 0, 0));
  CHECK(count_lines(io, "\"event\":\"voice\"") == 1 && g.voice_session(0).received() == 2);
  // A waiting voice_text slot does not hold the other floor's upload.
  line(g, "{\"type\":\"voice_text\",\"id\":2,\"x\":5,\"floor\":\"downstairs\",\"status\":0,\"fl\":0,\"t0\":\"hi\",\"t1\":\"\"}");
  CHECK(g.in_flight() == 1);
  feed(g, gw::kUpstairs, v_data(7, 2, 0, 0));
  CHECK(count_lines(io, "\"event\":\"voice\"") == 2);
  // END with all chunks in: even a held voice_end waits for the next tick, never lost.
  FakeIO io2;
  io2.cap = 4096;
  gw::Gateway g2(io2, "aa:bb:cc:dd:ee:ff", 0x1710);
  line(g2, kHelloVoice);
  feed(g2, gw::kUpstairs, v_begin(7, 0, 0));
  feed(g2, gw::kUpstairs, v_data(7, 0, 0, 0));
  io2.used = io2.cap - 300;  // not even the control reserve
  feed(g2, gw::kUpstairs, v_end(7, 1, 300, 0, 0));
  CHECK(count_lines(io2, "voice_end") == 0 && g2.voice_session(0).state == gw::VoiceSession::Open);
  CHECK(vframes(io2, tc::VType::Ack).back().u.ack.state == 1);
  io2.used = 0;
  run(g2, io2, 5);
  CHECK(count_lines(io2, "voice_end") == 1 && vframes(io2, tc::VType::Ack).back().u.ack.state == 2);
}

static void test_voice_text() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1800);
  line(g, kHelloVoice);
  // Downstairs, direct: the text goes straight to downstairs, split in two fields and joined, flags carried.
  feed(g, gw::kDownstairs, v_begin(7, 1, 1));
  feed(g, gw::kDownstairs, v_data(7, 0, 1, 1));
  feed(g, gw::kDownstairs, v_end(7, 1, 400, 1, 1));
  io.clear();
  line(g, "{\"type\":\"voice_text\",\"id\":50,\"x\":7,\"floor\":\"downstairs\",\"status\":0,\"fl\":1,"
          "\"t0\":\"Dinner is ready and the dog\",\"t1\":\" went out\"}");
  CHECK(io.lines.empty() && io.frames.size() == 1 && io.frames[0].to == gw::kDownstairs && g.in_flight() == 1);
  tc::VoiceMsg m;
  CHECK(vdec(io.frames[0], m) && m.type == tc::VType::Text && m.floor == 1 && m.sender == tc::kSenderGateway &&
        m.u.text.xfer == 7 && m.u.text.status == 0 && m.u.text.flags == 1 && m.u.text.len == 36 &&
        std::string(m.u.text.text) == "Dinner is ready and the dog went out");
  uint16_t seq = m.seq;
  // Retries on the usual schedule (0, 150, 400, 750, 1150), all the same frame.
  run(g, io, 1149);
  CHECK(io.frames.size() == 4);
  run(g, io, 1);
  CHECK(io.frames.size() == 5 && io.frames[4].at - io.frames[0].at == 1150 && io.frames[4].bytes == io.frames[0].bytes);
  // The GOT (kind 25, key xfer + status x 256) from downstairs finishes it, with the usual acked reply.
  io.now += 6;
  feed(g, gw::kDownstairs, v_got(25, tc::voice_text_key(7, 1), 0, 1, 1));  // a GOT for another status: ignored
  feed(g, gw::kDownstairs, v_got(25, tc::voice_text_key(8, 0), 0, 1, 1));  // another xfer: ignored
  feed(g, gw::kUpstairs, v_got(25, tc::voice_text_key(7, 0), 0, 0, 0));    // the other floor's slot: ignored
  CHECK(io.lines.empty() && g.in_flight() == 1);
  feed(g, gw::kDownstairs, v_got(25, tc::voice_text_key(7, 0), 0, 1, 1));
  char want[128];
  std::snprintf(want, sizeof(want), "{\"type\":\"voice_text\",\"id\":50,\"acked\":\"downstairs\",\"seq\":%u,\"ms\":%lu}", seq,
                (unsigned long)(io.now - io.frames[0].at));
  CHECK(io.lines.size() == 1 && io.lines[0] == want && g.in_flight() == 0);
  CHECK(!std::strcmp(g.status().last, "vtext acked 1156ms"));
  // Give up after 1.5 s: the failed line names the floor.
  io.clear();
  line(g, "{\"type\":\"voice_text\",\"id\":51,\"x\":7,\"floor\":\"downstairs\",\"status\":4,\"fl\":0,\"t0\":\"\",\"t1\":\"\"}");
  run(g, io, 1600);
  CHECK(io.frames.size() == 5 && io.lines.size() == 1 && g.in_flight() == 0);
  std::snprintf(want, sizeof(want), "{\"type\":\"voice_text\",\"id\":51,\"failed\":\"downstairs\",\"seq\":%u}",
                (unsigned)(seq + 1));
  CHECK(io.lines[0] == want);
  CHECK(!std::strcmp(g.status().last, "vtext failed"));
  // Latest wins per floor: an interim status 6 gives way to the final text; its GOT can never clear the final.
  io.clear();
  line(g, "{\"type\":\"voice_text\",\"id\":60,\"x\":9,\"floor\":\"downstairs\",\"status\":6,\"t0\":\"\"}");
  line(g, "{\"type\":\"voice_text\",\"id\":61,\"x\":9,\"floor\":\"upstairs\",\"status\":0,\"fl\":0,\"t0\":\"up text\"}");
  CHECK(io.lines.empty() && g.in_flight() == 2 && io.frames[1].to == gw::kUpstairs);
  line(g, "{\"type\":\"voice_text\",\"id\":62,\"x\":9,\"floor\":\"downstairs\",\"status\":0,\"fl\":0,\"t0\":\"final\"}");
  CHECK(io.lines.size() == 1 && g.in_flight() == 2);
  tc::VoiceMsg interim, fin;
  CHECK(vdec(io.frames[0], interim) && interim.u.text.status == 6 && interim.u.text.len == 0);
  std::snprintf(want, sizeof(want), "{\"type\":\"voice_text\",\"id\":60,\"superseded\":true,\"seq\":%u}", (unsigned)interim.seq);
  CHECK(io.lines[0] == want);
  CHECK(vdec(io.frames.back(), fin) && fin.u.text.status == 0 && std::string(fin.u.text.text) == "final");
  feed(g, gw::kDownstairs, v_got(25, tc::voice_text_key(9, 6), 0, 1, 1));  // the interim's GOT arrives late
  CHECK(io.lines.size() == 1 && g.in_flight() == 2);
  feed(g, gw::kDownstairs, v_got(25, tc::voice_text_key(9, 0), 1, 1, 1));  // result 1 (gone) still acks the request
  CHECK(io.lines.size() == 2 && count_lines(io, "\"id\":62,\"acked\":\"downstairs\"") == 1);
  // The upstairs slot is independent: its GOT finishes it.
  feed(g, gw::kUpstairs, v_got(25, tc::voice_text_key(9, 0), 0, 0, 0));
  CHECK(count_lines(io, "\"id\":61,\"acked\":\"upstairs\"") == 1 && g.in_flight() == 0);
  // A text frame for a session that came by upstairs (the relay) is sent to upstairs, floor 1.
  io.clear();
  feed(g, gw::kUpstairs, v_begin(21, 0, 1));
  feed(g, gw::kUpstairs, v_data(21, 0, 0, 1));
  feed(g, gw::kUpstairs, v_end(21, 1, 400, 0, 1));
  io.clear();
  line(g, "{\"type\":\"voice_text\",\"id\":70,\"x\":21,\"floor\":\"downstairs\",\"status\":0,\"fl\":0,\"t0\":\"via the relay\"}");
  CHECK(io.frames.size() == 1 && io.frames[0].to == gw::kUpstairs && vdec(io.frames[0], m) && m.floor == 1 &&
        m.type == tc::VType::Text);
  feed(g, gw::kUpstairs, v_got(25, tc::voice_text_key(21, 0), 0, 0, 1));  // the relayed GOT: header floor 1
  CHECK(count_lines(io, "\"id\":70,\"acked\":\"downstairs\"") == 1);
  // ... and one that came direct goes direct, even after a relayed one before it.
  feed(g, gw::kDownstairs, v_begin(22, 1, 1));
  io.clear();
  line(g, "{\"type\":\"voice_text\",\"id\":71,\"x\":22,\"floor\":\"downstairs\",\"status\":5}");
  CHECK(io.frames.size() == 1 && io.frames[0].to == gw::kDownstairs && vdec(io.frames[0], m) && m.u.text.status == 5 &&
        m.u.text.len == 0);
  // Rejections, each one reply with the id.
  FakeIO r;
  gw::Gateway rg(r, "aa:bb:cc:dd:ee:ff", 0x1810);
  line(rg, "{\"type\":\"voice_text\",\"id\":1,\"x\":0,\"floor\":\"upstairs\",\"status\":0,\"t0\":\"a\"}");
  line(rg, "{\"type\":\"voice_text\",\"id\":2,\"x\":256,\"floor\":\"upstairs\",\"status\":0,\"t0\":\"a\"}");
  line(rg, "{\"type\":\"voice_text\",\"id\":3,\"x\":1,\"floor\":\"attic\",\"status\":0,\"t0\":\"a\"}");
  line(rg, "{\"type\":\"voice_text\",\"id\":4,\"x\":1,\"floor\":\"upstairs\",\"status\":7,\"t0\":\"a\"}");
  line(rg, "{\"type\":\"voice_text\",\"id\":5,\"x\":1,\"floor\":\"upstairs\",\"status\":0,\"fl\":2,\"t0\":\"a\"}");
  line(rg, "{\"type\":\"voice_text\",\"id\":6,\"x\":1,\"floor\":\"upstairs\",\"status\":0}");
  line(rg, "{\"type\":\"voice_text\",\"id\":7,\"x\":1,\"floor\":\"upstairs\",\"status\":2,\"t0\":\"not empty\"}");
  line(rg, "{\"type\":\"voice_text\",\"id\":8,\"x\":1,\"floor\":\"upstairs\",\"status\":0,\"t0\":"
           "\"0123456789012345678901234567890123456789012345678901234567890\"}");
  line(rg, "{\"type\":\"voice_text\",\"id\":9,\"x\":1,\"floor\":\"upstairs\",\"status\":0,\"t0\":5}");
  line(rg, "{\"type\":\"voice_text\",\"x\":1,\"floor\":\"upstairs\",\"status\":0,\"t0\":\"a\"}");
  CHECK(rg.status().rejected == 10 && r.frames.empty() && r.lines.size() == 10 && rg.in_flight() == 0);
  CHECK(r.lines[0] == "{\"type\":\"voice_text\",\"id\":1,\"rejected\":\"x must be 1 to 255\"}");
  CHECK(r.lines[2] == "{\"type\":\"voice_text\",\"id\":3,\"rejected\":\"floor must be upstairs or downstairs\"}");
  CHECK(r.lines[3] == "{\"type\":\"voice_text\",\"id\":4,\"rejected\":\"status must be 0 to 6\"}");
  CHECK(r.lines[6] == "{\"type\":\"voice_text\",\"id\":7,\"rejected\":\"text must be empty unless status is 0\"}");
  CHECK(r.lines[9] == "{\"type\":\"voice_text\",\"rejected\":\"id must be a positive whole number\"}");
  // A full 120-character text (60 + 60) is one frame of exactly 130 bytes.
  r.clear();
  std::string t60(60, 'a'), u60(60, 'b');
  line(rg, ("{\"type\":\"voice_text\",\"id\":11,\"x\":2,\"floor\":\"upstairs\",\"status\":0,\"fl\":0,\"t0\":\"" + t60 + "\",\"t1\":\"" + u60 + "\"}").c_str());
  CHECK(r.frames.size() == 1 && r.frames[0].bytes.size() == 130 && r.frames[0].to == gw::kUpstairs);
}

static void test_voice_dismiss() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1900);
  line(g, kHelloVoice);
  io.clear();
  line(g, "{\"type\":\"voice_ack\",\"id\":80,\"floor\":\"upstairs\",\"msg\":3735928559}");
  CHECK(io.lines.empty() && io.frames.size() == 1 && io.frames[0].to == gw::kUpstairs);
  tc::VoiceMsg m;
  CHECK(vdec(io.frames[0], m) && m.type == tc::VType::Dismiss && m.floor == 0 && m.u.dismiss.id == 3735928559u &&
        io.frames[0].bytes.size() == 10);
  uint16_t seq = m.seq;
  // A second Got it for the same floor inside 1.5 s is refused (busy), not superseded; the other floor is free.
  line(g, "{\"type\":\"voice_ack\",\"id\":81,\"floor\":\"upstairs\",\"msg\":123}");
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"type\":\"voice_ack\",\"id\":81,\"rejected\":\"busy\"}" && io.frames.size() == 1);
  CHECK(g.status().rejected == 0);
  line(g, "{\"type\":\"voice_ack\",\"id\":82,\"floor\":\"downstairs\",\"msg\":124}");
  CHECK(io.lines.size() == 1 && io.frames.size() == 2 && g.in_flight() == 2);
  // The panel's GOT (kind 29, key = the id): removed, or gone when it was not there.
  io.now += 9;
  feed(g, gw::kUpstairs, v_got(29, 3735928558u, 0, 0, 0));  // another id: ignored
  CHECK(io.lines.size() == 1);
  feed(g, gw::kUpstairs, v_got(29, 3735928559u, 0, 0, 0));
  char want[160];
  std::snprintf(want, sizeof(want), "{\"type\":\"voice_ack\",\"id\":80,\"acked\":\"upstairs\",\"result\":\"removed\",\"seq\":%u,\"ms\":9}",
                (unsigned)seq);
  CHECK(io.lines.size() == 2 && io.lines[1] == want && !std::strcmp(g.status().last, "vack acked 9ms"));
  // The slot is free again: the retry the bridge makes 1.6 s later goes through.
  line(g, "{\"type\":\"voice_ack\",\"id\":83,\"floor\":\"upstairs\",\"msg\":123}");
  CHECK(io.frames.size() == 3 && g.in_flight() == 2);
  feed(g, gw::kUpstairs, v_got(29, 123, 1, 0, 0));
  CHECK(count_lines(io, "\"id\":83,\"acked\":\"upstairs\",\"result\":\"gone\"") == 1);
  // The downstairs dismiss goes by upstairs until downstairs has been heard directly, then direct.
  CHECK(io.frames[1].to == gw::kUpstairs && vdec(io.frames[1], m) && m.floor == 1);
  feed(g, gw::kUpstairs, v_got(29, 124, 0, 0, 1));  // relayed GOT: header floor 1
  CHECK(count_lines(io, "\"id\":82,\"acked\":\"downstairs\",\"result\":\"removed\"") == 1 && g.in_flight() == 0);
  feed(g, gw::kDownstairs, v_ping(5, 1, 1));
  io.clear();
  line(g, "{\"type\":\"voice_ack\",\"id\":84,\"floor\":\"downstairs\",\"msg\":125}");
  CHECK(io.frames.size() == 1 && io.frames[0].to == gw::kDownstairs);
  run(g, io, 1600);
  CHECK(count_lines(io, "{\"type\":\"voice_ack\",\"id\":84,\"failed\":\"downstairs\",\"seq\":") == 1 && io.frames.size() == 5);
  // Rejections.
  FakeIO r;
  gw::Gateway rg(r, "aa:bb:cc:dd:ee:ff", 0x1910);
  line(rg, "{\"type\":\"voice_ack\",\"id\":1,\"floor\":\"upstairs\",\"msg\":0}");
  line(rg, "{\"type\":\"voice_ack\",\"id\":2,\"floor\":\"upstairs\",\"msg\":4294967296}");
  line(rg, "{\"type\":\"voice_ack\",\"id\":3,\"floor\":\"upstairs\"}");
  line(rg, "{\"type\":\"voice_ack\",\"id\":4,\"floor\":\"cellar\",\"msg\":5}");
  line(rg, "{\"type\":\"voice_ack\",\"id\":5,\"msg\":5}");
  CHECK(rg.status().rejected == 5 && r.frames.empty() && r.lines.size() == 5);
  CHECK(r.lines[0] == "{\"type\":\"voice_ack\",\"id\":1,\"rejected\":\"msg must fit a u32 and not be 0\"}");
  line(rg, "{\"type\":\"voice_ack\",\"id\":6,\"floor\":\"upstairs\",\"msg\":4294967295}");
  CHECK(r.frames.size() == 1);
}

static void test_voice_push_ping() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1A00);
  // Before the bridge says voice: refused (result 2), nothing printed.
  feed(g, gw::kDownstairs, v_push(99, 0, 1793000000u, "Dinner is ready", 1, 1));
  CHECK(io.lines.empty());
  std::vector<int> to;
  auto gots = vframes(io, tc::VType::Got, 0, &to);
  CHECK(gots.size() == 1 && gots[0].u.got.kind == 28 && gots[0].u.got.key == 99 && gots[0].u.got.result == 2 &&
        gots[0].floor == 1 && to[0] == gw::kDownstairs);
  line(g, kHelloVoice);
  io.clear();
  // The receiving panel is downstairs, the writer upstairs. Escapes for " and \ only.
  feed(g, gw::kDownstairs, v_push(99, 0, 1793000000u, "Dinner \"now\" \\ ok", 1, 1));
  CHECK(io.lines.size() == 1 && io.lines[0] ==
        "{\"event\":\"voice_msg\",\"id\":99,\"to\":\"downstairs\",\"from\":\"upstairs\",\"time\":1793000000,"
        "\"text\":\"Dinner \\\"now\\\" \\\\ ok\"}");
  to.clear();
  gots = vframes(io, tc::VType::Got, 0, &to);
  CHECK(gots.size() == 1 && gots[0].u.got.kind == 28 && gots[0].u.got.key == 99 && gots[0].u.got.result == 0 &&
        to[0] == gw::kDownstairs);
  gw::JObject o;
  CHECK(gw::json_parse_object(io.lines[0].c_str(), io.lines[0].size(), o) && !std::strcmp(o.get("text")->s, "Dinner \"now\" \\ ok"));
  // Sent again (our GOT was lost): the GOT again, no second line.
  feed(g, gw::kDownstairs, v_push(99, 0, 1793000000u, "Dinner \"now\" \\ ok", 1, 1));
  CHECK(io.lines.size() == 1 && vframes(io, tc::VType::Got).size() == 2);
  // By the relay (sender upstairs, floor 1): the GOT goes back the same way.
  io.clear();
  feed(g, gw::kUpstairs, v_push(100, 0, 0, "relayed", 0, 1));
  to.clear();
  gots = vframes(io, tc::VType::Got, 0, &to);
  CHECK(io.lines.size() == 1 && gots.size() == 1 && to[0] == gw::kUpstairs && gots[0].floor == 1 && gots[0].u.got.key == 100);
  // The receiving panel is upstairs, the writer downstairs.
  io.clear();
  feed(g, gw::kUpstairs, v_push(101, 1, 5, "from below", 0, 0));
  CHECK(io.lines.size() == 1 && count_lines(io, "\"id\":101,\"to\":\"upstairs\",\"from\":\"downstairs\",\"time\":5") == 1);
  // The dedup ring holds the last 8 ids; a ninth pushes the first out.
  for (uint32_t id = 200; id < 208; id++) feed(g, gw::kUpstairs, v_push(id, 1, 5, "x", 0, 0));
  io.clear();
  feed(g, gw::kUpstairs, v_push(207, 1, 5, "x", 0, 0));
  CHECK(io.lines.empty());
  feed(g, gw::kUpstairs, v_push(101, 1, 5, "from below", 0, 0));  // pushed out by the eight after it
  CHECK(io.lines.size() == 1);
  // No room: nothing printed and no GOT, so the panel sends it again.
  FakeIO full;
  full.cap = 4096;
  gw::Gateway fg(full, "aa:bb:cc:dd:ee:ff", 0x1A10);
  line(fg, kHelloVoice);
  full.clear();
  full.used = full.cap - 100;
  feed(fg, gw::kUpstairs, v_push(300, 1, 5, "hello", 0, 0));
  CHECK(full.lines.empty() && full.frames.empty());
  full.used = 0;
  feed(fg, gw::kUpstairs, v_push(300, 1, 5, "hello", 0, 0));
  CHECK(full.lines.size() == 1 && full.frames.size() == 1);
  // A long text keeps the line under 512 bytes: 120 characters, all of them escapes.
  FakeIO big;
  gw::Gateway bg(big, "aa:bb:cc:dd:ee:ff", 0x1A20);
  line(bg, kHelloVoice);
  big.clear();
  feed(bg, gw::kUpstairs, v_push(400, 1, 4294967295u, std::string(120, '"').c_str(), 0, 0));
  CHECK(big.lines.size() == 1 && big.lines[0].size() < 512 && big.lines[0].size() > 320);
  CHECK(gw::json_parse_object(big.lines[0].c_str(), big.lines[0].size(), o) && std::strlen(o.get("text")->s) == 63);
  // PING: VOICE_GOT kind 30 straight back to downstairs, direct, only for a ping from downstairs.
  FakeIO p;
  gw::Gateway pg(p, "aa:bb:cc:dd:ee:ff", 0x1A30);
  feed(pg, gw::kDownstairs, v_ping(0xA5A5A5A5u, 1, 1));
  to.clear();
  gots = vframes(p, tc::VType::Got, 0, &to);
  CHECK(p.lines.empty() && gots.size() == 1 && gots[0].u.got.kind == 30 && gots[0].u.got.key == 0xA5A5A5A5u &&
        gots[0].u.got.result == 0 && gots[0].floor == 1 && to[0] == gw::kDownstairs);
  p.clear();
  feed(pg, gw::kUpstairs, v_ping(5, 0, 1));  // relayed or from upstairs: not answered
  feed(pg, gw::kUpstairs, v_ping(6, 0, 0));
  CHECK(p.frames.empty());
  // Frames the C6 should not see are ignored: an ACK, a TEXT, a DISMISS.
  tc::VoiceMsg a;
  a.type = tc::VType::Ack;
  a.u.ack = {1, 0, 0, 1};
  feed(pg, gw::kUpstairs, vf(a, 0, 0));
  tc::VoiceMsg d;
  d.type = tc::VType::Dismiss;
  d.u.dismiss.id = 5;
  feed(pg, gw::kUpstairs, vf(d, 0, 0));
  CHECK(p.frames.empty() && p.lines.empty());
}

// The old types are untouched by the voice code, in the middle of a stream (the old tests do the rest).
static void test_voice_old_types_unchanged() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1B00);
  line(g, kHelloVoice);
  feed(g, gw::kUpstairs, v_begin(7, 0, 0));
  io.clear();
  line(g, "{\"type\":\"mail_ack\",\"id\":32}");
  tc::Msg ma;
  CHECK(io.frames.size() == 1 && io.frames[0].to == gw::kUpstairs && tc::decode(io.frames[0].bytes.data(), io.frames[0].bytes.size(), ma) &&
        ma.type == tc::MsgType::MailAck);
  // A voice frame that is not from a known MAC, or whose sender byte does not match its MAC, is ignored.
  size_t f0 = io.frames.size();
  feed(g, -1, v_begin(8, 0, 0));
  feed(g, gw::kDownstairs, v_begin(8, 0, 0));  // sender byte 0 from the downstairs MAC
  CHECK(io.frames.size() == f0 && g.voice_session(0).xfer == 7);
  // in_flight counts the voice slots too.
  line(g, "{\"type\":\"voice_text\",\"id\":1,\"x\":1,\"floor\":\"upstairs\",\"status\":4}");
  line(g, "{\"type\":\"voice_ack\",\"id\":2,\"floor\":\"downstairs\",\"msg\":7}");
  CHECK(g.in_flight() == 3);
  run(g, io, 1600);
  CHECK(g.in_flight() == 0 && count_lines(io, "\"failed\":") == 3);
}

// Field fix 2026-11-20 (firmware/docs/voice-notes/WORKLOG.md): the voice hello, the ping answer gap and the air cap.
static void test_voice_air_safety() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x1C00);
  // The hello: at the first tick, then every 2 minutes, to upstairs only, VOICE_GOT kind 30 floor 0 with
  // header seq 0, key 1 from 0.6 (takes mu-law and 255 chunks; 0.5 sent key 0); the requests' seq numbers
  // are not moved by it.
  run(g, io, 1);
  CHECK(io.hellos.size() == 1 && io.frames.empty());
  tc::VoiceMsg h;
  CHECK(tc::voice_decode(io.hellos[0].bytes.data(), io.hellos[0].bytes.size(), h));
  CHECK(h.type == tc::VType::Got && h.u.got.kind == 30 && h.u.got.key == tc::kVoiceHelloKeyMulaw && h.u.got.result == 0 &&
        h.floor == 0 && h.sender == tc::kSenderGateway && h.seq == 0 && io.hellos[0].to == gw::kUpstairs);
  CHECK(tc::voice_relay_frame(io.hellos[0].bytes.data(), io.hellos[0].bytes.size(), tc::kSenderGateway, tc::kSenderUpstairs) < 0);
  run(g, io, tc::kVoiceHelloEveryMs - 2);
  CHECK(io.hellos.size() == 1);
  run(g, io, 2);
  CHECK(io.hellos.size() == 2);
  run(g, io, 10u * 60u * 1000u);
  CHECK(io.hellos.size() == 7);  // ten minutes: 5 more
  for (const Sent &s : io.hellos) CHECK(s.to == gw::kUpstairs);
  CHECK(g.status().voice_hellos == 7);
  line(g, "{\"type\":\"lucy\",\"id\":1,\"out\":1,\"since\":1793000400}");
  CHECK(io.frames.size() == 1 && io.frames[0].bytes[3] == 0x00 && io.frames[0].bytes[4] == 0x1C);  // seq 0x1C00
  run(g, io, 1600);
  io.clear();
  // Downstairs' pings: one answer per 250 ms at most, whatever the panel does.
  for (int i = 0; i < 10; i++) {
    feed(g, gw::kDownstairs, v_ping(100u + (uint32_t)i, 1, 1));
    io.now += 25;
  }
  CHECK(vframes(io, tc::VType::Got).size() == 1);
  io.now += 250;
  feed(g, gw::kDownstairs, v_ping(200, 1, 1));
  CHECK(vframes(io, tc::VType::Got).size() == 2);
  // The air cap: 200 PUSHes in one millisecond get 25 GOTs (the burst), the rest are held and counted;
  // a second later 50 more fit. The TComm frames are not capped.
  io.now += 5000;
  io.clear();
  uint32_t capped = g.status().voice_capped;
  for (uint32_t id = 1; id <= 200; id++) feed(g, gw::kUpstairs, v_push(1000 + id, 1, 5, "x", 0, 0));
  CHECK(vframes(io, tc::VType::Got).size() == tc::kVoiceGwBurst);
  CHECK(g.status().voice_capped - capped == 200 - tc::kVoiceGwBurst);
  io.now += 1000;
  for (uint32_t id = 1; id <= 200; id++) feed(g, gw::kUpstairs, v_push(5000 + id, 1, 5, "x", 0, 0));
  CHECK(vframes(io, tc::VType::Got).size() == tc::kVoiceGwBurst + tc::kVoiceGwBurst);  // refilled to the burst, no more
  line(g, "{\"type\":\"lucy\",\"id\":2,\"out\":0,\"since\":1793000500}");
  CHECK(!io.frames.empty() && io.frames.back().bytes[1] == (uint8_t)tc::MsgType::Lucy);
  // The cap itself: 50 a second with bursts of 25.
  tc::VoiceRateCap c(50, 25);
  int n = 0;
  for (int i = 0; i < 100; i++) n += c.take(7) ? 1 : 0;
  CHECK(n == 25);
  n = 0;
  for (uint32_t t = 8; t < 10008; t++) n += c.take(t) ? 1 : 0;  // 10 s, asking every millisecond
  CHECK(n >= 499 && n <= 501);
  CHECK(c.take(10008 + 4000000000u));  // a long gap (or the counter wrapping) refills, never overflows
}

// ---------------------------------------------------------------------------------------------
// 0.6 (field fix 2026-11-21): mu-law, 255 chunks, 68 chunks a second
// ---------------------------------------------------------------------------------------------

// A mu-law chunk: pred 0 and step 0 on the wire (the gateway refuses anything else for codec 1).
static std::vector<uint8_t> v_mu(int x, int idx, int sender, int floor) { return v_data(x, idx, sender, floor, 0, 0); }

// One panel's upload as a real panel does it: first sends in order every 14.7 ms (68 a second) and never
// 32 or more past base, the bitmap of every VOICE_ACK read, an unacknowledged chunk sent again after
// 150 ms, END (resent every 200 ms) once every first send is out, until state 2.
struct MuPanel {
  int x, sender, floor, n;
  std::vector<char> acked;
  std::vector<uint32_t> last;
  int next = 0, base = 0, sends = 0, resends = 0;
  bool done = false;
  uint32_t t_end = 0;
  size_t seen_frames = 0;
  MuPanel(int xfer, int snd, int fl, int chunks)
      : x(xfer), sender(snd), floor(fl), n(chunks), acked((size_t)chunks, 0), last((size_t)chunks, 0) {}
  void read_acks(const FakeIO &io) {
    for (; seen_frames < io.frames.size(); seen_frames++) {
      tc::VoiceMsg m;
      if (!vdec(io.frames[seen_frames], m) || m.type != tc::VType::Ack || m.floor != floor || m.u.ack.xfer != x) continue;
      if (m.u.ack.state == 2) done = true;
      for (int i = 0; i < m.u.ack.base && i < n; i++) acked[(size_t)i] = 1;
      for (int k = 0; k < 32; k++)
        if ((m.u.ack.bits >> k & 1u) && m.u.ack.base + k < n) acked[(size_t)(m.u.ack.base + k)] = 1;
      base = m.u.ack.base;
    }
  }
  // Called every millisecond.
  void step(gw::Gateway &g, FakeIO &io, uint32_t t, uint32_t origin) {
    read_acks(io);
    const uint32_t at = t - origin;
    if (next < n && at >= (uint32_t)next * 14700u / 1000u && next < base + 32) {
      feed(g, sender, v_mu(x, next, sender, floor));
      last[(size_t)next] = t;
      sends++;
      next++;
      return;
    }
    if (done) return;
    // A resend round: the oldest unacknowledged chunk that has waited 150 ms (one a millisecond at most).
    for (int i = base; i < next; i++)
      if (!acked[(size_t)i] && t - last[(size_t)i] >= 150) {
        feed(g, sender, v_mu(x, i, sender, floor));
        last[(size_t)i] = t;
        sends++;
        resends++;
        return;
      }
    if (next == n && t - t_end >= 200) {
      feed(g, sender, v_end(x, n, n * 237, sender, floor));
      t_end = t;
    }
  }
};

static int count_data_lines(const FakeIO &io, const char *f, int idx) {
  char key[64];
  std::snprintf(key, sizeof(key), "\"f\":\"%s\",\"i\":%d,", f, idx);
  return count_lines(io, key);
}

// A 203-chunk mu-law message from each floor (and from both at once), streamed at 68 chunks a second,
// through a serial port, while notices and Lucy replies keep arriving: every chunk is written to the Pi
// once, whole and under 512 bytes; the notices and the Lucy replies are whole and complete; the air cap
// throttles nothing.
static void run_mulaw_message(bool up, bool down, int drain_per_ms) {
  FakeIO io;
  io.cap = 4096;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x2000);
  line(g, kHelloVoice);
  const int N = 203;
  const int nfloors = (up ? 1 : 0) + (down ? 1 : 0);
  MuPanel pu(21, tc::kSenderUpstairs, 0, N), pd(22, tc::kSenderDownstairs, 1, N);
  if (up) feed(g, gw::kUpstairs, v_begin(21, 0, 0, 1, 210));
  if (down) feed(g, gw::kDownstairs, v_begin(22, 1, 1, 1, 210));
  int notices = 0, lucy_sent = 0, lucy_acked = 0;
  const uint32_t origin = io.now;
  size_t lucy_seen = 0;
  for (uint32_t t = 0; t < 30000 && !((!up || pu.done) && (!down || pd.done)); t++) {
    io.now++;
    g.tick();
    io.used = std::max(0, io.used - drain_per_ms);
    if (up) pu.step(g, io, io.now, origin);
    if (down) pd.step(g, io, io.now, origin);
    if (t % 400 == 100) {
      char txt[16];
      std::snprintf(txt, sizeof(txt), "KNOCK%d", notices);
      std::vector<uint8_t> nf = notice_frame(tc::kSenderUpstairs, (uint16_t)(100 + notices), 8, 1, (int16_t)(notices + 1),
                                             1793000000u + (uint32_t)notices, txt);
      notices++;
      g.on_frame(gw::kUpstairs, nf.data(), nf.size());
    }
    if (t % 700 == 250) {
      char l[96];
      std::snprintf(l, sizeof(l), "{\"type\":\"lucy\",\"id\":%d,\"out\":1,\"since\":%d}", 700 + lucy_sent, 2000 + lucy_sent);
      lucy_sent++;
      line(g, l);
    }
    // The upstairs panel ACKs a Lucy frame as soon as it is sent.
    for (; lucy_seen < io.frames.size(); lucy_seen++) {
      const Sent &f = io.frames[lucy_seen];
      tc::Msg m;
      if (tc::decode(f.bytes.data(), f.bytes.size(), m) && m.type == tc::MsgType::Lucy) {
        std::vector<uint8_t> a = ack_for(f);
        g.on_frame(gw::kUpstairs, a.data(), a.size());
        lucy_acked++;
      }
    }
  }
  CHECK(!io.overflow);
  CHECK((!up || pu.done) && (!down || pd.done));
  size_t max_line = 0;
  for (const std::string &l : io.lines) max_line = std::max(max_line, l.size());
  CHECK(max_line < 512 && max_line > 340);
  const char *fl[2] = {"up", "down"};
  for (int f = 0; f < 2; f++) {
    if (!(f ? down : up)) continue;
    for (int i = 0; i < N; i++) CHECK(count_data_lines(io, fl[f], i) == 1);
    CHECK(count_lines(io, f ? "\"event\":\"voice_end\",\"x\":22,\"f\":\"down\",\"n\":203,\"ns\":48111,\"sp\":1"
                            : "\"event\":\"voice_end\",\"x\":21,\"f\":\"up\",\"n\":203,\"ns\":48111,\"sp\":1") == 1);
    CHECK(count_lines(io, f ? "\"event\":\"voice_begin\",\"x\":22,\"f\":\"down\",\"purpose\":\"message\",\"codec\":1,\"cs\":237"
                            : "\"event\":\"voice_begin\",\"x\":21,\"f\":\"up\",\"purpose\":\"message\",\"codec\":1,\"cs\":237") == 1);
    CHECK(g.voice_session(f).state == gw::VoiceSession::Done && g.voice_session(f).received() == N);
  }
  // Every other line parses whole; chunk payloads round-trip with pred 0 and step 0; every notice and Lucy
  // reply is there.
  int ok_notices = 0, lucy_ok = 0, bad_lines = 0, data_ok = 0, data_lines = 0;
  for (const std::string &l : io.lines) {
    if (l.find("\"event\":\"voice\",") != std::string::npos) {
      data_lines++;
      const int i = (int)std::strtol(l.c_str() + l.find("\"i\":") + 4, nullptr, 10);
      data_ok += unb64(field_d(l)) == chunk_bytes(i) && l.find("\"p\":0,\"s\":0,") != std::string::npos ? 1 : 0;
      continue;
    }
    gw::JObject o;
    if (!gw::json_parse_object(l.c_str(), l.size(), o)) {
      bad_lines++;
      continue;
    }
    const gw::JField *ev = o.get("event"), *ty = o.get("type");
    if (ev && !std::strcmp(ev->s, "notice")) ok_notices++;
    if (ty && !std::strcmp(ty->s, "lucy") && o.get("acked")) lucy_ok++;
  }
  CHECK(bad_lines == 0);
  CHECK(data_lines == data_ok && data_lines == nfloors * N);
  CHECK(notices >= 5 && ok_notices == notices);
  CHECK(lucy_sent >= 3 && lucy_ok == lucy_sent && lucy_acked == lucy_sent);
  CHECK(g.status().voice_capped == 0 && g.status().voice_bad == 0);
  if (drain_per_ms >= 60) CHECK(g.status().voice_lines == (uint32_t)(nfloors * N));  // a fast port: nothing held
  std::printf("  mu-law %s%s, port %d B/ms: %d chunks each, longest line %u, %d resends, %u held, %u ACKs, capped %u\n", up ? "up" : "",
              down ? "down" : "", drain_per_ms, N, (unsigned)max_line, pu.resends + pd.resends, (unsigned)g.status().voice_held,
              (unsigned)vframes(io, tc::VType::Ack).size(), (unsigned)g.status().voice_capped);
}

static void test_voice_mulaw_message() {
  run_mulaw_message(true, false, 60);  // upstairs, a port that keeps up (68 lines a second is about 27 KB/s)
  run_mulaw_message(false, true, 60);  // downstairs
  run_mulaw_message(true, true, 60);   // both at once
  run_mulaw_message(true, false, 20);  // a port slower than the stream: the gate holds chunks, the panel resends
  run_mulaw_message(false, true, 20);
  run_mulaw_message(true, true, 30);
  // The cap at its new size: 100 a second, bursts of 25.
  tc::VoiceRateCap c(tc::kVoiceGwPerS, tc::kVoiceGwBurst);
  CHECK(tc::kVoiceGwPerS == 100 && tc::kVoiceGwBurst == 25);
  int n = 0;
  for (int i = 0; i < 100; i++) n += c.take(7) ? 1 : 0;
  CHECK(n == 25);
  n = 0;
  for (uint32_t t = 8; t < 10008; t++) n += c.take(t) ? 1 : 0;  // 10 s, asking every millisecond
  CHECK(n >= 999 && n <= 1001);
}

// The top of the index space: 255 chunks, out of order, END 255; chunk 255 does not exist.
static void test_voice_index_space_255() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x2100);
  line(g, kHelloVoice);
  feed(g, gw::kUpstairs, v_begin(9, 0, 0, 1, 255));
  CHECK(g.voice_session(0).max_chunks == 255 && count_lines(io, "\"codec\":1,\"cs\":237") == 1);
  for (int w = 0; w < 255; w += 32)
    for (int i = std::min(w + 31, 254); i >= w; i--) {
      io.now += 5;
      feed(g, gw::kUpstairs, v_mu(9, i, 0, 0));
      if (i % 7 == 0) feed(g, gw::kUpstairs, v_mu(9, i, 0, 0));
    }
  CHECK(g.voice_session(0).received() == 255 && g.voice_session(0).base() == 255);
  CHECK(count_data_lines(io, "up", 0) == 1 && count_data_lines(io, "up", 254) == 1);
  // A frame with idx 255 is not a frame at all (voice_decode refuses it).
  tc::VoiceMsg m;
  m.type = tc::VType::Data;
  m.u.data.xfer = 9;
  m.u.data.idx = 254;
  m.u.data.len = 237;
  std::vector<uint8_t> bad = vf(m, 0, 0);
  bad[7] = 255;  // idx
  uint32_t vb = g.status().voice_bad;
  feed(g, gw::kUpstairs, bad);
  CHECK(g.status().voice_bad == vb + 1 && g.voice_session(0).received() == 255);
  feed(g, gw::kUpstairs, v_end(9, 255, 255 * 237, 0, 0));
  CHECK(count_lines(io, "\"event\":\"voice_end\",\"x\":9,\"f\":\"up\",\"n\":255,\"ns\":60435,\"sp\":1") == 1);
  tc::VoiceMsg a = vframes(io, tc::VType::Ack).back();
  CHECK(a.u.ack.state == 2 && a.u.ack.base == 255 && a.u.ack.bits == 0);
  // A BEGIN for 255 chunks of ADPCM is taken too.
  feed(g, gw::kDownstairs, v_begin(3, 1, 1, 2, 255));
  CHECK(g.voice_session(1).state == gw::VoiceSession::Open && g.voice_session(1).max_chunks == 255);
}

// The ACK schedule at mu-law's pace: a chunk that fills a gap gets its ACK 80 ms later (base moved), and a
// trickle of resends cannot hold an ACK back past 300 ms from the first DATA.
static void test_voice_ack_pace() {
  FakeIO io;
  gw::Gateway g(io, "aa:bb:cc:dd:ee:ff", 0x2200);
  line(g, kHelloVoice);
  feed(g, gw::kUpstairs, v_begin(5, 0, 0, 1, 210));
  const int first[] = {0, 1, 3};
  for (int i : first) {
    io.now += 15;
    feed(g, gw::kUpstairs, v_mu(5, i, 0, 0));
  }
  run(g, io, 81);
  CHECK(vframes(io, tc::VType::Ack).size() == 2);  // BEGIN's, then the gap's
  CHECK(vframes(io, tc::VType::Ack).back().u.ack.base == 2 && vframes(io, tc::VType::Ack).back().u.ack.bits == 2);
  feed(g, gw::kUpstairs, v_mu(5, 2, 0, 0));  // fills the gap
  run(g, io, 79);
  CHECK(vframes(io, tc::VType::Ack).size() == 2);
  run(g, io, 2);
  CHECK(vframes(io, tc::VType::Ack).size() == 3 && vframes(io, tc::VType::Ack).back().u.ack.base == 4);
  // A trickle: a duplicate every 50 ms never lets 80 ms pass, yet the ACK comes within 300 ms.
  io.clear();
  uint32_t t0 = io.now;
  feed(g, gw::kUpstairs, v_mu(5, 3, 0, 0));
  uint32_t ack_t = 0;
  for (int k = 0; k < 20 && !ack_t; k++) {
    run(g, io, 50);
    if (!vframes(io, tc::VType::Ack).empty()) ack_t = io.now - t0;
    else feed(g, gw::kUpstairs, v_mu(5, 3, 0, 0));
  }
  CHECK(ack_t > 0 && ack_t <= 350);
  // Nothing to report: the due flag is dropped, and a later gap gets the full 80 ms.
  io.clear();
  feed(g, gw::kUpstairs, v_mu(5, 4, 0, 0));
  run(g, io, 200);
  CHECK(vframes(io, tc::VType::Ack).empty());
  feed(g, gw::kUpstairs, v_mu(5, 6, 0, 0));
  run(g, io, 79);
  CHECK(vframes(io, tc::VType::Ack).empty());
  run(g, io, 2);
  CHECK(vframes(io, tc::VType::Ack).size() == 1 && vframes(io, tc::VType::Ack)[0].u.ack.base == 5);
}

int main() {
  test_json();
  test_frames();
  test_acks();
  test_retries();
  test_types_apart();
  test_one_reply_each();
  test_checks();
  test_update();
  test_notices();
  test_rooms();
  test_phone();
  test_voice_hello();
  test_voice_upload();
  test_voice_gaps();
  test_voice_two_floors();
  test_voice_timeout_abort();
  test_voice_gate();
  test_voice_streaming_h2();
  test_voice_hold_for_request();
  test_voice_text();
  test_voice_dismiss();
  test_voice_push_ping();
  test_voice_old_types_unchanged();
  test_voice_air_safety();
  test_voice_mulaw_message();
  test_voice_index_space_255();
  test_voice_ack_pace();
  std::printf("gateway: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
