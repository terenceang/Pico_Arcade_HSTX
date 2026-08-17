#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/plugin_api.h"

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_QUEUE_TARGET_LEVEL ((AUDIO_SAMPLE_RATE / 4) / 60) // 200 packets per 60Hz frame

// Initialize the 48kHz HDMI audio subsystem
void audio_engine_init(void);

// Pre-fill the audio Data Island queue before launching Core 1
void audio_engine_prefill_queue(uint32_t target_level);

// Feed the HDMI Data Island queue by requesting samples from the active plugin
void audio_engine_feed_queue(uint32_t target_level);

// Set audio mute
void audio_engine_set_mute(bool mute);

// Debug 1 kHz test tone
void audio_engine_debug_play_test_tone(void);
void audio_engine_debug_stop_test_tone(void);

// Trigger a short (~50ms), self-fading feedback beep, mixed additively over
// whatever else is playing (including silence while muted). Fire-and-forget,
// non-blocking - call once per keypress edge, e.g. from the controller test
// card. Re-triggering while a beep is already playing restarts it.
void audio_engine_play_keypress_beep(void);

#endif // AUDIO_ENGINE_H
