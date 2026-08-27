#ifndef KINDOW_UI_H
#define KINDOW_UI_H

#include <stdint.h>

/*
 * Adapter de apresentação: tudo que é GTK/GDK/Cairo vive atrás deste header — janela,
 * área de desenho, pintura do frame e captura de toque. Não conhece VNC nem sessão:
 * recebe frames prontos via ui_show_frame e devolve toques crus pelo callback de clique
 * (em coordenadas de pixel da área de desenho — validar/clamp é responsabilidade de quem
 * consome, que é quem sabe o tamanho real do frame).
 */

typedef struct Ui Ui;

typedef void (*UiClickFn)(int x, int y, void *user_data);

/* Cria e mostra a janela em tela cheia. window_title vem do chamador porque o formato é
 * exigência da plataforma (ver kindle_platform_window_title), não decisão de UI. */
Ui *ui_create(const char *window_title, UiClickFn on_click, void *user_data);

/* Resolução real da tela detectada em runtime — o mesmo binário serve qualquer modelo de
 * Kindle que conectar, sem resolução hardcoded. */
int ui_screen_width(const Ui *ui);
int ui_screen_height(const Ui *ui);

/* Copia o frame (ARGB32, ver vnc_client.h) pra surface interna e agenda o redraw. O buffer
 * pode ser liberado pelo chamador assim que a função retornar. */
void ui_show_frame(Ui *ui, int width, int height, const uint32_t *argb32_pixels);

void ui_destroy(Ui *ui);

#endif
