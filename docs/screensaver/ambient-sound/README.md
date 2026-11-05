# Ambient sound: a saved concept

Stage 11 of the screensaver plan was optional, quiet ambient sound. It was designed and a working
generator was written on 5 November 2026, and then Mike decided not to put it in the panel:

> Save the concept of the sounds, but don't implement. I think they would be distracting after the
> initial curiosity wears off.

So nothing here is built into the firmware. This folder keeps the idea and the code in case it
comes back.

## What it sounds like

Everything is made as it plays from filtered noise and a few sine voices. There are no recordings,
as the brief asked.

| Sound | How it's made |
|---|---|
| The stream | Two noise bands for the body and the hiss, four narrow resonators that wander to a new pitch and loudness every 40 to 160 ms (the babble), and about six small rising "bloops" a second (the bubbles) |
| Wind | Low, slowly drifting noise whose strength follows gusts (a new one every 1.5 to 5 s, reached in about a second), with a soft whistle that rises in pitch with the gust |
| Rain | A steady high hiss and a low wash, with 60 to 310 droplet ticks a second as the rain grows |
| The fire | Short bright crackles (2 to 27 a second), now and then a small pop, and a low roar |
| Birds | Now and then a song: a chickadee's two-note "fee-bee", a quick trill, or a short warble with vibrato, at a random distance |

Every level glides to its target over about 0.8 s, so nothing clicks when the scene changes. A
gentle limiter keeps the mix under about -6 dBFS; measured peaks were -8 to -12 dBFS.

## Listening to it

`render_samples.cpp` renders eight samples to WAV: the stream alone, a day mix (stream, breeze,
birds), evening by the fire, wind, rain, a storm, the fire alone and the birds alone.

```
g++ -O2 -std=c++17 render_samples.cpp ambient.cpp -o render_samples
./render_samples out_dir
```

## What it would cost on the panel

- **PSRAM: none.** The generator is 904 bytes and would live in the audio task's internal RAM. It
  writes 512 frames at a time through the 2 KB buffer the chimes already use, and never touches
  PSRAM, so it wouldn't compete with the display or the screensaver for memory bandwidth. (Mike
  asked about this specifically: free PSRAM while the screensaver runs is about 219 KB.)
- **Processor:** about 0.03 µs a sample on the PC. On the panel's core 0 that was estimated at
  roughly a tenth of the core, never measured.
- **Flash:** a few KB.

## How it would plug in

These are the parts that were planned and not built:

1. **The audio task** (`firmware/tcomm/src/board/board_audio.cpp`) would stream the ambient sound in
   512-frame chunks while it's on. The microphone check that every sound was heard (a silent one
   resets the codec) must not see the ambient stream, which is quiet on purpose.
2. **The doorbell comes first.** When a chime or a reply sound is queued, the ambient fades out over
   one chunk (about 12 ms), the sound plays at its own volume, and the ambient fades back in. The
   codec's volume would change only during the silence between them.
3. **The amp** stays off whenever ambient sound is off, as the brief requires (no idle hiss).
4. **Settings:** an AMBIENT SOUND tile, off by default (another field at the end of `Settings`,
   with the saved-settings conversion that `board_storage.cpp` does for each new version).
5. **When it plays:** only while the screensaver runs, only with the setting on, and never in quiet
   hours (the brief allowed a cap instead; muting was the simpler choice).
6. **What it follows:** the screensaver would hand the generator its levels each frame: the stream
   beside the camp, the fire while it's lit, wind and rain from the scene's weather (the gusts and
   the storm too), birds by day, and only wind for the Lookout. The master level would follow the
   screensaver's fades, so the sound rises and settles with each loop.

## If it comes back

- Tune the sounds by ear on the panel's speaker, not the laptop's.
- Measure its processor share on core 0 with the screensaver and the radio running.
- Check that the chime's "heard" test still works with the ambient bed playing underneath.
