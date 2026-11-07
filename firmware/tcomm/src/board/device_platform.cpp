#include "device_platform.h"
#include <Arduino.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "../core/civil.h"
#include "board.h"
#include "../core/events.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace board {

static portMUX_TYPE g_save_mux = portMUX_INITIALIZER_UNLOCKED;

void DevicePlatform::apply_tz(const char *tz) {
  setenv("TZ", tz && *tz ? tz : "UTC0", 1);
  tzset();
}

void DevicePlatform::begin(const tc::Settings &s) {
  files_lock_ = xSemaphoreCreateMutex();  // here, before any task uses it (not inside a spinlock)
  apply_tz(s.tz);
  RtcTime r = rtc_read();
  if (r.valid) {
    timeval tv = {(time_t)r.utc, 0};
    settimeofday(&tv, nullptr);
    valid_ = true;
    quality_ = load_clock_quality();
  }
}

uint32_t DevicePlatform::mono_ms() { return millis(); }

tc::WallTime DevicePlatform::wall() {
  tc::WallTime w;
  w.valid = valid_;
  w.utc = valid_ ? (uint32_t)time(nullptr) : 0;
  w.quality = valid_ ? quality_ : (uint8_t)tc::kClockNone;
  return w;
}

void DevicePlatform::set_wall(uint32_t utc, uint8_t quality) {
  timeval tv = {(time_t)utc, 0};
  settimeofday(&tv, nullptr);
  valid_ = true;
  quality_ = quality;
  rtc_write(utc);
  taskENTER_CRITICAL(&g_save_mux);
  clock_q_ = quality;
  dirty_ |= kDirtyClock;
  taskEXIT_CRITICAL(&g_save_mux);
}

int32_t DevicePlatform::utc_offset(uint32_t utc) {
  // newlib's struct tm has no tm_gmtoff: turn the local fields back into seconds as if UTC.
  time_t t = (time_t)utc;
  struct tm tm;
  localtime_r(&t, &tm);
  int64_t local = tc::seconds_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return (int32_t)(local - (int64_t)utc);
}

void DevicePlatform::radio_send(uint8_t to, const uint8_t *data, size_t len) {
  if (demo_) demo_->from_panel(to, data, len);
  else board::radio_send(to, data, len);
}

void DevicePlatform::play(tc::Sound s, uint8_t volume) { audio_play(s, volume); }
void DevicePlatform::preview(tc::Sound s, uint8_t volume) { audio_play(s, volume, true); }
void DevicePlatform::set_backlight(uint8_t reg) { expander_backlight(reg); }
void DevicePlatform::save_own(const tc::OwnState &s) {
  taskENTER_CRITICAL(&g_save_mux);
  own_ = s;
  dirty_ |= kDirtyOwn;
  taskEXIT_CRITICAL(&g_save_mux);
}

void DevicePlatform::save_dog(const tc::DogState &d) {
  taskENTER_CRITICAL(&g_save_mux);
  dog_ = d;
  dirty_ |= kDirtyDog;
  taskEXIT_CRITICAL(&g_save_mux);
}

void DevicePlatform::save_mail(const tc::MailState &m) {
  taskENTER_CRITICAL(&g_save_mux);
  mail_ = m;
  dirty_ |= kDirtyMail;
  taskEXIT_CRITICAL(&g_save_mux);
}

void DevicePlatform::save_comfort_count(uint32_t n) {
  taskENTER_CRITICAL(&g_save_mux);
  comfort_n_ = n;
  dirty_ |= kDirtyComfort;
  taskEXIT_CRITICAL(&g_save_mux);
}

void DevicePlatform::save_requests(const tc::RequestState &s) {
  taskENTER_CRITICAL(&g_save_mux);
  requests_ = s;
  dirty_ |= kDirtyRequests;
  taskEXIT_CRITICAL(&g_save_mux);
}

// Called in the LVGL task: queued for the loop task (flush_saves), like write_file.
void DevicePlatform::append_line(const char *name, const char *header, const char *line) {
  PendingLine l = {};
  snprintf(l.name, sizeof(l.name), "%s", name);
  snprintf(l.header, sizeof(l.header), "%s", header ? header : "");
  snprintf(l.line, sizeof(l.line), "%s", line);
  queue_line(l);
}

// Called in the LVGL task: queued after the lines already waiting, so an edit never overtakes an
// append (the chart's REMOVE on an answer whose line is still waiting). Mike, 2026-11-06.
void DevicePlatform::set_csv_field(const char *name, uint32_t n, int column, const char *value) {
  PendingLine l = {};
  snprintf(l.name, sizeof(l.name), "%s", name);
  if (strlen(value) >= sizeof(l.line)) {
    elog("calendar: value for %s too long, not set", l.name);
    return;
  }
  l.set = true;
  l.n = n;
  l.column = column;
  snprintf(l.line, sizeof(l.line), "%s", value);
  queue_line(l);
}

// Writes one queued entry to the card. The caller holds the files lock.
void DevicePlatform::write_pending_line(const PendingLine &l) {
  if (l.set) {  // calendar_set_field logs its own outcome
    calendar_set_field(l.name, l.n, l.column, l.line);
    return;
  }
  uint32_t t1 = millis();
  bool ok = calendar_append(l.name, l.header, l.line);
  elog("calendar: line for %s %s in %lu ms", l.name, ok ? "appended" : "NOT SAVED", (unsigned long)(millis() - t1));
}

void DevicePlatform::queue_line(const PendingLine &l) {
  lock_files();
  if (nlines_ < (int)(sizeof(lines_) / sizeof(lines_[0]))) {
    lines_[nlines_++] = l;
    unlock_files();
    return;
  }
  // Never expected (an answer every few minutes at most). The lock stays held while the queued
  // entries go out in order and then this one, so nothing overtakes anything and the loop task
  // never touches the file meanwhile.
  elog("calendar: line queue full, writing %s from the interface task", l.name);
  for (int i = 0; i < nlines_; i++) write_pending_line(lines_[i]);
  nlines_ = 0;
  write_pending_line(l);
  unlock_files();
}

void DevicePlatform::save_settings(const tc::Settings &s) {
  taskENTER_CRITICAL(&g_save_mux);
  settings_ = s;
  dirty_ |= kDirtySettings;
  taskEXIT_CRITICAL(&g_save_mux);
  apply_tz(s.tz);
  audio_set_ringtone(s.ringtone);
}

void DevicePlatform::flush_saves() {
  // The calendar's files, one per call: the first waiting one.
  if (files_lock_) {
    lock_files();
    PendingFile job = {};
    int at = -1;
    for (int i = 0; i < npend_ && at < 0; i++)
      if (!pend_[i].writing) {
        pend_[i].writing = true;
        job = pend_[i];
        at = i;
      }
    unlock_files();
    if (at >= 0) {
      uint32_t t1 = millis();
      bool ok = calendar_write(job.name, job.buf, job.len);
      // UNVERIFIED on the panel: how long a card write stalls the screen and the sound.
      elog("calendar: %s, %u bytes %s in %lu ms", job.name, (unsigned)job.len, ok ? "saved" : "NOT SAVED",
           (unsigned long)(millis() - t1));
      lock_files();
      for (int i = 0; i < npend_; i++)
        if (pend_[i].writing && pend_[i].buf == job.buf) {
          for (int j = i; j + 1 < npend_; j++) pend_[j] = pend_[j + 1];
          npend_--;
          break;
        }
      unlock_files();
      free_big(job.buf);
    }
  }
  if (files_lock_) {  // appended lines and set fields, oldest first, one per call
    // The lock is held from taking the entry to finishing its write, so a full queue's writer in
    // the interface task can't get its newer entry onto the card ahead of this older one.
    lock_files();
    if (nlines_ > 0) {
      PendingLine l = lines_[0];
      for (int i = 0; i + 1 < nlines_; i++) lines_[i] = lines_[i + 1];
      nlines_--;
      write_pending_line(l);
    }
    unlock_files();
  }
  if (!dirty_) return;
  tc::OwnState own;
  tc::DogState dog;
  tc::MailState mail;
  tc::Settings settings;
  uint32_t comfort;
  tc::RequestState requests;
  uint8_t q;
  taskENTER_CRITICAL(&g_save_mux);
  uint8_t d = dirty_;
  dirty_ = 0;
  own = own_;
  dog = dog_;
  mail = mail_;
  settings = settings_;
  comfort = comfort_n_;
  requests = requests_;
  q = clock_q_;
  taskEXIT_CRITICAL(&g_save_mux);
  uint32_t t0 = millis();
  if (d & kDirtyOwn) board::save_own(own);
  if (d & kDirtyDog) board::save_dog(dog);
  if (d & kDirtyMail) board::save_mail(mail);
  if (d & kDirtySettings) board::save_settings(settings);
  if (d & kDirtyComfort) board::save_comfort_count(comfort);
  if (d & kDirtyRequests) board::save_requests(requests);
  if (d & kDirtyClock) save_clock_quality(q);

  // A flash write stalls both cores' caches; worth seeing next to a sound that went unheard.
  uint32_t ms = millis() - t0;
  if (ms >= 15) elog("saved to flash in %lu ms", (unsigned long)ms);
}

void *DevicePlatform::alloc_big(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}

void DevicePlatform::lock_files() {
  if (files_lock_) xSemaphoreTake((SemaphoreHandle_t)files_lock_, portMAX_DELAY);
}

void DevicePlatform::unlock_files() {
  if (files_lock_) xSemaphoreGive((SemaphoreHandle_t)files_lock_);
}

// Called in the LVGL task: what's waiting to be written is the file as it now is.
uint8_t *DevicePlatform::read_file(const char *name, size_t &len) {
  lock_files();
  for (int i = npend_ - 1; i >= 0; i--) {
    if (strcmp(pend_[i].name, name)) continue;
    uint8_t *copy = (uint8_t *)alloc_big(pend_[i].len ? pend_[i].len : 1);
    if (copy) memcpy(copy, pend_[i].buf, pend_[i].len);
    len = copy ? pend_[i].len : 0;
    unlock_files();
    return copy;
  }
  // The card read stays under the lock: a queued line may be rewriting this very file (the
  // temporary file is renamed over it), and a read in the middle could find it missing.
  uint32_t t0 = millis();
  uint8_t *buf = calendar_read(name, len);
  unlock_files();
  uint32_t ms = millis() - t0;
  if (ms >= 20) elog("calendar: %s read in %lu ms", name, (unsigned long)ms);
  return buf;
}

// Called in the LVGL task: queued for the loop task, which writes it (flush_saves).
void DevicePlatform::write_file(const char *name, uint8_t *buf, size_t len) {
  lock_files();
  for (int i = 0; i < npend_; i++) {
    if (pend_[i].writing || strcmp(pend_[i].name, name)) continue;
    free_big(pend_[i].buf);  // a newer version of a file still waiting
    pend_[i].buf = buf;
    pend_[i].len = len;
    unlock_files();
    return;
  }
  if (npend_ < (int)(sizeof(pend_) / sizeof(pend_[0]))) {
    PendingFile &p = pend_[npend_++];
    snprintf(p.name, sizeof(p.name), "%s", name);
    p.buf = buf;
    p.len = len;
    p.writing = false;
    unlock_files();
    return;
  }
  unlock_files();
  // Never expected (the controller writes a few files a day): written here rather than lost.
  elog("calendar: write queue full, writing %s from the interface task", name);
  calendar_write(name, buf, len);
  free_big(buf);
}

uint32_t DevicePlatform::random32() { return esp_random(); }

void DevicePlatform::log(const char *line) { elog("%s", line); }  // called from the LVGL task

}  // namespace board
