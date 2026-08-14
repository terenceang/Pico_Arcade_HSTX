# Space Invader PICO

**Version: 1.0.0**

A real emulator of the 1978 Taito/Midway Space Invaders arcade PCB (Intel 8080 CPU, memory map, I/O ports and shift-register sprite hardware) for the [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/), written in C against the Raspberry Pi Pico SDK, driving palettized DVI/HDMI video output and 48 kHz PCM audio over HDMI. This runs the *actual* arcade ROM (user-supplied - see [`roms/README.md`](roms/README.md)), not a from-scratch reimplementation of the game logic.

**Status:** Palettized 8bpp HDMI video, 48 kHz stereo PCM embedded HDMI audio (Data Islands), Intel 8080 CPU emulation core, arcade VRAM/port mapping, and sound-effect mixer are integrated and working. **There is a known, unresolved intermittent HDMI sync-loss bug under real gameplay** - see "Timing / HDMI stability note" below. See [`Emulator.md`](Emulator.md) and [`Video.md`](Video.md). **No input device is currently wired up** - a SNES controller driver was removed while investigating that bug (later confirmed not to have been the cause) and hasn't been replaced.

## What's here right now

- An Intel 8080 CPU interpreter and Space Invaders arcade machine emulation (`src/emu/`) - full instruction set, real port/shift-register hardware, running the unmodified original ROM. See [`Emulator.md`](Emulator.md).
- A high-performance palettized DVI/HDMI output engine (`lib/pico_hdmi`) - RP2350 hardware HSTX driven (see [`Video.md`](Video.md)).
- A 320x240 8bpp palettized framebuffer (75 KB SRAM), scaled 2x to the board's fixed 640x480p60 DVI timing. The emulated machine's 256x224 video RAM is un-rotated and letterboxed into it, with the classic red/white/green cabinet overlay tint reproduced at the video-conversion stage.
- Embedded 48 kHz stereo PCM HDMI Data Island audio transport, driven without external I2S hardware directly over HDMI.
- A debug test card (color bars, grayscale ramp, moving sync bar) for verifying the display pipeline independent of any game code.

## Timing / HDMI stability note (KNOWN ISSUE - unresolved)

This project has a hardware-level bug: after a while running the actual game, the HDMI signal can lose lock.
In the reproduced failure, Core 0 stays alive the whole time, but Core 1's scan clock jumps from 60 Hz to
somewhere around 2.3x-2.8x nominal (~137-166 Hz measured across different sessions) and stays there.

Investigation ruled out several suspects in turn - the audio Data Island queue (fixed separately and
confirmed smooth/glitch-free, but the sync-loss still reproduced after that fix), the SNES controller
subsystem (removed entirely, but the sync-loss still reproduced without it too), and a real, genuine
Core 0/Core 1 framebuffer data race found by comparing against `lib/pico_hdmi`'s own reference example
(fixed via double-buffering - see `CLAUDE.md` for detail - but the sync-loss *still* reproduced afterward on
a longer soak test, so it was only part of the picture).

Further isolation narrowed the trigger to specifically **real, varying ROM content** - not CPU emulation
running per se (a trivial NOP-loop workload with real interrupts and real per-pixel rendering stays
perfectly stable), but the *irregular*, data-dependent timing that comes from interpreting real machine code
(different opcodes cost different cycles and take different paths). That irregularity appears to raise the
odds of triggering pico_hdmi's own documented failure mode (a corrupted HSTX command word that desyncs the
DMA engine's FIFO pacing) rather than directly causing it - the measured rate jumps as a discrete step, not
a gradual increase with workload.

**No corrective/recovery mechanism (resync, reboot, etc.) is used here by design** - an earlier attempt at
an automatic recovery watchdog was removed, since detecting and reacting to the desync doesn't address why
it happens. The current approach is to attack likely sources of Core 0/Core 1 SRAM bus contention directly -
e.g. relocating pico_hdmi's active-line buffer into a dedicated scratch RAM bank away from Core 0's working
set (`PICO_HDMI_LINE_BUFFER_IN_SCRATCH_Y` in `CMakeLists.txt`) - not yet confirmed effective on hardware. See
`CLAUDE.md`'s "HSTX sync-loss caveat" for the full investigation history and current status.

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
| `src/video/` | Video & display pipeline (`dvi_display.c`/`.h`, `display_config.h`, `testcard.c`/`.h`, `debug_overlay.c`/`.h`) |
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
- [ ] Root-cause and fix intermittent HDMI sync-loss under real gameplay - a real Core 0/Core 1 data race was found and fixed, and the trigger has been narrowed to real/varying ROM content, but the underlying failure still reproduces; see `CLAUDE.md`'s "HSTX sync-loss caveat"
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
