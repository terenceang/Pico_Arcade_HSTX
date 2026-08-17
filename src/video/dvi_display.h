#ifndef DVI_DISPLAY_H
#define DVI_DISPLAY_H

#include <stdint.h>

// Raises core voltage and sets the system clock for the DVI bit clock.
// Must be called before stdio_init_all(), since it must run before the
// clock is stable for USB.
void dvi_display_clock_init(void);

// Configures the board's DVI pinout/timing and brings up the HSTX HDMI driver.
void dvi_display_init(void);

// Returns the 8bpp framebuffer Core 0 should render this frame's content
// into (FRAME_WIDTH * FRAME_HEIGHT bytes) - one of an internal double
// buffer, so it's always safe to write without racing Core 1's scanline
// callback, which reads the *other* buffer until dvi_display_present_frame()
// flips them. Call once per frame, before rendering scanlines; the returned
// pointer is only valid for that frame (a new call after
// dvi_display_present_frame() may return a different buffer).
uint8_t *dvi_display_get_write_buffer(void);

// Marks the buffer from dvi_display_get_write_buffer() as complete and
// atomically exposes it to Core 1 for scanout, reclaiming the buffer Core 1
// was previously reading. Call once per frame from Core 0, after rendering
// all of the frame's scanlines.
void dvi_display_present_frame(void);

// Sets palette RGB888 color for 8bpp index.
void dvi_display_set_palette(uint8_t index, uint32_t rgb888);

// Returns the RGB888 color for the given 8bpp palette index.
uint32_t dvi_display_get_palette_entry(uint8_t index);

// Core 1 entry point: idle worker for HSTX background tasks.
void core1_main(void);

// Blocks Core 0 until the next hardware VSYNC (video_frame_count changes).
// Returns the new video_frame_count value.
uint32_t dvi_display_wait_for_vsync(uint32_t last_vfc);

// Raw pico_hdmi video_frame_count (incremented once per real vsync serviced
// by Core 1's DMA ISR).
uint32_t dvi_display_get_video_frame_count(void);

#endif // DVI_DISPLAY_H
