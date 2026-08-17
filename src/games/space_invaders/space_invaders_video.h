#ifndef SPACE_INVADERS_VIDEO_H
#define SPACE_INVADERS_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

void space_invaders_video_init(void);
void space_invaders_video_render_frame(uint8_t *dst, const uint8_t *vram, uint32_t stride);
void space_invaders_video_load_palette(void);

#endif // SPACE_INVADERS_VIDEO_H

