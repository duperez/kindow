#ifndef KINDOW_REMOTE_CONTROL_H
#define KINDOW_REMOTE_CONTROL_H

#include <stdbool.h>

/*
 * Cliente do kindow-helperd (pi/kindow-helperd): o canal lateral de comando que o RFB não
 * cobre — hoje, só o zoom (Xft/DPI da sessão remota, aplicado ao vivo via xsettingsd).
 * Protocolo de uma linha por conexão TCP na porta 5910: "dpi <n>" / "get" -> "ok ..." ou
 * "err ...".
 *
 * As chamadas são bloqueantes (socket com timeout de poucos segundos) e rodam no thread
 * do GTK — aceitável porque são disparadas por toque explícito do usuário no menu, contra
 * um host que já está servindo a sessão VNC (se o Pi caiu, a sessão já caiu junto e o
 * watch de fd está cuidando disso).
 */

/* Os três controles de zoom independentes (mesmos nomes do protocolo do helperd):
 * dpi = conteúdo dos apps, deco = decoração de janela, panel = painel. */
typedef struct {
    int dpi;
    int deco;
    int panel;
} RemoteZoom;

/* Pede ao helperd os três valores atuais (persistidos entre sessões nos configs do Pi).
 * false se o helperd não respondeu — *out fica intocado. */
bool remote_control_get(const char *host, RemoteZoom *out);

/* Manda o Pi aplicar `value` no controle dado ("dpi", "deco" ou "panel"), ao vivo.
 * true só se o helperd confirmou com "ok". */
bool remote_control_set(const char *host, const char *control, int value);

#endif
