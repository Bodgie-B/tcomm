// Stage 22's compile-time switch (INTEGRATION_PLAN.md section 6.1; Mike, 2026-11-18). On by default since
// the stage's last step (C7, 2026-11-19): the panel compiles screensaver/core with no defines, so there the
// new pieces are in the plans. The host builds pick it with the CMake option SS_S22 (default ON;
// build-s22-off keeps -DSS_S22=OFF as the proof that, without the pieces, every plan is what it was).
//
// What it gates: only what changes a plan. With it off the new majors weigh 0 (tai chi, the long shot),
// tai chi is never forced, the herd's share is 0 and the ripe mushroom patch never plays, so every
// existing pinned plan line is drawn exactly as before. It does NOT gate any struct, enum, hook or the
// reeds (which never touch a plan): the World's layout is the same either way, so a tool built by hand
// against the library cannot mismatch on it (unlike SS_XMAS).
#pragma once

#ifndef SS_S22
#define SS_S22 1
#endif

#include <stdint.h>

namespace ss {
// SS_S22 as the core library was built with (the tests ask this before checking pinned plan lines).
int ssS22Switch();

// The plan pieces of stage 22 (step C0, INTEGRATION_PLAN 3.1 point 9). Each plans only once its own
// serial step has landed (a named constant per piece in majors.cpp, flipped by that step: C2 tai chi,
// C3 the long shot, C4 the herd, C5c the mushroom), so a stub major is never planned into an empty
// beat. Memory::s22_preview lets a test plan as if a piece had landed (bit 7 set, then these bits);
// the panel and every run leave it 0. S22_FROG (2026-11-25): the frog he licks, added later behind the
// same switch (kFrogLanded in majors.cpp).
enum S22Piece : uint8_t { S22_TAICHI = 1, S22_LONGSHOT = 2, S22_MUSHROOM = 4, S22_HERD = 8, S22_FROG = 0x10 };
constexpr uint8_t S22_PREVIEW = 0x80;
constexpr uint8_t S22_ALL = S22_TAICHI | S22_LONGSHOT | S22_MUSHROOM | S22_HERD | S22_FROG;

// Whether `piece` plans: never with SS_S22 off (whatever the preview byte says, so the off build plans
// exactly as before stage 22); otherwise the preview byte when its bit 7 is set, else `landed`.
inline bool s22Plans(uint8_t piece, bool landed, uint8_t preview) {
  if (SS_S22 == 0) return false;
  if ((preview & S22_PREVIEW) != 0) return (preview & piece) != 0;
  return landed;
}
}  // namespace ss
