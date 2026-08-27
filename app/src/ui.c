#include "ui.h"

#include <gtk/gtk.h>
#include <string.h>

#include "keyboard.h"
#include "timing.h"

/* Fração da altura da tela reservada pro teclado (faixa fixa embaixo — decisão de 26/08:
 * melhor pro e-ink que um overlay que aparece/some, porque a região nunca muda depois de
 * pintada; a variante overlay/gesto ficou como ideia futura configurável via menu, ver
 * docs/ideias-futuras.md). 35% em 1448px dá ~505px: 6 fileiras de ~84px, tecla padrão de
 * ~107px de largura em 1072px — tamanho confortável de dedo. */
#define KEYBOARD_HEIGHT_PERCENT 35

struct Ui {
    GtkWidget *window;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    int screen_width;
    int screen_height;
    /* y onde a faixa do teclado começa (== altura útil pro frame remoto) */
    int keyboard_top;
    Keyboard *keyboard;
    UiClickFn on_click;
    UiKeyFn on_key;
    void *callback_user_data;
    /* timestamp de quando o último gtk_widget_queue_draw foi pedido — usado só pra medir
     * quanto tempo até o expose de verdade rodar (ver on_expose). Zerado no início; a
     * primeira chamada de on_expose (antes de qualquer frame chegar) ignora essa medição. */
    struct timespec paint_requested_at;
};

static void draw_key_label(cairo_t *cr, const KeyboardKeyView *key) {
    if (!key->label[0]) {
        return; /* barra de espaço: sem rótulo */
    }
    /* rótulo de 1 caractere (contando UTF-8 multi-byte como 1) ganha fonte maior que
     * palavra (Esc, Tab, Ctrl...) — heurística: palavras têm 2+ chars ASCII */
    bool is_word = key->label[1] != '\0' && !(key->label[0] & 0x80);
    cairo_set_font_size(cr, key->h * (is_word ? 0.30 : 0.42));

    cairo_text_extents_t ext;
    cairo_text_extents(cr, key->label, &ext);
    cairo_move_to(cr, key->x + (key->w - ext.width) / 2.0 - ext.x_bearing,
                  key->y + (key->h - ext.height) / 2.0 - ext.y_bearing);
    cairo_show_text(cr, key->label);
}

/* Desenho do teclado: alto contraste pro e-ink — teclas brancas com borda preta, rótulo
 * preto; modificador armado (sticky Shift/Ctrl) invertido (fundo preto, texto branco),
 * que é o feedback de "armado" sem precisar de tons de cinza. */
static void draw_keyboard(cairo_t *cr, const Ui *ui) {
    cairo_save(cr);
    cairo_translate(cr, 0, ui->keyboard_top);

    /* fundo da faixa + linha separando do frame */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, ui->screen_width, ui->screen_height - ui->keyboard_top);
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
        /* inset de 3px entre teclas vizinhas, sem depender de gap no layout */
        key.x += 3;
        key.y += 3;
        key.w -= 6;
        key.h -= 6;

        if (key.highlighted) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, key.x, key.y, key.w, key.h);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            draw_key_label(cr, &key);
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, 2);
            cairo_rectangle(cr, key.x, key.y, key.w, key.h);
            cairo_stroke(cr);
            draw_key_label(cr, &key);
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
    if (event->area.y + event->area.height > ui->keyboard_top) {
        draw_keyboard(cr, ui);
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

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    Ui *ui = user_data;
    int x = (int)event->x;
    int y = (int)event->y;

    if (y < ui->keyboard_top) {
        ui->on_click(x, y, ui->callback_user_data);
        return TRUE;
    }

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;
    int n = keyboard_handle_tap(ui->keyboard, x, y - ui->keyboard_top, events, &visual_changed);
    for (int i = 0; i < n; i++) {
        ui->on_key(events[i].keysym, events[i].down, ui->callback_user_data);
    }
    if (visual_changed) {
        gtk_widget_queue_draw_area(ui->drawing_area, 0, ui->keyboard_top, ui->screen_width,
                                    ui->screen_height - ui->keyboard_top);
    }
    return TRUE;
}

Ui *ui_create(const char *window_title, UiClickFn on_click, UiKeyFn on_key, void *user_data) {
    Ui *ui = g_new0(Ui, 1);
    ui->on_click = on_click;
    ui->on_key = on_key;
    ui->callback_user_data = user_data;
    /* Resolução real detectada em runtime, não hardcoded — o mesmo binário serve qualquer
     * modelo de Kindle que conectar. */
    ui->screen_width = gdk_screen_width();
    ui->screen_height = gdk_screen_height();
    ui->keyboard_top = ui->screen_height * (100 - KEYBOARD_HEIGHT_PERCENT) / 100;
    ui->keyboard = keyboard_create(ui->screen_width, ui->screen_height - ui->keyboard_top);
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
    gtk_widget_add_events(ui->drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(ui->drawing_area, "expose-event", G_CALLBACK(on_expose), ui);
    g_signal_connect(ui->drawing_area, "button-press-event", G_CALLBACK(on_button_press), ui);
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
    if (ui->surface) {
        cairo_surface_destroy(ui->surface);
    }
    keyboard_destroy(ui->keyboard);
    g_free(ui);
}
