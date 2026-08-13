#ifndef GAME_H
#define GAME_H

#include <stdint.h>

// Initialises game state. Call once at startup.
void game_init(void);

// Renders framebuffer row `y` directly into dst (length FRAME_WIDTH).
void game_render_scanline(uint8_t *dst, unsigned y, unsigned frame_count);

#endif // GAME_H
