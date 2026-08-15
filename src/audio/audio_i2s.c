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

// sound_table[] PCM is authored at SOUND_SAMPLE_RATE_HZ (32 kHz).
// Output is at AUDIO_SAMPLE_RATE (32 kHz).
// With matching sample rates, voice advance is direct 1:1 with zero resampling overhead.
static inline int16_t voice_advance(audio_voice_t *v, bool loop) {
    if (!v->data || v->len == 0) {
        v->active = false;
        return 0;
    }
    int16_t sample = v->data[v->pos];
#if SOUND_SAMPLE_RATE_HZ == AUDIO_SAMPLE_RATE
    if (++v->pos >= v->len) {
        if (loop) {
            v->pos = 0;
        } else {
            v->active = false;
            v->pos = v->len - 1;
        }
    }
#else
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
#endif
    return sample;
}

#if DEBUG_AUDIO_TEST_TONE
#define DEBUG_TONE_FREQ_HZ 1000
#define DEBUG_TONE_LUT_LEN (AUDIO_SAMPLE_RATE / DEBUG_TONE_FREQ_HZ)
static int16_t debug_tone_lut[DEBUG_TONE_LUT_LEN];
static audio_voice_t debug_voice;
#endif

static inline void mix_one_sample(int16_t *out_mono) {
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

// Mixes and pushes 4-sample Data Island packets to keep the HDMI audio queue
// filled to target_level (200 packets per 60 Hz frame).
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
        audio_frame_counter = hstx_packet_set_audio_samples_cs_rate(
            &packet, samples, 4, audio_frame_counter, AUDIO_SAMPLE_RATE
        );
        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, DI_HSYNC_ACTIVE);
        if (!hstx_di_queue_push(&island)) {
            break;
        }
    }
}

// audio_i2s_feed_queue()'s per-call push cap is the anti-churn bound that
// matters once Core 1/HSTX is actively draining the queue in real time.
// Before Core 1 has been launched, nothing is consuming yet, so there's no
// burst/churn hazard - only an empty buffer that needs to actually reach
// target_level before real-time playback starts. Call once at boot, before
// multicore_launch_core1().
void audio_i2s_prefill_queue(uint32_t target_level) {
    while (hstx_di_queue_get_level() < target_level) {
        audio_i2s_feed_queue(target_level);
    }
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
