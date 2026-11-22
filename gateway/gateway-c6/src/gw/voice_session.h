// One voice upload as the C6 sees it (firmware/docs/voice-notes/DESIGN.md, 5.1): one session per floor.
// The state of the transfer only: which chunks have been written to the Pi, the END that was seen,
// and the timers. The lines and the radio frames are the Gateway's. Plain C++ (no Arduino), so the
// host tests and piece B's lossy-link test drive it directly.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "core/voice_proto.h"

namespace gw {

// How the session's frames come and go (the "via" word on the serial line, tc::vline::kVia).
enum Via : uint8_t { kViaDirect = 0, kViaUpstairs = 1 };

// Base64 (standard alphabet, with padding) of n bytes into out; returns the characters written.
// out must hold 4 * ((n + 2) / 3) + 1 bytes (a NUL is added).
size_t voice_base64(const uint8_t *in, size_t n, char *out);

struct VoiceSession {
  enum State : uint8_t { Idle = 0, Open = 1, Done = 2 };  // Done keeps xfer: a resent END still gets state 2
  State state = Idle;
  uint8_t xfer = 0;
  uint8_t purpose = 0, codec = 0;
  uint16_t chunk_samples = 0;
  uint8_t max_chunks = 0;
  uint8_t via = kViaDirect;  // how the last frame of this session came; kept after Done and after a clear
  uint32_t last_rx = 0;      // when the last frame of this session came (any type, held chunks included)
  // What the Pi has been told.
  bool begin_out = false;    // the voice_begin line was written
  uint8_t pi_abort = 0;      // a voice_abort line is owed to the Pi (a tc::VoiceAbort reason), 0 = none
  bool abort_out = false;    // Done: the Pi was told of the abort (it is told once)
  // The END that was seen.
  bool end_seen = false;
  uint8_t n_chunks = 0;
  uint16_t n_samples = 0;
  uint8_t speech = 0;
  // The ACK schedule (DESIGN 4.4): after BEGIN, after every 8 new chunks, 80 ms after the last DATA
  // when a gap exists, and on END.
  uint8_t new_since_ack = 0;
  bool ack_due = false;
  bool dup = false;          // a chunk came that was already written, or filled a gap: the panel did not see the bitmap
  uint32_t ack_at = 0;
  uint32_t ack_first = 0;    // when the first DATA since the last ACK came (caps how far ack_at can be pushed)
  int max_seen = -1;         // highest idx seen in a DATA frame, held back or not

  // Starts a session (any earlier one is gone). via and now as of the BEGIN.
  void open(uint8_t xfer, uint8_t purpose, uint8_t codec, uint16_t chunk_samples, uint8_t max_chunks, uint8_t via,
            uint32_t now);
  void clear();  // back to Idle; via is kept (the text and dismiss frames still go that way)
  bool has(int idx) const { return idx >= 0 && idx < tc::kVoiceMaxChunks && (w_[idx >> 5] >> (idx & 31) & 1u); }
  // Marks idx received (written to the Pi). False for idx outside max_chunks or already marked.
  bool mark(int idx);
  uint8_t base() const;   // the lowest idx not received (max_chunks when all are)
  uint32_t bits() const;  // bit k: idx base + k received (bit 0 is always 0 unless base is max_chunks)
  bool gap() const;       // a chunk below the highest one seen (or below END's count) is missing
  // END seen and every chunk 0..n_chunks-1 received.
  bool all_received() const;
  int received() const;

 private:
  // One bit per idx, sized from the shared constant (field fix 2026-11-21: kVoiceMaxChunks 128 -> 255
  // for mu-law; four words would have let has() and mark() run past the end).
  static constexpr int kWords = (tc::kVoiceMaxChunks + 31) / 32;
  uint32_t w_[kWords] = {};
};

}  // namespace gw
