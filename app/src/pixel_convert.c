#include "pixel_convert.h"

#include <stddef.h>

/* red_max/green_max/blue_max são fixos pra conexão inteira (vêm do formato de pixel
 * negociado uma vez, não mudam frame a frame) — em vez de dividir a cada pixel (divisão
 * inteira é cara no Cortex-A9 do Kindle, sem hardware de divisão; medido em ~164ms pra um
 * frame de 1024x758, ver docs/findings/kindle-hardware-test.md), pré-calcula os max+1
 * resultados possíveis uma vez por frame e troca a divisão por leitura de tabela no loop
 * quente. Formatos reais de TrueColor (8/16/32 bpp) sempre têm max de canal <= 255 (8 bits)
 * — confirmado contra o TigerVNC de verdade (max red 255 green 255 blue 255); acima disso a
 * tabela fica zerada e a leitura é mascarada em 0xFF, então nunca estoura o array, só
 * degrada (formato fora do que este módulo suporta, não deveria acontecer na prática). */
static void build_channel_lut(unsigned char lut[256], unsigned int max) {
    if (max == 0 || max > 255) {
        return;
    }
    for (unsigned int i = 0; i <= max; i++) {
        lut[i] = (unsigned char)(i * 255 / max);
    }
}

/* bpp é o mesmo pro frame inteiro (decidido uma vez, não muda pixel a pixel) — o `switch`
 * original rodava dentro do loop mais quente mesmo assim, sempre com o mesmo resultado a
 * cada iteração. Aqui o dispatch acontece uma vez por linha (cast do ponteiro da linha),
 * não uma vez por pixel, tirando esse branch redundante do caminho crítico. */
#define KINDOW_GRAY_PIXEL(v)                                                            \
    do {                                                                                \
        unsigned int r = red_lut[((v) >> red_shift) & red_max & 0xFFu];                 \
        unsigned int g = green_lut[((v) >> green_shift) & green_max & 0xFFu];           \
        unsigned int b = blue_lut[((v) >> blue_shift) & blue_max & 0xFFu];              \
        unsigned int gray = (r * 299 + g * 587 + b * 114) / 1000;                       \
        *out_row++ = (0xFFu << 24) | (gray << 16) | (gray << 8) | gray;                 \
    } while (0)

void pixel_convert_to_grayscale_argb32(const uint8_t *frame_buffer, int width, int height,
                                        const PixelFormat *format, uint32_t *out_pixels) {
    int bpp = format->bits_per_pixel / 8;

    unsigned char red_lut[256] = {0};
    unsigned char green_lut[256] = {0};
    unsigned char blue_lut[256] = {0};
    build_channel_lut(red_lut, format->red_max);
    build_channel_lut(green_lut, format->green_max);
    build_channel_lut(blue_lut, format->blue_max);

    unsigned int red_shift = format->red_shift, red_max = format->red_max;
    unsigned int green_shift = format->green_shift, green_max = format->green_max;
    unsigned int blue_shift = format->blue_shift, blue_max = format->blue_max;

    if (bpp == 4) {
        for (int y = 0; y < height; y++) {
            const uint32_t *row = (const uint32_t *)(frame_buffer + (size_t)y * width * 4);
            uint32_t *out_row = out_pixels + (size_t)y * width;
            for (int x = 0; x < width; x++) {
                KINDOW_GRAY_PIXEL(row[x]);
            }
        }
    } else if (bpp == 2) {
        for (int y = 0; y < height; y++) {
            const uint16_t *row = (const uint16_t *)(frame_buffer + (size_t)y * width * 2);
            uint32_t *out_row = out_pixels + (size_t)y * width;
            for (int x = 0; x < width; x++) {
                KINDOW_GRAY_PIXEL(row[x]);
            }
        }
    } else {
        for (int y = 0; y < height; y++) {
            const uint8_t *row = frame_buffer + (size_t)y * width;
            uint32_t *out_row = out_pixels + (size_t)y * width;
            for (int x = 0; x < width; x++) {
                KINDOW_GRAY_PIXEL(row[x]);
            }
        }
    }
}

#undef KINDOW_GRAY_PIXEL
