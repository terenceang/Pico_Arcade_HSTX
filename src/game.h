#ifndef GAME_H
#define GAME_H

#include <stdint.h>

// Initialises game state. Call once at startup.
void game_init(void);

// Runs the emulated 8080 CPU for one full 60 Hz frame (33,280 cycles):
// executes 16,640 cycles -> fires RST 1 (mid-screen) -> executes 16,640 cycles -> fires RST 2 (vblank).
void game_run_frame(void);

// Renders the entire arcade VRAM into the 320x240 8bpp frame buffer.
void game_render_frame(uint8_t *dst);

// Renders framebuffer row `y` directly into dst (length FRAME_WIDTH).
void game_render_scanline(uint8_t *dst, unsigned y, unsigned frame_count);

#endif // GAME_H
