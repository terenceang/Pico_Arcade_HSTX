#include "pacman_machine.h"
#include <string.h>
#include "platform/audio/audio_engine.h"
#include "games/pacman/pacman_config.h"

static uint8_t pacman_read_byte(void *ctx, uint16_t addr) {
    pacman_machine_t *m = (pacman_machine_t *)ctx;
    addr &= 0x7FFF;
    if (addr < 0x5000) {
        return m->ram[addr];
    } else if (addr <= 0x50FF) {
        if (addr <= 0x503F) return m->in0;
        else if (addr <= 0x507F) return m->in1;
        else if (addr <= 0x50BF) return m->dsw1;
    }
    return 0xFF;
}

static void pacman_write_byte(void *ctx, uint16_t addr, uint8_t data) {
    pacman_machine_t *m = (pacman_machine_t *)ctx;
    addr &= 0x7FFF;
    if (addr >= 0x4000 && addr < 0x5000) {
        m->ram[addr] = data;
    } else if (addr >= 0x5000 && addr <= 0x50FF) {
        if (addr == 0x5000) {
            m->int_enable = (data & 1);
            if (!m->int_enable) m->int_pending = false;
        } else if (addr == 0x5001) {
#if PACMAN_ENABLE_ATTRACT_SOUND
            namco_sound_set_enable(&m->sound, true);
#else
            namco_sound_set_enable(&m->sound, (data & 1) != 0);
#endif
        } else if (addr == 0x5003) {
            m->flip_screen = (data & 1);
        } else if (addr >= 0x5040 && addr <= 0x505F) {
            namco_sound_write(&m->sound, addr - 0x5040, data);
        } else if (addr >= 0x5060 && addr <= 0x506F) {
            m->sprite_coords[addr - 0x5060] = data;
        }
    }
}

static uint8_t pacman_read_io(void *ctx, uint8_t port) {
    (void)ctx; (void)port;
    return 0xFF;
}

static void pacman_write_io(void *ctx, uint8_t port, uint8_t data) {
    pacman_machine_t *m = (pacman_machine_t *)ctx;
    if (port == 0x00) {
        m->int_vector = data;
    }
}

void pacman_machine_init(pacman_machine_t *m, const uint8_t *rom, const uint8_t *sound_prom) {
    memset(m, 0, sizeof(pacman_machine_t));
    m->ctx.read = pacman_read_byte;
    m->ctx.write = pacman_write_byte;
    m->ctx.in = pacman_read_io;
    m->ctx.out = pacman_write_io;
    if (rom) {
        memcpy(m->ram, rom, 0x4000);
    }
    namco_sound_init(&m->sound, sound_prom);
    Z80Reset(&m->cpu);
    pacman_machine_reset(m);
}

void pacman_machine_reset(pacman_machine_t *m) {
    Z80Reset(&m->cpu);
    namco_sound_reset(&m->sound);
    memset(&m->ram[0x4000], 0, 0x1000);
    memset(m->sprite_coords, 0, sizeof(m->sprite_coords));
    m->int_enable = false;
    m->int_pending = false;
    m->flip_screen = false;
    m->int_vector = 0xFA;
    m->in0 = 0xFF;
    m->in1 = 0xFF; // Bit 4 high = Normal Game (Test mode OFF), Bit 7 high = Upright
    m->dsw1 = 0xC9;
}

void pacman_machine_vblank_interrupt(pacman_machine_t *m) {
    if (m->int_enable) {
        m->int_pending = true;
    }
}

void pacman_machine_run_frame(pacman_machine_t *m, int cycles) {
    if (m->int_enable && m->int_pending) {
        Z80Interrupt(&m->cpu, m->int_vector, m);
        m->int_pending = false;
    }

    int remaining = cycles;
    while (remaining > 0) {
        int chunk = remaining > 4096 ? 4096 : remaining;
        int executed = Z80Emulate(&m->cpu, chunk, m);
        audio_engine_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);
        remaining -= (executed > 0 ? executed : chunk);
    }
}

const uint8_t *pacman_machine_get_vram(const pacman_machine_t *m) {
    return &m->ram[0x4000];
}

const uint8_t *pacman_machine_get_color_ram(const pacman_machine_t *m) {
    return &m->ram[0x4400];
}

const uint8_t *pacman_machine_get_sprite_coords(const pacman_machine_t *m) {
    return m->sprite_coords;
}

const uint8_t *pacman_machine_get_sprite_attrs(const pacman_machine_t *m) {
    return &m->ram[0x4FF0];
}
