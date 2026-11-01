# Flashing the second panel

Both panels run the same radio build. A panel works out its floor from its own Wi-Fi MAC: the MAC
listed as `TCOMM_MAC_UPSTAIRS` in `secrets.h` is upstairs, the one listed as `TCOMM_MAC_DOWNSTAIRS`
is downstairs. So the second panel needs one line in `secrets.h` and no code change. The first
panel (14:C1:9F:54:BE:20) is upstairs.

Both panels get flashed: the new one, and then the first one again, because each panel only
listens to the MACs it was built with. The C6 gateway doesn't need flashing. It only ever sends to
upstairs, and upstairs passes the weather and Lucy on to downstairs.

Written 2026-10-31, before the second board arrived. The `mac` option of `fw.sh` and everything
about two panels talking to each other are UNVERIFIED until this runs.

## Before you start

The new board comes with Waveshare's factory demo, and flashing replaces it. (Waveshare's factory
image can put it back; see the handoff brief, Mike's setup.) The upload writes the bootloader, the partition
table and the app. It never erases the whole chip.

Have a microSD card in FAT32 for the new panel. The panel keeps past calendar events on it. It
won't format a card unless you type `sd format yes` on the serial port.

Run everything from Git Bash in the firmware folder, never PowerShell (Bitdefender blocks builds
started from PowerShell). Close any serial monitor on a board's port before flashing it.

## Steps

1. Plug in the new panel. The first can stay plugged in on its own port (it has been COM5); plug
   each straight into the PC, not into a hub without its own power. Find the new one's port in
   Device Manager, under Ports (COM & LPT), as "USB Serial Device (COMx)": the one that wasn't
   there before. Tell Claude the port, and Claude confirms it with you before writing anything.

2. Read its MAC. This writes nothing:

   ```
   bash tools/fw.sh mac COM7
   ```

   The line to keep is `MAC: xx:xx:xx:xx:xx:xx`. If Bitdefender objects to esptool being run this
   way, skip to step 4 instead: `fw.sh` prints the MAC after every upload, so flash first, then put
   the MAC in and flash again.

3. Put the MAC in `tcomm/src/config/secrets.h` (Notepad is fine). Change only the
   downstairs line. For a MAC of `ab:cd:ef:01:23:45` it reads:

   ```
   #define TCOMM_MAC_DOWNSTAIRS {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45}
   ```

   Leave the keys, the other MACs and the location alone. Don't paste the keys anywhere.

4. Flash the new panel with the radio build:

   ```
   bash tools/fw.sh radio COM7
   ```

   Look for "4 of 4 parts written and verified" and "upload OK".

5. Watch its first boot. It formats its own 9 MB calendar partition once (the partition is empty
   on a new board; the log says "calendar: no file system on the FAT partition, formatting it").
   That's the only thing it erases, and it doesn't touch the SD card. Settings > This unit is
   should read "DOWNSTAIRS (by MAC)".

6. Confirm the first panel's port (it has been COM5), and flash it so it knows the new panel:

   ```
   bash tools/fw.sh radio COM5
   ```

   Its settings, calendar, dog and mail state stay as they are. An upload doesn't touch them.

7. Set the new panel's own Settings: ringtone, volume, night light, screen at night and the
   temperature offset for where it hangs. It starts from the defaults.

8. Before either panel goes on the wall, install the Wi-Fi updater on both, so later updates
   don't need them taken down: `bash tools/fw.sh updater COM7`, then the same on COM5. Fill in the
   Wi-Fi name and password in `secrets.h` first. The steps and checks are in
   `docs/wifi-updates.md`.

## Checks, with both panels powered

With both panels on USB, `tools/two_panels.ps1` runs these checks over the two serial ports. Run it
from PowerShell (serial is fine there; only builds are blocked), with no serial monitor open on
either port:

```
powershell -ExecutionPolicy Bypass -File tools\two_panels.ps1
```

It asks each port which floor its panel is, so the ports don't need naming (`-Upstairs COM5
-Downstairs COM6` checks them instead). It changes the status, the dogs and the mail on each panel
in turn and times how long the other takes to show it, then puts everything back, even if a check
fails. Mail and the doorbell play sounds on both panels; `-Quiet` leaves them out. What only eyes
can check (the banner, the bell shaking and turning red, the mail badge pulsing) it prints as LOOK
lines; when it rings, it waits 15 seconds for someone to tap a reply on the banner. `-WeatherWait
15` waits up to 15 minutes for downstairs's Outside tile. Each run's serial traffic from both
panels goes to `sim/out/two-panels/`. The clock comparison needs firmware from 2026-11-01 on,
whose `info` prints the time.

The script was tested against two pretend panels that answer in the firmware's formats (working,
not hearing each other, losing touch partway, and one panel alone), not yet against two real
ones: UNVERIFIED until the second panel arrives.

The checks, by hand:

- Type `radio` on each panel's serial port. Each should name its own floor and count frames from
  the other, with nothing about a board missing from `secrets.h`.
- Each panel's tile for the other floor shows its status within a few seconds, not No Signal.
  Change the status on one; the other follows.
- Ring the doorbell from each side. The receiving panel chimes, shakes the bell and turns it red.
- The new panel shows the right time. If its own clock was never set, it copies the time from the
  first panel, or takes the Pi's time from the next weather message (passed on by upstairs).
- The first panel's calendar events appear on the new one within about a minute. Past events
  archived on the first panel's SD card stay there.
- Downstairs fills its Outside tile at the Pi's next weather send, passed on by upstairs. Lucy shows
  on both.
- Tap the mail badge downstairs; upstairs shows it. Only downstairs pulses.
- Tap a dog button on one panel; the other shows the same dog out.

To put a panel back on the demo build: `bash tools/fw.sh COM7` (no `radio`).
