#ifndef KINDOW_CONNECTION_STORE_H
#define KINDOW_CONNECTION_STORE_H

#include <stdbool.h>

/*
 * Módulo puro (mesmo espírito de keyboard.c/pixel_convert.c: zero GTK, zero VNC,
 * testável como unidade) — histórico de conexões já usadas (host+porta), pra alimentar
 * a tela de conexão (ver docs/ideias-futuras.md, item 5). Ordenado por uso mais recente
 * primeiro (MRU): connection_store_touch já deixa a lista pronta pra exibir sem o
 * chamador precisar calcular "qual é o último usado" separadamente.
 *
 * Formato do arquivo: uma linha por entrada, "host porta [senha]" (texto simples — a
 * lista é pequena o bastante, no máximo CONNECTION_STORE_MAX, que JSON não compensaria).
 * A senha é opcional (linha sem terceiro token = conexão sem senha) e NÃO pode conter
 * espaço (o formato é separado por espaço; o teclado do formulário aceita espaço, mas a
 * validação de quem chama deve rejeitar — ver ui.c). Fica em TEXTO SIMPLES no arquivo,
 * decisão consciente de 27/08: o firmware do Kindle não tem keychain/storage seguro, e
 * quem tem acesso físico/SSH ao device já lê qualquer arquivo do cartão de qualquer
 * jeito — criptografia própria só mudaria ONDE guardar a chave, não o risco real.
 * Criptografia de verdade ficou registrada em docs/ideias-futuras.md.
 */

#define CONNECTION_STORE_MAX 8
#define CONNECTION_STORE_HOST_LEN 64
#define CONNECTION_STORE_PASSWORD_LEN 32

typedef struct {
    char host[CONNECTION_STORE_HOST_LEN];
    int port;
    /* "" = sem senha (o normal pro Xvnc com SecurityTypes=None deste projeto). */
    char password[CONNECTION_STORE_PASSWORD_LEN];
} SavedConnection;

typedef struct {
    SavedConnection items[CONNECTION_STORE_MAX];
    int count;
} ConnectionStore;

/* Carrega de path. Arquivo ausente ou corrompido não é erro fatal — é o caso "primeiro
 * uso" ou "arquivo mexido a mão": o store fica vazio (count=0) e o app segue normal, só
 * sem histórico. Linhas que não batem o formato são ignoradas silenciosamente. */
void connection_store_load(ConnectionStore *store, const char *path);

/* Salva o estado atual em path (sobrescreve). false se não deu pra escrever (ex. sem
 * permissão ou disco cheio) — o chamador decide se loga; perder o histórico não trava o
 * app, só perde a conveniência na próxima abertura. */
bool connection_store_save(const ConnectionStore *store, const char *path);

/* Marca host:porta como usado agora: se já existe (mesmo host+porta), reordena pro
 * topo E atualiza a senha guardada (a mais recente que funcionou vence — identidade da
 * entrada é só host+porta, a senha é dado carregado junto); senão insere no topo.
 * password==NULL equivale a "" (sem senha). Acima de CONNECTION_STORE_MAX, descarta o
 * mais antigo (fila pequena de propósito — é pra "os Pis que você usa", não um log). */
void connection_store_touch(ConnectionStore *store, const char *host, int port,
                            const char *password);

#endif
