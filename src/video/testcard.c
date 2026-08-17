#include <string.h>

#include "testcard.h"
#include "display_config.h"
#include "dvi_display.h"

static uint8_t scanline_bars[FRAME_WIDTH];
static uint8_t scanline_mid[FRAME_WIDTH];
static uint8_t scanline_bottom[FRAME_WIDTH];
static uint8_t scanline_anim[FRAME_WIDTH];

static const uint8_t ebu_colors[7] = {
    COLOR_WHITE,
    COLOR_YELLOW,
    COLOR_CYAN,
    COLOR_GREEN,
    COLOR_MAGENTA,
    COLOR_RED,
    COLOR_BLUE
};

static void generate_top_bars(uint8_t *buf, unsigned width) {
    unsigned bar_width = width / 7;
    for (unsigned x = 0; x < width; ++x) {
        unsigned bar_idx = x / bar_width;
        if (bar_idx > 6) bar_idx = 6;
        buf[x] = ebu_colors[bar_idx];
    }
}

static void generate_mid_bars(uint8_t *buf, unsigned width) {
    unsigned bar_width = width / 7;
    static const uint8_t mid_colors[7] = {
        COLOR_BLUE,   COLOR_BLACK, COLOR_MAGENTA, COLOR_BLACK,
        COLOR_CYAN,   COLOR_BLACK, COLOR_WHITE
    };
    for (unsigned x = 0; x < width; ++x) {
        unsigned bar_idx = x / bar_width;
        if (bar_idx > 6) bar_idx = 6;
        buf[x] = mid_colors[bar_idx];
    }
}

static void generate_bottom_section(uint8_t *buf, unsigned width) {
    for (unsigned x = 0; x < width; ++x) {
        if (x < width * 3 / 4) {
            unsigned step = (x * 32) / (width * 3 / 4);
            buf[x] = (step > 16) ? COLOR_WHITE : COLOR_BLACK;
        } else {
            unsigned sub_x = x - (width * 3 / 4);
            unsigned seg = sub_x / ((width / 4) / 3);
            if (seg == 0) buf[x] = COLOR_BLACK;
            else if (seg == 1) buf[x] = COLOR_BLUE;
            else buf[x] = COLOR_WHITE;
        }
    }
}

void testcard_load_palette(void) {
    dvi_display_set_palette(COLOR_BLACK,   0x000000);
    dvi_display_set_palette(COLOR_WHITE,   0xFFFFFF);
    dvi_display_set_palette(COLOR_YELLOW,  0xFFFF00);
    dvi_display_set_palette(COLOR_CYAN,    0x00FFFF);
    dvi_display_set_palette(COLOR_GREEN,   0x00FF00);
    dvi_display_set_palette(COLOR_MAGENTA, 0xFF00FF);
    dvi_display_set_palette(COLOR_RED,     0xFF0000);
    dvi_display_set_palette(COLOR_BLUE,    0x0000FF);
}

void testcard_init(void) {
    testcard_load_palette();
    generate_top_bars(scanline_bars, FRAME_WIDTH);
    generate_mid_bars(scanline_mid, FRAME_WIDTH);
    generate_bottom_section(scanline_bottom, FRAME_WIDTH);
}

// Row thresholds as fractions of FRAME_HEIGHT (originally tuned as literal
// row numbers 160/180/225/235 for a 240-line framebuffer - kept
// proportional so the pattern still fills the whole screen at other
// framebuffer heights, e.g. this project's native 480-line mode).
#define TESTCARD_TOP_BARS_END   (FRAME_HEIGHT * 160 / 240)
#define TESTCARD_MID_BARS_END   (FRAME_HEIGHT * 180 / 240)
#define TESTCARD_SYNC_BAR_START (FRAME_HEIGHT * 225 / 240)
#define TESTCARD_SYNC_BAR_END   (FRAME_HEIGHT * 235 / 240)

void testcard_render_scanline(uint8_t *dst, unsigned y, unsigned frame_count) {
    if (y < TESTCARD_TOP_BARS_END) {
        memcpy(dst, scanline_bars, FRAME_WIDTH);
    } else if (y < TESTCARD_MID_BARS_END) {
        memcpy(dst, scanline_mid, FRAME_WIDTH);
    } else if (y >= TESTCARD_SYNC_BAR_START && y < TESTCARD_SYNC_BAR_END) {
        unsigned anim_pos = frame_count % (FRAME_WIDTH - 20);
        memcpy(scanline_anim, scanline_bottom, sizeof(scanline_anim));
        for (unsigned x = anim_pos; x < anim_pos + 20; ++x) {
            scanline_anim[x] = COLOR_WHITE;
        }
        memcpy(dst, scanline_anim, FRAME_WIDTH);
    } else {
        memcpy(dst, scanline_bottom, FRAME_WIDTH);
    }
}

void testcard_render_frame(uint8_t *dst, uint32_t frame_count) {
    for (unsigned y = 0; y < FRAME_HEIGHT; ++y) {
        testcard_render_scanline(dst + y * FRAME_WIDTH, y, frame_count);
    }
}
