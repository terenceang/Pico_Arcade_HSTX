#include "core/plugin_api.h"
#include "space_invaders_machine.h"
#include "sound_effects.h"
#include "sound_data.h"
#include "space_invaders_video.h"
#include "space_invaders_config.h"
#include "space_invaders_rom_data.h"
#include "video/display_config.h"

#include <string.h>

#define SI_CPU_HZ                      1996800
// Native arcade timing (59.542 Hz): 262 scanlines * 128 CPU cycles/line = 33,536 cycles
#define SI_CYCLES_PER_FRAME_NATIVE     33536
// Standard 60.0 Hz DVI/HDMI refresh rate: 1.9968 MHz / 60 = 33,280 cycles
#define SI_CYCLES_PER_FRAME_60HZ       (SI_CPU_HZ / 60)

#ifndef SI_TARGET_CYCLES_PER_FRAME
#define SI_TARGET_CYCLES_PER_FRAME     SI_CYCLES_PER_FRAME_60HZ
#endif

#define SI_CYCLES_PER_FRAME            SI_TARGET_CYCLES_PER_FRAME
#define SI_CYCLES_PER_HALF             (SI_CYCLES_PER_FRAME / 2)


static invaders_machine_t s_machine;

// Audio voice mixer state
#define AUDIO_MAX_VOICES 4
typedef struct {
    const int16_t *data;
    uint32_t pos;
    uint32_t len;
    uint32_t frac;
    bool active;
} audio_voice_t;

static audio_voice_t s_voices[AUDIO_MAX_VOICES];
static unsigned s_voice_steal_next = 0;
static audio_voice_t s_loop_voice;

static inline int16_t voice_advance(audio_voice_t *v, bool loop) {
    if (!v->data || v->len == 0) {
        v->active = false;
        return 0;
    }
    int16_t sample = v->data[v->pos];
    v->frac += SOUND_SAMPLE_RATE_HZ; // 32000
    while (v->frac >= 48000) {
        v->frac -= 48000;
        if (++v->pos >= v->len) {
            if (loop) {
                v->pos = 0;
            } else {
                v->active = false;
                v->pos = v->len - 1;
                break;
            }
        }
    }
    return sample;
}

static void si_audio_play_sample(sound_id_t id, bool loop) {
    if (id >= SOUND_COUNT) return;
    const sound_sample_t *s = &sound_table[id];
    if (!s->samples || s->frame_count == 0) return;

    if (loop) {
        s_loop_voice.data = s->samples;
        s_loop_voice.len = s->frame_count;
        s_loop_voice.pos = 0;
        s_loop_voice.frac = 0;
        s_loop_voice.active = true;
    } else {
        audio_voice_t *v = &s_voices[s_voice_steal_next];
        s_voice_steal_next = (s_voice_steal_next + 1) % AUDIO_MAX_VOICES;
        v->data = s->samples;
        v->len = s->frame_count;
        v->pos = 0;
        v->frac = 0;
        v->active = true;
    }
}

static void si_audio_stop_loop(void) {
    s_loop_voice.active = false;
}

static void si_apply_dip_switches(void) {
    s_machine.in2 = (uint8_t)((SI_DIP_SHIPS & 3) |
                             (SI_DIP_EXTRA_SHIP_1000 ? (1 << 3) : 0) |
                             (SI_DIP_COIN_INFO_DEMO ? (1 << 7) : 0));
}

static void si_plugin_init(void) {
    invaders_machine_init(&s_machine);
    s_machine.sound_write = sound_effects_on_port_write;
    si_apply_dip_switches();
    sound_effects_init(si_audio_play_sample, si_audio_stop_loop);
    space_invaders_video_init();
    memset(s_voices, 0, sizeof(s_voices));
    memset(&s_loop_voice, 0, sizeof(s_loop_voice));
}

static void si_plugin_reset(void) {
    invaders_machine_init(&s_machine);
    s_machine.sound_write = sound_effects_on_port_write;
    si_apply_dip_switches();
    memset(s_voices, 0, sizeof(s_voices));
    memset(&s_loop_voice, 0, sizeof(s_loop_voice));
}

static void si_plugin_run_frame(void) {
    invaders_machine_run_cycles(&s_machine, SI_CYCLES_PER_HALF);
    invaders_machine_interrupt_mid_screen(&s_machine);
    invaders_machine_run_cycles(&s_machine, SI_CYCLES_PER_HALF);
    invaders_machine_interrupt_vblank(&s_machine);
}

static void si_plugin_render_frame(uint8_t *fb, uint32_t stride) {
    space_invaders_video_render_frame(fb, invaders_machine_vram(&s_machine), stride);
}

static void si_plugin_load_palette(void) {
    space_invaders_video_load_palette();
}

static void si_plugin_render_audio(int16_t *buf_interleaved, uint32_t sample_count) {
    for (uint32_t i = 0; i < sample_count; i++) {
        int32_t mix = 0;
        if (s_loop_voice.active) {
            mix += voice_advance(&s_loop_voice, true);
        }
        for (unsigned v = 0; v < AUDIO_MAX_VOICES; ++v) {
            if (s_voices[v].active) {
                mix += voice_advance(&s_voices[v], false);
            }
        }
        if (mix > INT16_MAX) mix = INT16_MAX; else if (mix < INT16_MIN) mix = INT16_MIN;
        buf_interleaved[i * 2]     = (int16_t)mix;
        buf_interleaved[i * 2 + 1] = (int16_t)mix;
    }
}

static void si_plugin_update_inputs(uint32_t mask) {
    uint8_t in1 = 0x08; // bit 3 tied high
    if (mask & INPUT_COIN)     in1 |= SI_IN1_COIN;
    if (mask & INPUT_START_1P) in1 |= SI_IN1_P1_START;
    if (mask & INPUT_BUTTON_A) in1 |= SI_IN1_P1_FIRE;
    if (mask & INPUT_LEFT)     in1 |= SI_IN1_P1_LEFT;
    if (mask & INPUT_RIGHT)    in1 |= SI_IN1_P1_RIGHT;
    s_machine.in1 = in1;
}

static bool si_plugin_is_rom_valid(void) {
    for (size_t i = 0; i < sizeof(space_invaders_rom); i++) {
        if (space_invaders_rom[i] != 0) return true;
    }
    return false;
}

#include "video/missing_rom_screen.h"

static void si_plugin_render_missing_rom(uint8_t *fb, uint32_t frame_count) {
    static const char *files[] = {
        "invaders.h (2KB @ 0x0000)",
        "invaders.g (2KB @ 0x0800)",
        "invaders.f (2KB @ 0x1000)",
        "invaders.e (2KB @ 0x1800)",
    };
    missing_rom_screen_render_dialog(fb, "** SPACE INVADERS ROMS REQUIRED **", "roms/invaders/",
                                     files, 4, frame_count);
}

const emulator_plugin_t g_space_invaders_plugin = {
    .id = "space_invaders",
    .name = "Space Invaders (1978)",
    .version = "1.4.0",
    .author = "Taito / Midway",
    .init = si_plugin_init,
    .reset = si_plugin_reset,
    .run_frame = si_plugin_run_frame,
    .render_frame = si_plugin_render_frame,
    .load_palette = si_plugin_load_palette,
    .render_audio = si_plugin_render_audio,
    .update_inputs = si_plugin_update_inputs,
    .is_rom_valid = si_plugin_is_rom_valid,
    .render_missing_rom = si_plugin_render_missing_rom,
};
