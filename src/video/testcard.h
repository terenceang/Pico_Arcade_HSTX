#ifndef TESTCARD_H
#define TESTCARD_H

#include <stdint.h>

void testcard_init(void);
void testcard_load_palette(void);
void testcard_render_scanline(uint8_t *dst, unsigned y, unsigned frame_count);
void testcard_render_frame(uint8_t *dst, uint32_t frame_count);

#endif // TESTCARD_H
