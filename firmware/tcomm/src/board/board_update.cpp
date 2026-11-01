// Wi-Fi updates (docs/wifi-updates.md). The panel firmware runs from app0; the updater
// (updater) sits in app1, and this firmware only checks that it's there and restarts into
// it. The updater takes the new panel firmware over Wi-Fi, writes it into app0 and restarts into
// that. (The panel firmware's own Wi-Fi is only for the time after a power cut: board_radio.cpp.)
// Written and tested on the upstairs panel 2026-10-31.
#include <Arduino.h>
#include "board.h"
#include "esp_attr.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

namespace board {

// The app slot this firmware isn't running from: the updater's.
static const esp_partition_t *updater_slot() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *other = esp_ota_get_next_update_partition(running);
  return other == running ? nullptr : other;
}

bool updater_installed() {
  const esp_partition_t *p = updater_slot();
  esp_app_desc_t d;
  return p && esp_ota_get_partition_description(p, &d) == ESP_OK;  // a program's header, not empty flash
}

void restart_into_updater() {
  const esp_partition_t *p = updater_slot();
  // esp_ota_set_boot_partition checks the whole image first, so a damaged updater is refused here
  // rather than found by the bootloader.
  if (!p || esp_ota_set_boot_partition(p) != ESP_OK) {
    elog("updater: not installed, or damaged");
    return;
  }
  elog("updater: restarting into it");
  elog_flush();
  delay(100);
  esp_restart();
}

// Kept through a crash restart (not through a power cut, which clears it to garbage, hence the
// magic word).
RTC_NOINIT_ATTR static uint32_t g_guard_magic, g_guard_crashes;
static constexpr uint32_t kGuardMagic = 0x7C0A11EDu;

void crash_guard_begin() {
  // A crash is a panic or a watchdog the firmware itself trips. Not ESP_RST_WDT: esptool restarts
  // the board through the RTC watchdog after every USB upload, and that would count.
  esp_reset_reason_t r = esp_reset_reason();
  bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT;
  if (g_guard_magic != kGuardMagic || !crash) {
    g_guard_magic = kGuardMagic;
    g_guard_crashes = 0;
  }
  if (crash) g_guard_crashes = g_guard_crashes + 1;
  if (g_guard_crashes >= 3 && updater_installed()) {
    g_guard_crashes = 0;
    elog("updater: three crashes in a row");
    restart_into_updater();
  }
}

void crash_guard_healthy() { g_guard_crashes = 0; }

}  // namespace board
