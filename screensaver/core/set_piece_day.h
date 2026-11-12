// Once a day for each set piece (the Lookout at sunset, the sunrise overlook), for the host.
//
// The host keeps one SetPieceDay per set piece. While the clock is in that set piece's window it
// asks for it (setPieceWanted) until one has been SHOWN today; a set piece counts as shown only once
// its key moment has played (ss_app.h appSetPieceShown: the sun down, or the sun up over the ridge).
// So a touch in the first seconds of one (the screensaver stops) leaves the day's showing unused, and
// the next start in the window plays it again from the beginning. One forced by hand for testing
// never counts.
//
// Header only and plain C++, so the panel's host and the PC tests share the same rule
// (tests/test_sunrise.cpp checks it).
#pragma once

#include <stdint.h>

namespace ss {

struct SetPieceDay {
  int32_t shown_day;   // the local day number it was last shown on; -1 never
};

inline void setPieceReset(SetPieceDay& d) { d.shown_day = -1; }

// Whether the next loop should play it: inside its window, on a known day, not yet shown today.
inline bool setPieceWanted(const SetPieceDay& d, int32_t today, bool in_window) {
  return in_window && today >= 0 && d.shown_day != today;
}

// Call after the ticks of each frame. `running`: this loop is that set piece; `key_played`:
// appSetPieceShown; `forced`: played by hand (serial), which never counts. True the first time it
// counts today (the host logs it).
inline bool setPieceNote(SetPieceDay& d, int32_t today, bool running, bool key_played, bool forced) {
  if (!running || !key_played || forced || today < 0 || d.shown_day == today) return false;
  d.shown_day = today;
  return true;
}

}  // namespace ss
