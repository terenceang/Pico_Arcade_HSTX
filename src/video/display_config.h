#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include "pico_hdmi/video_output.h"

// ============================================================================
// Platform Video & Framebuffer Geometry
// ============================================================================
// Standard internal framebuffer resolution (320x240 8bpp palettized),
// scanline-doubled to physical 640x480p60 HDMI wire output.
#define FRAME_WIDTH  320
#define FRAME_HEIGHT 240
#define DISPLAY_REFRESH_HZ 60

#include "main.h"

// Note: Boot, sync, debug, and testcard options are configured centrally in main.h.


// ============================================================================
// Standard Host Palette Indices (8-bit palettized mode)
// ============================================================================
#define COLOR_BLACK   0 // Black (0x000000)
#define COLOR_WHITE   1 // White (0xFFFFFF)
#define COLOR_YELLOW  2 // Yellow (0xFFFF00)
#define COLOR_CYAN    3 // Cyan (0x00FFFF)
#define COLOR_GREEN   4 // Green (0x00FF00)
#define COLOR_MAGENTA 5 // Magenta (0xFF00FF)
#define COLOR_RED     6 // Red (0xFF0000)
#define COLOR_BLUE    7 // Blue (0x0000FF)

#endif // DISPLAY_CONFIG_H
