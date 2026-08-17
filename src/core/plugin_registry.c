#include "plugin_registry.h"
#include <string.h>
#include <stddef.h>

// Forward declare available plugins
extern const emulator_plugin_t g_space_invaders_plugin;
extern const emulator_plugin_t g_pacman_plugin;

static const emulator_plugin_t *s_plugins[] = {
    &g_space_invaders_plugin,
    &g_pacman_plugin,
};

#define PLUGIN_COUNT (sizeof(s_plugins) / sizeof(s_plugins[0]))

static uint32_t s_active_index = DEFAULT_PLUGIN_ID;

void plugin_registry_init(void) {
    if (s_active_index >= PLUGIN_COUNT) {
        s_active_index = 0;
    }
}

uint32_t plugin_registry_get_count(void) {
    return PLUGIN_COUNT;
}

const emulator_plugin_t *plugin_registry_get(uint32_t index) {
    if (index < PLUGIN_COUNT) {
        return s_plugins[index];
    }
    return NULL;
}

const emulator_plugin_t *plugin_registry_get_by_id(const char *id) {
    if (!id) return NULL;
    for (uint32_t i = 0; i < PLUGIN_COUNT; i++) {
        if (s_plugins[i] && s_plugins[i]->id && strcmp(s_plugins[i]->id, id) == 0) {
            return s_plugins[i];
        }
    }
    return NULL;
}

const emulator_plugin_t *plugin_registry_get_active(void) {
    if (s_active_index < PLUGIN_COUNT) {
        return s_plugins[s_active_index];
    }
    return s_plugins[0];
}

bool plugin_registry_set_active(uint32_t index) {
    if (index < PLUGIN_COUNT) {
        s_active_index = index;
        return true;
    }
    return false;
}

bool plugin_registry_set_active_by_id(const char *id) {
    for (uint32_t i = 0; i < PLUGIN_COUNT; i++) {
        if (s_plugins[i] && s_plugins[i]->id && strcmp(s_plugins[i]->id, id) == 0) {
            s_active_index = i;
            return true;
        }
    }
    return false;
}
