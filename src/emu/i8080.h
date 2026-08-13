#ifndef I8080_H
#define I8080_H

#include <stdint.h>

// Minimal, from-scratch Intel 8080 CPU interpreter core. Implements the
// full documented (and the handful of well-known undocumented duplicate)
// opcodes - every one of the 256 opcode values decodes to defined 8080
// behaviour: 0x08/0x10/0x18/0x20/0x28/0x30/0x38 are NOP duplicates, 0xCB is
// a JMP duplicate, 0xD9 is a RET duplicate, 0xDD/0xED/0xFD are CALL
// duplicates - all real hardware quirks, not omissions.
//
// This file has no knowledge of any particular machine's memory map or I/O
// devices - the host wires those up via the read/write/in/out function
// pointers below. See invaders_machine.c for the Space Invaders wiring.
typedef struct i8080 {
    uint8_t a, b, c, d, e, h, l;
    uint16_t sp, pc;

    // Flags stored individually rather than packed into a status byte -
    // simpler to get right per-instruction. PUSH PSW / POP PSW pack and
    // unpack these into the real 8080 flag byte layout (bit1 always set,
    // bits 3 and 5 always clear).
    uint8_t flag_s, flag_z, flag_ac, flag_p, flag_cy;

    uint8_t interrupts_enabled; // IFF: set by EI, cleared by DI and on interrupt accept
    uint8_t halted;             // set by HLT; cleared when an interrupt is accepted

    void *ctx;
    uint8_t (*mem_read)(void *ctx, uint16_t addr);
    void (*mem_write)(void *ctx, uint16_t addr, uint8_t value);
    uint8_t (*io_in)(void *ctx, uint8_t port);
    void (*io_out)(void *ctx, uint8_t port, uint8_t value);
} i8080_t;

// Resets registers/flags/PC to power-on state (PC=0, interrupts disabled).
// Does not touch the mem_read/mem_write/io_in/io_out/ctx wiring - set those
// up first.
void i8080_reset(i8080_t *cpu);

// Executes exactly one instruction at cpu->pc and returns the number of
// clock cycles (T-states) it took, per the standard 8080 timing table.
int i8080_step(i8080_t *cpu);

// Requests a hardware interrupt carrying the given single-byte RST
// instruction (e.g. 0xCF for RST 1, 0xD7 for RST 2). If interrupts are
// currently enabled, this behaves exactly like the CPU fetching that RST
// instead of its next real instruction: pushes PC, jumps to the RST
// vector, and clears the interrupt-enable flag (matching real 8080
// behaviour - a handler that wants to be re-interrupted must EI again
// itself before returning). If interrupts are disabled, the request is
// silently dropped, same as real hardware asserting INT while the CPU has
// interrupts masked. Returns the cycle cost (11) if accepted, 0 if dropped.
int i8080_interrupt(i8080_t *cpu, uint8_t rst_opcode);

#endif // I8080_H
