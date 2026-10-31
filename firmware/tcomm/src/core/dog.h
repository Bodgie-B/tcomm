// Which dogs are in the yard: Lucy (the upstairs panel's dog) and Copper (downstairs), each on its
// own, since both are sometimes out at once (Mike, 2026-10-25). Later change wins, dog by dog.
//
// "Later" is decided by a change counter, not by the clock: Mike's boards have no coin cell, so
// after a power cut both clocks may be unset, and two taps can land in the same second. Every
// change made here, or arriving from LucyGate, takes that dog's counter one past the highest seen.
#pragma once
#include "types.h"

namespace tc {

struct DogOne {
  bool out = false;
  uint32_t since = 0;  // UTC of its last change, for display; 0 = unknown
  uint32_t ver = 0;    // its change counter
};

struct DogState {
  DogOne dog[2];  // [0] Lucy, [1] Copper (dog_index)
  Dog out() const { return (Dog)((dog[0].out ? 1 : 0) | (dog[1].out ? 2 : 0)); }  // None, Lucy, Copper or Both
};

inline int dog_index(Dog d) { return d == Dog::Copper ? 1 : 0; }
inline bool operator==(const DogOne &a, const DogOne &b) { return a.out == b.out && a.since == b.since && a.ver == b.ver; }

class DogModel {
 public:
  void restore(const DogState &s) { s_ = s; }
  const DogState &state() const { return s_; }

  // The owner tapped their dog button: their dog goes out, or comes back in. The other dog
  // doesn't matter: both may be out.
  bool tap(Floor me, const WallTime &now);
  // A DOG message from the other panel: for each dog the higher counter wins; on a tie (both
  // panels changed the same dog at once) the later time, then "out", so both settle the same.
  // Returns true if anything changed here.
  bool apply_remote(const DogState &in);
  // A LUCY message from the LucyGate gateway, treated as a fresh press of Lucy's button. It never
  // touches Copper. since = the gateway's time, or our own clock when the gateway sent 0.
  bool apply_lucy(bool out, uint32_t since);

 private:
  DogState s_;
};

}  // namespace tc
