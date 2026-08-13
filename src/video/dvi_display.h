#ifndef DVI_DISPLAY_H
#define DVI_DISPLAY_H

// Raises core voltage and sets the system clock for the DVI bit clock.
// Must be called before stdio_init_all(), since it must run before the
// clock is stable for USB/UART.
void dvi_display_clock_init(void);

// Configures the board's DVI pinout/timing and brings up the HSTX HDMI driver.
void dvi_display_init(void);

// Sets the framebuffer pointer for scanline rendering.
void dvi_display_set_buffer(uint8_t *buffer);

// Converts the current 8bpp framebuffer (set via dvi_display_set_buffer) to
// pre-packed RGB565 scanlines Core 1's ISR can return directly with no
// per-pixel work. Call once per frame from Core 0, after rendering the
// frame's content into the framebuffer and before the next scanline sweep
// needs it.
void dvi_display_convert_frame(void);

// Sets palette RGB888 color for 8bpp index.
void dvi_display_set_palette(uint8_t index, uint32_t rgb888);

// Core 1 entry point: idle worker for HSTX background tasks.
void core1_main(void);

#endif // DVI_DISPLAY_H
