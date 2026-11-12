// The presence sensor (Mike, 2026-11-11; docs/presence-sensor.md): one plain on/off sensor wired to
// the UPSTAIRS panel's isolated input DI0, at the place where the hallway opens to the kitchen and
// living room. Plain C++: the panel reads the pin and feeds PresenceFilter; the controller only sees
// the debounced yes or no (Platform::presence). The timings here are named so they are easy to change.
#pragma once
#include <stdint.h>

namespace tc {

constexpr uint32_t kPresencePollMs = 100;       // how often the loop task reads the input
constexpr uint32_t kAwayClearMs = 5 * 60000;    // "Away from desk" ends after this long with no activity
constexpr uint32_t kNobodyNearMs = 15 * 60000;  // nobody near the panel (activity or a touch) for this long
constexpr uint32_t kNightWakeMs = 30000;        // the night path keeps the screen lit this long after activity
constexpr uint32_t kPasserByShowMs = 3000;      // View::passer_by stays true this long
constexpr uint32_t kPasserByGapMs = 120000;     // and comes at most once in this long
constexpr uint32_t kNearNoteMs = 15000;         // the tenant's "No one is near the panel" shows this long at most
constexpr uint8_t kPresenceErrorsToAbsent = 30; // this many failed reads in a row (3 s) count as nobody there

// Debounce: the state changes only when two reads in a row agree. A failed read changes nothing
// (one bad I2C transfer must not look like a person), but a long run of them means the sensor can't
// be trusted, so the state falls back to "nobody".
class PresenceFilter {
 public:
  // pin_high: the level read from the input. active_high: which level means someone is there.
  void feed(bool read_ok, bool pin_high, bool active_high) {
    if (!read_ok) {
      if (errors_in_row_ < 255) errors_in_row_++;
      errors_++;
      if (errors_in_row_ >= kPresenceErrorsToAbsent) {
        state_ = false;
        have_last_ = false;
      }
      return;
    }
    errors_in_row_ = 0;
    level_ = pin_high;
    have_level_ = true;
    const bool active = pin_high == active_high;
    if (have_last_ && active == last_active_) state_ = active;
    last_active_ = active;
    have_last_ = true;
  }
  bool present() const { return state_; }
  bool have_level() const { return have_level_; }  // at least one good read
  bool level() const { return level_; }            // the last good read of the pin, high or low
  uint32_t errors() const { return errors_; }      // failed reads since start
  void reset() { *this = PresenceFilter(); }

 private:
  bool state_ = false;
  bool last_active_ = false, have_last_ = false;
  bool level_ = false, have_level_ = false;
  uint8_t errors_in_row_ = 0;
  uint32_t errors_ = 0;
};

}  // namespace tc
