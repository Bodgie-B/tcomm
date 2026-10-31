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
| `gateway/gateway-c6/src/gw/` | The logic (requests, checks, retries, ACKs, replies) and a small JSON reader. Plain C++. |
| `gateway/gateway-c6/src/lcd/` | The 1.14 inch ST7789 screen, with a built-in 5 x 7 font. No display library. |
| `gateway/gateway-c6/src/shared/` | Three one-line files that compile the panels' own `core/protocol.cpp`, `events.cpp` and `civil.cpp` into the gateway. Nothing is copied. |
| `gateway/gateway-c6/listener/` | A stand-in for the upstairs panel, for testing before the panels' ESP-NOW is switched on |
| `gateway/tests/test_gateway.cpp` | 75 host checks, run by `bash firmware/tools/build_sim.sh` |

The host tests cover the brief's two OUTSIDE and two LUCY frames byte for byte, the ACK examples,
ACKs from an unknown MAC or with the wrong sender byte (ignored), the retry times (0, 150, 400,
750, 1150 ms, failed at 1.5 s), a newer request superseding one in flight, the two types retrying
independently, exactly one final reply per id, and every field check.

Capacity, built with the real secrets file: the gateway uses 966,393 bytes of the C6's
1,310,720-byte app space (73%, nearly all of it the Wi-Fi stack) and 45,388 bytes of static RAM
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
   fc:01:2c:fd:d3:a8 (esptool read_mac, 2026-10-25).

To compile without uploading, leave the port off: `bash firmware/tools/fw.sh gateway`.

## The secrets file

It's `firmware/tcomm/src/config/secrets.h`, the same file the panels use, so the panels, the
gateway and the listener are always built with the same keys. Copy `secrets.example.h` in the
same folder to `secrets.h` and fill it in. It's in `.gitignore`, so it never gets committed. The
gateway never prints the keys or shows them on its screen.

```
#define TCOMM_MAC_UPSTAIRS   {0x14, 0xC1, 0x9F, 0x54, 0xBE, 0x20}
#define TCOMM_MAC_DOWNSTAIRS {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define TCOMM_MAC_GATEWAY    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

#define TCOMM_PMK         "................"
#define TCOMM_LMK_PANELS  "................"
#define TCOMM_LMK_GATEWAY "................"
```

`TCOMM_MAC_UPSTAIRS` is the panel the gateway sends to. The line above assumes the first panel
(14:C1:9F:54:BE:20) goes upstairs; the panel works out its floor from this table too.
`TCOMM_MAC_DOWNSTAIRS` stays all zeros until the second panel arrives, and the gateway skips a zero
MAC. `TCOMM_MAC_GATEWAY` is the C6's own Wi-Fi MAC: for Mike's C6, `{0xFC, 0x01, 0x2C, 0xFD, 0xD3,
0xA8}` (step 7 above confirms it). The gateway runs without it, but the panels and the listener
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

A reset loses whatever was in flight, with no final reply for it. When `{"boot":true,...}` arrives,
treat any request still waiting as failed, and send the Lucy press again.

While the port is closed (the bridge restarting), the C6 keeps the replies it writes in a 2 KB
buffer, and they come out when the port opens again. Ignore replies for ids this run of the bridge
didn't send, and send `{"cmd":"hello"}` after opening.

Leave DTR and RTS alone. The chip's USB port resets the C6 when RTS is set while DTR is clear
(that's how esptool resets it); opening the port sets both together, which doesn't. pyserial's
defaults are fine, but don't set `ser.dtr = False`. UNVERIFIED until the hardware check below.

Anything that isn't a reply starts with something other than `{`: the chip's own start-up text
after a reset (`ESP-ROM:...`), system errors (`E (...`), and the gateway's few notes (`gw: ...`,
start-up problems, and radio send errors at most once a minute). Writes give up after 20 ms when
nothing is reading the port.

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
