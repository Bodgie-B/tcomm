// TComm radio messages (ESP-NOW payloads). HANDOFF: every message starts with a version byte,
// a type, a sender id and a sequence number; under 250 bytes; up to 5 tries over about 1.5 s;
// the receiver drops duplicates by sequence number. No heartbeats: nothing is sent on a timer.
//
// Wire format, little-endian:
//   header  ver u8 | type u8 | sender u8 | seq u16                              (5 bytes)
//   STATE   status u8 | flags u8 | since u32 | until u32 | fallback u8 | clock u32 | clock_quality u8
//   KNOCK   knock_id u16
//   REPLY   knock_id u16 | come_on u8
//   ACK     acked_seq u16 | acked_type u8
//   DOG     lucy_out u8 | lucy_since u32 | lucy_ver u32 | copper_out u8 | copper_since u32 | copper_ver u32
//           (each dog on its own: both can be out; ver = that dog's change counter)
//   READING flags u8 | temp_tenths_f i16 | humidity_tenths u16
//   LUCY    out u8 | since u32
//   OUTSIDE flags u8 | temp_tenths_f i16 | humidity u8 | forecast u8 | time u32 | phrase_len u8 | phrase
//           (forecast: a Forecast code, types.h; a code the panel doesn't know shows as no forecast.
//            phrase: WeatherGate's forecast text, shown as it comes. See ../gateway/docs/weathergate.md.)
//   EVENT   one calendar event, as event_write() lays it out (events.h)
//   EVENT_SUM count u16 | digest u32 | flags u8   (the sender's calendar in brief; 1 = please reply)
//   MAIL    waiting u8 | since u32 | ver u32   (mail for downstairs: upstairs marks it, downstairs
//           acknowledges it; ver = its change counter, as for a dog. See mail.h.)
//   COMFORT one survey answer, 33 bytes: n u32 | utc u32 | offset i32 | clock_q u8 | feel u8 | ask u8 | sent u8 |
//           status u8 | have u8 | in_t i16 | in_h u16 | up_t i16 | up_h u16 | out_t i16 | out_h u8 | removed u32
//           (downstairs to upstairs; every answer matters, so none replaces another. See comfort.h.)
//   REQUESTS the heat and cooling arrows, 32 bytes (Mike, 2026-11-06; requests.h): n u8 |
//           3 x (ask u8, source u8, sent u32) | acted u32 | acted_dismissed u8 | since u32 | ver u32
//           (unused slots zero; ver = the change counter, so like MAIL the later change wins)
//   COMFORT_DEL 12 bytes (Mike, 2026-11-06; downstairs removed answer n by mistake): n u32 | removed u32 | offset i32
//           (removed = when, UTC, 0 without a clock. Every removal matters, so none replaces another.)
//   NOTE    a floor's tile message (Mike, 2026-11-07; note.h), at most 1+1+64+16 = 82 bytes of body: floor u8 |
//           text_len u8 | text bytes (at most kNoteTextMax, no NUL) | set u32 | until u32 | since u32 | ver u32
//           (latest wins per floor: a newer NOTE for a floor replaces a pending one for that floor only)
//   UPDATE  reserved u8 (always 0)   (Mike, 2026-11-07: start your Wi-Fi updater; sent by the panel whose
//           Settings > Update over Wi-Fi > Both panels was tapped, or by the gateway to the upstairs panel
//           when the Pi sends it the serial line UPDATE (upstairs then does what that tap does).
//           Not latest-wins. See docs/wifi-updates.md and ../gateway/docs/weathergate.md.)
//   NOTICE  a house event for the Pi (Mike, 2026-11-11), UPSTAIRS to the gateway only, at most 1+1+2+4+1+40 = 49 bytes
//           of body: kind u8 (1 to 7, see NoticeKind) | floor u8 | value i16 | time u32 (UTC, 0 = no clock) |
//           text_len u8 | text bytes (at most kNoticeTextMax, printable ASCII, no NUL). The gateway ACKs it like a
//           panel would and prints one serial line (../gateway/docs/gateway-c6.md). Never ACKed = dropped quietly.
#pragma once
#include "types.h"
#include "events.h"
#include "comfort.h"
#include "requests.h"
#include "note.h"

namespace tc {

constexpr uint8_t kProtoVersion = 1;
constexpr size_t kMaxMsg = 250;          // ESP-NOW payload limit
constexpr size_t kMaxPhrase = 48;

enum class MsgType : uint8_t {
  State = 1, Knock = 2, Reply = 3, Ack = 4, DogMsg = 5, Reading = 6, Lucy = 7, Outside = 8, EventMsg = 9, EventSum = 10,
  Mail = 11, Comfort = 12, Requests = 13, ComfortDel = 14, Update = 15, Note = 16, Notice = 17
};

// The house events the upstairs panel reports to the Pi (Mike, 2026-11-11; ../gateway/docs/gateway-c6.md).
// The numbers are on the wire: never change one.
//   Knock: floor = who rang; value 0 = nobody answered in the ring time, 1 = it rang while the
//     other floor was Away (kept for later). Arrow: value +1 heat, -1 cooling. Mail: value 1.
//   DogOut: text LUCY / COPPER / BOTH, value = minutes out. Status: floor = whose, text = the word.
//   Note: floor = whose, text = the message's first kNoticeTextMax characters.
enum class NoticeKind : uint8_t { Knock = 1, ArrowUrgent = 2, ArrowMild = 3, Mail = 4, DogOut = 5, Status = 6, Note = 7 };
constexpr uint8_t kNoticeKinds = 7;
constexpr size_t kNoticeTextMax = 40;
// Which kinds go out, one bit each (bit k-1 for kind k). Starting value: knock, urgent arrow and
// dog out too long; the chatty ones wait until Mike turns them on.
constexpr uint8_t notice_bit(NoticeKind k) { return (uint8_t)(1u << ((uint8_t)k - 1)); }
constexpr uint8_t kNoticeDefaultMask = 0x01 | 0x02 | 0x10;  // kinds 1, 2 and 5
// The notice never to be queued behind panel traffic: at most this many wait at once.
constexpr int kNoticeQueueMax = 2;
enum Sender : uint8_t { kSenderUpstairs = 0, kSenderDownstairs = 1, kSenderGateway = 2 };
inline uint8_t sender_id(Floor f) { return f == Floor::Upstairs ? kSenderUpstairs : kSenderDownstairs; }

// The last two come from the UPSTAIRS panel when its presence sensor is fitted (Mike, 2026-11-11,
// docs/presence-sensor.md): kStateAwayDesk, Busy but walking about the house ("Away from desk" on
// his tile); kStateNobodyNear, nobody near the panel for 15 minutes (the tenant's ring says so).
// Both are 0 from a panel with no sensor, and an older panel ignores them.
enum StateFlags : uint8_t { kStatePleaseReply = 0x01, kStateClockValid = 0x02, kStateAwayDesk = 0x04, kStateNobodyNear = 0x08 };
enum ReadingFlags : uint8_t { kReadingValid = 0x01 };
// WeatherGate's two warnings (ZAMBRETTI_REFERENCE.md section 5), set by the gateway: danger heat
// (heat index 100 F or more) and danger cold (20 F or less). Either, both or neither.
enum OutsideFlags : uint8_t { kOutsideValid = 0x01, kOutsideDangerHeat = 0x02, kOutsideDangerCold = 0x04 };
enum SumFlags : uint8_t { kSumPleaseReply = 0x01 };

struct Msg {
  MsgType type = MsgType::State;
  uint8_t sender = 0;
  uint16_t seq = 0;
  union {
    struct { uint8_t status, flags; uint32_t since, until; uint8_t fallback; uint32_t clock; uint8_t clock_quality; } state;
    struct { uint16_t id; } knock;
    struct { uint16_t id; uint8_t come_on; } reply;
    struct { uint16_t seq; uint8_t type; } ack;
    struct { uint8_t out[2]; uint32_t since[2]; uint32_t ver[2]; } dog;  // [0] Lucy, [1] Copper
    struct { uint8_t flags; int16_t temp_tenths_f; uint16_t humidity_tenths; } reading;
    struct { uint8_t out; uint32_t since; } lucy;
    struct { uint8_t flags; int16_t temp_tenths_f; uint8_t humidity; uint8_t forecast; uint32_t time; uint8_t phrase_len; char phrase[kMaxPhrase + 1]; } outside;
    struct { uint8_t len; uint8_t data[kEventWireMax]; } event;  // checked by decode with event_read
    struct { uint16_t count; uint32_t digest; uint8_t flags; } sum;
    struct { uint8_t waiting; uint32_t since; uint32_t ver; } mail;
    ComfortAnswer comfort;
    RequestState req;
    struct { uint32_t n, removed; int32_t offset; } comfort_del;
    struct { uint8_t floor; NoteState st; } note;  // floor: a Floor as 0 upstairs, 1 downstairs
    struct { uint8_t kind, floor; int16_t value; uint32_t time; uint8_t text_len; char text[kNoticeTextMax + 1]; } notice;
  } u;
  Msg() : u{} {}
};

// Returns the encoded length, or 0 if the message can't be encoded.
size_t encode(const Msg &m, uint8_t *out, size_t cap);
// Returns false for anything malformed: wrong version, unknown type, wrong length, bad enum values.
bool decode(const uint8_t *in, size_t len, Msg &out);

// Remembers recent (sender, seq) pairs so retried messages are handled once. Entries expire, and
// senders start their sequence at a random number each boot, so a rebooted sender isn't mistaken
// for a duplicate.
class Dedup {
 public:
  static constexpr uint32_t kKeepMs = 10000;
  // Returns true if this message was seen recently; otherwise records it and returns false.
  bool seen(uint8_t sender, uint16_t seq, uint32_t mono_ms);

 private:
  struct Entry { uint8_t sender; uint16_t seq; uint32_t at; bool used; };
  Entry e_[24] = {};
  uint8_t next_ = 0;
};

// Outgoing messages waiting for an ACK. due() returns the next frame to (re)send; the caller
// hands it to the radio. Messages of a "latest wins" type replace an older pending one.
// Each retry is timed from the previous actual send, so a caller that stalls (a long redraw)
// spreads its tries out after the stall instead of sending them in one burst and giving up
// before the other side can answer. Without a stall the times are the offsets below.
class RetryQueue {
 public:
  static constexpr int kTries = 5;  // HANDOFF
  static constexpr uint32_t kOffsetsMs[kTries] = {0, 150, 400, 750, 1150};
  static constexpr uint32_t kGiveUpMs = 1500;  // HANDOFF: about 1.5 s (350 ms after the last try)
  // The catch-up after an outage queues up to 10 frames at once (controller.cpp, heard_from_peer), and
  // the calendar comparison goes last, so 8 slots dropped it (audit, 2026-11-11). 16 leaves room for
  // a doorbell or a status change in the same moment. Each slot is about 290 bytes of RAM.
  static constexpr int kSlots = 16;

  struct Frame { uint8_t data[kMaxMsg]; size_t len; uint16_t seq; MsgType type; };
  struct Result { bool failed; uint16_t seq; MsgType type; uint16_t knock_id; };

  // Queues a message; fills in sender and seq. Returns the seq, or 0 if it couldn't be queued.
  uint16_t push(Msg m, uint8_t sender, uint32_t mono_ms);
  // Next frame due now, if any. A NOTICE (for the gateway) only comes up when nothing for the
  // other panel is due.
  const Frame *due(uint32_t mono_ms);
  // Marks a frame as handed to the radio.
  void sent(uint16_t seq, uint32_t mono_ms);
  // An ACK arrived. Returns true and fills r if it matched a pending message.
  bool ack(uint16_t seq, Result &r);
  // Messages whose tries are used up and whose wait is over. Call until it returns false.
  bool expired(uint32_t mono_ms, Result &r);
  void set_seq(uint16_t s) { seq_ = s ? s : 1; }
  // Messages waiting for the other panel. Notices (for the gateway) are not counted: they must
  // never hold back panel-to-panel traffic.
  int pending() const;
  int pending(MsgType t) const;

 private:
  struct Slot { bool used; Frame f; int tries; uint32_t first; uint32_t last; uint16_t knock_id; uint8_t key; };
  static uint32_t next_at(const Slot &s);
  Slot s_[kSlots] = {};
  uint16_t seq_ = 1;
};

const char *msg_name(MsgType t);

}  // namespace tc
