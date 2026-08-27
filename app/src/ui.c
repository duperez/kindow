#include "ui.h"

#include <gtk/gtk.h>
#include <string.h>

#include "keyboard.h"
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

/* O painel entre o frame e a barra: nada (frame estendido até a barra), teclado, ou menu
 * — mutuamente exclusivos, alternados pelos botões Teclado/Menu da barra (reestrutura de
 * 27/08). Teclado e menu reservam a MESMA altura (ver KEYBOARD_HEIGHT_PERCENT), então só
 * a transição nada<->algo muda o tamanho útil do frame. */
typedef enum {
    PANEL_NONE,
    PANEL_KEYBOARD,
    PANEL_MENU,
} PanelMode;

/* De onde veio o índice em flash — teclado e barra têm esquemas de geometria diferentes
 * (keyboard_key_view vs. segmento fixo), então o mesmo índice numérico significa coisas
 * diferentes dependendo da origem. */
typedef enum {
    FLASH_NONE,
    FLASH_KEYBOARD_KEY,
    FLASH_BAR_BUTTON,
} FlashSource;

struct Ui {
    GtkWidget *window;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    int screen_width;
    int screen_height;
    /* y onde o painel (teclado/menu) começa — == altura útil pro frame remoto. Igual a
     * bar_top quando panel_mode é PANEL_NONE (painel sem altura reservada). */
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
    void *callback_user_data;
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
 * que é o feedback de "armado" sem precisar de tons de cinza. */
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
    const char *label;
    MenuAction action;
} MenuItem;

#define MENU_ROWS 6

static const MenuItem kMenuItems[] = {
    {0, 0.0f, 0.5f, "Apps  A-", MENU_ACTION_ZOOM_APPS_OUT},
    {0, 0.5f, 1.0f, "Apps  A+", MENU_ACTION_ZOOM_APPS_IN},
    {1, 0.0f, 0.5f, "Janela  A-", MENU_ACTION_ZOOM_DECO_OUT},
    {1, 0.5f, 1.0f, "Janela  A+", MENU_ACTION_ZOOM_DECO_IN},
    {2, 0.0f, 0.5f, "Painel  A-", MENU_ACTION_ZOOM_PANEL_OUT},
    {2, 0.5f, 1.0f, "Painel  A+", MENU_ACTION_ZOOM_PANEL_IN},
    /* Etapa 4 da reestrutura (27/08): quantas catracas de roda o scroll manda por toque
     * — puramente client-side, não passa pelo kindow-helperd/Pi (ver session.c). */
    {3, 0.0f, 0.5f, "Scroll  A-", MENU_ACTION_SCROLL_LINES_OUT},
    {3, 0.5f, 1.0f, "Scroll  A+", MENU_ACTION_SCROLL_LINES_IN},
    {4, 0.0f, 1.0f, "Status da conexão (log)", MENU_ACTION_STATUS},
    {5, 0.0f, 1.0f, "Sair do Kindow", MENU_ACTION_QUIT},
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

        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_line_width(cr, proportional_border_width(row_h));
        cairo_rectangle(cr, x, y, w, h);
        cairo_stroke(cr);
        draw_centered_label(cr, x, y, w, h, item->label);
    }

    cairo_restore(cr);
}

static const MenuItem *find_menu_item(const Ui *ui, int x, int y) {
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
            return &kMenuItems[i];
        }
    }
    return NULL;
}

/* Rótulos dos 4 botões, na mesma ordem do enum BarButton. Setas ↑/↓ já confirmadas
 * renderizando certo nesse device (mesma fonte do teclado) — ver kindle-hardware-test.md. */
static const char *const kBarLabels[BAR_BUTTON_COUNT] = {"↑", "↓", "Teclado", "Menu"};

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
    for (int i = 0; i < BAR_BUTTON_COUNT; i++) {
        int x = i * seg_w + inset;
        int y = inset;
        /* último segmento absorve o resto da divisão inteira, igual às fileiras do
         * teclado — sem isso sobraria uma faixa sem borda na ponta direita. */
        int w = (i == BAR_BUTTON_COUNT - 1 ? ui->screen_width - i * seg_w : seg_w) - 2 * inset;
        int h = bar_height - 2 * inset;

        bool highlighted = (i == BAR_TOGGLE_KEYBOARD && ui->panel_mode == PANEL_KEYBOARD) ||
                           (i == BAR_TOGGLE_MENU && ui->panel_mode == PANEL_MENU);
        bool flashing = ui->flash_source == FLASH_BAR_BUTTON && ui->flash_index == i;

        if (highlighted || flashing) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, x, y, w, h);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            draw_centered_label(cr, x, y, w, h, kBarLabels[i]);
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, border_w);
            cairo_rectangle(cr, x, y, w, h);
            cairo_stroke(cr);
            draw_centered_label(cr, x, y, w, h, kBarLabels[i]);
        }
    }

    cairo_restore(cr);
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
    if (ui->surface) {
        cairo_set_source_surface(cr, ui->surface, 0, 0);
        cairo_paint(cr);
    }
    if (event->area.y + event->area.height > ui->keyboard_top && event->area.y < ui->bar_top) {
        if (ui->panel_mode == PANEL_KEYBOARD) {
            draw_keyboard(cr, ui);
        } else if (ui->panel_mode == PANEL_MENU) {
            draw_menu(cr, ui);
        }
    }
    if (event->area.y + event->area.height > ui->bar_top) {
        draw_bar(cr, ui);
    }

    cairo_destroy(cr);

    if (ui->surface && event->area.y < ui->keyboard_top) {
        /* só loga a fila de espera quando o redraw inclui o frame de verdade — expose só
         * do teclado (ou antes de qualquer frame chegar) não tem paint_requested_at
         * válido pra comparar. */
        struct timespec t1 = timing_now();
        g_printerr("kindow: fila até o redraw %ld ms, cairo_paint %ld ms\n",
                   timing_elapsed_ms(ui->paint_requested_at, t0), timing_elapsed_ms(t0, t1));
    }
    return TRUE;
}

/* Agenda o redraw só do retângulo de UM alvo de flash (tecla do teclado ou botão da
 * barra — cada fonte tem seu próprio esquema de geometria). */
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

/* Abre/troca/fecha o painel conforme as 3 regras combinadas (27/08): nada aberto -> abre
 * o tocado; o outro aberto -> troca pro tocado; o próprio já aberto -> fecha (volta a
 * PANEL_NONE). Teclado e menu reservam a MESMA altura, então só a transição nada<->algo
 * dispara resize — trocar entre os dois com o painel já aberto é só redesenho local. */
static void toggle_panel(Ui *ui, BarButton button) {
    PanelMode wanted = (button == BAR_TOGGLE_KEYBOARD) ? PANEL_KEYBOARD : PANEL_MENU;
    bool was_open = ui->panel_mode != PANEL_NONE;

    ui->panel_mode = (ui->panel_mode == wanted) ? PANEL_NONE : wanted;
    /* Mesmo raciocínio da troca de página dentro do teclado (achado de review, 27/08):
     * "Esquerdo" só existe na página de símbolos do teclado — sair do painel de teclado
     * de qualquer jeito (pro menu, ou fechando) deixaria um arme vivo sem indicador
     * visual nenhum. Idempotente quando já desarmado, sem custo em trocar
     * letras<->símbolos->letras sem nunca ter armado nada. */
    keyboard_consume_left_click_arm(ui->keyboard);

    bool now_open = ui->panel_mode != PANEL_NONE;
    if (was_open != now_open) {
        ui->keyboard_top = now_open ? ui->bar_top * (100 - KEYBOARD_HEIGHT_PERCENT) / 100
                                     : ui->bar_top;
        if (ui->on_resize) {
            ui->on_resize(ui->screen_width, ui->keyboard_top, ui->callback_user_data);
        }
    }
}

static void handle_bar_tap(Ui *ui, int x) {
    int seg_w = ui->screen_width / BAR_BUTTON_COUNT;
    int index = x / seg_w;
    if (index >= BAR_BUTTON_COUNT) {
        index = BAR_BUTTON_COUNT - 1; /* arredondamento da divisão inteira na borda */
    }
    BarButton button = (BarButton)index;

    if (button == BAR_TOGGLE_KEYBOARD || button == BAR_TOGGLE_MENU) {
        toggle_panel(ui, button);
        /* mudança de painel é grande (pode até mudar tamanho de frame) — redesenha tudo
         * em vez de calcular a região exata, mais simples e o custo já é aceito
         * (ação deliberada e pouco frequente, não um hot path). */
        cancel_flash(ui);
        gtk_widget_queue_draw(ui->drawing_area);
    } else {
        start_flash(ui, FLASH_BAR_BUTTON, index);
        ui->on_bar(button, ui->callback_user_data);
    }
}

static void handle_panel_tap(Ui *ui, int x, int y) {
    int panel_y = y - ui->keyboard_top;

    if (ui->panel_mode == PANEL_MENU) {
        const MenuItem *item = find_menu_item(ui, x, panel_y);
        if (item) {
            ui->on_action(item->action, ui->callback_user_data);
        }
        return; /* menu não tem flash — cada toque já dispara a ação, sem estado local */
    }

    int tapped = keyboard_key_index_at(ui->keyboard, x, panel_y);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool right_click = false;
    bool visual_changed = false;
    int n = keyboard_handle_tap(ui->keyboard, x, panel_y, events, &right_click,
                                &visual_changed);
    for (int i = 0; i < n; i++) {
        ui->on_key(events[i].keysym, events[i].down, ui->callback_user_data);
    }
    if (right_click) {
        /* ação imediata (tecla "Direito") — não sticky, não muda desenho do teclado */
        ui->on_right_click(ui->callback_user_data);
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

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    Ui *ui = user_data;
    int x = (int)event->x;
    int y = (int)event->y;

    if (y >= ui->bar_top) {
        handle_bar_tap(ui, x);
    } else if (y < ui->keyboard_top) {
        if (keyboard_left_click_armed(ui->keyboard)) {
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
              UiRightClickFn on_right_click, void *user_data) {
    Ui *ui = g_new0(Ui, 1);
    ui->on_click = on_click;
    ui->on_key = on_key;
    ui->on_action = on_action;
    ui->on_bar = on_bar;
    ui->on_resize = on_resize;
    ui->on_drag = on_drag;
    ui->on_right_click = on_right_click;
    ui->callback_user_data = user_data;
    /* flash_source e dragging nascem FLASH_NONE/false (== 0) via g_new0; panel_mode
     * nasce PANEL_KEYBOARD explicitamente abaixo — estado inicial igual ao
     * comportamento de antes da barra existir (teclado sempre visível). */
    ui->panel_mode = PANEL_KEYBOARD;
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
