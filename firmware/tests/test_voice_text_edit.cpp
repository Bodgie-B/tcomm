// Voice notes piece H: tap to place the cursor (src/core/text_edit.{h,cpp}). Inserts and backspace
// at the start, middle, end and when full; the midpoint hit rule with a fake font; the calendar's
// one-line window keeping the cursor in view both ways; the note's word wrap with the cursor on each
// line and at a break; the title's capital rule; the registry the voice strip uses. Synthetic text.
#include "voice_test.h"
#include <string.h>
#include <string>
#include "core/text_edit.h"
#include "core/voice_proto.h"

using namespace tc;

namespace {

// Every character 10 px wide.
int fixed_w(const char *, int n, void *) { return n * 10; }

// A proportional fake: 'i', 'l' and space 4 px, 'W' and 'M' 16 px, the rest 10 px.
int glyph_w(char c) {
  if (c == 'i' || c == 'l' || c == ' ') return 4;
  if (c == 'W' || c == 'M') return 16;
  return 10;
}
int prop_w(const char *s, int n, void *ctx) {
  int *calls = (int *)ctx;
  if (calls) (*calls)++;
  int w = 0;
  for (int i = 0; i < n && s[i]; i++) w += glyph_w(s[i]);
  return w;
}

struct Buf {
  char b[32];
  TextEdit t;
  explicit Buf(const char *s, size_t cap = 10) {
    memset(b, 0, sizeof(b));
    strncpy(b, s, sizeof(b) - 1);
    t = TextEdit{b, cap, (int)strlen(b)};
  }
};

}  // namespace

VTEST(te_insert_start_middle_end) {
  Buf x("bd");
  te_set(x.t, 0);
  CHECK(te_insert(x.t, 'a'));
  CHECK(strcmp(x.b, "abd") == 0 && x.t.cursor == 1);
  te_set(x.t, 2);
  CHECK(te_insert(x.t, 'c'));
  CHECK(strcmp(x.b, "abcd") == 0 && x.t.cursor == 3);
  te_set(x.t, 4);
  CHECK(te_insert(x.t, 'e'));
  CHECK(strcmp(x.b, "abcde") == 0 && x.t.cursor == 5);
  CHECK(!te_insert(x.t, 0));  // never a terminator
  CHECK(strcmp(x.b, "abcde") == 0);
}

VTEST(te_insert_full) {
  Buf x("abcdefghi", 10);
  te_set(x.t, 3);
  CHECK(te_insert(x.t, 'X'));  // the tenth character
  CHECK(strcmp(x.b, "abcXdefghi") == 0 && x.t.cursor == 4);
  CHECK(!te_insert(x.t, 'Y'));  // full: refused, nothing moves
  CHECK(strcmp(x.b, "abcXdefghi") == 0 && x.t.cursor == 4);
  CHECK(x.b[10] == 0);
}

VTEST(te_insert_str_stops_at_refusal) {
  Buf x("ab", 5);
  te_set(x.t, 1);
  CHECK(!te_insert_str(x.t, "1234"));
  CHECK(strcmp(x.b, "a123b") == 0 && x.t.cursor == 4);
  Buf y("", 5);
  CHECK(te_insert_str(y.t, "hey"));
  CHECK(strcmp(y.b, "hey") == 0 && y.t.cursor == 3);
  CHECK(te_insert_str(y.t, nullptr));
}

VTEST(te_backspace_and_delete) {
  Buf x("abcd");
  te_set(x.t, 0);
  te_backspace(x.t);  // nothing before the start
  CHECK(strcmp(x.b, "abcd") == 0 && x.t.cursor == 0);
  te_set(x.t, 2);
  te_backspace(x.t);
  CHECK(strcmp(x.b, "acd") == 0 && x.t.cursor == 1);
  te_set(x.t, 3);
  te_backspace(x.t);
  CHECK(strcmp(x.b, "ac") == 0 && x.t.cursor == 2);
  te_set(x.t, 0);
  te_delete(x.t);
  CHECK(strcmp(x.b, "c") == 0 && x.t.cursor == 0);
  te_set(x.t, 1);
  te_delete(x.t);  // nothing after the end
  CHECK(strcmp(x.b, "c") == 0);
  te_backspace(x.t);
  te_backspace(x.t);
  CHECK(x.b[0] == 0 && x.t.cursor == 0);
}

VTEST(te_set_clamps) {
  Buf x("abc");
  te_set(x.t, -5);
  CHECK(x.t.cursor == 0);
  te_set(x.t, 99);
  CHECK(x.t.cursor == 3);
  x.t.cursor = 50;  // stale (the text got shorter behind its back): the edits clamp first
  CHECK(te_insert(x.t, 'd'));
  CHECK(strcmp(x.b, "abcd") == 0 && x.t.cursor == 4);
  x.t.cursor = 50;
  te_backspace(x.t);
  CHECK(strcmp(x.b, "abc") == 0 && x.t.cursor == 3);
}

VTEST(te_hit_midpoint_fixed) {
  const char *s = "hello";
  CHECK(te_hit(s, 0, 5, -20, fixed_w, nullptr) == 0);
  CHECK(te_hit(s, 0, 5, 0, fixed_w, nullptr) == 0);
  CHECK(te_hit(s, 0, 5, 4, fixed_w, nullptr) == 0);   // left half of 'h'
  CHECK(te_hit(s, 0, 5, 5, fixed_w, nullptr) == 1);   // exactly the midpoint goes after
  CHECK(te_hit(s, 0, 5, 6, fixed_w, nullptr) == 1);   // right half of 'h'
  CHECK(te_hit(s, 0, 5, 14, fixed_w, nullptr) == 1);
  CHECK(te_hit(s, 0, 5, 16, fixed_w, nullptr) == 2);
  CHECK(te_hit(s, 0, 5, 44, fixed_w, nullptr) == 4);
  CHECK(te_hit(s, 0, 5, 46, fixed_w, nullptr) == 5);
  CHECK(te_hit(s, 0, 5, 49, fixed_w, nullptr) == 5);
  CHECK(te_hit(s, 0, 5, 500, fixed_w, nullptr) == 5);  // past the end
  // A range in the middle (one wrapped line): x is from the left of s[a].
  const char *t = "one two three";
  CHECK(te_hit(t, 4, 7, -1, fixed_w, nullptr) == 4);
  CHECK(te_hit(t, 4, 7, 16, fixed_w, nullptr) == 6);
  CHECK(te_hit(t, 4, 7, 300, fixed_w, nullptr) == 7);  // never past b
  CHECK(te_hit(t, 4, 4, 30, fixed_w, nullptr) == 4);   // an empty range
  CHECK(te_hit(t, 7, 4, 30, fixed_w, nullptr) == 7);   // b < a: a
  // Every x from -5 to 60 lands on the nearest boundary.
  for (int x = -5; x <= 60; x++) {
    int got = te_hit(s, 0, 5, x, fixed_w, nullptr);
    int want = x <= 0 ? 0 : (x >= 50 ? 5 : (x + 5) / 10);
    CHECK(got == want);
  }
}

VTEST(te_hit_midpoint_proportional) {
  const char *s = "iWi";  // boundaries at 0, 4, 20, 24
  CHECK(te_hit(s, 0, 3, 1, prop_w, nullptr) == 0);
  CHECK(te_hit(s, 0, 3, 2, prop_w, nullptr) == 1);   // midpoint of 'i' (0..4)
  CHECK(te_hit(s, 0, 3, 11, prop_w, nullptr) == 1);  // left half of 'W' (4..20, mid 12)
  CHECK(te_hit(s, 0, 3, 12, prop_w, nullptr) == 2);
  CHECK(te_hit(s, 0, 3, 21, prop_w, nullptr) == 2);  // 'i' 20..24, mid 22
  CHECK(te_hit(s, 0, 3, 22, prop_w, nullptr) == 3);
  CHECK(te_hit(s, 0, 3, 30, prop_w, nullptr) == 3);
  int calls = 0;
  te_hit("abcdefghij", 0, 10, 5, prop_w, &calls);  // stops measuring once past x
  CHECK(calls == 1);
}

VTEST(te_window_fits_whole) {
  int first = 3;
  int end = te_window("short", 2, 100, fixed_w, nullptr, &first);
  CHECK(first == 0 && end == 5);
  first = 0;
  end = te_window("", 0, 100, fixed_w, nullptr, &first);
  CHECK(first == 0 && end == 0);
}

VTEST(te_window_follows_the_cursor_both_ways) {
  const char *s = "abcdefghijklmnopqrst";  // 20 chars, 200 px; the view is 8 chars
  const int W = 80;
  int first = 0;
  // Cursor at the end (the old fit_tail view): the tail shows, the cursor at its right.
  int end = te_window(s, 20, W, fixed_w, nullptr, &first);
  CHECK(first == 12 && end == 20);
  // Left inside the view: the view stays put.
  end = te_window(s, 14, W, fixed_w, nullptr, &first);
  CHECK(first == 12 && end == 20);
  // Left of the view: the view's first is the cursor.
  end = te_window(s, 5, W, fixed_w, nullptr, &first);
  CHECK(first == 5 && end == 13);
  // Right inside it, then past it: the view moves just enough.
  end = te_window(s, 13, W, fixed_w, nullptr, &first);
  CHECK(first == 5 && end == 13);
  end = te_window(s, 15, W, fixed_w, nullptr, &first);
  CHECK(first == 7 && end == 15);
  // To the start.
  end = te_window(s, 0, W, fixed_w, nullptr, &first);
  CHECK(first == 0 && end == 8);
  // A stale first past the cursor, or below 0, is put right.
  first = 50;
  end = te_window(s, 3, W, fixed_w, nullptr, &first);
  CHECK(first == 3 && end == 11);
  first = -4;
  end = te_window(s, 3, W, fixed_w, nullptr, &first);
  CHECK(first == 0 && end == 8);
  // A null first works as 0.
  CHECK(te_window(s, 3, W, fixed_w, nullptr, nullptr) == 8);
}

VTEST(te_window_slides_back_after_a_deletion) {
  char s[32] = "abcdefghijklmnopqrst";
  const int W = 80;
  int first = 0;
  te_window(s, 20, W, fixed_w, nullptr, &first);
  CHECK(first == 12);
  s[19] = 0;  // backspace at the end: 19 chars, cursor 19
  int end = te_window(s, 19, W, fixed_w, nullptr, &first);
  CHECK(first == 11 && end == 19);
  // A deletion in the middle with the view at the end keeps the view full.
  memmove(s + 15, s + 16, strlen(s + 16) + 1);  // 18 chars
  end = te_window(s, 15, W, fixed_w, nullptr, &first);
  CHECK(first == 10 && end == 18);
}

VTEST(te_window_property) {
  // Every text, cursor and width: the view fits, holds the cursor and starts at 0 when it all fits.
  const char *texts[] = {"Wi Wi Wi lMMl iiii WWWW abc", "MMMMMMMMMMMMMMMMMMMM", "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiii",
                         "Dentist at the new place on Elm"};
  for (const char *s : texts) {
    int len = (int)strlen(s);
    for (int W = 20; W <= 200; W += 13) {
      int first = 0;
      for (int pass = 0; pass < 2; pass++)
        for (int c = 0; c <= len; c++) {
          int cur = pass ? len - c : c;  // left to right, then back
          int end = te_window(s, cur, W, prop_w, nullptr, &first);
          CHECK(first >= 0 && first <= cur && cur <= end && end <= len);
          CHECK(prop_w(s + first, end - first, nullptr) <= W || end - first <= 1);
          if (prop_w(s, len, nullptr) <= W) CHECK(first == 0 && end == len);
          // The view is as full as it can be: one more character on the right would not fit.
          if (end < len) CHECK(prop_w(s + first, end + 1 - first, nullptr) > W);
        }
    }
  }
}

VTEST(te_wrap_lines) {
  // 10 px a character, lines 100 px (10 characters).
  int st[3] = {}, en[3] = {};
  const char *s = "Dinner is ready now come";  // "Dinner is " | "ready now " | "come"
  int n = te_wrap(s, 100, fixed_w, nullptr, st, en, 3);
  CHECK(n == 3);
  CHECK(st[0] == 0 && en[0] == 9);    // "Dinner is"
  CHECK(st[1] == 10 && en[1] == 19);  // "ready now"
  CHECK(st[2] == 20 && en[2] == 24);  // "come"
  // A line that ends in a space still fits: the trailing space doesn't count.
  n = te_wrap("abcdefghij klm", 100, fixed_w, nullptr, st, en, 3);
  CHECK(n == 2 && st[0] == 0 && en[0] == 10 && st[1] == 11 && en[1] == 14);
  // A word wider than the line breaks mid-word.
  n = te_wrap("abcdefghijklmno", 100, fixed_w, nullptr, st, en, 3);
  CHECK(n == 2 && en[0] == 10 && st[1] == 10 && en[1] == 15);
  // Spaces at the break are skipped; at most max lines.
  n = te_wrap("aaaa   bbbbbbbbbb cccccccccc dddd", 100, fixed_w, nullptr, st, en, 2);
  CHECK(n == 2 && en[0] == 4 && st[1] == 7);
  CHECK(te_wrap("", 100, fixed_w, nullptr, st, en, 3) == 0);
  // One character wider than the line never sticks.
  n = te_wrap("MM", 10, prop_w, nullptr, st, en, 3);
  CHECK(n == 2 && st[1] == 1);
}

VTEST(te_note_cursor_on_each_line_and_at_a_break) {
  int st[3] = {}, en[3] = {};
  const char *s = "Dinner is ready now";  // "Dinner is" [0,9) | "ready now" [10,19)
  int n = te_wrap(s, 100, fixed_w, nullptr, st, en, 3);
  CHECK(n == 2);
  CHECK(te_line_of(st, n, 0) == 0);
  CHECK(te_line_of(st, n, 5) == 0);
  CHECK(te_line_of(st, n, 9) == 0);    // after "is", before the break space
  CHECK(te_line_of(st, n, 10) == 1);   // exactly at the break: the next line
  CHECK(te_line_of(st, n, 15) == 1);
  CHECK(te_line_of(st, n, 19) == 1);   // the very end
  CHECK(te_line_of(st, 0, 0) == 0);    // empty text
  // A mid-word break: the boundary belongs to the next line too.
  n = te_wrap("abcdefghijklmno", 100, fixed_w, nullptr, st, en, 3);
  CHECK(te_line_of(st, n, 10) == 1 && te_line_of(st, n, 9) == 0);
  // A tap on line 1 hits inside that line's range only.
  n = te_wrap(s, 100, fixed_w, nullptr, st, en, 3);
  CHECK(te_hit(s, st[1], en[1], 0, fixed_w, nullptr) == 10);
  CHECK(te_hit(s, st[1], en[1], 26, fixed_w, nullptr) == 13);
  CHECK(te_hit(s, st[1], en[1], 999, fixed_w, nullptr) == 19);
  CHECK(te_hit(s, st[0], en[0], 999, fixed_w, nullptr) == 9);  // the end of line 0, before its space
}

VTEST(te_note_insert_then_wrap_check_undo) {
  // The note editor's rule: insert at the cursor, and undo it if a third line would be needed.
  char b[65] = "aaaa bbbb cccc dddd";  // 2 lines of 10
  TextEdit t{b, 64, 4};
  int st[3], en[3];
  CHECK(te_wrap(b, 100, fixed_w, nullptr, st, en, 3) == 2);
  bool ok = true;
  int inserted = 0;
  while (ok) {
    std::string before = b;
    int cur = t.cursor;
    CHECK(te_insert(t, 'x'));
    if (te_wrap(b, 100, fixed_w, nullptr, st, en, 3) > 2) {
      te_backspace(t);
      CHECK(before == b && t.cursor == cur);  // undone exactly
      ok = false;
    } else {
      inserted++;
    }
  }
  CHECK(inserted > 0 && inserted < 20);
}

VTEST(te_sentence_start_rule) {
  CHECK(te_sentence_start("", 0));
  CHECK(te_sentence_start("Dentist", 0));
  CHECK(!te_sentence_start("Dentist", 7));
  CHECK(!te_sentence_start("Dentist", 3));
  CHECK(te_sentence_start("Dr. Smith", 4));    // after ". "
  CHECK(!te_sentence_start("Dr. Smith", 3));   // right after the period, no space yet
  CHECK(te_sentence_start("Done? Next", 6));
  CHECK(te_sentence_start("Yes!  ok", 6));
  CHECK(!te_sentence_start("Yes!  ok", 7));
  CHECK(te_sentence_start("   x", 2));         // only spaces before
  CHECK(!te_sentence_start("a, b", 3));
  CHECK(te_sentence_start(nullptr, 3));
}

VTEST(te_voice_capital_at_the_cursor) {
  // The flags-byte rule (voice_apply_capital) looks at the text before the cursor, not the end.
  char t1[32] = "Dinner";
  voice_apply_capital(t1, "Call me. Then", 9, kVoiceTextSentenceCap);  // cursor after ". "
  CHECK(strcmp(t1, "Dinner") == 0);
  char t2[32] = "Dinner";
  voice_apply_capital(t2, "Call me. Then", 4, kVoiceTextSentenceCap);  // mid-sentence
  CHECK(strcmp(t2, "dinner") == 0);
  char t3[32] = "Dinner";
  voice_apply_capital(t3, "Call me. Then", 0, kVoiceTextSentenceCap);  // the start
  CHECK(strcmp(t3, "Dinner") == 0);
  char t4[32] = "Sam";
  voice_apply_capital(t4, "Call me. Then", 4, 0);  // a name's capital always stays
  CHECK(strcmp(t4, "Sam") == 0);
}

VTEST(te_registry) {
  char bufs[kTeSlots + 1][8] = {};
  TextEdit te[kTeSlots + 1];
  for (int i = 0; i <= kTeSlots; i++) te[i] = TextEdit{bufs[i], 7, 0};
  CHECK(te_find(bufs[0]) == nullptr);
  te_register(&te[0]);
  te_register(&te[0]);  // twice: one slot
  for (int i = 1; i < kTeSlots; i++) te_register(&te[i]);
  bool all = true;
  for (int i = 0; i < kTeSlots; i++) all = all && te_find(bufs[i]) == &te[i];
  CHECK(all);
  te_register(&te[kTeSlots]);  // full: not kept
  CHECK(te_find(bufs[kTeSlots]) == nullptr);
  te_unregister(&te[1]);
  te_register(&te[kTeSlots]);
  CHECK(te_find(bufs[kTeSlots]) == &te[kTeSlots] && te_find(bufs[1]) == nullptr);
  CHECK(te_find(nullptr) == nullptr);
  te_register(nullptr);
  for (TextEdit &t : te) te_unregister(&t);  // leave nothing dangling
  CHECK(te_find(bufs[0]) == nullptr && te_find(bufs[kTeSlots]) == nullptr);
}
