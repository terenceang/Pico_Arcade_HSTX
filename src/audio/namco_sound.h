#ifndef NAMCO_SOUND_H
#define NAMCO_SOUND_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t regs[32];       // Memory-mapped registers 0x5040 - 0x505F
    uint32_t accum[3];      // 20-bit phase accumulators for 3 voices
    bool sound_enable;      // Controlled by port 0x5001
    const uint8_t *prom;    // 256-byte 82s126.1m sound PROM
} namco_sound_t;

void namco_sound_init(namco_sound_t *snd, const uint8_t *sound_prom);
void namco_sound_reset(namco_sound_t *snd);
void namco_sound_write(namco_sound_t *snd, uint16_t offset, uint8_t val);
void namco_sound_set_enable(namco_sound_t *snd, bool enable);

// Generates 48 kHz stereo 16-bit PCM samples
void namco_sound_render_48k(namco_sound_t *snd, int16_t *buf_interleaved, uint32_t sample_count);

#endif // NAMCO_SOUND_H
