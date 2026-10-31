// Settings and state in NVS. Each blob carries its own layout version and size; anything that
// doesn't match is ignored and defaults are used. Bump a version whenever its struct changes, even
// if the size stays the same. Every field is range-checked after loading.
#include <Arduino.h>
#include <FFat.h>
#include <SD_MMC.h>
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include <Preferences.h>
#include <math.h>
#include "../core/synth.h"
#include "board.h"

namespace board {

static Preferences g_prefs;
static bool g_open = false;
static constexpr uint16_t kSettingsVersion = 3;  // 2, 3: ringtones renumbered (sci-fi, then Water drops removed)
static constexpr uint16_t kOwnVersion = 1;
static constexpr uint16_t kDogVersion = 3;  // 2: change counter added; 3: each dog on its own
static constexpr uint16_t kMailVersion = 1;

void storage_begin() { g_open = g_prefs.begin("tcomm", false); }

template <class T>
static bool load_blob(const char *key, uint16_t version, T &out) {
  if (!g_open) return false;
  uint8_t buf[sizeof(T) + 4];
  size_t n = g_prefs.getBytes(key, buf, sizeof(buf));
  if (n != sizeof(buf)) return false;
  uint16_t ver = (uint16_t)(buf[0] | (buf[1] << 8)), size = (uint16_t)(buf[2] | (buf[3] << 8));
  if (ver != version || size != sizeof(T)) return false;
  memcpy(&out, buf + 4, sizeof(T));
  return true;
}

template <class T>
static void save_blob(const char *key, uint16_t version, const T &v) {
  if (!g_open) return;
  uint8_t buf[sizeof(T) + 4];
  buf[0] = version & 0xFF;
  buf[1] = version >> 8;
  buf[2] = sizeof(T) & 0xFF;
  buf[3] = (sizeof(T) >> 8) & 0xFF;
  memcpy(buf + 4, &v, sizeof(T));
  g_prefs.putBytes(key, buf, sizeof(buf));
}

static bool settings_ok(const tc::Settings &s) {
  if ((uint8_t)s.floor > 1 || !tc::status_valid((uint8_t)s.timer_fallback_default) || (uint8_t)s.night_level > 2) return false;
  if (s.volume > 100 || s.quiet_volume_cap > 100 || s.soft_chime_drop > 100) return false;
  if (s.quiet_start_min >= 1440 || s.quiet_end_min >= 1440) return false;
  if (s.touch_wake_ms < 1000 || s.touch_wake_ms > 600000) return false;
  if (s.temp_offset_tenths_f < -200 || s.temp_offset_tenths_f > 200) return false;
  if (!(s.latitude >= -90 && s.latitude <= 90) || !(s.longitude >= -180 && s.longitude <= 180)) return false;  // also NaN
  if (s.outside_stale_min < 1 || s.outside_stale_min > 1440) return false;
  for (const tc::KnockRule &r : s.knock)
    if ((uint8_t)r.chime > 2) return false;
  return true;
}

bool load_settings(tc::Settings &s) {
  tc::Settings tmp;
  // Versions 1 and 2 had the same layout; only the ringtone numbers moved. A ringtone that has
  // been removed since it was saved becomes Westminster, and the other settings stay.
  uint16_t version = load_blob("settings", kSettingsVersion, tmp)
                         ? kSettingsVersion
                         : (load_blob("settings", 2, tmp) ? 2 : (load_blob("settings", 1, tmp) ? 1 : 0));
  if (!version) return false;
  tmp.ringtone = tc::ringtone_from_saved(version, tmp.ringtone);
  tmp.tz[sizeof(tmp.tz) - 1] = 0;
  if (!settings_ok(tmp)) return false;
  s = tmp;
  return true;
}
void save_settings(const tc::Settings &s) { save_blob("settings", kSettingsVersion, s); }

bool load_own(tc::OwnState &s) {
  tc::OwnState tmp;
  if (!load_blob("own", kOwnVersion, tmp)) return false;
  if (!tc::status_valid((uint8_t)tmp.status) || !tc::status_valid((uint8_t)tmp.fallback)) return false;
  s = tmp;
  return true;
}
void save_own(const tc::OwnState &s) { save_blob("own", kOwnVersion, s); }

bool load_dog(tc::DogState &d) {
  tc::DogState tmp;
  if (load_blob("dog", kDogVersion, tmp)) {
    for (const tc::DogOne &one : tmp.dog) {
      uint8_t b;
      memcpy(&b, &one.out, 1);
      if (b > 1) return false;
    }
    d = tmp;
    return true;
  }
  // Version 2: one dog at most, one counter. It becomes that dog's; both get the counter, so a
  // change from the other panel still compares the same way.
  struct V2 {
    tc::Dog out;
    uint32_t since;
    uint32_t ver;
  } old;
  if (!load_blob("dog", 2, old) || !tc::dog_valid((uint8_t)old.out) || old.out == tc::Dog::Both) return false;
  tmp = tc::DogState();
  for (int i = 0; i < 2; i++) tmp.dog[i].ver = old.ver;
  if (old.out != tc::Dog::None) tmp.dog[tc::dog_index(old.out)] = tc::DogOne{true, old.since, old.ver};
  d = tmp;
  return true;
}
void save_dog(const tc::DogState &d) { save_blob("dog", kDogVersion, d); }

bool load_mail(tc::MailState &m) {
  tc::MailState tmp;
  if (!load_blob("mail", kMailVersion, tmp)) return false;
  uint8_t b;
  memcpy(&b, &tmp.waiting, 1);
  if (b > 1) return false;
  m = tmp;
  return true;
}
void save_mail(const tc::MailState &m) { save_blob("mail", kMailVersion, m); }

// No saved quality means we don't know where the RTC's time came from: rank it lowest.
uint8_t load_clock_quality() {
  uint8_t q = g_open ? g_prefs.getUChar("clockq", tc::kClockFromPeer) : (uint8_t)tc::kClockFromPeer;
  return q > tc::kClockNetwork ? (uint8_t)tc::kClockFromPeer : q;
}
void save_clock_quality(uint8_t q) {
  if (g_open) g_prefs.putUChar("clockq", q);
}

// ---- The calendar's files: the SD card, else the FAT partition ----

static bool g_fat = false, g_sd = false;
static fs::FS *g_cal = nullptr;            // where the calendar's files are
static const char *const kDir = "/tcomm";
static const char *const kLegacy = "/events.bin";  // the FAT partition's calendar, 2026-10-25 morning

// A mounted file system's size and use. SD_MMC.totalBytes and usedBytes (core 3.2.0) measure
// FatFs drive 0, which is the FAT partition here (it mounts first), so ask ESP-IDF by mount point.
static void fs_size(const char *base, uint64_t &total, uint64_t &used) {
  uint64_t t = 0, f = 0;
  if (esp_vfs_fat_info(base, &t, &f) != ESP_OK) t = f = 0;
  total = t;
  used = t - f;
}

static void cal_path(char *out, size_t n, const char *name, bool tmp = false) {
  snprintf(out, n, "%s/%s.%s", kDir, name, tmp ? "tmp" : "bin");
}

static bool sd_mount(bool format) {
  expander_output(kExioSdCs, true);  // D3 high: the card starts up in SD mode, not SPI
  SD_MMC.setPins(12, 11, 13);        // CLK, CMD, D0: one-bit (hardware-notes.md)
  // Two files open at most (a file and its replacement).
  return SD_MMC.begin("/sd", true, format, SDMMC_FREQ_DEFAULT, 2);
}

static bool copy_file(fs::FS &from, const char *src, fs::FS &to, const char *dst) {
  File in = from.open(src, "r");
  if (!in) return false;
  File out = to.open(dst, "w");
  if (!out) return false;
  static uint8_t chunk[2048];
  bool ok = true;
  for (int n; ok && (n = in.read(chunk, sizeof(chunk))) > 0;) ok = out.write(chunk, n) == (size_t)n;
  in.close();
  out.close();
  return ok;
}

// The FAT partition's calendar files onto the card, where the card has none of that name.
static void copy_missing_to_sd() {
  if (!g_fat || !g_sd) return;
  if (!SD_MMC.exists(kDir)) SD_MMC.mkdir(kDir);
  int n = 0;
  char dst[48];
  File dir = FFat.open(kDir);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      char src[48];
      snprintf(src, sizeof(src), "%s/%s", kDir, f.name());
      snprintf(dst, sizeof(dst), "%s/%s", kDir, f.name());
      f.close();
      if (!SD_MMC.exists(dst) && copy_file(FFat, src, SD_MMC, dst)) n++;
    }
  }
  // The morning's file last, only if neither place has a newer calendar.
  cal_path(dst, sizeof(dst), "calendar");
  if (FFat.exists(kLegacy) && !SD_MMC.exists(dst) && copy_file(FFat, kLegacy, SD_MMC, dst)) n++;
  if (n) elog("calendar: %d files copied from the FAT partition to the SD card", n);
}

void calendar_fs_begin() {
  uint32_t t0 = millis();
  g_fat = FFat.begin(false, "/ffat", 2);
  if (!g_fat) {
    elog("calendar: no file system on the FAT partition, formatting it");
    g_fat = FFat.begin(true, "/ffat", 2);
    elog("calendar: FAT format %s in %lu ms", g_fat ? "done" : "FAILED", (unsigned long)(millis() - t0));
  }
  t0 = millis();
  g_sd = sd_mount(false);
  if (g_sd) {
    uint64_t total, used;
    fs_size("/sd", total, used);
    elog("calendar: SD card mounted in %lu ms, %lu MB, %lu MB used", (unsigned long)(millis() - t0),
         (unsigned long)(total >> 20), (unsigned long)(used >> 20));
  } else
    elog("calendar: no readable SD card (%lu ms): using the FAT partition", (unsigned long)(millis() - t0));
  copy_missing_to_sd();
  g_cal = g_sd ? (fs::FS *)&SD_MMC : g_fat ? (fs::FS *)&FFat : nullptr;
  if (g_cal && !g_cal->exists(kDir)) g_cal->mkdir(kDir);
}

uint8_t *calendar_read(const char *name, size_t &len) {
  len = 0;
  if (!g_cal) return nullptr;
  char p[48], t[48];
  cal_path(p, sizeof(p), name);
  cal_path(t, sizeof(t), name, true);
  if (!g_cal->exists(p) && g_cal->exists(t)) g_cal->rename(t, p);  // cut off between write and rename
  File f = g_cal->open(p, "r");
  if (!f && !strcmp(name, "calendar") && g_fat && FFat.exists(kLegacy)) f = FFat.open(kLegacy, "r");
  if (!f) return nullptr;
  size_t n = f.size();
  uint8_t *buf = (uint8_t *)heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bool ok = buf && f.read(buf, n) == n;
  f.close();
  if (!ok) {
    free(buf);
    elog("calendar: couldn't read %s", p);
    return nullptr;
  }
  len = n;
  return buf;
}

bool calendar_write(const char *name, const uint8_t *buf, size_t len) {
  if (!g_cal) return false;
  char p[48], t[48];
  cal_path(p, sizeof(p), name);
  cal_path(t, sizeof(t), name, true);
  File f = g_cal->open(t, "w");
  if (!f) return false;
  bool ok = f.write(buf, len) == len;
  f.close();
  if (!ok) return false;
  g_cal->remove(p);
  return g_cal->rename(t, p);
}

bool calendar_fs_info(CalendarFs &o) {
  o = CalendarFs{g_sd, g_fat, g_sd ? SD_MMC.cardSize() : 0, 0, 0, 0};
  if (!g_cal) return false;
  fs_size(g_sd ? "/sd" : "/ffat", o.total, o.used);
  File dir = g_cal->open(kDir);
  if (dir && dir.isDirectory())
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) o.files++;
  return true;
}

void sd_report() {
  if (!g_sd) {
    Serial.println("sd: no card mounted. Either there's no card, or it isn't FAT32 (exFAT can't be read here).");
    Serial.println("    \"sd format yes\" formats it as FAT32, erasing it, only if it still won't mount.");
    return;
  }
  sdcard_type_t t = SD_MMC.cardType();
  const char *kind = t == CARD_SDHC ? "SDHC/SDXC" : t == CARD_SD ? "SD" : t == CARD_MMC ? "MMC" : "unknown";
  CalendarFs i;
  calendar_fs_info(i);
  Serial.printf("sd: %s card, %llu MB; FAT file system %llu MB, %llu MB used; %d calendar files\n", kind,
                (unsigned long long)(i.card >> 20), (unsigned long long)(i.total >> 20),
                (unsigned long long)(i.used >> 20), i.files);
}

// FAT32 with 32 KB clusters, through ESP-IDF itself: SD_MMC.begin's own format (core 3.2.0) uses
// 512-byte clusters, which on a 64 GB card means 1 GB of FAT tables to write (minutes, with
// serial blocked meanwhile) and a slow card afterwards. With 32 KB it's about 16 MB.
static bool sd_format_fat32() {
  expander_output(kExioSdCs, true);
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = GPIO_NUM_12;
  slot.cmd = GPIO_NUM_11;
  slot.d0 = GPIO_NUM_13;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_vfs_fat_sdmmc_mount_config_t cfg = {};
  cfg.format_if_mount_failed = true;
  cfg.max_files = 2;
  cfg.allocation_unit_size = 32 * 1024;
  sdmmc_card_t *card = nullptr;
  esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdfmt", &host, &slot, &cfg, &card);
  if (err != ESP_OK) {
    Serial.printf("sd: format failed: %s\n", esp_err_to_name(err));
    return false;
  }
  esp_vfs_fat_sdcard_unmount("/sdfmt", card);
  return true;
}

bool sd_format_if_unreadable() {
  if (g_sd) {
    Serial.println("sd: the card is readable as it is, so it wasn't formatted");
    return true;
  }
  SD_MMC.end();
  uint32_t t0 = millis();
  Serial.println("sd: formatting the card as FAT32, 32 KB clusters (this erases it)...");
  g_sd = sd_format_fat32() && sd_mount(false);
  Serial.printf("sd: %s in %lu ms\n", g_sd ? "formatted and mounted" : "FAILED", (unsigned long)(millis() - t0));
  elog("calendar: SD card format %s in %lu ms", g_sd ? "done" : "FAILED", (unsigned long)(millis() - t0));
  if (!g_sd) return false;
  if (!SD_MMC.exists(kDir)) SD_MMC.mkdir(kDir);
  copy_missing_to_sd();
  g_cal = &SD_MMC;
  sd_report();
  return true;
}

uint8_t load_display_test() { return g_open ? g_prefs.getUChar("disptest2", 0xFF) : 0xFF; }
void save_display_test(uint8_t v) {
  if (g_open) g_prefs.putUChar("disptest2", v);
}

}  // namespace board
