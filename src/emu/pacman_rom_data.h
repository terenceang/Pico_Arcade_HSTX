#ifndef PACMAN_ROM_DATA_H
#define PACMAN_ROM_DATA_H

#include <stdint.h>
#include <stddef.h>

extern const uint8_t pacman_rom[16384];
extern const size_t pacman_rom_size;

extern const uint8_t pacman_tile_rom[4096];
extern const size_t pacman_tile_rom_size;

extern const uint8_t pacman_sprite_rom[4096];
extern const size_t pacman_sprite_rom_size;

extern const uint8_t pacman_palette_prom[32];
extern const size_t pacman_palette_prom_size;

extern const uint8_t pacman_color_prom[256];
extern const size_t pacman_color_prom_size;

extern const uint8_t pacman_sound_prom[256];
extern const size_t pacman_sound_prom_size;

#endif // PACMAN_ROM_DATA_H
