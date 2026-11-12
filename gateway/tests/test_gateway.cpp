// Host tests for the ESP32-C6 gateway's logic (gateway/gateway-c6/src/gw): the brief's frames
// byte for byte, ACK matching, TComm's retry schedule, supersede, the two types apart, and one
// final reply per request id. Built and run by firmware/tools/build_sim.sh.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
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
};

struct FakeIO : gw::GatewayIO {
  uint32_t now = 1000;
  std::vector<Sent> frames;
  std::vector<std::string> lines;
  uint32_t now_ms() override { return now; }
  void send_frame(const uint8_t *d, size_t n) override { frames.push_back({now, std::vector<uint8_t>(d, d + n)}); }
  void reply(const char *l) override { lines.push_back(l); }
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
      {"{\"cmd\":\"hello\"}", "{\"boot\":false,\"mac\":\"aa:bb:cc:dd:ee:ff\",\"channel\":1,\"fw\":\"gateway-c6 0.3\"}"},
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
  CHECK(io.lines.size() == 1 && io.lines[0] == "{\"boot\":true,\"mac\":\"aa:bb:cc:dd:ee:ff\",\"channel\":1,\"fw\":\"gateway-c6 0.3\"}");
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
  const char *words[] = {"knock", "arrow_urgent", "arrow_mild", "mail", "dog_out", "status", "note"};
  for (int k = 1; k <= 7; k++) {
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
  // Malformed: a kind of 0 or 8, a bad floor, a cut frame, trailing bytes: ignored, no ACK.
  std::vector<uint8_t> f = notice_frame(tc::kSenderUpstairs, 410, 1, 0, 0, 1, "ab");
  std::vector<uint8_t> bad = f;
  bad[5] = 0;
  send(gw::kUpstairs, bad);
  bad = f;
  bad[5] = 8;
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
  std::printf("gateway: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
