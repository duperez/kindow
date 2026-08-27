#ifndef KINDOW_KINDLE_PLATFORM_H
#define KINDOW_KINDLE_PLATFORM_H

#include <stdbool.h>

/*
 * Adapter da plataforma Kindle — espelho, do lado do device, do princípio de isolamento que
 * vnc_client.h aplica à libvncclient: todo conhecimento específico do firmware do Kindle
 * (lipc, o window manager Awesome, seus esquemas mágicos) fica atrás deste header. Nada
 * fora deste par .h/.c monta comando lipc nem conhece o formato de título de janela.
 */

/* Liga/desliga a trava de screensaver do Kindle (com true o device não dorme). Chamar com
 * true no início do app e false em TODO caminho de saída — se o processo morrer sem passar
 * pelo false (só SIGKILL causa isso), a trava fica presa até reboot ou reversão manual. */
void kindle_platform_keep_awake(bool awake);

/* Título de janela que o window manager do Kindle exige pra mapear o app em tela cheia.
 * String estática — não liberar. */
const char *kindle_platform_window_title(void);

#endif
