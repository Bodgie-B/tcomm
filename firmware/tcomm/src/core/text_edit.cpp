// Tap to place the cursor: see text_edit.h.
#include "text_edit.h"
#include <string.h>

namespace tc {

int te_len(const TextEdit &t) { return t.buf ? (int)strlen(t.buf) : 0; }

void te_set(TextEdit &t, int i) {
  int n = te_len(t);
  t.cursor = i < 0 ? 0 : (i > n ? n : i);
}

bool te_insert(TextEdit &t, char c) {
  if (!t.buf || !c) return false;
  int n = te_len(t);
  if ((size_t)n >= t.cap) return false;
  te_set(t, t.cursor);
  memmove(t.buf + t.cursor + 1, t.buf + t.cursor, (size_t)(n - t.cursor) + 1);  // the 0 too
  t.buf[t.cursor++] = c;
  return true;
}

bool te_insert_str(TextEdit &t, const char *s) {
  if (!s) return true;
  for (; *s; s++)
    if (!te_insert(t, *s)) return false;
  return true;
}

void te_backspace(TextEdit &t) {
  if (!t.buf) return;
  te_set(t, t.cursor);
  if (t.cursor == 0) return;
  int n = te_len(t);
  memmove(t.buf + t.cursor - 1, t.buf + t.cursor, (size_t)(n - t.cursor) + 1);
  t.cursor--;
}

void te_delete(TextEdit &t) {
  if (!t.buf) return;
  te_set(t, t.cursor);
  int n = te_len(t);
  if (t.cursor >= n) return;
  memmove(t.buf + t.cursor, t.buf + t.cursor + 1, (size_t)(n - t.cursor));
}

int te_hit(const char *s, int a, int b, int x, WidthFn w, void *ctx) {
  if (b < a) b = a;
  if (!s || !w || x <= 0) return a;
  int prev = 0;  // the width of s[a, i - 1)
  for (int i = a + 1; i <= b; i++) {
    int pw = w(s + a, i - a, ctx);
    if (x < pw) return x - prev < pw - x ? i - 1 : i;  // the glyph s[i - 1] split at its midpoint
    prev = pw;
  }
  return b;
}

int te_window(const char *s, int cursor, int width, WidthFn w, void *ctx, int *first) {
  int len = s ? (int)strlen(s) : 0;
  if (cursor < 0) cursor = 0;
  if (cursor > len) cursor = len;
  int f = first ? *first : 0;
  if (f < 0) f = 0;
  if (!s || !w || w(s, len, ctx) <= width) {  // it all fits: no window
    if (first) *first = 0;
    return len;
  }
  if (f > cursor) f = cursor;                                    // the cursor went left of the view
  while (f < cursor && w(s + f, cursor - f, ctx) > width) f++;  // or right of it
  while (f > 0 && w(s + f - 1, len - f + 1, ctx) <= width) f--;  // the tail is short: fill the room
  int end = f;
  while (end < len && w(s + f, end + 1 - f, ctx) <= width) end++;
  if (end < cursor) end = cursor;  // one glyph wider than the field: the cursor still counts
  if (first) *first = f;
  return end;
}

int te_wrap(const char *t, int width, WidthFn w, void *ctx, int *start, int *end, int max) {
  if (!t || !w) return 0;
  // The width of t[a, b) with its trailing spaces ignored (a line that ends in a space still fits:
  // the space is where it breaks).
  auto span_w = [&](int a, int b) {
    while (b > a && t[b - 1] == ' ') b--;
    return w(t + a, b - a, ctx);
  };
  int len = (int)strlen(t), pos = 0, n = 0;
  while (pos < len && n < max) {
    start[n] = pos;
    int fit = pos;  // the most characters that fit on this line
    for (int i = pos + 1; i <= len; i++) {
      if (span_w(pos, i) > width) break;
      fit = i;
    }
    if (fit == pos) fit = pos + 1;  // never stuck on one wide character
    int brk = fit, next = fit;
    if (fit < len && t[fit] != ' ') {
      int sp = fit - 1;
      while (sp >= pos && t[sp] != ' ') sp--;
      if (sp >= pos) brk = sp, next = sp + 1;  // break at the last space; no space: mid-word
    }
    end[n] = brk;
    while (brk > pos && end[n] > pos && t[end[n] - 1] == ' ') end[n]--;
    while (next < len && t[next] == ' ') next++;
    pos = next;
    n++;
  }
  return n;
}

int te_line_of(const int *start, int n, int cursor) {
  int line = 0;
  for (int i = 1; i < n; i++)
    if (start[i] <= cursor) line = i;
  return line;
}

bool te_sentence_start(const char *s, int cursor) {
  if (!s || cursor <= 0) return true;
  int n = (int)strlen(s);
  int i = (cursor < n ? cursor : n) - 1;
  int spaces = 0;
  while (i >= 0 && s[i] == ' ') i--, spaces++;
  if (i < 0) return true;
  return spaces > 0 && (s[i] == '.' || s[i] == '?' || s[i] == '!');
}

namespace {
TextEdit *g_reg[kTeSlots] = {};
}

void te_register(TextEdit *t) {
  if (!t) return;
  for (TextEdit *r : g_reg)
    if (r == t) return;
  for (TextEdit *&r : g_reg)
    if (!r) {
      r = t;
      return;
    }
}

void te_unregister(TextEdit *t) {
  for (TextEdit *&r : g_reg)
    if (r == t) r = nullptr;
}

TextEdit *te_find(const char *buf) {
  if (!buf) return nullptr;
  for (TextEdit *r : g_reg)
    if (r && r->buf == buf) return r;
  return nullptr;
}

}  // namespace tc
