#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define CHIPS_IMPL
#include "../src/emu/z80.h"

// Stubs for functions referenced in emu / video
void audio_engine_feed_queue(uint32_t level) {
    (void)level;
}

// Global palette tracking for saving BMPs
static uint32_t s_host_palette[256];
void dvi_display_set_palette(uint8_t index, uint32_t rgb888) {
    s_host_palette[index] = rgb888;
}

// Pull in the machine implementations
#include "../src/audio/namco_sound.h"
#include "../src/audio/namco_sound.c"
#include "../src/emu/pacman_machine.h"
#include "../src/emu/pacman_machine.c"
#include "../src/games/pacman/pacman_config.h"
#include "../src/games/pacman/pacman_video.h"
#include "../src/games/pacman/pacman_video.c"

// Space Invaders includes
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
    if (!f) {
        printf("Failed to open %s for writing\n", filename);
        return;
    }

    int row_stride = (width * 3 + 3) & ~3;
    uint32_t image_size = (uint32_t)row_stride * height;
    uint32_t file_size = 54 + image_size;

    BMPFileHeader fh;
    fh.bfType = 0x4D42; // "BM"
    fh.bfSize = file_size;
    fh.bfReserved1 = 0;
    fh.bfReserved2 = 0;
    fh.bfOffBits = 54;

    BMPInfoHeader ih;
    memset(&ih, 0, sizeof(ih));
    ih.biSize = sizeof(BMPInfoHeader);
    ih.biWidth = width;
    ih.biHeight = -height; // Top-down
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biCompression = 0;
    ih.biSizeImage = image_size;
    ih.biXPelsPerMeter = 2835;
    ih.biYPelsPerMeter = 2835;

    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);

    uint8_t *row_buf = (uint8_t *)calloc(1, row_stride);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t pal_idx = fb[y * width + x];
            uint32_t rgb = palette[pal_idx];
            uint8_t r = (rgb >> 16) & 0xFF;
            uint8_t g = (rgb >> 8) & 0xFF;
            uint8_t b = rgb & 0xFF;
            row_buf[x * 3 + 0] = b;
            row_buf[x * 3 + 1] = g;
            row_buf[x * 3 + 2] = r;
        }
        fwrite(row_buf, 1, row_stride, f);
    }
    free(row_buf);
    fclose(f);
    printf("Saved %s (%dx%d)\n", filename, width, height);
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

void run_pacman(int total_frames) {
    printf("\n=== Running Pac-Man Emulation (%d frames) ===\n", total_frames);
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

    uint8_t *fb = (uint8_t *)calloc(FRAME_WIDTH * FRAME_HEIGHT, 1);

    for (int frame = 0; frame <= total_frames; frame++) {
        pacman_machine_run_frame(&pac, 51200);
        pacman_machine_vblank_interrupt(&pac);

        if (frame == 700) {
            pacman_video_render_frame(&pac, fb);
            save_bmp_8bpp_palette("tools/pacman_attract_01_characters.bmp", FRAME_WIDTH, FRAME_HEIGHT, fb, s_host_palette);
            printf("Saved tools/pacman_attract_01_characters.bmp (Ghost Character & Nickname Table)\n");
        } else if (frame == 920) {
            pacman_video_render_frame(&pac, fb);
            save_bmp_8bpp_palette("tools/pacman_attract_02_pts_table.bmp", FRAME_WIDTH, FRAME_HEIGHT, fb, s_host_palette);
            printf("Saved tools/pacman_attract_02_pts_table.bmp (Point Values Table)\n");
        } else if (frame == 1050) {
            pacman_video_render_frame(&pac, fb);
            save_bmp_8bpp_palette("tools/pacman_attract_03_chase.bmp", FRAME_WIDTH, FRAME_HEIGHT, fb, s_host_palette);
            printf("Saved tools/pacman_attract_03_chase.bmp (Pac-Man & Ghosts Chase Animation)\n");
        } else if (frame == 1620) {
            pacman_video_render_frame(&pac, fb);
            save_bmp_8bpp_palette("tools/pacman_attract_04_demo_start.bmp", FRAME_WIDTH, FRAME_HEIGHT, fb, s_host_palette);
            printf("Saved tools/pacman_attract_04_demo_start.bmp (Beginning of Demo Screen - 4 Ghosts in Middle & READY!)\n");
        } else if (frame == 2200) {
            pacman_video_render_frame(&pac, fb);
            save_bmp_8bpp_palette("tools/pacman_attract_05_maze_demo.bmp", FRAME_WIDTH, FRAME_HEIGHT, fb, s_host_palette);
            printf("Saved tools/pacman_attract_05_maze_demo.bmp (Attract Gameplay Maze Demo)\n");
        }
    }
    free(fb);
}

void run_space_invaders(int total_frames) {
    printf("\n=== Running Space Invaders Emulation (%d frames) ===\n", total_frames);
    load_file("roms/space_invaders/invaders.h", &space_invaders_rom[0x0000], 2048);
    load_file("roms/space_invaders/invaders.g", &space_invaders_rom[0x0800], 2048);
    load_file("roms/space_invaders/invaders.f", &space_invaders_rom[0x1000], 2048);
    load_file("roms/space_invaders/invaders.e", &space_invaders_rom[0x1800], 2048);

    invaders_machine_t si;
    invaders_machine_init(&si);
    space_invaders_video_init();
    space_invaders_video_load_palette();

    uint8_t *fb = (uint8_t *)calloc(FRAME_WIDTH * FRAME_HEIGHT, 1);

    for (int frame = 0; frame <= total_frames; frame++) {
        invaders_machine_run_cycles(&si, 16640);
        invaders_machine_interrupt_mid_screen(&si);
        invaders_machine_run_cycles(&si, 16640);
        invaders_machine_interrupt_vblank(&si);

        if (frame == 600) {
            space_invaders_video_render_frame(fb, invaders_machine_vram(&si), FRAME_WIDTH);
            save_bmp_8bpp_palette("tools/si_attract_01_score_table.bmp", FRAME_WIDTH, FRAME_HEIGHT, fb, s_host_palette);
            printf("Saved tools/si_attract_01_score_table.bmp (Score Advance Table)\n");
        } else if (frame == 900) {
            space_invaders_video_render_frame(fb, invaders_machine_vram(&si), FRAME_WIDTH);
            save_bmp_8bpp_palette("tools/si_attract_02_insert_coin.bmp", FRAME_WIDTH, FRAME_HEIGHT, fb, s_host_palette);
            printf("Saved tools/si_attract_02_insert_coin.bmp (Space Invaders Title & Insert Coin)\n");
        } else if (frame == 2000) {
            space_invaders_video_render_frame(fb, invaders_machine_vram(&si), FRAME_WIDTH);
            save_bmp_8bpp_palette("tools/si_attract_03_demo_play.bmp", FRAME_WIDTH, FRAME_HEIGHT, fb, s_host_palette);
            printf("Saved tools/si_attract_03_demo_play.bmp (Attract Gameplay Demo)\n");
        }
    }
    free(fb);
}

int main(int argc, char **argv) {
    int frames = 2500;
    if (argc > 1) {
        frames = atoi(argv[1]);
    }
    run_pacman(frames);
    run_space_invaders(frames);
    printf("\nAll attract mode captures complete.\n");
    return 0;
}
