// Mail for downstairs (Mike, 2026-10-26). Mike brings in the mail and taps the outline on the
// upstairs panel's DOWNSTAIRS tile; a mail badge then shows on both panels. The tenant taps theirs
// to say they've seen it, which clears it on both. Mike can tap his again to take it back.
//
// Kept like a dog's state: one flag with a change counter, and the higher counter wins. A tie (both
// panels changed it at once) goes to the later time, then to "waiting": better an extra badge than
// a lost one.
#pragma once
#include "types.h"

namespace tc {

struct MailState {
  bool waiting = false;  // mail for downstairs, not yet acknowledged
  uint32_t since = 0;    // UTC of its last change; 0 = unknown
  uint32_t ver = 0;      // its change counter
};
inline bool operator==(const MailState &a, const MailState &b) {
  return a.waiting == b.waiting && a.since == b.since && a.ver == b.ver;
}

class MailModel {
 public:
  void restore(const MailState &s) { s_ = s; }
  const MailState &state() const { return s_; }

  // Upstairs: marks mail waiting, or takes it back.
  void toggle(const WallTime &now);
  // Downstairs: the tenant has seen it. False if nothing was waiting.
  bool acknowledge(const WallTime &now);
  // A MAIL message from the other panel. Returns true if anything changed here.
  bool apply_remote(const MailState &in);

 private:
  void set(bool waiting, const WallTime &now);
  MailState s_;
};

}  // namespace tc
