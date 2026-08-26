#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include "vnc_client.h"

/* PoC mínima: um botão "Atualizar" busca a tela do Pi sob demanda, e tocar na imagem
 * manda um clique naquele ponto e busca o resultado — cada interação abre e fecha sua
 * própria conexão (decisão em docs/findings/rfb-protocol.md). */
typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    int frame_width;
    int frame_height;
    const char *host;
    int port;
} AppState;

static void show_error(AppState *app, const char *message) {
    GtkWidget *dialog =
        gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK, "%s", message ? message : "Erro desconhecido");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_frame_ready(int width, int height, const uint32_t *argb32_pixels,
                            void *user_data) {
    AppState *app = user_data;

    if (app->surface) {
        cairo_surface_destroy(app->surface);
    }
    app->surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    if (cairo_surface_status(app->surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(app->surface);
        app->surface = NULL;
        app->frame_width = 0;
        app->frame_height = 0;
        return;
    }

    unsigned char *dst = cairo_image_surface_get_data(app->surface);
    int stride = cairo_image_surface_get_stride(app->surface);
    for (int y = 0; y < height; y++) {
        memcpy(dst + y * stride, argb32_pixels + (size_t)y * width, width * sizeof(uint32_t));
    }
    cairo_surface_mark_dirty(app->surface);

    app->frame_width = width;
    app->frame_height = height;

    gtk_widget_queue_draw(app->drawing_area);
}

static void refresh_frame(AppState *app) {
    char *error = NULL;
    VncClient *client = vnc_client_connect(app->host, app->port, &error);
    if (!client) {
        show_error(app, error);
        free(error);
        return;
    }

    if (!vnc_client_fetch_frame(client, on_frame_ready, app, &error)) {
        show_error(app, error);
        free(error);
    }

    vnc_client_disconnect(client);
}

static void on_refresh_clicked(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    refresh_frame((AppState *)user_data);
}

static gboolean on_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data) {
    (void)event;
    AppState *app = user_data;
    cairo_t *cr = gdk_cairo_create(widget->window);

    if (app->surface) {
        cairo_set_source_surface(cr, app->surface, 0, 0);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
    }

    cairo_destroy(cr);
    return TRUE;
}

static gboolean on_drawing_area_click(GtkWidget *widget, GdkEventButton *event,
                                       gpointer user_data) {
    (void)widget;
    AppState *app = user_data;
    if (app->frame_width <= 0) {
        return TRUE;
    }

    /* A área de desenho tem tamanho fixo; se o frame recebido for menor, um clique fora
     * dele ainda dispararia coordenadas inválidas pro servidor sem esse clamp. */
    int x = (int)event->x;
    int y = (int)event->y;
    if (x < 0 || y < 0 || x >= app->frame_width || y >= app->frame_height) {
        return TRUE;
    }

    char *error = NULL;
    VncClient *client = vnc_client_connect(app->host, app->port, &error);
    if (!client) {
        show_error(app, error);
        free(error);
        return TRUE;
    }

    /* clique esquerdo: pressiona e solta na mesma posição */
    vnc_client_send_pointer(client, x, y, 1);
    vnc_client_send_pointer(client, x, y, 0);

    if (!vnc_client_fetch_frame(client, on_frame_ready, app, &error)) {
        show_error(app, error);
        free(error);
    }

    vnc_client_disconnect(client);
    return TRUE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    AppState app = {0};
    app.host = argc > 1 ? argv[1] : "192.168.0.155";
    app.port = argc > 2 ? atoi(argv[2]) : 5901;

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Kindow");
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    app.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.drawing_area, 600, 800);
    gtk_widget_add_events(app.drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(app.drawing_area, "expose-event", G_CALLBACK(on_expose), &app);
    g_signal_connect(app.drawing_area, "button-press-event", G_CALLBACK(on_drawing_area_click),
                      &app);

    GtkWidget *refresh_button = gtk_button_new_with_label("Atualizar");
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), &app);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(vbox), app.drawing_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), refresh_button, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);

    gtk_widget_show_all(app.window);
    gtk_main();

    if (app.surface) {
        cairo_surface_destroy(app.surface);
    }

    return 0;
}
