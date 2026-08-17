#ifndef PACMAN_VIDEO_H
#define PACMAN_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include "emu/pacman_machine.h"

void pacman_video_init(const uint8_t *palette_prom, const uint8_t *color_prom,
                       const uint8_t *tile_rom, const uint8_t *sprite_rom);

// Uploads Pac-Man 32-color RGB palette to host display
void pacman_video_load_palette(void);

// Renders Pac-Man frame into 320x240 8bpp framebuffer
void pacman_video_render_frame(const pacman_machine_t *m, uint8_t *fb);

#endif // PACMAN_VIDEO_H
