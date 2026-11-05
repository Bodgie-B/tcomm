// Renders the ambient concept's sounds to WAV files for listening, and measures it (README.md).
//   g++ -O2 -std=c++17 render_samples.cpp ambient.cpp -o render_samples && ./render_samples out_dir
#include "ambient.h"
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
using namespace tc;
static void wav(const std::string& path, const std::vector<int16_t>& s) {
  FILE* f = fopen(path.c_str(), "wb");
  const uint32_t rate = kAmbientRate, bytes = (uint32_t)(s.size() * 2);
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
  fwrite("data", 1, 4, f); u32(bytes); fwrite(s.data(), 2, s.size(), f); fclose(f);
}
struct Clip { const char* name; AmbientMix mix; float secs; };
int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: render_samples out_dir\n");
    return 1;
  }
  std::string dir = argv[1];
  auto M = [](float st, float wi, float ra, float fi, float bi) { AmbientMix m; m.stream = st; m.wind = wi; m.rain = ra; m.fire = fi; m.birds = bi; m.master = 1; return m; };
  Clip clips[] = {
    {"1_stream", M(1, 0, 0, 0, 0), 20}, {"2_day_stream_birds_breeze", M(0.8f, 0.2f, 0, 0, 1), 30},
    {"3_evening_fire", M(0.55f, 0.12f, 0, 0.8f, 0), 25}, {"4_wind", M(0.35f, 1, 0, 0, 0), 25},
    {"5_rain", M(0.35f, 0.15f, 0.7f, 0, 0), 20}, {"6_storm", M(0.2f, 0.9f, 1, 0, 0), 20},
    {"7_fire_alone", M(0, 0, 0, 1, 0), 15}, {"8_birds_alone", M(0, 0, 0, 0, 1), 30},
  };
  printf("sizeof(AmbientSynth) = %zu bytes\n", sizeof(AmbientSynth));
  for (const Clip& c : clips) {
    AmbientSynth syn(12345);
    std::vector<int16_t> s((size_t)(c.secs * kAmbientRate));
    // Start from silence, as the screensaver does: the master fades in over the first second.
    AmbientMix m = c.mix;
    size_t done = 0;
    int peak = 0, maxjump = 0;
    double sq = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (done < s.size()) {
      const size_t n = s.size() - done < 512 ? s.size() - done : 512;
      syn.render(s.data() + done, n, m);
      done += n;
    }
    auto t1 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < s.size(); ++i) {
      if (abs(s[i]) > peak) peak = abs(s[i]);
      if (i && abs(s[i] - s[i - 1]) > maxjump) maxjump = abs(s[i] - s[i - 1]);
      sq += (double)s[i] * s[i];
    }
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    printf("%-28s peak %5.1f dBFS, rms %5.1f dBFS, biggest step %5d, %.3f us a sample on this PC\n", c.name,
           20 * log10(peak / 32768.0 + 1e-9), 10 * log10(sq / s.size() / (32768.0 * 32768.0) + 1e-12), maxjump, us / s.size());
    wav(dir + "/" + c.name + ".wav", s);
  }
  // Silence: master 0 gives zeros.
  AmbientSynth syn(1); AmbientMix z; std::vector<int16_t> q(4096); syn.render(q.data(), q.size(), z);
  int nz = 0; for (int16_t v : q) nz += v != 0;
  printf("master 0: %d non-zero samples of %zu\n", nz, q.size());
  return 0;
}
