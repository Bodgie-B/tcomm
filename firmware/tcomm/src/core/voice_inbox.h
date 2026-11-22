// Voice notes: the arrival inbox on the receiving panel, and the ding / wake / phone rules for a new
// message (docs/voice-notes/DESIGN.md 2.3, 7.4; piece B). Plain C++, host-tested.
//
// Up to 4 messages, oldest first; a 5th drops the oldest. Dedup by message id: the last 8 ids shown
// are kept with the inbox (VoiceInboxData.seen), so a retried SAY never shows twice, even after a
// restart. The whole VoiceInboxData is what Platform::save_voice_inbox saves.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "voice_proto.h"

namespace tc {

enum class VoiceAdd : uint8_t { Added = 0, Duplicate = 1, Bad = 2 };

class VoiceInbox {
 public:
  VoiceInbox() { clear(); }
  // A SAY's message. dropped (optional): set when the oldest had to go to make room.
  VoiceAdd add(uint32_t id, uint32_t time, uint8_t from, const char *text, size_t len, bool *dropped = nullptr);
  bool remove(uint32_t id);  // false when it isn't there
  bool pop_top();            // the shown (oldest) message leaves
  const VoiceNote *top() const { return d_.n ? &d_.msg[0] : nullptr; }
  int count() const { return d_.n; }
  bool seen(uint32_t id) const;
  // The saved inbox at start-up; false (and left empty) when voice_inbox_valid refuses it.
  bool restore(const VoiceInboxData &d);
  const VoiceInboxData &data() const { return d_; }
  void clear();

 private:
  void remember(uint32_t id);
  VoiceInboxData d_;
};

// What a new message does on the receiving panel (DESIGN 2.3 table, P0 notes 6).
struct VoiceArrival {
  bool ding;   // play Sound::Ding now
  bool defer;  // this panel is recording: the ding plays when the recording ends
  bool wake;   // voice_wake for touch_wake_ms, and the screensaver stops (with the ding)
  bool push;   // send VOICE_PUSH to the C6 for this floor's phones
};
// own: this panel's status. clock_valid / quiet / screen_off: from this panel's clock and Settings
// (quiet and screen_off false without a clock). recording: this panel has a capture running.
// banner: a doorbell banner is up (no extra ding).
//  - Ding and wake only when Open or Busy, outside quiet hours and outside the screen's dark hours.
//    Without a clock the ding still plays (at the capped volume, like the mail chime).
//  - The phone push only when Open or Busy, the clock is valid and it is outside quiet hours (the
//    doorbell's rule, kVoicePushOpenBusyOnly); whatever the recording or the banner.
VoiceArrival voice_arrival(Status own, bool clock_valid, bool quiet, bool screen_off, bool recording, bool banner);

}  // namespace tc
