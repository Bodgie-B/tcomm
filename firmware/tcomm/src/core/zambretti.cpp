#include "zambretti.h"
#include <string.h>

namespace tc {

namespace {

struct Letter {
  const char *text;
  Forecast symbol;
};

// ZAMBRETTI_REFERENCE.md section 2. S is in none of WeatherGate's trend tables yet, but it is
// defined there, so it is here.
const Letter kLetters[26] = {
    {"Settled Fine Weather", Forecast::Sun},                          // A
    {"Fine Weather", Forecast::Sun},                                  // B
    {"Fine, Becoming Less Settled", Forecast::CloudSun},              // C
    {"Fairly Fine, Showery Later", Forecast::CloudSun},               // D
    {"Showery, Becoming More Unsettled", Forecast::Rain},             // E
    {"Unsettled, Rain Later", Forecast::Rain},                        // F
    {"Rain at Times, Worse Later", Forecast::HeavyRain},              // G
    {"Rain at Times, Becoming Very Unsettled", Forecast::HeavyRain},  // H
    {"Very Unsettled, Rain", Forecast::HeavyRain},                    // I
    {"Settled Fine", Forecast::Sun},                                  // J
    {"Fine Weather", Forecast::Sun},                                  // K
    {"Fine, Possibly Showers", Forecast::CloudSun},                   // L
    {"Fairly Fine, Showers Likely", Forecast::CloudSun},              // M
    {"Showery, Bright Intervals", Forecast::Cloud},                   // N
    {"Changeable, Some Rain", Forecast::Rain},                        // O
    {"Unsettled, Rain at Times", Forecast::Rain},                     // P
    {"Rain at Frequent Intervals", Forecast::HeavyRain},              // Q
    {"Very Unsettled, Rain", Forecast::HeavyRain},                    // R
    {"Stormy, Much Rain", Forecast::Thunder},                         // S
    {"Settled Fine", Forecast::Sun},                                  // T
    {"Fine Weather", Forecast::Sun},                                  // U
    {"Becoming Fine", Forecast::Clearing},                            // V
    {"Fairly Fine, Improving", Forecast::Clearing},                   // W
    {"Fairly Fine, Possibly Showers", Forecast::CloudSun},            // X
    {"Showery Early, Improving", Forecast::Clearing},                 // Y
    {"Changeable, Mending", Forecast::Cloud},                         // Z
};

const struct {
  const char *key;
  Forecast f;
} kKeys[] = {
    {"sunny", Forecast::Sun},   {"partly_cloudy", Forecast::CloudSun}, {"cloudy", Forecast::Cloud},
    {"clearing", Forecast::Clearing}, {"rain", Forecast::Rain},        {"heavy_rain", Forecast::HeavyRain},
    {"snow", Forecast::Snow},   {"storm", Forecast::Thunder},
};

}  // namespace

Forecast zambretti_symbol(char letter) {
  if (letter < 'A' || letter > 'Z') return Forecast::Unknown;
  return kLetters[letter - 'A'].symbol;
}

const char *zambretti_text(char letter) {
  if (letter < 'A' || letter > 'Z') return "";
  return kLetters[letter - 'A'].text;
}

Forecast forecast_from_key(const char *key) {
  if (!key) return Forecast::Unknown;
  for (const auto &k : kKeys)
    if (!strcmp(key, k.key)) return k.f;
  return Forecast::Unknown;
}

const char *forecast_key(Forecast f) {
  for (const auto &k : kKeys)
    if (k.f == f) return k.key;
  return "";
}

}  // namespace tc
