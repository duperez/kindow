#ifndef KINDOW_VNC_CLIENT_H
#define KINDOW_VNC_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Único ponto de contato com libvncclient no projeto — ver princípio de isolamento em
 * docs/findings/libvncclient-api.md. Nada fora deste par .h/.c inclui rfb/rfbclient.h.
 *
 * Modelo de conexão: persistente pelo tempo de vida do app (revisão da decisão original em
 * docs/findings/rfb-protocol.md — motivada pelo custo real medido de reconectar a cada
 * interação, ver docs/findings/kindle-hardware-test.md). Fluxo esperado do chamador:
 *   1. vnc_client_connect
 *   2. vnc_client_get_fd, registrar no próprio loop de eventos (ex: g_unix_fd_add)
 *   3. vnc_client_start_updates uma única vez
 *   4. a cada vez que o fd sinalizar leitura, vnc_client_handle_messages
 *   5. vnc_client_send_pointer a qualquer momento, sem precisar re-pedir atualização —
 *      a mudança resultante chega sozinha pelo pedido incremental já em andamento
 *   6. vnc_client_disconnect ao encerrar
 */

typedef struct VncClient VncClient;

/* Buffer já convertido pra escala de cinza, pronto pra virar uma cairo_surface_t
 * (CAIRO_FORMAT_RGB24/ARGB32: um uint32_t por pixel, 0xAARRGGBB nativo do host). */
typedef void (*VncFrameReadyFn)(int width, int height, const uint32_t *argb32_pixels,
                                 void *user_data);

/* Conecta e faz o handshake RFB completo. Retorna NULL em caso de erro, preenchendo
 * *out_error com uma mensagem (chamador deve dar free()). */
VncClient *vnc_client_connect(const char *host, int port, char **out_error);

/* Fd do socket RFB já conectado, pra o chamador integrar no próprio loop de eventos e
 * saber quando chamar vnc_client_handle_messages. */
int vnc_client_get_fd(const VncClient *client);

/* Pede o primeiro framebuffer completo e liga o motor de push do protocolo: a partir daqui,
 * toda vez que vnc_client_handle_messages processar uma FramebufferUpdate, a própria
 * libvncclient dispara sozinha o próximo pedido incremental internamente (confirmado lendo
 * HandleRFBServerMessage em rfbclient.c ~linha 2564) — o servidor só responde de novo quando
 * o conteúdo realmente mudar. Chamar só uma vez por conexão, nunca de novo depois. */
bool vnc_client_start_updates(VncClient *client, char **out_error);

/* Processa uma leitura pendente no fd (chamar quando o loop de eventos sinalizar dado
 * disponível). Retorna true e chama on_frame se essa leitura fechou uma atualização de tela
 * com conteúdo real; false se não fechou ainda (inclui o burst vazio inicial do TigerVNC —
 * ver docs/findings/kindle-hardware-test.md, Achado #2) ou se não havia nada de novo. Erro
 * de protocolo/conexão vai em *out_error nesse caso, o chamador deve reconectar. */
bool vnc_client_handle_messages(VncClient *client, VncFrameReadyFn on_frame, void *user_data,
                                 char **out_error);

void vnc_client_send_pointer(VncClient *client, int x, int y, int button_mask);

/* Pede ao servidor pra redimensionar a área remota pro tamanho informado (extensão RFB
 * SetDesktopSize/ExtDesktopSize — o TigerVNC aplica via Xrandr). Só faz sentido chamar
 * depois que pelo menos um frame já foi processado nessa conexão; chamar mais de uma vez
 * por conexão é redundante (o pedido já converge sozinho, não precisa insistir). */
bool vnc_client_request_desktop_size(VncClient *client, int width, int height,
                                      char **out_error);

void vnc_client_disconnect(VncClient *client);

#endif
