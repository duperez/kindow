#ifndef KINDOW_VNC_CLIENT_H
#define KINDOW_VNC_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Único ponto de contato com libvncclient no projeto — ver princípio de isolamento em
 * docs/findings/libvncclient-api.md. Nada fora deste par .h/.c inclui rfb/rfbclient.h.
 *
 * Modelo de conexão: sem estado persistente entre interações (decisão em
 * docs/findings/rfb-protocol.md — reconectar a cada interação, não manter socket ocioso).
 * Cada chamador faz connect -> fetch_frame -> [send_pointer/send_key -> fetch_frame]* -> disconnect.
 */

typedef struct VncClient VncClient;

/* Buffer já convertido pra escala de cinza, pronto pra virar uma cairo_surface_t
 * (CAIRO_FORMAT_RGB24/ARGB32: um uint32_t por pixel, 0xAARRGGBB nativo do host). */
typedef void (*VncFrameReadyFn)(int width, int height, const uint32_t *argb32_pixels,
                                 void *user_data);

/* Conecta e faz o handshake RFB completo. Retorna NULL em caso de erro, preenchendo
 * *out_error com uma mensagem (chamador deve dar free()). */
VncClient *vnc_client_connect(const char *host, int port, char **out_error);

/* Pede um framebuffer completo (incremental=0, já que é sempre uma reconexão nova) e
 * bloqueia até a atualização chegar, chamando on_frame com o resultado convertido. */
bool vnc_client_fetch_frame(VncClient *client, VncFrameReadyFn on_frame, void *user_data,
                             char **out_error);

void vnc_client_send_pointer(VncClient *client, int x, int y, int button_mask);

void vnc_client_disconnect(VncClient *client);

#endif
