// The comfort survey's words and its file's lines: see comfort.h.
#include "comfort.h"
#include "civil.h"
#include <stdio.h>
#include <string.h>

namespace tc {

const char kComfortFile[] = "comfort.csv";
const char kComfortCsvHeader[] =
    "n,date,time,clock_sure,part_of_day,weekday,month,season,inside_f,inside_rh,answer,arrow,sent,status,"
    "upstairs_f,upstairs_rh,outside_f,outside_rh,removed\n";

DayPart day_part(int m) {
  if (m >= 22 * 60 || m < 6 * 60) return DayPart::Night;
  if (m < 10 * 60) return DayPart::Morning;
  if (m < 16 * 60) return DayPart::Midday;
  return DayPart::Evening;
}

const char *day_part_word(DayPart p) {
  switch (p) {
    case DayPart::Night: return "NIGHT";
    case DayPart::Morning: return "MORNING";
    case DayPart::Midday: return "MIDDAY";
    case DayPart::Evening: return "EVENING";
  }
  return "";
}

const char *season_word(int month) {
  if (month < 1 || month > 12) return "";
  if (month == 12 || month <= 2) return "WINTER";
  if (month <= 5) return "SPRING";
  if (month <= 8) return "SUMMER";
  return "FALL";
}

const char *feel_code(Feel f) {
  switch (f) {
    case Feel::MuchTooHot: return "MUCH_TOO_HOT";
    case Feel::TooWarm: return "TOO_WARM";
    case Feel::JustRight: return "JUST_RIGHT";
    case Feel::TooCool: return "TOO_COOL";
    case Feel::MuchTooCold: return "MUCH_TOO_COLD";
  }
  return "";
}

const char *feel_label(Feel f) {
  switch (f) {
    case Feel::MuchTooHot: return "MUCH TOO HOT";
    case Feel::TooWarm: return "TOO WARM";
    case Feel::JustRight: return "JUST RIGHT";
    case Feel::TooCool: return "TOO COOL";
    case Feel::MuchTooCold: return "MUCH TOO COLD";
  }
  return "";
}

const char *ask_code(Ask a) {
  switch (a) {
    case Ask::None: return "NONE";
    case Ask::UrgentHeat: return "URGENT_HEAT";
    case Ask::MildHeat: return "MILD_HEAT";
    case Ask::MildCool: return "MILD_COOL";
    case Ask::UrgentCool: return "URGENT_COOL";
  }
  return "";
}

const char *ask_sent_code(AskSent s) {
  switch (s) {
    case AskSent::Blank: return "";
    case AskSent::Send: return "SEND";
    case AskSent::Cancel: return "CANCEL";
  }
  return "";
}

namespace {

// Our own table so this file doesn't depend on the controller.
const char *status_code(uint8_t s) {
  static const char *const k[] = {"OPEN", "BUSY", "PRIVATE", "SLEEPING", "AWAY"};
  return s < 5 ? k[s] : "";
}

// Tenths with one decimal, signs right for values between -1 and 0 ("-0.5").
void tenths(char *out, size_t n, int v) {
  int a = v < 0 ? -v : v;
  snprintf(out, n, "%s%d.%d", v < 0 ? "-" : "", a / 10, a % 10);
}

}  // namespace

size_t comfort_csv_line(const ComfortAnswer &a, char *out, size_t cap) {
  static const char *const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char date[16] = "", tm[8] = "", wd[8] = "", mon[8] = "", rem[24] = "";
  const char *part = "", *season = "";
  if (a.utc) {
    Civil c = civil_from_seconds((int64_t)a.utc + a.offset);
    snprintf(date, sizeof(date), "%04d-%02d-%02d", c.year, c.month, c.day);
    snprintf(tm, sizeof(tm), "%02d:%02d", c.hour, c.minute);
    part = day_part_word(day_part(c.hour * 60 + c.minute));
    snprintf(wd, sizeof(wd), "%s", c.weekday >= 0 && c.weekday < 7 ? kDays[c.weekday] : "");
    snprintf(mon, sizeof(mon), "%s", c.month >= 1 && c.month <= 12 ? kMonths[c.month - 1] : "");
    season = season_word(c.month);
  }
  if (a.removed) {
    Civil c = civil_from_seconds((int64_t)a.removed + a.offset);
    snprintf(rem, sizeof(rem), "%04d-%02d-%02d %02d:%02d", c.year, c.month, c.day, c.hour, c.minute);
  }
  char in_f[12] = "", in_h[12] = "", up_f[12] = "", up_h[12] = "", out_f[12] = "", out_h[8] = "";
  if (a.have & kHaveInside) { tenths(in_f, sizeof(in_f), a.in_t); tenths(in_h, sizeof(in_h), a.in_h); }
  if (a.have & kHaveUpstairs) { tenths(up_f, sizeof(up_f), a.up_t); tenths(up_h, sizeof(up_h), a.up_h); }
  if (a.have & kHaveOutside) { tenths(out_f, sizeof(out_f), a.out_t); snprintf(out_h, sizeof(out_h), "%u", (unsigned)a.out_h); }
  bool sure = a.clock_q == kClockNetwork || a.clock_q == kClockFromPeer;
  int n = snprintf(out, cap, "%u,%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", (unsigned)a.n, date, tm, sure ? 1 : 0,
                   part, wd, mon, season, in_f, in_h, feel_valid(a.feel) ? feel_code((Feel)a.feel) : "",
                   ask_valid(a.ask) ? ask_code((Ask)a.ask) : "", ask_sent_valid(a.sent) ? ask_sent_code((AskSent)a.sent) : "",
                   status_code(a.status), up_f, up_h, out_f, out_h, rem);
  return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

}  // namespace tc
