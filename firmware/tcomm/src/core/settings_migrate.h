// Saved settings from an older firmware (board_storage.cpp keeps them in NVS as a blob with a
// version and a size). Plain C++ so the host tests can check that nothing Mike saved is lost.
//
// Layout history: 1-3 ended at screensaver_min's start (3: ringtones renumbered, handled in
// ringtone_from_saved); 4 added screensaver_min; 5 added screen_off_start_min and screen_off_end_min;
// 6 added the presence sensor's three fields and the notice mask. Each field was added last, so an
// old blob is the start of the current struct, and its size is the offset of the first field it
// didn't have, rounded up to the struct's alignment.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "settings.h"

namespace tc {

constexpr size_t settings_size_ending_at(size_t end) {
  return (end + alignof(Settings) - 1) / alignof(Settings) * alignof(Settings);
}
constexpr size_t kSettingsV3Size = settings_size_ending_at(offsetof(Settings, screensaver_min));
constexpr size_t kSettingsV4Size = settings_size_ending_at(offsetof(Settings, screen_off_start_min));
constexpr size_t kSettingsV5Size = settings_size_ending_at(offsetof(Settings, presence_fitted));
// Version 5 was saved when Settings was 104 bytes; if this ever fails, a field moved.
static_assert(kSettingsV5Size == 104, "the version 5 layout is 104 bytes");

// The size an older version saved, or 0 for a version this can't read.
inline size_t settings_old_size(uint16_t ver) {
  if (ver >= 1 && ver <= 3) return kSettingsV3Size;
  if (ver == 4) return kSettingsV4Size;
  if (ver == 5) return kSettingsV5Size;
  return 0;
}

// `data` is the blob without its 4-byte header and `size` the size its header gave. Fills `out`
// with every saved field as it was and the fields added since at their starting values (they may
// sit in the old layout's padding, so they're set, not left to the copy). False if the version or
// size doesn't fit.
inline bool settings_from_old(uint16_t ver, const uint8_t *data, size_t size, Settings &out) {
  const size_t want = settings_old_size(ver);
  if (!want || size != want) return false;
  Settings s;
  const Settings d;
  memcpy((void *)&s, data, want);
  if (ver <= 3) s.screensaver_min = d.screensaver_min;
  if (ver <= 4) {
    s.screen_off_start_min = d.screen_off_start_min;
    s.screen_off_end_min = d.screen_off_end_min;
  }
  s.presence_fitted = d.presence_fitted;
  s.presence_active_high = d.presence_active_high;
  s.presence_hold_s = d.presence_hold_s;
  s.notice_mask = d.notice_mask;
  out = s;
  return true;
}

}  // namespace tc
