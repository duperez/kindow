/*
 * Teste unitário do módulo de strings da UI (strings.c) — mesma convenção da suíte:
 * sem framework, assert() + exit(1), registrado como test() do Meson.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "strings.h"

static void test_default_language_is_english(void) {
    /* roda PRIMEIRO, antes de qualquer set_language — o default importa de verdade:
     * é o que um Kindle em qualquer língua não-portuguesa vai ver */
    assert(strings_language() == LANG_EN);
    assert(strcmp(tr(STR_BAR_KEYBOARD), "Keyboard") == 0);
}

static void test_every_id_has_text_in_both_languages(void) {
    /* pega tabela desalinhada/entrada esquecida: designated initializers deixam
     * buracos NULL em silêncio, e tr() converte NULL pra "" — um id sem texto nas
     * duas línguas é sempre bug de tabela */
    for (int lang = 0; lang < 2; lang++) {
        strings_set_language(lang == 0 ? LANG_EN : LANG_PT);
        for (int id = 0; id < STR_COUNT; id++) {
            const char *s = tr((StringId)id);
            assert(s != NULL);
            assert(s[0] != '\0');
        }
    }
}

static void test_switching_language_switches_text(void) {
    strings_set_language(LANG_PT);
    assert(strcmp(tr(STR_BAR_KEYBOARD), "Teclado") == 0);
    assert(strcmp(tr(STR_KEY_LEFT_CLICK), "Esquerdo") == 0);
    strings_set_language(LANG_EN);
    assert(strcmp(tr(STR_BAR_KEYBOARD), "Keyboard") == 0);
    assert(strcmp(tr(STR_KEY_LEFT_CLICK), "Left") == 0);
}

static void test_out_of_range_id_returns_empty_not_null(void) {
    assert(tr((StringId)-1) != NULL);
    assert(tr((StringId)-1)[0] == '\0');
    assert(tr((StringId)STR_COUNT) != NULL);
    assert(tr((StringId)STR_COUNT)[0] == '\0');
}

static void test_format_strings_have_expected_placeholders(void) {
    /* STR_CONNECTING/STR_CONNECT_FAILED são passados como FORMATO pro snprintf com
     * (host, porta) — um %s/%d fora de ordem ou faltando nas traduções viraria lixo ou
     * crash em runtime; trava aqui a presença e a ordem nas duas línguas */
    for (int lang = 0; lang < 2; lang++) {
        strings_set_language(lang == 0 ? LANG_EN : LANG_PT);
        StringId fmts[2] = {STR_CONNECTING, STR_CONNECT_FAILED};
        for (int i = 0; i < 2; i++) {
            const char *fmt = tr(fmts[i]);
            const char *s = strstr(fmt, "%s");
            const char *d = strstr(fmt, "%d");
            assert(s != NULL);
            assert(d != NULL);
            assert(s < d); /* host antes da porta, sempre */
        }
    }
}

int main(void) {
    test_default_language_is_english();
    test_every_id_has_text_in_both_languages();
    test_switching_language_switches_text();
    test_out_of_range_id_returns_empty_not_null();
    test_format_strings_have_expected_placeholders();

    printf("test_strings: todos os testes passaram\n");
    return 0;
}
