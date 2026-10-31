// The Arduino build uses -Os, which made the render loop call sine() out of line for every
// sample: Westminster took 2.25 s on the panel. This file is that loop.
#pragma GCC optimize("O2")
#include "synth.h"
#include <initializer_list>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace tc {

namespace {

constexpr float kPi = 3.14159265f;

// ---- Voices --------------------------------------------------------------------------------
// Every sound is a list of voices, each one sine (or one band of noise) with its own envelope,
// pitch path, vibrato and tremolo. A struck note is one voice per partial of its timbre; a bird's
// syllable is one voice gliding through a few pitches; a beak or a clapper is a burst of noise.

constexpr int kMaxPoints = 8;
struct Voice {
  float at = 0, len = 0;                    // start and length, seconds
  float amp = 1;
  float attack = 0.003f, release = 0.25f;   // linear ramps at the start and at the end, seconds
  float decay = 0;                          // exponential, per second
  int points = 1;                           // pitch path: hz[i] at t[i] seconds into the voice,
  float t[kMaxPoints] = {}, hz[kMaxPoints] = {};  // gliding evenly in pitch between points
  float vib_hz = 0, vib_depth = 0;          // vibrato; depth is a fraction of the pitch
  float am_hz = 0, am_depth = 0;            // tremolo; depth 1 pulses down to silence
  float wob_hz = 0, wob_depth = 0;          // a second, slower vibrato: the drift of a real voice
  float noise_bw = 0;                       // above 0: white noise through a resonator at hz[0]
  uint32_t seed = 0;                        //   with this bandwidth, instead of a sine
};

class Song {
 public:
  Song(Voice *v, int cap) : v_(v), cap_(cap) {}
  // A new voice, or a spare one (never rendered) once the list is full; wanted() counts both.
  Voice &add() {
    wanted_++;
    Voice &v = n_ < cap_ ? v_[n_++] : spare_;
    v = Voice();
    return v;
  }
  int size() const { return n_; }
  int wanted() const { return wanted_; }
  const Voice *voices() const { return v_; }

 private:
  Voice *v_;
  int cap_, n_ = 0, wanted_ = 0;
  Voice spare_;
};

struct Timbre {
  int n;
  float ratio[8], amp[8], decay[8];
};
// Struck bar: a free bar's inharmonic modes, the upper ones dying faster. Mike picked this sound.
constexpr Timbre kBar = {3, {1.0f, 2.756f, 5.404f}, {1.0f, 0.35f, 0.12f}, {1.2f, 3.0f, 7.0f}};
// Short blip for the reply sounds.
constexpr Timbre kBlip = {3, {1.0f, 2.0f, 2.756f}, {1.0f, 0.25f, 0.1f}, {9.0f, 14.0f, 20.0f}};
// Music-box tine: a clamped bar, whose second mode is 6.27 times the first.
constexpr Timbre kTine = {2, {1.0f, 6.267f}, {1.0f, 0.12f}, {1.4f, 7.0f}};
// Wind-chime tube: the bar's modes, ringing longer.
constexpr Timbre kTube = {3, {1.0f, 2.756f, 5.404f}, {1.0f, 0.3f, 0.1f}, {0.9f, 2.5f, 6.0f}};

float midi(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

// A struck note: one voice per partial (partials above about 20 kHz are left out).
void strike(Song &s, float hz, float at, float ring, const Timbre &tb, float gain = 1.0f) {
  for (int p = 0; p < tb.n; p++) {
    if (hz * tb.ratio[p] > 0.45f * kSynthRate) continue;
    Voice &v = s.add();
    v.at = at;
    v.len = ring;
    v.hz[0] = hz * tb.ratio[p];
    v.amp = tb.amp[p] * gain;
    v.decay = tb.decay[p];
  }
}

// One sine voice; set a pitch path afterwards with path().
Voice &tone(Song &s, float at, float len, float hz, float amp, float attack, float release) {
  Voice &v = s.add();
  v.at = at;
  v.len = len;
  v.hz[0] = hz;
  v.amp = amp;
  v.attack = attack;
  v.release = release;
  return v;
}

// Pitch path from (time, hz) pairs: path(v, {0, 4000, 0.3f, 3880}).
void path(Voice &v, std::initializer_list<float> th) {
  int i = 0;
  const float *p = th.begin();
  for (size_t k = 0; k + 1 < th.size() && i < kMaxPoints; k += 2, i++) {
    v.t[i] = p[k];
    v.hz[i] = p[k + 1];
  }
  v.points = i;
}

// A copy of a voice with every pitch multiplied by k: a harmonic that follows the same path.
Voice &harmonic(Song &s, const Voice &v, float k, float amp) {
  Voice &h = s.add();
  h = v;
  for (int i = 0; i < h.points; i++) h.hz[i] *= k;
  h.amp = v.amp * amp;
  return h;
}

// Band-passed white noise, for the strike of a clapper or a beak: centre hz, bandwidth bw.
Voice &noise(Song &s, float at, float len, float hz, float bw, float amp, float decay) {
  Voice &v = s.add();
  v.at = at;
  v.len = len;
  v.hz[0] = hz;
  v.noise_bw = bw;
  v.amp = amp;
  v.decay = decay;
  v.attack = 0.0005f;
  v.release = 0.005f;
  v.seed = 0x9E3779B9u * (uint32_t)s.wanted();  // each noise voice its own, the same every time
  return v;
}

// Fixed pseudo-random numbers, so the panel and the tests render the same sound.
struct Rand {
  uint32_t s;
  float next() {  // 0 to 1
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) / 16777216.0f;
  }
};

// ---- The sounds ----------------------------------------------------------------------------

const float E6 = 1318.51f, D6 = 1174.66f, C6 = 1046.50f, G5 = 783.99f, G6 = 1567.98f;

void westminster(Song &s) {
  // Changes 4 and 5 of the Westminster Quarters, the usual doorbell version.
  const float notes[8][2] = {{E6, 0.0f}, {C6, 0.6f}, {D6, 1.2f}, {G5, 1.8f},
                             {G5, 3.0f}, {D6, 3.6f}, {E6, 4.2f}, {C6, 4.8f}};
  for (const auto &n : notes) strike(s, n[0], n[1], 3.5f, kBar);
}

void ding_dong(Song &s) {
  strike(s, E6, 0.0f, 3.0f, kBar);
  strike(s, C6, 0.7f, 2.5f, kBar);
}

void rising_chime(Song &s) {  // C E G C on the bars
  const int notes[4] = {84, 88, 91, 96};
  for (int i = 0; i < 4; i++) strike(s, midi(notes[i]), 0.35f * i, i == 3 ? 3.0f : 2.4f, kBar);
}

// One swing of a tower bell tuned to nominal n. Its partials as a real minor-third bell has them,
// relative to the nominal: hum, prime (the strike note), tierce, nominal, deciem, superquint, octave
// and one higher (the quint and the faintest high one left out, for render time). The lower ones
// come in slightly detuned pairs, whose beating is the slow warble of a big bell; the clapper's
// strike is a burst of bright noise.
void tower_bell(Song &s, float n, float at, float ring, float gain) {
  struct P { float ratio, amp, decay, beat; };
  const P parts[] = {
      {0.25f, 0.2f, 0.3f, 0.25f}, {0.5f, 0.4f, 0.55f, 0.6f},  {0.595f, 0.5f, 0.7f, 0.9f},
      {1.0f, 1.0f, 0.9f, 1.2f},   {1.25f, 0.25f, 2.0f, 0.0f}, {1.5f, 0.4f, 1.6f, 0.0f},
      {2.0f, 0.2f, 2.8f, 0.0f},   {2.5f, 0.1f, 4.0f, 0.0f},
  };
  for (const P &p : parts) {
    float hz = n * p.ratio;
    if (p.beat > 0) {  // unequal pair: the warble never quite cancels
      tone(s, at, ring, hz - 0.5f * p.beat, 0.65f * p.amp * gain, 0.002f, 0.3f).decay = p.decay;
      tone(s, at, ring, hz + 0.5f * p.beat, 0.35f * p.amp * gain, 0.002f, 0.3f).decay = p.decay;
    } else {
      tone(s, at, ring, hz, p.amp * gain, 0.002f, 0.3f).decay = p.decay;
    }
  }
  noise(s, at, 0.12f, 2600.0f, 2200.0f, 0.5f * gain, 35.0f);
}

void church_bells(Song &s) {
  // Two tower bells swinging in turn, A above E, four strokes and then the long ring dying away.
  for (int i = 0; i < 4; i++) tower_bell(s, i % 2 ? 659.26f : 880.0f, 1.0f * i, 3.6f, i % 2 ? 1.0f : 0.85f);
}

void music_box(Song &s) {
  // A short tune of its own, melody over a few bass tines.
  const int melody[11] = {88, 91, 96, 95, 91, 93, 91, 88, 86, 88, 84};  // E G C B G A G E D E C
  for (int i = 0; i < 11; i++) strike(s, midi(melody[i]), 0.26f * i, i == 10 ? 2.4f : 1.6f, kTine);
  const int bass[4][2] = {{72, 0}, {79, 4}, {79, 8}, {72, 10}};  // C G G C, at melody notes 0, 4, 8, 10
  for (const auto &b : bass) strike(s, midi(b[0]), 0.26f * b[1], 1.8f, kTine, 0.6f);
}

void chickadee(Song &s) {
  // Black-capped chickadee, "fee-bee" twice: two clear whistles, the second a step lower.
  for (int rep = 0; rep < 2; rep++) {
    float t0 = 1.25f * rep;
    Voice &fee = tone(s, t0, 0.35f, 4000.0f, 1.0f, 0.04f, 0.06f);
    path(fee, {0.0f, 4000.0f, 0.33f, 3880.0f});
    Voice &bee = tone(s, t0 + 0.42f, 0.32f, 3420.0f, 0.9f, 0.03f, 0.07f);
    path(bee, {0.0f, 3420.0f, 0.3f, 3330.0f});
  }
}

void cardinal(Song &s) {
  // Northern cardinal: "what-cheer cheer cheer", then "birdie birdie..." in quick down-flicks.
  Voice &what = tone(s, 0.0f, 0.1f, 2200.0f, 0.7f, 0.01f, 0.02f);
  path(what, {0.0f, 2200.0f, 0.09f, 3600.0f});
  for (int i = 0; i < 3; i++) {
    Voice &v = tone(s, 0.2f + 0.42f * i, 0.3f, 4300.0f, 1.0f, 0.02f, 0.06f);
    path(v, {0.0f, 4300.0f, 0.06f, 4000.0f, 0.28f, 1900.0f});
  }
  for (int i = 0; i < 5; i++) {
    Voice &v = tone(s, 1.55f + 0.17f * i, 0.14f, 3600.0f, 0.85f, 0.01f, 0.03f);
    path(v, {0.0f, 3600.0f, 0.1f, 2300.0f, 0.13f, 2600.0f});
  }
}

void wind_chimes(Song &s) {
  // Pentatonic tubes stirred by two gusts of wind.
  const int notes[6] = {84, 86, 88, 91, 93, 96};  // C D E G A C
  Rand r{0xC41E};
  for (int i = 0; i < 12; i++) {
    float gust = i < 6 ? 0.0f : 1.3f;
    float at = gust + 0.8f * (i % 6) / 6.0f + 0.1f * r.next();
    int k = (int)(6.0f * r.next());
    if (k > 5) k = 5;
    strike(s, midi(notes[k]), at, 2.0f, kTube, 0.45f + 0.55f * r.next());
  }
}

void nightingale(Song &s) {
  // Common nightingale: whistles swelling in a slow crescendo, a run of quick "chook" notes, a fast
  // trill, and a rising whistle to finish.
  for (int i = 0; i < 4; i++) {
    Voice &v = tone(s, 0.32f * i, 0.22f, 2000.0f, 0.35f + 0.18f * i, 0.03f, 0.05f);
    path(v, {0.0f, 2000.0f, 0.15f, 2000.0f, 0.21f, 2150.0f});
    harmonic(s, v, 2.0f, 0.1f);
  }
  for (int i = 0; i < 8; i++) {
    Voice &v = tone(s, 1.3f + i / 14.0f, 0.045f, 3200.0f, 0.8f, 0.004f, 0.012f);
    path(v, {0.0f, 3200.0f, 0.04f, 2200.0f});
  }
  for (int i = 0; i < 12; i++) tone(s, 1.95f + i / 22.0f, 0.03f, i % 2 ? 3400.0f : 4200.0f, 0.7f, 0.003f, 0.01f);
  Voice &end = tone(s, 2.55f, 0.35f, 1600.0f, 0.85f, 0.02f, 0.06f);
  path(end, {0.0f, 1600.0f, 0.3f, 3000.0f});
  end.vib_hz = 30.0f;
  end.vib_depth = 0.01f;
  harmonic(s, end, 2.0f, 0.1f);
}

// One blow of a beak on dead wood: a click of noise ringing in the branch's hollow, a sharper tick
// on top, and a dull knock under it.
void peck(Song &s, float at, float gain, float tune) {
  noise(s, at, 0.05f, 1450.0f * tune, 650.0f, gain, 140.0f);
  noise(s, at, 0.02f, 4200.0f * tune, 3000.0f, 0.35f * gain, 450.0f);
  tone(s, at, 0.06f, 520.0f * tune, 0.35f * gain, 0.0005f, 0.01f).decay = 70.0f;
}

void woodpecker(Song &s) {
  // A downy woodpecker drumming: two rolls of about 17 blows a second, each fading at its end,
  // with the faint echo of the woods behind every blow. MIKE (2026-10-24): no call at the end.
  Rand r{0x7700D};
  float at = 0;
  for (int roll = 0; roll < 2; roll++) {
    int n = roll ? 16 : 14;
    for (int i = 0; i < n; i++) {
      float fade = i > n - 5 ? 1.0f - 0.15f * (i - (n - 5)) : 1.0f;
      float gain = (i == 0 ? 0.7f : 1.0f) * fade * (0.9f + 0.2f * r.next());
      float tune = 0.97f + 0.06f * r.next();
      peck(s, at, gain, tune);
      noise(s, at + 0.045f, 0.06f, 1300.0f * tune, 900.0f, 0.18f * gain, 60.0f);  // the echo
      at += 1.0f / 17.0f + 0.002f * (r.next() - 0.5f);
    }
    at += 0.9f;
  }
}

void frogs(Song &s) {
  // A pond in spring: three spring peepers calling, each a short rising peep, over a toad's trill.
  Voice &trill = tone(s, 0.5f, 1.8f, 1650.0f, 0.35f, 0.15f, 0.3f);
  trill.am_hz = 38.0f;
  trill.am_depth = 1.0f;
  harmonic(s, trill, 2.0f, 0.2f);
  const float base[3] = {2850.0f, 3050.0f, 3250.0f}, first[3] = {0.0f, 0.35f, 0.7f};
  Rand r{0xF209};
  for (int f = 0; f < 3; f++)
    for (int i = 0; i < 3; i++) {
      float at = first[f] + 0.95f * i + 0.08f * r.next();
      Voice &v = tone(s, at, 0.16f, base[f], 0.8f + 0.2f * r.next(), 0.02f, 0.05f);
      path(v, {0.0f, base[f], 0.14f, base[f] * 1.12f});
    }
}

using Builder = void (*)(Song &);
struct Ringtone {
  uint8_t id;  // what Settings store; never reused
  const char *name;
  ToneGroup group;
  Builder build;
};
// In list order. Ids 4 (Rounds), 6 (Old bell), 9 (Robin), 14 (Crickets) and 15 (Loon) are retired.
const Ringtone kRingtones[kRingtoneCount] = {
    {0, "Westminster", ToneGroup::Standard, westminster},   {1, "Ding-dong", ToneGroup::Standard, ding_dong},
    {2, "Rising chime", ToneGroup::Standard, rising_chime}, {3, "Church bells", ToneGroup::Standard, church_bells},
    {5, "Music box", ToneGroup::Standard, music_box},       {7, "Chickadee", ToneGroup::Nature, chickadee},
    {8, "Cardinal", ToneGroup::Nature, cardinal},           {10, "Wind chimes", ToneGroup::Nature, wind_chimes},
    {11, "Nightingale", ToneGroup::Nature, nightingale},    {12, "Woodpecker", ToneGroup::Nature, woodpecker},
    {13, "Frogs", ToneGroup::Nature, frogs},
};

int index_of(uint8_t id) {
  for (int i = 0; i < kRingtoneCount; i++)
    if (kRingtones[i].id == id) return i;
  return -1;
}

void build(Song &s, Sound sound, uint8_t ringtone) {
  switch (sound) {
    case Sound::Chime: {
      int i = index_of(ringtone);
      kRingtones[i < 0 ? 0 : i].build(s);
      break;
    }
    case Sound::SoftChime:  // Busy: one gentle two-note chime
      strike(s, E6, 0.0f, 1.8f, kBar);
      strike(s, C6, 0.5f, 2.0f, kBar);
      break;
    case Sound::ReplyYes:  // Come on: rising
      strike(s, C6, 0.0f, 0.4f, kBlip);
      strike(s, G6, 0.16f, 0.5f, kBlip);
      break;
    case Sound::ReplyNo:  // Not now: falling
      strike(s, G6, 0.0f, 0.4f, kBlip);
      strike(s, C6, 0.16f, 0.5f, kBlip);
      break;
    default: break;
  }
}

// ---- Rendering -----------------------------------------------------------------------------

// Sine table with linear interpolation: error under 2e-6, no library call per sample.
constexpr int kTableBits = 10, kTable = 1 << kTableBits, kFracBits = 32 - kTableBits;
float g_sin[kTable + 1];
bool g_sin_ready = false;

__attribute__((always_inline)) inline float sine(uint32_t ph) {
  uint32_t i = ph >> kFracBits;
  float f = (float)(ph & ((1u << kFracBits) - 1)) * (1.0f / (1u << kFracBits));
  return g_sin[i] + (g_sin[i + 1] - g_sin[i]) * f;
}

constexpr float kPhasePerHz = 4294967296.0f / kSynthRate;
constexpr uint32_t kQuarter = 1u << 30;

// What a voice carries from one block to the next.
struct State {
  uint32_t start, len, stop;  // samples; stop is where it has faded below -80 dB (or len)
  bool plain;                 // fixed pitch, no vibrato or tremolo: a struck partial
  float x, y, c, s;           // plain: a rotating phasor, its decay folded into the rotation
  float n1, n2, a1, a2, g;    // noise: the resonator's last two outputs and its coefficients
  uint32_t rng;
  uint32_t ph, vib, am, wob;  // otherwise: phases for the sine table
  float hz, step, env, d;
  int seg;
  uint32_t seg_end;  // sample (from the voice's start) where the current glide ends
};

void enter_segment(const Voice &v, State &st, int seg) {
  st.seg = seg;
  st.hz = v.hz[seg];
  if (seg + 1 < v.points) {
    uint32_t a = (uint32_t)(v.t[seg] * kSynthRate), b = (uint32_t)(v.t[seg + 1] * kSynthRate);
    uint32_t n = b > a ? b - a : 1;
    st.step = powf(v.hz[seg + 1] / v.hz[seg], 1.0f / n);
    st.seg_end = b > a ? b : a + 1;
  } else {
    st.step = 1.0f;
    st.seg_end = 0xFFFFFFFFu;
  }
}

void reset(const Voice &v, State &st) {
  st.start = (uint32_t)(v.at * kSynthRate);
  st.len = (uint32_t)(v.len * kSynthRate);
  st.stop = st.len;
  if (v.decay > 0) {  // past -80 dB the rest is inaudible: skip it
    uint32_t quiet = (uint32_t)(9.21f / v.decay * kSynthRate);
    if (quiet < st.stop) st.stop = quiet;
  }
  st.ph = st.vib = st.am = st.wob = 0;
  st.env = 1.0f;
  st.d = v.decay > 0 ? expf(-v.decay / kSynthRate) : 1.0f;
  enter_segment(v, st, 0);
  st.plain = v.points == 1 && v.vib_depth == 0 && v.am_depth == 0 && v.wob_depth == 0 && v.noise_bw == 0;
  float w = 2 * kPi * st.hz / kSynthRate;
  st.x = v.amp;
  st.y = 0;
  st.c = cosf(w) * st.d;
  st.s = sinf(w) * st.d;
  // Two-pole resonator, scaled to a peak gain of about 1 at its centre.
  float r = expf(-kPi * v.noise_bw / kSynthRate);
  st.a1 = 2 * r * cosf(w);
  st.a2 = -r * r;
  st.g = (1 - r) * sqrtf(1 - 2 * r * cosf(2 * w) + r * r);
  st.n1 = st.n2 = 0;
  st.rng = v.seed ? v.seed : 1;
}

// Adds the voice's part of the block [b0, b1) to mix. The loops have no divisions or library calls:
// on the ESP32-S3 each of those is an out-of-line call, and this runs millions of times.
void run(const Voice &v, State &st, float *mix, uint32_t b0, uint32_t b1) {
  uint32_t vs = st.start, ve = st.start + st.stop;
  if (vs >= b1 || ve <= b0) return;
  uint32_t k0 = (b0 > vs ? b0 : vs) - vs, k1 = (b1 < ve ? b1 : ve) - vs;
  float *m = mix + (vs + k0 - b0);
  const float att = v.attack * kSynthRate, rel = v.release * kSynthRate;
  const float inv_att = att > 0 ? 1.0f / att : 0, inv_rel = rel > 0 ? 1.0f / rel : 0;
  const float len = (float)st.len, amp = v.amp;
  if (st.plain) {
    // Most of every bell and bar: a phasor turned by a fixed rotation, four multiplies a sample.
    // The ramps at the start and end get their own loop, so the long middle has no tests.
    float x = st.x, y = st.y;
    const float c = st.c, s = st.s;
    uint32_t a_end = (uint32_t)ceilf(att), r_start = len > rel ? (uint32_t)(len - rel) + 1 : 0;
    uint32_t k = k0;
    while (k < k1) {
      uint32_t stop = k1;
      if (k >= a_end && k < r_start) {
        if (r_start < stop) stop = r_start;
        for (; k < stop; k++) {
          *m++ += y;
          float nx = x * c - y * s;
          y = x * s + y * c;
          x = nx;
        }
      } else {
        if (k < a_end && a_end < stop) stop = a_end;
        for (; k < stop; k++) {
          float fk = (float)k, left = len - fk;
          float e = (fk < att ? fk * inv_att : 1.0f) * (left < rel ? left * inv_rel : 1.0f);
          *m++ += e * y;
          float nx = x * c - y * s;
          y = x * s + y * c;
          x = nx;
        }
      }
    }
    st.x = x;
    st.y = y;
    return;
  }
  uint32_t ph = st.ph;
  float env = st.env;
  const float d = st.d;
  if (v.noise_bw > 0) {  // band of noise: fixed centre, no glide
    float n1 = st.n1, n2 = st.n2;
    const float a1 = st.a1, a2 = st.a2, g = st.g;
    uint32_t rng = st.rng;
    for (uint32_t k = k0; k < k1; k++) {
      float fk = (float)k, left = len - fk;
      float e = (fk < att ? fk * inv_att : 1.0f) * (left < rel ? left * inv_rel : 1.0f);
      rng = rng * 1664525u + 1013904223u;
      float x = (float)(int32_t)rng * (1.0f / 2147483648.0f);
      float y = g * x + a1 * n1 + a2 * n2;
      n2 = n1;
      n1 = y;
      *m++ += amp * env * e * y;
      env *= d;
    }
    st.n1 = n1;
    st.n2 = n2;
    st.rng = rng;
    st.env = env;
    return;
  }
  {
    // Everything the loop reads is a local: the voice list lives in PSRAM on the panel.
    float hz = st.hz, step = st.step;
    uint32_t vib = st.vib, am = st.am, wob = st.wob, seg_end = st.seg_end;
    const float vib_depth = v.vib_depth, am_depth = v.am_depth, wob_depth = v.wob_depth;
    const uint32_t vib_inc = (uint32_t)(v.vib_hz * kPhasePerHz), am_inc = (uint32_t)(v.am_hz * kPhasePerHz);
    const uint32_t wob_inc = (uint32_t)(v.wob_hz * kPhasePerHz);
    for (uint32_t k = k0; k < k1; k++) {
      if (k >= seg_end) {  // next glide of the pitch path
        enter_segment(v, st, st.seg + 1);
        hz = st.hz;
        step = st.step;
        seg_end = st.seg_end;
      }
      float fk = (float)k, left = len - fk;
      float e = (fk < att ? fk * inv_att : 1.0f) * (left < rel ? left * inv_rel : 1.0f);
      float trem = 1.0f - am_depth * (0.5f + 0.5f * sine(am + kQuarter));  // starts at the low point
      *m++ += amp * env * e * trem * sine(ph);
      ph += (uint32_t)(hz * (1.0f + vib_depth * sine(vib) + wob_depth * sine(wob)) * kPhasePerHz);
      hz *= step;
      vib += vib_inc;
      am += am_inc;
      wob += wob_inc;
      env *= d;
    }
    st.hz = hz;
    st.vib = vib;
    st.am = am;
    st.wob = wob;
  }
  st.ph = ph;
  st.env = env;
}

constexpr uint32_t kBlock = 512;  // about 12 ms
float g_block[kBlock];            // on the panel: internal RAM, much faster than PSRAM

// Builds the sound's voice list in memory from the heap; the caller frees it.
Voice *make_song(Sound s, uint8_t ringtone, int *n, int *wanted) {
  Voice *v = (Voice *)malloc(sizeof(Voice) * kMaxVoices);
  if (!v) return nullptr;
  Song song(v, kMaxVoices);
  build(song, s, ringtone);
  *n = song.size();
  if (wanted) *wanted = song.wanted();
  return v;
}

size_t frames_of(const Voice *v, int n) {
  float end = 0;
  for (int i = 0; i < n; i++)
    if (v[i].at + v[i].len > end) end = v[i].at + v[i].len;
  return (size_t)((end + 0.05f) * kSynthRate);  // a little silence after the last voice
}

}  // namespace

uint8_t ringtone_id(int index) { return kRingtones[index >= 0 && index < kRingtoneCount ? index : 0].id; }
int ringtone_index(uint8_t id) { return index_of(id); }
const char *ringtone_name(uint8_t id) {
  int i = index_of(id);
  return kRingtones[i < 0 ? 0 : i].name;
}
ToneGroup ringtone_group(uint8_t id) {
  int i = index_of(id);
  return kRingtones[i < 0 ? 0 : i].group;
}
const char *tone_group_name(ToneGroup g) { return g == ToneGroup::Nature ? "Nature" : "Standard"; }

uint8_t ringtone_from_saved(uint16_t version, uint8_t old) {
  // Version 1: 0-6 standard, 7-11 sci-fi (gone), 12-16 the first five nature ones. Into version 2.
  if (version == 1) old = old >= 12 ? (uint8_t)(old - 5) : (old >= 7 ? 0 : old);
  // Version 2: 11 was Water drops (gone), 12-16 the five nature ones added after it. Into version
  // 3, whose numbers are today's ids.
  if (version <= 2) old = old == 11 ? 0 : (old >= 12 ? (uint8_t)(old - 1) : old);
  return index_of(old) >= 0 ? old : 0;  // a retired id: Westminster
}

size_t synth_frames(Sound s, uint8_t ringtone) {
  int n = 0;
  Voice *v = make_song(s, ringtone, &n, nullptr);
  if (!v) return 0;
  size_t f = frames_of(v, n);
  free(v);
  return f;
}

size_t synth_max_chime_frames() {
  size_t most = 0;
  for (int i = 0; i < kRingtoneCount; i++) {
    size_t f = synth_frames(Sound::Chime, kRingtones[i].id);
    if (f > most) most = f;
  }
  return most;
}

int synth_voices(Sound s, uint8_t ringtone) {
  int n = 0, wanted = 0;
  Voice *v = make_song(s, ringtone, &n, &wanted);
  free(v);
  return wanted;
}

bool synth(Sound s, uint8_t ringtone, int16_t *out, size_t frames, void (*yield)()) {
  memset(out, 0, frames * sizeof(int16_t));
  if (!g_sin_ready) {
    for (int i = 0; i <= kTable; i++) g_sin[i] = sinf(2 * kPi * i / kTable);
    g_sin_ready = true;
  }
  int n = 0;
  Voice *v = make_song(s, ringtone, &n, nullptr);
  State *st = (State *)malloc(sizeof(State) * (n ? n : 1));
  if (!v || !st) {
    free(v);
    free(st);
    return false;
  }
  // Two passes: the first finds the peak, the second writes at -3.5 dBFS. Holding the whole mix
  // as floats instead would take twice the memory of the sound itself. The same sound always comes
  // out the same, so its gain is kept, and the next time it takes one pass.
  static float s_gain[4][32];
  int slot = s == Sound::Chime ? ringtone : 0;
  float *known = (int)s < 4 && slot < 32 ? &s_gain[(int)s][slot] : nullptr;
  float gain = known ? *known : 0;
  for (int pass = gain > 0 ? 1 : 0; pass < 2; pass++) {
    for (int i = 0; i < n; i++) reset(v[i], st[i]);
    float peak = 1e-6f;
    for (uint32_t b0 = 0; b0 < frames; b0 += kBlock) {
      uint32_t b1 = b0 + kBlock < frames ? b0 + kBlock : (uint32_t)frames;
      memset(g_block, 0, sizeof(g_block));
      for (int i = 0; i < n; i++) run(v[i], st[i], g_block, b0, b1);
      if (pass == 0) {
        for (uint32_t k = 0; k < b1 - b0; k++) {
          float a = g_block[k] < 0 ? -g_block[k] : g_block[k];
          if (a > peak) peak = a;
        }
      } else {
        for (uint32_t k = 0; k < b1 - b0; k++) {
          float x = g_block[k] * gain;
          x = x > 32767.0f ? 32767.0f : (x < -32767.0f ? -32767.0f : x);
          out[b0 + k] = (int16_t)(x >= 0 ? x + 0.5f : x - 0.5f);
        }
      }
      if (yield) yield();
    }
    if (pass == 0) gain = 32767.0f * 0.668f / peak;  // -3.5 dBFS
  }
  if (known) *known = gain;
  free(st);
  free(v);
  return true;
}

}  // namespace tc
