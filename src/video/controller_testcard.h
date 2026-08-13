#ifndef CONTROLLER_TESTCARD_H
#define CONTROLLER_TESTCARD_H

#include <stdint.h>

void controller_testcard_init(void);
void controller_testcard_render_scanline(uint8_t *dst, unsigned y, unsigned frame_count);

#endif // CONTROLLER_TESTCARD_H
