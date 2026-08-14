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
#if DEBUG_HDMI_STATUS_OVERLAY
#include "debug_overlay.h"
#endif

// Whether to show the debug test card this frame instead of the game -
// always false when DEBUG_TESTCARD is off, so the caller doesn't need its
// own #if.
static inline bool testcard_active_this_frame(uint32_t frame_count) {
#if DEBUG_TESTCARD
    static const uint32_t testcard_frames = (uint32_t)DEBUG_TESTCARD_SECONDS * DISPLAY_REFRESH_HZ;
    return (DEBUG_TESTCARD_SECONDS == 0) || (frame_count < testcard_frames);
#else
    (void)frame_count;
    return false;
#endif
}

#if DEBUG_AUDIO_TEST_TONE && DEBUG_TESTCARD
// The debug tone only accompanies the colour-bar test card's audio/video
// sanity check - stop it as soon as that card isn't what's showing, so it
// doesn't keep playing under the game. (With DEBUG_TESTCARD off, this is
// never called - see its only call site - so the tone just plays
// continuously the whole time, e.g. for verifying the HDMI audio queue
// stays glitch-free under real gameplay.)
static void stop_test_tone_once_testcard_ends(bool show_testcard) {
    static bool stopped = false;
    if (!show_testcard && !stopped) {
        audio_i2s_debug_stop_test_tone();
        stopped = true;
    }
}
#endif

// Renders every scanline of one frame into frame_buf (game content, or the
// debug test card if active), keeping the HDMI audio Data Island queue fed
// throughout. Audio consumption is ~200 packets/frame (48kHz / 4 samples
// per packet / 60Hz) spread across the whole frame in real time, so feeding
// it only once per frame (rather than periodically through the render loop)
// cannot keep up - see audio_i2s_feed_queue()'s own doc comment. Bounded by
// that function's per-call cap, so this stays many small top-ups rather
// than the large recovery burst that previously triggered the HSTX
// sync-loss failure mode (see CLAUDE.md's "HSTX sync-loss caveat").
static void render_frame(uint8_t *frame_buf, uint32_t frame_count, bool show_testcard) {
    for (unsigned y = 0; y < FRAME_HEIGHT; ++y) {
        if ((y & 7) == 0) {
            audio_i2s_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);
        }

        uint8_t *dst = frame_buf + y * FRAME_WIDTH;
#if DEBUG_TESTCARD
        if (show_testcard) {
            testcard_render_scanline(dst, y, frame_count);
            continue;
        }
#else
        (void)show_testcard;
#endif
        game_render_scanline(dst, y, frame_count);
    }
}

// Blocks until frame_count's frame deadline (start + (frame_count+1) frame
// periods - a fixed, wall-clock-anchored schedule, entirely independent of
// how long rendering took), continuing to feed the audio queue in small
// steps the whole time instead of one blocking sleep_until(). Rendering
// above typically finishes in a few ms, well inside the 16.67ms frame
// budget, so most of this function's time is this idle tail - a single
// sleep_until() here would leave the audio queue completely unfed for that
// whole stretch while Core 1 keeps draining it in real time, which is what
// was previously causing audio to starve/drop out after a few frames.
static void pace_frame_and_feed_audio(absolute_time_t start, uint32_t frame_count) {
    uint64_t target_us = ((uint64_t)frame_count + 1) * 1000000ull / DISPLAY_REFRESH_HZ;
    absolute_time_t deadline = delayed_by_us(start, target_us);
    while (!time_reached(deadline)) {
        audio_i2s_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);
        sleep_us(250);
    }
}

// Diagnostic only, no corrective action: once per ~1s of software frames,
// measures pico_hdmi's real vsync rate (video_frame_count) against Core 0's
// own wall-clock-paced frame_count. See CLAUDE.md's "HSTX sync-loss caveat".
static void update_hdmi_fps_diagnostic(uint32_t frame_count, uint32_t *last_check_frame, uint32_t *last_vfc,
                                        uint32_t *measured_fps) {
    if (frame_count - *last_check_frame < DISPLAY_REFRESH_HZ)
        return;

    uint32_t vfc = dvi_display_get_video_frame_count();
    *measured_fps = vfc - *last_vfc;
    printf("[DIAG] video_frame_count advanced %lu in %lu software frames (expect ~%d)\n",
           (unsigned long)*measured_fps, (unsigned long)(frame_count - *last_check_frame), DISPLAY_REFRESH_HZ);
    *last_vfc = vfc;
    *last_check_frame = frame_count;
}

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

    printf("[DEBUG] Launching Core 1...\n");
    multicore_launch_core1(core1_main);
    sleep_ms(100);
    printf("[DEBUG] Core 1 launched.\n");

    printf("\n[STATUS] Rendering HSTX HDMI 640x480 @ %dHz (%dx%d 8bpp palettized + HDMI Audio)...\n",
           DISPLAY_REFRESH_HZ, FRAME_WIDTH, FRAME_HEIGHT);

    uint32_t frame_count = 0;
    uint32_t last_vfc_check_frame = 0;
    uint32_t last_vfc = dvi_display_get_video_frame_count();
    uint32_t last_measured_hdmi_fps = DISPLAY_REFRESH_HZ;
    absolute_time_t start = get_absolute_time();

    while (true) {
        bool show_testcard = testcard_active_this_frame(frame_count);
#if DEBUG_AUDIO_TEST_TONE && DEBUG_TESTCARD
        stop_test_tone_once_testcard_ends(show_testcard);
#endif

        uint8_t *frame_buf = dvi_display_get_write_buffer();
        render_frame(frame_buf, frame_count, show_testcard);
#if DEBUG_HDMI_STATUS_OVERLAY
        debug_overlay_draw_hdmi_status(frame_buf, last_measured_hdmi_fps);
#endif

        // Publish this frame to Core 1 and reclaim the other buffer - see
        // dvi_display.c's double-buffering scheme (dvi_display_present_frame()).
        dvi_display_present_frame();

        // Video's pacing is this call alone, computed the same way
        // regardless of what audio does inside it - the frame cadence is
        // entirely wall-clock/HSTX-hardware driven, never audio-driven.
        pace_frame_and_feed_audio(start, frame_count);

        update_hdmi_fps_diagnostic(frame_count, &last_vfc_check_frame, &last_vfc, &last_measured_hdmi_fps);

        ++frame_count;
    }
}
