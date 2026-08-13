#include "i8080.h"

// ============================================================================
// Register-field decode helpers
//
// The 8080's dense instruction blocks (MOV, the ALU-immediate-source group,
// MVI, INR/DCR) all encode an 8-bit operand register in a 3-bit field using
// the same order: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) [memory] 7=A. Decoding that
// field generically here means i8080_step doesn't need 64+64+8+8 near-
// duplicate switch cases for those four blocks.
// ============================================================================

static inline uint8_t reg_get(i8080_t *c, unsigned idx) {
    switch (idx) {
        case 0: return c->b;
        case 1: return c->c;
        case 2: return c->d;
        case 3: return c->e;
        case 4: return c->h;
        case 5: return c->l;
        case 6: return c->mem_read(c->ctx, (uint16_t)((c->h << 8) | c->l));
        default: return c->a;
    }
}

static inline void reg_set(i8080_t *c, unsigned idx, uint8_t v) {
    switch (idx) {
        case 0: c->b = v; break;
        case 1: c->c = v; break;
        case 2: c->d = v; break;
        case 3: c->e = v; break;
        case 4: c->h = v; break;
        case 5: c->l = v; break;
        case 6: c->mem_write(c->ctx, (uint16_t)((c->h << 8) | c->l), v); break;
        default: c->a = v; break;
    }
}

// Register-pair decode for LXI/INX/DCX/DAD: 0=BC 1=DE 2=HL 3=SP.
static inline uint16_t rp_get(i8080_t *c, unsigned rp) {
    switch (rp) {
        case 0: return (uint16_t)((c->b << 8) | c->c);
        case 1: return (uint16_t)((c->d << 8) | c->e);
        case 2: return (uint16_t)((c->h << 8) | c->l);
        default: return c->sp;
    }
}

static inline void rp_set(i8080_t *c, unsigned rp, uint16_t v) {
    switch (rp) {
        case 0: c->b = (uint8_t)(v >> 8); c->c = (uint8_t)v; break;
        case 1: c->d = (uint8_t)(v >> 8); c->e = (uint8_t)v; break;
        case 2: c->h = (uint8_t)(v >> 8); c->l = (uint8_t)v; break;
        default: c->sp = v; break;
    }
}

// PUSH/POP use a different pairing for index 3: PSW (A + flag byte) instead
// of SP - a real 8080 quirk, not a typo.
static inline uint16_t rp_push_get(i8080_t *c, unsigned rp) {
    if (rp == 3) {
        uint8_t psw = 0x02; // bit1 always 1, bits 3/5 always 0 on real hardware
        psw |= c->flag_cy ? 0x01 : 0;
        psw |= c->flag_p ? 0x04 : 0;
        psw |= c->flag_ac ? 0x10 : 0;
        psw |= c->flag_z ? 0x40 : 0;
        psw |= c->flag_s ? 0x80 : 0;
        return (uint16_t)((c->a << 8) | psw);
    }
    return rp_get(c, rp);
}

static inline void rp_pop_set(i8080_t *c, unsigned rp, uint16_t v) {
    if (rp == 3) {
        c->a = (uint8_t)(v >> 8);
        uint8_t psw = (uint8_t)v;
        c->flag_cy = (psw & 0x01) != 0;
        c->flag_p = (psw & 0x04) != 0;
        c->flag_ac = (psw & 0x10) != 0;
        c->flag_z = (psw & 0x40) != 0;
        c->flag_s = (psw & 0x80) != 0;
        return;
    }
    rp_set(c, rp, v);
}

// ============================================================================
// Memory/stack helpers
// ============================================================================

static inline uint8_t fetch8(i8080_t *c) {
    return c->mem_read(c->ctx, c->pc++);
}

static inline uint16_t fetch16(i8080_t *c) {
    uint8_t lo = fetch8(c);
    uint8_t hi = fetch8(c);
    return (uint16_t)((hi << 8) | lo);
}

// Stack grows down; low byte at the lower address, matching real 8080 layout.
static inline void push16(i8080_t *c, uint16_t v) {
    uint8_t hi = (uint8_t)(v >> 8), lo = (uint8_t)v;
    c->mem_write(c->ctx, --c->sp, hi);
    c->mem_write(c->ctx, --c->sp, lo);
}

static inline uint16_t pop16(i8080_t *c) {
    uint8_t lo = c->mem_read(c->ctx, c->sp++);
    uint8_t hi = c->mem_read(c->ctx, c->sp++);
    return (uint16_t)((hi << 8) | lo);
}

// ============================================================================
// Flags
// ============================================================================

static inline uint8_t parity_even(uint8_t v) {
    return (uint8_t)(!__builtin_parity(v));
}

static inline void set_zsp(i8080_t *c, uint8_t v) {
    c->flag_z = (v == 0);
    c->flag_s = (v & 0x80) != 0;
    c->flag_p = parity_even(v);
}

static inline void do_add(i8080_t *c, uint8_t v, uint8_t carry_in) {
    unsigned sum = (unsigned)c->a + v + carry_in;
    c->flag_ac = (((c->a & 0xF) + (v & 0xF) + carry_in) & 0x10) != 0;
    c->flag_cy = sum > 0xFF;
    c->a = (uint8_t)sum;
    set_zsp(c, c->a);
}

static inline void do_sub(i8080_t *c, uint8_t v, uint8_t borrow_in) {
    int result = (int)c->a - (int)v - (int)borrow_in;
    c->flag_ac = (((int)(c->a & 0xF) - (int)(v & 0xF) - (int)borrow_in) >= 0);
    c->flag_cy = result < 0;
    c->a = (uint8_t)result;
    set_zsp(c, c->a);
}

static inline void do_cmp(i8080_t *c, uint8_t v) {
    int result = (int)c->a - (int)v;
    c->flag_ac = (((int)(c->a & 0xF) - (int)(v & 0xF)) >= 0);
    c->flag_cy = result < 0;
    set_zsp(c, (uint8_t)result);
}

// ANA sets AC to the OR of bit3 of both operands (a documented real-hardware
// quirk) rather than always 0 like XRA/ORA.
static inline void do_and(i8080_t *c, uint8_t v) {
    c->flag_ac = ((c->a | v) & 0x08) != 0;
    c->a &= v;
    c->flag_cy = 0;
    set_zsp(c, c->a);
}

static inline void do_xor(i8080_t *c, uint8_t v) {
    c->a ^= v;
    c->flag_cy = 0;
    c->flag_ac = 0;
    set_zsp(c, c->a);
}

static inline void do_or(i8080_t *c, uint8_t v) {
    c->a |= v;
    c->flag_cy = 0;
    c->flag_ac = 0;
    set_zsp(c, c->a);
}

static inline uint8_t do_inr(i8080_t *c, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    c->flag_ac = ((v & 0xF) + 1) > 0xF;
    set_zsp(c, r);
    return r;
}

static inline uint8_t do_dcr(i8080_t *c, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    c->flag_ac = (v & 0xF) != 0;
    set_zsp(c, r);
    return r;
}

// Textbook double-dabble BCD correction. Space Invaders' scoring code uses
// DAA to keep the score in packed BCD (cheap to turn into ASCII digits for
// the video RAM text), so this needs to be right, not just plausible.
static inline void do_daa(i8080_t *c) {
    uint8_t a = c->a;
    uint8_t cy = c->flag_cy;
    uint8_t correction = 0;

    if (c->flag_ac || (a & 0x0F) > 9)
        correction |= 0x06;
    if (cy || (a >> 4) > 9 || ((a >> 4) == 9 && (a & 0x0F) > 9)) {
        correction |= 0x60;
        cy = 1;
    }

    unsigned sum = (unsigned)a + correction;
    c->flag_ac = ((a & 0xF) + (correction & 0xF)) > 0xF;
    c->a = (uint8_t)sum;
    c->flag_cy = cy;
    set_zsp(c, c->a);
}

static inline int cond_true(i8080_t *c, unsigned cc) {
    switch (cc) {
        case 0: return !c->flag_z;  // NZ
        case 1: return c->flag_z;   // Z
        case 2: return !c->flag_cy; // NC
        case 3: return c->flag_cy;  // C
        case 4: return !c->flag_p;  // PO
        case 5: return c->flag_p;   // PE
        case 6: return !c->flag_s;  // P (plus/positive)
        default: return c->flag_s;  // M (minus)
    }
}

// ============================================================================
// Core
// ============================================================================

void i8080_reset(i8080_t *c) {
    c->a = c->b = c->c = c->d = c->e = c->h = c->l = 0;
    c->sp = 0;
    c->pc = 0;
    c->flag_s = c->flag_z = c->flag_ac = c->flag_p = c->flag_cy = 0;
    c->interrupts_enabled = 0;
    c->halted = 0;
}

int i8080_step(i8080_t *c) {
    if (c->halted)
        return 4; // idles like a NOP until an interrupt wakes it

    uint8_t op = fetch8(c);

    // --- Densely-packed regular blocks, decoded generically ---
    if (op >= 0x40 && op <= 0x7F && op != 0x76) { // MOV D,S
        unsigned dst = (op >> 3) & 7, src = op & 7;
        reg_set(c, dst, reg_get(c, src));
        return (dst == 6 || src == 6) ? 7 : 5;
    }
    if (op >= 0x80 && op <= 0xBF) { // ALU A,S (ADD/ADC/SUB/SBB/ANA/XRA/ORA/CMP)
        unsigned alu = (op >> 3) & 7, src = op & 7;
        uint8_t v = reg_get(c, src);
        switch (alu) {
            case 0: do_add(c, v, 0); break;
            case 1: do_add(c, v, c->flag_cy); break;
            case 2: do_sub(c, v, 0); break;
            case 3: do_sub(c, v, c->flag_cy); break;
            case 4: do_and(c, v); break;
            case 5: do_xor(c, v); break;
            case 6: do_or(c, v); break;
            default: do_cmp(c, v); break;
        }
        return (src == 6) ? 7 : 4;
    }
    if ((op & 0xC7) == 0x06) { // MVI D,d8
        unsigned dst = (op >> 3) & 7;
        reg_set(c, dst, fetch8(c));
        return (dst == 6) ? 10 : 7;
    }
    if ((op & 0xC7) == 0x04) { // INR D
        unsigned dst = (op >> 3) & 7;
        reg_set(c, dst, do_inr(c, reg_get(c, dst)));
        return (dst == 6) ? 10 : 5;
    }
    if ((op & 0xC7) == 0x05) { // DCR D
        unsigned dst = (op >> 3) & 7;
        reg_set(c, dst, do_dcr(c, reg_get(c, dst)));
        return (dst == 6) ? 10 : 5;
    }

    // --- Everything else ---
    switch (op) {
        case 0x00: case 0x08: case 0x10: case 0x18: // 0x08/0x10/0x18/... are
        case 0x20: case 0x28: case 0x30: case 0x38:  // documented NOP duplicates
            return 4;

        case 0x76: // HLT
            c->halted = 1;
            return 7;

        case 0x01: case 0x11: case 0x21: case 0x31: // LXI RP,d16
            rp_set(c, (op >> 4) & 3, fetch16(c));
            return 10;

        case 0x03: case 0x13: case 0x23: case 0x33: { // INX RP
            unsigned rp = (op >> 4) & 3;
            rp_set(c, rp, (uint16_t)(rp_get(c, rp) + 1));
            return 5;
        }
        case 0x0B: case 0x1B: case 0x2B: case 0x3B: { // DCX RP
            unsigned rp = (op >> 4) & 3;
            rp_set(c, rp, (uint16_t)(rp_get(c, rp) - 1));
            return 5;
        }
        case 0x09: case 0x19: case 0x29: case 0x39: { // DAD RP
            unsigned hl = rp_get(c, 2);
            unsigned sum = hl + rp_get(c, (op >> 4) & 3);
            c->flag_cy = sum > 0xFFFF;
            c->h = (uint8_t)(sum >> 8);
            c->l = (uint8_t)sum;
            return 10;
        }

        case 0x02: c->mem_write(c->ctx, (uint16_t)((c->b << 8) | c->c), c->a); return 7; // STAX B
        case 0x12: c->mem_write(c->ctx, (uint16_t)((c->d << 8) | c->e), c->a); return 7; // STAX D
        case 0x0A: c->a = c->mem_read(c->ctx, (uint16_t)((c->b << 8) | c->c)); return 7; // LDAX B
        case 0x1A: c->a = c->mem_read(c->ctx, (uint16_t)((c->d << 8) | c->e)); return 7; // LDAX D

        case 0x22: { // SHLD a16
            uint16_t addr = fetch16(c);
            c->mem_write(c->ctx, addr, c->l);
            c->mem_write(c->ctx, (uint16_t)(addr + 1), c->h);
            return 16;
        }
        case 0x2A: { // LHLD a16
            uint16_t addr = fetch16(c);
            c->l = c->mem_read(c->ctx, addr);
            c->h = c->mem_read(c->ctx, (uint16_t)(addr + 1));
            return 16;
        }
        case 0x32: c->mem_write(c->ctx, fetch16(c), c->a); return 13; // STA a16
        case 0x3A: c->a = c->mem_read(c->ctx, fetch16(c)); return 13; // LDA a16

        case 0x07: { // RLC
            uint8_t cy = (c->a >> 7) & 1;
            c->a = (uint8_t)((c->a << 1) | cy);
            c->flag_cy = cy;
            return 4;
        }
        case 0x0F: { // RRC
            uint8_t cy = c->a & 1;
            c->a = (uint8_t)((c->a >> 1) | (cy << 7));
            c->flag_cy = cy;
            return 4;
        }
        case 0x17: { // RAL
            uint8_t old_cy = c->flag_cy;
            c->flag_cy = (c->a >> 7) & 1;
            c->a = (uint8_t)((c->a << 1) | old_cy);
            return 4;
        }
        case 0x1F: { // RAR
            uint8_t old_cy = c->flag_cy;
            c->flag_cy = c->a & 1;
            c->a = (uint8_t)((c->a >> 1) | (old_cy << 7));
            return 4;
        }

        case 0x27: do_daa(c); return 4;                 // DAA
        case 0x2F: c->a = (uint8_t)~c->a; return 4;      // CMA
        case 0x37: c->flag_cy = 1; return 4;             // STC
        case 0x3F: c->flag_cy = !c->flag_cy; return 4;   // CMC

        case 0xC6: do_add(c, fetch8(c), 0); return 7;              // ADI
        case 0xCE: do_add(c, fetch8(c), c->flag_cy); return 7;     // ACI
        case 0xD6: do_sub(c, fetch8(c), 0); return 7;              // SUI
        case 0xDE: do_sub(c, fetch8(c), c->flag_cy); return 7;     // SBI
        case 0xE6: do_and(c, fetch8(c)); return 7;                 // ANI
        case 0xEE: do_xor(c, fetch8(c)); return 7;                 // XRI
        case 0xF6: do_or(c, fetch8(c)); return 7;                  // ORI
        case 0xFE: do_cmp(c, fetch8(c)); return 7;                 // CPI

        case 0xC3: case 0xCB: // JMP (0xCB documented duplicate)
            c->pc = fetch16(c);
            return 10;

        case 0xC2: case 0xCA: case 0xD2: case 0xDA: // Jcond a16
        case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
            uint16_t addr = fetch16(c);
            if (cond_true(c, (op >> 3) & 7))
                c->pc = addr;
            return 10;
        }

        case 0xCD: case 0xDD: case 0xED: case 0xFD: { // CALL (0xDD/0xED/0xFD documented duplicates)
            uint16_t addr = fetch16(c);
            push16(c, c->pc);
            c->pc = addr;
            return 17;
        }
        case 0xC4: case 0xCC: case 0xD4: case 0xDC: // Ccond a16
        case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
            uint16_t addr = fetch16(c);
            if (cond_true(c, (op >> 3) & 7)) {
                push16(c, c->pc);
                c->pc = addr;
                return 17;
            }
            return 11;
        }

        case 0xC9: case 0xD9: // RET (0xD9 documented duplicate)
            c->pc = pop16(c);
            return 10;

        case 0xC0: case 0xC8: case 0xD0: case 0xD8: // Rcond
        case 0xE0: case 0xE8: case 0xF0: case 0xF8:
            if (cond_true(c, (op >> 3) & 7)) {
                c->pc = pop16(c);
                return 11;
            }
            return 5;

        case 0xC7: case 0xCF: case 0xD7: case 0xDF: // RST N
        case 0xE7: case 0xEF: case 0xF7: case 0xFF: {
            unsigned n = (op >> 3) & 7;
            push16(c, c->pc);
            c->pc = (uint16_t)(n * 8);
            return 11;
        }

        case 0xE9: c->pc = rp_get(c, 2); return 5; // PCHL
        case 0xF9: c->sp = rp_get(c, 2); return 5; // SPHL

        case 0xC5: case 0xD5: case 0xE5: case 0xF5: // PUSH RP
            push16(c, rp_push_get(c, (op >> 4) & 3));
            return 11;
        case 0xC1: case 0xD1: case 0xE1: case 0xF1: // POP RP
            rp_pop_set(c, (op >> 4) & 3, pop16(c));
            return 10;

        case 0xE3: { // XTHL
            uint8_t lo = c->mem_read(c->ctx, c->sp);
            uint8_t hi = c->mem_read(c->ctx, (uint16_t)(c->sp + 1));
            c->mem_write(c->ctx, c->sp, c->l);
            c->mem_write(c->ctx, (uint16_t)(c->sp + 1), c->h);
            c->l = lo;
            c->h = hi;
            return 18;
        }
        case 0xEB: { // XCHG
            uint8_t th = c->h, tl = c->l;
            c->h = c->d; c->l = c->e;
            c->d = th; c->e = tl;
            return 4;
        }

        case 0xDB: c->a = c->io_in(c->ctx, fetch8(c)); return 10;      // IN port
        case 0xD3: c->io_out(c->ctx, fetch8(c), c->a); return 10;      // OUT port

        case 0xF3: c->interrupts_enabled = 0; return 4; // DI
        case 0xFB: c->interrupts_enabled = 1; return 4; // EI

        default:
            // Every one of the 256 possible opcode values is handled above
            // or by the generic blocks - this is unreachable for a real
            // 8080, but fail soft (as a NOP) rather than crash if it ever
            // is reached.
            return 4;
    }
}

int i8080_interrupt(i8080_t *c, uint8_t rst_opcode) {
    if (!c->interrupts_enabled)
        return 0;
    c->interrupts_enabled = 0;
    c->halted = 0;
    push16(c, c->pc);
    unsigned n = (rst_opcode >> 3) & 7;
    c->pc = (uint16_t)(n * 8);
    return 11;
}
