// Voice notes, piece A: the capture side as plain C++ (docs/voice-notes/DESIGN.md 3.1 to 3.4).
// No Arduino, ESP-IDF or LVGL headers, so every line of it runs in the host tests
// (tests/test_voice_dsp.cpp). board_audio.cpp's mic task only feeds it blocks and a clock.
//
//   VoiceResampler  44.1 kHz stereo -> 16 kHz mono: downmix, an 8th-order Butterworth low-pass (four
//                   biquads), linear interpolation at the 16 kHz instants.
//   VoiceEncoder    IMA ADPCM (or G.711 mu-law) into VoiceChunks, the golden-vector way.
//   VoiceLevel      noise floor, speech onset, the 0.8 s early stop, the level bar.
//   VoicePreRoll    the last few blocks, kept all the time so a press loses no first syllable.
//   VoiceCapture    one VoiceRec's whole life inside the mic task: feed, stop rules, first sends in
//                   order inside the 32-chunk window (check finding 6), END.
//   VoiceHandshake       the g_rec / g_rec_busy handshake between the mic task and the LVGL task (3.2).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include "voice_proto.h"

namespace tc {

// ---- Constants (cheap to change) ----
constexpr int kVoiceBlockFrames = 512;        // stereo frames the mic task reads at a time (11.6 ms)
constexpr uint32_t kVoiceInRate = 44100;      // the codec's rate (the codec is never reopened)
// Anti-alias: DESIGN 3.1 asked for two biquads (a 4th-order Butterworth at 6.5 kHz). That gives only
// -15 dB at 10 kHz, which the design's own test (10 kHz below -30 dB) cannot pass. Four biquads (8th
// order) at 6.3 kHz give -32 dB at 10 kHz and -1.6 dB at 6 kHz.
constexpr int kVoiceLpStages = 4;
constexpr float kVoiceLpHz = 6300.0f;
// Pre-roll: audio from this long before the press is taken from the ring, so the press itself
// (touch read, LVGL event, mic task noticing the VoiceRec) does not eat the first syllable. 0 turns it
// off. With it the clip can run up to 3.07 s (104 chunks, inside kVoiceClipChunks).
constexpr int kVoicePreRollBlocks = 6;        // ring size: 70 ms
constexpr uint32_t kVoicePreRollLeadMs = 60;
// First sends (DESIGN 3.2): at most this many per block (the stream is 1 chunk per 2.5 blocks, so
// this is catching up after the window held a chunk), and how many times a refused esp_now_send is
// retried on the next blocks before the chunk counts as sent (LVGL's resend path then covers it).
constexpr int kVoiceSendsPerBlock = 3;
constexpr int kVoiceSendTries = 3;
// A finished capture whose END cannot go out (the window never opens) gives up this long after the
// press: done is set so the mic task stops waiting. LVGL aborts long before (base not moving 1.5 s).
constexpr uint32_t kVoiceStuckMs = 12000;
// Level and speech detection (DESIGN 3.4). dB figures are block power, 10 log10(mean square).
constexpr float kVoiceOnsetDb = 12.0f;        // speech: above the floor by this in 3 of 4 blocks
constexpr float kVoiceQuietDb = 6.0f;         // quiet: below the floor by less than this
constexpr float kVoiceSpeechAbsDb = -36.0f;   // also speech: above this (room noise is near -48) in 3 of 4
constexpr int kVoiceWarmBlocks = 4;           // the floor starts as the minimum of the first blocks
constexpr uint8_t kVoiceToNone = 0xFF;        // VoiceRec::to: capture only (voice dump); no sends, no window
// Level boost before encoding (field test 2026-11-21: speech about -40 dBFS RMS, peaks -20 to -28, at
// one foot). Applied to the 16 kHz samples after the resampler, so the level bar and the speech rules
// (which use the raw block power) are unchanged. A soft limiter above the knee keeps every peak under
// the ceiling: no sample is ever clipped. Serial `voice boost N` changes the dB until restart.
constexpr float kVoiceBoostDb = 15.0f;
constexpr float kVoiceLimitKnee = 15000.0f;     // linear below this (about -6.8 dBFS)
constexpr float kVoiceLimitCeiling = 30000.0f;  // the most it gives (about -0.8 dBFS): under mu-law's top code
constexpr int kVoiceBoostMaxDb = 30;
// Treble restore (field finding 2026-11-21: the panel microphones deliver speech with 15-20 dB too
// little energy above 2 kHz, measured on the raw I2S data, so it is acoustic). One RBJ high shelf
// (S = 1) at 16 kHz, applied right after the resampler and before the level boost. Offline on 10
// real clips through Vosk: +10 dB at 1500 Hz cut word errors from 29% to 20% (+18 dB the same).
// Serial `voice treble N` changes the dB until restart; 0 bypasses the stage (bit for bit).
constexpr float kVoiceTrebleHz = 1500.0f;
constexpr float kVoiceTrebleDb = 10.0f;
constexpr int kVoiceTrebleMaxDb = 20;
constexpr uint32_t kVoiceRateOut = 16000;  // the resampler's output rate

// ---- Treble restore: one high-shelf biquad ----
class VoiceTreble {
 public:
  explicit VoiceTreble(float db = kVoiceTrebleDb) { set_db(db); }
  void set_db(float db);  // clamps to 0..kVoiceTrebleMaxDb, computes the coefficients, resets the state
  float db() const { return db_; }
  void reset() { z1_ = z2_ = 0.0f; }  // per recording
  void process(int16_t *pcm, int n);

 private:
  float db_ = 0, b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_ = 0, z2_ = 0;
};

// ---- Level boost with a soft limiter ----
// y = g x while |g x| <= knee; above it knee + (ceiling - knee) tanh((|g x| - knee) / (ceiling - knee)),
// which is continuous with slope 1 at the knee and never passes the ceiling. No state between samples.
class VoiceBoost {
 public:
  explicit VoiceBoost(float db = kVoiceBoostDb) { set_db(db); }
  void set_db(float db);
  float db() const { return db_; }
  float gain() const { return gain_; }
  int16_t apply(int16_t s) const;
  void process(int16_t *pcm, int n) const;
  uint32_t limited() const { return limited_; }  // samples the limiter touched since set_db
  void count_reset() { limited_ = 0; }

 private:
  float db_ = 0, gain_ = 1;
  mutable uint32_t limited_ = 0;
};

// The badge row's live microphone meter before START (no capture running): the peak of the last
// blocks in dBFS -> 0-100. -54 dBFS shows nothing, -18 dBFS fills the bar (speech at one foot peaks
// -20 to -28 on the raw microphones).
constexpr float kVoiceMeterLowDb = -54.0f, kVoiceMeterHighDb = -18.0f;
inline uint8_t voice_meter_level(float peak_db) {
  if (!(peak_db > kVoiceMeterLowDb)) return 0;
  if (peak_db >= kVoiceMeterHighDb) return 100;
  return (uint8_t)((peak_db - kVoiceMeterLowDb) * 100.0f / (kVoiceMeterHighDb - kVoiceMeterLowDb));
}

// ---- Resampler ----
class VoiceResampler {
 public:
  VoiceResampler() { reset(); }
  void reset();
  // mono: n samples at 44.1 kHz. Writes 16 kHz samples to out (at most cap) and returns how many.
  // One call per block, any n; the state runs on from call to call. About 2.756 inputs per output.
  int process(const int16_t *mono, int n, int16_t *out, int cap);
  static int out_max(int n) { return n * 160 / 441 + 2; }  // samples n inputs can make, at most

 private:
  struct Bq { float b0, b1, b2, a1, a2, z1, z2; };
  Bq bq_[kVoiceLpStages];
  float last_;  // the previous filtered sample (input index i-1)
  int rel_;     // next output position relative to input index i-1, in 1/160 of a sample
};
// Downmix interleaved stereo to mono ((L + R) / 2). Returns frames.
int voice_downmix(const int16_t *stereo, int frames, int16_t *mono);

// ---- Encoder ----
// Fills VoiceChunks as samples come in: ADPCM 474 samples a chunk, mu-law 237. Each ADPCM chunk's
// header is the state before its first sample, the state runs on unbroken, LOW nibble first, and an
// odd last sample leaves the high nibble 0 (voice_vectors.py adpcm_encode is the reference).
class VoiceEncoder {
 public:
  void begin(VoiceCodec codec, VoiceChunk *chunks, int max_chunks);
  // False when there is no room for the sample (every chunk is full): the capture is Full.
  bool push(int16_t s);
  // Closes a partial chunk so it counts as ready. True if there was one.
  bool flush();
  int ready() const { return ready_; }            // complete chunks
  uint32_t samples() const { return samples_; }   // samples accepted
  int per_chunk() const { return per_; }
  VoiceCodec codec() const { return codec_; }

 private:
  VoiceCodec codec_ = VoiceCodec::Adpcm;
  VoiceChunk *chunks_ = nullptr;
  int max_ = 0, per_ = 474, ready_ = 0, fill_ = 0;
  uint32_t samples_ = 0;
  int pred_ = 0, index_ = 0;
};
uint8_t voice_mulaw_encode(int16_t s);  // G.711, bias 0x84, clip 32635

// ---- Level, speech onset, early stop ----
class VoiceLevel {
 public:
  void begin(bool early_stop, uint32_t quiet_ms = kVoiceEarlyStopMs);
  // One block: power is the mean square, full scale = 1 (what the mic task already computes);
  // frames is its length at 44.1 kHz.
  void block(float power, int frames);
  bool speech() const { return speech_; }
  bool quiet_stop() const { return stop_; }       // after speech: quiet for quiet_ms
  uint8_t level() const { return level_; }        // 0-100 for the bar
  float floor_db() const { return floor_db_; }
  float last_db() const { return db_; }

 private:
  bool early_ = true, speech_ = false, stop_ = false;
  int blocks_ = 0;
  uint8_t hits_ = 0;      // the last four blocks: bit set = above the onset line
  uint32_t quiet_frames_ = 0, quiet_need_ = 0;
  float floor_db_ = -60.0f, db_ = -90.0f, shown_ = 0.0f;
  uint8_t level_ = 0;
};
float voice_power_db(float power);  // 10 log10, floored at -90

// ---- Pre-roll ----
struct VoicePreBlock {
  uint32_t t;      // arrival (millis) of the block
  float power;
  uint16_t n;      // frames used
  int16_t mono[kVoiceBlockFrames];
};
class VoicePreRoll {
 public:
  void init(VoicePreBlock *mem, int n) { mem_ = mem; cap_ = n; clear(); }
  void clear() { next_ = 0; count_ = 0; }
  void push(uint32_t t, float power, const int16_t *mono, int n);
  int count() const { return count_; }
  const VoicePreBlock &at(int i) const;  // 0 = oldest

 private:
  VoicePreBlock *mem_ = nullptr;
  int cap_ = 0, next_ = 0, count_ = 0;
};

// ---- Capture: one VoiceRec inside the mic task ----
struct VoiceSendHook {
  bool (*fn)(void *ctx, uint8_t to, const uint8_t *data, size_t len) = nullptr;  // board::radio_send_voice
  void *ctx = nullptr;
};
class VoiceCapture {
 public:
  void init(VoiceSendHook hook, VoicePreBlock *ring, int ring_n);
  // One call per block the microphones delivered. rec is the published VoiceRec or nullptr; gen
  // changes on every voice_rec_begin (a freed VoiceRec's address can be reused). bell is the
  // doorbell flag. now is millis(). power is the block's mean square (both channels, full scale 1).
  // Returns true on the call that finished the capture (rec->done is set by then).
  bool block(VoiceRec *rec, uint32_t gen, const int16_t *stereo, int frames, uint32_t now, float power, bool bell);

  // Counters for "info" (written by the mic task, read anywhere).
  uint32_t recordings() const { return recordings_; }
  uint32_t send_retries() const { return send_retries_; }
  uint32_t window_holds() const { return window_holds_; }
  VoiceStop last_stop() const { return last_stop_; }
  int last_chunks() const { return last_chunks_; }
  int preroll_used() const { return preroll_used_; }
  // The level boost (kVoiceBoostDb). set_boost_db takes effect at the next recording's start; any
  // thread (a float store read once per recording by the mic task).
  void set_boost_db(float db) { boost_db_.store(db); }
  float boost_db() const { return boost_db_.load(); }
  uint32_t last_limited() const { return last_limited_; }  // samples the limiter touched, last recording
  // The treble restore (kVoiceTrebleDb); same rules as the boost.
  void set_treble_db(float db) { treble_db_.store(db); }
  float treble_db() const { return treble_db_.load(); }

 private:
  void start(VoiceRec *rec, uint32_t gen);
  void feed(VoiceRec *rec, const int16_t *mono, int n, float power);
  void finish(VoiceRec *rec, VoiceStop why);
  void pump(VoiceRec *rec, uint32_t now);
  bool send_data(VoiceRec *rec, int idx);
  bool send_end(VoiceRec *rec);

  VoiceSendHook hook_;
  VoicePreRoll pre_;
  VoiceResampler rs_;
  VoiceEncoder enc_;
  VoiceLevel lvl_;
  VoiceTreble treble_;
  std::atomic<float> treble_db_{kVoiceTrebleDb};
  VoiceBoost boost_;
  std::atomic<float> boost_db_{kVoiceBoostDb};
  uint32_t last_limited_ = 0;
  VoiceRec *cur_ = nullptr;
  uint32_t gen_ = 0;
  bool finished_ = false;  // capture over (flushed or cancelled); sends may still be owed
  bool ending_ = false;    // END is owed
  bool done_ = false;
  bool full_ = false;      // the chunk array is full
  int fails_ = 0, end_fails_ = 0;
  int16_t mono_[kVoiceBlockFrames];
  int16_t pcm_[kVoiceBlockFrames * 160 / 441 + 4];
  // send_data's and send_end's message and frame, here rather than on the mic task's stack (about
  // 500 bytes less stack; field fix 2026-11-20). Only the mic task sends, one at a time.
  VoiceMsg msg_;
  uint8_t frame_[kMaxMsg];
  uint32_t recordings_ = 0, send_retries_ = 0, window_holds_ = 0;
  VoiceStop last_stop_ = VoiceStop::None;
  int last_chunks_ = 0, preroll_used_ = 0;
};

// ---- The handshake between the mic task and the LVGL task (DESIGN 3.2) ----
// g_rec / g_rec_busy as one object, so the host tests can run it. Ownership: LVGL builds a VoiceRec
// and begin()s it. The mic task brackets every use with enter()/leave(). LVGL frees the VoiceRec
// only after release() returned true. busy is set BEFORE rec is read (and cleared after the last
// use), and release clears rec BEFORE reading busy, all seq_cst, so the two cannot both miss each
// other: if release() sees busy false, the mic task's next enter() sees no rec.
class VoiceHandshake {
 public:
  // False if a VoiceRec is already published (rec stays the caller's).
  bool begin(VoiceRec *rec);
  // rec->stop = why, if no stop was asked yet. LVGL task.
  void stop(VoiceRec *rec, VoiceStop why);
  // Takes it back. True once the mic task is not inside it and never will be again; false: call
  // again next tick. Also clears the bell flag and the recording flag.
  bool release(VoiceRec *rec);
  // Mic task: the published VoiceRec (or nullptr) and its generation. Always call leave() after.
  VoiceRec *enter(uint32_t &gen);
  void leave() { busy_.store(false); }
  // The doorbell (audio_play, audio_task, begin when a chime is playing): the mic task stops the
  // recording with VoiceStop::Bell on its next block. Cleared by release().
  void ring_bell() { bell_.store(true); }
  bool bell() const { return bell_.load(); }
  // True from begin() until the mic task finished the capture (finished()) or release().
  bool recording() const { return recording_.load(); }
  void finished() { recording_.store(false); }
  bool busy() const { return busy_.load(); }

 private:
  std::atomic<VoiceRec *> rec_{nullptr};    // what the mic task sees
  std::atomic<VoiceRec *> owner_{nullptr};  // who begin()ed it, until release() succeeds
  std::atomic<uint32_t> gen_{0};
  std::atomic<bool> busy_{false}, bell_{false}, recording_{false};
};

}  // namespace tc
