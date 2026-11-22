# Voice notes work log

## 2026-11-20 P0 spec freeze

Built (nothing flashed, installed or committed):
- src/core/voice_proto.{h,cpp}: frame types 20-30 with encode/decode and strict validation,
  voice_peek, the relay rule (voice_relay_to, voice_relay_frame), GOT keys, the window rule, the
  capital rule (check finding 2), IMA ADPCM tables and decoder, mu-law decoder, VoiceRec handoff
  struct, saved inbox struct, VoiceView for the UI, owner-default constants, and the C6 serial line
  formats and Pi op names (tc::vline, with hello down_peer for check finding 1).
- Interface lines: board.h (radio_send_voice, voice_rec_begin/stop/release, voice_recording,
  load/save_voice_inbox, Ding note), controller.h (Platform voice virtuals with defaults, View::voice,
  Controller voice_* declarations, a marked spot for B's private state), ui.h (Sheet::Voice last,
  Actions voice_* with default bodies), protocol.h (kStateVoice 0x10), types.h (Sound::Ding 5,
  Count 6, kResidentSounds 5).
- ../gateway/gateway-c6/src/shared/core_voice_proto.cpp (the C6 compiles the panel's voice_proto.cpp).
- tools/voice_vectors.py and tests/vectors (ADPCM sweep 1 s and edge 475 samples, mu-law sweep).
- tests/voice_test.h, tests/test_voice_main.cpp (runner, vector readers, vt::StubPlatform),
  tests/test_voice_proto.cpp, tests/test_voice_vectors.py.
- tools/build_sim.sh: voice_proto.cpp on the gateway test line; a test_voice line (all
  tests/test_voice_*.cpp, plus the C6 voice_session.cpp once it exists) and the Python vector check.
- DESIGN.md: in-place "(P0" edits and a "P0 notes" section.

Tests (built by hand with g++ 16.2 into the scratchpad, not with build_sim.sh):
- test_voice: 18 tests, 62,398 checks, 0 failed (-Wall -Wextra -Werror). Fails as it should with a
  missing or corrupted vector file.
- Python: 6 tests OK (python -I -B -m unittest discover -s tests -p "test_voice_*.py");
  voice_vectors.py --check: 8 files, 0 differ.
- Existing: test_core 9,023 passed, test_gateway 156 passed, the same as before the change.
- -fsyntax-only -Werror: every src/core/*.cpp, controller.cpp, demo.cpp, the C6 shared file, and
  board.h through a stub driver header; ui/*.cpp and sim_main.cpp with LVGL headers: no warnings.

Cost estimate: voice_proto.o is about 4.3 KB of code on x86 -Os (expect 3-5 KB on the S3 and the
C6). RAM: View grows by about 290 B (VoiceView). No PSRAM until B and A use VoiceRec (25.6 KB per
job, measured on the host).

## 2026-11-20 Piece F: Pi stt service

Built in the Pi project (a separate repo, nothing committed): stt_service.py, systemd/stt.service,
test_stt/ (run_tests.py, fake_vosk.py, vectors copied from tests/vectors), STT_INSTALL.md.
No file of this repo was touched except this entry.

- Threads as the design and check finding 7 say: a receive thread that only calls recv and queues; a
  separate decode thread (parse, base64, table-driven ADPCM or mu-law, jobs, gap fill); one recognizer
  thread, the only one at nice 15 (os.setpriority on its own native thread id); a sender thread that
  writes each op in one sendall on its own connection and reads replies with select (check fix C10);
  a watchdog for the 2 s idle drop and the 10 s hard limit (status 4, flush, exit 1).
- Grammar read at start from the vocabulary file named in stt.conf (default ~/tcomm-stt/house_vocab.txt),
  built as json.dumps([" ".join(sorted unique words), "[unk]"]). No words, names or topics in the source.
- Clean-up: [unk] dropped, the "two" rule, I and I'm/I'll/I've/I'd, names from a Pi-only names file,
  first capital, ASCII only, 120-char cut at a word boundary. The flags byte (check finding 2) has
  bit 0 set only when the first capital comes from the sentence-start step (not a name, not "I").
- Jobs: one active, one waiting (interim status 6 at once), a third gets status 3; chunks are fed in
  order, the newest held one beat so the odd last sample can be trimmed to the END's sample count;
  up to 3 missing chunks become silence, 4 -> status 5; sp 0 -> status 1 without Vosk; abort and the
  C6's voice_reset drop jobs and rebuild the recognizer; the Lucy ALERT hold (file younger than 3 s)
  is read once when a job becomes active.
- Segments Vosk closes on its own (AcceptWaveform True) are kept and joined with the final text.
- Install helper: `stt_service.py clear-execstack` clears PT_GNU_STACK's executable flag in the venv's
  libvosk.so (ELF32 or ELF64, one byte, original kept as .orig) for glibc 2.41. Also `check` (counts
  only) and `decoder-timing` (13.3).

Tests: 64 Python tests (python -I -m unittest discover -s test_stt -p run_tests.py), 63 run and 1
skipped (the real Unix socket test: this laptop has no AF_UNIX). All pass with warnings as errors.
Decoder timing on the laptop: 3.3 s of audio in 0.007 s (0.2 % of real time); on the Pi 2 this is
UNVERIFIED (13.3, pass under 10 %).

Departures and open points for the owner: (1) Vosk is fed in 100 ms batches (1600 samples, a
setting, as measured in the bench), not per 29.6 ms chunk. (2) The unit has no Nice= line (the task
list said Nice 15, the design says only the recognizer thread; one line to add if wanted); limits are
CPUQuota 100 %, MemoryMax 300M as requested (the design text had 130 % and 250M). (3) Recognizer
construction time and Reset() with a grammar are still UNVERIFIED; reset_mode in stt.conf switches.
Cost: no flash or panel RAM; on the Pi about 133 MB peak RSS measured in the bench (limit 300M), CPU
about one core for about 4 s per message, nothing on disk beyond the venv and model.

## 2026-11-20 Piece D: C6 gateway 0.5

Built (all LF, ASCII; nothing flashed, installed or committed; no build_sim.sh, fw.sh or arduino-cli run):
- ../gateway/gateway-c6/src/gw/gateway.{h,cpp}: kFirmware "gateway-c6 0.5"; GatewayIO::send_frame(to,
  ...), tx_room(), rx_drops(), down_peer() (the last three have defaults, so old fakes still work);
  kinds 6-9 (voice_text and voice_ack per floor, matched by VOICE_GOT kind and key); Pending gains
  to, floor, got_kind, key; the failed line names the slot's floor; voice frames branch off in
  on_frame by byte [1]; hello now carries voice, drops, down_peer and the bridge's "voice":1 is
  remembered; GwStatus gains voice_lines, voice_held, voice_bad.
- ../gateway/gateway-c6/src/gw/voice_session.{h,cpp}: VoiceSession (per floor: xfer, codec,
  max_chunks, 128-bit received map, via, last_rx, END data, ACK schedule) and voice_base64. Its
  methods (open, mark, base, bits, gap, all_received) can be driven without the Gateway; piece B's
  lossy-link test may use them, but the frame logic (ACK cadence, timeout, state 2 and 3) is in
  Gateway::voice_*, not in the session class.
- ../gateway/gateway-c6/gateway-c6.ino: send_frame(to), RX queue 32 and a drop counter, TX buffer
  4096, tx_room from availableForWrite, down_peer set when the downstairs peer was added, "DROPS n"
  on the screen's last row.
- ../gateway/docs/gateway-c6.md: a 0.5 section. ../gateway/tests/test_gateway.cpp: FakeIO gets a modelled serial port
  (cap, used, overflow), the hello strings follow 0.5, 12 voice test functions.

Choices the design left open:
- Write gate: chunk lines need room for line + 1100 bytes (DESIGN 5.1); the other voice lines
  (begin, end, abort, voice_msg) need 600 bytes left. Both reserves are constants in gateway.h
  (kVoiceDataReserve, kVoiceCtlReserve). The brief said "600 bytes always free for control lines":
  that holds with a wide margin (a stream stops with at least 1100 free).
- "No voice output while a request slot waits for its ACK": applies to chunk lines, and to the six
  ACK-matched kinds only (outside, lucy, update, answers, mail_ack). A waiting voice_text or
  voice_ack slot does not hold the other floor's upload. A request that never gets an ACK holds
  chunks for up to 1.5 s (the panel would then see no base movement for 1.5 s and abort); normal ACKs
  come in tens of milliseconds, so it costs about one resend of one chunk.
- A replaced session whose voice_begin was printed gets a voice_abort line with reason "lost". A
  finished session that receives the panel's ABORT (CANCEL while the Pi recognises) forwards one
  voice_abort to the Pi. Idle-timeout and owed lines are retried each tick if the port was full.
- voice_ack busy replies are not counted as rejected on the screen.
- vs_[1].via starts as upstairs, so a voice_text for downstairs after a C6 reboot goes by the relay
  until downstairs has been heard directly.
- Not built: a route fallback on the later tries of a VOICE_TEXT (a text for downstairs sent direct
  stays direct for all five tries); the DESIGN's route is fixed at queue time.

Tests (g++ 16.2, -Wall -Wextra -Werror, built by hand into the scratchpad):
- test_gateway: 928 passed, 0 failed (156 before). The 156 old checks are unchanged apart from the
  hello string and the FakeIO signature. Two mutations were tried and caught: gate reserve 0 (7
  failures) and the request-waiting hold removed (1 failure).
- test_voice_proto and the vectors with voice_session.cpp linked: 18 tests, 62,398 checks, 0 failed.
  The full tests/test_voice_*.cpp set did not build at the time of the run because pieces A and B
  both define tc::VoiceLink (voice_dsp.h vs voice_tx.h): not D's files.

Cost (host x86 -Os; not measured on the C6): gateway.cpp 13.1 KB, voice_session.cpp 1.2 KB,
voice_proto.cpp 4.3 KB, so about 15 to 19 KB of C6 flash. Static RAM about +1.5 KB (four more
Pending slots, voice state, a 513-byte line buffer); heap +4 KB (RX queue 32 x 252 B) and +2 KB (USB
TX buffer 4096).

For the owner:
- The sketch changes could not be compiled here (no arduino-cli): the .ino edits are small and
  were read twice, but the first fw.sh gateway build is the real check. Serial.availableForWrite() is
  assumed to report free ring-buffer bytes (HWCDC, core 3.2.0); if it reports less than the true
  free space the gate only gets stricter.
- Piece E (bridge) must send {"cmd":"hello","voice":1}; without it every BEGIN is aborted with
  reason 7. The hello is also needed after every C6 reset.
- The reserves (1100 for chunks, 600 for the other voice lines) are one-line constants if you want them changed.

## 2026-11-20 Piece E: Pi bridge (tcomm_bridge.py)

Built (Pi project, separate repo; nothing installed, copied to the Pi, flashed or committed):
- Hello: {"cmd":"hello","voice":1}. A C6 boot line (not a hello reply) now also gets a fresh hello at
  once, because a rebooted C6 forgets pi_voice and would otherwise refuse every clip with reason 7.
  The hello reply's voice, drops and down_peer fields are kept and logged (check finding 1); a hello
  reply without "voice" marks the C6 as older than 0.5 and voice ops are then refused ("gateway does
  not support voice") instead of sent; unknown until a hello is answered counts as allowed.
- voice_begin, voice, voice_end, voice_abort go to stt.sock raw (the original line, byte for byte,
  one datagram, sanity cap 1024 B). Never logged with data: one journal line per clip (start, then
  end or abort with counts). A damaged voice line is counted and said once a minute without any of
  its text. A missing or full stt.sock is said at most once a minute (the existing rate limiter).
- C6 boot sends {"event":"voice_reset"} to stt.sock and forgets the clip counts.
- voice_msg goes to phone.sock only, as {"voice_msg":{event},"received":t}; validated (id 1 to
  4294967295, floors, text string, under 1024 B); the log has the floor and id only.
- Control ops: voice_text (x 1-255, floor, status 0-6, flags 0-1, text printable ASCII <= 120, text
  needed exactly when status is 0; sent as t0 + t1 of at most 60, plus "fl") and voice_ack (floor,
  msg 1-4294967295; "voice_dismiss" accepted as a synonym, still sent as type voice_ack). Replies
  (acked, result, failed, rejected busy, superseded) are relayed by C6 request id like mail_ack. They
  share the 8-pending limit and the 5 s reply wait. Logs carry status, length, floor and message id
  only.
- The control socket now wakes the serial wait (select on the listening socket): an op is served at
  once, not up to a 0.25 s tick later; also while the C6 port is closed. After an accept error the
  wake is held off for 1 s so a stuck socket cannot spin the loop. Serial reads take up to 16 KB.
- Tests: test_tcomm_bridge/test_voice.py (new, unittest) and fake_c6.py (voice_capable switch, off by
  default so old tests still see a 0.4); run_tests.py runs it as section 28.

Tests:
- laptop (Python 3.14, python -I -m unittest): 78 tests, 69 run, 9 skipped (the pty and real-socket
  class needs Linux), 0 failed.
- WSL Ubuntu (Linux, Python 3.14): the same 78 all run and pass, three times in a row with
  -W error::ResourceWarning; run_tests.py in full: 95 of 95 (the 94 existing checks plus the voice
  section).
- Real-socket class covers: ops answered in under 0.5 s with the loop tick set to 3 s, a 100-chunk
  clip arriving at a bound stt.sock in order and unchanged with two journal lines, boot reset and
  re-hello, voice_msg to phone.sock only, a 0.4 C6 refusing voice ops, and the port's wait on a pipe.

Cost: no flash or panel RAM. Pi: about 14 KB more source; per voice line one json.loads and one
sendto (about 34 a second while someone talks); a few hundred bytes of state.

Notes for others:
- G (phone_bell) and F (stt_service) should use op names voice_ack and voice_text; voice_dismiss works
  too. A voice_ack for a busy floor slot comes back as {"rejected":"busy"} (G retries once).
- Not fixed here: if net.unix.max_dgram_qlen on the Pi is small, the bridge never retries a full
  stt.sock (EAGAIN); the line is counted as not passed on and the service fills up to 3 gaps. The
  WSL value is 512; the Pi's is UNVERIFIED (design 13.3).

## 2026-11-20 Piece A: capture, DSP and ding

Built (nothing flashed, installed or committed; no arduino-cli or fw.sh run):
- src/core/voice_dsp.{h,cpp} (new, plain C++): VoiceResampler (44.1 kHz stereo to 16 kHz mono),
  VoiceEncoder (IMA ADPCM and mu-law into VoiceChunks), VoiceLevel (noise floor, onset, early stop,
  level bar), VoicePreRoll, VoiceCapture (one VoiceRec's life inside the mic task: stop rules, first
  sends in order inside the 32-chunk window, END), VoiceHandshake (the g_rec / g_rec_busy handshake,
  the doorbell flag and the recording flag). Named VoiceHandshake because piece B has a VoiceLink.
- src/board/board_audio.cpp: the voice tap in mic_task (int64 power accumulator, stack 3072 to 4096,
  stack high-water mark and tap timing in `info`), pre-roll ring in PSRAM, voice_rec_begin/stop/release,
  voice_recording, chime deferral (4-slot list, 50 ms queue wait while recording) and doorbell abort
  (audio_play and audio_task set the flag; only mic_task touches the VoiceRec), Sound::Ding rendered on
  demand and freed (soft chime if no room), play_clip takes the sound number, voice_dump.
- src/core/synth.cpp: Sound::Ding = one G5 kBar strike (0.7 s); the gain memo covers Sound 0-5.
- tcomm.ino: `sound ding`, `voice dump [seconds 1-3 [gain dB 0-36]]` (prints the ADPCM chunks as hex in
  the tests/vectors chunk format), help lines, and a forward declaration of board::voice_dump (board.h
  is P0's; move it there when the interfaces next open).
- tests/test_voice_dsp.cpp.

Decisions that differ from or add to the design (owner can undo each):
- Anti-alias filter: the design's two biquads (4th-order Butterworth at 6.5 kHz) give only -15 dB at
  10 kHz, so its own test (10 kHz below -30 dB) cannot pass. Four biquads at 6.3 kHz (8th order, constants
  kVoiceLpStages and kVoiceLpHz) measure -42 dB at 10 kHz, -2.0 dB at 6 kHz, flat below 3 kHz.
- Pre-roll: a 6-block ring (70 ms, 6.2 KB PSRAM) is filled while idle; a recording starts with the
  blocks from 60 ms before the press (kVoicePreRollLeadMs, 0 turns it off). The clip can therefore run
  to 3.07 s (104 chunks, inside kVoiceClipChunks).
- Speech detection also counts an absolute line (-36 dBFS in 3 of 4 blocks) besides floor + 12 dB, so
  a loud first syllable during the floor's warm-up cannot make END say speech 0.
- A recording started while a ring (Chime) is sounding is ended at once with the bell reason.
- mic_task makes at most 3 first sends per block, retries a refused esp_now_send on the next blocks, and
  after 3 refusals counts the chunk as sent so LVGL's resend path covers it (first_ms is set).
- If END cannot go out (window never opens) mic_task sets done 12 s after the press (kVoiceStuckMs).
- Cancel and Bell stops send no last chunk and no END (LVGL sends the ABORT); every other stop flushes.
- voice dump uses VoiceRec::to = 0xFF (kVoiceToNone): nothing is sent and the window is ignored.

Tests (g++ 16.2, C++17, -Wall -Wextra -Werror, by hand into the scratchpad): test_voice 49 tests,
134,860 checks, 0 failed at -O0, -O1 and -O2 (the 31 new ones are dsp_*); two mutants (release ignores
busy, window check removed) are caught. test_core 9,023 passed (unchanged), test_gateway passes.
board_audio.cpp syntax-checked with stubbed Arduino/FreeRTOS headers (-Werror).

Cost: voice_dsp about 4.3 KB code on x86 -Os (expect 4-6 KB on the S3) plus about 3 KB for the
board_audio additions: about 8 KB flash. RAM: VoiceCapture 1.7 KB BSS, mic stack +1 KB, gain memo
+256 B; PSRAM 6.2 KB always (pre-roll) and 66 KB for about 1 s per ding; the clip is B's 25.7 KB.
CPU: the resampler runs 3 us per block on the laptop; estimate 100-200 us per 11.6 ms block on the S3
(1-2 % of core 0), measured on the panel by `info` (tap avg and max).

For the owner and the other pieces:
- Link needs board::radio_send_voice from piece B (board_radio.cpp); board_audio.cpp calls it.
- Mic task never first-sends a chunk 32 or more past rec->base; B writes base from each VOICE_ACK.
- Check on the panel: `sound ding` by ear; `voice dump 3` and `voice dump 3 30` for 13.2 (decode the
  hex with tools/voice_vectors.py's decoder or any IMA decoder); `info` for the mic stack and tap time.

## 2026-11-20 piece G: phone_bell (Pi side, WeatherGate repo)

Built (Pi repo files only; nothing deployed, nothing committed, no ssh):
- phone_bell.py: Engine._voice_msg for a `voice_msg` datagram: push to the RECEIVING floor's topic,
  title "Message from <sender>", priority 3, tag speech_balloon, never quiet_ok, 15 min stale rule,
  dedup per (floor, id), one Got it http action + click to /voice/<token>. Tokens are
  secrets.token_urlsafe(16), at most 8 kept, valid voice_token_h (new [rules] key, default 12 h,
  1-24), compared with hmac.compare_digest. POST /voice/<token> -> Engine.voice_ack: 200 "Got it"
  (removed), 200 "Already dismissed at the panel" (gone, token stays used), 409 "Already done"
  (second phone, also while the first is in flight), 410 "This has expired", 502 "Couldn't reach the
  panel" (state back to open). A bridge "rejected":"busy" is retried once 1.6 s later (check fix C9).
  GET /voice/<token> shows a one-button page and never acts. `phone_bell.py tryvoice <floor>` helper.
  Marked TODO(voice) in voice_ack: a panel dismiss does not clear the phone push (kVoiceDismissClearsPhone).
- Test text, tokens, topics and addresses never reach the log (new checks plus the existing log scan).
- PHONE_BELL_INSTALL.md: addendum with the rules and the deploy and undo commands.
- The panel's Open/Busy rule is applied by the panel (VOICE_PUSH is only sent then); phone_bell cannot
  see the status, so it applies only quiet hours, age, topic and dedup.

Tests: test_phone_bell/run_tests.py 343 of 343 pass (236 existing, 107 new in section 13b), run on
Windows with the laptop's python (the Unix-socket section 13 is skipped there, as before).

For the owner: deploy order is bridge first (it forwards voice_msg and the voice_ack op), then
phone_bell.py; both need Mike's yes. Expect the Pi cost to be negligible (a few KB, no new threads).

## 2026-11-20 Piece C: panel UI

Built (nothing flashed, installed or committed):
- src/ui/ui_voice.cpp (new): the recorder (Sheet::Voice), the keyboard's voice strip and the
  arrival dialog, drawn from View::voice, acting through Actions::voice_*.
  - Badge TAP (LV_EVENT_PRESSED) starts the Message job at once, then opens the dim card: "Message
    to UPSTAIRS/DOWNSTAIRS", "Speak now: 3 seconds", a 140 px ring draining at 10 Hz around the
    countdown digit (from the press, kVoiceRecMs), a level bar, STOP and CANCEL. Then "Turning speech
    into text" (or "Waiting for the other floor's message") with stepping dots and CANCEL; then the
    result in B26 (B20 past 3 lines) with SEND TO <floor>, EDIT, TRY AGAIN, CANCEL. SEND is replaced by
    a grey "<floor> panel is offline" / "needs the update" from peer_offline / peer_voice. Sending,
    then a "Delivered" toast and the sheet closes. Errors show the VoiceErr text with TRY AGAIN and
    CANCEL; after "<floor> didn't answer" TRY AGAIN sends the same text again. 60 s idle close on the
    result, error and edit screens.
  - Hold rule: released more than 0.6 s after the press = hold, the release stops it; sooner = tap
    mode. Check fix C2 is done with PRESSING events, not the card's draw event (LVGL draws in chunks,
    so the first draw event can fire before the stall ends): a release seen more than 100 ms after the
    last PRESSING is judged by that last sure moment the finger was down. PRESS_LOST before 0.6 s
    cancels silently. PRESS_LOCK on the badge area and the mic key.
  - EDIT: the text in a 120-character, 3-line field with the keyboard (mic key included); the
    keyboard's Done or DONE returns to the result with the edited text.
  - Keyboard strip (784 x 56 over the top key row): digit, 300 px drain bar, level, STOP, CANCEL;
    other keys stop taking taps; the mic key turns red with a white stop square (a second press stops).
    Working and waiting states with dots; errors 4 s; "Only part of it fit" 3 s. Insert at the field's
    end through the owner's on_char: a space first unless empty or after a space, doubled spaces
    dropped, voice_apply_capital (check finding 2), the title's shift rule. lv_disp_trig_activity on
    every strip change. The strip lives and dies with its keyboard; its DELETE event cancels a job.
  - Arrival dialog: over the sender's tile at tile y 90-193 (screen 164-267 / 369-472), x 12-398;
    "FROM UPSTAIRS  9:42 PM" in B14, "+1" when more wait, text B20 if it fits the room else B16;
    3 px border in the sender tile's status colour (x 0.75); 200 ms fade; 700 ms tap guard (a press
    that began inside it never dismisses); hidden while any sheet is open, the calendar is down or
    the screensaver runs (panel only); the doorbell banner is moved above it; one tap calls
    voice_dismiss and the next message shows with a fresh guard.
- src/ui/ui.cpp: badge_hit (tile x 287-386, y 97-193, check fix C1) after note_hit, PRESSED /
  PRESSING / RELEASED / PRESS_LOST to ui_voice; clickable only on the own tile while
  View::voice.capable; voice_render after render_banner; open_sheet(Sheet::Voice). The note hit,
  status word, chevron, mail, no-mail and arrows are unchanged.
- src/ui/ui_common.cpp: close_sheet calls voice_sheet_closing (a recording, result or dictation in
  the closing sheet ends: cancel while recording or working, done after a result).
- src/ui/ui_internal.h: Keypad.on_mic (returns the field's text, null for no field) and Keypad.mic;
  the ui_voice entry points.
- src/ui/ui_calendar.cpp: KeyKind kMic; row 4 is ?123, comma, MIC, space, period, Done when on_mic
  is set (keys[34] full, static_assert); blue 0x1F5FBF (night 0x2A56A8) with the white mic icon;
  PRESSED/PRESSING/RELEASED/PRESS_LOST to ui_voice; on_mic hooked for the event editor (focused
  field); set_focus(kNone) cancels a dictation on that keyboard.
- src/ui/ui_note.cpp: on_mic hooked (only when voice is offered).
- src/ui/art.{h,cpp}: CalIcon::Mic (22 x 22: capsule, U, stem, base).
- src/app/panel_app.h: forwards voice_press/stop/cancel/done/send/dismiss to the Controller.

Tests (scratch builds by hand with g++ 16.2; build_sim.sh and fw.sh not run):
- -Wall -Wextra -Werror at -O1: ui_voice, ui, ui_common, ui_calendar, ui_note, art: no warnings.
- A scratch simulator driver (the real UI, controller and demo, a fake voice backend: canned text
  0.9 s after the stop, every error path): 96 checks, 0 failed, day and night, with 40 screenshots
  looked at. Covers badge hold and tap, STOP, CANCEL, TRY AGAIN, the C2 stall (a tap hidden by a
  650 ms draw stall stays a tap; a finger still down after it is a hold), EDIT with dictation
  mid-field ("Dinner is ready dinner is ready": space added, sentence capital lowered), SEND with
  the edited text, Delivered, offline and needs-update, no answer and resend, the note keyboard's
  strip (hold, tap, error 4 s, "Only part of it fit", editor closed mid-job cancels), the touch map
  (badge, small status text, status word, other floor's badge, badge without voice), the dialog
  (guard, +1, one-tap dismiss, next message, waits for a sheet, under the banner, both tiles).
- The real sim/sim_main.cpp built and run into the scratchpad against the current tree: all its
  touch suites still 0 failed.

For the owner and piece I:
- sim/sim_main.cpp's Proxy does not forward voice_* yet (not this piece's file); with the
  Controller saying capable, a badge press there shows "Couldn't start the recording". The keyboard
  screenshots in docs/screens will show the new mic key once build_sim.sh runs.
- voice_send is called from Idle after EDIT (the UI keeps its own copy of the text and ends the
  Controller's job before a dictation in EDIT); B's voice_send accepts that.
- Pre-existing, not touched: at night the note editor's chosen duration chip ("4 hr") has cream text
  on a cream face, so its word is invisible (theme cal cream equals ink at night).

Cost estimate: ui_voice.o is about 19 KB (text + data) on x86 -Os, the edits to the other UI files
about 1.6 KB more; expect about 15-20 KB of flash on the S3, and the same again in the PSRAM code copy.
Internal RAM about 2.2 KB of statics (the edit keyboard's Keypad is the bulk). LVGL heap only while
open: the card about 3 KB, the EDIT keyboard about the note editor's. PSRAM: the mic icon 22 x 22
(about 1.5 KB, made once). Draw: the ring arc's changed segment at 10 Hz, bars in 4-10 px steps,
dots 30 x 8 px every 300 ms; a state change rebuilds only the card's children.

## 2026-11-20 Piece B (link, controller, relay) and piece R (reach test)

Built (nothing flashed, installed or committed; no fw.sh, build_sim.sh, arduino-cli, esptool, ssh or COM port):
- New, plain C++: src/core/voice_tx.{h,cpp} (VoiceTx: BEGIN every 150 ms, via upstairs from 450 ms on the
  downstairs auto route, given up at 1.5 s; resends of chunks the latest VOICE_ACK misses, 200 ms after
  their last send, oldest first, 4 a tick, 5 sends a chunk, 2 x chunks in all, only below first_sent;
  rec->base written from each ACK; END every 200 ms for 1.5 s; base stalled 1.5 s; text wait 8 s, a Queued
  status extends it once to 20 s; ABORT 3 times at 0/100/250 ms. VoiceOut: 4 SAY/PUSH slots, tries
  0/150/400/750/1150 ms, give-up 1.5 s) and src/core/voice_inbox.{h,cpp} (4 messages, 5th drops the oldest,
  dedup by the last 8 ids, restore checked by voice_inbox_valid; voice_arrival = the ding/wake/push table).
- controller.cpp: voice frames go to on_voice before decode(); BEGIN/DATA/ACK upload, VOICE_TEXT answered
  with VOICE_GOT (a dead xfer's "gone"), SAY to the other panel and its GOT (Delivered / PeerNoAnswer and
  No signal on expiry), the inbox (saved on every change, loaded in begin()), ding at soft_volume or
  deferred while recording, voice_wake, VOICE_PUSH only when Open/Busy, clock valid and outside quiet hours,
  VOICE_DISMISS from the gateway's side (direct, or relayed by upstairs) with GOT result 0/1, the
  "TODO(voice): clear the phone push" hook in voice_dismiss, kStateVoice in every STATE, the auto route
  (ping 3 s after start, every 10 min, after a failed direct BEGIN; a miss retried 2 s later; 3 in a row =
  relay until a good one), View::voice. controller.h: two includes and B's block at the marked spot (a small
  public part: VoiceRoute, set_voice_route, voice_direct_ok, voice_report, voice_inbox).
- brightness.h: BacklightInputs::voice_wake (lights the dark hours like mail_wake).
- board_radio.cpp: radio_send_voice (no trace, no elog, atomic counters), on_recv counts voice frames
  instead of tracing them and, on upstairs, queues what voice_relay_frame says to relay (g_relay, 32 frames,
  storage in PSRAM, upstairs only) for the "vrelay" task (core 0, prio 3, 2560 B) that rewrites the sender
  byte; `radio` prints the voice and relay counts. Piece R: radio_reach (type-31 frames, MAC-level counts to
  the gateway only) and the ping variant (VOICE_PING nonce 0x5245xxxx, counts VOICE_GOT kind 30 back).
- device_platform.{h,cpp}: radio_send_voice (never through the demo loopback), the capture hooks to
  board::voice_rec_*, the inbox saved by flush_saves (kDirtyVoice). board_storage.cpp: load/save_voice_inbox
  (NVS blob "vinbox", version 1, checked with voice_inbox_valid).
- tcomm.ino: `reach N [ms]` and `reach ping N [ms]` (outside the LVGL lock), `voice`, `voice route
  auto|direct|relay`, help line, PanelPlatform::play stops the screensaver and triggers activity before a
  Ding (recursive LVGL lock, so A's `sound ding` outside the lock is safe too).

Tests (g++ 16.2 by hand, -Wall -Wextra -Werror, into the scratchpad, built the way the test_voice line of
build_sim.sh builds: tests/test_voice_*.cpp + src/core/*.cpp + voice_session.cpp):
- tests/test_voice_link.cpp: 34 tests, run against a reference C6 model and against piece D's own
  gw::Gateway with a fake Pi (gw_* tests). Lossy air 10 % loss, 5 % duplicates, 25 ms reordering: 48 runs
  on the reference C6 and 24 on D's gateway, all complete, 1342 resends seen, window never passed, never
  more than 5 sends a chunk or 3 x chunks in all. Relay forced, BEGIN fallback, route switch by pings,
  gateway silent, no Pi, END and text timeouts, Queued, cancel (3 ABORTs, late text "gone"), bell,
  clip freed only after release, SAY delivered / expiry / dedup across lost GOTs, inbox 5th drop and
  restart dedup, phone dismiss present/absent, ding table, no transcript in any log line.
- test_voice in all (with A's and P0's tests): 83 tests, 141,164 checks, 0 failed. test_core: 9,023 passed
  (unchanged). test_gateway: 928 passed (D's current tree).
- Firmware files: board_radio.cpp (radio on and off) syntax-checked against stub ESP-IDF headers with the
  example secrets file; device_platform.cpp's only errors there are the host libc's (setenv,
  settimeofday), not the new lines. tcomm.ino not compiled (needs the Arduino build).

Cost (estimate; x86 -Os object sizes): flash about +14 KB (controller +8.5 KB, voice_tx 3.7 KB, voice_inbox
1.1 KB, board_radio and device_platform about +1.5 KB), the same again in the PSRAM code copy. Internal RAM:
Controller +2.3 KB (VoiceTx 872 B, VoiceOut 608 B, inbox 568 B), DevicePlatform +1.1 KB (inbox copy and the
flush copy); upstairs only: vrelay stack 2.5 KB and 8 KB of PSRAM for the relay queue. Per job: the clip,
25.6 KB of PSRAM, freed when the text is in.

For the lead / owner:
- tools/build_sim.sh is P0's: its test_voice line does not link app/controller.cpp, gw/gateway.cpp or
  gw/json_line.cpp, so test_voice_link.cpp #includes those three .cpp files itself. If that line is ever
  given them, drop the #includes at the end of the test.
- D's gateway.cpp:667 trips -Wformat-truncation under -Werror (harmless: t0, t1 are checked to 60 first);
  silenced around the #include in the test only.
- D's C6 answers VOICE_PING only from downstairs, so `reach ping` on upstairs gets no answers (expected;
  the plain `reach` is the upstairs comparison).
- The route is RAM only (serial `voice route`, default auto); after the reach test sets the default,
  Controller::kVoiceRouteDefault is the one constant to change.
- View is rebuilt on the next 50 ms tick after a voice intent (like every other intent).

## 2026-11-20 Piece H: tap to place the cursor

Built (nothing flashed, installed or committed; no fw.sh, build_sim.sh, arduino-cli, esptool, ssh or COM port):
- src/core/text_edit.{h,cpp} (new, plain C++): TextEdit with te_insert / te_insert_str / te_backspace /
  te_delete / te_set at a cursor; te_hit (the gap nearest x, each glyph split at its midpoint, exactly on it
  goes after); te_window (the calendar's one-line view that follows the cursor both ways and slides back
  after a deletion); te_wrap (the note's word wrap, moved here unchanged from note_wrap so it can be
  tested); te_line_of (a cursor exactly at a break belongs to the next line); te_sentence_start (the
  title's capital rule at the cursor: 0, only spaces, or after ". ", "? ", "! "); a 6-slot registry
  (te_register / te_find) so the voice strip finds the cursor that goes with the text on_mic returns.
- src/ui/ui_note.cpp: note_wrap calls te_wrap; the field takes PRESSED and PRESSING (PRESS_LOCK): y picks
  the line, te_hit the gap (a line above the last ends before its break space); a slide moves the cursor
  at most 20 times a second. Keys insert at the cursor; the 2-line check undoes an insert that would need
  a third line; a space is refused next to a space on either side or at the start; backspace deletes
  before the cursor and tidies a space it leaves leading or doubled (undone if the joined words would need
  a third line). The cursor is drawn on the line that holds it, 3 px wide (was 2), centred on the gap.
- src/ui/ui_calendar.cpp (field parts only): each text field keeps a TextEdit and its view's first
  character. The focused field shows the window round the cursor with an ellipsis at whichever end the
  text goes on (fit_tail removed); holding a finger on either ellipsis scrolls a character per 50 ms.
  PRESSED on the title or notes field places the cursor before CLICKED focuses it; set_focus keeps the
  cursor and the title's shift follows te_sentence_start at the cursor (check fix C12), also after a tap
  in the focused title. Keys and backspace at the cursor. Cursors start at the end of each text when the
  editor opens and survive a change of light. Cursor 3 px wide.
- src/ui/ui_voice.cpp: the EDIT field the same way (line starts read from the label's own wrap with
  lv_label_get_letter_pos, the gap by te_hit on the label's own x); keys, backspace and the 3-line check
  at the cursor. strip_insert now inserts at the cursor of a registered field: a space before unless at
  the start or after a space (a space already after the cursor is stepped over instead, since the note and
  EDIT refuse a space next to a space), a space after when a letter or digit follows (never counted as a
  cut), and voice_apply_capital with the real cursor (the flags-byte rule).

Tests (g++ 16.2 by hand into the scratchpad, -Wall -Wextra -Werror):
- tests/test_voice_text_edit.cpp (named test_voice_* so build_sim.sh's test_voice line picks it up, as
  the P0 notes ask; DESIGN section 11 calls it test_text_edit): 17 tests, 9034 checks, 0 failed. Insert at
  start, middle, end and full; insert_str stops at the first refusal; backspace and delete at both ends;
  stale cursors clamp; the midpoint rule with a fixed-width and a proportional fake font, past the end, a
  middle range; te_window both ways, stale first, slide-back after deletes, and a property sweep (4 texts
  x 14 widths x every cursor, both directions); te_wrap breaks (space, trailing space, mid-word, max
  lines); the cursor on each line and at a break; insert-then-undo keeps the text exact; the capital rules;
  the registry. The whole test_voice build: 100 tests, 150198 checks, 0 failed. test_core: 9023 passed.
- A scratch simulator (piece C's harness, the real UI): 56 checks, 0 failed, day and night. Note field:
  tap mid-word, left of the text, right and left half of a glyph, backspace mid-text, press and slide,
  the no-double-space rule, voice text mid-text with both spaces and the capital lowered, a tap past the
  end of line 1, a tap at the start of line 2. Calendar: title tap mid-word, the capital at 0 (C12) and
  none mid-word, an unfocused notes field focused by a tap, a long notes field showing its end with a
  leading ellipsis, holding on the ellipsis scrolling back to the start, an insert at a tapped place in
  the long field, focus moved back to the title at the tapped place. EDIT: insert at a tap, backspace at
  0, voice at the cursor, a tap at the start of the second wrapped line. 16 screenshots looked at.
- The real sim/sim_main.cpp built and run into the scratchpad against this tree: its 10 touch suites
  still report 0 failed.

For the owner:
- After a voice insert the cursor sits after the space added behind the text, so a second dictation
  needs no space; typing straight on then glues to the next word. Say if the cursor should stop before
  that space instead (one line in strip_insert).
- While a dictation is working, a tap in the field still moves the cursor, and the text then goes where
  the cursor is at the moment it arrives.
- Not mine, seen while building: ui_outside.cpp has an unused redraw() that fails -Werror (build_sim.sh
  does not use -Werror, so no harm there).
- Speed on the panel is not measured. A press runs te_hit (at most 120 prefix measurements) and, in a
  calendar field, te_window; the worst case is a long notes field whose view jumps (a few thousand glyph
  widths, a few ms estimated). The redraw is the field box only.

Cost estimate (x86 -Os objects, old against new): text_edit about 1.9 KB code and 32 B RAM; ui_calendar
+1.2 KB code, +64 B RAM; ui_note +0.5 KB, +32 B; ui_voice +0.9 KB, +32 B. About 4.5 KB of flash on the S3
(expect 4 to 6 KB), the same again in the PSRAM code copy, about 160 B of internal RAM, no new PSRAM or
LVGL heap beyond what the fields had.

## 2026-11-20 Whole-tree verification (all pieces)

Nothing flashed, installed, copied to the Pi or committed; no fw.sh, build_sim.sh, arduino-cli,
esptool, ssh or COM port; secrets.h and phone.conf not read.

Host tests (g++ 16.2, C++17, -Wall -Wextra -Werror, built by hand into the scratchpad):
- test_core 9,023 passed; test_gateway 928 passed; test_voice 100 tests, 150,201 checks, 0 failed at
  -O1, -O0 and -O2 (the -O2 build needs -Wno-error=stringop-truncation for an old strncpy in
  Controller::send_outside, which is in HEAD and not voice work); -O2 run 4 times (one test uses a thread).
- tests/test_voice_vectors.py 6 OK; tools/voice_vectors.py --check: 8 files, 0 differ. The Pi copies in
  test_stt/vectors are byte-identical to tests/vectors.
- Pi (laptop): test_stt 64 OK (1 skipped), test_phone_bell 343/343, test_tcomm_bridge/test_voice.py 78
  run (9 skipped, Linux only). WSL Ubuntu: run_tests.py for the bridge 95/95, test_voice.py 78 with
  none skipped, test_stt 64 OK.
- -fsyntax-only -Werror: every ui/*.cpp, app/*.cpp, core/*.cpp, sim_main.cpp, the C6 gw/*.cpp and
  shared/*.cpp clean; board_audio.cpp, board_radio.cpp and device_platform.cpp clean against stub
  ESP-IDF and Arduino headers. Not compiled: board_storage.cpp (FFat, SD_MMC, Preferences), tcomm.ino
  and gateway-c6.ino; their diffs were read instead.

Checks: line endings of every changed file match HEAD in both repos; new files LF and ASCII; no em
dash added anywhere. The tcomm changes hold none of the vocabulary words as names, no ntfy topic, no
Pi login name and no IP address other than 127.0.0.1 (in the untracked .claude/launch.json).

Small slip fixed: VoiceHandshake::begin now clears the doorbell flag, so a ring_bell() that raced the
previous release cannot end the next recording at once (voice_dsp.cpp, one line; a check added to
dsp_link_basics in tests/test_voice_dsp.cpp).

## 2026-11-20 Field fix: nothing voice-related to a gateway that has not said it takes voice

What the field showed tonight: with the C6 still on gateway-c6 0.4, minutes after both panels booted
the voice firmware the C6 got no TComm ACK from upstairs at all, and only 1 to 4 of its 5 tries reached
upstairs at radio level, every minute. The panels still took the C6's frames. It cleared only when the
panels were rolled back AND the C6 was restarted; a C6 restart alone, with the voice firmware still on
the panels, did not help.

What it was (as far as the host can show):
- The voice firmware does not flood the air. Two real Controllers against git HEAD's own gw::Gateway
  0.4 (scratchpad flood harness) give the same TComm traffic as HEAD, frame for frame, and the C6 gets
  every OUTSIDE ACK in 10 ms. No relay, no loop, no resends and no mic sends while nobody records.
- The one new thing on the air was downstairs' VOICE_PING to the gateway: 3 in the first 8 s after
  boot, then one every 10 minutes (controller.cpp tick_voice_ping). They were the first frames the
  downstairs panel had ever sent the C6, which drops them unanswered (HEAD gateway.cpp:250).
- That a C6 restart was needed after the rollback says the C6 kept something from those frames. It
  fits the C6 0.4's radio or ESP-NOW state being upset by downstairs' frames, and it does not fit a
  panel-only cause such as low internal RAM (that clears with the rollback). The exact radio-level
  mechanism is not proven: the host cannot model it.

The fix (panel firmware and C6 0.5):
- The C6 0.5 sends a voice hello: VOICE_GOT kind 30 (Ping), key 0, floor 0, header seq 0, to upstairs
  only, at start-up and every 2 minutes (gateway.cpp voice_hello, from tick). No ping nonce is ever 0,
  and upstairs never relays a floor-0 frame.
- A panel that hears any voice frame straight from the gateway marks it as taking voice for 10 minutes
  (Controller::voice_heard_gateway). Upstairs passes that on in its STATE as kStateGwVoice (0x20,
  protocol.h); a panel sends only what it heard itself, never the other panel's flag back.
- Until one of those holds, a panel sends the gateway nothing voice-related: no VOICE_PING, no BEGIN
  (the press shows "Gateway needs an update" at once and takes no clip), no VOICE_PUSH (one log line).
  SAY between the panels still works. Five hellos missed in a row turn it off again, with a STATE.
- Downstairs' ping gap after 3 misses now doubles from 10 minutes up to an hour.
- An air cap on every voice frame: the panel's radio_send_voice at 100 a second, bursts of 40
  (board_radio.cpp, under a lock; refusals counted as "over the air cap" in `radio`), and the C6's own
  voice frames at 50 a second, bursts of 25 (gateway.cpp send_voice_to; GwStatus::voice_capped). The C6
  answers downstairs' pings at most once per 250 ms. The TComm frames and ACKs are never capped.
- voice_proto.h gained kVoiceHelloKey, kVoiceHelloEveryMs, kVoiceGwStaleMs and the small VoiceRateCap
  (it is the frozen P0 file; these are additions only, no frame changes, the vectors are unchanged).
- Serial `voice` now ends with what the panel knows of the gateway ("no voice hello, nothing sent to it").

Tests (g++ 16.2, C++17, -Wall -Wextra -Werror, by hand into the scratchpad; no build_sim.sh or fw.sh):
- New in test_voice_link.cpp: the reference C6 sends the hello, and has a 0.4 mode (no hello, every voice
  frame dropped) and the Pi's OUTSIDE on TComm's retry schedule.
  - fieldfix_old_c6_idle_ten_minutes: both panels against the 0.4 model for 10 minutes: 0 voice frames
    on the air, all 10 OUTSIDE ACKed on the first try (worst 20 ms). A mic press on each panel gives
    "Gateway needs an update" with nothing sent; a SAY still arrives, the phones are not told; 10 more
    minutes, still nothing to the C6.
  - fieldfix_c6_rolled_back_panels_go_quiet: hellos stop, both panels go quiet within 12 minutes and send
    no voice frame in the next hour; hellos back, voice back on both.
  - fieldfix_ping_backoff: a C6 that never answers pings gets 8 in 4 hours.
  - fieldfix_new_c6_idle_ten_minutes_then_recordings: against piece D's real gw::Gateway with an OUTSIDE
    a minute: 7 voice frames in 10 minutes (0.012 a second, worst second 2), 10 of 10 OUTSIDE ACKed, then
    a dictation upstairs and a message downstairs both get their text, an OUTSIDE after them is ACKed and
    nothing hit the C6's air cap.
- New in test_gateway.cpp: test_voice_air_safety (the hello's timing, target, bytes and seq; the ping
  answer gap; the cap on 200 PUSHes in one millisecond; VoiceRateCap's rate and its long-gap refill).
  FakeIO keeps the hellos apart so the older tests count only their own frames.
- Results: test_voice 104 tests, 150,248 checks, 0 failed; test_gateway 1,364 passed, 0 failed;
  test_core 9,023 passed, 0 failed; tests/test_voice_vectors.py 6 OK; tools/voice_vectors.py --check 8
  files, 0 differ.
- Scratchpad flood harness, the fixed panels against HEAD's real gateway-c6 0.4 for 1,300 s: upstairs 76
  frames, downstairs 32, the gateway 44, the same bytes as HEAD's panels; 0 voice frames (before the fix:
  5 VOICE_PINGs from downstairs); 44 of 44 requests ACKed.
- Not compiled: board_radio.cpp (no ESP-NOW stubs here); its change was read instead.

For the owner:
- To confirm in the house: flash both panels with this build and leave the C6 on 0.4. `voice` on
  downstairs should say "no voice hello, nothing sent to it", and the C6 should keep getting its ACKs.
  If the C6 still loses its ACKs with no voice frame on the air, the cause is in the build itself
  (memory or timing) and not in the voice traffic; `radio` on upstairs then tells which (send refusals).
- After the C6 0.5 goes on, a panel that has just started waits up to 2 minutes for the hello before
  the mic works ("Gateway needs an update" until then). Say if that is too long; the hello gap is one
  constant (kVoiceHelloEveryMs).
- DESIGN.md is not updated yet for the hello and kStateGwVoice; this entry is the record for now.

## 2026-11-20 Field fix 2: the voice build uses less internal RAM than the old build

Why: tonight's field result (ACKs lost upstairs right after boot, even with all voice traffic gated
off) points at the build itself. Leading theory: internal RAM starvation of the Wi-Fi driver's
dynamic buffers. The real link said "Global variables" 121,008 (old) -> 129,616 (voice), +8.6 KB, and
on top of that the voice build took the vrelay task (2,560 B stack + 352 B TCB, upstairs only) and
1 KB more mic stack from the internal heap. The old build had about 38 KB free, 22 KB largest block.

The panel's sdkconfig (high_perf libs, qio_opi): CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is NOT set
(so EXT_RAM_BSS_ATTR would do nothing), CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y (task stacks may
be in PSRAM), CONFIG_SPIRAM_FETCH_INSTRUCTIONS and CONFIG_SPIRAM_RODATA (code and constants already
run from PSRAM), CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096 (a plain malloc under 4 KB stays internal),
static RX buffers 8, dynamic RX 32, dynamic TX 32. So: heap_caps_malloc(MALLOC_CAP_SPIRAM) blocks
behind pointers or references, and a PSRAM stack for vrelay.

How it was measured: every panel source (src/app, board, core, gfx, ui and tcomm.ino) compiled with
the panel's own xtensa-esp32s3-elf-g++ 14.2 and the build's flags, defines and include paths (-Os,
TCOMM_RADIO_ESPNOW=1, TCOMM_DEMO=0, qio_opi sdkconfig), for git HEAD, for the voice build and for this
fix, then data+bss per object (size, nm). Not linked, no arduino-cli. The voice build came out +8,619
over HEAD, against the real link's +8,608, so the method holds to about 10 bytes.

What the voice work had added to internal RAM, and where it went (bytes, voice build -> now):
- g_ctrl (tcomm.ino:149) 11,020 -> 7,308. New Controller::Store (controller.h:438) holds VoiceTx 868,
  VoiceOut 608, VoiceInbox 568, the voice text 121, the last job's stats 24 and the whole View 1,552
  (View::voice is 288 of it); one block from the new Platform::alloc_state (controller.h:84; malloc on
  the host, PSRAM on the panel via DevicePlatform::alloc_big, device_platform.h:38), made in the
  constructor (controller.cpp:161 make_store) and freed by ~Controller. The members that named those
  parts are references into it, so no other code changed. Old build: 8,440.
- g_platform 9,564 -> 9,000 and the static copy in flush_saves -568: the voice inbox's pending copy and
  its outside-the-lock copy are one 1.1 KB PSRAM block made at the first save
  (device_platform.cpp:136 save_voice_inbox).
- board_audio: VoiceCapture g_cap 1,664 -> a pointer, made in PSRAM in audio_begin
  (board_audio.cpp:447); voice dump's 522-byte static line buffer now rides at the end of the clip's
  PSRAM block (board_audio.cpp:586).
- synth's gain memo 768 -> 152: only the chime has a gain per ringtone (synth.cpp:610); the old build's
  table was 512.
- ui.cpp: g_prev and render's pv, two whole View copies of 1,552 each (old state, grown 292 each by
  View::voice), now made in the LVGL heap (PSRAM) by the new ui_keep<T>() (ui_internal.h:22).
- ui_voice.cpp: the recorder (916, its edit Keypad is most of it) and the strip (152) by ui_keep; the
  empty VoiceView g_none (288) is const now, so it sits with the constants in PSRAM.
- board_radio: the vrelay task keeps its 2,560-byte stack in PSRAM (xTaskCreateStaticPinnedToCore,
  board_radio.cpp:249); its TCB is a 352-byte static (board_radio.cpp:79), which is the one addition.
  Safe: its code already runs from PSRAM, the sdkconfig allows PSRAM stacks, and it only takes a queue,
  a spinlock and esp_now_send, never a flash write, so it never runs with the cache off.
- Mic task stack 4,096 -> 3,072 (board_audio.cpp:462), as before the voice work. send_data and
  send_end kept a VoiceMsg and a 250-byte frame on that stack (544 B each, -fstack-usage); both are
  VoiceCapture members now (voice_dsp.h:182), so send_data takes 32. Deepest voice path from
  -fstack-usage: mic_task 80 (voice_tap inlined) + block 32 + pump 48 + send_data 32 + hook 32 +
  radio_send_voice 32 = 256, plus esp_now_send's own path from the library's frame sizes (esp_now_send
  48, mt_send 32, ieee80211_send_action_vendor_spec 80, _mgmt_output 80, _output_process 80,
  _output_do 64, pp up to 160, and heap and lock calls), about 1.0 KB in all, about 1.3 KB with an
  interrupt's frame: well inside 3,072.
- Left in internal RAM from the voice work: about 0.9 KB (board_radio's counters, air cap, relay queue
  control block and the TCB 533; ui_calendar 148; ui_note 24; text_edit 24; ui_voice 68; board_audio 62).
  ss_host's +10 is the screensaver's carry word, not voice.

Estimated numbers:
- Panel objects data+bss: HEAD 77,436; voice build 86,055; now 74,329 (3,107 under HEAD).
- "Global variables": about 129,616 - 11,726 = 117,890 bytes (about 3.1 KB under the old 121,008).
- Internal heap at run time against the voice build: 11.7 KB static, plus 1,024 mic stack, plus on
  upstairs 2,560 + 352 for vrelay: about 12.8 KB more free downstairs and 15.7 KB upstairs. Against the
  old build: about 3.1 KB more free on both panels (about 41 KB instead of 38 KB, if nothing else moved).
- PSRAM taken instead: about 3.7 KB Store, 2.2 KB VoiceCapture, 1.1 KB inbox copies, 4.2 KB of
  interface state, 2.5 KB vrelay stack upstairs: about 14 KB of 8 MB.

Behaviour: unchanged. The Store and ui_keep objects are made while the statics are constructed; ESP-IDF
5.4 adds PSRAM to the heap in its CORE init stage (add_psram_to_heap, priority 103), before the
constructors run. If PSRAM had no room each falls back to internal RAM (and the controller to abort if
there were none at all). Serial `info` now prints a line saying where the controller's state block is
("memory: controller 7308 bytes in internal RAM, its state block in PSRAM"), and the mic stack line says
"of 3072".

Tests (g++ 16.2, C++17, -Wall -Wextra -Werror, into the scratchpad; no build_sim.sh, fw.sh or
arduino-cli): test_core 9,023 passed, 0 failed; test_gateway 1,364 passed, 0 failed; test_voice 104
tests, 150,248 checks, 0 failed; tests/test_voice_*.py 6 OK; tools/voice_vectors.py --check 8 files,
0 differ. ui.cpp, ui_voice.cpp and demo.cpp also pass a host -fsyntax-only -Werror check, and every
panel source cross-compiles with the real toolchain.

For the owner:
- Flash both panels with this build. `info` should say "its state block in PSRAM", the internal free
  figure should be about 41 KB (old build about 38 KB), and the mic stack line should leave well over
  1 KB never used. Then watch whether the C6 gets its ACKs from upstairs after boot.
- If the ACKs still go missing with this build, low internal RAM was not the cause.
- DESIGN.md still says 4096 for the mic stack and puts the voice state in internal RAM (its lines 45,
  242, 391, 745); this entry is the record for now.

2026-11-20, review of the internal-RAM fix. Checked: nothing moved to PSRAM is touched from an ISR;
on_recv and on_sent (Wi-Fi task) touch only what they touched before (counters, the trace, the
relay queue, whose storage was already PSRAM); the vrelay task takes no flash write; the Store and
the ui_keep objects come from heap_caps/lv_mem_alloc at static construction with PSRAM already in the
heap (SPIRAM_BOOT_INIT, SPIRAM_USE_MALLOC; lv_conf.h LV_MEM_CUSTOM 1), with internal fallbacks;
g_cap is checked everywhere it is used; voice_rec_begin refuses without it. One fix in
board_radio.cpp radio_begin: both PSRAM blocks are taken before g_relay is published, so a failed
vrelay start can no longer clear g_relay while on_recv (already registered) may be using it, and
nothing leaks. Host tests rerun: test_core 9,023/0, test_gateway 1,364/0, test_voice 104 tests
150,248/0, Python 6 OK; board_radio.cpp cross-compiles. To watch on the panels: the mic task's
never-used stack in `info` after recordings, including while the gateway is unreachable (send errors).

## 2026-11-21 The badge recorder becomes a row: long press, then START

Why (Mike, 2026-11-21, after testing the blue button with the C6 and Pi back in): push-to-talk on
the badge was less than ideal. The badge now needs a long press, the hold-to-talk on it is gone, and
the speech entry is laid out so that what moves is never under the finger: after pressing START and
pulling the finger away the person watches the words arrive, then comes back to SEND, CANCEL or edit.

Built (nothing flashed, installed or committed; no fw.sh, build_sim.sh, arduino-cli, esptool, ssh or
COM port):
- src/ui/ui_voice.cpp (LF): the recorder section rewritten (DESIGN 2.2, new first part). One card
  784 x 224 across the own tile, its bottom 4 px under the tile's. Left of the badge: the timer
  (J44, "3.0 s" in tenths rounded up) and its 232 px bar, full until START, draining red to the
  pixel from View::voice.press_ms (kVoiceRecMs), held where the recording ended while the Pi works;
  the MICROPHONE level bar (View::voice.level, which the capture path already fills: no new
  accessor); a hint. Where the badge was: START (blue, a white microphone drawn from boxes), from
  its PRESS; it turns into a red STOP (a new press's click stops early; the starting press never
  does); press lock, so a finger held through the 3 s lands on nothing. After recording: SEND over
  CANCEL; SEND grey until the words arrive, "SENDING" while it goes, and grey with "UPSTAIRS /
  offline" or "needs update" when the other panel can't take it. Right: the text box ("The words
  show here", "Listening", "Turning speech into text" with dots, the words, or the error in red),
  TRY AGAIN and KEYBOARD (the existing edit card with its blue mic key and cursor; Done comes back to
  the row; from an empty box it types a message from scratch). CLOSE top right at any time.
  Handlers only change state; the parts are rebuilt from the render loop; the big button is only
  restyled, never rebuilt under the finger. Gone: badge_press/release/lost and the Press on the
  recorder (the keyboard strip keeps its hold rule), the old 640 x 340 card and its ring.
- src/ui/ui.cpp (CRLF): badge_hit takes LV_EVENT_LONG_PRESSED only (voice_badge_long); comments.
- src/ui/ui_internal.h (CRLF): voice_badge_event -> voice_badge_long.
- docs/voice-notes/DESIGN.md: 2.2 rewritten at its top (the old text kept under "superseded"),
  decision 4.
- docs/voice-notes/shots/: v01_ready, v02_recording, v03_working, v04_result, v05_keyboard,
  v06_offline, v07_error (day and night each) and v08_upstairs_recording_day, from the harness below.

RAM (the panel's xtensa-esp32s3-elf-g++ 14.2 with the build's flags, scratchpad/ramtrim/xc.sh, not
linked): ui_voice.o data+bss 68 -> 68, ui.o 6,591 -> 6,591. "Global variables" stays about 117,888
(limit 121,000). Code +1.8 KB (flash). The Recorder (ui_keep, PSRAM) is about the same size; the
card's LVGL objects (about 35 while open) are in the LVGL heap, PSRAM. No new task.
Redraw while recording: the timer label at 10 Hz, the drain bar's few pixels each frame, the level in
4 px steps, three dots: under 10,000 px a frame (the old ring was about 20,000 at 10 Hz).

Tests (g++ 16.2, C++17, -Wall -Wextra -Werror, into scratchpad/badge2):
- Badge-row harness (scratchpad/badge2/sim: b.sh, b_main.cpp on piece H's head, the real UI,
  controller and demo with the fake voice backend): 93 passed, 0 failed, day and night downstairs and
  upstairs: a tap on the badge does nothing; the long press opens the row and records nothing;
  START sits on the badge, timer and level left of it, text box right; START records from its press
  and its release does not stop it; the timer exact to the cap at 0.26 s and 1.26 s; the cap ends it
  and the timer holds at 0.0; SEND does nothing before the words; KEYBOARD edits and the mic key
  dictates; DONE and SEND send the edited words; a finger held through to the result presses
  nothing; TRY AGAIN; STOP; offline and old peer; CANCEL; an error, then typed words sent; CLOSE
  while ready and while recording; note text, status word, the other floor's badge and the no-voice
  badge area unchanged.
- The real sim/sim_main.cpp built against this tree: all its touch suites 0 failed.
- test_core 9,023 passed, 0 failed; test_gateway 1,364 passed, 0 failed; test_voice 104 tests,
  150,248 passed, 0 failed.

For the owner:
- Flash both panels. Hold the badge until the row comes up, let go, press START, take the finger
  away, speak, watch the words arrive, then SEND or CANCEL (or KEYBOARD to fix them).
- The level bar moves only while recording: the microphones are off until START.
- This changes only the screen. The poor recognition is a separate fix on the Pi.

## 2026-11-21 Field fix 3 (panel piece): mu-law, a level boost, a raw microphone capture, a live meter

Why: 10 real clips from the upstairs panel at one foot, decoded on the Pi, came out quiet (speech
about -40 dBFS RMS, peaks -20 to -28) and nearly without treble (85.6 % of the energy under 500 Hz,
1.2 % from 1 to 2 kHz, nothing above 2 kHz; a clean voice through the same ADPCM keeps about 13 %
above 1 kHz). Vosk got 36 % of the words wrong (29 % with +18 dB). The 6.3 kHz low-pass in
voice_dsp.cpp is not it, so the treble goes between the ES7210 and the resampler. Owner's choice:
less compression (mu-law), a diagnostic to find the treble loss, the mic gain only after that.

Built (nothing flashed, installed or committed; no fw.sh, build_sim.sh, arduino-cli, esptool, ssh
or COM port):
- Serial `mic raw [ms]` (50-1500, default 1000; board_audio.cpp mic_raw_dump): the mic task copies
  its own I2S blocks, as read (44.1 kHz, both slots, no downmix, no filter), into a PSRAM buffer
  (about 176 KB a second) until it is full; the always-on sound check keeps every block meanwhile.
  Then, from the loop task: `MIC RAW I2S` (the I2S config exactly as speaker_microphone.cpp builds
  it: port, Philips standard, TDM off, rate, MCLK multiple and frequency, slot mode and mask, data and
  slot widths, WS width and polarity, bit shift), `MIC RAW ES7210` (01, 02 with adc_div / doubler /
  DLL, 03, 04/05, 06, 07 OSR, 08 master or slave, 11, 12, the HPF 20-23, 40, 41, the gains 43-46, the
  power 47-4C, the gain setting), the header `MIC RAW rate 44100 ch 2 bits 16 frames N gain G regs
  00=.. .. 4C=..`, data lines `<first frame> <hex>` (64 frames, L then R, int16 LE), `MIC RAW END`,
  and `MIC RAW STATS` (per slot peak, RMS, DC; the L/R correlation, flagged near -1 as an inverted
  slot or as a flat slot). codec_dev.cpp gains microphone_codec_read_reg (and the header declares
  microphone_codec_gain_get, which was there). Note from reading es7210.c: es7210_config_sample()
  returns at once in slave mode (ours), so 02 and 07 keep es7210_open's c1 / 20 (the table's
  11.2896 MHz / 44.1 kHz entry: adc_div 1, doubler, DLL, OSR 32) and 04/05 are never written. That
  looks right for MCLK = 256 fs; the dump will say what the chip really holds.
- tools/mic_raw_wav.py: the capture into a stereo WAV (`--split`: L and R alone too), with per-slot
  peak, RMS, DC, the L/R correlation and the share of energy in 0-500 Hz, 0.5-1, 1-2, 2-4, 4-8 and
  8-22 kHz for L, R and (L+R)/2. Standard library only. tests/test_voice_micraw.py checks it.
- Level boost (voice_dsp VoiceBoost, in VoiceCapture::feed after the resampler, before the encoder):
  kVoiceBoostDb 15 dB; linear below 15000 (-6.8 dBFS), then a tanh soft knee that never passes 30000
  (-0.8 dBFS, under mu-law's top code), so nothing clips. The level bar and the speech / early-stop
  rules still use the raw block power. Serial `voice boost N` (0-30 dB) until restart; `voice dump`
  says the boost and how many samples the limiter touched.
- mu-law by default (kVoiceDefaultCodec), ADPCM kept as codec 2; serial `voice codec 1|2` until
  restart (the controller's Store, PSRAM). kVoiceMaxChunks 128 -> 255 (BEGIN max_chunks, DATA idx, ACK
  base, END n_chunks are u8; the wire format is unchanged). kVoiceClipChunksMulaw 210 (3.0 s + 80 ms
  for the pre-roll lead and the last block, + 2; about 51 KB of PSRAM a clip), ADPCM stays 104.
  voice_decode now takes max_chunks 1-255, idx < 255, any base and n_chunks (voice_chunks_ok keeps
  -Wtype-limits quiet). voice_rec_init takes 1-255. The `voice` report ends with the codec.
- The air: a mu-law upload is about 68 DATA frames a second (ADPCM 34). kVoicePanelPerS 100 -> 140
  (burst 40) so upstairs relaying downstairs' upload (about 77 a second clean, about 85 with 10 % loss)
  stays under it. The mic task's 3 first sends a block (86 blocks a second) and the 32-chunk window
  (0.47 s of mu-law) allow it; measured in the house test with both panels' caps modelled: 68 frames
  in the worst second from the recording panel, 77 from upstairs on the relay route, the cap never hit
  on a clean link, 0 to 3 refusals in 180 lossy runs.
- The link under loss (found by the lossy house tests with mu-law; ADPCM's half-length stream rarely
  hit it): the C6 0.5 sends its gap or duplicate ACK only 80 ms after the LAST DATA it saw, and the
  panel's resends (each chunk on its own 200 ms clock) could trickle without such a gap, with the
  window full, until a chunk ran out of sends. voice_tx now resends in rounds (one pass over the owed
  chunks, oldest first, 4 a tick) with 160 ms of quiet after a round that sent anything
  (kVoiceResendQuietMs), probes when base is stuck 400 ms with nothing due (one chunk the bitmap shows
  received goes again, every 300 ms: the C6 answers a duplicate with where it is), and allows 6 sends
  a chunk (was 5). Totals stay capped at 3 x the chunks.
- Live meter on the badge row before START: Actions::voice_meter() from the row's render while it is
  ready; the controller reads Platform::mic_meter (board::audio_mic_recent: the mic task's own block
  peaks of the last 200 ms, from mic_window; no capture, no new task) every 200 ms while the calls
  keep coming, maps -54..-18 dBFS to 0-100 (voice_meter_level) into View::voice.level, and drops it a
  second after the calls stop. While capturing, the recording's own level as before.
- C6 side (shared code, one fix made here because the constant would otherwise overrun it):
  gw::VoiceSession's bitmap was 4 words (128 bits); it is now sized from kVoiceMaxChunks (8 words).

RAM (the xtensa compile method, ramtrim/xc.sh, not linked): board_audio.o bss +13 bytes (g_raw,
g_raw_cap, g_raw_fill, g_raw_on), nothing else in internal RAM: sizeof(Controller) still 7,308 (the
codec and meter fields are in its PSRAM Store), VoiceTx 1,528 bytes (in the Store, PSRAM; its two
per-chunk arrays grew by 635 bytes), VoiceCapture 2,188 (PSRAM). "Global variables" about 117,901
(limit 121,000). New PSRAM: a mu-law clip 51 KB while recording (ADPCM 25 KB); `mic raw` 176 KB a
second only while it runs. Stack: mic_raw_dump about 1 KB on the loop task; the mic task's copy is
a memcpy. No new task.

Tests (g++ C++17 -Wall -Wextra -Werror into scratchpad/aq): test_core 9,023 passed, 0 failed;
test_gateway 1,364 passed, 0 failed; test_voice 114 tests, 182,462 passed, 0 failed; tests/test_voice_*.py
11 OK; tools/voice_vectors.py --check 8 files, 0 differ. Every changed panel source cross-compiles
with the real toolchain (tcomm.ino, controller, board_audio, board_radio, device_platform, codec_dev,
ui_voice, voice_dsp, voice_proto, voice_tx). WeatherGate's Python tests not run (nothing there
changed in this piece). New and changed tests:
- test_voice_proto: the 128 boundaries moved to 255 (BEGIN 255 decodes, DATA idx 255 refused, 254
  taken, ACK base and END n_chunks 255 taken); voice_rec_block_mulaw_255 (210 and 255 chunk blocks,
  u8 counters at the top, the window rule at 254, frames at the top of the index space).
- test_voice_dsp: dsp_boost_gain_and_soft_limit, dsp_capture_boost_through_mulaw (a -42 dBFS tone
  comes out +15 dB through capture and mu-law, a near-full-scale one never reaches mu-law's top code;
  a 3 s mu-law clip is 202-210 chunks, pred and step 0), dsp_meter_level_mapping. The 1 kHz
  pipeline test runs with the boost at 0 dB.
- test_voice_link: FakeMic follows the clip's codec (mu-law chunks pred 0 / step 0, 237 samples) and
  stops Full at max_chunks; Panel models the air cap (first sends retried 3 times like the mic task,
  relayed frames through upstairs' cap) and mic_meter; RefC6 now sends the duplicate ACK as the real
  C6 does, plus a gap-fill ACK (switch gap_fill_ack, see the note below). New:
  gw_mulaw_full_clip_end_to_end (each floor and the relay route through piece D's real gateway, caps
  on: 202 chunks to the Pi once each, BEGIN codec 1 / 237 / 210, 3.75 s to the words, cap never hit,
  under 140 a second), gw_mulaw_lossy_capped (36 runs at 10 % loss, or 8 % a hop on the relay, with
  duplicates and reordering, caps on: all complete), gw_mulaw_index_space_255 (the C6 takes a
  255-chunk session out of order with duplicates: state 2 at base 255, 255 lines),
  tx_resend_rounds_leave_quiet, house_codec_setting, house_badge_meter_before_start.
  Against the real C6 0.5 with 60 seeds (180 runs, scratch only): mu-law 179 of 180, ADPCM 180 of 180.

For the C6 piece (not changed here, gateway.cpp):
- The C6 must be rebuilt with this voice_proto.h BEFORE the panels record in mu-law: a C6 0.5 as
  flashed refuses a BEGIN with max_chunks 210 (its voice_decode still says 128), so the panel shows
  "Gateway needs an update". Until then, `voice codec 2` on each panel keeps ADPCM (104 chunks).
- Suggested: in voice_data_frame, set ack_at only when no ACK is already due (`if (!s.ack_due)
  s.ack_at = now + 80`), so a run of duplicates cannot push the ACK back for ever; and count a chunk
  that fills a gap (idx below max_seen) like a duplicate, so the panel hears at once that base moved.
  RefC6's gap_fill_ack models the second; with both the 180-run figure above should reach 180.
- Throughput to the Pi: 68 voice lines a second (about 26 KB/s of serial) instead of 34; the write
  gate (tx_room, kVoiceDataReserve) and the Pi bridge should be checked at that rate.
For the Pi piece: the stt service and bridge must take codec 1 (mu-law, cs 237) and chunk numbers up
to 254 (209 in practice), and max_chunks up to 255 in voice_begin.

For the owner:
- Order: C6 first (or `voice codec 2` on both panels until it is done), then both panels.
- Then, at one foot, `mic raw` and paste the lines into a file: `python tools/mic_raw_wav.py
  capture.txt raw.wav --split` says which slot carries what and how much is above 1 kHz before any of
  our processing. The mic gain stays as it is until that answer is in.
- Long-press the badge: the level bar now moves before START.

## 2026-11-21 Field fix 3 (C6 piece): gateway-c6 0.6, mu-law and 255 chunks

Built (nothing flashed or committed; no fw.sh, build_sim.sh, arduino-cli, esptool, ssh or COM port):
- kFirmware "gateway-c6 0.6" (the hello and the boot line say it).
- Sessions of up to 255 chunks, codec 1 with 237-sample chunks: the bitmap in voice_session was already
  sized from kVoiceMaxChunks (8 words) by the panel piece; every other bound was checked and holds
  (has/mark/base/bits/gap/all_received/received to max_chunks, base and n_chunks u8 up to 255, idx
  below max_chunks, END n_chunks <= max_chunks, the 32-chunk bitmap window). The DATA line is the same
  size for both codecs (237 bytes, 316 base64 characters): 376 to 378 bytes for mu-law (pred 0, step 0),
  under the 512 limit; the 600 bytes kept free for control lines (kVoiceCtlReserve) and the 1100 for
  chunk lines (kVoiceDataReserve) are unchanged. The stream is 68 lines a second (27 KB/s); the write
  gate was exercised at 20, 30 and 60 bytes a millisecond into the 4 KB buffer.
- ACKs: the panel piece's two suggestions, in a form that keeps the 80 ms rule. A chunk that fills a gap
  (idx below the highest seen) sets dup like a duplicate, so the panel hears at once that base moved;
  ack_at is still 80 ms after the last DATA but never later than kVoiceAckMaxMs (300) after the first
  DATA since the last ACK (VoiceSession::ack_first), so a trickle of resends cannot hold the ACK back;
  a due ACK with nothing to report is dropped, so a stale due time cannot fire early later.
- The C6's air cap kVoiceGwPerS 50 -> 100 (burst stays 25; core/voice_proto.h, the one shared line
  changed). The C6 sends only ACKs (one per 8 new chunks, one 80 ms after a gap or duplicate), GOTs,
  TEXT and DISMISS: about 9 a second per streaming floor in the tests (27 ACKs for a 203-chunk clip,
  39 to 84 with a slow port and resends), so 50 was not hit either, and 100 leaves headroom. 68 chunks a
  second inbound was never throttled (the cap is on the C6's own frames only).
- The receive queue in gateway-c6.ino 32 -> 48 frames (+4 KB heap, the C6 has room): 68 frames a second,
  77 with a relayed floor, in a loop that polls every millisecond.
- ../gateway/docs/gateway-c6.md updated (sessions, ACK schedule, line sizes, queue and cap, hello).

Tests (g++ C++17 -Wall -Wextra -Werror into scratchpad/aq): test_gateway 6,507 passed, 0 failed (was
1,364); test_voice 116 tests, 182,564 passed, 0 failed (was 114 and 182,462). New in test_gateway:
test_voice_mulaw_message (a 203-chunk mu-law message from upstairs, from downstairs and from both at
once at 68 a second, through a port that keeps up and through ones slower than the stream, with a
panel model that honours the window and the ACK bitmap and resends; every chunk once and whole, base64
round trip, pred 0 and step 0, voice_end n 203 ns 48111, longest line 378, notices and Lucy replies
whole and complete, nothing capped; the cap at 100 a second with bursts of 25), test_voice_index_space_255
(255 chunks out of order with duplicates, state 2 at base 255, idx 255 refused), test_voice_ack_pace
(gap-fill ACK, the 300 ms bound, the dropped due flag). New in test_voice_link:
gw06_mulaw_message_with_outside_and_lucy (the real gateway, each floor, relayed and both at once, with
OUTSIDE and LUCY every 350 ms: all answered, chunks once each, 202 per floor, C6 capped 0, serial held
11 chunks a floor for the waiting requests) and gw06_mulaw_lossy_with_outside_and_lucy (18 runs at 10 %
loss, 5 % duplicates, reordering: all complete, every request got exactly one final reply).
The gateway sketch itself (gateway-c6.ino) was not compiled (no arduino-cli): its change is one number.

For the owner: flash the C6 (gateway-remote) before the panels record in mu-law, or keep `voice codec 2`
on both panels until it is on. The Pi side must take codec 1, cs 237, and chunk numbers up to 254.

## 2026-11-21 Field fix 3: whole-tree verification (panel, C6 0.6, Pi)

Checked (nothing flashed, installed, deployed or committed; no fw.sh, build_sim.sh, arduino-cli,
esptool, ssh or COM port):
- Host tests (g++ C++17 -Wall -Wextra -Werror into scratchpad/aq): test_core 9,023 passed, 0 failed;
  test_gateway 6,507 passed, 0 failed; test_voice 117 tests, 182,595 passed, 0 failed (one test added,
  below). tests/test_voice_*.py: 11 OK.
- WeatherGate (WSL): test_stt 66 OK; test_tcomm_bridge 95/95 (test_voice.py 79 OK); test_gateway 52/52;
  test_lucy_gate 54/54; test_phone_bell 353/353; test_watchdog 18/18; test_house_alerts 91/92, the one
  failure being "HAP-python available for this test" (not installed in WSL; those files are unchanged
  since a155c46).
- RAM: every panel source (47 objects) cross-compiled with scratchpad/ramtrim/xc.sh and compared
  section by section (.data, .bss, .dram, .noinit; .ext_ram left out) with ramtrim/obj_fix, the build
  the 117,888 figure came from: the only change is board_audio.o +13 bytes. "Global variables" about
  117,901, under the 121,000 limit.
- Line endings: every file changed against git HEAD in both repos keeps HEAD's endings; the new files
  are LF, as their neighbours.
- No addresses, logins or new personal topics in the tcomm changes ("Mike" in the dated decision notes
  as elsewhere in the repo).

Interop:
- New panels with the C6 still on 0.5: added fieldfix3_mulaw_panel_with_c6_05 (RefC6 old05 drops a
  BEGIN with max_chunks over 128, as 0.5's voice_decode does). Each press, either floor, twice: "Gateway
  needs an update" at 1.5 s, 47 voice frames upstairs and 57 downstairs (both routes) for the whole
  press, 46 of them to the C6 (BEGINs, the window's 32 first sends, 3 ABORTs), then nothing but the
  hello and pings; `voice codec 2` (ADPCM, 104 chunks) still works through it. Fails safe.
- New panels with C6 0.6: the gw06 house tests (the real gateway, each floor, relayed and both at once,
  with OUTSIDE and LUCY, lossy): all complete, nothing capped.
- An old stt (MAX_CHUNKS 128) with a mu-law clip (scratch run of the real stt_service with the limit
  put back): BEGIN taken, chunks 0-127 kept, chunks 128-202 and the END dropped as "bad voice lines"
  (logged at most once a minute), no text posted, the job dropped 2 s later (idle_drop), or at once by
  the panel's ABORT. The panel says "The Pi didn't answer" after its 8 s text wait; a second try within
  2 s is told Queued and waits up to 20 s for the same answer. Fails safe, slowly. The bridge has no
  chunk or codec limits (a mu-law line is about 420 bytes against VOICE_LINE_MAX 1024).

Fixed: docs/voice-notes/DESIGN.md 3.1 still called codec 1 the fallback; it now says it is the default
since 2026-11-21.

Rollout order:
1. Pi: stt_service.py (MAX_CHUNKS 255) installed and restarted, after its backup and Mike's yes. The
   bridge needs no change.
2. C6 0.6 at the laptop (it is on the laptop, not the Pi, until the Pi's flashing is ready): check the
   boot line says "gateway-c6 0.6".
3. Both panels (Settings > Update over Wi-Fi > BOTH PANELS). Until step 2 is done, `voice codec 2` on
   each panel keeps ADPCM. Then `mic raw` at one foot for the treble question; the mic gain waits for
   that answer.

## 2026-11-21 Owner decisions: taller keypad, full-keypad recording, badge row icons, bolder arrival, codec by capability

Mike's five decisions of 2026-11-21, built in tcomm and ../gateway/gateway-c6 (nothing flashed,
installed or committed; no fw.sh, build_sim.sh, arduino-cli, esptool, ssh or COM port).

1. Taller keypad, layout B (as mocked up in shots k12, k14, k15, k22, k23, k26, k28). ui_internal.h
   holds the geometry once: kKeyH 58, kKeyGap 5, kKpShown 264, kKpTop 216 (was 40 px keys at 288).
   The calendar, the tile message editor and the voice edit card all build the keypad at kKpTop. The
   calendar editor: dialog top at screen 64, a 30 px header, 34 px fields at 38/76/114; the repeat
   button is the icon only (26 px, 52 px wide) at the right end of the DATE/TIME/LENGTH row, filled
   green with a cream icon when the event repeats; the rule's words ("Weekly, until Dec 31") are now
   the repeat menu's subtitle, with "(saved with the event)". Delete sits under the date row for an
   existing event only; while typing the dialog ends at screen 216, so Delete is out of sight; Done
   brings it back. The tile message editor: SEND and CANCEL 60 px high, SHOW FOR left of the chips on
   one row at 138, the card 200 px high.
2. The keyboard's voice strip: while it records it covers all four rows of keys (784 x 247, "Speak
   now", the digit, a 456 px drain bar, the microphone level, STOP 184 x 124 over CANCEL 184 x 80);
   when the recording ends (3 s or STOP) the keys show again, with only the top row covered by the
   "Turning speech into text" strip (66 px) until the words go in; then no strip at all.
3. The badge row: the microphone in START was 6 px right of centre because LVGL moves every child in
   by its parent's border width, and the button's 6 px ledge is a (bottom-only) border. The microphone
   and the stop square are now aligned (LV_ALIGN_TOP_MID), so they stay centred pressed or not;
   measured centred to the pixel. The keyboard's blue mic key was already centred (its keys have no
   border; checked). The word KEYBOARD is now a keyboard drawn from boxes (64 x 32: a rounded outline,
   two rows of keys, the space bar) in the ink colour, so it reads by day and at night.
4. The arrival card (ui_voice.cpp): a light card by day and at night (#D7CBB0 with #14201A ink at
   night), a 26 px header strip in the sender tile's status colour with a speech bubble and "MESSAGE
   FROM UPSTAIRS  9:42 PM", a "+N" pill when more wait, the message in the biggest font that holds it
   in two lines (B26 down to B16; B14 only past that), centred in its room, and "Tap to dismiss". Same
   place as before: tile y 90-193, so the top 90 px (mail badges, request arrows) stay tappable; the
   700 ms guard and the doorbell banner above it are kept. The pulse: four 5 px gold bars on the card's
   straight edges, on and off every 400 ms for 3.2 s, then off. Measured with partial refresh: the
   pulse adds about 82,000 px over its 2.8 s (7 changes), at most 25,000 px in one refresh (the
   harness's own idle redraw included); no whole-screen redraws. The dialog's state moved to PSRAM
   (ui_keep), like the strip.
5. Codec by capability. C6 0.6 now sends its voice hello with key 1 (tc::kVoiceHelloKeyMulaw: takes
   mu-law and 255 chunks); 0.5 sent key 0. The panel records which it last heard; upstairs passes it
   to downstairs in its STATE as kStateGwMulaw (0x40), the same way as kStateGwVoice; the hellos
   stopping for 10 minutes clears both. A panel records mu-law (codec 1, 210 chunks) only when the C6
   has said it takes it, else ADPCM (codec 2, 104 chunks). Serial `voice codec 1|2` still overrides
   until restart, and `voice codec 0` gives it back to the gateway; `voice` reports "gateway takes
   voice (...), mu-law too | ADPCM only; codec N ... [by hand]". Downstairs' ping nonce is never 0 or
   1, so it can never be mistaken for a hello. The new state lives in the controller's PSRAM store.
   The gateway's version stays 0.6.

Tests (g++ C++17 -Wall -Wextra -Werror, scratchpad/aq/t.sh): test_core 9,023 passed, 0 failed;
test_gateway 6,507 passed, 0 failed (the hello test now expects key 1; its hello filter decodes the
frame); test_voice 118 tests, 182,617 passed, 0 failed. The reference C6 model sends key 1, or key 0
with old05. fieldfix3_mulaw_panel_with_c6_05 is rewritten: a new panel with a C6 0.5 now WORKS, both
floors, two presses each, in ADPCM (BEGIN codec 2, 104 chunks; words in 3.75 s); mu-law set by hand
still fails safe ("Gateway needs an update" in 1.5 s, 45 frames to the C6) and `voice codec 0` works
again. New codec_follows_the_c6_hello: C6 0.5 -> 0.6 -> 0.5 -> 0.4 while running, both panels follow
at the next hello (downstairs through upstairs' STATE), each change logged.

Screen checks (scratchpad harnesses against the real tree): badge2/sim 93 passed (the KEYBOARD label
checks now look for the drawn keyboard; kMicY 442; keys at y >= 216); arrival 5 passed; keys2
b_real.sh (no KP_V, the real tree) renders every keypad use; fin/sim 47 passed (keypad at 216, 58 px
keys, the dialog and Delete while typing and after Done, the repeat words in the menu, the strip
covering all four rows then leaving, the words inserted, START's microphone and the mic key centred,
the keyboard icon centred, the arrival card's place, header, +1, font, pulse timing, guard, banner
above it, dismissal).

Shots: f01_badge_ready_day, f02_badge_result_day, f03_arrival_down_day, f04_arrival_up_night,
f05_calendar_typing, f06_dictation_full, f07_message_editor.

RAM: all 47 panel objects cross-compiled with scratchpad/ramtrim/xc.sh before and after and compared
section by section (.data, .bss, .dram, .noinit; .ext_ram left out): ui_calendar.o -4 bytes (the
repeat label pointer), ui_voice.o -28 (the dialog's state to PSRAM), nothing else; "Global variables"
about 117,869 (117,901 before), under the 121,000 limit. No new tasks.

Line endings kept (CRLF stays CRLF). For the owner: the C6 must be flashed with this 0.6 for the panels
to use mu-law; until then they record ADPCM by themselves (no `voice codec 2` needed any more).

### 2026-11-21: review of the owner-decisions build

- Arrival card: "Tap to dismiss" was cut off at the card's bottom edge, and the pulse's bottom gold bar
  covered it while lit (f03, f04). The message room is now 52 px (two B26 lines) and the line sits at
  card y 80, ending at 97, clear of the 5 px bar. f01-f07 re-rendered from the real tree.
- Calendar editor: a day/night switch while typing rebuilt the dialog at full height (Delete under the
  keyboard). cal_render now sets the typing height again after the rebuild.
- Rechecked: test_core 9,023, test_gateway 6,507, test_voice 182,617 passed, 0 failed; badge2/sim 93,
  arrival 5, fin/sim 47 passed; keys2 b_real.sh rendered. ui_calendar.o and ui_voice.o internal RAM
  unchanged (13,914 and 36 bytes), so "Global variables" stays about 117,869. Line endings unchanged.

### 2026-11-21: treble restore in the voice path

- Field finding: the panel microphones deliver speech with 15-20 dB too little energy above 2 kHz
  (measured on the raw I2S data, so it is acoustic, before our code). Offline, 10 real clips through
  Vosk on the Pi: a +10 dB RBJ high shelf at 1500 Hz (S = 1, at 16 kHz) ahead of the +15 dB boost cut
  word errors from 29% to 20% (+18 dB gave the same, so +10).
- voice_dsp.h/.cpp: new VoiceTreble (one float biquad, coefficients from kVoiceTrebleHz = 1500 and
  kVoiceTrebleDb = 10, computed in set_db; 0 dB = bypass, bit for bit; max 20). VoiceCapture::feed runs
  it right after the resampler and before VoiceBoost; start() reloads the dB and clears the filter
  state each recording. The state is two floats plus coefficients inside the VoiceCapture (PSRAM):
  no static arrays, so the internal-RAM figure (117,872) is unchanged by design (not cross-compiled).
- Serial `voice treble N` (0-20, until restart), next to `voice boost N`: tcomm.ino, board.h,
  board_audio.cpp (voice_set_treble / voice_treble).
- Tests: shelf gain (about 0 dB at 200 Hz, +10 dB within 1 dB at 6 kHz, 3-7 dB at the corner, clamps),
  bypass bit-exact at 0, a loud two-tone clip through the real capture and mu-law stays under the top
  code. Two older tests set the treble to 0 (their figures are the boost alone / the pipeline alone).
- test_core 9,023, test_gateway 6,507, test_voice 121 tests, 182,629 passed (182,617 before), 0 failed.
  Not flashed. Line endings kept per file (voice_dsp.*, test, board_audio.cpp, WORKLOG LF;
  tcomm.ino, board.h CRLF).
