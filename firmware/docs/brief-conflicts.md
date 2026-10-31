# Where the two briefs disagree

The rule from the handoff: the design brief wins on layout, colors and fonts. The handoff wins on everything else. This is how that rule plays out, and what it leaves for Mike to decide.

## Settled by the rule

| Topic | Design brief | Handoff | What gets built |
|---|---|---|---|
| Link between units | Heartbeat every 30 s, NO SIGNAL after 5 min of silence | No heartbeats. NO SIGNAL only when a send fails every retry; cleared by any message from that unit | Handoff. Nothing is sent on a timer. |
| Doorbell hold | Hold 1.0 s for an urgent chime with a progress ring, behind a flag | No hold gesture, no urgent chime | Handoff. Not built, no flag. |
| Doorbell on the receiving panel | "KNOCK from UPSTAIRS" banner, 20 s, Dismiss button | "Doorbell from upstairs" with Come on and Not now | Handoff wording and buttons. Banner look drawn in the Lookout palette (not on any board yet). |
| Doorbell on the sending panel | Not covered | Ringing, Delivered, then the reply; "No answer yet" at 60 s; "Couldn't reach" on failure; repeat taps ignored for 10 s | Handoff. |
| Timer entry | Hold any status button 0.6 s to open the timer sheet | Private and Sleeping continue to the timer sheet; the others apply at once | Handoff. No holds in the status menu, no "hold for timer" hint. |
| Timer fallback default | Open | Busy | Busy. |
| Dog state | Each unit shows its own dog only | One shared value; both panels show whichever dog is out; the two are never out together | Handoff at first. Changed 2026-10-25 (Mike: both are sometimes out now): each dog is kept on its own, both panels show LUCY OUT, COPPER OUT or BOTH DOGS OUT, and each panel's button changes only its own dog. Header pill layout from the design. |
| Recorded voice knock | Open question | Four short sound files only | Handoff. No recording. |
| Phone forwarding | Leave a hook point | Out of scope | Nothing built. |
| Clock source | NTP over Wi-Fi, RTC fallback | No Wi-Fi in normal mode; maintenance mode takes network time; units copy the clock from each other's STATE | Handoff. The Wi-Fi tile in Settings becomes the maintenance-mode entry. |
| 56 px gear and dog button | Draw at 56 px, touch area 90 x 90 | 56 px is fine | Both: 56 px drawn, enlarged touch area. |

## Decided by Mike on the panel (2026-10-22)

| Topic | What the briefs said | Mike's decision |
|---|---|---|
| Toolchain | Arduino core 3.3.11, pinned to Waveshare's CI | Core 3.2.0 with Espressif's high_perf libraries, the only build that gave a stable display (README, Toolchain). The bring-up sketch still builds on 3.3.11. |
| Undo after a status change | Design brief 10.5: a 5 s Undo bar | Removed: choosing another status from the menu does the same job. |
| Timer presets | Drawn: 15 min to 4 hr, 1 hr selected | Private: until changed, 15 min, 30 min, 1, 2, 4 hr, starts on "until changed". Sleeping: until changed, 2, 4, 6, 7, 8 hr, starts on 7 hr. Custom on both. |
| Ringtones (2026-10-24) | Westminster and a two-note ding-dong; the RINGTONE tile switched between them | 11 ringtones. Standard: Westminster, Ding-dong, Rising chime, Church bells, Music box. Nature: Chickadee, Cardinal, Wind chimes, Nightingale, Woodpecker, Frogs. Tried the same day and removed: five sci-fi ("not good and annoying"), Water drops, Rounds, Old bell, Robin, Crickets, Loon. Woodpecker and Church bells redone to sound more real (Woodpecker without its call at the end). RINGTONE opens a scrolling list grouped Standard and Nature; a tap chooses one and plays it. Settings store a permanent id per ringtone. |

## Needs Mike

1. The NO SIGNAL tile. The handoff says gray tile, last known state, how long ago. Board P6 shows the word NO SIGNAL with "last seen 7:08 PM" and dashed readings. A blend would keep the word NO SIGNAL with a since line like "was BUSY, last heard 7:08 PM". Which one?
2. During NO SIGNAL, should the other floor's temperature and humidity keep their last values, or show dashes as P6 does?
3. The drawn timer sheet has no "no timer" choice, so as drawn Private and Sleeping always expire. Should there be an "Until I change it" button? (Answered 2026-10-22: yes; Private starts on it.)
4. "A touch brings it to full light" at night. Does the backlight go to full while the night palette stays, or does the screen switch to day colors? The design rule says nothing glaring at night.
5. How bright should a doorbell banner wake a receiver that's in Private or Sleeping? (The handoff already flags this one.)
6. Should the sending panel make any sound when its own DOORBELL is tapped? The design says a chime plays on both; the handoff only describes the other panel.
7. The design brief proposes holding the gear 2.0 s for clean-screen mode. Keep it?
8. Status presets and holiday words are Settings tiles on board F11, but neither brief defines their editor screens. Build the tiles now and the editors later?
9. I'd compute sunrise and sunset on the device from latitude and longitude, which needs no network. That needs the panels' rough location, kept in the untracked config file. OK?
10. Phase windows for night, dawn, morning, midday, golden hour and dusk are open. The design brief's proposal works as a starting value if Mike has no preference.
11. Three night brightness levels are a recorded requirement; names and steps are open. The bring-up sketch shows how many usable backlight steps there are, so this can wait for that result.
