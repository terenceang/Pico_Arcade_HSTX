# Space Invaders Sound Effects (`sounds/space_invaders/` & `roms/space_invaders/`)

This directory contains the user-supplied sound sample files for the 1978 Space Invaders arcade machine (samples can also be placed directly in `roms/space_invaders/`).

The real 1978 arcade cabinet had no digital sound chip—its sound effects were produced by discrete analog circuitry (555 timers, noise circuits, and an SN76477 generator) triggered via I/O ports 3 and 5.

## Required Sound Files

Place the following 10 raw headerless PCM files directly in this folder (or in `roms/space_invaders/`):

| File | Port/Bit | Sound Description |
|---|---|---|
| `ufo.pcm` | Port 3, bit 0 | UFO background siren (loops while bit is active) |
| `shot.pcm` | Port 3, bit 1 | Player laser cannon shot |
| `player_die.pcm` | Port 3, bit 2 | Player base explosion ("Flash") |
| `invader_die.pcm` | Port 3, bit 3 | Invader destroyed |
| `extra_life.pcm` | Port 3, bit 4 | Bonus life awarded ("Extended play") |
| `fleet1.pcm` | Port 5, bit 0 | Invader march step 1 of 4 |
| `fleet2.pcm` | Port 5, bit 1 | Invader march step 2 of 4 |
| `fleet3.pcm` | Port 5, bit 2 | Invader march step 3 of 4 |
| `fleet4.pcm` | Port 5, bit 3 | Invader march step 4 of 4 |
| `ufo_hit.pcm` | Port 5, bit 4 | UFO destroyed explosion |

## Format Specification

- **Audio Format:** Raw headerless 16-bit signed little-endian PCM (`s16le`)
- **Channels:** Mono (1 channel)
- **Sample Rate:** 32,000 Hz

### Converting from WAV Files (e.g. MAME Samples)
You can use the built-in Python tool to automatically convert any standard WAV sample pack:
```sh
python tools/convert_samples.py roms/space_invaders/samples/
```

Or using `ffmpeg`:
```sh
ffmpeg -i input.wav -ar 32000 -ac 1 -f s16le roms/space_invaders/shot.pcm
```

If any files are omitted, the build will replace them with silent placeholders.
