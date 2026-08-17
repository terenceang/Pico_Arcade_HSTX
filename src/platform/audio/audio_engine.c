#include "audio_engine.h"
#include "core/plugin_registry.h"
#include "video/display_config.h"

#include <string.h>
#include <math.h>

#include "hardware/sync.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/hstx_data_island_queue.h"

#if DEBUG_AUDIO_TEST_TONE
#define DEBUG_TONE_LUT_LEN 48
static const int16_t debug_tone_lut[DEBUG_TONE_LUT_LEN] = {
         0,   2349,   4658,   6888,   8999,  10957,  12727,  14280,
     15588,  16629,  17386,  17846,  18000,  17846,  17386,  16629,
     15588,  14280,  12727,  10957,   8999,   6888,   4658,   2349,
         0,  -2349,  -4658,  -6888,  -8999, -10957, -12727, -14280,
    -15588, -16629, -17386, -17846, -18000, -17846, -17386, -16629,
    -15588, -14280, -12727, -10957,  -9000,  -6888,  -4658,  -2349
};
static uint32_t debug_tone_pos = 0;
static volatile bool debug_tone_active = false;
#endif

static int s_audio_frame_counter = 0;
static bool s_muted = false;

// Short keypress feedback beep: a fixed-frequency square wave with a linear
// fade-in/out envelope (avoids clicks at onset/release), mixed additively
// and independent of mute/plugin audio so it's audible even over silence.
#define KEYPRESS_BEEP_FREQ_HZ 1200
#define KEYPRESS_BEEP_HALF_PERIOD_SAMPLES (AUDIO_SAMPLE_RATE / (2 * KEYPRESS_BEEP_FREQ_HZ))
#define KEYPRESS_BEEP_DURATION_SAMPLES (AUDIO_SAMPLE_RATE / 20) // 50ms
#define KEYPRESS_BEEP_FADE_SAMPLES (AUDIO_SAMPLE_RATE / 200)    // 5ms
#define KEYPRESS_BEEP_AMPLITUDE 6000

static volatile uint32_t s_beep_samples_remaining = 0;
static uint32_t s_beep_half_period_counter = 0;
static bool s_beep_polarity = false;

void audio_engine_init(void) {
    s_audio_frame_counter = 0;
    s_muted = false;
#if DEBUG_AUDIO_TEST_TONE
    debug_tone_pos = 0;
    debug_tone_active = false;
#endif
    s_beep_samples_remaining = 0;
    s_beep_half_period_counter = 0;
    s_beep_polarity = false;
}

void audio_engine_play_keypress_beep(void) {
    uint32_t save = save_and_disable_interrupts();
    s_beep_samples_remaining = KEYPRESS_BEEP_DURATION_SAMPLES;
    s_beep_half_period_counter = 0;
    s_beep_polarity = false;
    restore_interrupts(save);
}

static void mix_keypress_beep(int16_t *pcm_interleaved) {
    for (int c = 0; c < 4; c++) {
        if (s_beep_samples_remaining == 0) {
            break;
        }

        if (s_beep_half_period_counter == 0) {
            s_beep_polarity = !s_beep_polarity;
            s_beep_half_period_counter = KEYPRESS_BEEP_HALF_PERIOD_SAMPLES;
        }
        s_beep_half_period_counter--;

        uint32_t elapsed = KEYPRESS_BEEP_DURATION_SAMPLES - s_beep_samples_remaining;
        uint32_t env = elapsed < s_beep_samples_remaining ? elapsed : s_beep_samples_remaining;
        if (env > KEYPRESS_BEEP_FADE_SAMPLES) {
            env = KEYPRESS_BEEP_FADE_SAMPLES;
        }

        int32_t t = (s_beep_polarity ? KEYPRESS_BEEP_AMPLITUDE : -KEYPRESS_BEEP_AMPLITUDE)
                    * (int32_t)env / (int32_t)KEYPRESS_BEEP_FADE_SAMPLES;

        int32_t left = (int32_t)pcm_interleaved[c * 2] + t;
        int32_t right = (int32_t)pcm_interleaved[c * 2 + 1] + t;
        if (left > INT16_MAX) left = INT16_MAX; else if (left < INT16_MIN) left = INT16_MIN;
        if (right > INT16_MAX) right = INT16_MAX; else if (right < INT16_MIN) right = INT16_MIN;
        pcm_interleaved[c * 2] = (int16_t)left;
        pcm_interleaved[c * 2 + 1] = (int16_t)right;

        s_beep_samples_remaining--;
    }
}

void audio_engine_set_mute(bool mute) {
    s_muted = mute;
}

#if DEBUG_AUDIO_TEST_TONE
void audio_engine_debug_play_test_tone(void) {
    uint32_t save = save_and_disable_interrupts();
    debug_tone_pos = 0;
    debug_tone_active = true;
    restore_interrupts(save);
}

void audio_engine_debug_stop_test_tone(void) {
    uint32_t save = save_and_disable_interrupts();
    debug_tone_active = false;
    restore_interrupts(save);
}
#else
void audio_engine_debug_play_test_tone(void) {}
void audio_engine_debug_stop_test_tone(void) {}
#endif

void audio_engine_feed_queue(uint32_t target_level) {
    const emulator_plugin_t *plugin = plugin_registry_get_active();

    while (hstx_di_queue_get_level() < target_level) {
        audio_sample_t samples[4];
        int16_t pcm_interleaved[8]; // 4 stereo samples

        if (s_muted
#if DEBUG_AUDIO_TEST_TONE
            && !debug_tone_active
#endif
        ) {
            memset(pcm_interleaved, 0, sizeof(pcm_interleaved));
        } else {
            if (!plugin || !plugin->render_audio) {
                memset(pcm_interleaved, 0, sizeof(pcm_interleaved));
            } else {
                plugin->render_audio(pcm_interleaved, 4);
            }

#if DEBUG_AUDIO_TEST_TONE
            if (debug_tone_active) {
                for (int c = 0; c < 4; c++) {
                    int32_t t = debug_tone_lut[debug_tone_pos];
                    if (++debug_tone_pos >= DEBUG_TONE_LUT_LEN) debug_tone_pos = 0;

                    int32_t left = (int32_t)pcm_interleaved[c * 2] + t;
                    int32_t right = (int32_t)pcm_interleaved[c * 2 + 1] + t;
                    if (left > INT16_MAX) left = INT16_MAX; else if (left < INT16_MIN) left = INT16_MIN;
                    if (right > INT16_MAX) right = INT16_MAX; else if (right < INT16_MIN) right = INT16_MIN;
                    pcm_interleaved[c * 2] = (int16_t)left;
                    pcm_interleaved[c * 2 + 1] = (int16_t)right;
                }
            }
#endif
        }

        if (s_beep_samples_remaining > 0) {
            mix_keypress_beep(pcm_interleaved);
        }

        for (int c = 0; c < 4; c++) {
            samples[c].left = pcm_interleaved[c * 2];
            samples[c].right = pcm_interleaved[c * 2 + 1];
        }

        hstx_packet_t packet;
        s_audio_frame_counter = hstx_packet_set_audio_samples_cs_rate(
            &packet, samples, 4, s_audio_frame_counter, AUDIO_SAMPLE_RATE
        );
        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, DI_HSYNC_ACTIVE);
        if (!hstx_di_queue_push(&island)) {
            break;
        }
    }
}

void audio_engine_prefill_queue(uint32_t target_level) {
    while (hstx_di_queue_get_level() < target_level) {
        audio_engine_feed_queue(target_level);
    }
}
