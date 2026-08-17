# Arcade Sound Effects (`sounds/`)

This directory contains sound samples and audio assets organized per arcade game.

> [!NOTE]
> **No Audio Assets Included**: This repository does not contain copyrighted recordings or audio dumps. Users must supply their own sound samples for games that require discrete audio samples.

## Game Sound Folders

- **[`sounds/space_invaders/`](space_invaders/README.md)**: Sound effects for Space Invaders (1978). Requires 10 raw 32 kHz 16-bit mono `.pcm` files representing the discrete analog sound generator triggers.

## Converting WAV Audio to Game PCM Format

Use the built-in conversion utility to automatically resample and place WAV files:
```sh
python tools/convert_samples.py <input_wav_directory>
```
