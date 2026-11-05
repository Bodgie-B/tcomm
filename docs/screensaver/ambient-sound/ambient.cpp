// The screensaver's ambient sound. See ambient.h. Everything is made sample by sample from a
// noise source, a handful of filters and a few sine voices: no buffers, no allocation (the object
// is under 1 KB), so it costs no PSRAM and no PSRAM bandwidth on the panel.
#include "ambient.h"
#include <math.h>

namespace tc {

namespace {

constexpr float kFs = (float)kAmbientRate;
constexpr float kTwoPi = 6.28318530718f;
constexpr int kParamStep = 32;                    // drifting pitches and events, this often (0.7 ms)
constexpr float kGlide = 1.0f / (0.8f * kFs);     // levels glide to their targets in about 0.8 s
constexpr float kOut = 0.5f * 32767.f;            // the mix's full scale: about -6 dBFS before the limiter

// A gentle limiter (a rational tanh): unity near zero, never past 1.
inline float soft(float x) {
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float glide(float v, float to, float k) { return v + (to - v) * k; }

// A sine good enough for water and birdsong, far cheaper than sinf on the panel (several voices
// per sample): x in [0, 2 pi).
inline float fastSin(float x) {
  if (x > 3.14159265f) x -= kTwoPi;
  const float ax = x < 0.f ? -x : x;
  float y = 1.27323954f * x - 0.40528473f * x * ax;
  const float ay = y < 0.f ? -y : y;
  return 0.225f * (y * ay - y) + y;
}

}  // namespace

// RBJ's cookbook filters.
void AmbientSynth::Biquad::bandpass(float hz, float q) {
  const float w = kTwoPi * hz / kFs, alpha = sinf(w) / (2.f * q), a0 = 1.f + alpha;
  b0 = alpha / a0;
  b1 = 0.f;
  b2 = -alpha / a0;
  a1 = -2.f * cosf(w) / a0;
  a2 = (1.f - alpha) / a0;
}

void AmbientSynth::Biquad::lowpass(float hz, float q) {
  const float w = kTwoPi * hz / kFs, alpha = sinf(w) / (2.f * q), c = cosf(w), a0 = 1.f + alpha;
  b0 = (1.f - c) / 2.f / a0;
  b1 = (1.f - c) / a0;
  b2 = b0;
  a1 = -2.f * c / a0;
  a2 = (1.f - alpha) / a0;
}

void AmbientSynth::Biquad::highpass(float hz, float q) {
  const float w = kTwoPi * hz / kFs, alpha = sinf(w) / (2.f * q), c = cosf(w), a0 = 1.f + alpha;
  b0 = (1.f + c) / 2.f / a0;
  b1 = -(1.f + c) / a0;
  b2 = b0;
  a1 = -2.f * c / a0;
  a2 = (1.f - alpha) / a0;
}

AmbientSynth::AmbientSynth(uint32_t seed) : rng_(seed ? seed : 1) {
  base_lo_.bandpass(450.f, 0.6f);
  base_hi_.highpass(2600.f, 0.7f);
  for (Resonator &r : res_) {
    r.hz = r.to_hz = 400.f + 1000.f * uniform();
    r.f.bandpass(r.hz, 7.f);
  }
  wind_lo_.lowpass(380.f, 0.6f);
  whistle_.bandpass(700.f, 24.f);
  rain_hiss_.bandpass(5000.f, 0.5f);
  rain_low_.lowpass(700.f, 0.7f);
  drop_hp_.highpass(3000.f, 0.7f);
  crack_hp_.highpass(2500.f, 0.7f);
  roar_lp_.lowpass(140.f, 0.7f);
  fire_hiss_.bandpass(1500.f, 0.7f);
  song_gap_ = (int)(kFs * 3.f);
}

float AmbientSynth::uniform() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return (float)(rng_ >> 8) * (1.f / 16777216.f);
}

float AmbientSynth::noise() { return 2.f * uniform() - 1.f; }

// A bird sings: a chickadee's "fee-bee", a quick trill, or a short warble, at some distance.
void AmbientSynth::startSong() {
  const float u = uniform();
  int t = 0, n = 0;
  auto add = [&](float f0, float f1, float vib, float ms, float gap_ms) {
    if (n >= 12) return;
    const int len = (int)(ms * kFs / 1000.f);
    song_[n++] = Note{f0, f1, vib, t, len};
    t += len + (int)(gap_ms * kFs / 1000.f);
  };
  if (u < 0.4f) {  // fee-bee
    add(3950.f, 3900.f, 0.f, 320.f, 70.f);
    add(3450.f, 3300.f, 0.f, 280.f, 0.f);
  } else if (u < 0.7f) {  // a trill
    const int notes = 6 + (int)(uniform() * 6.f);
    const float f = 4300.f + 900.f * uniform();
    for (int i = 0; i < notes; ++i) add(f + 300.f, f - 400.f, 0.f, 38.f, 24.f);
  } else {  // a warble
    const int notes = 3 + (int)(uniform() * 3.f);
    for (int i = 0; i < notes; ++i) {
      const float f = 2800.f + 1400.f * uniform();
      add(f, f * (0.9f + 0.2f * uniform()), 70.f, 80.f + 80.f * uniform(), 40.f + 60.f * uniform());
    }
  }
  song_notes_ = n;
  song_pos_ = 0;
  song_amp_ = 0.16f + 0.18f * uniform();  // nearer or further off: clear over the stream, never close
}

void AmbientSynth::stepParams() {
  // The stream's babble: each voice wanders to a new pitch and loudness every 40 to 160 ms.
  for (Resonator &r : res_) {
    r.left -= kParamStep;
    if (r.left <= 0) {
      r.to_hz = 380.f + 1150.f * uniform();
      const float a = uniform();
      r.to_amp = a * a;
      r.left = (int)(kFs * (0.04f + 0.12f * uniform()));
    }
    r.hz = glide(r.hz, r.to_hz, 0.08f);
    r.amp = glide(r.amp, r.to_amp, 0.12f);
    r.f.bandpass(r.hz, 7.f);
  }
  // Bubbles, about 6 a second at full.
  if (stream_ > 0.01f && uniform() < 6.f * kParamStep / kFs) {
    for (Bubble &b : bub_) {
      if (b.left > 0) continue;
      b.hz = 500.f + 800.f * uniform();
      b.left = (int)(kFs * (0.025f + 0.035f * uniform()));
      b.rise = powf(1.45f, 1.f / (float)b.left);
      b.amp = 0.12f + 0.2f * uniform();
      b.decay = expf(-1.f / (0.015f * kFs));
      b.phase = 0.f;
      break;
    }
  }
  // The wind's gusts: a new strength every 1.5 to 5 s, reached in about a second.
  gust_left_ -= kParamStep;
  if (gust_left_ <= 0) {
    to_gust_ = 0.25f + 0.75f * uniform();
    gust_left_ = (int)(kFs * (1.5f + 3.5f * uniform()));
  }
  gust_ = glide(gust_, to_gust_, kParamStep / (1.0f * kFs));
  whistle_.bandpass(480.f + 520.f * gust_, 24.f);
  // Raindrops: 60 to 310 a second as the rain grows.
  const float drops = rain_ * (60.f + 250.f * rain_) * kParamStep / kFs;
  if (uniform() < drops) {
    const float a = uniform();
    drop_env_ += 0.05f + 0.4f * a * a * a;
  }
  // The fire: crackles (2 to 27 a second), now and then a pop.
  if (fire_ > 0.01f) {
    if (uniform() < (2.f + 25.f * fire_) * kParamStep / kFs) {
      const float a = uniform();
      crack_env_ += 0.1f + 0.9f * a * a * a;
    }
    if (pop_amp_ < 0.01f && uniform() < (0.3f + 1.5f * fire_) * kParamStep / kFs) {
      pop_hz_ = 600.f + 800.f * uniform();
      pop_amp_ = 0.2f + 0.2f * uniform();
      pop_phase_ = 0.f;
    }
  }
  // The birds: a song now and then (about every 8 s at full).
  if (song_pos_ < 0) {
    song_gap_ -= kParamStep;
    if (song_gap_ <= 0) {
      if (birds_ > 0.02f) startSong();
      song_gap_ = (int)(kFs * (3.f + 10.f * uniform()) / (birds_ > 0.1f ? birds_ : 0.1f));
    }
  }
}

void AmbientSynth::render(int16_t *out, size_t frames, const AmbientMix &t) {
  target_master_ = t.master;
  const float crack_decay = expf(-1.f / (0.002f * kFs)), drop_decay = expf(-1.f / (0.0009f * kFs));
  const float pop_decay = expf(-1.f / (0.008f * kFs));
  for (size_t i = 0; i < frames; ++i) {
    if (--step_left_ <= 0) {
      step_left_ = kParamStep;
      stepParams();
    }
    stream_ = glide(stream_, t.stream, kGlide);
    wind_ = glide(wind_, t.wind, kGlide);
    rain_ = glide(rain_, t.rain, kGlide);
    fire_ = glide(fire_, t.fire, kGlide);
    birds_ = glide(birds_, t.birds, kGlide);
    master_ = glide(master_, t.master, kGlide);

    const float w = noise();
    float mix = 0.f;
    if (stream_ > 1e-4f) {
      float s = 0.45f * base_lo_.run(w) + 0.10f * base_hi_.run(w);
      for (Resonator &r : res_) s += 0.55f * r.amp * r.f.run(noise());
      for (Bubble &b : bub_) {
        if (b.left <= 0) continue;
        b.phase += kTwoPi * b.hz / kFs;
        if (b.phase > kTwoPi) b.phase -= kTwoPi;
        b.hz *= b.rise;
        s += b.amp * fastSin(b.phase);
        b.amp *= b.decay;
        --b.left;
      }
      mix += stream_ * s;
    }
    if (wind_ > 1e-4f) {
      brown_ = 0.996f * brown_ + 0.06f * noise();
      const float body = wind_lo_.run(brown_ + 0.3f * w);
      const float whistle = whistle_.run(w) * gust_ * gust_;
      mix += wind_ * (0.9f * gust_ * body + 0.5f * whistle);
    }
    if (rain_ > 1e-4f) {
      const float drop = drop_env_ * drop_hp_.run(noise());
      drop_env_ *= drop_decay;
      mix += rain_ * (0.22f * rain_hiss_.run(w) + 0.12f * rain_low_.run(w)) + drop;
    } else {
      drop_env_ = 0.f;
    }
    if (fire_ > 1e-4f || crack_env_ > 1e-4f) {
      const float crack = crack_env_ * crack_hp_.run(noise());
      crack_env_ *= crack_decay;
      float pop = 0.f;
      if (pop_amp_ > 0.01f) {
        pop_phase_ += kTwoPi * pop_hz_ / kFs;
        if (pop_phase_ > kTwoPi) pop_phase_ -= kTwoPi;
        pop = pop_amp_ * fastSin(pop_phase_);
        pop_amp_ *= pop_decay;
      }
      mix += 0.5f * crack + fire_ * (0.3f * roar_lp_.run(w) + 0.04f * fire_hiss_.run(w)) + 0.6f * pop;
    }
    if (song_pos_ >= 0) {
      float b = 0.f;
      for (int k = 0; k < song_notes_; ++k) {
        const Note &n = song_[k];
        if (song_pos_ < n.start || song_pos_ >= n.start + n.len) continue;
        const float u = (float)(song_pos_ - n.start) / (float)n.len;
        vib_phase_ += kTwoPi * 32.f / kFs;   // a 32 Hz vibrato, for the warbles
        if (vib_phase_ > kTwoPi) vib_phase_ -= kTwoPi;
        const float hz = n.f0 + (n.f1 - n.f0) * u + n.vib * fastSin(vib_phase_);
        bird_phase_ += kTwoPi * hz / kFs;
        if (bird_phase_ > kTwoPi) bird_phase_ -= kTwoPi;
        b = fastSin(bird_phase_) * fastSin(3.14159265f * u);  // each note swells and fades
        break;
      }
      mix += song_amp_ * b;
      const Note &last = song_[song_notes_ - 1];
      if (++song_pos_ >= last.start + last.len) song_pos_ = -1;
    }
    out[i] = (int16_t)(soft(master_ * mix) * kOut);
  }
}

}  // namespace tc
