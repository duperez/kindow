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
    bool right_click;
    bool visual_changed = true; /* valor de sentinela, pra provar que a função escreve nele */
    int n = keyboard_handle_tap(keyboard, x, y, events, &right_click, &visual_changed);

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
    bool right_click;

    int n = keyboard_handle_tap(keyboard, -5, -5, events, &right_click, &visual_changed);
    assert(n == 0);
    assert(visual_changed == false);

    visual_changed = true;
    n = keyboard_handle_tap(keyboard, KB_TEST_WIDTH + 50, KB_TEST_HEIGHT + 50, events,
                            &right_click, &visual_changed);
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
    bool right_click;

    /* toque no Shift: nenhum evento pro servidor, mas pede redraw */
    int n = keyboard_handle_tap(keyboard, sx, sy, events, &right_click, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);

    /* com Shift armado, o rótulo da tecla 'q' passa a mostrar 'Q' (o que vai sair de
     * verdade), e a tecla Shift aparece destacada */
    assert(strcmp(keyboard_key_view(keyboard, q_index).label, "Q") == 0);
    assert(keyboard_key_view(keyboard, shift_index).highlighted == true);

    /* toque na letra: sai maiúsculo, e o toque consome o Shift armado */
    visual_changed = false;
    n = keyboard_handle_tap(keyboard, qx, qy, events, &right_click, &visual_changed);
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
    n = keyboard_handle_tap(keyboard, qx, qy, events, &right_click, &visual_changed);
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
    bool right_click;

    int n = keyboard_handle_tap(keyboard, ctrl_x, ctrl_y, events, &right_click, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);
    assert(keyboard_key_view(keyboard, ctrl_index).highlighted == true);

    visual_changed = false;
    n = keyboard_handle_tap(keyboard, cx, cy, events, &right_click, &visual_changed);
    assert(n == 4);
    assert(events[0].keysym == KS_CONTROL_L && events[0].down == true);
    assert(events[1].keysym == 0x63u && events[1].down == true);  /* 'c' down */
    assert(events[2].keysym == 0x63u && events[2].down == false); /* 'c' up */
    assert(events[3].keysym == KS_CONTROL_L && events[3].down == false);
    assert(visual_changed == true); /* Ctrl desarmou */
    assert(keyboard_key_view(keyboard, ctrl_index).highlighted == false);

    /* Ctrl desarmado: próximo toque na mesma letra sai "normal", sem Control_L em volta */
    visual_changed = true;
    n = keyboard_handle_tap(keyboard, cx, cy, events, &right_click, &visual_changed);
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
    bool right_click;

    keyboard_handle_tap(keyboard, sx, sy, events, &right_click, &visual_changed);
    keyboard_handle_tap(keyboard, ctrl_x, ctrl_y, events, &right_click, &visual_changed);
    assert(keyboard_key_view(keyboard, shift_index).highlighted == true);
    assert(keyboard_key_view(keyboard, ctrl_index).highlighted == true);

    visual_changed = false;
    int n = keyboard_handle_tap(keyboard, cx, cy, events, &right_click, &visual_changed);
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
    bool right_click;

    int n = keyboard_handle_tap(keyboard, px, py, events, &right_click, &visual_changed);
    assert(n == 0);
    assert(visual_changed == true);

    int symbols_count = keyboard_key_count(keyboard);
    assert(symbols_count != letters_count);

    /* mesma posição de tela que antes era 'q', agora na página de símbolos */
    visual_changed = false;
    n = keyboard_handle_tap(keyboard, qx, qy, events, &right_click, &visual_changed);
    assert(n == 2);
    assert(events[0].keysym == 0x21u && events[0].down == true);  /* '!' down */
    assert(events[1].keysym == 0x21u && events[1].down == false); /* '!' up */

    /* volta pra letras via "abc" */
    int abc_index = find_key_index_by_label(keyboard, "abc");
    int ax, ay;
    center_of_index(keyboard, abc_index, &ax, &ay);

    visual_changed = false;
    n = keyboard_handle_tap(keyboard, ax, ay, events, &right_click, &visual_changed);
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
        bool right_click;
        int n = keyboard_handle_tap(keyboard, x, y, events, &right_click, &visual_changed);

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
        bool right_click;
        int n = keyboard_handle_tap(keyboard, x, y, events, &right_click, &visual_changed);
        assert(n == 0);
    }

    keyboard_destroy(keyboard);
}

/* keyboard_key_index_at é a versão sem efeito colateral do hit-test: mesmo índice que
 * keyboard_handle_tap usaria pra tecla sob o toque, mas sem mudar estado nem gerar
 * evento — e sempre relativo à página ATUAL, igual keyboard_key_view/count (a mesma
 * coordenada aponta pra teclas diferentes dependendo da página, como no teste de troca
 * de página acima). */
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

    keyboard_destroy(keyboard);
}

/* "Esquerdo"/"Direito" só existem na página de símbolos, substituindo o espaço só ali —
 * nas letras o espaço continua normal (decisão do usuário, 27/08: ?123 já é modo
 * explícito, então não custa reaproveitar; digitar espaço no dia a dia continua
 * funcionando na página comum). */
static void test_left_right_click_keys_only_on_symbols_page(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    /* letras: espaço existe (rótulo vazio, achado por não ser nenhum dos outros — usamos
     * keyboard_key_count como prova indireta de que Esquerdo/Direito não estão aqui) */
    int count = keyboard_key_count(keyboard);
    for (int i = 0; i < count; i++) {
        const char *label = keyboard_key_view(keyboard, i).label;
        assert(strcmp(label, "Esquerdo") != 0);
        assert(strcmp(label, "Direito") != 0);
    }

    /* símbolos: aparecem os dois, espaço não existe mais */
    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool right_click = false;
    bool visual_changed = false;
    int page_index = find_key_index_by_label(keyboard, "?123");
    int px, py;
    center_of_index(keyboard, page_index, &px, &py);
    keyboard_handle_tap(keyboard, px, py, events, &right_click, &visual_changed);

    find_key_index_by_label(keyboard, "Esquerdo"); /* sai do processo se não achar */
    find_key_index_by_label(keyboard, "Direito");

    keyboard_destroy(keyboard);
}

/* Sticky "Esquerdo": tocar arma (0 eventos, sem right_click, pede redraw, tecla fica
 * destacada) e o estado é consultável/consumível de fora (keyboard_left_click_armed) —
 * é assim que o ui.c vai saber, no toque seguinte no FRAME (fora da grade do teclado),
 * que deve iniciar um arrasto em vez de um clique normal. Tocar de novo desarma (mesmo
 * padrão toggle de Shift/Ctrl). */
static void test_left_click_key_arms_and_toggles(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool right_click = true;   /* sentinela: prova que a função escreve false */
    bool visual_changed = false;
    int page_index = find_key_index_by_label(keyboard, "?123");
    int px, py;
    center_of_index(keyboard, page_index, &px, &py);
    keyboard_handle_tap(keyboard, px, py, events, &right_click, &visual_changed);
    assert(keyboard_left_click_armed(keyboard) == false); /* ainda não tocou Esquerdo */

    int left_index = find_key_index_by_label(keyboard, "Esquerdo");
    int lx, ly;
    center_of_index(keyboard, left_index, &lx, &ly);

    right_click = true;
    visual_changed = false;
    int n = keyboard_handle_tap(keyboard, lx, ly, events, &right_click, &visual_changed);
    assert(n == 0);
    assert(right_click == false);
    assert(visual_changed == true);
    assert(keyboard_left_click_armed(keyboard) == true);
    assert(keyboard_key_view(keyboard, left_index).highlighted == true);

    /* consumo externo (simula o ui.c usando o arme num toque no frame) */
    keyboard_consume_left_click_arm(keyboard);
    assert(keyboard_left_click_armed(keyboard) == false);
    assert(keyboard_key_view(keyboard, left_index).highlighted == false);

    /* toggle: tocar Esquerdo de novo arma; tocar uma TERCEIRA vez desarma sem precisar
     * de consumo externo */
    keyboard_handle_tap(keyboard, lx, ly, events, &right_click, &visual_changed);
    assert(keyboard_left_click_armed(keyboard) == true);
    keyboard_handle_tap(keyboard, lx, ly, events, &right_click, &visual_changed);
    assert(keyboard_left_click_armed(keyboard) == false);

    keyboard_destroy(keyboard);
}

/* Achado de review (27/08): armar "Esquerdo" e trocar de página (via "abc", voltando pra
 * letras) tem que desarmar — sem isso, o arme ficaria vivo sem NENHUM indicador visual
 * (a tecla que mostrava o destaque desapareceu junto com a página de símbolos), e o
 * próximo arrasto no frame dispararia sem o usuário saber. */
static void test_left_click_arm_cleared_by_page_switch(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool right_click = false;
    bool visual_changed = false;
    int x, y;

    center_of_index(keyboard, find_key_index_by_label(keyboard, "?123"), &x, &y);
    keyboard_handle_tap(keyboard, x, y, events, &right_click, &visual_changed);

    center_of_index(keyboard, find_key_index_by_label(keyboard, "Esquerdo"), &x, &y);
    keyboard_handle_tap(keyboard, x, y, events, &right_click, &visual_changed);
    assert(keyboard_left_click_armed(keyboard) == true);

    /* volta pra letras via "abc" — o arme não deve sobreviver à troca */
    center_of_index(keyboard, find_key_index_by_label(keyboard, "abc"), &x, &y);
    keyboard_handle_tap(keyboard, x, y, events, &right_click, &visual_changed);
    assert(keyboard_left_click_armed(keyboard) == false);

    keyboard_destroy(keyboard);
}

/* "Direito": ação imediata — 0 eventos, right_click vem true, sem mudar estado visual
 * nem armar nada (diferente de Esquerdo, que é sticky). */
static void test_right_click_key_reports_immediately(void) {
    Keyboard *keyboard = keyboard_create(KB_TEST_WIDTH, KB_TEST_HEIGHT);
    assert(keyboard != NULL);

    KeyboardEvent events[KEYBOARD_MAX_EVENTS];
    bool right_click = false;
    bool visual_changed = false;
    int page_index = find_key_index_by_label(keyboard, "?123");
    int px, py;
    center_of_index(keyboard, page_index, &px, &py);
    keyboard_handle_tap(keyboard, px, py, events, &right_click, &visual_changed);

    int right_index = find_key_index_by_label(keyboard, "Direito");
    int rx, ry;
    center_of_index(keyboard, right_index, &rx, &ry);

    right_click = false;
    visual_changed = true; /* sentinela */
    int n = keyboard_handle_tap(keyboard, rx, ry, events, &right_click, &visual_changed);
    assert(n == 0);
    assert(right_click == true);
    assert(visual_changed == false);
    assert(keyboard_left_click_armed(keyboard) == false); /* não mexe no sticky esquerdo */

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
    test_key_index_at_matches_label_lookup_and_handles_out_of_bounds();
    test_left_right_click_keys_only_on_symbols_page();
    test_left_click_key_arms_and_toggles();
    test_left_click_arm_cleared_by_page_switch();
    test_right_click_key_reports_immediately();

    printf("test_keyboard: todos os testes passaram\n");
    return 0;
}
