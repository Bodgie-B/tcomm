# The ESP32-C6 gateway

The Waveshare ESP32-C6-GEEK sits on the Pi's USB port. The Pi's bridge (WeatherGate and LucyGate)
sends it one JSON line per request; the gateway turns each into a TComm OUTSIDE or LUCY frame,
sends it to the upstairs panel over ESP-NOW, retries until the panel ACKs or 1.5 s pass, and
answers the Pi once per request. The radio side follows `gateway/docs/gateway-brief.md`; the serial lines
follow the bridge's spec from the WeatherGate project.

Written 2026-10-25. The logic is tested on the PC. The first flash went onto Mike's C6 the same
evening (COM6): it printed its boot line and answered hello, and reopening the port from Windows
didn't restart it. It hasn't talked to a panel yet. Everything marked UNVERIFIED needs checking on
the hardware.

| Path | What it is |
|---|---|
| `gateway/gateway-c6/gateway-c6.ino` | The board: USB serial port, radio, screen |
| `gateway/gateway-c6/src/gw/` | The logic (requests, checks, retries, ACKs, replies), the voice session state (`voice_session.*`, from 0.5) and a small JSON reader. Plain C++. |
| `gateway/gateway-c6/src/lcd/` | The 1.14 inch ST7789 screen, with a built-in 5 x 7 font. No display library. |
| `gateway/gateway-c6/src/shared/` | Four one-line files that compile the panels' own `core/protocol.cpp`, `events.cpp`, `civil.cpp` and (from 0.5) `voice_proto.cpp` into the gateway. Nothing is copied. |
| `gateway/gateway-c6/listener/` | A stand-in for the upstairs panel, for testing before the panels' ESP-NOW is switched on |
| `gateway/tests/test_gateway.cpp` | 6,552 host checks (156 before 0.5, 1,364 at the start of 0.6, 6,507 at the start of 0.7), run by `bash firmware/tools/build_sim.sh` |

The host tests cover the brief's two OUTSIDE and two LUCY frames byte for byte, the ACK examples,
ACKs from an unknown MAC or with the wrong sender byte (ignored), the retry times (0, 150, 400,
750, 1150 ms, failed at 1.5 s), a newer request superseding one in flight, the two types retrying
independently, exactly one final reply per id, every field check, and (since 2026-11-07) the UPDATE
line: its frame byte for byte, the busy answer, its ACK, its give-up and its bad ids; and (from 0.4) the answer and mail_ack lines, the ring notice and the unsigned knock id.

Capacity, built with the real secrets file: the gateway uses 968,605 bytes of the C6's
1,310,720-byte app space (73%, nearly all of it the Wi-Fi stack) and 45,700 bytes of static RAM
out of 327,680 (13%). The listener uses 933,734
bytes on the panel.

## Software

Nothing new to install. The build uses what TComm already has:

- the Arduino IDE (its built-in arduino-cli does the compiling and uploading)
- TComm's own Arduino folder, `%LOCALAPPDATA%\tcomm\a15`, which already has the ESP32-C6 parts
  (core 3.2.0 and the RISC-V compiler)
- Git Bash, which came with Git for Windows (Claude Code uses it too)
- Windows' own USB serial driver; the C6's USB port is the chip's built-in USB Serial/JTAG

Build from Git Bash only. On 2026-10-25 Bitdefender's Advanced Threat Defense blocked a compile
started from PowerShell (powershell.exe running arduino-cli.exe) and quarantined the gateway's
first build script. Started from bash, the same compiles have gone through. So the gateway and
listener builds live in `firmware/tools/fw.sh` with the panel's.

## Flashing the gateway, one command at a time

Or tell Claude "flash the gateway on COM7" (with the right port).

1. Make the secrets file first (next section). `fw.sh` won't upload the gateway without it.

2. Plug the C6-GEEK into the laptop with a USB-C data cable.

3. Find its port in Device Manager, under Ports (COM & LPT). It shows as "USB Serial Device
   (COMn)", the same name the panel has, so unplug the panel first if you're unsure which is which.
   Mike's C6 came up as COM6 (2026-10-25).

4. Open Git Bash in the TComm folder (in File Explorer: right-click the folder, Show more
   options, Open Git Bash here).

5. Compile and upload, with the port from step 3:

   ```
   bash firmware/tools/fw.sh gateway COM7
   ```

   The first build takes about two minutes. It should end with a `BASE MAC:` line, "4 of 4 parts
   written and verified" and "upload OK". The upload writes the bootloader, partition table and
   app; it never erases the whole chip.

6. If the upload can't connect: hold the BOOT button, unplug and replug the USB cable, let go of
   BOOT, and run step 5 again. Then unplug and replug it once more to start the new firmware.

7. Check the gateway's MAC on its screen (second line) against `TCOMM_MAC_GATEWAY`. If they
   differ, the bottom line of the screen is yellow: put the screen's MAC in the secrets file and
   run step 5 again. The C6's Wi-Fi MAC is esptool's `BASE MAC` (6 bytes), not its longer `MAC`
   line, which is the chip's 8-byte Zigbee/Thread address. Mike's C6 reads BASE MAC
   02:00:00:00:00:03 (esptool read_mac, 2026-10-25).

To compile without uploading, leave the port off: `bash firmware/tools/fw.sh gateway`.

## Flashing the gateway on the Pi (Mike, 2026-11-07)

The gateway now stays plugged into the Pi, so the USB steps above are only for a C6 that's off the
Pi. From the laptop:

    bash firmware/tools/fw.sh gateway-remote

builds the gateway, copies its app image and the image's SHA-256 to the Pi with scp, and runs the
Pi's flash command over ssh, all through Mike's untracked `firmware/tools/gateway_flash.local.sh`. The Pi
checks the image and the chip's MAC, saves what's in app0, writes app0 only (never the bootloader,
the partition table or NVS) and prints the gateway's hello reply. `fw.sh` checks that the reply's
`fw` matches `kFirmware` in `gateway.h`, so bump that with every gateway change (0.2 is the first
with the update line, 0.3 the first with the notices, 0.4 the first with the phone answer and mail ack lines and the ring notice, 0.7 the first with the rooms line). The Pi's side in full, as asked of the WeatherGate session:
`firmware/docs/weathergate-remote-brief.md`.

## The secrets file

It's `firmware/tcomm/src/config/secrets.h`, the same file the panels use, so the panels, the
gateway and the listener are always built with the same keys. Copy `secrets.example.h` in the
same folder to `secrets.h` and fill it in. It's in `.gitignore`, so it never gets committed. The
gateway never prints the keys or shows them on its screen.

```
#define TCOMM_MAC_UPSTAIRS   {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}
#define TCOMM_MAC_DOWNSTAIRS {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define TCOMM_MAC_GATEWAY    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

#define TCOMM_PMK         "................"
#define TCOMM_LMK_PANELS  "................"
#define TCOMM_LMK_GATEWAY "................"
```

`TCOMM_MAC_UPSTAIRS` is the panel the gateway sends to. The line above assumes the first panel
(02:00:00:00:00:01) goes upstairs; the panel works out its floor from this table too.
`TCOMM_MAC_DOWNSTAIRS` stays all zeros until the second panel arrives, and the gateway skips a zero
MAC. `TCOMM_MAC_GATEWAY` is the C6's own Wi-Fi MAC, for example `{0x02, 0x00, 0x00, 0x00, 0x00,
0x03}` (step 7 above confirms it). The gateway runs without it, but the panels and the listener
ignore anything from a MAC that isn't in this table.

The three keys are exactly 16 characters each; the build stops if one isn't. `TCOMM_PMK` is
ESP-NOW's master key. `TCOMM_LMK_GATEWAY` is the link key between the gateway and each panel, and
`TCOMM_LMK_PANELS` is the panels' link to each other (the gateway doesn't use it). To make a key
without it appearing anywhere but your own screen, run this in Git Bash (OpenSSL comes with Git
for Windows) and paste the result between the quotes:

```
openssl rand -base64 12
```

Each run gives a new 16-character key. Don't send them in chat.

## What the screen shows

**The screen starts off (2026-11-15).** The gateway hangs facing the floor on the Pi and nothing
reads it, and the Pi's 5 V supply is marginal, so at every power-up and reset the backlight is held
off, the screen's controller is held in reset and nothing is drawn. The button on the board's side
(BOOT, GPIO 9) turns the screen on; pressing it again turns it off, and it goes off by itself after
ten minutes. Nothing is saved, so a restart is always dark. Lighting it takes about 0.4 s, during
which the gateway reads no serial line (the port's buffer holds what arrives). Everything the
screen shows is also in the serial lines: the MAC and firmware version in the hello reply, and a
wrong MAC or a missing secrets file as a note at start-up. Holding the button while plugging the
board in still starts the chip's loader, as it always did.

Seven lines, all in the same 12 x 14 pixel text (the first build's smaller lines were too small
to read). From the top: TCOMM GATEWAY; the MAC; the channel and firmware version; PI: WAITING
until the first line from the Pi, then PI: TALKING; how many requests were ACKed and how many
failed (and BAD with the number rejected, once there are any); the last result, NO REQUESTS YET
until the first one (green when ACKed, red when failed, yellow when rejected). The bottom line is
red for a problem (no secrets file, the upstairs MAC not set, a radio start-up error) or yellow
when `TCOMM_MAC_GATEWAY` doesn't match this board.

The screen's pins (SCLK 1, MOSI 2, DC 3, RST 4, CS 5, backlight 6) came from Waveshare's demo
code as quoted on the Home Assistant forum, not from the wiki. On 2026-10-25 Mike read every line
off the first flash, so the pins, the picture's offset (40, 53) and its orientation are right.
UNVERIFIED: the colours (the colour inversion and order). They're the constants at the top of
`src/lcd/lcd.cpp`.

## The serial protocol as built

The message formats are exactly the bridge's spec: no changes. These are the details the bridge
should know about.

A line over 512 bytes gets `{"error":"bad_json"}` and is dropped. Blank lines are ignored, and so
is every CR, so CRLF line endings work. A line that is JSON but has neither `"cmd":"hello"` nor a
known `type` gets `{"error":"unknown_type"}`; that includes any other `cmd`.

Every request needs an `id`, a positive whole number (up to 2^53 - 1). A request without a usable
id is rejected without one, since there's nothing to answer to:
`{"type":"lucy","rejected":"id must be a positive whole number"}`.

Whole-number fields must be JSON integers. `1.0` and `true` are rejected, and Python's
`json.dumps(True)` writes `true`, so send `int` values for `out`, `flags`, `forecast` and the
rest.

Two checks go a little past the spec: `flags` must be 0 to 7 (1 valid, 2 danger heat, 4 danger
cold), and `phrase` must be present (an empty string is fine). The rejections read
`"flags must be 0 to 7"` and `"phrase must be a string"`. Fields the gateway doesn't use are
ignored.

A `superseded` reply carries the seq of the request that gave way. The `ms` in an `acked` reply
counts from the first try to the ACK.

### The update line (Mike, 2026-11-07)

A third request type asks the gateway to start both panels' Wi-Fi updaters with no touch on either
panel. The whole chain is in `firmware/docs/remote-update.md`. The line is

    {"type":"update","id":21}

and needs only the `id` (any other fields are ignored). The gateway sends an UPDATE frame (type 15,
one reserved byte, always 0) to the upstairs panel with the usual retries (0, 150, 400, 750 and
1150 ms, giving up at 1.5 s). The upstairs panel passes it to downstairs and starts both updaters,
the same as Settings > Update over Wi-Fi > BOTH PANELS. The answers, in the same style as the other
types:

- `{"type":"update","id":21,"acked":"upstairs","seq":8192,"ms":57}`: upstairs took it and is
  starting the updaters (they appear a few seconds later, after the Wi-Fi join).
- `{"type":"update","id":21,"failed":"upstairs","seq":8192}`: no ACK after the retries, nothing
  started. The same `gw:` note as for the other types follows, saying whether a radio took the tries.
- `{"type":"update","id":22,"busy":true,"seq":8192}`: an update with that seq is still waiting for
  its ACK. Nothing new was sent; the first request keeps its own final reply (acked or failed).
- `{"type":"update","rejected":"id must be a positive whole number"}` (with the `id` when it was
  usable): the same rejection the other types give.

An update never replaces or delays an OUTSIDE or LUCY in flight, and `superseded` is never used
for it. The screen's last-result row reads `update acked 57ms` or `update failed`. A second
request after the first has been answered is a new update with a new seq.

A reset loses whatever was in flight, with no final reply for it. When `{"boot":true,...}` arrives,
treat any request still waiting as failed, and send the Lucy press again.

While the port is closed (the bridge restarting), the C6 keeps the replies it writes in a 2 KB (4 KB from 0.5)
buffer, and they come out when the port opens again. Ignore replies for ids this run of the bridge
didn't send, and send `{"cmd":"hello"}` after opening.

Leave DTR and RTS alone. The chip's USB port resets the C6 when RTS is set while DTR is clear
(that's how esptool resets it); opening the port sets both together, which doesn't. pyserial's
defaults are fine, but don't set `ser.dtr = False`. UNVERIFIED until the hardware check below.

Anything that isn't a reply starts with something other than `{`: the chip's own start-up text
after a reset (`ESP-ROM:...`), system errors (`E (...`), and the gateway's few notes (`gw: ...`,
start-up problems, and radio send errors at most once a minute). Writes give up after 20 ms when
nothing is reading the port.

## Notices from the panels (Mike, 2026-11-11)

From `gateway-c6 0.3` the link is no longer one-way. The upstairs panel tells the gateway about
things that happen in the house (a ring nobody answered, an urgent request for heat or cooling, a
dog out too long, and four chattier kinds that are off to begin with), and the gateway prints each
one as a line the Pi can turn into a phone notification or a WhatsApp message. The Pi's side is in
`firmware/docs/weathergate-events-brief.md`.

On the radio it is a NOTICE frame (type 17, layout in `core/protocol.h`), sent by the upstairs panel
only, to the gateway, with the usual five tries over 1.5 s. The gateway ACKs it with the same ACK
frame a panel would send, whenever it arrives, a repeat included (the first ACK may be what was
lost). Only a NOTICE from the upstairs panel's MAC is taken: any other MAC, the downstairs panel,
or a frame whose sender byte does not match its MAC is ignored, with no ACK. A frame the gateway
can't read (a kind outside 1 to 8, a floor over 1, text over 40 bytes) is ignored with no ACK.
Retries of one NOTICE are told apart by the frame's seq (kept for 10 s, as the panels do), so each
event prints once. If the gateway doesn't answer, the panel drops the notice after those tries and
shows nothing: it is never "No signal", and it never holds up what the two panels send each other.

The line is unsolicited (no `id`, nothing to reply to), one per event:

    {"event":"notice","kind":"knock","floor":"downstairs","value":0,"time":1793000000,"text":""}

| Field | Meaning |
|---|---|
| `event` | always `"notice"` |
| `kind` | `knock`, `arrow_urgent`, `arrow_mild`, `mail`, `dog_out`, `status`, `note` or `ring` (the numbers 1 to 8 on the radio, in that order) |
| `floor` | `"upstairs"` or `"downstairs"`: who the event is about (see below) |
| `value` | a whole number, signed (see below); for `ring` the knock id, unsigned, 1 to 65535 |
| `time` | UTC seconds since 1970 of the event itself, or 0 when the panel had no clock |
| `text` | up to 40 printable ASCII characters; `"` and `\` are escaped as in JSON; may be empty |

| `kind` | When the upstairs panel raises it | `floor` | `value` | `text` |
|---|---|---|---|---|
| `knock` | a ring went unanswered for its ring time (a minute), or rang while upstairs was Away | who rang | 0 unanswered, 1 rang during Away | empty |
| `arrow_urgent` | the tenant sent an urgent heat or cooling request | downstairs | +1 heat, -1 cooling | empty |
| `arrow_mild` | the tenant sent a mild heat or cooling request | downstairs | +1 heat, -1 cooling | empty |
| `mail` | mail was marked for downstairs (taking it back says nothing) | downstairs | 1 | empty |
| `mail` | "no mail" was marked (Mike, 2026-11-18: he went for the mail and there was none; taking it back says nothing) | downstairs | 3 | `NOMAIL` |
| `dog_out` | a dog passed its out-too-long limit (the same limit as the nudge on the tile) | upstairs (not meaningful) | minutes out | `LUCY`, `COPPER` or `BOTH` |
| `status` | a floor's status changed (a timer running out counts) | whose status | 0 | the word: `OPEN`, `BUSY`, `PRIVATE`, `SLEEPING` or `AWAY` |
| `note` | a floor's tile message was set (clearing it says nothing) | whose message | 0 | the message, cut at 40 characters |
| `ring` | see the phone section below (from 0.4) | the RUNG floor | the knock id, unsigned | `RING` or `ANSWERED` |

Each is raised once per occurrence, from state the upstairs panel already holds (what downstairs does
reaches upstairs through the normal sync). `time` is the event's own time where the panel knows it (the
arrow's sent time, the moment mail was marked, when the dog went out, when the message was written),
so a notice that is raised again after a restart carries the same `(kind, time)`; the Pi should drop
repeats by that pair. Which kinds are sent is a bitmask in the upstairs panel (bit 0 is kind 1):
`knock`, `arrow_urgent`, `arrow_mild`, `mail`, `dog_out`, `note` and `ring` are on to begin with
(0xDF; only `status` is off), and a Settings page for the rest is a follow-up. `mail` and `ring`
drive the phone pushes (Mike, 2026-11-15), so a panel whose saved mask predates them gains both once,
when its settings are carried forward; without `ring` no phone is ever pushed. `arrow_mild` and
`note` were added to the starting mask on 2026-11-18 (saved settings version 8) so the tenant's mild
temperature requests and notes reach Mike's phone; a panel on version 6 or 7 gains both bits once
(0x44), and a version 8 mask Mike has changed is never touched again. `note` is raised for either
floor's message, so the Pi must look at `floor` (downstairs is the tenant's). A line is at most about 190 bytes, well inside the 512-byte limit, and goes out in one
write like every other line.

While the Pi bridge isn't reading the port, these lines wait in the same 2 KB (4 KB from 0.5) buffer as the replies
and come out when the port opens (the Pi should ignore ones older than it cares about, by `time`).
Nothing here changes the request types, their replies, a request in flight or the screen.

## Phone answers and the ring notice (Mike, 2026-11-15)

From `gateway-c6 0.4` the phones can answer a ring and acknowledge the mail, through the Pi and
ntfy. Two more request types, in the same style as the others (one final reply per `id`, the usual
retries, sent to the upstairs panel; latest wins within `mail_ack`, and within `answer` only among
answers for the same rung floor):

    {"type":"answer","id":31,"floor":"downstairs","knock":43981,"come_on":1}
    {"type":"mail_ack","id":32}

- `answer`: `floor` is `"upstairs"` or `"downstairs"` and names the floor that was RUNG, `knock` is
  that ring's id (1 to 65535, from the `ring` notice's `value`) and `come_on` is 1 for Come on, 0
  for Not now. It goes out as a PHONE_ANSWER frame (type 18: floor u8, knock id u16, come_on u8).
  Upstairs applies it, or passes it to downstairs when the rung floor is downstairs, and the rung
  panel answers its banner as if tapped (the ringer sees "(from phone)").
- `mail_ack`: needs only the `id`. It goes out as a MAIL_ACK frame (type 19, one reserved byte,
  always 0), and upstairs clears the mail the way the downstairs panel's acknowledge tap does. Since
  2026-11-18 it clears whichever is showing, the mail badge or the "no mail" one (the `mail` notice
  with value 3 and text `NOMAIL`, same gate as a real mail notice: downstairs Open or Busy, upstairs
  clock valid, outside quiet hours). The same `mail_ack` is the phone's acknowledgement for both; the
  gateway needs no change, it prints the notice as it comes. The push words for the phones are the
  Pi's side (a `NOMAIL` notice is value 3 with that text; value 1 with empty text is real mail).

The replies are the same shapes as the other types:
`{"type":"answer","id":31,"acked":"upstairs","seq":16384,"ms":6}`, `"failed":"upstairs"`,
`"superseded":true` (an earlier `mail_ack`, or an earlier `answer` for the same rung floor, still
waiting for its ACK gave way) and
`{"type":"answer","id":3,"rejected":"knock must be 1 to 65535"}`. The rejection reasons are
`floor must be upstairs or downstairs`, `knock must be 1 to 65535`, `come_on must be 0 or 1` and,
for both types, `id must be a positive whole number`. The screen's last-result row reads
`answer acked 6ms`, `mail failed` and so on (`mail`, not `mail_ack`, so `mail acked 1150ms` still
fits the 20 characters). Neither type disturbs the others in flight, and an answer for one rung floor
never cuts off an answer for the other: each floor has its own slot (Mike, 2026-11-15), so an answer
to an upstairs ring and one to a downstairs ring can both be in flight, up to six requests in all.

The ring notice is a new `kind`, 8, `ring`, raised by the upstairs panel (its `floor` is the RUNG
floor and its `value` the knock id, printed unsigned so the Pi gets the id itself):

    {"event":"notice","kind":"ring","floor":"downstairs","value":43981,"time":1793000000,"text":"RING"}

`text` `RING` means a ring just went out and that floor's phones should be pushed (the panel has
already checked the floor is Open or Busy outside quiet hours). `ANSWERED` means the ring was
answered or dismissed at a panel, so the phones can be told. The other kinds still print `value`
signed. Like every notice it is ACKed and printed once however many times it is sent.

## Rooms for Apple Home (gateway-c6 0.7, Mike, 2026-11-22)

The upstairs panel sends both floors' indoor readings and upstairs occupancy to the C6; the C6
prints one line; the Pi shows them in Apple Home as the HomeKit bridge "TComm Rooms" (accessories
"Upstairs" and "Downstairs"). The panels never join Wi-Fi for this.

On the radio it is a ROOMS frame (type 32; 31 is the `reach` test's, 20 to 30 are voice), 18 bytes:
the usual header, then `flags u8 | up_temp_tenths_f i16 | up_hum_tenths u16 | down_temp_tenths_f i16 |
down_hum_tenths u16 | time u32`. Flags: bit 0 upstairs reading valid, bit 1 downstairs reading valid,
bit 2 occupancy known (sensor fitted upstairs), bit 3 occupied. Decode refuses any other length, a
flag above bit 3, occupied without known, and a humidity over 100.0.

It works like a NOTICE: sent by the upstairs panel only, five tries over 1.5 s, ACKed every time it
arrives (ACK type 32, the frame's seq), and printed once per seq (its own 10 s seen-list, so ROOMS
retries never push a notice out). A ROOMS from the downstairs panel, an unknown MAC or a mismatched
sender byte is ignored with no ACK. The panel sends at once when occupancy changes, when a temperature
moves 0.5 F or a humidity 2 points from what it last sent, when a reading's validity changes, and at
least every 5 minutes; never more than once in 10 s except for occupancy; first about 20 s after
start-up. A C6 older than 0.7 never ACKs, and the panel just tries again each minute.

    {"event":"rooms","up_t":69.2,"up_h":57.0,"down_t":68.1,"down_h":45.5,"occ":1,"time":1795358600}

| Field | Meaning |
|---|---|
| `up_t`, `up_h` | upstairs temperature in F and humidity in %, one decimal, as the tile shows them (offset applied); both `null` when the upstairs reading isn't valid |
| `down_t`, `down_h` | the same for downstairs (its last READING, if one came in the last 15 minutes); both `null` otherwise |
| `occ` | 1 occupied, 0 not (upstairs, held for the presence hold time), `null` when no sensor is fitted |
| `time` | the panel's UTC seconds when it sent it, 0 without a clock |

The decimals are printed with integer math (no float printf on the C6), so -1.5 F is `-1.5` and
-0.5 F is `-0.5`. A line is at most about 120 bytes.

## Testing end to end from the laptop

With both boards on the laptop's USB, Claude plays the Pi: it sends the bridge's JSON lines to the
C6, and reads the panel's side over the panel's own port. Mike's idea, 2026-10-25.

The panel needs its radio build for this: `bash firmware/tools/fw.sh radio COM5`. That's the same firmware
with `TCOMM_RADIO_ESPNOW 1` and `TCOMM_DEMO 0`, set from fw.sh, so `tcomm_config.h` doesn't change
and `bash firmware/tools/fw.sh COM5` puts the demo build back. Without demo mode the other floor's tile
shows NO SIGNAL (there's no second panel yet), and the outside tile shows dashes until the gateway
sends a reading. Its sends to the second panel are counted and skipped while that MAC is zeros.

Two panel commands help:

- `radio` prints the radio's counts (frames in; dropped for an unknown MAC, a wrong sender byte,
  a bad length or a full queue; frames out, and whether a radio acknowledged them), the last 24
  frames in and out with their type and seq, and what the outside tile and the dog pill show.
- `shot` sends the screen over the port as raw RGB565 (768,000 bytes, about a second).
  `python -P firmware/tools/shot2png.py shot.raw shot.png` turns it into a picture. A redraw during the
  shot can show half done; take another.

On the C6 side, each final reply says what happened, and after a `failed` reply the C6 adds one
`gw:` note saying whether any radio acknowledged the tries. If one did, the panel heard the frames
but didn't take them (its radio off in its build, or different keys). If none did, the panel is
off, too far away or on another channel.

A reading's `time` must be the real time (UTC seconds): the panel drops a reading more than 15
minutes old by its own clock, so set the panel's clock first if it says NOT SET.

## Results, 2026-10-25 evening

Run from the laptop with the C6 on COM6 and the panel's radio build on COM5:

- Every reading and Lucy press reached the panel and showed correctly in screen captures: the nine
  forecast icons, both warnings, temperature, humidity and phrase (non-ASCII as "?", cut to 48),
  LUCY OUT and back in. Captures of each icon are in `firmware/sim/out/gw/` (not committed).
- 43 timed requests after the fixes: every one got its final reply, 37 acked and 6 superseded,
  15 to 153 ms (median 46) from writing the line to reading the reply. The panel ACKed every
  frame and dropped none.
- The supersede and the two types apart, as designed. All the rejections and errors, with nothing
  sent over the radio.

Two problems came up in the Arduino core's USB serial driver (HWCDC, core 3.2.0), both worked
around in `gateway-c6.ino` (`finish_usb_tx`). A reply exactly 64 bytes long (one full USB packet,
which most Lucy acks are) was held by the computer until the next line came, because nothing
ended the transfer; ESP-IDF's own driver sends a zero-length packet there, and now the C6 does
too. And data left in the driver's buffer gets its send interrupt woken after 5 ms.

The panel restarted (`rst:0x15`, a reset asked for over USB) the first time its port was opened
after two of its uploads; later opens didn't restart it. The C6 never restarted when its port
was opened. Not yet tried from Linux.

The brief for the WeatherGate project, for when the C6 moves to the Pi:
`gateway/docs/weathergate-c6-brief.md`.

## Testing with the listener

Until the panels' own ESP-NOW is switched on, the listener stands in for the upstairs panel. It
goes on the panel itself, because the panel's MAC is the upstairs one. It replaces the panel
firmware until it's put back; the SD card and the panel's settings are left alone, and the panel
screen stays dark meanwhile.

1. Fill in all three MACs in the secrets file (the gateway's too).
2. `bash firmware/tools/fw.sh listener COM5`
3. Open a serial monitor on COM5 (the Arduino IDE's works, at 115200). It prints each frame from
   the gateway, decoded and in hex, and the ACK it sends back.
4. Send the gateway some lines: from the Pi, or from a serial monitor on the C6's port with line
   ending set to New Line, for example
   `{"type":"lucy","id":13,"out":1,"since":1793000400}`.
5. Type `drop` into the listener to stop its ACKs: the listener should show five tries and the
   gateway should answer `failed` after 1.5 s. `drop 2` skips two ACKs, so the gateway gets its
   ACK on the third try, about 400 ms in. `ack` goes back to normal.
6. Put the panel back: `bash firmware/tools/fw.sh COM5`

The hex won't match the brief's examples exactly, because the seq starts at a random number each
boot; the host tests check the bytes with the brief's own seq values.

## Still to check on the hardware

- The screen's colours. Mike read every line off it on 2026-10-25, so the pins, offset and
  orientation are right, and its MAC matched the BASE MAC. (The upload writes 4 parts: seen too.)
- That opening the port from Linux doesn't reset the C6: `sudo systemctl restart tcomm-bridge`
  should produce no `{"boot":true,...}`. From Windows it doesn't (opened twice with DTR on and RTS
  off, 2026-10-25: the only boot line was the one from the restart after the upload).
- A `failed` reply and its `gw:` note on real hardware (unplug the panel and send a Lucy press).
- The listener (not needed now that the panel's radio build works, but untried).
- Done 2026-10-25: the round trip with the real panel, encryption included (Results, above).

## Voice notes (gateway-c6 0.5, 2026-11-20; mu-law and 255 chunks from 0.6, 2026-11-21)

From `gateway-c6 0.5` the C6 carries the panels' spoken notes to the Pi and the Pi's text back
(`firmware/docs/voice-notes/DESIGN.md` is the whole design; this is the C6's part). Nothing about the older
request types, their replies or the notice lines changes.

**Radio.** Frames of types 20 to 30 (`core/voice_proto.h`) have their own codec and never use the
standard ACK, the 10 s dedup or the retry queue; every confirmation is end to end. The C6 takes
BEGIN, DATA, END, ABORT, GOT, PUSH and PING from either panel: upstairs' own frames carry floor 0, a
frame from upstairs with floor 1 is downstairs' frame relayed by upstairs, and a frame from the
downstairs MAC must carry floor 1. Replies go back the way the floor's last frame came (direct, or
via upstairs); the C6's own dismiss frames go the way the last frame from downstairs came, via
upstairs until downstairs has been heard directly. A PING (direct only, from downstairs) is answered
at once with VOICE_GOT kind 30.

**One session per floor** (`voice_session.*`): xfer, codec, a received map of 255 chunks (sized from `tc::kVoiceMaxChunks`; 128 before 0.6), the route, the
time of the last frame. A BEGIN for another xfer replaces the floor's session (the Pi gets a
`voice_abort` with reason `lost` for the old one), a resent BEGIN never starts over. The C6 sends a
VOICE_ACK after BEGIN, after every 8 new chunks, 80 ms after the last DATA when a gap exists (or a
chunk came twice, or a chunk filled a gap; 0.6), and on END. That 80 ms is never pushed past 300 ms
from the first DATA since the last ACK, so a trickle of resends cannot hold it back, and a due ACK with
nothing to report is dropped (0.6). A session with no frame for 2 s is aborted (a `voice_abort` line, and
VOICE_ABORT reason 4 to the panel, three times at 0, 100 and 250 ms). The last finished xfer is kept
per floor, so a resent END gets state 2 again and prints nothing. A BEGIN while no bridge hello has
said `"voice":1` gets VOICE_ABORT reason 7 (no Pi).

**Cut-through, no clip buffer.** Each DATA chunk becomes one line (about 390 bytes, under the 512
limit; a mu-law chunk, codec 1, 237 samples, pred and step 0, is the same size, 376 to 378 bytes, but
a mu-law clip streams at 68 lines a second, 27 KB/s, not 34):

    {"event":"voice_begin","x":7,"f":"down","purpose":"message","codec":2,"cs":474,"via":"direct"}
    {"event":"voice","x":7,"f":"down","i":12,"p":-311,"s":41,"d":"<316 base64 characters>"}
    {"event":"voice_end","x":7,"f":"down","n":96,"ns":45504,"sp":1}
    {"event":"voice_abort","x":7,"f":"down","reason":"user"}
    {"event":"voice_msg","id":3735928559,"to":"downstairs","from":"upstairs","time":1793000000,"text":"..."}

`f` is `up` or `down`. A chunk counts as received only if its line was written whole.

**The write gate.** The USB send buffer is now 4096 bytes. A chunk line is written only if
`availableForWrite() >= line + 1100`; begin, end, abort and `voice_msg` lines only if 600 bytes stay
free; nothing is written for a chunk while a request of the first six kinds waits for its ACK. So a
notice event (about 110 to 256 bytes) or a request's reply is never cut or lost behind a stream. A
chunk that was held back is a gap in the next VOICE_ACK, and the panel resends it. If a `voice_end`
line cannot be written yet, the C6 tries again on every tick and the panel's resent END.

**Requests from the Pi** (each answered once, with the usual retries, 1.5 s give-up, `acked`,
`failed`, `rejected` or `superseded`; matched by the panel's VOICE_GOT, not by an ACK seq):

    {"type":"voice_text","id":N,"x":7,"floor":"downstairs","status":0,"fl":1,"t0":"<=60 chars","t1":"<=60 chars"}
    {"type":"voice_ack","id":N,"floor":"downstairs","msg":3735928559}

`voice_text` becomes VOICE_TEXT (t0 and t1 joined; `status` 0 needs text, any other status none;
`fl` is the flags byte, bit 0 = the first capital is only a sentence start; missing means 0). Latest
wins per floor: an interim status 6 is answered `superseded` when the final text comes. `voice_ack`
becomes VOICE_DISMISS (a phone's Got it) and is answered
`{"type":"voice_ack","id":N,"acked":"downstairs","result":"removed"|"gone","seq":S,"ms":M}`. These
slots are not latest-wins: while one is waiting for the floor, another `voice_ack` for it is answered
`{"type":"voice_ack","id":N,"rejected":"busy"}` (not counted as a rejection on the screen), and the
bridge asks again 1.6 s later. The screen's last-result row shows `vtext` and `vack`.

**VOICE_PUSH** (the receiving panel's message for its phones) prints once as a `voice_msg` line (the
last 8 ids are remembered), then VOICE_GOT kind 28 goes back by the frame's route: result 0, or
result 2 (refused) while the bridge has not said `"voice":1`. If the line cannot be written there is
no GOT, and the panel sends the push again.

**Hello.** The bridge sends `{"cmd":"hello","voice":1}`; the reply (and the boot line) now also
carry `"voice":1`, `"drops":N` (radio frames the receive queue had to drop since the reset) and
`"down_peer":true|false` (the downstairs panel is registered as an ESP-NOW peer with its key: false
means downstairs frames cannot be taken directly at all, the reach test's first condition):

    {"boot":false,"mac":"aa:bb:cc:dd:ee:ff","channel":1,"fw":"gateway-c6 0.6","voice":1,"drops":0,"down_peer":true}

A C6 0.4 ignores the extra field in the bridge's hello. The receive queue holds 48 frames (32 in 0.5, 16
in 0.4; 0.6 takes 68 DATA frames a second, 77 with a relayed floor). The C6's own air cap is 100 voice
frames a second with bursts of 25 (50 in 0.5): it only ever sends ACKs, GOTs, TEXT and DISMISS, about
9 a second per streaming floor.
The screen shows `DROPS n` on its last row when there have been drops and nothing else needs it.

**Tests** (`gateway/tests/test_gateway.cpp`): sessions from both floors at once, direct and via upstairs; line
size under 512 and the base64 round trip; the ACK schedule and the gap bitmap; the write gate at the
exact byte and with a stalled port; the H2 test (a doorbell notice and a Lucy reply arrive whole
while voice streams, into a backing-up port and into a stopped one); the 2 s abort; voice_text split,
join, flags and every rejection; voice_ack removed, gone, busy; push dedup and escaping; ping; the old
types unchanged.

**Capacity (estimates from the host build until measured on the C6).** Code: `gateway.cpp` about
13 KB, `voice_session.cpp` 1.2 KB and the shared `voice_proto.cpp` 4.3 KB on x86 `-Os`, so about 15
to 19 KB of flash on the C6 (1.5% of its app space). Static RAM: about +1.2 KB (four more request
slots of 290 bytes), +0.3 KB of voice state and line buffer. Heap: the receive queue is +4 KB and the
USB send buffer +2 KB.
