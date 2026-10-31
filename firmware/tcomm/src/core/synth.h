// The panel's sounds, synthesized instead of read from microSD (no card needed): the doorbell
// chime in one of 17 ringtones, the soft chime for Busy, and the two reply blips.
// MIKE (2026-10-21): Westminster an octave up (E6 C6 D6 G5, G5 D6 E6 C6) is the doorbell chime.
// MIKE (2026-10-24): more ringtones, chosen from a list in Settings grouped by kind. Some came and
// went the same day (five sci-fi, Water drops, Rounds, Old bell, Robin, Crickets, Loon); Woodpecker
// and Church bells were redone with noise for the strikes.
// Every sound peaks at -3.5 dBFS: Waveshare's codec setup adds +3.5 dB at volume 100, so this is
// the loudest level that never clips.
#pragma once
#include "types.h"

namespace tc {

constexpr uint32_t kSynthRate = 44100;  // matches Waveshare's I2S setup

enum class ToneGroup : uint8_t { Standard, Nature };

// Each ringtone has a permanent id, which is what Settings store; a removed ringtone's id is never
// used again, so saved choices need no renumbering (ids 4, 6, 9, 14 and 15 are retired). The list in
// Settings shows them in table order, grouped by kind. Unknown ids get Westminster's (id 0).
constexpr uint8_t kRingtoneCount = 11;
uint8_t ringtone_id(int index);  // index: 0 to kRingtoneCount - 1, in list order
int ringtone_index(uint8_t id);  // -1 for an id no ringtone has
const char *ringtone_name(uint8_t id);
ToneGroup ringtone_group(uint8_t id);
const char *tone_group_name(ToneGroup g);  // "Standard", "Nature"
// A ringtone saved by a settings version (1 and 2 numbered them differently), as a known id; one
// that's gone becomes Westminster.
uint8_t ringtone_from_saved(uint16_t version, uint8_t old);

// Number of mono frames the sound needs. ringtone (an id) only matters for Sound::Chime.
size_t synth_frames(Sound s, uint8_t ringtone);
// The longest chime of all the ringtones, for a buffer that can hold any of them.
size_t synth_max_chime_frames();
// Renders mono 16-bit samples into out (synth_frames long). Works in small blocks, so it needs no
// scratch buffer; yield (optional) is called after each block of about 12 ms of sound, so a long
// render on the panel can let the idle task feed the watchdog. False if there was no memory for
// the voice list (out is then silent).
bool synth(Sound s, uint8_t ringtone, int16_t *out, size_t frames, void (*yield)() = nullptr);
// For the tests: how many sine voices the sound asks for (more than kMaxVoices means some were dropped).
int synth_voices(Sound s, uint8_t ringtone);
constexpr int kMaxVoices = 200;

}  // namespace tc
