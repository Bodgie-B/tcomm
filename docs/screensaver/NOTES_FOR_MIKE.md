# Screensaver notes for Mike (overnight, 1 to 2 November)

Everything here is PC work. Nothing has run on the panel, nothing is committed, and nothing is pushed.
The pictures are in `docs/screensaver/reviews/house/`.

## Fast internal RAM: what it is and why the screensaver needs it

The panel has two kinds of working memory. Internal RAM is about 320 KB on the processor chip
itself. The processor reads and writes it at full speed. PSRAM is 8 MB on a separate chip, joined
to the processor by a bus. It's far bigger, but every access goes through a small cache, and when
the data isn't in the cache the processor waits for the bus. For data read once in a straight line,
like copying a finished picture to the screen, PSRAM is fine. For data touched over and over in a
scattered order, it's slow.

The screensaver draws each frame in 15 horizontal strips, each 800 by 32 pixels. Every shape (the
hiker's arms and legs, a leaf, a ripple on the stream) is blended into the strip pixel by pixel,
often several layers on the same pixel, and then the finished strip is copied to the screen in one
go. That strip is the busiest memory in the whole program, which is why Mark wanted it in internal
RAM. The same goes for the rasterizer's scratch, about 11 KB of small tables it uses for the edges
of every shape.

The catch: the strip alone is 51 KB, and the panel only has about 62 KB of internal RAM free while
it runs. Keeping it there all the time would leave almost nothing for the Wi-Fi time check and the
radio. And the 11 KB of scratch was a fixed table, so it would have taken internal RAM all day,
even with the screensaver off.

What I changed: LVGL, which draws the normal screen, has its own 64 KB drawing buffer in internal
RAM. While the screensaver runs, LVGL draws nothing, so that buffer sits idle. Now the screensaver
borrows it: 51 KB for the strip plus 11 KB of scratch, 62 KB in all, inside LVGL's 64 KB. When the
screensaver stops, it hands the buffer back. So the screensaver uses no internal RAM at all when
it isn't showing, and the rest of its state (21 KB, down from 72 KB) goes in PSRAM.

How much faster this makes the drawing on the panel isn't measured yet; that's the first thing the
panel test will show. On the PC everything is fast memory, so the PC can't tell us.

Checked on the PC: all of Mark's tests pass, and 12 Lookout frames are still identical to Mark's own
renders, pixel for pixel.

## The soak test

A soak test leaves a program running for a long time with realistic, changing inputs and checks
that nothing ever goes wrong: no crash, no loop that never ends, no glitch, nothing creeping slowly
off. Short tests catch the bugs that show up in the first minute. A soak catches the ones that need
hours to turn up, like a rare mix (a storm in a Lookout loop with a doorbell during the bear), a
counter that overflows, or something that drifts.

Mark's plan asked for an hour in the preview, checked by eye. It never got done. The PC runs the
screensaver hundreds of times faster than real time, so I had a tool written that runs 24
simulated hours in minutes and checks every frame automatically. The clock moves through day and
night, the weather changes, and statuses, dogs and doorbells arrive at random. The results are
further down.

What came out of it: three runs of 24 simulated hours each, 142 loops per run, every family
(evening, morning, midday, the Lookout) and all eight weather kinds, with statuses, dogs and
doorbells arriving all along. There were no crashes, no hangs, no story beat running over its time,
and every loop ended on time (9 to 11 minutes, the Lookout about 5). Running the same hour twice
gave the same pictures to the last pixel, so it's deterministic.

It found three small timing slips in Mark's animal moments, all in the evening, where a little
animal brushes past the hiker instead of keeping clear: the mouse running off as he comes back with
firewood, the chipmunk's diagonal dash clipping his boot, and the squirrel not re-checking its path
after he starts walking. They happen in Mark's original build too, a few ticks in a day. All three are
fixed now: in six more 24-hour runs no animal touched him. The animals now turn round, or scatter
a moment early, instead of running into him, so about four evening loops in ten play a little
differently in the small animal moments. The story itself is unchanged.

It also measured the drawing load: on average about 8 of the 15 strips change each frame, right at
Mark's budget, and about 10 in rain, snow and storms (the drops cover the whole screen). Whether
that's fast enough is the first question for the panel test.

## Mark's open notes, in plain words

Fixed tonight:

- The rule that stops the same pair of surprises coming back too soon skipped the rain-shelter dive.
  Mark's note blamed a memory too small to hold it; that had already been widened, but a leftover
  exception still let the dive dodge the rule. It's gone, and the build now refuses to compile if
  the surprises ever outgrow the memory again. Only storm loops change: their surprises vary more
  from one storm loop to the next (checked over 200 loops per case against Mark's build).

Waiting on the panel, because only the real screen can answer them:

- Rain and storms change about 10 of the 15 strips every frame, against Mark's budget of 8,
  because the drops cover the whole screen. If the panel turns out too slow for that, the fixes are
  fewer drops or drops that move every other frame.
- Every time the light changes (a sunset step, the morning fog lifting, lightning), the whole
  background is redrawn: 16 to 25 times in a storm loop, 26 to 37 in the morning. Each redraw takes
  a moment on the panel. Not measured yet.
- The colors on the real screen, the Lookout's orange sun, its one-pixel stars, the night tent and
  mushroom, and whether the small hiker on the tower reads at real size.

Your calls (Mark asked):

- A random storm (when no real weather has come in) never plans the rain-shelter dive. Should it?
- The stream surprises come up in about two loops out of three. Lower them?
- The Lookout sun's timing differs a little from the wording in your 5b request; Mark's version
  puts it 7/8 down as he sits, which is what you asked for in practice.

Small things Mark's reviewers noted and left:

- The Lookout tower is a bit short next to the hiker, there's no gust of wind as he walks up, and
  in one seed out of twenty the sun hides behind a cloud for 40 seconds on the way down.
- Midday ideas: a pair of boots by the tent, calmer lunch pacing, and the wind shouldn't pick up
  leaves that the story is using.
- Limits Mark accepted: the bear's third version runs 76 seconds, the pine tree doesn't shake, the
  squirrel's nut moment rarely fits, the ranger is only 8 pixels tall, and the little animals don't
  yet act on whether they're brave or shy.

## House reactions

Built tonight (pictures in `docs/screensaver/reviews/house/`):

- The status chip: the other floor's status, bottom right, in the same colors and Jost lettering as
  the panel's own status tiles, day and night. (Mark's brief listed older colors; the chip follows
  the panel instead, so it matches what you see all day.) It's 160 wide rather than the brief's 140,
  because NO SIGNAL doesn't fit in 140. Over 20 loops the scene's action never came into its corner.
- Dog badges beside it: one for one dog out, two for both. Lucy has an upright ear and a raised
  tail, Copper a soft ear and a lower tail. Which is which is my guess; tell me if it's backwards.
- The doorbell: the chip pulses, and the hiker jumps, turns to the chip, waves and points at it
  while the whole camp holds still to listen (3.4 seconds). With his hands full he just hops and
  looks. Afterwards everything carries on exactly where it stopped: the test compares a run with a
  bell against one without, and apart from the 3.4-second pause they match tick for tick. Out of
  frame, in the tent or at the Lookout, only the chip and the chime answer.
- No signal shows on the chip in its own gray.
- The house's real weather: the gateway's forecast, temperature and humidity pick the scene's rain,
  snow, storm, fog, heat or chill.
- The touch reveal: a circle opens from your finger and shows the main screen underneath, exactly as
  it would look without the screensaver.

Ideas for more, for you to pick from:

1. When Lucy or Copper goes out, the hiker's own camp dog trots in and plays fetch. Two dogs when
   both are out. This ties your camp-dog idea to the real dogs.
2. Mail downstairs: a bird lands on the tent with a letter, or a little mailbox by the trail raises
   its flag. Downstairs only, like the mail badge.
3. Small nods to the other floor's status. Sleeping: the hiker yawns and the owl comes out early.
   Away: a "gone hiking" sign on the tent. Busy: he's chopping wood. Private: he zips the tent shut.
4. The doorbell's reply. When the other panel answers "Come on", the hiker beckons; for "Not now",
   he holds up a hand.
5. Birthdays and holidays from the calendar: a balloon tied to the tent, a cake by the fire, a flag
   on the Lookout tower. The panel already has the holiday icons to match.
6. A missed doorbell while away: a note pinned to the tent until someone touches the screen.
7. Quiet hours: the fire burns lower and the hiker turns in earlier.
8. A heat or cold warning from the gateway: he fans himself with his hat, or hugs himself and his
   breath shows. Partly there already through the weather kinds.

## Your morning and daytime ideas

The plan is in `docs/screensaver/MORNING_DAYTIME_PLAN.md`, with a build order. A developer guide for
adding scenes, the instructions Mark never sent, is in `docs/screensaver/HOW_TO_ADD_SCENES.md`.
Sheets are in `docs/screensaver/reviews/morning_day/`.

- The camp dog and fetch: built. A brown dog about the hiker's knee height, with a floppy ear and a
  wagging tail. Three versions: a good game of fetch, the stick into the stream (the dog shakes off
  right beside him and he wipes his face), and the dog coming back dragging a branch three times its
  size. Fetch comes up in about a third of midday loops, and always when Lucy or Copper is out; with
  both out, a second, smaller dog joins in.
- The kite: built into the midday scenes, in about one midday loop in five. Three versions: a calm
  flight, the kite winning (it hauls him off the right edge and he walks back in without it), and
  three failed launches before a breeze lifts it out of his lap. It never flies in rain, snow, storms
  or at night. If random rain turns up after the kite was planned, he holds out a palm, shrugs and
  sits down instead.
- The boomerang: also in about one midday loop in five. It comes back and he catches it, or an eagle
  swoops in and carries it off, or it sticks in the pine behind him and stays there into the next
  loop too, like the shoe the raccoon steals.
- The "camp stays up" morning: built, in about six mornings out of ten (Mark's break-camp morning
  plays the rest, exactly as before). He wakes, the coffee boils over and he jumps back, then the
  morning pee: a glance at you, off behind the same big pine, a rustle, a squirrel bolts, and the
  raccoon slips in and walks off with one of his shoes. Breakfast is a pancake flip that lands in
  the pan, on his hat, or in a passing bird's beak. Then a cold wash in the stream (splash, teeth
  chattering, toothbrush, spit), a stuff sack that won't stay shut, and off on a day hike with the
  camp left standing.

One thing to know: adding the dog to the animal roster changes a label in the evening's internal
test record (which animals count as brave that loop). The evening you approved plays exactly the
same; only that list now includes the dog.
- The mountain bike, archery, the slackline and the ropes course: built and in the midday scenes.
  The bike rides through the stream and back, hits a rock and goes over the handlebars into the
  water, or holds a wheelie until he tips over backward under it. Archery is two misses then a
  bullseye, an arrow into the pine, or one arrow splitting another. On the slackline he wobbles and
  falls and tries again, or makes it across with a pose in the middle, and then a squirrel trots
  across it with ease while he watches with his hands on his hips. The ropes course rises out of the
  ground: a wobbly bridge, a tyre swing that leaves him dizzy, and a cargo net where his foot catches
  and he hangs upside down before dropping. A midday loop plays two or three of these, picked like
  the surprises. If the doorbell rings while he has the bow up, he only turns his head.
