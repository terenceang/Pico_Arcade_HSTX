#include "screen_capture.h"

#if ENABLE_SCREEN_CAPTURE

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "video/dvi_display.h"
#include "video/display_config.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFileHeader;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

void screen_capture_init(void) {
}

void screen_capture_dump_bmp(const uint8_t *fb) {
    if (!fb) return;
    int width = FRAME_WIDTH;
    int height = FRAME_HEIGHT;
    int row_stride = (width * 3 + 3) & ~3;
    uint32_t image_size = (uint32_t)row_stride * height;

    BMPFileHeader fh = { 0x4D42, 54 + image_size, 0, 0, 54 };
    BMPInfoHeader ih = { sizeof(BMPInfoHeader), width, -height, 1, 24, 0, image_size, 2835, 2835, 0, 0 };

    printf("\n=== BMP_START ===\n");
    const uint8_t *fh_bytes = (const uint8_t *)&fh;
    for (size_t i = 0; i < sizeof(fh); i++) putchar(fh_bytes[i]);
    const uint8_t *ih_bytes = (const uint8_t *)&ih;
    for (size_t i = 0; i < sizeof(ih); i++) putchar(ih_bytes[i]);

    uint8_t row_buf[320 * 3 + 4];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t pal_idx = fb[y * width + x];
            uint32_t rgb = dvi_display_get_palette_entry(pal_idx);
            row_buf[x * 3 + 0] = rgb & 0xFF;         // Blue
            row_buf[x * 3 + 1] = (rgb >> 8) & 0xFF;  // Green
            row_buf[x * 3 + 2] = (rgb >> 16) & 0xFF; // Red
        }
        for (int p = width * 3; p < row_stride; p++) row_buf[p] = 0;
        for (int i = 0; i < row_stride; i++) putchar(row_buf[i]);
    }
    printf("\n=== BMP_END ===\n");
}

void screen_capture_poll(const uint8_t *fb, uint32_t input_mask) {
    int ch = getchar_timeout_us(0);
    if (ch == 'c' || ch == 'C') {
        screen_capture_dump_bmp(fb);
    }
    (void)input_mask;
}

#endif // ENABLE_SCREEN_CAPTURE
