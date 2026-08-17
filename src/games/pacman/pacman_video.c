#include "pacman_video.h"
#include "dvi_display.h"
#include "display_config.h"
#include "pacman_config.h"
#include <string.h>

static const uint8_t *s_palette_prom = NULL;
static const uint8_t *s_color_prom = NULL;
static const uint8_t *s_tile_rom = NULL;
static const uint8_t *s_sprite_rom = NULL;

// Precomputed 2bpp pixel decodes (built once at init by pacman_video_build_pixel_tables()).
// Packed 4 pixels/byte to keep SRAM use low (256 tiles + 64 sprites = 8KB instead
// of 32KB unpacked, since the RP2350's main SRAM is nearly fully allocated).
// s_tile_pixels is indexed [tile * 16 + px_index / 4], s_sprite_pixels [sprite * 64 + px_index / 4].
static uint8_t s_tile_pixels[256 * 16];
static uint8_t s_sprite_pixels[64 * 64];

static void pacman_video_build_pixel_tables(void);

static inline uint8_t pacman_tile_px(uint8_t tile, int px, int py) {
    uint16_t idx = (uint16_t)(py * 8 + px);
    return (s_tile_pixels[tile * 16 + (idx >> 2)] >> ((idx & 3) << 1)) & 3;
}

static inline uint8_t pacman_sprite_px(uint8_t sprite, int px, int py) {
    uint16_t idx = (uint16_t)(py * 16 + px);
    return (s_sprite_pixels[sprite * 64 + (idx >> 2)] >> ((idx & 3) << 1)) & 3;
}

// 32 authentic arcade palette colors (RGB888)
static uint32_t s_palette_rgb[32];

// Authentic Pac-Man 32-color RGB values (bipolar PROM 82S123.7F standard decode)
static const uint32_t s_default_palette_rgb[32] = {
    0x000000, 0xFF0000, 0xFFB8DE, 0x00FFFF, 0xFFB847, 0xFFFF00, 0xDEDEFF, 0x2121DE,
    0xDE9751, 0x47B8FF, 0xAE0000, 0x009700, 0x00FF00, 0x974700, 0xDE47AE, 0xFFFFFF,
    0x000000, 0xFF0000, 0xFFB8DE, 0x00FFFF, 0xFFB847, 0xFFFF00, 0xDEDEFF, 0x2121DE,
    0xDE9751, 0x47B8FF, 0xAE0000, 0x009700, 0x00FF00, 0x974700, 0xDE47AE, 0xFFFFFF
};

// Default color lookup table (82S126.4A colormap standard decode: 64 palettes x 4 colors).
// Palette 9 - which the real ROM code always uses for the Pac-Man player sprite,
// confirmed by running the real ROMs through this exact decoder - is hand-corrected
// below: the general repeating-pattern guess mapped its pixel-value-3 (the value
// Pac-Man's actual sprite data uses almost throughout his animation) to color 0
// (black), rendering him as an invisible black silhouette instead of yellow. Every
// other palette keeps the original repeating-pattern placeholder unchanged.
static const uint8_t s_default_color_prom[256] = {
    0,  7,  6,  0,   0,  5,  9,  7,   0,  1,  9,  7,   0,  2,  9,  7, // palette 1 (text + ghost1-frightened body): raw2/3 blue like the other ghosts
    0,  3,  9,  7,   0,  4,  9,  7,   0,  8,  9,  1,   0,  9,  1,  7,
    0,  7,  6,  0,   0,  5,  5,  5,   0,  1,  9,  7,   0,  2,  9,  7, // palette 9 (Pac-Man): raw2 1-3 all yellow
    0,  3,  9,  7,   0,  4,  9,  7,   0,  8,  9,  1,   0,  9,  1,  7,
    0,  15, 6,  6,   0,  5,  0,  0,   0,  1,  9,  7,   0,  2,  9,  7, // palette 16 (dots+walls): raw1=1->white, raw2/3=6->lavender (was black gaps)
    0,  3,  9,  7,   0,  4,  9,  7,   0,  8,  9,  1,   0,  9,  1,  7,
    0,  7,  6,  0,   0,  5,  0,  0,   0,  1,  9,  7,   0,  2,  9,  7,
    0,  3,  9,  7,   0,  4,  9,  7,   0,  8,  9,  1,   0,  9,  1,  7,
    0,  7,  6,  0,   0,  5,  0,  0,   0,  1,  9,  7,   0,  2,  9,  7,
    0,  3,  9,  7,   0,  4,  9,  7,   0,  8,  9,  1,   0,  9,  1,  7,
    0,  7,  6,  0,   0,  5,  0,  0,   0,  1,  9,  7,   0,  2,  9,  7,
    0,  3,  9,  7,   0,  4,  9,  7,   0,  8,  9,  1,   0,  9,  1,  7,
    0,  7,  6,  0,   0,  5,  0,  0,   0,  1,  9,  7,   0,  2,  9,  7,
    0,  7,  6,  0,   0,  5,  0,  0,   0,  1,  9,  7,   0,  2,  9,  7,
    0,  7,  6,  0,   0,  5,  0,  0,   0,  1,  9,  7,   0,  2,  9,  7,
    0,  7,  6,  0,   0,  5,  0,  0,   0,  1,  9,  7,   0,  2,  9,  7
};

void pacman_video_load_palette(void) {
    for (int i = 0; i < 32; i++) {
        dvi_display_set_palette((uint8_t)i, s_palette_rgb[i]);
    }
}

void pacman_video_init(const uint8_t *palette_prom, const uint8_t *color_prom,
                       const uint8_t *tile_rom, const uint8_t *sprite_rom) {
    s_tile_rom = tile_rom;
    s_sprite_rom = sprite_rom;

    bool has_palette = false;
    if (palette_prom) {
        for (int i = 0; i < 32; i++) {
            if (palette_prom[i] != 0) {
                has_palette = true;
                break;
            }
        }
    }

    if (has_palette) {
        s_palette_prom = palette_prom;
        for (int i = 0; i < 32; i++) {
            uint8_t d = palette_prom[i];
            int r = ((d >> 0) & 1) * 0x21 + ((d >> 1) & 1) * 0x47 + ((d >> 2) & 1) * 0x97;
            int g = ((d >> 3) & 1) * 0x21 + ((d >> 4) & 1) * 0x47 + ((d >> 5) & 1) * 0x97;
            int b = ((d >> 6) & 1) * 0x51 + ((d >> 7) & 1) * 0xAE;
            s_palette_rgb[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    } else {
        s_palette_prom = NULL;
        for (int i = 0; i < 32; i++) {
            s_palette_rgb[i] = s_default_palette_rgb[i];
        }
    }

    bool has_color = false;
    if (color_prom) {
        for (int i = 0; i < 256; i++) {
            if (color_prom[i] != 0) {
                has_color = true;
                break;
            }
        }
    }
    s_color_prom = has_color ? color_prom : s_default_color_prom;
    pacman_video_build_pixel_tables();
}

// Decode 2bpp 8x8 tile pixel at (px, py) from authentic 16-byte Pac-Man tile ROM
static inline uint8_t get_tile_pixel(const uint8_t *rom, uint8_t tile_idx, int px, int py) {
    if (!rom) return 0;
    const uint8_t *tile_data = &rom[tile_idx * 16];
    // This ROM family stores tiles column-major: the 8 columns run right-to-left
    // (byte index 7-px), split into two 4-row halves (rows 0-3 in bytes 8-15,
    // rows 4-7 in bytes 0-7). Each byte packs 4 rows of 2bpp - plane 0 in bits
    // 0-3, plane 1 in bits 4-7, rows 3..0 within the nibble.
    int byte = (7 - px) + (py < 4 ? 8 : 0);
    int bit = 3 - (py & 3);
    uint8_t b = tile_data[byte];
    uint8_t bit0 = (b >> bit) & 1;
    uint8_t bit1 = (b >> (bit + 4)) & 1;
    return (bit1 << 1) | bit0;
}

// Decode 2bpp 16x16 sprite pixel at (px, py) from authentic 64-byte Pac-Man sprite ROM.
// In Pac-Man arcade hardware, each 64-byte sprite defines 16 scanlines constructed from
// 8-byte planar row pairs indexed by bit, scanned right-to-left into hardware shift registers.
static inline uint8_t get_sprite_pixel(const uint8_t *rom, uint8_t sprite_idx, int px, int py) {
    if (!rom) return 0;
    static const struct {
        uint8_t rl;
        uint8_t rr;
        uint8_t bit;
    } s_scanlines[16] = {
        {0, 4, 1}, {1, 5, 3}, {1, 5, 2}, {1, 5, 1},
        {1, 5, 0}, {2, 6, 3}, {2, 6, 2}, {2, 6, 1},
        {2, 6, 0}, {3, 7, 3}, {3, 7, 2}, {3, 7, 1},
        {3, 7, 0}, {0, 4, 3}, {0, 4, 2}, {0, 4, 0}
    };
    const uint8_t *sdata = &rom[sprite_idx * 64];
    uint8_t rl = s_scanlines[py].rl;
    uint8_t rr = s_scanlines[py].rr;
    uint8_t bit = s_scanlines[py].bit;
    uint8_t byte = (px < 8) ? sdata[rr * 8 + (7 - px)] : sdata[rl * 8 + (15 - px)];
    uint8_t bit0 = (byte >> bit) & 1;
    uint8_t bit1 = (byte >> (bit + 4)) & 1;
    return (bit1 << 1) | bit0;
}

// Decode all 256 tiles and 64 sprites into raw 2bpp pixel tables once at init,
// so pacman_video_render_frame() is pure table lookups instead of ~70k branchy
// decode calls per frame.
static void pacman_video_build_pixel_tables(void) {
    memset(s_tile_pixels, 0, sizeof(s_tile_pixels));
    memset(s_sprite_pixels, 0, sizeof(s_sprite_pixels));

    for (int t = 0; t < 256; t++) {
        for (int py = 0; py < 8; py++) {
            for (int px = 0; px < 8; px++) {
                uint16_t idx = (uint16_t)(py * 8 + px);
                uint8_t val = get_tile_pixel(s_tile_rom, (uint8_t)t, px, py);
                s_tile_pixels[t * 16 + (idx >> 2)] |= (uint8_t)(val << ((idx & 3) << 1));
            }
        }
    }
    for (int s = 0; s < 64; s++) {
        for (int py = 0; py < 16; py++) {
            for (int px = 0; px < 16; px++) {
                uint16_t idx = (uint16_t)(py * 16 + px);
                uint8_t val = get_sprite_pixel(s_sprite_rom, (uint8_t)s, px, py);
                s_sprite_pixels[s * 64 + (idx >> 2)] |= (uint8_t)(val << ((idx & 3) << 1));
            }
        }
    }
}

// Maps native 224x288 arcade coordinate (nx, ny) to 320x240 framebuffer (dx, dy)
static inline bool pacman_map_coords(int nx, int ny, int *out_dx, int *out_dy) {
#if PACMAN_DISPLAY_FLIP_H
    nx = 223 - nx;
#endif
#if PACMAN_DISPLAY_FLIP_V
    ny = 287 - ny;
#endif

    int dx, dy;
#if PACMAN_DISPLAY_ROTATION == 0
    // 0° = Upright Arcade Monitor: native portrait 224x288 shown upright (tunnels
    // left/right, score on top), pillarboxed horizontally; vertically cropped to
    // 240 rows. Joystick directions map 1:1 to on-screen movement.
    dx = 48 + nx + PACMAN_SCREEN_OFFSET_X;
    dy = (ny - 8) + PACMAN_SCREEN_OFFSET_Y;
#elif PACMAN_DISPLAY_ROTATION == 90
    // 90° = Rotated to fill a landscape monitor: full 288x224 unclipped across 320x240
    dx = 16 + (287 - ny) + PACMAN_SCREEN_OFFSET_X;
    dy = 8 + nx + PACMAN_SCREEN_OFFSET_Y;
#elif PACMAN_DISPLAY_ROTATION == 180
    // 180° = Inverted Upright Arcade Monitor (180° flip of 0°)
    dx = 48 + (223 - nx) + PACMAN_SCREEN_OFFSET_X;
    dy = (279 - ny) + PACMAN_SCREEN_OFFSET_Y;
#elif PACMAN_DISPLAY_ROTATION == 270
    // 270° = Inverted Landscape (180° flip of 90°)
    dx = 16 + ny + PACMAN_SCREEN_OFFSET_X;
    dy = 8 + (223 - nx) + PACMAN_SCREEN_OFFSET_Y;
#else
    dx = 48 + nx;
    dy = (ny - 8);
#endif

    if (dx < 0 || dx >= FRAME_WIDTH || dy < 0 || dy >= FRAME_HEIGHT) {
        return false;
    }
    *out_dx = dx;
    *out_dy = dy;
    return true;
}

void pacman_video_render_frame(const pacman_machine_t *m, uint8_t *fb) {
    // Clear 320x240 framebuffer to black
    memset(fb, 0, FRAME_WIDTH * FRAME_HEIGHT);

    const uint8_t *vram = pacman_machine_get_vram(m);
    const uint8_t *cram = pacman_machine_get_color_ram(m);
    bool flip_screen = m->flip_screen;

    // 1. Draw Tilemap (28 columns x 36 rows)
    for (int r = 0; r < 36; r++) {
        for (int c = 0; c < 28; c++) {
            uint16_t tile_addr;
            if (!flip_screen) {
                if (r < 2) {
                    tile_addr = 0x3C0 + r * 32 + (c + 2);
                } else if (r >= 34) {
                    tile_addr = 0x000 + (r - 34) * 32 + (c + 2);
                } else {
                    tile_addr = 0x040 + c * 32 + (r - 2);
                }
            } else {
                int r_flip = 35 - r;
                int c_flip = 27 - c;
                if (r_flip < 2) {
                    tile_addr = 0x3C0 + r_flip * 32 + (c_flip + 2);
                } else if (r_flip >= 34) {
                    tile_addr = 0x000 + (r_flip - 34) * 32 + (c_flip + 2);
                } else {
                    tile_addr = 0x040 + c_flip * 32 + (r_flip - 2);
                }
            }

            uint8_t tile_idx = vram[tile_addr];
            uint8_t pal_idx = cram[tile_addr] & 0x1F; // MAME: color = colorram & 0x1f

            uint8_t cell_col[4];
            for (int i = 0; i < 4; i++) {
                cell_col[i] = s_color_prom[pal_idx * 4 + i] & 0x1F;
            }

#if PACMAN_DISPLAY_ROTATION == 270 && !PACMAN_DISPLAY_FLIP_H && !PACMAN_DISPLAY_FLIP_V
            int base_dx = 16 + r * 8;
            int base_dy = 8 + c * 8;
            for (int px = 0; px < 8; px++) {
                int src_px = flip_screen ? (7 - px) : px;
                uint8_t *dst_row = &fb[(base_dy + (7 - px)) * FRAME_WIDTH + base_dx];
                for (int py = 0; py < 8; py++) {
                    int src_py = flip_screen ? (7 - py) : py;
                    uint8_t raw2 = pacman_tile_px(tile_idx, src_px, src_py);
                    if (raw2 != 0) {
                        dst_row[py] = cell_col[raw2];
                    }
                }
            }
#else
            for (int py = 0; py < 8; py++) {
                for (int px = 0; px < 8; px++) {
                    int nx = (27 - c) * 8 + px;
                    int ny = r * 8 + py;

                    int dx, dy;
                    if (!pacman_map_coords(nx, ny, &dx, &dy)) continue;

                    int src_px = flip_screen ? (7 - px) : px;
                    int src_py = flip_screen ? (7 - py) : py;

                    uint8_t raw2 = pacman_tile_px(tile_idx, src_px, src_py);
                    if (raw2 != 0) {
                        fb[dy * FRAME_WIDTH + dx] = cell_col[raw2];
                    }
                }
            }
#endif
        }
    }

    // 2. Draw 8 Hardware Sprites (rendered in reverse order: sprite 7 down to 0)
    const uint8_t *coords = pacman_machine_get_sprite_coords(m);
    const uint8_t *attrs = pacman_machine_get_sprite_attrs(m);

    for (int s = 7; s >= 0; s--) {
        int base_x, base_y;
        if (!flip_screen) {
            base_x = 235 - coords[s * 2];
            base_y = 272 - coords[s * 2 + 1];
        } else {
            base_x = coords[s * 2] - 12;
            base_y = coords[s * 2 + 1];
        }

        if (base_x >= 224 || base_x <= -16 || base_y >= 288 || base_y <= -16) {
            continue;
        }

        uint8_t attr0 = attrs[s * 2];
        uint8_t attr1 = attrs[s * 2 + 1];

        uint8_t sprite_idx = attr0 >> 2;
        bool flip_y = (attr0 & 1) != 0;
        bool flip_x = (attr0 & 2) != 0;
        if (flip_screen) {
            flip_x = !flip_x;
            flip_y = !flip_y;
        }
        uint8_t pal_idx = attr1 & 0x1F;

        uint8_t cell_col[4];
        for (int i = 0; i < 4; i++) {
            cell_col[i] = s_color_prom[pal_idx * 4 + i] & 0x1F;
        }

        for (int py = 0; py < 16; py++) {
            int src_y = flip_y ? (15 - py) : py;

            for (int px = 0; px < 16; px++) {
                int src_x = flip_x ? (15 - px) : px;
                uint8_t raw2 = pacman_sprite_px(sprite_idx, src_x, src_y);
                if (raw2 != 0) { // Color 0 in sprite is transparent
                    int nx = base_x + px;
                    int ny = base_y + py;
#if PACMAN_DISPLAY_ROTATION == 270 && !PACMAN_DISPLAY_FLIP_H && !PACMAN_DISPLAY_FLIP_V
                    int dx = 16 + ny;
                    int dy = 8 + (223 - nx);
                    if (dx >= 0 && dx < FRAME_WIDTH && dy >= 0 && dy < FRAME_HEIGHT) {
                        fb[dy * FRAME_WIDTH + dx] = cell_col[raw2];
                    }
#else
                    int dx, dy;
                    if (pacman_map_coords(nx, ny, &dx, &dy)) {
                        fb[dy * FRAME_WIDTH + dx] = cell_col[raw2];
                    }
#endif
                }
            }
        }
    }
}
