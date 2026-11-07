# Temperature requests and the tenant's comfort chart

Design only, written 2026-11-06 from Mike's answers, revised the same day after he went through the
mockups in `docs/temperature-mockups/` (drawn by `tools/temperature_mockups.py`). Nothing here is
built yet.

The downstairs tenant is new to the basement and has their own idea of comfortable. The thermostat
is upstairs and Mike runs it. This feature lets the tenant say how they feel and ask for more heat
or more cooling, and it collects their answers into a comfort chart of their own. Predictions from
that chart come later, only when Mike decides the data is good enough and a new build turns them on.

## Asking how they feel (downstairs panel)

1. A tap on the INSIDE temperature of the DOWNSTAIRS card opens the survey on the left side of the
   screen, top to bottom: MUCH TOO HOT, TOO WARM, JUST RIGHT, TOO COOL, MUCH TOO COLD.
2. Every answer is saved at once with the date and time, the downstairs temperature and humidity,
   and the other readings under "Data", whatever happens next.
3. JUST RIGHT closes the menu. No message, nothing sent.
4. Any other answer opens the request column on the right with the matching arrow already picked:

   | Answer | Arrow picked |
   |---|---|
   | MUCH TOO HOT | urgent cooling, two overlapping down arrows |
   | TOO WARM | mild cooling, one down arrow |
   | TOO COOL | mild heat, one up arrow |
   | MUCH TOO COLD | urgent heat, two overlapping up arrows |

   The column holds all four arrows (urgent heat at the top, urgent cooling at the bottom), drawn
   exactly as they'll appear upstairs, with no words. The tenant can tap a different one before
   sending. A card in the middle of the screen shows the chosen arrow with SEND over CANCEL.
   CANCEL still keeps the survey answer.
5. Each visit saves one answer. Tapping a different survey answer while the request column is open
   replaces the first one.
6. The survey buttons are coloured like the chart's dots. INSIDE carries no hint that it can be
   tapped: Mike tells the tenant, and the screen stays clean.

## The arrows (both panels)

They sit at the top right of the UPSTAIRS card's status area, where the mail badge sits on the
DOWNSTAIRS card. Both panels show the same arrows, so the tenant sees what Mike sees.

| Arrow | From the tenant: coloured arrow in a dark circle | From a prediction (later): the colour fills the circle |
|---|---|---|
| Mild heat, one up arrow | light red | light orange |
| Urgent heat, two up arrows, flashing | red | dark orange |
| Mild cooling, one down arrow | light blue | light purple |
| Urgent cooling, two down arrows, flashing | blue | dark purple |

Urgent arrows flash steadily: lit for half a second, dim for half a second.

- A new request while others are showing goes to the right of them. Three at most: a fourth pushes
  the oldest off.
- Upstairs, an urgent arrow wakes the screen the way the doorbell does and dings every 15 seconds
  until it's cleared. The ding sounds while Mike is BUSY, at a modest volume: the soft chime at
  BUSY's doorbell level. Each 15-second cycle wakes the screen again, so it stays lit while an urgent
  arrow shows.
- From 10 PM to 6 AM there's no ding, but an urgent arrow still lights Mike's screen at its night
  level, as a quiet sign if he's up. The ding follows the screen's dark hours in Settings, which are
  10 PM to 6 AM now.
- Mike clears them with one tap on any arrow, all of them at once. On the downstairs panel,
  "Action taken on temperature" then takes the place of the "since" line on the UPSTAIRS card, plain
  text in that card's own text colour, for 30 minutes or until the tenant taps it away.
- The tenant can tap the arrows on their own panel to take them back. They're gone at once from both
  panels; to ask again, the tenant goes through the survey.
- An arrow nobody clears goes away on its own after 4 hours (Mike said "some hours"; easy to change).

## The comfort chart (both panels)

A long press on the downstairs INSIDE temperature opens the chart, on either panel.

- It's a psychrometric chart: temperature along the bottom (55 to 90 °F), moisture in the air up the
  side (0 to 140 grains per pound), and curved lines for 20, 40, 60, 80 and 100 % relative humidity.
- The standard comfort zones are drawn in light gray, one for winter clothing and one for summer
  clothing (see the research below).
- Every answer ever given is a small dot at its temperature and humidity: dark red for MUCH TOO
  HOT, light pink for TOO WARM, dark gray for JUST RIGHT, light blue for TOO COOL, dark blue for
  MUCH TOO COLD. A thin dark ring keeps the light colours visible on the light background.
- A ring marks where downstairs is right now.
- Buttons along the top, ALL, NIGHT, MORNING, MIDDAY and EVENING, pick which answers are shown, one
  at a time, ALL by default, so the differences Mike suspects can be seen. The chosen button is filled
  dark (plum with cream text) with an outline around it; the others are light.
- Removing a mistake (downstairs panel only): tap near a dot and the nearest one is picked, with its
  date, time and answer, and a REMOVE button. A removed answer is marked removed in the file rather
  than erased, so it's never lost, and it's no longer drawn.

As built (2026-11-06): a short tap on INSIDE downstairs opens the survey and a long press opens the
chart, on either panel. The chart closes with the X or after 60 seconds without a touch, and frees
its memory. REMOVE is taller than in the mockup so a finger can hit it. At night the chosen button
is cream rather than dark, so it shows on the dark sheet. A removal reaches upstairs after any
answer still on its way, one message at a time; if the downstairs panel restarts while a removal is
still waiting to reach upstairs, upstairs keeps that dot until it's removed again.

Answers are kept forever on the SD card, like past calendar events. Each panel keeps its own copy
so either can draw the chart; a new answer or a removal goes to upstairs over the radio with the
usual retries.

## Data

Mike's theory: answers at night, in the morning, at midday and in the evening may differ, because
of bed covers at night, what people wear in the morning and during the day, and how active they
are. So each answer records when it was given, and what else was going on, to be studied later.

`/tcomm/comfort.csv` on the SD card (the folder the calendar uses), one line per answer, with a header
line first:

| Column | What it holds |
|---|---|
| n | the answer's number, 1 for the first ever, so the two panels' copies can be matched |
| date | 2026-11-06 |
| time | 21:47 (24-hour, local) |
| clock_sure | 1 if the clock was set from the internet or the other panel, 0 if not |
| part_of_day | NIGHT 10 PM-6 AM, MORNING 6-10 AM, MIDDAY 10 AM-4 PM, EVENING 4-10 PM |
| weekday | Mon to Sun (work days and weekends may differ too) |
| month | Jan to Dec |
| season | WINTER Dec-Feb, SPRING Mar-May, SUMMER Jun-Aug, FALL Sep-Nov (by month, as weather services count them) |
| inside_f | downstairs temperature, tenths of a degree |
| inside_rh | downstairs humidity, % |
| answer | MUCH_TOO_HOT, TOO_WARM, JUST_RIGHT, TOO_COOL, MUCH_TOO_COLD |
| arrow | what was sent (URGENT_HEAT, MILD_HEAT, MILD_COOL, URGENT_COOL) or NONE |
| sent | SEND, CANCEL, or blank for JUST RIGHT |
| status | the tenant's status at the time (OPEN, BUSY, PRIVATE, SLEEPING, AWAY): SLEEPING says they're likely under the covers |
| upstairs_f, upstairs_rh | upstairs reading at the time, near the thermostat |
| outside_f, outside_rh | outside reading from the weather station, blank if none |
| removed | blank, or the date and time the tenant removed it |

The part of the day is worked out from the time, so the boundaries can be changed later and the
old answers sorted again. Month and season are saved now for later study (Mike, 2026-11-06); nothing
uses them yet. The status, upstairs and outside columns weren't asked for; they cost nothing now and
can't be filled in afterwards. Ten answers a day for ten years is about 3 MB.

`/tcomm/requests.csv` (stage 2, downstairs) gets a line for each arrow when it ends:
`sent_date,sent_time,part_of_day,arrow,source,ended,ended_date,ended_time,waited_min`, where ended
is CLEARED (Mike tapped), TAKEN_BACK (the tenant tapped), PUSHED_OFF (a fourth came) or TIMED_OUT
(4 hours). That shows how long requests wait, by part of the day.

Radio: one message for a new answer or a removal, and one for the arrows' state (up to three, each
with its direction, mild or urgent, tenant or predicted) and the ACTION TAKEN message. Both use the
change counter the mail badge and the dogs already use, so a panel that missed one catches up.

## Predictions (later, not in the first build)

Not built until Mike decides there's enough data and a new build turns them on. He plans to collect
answers for weeks first. The method planned:

1. Combine temperature and moisture into one "feels like" number for this tenant, starting from the
   standard model's lean (about 0.45 °F per 10 grains per pound) and letting the tenant's answers
   adjust it.
2. Score the answers from -2 (MUCH TOO COLD) to +2 (MUCH TOO HOT) and find the four dividing lines
   between them along the "feels like" number, weighting recent weeks more. If the part of the day
   turns out to matter, each part gets its own lines.
3. When downstairs crosses the TOO WARM or TOO COOL line, send a mild predicted arrow; past MUCH
   TOO HOT or MUCH TOO COLD, an urgent one.
4. Draw the dividing lines on the chart, so the tenant's learned zone shows next to the gray one.

Guards to settle then: how many answers first, how long a reading must stay over a line, no
predicted arrow while any arrow is showing, and how often the panel may ask.

## Build stages

1. Survey and data: the survey menu, `answers.csv` with every column, the copy to upstairs.
2. Requests: the right-hand column with SEND and CANCEL, the arrows on both panels, the ding and the
   wake, clearing, taking back, the 4-hour timeout, ACTION TAKEN, `requests.csv`.
3. Chart: the long press, the chart, the part-of-day buttons, removing a mistake.

Each stage gets checks in `tests/test_core.cpp` (the arrow rules, the part of the day, the file
lines), PC screenshots, a capacity report, and a flash of both panels after confirming the ports.

## What the research says

### The standard comfort zone

ASHRAE Standard 55 draws comfort as a zone on the psychrometric chart for people sitting quietly
(about 1.1 met) in still air, where the PMV model predicts at least 80 % of people are satisfied
(PMV between -0.5 and +0.5). There's one zone for winter clothing (1.0 clo) and one for summer
clothing (0.5 clo), and it only goes up to a humidity ratio of 0.012 (84 grains per pound). Above
that the standard uses the full PMV calculation instead. Worked out here with the ISO 7730 PMV
method:

| Moisture | Winter clothing | Summer clothing |
|---|---|---|
| Dry air (0 gr/lb) | 70.9 to 79.0 °F | 77.1 to 82.9 °F |
| 28 gr/lb (about 25 % RH at 70 °F) | 69.7 to 77.7 °F | 76.2 to 82.0 °F |
| 56 gr/lb (about 50 % RH at 72 °F) | 68.4 to 76.5 °F | 75.3 to 81.1 °F |
| 84 gr/lb (the zone's top) | 67.2 to 75.3 °F | 74.4 to 80.2 °F |

The zones lean left: damper air makes the same temperature feel warmer. A 2020 addendum to the
standard replaced the drawn zone with the PMV calculation itself; the drawn zone is still the
familiar picture, and these numbers come from the same calculation.

### How much humidity matters

By the standard's model, going from 30 % to 60 % relative humidity at 72 °F feels about 1.5 °F
warmer. Lab and field studies find even less effect at ordinary room temperatures; humidity starts
to matter a lot above about 86 °F (30 °C). Dry air in winter causes dry eyes and static, but those
aren't about feeling warm or cold. So in a basement, temperature will decide most answers, and
humidity mostly matters in a damp summer.

### Clothing and activity move the zone more than humidity

Winter and summer clothing shift comfort by 5 to 6 °F. Activity matters too: the standard's zone
assumes sitting quietly, and moving about or lying under covers changes things. That's the reason
for recording the part of the day.

### Personal comfort models

What Mike described has a name in the research: a personal comfort model, built from one person's
own answers instead of the average person's. Kim and colleagues at UC Berkeley (2018) found
personal models predicted people's preferences correctly about 73 % of the time, against about 51 %
for the standard PMV and adaptive models in the same buildings. So the tenant's own chart should
beat the gray zone once it has enough answers.

## Capacity, estimated

- Flash: a few tens of KB for all three stages.
- PSRAM: no new screen-sized buffer. The chart is one object that draws its lines and dots as the
  screen is drawn, so it doesn't need 768 KB like a canvas would. Answers are read from the SD card
  when the chart opens (a few years fit in well under 100 KB) and freed when it closes.
- Draw time: one full redraw when the chart opens; thousands of small dots are fine.

## Sources

- [ASHRAE 55 (Wikipedia)](https://en.wikipedia.org/wiki/ASHRAE_55)
- [ASHRAE 55-2017 addendum d (removes the graphic comfort zone method)](https://www.ashrae.org/file%20library/technical%20resources/standards%20and%20guidelines/standards%20addenda/55_2017_d_20200731.pdf)
- [Kim et al. (2018), personal comfort models](https://escholarship.org/uc/item/9nh750dd)
- [Zuo, Luo and Liu (2021), humidity at 26, 30 and 37 °C](https://zendy.io/title/10.1111/ina.12739)
- [Buonocore et al., humidity and air movement in classrooms](https://labeee.ufsc.br/en/node/782)
