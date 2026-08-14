#ifndef INVADERS_MACHINE_H
#define INVADERS_MACHINE_H

#include <stdint.h>

#include "i8080.h"

// Emulates the Space Invaders arcade PCB's memory map, I/O ports, and the
// shift-register sprite-drawing hardware wired to the CPU core - everything
// the CPU itself doesn't know about. Port map and shift-register behaviour
// match the real cabinet (see computerarcheology.com's Space Invaders
// hardware writeup, the canonical reference for this board).
typedef struct {
    i8080_t cpu;

    // $0000-$1FFF ROM (space_invaders_rom, embedded at build time from
    // roms/ - see CMakeLists.txt / roms/README.md), $2000-$3FFF RAM (work
    // RAM + video RAM). Addresses >= $4000 mirror back into this same 8KB,
    // matching the real PCB's incomplete address decoding - the ROM never
    // actually touches that range, so it's a formality, not a feature.
    uint8_t ram[0x2000];

    // 16-bit sprite/bullet shift register. OUT 4 shifts a new byte in from
    // the top (dropping the old low byte), OUT 2 sets a 0-7 bit read
    // offset, IN 3 reads the shifted-and-masked result. This is the real
    // hardware's trick for drawing arbitrarily bit-shifted sprites without
    // the CPU doing the shifting itself.
    uint16_t shift_register;
    uint8_t shift_offset;

    // Latched input port bits (see the SI_IN_* bit masks below). Bits not
    // driven by an actual control default to this cabinet's real idle
    // state (DIP switches, "not connected" pull-ups).
    uint8_t in0, in1, in2;

    // Optional hook for port 3/5 discrete sound-effect writes (real
    // hardware's UFO/Shot/Flash/fleet-movement thumps etc. - see
    // src/audio/sound_effects.c, which knows the actual bit mapping this
    // file deliberately doesn't). NULL by default, so writes are simply
    // dropped and this machine still runs standalone with no audio
    // subsystem attached - same optional-callback pattern i8080_t itself
    // uses for its mem/io hooks.
    void (*sound_write)(void *ctx, uint8_t port, uint8_t value);
    void *sound_ctx;
} invaders_machine_t;

// Wires up the CPU's memory/IO callbacks, clears RAM, and resets the CPU to
// power-on state. Call once at startup.
void invaders_machine_init(invaders_machine_t *m);

// Runs the CPU for at least `min_cycles` T-states (the last instruction is
// always allowed to finish, so this can slightly overrun) and returns the
// number of cycles actually consumed.
int invaders_machine_run_cycles(invaders_machine_t *m, int min_cycles);

// Delivers RST 1 ($CF) or RST 2 ($D7) - the two interrupts real Space
// Invaders hardware raises once each per 60Hz video frame, synced to the
// CRT beam reaching mid-screen and vblank respectively. The game's own
// interrupt handlers use these to know when it's safe to update which half
// of video RAM.
void invaders_machine_interrupt_mid_screen(invaders_machine_t *m);
void invaders_machine_interrupt_vblank(invaders_machine_t *m);

// Read-only access to video RAM ($2400-$3FFF: 256x224 pixels, 1 bit per
// pixel, rotated 90 degrees - see game.c for the un-rotation into the
// display framebuffer).
const uint8_t *invaders_machine_vram(const invaders_machine_t *m);

// IN1 control bits, matching the real cabinet's port wiring. Coin/2P
// controls aren't defined here - this board only drives a single-player
// cocktail-less cabinet setup.
#define SI_IN1_COIN     (1u << 0)
#define SI_IN1_P1_START (1u << 2)
#define SI_IN1_P1_FIRE  (1u << 4)
#define SI_IN1_P1_LEFT  (1u << 5)
#define SI_IN1_P1_RIGHT (1u << 6)

// Sets or clears the given IN1 control bit(s) (SI_IN1_* above). No input
// device is currently wired up to call this (see CLAUDE.md's "HSTX
// sync-loss caveat" for why the SNES controller driver that used to call it
// from game.c was removed) - kept for whatever input method replaces it.
void invaders_machine_set_in1(invaders_machine_t *m, uint8_t bits, int pressed);

#endif // INVADERS_MACHINE_H
