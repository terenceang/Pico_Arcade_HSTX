#ifndef SCREEN_CAPTURE_H
#define SCREEN_CAPTURE_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#if ENABLE_SCREEN_CAPTURE

// Initializes screen capture subsystem (USB serial polling / triggers)
void screen_capture_init(void);

// Polls for screen capture trigger (e.g. via USB serial command or button combo)
// and transmits BMP over USB serial if triggered.
void screen_capture_poll(const uint8_t *fb, uint32_t input_mask);

// Force dumps the given 320x240 framebuffer as a BMP over USB serial
void screen_capture_dump_bmp(const uint8_t *fb);

#else

// Zero-overhead inline stubs when ENABLE_SCREEN_CAPTURE is 0
static inline void screen_capture_init(void) {}
static inline void screen_capture_poll(const uint8_t *fb, uint32_t input_mask) {
    (void)fb;
    (void)input_mask;
}
static inline void screen_capture_dump_bmp(const uint8_t *fb) {
    (void)fb;
}

#endif // ENABLE_SCREEN_CAPTURE

#endif // SCREEN_CAPTURE_H
