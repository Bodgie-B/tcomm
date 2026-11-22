// Voice notes, piece B: the link (core/voice_tx), the inbox (core/voice_inbox) and the controller's
// voice paths, against a simulated house: two Controllers, a reference C6 and a lossy air.
//
// Two C6s:
//  - RefC6, a reference model written from DESIGN 4.4, 5.1 and 5.2 (one session per floor, VOICE_ACK on
//    BEGIN, every 8 new chunks, 80 ms after a gap and on END, state 2 only with every chunk, the last
//    completed xfer kept per floor, replies by the way the session's last frame came, PING answered
//    straight back, PUSH answered with GOT 28, a canned "Pi" sending VOICE_TEXT until VOICE_GOT). It has
//    switches for the failure cases (no Pi, END lost, Queued, no ping answers).
//  - RealC6: piece D's own gw::Gateway (../gateway/gateway-c6/src/gw: gateway.cpp with the VoiceSession of
//    voice_session.cpp) behind a GatewayIO, with a fake Pi on its serial lines (hello with voice, a
//    voice_text request after each voice_end, voice_ack for a phone's Got it). The gw_* tests run the
//    main scenarios against it.
// The test_voice line of tools/build_sim.sh links tests/test_voice_*.cpp, the core files and
// voice_session.cpp only, so this file compiles the controller and the rest of the gateway into itself
// (the #includes of .cpp files at the end).
//
// The upstairs relay is modelled as board_radio's on_recv + vrelay task: a voice frame arriving at
// upstairs that tc::voice_relay_frame says to pass on gets upstairs' sender byte and goes to the next
// hop without reaching the controller. The mic task is modelled from the VoiceRec contract
// (voice_proto.h): a chunk every 29.6 ms from the press, first sends in order and never 32 or more past
// rec->base, END after every first send, done.
//
// Test text is synthetic.
#include "voice_test.h"
#include <map>
#include <string.h>
#include "core/brightness.h"
#include "core/voice_dsp.h"
#include "core/voice_inbox.h"
#include "core/voice_tx.h"
#include "gw/gateway.h"

using namespace tc;

namespace {

// ---------------------------------------------------------------------------------------------
// Deterministic randomness

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  bool chance(int pct) { return pct > 0 && (int)(next() % 100) < pct; }
  int upto(int n) { return n > 0 ? (int)(next() % (uint32_t)(n + 1)) : 0; }
};

uint32_t g_now = 0;

// ---------------------------------------------------------------------------------------------
// The air: frames between the three boards with loss, duplicates and reordering (random delay).

struct Pkt {
  uint32_t at;
  uint32_t order;
  uint8_t from, to;
  std::vector<uint8_t> d;
};

struct Air {
  Rng rng{7};
  int drop = 0, dup = 0, jitter = 0;  // percent, percent, extra ms (reorders)
  int voice_only_loss = 1;            // loss and duplicates for voice frames only (the plain link stays clean)
  bool cut[3][3] = {};                // from -> to dead
  std::vector<Pkt> q;
  uint32_t order = 0;
  // What went on the air, for the checks.
  struct Seen {
    uint32_t at;
    uint8_t from, to;
    std::vector<uint8_t> d;
  };
  std::vector<Seen> log;

  void send(uint8_t from, uint8_t to, const uint8_t *d, size_t n) {
    log.push_back({g_now, from, to, std::vector<uint8_t>(d, d + n)});
    if (to > 2 || cut[from][to]) return;
    bool voice = is_voice_frame(d, n);
    bool lossy = voice || !voice_only_loss;
    if (lossy && rng.chance(drop)) return;
    Pkt p{g_now + 2 + (uint32_t)(lossy ? rng.upto(jitter) : 0), order++, from, to, std::vector<uint8_t>(d, d + n)};
    q.push_back(p);
    if (lossy && rng.chance(dup)) {
      p.at += 1 + (uint32_t)rng.upto(jitter);
      p.order = order++;
      q.push_back(p);
    }
  }
  // The next frame due by now (earliest first, then in send order), if any.
  bool pop(Pkt &out) {
    int best = -1;
    for (int i = 0; i < (int)q.size(); i++) {
      if (ms_diff(g_now, q[i].at) < 0) continue;
      if (best < 0 || ms_diff(q[best].at, q[i].at) > 0 || (q[i].at == q[best].at && q[i].order < q[best].order)) best = i;
    }
    if (best < 0) return false;
    out = q[best];
    q.erase(q.begin() + best);
    return true;
  }
  int count(uint8_t from, uint8_t to, VType t) const {
    int n = 0;
    for (const Seen &s : log)
      if (s.from == from && s.to == to && s.d.size() >= 2 && s.d[1] == (uint8_t)t) n++;
    return n;
  }
};

// ---------------------------------------------------------------------------------------------
// The mic task, from the VoiceRec contract.

struct FakeMic {
  VoiceRec *rec = nullptr;
  uint8_t me = 0;
  Air *air = nullptr;
  bool speech = true;
  uint32_t release_after_ms = 0;  // 0: until the countdown or a stop; else a hold released this long after the press
  int window_violations = 0;
  int busy_releases = 0;          // voice_rec_release answers false this many times first
  uint16_t seq = 0;
  uint32_t samples_done = 0;
  VoiceRateCap *cap = nullptr;  // the panel's air cap (Panel::capped), or none
  int *cap_refused = nullptr;
  int fails = 0;

  bool begin(VoiceRec *r) {
    if (rec) return false;
    rec = r;
    samples_done = 0;
    return true;
  }
  bool release(VoiceRec *r) {
    if (r != rec) return true;
    if (busy_releases > 0) {
      busy_releases--;
      return false;
    }
    rec = nullptr;
    return true;
  }
  bool send(VoiceMsg &m) {
    m.sender = rec->sender;
    m.floor = rec->floor;
    m.seq = ++seq;
    if (cap && !cap->take(g_now)) {
      if (cap_refused) (*cap_refused)++;
      return false;
    }
    uint8_t buf[kMaxMsg];
    size_t n = voice_encode(m, buf, sizeof(buf));
    if (n) air->send(me, rec->to, buf, n);
    return true;
  }
  // A chunk as the codec makes it: mu-law chunks carry pred 0 and step 0 (the C6 refuses anything else).
  void make_chunk(int i, int len) {
    VoiceChunk &c = rec->chunks[i];
    const bool mu = rec->codec == (uint8_t)VoiceCodec::Mulaw;
    c.pred = mu ? (int16_t)0 : (int16_t)(i * 37 - 500);
    c.step = mu ? (uint8_t)0 : (uint8_t)(i % 89);
    c.len = (uint8_t)len;
    for (int k = 0; k < len; k++) c.data[k] = (uint8_t)(i * 13 + k);
  }
  void step() {
    if (!rec || rec->done.load()) return;
    const uint32_t age = (uint32_t)ms_diff(g_now, rec->press_ms);
    uint8_t stopped = rec->stopped.load();
    if (!stopped) {
      uint8_t req = rec->stop.load();
      if (req) stopped = req;
      else if (age >= kVoiceRecMs) stopped = (uint8_t)VoiceStop::Countdown;
      else if (release_after_ms && age >= release_after_ms) stopped = (uint8_t)VoiceStop::Release;
    }
    // Samples captured so far (16 a ms), whole chunks become ready.
    uint32_t want = stopped ? samples_done : (age > kVoiceRecMs ? kVoiceRecMs : age) * 16u;
    if (!stopped) samples_done = want;
    int ready = rec->chunks_ready.load();
    const uint32_t per = rec->chunk_samples;  // 474 ADPCM, 237 mu-law
    const bool mu = rec->codec == (uint8_t)VoiceCodec::Mulaw;
    int whole = (int)(samples_done / per);
    if (whole > rec->max_chunks) {  // the array is full: the capture stops (VoiceStop::Full)
      whole = rec->max_chunks;
      samples_done = (uint32_t)whole * per;
      if (!stopped) stopped = (uint8_t)VoiceStop::Full;
    }
    for (; ready < whole; ready++) make_chunk(ready, (int)kVoiceChunkBytes);
    if (stopped && !rec->stopped.load()) {
      uint32_t rest = samples_done - (uint32_t)whole * per;
      if (rest && ready < rec->max_chunks) {
        make_chunk(ready, (int)(mu ? rest : (rest + 1) / 2));
        ready++;
      }
      rec->stopped.store(stopped);
    }
    rec->chunks_ready.store((uint8_t)ready);
    rec->n_samples.store((uint16_t)samples_done);
    rec->level.store(40);
    if (speech) rec->speech.store(true);
    // First sends, strictly in order, never 32 or more past base (check finding 6).
    for (int fs = rec->first_sent.load(); fs < ready; fs++) {
      if (!voice_in_window(fs, rec->base.load())) break;
      if (fs >= rec->base.load() + kVoiceWindow) window_violations++;
      const VoiceChunk &c = rec->chunks[fs];
      VoiceMsg m;
      m.type = VType::Data;
      m.u.data.xfer = rec->xfer;
      m.u.data.idx = (uint8_t)fs;
      m.u.data.pred = c.pred;
      m.u.data.step = c.step;
      m.u.data.len = c.len;
      memcpy(m.u.data.data, c.data, c.len);
      if (!send(m) && ++fails < kVoiceSendTries) break;  // refused by the air cap: the next step tries again
      fails = 0;
      rec->first_ms[fs] = g_now;
      rec->first_sent.store((uint8_t)(fs + 1));
    }
    if (rec->stopped.load() && rec->first_sent.load() == ready && !rec->end_sent.load()) {
      VoiceMsg m;
      m.type = VType::End;
      m.u.end.xfer = rec->xfer;
      m.u.end.n_chunks = (uint8_t)ready;
      m.u.end.n_samples = (uint16_t)samples_done;
      m.u.end.speech = rec->speech.load() ? 1 : 0;
      if (send(m) || ++fails >= kVoiceSendTries) {
        fails = 0;
        rec->end_sent.store(true);
        rec->done.store(true);
      }
    }
  }
};

// ---------------------------------------------------------------------------------------------
// A panel's platform.

struct Panel : vt::StubPlatform {
  uint8_t id = 0;
  Air *air = nullptr;
  FakeMic mic;
  bool mic_ok = true;
  WallTime wt;
  std::vector<std::string> logs;
  int allocs = 0, frees = 0;
  // board_radio's air cap on every voice frame (kVoicePanelPerS, kVoicePanelBurst), relayed ones included;
  // off unless a test turns it on (cap_on()).
  bool capped = false;
  VoiceRateCap cap{kVoicePanelPerS, kVoicePanelBurst};
  int cap_refused = 0;
  // The live meter's source (Platform::mic_meter): the peak the mic task would report, or none.
  bool meter_ok = false;
  float meter_peak = -99.0f;
  int meter_reads = 0;
  void cap_on() {
    capped = true;
    mic.cap = &cap;
    mic.cap_refused = &cap_refused;
  }
  bool mic_meter(float &peak_db) override {
    meter_reads++;
    peak_db = meter_peak;
    return meter_ok;
  }

  uint32_t mono_ms() override { return g_now; }
  WallTime wall() override { return wt; }
  void radio_send(uint8_t to, const uint8_t *d, size_t n) override { air->send(id, to, d, n); }
  bool radio_send_voice(uint8_t to, const uint8_t *d, size_t n) override {
    if (capped && !cap.take(g_now)) {
      cap_refused++;
      return false;
    }
    voice_out.push_back({to, std::vector<uint8_t>(d, d + n)});
    if (radio_ok) air->send(id, to, d, n);
    return radio_ok;
  }
  bool voice_rec_begin(VoiceRec *r) override { return mic_ok && mic.begin(r); }
  bool voice_rec_release(VoiceRec *r) override { return mic.release(r); }
  void log(const char *l) override { logs.push_back(l); }
  void *alloc_big(size_t n) override {
    allocs++;
    return malloc(n);
  }
  void free_big(void *p) override {
    if (p) frees++;
    free(p);
  }
  bool logged(const char *part) const {
    for (const std::string &l : logs)
      if (l.find(part) != std::string::npos) return true;
    return false;
  }
  int dings() const {
    int n = 0;
    for (Sound s : played) n += s == Sound::Ding ? 1 : 0;
    return n;
  }
};

// ---------------------------------------------------------------------------------------------
// The reference C6 (see the top of the file).

struct RefC6 {
  Air *air = nullptr;
  struct Sess {
    bool on = false;
    uint8_t xfer = 0, max = 0, via = 0;
    bool got[kVoiceMaxChunks] = {};
    int base = 0, top = 0;   // all < base received; highest idx + 1 seen
    int fresh = 0;           // new chunks since the last ACK
    bool gap_ack = false;
    bool dup = false;        // a chunk came that was already received (as gw::VoiceSession::dup): ACK 80 ms on
    uint32_t last_rx = 0, last_data = 0;
    uint8_t last_done = 0;
  } s[2];
  // The "Pi": the text for a completed xfer, and the TEXT retries until VOICE_GOT.
  const char *text = "Dinner is ready";
  uint8_t flags = kVoiceTextSentenceCap;
  uint8_t status = 0;          // the final status
  bool pi_on = true;
  uint32_t text_ms = 700;      // after state 2
  bool queued_at_begin = false;
  int queued_extra = 0;        // more Queued statuses, one every 2 s after state 2
  bool drop_end = false;
  bool abort_begin_nopi = false;
  bool answer_pings = true;
  bool gap_fill_ack = true;    // ACK 80 ms after a DATA that filled a gap (see on_frame); false: as the C6 0.5
  // Field fix 2026-11-20: the voice hello (VOICE_GOT kind 30, to upstairs) at the first tick and every
  // kVoiceHelloEveryMs, as gw::Gateway sends it: key 1 (mu-law and 255 chunks, as 0.6), or key 0 with
  // old05 (as 0.5: ADPCM only). old04: a C6 0.4 instead (gateway.cpp at git HEAD):
  // no hello, and every voice frame dropped unanswered (tc::decode fails on types 20-30).
  bool hello = true;
  bool old04 = false;
  // old05: a C6 0.5 as flashed (built with kVoiceMaxChunks 128): its voice_decode refuses a BEGIN with
  // max_chunks over 128, a DATA idx 128 or more and an END n_chunks over 128, and drops them unanswered.
  bool old05 = false;
  bool hello_sent = false;
  uint32_t hello_at = 0;
  int hellos = 0, voice_in = 0;
  // The Pi's OUTSIDE to upstairs (off unless outside_every_ms), on TComm's retry schedule (0, 150, 400,
  // 750, 1150 ms, give up at 1500), as both C6 versions send it; and what came back.
  uint32_t outside_every_ms = 0, outside_next = 0;
  bool out_pending = false;
  uint16_t out_seq = 700;
  uint32_t out_t0 = 0;
  int out_tries = 0, outside_sent = 0, outside_acked = 0, outside_failed = 0, outside_extra_tries = 0;
  uint32_t outside_worst_ms = 0;
  uint8_t out_frame[kMaxMsg];
  size_t out_len = 0;
  struct Out {
    bool on = false;
    uint8_t to = 0, kind = 0, len = 0, tries = 0;
    uint32_t key = 0, t0 = 0;
    uint8_t frame[kMaxMsg];
  };
  std::vector<Out> outs;
  struct Due {
    uint32_t at;
    uint8_t floor, xfer, status, via;
  };
  std::vector<Due> due;
  std::vector<uint32_t> pushes;  // ids of VOICE_PUSH taken
  std::map<uint32_t, uint8_t> dismiss_result;
  int window_violations = 0, begins = 0, completes = 0;
  uint16_t seq = 0;

  void send(uint8_t to, VoiceMsg &m) {
    m.sender = kSenderGateway;
    m.seq = ++seq;
    uint8_t buf[kMaxMsg];
    size_t n = voice_encode(m, buf, sizeof(buf));
    if (n) air->send(kSenderGateway, to, buf, n);
  }
  void retried(uint8_t to, VoiceMsg &m, uint8_t kind, uint32_t key) {
    m.sender = kSenderGateway;
    m.seq = ++seq;
    Out o;
    o.on = true;
    o.to = to;
    o.kind = kind;
    o.key = key;
    o.t0 = g_now;
    o.len = (uint8_t)voice_encode(m, o.frame, sizeof(o.frame));
    outs.push_back(o);
  }
  void ack(int f, uint8_t state) {
    Sess &x = s[f];
    VoiceMsg m;
    m.type = VType::Ack;
    m.floor = (uint8_t)f;
    m.u.ack.xfer = x.xfer;
    m.u.ack.base = (uint8_t)x.base;
    uint32_t bits = 0;
    for (int k = 0; k < 32; k++)
      if (x.base + k < kVoiceMaxChunks && x.got[x.base + k]) bits |= 1u << k;
    m.u.ack.bits = bits;
    m.u.ack.state = state;
    send(x.via, m);
    x.fresh = 0;
    x.gap_ack = false;
    x.dup = false;
  }
  void text_out(uint8_t f, uint8_t xfer, uint8_t st, uint8_t via) {
    VoiceMsg m;
    m.type = VType::Text;
    m.floor = f;
    m.u.text.xfer = xfer;
    m.u.text.status = st;
    if (st == (uint8_t)VoiceStatus::Ok) {
      m.u.text.flags = flags;
      m.u.text.len = (uint8_t)strlen(text);
      memcpy(m.u.text.text, text, m.u.text.len);
    }
    retried(via, m, (uint8_t)VType::Text, voice_text_key(xfer, st));
  }
  void on_frame(uint8_t from, const std::vector<uint8_t> &d) {
    if (is_voice_frame(d.data(), d.size())) voice_in++;
    if (!is_voice_frame(d.data(), d.size())) {  // a TComm frame: the ACK for the pending OUTSIDE
      Msg a;
      if (decode(d.data(), d.size(), a) && a.sender == from && from == kSenderUpstairs && a.type == MsgType::Ack &&
          a.u.ack.type == (uint8_t)MsgType::Outside && out_pending && a.u.ack.seq == out_seq) {
        out_pending = false;
        outside_acked++;
        outside_extra_tries += out_tries - 1;
        uint32_t ms = (uint32_t)ms_diff(g_now, out_t0);
        if (ms > outside_worst_ms) outside_worst_ms = ms;
      }
      return;
    }
    if (old04) return;  // gateway.cpp:250 at HEAD: decode fails, return
    VoiceMsg m;
    if (!voice_decode(d.data(), d.size(), m)) return;
    if (old05 && ((m.type == VType::Begin && m.u.begin.max_chunks > 128) ||
                  (m.type == VType::Data && m.u.data.idx >= 128) || (m.type == VType::End && m.u.end.n_chunks > 128)))
      return;
    if (m.sender != from) return;  // the MAC check
    int f = m.floor;
    Sess &x = s[f];
    switch (m.type) {
      case VType::Begin:
        begins++;
        if (abort_begin_nopi) {
          VoiceMsg a;
          a.type = VType::Abort;
          a.floor = (uint8_t)f;
          a.u.abort.xfer = m.u.begin.xfer;
          a.u.abort.reason = (uint8_t)VoiceAbort::NoPi;
          send(from, a);
          return;
        }
        if (!(x.on && x.xfer == m.u.begin.xfer)) {
          x = Sess();
          x.last_done = s[f].last_done;
          x.on = true;
          x.xfer = m.u.begin.xfer;
          x.max = m.u.begin.max_chunks;
          if (queued_at_begin && pi_on) due.push_back({g_now, (uint8_t)f, x.xfer, (uint8_t)VoiceStatus::Queued, from});
        }
        x.via = from;
        x.last_rx = g_now;
        ack(f, (uint8_t)VoiceAckState::Open);
        return;
      case VType::Data: {
        if (!x.on || x.xfer != m.u.data.xfer || m.u.data.idx >= x.max) return;
        x.via = from;
        x.last_rx = x.last_data = g_now;
        int i = m.u.data.idx;
        if (i >= x.base + kVoiceWindow) window_violations++;
        if (!x.got[i]) {
          x.got[i] = true;
          x.fresh++;
          // It fills a gap (a higher chunk came first): ACK 80 ms on, so a sender whose window is full
          // hears that base moved. Field fix 2026-11-21: with mu-law the 32-chunk window is only 0.47 s,
          // and without this a resend that closes the gap can leave the panel waiting until it stalls.
          // gw::Gateway 0.6 does this too (VoiceSession::dup is set for a chunk that fills a gap).
          if (gap_fill_ack && i + 1 < x.top) x.dup = true;
        } else {
          x.dup = true;  // the panel did not see the bitmap (as gw::VoiceSession::dup)
        }
        if (i + 1 > x.top) x.top = i + 1;
        while (x.base < kVoiceMaxChunks && x.got[x.base]) x.base++;
        if (x.fresh >= 8) ack(f, (uint8_t)VoiceAckState::Open);
        else x.gap_ack = x.base < x.top || x.dup;
        return;
      }
      case VType::End: {
        if (drop_end) return;
        if (x.last_done == m.u.end.xfer && !(x.on && x.xfer == m.u.end.xfer)) {
          VoiceMsg a;  // resent END after completion: state 2 again (check fix C6)
          a.type = VType::Ack;
          a.floor = (uint8_t)f;
          a.u.ack.xfer = m.u.end.xfer;
          a.u.ack.base = m.u.end.n_chunks;
          a.u.ack.state = (uint8_t)VoiceAckState::Complete;
          send(from, a);
          return;
        }
        if (!x.on || x.xfer != m.u.end.xfer) return;
        x.via = from;
        x.last_rx = g_now;
        bool all = true;
        for (int i = 0; i < m.u.end.n_chunks; i++) all = all && x.got[i];
        if (!all) {
          ack(f, (uint8_t)VoiceAckState::Open);
          return;
        }
        ack(f, (uint8_t)VoiceAckState::Complete);
        completes++;
        x.on = false;
        x.last_done = x.xfer;
        if (pi_on) {
          for (int k = 0; k < queued_extra; k++)
            due.push_back({g_now + 2000u * (uint32_t)(k + 1), (uint8_t)f, x.xfer, (uint8_t)VoiceStatus::Queued, from});
          due.push_back({g_now + text_ms, (uint8_t)f, x.xfer, status, from});
        }
        return;
      }
      case VType::Abort:
        if (x.on && x.xfer == m.u.abort.xfer) x.on = false;
        for (size_t k = 0; k < due.size(); k++)
          if (due[k].floor == f && due[k].xfer == m.u.abort.xfer) due[k].at = 0xFFFFFFFFu;
        return;
      case VType::Got:
        for (Out &o : outs)
          if (o.on && o.kind == m.u.got.kind && o.key == m.u.got.key) {
            o.on = false;
            if (o.kind == (uint8_t)VType::Dismiss) dismiss_result[o.key] = m.u.got.result;
          }
        return;
      case VType::Push: {
        bool seen = false;
        for (uint32_t id : pushes) seen = seen || id == m.u.push.id;
        if (!seen) pushes.push_back(m.u.push.id);
        VoiceMsg g;
        g.type = VType::Got;
        g.floor = (uint8_t)f;
        g.u.got.kind = (uint8_t)VType::Push;
        g.u.got.key = m.u.push.id;
        g.u.got.result = 0;
        send(from, g);
        return;
      }
      case VType::Ping: {
        if (!answer_pings) return;
        VoiceMsg g;
        g.type = VType::Got;
        g.floor = (uint8_t)f;
        g.u.got.kind = (uint8_t)VType::Ping;
        g.u.got.key = m.u.ping.nonce;
        send(from, g);  // straight back
        return;
      }
      default:
        return;
    }
  }
  void dismiss(uint8_t f, uint32_t id, uint8_t via) {
    VoiceMsg m;
    m.type = VType::Dismiss;
    m.floor = f;
    m.u.dismiss.id = id;
    retried(via, m, (uint8_t)VType::Dismiss, id);
  }
  void tick() {
    if (hello && !old04 && (!hello_sent || ms_diff(g_now, hello_at) >= (int32_t)kVoiceHelloEveryMs)) {
      hello_sent = true;
      hello_at = g_now;
      VoiceMsg g;
      g.type = VType::Got;
      g.floor = 0;
      g.u.got.kind = (uint8_t)VType::Ping;
      g.u.got.key = old05 ? kVoiceHelloKey : kVoiceHelloKeyMulaw;
      send(kSenderUpstairs, g);
      hellos++;
    }
    if (outside_every_ms) {
      if (!out_pending && ms_reached(g_now, outside_next)) {
        Msg o;
        o.type = MsgType::Outside;
        o.sender = kSenderGateway;
        o.seq = ++out_seq;
        o.u.outside.flags = 1;
        o.u.outside.temp_tenths_f = 485;
        o.u.outside.humidity = 71;
        o.u.outside.forecast = 1;
        o.u.outside.time = 1793000000u;
        o.u.outside.phrase_len = 12;
        memcpy(o.u.outside.phrase, "Settled Fine", 13);
        out_len = encode(o, out_frame, sizeof(out_frame));
        out_pending = out_len > 0;
        out_t0 = g_now;
        out_tries = 0;
        outside_sent++;
        outside_next = g_now + outside_every_ms;
      }
      static const uint32_t kOutAt[5] = {0, 150, 400, 750, 1150};
      if (out_pending && out_tries < 5 && ms_diff(g_now, out_t0) >= (int32_t)kOutAt[out_tries]) {
        air->send(kSenderGateway, kSenderUpstairs, out_frame, out_len);
        out_tries++;
      }
      if (out_pending && ms_diff(g_now, out_t0) >= 1500) {
        out_pending = false;
        outside_failed++;
      }
    }
    for (int f = 0; f < 2; f++) {
      Sess &x = s[f];
      if (!x.on) continue;
      if (x.gap_ack && ms_diff(g_now, x.last_data) >= 80) ack(f, (uint8_t)VoiceAckState::Open);
      if (ms_diff(g_now, x.last_rx) >= 2000) {  // idle: abort to the panel
        VoiceMsg a;
        a.type = VType::Abort;
        a.floor = (uint8_t)f;
        a.u.abort.xfer = x.xfer;
        a.u.abort.reason = (uint8_t)VoiceAbort::Timeout;
        send(x.via, a);
        x.on = false;
      }
    }
    for (size_t k = 0; k < due.size();) {
      if (due[k].at != 0xFFFFFFFFu && ms_reached(g_now, due[k].at)) {
        text_out(due[k].floor, due[k].xfer, due[k].status, due[k].via);
        due.erase(due.begin() + k);
      } else {
        k++;
      }
    }
    static const uint32_t kAt[5] = {0, 150, 400, 750, 1150};
    for (Out &o : outs) {
      if (!o.on) continue;
      if (o.tries < 5 && ms_diff(g_now, o.t0) >= (int32_t)kAt[o.tries]) {
        air->send(kSenderGateway, o.to, o.frame, o.len);
        o.tries++;
      }
      if (ms_diff(g_now, o.t0) >= 1500) o.on = false;
    }
  }
};

// ---------------------------------------------------------------------------------------------
// Piece D's gateway, with a fake Pi on its serial side.

int line_int(const std::string &l, const char *key) {
  size_t at = l.find(key);
  return at == std::string::npos ? -1 : atoi(l.c_str() + at + strlen(key));
}
std::string line_str(const std::string &l, const char *key) {
  size_t at = l.find(key);
  if (at == std::string::npos) return "";
  at += strlen(key);
  size_t end = l.find('"', at);
  return l.substr(at, end - at);
}

struct RealC6 : gw::GatewayIO {
  Air *air = nullptr;
  gw::Gateway g{*this, "02:00:00:00:00:02", 100};
  std::vector<std::string> lines;  // everything the gateway printed
  const char *text = "Dinner is ready";
  uint32_t text_ms = 700;
  struct Due {
    uint32_t at;
    int x;
    std::string floor;
  };
  std::vector<Due> due;
  int64_t next_id = 1;
  int window_violations = 0, voice_ends = 0, msgs = 0;

  uint32_t now_ms() override { return g_now; }
  void send_frame(int to, const uint8_t *d, size_t n) override { air->send(kSenderGateway, (uint8_t)to, d, n); }
  void reply(const char *line) override {
    std::string l(line);
    lines.push_back(l);
    if (l.find("\"event\":\"voice_end\"") != std::string::npos) {
      voice_ends++;
      due.push_back({g_now + text_ms, line_int(l, "\"x\":"), line_str(l, "\"f\":\"") == "up" ? "upstairs" : "downstairs"});
    }
    if (l.find("\"event\":\"voice_msg\"") != std::string::npos) msgs++;
  }
  void say(const std::string &l) { g.on_line(l.c_str(), l.size()); }
  void start() {
    g.boot();
    say("{\"cmd\":\"hello\",\"voice\":1}");
  }
  void on_frame(uint8_t from, const std::vector<uint8_t> &d) {
    VoiceMsg m;
    if (voice_decode(d.data(), d.size(), m) && m.type == VType::Data) {
      const gw::VoiceSession &vs = g.voice_session(m.floor);
      if (vs.state == gw::VoiceSession::Open && vs.xfer == m.u.data.xfer && m.u.data.idx >= vs.base() + kVoiceWindow)
        window_violations++;
    }
    g.on_frame(from, d.data(), d.size());
  }
  void tick() {
    for (size_t k = 0; k < due.size();) {
      if (!ms_reached(g_now, due[k].at)) {
        k++;
        continue;
      }
      char l[300];
      snprintf(l, sizeof l,
               "{\"type\":\"voice_text\",\"id\":%lld,\"x\":%d,\"floor\":\"%s\",\"status\":0,\"fl\":1,\"t0\":\"%s\",\"t1\":\"\"}",
               (long long)next_id++, due[k].x, due[k].floor.c_str(), text);
      say(l);
      due.erase(due.begin() + k);
    }
    g.tick();
  }
  // The Pi's OUTSIDE (the bridge sends one a minute); its reply is an "acked" or "failed" line.
  void outside() {
    char l[200];
    snprintf(l, sizeof l,
             "{\"type\":\"outside\",\"id\":%lld,\"temp_tenths_f\":485,\"humidity\":71,\"forecast\":1,\"flags\":1,"
             "\"time\":1793000000,\"phrase\":\"Settled Fine\"}",
             (long long)next_id++);
    say(l);
  }
  void phone_ack(const char *floor, uint32_t id) {
    char l[160];
    snprintf(l, sizeof l, "{\"type\":\"voice_ack\",\"id\":%lld,\"floor\":\"%s\",\"msg\":%lu}", (long long)next_id++,
             floor, (unsigned long)id);
    say(l);
  }
  int count(const char *part) const {
    int n = 0;
    for (const std::string &l : lines) n += l.find(part) != std::string::npos ? 1 : 0;
    return n;
  }
};

// ---------------------------------------------------------------------------------------------
// The house: two panels, the C6 and the air, stepped 10 ms at a time.

constexpr uint32_t kNoon = 1767225600u + 12 * 3600;  // 2026-01-01 12:00 UTC (offset 0)

struct House {
  Air air;
  Panel pu, pd;
  Controller *up = nullptr, *down = nullptr;
  RefC6 c6;
  RealC6 *real = nullptr;  // piece D's gateway instead of the reference model
  int relayed = 0;
  uint32_t utc_base = kNoon;  // the wall clock at g_now 1000

  explicit House(bool real_c6 = false) {
    if (real_c6) {
      real = new RealC6;
      real->air = &air;
      real->start();
    }
    pu.id = kSenderUpstairs;
    pd.id = kSenderDownstairs;
    pu.air = pd.air = &air;
    pu.mic.air = pd.mic.air = &air;
    pu.mic.me = kSenderUpstairs;
    pd.mic.me = kSenderDownstairs;
    pu.wt.valid = pd.wt.valid = true;
    pu.wt.utc = pd.wt.utc = kNoon;
    pu.wt.quality = pd.wt.quality = kClockNetwork;
    c6.air = &air;
    g_now = 1000;
    start(true, true);
  }
  ~House() {
    delete up;
    delete down;
    delete real;
  }
  void start(bool u, bool d) {
    Settings s;
    if (u) {
      delete up;
      s.floor = Floor::Upstairs;
      up = new Controller(pu);
      up->begin(s, OwnState(), DogState());
    }
    if (d) {
      delete down;
      s.floor = Floor::Downstairs;
      down = new Controller(pd);
      down->begin(s, OwnState(), DogState());
    }
  }
  void deliver(const Pkt &p) {
    if (p.to == kSenderGateway) {
      if (real) real->on_frame(p.from, p.d);
      else c6.on_frame(p.from, p.d);
      return;
    }
    if (p.d.size() >= 3 && p.d[2] != p.from) return;  // board_radio's sender byte check
    if (p.to == kSenderUpstairs && is_voice_frame(p.d.data(), p.d.size())) {
      int to = voice_relay_frame(p.d.data(), p.d.size(), p.from, kSenderUpstairs);
      if (to >= 0) {  // the vrelay task: this hop's sender byte, on to the next
        if (pu.capped && !pu.cap.take(g_now)) {  // radio_send_voice's air cap: the frame is dropped
          pu.cap_refused++;
          return;
        }
        std::vector<uint8_t> f = p.d;
        voice_set_sender(f.data(), kSenderUpstairs);
        air.send(kSenderUpstairs, (uint8_t)to, f.data(), f.size());
        relayed++;
        return;
      }
    }
    Controller *c = p.to == kSenderUpstairs ? up : down;
    if (c) c->on_radio(p.d.data(), p.d.size());
  }
  void step() {
    g_now += 10;
    pu.mic.step();
    pd.mic.step();
    Pkt p;
    while (air.pop(p)) deliver(p);
    if (real) real->tick();
    else c6.tick();
    if (g_now % 50 == 0) {
      pu.wt.utc = pd.wt.utc = utc_base + (g_now - 1000) / 1000;
      if (up) up->tick();
      if (down) down->tick();
    }
  }
  void run(uint32_t ms) {
    uint32_t end = g_now + ms;
    while (ms_diff(end, g_now) > 0) step();
  }
  // Runs until the controller's voice UI is one of the final states, or ms pass. Returns the time taken.
  uint32_t run_until_result(Controller &c, uint32_t ms) {
    uint32_t t0 = g_now;
    while (ms_diff(g_now, t0) < (int32_t)ms) {
      VoiceUi u = c.view().voice.ui;
      if (u == VoiceUi::Result || u == VoiceUi::Error) break;
      step();
    }
    return (uint32_t)ms_diff(g_now, t0);
  }
  void settle() { run(2000); }  // the boot exchange (STATE both ways)
};

int count_sent(const Panel &p, VType t, uint8_t to = 0xFF) {
  int n = 0;
  for (const auto &s : p.voice_out)
    if (s.bytes.size() >= 2 && s.bytes[1] == (uint8_t)t && (to == 0xFF || s.to == to)) n++;
  return n;
}

// The data sends of one panel per chunk (from the air log: what that panel itself put on the air).
std::map<int, int> data_sends(const Air &a, uint8_t from) {
  std::map<int, int> m;
  for (const Air::Seen &s : a.log)
    if (s.from == from && s.d.size() > 7 && s.d[1] == (uint8_t)VType::Data && s.d[2] == from) m[s.d[7]]++;
  return m;
}

// ---------------------------------------------------------------------------------------------
// A loopback VoiceLink for the unit tests of VoiceTx / VoiceOut alone.

struct Capture : VoiceLink {
  struct F {
    uint8_t to;
    std::vector<uint8_t> d;
    uint32_t at;
  };
  std::vector<F> out;
  bool ok = true;
  bool voice_send(uint8_t to, const uint8_t *d, size_t n) override {
    out.push_back({to, std::vector<uint8_t>(d, d + n), g_now});
    return ok;
  }
  int count(VType t) const {
    int n = 0;
    for (const F &f : out) n += f.d.size() >= 2 && f.d[1] == (uint8_t)t ? 1 : 0;
    return n;
  }
  std::vector<uint32_t> times(VType t) const {
    std::vector<uint32_t> v;
    for (const F &f : out)
      if (f.d.size() >= 2 && f.d[1] == (uint8_t)t) v.push_back(f.at);
    return v;
  }
};

VoiceMsg ack_msg(uint8_t floor, uint8_t xfer, uint8_t base, uint32_t bits, VoiceAckState st) {
  VoiceMsg m;
  m.type = VType::Ack;
  m.sender = kSenderGateway;
  m.floor = floor;
  m.u.ack.xfer = xfer;
  m.u.ack.base = base;
  m.u.ack.bits = bits;
  m.u.ack.state = (uint8_t)st;
  return m;
}

struct RecBox {
  std::vector<uint8_t> mem;
  VoiceRec *rec;
  explicit RecBox(uint8_t xfer = 9, uint8_t floor = 0) : mem(voice_rec_bytes((uint8_t)kVoiceClipChunks) + 8) {
    rec = voice_rec_init(mem.data(), mem.size(), (uint8_t)kVoiceClipChunks);
    rec->xfer = xfer;
    rec->floor = floor;
    rec->sender = floor;
  }
  ~RecBox() { voice_rec_destroy(rec); }
  // n chunks ready and first-sent at time t
  void sent(int n, uint32_t t) {
    for (int i = rec->first_sent.load(); i < n; i++) {
      rec->chunks[i].len = 237;
      rec->first_ms[i] = t;
    }
    rec->chunks_ready.store((uint8_t)n);
    rec->first_sent.store((uint8_t)n);
  }
};

}  // namespace

// =============================================================================================
// VoiceTx alone

VTEST(tx_begin_resent_then_gives_up) {
  g_now = 5000;
  Capture l;
  RecBox b;
  VoiceTx tx;
  tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
  for (int i = 0; i < 40; i++) {
    g_now += 50;
    tx.tick(g_now, l);
  }
  CHECK(tx.phase() == VoiceTxPhase::Failed);
  CHECK(tx.err() == VoiceErr::GatewayOld);
  CHECK(tx.direct_failed());
  std::vector<uint32_t> t = l.times(VType::Begin);
  CHECK(t.size() >= 3 && t[0] == 5000 && t[1] == 5150 && t[2] == 5300);
  for (uint32_t x : t) CHECK(x - 5000 < kVoiceBeginGiveUpMs);
  std::vector<uint32_t> a = l.times(VType::Abort);  // 3 ABORTs at 0, 100, 250 ms after the give-up
  CHECK(a.size() == 3);
  if (a.size() == 3) {
    CHECK(a[0] == 6500);
    CHECK(a[1] - a[0] == 100);
    CHECK(a[2] - a[0] == 250);
  }
}

VTEST(tx_begin_falls_back_at_450) {
  g_now = 100;
  Capture l;
  RecBox b(4, 1);
  VoiceTx tx;
  tx.start(b.rec, VoicePurpose::Message, kSenderGateway, kSenderUpstairs, g_now, l);
  for (int i = 0; i < 12; i++) {
    g_now += 50;
    tx.tick(g_now, l);
  }
  int direct = 0, relay = 0;
  for (const auto &f : l.out) {
    if (f.d[1] != (uint8_t)VType::Begin) continue;
    if (f.to == kSenderGateway) {
      direct++;
      CHECK(f.at - 100 < kVoiceBeginFallbackMs);
    } else {
      relay++;
      CHECK(f.to == kSenderUpstairs && f.at - 100 >= kVoiceBeginFallbackMs);
    }
  }
  CHECK(direct == 3);
  CHECK(relay >= 1);
  CHECK(tx.fell_back() && tx.direct_failed());
  tx.on_ack(ack_msg(1, 4, 0, 0, VoiceAckState::Open), g_now);
  CHECK(tx.phase() == VoiceTxPhase::Stream);
  CHECK(tx.to() == kSenderUpstairs);
}

VTEST(tx_resend_rules) {
  g_now = 1000;
  Capture l;
  RecBox b;
  VoiceTx tx;
  tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
  tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Open), g_now);
  b.sent(20, 1000);
  l.out.clear();
  g_now = 1150;  // 150 ms: nothing is due yet
  tx.tick(g_now, l);
  CHECK(l.count(VType::Data) == 0);
  g_now = 1200;  // 200 ms: at most 4, oldest first
  tx.tick(g_now, l);
  CHECK(l.count(VType::Data) == 4);
  if (l.out.size() == 4) CHECK(l.out[0].d[7] == 0 && l.out[3].d[7] == 3);
  // An ACK with chunks 0-9 and 12 received: the next resends skip them.
  tx.on_ack(ack_msg(0, 9, 10, 1u << 2, VoiceAckState::Open), g_now);
  CHECK(b.rec->base.load() == 10);
  l.out.clear();
  g_now = 1250;
  tx.tick(g_now, l);
  for (const auto &f : l.out) CHECK(f.d[7] >= 10 && f.d[7] != 12 && f.d[7] < 20);
  // Never at or past first_sent, never more than 5 sends a chunk, never more than 2 x chunks resends.
  int per[kVoiceMaxChunks] = {};
  for (int i = 0; i < 200; i++) {
    g_now += 50;
    tx.tick(g_now, l);
    if (tx.phase() == VoiceTxPhase::Failed) break;
  }
  for (const auto &f : l.out)
    if (f.d[1] == (uint8_t)VType::Data) {
      CHECK(f.d[7] < 20);
      per[f.d[7]]++;
    }
  for (int i = 0; i < 20; i++) CHECK(per[i] <= kVoiceSendsPerChunk - 1);
  CHECK(tx.stats().resends <= 2u * 20u);
  // base stuck at 10 with chunks owed: given up 1.5 s after it last moved.
  CHECK(tx.phase() == VoiceTxPhase::Failed);
  CHECK(tx.err() == VoiceErr::SendFailed);
}

VTEST(tx_refused_send_not_counted) {
  g_now = 1000;
  Capture l;
  RecBox b;
  VoiceTx tx;
  tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
  tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Open), g_now);
  b.sent(2, 1000);
  l.ok = false;
  g_now = 1200;
  tx.tick(g_now, l);
  CHECK(tx.stats().refused >= 2);
  CHECK(tx.stats().resends == 0);
}

VTEST(tx_end_and_text_timeouts) {
  {  // END never confirmed: 1.5 s
    g_now = 1000;
    Capture l;
    RecBox b;
    VoiceTx tx;
    tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
    tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Open), g_now);
    b.sent(3, 1000);
    b.rec->done.store(true);
    tx.on_ack(ack_msg(0, 9, 3, 0, VoiceAckState::Open), g_now);
    for (int i = 0; i < 40 && tx.phase() != VoiceTxPhase::Failed; i++) {
      g_now += 50;
      tx.tick(g_now, l);
    }
    CHECK(tx.phase() == VoiceTxPhase::Failed && tx.err() == VoiceErr::SendFailed);
    CHECK(g_now - 1050 >= kVoiceEndGiveUpMs && g_now - 1050 < kVoiceEndGiveUpMs + 100);
    CHECK(l.count(VType::End) >= 6);  // every 200 ms
  }
  {  // text never comes: 8 s after state 2
    g_now = 1000;
    Capture l;
    RecBox b;
    VoiceTx tx;
    tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
    b.sent(3, 1000);
    b.rec->done.store(true);
    tx.on_ack(ack_msg(0, 9, 3, 0, VoiceAckState::Complete), g_now);
    CHECK(tx.phase() == VoiceTxPhase::WaitText);
    uint32_t t2 = g_now;
    while (tx.phase() == VoiceTxPhase::WaitText && g_now - t2 < 30000) {
      g_now += 50;
      tx.tick(g_now, l);
    }
    CHECK(tx.err() == VoiceErr::NoPi);
    CHECK(g_now - t2 >= kVoiceTextWaitMs && g_now - t2 < kVoiceTextWaitMs + 60);
  }
}

VTEST(tx_queued_extends_once) {
  g_now = 1000;
  Capture l;
  RecBox b;
  VoiceTx tx;
  tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
  VoiceMsg q;
  q.type = VType::Text;
  q.sender = kSenderGateway;
  q.u.text.xfer = 9;
  q.u.text.status = (uint8_t)VoiceStatus::Queued;
  tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Open), g_now);
  CHECK(tx.on_text(q, g_now) == VoiceTextIs::Interim);  // while chunks still flow
  CHECK(tx.waiting());
  b.sent(3, 1000);
  b.rec->done.store(true);
  tx.on_ack(ack_msg(0, 9, 3, 0, VoiceAckState::Complete), g_now);
  uint32_t t2 = g_now;
  g_now += 5000;
  CHECK(tx.on_text(q, g_now) == VoiceTextIs::Interim);  // a second one does not extend again
  while (tx.phase() == VoiceTxPhase::WaitText && g_now - t2 < 60000) {
    g_now += 50;
    tx.tick(g_now, l);
  }
  CHECK(tx.err() == VoiceErr::NoPi);
  CHECK(g_now - t2 >= kVoiceQueuedWaitMs && g_now - t2 < kVoiceQueuedWaitMs + 60);
}

VTEST(tx_text_statuses_and_duplicates) {
  const struct {
    VoiceStatus st;
    VoiceErr err;
  } table[] = {{VoiceStatus::Nothing, VoiceErr::Nothing}, {VoiceStatus::Failed, VoiceErr::NotCaught},
               {VoiceStatus::Busy, VoiceErr::Busy},       {VoiceStatus::Timeout, VoiceErr::NoPi},
               {VoiceStatus::Lost, VoiceErr::Lost}};
  for (const auto &row : table) {
    g_now = 1000;
    Capture l;
    RecBox b;
    VoiceTx tx;
    tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
    tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Complete), g_now);
    VoiceMsg t;
    t.type = VType::Text;
    t.u.text.xfer = 9;
    t.u.text.status = (uint8_t)row.st;
    CHECK(tx.on_text(t, g_now) == VoiceTextIs::Final);
    CHECK(tx.phase() == VoiceTxPhase::Failed && tx.err() == row.err);
    CHECK(tx.on_text(t, g_now) == VoiceTextIs::Duplicate);
    t.u.text.xfer = 10;
    CHECK(tx.on_text(t, g_now) == VoiceTextIs::NotMine);
  }
  // Ok carries the text and its flags.
  g_now = 1000;
  Capture l;
  RecBox b;
  VoiceTx tx;
  tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
  tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Complete), g_now);
  VoiceMsg t;
  t.type = VType::Text;
  t.u.text.xfer = 9;
  t.u.text.status = 0;
  t.u.text.flags = kVoiceTextSentenceCap;
  t.u.text.len = 5;
  memcpy(t.u.text.text, "Hello", 6);
  CHECK(tx.on_text(t, g_now) == VoiceTextIs::Final);
  CHECK(tx.phase() == VoiceTxPhase::Done && !strcmp(tx.text(), "Hello") && tx.text_flags() == kVoiceTextSentenceCap);
  // A cancelled job: its text is not taken.
  RecBox b2(11);
  VoiceTx tx2;
  tx2.start(b2.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
  tx2.cancel(VoiceAbort::User, g_now, l);
  t.u.text.xfer = 11;
  CHECK(tx2.on_text(t, g_now) == VoiceTextIs::NotMine);
  CHECK(tx2.err() == VoiceErr::None && tx2.phase() == VoiceTxPhase::Failed);
}

VTEST(tx_ack_reordered_and_unknown) {
  g_now = 1000;
  Capture l;
  RecBox b;
  VoiceTx tx;
  tx.start(b.rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
  // State 3 before the BEGIN landed: still waiting, BEGIN goes on.
  tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Unknown), g_now);
  CHECK(tx.phase() == VoiceTxPhase::Begin);
  tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Open), g_now);
  b.sent(30, 1000);
  tx.on_ack(ack_msg(0, 9, 20, 0, VoiceAckState::Open), g_now);
  tx.on_ack(ack_msg(0, 9, 8, 0xFFFF, VoiceAckState::Open), g_now);  // older: ignored
  CHECK(b.rec->base.load() == 20);
  tx.on_ack(ack_msg(0, 7, 25, 0, VoiceAckState::Open), g_now);  // another xfer: ignored
  CHECK(b.rec->base.load() == 20);
  tx.on_ack(ack_msg(0, 9, 20, 0, VoiceAckState::Unknown), g_now);  // the C6 lost it
  CHECK(tx.phase() == VoiceTxPhase::Failed && tx.err() == VoiceErr::SendFailed);
}

// =============================================================================================
// VoiceOut alone

VTEST(out_retry_schedule_and_expiry) {
  g_now = 2000;
  Capture l;
  VoiceOut o;
  uint8_t f[20] = {1, 26};
  CHECK(o.add(kSenderDownstairs, 26, 77, f, sizeof f, g_now));
  for (int i = 0; i < 4; i++) CHECK(o.add(kSenderGateway, 28, 100 + i, f, sizeof f, g_now) == (i < 3));
  uint8_t kind = 0;
  uint32_t key = 0;
  for (int i = 0; i <= 40; i++) {
    o.tick(g_now, l);
    if (o.expired(g_now, kind, key)) break;
    g_now += 50;
  }
  std::vector<uint32_t> t;
  for (const auto &x : l.out)
    if (x.to == kSenderDownstairs) t.push_back(x.at - 2000);
  CHECK(t.size() == 5);
  if (t.size() == 5) CHECK(t[0] == 0 && t[1] == 150 && t[2] == 400 && t[3] == 750 && t[4] == 1150);
  CHECK(g_now - 2000 == kVoiceOutGiveUpMs);
  CHECK(kind == 26 && key == 77);
  CHECK(o.on_got(28, 101));
  CHECK(!o.on_got(28, 101));
  CHECK(!o.on_got(26, 101));
  int left = 0;
  while (o.expired(g_now, kind, key)) left++;
  CHECK(left == 2);
  CHECK(o.used() == 0);
}

// =============================================================================================
// The inbox and the arrival rules

VTEST(inbox_order_drop_dedup_restore) {
  VoiceInbox in;
  bool dropped = false;
  char t[8];
  for (int i = 1; i <= 5; i++) {
    snprintf(t, sizeof t, "msg %d", i);
    CHECK(in.add((uint32_t)i * 11, 0, 0, t, strlen(t), &dropped) == VoiceAdd::Added);
    CHECK(dropped == (i == 5));
  }
  CHECK(in.count() == 4);
  CHECK(in.top()->id == 22);  // the oldest (11) went
  CHECK(!strcmp(in.data().msg[3].text, "msg 5"));
  CHECK(in.add(11, 0, 0, "x", 1) == VoiceAdd::Duplicate);  // dropped, but its id is still remembered
  CHECK(in.add(33, 0, 0, "x", 1) == VoiceAdd::Duplicate);
  CHECK(in.add(0, 0, 0, "x", 1) == VoiceAdd::Bad);
  CHECK(in.add(99, 0, 2, "x", 1) == VoiceAdd::Bad);
  CHECK(in.add(99, 0, 0, "x\n", 2) == VoiceAdd::Bad);
  CHECK(in.add(99, 0, 0, "", 0) == VoiceAdd::Bad);
  CHECK(in.remove(33));
  CHECK(!in.remove(33));
  CHECK(in.count() == 3 && in.top()->id == 22);
  CHECK(in.pop_top() && in.top()->id == 44);
  // A restart: the saved data comes back, dedup included.
  VoiceInboxData saved = in.data();
  CHECK(voice_inbox_valid(saved));
  VoiceInbox again;
  CHECK(again.restore(saved));
  CHECK(again.count() == 2 && again.top()->id == 44);
  CHECK(again.add(22, 0, 0, "x", 1) == VoiceAdd::Duplicate);
  CHECK(again.add(11, 0, 0, "x", 1) == VoiceAdd::Duplicate);
  saved.n = 9;
  CHECK(!again.restore(saved));
  CHECK(again.count() == 0);
  // The seen ring keeps the last 8 ids.
  VoiceInbox ring;
  for (uint32_t i = 1; i <= 12; i++) ring.add(i, 0, 1, "y", 1);
  CHECK(!ring.seen(4));
  CHECK(ring.seen(5) && ring.seen(12));
}

VTEST(arrival_table) {
  const Status all[] = {Status::Open, Status::Busy, Status::Private, Status::Sleeping, Status::Away};
  for (Status s : all) {
    const bool ob = s == Status::Open || s == Status::Busy;
    for (int quiet = 0; quiet < 2; quiet++)
      for (int dark = 0; dark < 2; dark++)
        for (int rec = 0; rec < 2; rec++)
          for (int banner = 0; banner < 2; banner++) {
            VoiceArrival a = voice_arrival(s, true, quiet, dark, rec, banner);
            const bool sound = ob && !quiet && !dark;
            CHECK(a.wake == sound);
            CHECK(a.ding == (sound && !rec && !banner));
            CHECK(a.defer == (sound && rec && !banner));
            CHECK(a.push == (ob && !quiet));
            CHECK(!(a.ding && a.defer));
          }
    // No clock: the ding still plays (quiet unknown), the phones are never pushed.
    VoiceArrival a = voice_arrival(s, false, true, true, false, false);
    CHECK(a.ding == ob);
    CHECK(!a.push);
  }
}

VTEST(backlight_voice_wake_in_dark_hours) {
  Settings s;
  s.screen_off_overnight = true;
  BacklightInputs in;
  in.minute_of_day = 23 * 60;
  CHECK(backlight_for(s, in) == kBacklightOff);
  in.voice_wake = true;
  CHECK(backlight_for(s, in) != kBacklightOff);
}

// =============================================================================================
// The house: uploads end to end

VTEST(house_boot_exchange_advertises_voice) {
  House h;
  h.settle();
  CHECK(h.up->view().voice.peer_voice);
  CHECK(h.down->view().voice.peer_voice);
  CHECK(!h.down->view().voice.peer_offline);
  CHECK(h.up->view().voice.capable);
}

VTEST(house_dictation_clean_link) {
  House h;
  h.settle();
  CHECK(h.up->voice_press(VoicePurpose::Dictation));
  h.run(50);  // the view is rebuilt at the next tick, as for every other intent
  CHECK(h.up->view().voice.ui == VoiceUi::Recording);
  h.run(450);
  CHECK(h.up->view().voice.ui == VoiceUi::Recording);
  CHECK(h.up->view().voice.level == 40);
  uint32_t took = h.run_until_result(*h.up, 15000);
  const VoiceView &v = h.up->view().voice;
  CHECK(v.ui == VoiceUi::Result);
  CHECK(!strcmp(v.text, "Dinner is ready"));
  CHECK(v.text_flags == kVoiceTextSentenceCap);
  CHECK(v.text_seq == 1);
  CHECK(took < 5000);  // 3 s capture + 0.7 s Pi + the link
  CHECK(h.pu.mic.window_violations == 0 && h.c6.window_violations == 0);
  CHECK(h.c6.completes == 1);
  CHECK(h.pu.allocs >= 1);
  h.run(200);
  // The clip is freed once the text is in; the keyboard takes the text, back to Idle.
  CHECK(h.pu.mic.rec == nullptr);
  h.up->voice_done();
  h.run(50);
  CHECK(h.up->view().voice.ui == VoiceUi::Idle);
  CHECK(h.up->view().voice.text_seq == 1);
  // No transcript in any log line.
  for (const std::string &l : h.pu.logs) CHECK(l.find("Dinner") == std::string::npos);
  CHECK(h.pu.logged("voice: x="));
  // The GOT for the text went back, so the C6's TEXT slot cleared.
  bool open = false;
  for (const auto &o : h.c6.outs) open = open || o.on;
  CHECK(!open);
}

VTEST(house_lossy_links_complete) {
  // 10 % loss, 5 % duplicates, up to 25 ms reordering, on every voice hop; each floor, many seeds.
  int done = 0, runs = 0, extra = 0;
  for (uint32_t seed = 1; seed <= 24; seed++) {
    for (int fl = 0; fl < 2; fl++) {
      House h;
      h.settle();
      h.air.rng = Rng(seed * 2654435761u + (uint32_t)fl);
      h.air.drop = 10;
      h.air.dup = 5;
      h.air.jitter = 25;
      Controller &c = fl ? *h.down : *h.up;
      Panel &p = fl ? h.pd : h.pu;
      p.mic.release_after_ms = 1500 + seed * 50;  // a hold of 1.5-2.7 s
      CHECK(c.voice_press(VoicePurpose::Dictation));
      h.run_until_result(c, 20000);
      runs++;
      bool ok = c.view().voice.ui == VoiceUi::Result && !strcmp(c.view().voice.text, "Dinner is ready");
      done += ok ? 1 : 0;
      if (!ok) printf("  lossy seed %u floor %d: ui %d err %d\n", seed, fl, (int)c.view().voice.ui, (int)c.view().voice.err);
      CHECK(p.mic.window_violations == 0);
      CHECK(h.c6.window_violations == 0);
      for (const auto &kv : data_sends(h.air, p.id)) CHECK(kv.second <= kVoiceSendsPerChunk);
      int chunks = 0, total = 0;
      for (const auto &kv : data_sends(h.air, p.id)) {
        chunks++;
        total += kv.second;
      }
      CHECK(total <= 3 * chunks);
      extra += total - chunks;
    }
  }
  CHECK(done == runs);
  CHECK(extra > 100);  // the loss really was there and the resends covered it
  printf("  lossy: %d runs, %d resends in all\n", runs, extra);
}

VTEST(house_relay_route_lossy) {
  // Downstairs forced through upstairs, 8 % loss on each of the two hops.
  int ok = 0;
  for (uint32_t seed = 1; seed <= 10; seed++) {
    House h;
    h.settle();
    h.down->set_voice_route(Controller::VoiceRoute::Relay);
    h.air.rng = Rng(seed * 40503u);
    h.air.drop = 8;
    h.air.dup = 5;
    h.air.jitter = 20;
    h.pd.mic.release_after_ms = 2000;
    CHECK(h.down->voice_press(VoicePurpose::Message));
    h.run_until_result(*h.down, 20000);
    ok += h.down->view().voice.ui == VoiceUi::Result ? 1 : 0;
    CHECK(h.relayed > 50);
    CHECK(h.air.count(kSenderDownstairs, kSenderGateway, VType::Data) == 0);  // nothing direct
    CHECK(h.c6.s[1].via == kSenderUpstairs);
  }
  CHECK(ok == 10);
}

VTEST(house_begin_falls_back_then_route_switches) {
  House h;
  h.settle();
  h.air.cut[kSenderDownstairs][kSenderGateway] = true;  // downstairs cannot reach the C6 directly
  h.air.cut[kSenderGateway][kSenderDownstairs] = true;
  h.pd.mic.release_after_ms = 1200;
  CHECK(h.down->voice_press(VoicePurpose::Message));
  h.run_until_result(*h.down, 15000);
  CHECK(h.down->view().voice.ui == VoiceUi::Result);
  CHECK(h.air.count(kSenderDownstairs, kSenderGateway, VType::Begin) == 3);  // 0, 150, 300 direct
  CHECK(h.air.count(kSenderDownstairs, kSenderUpstairs, VType::Begin) >= 1);
  CHECK(h.c6.s[1].via == kSenderUpstairs);
  h.down->voice_done();
  // The failed direct BEGIN sends a ping at once; 3 misses in a row switch the route to the relay.
  CHECK(h.down->voice_direct_ok());
  h.run(8000);
  CHECK(!h.down->voice_direct_ok());
  CHECK(h.pd.logged("using the relay through upstairs"));
  // The next job goes through upstairs from the first BEGIN.
  int before = h.air.count(kSenderDownstairs, kSenderGateway, VType::Begin);
  h.pd.mic.release_after_ms = 800;
  CHECK(h.down->voice_press(VoicePurpose::Dictation));
  h.run_until_result(*h.down, 15000);
  CHECK(h.down->view().voice.ui == VoiceUi::Result);
  CHECK(h.air.count(kSenderDownstairs, kSenderGateway, VType::Begin) == before);
  h.down->voice_done();
  // The link comes back: the 10-minute ping finds it, direct again.
  h.air.cut[kSenderDownstairs][kSenderGateway] = false;
  h.air.cut[kSenderGateway][kSenderDownstairs] = false;
  h.run(601000);
  CHECK(h.down->voice_direct_ok());
  CHECK(h.pd.logged("direct again"));
}

VTEST(house_gateway_silent_and_no_pi) {
  {
    House h;
    h.settle();
    h.air.cut[kSenderUpstairs][kSenderGateway] = true;
    CHECK(h.up->voice_press(VoicePurpose::Dictation));
    h.run_until_result(*h.up, 5000);
    CHECK(h.up->view().voice.ui == VoiceUi::Error);
    CHECK(h.up->view().voice.err == VoiceErr::GatewayOld);
    h.run(400);
    CHECK(count_sent(h.pu, VType::Abort) == 3);
    CHECK(h.pu.mic.rec == nullptr);
    CHECK(h.pu.allocs == h.pu.frees + 1);  // the calendar's block stays; the clip went
    h.run(4000);
    CHECK(h.up->view().voice.ui == VoiceUi::Idle);  // the error shows 4 s
  }
  {
    House h;
    h.settle();
    h.c6.abort_begin_nopi = true;
    CHECK(h.up->voice_press(VoicePurpose::Dictation));
    h.run_until_result(*h.up, 5000);
    CHECK(h.up->view().voice.err == VoiceErr::NoPi);
  }
}

VTEST(house_end_and_text_timeouts) {
  {
    House h;
    h.settle();
    h.c6.drop_end = true;
    h.pu.mic.release_after_ms = 1000;
    CHECK(h.up->voice_press(VoicePurpose::Dictation));
    h.run_until_result(*h.up, 10000);
    CHECK(h.up->view().voice.err == VoiceErr::SendFailed);
  }
  {
    House h;
    h.settle();
    h.c6.pi_on = false;
    h.pu.mic.release_after_ms = 1000;
    CHECK(h.up->voice_press(VoicePurpose::Dictation));
    h.run(1300);
    CHECK(h.up->view().voice.ui == VoiceUi::Working);
    uint32_t t = h.run_until_result(*h.up, 20000);
    CHECK(h.up->view().voice.err == VoiceErr::NoPi);
    CHECK(t >= 7500 && t <= 8500);
  }
}

VTEST(house_queued_waits_longer) {
  House h;
  h.settle();
  h.c6.queued_at_begin = true;
  h.c6.text_ms = 15000;  // the other floor's message ahead: 15 s after state 2, inside the 20 s
  h.pu.mic.release_after_ms = 1000;
  CHECK(h.up->voice_press(VoicePurpose::Dictation));
  h.run(2000);
  CHECK(h.up->view().voice.ui == VoiceUi::Waiting);
  h.run_until_result(*h.up, 30000);
  CHECK(h.up->view().voice.ui == VoiceUi::Result);
}

VTEST(house_cancel_and_bell) {
  {
    House h;
    h.settle();
    CHECK(h.up->voice_press(VoicePurpose::Dictation));
    h.run(800);
    h.up->voice_cancel();
    h.run(50);
    CHECK(h.up->view().voice.ui == VoiceUi::Idle);
    h.run(350);
    CHECK(count_sent(h.pu, VType::Abort) == 3);
    CHECK(!h.c6.s[0].on);
    CHECK(h.pu.mic.rec == nullptr);
    // A late text for the cancelled job: answered "gone", not shown.
    VoiceMsg t;
    t.type = VType::Text;
    t.sender = kSenderGateway;
    t.floor = 0;
    t.u.text.xfer = h.pu.voice_out.back().bytes[6];
    t.u.text.status = (uint8_t)VoiceStatus::Nothing;
    uint8_t buf[64];
    size_t n = voice_encode(t, buf, sizeof buf);
    size_t before = h.pu.voice_out.size();
    h.up->on_radio(buf, n);
    CHECK(h.pu.voice_out.size() == before + 1);
    VoiceMsg g;
    CHECK(voice_decode(h.pu.voice_out.back().bytes.data(), h.pu.voice_out.back().bytes.size(), g));
    CHECK(g.type == VType::Got && g.u.got.kind == 25 && g.u.got.result == (uint8_t)VoiceGotResult::Gone);
    CHECK(h.up->view().voice.ui == VoiceUi::Idle);
  }
  {  // the doorbell stops the recording (mic task: g_rec_bell -> rec->stop = Bell)
    House h;
    h.settle();
    CHECK(h.up->voice_press(VoicePurpose::Message));
    h.run(500);
    h.pu.mic.rec->stop.store((uint8_t)VoiceStop::Bell);
    h.run(200);
    CHECK(h.up->view().voice.ui == VoiceUi::Error);
    CHECK(h.up->view().voice.err == VoiceErr::Bell);
    h.run(400);
    CHECK(count_sent(h.pu, VType::Abort) == 3);
  }
  {  // the mic task is still inside the clip for a while: freed only when it lets go
    House h;
    h.settle();
    CHECK(h.up->voice_press(VoicePurpose::Dictation));
    h.run(300);
    h.pu.mic.busy_releases = 3;
    int frees = h.pu.frees;
    h.up->voice_cancel();
    CHECK(h.pu.frees == frees);
    CHECK(!h.up->voice_press(VoicePurpose::Dictation));  // the last clip is still held
    h.run(300);
    CHECK(h.pu.frees == frees + 1);
    CHECK(h.up->voice_press(VoicePurpose::Dictation));
  }
}

VTEST(house_press_refused) {
  House h;
  h.settle();
  h.pu.mic_ok = false;
  CHECK(!h.up->voice_press(VoicePurpose::Dictation));
  h.run(50);
  CHECK(h.up->view().voice.ui == VoiceUi::Error);
  CHECK(h.pu.allocs == h.pu.frees + 1);
  h.pu.mic_ok = true;
  h.run(4100);
  CHECK(h.up->voice_press(VoicePurpose::Dictation));
  CHECK(!h.up->voice_press(VoicePurpose::Message));  // one job at a time
}

// =============================================================================================
// SAY, the inbox on the receiving panel, the phones

VTEST(house_say_delivered_ding_push) {
  House h;
  h.settle();
  CHECK(h.up->voice_send("  Dinner is ready  "));
  h.run(200);
  CHECK(h.up->view().voice.ui == VoiceUi::Delivered);
  const VoiceView &d = h.down->view().voice;
  CHECK(d.inbox_n == 1);
  CHECK(!strcmp(d.top.text, "Dinner is ready"));
  CHECK(d.top.from == 0);
  CHECK(d.top_when[0] != 0);  // "12:00 PM"
  CHECK(h.pd.dings() == 1);
  CHECK(h.pd.inbox_saved && h.pd.inbox.n == 1);
  h.run(200);
  CHECK(h.c6.pushes.size() == 1);
  CHECK(count_sent(h.pd, VType::Push) == 1);  // answered: no retry
  h.run(1500);
  CHECK(h.up->view().voice.ui == VoiceUi::Idle);  // the toast went
  // The receiver's screen was woken (voice_wake): lit even in the dark hours.
  for (const std::string &l : h.pd.logs) CHECK(l.find("Dinner") == std::string::npos);
  for (const std::string &l : h.pu.logs) CHECK(l.find("Dinner") == std::string::npos);
}

VTEST(house_say_offline_refused_or_expires) {
  House h;
  h.settle();
  CHECK(!h.up->voice_send(""));
  CHECK(!h.up->voice_send("   "));
  h.air.cut[kSenderUpstairs][kSenderDownstairs] = true;
  CHECK(h.up->voice_send("Back at six"));
  CHECK(!h.up->voice_send("Second"));  // one SAY at a time
  h.run(1600);
  CHECK(h.up->view().voice.ui == VoiceUi::Error);
  CHECK(h.up->view().voice.err == VoiceErr::PeerNoAnswer);
  CHECK(count_sent(h.pu, VType::Say) == 5);
  CHECK(h.up->view().voice.peer_offline);
  CHECK(!h.up->voice_send("Again"));  // offline now
  CHECK(h.down->view().voice.inbox_n == 0);
}

VTEST(house_say_retry_dedup) {
  House h;
  h.settle();
  // The receiver's answers are lost: the SAY is tried 5 times, but shows (and dings) once.
  h.air.cut[kSenderDownstairs][kSenderUpstairs] = true;
  CHECK(h.up->voice_send("Back at six"));
  h.run(1700);
  CHECK(count_sent(h.pu, VType::Say) == 5);
  CHECK(h.down->view().voice.inbox_n == 1);
  CHECK(h.pd.dings() == 1);
  CHECK(count_sent(h.pd, VType::Got) >= 5);
}

VTEST(house_inbox_five_and_restart) {
  House h;
  h.settle();
  for (int i = 0; i < 5; i++) {
    char t[16];
    snprintf(t, sizeof t, "Note %d", i + 1);
    CHECK(h.up->voice_send(t));
    h.run(1700);
    h.up->voice_done();
  }
  CHECK(h.down->view().voice.inbox_n == 4);
  CHECK(!strcmp(h.down->view().voice.top.text, "Note 2"));
  uint16_t seq = h.down->view().voice.inbox_seq;
  CHECK(h.down->voice_dismiss());
  h.run(50);
  CHECK(h.down->view().voice.inbox_seq != seq);
  CHECK(!strcmp(h.down->view().voice.top.text, "Note 3"));
  CHECK(h.pd.inbox.n == 3);
  // A restart of the downstairs panel: the saved inbox comes back, and a retried SAY with a seen id
  // (the one of "Note 5") does not show again.
  uint32_t id5 = h.pd.inbox.msg[2].id;
  h.start(false, true);
  h.run(2000);
  CHECK(h.down->view().voice.inbox_n == 3);
  CHECK(!strcmp(h.down->view().voice.top.text, "Note 3"));
  int dings = h.pd.dings();
  VoiceMsg m;
  m.type = VType::Say;
  m.sender = kSenderUpstairs;
  m.floor = 0;
  m.u.say.id = id5;
  m.u.say.len = 6;
  memcpy(m.u.say.text, "Note 5", 6);
  uint8_t buf[64];
  size_t n = voice_encode(m, buf, sizeof buf);
  h.down->on_radio(buf, n);
  CHECK(h.down->view().voice.inbox_n == 3);
  CHECK(h.pd.dings() == dings);
  VoiceMsg g;
  CHECK(voice_decode(h.pd.voice_out.back().bytes.data(), h.pd.voice_out.back().bytes.size(), g));
  CHECK(g.type == VType::Got && g.u.got.kind == 26 && g.u.got.key == id5 && g.u.got.result == 0);
  while (h.down->voice_dismiss()) {
  }
  h.run(50);
  CHECK(h.down->view().voice.inbox_n == 0);
  CHECK(!h.down->voice_dismiss());
}

VTEST(house_phone_dismiss) {
  House h;
  h.settle();
  CHECK(h.up->voice_send("Parcel at the door"));
  h.run(500);
  CHECK(h.down->view().voice.inbox_n == 1);
  uint32_t id = h.down->view().voice.top.id;
  // Downstairs' dismiss comes relayed by upstairs (the C6's down_via default).
  h.c6.dismiss(1, id, kSenderUpstairs);
  h.run(300);
  CHECK(h.down->view().voice.inbox_n == 0);
  CHECK(h.c6.dismiss_result.count(id) && h.c6.dismiss_result[id] == (uint8_t)VoiceGotResult::Ok);
  CHECK(h.pd.logged("dismissed from a phone"));
  // Again: not there any more.
  h.c6.dismiss_result.clear();
  h.c6.dismiss(1, id, kSenderDownstairs);
  h.run(300);
  CHECK(h.c6.dismiss_result.count(id) && h.c6.dismiss_result[id] == (uint8_t)VoiceGotResult::Gone);
  // Upstairs' own inbox, dismissed directly.
  CHECK(h.down->voice_send("Thanks"));
  h.run(500);
  CHECK(h.up->view().voice.inbox_n == 1);
  uint32_t id2 = h.up->view().voice.top.id;
  h.c6.dismiss(0, id2, kSenderUpstairs);
  h.run(300);
  CHECK(h.up->view().voice.inbox_n == 0);
  // A DISMISS from the other panel (not the gateway's side) is not taken.
  CHECK(h.down->voice_send("Again"));
  h.run(500);
  CHECK(h.up->view().voice.inbox_n == 1);
  VoiceMsg m;
  m.type = VType::Dismiss;
  m.sender = kSenderDownstairs;
  m.floor = 0;
  m.u.dismiss.id = h.up->view().voice.top.id;
  uint8_t buf[16];
  size_t n = voice_encode(m, buf, sizeof buf);
  h.up->on_radio(buf, n);
  CHECK(h.up->view().voice.inbox_n == 1);
}

VTEST(house_ding_rules_by_status_hours_recording) {
  {  // Private: no ding, no wake, no push; the dialog still has it
    House h;
    h.settle();
    h.down->choose_status(Status::Private);
    h.run(300);
    CHECK(h.up->voice_send("Quiet one"));
    h.run(500);
    CHECK(h.down->view().voice.inbox_n == 1);
    CHECK(h.pd.dings() == 0);
    CHECK(h.c6.pushes.empty());
  }
  {  // Busy: ding and push
    House h;
    h.settle();
    h.down->choose_status(Status::Busy);
    h.run(300);
    CHECK(h.up->voice_send("Busy one"));
    h.run(500);
    CHECK(h.pd.dings() == 1);
    CHECK(h.c6.pushes.size() == 1);
  }
  {  // Quiet hours (23:00): nothing but the dialog
    House h;
    h.utc_base = kNoon + 11 * 3600;
    h.settle();
    CHECK(h.up->voice_send("Late one"));
    h.run(500);
    CHECK(h.down->view().voice.inbox_n == 1);
    CHECK(h.pd.dings() == 0);
    CHECK(h.c6.pushes.empty());
  }
  {  // The screen's dark hours outside quiet hours: no ding and no wake, but the phones are pushed
    House h;
    h.settle();
    Settings s = h.down->settings();
    s.quiet_start_min = s.quiet_end_min = 0;  // no quiet hours
    s.screen_off_overnight = true;
    s.screen_off_start_min = 11 * 60;
    s.screen_off_end_min = 13 * 60;
    h.down->apply_settings(s);
    CHECK(h.up->voice_send("Dark one"));
    h.run(500);
    CHECK(h.pd.dings() == 0);
    CHECK(h.c6.pushes.size() == 1);
  }
  {  // Recording: the ding waits until the recording ends
    House h;
    h.settle();
    CHECK(h.down->voice_press(VoicePurpose::Dictation));
    h.run(300);
    CHECK(h.up->voice_send("While you talk"));
    h.run(300);
    CHECK(h.down->view().voice.inbox_n == 1);
    CHECK(h.pd.dings() == 0);
    h.run(3000);  // the countdown ends it
    CHECK(h.pd.dings() == 1);
  }
  {  // A doorbell banner up: no extra ding
    House h;
    h.settle();
    h.up->tap_bell();
    h.run(300);
    CHECK(h.down->view().banner.kind != BannerKind::None);
    int before = h.pd.dings();
    CHECK(h.up->voice_send("At the door"));
    h.run(500);
    CHECK(h.pd.dings() == before);
    CHECK(h.down->view().voice.inbox_n == 1);
  }
}

VTEST(house_push_expires_quietly) {
  House h;
  h.settle();
  h.air.cut[kSenderDownstairs][kSenderGateway] = true;  // downstairs' auto route is direct until pings fail
  CHECK(h.up->voice_send("Push me"));
  h.run(2000);
  CHECK(h.down->view().voice.inbox_n == 1);
  CHECK(count_sent(h.pd, VType::Push) == 5);
  CHECK(h.pd.logged("VOICE_PUSH had no answer"));
  CHECK(!h.down->view().voice.peer_offline);  // a PUSH failing never marks the other panel
}

VTEST(house_pings_auto_route) {
  House h;
  h.settle();
  h.run(4000);
  CHECK(count_sent(h.pd, VType::Ping, kSenderGateway) == 1);  // a few seconds after start-up
  CHECK(h.down->voice_direct_ok());
  CHECK(count_sent(h.pu, VType::Ping) == 0);                  // upstairs never pings
  h.c6.answer_pings = false;
  h.run(600000);
  h.run(8000);
  CHECK(!h.down->voice_direct_ok());
  h.down->set_voice_route(Controller::VoiceRoute::Direct);
  int n = count_sent(h.pd, VType::Ping);
  h.run(700000);
  CHECK(count_sent(h.pd, VType::Ping) == n);  // a forced route never pings
  char line[400];
  h.down->voice_report(line, sizeof line);
  CHECK(strstr(line, "route direct") != nullptr);
}

// =============================================================================================
// The same house against piece D's gateway (gw::Gateway with its VoiceSession)

VTEST(gw_dictation_both_floors) {
  for (int fl = 0; fl < 2; fl++) {
    House h(true);
    h.settle();
    Controller &c = fl ? *h.down : *h.up;
    Panel &p = fl ? h.pd : h.pu;
    p.mic.release_after_ms = 2000;
    CHECK(c.voice_press(VoicePurpose::Dictation));
    h.run_until_result(c, 15000);
    CHECK(c.view().voice.ui == VoiceUi::Result);
    CHECK(!strcmp(c.view().voice.text, "Dinner is ready"));
    CHECK(c.view().voice.text_flags == kVoiceTextSentenceCap);
    CHECK(h.real->voice_ends == 1);
    CHECK(h.real->count(fl ? "\"f\":\"down\"" : "\"f\":\"up\"") > 50);
    CHECK(h.real->count("\"acked\":") >= 1);  // the voice_text request was confirmed by the panel's VOICE_GOT
    CHECK(h.real->window_violations == 0 && p.mic.window_violations == 0);
  }
}

VTEST(gw_lossy_both_floors) {
  int ok = 0, runs = 0, extra = 0;
  for (uint32_t seed = 1; seed <= 12; seed++)
    for (int fl = 0; fl < 2; fl++) {
      House h(true);
      h.settle();
      h.air.rng = Rng(seed * 69069u + (uint32_t)fl * 7u);
      h.air.drop = 10;
      h.air.dup = 5;
      h.air.jitter = 25;
      Controller &c = fl ? *h.down : *h.up;
      Panel &p = fl ? h.pd : h.pu;
      p.mic.release_after_ms = 1500 + seed * 100;
      CHECK(c.voice_press(VoicePurpose::Dictation));
      h.run_until_result(c, 20000);
      runs++;
      bool good = c.view().voice.ui == VoiceUi::Result;
      ok += good ? 1 : 0;
      if (!good) printf("  gw lossy seed %u floor %d: ui %d err %d\n", seed, fl, (int)c.view().voice.ui, (int)c.view().voice.err);
      CHECK(h.real->window_violations == 0 && p.mic.window_violations == 0);
      int chunks = 0, total = 0;
      for (const auto &kv : data_sends(h.air, p.id)) {
        CHECK(kv.second <= kVoiceSendsPerChunk);
        chunks++;
        total += kv.second;
      }
      CHECK(total <= 3 * chunks);
      extra += total - chunks;
    }
  CHECK(ok == runs);
  CHECK(extra > 50);
}

VTEST(gw_relay_and_fallback) {
  {  // forced relay
    House h(true);
    h.settle();
    h.down->set_voice_route(Controller::VoiceRoute::Relay);
    h.pd.mic.release_after_ms = 1500;
    CHECK(h.down->voice_press(VoicePurpose::Message));
    h.run_until_result(*h.down, 15000);
    CHECK(h.down->view().voice.ui == VoiceUi::Result);
    CHECK(h.real->count("\"via\":\"upstairs\"") == 1);
    CHECK(h.relayed > 50);
  }
  {  // auto, the direct link dead: BEGIN via upstairs from 450 ms
    House h(true);
    h.settle();
    h.air.cut[kSenderDownstairs][kSenderGateway] = true;
    h.air.cut[kSenderGateway][kSenderDownstairs] = true;
    h.pd.mic.release_after_ms = 1500;
    CHECK(h.down->voice_press(VoicePurpose::Message));
    h.run_until_result(*h.down, 15000);
    CHECK(h.down->view().voice.ui == VoiceUi::Result);
    h.down->voice_done();
    h.run(8000);
    CHECK(!h.down->voice_direct_ok());  // the gateway's pings never came back
  }
  {  // auto, direct fine: the ping is answered by the real gateway
    House h(true);
    h.settle();
    h.run(4000);
    CHECK(count_sent(h.pd, VType::Ping) == 1);
    CHECK(h.down->voice_direct_ok());
    CHECK(h.pd.logs.size() == 0 || !h.pd.logged("unanswered"));
  }
}

VTEST(gw_message_push_and_phone_dismiss) {
  House h(true);
  h.settle();
  CHECK(h.up->voice_send("Parcel at the door"));
  h.run(600);
  CHECK(h.up->view().voice.ui == VoiceUi::Delivered);
  CHECK(h.down->view().voice.inbox_n == 1);
  CHECK(h.real->msgs == 1);  // one voice_msg line for the downstairs phones
  CHECK(h.real->count("\"to\":\"downstairs\"") == 1);
  uint32_t id = h.down->view().voice.top.id;
  h.real->phone_ack("downstairs", id);
  h.run(600);
  CHECK(h.down->view().voice.inbox_n == 0);
  CHECK(h.real->count("\"result\":\"removed\"") == 1);
  h.real->phone_ack("downstairs", id);  // the second phone: already gone
  h.run(600);
  CHECK(h.real->count("\"result\":\"gone\"") == 1);
  // The other way: upstairs' own phones, its inbox dismissed directly.
  CHECK(h.down->voice_send("Thanks"));
  h.run(600);
  CHECK(h.up->view().voice.inbox_n == 1);
  h.real->phone_ack("upstairs", h.up->view().voice.top.id);
  h.run(600);
  CHECK(h.up->view().voice.inbox_n == 0);
}

// =============================================================================================
// Field fix 2026-11-20 (docs/voice-notes/WORKLOG.md): with the C6 still on 0.4 the voice firmware sent
// downstairs' VOICE_PINGs to it, and in the field the C6 then got no TComm ACK from upstairs until both
// the panels were rolled back and the C6 restarted. Now nothing voice-related goes to a gateway that
// has not sent its voice hello, and these tests hold the air to that.

struct AirStats {
  int voice = 0, voice_to_gw = 0, voice_from_down_to_gw = 0, worst_second = 0;
};
AirStats air_stats(const Air &a, uint32_t t0, uint32_t t1) {
  AirStats s;
  std::map<uint32_t, int> per_s;
  for (const Air::Seen &x : a.log) {
    if (ms_diff(x.at, t0) < 0 || ms_diff(x.at, t1) >= 0 || !is_voice_frame(x.d.data(), x.d.size())) continue;
    s.voice++;
    if (x.to == kSenderGateway) s.voice_to_gw++;
    if (x.to == kSenderGateway && x.from == kSenderDownstairs) s.voice_from_down_to_gw++;
    int n = ++per_s[x.at / 1000];
    if (n > s.worst_second) s.worst_second = n;
  }
  return s;
}

VTEST(fieldfix_old_c6_idle_ten_minutes) {
  House h;
  h.c6.old04 = true;  // the C6 in the field tonight: gateway-c6 0.4
  h.c6.outside_every_ms = 60000;
  const uint32_t t0 = g_now;
  h.run(600000);
  const AirStats s = air_stats(h.air, t0, g_now);
  printf("  old C6, 10 min idle: %d voice frames on the air (%d to the C6), OUTSIDE %d sent, %d acked (%d extra tries, "
         "worst %lu ms), %d failed\n",
         s.voice, s.voice_to_gw, h.c6.outside_sent, h.c6.outside_acked, h.c6.outside_extra_tries,
         (unsigned long)h.c6.outside_worst_ms, h.c6.outside_failed);
  CHECK(s.voice == 0);  // not one voice frame from either panel, to anyone
  CHECK(h.c6.voice_in == 0);
  CHECK(h.c6.outside_sent == 10);
  CHECK(h.c6.outside_acked == h.c6.outside_sent && h.c6.outside_failed == 0);
  CHECK(h.c6.outside_extra_tries == 0);  // each on its first try
  CHECK(h.c6.outside_worst_ms <= 70);
  CHECK(!h.up->voice_gateway_known() && !h.down->voice_gateway_known());
  CHECK(count_sent(h.pd, VType::Ping) == 0 && count_sent(h.pu, VType::Ping) == 0);
  // Someone presses the mic on either panel: "Gateway needs an update", and still nothing goes out.
  const int ua = h.pu.allocs, da = h.pd.allocs;
  CHECK(!h.up->voice_press(VoicePurpose::Dictation));
  CHECK(!h.down->voice_press(VoicePurpose::Message));
  h.run(100);
  CHECK(h.up->view().voice.ui == VoiceUi::Error && h.up->view().voice.err == VoiceErr::GatewayOld);
  CHECK(h.down->view().voice.ui == VoiceUi::Error && h.down->view().voice.err == VoiceErr::GatewayOld);
  CHECK(h.pu.allocs == ua && h.pd.allocs == da);  // no clip taken
  // A typed message between the panels still works; the phones are not told through a C6 0.4.
  h.run(4000);  // the error shows 4 s
  CHECK(h.up->voice_send("Parcel at the door"));
  h.run(5000);
  CHECK(h.down->view().voice.inbox_n == 1);
  CHECK(h.pd.logged("phones were not told"));
  const AirStats s2 = air_stats(h.air, t0, g_now);
  CHECK(s2.voice_to_gw == 0 && h.c6.voice_in == 0);
  CHECK(s2.voice == 2);  // the SAY and its GOT, panel to panel
  // Ten more minutes: still nothing to the C6, and every OUTSIDE acked.
  h.run(600000);
  CHECK(air_stats(h.air, t0, g_now).voice_to_gw == 0);
  CHECK(h.c6.outside_acked == h.c6.outside_sent && h.c6.outside_failed == 0 && h.c6.outside_extra_tries == 0);
  char line[400];
  h.down->voice_report(line, sizeof line);
  CHECK(strstr(line, "no voice hello") != nullptr);
}

VTEST(fieldfix_c6_rolled_back_panels_go_quiet) {
  // A C6 0.5 that is put back on 0.4: the hellos stop, and 10 minutes later both panels stop sending
  // it anything (downstairs' pings with them).
  House h;
  h.c6.outside_every_ms = 60000;
  h.settle();
  h.run(5000);
  CHECK(h.up->voice_gateway_known() && h.down->voice_gateway_known());
  CHECK(count_sent(h.pd, VType::Ping) == 1);
  h.c6.old04 = true;
  h.run(kVoiceHelloEveryMs + kVoiceGwStaleMs + 1000);
  CHECK(!h.up->voice_gateway_known() && !h.down->voice_gateway_known());
  CHECK(h.pu.logged("no voice hello from the gateway"));
  const uint32_t t1 = g_now;
  h.run(3600000);  // an hour
  CHECK(air_stats(h.air, t1, g_now).voice == 0);
  CHECK(h.c6.outside_acked == h.c6.outside_sent && h.c6.outside_failed == 0);
  // And back on 0.5: the next hello turns voice on again on both panels.
  h.c6.old04 = false;
  h.run(kVoiceHelloEveryMs + 1000);
  CHECK(h.up->voice_gateway_known() && h.down->voice_gateway_known());
}

// Mike, 2026-11-21: the codec by capability. The new panels with the C6 still on 0.5 (its hello says key
// 0: ADPCM only, at most 128 chunks): both floors record in ADPCM (104-chunk clips) and every press, either
// floor, twice, WORKS (whole-tree check 2026-11-21 had each one end in "Gateway needs an update" with
// mu-law). Only `voice codec 1` by hand still sends it mu-law, and that fails safe as before: "Gateway
// needs an update" within the BEGIN give-up, a bounded handful of frames, nothing after.
// The first BEGIN a panel put on the air (from the air log), decoded.
static bool begin_from(const Air &a, uint8_t from, VoiceMsg &m) {
  for (const Air::Seen &s : a.log)
    if (s.from == from && s.d.size() >= 2 && s.d[1] == (uint8_t)VType::Begin && voice_decode(s.d.data(), s.d.size(), m))
      return true;
  return false;
}

VTEST(fieldfix3_mulaw_panel_with_c6_05) {
  House h;
  h.c6.old05 = true;
  h.settle();
  CHECK(h.up->voice_gateway_known() && h.down->voice_gateway_known());
  CHECK(!h.up->voice_gateway_mulaw() && !h.down->voice_gateway_mulaw());
  CHECK(h.up->voice_codec() == VoiceCodec::Adpcm && h.down->voice_codec() == VoiceCodec::Adpcm);
  for (int floor = 0; floor < 2; floor++) {
    Controller &c = floor ? *h.down : *h.up;
    for (int press = 0; press < 2; press++) {
      const uint32_t t0 = g_now;
      CHECK(c.voice_press(VoicePurpose::Message));
      const uint32_t took = h.run_until_result(c, 15000);
      CHECK(c.view().voice.ui == VoiceUi::Result && !strcmp(c.view().voice.text, "Dinner is ready"));
      VoiceMsg b;
      bool found = false;
      for (const Air::Seen &x : h.air.log)
        if (ms_diff(x.at, t0) >= 0 && x.from == (floor ? kSenderDownstairs : kSenderUpstairs) && x.d.size() >= 2 &&
            x.d[1] == (uint8_t)VType::Begin && voice_decode(x.d.data(), x.d.size(), b)) {
          found = true;
          break;
        }
      CHECK(found && b.u.begin.codec == 2 && b.u.begin.chunk_samples == kVoiceChunkSamplesAdpcm &&
            b.u.begin.max_chunks == kVoiceClipChunks && b.u.begin.max_chunks <= 128);
      if (press == 0)
        printf("  new panel %s with C6 0.5: ADPCM, words in %lu ms, %d voice frames\n", floor ? "downstairs" : "upstairs",
               (unsigned long)took, air_stats(h.air, t0, g_now).voice);
      c.voice_done();
      h.run(1000);
      CHECK(c.view().voice.ui == VoiceUi::Idle);
    }
  }
  CHECK(h.c6.out_pending == false);
  char rep[700];
  h.down->voice_report(rep, sizeof rep);
  CHECK(strstr(rep, "codec 2 ADPCM") != nullptr && strstr(rep, "by hand") == nullptr);
  // mu-law by hand: refused by the 0.5 C6, and it fails safe as before.
  h.up->set_voice_codec(VoiceCodec::Mulaw);
  CHECK(h.up->voice_codec() == VoiceCodec::Mulaw && h.up->voice_codec_by_hand());
  const uint32_t t0 = g_now;
  CHECK(h.up->voice_press(VoicePurpose::Message));
  const uint32_t took = h.run_until_result(*h.up, 15000);
  CHECK(h.up->view().voice.ui == VoiceUi::Error && h.up->view().voice.err == VoiceErr::GatewayOld);
  CHECK(took <= kVoiceBeginGiveUpMs + 200);
  h.run(5000);  // ABORTs, then the 4 s error
  const AirStats s = air_stats(h.air, t0, g_now);
  printf("  mu-law by hand with C6 0.5: \"Gateway needs an update\" in %lu ms, %d voice frames (%d to the C6)\n",
         (unsigned long)took, s.voice, s.voice_to_gw);
  CHECK(s.voice_to_gw <= kVoiceWindow + 12 + kVoiceAbortSends);
  CHECK(h.up->view().voice.ui == VoiceUi::Idle);
  h.up->voice_report(rep, sizeof rep);
  CHECK(strstr(rep, "codec 1 mu-law by hand") != nullptr);
  // `voice codec 0`: back to what the gateway takes, and it works again.
  h.up->set_voice_codec_auto();
  CHECK(h.up->voice_codec() == VoiceCodec::Adpcm && !h.up->voice_codec_by_hand());
  CHECK(h.up->voice_press(VoicePurpose::Dictation));
  h.run_until_result(*h.up, 15000);
  CHECK(h.up->view().voice.ui == VoiceUi::Result);
}

// The C6 flashed from 0.5 to 0.6 and back while the panels run: the next hello changes the codec of the
// next recording on both panels (downstairs through upstairs' STATE, kStateGwMulaw), each logged once.
VTEST(codec_follows_the_c6_hello) {
  House h;
  h.c6.old05 = true;
  h.settle();
  CHECK(h.up->voice_codec() == VoiceCodec::Adpcm && h.down->voice_codec() == VoiceCodec::Adpcm);
  h.c6.old05 = false;  // 0.6 now: its next hello is key 1
  h.run(kVoiceHelloEveryMs + 1000);
  CHECK(h.up->voice_gateway_mulaw() && h.down->voice_gateway_mulaw());
  CHECK(h.up->voice_codec() == VoiceCodec::Mulaw && h.down->voice_codec() == VoiceCodec::Mulaw);
  CHECK(h.pu.logged("the gateway takes mu-law"));
  char rep[700];
  h.down->voice_report(rep, sizeof rep);
  CHECK(strstr(rep, ", mu-law too; codec 1 mu-law") != nullptr);
  for (int floor = 0; floor < 2; floor++) {
    Controller &c = floor ? *h.down : *h.up;
    h.air.log.clear();
    CHECK(c.voice_press(VoicePurpose::Message));
    h.run_until_result(c, 15000);
    CHECK(c.view().voice.ui == VoiceUi::Result);
    VoiceMsg b;
    CHECK(begin_from(h.air, floor ? kSenderDownstairs : kSenderUpstairs, b) && b.u.begin.codec == 1 &&
          b.u.begin.max_chunks == kVoiceClipChunksMulaw);
    c.voice_done();
    h.run(1000);
  }
  h.c6.old05 = true;  // rolled back to 0.5: key 0 again
  h.run(kVoiceHelloEveryMs + 1000);
  CHECK(!h.up->voice_gateway_mulaw() && !h.down->voice_gateway_mulaw());
  CHECK(h.up->voice_codec() == VoiceCodec::Adpcm && h.down->voice_codec() == VoiceCodec::Adpcm);
  CHECK(h.pu.logged("the gateway takes ADPCM only"));
  h.air.log.clear();
  CHECK(h.down->voice_press(VoicePurpose::Message));
  h.run_until_result(*h.down, 15000);
  CHECK(h.down->view().voice.ui == VoiceUi::Result);
  VoiceMsg b;
  CHECK(begin_from(h.air, kSenderDownstairs, b) && b.u.begin.codec == 2 && b.u.begin.max_chunks == kVoiceClipChunks);
  // A C6 that stops saying hello (back on 0.4): mu-law goes with the voice, both panels.
  h.c6.old05 = false;
  h.run(kVoiceHelloEveryMs + 1000);
  CHECK(h.down->voice_gateway_mulaw());
  h.c6.old04 = true;
  h.run(kVoiceHelloEveryMs + kVoiceGwStaleMs + 1000);
  CHECK(!h.up->voice_gateway_known() && !h.down->voice_gateway_known());
  CHECK(!h.up->voice_gateway_mulaw() && !h.down->voice_gateway_mulaw());
}

VTEST(fieldfix_ping_backoff) {
  // A C6 that says hello but never answers a ping: 3 misses, then 10, 20, 40, 60, 60 minutes apart.
  House h;
  h.c6.answer_pings = false;
  h.run(4 * 3600000u);
  const int n = count_sent(h.pd, VType::Ping);
  printf("  pings in 4 hours, none answered: %d\n", n);
  CHECK(n >= 6 && n <= 9);  // 3 + at about 10, 30, 70, 130, 190 min
}

VTEST(fieldfix_new_c6_idle_ten_minutes_then_recordings) {
  House h(true);  // piece D's gateway, 0.5
  const uint32_t t0 = g_now;
  int requests = 0;
  for (int minute = 0; minute < 10; minute++) {
    h.real->outside();
    requests++;
    h.run(60000);
  }
  const AirStats s = air_stats(h.air, t0, g_now);
  printf("  C6 0.5, 10 min idle: %d voice frames on the air (%.4f a second, worst second %d; %d to the C6, %d from "
         "downstairs), OUTSIDE %d of %d acked\n",
         s.voice, s.voice / 600.0, s.worst_second, s.voice_to_gw, s.voice_from_down_to_gw,
         h.real->count("\"acked\":\"upstairs\""), requests);
  CHECK(h.real->count("\"acked\":\"upstairs\"") == requests);
  CHECK(h.real->count("\"failed\"") == 0);
  CHECK(s.voice <= 12);  // 5 hellos, downstairs' first ping and its answer (and the 10-minute one)
  CHECK(s.worst_second <= 2);
  CHECK(h.up->voice_gateway_known() && h.down->voice_gateway_known());
  CHECK(count_sent(h.pd, VType::Ping) >= 1 && count_sent(h.pu, VType::Ping) == 0);
  CHECK(h.down->voice_direct_ok());
  // A recording each way.
  h.pu.mic.release_after_ms = 2000;
  CHECK(h.up->voice_press(VoicePurpose::Dictation));
  h.run_until_result(*h.up, 15000);
  CHECK(h.up->view().voice.ui == VoiceUi::Result && !strcmp(h.up->view().voice.text, "Dinner is ready"));
  h.up->voice_done();
  h.pd.mic.release_after_ms = 1500;
  CHECK(h.down->voice_press(VoicePurpose::Message));
  h.run_until_result(*h.down, 15000);
  CHECK(h.down->view().voice.ui == VoiceUi::Result && !strcmp(h.down->view().voice.text, "Dinner is ready"));
  h.down->voice_done();
  CHECK(h.real->voice_ends == 2);
  // An OUTSIDE straight after still gets its ACK.
  h.real->outside();
  h.run(2000);
  int outside_acked = 0;
  for (const std::string &l : h.real->lines)
    outside_acked += l.find("\"type\":\"outside\"") != std::string::npos && l.find("\"acked\":\"upstairs\"") != std::string::npos;
  CHECK(outside_acked == requests + 1);
  CHECK(h.real->count("\"failed\"") == 0);
  CHECK(h.real->g.status().voice_capped == 0);
}

// ---------------------------------------------------------------------------------------------
// Field fix 2026-11-21: mu-law by default, 255 chunks of index space, the panel's air cap, the codec
// setting and the badge row's live meter.

namespace {

// The largest "i" in the C6's voice lines for a floor, and how many there were.
int max_voice_idx(const RealC6 &c, const char *f, int &lines) {
  int top = -1;
  lines = 0;
  for (const std::string &l : c.lines) {
    if (l.find("\"event\":\"voice\"") == std::string::npos || l.find(f) == std::string::npos) continue;
    lines++;
    int i = line_int(l, "\"i\":");
    if (i > top) top = i;
  }
  return top;
}

// The most voice frames one device put on the air in any 1000 ms window (from the air log).
int worst_second(const Air &a, uint8_t from) {
  std::vector<uint32_t> t;
  for (const Air::Seen &s : a.log)
    if (s.from == from && is_voice_frame(s.d.data(), s.d.size())) t.push_back(s.at);
  int worst = 0;
  size_t j = 0;
  for (size_t i = 0; i < t.size(); i++) {
    while (ms_diff(t[i], t[j]) >= 1000) j++;
    int n = (int)(i - j + 1);
    if (n > worst) worst = n;
  }
  return worst;
}

const VoiceMsg *first_begin(const Air &a, uint8_t from, VoiceMsg &m) {
  for (const Air::Seen &s : a.log)
    if (s.from == from && s.d.size() >= 2 && s.d[1] == (uint8_t)VType::Begin && voice_decode(s.d.data(), s.d.size(), m))
      return &m;
  return nullptr;
}

}  // namespace

// A whole 3 s mu-law clip (about 203 chunks) on each floor and on the relay route, end to end through
// piece D's gateway, with both panels' air caps on: every chunk reaches the Pi, the cap is never hit on a
// clean link, and no panel puts more than kVoicePanelPerS voice frames on the air in any second.
VTEST(gw_mulaw_full_clip_end_to_end) {
  for (int route = 0; route < 3; route++) {  // 0 upstairs, 1 downstairs direct, 2 downstairs via upstairs
    House h(true);
    h.pu.cap_on();
    h.pd.cap_on();
    h.settle();
    const bool down = route > 0;
    if (route == 2) h.down->set_voice_route(Controller::VoiceRoute::Relay);
    Controller &c = down ? *h.down : *h.up;
    Panel &p = down ? h.pd : h.pu;
    CHECK(c.voice_codec() == VoiceCodec::Mulaw);
    CHECK(c.voice_press(VoicePurpose::Message));
    uint32_t took = h.run_until_result(c, 15000);
    CHECK(c.view().voice.ui == VoiceUi::Result && !strcmp(c.view().voice.text, "Dinner is ready"));
    CHECK(took < 5500);
    VoiceMsg b;
    CHECK(first_begin(h.air, p.id, b) != nullptr);
    CHECK(b.u.begin.codec == 1 && b.u.begin.chunk_samples == 237 && b.u.begin.max_chunks == kVoiceClipChunksMulaw);
    CHECK(h.real->count("\"codec\":1,\"cs\":237") == 1);
    int lines = 0;
    int top = max_voice_idx(*h.real, down ? "\"f\":\"down\"" : "\"f\":\"up\"", lines);
    CHECK(top >= 201 && top < kVoiceClipChunksMulaw);  // 3.0 s: 48,000 samples = 202.5 chunks
    CHECK(lines == top + 1);                           // each chunk written to the Pi once, none missing
    CHECK(h.real->count("\"n\":20") == 1);             // voice_end with n 202 or 203
    CHECK(h.real->window_violations == 0 && p.mic.window_violations == 0);
    CHECK(h.pu.cap_refused == 0 && h.pd.cap_refused == 0);
    CHECK(worst_second(h.air, kSenderUpstairs) <= (int)kVoicePanelPerS);
    CHECK(worst_second(h.air, kSenderDownstairs) <= (int)kVoicePanelPerS);
    // The stream itself: about 68 DATA frames a second from the recording panel.
    int data = h.air.count(p.id, route == 2 ? kSenderUpstairs : kSenderGateway, VType::Data);
    CHECK(data >= top + 1 && data <= top + 10);
    if (route == 2) CHECK(h.air.count(kSenderUpstairs, kSenderGateway, VType::Data) >= top + 1);
    printf("  mu-law clip route %d: %d chunks, %d DATA, worst second up %d down %d frames, %u ms to the words\n", route,
           top + 1, data, worst_second(h.air, kSenderUpstairs), worst_second(h.air, kSenderDownstairs), took);
  }
}

// The same through 10 % loss, 5 % duplicates and reordering on every voice hop, air caps on, against
// piece D's gateway as it is (0.5): every run completes.
VTEST(gw_mulaw_lossy_capped) {
  int ok = 0, runs = 0, extra = 0, refused = 0;
  for (uint32_t seed = 1; seed <= 12; seed++)
    for (int route = 0; route < 3; route++) {
      House h(true);
      h.pu.cap_on();
      h.pd.cap_on();
      h.settle();
      h.air.rng = Rng(seed * 2246822519u + (uint32_t)route * 13u);
      h.air.drop = route == 2 ? 8 : 10;  // the relay route: 8 % on each of its two hops, as house_relay_route_lossy
      h.air.dup = 5;
      h.air.jitter = 25;
      const bool down = route > 0;
      if (route == 2) h.down->set_voice_route(Controller::VoiceRoute::Relay);
      Controller &c = down ? *h.down : *h.up;
      Panel &p = down ? h.pd : h.pu;
      CHECK(c.voice_press(VoicePurpose::Dictation));
      h.run_until_result(c, 20000);
      runs++;
      bool good = c.view().voice.ui == VoiceUi::Result;
      ok += good ? 1 : 0;
      if (!good) printf("  mu-law lossy seed %u route %d: ui %d err %d\n", seed, route, (int)c.view().voice.ui, (int)c.view().voice.err);
      CHECK(h.real->window_violations == 0 && p.mic.window_violations == 0);
      int chunks = 0, total = 0;
      for (const auto &kv : data_sends(h.air, p.id)) {
        CHECK(kv.second <= kVoiceSendsPerChunk);
        chunks++;
        total += kv.second;
      }
      CHECK(total <= 3 * chunks);
      CHECK(chunks >= 200);
      extra += total - chunks;
      refused += h.pu.cap_refused + h.pd.cap_refused;
    }
  CHECK(ok == runs);
  CHECK(extra > 100);
  printf("  mu-law lossy: %d of %d runs, %d resends, %d frames over the air cap\n", ok, runs, extra, refused);
}

// The top of the index space on the C6: a BEGIN for 255 chunks, DATA 0..254 out of order with
// duplicates, END 255: state 2 at base 255 and every chunk written to the Pi once.
VTEST(gw_mulaw_index_space_255) {
  g_now = 1000;
  Air air;
  RealC6 c6;
  c6.air = &air;
  c6.start();
  auto frame = [&](VoiceMsg &m) {
    m.sender = kSenderDownstairs;
    m.floor = 1;
    uint8_t b[kMaxMsg];
    size_t n = voice_encode(m, b, sizeof b);
    CHECK(n > 0);
    c6.on_frame(kSenderDownstairs, std::vector<uint8_t>(b, b + n));
    g_now += 2;
    c6.tick();
  };
  VoiceMsg m;
  m.type = VType::Begin;
  m.u.begin.xfer = 77;
  m.u.begin.purpose = 1;
  m.u.begin.codec = 1;
  m.u.begin.chunk_samples = 237;
  m.u.begin.max_chunks = 255;
  frame(m);
  const gw::VoiceSession &vs = c6.g.voice_session(1);
  CHECK(vs.state == gw::VoiceSession::Open && vs.max_chunks == 255 && vs.codec == 1);
  // In windows of 32, each window's chunks backwards, every fifth one twice.
  for (int w = 0; w < 255; w += 32) {
    for (int i = (w + 31 < 254 ? w + 31 : 254); i >= w; i--) {
      VoiceMsg d;
      d.type = VType::Data;
      d.u.data.xfer = 77;
      d.u.data.idx = (uint8_t)i;
      d.u.data.len = 237;
      for (int k = 0; k < 237; k++) d.u.data.data[k] = (uint8_t)(i + k);
      frame(d);
      if (i % 5 == 0) frame(d);
    }
  }
  CHECK(vs.received() == 255 && vs.base() == 255 && vs.has(254) && !vs.has(255));
  VoiceMsg e;
  e.type = VType::End;
  e.u.end.xfer = 77;
  e.u.end.n_chunks = 255;
  e.u.end.n_samples = (uint16_t)(255 * 237);
  e.u.end.speech = 1;
  frame(e);
  int lines = 0;
  int top = max_voice_idx(c6, "\"f\":\"down\"", lines);
  CHECK(top == 254 && lines == 255);
  CHECK(c6.voice_ends == 1 && c6.count("\"n\":255,\"ns\":60435") == 1);
  // The last ACK to downstairs: state 2, base 255.
  bool complete = false;
  for (const Air::Seen &s : air.log) {
    VoiceMsg a;
    if (s.to == kSenderDownstairs && voice_decode(s.d.data(), s.d.size(), a) && a.type == VType::Ack)
      complete = a.u.ack.state == (uint8_t)VoiceAckState::Complete && a.u.ack.base == 255;
  }
  CHECK(complete);
  // VoiceSession alone: the last bits, and nothing past max_chunks.
  gw::VoiceSession s2;
  s2.open(5, 1, 1, 237, 255, gw::kViaDirect, 0);
  CHECK(s2.mark(254) && !s2.mark(254) && !s2.mark(255) && s2.has(254) && s2.base() == 0);
  for (int i = 0; i < 254; i++) s2.mark(i);
  CHECK(s2.base() == 255 && s2.received() == 255);
  s2.clear();
  CHECK(!s2.has(254) && s2.received() == 0);
}

// The panel's resends go in rounds with a quiet gap between them, so the C6's "80 ms after the last
// DATA" ACK gets out while the window is full (the stall that mu-law's longer clip made likely).
VTEST(tx_resend_rounds_leave_quiet) {
  g_now = 1000;
  Capture l;
  std::vector<uint8_t> mem(voice_rec_bytes((uint8_t)kVoiceClipChunksMulaw) + 8);
  VoiceRec *rec = voice_rec_init(mem.data(), mem.size(), (uint8_t)kVoiceClipChunksMulaw);
  rec->xfer = 9;
  rec->codec = 1;
  rec->chunk_samples = 237;
  VoiceTx tx;
  tx.start(rec, VoicePurpose::Dictation, kSenderGateway, -1, g_now, l);
  tx.on_ack(ack_msg(0, 9, 0, 0, VoiceAckState::Open), g_now);
  // 32 chunks out (the window is full), the C6 has every other one.
  for (int i = 0; i < 32; i++) {
    rec->chunks[i].len = 237;
    rec->first_ms[i] = 1000 + (uint32_t)i * 15;  // first sent over 0.5 s, as a 68-a-second stream
  }
  rec->chunks_ready.store(32);
  rec->first_sent.store(32);
  uint32_t odd = 0;
  for (int k = 1; k < 32; k += 2) odd |= 1u << k;
  tx.on_ack(ack_msg(0, 9, 0, odd, VoiceAckState::Open), g_now);
  l.out.clear();
  std::vector<uint32_t> at;
  for (int t = 0; t < 40 && tx.phase() == VoiceTxPhase::Stream; t++) {
    g_now += 50;
    size_t before = l.out.size();
    tx.tick(g_now, l);
    for (size_t k = before; k < l.out.size(); k++)
      if (l.out[k].d[1] == (uint8_t)VType::Data) at.push_back(g_now);
  }
  CHECK(at.size() >= 16);
  // Somewhere after the first round, a gap of at least kVoiceResendQuietMs with no resend.
  uint32_t gap = 0;
  for (size_t k = 1; k < at.size(); k++) gap = at[k] - at[k - 1] > gap ? at[k] - at[k - 1] : gap;
  CHECK(gap >= kVoiceResendQuietMs);
  for (const auto &f : l.out)
    if (f.d[1] == (uint8_t)VType::Data) CHECK(f.d[7] % 2 == 0);  // only the missing ones
  voice_rec_destroy(rec);
}

// The codec is a setting: mu-law by default, ADPCM with `voice codec 2`, the clip sized to match.
VTEST(house_codec_setting) {
  House h;
  h.settle();
  CHECK(h.up->voice_codec() == VoiceCodec::Mulaw);
  CHECK(h.up->voice_press(VoicePurpose::Dictation));
  h.run_until_result(*h.up, 15000);
  CHECK(h.up->view().voice.ui == VoiceUi::Result);
  VoiceMsg b;
  CHECK(first_begin(h.air, kSenderUpstairs, b) && b.u.begin.codec == 1 && b.u.begin.max_chunks == kVoiceClipChunksMulaw);
  h.up->voice_done();
  h.run(300);
  h.up->set_voice_codec(VoiceCodec::Adpcm);
  CHECK(h.up->voice_codec() == VoiceCodec::Adpcm);
  char rep[600];
  h.up->voice_report(rep, sizeof rep);
  CHECK(strstr(rep, "codec 2 ADPCM") != nullptr);
  h.air.log.clear();
  CHECK(h.up->voice_press(VoicePurpose::Dictation));
  h.run_until_result(*h.up, 15000);
  CHECK(h.up->view().voice.ui == VoiceUi::Result);
  CHECK(first_begin(h.air, kSenderUpstairs, b) && b.u.begin.codec == 2 && b.u.begin.chunk_samples == 474 &&
        b.u.begin.max_chunks == kVoiceClipChunks);
  CHECK(h.air.count(kSenderUpstairs, kSenderGateway, VType::Data) <= kVoiceClipChunks + 10);
  h.up->set_voice_codec((VoiceCodec)7);  // anything else is ADPCM
  CHECK(h.up->voice_codec() == VoiceCodec::Adpcm);
  h.up->set_voice_codec(VoiceCodec::Mulaw);
  h.up->voice_report(rep, sizeof rep);
  CHECK(strstr(rep, "codec 1 mu-law") != nullptr);
}

// The badge row's live meter before START: View::voice.level follows Platform::mic_meter a few times a
// second while voice_meter() keeps coming, goes to 0 a second after it stops, starts no capture, and
// gives way to the recording's own level while capturing.
VTEST(house_badge_meter_before_start) {
  House h;
  h.settle();
  h.pu.meter_ok = true;
  h.pu.meter_peak = -30.0f;
  CHECK(h.up->view().voice.level == 0);
  int reads0 = h.pu.meter_reads, allocs0 = h.pu.allocs;
  for (int i = 0; i < 20; i++) {  // one second of the row open, a render every 50 ms
    h.up->voice_meter();
    h.run(50);
  }
  CHECK(h.up->view().voice.level == voice_meter_level(-30.0f));
  int reads = h.pu.meter_reads - reads0;
  CHECK(reads >= 4 && reads <= 6);  // every 200 ms
  CHECK(h.pu.mic.rec == nullptr && h.pu.allocs == allocs0);  // nothing recorded, no clip
  h.pu.meter_peak = -20.0f;
  for (int i = 0; i < 5; i++) {
    h.up->voice_meter();
    h.run(50);
  }
  CHECK(h.up->view().voice.level == voice_meter_level(-20.0f));
  // START while the meter runs: the recording's level (FakeMic says 40) replaces it.
  CHECK(h.up->voice_press(VoicePurpose::Message));
  h.up->voice_meter();
  h.run(300);
  CHECK(h.up->view().voice.level == 40);
  h.up->voice_cancel();
  h.run(200);
  // The row closed: the calls stop and the meter goes quiet within a second.
  h.run(1100);
  CHECK(h.up->view().voice.level == 0);
  int after = h.pu.meter_reads;
  h.run(1000);
  CHECK(h.pu.meter_reads == after);  // no more reads once nobody asks
  // No microphone: level 0.
  h.pu.meter_ok = false;
  for (int i = 0; i < 6; i++) {
    h.up->voice_meter();
    h.run(50);
  }
  CHECK(h.up->view().voice.level == 0);
}

// gateway-c6 0.6: a 3 s mu-law message (about 203 chunks) from each floor, direct and relayed, and from both
// at once, through piece D's real gateway with the panels' air caps on, while the Pi's OUTSIDE and a LUCY
// reply arrive every 700 ms: every chunk is written to the Pi once, every OUTSIDE and every LUCY gets its
// "acked" line whole, nothing failed, and the C6's own air cap (kVoiceGwPerS) throttles nothing.
namespace {

struct Pi06 {
  int outside = 0, lucy = 0;
  uint32_t next = 0;
};

void pi_traffic(House &h, Pi06 &p) {
  if (p.next == 0) p.next = g_now + 300;
  if (ms_diff(g_now, p.next) < 0) return;
  if ((p.outside + p.lucy) % 2 == 0) {
    h.real->outside();
    p.outside++;
  } else {
    char l[120];
    snprintf(l, sizeof l, "{\"type\":\"lucy\",\"id\":%d,\"out\":%d,\"since\":%d}", 9000 + p.lucy, p.lucy % 2, 1793000000 + p.lucy);
    h.real->say(l);
    p.lucy++;
  }
  p.next += 350;
}

void check_replies(const House &h, const Pi06 &p) {
  int out_ok = 0, lucy_ok = 0, bad = 0;
  size_t longest = 0;
  for (const std::string &l : h.real->lines) {
    if (l.size() > longest) longest = l.size();
    if (l.empty() || l[0] != '{' || l[l.size() - 1] != '}') bad++;
    if (l.find("\"type\":\"outside\"") != std::string::npos && l.find("\"acked\":\"upstairs\"") != std::string::npos) out_ok++;
    if (l.find("\"type\":\"lucy\"") != std::string::npos && l.find("\"acked\":\"upstairs\"") != std::string::npos) lucy_ok++;
  }
  CHECK(bad == 0 && longest < 512);
  CHECK(p.outside >= 3 && p.lucy >= 3);
  CHECK(out_ok == p.outside && lucy_ok == p.lucy);
  CHECK(h.real->count("\"failed\"") == 0 && h.real->count("\"rejected\"") == 0);
  CHECK(h.real->g.status().voice_capped == 0);
}

}  // namespace

VTEST(gw06_mulaw_message_with_outside_and_lucy) {
  for (int route = 0; route < 4; route++) {  // 0 upstairs, 1 downstairs direct, 2 downstairs relayed, 3 both at once
    House h(true);
    h.pu.cap_on();
    h.pd.cap_on();
    h.settle();
    if (route == 2) h.down->set_voice_route(Controller::VoiceRoute::Relay);
    const bool up = route == 0 || route == 3, down = route >= 1;
    if (up) CHECK(h.up->voice_press(VoicePurpose::Message));
    if (down) CHECK(h.down->voice_press(VoicePurpose::Message));
    Pi06 p;
    const uint32_t t0 = g_now;
    auto finished = [&](Controller &c) { return c.view().voice.ui == VoiceUi::Result || c.view().voice.ui == VoiceUi::Error; };
    while (ms_diff(g_now, t0) < 20000 && !((!up || finished(*h.up)) && (!down || finished(*h.down)))) {
      h.step();
      pi_traffic(h, p);
    }
    h.run(1500);  // the last replies
    if (up) CHECK(h.up->view().voice.ui == VoiceUi::Result && !strcmp(h.up->view().voice.text, "Dinner is ready"));
    if (down) CHECK(h.down->view().voice.ui == VoiceUi::Result && !strcmp(h.down->view().voice.text, "Dinner is ready"));
    int lines = 0, top_up = -1, top_down = -1;
    if (up) top_up = max_voice_idx(*h.real, "\"f\":\"up\"", lines);
    const int up_lines = lines;
    lines = 0;
    if (down) top_down = max_voice_idx(*h.real, "\"f\":\"down\"", lines);
    if (up) CHECK(top_up >= 201 && top_up < kVoiceClipChunksMulaw && up_lines == top_up + 1);
    if (down) CHECK(top_down >= 201 && top_down < kVoiceClipChunksMulaw && lines == top_down + 1);
    CHECK(h.real->voice_ends == (up ? 1 : 0) + (down ? 1 : 0));
    CHECK(h.real->window_violations == 0);
    CHECK(h.pu.cap_refused == 0 && h.pd.cap_refused == 0);
    check_replies(h, p);
    printf("  gw 0.6 route %d: %d up + %d down chunks, OUTSIDE %d and LUCY %d answered, serial held %u, C6 capped %u\n",
           route, up ? up_lines : 0, down ? lines : 0, p.outside, p.lucy, (unsigned)h.real->g.status().voice_held,
           (unsigned)h.real->g.status().voice_capped);
  }
}

// The same through 10 % loss, 5 % duplicates and reordering, with the OUTSIDE and LUCY traffic: every run
// completes, and no reply to the Pi is lost or cut.
VTEST(gw06_mulaw_lossy_with_outside_and_lucy) {
  int ok = 0, runs = 0;
  for (uint32_t seed = 1; seed <= 6; seed++)
    for (int route = 0; route < 3; route++) {
      House h(true);
      h.pu.cap_on();
      h.pd.cap_on();
      h.settle();
      h.air.rng = Rng(seed * 3266489917u + (uint32_t)route * 7u);
      h.air.drop = route == 2 ? 8 : 10;
      h.air.dup = 5;
      h.air.jitter = 25;
      if (route == 2) h.down->set_voice_route(Controller::VoiceRoute::Relay);
      Controller &c = route ? *h.down : *h.up;
      CHECK(c.voice_press(VoicePurpose::Message));
      Pi06 p;
      const uint32_t t0 = g_now;
      while (ms_diff(g_now, t0) < 20000 && c.view().voice.ui != VoiceUi::Result && c.view().voice.ui != VoiceUi::Error) {
        h.step();
        pi_traffic(h, p);
      }
      runs++;
      ok += c.view().voice.ui == VoiceUi::Result ? 1 : 0;
      // Settle with no new traffic, so a request in flight on the lossy air has its retries.
      h.run(3000);
      int bad = 0;
      for (const std::string &l : h.real->lines)
        if (l.empty() || l[0] != '{' || l[l.size() - 1] != '}' || l.size() >= 512) bad++;
      CHECK(bad == 0);
      int out_done = 0, lucy_done = 0;
      for (const std::string &l : h.real->lines) {
        const bool final = l.find("\"acked\"") != std::string::npos || l.find("\"failed\"") != std::string::npos ||
                           l.find("\"superseded\"") != std::string::npos;
        if (l.find("\"type\":\"outside\"") != std::string::npos && final) out_done++;
        if (l.find("\"type\":\"lucy\"") != std::string::npos && final) lucy_done++;
      }
      CHECK(out_done == p.outside && lucy_done == p.lucy);  // each request got exactly one final reply
    }
  CHECK(ok == runs);
  printf("  gw 0.6 lossy with OUTSIDE and LUCY: %d of %d runs\n", ok, runs);
}

// ---------------------------------------------------------------------------------------------
// The controller and the rest of piece D's gateway, compiled into this test (see the top of the file).
#include "app/controller.cpp"
// gateway.cpp's joined-text snprintf trips -Wformat-truncation at -O1 (a warning, not a bug: t0 and t1
// are each checked to 60 characters first); kept quiet here so this file builds with -Werror.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include "gw/gateway.cpp"
#include "gw/json_line.cpp"
#pragma GCC diagnostic pop
