#include "civil.h"
#include <stdio.h>

namespace tc {

int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int &y, int &m, int &d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yoe + era * 400 + (m <= 2));
}

Civil civil_from_seconds(int64_t s) {
  int64_t days = s >= 0 ? s / 86400 : (s - 86399) / 86400;
  int64_t rem = s - days * 86400;
  Civil c;
  civil_from_days(days, c.year, c.month, c.day);
  c.hour = (int)(rem / 3600);
  c.minute = (int)(rem % 3600 / 60);
  c.second = (int)(rem % 60);
  c.weekday = (int)((days % 7 + 11) % 7);  // 1970-01-01 was a Thursday (4)
  c.yday = (int)(days - days_from_civil(c.year, 1, 1));
  return c;
}

int64_t seconds_from_civil(int y, int m, int d, int hh, int mm, int ss) {
  return days_from_civil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
}

int minutes_of_day(int64_t s) {
  Civil c = civil_from_seconds(s);
  return c.hour * 60 + c.minute;
}

void format_time12(char *out, size_t n, int64_t s) {
  Civil c = civil_from_seconds(s);
  int h = c.hour % 12;
  if (h == 0) h = 12;
  snprintf(out, n, "%d:%02d %s", h, c.minute, c.hour < 12 ? "AM" : "PM");
}

void format_date_long(char *out, size_t n, int64_t s) {
  static const char *const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  // AP-style month abbreviations, as on the boards ("Oct. 12"); short months are spelled out.
  static const char *const kMonths[] = {"Jan.", "Feb.", "March", "April", "May", "June",
                                        "July", "Aug.", "Sept.", "Oct.", "Nov.", "Dec."};
  Civil c = civil_from_seconds(s);
  snprintf(out, n, "%s, %s %d", kDays[c.weekday], kMonths[c.month - 1], c.day);
}

void format_duration(char *out, size_t n, uint32_t sec) {
  uint32_t min = sec / 60;
  if (min < 1) snprintf(out, n, "under a min");
  else if (min < 60) snprintf(out, n, "%u min", (unsigned)min);
  else if (min < 24 * 60) {
    uint32_t h = min / 60, m = min % 60;
    if (m == 0 || h >= 3) snprintf(out, n, "%u hr", (unsigned)h);
    else snprintf(out, n, "%u hr %u min", (unsigned)h, (unsigned)m);
  } else {
    uint32_t d = min / (24 * 60);
    snprintf(out, n, "%u day%s", (unsigned)d, d == 1 ? "" : "s");
  }
}

}  // namespace tc
