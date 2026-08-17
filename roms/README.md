# Arcade ROMs

This project emulates authentic arcade hardware and requires original arcade ROM dumps to run the games.

**This repository does not include copyrighted arcade ROMs.** You must supply your own legally-obtained ROM dumps in their respective game folders.

## Game ROM Directories

- **Space Invaders**: Place ROM files in [`roms/space_invaders/`](space_invaders/README.md)
  - `invaders.h`, `invaders.g`, `invaders.f`, `invaders.e` (2 KB each)
- **Pac-Man**: Place ROM and PROM files in [`roms/pacman/`](pacman/README.md)
  - Program ROMs: `pacman.6e`, `pacman.6f`, `pacman.6h`, `pacman.6j`
  - Tile/Sprite ROMs: `pacman.5e`, `pacman.5f`
  - Color/Sound PROMs: `82s123.7f`, `82s126.4a`, `82s126.1m`

## Build Behavior

CMake embeds these files into the firmware image at build time. If ROM files are missing, the build will still succeed with placeholder data, and on hardware the emulator will display a missing ROM prompt showing the required files and paths.

If you add or modify ROM files after running CMake once, re-run the CMake configure step before rebuilding.
