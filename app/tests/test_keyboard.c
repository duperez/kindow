/*
 * Teste unitário do teclado virtual puro (keyboard.c) — mesmo espírito do
 * test_pixel_convert.c: sem GTK, sem libvncclient, sem I/O, só a lógica de layout,
 * hit-test e a máquina de estado dos sticky modifiers (Shift/Ctrl) e troca de página.
 *
 * Convenção do projeto: sem framework de teste externo, assert() + exit(1) em caso de
 * falha, registrado como test() do Meson.
 *
 * Os testes acham as teclas pelo RÓTULO via keyboard_key_view (nunca hardcodam x/y —
 * a geometria é dado de layout interno do módulo, não contrato público) e tocam no
 * centro do retângulo encontrado.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keyboard.h"

/* A assinatura de keyboard_handle_tap ganhou o out-param de ação (página de menu,
 * 26/08). Os casos abaixo são anteriores ao menu e só olham eventos/visual — este
 * wrapper absorve o parâmetro novo e ainda garante o contrato "eventos e ação nunca
 * vêm juntos no mesmo toque". Casos específicos do menu chamam a função real. */
static int tap(Keyboard *keyboard, int x, int y, KeyboardEvent *events, bool *visual) {
    KeyboardAction action = KEYBOARD_ACTION_NONE;
    int n = keyboard_handle_tap(keyboard, x, y, events, &action, visual);
    assert(!(n > 0 && action != KEYBOARD_ACTION_NONE));
    return n;
}

/* Keysyms X11 usados nas asserções — espelham as constantes internas de keyboard.c.
 * Não incluímos keyboard.c aqui de propósito: o teste deve validar o CONTRATO público
 * (quais keysyms saem pra cada tecla), não depender dos nomes internos do módulo. */
#define KS_BACKSPACE 0xFF08u
#define KS_TAB 0xFF09u
#define KS_RETURN 0xFF0Du
#define KS_ESCAPE 0xFF1Bu
#define KS_LEFT 0xFF51u
#define KS_UP 0xFF52u
#define KS_RIGHT 0xFF53u
#define KS_DOWN 0xFF54u
#define KS_CONTROL_L 0xFFE3u

/* Dimensões usadas em todos os testes — arbitrárias, mas fixas, pra geometria ser
 * reproduzível entre os casos (não precisa bater com resolução real de Kindle nenhuma,
 * já que o layout é proporcional). */
#define KB_TEST_WIDTH 800
#define KB_TEST_HEIGHT 600

/* Acha o índice da tecla, na página atual, cujo RÓTULO ATUAL é `label`. Falha alto (em
 * vez de devolver lixo) se não achar — um rótulo sumir do layout é o tipo de regressão
 * que a gente quer que estoure na cara, não vire falso-positivo silencioso. */
static int find_key_index_by_label(const Keyboard *keyboard, const char *label) {
    int count = keyboard_key_count(keyboard);
    for (int i = 0; i < count; i++) {
        if (strcmp(keyboard_key_view(keyboard, i).label, label) == 0) {
            return i;
        }
    }
    fprintf(stderr, "tecla com rótulo '%s' não encontrada na página atual\n", label);
    exit(1);
}

/* Centro do retângulo da tecla de índice `index`, na página atual — coordenada que com
 * certeza cai dentro do hit-test (find_key usa `<`, então a borda w/2 sempre é interna
 * pra qualquer w >= 1). */
static void center_of_index(const Keyboard *keyboard, int index, int *out_x, int *out_y) {
    KeyboardKeyView view = keyboard_key_view(keyboard, index);
    *out_x = view.x + view.w / 2;
    *out_y = view.y + view.h / 2;
}

/* Igual a find_key_index_by_label, mas devolve -1 em vez de sair do processo quando o
 * rótulo não existe na página atual — usada pra AFIRMAR ausência (ex.: "Voltar ao
 * teclado" não deve existir fora da página de menu), onde sumir é o resultado esperado,
 * não uma falha de teste. */
static int find_key_index_by_label_opt(const Keyboard *keyboard, const char *label) {
    int count = keyboard_key_count(keyboard);
    for (int i = 0; i < count; i++) {
        if (strcmp(keyboard_key_view(keyboard, i).label, label) == 0) {
            return i;
        }
    }
    return -1;
}

/* Entra na página de menu do jeito "normal": arma Shift, arma Ctrl, toca na tecla de
 * página (que nesse estado mostra "Menu" e consome os dois modificadores). Usado pelos
 * testes que só precisam CHEGAR na página de menu pra testar outra coisa — o mecanismo
 * do chord em si (rótulo "Menu", consumo dos modificadores) é coberto à parte em
 * test_menu_chord_opens_menu_and_consumes_modifiers. */
static void enter_menu_page(Keyboard *keyboard) {
    int shift_index = find_key_index_by_label(keyboard, "Shift");
    int ctrl_index = find_key_index_by_label(keyboard, "Ctrl");
    int page_index = find_key_index_by_label(keyboard, "?123");
    int sx, sy, cx, cy, px, py;
    center_of_index(keyboard, shift_index, &sx, &sy);
    center_of_index(keyboard, ctrl_index, &cx, &cy);
    center_of_index(keyboard, page_index, &px, &py);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;
    tap(keyboard, sx, sy, events, &visual_changed);
    tap(keyboard, cx, cy, events, &visual_changed);
    tap(keyboard, px, py, events, &visual_changed); /* consome o chord, abre PAGE_MENU */
}

/* Geometria: cada fileira cobre a largura inteira (primeira tecla da primeira fileira
 * começa em x=0, última tecla de CADA fileira termina exatamente em width), são 6
 * fileiras, e a última termina exatamente em height. Isso é o que garante que não sobra
 * nem falta um pixel de tela sem tecla em cima — bug aqui vira "toque não faz nada" numa
 * faixa real da tela do Kindle. */
static void test_geometry_rows_span_full_width_and_height(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int count = keyboard_key_count(keyboard);
    assert(count > 0);

    KeyboardKeyView first = keyboard_key_view(keyboard, 0);
    assert(first.x == 0);

    int distinct_rows = 0;
    int prev_y = -1;
    int last_row_y = -1;
    int last_row_h = -1;
    for (int i = 0; i < count; i++) {
        KeyboardKeyView view = keyboard_key_view(keyboard, i);
        if (view.y != prev_y) {
            /* mudou de fileira: a tecla anterior era a última da fileira que fechou, e
             * ela precisa terminar exatamente na largura total */
            if (i > 0) {
                KeyboardKeyView prev_last = keyboard_key_view(keyboard, i - 1);
                assert(prev_last.x + prev_last.w == KB_TEST_WIDTH);
            }
            distinct_rows++;
            prev_y = view.y;
        }
        if (i == count - 1) {
            assert(view.x + view.w == KB_TEST_WIDTH);
            last_row_y = view.y;
            last_row_h = view.h;
        }
    }
    assert(distinct_rows == 6);
    assert(last_row_y + last_row_h == KB_TEST_HEIGHT);

    keyboard_destroy(keyboard);
}

/* Hit-test básico: tocar no centro de uma tecla conhecida ('q') gera o par down/up do
 * keysym esperado, e nada de modificador armado sobra (visual_changed fica false). */
static void test_tap_on_known_key_generates_down_up_pair(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int q_index = find_key_index_by_label(keyboard, "q");
    int x, y;
    center_of_index(keyboard, q_index, &x, &y);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = true; /* valor de sentinela, pra provar que a função escreve nele */
    int n = tap(keyboard, x, y, events, &visual_changed);

    assert(n == 2);
    assert(events[0].keysym == 0x71u && events[0].down == true);  /* 'q' down */
    assert(events[1].keysym == 0x71u && events[1].down == false); /* 'q' up */
    assert(visual_changed == false);

    keyboard_destroy(keyboard);
}

/* Toque fora de qualquer tecla (fora dos limites da área do teclado nos dois eixos) não
 * gera evento nenhum e não mexe em estado visual. */
static void test_tap_outside_any_key_returns_zero_events(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = true;

    int n = tap(keyboard, -5, -5, events, &visual_changed);
    assert(n == 0);
    assert(visual_changed == false);

    visual_changed = true;
    n = tap(keyboard, KB_TEST_WIDTH + 50, KB_TEST_HEIGHT + 50, events,
                            &visual_changed);
    assert(n == 0);
    assert(visual_changed == false);

    keyboard_destroy(keyboard);
}

/* Sticky Shift: tocar em Shift não gera evento (só arma o estado e pede redraw); o
 * próximo toque numa letra sai maiúsculo E desarma Shift sozinho (um toque só, não
 * fica "preso" ligado); o rótulo mostrado também reflete o estado armado/desarmado. */
static void test_sticky_shift_uppercases_next_letter_then_disarms(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int shift_index = find_key_index_by_label(keyboard, "Shift");
    int q_index = find_key_index_by_label(keyboard, "q");
    int qx, qy;
    center_of_index(keyboard, q_index, &qx, &qy); /* geometria não muda com Shift armado */

    int sx, sy;
    center_of_index(keyboard, shift_index, &sx, &sy);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;

    /* toque no Shift: nenhum evento pro servidor, mas pede redraw */
    int n = tap(keyboard, sx, sy, events, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);

    /* com Shift armado, o rótulo da tecla 'q' passa a mostrar 'Q' (o que vai sair de
     * verdade), e a tecla Shift aparece destacada */
    assert(strcmp(keyboard_key_view(keyboard, q_index).label, "Q") == 0);
    assert(keyboard_key_view(keyboard, shift_index).highlighted == true);

    /* toque na letra: sai maiúsculo, e o toque consome o Shift armado */
    visual_changed = false;
    n = tap(keyboard, qx, qy, events, &visual_changed);
    assert(n == 2);
    assert(events[0].keysym == 0x51u && events[0].down == true);  /* 'Q' down */
    assert(events[1].keysym == 0x51u && events[1].down == false); /* 'Q' up */
    assert(visual_changed == true); /* Shift desarmou, redraw precisa acontecer */

    /* Shift desarmado: rótulo volta a minúsculo e não fica mais destacado */
    assert(strcmp(keyboard_key_view(keyboard, q_index).label, "q") == 0);
    assert(keyboard_key_view(keyboard, shift_index).highlighted == false);

    /* próximo toque na mesma tecla volta a sair minúsculo — prova que Shift não ficou
     * "preso" armado */
    visual_changed = true;
    n = tap(keyboard, qx, qy, events, &visual_changed);
    assert(n == 2);
    assert(events[0].keysym == 0x71u && events[0].down == true); /* 'q' minúsculo de novo */
    assert(visual_changed == false);

    keyboard_destroy(keyboard);
}

/* Sticky Ctrl: tocar em Ctrl não gera evento; o próximo toque numa letra sai embrulhado
 * em Control_L down/up ao redor do keysym normal (não maiúsculo — Ctrl não mexe em
 * caixa), na ordem Control_L down, tecla down, tecla up, Control_L up; e desarma sozinho
 * depois de um toque. */
static void test_sticky_ctrl_wraps_control_l_around_letter(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int ctrl_index = find_key_index_by_label(keyboard, "Ctrl");
    int c_index = find_key_index_by_label(keyboard, "c");
    int cx, cy;
    center_of_index(keyboard, c_index, &cx, &cy);
    int ctrl_x, ctrl_y;
    center_of_index(keyboard, ctrl_index, &ctrl_x, &ctrl_y);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;

    int n = tap(keyboard, ctrl_x, ctrl_y, events, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);
    assert(keyboard_key_view(keyboard, ctrl_index).highlighted == true);

    visual_changed = false;
    n = tap(keyboard, cx, cy, events, &visual_changed);
    assert(n == 4);
    assert(events[0].keysym == KS_CONTROL_L && events[0].down == true);
    assert(events[1].keysym == 0x63u && events[1].down == true);  /* 'c' down */
    assert(events[2].keysym == 0x63u && events[2].down == false); /* 'c' up */
    assert(events[3].keysym == KS_CONTROL_L && events[3].down == false);
    assert(visual_changed == true); /* Ctrl desarmou */
    assert(keyboard_key_view(keyboard, ctrl_index).highlighted == false);

    /* Ctrl desarmado: próximo toque na mesma letra sai "normal", sem Control_L em volta */
    visual_changed = true;
    n = tap(keyboard, cx, cy, events, &visual_changed);
    assert(n == 2);
    assert(events[0].keysym == 0x63u && events[0].down == true);
    assert(events[1].keysym == 0x63u && events[1].down == false);
    assert(visual_changed == false);

    keyboard_destroy(keyboard);
}

/* Ctrl+Shift armados juntos: o keysym embrulhado em Control_L é o MAIÚSCULO (a variante
 * shifted), e os dois modificadores desarmam no mesmo toque. */
static void test_ctrl_and_shift_together_wrap_uppercase_letter(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int shift_index = find_key_index_by_label(keyboard, "Shift");
    int ctrl_index = find_key_index_by_label(keyboard, "Ctrl");
    int c_index = find_key_index_by_label(keyboard, "c");
    int sx, sy, ctrl_x, ctrl_y, cx, cy;
    center_of_index(keyboard, shift_index, &sx, &sy);
    center_of_index(keyboard, ctrl_index, &ctrl_x, &ctrl_y);
    center_of_index(keyboard, c_index, &cx, &cy);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;

    tap(keyboard, sx, sy, events, &visual_changed);
    tap(keyboard, ctrl_x, ctrl_y, events, &visual_changed);
    assert(keyboard_key_view(keyboard, shift_index).highlighted == true);
    assert(keyboard_key_view(keyboard, ctrl_index).highlighted == true);

    visual_changed = false;
    int n = tap(keyboard, cx, cy, events, &visual_changed);
    assert(n == 4);
    assert(events[0].keysym == KS_CONTROL_L && events[0].down == true);
    assert(events[1].keysym == 0x43u && events[1].down == true);  /* 'C' maiúsculo down */
    assert(events[2].keysym == 0x43u && events[2].down == false); /* 'C' maiúsculo up */
    assert(events[3].keysym == KS_CONTROL_L && events[3].down == false);
    assert(visual_changed == true);

    /* os dois desarmaram com um toque só */
    assert(keyboard_key_view(keyboard, shift_index).highlighted == false);
    assert(keyboard_key_view(keyboard, ctrl_index).highlighted == false);

    keyboard_destroy(keyboard);
}

/* Troca de página (letras <-> símbolos): tocar em "?123" não gera evento, pede redraw, e
 * muda a contagem de teclas (página de símbolos tem número diferente de teclas). A linha
 * do QWERTY e a linha de símbolos 1 têm as mesmas 10 colunas de largura igual, então
 * tocar na MESMA coordenada onde estava 'q' deve acertar a tecla correspondente daquela
 * posição na página nova (o primeiro símbolo, '!'). "abc" volta pra página de letras. */
static void test_page_switch_changes_keys_at_same_position(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int letters_count = keyboard_key_count(keyboard);

    int q_index = find_key_index_by_label(keyboard, "q");
    int qx, qy;
    center_of_index(keyboard, q_index, &qx, &qy);

    int page_index = find_key_index_by_label(keyboard, "?123");
    int px, py;
    center_of_index(keyboard, page_index, &px, &py);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;

    int n = tap(keyboard, px, py, events, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);

    int symbols_count = keyboard_key_count(keyboard);
    assert(symbols_count != letters_count);

    /* mesma posição de tela que antes era 'q', agora na página de símbolos */
    visual_changed = false;
    n = tap(keyboard, qx, qy, events, &visual_changed);
    assert(n == 2);
    assert(events[0].keysym == 0x21u && events[0].down == true);  /* '!' down */
    assert(events[1].keysym == 0x21u && events[1].down == false); /* '!' up */

    /* volta pra letras via "abc" */
    int abc_index = find_key_index_by_label(keyboard, "abc");
    int ax, ay;
    center_of_index(keyboard, abc_index, &ax, &ay);

    visual_changed = false;
    n = tap(keyboard, ax, ay, events, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);
    assert(keyboard_key_count(keyboard) == letters_count);

    keyboard_destroy(keyboard);
}

/* Teclas especiais mandam o keysym certo (down/up), sem depender de modificador nenhum:
 * Backspace, Enter, Tab, Esc e as quatro setas. */
static void test_special_keys_send_expected_keysyms(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    struct {
        const char *label;
        uint32_t keysym;
    } cases[] = {
        {"Bksp", KS_BACKSPACE}, {"Enter", KS_RETURN}, {"Tab", KS_TAB}, {"Esc", KS_ESCAPE},
        {"←", KS_LEFT},         {"↑", KS_UP},          {"↓", KS_DOWN}, {"→", KS_RIGHT},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int index = find_key_index_by_label(keyboard, cases[i].label);
        int x, y;
        center_of_index(keyboard, index, &x, &y);

        KeyboardEvent events[KEYBOARD_MAX_EVENTS];
        bool visual_changed = true;
        int n = tap(keyboard, x, y, events, &visual_changed);

        assert(n == 2);
        assert(events[0].keysym == cases[i].keysym && events[0].down == true);
        assert(events[1].keysym == cases[i].keysym && events[1].down == false);
        assert(visual_changed == false);
    }

    keyboard_destroy(keyboard);
}

/* Nenhum modificador (Shift, Ctrl, troca de página) jamais escreve em out_events — só
 * mexem em estado local. Cobre os quatro rótulos de modificador que existem nas duas
 * páginas (Shift/Ctrl na de letras, Ctrl/"abc" na de símbolos — "?123" já testado
 * acima junto da troca de página). */
static void test_modifier_taps_never_produce_events(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    const char *modifier_labels[] = {"Shift", "Ctrl"};
    for (size_t i = 0; i < sizeof(modifier_labels) / sizeof(modifier_labels[0]); i++) {
        int index = find_key_index_by_label(keyboard, modifier_labels[i]);
        int x, y;
        center_of_index(keyboard, index, &x, &y);

        KeyboardEvent events[KEYBOARD_MAX_EVENTS];
        bool visual_changed = false;
        int n = tap(keyboard, x, y, events, &visual_changed);
        assert(n == 0);
    }

    keyboard_destroy(keyboard);
}

/* Chord do menu (Ctrl+Shift armados + toque na tecla de página): abre a página de menu
 * e consome os dois modificadores no mesmo toque — diferente da troca de página normal,
 * que não mexe em Shift/Ctrl. O rótulo da tecla de página já reflete o chord disponível
 * ANTES do toque ("Menu" em vez de "?123", com destaque), mesma engrenagem do rótulo
 * dinâmico do Shift. Chama keyboard_handle_tap direto (não o wrapper tap()) porque o
 * caso central deste teste é justamente checar out_action == NONE explicitamente. */
static void test_menu_chord_opens_menu_and_consumes_modifiers(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int letters_count = keyboard_key_count(keyboard);
    int page_index = find_key_index_by_label(keyboard, "?123");
    int px, py;
    center_of_index(keyboard, page_index, &px, &py); /* geometria não muda com modificador */

    int shift_index = find_key_index_by_label(keyboard, "Shift");
    int ctrl_index = find_key_index_by_label(keyboard, "Ctrl");
    int sx, sy, cx, cy;
    center_of_index(keyboard, shift_index, &sx, &sy);
    center_of_index(keyboard, ctrl_index, &cx, &cy);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;
    tap(keyboard, sx, sy, events, &visual_changed);
    tap(keyboard, cx, cy, events, &visual_changed);

    /* com os dois armados, a tecla de página vira a porta do menu: rótulo "Menu" e
     * destaque, mesmo antes de ser tocada */
    KeyboardKeyView page_view = keyboard_key_view(keyboard, page_index);
    assert(strcmp(page_view.label, "Menu") == 0);
    assert(page_view.highlighted == true);

    KeyboardAction action = KEYBOARD_ACTION_NONE;
    visual_changed = false;
    int n = keyboard_handle_tap(keyboard, px, py, events, &action, &visual_changed);
    assert(n == 0);
    assert(action == KEYBOARD_ACTION_NONE);
    assert(visual_changed == true);

    /* página mudou: contagem de teclas diferente, e "Voltar ao teclado" passou a existir */
    assert(keyboard_key_count(keyboard) != letters_count);
    int back_index = find_key_index_by_label(keyboard, "Voltar ao teclado");
    int bx, by;
    center_of_index(keyboard, back_index, &bx, &by);

    /* os dois modificadores foram consumidos pelo chord: voltando pro teclado, "q" sai
     * minúsculo (Shift consumido) e sem Control_L em volta (Ctrl consumido) */
    visual_changed = false;
    n = tap(keyboard, bx, by, events, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);
    assert(keyboard_key_count(keyboard) == letters_count);

    int q_index = find_key_index_by_label(keyboard, "q");
    int qx, qy;
    center_of_index(keyboard, q_index, &qx, &qy);
    visual_changed = true;
    n = tap(keyboard, qx, qy, events, &visual_changed);
    assert(n == 2);
    assert(events[0].keysym == 0x71u && events[0].down == true); /* 'q' minúsculo */
    assert(events[1].keysym == 0x71u && events[1].down == false);
    assert(visual_changed == false); /* nenhum modificador sobrou pra desarmar */

    keyboard_destroy(keyboard);
}

/* Sem os dois modificadores armados JUNTOS, a tecla de página sempre alterna
 * letras<->símbolos — nunca abre o menu. Cobre os dois jeitos de "sem chord": só Shift
 * armado, e nenhum modificador armado (o caso mais comum, já que a maioria dos toques
 * na tecla de página é troca de página de verdade). */
static void test_page_toggle_without_chord_never_opens_menu(void) {
    /* caso 1: só Shift armado (sem Ctrl) */
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int shift_index = find_key_index_by_label(keyboard, "Shift");
    int sx, sy;
    center_of_index(keyboard, shift_index, &sx, &sy);
    int page_index = find_key_index_by_label(keyboard, "?123");
    int px, py;
    center_of_index(keyboard, page_index, &px, &py);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;
    tap(keyboard, sx, sy, events, &visual_changed); /* arma só Shift */

    /* sem Ctrl também armado, a tecla de página continua "?123", nunca "Menu" */
    assert(strcmp(keyboard_key_view(keyboard, page_index).label, "?123") == 0);

    visual_changed = false;
    int n = tap(keyboard, px, py, events, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);
    /* foi troca de página normal: "Voltar ao teclado" não existe na página resultante */
    assert(find_key_index_by_label_opt(keyboard, "Voltar ao teclado") == -1);

    keyboard_destroy(keyboard);

    /* caso 2: nenhum modificador armado */
    keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);
    page_index = find_key_index_by_label(keyboard, "?123");
    center_of_index(keyboard, page_index, &px, &py);

    visual_changed = false;
    n = tap(keyboard, px, py, events, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);
    assert(find_key_index_by_label_opt(keyboard, "Voltar ao teclado") == -1);

    keyboard_destroy(keyboard);
}

/* Toques na página de menu, fora de "Voltar ao teclado": nunca produzem evento pro
 * servidor (contrato "eventos e ação nunca vêm juntos", checado explicitamente aqui via
 * n == 0 sempre que a ação não é NONE), só emitem a KeyboardAction correspondente — e
 * visual_changed fica false, já que a ação não muda nada visível no PRÓPRIO teclado
 * (quem reage à ação, redesenhando o que for, é o chamador em main.c/ui.c). Cobre os
 * seis rótulos de ação: os três pares A+/A- de zoom (independentes entre si), "Sair do
 * Kindow" e "Status da conexão (log)". */
static void test_menu_actions_emit_expected_action_without_events(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);
    enter_menu_page(keyboard);

    struct {
        const char *label;
        KeyboardAction action;
    } cases[] = {
        {"Apps  A+", KEYBOARD_ACTION_ZOOM_APPS_IN},
        {"Apps  A-", KEYBOARD_ACTION_ZOOM_APPS_OUT},
        {"Janela  A+", KEYBOARD_ACTION_ZOOM_DECO_IN},
        {"Janela  A-", KEYBOARD_ACTION_ZOOM_DECO_OUT},
        {"Painel  A+", KEYBOARD_ACTION_ZOOM_PANEL_IN},
        {"Painel  A-", KEYBOARD_ACTION_ZOOM_PANEL_OUT},
        {"Sair do Kindow", KEYBOARD_ACTION_QUIT},
        {"Status da conexão (log)", KEYBOARD_ACTION_STATUS},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int index = find_key_index_by_label(keyboard, cases[i].label);
        int x, y;
        center_of_index(keyboard, index, &x, &y);

        KeyboardEvent events[KEYBOARD_MAX_EVENTS];
        KeyboardAction action = KEYBOARD_ACTION_NONE;
        bool visual_changed = true; /* sentinela: prova que a função escreve false nela */
        int n = keyboard_handle_tap(keyboard, x, y, events, &action, &visual_changed);

        assert(n == 0); /* contrato: eventos e ação nunca vêm juntos no mesmo toque */
        assert(action == cases[i].action);
        assert(visual_changed == false);
    }

    keyboard_destroy(keyboard);
}

/* "Voltar ao teclado": volta pra página de letras (rótulo "q" existe de novo, contagem
 * de teclas volta ao valor original), sem gerar evento nem ação, mas pedindo redraw
 * (página mudou). */
static void test_menu_back_returns_to_letters_page(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);
    int letters_count = keyboard_key_count(keyboard);

    enter_menu_page(keyboard);
    assert(keyboard_key_count(keyboard) != letters_count);

    int back_index = find_key_index_by_label(keyboard, "Voltar ao teclado");
    int bx, by;
    center_of_index(keyboard, back_index, &bx, &by);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    KeyboardAction action = KEYBOARD_ACTION_NONE;
    bool visual_changed = false;
    int n = keyboard_handle_tap(keyboard, bx, by, events, &action, &visual_changed);

    assert(n == 0);
    assert(action == KEYBOARD_ACTION_NONE);
    assert(visual_changed == true);
    assert(keyboard_key_count(keyboard) == letters_count);
    find_key_index_by_label(keyboard, "q"); /* existe de novo (sai do processo se não achar) */

    keyboard_destroy(keyboard);
}

/* keyboard_key_index_at é a versão sem efeito colateral do hit-test: mesmo índice que
 * keyboard_handle_tap usaria pra tecla sob o toque, mas sem mudar estado nem gerar
 * evento/ação — e sempre relativo à página ATUAL, igual keyboard_key_view/count (a
 * mesma coordenada aponta pra teclas diferentes dependendo da página, como no teste de
 * troca de página acima). */
static void test_key_index_at_matches_label_lookup_and_handles_out_of_bounds(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    int q_index = find_key_index_by_label(keyboard, "q");
    int qx, qy;
    center_of_index(keyboard, q_index, &qx, &qy);
    assert(keyboard_key_index_at(keyboard, qx, qy) == q_index);

    /* fora dos limites: negativo nos dois eixos, além de width/height, e exatamente na
     * borda direita/inferior (find_key usa '<', então a borda já é externa) */
    assert(keyboard_key_index_at(keyboard, -5, -5) == -1);
    assert(keyboard_key_index_at(keyboard, KB_TEST_WIDTH + 50, KB_TEST_HEIGHT + 50) == -1);
    assert(keyboard_key_index_at(keyboard, KB_TEST_WIDTH, 0) == -1);
    assert(keyboard_key_index_at(keyboard, 0, KB_TEST_HEIGHT) == -1);

    /* na página de menu, a mesma consulta usa índice/geometria do menu — prova de que a
     * consulta é relativa à página atual, não sempre à de letras */
    enter_menu_page(keyboard);
    int back_index = find_key_index_by_label(keyboard, "Voltar ao teclado");
    int bx, by;
    center_of_index(keyboard, back_index, &bx, &by);
    assert(keyboard_key_index_at(keyboard, bx, by) == back_index);

    keyboard_destroy(keyboard);
}

/* O chord Ctrl+Shift também abre o menu a partir da página de SÍMBOLOS — comportamento
 * intencional (a tecla "abc" é KEY_PAGE igual à "?123", e o chord vale pra qualquer
 * tecla de página fora do menu). O caminho real exercitado: a página de símbolos não
 * tem tecla Shift, mas o Shift armado SOBREVIVE à troca de página (só o chord ou uma
 * tecla normal consomem) — então arma-se Shift nas letras, troca-se de página, e o Ctrl
 * de lá completa o chord. Gap apontado em review de teste, fechado aqui. */
static void test_menu_chord_works_from_symbols_page(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool visual_changed = false;
    int x, y;

    /* letras: arma só o Shift e troca de página — sem o chord completo, "?123" deve
     * alternar normalmente (não abrir menu), levando o Shift armado junto */
    center_of_index(keyboard, find_key_index_by_label(keyboard, "Shift"), &x, &y);
    tap(keyboard, x, y, events, &visual_changed);
    center_of_index(keyboard, find_key_index_by_label(keyboard, "?123"), &x, &y);
    tap(keyboard, x, y, events, &visual_changed);
    assert(find_key_index_by_label_opt(keyboard, "Voltar ao teclado") < 0);
    int abc_index = find_key_index_by_label_opt(keyboard, "abc");
    assert(abc_index >= 0); /* chegou na página de símbolos */

    /* símbolos: arma o Ctrl daqui — o chord completa e a tecla de página vira "Menu" */
    center_of_index(keyboard, find_key_index_by_label(keyboard, "Ctrl"), &x, &y);
    tap(keyboard, x, y, events, &visual_changed);
    KeyboardKeyView page_key = keyboard_key_view(keyboard, abc_index);
    assert(strcmp(page_key.label, "Menu") == 0);
    assert(page_key.highlighted);

    center_of_index(keyboard, abc_index, &x, &y);
    tap(keyboard, x, y, events, &visual_changed);
    assert(find_key_index_by_label_opt(keyboard, "Voltar ao teclado") >= 0);

    keyboard_destroy(keyboard);
}

int main(void) {
    test_geometry_rows_span_full_width_and_height();
    test_tap_on_known_key_generates_down_up_pair();
    test_tap_outside_any_key_returns_zero_events();
    test_sticky_shift_uppercases_next_letter_then_disarms();
    test_sticky_ctrl_wraps_control_l_around_letter();
    test_ctrl_and_shift_together_wrap_uppercase_letter();
    test_page_switch_changes_keys_at_same_position();
    test_special_keys_send_expected_keysyms();
    test_modifier_taps_never_produce_events();
    test_menu_chord_opens_menu_and_consumes_modifiers();
    test_page_toggle_without_chord_never_opens_menu();
    test_menu_actions_emit_expected_action_without_events();
    test_menu_back_returns_to_letters_page();
    test_key_index_at_matches_label_lookup_and_handles_out_of_bounds();
    test_menu_chord_works_from_symbols_page();

    printf("test_keyboard: todos os testes passaram\n");
    return 0;
}
