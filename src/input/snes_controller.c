#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "snes_controller.pio.h"
#include "snes_controller.h"

#define SNES_PIO_INST pio2 // PIO2 is completely free (PIO0 is DVI, PIO1 is I2S audio)

static PIO s_pio = SNES_PIO_INST;
static uint s_sm = 0;
static bool s_initialized = false;
static uint16_t s_last_buttons = 0;

void snes_controller_init(void) {
    if (s_initialized)
        return;

    printf("[DEBUG] Initializing SNES controller PIO driver (Latch: GPIO%d, Clock: GPIO%d, Data: GPIO%d)...\n",
           SNES_PIN_LATCH, SNES_PIN_CLOCK, SNES_PIN_DATA);

    s_sm = pio_claim_unused_sm(s_pio, true);
    uint offset = pio_add_program(s_pio, &snes_controller_program);

    // 1 MHz PIO clock -> 1 us cycle time (12 us Latch pulse, 500 kHz Clock frequency)
    float clkdiv = (float)clock_get_hz(clk_sys) / 1000000.0f;

    snes_controller_program_init(s_pio, s_sm, offset,
                                 SNES_PIN_LATCH, SNES_PIN_CLOCK, SNES_PIN_DATA,
                                 clkdiv);

    pio_sm_set_enabled(s_pio, s_sm, true);
    s_initialized = true;

    printf("[DEBUG] SNES controller PIO driver initialized successfully.\n");
}

uint16_t snes_controller_read(void) {
    if (!s_initialized)
        return 0;

    // Drain FIFO to get the most recent reading
    while (!pio_sm_is_rx_fifo_empty(s_pio, s_sm)) {
        uint32_t raw_word = pio_sm_get(s_pio, s_sm);
        // SNES button bits are active LOW (0 when pressed), and land in the upper
        // 16 bits of the pushed word (shift_right ISR, only 16 of 32 bits shifted -
        // see snes_controller.pio). Shift down before inverting so 1 = pressed.
        s_last_buttons = (uint16_t)(~(raw_word >> 16) & 0xFFFF);
    }

    return s_last_buttons;
}
