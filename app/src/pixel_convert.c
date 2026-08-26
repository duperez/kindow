#include "pixel_convert.h"

#include <stddef.h>

void pixel_convert_to_grayscale_argb32(const uint8_t *frame_buffer, int width, int height,
                                        const PixelFormat *format, uint32_t *out_pixels) {
    int bpp = format->bits_per_pixel / 8;

    for (int y = 0; y < height; y++) {
        const uint8_t *row = frame_buffer + (size_t)y * width * bpp;
        for (int x = 0; x < width; x++) {
            const uint8_t *p = row + (size_t)x * bpp;
            unsigned int v;
            switch (bpp) {
                case 4: v = *(const uint32_t *)p; break;
                case 2: v = *(const uint16_t *)p; break;
                default: v = *p; break;
            }
            unsigned int r = ((v >> format->red_shift) & format->red_max) * 255 / format->red_max;
            unsigned int g =
                ((v >> format->green_shift) & format->green_max) * 255 / format->green_max;
            unsigned int b =
                ((v >> format->blue_shift) & format->blue_max) * 255 / format->blue_max;
            unsigned int gray = (r * 299 + g * 587 + b * 114) / 1000;
            out_pixels[y * width + x] = (0xFFu << 24) | (gray << 16) | (gray << 8) | gray;
        }
    }
}
