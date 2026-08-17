#ifndef SNES_CONTROLLER_H
#define SNES_CONTROLLER_H

#include <stdint.h>

// Default GPIO Pin configuration for SNES controller on Raspberry Pi Pico 2
// Note: GPIO 12-19 are strictly reserved for HSTX DVI output!
#ifndef SNES_PIN_CLOCK
#define SNES_PIN_CLOCK 26 // Physical Header Pin 31
#endif

#ifndef SNES_PIN_LATCH
#define SNES_PIN_LATCH 27 // Physical Header Pin 32
#endif

#ifndef SNES_PIN_DATA
#define SNES_PIN_DATA 28 // Physical Header Pin 34
#endif

// SNES Controller Button Bitmasks (raw, active HIGH when pressed) - matches the
// physical shift-register sample order documented in snes_controller.pio.
#define SNES_BTN_B      (1u << 0)
#define SNES_BTN_Y      (1u << 1)
#define SNES_BTN_SELECT (1u << 2)
#define SNES_BTN_START  (1u << 3)
#define SNES_BTN_UP     (1u << 4)
#define SNES_BTN_DOWN   (1u << 5)
#define SNES_BTN_LEFT   (1u << 6)
#define SNES_BTN_RIGHT  (1u << 7)
#define SNES_BTN_A      (1u << 8)
#define SNES_BTN_X      (1u << 9)
#define SNES_BTN_L      (1u << 10)
#define SNES_BTN_R      (1u << 11)

// Initializes the gated PIO SNES controller driver and primes its request pipeline.
// The state machine is idle (zero GPIO activity) until explicitly triggered by
// snes_controller_poll() - see snes_controller.pio.
void snes_controller_init(void);

// Non-blocking, pipelined poll: collects the result of the read triggered on the
// previous call (if ready) and triggers the next one. Never spins or blocks - call
// once per frame. Returns the latest decoded emulator_input_mask_t bitmask (see
// core/plugin_api.h); an unplugged controller decodes to 0 (no buttons pressed).
uint32_t snes_controller_poll(void);

// Returns the raw SNES_BTN_* bitmask (per-button, not remapped to INPUT_*) as of
// the last snes_controller_poll() call - for diagnostics (see controller_testcard.c).
uint16_t snes_controller_get_raw_buttons(void);

#endif // SNES_CONTROLLER_H
