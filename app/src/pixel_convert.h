#ifndef KINDOW_PIXEL_CONVERT_H
#define KINDOW_PIXEL_CONVERT_H

#include <stdint.h>

/*
 * Módulo à parte, sem nenhum tipo de libvncclient — só descreve o formato de pixel com
 * campos primitivos (o mesmo tanto que rfbPixelFormat expõe pros canais RGB). Extraído de
 * vnc_client.c especificamente pra deixar a matemática de conversão de pixel testável como
 * unidade, sem precisar de um rfbClient real nem de conexão de rede.
 */
typedef struct {
    int bits_per_pixel; /* 8, 16 ou 32 */
    unsigned int red_shift, red_max;
    unsigned int green_shift, green_max;
    unsigned int blue_shift, blue_max;
} PixelFormat;

/* Converte um framebuffer raw (no formato de pixel descrito por `format`) pra um buffer
 * ARGB32 em escala de cinza (0xFF + luminância BT.601 replicada nos 3 canais). `out_pixels`
 * já deve vir alocado pelo chamador com width*height elementos. Lógica pura, sem I/O. */
void pixel_convert_to_grayscale_argb32(const uint8_t *frame_buffer, int width, int height,
                                        const PixelFormat *format, uint32_t *out_pixels);

#endif
