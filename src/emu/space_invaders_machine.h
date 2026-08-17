#ifndef INVADERS_MACHINE_H
#define INVADERS_MACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include "z80emu.h"
#include "z80user.h"

// Emulates the Space Invaders arcade PCB's memory map, I/O ports, and the
// shift-register sprite-drawing hardware wired to the CPU core.
typedef struct invaders_machine {
    z80_ctx_t ctx;          // MUST be first member so (void *)m == (z80_ctx_t *)m
    Z80_STATE cpu;
    uint8_t int_vector;
    bool int_pending;

    // $0000-$1FFF ROM, $2000-$3FFF RAM (work RAM + video RAM).
    uint8_t ram[0x2000];

    // 16-bit sprite/bullet shift register. OUT 4 shifts a new byte in from
    // the top (dropping the old low byte), OUT 2 sets a 0-7 bit read
    // offset, IN 3 reads the shifted-and-masked result.
    uint16_t shift_register;
    uint8_t shift_offset;

    // Latched input port bits (see the SI_IN_* bit masks below).
    uint8_t in0, in1, in2;

    // Optional hook for port 3/5 discrete sound-effect writes
    void (*sound_write)(void *ctx, uint8_t port, uint8_t value);
    void *sound_ctx;
} invaders_machine_t;

// Wires up the CPU's memory/IO callbacks, clears RAM, and resets the CPU to
// power-on state. Call once at startup.
void invaders_machine_init(invaders_machine_t *m);

// Runs the CPU for at least `min_cycles` T-states and returns the
// number of cycles actually consumed.
int invaders_machine_run_cycles(invaders_machine_t *m, int min_cycles);

// Delivers RST 1 ($CF) or RST 2 ($D7)
void invaders_machine_interrupt_mid_screen(invaders_machine_t *m);
void invaders_machine_interrupt_vblank(invaders_machine_t *m);

// Read-only access to video RAM ($2400-$3FFF)
const uint8_t *invaders_machine_vram(const invaders_machine_t *m);

// IN1 control bits, matching the real cabinet's port wiring.
#define SI_IN1_COIN     (1u << 0)
#define SI_IN1_P1_START (1u << 2)
#define SI_IN1_P1_FIRE  (1u << 4)
#define SI_IN1_P1_LEFT  (1u << 5)
#define SI_IN1_P1_RIGHT (1u << 6)

void invaders_machine_set_in1(invaders_machine_t *m, uint8_t bits, int pressed);

#endif // INVADERS_MACHINE_H
