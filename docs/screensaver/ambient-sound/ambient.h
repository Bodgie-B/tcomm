// A SAVED CONCEPT, not built into the panel firmware (Mike, 2026-11-05: "Save the concept of the
// sounds, but don't implement. I think they would be distracting after the initial curiosity wears
// off."). See README.md beside it.
//
// The screensaver's quiet ambient sound (screensaver brief section 11, stage 11): the camp's
// stream, the wind, the rain, the fire's crackle and the odd bird, made as it plays from filtered
// noise and a few sine voices (no recordings, as the brief asks). Mono 16-bit at kAmbientRate, in
// blocks of any length; every level glides toward its target, so a change never clicks. Plain C++:
// render_samples.cpp renders it to WAV files for listening.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tc {

constexpr uint32_t kAmbientRate = 44100;  // the panel's I2S rate (core/synth.h kSynthRate)

// What should be heard, each 0 to 1. The screensaver sets these from its scene.
struct AmbientMix {
  float stream = 0;  // the camp's stream (always there beside the camp)
  float wind = 0;    // a breeze at a little, gusts and a whistle at 1
  float rain = 0;
  float fire = 0;    // the crackle, a few pops and a low roar
  float birds = 0;   // how often a bird sings (by day; 1 is about every 8 s)
  float master = 0;  // all of it: 0 is silence (the screensaver's fades)
};

class AmbientSynth {
 public:
  explicit AmbientSynth(uint32_t seed = 1);
  void render(int16_t *out, size_t frames, const AmbientMix &target);
  // True once the master level has glided to nothing (the stream can stop).
  bool silent() const { return master_ < 1e-4f && target_master_ <= 0.f; }

  struct Biquad {
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bandpass(float hz, float q);
    void lowpass(float hz, float q);
    void highpass(float hz, float q);
    float run(float x) {
      const float y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };

 private:
  struct Resonator {  // one of the stream's babbling voices
    Biquad f;
    float hz = 800, to_hz = 800, amp = 0, to_amp = 0;
    int left = 0;  // samples until it picks a new pitch and loudness
  };
  struct Bubble {
    float phase = 0, hz = 0, rise = 0, amp = 0, decay = 0;
    int left = 0;
  };
  struct Note {  // a bird's note
    float f0, f1, vib;  // start and end pitch, vibrato depth (Hz)
    int start, len;     // in samples from the song's start
  };

  float noise();
  float uniform();
  void stepParams();  // every kParamStep samples: drifting pitches, gusts, new events
  void startSong();

  uint32_t rng_;
  // Levels now (gliding) and asked for.
  float stream_ = 0, wind_ = 0, rain_ = 0, fire_ = 0, birds_ = 0, master_ = 0;
  float target_master_ = 0;
  int step_left_ = 0;

  // The stream.
  Biquad base_lo_, base_hi_;
  Resonator res_[4];
  Bubble bub_[4];
  // The wind.
  float brown_ = 0, gust_ = 0.5f, to_gust_ = 0.5f;
  int gust_left_ = 0;
  Biquad wind_lo_, whistle_;
  // The rain.
  Biquad rain_hiss_, rain_low_, drop_hp_;
  float drop_env_ = 0;
  // The fire.
  Biquad crack_hp_, roar_lp_, fire_hiss_;
  float crack_env_ = 0;
  float pop_phase_ = 0, pop_hz_ = 0, pop_amp_ = 0;
  // The birds.
  Note song_[12];
  int song_notes_ = 0, song_pos_ = -1, song_gap_ = 0;
  float song_amp_ = 0, bird_phase_ = 0, vib_phase_ = 0;
};

}  // namespace tc
