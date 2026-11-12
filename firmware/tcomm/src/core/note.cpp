// A floor's tile message (Mike, 2026-11-07): see note.h.
#include "note.h"
#include <string.h>

namespace tc {

const uint32_t kNoteDurationS[kNoteDurations] = {15 * 60, 30 * 60, 3600, 2 * 3600, 4 * 3600};

bool operator==(const NoteState &a, const NoteState &b) {
  return a.ver == b.ver && a.set == b.set && a.until == b.until && a.since == b.since &&
         strncmp(a.text, b.text, kNoteTextMax) == 0;
}

static size_t text_len(const NoteState &s) { return strnlen(s.text, kNoteTextMax); }

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

void NoteModel::bump(const WallTime &now) {
  s_.ver++;  // one past the highest seen: apply_remote keeps the higher counter
  s_.since = now.valid ? now.utc : 0;
}

void NoteModel::set(const char *text, uint32_t duration_s, const WallTime &now) {
  size_t len = text ? strnlen(text, kNoteTextMax + 8) : 0;
  size_t start = 0;
  while (start < len && is_space(text[start])) start++;
  size_t end = len;
  if (end - start > (size_t)kNoteTextMax) {
    end = start + kNoteTextMax;
    // Never cut a UTF-8 character: if the first dropped byte is a continuation byte, the cut is
    // inside a character, so back up to that character's lead byte and drop the whole character.
    while (end > start && ((uint8_t)text[end] & 0xC0) == 0x80) end--;
  }
  while (end > start && is_space(text[end - 1])) end--;
  if (end == start) {
    clear(now);
    return;
  }
  if (duration_s == 0) duration_s = kNoteMaxS;
  if (duration_s > kNoteMaxS) duration_s = kNoteMaxS;
  memset(s_.text, 0, sizeof(s_.text));
  memcpy(s_.text, text + start, end - start);
  s_.set = now.valid ? now.utc : 0;
  s_.until = now.valid ? now.utc + duration_s : 0;
  bump(now);
}

bool NoteModel::clear(const WallTime &now) {
  if (s_.text[0] == 0) return false;
  bool was_showing = showing(now.valid ? now.utc : 0);
  memset(s_.text, 0, sizeof(s_.text));
  s_.set = 0;
  s_.until = 0;
  if (!was_showing) return false;  // an expired copy is dropped quietly; the other panel hides it too
  bump(now);
  return true;
}

static bool newer(const NoteState &in, const NoteState &cur) {
  if (in.ver != cur.ver) return in.ver > cur.ver;
  if (in.since != cur.since) return in.since > cur.since;
  size_t li = text_len(in), lc = text_len(cur);
  if (li != lc) return li > lc;
  return memcmp(in.text, cur.text, li) > 0;  // a true tie: both panels keep the same one
}

bool NoteModel::apply_remote(const NoteState &in) {
  NoteState c = in;
  c.text[kNoteTextMax] = 0;  // the text always ends, whatever arrived
  if (c == s_ || !newer(c, s_)) return false;
  s_ = c;
  return true;
}

bool NoteModel::showing(uint32_t utc) const {
  if (s_.text[0] == 0) return false;
  return utc == 0 || s_.until == 0 || utc < s_.until;
}

}  // namespace tc
