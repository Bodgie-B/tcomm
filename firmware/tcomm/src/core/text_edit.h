// Tap to place the cursor (docs/voice-notes/DESIGN.md section 8, piece H): the edit and hit-test
// maths behind the text fields of the tile message editor, the calendar's event editor and the voice
// recorder's EDIT field. Plain C++ with no LVGL: the caller passes a WidthFn that measures text in
// its own font, so the host tests can use a fixed-width fake.
#pragma once
#include <stddef.h>

namespace tc {

// A field's text and its cursor. buf holds at most cap characters plus the terminating 0; cursor is
// a character boundary, 0..strlen(buf).
struct TextEdit {
  char *buf;
  size_t cap;
  int cursor;
};

bool te_insert(TextEdit &t, char c);             // at the cursor; false if full; the cursor moves on
bool te_insert_str(TextEdit &t, const char *s);  // stops at the first refused character
void te_backspace(TextEdit &t);                  // the character before the cursor
void te_delete(TextEdit &t);                     // the character after the cursor
void te_set(TextEdit &t, int i);                 // clamped 0..len
int te_len(const TextEdit &t);

// The width in pixels of the first n characters of s.
using WidthFn = int (*)(const char *s, int n, void *ctx);

// The character boundary in [a, b] nearest to x, where x is measured from the left of s[a]: each
// glyph is split at its midpoint (a tap on its left half goes before it, on its right half after
// it; exactly on the midpoint goes after). x < 0 gives a, past the end gives b.
int te_hit(const char *s, int a, int b, int x, WidthFn w, void *ctx);

// A one-line view of s that keeps the cursor in sight (the calendar fields). *first is the first
// character shown; pass the last view's first so the view only moves when it must. Returns the end
// of the view (exclusive): s[*first, end) fits width and *first <= cursor <= end. Text that fits
// whole shows whole; after a deletion the view slides back so no room is wasted at the end.
int te_window(const char *s, int cursor, int width, WidthFn w, void *ctx, int *first);

// Word wrap as the tile shows a message (the note editor's note_wrap): up to max lines; line i is
// s[start[i], end[i]) without its trailing spaces, breaking at the last space that fits, mid-word
// when a word alone is wider than the line. Trailing spaces never count toward the width.
int te_wrap(const char *s, int width, WidthFn w, void *ctx, int *start, int *end, int max);

// The wrapped line that holds the cursor: the last line starting at or before it, so a cursor
// exactly at a wrap break belongs to the next line. n == 0 gives 0.
int te_line_of(const int *start, int n, int cursor);

// A capital is due at the cursor: it is at 0, only spaces are before it, or the last non-space
// before it is '.', '?' or '!' with a space after it (the title's capital-first rule).
bool te_sentence_start(const char *s, int cursor);

// The voice strip (ui_voice.cpp) gets only the field's text from the keyboard's owner; the owners
// register their TextEdit here so the strip can find the cursor that goes with that text. Up to
// kTeSlots (4 are used: the note, the calendar's title and notes, EDIT); registering twice is
// harmless. te_find returns null for an unregistered buffer (the strip then inserts at the end).
constexpr int kTeSlots = 6;
void te_register(TextEdit *t);
void te_unregister(TextEdit *t);
TextEdit *te_find(const char *buf);

}  // namespace tc
