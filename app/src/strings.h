#ifndef KINDOW_STRINGS_H
#define KINDOW_STRINGS_H

/*
 * Módulo puro de strings da UI (mesmo espírito de keyboard.c/connection_store.c: zero
 * GTK, zero VNC, testável como unidade) — todas as strings visíveis ao usuário vivem
 * aqui, em duas línguas. Quem decide a língua é o main.c no boot (a partir do locale do
 * firmware do Kindle, ver kindle_platform_locale) — depois disso, todo texto visível
 * passa por tr().
 *
 * Inglês é o padrão (o público do jailbreak de Kindle é majoritariamente anglófono);
 * português entra quando o locale do device é pt_*. Logs de stderr NÃO passam por aqui
 * de propósito — são canal de debug, não UI, e ficam em português como o resto do
 * projeto.
 */

typedef enum {
    LANG_EN = 0, /* padrão — ver strings_set_language */
    LANG_PT,
} UiLanguage;

typedef enum {
    /* barra */
    STR_BAR_KEYBOARD,
    STR_BAR_MENU,
    STR_BAR_QUIT,
    /* menu */
    STR_MENU_APPS_OUT,
    STR_MENU_APPS_IN,
    STR_MENU_DECO_OUT,
    STR_MENU_DECO_IN,
    STR_MENU_PANEL_OUT,
    STR_MENU_PANEL_IN,
    STR_MENU_SCROLL_OUT,
    STR_MENU_SCROLL_IN,
    STR_MENU_DISCONNECT,
    STR_MENU_STATUS,
    STR_MENU_QUIT_APP,
    /* teclado (só as teclas com palavra traduzível — letras/símbolos são universais) */
    STR_KEY_LEFT_CLICK,
    STR_KEY_RIGHT_CLICK,
    /* tela de conexão */
    STR_FORM_IP,       /* prefixo "IP: " */
    STR_FORM_PORT,     /* prefixo "Porta: " */
    STR_FORM_PASSWORD, /* prefixo "Senha: " */
    STR_FORM_CONNECT,
    STR_FORM_CANCEL,
    STR_BACK,
    STR_CONNECTING,     /* formato: host, porta */
    STR_CONNECT_FAILED, /* formato: host, porta */
    STR_CHECK_FIELDS,   /* fallback do detalhe de erro */
    /* erros do vnc_client.c que alcançam a tela de erro (todo set_error nos caminhos
     * de try_connect/start_updates chega visível via on_connect_attempt_failed —
     * achado de review, 27/08: não só os 4 do handshake) */
    STR_ERR_NEEDS_PASSWORD,
    STR_ERR_PASSWORD_REFUSED,
    STR_ERR_CONNECT_FAILED,
    STR_ERR_HANDSHAKE_FAILED,
    STR_ERR_OUT_OF_MEMORY,
    STR_ERR_INIT_FAILED,
    STR_ERR_SESSION_SETUP_FAILED,
    STR_ERR_UPDATES_ALREADY_STARTED,
    STR_ERR_FIRST_UPDATE_FAILED,

    STR_COUNT,
} StringId;

/* Define a língua de toda a UI — chamar uma vez no boot, antes de criar a UI (não há
 * troca ao vivo: a língua do device não muda com o app aberto). Default sem chamada
 * nenhuma: inglês. */
void strings_set_language(UiLanguage lang);

UiLanguage strings_language(void);

/* A string do id na língua atual. Estática — não liberar. Ids fora da faixa retornam
 * "" (nunca NULL — chamadores passam direto pra snprintf/desenho sem checar). */
const char *tr(StringId id);

#endif
