#include "debug_overlay.h"
#include "display_config.h"

// Minimal 3-wide x 5-tall bitmap font, digits 0-9 only - there's no general
// font renderer in this codebase (see testcard.c's same block-graphics
// approach). Each row's 3 low bits select which of the 3 columns are lit,
// MSB = leftmost column.
static const uint8_t digit_font[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, // 0
    {0x2, 0x6, 0x2, 0x2, 0x7}, // 1
    {0x7, 0x1, 0x7, 0x4, 0x7}, // 2
    {0x7, 0x1, 0x7, 0x1, 0x7}, // 3
    {0x5, 0x5, 0x7, 0x1, 0x1}, // 4
    {0x7, 0x4, 0x7, 0x1, 0x7}, // 5
    {0x7, 0x4, 0x7, 0x5, 0x7}, // 6
    {0x7, 0x1, 0x1, 0x1, 0x1}, // 7
    {0x7, 0x5, 0x7, 0x5, 0x7}, // 8
    {0x7, 0x5, 0x7, 0x1, 0x7}, // 9
};

#define FONT_W      3
#define FONT_H      5
#define GLYPH_SCALE 3
#define GLYPH_GAP   (GLYPH_SCALE) // horizontal gap between digits, in pixels

static void draw_digit(uint8_t *frame_buf, int x0, int y0, unsigned digit, uint8_t color) {
    if (digit > 9)
        return;
    for (int row = 0; row < FONT_H; ++row) {
        uint8_t bits = digit_font[digit][row];
        for (int col = 0; col < FONT_W; ++col) {
            if (!(bits & (1u << (FONT_W - 1 - col))))
                continue;
            int px0 = x0 + col * GLYPH_SCALE;
            int py0 = y0 + row * GLYPH_SCALE;
            for (int sy = 0; sy < GLYPH_SCALE; ++sy) {
                int py = py0 + sy;
                if (py < 0 || py >= (int)FRAME_HEIGHT)
                    continue;
                uint8_t *dst_row = frame_buf + (unsigned)py * FRAME_WIDTH;
                for (int sx = 0; sx < GLYPH_SCALE; ++sx) {
                    int px = px0 + sx;
                    if (px < 0 || px >= (int)FRAME_WIDTH)
                        continue;
                    dst_row[px] = color;
                }
            }
        }
    }
}

// Draws value's decimal digits starting at (x0, y0), returns the x
// coordinate just past the last digit drawn (for chaining further text).
static int draw_number(uint8_t *frame_buf, int x0, int y0, uint32_t value, uint8_t color) {
    char digits[10];
    int n = 0;
    if (value == 0) {
        digits[n++] = 0;
    } else {
        while (value > 0 && n < (int)sizeof(digits)) {
            digits[n++] = (char)(value % 10);
            value /= 10;
        }
    }
    int x = x0;
    for (int i = n - 1; i >= 0; --i) {
        draw_digit(frame_buf, x, y0, (unsigned)digits[i], color);
        x += FONT_W * GLYPH_SCALE + GLYPH_GAP;
    }
    return x;
}

void debug_overlay_draw_hdmi_status(uint8_t *frame_buf, uint32_t hdmi_fps) {
    // Solid background box for contrast against whatever game content is
    // underneath - this overlay draws after the game's own rendering.
    const int box_w = 40, box_h = FONT_H * GLYPH_SCALE + 4;
    for (int y = 0; y < box_h && y < (int)FRAME_HEIGHT; ++y) {
        uint8_t *row = frame_buf + (unsigned)y * FRAME_WIDTH;
        for (int x = 0; x < box_w && x < (int)FRAME_WIDTH; ++x) {
            row[x] = COLOR_BLACK;
        }
    }

    draw_number(frame_buf, 2, 2, hdmi_fps, COLOR_WHITE);
}
