/*
 * Teste unitário do histórico de conexões (connection_store.c) — mesmo espírito de
 * test_keyboard.c: sem GTK, sem libvncclient, assert() + exit(1) em caso de falha,
 * registrado como test() do Meson. Único I/O de verdade é o arquivo de load/save, feito
 * num path temporário criado por cada teste (nunca toca /mnt/us — isso é
 * responsabilidade do kindle_platform.c, não deste módulo).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "connection_store.h"

/* Cada teste usa um path próprio (PID + endereço de uma variável local, únicos o
 * bastante pra rodar em paralelo sem colisão) e limpa depois de si — não deixa lixo em
 * /tmp entre execuções. */
static void temp_path(char *buf, size_t buf_len) {
    int marker;
    snprintf(buf, buf_len, "/tmp/kindow-test-connection-store-%d-%p.txt", (int)getpid(),
             (void *)&marker);
}

static void test_touch_new_entry_goes_to_front(void) {
    ConnectionStore store = {0};
    connection_store_touch(&store, "192.168.0.10", 5901, NULL);
    assert(store.count == 1);
    assert(strcmp(store.items[0].host, "192.168.0.10") == 0);
    assert(store.items[0].port == 5901);

    connection_store_touch(&store, "192.168.0.20", 5902, NULL);
    assert(store.count == 2);
    assert(strcmp(store.items[0].host, "192.168.0.20") == 0); /* o mais recente vai pro topo */
    assert(strcmp(store.items[1].host, "192.168.0.10") == 0);
}

static void test_touch_existing_entry_moves_to_front_without_duplicating(void) {
    ConnectionStore store = {0};
    connection_store_touch(&store, "192.168.0.10", 5901, NULL);
    connection_store_touch(&store, "192.168.0.20", 5901, NULL);
    connection_store_touch(&store, "192.168.0.10", 5901, NULL); /* re-toca o primeiro */

    assert(store.count == 2); /* não duplicou */
    assert(strcmp(store.items[0].host, "192.168.0.10") == 0); /* voltou pro topo */
    assert(strcmp(store.items[1].host, "192.168.0.20") == 0);
}

static void test_touch_same_host_different_port_is_a_different_entry(void) {
    ConnectionStore store = {0};
    connection_store_touch(&store, "192.168.0.10", 5901, NULL);
    connection_store_touch(&store, "192.168.0.10", 5902, NULL);
    assert(store.count == 2); /* porta faz parte da identidade da entrada */
}

static void test_touch_evicts_oldest_when_full(void) {
    ConnectionStore store = {0};
    char host[32];
    for (int i = 0; i < CONNECTION_STORE_MAX; i++) {
        snprintf(host, sizeof(host), "10.0.0.%d", i);
        connection_store_touch(&store, host, 5901, NULL);
    }
    assert(store.count == CONNECTION_STORE_MAX);

    connection_store_touch(&store, "10.0.0.99", 5901, NULL); /* mais uma, deveria expulsar a mais antiga */
    assert(store.count == CONNECTION_STORE_MAX); /* nunca passa do teto */
    assert(strcmp(store.items[0].host, "10.0.0.99") == 0);
    for (int i = 0; i < store.count; i++) {
        assert(strcmp(store.items[i].host, "10.0.0.0") != 0); /* a mais antiga (índice 0
                                                                 * original) sumiu */
    }
}

static void test_save_then_load_round_trips_order(void) {
    char path[128];
    temp_path(path, sizeof(path));

    ConnectionStore store = {0};
    connection_store_touch(&store, "192.168.0.10", 5901, NULL);
    connection_store_touch(&store, "192.168.0.20", 5902, NULL);
    connection_store_touch(&store, "192.168.0.30", 5903, NULL);
    assert(connection_store_save(&store, path));

    ConnectionStore loaded;
    connection_store_load(&loaded, path);
    assert(loaded.count == 3);
    assert(strcmp(loaded.items[0].host, "192.168.0.30") == 0); /* MRU preservado */
    assert(loaded.items[0].port == 5903);
    assert(strcmp(loaded.items[1].host, "192.168.0.20") == 0);
    assert(strcmp(loaded.items[2].host, "192.168.0.10") == 0);

    unlink(path);
}

static void test_load_missing_file_yields_empty_store_not_error(void) {
    ConnectionStore store;
    store.count = 99; /* sentinela — load precisa zerar mesmo sem achar o arquivo */
    connection_store_load(&store, "/tmp/kindow-test-connection-store-does-not-exist.txt");
    assert(store.count == 0);
}

static void test_load_ignores_malformed_lines(void) {
    char path[128];
    temp_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "192.168.0.10 5901\n");
    fprintf(f, "linha sem porta nenhuma\n");
    fprintf(f, "\n");
    fprintf(f, "192.168.0.20 5902\n");
    fclose(f);

    ConnectionStore store;
    connection_store_load(&store, path);
    assert(store.count == 2); /* as duas linhas malformadas foram ignoradas, não travaram o load */
    assert(strcmp(store.items[0].host, "192.168.0.10") == 0);
    assert(strcmp(store.items[1].host, "192.168.0.20") == 0);

    unlink(path);
}

static void test_touch_host_at_max_length_boundary_is_preserved(void) {
    ConnectionStore store = {0};
    char host[CONNECTION_STORE_HOST_LEN];
    memset(host, 'a', sizeof(host) - 1); /* CONNECTION_STORE_HOST_LEN-1 chars, o maior host
                                           * que cabe inteiro sem truncar */
    host[sizeof(host) - 1] = '\0';

    connection_store_touch(&store, host, 5901, NULL);
    assert(store.count == 1);
    assert(strcmp(store.items[0].host, host) == 0); /* nada truncado */
}

static void test_touch_truncates_host_longer_than_max_length(void) {
    ConnectionStore store = {0};
    char long_host[CONNECTION_STORE_HOST_LEN + 32];
    memset(long_host, 'b', sizeof(long_host) - 1);
    long_host[sizeof(long_host) - 1] = '\0';

    connection_store_touch(&store, long_host, 5901, NULL); /* não pode estourar o buffer fixo */
    assert(store.count == 1);
    assert(strlen(store.items[0].host) == CONNECTION_STORE_HOST_LEN - 1); /* truncado, com espaço pro \0 */
    assert(strncmp(store.items[0].host, long_host, CONNECTION_STORE_HOST_LEN - 1) == 0);
}

static void test_touch_port_zero_and_negative_are_valid_identities(void) {
    ConnectionStore store = {0};
    connection_store_touch(&store, "192.168.0.10", 0, NULL);
    connection_store_touch(&store, "192.168.0.10", -1, NULL);
    /* porta faz parte da identidade da entrada, então 0 e -1 são entradas distintas de
     * qualquer outra porta — o módulo não valida faixa de porta, só usa como está */
    assert(store.count == 2);
    assert(store.items[0].port == -1);
    assert(store.items[1].port == 0);
}

static void test_save_then_load_round_trips_negative_port(void) {
    char path[128];
    temp_path(path, sizeof(path));

    ConnectionStore store = {0};
    connection_store_touch(&store, "192.168.0.10", -1, NULL);
    assert(connection_store_save(&store, path));

    ConnectionStore loaded;
    connection_store_load(&loaded, path);
    assert(loaded.count == 1);
    assert(loaded.items[0].port == -1); /* "%d" no save/load lida com sinal sem caso especial */

    unlink(path);
}

static void test_save_with_empty_store_produces_empty_file_that_loads_empty(void) {
    char path[128];
    temp_path(path, sizeof(path));

    ConnectionStore store = {0};
    assert(connection_store_save(&store, path)); /* count==0 ainda é um save válido, não erro */

    ConnectionStore loaded;
    loaded.count = 99; /* sentinela */
    connection_store_load(&loaded, path);
    assert(loaded.count == 0);

    unlink(path);
}

static void test_save_then_load_with_empty_host_does_not_round_trip(void) {
    /* Documenta um comportamento real do formato texto "host porta": um host vazio
     * produz a linha " 5901\n" (sem token antes da porta). No load, "%63s %d" lê "5901"
     * como se fosse o host e não sobra token pra porta, então sscanf retorna 1 (não 2) e
     * a linha é descartada como malformada — a entrada some silenciosamente. O módulo não
     * valida host vazio em connection_store_touch; quem tem que impedir isso é o
     * chamador (a tela de conexão), não este módulo. */
    char path[128];
    temp_path(path, sizeof(path));

    ConnectionStore store = {0};
    connection_store_touch(&store, "", 5901, NULL);
    assert(store.count == 1); /* touch aceita host vazio sem reclamar */
    assert(connection_store_save(&store, path));

    ConnectionStore loaded;
    connection_store_load(&loaded, path);
    assert(loaded.count == 0); /* mas o round-trip perde a entrada */

    unlink(path);
}

static void test_touch_re_touching_existing_entry_when_full_does_not_evict_others(void) {
    ConnectionStore store = {0};
    char host[32];
    for (int i = 0; i < CONNECTION_STORE_MAX; i++) {
        snprintf(host, sizeof(host), "10.0.0.%d", i);
        connection_store_touch(&store, host, 5901, NULL);
    }
    assert(store.count == CONNECTION_STORE_MAX);

    /* re-toca a entrada mais antiga (índice 0 original, "10.0.0.0") — como ela já existe,
     * só deveria mover pro topo, sem expulsar ninguém, já que o count efetivo nunca passa
     * do teto durante a operação. */
    connection_store_touch(&store, "10.0.0.0", 5901, NULL);
    assert(store.count == CONNECTION_STORE_MAX); /* ninguém foi perdido */
    assert(strcmp(store.items[0].host, "10.0.0.0") == 0); /* voltou pro topo */

    /* todas as outras CONNECTION_STORE_MAX-1 entradas originais continuam presentes */
    for (int i = 1; i < CONNECTION_STORE_MAX; i++) {
        snprintf(host, sizeof(host), "10.0.0.%d", i);
        bool found = false;
        for (int j = 0; j < store.count; j++) {
            if (strcmp(store.items[j].host, host) == 0) {
                found = true;
                break;
            }
        }
        assert(found);
    }
}

static void test_password_round_trips_and_null_means_empty(void) {
    char path[128];
    temp_path(path, sizeof(path));

    ConnectionStore store = {0};
    connection_store_touch(&store, "192.168.0.10", 5901, "segredo1");
    connection_store_touch(&store, "192.168.0.20", 5902, NULL); /* NULL == sem senha */
    connection_store_touch(&store, "192.168.0.30", 5903, "");   /* "" também */
    assert(strcmp(store.items[2].password, "segredo1") == 0);
    assert(store.items[1].password[0] == '\0');
    assert(store.items[0].password[0] == '\0');
    assert(connection_store_save(&store, path));

    ConnectionStore loaded;
    connection_store_load(&loaded, path);
    assert(loaded.count == 3);
    /* MRU: .30 primeiro, .10 (com senha) por último */
    assert(strcmp(loaded.items[2].host, "192.168.0.10") == 0);
    assert(strcmp(loaded.items[2].password, "segredo1") == 0);
    assert(loaded.items[0].password[0] == '\0'); /* linha de 2 tokens carrega sem senha */
    assert(loaded.items[1].password[0] == '\0');

    unlink(path);
}

static void test_re_touch_updates_stored_password(void) {
    ConnectionStore store = {0};
    connection_store_touch(&store, "192.168.0.10", 5901, "antiga");
    connection_store_touch(&store, "192.168.0.20", 5902, NULL);
    /* re-touch da mesma identidade (host+porta) com senha nova: atualiza, não duplica */
    connection_store_touch(&store, "192.168.0.10", 5901, "nova");
    assert(store.count == 2);
    assert(strcmp(store.items[0].host, "192.168.0.10") == 0);
    assert(strcmp(store.items[0].password, "nova") == 0);
    /* e re-touch com NULL limpa a senha (Pi deixou de exigir senha) */
    connection_store_touch(&store, "192.168.0.10", 5901, NULL);
    assert(store.items[0].password[0] == '\0');
}

static void test_load_old_format_without_password_still_works(void) {
    /* Arquivo salvo por uma versão anterior do app (antes da senha existir) precisa
     * continuar carregando — o terceiro token é opcional por contrato. */
    char path[128];
    temp_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "192.168.0.10 5901\n");
    fprintf(f, "192.168.0.20 5902 comsenha\n");
    fclose(f);

    ConnectionStore store;
    connection_store_load(&store, path);
    assert(store.count == 2);
    assert(store.items[0].password[0] == '\0');
    assert(strcmp(store.items[1].password, "comsenha") == 0);

    unlink(path);
}

static void test_touch_password_at_max_length_boundary_is_preserved(void) {
    ConnectionStore store = {0};
    char password[CONNECTION_STORE_PASSWORD_LEN];
    memset(password, 'p', sizeof(password) - 1); /* CONNECTION_STORE_PASSWORD_LEN-1 chars,
                                                    * a maior senha que cabe inteira */
    password[sizeof(password) - 1] = '\0';

    connection_store_touch(&store, "192.168.0.10", 5901, password);
    assert(store.count == 1);
    assert(strcmp(store.items[0].password, password) == 0); /* nada truncado */
}

static void test_touch_truncates_password_longer_than_max_length(void) {
    ConnectionStore store = {0};
    char long_password[CONNECTION_STORE_PASSWORD_LEN + 32];
    memset(long_password, 'q', sizeof(long_password) - 1);
    long_password[sizeof(long_password) - 1] = '\0';

    connection_store_touch(&store, "192.168.0.10", 5901, long_password); /* não pode estourar o buffer fixo */
    assert(store.count == 1);
    assert(strlen(store.items[0].password) == CONNECTION_STORE_PASSWORD_LEN - 1); /* truncado, com espaço pro \0 */
    assert(strncmp(store.items[0].password, long_password, CONNECTION_STORE_PASSWORD_LEN - 1) == 0);
}

static void test_load_line_with_extra_token_after_password_ignores_the_rest(void) {
    /* "%63s %d %31s" só lê 3 tokens — um quarto token na linha (lixo, campo futuro,
     * edição manual do arquivo) não quebra o parse nem vaza pro campo senha. */
    char path[128];
    temp_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "192.168.0.10 5901 segredo lixo_extra\n");
    fclose(f);

    ConnectionStore store;
    connection_store_load(&store, path);
    assert(store.count == 1);
    assert(strcmp(store.items[0].host, "192.168.0.10") == 0);
    assert(store.items[0].port == 5901);
    assert(strcmp(store.items[0].password, "segredo") == 0); /* "lixo_extra" foi descartado, não virou senha */

    unlink(path);
}

static void test_touch_evicts_oldest_preserving_password_of_survivors(void) {
    /* test_touch_evicts_oldest_when_full já cobre a identidade (host) sobrevivente; este
     * cobre que a SENHA das entradas que ficam não é perdida/embaralhada na eviction. */
    ConnectionStore store = {0};
    char host[32];
    char password[32];
    for (int i = 0; i < CONNECTION_STORE_MAX; i++) {
        snprintf(host, sizeof(host), "10.0.0.%d", i);
        snprintf(password, sizeof(password), "senha%d", i);
        connection_store_touch(&store, host, 5901, password);
    }
    assert(store.count == CONNECTION_STORE_MAX);

    connection_store_touch(&store, "10.0.0.99", 5901, "senha99"); /* expulsa a mais antiga (10.0.0.0) */
    assert(store.count == CONNECTION_STORE_MAX);

    for (int i = 0; i < store.count; i++) {
        for (int j = 1; j < CONNECTION_STORE_MAX; j++) {
            snprintf(host, sizeof(host), "10.0.0.%d", j);
            if (strcmp(store.items[i].host, host) == 0) {
                snprintf(password, sizeof(password), "senha%d", j);
                assert(strcmp(store.items[i].password, password) == 0); /* senha certa continua ligada ao host certo */
            }
        }
    }
}

int main(void) {
    test_touch_new_entry_goes_to_front();
    test_touch_existing_entry_moves_to_front_without_duplicating();
    test_touch_same_host_different_port_is_a_different_entry();
    test_touch_evicts_oldest_when_full();
    test_save_then_load_round_trips_order();
    test_load_missing_file_yields_empty_store_not_error();
    test_load_ignores_malformed_lines();
    test_touch_host_at_max_length_boundary_is_preserved();
    test_touch_truncates_host_longer_than_max_length();
    test_touch_port_zero_and_negative_are_valid_identities();
    test_save_then_load_round_trips_negative_port();
    test_save_with_empty_store_produces_empty_file_that_loads_empty();
    test_save_then_load_with_empty_host_does_not_round_trip();
    test_touch_re_touching_existing_entry_when_full_does_not_evict_others();
    test_password_round_trips_and_null_means_empty();
    test_re_touch_updates_stored_password();
    test_load_old_format_without_password_still_works();
    test_touch_password_at_max_length_boundary_is_preserved();
    test_touch_truncates_password_longer_than_max_length();
    test_load_line_with_extra_token_after_password_ignores_the_rest();
    test_touch_evicts_oldest_preserving_password_of_survivors();

    printf("test_connection_store: todos os testes passaram\n");
    return 0;
}
