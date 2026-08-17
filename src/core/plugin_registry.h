#ifndef PLUGIN_REGISTRY_H
#define PLUGIN_REGISTRY_H

#include "plugin_api.h"
#include "video/display_config.h"
#include "main.h"

// Available Plugin Identifiers mapped to main.h game constants
#define PLUGIN_ID_SPACE_INVADERS GAME_SPACE_INVADERS
#define PLUGIN_ID_PACMAN         GAME_PACMAN

#ifndef DEFAULT_PLUGIN_ID
#define DEFAULT_PLUGIN_ID ACTIVE_GAME
#endif

// Registry Functions
void plugin_registry_init(void);
uint32_t plugin_registry_get_count(void);
const emulator_plugin_t *plugin_registry_get(uint32_t index);
const emulator_plugin_t *plugin_registry_get_by_id(const char *id);

// Active Plugin Management
const emulator_plugin_t *plugin_registry_get_active(void);
bool plugin_registry_set_active(uint32_t index);
bool plugin_registry_set_active_by_id(const char *id);

#endif // PLUGIN_REGISTRY_H
