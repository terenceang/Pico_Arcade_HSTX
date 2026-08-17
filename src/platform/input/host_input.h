#ifndef HOST_INPUT_H
#define HOST_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "core/plugin_api.h"

// Initialize host platform input hardware (BOOTSEL button, GPIOs, etc.)
void host_input_init(void);

// Poll physical inputs and return a standardized emulator_input_mask_t bitmask
uint32_t host_input_poll(void);

// Direct status of the onboard BOOTSEL button
bool host_input_bootsel_pressed(void);

#endif // HOST_INPUT_H
