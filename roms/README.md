# Space Invaders arcade ROM

This project emulates the original Taito/Midway 1978 "Space Invaders"
arcade PCB (Intel 8080 CPU) rather than reimplementing the game logic in C
- see `src/emu/` and `Emulator.md`. That means it needs the *real* arcade
ROM dump to run, same as MAME or any other 8080 Space Invaders emulator
would.

**This repository does not, and will not, include that ROM.** It's Taito's
copyrighted work. You need to supply your own legally-obtained dump.

## What to put here

Drop the 4 standard 2KB ROM files, named exactly as below, directly in this
folder:

| File | Loaded at | Size |
|---|---|---|
| `invaders.h` | $0000-$07FF | 2048 bytes |
| `invaders.g` | $0800-$0FFF | 2048 bytes |
| `invaders.f` | $1000-$17FF | 2048 bytes |
| `invaders.e` | $1800-$1FFF | 2048 bytes |

These are the standard filenames used by MAME's `invaders` romset and by
essentially every 8080 Space Invaders emulator write-up - if you have an
`invaders.zip` MAME romset, these 4 files are already inside it under
these exact names.

This folder is gitignored except this file - nothing you place here gets
committed.

**If you add these files after already having run `cmake` once**, re-run the
configure step (`cmake -S . -B build -G Ninja -DPICO_BOARD=pico2`)
before building again - CMake only notices these files exist at configure
time, not at build time.

## What happens if it's missing

`cmake/generate_rom.cmake` embeds these files into the firmware image at
build time. If one or more are missing, the build still succeeds but
substitutes zero-filled placeholder data for the missing file(s) (with a
CMake warning) - enough to confirm the emulator core compiles and links,
but the "game" will just be a CPU executing NOP forever against a blank
screen, not anything playable.
