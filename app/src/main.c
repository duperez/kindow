#include <glib-unix.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vnc_client.h"

/* PoC: conexão VNC persistente pelo tempo de vida do app (revisão da decisão original de
 * reconectar por interação — motivada pelo custo real medido em
 * docs/findings/kindle-hardware-test.md: cada conexão nova paga handshake + o burst vazio
 * do TigerVNC, 1-2s). O modelo agora é push: depois do primeiro pedido explícito
 * (vnc_client_start_updates), a própria libvncclient mantém sozinha um pedido incremental
 * sempre em andamento — a tela atualiza sozinha quando o Pi muda, sem botão nem toque
 * disparando busca nenhuma. Tocar na imagem só manda o PointerEvent; a mudança resultante
 * (se houver) chega pelo pedido incremental que já está em andamento. */
typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    int frame_width;
    int frame_height;
    const char *host;
    int port;
    VncClient *client;
    /* timestamp de quando o último gtk_widget_queue_draw foi pedido — usado só pra medir
     * quanto tempo até o expose de verdade rodar (ver on_expose). Zerado no início; a
     * primeira chamada de on_expose (antes de qualquer frame chegar) ignora essa medição. */
    struct timespec paint_requested_at;
    /* Tamanho real da tela do Kindle, detectado via gdk_screen_width/height em vez de
     * hardcoded — assim o mesmo binário serve qualquer modelo de Kindle. */
    int screen_width;
    int screen_height;
    /* Evita pedir redimensionamento de novo a cada frame da mesma conexão — um pedido já
     * basta, e insistir seria redundante (ver comentário em vnc_client_request_desktop_size).
     * Reseta a cada nova conexão (try_connect). */
    bool resize_requested;
} AppState;

static void show_error(const char *message) {
    g_printerr("kindow: %s\n", message ? message : "erro desconhecido");
}

/* Mantém o Kindle acordado só enquanto este app está na tela — mesmo padrão (e mesma
 * justificativa) do pet_dashboard no projeto irmão `kindle`, ver
 * ../../kindle/docs/findings/screensaver-app-lifecycle.md: liga no início, desliga em todo
 * caminho de saída alcançável (destroy da janela, SIGTERM do kill de deploy, SIGINT).
 * SIGKILL é o único caminho que isso não pega — a propriedade ficaria travada em 1 até
 * reboot ou reversão manual. */
static void set_prevent_screensaver(int value) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "lipc-set-prop -i com.lab126.powerd preventScreenSaver %d", value);
    int rc = system(cmd);
    if (rc != 0) {
        g_printerr("kindow: lipc-set-prop preventScreenSaver=%d falhou (rc=%d)\n", value, rc);
    }
}

static gboolean on_quit_signal(gpointer user_data) {
    (void)user_data;
    gtk_main_quit(); /* a limpeza (preventScreenSaver=0 incluso) roda depois do gtk_main */
    return FALSE;
}

static void on_frame_ready(int width, int height, const uint32_t *argb32_pixels,
                            void *user_data) {
    AppState *app = user_data;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

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

    clock_gettime(CLOCK_MONOTONIC, &t1);
    long copy_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    g_printerr("kindow: cópia pro cairo_surface (%dx%d) levou %ld ms\n", width, height, copy_ms);

    app->frame_width = width;
    app->frame_height = height;

    /* Se o frame que chegou ainda não bate com a tela real do Kindle, pede pro servidor
     * redimensionar (uma vez só por conexão — ver vnc_client_request_desktop_size). Se der
     * certo, o próximo frame já vem do tamanho certo e essa condição deixa de ser verdadeira,
     * sem precisar insistir. */
    if (!app->resize_requested && (width != app->screen_width || height != app->screen_height)) {
        app->resize_requested = true;
        char *resize_error = NULL;
        if (!vnc_client_request_desktop_size(app->client, app->screen_width, app->screen_height,
                                              &resize_error)) {
            show_error(resize_error);
            free(resize_error);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &app->paint_requested_at);
    gtk_widget_queue_draw(app->drawing_area);
}

static gboolean on_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data) {
    (void)event;
    AppState *app = user_data;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    cairo_t *cr = gdk_cairo_create(widget->window);

    if (app->surface) {
        cairo_set_source_surface(cr, app->surface, 0, 0);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
    }

    cairo_destroy(cr);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (app->surface) {
        /* só loga a fila de espera quando existe um frame de verdade sendo pintado —
         * o primeiro expose (tela em branco, antes de qualquer frame chegar) não tem um
         * paint_requested_at válido pra comparar. */
        long wait_ms = (t0.tv_sec - app->paint_requested_at.tv_sec) * 1000 +
                       (t0.tv_nsec - app->paint_requested_at.tv_nsec) / 1000000;
        long paint_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
        g_printerr("kindow: fila até o redraw %ld ms, cairo_paint %ld ms\n", wait_ms, paint_ms);
    }
    return TRUE;
}

static gboolean on_drawing_area_click(GtkWidget *widget, GdkEventButton *event,
                                       gpointer user_data) {
    (void)widget;
    AppState *app = user_data;
    if (!app->client || app->frame_width <= 0) {
        return TRUE;
    }

    /* A área de desenho tem tamanho fixo; se o frame recebido for menor, um clique fora
     * dele ainda dispararia coordenadas inválidas pro servidor sem esse clamp. */
    int x = (int)event->x;
    int y = (int)event->y;
    if (x < 0 || y < 0 || x >= app->frame_width || y >= app->frame_height) {
        return TRUE;
    }

    /* clique esquerdo: pressiona e solta na mesma posição. Não busca atualização de volta
     * explicitamente — o pedido incremental já está em andamento (vnc_client_start_updates)
     * e vai capturar sozinho qualquer mudança de tela que esse clique causar no Pi. */
    vnc_client_send_pointer(app->client, x, y, 1);
    vnc_client_send_pointer(app->client, x, y, 0);
    return TRUE;
}

static gboolean on_reconnect_timeout(gpointer user_data);
static gboolean on_socket_ready(GIOChannel *source, GIOCondition condition, gpointer user_data);

static void handle_connection_lost(AppState *app) {
    if (app->client) {
        vnc_client_disconnect(app->client);
        app->client = NULL;
    }
    app->frame_width = 0;
    app->frame_height = 0;
    /* timer de retry até reconectar — cobre WiFi caindo ou o Pi reiniciando */
    g_timeout_add_seconds(2, on_reconnect_timeout, app);
}

/* Registra o fd do cliente no loop do GTK. g_unix_fd_add não existe nesse glib do sysroot
 * (só a partir da 2.36) — GIOChannel/g_io_add_watch é a API disponível aqui. O canal não
 * deve fechar o fd sozinho (close_on_unref=FALSE): quem é dono do fd é o VncClient, fechado
 * via vnc_client_disconnect -> rfbClientCleanup. */
static void watch_client_fd(AppState *app) {
    GIOChannel *channel = g_io_channel_unix_new(vnc_client_get_fd(app->client));
    g_io_channel_set_close_on_unref(channel, FALSE);
    g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, on_socket_ready, app);
    g_io_channel_unref(channel);
}

static gboolean try_connect(AppState *app) {
    char *error = NULL;
    app->resize_requested = false;
    app->client = vnc_client_connect(app->host, app->port, &error);
    if (!app->client) {
        show_error(error);
        free(error);
        return FALSE;
    }

    if (!vnc_client_start_updates(app->client, &error)) {
        show_error(error);
        free(error);
        vnc_client_disconnect(app->client);
        app->client = NULL;
        return FALSE;
    }

    watch_client_fd(app);
    return TRUE;
}

static gboolean on_reconnect_timeout(gpointer user_data) {
    AppState *app = user_data;
    if (try_connect(app)) {
        return FALSE; /* conectou: remove esse timer (equivalente a G_SOURCE_REMOVE) */
    }
    return TRUE; /* continua tentando (equivalente a G_SOURCE_CONTINUE) */
}

static gboolean on_socket_ready(GIOChannel *source, GIOCondition condition, gpointer user_data) {
    (void)source;
    AppState *app = user_data;
    if (!app->client) {
        return FALSE; /* watch órfão de uma conexão já derrubada */
    }

    if (condition & (G_IO_HUP | G_IO_ERR)) {
        show_error("conexão perdida com o servidor VNC");
        handle_connection_lost(app);
        return FALSE;
    }

    char *error = NULL;
    vnc_client_handle_messages(app->client, on_frame_ready, app, &error);
    if (error) {
        show_error(error);
        free(error);
        handle_connection_lost(app);
        return FALSE;
    }
    return TRUE;
}

/* Gatilho de debug: `kill -HUP <pid>` imprime o estado atual da conexão no log — útil pra
 * confirmar via SSH que o app está vivo e conectado sem precisar tocar a tela física. Não
 * força uma atualização: vnc_client_start_updates só pode ser chamado uma vez por conexão
 * (contrato documentado em vnc_client.h) — chamar de novo aqui deixaria dois pedidos de
 * atualização pendentes ao mesmo tempo no servidor. O motor de push (achado #1 em
 * kindle-hardware-test.md) já cobre qualquer atualização real sozinho, sem precisar de
 * gatilho manual nenhum. SIGHUP em vez de SIGUSR1 porque o glib desse sysroot só aceita
 * SIGHUP/SIGINT/SIGTERM em g_unix_signal_add_watch_full. */
static gboolean on_refresh_signal(gpointer user_data) {
    AppState *app = user_data;
    g_printerr("kindow: status — conectado=%s frame=%dx%d\n",
               app->client ? "sim" : "não", app->frame_width, app->frame_height);
    return TRUE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    AppState app = {0};
    app.host = argc > 1 ? argv[1] : "192.168.0.155";
    app.port = argc > 2 ? atoi(argv[2]) : 5901;

    g_unix_signal_add_watch_full(SIGHUP, G_PRIORITY_DEFAULT, on_refresh_signal, &app, NULL);
    /* SIGTERM é o que o `kill` do fluxo de deploy manda — sem tratar, o processo morre sem
     * passar pela limpeza pós-gtk_main e deixaria preventScreenSaver preso em 1. */
    g_unix_signal_add_watch_full(SIGTERM, G_PRIORITY_DEFAULT, on_quit_signal, NULL, NULL);
    g_unix_signal_add_watch_full(SIGINT, G_PRIORITY_DEFAULT, on_quit_signal, NULL, NULL);

    set_prevent_screensaver(1);

    /* Tamanho real da tela detectado em runtime, não hardcoded — o mesmo binário serve
     * qualquer modelo de Kindle que conectar (resolução medida via xwininfo pra este device
     * era 1072x1448, mas isso não deve ser assumido pra outros). */
    app.screen_width = gdk_screen_width();
    app.screen_height = gdk_screen_height();

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    /* O window manager do Kindle (Awesome WM) só mapeia/exibe em tela cheia janelas cujo
     * título segue esse esquema key-value (L:layer, N:role, ID:reverse-domain, PC:N esconde
     * a barra de status do Kindle) — descoberto no projeto irmão `kindle`, replicado aqui do
     * mesmo jeito que o pet_dashboard já faz. Sem isso a janela fica como stub 10x10 nunca
     * mapeado, mesmo com o processo rodando normalmente. */
    gtk_window_set_title(GTK_WINDOW(app.window),
                          "L:A_N:application_ID:com.eduardo.kindowclient_PC:N");
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    app.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.drawing_area, app.screen_width, app.screen_height);
    gtk_widget_add_events(app.drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(app.drawing_area, "expose-event", G_CALLBACK(on_expose), &app);
    g_signal_connect(app.drawing_area, "button-press-event", G_CALLBACK(on_drawing_area_click),
                      &app);
    gtk_container_add(GTK_CONTAINER(app.window), app.drawing_area);

    gtk_widget_show_all(app.window);

    if (!try_connect(&app)) {
        g_timeout_add_seconds(2, on_reconnect_timeout, &app);
    }

    gtk_main();

    set_prevent_screensaver(0);
    if (app.client) {
        vnc_client_disconnect(app.client);
    }
    if (app.surface) {
        cairo_surface_destroy(app.surface);
    }

    return 0;
}
