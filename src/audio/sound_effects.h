#ifndef SOUND_EFFECTS_H
#define SOUND_EFFECTS_H

#include <stdint.h>

// Decodes real Space Invaders port 3/5 discrete sound-effect writes (bit
// mapping per computerarcheology.com's hardware writeup - the same
// reference invaders_machine.c cites for the rest of the port map) into
// audio_i2s_play_sound()/audio_i2s_set_sound_loop() calls, plus the port 3
// bit 5 AMP-enable line (audio_i2s_set_mute()).
//
// Matches invaders_machine_t's sound_write callback signature exactly, so
// it can be wired up directly with no wrapper:
//     s_machine.sound_write = sound_effects_on_port_write;
void sound_effects_on_port_write(void *ctx, uint8_t port, uint8_t value);

#endif // SOUND_EFFECTS_H
