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

// ---- The view (Mike, 2026-11-18) ----------------------------------------------------------------
// Mike: the chart is large and the group of dots is small, so a second after it opens the chart
// zooms in smoothly to the dots, to an area half as big again as the dots take up. A view is the
// part of the chart the plot shows: a temperature range across and a moisture range up.
struct ChartView {
  float t0, t1;  // degrees F at the plot's left and right edges
  float g0, g1;  // gr/lb at the plot's bottom and top edges
};
constexpr ChartView kChartFull = {kChartTMin, kChartTMax, 0, kChartGrMax};
// "50% greater than the extents" of the dots (Mike, 2026-11-18): the dots' box, 1.5 times as wide
// and as tall, about its middle.
constexpr float kChartZoomGrow = 1.5f;
// One dot or a tight cluster must not zoom absurdly far: never less than a fifth of the chart each
// way. 6 F across leaves 6 or 7 labelled degree lines; 24 gr/lb up leaves 4 or 5 lines 5 apart.
constexpr float kChartMinSpanT = 6, kChartMinSpanG = 24;
// The zoom's timing (Mike, 2026-11-18): the whole chart for a second, then about 1.4 s of zoom.
constexpr uint32_t kChartHoldMs = 1000, kChartZoomMs = 1400;

// Whether the chart draws this answer with this chip chosen (0 = ALL, else a DayPart + 1): not
// removed, a known answer, the chosen part of the day, and inside the whole chart's range.
bool chart_point_shown(const ComfortPoint &p, int chip);
// The view for dots spanning these temperatures and moistures: their box grown by kChartZoomGrow
// about its middle, no smaller than the minimum spans, moved to lie inside the whole chart, and
// never larger than it.
ChartView chart_view_around(float t_lo, float t_hi, float g_lo, float g_hi);
// The view to zoom to for the answers now shown; the whole chart when none is shown.
ChartView chart_target_view(const ComfortPoint *pts, int n, int chip);
// Whether two views are the same to well under a pixel.
bool chart_view_same(const ChartView &a, const ChartView &b);
// The zoom's pace: 0 at u = 0, 1 at u = 1 (u is the share of the time gone), starting gently from
// rest, fastest a little before halfway, and slowing over a long tail: 1 - (1 - u^2)^3. At three
// quarters of the time 92 % of the way is done, so the last quarter is a slow settle.
float chart_ease(float u);
// The view a share e (0..1, already eased) of the way from a to b. The two edges of each axis move
// together so the span shrinks or grows by the same factor in equal steps of e (a zoom that looks
// even), which also keeps one point of the chart still on the screen for the whole move.
ChartView chart_view_lerp(const ChartView &a, const ChartView &b, float e);
// The step between labelled grid lines for a span: the largest of 1, 2, 5 times a power of ten
// that still gives at least 4 steps (so 4 to 10 lines). 30 F gives 5, 120 gr/lb gives 20, as the
// whole chart always had.
float chart_nice_step(float span);
// The step between relative-humidity curves for a view, in percent: 20 as on the whole chart,
// or 10, 5, 2 or 1 when fewer than two of the wider-spaced curves would cross the view.
int chart_rh_step(const ChartView &v);
// Where a temperature and a moisture fall on a plot whose edges are at these pixels.
float chart_x(const ChartView &v, float t, int left, int right);
float chart_y(const ChartView &v, float g, int top, int bottom);

// The removed column's text for an answer removed at utc, in local time: "YYYY-MM-DD HH:MM".
size_t comfort_removed_text(uint32_t utc, int32_t offset, char *out, size_t cap);
constexpr int kComfortRemovedColumn = 18;  // 0-based: the last column, "removed"

}  // namespace tc
