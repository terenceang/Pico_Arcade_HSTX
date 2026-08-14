#ifndef DEBUG_OVERLAY_H
#define DEBUG_OVERLAY_H

#include <stdint.h>

// Draws a small "<hdmi_fps>" readout into the top-left corner of an 8bpp
// framebuffer, using a minimal built-in 3x5 digit font - see
// display_config.h's DEBUG_HDMI_STATUS_OVERLAY. hdmi_fps is pico_hdmi's
// actual measured vsync rate (dvi_display_get_video_frame_count()'s advance
// over the last ~1s), not Core 0's software-paced frame rate - showing this
// live on screen catches the HDMI sync-loss bug's symptom (a spike well
// above 60) without needing a serial connection at all. Call once per
// frame, after rendering game content and before presenting the frame, so
// the overlay draws on top.
void debug_overlay_draw_hdmi_status(uint8_t *frame_buf, uint32_t hdmi_fps);

#endif // DEBUG_OVERLAY_H
