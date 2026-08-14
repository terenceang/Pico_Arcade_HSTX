#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "display_config.h"
#include "dvi_display.h"
#include "game.h"
#include "audio_i2s.h"
#if DEBUG_TESTCARD
#include "testcard.h"
#endif

static uint8_t fb[FRAME_WIDTH * FRAME_HEIGHT];

int main() {
    dvi_display_clock_init();
    stdio_init_all();

    printf("\n==================================================\n");
    printf("  Space Invader PICO  v%s\n", SPACE_INVADER_PICO_VERSION);
    printf("==================================================\n");
    printf("[DEBUG] Microcontroller: RP2350 (Cortex-M33)\n");

    dvi_display_init(); // prints the actual core voltage - see dvi_display.c's VREG_VSEL

#if DEBUG_TESTCARD
    testcard_init();
    printf("[DEBUG] DEBUG_TESTCARD enabled: test card for %d seconds, then the game.\n",
           DEBUG_TESTCARD_SECONDS);
#endif
    game_init();

    printf("[DEBUG] Initializing audio mixer...\n");
    audio_i2s_init();
    printf("[DEBUG] Audio mixer initialized.\n");
#if DEBUG_AUDIO_TEST_TONE
    audio_i2s_debug_play_test_tone();
    printf("[DEBUG] DEBUG_AUDIO_TEST_TONE enabled: playing continuous test tone.\n");
#endif

    printf("[DEBUG] Pre-filling audio Data Island queue...\n");
    audio_i2s_prefill_queue(AUDIO_QUEUE_TARGET_LEVEL);

    dvi_display_set_buffer(fb);

    printf("[DEBUG] Launching Core 1...\n");
    multicore_launch_core1(core1_main);
    sleep_ms(100);
    printf("[DEBUG] Core 1 launched.\n");

    printf("\n[STATUS] Rendering HSTX HDMI 640x480 @ %dHz (%dx%d 8bpp palettized + HDMI Audio)...\n",
           DISPLAY_REFRESH_HZ, FRAME_WIDTH, FRAME_HEIGHT);

    uint32_t frame_count = 0;
    absolute_time_t start = get_absolute_time();

#if DEBUG_TESTCARD
    const uint32_t testcard_frames = (uint32_t)DEBUG_TESTCARD_SECONDS * DISPLAY_REFRESH_HZ;
#endif

    while (true) {
#if DEBUG_TESTCARD
        bool show_testcard = (DEBUG_TESTCARD_SECONDS == 0) || (frame_count < testcard_frames);
#else
        bool show_testcard = false;
#endif
#if DEBUG_AUDIO_TEST_TONE && DEBUG_TESTCARD
        // Tone only accompanies the colour-bar test card's audio/video sanity
        // check - stop it as soon as that card isn't what's showing, so it
        // doesn't keep playing under the game.
        // (With DEBUG_TESTCARD off, there's no card to accompany - the tone
        // just plays continuously the whole time, e.g. for verifying the
        // HDMI audio queue stays glitch-free under real gameplay.)
        static bool test_tone_stopped = false;
        if (!show_testcard && !test_tone_stopped) {
            audio_i2s_debug_stop_test_tone();
            test_tone_stopped = true;
        }
#endif
        for (unsigned y = 0; y < FRAME_HEIGHT; ++y) {
            // Keep the HDMI audio Data Island queue continuously topped up
            // across the frame rather than front-loading it once per frame -
            // consumption is ~200 packets/frame (48kHz / 4 samples per
            // packet / 60Hz), so a once-per-frame call cannot keep up.
            // Bounded by audio_i2s_feed_queue()'s own per-call cap, so this
            // stays many small top-ups rather than the large recovery burst
            // that previously triggered the HSTX sync-loss failure mode
            // (see CLAUDE.md's "HSTX sync-loss caveat").
            if ((y & 7) == 0) {
                audio_i2s_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);
            }
            uint8_t *dst = fb + y * FRAME_WIDTH;
#if DEBUG_TESTCARD
            if (show_testcard) {
                testcard_render_scanline(dst, y, frame_count);
                continue;
            }
#endif
            game_render_scanline(dst, y, frame_count);
        }

        // Convert the freshly-rendered 8bpp frame to pre-packed RGB565 on
        // Core 0 (ample time budget) - Core 1's ISR then just returns a
        // pointer, with no per-pixel palette lookups on that critical path.
        dvi_display_convert_frame();

        // Video's pacing is this deadline alone, computed the same way
        // regardless of what audio does below - the frame cadence is
        // entirely wall-clock/HSTX-hardware driven, never audio-driven.
        uint64_t target_us = ((uint64_t)frame_count + 1) * 1000000ull / DISPLAY_REFRESH_HZ;
        absolute_time_t deadline = delayed_by_us(start, target_us);

        // Rendering + conversion above finishes in a few ms, well inside the
        // 16.67ms frame budget - a single sleep_until() here would leave the
        // rest of the frame's idle tail completely unfed while Core 1 keeps
        // draining the audio queue in real time, which is what was causing
        // audio to starve/drop out after a few frames. Keep polling and
        // topping the queue up in small steps until the same deadline
        // instead, so audio stays fed for the whole frame period without
        // changing the frame's timing by a single microsecond.
        while (!time_reached(deadline)) {
            audio_i2s_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);
            sleep_us(250);
        }

        ++frame_count;
    }
}
