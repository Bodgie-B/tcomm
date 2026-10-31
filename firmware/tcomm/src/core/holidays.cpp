#include "holidays.h"
#include "civil.h"

namespace tc {

namespace {

// 5 U.S.C. 6103(a). Washington's Birthday is its legal name; the calendar uses the common one.
const Holiday kNewYear = {"New Year's Day", "New Year's", "Happy New Year", DayIcon::Fireworks};
const Holiday kMlk = {"Martin Luther King Jr. Day", "MLK Day", "Martin Luther King Jr. Day", DayIcon::Dove};
const Holiday kPresidents = {"Presidents' Day", "Presidents'", "Presidents' Day", DayIcon::Washington};
const Holiday kMemorial = {"Memorial Day", "Memorial", "Memorial Day", DayIcon::FoldedFlag};
const Holiday kJuneteenth = {"Juneteenth", "Juneteenth", "Juneteenth", DayIcon::JuneteenthFlag};
const Holiday kIndependence = {"Independence Day", "July 4th", "Happy 4th of July", DayIcon::Flag};
const Holiday kLabor = {"Labor Day", "Labor Day", "Happy Labor Day", DayIcon::Labor};
const Holiday kColumbus = {"Columbus Day", "Columbus", "Columbus Day", DayIcon::Ship};
const Holiday kVeterans = {"Veterans Day", "Veterans", "Veterans Day", DayIcon::Pledge};
const Holiday kThanksgiving = {"Thanksgiving Day", "Thanksgiving", "Happy Thanksgiving", DayIcon::Leaf};
const Holiday kChristmas = {"Christmas Day", "Christmas", "Merry Christmas", DayIcon::Tree};

}  // namespace

const Holiday *federal_holiday(int32_t day) {
  Civil c = civil_from_seconds((int64_t)day * 86400);
  const int m = c.month, d = c.day, wd = c.weekday;  // 0 = Sunday
  const int nth = (d - 1) / 7 + 1;                    // which of its weekday in the month
  if (m == 1 && d == 1) return &kNewYear;
  if (m == 1 && wd == 1 && nth == 3) return &kMlk;
  if (m == 2 && wd == 1 && nth == 3) return &kPresidents;
  if (m == 5 && wd == 1 && d >= 25) return &kMemorial;  // the last Monday
  if (m == 6 && d == 19 && c.year >= 2021) return &kJuneteenth;
  if (m == 7 && d == 4) return &kIndependence;
  if (m == 9 && wd == 1 && nth == 1) return &kLabor;
  if (m == 10 && wd == 1 && nth == 2) return &kColumbus;
  if (m == 11 && d == 11) return &kVeterans;
  if (m == 11 && wd == 4 && nth == 4) return &kThanksgiving;
  if (m == 12 && d == 25) return &kChristmas;
  return nullptr;
}

}  // namespace tc
