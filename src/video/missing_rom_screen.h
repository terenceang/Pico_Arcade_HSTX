#ifndef MISSING_ROM_SCREEN_H
#define MISSING_ROM_SCREEN_H

#include <stdint.h>
#include <stdbool.h>

// Generic helper to render on-screen text and missing ROM guide
void missing_rom_screen_draw_string(uint8_t *fb, int x, int y, const char *str, uint8_t color);

void missing_rom_screen_render_dialog(uint8_t *fb, const char *title, const char *path,
                                      const char **files, uint32_t file_count, uint32_t frame_count);

#endif // MISSING_ROM_SCREEN_H
