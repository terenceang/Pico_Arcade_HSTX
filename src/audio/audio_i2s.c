#include <stdint.h>
#include <stdbool.h>

#include "display_config.h"

#if DEBUG_AUDIO_TEST_TONE
#include <math.h>
#endif

#include "hardware/sync.h"
#include "audio_i2s.h"
#include "sound_data.h"

#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/hstx_data_island_queue.h"

#define AUDIO_MAX_VOICES 4

typedef struct {
    const int16_t *data;
    uint32_t pos;
    uint32_t len;
    uint32_t frac; // resampling accumulator - see voice_advance()
    bool active;
} audio_voice_t;

static audio_voice_t voices[AUDIO_MAX_VOICES];
static unsigned voice_steal_next;
static audio_voice_t loop_voice;
static int audio_frame_counter = 0;

// sound_table[] PCM is authored at SOUND_SAMPLE_RATE_HZ (see sound_data.h),
// but the mixer must output at AUDIO_SAMPLE_RATE (fixed by the HDMI audio
// pacing/ACR math - see dvi_display.c). A Bresenham-style accumulator steps
// the source position at SOUND_SAMPLE_RATE_HZ/AUDIO_SAMPLE_RATE per output
// sample; the accumulator itself stays bounded by AUDIO_SAMPLE_RATE
// regardless of how long the sound is, unlike a fixed-point position would
// be. Nearest-sample only (no interpolation) - acceptable for short 8-bit-
// era arcade effects.
static inline int16_t voice_advance(audio_voice_t *v, bool loop) {
    int16_t sample = v->data[v->pos];
    v->frac += SOUND_SAMPLE_RATE_HZ;
    while (v->frac >= AUDIO_SAMPLE_RATE) {
        v->frac -= AUDIO_SAMPLE_RATE;
        if (++v->pos >= v->len) {
            if (loop) {
                v->pos = 0;
            } else {
                v->active = false;
                v->pos = v->len - 1;
                break;
            }
        }
    }
    return sample;
}

#if DEBUG_AUDIO_TEST_TONE
#define DEBUG_TONE_LUT_LEN 48 // AUDIO_SAMPLE_RATE (48,000 Hz) / 48 = 1,000 Hz (1 kHz tone)
static int16_t debug_tone_lut[DEBUG_TONE_LUT_LEN];
static audio_voice_t debug_voice;
#endif

static void mix_one_sample(int16_t *out_mono) {
    int32_t mix = 0;

    if (loop_voice.active) {
        mix += voice_advance(&loop_voice, true);
    }

#if DEBUG_AUDIO_TEST_TONE
    if (debug_voice.active) {
        mix += debug_voice.data[debug_voice.pos];
        if (++debug_voice.pos >= debug_voice.len)
            debug_voice.pos = 0;
    }
#endif

    for (unsigned v = 0; v < AUDIO_MAX_VOICES; ++v) {
        if (!voices[v].active)
            continue;
        mix += voice_advance(&voices[v], false);
    }

    if (mix > INT16_MAX)
        mix = INT16_MAX;
    else if (mix < INT16_MIN)
        mix = INT16_MIN;

    *out_mono = (int16_t)mix;
}

// Mixes and pushes 4-sample Data Island packets until the HDMI audio queue
// reaches target_level, instead of generating a whole video frame's worth
// in one burst - mirrors pico_hdmi's own bouncing_box reference example,
// which keeps the queue continuously topped up rather than front-loading
// it once per frame. Call this often (e.g. periodically while rendering a
// frame's scanlines), not just once per frame.
void audio_i2s_feed_queue(uint32_t target_level) {
    while (hstx_di_queue_get_level() < target_level) {
        audio_sample_t samples[4];
        for (int c = 0; c < 4; c++) {
            int16_t sample16;
            mix_one_sample(&sample16);
            samples[c].left = sample16;
            samples[c].right = sample16;
        }
        hstx_packet_t packet;
        // Matches pico_hdmi's own bouncing_box reference example exactly
        // (explicit IEC 60958 channel status, sample-rate-aware).
        audio_frame_counter = hstx_packet_set_audio_samples_cs_rate(
            &packet, samples, 4, audio_frame_counter, AUDIO_SAMPLE_RATE
        );
        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, DI_HSYNC_ACTIVE);
        hstx_di_queue_push(&island);
    }
}

void audio_i2s_step_frame(void) {
    audio_i2s_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);
}

void audio_i2s_set_mute(bool mute) {
    (void)mute;
}

#if DEBUG_AUDIO_TEST_TONE
void audio_i2s_debug_play_test_tone(void) {
    for (unsigned i = 0; i < DEBUG_TONE_LUT_LEN; ++i)
        debug_tone_lut[i] = (int16_t)(20000 * sinf(2.0f * (float)M_PI * (float)i / DEBUG_TONE_LUT_LEN));

    uint32_t save = save_and_disable_interrupts();
    debug_voice.data = debug_tone_lut;
    debug_voice.len = DEBUG_TONE_LUT_LEN;
    debug_voice.pos = 0;
    debug_voice.active = true;
    restore_interrupts(save);
}

void audio_i2s_debug_stop_test_tone(void) {
    uint32_t save = save_and_disable_interrupts();
    debug_voice.active = false;
    restore_interrupts(save);
}
#endif

void audio_i2s_play_sound(sound_id_t sound_id) {
    if (sound_id >= SOUND_COUNT)
        return;
    const sound_sample_t *s = &sound_table[sound_id];
    if (s->frame_count == 0)
        return;

    uint32_t save = save_and_disable_interrupts();

    unsigned slot = AUDIO_MAX_VOICES;
    for (unsigned i = 0; i < AUDIO_MAX_VOICES; ++i) {
        if (!voices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == AUDIO_MAX_VOICES) {
        slot = voice_steal_next;
        voice_steal_next = (voice_steal_next + 1) % AUDIO_MAX_VOICES;
    }
    voices[slot].data = s->samples;
    voices[slot].len = s->frame_count;
    voices[slot].pos = 0;
    voices[slot].frac = 0;
    voices[slot].active = true;

    restore_interrupts(save);
}

void audio_i2s_set_sound_loop(sound_id_t sound_id, bool active) {
    if (sound_id >= SOUND_COUNT)
        return;

    uint32_t save = save_and_disable_interrupts();

    if (!active) {
        loop_voice.active = false;
    } else {
        const sound_sample_t *s = &sound_table[sound_id];
        if (s->frame_count == 0) {
            loop_voice.active = false;
        } else if (!loop_voice.active || loop_voice.data != s->samples) {
            loop_voice.data = s->samples;
            loop_voice.len = s->frame_count;
            loop_voice.pos = 0;
            loop_voice.frac = 0;
            loop_voice.active = true;
        }
    }

    restore_interrupts(save);
}

void audio_i2s_init(void) {
    voice_steal_next = 0;
}
