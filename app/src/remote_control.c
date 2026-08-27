#include "remote_control.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* Porta fixa do kindow-helperd (pi/kindow-helperd) — par do PORT de lá. */
#define HELPERD_PORT "5910"
#define IO_TIMEOUT_SECONDS 3

/* Uma transação completa: conecta, manda `request` (com \n), lê uma linha de resposta em
 * `response`. false em qualquer falha de rede/timeout. */
static bool transact(const char *host, const char *request, char *response, size_t size) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *info = NULL;
    if (getaddrinfo(host, HELPERD_PORT, &hints, &info) != 0 || !info) {
        return false;
    }

    int fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(info);
        return false;
    }

    /* No Linux (único alvo — Kindle), SO_SNDTIMEO limita TAMBÉM o connect(), não só o
     * write — verificado empiricamente em review: connect() contra host buraco-negro
     * (SYN descartado sem resposta) retornou em ~3s com esse setup, não nos ~2min do
     * tcp_syn_retries. Ou seja, o pior caso de UI parada é IO_TIMEOUT_SECONDS, que é o
     * trade-off aceito documentado no .h. */
    struct timeval timeout = {.tv_sec = IO_TIMEOUT_SECONDS};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    bool ok = false;
    if (connect(fd, info->ai_addr, info->ai_addrlen) == 0) {
        size_t len = strlen(request);
        if (write(fd, request, len) == (ssize_t)len) {
            ssize_t got = read(fd, response, size - 1);
            if (got > 0) {
                response[got] = '\0';
                ok = true;
            }
        }
    }
    freeaddrinfo(info);
    close(fd);
    return ok;
}

bool remote_control_get(const char *host, RemoteZoom *out) {
    char response[64];
    if (!transact(host, "get\n", response, sizeof(response))) {
        fprintf(stderr, "kindow: helperd não respondeu ao get (rodando no Pi?)\n");
        return false;
    }
    RemoteZoom zoom;
    if (sscanf(response, "ok %d %d %d", &zoom.dpi, &zoom.deco, &zoom.panel) != 3) {
        fprintf(stderr, "kindow: resposta inesperada do helperd: %s", response);
        return false;
    }
    *out = zoom;
    return true;
}

bool remote_control_set(const char *host, const char *control, int value) {
    char request[32];
    snprintf(request, sizeof(request), "%s %d\n", control, value);
    char response[64];
    if (!transact(host, request, response, sizeof(response))) {
        fprintf(stderr, "kindow: helperd não respondeu ao %s (rodando no Pi?)\n", control);
        return false;
    }
    if (strncmp(response, "ok", 2) != 0) {
        fprintf(stderr, "kindow: helperd recusou %s %d: %s", control, value, response);
        return false;
    }
    return true;
}
