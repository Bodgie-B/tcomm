// The house's weather as the scene's weather.
//
// The panel gets an OUTSIDE reading from the gateway (a forecast code from
// WeatherGate, a temperature, a humidity). The host fills an OutsideReading
// from it, calls sceneWeatherFromOutside, and copies the result into Inputs
// (ss_app.h): code -> weather_code, have_outside -> have_outside, overcast and
// fog -> weather.overcast and weather.fog. night_dim is not here: the host
// sets it from its own night rule. The tables behind the mapping are at the top
// of house_weather.cpp.
//
// The forecast's words decide first (Mike, 2026-11-11): the icon alone made it
// rain in the scene on a "rain later" forecast. The icon code is used only when
// the words are not one of the 26 Zambretti texts.
//
// Standalone: no panel headers. The OutsideReading fields say which firmware
// names they come from (firmware/tcomm/src).
#pragma once

namespace ss {

// WeatherGate's forecast codes as the OUTSIDE message carries them
// (docs/weathergate.md, "Forecast codes"; firmware core/types.h Forecast).
// 6 is the design boards' clear night and is never sent; any code not listed
// here is treated as none.
enum OutsideForecastCode {
  OFC_NONE = 0, OFC_SUNNY = 1, OFC_PARTLY_CLOUDY = 2, OFC_CLOUDY = 3, OFC_RAIN = 4, OFC_SNOW = 5, OFC_STORM = 7,
  OFC_CLEARING = 8, OFC_HEAVY_RAIN = 9
};

// Inputs::weather_code (ss_app.h; scene/weather.cpp kCodeKinds maps it to a kind).
enum SceneWeatherCode { SWC_CLEAR = 0, SWC_RAIN, SWC_SNOW, SWC_WIND, SWC_HEAT, SWC_FOG, SWC_CHILL, SWC_STORM };

// What the panel knows of the outside. There is no wind in an OUTSIDE reading.
struct OutsideReading {
  bool valid;         // OutsideView::valid (app/controller.h): a reading under Settings::outside_stale_min (15 min) old
  int forecast;       // the OUTSIDE forecast byte, OutsideData::code (OutsideForecastCode); 0 none
  // The forecast text as its Zambretti letter, 'A' to 'Z' (firmware core/zambretti.h
  // zambretti_letter(OutsideView::phrase)); 0 when the text is not one of the 26
  // ("No Pressure Data", a snow wording): then the forecast code decides, as before.
  char phrase;
  float temp_f;       // OutsideData::temp is tenths of a degree F; this is degrees F
  float humidity;     // OutsideData::hum, percent 0..100
  bool danger_heat;   // kOutsideDangerHeat (core/protocol.h): heat index 100 F or more. A dry sky plays heat
  bool danger_cold;   // kOutsideDangerCold: 20 F or less. Carried; the chill row (40 F) already covers it
};

struct SceneWeather {
  int code;           // Inputs::weather_code (SceneWeatherCode)
  float overcast;     // Inputs::weather.overcast, 0..1
  float fog;          // Inputs::weather.fog, 0..1
  bool have_outside;  // Inputs::have_outside: false means the core picks its own weather
};

// No reading (not valid): have_outside false, code 0, no tint, and the core
// picks its own weather. A reading with a known text plays the text's row; one
// with only a known forecast code plays the code's row; one with neither plays
// a clear sky (never the core's random weather: that snowed in July). For a dry
// row the temperature and humidity may then turn it to fog, heat or chill.
SceneWeather sceneWeatherFromOutside(const OutsideReading& r);

}  // namespace ss
