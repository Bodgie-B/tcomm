// A small test frame for the voice-notes host tests (tests/test_voice_*.cpp), owned by P0.
// Each piece adds its own tests/test_voice_<piece>.cpp with VTEST blocks; tests/test_voice_main.cpp
// runs them all. Built by tools/build_sim.sh as test_voice.exe:
//   g++ -std=c++17 -Wall -Wextra -I tcomm/src -I ../gateway/gateway-c6/src tests/test_voice_*.cpp
//       tcomm/src/core/*.cpp [../gateway/gateway-c6/src/gw/voice_session.cpp] -o test_voice.exe
//   test_voice.exe tests/vectors
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "app/controller.h"

namespace vt {

using Fn = void (*)();
struct Reg {
  Reg(const char *name, Fn fn);
};
void check(bool ok, const char *expr, const char *file, int line);
const char *vectors_dir();  // argv[1] of test_voice.exe, default "tests/vectors"

// Golden vector files (tools/voice_vectors.py): 16-bit samples, and chunks.
struct Chunk {
  int idx;
  int pred;
  int step;
  std::vector<uint8_t> data;
};
bool read_samples(const char *name, std::vector<int16_t> &out);
bool read_chunks(const char *name, std::vector<Chunk> &out);

// Stub hooks: a tc::Platform with a settable clock that records every voice frame sent and every
// sound played, and keeps the saved inbox. Pieces override what they need (voice_rec_begin, ...).
class StubPlatform : public tc::Platform {
 public:
  struct Sent {
    uint8_t to;
    std::vector<uint8_t> bytes;
  };
  uint32_t now = 1000;
  bool radio_ok = true;  // what radio_send_voice returns
  std::vector<Sent> voice_out, plain_out;
  std::vector<tc::Sound> played;
  tc::VoiceInboxData inbox;
  bool inbox_saved = false;
  uint32_t rnd = 12345;

  uint32_t mono_ms() override { return now; }
  tc::WallTime wall() override { return tc::WallTime(); }
  void set_wall(uint32_t, uint8_t) override {}
  int32_t utc_offset(uint32_t) override { return 0; }
  void radio_send(uint8_t to, const uint8_t *d, size_t n) override { plain_out.push_back({to, std::vector<uint8_t>(d, d + n)}); }
  void play(tc::Sound s, uint8_t) override { played.push_back(s); }
  void set_backlight(uint8_t) override {}
  uint32_t random32() override { return rnd = rnd * 1103515245u + 12345u; }
  bool radio_send_voice(uint8_t to, const uint8_t *d, size_t n) override {
    voice_out.push_back({to, std::vector<uint8_t>(d, d + n)});
    return radio_ok;
  }
  void save_voice_inbox(const tc::VoiceInboxData &d) override {
    inbox = d;
    inbox_saved = true;
  }
  bool load_voice_inbox(tc::VoiceInboxData &d) override {
    if (!inbox_saved) return false;
    d = inbox;
    return true;
  }
};

}  // namespace vt

#define VT_CAT2(a, b) a##b
#define VT_CAT(a, b) VT_CAT2(a, b)
// VTEST(name) { ...CHECK(...)... }  registers a test; names are printed when a check fails.
#define VTEST(name)                                       \
  static void name();                                     \
  static vt::Reg VT_CAT(vt_reg_, name)(#name, &name);     \
  static void name()
#define CHECK(e) vt::check((e), #e, __FILE__, __LINE__)
