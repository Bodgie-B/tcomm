# TComm: handoff brief for Claude Code

Read this whole file before touching anything. It is self-contained. Mike will paste a separate GUI design brief from Claude Design below the marked section near the end. Where that design brief and this file disagree about layout, colors or fonts, the design brief wins. Everything else here stands.

## Goal

Build firmware for two wall panels, one at the top of the basement stairs (UPSTAIRS, Mike) and one at the bottom (DOWNSTAIRS, his sister). Each panel shows the other floor's status in color, lets you set your own status, rings a doorbell on the other panel, shows both floors' temperature and humidity, shows which dog (Lucy or Copper) is in the yard, and shows outside weather from Mike's WeatherGate project. The panels talk to each other directly over ESP-NOW. They don't need the router.

Board: Waveshare ESP32-S3-Touch-LCD-4.3C (800x480 IPS, 5-point capacitive touch, 16 MB flash, 8 MB PSRAM). Mike has one board in hand. A second one is bought only after the interface and radio phases pass. A spare ESP32 can stand in as the second node for radio work.

## Mike's setup

- Windows laptop with one USB-C port. The board shows up there as COM5 (USB Serial Device). Verify before every flash, because the number can change.
- Download mode: hold BOOT, power-cycle the board (unplug and replug USB-C), then start the flash. This is the only method Waveshare documents. Press RESET after flashing.
- Mike used Espressif's Flash Download Tool V3.9.11 (ChipType ESP32-S3, WorkMode Develop, LoadMode USB, baud 1152000) and it worked. Detected flash: XMC vendor, 16 MB, QUAD, 40 MHz crystal.
- Factory restore if anything goes wrong: flash `ESP32-S3-Touch-LCD-4.3C-Test.bin` at address 0x00. The file is in the Downloads folder on Mike's laptop. Source: the `firmware` folder of github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3C.
- Do not run AIChats in the factory firmware. It traps the board on a Wi-Fi setup screen with no exit. Reflashing the factory image cleared it.

## State of the board today (2026-10-21)

- Factory firmware restored and running: launcher with SquareLine, DrawPanel, Gallery, MusicPlayer, AIChats, Settings, SpecAnalyzer.
- Touch works everywhere Mike tried. The backlight dims low enough that he can't see the screen, so the low-brightness check passed by his judgment. Flicker at low brightness has not been checked (slow-motion phone video).
- Sound came out of the unit during the AI app demo, so a speaker is connected. Its size, impedance and loudness are unknown.
- Whether the onboard clock holds time with power off is unknown. The coin cell is not in Waveshare's package list. Test: set the time, unplug for a minute, replug.
- MAC addresses read from the flash tool for this board: Wi-Fi station 14:C1:9F:54:BE:20, soft-AP 14:C1:9F:54:BE:21, Bluetooth 14:C1:9F:54:BE:22. ESP-NOW peers normally use the station MAC (my understanding, confirm in code).
- The two floors are not assigned yet. The firmware needs a stored "which floor am I" setting.

## Decisions already made (do not reopen)

- Status states: Open, Busy, Private, Sleeping, Away, plus No signal for the other floor.
- Tap your own status tile to open a status sheet. Private and Sleeping continue to a timer sheet with a selectable fallback status (default Busy as a starting value). After any change, show a 5-second Undo bar. The other floor's tile and the weather zones do not respond to touch.
- DOORBELL tile: one tap sends a normal chime. There is no urgent hold gesture and no urgent chime. Ignore repeat taps for 10 seconds. The sender sees "Ringing", then "Delivered" after the ACK, then the reply (Come on or Not now) within about a second. If nobody answers in 60 seconds, show "No answer yet".
- Dog tile: the UPSTAIRS tile is Lucy's, the DOWNSTAIRS tile is Copper's. One shared value says which dog is in the yard: Lucy, Copper or neither. Both panels show it as a paw button (neither) or a "LUCY OUT" or "COPPER OUT" tile. The two dogs are never out together. While the other household's dog is out, tapping your own dog tile does nothing (default; Mike has not confirmed). Lucy's value can also be set from outside (see LUCY below). Later change wins.
- No heartbeats. Mike did not want a periodic message. Nothing is sent on a timer.
- No Signal works like this: when a message gets no ACK after all retries, the sender marks the other side as No signal (gray tile, last known state, how long ago). Any message received from that unit clears it. Consequence to accept: a unit that loses power while everything is quiet is only noticed the next time something is sent.
- Night: the screen stays on at the dim night level, readable. A touch brings it to full light, and it returns to dim after 30 seconds without a touch. A "screen off overnight" option exists in Settings, off by default.
- Colors are final. Do not adjust them for contrast. The 56 px gear and dog button are fine. Keep the other touch targets at 90 px or taller.
- Phone connectivity is out of scope for now (no phone alerts, no ntfy, no Bluetooth setup page). Do not build it.
- Neither panel joins the home Wi-Fi in normal mode, which keeps one fixed ESP-NOW channel. A maintenance mode (from Settings or by holding BOOT at power-on) joins home Wi-Fi to take network time and firmware updates, then reboots to normal mode.

## Behavior when a doorbell arrives (starting values, changeable in Settings)

| Receiver state | What happens |
|---|---|
| Open | Chime at the set volume, banner |
| Busy | Soft chime, banner |
| Private | Banner only, screen wakes |
| Sleeping | Banner only, screen wakes at night level |
| Away | Saved, shown on return |

The banner reads "Doorbell from upstairs" (or downstairs) with two buttons, Come on and Not now. Quiet hours cap the chime volume. A doorbell press wakes the receiving screen at the current brightness level. The plan has a small inconsistency here (Private wakes at evening brightness in one place), so settle it when building the banner.

## Radio protocol (ESP-NOW)

Every message starts with a version byte, a type, a sender id and a sequence number. Keep every message under 250 bytes. Retry up to 5 times over about 1.5 seconds until an ACK arrives. The receiver drops duplicates by sequence number. If every retry fails, the DOORBELL tile shows "Couldn't reach". Encryption uses ESP-NOW's built-in CCMP with a 16-byte master key and a 16-byte key per peer. The first version hard-codes both MAC addresses and the keys. Keep the keys out of any repository. Put them in an untracked config file.

| Message | Sent when | Contents |
|---|---|---|
| STATE | Status changes or expires, and at boot | State id, time it started, expiry, clock time |
| KNOCK | DOORBELL tapped | Knock id |
| REPLY | Knock answered | Knock id, Come on or Not now |
| ACK | Any message received | Sequence number echoed back |
| DOG | Dog tile tapped, or a new Lucy value arrives | Which dog is out (Lucy, Copper or none), time it started |
| READING | Temperature moves about 1 F or humidity about 3 points, and at boot | Temperature, humidity |
| LUCY | LucyGate button pressed (gateway to the upstairs unit) | Lucy in or out, time it started |
| OUTSIDE | WeatherGate sends a reading (gateway to the upstairs unit, which passes it on to downstairs) | Temperature (F), humidity, forecast code and phrase, reading time |

Boot exchange: a rebooted unit sends its STATE, and the other unit answers with its own STATE plus its DOG value and last READING. The units also copy the clock from each other's STATE messages. A panel shows dashes for the Outside tile if no OUTSIDE reading arrives for 15 minutes (starting value).

Time value of 0 (rule added 2026-10-30): the gateway sends a time of 0 until the Pi's clock has synced over NTP, because after a cold boot the Pi restores its last saved clock, which was about 24 minutes behind in testing. In every message type, a panel must treat a time of 0 as "no time": never set its own clock from it and never display it. Measure the 15-minute Outside staleness from when the panel received the reading, not from the reading's own timestamp, so a time-0 reading still counts as fresh.

The LUCY and OUTSIDE messages come from Mike's WeatherGate Raspberry Pi 2 through an ESP32-C6 gateway on its USB port. That gateway and the Pi-side bridge are Mike's work and are not part of the panel firmware. Their code is not in this repo.

Gateway status (Mike's test report, 2026-10-30):
- The gateway is working. The C6 shows up on the Pi as a USB serial device (`/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_...`, `ttyACM0`) with a udev alias `/dev/tcomm-gateway`. A Pi service, `tcomm-bridge`, talks to it. The serial ID includes the C6's MAC, FC:01:2C:FD:D3:A8. That should be its station MAC for ESP-NOW peering (unverified, confirm in code).
- Opening the port from the Pi does not reset the C6.
- Real LUCY and OUTSIDE messages reached Mike's 4.3C panel (the S3 board) and were acked: first contact in 5 ms (LUCY) and 48 ms (OUTSIDE), readings about once a minute acked in 22 to 41 ms, and a LucyGate button double-click OUT then IN acked in 6 and 26 ms. A day-long run started 2026-10-30, with no stuck replies so far.
- Not yet tested: the gateway's `failed` reply to the Pi when a panel doesn't ack. This is how the Pi learns a message didn't get through. Test it by unplugging the panel's power and pressing the LucyGate button.
- ESP-NOW between the C6 gateway and the S3 panel is confirmed: the receiver in these tests was the 4.3C panel. Since then the panel has run on wall power, unplugged from the laptop, updating weather and Lucy data from the gateway on its own with no failures (Mike, 2026-10-30). The firmware it runs for this is not in this repo.
- The C6 is now permanently plugged into the Pi. Test panel firmware against the real gateway rather than fake messages.

## Hardware facts to design around

These come from Waveshare's docs and my count of its pin tables. Verify against the schematic before depending on them.

- No free GPIOs. Extras go through the I2C terminal, the isolated inputs and outputs, or the IO expander.
- I2C bus: SDA GPIO8, SCL GPIO9, shared by touch (GT911), the RTC (PCF85063), the audio codec control and the external I2C terminal. Use one lock around all I2C access. Scan the bus on first boot and confirm no address clash with the SHT40 (0x44).
- Backlight: enable on EXIO2, PWM brightness through the expander. The number of dimming steps is undocumented.
- Audio: ES8311 codec and ES7210 ADC, I2S (MCLK GPIO6, data in GPIO15, LRCK GPIO16, data out GPIO43, SCLK GPIO44), amp enable on EXIO4. Speaker header is MX1.25 2-pin.
- microSD over SD_MMC. The card must be FAT32. The docs also give EXIO4 to the SD chip select. This looks like a double assignment. Check the schematic before using the SD card and the amp together.
- Touch reset on EXIO1. Isolated inputs DI0 (EXIO0) and DI1 (EXIO5), outputs DO0 (EXIO6) and DO1 (EXIO7).
- USB-C (GPIO19 and 20) is native USB for flashing and logs. Power is 5 V on USB-C or 7 to 36 V on the screw terminal. Current draw with backlight and speaker is unmeasured.
- Planned external sensor: SHT40 on the I2C terminal, on a cable a foot or two long, outside the case. Not confirmed present. Check the terminal's VOUT voltage before connecting it, because a resistor selects 3.3 V or 5 V.

## Firmware approach

- Start from Waveshare's Arduino examples for the 4.3C. They already bring up display, touch, backlight, audio and RTC. Waveshare also supports ESP-IDF, so keep application logic in separate modules that make no hardware calls, which keeps a later switch cheap.
- ESPHome is not the base. The community LVGL configs found cover the older 4.3, not the 4.3C.
- Layers: interface (LVGL screens and touch), app logic (status state machine, doorbell handling, expiry timers, dog value, brightness policy), radio module (encode, retry, deduplicate), services (sensor, audio, saved settings, RTC), board layer (drivers, expander, I2C lock).
- Put the state machine and message encoding in plain C++ with no hardware calls so they can be tested on a PC.
- LVGL draw buffers go in the 8 MB PSRAM. Interface, radio and audio run as separate tasks.
- LVGL rules from the design: flat solid colors only (no gradients, blur, glow or soft shadows), bold large type, simple geometric icons, animation limited to fades, slides and a progress ring. Fonts must be converted to LVGL fonts at every size used. Check flash use before adding sizes.
- Sky and sun/moon behavior needs sunrise and sunset times. Decide between a computed value from a configured location and a manual setting.
- Sound: four short mono files on the microSD (normal chime, soft chime, reply yes, reply no). Switch the amp off between sounds to avoid idle hiss.

## Build order and done-when

1. Bring-up. Set up the toolchain and run Waveshare's demos for display, touch, backlight, speaker, microphone, RTC and microSD. Scan I2C. Done when every demo runs on Mike's laptop and the flash path is repeatable.
2. Interface alone. All screens and states, DOORBELL tile, dog tile, status sheet, timer sheet, Undo bar, Settings, brightness schedule, night palette, with fake data. Done when every state can be set, night screens have been reviewed in the dark and a 24-hour run shows no glitches.
3. Radio. STATE, KNOCK, REPLY, ACK, DOG, READING (then LUCY and OUTSIDE) between two nodes. Done when 50 doorbell presses in a row arrive, unplugging one node shows No signal on the other the next time it sends, and range works through the floor.
4. Sound and rules. Chime set, volume by state, quiet hours. Done when the behavior table passes a scripted run on both units.
5. Sensor. SHT40 readings on screen and in READING messages with per-unit offsets.
6. Install. Mount, wire and run for 7 days with no unplanned reboots and no missed presses in the log.

## Working rules

- Confirm the COM port with Mike before every flash. Never erase flash without asking.
- Never commit Wi-Fi passwords, ESP-NOW keys or anything from Mike's other projects. Keep them in an untracked config file.
- Mark anything you could not verify as unverified in code comments and in your reports. Do not fill gaps with guesses.
- Ask before any purchase suggestion. Report what you changed and what you tested after each phase.
- Screens must work at 800x480 landscape with touch targets at least 90 px tall (except the 56 px gear and dog button).

## Open items to settle while building

- What the factory Settings app does to the clock, and whether the clock holds time without a coin cell.
- The Private and Sleeping wake brightness for a doorbell banner.
- Whether the downstairs panel shows a banner or tone when Lucy goes out (currently none).
- How the LucyGate in/out button is wired on the Pi. The existing button on that Pi cycles STANDBY, ARMED and ALERT for the carport sensor, so the new one needs its own input. This is Mike's side.
- The gateway's `failed` reply to the Pi (see Gateway status).
- Final forecast code list for the Outside icon (placeholders: sun, cloud with sun, rain, snow, moon).

## GUI design brief (paste below this line)

[Mike pastes the Claude Design brief here. It overrides this file on layout, colors, type and icons.]
