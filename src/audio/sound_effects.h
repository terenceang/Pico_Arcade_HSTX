#ifndef SOUND_EFFECTS_H
#define SOUND_EFFECTS_H

#include <stdint.h>
#include <stdbool.h>
#include "sound_data.h"

typedef void (*sound_play_fn)(sound_id_t id, bool loop);
typedef void (*sound_stop_loop_fn)(void);

void sound_effects_init(sound_play_fn play_fn, sound_stop_loop_fn stop_loop_fn);
void sound_effects_on_port_write(void *ctx, uint8_t port, uint8_t value);

#endif // SOUND_EFFECTS_H
