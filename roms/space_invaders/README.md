# Space Invaders Arcade Assets (`roms/space_invaders/`)

Place the authentic 1978 Taito/Midway Space Invaders arcade ROM files and sound effect samples in this directory.

---

## 1. Required Program ROM Files (8 KB total, 2 KB each)

- `invaders.h` (2,048 bytes, loaded at $0000-$07FF)
- `invaders.g` (2,048 bytes, loaded at $0800-$0FFF)
- `invaders.f` (2,048 bytes, loaded at $1000-$17FF)
- `invaders.e` (2,048 bytes, loaded at $1800-$1FFF)

---

## 2. Sound Effect Samples (`.pcm` or `.wav`)

Space Invaders had discrete analog sound circuitry triggered by I/O ports. You can place raw 32 kHz 16-bit mono `.pcm` samples or standard MAME `.wav` files directly in this directory (or in `roms/space_invaders/samples/`):

| PCM File | WAV Name | Port/Bit | Description |
|---|---|---|---|
| `ufo.pcm` | `0.wav` / `ufo.wav` | Port 3, bit 0 | UFO background siren |
| `shot.pcm` | `1.wav` / `shot.wav` | Port 3, bit 1 | Player laser cannon |
| `player_die.pcm` | `2.wav` / `player_die.wav` | Port 3, bit 2 | Player base explosion |
| `invader_die.pcm` | `3.wav` / `invader_die.wav` | Port 3, bit 3 | Invader destroyed |
| `extra_life.pcm` | `9.wav` / `extra_life.wav` | Port 3, bit 4 | Bonus life awarded |
| `fleet1.pcm` | `4.wav` / `fleet1.wav` | Port 5, bit 0 | Fleet march step 1 |
| `fleet2.pcm` | `5.wav` / `fleet2.wav` | Port 5, bit 1 | Fleet march step 2 |
| `fleet3.pcm` | `6.wav` / `fleet3.wav` | Port 5, bit 2 | Fleet march step 3 |
| `fleet4.pcm` | `7.wav` / `fleet4.wav` | Port 5, bit 3 | Fleet march step 4 |
| `ufo_hit.pcm` | `8.wav` / `ufo_hit.wav` | Port 5, bit 4 | UFO destroyed explosion |

### Converting WAV files:
If you place `.wav` files here, convert them to `.pcm` using:
```sh
python tools/convert_samples.py roms/space_invaders/
```

---

## Notes:
- All ROM and PCM binary files in this folder are completely gitignored.
- If any file is missing, the build embeds a zero-filled placeholder.

