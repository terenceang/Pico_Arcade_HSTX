#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include "dvi_display.h"
#include "display_config.h"

#include "pico_hdmi/video_output.h"
#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_pins.h"

#define VREG_VSEL       VREG_VOLTAGE_1_20
#define DVI_SYS_CLK_KHZ 126000

static uint8_t *display_fb = NULL;
static uint16_t palette_rgb565[256];

static void __scratch_x("") dvi_scanline_cb(uint32_t v_scanline, uint32_t active_line, uint32_t *dst) {
    (void)v_scanline;
    // Map 480 active lines to 240 framebuffer lines (2x vertical line doubling)
    uint32_t src_y = active_line >> 1;
    if (!display_fb || src_y >= FRAME_HEIGHT) {
        for (int i = 0; i < FRAME_WIDTH; ++i) {
            dst[i] = 0;
        }
        return;
    }
    const uint8_t *src = &display_fb[src_y * FRAME_WIDTH];
    // 2x horizontal pixel doubling: each 8-bit palettized pixel is expanded to two 16-bit RGB565 pixels
    for (int i = 0; i < FRAME_WIDTH; ++i) {
        uint32_t color = palette_rgb565[src[i]];
        dst[i] = color | (color << 16);
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
    palette_rgb565[index] = (r5 << 11) | (g6 << 5) | b5;
}

void dvi_display_set_buffer(uint8_t *buffer) {
    display_fb = buffer;
}

void dvi_display_init(void) {
    uint32_t actual_clk = clock_get_hz(clk_sys);
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
    video_output_set_dvi_mode(true);
    pico_hdmi_set_audio_sample_rate(32000);
    video_output_set_scanline_callback(dvi_scanline_cb);

    printf("[DEBUG] pico_hdmi HSTX driver initialized successfully.\n");
}

void core1_main(void) {
    video_output_core1_run();
}
