# Campsite Screensaver: Build Brief

This brief is for whoever builds the screensaver. The current plan is mtburbridge-creator working with Claude (Fable) in Claude Code. It covers what the screensaver is, how it should feel, the content to draw from, the hardware it runs on, how it fits into the TComm panel firmware, and the decisions Mike has already made.

How to read it:

- Sections 1 to 9 are the creative intent. Treat the scene lists as a menu, not a script. The pacing rules matter most.
- Sections 10 to 16 are the engineering frame. These are firm unless they're marked as proposed or unverified.
- The goal is a screensaver that is charming, funny, rarely repeats, looks polished and uses very little of the panel's resources. Use your own judgment on everything this brief leaves open, and invent beyond the idea pools. Mike wants to be surprised by it.

Labels used below:

- **Decided:** Mike chose it.
- **Proposed:** a starting value from the person writing this brief; change it if there's a reason.
- **Unverified:** not confirmed on the hardware or in the docs.

## 1. What it is

A wordless, slow animated story that plays when the panel has been idle for a while. A nature scene builds itself from an empty frame, then a hiker walks in, sets up camp, and goes about his evening while small animals watch him. Most of the time very little happens: wind in the grass, a moving stream, a bird crossing the sky. Every so often a bigger moment lands, like a squirrel spinning off a branch into the stream, or a bear going after the food bag while the hiker's silhouette watches nervously from inside the lit tent.

The tone is a Pixar short: warm, a little funny, never mean. Quiet moments (a sunset, a good book, a finished rock cairn) get as much weight as the gags.

Everything is drawn in code from simple shapes. No video, no frame sequences, no Lottie, no bitmap sprite sheets. The binary should hold kilobytes of animation logic, not megabytes of assets.

## 2. Visual direction: Firewatch

**Decided.** The whole TComm panel theme was inspired by the game Firewatch, and the screensaver should look like it belongs in that world. Work from the general art style only. Don't reproduce the game's actual artwork, characters, logo or specific scenes.

What that style means in practice:

- **Flat, layered shapes.** The landscape is stacked silhouettes (ridges, tree lines, rocks), each layer one flat color. Depth comes from layering, not from shading or texture.
- **Atmospheric perspective.** Each layer farther back moves closer to the sky color, lighter and lower in saturation. Near layers are darker and richer. This one rule does most of the work of making flat shapes read as deep space.
- **Strong, limited palette per time of day.** Each mood has a small palette (around 6 to 10 colors), often a warm and cool pairing such as burnt orange against teal, gold against violet, or deep blue against amber firelight. The palette changes with time of day and weather. The shapes stay the same.
- **Stepped skies.** The sky is horizontal bands of flat color, not a smooth gradient. That fits the style and costs almost nothing to draw.
- **Graphic silhouettes.** Pines are clean stacked triangles or jagged shapes, not detailed trees. Characters read from their silhouette first. The hiker, bear, deer and others should each be recognizable as a black shape.
- **Light as color, not effects.** A fire is a warm color pool on nearby shapes, done by switching palette on the affected layers or drawing flat light shapes. Glow and blur are not used. The sun can be a flat disc or implied by lit cloud edges.
- **Clean edges.** Shapes need anti-aliased edges (see section 12). Jagged stair-step edges are what would make this look cheap.

The screensaver has its own palettes and does not use Framework 1's colors. The one exception is the status chip (section 11), which keeps the Framework 1 status colors so it reads the same as the main interface.

### Starting palettes (proposed, tune by eye on the actual panel)

These are starting points to get the mood right, not final values. IPS panels in RGB565 shift some colors, so check every palette on the real screen.

| Mood | Sky bands (top to horizon) | Far ridge | Mid trees | Near ground | Accent |
|---|---|---|---|---|---|
| Day | #4F86A8, #6E9FB8, #94B9C6, #C3D3C9 | #7FA3A8 | #3E6658 | #2C4A3C | #E8C170 (sun) |
| Golden hour | #D9774A, #E89A55, #F2BC6B, #F7D58C | #B8705A | #6B4A45 | #3A2E35 | #FFE3A3 |
| Dusk | #3B3A6B, #6C4A7A, #B5607A, #E58A6B | #5A4466 | #33304D | #201E33 | #F2A65A (fire) |
| Night | #0E1A2E, #13233B, #1A2D47, #22385A | #1B2A40 | #121D2E | #0B1420 | #E07B39 (fire), #FFF1C2 (star) |

The night palette also has to follow the night rule in section 11 (dim, no large bright areas).

## 3. Core principles

1. **Procedural only.** Trees, tent, hiker, animals, fire, water and sky are lines, circles, polygons and filled bands computed at runtime.
2. **Tiny footprint.** Scenes are data (timelines, positions, palette indices, shape lists) plus shared drawing routines.
3. **Slow play.** It isn't a string of gags. Long quiet stretches, frequent small motion, rare big moments.
4. **Animals behave; they don't follow a script.** Each critter reacts to the hiker's distance and state, which makes them feel alive and gives variation for free.
5. **Vary every loop.** A random seed per loop changes which gags fire, which item gets stolen, bird timing, story variants and so on. Real weather and real time of day set the base mood (section 11).
6. **Polish over quantity.** Fewer scenes with good timing, easing and silhouettes beat many rough ones. A smaller set that looks finished is the better result.

## 4. Pacing model

Three layers run at once, each at its own speed.

| Layer | Frequency | Examples | Purpose |
|---|---|---|---|
| Ambient | Always on | Grass sway, cloud drift, stream flow, fire flicker, distant bird | Keeps the scene alive without asking for attention |
| Minor effects | Every 10 to 30 s | Leaf falls, chipmunk crosses, moth circles lantern, bee visits flower | Small rewards for anyone glancing at the screen |
| Major effects | 2 to 4 per loop | Falling branch, storm, bear, squirrel splash | The memorable moments |

Rules:

- Leave real pauses. Stretches of 20 to 40 s where only the ambient layer moves are what make it calm. Without them it reads as busy.
- Randomize the gaps between majors so the viewer doesn't feel a clock. Don't put two majors close together unless one is a deliberate follow-up (rain, then wet wood and a smoky fire).
- Give each major a beat of stillness before and after.

## 5. Content bins

All content goes into four bins. The loop plays the spine in order, fills gaps with calm moments and motifs, and drops in a surprise now and then.

| Bin | What it holds | How it plays |
|---|---|---|
| Story spine | The hiker's arc for this loop | Once per loop, in order |
| Motifs | Recurring small bits (chipmunk carrying things, burrs on socks, the frog) | Several times per loop |
| Calm moments | Quiet appreciative beats | Fill the long gaps |
| Surprises | Random majors from a pool | Occasional, randomized |

## 6. Story spine (default camp loop)

Roughly 3 to 5 minutes.

1. **Build.** The scene builds with no people. The sky fades in. Background trees stand up from the ground with an overshoot and settle. A stream rolls in from right to left. Small plants pop up in the foreground, the last one a mushroom.
2. **Arrival.** The hiker walks in from the left, stops, looks around, and decides this is a good spot.
3. **Camp setup.** He unpacks, and the tent goes up in stages (bent peg, so he tries a rock instead).
4. **Firewood.** He leaves the frame to gather wood and comes back with an armload.
5. **Fire.** He builds it, blows on it, gets burned, yelps, and cools his hand in the stream.
6. **Dinner.** He cooks and eats while animals edge closer and scatter when he moves.
7. **Food bag.** He hangs it from a tree branch and goes into the tent.
8. **Bear.** A bear tries several ways to reach the bag (climbing, shaking the trunk, reaching with a branch). The hiker's shadow inside the lit tent watches nervously.
9. **Close.** Something small gets stolen, the scene goes to night, fades, and the loop restarts with a new seed.

Each beat should have two or three variants (a different setup mishap, a different thief, a different bear tactic) so the spine itself doesn't repeat exactly. The time of day and weather set by real data (section 11) should change which spine variant fits. For example, a morning loop might open with him waking in fog and making coffee, then breaking camp instead of setting it up.

## 7. Major scene: "Lookout"

**Decided:** the Lookout plays only around real sunset, in place of the camp spine. **Proposed** window: starting from about 30 minutes before local sunset to 15 minutes after. Section 11 covers how sunset time is computed.

It's a long, quiet set piece, about 3 to 4 minutes, mostly slow motion and stillness. A fire lookout tower on a ridge at sunset fits Firewatch especially well. Design an original tower, not one from the game.

| # | Beat | Duration | Notes |
|---|---|---|---|
| 1 | Approach | ~30 s | The hiker walks a switchback trail up a hillside, getting smaller as he climbs. Trees thin out and the wind picks up slightly. |
| 2 | Pause on trail | ~8 s | He stops, breathes, looks up. A hawk drifts across the top of the frame. Nothing else happens. |
| 3 | Tower reveal | ~12 s | A timber fire lookout rises above the ridge, silhouette first, then detail. Its windows catch the low sun. |
| 4 | Climb | ~40 s | Zigzag stairs, deliberate steps. He pauses at a landing to look over the valley. A bird on the railing leaves when he moves. |
| 5 | Explore the top | ~50 s | He walks around the cab, peeks through windows, touches the fire-finder table and taps the compass plate. A chipmunk watches from a beam. |
| 6 | Settle | ~20 s | He sits at the west railing, legs over the edge, arms on the rail. |
| 7 | Sunset | 90 to 120 s | The sky moves through gold, orange, pink and violet. The valley falls into shadow layer by layer. Horizon clouds hold color last. One palette step about every 10 to 15 s. |
| 8 | Birds | During sunset | One bird every 15 to 25 s (random), at a different height and speed each time. Sometimes a pair, and once a pair circles and lands in the far treeline. Small and dark against the sky. |
| 9 | Fade | ~10 s | Deep blue, one star appears, then dim to the loop start. |

Notes on feel:

- The hiker doesn't move during the sunset except for a slow breathing cycle (about 4 s, very small movement). Stillness is what makes it read as calm.
- Light the clouds and the valley layers rather than animating a sinking sun.
- Stars appear only at the very end. One is enough.

The palette stepping and the valley going dark layer by layer are the same atmospheric-perspective system as section 2 run over time. That's why this scene is cheap to draw even though it looks rich.

## 8. Idea pools

These are candidates. Not everything needs to ship, and new ideas in the same spirit are welcome.

### Calm and appreciative moments

- Stacking a rock cairn: he picks flat stones one at a time and balances each one. The last clicks into place and he steps back to admire it.
- Watching a sunset from camp with a mug, not moving
- Reading a paperback against a log. A chipmunk climbs up and "reads" over his shoulder.
- Watching stars come out one at a time, counting, losing count, starting over
- Morning light rays through the trees, dust motes, a long stretch and yawn
- Skipping a stone: three bounces, then a fourth, and the ripples settle
- Sketching the view in a small notebook, with the drawing appearing as a faint overlay
- Eyes closed, listening to the stream. A leaf lands on his knee and he leaves it there.
- Fireflies blink on at dusk, and one lands on his finger
- Lying in the grass watching clouds. One becomes a rough animal shape, and he squints and nods.
- Fire glow on his face while he hums (mouth moves, flame flickers)
- Dawn fog lifting off the water while he wakes and shivers in his sleeping bag
- Morning coffee
- Filtering water at the stream while a frog watches

### Comic beats

- Two squirrels chase up a tree. One spins off a branch and drops into the stream. The hiker laughs. The squirrel shakes itself off and climbs onto a log as if nothing happened.
- A deer steps on a stick and startles. The hiker laughs, and the deer walks off offended, head high.
- A sock blows off the line and lands on a frog. The frog doesn't move, and he gives up on rescuing the sock.
- Log crossing over a creek: a wobble, windmilling arms, a recovery
- Map reading on a rock. The wind takes a corner and he chases the map downhill.
- The camp chair collapses mid-sit with a soft puff of dust.
- The coffee pot boils over, steam hisses and he jumps back.
- Gear pile: he holds up an item and it's a comically tiny fork.
- Fishing: a fish jumps, he yelps, and it escapes. Or a fish leaps out and lands in the fire pit.
- A pinecone bounces off the tent, then off his head.
- Burrs on his socks, pulled off one at a time (a good recurring motif)
- Wishing on stars: he wishes on a meteor, and a raccoon nearby "wishes" on his shoe.

### Weather

| Effect | Visual ideas |
|---|---|
| Wind | Grass and tent flaps bend in waves, leaves blow across, his hat or map blows away |
| Rain | Diagonal streaks, drips off the tent, puddle ripples, the fire hisses and smokes |
| Snow | Slow flakes, snow building on the tent roof and shaken off, a snowball moment with a fox |
| Heat | Horizon shimmer (row offsets by a sine), fanning himself with a map, sweat drops |
| Fog | Translucent drifting bands (done as palette shifts toward the fog color per layer, not real transparency), trees fading in and out |
| Chill | Breath puffs, arm hugging, shivering, frost growing on the tent corners |
| Lightning | A brief flash, then silhouettes. The tent sags and rebounds as he dives in. At night the flash must stay within the night brightness limits (section 11): a palette shift to a lighter blue-gray, never a white screen. |

### Vegetation

- Poison ivy (three leaves) by the path. He brushes it, later scratches his arm, and a small rash dot appears.
- A dead branch cracks and thuds near the tent with a puff of dust.
- Wet wood hisses and spits. Smoke rolls sideways and he waves it off.
- Smoke follows him around the fire. He moves, the wind shifts, and it follows him again.
- Mushrooms pop up overnight. One is a little house with a snail living in it.
- Flowers open and close with the time of day.

### Fauna

| Animal | Behavior ideas |
|---|---|
| Ants | A line carries off a crumb, then a bigger chunk, then the whole cookie, and eventually the sandwich, while he watches |
| Bees | One investigates his cup. He waves it off and it lands on his nose. |
| Squirrel | Buries a nut, forgets where, and digs up three other nuts. Also the stream splash gag. |
| Chipmunk | A recurring runner carrying tiny objects across the frame |
| Raccoon | Goes through his pack, sits to eat a granola bar and looks up guiltily. Steals a shoe. |
| Deer | At the edge of camp, one step closer each beat |
| Owl | Perched above the tent: a slow blink, a full head turn |
| Frog | Hops into the stream with a splash and dodges his hand |
| Bird | Swoops for a chip and drops it in the stream |
| Mice | Nibble a hole in the bottom of the tent, discovered in the morning |
| Bear | The food bag sequence. Also sniffing the tent with his silhouette inside. |

### Bathroom break

Yes, he goes off to pee. Keep it off camera, in keeping with the tone.

- He wanders behind a tree with a "just going over here" glance. The view stays on camp and a squirrel reacts to the rustling.
- While he's gone, a raccoon walks off with his shoe.
- Running joke: he always uses the same tree, and the animals learn it's their window to raid camp.
- A squirrel on a stump watches him leave and return with a knowing look.

## 9. Animation craft

- Anticipation before big moves: wind up before a jump, tilt before a sneak, hesitate before a grab.
- Overshoot and settle on pops and landings. Ease in and out on everything. Nothing moves at constant speed except things that really do (a cloud, the stream).
- Give each animal a personality (bold, nervous, sneaky) so the same approach beat reads differently.
- Let the audience see a gag coming before the character does (the squirrel eyeing the bag while he's turned away).
- When something feels rushed, add stillness, not more motion.
- Rig characters as jointed shapes (hips, knees, shoulders, elbows, head) driven by a few angles. A walk cycle is a handful of sine curves on those angles, not keyframes. Poses (crouch, reach, sit, startled) are angle sets to blend between.
- Squash and stretch on small things (frog, squirrel, pinecone) is cheap and adds a lot of life.

## 10. Hardware

The board is the same one TComm uses. **Decided.**

| Item | Fact | Status |
|---|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-4.3C | Confirmed (Mike has one in hand) |
| CPU | ESP32-S3, dual-core Xtensa LX7, up to 240 MHz, single-precision FPU | From Espressif's chip specs |
| Display | 4.3 in IPS, 800x480, landscape, 16-bit RGB parallel interface | Confirmed by Waveshare docs |
| Memory | 16 MB flash, 8 MB PSRAM | Confirmed (flash detected as 16 MB by the flash tool) |
| Touch | GT911 capacitive, on the I2C bus (SDA GPIO8, SCL GPIO9), reset on IO expander EXIO1 | From Waveshare docs |
| Backlight | Enable on EXIO2, PWM brightness through the IO expander. Number of steps undocumented. | From Waveshare docs, steps unverified |
| Audio | ES8311 codec over I2S, amplifier enable on EXIO4. The docs also list EXIO4 as the microSD chip select. This looks like a double assignment; check the schematic before using the speaker and the SD card together. | From Waveshare docs, conflict unverified |
| Programming | USB-C (native USB, GPIO19/20) | Confirmed, Mike has flashed over it |

What this means for rendering:

- A full 800x480 frame in RGB565 is 768,000 bytes. It only fits in PSRAM, not in internal SRAM (the S3 has about 512 KB of internal SRAM in total, shared with everything else).
- On ESP32-S3 RGB panels, the display controller continuously reads the framebuffer out of PSRAM to refresh the screen. Heavy CPU traffic to PSRAM at the same moment competes for the same bus, and on these boards that can show up as tearing or the picture briefly shifting. Espressif's RGB driver offers bounce buffers in internal SRAM to reduce this. How Waveshare's example code configures the panel (bounce buffers, double framebuffer, pixel clock) is **unverified** and should be read from their example before choosing a rendering approach.
- The practical consequences: redraw as few pixels per frame as possible, write to PSRAM in long sequential runs rather than scattered pixels, and keep per-frame working data in internal SRAM.

## 11. How it fits into TComm

The screensaver is an idle mode inside the TComm panel firmware, not separate firmware. **Decided.**

The TComm firmware doesn't exist yet. It's at the bring-up stage (see `docs/CLAUDE_CODE_HANDOFF.md`). So build the screensaver as a self-contained module with a small interface, develop it standalone on the board and in a PC preview (section 14), and wire it into the panel firmware once the interface stage exists. Build it so that integration only involves calling that interface.

### Start and stop

| Rule | Value | Status |
|---|---|---|
| Starts after | 10 minutes with no touch | Decided |
| Stops on | A touch anywhere. Only a touch. | Decided |
| What the stopping touch does | Wakes the screen only. It doesn't press the button under the finger. Buttons respond to the next touch. | Decided |
| If "screen off overnight" is on in Settings | The screensaver doesn't run while the screen is off | Proposed |

### The touch reveal

**Decided.** A touch sends out a circle from the touch point that quickly clears away the scene and reveals the main TComm screen underneath, ready to use. **Proposed** duration: about 300 to 400 ms, with ease-out so it starts fast and settles. A thin ring at the edge of the circle in a palette color would make the transition look deliberate.

A cheap way to do it (proposed): have LVGL render the main screen into a second full-frame buffer in PSRAM. Each frame of the reveal, for every screen row the circle crosses, compute the circle's left and right edge for that row and copy that span from the main-screen buffer into the visible framebuffer. Only the band of rows between the previous and current radius changes each frame, so the work is small. Anti-alias the circle edge if it looks rough.

Fallback, also accepted by Mike: if the circle turns out impractical, use a simple animated vector wipe that clears the scene to reveal the main screen.

When the main screen appears, it follows the normal TComm brightness rules. At night, the touch brings the screen to full light and it returns to dim after 30 seconds without a touch.

### Radio events while the screensaver runs

**Decided:** events never stop the screensaver. Only a touch does. The panel keeps receiving and handling every radio message in the background, exactly as it would with the main screen showing.

| Event | What the screensaver shows | Status |
|---|---|---|
| Other floor changes status | The status chip changes color and label | Decided (chip), proposed (details) |
| Doorbell from the other floor | The chime plays (following TComm's status and quiet-hours rules). The hiker reacts in the scene: stops, turns toward the viewer, and waves or points toward the screen edge. The status chip pulses. The doorbell banner (Come on / Not now) is waiting on the main screen when someone touches to reveal it. | Decided (chime plus visual cue), proposed (details) |
| A dog goes out or comes in | A small dog glyph appears on or next to the status chip | Proposed |
| No signal | The chip shows the No signal color and label | Proposed |

The hiker's doorbell reaction should interrupt whatever he is doing in a natural way, then let him go back to it. This is the screensaver's one hard real-time duty: the reaction should start within about half a second of the event (proposed).

### Status chip

**Decided:** a small chip stays on screen while the screensaver runs, showing the other floor's status. **Proposed** design:

- About 140x40 px in a corner (bottom right, say) with rounded ends.
- Filled with the Framework 1 status color, with the status label in the panel's UI font.
- Status colors, which are final and must not be changed: Open #2F5D3A, Busy #B97A12, Private #B85C3E, Sleeping #3D5A8C, Away #7C8472, No signal #4F5964.
- It should feel placed in the scene (consistent margins, maybe a thin outline in the scene palette) rather than pasted on top.
- Keep the scene's important action away from that corner.

### Weather and time of day

**Decided:** real data sets the base and random extras go on top.

- **Time of day** comes from the panel's clock (the RTC, kept in sync between the two panels). It picks the palette family (morning, day, golden hour, dusk, night) and which spine variants fit.
- **Weather** comes from the OUTSIDE message that WeatherGate already sends to the panels. It carries temperature (°F), humidity, a forecast code and phrase, and a reading time. Map these to scene weather: rain or snow from the forecast, fog from high humidity at cool temperatures, heat shimmer from high temperatures, chill and breath puffs from low temperatures, and so on. The forecast codes come from a Zambretti forecaster. Their exact list isn't documented in this repo yet, so make the mapping table easy to edit. (**Unverified**: code list.)
- **Random extras:** on top of the real base, a loop may add small weather moments (a gust of wind, a short shower) as surprises. Don't contradict the real weather badly. No snowstorm on a hot, clear afternoon.
- **No data:** if no OUTSIDE reading has arrived for 15 minutes (the same rule the panel uses for its Outside tile), fall back to fully random weather that suits the season and time of day.
- **Sunset time** for the Lookout window: compute it on the device with a standard approximate solar position formula from the date and the house's latitude and longitude. Accuracy within a few minutes is plenty. The latitude and longitude go in the untracked local config file, not in the repo (it's public).

### Night

**Decided:** the screensaver keeps running at night as a dimmed night camp scene, using the night palette: fire glow, tent light, stars, owl, fireflies, the bear. It must follow TComm's night rule:

- The backlight is at the night level.
- No large bright areas and no pure white. Keep the brightest colors small (fire, a star, the lit tent).
- Lightning becomes a palette shift, not a flash.

**Proposed:** at night, lower the story's energy too: fewer majors, longer pauses.

### Sound

**Decided:** optional quiet ambient sound (stream, birds, fire crackle, wind) is allowed. It's off by default, with a Settings toggle.

Requirements:

- When it's off, the amplifier stays disabled. The plan already says to switch the amp off between sounds to avoid idle hiss.
- The doorbell chime always takes priority. Ambient sound stops or ducks under it.
- Quiet hours cap or mute ambient sound.
- To stay in keeping with "no assets", generate ambient sound procedurally where you can (filtered noise for stream and wind, short synthesized crackles and chirps). Short samples are acceptable if procedural sound doesn't work, but keep them small. This is the lowest-priority feature. Build it last.

### TComm's own UI rules don't bind the screensaver

The main TComm interface follows Framework 1's rules: flat solid colors, no gradients, blur, glow or soft shadows, and limited animation. The screensaver is a separate visual mode with its own Firewatch palettes and full animation. Those main-UI rules don't apply to it, except that:

- the status chip uses the Framework 1 status colors
- the night rule above still holds
- the main screen revealed by a touch must look exactly as it would have without the screensaver

## 12. Rendering approach

Fable should design the renderer. The constraints and expectations are below. Anything marked proposed can change if a better idea holds up on the hardware.

### Constraints

- **Frame rate:** target 20 to 25 fps, steady. Steady matters more than high. A stutter during a slow pan is worse than a lower constant rate.
- **Radio comes first:** the radio and touch tasks must never miss a deadline because of the screensaver. Run the screensaver at a lower task priority than radio and touch. If a frame runs long, skip the frame rather than block.
- **Memory budget (proposed):**
  - Flash for screensaver code and data under about 200 KB.
  - PSRAM for buffers up to about 2.5 MB: a background layer cache, the main-screen buffer for the reveal, and working space. That leaves room for LVGL and the rest of TComm in the 8 MB.
  - Internal SRAM use small and bounded, with no growing allocations during a loop.
  - Allocate everything at screensaver start and free it on stop, or allocate once at boot. No allocations per frame.
- **Determinism:** given the same seed and the same inputs (time, weather), a loop should play out the same way. This makes bugs reproducible and lets the PC preview match the board.

### Expected techniques (proposed)

- **Layer cache:** the static parts of the scene (sky bands, far ridges, distant trees) are drawn once into a PSRAM buffer and redrawn only when the palette changes (time-of-day steps, weather, the Lookout sunset). Moving things are drawn over that.
- **Dirty rectangles:** each frame, redraw only the regions that changed: restore from the layer cache, then draw the moving elements in order. Birds and small critters redraw only their own bounding boxes.
- **Draw order:** sky, far ridges, far trees, stream, midground, actors, foreground plants, status chip.
- **Palette-indexed scene description:** shapes store a palette index, not a color. Changing the time of day is then a palette swap plus a redraw of the affected layers, and the atmospheric-perspective layers are just index ramps.
- **Anti-aliasing:** fill polygons and circles with a scanline rasterizer that computes edge coverage at subpixel precision and blends only the edge pixels. Interior spans are plain fills, which are fast. Anti-aliased lines can use Wu's algorithm or a coverage method. This is the biggest factor in whether the result looks polished or cheap. Use an RGB565 blend helper with a fast path (precomputed blends between neighboring palette colors are one option).
- **Cheap motion:** sums of sines and lookup tables for fire, water, grass and breathing. No per-pixel noise. A sine table and a fixed-point or float path, whichever measures faster on the S3.
- **Vertical sync:** if Waveshare's panel setup makes it possible, sync buffer swaps or copies to the panel refresh to avoid tearing. How possible this is depends on their driver setup (**unverified**, see section 10).
- **LVGL coexistence:** while the screensaver is active, it can own the framebuffer with LVGL's refresh paused, or draw through an LVGL canvas object. Owning the framebuffer is expected to be faster, but that's an expectation, not a measurement. Measure both if unsure, and choose the one that holds the frame rate while leaving LVGL able to render the main screen for the reveal.

### Starting code sketches (proposed, not built or tested)

Scheduler: a list of timed beats, each with a duration and an update function that receives normalized progress 0..1. Ambient systems and critters update every frame regardless of the current beat. A surprise scheduler picks from the pool with randomized gaps.

```cpp
struct Beat {
  float duration;              // seconds
  void (*update)(float t);     // t runs 0..1 within the beat
};

float easeInOut(float t) { return t * t * (3 - 2 * t); }
float easeOutBack(float t) {   // overshoot then settle
  const float c = 1.70158f;
  t -= 1.0f;
  return t * t * ((c + 1) * t + c) + 1;
}
```

Critters: each critter is a small state machine driven by distance to the hiker, not by the timeline.

```cpp
enum CritterState { IDLE, WATCHING, SNEAKING, FLEEING };

struct Critter {
  float x, y;
  CritterState state;
  float fearRadius;            // per-animal personality
};

void updateCritter(Critter &c, float hikerX, float dt) {
  float d = fabsf(c.x - hikerX);
  if (d < c.fearRadius)            c.state = FLEEING;
  else if (d < c.fearRadius * 3)   c.state = WATCHING;
  else                             c.state = SNEAKING;
  // move and animate per state
}
```

Sky: horizontal bands from a palette, driven by one progress value. Needs an RGB565 interpolation helper (`lerpRGB565`, not yet written) for palette transitions.

## 13. Variety system

"Doesn't seem to repeat" is a core requirement, so design it as a system rather than leaving it to randomness. Proposed rules:

- **Seed per loop** from a hardware random source mixed with the time.
- **Recent-history memory:** remember the last few loops' majors, spine variants and calm moments, and don't repeat a major within the last 3 loops or a calm moment within the last 2. Keeping this in RAM is enough. Optionally save it to NVS (on-board flash storage) so a reboot doesn't reset it, but keep writes rare (once per loop at most) to spare the flash.
- **Weighted picks:** weight content by fit with the time of day, weather and night mode, and lower the weight of anything seen recently.
- **Combinations:** let motifs and critters interact with whatever beat is running (the chipmunk steals from whatever the hiker just set down, the raccoon targets whichever item is nearest). A small number of parts then yields many distinct moments.
- **Parameter jitter:** small random variation in timing, positions, which tree, which side an animal enters from, cloud shapes and bird paths. It should never be large enough to break a gag's timing.
- **Rare events:** a few things that show up only occasionally (a meteor wish, the snail house, a moose passing far off) give long-term viewers something to discover.

## 14. Repo layout and workflow

Proposed:

```
screensaver/
  core/        pure C++: scheduler, scenes, rigs, critters, palettes, rasterizer.
               Draws into a plain uint16_t RGB565 buffer. No ESP-IDF, Arduino or LVGL calls.
  port_esp32/  framebuffer access, frame timing, touch hook, the reveal, the interface to TComm
  sim/         PC preview: runs core/ in a desktop window (SDL2 or similar) at 800x480,
               with keys for fast-forward, jump to beat, change seed, force time of day
               or weather, and trigger a doorbell
```

- The PC preview is the main development loop: build a scene there, then confirm on the board. It also lets Mike review scenes without flashing.
- `core/` must build on both the PC and the ESP32 with no changes.
- **Interface to the panel firmware (proposed shape):** start, stop with a touch point (runs the reveal), per-frame tick, and a setter for inputs: other floor's status, dog value, last OUTSIDE reading or none, clock time, night mode on or off, ambient sound on or off. Add a doorbell-event call for the hiker's reaction.
- **Work on a branch** (for example `screensaver`) and open pull requests to `main`, so Mike can review.
- **This repo is public.** Never commit Wi-Fi passwords, ESP-NOW keys, tokens, the house's latitude and longitude, or anything from Mike's other projects. Use the untracked local config file (`.gitignore` already covers `config_local.*` and `secrets.*`).
- **Flashing:** confirm the COM port with the board's owner before every flash, and never erase flash without asking. Mike's board shows up as COM5 on his laptop, but the number can change. If the factory firmware needs restoring, see `docs/CLAUDE_CODE_HANDOFF.md`.
- **Mark anything unverified** in code comments and reports.

## 15. Build order and done-when

1. **Foundation.**
   - Build: the `core/` buffer and rasterizer with anti-aliasing, the palette system, easing helpers, the scheduler and the PC preview.
   - Done when: a static Firewatch-style landscape (sky bands, three ridge layers, a tree line) renders in the preview with clean edges, and a palette change to dusk looks right.
2. **On the board.**
   - Build: the same landscape on the panel, with the layer cache, dirty rectangles and frame timer.
   - Done when: a moving element (a cloud, a bird) runs at a steady 20+ fps with no visible tearing or picture shift, and frame time, PSRAM use and flash use are logged.
3. **Scene build-up and ambient layer.**
   - Build: sky fade, trees standing up, the stream rolling in, plants popping, mushroom last; then grass sway, stream flow, cloud drift and fire flicker.
   - Done when: it runs for an hour without a glitch.
4. **Hiker rig.**
   - Build: walk cycle, idle breathing, a set of poses.
   - Done when: his silhouette reads clearly at the sizes the scenes use.
5. **Story spine** beats, one at a time, with variants.
6. **Critters and the minor-effect scheduler.**
7. **Surprise pool, variety system and per-loop seed.**
   - Done when: ten loops in a row in the preview show no two alike in their majors and spine variants.
8. **Integration hooks.**
   - Build: the status chip, the doorbell reaction, the touch reveal (or the wipe fallback), wake-only touch and the real-data inputs, tested with fake inputs.
   - Done when: a touch reveals the main screen in under half a second, and a fake doorbell triggers the chime cue and the hiker's reaction.
9. **Night mode and weather systems.**
   - Done when: the night scene has been reviewed in a dark room on the real panel.
10. **Lookout scene,** gated to real sunset.
11. **Optional ambient sound,** off by default.

At each stage, send Mike a short screen recording (a phone video of the panel is fine) so he can comment on the look before the next stage builds on it.

## 16. Still open

None of these block starting.

- The exact Zambretti forecast code list from WeatherGate, for the weather mapping table
- The number of backlight dimming steps, which affects how the night level looks
- The EXIO4 double assignment (amp vs. SD chip select), which affects ambient sound
- How Waveshare's example configures the RGB panel (bounce buffers, double buffering, pixel clock), which affects tearing and the rendering approach
- Final look of the status chip and the hiker's doorbell reaction, which Mike reviews when stage 8 is ready
