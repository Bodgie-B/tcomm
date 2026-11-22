// Voice notes: the radio frames, the shared constants and the small shared structs
// (docs/voice-notes/DESIGN.md, sections 3-5 and "P0 notes"). Frozen by piece P0: the other pieces
// build against this file and do not change it without the lead.
// Plain C++: no Arduino, ESP-IDF or LVGL headers. The C6 gateway compiles voice_proto.cpp too
// (../gateway/gateway-c6/src/shared/core_voice_proto.cpp).
//
// Voice frames are NOT tc::Msg: they have their own codec here, so a 237-byte member never grows
// every Msg. They share the version byte and the sender ids with protocol.h, and use types 20-30,
// which tc::decode() rejects (old firmware drops them un-ACKed). They never use the standard ACK,
// Dedup or RetryQueue: every confirmation is end to end (VOICE_ACK, VOICE_GOT).
//
// Header, 6 bytes, little-endian:
//   ver u8 (= kProtoVersion) | type u8 | sender u8 (the device that sent THIS hop) | seq u16 (trace
//   only) | floor u8 (the panel end of the conversation: 0 upstairs, 1 downstairs; panel -> C6 the
//   origin, C6 -> panel the destination, SAY the writer's floor)
//
// Bodies after the header (sizes are whole frames):
//   20 VOICE_BEGIN   panel -> C6   xfer u8 | purpose u8 | codec u8 | chunk_samples u16 | max_chunks u8   12
//   21 VOICE_DATA    panel -> C6   xfer u8 | idx u8 | pred i16 | step u8 | len u8 | data[len]      12+len
//   22 VOICE_ACK     C6 -> panel   xfer u8 | base u8 | bits u32 | state u8                               13
//   23 VOICE_END     panel -> C6   xfer u8 | n_chunks u8 | n_samples u16 | speech u8                     11
//   24 VOICE_ABORT   either        xfer u8 | reason u8                                                    8
//   25 VOICE_TEXT    C6 -> panel   xfer u8 | status u8 | flags u8 | len u8 | text[len]            10+len
//   26 SAY           panel->panel  id u32 | time u32 | len u8 | text[len]                         15+len
//   27 VOICE_GOT     any           kind u8 | key u32 | result u8                                         12
//   28 VOICE_PUSH    panel -> C6   id u32 | from u8 | time u32 | len u8 | text[len]               16+len
//   29 VOICE_DISMISS C6 -> panel   id u32                                                                10
//   30 VOICE_PING    downstairs -> C6, direct only: nonce u32                                            10
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include "types.h"
#include "protocol.h"

namespace tc {

// ---- Frame types and sizes ----
enum class VType : uint8_t {
  Begin = 20, Data = 21, Ack = 22, End = 23, Abort = 24, Text = 25, Say = 26, Got = 27, Push = 28, Dismiss = 29,
  Ping = 30
};
constexpr uint8_t kVoiceTypeFirst = 20, kVoiceTypeLast = 30;
// The reach test's frames (piece R, DESIGN 13.0): no device decodes type 31, so nothing acts on it.
constexpr uint8_t kVoiceReachType = 31;
inline bool is_voice_type(uint8_t t) { return t >= kVoiceTypeFirst && t <= kVoiceTypeLast; }
// Byte [1] decides: voice frames go to voice_decode(), everything else to decode() as before.
inline bool is_voice_frame(const uint8_t *d, size_t len) {
  return d && len >= 2 && d[0] == kProtoVersion && is_voice_type(d[1]);
}

constexpr size_t kVoiceHeader = 6;
constexpr size_t kVoiceChunkBytes = 237;   // data bytes in a full chunk, both codecs
constexpr uint16_t kVoiceChunkSamplesAdpcm = 474;  // 2 samples a byte, LOW nibble first
constexpr uint16_t kVoiceChunkSamplesMulaw = 237;  // 1 sample a byte
// Field fix 2026-11-21 (mu-law by default): 255, so 3 s of mu-law (about 205 chunks) fits. BEGIN's
// max_chunks, DATA's idx, ACK's base and END's n_chunks are all u8, so the wire format is unchanged:
// idx < 255; max_chunks in BEGIN is 1-255; base and n_chunks 0-255. Was 128.
constexpr int kVoiceMaxChunks = 255;
static_assert(kVoiceMaxChunks <= 255, "idx, max_chunks, base and n_chunks are u8 on the wire");
constexpr int kVoiceWindow = 32;           // never more than 32 chunks past base (= the ACK bitmap)
// True for a chunk count the frames can carry (an int, so a u8 field compares without a range warning).
inline bool voice_chunks_ok(int n) { return n >= 0 && n <= kVoiceMaxChunks; }
constexpr size_t kVoiceTextMax = 120;      // VOICE_TEXT, SAY, VOICE_PUSH text bytes, 0x20-0x7E, no NUL
constexpr uint8_t kVoiceStepMax = 88;      // the IMA step index

constexpr size_t kVoiceBeginLen = 12, kVoiceAckLen = 13, kVoiceEndLen = 11, kVoiceAbortLen = 8, kVoiceGotLen = 12,
                 kVoiceDismissLen = 10, kVoicePingLen = 10;
constexpr size_t kVoiceDataBase = 12, kVoiceTextBase = 10, kVoiceSayBase = 15, kVoicePushBase = 16;
static_assert(kVoiceDataBase + kVoiceChunkBytes <= kMaxMsg, "a DATA frame fits one ESP-NOW payload");
static_assert(kVoicePushBase + kVoiceTextMax <= kMaxMsg, "a PUSH frame fits one ESP-NOW payload");

// ---- Field values (on the wire: never renumber) ----
enum class VoicePurpose : uint8_t { Dictation = 1, Message = 2 };
enum class VoiceCodec : uint8_t { Mulaw = 1, Adpcm = 2 };
enum class VoiceAckState : uint8_t { Open = 1, Complete = 2, Unknown = 3 };
enum class VoiceAbort : uint8_t { User = 1, Bell = 2, Busy = 3, Timeout = 4, Lost = 5, Silent = 6, NoPi = 7 };
// VOICE_TEXT status. Ok carries text (len >= 1); every other status carries none (len 0).
// Queued is interim: the final text for the same xfer follows.
enum class VoiceStatus : uint8_t { Ok = 0, Nothing = 1, Failed = 2, Busy = 3, Timeout = 4, Lost = 5, Queued = 6 };
constexpr uint8_t kVoiceStatusMax = 6;
// VOICE_TEXT flags (check finding 2). Bit 0: the text's first capital letter is there only because
// it starts a sentence (not a name, not "I"). The panel lowercases it when inserting mid-field,
// unless the cursor is at 0 or after ". ", "? " or "! " (voice_keep_first_capital below).
constexpr uint8_t kVoiceTextSentenceCap = 0x01;
constexpr uint8_t kVoiceTextFlagsKnown = 0x01;  // any other bit set: the frame is refused
enum class VoiceGotResult : uint8_t { Ok = 0, Gone = 1, Refused = 2 };

// ---- Owner defaults (Mike, 2026-11-20). Constants so they are cheap to change. ----
constexpr uint32_t kVoiceRecMs = 3000;       // the cap counts from the PRESS; total 3.0 s; the
                                             // countdown shows from the press (badge card and strip)
constexpr uint32_t kVoiceHoldMs = 600;       // released later than this after the press = hold-to-talk
constexpr bool kVoiceEarlyStop = true;       // early stop on quiet after speech: on
constexpr uint32_t kVoiceEarlyStopMs = 800;  // that quiet
constexpr bool kVoiceEditButton = true;      // EDIT on the result screen: on
// Phone push only when the RECEIVING floor is Open or Busy and outside quiet hours (the doorbell's
// rule). The receiving panel decides this and sends VOICE_PUSH only then (piece B); phone_bell
// also keeps its own quiet hours (piece G).
constexpr bool kVoicePushOpenBusyOnly = true;
constexpr uint32_t kVoicePhoneTokenHours = 12;  // the phone's Got it link lives 12 h (Pi side)
// A dismiss on the panel does NOT clear the phone push yet. Piece B leaves one marked hook in the
// controller's dismiss path ("TODO(voice): clear the phone push") that sends nothing today.
constexpr bool kVoiceDismissClearsPhone = false;

constexpr uint32_t kVoiceRateHz = 16000;
// Chunks a recording can need: 3.0 s at 16 kHz is 48,000 samples = 101.3 ADPCM chunks; 2 spare for
// the block the stop lands in. BEGIN's max_chunks for ADPCM.
constexpr int kVoiceClipChunks =
    (int)((kVoiceRecMs * (kVoiceRateHz / 1000) + kVoiceChunkSamplesAdpcm - 1) / kVoiceChunkSamplesAdpcm) + 2;
static_assert(kVoiceClipChunks <= kVoiceMaxChunks, "the clip fits the frame's index space");
// mu-law (the default since the field test of 2026-11-21: less compression for the recogniser) holds
// half the time per chunk: 3.0 s is 202.5 chunks; 80 ms more for the pre-roll lead and the block the
// stop lands in, and 2 spare: 210. BEGIN's max_chunks for mu-law. About 68 DATA frames a second.
constexpr int kVoiceClipChunksMulaw =
    (int)(((kVoiceRecMs + 80) * (kVoiceRateHz / 1000) + kVoiceChunkSamplesMulaw - 1) / kVoiceChunkSamplesMulaw) + 2;
static_assert(kVoiceClipChunksMulaw <= kVoiceMaxChunks, "a mu-law clip fits the frame's index space");
// The codec a panel records with (Mike, 2026-11-21: by what the gateway takes). Mu-law only when the C6
// has said it takes it (its voice hello with kVoiceHelloKeyMulaw, heard by this panel or passed on in
// upstairs' STATE as kStateGwMulaw); otherwise ADPCM, which every C6 that takes voice takes (0.5 on).
// Serial `voice codec 1|2` sets it by hand until restart; `voice codec 0` gives it back to the gateway.
constexpr VoiceCodec kVoiceDefaultCodec = VoiceCodec::Mulaw;  // with a C6 that takes it
inline VoiceCodec voice_auto_codec(bool gw_mulaw) { return gw_mulaw ? VoiceCodec::Mulaw : VoiceCodec::Adpcm; }
// Per codec: samples a chunk, and the chunks a clip is given (BEGIN's max_chunks). Anything but mu-law
// is ADPCM.
inline uint16_t voice_chunk_samples(uint8_t codec) {
  return codec == (uint8_t)VoiceCodec::Mulaw ? kVoiceChunkSamplesMulaw : kVoiceChunkSamplesAdpcm;
}
inline int voice_clip_chunks(uint8_t codec) {
  return codec == (uint8_t)VoiceCodec::Mulaw ? kVoiceClipChunksMulaw : kVoiceClipChunks;
}

// ---- Arrival inbox (saved on every change; DESIGN 2.3) ----
constexpr int kVoiceInboxMax = 4;
constexpr int kVoiceSeenIds = 8;

// ---- The gateway's voice hello and the air caps (field fix 2026-11-20, docs/voice-notes/WORKLOG.md) ----
// A C6 that takes voice (0.5 on) says so with VOICE_GOT kind 30 (Ping), floor 0, to UPSTAIRS only (the
// proven link), at start-up and every kVoiceHelloEveryMs. The key says what it takes: kVoiceHelloKey (C6
// 0.5) ADPCM clips of up to 128 chunks; kVoiceHelloKeyMulaw (0.6 on, Mike 2026-11-21) mu-law as well, and
// up to 255 chunks. No ping nonce is ever 0 or 1, so a hello never answers a ping. A panel sends the
// gateway nothing voice-related until it has heard a hello (downstairs: or upstairs' kStateGwVoice) within
// kVoiceGwStaleMs, so a C6 0.4 gets no voice frame at all.
constexpr uint32_t kVoiceHelloKey = 0;
constexpr uint32_t kVoiceHelloKeyMulaw = 1;
inline bool voice_hello_key(uint32_t key) { return key <= kVoiceHelloKeyMulaw; }
constexpr uint32_t kVoiceHelloEveryMs = 120000;
constexpr uint32_t kVoiceGwStaleMs = 600000;  // five hellos missed in a row: the gateway takes no voice
// A token bucket for voice frames on the air: per_s a second, bursts of up to `burst`. Not thread safe:
// board_radio wraps it in a lock. A refused frame is like a radio refusal: the resend logic covers it.
struct VoiceRateCap {
  uint32_t per_s, burst;
  uint32_t milli = 0, last = 0;  // tokens in thousandths of a frame; when last refilled
  bool started = false;
  VoiceRateCap(uint32_t per_second, uint32_t burst_frames) : per_s(per_second), burst(burst_frames) {}
  bool take(uint32_t now_ms) {
    if (!started) {
      started = true;
      milli = burst * 1000u;
    } else {
      uint32_t dt = now_ms - last;
      if (dt > 100000u) dt = 100000u;
      milli += dt * per_s;
      if (milli > burst * 1000u) milli = burst * 1000u;
    }
    last = now_ms;
    if (milli < 1000u) return false;
    milli -= 1000u;
    return true;
  }
};
// The panel's cap on everything radio_send_voice sends, and the C6's on its own voice frames (ACKs,
// GOTs, TEXT, DISMISS). A mu-law upload is about 68 DATA frames a second (ADPCM 34), plus resends and
// BEGIN/END; upstairs relaying downstairs' upload passes those on and the ACKs back, about 85 a second
// with 10 % loss. 140 (was 100, field fix 2026-11-21) leaves room for that; the burst stays 40.
// The C6 sends only ACKs (one per 8 new chunks, one 80 ms after a gap or duplicate), GOTs, TEXT and
// DISMISS: about 9 a second per streaming floor, 20 with both floors and heavy loss. 100 (was 50,
// gateway-c6 0.6) is for headroom and for the downstream direction of any later streaming; the burst
// stays 25 so a stuck loop still cannot flood the air.
constexpr uint32_t kVoicePanelPerS = 140, kVoicePanelBurst = 40;
constexpr uint32_t kVoiceGwPerS = 100, kVoiceGwBurst = 25;

// ---- The decoded frame ----
struct VoiceMsg {
  VType type = VType::Ping;
  uint8_t sender = 0;
  uint16_t seq = 0;
  uint8_t floor = 0;
  union {
    struct { uint8_t xfer, purpose, codec; uint16_t chunk_samples; uint8_t max_chunks; } begin;
    struct { uint8_t xfer, idx; int16_t pred; uint8_t step, len; uint8_t data[kVoiceChunkBytes]; } data;
    struct { uint8_t xfer, base; uint32_t bits; uint8_t state; } ack;
    struct { uint8_t xfer, n_chunks; uint16_t n_samples; uint8_t speech; } end;
    struct { uint8_t xfer, reason; } abort;
    struct { uint8_t xfer, status, flags, len; char text[kVoiceTextMax + 1]; } text;
    struct { uint32_t id, time; uint8_t len; char text[kVoiceTextMax + 1]; } say;
    struct { uint8_t kind; uint32_t key; uint8_t result; } got;
    struct { uint32_t id; uint8_t from; uint32_t time; uint8_t len; char text[kVoiceTextMax + 1]; } push;
    struct { uint32_t id; } dismiss;
    struct { uint32_t nonce; } ping;
  } u;
  VoiceMsg() : u{} {}
};

// Returns the encoded length, or 0 if m would not decode (the same checks as voice_decode).
// Text fields are taken from their len (len bytes are copied; no NUL needed).
size_t voice_encode(const VoiceMsg &m, uint8_t *out, size_t cap);
// False for anything malformed: wrong version, a type outside 20-30, sender > 2, floor > 1, a
// length that does not match exactly, xfer 0, idx >= 255, a DATA len 0 or > 237, step > 88, a
// codec outside 1-2, a purpose outside 1-2, chunk_samples not the codec's, max_chunks 0 (1-255
// pass), an ACK state outside 1-3, an abort reason outside
// 1-7, a status above 6, unknown flag bits, status Ok with no text or any other status with text,
// text bytes outside 0x20-0x7E, a SAY, PUSH or DISMISS id 0, an empty SAY or PUSH, PUSH from == floor,
// a GOT kind not 25/26/28/29/30, a GOT result above 2, END speech above 1 or n_samples above
// n_chunks x 474. Text comes back NUL-terminated. A DATA frame does not name its codec, so the
// receiver of a mu-law upload (the C6, the Pi) checks pred 0 and step 0 against the BEGIN.
bool voice_decode(const uint8_t *in, size_t len, VoiceMsg &out);
const char *voice_type_name(uint8_t type);  // "VOICE_BEGIN" ... "VOICE_PING", "?" otherwise

// The header fields without a full decode (the relay task and on_recv use it; cheap, no copy).
struct VoicePeek { uint8_t type, sender, floor, kind; };  // kind: a GOT's confirmed type, else 0
bool voice_peek(const uint8_t *d, size_t len, VoicePeek &p);
// The relay rewrites only the sender byte (byte [2]) to its own id before sending the next hop.
inline void voice_set_sender(uint8_t *d, uint8_t sender) { d[2] = sender; }

// VOICE_GOT keys. For VOICE_TEXT: xfer + status x 256, so the GOT for an interim Queued can never
// clear the final text of the same xfer. For SAY, PUSH, DISMISS: the message id. For PING: the nonce.
inline uint32_t voice_text_key(uint8_t xfer, uint8_t status) { return (uint32_t)xfer + (uint32_t)status * 256u; }

// The window rule (check finding 6): nobody, mic_task included, makes a FIRST send of a chunk 32 or
// more past base. mic_task holds such a chunk until a VOICE_ACK moves base.
inline bool voice_in_window(int idx, int base) { return idx >= 0 && idx < base + kVoiceWindow; }

// Upstairs' relay rule (DESIGN 4.7), one pure function: the sender id to pass the frame on to
// (kSenderGateway or kSenderDownstairs), or -1 when it is not relayed (handle it here, or drop it).
//   me must be upstairs. From downstairs with floor 1: types 20, 21, 23, 24, 28, or 27 confirming 25
//   or 29 -> gateway. From the gateway with floor 1: types 22, 24, 25, 29, or 27 confirming 28 ->
//   downstairs. SAY, a GOT for SAY, PING and a GOT for PING are never relayed.
int voice_relay_to(uint8_t type, uint8_t kind, uint8_t from, uint8_t floor, uint8_t me);
// The same, straight from a frame's bytes (voice_peek, then voice_relay_to). -1 for a bad frame.
int voice_relay_frame(const uint8_t *d, size_t len, uint8_t from, uint8_t me);

// The capital rule for VOICE_TEXT flags bit 0 (check finding 2). field: the text in the field,
// cursor: where the text goes in (0..strlen). True keeps the first letter as it came; false means
// lowercase it. Keeps it when the flag is clear (a name, "I"), the cursor is at 0, only spaces are
// before it, or the last non-space before the cursor is '.', '?' or '!' (the insert adds the space
// if there is none, so ".", ". " and ".  " all count).
bool voice_keep_first_capital(const char *field, int cursor, uint8_t flags);
// Applies it in place to text (its first letter only).
void voice_apply_capital(char *text, const char *field, int cursor, uint8_t flags);

// ---- Codecs, as frozen (DESIGN 3.1, P0 notes; the golden vectors in tests/vectors bind them) ----
// IMA ADPCM 4-bit, 16 kHz mono. Each chunk carries the decoder state BEFORE its first sample
// (pred, step index), so chunks decode alone. A recording starts at pred 0, index 0. Two samples a
// byte, LOW nibble first; an odd last sample leaves the high nibble 0. The encoder (piece A,
// voice_dsp) is the standard one (see tools/voice_vectors.py adpcm_encode).
extern const int16_t kImaStep[89];
extern const int8_t kImaIndex[16];
// Decodes n samples (n <= 2 x bytes) from one chunk. Returns n, or 0 if step > 88.
int voice_adpcm_decode(int16_t pred, uint8_t step, const uint8_t *data, int n, int16_t *out);
// G.711 mu-law (the fallback codec; one sample a byte, pred and step 0 on the wire).
int16_t voice_mulaw_decode(uint8_t b);

// ---- The capture handoff between mic_task (piece A) and the LVGL side (piece B) ----
// DESIGN 3.2. One VoiceRec per job, from alloc_big(voice_rec_bytes(max_chunks)), built with
// voice_rec_init. LVGL fills the setup fields, then publishes it (board::voice_rec_begin, a release
// store of g_rec). From then on each field has ONE writer, as marked. LVGL frees it only after
// board::voice_rec_release returned true (mic_task is not inside it and never will be again).
enum class VoiceStop : uint8_t {
  None = 0, Release = 1, StopKey = 2, Countdown = 3, Quiet = 4, Cancel = 5, Bell = 6, Full = 7
};
struct VoiceChunk {  // one chunk as it goes in a DATA frame
  int16_t pred;
  uint8_t step;
  uint8_t len;  // data bytes used
  uint8_t data[kVoiceChunkBytes];
};
struct VoiceRec {
  // Setup: written by LVGL before publishing, read-only afterwards.
  uint8_t xfer = 0;          // 1-255
  uint8_t floor = 0;         // this panel's floor (0, 1)
  uint8_t sender = 0;        // this panel's sender id
  uint8_t to = kSenderGateway;  // first hop for DATA and END: the gateway, or upstairs on the relay route
  uint8_t codec = (uint8_t)VoiceCodec::Adpcm;
  uint16_t chunk_samples = kVoiceChunkSamplesAdpcm;
  uint8_t max_chunks = 0;    // chunks[] holds this many
  uint32_t press_ms = 0;     // millis() at the press; mic_task stops by itself at press_ms + kVoiceRecMs
  bool early_stop = kVoiceEarlyStop;
  VoiceChunk *chunks = nullptr;  // max_chunks entries, right after this struct in the same block
  uint32_t *first_ms = nullptr;  // max_chunks entries: millis() of each chunk's first send
  // Written by mic_task only.
  std::atomic<uint8_t> chunks_ready{0};  // chunks [0, chunks_ready) are complete and will not change
  std::atomic<uint8_t> first_sent{0};    // chunks [0, first_sent) have had their first send (in order)
  std::atomic<uint16_t> n_samples{0};    // 16 kHz samples encoded so far
  std::atomic<uint8_t> level{0};         // 0-100, for the level bar
  std::atomic<bool> speech{false};       // speech onset seen (END's speech byte)
  std::atomic<uint8_t> stopped{0};       // the VoiceStop that ended capture (mic_task's own or LVGL's)
  std::atomic<bool> end_sent{false};     // END had its first send (only after every chunk's first send)
  std::atomic<bool> done{false};         // mic_task has finished with capture; END is out
  std::atomic<uint16_t> seq{0};          // header seq counter for mic_task's own sends (trace only)
  // Written by LVGL only.
  std::atomic<uint8_t> stop{0};          // a VoiceStop request; mic_task acts on its next block
  std::atomic<uint8_t> base{0};          // the latest VOICE_ACK base: the window for first sends
};
size_t voice_rec_bytes(uint8_t max_chunks);
// Builds a VoiceRec in mem (n >= voice_rec_bytes(max_chunks)) and points chunks and first_ms into
// the same block. Returns it, or nullptr if mem is null or n too small.
VoiceRec *voice_rec_init(void *mem, size_t n, uint8_t max_chunks);
// Destroys it in place (call before free_big; trivial today).
void voice_rec_destroy(VoiceRec *r);

// ---- The arrival inbox as saved (Platform::save_voice_inbox, board::save_voice_inbox) ----
// Plain data (no initialisers): all zero is an empty slot, and View is cleared with memset.
struct VoiceNote {
  uint32_t id;     // the SAY's id, never 0 for a used slot
  uint32_t time;   // the SAY's time (UTC, 0 = no clock)
  uint8_t from;    // the writer's floor
  uint8_t len;
  char text[kVoiceTextMax + 1];
};
struct VoiceInboxData {
  static constexpr uint8_t kVersion = 1;
  uint8_t version = kVersion;
  uint8_t n = 0;                       // messages, oldest first
  VoiceNote msg[kVoiceInboxMax] = {};
  uint32_t seen[kVoiceSeenIds] = {};   // the last 8 ids shown, so a retried SAY never shows twice
  uint8_t seen_next = 0;
};
// True if d is usable as loaded (version, counts, ids, NUL-terminated printable text).
bool voice_inbox_valid(const VoiceInboxData &d);

// ---- What the interface shows (View::voice; built by the controller, piece B; drawn by piece C) ----
enum class VoiceUi : uint8_t {
  Idle = 0,        // nothing: the plain keyboard, no recorder
  Recording = 1,   // countdown from press_ms, level bar, STOP and CANCEL
  Working = 2,     // "Turning speech into text", stepping dots, CANCEL
  Waiting = 3,     // "Waiting for the other floor's message" (status Queued), CANCEL
  Result = 4,      // text ready: the keyboard inserts it once (text_seq); the recorder shows it with SEND etc.
  Sending = 5,     // recorder: SEND tapped, waiting for VOICE_GOT for the SAY
  Delivered = 6,   // recorder: toast "Delivered", then the sheet closes
  Error = 7        // err says which, for 4 s
};
enum class VoiceErr : uint8_t {
  None = 0,
  NoPi = 1,          // "The Pi didn't answer"
  GatewayOld = 2,    // "Gateway needs an update" (no answer to BEGIN at all)
  SendFailed = 3,    // "Couldn't send the recording"
  Lost = 4,          // "Lost part of the recording, try again"
  Nothing = 5,       // "Didn't hear anything"
  NotCaught = 6,     // "Didn't catch that" (status Failed)
  NoMemory = 7,      // "Not enough memory to record"
  PeerNoAnswer = 8,  // "DOWNSTAIRS didn't answer" (the SAY expired)
  Bell = 9,          // "Recording stopped for the bell"
  Busy = 10          // "The Pi is busy, try again" (status Busy)
};
// Plain data like the rest of View (Controller::build_view clears View with memset): all zero is
// "nothing to show" (ui Idle, err None; purpose 0 = no job).
struct VoiceView {
  bool capable;           // this panel can record (mic key and badge recorder offered)
  bool peer_voice;        // the other panel advertises kStateVoice (SEND offered)
  bool peer_offline;      // the other panel shows No signal
  VoiceUi ui;
  VoicePurpose purpose;   // of the job shown; 0 when there is none
  uint32_t press_ms;      // mono ms of the press: countdown = kVoiceRecMs - (now - press_ms)
  uint8_t level;          // 0-100
  VoiceErr err;
  uint16_t text_seq;      // bumps once per new result, so the keyboard inserts it once
  uint8_t text_flags;     // VOICE_TEXT flags (kVoiceTextSentenceCap)
  char text[kVoiceTextMax + 1];
  // The arrival dialog: the oldest message in the inbox, shown over the SENDER's tile.
  uint8_t inbox_n;        // 0: no dialog; > 1: "+1" at the right of line 1
  uint16_t inbox_seq;     // bumps when the shown message changes (the 700 ms tap guard restarts)
  VoiceNote top;
  char top_when[12];      // "9:42 PM", empty without a clock
};

// ---- The C6 serial lines and the Pi's names (DESIGN 5.2, 6.6, 7.2; check findings 1 and 2) ----
// Frozen here so the C6 (piece D), the bridge (E), the stt service (F) and phone_bell (G) agree.
// One line each, no newline inside, at most 512 bytes (gw::kMaxLine). The printf formats are the C6's;
// the Python pieces parse the same keys. Numbers: pass unsigned values cast to unsigned (%u) or
// unsigned long (%lu), pred as int (%d).
namespace vline {
// Floor words: "f" in the C6 -> Pi voice events is the short word; every other floor field is the long one.
constexpr const char *kFloorShort[2] = {"up", "down"};
constexpr const char *kFloorLong[2] = {"upstairs", "downstairs"};
constexpr const char *kPurpose[3] = {"", "dictation", "message"};             // by VoicePurpose
constexpr const char *kVia[2] = {"direct", "upstairs"};                      // how the session's frames came
constexpr const char *kAbortWord[8] = {"", "user", "bell", "busy", "timeout", "lost", "silent", "nopi"};  // by VoiceAbort

// C6 -> Pi events. "d" is the chunk's data bytes in standard base64 with padding (237 B -> 316 chars).
constexpr const char *kEvBegin =
    "{\"event\":\"voice_begin\",\"x\":%u,\"f\":\"%s\",\"purpose\":\"%s\",\"codec\":%u,\"cs\":%u,\"via\":\"%s\"}";
constexpr const char *kEvData = "{\"event\":\"voice\",\"x\":%u,\"f\":\"%s\",\"i\":%u,\"p\":%d,\"s\":%u,\"d\":\"%s\"}";
constexpr const char *kEvEnd = "{\"event\":\"voice_end\",\"x\":%u,\"f\":\"%s\",\"n\":%u,\"ns\":%u,\"sp\":%u}";
constexpr const char *kEvAbort = "{\"event\":\"voice_abort\",\"x\":%u,\"f\":\"%s\",\"reason\":\"%s\"}";
// text JSON-escaped by the C6 (only '"' and '\' can occur in 0x20-0x7E).
constexpr const char *kEvMsg =
    "{\"event\":\"voice_msg\",\"id\":%lu,\"to\":\"%s\",\"from\":\"%s\",\"time\":%lu,\"text\":\"%s\"}";

// Pi -> C6 requests, each answered by one acked / failed / rejected / superseded reply in the
// existing pattern ({"type":"voice_text","id":N,"acked":"downstairs","seq":S,"ms":M}; a voice_ack's
// acked reply adds "result":"removed"|"gone"; a voice_ack for a floor whose slot is busy gets
// "rejected":"busy"). The text arrives split in t0 and t1 (each at most 60 chars, json_line holds 63);
// the C6 joins t0 + t1. "fl" is the VOICE_TEXT flags byte (bit 0: kVoiceTextSentenceCap).
constexpr const char *kReqText = "voice_text";  // {"type":"voice_text","id":N,"x":7,"floor":"downstairs","status":0,"fl":1,"t0":"..","t1":".."}
constexpr const char *kReqAck = "voice_ack";    // {"type":"voice_ack","id":N,"floor":"downstairs","msg":3735928559}
constexpr size_t kTextPart = 60;
constexpr const char *kResultRemoved = "removed", *kResultGone = "gone";

// Hello. The bridge sends {"cmd":"hello","voice":1}; the C6 0.5 adds three fields to its hello reply:
//   "voice":1          it takes voice frames
//   "drops":N          radio frames its RX queue had to drop since the reset
//   "down_peer":true   the downstairs panel is registered as an ESP-NOW peer with its key (check
//                      finding 1: false means downstairs frames cannot be taken directly at all)
constexpr const char *kHelloVoice = "voice", *kHelloDrops = "drops", *kHelloDownPeer = "down_peer";

// Bridge control-socket ops (PI tcomm_bridge.py, piece E), one JSON line per connection:
//   {"op":"voice_text","x":7,"floor":"downstairs","status":0,"flags":1,"text":"Dinner is ready"}
//     from the stt service: x 1-255, floor, status 0-6, flags 0-1, text printable ASCII <= 120 (empty
//     unless status 0). The bridge splits text at <= 60 chars into t0/t1 and sends kReqText with "fl".
//   {"op":"voice_ack","floor":"downstairs","msg":N}   from phone_bell (msg 1-4294967295)
constexpr const char *kOpText = "voice_text", *kOpAck = "voice_ack";
// The bridge passes voice_begin, voice, voice_end and voice_abort lines to stt.sock unchanged (one
// datagram each, never logged with "d"), sends {"event":"voice_reset"} there when the C6 says boot,
// and passes voice_msg to phone.sock as {"voice_msg":{...the event...},"received":<unix s>}.
constexpr const char *kEvReset = "voice_reset";
}  // namespace vline

}  // namespace tc
