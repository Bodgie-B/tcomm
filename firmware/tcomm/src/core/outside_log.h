// The outside weather kept forever (Mike, 2026-11-07): every 15 minutes each panel adds the outside
// reading (temperature, humidity, forecast) to the day's file on its SD card, "oYYYYMMDD.csv" in
// /tcomm (local date). A long press on the Outside tile opens a chart of a day: temperature and
// humidity over midnight to midnight, the most frequent forecast icon above each hour, arrows for
// earlier days. Later the files can give monthly or seasonal trends for the house.
//
// Plain C++ (no LVGL): the file format, parsing a day back, and the hourly forecast.
#pragma once
#include "types.h"

namespace tc {

constexpr uint32_t kOutsideEveryS = 15 * 60;  // Mike: a reading every 15 minutes

// One saved reading. Readings are taken on the quarter hours (local :00, :15, :30, :45) while the
// panel has a fresh outside reading; with none fresh, that quarter is skipped (a gap on the chart).
struct OutsideSample {
  uint32_t utc;      // when it was taken (the panel's clock)
  int32_t offset;    // the UTC offset then, seconds
  int16_t t;         // tenths of a degree F
  uint8_t h;         // percent
  uint8_t forecast;  // a Forecast code (types.h), 0 unknown
  uint8_t flags;     // the OUTSIDE flags (danger heat, danger cold)
};

// The day file for the local day containing utc: "o20261107.csv" (13 characters and a NUL).
constexpr int kOutsideNameLen = 16;
void outside_file_name(uint32_t utc, int32_t offset, char out[kOutsideNameLen]);
// The same for a local day number (days since 1970-01-01 in local time), for the chart's arrows.
void outside_file_name_day(int32_t local_day, char out[kOutsideNameLen]);

// The file: a header, then one line per reading, both ending in "\n":
//   date,time,utc,temp_f,rh,forecast,flags
// date YYYY-MM-DD and time HH:MM local; temp_f one decimal; forecast the code number; flags the
// OUTSIDE flags number.
extern const char kOutsideCsvHeader[];
size_t outside_csv_line(const OutsideSample &s, char *out, size_t cap);  // 0 if it didn't fit

// A reading read back for the chart.
struct OutsidePoint {
  uint16_t minute;   // local minute of the day, 0-1439
  int16_t t;         // tenths of a degree F
  uint8_t h;         // percent
  uint8_t forecast;  // Forecast code
  uint8_t flags;
};
// Parses a day's file: skips the header and malformed lines (a short last line from a power cut
// too). Returns how many points were written (at most cap), in file order.
int outside_parse(const char *text, size_t len, OutsidePoint *out, int cap);
// The forecast shown above each hour: the code seen most often among that hour's readings (unknown
// codes don't count; a tie goes to the later reading); 0 for an hour with none.
void outside_hourly_forecast(const OutsidePoint *p, int n, uint8_t out[24]);

}  // namespace tc
