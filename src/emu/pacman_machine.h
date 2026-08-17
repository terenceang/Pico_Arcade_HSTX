#ifndef PACMAN_MACHINE_H
#define PACMAN_MACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include "z80emu.h"
#include "z80user.h"
#include "audio/namco_sound.h"

typedef struct pacman_machine {
    z80_ctx_t ctx;          // MUST be first member so (void *)m == (z80_ctx_t *)m
    Z80_STATE cpu;
    uint8_t ram[0x5000];    // 0x0000-0x3FFF: ROM, 0x4000-0x43FF: VRAM, 0x4400-0x47FF: ColorRAM, 0x4800-0x4FFF: WorkRAM
    uint8_t sprite_coords[16]; // 0x5060 - 0x506F: Sprite X/Y coordinates
    namco_sound_t sound;

    bool int_enable;
    bool int_pending;
    bool flip_screen;
    uint8_t int_vector;     // Latched interrupt vector (set by OUT 0, default 0xFA)
    uint8_t in0;            // Input Port 0 (0x5000 read)
    uint8_t in1;            // Input Port 1 (0x5040 read)
    uint8_t dsw1;           // DIP Switch 1 (0x5080 read)
} pacman_machine_t;

void pacman_machine_init(pacman_machine_t *m, const uint8_t *rom, const uint8_t *sound_prom);
void pacman_machine_reset(pacman_machine_t *m);
void pacman_machine_run_frame(pacman_machine_t *m, int cycles);
void pacman_machine_vblank_interrupt(pacman_machine_t *m);

// Video VRAM / Color RAM / Sprite RAM accessors
const uint8_t *pacman_machine_get_vram(const pacman_machine_t *m);
const uint8_t *pacman_machine_get_color_ram(const pacman_machine_t *m);
const uint8_t *pacman_machine_get_sprite_coords(const pacman_machine_t *m);
const uint8_t *pacman_machine_get_sprite_attrs(const pacman_machine_t *m);

#endif // PACMAN_MACHINE_H
