# The calendar roll-down

Built overnight on 2026-10-24 from the Claude Design spec "Signal Panel GUI: calendar roll-down
spec, all six times of day (boards V1 to V7)", flashed on 2026-10-25, then extended the same day
at Mike's request (the second half of this page). The boards are in `docs/design/boards/cal/`,
and side-by-side comparisons (the simulator on top, the board underneath) are in
`docs/design/compare/cal/`.

## What it does

- A tap anywhere on the date strip opens the calendar over the tiles; another tap closes it. It
  appears and goes at once: the spec's roll (360 ms down, 300 up) and the keyboard's slide ran at
  5 and 8 fps on the panel (2026-10-25), under the spec's 20 fps, so they're off. While it's down, an up chevron after the time shows how to roll it
  back; while it's up there's no chevron (Mike, 2026-10-25). The calendar needs the clock set;
  without it the strip does nothing.
- Month, Week and Day views, with the toolbar's previous and next buttons, Today, and the
  Month/Week/Day switch.
- An editor with an on-screen keyboard for TITLE and NOTES. A new event gets this unit's floor,
  with no prompt. An edited event keeps the floor it was made on.
- Delete, with Undo for 5 seconds.
- Both panels keep the whole calendar. A change is sent to the other panel at once. When they
  find each other after a gap (a boot, or a lost signal), they compare a summary of their
  calendars and send what differs. Nothing is sent on a timer.
- The calendar is saved to the SD card 2 seconds after a change (see "Storage" below). Each file
  is written as a new file and then renamed over the old one, so a power cut mid-write keeps the
  old one.

## Where it matches the boards

All 26 boards were rendered in the simulator and compared: the four views at all six times of
day on a downstairs unit, plus Month and the editor on an upstairs unit. Text positions, colours,
markers, pills and the keyboard line up to within a pixel or two. A new event on each unit gets
that unit's caret and floor with no prompt (checked in the simulator for both floors).

## Where the spec text and the boards differ (built to the boards)

1. Month cells show a short time after the title ("Dinner 6:30p").
2. The Week and Day bubbles have a 9 px corner radius, not fully rounded ends.
3. Cards and bubbles use the floor colours (up ink, down edge) for their borders, not "plum".
4. Save uses the accent fill.
5. The editor's header shows this unit's floor even when the event belongs to the other floor
   (the spec says so and CalEditUpGolden shows it). The event itself keeps its own floor.
6. The boards' sample events disagree with each other. The Week board shows one event on
   Thursday the 15th, the Day board three. Month has "Dentist 2p" and an all-day "Farmers mkt";
   Week has the dentist at 2:30 PM and the market at 9:00 AM. Each view was rendered with its own
   board's events.
7. The Day board has a 30-minute event, so timed events store their length in minutes. The
   PROPOSED data model had hours.
8. The Day view's count leaves out multi-day events ("3 events" on the board, with a trip too).

## PROPOSED items built (please say yes or no to each)

- It opens on today's month with today selected, every time.
- It closes after 90 seconds without a touch, except while the editor is up (so a draft isn't
  lost).
- A tap on the selected day's numeral in Month, or its header in Week, opens that day.
- Six-week months keep one lane per day; a day with more shows "+N", which opens the day.
- The Week view stacks up to four multi-day bubbles, and a day column with too many events
  scrolls. A one-day all-day event shows as a card reading "All day".
- In the Day view, a tap on an empty hour starts a new event at that hour. Today has a red line
  at the current time. The timeline opens at 9 AM unless the first event or the current hour
  would be out of sight.
- A new event starts at the next whole hour today, or 9 AM on another day, for one hour.
- No Delete button while creating an event.
- Delete gives a 5-second Undo.
- Add buttons have a touch area of at least 44 by 44 px.
- With "days" chosen, TIME reads "All day" in grey.
- An event, or the deletion of one, is dropped 180 days after it ends. The calendar holds 160
  events; when it's full, the oldest deletion makes room.

## Not built, or built differently

- The DATE, TIME and LENGTH pickers aren't designed, so they're stubs: a tap shows "Date, time
  and length pickers aren't designed yet". Until they are, an event can only be made on the day
  that was selected, at the default hour, one hour or one day long.
- These animations aren't built: the view cross-fade, the previous/next slide, the selection
  slide, the tap scale, the save pop, the delete fade and the editor's close. The roll and the
  keyboard slide are built.
- Overlapping events in the Day view are drawn on top of each other, not side by side.
- Timed events can't run past midnight (they end at midnight).
- When the date strip shows a holiday message that would run into the chevron, the message is
  hidden while the chevron shows.
- The long toolbar titles shorten when they'd run into Today: "Wednesday, Sep 30", and a week
  across the new year drops its first year ("Dec 27 – Jan 2, 2027").

## Conflict with the 90 px touch rule

The boards' keys are 40 px tall, the toolbar buttons 36 to 40 px, Cancel and Save 32 px, Delete
28 px, and the event rows in Month 18 to 22 px. That's well under the 90 px rule (the gear and
the dog button are the agreed 56 px exceptions). I built them as the boards show. Please try
typing and tapping events on the panel and tell me if any of them need to grow.

## Adversarial review: what it found and what changed

After the build I went through the calendar code looking for ways to break it, and drove it by
touch in the simulator (tap, type, save, delete, Undo, close mid-edit, a light change mid-roll,
the auto close). Fixed:

1. **Crash:** closing the calendar with the editor up deleted the editor twice. The simulator
   crashed on it. Delayed deletes are now cancelled when the whole calendar goes.
2. **Crash:** the toast's timer freed itself after the first toast, and the next toast (for
   example "Event deleted" after "Add a title first") wrote into freed memory. The touch test
   crashed on it. The timer is now kept and paused.
3. **Stuck open:** a change of light (for example dusk starting) while the calendar was rolling
   deleted the moving panel with its animation. The calendar then thought it was still rolling
   and couldn't be closed. A light change now waits for the roll to finish.
4. **Deleted events coming back:** deletions were forgotten after 30 days, so a panel that had
   been unplugged longer could bring a deleted event back. Deletions are now kept as long as the
   event itself would be (180 days after it ends), and old ones from the other panel are refused.
5. **Memory leak (demo mode):** the simulated second panel leaked about 60 KB of PSRAM each time
   it was "plugged in" from Settings > Test. It now frees its calendar.
6. **Lost change:** a calendar change dropped because the send queue was full was never sent
   again. A dropped change now makes the panels compare calendars as soon as there's room.
7. **Lost draft:** the 90-second auto close discarded an unsaved draft. It no longer closes with
   the editor up.
8. The toast sat over the keyboard's bottom row while typing. It now sits above the keys, and
   no toast blocks a touch.
9. Long titles ran under the Today button. They now shorten (see above).
10. Found in the board comparison: long event names wrapped onto a second line instead of
    ending in "…", and the keyboard's letters sat at the left edge of each key. Both fixed.
11. The bubbles' pictures are freed when the calendar rolls up, so it holds no extra PSRAM while
    closed.

Known and accepted: if the calendar is full and the oldest deletion is reused before the other
panel heard about it, that event can come back. Both panels must run this firmware: older
firmware doesn't know the calendar messages and won't acknowledge them, so it would show
No signal.

## Tests

- 532 host checks pass, including the event store (ordering, merging, pruning, the file format
  and a damaged file), sync between two controllers that were cut off and reconnected, and save
  and reload.
- The simulator renders the 26 board scenes, a new event on each floor, a touch-driven run
  through the calendar (all checks pass), and edge cases: the longest titles and a six-week
  month.
- The firmware compiles. Size and memory are in `docs/capacity.md`.

## UNVERIFIED until it runs on the panel

- The first boot with this firmware formats the empty FAT partition. How long that takes is
  logged ("calendar: format done in N ms").
- How long a calendar save stalls the screen and the sound. Each save is logged ("calendar: N
  bytes saved in N ms").
- The roll's frame rate. Each animation is logged, for example "calendar roll down: 11 frames
  in 360 ms, 30 fps". The spec says an animation under about 20 fps goes; I'll remove any that
  is.
- How the 40 px keys feel to type on.

## Trying it on the panel

1. Tell me the panel is on COM5; I won't flash until you confirm. The first boot formats the
   empty 9.9 MB FAT partition (there's nothing on it; settings live elsewhere and aren't
   touched).
2. Tap the date strip. The calendar should roll down; tap it again to roll it up.
3. Add an event from the Day view's ADD EVENT, or the add button on a selected day. Type a
   title, Save. Tap it to edit; try Delete and Undo.
4. Switch views, step back and forward, tap Today.
5. Unplug the panel and plug it back in: the events should still be there.
6. Serial `info` now also prints the slowest redraw and the calendar's slots and file size.

---

# Added on 2026-10-25

Mike tried the flashed calendar, then asked for the pickers, a repeating-event menu with
birthdays and anniversaries, US federal holidays, no down chevron, and a Zambretti demo in
Settings. His answers to the questions asked first:

| Question | Mike's answer |
|---|---|
| Pickers | A month grid for the date; steppers for time and length; buttons at least 90 px |
| Changing one day of a series | Ask each time: "Just this one" or "All of them" |
| Birthday text on the strip | At the right end, where holiday messages go |
| Count the years ("turns 40") | No, just the name |
| Holiday icons | One per holiday |
| Second Monday of October | Columbus Day |
| The overnight PROPOSED items | Keep them all, except: keep past events for good (no 180 days) |
| Keyboard size | Keep the boards' 40 px keys |
| Where to keep past events | The SD card ("it will not be removed") |
| Formatting the card | Yes, as FAT32, if it won't mount as it is (from a serial command, once) |

## The pickers

- **DATE**: a month grid over the editor, with previous and next month. A tap on a day picks it.
  Cancel leaves the date as it was.
- **TIME**: hour and minute (5-minute steps) with up and down buttons (hold to repeat), AM and
  PM, and the start and end shown under the title. Picking a time makes an all-day event a timed
  one, an hour long.
- **LENGTH**: minus and plus (15 minutes, or a day), and the usual lengths: 30 min, 1, 2 or 3
  hours, or All day; for an all-day event, Timed, 1, 2 or 3 days, or a week.
- The buttons are 90 px tall. The date grid's days are 46 px (six rows of a month don't fit
  otherwise), like the boards' keys.

## Repeating events

- The bubble with two circling arrows under LENGTH, on the right, reads "Repeating Event" or the
  rule ("Weekly, until Dec 31", "Monthly, 10 times", "Birthday, every year").
- Its menu: Doesn't repeat, Daily, Weekly, Monthly, Yearly, Birthday, Anniversary; and the end:
  never, on a date (the date grid again), or after a number of times (minus and plus).
- Monthly on the 29th to 31st skips months without that day. A February 29 birthday is on the
  28th in other years.
- Changing or deleting an occurrence asks "Just this one" or "All of them". Just this one takes
  that day out of the series, and a changed one becomes an event of its own (on the series'
  floor). All of them changes the series; moving the occurrence's date moves the whole series by
  as much. A series can have 24 single changes. Undo works for both kinds of delete.
- **Birthdays and anniversaries** repeat every year and are all-day. The editor calls the title
  NAME. They show a cake or two rings in place of the floor caret in the Month, Week and Day views
  (still in the floor's colours). On the day, the date strip shows "Happy Birthday, Sam" or
  "Happy Anniversary, Mike & Jo" with a cake or rings, or just the name when the full words
  don't fit.

## US federal holidays

- Worked out for any year: New Year's Day, Martin Luther King Jr. Day, Presidents' Day (the law
  calls it Washington's Birthday), Memorial Day, Juneteenth (from 2021), Independence Day, Labor
  Day, Columbus Day, Veterans Day, Thanksgiving and Christmas. On the real date, not the observed
  weekday when it falls on a weekend.
- Month view: a green bubble with the short name beside the date (shorter on the selected day,
  where the add button sits). Week view: a green bubble in the day's first box under the date.
  Day view: a green bubble with the full name under the count of events.
- The date strip: the holiday's message with its own icon at the right end: fireworks (New
  Year's), a dove (MLK Day), a flag (Presidents', Memorial, Juneteenth, July 4th, Veterans), tools
  (Labor Day), a ship (Columbus Day), a maple leaf (Thanksgiving), the holly (Christmas). Several
  messages on one day (a holiday and a birthday) take turns every 8 seconds. With the dog out
  the strip is narrower: the words step down to a smaller size, then to the short name, then to
  the icon alone. `docs/screens/strips_holidays.png` shows them all.
- The boards' Monday, Oct. 12, 2026 is Columbus Day, so every board scene now shows its green
  bubble and its strip message; the boards don't.

## Storage: the SD card, and past events kept for good

- What's upcoming, what repeats, and the last 92 days stay in memory (240 events at most).
  Anything older moves to a file per month on the card (`/tcomm/2026-10.bin`), or `series.bin`
  for a series that has ended, and is read back when the calendar shows that month. Nothing is
  ever deleted. A deletion is kept too, so a deleted event can't come back from the other panel.
- If the card can't be mounted, the files go on the panel's FAT partition instead, and are
  copied onto the card the next time it mounts. The calendar made this morning (on the FAT
  partition) is read from there and copied to the card.
- This firmware reads FAT32 cards, not exFAT. Serial `sd` says what the card is; `sd format yes`
  formats it as FAT32 only if it can't be mounted as it is.
- When the store fills with recent events, the one that ended longest ago goes to the archive
  early to make room.
- Both panels archive by the same rule, and an old event that arrives from the other panel goes
  straight into this panel's archive. Not covered: if a panel is off for more than three months,
  events the other panel archived meanwhile don't reach it.

## Also changed

- Overlapping events in the Day view now sit side by side.
- The editor's DATE shows the occurrence being edited.

## Not built

- Repeat every 2 weeks (or any other interval).
- "This and following" for a series (only "just this one" and "all of them").
- Ages and years for birthdays and anniversaries (Mike: just the name).
- The observed weekday for a holiday that falls on a weekend.

## Fixed in review

- A bordered, slightly see-through box (the series prompt) drew with no fill in LVGL; it's
  opaque now, and so are the pickers.
- A file-copy order bug: the morning's calendar could have been copied onto the card over a
  newer one from the FAT partition.
- The lock for the card's write queue was made inside a spinlock, where the ESP32 can't allocate
  memory; it's made at start-up now.
- A failed "just this one" said "That event was deleted"; it now says it couldn't change just
  that one.

## Tests

- 591 host checks pass, including repeats (weekly, monthly on the 31st, daily, February 29,
  multi-day weekends, counts, end dates, skips), the holidays for 2026 and 2027, the archive
  (written at the day's start, read back, changed, deleted, filled by the other panel, making
  room), "just this one" between two panels, and a birthday's strip message.
- The simulator's second run by touch passes: the time picker, the repeat menu, saving a weekly
  event, a week later "just this one", and the event that takes its place.

## Measured on the panel (2026-10-25)

- The roll: 3 frames in 584 ms down, 3 in 528 ms up (5 fps). The keyboard: 2 frames in 232 ms
  (8 fps). Both are now off (spec: under about 20 fps, an animation goes).
- Saving the calendar to the SD card: 170 bytes in 24 ms.
- The SD card mounts at boot in 76 ms.

## UNVERIFIED until it runs on the panel

- How long a bigger save takes (each is logged).
- How long reading a past month's file takes when the calendar shows it (logged when over
  20 ms).
