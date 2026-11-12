// A short message on a floor's tile (Mike, 2026-11-07): mild coordination between the floors, such
// as "need to wash my clothes today before 3PM". Each panel writes its own floor's message from its
// own tile (a tap on the line under the status word opens a keyboard); it shows in place of that
// line, in up to two lines, on that floor's tile on BOTH panels. One tap on the line by the floor
// that wrote it takes it away at once. It goes on its own when its time is up (the writer picks how
// long, at most 4 hours). Upstairs, Mike clearing the heat and cooling arrows ("Action taken on
// temperature") takes his message away too: the confirmation takes that spot.
//
// Kept like the mail badge: one state per floor with a change counter, the higher counter wins, a
// tie goes to the later change, then to the longer text, then to the larger bytes. Only the floor
// that owns a message changes it; the other panel keeps a copy. Ages are worked out from the
// times, not stored as changes, so nothing is sent when a message times out.
//
// Plain data with no default member initializers: NoteState rides in the protocol's Msg union.
#pragma once
#include "types.h"

namespace tc {

// Bytes of text (no line break stored: the screen wraps it into at most two lines; the keyboard
// stops accepting characters once two lines of the tile's line font are full).
constexpr int kNoteTextMax = 64;
constexpr uint32_t kNoteMaxS = 4 * 3600;  // Mike: a message goes after 4 hours on its own
// The choices the editor offers (seconds), shortest first; the last is the default.
constexpr int kNoteDurations = 5;
extern const uint32_t kNoteDurationS[kNoteDurations];  // 15 min, 30 min, 1 hr, 2 hr, 4 hr

struct NoteState {
  char text[kNoteTextMax + 1];  // empty: no message
  uint32_t set;                 // UTC it was written; 0 = no clock then
  uint32_t until;               // UTC it goes; 0 = no clock (it then stays until tapped away)
  uint32_t since;               // UTC of the last change; 0 = unknown
  uint32_t ver;                 // the change counter
};
bool operator==(const NoteState &a, const NoteState &b);

class NoteModel {
 public:
  void restore(const NoteState &s) { s_ = s; }
  const NoteState &state() const { return s_; }

  // The owner writes a message for duration_s (capped at kNoteMaxS). Text is copied up to
  // kNoteTextMax bytes; leading and trailing spaces are dropped; an empty text clears.
  void set(const char *text, uint32_t duration_s, const WallTime &now);
  // The owner takes it away (a tap, or Mike's "Action taken on temperature"). False if none showed.
  bool clear(const WallTime &now);
  // A NOTE message from the other panel about this floor. True if anything changed here.
  bool apply_remote(const NoteState &in);
  // Whether it shows at utc (utc 0: no clock, so nothing ages out).
  bool showing(uint32_t utc) const;

 private:
  void bump(const WallTime &now);
  NoteState s_ = {};
};

}  // namespace tc
