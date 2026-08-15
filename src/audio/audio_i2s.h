#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

#include <stdbool.h>
#include <stdint.h>

#include "display_config.h"
#include "sound_data.h"

// Software audio mixer for the Raspberry Pi Pico 2 - pushes mixed PCM samples
// to pico_hdmi's HSTX Data Island packet queue for HDMI audio output.
//
// Mixes up to AUDIO_MAX_VOICES simultaneous one-shot sound effects plus one
// looping voice (real hardware only ever loops the UFO hum) into a single
// mono stream - see audio_i2s.c. Sample data itself comes from sound_table
// (sound_data.h), embedded at build time from user-supplied files in
// sounds/ (see sounds/README.md) - there's no audio ROM to draw from, since
// the real cabinet's sounds came from discrete analog circuitry, not
// samples.

#define AUDIO_SAMPLE_RATE 48000

// Default HDMI audio queue fill target for audio_i2s_feed_queue() - one video frame's worth
// (AUDIO_SAMPLE_RATE / 4 samples per packet / DISPLAY_REFRESH_HZ).
#define AUDIO_QUEUE_TARGET_LEVEL ((AUDIO_SAMPLE_RATE / 4) / DISPLAY_REFRESH_HZ)

// Resets the software mixer's voice state. Call once, before the main
// loop starts calling audio_i2s_feed_queue().
void audio_i2s_init(void);

// Mixes and pushes 4-sample Data Island packets until the HDMI audio queue
// reaches target_level. Call this periodically (not just once per frame) so
// the queue stays continuously topped up rather than front-loaded in one
// burst - see audio_i2s.c.
void audio_i2s_feed_queue(uint32_t target_level);

// Blocks until the HDMI audio queue reaches target_level, ignoring
// audio_i2s_feed_queue()'s usual per-call push cap - safe only because
// nothing is draining the queue yet. Call once at boot, before
// multicore_launch_core1() brings up the Core 1/HSTX consumer; never call
// this once Core 1 is running (see audio_i2s_feed_queue()'s cap for why).
void audio_i2s_prefill_queue(uint32_t target_level);

// No physical amp/DAC in this design - always a no-op. Kept so
// sound_effects.c's port 3 bit 5 (AMP-enable) wiring has somewhere to write
// without needing a compile-time flag.
void audio_i2s_set_mute(bool mute);

// Starts sound_id playing from the beginning on the next free voice
// (stealing the least-recently-started of AUDIO_MAX_VOICES if all are
// busy - see audio_i2s.c). A sound_id whose file wasn't supplied at build
// time (sound_table[id].frame_count == 0 - see sounds/README.md) is
// silently a no-op. Only ever touches a few words under a brief interrupt-
// disable, so it's safe to call from Core 0's hot loop - see Hardware.md's
// timing budget note.
void audio_i2s_play_sound(sound_id_t sound_id);

// Starts/stops looping playback of sound_id. Only one sound can loop at a
// time (the real cabinet only ever loops the UFO hum, hence a single loop
// voice rather than one per AUDIO_MAX_VOICES slot). active=true (re)starts
// the loop from the beginning, unless sound_id is already the one looping -
// then it's a no-op, so repeated writes while the trigger bit stays set
// don't restart the waveform mid-play. active=false stops immediately
// (goes silent, not "let it finish").
void audio_i2s_set_sound_loop(sound_id_t sound_id, bool active);

#if DEBUG_AUDIO_TEST_TONE
// Hardware bring-up helper only (see display_config.h's
// DEBUG_AUDIO_TEST_TONE) - starts a continuous 1kHz test tone on a
// dedicated debug voice, entirely separate from the real one-shot/loop
// voices, so nothing the emulated machine's port 3/5 writes do can
// interrupt it. Call once, after audio_i2s_init(). Meant for confirming the
// HDMI embedded audio path (Data Island transmission, receiver decoding)
// works before trusting real sound-effect playback.
void audio_i2s_debug_play_test_tone(void);
void audio_i2s_debug_stop_test_tone(void);
#endif

#endif // AUDIO_I2S_H
