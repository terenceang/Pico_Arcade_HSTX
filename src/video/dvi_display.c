#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/sync.h"

#include "dvi_display.h"
#include "display_config.h"
#include "platform/audio/audio_engine.h"

#include "pico_hdmi/video_output.h"
#include "pico_hdmi/video_output_precomposed.h"
#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_pins.h"

static void on_vsync_event(void) {
    __asm volatile("sev");
}

// Precomposed active-line template ring buffer (PICO_HDMI_PRECOMPOSED_ACTIVE_LINES).
// Builds static HDMI headers once at boot so the ISR does not dynamically rebuild them.
static video_output_precomposed_line_t compose_ring[16];

// RP2350 power-on default (VREG_VOLTAGE_DEFAULT) - matches lib/pico_hdmi's
// own 480p reference example (examples/bouncing_box), which never raises
// voltage for this mode. This project previously ran at 1.20V for no
// documented reason; higher voltage means faster edges, which can mean more
// ringing/overshoot on marginal wiring rather than better margin - testing
// parity with the proven reference config.
#define VREG_VSEL       VREG_VOLTAGE_1_10
#define VREG_VSEL_STR   "1.10V" // Keep in sync with VREG_VSEL above - debug print only
#define DVI_SYS_CLK_KHZ 126000 // 5x 25.2 MHz pixel clock = exact 60.000 Hz standard CEA timing (1.00x native arcade speed)

// Tried placing this in scratch Y RAM alongside pico_hdmi's own line_buffer
// (PICO_HDMI_LINE_BUFFER_IN_SCRATCH_Y, set in CMakeLists.txt) too, since
// it's read on every pixel by Core 1's ISR - but scratch Y is genuinely
// tiny (it also holds Core 1's own stack by default) and doesn't have room
// for both; the linker overflowed by 256 bytes. Left in regular SRAM.
// Space Invaders uses palette indices 0..7 (display_config.h). Placing this
// 16-entry table (64 bytes) in Scratch X guarantees Core 1 NEVER contends with
// Core 0 on the shared SRAM bus for palette lookups during the time-critical
// H-blank ISR window. Full 256-entry palette_packed is retained in SRAM for
// API completeness.
static uint32_t palette_packed[256];
static uint32_t palette_rgb888[256];
static uint32_t __scratch_x("palette_fast") palette_fast[16];

// Double-buffered 8bpp framebuffer (~76.8KB each - a pre-converted RGB565
// double buffer would be ~300KB each, and two of those (~600KB) don't fit
// in the RP2350's 520KB SRAM). Core 0 only ever writes into fb_buffers[the
// index NOT currently exposed to Core 1]; dvi_display_present_frame()
// atomically flips which index Core 1 reads, once per frame, so Core 1
// never observes a buffer Core 0 is concurrently writing.
static uint8_t fb_buffers[2][FRAME_WIDTH * FRAME_HEIGHT] __attribute__((aligned(4)));
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
// lookup (320 lookups, scaled to RGB565 packed pairs).
//
// Optimizations:
// 1. Reads 4 pixels at a time via aligned 32-bit word loads (80 bus cycles
//    instead of 320 individual byte cycles, reducing shared SRAM traffic by 75%).
// 2. Looks up colors from palette_fast in Scratch X RAM (zero bus contention).
// 3. Reuses dst on odd scanlines since dst (in Scratch Y line_buffer) is untouched.
static void __scratch_x("") dvi_scanline_fill_cb(uint32_t v_scanline, uint32_t active_line, uint32_t *dst) {
    (void)v_scanline;

    if (active_line & 1)
        return; // second physical line of this logical row - dst is already correct

    const uint32_t *src32 = (const uint32_t *)(fb_buffers[s_front_idx] + (size_t)(active_line >> 1) * FRAME_WIDTH);
    const uint32_t *pal = palette_fast;

    for (unsigned i = 0; i < FRAME_WIDTH / 4; ++i) {
        uint32_t s4 = src32[i];
        dst[i * 4 + 0] = pal[s4 & 0x0F];
        dst[i * 4 + 1] = pal[(s4 >> 8) & 0x0F];
        dst[i * 4 + 2] = pal[(s4 >> 16) & 0x0F];
        dst[i * 4 + 3] = pal[(s4 >> 24) & 0x0F];
    }
}

void dvi_display_clock_init(void) {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(20);
    set_sys_clock_khz(DVI_SYS_CLK_KHZ, true);
}

void dvi_display_set_palette(uint8_t index, uint32_t rgb888) {
    palette_rgb888[index] = rgb888;
    uint16_t r5 = (rgb888 >> 19) & 0x1F;
    uint16_t g6 = (rgb888 >> 10) & 0x3F;
    uint16_t b5 = (rgb888 >> 3) & 0x1F;
    uint16_t color16 = (r5 << 11) | (g6 << 5) | b5;
    uint32_t packed = (uint32_t)color16 | ((uint32_t)color16 << 16);
    palette_packed[index] = packed;
    if (index < 16) {
        palette_fast[index] = packed;
    }
}

uint32_t dvi_display_get_palette_entry(uint8_t index) {
    return palette_rgb888[index];
}

void dvi_display_init(void) {
    // DVI Sock HSTX Pinout Configuration:
    // Configured by default for the Adafruit DVI Sock / Breakout board.
    // (If using DIY resistor boards, Pimoroni DV Base, or custom wiring, adjust below).
    //   GP12 -> D0+, GP13 -> D0- (Blue)
    //   GP14 -> CK+, GP15 -> CK- (Clock)
    //   GP16 -> D2+, GP17 -> D2- (Red)
    //   GP18 -> D1+, GP19 -> D1- (Green)
    static const pico_hdmi_hstx_pinout_t user_pinout = {
        .clock = { .positive_gpio = 14, .negative_gpio = 15 },
        .data = {
            [0] = { .positive_gpio = 12, .negative_gpio = 13 }, // D0 (Blue) on GP12/13
            [1] = { .positive_gpio = 18, .negative_gpio = 19 }, // D1 (Green) on GP18/19
            [2] = { .positive_gpio = 16, .negative_gpio = 17 }  // D2 (Red) on GP16/17
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
    video_output_set_compose_ring(compose_ring, count_of(compose_ring));
    video_output_compose_service();
    video_output_set_scanline_callback(dvi_scanline_fill_cb);
    video_output_set_vsync_callback(on_vsync_event);
}

uint32_t dvi_display_wait_for_vsync(uint32_t last_vfc) {
    uint32_t vfc;
    while ((vfc = video_frame_count) == last_vfc) {
        __asm volatile("wfe"); // Low-power standby: zero bus traffic while waiting for VSYNC event
    }
    return vfc;
}

// Raw pico_hdmi vsync counter
uint32_t dvi_display_get_video_frame_count(void) {
    return video_frame_count;
}

void core1_main(void) {
    video_output_core1_run();
}
