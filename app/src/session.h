#ifndef KINDOW_SESSION_H
#define KINDOW_SESSION_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Núcleo da aplicação: o ciclo de vida da sessão VNC — conectar, manter o push de
 * atualizações rodando, reconectar sozinho se a conexão cair (WiFi oscilando, Pi
 * reiniciando), pedir o redimensionamento da tela remota pro tamanho-alvo, e traduzir
 * cliques em PointerEvents válidos.
 *
 * Fronteiras (Ports & Adapters, versão leve): este módulo fala com o protocolo só através
 * de vnc_client.h e usa GLib pelo papel de event loop do processo (g_timeout_add,
 * g_io_add_watch) — mas NÃO conhece GTK, GDK nem Cairo. O que fazer com um frame pronto é
 * decisão do chamador, via callback.
 */

typedef struct Session Session;

typedef struct {
    /* Frame completo pronto (já convertido pra escala de cinza ARGB32, ver vnc_client.h).
     * O buffer só é válido durante a chamada — copiar se precisar guardar. */
    void (*on_frame)(int width, int height, const uint32_t *argb32_pixels, void *user_data);
    void *user_data;
} SessionCallbacks;

/* Começa a sessão contra host:port. target_width/height é o tamanho real da tela local: se
 * o servidor estiver com outra resolução, a sessão pede o resize sozinha (uma vez por
 * conexão) assim que o primeiro frame chegar. Se a conexão falhar agora ou cair depois, a
 * sessão fica re-tentando sozinha a cada 2s — por isso nunca retorna NULL por falha de
 * rede, só por falta de memória. */
Session *session_start(const char *host, int port, int target_width, int target_height,
                       SessionCallbacks callbacks);

/* Clique esquerdo (press+release) na posição dada, em coordenadas do frame. Ignorado em
 * silêncio se não há conexão, nenhum frame chegou ainda, ou a posição cai fora do frame
 * atual (coordenadas inválidas não devem chegar ao servidor). */
void session_send_click(Session *session, int x, int y);

/* Evento de tecla (keysym X11; down=true pressiona, false solta). Ignorado em silêncio se
 * não há conexão — diferente do clique, não depende de frame nenhum ter chegado (tecla não
 * carrega coordenada pra validar). */
void session_send_key(Session *session, uint32_t keysym, bool down);

/* Imprime no stderr o estado atual (conectado? tamanho do último frame?) — gatilho de
 * debug, ver o handler de SIGHUP em main.c. */
void session_log_status(const Session *session);

/* Encerra a conexão e libera tudo, inclusive timers/watches pendentes de reconexão. */
void session_shutdown(Session *session);

#endif
