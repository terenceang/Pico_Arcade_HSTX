#include <stdio.h>
#include <string.h>

#include "testcard.h"
#include "display_config.h"

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

void testcard_init(void) {
    printf("[DEBUG] Pre-rendering colorbar line buffers (8bpp)...\n");
    generate_top_bars(scanline_bars, FRAME_WIDTH);
    generate_mid_bars(scanline_mid, FRAME_WIDTH);
    generate_bottom_section(scanline_bottom, FRAME_WIDTH);
}

void testcard_render_scanline(uint8_t *dst, unsigned y, unsigned frame_count) {
    if (y < 160) {
        memcpy(dst, scanline_bars, FRAME_WIDTH);
    } else if (y < 180) {
        memcpy(dst, scanline_mid, FRAME_WIDTH);
    } else if (y >= 225 && y < 235) {
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
