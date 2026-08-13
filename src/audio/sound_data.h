#ifndef SOUND_DATA_H
#define SOUND_DATA_H

#include <stddef.h>
#include <stdint.h>

// Sample rate every embedded sound is expected to already be at - see
// sounds/README.md. audio_i2s.c mixes at this rate with no resampling, so a
// file recorded/converted at any other rate plays back at the wrong
// pitch/speed.
#define SOUND_SAMPLE_RATE_HZ 32000

typedef struct {
    const int16_t *samples; // mono 16-bit PCM, SOUND_SAMPLE_RATE_HZ
    size_t frame_count;     // 0 if this effect's file wasn't supplied at build time (see cmake/generate_sounds.cmake) - silent, not an error
} sound_sample_t;

// One entry per real Space Invaders discrete sound-effect trigger bit - see
// src/audio/sound_effects.c for the exact port 3/5 bit -> ID mapping
// (matches computerarcheology.com's hardware writeup, the same reference
// invaders_machine.c cites for the rest of the port map).
typedef enum {
    SOUND_UFO = 0,     // port 3 bit 0 - loops while the bit stays set
    SOUND_SHOT,        // port 3 bit 1
    SOUND_PLAYER_DIE,  // port 3 bit 2 ("Flash")
    SOUND_INVADER_DIE, // port 3 bit 3
    SOUND_EXTRA_LIFE,  // port 3 bit 4 ("Extended play")
    SOUND_FLEET1,      // port 5 bit 0
    SOUND_FLEET2,      // port 5 bit 1
    SOUND_FLEET3,      // port 5 bit 2
    SOUND_FLEET4,      // port 5 bit 3
    SOUND_UFO_HIT,     // port 5 bit 4
    SOUND_COUNT,
} sound_id_t;

// Defined in the CMake-generated generated/sound_data.c (see
// cmake/generate_sounds.cmake), indexed by sound_id_t. Any sound whose file
// wasn't supplied at build time still gets a valid (zero-length) entry, so
// this array is always fully populated even with no sounds/ files present.
extern const sound_sample_t sound_table[SOUND_COUNT];

#endif // SOUND_DATA_H
