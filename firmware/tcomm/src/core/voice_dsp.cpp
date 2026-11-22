// Voice notes, piece A: resampler, encoders, level detector, pre-roll, capture state machine and
// the mic task / LVGL handshake. Plain C++ (see voice_dsp.h and docs/voice-notes/DESIGN.md 3).
#include "voice_dsp.h"
#include <math.h>
#include <string.h>

namespace tc {

// ---- Resampler ----

namespace {
constexpr float kPi = 3.14159265358979f;

int16_t clip16(float v) {
  if (v >= 32767.0f) return 32767;
  if (v <= -32768.0f) return -32768;
  return (int16_t)(v >= 0 ? v + 0.5f : v - 0.5f);
}
}  // namespace

void VoiceResampler::reset() {
  // RBJ low-pass biquads with the Butterworth Qs of an order 2 x stages filter:
  // Q_k = 1 / (2 sin((2k - 1) pi / (2 n))), k = 1 .. stages (n = 4: 1.3066 and 0.5412, DESIGN 3.1).
  const float n = 2.0f * kVoiceLpStages;
  const float w0 = 2.0f * kPi * kVoiceLpHz / (float)kVoiceInRate;
  const float c = cosf(w0), s = sinf(w0);
  for (int k = 0; k < kVoiceLpStages; k++) {
    float q = 1.0f / (2.0f * sinf((2.0f * (k + 1) - 1.0f) * kPi / (2.0f * n)));
    float alpha = s / (2.0f * q);
    float a0 = 1.0f + alpha;
    Bq &b = bq_[k];
    b.b0 = (1.0f - c) / 2.0f / a0;
    b.b1 = (1.0f - c) / a0;
    b.b2 = b.b0;
    b.a1 = -2.0f * c / a0;
    b.a2 = (1.0f - alpha) / a0;
    b.z1 = b.z2 = 0;
  }
  last_ = 0;
  rel_ = 0;
}

int VoiceResampler::process(const int16_t *mono, int n, int16_t *out, int cap) {
  int o = 0;
  for (int i = 0; i < n; i++) {
    float y = (float)mono[i];
    for (int k = 0; k < kVoiceLpStages; k++) {  // transposed direct form II
      Bq &b = bq_[k];
      float x = y;
      y = b.b0 * x + b.z1;
      b.z1 = b.b1 * x - b.a1 * y + b.z2;
      b.z2 = b.b2 * x - b.a2 * y;
    }
    // y is input index i; last_ is index i-1. rel_ is the next output's place relative to i-1 in
    // 1/160 of a sample (441/160 = 44100/16000 inputs per output), so one output at most per input.
    if (rel_ < 160) {
      if (o < cap) out[o++] = clip16(last_ + (y - last_) * (float)rel_ * (1.0f / 160.0f));
      rel_ += 441;
    }
    rel_ -= 160;
    last_ = y;
  }
  return o;
}

int voice_downmix(const int16_t *stereo, int frames, int16_t *mono) {
  for (int i = 0; i < frames; i++) mono[i] = (int16_t)(((int)stereo[2 * i] + (int)stereo[2 * i + 1]) >> 1);
  return frames;
}

// ---- Treble restore ----

void VoiceTreble::set_db(float db) {
  if (!(db >= 0.0f)) db = 0.0f;  // NaN and cuts both mean off
  if (db > (float)kVoiceTrebleMaxDb) db = (float)kVoiceTrebleMaxDb;
  db_ = db;
  reset();
  // RBJ cookbook high shelf, S = 1: alpha = sin(w0) / 2 * sqrt((A + 1/A) (1/S - 1) + 2) = sin(w0) / sqrt(2).
  const float A = powf(10.0f, db / 40.0f);
  const float w0 = 2.0f * kPi * kVoiceTrebleHz / (float)kVoiceRateOut;
  const float cs = cosf(w0);
  const float alpha = sinf(w0) * 0.70710678f;
  const float tsa = 2.0f * sqrtf(A) * alpha;
  const float a0 = (A + 1.0f) - (A - 1.0f) * cs + tsa;
  b0_ = A * ((A + 1.0f) + (A - 1.0f) * cs + tsa) / a0;
  b1_ = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs) / a0;
  b2_ = A * ((A + 1.0f) + (A - 1.0f) * cs - tsa) / a0;
  a1_ = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs) / a0;
  a2_ = ((A + 1.0f) - (A - 1.0f) * cs - tsa) / a0;
}

void VoiceTreble::process(int16_t *pcm, int n) {
  if (db_ == 0.0f) return;  // off: untouched, bit for bit
  for (int i = 0; i < n; i++) {
    const float x = (float)pcm[i];
    const float y = b0_ * x + z1_;  // transposed direct form II
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    pcm[i] = clip16(y);
  }
}

// ---- Level boost ----

void VoiceBoost::set_db(float db) {
  if (!(db >= 0.0f)) db = 0.0f;  // NaN and cuts both mean no boost
  if (db > (float)kVoiceBoostMaxDb) db = (float)kVoiceBoostMaxDb;
  db_ = db;
  gain_ = powf(10.0f, db / 20.0f);
  limited_ = 0;
}

int16_t VoiceBoost::apply(int16_t s) const {
  float y = (float)s * gain_;
  float a = y < 0 ? -y : y;
  if (a > kVoiceLimitKnee) {
    const float span = kVoiceLimitCeiling - kVoiceLimitKnee;
    a = kVoiceLimitKnee + span * tanhf((a - kVoiceLimitKnee) / span);
    if (a > kVoiceLimitCeiling) a = kVoiceLimitCeiling;  // tanh < 1; only float rounding could get here
    y = y < 0 ? -a : a;
    limited_++;
  }
  return clip16(y);
}

void VoiceBoost::process(int16_t *pcm, int n) const {
  if (gain_ == 1.0f) return;  // 0 dB: untouched, bit for bit
  for (int i = 0; i < n; i++) pcm[i] = apply(pcm[i]);
}

// ---- Encoder ----

void VoiceEncoder::begin(VoiceCodec codec, VoiceChunk *chunks, int max_chunks) {
  codec_ = codec;
  chunks_ = chunks;
  max_ = max_chunks;
  per_ = codec == VoiceCodec::Mulaw ? kVoiceChunkSamplesMulaw : kVoiceChunkSamplesAdpcm;
  ready_ = fill_ = 0;
  samples_ = 0;
  pred_ = index_ = 0;
}

uint8_t voice_mulaw_encode(int16_t sample) {
  int s = sample;
  int sign = 0;
  if (s < 0) {
    s = -s;
    sign = 0x80;
  }
  if (s > 32635) s = 32635;
  s += 0x84;
  int exponent = 7;
  int mask = 0x4000;
  while (exponent > 0 && !(s & mask)) {
    exponent--;
    mask >>= 1;
  }
  int mantissa = (s >> (exponent + 3)) & 0x0F;
  return (uint8_t)(~(sign | (exponent << 4) | mantissa) & 0xFF);
}

bool VoiceEncoder::push(int16_t sample) {
  if (!chunks_ || ready_ >= max_) return false;
  VoiceChunk &c = chunks_[ready_];
  if (fill_ == 0) {  // the chunk's header is the state before its first sample
    c.pred = codec_ == VoiceCodec::Adpcm ? (int16_t)pred_ : (int16_t)0;
    c.step = codec_ == VoiceCodec::Adpcm ? (uint8_t)index_ : (uint8_t)0;
    c.len = 0;
  }
  if (codec_ == VoiceCodec::Mulaw) {
    c.data[fill_] = voice_mulaw_encode(sample);
  } else {
    int step = kImaStep[index_];
    int diff = (int)sample - pred_;
    int code = 0;
    if (diff < 0) {
      code = 8;
      diff = -diff;
    }
    int vpdiff = step >> 3;
    if (diff >= step) {
      code |= 4;
      diff -= step;
      vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
      code |= 2;
      diff -= step;
      vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
      code |= 1;
      vpdiff += step;
    }
    pred_ += (code & 8) ? -vpdiff : vpdiff;
    if (pred_ > 32767) pred_ = 32767;
    if (pred_ < -32768) pred_ = -32768;
    index_ += kImaIndex[code];
    if (index_ < 0) index_ = 0;
    if (index_ > 88) index_ = 88;
    if (fill_ & 1) c.data[fill_ >> 1] = (uint8_t)(c.data[fill_ >> 1] | (code << 4));
    else c.data[fill_ >> 1] = (uint8_t)code;  // an odd last sample leaves the high nibble 0
  }
  fill_++;
  samples_++;
  c.len = (uint8_t)(codec_ == VoiceCodec::Mulaw ? fill_ : (fill_ + 1) / 2);
  if (fill_ == per_) {
    ready_++;
    fill_ = 0;
  }
  return true;
}

bool VoiceEncoder::flush() {
  if (fill_ == 0 || ready_ >= max_) return false;
  ready_++;
  fill_ = 0;
  return true;
}

// ---- Level ----

float voice_power_db(float power) {
  if (!(power > 1e-9f)) return -90.0f;
  return 10.0f * log10f(power);
}

void VoiceLevel::begin(bool early_stop, uint32_t quiet_ms) {
  early_ = early_stop;
  speech_ = stop_ = false;
  blocks_ = 0;
  hits_ = 0;
  quiet_frames_ = 0;
  quiet_need_ = (uint32_t)((uint64_t)quiet_ms * kVoiceInRate / 1000);
  floor_db_ = -60.0f;
  db_ = -90.0f;
  shown_ = 0;
  level_ = 0;
}

void VoiceLevel::block(float power, int frames) {
  db_ = voice_power_db(power);
  blocks_++;
  if (blocks_ <= kVoiceWarmBlocks) {  // the floor starts as the quietest of the first blocks
    floor_db_ = blocks_ == 1 ? db_ : (db_ < floor_db_ ? db_ : floor_db_);
  } else {
    // Only quiet blocks move the floor: down quickly, up slowly, so speech never raises it.
    if (db_ < floor_db_ + kVoiceQuietDb) floor_db_ += (db_ < floor_db_ ? 0.2f : 0.02f) * (db_ - floor_db_);
  }
  if (floor_db_ < -70.0f) floor_db_ = -70.0f;
  if (blocks_ > kVoiceWarmBlocks) {
    bool hit = db_ > floor_db_ + kVoiceOnsetDb || db_ > kVoiceSpeechAbsDb;
    hits_ = (uint8_t)(((hits_ << 1) | (hit ? 1 : 0)) & 0x0F);
    int n = (hits_ & 1) + ((hits_ >> 1) & 1) + ((hits_ >> 2) & 1) + ((hits_ >> 3) & 1);
    if (!speech_ && n >= 3) speech_ = true;
    bool quiet = db_ < floor_db_ + kVoiceQuietDb;
    quiet_frames_ = quiet ? quiet_frames_ + (uint32_t)frames : 0;
    if (early_ && speech_ && quiet_frames_ >= quiet_need_) stop_ = true;
  }
  float target = (db_ - (floor_db_ + 3.0f)) * (100.0f / 25.0f);
  if (target < 0) target = 0;
  if (target > 100) target = 100;
  shown_ = target > shown_ ? target : shown_ * 0.85f;
  level_ = (uint8_t)shown_;
}

// ---- Pre-roll ----

void VoicePreRoll::push(uint32_t t, float power, const int16_t *mono, int n) {
  if (!mem_ || cap_ <= 0) return;
  if (n > kVoiceBlockFrames) n = kVoiceBlockFrames;
  VoicePreBlock &b = mem_[next_];
  b.t = t;
  b.power = power;
  b.n = (uint16_t)n;
  memcpy(b.mono, mono, (size_t)n * sizeof(int16_t));
  next_ = (next_ + 1) % cap_;
  if (count_ < cap_) count_++;
}

const VoicePreBlock &VoicePreRoll::at(int i) const {
  int idx = (next_ - count_ + i + 2 * cap_) % cap_;
  return mem_[idx];
}

// ---- Capture ----

void VoiceCapture::init(VoiceSendHook hook, VoicePreBlock *ring, int ring_n) {
  hook_ = hook;
  pre_.init(ring, ring_n);
  cur_ = nullptr;
  gen_ = 0;
  finished_ = ending_ = done_ = false;
}

void VoiceCapture::start(VoiceRec *rec, uint32_t gen) {
  cur_ = rec;
  gen_ = gen;
  finished_ = ending_ = done_ = full_ = false;
  fails_ = end_fails_ = 0;
  rs_.reset();
  VoiceCodec codec = rec->codec == (uint8_t)VoiceCodec::Mulaw ? VoiceCodec::Mulaw : VoiceCodec::Adpcm;
  enc_.begin(codec, rec->chunks, rec->max_chunks);
  lvl_.begin(rec->early_stop);
  treble_.set_db(treble_db_.load());  // also clears the filter state
  boost_.set_db(boost_db_.load());
  last_limited_ = 0;
  recordings_++;
  last_stop_ = VoiceStop::None;
  last_chunks_ = 0;
  // Pre-roll: the blocks that arrived from kVoicePreRollLeadMs before the press, oldest first.
  preroll_used_ = 0;
  uint32_t from = rec->press_ms - kVoicePreRollLeadMs;
  for (int i = 0; i < pre_.count(); i++) {
    const VoicePreBlock &b = pre_.at(i);
    if ((int32_t)(b.t - from) < 0) continue;
    feed(rec, b.mono, b.n, b.power);
    preroll_used_++;
  }
  pre_.clear();
}

// Resample, encode and measure one block of mono audio; publishes the counters LVGL reads.
void VoiceCapture::feed(VoiceRec *rec, const int16_t *mono, int n, float power) {
  int m = rs_.process(mono, n, pcm_, (int)(sizeof(pcm_) / sizeof(pcm_[0])));
  treble_.process(pcm_, m);  // the treble restore, 16 kHz domain, before the boost
  boost_.process(pcm_, m);  // the level boost and its limiter, in the 16 kHz domain
  last_limited_ = boost_.limited();
  for (int i = 0; i < m; i++) {
    if (!enc_.push(pcm_[i])) {
      full_ = true;
      break;
    }
  }
  lvl_.block(power, n);
  rec->n_samples.store((uint16_t)enc_.samples());
  rec->level.store(lvl_.level());
  if (lvl_.speech()) rec->speech.store(true);
  rec->chunks_ready.store((uint8_t)enc_.ready(), std::memory_order_release);
}

void VoiceCapture::finish(VoiceRec *rec, VoiceStop why) {
  if ((uint8_t)why == 0 || (uint8_t)why > (uint8_t)VoiceStop::Full) why = VoiceStop::Cancel;
  finished_ = true;
  last_stop_ = why;
  rec->stopped.store((uint8_t)why);
  if (why == VoiceStop::Cancel || why == VoiceStop::Bell) {
    // LVGL sends the ABORT; no last chunk and no END.
    last_chunks_ = enc_.ready();
    done_ = true;
    rec->done.store(true);
    return;
  }
  enc_.flush();
  rec->n_samples.store((uint16_t)enc_.samples());
  rec->chunks_ready.store((uint8_t)enc_.ready(), std::memory_order_release);
  last_chunks_ = enc_.ready();
  ending_ = true;
}

bool VoiceCapture::send_data(VoiceRec *rec, int idx) {
  VoiceMsg &m = msg_;
  memset(&m.u, 0, sizeof(m.u));  // as a new VoiceMsg
  m.type = VType::Data;
  m.sender = rec->sender;
  m.seq = rec->seq.fetch_add(1);
  m.floor = rec->floor;
  const VoiceChunk &c = rec->chunks[idx];
  m.u.data.xfer = rec->xfer;
  m.u.data.idx = (uint8_t)idx;
  m.u.data.pred = c.pred;
  m.u.data.step = c.step;
  m.u.data.len = c.len;
  memcpy(m.u.data.data, c.data, c.len);
  size_t n = voice_encode(m, frame_, sizeof(frame_));
  if (!n || !hook_.fn) return false;
  return hook_.fn(hook_.ctx, rec->to, frame_, n);
}

bool VoiceCapture::send_end(VoiceRec *rec) {
  VoiceMsg &m = msg_;
  memset(&m.u, 0, sizeof(m.u));  // as a new VoiceMsg
  m.type = VType::End;
  m.sender = rec->sender;
  m.seq = rec->seq.fetch_add(1);
  m.floor = rec->floor;
  m.u.end.xfer = rec->xfer;
  m.u.end.n_chunks = rec->chunks_ready.load();
  m.u.end.n_samples = rec->n_samples.load();
  m.u.end.speech = rec->speech.load() ? 1 : 0;
  size_t n = voice_encode(m, frame_, sizeof(frame_));
  if (!n || !hook_.fn) return false;
  return hook_.fn(hook_.ctx, rec->to, frame_, n);
}

// First sends, strictly in order, none 32 or more past base (check finding 6); then END once
// every chunk has had its first send. LVGL only resends below first_sent.
void VoiceCapture::pump(VoiceRec *rec, uint32_t now) {
  const bool none = rec->to == kVoiceToNone;  // voice dump: nothing goes out and no ACK moves base
  uint8_t ready = rec->chunks_ready.load(std::memory_order_acquire);
  uint8_t fs = rec->first_sent.load();
  uint8_t base = rec->base.load();
  int budget = kVoiceSendsPerBlock;
  while (fs < ready && budget > 0) {
    if (!none && !voice_in_window(fs, base)) {
      window_holds_++;
      break;
    }
    bool ok = none ? true : send_data(rec, fs);
    if (!ok && ++fails_ < kVoiceSendTries) {
      send_retries_++;
      break;  // the next block tries again
    }
    fails_ = 0;
    rec->first_ms[fs] = now;
    fs++;
    rec->first_sent.store(fs, std::memory_order_release);
    budget--;
  }
  if (ending_ && !rec->end_sent.load() && fs == ready && !done_) {
    bool ok = none ? true : send_end(rec);
    if (!ok && ++end_fails_ < kVoiceSendTries) {
      send_retries_++;
    } else {
      rec->end_sent.store(true);
      done_ = true;
      rec->done.store(true);
    }
  }
  if (finished_ && !done_ && (int32_t)(now - (rec->press_ms + kVoiceStuckMs)) > 0) {
    done_ = true;  // END never got out; LVGL's own timeouts abort the job
    rec->done.store(true);
  }
}

bool VoiceCapture::block(VoiceRec *rec, uint32_t gen, const int16_t *stereo, int frames, uint32_t now, float power,
                         bool bell) {
  if (frames > kVoiceBlockFrames) frames = kVoiceBlockFrames;
  if (!rec) {  // nothing published: keep the pre-roll ring fresh
    cur_ = nullptr;
    gen_ = 0;
    int n = voice_downmix(stereo, frames, mono_);
    pre_.push(now, power, mono_, n);
    return false;
  }
  if (rec != cur_ || gen != gen_) start(rec, gen);
  if (done_) return false;
  if (!finished_) {
    uint8_t req = rec->stop.load();
    if (req) {
      finish(rec, (VoiceStop)req);
    } else if (bell) {
      finish(rec, VoiceStop::Bell);
    } else if ((int32_t)(now - (rec->press_ms + kVoiceRecMs)) >= 0) {
      finish(rec, VoiceStop::Countdown);
    } else {
      int n = voice_downmix(stereo, frames, mono_);
      feed(rec, mono_, n, power);
      if (full_) finish(rec, VoiceStop::Full);
      else if (rec->early_stop && lvl_.quiet_stop()) finish(rec, VoiceStop::Quiet);
    }
  }
  if (done_) return true;  // cancelled or bell: finished by this call
  pump(rec, now);
  return done_;
}

// ---- Handshake ----

bool VoiceHandshake::begin(VoiceRec *rec) {
  if (!rec) return false;
  VoiceRec *none = nullptr;
  if (!owner_.compare_exchange_strong(none, rec)) return false;
  gen_.fetch_add(1);
  // A ring_bell() that raced the previous release() (audio_play saw recording() still true) must not
  // end this new recording at once; a ring that is sounding now is set again by voice_rec_begin.
  bell_.store(false);
  recording_.store(true);
  rec_.store(rec);  // seq_cst: the mic task sees a VoiceRec that is fully set up
  return true;
}

void VoiceHandshake::stop(VoiceRec *rec, VoiceStop why) {
  if (!rec || owner_.load() != rec) return;
  uint8_t none = 0;
  rec->stop.compare_exchange_strong(none, (uint8_t)why);
}

bool VoiceHandshake::release(VoiceRec *rec) {
  if (owner_.load() != rec) return true;  // not published (or already released)
  rec_.store(nullptr);                    // first, then look at busy (seq_cst on both)
  if (busy_.load()) return false;
  bell_.store(false);
  recording_.store(false);
  owner_.store(nullptr);
  return true;
}

VoiceRec *VoiceHandshake::enter(uint32_t &gen) {
  busy_.store(true);  // first, then read rec (seq_cst on both)
  VoiceRec *r = rec_.load();
  gen = gen_.load();
  return r;
}

}  // namespace tc
