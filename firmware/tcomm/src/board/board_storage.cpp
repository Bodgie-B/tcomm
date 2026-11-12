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
#include <stddef.h>
#include "../core/settings_migrate.h"
#include "../core/synth.h"
#include "../core/comfort.h"
#include "../core/events.h"
#include "board.h"

namespace board {

static Preferences g_prefs;
static bool g_open = false;
// 2, 3: ringtones renumbered (sci-fi, then Water drops removed); 4: screensaver_min added; 5: the screen's
// dark hours (screen_off_start_min, screen_off_end_min) added; 6: the presence sensor's three fields and the
// notice mask added (Mike, 2026-11-11). The older layouts are read by tc::settings_from_old
// (core/settings_migrate.h, checked by the host tests), so an update keeps everything saved.
static constexpr uint16_t kSettingsVersion = 6;
static constexpr uint16_t kOwnVersion = 1;
static constexpr uint16_t kDogVersion = 3;  // 2: change counter added; 3: each dog on its own
static constexpr uint16_t kMailVersion = 1;
static constexpr uint16_t kComfortVersion = 1;
static constexpr uint16_t kRequestsVersion = 1;
static constexpr uint16_t kComfortPendingVersion = 1;
static constexpr uint16_t kNotesVersion = 1;

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
  if (s.screensaver_min < tc::kScreensaverMinLeast || s.screensaver_min > tc::kScreensaverMinMost) return false;
  if (s.screen_off_start_min >= 1440 || s.screen_off_end_min >= 1440) return false;
  if (s.presence_hold_s < tc::kPresenceHoldLeast || s.presence_hold_s > tc::kPresenceHoldMost) return false;
  if (s.notice_mask > 0x7F) return false;
  for (const tc::KnockRule &r : s.knock)
    if ((uint8_t)r.chime > 2) return false;
  return true;
}

// Settings saved by an older version: the same bytes as far as that layout went; the fields added
// since start at their defaults (tc::settings_from_old does the copying and the filling). Returns the
// version they were saved with, or 0.
static uint16_t load_settings_old(tc::Settings &out) {
  if (!g_open) return 0;
  const size_t len = g_prefs.getBytesLength("settings");
  if (len < 4 || len > sizeof(tc::Settings) + 4) return 0;
  uint8_t buf[sizeof(tc::Settings) + 4];
  if (g_prefs.getBytes("settings", buf, len) != len) return 0;
  const uint16_t ver = (uint16_t)(buf[0] | (buf[1] << 8)), size = (uint16_t)(buf[2] | (buf[3] << 8));
  if (len != (size_t)size + 4) return 0;
  return tc::settings_from_old(ver, buf + 4, size, out) ? ver : 0;
}

bool load_settings(tc::Settings &s) {
  tc::Settings tmp;
  // Versions 1 and 2 had the same layout; only the ringtone numbers moved. A ringtone that has
  // been removed since it was saved becomes Westminster, and the other settings stay. Versions 4,
  // 5 and 6 added fields at the end.
  uint16_t version = load_blob("settings", kSettingsVersion, tmp) ? kSettingsVersion : load_settings_old(tmp);
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

bool load_comfort_count(uint32_t &n) {
  uint32_t tmp;
  if (!load_blob("comfort", kComfortVersion, tmp)) return false;
  n = tmp;
  return true;
}
void save_comfort_count(uint32_t n) { save_blob("comfort", kComfortVersion, n); }

// The comfort answers and removals still waiting for upstairs' ACK (Mike, 2026-11-07: a restart
// downstairs must lose neither): a count and up to kComfortPendingMax records, in one blob.
struct ComfortPendingBlob {
  uint32_t n;
  tc::ComfortPending p[tc::kComfortPendingMax];
};

bool load_comfort_pending(tc::ComfortPending *out, int max, int &n) {
  n = 0;
  static ComfortPendingBlob tmp;  // too big to be worth the loop task's stack; one caller, at start-up
  if (!out || max <= 0 || !load_blob("cpend", kComfortPendingVersion, tmp)) return false;
  if (tmp.n > (uint32_t)tc::kComfortPendingMax || tmp.n > (uint32_t)max) return false;
  for (uint32_t i = 0; i < tmp.n; i++) {
    const tc::ComfortPending &r = tmp.p[i];
    if (r.del > 1) return false;
    if (r.del == 0 && !tc::feel_valid(r.a.feel)) return false;
  }
  for (uint32_t i = 0; i < tmp.n; i++) out[i] = tmp.p[i];
  n = (int)tmp.n;
  return true;
}
void save_comfort_pending(const tc::ComfortPending *p, int n) {
  static ComfortPendingBlob blob;  // zeroed, so the unused records (and padding) are always the same bytes
  memset(&blob, 0, sizeof(blob));
  if (!p || n < 0) n = 0;
  if (n > tc::kComfortPendingMax) n = tc::kComfortPendingMax;
  blob.n = (uint32_t)n;
  for (int i = 0; i < n; i++) blob.p[i] = p[i];
  save_blob("cpend", kComfortPendingVersion, blob);
}

// The arrows (requests.h, docs/temperature-requests.md). Anything this build can't check is ignored,
// as the other blobs are: a held arrow must be a real one (never None) from a known source.
bool load_requests(tc::RequestState &s) {
  tc::RequestState tmp;
  if (!load_blob("requests", kRequestsVersion, tmp)) return false;
  if (tmp.n > tc::kMaxArrows) return false;
  for (int i = 0; i < tmp.n; i++)
    if (!tc::ask_valid(tmp.a[i].ask) || tmp.a[i].ask == (uint8_t)tc::Ask::None || !tc::req_source_valid(tmp.a[i].source))
      return false;
  uint8_t b;
  memcpy(&b, &tmp.acted_dismissed, 1);  // a bool: read as a byte, so a bad value isn't taken as one
  if (b > 1) return false;
  s = tmp;
  return true;
}
void save_requests(const tc::RequestState &s) { save_blob("requests", kRequestsVersion, s); }

// The tile messages (note.h, Mike 2026-11-07): both floors in one blob, so a restart keeps each
// floor's message on both panels. Text must end with a NUL inside its buffer, or nothing is used.
struct NotesBlob {
  tc::NoteState n[2];
};

bool load_notes(tc::NoteState *notes2) {
  NotesBlob tmp;
  if (!load_blob("notes", kNotesVersion, tmp)) return false;
  for (int i = 0; i < 2; i++) {
    bool nul = false;
    for (int j = 0; j <= tc::kNoteTextMax && !nul; j++) nul = tmp.n[i].text[j] == 0;
    if (!nul) return false;
  }
  for (int i = 0; i < 2; i++) notes2[i] = tmp.n[i];
  return true;
}

void save_notes(const tc::NoteState *notes2) {
  NotesBlob blob;
  memset(&blob, 0, sizeof(blob));  // zeroed first, so the padding is always the same bytes
  blob.n[0] = notes2[0];
  blob.n[1] = notes2[1];
  save_blob("notes", kNotesVersion, blob);
}

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

// Running on the FAT partition because the card was unreadable at start-up or failed since. While
// that lasts, a marker file on the FAT partition says its files hold changes the card hasn't seen:
// the next start-up with a readable card merges them in (merge_fat_into_sd) and only then removes it.
static const char *const kMarkerName = "fallback.dirty";
static const char *const kMarker = "/tcomm/fallback.dirty";
static bool g_on_fallback = false;
// The marker lists, one name a line, the files written on the FAT partition in this run (each once,
// the first time): only those are merged onto the card. "*" means the list overflowed: all of them.
static constexpr int kListedMax = 48;
static char g_listed[kListedMax][16];
static int g_nlisted = 0;
static bool g_list_overflow = false;
static uint32_t g_write_failures = 0;      // failed tries to write (a retry that worked still counts)
static bool g_remounted = false;
static uint32_t g_last_remount = 0;        // millis() of the last remount try, when g_remounted
static constexpr uint32_t kRemountGapMs = 60000;
static volatile int g_test_fail = 0;       // serial test: 1 the card fails for good, 2 once, then it works

bool storage_on_fallback() { return g_on_fallback; }
uint32_t storage_write_failures() { return g_write_failures; }
void storage_test_fail(int mode) { g_test_fail = mode; }

// Who is using the files right now. A remount waits until nobody is, and nobody starts meanwhile,
// so no file is open on a card that is being unmounted. (Reads, writes and the card's size all
// count; the files lock in device_platform.cpp is a different thing and is never taken in here.)
static portMUX_TYPE g_op_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int g_active = 0;
static volatile bool g_remounting = false;

static void op_enter() {
  for (;;) {
    bool ok;
    taskENTER_CRITICAL(&g_op_mux);
    ok = !g_remounting;
    if (ok) g_active = g_active + 1;
    taskEXIT_CRITICAL(&g_op_mux);
    if (ok) return;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void op_exit() {
  taskENTER_CRITICAL(&g_op_mux);
  g_active = g_active - 1;
  taskEXIT_CRITICAL(&g_op_mux);
}

struct OpGuard {
  OpGuard() { op_enter(); }
  ~OpGuard() { op_exit(); }
};

// A mounted file system's size and use. SD_MMC.totalBytes and usedBytes (core 3.2.0) measure
// FatFs drive 0, which is the FAT partition here (it mounts first), so ask ESP-IDF by mount point.
static void fs_size(const char *base, uint64_t &total, uint64_t &used) {
  uint64_t t = 0, f = 0;
  if (esp_vfs_fat_info(base, &t, &f) != ESP_OK) t = f = 0;
  total = t;
  used = t - f;
}

static void cal_path(char *out, size_t n, const char *name, bool tmp = false) {
  // A name with a dot ("comfort.csv", Mike, 2026-11-06) is the whole file name.
  if (strchr(name, '.')) snprintf(out, n, "%s/%s%s", kDir, name, tmp ? ".tmp" : "");
  else snprintf(out, n, "%s/%s.%s", kDir, name, tmp ? "tmp" : "bin");
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
      bool marker = !strcmp(f.name(), kMarkerName);  // says something about the FAT partition only
      f.close();
      if (!marker && !SD_MMC.exists(dst) && copy_file(FFat, src, SD_MMC, dst)) n++;
    }
  }
  // The morning's file last, only if neither place has a newer calendar.
  cal_path(dst, sizeof(dst), "calendar");
  if (FFat.exists(kLegacy) && !SD_MMC.exists(dst) && copy_file(FFat, kLegacy, SD_MMC, dst)) n++;
  if (n) elog("calendar: %d files copied from the FAT partition to the SD card", n);
}

// The calendar file format ends with a checksum (events.cpp: FNV-1a of everything before it, four
// bytes, little-endian, after the "TCEV" tag). A binary file is complete when that adds up, which
// a file cut short by a power cut never does. Streamed, so nothing is held whole.
static bool tcev_complete(fs::FS &fs, const char *path) {
  File f = fs.open(path, "r");
  if (!f) return false;
  size_t size = f.size();
  if (size < 16) {
    f.close();
    return false;
  }
  const size_t body = size - 4;
  uint32_t h = 2166136261u, stored = 0;
  bool magic = true;
  uint8_t c[256];
  size_t pos = 0;
  while (pos < size) {
    int n = f.read(c, sizeof(c));
    if (n <= 0) break;
    for (int i = 0; i < n && pos < size; i++, pos++) {
      if (pos < 4 && c[i] != (uint8_t) "TCEV"[pos]) magic = false;
      if (pos < body) h = (h ^ c[i]) * 16777619u;
      else stored |= (uint32_t)c[i] << (8 * (pos - body));
    }
  }
  f.close();
  return pos == size && magic && h == stored;
}

// A power cut between the remove and the rename leaves only the ".tmp", which then holds the one
// copy: it becomes the file again before anything reads, appends to or edits it. A binary file's
// ".tmp" counts only when it is complete: if the real file never existed, a ".tmp" that was still
// being written is half a file, and is left alone (the next write overwrites it).
static void recover_tmp(fs::FS &fs, const char *p, const char *t, bool binary) {
  if (fs.exists(p) || !fs.exists(t)) return;
  if (binary && !tcev_complete(fs, t)) {
    elog("calendar: %s is unfinished and %s doesn't exist: not used", t, p);
    return;
  }
  fs.rename(t, p);
}

static bool is_binary_name(const char *name) { return !strchr(name, '.'); }
static bool is_csv_name(const char *name) {
  size_t n = strlen(name);
  return n > 4 && !strcmp(name + n - 4, ".csv");
}

// ---- Merging the FAT partition's changes onto the card ----

// Copies, and checks the copy is as long as the original. The caller renames it into place.
static bool copy_verified(fs::FS &from, const char *src, fs::FS &to, const char *dst) {
  size_t want = 0;
  {
    File in = from.open(src, "r");
    if (!in) return false;
    want = in.size();
    in.close();
  }
  if (!copy_file(from, src, to, dst)) return false;
  File out = to.open(dst, "r");
  if (!out) return false;
  size_t got = out.size();
  out.close();
  return got == want;
}

static uint8_t g_mbuf[512], g_mbuf2[512];  // start-up only: one merge at a time

static bool same_file(fs::FS &a, const char *pa, fs::FS &b, const char *pb) {
  File fa = a.open(pa, "r");
  File fb = b.open(pb, "r");
  if (!fa || !fb || fa.size() != fb.size()) return false;
  bool same = true;
  for (int n; same && (n = fa.read(g_mbuf, sizeof(g_mbuf))) > 0;)
    same = fb.read(g_mbuf2, n) == (size_t)n && !memcmp(g_mbuf, g_mbuf2, (size_t)n);
  fa.close();
  fb.close();
  return same;
}

// The first free name.bak, name.bak1 ... name.bak9 for the file about to lose to a newer one. An
// older copy is never replaced while a free name is left; when all ten are taken the last goes.
static void bak_slot(fs::FS &fs, const char *dst, char *out, size_t cap) {
  for (int i = 0; i < 10; i++) {
    if (i == 0) snprintf(out, cap, "%s.bak", dst);
    else snprintf(out, cap, "%s.bak%d", dst, i);
    if (!fs.exists(out)) return;
  }
  fs.remove(out);  // out is name.bak9
}

// Lines are compared whole, by a 64-bit hash of their bytes (a trailing CR left out), so neither
// file is ever held. Calls on_line(number, hash, complete) for each line; a last line with no line
// end is incomplete. False if the file can't be opened.
static constexpr uint64_t kHashInit = 14695981039346656037ULL;
template <class F>
static bool each_line(fs::FS &fs, const char *path, F on_line) {
  File f = fs.open(path, "r");
  if (!f) return false;
  uint64_t h = kHashInit;
  bool any = false, stop = false, over = false;  // over: the line is longer than line[] holds
  int idx = 0;
  char line[tc::kComfortLineMax];
  size_t len = 0;
  for (int n; !stop && (n = f.read(g_mbuf, sizeof(g_mbuf))) > 0;) {
    for (int i = 0; i < n && !stop; i++) {
      uint8_t c = g_mbuf[i];
      if (c == '\n') {
        stop = !on_line(idx++, h, true, (const char *)line, len, over);
        h = kHashInit;
        any = false;
        over = false;
        len = 0;
      } else {
        if (c != '\r') h = (h ^ c) * 1099511628211ULL;
        any = true;
        if (len < sizeof(line)) line[len++] = (char)c;
        else over = true;
      }
    }
  }
  if (!stop && any) on_line(idx, h, false, (const char *)line, len, over);
  f.close();
  return true;
}

static bool mput(File &f, const char *p, size_t n) { return n == 0 || f.write((const uint8_t *)p, n) == n; }

// A comfort line's first column, the answer number n (digits only; the header never is).
static bool line_key(const char *line, size_t len, uint32_t &k) {
  uint64_t v = 0;
  size_t d = 0;
  for (; d < len && line[d] != ','; d++) {
    if (line[d] < '0' || line[d] > '9' || d >= 10) return false;
    v = v * 10 + (uint64_t)(line[d] - '0');
  }
  if (d == 0 || v > 0xFFFFFFFFULL) return false;
  k = (uint32_t)v;
  return true;
}

static constexpr int kMergeBatch = 128;  // FAT lines checked against the card's file per pass over it
static constexpr int kKeyBatch = 16;     // the same for comfort.csv, which also holds each line's text
static uint64_t g_mh[kMergeBatch];
static bool g_mneed[kMergeBatch];
static uint32_t g_mkey[kKeyBatch];
static bool g_mkeyed[kKeyBatch], g_mrepl[kKeyBatch], g_mdone[kKeyBatch];
static size_t g_mlen[kKeyBatch];
static char g_mtext[kKeyBatch][tc::kComfortLineMax];

struct MergeCounts {
  int copied, lines, edited, replaced, archives, problems;
};

static bool card_ends_without_newline(const char *dst) {
  File r = SD_MMC.open(dst, "r");
  if (!r) return false;
  bool no_nl = false;
  size_t sz = r.size();
  if (sz > 0 && r.seek(sz - 1)) {
    int last = r.read();
    no_nl = last >= 0 && last != '\n';
  }
  r.close();
  return no_nl;
}

// Appends to the card's file the lines of the FAT file numbered first.. (nb of them) that g_mneed marks.
static bool merge_append_wanted(const char *src, const char *dst, int first, int nb, bool &need_nl, int &added) {
  File in = FFat.open(src, "r");
  if (!in) return false;
  File out = SD_MMC.open(dst, "a");
  if (!out) {
    in.close();
    return false;
  }
  bool ok = true;
  if (need_nl) {  // a power cut left the card's last line without its line end
    ok = out.write((const uint8_t *)"\n", 1) == 1;
    need_nl = !ok;
  }
  int idx = 0, run = -1;  // run: where the wanted line's bytes start in this chunk
  bool start = true, want = false;
  for (int n; ok && (n = in.read(g_mbuf, sizeof(g_mbuf))) > 0;) {
    run = want ? 0 : -1;  // a wanted line carries on from the last chunk
    for (int i = 0; ok && i < n; i++) {
      if (start) {
        start = false;
        want = idx >= first && idx - first < nb && g_mneed[idx - first];
        if (want) run = i;
      }
      if (g_mbuf[i] == '\n') {
        if (want) {
          size_t k = (size_t)(i + 1 - run);
          ok = out.write(g_mbuf + run, k) == k;
          if (ok) added++;
        }
        want = false;
        run = -1;
        start = true;
        idx++;
      }
    }
    if (ok && want && run >= 0) ok = out.write(g_mbuf + run, (size_t)(n - run)) == (size_t)(n - run);
  }
  in.close();
  out.close();
  return ok;
}

// comfort.csv: the card's line with the same number n as a FAT line, but different, gives way to the
// FAT line (an edit made on the FAT partition is the later one). The card's file is copied line by
// line to a .tmp with those lines swapped, and the old one is kept as .bak.
static bool comfort_replace(const char *dst, int nb, int &edited) {
  char tmp[56], bak[64];
  snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
  File out = SD_MMC.open(tmp, "w");
  if (!out) return false;
  bool ok = true, bad = false;
  int swapped = 0;
  bool read_ok = each_line(SD_MMC, dst, [&](int, uint64_t, bool complete, const char *line, size_t len, bool over) -> bool {
    if (over) {  // too long to hold: not rewritten
      bad = true;
      return false;
    }
    const char *q = line;
    size_t m = len;
    uint32_t k;
    if (complete && line_key(line, len, k))
      for (int j = 0; j < nb; j++)
        if (g_mkeyed[j] && g_mrepl[j] && !g_mdone[j] && g_mkey[j] == k) {
          q = g_mtext[j];
          m = g_mlen[j];
          g_mdone[j] = true;
          swapped++;
          break;
        }
    ok = mput(out, q, m) && (!complete || mput(out, "\n", 1));
    return ok;
  });
  out.close();
  if (!read_ok || !ok || bad) {
    SD_MMC.remove(tmp);
    return false;
  }
  bak_slot(SD_MMC, dst, bak, sizeof(bak));
  if (!SD_MMC.rename(dst, bak) || !SD_MMC.rename(tmp, dst)) return false;  // a left .tmp is recovered at the next start
  edited += swapped;
  return true;
}

// A line file: what the FAT copy has that the card's lacks goes on the end of the card's, in the FAT
// copy's order, header once. Merging again adds nothing, so a power cut halfway just repeats it.
// keyed (comfort.csv): lines are matched by their number n, and see comfort_replace.
static bool merge_csv(const char *src, const char *dst, bool keyed, int &added, int &edited) {
  bool need_nl = card_ends_without_newline(dst);
  const int bs = keyed ? kKeyBatch : kMergeBatch;
  for (int first = 0;; first += bs) {
    int nb = 0;
    bool more = false;
    bool ok = each_line(FFat, src, [&](int idx, uint64_t h, bool complete, const char *line, size_t len, bool over) -> bool {
      if (idx >= first + bs) {
        more = true;
        return false;
      }
      if (idx >= first) {
        int j = idx - first;
        g_mh[j] = h;
        g_mneed[j] = complete && idx > 0;  // line 0 is the header: the card's file has its own
        if (keyed) {
          g_mkeyed[j] = g_mrepl[j] = g_mdone[j] = false;
          uint32_t k;
          if (g_mneed[j] && !over && line_key(line, len, k)) {
            g_mkeyed[j] = true;
            g_mkey[j] = k;
            g_mlen[j] = len;
            memcpy(g_mtext[j], line, len);
          }
        }
        nb = j + 1;
      }
      return true;
    });
    if (!ok) return false;
    if (nb == 0) return true;
    ok = each_line(SD_MMC, dst, [&](int, uint64_t h, bool complete, const char *line, size_t len, bool) -> bool {
      if (!complete) return true;
      uint32_t k = 0;
      bool has_key = keyed && line_key(line, len, k);
      for (int j = 0; j < nb; j++) {
        if (keyed && g_mkeyed[j]) {
          if (!has_key || g_mkey[j] != k) continue;
          g_mneed[j] = false;  // the card has this number
          if (h == g_mh[j]) {
            g_mdone[j] = true;
            g_mrepl[j] = false;
          } else if (!g_mdone[j])
            g_mrepl[j] = true;
        } else if (g_mneed[j] && g_mh[j] == h)
          g_mneed[j] = false;
      }
      return true;
    });
    if (!ok) return false;
    if (keyed) {
      bool any = false;
      for (int j = 0; j < nb; j++) any = any || (g_mkeyed[j] && g_mrepl[j] && !g_mdone[j]);
      if (any && !comfort_replace(dst, nb, edited)) return false;
    }
    int want = 0;
    for (int j = 0; j < nb; j++) {
      for (int k = 0; k < j && g_mneed[j]; k++)
        if (g_mh[k] == g_mh[j]) g_mneed[j] = false;  // the same line twice in one batch
      if (g_mneed[j]) want++;
    }
    if (want && !merge_append_wanted(src, dst, first, nb, need_nl, added)) return false;
    if (!more) return true;
    delay(1);
  }
}

// ---- Archive months ("2026-10.bin", "series.bin") ----
// Merged event by event the way Controller::archive_put does it (archive_merge: newer wins, new ones
// added), never replaced: past events are kept forever. The result goes through a verified .tmp and
// the card's original is kept as .bak. If it can't be done (a file unreadable, too many events, no
// memory), the card's file is left alone, the FAT one is copied beside it as name.fat, and false.
static constexpr int kArchiveMaxEvents = 4000;  // per file: a month holds a few dozen

static uint8_t *read_whole(fs::FS &fs, const char *path, size_t &len) {
  len = 0;
  File f = fs.open(path, "r");
  if (!f) return nullptr;
  size_t n = f.size();
  uint8_t *buf = (uint8_t *)heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bool ok = buf && f.read(buf, n) == n;
  f.close();
  if (!ok) {
    free(buf);
    return nullptr;
  }
  len = n;
  return buf;
}

static tc::Event *new_events(int n) {
  tc::Event *e = (tc::Event *)heap_caps_malloc(sizeof(tc::Event) * (n > 0 ? n : 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (e)
    for (int i = 0; i < n; i++) new (&e[i]) tc::Event();
  return e;
}

static bool merge_archive(const char *src, const char *dst, const char *tmp, MergeCounts &c) {
  size_t cl = 0, fl = 0;
  uint8_t *cb = read_whole(SD_MMC, dst, cl);
  uint8_t *fb = read_whole(FFat, src, fl);
  tc::Event *all = nullptr, *fe = nullptr;
  bool ok = cb && fb && cl >= 16 && fl >= 16;
  int ccap = 0, fcap = 0, cn = 0, fn = 0;
  if (ok) {
    ccap = cb[6] | cb[7] << 8;
    fcap = fb[6] | fb[7] << 8;
    ok = ccap <= kArchiveMaxEvents && fcap <= kArchiveMaxEvents;
  }
  if (ok) {
    all = new_events(ccap + fcap);
    fe = new_events(fcap);
    ok = all && fe;
  }
  if (ok) {
    uint32_t clock = 0;
    cn = tc::events_deserialize(cb, cl, all, ccap + fcap, clock);
    fn = tc::events_deserialize(fb, fl, fe, fcap, clock);
    ok = cn >= 0 && fn >= 0;
  }
  bool done = false;
  if (ok) {
    bool changed = false;
    for (int i = 0; i < fn; i++) changed |= tc::archive_merge(all, cn, ccap + fcap, fe[i]);
    if (!changed) {
      done = true;  // the card already has everything
    } else {
      size_t size = tc::events_serialized_size(all, cn);
      uint8_t *out = (uint8_t *)heap_caps_malloc(size ? size : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      size_t k = out ? tc::events_serialize(all, cn, 0, out, size) : 0;
      bool wrote = false;
      if (k) {
        File f = SD_MMC.open(tmp, "w");
        wrote = f && f.write(out, k) == k;
        if (f) f.close();
        if (wrote) {
          File v = SD_MMC.open(tmp, "r");
          wrote = v && v.size() == k;
          if (v) v.close();
        }
      }
      free(out);
      if (wrote) {
        char bak[64];
        bak_slot(SD_MMC, dst, bak, sizeof(bak));
        if (SD_MMC.rename(dst, bak) && SD_MMC.rename(tmp, dst)) {
          done = true;
          c.archives++;
        }
      } else
        SD_MMC.remove(tmp);
    }
  }
  free(cb);
  free(fb);
  free(all);
  free(fe);
  if (done) return true;
  char beside[64];
  snprintf(beside, sizeof(beside), "%s.fat", dst);
  if (SD_MMC.exists(dst) && copy_verified(FFat, src, SD_MMC, tmp)) {  // never when the card's file is gone: its .tmp is the copy
    SD_MMC.remove(beside);
    SD_MMC.rename(tmp, beside);
  }
  elog("calendar: archive %s couldn't be merged: the FAT copy is kept beside it as %s", dst, beside);
  return false;
}

// ---- Which files this fallback episode wrote: the marker's list ----
static char g_mk[1024];
static bool g_mk_empty = true, g_mk_star = false;

static bool marker_scan(const char *name) {
  size_t nl = strlen(name);
  for (const char *p = g_mk; *p;) {
    const char *e = strchr(p, '\n');
    size_t l = e ? (size_t)(e - p) : strlen(p);
    size_t cmp = l;
    if (cmp && p[cmp - 1] == '\r') cmp--;
    if (cmp == nl && !strncmp(p, name, cmp)) return true;
    p += e ? l + 1 : l;
  }
  return false;
}

static void read_marker() {
  g_mk[0] = 0;
  g_mk_empty = true;
  g_mk_star = false;
  File m = FFat.open(kMarker, "r");
  if (!m) return;
  int n = m.read((uint8_t *)g_mk, sizeof(g_mk) - 1);
  m.close();
  if (n <= 0) return;
  g_mk[n] = 0;
  for (int i = 0; i < n; i++)
    if (g_mk[i] != '\n' && g_mk[i] != '\r' && g_mk[i] != ' ') g_mk_empty = false;
  g_mk_star = marker_scan("*");
}

static bool marker_lists(const char *name) { return g_mk_star || marker_scan(name); }

static bool is_bin_name(const char *name) {
  size_t n = strlen(name);
  return n > 4 && !strcmp(name + n - 4, ".bin");
}

static void merge_one(const char *name, MergeCounts &c) {
  char src[48], dst[48], tmp[56], bak[64];
  snprintf(src, sizeof(src), "%s/%s", kDir, name);
  snprintf(dst, sizeof(dst), "%s/%s", kDir, name);
  snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
  recover_tmp(SD_MMC, dst, tmp, is_bin_name(name));  // the card's own cut-short rewrite first
  bool card_has = false;
  if (SD_MMC.exists(dst)) {
    File chk = SD_MMC.open(dst, "r");
    if (!chk) {  // there, but won't open: not the same as missing
      c.problems++;
      return;
    }
    card_has = chk.size() > 0;
    chk.close();
  }
  if (!card_has) {  // nothing there to lose: the FAT copy, through a temporary file
    if (copy_verified(FFat, src, SD_MMC, tmp)) {
      SD_MMC.remove(dst);
      if (SD_MMC.rename(tmp, dst)) c.copied++;
      else c.problems++;
    } else {
      SD_MMC.remove(tmp);
      c.problems++;
    }
    return;
  }
  if (is_csv_name(name)) {
    // The line files are harmless to merge, so an empty or unreadable marker still merges them all;
    // otherwise only the ones this episode wrote.
    if (!g_mk_empty && !marker_lists(name)) return;
    int added = 0, edited = 0;
    bool ok = merge_csv(src, dst, !strcmp(name, tc::kComfortFile), added, edited);
    c.lines += added;
    c.edited += edited;
    if (!ok) c.problems++;
    return;
  }
  // Anything else is only merged if this episode wrote it: FAT copies linger from earlier ones.
  if (!marker_lists(name)) return;
  // Archive months are merged event by event; they are never replaced.
  if (is_bin_name(name) && strcmp(name, "calendar.bin")) {
    if (!merge_archive(src, dst, tmp, c)) c.problems++;
    return;
  }
  // The main calendar: always written whole from memory, so the FAT copy is the newer one. The card's
  // copy stays beside it as .bak. The new file is complete on the card (as .tmp) before anything is
  // renamed, and a cut between the two renames leaves the .tmp, which recover_tmp turns into the
  // file at the next start.
  if (same_file(FFat, src, SD_MMC, dst)) return;
  if (!copy_verified(FFat, src, SD_MMC, tmp)) {
    SD_MMC.remove(tmp);
    c.problems++;
    return;
  }
  bak_slot(SD_MMC, dst, bak, sizeof(bak));
  if (SD_MMC.rename(dst, bak) && SD_MMC.rename(tmp, dst)) c.replaced++;
  else c.problems++;
}

// True when every file was merged. The marker is removed by the caller, only on true.
static bool merge_fat_into_sd() {
  if (!g_fat || !g_sd) return false;
  uint32_t t0 = millis();
  MergeCounts c = {};
  read_marker();
  File dir = FFat.open(kDir);
  if (!dir || !dir.isDirectory()) return false;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    char name[32] = "";
    bool is_dir = f.isDirectory();
    if (!is_dir && strlen(f.name()) < sizeof(name)) snprintf(name, sizeof(name), "%s", f.name());
    f.close();
    if (is_dir) continue;
    if (!name[0]) {  // a name too long for the buffers: not merged, so the marker stays
      c.problems++;
      continue;
    }
    if (!strcmp(name, kMarkerName) || strstr(name, ".bak") || strstr(name, ".tmp") || strstr(name, ".fat")) continue;
    merge_one(name, c);
  }
  elog("calendar: FAT changes merged onto the card in %lu ms: %d files copied, %d lines appended, %d comfort lines edited, %d archives merged, %d files replaced (the card's kept as .bak), %d problems",
       (unsigned long)(millis() - t0), c.copied, c.lines, c.edited, c.archives, c.replaced, c.problems);
  return c.problems == 0;
}

// The card is readable: the FAT partition's changes first (if the marker says there are some), then
// any file the card has none of.
static void sd_merge_and_copy() {
  if (!g_fat || !g_sd) return;
  if (!SD_MMC.exists(kDir)) SD_MMC.mkdir(kDir);
  if (FFat.exists(kMarker)) {
    if (merge_fat_into_sd() && FFat.remove(kMarker)) elog("calendar: merge done, FAT marker removed");
    else elog("calendar: merge not finished: the FAT marker stays, and it is tried again at the next start");
  }
  copy_missing_to_sd();
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
  sd_merge_and_copy();
  g_cal = g_sd ? (fs::FS *)&SD_MMC : g_fat ? (fs::FS *)&FFat : nullptr;
  g_on_fallback = !g_sd && g_fat;
  if (g_cal && !g_cal->exists(kDir)) g_cal->mkdir(kDir);
}

// ---- A card that fails while running ----

// The file's name on disk, as the marker lists it ("calendar" is "calendar.bin").
static void disk_name(char *out, size_t n, const char *name) {
  if (strchr(name, '.')) snprintf(out, n, "%s", name);
  else snprintf(out, n, "%s.bin", name);
}

// The marker exists (made empty if need be).
static void make_marker() {
  if (!g_fat) return;
  if (!FFat.exists(kDir)) FFat.mkdir(kDir);
  if (!FFat.exists(kMarker)) {
    File m = FFat.open(kMarker, "w");
    if (m) m.close();
  }
}

// Before a file is first written on the FAT partition as the fallback in this run: its name goes
// into the marker, so a power cut right after the write can't leave changes nobody knows to merge.
static void list_on_fat(const char *name) {
  if (!g_fat || !name || g_list_overflow) return;
  char d[16];
  disk_name(d, sizeof(d), name);
  for (int i = 0; i < g_nlisted; i++)
    if (!strcmp(g_listed[i], d)) return;
  if (!FFat.exists(kDir)) FFat.mkdir(kDir);
  bool full = g_nlisted >= kListedMax || strlen(d) >= sizeof(g_listed[0]);
  File m = FFat.open(kMarker, "a");
  if (!m) {
    elog("calendar: couldn't open the FAT marker %s", kMarker);
    return;
  }
  bool ok = m.print(full ? "*\n" : d) == (full ? 2u : strlen(d)) && (full || m.print("\n") == 1);
  m.close();
  if (!ok) {
    elog("calendar: couldn't write the FAT marker %s", kMarker);
    return;
  }
  if (full) g_list_overflow = true;
  else snprintf(g_listed[g_nlisted++], sizeof(g_listed[0]), "%s", d);
}

static bool sd_remount() {
  if (g_test_fail == 1) return false;
  SD_MMC.end();
  return sd_mount(false);  // the same arguments as at start-up
}

static bool switch_to_fat() {
  if (!g_fat) {
    elog("calendar: the card failed and there is no FAT partition: nothing can be saved");
    return false;
  }
  SD_MMC.end();
  g_sd = false;
  g_on_fallback = true;
  g_cal = &FFat;
  make_marker();
  elog("calendar: the card failed: the FAT partition is used for the rest of this run, and its changes go onto the card at the next start");
  return true;
}

// Called inside an op after a try failed. Remounts the card (at most once a minute), else switches
// to the FAT partition. True when something changed, so one more try is worth making.
static bool recover() {
  bool leader = false;
  taskENTER_CRITICAL(&g_op_mux);
  g_active = g_active - 1;  // this op is paused while the card is dealt with
  if (!g_remounting) {
    g_remounting = true;
    leader = true;
  }
  taskEXIT_CRITICAL(&g_op_mux);
  if (!leader) {  // another task is on it: wait, then try again with whatever it left
    while (g_remounting) vTaskDelay(pdMS_TO_TICKS(10));
    op_enter();
    return true;
  }
  for (;;) {  // every other op finishes first; no new one starts
    int n;
    taskENTER_CRITICAL(&g_op_mux);
    n = g_active;
    taskEXIT_CRITICAL(&g_op_mux);
    if (n == 0) break;
    vTaskDelay(1);
  }
  bool changed = false;
  if (g_cal == &SD_MMC) {
    uint32_t now = millis();
    if (!g_remounted || now - g_last_remount >= kRemountGapMs) {
      g_remounted = true;
      g_last_remount = now;
      changed = sd_remount();
      elog("calendar: SD card remount %s in %lu ms", changed ? "worked" : "FAILED", (unsigned long)(millis() - now));
      if (changed && !SD_MMC.exists(kDir)) SD_MMC.mkdir(kDir);
    }
    if (!changed) changed = switch_to_fat();  // remount failed, or one was tried within the minute
  }
  taskENTER_CRITICAL(&g_op_mux);
  g_remounting = false;
  g_active = g_active + 1;
  taskEXIT_CRITICAL(&g_op_mux);
  return changed;
}

// Runs one file operation on the file system in use, and again after a remount or on the FAT
// partition if it fails. once(fs) returns 0 done, 1 the file system failed, 2 refused for another
// reason (no such line, nothing to read): no recovery for those. Writes mark the FAT partition as
// holding changes the card hasn't seen (name: the file, for the marker's list). At most four tries, so nothing loops.
template <class F>
static bool with_fallback(bool writing, const char *name, F once) {
  OpGuard guard;
  for (int attempt = 0; attempt < 4; attempt++) {
    if (!g_cal) {
      if (writing) g_write_failures++;
      return false;
    }
    if (writing && g_cal == &FFat) list_on_fat(name);
    int r;
    if (g_test_fail && g_cal == &SD_MMC) {
      r = 1;
      if (g_test_fail == 2) g_test_fail = 0;  // once only; the remount then works
    } else
      r = once(*g_cal);
    if (r == 0) return true;
    if (r == 2) return false;
    if (writing) g_write_failures++;
    if (!recover()) return false;
  }
  return false;
}

uint8_t *calendar_read(const char *name, size_t &len) {
  len = 0;
  char p[48], t[48];
  cal_path(p, sizeof(p), name);
  cal_path(t, sizeof(t), name, true);
  uint8_t *out = nullptr;
  size_t out_len = 0;
  with_fallback(false, name, [&](fs::FS &fs) -> int {
    recover_tmp(fs, p, t, is_binary_name(name));
    File f = fs.open(p, "r");
    if (!f && !strcmp(name, "calendar") && g_fat && FFat.exists(kLegacy)) f = FFat.open(kLegacy, "r");
    // No such file is an answer; a /tcomm folder that has gone missing means the card has.
    if (!f) return fs.exists(kDir) ? 2 : 1;
    size_t n = f.size();
    uint8_t *buf = (uint8_t *)heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = buf && f.read(buf, n) == n;
    f.close();
    if (!ok) {
      const bool had_memory = buf != nullptr;
      free(buf);
      elog("calendar: couldn't read %s", p);
      return had_memory ? 1 : 2;  // no memory is not the card's fault
    }
    out = buf;
    out_len = n;
    return 0;
  });
  len = out_len;
  return out;
}

// The first column of the last complete line (one ending in a newline), read from the file's last
// few hundred bytes only: a comfort file grows for years. Mike, 2026-11-07: the answer count must
// never go backwards, even if the saved one is lost. 0 if there is no such line or it isn't a number.
uint32_t calendar_last_number(const char *name) {
  if (!name) return 0;
  OpGuard guard;
  if (!g_cal) return 0;
  char p[48], t[48];
  cal_path(p, sizeof(p), name);
  cal_path(t, sizeof(t), name, true);
  recover_tmp(*g_cal, p, t, is_binary_name(name));
  File f = g_cal->open(p, "r");
  if (!f) return 0;
  size_t size = f.size();
  static constexpr size_t kTail = 512;
  size_t start = size > kTail ? size - kTail : 0;
  char buf[kTail + 1];
  size_t got = 0;
  if (size && f.seek(start)) got = f.read((uint8_t *)buf, size - start);
  f.close();
  if (got == 0) return 0;
  // Drop anything after the last newline (a line a power cut left short).
  size_t end = got;
  while (end > 0 && buf[end - 1] != '\n') end--;
  if (end == 0) return 0;
  end--;  // the last complete line ends just before this newline
  size_t b = end;
  while (b > 0 && buf[b - 1] != '\n') b--;
  // If the tail began mid-line, the line is cut at its front: only whole lines count.
  if (b == 0 && start > 0) return 0;
  uint32_t v = 0;
  size_t i = b;
  size_t digits = 0;
  while (i < end && buf[i] >= '0' && buf[i] <= '9') {
    v = v * 10 + (uint32_t)(buf[i] - '0');
    i++;
    digits++;
  }
  if (digits == 0 || (i < end && buf[i] != ',')) return 0;
  return v;
}

bool calendar_write(const char *name, const uint8_t *buf, size_t len) {
  char p[48], t[48];
  cal_path(p, sizeof(p), name);
  cal_path(t, sizeof(t), name, true);
  return with_fallback(true, name, [&](fs::FS &fs) -> int {
    recover_tmp(fs, p, t, is_binary_name(name));  // before the .tmp is overwritten, in case it is the only copy
    File f = fs.open(t, "w");
    if (!f) return 1;
    bool ok = f.write(buf, len) == len;
    f.close();
    if (!ok) {
      fs.remove(t);  // half a file; the real one is still there
      return 1;
    }
    fs.remove(p);
    return fs.rename(t, p) ? 0 : 1;  // if this fails the complete .tmp is kept: recover_tmp uses it
  });
}

bool calendar_append(const char *name, const char *header, const char *line) {
  char p[48], t[48];
  cal_path(p, sizeof(p), name);
  cal_path(t, sizeof(t), name, true);
  return with_fallback(true, name, [&](fs::FS &fs) -> int {
    recover_tmp(fs, p, t, is_binary_name(name));
    // A power cut can leave the last line without its line end; the new line must not merge into it.
    bool need_nl = false;
    {
      File r = fs.open(p, "r");
      if (r) {
        size_t sz = r.size();
        if (sz > 0 && r.seek(sz - 1)) {
          int last = r.read();
          need_nl = last >= 0 && last != '\n';
        }
        r.close();
      }
    }
    File f = fs.open(p, "a");
    if (!f) {
      elog("calendar: couldn't open %s to append", p);
      return 1;
    }
    // The header and the line come with their own line ends (comfort.h).
    bool ok = true;
    if (need_nl) ok = f.print("\n") == 1;
    if (f.size() == 0 && header && *header) ok = f.print(header) == strlen(header);
    ok = ok && f.print(line) == strlen(line);
    f.close();
    if (!ok) elog("calendar: appending to %s failed", p);
    return ok ? 0 : 1;
  });
}

// calendar_set_field: the file is copied line by line into a temporary file, the one line's column
// replaced on the way. Mike, 2026-11-06: a comfort file grows for years, so it's never held whole.
static bool put_bytes(File &f, const char *p, size_t n) { return n == 0 || f.write((const uint8_t *)p, n) == n; }

// True when the line's first column is the number n (digits only, so the header never is).
static bool line_is_n(const char *line, size_t len, uint32_t n) {
  uint64_t v = 0;
  size_t digits = 0;
  for (; digits < len && line[digits] != ','; digits++) {
    if (line[digits] < '0' || line[digits] > '9' || digits >= 10) return false;
    v = v * 10 + (uint64_t)(line[digits] - '0');
  }
  return digits > 0 && v == n;
}

// The line with its column (0-based) replaced by value, into out; false if the line has no such
// column or the result doesn't fit.
static bool line_set_column(const char *line, size_t len, int column, const char *value, char *out, size_t cap,
                            size_t &total) {
  size_t start = 0;
  for (int c = 0; c < column; c++) {
    const char *comma = (const char *)memchr(line + start, ',', len - start);
    if (!comma) return false;
    start = (size_t)(comma - line) + 1;
  }
  size_t end = start;
  while (end < len && line[end] != ',') end++;
  size_t vl = strlen(value);
  total = start + vl + (len - end);
  if (total > cap) return false;
  memcpy(out, line, start);
  memcpy(out + start, value, vl);
  memcpy(out + start + vl, line + end, len - end);
  return true;
}

// Writes one line (its line end too, when nl) to out, with the column replaced when it's the line
// for n. found is set when it is. False if a write failed or the column is missing (no_column).
static bool put_line(File &out, const char *line, size_t len, bool nl, uint32_t n, int column, const char *value,
                     bool &found, bool &no_column) {
  char edited[2 * tc::kComfortLineMax];
  const char *p = line;
  size_t m = len;
  if (!found && line_is_n(line, len, n)) {
    found = true;
    if (!line_set_column(line, len, column, value, edited, sizeof(edited), m)) {
      no_column = true;
      return false;
    }
    p = edited;
  }
  return put_bytes(out, p, m) && (!nl || put_bytes(out, "\n", 1));
}

bool calendar_set_field(const char *name, uint32_t n, int column, const char *value) {
  if (!value || column < 0 || strchr(value, ',') || strchr(value, '\n') || strlen(value) > tc::kComfortLineMax) return false;
  char p[48], t[48];
  cal_path(p, sizeof(p), name);
  cal_path(t, sizeof(t), name, true);
  return with_fallback(true, name, [&](fs::FS &fs) -> int {
    recover_tmp(fs, p, t, is_binary_name(name));
    File in = fs.open(p, "r");
    if (!in) {
      elog("calendar: couldn't open %s to set a field", p);
      return fs.exists(kDir) ? 2 : 1;  // no such file is an answer; no /tcomm folder means no card
    }
    File out = fs.open(t, "w");
    if (!out) {
      in.close();
      elog("calendar: couldn't open %s to write", t);
      return 1;
    }
    bool ok = true, found = false, raw = false, no_column = false;  // raw: a line too long to hold, copied as it comes
    char line[tc::kComfortLineMax];
    size_t len = 0;
    uint8_t chunk[256];
    for (int got; ok && (got = in.read(chunk, sizeof(chunk))) > 0;) {
      for (int i = 0; ok && i < got; i++) {
        char c = (char)chunk[i];
        if (c == '\n') {
          ok = raw ? put_bytes(out, &c, 1) : put_line(out, line, len, true, n, column, value, found, no_column);
          len = 0;
          raw = false;
        } else if (raw) {
          ok = put_bytes(out, &c, 1);
        } else if (len < sizeof(line)) {
          line[len++] = c;
        } else {  // too long to hold: what's held goes out as it is, and the rest of the line after it
          raw = true;
          ok = put_bytes(out, line, len) && put_bytes(out, &c, 1);
        }
      }
    }
    if (ok && !raw && len > 0) ok = put_line(out, line, len, false, n, column, value, found, no_column);  // no line end
    in.close();
    out.close();
    if (!ok || !found) {
      fs.remove(t);
      elog("calendar: %s line %lu not changed: %s", p, (unsigned long)n,
           ok ? "no such line" : no_column ? "no such column" : "a write failed");
      return no_column || (ok && !found) ? 2 : 1;
    }
    fs.remove(p);
    ok = fs.rename(t, p);
    elog("calendar: %s line %lu column %d %s", p, (unsigned long)n, column, ok ? "set" : "NOT SET (rename failed)");
    return ok ? 0 : 1;
  });
}

bool calendar_fs_info(CalendarFs &o) {
  OpGuard guard;  // not while the card is being remounted
  o = CalendarFs{g_sd, g_fat, g_sd ? SD_MMC.cardSize() : 0, 0, 0, 0};
  if (!g_cal) return false;
  fs_size(g_sd ? "/sd" : "/ffat", o.total, o.used);
  File dir = g_cal->open(kDir);
  if (dir && dir.isDirectory())
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) o.files++;
  return true;
}

void sd_report() {
  Serial.printf("storage: %s; %lu failed write tries this run; FAT marker %s\n",
                g_on_fallback ? "running on the FAT partition (the card was unreadable or failed)" : "on the card",
                (unsigned long)g_write_failures, g_fat && FFat.exists(kMarker) ? "present (changes wait to be merged)" : "absent");
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
  sd_merge_and_copy();  // a formatted card is empty, but the FAT changes still go through the same steps
  g_cal = &SD_MMC;
  g_on_fallback = false;
  sd_report();
  return true;
}

uint8_t load_display_test() { return g_open ? g_prefs.getUChar("disptest2", 0xFF) : 0xFF; }
void save_display_test(uint8_t v) {
  if (g_open) g_prefs.putUChar("disptest2", v);
}

}  // namespace board
