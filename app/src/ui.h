#ifndef KINDOW_UI_H
#define KINDOW_UI_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Adapter de apresentação: tudo que é GTK/GDK/Cairo vive atrás deste header — janela,
 * área de desenho, pintura do frame, o teclado virtual, o menu e a barra fixa (rodapé
 * sempre visível, mesmo com o painel escondido — reestrutura de 27/08, ver
 * docs/ideias-futuras.md). A LÓGICA do teclado — layout, hit-test, sticky keys — mora no
 * módulo puro keyboard.c; o menu (lista curta e estática de ações) e a barra (4 botões de
 * posição fixa) não têm complexidade que justifique módulo próprio, moram aqui mesmo.
 *
 * Teclado e menu compartilham a mesma área de tela (o "painel", entre o frame e a barra)
 * de forma mutuamente exclusiva — um state machine de 3 estados (nada/teclado/menu)
 * interno a este módulo, alternado pelos botões Teclado/Menu da barra. Só transições
 * nada<->algo mudam o tamanho da área útil do frame (teclado e menu reservam a MESMA
 * altura, decisão de 27/08) — por isso o callback on_resize só dispara nessas transições.
 *
 * Não conhece VNC nem sessão: recebe frames prontos via ui_show_frame e devolve interação
 * pelos callbacks — toque na área do frame vira on_click (coordenadas cruas; validar/
 * clamp é de quem consome, que sabe o tamanho real do frame), toque no teclado vira on_key
 * (keysyms já resolvidos pelo módulo keyboard), toque no menu vira on_action, toque na
 * barra vira on_bar, e uma mudança de tamanho da área útil vira on_resize.
 */

typedef struct Ui Ui;

typedef void (*UiClickFn)(int x, int y, void *user_data);
typedef void (*UiKeyFn)(uint32_t keysym, bool down, void *user_data);

/* Ações do menu (sair, status, zoom em 3 camadas) — a UI só transporta; o significado é
 * decisão do wiring (main.c). Vive aqui (não em keyboard.h) desde a reestrutura de 27/08:
 * o menu deixou de ser uma página do teclado pra virar conteúdo próprio do painel. */
typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_QUIT,   /* sair do app local */
    MENU_ACTION_STATUS, /* logar estado da conexão (debug sem SSH) */
    /* Zoom remoto em três camadas INDEPENDENTES (via kindow-helperd no Pi): apps =
     * conteúdo (Xft/DPI via xsettingsd), deco = decoração de janela (titlebar/botões do
     * Openbox), panel = painel (fontes do tint2). */
    MENU_ACTION_ZOOM_APPS_IN,
    MENU_ACTION_ZOOM_APPS_OUT,
    MENU_ACTION_ZOOM_DECO_IN,
    MENU_ACTION_ZOOM_DECO_OUT,
    MENU_ACTION_ZOOM_PANEL_IN,
    MENU_ACTION_ZOOM_PANEL_OUT,
    /* Quantas catracas de roda por toque de scroll (client-side puro, não passa pelo Pi
     * — ver session_set_scroll_lines). Etapa 4 da reestrutura de UI, 27/08. */
    MENU_ACTION_SCROLL_LINES_IN,
    MENU_ACTION_SCROLL_LINES_OUT,
    /* Derruba a sessão atual e volta pra tela de conexão (ver ui_show_connect_list) —
     * não mexe no histórico salvo, só sai da sessão ativa. */
    MENU_ACTION_DISCONNECT,
} MenuAction;

typedef void (*UiActionFn)(MenuAction action, void *user_data);

/* Os 4 botões da barra fixa, na ordem em que aparecem na tela (esquerda pra direita). */
typedef enum {
    BAR_SCROLL_UP,
    BAR_SCROLL_DOWN,
    BAR_TOGGLE_KEYBOARD,
    BAR_TOGGLE_MENU,
} BarButton;

typedef void (*UiBarFn)(BarButton button, void *user_data);

/* Disparado quando abrir/fechar o painel (teclado ou menu) muda a área útil do frame —
 * o chamador deve pedir um SetDesktopSize novo pro servidor com esse tamanho. NÃO dispara
 * ao trocar entre teclado e menu com o painel já aberto (mesma altura reservada pros
 * dois). */
typedef void (*UiResizeFn)(int width, int height, void *user_data);

/* Disparado durante um "clique contínuo" (arrasto) — etapa 3 da reestrutura (27/08). O
 * gatilho é a tecla "Esquerdo" (sticky) na página de símbolos do teclado, substituindo a
 * barra de espaço só ali (nas letras o espaço continua normal — decisão do usuário: só
 * ?123 já é um modo explícito, dá pra reaproveitar sem tirar a capacidade de digitar
 * espaço no dia a dia). held=true no toque inicial no FRAME e em cada posição
 * intermediária enquanto o dedo desliza, held=false quando o dedo LEVANTA da tela (não
 * precisa apertar de novo pra soltar — bounded pela duração física do toque, e como só
 * existe um ponto de contato nesse hardware, os outros controles ficam "congelados" de
 * graça, sem código extra de trava). O que o arrasto significa (mover janela,
 * redimensionar, selecionar texto) é decidido pelo servidor (Openbox/GTK) pela posição
 * onde começou — a UI só repassa coordenadas cruas, igual on_click. */
typedef void (*UiDragFn)(int x, int y, bool held, void *user_data);

/* Disparado pela tecla "Direito" (não-sticky, mesma página de símbolos que "Esquerdo") —
 * ação imediata, sem coordenada (o chamador já sabe a última posição tocada, ver
 * session_send_right_click). */
typedef void (*UiRightClickFn)(void *user_data);

/* Uma entrada da lista de conexões já usadas, só pra exibição/reuso — a UI não sabe nem
 * precisa saber de onde isso vem (histórico persistido é responsabilidade de quem
 * chama, ver connection_store.h). Os ponteiros são copiados internamente; só precisam
 * valer durante a chamada de ui_show_connect_list. password NULL ou "" = sem senha (a
 * senha nunca é EXIBIDA na lista — só viaja de volta no on_connect_request quando a
 * entrada é tocada). */
typedef struct {
    const char *host;
    int port;
    const char *password;
} UiConnectionEntry;

/* Disparado ao selecionar uma entrada da lista OU ao confirmar o formulário "+" — nos
 * dois casos o chamador decide o que "conectar" significa (ver session_connect) e chama
 * ui_show_connecting em seguida. password "" = sem senha. */
typedef void (*UiConnectRequestFn)(const char *host, int port, const char *password,
                                   void *user_data);

/* Disparado pelo botão "Voltar" da tela de conectando — o chamador decide o que
 * cancelar significa (ver session_disconnect) e chama ui_show_connect_list em seguida. */
typedef void (*UiBackFn)(void *user_data);

/* Cria a janela em tela cheia. O app agora começa SEM sessão ativa — quem decide qual
 * tela mostrar primeiro é o chamador, via ui_show_connect_list/ui_show_connecting logo
 * depois de ui_create (ver docs/ideias-futuras.md, item 5). window_title vem do chamador
 * porque o formato é exigência da plataforma (ver kindle_platform_window_title), não
 * decisão de UI. */
Ui *ui_create(const char *window_title, UiClickFn on_click, UiKeyFn on_key,
              UiActionFn on_action, UiBarFn on_bar, UiResizeFn on_resize, UiDragFn on_drag,
              UiRightClickFn on_right_click, UiConnectRequestFn on_connect_request,
              UiBackFn on_back, void *user_data);

/* Mostra a lista de conexões salvas (mais recente primeiro) + uma linha "+" pra
 * adicionar uma nova via teclado. selected_index é só destaque visual inicial (ex.: a
 * mais recente) — não confirma sozinho, o usuário ainda precisa tocar a linha ou o "+".
 * Chamar depois de ui_create e sempre que MENU_ACTION_DISCONNECT ou UiBackFn disparar. */
void ui_show_connect_list(Ui *ui, const UiConnectionEntry *entries, int count,
                          int selected_index);

/* Mostra "Conectando a host:porta..." com um botão de voltar (ver UiBackFn). Chamar
 * logo após pedir a conexão (UiConnectRequestFn), antes de saber se ela vai vingar. */
void ui_show_connecting(Ui *ui, const char *host, int port);

/* Troca a mensagem da tela de conectando por um aviso de erro (item 9 de
 * docs/ideias-futuras.md): "Não foi possível conectar..." + o motivo (detail, curto —
 * uma linha, sem quebra; NULL omite). Só tem efeito com a tela de conectando ativa; a
 * sessão continua re-tentando em segundo plano — se uma tentativa futura vingar,
 * ui_show_session limpa isso sozinho, e ui_show_connecting (conexão nova) também. */
void ui_show_connect_error(Ui *ui, const char *detail);

/* Volta pra tela normal (frame + barra + painel) — chamar quando o primeiro frame da
 * conexão nova chega de verdade (ver ui_show_frame), nunca antes: é o único sinal
 * confiável de que o servidor respondeu, TCP+handshake sozinhos não bastam. */
void ui_show_session(Ui *ui);

/* Área útil pro frame remoto: a tela real (detectada em runtime — o mesmo binário serve
 * qualquer modelo de Kindle) MENOS a barra e, se o painel estiver aberto, a faixa dele. É
 * este tamanho que deve ser pedido ao servidor como resolução remota. */
int ui_frame_width(const Ui *ui);
int ui_frame_height(const Ui *ui);

/* Copia o frame (ARGB32, ver vnc_client.h) pra surface interna e agenda o redraw só da
 * área do frame (a faixa do teclado não é tocada — importante no e-ink, refresh é caro).
 * O buffer pode ser liberado pelo chamador assim que a função retornar. */
void ui_show_frame(Ui *ui, int width, int height, const uint32_t *argb32_pixels);

void ui_destroy(Ui *ui);

#endif
