// WeatherGate's Zambretti forecaster, as its ZAMBRETTI_REFERENCE.md (2026-10-24) describes it:
// letters A to Z, each with a text and one of eight symbols. WeatherGate sends the symbol as the
// OUTSIDE message's forecast code and its text as the phrase (after its own rain-to-snow swap, so
// the text it sends is the one to show). These tables are for a gateway that sends only a letter,
// for the demo and for the tests.
#pragma once
#include "types.h"

namespace tc {

// The symbol for a letter, as WeatherGate assigns it; Unknown for anything but A to Z.
Forecast zambretti_symbol(char letter);
// The letter's text, as WeatherGate words it ("Rain at Times, Worse Later"); "" if none.
const char *zambretti_text(char letter);
// WeatherGate's symbol keys ("sunny", "partly_cloudy", ... "storm") and back.
Forecast forecast_from_key(const char *key);
const char *forecast_key(Forecast f);  // "" for Unknown and Moon

}  // namespace tc
