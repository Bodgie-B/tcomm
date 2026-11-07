// The comfort chart's data: reading comfort.csv back, the chart's units and the comfort zones.
// See comfort_chart.h.
#include "comfort_chart.h"
#include "civil.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace tc {

const ZoneRow kComfortZones[kZoneRows] = {
    {0, 70.9f, 79.0f, 77.1f, 82.9f},
    {28, 69.7f, 77.7f, 76.2f, 82.0f},
    {56, 68.4f, 76.5f, 75.3f, 81.1f},
    {84, 67.2f, 75.3f, 74.4f, 80.2f},
};

namespace {

constexpr int kColumns = 19;  // the file's columns (comfort.h)

// One field of a line: where it starts and how long it is (no quoting in this file).
struct Field { const char *p; size_t n; };

bool is_blank(const Field &f) { return f.n == 0; }

// "67.1" or "-0.5" as tenths. False when blank or not a number.
bool parse_tenths(const Field &f, int &out) {
  size_t i = 0;
  bool neg = false;
  if (i < f.n && f.p[i] == '-') { neg = true; i++; }
  if (i >= f.n || f.p[i] < '0' || f.p[i] > '9') return false;
  int whole = 0;
  while (i < f.n && f.p[i] >= '0' && f.p[i] <= '9') { whole = whole * 10 + (f.p[i] - '0'); if (whole > 3000) return false; i++; }
  int tenth = 0;
  if (i < f.n && f.p[i] == '.') {
    i++;
    if (i < f.n && f.p[i] >= '0' && f.p[i] <= '9') tenth = f.p[i++] - '0';
  }
  while (i < f.n && f.p[i] >= '0' && f.p[i] <= '9') i++;  // never written, but harmless
  if (i != f.n) return false;
  out = (neg ? -1 : 1) * (whole * 10 + tenth);
  return true;
}

bool parse_uint(const Field &f, uint32_t &out) {
  if (f.n == 0 || f.n > 9) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < f.n; i++) {
    if (f.p[i] < '0' || f.p[i] > '9') return false;
    v = v * 10 + (uint32_t)(f.p[i] - '0');
  }
  out = v;
  return true;
}

bool same(const Field &f, const char *word) { return strlen(word) == f.n && !strncmp(f.p, word, f.n); }

int two(const char *p) { return (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') ? (p[0] - '0') * 10 + (p[1] - '0') : -1; }

// Walks the lines of the text; calls fn(line, len) for each non-empty one, with CRLF's CR dropped.
template <class F>
void each_line(const char *text, size_t len, F fn) {
  size_t i = 0;
  while (i < len) {
    size_t j = i;
    while (j < len && text[j] != '\n') j++;
    size_t e = j;
    if (e > i && text[e - 1] == '\r') e--;
    if (e > i) fn(text + i, e - i);
    i = j + 1;
  }
}

// Splits a line into fields; returns how many there were (at most kColumns kept).
int split(const char *line, size_t len, Field *f) {
  int n = 0;
  size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i == len || line[i] == ',') {
      if (n < kColumns) f[n] = Field{line + start, i - start};
      n++;
      start = i + 1;
    }
  }
  return n;
}

bool read_point(const char *line, size_t len, ComfortPoint &o) {
  Field f[kColumns];
  if (split(line, len, f) < kColumns) return false;
  uint32_t n;
  int t, h;
  if (!parse_uint(f[0], n) || n == 0 || !parse_tenths(f[8], t) || !parse_tenths(f[9], h) || h < 0) return false;
  int feel = -1;
  for (int k = 0; k < kFeelCount; k++) if (same(f[10], feel_code((Feel)k))) feel = k;
  if (feel < 0) return false;
  o = ComfortPoint{};
  o.n = n; o.t = (int16_t)t; o.h = (uint16_t)h; o.feel = (uint8_t)feel;
  o.part = 0xFF; o.weekday = 0xFF;
  for (int k = 0; k < 4; k++) if (same(f[4], day_part_word((DayPart)k))) o.part = (uint8_t)k;
  static const char *const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  for (int k = 0; k < 7; k++) if (same(f[5], kDays[k])) o.weekday = (uint8_t)k;
  if (f[1].n == 10 && f[1].p[4] == '-' && f[1].p[7] == '-') {
    int y1 = two(f[1].p), y2 = two(f[1].p + 2), mo = two(f[1].p + 5), d = two(f[1].p + 8);
    if (y1 >= 0 && y2 >= 0 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31) {
      o.year = (uint16_t)(y1 * 100 + y2); o.month = (uint8_t)mo; o.day = (uint8_t)d;
    }
  }
  if (f[2].n == 5 && f[2].p[2] == ':') {
    int hh = two(f[2].p), mm = two(f[2].p + 3);
    if (hh >= 0 && hh < 24 && mm >= 0 && mm < 60) { o.hour = (uint8_t)hh; o.minute = (uint8_t)mm; }
  }
  o.removed = !is_blank(f[kComfortRemovedColumn]);
  return true;
}

}  // namespace

// Every non-empty line, less the header: an upper bound for comfort_parse.
int comfort_count_lines(const char *text, size_t len) {
  if (!text) return 0;
  int n = 0;
  each_line(text, len, [&](const char *l, size_t k) { if (!(k >= 2 && l[0] == 'n' && l[1] == ',')) n++; });
  return n;
}

int comfort_parse(const char *text, size_t len, ComfortPoint *out, int cap) {
  if (!text || !out) return 0;
  int n = 0;
  each_line(text, len, [&](const char *l, size_t k) {
    if (n < cap && read_point(l, k, out[n])) n++;
  });
  return n;
}

float grains_per_lb(float temp_f, float rh_pct) {
  double c = (temp_f - 32.0) * 5.0 / 9.0;
  double ps = 1000.0 * exp(16.6536 - 4030.183 / (c + 235.0));  // saturation pressure, Pa
  double pw = rh_pct / 100.0 * ps;
  double w = 0.62198 * pw / (101325.0 - pw);
  return (float)(w * 7000.0);
}

// Mike, 2026-11-06: the removed column says when. With no clock (utc 0) it still says REMOVED so the
// answer is marked.
size_t comfort_removed_text(uint32_t utc, int32_t offset, char *out, size_t cap) {
  if (!out || !cap) return 0;
  int n;
  if (!utc) n = snprintf(out, cap, "REMOVED");
  else {
    Civil c = civil_from_seconds((int64_t)utc + offset);
    n = snprintf(out, cap, "%04d-%02d-%02d %02d:%02d", c.year, c.month, c.day, c.hour, c.minute);
  }
  return n > 0 && (size_t)n < cap ? (size_t)n : (out[0] = 0, 0);
}

}  // namespace tc
