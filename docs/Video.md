# Video & HDMI Audio Pipeline

> [!IMPORTANT]
> **CRITICAL ARCHITECTURAL RULE: VIDEO PIPELINE IS UNTOUCHABLE**
> The video and audio transport subsystems (`src/video/`, `lib/pico_hdmi/`, `dvi_display.*`) represent verified, fixed host platform infrastructure. 
> - **Games are modular plugins** implementing `emulator_plugin_t` (`src/core/plugin_api.h`).
> - **Never modify the video pipeline** when adding, modifying, or tuning game plugins.
> - Plugins interact with video purely by rendering into the 320x240 8bpp frame buffer provided each frame, and loading their 256-color palette.

This document explains how this project gets pixels and audio onto the screen:
the software pipeline, the buffering/timing budget, and how that maps onto the Raspberry Pi Pico 2.

Video output and embedded HDMI audio are driven by `lib/pico_hdmi` - a high-performance
engine utilizing the RP2350's hardware **HSTX** (High-Speed Transmit) peripheral on GPIO 12-19, driving 640x480p60 output from a 320x240 palettized canvas with 48 kHz stereo PCM HDMI embedded audio Data Islands.

---

## Hardware HSTX Architecture

The RP2350 includes a dedicated hardware peripheral (HSTX) designed for high-speed serial TMDS encoding and transmission over GPIO 12-19. Unlike bit-banged PIO solutions on older microcontrollers, HSTX handles TMDS serialization in hardware with near-zero CPU overhead.

---

## Pipeline overview

```
Core 0 (producer & emulator, main.c)          Core 1 / DMA ISR (pico_hdmi / HSTX engine)
------------------------------------          -------------------------------------------
dvi_display_wait_for_vsync()                  dvi_scanline_fill_cb()
(locks strictly to hardware 60 Hz VSYNC)      (8bpp -> RGB565 palette lookup from Scratch X,
        |                                      reading fb_buffers[s_front_idx])
dvi_display_present_frame()                            |
(atomically flips double-buffer during V-blank)        v
        |                                     Precomposed Active-Line Headers
plugin->run_frame()                           (Patches 36-word Data Island in < 1.2 µs)
(active plugin's own CPU-emulation pacing)             |
        |                                              v
plugin->render_frame()                        HSTX Hardware Encoder (TMDS + Data Islands)
(active plugin's VRAM -> 320x240 write buffer)         |
        |                                              v
audio_engine_feed_queue(200)                  HSTX PHY -> GPIO 12-19
(48 kHz stereo PCM -> hstx_di_queue_push)
```

**Core 0** (`main.c`, `dvi_display.c`, `src/core/plugin_registry.c`, `src/games/<game>/`):
- Synchronizes frame start strictly to hardware VSYNC via `dvi_display_wait_for_vsync()`, guaranteeing a rock-solid 60.000 Hz frame rate.
- Calls `dvi_display_present_frame()` during vertical blanking, eliminating tearing and buffer data races.
- Runs the active plugin's CPU emulation once per frame (`plugin->run_frame()`). Space Invaders' plugin runs its Z80-core-hosted 8080 emulation in two clean half-frame blocks internally, firing `RST 1` mid-screen and `RST 2` vblank per the 1978 arcade PCB hardware specification, finishing in ~2 ms and leaving ~14.6 ms of the frame free of shared bus traffic.
- Renders the active plugin's full arcade VRAM to the 320x240 8bpp write buffer (`plugin->render_frame()`; Space Invaders' `space_invaders_video.c` does this in a single linear pass).
- Feeds 48 kHz PCM audio samples into the Data Island queue (`audio_engine_feed_queue()`, `src/platform/audio/audio_engine.c`), keeping the queue continuously topped up at 200 packets per frame.

**HSTX Engine** (`pico_hdmi`, Core 1):
- `PICO_HDMI_PRECOMPOSED_ACTIVE_LINES`: Static active-line headers are built once at boot. The scanline ISR only patches the 36-word audio packet into the slot (< 1.2 µs), providing massive timing margin against the 6.35 µs H-blank deadline.
- `dvi_scanline_fill_cb()`: Converts 8bpp to packed RGB565 using `palette_fast` in Scratch X RAM with zero shared bus contention.
- Hardware TMDS encoding via RP2350 HSTX peripheral over GPIO 12-19.
- Injects 48 kHz HDMI Data Island packets (Audio samples, InfoFrames, ACR N=6144 CTS=25200) during blanking intervals.

---

## Resolution scaling: 320x240 -> 640x480

Both axes are doubled in software inside Core 1's per-line fill callback (`dvi_scanline_fill_cb()` in `dvi_display.c`), not in HSTX hardware:

- **Horizontal 2x**: each 8bpp source pixel's looked-up RGB565 color is packed into both halves of one 32-bit word (`color | (color << 16)`), which is how HSTX's packed-pixel format already represents 2 physical pixels per word.
- **Vertical 2x**: the fill callback maps physical line `active_line` to logical row `active_line >> 1`, so each of the 240 source rows is reused for 2 of the 480 physical scanlines.

---

## Board-specific details (Raspberry Pi Pico 2)

- **HSTX DVI pins are GPIO 12-19**: RP2350 hardware HSTX peripheral handles TMDS serialization in hardware.
- **126 MHz system clock / 1.10V core voltage** (RP2350 power-on default): Set in `dvi_display_clock_init()`.

---

## File map

| File | Role |
|---|---|
| `src/main.c` | Core 0 entry point; main loop hardware-VSYNC-genlocked to `dvi_display_wait_for_vsync()` |
| `src/core/plugin_registry.c` / `.h` | Dynamic plugin registry and active game selection |
| `src/games/<game_name>/` | Per-game plugin: paces emulated CPU against the frame loop, converts video RAM to 8bpp frames |
| `src/video/testcard.c` / `.h` | Colorbar / grayscale test card pattern generator (8bpp palettized) |
| `src/video/display_config.h` | Framebuffer resolution, 8-bit palette indices, debug flags |
| `src/video/dvi_display.c` / `.h` | Clock/voltage setup, palette setup, double-buffered 8bpp framebuffer, Core-1 scanline fill callback (8bpp->RGB565) for `pico_hdmi` HSTX driver |
| `lib/pico_hdmi/` | Core HSTX DVI + HDMI Data Island audio driver library |
| `src/platform/audio/audio_engine.c` / `.h` | Data Island audio queue feeder (48 kHz PCM, mute control) |
