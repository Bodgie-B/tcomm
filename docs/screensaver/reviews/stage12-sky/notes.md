# Sky truth (stage 1 of the sky work; review folder stage12-sky)

For review on the PC before anything goes to the panels. Nothing is flashed, nothing is committed.
Look at `contact_sheet.png` first; each tile is also here full size (800 x 480) as `NN_name.png`.

## What changed

1. **The sun follows its real arc.** The panel works out where the sun is from its clock and its
   place and hands the scene a compass bearing and a height. The scene looks south: the sun comes
   up on the left, climbs, and goes down on the right, about a pixel a minute. The colours still
   come from the same sky phases as before (dawn, morning, midday, golden, dusk, night), and those
   are set by the real sunrise, the real sunset and the sun 6 degrees up, so the light is golden
   exactly when the sun is low.
2. **The moon is real too.** Its phase comes from the date and its place from a simple formula
   (good to a degree or two). Waxing is lit on the right, waning on the left. It is not drawn
   within about two days of new, or when it is down; on those nights there are more stars and the
   usual ones are a little bigger. Near full, and high, it puts a little more light on the stream.
   By day a moon that is at least a quarter lit shows as a pale disc.
3. **Rain only when the words say rain.** The scene now picks its weather from the forecast's
   words, in the four groups you approved. The icon is used only for words that are not one of
   the 26 (the snow wordings, "No Pressure Data"). A reading with no usable forecast now plays a
   clear sky with the temperature rules; it can no longer fall back to random weather.
4. **A rainy sky looks rainy.** A cloud deck across the top of the sky and a darker sky under it.
   Gray-and-dry days get the deck without the darkening; "some cloud" days get a few still, high
   clouds besides the two that drift.

## The tiles

| # | Tile | What it shows |
|---|---|---|
| 1-8 | sunrise, early morning, mid-morning, midday, mid-afternoon, late afternoon, golden hour, sunset | One clear day, the same camp. The sun rises behind the ridges on the left and sets behind them on the right. Fine-weather words ("Settled Fine") |
| 9 | no moon (new): more stars | A night at new moon: no moon, 16 more stars, the usual 9 a little bigger |
| 10-15 | waxing crescent, first quarter, waxing gibbous, full, last quarter, waning crescent | Six nights through a month, each at a moment that phase is really up, so each is in a different part of the sky. Tile 13 (full) has the brighter stream |
| 16 | gray and dry | "Unsettled, Rain at Times": the deck, no rain, no darkening. Before, this forecast rained |
| 17-19 | rain: light, medium, dark | "Rain at Frequent Intervals" at the three darkness levels |
| 20 | storm | "Stormy, Much Rain": the fullest deck, the medium level plus the storm's extra. No lightning in this frame |

The day tiles and the weather tiles use the placeholder place every PC build uses, on an equinox
day. On your panels the arc is the house's own and changes with the season: wider and higher in
summer, narrower and lower in winter.

The night tiles are drawn with the panel's night gain (1.6), because the PC's default night is too
dark to judge a moon by. On the panel the backlight is dimmer than this picture.

## Choices for you

| What | Where | Now | Options |
|---|---|---|---|
| How dark the rain sky is | `kRainDark`, `screensaver/core/scene/weather.cpp` | 0.45 (medium) | 0.25 light (tile 17), 0.45 medium (18), 0.65 dark (19) |
| How much darker a storm is than rain | `kStormDarker`, same file | 0.25 | tile 20 is medium + 0.25 |
| The moon by day | `kDayMoon`, `screensaver/core/scene/landscape.cpp` | on, from a quarter lit | off |
| The full moon's light on the stream | `kMoonlightMix`, `screensaver/core/scene/app.cpp` | 0.30 | 0 turns it off |
| Extra stars on a moonless night | `LS_EXTRA_STARS`, `screensaver/core/scene/landscape.h` | 16 | any number; they cost nothing per frame |
| How much of the compass fits the frame | `kSkyAzSpan`, `landscape.cpp` | 125 degrees either side of south | smaller spreads the arc wider |

To try a rain level without rebuilding: `ss_preview --weather rain --overcast 0.9 --rain-dark 0.65`.
To put the sun and moon anywhere: `ss_preview --sky SUN_BEARING SUN_HEIGHT MOON_BEARING MOON_HEIGHT PHASE`.

## The words and the weather

| Scene | Forecast words |
|---|---|
| Storm (rain, lightning, darkest sky) | Stormy, Much Rain |
| Rain (darker sky, heavy cloud) | Very Unsettled, Rain; Rain at Frequent Intervals; Rain at Times, Worse Later; Rain at Times, Becoming Very Unsettled |
| Gray and dry (heavy cloud) | Unsettled, Rain at Times; Unsettled, Rain Later; Changeable, Some Rain; Showery, Becoming More Unsettled; Showery, Bright Intervals |
| Some cloud, dry | Fine, Possibly Showers; Fairly Fine, Showers Likely; Fairly Fine, Showery Later; Fairly Fine, Possibly Showers; Fine, Becoming Less Settled; Changeable, Mending |
| As before, by the icon | every other text, and any text that is not one of the 26 |

Snow keeps its rule (the snow icon: snow at 38 F or colder, rain above). Fog, heat and chill
still take over a dry sky.

## The Lookout

The Lookout keeps its own sun and its own sunset; none of this touches it (no real sun, no cloud
deck, no rain darkening there). The two meet at the fade between loops. In the half hour before
sunset the camp's real sun is low on the right; the Lookout then plays its sunset once; the camp
loop after it opens with the real sky again, the sun down behind the ridges in dusk colours.

## Cost

- No new full-frame buffer and no new table. The scene's own block grows by 72 bytes (PSRAM;
  28,344 of its 32,768); the panel host keeps about 60 bytes more (internal RAM). Flash: about
  6 to 8 KB (an estimate from the PC build, where the core grew 6.4 KB; not built for the panel).
- Per frame: nothing. The sun, the moon, the stars and the cloud deck sit in the cached
  background. When the sun or moon moves a pixel (about once a minute each) only the rows it is
  in are repainted in the cache: on the PC that is 7 percent of a whole background redraw, so
  about 8 ms on the panel, once a minute. A change of cloud cover, or the stars changing as the
  moon rises or sets, is one whole redraw (about 110 ms on the panel), a few times a day.
- A rainy or gray sky's whole redraw is a little slower (the deck is about 25 more ellipses).

## To build these again

See the top of `screensaver/tools/sky_stills.cpp`, then `screensaver/tools/sky_sheet.py`.

## Not yet judged

- The rain levels on the panel itself: its screen and backlight are not this monitor.
- The night tiles in a dark room.
- The once-a-minute repaint on the panel (expected about 8 ms; not measured there).
- The panel host change in `ss_host.cpp` is written but not compiled (no firmware build was run).
