#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define CHIPS_IMPL
#include "../src/emu/z80.h"

void audio_engine_feed_queue(uint32_t level) { (void)level; }

static uint32_t s_host_palette[256];
void dvi_display_set_palette(uint8_t index, uint32_t rgb888) {
    s_host_palette[index] = rgb888;
}

#include "../src/audio/namco_sound.h"
#include "../src/audio/namco_sound.c"
#include "../src/emu/pacman_machine.h"
#include "../src/emu/pacman_machine.c"
#include "../src/games/pacman/pacman_config.h"
#include "../src/games/pacman/pacman_video.h"
#include "../src/games/pacman/pacman_video.c"

#define SPACE_INVADERS_ROM_DATA_H
uint8_t space_invaders_rom[8192];
const size_t space_invaders_rom_size = 8192;
#include "../src/emu/space_invaders_machine.h"
#include "../src/emu/space_invaders_machine.c"
#include "../src/games/space_invaders/space_invaders_config.h"
#include "../src/games/space_invaders/space_invaders_video.h"
#include "../src/games/space_invaders/space_invaders_video.c"

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFileHeader;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

static void save_bmp_8bpp_palette(const char *filename, int width, int height, const uint8_t *fb, const uint32_t *palette) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    int row_stride = (width * 3 + 3) & ~3;
    uint32_t image_size = (uint32_t)row_stride * height;
    BMPFileHeader fh = { 0x4D42, 54 + image_size, 0, 0, 54 };
    BMPInfoHeader ih = { sizeof(BMPInfoHeader), width, -height, 1, 24, 0, image_size, 2835, 2835, 0, 0 };
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    uint8_t *row_buf = (uint8_t *)calloc(1, row_stride);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t rgb = palette[fb[y * width + x]];
            row_buf[x * 3 + 0] = rgb & 0xFF;
            row_buf[x * 3 + 1] = (rgb >> 8) & 0xFF;
            row_buf[x * 3 + 2] = (rgb >> 16) & 0xFF;
        }
        fwrite(row_buf, 1, row_stride, f);
    }
    free(row_buf);
    fclose(f);
}

// Render native 224x288 Pacman image directly (unrotated, unclipped portrait)
void render_pacman_native(const pacman_machine_t *m, const char *filename) {
    int w = 224;
    int h = 288;
    uint8_t *fb = (uint8_t *)calloc(w * h, 1);
    const uint8_t *vram = pacman_machine_get_vram(m);
    const uint8_t *cram = pacman_machine_get_color_ram(m);
    
    // Draw tiles 28 cols x 36 rows
    for (int r = 0; r < 36; r++) {
        for (int c = 0; c < 28; c++) {
            uint16_t tile_addr;
            if (r < 2) tile_addr = 0x3C0 + r * 32 + (c + 2);
            else if (r >= 34) tile_addr = 0x000 + (r - 34) * 32 + (c + 2);
            else tile_addr = 0x040 + c * 32 + (r - 2);

            uint8_t tile_idx = vram[tile_addr];
            uint8_t pal_idx = cram[tile_addr] & 0x1F;
            uint8_t cell_col[4];
            for (int i = 0; i < 4; i++) {
                cell_col[i] = s_color_prom[pal_idx * 4 + i] & 0x1F;
            }

            for (int py = 0; py < 8; py++) {
                for (int px = 0; px < 8; px++) {
                    int nx = (27 - c) * 8 + px;
                    int ny = r * 8 + py;
                    uint8_t raw2 = pacman_tile_px(tile_idx, px, py);
                    if (raw2 != 0 && nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        fb[ny * w + nx] = cell_col[raw2];
                    }
                }
            }
        }
    }

    // Draw sprites
    const uint8_t *coords = pacman_machine_get_sprite_coords(m);
    const uint8_t *attrs = pacman_machine_get_sprite_attrs(m);
    for (int s = 7; s >= 0; s--) {
        int base_x = 235 - coords[s * 2];
        int base_y = 272 - coords[s * 2 + 1];
        if (base_x >= w || base_x <= -16 || base_y >= h || base_y <= -16) continue;

        uint8_t attr0 = attrs[s * 2];
        uint8_t attr1 = attrs[s * 2 + 1];
        uint8_t sprite_idx = attr0 >> 2;
        bool flip_y = (attr0 & 1) != 0;
        bool flip_x = (attr0 & 2) != 0;
        uint8_t pal_idx = attr1 & 0x1F;
        uint8_t cell_col[4];
        for (int i = 0; i < 4; i++) {
            cell_col[i] = s_color_prom[pal_idx * 4 + i] & 0x1F;
        }

        for (int py = 0; py < 16; py++) {
            int src_y = flip_y ? (15 - py) : py;
            for (int px = 0; px < 16; px++) {
                int nx = base_x + px;
                int ny = base_y + py;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                int src_x = flip_x ? (15 - px) : px;
                uint8_t raw2 = pacman_sprite_px(sprite_idx, src_x, src_y);
                if (raw2 != 0) {
                    fb[ny * w + nx] = cell_col[raw2];
                }
            }
        }
    }

    save_bmp_8bpp_palette(filename, w, h, fb, s_host_palette);
    free(fb);
    printf("Saved native Pac-Man capture: %s (%dx%d)\n", filename, w, h);
}

// Render native 224x256 Space Invaders image directly (unrotated, unclipped portrait)
void render_si_native(const uint8_t *vram, const char *filename) {
    int w = 224;
    int h = 256;
    uint8_t *fb = (uint8_t *)calloc(w * h, 1);
    
    // In Space Invaders native portrait:
    // Screen is 224 columns wide (x = 0..223).
    // Each column has 256 pixels = 32 bytes (y = 0..255).
    // VRAM address = x * 32 + (y / 8).
    // Bit = (y % 8) or (7 - y%8).
    // Let's test standard arcade orientation:
    // In Midway 8080 hardware:
    // Bottom of screen is at low y or high y?
    // Low address column 0 is left, byte 0 is bottom (y=255 down to 0).
    for (int x = 0; x < w; x++) {
        const uint8_t *col = vram + (x * 32);
        for (int y = 0; y < h; y++) {
            // Pixel row 0 (top) is bit 7 of byte 31, or byte 0 bit 0?
            // Space Invaders scans bottom-to-top, left-to-right on CRT.
            // On CRT: y=0 is top, y=255 is bottom.
            // Address for (x, y): ly = 255 - y -> byte = ly >> 3, bit = ly & 7.
            int ly = 255 - y;
            uint8_t b = col[ly >> 3];
            bool lit = (b & (1 << (ly & 7))) != 0;
            if (lit) {
                // Color overlay: top 32 rows red, bottom 40 rows green, middle white
                if (y < 32) fb[y * w + x] = COLOR_RED;
                else if (y >= 256 - 40) fb[y * w + x] = COLOR_GREEN;
                else fb[y * w + x] = COLOR_WHITE;
            } else {
                fb[y * w + x] = COLOR_BLACK;
            }
        }
    }

    save_bmp_8bpp_palette(filename, w, h, fb, s_host_palette);
    free(fb);
    printf("Saved native Space Invaders capture: %s (%dx%d)\n", filename, w, h);
}

static bool load_file(const char *path, uint8_t *dst, size_t max_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fread(dst, 1, max_len, f);
    fclose(f);
    return true;
}

static uint8_t g_pacman_rom[16384];
static uint8_t g_pacman_tile_rom[4096];
static uint8_t g_pacman_sprite_rom[4096];
static uint8_t g_pacman_palette_prom[32];
static uint8_t g_pacman_color_prom[256];
static uint8_t g_pacman_sound_prom[256];

int main() {
    load_file("roms/pacman/pacman.6e", &g_pacman_rom[0x0000], 4096);
    load_file("roms/pacman/pacman.6f", &g_pacman_rom[0x1000], 4096);
    load_file("roms/pacman/pacman.6h", &g_pacman_rom[0x2000], 4096);
    load_file("roms/pacman/pacman.6j", &g_pacman_rom[0x3000], 4096);
    load_file("roms/pacman/pacman.5e", g_pacman_tile_rom, 4096);
    load_file("roms/pacman/pacman.5f", g_pacman_sprite_rom, 4096);
    load_file("roms/pacman/82s123.7f", g_pacman_palette_prom, 32);
    load_file("roms/pacman/82s126.4a", g_pacman_color_prom, 256);
    load_file("roms/pacman/82s126.1m", g_pacman_sound_prom, 256);

    pacman_machine_t pac;
    pacman_machine_init(&pac, g_pacman_rom, g_pacman_sound_prom);
    pacman_video_init(g_pacman_palette_prom, g_pacman_color_prom, g_pacman_tile_rom, g_pacman_sprite_rom);
    pacman_video_load_palette();

    // Step Pac-Man to frame 600 (attract / character intro screen)
    for (int frame = 0; frame <= 600; frame++) {
        pacman_machine_run_frame(&pac, 51200);
        pacman_machine_vblank_interrupt(&pac);
    }
    render_pacman_native(&pac, "tools/pacman_native_intro.bmp");

    // Space Invaders
    load_file("roms/space_invaders/invaders.h", &space_invaders_rom[0x0000], 2048);
    load_file("roms/space_invaders/invaders.g", &space_invaders_rom[0x0800], 2048);
    load_file("roms/space_invaders/invaders.f", &space_invaders_rom[0x1000], 2048);
    load_file("roms/space_invaders/invaders.e", &space_invaders_rom[0x1800], 2048);

    invaders_machine_t si;
    invaders_machine_init(&si);
    space_invaders_video_init();
    space_invaders_video_load_palette();

    for (int frame = 0; frame <= 600; frame++) {
        invaders_machine_run_cycles(&si, 16640);
        invaders_machine_interrupt_mid_screen(&si);
        invaders_machine_run_cycles(&si, 16640);
        invaders_machine_interrupt_vblank(&si);
    }
    render_si_native(invaders_machine_vram(&si), "tools/si_native_intro.bmp");

    return 0;
}
