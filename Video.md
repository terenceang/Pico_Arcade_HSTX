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
dvi_display_get_write_buffer()                dvi_scanline_fill_cb()
game_render_scanline()                        (8bpp -> RGB565 palette lookup +
(8bpp indexed -> the write buffer)              horizontal doubling, per line,
        |                                       reading whichever fb_buffers[]
dvi_display_present_frame()                     entry Core 0 isn't writing)
(atomically flips which of                          |
 fb_buffers[2] Core 1 reads)                         v
        |                                     HSTX Hardware Encoder (TMDS + Data Islands)
audio_i2s_feed_queue()                              |
(48 kHz stereo PCM -> hstx_di_queue_push,           v
 called repeatedly through the frame,         HSTX PHY -> GPIO 12-19
 not once as a single burst)
        |
sleep_until(delayed_by_us(start, target_us))
(Microsecond wall-clock timekeeping anchor)
```

**Core 0** (`main.c`, `dvi_display.c`):
- Gets a write buffer via `dvi_display_get_write_buffer()` and renders 8bpp scanlines into it (`game_render_scanline()`).
- Calls `dvi_display_present_frame()` once the frame is complete - atomically flips which of `dvi_display.c`'s two 8bpp framebuffers (`fb_buffers[2]`) Core 1 reads, so Core 0 always writes the buffer Core 1 *isn't* currently reading. An earlier design instead pre-converted the whole frame to RGB565 on Core 0 and handed Core 1 a raw pointer into a single (non-double-buffered) array - a genuine, unsynchronized cross-core data race, since Core 0 could write the same memory Core 1's DMA was concurrently reading. See `CLAUDE.md`'s "HSTX sync-loss caveat" for the investigation that found this.
- Generates 48 kHz PCM audio samples and pushes HDMI Data Islands via `audio_i2s_feed_queue()`, called repeatedly through the frame (not as one burst) to keep pico_hdmi's Data Island queue continuously topped up.
- Paces the main loop using an absolute microsecond wall-clock anchor (`sleep_until(delayed_by_us(start, target_us))`), eliminating clock drift between CPU audio production and HDMI CTS/N clock generation.

**HSTX Engine** (`pico_hdmi`, Core 1):
- `dvi_scanline_fill_cb()` (a *scanline fill* callback, matching `pico_hdmi`'s own reference example - `examples/bouncing_box`) does the 8bpp->RGB565 palette lookup and horizontal-doubling itself, per line, directly into the ISR's own line buffer - reading from whichever `fb_buffers[]` entry Core 0 published via `dvi_display_present_frame()`. An earlier per-pixel-lookup-in-the-ISR design (a *different* problem than the data race above) had blown HDMI mode's per-line timing budget at native 640-pixel width; the current 320-pixel-wide lookup (doubled via packing, not re-fetching) is the design that replaced it - see display_config.h's FRAME_WIDTH/HEIGHT comment.
- Hardware TMDS encoding via RP2350 HSTX peripheral.
- Injects HDMI Data Island packets (Audio samples, InfoFrames, ACR) during horizontal sync/blanking periods.
- `dvi_display.c` also registers a background task (`hdmi_sync_watchdog_task()`, via `video_output_set_background_task()`) that runs on Core 1's own idle loop, monitoring for a known `pico_hdmi` failure mode (a corrupted HSTX command word desyncing the DMA engine permanently) and calling `video_output_force_resync()` to recover automatically - see `CLAUDE.md`'s "HSTX sync-loss caveat".

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
