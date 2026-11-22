# Stage 22: review findings the builders must apply

Written 2026-11-18 from two read-only reviews of the six plans (one against the code, one against the
budgets and pacing). Where a finding and a plan disagree, the finding wins. Each has the piece, the
claim, the evidence and the fix to build.

## F1 [major] Mushroom (sky, band repaint)

Claim: The trip's sky is not a pure function of state when the real sun moves, although section 6.2 says every pixel is. The halo is drawn from the live sun position, not from the band's latched clock, and it reaches further than the rows the app repaints.

Evidence: MUSHROOM_PLAN 6.2: halo discs of radius r+7+3b and r+16+5b (up to r+21) behind the sun. app.cpp:336-359: on a sun move the app repaints and marks only landscapeSunBounds (old and new squares). Rows of the halo outside that square keep the halo at the old position until their band's turn comes round (up to 10 ticks). A full redraw in that window differs, so test 11.3 'incremental equals full' and the doorbell 'frames stay equal to a full redraw' check can fail on a sun-move tick.

Fix: While skyTripOn, make landscapeSunBounds (or a trip variant the app calls) include the largest halo (r+21 plus fringe), so the existing repaintRows and markDirtyRect cover it. Add a test that moves the sun one pixel mid-trip and compares against a full render on that tick.

## F2 [major] Mushroom (sky, end of the trip)

Claim: The plan never says how the ten band clocks get back to 0. Zeroing them at once leaves polygon-drawn sky in the cache where a full redraw uses fillRect, and the plan's only stated repaint path is a 10-band repaint in one frame.

Evidence: 6.2: 'a band whose latched clock is 0 takes the old fillRect path ... switching on and off repaints nothing'. But at t = 104 s every tq[i] holds about 2,590, and test 11.3 wants Landscape::trip all zero and the sky rows equal to a full redraw. Section 1 lists 'whether an opaque polygon with a level edge gives exactly the pixels of fillRect' as UNVERIFIED. The only reset written down (10.2 dayTrack: 'rows 0..320 repainted and marked') is ten cache bands plus ten full-width strips in one frame: about 73 ms + 50 ms by the plan's own figures (110 ms / 15 per band, 5 ms a strip).

Fix: In actTrip, from t = 102 s (ramps already at 0, 50 ticks left), latch 0 instead of the clock as each band's turn comes, so all ten return through the fillRect path by the ordinary rolling repaint. For the overrun path in majorEnd, do the same at two bands a tick over five ticks instead of one 320-row repaint.

## F3 [major] Mushroom (visions, upper strips)

Claim: The cost section counts '8 to 9 strips a frame' during the sky, but vision 2 of set 0 (the antler tree) adds three upper strips every tick on top of the sky's band, for about 26 s.

Evidence: 5.5: deer at y 340 (48 px tall, head about y 292), antlers 56 px tall and 70 px across, anchored to critterHead every tick: rows about 236..340 = strips 7, 8, 9, 10. In-frame critters are marked every tick (HOW_TO 1.4 rule 3), and the antlers follow the head. He lies in strips 10 and 11, so 7, 8 and 9 are new. With the sky's one full-width strip that is about 12 strips, not 8 to 9. On the plan's own figures (42 ms settled, 11 ms a band, 5 ms a strip) that is about 68 ms a frame; past 40 ms the host runs two ticks a render, so two bands. Vision 1's chipmunk at scale 3.2 (45 px, top about y 303) adds strip 9 the same way.

Fix: Keep the tree in one extra strip: antlers at most 28 px tall (rows 264..292, strip 8 only) or stand the deer at y 356 so the head is in strip 9. Mark the antler glyph only when its quantized growth or the head's whole-pixel position changes. Set kTripBandEvery to 2 while a vision that reaches above row 320 is showing. Report the sky's 104 s strip count per vision, not one average.

## F4 [major] Mushroom (set 2, vision 2: the stars settle)

Claim: The falling stars do not fit their 26 s slot, and once they leave the cached sky they dirty up to five upper strips a tick.

Evidence: 5.5: five day stars float down at 12 px/s, one by one, land on him, 'rest there glowing for 6 s, and fade one by one'. Stars are at y 10..195 (3.3); his chest is near y 345 (lie.y 354). Fall distance 150 to 335 px = 12.5 to 28 s each, before the 6 s rest and the fade, in a slot of t 62..88 (26 s). Section 5.4 also has the stars going out from t = 84. Outside the cache each is a moving prop in its own row band, so five in flight are up to five more strips a tick (strips 0..9), plus the sky band.

Fix: Three stars, only those with y >= 150, released 4 s apart from t = 62; fall 16 px/s (about 12 s); rest 4 s; fade by t = 88. Mark a falling star on whole-pixel moves only (16 px/s is one mark every 1.5 ticks). Exclude the chosen three from the t = 84 'go out' list. Add the slot arithmetic to test 11.3.

## F5 [major] Mushroom (the wade)

Claim: The crossing is steeper than it is wide, and OP_WALK paces only x, so he slides down the screen about 1.6 times faster than his steps say. It will read as skating, twice (in and back).

Evidence: 3.2: AT_M0 = (Nx - 50, far water edge - 4, y about 392..404), AT_M1 = (Nx, pickables' y + 2, about 452..472): dx 50, dy 55 to 70. Script 5.1 step 4 uses walk(AT_M2, 12), walk(AT_M1, 12). HOW_TO 1.3: walk 'x moves; feet.y slides on the path'; world.cpp:214-217 sets feet.y by the x fraction. 50 px at 12 px/s is 4.2 s, during which y moves about 62 px: true speed about 19 px/s on a 12 px/s gait.

Fix: Pace the wade by path length: an act that moves feet along M0 to M1 at 12 px/s of true distance (about 80 px, 6.7 s each way) and advances the walk phase by distance. Add about 5 s to the beat (196 to 201) so the stillness in step 11 is not what pays for it.

## F6 [minor] Mushroom (sky, patch join)

Claim: A sun or moon move in a two-tick frame can turn one band's repaint into most of the sky in one frame.

Evidence: renderer.cpp:26-38: repaintRows keeps two patches and widens the second to cover a third. The plan's own regime is two ticks a render during the sky (6.3). Tick 1: band i (patch 0). Tick 2: the sun's rows, e.g. 40..100 (patch 1), then band i+1, e.g. rows 288..320, joined: rows 40..320, nine bands, about 65 ms by the plan's 7.3 ms a band. Rare (the sun moves about a pixel a minute, app.cpp:332), so about twice a trip.

Fix: When a sun or moon repaint is pending in the tick, skip that tick's band (the band counter does not advance), or give Renderer a third patch slot. Note it in risk 2 with the number.

## F7 [minor] Mushroom (day stars under overcast)

Claim: The wheeling stars will be near-invisible grey in the weather the trip most often follows.

Evidence: 6.2 draws the stars 'in STAR mixed 90 % over the band colour'. palette.cpp:270: STAR is mixed up to 85 % toward SKY1 under a cloud deck ('the cloud deck hides the stars too'). 7.2 of the plan says overcast is 'likely after rain'.

Fix: Draw the trip's stars in lit(butter 0xFFE9A8) instead of the STAR slot, capped by the same SKY2 scaling, so they dim with the mood and the night rule but not with the deck.

## F8 [major] Tai Chi (fog lift)

Claim: The form will be interrupted by more whole-cache palette rebuilds than the plan says, bunched in its middle: about 7 to 11, not '5 to 8', roughly one every 13 to 20 s in the piece where smooth slow motion matters most.

Evidence: spine_morning.cpp:1651-1652 and 1776-1783: fog_t1 = ticksThroughPause(p, MB_COFFEE) is computed after majorsPlan and the fit, so a major after the wake stretches the lift; each 0.05 step is 'a palette change: a cache rebuild'; the curve is easeInOut = smoothstep (ease.cpp:53). Span from the build: wake 30 + pause 14 + still 6 + tai chi 150 + still 6 + coffee 74 + pause 14 = 294 s. The form sits at u 0.17 to 0.68: smoothstep 0.077 to 0.758, so 68 % of fog_steps (11 to 16, line 1625) = 7.5 to 10.9 steps. Each is 110 ms at once or about 16 ms more a frame for 15 frames (MUSHROOM_PLAN section 1, from ss_host.cpp).

Fix: In morningTick, when the plan holds tai chi, do not let the step count change while taichiInForm unless the form is inside a hold of 2 s or more (wuji, crane, single whip, rooster, the closing): a pending step waits for the next such hold. Stillness hides the stutter. The rebuild count stays 1 + fog_steps. Correct section 6.4's '5 to 8' and add the count to test 11.2.

## F9 [major] Cattails (the brush)

Claim: During a doorbell freeze a mover caught inside a clump's footprint pushes the clump every tick for 3.4 s, so the reeds lean hard beside a frozen animal or hiker.

Evidence: CATTAILS 3.2: reedsStep pushes '40 px/s of tip speed a tick' while Critter::moving or Hiker::walking, and 'runs on the ambient side'; 9.2 puts reedsStep after streamStep, which is world.cpp:545, before the react.active branch at 546. In a freeze the director does not step and stepHiker runs with dt 0 (world.cpp:213), so critters.h:129 'moving' and hiker.h:101 'walking' stay true for 85 ticks.

Fix: Push from displacement, not the flag: keep last tick's feet x per mover (or one int16 per clump for the nearest mover) and push only when it changed by at least 0.5 px. Or skip the brush scan while w.react.active, fireIceHolds or visitorsHolds. Add a test: bell while a deer stands mid-walk in RS_FAR; the clump is at rest within 2 s.

## F10 [major] Cattails (sway state)

Claim: The data cannot hold the motion the plan promises. One quantized byte per clump drives the draw, but leaves, heads, reeds and each stalk are meant to move out of phase.

Evidence: 3.1: heads sample the wind 40 px upwind at 0.55, leaves at base.x at 1.0, reeds at 1.3, 'stalk k adds a phase offset by sampling at x + 9k'; 4.5: 'Nothing in a clump moves in step'. Section 7: int8_t sway_q[REED_CLUMPS], 'what the draw reads', and d_sway[REED_CLUMPS] for the compare. Either the draw scales one value (everything in step: stiff), or it reads unquantized samples the dirty compare never sees (stale pixels; the incremental-equals-full test fails).

Fix: Store two quantized values per clump, sway_q[REED_CLUMPS][2] (leaves, heads); reeds read the heads' value from the previous sub-tick times 1.3, and stalk k reads a fixed blend of the two. d_sway likewise: +10 bytes, 216 to 226. The integration cap is 224, so drop pad0 and the seeds' dirty words (recompute from age) in the same change. Update the '3 to 4 marks a second' estimate: two values change about twice as often.

## F11 [minor] Cattails (fireflies)

Claim: The firefly over RS_UPSTREAM leaves the strips the plan says it stays in, and the hard on/off will read as a blink, not a glow.

Evidence: 3.5: a figure 8 px tall round a point 10 px above the leaves; RS_UPSTREAM base y about 389 (top(512) = 388, +1), leaves 0.8 x 33 = 26 px, so the centre is about y 353 and the figure reaches y 349: strip 10 (rows 320..351). Section 12 says 'Fireflies and seeds stay in strips 11 to 14'. 'Lit for 0.5 s ... drawn only while lit' is a step on and off.

Fix: Centre the figure 4 px above the leaves for RS_UPSTREAM (or clamp its y to >= 356). Fade the dot over 4 ticks in and 8 out by radius (1.3 to 0.6) in two quantized steps, marking only on a step.

## F12 [major] Deer herd (the way out)

Claim: The exit is not 'a loose line': by the plan's own times and speeds the doe, D4 and D2 leave as one overlapping silhouette for about 6 s.

Evidence: Section 3: doe leaves G0 (x 464) at 86.0 at 70 px/s, lane y 378. D4 (x 354) starts 'at about 87.7' at 66 px/s, lane 368: the doe passes x 354 at 87.6, so D4 starts about 7 px behind her and loses 4 px/s. D2 (x about 296) starts at 88.7 at 68 px/s, lane 364: D4 is at x 288 then. Adults are 64 px long and 48 px tall; the lanes are 4 to 10 px apart. Three bodies overlap by more than 80 % from 88.7 until they leave the frame near 93. Section 12 risk 1 names merging as the main risk.

Fix: Each follower starts when the deer ahead is a body length (72 px) past it: D4 at 88.6, D2 at 90.5, D3 at 93.0, the fawn after D3. Spread the exit lanes to y 380, 366, 372, 360. That ends about 97.5 s; move the soft alarm to 77 s or make kHerdS 107 so the last stillness stays 8 s. Add the 2 px rule to test_day_herd for the exit, sampled every 5 ticks.

## F13 [minor] Longshot (timing)

Claim: The beat has less slack than its own test needs, and the long walks are faster than the house stroll.

Evidence: Section 5: 'beat seconds PROPOSED with 4 s of slack' (A script 98, beat 102); test 10.1 requires 'the script done 3 s before the beat ends', leaving 1 s for seed variance across three walks of about 255 px, go() routing and two until() steps. Walks are 40 px/s; kStroll is 34 (script.h:70), tai chi uses 28 and the mushroom 30.

Fix: Walk at 34 with the rifle (30 on the walk out); beats 109, 121, 126 (script + 7 s, as tai chi does). The extra seconds are walking stillness, which suits the piece.

## F14 [minor] Longshot (variant C)

Claim: until(heard) can stall the script to the beat's end, including under the plan's own 'no arcs' alternative.

Evidence: 5.1 C: 'when they [the arcs] fade the tick raises heard', then until(heard). Section 2 offers kLsArcs = false; glyphNew can also return none (GLYPH_MAX 28 shared, 4 kept targets). HOW_TO gotcha 10: until() has no timeout and a stall counts an overrun.

Fix: Raise heard from the clock: hit_t + 2.3 s (1.2 s flight-to-gong plus 1.1 s of travel), whether or not the arcs glyph exists.

## F15 [major] DEER_PLAN 7.1 (deer rig, the white tail flag)

Claim: The flag is drawn when clamp01(P.tail + c.tail_add) > 0.08, and 'nothing is drawn below 0.08, so every existing deer scene is pixel for pixel what it was'; the test pins every existing deer action's hash.

Evidence: Repo root the repo root. screensaver/core/scene/critters.cpp:467 and 485: the deer's run uses the shared bounding gait, which adds p.tail += w * 0.25f * cc every cycle, so P.tail swings to +0.25 with tail_add 0. critters.cpp:543: CA_SHAKE (new for the deer) adds 0.35 * s. The deer's base poses set no tail (critters.cpp:171-180), so only the gait trips it. Result: every existing running deer (a scattered deer, deer_stick's exit, the stream's deer leaving in a hurry) would flash a pale flag for part of each canter stride, and the fawn's shake would flicker it. The CA_RUN hash check in the plan's own test fails.

Fix: Drive the flag from c.tail_add alone (k = clamp01(c.tail_add)), ignoring P.tail for the deer. The herd already sets tail_add for every flag state, so nothing else in the plan changes.

## F16 [major] CATTAILS_PLAN 6.3, 8, 9.2 (the pick overlay against fire and ice and the visiting couple)

Claim: 'Fire and ice, the visiting couple: both hold the story; the pick does not start or step while either is on', with fireice.* and visitors.* listed as not edited.

Evidence: screensaver/core/scene/fireice.cpp:769-795 (fireIceCalm): it allows POSE_SIT, POSE_STAND and POSE_CROUCH, and tests the weather's gag, hug_w, ov_w, shiver and the HikerFx fields. The pick is a pose overlay that writes none of those (plan 4.1: 'his pose, facing, feet, carry and HikerFx are never written'), so fireIceCalm is true in the middle of a pick. visitors.cpp:773: visitorsCanStart ends in fireIceCalm, so the couple can start too. Then world.cpp:554-572 runs stepHiker but the plan's reedsStoryStep returns at once, so the overlay's clock freezes with its weight above 0: his arm stays reached back (form A), or he is left crouched away from home (form B) while fire and ice's script moves him for minutes, and 'gagHome' teleports him afterwards.

Fix: Add one line to fireIceCalm (which covers visitorsCanStart): return false while reedsPickActive(w). Move fireice.cpp to the edited list and add it to the foundation step in INTEGRATION_PLAN A1. Add a test: temp_ask set during each pick stage never starts fire and ice until the pick is over.

## F17 [major] CATTAILS_PLAN 3.1, 2.2, 10 (wind scaling against the fixed dirty boxes)

Claim: d = ambientWindAt * (H / 18) * 0.55 for heads, 1.3 for reeds; 'calm air: heads nod about 1 px each way'; 'a gust: up to 6 px at a camp-bank head, 10 px at a reed plume'; 'the dirty box pads 16 px each side'.

Evidence: screensaver/core/scene/ambient.cpp:66-69: sway = gust * (-0.8 + 2.6 * sumSines(...)), gust = (0.75 + 0.25 sin) * weatherSwayMul. core/anim/ease.cpp:126-138: sumSines is normalised to -1..1. weather.cpp:783: weatherSwayMul reaches 2.2. So the field is -3.4..+1.8 px in calm air and up to -7.5 px in a full gust. Plan arithmetic: RS_FORE reed (1.12 x 54 = 60 px): 7.5 x (60/18) x 1.3 = 32 px; RS_NEAR reed (43 px): 23 px; RS_FORE head: 7.5 x 3 x 0.55 = 12 px. Even at a typical sumSines of 0.7 the RS_FORE reed moves 25 px. All exceed the 16 px pad, so the plan's own 'bounds hold every draw at a full gust' test fails and pixels are left outside the marked box. Calm air is also about 3 px for a 33 px head, not 1.

Fix: Clamp each stalk's tip offset to the pad (for example min(|d|, 14 px)) or scale by sqrt(H / 18) instead of H / 18, and size the pad per clump from the clamped maximum. Correct the stated amplitudes. Keep the bounds test at swayMul 2.2 with sumSines forced to +-1.

## F18 [major] CATTAILS_PLAN 2.4, 9.1, 9.2 (the cached sedge fringe)

Claim: reedsDrawStatic 'takes only the landscape, so landscape.cpp can call it' from drawLayer's BL_TUFTS case, and skips 18 px either side of every story spot of section 2.2.

Evidence: screensaver/core/scene/landscape.cpp:895 and 1024-1038: drawLayer and landscapeDrawStatic receive only const Landscape&, no World. Every story spot in 2.2 is relative to the fire or the camp layout (F.x + 76, +95, +102, +112, +126, +150, +163, +205), and those live in World: fire.pos is set in worldBeginLoop (world.cpp:344), CampLayout in spineLayout (spine.cpp:2832-2862). The Landscape holds neither, so the function as declared cannot know where to leave gaps. Moving the call out to worldDrawStatic (world.cpp:732-733) would lose the build-up path (landscapeDrawLayers, landscape.cpp:1035) the plan relies on for the pop-in.

Fix: Have reedsInit write the fringe into the Landscape (for example up to 16 int16 x positions, or a 1 bit per 8 px mask along the stream) and let reedsDrawStatic read only that. Count the bytes in INTEGRATION_PLAN 5.3 (about +16 to +32 on sizeof(App), which the 29,992 wire has about 70 left for).

## F19 [major] MUSHROOM_PLAN 5.5 set 0 (the deer with the tree)

Claim: The antlers are 'anchored at critterHead every tick' and turn with the deer's head.

Evidence: screensaver/core/scene/critters.cpp:1026 is the only place has_head is set (the bear); critters.cpp:1782-1785 and critters.h:167-170: for every other kind critterHead returns the top centre of the bounds. For a side-on deer that is above the middle of its back, not its head, and it shifts as the bounds change with the gait and the head pose. The 56 px tree would grow out of the deer's back and slide about. (DEER_PLAN 7.1 notes the same fallback for critterMouth.)

Fix: In the deer rig step (INTEGRATION B3), add a second new anchor beside critterDeerNose, for example critterDeerCrown(const Critter&), from the head outline in frame space. Do not set has_head for the deer: critterMouth falls back to critterHead and the stream's drinking rings are placed from it (stream.cpp, per the deer plan).

## F20 [major] TAICHI_PLAN 4.3 step 6 (arm IK)

Claim: 'The elbow takes the low or back side (rig elbow bend >= 0): the elbows hang, as they should.'

Evidence: screensaver/core/scene/hiker.cpp:404-405: elbow = shoulder + dirA(P.shoulder) * kUpper, hand = elbow + dirA(P.shoulder + P.elbow) * kFore; angles are from straight down, + toward the facing (hiker.h:58-66). For a hand behind the shoulder, for example WIND hand 1 at (-0.21, -0.04) H: the hand direction is about -1.76 rad and the reach 0.214 of 0.31, so the two solutions put the upper arm at about -0.96 (elbow low) or -2.56 (elbow above the shoulder). The low one needs the forearm angle below the upper arm's, a negative elbow bend. With bend >= 0 forced, every back-reaching posture (WIND, MONK_OPEN, WHIP_GATHER, SNAKE, KICK, WHIP when not at full reach) gets a chicken-wing elbow raised behind him. The two statements in the plan cannot both hold.

Fix: Choose the IK branch by the lower elbow (larger cos of the upper-arm angle), allowing a negative P.elbow for hands behind the shoulder; blend the branch across the vertical through the shoulder so it does not snap as a hand passes the ear. Add to test 11.1: the elbow is never above the shoulder joint except in CRANE, FAN and NEEDLE_UP.

## F21 [major] DEER_PLAN 5 and 3.1 (a house dog let out during the build)

Claim: 'A no-show: if a house dog is in the scene when the beat begins (it went out during the 13 s build), no deer come.'

Evidence: screensaver/core/scene/housedogs.cpp:81-85 and 744-750: a dog cannot come in until sceneReady (build.phase == BP_DONE), and its 1 to 3.5 s wait only counts down after that. So at the herd beat's first tick no dog is ever in the scene, even when w.dogs.want has been non-zero for 13 s. The no-show test never fires. The hard alarm is a rising edge of 'a dog wanted out' against prev_in; if begin seeds prev_in from the current value there is no edge either. The doe then walks in at 5 s while the dog trots in from the same left edge at 1 to 3.5 s.

Fix: Make the no-show test w.dogs.want != 0 at herdBeatBegin (not 'in the scene'), and state that prev_in starts at 0 so a dog already wanted counts as an alarm for any deer spawned. Update the test 'A no-show (dog out at tick 300)' to assert zero deer spawned.

## F22 [minor] CATTAILS_PLAN 4.3 and MUSHROOM_PLAN 5.5 (critter actions the kinds do not have)

Claim: The blackbird does 'a head turn (CA_HEADTURN)'; the big chipmunk 'turns its head once (CA_HEADTURN)'.

Evidence: screensaver/core/scene/critters.cpp:43: kAlways is CA_IDLE only. critters.cpp:59-62 (chipmunk) and 80-82 (bird): neither row has bit(CA_HEADTURN); only the deer and the owl do. critters.h:139: an unsupported action silently becomes CA_IDLE. Both moves would play as nothing.

Fix: Use CA_LOOK for both (both kinds support it), or add bit(CA_HEADTURN) and a pose to the bird and chipmunk rows in the deer-rig step, which already owns critters.cpp. If added, the existing per-kind action hashes need a new baseline for those two kinds only.

## F23 [minor] CATTAILS_PLAN 3.2 (the brush during a doorbell freeze)

Claim: The brush pushes a clump every tick while Critter::moving or Hiker::walking, and 'runs on the ambient side: plants keep swinging through a doorbell freeze, which is right'.

Evidence: screensaver/core/scene/world.cpp:546-553: in the react branch directorStep is not called and stepHiker runs with dt 0, so a critter or the hiker frozen mid-stride keeps moving / walking == true for all 85 ticks. A mover frozen inside a footprint would push the clump at 40 px/s every tick and pin it at the cap for 3.4 s, then release it with a snap.

Fix: Skip the brush while w.react.active, or push only when the mover's feet.x changed since the last tick (one int16 per clump is not needed: compare against the mover's own last x kept in a local scan).

## F24 [minor] CATTAILS_PLAN 2.2, 2.3, 10 (RS_FIRESIDE and the seated reach)

Claim: RS_FIRESIDE's first reed tip stands '14 px behind his hips'; the test wants it '12 to 18 px behind camp.sit'.

Evidence: Plan numbers: base x = F.x + 54; reeds stand at base.x + {-9, +11} * s (2.3). screensaver/core/scene/spine.cpp:2841: camp.sit.x = F.x + 40. So the first reed's foot is 5 px behind his seat and the second 25 px; neither is 14. The site test as written fails for every seed at s = 1.

Fix: Give RS_FIRESIDE its own stalk offsets with the pick reed at base.x + 0 (or move the base to F.x + 63 and keep -9), and restate the test against that stalk.

## F25 [minor] DEER_PLAN 6.2 (fire and ice while the prints lie)

Claim: 'The doorbell reaction or fire and ice can start during return, when only the prints are left.'

Evidence: screensaver/core/scene/fireice.cpp:779: fireIceCalm returns false when w.day.kind != DAY_NONE, and visitorsCanStart ends in it (visitors.cpp:773). The herd holds DAY_HERD from its beat until the prints have faded (25 s after he looks), so the tenant's too hot / too cold play and the visiting couple are held off for the whole return beat and the fade, up to about a minute. The doorbell is unaffected (react.cpp:26).

Fix: Either accept and say so in the plan (and drop the 'not called while fire and ice hold' reasoning), or let fireIceCalm treat DAY_HERD with alarm over and no deer left as calm. The second needs fireice.cpp in the file plan.

## F26 [minor] INTEGRATION_PLAN 3.1 point 11 (floor_n in spineFitLength)

Claim: floor_n is 1 when the plan holds MJ_TAICHI or MJ_MUSHROOM, and kept majors are never dropped.

Evidence: screensaver/core/scene/spine.cpp:2764-2771: drop starts at n_majors - 1 and walks down 'while (drop > 0 && kept(drop))', so with two majors that are both kept (a forced fetch plus the forced mushroom, the plan's dog-out ripe loop) it stops at index 0 and drops it although kept(0) is true. Today this cannot happen because the floor is 2. It only bites if that loop's total passes 650 s (the plan estimates about 600).

Fix: In the drop loop, break out when every remaining major is kept (or set floor_n to the number of kept majors). Add the dog-out ripe loop with four fillers to the fit tests.

## F27 [minor] TAICHI_PLAN 9 and MUSHROOM_PLAN 5.8 (the no-show's length)

Claim: taichiSeconds: 'the no-show 46 like the others'; the mushroom's no-show likewise.

Evidence: screensaver/core/scene/day_plink.cpp:488 and day_bike.cpp:223: DAY_NOSHOW is set on MajorState::var in the begin hook. majors.cpp:3906-3908: the beat's seconds come from the plan's major_var when the timeline is built at loop start (app.cpp:216), before any begin hook. So the '46' branch (day_plink.cpp:459) is never reached from a random rain: the beat keeps its planned length. For plink that is 103 to 120 s; for tai chi it would be 143 to 165 s and for the mushroom 196 s, of which the script uses 46.

Fix: Say so in both plans and decide: accept about two minutes of him sitting in the rain (rare: only with no OUTSIDE reading), or give those two no-shows a longer script (walk to the tent, sit in the door) so the time is used. Do not shorten the beat after weatherPlan, which is given the loop's ticks (app.cpp:206).

## F28 [minor] MUSHROOM_PLAN 6.2, 6.3 (one cache band a tick through repaintRows)

Claim: 'If frames run past 40 ms the host runs two ticks before a render, which means two bands in that frame' at about 11 ms a band.

Evidence: screensaver/core/render/renderer.cpp:26-38 and renderer.h (patch_y0[2]): the renderer holds two bands; a third request before a render is merged into the second as one span from the lowest to the highest row. The real sun and moon also call repaintRows (app.cpp:358). So trip band 9 (rows 288..320) plus a sun band plus band 0, or three catch-up ticks across the wrap (8, 9, 0), become one repaint of rows 0..320: about two thirds of the cache in one frame (roughly 70 ms by the plan's own 110 ms figure), a visible hitch that then causes more catch-up.

Fix: In the trip's dayTrack, skip the band repaint on a tick when Renderer::patch_n is already 2 (the latched clock simply stays one tick older), and never queue the wrap pair 9 and 0 in the same render. Add to test_skytrip: three ticks without a render never widen a patch beyond 64 rows.
