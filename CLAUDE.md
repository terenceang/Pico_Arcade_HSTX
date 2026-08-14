# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An emulator of the real 1978 Taito/Midway Space Invaders arcade PCB (Intel 8080 CPU,
memory map, I/O ports, shift-register sprite hardware - `src/emu/`) for the Raspberry Pi Pico 2
(RP2350), written in C against the Raspberry Pi Pico SDK, driving DVI video
output over HDMI. It runs the *actual* arcade ROM, not a
reimplementation of the game logic - see `Emulator.md`. The HSTX HDMI
video pipeline (`src/video/`, `lib/pico_hdmi`), the CPU core + video output
(`src/emu/`, `src/game.c`), and sound-effect playback
(`src/audio/`, driven by the emulated machine's own port 3/5 writes, embedded into
HDMI Data Islands by `pico_hdmi`) are built and working. **No input device is currently
wired up** - a SNES controller driver used to map to the emulated machine's
coin/start/joystick/fire inputs (`invaders_machine_set_in1()`) but was removed after being
implicated in the HDMI sync-loss investigation below; the game currently just idles on the
attract screen. See the Roadmap in `README.md` and the "HSTX sync-loss caveat" below.

**The real arcade ROM is required and is not in this repo** (Taito's copyrighted work -
see `roms/README.md`). Without it in `roms/`, the build substitutes a zero-filled
placeholder so compilation still succeeds, but the firmware won't run the actual game.

## HSTX sync-loss caveat (STATUS: root cause unconfirmed, auto-recovery mitigation added as of 2026-08-14)

This project has reproduced a real hardware failure mode in the vendored HDMI HSTX/Data Island path:
Core 0 stays alive and running, but after some period of normal operation, Core 1's scan clock snaps from
60 Hz to a fixed ~137.8 Hz (~2.3x) and never recovers. It is not a CPU crash, not a corrupted emulator
state, and not a simple DMA-length corruption visible from a serial log.

**The audio-queue-churn theory below was the leading hypothesis and has since been disproven as the
(sole) cause.** `src/main.c` / `src/audio/audio_i2s.*` were reworked so the Data Island queue is (a) fed
continuously across the *entire* frame period - including Core 0's idle tail between frames, not just its
short render/convert compute burst, which is where the queue was silently going unfed before - and (b) kept
at a deep ~200-packet (~one video frame) buffer depth so ordinary Core 0 scheduling jitter can't drain it,
while every individual refill call still only ever pushes a small, bounded number of packets (never one big
catch-up burst). Audio is now verified smooth and glitch-free on real hardware (use `DEBUG_AUDIO_TEST_TONE`'s
continuous 1kHz tone to check this independently of game sound effects) - but the HDMI sync-loss still
happens after a while under the same conditions. That means either the trigger isn't audio-related at all,
or audio-queue churn was only one of several contributing factors, not the root cause.

Original (now-superseded) hypothesis, kept for context: sustained high-rate audio-queue churn while Core 0's
frame render falls behind was believed to be the trigger - a steady empty queue from boot, or a one-time
real-to-silence transition, did not reproduce it, but repeated large refill bursts under backlog did. The
mitigation of bounding queue refills is still good practice and is kept in the current code, but should no
longer be treated as *the* fix for this failure - only as one contributing precaution.

**Confirmed narrowing (soak test on real hardware, `DEBUG_TESTCARD 1` + `DEBUG_AUDIO_TEST_TONE 1`, permanent
test card, game never shown):** does not reproduce. With the test card running indefinitely, `game.c`'s
`game_render_scanline()` is never called, which means `i8080_step()` never executes (zero CPU emulation),
`invaders_machine_interrupt_mid_screen()`/`_vblank()` never fire, and `snes_controller_read()` is never
polled - only `testcard_render_scanline()` (a handful of cheap `memcpy`s) plus the now-fixed continuous
audio feeding are running every frame, indefinitely, with no drop. This rules out the base HSTX/DMA video
path and the audio path as standalone causes (already suspected from the audio fix above, now confirmed by
a direct on/off test) and narrows the trigger to something that only runs while the game is active: CPU
emulation (`i8080_step`/`invaders_machine_run_cycles`), the per-frame RST1/RST2 interrupt delivery, SNES
controller PIO polling, or `render_arcade_row()`'s per-pixel VRAM sampling in `game.c` (as opposed to
`testcard.c`'s simpler fixed-pattern fill) - any of which could be adding enough sustained Core 0 latency,
or triggering some workload-dependent DMA/IRQ interaction, that the test card's much lighter, static
workload never hits.

**Confirmed narrowing #2 (soak test, `DEBUG_CONTROLLER_TESTCARD 1` + `DEBUG_AUDIO_TEST_TONE 1`, still zero
CPU emulation/interrupts):** DOES reproduce. This isolates the trigger further than expected: `game_init()`
calls `snes_controller_init()` unconditionally on every build regardless of which debug mode is active, so
PIO2's state machine was already configured identically in the test-card run above - the only actual
runtime difference in this run is that `snes_controller_read()` gets *called* once per frame, which drains
the PIO RX FIFO.

`src/input/snes_controller.pio` is a free-running, autonomous program (no `.wrap_target`/`.wrap`, so it just
loops forever): it pulses latch/clock and pushes a fresh 16-bit sample to the 4-word-deep RX FIFO roughly
every 46us, with no gating on whether anyone is reading. Left undrained (the plain test-card run), the FIFO
fills in ~4*46us =~ 184us after boot and the SM then permanently stalls on `push block` - it goes electrically
silent on GPIO2-4 for the rest of the run, a one-time burst. Drained once per frame (this run, and the real
game's normal input handling), each read empties the FIFO and lets the SM burst back into ~184us of activity
before re-filling and stalling again - so instead of one brief burst at boot, GPIO2-4 gets a recurring ~184us
burst of PIO activity once every ~16.67ms, indefinitely. That recurring bursty pattern - not CPU emulation,
which still isn't running in this test - is the leading suspect now, and would also explain why the failure
takes "a while" to appear: it plausibly needs many repeated bursts to hit whatever rare timing coincidence
with Core 1's HSTX work is actually responsible (a shared bus/DMA-arbitration hazard between the PIO block
and HSTX peripheral, or electrical noise coupling from the continuous ~1MHz-ish GPIO toggling into the HSTX
differential output, are both plausible at the RP2350 silicon/board level - this is speculative and unverified,
not confirmed).

**Confirmed narrowing #3 (soak test, real game - `DEBUG_TESTCARD 0`, `DEBUG_CONTROLLER_TESTCARD 0` - with
`DEBUG_SKIP_CONTROLLER_POLL 1` so `snes_controller_read()` is never called, `DEBUG_AUDIO_TEST_TONE 1`):**
DOES reproduce. This was meant to isolate "PIO polling" from "CPU emulation" as independent variables - and
it shows CPU emulation is independently *also* sufficient on its own, without any SNES polling at all. Taken
together with narrowing #2, **both** "poll the SNES PIO FIFO every frame, no CPU running" and "run the real
CPU emulation every frame, no polling" each independently reproduce the failure, while the plain test card
(narrowing #1: neither) does not. That rules out either one being *the* single root cause.

The common thread across the two failing configurations, and the one thing missing from the passing one:
the test card's per-frame Core 0 cost is identical every single frame (fixed-size `memcpy`s, no
data-dependent branching) - the controller test card's PIO refill bursts and the real game's CPU
emulation (game-state-dependent instruction mix, real ROM code, real interrupts) both vary frame-to-frame
instead. This points toward the underlying `pico_hdmi` HSTX/DMA engine being sensitive to Core 0
*irregularity/jitter* in some form, not to a specific subsystem in this project's own code, and not simply
to "how much work" Core 0 does per frame.

**Confirmed narrowing #4 (soak test, real game with `DEBUG_CPU_NOP_ROM 1` + `DEBUG_SKIP_CONTROLLER_POLL 1` +
`DEBUG_AUDIO_TEST_TONE 1`):** does NOT reproduce. `i8080_step()` ran the full real cycle count every
scanline and RST1/RST2 fired and got taken normally, but with the ROM forced to all-NOP, VRAM never changed
(constant all-black `render_arcade_row()` output every frame) and no sound port was ever hit. This survived
indefinitely despite doing substantially *more* total Core 0 work per frame than the plain test card
(narrowing #1) that also survived - ruling out "how much Core 0 work" as the deciding factor, and also
weakening the earlier jitter/irregularity theory, since a NOP loop's dispatch cost is highly uniform yet
this ran far more of it than narrowing #1 without dropping.

**Confirmed narrowing #5 (soak test, `DEBUG_CONTROLLER_TESTCARD 1` + `DEBUG_SKIP_CONTROLLER_POLL 1` +
`DEBUG_AUDIO_TEST_TONE 1` - the controller diagnostic card shown, but with its own `snes_controller_read()`
call also skipped via the same flag):** DOES reproduce, with the SNES PIO FIFO never drained (same
boot-then-stall PIO behavior as the passing narrowing #1 test) and zero CPU emulation running. This overturns
narrowing #2's conclusion: it wasn't SNES PIO polling causing the earlier failure after all - the common
factor across narrowing #2 and #5 is `src/video/controller_testcard.c`'s own render loop (a `memset` plus a
nested per-scanline loop over 12 button boxes with bounds checks), which neither narrowing #1's plain
`memcpy`-based test card nor narrowing #4's real, optimized `render_arcade_row()` lookup-table path shares.

**Net result across all five tests: no single subsystem was cleanly isolated as sufficient and necessary.**
Real CPU emulation with real interrupts survived when content was static (#4); a debug-only, comparatively
simple render loop reproduced the failure with zero CPU emulation and zero PIO activity (#5); SNES PIO
polling looked implicated (#2) but turned out not to be necessary (#5 dropped without it). The one subsystem
implicated across multiple tests, directly or as the "only remaining difference," is the SNES controller
input system (`src/input/` and its `controller_testcard.c` diagnostic) - so **it has been removed from the
project entirely** (see the commit that added this note) rather than continuing to chase an inconclusive
signal. This is a mitigation attempt, not a confirmed fix: whether the sync-loss still reproduces with the
real game (no test cards, no SNES code at all) has not yet been soak-tested on hardware as of this note.
If you touch the audio queue, HSTX timing path, or Core 1's ISR - or reintroduce any input method - treat
sustained HDMI sync-loss as a still-open bug and validate on hardware before claiming any related change
fixes it.

**Confirmed narrowing #6 (soak test, real game, SNES controller code entirely removed from the project):**
DOES reproduce. SNES removal did not fix the sync-loss - confirming it was never the (sole) root cause. This
prompted actually reading `lib/pico_hdmi/src/video_output.c`'s DMA/ISR engine instead of continuing to
toggle Core 0 subsystems on and off, since five tests had failed to isolate a Core-0-side culprit at all.

**Likely mechanism found.** `video_output_force_resync()`'s own doc comment in that file describes a known
pico_hdmi failure mode that matches this bug's symptom exactly: "a single corrupted/mis-sized [HSTX] command
word makes the expander misinterpret everything after it, permanently - the sink loses lock while scanlines
'complete' at bus speed because the FIFO no longer back-pressures [the DMA engine]." That "scanlines complete
at bus speed" is what CLAUDE.md's original description ("Core 1's scan clock snaps from 60Hz to a fixed
~137.8Hz and never recovers") was actually observing - not a literal clock change, but the DMA engine racing
ahead unthrottled once the HSTX command stream desyncs, permanently, until something explicitly restarts it.
The library ships exactly that recovery function - **but nothing in this project was ever calling it.**
Once desynced, this project just stayed desynced forever, matching "never recovers" precisely.

This also explains why five different, seemingly-contradictory Core-0-workload tests all pointed in
different directions: `dma_irq_handler()` (`video_output.c`) runs on Core 1 at the highest IRQ priority, but
it's still a plain CPU instruction stream sharing the same memory bus as Core 0 - and it synchronously calls
our `scanline_pointer_callback()` *inside the ISR* to read `rgb565_lines[]`, which Core 0 concurrently writes
in `dvi_display_convert_frame()`. If Core 0's bus traffic ever stalls that ISR at the wrong instant for long
enough to corrupt a DMA-posted command word, this exact permanent desync could result - which would depend
on rare timing coincidences rather than "what kind" of Core 0 work is running, exactly matching how
inconsistent narrowings #1-#5 looked from the Core-0 side. This mechanism is a strong candidate, not
confirmed with certainty - the actual trigger for the corrupted command word itself (a genuine race in
`dma_irq_handler()`'s own state machine, bus-arbitration starvation, or something else in the HSTX/DMA
setup) is still unknown.

**Mitigation implemented (not a root-cause fix): an automatic recovery watchdog.** `dvi_display.c`'s
`hdmi_sync_watchdog_task()` runs as pico_hdmi's Core 1 background task (registered via
`video_output_set_background_task()` in `core1_main()`) - self-contained on Core 1, since
`video_output_force_resync()` is documented safe to call from Core 1 thread context specifically (it
manipulates Core-1-owned DMA/HSTX state; calling it from Core 0 would not actually mask Core 1's own NVIC
and would race). It compares `video_frame_count`'s actual advance against wall-clock time every ~0.5s and
calls `video_output_force_resync()` if the rate is far above nominal 60Hz (threshold tuned well above normal
jitter, well below the ~137.8Hz/2.3x desync rate - see the constants in `dvi_display.c`). `main.c` logs a
`[WARN]` line whenever `dvi_display_get_hdmi_resync_count()` increases, so recoveries are visible on the
serial console. This turns a permanent, unrecoverable signal loss into a brief, self-healing glitch (at most
~0.5-1s of visibly wrong output) instead - a real improvement even without knowing the root cause, but not
a substitute for finding and fixing whatever actually corrupts the command stream. Validate on hardware:
confirm the watchdog actually fires and recovers when the failure reproduces, and that it doesn't
false-trigger during ordinary operation.

If this mitigation turns out insufficient, or you want to pursue the actual root cause: instrument
`dma_irq_handler()` itself (e.g., a cheap counter/timestamp at entry, checked from Core 0) to see whether its
own execution time occasionally spikes right before a desync; consider whether `scanline_pointer_callback()`
being called synchronously inside the highest-priority ISR is inherently fragile under Core 0 bus contention
regardless of what Core 0 is doing; and whether the failure correlates with wall-clock uptime/frame count
rather than any specific workload. The SNES controller subsystem remains removed (see the commit that added
this note) since it's still one less variable, even though it's now confirmed not to have been the cause.

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

`src/display_config.h` controls a debug screen shown before/instead of the game, off by
default:

```c
#define DEBUG_TESTCARD 0             // 1 to show the color-bar/grayscale test card at boot
#define DEBUG_TESTCARD_SECONDS 5     // seconds to show it before handing off (0 = permanent)
```

`testcard.c` draws the color-bar/grayscale/moving-sync-bar pattern - a display-pipeline
sanity check independent of the emulator or ROM. See `main.c` for how it's sequenced into
the per-scanline render loop. (There used to be a second controller-diagnostic test card
alongside this one; it was removed along with the SNES controller driver it exercised - see
the "HSTX sync-loss caveat" above.)

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

`invaders_machine_set_in1()` is the machine's input API (coin/start/joystick/fire), but no
input device currently calls it - a SNES controller driver used to, from `game.c`, but was
removed after being implicated in the HDMI sync-loss investigation (see the caveat above).
The game currently just idles on the attract screen.

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
| `src/video/` | Video & display pipeline (`dvi_display.c`/`.h`, `display_config.h`, `testcard.c`/`.h`) |
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
list in `CMakeLists.txt` - there's no globbing. Nothing in this project currently uses PIO
(the SNES controller driver did, via `pico_generate_pio_header()` and the `hardware_pio`
link library, both removed along with it - see the "HSTX sync-loss caveat"); a future PIO
consumer would need to add both back.

## Key Attributions

- `lib/pico_hdmi`: HSTX HDMI library for RP2350 (https://github.com/fliperama86/pico_hdmi).
- `PicoDVI-audio`: Shuichi Takano (https://github.com/shuichitakano/PicoDVI-audio).
- `PicoDVI`: Luke Wren (`Wren6991`, https://github.com/Wren6991/PicoDVI).
- `Space Invaders Arcade Hardware Specifications`: Computer Archeology team & Paul Robson (http://www.computerarcheology.com/Arcade/SpaceInvaders/).
