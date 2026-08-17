#ifndef SI_CONFIG_H
#define SI_CONFIG_H

// ============================================================================
// 1. Cabinet & Display Orientation Settings
// ============================================================================
#define SI_CABINET_UPRIGHT   0
#define SI_CABINET_COCKTAIL  1

#ifndef SI_CABINET_TYPE
#define SI_CABINET_TYPE SI_CABINET_UPRIGHT
#endif

#ifndef SI_DISPLAY_ROTATION
#define SI_DISPLAY_ROTATION 0 // 0, 90, 180, 270 degrees
#endif

#ifndef SI_DISPLAY_FLIP_H
#define SI_DISPLAY_FLIP_H 180
#endif

#ifndef SI_DISPLAY_FLIP_V
#define SI_DISPLAY_FLIP_V 0
#endif

#ifndef SI_SCREEN_OFFSET_X
#define SI_SCREEN_OFFSET_X 0
#endif

#ifndef SI_SCREEN_OFFSET_Y
#define SI_SCREEN_OFFSET_Y 0
#endif

#if SI_DISPLAY_ROTATION != 0 && SI_DISPLAY_ROTATION != 90 && \
    SI_DISPLAY_ROTATION != 180 && SI_DISPLAY_ROTATION != 270
#error "SI_DISPLAY_ROTATION must be 0, 90, 180, or 270"
#endif

// Color overlay filters (simulating arcade cellophane gel strips)
#ifndef SI_ENABLE_COLOR_OVERLAY
#define SI_ENABLE_COLOR_OVERLAY 1
#endif

// ============================================================================
// 2. Display Scaling Mode
// ============================================================================
#define SI_SCALE_NONE 0 // 1:1 unscaled native arcade
#define SI_SCALE_FIT  1 // Uniform aspect ratio fit to 320x240
#define SI_SCALE_X    2 // Stretch horizontal width
#define SI_SCALE_Y    3 // Stretch vertical height

#ifndef SI_SCALE_MODE
#define SI_SCALE_MODE SI_SCALE_FIT
#endif

// ============================================================================
// 3. Arcade DIP Switches (Gameplay & Difficulty)
// ============================================================================
// Starting Ships / Lives:
#define SI_SHIPS_3 0
#define SI_SHIPS_4 1
#define SI_SHIPS_5 2
#define SI_SHIPS_6 3

#ifndef SI_DIP_SHIPS
#define SI_DIP_SHIPS SI_SHIPS_3
#endif

// Extra Ship Bonus Threshold:
#define SI_BONUS_1500_PTS 0
#define SI_BONUS_1000_PTS 1

#ifndef SI_DIP_EXTRA_SHIP_1000
#define SI_DIP_EXTRA_SHIP_1000 SI_BONUS_1500_PTS
#endif

// Coin information display during attract demo:
#define SI_COIN_INFO_SHOW 0
#define SI_COIN_INFO_HIDE 1

#ifndef SI_DIP_COIN_INFO_DEMO
#define SI_DIP_COIN_INFO_DEMO SI_COIN_INFO_SHOW
#endif

// ============================================================================
// 4. CPU & Emulation Pacing Configuration
// ============================================================================
// Target CPU cycle budget per frame:
//   33280 : Exact 1.9968 MHz real-time CPU speed at 60.0 Hz DVI refresh (1996800 / 60)
//   33536 : Authentic arcade video timing (262 lines * 128 cycles @ 59.542 Hz)
#ifndef SI_TARGET_CYCLES_PER_FRAME
#define SI_TARGET_CYCLES_PER_FRAME 33280
#endif

#endif // SI_CONFIG_H
