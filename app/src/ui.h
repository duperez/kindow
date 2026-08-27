#ifndef KINDOW_UI_H
#define KINDOW_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "keyboard.h" /* KeyboardAction — as ações do menu atravessam a UI intactas */

/*
 * Adapter de apresentação: tudo que é GTK/GDK/Cairo vive atrás deste header — janela,
 * área de desenho, pintura do frame, e o teclado virtual (uma faixa fixa reservada na
 * parte de baixo da tela; a LÓGICA do teclado — layout, hit-test, sticky keys — mora no
 * módulo puro keyboard.c, aqui só desenho e roteamento de toque). Não conhece VNC nem
 * sessão: recebe frames prontos via ui_show_frame e devolve interação pelos callbacks —
 * toque na área do frame vira on_click (coordenadas cruas; validar/clamp é de quem
 * consome, que sabe o tamanho real do frame), toque na faixa do teclado vira on_key
 * (keysyms já resolvidos pelo módulo keyboard).
 */

typedef struct Ui Ui;

typedef void (*UiClickFn)(int x, int y, void *user_data);
typedef void (*UiKeyFn)(uint32_t keysym, bool down, void *user_data);
/* Ação da página de menu do teclado (sair, zoom...) — a UI só transporta; o significado
 * é decisão do wiring (main.c). */
typedef void (*UiActionFn)(KeyboardAction action, void *user_data);

/* Cria e mostra a janela em tela cheia, com a faixa do teclado já reservada. window_title
 * vem do chamador porque o formato é exigência da plataforma (ver
 * kindle_platform_window_title), não decisão de UI. */
Ui *ui_create(const char *window_title, UiClickFn on_click, UiKeyFn on_key,
              UiActionFn on_action, void *user_data);

/* Área útil pro frame remoto: a tela real (detectada em runtime — o mesmo binário serve
 * qualquer modelo de Kindle) MENOS a faixa reservada do teclado. É este tamanho que deve
 * ser pedido ao servidor como resolução remota. */
int ui_frame_width(const Ui *ui);
int ui_frame_height(const Ui *ui);

/* Copia o frame (ARGB32, ver vnc_client.h) pra surface interna e agenda o redraw só da
 * área do frame (a faixa do teclado não é tocada — importante no e-ink, refresh é caro).
 * O buffer pode ser liberado pelo chamador assim que a função retornar. */
void ui_show_frame(Ui *ui, int width, int height, const uint32_t *argb32_pixels);

void ui_destroy(Ui *ui);

#endif
