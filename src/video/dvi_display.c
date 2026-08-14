#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include "dvi_display.h"
#include "display_config.h"
#include "audio_i2s.h"

#include "pico_hdmi/video_output.h"
#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_pins.h"

// RP2350 power-on default (VREG_VOLTAGE_DEFAULT) - matches lib/pico_hdmi's
// own 480p reference example (examples/bouncing_box), which never raises
// voltage for this mode. This project previously ran at 1.20V for no
// documented reason; higher voltage means faster edges, which can mean more
// ringing/overshoot on marginal wiring rather than better margin - testing
// parity with the proven reference config.
#define VREG_VSEL       VREG_VOLTAGE_1_10
#define VREG_VSEL_STR   "1.10V" // Keep in sync with VREG_VSEL above - debug print only
#define DVI_SYS_CLK_KHZ 126000

static uint16_t palette_rgb565[256];
static uint32_t palette_packed[256];

// Double-buffered 8bpp framebuffer (~76.8KB each - a pre-converted RGB565
// double buffer would be ~300KB each, and two of those (~600KB) don't fit
// in the RP2350's 520KB SRAM). Core 0 only ever writes into fb_buffers[the
// index NOT currently exposed to Core 1]; dvi_display_present_frame()
// atomically flips which index Core 1 reads, once per frame, so Core 1
// never observes a buffer Core 0 is concurrently writing. This replaces an
// earlier design (single un-double-buffered pre-converted RGB565 array,
// written by Core 0 and read by Core 1's DMA with no synchronization at
// all) that had a genuine, per-frame data race - independently confirmed as
// the leading suspect for this project's HDMI sync-loss bug after reading
// pico_hdmi's own reference example (examples/bouncing_box), which never
// shares a buffer across cores like this at all - see CLAUDE.md's "HSTX
// sync-loss caveat".
static uint8_t fb_buffers[2][FRAME_WIDTH * FRAME_HEIGHT];
static volatile int s_front_idx = 0; // buffer index Core 1's ISR reads from
static int s_write_idx = 1;          // buffer index Core 0 is filling this frame (Core 0-only state)

uint8_t *dvi_display_get_write_buffer(void) {
    return fb_buffers[s_write_idx];
}

void dvi_display_present_frame(void) {
    // Publish the just-filled buffer to Core 1 and reclaim the buffer Core 1
    // was reading (safe the instant the flip below becomes visible, since
    // dvi_scanline_fill_cb() re-reads s_front_idx fresh on every scanline -
    // it will never touch the old buffer again after this point).
    __compiler_memory_barrier(); // buffer writes must be visible before the flip publishes them
    int new_front = s_write_idx;
    s_write_idx = s_front_idx;
    s_front_idx = new_front;
}

// Runs on Core 1, inside the time-critical scanline ISR - per-line palette
// lookup (320 lookups, not the 640 that blew HDMI mode's per-line budget in
// an earlier, native-640-wide-framebuffer design; see display_config.h's
// FRAME_WIDTH/HEIGHT comment) plus the same horizontal-doubling packing the
// old Core-0-side conversion pass used to do. Matches pico_hdmi's own
// reference example's synchronous fill-callback pattern (examples/
// bouncing_box) rather than handing Core 1 a raw pointer into cross-core
// shared memory. active_line ranges 0..MODE_V_ACTIVE_LINES-1 (0..479); >>1
// always lands in 0..FRAME_HEIGHT-1 (0..239), so no bounds check is needed.
static void __scratch_x("") dvi_scanline_fill_cb(uint32_t v_scanline, uint32_t active_line, uint32_t *dst) {
    (void)v_scanline;

    const uint8_t *src = fb_buffers[s_front_idx] + (size_t)(active_line >> 1) * FRAME_WIDTH;
    const uint32_t *pal = palette_packed;

    unsigned i = 0;
    for (; i + 3 < FRAME_WIDTH; i += 4) {
        dst[i + 0] = pal[src[i + 0]];
        dst[i + 1] = pal[src[i + 1]];
        dst[i + 2] = pal[src[i + 2]];
        dst[i + 3] = pal[src[i + 3]];
    }
    for (; i < FRAME_WIDTH; ++i) {
        dst[i] = pal[src[i]];
    }
}

void dvi_display_clock_init(void) {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(20);
    set_sys_clock_khz(DVI_SYS_CLK_KHZ, true);
}

void dvi_display_set_palette(uint8_t index, uint32_t rgb888) {
    uint16_t r5 = (rgb888 >> 19) & 0x1F;
    uint16_t g6 = (rgb888 >> 10) & 0x3F;
    uint16_t b5 = (rgb888 >> 3) & 0x1F;
    uint16_t color16 = (r5 << 11) | (g6 << 5) | b5;
    palette_rgb565[index] = color16;
    palette_packed[index] = (uint32_t)color16 | ((uint32_t)color16 << 16);
}

void dvi_display_init(void) {
    uint32_t actual_clk = clock_get_hz(clk_sys);
    printf("[DEBUG] Core Voltage   : %s\n", VREG_VSEL_STR);
    printf("[DEBUG] System Clock   : %lu Hz (Requested: %u kHz)\n", actual_clk, DVI_SYS_CLK_KHZ);
    printf("[DEBUG] --- HSTX HDMI Pinout Configuration (User Specific Wiring) ---\n");
    printf("[DEBUG] D0: 12(P)/13(N), CK: 14(P)/15(N), D2: 16(P)/17(N), D1: 18(P)/19(N)\n");

    // Exact User Specified Wiring Pinout:
    // GP12 to D0+, GP13 to D0-
    // GP14 to CK+, GP15 to CK-
    // GP16 to D2+, GP17 to D2-
    // GP18 to D1+, GP19 to D1-
    static const pico_hdmi_hstx_pinout_t user_pinout = {
        .clock = { .positive_gpio = 14, .negative_gpio = 15 },
        .data = {
            [0] = { .positive_gpio = 12, .negative_gpio = 13 }, // D0+ on GP12, D0- on GP13
            [1] = { .positive_gpio = 18, .negative_gpio = 19 }, // D1+ on GP18, D1- on GP19
            [2] = { .positive_gpio = 16, .negative_gpio = 17 }  // D2+ on GP16, D2- on GP17
        }
    };
    video_output_set_hstx_pinout(&user_pinout);

    dvi_display_set_palette(COLOR_BLACK,   0x000000);
    dvi_display_set_palette(COLOR_WHITE,   0xFFFFFF);
    dvi_display_set_palette(COLOR_YELLOW,  0xFFFF00);
    dvi_display_set_palette(COLOR_CYAN,    0x00FFFF);
    dvi_display_set_palette(COLOR_GREEN,   0x00FF00);
    dvi_display_set_palette(COLOR_MAGENTA, 0xFF00FF);
    dvi_display_set_palette(COLOR_RED,     0xFF0000);
    dvi_display_set_palette(COLOR_BLUE,    0x0000FF);

    hstx_di_queue_init();
    video_output_init(FRAME_WIDTH, FRAME_HEIGHT);
    video_output_set_dvi_mode(false); // HDMI mode: Data Islands + audio enabled
    pico_hdmi_set_audio_sample_rate(AUDIO_SAMPLE_RATE);
    video_output_set_scanline_callback(dvi_scanline_fill_cb);

    printf("[DEBUG] pico_hdmi HSTX driver initialized successfully.\n");
}

// ============================================================================
// HDMI sync-loss watchdog
// ============================================================================
//
// pico_hdmi's own video_output_force_resync() doc comment (video_output.c)
// describes a failure mode this project has independently reproduced: a
// single corrupted/mis-sized HSTX command word desyncs the TMDS expander
// permanently - the sink loses lock while scanlines "complete" at bus speed
// because the FIFO no longer back-pressures the DMA engine. That symptom
// (video_frame_count advancing far faster than the real 60Hz refresh, never
// recovering on its own) matches this project's "HDMI sync-loss" bug
// exactly - see CLAUDE.md's "HSTX sync-loss caveat" for the investigation
// history. pico_hdmi ships video_output_force_resync() as the recovery
// path, but nothing was ever calling it - once desynced, this project just
// stayed desynced forever.
//
// This watchdog runs as pico_hdmi's Core 1 background task (registered
// below), self-contained on Core 1: it compares video_frame_count's actual
// advance against wall-clock time every ~0.5s and force-resyncs if it's
// running far faster than the real refresh rate should allow. It does NOT
// fix why the desync happens in the first place (still unknown) - it only
// makes the symptom transient/self-healing instead of permanent.
// video_output_force_resync() is documented as safe to call from Core 1
// thread context specifically (it touches Core 1-owned DMA/HSTX state) -
// do not move this check to Core 0.
#define HDMI_WATCHDOG_CHECK_US 500000 // check every ~0.5s
// Desync runs at ~137.8Hz (~2.3x nominal 60Hz - see CLAUDE.md). Trigger well
// above ordinary jitter but well below that rate: 1.5x the expected count
// for the elapsed window, plus a small flat margin for short windows.
#define HDMI_WATCHDOG_RATIO_NUM 3
#define HDMI_WATCHDOG_RATIO_DEN 2
#define HDMI_WATCHDOG_SLACK_FRAMES 5

static void hdmi_sync_watchdog_task(void) {
    static absolute_time_t s_last_check;
    static uint32_t s_last_vfc;
    static bool s_started = false;

    if (!s_started) {
        s_last_check = get_absolute_time();
        s_last_vfc = video_frame_count;
        s_started = true;
        return;
    }

    int64_t elapsed_us = absolute_time_diff_us(s_last_check, get_absolute_time());
    if (elapsed_us < HDMI_WATCHDOG_CHECK_US)
        return;

    uint32_t vfc = video_frame_count;
    uint32_t delta = vfc - s_last_vfc;
    s_last_check = get_absolute_time();
    s_last_vfc = vfc;

    uint32_t expected = (uint32_t)((elapsed_us * DISPLAY_REFRESH_HZ) / 1000000);
    uint32_t threshold = (expected * HDMI_WATCHDOG_RATIO_NUM) / HDMI_WATCHDOG_RATIO_DEN + HDMI_WATCHDOG_SLACK_FRAMES;

    if (delta > threshold) {
        video_output_force_resync();
        s_last_vfc = video_frame_count; // re-baseline post-resync
    }
}

uint32_t dvi_display_get_hdmi_resync_count(void) {
    return video_output_resync_count;
}

void core1_main(void) {
    video_output_set_background_task(hdmi_sync_watchdog_task);
    video_output_core1_run();
}
