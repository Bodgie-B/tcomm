// The tenant's comfort chart (Mike, 2026-11-06; docs/temperature-requests.md, stage 3): the survey's
// answers read back from comfort.csv and placed on a psychrometric chart. A long press on the
// downstairs INSIDE temperature opens it on either panel; on the downstairs panel a tap near a dot
// can remove an answer given by mistake (it's marked removed in the file, never erased).
//
// Plain C++ (no LVGL): parsing, the chart's units and the standard comfort zones, tested on the PC.
#pragma once
#include "comfort.h"

namespace tc {

// One answer as the chart needs it, from one line of comfort.csv.
struct ComfortPoint {
  uint32_t n;          // the answer's number (first column)
  int16_t t;           // downstairs temperature, tenths of a degree F
  uint16_t h;          // downstairs humidity, tenths of a percent
  uint8_t feel;        // a Feel
  uint8_t part;        // a DayPart; 0xFF when the line has no time
  bool removed;        // the removed column is filled
  uint16_t year;       // local date and time from the line; year 0 when blank
  uint8_t month, day, hour, minute;
  uint8_t weekday;     // 0 Sunday .. 6 Saturday; 0xFF when blank
};

// The number of answer lines in the text (not the header): enough room for comfort_parse.
int comfort_count_lines(const char *text, size_t len);
// Parses comfort.csv text: skips the header, malformed lines (a short last line from a power cut
// included) and lines without the inside reading (they can't be placed). Returns how many points
// were written to out (at most cap), oldest first.
int comfort_parse(const char *text, size_t len, ComfortPoint *out, int cap);

// The chart's height: grains of moisture per pound of dry air, from temperature (F) and relative
// humidity (percent), at sea-level pressure. 7000 grains = 1 lb.
float grains_per_lb(float temp_f, float rh_pct);

// ASHRAE 55's comfort zones as drawn (docs/temperature-requests.md, "The standard comfort zone"):
// for each moisture level, the coolest and warmest comfortable temperatures (F) in winter clothing
// (1.0 clo) and summer clothing (0.5 clo), worked out with the ISO 7730 PMV method at 1.1 met and
// still air. Straight lines between the rows; the zones end at 84 gr/lb.
struct ZoneRow {
  float gr;
  float winter_lo, winter_hi, summer_lo, summer_hi;
};
constexpr int kZoneRows = 4;
extern const ZoneRow kComfortZones[kZoneRows];

// The chart's range (the mockup's): 55-85 F across, 0-120 gr/lb up.
constexpr float kChartTMin = 55, kChartTMax = 85, kChartGrMax = 120;

// The removed column's text for an answer removed at utc, in local time: "YYYY-MM-DD HH:MM".
size_t comfort_removed_text(uint32_t utc, int32_t offset, char *out, size_t cap);
constexpr int kComfortRemovedColumn = 18;  // 0-based: the last column, "removed"

}  // namespace tc
