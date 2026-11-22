#include "voice_session.h"

namespace gw {

size_t voice_base64(const uint8_t *in, size_t n, char *out) {
  static const char kTab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  size_t i = 0;
  for (; i + 3 <= n; i += 3) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
    out[o++] = kTab[v >> 18 & 63];
    out[o++] = kTab[v >> 12 & 63];
    out[o++] = kTab[v >> 6 & 63];
    out[o++] = kTab[v & 63];
  }
  if (i < n) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
    out[o++] = kTab[v >> 18 & 63];
    out[o++] = kTab[v >> 12 & 63];
    out[o++] = i + 1 < n ? kTab[v >> 6 & 63] : '=';
    out[o++] = '=';
  }
  out[o] = 0;
  return o;
}

void VoiceSession::open(uint8_t x, uint8_t pu, uint8_t co, uint16_t cs, uint8_t mc, uint8_t v, uint32_t now) {
  clear();
  state = Open;
  xfer = x;
  purpose = pu;
  codec = co;
  chunk_samples = cs;
  max_chunks = mc;
  via = v;
  last_rx = now;
}

void VoiceSession::clear() {
  state = Idle;
  begin_out = false;
  pi_abort = 0;
  abort_out = false;
  end_seen = false;
  n_chunks = 0;
  n_samples = 0;
  speech = 0;
  new_since_ack = 0;
  ack_due = false;
  dup = false;
  ack_at = 0;
  ack_first = 0;
  max_seen = -1;
  for (int i = 0; i < kWords; i++) w_[i] = 0;
}

bool VoiceSession::mark(int idx) {
  if (idx < 0 || idx >= max_chunks || has(idx)) return false;
  w_[idx >> 5] |= 1u << (idx & 31);
  return true;
}

uint8_t VoiceSession::base() const {
  int i = 0;
  while (i < max_chunks && has(i)) i++;
  return (uint8_t)i;
}

uint32_t VoiceSession::bits() const {
  int b = base();
  uint32_t v = 0;
  for (int k = 0; k < tc::kVoiceWindow; k++)
    if (b + k < max_chunks && has(b + k)) v |= 1u << k;
  return v;
}

bool VoiceSession::gap() const {
  int b = base();
  if (end_seen) return b < n_chunks;
  return b <= max_seen;
}

bool VoiceSession::all_received() const {
  if (!end_seen) return false;
  for (int i = 0; i < n_chunks; i++)
    if (!has(i)) return false;
  return true;
}

int VoiceSession::received() const {
  int n = 0;
  for (int i = 0; i < max_chunks; i++) n += has(i) ? 1 : 0;
  return n;
}

}  // namespace gw
