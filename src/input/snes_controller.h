#ifndef SNES_CONTROLLER_H
#define SNES_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

// Default GPIO Pin configuration for SNES controller on Raspberry Pi Pico 2
// Note: GPIO 12-19 are strictly reserved for HSTX DVI output!
#ifndef SNES_PIN_LATCH
#define SNES_PIN_LATCH 2 // Physical Header Pin 4
#endif

#ifndef SNES_PIN_CLOCK
#define SNES_PIN_CLOCK 3 // Physical Header Pin 5
#endif

#ifndef SNES_PIN_DATA
#define SNES_PIN_DATA 4 // Physical Header Pin 6
#endif

// SNES Controller Button Bitmasks (Active HIGH when pressed)
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

// Initializes the PIO SNES controller driver state machine.
void snes_controller_init(void);

// Polls the latest 16-bit SNES controller button state.
// Returns bitmask of pressed buttons (active HIGH, SNES_BTN_* constants above).
uint16_t snes_controller_read(void);

#endif // SNES_CONTROLLER_H
