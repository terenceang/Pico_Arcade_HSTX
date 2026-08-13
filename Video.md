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
game_render_scanline()                        dvi_scanline_ptr_cb()
(8bpp indexed -> fb[320x240])                       |
        |                                           v
dvi_display_convert_frame()                   Return pointer into rgb565_lines[]
(8bpp -> pre-packed RGB565,                   (no per-pixel work - pointer only)
 once per frame, ALL 240 lines)                     |
        |                                           v
audio_i2s_feed_queue()                        HSTX Hardware Encoder (TMDS + Data Islands)
(48 kHz stereo PCM -> hstx_di_queue_push,           |
 called repeatedly through the frame,               v
 not once as a single burst)                  HSTX PHY -> GPIO 12-19
        |
sleep_until(delayed_by_us(start, chunks * CHUNK_US))
(Microsecond wall-clock timekeeping anchor)
```

**Core 0** (`main.c`, `dvi_display.c`):
- Renders 8bpp scanlines into `fb` (`game_render_scanline()`).
- Converts the whole frame from 8bpp to pre-packed RGB565 (`dvi_display_convert_frame()`, in `dvi_display.c`) once per frame, *after* rendering - this is deliberate: Core 1's ISR used to do this palette lookup per pixel itself, which fit pure-DVI mode's per-line budget but not HDMI mode's (Data Island construction shares that same budget) and caused total loss of signal lock. Moving it to Core 0, which has ample slack, fixed it - see the file's own comment on `rgb565_lines`.
- Generates 48 kHz PCM audio samples and pushes HDMI Data Islands via `audio_i2s_feed_queue()`, called repeatedly through the frame (not as one burst) to keep pico_hdmi's Data Island queue continuously topped up.
- Paces the main loop using an absolute microsecond wall-clock anchor (`sleep_until(delayed_by_us(start, chunks_pushed * CHUNK_US))`), eliminating clock drift between CPU audio production and HDMI CTS/N clock generation.

**HSTX Engine** (`pico_hdmi`, Core 1):
- `dvi_scanline_ptr_cb()` (a *scanline pointer* callback, not a fill callback) just returns the address of the already-converted line - zero per-pixel work on this time-critical path.
- Hardware TMDS encoding via RP2350 HSTX peripheral.
- Injects HDMI Data Island packets (Audio samples, InfoFrames, ACR) during horizontal sync/blanking periods.

---

## Resolution scaling: 320x240 -> 640x480

Both axes are doubled in software during the Core 0 conversion pass (`dvi_display_convert_frame()` in `dvi_display.c`), not in HSTX hardware:

- **Horizontal 2x**: each 8bpp source pixel's looked-up RGB565 color is packed into both halves of one 32-bit word (`color | (color << 16)`), which is how HSTX's packed-pixel format already represents 2 physical pixels per word.
- **Vertical 2x**: Core 1's pointer callback maps physical line `active_line` to logical row `active_line >> 1`, so each of the 240 converted rows is reused for 2 of the 480 physical scanlines.

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
| `src/video/dvi_display.c` / `.h` | Clock/voltage setup, palette setup, Core-0 8bpp->RGB565 frame conversion, scanline pointer callback for `pico_hdmi` HSTX driver |
| `lib/pico_hdmi/` | Core HSTX DVI + HDMI Data Island audio driver library |
| `src/audio/audio_i2s.c` / `.h` | Software audio mixer & 48 kHz PCM batch generator |
