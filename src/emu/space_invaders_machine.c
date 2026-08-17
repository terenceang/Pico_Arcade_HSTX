#include "space_invaders_machine.h"
#include <string.h>
#include "video/display_config.h"
#include "space_invaders_rom_data.h"
#include "platform/audio/audio_engine.h"

static uint8_t invaders_read_byte(void *ctx, uint16_t addr) {
    invaders_machine_t *m = (invaders_machine_t *)ctx;
    addr &= 0x3FFF;
    if (addr < 0x2000) {
#if DEBUG_CPU_NOP_ROM
        return 0x00;
#else
        return space_invaders_rom[addr];
#endif
    } else {
        return m->ram[addr - 0x2000];
    }
}

static void invaders_write_byte(void *ctx, uint16_t addr, uint8_t data) {
    invaders_machine_t *m = (invaders_machine_t *)ctx;
    addr &= 0x3FFF;
    if (addr >= 0x2000) {
        m->ram[addr - 0x2000] = data;
    }
}

static uint8_t invaders_read_io(void *ctx, uint8_t port) {
    invaders_machine_t *m = (invaders_machine_t *)ctx;
    switch (port) {
        case 0: return m->in0;
        case 1: return m->in1;
        case 2: return m->in2;
        case 3: return (uint8_t)((m->shift_register >> (8 - m->shift_offset)) & 0xFF);
        default: return 0xFF;
    }
}

static void invaders_write_io(void *ctx, uint8_t port, uint8_t data) {
    invaders_machine_t *m = (invaders_machine_t *)ctx;
    switch (port) {
        case 2:
            m->shift_offset = data & 0x07;
            break;
        case 3:
            if (m->sound_write) m->sound_write(m->sound_ctx, 3, data);
            break;
        case 4:
            m->shift_register = (uint16_t)((m->shift_register >> 8) | (data << 8));
            break;
        case 5:
            if (m->sound_write) m->sound_write(m->sound_ctx, 5, data);
            break;
        case 6:
            // Watchdog port
            break;
        default:
            break;
    }
}

void invaders_machine_init(invaders_machine_t *m) {
    memset(m, 0, sizeof(invaders_machine_t));
    m->ctx.read = invaders_read_byte;
    m->ctx.write = invaders_write_byte;
    m->ctx.in = invaders_read_io;
    m->ctx.out = invaders_write_io;

    m->shift_register = 0;
    m->shift_offset = 0;

    m->in0 = 0x0E;
    m->in1 = 0x08;
    m->in2 = 0x00;

    m->sound_write = NULL;
    m->sound_ctx = NULL;
    m->int_pending = false;
    m->int_vector = 0;

    Z80Reset(&m->cpu);
}

int invaders_machine_run_cycles(invaders_machine_t *m, int min_cycles) {
    if (m->int_pending) {
        Z80Interrupt(&m->cpu, m->int_vector, m);
        m->int_pending = false;
    }

    int remaining = min_cycles;
    int total_executed = 0;
    while (remaining > 0) {
        int chunk = remaining > 4096 ? 4096 : remaining;
        int executed = Z80Emulate(&m->cpu, chunk, m);
        audio_engine_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);
        int step = (executed > 0 ? executed : chunk);
        remaining -= step;
        total_executed += step;
    }
    return total_executed;
}

void invaders_machine_interrupt_mid_screen(invaders_machine_t *m) {
    m->int_vector = 0xCF; // RST 1
    m->int_pending = true;
}

void invaders_machine_interrupt_vblank(invaders_machine_t *m) {
    m->int_vector = 0xD7; // RST 2
    m->int_pending = true;
}

const uint8_t *invaders_machine_vram(const invaders_machine_t *m) {
    return &m->ram[0x0400];
}

void invaders_machine_set_in1(invaders_machine_t *m, uint8_t bits, int pressed) {
    if (pressed) {
        m->in1 |= bits;
    } else {
        m->in1 &= ~bits;
    }
}
