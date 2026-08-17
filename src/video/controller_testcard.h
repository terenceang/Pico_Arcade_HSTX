#ifndef CONTROLLER_TESTCARD_H
#define CONTROLLER_TESTCARD_H

#include <stdint.h>

// Draws a live SNES pad diagram into the 320x240 8bpp framebuffer, one box per
// button, lit green while held - so wiring and button mapping can be checked
// visually. Reads the raw per-button state via snes_controller_get_raw_buttons(),
// which is kept fresh every frame by host_input_poll() regardless of this card
// being shown. Uses the same COLOR_BLACK/WHITE/GREEN palette entries as
// testcard.c - call testcard_load_palette() before showing this card.
void controller_testcard_render_frame(uint8_t *dst, uint32_t frame_count);

#endif // CONTROLLER_TESTCARD_H
