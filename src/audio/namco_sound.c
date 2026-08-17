#include "namco_sound.h"
#include <string.h>

static const uint8_t s_default_sound_prom[256] = {
    // Wave 0: 32-sample Sine Wave
    7, 8, 10, 11, 13, 14, 14, 15, 15, 15, 14, 14, 13, 11, 10, 8,
    7, 6,  4,  3,  1,  0,  0,  0,  0,  0,  0,  1,  3,  4,  6, 7,
    // Wave 1: Triangle / Sawtooth
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    // Wave 2: 12.5% Pulse
    15, 15, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // Wave 3: 25% Pulse
    15, 15, 15, 15, 15, 15, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // Wave 4: 50% Square Wave
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // Wave 5: Chirp / Exponential Ramp
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
    8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
    // Wave 6: High Frequency Octave / Siren
    0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15,
    0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15,
    // Wave 7: Complex Harmonic
    7, 12, 3, 15, 1, 9, 4, 14, 2, 11, 6, 13, 0, 8, 5, 10,
    7, 2, 11, 0, 13, 5, 10, 1, 12, 3, 8, 2, 14, 6, 9, 4
};

void namco_sound_init(namco_sound_t *snd, const uint8_t *sound_prom) {
    memset(snd, 0, sizeof(namco_sound_t));
    bool has_prom = false;
    if (sound_prom) {
        for (int i = 0; i < 256; i++) {
            if (sound_prom[i] != 0) {
                has_prom = true;
                break;
            }
        }
    }
    snd->prom = has_prom ? sound_prom : s_default_sound_prom;
    snd->sound_enable = true;
}

void namco_sound_reset(namco_sound_t *snd) {
    memset(snd->regs, 0, sizeof(snd->regs));
    memset(snd->accum, 0, sizeof(snd->accum));
    snd->sound_enable = true;
}

void namco_sound_write(namco_sound_t *snd, uint16_t offset, uint8_t val) {
    if (offset < 32) {
        snd->regs[offset] = val & 0x0F;
    }
}

void namco_sound_set_enable(namco_sound_t *snd, bool enable) {
    snd->sound_enable = enable;
}

void namco_sound_render_48k(namco_sound_t *snd, int16_t *buf_interleaved, uint32_t sample_count) {
    if (!snd->sound_enable || !snd->prom) {
        memset(buf_interleaved, 0, sample_count * 2 * sizeof(int16_t));
        return;
    }

    // Decode 3 voices (Namco WSG 3-voice 4-bit register map)
    // Voice 1 (20-bit frequency at 0x10..0x14, wave at 0x05, vol at 0x15):
    uint32_t step1 = (snd->regs[0x10]) | (snd->regs[0x11] << 4) | (snd->regs[0x12] << 8) |
                     (snd->regs[0x13] << 12) | (snd->regs[0x14] << 16);
    uint8_t wave1 = snd->regs[0x05] & 0x07;
    uint8_t vol1 = snd->regs[0x15] & 0x0F;

    // Voice 2 (20-bit frequency with 16-bit at 0x16..0x19 shifted by 4, wave at 0x0A, vol at 0x1A):
    uint32_t step2 = (snd->regs[0x16] << 4) | (snd->regs[0x17] << 8) |
                     (snd->regs[0x18] << 12) | (snd->regs[0x19] << 16);
    uint8_t wave2 = snd->regs[0x0A] & 0x07;
    uint8_t vol2 = snd->regs[0x1A] & 0x0F;

    // Voice 3 (20-bit frequency with 16-bit at 0x1B..0x1E shifted by 4, wave at 0x0F, vol at 0x1F):
    uint32_t step3 = (snd->regs[0x1B] << 4) | (snd->regs[0x1C] << 8) |
                     (snd->regs[0x1D] << 12) | (snd->regs[0x1E] << 16);
    uint8_t wave3 = snd->regs[0x0F] & 0x07;
    uint8_t vol3 = snd->regs[0x1F] & 0x0F;

    const uint8_t *w1 = &snd->prom[wave1 * 32];
    const uint8_t *w2 = &snd->prom[wave2 * 32];
    const uint8_t *w3 = &snd->prom[wave3 * 32];

    for (uint32_t i = 0; i < sample_count; i++) {
        int sum = 0;

        if (vol1 > 0 && step1 > 0) {
            snd->accum[0] = (snd->accum[0] + (step1 * 2)) & 0xFFFFF; // 96kHz / 48kHz = step * 2
            uint8_t sample_idx = (snd->accum[0] >> 15) & 31;
            int s = (int)(w1[sample_idx] & 0x0F) - 8;
            sum += s * (int)vol1;
        }

        if (vol2 > 0 && step2 > 0) {
            snd->accum[1] = (snd->accum[1] + (step2 * 2)) & 0xFFFFF;
            uint8_t sample_idx = (snd->accum[1] >> 15) & 31;
            int s = (int)(w2[sample_idx] & 0x0F) - 8;
            sum += s * (int)vol2;
        }

        if (vol3 > 0 && step3 > 0) {
            snd->accum[2] = (snd->accum[2] + (step3 * 2)) & 0xFFFFF;
            uint8_t sample_idx = (snd->accum[2] >> 15) & 31;
            int s = (int)(w3[sample_idx] & 0x0F) - 8;
            sum += s * (int)vol3;
        }

        // Scale sum (max 3 * 8 * 15 = 360) to 16-bit signed PCM
        int16_t out_pcm = (int16_t)(sum * 60);
        buf_interleaved[i * 2]     = out_pcm; // Left channel
        buf_interleaved[i * 2 + 1] = out_pcm; // Right channel
    }
}
