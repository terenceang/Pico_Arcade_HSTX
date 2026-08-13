#include "invaders_machine.h"

#include <string.h>

#include "rom_data.h"

static inline uint8_t mem_read(void *ctx, uint16_t addr) {
    invaders_machine_t *m = (invaders_machine_t *)ctx;
    if (__builtin_expect(addr < 0x2000, 1))
        return space_invaders_rom[addr];
    return m->ram[(addr - 0x2000) & 0x1FFF];
}

static inline void mem_write(void *ctx, uint16_t addr, uint8_t value) {
    invaders_machine_t *m = (invaders_machine_t *)ctx;
    if (__builtin_expect(addr < 0x2000, 0))
        return; // ROM: writes silently ignored, matching real hardware
    m->ram[(addr - 0x2000) & 0x1FFF] = value;
}

static uint8_t io_in(void *ctx, uint8_t port) {
    invaders_machine_t *m = (invaders_machine_t *)ctx;
    switch (port) {
        case 0: return m->in0;
        case 1: return m->in1;
        case 2: return m->in2;
        case 3: return (uint8_t)(m->shift_register >> (8 - m->shift_offset));
        default: return 0xFF;
    }
}

static void io_out(void *ctx, uint8_t port, uint8_t value) {
    invaders_machine_t *m = (invaders_machine_t *)ctx;
    switch (port) {
        case 2: m->shift_offset = value & 0x07; break;
        case 4: m->shift_register = (uint16_t)((value << 8) | (m->shift_register >> 8)); break;
        case 3:
        case 5:
            // Discrete sound-effect trigger bits - forwarded verbatim to
            // whoever wired up sound_write (see invaders_machine.h), which
            // is the only place that knows what each bit means.
            if (m->sound_write)
                m->sound_write(m->sound_ctx, port, value);
            break;
        // Port 6: watchdog reset strobe - not emulated yet; writes are
        // accepted and dropped, matching how real hardware behaves when
        // nothing is listening on a port.
        default: break;
    }
}

void invaders_machine_init(invaders_machine_t *m) {
    memset(m->ram, 0, sizeof(m->ram));
    m->shift_register = 0;
    m->shift_offset = 0;

    // IN0: mostly "not connected" (idles high) on this board revision.
    m->in0 = 0x0E;
    // IN1: bit3 is tied high on real hardware; coin/start/fire/left/right
    // all idle at 0 (not pressed/inserted) until real controls are wired.
    m->in1 = 0x08;
    // IN2 DIP switches: 3 ships (bits0-1 = 00), extra ship at 1500 points
    // (bit3 = 0), tilt idle (bit2 = 0), coin info shown in demo (bit7 = 0).
    m->in2 = 0x00;

    m->sound_write = NULL;
    m->sound_ctx = NULL;

    m->cpu.ctx = m;
    m->cpu.mem_read = mem_read;
    m->cpu.mem_write = mem_write;
    m->cpu.io_in = io_in;
    m->cpu.io_out = io_out;
    i8080_reset(&m->cpu);
}

int invaders_machine_run_cycles(invaders_machine_t *m, int min_cycles) {
    int done = 0;
    while (done < min_cycles)
        done += i8080_step(&m->cpu);
    return done;
}

void invaders_machine_interrupt_mid_screen(invaders_machine_t *m) {
    i8080_interrupt(&m->cpu, 0xCF); // RST 1
}

void invaders_machine_interrupt_vblank(invaders_machine_t *m) {
    i8080_interrupt(&m->cpu, 0xD7); // RST 2
}

const uint8_t *invaders_machine_vram(const invaders_machine_t *m) {
    return &m->ram[0x0400]; // $2400 - $2000
}

void invaders_machine_set_in1(invaders_machine_t *m, uint8_t bits, int pressed) {
    if (pressed)
        m->in1 |= bits;
    else
        m->in1 &= (uint8_t)~bits;
}
