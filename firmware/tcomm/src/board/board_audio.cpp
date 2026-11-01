// Sounds are synthesized into PSRAM (no microSD needed) and played from a task so the interface
// never waits on I2S. Only the chosen ringtone is held: choosing another renders it in its place
// (a fraction of a second, logged) before it plays. A preview stops early when anything newer
// is asked for, so tapping through the ringtone list doesn't queue them all up.
//
// The microphones sit a few centimetres from the speaker, and a task listens all the time, so
// every sound is checked: one the microphones didn't hear is logged with the state of the amp pin
// and the codec, and the next ones get a recovery step each (see recover()).
// MIKE (2026-10-24): after a few quick taps in the ringtone list the sound stopped until a restart.
// The check above caught it: this task was stuck in a USB serial write (the TX timeout of 0 never
// gives up in core 3.2.0, see setup() in tcomm.ino). It no longer writes to serial at all; it logs.
#include <Arduino.h>
#include <math.h>
#include "../core/synth.h"
#include "../ws/speaker_microphone/codec_dev.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace board {

struct Clip {
  int16_t *pcm = nullptr;  // mono
  size_t frames = 0, cap = 0;
};
// Indexed by tc::Sound; [0] holds the chosen ringtone's chime, in a buffer its own size.
static Clip g_clips[4];
static QueueHandle_t g_q = nullptr;
static volatile uint8_t g_ringtone = 0;  // chosen (set from the interface task)
static uint8_t g_rendered = 0xFF;        // in g_clips[0]

// The amp stays on through a run of sounds and goes off once nothing has played for kAmpHoldMs,
// rather than going on and off around every sound. Serial "amp hold 0" brings back the old way.
constexpr uint32_t kAmpHoldMs = 2000;
static bool g_amp_on = false;
static uint32_t g_amp_idle_since = 0;
static volatile bool g_amp_hold = true;
constexpr uint8_t kReqReopen = 0xF0;  // not a sound: close and reopen the codec (serial "codec reopen")

// Written by the audio task only; the log reads them as they are.
static uint32_t g_plays = 0, g_cut = 0, g_write_errors = 0, g_amp_errors = 0, g_silent = 0, g_heard = 0;
static int g_last_error = 0;
static uint8_t g_last_sound = 0xFF, g_last_volume = 0;
static int g_silent_run = 0;  // silent sounds in a row

// ---- Listening ---------------------------------------------------------------------------------

// Peak and power of each 512-frame block the microphones deliver (about 12 ms), stamped with the
// time the block arrived. About two seconds of history.
struct MicBlock {
  uint32_t t;
  uint16_t peak;
  float power;  // mean square, full scale = 1
};
static constexpr int kMicBlocks = 160;
static MicBlock g_mic[kMicBlocks];
static volatile int g_mic_next = 0;
static volatile bool g_mic_pause = false, g_mic_paused = false;
// A sound counts as heard if the microphones peak above this while it plays. At volume 60 the
// chime peaks near -4 dBFS at the microphones and the room near -48 (measured 2026-10-24).
constexpr float kHeardDb = -38.0f;

static void mic_task(void *) {
  static int16_t buf[1024];  // 512 stereo frames
  for (;;) {
    if (g_mic_pause) {
      g_mic_paused = true;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    g_mic_paused = false;
    size_t got = 0;
    if (mic_i2s_read(buf, sizeof(buf), &got, 100) != 0) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    int peak = 0;
    double sum = 0;
    for (int k = 0; k < 1024; k++) {
      int v = buf[k];
      sum += (double)v * v;
      if (abs(v) > peak) peak = abs(v);
    }
    MicBlock &b = g_mic[g_mic_next];
    b.t = millis();
    b.peak = (uint16_t)(peak > 65535 ? 65535 : peak);
    b.power = (float)(sum / 1024 / (32768.0 * 32768.0));
    g_mic_next = (g_mic_next + 1) % kMicBlocks;
  }
}

// What the microphones heard in blocks that arrived between t0 and t1. False if none did.
static bool mic_window(uint32_t t0, uint32_t t1, float &peak_db, float &rms_db) {
  int peak = 0, n = 0;
  double power = 0;
  for (int i = 0; i < kMicBlocks; i++) {
    const MicBlock &b = g_mic[i];
    if (b.t == 0 || (int32_t)(b.t - t0) < 0 || (int32_t)(b.t - t1) > 0) continue;
    if (b.peak > peak) peak = b.peak;
    power += b.power;
    n++;
  }
  if (!n) return false;
  peak_db = 20.0f * log10f(peak / 32768.0f + 1e-9f);
  rms_db = 10.0f * log10f((float)(power / n) + 1e-12f);
  return true;
}

// ---- Rendering ---------------------------------------------------------------------------------

// Synthesis takes a few hundred ms per sound; give the idle task a tick every 50 ms so the task
// watchdog (5 s, checks IDLE0) is fed.
static void let_idle_run() {
  static uint32_t last = 0;
  if (millis() - last >= 50) {
    vTaskDelay(1);
    last = millis();
  }
}

struct Req {
  uint8_t sound, volume;
  bool preview;
};

static bool render(Clip &c, tc::Sound s, uint8_t ringtone) {
  size_t n = tc::synth_frames(s, ringtone);
  // Room for exactly this sound. The chime's buffer used to stay at the longest ringtone's size
  // (Westminster, 736 KB) whichever was chosen; Cardinal needs 213 KB (2026-11-01). The new buffer
  // comes first, so the old sound survives if there's no room; failing that, the old one goes first.
  if (n && (!c.pcm || c.cap != n)) {
    int16_t *p = (int16_t *)heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!p && c.pcm) {
      heap_caps_free(c.pcm);
      c.pcm = nullptr;
      c.cap = c.frames = 0;
      p = (int16_t *)heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    }
    if (!p) {
      elog("no memory for sound %d", (int)s);
      return false;
    }
    if (c.pcm) heap_caps_free(c.pcm);
    c.pcm = p;
    c.cap = n;
  }
  c.frames = 0;
  if (n == 0 || !tc::synth(s, ringtone, c.pcm, n, let_idle_run)) {
    elog("sound %d (ringtone %u) not made", (int)s, (unsigned)ringtone);
    return false;
  }
  c.frames = n;
  return true;
}

// Renders the chosen ringtone if it isn't the one held. Once per choice, even if it fails.
static void refresh_chime() {
  uint8_t want = g_ringtone;
  if (want == g_rendered) return;
  uint32_t t0 = millis();
  if (render(g_clips[0], tc::Sound::Chime, want))
    elog("ringtone %s ready in %lu ms", tc::ringtone_name(want), (unsigned long)(millis() - t0));
  g_rendered = want;
}

// ---- Playing -----------------------------------------------------------------------------------

// Codec calls return esp_codec_dev's codes (0 or negative), whatever their declared type says.
static bool sound_error(const char *what, int e) {
  if (e == 0) return false;
  g_write_errors++;
  g_last_error = e;
  elog("%s failed: %d", what, e);
  return true;
}

// Switching on always writes, even when the amp should already be on: a write can be lost on the
// bus, and the next sound puts it right. A failed switch-off is tried again after another hold.
static void amp(bool on) {
  if (!on && !g_amp_on) return;
  bool was_on = g_amp_on;
  if (!expander_output(kExioAmp, on)) {
    g_amp_errors++;
    elog("amp didn't switch %s", on ? "on" : "off");
    if (!on) g_amp_idle_since = millis();
    return;
  }
  g_amp_on = on;
  if (on && !was_on) vTaskDelay(pdMS_TO_TICKS(30));  // UNVERIFIED settle time; Mike heard no pop in the bring-up test
}

static void reopen_codec() {
  g_mic_pause = true;  // the reopen closes the microphones' side too
  for (int i = 0; i < 10 && !g_mic_paused; i++) vTaskDelay(pdMS_TO_TICKS(10));
  int e = (int)speaker_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, (i2s_slot_mode_t)CODEC_DEFAULT_CHANNEL);
  g_mic_pause = false;
  elog("codec reopened: %d", e);
}

// What the parts that make sound say about themselves, into the log.
static void log_state() {
  uint8_t in = 0, mode = 0;
  bool in_ok = expander_read(0x04, in), mode_ok = expander_read(0x02, mode);
  elog("expander: pins %s%02X (amp pin %s), mode %s%02X", in_ok ? "" : "unread ", in, (in >> kExioAmp) & 1 ? "high" : "low",
       mode_ok ? "" : "unread ", mode);
  char line[3 * 18 + 1];
  for (int part = 0; part < 4; part++) {  // ES8311 registers 00 to 45, 18 to a line
    int n = 0;
    for (int r = part * 18; r < part * 18 + 18; r++) {
      int v = -1;
      if (speaker_codec_read_reg(r, &v) != 0) v = -1;
      n += snprintf(line + n, sizeof(line) - n, v < 0 ? "-- " : "%02X ", v & 0xFF);
    }
    elog("codec %02X-%02X: %s", part * 18, part * 18 + 17, line);
  }
}

// After a silent sound: first reopen the codec; if the next is silent too, set the expander up
// again as well; after that keep doing both. Each step is logged, so the log says which one worked.
static void recover() {
  if (g_silent_run == 1) {
    log_state();
    reopen_codec();
  } else {
    if (g_silent_run == 2) log_state();
    bool ok = expander_begin();
    elog("expander set up again: %s", ok ? "ok" : "failed");
    g_amp_on = false;
    reopen_codec();
  }
}

static const char *sound_name(int s) {
  static const char *const kNames[] = {"chime", "soft chime", "come on", "not now"};
  return s >= 0 && s < 4 ? kNames[s] : "?";
}

static void play_clip(const Clip &c, uint8_t volume, bool preview) {
  static int16_t stereo[2 * 512];  // Waveshare's I2S setup is 16-bit stereo at 44.1 kHz
  g_plays++;
  int which = (int)(&c - g_clips);
  g_last_sound = (uint8_t)which;
  g_last_volume = volume;
  amp(true);
  sound_error("codec volume", (int)speaker_codec_volume_set(volume, nullptr));
  uint32_t t_start = millis();
  size_t played = 0;
  bool cut = false;
  for (size_t i = 0; i < c.frames; i += 512) {
    size_t n = c.frames - i < 512 ? c.frames - i : 512;
    // A preview gives way to anything newer: this last chunk fades to silence (12 ms, no click).
    bool stop = preview && uxQueueMessagesWaiting(g_q) > 0;
    for (size_t k = 0; k < n; k++) {
      int16_t v = c.pcm[i + k];
      if (stop) v = (int16_t)(v * (int32_t)(n - k) / (int32_t)n);
      stereo[2 * k] = stereo[2 * k + 1] = v;
    }
    size_t written = 0;
    if (sound_error("sound write", (int)speaker_i2s_write(stereo, n * 4, &written, 1000))) break;
    played += n;
    if (stop) {
      g_cut++;
      cut = true;
      break;
    }
  }
  uint32_t t_end = millis();
  g_amp_idle_since = t_end;

  // Judge it by what the microphones heard: the last second of it, allowing for the DMA buffers
  // on the way out and on the way in. Too short a sound (a preview cut at once) isn't judged.
  const char *name = which == 0 ? tc::ringtone_name(g_rendered) : sound_name(which);
  if (played >= 11025) {
    vTaskDelay(pdMS_TO_TICKS(110));
    uint32_t w0 = t_start + 60;
    if ((int32_t)(t_end - 1000 - w0) > 0) w0 = t_end - 1000;
    float peak = -99, rms = -99;
    bool measured = mic_window(w0, t_end + 100, peak, rms);
    bool heard = measured && peak >= kHeardDb;
    elog("play %s vol %u%s: %.2f s%s, mic peak %.1f rms %.1f dBFS%s", name, (unsigned)volume, preview ? " (preview)" : "",
         played / 44100.0f, cut ? " cut" : "", peak, rms, !measured ? " NOT MEASURED" : (heard ? "" : " SILENT"));
    if (measured && heard) {
      g_heard++;
      if (g_silent_run) elog("sound back after %d silent", g_silent_run);
      g_silent_run = 0;
    } else if (measured) {
      g_silent++;
      g_silent_run++;
      recover();
    }
  }

  if (!g_amp_hold) {
    vTaskDelay(pdMS_TO_TICKS(80));  // let the DMA buffers drain before the amp goes off
    amp(false);
  }
}

static void audio_task(void *) {
  // The doorbell chime first, so it's ready soonest; each clip works on its own if another fails.
  uint32_t t0 = millis();
  refresh_chime();
  render(g_clips[1], tc::Sound::SoftChime, 0);
  render(g_clips[2], tc::Sound::ReplyYes, 0);
  render(g_clips[3], tc::Sound::ReplyNo, 0);
  elog("sounds ready in %lu ms", (unsigned long)(millis() - t0));
  Req r;
  for (;;) {
    // A newly chosen ringtone is rendered straight away, so the next doorbell doesn't wait for it,
    // and the amp goes off once nothing has played for a while.
    TickType_t wait = portMAX_DELAY;
    if (g_ringtone != g_rendered) {
      wait = pdMS_TO_TICKS(100);
    } else if (g_amp_on) {
      uint32_t idle = millis() - g_amp_idle_since;
      wait = idle >= kAmpHoldMs ? 0 : pdMS_TO_TICKS(kAmpHoldMs - idle);
    }
    if (xQueueReceive(g_q, &r, wait) != pdTRUE) {
      refresh_chime();
      if (g_amp_on && millis() - g_amp_idle_since >= kAmpHoldMs) amp(false);
      continue;
    }
    if (r.sound == kReqReopen) {
      amp(false);
      reopen_codec();
      continue;
    }
    // Of several previews waiting (taps through the list), only the newest plays.
    Req next;
    while (r.preview && xQueuePeek(g_q, &next, 0) == pdTRUE && next.preview) xQueueReceive(g_q, &r, 0);
    if (r.sound == (uint8_t)tc::Sound::Chime) refresh_chime();
    int which = r.sound;
    // A doorbell is never silent: with no chime made (no room for a newly chosen ringtone), the
    // soft chime plays instead.
    if (which == 0 && !g_clips[0].frames && !r.preview) which = 1;
    if (which > 3 || !g_clips[which].frames) continue;  // not made yet, or failed
    play_clip(g_clips[which], r.volume, r.preview);
  }
}

bool audio_begin(uint8_t ringtone) {
  g_ringtone = tc::ringtone_index(ringtone) >= 0 ? ringtone : 0;
  expander_output(kExioAmp, false);  // Waveshare's codec init leaves it on
  g_q = xQueueCreate(8, sizeof(Req));
  if (!g_q) return false;
  // The listener above the audio task: it mostly waits on the microphones, and must keep up.
  if (xTaskCreatePinnedToCore(mic_task, "mic", 3072, nullptr, 4, nullptr, 0) != pdPASS) elog("no microphone task");
  return xTaskCreatePinnedToCore(audio_task, "audio", 4096, nullptr, 3, nullptr, 0) == pdPASS;
}

void audio_play(tc::Sound s, uint8_t volume, bool preview) {
  if (!g_q) return;
  Req r = {(uint8_t)s, volume, preview};
  if (xQueueSend(g_q, &r, 0) != pdTRUE) elog("sound queue full");
}

void audio_reopen_codec() {
  if (!g_q) return;
  Req r = {kReqReopen, 0, false};
  xQueueSend(g_q, &r, 0);
}

void audio_amp_hold(bool hold) { g_amp_hold = hold; }

bool audio_listen(uint32_t ms, float level_db[2], float &peak_db) {
  uint32_t t0 = millis();
  vTaskDelay(pdMS_TO_TICKS(ms + 40));
  float rms = -99;
  if (!mic_window(t0 + 40, millis(), peak_db, rms)) return false;
  level_db[0] = level_db[1] = rms;
  return true;
}

void audio_log_state() { log_state(); }

void audio_report() {
  Serial.printf("audio: %lu plays (%lu cut short, %lu heard, %lu SILENT, %d silent in a row), last sound %d at "
                "volume %u; %lu codec errors (last %d), %lu amp errors, amp %s (hold %s); ringtone %s held, %s "
                "chosen; %u waiting\n",
                (unsigned long)g_plays, (unsigned long)g_cut, (unsigned long)g_heard, (unsigned long)g_silent,
                g_silent_run, g_last_sound == 0xFF ? -1 : (int)g_last_sound, (unsigned)g_last_volume,
                (unsigned long)g_write_errors, g_last_error, (unsigned long)g_amp_errors, g_amp_on ? "on" : "off",
                g_amp_hold ? "on" : "off", g_rendered != 0xFF ? tc::ringtone_name(g_rendered) : "none",
                tc::ringtone_name(g_ringtone), g_q ? (unsigned)uxQueueMessagesWaiting(g_q) : 0u);
}

void audio_set_ringtone(uint8_t ringtone) { g_ringtone = tc::ringtone_index(ringtone) >= 0 ? ringtone : 0; }

}  // namespace board
