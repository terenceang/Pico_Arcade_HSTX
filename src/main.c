#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "main.h"
#include "video/display_config.h"
#include "video/dvi_display.h"
#include "core/plugin_registry.h"
#include "platform/audio/audio_engine.h"
#include "platform/input/host_input.h"
#include "video/screen_capture.h"

#if DEBUG_TESTCARD
#include "video/testcard.h"
#endif
#if DEBUG_CONTROLLER_TESTCARD
#include "video/controller_testcard.h"
#include "video/testcard.h" // reuse testcard_load_palette() for BLACK/WHITE/GREEN
#endif

static inline bool boot_sync_active(uint32_t frame_count) {
#if BOOT_SYNC_FRAMES > 0
    return frame_count < BOOT_SYNC_FRAMES;
#else
    (void)frame_count;
    return false;
#endif
}

static inline bool testcard_active_this_frame(uint32_t frame_count) {
#if DEBUG_TESTCARD
    static const uint32_t testcard_frames = (uint32_t)DEBUG_TESTCARD_SECONDS * DISPLAY_REFRESH_HZ;
    return (DEBUG_TESTCARD_SECONDS == 0) || (frame_count < testcard_frames);
#else
    (void)frame_count;
    return false;
#endif
}

// Shown permanently once (if) the color-bar test card's own window ends -
// mutually exclusive with it, never with the game.
static inline bool controller_testcard_active_this_frame(bool show_testcard) {
#if DEBUG_CONTROLLER_TESTCARD
    return !show_testcard;
#else
    (void)show_testcard;
    return false;
#endif
}

#if DEBUG_AUDIO_TEST_TONE && DEBUG_TESTCARD
static void stop_test_tone_once_testcard_ends(bool show_testcard) {
    static bool stopped = false;
    if (!show_testcard && !stopped) {
        audio_engine_debug_stop_test_tone();
        stopped = true;
    }
}
#endif

static void handle_testcard_transition(const emulator_plugin_t *plugin, bool show_testcard, bool show_controller_testcard) {
    static bool palette_loaded = false;
    if (show_testcard || palette_loaded) {
        return;
    }
#if DEBUG_CONTROLLER_TESTCARD
    if (show_controller_testcard) {
        testcard_load_palette();
        palette_loaded = true;
        return;
    }
#else
    (void)show_controller_testcard;
#endif
    if (plugin && plugin->load_palette) {
        plugin->load_palette();
    }
    palette_loaded = true;
}

static void render_frame(const emulator_plugin_t *plugin, uint8_t *frame_buf, uint32_t frame_count, bool show_testcard, bool show_controller_testcard, bool rom_valid) {
#if DEBUG_TESTCARD
    if (show_testcard) {
        testcard_render_frame(frame_buf, frame_count);
        return;
    }
#else
    (void)show_testcard;
#endif
#if DEBUG_CONTROLLER_TESTCARD
    if (show_controller_testcard) {
        controller_testcard_render_frame(frame_buf, frame_count);
        return;
    }
#else
    (void)show_controller_testcard;
#endif
    if (!rom_valid) {
        if (plugin && plugin->render_missing_rom) {
            plugin->render_missing_rom(frame_buf, frame_count);
        } else {
            memset(frame_buf, COLOR_BLACK, FRAME_WIDTH * FRAME_HEIGHT);
        }
    } else if (plugin && plugin->render_frame) {
        plugin->render_frame(frame_buf, FRAME_WIDTH);
    }
}

int main() {
    dvi_display_clock_init();
    stdio_init_all();
    dvi_display_init();

#if DEBUG_TESTCARD
    testcard_init();
#endif

    // Initialize Plugin Registry
    plugin_registry_init();
    const emulator_plugin_t *plugin = plugin_registry_get_active();
    if (plugin) {
        if (plugin->init) plugin->init();
#if !DEBUG_TESTCARD
        if (plugin->load_palette) plugin->load_palette();
#endif
    }

    audio_engine_init();
    host_input_init();
#if !DEBUG_AUDIO_TEST_TONE
    audio_engine_set_mute(true); // Silence audio during boot screen
#endif

#if DEBUG_AUDIO_TEST_TONE
    audio_engine_debug_play_test_tone();
#endif

    audio_engine_prefill_queue(AUDIO_QUEUE_TARGET_LEVEL);

    multicore_launch_core1(core1_main);
    sleep_ms(100);

    uint32_t frame_count = 0;
    uint32_t last_vfc = dvi_display_get_video_frame_count();
    bool rom_valid = plugin && plugin->is_rom_valid ? plugin->is_rom_valid() : true;

    while (true) {
        // 1. Wait for hardware VSYNC from Core 1 HSTX (exact 60.000 Hz genlock)
        last_vfc = dvi_display_wait_for_vsync(last_vfc);

        // 2. Publish completed buffer during V-blank and switch to write buffer
        dvi_display_present_frame();
        uint8_t *frame_buf = dvi_display_get_write_buffer();

        bool show_testcard = testcard_active_this_frame(frame_count);
        bool show_controller_testcard = controller_testcard_active_this_frame(show_testcard);
        bool syncing = !show_testcard && !show_controller_testcard && boot_sync_active(frame_count);

        // Mute audio during black boot sync, unmute when game (or the controller
        // test card) starts
        if (!show_testcard) {
            audio_engine_set_mute(syncing);
        }

        // 3. Immediately top up audio queue at VSYNC
        audio_engine_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);

        handle_testcard_transition(plugin, show_testcard, show_controller_testcard);

#if DEBUG_AUDIO_TEST_TONE && DEBUG_TESTCARD
        stop_test_tone_once_testcard_ends(show_testcard);
#endif
        // 4. Poll host platform inputs and trigger active game plugin
        uint32_t input_mask = host_input_poll();
        if (plugin && plugin->update_inputs) {
            plugin->update_inputs(input_mask);
        }

#if DEBUG_CONTROLLER_TESTCARD
        // Audible click feedback for the controller test card only - real
        // gameplay presses (fire, etc.) shouldn't beep over game audio.
        if (show_controller_testcard) {
            static uint32_t s_prev_input_mask = 0;
            uint32_t newly_pressed = input_mask & ~s_prev_input_mask;
            if (newly_pressed) {
                audio_engine_play_keypress_beep();
            }
            s_prev_input_mask = input_mask;
        }
#endif

#if DEBUG_FPS_MONITOR
        uint64_t t_frame_start = time_us_64();
#endif

        // 5. Run arcade emulation only after the boot sync period completes
        uint32_t emu_us = 0;
        if (!syncing && !show_testcard && !show_controller_testcard && rom_valid && plugin && plugin->run_frame) {
#if DEBUG_FPS_MONITOR
            uint64_t t0 = time_us_64();
            plugin->run_frame();
            emu_us = (uint32_t)(time_us_64() - t0);
#else
            plugin->run_frame();
#endif
        }

        // Top up audio queue before rendering
        audio_engine_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);

        // 5. Render arcade VRAM to 8bpp write buffer (or black during sync)
        uint32_t render_us = 0;
        if (syncing) {
            memset(frame_buf, COLOR_BLACK, FRAME_WIDTH * FRAME_HEIGHT);
        } else {
#if DEBUG_FPS_MONITOR
            uint64_t t0 = time_us_64();
            render_frame(plugin, frame_buf, frame_count, show_testcard, show_controller_testcard, rom_valid);
            render_us = (uint32_t)(time_us_64() - t0);
#else
            render_frame(plugin, frame_buf, frame_count, show_testcard, show_controller_testcard, rom_valid);
#endif
        }

        // 6. Keep audio Data Island queue topped up at end of frame
        audio_engine_feed_queue(AUDIO_QUEUE_TARGET_LEVEL);

        // 7. Optional screen capture hook (zero overhead when ENABLE_SCREEN_CAPTURE is 0)
        screen_capture_poll(frame_buf, input_mask);

#if DEBUG_FPS_MONITOR
        static uint64_t s_last_report_us = 0;
        static uint32_t s_period_frames = 0;
        static uint64_t s_accum_emu_us = 0;
        static uint64_t s_accum_render_us = 0;
        static uint64_t s_accum_work_us = 0;
        static uint32_t s_max_work_us = 0;

        uint32_t work_us = (uint32_t)(time_us_64() - t_frame_start);
        s_period_frames++;
        s_accum_emu_us += emu_us;
        s_accum_render_us += render_us;
        s_accum_work_us += work_us;
        if (work_us > s_max_work_us) s_max_work_us = work_us;

        uint64_t now_us = time_us_64();
        if (s_last_report_us == 0) s_last_report_us = now_us;
        if (now_us - s_last_report_us >= (uint64_t)DEBUG_FPS_INTERVAL_SECONDS * 1000000ULL) {
            float elapsed_sec = (float)(now_us - s_last_report_us) / 1000000.0f;
            float fps = (float)s_period_frames / elapsed_sec;
            uint32_t avg_emu = s_period_frames > 0 ? (uint32_t)(s_accum_emu_us / s_period_frames) : 0;
            uint32_t avg_render = s_period_frames > 0 ? (uint32_t)(s_accum_render_us / s_period_frames) : 0;
            uint32_t avg_work = s_period_frames > 0 ? (uint32_t)(s_accum_work_us / s_period_frames) : 0;
            float load_pct = (float)avg_work * 100.0f / 16666.67f;

            printf("[PERF] Game: %s | FPS: %5.2f | Core0 Work: %lu us (Emu: %lu us, Render: %lu us, Max: %lu us) | Load: %5.1f%%\n",
                   plugin ? plugin->name : "None", fps, (unsigned long)avg_work,
                   (unsigned long)avg_emu, (unsigned long)avg_render, (unsigned long)s_max_work_us, load_pct);
            fflush(stdout);

            s_last_report_us = now_us;
            s_period_frames = 0;
            s_accum_emu_us = 0;
            s_accum_render_us = 0;
            s_accum_work_us = 0;
            s_max_work_us = 0;
        }
#endif

        ++frame_count;
    }
}
