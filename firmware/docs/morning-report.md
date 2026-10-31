# Morning report, Thursday Oct. 22

Phase 2 is written: the full Lookout interface running on demo data, the core logic, and the board layer underneath it. Nothing was flashed overnight. The firmware compiles cleanly for the board (919 KB of the 3 MB app slot, no warnings in our code with every warning turned on), the 234 host tests pass, and the PC simulator renders all 26 screens into `docs/screens/`. The work is committed to a local git repository. Nothing was pushed anywhere and nothing was bought.

Three independent reviewers went through the code after I finished it. They found 56 problems; I fixed all but the few listed under "Still open". The details are below the test steps.

## Trying it this morning

First, tell me the board is still on COM5. I won't flash until you confirm.

1. I flash the phase 2 firmware. Demo mode is on, so the other floor's panel, LucyGate and WeatherGate are all simulated inside this one board. The radio is off.
2. Watch the boot. The screen should stay dark until the first frame, then come up at the brightness for the time of day. With no clock set it uses night brightness on purpose (a power cut is often at night). The serial log should print `[tcomm] sounds ready in N ms` within a few seconds. I couldn't measure that on the PC, so I'd like the number. If the board reboots in a loop instead, the watchdog is firing and I'll need the log.
3. Set the clock, either from Settings > Time & Date or by typing `time 2026-10-22 07:30` in the serial monitor. The clock editor now starts at the build date, has a month column, and repeats while you hold a button. Until the clock is set, timers are blocked and the doorbell plays at the quiet-hours cap (80).
4. Use it like you would: statuses, the timer sheet, Undo, the doorbell, the dog pill, Settings.
5. Settings > Test drives the simulated side with eight buttons: the other floor's next status, unplug the other panel (you'll see No signal after your next change), the other floor rings you, the other floor's dog, LucyGate, the next WeatherGate reading, how the other floor answers your doorbell, and a sky phase override so you can see all six times of day without waiting. Serial `demo 0` to `demo 7` do the same eight things. `status 0` to `status 4` (Open, Busy, Private, Sleeping, Away), `bell`, `dog` and `info` also work.

Opening the serial monitor sometimes resets the board (we saw that in phase 1), so a restart when you connect isn't a fault.

What I most want your eyes and ears on:

- How long the screen freezes when the sky changes phase. On the PC the sky takes about 15 ms to draw. The reviewer estimated 0.7 to 1.3 s on the ESP32 with the drawing in PSRAM. If it's that slow, touches during a dawn or dusk change will lag, and I'll move the drawing to the other core.
- Whether the chime at 90 still sounds right through the firmware's sound path (it's the same synthesized Westminster HI you picked).
- Whether the pressed look on buttons reads clearly: the face drops onto its ledge and gets an outline.
- Holding the gear for 2 s opens cleaning mode. Touch is off for 30 s, and you finish early by holding "Hold to finish" for 1.5 s.

## What the review found and what changed

The reviews split the code three ways: core logic and messaging (22 findings), the interface (18) and the board layer (16). Each reviewer wrote test programs to confirm what they suspected, and most findings were reproduced rather than guessed. These are the ones that would have hurt:

- Building the sounds at boot could run past the 5 s task watchdog and reboot the board in a loop. The note synthesis no longer divides or calls library functions per sample, and it yields between notes. The timing on the board is still unmeasured, which is why I want the "sounds ready" number.
- With no clock (every power-up, since there's no coin cell), a dog could go out but never come back in, and a quick double tap could leave the wrong dog out. Dog changes now carry a change counter, and the later change always wins.
- Five things broke after about 24.8 days without a reboot, when the millisecond counter wraps: the doorbell stopped working, the Undo bar came back, the screen went to full brightness at night, and the "Timer ended" and "Missed" notices reappeared. Every timed flag is now cleared once it expires, and the tests run the counter past the wrap.
- Two fresh boards both default to DOWNSTAIRS and would never talk to each other once the radio is on. With the radio on, each board now takes its floor from its own MAC address, and the swap in Settings shows "(by MAC)" and is locked.
- An upward drag on any sheet scrolled the whole overlay layer, and it stayed scrolled.
- The interface redrew about 6.6 full screens a second with nothing changing, and 20 a second with the screen off. Only things that changed redraw now.
- If the laptop stayed plugged in with no monitor reading, every serial log line froze the screen for up to 2 s. Log lines are now dropped instead.
- An LVGL out-of-memory error froze the panel forever with nothing logged. It now reboots with a core dump, and the interface task is watched by the watchdog.
- The factory firmware's clock value read as the year 2055 and could spread to the other panel. The firmware now only accepts a year within ten years of the build.

Smaller fixes on the core side: a single bad sensor read no longer leaves dashes on the other panel for hours; the other panel gets the "since" time once the clock is set; a stale outside reading is no longer passed on as fresh; a late doorbell answer is still accepted after "Couldn't reach"; a LucyGate press with no timestamp isn't rejected; a missed ring with no clock still leaves its note; after an outage the panels resend readings as well as status and dog; the clock copied from the other panel no longer steps back by a second; and a protocol version mismatch is logged instead of looking like No signal.

On the interface: the doorbell tile's arch is now the round 125 px top from the drawing (it was flattened); the chevron was clipped by 3 px; the tile edges had a light halo; the dog pill covered the start of the date while it animated; the small sun sat behind the time; a change between day and night closed whatever sheet was open; choosing Custom on the timer hid the "then switch to" row; the swap dialog wasn't modal, and two taps made two dialogs; a wipe across cleaning mode's Done button ended it; volume had eleven levels where the brief says five (now 60 to 100 in steps of 10, shown as "4 of 5"); and the dog pill no longer shows a press when the handoff rule blocks the tap.

On the board side: settings writes moved off the interface task, so a status tap can't glitch the picture; each saved structure has its own version and range checks; the screen starts dark rather than at full white; the amp and the two isolated outputs are switched off at boot (they were left on); one failed sound no longer silences all of them; radio set-up errors are reported; and LVGL's memory comes from PSRAM so it doesn't compete with Wi-Fi.

The core reviewer also found three tests that passed without testing anything. Those are fixed, and the test harness can now drop single frames in either direction, which covers lost ACKs, late replies and duplicates.

After the fixes I checked every screenshot again and caught three layout problems the changes had caused: "INSIDE" overlapping the floor name, the volume value cut off, and the No signal line running under the badge. All three are fixed.

## Still open or unverified

- Nothing has run on the board since the bring-up sketch. Everything above is tested on the PC only.
- The ESP-NOW radio code compiles but has never run. That's phase 3.
- In phase 3, radio ACKs are handled in the interface task, so a slow redraw could delay them past the 1.5 s retry window. I'd move them to their own task then.
- The timer sheet and the clock editor keep their colors if day turns to night while they're open. That's deliberate: rebuilding them would lose the choice in progress.
- Waveshare's audio driver stops the boot if the unused microphone chip fails to start. It worked in phase 1, so I left their code alone.
- The simulator has no touch input. The reviewer built a version that does, in `%LOCALAPPDATA%\tcomm\host\review`, but it isn't part of the project.

## Questions for you

The eleven open questions from the two briefs are still in `docs/brief-conflicts.md`. Each has a default chosen and marked PROPOSED in `tcomm/src/core/settings.h` (or at the spot in the code), so nothing waits on them. The review added these:

1. A clock set from the network outranks a clock set by hand forever, even after weeks of drift. Should network time lose its rank after a while, say a week?
2. Which Wi-Fi channel are LucyGate and WeatherGate on? If they're on the home Wi-Fi they follow the router's channel, and the panels are fixed on channel 1.
3. The handoff gives LucyGate and WeatherGate the same sender id. If they're separate devices, they need separate ids, or one's message can be thrown away as a duplicate of the other's.
4. "Retry up to 5 times": the code sends 5 times in total. Did you mean 6 (the first send plus 5 retries)?
5. If the clock is lost while a Private or Sleeping timer is running, the timer can't expire. Keep that status until someone changes it, or switch to the fallback at boot?
6. A coin cell for the clock chip would stop the clock being lost in every power cut. Do you want me to look up the right part?
7. The two isolated outputs (DO0 and DO1) are now held off. Nothing in either brief uses them. Is anything wired to them?
8. Volume is now five levels from 60 to 100, with your 90 by day and the 80 night cap. Is 60 a sensible bottom?

Build, flash and test commands are in `README.md`. Screens are in `docs/screens/`, sounds in `docs/sounds/`, and the board facts we measured are in `docs/hardware-notes.md`.
