#include <stdbool.h>
#include <stdint.h>

#include "audio_i2s.h"
#include "sound_data.h"
#include "sound_effects.h"

// Port 3: bit0=UFO(repeats) bit1=Shot bit2=Flash(player die)
// bit3=Invader die bit4=Extended play bit5=AMP enable.
// Port 5: bit0-3=Fleet movement 1-4 bit4=UFO Hit.
// (computerarcheology.com's Space Invaders hardware writeup - see
// sound_effects.h.)
#define PORT3_BIT_UFO         0x01
#define PORT3_BIT_SHOT        0x02
#define PORT3_BIT_PLAYER_DIE  0x04
#define PORT3_BIT_INVADER_DIE 0x08
#define PORT3_BIT_EXTRA_LIFE  0x10
#define PORT3_BIT_AMP_ENABLE  0x20

#define PORT5_BIT_FLEET1  0x01
#define PORT5_BIT_FLEET2  0x02
#define PORT5_BIT_FLEET3  0x04
#define PORT5_BIT_FLEET4  0x08
#define PORT5_BIT_UFO_HIT 0x10

static uint8_t prev_port3 = 0;
static uint8_t prev_port5 = 0;

// One-shot effects trigger on the bit's 0->1 transition, not its level -
// the ROM pulses these bits briefly, and the sample is meant to play out in
// full regardless of how soon the bit clears again.
static inline bool rising_edge(uint8_t prev, uint8_t now, uint8_t bit) {
    return !(prev & bit) && (now & bit);
}

void sound_effects_on_port_write(void *ctx, uint8_t port, uint8_t value) {
    (void)ctx;

    switch (port) {
    case 3:
        if (rising_edge(prev_port3, value, PORT3_BIT_SHOT))
            audio_i2s_play_sound(SOUND_SHOT);
        if (rising_edge(prev_port3, value, PORT3_BIT_PLAYER_DIE))
            audio_i2s_play_sound(SOUND_PLAYER_DIE);
        if (rising_edge(prev_port3, value, PORT3_BIT_INVADER_DIE))
            audio_i2s_play_sound(SOUND_INVADER_DIE);
        if (rising_edge(prev_port3, value, PORT3_BIT_EXTRA_LIFE))
            audio_i2s_play_sound(SOUND_EXTRA_LIFE);
        // UFO is level-triggered (loops for as long as the bit stays set),
        // unlike every other bit here.
        audio_i2s_set_sound_loop(SOUND_UFO, (value & PORT3_BIT_UFO) != 0);
        // Real hardware's AMP-enable relay - no physical amp on this board
        // (audio_i2s_set_mute() is a no-op), kept for parity with the real
        // port 3 bit in case a future output stage needs it.
        audio_i2s_set_mute((value & PORT3_BIT_AMP_ENABLE) == 0);
        prev_port3 = value;
        break;
    case 5:
        if (rising_edge(prev_port5, value, PORT5_BIT_FLEET1))
            audio_i2s_play_sound(SOUND_FLEET1);
        if (rising_edge(prev_port5, value, PORT5_BIT_FLEET2))
            audio_i2s_play_sound(SOUND_FLEET2);
        if (rising_edge(prev_port5, value, PORT5_BIT_FLEET3))
            audio_i2s_play_sound(SOUND_FLEET3);
        if (rising_edge(prev_port5, value, PORT5_BIT_FLEET4))
            audio_i2s_play_sound(SOUND_FLEET4);
        if (rising_edge(prev_port5, value, PORT5_BIT_UFO_HIT))
            audio_i2s_play_sound(SOUND_UFO_HIT);
        prev_port5 = value;
        break;
    default:
        break;
    }
}
