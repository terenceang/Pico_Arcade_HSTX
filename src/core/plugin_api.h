#ifndef PLUGIN_API_H
#define PLUGIN_API_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Modular Emulator Plugin Interface
 * 
 * ARCHITECTURAL INVARIANT:
 * - Every arcade game is an isolated plugin conforming to `emulator_plugin_t`.
 * - The host video and audio transport subsystems (src/video/, lib/pico_hdmi/)
 *   are rock-solid platform infrastructure and MUST NOT BE TOUCHED for game plugins.
 * - Plugins interact strictly through this interface: rendering to the 320x240 8bpp
 *   framebuffer (`render_frame`), configuring their 256-color palette (`load_palette`),
 *   and rendering 48 kHz stereo audio (`render_audio`).
 */

// Standard Input Bitmask for Arcade & Console Controls
typedef enum {
    INPUT_UP       = (1u << 0),
    INPUT_DOWN     = (1u << 1),
    INPUT_LEFT     = (1u << 2),
    INPUT_RIGHT    = (1u << 3),
    INPUT_BUTTON_A = (1u << 4), // Primary Action / Fire
    INPUT_BUTTON_B = (1u << 5), // Secondary Action / Jump
    INPUT_BUTTON_C = (1u << 6),
    INPUT_BUTTON_D = (1u << 7),
    INPUT_START_1P = (1u << 8),
    INPUT_START_2P = (1u << 9),
    INPUT_COIN     = (1u << 10),
    INPUT_SERVICE  = (1u << 11),
} emulator_input_mask_t;

typedef struct emulator_plugin {
    // Metadata
    const char *id;                // e.g. "space_invaders", "pacman"
    const char *name;              // e.g. "Space Invaders (1978)", "Pac-Man (1980)"
    const char *version;           // e.g. "1.1.0"
    const char *author;            // Copyright / Author

    // Lifecycle
    void (*init)(void);
    void (*reset)(void);

    // Frame Execution (Runs 1 60Hz frame worth of CPU / hardware emulation)
    void (*run_frame)(void);

    // Video Output (Renders current frame into 320x240 8bpp palettized framebuffer)
    void (*render_frame)(uint8_t *fb, uint32_t stride);

    // Palette Configuration (Loads the hardware 256-entry RGB888 palette)
    void (*load_palette)(void);

    // Audio Output (Renders up to `sample_count` stereo 48kHz 16-bit LPCM samples)
    void (*render_audio)(int16_t *buf_interleaved, uint32_t sample_count);

    // Input Handling
    void (*update_inputs)(uint32_t input_mask);

    // ROM Validation & Diagnostic Display
    bool (*is_rom_valid)(void);
    void (*render_missing_rom)(uint8_t *fb, uint32_t frame_count);
} emulator_plugin_t;

#endif // PLUGIN_API_H
