#ifndef SPACE_INVADERS_ROM_DATA_H
#define SPACE_INVADERS_ROM_DATA_H

#include <stddef.h>
#include <stdint.h>

// Defined by the CMake-generated source built from roms/space_invaders/ (see
// cmake/generate_space_invaders_rom.cmake and roms/space_invaders/README.md) - the real Space Invaders
// arcade ROM, or a zero-filled placeholder if the real files weren't
// present at build time.
extern const uint8_t space_invaders_rom[8192];
extern const size_t space_invaders_rom_size;

#endif // SPACE_INVADERS_ROM_DATA_H

