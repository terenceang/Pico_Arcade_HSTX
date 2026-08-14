# Space Invader PICO

**Version: 1.0.0**

A real emulator of the 1978 Taito/Midway Space Invaders arcade PCB (Intel 8080 CPU, memory map, I/O ports and shift-register sprite hardware) for the [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/), written in C against the Raspberry Pi Pico SDK, driving palettized DVI/HDMI video output and 48 kHz PCM audio over HDMI. This runs the *actual* arcade ROM (user-supplied - see [`roms/README.md`](roms/README.md)), not a from-scratch reimplementation of the game logic.

**Status: Feature Complete.** Palettized 8bpp HDMI video, 48 kHz stereo PCM embedded HDMI audio (Data Islands), Intel 8080 CPU emulation core, arcade VRAM/port mapping, and sound-effect mixer are integrated and verified working, including a since-fixed HDMI sync-loss bug (see "Timing / HDMI stability note" below). See [`Emulator.md`](Emulator.md) and [`Video.md`](Video.md). **No input device is currently wired up** - a SNES controller driver was removed while investigating that bug (later confirmed not to have been the cause) and hasn't been replaced.

## What's here right now

- An Intel 8080 CPU interpreter and Space Invaders arcade machine emulation (`src/emu/`) - full instruction set, real port/shift-register hardware, running the unmodified original ROM. See [`Emulator.md`](Emulator.md).
- A high-performance palettized DVI/HDMI output engine (`lib/pico_hdmi`) - RP2350 hardware HSTX driven (see [`Video.md`](Video.md)).
- A 320x240 8bpp palettized framebuffer (75 KB SRAM), scaled 2x to the board's fixed 640x480p60 DVI timing. The emulated machine's 256x224 video RAM is un-rotated and letterboxed into it, with the classic red/white/green cabinet overlay tint reproduced at the video-conversion stage.
- Embedded 48 kHz stereo PCM HDMI Data Island audio transport, driven without external I2S hardware directly over HDMI.
- A debug test card (color bars, grayscale ramp, moving sync bar) for verifying the display pipeline independent of any game code.

## Timing / HDMI stability note (fixed - kept for history)

This project used to have a hardware-level bug worth preserving the history of: after a while running the
actual game, the HDMI signal would lose lock permanently. In the reproduced failure, Core 0 stayed alive the
whole time, but Core 1's scan clock jumped from 60 Hz to a fixed ~137.8 Hz (~2.3x) and stayed there.

Investigation ruled out several suspects in turn - the audio Data Island queue (fixed separately: it's now
kept continuously topped up across the whole frame with a bounded refill cap, and audio is smooth and
glitch-free, but the sync-loss still reproduced after that fix), CPU emulation, and the SNES controller
subsystem (removed entirely - see below - but the sync-loss still reproduced without it too). Reading
`lib/pico_hdmi`'s own reference example (`examples/bouncing_box`) at that point turned up the real cause:
its Core 1 ISR computes pixels synchronously into an ISR-owned buffer with zero cross-core memory dependency,
while this project instead had Core 0 pre-convert the whole frame into a single, non-double-buffered array
and hand Core 1's DMA a raw pointer into it - an unsynchronized, ongoing per-frame data race between the two
independent, unsynchronized loops. That's what "Core 1's scan clock snapping to ~137.8Hz and never
recovering" actually was: pico_hdmi's own `video_output_force_resync()` doc comment describes exactly this
failure mode (a corrupted HSTX command word desyncing the DMA engine, which then races ahead unthrottled).

**Fixed** by double-buffering the smaller 8bpp source framebuffer instead of the full RGB565 output (full
RGB565 double-buffering would have needed ~600KB, more than the RP2350's 520KB SRAM) and moving the palette
lookup back into Core 1's ISR per line, matching the reference example's synchronous pattern - see
`dvi_display.c`. Confirmed via soak test on real hardware. An automatic recovery watchdog
(`hdmi_sync_watchdog_task()`, calling pico_hdmi's `video_output_force_resync()`) is kept in place as a cheap
safety net even though the root cause is understood and fixed. See `CLAUDE.md`'s "HSTX sync-loss caveat" for
the full investigation history.

## Hardware

- Raspberry Pi Pico 2 board
- HDMI expansion/DVI sock module and an HDMI cable to a DVI/HDMI-capable display with audio speakers or HDMI audio extractor
- USB cable for flashing/power

## Building

This project targets the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (developed against SDK 2.3.0) and the `pico2` board definition.

**You need the real arcade ROM.** This project doesn't include Taito's copyrighted ROM - drop your own legally-obtained dump into `roms/` before building (see [`roms/README.md`](roms/README.md) for the exact files/names needed). Without it, the firmware still builds (against a zero-filled placeholder) but won't run the actual game.

**Easiest path**: open the folder in VS Code with the [Raspberry Pi Pico extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico) installed - the `.vscode/` config in this repo is already set up for it. Build, then flash via the extension or by copying the generated `.uf2` to the Pico while it's in BOOTSEL mode.

**Manual CLI build**, once you have the Pico SDK, ARM toolchain, CMake, and Ninja on your `PATH`:

```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2
cmake --build build
```

This produces `build/Space_Invader_PICO.uf2`.

## Debug test card

By default, the app boots straight into the game (`DEBUG_TESTCARD 0` in `src/display_config.h`). To enable the debug test card:

```c
#define DEBUG_TESTCARD 1          // 1 to enable test card at boot
#define DEBUG_TESTCARD_SECONDS 5  // Seconds to display before handing off to game (0 for permanent)
```

## Project layout

| Path | What it is |
|---|---|
| `src/main.c` | Entry point; main loop with microsecond wall-clock timekeeping |
| `src/game.c` / `.h` | Paces the emulated CPU against the frame loop and converts its video RAM into 8bpp scanlines - see [`Emulator.md`](Emulator.md) |
| `src/emu/` | The Intel 8080 CPU core + Space Invaders arcade machine emulation (memory map, ports, shift register) - see [`Emulator.md`](Emulator.md) |
| `roms/` | Where you put the real arcade ROM (gitignored, not vendored - see `roms/README.md`) |
| `src/video/` | Video & display pipeline (`dvi_display.c`/`.h`, `display_config.h`, `testcard.c`/`.h`) |
| `lib/pico_hdmi/` | RP2350 hardware HSTX DVI + HDMI Data Island audio driver library |
| `src/audio/audio_i2s.c` / `.h` | Software audio mixer & 48 kHz PCM frame batch generator |
| `Hardware.md` | Board pinout and hardware specs |
| `Video.md` | How the HSTX HDMI pipeline works, wall-clock timekeeping, RGB565 conversion |
| `Emulator.md` | How the 8080 CPU core + arcade machine emulation works, video RAM rotation, known limitations |

## Roadmap

- [x] Hardware HSTX HDMI bring-up on Raspberry Pi Pico 2 (GPIO 12-19)
- [x] High-performance 8bpp palettized DVI engine & 48 kHz stereo PCM embedded HDMI audio (`lib/pico_hdmi`)
- [x] Intel 8080 CPU core + Space Invaders arcade machine emulation, running the real ROM
- [x] Video RAM → framebuffer conversion (8bpp indexed, letterboxing, color overlay)
- [x] Software audio mixer & sound-effect trigger decoder (`src/audio/`)
- [x] ~~SNES controller input wired to `invaders_machine_set_in1()`~~ - removed while investigating the HDMI sync-loss bug below; later confirmed not to have been the cause
- [x] Root-caused and fixed intermittent HDMI sync-loss (an unsynchronized Core 0/Core 1 framebuffer data race - see `CLAUDE.md`'s "HSTX sync-loss caveat"), plus an automatic recovery watchdog kept as a safety net
- [ ] Add a replacement input method (no controller is currently wired up at all)

## Acknowledgements & Attributions

This project builds upon open-source implementations and hardware documentation:

- **[pico_hdmi](https://github.com/fliperama86/pico_hdmi)** — High-performance RP2350 hardware HSTX DVI driver and HDMI Data Island audio packet transport engine (`lib/pico_hdmi`).
- **[PicoDVI-audio](https://github.com/shuichitakano/PicoDVI-audio)** by Shuichi Takano — HDMI Data Island packetization and TERC4 encoding algorithms for Pico hardware.
- **[PicoDVI](https://github.com/Wren6991/PicoDVI)** by Luke Wren ([`Wren6991`](https://github.com/Wren6991)) — DVI TMDS encoding pipeline for RP2040 and RP2350 microcontrollers.
- **[Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)** by Raspberry Pi Ltd — Official C/C++ SDK for RP2040 and RP2350 microcontrollers.
- **[Computer Archeology](http://www.computerarcheology.com/Arcade/SpaceInvaders/)** by Paul Robson & team — Detailed hardware documentation, memory maps, and I/O port specifications for the 1978 Taito/Midway Space Invaders arcade PCB.

## License

[MIT](LICENSE) for this project's own code (the 8080 CPU core, arcade machine emulation in `src/emu/`, and video/audio drivers). **Not covered**: the Space Invaders arcade ROM itself is Taito/Midway's copyrighted work, is not included in this repository, and is not covered by this project's MIT license - see `roms/README.md`.
