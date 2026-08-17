#include <string.h>
#include <stdbool.h>

#include "controller_testcard.h"
#include "display_config.h"
#include "platform/input/snes_controller.h"

// Layout: SNES-style diagram drawn from flat-shaded boxes/circles (there's no
// font renderer in this codebase - see testcard.c for the same block-graphics
// approach). Wiring and button mapping can be checked visually without a
// serial console: D-pad + shoulders + Select/Start light up white->green when
// held; the four round face buttons are outlined in their real Super Famicom
// colors (Y=green, X=blue, A=red, B=yellow) and fill solid when held.
typedef struct {
    uint16_t mask;
    int x0, x1, y0, y1;
} button_box_t;

static const button_box_t s_boxes[] = {
    // D-pad cross (left cluster)
    { SNES_BTN_UP,     62,  98,  60,  96  },
    { SNES_BTN_LEFT,   26,  62,  96,  132 },
    { SNES_BTN_RIGHT,  98,  134, 96,  132 },
    { SNES_BTN_DOWN,   62,  98,  132, 168 },
    // Shoulder buttons
    { SNES_BTN_L,      10,  150, 15,  40  },
    { SNES_BTN_R,      170, 310, 15,  40  },
    // Select / Start
    { SNES_BTN_SELECT, 110, 150, 190, 215 },
    { SNES_BTN_START,  170, 210, 190, 215 },
};
#define NUM_BOXES (sizeof(s_boxes) / sizeof(s_boxes[0]))
#define BOX_BORDER 3

// Face-button diamond (right cluster, standard SNES Y/X/A/B positions and colors)
typedef struct {
    uint16_t mask;
    int cx, cy, radius;
    uint8_t color;
} round_button_t;

static const round_button_t s_round_buttons[] = {
    { SNES_BTN_X, 240, 78,  18, COLOR_BLUE   },
    { SNES_BTN_Y, 204, 114, 18, COLOR_GREEN  },
    { SNES_BTN_A, 276, 114, 18, COLOR_RED    },
    { SNES_BTN_B, 240, 150, 18, COLOR_YELLOW },
};
#define NUM_ROUND_BUTTONS (sizeof(s_round_buttons) / sizeof(s_round_buttons[0]))
#define ROUND_BORDER 3

static void draw_box(uint8_t *dst, const button_box_t *b, bool pressed) {
    for (int y = b->y0; y < b->y1; ++y) {
        bool row_is_border = (y < b->y0 + BOX_BORDER) || (y >= b->y1 - BOX_BORDER);
        uint8_t *row = dst + (unsigned)y * FRAME_WIDTH;

        for (int x = b->x0; x < b->x1; ++x) {
            if (pressed) {
                row[x] = COLOR_GREEN;
            } else if (row_is_border || x < b->x0 + BOX_BORDER || x >= b->x1 - BOX_BORDER) {
                row[x] = COLOR_WHITE;
            }
            // else: unpressed interior - leave as COLOR_BLACK from the memset below.
        }
    }
}

static void draw_round_button(uint8_t *dst, const round_button_t *b, bool pressed) {
    int r2 = b->radius * b->radius;
    int inner_r = b->radius - ROUND_BORDER;
    int inner_r2 = inner_r * inner_r;

    for (int y = b->cy - b->radius; y <= b->cy + b->radius; ++y) {
        int dy = y - b->cy;
        uint8_t *row = dst + (unsigned)y * FRAME_WIDTH;

        for (int x = b->cx - b->radius; x <= b->cx + b->radius; ++x) {
            int dx = x - b->cx;
            int dist2 = dx * dx + dy * dy;
            if (dist2 > r2) {
                continue; // outside the circle - leave as COLOR_BLACK background
            }
            if (pressed || dist2 > inner_r2) {
                // Filled solid when held; otherwise just the colored ring outline.
                row[x] = b->color;
            }
            // else: unpressed interior - leave as COLOR_BLACK from the memset below.
        }
    }
}

void controller_testcard_render_frame(uint8_t *dst, uint32_t frame_count) {
    (void)frame_count;

    uint16_t buttons = snes_controller_get_raw_buttons();

    memset(dst, COLOR_BLACK, FRAME_WIDTH * FRAME_HEIGHT);

    for (unsigned i = 0; i < NUM_BOXES; ++i) {
        const button_box_t *b = &s_boxes[i];
        draw_box(dst, b, (buttons & b->mask) != 0);
    }

    for (unsigned i = 0; i < NUM_ROUND_BUTTONS; ++i) {
        const round_button_t *b = &s_round_buttons[i];
        draw_round_button(dst, b, (buttons & b->mask) != 0);
    }
}
