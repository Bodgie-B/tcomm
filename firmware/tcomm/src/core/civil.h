// Calendar arithmetic and text formatting for local time. The platform supplies the UTC offset
// (it knows the time-zone rules); everything here works on "local seconds" = utc + offset.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace tc {

struct Civil {
  int year, month, day;   // month 1-12, day 1-31
  int hour, minute, second;
  int weekday;            // 0 = Sunday
  int yday;               // 0-365
};

// Howard Hinnant's days_from_civil / civil_from_days, valid far beyond our range.
int64_t days_from_civil(int y, int m, int d);
Civil civil_from_seconds(int64_t local_seconds);
int64_t seconds_from_civil(int y, int m, int d, int hh, int mm, int ss);  // treats the fields as UTC
int minutes_of_day(int64_t local_seconds);

// "6:10 PM"
void format_time12(char *out, size_t n, int64_t local_seconds);
// "Monday, Oct. 12"
void format_date_long(char *out, size_t n, int64_t local_seconds);
// "3 min", "2 hr", "1 hr 20 min", "3 days" for "how long ago" texts
void format_duration(char *out, size_t n, uint32_t seconds);

}  // namespace tc
