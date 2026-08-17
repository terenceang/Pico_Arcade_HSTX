#include "snes_controller.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "snes_controller.pio.h"
#include "core/plugin_api.h"

#define SNES_PIO_INST pio2 // PIO0-3 are all otherwise unused in this project

static PIO s_pio = SNES_PIO_INST;
static uint s_sm = 0;
static bool s_initialized = false;
static uint32_t s_last_mask = 0;
static uint16_t s_last_buttons = 0;

// Bit order matches the shift-register sample order documented in
// snes_controller.pio: B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R.
static const uint32_t s_button_to_input[12] = {
    INPUT_BUTTON_A, // B    -> primary fire
    INPUT_BUTTON_C, // Y
    INPUT_COIN,     // Select
    INPUT_START_1P, // Start
    INPUT_UP,       // Up
    INPUT_DOWN,     // Down
    INPUT_LEFT,     // Left
    INPUT_RIGHT,    // Right
    INPUT_BUTTON_B, // A
    INPUT_BUTTON_D, // X
    INPUT_START_2P, // L
    INPUT_SERVICE,  // R
};

static uint16_t decode_snes_buttons(uint32_t raw_word) {
    // Button bits are active LOW (0 when pressed) and land in the upper 16 bits of
    // the pushed word (shift_right ISR, only 16 of 32 bits shifted - see
    // snes_controller.pio). Shift down before inverting so 1 = pressed.
    return (uint16_t)(~(raw_word >> 16) & 0xFFFFu);
}

static uint32_t map_snes_buttons_to_input_mask(uint16_t buttons) {
    uint32_t mask = 0;
    for (int i = 0; i < 12; i++) {
        if (buttons & (1u << i)) {
            mask |= s_button_to_input[i];
        }
    }
    return mask;
}

void snes_controller_init(void) {
    if (s_initialized)
        return;

    s_sm = pio_claim_unused_sm(s_pio, true);
    uint offset = pio_add_program(s_pio, &snes_controller_program);

    // 1 MHz PIO clock -> 1 us cycle time (12 us Latch pulse, 500 kHz Clock frequency)
    float clkdiv = (float)clock_get_hz(clk_sys) / 1000000.0f;

    snes_controller_program_init(s_pio, s_sm, offset,
                                 SNES_PIN_LATCH, SNES_PIN_CLOCK, SNES_PIN_DATA,
                                 clkdiv);

    pio_sm_set_enabled(s_pio, s_sm, true);
    s_initialized = true;

    // Prime the pipeline: trigger the first read now so a result is ready by the
    // second call to snes_controller_poll() (one frame later).
    pio_sm_put(s_pio, s_sm, 0);
}

uint32_t snes_controller_poll(void) {
    if (!s_initialized)
        return 0;

    // Non-blocking collect: was the read triggered on the previous call finished?
    if (!pio_sm_is_rx_fifo_empty(s_pio, s_sm)) {
        uint32_t raw_word = pio_sm_get(s_pio, s_sm);
        s_last_buttons = decode_snes_buttons(raw_word);
        s_last_mask = map_snes_buttons_to_input_mask(s_last_buttons);
    }
    // Otherwise keep the cached mask - correct both while a read is still in
    // flight and when no controller is plugged in.

    // Non-blocking issue: trigger next frame's read, but never queue more than
    // one outstanding trigger.
    if (pio_sm_is_tx_fifo_empty(s_pio, s_sm)) {
        pio_sm_put(s_pio, s_sm, 0);
    }

    return s_last_mask;
}

uint16_t snes_controller_get_raw_buttons(void) {
    return s_last_buttons;
}
