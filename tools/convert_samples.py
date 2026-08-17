#!/usr/bin/env python3
"""
convert_samples.py - Convert MAME or arcade Space Invaders WAV samples to raw 32 kHz PCM.

Usage:
    python tools/convert_samples.py <input_directory_or_wav_files>

MAME sample numbering mapping:
    0.wav -> ufo.pcm
    1.wav -> shot.pcm
    2.wav -> player_die.pcm
    3.wav -> invader_die.pcm
    4.wav -> fleet1.pcm
    5.wav -> fleet2.pcm
    6.wav -> fleet3.pcm
    7.wav -> fleet4.pcm
    8.wav -> ufo_hit.pcm
    9.wav -> extra_life.pcm
"""

import sys
import os
import wave
import struct

SAMPLE_RATE = 32000
ROMS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "roms", "space_invaders"))

MAME_MAPPING = {
    "0.wav": "ufo.pcm",
    "1.wav": "shot.pcm",
    "2.wav": "player_die.pcm",
    "3.wav": "invader_die.pcm",
    "4.wav": "fleet1.pcm",
    "5.wav": "fleet2.pcm",
    "6.wav": "fleet3.pcm",
    "7.wav": "fleet4.pcm",
    "8.wav": "ufo_hit.pcm",
    "9.wav": "extra_life.pcm",
    # Also support descriptive filenames
    "ufo.wav": "ufo.pcm",
    "shot.wav": "shot.pcm",
    "player_die.wav": "player_die.pcm",
    "explosion.wav": "player_die.pcm",
    "invader_die.wav": "invader_die.pcm",
    "invader_hit.wav": "invader_die.pcm",
    "fleet1.wav": "fleet1.pcm",
    "fleet2.wav": "fleet2.pcm",
    "fleet3.wav": "fleet3.pcm",
    "fleet4.wav": "fleet4.pcm",
    "fastinvader1.wav": "fleet1.pcm",
    "fastinvader2.wav": "fleet2.pcm",
    "fastinvader3.wav": "fleet3.pcm",
    "fastinvader4.wav": "fleet4.pcm",
    "ufo_hit.wav": "ufo_hit.pcm",
    "extra_life.wav": "extra_life.pcm",
    "extended.wav": "extra_life.pcm"
}

def convert_wav_to_pcm(wav_path, output_pcm_path):
    with wave.open(wav_path, 'rb') as w:
        n_channels = w.getnchannels()
        sampwidth = w.getsampwidth()
        framerate = w.getframerate()
        n_frames = w.getnframes()
        raw_frames = w.readframes(n_frames)

    # Convert raw bytes to normalized float samples [-1.0, 1.0]
    samples = []
    if sampwidth == 1: # 8-bit unsigned
        for i in range(0, len(raw_frames), n_channels):
            # Take channel 0
            val = (raw_frames[i] - 128) / 128.0
            samples.append(val)
    elif sampwidth == 2: # 16-bit signed
        fmt = f"<{n_frames * n_channels}h"
        unpacked = struct.unpack(fmt, raw_frames)
        for i in range(0, len(unpacked), n_channels):
            samples.append(unpacked[i] / 32768.0)
    else:
        print(f"Unsupported sample width: {sampwidth} bytes in {wav_path}")
        return False

    # Resample to 32,000 Hz using linear interpolation
    if framerate != SAMPLE_RATE:
        ratio = SAMPLE_RATE / framerate
        new_len = int(len(samples) * ratio)
        resampled = []
        for i in range(new_len):
            orig_idx = i / ratio
            idx0 = int(orig_idx)
            idx1 = min(idx0 + 1, len(samples) - 1)
            frac = orig_idx - idx0
            val = samples[idx0] * (1.0 - frac) + samples[idx1] * frac
            resampled.append(val)
        samples = resampled

    # Write raw 16-bit signed little-endian PCM
    os.makedirs(os.path.dirname(output_pcm_path), exist_ok=True)
    with open(output_pcm_path, 'wb') as f:
        for s in samples:
            clamped = max(-32768, min(32767, int(s * 32767.0)))
            f.write(struct.pack('<h', clamped))

    print(f"Converted: {os.path.basename(wav_path)} -> {os.path.basename(output_pcm_path)} ({len(samples)} samples @ 32 kHz)")
    return True

def main():
    input_path = sys.argv[1] if len(sys.argv) > 1 else ROMS_DIR
    out_dir = ROMS_DIR

    wav_files = []
    if os.path.isdir(input_path):
        out_dir = input_path
        for fname in os.listdir(input_path):
            if fname.lower().endswith(".wav"):
                wav_files.append(os.path.join(input_path, fname))
        # Also check samples/ subdirectory
        samples_sub = os.path.join(input_path, "samples")
        if os.path.isdir(samples_sub):
            for fname in os.listdir(samples_sub):
                if fname.lower().endswith(".wav"):
                    wav_files.append(os.path.join(samples_sub, fname))
    else:
        wav_files = sys.argv[1:]

    if not wav_files:
        print(f"No .wav files found in {input_path}")
        print("Usage: python tools/convert_samples.py [path_to_wav_directory]")
        return

    converted = 0
    for w in wav_files:
        base = os.path.basename(w).lower()
        if base in MAME_MAPPING:
            out_name = MAME_MAPPING[base]
            out_path = os.path.join(out_dir, out_name)
            if convert_wav_to_pcm(w, out_path):
                converted += 1
        else:
            print(f"Skipping unrecognized WAV: {base}")

    print(f"\nSuccessfully converted {converted} samples into {out_dir}/")

if __name__ == "__main__":
    main()
