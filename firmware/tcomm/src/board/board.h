// Board layer for the Waveshare ESP32-S3-Touch-LCD-4.3C: everything that touches hardware, apart
// from Waveshare's display, touch and codec drivers (src/ws). Nothing here reboots on an I2C error:
// Waveshare's helpers use ESP_ERROR_CHECK, so after start-up only these functions talk on the bus.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../core/dog.h"
#include "../core/mail.h"
#include "../core/note.h"
#include "../core/requests.h"
#include "../core/settings.h"
#include "../core/status.h"
#include "../core/voice_proto.h"
#include "driver/i2c_master.h"

namespace board {

// ---- I2C: one bus (GPIO8/9), one lock around every transfer this firmware makes (handoff). ----
// GT911 touch and the ES8311 codec use their own handles on the same bus; ESP-IDF's i2c_master
// driver serializes individual transfers between tasks.
// Touch and the expander run at 400 kHz, the codecs and the SHT40 at 100. With the SHT40's cable
// attached, serial "i2c test" found no failed or wrong reads at either speed (2026-10-24).
constexpr uint32_t kI2cHz = 400000;
bool i2c_begin();
i2c_master_dev_handle_t i2c_device(uint8_t addr, uint32_t scl_hz = kI2cHz);
bool i2c_write(i2c_master_dev_handle_t dev, const uint8_t *data, size_t n);
bool i2c_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out, size_t n);
bool i2c_receive(i2c_master_dev_handle_t dev, uint8_t *out, size_t n);  // a plain read, no register
bool i2c_probe(uint8_t addr);
uint32_t i2c_errors();  // failed transfers through the functions above, since start-up
// Serial "i2c test": reads known IDs from the codec and the touch controller many times at the
// given speed and prints how many transfers failed or came back wrong.
void i2c_test(uint32_t scl_hz, int tries);
class I2cLock {
 public:
  I2cLock();
  ~I2cLock();
  bool ok() const { return ok_; }

 private:
  bool ok_;
};

// ---- IO expander (CH32V003 at 0x24). Pins: EXIO2 panel+backlight enable, EXIO3 speaker amp. ----
constexpr uint8_t kExioDisp = 2, kExioAmp = 3, kExioSdCs = 4;
bool expander_begin();  // after Waveshare's IO_EXTENSION_Init and touch reset
bool expander_output(uint8_t pin, bool on);
bool expander_read(uint8_t reg, uint8_t &value);  // 0x04: the pins' levels
bool expander_backlight(uint8_t reg);  // PWM register: lower is brighter, 255 dark (measured 2026-10-21)

// ---- SHT40 temperature and humidity sensor (0x44) on the screw terminal: VOUT, GND, SDA, SCL. ----
// sht40_begin: true if a SHT40 answers with a valid serial number (it may be plugged in later, so
// it can be called again). sht40_measure: about 10 ms; the I2C lock is not held while it waits.
bool sht40_begin(uint32_t *serial = nullptr);
bool sht40_measure(int16_t &temp_tenths_f, uint16_t &hum_tenths);

// ---- PCF85063A clock. 2000-based year, 24 h, UTC. ----
struct RtcTime {
  bool valid;    // false when the oscillator-stop flag is set (power was lost with no coin cell)
  uint32_t utc;
};
RtcTime rtc_read();
bool rtc_write(uint32_t utc);

// ---- Saved settings and state (NVS via Preferences). ----
void storage_begin();
bool load_settings(tc::Settings &s);
void save_settings(const tc::Settings &s);
bool load_own(tc::OwnState &s);
void save_own(const tc::OwnState &s);
bool load_dog(tc::DogState &d);
void save_dog(const tc::DogState &d);
bool load_mail(tc::MailState &m);
void save_mail(const tc::MailState &m);
bool load_comfort_count(uint32_t &n);  // the comfort survey's answer count (comfort.h)
void save_comfort_count(uint32_t n);
// The comfort answers and removals still waiting for upstairs' ACK (Mike, 2026-11-07), oldest first.
// load: false (and n = 0) if nothing is saved or anything in it isn't valid.
bool load_comfort_pending(tc::ComfortPending *out, int max, int &n);
void save_comfort_pending(const tc::ComfortPending *p, int n);
bool load_requests(tc::RequestState &s);  // the heat and cooling arrows held (requests.h)
void save_requests(const tc::RequestState &s);
// The two floors' tile messages (note.h, Mike 2026-11-07), [0] upstairs, [1] downstairs, saved together.
// load: false if nothing is saved, or what is saved has text with no NUL in it.
bool load_notes(tc::NoteState *notes2);
void save_notes(const tc::NoteState *notes2);
// The voice-message inbox and its last 8 ids (voice_proto.h, docs/voice-notes/DESIGN.md 2.3), saved on
// every change. load: false if nothing is saved or tc::voice_inbox_valid refuses it. (Piece B.)
bool load_voice_inbox(tc::VoiceInboxData &d);
void save_voice_inbox(const tc::VoiceInboxData &d);
uint8_t load_clock_quality();
void save_clock_quality(uint8_t q);
// The calendar's files (tc::Platform::read_file names), in /tcomm on the SD card (Mike,
// 2026-10-25: rely on the card). Without a readable card they go on the 9.9 MB FAT partition
// instead, and when the card is readable again they're copied onto it. Each write goes to a new
// file that is then renamed over the old one, so a power cut mid-write keeps the old file.
// calendar_fs_begin: after expander_begin (the card's D3 line is on EXIO4).
void calendar_fs_begin();
uint8_t *calendar_read(const char *name, size_t &len);  // a PSRAM buffer to free(), or null
bool calendar_write(const char *name, const uint8_t *buf, size_t len);
// The first column of the file's last complete line, as a number (0 if none or not a number); only
// the file's last few hundred bytes are read. Mike, 2026-11-07: the comfort count's safety net.
uint32_t calendar_last_number(const char *name);
// A name with a dot in it ("comfort.csv") is the whole file name; others get ".bin".
// calendar_append adds one line to the end, header first if the file is new or empty. A plain append,
// not temp and rename: a power cut at worst cuts the last line short.
bool calendar_append(const char *name, const char *header, const char *line);
// calendar_set_field: in the line whose first column is n, replaces column `column` (0-based) with
// value, as calendar_write does it (a temporary file renamed over the old one, so a power cut keeps
// the old file). The file is copied line by line, never held whole. Mike, 2026-11-06: a comfort
// answer's "removed" column, from the chart's REMOVE. False if there's no such line or column.
bool calendar_set_field(const char *name, uint32_t n, int column, const char *value);
struct CalendarFs {
  bool sd, fat;               // the card is in use; the FAT partition is mounted
  uint64_t card, total, used;  // bytes: the card's size, then the file system in use
  int files;                   // in /tcomm
};
bool calendar_fs_info(CalendarFs &out);
// A card that fails while running: the write is tried again after a remount (at most one a minute),
// and if that fails too the calendar's files move to the FAT partition for the rest of the run. A
// marker file there makes the next start-up with a readable card merge them onto it.
// storage_on_fallback: the files are on the FAT partition (the card was unreadable at start-up, or
// failed since). storage_write_failures: write tries that failed this run, even those a retry saved.
bool storage_on_fallback();
uint32_t storage_write_failures();
// For tests over serial: 1 makes the card fail for good (writes fail, the remount fails, the FAT
// partition takes over; a restart undoes it), 2 fails one try (the remount works), 0 is normal.
void storage_test_fail(int mode);
// Serial "sd": what the card is. "sd format yes": formats the card as FAT32, only if it can't be
// mounted as it is (an exFAT card can't: this core reads FAT only), then moves the calendar's
// files onto it. UNVERIFIED on the panel.
void sd_report();
bool sd_format_if_unreadable();
// The display test's choice (Settings > Test): pixel clock and copy-buffer preset, and whether
// the artwork is drawn in internal-RAM strips. 0xFF when never chosen.
uint8_t load_display_test();
void save_display_test(uint8_t v);

// ---- Audio: the synthesized sounds, played from their own task. ----
bool audio_begin(uint8_t ringtone);  // after Waveshare's codec_init()
// preview: a sample from Settings, cut short by whatever is asked for next.
// tc::Sound::Ding (voice message arrived) is rendered when asked for and freed after playing; while a
// recording runs it waits like every sound but the doorbell Chime (DESIGN 3.4, 3.5; piece A).
void audio_play(tc::Sound s, uint8_t volume, bool preview = false);
void audio_set_ringtone(uint8_t ringtone);
void audio_report();  // one line on Serial: plays, errors, the ringtone held and chosen
// For tests over serial: what the microphones hear for ms (RMS of each, and the peak, in dBFS);
// whether the amp stays on between sounds; closing and reopening the codec.
bool audio_listen(uint32_t ms, float level_db[2], float &peak_db);
void audio_amp_hold(bool hold);
void audio_reopen_codec();
void audio_log_state();  // the amp pin and the codec's registers, into the event log

// ---- Voice capture (docs/voice-notes/DESIGN.md 3.2; piece A, board_audio.cpp). ----
// The mic task stays the only I2S reader. LVGL builds a tc::VoiceRec (voice_proto.h) and hands it over;
// from then on the mic task resamples, encodes, makes each chunk's first send (radio_send_voice to
// rec->to, never 32 or more chunks past rec->base) and sends END itself when it stops.
// voice_rec_begin: publishes rec (a release store of g_rec). False if a recording is already running
// or the mic task is not running; then rec is still the caller's.
// voice_rec_stop: asks the mic task to stop (rec->stop = why, if no stop was asked yet); it flushes the
// last chunk, sends it and END, and sets rec->done.
// voice_rec_release: takes rec back (g_rec = nullptr). True once the mic task is not inside it and never
// will be again: only then free it. False: call again from the next tick (never a busy loop). Also
// clears the doorbell flag (g_rec_bell) that stopped this recording.
bool voice_rec_begin(tc::VoiceRec *rec);
void voice_rec_stop(tc::VoiceRec *rec, tc::VoiceStop why);
bool voice_rec_release(tc::VoiceRec *rec);
bool voice_recording();  // a VoiceRec is published and not yet done
// Field fix 2026-11-21 (board_audio.cpp):
// audio_mic_recent: the mic task's blocks of the last ms (peak and RMS in dBFS), no waiting and no
// capture; the badge row's live meter. False if none arrived.
// voice_set_boost / voice_boost: the level boost before encoding (tc::kVoiceBoostDb, 0-30 dB), from the
// next recording on, until restart (serial `voice boost N`).
// mic_raw_dump: serial `mic raw [ms]`, the raw 44.1 kHz stereo blocks (50-1500 ms) as hex lines with the
// I2S and ES7210 settings; loop task, outside the LVGL lock.
bool audio_mic_recent(uint32_t ms, float &peak_db, float &rms_db);
void voice_set_boost(int db);
int voice_boost();
void voice_set_treble(int db);  // the treble restore (tc::kVoiceTrebleDb, 0-20 dB), serial `voice treble N`
int voice_treble();
void mic_raw_dump(uint32_t ms);

// ---- Event log: the last 64 events with their time, for serial "log" (board_log.cpp). ----
// Any task may call elog; only the loop task writes to USB serial (elog_flush, elog_print).
void elog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void elog_flush();  // new lines to serial, from loop()
void elog_print();

// ---- ESP-NOW (phase 3; off unless TCOMM_RADIO_ESPNOW). ----
// Which floor this board is, from its Wi-Fi station MAC and the table in secrets.h: 0 upstairs,
// 1 downstairs, -1 not listed (or radio off). With the radio on, this decides the floor.
int radio_own_floor();
bool radio_begin();
void radio_send(uint8_t to, const uint8_t *data, size_t len);
// Voice frames (types 20-30; DESIGN 4.6; piece B, board_radio.cpp): true when esp_now_send took it. No
// trace-ring entry and no elog per error; counted as voice_out and voice_err for serial "radio". Safe
// from the mic task, the relay task and the LVGL task. on_recv counts voice frames in as voice_in (no
// trace) and, on upstairs, queues the ones tc::voice_relay_frame says to pass on for the "vrelay" task.
bool radio_send_voice(uint8_t to, const uint8_t *data, size_t len);
bool radio_poll(uint8_t *out, size_t *len);  // one received frame from a known peer, if any
void radio_report();  // for serial "radio": counts, and the last frames in and out

// ---- Wi-Fi updates (board_update.cpp, docs/wifi-updates.md). ----
// The updater is its own small sketch (updater) in the app slot this firmware isn't
// running from, and it takes new firmware over Wi-Fi. Whether it's there, and restarting into it.
bool updater_installed();
void restart_into_updater();  // returns only if there's no updater to start
// First thing in setup(): three crash restarts in a row start the updater, if it's installed, so a
// build that crashes at start-up can still be replaced over Wi-Fi. crash_guard_healthy() after a
// minute of normal running clears the count.
void crash_guard_begin();
void crash_guard_healthy();

}  // namespace board
