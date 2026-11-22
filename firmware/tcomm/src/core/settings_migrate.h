// Saved settings from an older firmware (board_storage.cpp keeps them in NVS as a blob with a
// version and a size). Plain C++ so the host tests can check that nothing Mike saved is lost.
//
// Layout history: 1-3 ended at screensaver_min's start (3: ringtones renumbered, handled in
// ringtone_from_saved); 4 added screensaver_min; 5 added screen_off_start_min and screen_off_end_min;
// 6 added the presence sensor's three fields and the notice mask; 7 has the same layout as 6, and
// a version 6 mask gains the mail and ring kinds once (Mike, 2026-11-15: they push the phones); 8 has
// the same layout again, and a version 6 or 7 mask gains the mild arrow and note kinds once (Mike,
// 2026-11-18: the tenant's mild requests and notes push his phone). Each
// field was added last, so an old blob is the start of the current struct, and its size is the
// offset of the first field it didn't have, rounded up to the struct's alignment.
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
constexpr size_t kSettingsV6Size = settings_size_ending_at(offsetof(Settings, notice_mask) + 1);
// Version 5 was saved when Settings was 104 bytes; if this ever fails, a field moved.
static_assert(kSettingsV5Size == 104, "the version 5 layout is 104 bytes");
static_assert(kSettingsV6Size == 112, "the version 6 layout is 112 bytes");
// The kinds a version 6 mask gains in version 7: Mail (bit 0x08) and Ring (bit 0x80), protocol.h's
// NoticeKind 4 and 8 (settings.h can't include protocol.h; checked in the host tests).
constexpr uint8_t kNoticeMaskAddedV7 = 0x08 | 0x80;
// Version 7 has the version 6 layout, so it is the same size.
constexpr size_t kSettingsV7Size = kSettingsV6Size;
// The kinds a version 7 mask gains in version 8: ArrowMild (bit 0x04) and Note (bit 0x40), NoticeKind
// 3 and 7. A mask that has the bit switched off by hand in version 8 is not touched again.
constexpr uint8_t kNoticeMaskAddedV8 = 0x04 | 0x40;

// The size an older version saved, or 0 for a version this can't read.
inline size_t settings_old_size(uint16_t ver) {
  if (ver >= 1 && ver <= 3) return kSettingsV3Size;
  if (ver == 4) return kSettingsV4Size;
  if (ver == 5) return kSettingsV5Size;
  if (ver == 6) return kSettingsV6Size;
  if (ver == 7) return kSettingsV7Size;
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
  if (ver == 6 || ver == 7) {  // everything as saved; the mask gains the phones' kinds, once each
    if (ver == 6) s.notice_mask = (uint8_t)(s.notice_mask | kNoticeMaskAddedV7);
    s.notice_mask = (uint8_t)(s.notice_mask | kNoticeMaskAddedV8);
    out = s;
    return true;
  }
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
