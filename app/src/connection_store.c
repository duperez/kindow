#include "connection_store.h"

#include <stdio.h>
#include <string.h>

void connection_store_load(ConnectionStore *store, const char *path) {
    store->count = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[192];
    while (store->count < CONNECTION_STORE_MAX && fgets(line, sizeof(line), f)) {
        char host[CONNECTION_STORE_HOST_LEN];
        char password[CONNECTION_STORE_PASSWORD_LEN];
        int port;
        /* "%63s"/"%31s" limitam a leitura ao tamanho dos buffers — nunca estouram mesmo
         * com uma linha corrompida/gigante. Senha é o terceiro token, opcional (2
         * tokens casados = entrada sem senha, formato antigo continua válido). */
        int matched = sscanf(line, "%63s %d %31s", host, &port, password);
        if (matched < 2) {
            continue; /* linha não bate o formato — ignora, não é erro fatal */
        }
        SavedConnection *entry = &store->items[store->count++];
        snprintf(entry->host, sizeof(entry->host), "%s", host);
        entry->port = port;
        if (matched == 3) {
            snprintf(entry->password, sizeof(entry->password), "%s", password);
        } else {
            entry->password[0] = '\0';
        }
    }
    fclose(f);
}

bool connection_store_save(const ConnectionStore *store, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }
    for (int i = 0; i < store->count; i++) {
        if (store->items[i].password[0]) {
            fprintf(f, "%s %d %s\n", store->items[i].host, store->items[i].port,
                    store->items[i].password);
        } else {
            fprintf(f, "%s %d\n", store->items[i].host, store->items[i].port);
        }
    }
    fclose(f);
    return true;
}

void connection_store_touch(ConnectionStore *store, const char *host, int port,
                            const char *password) {
    SavedConnection entry;
    int existing = -1;
    for (int i = 0; i < store->count; i++) {
        if (store->items[i].port == port && strcmp(store->items[i].host, host) == 0) {
            existing = i;
            break;
        }
    }

    if (existing >= 0) {
        entry = store->items[existing];
        for (int i = existing; i < store->count - 1; i++) {
            store->items[i] = store->items[i + 1];
        }
        store->count--;
    } else {
        snprintf(entry.host, sizeof(entry.host), "%s", host);
        entry.port = port;
    }
    /* Senha sempre atualizada (entrada nova OU re-touch): a mais recente que o usuário
     * usou pra conectar com sucesso vence — ver contrato no .h. */
    snprintf(entry.password, sizeof(entry.password), "%s", password ? password : "");

    if (store->count >= CONNECTION_STORE_MAX) {
        store->count = CONNECTION_STORE_MAX - 1; /* descarta o mais antigo pra abrir vaga */
    }
    for (int i = store->count; i > 0; i--) {
        store->items[i] = store->items[i - 1];
    }
    store->items[0] = entry;
    store->count++;
}
