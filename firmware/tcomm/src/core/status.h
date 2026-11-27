// The panel owner's status and its optional timer. (No Undo: Mike, 2026-10-22, "it's easy enough
// to select a different status through the standard menu".)
#pragma once
#include "types.h"

namespace tc {

// Substatuses (Mike, 2026-11-26): a nuance of the current status, picked from the menu a long press
// on the own tile's status opens. One list for all of them; each status offers a few (sub_options).
// The numbers are on the wire (STATE) and in the saved state: never change one.
//   Courtesy  "Courtesy ring": the doorbell rings (Open: as before; Busy: a soft chime)
//   Free      "Free passage": Open, the bell is off and the ringer reads COME ON IN
//   Quiet     "Quiet passage": Open, Busy or Sleeping, the bell is off, COME IN QUIETLY
//   PreferNo  "Prefer no passage": Busy or Sleeping, the ringer reads ONLY IF NEEDED and can still ring
//   AllOut    "All out": Away, as Away always was (the ring is kept for later)
//   OneOut1/2 "<name> out": Away with one of the two people still home, so the bell rings as Open's
//   None      Private has no options
enum class Sub : uint8_t { Courtesy = 0, Free = 1, Quiet = 2, PreferNo = 3, AllOut = 4, OneOut1 = 5, OneOut2 = 6, None = 7 };
constexpr int kSubMax = 3;  // the most options one status has

// Mike, 2026-11-26: the defaults. Open and Busy keep today's doorbell (Courtesy ring); Sleeping
// starts on Quiet passage; Away on All out. Picking a status always comes back to its default.
inline Sub default_sub(Status s) {
  switch (s) {
    case Status::Open:
    case Status::Busy: return Sub::Courtesy;
    case Status::Sleeping: return Sub::Quiet;
    case Status::Away: return Sub::AllOut;
    default: return Sub::None;
  }
}
// The options a status offers, in the menu's order (Mike's order). Returns how many (0 for Private).
// Inline: the gateway builds the panels' protocol.cpp, which checks STATE's byte, without this file's .cpp.
inline int sub_options(Status s, Sub out[kSubMax]) {
  int n = 0;
  switch (s) {
    case Status::Open: out[n++] = Sub::Free; out[n++] = Sub::Courtesy; out[n++] = Sub::Quiet; break;
    case Status::Busy: out[n++] = Sub::Quiet; out[n++] = Sub::Courtesy; out[n++] = Sub::PreferNo; break;
    case Status::Sleeping: out[n++] = Sub::Quiet; out[n++] = Sub::PreferNo; break;
    case Status::Away: out[n++] = Sub::AllOut; out[n++] = Sub::OneOut1; out[n++] = Sub::OneOut2; break;
    default: break;
  }
  return n;
}
// v is one of s's options (Private: only None).
inline bool sub_valid(Status s, uint8_t v) {
  if (s == Status::Private) return v == (uint8_t)Sub::None;
  Sub o[kSubMax];
  int n = sub_options(s, o);
  for (int i = 0; i < n; i++)
    if ((uint8_t)o[i] == v) return true;
  return false;
}

// The two people of each floor for Away's "<name> out" (Mike, 2026-11-26). The names live in the
// untracked secrets.h (TCOMM_AWAY_UP_1, TCOMM_AWAY_UP_2, TCOMM_AWAY_DOWN_1, TCOMM_AWAY_DOWN_2) and
// tcomm.ino puts them here at start-up; until then, and in the tests and the simulator, they are
// "Person 1" and "Person 2" upstairs, "Person 3" and "Person 4" downstairs. Cut to kAwayNameMax.
constexpr size_t kAwayNameMax = 15;
void set_away_name(Floor f, int i, const char *name);  // i 0 or 1
const char *away_name(Floor f, int i);
// The option's name as the menu and the tile line show it: "Free passage", "Person 1 out". f is the
// floor whose status it is (the Away names are that floor's).
void sub_label(char *out, size_t n, Sub s, Floor f);
// One line under the name in the menu: what the option does to a ring.
const char *sub_effect(Status st, Sub s);

struct OwnState {
  Status status = Status::Open;
  uint32_t since = 0;   // UTC when this status began; 0 = unknown (clock wasn't set)
  uint32_t until = 0;   // UTC when the timer ends; 0 = no timer
  Status fallback = Status::Busy;
  // Mike, 2026-11-26. In the byte after fallback, which was padding, so the saved size is the same.
  Sub sub = Sub::Courtesy;
};

// The saved own state (board_storage.cpp): version 1 had no substatus; it was padding there, so its
// bytes mean nothing. The same 16 bytes either way.
struct OwnStateV1 {
  Status status;
  uint32_t since, until;
  Status fallback;
};
static_assert(sizeof(OwnStateV1) == 16 && sizeof(OwnState) == 16, "the saved own state is 16 bytes");
constexpr uint16_t kOwnVersion = 2;  // 2: substatus added (Mike, 2026-11-26)
// `data` is the blob without its header, `size` the size its header gave. Version 1 gets the
// status's default substatus. False when the version, size or values don't fit.
bool own_from_saved(uint16_t ver, const uint8_t *data, size_t size, OwnState &out);

class StatusModel {
 public:
  void restore(const OwnState &s) {
    cur_ = s;
    if (!sub_valid(cur_.status, (uint8_t)cur_.sub)) cur_.sub = default_sub(cur_.status);
    since_pending_ = false;
  }
  const OwnState &state() const { return cur_; }

  // Any change made by the owner. Choosing a new status cancels a running timer, and any choice
  // puts the substatus back to that status's default (Mike, 2026-11-26).
  // Returns false if nothing changed (same status, its default substatus, no timer running).
  bool choose(Status s, const WallTime &now, uint32_t mono_ms);
  // Start a timed status. Needs a valid clock: the expiry has to survive a reboot.
  bool choose_timed(Status s, uint32_t duration_s, Status fallback, const WallTime &now);
  // A substatus of the current status, from its menu. A running timer keeps running. The tile's
  // "since" starts again: the line reads "<option> since 4:10 PM". False if it isn't one of the
  // status's options or is already chosen.
  bool choose_sub(Sub s, const WallTime &now, uint32_t mono_ms);

  // Applies an expired timer: status becomes the fallback, starting at the expiry time, with its
  // default substatus. Does nothing to the timer while the clock is unknown. Returns true if the
  // status changed.
  bool tick(const WallTime &now);

  // The clock became valid: a status chosen while it was unknown gets a start time, back-dated by
  // the time that has passed since it was chosen. Returns true if the start time changed.
  bool clock_became_valid(uint32_t utc, uint32_t mono_ms);

 private:
  void restart_since(const WallTime &now, uint32_t mono_ms);
  OwnState cur_;
  bool since_pending_ = false;  // chosen with no clock; since_mono_ is when
  uint32_t since_mono_ = 0;
};

}  // namespace tc
