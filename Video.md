# Video & HDMI Audio Pipeline

This document explains how this project gets pixels and audio onto the screen:
the software pipeline, the buffering/timing budget, and how that maps onto the Raspberry Pi Pico 2.

Video output and embedded HDMI audio are driven by `lib/pico_hdmi` - a high-performance
engine utilizing the RP2350's hardware **HSTX** (High-Speed Transmit) peripheral on GPIO 12-19, driving 640x480p60 output from a 320x240 palettized canvas with 32 kHz stereo PCM HDMI embedded audio Data Islands.

---

## Hardware HSTX Architecture

The RP2350 includes a dedicated hardware peripheral (HSTX) designed for high-speed serial TMDS encoding and transmission over GPIO 12-19. Unlike bit-banged PIO solutions on older microcontrollers, HSTX handles TMDS serialization in hardware with near-zero CPU overhead.

---

## Pipeline overview

```
Core 0 (producer & emulator, main.c)          Core 1 / DMA (pico_hdmi / HSTX engine)
------------------------------------          -------------------------------------
game_render_scanline()                        dvi_scanline_cb()
(8bpp indexed -> fb[320x240])                       |
        |                                           v
        +-----> Writes 8bpp fb -------->  Lookup RGB565 & populate line buffer
        |                                           |
audio_i2s_step_frame()                              v
(32 kHz stereo PCM -> hstx_di_queue_push)     HSTX Hardware Encoder (TMDS + Data Islands)
        |                                           |
sleep_until(delayed_by_us(start, chunks * CHUNK_US)) v
(Microsecond wall-clock timekeeping anchor)       HSTX PHY -> GPIO 12-19
```

**Core 0** (`main.c`):
- Renders 8bpp scanlines directly into `fb` (`game_render_scanline()`).
- Generates 32 kHz PCM audio samples in frame batches (`audio_i2s_step_frame()`) and pushes HDMI Data Islands.
- Paces the main loop using an absolute microsecond wall-clock anchor (`sleep_until(delayed_by_us(start, chunks_pushed * CHUNK_US))`), eliminating clock drift between CPU audio production and HDMI CTS/N clock generation.

**HSTX Engine** (`pico_hdmi`):
- Converts 8bpp palette indices to RGB565 values via scanline callback (`dvi_scanline_cb`).
- Hardware TMDS encoding via RP2350 HSTX peripheral.
- Injects HDMI Data Island packets (Audio samples, InfoFrames, ACR) during horizontal sync/blanking periods.

---

## Resolution scaling: 320x240 -> 640x480

- **Horizontal 2x**: Done during TMDS encode (`n_pix = 160` source pixels per line, hardware pixel-doubling to 320 symbols = 640 wire pixels).
- **Vertical 2x**: Handled via DMA IRQ line repeating (`v_ctr % 2` repeat check).

---

## Board-specific details (Raspberry Pi Pico 2)

- **HSTX DVI pins are GPIO 12-19**: RP2350 hardware HSTX peripheral handles TMDS serialization in hardware.
- **126 MHz system clock / 1.20V core voltage**: Set in `dvi_display_clock_init()`.

---

## File map

| File | Role |
|---|---|
| `src/main.c` | Core 0 entry point; main loop with microsecond wall-clock timekeeping |
| `src/game.c` / `.h` | Paces emulated CPU against frame loop, converts video RAM to 8bpp scanlines |
| `src/video/testcard.c` / `.h` | Colorbar / grayscale test card pattern generator (8bpp palettized) |
| `src/video/display_config.h` | Framebuffer resolution, 8-bit palette indices, debug flags |
| `src/video/dvi_display.c` / `.h` | Clock/voltage setup, palette setup, scanline callback for `pico_hdmi` HSTX driver |
| `lib/pico_hdmi/` | Core HSTX DVI + HDMI Data Island audio driver library |
| `src/audio/audio_i2s.c` / `.h` | Software audio mixer & 32 kHz PCM batch generator |
