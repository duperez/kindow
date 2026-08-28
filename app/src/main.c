#include <glib-unix.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

#include "connection_store.h"
#include "kindle_platform.h"
#include "remote_control.h"
#include "session.h"
#include "strings.h"
#include "ui.h"

/*
 * Wiring do app — só instancia os módulos e liga um no outro:
 *
 *   ui  --clique-->  session  --PointerEvent-->  vnc_client (--> Pi)
 *   ui  <--frame--   session  <--FramebufferUpdate--  vnc_client (<-- Pi)
 *
 * Nenhuma lógica mora aqui: política de conexão/reconexão/resize é do session.c, desenho e
 * toque são do ui.c, e as particularidades do device (screensaver, título mágico de janela)
 * do kindle_platform.c.
 */
typedef struct {
    Ui *ui;
    Session *session;
    /* Host/porta do Pi CONECTADO agora (ou tentando) — diferente de antes (26/08), não
     * é mais fixo desde o boot: muda toda vez que o usuário escolhe/digita outro Pi na
     * tela de conexão (item 5, 27/08, ver docs/ideias-futuras.md). */
    char host[CONNECTION_STORE_HOST_LEN];
    int port;
    /* true entre pedir uma conexão nova e o primeiro frame chegar — é o sinal que
     * on_session_frame usa pra saber que essa é a hora de sair de SCREEN_CONNECTING e
     * persistir no histórico (ver on_session_frame). Reconexões automáticas depois de
     * uma queda de rede NÃO passam por aqui de novo — só uma conexão pedida pelo
     * usuário conta como "nova". */
    bool connecting_first_frame;
    /* Senha da conexão pedida agora (item 10) — vai pro histórico junto com host/porta
     * quando o primeiro frame confirmar que ela funcionou. */
    char password[CONNECTION_STORE_PASSWORD_LEN];
    ConnectionStore store;
    char store_path[256];
    /* Valores de zoom remotos conhecidos; dpi==0 = ainda não consultados (busca os três
     * no helperd no primeiro zoom, pra ancorar os A+/A- nos valores reais persistidos no
     * Pi em vez de assumir). Zerado a cada troca de Pi — valores de um Pi não servem
     * pra outro. */
    RemoteZoom remote;
} App;

/* Passo e limites de cada controle de zoom (espelham as faixas do kindow-helperd). */
typedef struct {
    const char *control; /* nome no protocolo do helperd */
    int step, min, max;
} ZoomSpec;

static const ZoomSpec kZoomApps = {"dpi", 24, 96, 384};
static const ZoomSpec kZoomDeco = {"deco", 2, 8, 40};
static const ZoomSpec kZoomPanel = {"panel", 2, 7, 32};

/* Única "lógica" tolerada aqui no wiring: escolher o próximo valor e repassar. Cresceu
 * além disso (mais ações, estado, feedback visual), extrair um módulo próprio. */
static void handle_zoom(App *app, const ZoomSpec *spec, int *value, int direction) {
    if (app->remote.dpi == 0 && !remote_control_get(app->host, &app->remote)) {
        return; /* helperd fora do ar — erro já logado */
    }
    int next = *value + direction * spec->step;
    if (next < spec->min || next > spec->max) {
        g_printerr("kindow: zoom de %s já está no limite (%d)\n", spec->control, *value);
        return;
    }
    if (remote_control_set(app->host, spec->control, next)) {
        *value = next;
        g_printerr("kindow: %s remoto agora é %d\n", spec->control, next);
    }
}

/* Linhas de scroll — etapa 4 da reestrutura (27/08): puramente client-side (diferente do
 * zoom, não passa pelo kindow-helperd/Pi), mesmo formato de par -/+ do menu. */
#define SCROLL_LINES_MIN 1
#define SCROLL_LINES_MAX 10
#define SCROLL_LINES_STEP 1

static void handle_scroll_lines(App *app, int direction) {
    int current = session_get_scroll_lines(app->session);
    int next = current + direction * SCROLL_LINES_STEP;
    if (next < SCROLL_LINES_MIN || next > SCROLL_LINES_MAX) {
        g_printerr("kindow: linhas de scroll já está no limite (%d)\n", current);
        return;
    }
    session_set_scroll_lines(app->session, next);
    g_printerr("kindow: linhas de scroll agora é %d\n", next);
}

/* Monta a lista de exibição a partir do histórico persistido e manda pra UI — chamada
 * toda vez que a tela de conexão precisa reaparecer (boot, "Voltar", "Desconectar").
 * O histórico já nasce ordenado por uso mais recente primeiro (connection_store.h), daí
 * index 0 ser sempre o destaque inicial. */
static void show_connect_list(App *app) {
    UiConnectionEntry entries[CONNECTION_STORE_MAX];
    for (int i = 0; i < app->store.count; i++) {
        entries[i].host = app->store.items[i].host;
        entries[i].port = app->store.items[i].port;
        entries[i].password = app->store.items[i].password;
    }
    ui_show_connect_list(app->ui, entries, app->store.count, 0);
}

static void on_session_frame(int width, int height, const uint32_t *argb32_pixels,
                              void *user_data) {
    App *app = user_data;
    if (app->connecting_first_frame) {
        /* primeiro frame de verdade depois de um pedido de conexão do usuário — é o
         * único sinal confiável de que o servidor respondeu (TCP+handshake sozinhos não
         * bastam, ver ui_show_session em ui.h). Só agora vale persistir no histórico. */
        app->connecting_first_frame = false;
        connection_store_touch(&app->store, app->host, app->port, app->password);
        if (!connection_store_save(&app->store, app->store_path)) {
            g_printerr("kindow: não consegui salvar o histórico de conexões em %s\n",
                       app->store_path);
        }
        ui_show_session(app->ui);
    }
    ui_show_frame(app->ui, width, height, argb32_pixels);
}

static void on_ui_click(int x, int y, void *user_data) {
    App *app = user_data;
    session_send_click(app->session, x, y);
}

static void on_ui_key(uint32_t keysym, bool down, void *user_data) {
    App *app = user_data;
    session_send_key(app->session, keysym, down);
}

static void on_ui_action(MenuAction action, void *user_data) {
    App *app = user_data;
    switch (action) {
    case MENU_ACTION_QUIT:
        gtk_main_quit(); /* limpeza (keep_awake(false) incluso) roda depois do gtk_main */
        break;
    case MENU_ACTION_STATUS:
        session_log_status(app->session);
        break;
    case MENU_ACTION_ZOOM_APPS_IN:
        handle_zoom(app, &kZoomApps, &app->remote.dpi, +1);
        break;
    case MENU_ACTION_ZOOM_APPS_OUT:
        handle_zoom(app, &kZoomApps, &app->remote.dpi, -1);
        break;
    case MENU_ACTION_ZOOM_DECO_IN:
        handle_zoom(app, &kZoomDeco, &app->remote.deco, +1);
        break;
    case MENU_ACTION_ZOOM_DECO_OUT:
        handle_zoom(app, &kZoomDeco, &app->remote.deco, -1);
        break;
    case MENU_ACTION_ZOOM_PANEL_IN:
        handle_zoom(app, &kZoomPanel, &app->remote.panel, +1);
        break;
    case MENU_ACTION_ZOOM_PANEL_OUT:
        handle_zoom(app, &kZoomPanel, &app->remote.panel, -1);
        break;
    case MENU_ACTION_SCROLL_LINES_IN:
        handle_scroll_lines(app, +1);
        break;
    case MENU_ACTION_SCROLL_LINES_OUT:
        handle_scroll_lines(app, -1);
        break;
    case MENU_ACTION_DISCONNECT:
        session_disconnect(app->session);
        show_connect_list(app);
        break;
    case MENU_ACTION_NONE:
        break;
    }
}

static void on_ui_bar(BarButton button, void *user_data) {
    App *app = user_data;
    switch (button) {
    case BAR_SCROLL_UP:
        session_send_scroll(app->session, true);
        break;
    case BAR_SCROLL_DOWN:
        session_send_scroll(app->session, false);
        break;
    case BAR_TOGGLE_KEYBOARD:
    case BAR_TOGGLE_MENU:
        /* toggle_panel (ui.c) já decidiu o novo modo e, se preciso, chamou on_resize —
         * nada a fazer aqui além do que on_resize já cobre. */
        break;
    }
}

static void on_ui_resize(int width, int height, void *user_data) {
    App *app = user_data;
    session_set_target_size(app->session, width, height);
}

/* Etapa 3 da reestrutura (27/08): arrasto vindo do clique esquerdo sticky (tecla
 * "Esquerdo", página de símbolos do teclado) — held=true no press inicial e em cada
 * posição intermediária, held=false quando o dedo levanta. session_send_drag decide
 * sozinho o que mandar pro protocolo (ver session.h); esta função só repassa. */
static void on_ui_drag(int x, int y, bool held, void *user_data) {
    App *app = user_data;
    session_send_drag(app->session, x, y, held);
}

static void on_ui_right_click(void *user_data) {
    App *app = user_data;
    session_send_right_click(app->session);
}

/* Quantas falhas de tentativa SEGUIDAS antes de trocar o "Conectando..." pelo aviso de
 * erro (item 9). 2 dá uma chance a uma falha transitória (WiFi piscou) sem deixar o
 * usuário olhando uma tela parada por muito tempo — com o retry de 2s do session.c, o
 * aviso aparece ~2-4s depois do toque. A sessão continua tentando mesmo depois do
 * aviso; se vingar, a tela normal aparece sozinha. */
#define CONNECT_FAILURES_BEFORE_ERROR 2

static void on_session_attempt_failed(int consecutive_failures, const char *error,
                                      void *user_data) {
    App *app = user_data;
    if (!app->connecting_first_frame) {
        return; /* queda de sessão já estabelecida: o retry silencioso de sempre — a
                 * tela continua mostrando o último frame, sem tela de erro por cima */
    }
    if (consecutive_failures >= CONNECT_FAILURES_BEFORE_ERROR) {
        ui_show_connect_error(app->ui, error);
    }
}

/* Tela de conexão (item 5, 27/08): disparado ao tocar uma linha da lista ou confirmar o
 * formulário "+" — nos dois casos é um pedido de conexão a um Pi ainda não confirmado
 * (só vira sessão de fato quando on_session_frame vir o primeiro frame). */
static void on_ui_connect_request(const char *host, int port, const char *password,
                                  void *user_data) {
    App *app = user_data;
    snprintf(app->host, sizeof(app->host), "%s", host);
    app->port = port;
    snprintf(app->password, sizeof(app->password), "%s", password ? password : "");
    app->remote = (RemoteZoom){0}; /* valores de zoom são por-Pi, o cache antigo não serve */
    app->connecting_first_frame = true;
    /* ui_show_connecting ANTES de session_connect: a primeira tentativa (e a primeira
     * falha, se houver) acontece SÍNCRONA dentro de session_connect — a tela de
     * conectando precisa já estar armada pra ui_show_connect_error dela não ser
     * descartado (ver o guard `if (!ui->connecting)` em ui.c). */
    ui_show_connecting(app->ui, host, port);
    session_connect(app->session, host, port, password);
}

static void on_ui_back(void *user_data) {
    App *app = user_data;
    session_disconnect(app->session);
    app->connecting_first_frame = false;
    show_connect_list(app);
}

/* Gatilho de debug: `kill -HUP <pid>` imprime o estado atual da conexão no log — útil pra
 * confirmar via SSH que o app está vivo e conectado sem precisar tocar a tela física.
 * SIGHUP em vez de SIGUSR1 porque o glib desse sysroot só aceita SIGHUP/SIGINT/SIGTERM em
 * g_unix_signal_add_watch_full. */
static gboolean on_status_signal(gpointer user_data) {
    App *app = user_data;
    session_log_status(app->session);
    return TRUE;
}

static gboolean on_quit_signal(gpointer user_data) {
    (void)user_data;
    gtk_main_quit(); /* a limpeza (keep_awake(false) incluso) roda depois do gtk_main */
    return FALSE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    /* Língua da UI: decidida uma vez no boot a partir do idioma configurado no Kindle
     * (inglês é o padrão; KINDOW_LANG=pt|en no ambiente força, pra teste) — antes de
     * qualquer ui_create/tr(). */
    strings_set_language(kindle_platform_language_is_portuguese() ? LANG_PT : LANG_EN);

    App app = {0};
    snprintf(app.store_path, sizeof(app.store_path), "%s/connections.txt",
             kindle_platform_data_dir());
    connection_store_load(&app.store, app.store_path);

    g_unix_signal_add_watch_full(SIGHUP, G_PRIORITY_DEFAULT, on_status_signal, &app, NULL);
    /* SIGTERM é o que o `kill` do fluxo de deploy manda — sem tratar, o processo morre sem
     * passar pela limpeza pós-gtk_main e deixaria o screensaver preso desligado. */
    g_unix_signal_add_watch_full(SIGTERM, G_PRIORITY_DEFAULT, on_quit_signal, NULL, NULL);
    g_unix_signal_add_watch_full(SIGINT, G_PRIORITY_DEFAULT, on_quit_signal, NULL, NULL);

    kindle_platform_keep_awake(true);

    app.ui = ui_create(kindle_platform_window_title(), on_ui_click, on_ui_key, on_ui_action,
                        on_ui_bar, on_ui_resize, on_ui_drag, on_ui_right_click,
                        on_ui_connect_request, on_ui_back, &app);
    if (!app.ui) {
        g_printerr("kindow: sem memória pra criar a UI\n");
        kindle_platform_keep_awake(false);
        return 1;
    }
    /* O alvo do resize remoto é a ÁREA ÚTIL (tela menos a faixa do teclado) — o servidor
     * renderiza exatamente o espaço disponível, 1:1, sem escala nem corte. Sem host
     * nenhum ainda (ver tela de conexão abaixo): session_create só reserva o alvo, quem
     * decide A QUEM conectar agora é o usuário, tocando a lista ou o "+". */
    app.session = session_create(
        ui_frame_width(app.ui), ui_frame_height(app.ui),
        (SessionCallbacks){.on_frame = on_session_frame,
                           .on_connect_attempt_failed = on_session_attempt_failed,
                           .user_data = &app});
    if (!app.session) {
        /* só acontece por falta de memória */
        g_printerr("kindow: sem memória pra iniciar a sessão\n");
        kindle_platform_keep_awake(false);
        ui_destroy(app.ui);
        return 1;
    }

    if (argc > 1) {
        /* atalho de dev/teste (mesmo papel do argv de antes da tela de conexão existir):
         * pula a lista e conecta direto no host passado na linha de comando. */
        const char *host = argv[1];
        int port = argc > 2 ? atoi(argv[2]) : 5901;
        on_ui_connect_request(host, port, argc > 3 ? argv[3] : "", &app);
    } else {
        show_connect_list(&app);
    }

    gtk_main();

    kindle_platform_keep_awake(false);
    session_shutdown(app.session);
    ui_destroy(app.ui);

    return 0;
}
