/*
 * Teste unitário da matemática pura de conversão de pixel em pixel_convert.c (extraída de
 * vnc_client.c's convert_and_emit especificamente pra isso). Sem GTK, sem libvncclient, sem
 * rede — só a conta de shift/max -> escala de cinza ARGB32.
 *
 * Convenção do projeto: sem framework de teste externo (Unity/cmocka), assert() + exit(1) em
 * caso de falha, registrado como test() do Meson.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pixel_convert.h"

/* Formato "padrão" de 32bpp que rfbGetClient(8,3,4) negocia por padrão: 8 bits por canal,
 * red no byte do meio-alto, green no meio-baixo, blue no baixo (layout 0x00RRGGBB). */
static const PixelFormat kFormat32 = {
    .bits_per_pixel = 32,
    .red_shift = 16,
    .red_max = 255,
    .green_shift = 8,
    .green_max = 255,
    .blue_shift = 0,
    .blue_max = 255,
};

/* RGB565 — formato comum de 16bpp, onde redMax/greenMax/blueMax são bem menores que 255,
 * o que exercita a parte de escala (`* 255 / max`) que em 32bpp de 8-bit por canal é sempre
 * um no-op (max já é 255). */
static const PixelFormat kFormat16_565 = {
    .bits_per_pixel = 16,
    .red_shift = 11,
    .red_max = 31,
    .green_shift = 5,
    .green_max = 63,
    .blue_shift = 0,
    .blue_max = 31,
};

static uint32_t convert_single_pixel_32(uint32_t raw_pixel, const PixelFormat *format) {
    uint32_t out = 0;
    pixel_convert_to_grayscale_argb32((const uint8_t *)&raw_pixel, 1, 1, format, &out);
    return out;
}

static uint32_t convert_single_pixel_16(uint16_t raw_pixel, const PixelFormat *format) {
    uint32_t out = 0;
    pixel_convert_to_grayscale_argb32((const uint8_t *)&raw_pixel, 1, 1, format, &out);
    return out;
}

static void assert_gray_pixel(uint32_t argb, unsigned int expected_gray) {
    unsigned int a = (argb >> 24) & 0xFF;
    unsigned int r = (argb >> 16) & 0xFF;
    unsigned int g = (argb >> 8) & 0xFF;
    unsigned int b = argb & 0xFF;
    assert(a == 0xFF);
    assert(r == expected_gray);
    assert(g == expected_gray);
    assert(b == expected_gray);
}

static void test_white_pixel_becomes_gray_255(void) {
    uint32_t raw = (255u << 16) | (255u << 8) | 255u; /* branco puro, 32bpp */
    uint32_t out = convert_single_pixel_32(raw, &kFormat32);
    assert_gray_pixel(out, 255);
}

static void test_black_pixel_becomes_gray_0(void) {
    uint32_t raw = 0;
    uint32_t out = convert_single_pixel_32(raw, &kFormat32);
    assert_gray_pixel(out, 0);
}

static void test_pure_red_uses_luminance_formula(void) {
    /* 299/587/114 aplicado a (255,0,0): (255*299)/1000 = 76 (truncado) */
    uint32_t raw = (255u << 16) | (0u << 8) | 0u;
    uint32_t out = convert_single_pixel_32(raw, &kFormat32);
    assert_gray_pixel(out, 76);
}

static void test_pure_green_uses_luminance_formula(void) {
    /* (255*587)/1000 = 149 (truncado) */
    uint32_t raw = (0u << 16) | (255u << 8) | 0u;
    uint32_t out = convert_single_pixel_32(raw, &kFormat32);
    assert_gray_pixel(out, 149);
}

static void test_pure_blue_uses_luminance_formula(void) {
    /* (255*114)/1000 = 29 (truncado) */
    uint32_t raw = (0u << 16) | (0u << 8) | 255u;
    uint32_t out = convert_single_pixel_32(raw, &kFormat32);
    assert_gray_pixel(out, 29);
}

static void test_16bpp_white_scales_to_255(void) {
    /* extremos de canal (redMax/greenMax/blueMax) devem virar 255, mesmo em 16bpp */
    uint16_t raw = (uint16_t)((31u << 11) | (63u << 5) | 31u);
    uint32_t out = convert_single_pixel_16(raw, &kFormat16_565);
    assert_gray_pixel(out, 255);
}

static void test_16bpp_black_scales_to_0(void) {
    uint16_t raw = 0;
    uint32_t out = convert_single_pixel_16(raw, &kFormat16_565);
    assert_gray_pixel(out, 0);
}

static void test_16bpp_midtone_scales_proportionally(void) {
    /* red em 15/31 (não é extremo) exercita de fato o `* 255 / max`, não só os casos
     * triviais 0 e max que passariam mesmo com uma escala quebrada.
     * r = 15 * 255 / 31 = 123 (truncado); g e b ficam em 0.
     * gray = (123*299)/1000 = 36 (truncado) */
    uint16_t raw = (uint16_t)(15u << 11);
    uint32_t out = convert_single_pixel_16(raw, &kFormat16_565);
    assert_gray_pixel(out, 36);
}

static void test_8bpp_pixel_reads_single_byte(void) {
    /* formato de 8bpp (paleta/grayscale de 1 byte por pixel) exercita o branch `default: v = *p`
     * -- redMax=greenMax=blueMax=7/7/3 com todos os bits ligados simula um "branco" de 8bpp
     * (RGB332), que deve virar cinza 255. */
    PixelFormat format8 = {
        .bits_per_pixel = 8,
        .red_shift = 5,
        .red_max = 7,
        .green_shift = 2,
        .green_max = 7,
        .blue_shift = 0,
        .blue_max = 3,
    };
    uint8_t raw = (uint8_t)((7u << 5) | (7u << 2) | 3u);
    uint32_t out = 0;
    pixel_convert_to_grayscale_argb32(&raw, 1, 1, &format8, &out);
    assert_gray_pixel(out, 255);
}

static void test_multi_pixel_row_and_width_stride(void) {
    /* 2x1: garante que o segundo pixel é lido do offset certo (largura * bpp), não
     * hardcoded pra 1 pixel só. */
    uint32_t raw[2] = {
        (255u << 16) | (255u << 8) | 255u, /* branco */
        0u,                                 /* preto */
    };
    uint32_t out[2] = {0, 0};
    pixel_convert_to_grayscale_argb32((const uint8_t *)raw, 2, 1, &kFormat32, out);
    assert_gray_pixel(out[0], 255);
    assert_gray_pixel(out[1], 0);
}

int main(void) {
    test_white_pixel_becomes_gray_255();
    test_black_pixel_becomes_gray_0();
    test_pure_red_uses_luminance_formula();
    test_pure_green_uses_luminance_formula();
    test_pure_blue_uses_luminance_formula();
    test_16bpp_white_scales_to_255();
    test_16bpp_black_scales_to_0();
    test_16bpp_midtone_scales_proportionally();
    test_8bpp_pixel_reads_single_byte();
    test_multi_pixel_row_and_width_stride();

    printf("test_pixel_convert: todos os testes passaram\n");
    return 0;
}
