// The artwork of the Pd boards (P1d to P6d, the GUI spec's visual reference) as data.
// tools/boards_to_art.py turns the boards' display lists into board_art_data.h: every shape as
// points (curves already flattened), its fill or stroke, and where its colour comes from. The
// boards show six status and light combinations; the scenery colours are stored as rules on their
// zone's base colour, so any status in any light comes out as the boards' generator would make it.
#pragma once
#include <stdint.h>
#include "../gfx/canvas.h"

namespace boardart {

enum Kind : uint8_t { kFill, kStroke, kDisc, kRing };

// Where a shape's colour comes from.
enum Role : uint8_t {
  kZoneW,    // floor tile: the weather zone's base colour, by the rule for the tile's class
  kZoneS,    // floor tile: the status colour, by the rule for the tile's class
  kFixed,    // rgb[0]
  kPerMode,  // rgb[day, evening, night]
  kStripK,   // the date strip's colour times v[0]
  kFg,       // icons and badges: the drawing colour
  kBg,       //   and the colour of their cut-outs and glyph strokes
};

// Tile classes, for the zone rules: dark tiles by day and evening, light ones (BUSY, dark text),
// and every tile at night.
enum Class : uint8_t { kDark = 0, kLight = 1, kNight = 2 };

struct Shape {
  Kind kind;
  Role role;
  uint8_t clip;     // 1: inside the status zone only
  uint8_t ops;      // bit c set: class c lightens toward white by v[c]; clear: scales by v[c]
  float v[3];
  uint32_t rgb[3];
  float w, r, cx, cy;  // stroke width; disc or ring radius and centre
  uint16_t sub0, nsub;  // outlines (fills) or polylines (strokes) in kSubs
};
struct Sub {
  uint16_t pt0, npt;
  uint8_t closed;
};
struct Group {
  const Shape *shapes;
  uint16_t n;
};

// Sky per phase: bands from y 0, 190, 290 and 380; three rings around the sun or moon (outer to
// inner) and the disc; the two hills; and the faint rings laid over the whole screen.
struct SkyPhase {
  uint32_t bands[4];
  float ring_r[4];
  uint32_t ring_c[4];
  uint32_t hills[2];
  uint32_t overlay;
  float overlay_a[3];
  float centre[2];  // where the board has the disc; the panel moves it with the time of day
};

struct Paint {
  gfx::Color zone_w, zone_s;  // floor tiles
  Class cls;
  int mode;                   // 0 day, 1 evening, 2 night
  gfx::Color strip;
  gfx::Color fg, bg;          // icons and badges
};

gfx::Color colour(const Shape &s, const Paint &p);
// One shape in a given colour, its points moved by (dx, dy), through whatever clip the canvas has.
void draw_shape(gfx::Canvas &c, const Shape &s, gfx::Color col, float dx = 0, float dy = 0);
// Paints the group's shapes with their points moved by (dx, dy). Shapes marked clip are painted
// through status_zone, the others through outer (either may be null).
void draw(gfx::Canvas &c, const Group &g, const Paint &p, float dx = 0, float dy = 0,
          const gfx::RRect *outer = nullptr, const gfx::RRect *status_zone = nullptr);

extern const Group kUpTile, kDownTile, kOutsideTile, kSkyHills, kStripOut, kStripIn, kStripHolly;
extern const Group kBadgeOpen, kBadgeBusy, kBadgePrivate, kBadgeSleeping, kBadgeAway, kBadgeNoSignal;
extern const Group kPawIn, kPawOut, kGear, kCaret, kDropIn, kDropOut, kBell;
// Forecast icons (tools/forecast_icons.py): WeatherGate's Zambretti symbols, night versions, and
// the danger-heat sun in two parts.
extern const Group kFcSun, kFcMoon, kFcCloudSun, kFcCloudMoon, kFcCloud, kFcClearing, kFcClearingMoon;
extern const Group kFcRain, kFcHeavyRain, kFcSnow, kFcStorm, kFcHeatDisc, kFcHeatRays;
extern const SkyPhase kSky[6];
extern const float kSkyStars[12][3];
extern const uint32_t kStripDisc[6];
extern const float kStripDiscR;
extern const float kCrescent[2][4];
extern const uint32_t kCrescentColour;
extern const float kStripStars[5][3];

}  // namespace boardart
