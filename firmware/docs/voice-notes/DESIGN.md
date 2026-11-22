# Voice notes: speech to text through the WeatherGate Pi (design v2)

Version 2, 2026-11-20. Design only: nothing here is built, flashed or installed. v2 folds in Mike's
decisions of 2026-11-20 (section 0) and fixes every HIGH and MEDIUM finding of the first review
(section 15 says how). Paths are relative to the tcomm repo unless marked PI
(~/OneDrive/Documents/Projects/WeatherGate). "UNVERIFIED" means no file or
measurement backs it yet; each one is checked in a named test step (section 13).

Privacy rule for every piece: PI stt/house_vocab.txt (the grammar word list) and the names list
hold family names. They live on the Pi and in the WeatherGate folder only and are never copied
into this public repo, its tests, its docs or a log. Test vectors and test phrases here are
synthetic.

## 0. Mike's decisions (2026-11-20), which override v1

| # | Decision | Where in this design |
|---|---|---|
| 1 | Recording capped at 3 s with a visible countdown ("this is all you have"). Release, STOP or the countdown ends it; an early stop on silence is allowed. Replaces the 8 s cap and the 1.2 s silence stop | 2.1, 2.2, 3.4 |
| 2 | 2 s or less from the end of recording to the text in the field | 9.2 |
| 3 | Keyboard mic key: BLUE button, WHITE microphone glyph | 2.1 |
| 4 | Badge and status text are separate touch areas. A LONG press on the own floor's badge opens the recorder (Mike, 2026-11-21; was a tap that recorded at once). The small status text left of the badge still opens the tile-message editor, unchanged except that its keyboard gains the mic key | 2.2 |
| 5 | Arrival ding: one strike, lower than the dog alert (five G6 kBar strikes, src/core/synth.cpp:341-342); rendered on demand so no PSRAM is held | 2.3, 3.5 |
| 6 | The message also goes to the receiving floor's phones as an ntfy push (priority 3, quiet hours, never through Do Not Disturb, LAN button with a one-time token, no topics in the repo or logs). Got it on the phone dismisses it on the panel too | 7 |
| 7 | Downstairs to C6 has never carried traffic. Build both routes (direct, and relayed by upstairs), chosen by a reach test | 4.7, 13.0 |
| 8 | Engine: Vosk small en-us with a runtime grammar of about 1000 household words. Measured on the Pi 2 (numbers in 6.1). "two" is left out of the list and put back before time words. Capitalise names and the sentence start, "i" to "I" | 6 |
| 9 | Same update, after voice works: tap inside any text field to place the cursor, then insert or delete there | 8 |

## 1. Facts the design rests on (checked in the code)

| Fact | Where |
|---|---|
| One hand-built Keypad (on_char, on_back, on_done), keys[34], 33 used | src/ui/ui_internal.h:139-155, src/ui/ui_calendar.cpp:1297-1324 |
| Key presses: key_event acts on CLICKED (and LONG_PRESSED_REPEAT for backspace) | src/ui/ui_calendar.cpp:1223-1230 |
| Keypad users: tile-message editor, calendar event editor | src/ui/ui_note.cpp:238-245, src/ui/ui_calendar.cpp:1326-1330 |
| Note field: 2 labels plus a 2 px cursor box always at the end; on_char appends, on_back deletes the last | src/ui/ui_note.cpp:80-104, 120-143 |
| Calendar fields: one label each, focused text shown by fit_tail (the tail that fits), cursor at the end | src/ui/ui_calendar.cpp:1084, 1096-1119, 1202-1219 |
| Field limits: note 64 chars in 2 lines, calendar title 40, notes 120 | src/core/note.h:21, src/core/events.h:25-26 |
| Badge image at tile (289,105), 80 x 80, no handlers | src/ui/ui.cpp:271-272, src/ui/art.h:16 |
| Status zone (whole 386 x 193) takes CLICKED and opens the status sheet; own tile only | src/ui/ui.cpp:282-283, 427-431 |
| Note hit x 0-285, y 124-193 (the "small status text") opens the tile-message editor | src/ui/ui.cpp:223, 286-287, 213-219 |
| Mail and no-mail hits x 290-386, y 0-90 (downstairs tile); arrows y 0-90 and acted strip x 0-300, y 136-186 (upstairs tile) | src/ui/ui.cpp:230-240, 293-305, 309-321 |
| Weather zone x 386-514 on the downstairs tile, short and long press | src/ui/ui.cpp:328-331 |
| Tiles at screen (12,74) upstairs and (12,279) downstairs, 514 x 193, status zone 386 wide | src/ui/ui.cpp:852-853, src/ui/art.h:14 |
| Sheets and the banner live on lv_layer_top; new_sheet raises the doorbell banner | src/ui/ui_common.cpp:238-261 |
| Mic task: core 0, prio 4, 3072 B stack, 512 stereo frames per read, peak and power only; pause handshake g_mic_pause/g_mic_paused | src/board/board_audio.cpp:59-85, 350 |
| Clips rendered once at boot into g_clips[5] and kept in PSRAM; `which > 4` guard | src/board/board_audio.cpp:29, 128-155, 304-309, 339 |
| Dog alert = five G6 kBar strikes; SoftChime = E6 then C6; G5 = 783.99 Hz is defined | src/core/synth.cpp:142, 329-342 |
| radio_send returns void, traces every send into a 24-entry ring, elogs every error | src/board/board_radio.cpp:55-70, 163-174 |
| on_sent counts MAC-level "reached" before decryption, so any board with the MAC counts | src/board/board_radio.cpp:107-111 |
| Panel RX queue 16 frames, drained every 50 ms in app_tick on the LVGL task | src/board/board_radio.cpp:134, tcomm.ino:213-227, 515 |
| decode() rejects any other version and unknown types; an undecodable frame is dropped un-ACKed | src/core/protocol.cpp:135, 265; src/app/controller.cpp:699-710 |
| kProtoVersion 1, kMaxMsg 250, types 1-19 used; StateFlags bits 0x01-0x08 used | src/core/protocol.h:68-75, 109 |
| Notices: upstairs only, one-byte mask already full (8 kinds), text <= 40 | src/core/protocol.h:78-96, src/app/controller.cpp:599-618 |
| Gateway sends only to upstairs; upstairs relays PHONE_ANSWER for a downstairs ring | ../gateway/gateway-c6/gateway-c6.ino:113-120, src/app/controller.cpp:1549-1575 |
| C6: one Pending slot per kind (6 kinds), raw frame bytes kept, matched by ACK seq | ../gateway/gateway-c6/src/gw/gateway.h:57-75 |
| C6 takes NOTICE only from upstairs; drops other non-ACK frames | ../gateway/gateway-c6/src/gw/gateway.cpp:245-256 |
| C6 serial: lines <= 512 B, TX ring 2048, write timeout 20 ms, one write per line, no free-space check; JSON strings <= 63 chars | gateway.h:15, gateway-c6.ino:122-133, 294-299, src/gw/json_line.h:12-27 |
| C6 RX queue 16 frames | gateway-c6.ino:88-95, 148 |
| Bridge: single loop, select on serial with a 0.25 s tick; control socket polled once per step | PI tcomm_bridge.py:188, 1130-1142, 1011-1041 |
| Bridge passes "notice" events to alerts.sock and phone.sock, logs any other event at info | PI tcomm_bridge.py:724-750 |
| Bridge ops answer/mail_ack: validated in _op_body, 8 pending, 5 s reply wait, 512 B read | PI tcomm_bridge.py:176-184, 959-999, 1034-1047 |
| phone_bell: priority 3 for mail and notes, never 5 for non-rings, quiet hours 22:00-07:00, tokens from secrets.token_urlsafe(16) compared with hmac, http action buttons with clear true, no path or topic in logs | PI phone_bell.py:21-37, 477-523, 565-597, 666-700, 858-859 |
| Lucy Gate writes its state word (STANDBY, ARMED, ALERT, RESTART) to lucy_status.txt | PI lucy_gate.py:88, 94-100, 745 |
| Pi supply sits near the 4.63 V warning line; logins and load steps push it under | PI WEATHERGATE_MEMORY.md:20-23, PI CLAUDE_CODE_HANDOFF.md:92 |
| Bench script: grammar = one string of all words plus "[unk]"; 100 ms paced feed; "two" put back before time words | PI stt/bench.py:23-36, 54-70, 76-94 |

## 2. User flows

### 2.1 Keyboard mic key (every keyboard)

- Row 4 becomes: ?123 (1.5 keys), comma, MIC, SPACE (what is left), period, Done (2.4 keys). New
  KeyKind kMic takes the last free slot (keys[34] becomes full; a static_assert guards it).
- Look: fill blue 0x1F5FBF (night 0x2A56A8), glyph white 0xFFFFFF: capsule 8 x 12 (fill_rrect),
  a U arc under it, a stem and a base line, on the same 22 x 22 icon canvas as Shift and
  Backspace. While recording the key turns red 0xC62828 with a white 8 x 8 square (stop).
- Keypad gets `void (*on_mic)() = nullptr;`. kp_build adds the key only when on_mic is set. The
  note editor and the calendar editor set it, so both keyboards have it (decision 4).
- Press the MIC key (LV_EVENT_PRESSED, so the first syllable is not lost waiting for the release):
  capture starts at once and BEGIN goes out. A 784 x 56 strip covers keypad row 1: a big countdown
  digit "3", "2", "1" (J50), a 300 x 8 bar draining over 3 s, a 160 x 8 level bar, STOP and CANCEL.
  The other keys are disabled while recording. The key has LV_OBJ_FLAG_PRESS_LOCK set explicitly.
- How it ends: released more than 0.6 s after the press = hold-to-talk, the release ends it.
  Released sooner = tap mode, recording runs until the countdown ends, STOP (or the red MIC key) is
  tapped, or 0.8 s of quiet follows speech (the optional early stop, 3.4). PRESS_LOST before 0.6 s
  cancels silently.
- Then the strip shows "Turning speech into text" with three dots stepping every 300 ms (30 x 8
  px, cheap) and CANCEL.
- Result: inserted at the cursor (section 8; until piece H lands, at the end). A space is added
  before the text if the character before the cursor is not a space and not the start, and after
  it if the next character is a letter. Each character goes through the owner's on_char; the
  first refusal stops the insert and the owner's red border flash shows the field is full. The
  title fields keep their shift rule. A cut is never silent: if the whole text did not fit, the
  strip says "Only part of it fit" for 3 s.
- Errors (strip text for 4 s, then the plain keyboard): "The Pi didn't answer", "Gateway needs an
  update", "Couldn't send the recording", "Lost part of the recording, try again", "Didn't hear
  anything", "Didn't catch that". "Waiting for the other floor's message" is shown, not an error.
- The note editor's 60 s idle close (src/ui/ui_note.cpp:152-161) must not fire during a voice job:
  each strip state change calls lv_disp_trig_activity().

### 2.2 Badge long press: the recorder

Mike, 2026-11-21 (replaces the tap-to-record card below; the struck parts are kept for the record):
- A LONG press (LVGL's 400 ms, as on the comfort and outside tiles) on badge_hit opens Sheet::Voice.
  It does NOT record. A tap on the badge does nothing. Push-to-talk / hold-to-talk on the badge is
  gone (the keyboard's mic key keeps its 0.6 s hold rule, 2.1).
- The card is one row 784 x 224 at screen x 8, its bottom 4 px under the own tile's (upstairs y
  47-271, downstairs y 252-476), so its parts line up with the badge (card x 291-390, y 124-220):
  - left (x 20-252): the timer in J44, "3.0 s" in tenths rounded up, and its 232 px bar, full until
    START, red and draining to the pixel while recording (kVoiceRecMs from View::voice.press_ms), held
    where the recording ended while the Pi works; MICROPHONE and the level bar under it
    (View::voice.level, the capture path's own level, 4 px steps; field fix 2026-11-21: before START
    a live meter from the always-on mic task's block peaks, read 5 times a second while the row is
    open and ready, nothing recorded: Actions::voice_meter, Platform::mic_meter); a hint ("Press START, then speak",
    "Speak now", "Check the words, then SEND").
  - centre (x 276-406, y 68-214, over the badge): START, blue with a white microphone. Its PRESS
    starts the recording (capture and BEGIN at once); it then turns red with a white square and STOP
    on it; a NEW press's click stops early (the press that started it never does). Press lock: a
    finger held through the 3 s never lands on what replaces it. After recording the centre is SEND
    (accent, 74 px) over CANCEL (64 px); SEND is grey until the words arrive, grey "SENDING" while it
    goes, and grey with "UPSTAIRS / offline" or "needs update" when the other panel cannot take it.
  - right (x 426-764): the text box (field colour, 92 px; B20, B16 if the words need it): "The words
    show here", "Listening", "Turning speech into text" or "Waiting for the other floor's message"
    with stepping dots, the words, or the error in the danger colour. Under it TRY AGAIN (records
    again, the old words gone) and KEYBOARD (the edit card above the keyboard, with the blue mic key
    and tap-to-place-cursor; its Done comes back to the row; a tap on the text box does the same).
    KEYBOARD from the empty box types a message from scratch.
  - CLOSE at the top right at any time (cancels a recording; a SAY already out keeps its retries).
- Idle close 60 s while ready, on the result or on an error. Day and night palettes as the other
  sheets. All of it lives in the LVGL heap (PSRAM): no new static RAM, no task.

Before 2026-11-21 (superseded):

- New hit object `t.badge_hit = plain(t.cont, 287, 97, 99, 96)` (tile x 287-386, y 97-193),
  created after t.note_hit and before the chevron is brought forward (src/ui/ui.cpp:286-290). It
  clears the note hit (x < 285) and the mail, no-mail and arrow hits (y < 90). It overlaps the
  acted strip (x < 300) only on the upstairs tile, and that strip is shown only on the downstairs
  panel, where the upstairs tile is not the own tile, so the two are never live together. The whole
  badge picture (x 289-369) is inside it (check fix C1). Clickable on the own tile only,
  switched with t.zone (src/ui/ui.cpp:427-431). LV_OBJ_FLAG_PRESS_LOCK set explicitly.
- Touch areas after this change (decision 4): badge = recorder; the small status text (note hit)
  = tile-message editor, unchanged; the rest of the zone (the status word) and the chevron = the
  status sheet, unchanged. Nothing else on the tile changes.
- LV_EVENT_PRESSED on badge_hit: capture starts and BEGIN goes out (no tick sound: the speaker is
  next to the mics). Sheet::Voice opens (dim overlay). A tap and a hold behave as in 2.1 (0.6 s
  rule; PRESS_LOST before 0.6 s cancels). The dim overlay's first draw blocks LVGL for about 0.3 s
  (up to 0.6 s), so a quick tap's release is only seen after it: a release that LVGL first sees
  before the card's first draw finished always counts as a tap, never as the end of a hold (check
  fix C2).
- Card 600 x 300 centred. Title "Message to DOWNSTAIRS" (the other floor's word). Under it
  "Speak now: 3 seconds". A 140 px ring drains over 3 s around the countdown digit (J64); level
  bar; STOP and CANCEL. The ring arc is redrawn at 10 Hz (about 20,000 px, about 15 ms at 1.3
  px/us).
- (P0, owner default) The 3.0 s cap counts from the PRESS, and the countdown is shown from the press
  on both the badge card and the keyboard strip (kVoiceRecMs). The card's first draw (about 0.3 s)
  eats into the 3 s the person sees; the clip is 3.0 s at most (102 ADPCM chunks, 104 allowed).
  Superseded: "countdown from the card's first draw, 0.3 s kept, 3.3 s clip".
- After stop: "Turning speech into text" with stepping dots and CANCEL (sends VOICE_ABORT).
- Result screen: the text in B26 (3 lines, 120 chars), and SEND TO DOWNSTAIRS (accent), EDIT,
  TRY AGAIN, CANCEL. EDIT opens the text in a 120-char field with the keyboard (with its mic key
  and, after piece H, tap-to-place-cursor); its Done returns to the result screen.
- SEND: "Sending", then on VOICE_GOT for the SAY a toast "Delivered" and the sheet closes. After
  1.5 s of retries: "DOWNSTAIRS didn't answer" with TRY AGAIN and CANCEL. Not queued for later.
- Other panel shown No signal, or not advertising voice (4.8): SEND is replaced by a grey
  "DOWNSTAIRS panel is offline" or "DOWNSTAIRS panel needs the update"; the text can still be read.
- Result screen idle close: 60 s (same pattern as src/ui/ui_note.cpp:152-161).
- A doorbell ring (Sound::Chime) while recording aborts the recording (3.4); the sheet says
  "Recording stopped for the bell". The banner rises over any sheet already
  (src/ui/ui_common.cpp:260).

### 2.3 Arrival on the other panel

- New frame SAY (4.3), not a tile message (tile messages are latest-wins, owner-cleared and
  re-sent at boot, src/app/controller.cpp:941-962).
- Inbox of up to 4 messages, saved on every change (new Platform::save_voice_inbox, like
  save_notes); a 5th drops the oldest. Dedup by message id (u32); the last 8 ids are saved with the
  inbox so a retried SAY never shows twice, even across a restart.
- Dialog (fixes review H3 and H4):
  - Place: over the SENDER's tile on the receiving panel, status zone only, tile x 0-386, y 90-193
    (screen y 164-267 for upstairs, 369-472 for downstairs). The top 90 px stay uncovered, so the
    mail, no-mail and arrow badges stay visible and live, and the weather zone (x >= 386) too.
  - Under it, only on the downstairs panel and only while "Action taken on temperature" is shown:
    the acted strip (tile y 136-186). It is information; its tap only clears it
    (src/app/controller.cpp:1356-1362). It is back after the one tap that dismisses the message.
  - Content: line 1 "FROM UPSTAIRS  9:42 PM" in B14, "+1" at its right when the inbox holds more.
    Text in B20 if it fits 3 lines of 362 px, else B16 (120 chars always fit 3 lines of B16).
    Cream card, 3 px border in the sender floor's colour, 200 ms fade like the banner. Exact
    heights are checked in the sim screenshots (piece C).
  - Never shown while a sheet is open: an arrival during a sheet waits, and close_sheet() shows it
    (src/ui/ui_common.cpp:238-247 gets the hook). Not shown while the screensaver runs (it is
    stopped first when the ding rules say so, table below; otherwise the dialog waits for the next
    reveal).
  - Taps are ignored for the first 700 ms after it becomes visible, so a tap already under way
    cannot dismiss it unread.
  - The doorbell banner is moved to the foreground after the dialog is shown, so a ring is never
    under it.
- One tap anywhere on the dialog (after the 700 ms guard): that message leaves the inbox and is
  saved; the next one (oldest first) shows. Nothing goes back to the sender. A phone's Got it
  removes it the same way (section 7).
- Ding: Sound::Ding, one `strike(s, G5, 0, 0.7f, kBar)`: the same bell-bar timbre, an octave
  under the dog alert's G6 and unlike SoftChime's E6 start. Rendered when needed and freed after
  playing (3.5). Played at soft_volume(). Mike confirms by ear with serial `sound ding`
  (alternative D6 if G5 sounds too dull).
- Ding and wake:

| Receiver state | Ding | Screensaver | Backlight | Dialog |
|---|---|---|---|---|
| Open or Busy, outside quiet hours | yes | stopped (screensaver_stop, src/ui/ss_host.h:48) plus lv_disp_trig_activity() | voice_wake for touch_wake_ms, like mail_wake | shown (or after the sheet closes) |
| Private, Sleeping or Away | no | left running | no wake | shown when next revealed |
| Quiet hours (settings.h:116) | no | left running | no wake | shown when next revealed |
| Screen-off hours (settings.h:121) | no | n/a | stays off | shown when next lit |
| This panel is recording | deferred until recording ends (3.4) | as above | as above | after the recorder closes |
| A doorbell banner up | no extra ding | banner rules | banner rules | under the banner |

  Order on arrival: screensaver_stop() first (it ends the run at once, src/ui/ss_host.cpp:847),
  then audio_play(Ding), so the 66 KB render never competes with the screensaver's PSRAM (check
  fix C3).
  New BacklightInputs flag `voice_wake` (src/core/brightness.h:10-21), set like mail_wake
  (src/app/controller.cpp:83, 1502-1503, 2076).
- Then the receiving panel sends VOICE_PUSH to the C6 so its floor's phones get the push
  (section 7). (P0, owner default) Only when this receiving floor is Open or Busy and outside quiet
  hours, the doorbell's rule (kVoicePushOpenBusyOnly); phone_bell keeps its own quiet hours too.

### 2.4 Shared states and limits

- One voice job per panel (keyboard or recorder). The C6 holds one session per floor, so both
  floors can upload at once; the Pi recognises one job at a time with a queue of 2 (6.3).
- A press is impossible while the screensaver runs (the first touch only reveals,
  src/ui/ss_host.cpp:14-20).
- The clip buffer is its own allocation (3.3), not the screensaver's spare picture.

## 3. Audio capture (panel)

### 3.1 Format

- Captured at the codec's 44.1 kHz stereo 16-bit. The codec is not reopened at 16 kHz (TX and RX
  share the clock, src/ws/.../audio_codec_data_i2s.c:279-283).
- Downmix (L+R)/2. Resample 44,100 -> 16,000: two cascaded biquad low-passes at 6.5 kHz (Q 0.54
  and 1.31), linear interpolation at the 16 kHz instants. Float, about 186 outputs per 512-frame
  block, about 0.1 ms per 11.6 ms block (estimate; measured with micros() in piece A).
- Field fix 2026-11-21 (the field test: 36 % word errors; owner: less compression): the default is
  now codec 1, mu-law, 237 samples a chunk, 210 chunks a clip (3.0 s is about 203; about 68 DATA
  frames a second), kVoiceMaxChunks 255 (all the u8 fields' range). ADPCM stays as codec 2, and
  serial `voice codec 1|2` picks one until restart. A level boost (kVoiceBoostDb 15 dB, soft limiter
  above -7 dBFS, never past -0.8 dBFS; serial `voice boost N`) runs on the 16 kHz samples after the
  resampler, before either encoder. The text below is the original design.
- Codec by capability (Mike, 2026-11-21): a panel records in mu-law only when the C6 has said it takes
  it, otherwise ADPCM. C6 0.6 sends its voice hello (VOICE_GOT kind 30) with key 1 ("takes mu-law and
  255 chunks"); 0.5 sent key 0 (ADPCM, at most 128 chunks). Upstairs records the key it last heard and
  passes it to downstairs in its STATE (kStateGwMulaw 0x40, beside kStateGwVoice); the hellos stopping
  for 10 minutes clears both. Serial `voice codec 1|2` still sets the codec by hand until restart, and
  `voice codec 0` gives it back to the gateway. So a new panel with a C6 0.5 works (ADPCM) instead of
  ending in "Gateway needs an update".
- Codec 2 (the default until 2026-11-21): IMA ADPCM 4-bit, 474 samples per chunk (237 data bytes), each chunk carrying
  its own predictor (i16) and step index (u8), so chunks decode alone. Nibble order: LOW nibble
  first. The Pi decoder is our own Python (audioop was removed from Python 3.13, so v1's audioop
  question, review M11, is gone); the golden vectors (12.1) bind the C++ encoder and the Python
  decoder to the same order.
- Codec 1 (the default since 2026-11-21; first planned as the fallback, same framing): mu-law 8-bit, 237
  samples per chunk. Originally only to be used if the Pi-side ADPCM decode proved too slow (13.3); BEGIN names the codec, so switching is a panel setting, not
  a protocol change.
- ADPCM at 16 kHz = 8,000 B/s. 3.3 s = 52,800 samples = 112 chunks (34 frames/s on the air).

### 3.2 Where the code runs, and the handshake (fixes review M3)

- A tap in mic_task right after mic_i2s_read (src/board/board_audio.cpp:79): when `g_rec` is set,
  `voice_rec_feed(rec, buf, 512)` resamples and encodes. mic_task stays the only I2S reader and
  keeps filling g_mic.
- mic_task also makes the FIRST send of each finished chunk itself (board::radio_send_voice, 4.6),
  and sends END itself when it sees the stop, so a long LVGL redraw (0.3-0.6 s,
  docs/hardware-notes.md:85) never delays the stream or the end. Resends, ACK bookkeeping and
  timeouts stay on the LVGL side (voice_tx, 4.5).
- (P0, check finding 6) mic_task never makes a first send of a chunk 32 or more past base
  (rec->base, written by LVGL from each VOICE_ACK; voice_in_window). It first-sends strictly in
  order; a chunk outside the window waits, and mic_task retries on each block (11.6 ms). END goes
  out only after every chunk has had its first send. LVGL never makes a first send; it only resends
  chunks below rec->first_sent.
- Ownership handshake, the same shape as g_mic_pause/g_mic_paused (board_audio.cpp:59, 66-72):
  1. LVGL allocates the VoiceRec, fills it, then sets `g_rec = rec` (release store).
  2. mic_task reads g_rec once per block into a local; `g_rec_busy = true` while it uses it.
  3. To stop: LVGL sets `rec->stop = reason`. mic_task flushes the last partial chunk, sends it
     and END, sets `rec->done = true`.
  4. To free (text shown, cancel, error): LVGL sets `g_rec = nullptr`, waits until
     `g_rec_busy == false` (at most one block, polled from the 50 ms tick, never a busy loop),
     then free_big. A free never happens while mic_task is inside voice_rec_feed.
- The double accumulator in mic_task becomes int64 (audit-2026-11-11.md:67) to pay for the tap.
  Mic task stack 3072 -> 4096; the high-water mark goes into `info`.
- Resampler, encoders and the level detector are plain C++ in src/core/voice_dsp.{h,cpp}.

### 3.3 Buffer

- At the press: alloc_big(112 x 240 + 64) = about 27 KB in PSRAM for the clip (chunks kept until
  the text comes back, so any chunk can be resent). Failure -> "Not enough memory to record".
- Single writer (mic_task), single reader (LVGL voice_tx), an atomic `chunks_ready` counter.

### 3.4 Level, early stop, gain, chime clash

- Level: the block power already computed. Noise floor = slow minimum of quiet blocks (room about
  -48 dBFS, board_audio.cpp:61-63). Speech onset = block above floor + 12 dB in 3 of 4 blocks.
- No leading trim: everything from the press is sent (a 3.3 s clip is short, and sending from the
  first block keeps the C6 session fed, which removes v1's start race, review M2).
- Early stop (optional, on by default): after onset, 0.8 s below floor + 6 dB stops it. No onset
  by the end: END carries speech = 0, the Pi answers status 1 and the panel says "Didn't hear
  anything".
- Gain: ES7210 stays at 24 dB (codec_dev.cpp:62). The audio test (13.2) records at 24 and 30 dB;
  if 30 wins it is set only while recording and restored after (kHeardDb was tuned at 24).
- Chime clash (fixes review L6). audio_task reads a `g_recording` flag:
  - Sound::Chime (doorbell, ringtone) sets the atomic `g_rec_bell = true` at once, then plays.
    Never delayed. audio_task never touches the VoiceRec itself (LVGL may be freeing it, and only
    mic_task is covered by the g_rec_busy handshake); mic_task turns g_rec_bell into
    `rec->stop = bell` on its next block, and LVGL clears g_rec_bell when it frees the clip (check
    fix C4).
  - Any other sound is moved from the queue into a 4-slot deferred list held by audio_task; while
    recording, audio_task waits on its queue with a 50 ms timeout instead of forever, so it keeps
    seeing new Chimes. The newest-preview collapse (board_audio.cpp:326-328) is unchanged. When
    recording ends the deferred sounds play in order; a fifth is dropped with one log line.
  - The first 0.3 s are flagged if a sound ended under 2 s ago (amp hiss, kAmpHoldMs,
    board_audio.cpp:36); the Pi gets the clip anyway (hiss is quiet), the flag only goes in the log.

### 3.5 The ding, rendered on demand (fixes review M6, M7)

- Sound::Ding = 5 in the Sound enum (src/core/types.h:42), which moves Sound::Count from 5 to 6
  (nothing in the tree uses Count today; P0 checks again). g_clips stays [5]: Ding is never a
  resident clip, and audio_task handles Ding before the `which > 4` guard
  (src/board/board_audio.cpp:339) (check fix C5). audio_task renders it into a temporary Clip (PSRAM), plays it, frees it.
- Size: (0.7 s strike plus the synth tail, about 0.75 s) x 44,100 x 2 B = about 66 KB, held for
  about one second. Render time UNVERIFIED (estimate 30-60 ms; logged by piece A).
- Never rendered while recording (deferred like any other sound). If the alloc fails, SoftChime
  plays instead (already resident), so an arrival is never silent.

## 4. Radio protocol additions

### 4.1 Rules

- kProtoVersion stays 1. New types 20-30 are invisible to old firmware (decode() returns false,
  the frame is dropped un-ACKed, src/app/controller.cpp:699-710; the C6 0.4 ignores them too,
  gateway.cpp:249-250).
- Voice frames have their own codec, src/core/voice_proto.{h,cpp}, not the Msg union (a 237-byte
  member would grow every Msg). on_radio (controller) and Gateway::on_frame look at byte [1]
  first: 20-30 go to voice_decode(), the rest to decode() as now. msg_name() learns the names.
- Voice frames never use the standard ACK, the 10 s Dedup or the RetryQueue. Every confirmation is
  end to end inside the voice layer (VOICE_ACK for the upload, VOICE_GOT for the rest), which is
  what lets upstairs relay frames untouched (4.7). No change to RetryQueue (v1's push_raw is gone).
- The sender/MAC check (board_radio.cpp:82-104, gateway.cpp:247-248) applies unchanged: the header
  sender byte is always the device that transmitted this hop.

### 4.2 Header (6 bytes, little-endian)

`ver u8 (=1), type u8, sender u8 (this hop), seq u16 (trace only), floor u8`

floor is the panel end of the conversation: 0 upstairs, 1 downstairs. For panel -> C6 frames it is
the origin, for C6 -> panel frames the destination, for SAY the writer's floor.

### 4.3 Types

| Type | Name | Direction | Body after the header | Bytes | Confirmed by |
|---|---|---|---|---|---|
| 20 | VOICE_BEGIN | panel -> C6 | xfer u8 (1-255), purpose u8 (1 dictation, 2 message), codec u8 (1 mu-law, 2 ADPCM), chunk_samples u16 (474 or 237), max_chunks u8 (1-255; was <= 128) | 12 | VOICE_ACK |
| 21 | VOICE_DATA | panel -> C6 | xfer u8, idx u8, pred i16, step u8, len u8, data[len <= 237] | <= 249 | VOICE_ACK bitmap |
| 22 | VOICE_ACK | C6 -> panel | xfer u8, base u8 (all idx < base received), bits u32 (idx base..base+31), state u8 (1 open, 2 complete, 3 unknown xfer) | 13 | none |
| 23 | VOICE_END | panel -> C6 | xfer u8, n_chunks u8, n_samples u16, speech u8 (0/1) | 11 | VOICE_ACK state 2 |
| 24 | VOICE_ABORT | either | xfer u8, reason u8 (1 user, 2 bell, 3 busy, 4 timeout, 5 lost, 6 silent, 7 no Pi) | 8 | none: sent 3 times (0, 100, 250 ms) |
| 25 | VOICE_TEXT | C6 -> panel | xfer u8, status u8 (0 ok, 1 nothing heard, 2 failed, 3 busy, 4 timeout, 5 lost audio, 6 queued: interim), flags u8 (P0: bit 0 = the first capital is only a sentence start), len u8, text[len <= 120] | <= 130 | VOICE_GOT |
| 26 | SAY | panel -> panel | id u32 (random32), time u32 (UTC, 0 = no clock), len u8, text[len <= 120] | <= 135 | VOICE_GOT |
| 27 | VOICE_GOT | any | kind u8 (the type confirmed: 25, 26, 28, 29, 30), key u32 (xfer + status x 256 for 25, so the GOT for an interim status 6 can never clear the final text for the same xfer; id for 26/28/29; nonce for 30), result u8 (0 ok/removed, 1 gone/unknown, 2 refused) | 12 | none |
| 28 | VOICE_PUSH | panel -> C6 | id u32, from u8 (the writer's floor), time u32, len u8, text[len <= 120] | <= 136 | VOICE_GOT |
| 29 | VOICE_DISMISS | C6 -> panel | id u32 | 10 | VOICE_GOT (result 0 removed, 1 not there) |
| 30 | VOICE_PING | downstairs -> C6, direct only | nonce u32 | 10 | VOICE_GOT kind 30, direct |

- Validation in voice_decode: exact lengths (len fields must match), xfer != 0, idx < max_chunks
  (idx < 255 since 2026-11-21; was 128), len <= 237, step <= 88, codec in 1-2, text bytes 0x20-0x7E only (as notes,
  protocol.cpp:177), floor in 0-1, kind in the list. Anything else: dropped, logged once a minute.
- M1 fix: the window is 32 chunks past base, the same span as the 32-bit bitmap, so every chunk in
  flight can be confirmed.

### 4.4 Upload, end to end

1. Press: panel sends BEGIN (to the C6 by the floor's route, 4.7), starts capture at once.
2. mic_task first-sends DATA as each chunk fills (one every 29.6 ms). The C6 drops DATA for an
   xfer it has no session for; resends cover chunks sent before BEGIN landed.
3. C6 answers BEGIN with VOICE_ACK state 1, then sends VOICE_ACK after every 8 new chunks, 80 ms
   after the last DATA when a gap exists, and on END.
4. Stop: mic_task sends the last chunk and END (n_chunks, n_samples, speech).
5. C6 answers END with VOICE_ACK state 2 once every chunk 0..n-1 has been written to the Pi, prints
   voice_end, and frees the session (the text comes back by floor and xfer, 5.2). It keeps the
   last completed xfer per floor, so a resent END (its state 2 was lost) gets state 2 again, not
   state 3, and voice_end is not printed twice (check fix C6).
6. Pi recognises while the chunks arrive; the text comes back as VOICE_TEXT; the panel answers
   VOICE_GOT and shows it.

### 4.5 Panel transfer logic (src/core/voice_tx.{h,cpp}, plain C++)

- BEGIN resent at 0/150/300 ms until any VOICE_ACK for the xfer. Downstairs on route auto: no
  answer in 450 ms direct -> the same BEGIN via upstairs (4.7) for 1.05 s more. No answer at all
  in 1.5 s -> "Gateway needs an update or is off" (and an ABORT so the chunks stop).
- Resends (LVGL tick, 50 ms): a chunk not set in the latest bitmap and first-sent >= 200 ms ago,
  oldest first, at most 4 per tick, at most 5 sends per chunk, total sends capped at 3 x n_chunks.
  Never more than 32 chunks past base.
  Field fix 2026-11-21 (mu-law's 68 chunks a second fill the 32-chunk window in 0.47 s): at most 6
  sends per chunk; resends go in rounds (one pass over the owed chunks, oldest first) with 160 ms of
  quiet after a round that sent anything, because the C6 0.5 sends its gap or duplicate ACK only
  80 ms after the LAST DATA it saw; and a probe: base stuck 400 ms with nothing due -> one chunk the
  bitmap shows received goes again (every 300 ms), so the C6 answers with where it is.
- END resent every 200 ms until VOICE_ACK state 2; 1.5 s without it -> ABORT timeout.
- base not moving for 1.5 s while chunks are owed -> ABORT timeout ("Couldn't send the
  recording").
- Text wait: 8 s after VOICE_ACK state 2 (budget 9.2 is under 2 s). A VOICE_TEXT status 6
  (queued behind the other floor) shows "Waiting for the other floor's message" and extends the
  wait once to 20 s. Timeout -> "The Pi didn't answer". CANCEL sends ABORT user; a later TEXT for
  a dead xfer is answered with VOICE_GOT and ignored.
- VoiceOut: 4 retry slots for SAY and VOICE_PUSH (frame bytes, dest, kind, key), tries at
  0/150/400/750/1150 ms, give up at 1.5 s (the RetryQueue schedule), cleared by the matching
  VOICE_GOT. SAY expiry marks the peer No signal like a failed KNOCK (it was only offered because
  the peer advertised voice, 4.8). VOICE_PUSH expiry: one log line, dropped (like a NOTICE,
  src/app/controller.cpp:106-109).

### 4.6 Radio API for voice (fixes review M5)

- New `bool board::radio_send_voice(uint8_t to, const uint8_t *d, size_t len)` and
  `Platform::radio_send_voice`: returns true when esp_now_send accepted it; no trace ring entry
  (so a 112-chunk upload never wipes the doorbell diagnostics), no elog per error; its own counters
  voice_out, voice_err, shown by serial `radio`. Safe from mic_task and the LVGL task (counters
  are per-caller or atomic).
- on_recv (src/board/board_radio.cpp:80-104) also skips trace() for types 20-30 and counts them
  as voice_in instead: on upstairs a relayed upload is 34 frames/s inbound, which would wipe the
  24-entry ring as fast as the sends would (check fix C7).
- An esp_now_send error (ESP_ERR_ESPNOW_NO_MEM) just leaves the chunk unsent; the resend logic
  picks it up. In flight is bounded by the 29.6 ms chunk pace plus at most 4 resends per 50 ms.
- radio_send (existing) is unchanged.

### 4.7 Downstairs route: direct or relayed by upstairs (decision 7, review M9)

- Route setting per panel: auto (default), direct, relay. Upstairs is always direct (the proven
  link). Serial `voice route [auto|direct|relay]` shows or forces it.
- Auto on downstairs: VOICE_PING to the C6 directly at boot, every 10 min, and after any failed
  direct BEGIN. A VOICE_GOT kind 30 within 300 ms = direct is good; 3 missed pings in a row =
  relay until the next good ping. The reach test (13.0) sets the starting default.
- Relay on upstairs (no LVGL in the path, so redraw stalls cannot drop relayed frames):
  - board_radio's on_recv puts voice frames that need relaying on a new g_relay queue (32 frames,
    storage in PSRAM, about 8 KB), everything else on g_rx as now.
  - A small task "vrelay" (core 0, prio 3, 2560 B stack) drains g_relay: sets byte [2] to its own
    sender id and calls radio_send_voice to the next hop. Peers and keys already exist on both
    sides (board_radio.cpp:150-159, gateway-c6.ino:157-166).
  - The rule is one pure function, `voice_relay_to(type, kind, from, floor, me)` in voice_proto.cpp
    (host-tested): from downstairs with floor 1 and type 20, 21, 23, 24, 28, or 27 confirming 25 or
    29 -> gateway; from the gateway with floor 1 and type 22, 24, 25, 29, or 27 confirming 28 ->
    downstairs; SAY, VOICE_GOT for SAY and PING are never relayed.
- The C6 replies to downstairs by the way the session's last frame came (session.via: direct or
  upstairs). For a C6-started frame (VOICE_DISMISS) it uses down_via, the route of the last frame
  heard from downstairs, default upstairs until downstairs has been heard directly.
- Relay airtime: 34 frames/s in and out of upstairs during an upload, for 3.3 s.

### 4.8 Capability bit

- New StateFlags bit kStateVoice = 0x10 (src/core/protocol.h:109), set by a voice-capable panel in
  every STATE. decode does not check unknown bits (protocol.cpp:142-147). PeerView gets `voice`;
  SEND and the relay are offered only when it is set. The C6 advertises voice only by answering
  BEGIN.

## 5. C6 gateway 0.5 (../gateway/gateway-c6)

### 5.1 Radio side

- kFirmware "gateway-c6 0.5" (gateway.h:14). GatewayIO::send_frame(int to, data, len); the sketch
  sends to kPanels[to] (gateway-c6.ino:113-120); existing callers pass kUpstairs.
- RX queue 16 -> 32 frames, and a drop counter on the status screen and in the hello line (review
  L7).
- New VoiceSession in src/gw/voice_session.{h,cpp} (plain C++), two instances, one per floor:
  xfer, codec, max_chunks, received bitmap 128 bits (255 since 2026-11-21, sized from
  kVoiceMaxChunks), via, last_rx, state. Accepts types 20, 21,
  23, 24, 27, 28, 30 from either panel (on_frame today drops all but ACK and upstairs NOTICE,
  gateway.cpp:251-256). A new BEGIN with another xfer replaces that floor's session.
- Cut-through, no clip buffer. A DATA chunk is marked received only if it was written to the
  serial port whole, and it is written only if `Serial.availableForWrite() >= line_len + 1100`
  (fixes review H2: two full control lines, 2 x 512 + margin, always stay free for a notice event
  or an acked/failed reply). TX buffer raised 2048 -> 4096 (gateway-c6.ino:296), leaving room for
  about 7 voice lines. A held-back chunk shows as a gap in the next VOICE_ACK and is resent by the
  panel.
- Session idle timeout 2 s without a frame: voice_abort line to the Pi, VOICE_ABORT timeout to the
  panel. (Chunks flow every 29.6 ms from the press, so this never fires on a slow talker.)
- VOICE_PING: VOICE_GOT kind 30 straight back to downstairs, direct.
- VOICE_PUSH: printed once as a voice_msg line (dedup ring of the last 8 ids), then VOICE_GOT kind
  28 by the frame's route. If the Pi has not said hello with voice, result 2 (refused).
- No Pi with voice (no hello voice:1 since the bridge connected): BEGIN is answered with
  VOICE_ABORT reason 7.

### 5.2 Serial lines

C6 -> Pi (events, "f" = floor):

```
{"event":"voice_begin","x":7,"f":"down","purpose":"message","codec":2,"cs":474,"via":"direct"}
{"event":"voice","x":7,"f":"down","i":12,"p":-311,"s":41,"d":"<base64 of 237 B, 316 chars>"}
{"event":"voice_end","x":7,"f":"down","n":96,"ns":45504,"sp":1}
{"event":"voice_abort","x":7,"f":"down","reason":"user"}
{"event":"voice_msg","id":3735928559,"to":"downstairs","from":"upstairs","time":1763650000,"text":"..."}
```

- The voice line is about 370 B (under kMaxLine 512). voice_msg text is JSON-escaped by the C6
  (only `"` and `\` can occur in 0x20-0x7E); worst case about 380 B.

Pi -> C6 (requests, each answered by one acked/failed/rejected reply like the others):

```
{"type":"voice_text","id":N,"x":7,"floor":"downstairs","status":0,"fl":1,"t0":"<= 60 chars","t1":"<= 60 chars"}
{"type":"voice_ack","id":N,"floor":"downstairs","msg":3735928559}
```

- Text split in two fields because JField.s holds 63 (json_line.h:19); the C6 joins them.
- New Kind slots: kVoiceTextUp 6, kVoiceTextDown 7, kVoiceAckUp 8, kVoiceAckDown 9; kKinds 10.
  The same 5-try schedule and give-up as the other kinds. Pending.frame holds the raw voice frame
  (gateway.h:63-71 keeps bytes). These slots are matched by VOICE_GOT (kind, key), not by ACK seq.
- Pending gains `to` (panel index) and the route used, because send_try today always sends to
  upstairs (gateway-c6.ino:113-120, gateway.cpp:45-50) and the give-up line always names upstairs
  (gateway.cpp:283-284); voice slots fill the floor in from the slot (check fix C8).
- The text slots are latest-wins per floor (an interim status 6 gives way to the final text; the
  bridge gets "superseded" for the interim). The dismiss slots are NOT latest-wins: two Got it taps
  on two messages for the same floor within 1.5 s would otherwise supersede the first and its phone
  would see "Couldn't reach the panel". A voice_ack while that floor's slot is busy gets
  `"rejected":"busy"`, and phone_bell retries it once 1.6 s later (check fix C9).
- Replies: `{"type":"voice_text","id":N,"acked":"downstairs","seq":S,"ms":M}`,
  `{"type":"voice_ack","id":N,"acked":"downstairs","result":"removed"|"gone","seq":S,"ms":M}`,
  or `"failed"` after 1.5 s. kind_name and the 20-character screen_name ("vtext", "vack") follow
  the existing pattern (gateway.h:80-93).
- Hello: the bridge sends `{"cmd":"hello","voice":1}`; the C6 remembers pi_voice and adds
  `"voice":1`, `"drops":N` and (P0, check finding 1) `"down_peer":true|false` (the downstairs
  panel is registered as an ESP-NOW peer with its key) to its hello reply. A C6 0.4 ignores the
  extra field.

## 6. Pi STT service (PI stt_service.py, new)

### 6.1 Engine (decision 8, measured)

- Vosk vosk-model-small-en-us-0.15 with a runtime grammar built exactly as PI stt/bench.py:54-60
  does it: `json.dumps([" ".join(words), "[unk]"])`, words from house_vocab.txt (about 970 words).
- Measured on the real Pi 2 (600 MHz cap, Raspbian 13, Python 3.13, venv ~/stt-bench, libvosk.so
  execstack flag cleared), 100 ms chunks at real time: final text 0.68-0.72 s (median) and at worst
  0.92 s after the last chunk; RTF about 1.2 (lags 0.3-0.6 s behind live audio by the end of a 2-3
  s phrase); model load 7 s; peak RSS 133 MB; word errors 7% on 120 TTS clips, 9% on 80 held-out
  clips; single thread at nice 15, no undervoltage. Full vocabulary without a grammar: 6x slower
  than real time, so the grammar is required.
- Not yet measured: recognizer construction time with the grammar (the bench built one per clip
  before timing, bench.py:80-82), whether Reset() keeps the grammar, and the numbers with all four
  services busy (13.3).

### 6.2 Processes and threads

- One process, model and grammar loaded once at start (7 s); the socket is bound only after the
  load, so an early clip sees "isn't listening" at the bridge and the panel says "The Pi didn't
  answer".
- Receive thread, normal priority: blocks in recv on ~/tcomm-gateway/stt.sock (AF_UNIX
  SOCK_DGRAM, umask 077, chmod 600, the phone_bell.py open_listener pattern, PI
  phone_bell.py:929-943), SO_RCVBUF raised to 256 KB (the kernel caps it at rmem_max). (P0, check
  finding 7) It ONLY calls recv and puts each datagram on an in-memory queue: nothing else, so it
  is always back in recv at once. Fixes review M4: reading is never starved by the niced engine.
- Decode thread, normal priority (separate from the receive thread): takes datagrams off that
  queue, parses the line, base64-decodes, decodes ADPCM (or mu-law) to 16-bit PCM with a
  table-driven decoder (89 x 16 precomputed diff and next-index tables) and hands PCM to the job.
- Recognizer thread, nice 15 for this thread only: `os.setpriority(os.PRIO_PROCESS,
  threading.get_native_id(), 15)` at thread start (Linux applies it per thread). Feeds Vosk in
  100 ms batches (1600 samples), as the bench did. Vosk's cffi calls release the GIL, so the
  receive thread keeps running.
- Sender thread, normal priority: sends one line per op to bridge.sock with sendall (one write,
  review L5), one connection per op (the bridge accepts one op per connection and keeps up to 8
  waiting, PI tcomm_bridge.py:182, 1011-1041). It does not wait for one reply before sending the
  next op: replies are read with select over the open connections, up to 6 s each, and only the
  reply word is logged. A blocking wait would let one floor's slow VOICE_GOT (up to 1.5 s of C6
  retries) hold the other floor's final text past the 2 s target (check fix C10).
- Audio is never written to disk: PCM lives in memory per job and is dropped when the job ends.
  The unit also forbids it (ProtectHome read-only, 6.5).

### 6.3 Jobs

- A job per (floor, x). At most 2 jobs: one being recognised, one waiting (review M8). A third
  BEGIN while two exist: VOICE_TEXT status 3 (busy) for it.
- The waiting job buffers its PCM and gets an interim status 6 (queued) at once so its panel can
  say so; it is fed as soon as the first finishes (unpaced: 3.3 s of audio at RTF 1.2 is about 4
  s).
- Chunk order: chunks are fed in idx order. A missing chunk is waited for until a later voice_end
  says the C6 has written everything; then up to 3 missing chunks (90 ms) are filled with silence
  and recognition goes on; more than 3 missing -> status 5 "Lost part of the recording". The C6
  only prints voice_end after every chunk was written, so a gap at the Pi means a loss between the
  C6 and the service (bridge sendto EAGAIN, a restart).
- voice_end with sp = 0 -> status 1 without waiting for Vosk's result. A job with no frame for 2 s
  and no end is dropped silently (the C6 already told the panel). voice_abort drops the job; if it
  is being recognised, feeding stops and the recognizer is reset.
- voice_reset (the C6 rebooted, sent by the bridge) drops every job.
- Hard limit: no final text 10 s after voice_end -> status 4 and the process exits non-zero
  (systemd restarts it; model reload 7 s).
- Lucy Gate (review H1): at job start the service reads ~/weathergate/lucy_status.txt (PI
  lucy_gate.py:88). If it says ALERT and its mtime is under 3 s old (the 1 s jingle is playing),
  feeding is held until 3 s after that mtime; the audio waits in memory. The text is late only in
  that rare case.
- Recognizer reuse: after FinalResult, rec.Reset() if it keeps the grammar (checked in 13.3),
  else a fresh KaldiRecognizer is built right after each job, before the next is likely, so
  construction is never on the latency path.

### 6.4 Text clean-up (post-process)

In order, on Vosk's lower-case words:
1. Drop "[unk]".
2. "two" back before time words: the rule from PI stt/bench.py:23-31 (to + o'clock, thirty,
   fifteen, forty, minutes, hours, days, weeks, of, more, unless after going, have, need, want,
   used).
3. "i" -> "I", and "i'm", "i'll", "i've", "i'd" -> "I'm", "I'll", "I've", "I'd".
4. Names capitalised from ~/tcomm-stt/names.txt (one name per line, mode 600, Pi only).
5. First letter capital; spaces collapsed; ASCII 0x20-0x7E only.
6. Cut to 120 chars at a word boundary (3 s of speech is about 8-12 words, so this is rare).
7. Empty -> status 1.

The result goes to the bridge as one op (6.6). Transcripts are never logged: the log line is
`job x=7 floor=downstairs: 96 chunks 2.9 s, text 0.74 s after end, 9 words, status 0`.

### 6.5 Files and unit

- Code: PI stt_service.py (repo folder ~/weathergate). Engine: ~/tcomm-stt/venv (python3.13 -m
  venv, `pip install vosk` from the same wheel as the bench, then the same execstack clear on
  libvosk.so), model ~/tcomm-stt/vosk-model-small-en-us-0.15 (moved from ~/stt-bench),
  ~/tcomm-stt/house_vocab.txt (copied from PI stt/house_vocab.txt), ~/tcomm-stt/names.txt.
  Settings ~/tcomm-gateway/stt.conf: early-stop and timing values only, no secrets. Nothing is
  compiled on the Pi and no swap file is made (review H1).
- PI systemd/stt.service:

```
[Unit]
Description=WeatherGate speech to text for TComm voice notes (Vosk, grammar)
After=tcomm-bridge.service

[Service]
Type=simple
User=<pi user>
WorkingDirectory=/home/<pi user>/weathergate
ExecStart=/home/<pi user>/tcomm-stt/venv/bin/python /home/<pi user>/weathergate/stt_service.py run
UMask=0077
Restart=always
RestartSec=10s
StandardOutput=journal
StandardError=journal
SyslogIdentifier=stt
Environment=PYTHONUNBUFFERED=1
TimeoutStopSec=10
# One engine thread (niced to 15 inside the program); the rest is light.
CPUQuota=130%
MemoryMax=250M
TasksMax=16
# No network at all, and no writes outside the socket folder.
RestrictAddressFamilies=AF_UNIX
IPAddressDeny=any
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=full
ProtectHome=read-only
ReadWritePaths=/home/<pi user>/tcomm-gateway

[Install]
WantedBy=multi-user.target
```

- Nice is not set for the unit (the receive thread must keep up); only the recognizer thread is
  niced. CPUQuota 130% caps the whole service at one core plus headroom.
- Capacity: RSS about 133 MB peak (MemoryMax 250M); disk about 40 MB model plus about 20 MB venv
  (estimate); CPU about one core for about 4 s per message, idle otherwise.

### 6.6 Bridge ops it sends

```
{"op":"voice_text","x":7,"floor":"downstairs","status":0,"flags":1,"text":"Dinner is ready"}
```

The bridge validates (x 1-255, floor, status 0-6, flags 0-1, text printable ASCII <= 120), splits
the text into t0/t1 at <= 60 chars, writes the voice_text request line with "fl" and relays the
C6's reply. (P0) flags bit 0 is set by the stt service when the text's first capital comes only
from clean-up step 5 (sentence start), not from a name or "I".

## 7. The phone push and the phone dismiss (decision 6)

### 7.1 Path, end to end

```
receiving panel --VOICE_PUSH--> C6 --voice_msg line--> bridge --datagram--> phone.sock --> phone_bell
phone_bell --HTTPS--> ntfy --> phones of the receiving floor
phone "Got it" --HTTP POST on the LAN--> phone_bell /voice/<token>
phone_bell --op voice_ack--> bridge --voice_ack request--> C6 --VOICE_DISMISS--> receiving panel
panel removes it, VOICE_GOT result --> C6 --reply acked + result--> bridge --> phone_bell --> HTTP reply
```

Downstairs panel legs go direct or via upstairs (4.7).

### 7.2 Bridge (PI tcomm_bridge.py)

- `_on_event`: voice_msg -> phone.sock only, as `{"voice_msg":msg,"received":now}` (< 1024 B,
  NOTICE_MAX_BYTES, :169). Log: `Voice message for downstairs (id 1234), passed on`; never the text.
- New op `{"op":"voice_ack","floor":"downstairs","msg":N}` (msg 1 to 4294967295) through
  _op_body/_start_op and the same reply relay as mail_ack (PI tcomm_bridge.py:959-999); the op name
  joins the list at :1045.

### 7.3 phone_bell (PI phone_bell.py)

- Engine.handle: a datagram with "voice_msg" goes to `_voice_msg(to, from, mid, t, text,
  received, now)`:
  - topic of the RECEIVING floor (`to`); no topic -> "off".
  - quiet hours -> no push (never quiet_ok); older than MAIL_STALE_S (15 min, :87) -> no push.
  - dedup: an id pushed already -> "duplicate".
  - token = secrets.token_urlsafe(16); stored as {floor, mid, created, state "open"}; at most 8
    kept (oldest dropped); valid voice_token_h (new [rules] key, default 12 h, 1-24).
  - push:

```
{"topic": <to's topic>, "title": "Message from Upstairs", "message": "<the text>",
 "priority": 3, "tags": ["speech_balloon"],
 "click": "<base_url>/voice/<token>",
 "actions": [{"action": "http", "label": "Got it", "method": "POST", "clear": true,
              "url": "<base_url>/voice/<token>"}]}
```

  (title "Message from Downstairs" for the other way). Priority 3, no time-sensitive flag, so
  Focus and Do Not Disturb still win. No base_url -> no buttons and no click.
- Routes: `POST /voice/<token>` -> Engine.voice_ack; `GET /voice/<token>` -> a small page with one
  big Got it button that POSTs to the same path (for a tap on the push itself; GET never acts,
  as the ring page, :853-856).
- Engine.voice_ack(token, now) mirrors mail_ack (:666-700): hmac.compare_digest on the token;
  unknown or expired -> 410 "This has expired"; used -> 409 "Already done" (the second downstairs
  phone); else state "used" and op voice_ack. Reply acked with result removed -> 200 "Got it";
  result gone -> 200 "Already dismissed at the panel" (state stays used); anything else -> state
  back to open, 502 "Couldn't reach the panel".
- Logs: `Voice message for downstairs: pushing`, `Phone tap: voice -> 200`. Never the text, the
  token, the topic or the address.

### 7.4 Panel side

- Receiving panel: after a new SAY is saved in the inbox, VOICE_PUSH {id, from, time, text} goes
  to the C6 (VoiceOut, 4.5). The VOICE_GOT result 2 (Pi without voice) is logged only.
- VOICE_DISMISS {id}: removes the message from the inbox if there (save, dialog updates), answers
  VOICE_GOT result 0; not there -> result 1. Only accepted from the gateway (directly, or from
  upstairs relaying with floor = downstairs).
- A dismissal at the panel sends nothing; a later phone tap gets "Already dismissed at the panel".

## 8. Tap to place the cursor (decision 9, piece H, after voice works)

### 8.1 Behaviour

- Both editors: the note field (2 lines, src/ui/ui_note.cpp) and the calendar title and notes
  fields (src/ui/ui_calendar.cpp), and the EDIT field of the recorder (2.2).
- A tap inside the focused field puts the cursor at the nearest character boundary to the finger
  (midpoint rule on each glyph). Tapping an unfocused calendar field focuses it and places the
  cursor at the tap (past the end of the text = at the end). The field's existing CLICKED handler
  (field_clicked -> set_focus -> refresh_fields, src/ui/ui_calendar.cpp:1862-1864, 1184-1197) runs
  after PRESSED, so set_focus must keep the cursor PRESSED placed instead of moving it to the end,
  and the title's capital-first rule (ui_calendar.cpp:1186) looks at the cursor (cursor 0, or after
  ". "), not at an empty field (check fix C12).
- Press and slide moves the cursor along live (PRESSING, at most 20 updates/s), for fingers too
  fat to hit a gap first time.
- Every key inserts at the cursor; backspace deletes the character before it (its repeat stays,
  src/ui/ui_calendar.cpp:1227); voice text goes in at the cursor with the spacing rule of 2.1.
- The cursor stays visible: in the note field it is drawn on the line that holds it; in a
  calendar field the shown window follows the cursor (it replaces fit_tail's tail-only view,
  src/ui/ui_calendar.cpp:1084, 1106).
- Limits stay as they are: the note's 2-line wrap check runs after an insert at the cursor and
  undoes it if a third line would be needed (ui_note.cpp:120-137); calendar fields keep their
  lengths. The note field's no-leading/no-double-space rule looks at both neighbours.

### 8.2 Code

- src/core/text_edit.{h,cpp} (plain C++, host-tested):

```
struct TextEdit { char *buf; size_t cap; int cursor; };
bool te_insert(TextEdit &t, char c);              // false if full; cursor moves on
bool te_insert_str(TextEdit &t, const char *s);   // stops at the first refused char
void te_backspace(TextEdit &t);
void te_set(TextEdit &t, int i);                  // clamped 0..len
using WidthFn = int (*)(const char *s, int n, void *ctx);
int te_hit(const char *s, int a, int b, int x, WidthFn w, void *ctx);   // index in [a, b] nearest x
int te_window(const char *s, int cursor, int width, WidthFn w, void *ctx, int *first);  // calendar view
```

- The note editor keeps note_wrap (ui_note.cpp:33-60); the line holding the cursor is the wrap
  range that contains it (a cursor exactly at a wrap break belongs to the next line, except at the
  very end). te_hit is called with the line's range.
- UI events: the field object gets LV_EVENT_PRESSED and PRESSING; lv_indev_get_point minus the
  field's coordinates gives local x, y; y picks the line (note) and te_hit the index.
- Cost: te_hit measures at most 120 prefixes with text_width (under 2 ms, estimate); the field
  redraw is the field box only (note field about 420 x 64, calendar field 668 x 32).

## 9. Capacity and latency

### 9.1 Capacity per device (estimates until measured; report with every build, memory rule)

| Device | Item | Estimate |
|---|---|---|
| Panel flash | voice_dsp, voice_proto, voice_tx, inbox, relay task, UI recorder, strip, dialog, mic icon, text_edit | +25 to 35 KB code (builds were 2.27-2.28 MB on 5 Nov, docs/screensaver/WORKLOG.md) |
| Panel PSRAM, always | code copy grows by the code above (the program runs from PSRAM, docs/hardware-notes.md:84); g_relay queue storage 8 KB (both panels, used upstairs) | about +35 to 45 KB |
| Panel PSRAM, during a job | clip 27 KB, freed when the text is shown | 27 KB |
| Panel PSRAM, during a ding | temporary clip, freed after it plays | about 66 KB for about 1 s |
| Panel PSRAM with the screensaver running | already the tightest (130-160 KB free estimated, docs/memory-audit-2026-11-12.md:173-174); a job cannot start while it runs (the first touch only reveals) and a ding stops it first (2.3), so only the +35-45 KB always-on part counts there | leaves about 85-125 KB |
| Panel internal RAM | VoiceRec and VoiceTx state (bitmaps 16 B), VoiceOut 4 x 160 B, inbox 4 x 134 B + 8 ids, vrelay stack 2.5 KB, mic stack +1 KB | about 5 KB |
| Panel CPU | resample + ADPCM in mic task; first sends | about 1-2 % of core 0 while recording |
| Panel draw | countdown ring 10 Hz (about 15 ms each), level bar, dots | inside the 1.3 px/us budget (memory note) |
| C6 flash | voice_session, voice_proto, base64, escaping, 4 kinds | +8 to 12 KB (968,605 of 1,310,720 used, ../gateway/docs/gateway-c6.md:28-30) |
| C6 RAM | 2 sessions, dedup ring, RX queue +16 frames (4 KB), TX buffer +2 KB | about 7 KB |
| Pi RAM | stt service with model and grammar | 133 MB peak (measured); MemoryMax 250M |
| Pi CPU | one core at nice 15 for about 4 s per message; ADPCM decode in the receive thread UNVERIFIED (13.3), estimate 5-10 % of a core while recording | idle between messages |
| Pi disk | model 40 MB, venv about 20 MB, no audio ever | about 60 MB |

### 9.2 Latency budget, end of recording to text in the field (target <= 2 s)

The end of recording is the moment the countdown ends, STOP is tapped, the hold is released or
the early stop fires. Recognition runs during the recording (streamed), so only Vosk's lag and
its finish are left after that moment.

| Step | Median | Worst | Basis |
|---|---|---|---|
| mic_task sees the stop, sends the last chunk and END | 0.01 | 0.03 | one 11.6 ms block; independent of LVGL |
| Air + C6 RX + serial cut-through (direct) | 0.01 | 0.03 | median gateway ACK 46 ms round trip, ../gateway/docs/gateway-c6.md:371-373 |
| USB + bridge select wake + datagram | 0.01 | 0.03 | the bridge reads serial reactively (tcomm_bridge.py:449-474) |
| ADPCM decode of the last chunk, feed | 0.005 | 0.02 | UNVERIFIED, 13.3 |
| Vosk: lag at the end + final result | 0.85 | 1.10 | measured 0.68-0.72 median, 0.92 worst for 2-3 s phrases. At RTF 1.2 the lag at the end is 0.2 x the clip length, so a 3.3 s clip carries about 0.66 s of lag where a 2.5 s phrase had 0.5 s (+0.15 median, +0.18 worst; check fix C11) |
| Clean-up + op to the bridge + serial line | 0.01 | 0.03 | requires the bridge wake fix below |
| C6 -> VOICE_TEXT -> panel radio | 0.02 | 0.05 | as above |
| Panel RX drain (50 ms app_tick) | 0.025 | 0.05 | tcomm.ino:213-227 |
| A long redraw in progress on the panel | 0 | 0.30 | worst redraw stalls, docs/hardware-notes.md:85 |
| Insert + field redraw | 0.03 | 0.04 | field box only |
| Total, direct route | about 1.0 s | about 1.7 s | |
| Relay through upstairs adds | +0.02 | +0.10 | two extra hops, vrelay task immediate |
| Total, relayed | about 1.0 s | about 1.8 s | |

- The bridge wake fix is part of piece E: read_lines (PI tcomm_bridge.py:449-474) also selects on
  the control socket, so an op is taken at once instead of at the next 0.25 s tick. Without it the
  worst case would be about 1.95 s, too close to the target.
- Outside the target, by design and shown on screen: the other floor's message ahead in the queue
  (up to about 1 s left of it plus about 4 s for this one), and a Lucy alert that started less
  than 3 s before.
- Acceptance (13.4): median <= 1.2 s and 95th percentile <= 2.0 s over 30 messages per floor,
  measured on the panel from the stop to the insert (millis in the `voice` log line).

## 10. Privacy

- Audio exists on the panel only in the 27 KB PSRAM clip, freed when the job ends; never saved,
  never sent anywhere but the C6. ESP-NOW frames are encrypted with the existing LMKs
  (board_radio.cpp:150-159); the USB serial to the Pi is local.
- On the Pi audio is in memory only; the service has no network (RestrictAddressFamilies=AF_UNIX)
  and cannot write outside ~/tcomm-gateway. No chunk, PCM or transcript in any log (the bridge
  already keeps note text out, tcomm_bridge.py:730-732); logs carry x, floor, counts, timings and
  status codes only.
- The transcript leaves the house only as the phone push the receiving floor already gets for
  mail and doorbells (decision 6), through the same ntfy topics. SAY text is kept on the receiving
  panel until dismissed.
- Public repo: no MACs, keys, ntfy topics, addresses, vocabulary or names; test phrases are
  synthetic. Scan before any push (memory rule).

## 11. Build order and file ownership

Each piece has one owner while it is open; a file is never edited by two pieces at once. P0 lands
first and freezes every shared interface, so A-G then touch only their own files. H starts after C
has merged (it reuses two of C's files).

| Piece | Owns (new or edited) | Depends | Tests |
|---|---|---|---|
| P0 Spec freeze | src/core/voice_proto.{h,cpp} (types, layouts, constants, encode/decode, voice_relay_to); interface lines in src/board/board.h (radio_send_voice, voice capture hooks, Ding), src/app/controller.h (Platform: radio_send_voice, voice_rec_begin/stop/release, save/load_voice_inbox), src/ui/ui.h (Sheet::Voice, Actions voice_*), src/core/protocol.h (kStateVoice only), src/core/types.h (Sound::Ding); tools/voice_vectors.py and tests/vectors/voice_adpcm_*; tests/test_voice_main.cpp with stub hooks; tools/build_sim.sh (a test_voice line, and voice_proto.cpp on the gateway test line); ../gateway/gateway-c6/src/shared/core_voice_proto.cpp | none | tests/test_voice_proto.cpp: every type round trip, each bad length and bad field rejected, old decode() still rejects 20-30, relay rule table (every type x from x floor) |
| R Reach test | tcomm.ino serial `reach N [ms]`; src/board/board_radio.cpp (snapshot of reached/missed) | none (can go before P0) | none on the host; the on-air test 13.0 |
| A Capture + DSP + ding | src/core/voice_dsp.{h,cpp}; src/board/board_audio.cpp (tap, handshake, int64, stack, chime rule and deferred list, Ding on demand); src/core/synth.cpp (Ding); tcomm.ino `voice dump N`, `sound ding` | P0, R merged (tcomm.ino) | tests/test_voice_dsp.cpp: resampler response (1 kHz passes, 10 kHz < -30 dB), ADPCM matches the golden vectors bit for bit, each chunk decodes alone, mu-law round trip, onset and early stop on synthetic power traces, handshake state machine with a fake task loop |
| B Link + controller + relay | src/core/voice_tx.{h,cpp}, src/core/voice_inbox.{h,cpp} (inbox, id dedup, ding/wake decision), src/board/board_radio.cpp (radio_send_voice, g_relay, vrelay task), src/board/device_platform.{h,cpp}, src/app/controller.cpp (routing, SAY, PUSH, DISMISS, kStateVoice, voice_wake, route auto, pings), src/core/brightness.h | P0, R merged | tests/test_voice_link.cpp: voice_tx against a VoiceSession compiled from the C6 source over a lossy fake link (10 % drop, duplicates, reorder) completes; window never passes 32; BEGIN direct->relay fallback; END and text timeouts; queued status extends once; SAY and PUSH retries and expiry rules; inbox order, 5th drops oldest, dedup across a simulated restart; dismiss present/absent; ding table by status, quiet hours, screen-off, recording |
| C Panel UI | src/ui/ui_voice.cpp (recorder, strip, result, EDIT, arrival dialog); src/ui/ui.cpp (badge_hit, banner order); src/ui/ui_common.cpp (close_sheet hook); src/ui/ui_internal.h (Keypad.on_mic); src/ui/ui_calendar.cpp (kMic key, on_mic hookup); src/ui/ui_note.cpp (on_mic hookup); src/ui/art.{h,cpp} (Mic icon); src/app/panel_app.h | P0 | sim build with a fake voice backend (canned text after 0.9 s, every error path); screenshots of each state in day and night themes; touch map check: badge, note text, status word, mail and no-mail, arrows each open the right thing; 700 ms guard; dialog waits for a sheet |
| D C6 0.5 | ../gateway/gateway-c6/src/gw/gateway.{h,cpp}, src/gw/voice_session.{h,cpp}, gateway-c6.ino (send_frame(to), queue 32, TX 4096, write gate, drops), ../gateway/docs/gateway-c6.md | P0 | ../gateway/tests/test_gateway.cpp: sessions from both floors at once, direct and via upstairs, cut-through lines < 512 B, write gate leaves bits clear when the fake port is short of room, a notice event and a reply during a backed-up stream arrive whole (H2 test), 2 s timeout abort, voice_text split/join, voice_ack dismiss, push dedup, ping, kinds 6-9 latest-wins, old types unchanged |
| E Pi bridge | PI tcomm_bridge.py (voice events -> stt.sock raw, voice_msg -> phone.sock, ops voice_text and voice_ack, hello voice:1, voice_reset on C6 boot, control-socket wake in read_lines), PI test_tcomm_bridge/* (fake_c6.py voice lines) | P0 | run_tests.py: events forwarded raw and never logged with data or text; ops validated (each bad field), split at 60, reply relayed; reset on boot; an op is answered without waiting a tick; the existing 60 checks pass |
| F Pi stt service | PI stt_service.py, PI systemd/stt.service, PI test_stt/ (run_tests.py, fake_vosk.py, vectors copied from tests/vectors) | P0 | run_tests.py with a fake engine: ADPCM decode matches the vectors; out-of-order and duplicate chunks; up to 3 gaps filled, 4 -> status 5; sp 0 -> status 1; queue of 2, third busy, interim 6; abort and reset; 2 s drop; 10 s hard limit; Lucy ALERT hold (fake file and clock); clean-up table (two/to, I, names from a test list, capitals, 120 cut); the recognizer thread asks for nice 15 (patched os.setpriority); captured log has no transcript; a timing test of the decoder on the laptop |
| G phone_bell | PI phone_bell.py, PI test_phone_bell/run_tests.py, PI PHONE_BELL_INSTALL.md (a short section) | P0 | voice_msg push copy per floor; quiet hours; stale; duplicate; no base_url; token first-wins across two phones; expired; result removed vs gone; bridge failure resets to open; GET never acts; no text or token in the log |
| H Cursor | src/core/text_edit.{h,cpp}, tests/test_text_edit.cpp, src/ui/ui_note.cpp, src/ui/ui_calendar.cpp (field parts), src/ui/ui_voice.cpp (EDIT field) | C merged | te_insert/backspace at start, middle, end, full; te_hit with a fixed-width fake font (midpoint rule, past the end); note wrap with the cursor on each line and at a break; te_window keeps the cursor visible both ways; sim: tap and slide in each field |
| I Integration | none new | A-G (H later) | sim end to end with the fake backend; then on air (13.4) |

Agent notes: every workflow agent gets an explicit `model` (Sonnet for P0 vectors, A, D, E, F, G,
H; Opus for B and C, which need controller and UI judgement). Builds only through tools/fw.sh and
tools/build_sim.sh in Git Bash, run by the main session, never by a piece agent. Nothing flashed or
installed without Mike.

## 12. Test details fixed in advance

1. Golden vectors (P0): tools/voice_vectors.py makes a 1 s 16 kHz synthetic signal (a sweep plus
   noise bursts), its ADPCM chunks (low nibble first) and the expected decode. A (C++) and F
   (Python) must match bit for bit. Synthetic only.
2. Lossy-link simulator in test_voice_link.cpp drives voice_tx against the C6's VoiceSession (the
   C6 already compiles panel core files, ../gateway/gateway-c6/src/shared/core_protocol.cpp:3).
3. `voice dump N` prints a recorded clip as hex for the PC; the WAVs are the audio test's clips
   (13.2) and are kept on the laptop only, outside the repo.

## 13. Test and rollout order

All Pi steps: daytime, Lucy Gate in STANDBY, as few ssh logins as possible (a login is itself a
load step, PI CLAUDE_CODE_HANDOFF.md:92), each needs Mike's yes (HANDOFF rule E3).

13.0 Reach test, first (piece R on both panels, OTA). On downstairs: `reach 200 30` sends 200
249-byte frames of type 31 (no device decodes it, so nothing acts on it) to the C6 at 30 ms
spacing and prints the reached/missed delta (MAC-level, board_radio.cpp:107-111). Three runs at
different times of day, the same on upstairs for comparison. 97 % or more reached on every run ->
default route direct; otherwise relay. Auto keeps checking at run time (4.7).
(P0, check finding 1) MAC-level "reached" counts any board with the MAC, before decryption, so it
is not proof that the C6 takes downstairs frames. Two more conditions, checked once the C6 0.5 is
on (start of 13.4, before the default is set): the C6 hello says `"down_peer":true`, and
`reach ping N [ms]` on downstairs (piece R; VOICE_PING frames built with voice_encode, nonce =
the try number) counts the VOICE_GOT kind 30 replies that come back end to end. Direct is the
default only if both the reached rate and the end-to-end ping rate are 97 % or more.

13.1 Host tests: tools/build_sim.sh (test_core, test_voice, test_gateway) and the Pi tests on the
laptop (test_tcomm_bridge, test_stt, test_phone_bell). All pass before anything goes on air.

13.2 Panel audio (after A, OTA): 12 phrases with `voice dump` at 24 and 30 dB, at arm's length
and from 2 m, plus 3 with a chime playing. Pass: speech peaks -20 to -3 dBFS, no clipping, the
chime rule works, mic stack high-water mark logged.

13.3 Pi install (pieces E, F, G together, bridge before the C6):
- Baseline in one login: `python3 -V; free -m; df -h /; vcgencmd get_throttled; sysctl
  net.unix.max_dgram_qlen; dmesg | grep -ci undervoltage; systemctl is-active weather-station
  lucy-gate tcomm-bridge phone-bell house-alerts`.
- Backup: Pi copies tcomm_bridge.py.bak-<date> and phone_bell.py.bak-<date>; a Windows backup
  folder backups\<date>_pre-voice\. One file per scp, md5 compared, py_compile on the Pi.
- ~/tcomm-stt set up as 6.5 (new venv, model moved, vocabulary and names copied, execstack
  cleared). In the same login, three timings printed (no text): recognizer construction with the
  grammar, Reset() then a second clip, and the ADPCM decoder over 3.3 s of synthetic chunks (pass:
  under 10 % of real time, else codec 1).
- Install stt.service, daemon-reload, enable --now stt; restart tcomm-bridge and phone-bell. A C6
  0.4 sends no voice lines, so nothing changes yet. Check is-active for all six services and
  get_throttled.
- Undo: `sudo systemctl disable --now stt; sudo rm /etc/systemd/system/stt.service; sudo
  systemctl daemon-reload`, restore both .bak files, restart tcomm-bridge and phone-bell. ~/tcomm-stt
  can stay (inert) or be removed. The panels and C6 keep working; voice just says "The Pi didn't
  answer".
- ~/stt-bench is removed only after 13.4 passes, with Mike's yes.

13.4 C6 and panels together, same session (the bridge is already there, review L4):
- C6 0.5 first, Lucy Gate STANDBY, the way 0.4 went on (Mike's choice of USB on the laptop or
  `bash tools/fw.sh gateway-remote`). Check hello: fw 0.5, voice 1.
- Then Settings > Update over Wi-Fi > BOTH PANELS and `bash tools/fw.sh ota-both` (memory: next
  flash). `info` on both panels before and after (capacity report).
- On air, in order: dictation upstairs; dictation downstairs (direct and forced relay); a message
  each way, ding by ear (G5), dialog placement on both panels; phone push to each floor; Got it
  from a phone clears the panel; Got it after a panel dismiss says so; two-floor collision (queued
  message); 30 timed messages per floor (pass 9.2); 20 messages back to back on the Pi, then
  get_throttled and the dmesg undervoltage count compared with the baseline (pass: no new
  undervoltage) and `journalctl -u lucy-gate -u tcomm-bridge --since -15min` clean; one message
  sent while Lucy Gate is ARMED (Mike triggers nothing else).
- Undo: OTA back to the previous panel build and C6 0.4; the Pi side can stay (it is inert without
  voice frames).

13.5 Piece H (cursor) as its own OTA after 13.4 passes: tap and slide in the note field, both
calendar fields and EDIT; voice inserted mid-text.

## 14. Known gaps and risks

- Downstairs to C6 radio: unproven until 13.0; both routes are built, so it changes the default,
  not the design.
- Pi supply margin: single thread measured clean, but not alongside a Lucy jingle (13.4 checks;
  the ALERT hold covers the jingle itself).
- Vosk numbers come from TTS clips at 16 kHz PCM; real panel audio through ADPCM is checked in
  13.4 (word errors judged by Mike on the 30-message run). If ADPCM hurts accuracy, codec 1.
- Recognizer construction and Reset() behaviour with a grammar: UNVERIFIED (13.3).
- net.unix.max_dgram_qlen on this kernel: UNVERIFIED (13.3); the normal-priority receive thread
  makes a small queue safe at 34 datagrams/s.
- HWCDC throughput with 34 lines/s plus control lines: covered by the write gate and resends,
  watched in 13.4.
- Free PSRAM with the screensaver running stays the tightest figure; take `info` before and after.

## 15. Review findings and how v2 deals with them

| Finding | v2 |
|---|---|
| H1 Pi supply under 2-thread STT, building on the Pi | One engine thread at nice 15 (measured, no undervoltage); CPUQuota 130%; nothing compiled on the Pi, no swap file (Vosk wheel); 20-message back-to-back gate with get_throttled and dmesg (13.4); hold while a Lucy ALERT is under 3 s old (6.3) |
| H2 Voice lines cutting control lines on the C6 serial | Write gate keeps 1100 B free (two full lines); TX buffer 4096; host test that a notice and a reply arrive whole during a backed-up stream (5.1, piece D) |
| H3 Dialog over live controls | Dialog at tile y 90-193 of the status zone: badges, arrows and the weather zone stay live; only the informational acted strip is under it until one tap (2.3) |
| H4 Dialog dismissed unread | Never shown while a sheet is open (close_sheet shows it), 700 ms tap guard, banner raised above it (2.3) |
| M1 Window wider than the bitmap | Window 32 = bitmap 32 (4.3) |
| M2 Session timeout before speech | No leading trim: chunks flow from the press; BEGIN at the press; C6 idle 2 s (3.4, 5.1) |
| M3 Use-after-free mic task / UI | g_rec / g_rec_busy handshake before free_big (3.2) |
| M4 Loss between C6 and the service | Receive thread at normal priority, SO_RCVBUF 256 KB; voice_end only after every chunk was written; up to 3 gaps filled, more -> "Lost part of the recording" (6.2, 6.3). A Pi-to-panel resend path is not built: with these changes a loss needs a bridge restart mid-clip |
| M5 radio_send has no result, trace flooding | radio_send_voice returns bool, no trace, no elog, own counters; owned by piece B (4.6) |
| M6 Ding PSRAM not counted | Rendered on demand, about 66 KB for about 1 s, counted in 9.1 (3.5) |
| M7 Ding like the dog alert | G5 single strike, an octave under G6; D6 as the alternative by ear (2.3) |
| M8 Second floor's clip thrown away | Two C6 sessions, Pi queue of 2 with an interim "queued" status and one timeout extension (6.3, 4.5) |
| M9 Downstairs link unproven | Reach test is step 0; both routes built; auto route with pings (4.7, 13.0) |
| M10 Whisper versus the timeouts | No longer applies: Vosk with a grammar was chosen and measured (6.1) |
| M11 audioop nibble order | No longer applies: audioop is gone in Python 3.13; our decoder follows the golden vectors (3.1) |
| L1 Text cap | 3 s is about 8-12 words; cut at a word boundary, never silent ("Only part of it fit") |
| L2 First syllable | Capture starts at PRESSED (2.1, 2.2) |
| L3 Press lock | LV_OBJ_FLAG_PRESS_LOCK set explicitly on the badge hit and the mic key, checked in the sim |
| L4 Rollout order | Bridge before the C6 (13.3 then 13.4) |
| L5 Control socket blocking | The service sends each op in one sendall |
| L6 Chime handling during recording | Deferred list and 50 ms queue wait in audio_task (3.4) |
| L7 C6 queue overflow | Queue 32 and a drop counter (5.1) |

## 16. Still needing Mike

1. The reach test (13.0): run it on both panels (needs piece R flashed by OTA).
2. The ding by ear: G5 (default, built) or D6.
3. Phone push rules: built as only when the receiving floor is Open or Busy and outside quiet hours
   (owner default, P0 notes); one constant to change.
4. How long a phone's Got it keeps working: built as 12 h (owner default).
5. EDIT on the result screen: built on (owner default).
6. The early stop on 0.8 s of quiet: built on (owner default).
7. Yes for each Pi step (13.3), the C6 flash and the panel OTA (13.4), and removing ~/stt-bench.
8. The names list for capitalisation (~/tcomm-stt/names.txt on the Pi only): which names go in.

## Check fixes (review of v2, 2026-11-20)

Small problems found by the check and fixed in place above:

- C1 (2.2) Badge hit widened to x 287-386 so a tap on the badge's left 11 px (x 289-299) no longer
  opens the status sheet; the overlap with the acted strip is never live.
- C2 (2.2) A tap's release seen only after the overlay's first draw (0.3-0.6 s LVGL stall) could
  read as a hold and end the recording at once; such a release now always counts as a tap.
- C3 (2.3) Arrival order fixed: screensaver_stop before the Ding render.
- C4 (3.4) audio_task wrote rec->stop directly, outside the g_rec_busy handshake (a use after
  free if LVGL frees the clip at that moment); now an atomic g_rec_bell that mic_task applies.
- C5 (3.5) Sound::Ding = 5 collided with Sound::Count = 5 (src/core/types.h:42); Count moves to 6,
  and Ding is handled before the `which > 4` guard.
- C6 (4.4) A resent END after the C6 freed the session would get state 3 (unknown xfer); the C6
  now remembers the last completed xfer per floor.
- C7 (4.6) on_recv traced every inbound frame, so relayed uploads would still wipe the trace ring;
  voice types are counted, not traced.
- C8 (5.2) C6 Pending had no destination (send_try always sends upstairs, the give-up line always
  names upstairs); Pending gains `to` and the route.
- C9 (5.2) Latest-wins dismiss slots would supersede a first Got it when two messages are
  dismissed close together; the dismiss slots refuse while busy and phone_bell retries once.
- C10 (6.2) The stt sender waited for each reply before the next op, so one floor's slow
  VOICE_GOT could hold the other floor's text; ops are now pipelined over separate connections.
- C11 (9.2) The Vosk row under-scaled the RTF 1.2 lag for a 3.3 s clip; median 0.85 s, worst
  1.10 s; totals about 1.0 s median, 1.7 s worst direct, 1.8 s worst relayed (still under 2 s).
- C12 (8.1) The calendar CLICKED handler and the title capital rule would undo a tap-placed
  cursor; both now follow the cursor.
- C13 (6.5) The Pi login name and home path in the unit sample are placeholders, since this doc is
  in the public repo.

## P0 notes (spec freeze, 2026-11-20)

P0 wrote the shared interfaces; A-G build against them. Where this section and the text above
disagree, this section wins. Changes made in place above are marked "(P0".

### What is frozen, and where

| Interface | File | Notes |
|---|---|---|
| Frame types 20-30, layouts, encode/decode, voice_peek, voice_relay_to, voice_relay_frame, voice_type_name, voice_text_key, voice_in_window | src/core/voice_proto.{h,cpp} | Compiled into the C6 by ../gateway/gateway-c6/src/shared/core_voice_proto.cpp |
| Owner defaults as constants | voice_proto.h | kVoiceRecMs 3000 (from the press), kVoiceHoldMs 600, kVoiceEarlyStop on, kVoiceEarlyStopMs 800, kVoiceEditButton on, kVoicePushOpenBusyOnly, kVoicePhoneTokenHours 12, kVoiceDismissClearsPhone false |
| VoiceRec (the capture handoff A <-> B), VoiceChunk, VoiceStop, voice_rec_bytes/init/destroy | voice_proto.h | One writer per field, marked in the struct |
| VoiceInboxData, VoiceNote, voice_inbox_valid (the saved inbox) | voice_proto.h | |
| VoiceView, VoiceUi, VoiceErr (controller -> UI) | voice_proto.h, View::voice in controller.h | Plain data: View is cleared with memset |
| The capital rule (check finding 2): voice_keep_first_capital, voice_apply_capital | voice_proto.{h,cpp} | Host-tested |
| IMA ADPCM step/index tables, voice_adpcm_decode, voice_mulaw_decode | voice_proto.{h,cpp} | Encoders stay in A's voice_dsp |
| C6 serial line formats, floor/purpose/abort words, hello fields, bridge op names | voice_proto.h, namespace tc::vline | For D (printf formats) and E, F, G (the same keys) |
| Platform: radio_send_voice, voice_rec_begin/stop/release, save/load_voice_inbox | src/app/controller.h | Default bodies: no voice |
| Controller: voice_press, voice_stop, voice_cancel, voice_done, voice_send, voice_dismiss, restore_voice_inbox | controller.h (declared only) | B defines them in controller.cpp. Nothing calls them yet, so the build links |
| board: radio_send_voice, voice_rec_begin/stop/release, voice_recording, load/save_voice_inbox | src/board/board.h | Declared only: A defines the capture ones, B the radio and inbox ones |
| ui::Sheet::Voice (appended last), Actions voice_press/stop/cancel/done/send/dismiss | src/ui/ui.h | Default bodies |
| kStateVoice = 0x10 | src/core/protocol.h | |
| Sound::Ding = 5, Count = 6, kResidentSounds = 5 | src/core/types.h | audio_task's `which > 4` guard still drops Ding until A lands (silent, harmless) |
| Golden vectors | tools/voice_vectors.py, tests/vectors/voice_adpcm_{sweep,edge}_*.txt, voice_mulaw_sweep_*.txt | Integer-only generator; `--check` |
| Host test runner | tests/voice_test.h (VTEST, CHECK, vector readers, vt::StubPlatform), tests/test_voice_main.cpp | Each piece adds tests/test_voice_<x>.cpp; build_sim.sh compiles tests/test_voice_*.cpp |

### Details the freeze settles

1. VOICE_TEXT gains a flags byte after status (check finding 2): 10 + len bytes, at most 130.
   Bit 0 (kVoiceTextSentenceCap): the first capital is only a sentence start. Other bits refused.
   The bridge op carries `"flags"`, the C6 request line `"fl"`. The panel lowercases the first
   letter when inserting unless the cursor is at 0, only spaces are before it, or the last
   non-space before it is '.', '?' or '!'. Status Ok always has text (len >= 1); every other
   status has none (len 0), so the GOT key rule (xfer + status x 256) stays unambiguous.
2. The cap counts from the press: 3.0 s total, countdown from the press on the badge card and the
   keyboard strip (supersedes the 3.3 s clip of 2.2, 3.1 and 3.3). BEGIN's max_chunks for ADPCM is
   kVoiceClipChunks = 104 (102 needed + 2 for the stop block); the VoiceRec block is about 25.7 KB
   of PSRAM (test: <= 27 KB).
3. The window (check finding 6): nobody makes a first send of idx >= base + 32. Only mic_task
   makes first sends, strictly in order (first_sent counter); it holds a chunk outside the window
   and retries every block. END only after every chunk's first send. LVGL resends only below
   first_sent and writes rec->base from each VOICE_ACK.
4. Reach test (check finding 1): the C6 hello reports `"down_peer":true|false`; `reach ping N [ms]`
   (piece R) counts VOICE_GOT kind 30 replies end to end. Both, plus the radio-level 97 %, decide
   the direct route (13.0). The reach frames' type is kVoiceReachType = 31.
5. Pi threads (check finding 7): the receive thread only calls recv and queues; a separate
   normal-priority decode thread parses and decodes; the recognizer thread alone runs at nice 15.
6. Phone push (owner default): the RECEIVING panel sends VOICE_PUSH only while its own status is
   Open or Busy, its clock is valid and it is outside quiet hours (the doorbell's rule, as the
   upstairs panel decides RING notices today); phone_bell still applies its quiet hours. A panel
   dismiss sends nothing to the phones yet: B leaves one marked hook ("TODO(voice): clear the phone
   push") in its dismiss path.
7. Got it token: 12 h (phone_bell voice_token_h default 12).
8. Validation added beyond 4.3: ACK base <= 128 (any u8 since 2026-11-21); END n_samples <= n_chunks x 474; PUSH from != the
   frame's floor; SAY and PUSH must carry text; chunk_samples must be the codec's (474 ADPCM, 237
   mu-law). A DATA frame cannot know its codec, so a mu-law upload's pred 0 / step 0 is checked by
   the C6 and the Pi against the BEGIN.
9. The ADPCM codec exactly: the standard IMA encoder (tools/voice_vectors.py adpcm_encode_sample);
   a recording starts at pred 0, step index 0; each chunk header is the state before its first
   sample and the state runs on unbroken across chunks; LOW nibble first; an odd last sample leaves
   the high nibble 0. mu-law: G.711, bias 0x84, clip 32635. The vector files bind A (C++ encoder)
   and F (Python decoder) bit for bit; voice_adpcm_decode is checked against them.
10. msg_name() in protocol.cpp is not changed (protocol.cpp is not P0's); voice_type_name() gives
    the names, and callers pick by is_voice_type(byte [1]).
11. The C6 hello field name is `down_peer` (JSON true/false). Floor words: `"f"` in the C6 -> Pi
    voice events is "up"/"down"; every other floor field is "upstairs"/"downstairs" (as 5.2 shows).

### Hand-offs P0 did not own

- B: board_storage.cpp is in no piece's list; B implements board::load/save_voice_inbox there (an
  NVS blob of VoiceInboxData, checked with voice_inbox_valid) and adds the Controller's private
  voice members at the marked spot in controller.h.
- A: board::voice_rec_begin/stop/release/voice_recording in board_audio.cpp; Ding in synth.cpp and
  before the `which > 4` guard.
- R: `reach ping N [ms]` alongside `reach N [ms]` (check finding 1).
- D: hello fields voice, drops, down_peer; the "fl" field joined into VOICE_TEXT flags.
- E, F, G: the op and line names in tc::vline; F follows finding 7 and sets flags bit 0.
