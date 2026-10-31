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

1. Plug in only the new panel. Find its port in Device Manager, under Ports (COM & LPT), as "USB
   Serial Device (COMx)". Tell Claude the port, and Claude confirms it with you before writing
   anything.

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

6. Plug the first panel back in, confirm its port (it has been COM5), and flash it so it knows the
   new panel:

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

- Type `radio` on each panel's serial port. Each should name its own floor and count frames from
  the other, with nothing about a board missing from `secrets.h`.
- Each panel's tile for the other floor shows its status within a few seconds, not No Signal.
  Change the status on one; the other follows.
- Ring the doorbell from each side. The receiving panel chimes, shakes the bell and turns it red.
- The new panel shows the right time. If its own clock was never set, it copies the time from the
  first panel.
- The first panel's calendar events appear on the new one within about a minute. Past events
  archived on the first panel's SD card stay there.
- Downstairs fills its Outside tile at the Pi's next weather send, passed on by upstairs. Lucy shows
  on both.
- Tap the mail badge downstairs; upstairs shows it. Only downstairs pulses.
- Tap a dog button on one panel; the other shows the same dog out.

To put a panel back on the demo build: `bash tools/fw.sh COM7` (no `radio`).
