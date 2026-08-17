#ifndef __Z80USER_INCLUDED__
#define __Z80USER_INCLUDED__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct z80_ctx {
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t val);
    uint8_t (*in)(void *ctx, uint8_t port);
    void (*out)(void *ctx, uint8_t port, uint8_t val);
} z80_ctx_t;

#define Z80_READ_BYTE(address, x) \
    do { \
        const z80_ctx_t *c = (const z80_ctx_t *)context; \
        (x) = c->read(context, (address) & 0xffff); \
    } while (0)

#define Z80_FETCH_BYTE(address, x) Z80_READ_BYTE((address), (x))

#define Z80_READ_WORD(address, x) \
    do { \
        const z80_ctx_t *c = (const z80_ctx_t *)context; \
        uint16_t a = (address) & 0xffff; \
        (x) = (uint16_t)(c->read(context, a) | (c->read(context, (a + 1) & 0xffff) << 8)); \
    } while (0)

#define Z80_FETCH_WORD(address, x) Z80_READ_WORD((address), (x))

#define Z80_WRITE_BYTE(address, x) \
    do { \
        const z80_ctx_t *c = (const z80_ctx_t *)context; \
        c->write(context, (address) & 0xffff, (uint8_t)(x)); \
    } while (0)

#define Z80_WRITE_WORD(address, x) \
    do { \
        const z80_ctx_t *c = (const z80_ctx_t *)context; \
        uint16_t a = (address) & 0xffff; \
        c->write(context, a, (uint8_t)(x)); \
        c->write(context, (a + 1) & 0xffff, (uint8_t)((x) >> 8)); \
    } while (0)

#define Z80_READ_WORD_INTERRUPT(address, x) Z80_READ_WORD((address), (x))
#define Z80_WRITE_WORD_INTERRUPT(address, x) Z80_WRITE_WORD((address), (x))

#define Z80_INPUT_BYTE(port, x) \
    do { \
        const z80_ctx_t *c = (const z80_ctx_t *)context; \
        (x) = c->in(context, (uint8_t)(port)); \
    } while (0)

#define Z80_OUTPUT_BYTE(port, x) \
    do { \
        const z80_ctx_t *c = (const z80_ctx_t *)context; \
        c->out(context, (uint8_t)(port), (uint8_t)(x)); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif
