#include "ui.h"

#include <gtk/gtk.h>
#include <string.h>

#include "timing.h"

struct Ui {
    GtkWidget *window;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    int screen_width;
    int screen_height;
    UiClickFn on_click;
    void *on_click_user_data;
    /* timestamp de quando o último gtk_widget_queue_draw foi pedido — usado só pra medir
     * quanto tempo até o expose de verdade rodar (ver on_expose). Zerado no início; a
     * primeira chamada de on_expose (antes de qualquer frame chegar) ignora essa medição. */
    struct timespec paint_requested_at;
};

static gboolean on_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data) {
    (void)event;
    Ui *ui = user_data;
    struct timespec t0 = timing_now();

    cairo_t *cr = gdk_cairo_create(widget->window);

    if (ui->surface) {
        cairo_set_source_surface(cr, ui->surface, 0, 0);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
    }

    cairo_destroy(cr);

    if (ui->surface) {
        /* só loga a fila de espera quando existe um frame de verdade sendo pintado —
         * o primeiro expose (tela em branco, antes de qualquer frame chegar) não tem um
         * paint_requested_at válido pra comparar. */
        struct timespec t1 = timing_now();
        g_printerr("kindow: fila até o redraw %ld ms, cairo_paint %ld ms\n",
                   timing_elapsed_ms(ui->paint_requested_at, t0), timing_elapsed_ms(t0, t1));
    }
    return TRUE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    Ui *ui = user_data;
    ui->on_click((int)event->x, (int)event->y, ui->on_click_user_data);
    return TRUE;
}

Ui *ui_create(const char *window_title, UiClickFn on_click, void *user_data) {
    Ui *ui = g_new0(Ui, 1);
    ui->on_click = on_click;
    ui->on_click_user_data = user_data;
    ui->screen_width = gdk_screen_width();
    ui->screen_height = gdk_screen_height();

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

int ui_screen_width(const Ui *ui) {
    return ui->screen_width;
}

int ui_screen_height(const Ui *ui) {
    return ui->screen_height;
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
    gtk_widget_queue_draw(ui->drawing_area);
}

void ui_destroy(Ui *ui) {
    if (!ui) {
        return;
    }
    if (ui->surface) {
        cairo_surface_destroy(ui->surface);
    }
    g_free(ui);
}
