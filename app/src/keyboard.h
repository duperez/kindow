#ifndef KINDOW_KEYBOARD_H
#define KINDOW_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Módulo puro do teclado virtual (mesmo espírito do pixel_convert: zero GTK, zero VNC —
 * testável como unidade). Dono de tudo que é LÓGICA de teclado: o layout (páginas de
 * letras/símbolos como dados), o hit-test (toque → tecla) e a máquina de estado dos sticky
 * modifiers (Shift/Ctrl armam pro próximo toque — padrão consagrado de teclado de toque,
 * já que segurar modificador + tocar outra tecla exigiria multi-touch confiável, que o
 * toque do Kindle não entrega).
 *
 * Quem DESENHA as teclas é o ui.c, lendo a geometria/rótulos via keyboard_key_view — este
 * módulo só diz onde cada tecla está e o que ela faz.
 *
 * Os keysyms são os do X11, que é o vocabulário do protocolo RFB (SendKeyEvent): pra
 * ASCII imprimível o keysym é o próprio código do caractere; teclas especiais usam a faixa
 * 0xFFxx (Enter 0xFF0D, Backspace 0xFF08, setas 0xFF51-0xFF54, etc.). O servidor (Xvnc)
 * resolve sozinho keysym → keycode+modificadores — mandar 'A' maiúsculo direto funciona,
 * sem precisar embrulhar em Shift down/up.
 */

typedef struct Keyboard Keyboard;

typedef struct {
    uint32_t keysym;
    bool down;
} KeyboardEvent;

/* Máximo de eventos que um toque único pode gerar: Ctrl down + tecla down + tecla up +
 * Ctrl up (quando o sticky Ctrl está armado). */
#define KEYBOARD_MAX_EVENTS 4

/* Ações locais do app, emitidas pela página de MENU do teclado (a faixa do teclado é o
 * único espaço de tela disponível sem roubar área do frame — decisão de 26/08: o menu é
 * uma terceira página, alcançada armando Ctrl+Shift e tocando a tecla de página, que
 * nesse estado mostra "Menu"). Diferente de um keysym, uma ação NÃO vai pro servidor
 * VNC — é o chamador (main.c) que decide o que ela significa. */
typedef enum {
    KEYBOARD_ACTION_NONE = 0,
    KEYBOARD_ACTION_QUIT,   /* sair do app local */
    KEYBOARD_ACTION_STATUS, /* logar estado da conexão (debug sem SSH) */
    /* Zoom remoto em três camadas INDEPENDENTES (via kindow-helperd no Pi — decisão de
     * 26/08): apps = conteúdo (Xft/DPI via xsettingsd), deco = decoração de janela
     * (titlebar/botões do Openbox), panel = painel (fontes do tint2). */
    KEYBOARD_ACTION_ZOOM_APPS_IN,
    KEYBOARD_ACTION_ZOOM_APPS_OUT,
    KEYBOARD_ACTION_ZOOM_DECO_IN,
    KEYBOARD_ACTION_ZOOM_DECO_OUT,
    KEYBOARD_ACTION_ZOOM_PANEL_IN,
    KEYBOARD_ACTION_ZOOM_PANEL_OUT,
} KeyboardAction;

/* Visão de uma tecla pra desenho: retângulo em pixels (relativo à área do teclado),
 * rótulo e se deve ser destacada (modificador armado / chord de menu disponível). */
typedef struct {
    int x, y, w, h;
    const char *label;
    bool highlighted;
} KeyboardKeyView;

/* Cria o teclado pra uma área de width_px x height_px (a geometria das teclas é calculada
 * uma vez aqui, proporcional — serve qualquer resolução de Kindle). NULL se sem memória. */
Keyboard *keyboard_create(int width_px, int height_px);

/* Quantas teclas existem na página ATUAL (muda quando o usuário alterna letras/símbolos). */
int keyboard_key_count(const Keyboard *keyboard);

/* Tecla de índice 0 <= index < keyboard_key_count() da página atual. */
KeyboardKeyView keyboard_key_view(const Keyboard *keyboard, int index);

/* Índice (na página atual) da tecla sob (x, y), ou -1 se o toque não caiu em tecla.
 * Consulta pura, sem efeito — existe pro chamador saber QUAL tecla foi tocada (ex.: o
 * flash de feedback visual do ui.c), separada do handle_tap que muda estado. */
int keyboard_key_index_at(const Keyboard *keyboard, int x, int y);

/* Processa um toque em (x, y), relativo à área do teclado. Escreve em out_events os
 * eventos de tecla a mandar pro servidor (na ordem) e retorna quantos são — 0 quando o
 * toque não gera evento nenhum (caiu fora de tecla, ou foi num modificador/troca de
 * página, que só muda estado local). *out_action recebe a ação local se o toque caiu
 * numa tecla da página de menu (KEYBOARD_ACTION_NONE caso contrário) — eventos e ação
 * nunca vêm juntos no mesmo toque. *out_visual_changed avisa se o desenho do teclado
 * precisa ser refeito (modificador armado/desarmado ou página trocada). */
int keyboard_handle_tap(Keyboard *keyboard, int x, int y,
                        KeyboardEvent out_events[KEYBOARD_MAX_EVENTS],
                        KeyboardAction *out_action, bool *out_visual_changed);

void keyboard_destroy(Keyboard *keyboard);

#endif
