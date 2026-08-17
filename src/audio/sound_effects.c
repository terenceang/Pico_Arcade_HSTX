#include "sound_effects.h"
#include <stddef.h>

static sound_play_fn s_play_fn = NULL;
static sound_stop_loop_fn s_stop_loop_fn = NULL;

static uint8_t prev_port3 = 0;
static uint8_t prev_port5 = 0;

void sound_effects_init(sound_play_fn play_fn, sound_stop_loop_fn stop_loop_fn) {
    s_play_fn = play_fn;
    s_stop_loop_fn = stop_loop_fn;
    prev_port3 = 0;
    prev_port5 = 0;
}

static inline bool rising_edge(uint8_t prev, uint8_t now, uint8_t bit) {
    return !(prev & bit) && (now & bit);
}

void sound_effects_on_port_write(void *ctx, uint8_t port, uint8_t value) {
    (void)ctx;

    switch (port) {
    case 3:
        if (rising_edge(prev_port3, value, 0x02) && s_play_fn)
            s_play_fn(SOUND_SHOT, false);
        if (rising_edge(prev_port3, value, 0x04) && s_play_fn)
            s_play_fn(SOUND_PLAYER_DIE, false);
        if (rising_edge(prev_port3, value, 0x08) && s_play_fn)
            s_play_fn(SOUND_INVADER_DIE, false);
        if (rising_edge(prev_port3, value, 0x10) && s_play_fn)
            s_play_fn(SOUND_EXTRA_LIFE, false);
        if (value & 0x01) {
            if (s_play_fn) s_play_fn(SOUND_UFO, true);
        } else {
            if (s_stop_loop_fn) s_stop_loop_fn();
        }
        prev_port3 = value;
        break;
    case 5:
        if (rising_edge(prev_port5, value, 0x01) && s_play_fn)
            s_play_fn(SOUND_FLEET1, false);
        if (rising_edge(prev_port5, value, 0x02) && s_play_fn)
            s_play_fn(SOUND_FLEET2, false);
        if (rising_edge(prev_port5, value, 0x04) && s_play_fn)
            s_play_fn(SOUND_FLEET3, false);
        if (rising_edge(prev_port5, value, 0x08) && s_play_fn)
            s_play_fn(SOUND_FLEET4, false);
        if (rising_edge(prev_port5, value, 0x10) && s_play_fn)
            s_play_fn(SOUND_UFO_HIT, false);
        prev_port5 = value;
        break;
    default:
        break;
    }
}
