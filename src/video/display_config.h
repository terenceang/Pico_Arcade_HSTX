#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include "pico_hdmi/video_output.h"

// Framebuffer resolution (640x480 8bpp palettized)
#define FRAME_WIDTH  640
#define FRAME_HEIGHT 480

// Fixed by the DVI timing this project always runs (640x480p60 - see
// Video.md). Used to convert seconds into frame counts.
#define DISPLAY_REFRESH_HZ 60

// Debug test card: at boot, main.c shows the colour-bar/grayscale test
// pattern (testcard.c). Set to 0 to show permanently, or N > 0 to show for N
// seconds before handing off to the game. Set DEBUG_TESTCARD 0 to disable.
#ifndef DEBUG_TESTCARD
#define DEBUG_TESTCARD 1
#endif
#ifndef DEBUG_TESTCARD_SECONDS
#define DEBUG_TESTCARD_SECONDS 0
#endif

// Debug controller test card: shows a live SNES-controller button diagram
// (controller_testcard.c) instead of the game - each button lights up green
// while held, for checking wiring/mapping without a serial console. Shown
// permanently while enabled (no timeout, unlike DEBUG_TESTCARD above) since
// bring-up testing needs time to press every button. Set to 0 to go
// straight into the game. Takes a back seat to DEBUG_TESTCARD above if both
// are enabled - the colour-bar card shows first, then this.
#ifndef DEBUG_CONTROLLER_TESTCARD
#define DEBUG_CONTROLLER_TESTCARD 0
#endif

// Debug audio test tone: set to 1 to play test tone during test card, 0 to disable.
#ifndef DEBUG_AUDIO_TEST_TONE
#define DEBUG_AUDIO_TEST_TONE 1
#endif

// Screen orientation, to match however the physical display is actually
// mounted - see Emulator.md's "Screen orientation" section. Neither of
// these has any effect on the DVI engine's own fixed 640x480p60 output
// timing (src/dvi/) - only on how src/game.c samples video RAM into the
// framebuffer.
//
// SI_DISPLAY_ROTATION: rotation applied to the image, in degrees
// clockwise. Must be 0, 90, 180, or 270 - anything else is a compile
// error. If the image comes out sideways, upside down, or mirrored on a
// different physical mount, that's what SI_DISPLAY_ROTATION and the two
// flip flags below are for: try the other rotation values and/or flips
// (16 combinations total) rather than editing the rendering code itself.
//
// 0 + SI_DISPLAY_FLIP_H (below) is the verified-correct setting for this
// board's actual mounted display - not the default starting guess this
// project shipped with initially.
#ifndef SI_DISPLAY_ROTATION
#define SI_DISPLAY_ROTATION 0
#endif

// Mirror the image horizontally/vertically, applied independently of
// SI_DISPLAY_ROTATION (before it, in the game's own un-rotated coordinate
// space) - covers cabinets/mounts that also flip the image, e.g. a mirror
// used to fold the optical path, or a display driven through a connector
// that inverts a scan direction.
#ifndef SI_DISPLAY_FLIP_H
#define SI_DISPLAY_FLIP_H 1
#endif
#ifndef SI_DISPLAY_FLIP_V
#define SI_DISPLAY_FLIP_V 0
#endif

#if SI_DISPLAY_ROTATION != 0 && SI_DISPLAY_ROTATION != 90 && \
    SI_DISPLAY_ROTATION != 180 && SI_DISPLAY_ROTATION != 270
#error "SI_DISPLAY_ROTATION must be 0, 90, 180, or 270"
#endif

// Classic arcade-cabinet color overlay (red/green tint over an otherwise
// monochrome image - see Emulator.md's "Color overlay" section). Set to 0
// for a plain white-on-black monochrome image instead, matching exactly
// what the real hardware's video RAM contains bit-for-bit with no tint
// applied - useful for checking the raw video output independent of the
// cosmetic overlay.
#ifndef SI_ENABLE_COLOR_OVERLAY
#define SI_ENABLE_COLOR_OVERLAY 1
#endif

// Shifts the game image within the 320x240 framebuffer, in pixels -
// positive X moves the image right, positive Y moves it down (negative
// values move it left/up). Applied on top of the SI_DISPLAY_ROTATION
// letterbox centering, independent of it - useful for nudging the image
// to compensate for a slightly misaligned bezel/mount or display
// overscan. Large values simply push part or all of the image off-screen
// (clipped, not an error).
#ifndef SI_SCREEN_OFFSET_X
#define SI_SCREEN_OFFSET_X 0
#endif
#ifndef SI_SCREEN_OFFSET_Y
#define SI_SCREEN_OFFSET_Y 10
#endif

// Scaling applied to the game image before centering/letterboxing within
// the 320x240 framebuffer - see Emulator.md's "Image scaling" section.
// SI_SCALE_NONE: 1:1, no scaling - this project's original behavior.
// SI_SCALE_FIT:  uniform scale, as large as possible while still fitting
//                both axes (classic letterboxed "contain" scaling).
// SI_SCALE_X:    scale to exactly fill the framebuffer's width; height
//                stays at its native (unscaled) size.
// SI_SCALE_Y:    scale to exactly fill the framebuffer's height; width
//                stays at its native (unscaled) size.
#define SI_SCALE_NONE 0
#define SI_SCALE_FIT  1
#define SI_SCALE_X    2
#define SI_SCALE_Y    3

#ifndef SI_SCALE_MODE
#define SI_SCALE_MODE SI_SCALE_FIT
#endif

#if SI_SCALE_MODE != SI_SCALE_NONE && SI_SCALE_MODE != SI_SCALE_FIT && \
    SI_SCALE_MODE != SI_SCALE_X && SI_SCALE_MODE != SI_SCALE_Y
#error "SI_SCALE_MODE must be SI_SCALE_NONE, SI_SCALE_FIT, SI_SCALE_X, or SI_SCALE_Y"
#endif

// ============================================================================
// Palette Index Definitions (8-bit palettized mode)
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
