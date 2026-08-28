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

/* Diretório de dados persistentes do app (partição de usuário /mnt/us, sobrevive a
 * reboot e atualização de firmware — diferente do resto do filesystem no jailbreak).
 * Cria o diretório se ainda não existir (idempotente, seguro chamar toda vez que for
 * usar). String estática — não liberar. */
const char *kindle_platform_data_dir(void);

/* true se o idioma configurado no Kindle é português. Fonte: a variável de ambiente
 * KINDOW_LANG ("pt"/"en" — override de teste, vence tudo), senão o LANG de
 * /var/local/system/locale (onde o firmware guarda o idioma escolhido nas
 * configurações — verificado no device real, 27/08). Qualquer outra língua, arquivo
 * ausente ou ilegível => false (inglês é o padrão do app). */
bool kindle_platform_language_is_portuguese(void);

#endif
