#ifndef MAIN_H
#define MAIN_H

// ============================================================================
// 1. Central Game Selection
// ============================================================================
// Select which arcade emulator plugin runs on boot:
//   GAME_SPACE_INVADERS (0) : Space Invaders (1978, Midway/Taito 8080)
//   GAME_PACMAN         (1) : Pac-Man (1980, Namco/Midway Z80)
#define GAME_SPACE_INVADERS  0
#define GAME_PACMAN          1

#ifndef ACTIVE_GAME
#define ACTIVE_GAME GAME_PACMAN
#endif

// ============================================================================
// 2. Boot & Sync Settings
// ============================================================================
// Number of black sync frames transmitted at boot before game emulation begins.
// Gives HDMI monitors and TVs time to acquire pixel clock and sync lock.
// 60 frames = 1.0 second at 60 Hz. Set to 0 to disable.
#ifndef BOOT_SYNC_FRAMES
#define BOOT_SYNC_FRAMES 60
#endif

// ============================================================================
// 3. Platform Diagnostic & Test Modes
// ============================================================================
// Test card pattern: shows the color-bar/grayscale test pattern at boot.
// Set to 0 to show permanently, or N > 0 to show for N seconds. Set 0 to disable.
#ifndef DEBUG_TESTCARD
#define DEBUG_TESTCARD 0
#endif

#ifndef DEBUG_TESTCARD_SECONDS
#define DEBUG_TESTCARD_SECONDS 3
#endif

// Audio test tone: set to 1 to play a continuous 1kHz tone during test card, 0 to disable.
#ifndef DEBUG_AUDIO_TEST_TONE
#define DEBUG_AUDIO_TEST_TONE 0
#endif

// Controller test card: shows a live SNES pad diagram (each button lights up
// green while held, with a keypress beep) instead of the game, for verifying
// wiring/mapping. Shown permanently once DEBUG_TESTCARD's window (if any) ends.
// Set to 0 to disable and run the game normally.
#ifndef DEBUG_CONTROLLER_TESTCARD
#define DEBUG_CONTROLLER_TESTCARD 0
#endif

// ============================================================================
// 4. Screen Capture Compile Option
// ============================================================================
// When set to 0 (default for normal gameplay): screen capture routines and
// overheads are completely compiled out, ensuring zero impact on gameplay speed.
// When set to 1: enables runtime framebuffer capture over USB serial.
#ifndef ENABLE_SCREEN_CAPTURE
#define ENABLE_SCREEN_CAPTURE 0
#endif

// ============================================================================
// 5. Framerate & Performance Profiler
// ============================================================================
// When set to 1: samples and prints FPS, CPU emulation time, and video render
// time to the USB serial monitor at the specified interval.
#ifndef DEBUG_FPS_MONITOR
#define DEBUG_FPS_MONITOR 1
#endif

#ifndef DEBUG_FPS_INTERVAL_SECONDS
#define DEBUG_FPS_INTERVAL_SECONDS 5 // 5 seconds
#endif

#endif // MAIN_H
