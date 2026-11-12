# Presence sensor (upstairs panel)

Written 2026-11-11 (overnight stage 4). One plain on/off presence sensor on the UPSTAIRS panel, at the
place where the hallway opens to the kitchen and living room (not in the office). The sensor's model is
not known yet, so the firmware treats it as a switch: it either says "someone is there" or it doesn't.
Nothing below happens until you turn it on in Settings (the default is off).

## Wiring

**Mike's sensor (2026-11-12): an HC-SR501 type PIR board. Wire it as drawn in
`presence-wiring.svg` (a PNG copy sits beside it).**

What the V1.1 schematic shows for the input (checked 2026-11-12, this replaces the open questions
further down): DI0 goes through a two-way optocoupler (PC814) and a 3.6 kΩ resistor to DI COM, and
the board itself feeds DI COM from its own 5 V through a diode and 220 Ω. So nothing has to be
connected to DI COM: joining DI0 to the panel's GND turns the input on (about 0.9 mA flows), and the
pin then reads LOW. The pin is pulled up to 3.3 V with 4.7 kΩ, so an open input reads HIGH.

The PIR board's OUT pin gives 3.3 V on movement and 0 V at rest. That can't drive the input by
itself in the right direction with any margin, so one small NPN transistor (2N3904 or similar) sits
between them:

- Sensor VCC to terminal 10 (5 V). The board takes 4.5 to 20 V and well under 1 mA.
- Sensor GND and the transistor's emitter to a GND screw (8 in the drawing; 7, 8 and 14 are the
  same GND).
- Sensor OUT through a 10 kΩ resistor to the transistor's base.
- The transistor's collector to terminal 15 (DI0).
- Terminal 13 (DI COM) stays empty.

Movement turns the transistor on, which pulls DI0 to GND, so set **Active level: LOW**. Wired like
this the sensor shares the panel's ground and 5 V, so it isn't isolated, which doesn't matter for a
sensor a short wire away.

On the PIR board: jumper on H (it stays on while movement continues), the time dial at its
shortest (the panel adds its own hold time), the distance dial in the middle to start. It needs
about a minute after power-up before it settles, and it ignores movement for 2 to 3 seconds after
each time it switches off. These boards can false-trigger close to a Wi-Fi or ESP32 radio: if
"Seen now" comes up with nobody about, move the sensor a foot or so from the panel.

The original notes, before the sensor was known:

The panel's screw terminal P1 has an "Isolated I/O" group (positions 11 to 16, from `hardware-notes.md`):
11 DO0, 12 DO1, 13 DI COM, 14 GND, 15 DI0, 16 DI1. The sensor goes on **DI0 (15)** and **DI COM (13)**.
DI0 is expander pin EXIO0, read through the optocoupler's output, so the sensor is electrically
separate from the panel's own circuits: the sensor's supply and the panel's supply don't have to share
a ground. The sensor needs its own power (the panel's 5 V pins, positions 9 and 10, or 3.3 V on 3, can
feed a small sensor if its current is low, but then the isolation is gone for that wire).

What I could not confirm from the notes and the schematic text we have:

- The voltage range the isolated input expects. The notes name the terminal and the pin only. A typical
  isolated input of this kind takes about 5 to 24 V through a resistor and an LED; check Waveshare's
  schematic for the resistor before connecting a sensor that outputs more than 5 V.
- Which way current flows through DI COM (whether COM is the return for DI0 or the supply side), and
  therefore whether the pin reads high or low when the input is driven. That is why Settings has an
  "Active level" choice.
- Whether the input has its own pull-up inside: it is an isolated input, so nothing on the panel side
  of the optocoupler needs one, but the sensor side may.

What you must check on your sensor before wiring it:

1. Its output type: relay contact (dry, needs its own voltage in series), open collector / NPN (needs
   a pull-up to the supply), or a push-pull voltage output (drives high and low by itself).
2. The voltage of the output when it triggers, and whether that is within what DI0 takes.
3. Whether it needs its own supply (most PIR and mmWave modules do: 5 V or 12 V), and whether that supply
   is common with DI COM.
4. Its own hold time: many sensors stay on for several seconds after movement. The panel adds its own
   hold time on top (below), so a short one on the sensor is fine.

## Turning it on

Settings (gear), the PRESENCE SENSOR tile (upstairs panel only):

- **Fitted: YES** switches the features on. NO (the start value) means nothing changes: the panel
  never reads the input and every feature below is off.
- **Active level: HIGH / LOW**: the level read from the input that means "someone is there". If the
  line at the top of the page says "Seen now" with nobody about and the sensor idle, flip this.
- **Hold**: how long after the last activity the place counts as occupied, 10 to 600 s in 10 s steps
  (start value 60 s).
- The line at the top shows the sensor live: "Seen now", "Last seen N min ago", or "Not seen since
  start". Wave at the sensor to check the wiring.

The settings are saved (version 6 of the saved settings; an older saved layout is read and keeps
everything: floor, ringtone, volume, quiet hours, screen hours and the rest).

## What it does

The panel reads the input about every 100 ms from its background task (one I2C read; a failed read
changes nothing, and 3 s of failed reads count as nobody there) and needs two reads in a row to agree
before it believes a change.

1. **Away from desk.** While your status is Busy, activity at the sensor shows "Away from desk" in
   place of the "since" line on your tile, on both panels (you are Busy but walking about the house,
   not in your office). It turns on when activity is seen while Busy and stays on until 5 minutes pass
   with no activity (`kAwayClearMs`), or until your status is no longer Busy. It sits below a typed tile
   message and the "Action taken on temperature" line, and above the calendar item. A Busy timer's
   "until 3:30 PM" line is kept, because the end time is worth more to the tenant. It reaches the
   downstairs panel in one spare bit of the STATE message (see the wire change below).
2. **Knock routing.** The upstairs panel tracks how long it is since anyone was near it: activity at
   the sensor or a touch on the screen. When that reaches 15 minutes (`kNobodyNearMs`), it tells the
   downstairs panel (a second STATE bit). If the tenant then rings, the downstairs panel shows "No one
   is near the panel" in the toast bubble at the top of its screen while the ring waits (at most 15 s,
   and it goes when you answer). The tile has no room for a second line, so the toast carries it.
   The upstairs side rings exactly as before: the only sound options that exist are the volume and its
   quiet-hours cap, and raising the volume above the cap at night would break your quiet hours, so
   nothing new was added for sound.
3. **Night path.** With "Screen at night: off overnight" on, and during its dark hours, activity at
   the sensor brings the screen up to your night light level (Settings > Night light) and keeps it
   there for 30 s after the last activity (`kNightWakeMs`), then it goes dark again. No sound, and
   nothing happens to a running screensaver beyond what the screen being lit shows. A touch behaves as
   it always did (full light for the touch-wake time). If the screen is only dim at night (not off),
   there is nothing to do and nothing changes.
4. **Passer-by for the screensaver.** The View has `passer_by`: true for 3 s each time someone
   arrives after the place was empty (not seen for the hold time), at most once every 2 minutes, on the
   upstairs panel only. The screensaver's hiker waves at it.

## Wire change

One thing on the radio: two spare bits in the STATE message's flags byte, `kStateAwayDesk` (0x04) and
`kStateNobodyNear` (0x08). STATE did not grow. A panel with no sensor sends both as 0, and an older
firmware ignores them, so the two panels can be updated one at a time. STATE goes out when either bit
changes (no timer).

## Testing

Serial commands (115200 baud, on the upstairs panel):

- `presence` prints whether it is fitted, the raw level read from the pin (high or low), the
  debounced state, whether the place counts as occupied (within the hold time), how many reads failed,
  and how many minutes since it last saw anyone.
- `presence sim 1` forces "someone is there"; `presence sim 0` forces nobody; `presence sim off` goes
  back to the real sensor. The sensor must still be switched on in Settings.

A walk-through: switch it on in Settings, set Busy, `presence sim 1`: your tile on both panels reads
"Away from desk". `presence sim 0`: it goes after 5 minutes. For the knock note, leave the panel alone
for 15 minutes (no touch, no activity), then ring from downstairs. For the night path, turn on "Screen at
night: off overnight", wait for the dark hours (or set the hours to include now), and `presence sim 1`.

The host tests cover the timings, the priority on the tile line, both STATE bits reaching the other
panel, not-fitted meaning no change, the night path, the passer-by limits, the debounce filter, and
the version 6 settings migration (`tests/test_core.cpp`, the functions starting `test_presence`).
