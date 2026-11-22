// Mail for downstairs (Mike, 2026-10-26). Mike brings in the mail and taps the outline on the
// upstairs panel's DOWNSTAIRS tile; a mail badge then shows on both panels. The tenant taps theirs
// to say they've seen it, which clears it on both. Mike can tap his again to take it back.
//
// Kept like a dog's state: one flag with a change counter, and the higher counter wins. A tie (both
// panels changed it at once) goes to the later time, then to "waiting": better an extra badge than
// a lost one.
//
// "No mail" (Mike, 2026-11-18): the same badge row also carries the opposite news, "I went to get the
// mail and there was none". It rides in this same state, so it syncs, saves and wins by the same
// counter. Mail waiting and no-mail never show together: marking one clears the other. The tenant's
// tap (or a phone's "Got it") clears whichever is showing.
#pragma once
#include "types.h"

namespace tc {

struct MailState {
  bool waiting = false;  // mail for downstairs, not yet acknowledged
  bool none = false;     // Mike, 2026-11-18: he checked and there is no mail; not yet acknowledged (never with waiting)
  uint32_t since = 0;    // UTC of its last change; 0 = unknown
  uint32_t ver = 0;      // its change counter
};
inline bool operator==(const MailState &a, const MailState &b) {
  return a.waiting == b.waiting && a.none == b.none && a.since == b.since && a.ver == b.ver;
}

class MailModel {
 public:
  void restore(const MailState &s) { s_ = s; }
  const MailState &state() const { return s_; }

  // Upstairs: marks mail waiting, or takes it back. Marking clears a no-mail mark.
  void toggle(const WallTime &now);
  // Upstairs: marks "no mail", or takes it back. Marking clears mail waiting.
  void toggle_none(const WallTime &now);
  // Downstairs: the tenant has seen it, whichever is showing. False if nothing was.
  bool acknowledge(const WallTime &now);
  // Only the mail badge's own tap (or only the no-mail badge's): false when that one wasn't showing.
  bool acknowledge_mail(const WallTime &now);
  bool acknowledge_none(const WallTime &now);
  // A MAIL message from the other panel. Returns true if anything changed here.
  bool apply_remote(const MailState &in);

 private:
  void set(bool waiting, bool none, const WallTime &now);
  MailState s_;
};

}  // namespace tc
