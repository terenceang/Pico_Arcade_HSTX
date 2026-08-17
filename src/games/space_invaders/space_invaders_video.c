#include "space_invaders_video.h"
#include "space_invaders_config.h"
#include "video/display_config.h"
#include "video/dvi_display.h"
#include <string.h>

#define SI_ARCADE_WIDTH  256
#define SI_ARCADE_HEIGHT 224

// SI_ROTATED: true for the two rotations that swap which physical screen
// axis reads VRAM columns vs. bit-within-column (see the derivation below).
#if SI_DISPLAY_ROTATION == 90 || SI_DISPLAY_ROTATION == 270
#define SI_ROTATED 1
#else
#define SI_ROTATED 0
#endif

#if SI_ROTATED
#define SI_CONTENT_W SI_ARCADE_HEIGHT
#define SI_CONTENT_H SI_ARCADE_WIDTH
#else
#define SI_CONTENT_W SI_ARCADE_WIDTH
#define SI_CONTENT_H SI_ARCADE_HEIGHT
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

static int s_clip_start = 0;
static int s_clip_end = FRAME_WIDTH;
static uint8_t s_col_lit[FRAME_WIDTH]; // overlay tint per output column, both rotation modes

#if !SI_ROTATED
// !SI_ROTATED (0/180): per output column x, VRAM bit position within the
// row's single VRAM column depends only on x - precompute byte/bit here.
typedef struct {
    uint8_t byte_idx;
    uint8_t bit_mask;
} render_map_t;
static render_map_t s_row_map[FRAME_WIDTH];
#else
// SI_ROTATED (90/270): axis roles swap - per output column x, which VRAM
// *column* to read depends only on x (precompute its byte offset here);
// bit-within-column now depends only on y, computed once per row instead.
static uint16_t s_col_offset[FRAME_WIDTH];
#endif

static inline uint8_t lit_pixel_color(unsigned ox) {
#if SI_ENABLE_COLOR_OVERLAY
    if (ox < SI_OVERLAY_RED_ROWS)
        return COLOR_RED;
    if (ox >= (SI_CONTENT_W - SI_OVERLAY_GREEN_ROWS))
        return COLOR_GREEN;
#endif
    return COLOR_WHITE;
}

// Derivation: VRAM is a fixed physical layout (224 columns x 32 bytes/256
// bits each) that always un-rotates to a native 256x224 "landscape" image
// via the fixed relationship lx = cx, ly = (ARCADE_HEIGHT-1) - cy - this
// part never changes with SI_DISPLAY_ROTATION/FLIP. What SI_DISPLAY_ROTATION
// and the FLIP flags select is how that fixed native (cx, cy) space maps to
// the scaled/offset content coordinates (rx, ry) - here we invert that
// mapping, since we iterate destination pixels: (rx, ry) [= (ox, sy) below,
// already known from the output pixel via scaling] -> (cx, cy) -> (lx, ly).
//
// Flips are applied in final (post-rotation) space, matching how a real
// display's mirror setting works - flip the picture you actually see.
void space_invaders_video_init(void) {
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

        unsigned rx = SI_DISPLAY_FLIP_H ? (SI_CONTENT_W - 1 - ox) : ox;

#if !SI_ROTATED
        // cx depends only on rx (=x) here - precomputable per column.
    #if SI_DISPLAY_ROTATION == 180
        unsigned cx = (SI_ARCADE_WIDTH - 1) - rx;
    #else
        unsigned cx = rx;
    #endif
        unsigned lx = cx;
        s_row_map[x].byte_idx = (uint8_t)(lx >> 3);
        s_row_map[x].bit_mask = (uint8_t)(1u << (lx & 7));
#else
        // cy depends only on rx (=x) here - precomputable per column.
    #if SI_DISPLAY_ROTATION == 90
        unsigned cy = (SI_ARCADE_HEIGHT - 1) - rx;
    #else // 270
        unsigned cy = rx;
    #endif
        unsigned ly = (SI_ARCADE_HEIGHT - 1) - cy;
        s_col_offset[x] = (uint16_t)(ly * 32);
#endif
        s_col_lit[x] = lit_pixel_color(ox);
    }
}

void space_invaders_video_load_palette(void) {
    dvi_display_set_palette(COLOR_BLACK,   0x000000);
    dvi_display_set_palette(COLOR_WHITE,   0xFFFFFF);
    dvi_display_set_palette(COLOR_YELLOW,  0xFFFF00);
    dvi_display_set_palette(COLOR_CYAN,    0x00FFFF);
    dvi_display_set_palette(COLOR_GREEN,   0x00FF00);
    dvi_display_set_palette(COLOR_MAGENTA, 0xFF00FF);
    dvi_display_set_palette(COLOR_RED,     0xFF0000);
    dvi_display_set_palette(COLOR_BLUE,    0x0000FF);
}

static inline void render_arcade_row(uint8_t *dst, const uint8_t *vram, unsigned y) {
    int oy = ((int)y - SI_SCREEN_OFFSET_Y - (int)SI_ACTIVE_Y_OFFSET) + (int)SI_ACTIVE_Y_CROP;
    if (oy < 0 || oy >= (int)SI_DISPLAY_H) {
        memset(dst, COLOR_BLACK, FRAME_WIDTH);
        return;
    }

    uint32_t step_y = ((uint32_t)SI_CONTENT_H << 16) / SI_DISPLAY_H;
    unsigned sy = (uint32_t)oy * step_y >> 16;

    unsigned ry = SI_DISPLAY_FLIP_V ? (SI_CONTENT_H - 1 - sy) : sy;

    if (s_clip_start > 0)
        memset(dst, COLOR_BLACK, s_clip_start);

#if !SI_ROTATED
    // cy depends only on ry (=y) here - fixed for the whole row.
    #if SI_DISPLAY_ROTATION == 180
        unsigned cy = (SI_ARCADE_HEIGHT - 1) - ry;
    #else
        unsigned cy = ry;
    #endif
    unsigned ly = (SI_ARCADE_HEIGHT - 1) - cy;
    const uint8_t *col_base = vram + (ly * 32);

    for (int x = s_clip_start; x < s_clip_end; ++x) {
        uint8_t byte = col_base[s_row_map[x].byte_idx];
#if SI_ENABLE_COLOR_OVERLAY
        dst[x] = (byte & s_row_map[x].bit_mask) ? s_col_lit[x] : COLOR_BLACK;
#else
        dst[x] = (byte & s_row_map[x].bit_mask) ? COLOR_WHITE : COLOR_BLACK;
#endif
    }
#else
    // cx depends only on ry (=y) here - fixed for the whole row.
    #if SI_DISPLAY_ROTATION == 90
        unsigned cx = ry;
    #else // 270
        unsigned cx = (SI_ARCADE_WIDTH - 1) - ry;
    #endif
    unsigned lx = cx;
    uint8_t byte_idx = (uint8_t)(lx >> 3);
    uint8_t bit_mask = (uint8_t)(1u << (lx & 7));

    for (int x = s_clip_start; x < s_clip_end; ++x) {
        uint8_t byte = vram[s_col_offset[x] + byte_idx];
#if SI_ENABLE_COLOR_OVERLAY
        dst[x] = (byte & bit_mask) ? s_col_lit[x] : COLOR_BLACK;
#else
        dst[x] = (byte & bit_mask) ? COLOR_WHITE : COLOR_BLACK;
#endif
    }
#endif

    if (FRAME_WIDTH > s_clip_end)
        memset(dst + s_clip_end, COLOR_BLACK, FRAME_WIDTH - s_clip_end);
}

void space_invaders_video_render_frame(uint8_t *dst, const uint8_t *vram, uint32_t stride) {
    for (unsigned y = 0; y < FRAME_HEIGHT; ++y) {
        render_arcade_row(dst + y * stride, vram, y);
    }
}
