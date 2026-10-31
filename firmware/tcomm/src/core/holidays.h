// US federal holidays (Mike, 2026-10-25), worked out for any year: the calendar shows them in
// green, and the date strip shows a message with an icon on the day. On the real date, not the
// observed weekday when it falls on a weekend.
#pragma once
#include <stdint.h>

namespace tc {

// The date strip's icons for holidays, birthdays and anniversaries.
enum class DayIcon : uint8_t { None, Holly, Fireworks, Flag, Dove, Tools, Ship, Leaf, Cake, Rings };

struct Holiday {
  const char *name;     // "Thanksgiving Day": the Day view
  const char *short_name;  // "Thanksgiving": the Month and Week views
  const char *message;  // "Happy Thanksgiving": the date strip
  DayIcon icon;
};

// The federal holiday on a local day (days since 1970-01-01), or null.
const Holiday *federal_holiday(int32_t day);

}  // namespace tc
