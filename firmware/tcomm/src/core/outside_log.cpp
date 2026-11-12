// The outside weather kept forever (Mike, 2026-11-07): see outside_log.h.
#include "outside_log.h"
#include "civil.h"
#include <stdio.h>
#include <string.h>

namespace tc {

const char kOutsideCsvHeader[] = "date,time,utc,temp_f,rh,forecast,flags\n";

static void name_from_civil(const Civil &c, char out[kOutsideNameLen]) {
  snprintf(out, kOutsideNameLen, "o%04d%02d%02d.csv", c.year, c.month, c.day);
}

void outside_file_name(uint32_t utc, int32_t offset, char out[kOutsideNameLen]) {
  name_from_civil(civil_from_seconds((int64_t)utc + offset), out);
}

void outside_file_name_day(int32_t local_day, char out[kOutsideNameLen]) {
  name_from_civil(civil_from_seconds((int64_t)local_day * 86400), out);
}

size_t outside_csv_line(const OutsideSample &s, char *out, size_t cap) {
  Civil c = civil_from_seconds((int64_t)s.utc + s.offset);
  int t = s.t;
  const char *sign = t < 0 ? "-" : "";
  int a = t < 0 ? -t : t;
  int n = snprintf(out, cap, "%04d-%02d-%02d,%02d:%02d,%u,%s%d.%d,%u,%u,%u\n", c.year, c.month, c.day, c.hour,
                   c.minute, (unsigned)s.utc, sign, a / 10, a % 10, (unsigned)s.h, (unsigned)s.forecast,
                   (unsigned)s.flags);
  if (n < 0 || (size_t)n >= cap) {
    if (cap) out[0] = 0;
    return 0;
  }
  return (size_t)n;
}

// ---- Reading a day back ----

// A run of digits at p (up to end); false if there are none. Advances p.
static bool read_uint(const char *&p, const char *end, uint32_t &v, int max_digits = 10) {
  int n = 0;
  uint64_t x = 0;
  while (p < end && *p >= '0' && *p <= '9' && n < max_digits) {
    x = x * 10 + (uint32_t)(*p - '0');
    p++;
    n++;
  }
  if (n == 0 || x > 0xFFFFFFFFull) return false;
  v = (uint32_t)x;
  return true;
}

static bool expect(const char *&p, const char *end, char c) {
  if (p < end && *p == c) {
    p++;
    return true;
  }
  return false;
}

// One line (no line end) into a point; false if it isn't a complete reading.
static bool parse_line(const char *p, const char *end, OutsidePoint &o) {
  uint32_t y, mo, d, hh, mm, utc, ti, tf, rh, fc, fl;
  if (!read_uint(p, end, y, 4) || !expect(p, end, '-') || !read_uint(p, end, mo, 2) || !expect(p, end, '-') ||
      !read_uint(p, end, d, 2) || !expect(p, end, ','))
    return false;
  if (!read_uint(p, end, hh, 2) || !expect(p, end, ':') || !read_uint(p, end, mm, 2) || !expect(p, end, ',')) return false;
  if (hh > 23 || mm > 59 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
  if (!read_uint(p, end, utc, 10) || !expect(p, end, ',')) return false;
  bool neg = expect(p, end, '-');
  if (!read_uint(p, end, ti, 4) || !expect(p, end, '.') || !read_uint(p, end, tf, 1) || !expect(p, end, ',')) return false;
  if (!read_uint(p, end, rh, 3) || !expect(p, end, ',')) return false;
  if (!read_uint(p, end, fc, 3)) return false;
  if (!expect(p, end, ',') || !read_uint(p, end, fl, 3)) return false;
  if (p != end) return false;  // trailing junk: not one of ours
  int t = (int)(ti * 10 + tf);
  if (t > 32000) return false;
  o.minute = (uint16_t)(hh * 60 + mm);
  o.t = (int16_t)(neg ? -t : t);
  o.h = (uint8_t)(rh > 255 ? 255 : rh);
  o.forecast = (uint8_t)(fc > 255 ? 255 : fc);
  o.flags = (uint8_t)(fl > 255 ? 255 : fl);
  return true;
}

int outside_parse(const char *text, size_t len, OutsidePoint *out, int cap) {
  int n = 0;
  const char *p = text, *end = text + len;
  while (p < end && n < cap) {
    const char *e = p;
    while (e < end && *e != '\n') e++;
    const char *le = e;
    if (le > p && le[-1] == '\r') le--;  // CRLF
    // The header and any damaged line fail parse_line (it needs a digit date and all seven fields),
    // and a last line cut short by a power cut lacks fields, so it is skipped the same way.
    OutsidePoint o;
    if (parse_line(p, le, o)) out[n++] = o;
    p = e < end ? e + 1 : end;
  }
  return n;
}

void outside_hourly_forecast(const OutsidePoint *pts, int n, uint8_t out[24]) {
  for (int hr = 0; hr < 24; hr++) {
    int count[10] = {};
    int last[10] = {};  // index of the latest reading with that code, for ties
    for (int i = 0; i < n; i++) {
      if (pts[i].minute / 60 != hr) continue;
      uint8_t c = pts[i].forecast;
      if (c < 1 || c > 9) continue;  // unknown codes don't count
      count[c]++;
      last[c] = i;
    }
    uint8_t best = 0;
    for (int c = 1; c <= 9; c++) {
      if (!count[c]) continue;
      if (!best || count[c] > count[best] || (count[c] == count[best] && last[c] > last[best])) best = (uint8_t)c;
    }
    out[hr] = best;
  }
}

}  // namespace tc
