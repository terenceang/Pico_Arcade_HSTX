# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An emulator of the real 1978 Taito/Midway Space Invaders arcade PCB (Intel 8080 CPU,
memory map, I/O ports, shift-register sprite hardware - `src/emu/`) for the Raspberry Pi Pico 2
(RP2350), written in C against the Raspberry Pi Pico SDK, driving DVI video
output over HDMI. It runs the *actual* arcade ROM, not a
reimplementation of the game logic - see `Emulator.md`. **Status: Feature Complete** - the HSTX HDMI
video pipeline (`src/video/`, `lib/pico_hdmi`), the CPU core + video output
(`src/emu/`, `src/game.c`), SNES-controller input (`src/input/`, mapped to the
emulated machine's coin/start/joystick/fire inputs), and sound-effect playback
(`src/audio/`, driven by the emulated machine's own port 3/5 writes, embedded into
HDMI Data Islands by `pico_hdmi`) are built and working. See the Roadmap in `README.md`.

**The real arcade ROM is required and is not in this repo** (Taito's copyrighted work -
see `roms/README.md`). Without it in `roms/`, the build substitutes a zero-filled
placeholder so compilation still succeeds, but the firmware won't run the actual game.

## Build

Targets Pico SDK 2.3.0 and the `pico2` board definition (referenced via `PICO_BOARD` in `CMakeLists.txt`).

```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2
cmake --build build
```

This produces `build/Space_Invader_PICO.uf2` (flash by holding BOOTSEL while plugging in
the board, then copying the `.uf2` to the drive that appears).

Opening the folder in VS Code with the Raspberry Pi Pico extension also works; `.vscode/`
is already configured for it (build/run/flash/reset tasks in `.vscode/tasks.json`, SDK
paths in `.vscode/settings.json`). There is no separate lint step or test suite - this is
firmware; correctness is verified by building and by flashing/observing on real hardware.

There is no CI here. When you change anything under `src/`, build (`cmake --build build`)
before considering the change done - a broken build is the primary failure mode this
project can catch automatically.

### Debug test cards

`src/display_config.h` controls two independent debug screens shown before/instead of the
game, both off by default:

```c
#define DEBUG_TESTCARD 0             // 1 to show the color-bar/grayscale test card at boot
#define DEBUG_TESTCARD_SECONDS 5     // seconds to show it before handing off (0 = permanent)
#define DEBUG_CONTROLLER_TESTCARD 0  // 1 to show a live SNES button diagram instead of the game
```

`testcard.c` draws the color-bar/grayscale/moving-sync-bar pattern; `controller_testcard.c`
draws a button-diagram (D-pad, face buttons, shoulders, select/start) that lights each
button green while held, using the same `snes_controller_read()` the game itself uses - a
hardware/wiring check independent of the emulator or ROM. If both are enabled, the
color-bar card shows first, then the controller card. See `main.c` for how the two are
sequenced into the per-scanline render loop.

## Architecture

Read `Emulator.md` before touching anything under `src/emu/` or `src/game.c`, and
`Video.md`/`Hardware.md` before touching anything under `src/dvi/` or `src/dvi_display.*`
- they cover the pipeline, timing budget, and board-specific pinout gotchas in depth.

### Emulator core (`src/emu/`, `src/game.c`)

`src/emu/i8080.c` is a from-scratch Intel 8080 interpreter: full documented instruction
set plus the well-known undocumented opcode duplicates real 8080 silicon has (e.g.
`0xCB`=`JMP`, `0xD9`=`RET`, `0xDD/0xED/0xFD`=`CALL`) - these are hardware facts to
preserve, not bugs to fix. It has zero dependency on the Pico SDK or this project's
memory map; it's wired to a specific machine purely through the `mem_read`/`mem_write`/
`io_in`/`io_out`/`ctx` function pointers in `i8080_t`.

`src/emu/invaders_machine.c` wires that CPU to the real Space Invaders arcade memory map
(`$0000-$1FFF` ROM, `$2000-$3FFF` RAM including video RAM at `$2400`) and I/O ports
(inputs, DIP switches, and the 16-bit shift register the real hardware uses to draw
bit-shifted sprites - `OUT 4` shifts a byte in, `OUT 2` sets a 0-7 bit read offset, `IN 3`
reads the shifted result).

`src/game.c` doesn't contain game logic - it runs the emulated CPU in small slices
interleaved with each scanline (`SI_CYCLES_PER_ROW`), firing the two real per-frame
interrupts (`RST 1` mid-screen, `RST 2` vblank) at the scanline calls nearest their real
timing, then samples the emulated machine's 256x224 1bpp video RAM into the 320x240
RGB565 framebuffer (letterboxed, with the classic red/green cabinet overlay tint applied
at this conversion step). Running the CPU in small per-scanline slices rather than one
big per-frame burst is required by the DVI pipeline's hard timing budget below - see
`Emulator.md`'s "Interrupt timing" section for why.

**Screen orientation**: the real cabinet's monitor is mounted vertically (portrait), not
landscape - `SI_DISPLAY_ROTATION` (0/90/180/270 degrees) and `SI_DISPLAY_FLIP_H`/
`SI_DISPLAY_FLIP_V` in `src/display_config.h` tell `render_arcade_row()` how the physical
display here is actually mounted; 16 combinations cover every fixed orientation. If the
image comes out wrong (sideways, upside down, mirrored, or overlay bands on the wrong
edge), that's a `display_config.h` value to try, not a rendering-code change - see
`Emulator.md`'s "Screen orientation" section, which also explains why this is exposed as
a couple of numbers to experiment with rather than one hardcoded transform: several
earlier attempts at deriving "the one correct" transform by hand were each wrong in a
different way.

Inputs are wired via `invaders_machine_set_in1()`, called every frame from `game.c` with
the SNES controller's decoded button state (`src/input/snes_controller.c`) -
SELECT/START/LEFT/RIGHT/A|B|X|Y map to coin/start/joystick/fire respectively.

**The real arcade ROM is not vendored** - it's loaded from 4 user-supplied files in
`roms/` (gitignored) and embedded into the flash image at build time by
`cmake/generate_rom.cmake` (see `CMakeLists.txt`'s custom command generating
`generated/rom_data.c`). If you add anything that needs to know ROM contents at build
time, that generated file / `src/emu/rom_data.h` is where to look.

#### DVI & HDMI Audio pipeline (`lib/pico_hdmi`, `src/video/dvi_display.*`)

The essentials:

**Hardware HSTX on RP2350:** Video output and HDMI Data Island audio are driven by
`lib/pico_hdmi` - a high-performance RP2350 hardware HSTX driver utilizing GPIO 12-19,
640x480p60 output, 320x240 8bpp palettized framebuffer, and 48 kHz stereo PCM HDMI embedded audio.

**System Architecture**:
- **Core 0** (`main.c`, `dvi_display.c`): scanline/frame producer & CPU emulator. Updates the
  320x240 8-bit palette-indexed framebuffer (`fb`), converts the whole frame to pre-packed RGB565
  once per frame (`dvi_display_convert_frame()`), and pushes 48 kHz PCM audio samples via
  `audio_i2s_feed_queue()` (called repeatedly through the frame, not as one burst).
- **HSTX Engine** (`pico_hdmi`, Core 1): `dvi_scanline_ptr_cb()` - a scanline *pointer* callback,
  not a fill callback - just returns the address of the frame Core 0 already converted, doing zero
  per-pixel work on this time-critical path. Hardware TMDS-encodes scanlines over HSTX, and injects
  audio Data Islands during H-blanking. **This split matters**: Core 1's ISR previously did the
  8bpp->RGB565 palette lookup itself, which fit pure-DVI mode's per-line budget but not HDMI mode's
  (Data Island construction shares that budget) and caused total, permanent loss of HDMI signal
  lock - not just missing/glitchy audio. See `Video.md`'s pipeline overview before changing either
  side of this split.

**Resolution scaling & Palette LUT**:
Logical 320x240 8bpp framebuffer is converted to RGB565 and doubled to 640x480 wire timing in
software during the Core 0 conversion pass (not HSTX hardware pixel-doubling - see Video.md).
256 palette entries (0xRRGGBB) are mapped via `dvi_display_set_palette()`.

### File map

| Path | Role |
|---|---|
| `src/main.c` | Entry point; Core 0 scanline/frame producer loop & audio dispatch |
| `src/game.c` / `.h` | Paces the emulated CPU against the frame loop, converts video RAM into 8bpp scanlines |
| `src/emu/i8080.c` / `.h` | Intel 8080 CPU interpreter - full instruction set, no machine-specific knowledge |
| `src/emu/invaders_machine.c` / `.h` | Space Invaders memory map, I/O ports, shift register, interrupt delivery |
| `src/emu/rom_data.h` | Declares the embedded ROM array defined by the CMake-generated source |
| `roms/` | User-supplied real arcade ROM files go here (gitignored, not vendored) |
| `cmake/generate_rom.cmake` | Embeds `roms/invaders.{h,g,f,e}` into a linkable C array at build time |
| `src/video/` | Video & display pipeline (`dvi_display.c`/`.h`, `display_config.h`, `testcard.c`/`.h`, `controller_testcard.c`/`.h`) |
| `lib/pico_hdmi/` | Core RP2350 hardware HSTX DVI + HDMI Data Island audio driver library |
| `src/audio/audio_i2s.c` / `.h` | Software audio mixer - mixes sound-effect voices into 48 kHz stereo PCM and pushes Data Islands |
| `src/audio/sound_effects.c` / `.h` | Decodes port 3/5 sound-effect bits into `audio_i2s_*` calls |
| `src/audio/sound_data.h` | `sound_id_t` enum + `sound_sample_t`/`sound_table[]` declarations |
| `sounds/` | User-supplied sound-effect PCM files |
| `cmake/generate_sounds.cmake` | Embeds `sounds/*.pcm` into `sound_table[]` at build time |
| `Hardware.md` | Board pinout and hardware specs |
| `Video.md` | Full HSTX DVI pipeline writeup, timing budget, why you can't block Core 0/1 |
| `Emulator.md` | 8080 core + arcade machine emulation writeup, video RAM rotation, known limitations |

## Adding new source files

New `.c`/`.S` files must be added explicitly to the `add_executable(Space_Invader_PICO ...)`
list in `CMakeLists.txt` - there's no globbing. If a new file needs a PIO program, add it
via `pico_generate_pio_header()` following the existing `snes_controller.pio` example.

## Key Attributions

- `lib/pico_hdmi`: HSTX HDMI library for RP2350 (https://github.com/fliperama86/pico_hdmi).
- `PicoDVI-audio`: Shuichi Takano (https://github.com/shuichitakano/PicoDVI-audio).
- `PicoDVI`: Luke Wren (`Wren6991`, https://github.com/Wren6991/PicoDVI).
- `Space Invaders Arcade Hardware Specifications`: Computer Archeology team & Paul Robson (http://www.computerarcheology.com/Arcade/SpaceInvaders/).
