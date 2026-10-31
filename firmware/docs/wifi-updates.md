# Updating the panels over Wi-Fi

Written 2026-10-31. Tested on the upstairs panel the same day as far as noted below; the rest is
UNVERIFIED.

The panel firmware never joins Wi-Fi. A second, small program, the updater
(`updater`), sits in the panel's other program slot. When you ask for an update, the panel
restarts into the updater. It joins the home Wi-Fi, waits up to ten minutes for the new firmware
from the laptop, writes it over the panel firmware and restarts into it. If nothing arrives, it
restarts into the panel firmware as it was. While it's in the updater the panel is off the radio,
so the other panel shows No Signal for those few minutes.

Why a separate program: these builds copy all their program code into PSRAM at start-up, so Wi-Fi's
network stack inside the panel firmware would cost about 260 KB of PSRAM for good, and the radio
build has about 750 KB free once the screen is drawn. The updater pays that cost only while it
runs. The flash already has room: two 3 MB program slots, and the panel firmware is 1.8 MB.

## Once per panel, by USB, before mounting

1. Open `tcomm/src/config/secrets.h` in Notepad and type your home Wi-Fi's name and
   password between the quotes on the `TCOMM_WIFI_SSID` and `TCOMM_WIFI_PASS` lines. Save.
   `TCOMM_OTA_PASS` is already filled in with a random upload password. Don't paste any of these
   into chat.
2. Flash the panel firmware as usual, after confirming the port: `bash tools/fw.sh radio COM5`. It
   has the Settings tile and the crash fallback.
3. Install the updater: `bash tools/fw.sh updater COM5`. It writes only the second slot; look for
   "updater written into app1".
4. Check it: Settings > Update over Wi-Fi should read "Tap to start". Tap it, then Restart. The
   screen should show TCOMM UPDATE, the panel's name, its Wi-Fi address and a countdown. Send the
   firmware (below), or wait ten minutes and it goes back on its own.

## Updating a mounted panel

1. On the panel: Settings > Update over Wi-Fi > Restart. (Over USB serial, `update` does the same.)
2. Wait for READY FOR THE UPLOAD and note the address on the screen, for example
   "WI-FI 192.168.1.37".
3. On the laptop, in Git Bash in the firmware folder: `bash tools/fw.sh ota 192.168.1.37`.
   `tcomm-upstairs.local` or `tcomm-downstairs.local` may work instead of the address, if Windows
   finds names on the network.
4. The screen counts the kilobytes as they arrive, and the panel restarts into the new firmware.
   The send took about 70 seconds on 2026-10-31, most of it the compile.

The laptop sends the file to the panel with curl, an ordinary web upload, so nothing has to get in
through the laptop's firewall. (The first version used Arduino's espota, which needs the panel to
connect back to the laptop; Windows blocked that on 2026-10-31.)

## When something goes wrong

- No Wi-Fi name in `secrets.h`, or no connection within two minutes: the screen says so and the
  panel goes back to its firmware.
- An upload that fails part way: UPLOAD FAILED, and another ten minutes to try again. If the panel
  firmware was partly overwritten, the updater stays (NO PANEL FIRMWARE. STILL WAITING) until a
  good upload arrives, and starts itself again after a power cut.
- A new build that crashes at start-up: after three crash restarts in a row, the panel starts the
  updater on its own, so a fixed build can go on without taking the panel down.
- The updater itself only changes by USB.

## Security

The updater listens only while it runs (twelve minutes at most), only on the home network, and only
takes an upload with the password from `secrets.h`. The Wi-Fi password and the upload password live
only in `secrets.h`, which never goes to GitHub.

## Checked on the upstairs panel, 2026-10-31

- `fw.sh updater COM5` wrote the updater into the second slot (esptool from bash: Bitdefender let
  it through), and the panel firmware kept running.
- Serial `update` restarted the panel into the updater, which named itself tcomm-upstairs, joined
  the Wi-Fi in a few seconds and waited.
- `fw.sh ota` with the address on the screen sent the radio build over Wi-Fi; the panel answered "Update Success",
  restarted into it and came back up as UPSTAIRS with its radio, SD card and readings.

## Not yet checked

- The updater's screen and backlight level (nobody has looked at it yet), and the Settings tile
  path (the test used serial `update`).
- Wi-Fi strength where each panel hangs, the basement one especially.
- The crash fallback, which needs a build that crashes on purpose to test.
