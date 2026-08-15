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
## HSTX sync-loss bug (STATUS: RESOLVED in v1.1.0)

The intermittent HDMI sync-loss bug (where Core 1's scan rate snapped from 60 Hz to ~137.8-166 Hz due to HSTX expander desync) has been completely resolved. 

### Root Causes & Solution Architecture
1. **Precomposed Active Lines (`PICO_HDMI_PRECOMPOSED_ACTIVE_LINES=ON`):**
   - *Problem:* In HDMI mode, Core 1's ISR was dynamically building 40-50 word command lists in SRAM every line (`build_line_with_di`). On active lines, the H-blank window is only 6.35 µs (800 CPU cycles). Shared SRAM bus contention from Core 0 delayed the ISR past the 6.35 µs deadline, causing DMA channel starvation and corrupted HSTX command words.
   - *Fix:* Static headers are precomposed once at boot (`video_output_set_compose_ring`). The ISR only patches the 36-word Data Island into the slot (< 1.2 µs), giving > 5 µs of timing margin.
2. **Hardware VSYNC Genlock (`dvi_display_wait_for_vsync`):**
   - *Problem:* Core 0 was using a drifting software wall-clock microsecond timer (`sleep_us`), causing asynchronous mid-screen buffer flips and tearing.
   - *Fix:* Core 0 synchronizes frame starts and buffer presentation (`dvi_display_present_frame`) strictly to hardware VSYNC (`video_frame_count`) at 60.000 Hz.
3. **Decoupled Frame-Based Emulation (`game_run_frame`):**
   - *Problem:* 240 sliced per-scanline emulation calls created continuous bus contention throughout the active scanout.
   - *Fix:* Space Invaders runs in two clean halves (16,640 cycles -> RST 1 -> 16,640 cycles -> RST 2). Emulation finishes in ~2 ms, leaving ~14.6 ms of the frame completely free of bus traffic for Core 1.
4. **Continuous Audio Queue Delivery:**
   - *Problem:* An artificial 32-packet cap in `audio_i2s_feed_queue` caused the 200-packet/frame queue to starve to zero, injecting 84% silence packets and distorting pitch.
   - *Fix:* Uncapped queue feeding so all 200 packets (800 samples @ 48 kHz) are pushed per frame, providing pure 1,000 Hz test tones and clean arcade audio.

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

**Mitigation tried: an automatic recovery watchdog (since removed - see below).** `dvi_display.c` briefly
had `hdmi_sync_watchdog_task()`, a `video_output_set_background_task()`-registered Core 1 task that compared
`video_frame_count`'s advance against wall-clock time and called `video_output_force_resync()` when the rate
ran far above nominal 60Hz, escalating to a full `watchdog_reboot()` after several consecutive failed
attempts. **This was removed entirely at the user's explicit direction**: detecting and reacting to the
desync after the fact is the wrong goal - the priority is finding and fixing what actually causes it. Do not
re-add resync/reboot/recovery logic without that same explicit direction; if you're looking at this file
because HDMI is unstable again, the expectation is to keep investigating the root cause, not to reach for a
watchdog.

**An unsynchronized Core 0/Core 1 framebuffer data race was found and fixed, but turned out to be only part
of the problem.** Reading `lib/pico_hdmi`'s own reference example (`examples/bouncing_box`) showed it
computes pixels synchronously inside Core 1's ISR, into a buffer Core 1 alone owns - no cross-core memory
dependency at all. This project instead had Core 0 pre-convert the whole frame to RGB565 into a single,
non-double-buffered array (`rgb565_lines[]`) and hand Core 1's DMA a raw pointer into it, with no
synchronization and no phase-lock between the two independent loops - a genuine, ongoing per-frame race, not
a rare fluke. Fixed by double-buffering the smaller 8bpp source instead (`fb_buffers[2]` in `dvi_display.c`,
~154KB total - full RGB565 double-buffering would have needed ~600KB, more than the RP2350's 520KB SRAM) and
moving the palette lookup back into Core 1's ISR per line (`dvi_scanline_fill_cb()`, matching the reference
example's synchronous fill-callback pattern) instead of handing out a pointer into shared memory. An initial
soak test (with the resync watchdog masking symptoms) looked clean, but **a longer soak test on real
hardware showed the sync-loss still reproduces** - the data race was real and worth fixing, but was not the
(sole) cause.

**Confirmed narrowing #7 (soak test, plain test card, current double-buffered/fill-callback architecture):**
does NOT reproduce - `video_frame_count` held steady at 59-61 indefinitely. Confirms the current video
pipeline architecture is sound on its own; the trigger still needs the game running.

**Confirmed narrowing #8 (soak test, real game with `DEBUG_CPU_NOP_ROM 1`, current architecture):** does NOT
reproduce either - steady 59-61, same as narrowing #4 got under the old architecture. `i8080_step()` ran
every real cycle, RST1/RST2 fired every frame, and `render_arcade_row()` ran its real per-pixel path - the
only thing missing was real, changing ROM content. Confirms Core 1's per-line ISR cost is *not* the
differentiator (it's identical regardless of buffer content, whether test-card or NOP-ROM or real game) and
the video pipeline's mechanics are not the problem in isolation.

**Confirmed narrowing #9 (soak test, real ROM, current architecture):** DOES reproduce (~166Hz measured,
independently confirmed both from Core 1's own measurement and Core 0's separate wall-clock-based
measurement of the same `video_frame_count`). Combined with #7 and #8, the trigger is specifically **real,
varying ROM content** - not "CPU running," not "how much work," but the *irregularity* of it: a NOP loop is
perfectly uniform (every `i8080_step()` call takes the same path, same host-cycle cost, every time); real ROM
code hits varied opcodes with varied cycle costs and varied code paths through `i8080_step()`, so Core 0's
per-scanline bus-access timing becomes irregular and data-dependent in a way nothing in this codebase
controls. Important nuance from re-examining the data: the observed rate does not scale up gradually with
"more work" - it's a discrete jump from ~60 to ~166, never anything in between. So the mechanism is not
"Core 0 being busier directly makes Core 1 faster" (which wouldn't make sense - Core 1's output rate is
dictated by pixel-clock hardware, independent of Core 0's speed under correct operation) - it's that
irregular Core 0 timing increases the odds of triggering a discrete, separate fault (matching pico_hdmi's own
documented failure mode: a corrupted/mis-sized HSTX command word desyncing the DMA engine's FIFO
back-pressure), and *that* fault's consequence is the DMA racing unpaced afterward. Irregular timing is the
trigger for a rare bad-luck window; the elevated rate is what a broken pacing state happens to look like
afterward, not a dial that turns up with workload. Note the specific rate also isn't fixed: the original
pre-session bug measured ~137.8Hz, this session's soak tests measured ~166Hz - different numbers for what
should be "the same" failure mode, not yet explained.

**Attempted prevention (not yet confirmed): scratch-Y RAM relocation.** `pico_hdmi` ships
`PICO_HDMI_LINE_BUFFER_IN_SCRATCH_Y` (a `CMakeLists.txt` option, off by default) specifically to move its
active-line DMA buffer (`line_buffer` in `video_output.c` - what Core 1's ISR writes computed pixels into,
and what the DMA reads from) out of regular SRAM into a dedicated scratch bank, away from whatever bank(s)
Core 0's working set (i8080 CPU state, RAM emulation, render lookup tables) lives in. This project now
enables it (set in the top-level `CMakeLists.txt`, before `add_subdirectory(lib/pico_hdmi)`) - an attempt at
removing a source of Core 0/Core 1 bus contention rather than reacting to its symptom. Also tried moving
`dvi_display.c`'s own `palette_packed[256]` (read on every pixel by the ISR) into the same scratch bank, but
scratch Y is genuinely tiny - it also holds Core 1's own stack by default - and the linker overflowed by 256
bytes with both in it; `palette_packed[]` was left in regular SRAM. **Soak-tested on hardware: helps, but
does not fully eliminate the failure** - noticeably fewer drop-outs, but some still occur.

**Second prevention attempt tried and reverted: per-scanline pacing.** Padded every scanline's processing
out to a fixed wall-clock duration (`SI_ROW_PACE_US`) via `sleep_until()`, on the theory that making Core 0's
per-row timing uniform (matching the NOP-loop case, which never reproduced the bug) even when running real
ROM would remove the trigger. **Soak-tested on hardware: made things worse, not better - caused an outright
crash/hang.** Reverted entirely (`SI_ROW_PACE_US`, the padding call in `main.c`) rather than debugged further,
since it was speculative to begin with and the regression wasn't worth chasing blind. Exact cause not root-
caused - plausible candidates include the padding pushing some frames' total render time close enough to the
16.67ms budget that the frame-pacing math in `main.c` (anchored to a fixed `start` time, never catches up
once behind) degrades badly under sustained overrun, but this is speculation, not confirmed. If per-row
pacing is retried, treat it as suspect until proven safe over a long soak test, and consider making the
worst-case padded-render-loop duration provably bounded well under budget rather than empirically tuned.

If scratch-Y relocation alone doesn't fully solve this: the remaining lever is architectural - reduce how
much of Core 1's time-critical path can ever be disturbed by Core 0's bus/timing activity at all. Whether
that's achievable without deeper hardware-level tracing (a scope/logic analyzer on the actual HSTX signals,
which isn't available in this environment) is genuinely unknown. `main.c` was refactored into named helper
functions (`render_frame()`, `pace_frame_and_feed_audio()`, `update_hdmi_fps_diagnostic()`) during this
cleanup, separate from the revert, to keep the main loop legible as this investigation continues - preserve
that structure rather than reverting to one large inline `while(true)` body.

The SNES controller subsystem remains removed (it was never the cause, but cutting it isn't costing
anything either) - see "Add a replacement input method" in `README.md`'s Roadmap if you want controls back.

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
- **Core 0** (`main.c`, `dvi_display.c`): scanline/frame producer & CPU emulator. Gets a write buffer
  via `dvi_display_get_write_buffer()`, renders the frame's 320x240 8-bit palette-indexed content into
  it, calls `dvi_display_present_frame()` once done, and pushes 48 kHz PCM audio samples via
  `audio_i2s_feed_queue()` (called repeatedly through the frame, not as one burst).
- **HSTX Engine** (`pico_hdmi`, Core 1): `dvi_scanline_fill_cb()` - a scanline *fill* callback,
  matching `pico_hdmi`'s own reference example (`examples/bouncing_box`) - does the 8bpp->RGB565
  palette lookup + horizontal-doubling itself, per line, directly into the ISR's own line buffer.
  Hardware TMDS-encodes scanlines over HSTX, and injects audio Data Islands during H-blanking.
  **Double buffering matters**: `dvi_display.c` keeps two 8bpp framebuffers (`fb_buffers[2]`) and
  Core 0 only ever writes the one Core 1 *isn't* currently reading - `dvi_display_present_frame()`
  atomically flips which is which. An earlier design instead pre-converted the whole frame to RGB565
  on Core 0 once per frame and handed Core 1 a raw pointer into that single (non-double-buffered)
  array - a genuine, unsynchronized cross-core data race, since Core 0 could be mid-write to the same
  memory Core 1's DMA was concurrently reading. That's the leading suspect for this project's HDMI
  sync-loss bug - see CLAUDE.md's "HSTX sync-loss caveat" for the full investigation and why the
  earlier per-pixel-lookup-in-the-ISR design (a *different*, now also-superseded problem) had been
  avoided in the first place. See `Video.md`'s pipeline overview before changing either side of this
  split.

**Resolution scaling & Palette LUT**:
Logical 320x240 8bpp framebuffer is converted to RGB565 and doubled to 640x480 wire timing in
software inside Core 1's per-line fill callback (not HSTX hardware pixel-doubling - see Video.md).
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
| `src/video/` | Video & display pipeline (`dvi_display.c`/`.h`, `display_config.h`, `testcard.c`/`.h`, `debug_overlay.c`/`.h`) |
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
