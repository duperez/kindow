#ifndef KINDOW_TIMING_H
#define KINDOW_TIMING_H

#include <time.h>

/*
 * Helper mínimo pra instrumentação de latência (a conta timespec->ms aparecia repetida em
 * todos os pontos medidos). Os logs de timing continuam ligados de propósito: latência ainda
 * é tema vivo do projeto (ver tabela medida em docs/findings/kindle-hardware-test.md) e o
 * custo de logar uma linha por frame é desprezível perto do refresh do e-ink.
 */

static inline struct timespec timing_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}

static inline long timing_elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

#endif
