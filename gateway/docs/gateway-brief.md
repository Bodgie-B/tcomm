# Gateway brief: WeatherGate and LucyGate to the TComm panels over ESP-NOW

Written 2026-10-25 from the TComm firmware as it stands (commit dd98590). TComm is the pair of
wall panels (Waveshare ESP32-S3-Touch-LCD-4.3C) at the top and bottom of the basement stairs.
They show outside weather on their OUTSIDE tile and which dogs are in the yard on their dog
button. This brief is everything the Raspberry Pi side needs to make an ESP32-C6 send both the
way the panels expect. Where something isn't decided, it says so.

## The setup

One Raspberry Pi 2 runs two programs. WeatherGate reads outside temperature, humidity and
pressure and works out a Zambretti forecast. LucyGate handles Lucy's button: Lucy is out, or
she's back in (which cancels the Lucy Out). Mike's ESP32-C6-GEEK (Waveshare: ESP32-C6, 16 MB
flash, USB-A plug, small LCD) plugs into one of the Pi's USB ports and is the radio for both.

To the panels, the Pi and its C6 are a single device, "the gateway". That's how TComm was
designed: the panels accept exactly one gateway, and that gateway sends two kinds of message,
OUTSIDE (a weather reading) and LUCY (Lucy out or in). Both go to the upstairs panel, which
passes them on to the downstairs panel itself.

The programs hand their messages to the C6 over USB serial. The C6 turns each into a TComm
message, sends it over ESP-NOW, waits for the panel's ACK and retries if it doesn't come. Only
the ESP-NOW side is fixed by TComm; how the Pi talks to the C6 is the Pi project's choice, and a
suggestion is near the end.

## Radio settings that must match the panels

The panels run ESP-NOW on Wi-Fi channel 1 and never join the home Wi-Fi (joining would move them
to the router's channel). The C6 has to do the same: Wi-Fi in station mode, not connected to any
access point, channel set to 1 with `esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE)` before
`esp_now_init()`. It must not connect to Wi-Fi at any point, even briefly.

ESP-NOW encryption is on (its built-in CCMP). The C6 sets the shared 16-byte primary key with
`esp_now_set_pmk()` and adds each panel as a peer with `encrypt = true` and the 16-byte gateway
link key as its `lmk`, on channel 1 and the station interface. Mike chooses the keys and puts the
same values in both projects' secrets files. Those files are never committed (TComm's is
`firmware/tcomm/src/config/secrets.h`, listed in .gitignore). Keep the keys out of code, logs,
the C6's screen and chat.

Each panel accepts frames only from three MAC addresses (upstairs, downstairs and the gateway),
and only when the message's sender byte matches the MAC it came from. The gateway's sender byte
is 2, for both message types. So:

- The C6 needs both panels' Wi-Fi station MACs. The first panel's is 14:C1:9F:54:BE:20; which
  floor it becomes isn't decided yet. The second panel hasn't arrived, so its MAC isn't known.
- TComm needs the C6's Wi-Fi station MAC (`esp_read_mac(mac, ESP_MAC_WIFI_STA)`) for its
  `TCOMM_MAC_GATEWAY`. Print it on the serial port at boot, and on the C6's screen if that's
  easy, so Mike can copy it across.

Every message starts with the same 5-byte header, little-endian:

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | u8 | version | 1 (the panels drop anything else) |
| 1 | u8 | type | 8 OUTSIDE, 7 LUCY |
| 2 | u8 | sender | 2 (the gateway) |
| 3 | u16 | seq | new for each message, the same on its retries |

The C6 keeps one seq counter for everything it sends, both types together, starting at a random
value each boot. The panels drop a repeat of a (sender, seq) pair they've seen in the last 10
seconds, and both types share sender 2, so a LUCY and an OUTSIDE must never share a seq.

## OUTSIDE: a weather reading (WeatherGate)

One message per reading, no padding. The panel rejects any frame whose length isn't exactly
15 + phrase_len bytes.

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 5 | header | type 8 |
| 5 | u8 | flags | bit 0 valid (always set), bit 1 danger heat, bit 2 danger cold; other bits 0 |
| 6 | i16 | temp_tenths_f | outside temperature in tenths of a °F: 485 is 48.5 °F, -400 is -40 °F |
| 8 | u8 | humidity | whole percent, 0 to 100 (anything over 100 is rejected) |
| 9 | u8 | forecast | the symbol code, table below |
| 10 | u32 | time | UTC seconds when the reading was taken; 0 if the Pi's clock isn't set |
| 14 | u8 | phrase_len | 0 to 48 |
| 15 | bytes | phrase | the forecast text, printable ASCII (0x20 to 0x7E), no terminator |

Two examples, as hex, with seq 0x1234 and 0x1235 (the time is 2026-10-26 07:33:20 UTC):

48.5 °F, 71 %, sunny, "Settled Fine" (27 bytes):
`01 08 02 34 12 01 E5 01 47 01 40 02 DF 6A 0C 53 65 74 74 6C 65 64 20 46 69 6E 65`

18.0 °F, 70 %, snow, danger cold, "Snow at Times, Worse Later" (41 bytes):
`01 08 02 35 12 05 B4 00 46 05 6C 03 DF 6A 1A 53 6E 6F 77 20 61 74 20 54 69 6D 65 73 2C 20 57 6F 72 73 65 20 4C 61 74 65 72`

### Forecast codes

| Code | WeatherGate symbol key | Zambretti letters | The panel shows |
|---|---|---|---|
| 1 | `sunny` | A, B, J, K, T, U | sun; the moon at night |
| 2 | `partly_cloudy` | C, D, L, M, X | sun behind a cloud; the moon behind it at night |
| 3 | `cloudy` | N, Z | a cloud |
| 4 | `rain` | E, F, O, P | cloud, three drops |
| 5 | `snow` | any rain letter after the rain-to-snow swap | cloud, five flakes |
| 7 | `storm` | S | cloud, lightning bolt |
| 8 | `clearing` | V, W, Y | sun with a small cloud breaking away; the moon at night |
| 9 | `heavy_rain` | G, H, I, Q, R | cloud, five longer drops |
| 0 | (none) | | a dash, with the temperature and humidity still shown |

Never send 6: that's the panels' own night sky, and they pick the moon from the time of day. A
code the panel doesn't know shows as the dash. The letters come from WeatherGate's
ZAMBRETTI_REFERENCE.md, which TComm copied into `firmware/tcomm/src/core/zambretti.cpp`; if
WeatherGate's table changes, nothing on the panel needs to, since the panel shows the code and
text it's sent.

### The text

Send WeatherGate's forecast text as it would show it, after the rain-to-snow swap ("Snow at
Times, Worse Later"), or "No Pressure Data" when there's no pressure reading. ASCII only: no
degree signs or curly quotes (the panel turns anything else into "?"). The panel wraps it in up
to four lines beside the icon and steps the font down for the longest Zambretti texts. The
longest is 38 characters; the limit is 48.

### The warnings

Set the two flag bits from WeatherGate's own `danger_heat` (heat index 100 °F or more) and
`danger_cold` (20 °F or less), independently. With danger heat the panel replaces the icon with a
bigger sun whose rays pulse; with danger cold it hangs icicles across the top of the screen.

### How often

Send a reading when there's a new one, and at least every 5 minutes; there's no need to send
more than once a minute. A panel shows dashes once a reading is 15 minutes old (going by `time`
when both clocks are set, otherwise by when it arrived), so a gateway that goes quiet shows up on
the wall.

## LUCY: Lucy out, or back in (LucyGate)

One message per button press, exactly 10 bytes:

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 5 | header | type 7 |
| 5 | u8 | out | 1: Lucy is out. 0: Lucy is back in (cancels the Lucy Out) |
| 6 | u32 | since | UTC seconds of the press; 0 if the Pi's clock isn't set (the panel uses its own) |

Lucy out at 07:40:00 UTC, seq 0x1236: `01 07 02 36 12 01 D0 03 DF 6A`

Lucy back in at 07:55:00 UTC, seq 0x1237: `01 07 02 37 12 00 54 07 DF 6A`

What the panels do with it:

- Lucy out marks Lucy as out on both panels. Copper is kept separately, so both can be out at
  once (Mike, 2026-10-25): the dog button then reads BOTH DOGS OUT, otherwise LUCY OUT.
- Lucy in marks Lucy as back in and leaves Copper as he is. If Lucy isn't out, it changes
  nothing. LucyGate never touches Copper.
- Sending the same state again (Lucy out while she's already out) changes nothing, and it's
  still acknowledged. So a press that might have been missed can safely be sent again.
- The upstairs panel's dog button is Lucy's and can change her too; whichever change comes last
  wins, the button or LucyGate. The panels keep a change counter per dog for that, so the Pi's
  clock isn't part of the decision. The downstairs button is Copper's.
- The panels never tell the gateway whether Lucy is out. LucyGate only knows what it sent, so if
  it shows Lucy's state on its own screen or light, it's showing its own last press, not the
  panels'.

Send it to the upstairs panel, like OUTSIDE; the upstairs panel passes the change to downstairs.

## Acknowledgements and retries (both types)

Every panel answers each message with an ACK, retries and duplicates included. ESP-NOW's own
send callback only says the radio frame got through; wait for this ACK instead:

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | u8 | version | 1 |
| 1 | u8 | type | 4 (ACK) |
| 2 | u8 | sender | 0 upstairs, 1 downstairs |
| 3 | u16 | seq | 0 |
| 5 | u16 | acked_seq | the seq of the message it answers |
| 7 | u8 | acked_type | 8 for OUTSIDE, 7 for LUCY |

The ACK for the first OUTSIDE example is `01 04 00 00 00 34 12 08`; for the Lucy-out example,
`01 04 00 00 00 36 12 07`. The C6 should accept ACKs only from the panels' MACs, with the sender
byte matching, the same check the panels make.

Retry on TComm's schedule: tries at 0, 150, 400, 750 and 1150 ms after the first send, stopping
at the ACK, and giving up 1.5 s after the first try. The two types are retried independently: a
weather reading and a Lucy press can both be in flight at once. Within a type only the latest
matters, so a newer reading replaces one still being retried, and a newer Lucy press replaces an
older one (pressing out then quickly in sends only "in" if "out" hasn't been acknowledged yet).
A LUCY never replaces an OUTSIDE, or the other way round.

A missed Lucy press matters more than a missed reading (the next reading comes within minutes;
a press doesn't). When a LUCY gives up with no ACK, the C6 should report it, and LucyGate
should send it again (a new seq) every 30 seconds until it's acknowledged or a newer press
replaces it. That's safe because a repeat changes nothing on the panels. Panels never send the
gateway anything but ACKs.

## Two programs, one C6 (a suggestion)

The C6-GEEK's USB port is the chip's own USB Serial/JTAG, which shows up on the Pi as
`/dev/ttyACM0`. Only one process should open it: two programs writing to one serial port would
interleave their lines and steal each other's replies. A small bridge service on the Pi (a
systemd unit) can own the port and take messages from both programs over a local socket, for
example a Unix socket that each program writes JSON lines to. It passes each line to the C6 and
sends the C6's reply back to whichever program asked. Putting LUCY and OUTSIDE through one
process also keeps the single seq counter and the per-type retries in one place, on the C6.

One JSON object per line works well from Python and is easy to parse on the C6:

Pi to C6:
`{"type":"outside","temp_tenths_f":485,"humidity":71,"forecast":1,"flags":1,"time":1793000000,"phrase":"Settled Fine"}`
`{"type":"lucy","out":1,"since":1793000400}`

C6 to Pi, after each message: `{"type":"lucy","acked":"upstairs","seq":4662,"ms":9}` or
`{"type":"lucy","failed":"upstairs","seq":4662}`; at boot:
`{"boot":true,"mac":"xx:xx:xx:xx:xx:xx","channel":1}`.

The C6 should check what it's given before sending (humidity 0 to 100, forecast code 0 to 9 but
not 6, phrase cut to 48 printable ASCII characters, out 0 or 1) and say so when it rejects a
line. The keys and the panels' MACs belong in the C6's own firmware secrets, not on the serial
link. The bridge should reopen the port if the C6 resets, and keep a Lucy press it couldn't
deliver so it can resend it.

## What isn't ready on the TComm side

TComm's ESP-NOW code is written and compiles, but it has never run: the panel firmware currently
builds with `TCOMM_RADIO_ESPNOW 0` and demo mode on, so the panel isn't listening yet. Once the
C6 sends, a TComm session needs to put the gateway's and panels' MACs and the keys in TComm's
`secrets.h`, build with ESP-NOW on and demo mode off, and test against the C6. With one panel,
that panel shows No signal for the other floor but still shows the outside reading and LUCY OUT.

Until then, the C6 can be tested against a second ESP32 running a small listener that decodes
the frames, checks them against the examples above and sends the ACKs back.

## Don'ts

Don't join Wi-Fi or change the channel. Don't send forecast code 6, more than 48 characters of
text, or non-ASCII text. Don't give a LUCY and an OUTSIDE the same seq. Don't send readings more
than once a minute. Don't commit the keys or MACs.
