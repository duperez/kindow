#include "session.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "vnc_client.h"

/* PoC: conexão VNC persistente pelo tempo de vida do app (revisão da decisão original de
 * reconectar por interação — motivada pelo custo real medido em
 * docs/findings/kindle-hardware-test.md: cada conexão nova paga handshake + o burst vazio
 * do TigerVNC, 1-2s). O modelo agora é push: depois do primeiro pedido explícito
 * (vnc_client_start_updates), a própria libvncclient mantém sozinha um pedido incremental
 * sempre em andamento — a tela atualiza sozinha quando o Pi muda, sem botão nem toque
 * disparando busca nenhuma. Um clique só manda o PointerEvent; a mudança resultante (se
 * houver) chega pelo pedido incremental que já está em andamento. */
struct Session {
    char *host;
    int port;
    /* Tamanho real da tela local (vem do chamador, que é quem conhece o display) — alvo do
     * pedido de resize remoto. */
    int target_width;
    int target_height;
    SessionCallbacks callbacks;

    VncClient *client;
    /* Tamanho do último frame recebido; 0x0 enquanto nenhum frame chegou nesta conexão.
     * Também é o limite de validade pra coordenadas de clique. */
    int frame_width;
    int frame_height;
    /* Evita pedir redimensionamento de novo a cada frame da mesma conexão — um pedido já
     * basta, e insistir seria redundante (ver comentário em vnc_client_request_desktop_size).
     * Reseta a cada nova conexão (try_connect). */
    bool resize_requested;

    /* Sources do GLib atualmente ativas (0 = nenhuma). No máximo uma de cada existe por
     * vez: ou a sessão está conectada (watch no fd) ou está re-tentando (timer de 2s). */
    guint watch_id;
    guint retry_id;
};

/* Toda mensagem de erro da API do vnc_client vem malloc'ada pro chamador liberar — este
 * helper fecha o par imprimir+liberar num lugar só. Aceita NULL. */
static void report_error_and_free(char *error) {
    g_printerr("kindow: %s\n", error ? error : "erro desconhecido");
    free(error);
}

static gboolean on_reconnect_timeout(gpointer user_data);
static gboolean on_socket_ready(GIOChannel *source, GIOCondition condition, gpointer user_data);

static void schedule_retry(Session *session) {
    session->retry_id = g_timeout_add_seconds(2, on_reconnect_timeout, session);
}

/* Derruba a conexão atual e agenda a re-tentativa. Só é chamada de dentro do watch do fd —
 * o próprio watch se remove retornando FALSE, por isso aqui só zera o id, sem
 * g_source_remove (remover duas vezes geraria warning do GLib). */
static void handle_connection_lost(Session *session) {
    session->watch_id = 0;
    if (session->client) {
        vnc_client_disconnect(session->client);
        session->client = NULL;
    }
    session->frame_width = 0;
    session->frame_height = 0;
    schedule_retry(session);
}

/* Registra o fd do cliente no loop do GLib. g_unix_fd_add não existe nesse glib do sysroot
 * (só a partir da 2.36) — GIOChannel/g_io_add_watch é a API disponível aqui. O canal não
 * deve fechar o fd sozinho (close_on_unref=FALSE): quem é dono do fd é o VncClient, fechado
 * via vnc_client_disconnect -> rfbClientCleanup. */
static void watch_client_fd(Session *session) {
    GIOChannel *channel = g_io_channel_unix_new(vnc_client_get_fd(session->client));
    g_io_channel_set_close_on_unref(channel, FALSE);
    session->watch_id =
        g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, on_socket_ready, session);
    g_io_channel_unref(channel);
}

static gboolean try_connect(Session *session) {
    char *error = NULL;
    session->resize_requested = false;
    session->client = vnc_client_connect(session->host, session->port, &error);
    if (!session->client) {
        report_error_and_free(error);
        return FALSE;
    }

    if (!vnc_client_start_updates(session->client, &error)) {
        report_error_and_free(error);
        vnc_client_disconnect(session->client);
        session->client = NULL;
        return FALSE;
    }

    watch_client_fd(session);
    return TRUE;
}

static gboolean on_reconnect_timeout(gpointer user_data) {
    Session *session = user_data;
    if (try_connect(session)) {
        session->retry_id = 0;
        return FALSE; /* conectou: remove esse timer (equivalente a G_SOURCE_REMOVE) */
    }
    return TRUE; /* continua tentando (equivalente a G_SOURCE_CONTINUE) */
}

/* Frame completo entregue pelo vnc_client: repassa pra quem consome (UI) e aplica a
 * política de resize — se o frame ainda não bate com a tela local, pede pro servidor
 * redimensionar (uma vez só por conexão). Se der certo, o próximo frame já vem do tamanho
 * certo e a condição deixa de ser verdadeira, sem precisar insistir. */
static void on_client_frame(int width, int height, const uint32_t *argb32_pixels,
                             void *user_data) {
    Session *session = user_data;
    session->frame_width = width;
    session->frame_height = height;

    /* Resize antes de repassar o frame: o write no socket é bloqueante, e rodar ele depois
     * do repasse contaminaria a métrica "fila até o redraw" da UI (que começa a contar no
     * queue_draw interno do on_frame) com tempo de rede — achado de review. */
    if (!session->resize_requested &&
        (width != session->target_width || height != session->target_height)) {
        session->resize_requested = true;
        char *resize_error = NULL;
        if (!vnc_client_request_desktop_size(session->client, session->target_width,
                                              session->target_height, &resize_error)) {
            report_error_and_free(resize_error);
        }
    }

    session->callbacks.on_frame(width, height, argb32_pixels, session->callbacks.user_data);
}

static gboolean on_socket_ready(GIOChannel *source, GIOCondition condition, gpointer user_data) {
    (void)source;
    Session *session = user_data;
    if (!session->client) {
        session->watch_id = 0;
        return FALSE; /* watch órfão de uma conexão já derrubada */
    }

    if (condition & (G_IO_HUP | G_IO_ERR)) {
        g_printerr("kindow: conexão perdida com o servidor VNC\n");
        handle_connection_lost(session);
        return FALSE;
    }

    char *error = NULL;
    vnc_client_handle_messages(session->client, on_client_frame, session, &error);
    if (error) {
        report_error_and_free(error);
        handle_connection_lost(session);
        return FALSE;
    }
    return TRUE;
}

Session *session_start(const char *host, int port, int target_width, int target_height,
                       SessionCallbacks callbacks) {
    Session *session = calloc(1, sizeof(Session));
    if (!session) {
        return NULL;
    }
    session->host = strdup(host);
    if (!session->host) {
        free(session);
        return NULL;
    }
    session->port = port;
    session->target_width = target_width;
    session->target_height = target_height;
    session->callbacks = callbacks;

    if (!try_connect(session)) {
        schedule_retry(session);
    }
    return session;
}

void session_send_click(Session *session, int x, int y) {
    if (!session->client || session->frame_width <= 0) {
        return;
    }

    /* A área visível local tem tamanho fixo; se o frame recebido for menor, um clique fora
     * dele ainda dispararia coordenadas inválidas pro servidor sem esse clamp. */
    if (x < 0 || y < 0 || x >= session->frame_width || y >= session->frame_height) {
        return;
    }

    /* clique esquerdo: pressiona e solta na mesma posição. Não busca atualização de volta
     * explicitamente — o pedido incremental já está em andamento (vnc_client_start_updates)
     * e vai capturar sozinho qualquer mudança de tela que esse clique causar no Pi. */
    vnc_client_send_pointer(session->client, x, y, 1);
    vnc_client_send_pointer(session->client, x, y, 0);
}

void session_log_status(const Session *session) {
    g_printerr("kindow: status — conectado=%s frame=%dx%d\n",
               session->client ? "sim" : "não", session->frame_width, session->frame_height);
}

void session_shutdown(Session *session) {
    if (!session) {
        return;
    }
    if (session->watch_id) {
        g_source_remove(session->watch_id);
    }
    if (session->retry_id) {
        g_source_remove(session->retry_id);
    }
    if (session->client) {
        vnc_client_disconnect(session->client);
    }
    free(session->host);
    free(session);
}
