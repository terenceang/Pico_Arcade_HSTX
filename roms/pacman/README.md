# Pac-Man Arcade ROMs

Place the authentic 1980 Namco/Midway Pac-Man arcade ROM and PROM files in this directory:

### Required Program ROMs (16 KB):
- `pacman.6e` (4,096 bytes, MD5: `50524a1144a8420e6d87a4e8303e077e` or Midway/Namco set)
- `pacman.6f` (4,096 bytes, MD5: `464016f45209772d512ef9e53068e27c`)
- `pacman.6h` (4,096 bytes, MD5: `2753443a504be09f3c1bf3258c70420f`)
- `pacman.6j` (4,096 bytes, MD5: `969b82142345598687a070eb37c02b23`)

### Required Character & Sprite ROMs (8 KB):
- `pacman.5e` (4,096 bytes, 8x8 character tiles)
- `pacman.5f` (4,096 bytes, 16x16 sprite tiles)

### Required PROMs (544 bytes):
- `82s123.7f` (32 bytes, 32-color RGB palette PROM)
- `82s126.4a` (256 bytes, color lookup table PROM)
- `82s126.1m` (256 bytes, 3-voice sound wavetable PROM)

If any PROM is missing, the build still succeeds using a hand-built placeholder table
(`s_default_color_prom`/`s_default_palette_rgb` in `src/games/pacman/pacman_video.c`)
instead of `82s123.7f`/`82s126.4a` - colors will be close but not authentic. Supply the
real dumps for exact original arcade colors.
