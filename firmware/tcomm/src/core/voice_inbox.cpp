// Voice notes: the arrival inbox and the arrival rules. See voice_inbox.h.
#include "voice_inbox.h"
#include <string.h>

namespace tc {

void VoiceInbox::clear() {
  memset((void *)&d_, 0, sizeof(d_));  // zeroed first, so a saved blob's padding is always the same bytes
  d_.version = VoiceInboxData::kVersion;
}

bool VoiceInbox::seen(uint32_t id) const {
  if (!id) return false;
  for (int i = 0; i < kVoiceSeenIds; i++)
    if (d_.seen[i] == id) return true;
  for (int i = 0; i < d_.n; i++)
    if (d_.msg[i].id == id) return true;
  return false;
}

void VoiceInbox::remember(uint32_t id) {
  d_.seen[d_.seen_next] = id;
  d_.seen_next = (uint8_t)((d_.seen_next + 1) % kVoiceSeenIds);
}

VoiceAdd VoiceInbox::add(uint32_t id, uint32_t time, uint8_t from, const char *text, size_t len, bool *dropped) {
  if (dropped) *dropped = false;
  if (!id || from > 1 || !text || len == 0 || len > kVoiceTextMax) return VoiceAdd::Bad;
  for (size_t i = 0; i < len; i++)
    if ((uint8_t)text[i] < 0x20 || (uint8_t)text[i] > 0x7E) return VoiceAdd::Bad;
  if (seen(id)) return VoiceAdd::Duplicate;
  if (d_.n >= kVoiceInboxMax) {
    memmove(&d_.msg[0], &d_.msg[1], sizeof(VoiceNote) * (kVoiceInboxMax - 1));
    d_.n = kVoiceInboxMax - 1;
    if (dropped) *dropped = true;
  }
  VoiceNote &v = d_.msg[d_.n++];
  memset(&v, 0, sizeof(v));
  v.id = id;
  v.time = time;
  v.from = from;
  v.len = (uint8_t)len;
  memcpy(v.text, text, len);
  remember(id);
  return VoiceAdd::Added;
}

bool VoiceInbox::remove(uint32_t id) {
  if (!id) return false;
  for (int i = 0; i < d_.n; i++) {
    if (d_.msg[i].id != id) continue;
    for (int j = i + 1; j < d_.n; j++) d_.msg[j - 1] = d_.msg[j];
    d_.n--;
    memset(&d_.msg[d_.n], 0, sizeof(VoiceNote));
    return true;
  }
  return false;
}

bool VoiceInbox::pop_top() { return d_.n ? remove(d_.msg[0].id) : false; }

bool VoiceInbox::restore(const VoiceInboxData &d) {
  if (!voice_inbox_valid(d)) {
    clear();
    return false;
  }
  clear();
  d_.n = d.n;
  for (int i = 0; i < d.n; i++) d_.msg[i] = d.msg[i];
  for (int i = 0; i < kVoiceSeenIds; i++) d_.seen[i] = d.seen[i];
  d_.seen_next = d.seen_next;
  return true;
}

VoiceArrival voice_arrival(Status own, bool clock_valid, bool quiet, bool screen_off, bool recording, bool banner) {
  VoiceArrival a = {};
  const bool open_busy = own == Status::Open || own == Status::Busy;
  if (!clock_valid) quiet = screen_off = false;  // no clock: neither is known
  const bool sound_ok = open_busy && !quiet && !screen_off;
  a.wake = sound_ok;
  a.ding = sound_ok && !banner && !recording;
  a.defer = sound_ok && !banner && recording;
  a.push = kVoicePushOpenBusyOnly ? (open_busy && clock_valid && !quiet) : true;
  return a;
}

}  // namespace tc
