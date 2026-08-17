# Pico Arcade HSTX

**Version: 1.4.0**

A modular arcade machine emulation platform for the [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) (RP2350), written in C against the Raspberry Pi Pico SDK, driving palettized DVI/HDMI video output (640x480p60) and 48 kHz stereo PCM audio over hardware HSTX.

> [!IMPORTANT]
> **NO COPYRIGHTED ROMS OR AUDIO SAMPLES ARE INCLUDED**
> - **No Game ROMs Included**: This repository contains emulator engine and hardware interface code only. **No proprietary game ROMs, character PROMs, palette PROMs, or audio sample dumps are included, distributed, or hosted in this repository.**
> - **User-Supplied Assets**: Users must provide their own legally obtained ROM dumps (`roms/space_invaders/`, `roms/pacman/`) and audio files (`sounds/` / `roms/`). See [`roms/README.md`](roms/README.md) and [`sounds/README.md`](sounds/README.md). If missing at build time, the build system generates harmless zero-filled placeholders.
> - **Modular Architecture**: Games are isolated plugins under `src/games/`. The host video and audio subsystem (`src/video/`, `lib/pico_hdmi/`) is fixed platform infrastructure.

**Status:** Rock-solid 60.000 Hz hardware-genlocked 8bpp HDMI video, 48 kHz stereo PCM embedded HDMI audio (Data Islands), modular emulator plugin architecture, CPU emulation cores (8080 & Z80), real SNES controller input, and verified support for multiple arcade titles. See [`docs/Emulator.md`](docs/Emulator.md) and [`docs/Video.md`](docs/Video.md).

## What's here right now

- **Modular Arcade Plugin System** (`src/core/`, `src/games/`): Clean plugin API allowing plug-and-play arcade machines without modifying host video/audio drivers.
- **Space Invaders Plugin** (`src/games/space_invaders/`): Authentic 1978 Intel 8080 PCB emulation, 1bpp VRAM decoder, color overlays, port/shift-register hardware, analog sound decoder.
- **Pac-Man Plugin** (`src/games/pacman/`): Authentic 1980 Z80 PCB emulation, tile/sprite VRAM decoder, 32-color palette decoding, Namco 3-voice custom wavetable sound.
- **High-Performance HSTX HDMI Output Engine** (`lib/pico_hdmi`): RP2350 hardware HSTX driven with precomposed active-line headers (`PICO_HDMI_PRECOMPOSED_ACTIVE_LINES`) (see [`docs/Video.md`](docs/Video.md)).
- **Double-Buffered 320x240 8bpp Framebuffer** (75 KB SRAM), scanline-doubled to 640x480p60 DVI timing.
- **Embedded 48 kHz stereo PCM HDMI Data Island audio transport**, driven directly over HDMI without external DACs or I2S wiring.
- **Hardware VSYNC genlocked main loop** (`dvi_display_wait_for_vsync`) ensuring zero frame tearing and rock-solid 60 FPS timing.
- **SNES controller input** (`src/platform/input/snes_controller.{c,h,pio}`): gated, non-blocking PIO driver on GPIO 26/27/28 driving D-pad/fire/start/coin for both games; BOOTSEL also wired as a coin/select input.
- **Debug test cards** for verifying the display pipeline and controller wiring independent of any game code.

## Timing & HDMI Stability Architecture

HDMI video and audio stability is achieved through four architectural pillars:
1. **Precomposed Active Lines:** Static HDMI active headers are prebuilt once at boot in a compose ring buffer. The scanline ISR only patches the 36-word audio packet into the slot in `< 1.2 µs`, easily fitting within the 6.35 µs H-blank interval.
2. **Hardware VSYNC Genlock:** Core 0 synchronizes frame execution and buffer swaps strictly to Core 1's hardware VSYNC (`video_frame_count`) at 60.000 Hz.
3. **Decoupled Frame Emulation:** CPU emulation runs cleanly once per frame without interfering with scanout deadlines.
4. **48 kHz Audio Delivery:** Core 0 feeds 200 packets (800 samples) per frame into the Data Island queue, perfectly matching the 60 Hz hardware consumption rate.

## Hardware

- **Raspberry Pi Pico 2** (RP2350A)
- **DVI Sock / HDMI Expansion**: Configured by default for the **Adafruit DVI Sock** pinout.
  > [!NOTE]
  > **DVI Sock Wiring**: Different DVI sock designs (Adafruit, DIY resistor DAC boards, Pimoroni DV Base) route TMDS pairs to different GPIO pins. If using a non-Adafruit board, adjust the `user_pinout` configuration in [`src/video/dvi_display.c`](src/video/dvi_display.c#L129-L136) as detailed in [`docs/Hardware.md`](docs/Hardware.md).
- **HDMI cable & display**: Standard DVI/HDMI monitor or TV supporting 640x480p60 video and 48 kHz stereo audio.
- **SNES Controller** (Optional): Connected to GPIO 26 (Clock), 27 (Latch), 28 (Data).
- **USB cable**: For power and flashing.

## Building & ROM Setup

This project targets the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (developed against SDK 2.3.0) and the `pico2` board definition.

> [!NOTE]
> **Arcade ROMs are NOT included in this repository.**
> Drop your legally obtained arcade ROM dumps into `roms/space_invaders/` and `roms/pacman/` before building (see [`roms/README.md`](roms/README.md) for exact file names and checksums). Without them, the project compiles cleanly against placeholder data, but will display a missing ROM notice on screen rather than running the game.

**Easiest path**: open the folder in VS Code with the [Raspberry Pi Pico extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico) installed - the `.vscode/` config in this repo is already set up for it. Build, then flash via the extension or by copying the generated `.uf2` to the Pico while it's in BOOTSEL mode.

**Manual CLI build**, once you have the Pico SDK, ARM toolchain, CMake, and Ninja on your `PATH`:

```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2
cmake --build build
```

This produces `build/Pico_Arcade_HSTX.uf2`.

## Debug test cards

By default, the app boots straight into the game (both flags below are `0` in
`src/video/display_config.h`). Two independent debug screens are available:

```c
#define DEBUG_TESTCARD 1             // 1 to enable the color-bar/grayscale test card at boot
#define DEBUG_TESTCARD_SECONDS 5     // Seconds to display before handing off to game (0 for permanent)

#define DEBUG_CONTROLLER_TESTCARD 1  // 1 to show a live SNES pad diagnostic diagram instead of the game
```

`DEBUG_CONTROLLER_TESTCARD` draws a D-pad + colored face-button diagram (Y=green, X=blue,
A=red, B=yellow, matching the real Super Famicom pad) that lights up as buttons are pressed,
with a short keypress beep - useful for checking controller wiring/mapping without a serial
console. It's shown permanently once `DEBUG_TESTCARD`'s own window (if enabled) ends.

## Project layout

| Path | What it is |
|---|---|
| `src/main.c` | Entry point; Core 0 hardware VSYNC genlocked frame loop & audio dispatch |
| `src/core/plugin_api.h` | Modular game plugin interface (`emulator_plugin_t`) |
| `src/core/plugin_registry.c` / `.h` | Dynamic plugin registry and active game selection |
| `src/games/space_invaders/` | Space Invaders plugin (rendering, DIP switches, overlay) |
| `src/games/pacman/` | Pac-Man plugin (Z80 pacing, 8x8/16x16 video decode, DIP switches) |
| `src/emu/` | CPU emulation cores (8080, Z80) and hardware machine mapping |
| `roms/` | Where you put authentic arcade ROMs (gitignored, not vendored - see `roms/README.md`) |
| `src/video/` | **UNTOUCHABLE** host video pipeline (`dvi_display.*`, `display_config.h`, `testcard.*`) |
| `lib/pico_hdmi/` | **UNTOUCHABLE** RP2350 hardware HSTX DVI + HDMI Data Island audio driver library |
| `src/platform/audio/` | Host audio engine (48 kHz Data Island queue feeder, mute control) |
| `docs/Hardware.md` | Board pinout and hardware specs |
| `docs/HDMI_API.md` | HDMI host video & audio subsystem API specification |
| `docs/Video.md` | Full HSTX DVI pipeline writeup, timing budget, why you can't block Core 0/1 |
| `docs/Emulator.md` | Arcade machine emulation writeup, video RAM rotation, known limitations |
| `docs/DisplayStabilityAnalysis.md` | Hardware timing, genlock, and HSTX stability analysis |

## Roadmap

- [x] Hardware HSTX HDMI bring-up on Raspberry Pi Pico 2 (GPIO 12-19)
- [x] High-performance 8bpp palettized DVI engine & 48 kHz stereo PCM embedded HDMI audio (`lib/pico_hdmi`)
- [x] Space Invaders arcade machine emulation, running the real Intel 8080 ROM on the cycle-accurate Z80 core (`src/emu/z80emu.c`)
- [x] Video RAM → framebuffer conversion (8bpp indexed, letterboxing, color overlay)
- [x] Software audio mixer & sound-effect trigger decoder (`src/audio/`)
- [x] ~~SNES controller input wired to `invaders_machine_set_in1()`~~ - removed while investigating the HDMI sync-loss bug below (later confirmed not to have been the cause), then re-added with a gated, non-blocking PIO design (`src/platform/input/snes_controller.{c,h,pio}`) - soak-tested on hardware with the pad actively in use and confirmed not to reintroduce it, see `CLAUDE.md`'s "HSTX sync-loss caveat" (narrowing #10)
- [ ] Root-cause and fix intermittent HDMI sync-loss under real gameplay - a real Core 0/Core 1 data race was found and fixed, the trigger was narrowed to real/varying ROM content, and a further mitigation (scratch-Y RAM relocation) was applied; believed fixed as of v1.2.0 (no reproduction across extended hardware testing since, including the SNES controller soak test above), but not formally re-confirmed and the underlying root-cause mechanism is still unproven - see `CLAUDE.md`'s "HSTX sync-loss caveat"
- [x] ~~Add a replacement input method~~ - BOOTSEL is wired up as a coin/select input (`src/platform/input/host_input.c`)
- [x] ~~Wire up real gameplay controls (move/fire)~~ - SNES controller D-pad/buttons now drive movement, fire, and start (`src/platform/input/snes_controller.c`)
- [x] Controller test card (`DEBUG_CONTROLLER_TESTCARD`) - live SNES pad diagram with real Super Famicom button colors and a keypress beep, for verifying wiring/mapping (`src/video/controller_testcard.c`)

## Acknowledgements & Attributions

This project builds upon open-source implementations and hardware documentation:

- **[z80emu](https://github.com/anotherlin/z80emu)** by Lin Ke-Fong — Fast, cycle-accurate, instruction-stepped Z80 CPU emulator core (`src/emu/z80emu.*`), passing ZEXALL and ZEXDOC compliance suites.
- **[chips](https://github.com/floooh/chips)** by Andre Weissflog ([`floooh`](https://github.com/floooh)) — Header-only 8-bit chip emulation library.
- **[pico_hdmi](https://github.com/fliperama86/pico_hdmi)** — High-performance RP2350 hardware HSTX DVI driver and HDMI Data Island audio packet transport engine (`lib/pico_hdmi`).
- **[PicoDVI-audio](https://github.com/shuichitakano/PicoDVI-audio)** by Shuichi Takano — HDMI Data Island packetization and TERC4 encoding algorithms for Pico hardware.
- **[PicoDVI](https://github.com/Wren6991/PicoDVI)** by Luke Wren ([`Wren6991`](https://github.com/Wren6991)) — DVI TMDS encoding pipeline for RP2040 and RP2350 microcontrollers.
- **[Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)** by Raspberry Pi Ltd — Official C/C++ SDK for RP2040 and RP2350 microcontrollers.
- **[Computer Archeology](http://www.computerarcheology.com/Arcade/SpaceInvaders/)** by Paul Robson & team — Detailed hardware documentation, memory maps, and I/O port specifications for the 1978 Taito/Midway Space Invaders arcade PCB.
- **[MAME](https://github.com/mamedev/mame)** & **Namco** — Namco 3-voice custom wavetable sound synthesis documentation and Pac-Man hardware reference.

### AI-Assisted Development Notice

Parts of this codebase—including modular architecture design, video rasterizer fast-path conversions, bus contention mitigations, debugging, and real-time performance optimizations—were generated, debugged, and optimized with the assistance of AI coding agents (Google DeepMind Antigravity / Claude Code).

## License

[MIT](LICENSE) for this project's own code (the CPU cores and arcade machine emulation in `src/emu/`, and video/audio drivers). **Not covered**: Arcade ROM and PROM files are their respective owners' copyrighted works, are not included in this repository, and are not covered by this project's MIT license - see `roms/README.md`.
