// Sounds are synthesized into PSRAM (no microSD needed) and played from a task so the interface
// never waits on I2S. Only the chosen ringtone is held: choosing another renders it in its place
// (a fraction of a second, logged) before it plays. A preview stops early when anything newer
// is asked for, so tapping through the ringtone list doesn't queue them all up.
//
// Voice notes (docs/voice-notes/DESIGN.md 3): the same listening task also records a message when
// the LVGL task hands it a tc::VoiceRec (core/voice_dsp.h does the work in plain C++; this file only
// feeds it blocks), and the audio task defers every sound but the doorbell while that runs and
// renders the arrival ding on demand.
//
// The microphones sit a few centimetres from the speaker, and a task listens all the time, so
// every sound is checked: one the microphones didn't hear is logged with the state of the amp pin
// and the codec, and the next ones get a recovery step each (see recover()).
// MIKE (2026-10-24): after a few quick taps in the ringtone list the sound stopped until a restart.
// The check above caught it: this task was stuck in a USB serial write (the TX timeout of 0 never
// gives up in core 3.2.0, see setup() in tcomm.ino). It no longer writes to serial at all; it logs.
#include <Arduino.h>
#include <math.h>
#include <new>
#include "../core/synth.h"
#include "../core/voice_dsp.h"
#include "../core/voice_proto.h"
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
static Clip g_clips[5];
static QueueHandle_t g_q = nullptr;
static volatile uint8_t g_ringtone = 0;  // chosen (set from the interface task)
static uint8_t g_rendered = 0xFF;        // in g_clips[0]

// Voice capture (DESIGN 3.2). g_link is the g_rec / g_rec_busy handshake plus the doorbell flag
// (g_rec_bell) and the recording flag the audio task reads; g_cap is used by the mic task only. g_cap
// (about 2.2 KB) lives in PSRAM, made by audio_begin (field fix 2026-11-20: off internal RAM).
static tc::VoiceHandshake g_link;
static tc::VoiceCapture *g_cap = nullptr;
static tc::VoicePreBlock *g_pre = nullptr;  // pre-roll ring storage, PSRAM
static TaskHandle_t g_mic_handle = nullptr;
static volatile bool g_mic_alive = false;   // the mic task has read at least one block
static std::atomic<bool> g_chime_playing{false};
// Time the voice tap takes in the mic task (microseconds): while recording, and when idle (only
// the pre-roll copy). Written by the mic task, read by "info".
static uint32_t g_tap_max_rec = 0, g_tap_max_idle = 0, g_tap_n_rec = 0, g_tap_n_idle = 0;
static uint64_t g_tap_sum_rec = 0, g_tap_sum_idle = 0;

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

static bool voice_send_hook(void *, uint8_t to, const uint8_t *d, size_t n) { return radio_send_voice(to, d, n); }

// Serial "mic raw [ms]" (field diagnosis 2026-11-21: almost no energy above 1 kHz reaches the Pi). The
// mic task copies its I2S blocks as they are (44.1 kHz, both slots, no downmix, no filter) into a PSRAM
// buffer the loop task made, until it is full. Only these few words are in internal RAM.
static int16_t *volatile g_raw = nullptr;    // interleaved L R, g_raw_cap frames
static volatile uint32_t g_raw_cap = 0;
static std::atomic<uint32_t> g_raw_fill{0};  // frames copied
static std::atomic<bool> g_raw_on{false};    // the loop task arms it; the mic task clears it when full

static void raw_copy(const int16_t *buf, uint32_t frames) {
  uint32_t fill = g_raw_fill.load(std::memory_order_relaxed);
  uint32_t n = g_raw_cap - fill;
  if (n > frames) n = frames;
  if (n) memcpy(g_raw + 2 * fill, buf, (size_t)n * 4);
  g_raw_fill.store(fill + n, std::memory_order_release);
  if (fill + n >= g_raw_cap) g_raw_on.store(false, std::memory_order_release);
}

// After each block: the voice tap. Brackets every use of the published VoiceRec with enter/leave so
// LVGL can free it safely (VoiceHandshake). Idle (nothing published) it only keeps the pre-roll ring fresh.
static void voice_tap(const int16_t *buf, uint32_t now, float power) {
  if (!g_cap) return;  // no memory at start-up: no voice capture (voice_rec_begin refuses too)
  uint32_t t0 = micros();
  uint32_t gen = 0;
  tc::VoiceRec *r = g_link.enter(gen);
  bool fin = g_cap->block(r, gen, buf, 512, now, power, g_link.bell());
  if (fin) g_link.finished();  // the audio task may play its deferred sounds now
  g_link.leave();
  uint32_t us = micros() - t0;
  if (r) {
    if (us > g_tap_max_rec) g_tap_max_rec = us;
    g_tap_sum_rec += us;
    g_tap_n_rec++;
  } else {
    if (us > g_tap_max_idle) g_tap_max_idle = us;
    g_tap_sum_idle += us;
    g_tap_n_idle++;
  }
}

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
    int64_t sum = 0;  // int64: 1024 squares of up to 2^30 do not fit 32 bits (audit 2026-11-11)
    for (int k = 0; k < 1024; k++) {
      int v = buf[k];
      sum += (int64_t)v * v;
      if (abs(v) > peak) peak = abs(v);
    }
    MicBlock &b = g_mic[g_mic_next];
    b.t = millis();
    b.peak = (uint16_t)(peak > 65535 ? 65535 : peak);
    b.power = (float)((double)sum / 1024.0 / (32768.0 * 32768.0));
    g_mic_next = (g_mic_next + 1) % kMicBlocks;
    g_mic_alive = true;
    if (g_raw_on.load(std::memory_order_acquire)) raw_copy(buf, 512);  // serial "mic raw" only: a 2 KB copy
    voice_tap(buf, b.t, b.power);
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
  static const char *const kNames[] = {"chime", "soft chime", "come on", "not now", "dog alert", "ding"};
  return s >= 0 && s < 6 ? kNames[s] : "?";
}

// which: the Sound it is (the g_clips index for a resident clip; the Ding is a temporary clip).
static void play_clip(const Clip &c, int which, uint8_t volume, bool preview) {
  static int16_t stereo[2 * 512];  // Waveshare's I2S setup is 16-bit stereo at 44.1 kHz
  g_plays++;
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

// The arrival ding (DESIGN 3.5): rendered when asked for, played, freed. About 66 KB of PSRAM for
// about a second. With no room the soft chime plays instead, so an arrival is never silent.
static void play_ding(uint8_t volume) {
  Clip c;
  uint32_t t0 = millis();
  if (render(c, tc::Sound::Ding, 0)) {
    elog("ding rendered in %lu ms (%u KB)", (unsigned long)(millis() - t0), (unsigned)(c.cap * sizeof(int16_t) / 1024));
    play_clip(c, (int)tc::Sound::Ding, volume, false);
  } else if (g_clips[1].frames) {
    elog("ding: no room to render it, soft chime instead");
    play_clip(g_clips[1], 1, volume, false);
  }
  if (c.pcm) heap_caps_free(c.pcm);
}

// Sounds that arrive while a message is being recorded wait here (DESIGN 3.4), oldest first.
constexpr int kDeferMax = 4;

static void audio_task(void *) {
  // The doorbell chime first, so it's ready soonest; each clip works on its own if another fails.
  uint32_t t0 = millis();
  refresh_chime();
  render(g_clips[1], tc::Sound::SoftChime, 0);
  render(g_clips[2], tc::Sound::ReplyYes, 0);
  render(g_clips[3], tc::Sound::ReplyNo, 0);
  render(g_clips[4], tc::Sound::DogAlert, 0);
  elog("sounds ready in %lu ms", (unsigned long)(millis() - t0));
  Req r;
  Req deferred[kDeferMax];
  int n_deferred = 0;
  for (;;) {
    bool recording = g_link.recording();
    bool from_list = false;
    if (!recording && n_deferred > 0) {  // the recording is over: the waiting sounds play in order
      r = deferred[0];
      for (int i = 1; i < n_deferred; i++) deferred[i - 1] = deferred[i];
      n_deferred--;
      from_list = true;
    } else {
      // A newly chosen ringtone is rendered straight away, so the next doorbell doesn't wait for it,
      // and the amp goes off once nothing has played for a while. While recording the wait is at most
      // 50 ms, so a doorbell that arrives meanwhile is seen at once.
      TickType_t wait = portMAX_DELAY;
      if (g_ringtone != g_rendered) {
        wait = pdMS_TO_TICKS(100);
      } else if (g_amp_on) {
        uint32_t idle = millis() - g_amp_idle_since;
        wait = idle >= kAmpHoldMs ? 0 : pdMS_TO_TICKS(kAmpHoldMs - idle);
      }
      if (recording && wait > pdMS_TO_TICKS(50)) wait = pdMS_TO_TICKS(50);
      if (xQueueReceive(g_q, &r, wait) != pdTRUE) {
        refresh_chime();
        if (g_amp_on && millis() - g_amp_idle_since >= kAmpHoldMs) amp(false);
        continue;
      }
    }
    if (!from_list) {
      recording = g_link.recording();  // it may have started while this waited
      bool is_chime = r.sound == (uint8_t)tc::Sound::Chime;
      if (is_chime && recording) g_link.ring_bell();  // the mic task ends the recording on its next block
      if (recording && !is_chime) {
        if (n_deferred < kDeferMax) deferred[n_deferred++] = r;
        else elog("voice: sounds waiting for the recording are full, sound %d dropped", (int)r.sound);
        continue;
      }
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
    if (which == (int)tc::Sound::Ding) {  // not a resident clip: handled before the guard below
      play_ding(r.volume);
      continue;
    }
    if (which > 4 || !g_clips[which].frames) continue;  // not made yet, or failed
    if (which == 0) g_chime_playing.store(true);
    play_clip(g_clips[which], which, r.volume, r.preview);
    g_chime_playing.store(false);
  }
}

bool audio_begin(uint8_t ringtone) {
  g_ringtone = tc::ringtone_index(ringtone) >= 0 ? ringtone : 0;
  expander_output(kExioAmp, false);  // Waveshare's codec init leaves it on
  g_q = xQueueCreate(8, sizeof(Req));
  if (!g_q) return false;
  // Pre-roll ring (the last 70 ms of audio, DESIGN 3.4): a little PSRAM; without it no pre-roll.
  g_pre = (tc::VoicePreBlock *)heap_caps_malloc(sizeof(tc::VoicePreBlock) * tc::kVoicePreRollBlocks, MALLOC_CAP_SPIRAM);
  // The capture state in PSRAM too (field fix 2026-11-20); internal RAM only if PSRAM has no room.
  void *cm = heap_caps_malloc(sizeof(tc::VoiceCapture), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!cm) cm = malloc(sizeof(tc::VoiceCapture));
  if (cm) {
    tc::VoiceCapture *cap = new (cm) tc::VoiceCapture();
    tc::VoiceSendHook hook;
    hook.fn = voice_send_hook;
    cap->init(hook, g_pre, g_pre ? tc::kVoicePreRollBlocks : 0);
    g_cap = cap;  // before the mic task starts
  } else {
    elog("voice: no memory for the capture state, no recording");
  }
  // The listener above the audio task: it mostly waits on the microphones, and must keep up. 3072 bytes
  // of stack, as before the voice tap: the tap's deepest call (a chunk sent through esp_now_send) needs
  // about 1.2 KB with its message and frame buffers in VoiceCapture, not on this stack (field fix
  // 2026-11-20; it was 4096). The high-water mark is in "info".
  if (xTaskCreatePinnedToCore(mic_task, "mic", 3072, nullptr, 4, &g_mic_handle, 0) != pdPASS) elog("no microphone task");
  return xTaskCreatePinnedToCore(audio_task, "audio", 4096, nullptr, 3, nullptr, 0) == pdPASS;
}

void audio_play(tc::Sound s, uint8_t volume, bool preview) {
  if (!g_q) return;
  // A doorbell while a message is recorded ends the recording at once (the sound itself is never
  // delayed); the audio task only sets the same flag, it never touches the VoiceRec (DESIGN 3.4).
  if (s == tc::Sound::Chime && g_link.recording()) g_link.ring_bell();
  Req r = {(uint8_t)s, volume, preview};
  if (xQueueSend(g_q, &r, 0) != pdTRUE) elog("sound queue full");
}

void audio_reopen_codec() {
  if (!g_q) return;
  Req r = {kReqReopen, 0, false};
  xQueueSend(g_q, &r, 0);
}

void audio_amp_hold(bool hold) { g_amp_hold = hold; }

bool audio_mic_recent(uint32_t ms, float &peak_db, float &rms_db) {
  const uint32_t now = millis();
  return mic_window(now - ms, now, peak_db, rms_db);
}

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
  // Voice capture: the mic task's stack (3072 bytes) and what the voice tap costs a block (11.6 ms).
  if (!g_cap) {
    Serial.println("voice: no capture state (no memory at start-up)");
    return;
  }
  Serial.printf("voice: mic stack %u of 3072 bytes never used; tap while recording avg %lu max %lu us over %lu blocks, "
                "idle avg %lu max %lu us over %lu; %lu recordings (last stop %d, %d chunks, pre-roll %d blocks used), "
                "%lu send retries, %lu window holds; pre-roll ring %s\n",
                g_mic_handle ? (unsigned)uxTaskGetStackHighWaterMark(g_mic_handle) : 0u,
                (unsigned long)(g_tap_n_rec ? g_tap_sum_rec / g_tap_n_rec : 0), (unsigned long)g_tap_max_rec,
                (unsigned long)g_tap_n_rec, (unsigned long)(g_tap_n_idle ? g_tap_sum_idle / g_tap_n_idle : 0),
                (unsigned long)g_tap_max_idle, (unsigned long)g_tap_n_idle, (unsigned long)g_cap->recordings(),
                (int)g_cap->last_stop(), g_cap->last_chunks(), g_cap->preroll_used(), (unsigned long)g_cap->send_retries(),
                (unsigned long)g_cap->window_holds(), g_pre ? "held (6 KB)" : "NOT HELD");
}

void audio_set_ringtone(uint8_t ringtone) { g_ringtone = tc::ringtone_index(ringtone) >= 0 ? ringtone : 0; }

// ---- Voice capture API (DESIGN 3.2) ----

bool voice_rec_begin(tc::VoiceRec *rec) {
  if (!rec || !g_mic_alive || !g_cap) return false;
  if (!g_link.begin(rec)) return false;
  // A ring is sounding right now: the clip would be the bell, so the mic task ends it at once.
  if (g_chime_playing.load()) g_link.ring_bell();
  // Amp hiss (kAmpHoldMs): the amp is still on from a sound that ended under 2 s ago. Logged only.
  if (g_amp_on) elog("voice: recording starts while the amp is on (hiss in the first 0.3 s)");
  return true;
}

void voice_rec_stop(tc::VoiceRec *rec, tc::VoiceStop why) { g_link.stop(rec, why); }

bool voice_rec_release(tc::VoiceRec *rec) { return g_link.release(rec); }

bool voice_recording() { return g_link.recording(); }

void voice_set_boost(int db) {
  if (db < 0) db = 0;
  if (db > tc::kVoiceBoostMaxDb) db = tc::kVoiceBoostMaxDb;
  if (g_cap) g_cap->set_boost_db((float)db);
}

int voice_boost() { return g_cap ? (int)lrintf(g_cap->boost_db()) : 0; }

void voice_set_treble(int db) {
  if (db < 0) db = 0;
  if (db > tc::kVoiceTrebleMaxDb) db = tc::kVoiceTrebleMaxDb;
  if (g_cap) g_cap->set_treble_db((float)db);
}

int voice_treble() { return g_cap ? (int)lrintf(g_cap->treble_db()) : 0; }

// Serial "voice dump [seconds [gain dB]]": records for 1 to 3 seconds through the real capture
// path (no radio, no early stop; ADPCM whatever `voice codec` says, with the level boost), then prints the ADPCM chunks as hex in the format of
// tests/vectors/voice_adpcm_*_chunks.txt, so the laptop can decode them. The microphone gain (ES7210,
// 0 to 36 dB, default 24) is set for the recording and put back. Runs in the loop task, outside the
// LVGL lock, and takes seconds like "listen".
void voice_dump(uint32_t secs, int gain_db) {
  if (secs < 1) secs = 1;
  if (secs > 3) secs = 3;
  if (!g_mic_alive) {
    Serial.println("voice dump: the microphone task is not running");
    return;
  }
  // The clip, and after it the hex line buffer (in PSRAM with it, not a static in internal RAM).
  constexpr size_t kLine = 48 + 2 * tc::kVoiceChunkBytes;
  size_t bytes = tc::voice_rec_bytes(tc::kVoiceClipChunks);
  void *mem = heap_caps_malloc(bytes + kLine, MALLOC_CAP_SPIRAM);
  tc::VoiceRec *rec = mem ? tc::voice_rec_init(mem, bytes, tc::kVoiceClipChunks) : nullptr;
  if (!rec) {
    if (mem) heap_caps_free(mem);
    Serial.println("voice dump: not enough memory");
    return;
  }
  rec->xfer = 1;
  rec->to = tc::kVoiceToNone;  // capture only: nothing is sent, no ACK is waited for
  rec->early_stop = false;
  if (gain_db >= 0) {
    microphone_codec_gain_set(gain_db, nullptr);
    vTaskDelay(pdMS_TO_TICKS(60));
  }
  uint32_t t0 = millis();
  rec->press_ms = t0;
  if (!voice_rec_begin(rec)) {
    Serial.println("voice dump: a recording is already running");
  } else {
    Serial.printf("voice dump: speak now, %u s, gain %d dB\n", (unsigned)secs,
                  gain_db >= 0 ? gain_db : (int)CODEC_DEFAULT_ADC_VOLUME);
    vTaskDelay(pdMS_TO_TICKS(secs * 1000));
    voice_rec_stop(rec, tc::VoiceStop::StopKey);
    for (int i = 0; i < 100 && !rec->done.load(); i++) vTaskDelay(pdMS_TO_TICKS(10));
    uint32_t t1 = millis();
    float peak_db = -99, rms_db = -99;
    bool heard = mic_window(t0, t1, peak_db, rms_db);
    int n = rec->chunks_ready.load();
    Serial.setTxTimeoutMs(250);  // as "shot": a laptop reading the port must never miss a piece
    Serial.printf("VOICE DUMP %d chunks %u samples ADPCM boost %d dB (%lu samples limited) stop %u speech %d peak %.1f "
                  "rms %.1f dBFS%s\n", n, (unsigned)rec->n_samples.load(), voice_boost(),
                  (unsigned long)g_cap->last_limited(), (unsigned)rec->stopped.load(), rec->speech.load() ? 1 : 0, peak_db,
                  rms_db, heard ? "" : " NOT MEASURED");
    char *line = (char *)mem + bytes;
    for (int i = 0; i < n; i++) {
      const tc::VoiceChunk &c = rec->chunks[i];
      int k = snprintf(line, kLine, "%d %d %u %u ", i, (int)c.pred, (unsigned)c.step, (unsigned)c.len);
      for (int b = 0; b < c.len; b++) k += snprintf(line + k, kLine - k, "%02x", c.data[b]);
      Serial.println(line);
    }
    Serial.println("VOICE DUMP END");
    Serial.setTxTimeoutMs(2);
  }
  if (gain_db >= 0) microphone_codec_gain_set((int)CODEC_DEFAULT_ADC_VOLUME, nullptr);
  bool freed = false;
  for (int i = 0; i < 100 && !freed; i++) {
    freed = voice_rec_release(rec);
    if (!freed) vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (freed) {
    tc::voice_rec_destroy(rec);
    heap_caps_free(mem);
  } else {
    elog("voice dump: the microphone task did not let go, buffer kept");
  }
}

// Serial "mic raw [ms]" (field diagnosis 2026-11-21): the raw microphone blocks as the mic task reads
// them, so the laptop can rebuild a stereo WAV (tools/mic_raw_wav.py) and see which slot carries what and
// where the treble goes. Prints, in order:
//   MIC RAW I2S ...       the I2S setup as built (rate, slot mode and mask, widths, MCLK multiple)
//   MIC RAW ES7210 ...    the ES7210 setup that matters, from its registers as they are now
//   MIC RAW rate 44100 ch 2 bits 16 frames N gain G regs 00=.. 01=.. ... 4C=..   (the header)
//   <first frame> <hex>   64 frames a line, each frame L then R, int16 little-endian
//   MIC RAW END
//   MIC RAW STATS ...     per slot: peak, RMS, DC; and the L/R correlation (-1: one slot inverted)
// Takes ms + about 2 s; runs in the loop task, outside the LVGL lock. The always-on sound check keeps
// its blocks the whole time (the copy is beside it, 2 KB a block, only while armed).
void mic_raw_dump(uint32_t ms) {
  if (ms < 50) ms = 50;
  if (ms > 1500) ms = 1500;
  if (!g_mic_alive) {
    Serial.println("mic raw: the microphone task is not running");
    return;
  }
  if (g_raw_on.load()) {
    Serial.println("mic raw: a capture is already running");
    return;
  }
  const uint32_t frames = (uint32_t)((uint64_t)ms * CODEC_DEFAULT_SAMPLE_RATE / 1000);
  constexpr uint32_t kPerLine = 64;
  constexpr size_t kLine = 16 + kPerLine * 8 + 4;
  int16_t *pcm = (int16_t *)heap_caps_malloc((size_t)frames * 4 + kLine, MALLOC_CAP_SPIRAM);
  if (!pcm) {
    Serial.println("mic raw: not enough memory");
    return;
  }
  char *line = (char *)(pcm + 2 * frames);
  // The ES7210's registers first (I2C), so no bus traffic of ours overlaps the capture.
  static constexpr int kRegs = 0x4D;
  int regs[kRegs];
  for (int r = 0; r < kRegs; r++)
    if (microphone_codec_read_reg(r, &regs[r]) != 0) regs[r] = -1;
  // Capture.
  g_raw = pcm;
  g_raw_cap = frames;
  g_raw_fill.store(0);
  g_raw_on.store(true, std::memory_order_release);
  uint32_t t0 = millis();
  while (g_raw_on.load(std::memory_order_acquire) && millis() - t0 < ms + 1000) vTaskDelay(pdMS_TO_TICKS(10));
  if (g_raw_on.load()) {
    g_raw_on.store(false);
    vTaskDelay(pdMS_TO_TICKS(50));  // a copy in progress finishes (one block is 11.6 ms)
  }
  const uint32_t got = g_raw_fill.load(std::memory_order_acquire);
  g_raw = nullptr;
  Serial.setTxTimeoutMs(250);  // as "shot": a laptop reading the port must never miss a piece
  // The I2S setup exactly as speaker_microphone.cpp builds it (CODEC_DEFAULT_TDM 0: standard mode).
  const i2s_std_config_t ic = I2S_DUPLEX_MONO_CFG(CODEC_DEFAULT_SAMPLE_RATE);
  Serial.printf("MIC RAW I2S port %d std philips, tdm %d, rate %lu, mclk x%u (%lu Hz), slot mode %s, slot mask %u, data %u bit, "
                "slot %u bit, ws width %u, ws pol %u, bit shift %u; ESP32 master, ES7210 slave; mics 1+2 selected\n",
                (int)I2S_NUM, (int)CODEC_DEFAULT_TDM, (unsigned long)ic.clk_cfg.sample_rate_hz,
                (unsigned)ic.clk_cfg.mclk_multiple, (unsigned long)(ic.clk_cfg.sample_rate_hz * (uint32_t)ic.clk_cfg.mclk_multiple),
                ic.slot_cfg.slot_mode == I2S_SLOT_MODE_STEREO ? "stereo" : "mono", (unsigned)ic.slot_cfg.slot_mask,
                (unsigned)ic.slot_cfg.data_bit_width, (unsigned)ic.slot_cfg.slot_bit_width, (unsigned)ic.slot_cfg.ws_width,
                (unsigned)ic.slot_cfg.ws_pol, (unsigned)ic.slot_cfg.bit_shift);
  // es7210_config_sample() returns at once in slave mode, so 02 and 07 keep what es7210_open wrote (c1,
  // 20: adc_div 1, doubler and DLL on, OSR 32, the table's 11.2896 MHz / 44.1 kHz entry) and 04/05 (LRCK
  // divider, master only) are never written. Shown here as read back (-1: the read failed).
  auto rv = [&](int r) { return regs[r] < 0 ? -1 : (regs[r] & 0xFF); };
  const int m2 = rv(0x02) < 0 ? 0 : rv(0x02);
  Serial.printf("MIC RAW ES7210 01 clk off %02X, 02 mainclk %02X (adc_div %d, doubler %d, dll %d), 03 mclk src %02X, "
                "04/05 lrck div %02X %02X, 06 power down %02X, 07 osr %02X, 08 mode %02X (%s), 11 sdp1 %02X, 12 sdp2 %02X, "
                "20-23 hpf %02X %02X %02X %02X, 40 analog %02X, 41 bias12 %02X, 43-46 gain %02X %02X %02X %02X, "
                "47-4C power %02X %02X %02X %02X %02X %02X; gain setting %d dB\n",
                rv(0x01), rv(0x02), m2 & 0x1F, (m2 >> 6) & 1, (m2 >> 7) & 1, rv(0x03), rv(0x04), rv(0x05), rv(0x06), rv(0x07),
                rv(0x08), (rv(0x08) > 0 && (rv(0x08) & 1)) ? "master" : "slave", rv(0x11), rv(0x12), rv(0x20), rv(0x21),
                rv(0x22), rv(0x23), rv(0x40), rv(0x41), rv(0x43), rv(0x44), rv(0x45), rv(0x46), rv(0x47), rv(0x48), rv(0x49),
                rv(0x4A), rv(0x4B), rv(0x4C), microphone_codec_gain_get());
  // The header: everything to rebuild the WAV, and the whole register dump.
  {
    char hdr[64 + kRegs * 6];
    int k = snprintf(hdr, sizeof hdr, "MIC RAW rate %lu ch 2 bits 16 frames %lu gain %d regs",
                     (unsigned long)CODEC_DEFAULT_SAMPLE_RATE, (unsigned long)got, microphone_codec_gain_get());
    for (int r = 0; r < kRegs && k < (int)sizeof hdr - 7; r++)
      k += snprintf(hdr + k, sizeof hdr - k, regs[r] < 0 ? " %02X=--" : " %02X=%02X", r, regs[r] & 0xFF);
    Serial.println(hdr);
  }
  static const char kHex[] = "0123456789abcdef";
  for (uint32_t f = 0; f < got; f += kPerLine) {
    uint32_t n = got - f < kPerLine ? got - f : kPerLine;
    int k = snprintf(line, kLine, "%lu ", (unsigned long)f);
    const uint8_t *b = (const uint8_t *)(pcm + 2 * f);
    for (uint32_t i = 0; i < n * 4; i++) {
      line[k++] = kHex[b[i] >> 4];
      line[k++] = kHex[b[i] & 15];
    }
    line[k] = 0;
    Serial.println(line);
  }
  Serial.println("MIC RAW END");
  // What the two slots hold: peak, RMS and DC each, and how alike they are.
  double sl = 0, sr = 0, ql = 0, qr = 0, lr = 0;
  int pl = 0, pr = 0;
  for (uint32_t i = 0; i < got; i++) {
    int l = pcm[2 * i], r = pcm[2 * i + 1];
    sl += l;
    sr += r;
    ql += (double)l * l;
    qr += (double)r * r;
    lr += (double)l * r;
    if (abs(l) > pl) pl = abs(l);
    if (abs(r) > pr) pr = abs(r);
  }
  if (got) {
    const double n = (double)got, ml = sl / n, mr = sr / n;
    const double vl = ql / n - ml * ml, vr = qr / n - mr * mr, cv = lr / n - ml * mr;
    const double corr = (vl > 0 && vr > 0) ? cv / sqrt(vl * vr) : 0;
    auto db = [](double x) { return x > 0 ? 20.0 * log10(x / 32768.0) : -99.0; };
    Serial.printf("MIC RAW STATS L peak %.1f rms %.1f dc %.0f; R peak %.1f rms %.1f dc %.0f dBFS; L/R correlation %.3f%s\n",
                  db(pl), db(sqrt(ql / n)), ml, db(pr), db(sqrt(qr / n)), mr, corr,
                  corr < -0.5 ? " (one slot looks inverted)" : (vl <= 0 || vr <= 0) ? " (one slot is flat)" : "");
  } else {
    Serial.println("MIC RAW STATS nothing captured");
  }
  Serial.setTxTimeoutMs(2);
  heap_caps_free(pcm);
}

}  // namespace board
