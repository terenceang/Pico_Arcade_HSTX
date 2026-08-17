#ifndef PACMAN_CONFIG_H
#define PACMAN_CONFIG_H

// ============================================================================
// 1. Cabinet & Display Orientation Settings
// ============================================================================
#define PACMAN_CABINET_UPRIGHT   1 // Standard vertical arcade cabinet
#define PACMAN_CABINET_COCKTAIL  0 // Sit-down table cabinet (flips 180° for 2P)

#ifndef PACMAN_CABINET_TYPE
#define PACMAN_CABINET_TYPE PACMAN_CABINET_UPRIGHT
#endif

#ifndef PACMAN_DISPLAY_ROTATION
#define PACMAN_DISPLAY_ROTATION 270 // 0, 90, 180, 270 degrees - 270 = Upright Vertical Portrait Mode
#endif

#ifndef PACMAN_DISPLAY_FLIP_H
#define PACMAN_DISPLAY_FLIP_H 0
#endif

#ifndef PACMAN_DISPLAY_FLIP_V
#define PACMAN_DISPLAY_FLIP_V 0
#endif

#ifndef PACMAN_SCREEN_OFFSET_X
#define PACMAN_SCREEN_OFFSET_X 0
#endif

#ifndef PACMAN_SCREEN_OFFSET_Y
#define PACMAN_SCREEN_OFFSET_Y 0
#endif

#if PACMAN_DISPLAY_ROTATION != 0 && PACMAN_DISPLAY_ROTATION != 90 && \
    PACMAN_DISPLAY_ROTATION != 180 && PACMAN_DISPLAY_ROTATION != 270
#error "PACMAN_DISPLAY_ROTATION must be 0, 90, 180, or 270"
#endif

// ============================================================================
// 2. Arcade DIP Switches (Gameplay & Difficulty)
// ============================================================================
// Starting Lives:
#define PACMAN_LIVES_1 0
#define PACMAN_LIVES_2 1
#define PACMAN_LIVES_3 2
#define PACMAN_LIVES_5 3

#ifndef PACMAN_DIP_LIVES
#define PACMAN_DIP_LIVES PACMAN_LIVES_3
#endif

// Bonus Life Threshold:
#define PACMAN_BONUS_10000 0
#define PACMAN_BONUS_15000 1
#define PACMAN_BONUS_20000 2
#define PACMAN_BONUS_NONE  3

#ifndef PACMAN_DIP_BONUS_LIFE
#define PACMAN_DIP_BONUS_LIFE PACMAN_BONUS_10000
#endif

// Difficulty:
#define PACMAN_DIFFICULTY_HARD   0
#define PACMAN_DIFFICULTY_NORMAL 1

#ifndef PACMAN_DIP_DIFFICULTY_NORMAL
#define PACMAN_DIP_DIFFICULTY_NORMAL PACMAN_DIFFICULTY_NORMAL
#endif

// Ghost Names:
#define PACMAN_GHOST_NAMES_ALTERNATE 0
#define PACMAN_GHOST_NAMES_NORMAL    1

#ifndef PACMAN_DIP_GHOST_NAMES_NORMAL
#define PACMAN_DIP_GHOST_NAMES_NORMAL PACMAN_GHOST_NAMES_NORMAL
#endif

// ============================================================================
// 3. Audio & Attract Mode Settings
// ============================================================================
// 1 = Sound effects & siren enabled during attract demo mode
// 0 = Authentic arcade silent attract mode (sound only during gameplay)
#ifndef PACMAN_ENABLE_ATTRACT_SOUND
#define PACMAN_ENABLE_ATTRACT_SOUND 1
#endif

// ============================================================================
// 4. CPU & Emulation Pacing Configuration
// ============================================================================
// Target CPU cycle budget per frame:
//   51200 : Exact 3.072 MHz real-time CPU speed at 60.0 Hz DVI refresh (3072000 / 60)
//   50688 : Authentic arcade video timing (264 lines * 192 cycles @ 60.606 Hz)
#ifndef PACMAN_TARGET_CYCLES_PER_FRAME
#define PACMAN_TARGET_CYCLES_PER_FRAME 51200
#endif

#endif // PACMAN_CONFIG_H
