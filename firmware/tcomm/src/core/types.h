// Shared types for the TComm core. Plain C++: no Arduino, ESP-IDF or LVGL headers anywhere in core/.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace tc {

// Monotonic milliseconds wrap after about 49 days. Always compare with these helpers, never with < or >.
inline int32_t ms_diff(uint32_t later, uint32_t earlier) { return (int32_t)(later - earlier); }
inline bool ms_reached(uint32_t now, uint32_t deadline) { return ms_diff(now, deadline) >= 0; }

enum class Floor : uint8_t { Upstairs = 0, Downstairs = 1 };
inline Floor other_floor(Floor f) { return f == Floor::Upstairs ? Floor::Downstairs : Floor::Upstairs; }

// A person's own status. "No signal" is not a status: it's a flag on the view of the other floor.
enum class Status : uint8_t { Open = 0, Busy = 1, Private = 2, Sleeping = 3, Away = 4 };
constexpr int kStatusCount = 5;
inline bool status_valid(uint8_t v) { return v < kStatusCount; }

// Which dogs are in the yard: bits, Lucy 1 and Copper 2, so both out is 3 (Mike, 2026-10-25: it
// happens now). Each is kept on its own (dog.h).
enum class Dog : uint8_t { None = 0, Lucy = 1, Copper = 2, Both = 3 };
inline bool dog_valid(uint8_t v) { return v <= 3; }
inline Dog own_dog(Floor f) { return f == Floor::Upstairs ? Dog::Lucy : Dog::Copper; }

// Palette and backlight level. Picked from the sky phase.
enum class Mode : uint8_t { Day = 0, Evening = 1, Night = 2 };

// Six sky phases from the design brief.
enum class Phase : uint8_t { Night = 0, Dawn = 1, Morning = 2, Midday = 3, Golden = 4, Dusk = 5 };
inline Mode mode_for(Phase p) {
  switch (p) {
    case Phase::Night: return Mode::Night;
    case Phase::Dawn:
    case Phase::Dusk: return Mode::Evening;
    default: return Mode::Day;
  }
}

// Sounds the panel plays. Synthesized at boot (core/synth.h).
enum class Sound : uint8_t { Chime = 0, SoftChime = 1, ReplyYes = 2, ReplyNo = 3, Count = 4 };

// Wall clock as the platform reports it. quality ranks where the time came from, so units
// copy the clock only from a better source and never ping-pong.
enum ClockQuality : uint8_t { kClockNone = 0, kClockFromPeer = 1, kClockManual = 2, kClockNetwork = 3 };
struct WallTime {
  bool valid = false;
  uint32_t utc = 0;  // seconds since 1970-01-01 UTC
  uint8_t quality = kClockNone;
};

// Forecast codes for the Outside tile: WeatherGate's eight Zambretti symbols (see zambretti.h),
// sent as the OUTSIDE message's forecast byte. Sun, CloudSun and Clearing show the moon at night.
//   1 sunny  2 partly_cloudy  3 cloudy  4 rain  5 snow  7 storm  8 clearing  9 heavy_rain
// 6 is the boards' clear night; the panel now draws sunny as the moon at night itself.
enum class Forecast : uint8_t {
  Unknown = 0, Sun = 1, CloudSun = 2, Cloud = 3, Rain = 4, Snow = 5, Moon = 6, Thunder = 7, Clearing = 8, HeavyRain = 9
};
inline bool forecast_valid(uint8_t v) { return v <= 9; }

}  // namespace tc
