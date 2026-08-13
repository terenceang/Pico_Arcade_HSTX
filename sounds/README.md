# Space Invaders sound effects

This project runs the *real* Taito/Midway arcade ROM (see `roms/README.md`,
`src/emu/`), but the real cabinet's sound effects didn't come from that ROM
at all - they came from a discrete analog sound board (oscillators, noise
generators, filters wired directly to logic gates), not sampled audio. There
is nothing to extract from the ROM dump for sound, unlike the video.

**This repository does not, and will not, include sound recordings.** Supply
your own - a recording of a real cabinet, a legally-obtained sample pack, or
synthesized effects you're happy with - converted to the format below.

## What to put here

Drop 10 raw headerless PCM files, named exactly as below, directly in this
folder. Each corresponds to one of the real cabinet's discrete sound-effect
trigger bits (port 3/5 - see `src/audio/sound_effects.c`,
`src/audio/sound_data.h`):

| File | Port/bit | Effect |
|---|---|---|
| `ufo.pcm` | Port 3, bit 0 | UFO hum (loops for as long as the bit stays set) |
| `shot.pcm` | Port 3, bit 1 | Player shot fired |
| `player_die.pcm` | Port 3, bit 2 | Player ship destroyed ("Flash") |
| `invader_die.pcm` | Port 3, bit 3 | An invader destroyed |
| `extra_life.pcm` | Port 3, bit 4 | Extra life awarded ("Extended play") |
| `fleet1.pcm` | Port 5, bit 0 | Fleet movement thump 1 of 4 |
| `fleet2.pcm` | Port 5, bit 1 | Fleet movement thump 2 of 4 |
| `fleet3.pcm` | Port 5, bit 2 | Fleet movement thump 3 of 4 |
| `fleet4.pcm` | Port 5, bit 3 | Fleet movement thump 4 of 4 |
| `ufo_hit.pcm` | Port 5, bit 4 | UFO destroyed |

## Required format

**16-bit signed little-endian PCM, mono, 32000 Hz, no file header** (not a
`.wav` - the raw sample data only). `cmake/generate_sounds.cmake` embeds
these bytes directly into the flash image with no parsing or resampling, so
anything else (a WAV header still attached, a different sample rate, stereo)
will either fail to build cleanly or play back distorted/at the wrong pitch.

Converting from a WAV/MP3/etc. with `ffmpeg`:

```sh
ffmpeg -i input.wav -ar 32000 -ac 1 -f s16le sounds/shot.pcm
```

This folder is gitignored except this file - nothing you place here gets
committed.

**If you add these files after already having run `cmake` once**, re-run the
configure step (`cmake -S . -B build -G Ninja -DPICO_BOARD=pico2`)
before building again - CMake only notices these files exist at configure
time, not at build time.

## What happens if some are missing

`cmake/generate_sounds.cmake` embeds whatever's present; any missing file
gets a silent 0-length placeholder instead (with a CMake warning) - the
build still succeeds and the rest of the game's audio (and video) works
normally, that one effect just plays nothing.
