// Voice notes piece A: the resampler, the ADPCM and mu-law encoders against the golden vectors, the
// level detector (onset, early stop) on synthetic power traces, the pre-roll, the capture state
// machine run by a fake mic task loop (window, order, END, every stop reason, refused sends), the
// mic task / LVGL handshake (VoiceHandshake), and the Ding sound. Synthetic signals only.
#include "voice_test.h"
#include <functional>
#include <limits>
#include <math.h>
#include <memory>
#include <string.h>
#include <thread>
#include <atomic>
#include "core/synth.h"
#include "core/voice_dsp.h"
#include "core/voice_proto.h"

using namespace tc;

namespace {

const double kTwoPi = 6.283185307179586;

struct Lcg {
  uint32_t x;
  explicit Lcg(uint32_t seed) : x(seed) {}
  uint32_t next() { return x = x * 1664525u + 1013904223u; }
  int sample(int amp) { return (int)(((int)((next() >> 16) & 0xFFFF) - 32768) * (int64_t)amp >> 15); }
};

// Mono 44.1 kHz sine, amplitude amp, n samples.
std::vector<int16_t> sine44(double hz, int amp, int n) {
  std::vector<int16_t> v(n);
  for (int i = 0; i < n; i++) v[i] = (int16_t)lrint(amp * sin(kTwoPi * hz * i / 44100.0));
  return v;
}

double rms_of(const int16_t *p, int n) {
  double s = 0;
  for (int i = 0; i < n; i++) s += (double)p[i] * p[i];
  return n ? sqrt(s / n) : 0;
}

// Response of the whole resampler to a sine, in dB against an ideal pass (amp / sqrt 2).
double response_db(double hz, int amp = 10000) {
  VoiceResampler r;
  std::vector<int16_t> in = sine44(hz, amp, 44100), out(VoiceResampler::out_max(44100));
  int n = r.process(in.data(), (int)in.size(), out.data(), (int)out.size());
  if (n < 15990 || n > 16010) return 99;
  double e = rms_of(out.data() + n / 2, n / 2);  // after the filter has settled
  return 20 * log10(e / (amp / sqrt(2.0)));
}

std::vector<int16_t> load_samples(const char *name) {
  std::vector<int16_t> v;
  bool ok = vt::read_samples(name, v);
  CHECK(ok);
  return v;
}

// Encodes with VoiceEncoder into a chunk array and returns how many chunks came out.
int encode_all(VoiceCodec codec, const std::vector<int16_t> &in, std::vector<VoiceChunk> &chunks) {
  chunks.assign(kVoiceMaxChunks, VoiceChunk());
  VoiceEncoder e;
  e.begin(codec, chunks.data(), kVoiceMaxChunks);
  for (int16_t s : in) CHECK(e.push(s));
  e.flush();
  chunks.resize(e.ready());
  return e.ready();
}

}  // namespace

// ---- Resampler ----

VTEST(dsp_resampler_response) {
  CHECK(fabs(response_db(300)) < 0.5);
  CHECK(fabs(response_db(1000)) < 0.5);
  CHECK(fabs(response_db(3000)) < 0.7);
  CHECK(response_db(6000) > -3.0);   // the top of the speech band survives
  CHECK(response_db(10000) < -30.0);  // DESIGN 12: 10 kHz below -30 dB (it folds to 6 kHz otherwise)
  CHECK(response_db(12000) < -30.0);
  CHECK(response_db(15000) < -30.0);
  CHECK(response_db(20000) < -30.0);
  CHECK(response_db(9500) < -27.0);
}

VTEST(dsp_resampler_rate_and_dc) {
  VoiceResampler r;
  std::vector<int16_t> in(44100 * 3, 4000), out(VoiceResampler::out_max((int)in.size()));
  int n = r.process(in.data(), (int)in.size(), out.data(), (int)out.size());
  CHECK(n >= 47990 && n <= 48010);  // 16000 a second
  double dc = rms_of(out.data() + n - 100, 100);
  CHECK(fabs(dc - 4000) < 40);      // DC passes at unity
  // Out of range input clips instead of wrapping.
  VoiceResampler r2;
  std::vector<int16_t> loud(2000), o2(VoiceResampler::out_max(2000));
  for (int i = 0; i < 2000; i++) loud[i] = (i / 3) & 1 ? 32767 : -32768;
  int m = r2.process(loud.data(), 2000, o2.data(), (int)o2.size());
  CHECK(m > 0);
}

VTEST(dsp_resampler_block_split_is_exact) {
  std::vector<int16_t> in = sine44(777, 12000, 44100);
  Lcg rng(5);
  for (auto &s : in) s = (int16_t)(s / 2 + rng.sample(3000));
  VoiceResampler a, b;
  std::vector<int16_t> oa(VoiceResampler::out_max((int)in.size())), ob(oa.size());
  int na = a.process(in.data(), (int)in.size(), oa.data(), (int)oa.size());
  int nb = 0;
  for (int at = 0; at < (int)in.size();) {  // blocks of a ragged size
    int n = 1 + (int)(rng.next() >> 20) % 700;
    if (at + n > (int)in.size()) n = (int)in.size() - at;
    nb += b.process(in.data() + at, n, ob.data() + nb, (int)ob.size() - nb);
    at += n;
  }
  CHECK(na == nb);
  CHECK(memcmp(oa.data(), ob.data(), (size_t)na * 2) == 0);
}

VTEST(dsp_downmix) {
  int16_t st[8] = {100, 300, -100, -301, 32767, 32767, -32768, -32768};
  int16_t m[4];
  CHECK(voice_downmix(st, 4, m) == 4);
  CHECK(m[0] == 200 && m[1] == -201 && m[2] == 32767 && m[3] == -32768);
}

// ---- Encoders against the golden vectors (bit for bit) ----

static void adpcm_vector(const char *prefix) {
  std::vector<int16_t> in = load_samples((std::string(prefix) + "_input.txt").c_str());
  std::vector<int16_t> dec = load_samples((std::string(prefix) + "_decoded.txt").c_str());
  std::vector<vt::Chunk> want;
  CHECK(vt::read_chunks((std::string(prefix) + "_chunks.txt").c_str(), want));
  std::vector<VoiceChunk> got;
  int n = encode_all(VoiceCodec::Adpcm, in, got);
  CHECK(n == (int)want.size());
  size_t at = 0;
  for (int i = 0; i < n && i < (int)want.size(); i++) {
    CHECK(got[i].pred == want[i].pred);
    CHECK(got[i].step == want[i].step);
    CHECK(got[i].len == want[i].data.size());
    CHECK(memcmp(got[i].data, want[i].data.data(), want[i].data.size()) == 0);
    // Each chunk decodes alone, from its own header, to the vector's decode.
    int samples = (int)(in.size() - (size_t)i * 474);
    if (samples > 474) samples = 474;
    int16_t out[474];
    CHECK(voice_adpcm_decode(got[i].pred, got[i].step, got[i].data, samples, out) == samples);
    CHECK(memcmp(out, dec.data() + at, (size_t)samples * 2) == 0);
    at += (size_t)samples;
  }
  CHECK(at == dec.size());
}

VTEST(dsp_adpcm_matches_sweep_vectors) { adpcm_vector("voice_adpcm_sweep"); }

VTEST(dsp_adpcm_matches_edge_vectors) {
  adpcm_vector("voice_adpcm_edge");
  // The edge signal is 475 samples: the second chunk holds one sample, its high nibble 0.
  std::vector<int16_t> in = load_samples("voice_adpcm_edge_input.txt");
  std::vector<VoiceChunk> got;
  CHECK(encode_all(VoiceCodec::Adpcm, in, got) == 2);
  CHECK(got[0].len == 237 && got[1].len == 1);
  CHECK((got[1].data[0] & 0xF0) == 0);
}

VTEST(dsp_mulaw_matches_vectors_and_round_trips) {
  std::vector<int16_t> in = load_samples("voice_adpcm_sweep_input.txt");
  std::vector<int16_t> dec = load_samples("voice_mulaw_sweep_decoded.txt");
  std::vector<vt::Chunk> want;
  CHECK(vt::read_chunks("voice_mulaw_sweep_chunks.txt", want));
  std::vector<VoiceChunk> got;
  int n = encode_all(VoiceCodec::Mulaw, in, got);
  CHECK(n == (int)want.size());
  size_t at = 0;
  for (int i = 0; i < n && i < (int)want.size(); i++) {
    CHECK(got[i].pred == 0 && got[i].step == 0);
    CHECK(got[i].len == want[i].data.size());
    CHECK(memcmp(got[i].data, want[i].data.data(), want[i].data.size()) == 0);
    for (int k = 0; k < got[i].len; k++, at++) {
      int16_t d = voice_mulaw_decode(got[i].data[k]);
      CHECK(d == dec[at]);
      int err = abs((int)d - (int)in[at]);
      CHECK(err <= 1100);  // G.711 is 13-bit: the step at full scale is 1024
    }
  }
  CHECK(at == dec.size());
  for (int v = -32768; v <= 32767; v += 97) {  // every encode decodes within its step
    int16_t d = voice_mulaw_decode(voice_mulaw_encode((int16_t)v));
    int lim = abs(v) > 32635 ? 1200 : (abs(v) / 16 + 40);
    CHECK(abs((int)d - v) <= lim);
  }
}

VTEST(dsp_encoder_full_and_flush) {
  std::vector<VoiceChunk> ch(2);
  VoiceEncoder e;
  e.begin(VoiceCodec::Adpcm, ch.data(), 2);
  CHECK(e.per_chunk() == 474);
  for (int i = 0; i < 948; i++) CHECK(e.push((int16_t)(i * 31)));
  CHECK(e.ready() == 2);
  CHECK(!e.push(1));      // no room: the capture is Full
  CHECK(!e.flush());      // nothing partial
  CHECK(e.samples() == 948);
  e.begin(VoiceCodec::Adpcm, ch.data(), 2);
  for (int i = 0; i < 475; i++) CHECK(e.push(1000));
  CHECK(e.ready() == 1);
  CHECK(e.flush() && e.ready() == 2);
  CHECK(ch[1].len == 1);
  e.begin(VoiceCodec::Mulaw, ch.data(), 2);
  CHECK(e.per_chunk() == 237);
  for (int i = 0; i < 238; i++) CHECK(e.push(5));
  CHECK(e.ready() == 1 && e.flush() && ch[1].len == 1);
}

// ---- Level: onset, early stop, floor ----

namespace {
double db_to_power(double db) { return pow(10.0, db / 10.0); }
const int kBlk = 512;
}  // namespace

VTEST(dsp_level_onset_and_early_stop) {
  VoiceLevel l;
  l.begin(true);
  Lcg rng(9);
  auto room = [&]() { return db_to_power(-48.0 + (rng.next() % 300) / 100.0 - 1.5); };  // -49.5 .. -46.5
  for (int i = 0; i < 20; i++) l.block(room(), kBlk);  // 0.23 s of room
  CHECK(!l.speech() && !l.quiet_stop());
  CHECK(l.floor_db() < -46.0 && l.floor_db() > -52.0);
  // One loud block is not speech (3 of 4 needed).
  l.block(db_to_power(-20), kBlk);
  for (int i = 0; i < 6; i++) l.block(room(), kBlk);
  CHECK(!l.speech());
  // Speech: -25 dB blocks with dips; 3 of the last 4 above floor + 12 starts it.
  for (int i = 0; i < 40; i++) l.block(db_to_power(i % 4 == 3 ? -52 : -25), kBlk);
  CHECK(l.speech());
  CHECK(!l.quiet_stop());
  CHECK(l.level() >= 0);
  // 0.8 s of quiet ends it: 800 ms is 69 blocks of 11.6 ms.
  int n = 0;
  while (!l.quiet_stop() && n < 200) {
    l.block(room(), kBlk);
    n++;
  }
  CHECK(n >= 68 && n <= 70);
  CHECK(l.floor_db() < -46.0);  // speech never raised the floor
  // A pause in speech shorter than 0.8 s does not stop it.
  VoiceLevel k;
  k.begin(true);
  for (int i = 0; i < 12; i++) k.block(room(), kBlk);
  for (int i = 0; i < 30; i++) k.block(db_to_power(-25), kBlk);
  for (int i = 0; i < 60; i++) k.block(room(), kBlk);  // 0.7 s
  for (int i = 0; i < 10; i++) k.block(db_to_power(-25), kBlk);
  CHECK(k.speech() && !k.quiet_stop());
  for (int i = 0; i < 60; i++) k.block(room(), kBlk);
  CHECK(!k.quiet_stop());  // the quiet run restarted
}

VTEST(dsp_level_no_speech_never_stops) {
  VoiceLevel l;
  l.begin(true);
  Lcg rng(3);
  for (int i = 0; i < 400; i++) l.block(db_to_power(-48.0 + (rng.next() % 200) / 100.0), kBlk);  // 4.6 s of room
  CHECK(!l.speech() && !l.quiet_stop());
  VoiceLevel off;  // early stop off: speech is seen but nothing stops
  off.begin(false);
  for (int i = 0; i < 10; i++) off.block(db_to_power(-48), kBlk);
  for (int i = 0; i < 30; i++) off.block(db_to_power(-25), kBlk);
  for (int i = 0; i < 200; i++) off.block(db_to_power(-48), kBlk);
  CHECK(off.speech() && !off.quiet_stop());
}

VTEST(dsp_level_warm_up_and_fallbacks) {
  // Speech from the very first block: the floor starts high, the absolute line still sees it.
  VoiceLevel l;
  l.begin(true);
  for (int i = 0; i < 20; i++) l.block(db_to_power(-25), kBlk);
  CHECK(l.speech());
  // Digital silence does not drag the floor below -70.
  VoiceLevel z;
  z.begin(true);
  for (int i = 0; i < 10; i++) z.block(0.0f, kBlk);
  CHECK(z.floor_db() >= -70.0f);
  CHECK(voice_power_db(0.0f) == -90.0f);
  // Level follows the signal up at once and falls back slowly.
  VoiceLevel v;
  v.begin(true);
  for (int i = 0; i < 12; i++) v.block(db_to_power(-48), kBlk);
  uint8_t quiet = v.level();
  v.block(db_to_power(-20), kBlk);
  uint8_t loud = v.level();
  v.block(db_to_power(-48), kBlk);
  CHECK(quiet <= 10 && loud >= 90 && v.level() < loud && v.level() > quiet);
}

VTEST(dsp_preroll_ring) {
  std::vector<VoicePreBlock> mem(3);
  VoicePreRoll p;
  p.init(mem.data(), 3);
  int16_t a[512];
  for (int i = 1; i <= 5; i++) {
    for (int k = 0; k < 512; k++) a[k] = (int16_t)i;
    p.push((uint32_t)(i * 10), (float)i, a, 512);
  }
  CHECK(p.count() == 3);
  CHECK(p.at(0).t == 30 && p.at(1).t == 40 && p.at(2).t == 50);
  CHECK(p.at(2).mono[511] == 5 && p.at(0).power == 3.0f);
  p.clear();
  CHECK(p.count() == 0);
  VoicePreRoll none;  // no memory: pushes are ignored
  none.push(1, 1, a, 512);
  CHECK(none.count() == 0);
}

// ---- Capture, run by a fake mic task loop ----

namespace {

const uint32_t kT0 = 100000;

struct Sent {
  int blk;
  VoiceMsg m;
};

struct Harness {
  VoiceCapture cap;
  VoiceHandshake link;
  std::vector<VoicePreBlock> ring;
  std::vector<uint8_t> mem;
  VoiceRec *rec = nullptr;
  std::vector<Sent> sent;
  int refuse = 0;             // refuse the next n sends
  int refuse_idx = -1;        // refuse every send of this DATA idx (-1 none)
  int blk = 0;
  uint32_t press_ms = 0;
  std::function<void(int, int16_t *)> src;
  std::function<void(Harness &)> on_block;
  uint32_t gen_seen = 0;
  bool finished = false;

  Harness() : ring(kVoicePreRollBlocks) {
    VoiceSendHook h;
    h.fn = &Harness::send_cb;
    h.ctx = this;
    cap.init(h, ring.data(), (int)ring.size());
    src = [](int, int16_t *b) { memset(b, 0, 1024 * 2); };
  }
  static bool send_cb(void *ctx, uint8_t to, const uint8_t *d, size_t n) {
    Harness *h = (Harness *)ctx;
    (void)to;
    VoiceMsg m;
    if (!voice_decode(d, n, m)) return true;  // counted below by the tests that care
    if (h->refuse > 0) {
      h->refuse--;
      return false;
    }
    if (h->refuse_idx >= 0 && m.type == VType::Data && m.u.data.idx == h->refuse_idx) return false;
    h->sent.push_back({h->blk, m});
    return true;
  }
  uint32_t now() const { return kT0 + (uint32_t)((uint64_t)blk * 512 * 1000 / 44100); }
  void make_rec(int max_chunks = kVoiceClipChunks, bool early = true, uint8_t to = kSenderGateway, uint8_t codec = 2) {
    mem.assign(voice_rec_bytes((uint8_t)max_chunks) + 16, 0xEE);
    rec = voice_rec_init(mem.data(), mem.size(), (uint8_t)max_chunks);
    rec->xfer = 7;
    rec->floor = 1;
    rec->sender = kSenderDownstairs;
    rec->to = to;
    rec->codec = codec;
    rec->chunk_samples = codec == 1 ? 237 : 474;
    rec->early_stop = early;
  }
  // The LVGL side: press now (press_ms = the time of the next block) and publish.
  void press() {
    press_ms = now();
    rec->press_ms = press_ms;
    CHECK(link.begin(rec));
  }
  // One iteration of the mic task loop.
  bool step(bool bell_override = false) {
    int16_t buf[1024];
    src(blk, buf);
    int64_t sum = 0;
    for (int k = 0; k < 1024; k++) sum += (int64_t)buf[k] * buf[k];
    float power = (float)((double)sum / 1024.0 / (32768.0 * 32768.0));
    uint32_t gen = 0;
    VoiceRec *r = link.enter(gen);
    bool fin = cap.block(r, gen, buf, 512, now(), power, link.bell() || bell_override);
    if (fin) link.finished();
    link.leave();
    if (on_block) on_block(*this);
    blk++;
    if (fin) finished = true;
    return fin;
  }
  void run(int blocks) {
    for (int i = 0; i < blocks; i++) step();
  }
  // A lossless, instant ACK: base = every chunk sent so far.
  static void ack_all(Harness &h) {
    if (h.rec) h.rec->base.store(h.rec->first_sent.load());
  }
  int count(VType t) const {
    int n = 0;
    for (const Sent &s : sent) n += s.m.type == t;
    return n;
  }
  const Sent *find_end() const {
    for (const Sent &s : sent)
      if (s.m.type == VType::End) return &s;
    return nullptr;
  }
};

// Room noise at about -48 dB per block power, plus speech-like tone bursts between blocks a and b.
std::function<void(int, int16_t *)> source(int speech_from, int speech_to, int room_amp = 600, int tone_amp = 7000) {
  auto rng = std::make_shared<Lcg>(77);
  return [=](int blk, int16_t *b) {
    for (int i = 0; i < 512; i++) {
      int v = rng->sample(room_amp);
      if (blk >= speech_from && blk < speech_to) {
        double t = (blk * 512 + i) / 44100.0;
        v += (int)(tone_amp * (sin(kTwoPi * 220 * t) + 0.5 * sin(kTwoPi * 880 * t)) / 1.5);
      }
      b[2 * i] = b[2 * i + 1] = (int16_t)v;
    }
  };
}

// The chunks the receiver would have, by idx, from the DATA frames sent.
std::vector<VoiceChunk> received_chunks(const Harness &h, int &highest) {
  std::vector<VoiceChunk> c(kVoiceMaxChunks);
  highest = -1;
  for (const Sent &s : h.sent) {
    if (s.m.type != VType::Data) continue;
    int i = s.m.u.data.idx;
    c[i].pred = s.m.u.data.pred;
    c[i].step = s.m.u.data.step;
    c[i].len = s.m.u.data.len;
    memcpy(c[i].data, s.m.u.data.data, s.m.u.data.len);
    if (i > highest) highest = i;
  }
  return c;
}

// Decodes what was sent into PCM (state restarted from each chunk's own header).
std::vector<int16_t> decode_received(const Harness &h, int n_samples) {
  int hi;
  std::vector<VoiceChunk> c = received_chunks(h, hi);
  std::vector<int16_t> pcm;
  int left = n_samples;
  for (int i = 0; i <= hi && left > 0; i++) {
    int n = left < 474 ? left : 474;
    int16_t out[474];
    voice_adpcm_decode(c[i].pred, c[i].step, c[i].data, n, out);
    pcm.insert(pcm.end(), out, out + n);
    left -= n;
  }
  return pcm;
}

}  // namespace

VTEST(dsp_capture_countdown_full_run) {
  Harness h;
  h.make_rec();
  h.src = source(40, 200);  // speech from 0.46 s to 2.3 s
  h.on_block = Harness::ack_all;
  h.run(8);  // 93 ms of room before the press fills the pre-roll ring
  CHECK(h.cap.recordings() == 0);
  h.press();
  h.run(2);  // the LVGL side was two blocks late: the pre-roll covers them
  for (int i = 0; i < 400 && !h.finished; i++) h.step();
  CHECK(h.finished);
  VoiceRec *r = h.rec;
  CHECK(r->stopped.load() == (uint8_t)VoiceStop::Countdown);
  CHECK(r->done.load() && r->end_sent.load());
  CHECK(h.cap.preroll_used() >= 2);
  CHECK(h.cap.last_stop() == VoiceStop::Countdown);
  // Time: the stop came within one block of press + 3.0 s.
  int stopped_blk = h.blk - 1;
  uint32_t t_stop = kT0 + (uint32_t)((uint64_t)stopped_blk * 512 * 1000 / 44100);
  CHECK(t_stop >= h.press_ms + kVoiceRecMs && t_stop < h.press_ms + kVoiceRecMs + 14);
  // Size: at most 104 chunks (the ring's 60 ms lead can lift it past 102 to 104).
  int n_chunks = r->chunks_ready.load();
  CHECK(n_chunks >= 102 && n_chunks <= kVoiceClipChunks);
  int n_samples = r->n_samples.load();
  CHECK(n_samples >= 47900 && n_samples <= n_chunks * 474);
  CHECK(r->speech.load());
  // Frames: DATA 0..n-1 in order, each once, then one END, with the right numbers.
  CHECK(h.count(VType::Data) == n_chunks);
  CHECK(h.count(VType::End) == 1);
  int next = 0;
  for (const Sent &s : h.sent) {
    if (s.m.type == VType::Data) {
      CHECK(s.m.u.data.idx == next);
      CHECK(s.m.u.data.xfer == 7 && s.m.floor == 1 && s.m.sender == kSenderDownstairs);
      next++;
    } else {
      CHECK(next == n_chunks);  // END is after the last DATA
    }
  }
  const Sent *e = h.find_end();
  CHECK(e && e->m.u.end.n_chunks == n_chunks && e->m.u.end.n_samples == n_samples && e->m.u.end.speech == 1);
  // The mic task keeps being called after done and does nothing.
  size_t before = h.sent.size();
  h.run(10);
  CHECK(h.sent.size() == before);
  // Release hands it back: busy is clear, so true at once.
  CHECK(h.link.release(r));
  CHECK(!h.link.recording());
}

VTEST(dsp_capture_audio_survives_the_pipeline) {
  Harness h;
  h.cap.set_boost_db(0);  // the level boost has its own test (dsp_capture_boost_*)
  h.cap.set_treble_db(0);  // and so has the treble restore (dsp_treble_*)
  h.make_rec();
  // A steady 1 kHz tone, 6000 amplitude (both channels), no noise.
  h.src = [](int blk, int16_t *b) {
    for (int i = 0; i < 512; i++) b[2 * i] = b[2 * i + 1] = (int16_t)lrint(6000 * sin(kTwoPi * 1000 * (blk * 512 + i) / 44100.0));
  };
  h.on_block = Harness::ack_all;
  h.run(8);
  h.press();
  for (int i = 0; i < 400 && !h.finished; i++) h.step();
  int n = h.rec->n_samples.load();
  std::vector<int16_t> pcm = decode_received(h, n);
  CHECK((int)pcm.size() == n);
  double e = rms_of(pcm.data() + 2000, n - 4000);  // skip the filter and ADPCM settling
  CHECK(fabs(20 * log10(e / (6000 / sqrt(2.0)))) < 1.0);
  // Frequency: zero crossings of the decoded clip, 1 kHz.
  int cross = 0;
  for (int i = 2001; i < n - 2000; i++) cross += (pcm[i - 1] < 0) != (pcm[i] < 0);
  double hz = cross / 2.0 / ((n - 4001) / 16000.0);
  CHECK(fabs(hz - 1000) < 15);
}

VTEST(dsp_capture_window_holds_and_order) {
  Harness h;
  h.make_rec();
  h.src = source(30, 400);  // speech all through, so no early stop
  // No ACKs: base stays 0. Nothing may go out past chunk 31, and END must wait.
  h.run(6);
  h.press();
  for (int i = 0; i < 330; i++) h.step();  // countdown ends at 258 blocks; run on past it
  CHECK(h.rec->stopped.load() == (uint8_t)VoiceStop::Countdown);
  CHECK(h.rec->first_sent.load() == 32);
  CHECK(h.count(VType::Data) == 32);
  CHECK(h.count(VType::End) == 0);
  CHECK(!h.rec->done.load() && !h.finished);
  CHECK(h.cap.window_holds() > 0);
  for (const Sent &s : h.sent) CHECK(voice_in_window(s.m.u.data.idx, 0));
  // base moves to 10: chunks 32..41 may go, no more.
  h.rec->base.store(10);
  h.run(5);
  CHECK(h.rec->first_sent.load() == 42);
  CHECK(h.count(VType::Data) == 42);
  CHECK(h.count(VType::End) == 0);
  // base keeps up chunk by chunk (as the C6 ACKs would): every first send stays inside the window.
  int max_ahead = 0;
  h.on_block = [&](Harness &x) {
    int fs = x.rec->first_sent.load();
    int base = x.rec->base.load();
    if (fs - base > max_ahead) max_ahead = fs - base;
    x.rec->base.store((uint8_t)(fs < base + 3 ? fs : base + 3));
  };
  for (int i = 0; i < 100 && !h.finished; i++) h.step();
  CHECK(h.finished);
  CHECK(max_ahead <= kVoiceWindow);
  int n = h.rec->chunks_ready.load();
  CHECK(h.count(VType::Data) == n);
  int next = 0;
  for (const Sent &s : h.sent) {
    if (s.m.type == VType::Data) CHECK(s.m.u.data.idx == next++);
    else CHECK(next == n);
  }
  CHECK(h.count(VType::End) == 1);
}

VTEST(dsp_capture_no_burst_after_the_window_opens) {
  Harness h;
  h.make_rec();
  h.src = source(30, 400);
  h.run(6);
  h.press();
  for (int i = 0; i < 150; i++) h.step();  // 36 chunks made, 32 sent, window shut
  CHECK(h.rec->first_sent.load() == 32);
  h.rec->base.store(32);
  size_t before = h.sent.size();
  h.step();
  CHECK(h.sent.size() - before <= (size_t)kVoiceSendsPerBlock);
}

VTEST(dsp_capture_release_flushes_the_last_chunk) {
  Harness h;
  h.make_rec();
  h.src = source(10, 400);
  h.on_block = Harness::ack_all;
  h.run(6);
  h.press();
  h.run(100);  // 1.16 s
  h.link.stop(h.rec, VoiceStop::Release);
  h.link.stop(h.rec, VoiceStop::Cancel);  // the first request wins
  CHECK(h.rec->stop.load() == (uint8_t)VoiceStop::Release);
  h.step();
  CHECK(h.finished);
  CHECK(h.rec->stopped.load() == (uint8_t)VoiceStop::Release);
  int n_samples = h.rec->n_samples.load();
  int n_chunks = h.rec->chunks_ready.load();
  CHECK(n_chunks == (n_samples + 473) / 474);
  CHECK(n_samples % 474 != 0);  // the last chunk is partial
  const Sent *e = h.find_end();
  CHECK(e && e->m.u.end.n_chunks == n_chunks && e->m.u.end.n_samples == n_samples);
  CHECK(h.count(VType::Data) == n_chunks);
  int hi;
  std::vector<VoiceChunk> c = received_chunks(h, hi);
  CHECK(hi == n_chunks - 1);
  CHECK(c[hi].len == (n_samples - 474 * hi + 1) / 2);
  // Total decodes to n_samples.
  CHECK((int)decode_received(h, n_samples).size() == n_samples);
}

VTEST(dsp_capture_early_stop_on_quiet_after_speech) {
  Harness h;
  h.make_rec();
  h.src = source(30, 100);  // speech blocks 30-99, quiet after
  h.on_block = Harness::ack_all;
  h.run(8);
  h.press();
  for (int i = 0; i < 400 && !h.finished; i++) h.step();
  CHECK(h.rec->stopped.load() == (uint8_t)VoiceStop::Quiet);
  int quiet_blocks = (h.blk - 1) - 100 + 1;  // blocks after the last speech block, up to the stop
  CHECK(quiet_blocks >= 68 && quiet_blocks <= 75);
  const Sent *e = h.find_end();
  CHECK(e && e->m.u.end.speech == 1);
  CHECK(h.rec->n_samples.load() < 48000);
  // With early stop off (a serial test) it runs to the countdown.
  Harness g;
  g.make_rec(kVoiceClipChunks, false);
  g.src = source(30, 100);
  g.on_block = Harness::ack_all;
  g.run(8);
  g.press();
  for (int i = 0; i < 400 && !g.finished; i++) g.step();
  CHECK(g.rec->stopped.load() == (uint8_t)VoiceStop::Countdown);
}

VTEST(dsp_capture_no_speech_says_so) {
  Harness h;
  h.make_rec();
  h.src = source(100000, 100001);  // room only
  h.on_block = Harness::ack_all;
  h.run(8);
  h.press();
  for (int i = 0; i < 400 && !h.finished; i++) h.step();
  CHECK(h.rec->stopped.load() == (uint8_t)VoiceStop::Countdown);  // never stopped early
  const Sent *e = h.find_end();
  CHECK(e && e->m.u.end.speech == 0 && !h.rec->speech.load());
}

VTEST(dsp_capture_bell_and_cancel_send_no_end) {
  for (int which = 0; which < 2; which++) {
    Harness h;
    h.make_rec();
    h.src = source(10, 400);
    h.on_block = Harness::ack_all;
    h.run(6);
    h.press();
    h.run(80);
    size_t before = h.sent.size();
    CHECK(before > 0);
    if (which == 0) h.link.ring_bell();       // the doorbell (audio_play / audio_task)
    else h.link.stop(h.rec, VoiceStop::Cancel);
    bool fin = h.step();
    CHECK(fin && h.finished);
    CHECK(h.rec->stopped.load() == (uint8_t)(which == 0 ? VoiceStop::Bell : VoiceStop::Cancel));
    CHECK(h.rec->done.load() && !h.rec->end_sent.load());
    h.run(20);
    CHECK(h.sent.size() == before);  // no last chunk, no END, nothing more
    CHECK(h.count(VType::End) == 0);
    CHECK(!h.link.recording());       // the mic task said it is finished
    CHECK(h.link.bell() == (which == 0));
    CHECK(h.link.release(h.rec));
    CHECK(!h.link.bell());            // release clears the bell flag
  }
}

VTEST(dsp_capture_refused_sends_are_retried_in_order) {
  Harness h;
  h.make_rec();
  h.src = source(10, 400);
  h.on_block = Harness::ack_all;
  h.run(6);
  h.press();
  h.refuse = 2;  // the radio refuses the first two tries: the same chunk goes on the next blocks
  for (int i = 0; i < 400 && !h.finished; i++) h.step();
  CHECK(h.finished);
  CHECK(h.cap.send_retries() >= 2);
  int n = h.rec->chunks_ready.load();
  CHECK(h.count(VType::Data) == n);
  int next = 0;
  for (const Sent &s : h.sent)
    if (s.m.type == VType::Data) CHECK(s.m.u.data.idx == next++);
  CHECK(h.count(VType::End) == 1);
}

VTEST(dsp_capture_a_chunk_the_radio_always_refuses_is_left_to_the_resend_path) {
  Harness h;
  h.make_rec();
  h.src = source(10, 400);
  h.on_block = Harness::ack_all;
  h.refuse_idx = 5;
  h.run(6);
  h.press();
  for (int i = 0; i < 400 && !h.finished; i++) h.step();
  CHECK(h.finished);  // the stream and END went on
  int n = h.rec->chunks_ready.load();
  CHECK(h.rec->first_sent.load() == n);
  CHECK(h.count(VType::Data) == n - 1);  // chunk 5 was never sent: LVGL resends it (first_ms is set)
  CHECK(h.rec->first_ms[5] != 0);
  CHECK(h.count(VType::End) == 1);
}

VTEST(dsp_capture_full_array_stops_the_recording) {
  Harness h;
  h.make_rec(10);
  h.src = source(10, 400);
  h.on_block = Harness::ack_all;
  h.run(6);
  h.press();
  for (int i = 0; i < 100 && !h.finished; i++) h.step();
  CHECK(h.finished);
  CHECK(h.rec->stopped.load() == (uint8_t)VoiceStop::Full);
  CHECK(h.rec->chunks_ready.load() == 10 && h.rec->n_samples.load() == 4740);
  const Sent *e = h.find_end();
  CHECK(e && e->m.u.end.n_chunks == 10 && e->m.u.end.n_samples == 4740);
}

VTEST(dsp_capture_mulaw_codec) {
  Harness h;
  h.make_rec(kVoiceMaxChunks, true, kSenderGateway, 1);
  h.src = source(10, 400);
  h.on_block = Harness::ack_all;
  h.run(6);
  h.press();
  for (int i = 0; i < 100; i++) h.step();
  h.link.stop(h.rec, VoiceStop::StopKey);
  h.step();
  CHECK(h.finished);
  int n = h.rec->chunks_ready.load();
  CHECK(n == (h.rec->n_samples.load() + 236) / 237);
  for (const Sent &s : h.sent)
    if (s.m.type == VType::Data) CHECK(s.m.u.data.pred == 0 && s.m.u.data.step == 0);
}

VTEST(dsp_capture_serial_dump_mode_sends_nothing) {
  Harness h;
  h.make_rec(kVoiceClipChunks, false, kVoiceToNone);
  h.src = source(10, 400);
  h.run(6);
  h.press();  // no ACKs ever arrive and no window applies
  for (int i = 0; i < 400 && !h.finished; i++) h.step();
  CHECK(h.finished);
  CHECK(h.sent.empty());
  CHECK(h.rec->end_sent.load() && h.rec->done.load());
  CHECK(h.rec->first_sent.load() == h.rec->chunks_ready.load());
  CHECK(h.rec->chunks_ready.load() >= 102);
}

VTEST(dsp_capture_second_recording_at_the_same_address_starts_fresh) {
  Harness h;
  h.make_rec();
  h.src = source(10, 400);
  h.on_block = Harness::ack_all;
  h.run(6);
  h.press();
  h.run(50);
  h.link.stop(h.rec, VoiceStop::Release);
  h.step();
  CHECK(h.finished);
  CHECK(h.link.release(h.rec));
  h.step();  // the mic task sees no VoiceRec: the pre-roll fills again
  h.step();
  size_t first = h.sent.size();
  // A new recording in the same memory, as alloc_big can hand it back.
  h.sent.clear();
  h.finished = false;
  std::vector<uint8_t> keep = h.mem;
  h.make_rec();  // same size, same address (assign reuses the buffer)
  h.rec->xfer = 8;
  h.press();
  h.run(60);
  CHECK(first > 0);
  CHECK(h.cap.recordings() == 2);
  CHECK(!h.sent.empty() && h.sent[0].m.type == VType::Data && h.sent[0].m.u.data.idx == 0 && h.sent[0].m.u.data.xfer == 8);
  // A recording begun with no block in between (release then begin inside one block time): the new
  // generation alone tells them apart.
  h.link.stop(h.rec, VoiceStop::Release);
  h.step();
  CHECK(h.link.release(h.rec));
  h.sent.clear();
  h.make_rec();
  h.rec->xfer = 9;
  h.press();
  h.run(60);
  CHECK(h.cap.recordings() == 3);
  CHECK(!h.sent.empty() && h.sent[0].m.u.data.idx == 0 && h.sent[0].m.u.data.xfer == 9);
}

VTEST(dsp_capture_stuck_end_gives_up) {
  Harness h;
  h.make_rec();
  h.src = source(10, 4000);
  h.run(6);
  h.press();  // no ACKs: the window shuts at 32 and END never goes out
  for (int i = 0; i < 1200 && !h.finished; i++) h.step();  // 13.9 s
  CHECK(h.finished);
  CHECK(h.rec->done.load() && !h.rec->end_sent.load());
  CHECK(h.count(VType::End) == 0);
  CHECK(h.blk * 512 * 1000 / 44100 >= (int)kVoiceStuckMs);
}

VTEST(dsp_capture_pre_roll_window) {
  // A tone starts 40 ms before the press: the pre-roll puts it in the clip.
  Harness h;
  h.make_rec();
  h.src = [](int blk, int16_t *b) {
    for (int i = 0; i < 512; i++) {
      int v = blk >= 7 ? (int)(8000 * sin(kTwoPi * 500 * (blk * 512 + i) / 44100.0)) : 0;
      b[2 * i] = b[2 * i + 1] = (int16_t)v;
    }
  };
  h.on_block = Harness::ack_all;
  h.run(8);  // blocks 0-7; the tone began at block 7
  h.press(); // press at block 8 (t = 92.8 ms): block 7 arrived 11.6 ms earlier, inside the 60 ms lead
  h.step();
  CHECK(h.cap.preroll_used() >= 1);
  // Blocks older than the lead stay out: the ring held 6 blocks (blocks 2-7), arrivals 23 to 81 ms
  // after the start; press is 92 ms after it, so from = press - 60 ms includes arrivals >= 32 ms.
  CHECK(h.cap.preroll_used() <= 5);
}

// ---- The handshake ----

VTEST(dsp_link_basics) {
  VoiceHandshake l;
  std::vector<uint8_t> a(voice_rec_bytes(4) + 16), b(voice_rec_bytes(4) + 16);
  VoiceRec *ra = voice_rec_init(a.data(), a.size(), 4), *rb = voice_rec_init(b.data(), b.size(), 4);
  CHECK(!l.begin(nullptr));
  CHECK(l.begin(ra));
  CHECK(l.recording());
  CHECK(!l.begin(rb));  // one at a time; rb is still the caller's
  uint32_t g1 = 0;
  CHECK(l.enter(g1) == ra);
  CHECK(!l.release(ra));  // the mic task is inside: not yet
  l.leave();
  uint32_t g2 = 0;
  CHECK(l.enter(g2) == nullptr);  // released from the mic task's point of view already
  l.leave();
  CHECK(l.release(ra));
  CHECK(!l.recording());
  CHECK(l.release(ra));   // again: harmless
  CHECK(l.begin(rb));
  uint32_t g3 = 0;
  CHECK(l.enter(g3) == rb);
  l.leave();
  CHECK(g3 != g1);        // a new generation each begin
  CHECK(l.release(ra));   // a stale release of the first one does not touch the second
  CHECK(l.recording());
  l.stop(ra, VoiceStop::Cancel);  // not the owner: ignored
  CHECK(rb->stop.load() == 0 && ra->stop.load() == 0);
  l.stop(rb, VoiceStop::StopKey);
  l.stop(rb, VoiceStop::Release);
  CHECK(rb->stop.load() == (uint8_t)VoiceStop::StopKey);
  l.ring_bell();
  CHECK(l.bell());
  l.finished();
  CHECK(!l.recording());
  CHECK(l.release(rb));
  CHECK(!l.bell() && !l.recording());
  // A ring_bell() that lands just after release (audio_play saw recording() true a moment before)
  // must not end the next recording at once.
  l.ring_bell();
  CHECK(l.begin(ra));
  CHECK(!l.bell());
  CHECK(l.release(ra));
}

VTEST(dsp_link_threaded_release_never_overlaps_a_use) {
  // The mic task thread brackets every use with enter/leave and checks the VoiceRec it holds is
  // intact; the main thread publishes, releases, and poisons the memory the moment release says
  // true. Any overlap shows as a poisoned field seen inside enter..leave.
  const int kRecs = 4, kCycles = 4000;
  VoiceHandshake link;
  std::vector<std::vector<uint8_t>> mem(kRecs);
  VoiceRec *rec[kRecs];
  for (int i = 0; i < kRecs; i++) {
    mem[i].assign(voice_rec_bytes(8) + 16, 0);
    rec[i] = voice_rec_init(mem[i].data(), mem[i].size(), 8);
  }
  std::atomic<bool> quit{false};
  std::atomic<long> poisoned{0}, seen{0};
  std::thread mic([&]() {
    while (!quit.load()) {
      uint32_t gen;
      VoiceRec *r = link.enter(gen);
      if (r) {
        for (int k = 0; k < 20; k++) {
          if (r->xfer != 7 || r->max_chunks != 8 || r->chunks == nullptr) poisoned++;
          r->level.store((uint8_t)k);
          std::this_thread::yield();
        }
        seen++;
      }
      link.leave();
      std::this_thread::yield();
    }
  });
  for (int c = 0; c < kCycles; c++) {
    VoiceRec *r = rec[c % kRecs];
    r->xfer = 7;
    CHECK(link.begin(r));
    long target = seen.load() + 1;
    for (int w = 0; w < 200 && seen.load() < target; w++) std::this_thread::yield();
    int spins = 0;
    while (!link.release(r)) {
      std::this_thread::yield();
      if (++spins > 10000000) break;
    }
    r->xfer = 0xDD;  // poison, as free_big would
    r->max_chunks = 0xDD;
    r->chunks = nullptr;
    // Put it back together for the next lap.
    for (volatile int i = 0; i < 50; i++) {}
    r->max_chunks = 8;
    r->chunks = (VoiceChunk *)(mem[c % kRecs].data() + ((sizeof(VoiceRec) + 7) & ~(size_t)7));
  }
  quit.store(true);
  mic.join();
  CHECK(poisoned.load() == 0);
  CHECK(seen.load() > 50);  // the mic thread really did use them
}

// ---- Ding ----

VTEST(dsp_ding_is_one_g5_strike) {
  CHECK((int)Sound::Ding == 5 && (int)Sound::Count == 6 && kResidentSounds == 5);
  CHECK(synth_voices(Sound::Ding, 0) == 3);  // the bar's three partials, one strike
  size_t n = synth_frames(Sound::Ding, 0);
  CHECK(n > (size_t)(0.70 * 44100) && n < (size_t)(0.80 * 44100));
  std::vector<int16_t> pcm(n);
  CHECK(synth(Sound::Ding, 0, pcm.data(), n));
  int peak = 0;
  for (int16_t s : pcm) peak = abs((int)s) > peak ? abs((int)s) : peak;
  CHECK(peak > 21500 && peak < 22300);  // -3.5 dBFS like every sound
  // The pitch: zero crossings over the first 150 ms, G5 = 783.99 Hz (the partials add, so allow 3%).
  int cross = 0, from = 2000, to = 2000 + 6615;
  for (int i = from + 1; i < to; i++) cross += ((pcm[i - 1] < 0) != (pcm[i] < 0));
  double hz = cross / 2.0 / ((to - from - 1) / 44100.0);
  CHECK(hz > 783.99 * 0.95 && hz < 783.99 * 1.05);
  // One strike: it dies away (the last quarter is much quieter than the first tenth).
  double head = rms_of(pcm.data(), (int)(n / 10)), tail = rms_of(pcm.data() + n * 3 / 4, (int)(n / 4));
  CHECK(tail < head * 0.5);
  // Not the dog alert's note (G6): the dog alert's first strike is an octave up.
  size_t dn = synth_frames(Sound::DogAlert, 0);
  CHECK(dn > n);
}

// ---- Level boost and the live meter (field fix 2026-11-21) ----

VTEST(dsp_boost_gain_and_soft_limit) {
  VoiceBoost b;  // the default
  CHECK(fabs(b.db() - kVoiceBoostDb) < 1e-6 && fabs(b.gain() - pow(10.0, 15.0 / 20.0)) < 1e-3);
  // Below the knee: exactly the gain (rounded).
  for (int s : {0, 1, -1, 100, -100, 1000, -2500, 2800}) {
    int want = (int)lrint(s * b.gain());
    CHECK(abs(b.apply((int16_t)s) - want) <= 1);
  }
  // Above the knee: never past the ceiling, never clipped, monotonic, and continuous at the knee.
  int last = -40000;
  for (int s = -32768; s <= 32767; s += 7) {
    int y = b.apply((int16_t)s);
    CHECK(y >= -(int)kVoiceLimitCeiling && y <= (int)kVoiceLimitCeiling);
    CHECK(y >= last);
    last = y;
  }
  CHECK(b.apply(32767) <= 30000 && b.apply(-32768) >= -30000);
  CHECK(b.apply(32767) > 29000);  // full scale in is still loud out
  int k = (int)(kVoiceLimitKnee / b.gain());
  CHECK(abs(b.apply((int16_t)(k + 1)) - b.apply((int16_t)k)) <= 8);
  CHECK(b.limited() > 0);
  // 0 dB leaves the samples alone, bit for bit; the setting is clamped to 0-30 dB.
  VoiceBoost z(0);
  int16_t pcm[5] = {32767, -32768, 1, -1, 12345};
  z.process(pcm, 5);
  CHECK(pcm[0] == 32767 && pcm[1] == -32768 && pcm[2] == 1 && pcm[3] == -1 && pcm[4] == 12345 && z.limited() == 0);
  VoiceBoost hi(99), neg(-5);
  CHECK(hi.db() == (float)kVoiceBoostMaxDb && neg.db() == 0.0f && neg.gain() == 1.0f);
}

// Speech at the field level (-40 dBFS RMS, peaks near -22) comes out 15 dB louder through the real
// capture path and the mu-law encoder, and a loud clip never hits full scale.
VTEST(dsp_capture_boost_through_mulaw) {
  for (int loud = 0; loud < 2; loud++) {
    Harness h;
    h.cap.set_treble_db(0);  // the +15 dB figure is the boost alone
    h.make_rec(kVoiceClipChunksMulaw, false, kSenderGateway, 1);
    const int amp = loud ? 30000 : 260;  // a 1 kHz tone: about -42 dBFS RMS, or nearly full scale
    h.src = [amp](int blk, int16_t *b) {
      for (int i = 0; i < 512; i++) b[2 * i] = b[2 * i + 1] = (int16_t)lrint(amp * sin(kTwoPi * 1000 * (blk * 512 + i) / 44100.0));
    };
    h.on_block = Harness::ack_all;
    h.run(8);
    h.press();
    for (int i = 0; i < 400 && !h.finished; i++) h.step();
    CHECK(h.finished && h.rec->stopped.load() == (uint8_t)VoiceStop::Countdown);
    int n = h.rec->n_samples.load();
    int hi;
    std::vector<VoiceChunk> c = received_chunks(h, hi);
    std::vector<int16_t> pcm;
    for (int i = 0; i <= hi; i++)
      for (int k = 0; k < c[i].len && (int)pcm.size() < n; k++) pcm.push_back(voice_mulaw_decode(c[i].data[k]));
    CHECK((int)pcm.size() == n);
    if ((int)pcm.size() != n || n < 6000) continue;
    double e = rms_of(pcm.data() + 2000, n - 4000);
    int peak = 0;
    for (int16_t s : pcm) peak = abs((int)s) > peak ? abs((int)s) : peak;
    if (!loud) {
      CHECK(fabs(20 * log10(e / (amp / sqrt(2.0))) - 15.0) < 1.0);  // +15 dB, within mu-law's step
      CHECK(h.cap.last_limited() == 0);
    } else {
      CHECK(peak < 32124);           // never mu-law's top code (32124): the limiter keeps the peaks under it
      CHECK(peak > 27000);
      CHECK(h.cap.last_limited() > 1000);
    }
    // 3.0 s of mu-law is about 203 chunks; the clip has room for 210.
    int chunks = h.rec->chunks_ready.load();
    CHECK(chunks >= 202 && chunks <= kVoiceClipChunksMulaw);
    for (const Sent &s : h.sent)
      if (s.m.type == VType::Data) CHECK(s.m.u.data.pred == 0 && s.m.u.data.step == 0);
  }
  // The boost setting is read at each recording's start.
  Harness g;
  g.cap.set_boost_db(6);
  CHECK(fabs(g.cap.boost_db() - 6.0f) < 1e-6);
}

// ---- Treble restore (field finding 2026-11-21) ----

static double treble_gain_db(VoiceTreble &t, double hz) {
  const int n = 16000;
  std::vector<int16_t> x(n);
  for (int i = 0; i < n; i++) x[i] = (int16_t)lrint(4000 * sin(kTwoPi * hz * i / 16000.0));
  std::vector<int16_t> y = x;
  t.reset();
  t.process(y.data(), n);
  return 20 * log10(rms_of(y.data() + 4000, n - 4000) / rms_of(x.data() + 4000, n - 4000));
}

VTEST(dsp_treble_shelf_gain) {
  VoiceTreble t;
  CHECK(t.db() == kVoiceTrebleDb);
  CHECK(fabs(treble_gain_db(t, 200)) < 0.5);
  double g6 = treble_gain_db(t, 6000);
  CHECK(fabs(g6 - 10.0) < 1.0);
  double g15 = treble_gain_db(t, 1500);
  CHECK(g15 > 3.0 && g15 < 7.0);  // about half the shelf at its corner
  VoiceTreble big(99), neg(-4);
  CHECK(big.db() == (float)kVoiceTrebleMaxDb && neg.db() == 0.0f);
}

VTEST(dsp_treble_bypass_is_bit_exact) {
  VoiceTreble t(0);
  std::vector<int16_t> x(2000), y;
  for (int i = 0; i < 2000; i++) x[i] = (int16_t)(((i * 7919) % 65536) - 32768);
  y = x;
  t.process(y.data(), (int)y.size());
  CHECK(y == x);
}

// A loud clip through the real capture and mu-law with the treble on never reaches the top code.
VTEST(dsp_capture_treble_through_mulaw) {
  Harness h;  // treble and boost at their defaults
  h.make_rec(kVoiceClipChunksMulaw, false, kSenderGateway, 1);
  h.src = [](int blk, int16_t *b) {
    for (int i = 0; i < 512; i++) {
      double t = (blk * 512 + i) / 44100.0;
      b[2 * i] = b[2 * i + 1] = (int16_t)lrint(18000 * sin(kTwoPi * 1000 * t) + 14000 * sin(kTwoPi * 5000 * t));
    }
  };
  h.on_block = Harness::ack_all;
  h.run(8);
  h.press();
  for (int i = 0; i < 400 && !h.finished; i++) h.step();
  CHECK(h.finished);
  int n = h.rec->n_samples.load(), hi;
  std::vector<VoiceChunk> c = received_chunks(h, hi);
  int peak = 0, got = 0;
  for (int i = 0; i <= hi; i++)
    for (int k = 0; k < c[i].len && got < n; k++, got++) {
      int v = abs((int)voice_mulaw_decode(c[i].data[k]));
      peak = v > peak ? v : peak;
    }
  CHECK(got == n && n > 6000);
  CHECK(peak < 32124 && peak > 27000);
  CHECK(h.cap.treble_db() == kVoiceTrebleDb);
  h.cap.set_treble_db(7);
  CHECK(fabs(h.cap.treble_db() - 7.0f) < 1e-6);
}

VTEST(dsp_meter_level_mapping) {
  CHECK(voice_meter_level(-99) == 0 && voice_meter_level(kVoiceMeterLowDb) == 0);
  CHECK(voice_meter_level(kVoiceMeterHighDb) == 100 && voice_meter_level(0) == 100);
  CHECK(voice_meter_level(-36) == 50);
  CHECK(voice_meter_level(-48) < voice_meter_level(-24));
  float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK(voice_meter_level(nan) == 0);
}
