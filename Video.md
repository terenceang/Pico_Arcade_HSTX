# Video & HDMI Audio Pipeline

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
game_run_frame()                              (Patches 36-word Data Island in < 1.2 µs)
(16,640 cyc -> RST 1 -> 16,640 cyc -> RST 2)           |
        |                                              v
game_render_frame()                           HSTX Hardware Encoder (TMDS + Data Islands)
(256x224 VRAM -> 320x240 write buffer)                 |
        |                                              v
audio_i2s_feed_queue(200)                     HSTX PHY -> GPIO 12-19
(48 kHz stereo PCM -> hstx_di_queue_push)
```

**Core 0** (`main.c`, `dvi_display.c`, `game.c`):
- Synchronizes frame start strictly to hardware VSYNC via `dvi_display_wait_for_vsync()`, guaranteeing a rock-solid 60.000 Hz frame rate.
- Calls `dvi_display_present_frame()` during vertical blanking, eliminating tearing and buffer data races.
- Runs Space Invaders 8080 CPU emulation in two clean half-frame blocks (`game_run_frame()`), firing `RST 1` mid-screen and `RST 2` vblank per the 1978 arcade PCB hardware specification. Finishes in ~2 ms, leaving ~14.6 ms of the frame free of shared bus traffic.
- Renders the full 256x224 arcade VRAM to the 320x240 8bpp write buffer in a single linear pass (`game_render_frame()`).
- Feeds 48 kHz PCM audio samples into the Data Island queue (`audio_i2s_feed_queue()`), keeping the queue continuously topped up at 200 packets per frame.

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
| `src/main.c` | Core 0 entry point; main loop with microsecond wall-clock timekeeping |
| `src/game.c` / `.h` | Paces emulated CPU against frame loop, converts video RAM to 8bpp scanlines |
| `src/video/testcard.c` / `.h` | Colorbar / grayscale test card pattern generator (8bpp palettized) |
| `src/video/display_config.h` | Framebuffer resolution, 8-bit palette indices, debug flags |
| `src/video/dvi_display.c` / `.h` | Clock/voltage setup, palette setup, double-buffered 8bpp framebuffer, Core-1 scanline fill callback (8bpp->RGB565) for `pico_hdmi` HSTX driver, HDMI sync-loss watchdog |
| `lib/pico_hdmi/` | Core HSTX DVI + HDMI Data Island audio driver library |
| `src/audio/audio_i2s.c` / `.h` | Software audio mixer & 48 kHz PCM batch generator |
