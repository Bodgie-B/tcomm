# Two commands for the WeatherGate Pi, from TComm (2026-11-07)

For the WeatherGate project's Claude Code session, from the TComm session. Mike carries it across.

Mike's decision: the C6 gateway stays plugged into the Pi from now on. It won't be moved to the
laptop for updates. The laptop reaches the Pi only with ssh and scp. That needs two commands on the
Pi, both run over ssh from the laptop:

1. A flash command that writes a gateway firmware image the laptop has copied over with scp.
2. An update command that asks the gateway to start both panels' Wi-Fi updaters, so both panels
   get new firmware with nobody touching either one.

Build the flash command first. The gateway's firmware today (`gateway-c6 0.1`) doesn't know the
update line and answers it with `{"error":"unknown_type"}`. The first image sent through the flash
command will be `gateway-c6 0.2`, which does.

This changes one rule in the earlier brief (`weathergate-c6-brief.md`, "Who owns what"), which said
never to run esptool against the C6 on the Pi. TComm still builds the gateway's firmware. From now
on the Pi writes it, but only through the flash command below and only with an image the laptop
sent. Everything else in that brief stands: the serial lines, the port settings, DTR and RTS left
alone, one process owning the port.

## Rules for both commands

- They run only when called explicitly over ssh. Nothing starts them on a timer, at boot, from the
  weather data, or from anything the gateway prints.
- No new network service, open port or web page. ssh with Mike's key stays the only way in. How the
  update command reaches the running bridge is your choice (a local Unix socket that only the
  bridge's user can open would do), as long as nothing listens on the network.
- One lock covers both. While one is running, the other (or a second copy of the same one) refuses
  at once with a non-zero exit instead of waiting.
- The bridge keeps sole ownership of the serial port. The update command goes through the bridge's
  open port. The flash command stops the bridge first and always starts it again before it exits,
  whatever happened.
- Every image has the radio keys compiled in. Keep the images in one folder only the bridge's user
  can read (mode 700), never in the Pi project's git, never copied anywhere else, and never print
  what's inside them. Keep the newest image and one backup, nothing older.
- Print short plain lines saying what happened, and finish with one result line the laptop can
  check (below).

## The flash command

### What the laptop sends

Two files, copied with scp into a folder you choose:

- `gateway-c6.ino.bin`, the gateway's application image, about 970 KB
- `gateway-c6.ino.bin.sha256`, one line in `sha256sum` format for that file

Then the laptop runs your flash command over ssh, pointing at those files.

### The board

| | |
|---|---|
| Chip | ESP32-C6 (Waveshare ESP32-C6-GEEK), 4 MB flash |
| Port | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_FC:01:2C:FD:D3:A8-if00`, or `/dev/tcomm-gateway` if the udev rule is in place |
| Base MAC | `fc:01:2c:fd:d3:a8` |
| Where the image goes | `0x10000`, the app0 slot |
| Slot size | `0x140000` (1,310,720 bytes); an image can't be bigger |
| Never written | bootloader `0x0`, partition table `0x8000`, nvs `0x9000`, otadata `0xE000`, and everything from `0x150000` up |

esptool resets the C6 into its ROM loader and back again over the chip's own USB port, using DTR
and RTS (its defaults, `--before default_reset --after hard_reset`), with no buttons to press.
That's the one time those lines should be touched. The bridge must still leave them alone.

The command names below are esptool 4.x's. esptool 5.x spells them with hyphens (`read-mac`,
`write-flash`).

### What it does, in order

1. Take the lock.
2. Check the image before touching anything: the SHA-256 matches the `.sha256` file, the size is
   between 64 KB and 1,310,720 bytes, the first byte is `0xE9`, and `esptool image_info` names the
   ESP32-C6 as its chip. If any check fails, stop with nothing changed and the bridge still running.
3. Stop the bridge, and make sure nothing has the port open.
4. Read the chip with `esptool --chip esp32c6 --port PORT read_mac`. Carry on only if it's an
   ESP32-C6 and its base MAC is `fc:01:2c:fd:d3:a8`. Otherwise start the bridge again and stop.
5. Save what's in the slot now with `read_flash 0x10000 0x140000` into a new backup file, and write
   a `.sha256` beside it. Only when that read succeeds does it replace the previous backup.
6. Write the image at `0x10000` and nowhere else: `write_flash 0x10000 gateway-c6.ino.bin`. No
   `erase_flash`, no `erase_region`, no other address, no bootloader and no partition table.
   esptool checks what it wrote and prints `Hash of data verified`; judge the write by that line.
   With the Arduino core TComm builds on, the C6 drops off USB while it restarts, so esptool can
   report a serial error after `Leaving...` even when the write was fine.
7. Wait for the port to come back (a few seconds), then open it briefly yourself, the way the bridge
   does: exclusive, DTR and RTS left alone. Read for up to 10 s. The C6 keeps its start-up line
   while the port is closed, so `{"boot":true,...}` should come out. Then send `{"cmd":"hello"}`,
   read the `{"boot":false,...}` reply, and close the port.
8. Start the bridge again. Once it's talking to the C6, have it send the current Lucy state once,
   in case Lucy went out or came in while the bridge was down. Weather readings missed in that
   minute need nothing: the panels keep showing the last one until the next arrives.
9. Print the hello reply as the last line and exit 0, for example
   `{"boot":false,"mac":"fc:01:2c:fd:d3:a8","channel":1,"fw":"gateway-c6 0.2"}`. The laptop checks
   the `fw` value against the version it built.

For anything else, exit non-zero with a line saying which step failed. If the bridge won't start
again, say so plainly in the output and exit non-zero.

### A dry run and a way back

- A dry-run option: steps 1 to 4, then start the bridge again, writing nothing.
- A rollback option: write the saved backup instead of a new image, with the same checks and steps
  (the backup's own `.sha256` from step 5 stands in for the laptop's).

If a write is cut off (a power cut, the cable), the C6 won't start the broken app, but its ROM
loader is in the chip itself, not in flash, so it still answers over USB. Run the flash command
again, or the rollback. The bootloader and partition table are never touched, so nothing in this
can leave the C6 unreachable from the Pi.

Please don't flash the gateway with anything yourself. The first real flash will come from the
laptop with TComm's image. Running the dry run on the Pi is fine and useful.

## The update command

It:

1. Takes the lock.
2. Has the bridge write one line to the gateway on the port it already holds (never open the port a
   second time while the bridge has it): `{"type":"update","id":N}`. N is a positive whole number
   not used before in this run of the bridge, from the same counter as the OUTSIDE and LUCY lines.
   Nothing else goes in the line.
3. Waits up to 5 s for the gateway's final line with that id, ignoring other lines. An
   `{"error":...}` line with no id that arrives just after the update line is its answer too.
4. Prints that line, and exits 0 only for `acked`.

The gateway's answers (the full detail is in TComm's `../gateway/docs/gateway-c6.md`, "The update line"):

| Answer | What it means |
|---|---|
| `{"type":"update","id":N,"acked":"upstairs","seq":...,"ms":...}` | upstairs took it; both panels start their updaters a few seconds later |
| `{"type":"update","id":N,"failed":"upstairs","seq":...}` | no answer from the upstairs panel after about 1.5 s, so nothing started; a `gw:` note follows |
| `{"type":"update","id":N,"busy":true,"seq":...}` | an earlier update request is still waiting for its answer |
| `{"type":"update","rejected":"..."}` | a bad line |
| `{"error":"unknown_type"}` | the gateway still has its old firmware |

Exit non-zero for everything except `acked`, including no answer within 5 s and the gateway not
being connected.

After `acked`, nothing more is needed from the Pi. The panels join the home Wi-Fi and wait up to
ten minutes in their updaters, and the laptop sends them the new firmware directly.

Testing it: before the gateway is flashed, the update command is safe to try, because the old
firmware answers `{"error":"unknown_type"}` and nothing happens to the panels (expect a non-zero
exit). After the flash, please don't send it as a test, since an `acked` restarts both panels into
their updaters. TComm will run the first real one end to end with Mike.

## What runs on the laptop

The TComm repository is public, so the Pi's address, user name and paths never go in it. Two files
on Mike's laptop hold them instead. Git ignores both, and they run under Git Bash on Windows with
its OpenSSH `ssh` and `scp`.

`tools/remote_trigger.local.sh` takes no arguments. It runs the update command over ssh and passes
its output and exit status through. TComm's `bash tools/fw.sh ota-remote` runs it with a 30 s limit
(after compiling the panel firmware), then finds both panels' updaters on the network and uploads.

`tools/gateway_flash.local.sh DIR` gets a folder on the laptop holding the two files. It copies
them with scp to your folder, then runs the flash command over ssh, passing its output and exit
status through. TComm's `bash tools/fw.sh gateway-remote` builds the gateway firmware, writes the
two files, runs this with a 5 minute limit, and checks the `fw` in the last line.

Use `-o BatchMode=yes` on both ssh and scp, so a missing key fails at once instead of waiting for a
password.

## What to send back to Mike

- The exact contents of both laptop files.
- The folder the images go into, and the dry-run and rollback commands.
- What the dry run printed: the esptool version, and the chip and MAC it read.
- Whether the by-id port name matched (or `/dev/tcomm-gateway` exists).
- Anything in this brief that doesn't fit how the bridge is built, before building around it.
