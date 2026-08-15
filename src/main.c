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
#if DEBUG_TESTCARD
    if (show_testcard) {
        testcard_render_frame(frame_buf, frame_count);
        return;
    }
#else
    (void)show_testcard;
    (void)frame_count;
#endif
    game_render_frame(frame_buf);
}

// Diagnostic only, no corrective action: measures pico_hdmi's real vsync rate
// against Core 0's frame counter.
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

    printf("[DEBUG] Initializing audio mixer (48 kHz)...\n");
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

    printf("\n[STATUS] Rendering HSTX HDMI 640x480 @ %dHz (%dx%d 8bpp palettized + HDMI Audio 48kHz)...\n",
           DISPLAY_REFRESH_HZ, FRAME_WIDTH, FRAME_HEIGHT);

    uint32_t frame_count = 0;
    uint32_t last_vfc_check_frame = 0;
    uint32_t last_vfc = dvi_display_get_video_frame_count();
    uint32_t last_measured_hdmi_fps = DISPLAY_REFRESH_HZ;

    while (true) {
        // 1. Wait for hardware VSYNC from Core 1 HSTX (exact 60.000 Hz genlock)
        last_vfc = dvi_display_wait_for_vsync(last_vfc);

        // 2. Publish completed buffer during V-blank and switch to write buffer
        dvi_display_present_frame();
        uint8_t *frame_buf = dvi_display_get_write_buffer();

        // 3. Run Space Invaders arcade emulation for 1 full frame (33,280 cycles)
        bool show_testcard = testcard_active_this_frame(frame_count);
#if DEBUG_AUDIO_TEST_TONE && DEBUG_TESTCARD
        stop_test_tone_once_testcard_ends(show_testcard);
#endif
        if (!show_testcard) {
            game_run_frame();
        }

        // 4. Render arcade VRAM to 8bpp write buffer
        render_frame(frame_buf, frame_count, show_testcard);
#if DEBUG_HDMI_STATUS_OVERLAY
        debug_overlay_draw_hdmi_status(frame_buf, last_measured_hdmi_fps);
#endif

        // 5. Keep audio Data Island queue topped up
        audio_i2s_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);

        // 6. Live FPS diagnostic
        update_hdmi_fps_diagnostic(frame_count, &last_vfc_check_frame, &last_vfc, &last_measured_hdmi_fps);

        ++frame_count;
    }
}
