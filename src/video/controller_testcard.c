#include <stdbool.h>
#include <string.h>

#include "controller_testcard.h"
#include "display_config.h"
#include "snes_controller.h"

// Layout: SNES-style diagram drawn from flat-shaded boxes (there's no font
// renderer in this codebase - see testcard.c for the same block-graphics
// approach). Each box lights up green while its button is held, so wiring
// and button mapping can be checked visually without a serial console.
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
    // Face-button diamond (right cluster, standard SNES Y/X/A/B positions)
    { SNES_BTN_X,      222, 258, 60,  96  },
    { SNES_BTN_Y,      186, 222, 96,  132 },
    { SNES_BTN_A,      258, 294, 96,  132 },
    { SNES_BTN_B,      222, 258, 132, 168 },
    // Shoulder buttons
    { SNES_BTN_L,      10,  150, 15,  40  },
    { SNES_BTN_R,      170, 310, 15,  40  },
    // Select / Start
    { SNES_BTN_SELECT, 110, 150, 190, 215 },
    { SNES_BTN_START,  170, 210, 190, 215 },
};
#define NUM_BOXES (sizeof(s_boxes) / sizeof(s_boxes[0]))
#define BOX_BORDER 3

static uint16_t s_buttons = 0;

void controller_testcard_init(void) {
    snes_controller_init();
}

void controller_testcard_render_scanline(uint8_t *dst, unsigned y, unsigned frame_count) {
    (void)frame_count;

    if (y == 0) {
        s_buttons = snes_controller_read();
    }

    memset(dst, COLOR_BLACK, FRAME_WIDTH);

    for (unsigned i = 0; i < NUM_BOXES; ++i) {
        const button_box_t *b = &s_boxes[i];
        if ((int)y < b->y0 || (int)y >= b->y1)
            continue;

        bool pressed = (s_buttons & b->mask) != 0;
        bool row_is_border = ((int)y < b->y0 + BOX_BORDER) || ((int)y >= b->y1 - BOX_BORDER);

        for (int x = b->x0; x < b->x1; ++x) {
            if (pressed) {
                dst[x] = COLOR_GREEN;
            } else if (row_is_border || x < b->x0 + BOX_BORDER || x >= b->x1 - BOX_BORDER) {
                dst[x] = COLOR_WHITE;
            }
            // else: unpressed interior - leave as COLOR_BLACK from the memset above.
        }
    }
}
