#include "host_input.h"
#include "snes_controller.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

// Hardware-safe QSPI CS pin inspection in RAM to detect BOOTSEL button
static bool __no_inline_not_in_flash_func(bootsel_raw_read)(void) {
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();

    // Hi-Z flash CS pin
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 100; ++i);

#if defined(SIO_GPIO_HI_IN_QSPI_CSN_BITS)
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#elif defined(SIO_GPIO_HI_IN_QSPI_CS_BITS)
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CS_BITS
#else
    #define CS_BIT (1u << 1)
#endif
    // Button pulls pin LOW when pressed
    bool pressed = !(sio_hw->gpio_hi_in & CS_BIT);

    // Restore normal CS pin override
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);
    return pressed;
}

bool host_input_bootsel_pressed(void) {
    return bootsel_raw_read();
}

// Coin pulse generator state
static bool s_last_bootsel = false;
static uint32_t s_coin_pulse_frames = 0;

void host_input_init(void) {
    s_last_bootsel = false;
    s_coin_pulse_frames = 0;
    snes_controller_init();
}

uint32_t host_input_poll(void) {
    uint32_t mask = 0;
    bool bootsel = bootsel_raw_read();

    // Detect rising edge on BOOTSEL button press
    if (bootsel && !s_last_bootsel) {
        // Trigger a 6-frame (~100ms @ 60Hz) arcade coin switch pulse
        s_coin_pulse_frames = 6;
    }
    s_last_bootsel = bootsel;

    if (s_coin_pulse_frames > 0) {
        mask |= INPUT_COIN;
        s_coin_pulse_frames--;
    }

    mask |= snes_controller_poll();

    return mask;
}
