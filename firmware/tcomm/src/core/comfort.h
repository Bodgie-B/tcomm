// The downstairs tenant's comfort survey (Mike, 2026-11-06; docs/temperature-requests.md). The
// tenant taps INSIDE on the DOWNSTAIRS tile and says how it feels; every answer is kept forever on
// the SD card ("comfort.csv" in /tcomm), with the readings and the time, and copied to upstairs so
// either panel can later draw the tenant's comfort chart.
//
// Stage 1 records answers only. The arrow and sent fields are for stage 2 (asking upstairs for heat
// or cooling) and removed for stage 3 (removing a mistake from the chart); they stay NONE, blank
// and 0 until then.
//
// Plain data with no default member initializers: it rides in the protocol's Msg union.
#pragma once
#include "types.h"

namespace tc {

// Top to bottom on the survey.
enum class Feel : uint8_t { MuchTooHot = 0, TooWarm = 1, JustRight = 2, TooCool = 3, MuchTooCold = 4 };
constexpr int kFeelCount = 5;
inline bool feel_valid(uint8_t v) { return v < kFeelCount; }

// Stage 2: the request sent with an answer, and whether the tenant sent it.
enum class Ask : uint8_t { None = 0, UrgentHeat = 1, MildHeat = 2, MildCool = 3, UrgentCool = 4 };
inline bool ask_valid(uint8_t v) { return v <= 4; }
enum class AskSent : uint8_t { Blank = 0, Send = 1, Cancel = 2 };
inline bool ask_sent_valid(uint8_t v) { return v <= 2; }

// Which readings an answer carries.
enum ComfortHave : uint8_t { kHaveInside = 0x01, kHaveUpstairs = 0x02, kHaveOutside = 0x04 };

struct ComfortAnswer {
  uint32_t n;        // its number: 1 for the first answer ever, counted downstairs
  uint32_t utc;      // when it was given; 0 = the clock wasn't set
  int32_t offset;    // the UTC offset then, in seconds (local time = utc + offset)
  uint8_t clock_q;   // the ClockQuality then
  uint8_t feel;      // a Feel
  uint8_t ask;       // an Ask (stage 2)
  uint8_t sent;      // an AskSent (stage 2)
  uint8_t status;    // the tenant's Status then
  uint8_t have;      // ComfortHave bits
  int16_t in_t;      // downstairs, tenths of a degree F
  uint16_t in_h;     // downstairs, tenths of a percent
  int16_t up_t;      // upstairs, tenths of a degree F
  uint16_t up_h;     // upstairs, tenths of a percent
  int16_t out_t;     // outside, tenths of a degree F
  uint8_t out_h;     // outside, percent
  uint32_t removed;  // when the tenant removed it (stage 3); 0 = not removed
};

// Parts of the day (Mike's theory: comfort may differ between them). NIGHT 10 PM-6 AM (the screen's
// dark hours), MORNING 6-10 AM, MIDDAY 10 AM-4 PM, EVENING 4-10 PM.
enum class DayPart : uint8_t { Night = 0, Morning = 1, Midday = 2, Evening = 3 };
DayPart day_part(int minute_of_day);
const char *day_part_word(DayPart p);  // "NIGHT"
// By month, as weather services count seasons: WINTER Dec-Feb, SPRING Mar-May, SUMMER Jun-Aug,
// FALL Sep-Nov. month 1-12.
const char *season_word(int month);

const char *feel_code(Feel f);      // the file's word: "MUCH_TOO_HOT"
const char *feel_label(Feel f);     // the button's: "MUCH TOO HOT"
const char *ask_code(Ask a);        // "NONE", "URGENT_HEAT", "MILD_HEAT", "MILD_COOL", "URGENT_COOL"
const char *ask_sent_code(AskSent s);  // "", "SEND", "CANCEL"

// The file. One header line, then one line per answer, both ending in "\n":
//   n,date,time,clock_sure,part_of_day,weekday,month,season,inside_f,inside_rh,answer,arrow,sent,
//   status,upstairs_f,upstairs_rh,outside_f,outside_rh,removed
extern const char kComfortFile[];       // "comfort.csv"
extern const char kComfortCsvHeader[];  // the header line above, with its "\n"
// Returns the line's length, or 0 if it didn't fit in cap. kComfortLineMax holds any line (the
// longest possible, every field at its widest, is under 170 characters) and the header.
constexpr size_t kComfortLineMax = 200;
size_t comfort_csv_line(const ComfortAnswer &a, char *out, size_t cap);

}  // namespace tc
