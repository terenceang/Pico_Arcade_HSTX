#include "core/plugin_api.h"
#include "pacman_machine.h"
#include "pacman_video.h"
#include "namco_sound.h"
#include "pacman_config.h"
#include "pacman_rom_data.h"
#include "video/dvi_display.h"

#include <string.h>

#define PACMAN_CPU_HZ                  3072000
// Native arcade timing (60.606 Hz): 264 scanlines * 192 CPU cycles/line = 50,688 cycles
#define PACMAN_CYCLES_PER_FRAME_NATIVE 50688
// Standard 60.0 Hz DVI/HDMI refresh rate: 3.072 MHz / 60 = 51,200 cycles
#define PACMAN_CYCLES_PER_FRAME_60HZ   (PACMAN_CPU_HZ / 60)

#ifndef PACMAN_TARGET_CYCLES_PER_FRAME
#define PACMAN_TARGET_CYCLES_PER_FRAME PACMAN_CYCLES_PER_FRAME_60HZ
#endif

static pacman_machine_t s_pacman;

static void pacman_apply_dip_switches(void) {
    s_pacman.dsw1 = (uint8_t)(0x01 | // 1 coin 1 play
                              ((PACMAN_DIP_LIVES & 3) << 2) |
                              ((PACMAN_DIP_BONUS_LIFE & 3) << 4) |
                              (PACMAN_DIP_DIFFICULTY_NORMAL ? (1 << 6) : 0) |
                              (PACMAN_DIP_GHOST_NAMES_NORMAL ? (1 << 7) : 0));
}

static void pacman_plugin_init(void) {
    pacman_machine_init(&s_pacman, pacman_rom, pacman_sound_prom);
    pacman_apply_dip_switches();
#if PACMAN_CABINET_TYPE == PACMAN_CABINET_UPRIGHT
    s_pacman.in1 = 0xFF; // Upright cabinet, Test Mode OFF (bit 4 high)
#else
    s_pacman.in1 = 0x7F; // Cocktail cabinet, Test Mode OFF (bit 4 high)
#endif
    pacman_video_init(pacman_palette_prom, pacman_color_prom, pacman_tile_rom, pacman_sprite_rom);
}

static void pacman_plugin_reset(void) {
    pacman_machine_reset(&s_pacman);
    pacman_apply_dip_switches();
#if PACMAN_CABINET_TYPE == PACMAN_CABINET_UPRIGHT
    s_pacman.in1 = 0xFF;
#else
    s_pacman.in1 = 0x7F;
#endif
}

static void pacman_plugin_run_frame(void) {
    pacman_machine_run_frame(&s_pacman, PACMAN_TARGET_CYCLES_PER_FRAME);
    pacman_machine_vblank_interrupt(&s_pacman);
}

static void pacman_plugin_render_frame(uint8_t *fb, uint32_t stride) {
    (void)stride;
    pacman_video_render_frame(&s_pacman, fb);
}

static void pacman_plugin_load_palette(void) {
    pacman_video_load_palette();
}

static void pacman_plugin_render_audio(int16_t *buf_interleaved, uint32_t sample_count) {
    namco_sound_render_48k(&s_pacman.sound, buf_interleaved, sample_count);
}

static void pacman_plugin_update_inputs(uint32_t mask) {
    // IN0 (Active Low): bit0=Up, bit1=Left, bit2=Right, bit3=Down, bit5=Coin1
    uint8_t in0 = 0xFF;
    if (mask & INPUT_UP)       in0 &= ~(1u << 0); // Up is bit0
    if (mask & INPUT_DOWN)     in0 &= ~(1u << 3); // Down is bit3
    if (mask & INPUT_LEFT)     in0 &= ~(1u << 1); // Left is bit1
    if (mask & INPUT_RIGHT)    in0 &= ~(1u << 2); // Right is bit2
    if (mask & INPUT_COIN)     in0 &= ~(1u << 5); // Coin 1
    s_pacman.in0 = in0;

    // IN1 (Active Low): bit4=Board Test (1=Normal, 0=Test), bit5=1P Start, bit6=2P Start, bit7=Cabinet (1=Upright, 0=Cocktail)
    uint8_t in1 = 0xFF; // All lines high by default (Normal play mode, test mode OFF)
#if PACMAN_CABINET_TYPE == PACMAN_CABINET_UPRIGHT
    in1 |= (1u << 7); // 1 = Upright
#else
    in1 &= ~(1u << 7); // 0 = Cocktail Table
#endif
    if (mask & INPUT_START_1P) in1 &= ~(1u << 5);
    if (mask & INPUT_START_2P) in1 &= ~(1u << 6);
    s_pacman.in1 = in1;
}

static bool pacman_plugin_is_rom_valid(void) {
    for (size_t i = 0; i < sizeof(pacman_rom); i++) {
        if (pacman_rom[i] != 0) return true;
    }
    return false;
}

#include "video/missing_rom_screen.h"

static void pacman_plugin_render_missing_rom(uint8_t *fb, uint32_t frame_count) {
    static const char *files[] = {
        "pacman.6e, 6f, 6h, 6j (16KB Z80 ROM)",
        "pacman.5e, 5f (8KB Tiles/Sprites)",
        "82s123.7f, 82s126.4a (Palette PROMs)",
        "82s126.1m (Sound Wavetable PROM)",
    };
    missing_rom_screen_render_dialog(fb, "** PAC-MAN ARCADE ROMS REQUIRED **", "roms/pacman/",
                                     files, 4, frame_count);
}

const emulator_plugin_t g_pacman_plugin = {
    .id = "pacman",
    .name = "Pac-Man (1980)",
    .version = "1.4.0",
    .author = "Namco / Midway",
    .init = pacman_plugin_init,
    .reset = pacman_plugin_reset,
    .run_frame = pacman_plugin_run_frame,
    .render_frame = pacman_plugin_render_frame,
    .load_palette = pacman_plugin_load_palette,
    .render_audio = pacman_plugin_render_audio,
    .update_inputs = pacman_plugin_update_inputs,
    .is_rom_valid = pacman_plugin_is_rom_valid,
    .render_missing_rom = pacman_plugin_render_missing_rom,
};
