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
#if DEBUG_CONTROLLER_TESTCARD
#include "controller_testcard.h"
#endif

static uint8_t fb[FRAME_WIDTH * FRAME_HEIGHT];

int main() {
    dvi_display_clock_init();
    stdio_init_all();

    printf("\n==================================================\n");
    printf("  Space Invader PICO  v%s\n", SPACE_INVADER_PICO_VERSION);
    printf("==================================================\n");
    printf("[DEBUG] Microcontroller: RP2350 (Cortex-M33)\n");
    printf("[DEBUG] Core Voltage   : 1.25V\n");

    dvi_display_init();

#if DEBUG_TESTCARD
    testcard_init();
    printf("[DEBUG] DEBUG_TESTCARD enabled: test card for %d seconds, then the game.\n",
           DEBUG_TESTCARD_SECONDS);
#endif
#if DEBUG_CONTROLLER_TESTCARD
    controller_testcard_init();
    printf("[DEBUG] DEBUG_CONTROLLER_TESTCARD enabled: showing controller test card instead of the game.\n");
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
    for (int i = 0; i < 4; ++i) {
        audio_i2s_step_frame();
    }

    dvi_display_set_buffer(fb);

    printf("[DEBUG] Launching Core 1...\n");
    multicore_launch_core1(core1_main);
    sleep_ms(100);
    printf("[DEBUG] Core 1 launched.\n");

    printf("\n[STATUS] Rendering HSTX HDMI 640x480 @ 60Hz (320x240 8bpp palettized + HDMI Audio)...\n");

    const uint64_t CHUNK_US = (uint64_t)AUDIO_FRAMES_PER_VIDEO_FRAME * 1000000ull / AUDIO_SAMPLE_RATE;

    uint32_t frame_count = 0;
    uint64_t chunks_pushed = 0;
    absolute_time_t start = get_absolute_time();

#if DEBUG_TESTCARD
    const uint32_t testcard_frames = (uint32_t)DEBUG_TESTCARD_SECONDS * DISPLAY_REFRESH_HZ;
#endif

    while (true) {
        audio_i2s_step_frame();
        ++chunks_pushed;

#if DEBUG_TESTCARD
        bool show_testcard = (DEBUG_TESTCARD_SECONDS == 0) || (frame_count < testcard_frames);
#else
        bool show_testcard = false;
#endif
#if DEBUG_AUDIO_TEST_TONE
        // Tone only accompanies the colour-bar test card's audio/video sanity
        // check - stop it as soon as that card isn't what's showing, so it
        // doesn't keep playing under the controller test card or the game.
        static bool test_tone_stopped = false;
        if (!show_testcard && !test_tone_stopped) {
            audio_i2s_debug_stop_test_tone();
            test_tone_stopped = true;
        }
#endif
        for (unsigned y = 0; y < FRAME_HEIGHT; ++y) {
            uint8_t *dst = fb + y * FRAME_WIDTH;
#if DEBUG_TESTCARD
            if (show_testcard) {
                testcard_render_scanline(dst, y, frame_count);
                continue;
            }
#endif
#if DEBUG_CONTROLLER_TESTCARD
            controller_testcard_render_scanline(dst, y, frame_count);
#else
            game_render_scanline(dst, y, frame_count);
#endif
        }

        sleep_until(delayed_by_us(start, chunks_pushed * CHUNK_US));

        ++frame_count;
    }
}
