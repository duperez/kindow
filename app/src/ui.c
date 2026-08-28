#include "ui.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keyboard.h"
#include "strings.h"
#include "timing.h"

/* Quanto tempo a tecla tocada fica invertida (pedido do usuário: tecla normal "pisca"
 * preto, mesmo visual das sticky armadas, só que momentâneo). No e-ink o próprio refresh
 * já come boa parte disso — o valor é o teto lógico, não a duração percebida exata. */
#define KEY_FLASH_MS 180

/* Fração da altura ACIMA da barra reservada pro teclado (faixa fixa — decisão de 26/08:
 * melhor pro e-ink que um overlay que aparece/some, porque a região nunca muda depois de
 * pintada; a variante overlay/gesto ficou como ideia futura configurável via menu, ver
 * docs/ideias-futuras.md). 35% dá 6 fileiras de ~84px, tecla padrão de ~107px de largura
 * em 1072px — tamanho confortável de dedo. */
#define KEYBOARD_HEIGHT_PERCENT 35

/* Altura da barra fixa do rodapé, em % da altura da tela (etapa 5 da reestrutura,
 * 27/08 — revisão de um valor fixo em pixel anterior: um Kindle de resolução bem
 * diferente da testada, 1072x1448, deixaria a barra desproporcionalmente minúscula ou
 * enorme). 4% reproduz ~58px no device testado, perto do valor original de 60px. Piso
 * em pixels absolutos como rede de segurança — sem ele, uma tela hipotética muito baixa
 * deixaria a barra pequena demais pra tocar com confiança. */
#define BAR_HEIGHT_PERCENT 4
#define BAR_HEIGHT_MIN_PX 40
#define BAR_BUTTON_COUNT 4

/* Espaçamento entre teclas/botões vizinhos e largura de borda, também em proporção
 * (etapa 5) — calculados a partir da altura LOCAL de cada linha/botão (não da tela
 * inteira), pra manter o mesmo peso visual relativo em qualquer resolução. Antes eram
 * pixels fixos (3px de inset, 2px de borda) que só "davam certo" por coincidência no
 * device testado. */
#define INSET_PERCENT 4
#define BORDER_DIVISOR 40 /* largura de borda = altura local / BORDER_DIVISOR, mínimo 1 */

static int proportional_inset(int local_height) {
    int inset = local_height * INSET_PERCENT / 100;
    return inset < 1 ? 1 : inset;
}

static int proportional_border_width(int local_height) {
    int width = local_height / BORDER_DIVISOR;
    return width < 1 ? 1 : width;
}

/* Distância mínima (px) entre duas posições de arrasto pra valer a pena reenviar ao
 * servidor — throttle adicional ao coalescing do próprio GDK (ver on_motion_notify):
 * motion pode disparar em alta frequência, e cada envio é rede + potencial redraw do
 * lado do servidor, caro no e-ink. Não validado no hardware ainda (etapa 3, 27/08) —
 * valor de partida, não medido. */
#define DRAG_MIN_MOVE_PX 8

#define CONNECT_HOST_BUF_LEN 64
#define CONNECT_PORT_BUF_LEN 8
#define CONNECT_PASSWORD_BUF_LEN 32
#define CONNECT_ERROR_BUF_LEN 96

typedef struct {
    char host[CONNECT_HOST_BUF_LEN];
    int port;
    char password[CONNECT_PASSWORD_BUF_LEN]; /* "" = sem senha; nunca exibida na lista */
} UiConnectDisplayEntry;
#define UI_CONNECT_ENTRIES_MAX 8

/* Qual campo do formulário "+" recebe o próximo toque de tecla. */
typedef enum {
    FORM_FIELD_HOST,
    FORM_FIELD_PORT,
    FORM_FIELD_PASSWORD,
} FormField;

/* Quantas linhas da lista de conexões cabem no painel de uma vez, sem rolar — reaproveita
 * o mesmo ritmo visual do teclado (6 fileiras), em vez de uma proporção nova. Acima
 * disso, os botões ↑/↓ da barra rolam a lista (ver handle_bar_tap) — mais simples que
 * uma barra de rolagem própria, e esses botões já existem e ficam livres sem sessão
 * ativa (reestrutura de 27/08, sugestão do usuário depois de testar a primeira versão). */
#define CONNECT_LIST_VISIBLE_ROWS 6

/* Fileiras de campos (IP/Porta) e botões (Conectar/Cancelar) do formulário "+", dentro
 * da área do FRAME — mesma área que mostraria a tela do Pi (reestrutura de 27/08: a
 * primeira versão desenhava um cabeçalho de tela cheia com proporção própria; ficou
 * visualmente inconsistente com o resto do app, feedback do usuário depois de testar no
 * device). 2 das 8 fileiras vão pros campos/botões, o resto do frame fica em branco. */
#define CONNECT_FORM_FIELD_ROWS 8

/* Largura do campo IP no formulário "+" — o resto vai pro campo Porta. IP costuma ser
 * bem mais longo de digitar/ler (dotted-quad) que a porta (4-5 dígitos), daí a divisão
 * desigual em vez de 50/50 (achado de review: valor nomeado, não mágico inline). */
#define CONNECT_FORM_IP_WIDTH_PERCENT 65

/* O painel entre o frame e a barra: nada, teclado (sessão), menu (sessão), lista de
 * conexões salvas, formulário "+" (mesma instância de Keyboard do teclado normal — só
 * o destino dos eventos muda), ou "conectando..." com uma linha de voltar. As 3 últimas
 * só existem sem sessão ativa (ver bool connected em struct Ui). Reestruturado em 27/08
 * pra reaproveitar a MESMA área/geometria da sessão (frame + painel + barra) em vez de
 * telas cheias próprias com proporções inventadas — era o desenho original da tela de
 * conexão, mas ficou rudimentar/inconsistente com teclado e menu (feedback do usuário). */
typedef enum {
    PANEL_NONE,
    PANEL_KEYBOARD,
    PANEL_MENU,
    PANEL_CONNECT_LIST,
    PANEL_CONNECT_FORM,
    PANEL_CONNECTING,
} PanelMode;

/* De onde veio o índice em flash — teclado (sessão OU formulário "+", mesma instância) e
 * barra têm esquemas de geometria diferentes, então o mesmo índice numérico significa
 * coisas diferentes dependendo da origem. */
typedef enum {
    FLASH_NONE,
    FLASH_KEYBOARD_KEY,
    FLASH_BAR_BUTTON,
    FLASH_MENU_ITEM,
} FlashSource;

struct Ui {
    GtkWidget *window;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    int screen_width;
    int screen_height;
    /* y onde o painel (teclado/menu/tela de conexão) começa — == altura útil pro frame
     * remoto. Igual a bar_top quando panel_mode é PANEL_NONE (painel sem altura
     * reservada). */
    int keyboard_top;
    /* y onde a barra fixa começa — sempre screen_height - BAR_HEIGHT_PX, nunca muda em
     * runtime (diferente de keyboard_top, que alterna com o painel). */
    int bar_top;
    PanelMode panel_mode;
    Keyboard *keyboard;
    UiClickFn on_click;
    UiKeyFn on_key;
    UiActionFn on_action;
    UiBarFn on_bar;
    UiResizeFn on_resize;
    UiDragFn on_drag;
    UiRightClickFn on_right_click;
    UiConnectRequestFn on_connect_request;
    UiBackFn on_back;
    void *callback_user_data;

    /* Tela de conexão (item 5, 27/08). connected=true é o único sinal de "existe sessão
     * de verdade" — decide o que a área do FRAME mostra (pixels do Pi vs. algo local).
     * connecting=true é INDEPENDENTE de panel_mode (sobrevive ao toggle Teclado
     * escondendo o painel) — só controla a mensagem "Conectando..." no frame.
     * pre_connect_panel lembra qual sub-tela (lista ou formulário) volta quando o
     * Teclado é re-ativado depois de escondido, e também decide se o FRAME mostra os
     * campos do formulário — pelo mesmo motivo de connecting, isso não deve sumir só
     * porque o painel foi escondido. */
    bool connected;
    bool connecting;
    PanelMode pre_connect_panel;
    UiConnectDisplayEntry connect_entries[UI_CONNECT_ENTRIES_MAX];
    int connect_entry_count;
    int connect_selected_index; /* destaque visual (MRU) — não confirma sozinho */
    int connect_scroll_offset;  /* índice da 1ª entrada visível — rolada pelos botões
                                  * ↑/↓ da barra, reaproveitados sem sessão ativa */
    char form_host[CONNECT_HOST_BUF_LEN];
    char form_port[CONNECT_PORT_BUF_LEN];
    char form_password[CONNECT_PASSWORD_BUF_LEN];
    FormField form_active;
    char connecting_host[CONNECT_HOST_BUF_LEN];
    int connecting_port;
    /* Erro da tela de conectando (item 9) — "" = sem erro, mostrando o "Conectando..."
     * normal. Setado por ui_show_connect_error, limpo por ui_show_connecting (conexão
     * nova) e ui_show_session (deu certo afinal). */
    char connecting_error[CONNECT_ERROR_BUF_LEN];

    /* Etapa 3 da reestrutura (27/08): true do toque inicial no frame (com o clique
     * esquerdo armado no teclado) até o dedo levantar da tela. Só existe um ponto de
     * contato possível nesse hardware, então enquanto isso é true, fisicamente não dá
     * pra tocar outro controle — o "congelamento" da barra/teclado sai de graça, sem
     * guarda explícita em nenhum outro handler. */
    bool dragging;
    int drag_last_x, drag_last_y; /* última posição REPASSADA (não a última recebida) —
                                    * base do throttle por distância mínima, ver
                                    * on_motion_notify. */
    /* Feedback de toque: o que está "piscando" (invertido por um instante) — tecla do
     * teclado ou botão da barra, nunca os dois ao mesmo tempo. flash_timer_id é a source
     * que vai apagar o flash — guardada pra um toque rápido em outro alvo cancelar o
     * apagamento antigo (senão o timer do alvo anterior apagaria o flash do novo cedo
     * demais). */
    FlashSource flash_source;
    int flash_index;
    guint flash_timer_id;
    /* timestamp de quando o último gtk_widget_queue_draw foi pedido — usado só pra medir
     * quanto tempo até o expose de verdade rodar (ver on_expose). Zerado no início; a
     * primeira chamada de on_expose (antes de qualquer frame chegar) ignora essa medição. */
    struct timespec paint_requested_at;
};

/* Rótulo centralizado num retângulo — usado tanto pelas teclas do teclado quanto pelos
 * botões da barra, daí viver solto em vez de preso a KeyboardKeyView. */
static void draw_centered_label(cairo_t *cr, int x, int y, int w, int h, const char *label) {
    if (!label[0]) {
        return; /* barra de espaço do teclado: sem rótulo */
    }
    /* rótulo de 1 caractere (contando UTF-8 multi-byte como 1) ganha fonte maior que
     * palavra (Esc, Tab, Ctrl...) — heurística: palavras têm 2+ chars ASCII */
    bool is_word = label[1] != '\0' && !(label[0] & 0x80);
    cairo_set_font_size(cr, h * (is_word ? 0.30 : 0.42));

    cairo_text_extents_t ext;
    cairo_text_extents(cr, label, &ext);
    cairo_move_to(cr, x + (w - ext.width) / 2.0 - ext.x_bearing,
                  y + (h - ext.height) / 2.0 - ext.y_bearing);
    cairo_show_text(cr, label);
}

static void draw_key_label(cairo_t *cr, const KeyboardKeyView *key) {
    draw_centered_label(cr, key->x, key->y, key->w, key->h, key->label);
}

/* Desenho do teclado: alto contraste pro e-ink — teclas brancas com borda preta, rótulo
 * preto; modificador armado (sticky Shift/Ctrl) invertido (fundo preto, texto branco),
 * que é o feedback de "armado" sem precisar de tons de cinza. Reaproveitado tanto pra
 * sessão real (PANEL_KEYBOARD) quanto pro formulário "+" (PANEL_CONNECT_FORM) — mesma
 * instância de Keyboard, ver struct Ui e handle_panel_tap. */
static void draw_keyboard(cairo_t *cr, const Ui *ui) {
    cairo_save(cr);
    cairo_translate(cr, 0, ui->keyboard_top);

    /* fundo da faixa + linha separando do frame */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, ui->screen_width, ui->bar_top - ui->keyboard_top);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, 1);
    cairo_line_to(cr, ui->screen_width, 1);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);

    int count = keyboard_key_count(ui->keyboard);
    for (int i = 0; i < count; i++) {
        KeyboardKeyView key = keyboard_key_view(ui->keyboard, i);
        if (ui->flash_source == FLASH_KEYBOARD_KEY && i == ui->flash_index) {
            key.highlighted = true; /* feedback de toque: pisca invertida */
        }
        /* inset entre teclas vizinhas, proporcional à altura da própria tecla (etapa 5) */
        int inset = proportional_inset(key.h);
        key.x += inset;
        key.y += inset;
        key.w -= 2 * inset;
        key.h -= 2 * inset;

        if (key.highlighted) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, key.x, key.y, key.w, key.h);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            draw_key_label(cr, &key);
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, proportional_border_width(key.h));
            cairo_rectangle(cr, key.x, key.y, key.w, key.h);
            cairo_stroke(cr);
            draw_key_label(cr, &key);
        }
    }

    cairo_restore(cr);
}

/* ---- Menu: lista curta e estática de ações (zoom em 3 camadas, status, sair) — vive
 * aqui (não em módulo próprio) porque, diferente do teclado, não tem estado sticky nem
 * troca de página: é só uma tabela de retângulos que emitem MenuAction ao toque. Um par
 * A-/A+ por linha para cada camada de zoom (independentes entre si — ver
 * app/src/main.c), "Status" e "Sair" isolados na última linha, longe de toque acidental
 * nos pares de zoom que o usuário usa repetidamente. ---- */

typedef struct {
    int row;
    float x0, x1; /* fração horizontal ocupada nessa linha, 0..1 */
    StringId label; /* resolvido via tr() na hora de desenhar (i18n, 27/08) */
    MenuAction action;
} MenuItem;

#define MENU_ROWS 7

static const MenuItem kMenuItems[] = {
    {0, 0.0f, 0.5f, STR_MENU_APPS_OUT, MENU_ACTION_ZOOM_APPS_OUT},
    {0, 0.5f, 1.0f, STR_MENU_APPS_IN, MENU_ACTION_ZOOM_APPS_IN},
    {1, 0.0f, 0.5f, STR_MENU_DECO_OUT, MENU_ACTION_ZOOM_DECO_OUT},
    {1, 0.5f, 1.0f, STR_MENU_DECO_IN, MENU_ACTION_ZOOM_DECO_IN},
    {2, 0.0f, 0.5f, STR_MENU_PANEL_OUT, MENU_ACTION_ZOOM_PANEL_OUT},
    {2, 0.5f, 1.0f, STR_MENU_PANEL_IN, MENU_ACTION_ZOOM_PANEL_IN},
    /* Etapa 4 da reestrutura (27/08): quantas catracas de roda o scroll manda por toque
     * — puramente client-side, não passa pelo kindow-helperd/Pi (ver session.c). */
    {3, 0.0f, 0.5f, STR_MENU_SCROLL_OUT, MENU_ACTION_SCROLL_LINES_OUT},
    {3, 0.5f, 1.0f, STR_MENU_SCROLL_IN, MENU_ACTION_SCROLL_LINES_IN},
    /* Item 5 da fila (27/08): volta pra tela de conexão sem mexer no histórico salvo —
     * linha isolada, mesmo raciocínio de Status/Sair (longe de toque acidental nos
     * pares de zoom/scroll, que são os controles mais usados no dia a dia). */
    {4, 0.0f, 1.0f, STR_MENU_DISCONNECT, MENU_ACTION_DISCONNECT},
    {5, 0.0f, 1.0f, STR_MENU_STATUS, MENU_ACTION_STATUS},
    {6, 0.0f, 1.0f, STR_MENU_QUIT_APP, MENU_ACTION_QUIT},
};
#define MENU_ITEM_COUNT (int)(sizeof(kMenuItems) / sizeof(kMenuItems[0]))

static void draw_menu(cairo_t *cr, const Ui *ui) {
    cairo_save(cr);
    cairo_translate(cr, 0, ui->keyboard_top); /* mesma área que o teclado ocuparia */

    int panel_h = ui->bar_top - ui->keyboard_top;
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, ui->screen_width, panel_h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, 1);
    cairo_line_to(cr, ui->screen_width, 1);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);

    int row_h = panel_h / MENU_ROWS;
    int inset = proportional_inset(row_h);
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        const MenuItem *item = &kMenuItems[i];
        int x = (int)(item->x0 * ui->screen_width) + inset;
        int y = item->row * row_h + inset;
        int w = (int)((item->x1 - item->x0) * ui->screen_width) - 2 * inset;
        int h = row_h - 2 * inset;

        if (ui->flash_source == FLASH_MENU_ITEM && i == ui->flash_index) {
            /* feedback de toque: pisca invertida, mesmo tratamento visual da tecla do
             * teclado — pedido do usuário depois de notar que o menu disparava a ação
             * sem indicar visualmente ONDE o toque caiu (27/08). */
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, x, y, w, h);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            draw_centered_label(cr, x, y, w, h, tr(item->label));
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, proportional_border_width(row_h));
            cairo_rectangle(cr, x, y, w, h);
            cairo_stroke(cr);
            draw_centered_label(cr, x, y, w, h, tr(item->label));
        }
    }

    cairo_restore(cr);
}

/* Índice em kMenuItems (não ponteiro) pra caber no mesmo esquema de FlashSource/
 * flash_index das outras fontes de flash (tecla do teclado, botão da barra) — -1 se o
 * toque não caiu em item nenhum. */
static int find_menu_item_index(const Ui *ui, int x, int y) {
    int panel_h = ui->bar_top - ui->keyboard_top;
    int row_h = panel_h / MENU_ROWS;
    int row = y / row_h;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (kMenuItems[i].row != row) {
            continue;
        }
        int x0 = (int)(kMenuItems[i].x0 * ui->screen_width);
        int x1 = (int)(kMenuItems[i].x1 * ui->screen_width);
        if (x >= x0 && x < x1) {
            return i;
        }
    }
    return -1;
}

/* Rótulos dos 4 botões, na mesma ordem do enum BarButton. Setas ↑/↓ já confirmadas
 * renderizando certo nesse device (mesma fonte do teclado) — ver kindle-hardware-test.md.
 * "Teclado" mantém o mesmo rótulo mesmo sem sessão ativa (na prática alterna
 * lista/formulário de conexão, não o teclado normal) — trocar dinamicamente não foi
 * pedido. "Menu" é diferente: vira "Sair" sem sessão ativa (ver draw_bar) — achado do
 * usuário, 27/08: sem isso o usuário ficava PRESO na tela de conexão, sem nenhum jeito
 * de sair do app antes de conectar em algum lugar. */
static const char *bar_label(const Ui *ui, int index) {
    switch ((BarButton)index) {
    case BAR_SCROLL_UP:
        return "↑";
    case BAR_SCROLL_DOWN:
        return "↓";
    case BAR_TOGGLE_KEYBOARD:
        return tr(STR_BAR_KEYBOARD);
    case BAR_TOGGLE_MENU:
    default:
        /* vira "Sair"/"Quit" sem sessão ativa — ver comentário acima */
        return ui->connected ? tr(STR_BAR_MENU) : tr(STR_BAR_QUIT);
    }
}

/* Desenho da barra: mesmo estilo de alto contraste do teclado. Teclado/Menu ficam
 * PERSISTENTEMENTE invertidos enquanto aquele for o modo atual do painel (mesmo
 * tratamento visual das sticky Shift/Ctrl do teclado — a mudança de estado já É o
 * feedback, sem precisar de flash); Scroll ↑/↓ nunca ficam com destaque persistente (são
 * ações imediatas, o flash é feedback suficiente). */
static void draw_bar(cairo_t *cr, const Ui *ui) {
    cairo_save(cr);
    cairo_translate(cr, 0, ui->bar_top);

    int bar_height = ui->screen_height - ui->bar_top;
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, ui->screen_width, bar_height);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, 1);
    cairo_line_to(cr, ui->screen_width, 1);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);

    int seg_w = ui->screen_width / BAR_BUTTON_COUNT;
    int inset = proportional_inset(bar_height);
    int border_w = proportional_border_width(bar_height);
    /* "Teclado" fica destacado pra QUALQUER um dos conteúdos que ele alterna — teclado
     * de verdade na sessão, ou lista/formulário/conectando sem sessão (ver PanelMode) —
     * não só PANEL_KEYBOARD. */
    bool keyboard_button_active = ui->panel_mode == PANEL_KEYBOARD ||
                                  ui->panel_mode == PANEL_CONNECT_LIST ||
                                  ui->panel_mode == PANEL_CONNECT_FORM ||
                                  ui->panel_mode == PANEL_CONNECTING;
    for (int i = 0; i < BAR_BUTTON_COUNT; i++) {
        int x = i * seg_w + inset;
        int y = inset;
        /* último segmento absorve o resto da divisão inteira, igual às fileiras do
         * teclado — sem isso sobraria uma faixa sem borda na ponta direita. */
        int w = (i == BAR_BUTTON_COUNT - 1 ? ui->screen_width - i * seg_w : seg_w) - 2 * inset;
        int h = bar_height - 2 * inset;

        bool highlighted = (i == BAR_TOGGLE_KEYBOARD && keyboard_button_active) ||
                           (i == BAR_TOGGLE_MENU && ui->panel_mode == PANEL_MENU);
        bool flashing = ui->flash_source == FLASH_BAR_BUTTON && ui->flash_index == i;
        const char *label = bar_label(ui, i);

        if (highlighted || flashing) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, x, y, w, h);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            draw_centered_label(cr, x, y, w, h, label);
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, border_w);
            cairo_rectangle(cr, x, y, w, h);
            cairo_stroke(cr);
            draw_centered_label(cr, x, y, w, h, label);
        }
    }

    cairo_restore(cr);
}

/* ---- Tela de conexão (item 5, 27/08 — redesenhada no mesmo dia depois de testar no
 * device: a primeira versão usava telas cheias com proporções próprias e ficou
 * "rudimentar" comparado a teclado/menu, feedback do usuário). Agora reaproveita a MESMA
 * área/geometria da sessão: painel = lista, formulário (mesmo Keyboard do teclado
 * normal) ou "conectando..."; frame = campos do formulário ou mensagem de conectando;
 * barra sempre visível, com "Menu" bloqueado ao toque (não faz sentido sem sessão) e
 * ↑/↓ reaproveitados pra rolar a lista em vez de mandar scroll pro Pi. ---- */

static int connect_list_row_h(const Ui *ui) {
    return (ui->bar_top - ui->keyboard_top) / CONNECT_LIST_VISIBLE_ROWS;
}

static void draw_connect_list_panel(cairo_t *cr, const Ui *ui) {
    cairo_save(cr);
    cairo_translate(cr, 0, ui->keyboard_top);

    int panel_h = ui->bar_top - ui->keyboard_top;
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, ui->screen_width, panel_h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, 1);
    cairo_line_to(cr, ui->screen_width, 1);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);

    int row_h = connect_list_row_h(ui);
    int inset = proportional_inset(row_h);
    int border_w = proportional_border_width(row_h);
    int total_rows = ui->connect_entry_count + 1; /* +1 pra linha "+" */

    for (int slot = 0; slot < CONNECT_LIST_VISIBLE_ROWS; slot++) {
        int row = slot + ui->connect_scroll_offset;
        if (row >= total_rows) {
            break;
        }
        int x = inset;
        int y = slot * row_h + inset;
        int w = ui->screen_width - 2 * inset;
        int h = row_h - 2 * inset;

        bool is_plus = (row == ui->connect_entry_count);
        char label[96];
        if (is_plus) {
            snprintf(label, sizeof(label), "+");
        } else {
            snprintf(label, sizeof(label), "%s:%d", ui->connect_entries[row].host,
                     ui->connect_entries[row].port);
        }

        /* !is_plus: achado de review, 27/08 — sem essa checagem, lista vazia (primeiro
         * uso, connect_entry_count==0) desenhava a própria linha "+" destacada como se
         * já estivesse "selecionada" (selected_index nasce 0, e "+" também é a linha 0
         * quando não há nenhuma conexão salva ainda). */
        bool selected = !is_plus && row == ui->connect_selected_index;
        if (selected) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, x, y, w, h);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            draw_centered_label(cr, x, y, w, h, label);
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, border_w);
            cairo_rectangle(cr, x, y, w, h);
            cairo_stroke(cr);
            draw_centered_label(cr, x, y, w, h, label);
        }
    }

    cairo_restore(cr);
}

static void draw_connecting_panel(cairo_t *cr, const Ui *ui) {
    cairo_save(cr);
    cairo_translate(cr, 0, ui->keyboard_top);

    int panel_h = ui->bar_top - ui->keyboard_top;
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, ui->screen_width, panel_h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, 1);
    cairo_line_to(cr, ui->screen_width, 1);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);

    /* Botão "Voltar" com a MESMA altura de linha da lista/teclado (row_h de
     * CONNECT_LIST_VISIBLE_ROWS), não a altura do painel inteiro — achado de review:
     * draw_centered_label só escala a fonte pela ALTURA da caixa (sem olhar a largura
     * do texto), então passar panel_h inteiro deixava o rótulo ~6x maior que qualquer
     * outro texto do app. Centralizado verticalmente no painel, resto em branco. */
    int row_h = connect_list_row_h(ui);
    int inset = proportional_inset(row_h);
    int x = inset, y = (panel_h - row_h) / 2;
    int w = ui->screen_width - 2 * inset, h = row_h - 2 * inset;
    cairo_set_line_width(cr, proportional_border_width(row_h));
    cairo_rectangle(cr, x, y, w, h);
    cairo_stroke(cr);
    draw_centered_label(cr, x, y, w, h, tr(STR_BACK));

    cairo_restore(cr);
}

static int connect_form_row_h(const Ui *ui) {
    return ui->keyboard_top / CONNECT_FORM_FIELD_ROWS;
}

/* Campos IP/Porta/Senha + botões Conectar/Cancelar do formulário "+", desenhados na
 * área do FRAME (mesma área que mostraria a tela do Pi) — o teclado pra digitar fica no
 * painel logo abaixo, desenhado por draw_keyboard (mesma instância/função da sessão
 * real). 3 fileiras: IP+Porta, Senha, botões. */
static void draw_connect_form_fields(cairo_t *cr, const Ui *ui) {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, ui->screen_width, ui->keyboard_top);
    cairo_fill(cr);

    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);

    int row_h = connect_form_row_h(ui);
    int inset = proportional_inset(row_h);
    int border_w = proportional_border_width(row_h);
    int ip_w = ui->screen_width * CONNECT_FORM_IP_WIDTH_PERCENT / 100;

    char ip_label[96];
    snprintf(ip_label, sizeof(ip_label), "%s%s", tr(STR_FORM_IP), ui->form_host);
    char port_label[32];
    snprintf(port_label, sizeof(port_label), "%s%s", tr(STR_FORM_PORT), ui->form_port);
    /* Senha mascarada com '*' — o texto real nunca aparece na tela (nem precisa: sem
     * cursor/edição no meio, ver o campo de senha como write-only visualmente). */
    char password_label[64];
    {
        size_t n = strlen(ui->form_password);
        char stars[CONNECT_PASSWORD_BUF_LEN];
        memset(stars, '*', n);
        stars[n] = '\0';
        snprintf(password_label, sizeof(password_label), "%s%s", tr(STR_FORM_PASSWORD), stars);
    }

    struct {
        int x, y, w;
        const char *label;
        bool active;
    } fields[3] = {
        {0, 0, ip_w, ip_label, ui->form_active == FORM_FIELD_HOST},
        {ip_w, 0, ui->screen_width - ip_w, port_label, ui->form_active == FORM_FIELD_PORT},
        {0, row_h, ui->screen_width, password_label,
         ui->form_active == FORM_FIELD_PASSWORD},
    };
    for (int i = 0; i < 3; i++) {
        int x = fields[i].x + inset;
        int y = fields[i].y + inset;
        int w = fields[i].w - 2 * inset;
        int h = row_h - 2 * inset;
        if (fields[i].active) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, x, y, w, h);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            draw_centered_label(cr, x, y, w, h, fields[i].label);
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, border_w);
            cairo_rectangle(cr, x, y, w, h);
            cairo_stroke(cr);
            draw_centered_label(cr, x, y, w, h, fields[i].label);
        }
    }

    int half_w = ui->screen_width / 2;
    const char *labels[2] = {tr(STR_FORM_CONNECT), tr(STR_FORM_CANCEL)};
    for (int i = 0; i < 2; i++) {
        int x = i * half_w + inset;
        int y = 2 * row_h + inset;
        int w = half_w - 2 * inset;
        int h = row_h - 2 * inset;
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_line_width(cr, border_w);
        cairo_rectangle(cr, x, y, w, h);
        cairo_stroke(cr);
        draw_centered_label(cr, x, y, w, h, labels[i]);
    }
}

static void draw_connecting_message(cairo_t *cr, const Ui *ui) {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, ui->screen_width, ui->keyboard_top);
    cairo_fill(cr);

    /* Preto explícito ANTES dos rótulos — sem isso o texto sai branco sobre o fundo
     * branco recém-pintado acima (bug real: a mensagem de conectando/erro ficou
     * invisível até um screenshot de framebuffer revelar, 27/08 — o usuário via "só
     * um botão Voltar" na tela). */
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);

    /* Mesma fileira usada pelos campos do formulário "+" (connect_form_row_h), não a
     * altura do frame inteiro — achado de review: draw_centered_label só escala a
     * fonte pela ALTURA da caixa, sem olhar a largura do texto, então keyboard_top
     * inteiro como h deixava a fonte gigante o bastante pra vazar pra fora da tela. */
    int row_h = connect_form_row_h(ui);
    char label[128];
    if (ui->connecting_error[0]) {
        /* Item 9: depois de N tentativas falhadas (quem conta é o main.c), o
         * "Conectando..." dá lugar ao aviso — a sessão CONTINUA tentando em segundo
         * plano; se vingar depois, ui_show_session limpa tudo isso sozinho. */
        snprintf(label, sizeof(label), tr(STR_CONNECT_FAILED), ui->connecting_host,
                 ui->connecting_port);
        draw_centered_label(cr, 0, 0, ui->screen_width, row_h, label);
        draw_centered_label(cr, 0, row_h, ui->screen_width, row_h, ui->connecting_error);
    } else {
        snprintf(label, sizeof(label), tr(STR_CONNECTING), ui->connecting_host,
                 ui->connecting_port);
        draw_centered_label(cr, 0, 0, ui->screen_width, row_h, label);
    }
}

static gboolean on_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data) {
    Ui *ui = user_data;
    struct timespec t0 = timing_now();

    cairo_t *cr = gdk_cairo_create(widget->window);
    /* Restringe o desenho (e o damage repassado pro X/e-ink) à área que o GTK marcou como
     * suja — frame e teclado são agendados separadamente (queue_draw_area), e sem esse
     * clip cada frame novo redesenharia (e faria o e-ink piscar) a faixa do teclado junto. */
    gdk_cairo_rectangle(cr, &event->area);
    cairo_clip(cr);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    /* Área do FRAME (0..keyboard_top): pixels do Pi quando conectado; senão, mensagem de
     * conectando ou campos do formulário "+" — ambos independentes de panel_mode (não
     * somem se o Teclado for escondido, ver comentário em struct Ui). Fora desses casos
     * (lista, ou nada em especial), fica em branco mesmo. */
    if (ui->connected) {
        if (ui->surface) {
            cairo_set_source_surface(cr, ui->surface, 0, 0);
            cairo_paint(cr);
        }
    } else if (ui->connecting) {
        draw_connecting_message(cr, ui);
    } else if (ui->pre_connect_panel == PANEL_CONNECT_FORM) {
        draw_connect_form_fields(cr, ui);
    }

    if (event->area.y + event->area.height > ui->keyboard_top && event->area.y < ui->bar_top) {
        switch (ui->panel_mode) {
        case PANEL_KEYBOARD:
        case PANEL_CONNECT_FORM:
            draw_keyboard(cr, ui);
            break;
        case PANEL_MENU:
            draw_menu(cr, ui);
            break;
        case PANEL_CONNECT_LIST:
            draw_connect_list_panel(cr, ui);
            break;
        case PANEL_CONNECTING:
            draw_connecting_panel(cr, ui);
            break;
        case PANEL_NONE:
            break;
        }
    }

    if (event->area.y + event->area.height > ui->bar_top) {
        draw_bar(cr, ui);
    }

    cairo_destroy(cr);

    if (ui->connected && ui->surface && event->area.y < ui->keyboard_top) {
        /* só loga a fila de espera quando o redraw inclui o frame de verdade — expose só
         * do teclado (ou antes de qualquer frame chegar) não tem paint_requested_at
         * válido pra comparar. */
        struct timespec t1 = timing_now();
        g_printerr("kindow: fila até o redraw %ld ms, cairo_paint %ld ms\n",
                   timing_elapsed_ms(ui->paint_requested_at, t0), timing_elapsed_ms(t0, t1));
    }
    return TRUE;
}

/* Agenda o redraw só do retângulo de UM alvo de flash (tecla do teclado — sessão ou
 * formulário "+", mesma instância/geometria —, botão da barra, ou item do menu). */
static void queue_flash_redraw(Ui *ui, FlashSource source, int index) {
    if (source == FLASH_KEYBOARD_KEY) {
        KeyboardKeyView key = keyboard_key_view(ui->keyboard, index);
        gtk_widget_queue_draw_area(ui->drawing_area, key.x, ui->keyboard_top + key.y, key.w,
                                    key.h);
    } else if (source == FLASH_BAR_BUTTON) {
        int seg_w = ui->screen_width / BAR_BUTTON_COUNT;
        int x = index * seg_w;
        int w = (index == BAR_BUTTON_COUNT - 1) ? ui->screen_width - x : seg_w;
        gtk_widget_queue_draw_area(ui->drawing_area, x, ui->bar_top, w,
                                    ui->screen_height - ui->bar_top);
    } else if (source == FLASH_MENU_ITEM) {
        /* mesma conta de geometria de draw_menu/find_menu_item_index — redesenha só a
         * linha inteira (não só a fração x0..x1 do item) por simplicidade, já que os
         * pares A-/A+ de uma linha ficam lado a lado e um redraw de linha inteira não
         * é mais caro que dois redraws de metade de linha. */
        int panel_h = ui->bar_top - ui->keyboard_top;
        int row_h = panel_h / MENU_ROWS;
        int row = kMenuItems[index].row;
        gtk_widget_queue_draw_area(ui->drawing_area, 0, ui->keyboard_top + row * row_h,
                                    ui->screen_width, row_h);
    }
}

static gboolean on_flash_timeout(gpointer user_data) {
    Ui *ui = user_data;
    ui->flash_timer_id = 0;
    if (ui->flash_source != FLASH_NONE) {
        FlashSource source = ui->flash_source;
        int index = ui->flash_index;
        ui->flash_source = FLASH_NONE;
        queue_flash_redraw(ui, source, index);
    }
    return FALSE;
}

static void start_flash(Ui *ui, FlashSource source, int index) {
    if (ui->flash_timer_id) {
        g_source_remove(ui->flash_timer_id);
    }
    if (ui->flash_source != FLASH_NONE &&
        !(ui->flash_source == source && ui->flash_index == index)) {
        queue_flash_redraw(ui, ui->flash_source, ui->flash_index); /* apaga o flash anterior */
    }
    ui->flash_source = source;
    ui->flash_index = index;
    queue_flash_redraw(ui, source, index);
    ui->flash_timer_id = g_timeout_add(KEY_FLASH_MS, on_flash_timeout, ui);
}

/* Cancela qualquer flash pendente sem redesenhar — usado quando uma mudança maior
 * (troca de página/painel) já vai redesenhar a área inteira por conta própria, então
 * reagendar o redraw do flash seria redundante. */
static void cancel_flash(Ui *ui) {
    if (ui->flash_timer_id) {
        g_source_remove(ui->flash_timer_id);
        ui->flash_timer_id = 0;
    }
    ui->flash_source = FLASH_NONE;
}

/* Aplica `wanted` a panel_mode e dispara on_resize se abrir/fechar mudou o tamanho útil
 * do frame — função só (não só o toggle da barra) porque as transições de tela da
 * conexão (ui_show_connect_list/connecting/session) também precisam desse mesmo ajuste
 * de área, não só o toque no botão Teclado/Menu. */
static void apply_panel_mode(Ui *ui, PanelMode wanted) {
    bool was_open = ui->panel_mode != PANEL_NONE;
    ui->panel_mode = wanted;
    bool now_open = ui->panel_mode != PANEL_NONE;
    if (was_open != now_open) {
        ui->keyboard_top = now_open ? ui->bar_top * (100 - KEYBOARD_HEIGHT_PERCENT) / 100
                                     : ui->bar_top;
        if (ui->on_resize) {
            ui->on_resize(ui->screen_width, ui->keyboard_top, ui->callback_user_data);
        }
    }
}

/* Abre/troca/fecha o painel conforme as 3 regras combinadas (27/08): nada aberto -> abre
 * o tocado; o outro aberto -> troca pro tocado; o próprio já aberto -> fecha (volta a
 * PANEL_NONE). "Teclado" alterna pro conteúdo certo conforme o estado (teclado de
 * verdade se conectado; senão, o que a tela de conexão tava mostrando por último — lista,
 * formulário ou conectando). "Menu" só chega aqui quando HÁ sessão ativa — sem sessão o
 * mesmo botão vira "Sair" (ver handle_bar_tap), tratado antes de chegar nesta função. */
static void toggle_panel(Ui *ui, BarButton button) {
    /* Mesmo raciocínio da troca de página dentro do teclado (achado de review, 27/08):
     * "Esquerdo" só existe na página de símbolos — sair do painel de teclado de
     * qualquer jeito deixaria um arme vivo sem indicador visual nenhum. Idempotente
     * quando já desarmado. */
    keyboard_consume_left_click_arm(ui->keyboard);

    if (button == BAR_TOGGLE_MENU) {
        apply_panel_mode(ui, ui->panel_mode == PANEL_MENU ? PANEL_NONE : PANEL_MENU);
        return;
    }

    PanelMode target = ui->connected      ? PANEL_KEYBOARD
                       : ui->connecting   ? PANEL_CONNECTING
                                          : ui->pre_connect_panel;
    apply_panel_mode(ui, ui->panel_mode == target ? PANEL_NONE : target);
}

static void handle_bar_tap(Ui *ui, int x) {
    int seg_w = ui->screen_width / BAR_BUTTON_COUNT;
    int index = x / seg_w;
    if (index >= BAR_BUTTON_COUNT) {
        index = BAR_BUTTON_COUNT - 1; /* arredondamento da divisão inteira na borda */
    }
    BarButton button = (BarButton)index;

    if (button == BAR_TOGGLE_MENU && !ui->connected) {
        /* "Menu" vira "Sair" sem sessão ativa — zoom/scroll de Pi não fazem sentido
         * antes de conectar, mas o usuário precisa de algum jeito de sair do app sem
         * precisar se conectar a algo primeiro (achado do usuário, 27/08: antes disso
         * o botão só ficava bloqueado, sem alternativa nenhuma de saída na tela de
         * conexão). Ação imediata, não um painel — mesmo tratamento do scroll. */
        start_flash(ui, FLASH_BAR_BUTTON, index);
        ui->on_action(MENU_ACTION_QUIT, ui->callback_user_data);
        return;
    }

    if (button == BAR_TOGGLE_KEYBOARD || button == BAR_TOGGLE_MENU) {
        toggle_panel(ui, button);
        /* mudança de painel é grande (pode até mudar tamanho de frame) — redesenha
         * tudo em vez de calcular a região exata, mais simples e o custo já é aceito
         * (ação deliberada e pouco frequente, não um hot path). */
        cancel_flash(ui);
        gtk_widget_queue_draw(ui->drawing_area);
        return;
    }

    if (ui->panel_mode == PANEL_CONNECT_LIST) {
        /* Sem sessão ativa, ↑/↓ não têm o que rolar no Pi — repropósito pra rolar a
         * lista de conexões salvas em vez de mandar scroll pro servidor (sugestão do
         * usuário, 27/08: evita precisar de uma barra de rolagem própria). */
        int total_rows = ui->connect_entry_count + 1; /* +1 pra linha "+" */
        int max_offset = total_rows > CONNECT_LIST_VISIBLE_ROWS
                              ? total_rows - CONNECT_LIST_VISIBLE_ROWS
                              : 0;
        if (button == BAR_SCROLL_UP && ui->connect_scroll_offset > 0) {
            ui->connect_scroll_offset--;
        } else if (button == BAR_SCROLL_DOWN && ui->connect_scroll_offset < max_offset) {
            ui->connect_scroll_offset++;
        }
        start_flash(ui, FLASH_BAR_BUTTON, index);
        gtk_widget_queue_draw_area(ui->drawing_area, 0, ui->keyboard_top, ui->screen_width,
                                    ui->bar_top - ui->keyboard_top);
        return;
    }

    /* Nos demais casos (sessão conectada, ou lista fora de vista): comportamento normal
     * — session_send_scroll já ignora em silêncio sem conexão ativa (ver session.c), daí
     * não precisar bloquear explicitamente PANEL_CONNECT_FORM/PANEL_CONNECTING/nenhum. */
    start_flash(ui, FLASH_BAR_BUTTON, index);
    ui->on_bar(button, ui->callback_user_data);
}

static void handle_connect_list_panel_tap(Ui *ui, int x, int panel_y) {
    (void)x;
    int row_h = connect_list_row_h(ui);
    int row = panel_y / row_h + ui->connect_scroll_offset;
    int total_rows = ui->connect_entry_count + 1;
    if (row >= total_rows) {
        return; /* abaixo da última linha visível (sobra da divisão) — sem alvo */
    }

    if (row == ui->connect_entry_count) {
        /* linha "+": entra no formulário do zero — porta pré-preenchida com o default
         * mais comum (poupa digitação no caso comum), host/senha vazios */
        ui->form_host[0] = '\0';
        snprintf(ui->form_port, sizeof(ui->form_port), "5901");
        ui->form_password[0] = '\0';
        ui->form_active = FORM_FIELD_HOST;
        ui->pre_connect_panel = PANEL_CONNECT_FORM;
        apply_panel_mode(ui, PANEL_CONNECT_FORM);
        gtk_widget_queue_draw(ui->drawing_area);
        return;
    }

    ui->on_connect_request(ui->connect_entries[row].host, ui->connect_entries[row].port,
                           ui->connect_entries[row].password, ui->callback_user_data);
}

/* Backspace (0xFF08, ver keyboard.h) apaga o último char; ASCII imprimível (0x21-0x7E)
 * é anexado; espaço (0x20) e o resto (setas, Tab, Enter, o keysym-fantasma de um chord
 * Ctrl) são ignorados em silêncio — espaço porque nenhum dos três campos aceita
 * (IP/porta por natureza; senha porque o formato do connection_store é separado por
 * espaço, ver connection_store.h), o resto porque não faz sentido num campo de texto
 * local. */
static void append_or_backspace(char *buf, size_t buf_len, uint32_t keysym) {
    size_t len = strlen(buf);
    if (keysym == 0xFF08) {
        if (len > 0) {
            buf[len - 1] = '\0';
        }
    } else if (keysym > 0x20 && keysym <= 0x7E && len + 1 < buf_len) {
        buf[len] = (char)keysym;
        buf[len + 1] = '\0';
    }
}

/* Buffer + tamanho do campo ativo do formulário — par de helpers pro roteamento de
 * digitação em handle_panel_tap. */
static char *form_active_buf(Ui *ui, size_t *out_len) {
    switch (ui->form_active) {
    case FORM_FIELD_PORT:
        *out_len = sizeof(ui->form_port);
        return ui->form_port;
    case FORM_FIELD_PASSWORD:
        *out_len = sizeof(ui->form_password);
        return ui->form_password;
    case FORM_FIELD_HOST:
    default:
        *out_len = sizeof(ui->form_host);
        return ui->form_host;
    }
}

static void handle_panel_tap(Ui *ui, int x, int y) {
    int panel_y = y - ui->keyboard_top;

    if (ui->panel_mode == PANEL_MENU) {
        int index = find_menu_item_index(ui, x, panel_y);
        if (index >= 0) {
            start_flash(ui, FLASH_MENU_ITEM, index);
            ui->on_action(kMenuItems[index].action, ui->callback_user_data);
        }
        return;
    }
    if (ui->panel_mode == PANEL_CONNECT_LIST) {
        handle_connect_list_panel_tap(ui, x, panel_y);
        return;
    }
    if (ui->panel_mode == PANEL_CONNECTING) {
        /* linha única "Voltar" ocupando o painel inteiro — mesma convenção de
         * find_menu_item, que também não desconta o inset da área tocável */
        ui->on_back(ui->callback_user_data);
        return;
    }

    /* PANEL_KEYBOARD (sessão real) ou PANEL_CONNECT_FORM (formulário "+") — MESMA
     * instância de Keyboard nos dois casos; só o destino dos eventos muda. */
    int tapped = keyboard_key_index_at(ui->keyboard, x, panel_y);
    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool right_click = false;
    bool visual_changed = false;
    int n = keyboard_handle_tap(ui->keyboard, x, panel_y, events, &right_click,
                                &visual_changed);

    if (ui->panel_mode == PANEL_CONNECT_FORM) {
        size_t active_len;
        char *active = form_active_buf(ui, &active_len);
        for (int i = 0; i < n; i++) {
            if (events[i].down) {
                append_or_backspace(active, active_len, events[i].keysym);
            }
        }
        if (n > 0) {
            /* texto pode ter mudado — redesenha só as 2 fileiras de campos (IP+Porta e
             * Senha), na área do frame (mais barato no e-ink que redesenhar tudo) */
            gtk_widget_queue_draw_area(ui->drawing_area, 0, 0, ui->screen_width,
                                        2 * connect_form_row_h(ui));
        }
        /* right_click (tecla "Direito") não faz sentido aqui — inerte de propósito */
    } else {
        for (int i = 0; i < n; i++) {
            ui->on_key(events[i].keysym, events[i].down, ui->callback_user_data);
        }
        if (right_click) {
            /* ação imediata (tecla "Direito") — não sticky, não muda desenho do teclado */
            ui->on_right_click(ui->callback_user_data);
        }
    }

    if (visual_changed) {
        /* sticky/página: o redraw da faixa inteira já é o feedback — sem flash (e o
         * flash pendente de antes morre junto, o estado da página pode ter mudado) */
        cancel_flash(ui);
        gtk_widget_queue_draw_area(ui->drawing_area, 0, ui->keyboard_top, ui->screen_width,
                                    ui->bar_top - ui->keyboard_top);
    } else if (tapped >= 0) {
        /* tecla normal: pisca invertida por um instante */
        start_flash(ui, FLASH_KEYBOARD_KEY, tapped);
    }
}

/* Toque no FRAME com o clique esquerdo armado (tecla "Esquerdo", teclado): consome o
 * arme (fora da grade do teclado, por isso via keyboard_consume_left_click_arm — ver
 * keyboard.h) e inicia o arrasto. Diferente de um clique normal, não manda press+release
 * na mesma chamada — o release só acontece quando o dedo LEVANTAR (on_button_release). */
static void start_drag(Ui *ui, int x, int y) {
    keyboard_consume_left_click_arm(ui->keyboard);
    ui->dragging = true;
    ui->drag_last_x = x;
    ui->drag_last_y = y;
    if (ui->panel_mode == PANEL_KEYBOARD) {
        /* "Esquerdo" pode estar visível agora, destacado — o consumo aconteceu numa
         * área diferente da que o desenhou, então precisa de redraw explícito pra
         * tirar o destaque (diferente de um toque na própria grade, que já dispara
         * redraw sozinho via visual_changed). */
        gtk_widget_queue_draw_area(ui->drawing_area, 0, ui->keyboard_top, ui->screen_width,
                                    ui->bar_top - ui->keyboard_top);
    }
    ui->on_drag(x, y, true, ui->callback_user_data);
}

/* Toque na área do FRAME do formulário "+" (fileira IP+Porta, fileira Senha, fileira
 * Conectar/Cancelar — ver draw_connect_form_fields) — só chamado quando
 * pre_connect_panel==PANEL_CONNECT_FORM (independente do painel/teclado estar visível
 * ou não, ver on_button_press). */
static void handle_connect_form_frame_tap(Ui *ui, int x, int y) {
    int row_h = connect_form_row_h(ui);
    int ip_w = ui->screen_width * CONNECT_FORM_IP_WIDTH_PERCENT / 100;

    if (y < 2 * row_h) {
        /* fileiras de campos: só troca qual campo recebe o próximo toque de tecla, sem
         * mexer no texto já digitado em nenhum deles */
        if (y < row_h) {
            ui->form_active = (x < ip_w) ? FORM_FIELD_HOST : FORM_FIELD_PORT;
        } else {
            ui->form_active = FORM_FIELD_PASSWORD;
        }
        gtk_widget_queue_draw_area(ui->drawing_area, 0, 0, ui->screen_width, 2 * row_h);
        return;
    }
    if (y < 3 * row_h) {
        /* fileira dos botões: Conectar (esquerda) / Cancelar (direita) */
        if (x < ui->screen_width / 2) {
            if (ui->form_host[0] == '\0') {
                return; /* IP vazio: ignora o toque, nada de válido pra conectar ainda */
            }
            int port = atoi(ui->form_port);
            if (port <= 0 || port > 65535) {
                return; /* porta inválida: mesmo raciocínio, ignora em silêncio */
            }
            ui->on_connect_request(ui->form_host, port, ui->form_password,
                                   ui->callback_user_data);
        } else {
            ui->pre_connect_panel = PANEL_CONNECT_LIST;
            apply_panel_mode(ui, PANEL_CONNECT_LIST);
            gtk_widget_queue_draw(ui->drawing_area);
        }
        return;
    }
    /* abaixo dos botões: resto do frame, deliberadamente em branco — sem alvo */
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    Ui *ui = user_data;
    int x = (int)event->x;
    int y = (int)event->y;

    if (y >= ui->bar_top) {
        handle_bar_tap(ui, x);
    } else if (y < ui->keyboard_top) {
        if (!ui->connected) {
            /* Mesma prioridade de on_expose/toggle_panel: connecting vence
             * pre_connect_panel — achado de review, 27/08: sem o `!ui->connecting`
             * aqui, um toque no frame durante "Conectando..." caía no handler do
             * formulário (mesma geometria), podendo reiniciar a tentativa ou trocar de
             * painel sem derrubar a conexão pendente. */
            if (!ui->connecting && ui->pre_connect_panel == PANEL_CONNECT_FORM) {
                handle_connect_form_frame_tap(ui, x, y);
            }
            /* lista ou conectando: frame em branco, nada tocável ali */
        } else if (keyboard_left_click_armed(ui->keyboard)) {
            start_drag(ui, x, y);
        } else {
            ui->on_click(x, y, ui->callback_user_data);
        }
    } else {
        handle_panel_tap(ui, x, y);
    }
    return TRUE;
}

/* Posições intermediárias enquanto o dedo desliza (arrasto em andamento). event->is_hint
 * com GDK_POINTER_MOTION_HINT_MASK é o idiom clássico do GTK2 pra throttling: o GDK só
 * entrega o PRÓXIMO motion depois que a gente pede a posição de novo
 * (gdk_window_get_pointer), nunca deixando mais de um eventos pendente — evita
 * inundação se o hardware amostrar mais rápido do que a gente processa. Complementado
 * por um throttle de distância mínima (DRAG_MIN_MOVE_PX) — não validado no hardware
 * ainda, ver comentário na definição. */
static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    Ui *ui = user_data;
    if (!ui->dragging) {
        return TRUE; /* motion sem arrasto ativo não deveria disparar, mas por garantia */
    }

    int x, y;
    if (event->is_hint) {
        gdk_window_get_pointer(widget->window, &x, &y, NULL);
    } else {
        x = (int)event->x;
        y = (int)event->y;
    }

    int dx = x - ui->drag_last_x;
    int dy = y - ui->drag_last_y;
    if (dx * dx + dy * dy < DRAG_MIN_MOVE_PX * DRAG_MIN_MOVE_PX) {
        return TRUE;
    }

    ui->drag_last_x = x;
    ui->drag_last_y = y;
    ui->on_drag(x, y, true, ui->callback_user_data);
    return TRUE;
}

/* Dedo levantou da tela: fim do arrasto, sem precisar apertar nenhum botão de novo pra
 * soltar (decisão de 27/08 — ver UiDragFn no .h pro raciocínio completo). */
static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    Ui *ui = user_data;
    if (!ui->dragging) {
        return TRUE; /* release de um clique normal (press+release já foi tudo numa
                       * chamada só) — nada a fazer aqui */
    }
    ui->dragging = false;
    ui->on_drag((int)event->x, (int)event->y, false, ui->callback_user_data);
    return TRUE;
}

Ui *ui_create(const char *window_title, UiClickFn on_click, UiKeyFn on_key,
              UiActionFn on_action, UiBarFn on_bar, UiResizeFn on_resize, UiDragFn on_drag,
              UiRightClickFn on_right_click, UiConnectRequestFn on_connect_request,
              UiBackFn on_back, void *user_data) {
    Ui *ui = g_new0(Ui, 1);
    ui->on_click = on_click;
    ui->on_key = on_key;
    ui->on_action = on_action;
    ui->on_bar = on_bar;
    ui->on_resize = on_resize;
    ui->on_drag = on_drag;
    ui->on_right_click = on_right_click;
    ui->on_connect_request = on_connect_request;
    ui->on_back = on_back;
    ui->callback_user_data = user_data;
    /* flash_source, dragging, connected e connecting nascem FLASH_NONE/false (== 0) via
     * g_new0. panel_mode e pre_connect_panel nascem PANEL_CONNECT_LIST explicitamente
     * abaixo — o app começa sem sessão nenhuma (ver docs/ideias-futuras.md item 5);
     * main.c chama ui_show_connect_list logo em seguida com a lista de verdade, isso
     * aqui só evita desenhar lixo no primeiro expose que o GTK dispara ao mapear a
     * janela. */
    ui->panel_mode = PANEL_CONNECT_LIST;
    ui->pre_connect_panel = PANEL_CONNECT_LIST;
    /* Resolução real detectada em runtime, não hardcoded — o mesmo binário serve qualquer
     * modelo de Kindle que conectar. */
    ui->screen_width = gdk_screen_width();
    ui->screen_height = gdk_screen_height();
    /* A barra é sempre visível, altura em % com piso de segurança (etapa 5 — ver defines
     * no topo). O painel ocupa uma fração do espaço que SOBRA acima dela (não da tela
     * inteira) — assim a proporção não muda com a presença da barra. */
    int bar_height = ui->screen_height * BAR_HEIGHT_PERCENT / 100;
    if (bar_height < BAR_HEIGHT_MIN_PX) {
        bar_height = BAR_HEIGHT_MIN_PX;
    }
    ui->bar_top = ui->screen_height - bar_height;
    ui->keyboard_top = ui->bar_top * (100 - KEYBOARD_HEIGHT_PERCENT) / 100;
    ui->keyboard = keyboard_create(ui->screen_width, ui->bar_top - ui->keyboard_top);
    if (!ui->keyboard) {
        g_free(ui);
        return NULL;
    }

    ui->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui->window), window_title);
    /* Fechar a janela encerra o app — não existe modo "janela fechada mas rodando". */
    g_signal_connect(ui->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    ui->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(ui->drawing_area, ui->screen_width, ui->screen_height);
    /* RELEASE e MOTION (+ HINT, pro coalescing) são novos — etapa 3, pro arrasto do
     * clique esquerdo. PRESS já existia. */
    gtk_widget_add_events(ui->drawing_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                            GDK_POINTER_MOTION_MASK |
                                            GDK_POINTER_MOTION_HINT_MASK);
    g_signal_connect(ui->drawing_area, "expose-event", G_CALLBACK(on_expose), ui);
    g_signal_connect(ui->drawing_area, "button-press-event", G_CALLBACK(on_button_press), ui);
    g_signal_connect(ui->drawing_area, "button-release-event", G_CALLBACK(on_button_release),
                      ui);
    g_signal_connect(ui->drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), ui);
    gtk_container_add(GTK_CONTAINER(ui->window), ui->drawing_area);

    gtk_widget_show_all(ui->window);
    return ui;
}

int ui_frame_width(const Ui *ui) {
    return ui->screen_width;
}

int ui_frame_height(const Ui *ui) {
    return ui->keyboard_top;
}

void ui_show_frame(Ui *ui, int width, int height, const uint32_t *argb32_pixels) {
    struct timespec t0 = timing_now();

    if (ui->surface) {
        cairo_surface_destroy(ui->surface);
    }
    ui->surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    if (cairo_surface_status(ui->surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(ui->surface);
        ui->surface = NULL;
        return;
    }

    unsigned char *dst = cairo_image_surface_get_data(ui->surface);
    int stride = cairo_image_surface_get_stride(ui->surface);
    for (int y = 0; y < height; y++) {
        memcpy(dst + y * stride, argb32_pixels + (size_t)y * width, width * sizeof(uint32_t));
    }
    cairo_surface_mark_dirty(ui->surface);

    struct timespec t1 = timing_now();
    g_printerr("kindow: cópia pro cairo_surface (%dx%d) levou %ld ms\n", width, height,
               timing_elapsed_ms(t0, t1));

    ui->paint_requested_at = timing_now();
    /* Redraw só da área do frame — a faixa do teclado nunca é suja por um frame novo
     * (enquanto o servidor ainda manda o tamanho antigo, maior que a área útil, o clip do
     * expose limita o desenho de qualquer jeito). */
    gtk_widget_queue_draw_area(ui->drawing_area, 0, 0, ui->screen_width, ui->keyboard_top);
}

void ui_show_connect_list(Ui *ui, const UiConnectionEntry *entries, int count,
                          int selected_index) {
    if (count > UI_CONNECT_ENTRIES_MAX) {
        count = UI_CONNECT_ENTRIES_MAX; /* mesmo teto de connection_store.h — não deveria
                                          * acontecer, mas não vale travar por isso */
    }
    ui->connect_entry_count = count;
    for (int i = 0; i < count; i++) {
        snprintf(ui->connect_entries[i].host, sizeof(ui->connect_entries[i].host), "%s",
                 entries[i].host);
        ui->connect_entries[i].port = entries[i].port;
        snprintf(ui->connect_entries[i].password, sizeof(ui->connect_entries[i].password),
                 "%s", entries[i].password ? entries[i].password : "");
    }
    ui->connect_selected_index = selected_index;
    ui->connect_scroll_offset = 0;
    ui->connected = false;
    ui->connecting = false;
    ui->pre_connect_panel = PANEL_CONNECT_LIST;
    apply_panel_mode(ui, PANEL_CONNECT_LIST);
    cancel_flash(ui);
    gtk_widget_queue_draw(ui->drawing_area);
}

void ui_show_connecting(Ui *ui, const char *host, int port) {
    snprintf(ui->connecting_host, sizeof(ui->connecting_host), "%s", host);
    ui->connecting_port = port;
    ui->connecting_error[0] = '\0'; /* conexão nova começa sem erro nenhum na tela */
    ui->connected = false;
    ui->connecting = true;
    apply_panel_mode(ui, PANEL_CONNECTING);
    cancel_flash(ui);
    gtk_widget_queue_draw(ui->drawing_area);
}

void ui_show_connect_error(Ui *ui, const char *detail) {
    if (!ui->connecting) {
        return; /* só faz sentido com a tela de conectando ativa (ver contrato no .h) */
    }
    snprintf(ui->connecting_error, sizeof(ui->connecting_error), "%s",
             detail ? detail : tr(STR_CHECK_FIELDS));
    /* só a área do frame muda (as 2 fileiras da mensagem) — painel/barra ficam como
     * estão */
    gtk_widget_queue_draw_area(ui->drawing_area, 0, 0, ui->screen_width, ui->keyboard_top);
}

void ui_show_session(Ui *ui) {
    ui->connected = true;
    ui->connecting = false;
    ui->connecting_error[0] = '\0';
    /* Limpa qualquer arme residual do clique-esquerdo que possa ter sobrado de um toque
     * na página de símbolos durante o formulário "+" — a mesma instância de Keyboard é
     * reaproveitada entre a tela de conexão e a sessão real; sem isso, um arme
     * esquecido ali vazaria pro primeiro arrasto de verdade (mesma classe de bug do
     * achado de review de 27/08 pro caso letras<->símbolos/teclado<->menu). */
    keyboard_consume_left_click_arm(ui->keyboard);
    apply_panel_mode(ui, PANEL_KEYBOARD);
    cancel_flash(ui);
    gtk_widget_queue_draw(ui->drawing_area);
}

void ui_destroy(Ui *ui) {
    if (!ui) {
        return;
    }
    if (ui->flash_timer_id) {
        g_source_remove(ui->flash_timer_id);
    }
    if (ui->surface) {
        cairo_surface_destroy(ui->surface);
    }
    keyboard_destroy(ui->keyboard);
    g_free(ui);
}
