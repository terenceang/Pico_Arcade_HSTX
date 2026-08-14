#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "game.h"
#include "display_config.h"
#include "invaders_machine.h"
#include "sound_effects.h"
#include "snes_controller.h"

// ============================================================================
// Space Invaders arcade emulator: video output.
// ============================================================================

#define SI_ARCADE_WIDTH  256
#define SI_ARCADE_HEIGHT 224

#if SI_DISPLAY_ROTATION == 0 || SI_DISPLAY_ROTATION == 180
#define SI_CONTENT_W SI_ARCADE_WIDTH
#define SI_CONTENT_H SI_ARCADE_HEIGHT
#else
#define SI_CONTENT_W SI_ARCADE_HEIGHT
#define SI_CONTENT_H SI_ARCADE_WIDTH
#endif

#if SI_SCALE_MODE == SI_SCALE_FIT
    #if (FRAME_WIDTH * SI_CONTENT_H) < (FRAME_HEIGHT * SI_CONTENT_W)
        #define SI_DISPLAY_W FRAME_WIDTH
        #define SI_DISPLAY_H (SI_CONTENT_H * FRAME_WIDTH / SI_CONTENT_W)
    #else
        #define SI_DISPLAY_W (SI_CONTENT_W * FRAME_HEIGHT / SI_CONTENT_H)
        #define SI_DISPLAY_H FRAME_HEIGHT
    #endif
#elif SI_SCALE_MODE == SI_SCALE_X
    #define SI_DISPLAY_W FRAME_WIDTH
    #define SI_DISPLAY_H SI_CONTENT_H
#elif SI_SCALE_MODE == SI_SCALE_Y
    #define SI_DISPLAY_W SI_CONTENT_W
    #define SI_DISPLAY_H FRAME_HEIGHT
#else // SI_SCALE_NONE
    #define SI_DISPLAY_W SI_CONTENT_W
    #define SI_DISPLAY_H SI_CONTENT_H
#endif

#define SI_ACTIVE_X_OFFSET ((FRAME_WIDTH - SI_DISPLAY_W) / 2)
#if SI_DISPLAY_H > FRAME_HEIGHT
#define SI_ACTIVE_Y_CROP   ((SI_DISPLAY_H - FRAME_HEIGHT) / 2)
#define SI_ACTIVE_Y_OFFSET 0
#define SI_ACTIVE_Y_LIMIT  FRAME_HEIGHT
#else
#define SI_ACTIVE_Y_CROP   0
#define SI_ACTIVE_Y_OFFSET ((FRAME_HEIGHT - SI_DISPLAY_H) / 2)
#define SI_ACTIVE_Y_LIMIT  (SI_ACTIVE_Y_OFFSET + SI_DISPLAY_H)
#endif

#define SI_OVERLAY_RED_ROWS   32
#define SI_OVERLAY_GREEN_ROWS 40

#define SI_CPU_HZ            1996800
#define SI_CYCLES_PER_FRAME  (SI_CPU_HZ / DISPLAY_REFRESH_HZ)
#define SI_CYCLES_PER_ROW    (SI_CYCLES_PER_FRAME / FRAME_HEIGHT)
#define SI_MID_SCREEN_ROW    (FRAME_HEIGHT / 2)

static invaders_machine_t s_machine;
static bool s_mid_screen_fired;

static inline uint8_t lit_pixel_color(unsigned ox, unsigned active_width);

#if SI_DISPLAY_ROTATION == 0 || SI_DISPLAY_ROTATION == 180
static int s_clip_start;
static int s_clip_end;
static uint8_t s_row_byte_index[FRAME_WIDTH];
static uint8_t s_row_bit_mask[FRAME_WIDTH];
#if SI_ENABLE_COLOR_OVERLAY
static uint8_t s_row_lit_color[FRAME_WIDTH];
#endif

static void init_render_row_map(void) {
    int start_x = SI_SCREEN_OFFSET_X + SI_ACTIVE_X_OFFSET;
    int end_x = start_x + (int)SI_DISPLAY_W;

    s_clip_start = start_x < 0 ? 0 : (start_x > FRAME_WIDTH ? FRAME_WIDTH : start_x);
    s_clip_end = end_x < 0 ? 0 : (end_x > FRAME_WIDTH ? FRAME_WIDTH : end_x);

    if (s_clip_start >= s_clip_end)
        return;

    uint32_t step_x = ((uint32_t)SI_CONTENT_W << 16) / SI_DISPLAY_W;
    uint32_t ox_fp = (uint32_t)(s_clip_start - start_x) * step_x;

    for (int x = s_clip_start; x < s_clip_end; ++x) {
        unsigned ox = ox_fp >> 16;
        ox_fp += step_x;

#if SI_DISPLAY_ROTATION == 180
        unsigned lx = (SI_ARCADE_WIDTH - 1) - ox;
#else
        unsigned lx = ox;
#endif
#if SI_DISPLAY_FLIP_H
        lx = (SI_ARCADE_WIDTH - 1) - lx;
#endif

        s_row_byte_index[x] = (uint8_t)(lx >> 3);
        s_row_bit_mask[x] = (uint8_t)(1u << (lx & 7));
#if SI_ENABLE_COLOR_OVERLAY
        s_row_lit_color[x] = lit_pixel_color(ox, SI_ARCADE_WIDTH);
#endif
    }
}
#endif

static inline int sample_bit(const uint8_t *vram, unsigned lx, unsigned ly) {
    unsigned col = (SI_ARCADE_HEIGHT - 1) - ly;
    const uint8_t *column = vram + (size_t)col * 32;
    return (column[lx >> 3] >> (lx & 7)) & 1;
}

#if SI_ENABLE_COLOR_OVERLAY
static inline uint8_t overlay_color_for_screen_x(unsigned ox, unsigned active_width) {
    if (ox >= active_width - SI_OVERLAY_RED_ROWS)
        return COLOR_RED;
    if (ox < SI_OVERLAY_GREEN_ROWS)
        return COLOR_GREEN;
    return COLOR_WHITE;
}
#endif

static inline uint8_t lit_pixel_color(unsigned ox, unsigned active_width) {
#if SI_ENABLE_COLOR_OVERLAY
    return overlay_color_for_screen_x(ox, active_width);
#else
    (void)ox;
    (void)active_width;
    return COLOR_WHITE;
#endif
}

static inline void apply_mirror(unsigned *lx, unsigned *ly) {
#if SI_DISPLAY_FLIP_H
    *lx = (SI_ARCADE_WIDTH - 1) - *lx;
#endif
#if SI_DISPLAY_FLIP_V
    *ly = (SI_ARCADE_HEIGHT - 1) - *ly;
#endif
}

#if SI_DISPLAY_ROTATION == 0 || SI_DISPLAY_ROTATION == 180

static inline void render_arcade_row(uint8_t *buf, const uint8_t *vram, unsigned ay) {
    int screen_y = (int)ay - SI_SCREEN_OFFSET_Y;
    if (screen_y < (int)SI_ACTIVE_Y_OFFSET || screen_y >= (int)(SI_ACTIVE_Y_OFFSET + SI_DISPLAY_H)) {
        memset(buf, COLOR_BLACK, FRAME_WIDTH * sizeof(uint8_t));
        return;
    }
    unsigned display_y = (unsigned)screen_y - SI_ACTIVE_Y_OFFSET;
    unsigned oy = (display_y * SI_CONTENT_H) / SI_DISPLAY_H;

    if (s_clip_start > 0)
        memset(buf, COLOR_BLACK, (size_t)s_clip_start * sizeof(uint8_t));
    if (s_clip_end < FRAME_WIDTH)
        memset(buf + s_clip_end, COLOR_BLACK, (size_t)(FRAME_WIDTH - s_clip_end) * sizeof(uint8_t));

    if (s_clip_start >= s_clip_end)
        return;

#if SI_DISPLAY_ROTATION == 180
    unsigned ly = (SI_ARCADE_HEIGHT - 1) - oy;
#else
    unsigned ly = oy;
#endif
#if SI_DISPLAY_FLIP_V
    ly = (SI_ARCADE_HEIGHT - 1) - ly;
#endif
    unsigned col = (SI_ARCADE_HEIGHT - 1) - ly;
    const uint8_t *column = vram + (size_t)col * 32;

    for (int x = s_clip_start; x < s_clip_end; ++x) {
        int bit = (column[s_row_byte_index[x]] & s_row_bit_mask[x]) != 0;
#if SI_ENABLE_COLOR_OVERLAY
        buf[x] = bit ? s_row_lit_color[x] : COLOR_BLACK;
#else
        buf[x] = bit ? COLOR_WHITE : COLOR_BLACK;
#endif
    }
}

#else // SI_DISPLAY_ROTATION == 90 or 270

static inline void render_arcade_row(uint8_t *buf, const uint8_t *vram, unsigned ay) {
    int screen_ay = (int)ay - SI_SCREEN_OFFSET_Y;
    if (screen_ay < (int)SI_ACTIVE_Y_OFFSET || screen_ay >= (int)SI_ACTIVE_Y_LIMIT) {
        memset(buf, COLOR_BLACK, FRAME_WIDTH * sizeof(uint8_t));
        return;
    }
    unsigned display_ay = (unsigned)screen_ay - SI_ACTIVE_Y_OFFSET + SI_ACTIVE_Y_CROP;
    unsigned oy = (display_ay * SI_CONTENT_H) / SI_DISPLAY_H;

    int start_x = SI_SCREEN_OFFSET_X + SI_ACTIVE_X_OFFSET;
    int end_x = start_x + (int)SI_DISPLAY_W;
    int clip_start = start_x < 0 ? 0 : (start_x > FRAME_WIDTH ? FRAME_WIDTH : start_x);
    int clip_end = end_x < 0 ? 0 : (end_x > FRAME_WIDTH ? FRAME_WIDTH : end_x);

    if (clip_start > 0)
        memset(buf, COLOR_BLACK, (size_t)clip_start * sizeof(uint8_t));
    if (clip_end < FRAME_WIDTH)
        memset(buf + clip_end, COLOR_BLACK, (size_t)(FRAME_WIDTH - clip_end) * sizeof(uint8_t));

    if (clip_start >= clip_end)
        return;

    uint32_t step_x = ((uint32_t)SI_CONTENT_W << 16) / SI_DISPLAY_W;
    uint32_t ox_fp = (uint32_t)(clip_start - start_x) * step_x;

    for (int x = clip_start; x < clip_end; ++x) {
        unsigned ox = ox_fp >> 16;
        ox_fp += step_x;

#if SI_DISPLAY_ROTATION == 90
        unsigned lx = oy;
        unsigned ly = (SI_ARCADE_HEIGHT - 1) - ox;
#else // 270
        unsigned lx = (SI_ARCADE_WIDTH - 1) - oy;
        unsigned ly = ox;
#endif
        unsigned cur_lx = lx, cur_ly = ly;
        apply_mirror(&cur_lx, &cur_ly);

        buf[x] = sample_bit(vram, cur_lx, cur_ly) ? lit_pixel_color(ox, SI_ARCADE_HEIGHT) : COLOR_BLACK;
    }
}

#endif

void game_init(void) {
    invaders_machine_init(&s_machine);
    s_machine.sound_write = sound_effects_on_port_write;
    s_mid_screen_fired = false;
    snes_controller_init();
#if SI_DISPLAY_ROTATION == 0 || SI_DISPLAY_ROTATION == 180
    init_render_row_map();
#endif
}

void game_render_scanline(uint8_t *dst, unsigned y, unsigned frame_count) {
    (void)frame_count;

    if (y == 0) {
        uint16_t btns = snes_controller_read();
        invaders_machine_set_in1(&s_machine, SI_IN1_COIN,     (btns & SNES_BTN_SELECT) != 0);
        invaders_machine_set_in1(&s_machine, SI_IN1_P1_START, (btns & SNES_BTN_START)  != 0);
        invaders_machine_set_in1(&s_machine, SI_IN1_P1_LEFT,  (btns & SNES_BTN_LEFT)   != 0);
        invaders_machine_set_in1(&s_machine, SI_IN1_P1_RIGHT, (btns & SNES_BTN_RIGHT)  != 0);
        invaders_machine_set_in1(&s_machine, SI_IN1_P1_FIRE,  (btns & (SNES_BTN_A | SNES_BTN_B | SNES_BTN_Y | SNES_BTN_X)) != 0);

        invaders_machine_interrupt_vblank(&s_machine);
        s_mid_screen_fired = false;
    }

    invaders_machine_run_cycles(&s_machine, SI_CYCLES_PER_ROW);

    if (!s_mid_screen_fired && y >= SI_MID_SCREEN_ROW) {
        invaders_machine_interrupt_mid_screen(&s_machine);
        s_mid_screen_fired = true;
    }

    render_arcade_row(dst, invaders_machine_vram(&s_machine), y);
}
