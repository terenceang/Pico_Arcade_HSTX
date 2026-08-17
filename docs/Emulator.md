# Emulator Core - How the "game" actually runs

This project doesn't reimplement Space Invaders' game logic in C. `src/emu/`
emulates the real 1978 Taito/Midway arcade PCB - an Intel 8080 CPU, its
memory map, its I/O ports and shift-register sprite hardware - and runs the
*actual arcade ROM* on it (supplied locally by the user, see
[`roms/space_invaders/README.md`](../roms/space_invaders/README.md); never vendored in this repo).
`src/games/space_invaders/space_invaders_plugin.c` is the glue that paces
that emulated CPU against this project's plugin frame loop, and
`space_invaders_video.c` turns its video RAM into a framebuffer frame for the DVI
pipeline in `src/video/`. For how that frame actually gets to the screen,
see [`Video.md`](Video.md) - this document stops at "here's a 320x240 8bpp
frame," which is where `Video.md`'s pipeline picks up.

This document covers the Space Invaders plugin specifically. Pac-Man
(`src/games/pacman/`, `src/emu/pacman_machine.c`) follows the same
plugin-boundary shape but isn't detailed here.

---

## File map

| File | Role |
|---|---|
| `src/emu/z80emu.c` / `.h` | Self-contained, cycle-accurate instruction-stepped Zilog Z80 emulator (Lin Ke-Fong's `z80emu` core) - registers, flags, all opcodes including undocumented/prefixed ones, interrupt delivery. Atomic instruction execution with exact T-state accounting, passing ZEXALL/ZEXDOC suites. Space Invaders' original Intel 8080 ROM and Pac-Man's Z80 ROM run on this core. |
| `src/emu/space_invaders_machine.h` / `.c` | The arcade PCB wiring around that CPU: ROM/RAM memory map, input ports, the shift-register sprite hardware, and the two per-frame interrupts. |
| `src/emu/pacman_machine.h` / `.c` | The Pac-Man arcade PCB wiring: ROM/RAM memory map, I/O ports, Namco 3-voice sound, sprite registers, and VBLANK interrupt mode 2. |
| `src/emu/space_invaders_rom_data.h` | Declares the embedded ROM array defined by the CMake-generated source (see below). |
| `roms/space_invaders/README.md` | What ROM files to supply and where. |
| `cmake/generate_space_invaders_rom.cmake` | Turns `roms/space_invaders/invaders.{h,g,f,e}` into a linkable C byte array at build time. |
| `src/games/space_invaders/space_invaders_plugin.c` | Runs the CPU for the frame (`run_frame`); mixes/resamples sound-effect voices (`render_audio`); maps `emulator_input_mask_t` to `IN1` (`update_inputs`). |
| `src/games/space_invaders/space_invaders_video.c` / `.h` | Un-rotates/scales video RAM into the 320x240 8bpp framebuffer; applies the classic color-overlay tint. |
| `src/games/space_invaders/space_invaders_config.h` | Rotation, scaling, screen offset, and DIP switch settings (was `src/display_config.h` before the plugin refactor). |

## The CPU core (`src/emu/z80emu.c`, running Space Invaders & Pac-Man)

Both Space Invaders and Pac-Man are powered by Lin Ke-Fong's cycle-accurate `z80emu`
engine (`src/emu/z80emu.c`, `src/emu/z80emu.h`). Unlike pin-stepped interpreters
that branch through thousands of micro-cycles on every clock tick, `z80emu` is an
instruction-stepped core that processes complete instructions atomically against
memory while tracking exact T-state cycles. This achieves sub-3.5 ms frame emulation
times on the RP2350 (over 10x faster than pin-level simulation) while passing rigorous
Z80 compliance test suites including ZEXALL and ZEXDOC.

Two 8080 flag quirks the old `i8080.c` deliberately implemented, that
mattered for this specific ROM, are worth knowing don't necessarily carry
over unchanged now that `z80.c` drives it:

- `DAA` (double-dabble BCD correction) matters because Space Invaders'
  scoring code keeps the on-screen score in packed BCD and uses `DAA` to
  maintain it - getting this wrong shows up immediately as garbled score
  digits. `z80.c` implements its own `DAA` (`_z80_daa()`), which is the
  real Z80's documented behavior, not a copy of `i8080.c`'s.
- The old `i8080.c` gave `ANA` an auxiliary-carry quirk (logical OR of bit 3
  of both operands, not always-0 like `XRA`/`ORA`) matching real 8080
  silicon. **This is confirmed different on the new core**: `z80.c`'s
  `_z80_and8()` unconditionally sets the half-carry flag (`Z80_HF`) instead
  - real, documented Z80 `AND` behavior, but not the 8080 quirk. Whether
  this or the `DAA`/prefix-byte differences above actually matter for this
  ROM hasn't been exhaustively checked here; the game visibly working
  correctly (score display, gameplay) is the practical evidence so far, not
  a flag-by-flag audit against the old `i8080.c`.

The core has zero dependency on the Pico SDK or this project's memory map -
`invaders_machine_run_cycles()` drives `z80_tick()` in a loop, inspecting the
returned pin mask to service memory/IO reads and writes against
`invaders_machine_t`'s own RAM/port state (there's no stored
`mem_read`/`mem_write`/`io_in`/`io_out` function-pointer table the way the
old, now-removed `i8080_t` had - `z80_t` is pin-driven, and the caller does
the dispatch).

## The machine (`src/emu/space_invaders_machine.c`)

Memory map:

| Range | Contents |
|---|---|
| `$0000-$1FFF` | ROM (`space_invaders_rom`, embedded from `roms/`) |
| `$2000-$23FF` | Work RAM |
| `$2400-$3FFF` | Video RAM (7168 bytes, 256x224 1bpp) |
| `$4000-$FFFF` | Mirrors `$2000-$3FFF` (real PCB doesn't fully decode the top address bits; the ROM never actually reads/writes up here) |

I/O ports (matching the real cabinet's wiring):

| Port | Direction | Purpose |
|---|---|---|
| 0, 1, 2 | Read | Input bits (coin/start/joystick/fire, DIP switches) |
| 3 | Read | Shift-register result (see below) |
| 2 | Write | Shift-register read offset (0-7 bits) |
| 4 | Write | Shifts a new byte into the shift register |
| 3, 5 | Write | Discrete sound-effect trigger bits - forwarded to `src/audio/sound_effects.c` (see below) |
| 6 | Write | Watchdog reset strobe - **not emulated**, no-op |

**The shift register** is the real hardware's trick for drawing
arbitrarily bit-shifted sprites (bullets, aliens, the player ship) without
the CPU doing the shifting itself: `OUT 4` pushes a new byte in from the
top (`shift_register = (new_byte << 8) | (shift_register >> 8)`), `OUT 2`
sets a 0-7 bit offset, and `IN 3` returns
`(shift_register >> (8 - offset)) & 0xFF` - an 8-bit window into the
16-bit register at an arbitrary bit position. The game uses this
constantly; without it, sprites would render torn or not move smoothly
between byte boundaries.

**Sound effects**: the real cabinet's sounds came from a discrete analog
sound board wired to port 3/5 bits, not a sample ROM - there's nothing to
extract for audio the way there is for video. `invaders_machine_t` exposes
an optional `sound_write` callback (NULL by default, so the machine still
runs standalone with no audio attached), which `space_invaders_plugin.c`'s
`init()`/`reset()` wire to `src/audio/sound_effects.c`. That file knows the
actual bit mapping (port 3: UFO/Shot/Flash/Invader die/Extended play/AMP-enable;
port 5: 4 fleet-movement thumps + UFO hit - see computerarcheology.com's
hardware writeup) and turns each into a call through the injected
`sound_play_fn`/`sound_stop_loop_fn` callbacks (`sound_effects_init()`) -
`space_invaders_plugin.c` wires those to its own `si_audio_play_sample()`
(one-shot, or looped for the UFO) / `si_audio_stop_loop()`, so
`sound_effects.c` has no direct dependency on the audio engine. The plugin's
`render_audio()` mixes up to 4 one-shot voices plus the UFO loop into one
mono stream (`voice_advance()`, nearest-sample resampling from
`SOUND_SAMPLE_RATE_HZ` to the 48 kHz transport rate), which
`src/platform/audio/audio_engine.c` then feeds into the HDMI Data Island
queue. The actual sample data behind each effect is **user-supplied**
(`roms/space_invaders/*.pcm`, gitignored - see `roms/space_invaders/README.md`) and embedded at build
time the same way the ROM is; a sound whose file wasn't supplied plays
nothing rather than failing the build.

**Inputs**: BOOTSEL is wired as coin/start via `src/platform/input/host_input.c`
-> `plugin->update_inputs()` -> `space_invaders_plugin.c`'s
`si_plugin_update_inputs()`, which sets `IN1`'s coin/1P-start/fire/left/right
bits directly on `s_machine.in1`. `invaders_machine_set_in1()` still exists
with matching `SI_IN1_*` bit masks but isn't actually called by that path
(see its header comment in `invaders_machine.h`) - it's kept for whatever
richer input method eventually replaces direct-register-write coin/start-only
control (no directional/action gameplay controls exist yet; see the project
[Roadmap](README.md#roadmap)). With no coin inserted and no start pressed,
the ROM runs its own real attract-mode/demo loop untouched, exactly as real
hardware does sitting idle - which is itself a nice side effect of emulating
the actual ROM instead of writing new game logic.

## Interrupt timing vs. our frame loop

Real hardware interrupts the CPU twice per 60Hz frame, synced to the CRT
beam: `RST 1` at mid-screen, `RST 2` at vblank. This project has no literal
CRT beam position to sync against and, as of the current architecture, does
**not** slice CPU emulation across scanlines at all: `main.c` calls
`plugin->run_frame()` exactly once per hardware-VSYNC-genlocked frame
(`dvi_display_wait_for_vsync()`), then `plugin->render_frame()` once to
produce the whole 320x240 frame in one linear pass. `space_invaders_plugin.c`'s
`run_frame()` (`si_plugin_run_frame()`) is:

```c
invaders_machine_run_cycles(&s_machine, SI_CYCLES_PER_HALF);
invaders_machine_interrupt_mid_screen(&s_machine);
invaders_machine_run_cycles(&s_machine, SI_CYCLES_PER_HALF);
invaders_machine_interrupt_vblank(&s_machine);
```

i.e. two clean half-frame CPU bursts (`SI_CYCLES_PER_HALF = SI_CYCLES_PER_FRAME
/ 2`, `SI_CYCLES_PER_FRAME = 1996800 / 60 ≈ 33280`) with `RST 1` fired between
them and `RST 2` after the second. The *total* cycles per frame and per
interrupt-half are correct, matching the real PCB; only the sub-frame
distribution (which the real hardware ties to CRT beam position) isn't
emulated at finer granularity than "first half" / "second half." This is a
deliberate, documented simplification (see Limitations below) - not
something the actual game logic is sensitive to.

**This replaced an earlier per-scanline-sliced design** (240 small CPU
slices interleaved with scanline output, one per call) that this section
used to describe. That was deliberately replaced by the current
once-per-frame two-half-burst approach specifically to *fix* the HDMI
sync-loss bug - see CLAUDE.md's "HSTX sync-loss caveat," pillar 3
("Decoupled Frame-Based Emulation"): running CPU emulation in one
uninterrupted ~2 ms burst per frame, instead of spreading it across the
whole scanout, leaves ~14.6 ms of each 16.67 ms frame completely free of
Core 0 bus traffic for Core 1's HSTX engine - the opposite tradeoff from
what this section previously described as necessary.

## Screen orientation, scaling, overlay & offset (`space_invaders_video.c`)

**This entire section was rewritten** to match `src/games/space_invaders/space_invaders_video.c`,
which is a differently-structured reimplementation than the `src/game.c`
version this document used to describe (no more `apply_mirror()`,
`sample_pixel()`, `sample_bit()`, `overlay_color_for_screen_x()`,
`SI_FB_X_OFFSET`/`SI_ROT_X_OFFSET`/`SI_ROT_CROP`). All settings below live in
`src/games/space_invaders/space_invaders_config.h` (moved from the old top-level
`src/display_config.h`).

The real cabinet's monitor is mounted **vertically** (portrait) - this is
the actual arcade hardware's native orientation (confirmed by, among other
things, MAME's driver for this game using the `ROT270` orientation flag,
which exists specifically to mark games whose cabinet monitor is physically
rotated from normal landscape). The game draws into video RAM in that
native vertical scan order. Video RAM is 7168 bytes = 224 columns x 32 bytes
each (256 bits per column): byte `col*32 + row/8`, bit `row%8`. This fixed
physical layout un-rotates to a native 256x224 "landscape" content image via
a fixed relationship (`lx = cx`, `ly = 223 - cy`) that never changes with any
setting below - `SI_DISPLAY_ROTATION`/`FLIP_H`/`FLIP_V` instead control how
that native content image maps onto the physical screen.

`space_invaders_config.h` exposes:

- **`SI_DISPLAY_ROTATION`**: `0`, `90`, `180`, or `270`.
- **`SI_DISPLAY_FLIP_H`** / **`SI_DISPLAY_FLIP_V`**: mirror the image
  horizontally/vertically, applied in final (post-rotation) display space -
  i.e. they flip what you actually see, the way a real display's mirror
  setting would, regardless of rotation.
- **`SI_ENABLE_COLOR_OVERLAY`**: red/green cellophane-strip tint, on by default.
- **`SI_SCALE_MODE`**: `SI_SCALE_NONE` / `SI_SCALE_FIT` (default) / `SI_SCALE_X` / `SI_SCALE_Y`.
- **`SI_SCREEN_OFFSET_X`** / **`SI_SCREEN_OFFSET_Y`**: nudge the displayed image within the 320x240 framebuffer (positive X right, positive Y down).

**All 16 rotation/flip combinations are implemented and verified.** An
earlier pass through this doc (while auditing for plugin-refactor drift)
found that the version of `space_invaders_video.c` inherited from the refactor only
implemented `SI_DISPLAY_ROTATION` 0/180 and `SI_DISPLAY_FLIP_H`, and that
`SI_DISPLAY_FLIP_V`/`SI_SCREEN_OFFSET_Y` were defined but silently unused -
i.e. exactly the kind of subtly-wrong rotation math this project's history
warns about (see the old, superseded four-formula implementation this
section used to describe). That's been fixed: `space_invaders_video_init()` and
`render_arcade_row()` now derive `(lx, ly)` from a single inverse mapping
covering all four rotations, with flips composed on top in final display
space. **Verification methodology** (matching this project's own established
practice of not trusting rotation math from inspection alone): every one of
the 224x256=57,344 possible VRAM bits was checked, for all 16
rotation/flip combinations (16 x 57,344 ≈ 917k checks total), against an
independently-derived standard image-rotation formula (not derived from
`space_invaders_video.c`'s own code) - zero mismatches. This has **not** been verified
on real hardware yet (only in simulation) - if you have a physically
rotated monitor to test against, treat this as the next thing to confirm.

**Color overlay**: the real machine's video hardware only ever outputs
1-bit black/white - the color you remember from the cabinet came from
cellophane/acetate strips glued over the glass. `lit_pixel_color(ox)`
(`space_invaders_video.c`) returns red for `ox < 32`, green for
`ox >= SI_CONTENT_W - 40`, white otherwise, gated by
`SI_ENABLE_COLOR_OVERLAY`; `ox` is the raw output-column position (before
any rotation/flip/mirror is applied), so the bands always sit at the
physical left/right screen edges regardless of `SI_DISPLAY_ROTATION` -
matching the original hardware-overlay design intent of being decoupled
from content rotation. Precomputed once per column into `s_col_lit[]`.

**Scaling & centering**: `SI_DISPLAY_W`/`SI_DISPLAY_H` are derived from
`SI_CONTENT_W`/`SI_CONTENT_H` (256x224, swapped to 224x256 for 90/270) and
`SI_SCALE_MODE` at compile time (`FIT` picks the binding axis via
cross-multiplication, no runtime division or float). `SI_ACTIVE_X_OFFSET`
centers horizontally; vertically, if `SI_DISPLAY_H` exceeds `FRAME_HEIGHT`
the excess is cropped evenly off both ends (`SI_ACTIVE_Y_CROP`), otherwise
it's centered with a black border (`SI_ACTIVE_Y_OFFSET`/`SI_ACTIVE_Y_LIMIT`)
- one set of formulas handles both cases generically. Within the displayed
region, output pixels are mapped back to content space via fixed-point
nearest-neighbor stepping (`step_x`/`step_y`, `<<16` fixed point).

**Implementation note on 90/270**: for `SI_DISPLAY_ROTATION` 0/180, which
output axis reads a VRAM *column* vs. a *bit-within-column* never changes
(only *which* column/bit, i.e. mirroring), so `space_invaders_video_init()` can
precompute byte/bit selection per output column `x` once
(`s_row_map[]`) and `render_arcade_row()` picks a single VRAM column for
the whole row. For 90/270 these axis roles **swap** - which VRAM column to
read now depends on `x` (still precomputable per column, into
`s_col_offset[]`), while the bit-within-column now depends on `y` (computed
once per row instead). Both branches are selected entirely at compile time
(`SI_ROTATED`), so there's no runtime cost for the rotation you didn't pick.

**Screen offset**: `SI_SCREEN_OFFSET_X` shifts `SI_ACTIVE_X_OFFSET` at init
time (`space_invaders_video_init()`); `SI_SCREEN_OFFSET_Y` shifts the equivalent
vertical origin per row (`render_arcade_row()`) - both compose with the
scaling/centering above rather than replacing it. Pixels pushed outside the
framebuffer simply clip to the black border.

## Limitations

- **Sound effects require user-supplied sample files.** Ports 3/5 are fully
  decoded and wired through to the plugin's voice mixer (see the "Sound
  effects" section above), but the actual PCM data behind each effect isn't
  vendored - it comes from `roms/space_invaders/*.pcm`, which you have to supply yourself
  (see `roms/space_invaders/README.md`). Without those files, the game runs with correct
  sound *timing* but no actual audio.
- **Only coin/start/fire/left/right are wired**, via BOOTSEL
  (`src/platform/input/host_input.c`) and SNES controller (`src/platform/input/snes_controller.c`).
- **90/270 rotation and both flip settings are simulation-verified but not
  yet confirmed on real hardware** - see "Screen orientation" above.
- **No watchdog.** Real cabinets reset if the ROM stops periodically
  strobing port 6 (a hung game resets itself); this emulator just keeps
  running a hung CPU state forever. Not expected to matter since we're
  running the unmodified real ROM, which strobes it correctly.
