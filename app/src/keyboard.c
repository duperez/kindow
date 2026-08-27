#include "keyboard.h"

#include <stdlib.h>

/* Keysyms X11 usados aqui (rfb/keysym.h existe na lib vendorizada, mas incluir ele
 * acoplaria este módulo puro ao header da libvncclient por meia dúzia de constantes
 * públicas e estáveis do padrão X11 — replicadas locais de propósito). */
#define KS_BACKSPACE 0xFF08u
#define KS_TAB 0xFF09u
#define KS_RETURN 0xFF0Du
#define KS_ESCAPE 0xFF1Bu
#define KS_LEFT 0xFF51u
#define KS_UP 0xFF52u
#define KS_RIGHT 0xFF53u
#define KS_DOWN 0xFF54u
#define KS_CONTROL_L 0xFFE3u

typedef enum {
    KEY_NORMAL, /* manda keysym (ou a variante shifted, se Shift armado) */
    KEY_SHIFT,  /* sticky: arma Shift pro próximo toque */
    KEY_CTRL,   /* sticky: arma Ctrl pro próximo toque */
    KEY_PAGE,   /* alterna letras <-> símbolos; com Ctrl+Shift armados abre o MENU */
    KEY_ACTION, /* página de menu: emite uma KeyboardAction local (nada vai pro servidor) */
} KeyType;

typedef struct {
    const char *label;
    const char *shifted_label; /* rótulo com Shift armado; NULL = mantém o normal */
    uint32_t keysym;
    uint32_t shifted; /* keysym com Shift armado; 0 = sem variante (manda o normal) */
    float units;      /* largura relativa dentro da linha (1.0 = tecla padrão) */
    KeyType type;
    KeyboardAction action; /* só pra KEY_ACTION; zero-init nos demais */
} KeyDef;

/* Atalhos pra tabela ficar legível. Pra ASCII imprimível, keysym == código do caractere. */
#define K(ch, lbl) {lbl, NULL, (uint32_t)(ch), 0, 1.0f, KEY_NORMAL, 0}
#define KS(ch, sh, lbl, shlbl) \
    {lbl, shlbl, (uint32_t)(ch), (uint32_t)(sh), 1.0f, KEY_NORMAL, 0}
#define KW(ch, lbl, u) {lbl, NULL, (uint32_t)(ch), 0, u, KEY_NORMAL, 0}
#define KSW(ch, sh, lbl, shlbl, u) \
    {lbl, shlbl, (uint32_t)(ch), (uint32_t)(sh), u, KEY_NORMAL, 0}

/* ---- Página 0: letras ---- */

static const KeyDef kRowDigits[] = {
    KS('1', '!', "1", "!"), KS('2', '@', "2", "@"), KS('3', '#', "3", "#"),
    KS('4', '$', "4", "$"), KS('5', '%', "5", "%"), KS('6', '^', "6", "^"),
    KS('7', '&', "7", "&"), KS('8', '*', "8", "*"), KS('9', '(', "9", "("),
    KS('0', ')', "0", ")"),
};

static const KeyDef kRowQwerty[] = {
    KS('q', 'Q', "q", "Q"), KS('w', 'W', "w", "W"), KS('e', 'E', "e", "E"),
    KS('r', 'R', "r", "R"), KS('t', 'T', "t", "T"), KS('y', 'Y', "y", "Y"),
    KS('u', 'U', "u", "U"), KS('i', 'I', "i", "I"), KS('o', 'O', "o", "O"),
    KS('p', 'P', "p", "P"),
};

static const KeyDef kRowHome[] = {
    KS('a', 'A', "a", "A"), KS('s', 'S', "s", "S"), KS('d', 'D', "d", "D"),
    KS('f', 'F', "f", "F"), KS('g', 'G', "g", "G"), KS('h', 'H', "h", "H"),
    KS('j', 'J', "j", "J"), KS('k', 'K', "k", "K"), KS('l', 'L', "l", "L"),
};

static const KeyDef kRowShift[] = {
    {"Shift", NULL, 0, 0, 1.5f, KEY_SHIFT, 0},
    KS('z', 'Z', "z", "Z"), KS('x', 'X', "x", "X"), KS('c', 'C', "c", "C"),
    KS('v', 'V', "v", "V"), KS('b', 'B', "b", "B"), KS('n', 'N', "n", "N"),
    KS('m', 'M', "m", "M"),
    /* "Bksp" em texto, não glifo ⌫ — a fonte do Kindle não tem o glifo (aparecia como
     * quadradinho/tofu, visto no hardware real). */
    {"Bksp", NULL, KS_BACKSPACE, 0, 1.5f, KEY_NORMAL, 0},
};

static const KeyDef kRowSpaceLetters[] = {
    {"?123", NULL, 0, 0, 1.5f, KEY_PAGE, 0},
    {"Ctrl", NULL, 0, 0, 1.5f, KEY_CTRL, 0},
    KS(',', ';', ",", ";"),
    KW(' ', "", 3.0f),
    KS('.', ':', ".", ":"),
    {"Enter", NULL, KS_RETURN, 0, 2.0f, KEY_NORMAL, 0},
};

static const KeyDef kRowNav[] = {
    {"Esc", NULL, KS_ESCAPE, 0, 1.25f, KEY_NORMAL, 0},
    {"Tab", NULL, KS_TAB, 0, 1.25f, KEY_NORMAL, 0},
    KSW('/', '?', "/", "?", 1.25f),
    KSW('-', '_', "-", "_", 1.25f),
    {"←", NULL, KS_LEFT, 0, 1.25f, KEY_NORMAL, 0},
    {"↑", NULL, KS_UP, 0, 1.25f, KEY_NORMAL, 0},
    {"↓", NULL, KS_DOWN, 0, 1.25f, KEY_NORMAL, 0},
    {"→", NULL, KS_RIGHT, 0, 1.25f, KEY_NORMAL, 0},
};

/* ---- Página 1: símbolos (mesma fileira de números/nav; miolo trocado) ---- */

static const KeyDef kRowSym1[] = {
    K('!', "!"), K('@', "@"), K('#', "#"), K('$', "$"), K('%', "%"),
    K('^', "^"), K('&', "&"), K('*', "*"), K('(', "("), K(')', ")"),
};

static const KeyDef kRowSym2[] = {
    K('~', "~"), K('`', "`"), K('\'', "'"), K('"', "\""), K('|', "|"),
    K('\\', "\\"), K('{', "{"), K('}', "}"), K('[', "["), K(']', "]"),
};

static const KeyDef kRowSym3[] = {
    K('<', "<"), K('>', ">"), K('=', "="), K('+', "+"), K('_', "_"),
    K(':', ":"), K(';', ";"), K('?', "?"),
    {"Bksp", NULL, KS_BACKSPACE, 0, 2.0f, KEY_NORMAL, 0},
};

static const KeyDef kRowSpaceSymbols[] = {
    {"abc", NULL, 0, 0, 1.5f, KEY_PAGE, 0},
    {"Ctrl", NULL, 0, 0, 1.5f, KEY_CTRL, 0},
    K(',', ","),
    KW(' ', "", 3.0f),
    K('.', "."),
    {"Enter", NULL, KS_RETURN, 0, 2.0f, KEY_NORMAL, 0},
};

/* ---- Página 2: menu do app (ações locais — ver KeyboardAction no .h). Um par A-/A+
 * por camada de zoom, cada uma independente das outras; "Sair" fica na última fileira,
 * longe dos pares de zoom que o usuário toca repetidamente. ---- */

static const KeyDef kMenuBack[] = {
    {"Voltar ao teclado", NULL, 0, 0, 1.0f, KEY_PAGE, 0},
};

static const KeyDef kMenuZoomApps[] = {
    {"Apps  A-", NULL, 0, 0, 1.0f, KEY_ACTION, KEYBOARD_ACTION_ZOOM_APPS_OUT},
    {"Apps  A+", NULL, 0, 0, 1.0f, KEY_ACTION, KEYBOARD_ACTION_ZOOM_APPS_IN},
};

static const KeyDef kMenuZoomDeco[] = {
    {"Janela  A-", NULL, 0, 0, 1.0f, KEY_ACTION, KEYBOARD_ACTION_ZOOM_DECO_OUT},
    {"Janela  A+", NULL, 0, 0, 1.0f, KEY_ACTION, KEYBOARD_ACTION_ZOOM_DECO_IN},
};

static const KeyDef kMenuZoomPanel[] = {
    {"Painel  A-", NULL, 0, 0, 1.0f, KEY_ACTION, KEYBOARD_ACTION_ZOOM_PANEL_OUT},
    {"Painel  A+", NULL, 0, 0, 1.0f, KEY_ACTION, KEYBOARD_ACTION_ZOOM_PANEL_IN},
};

static const KeyDef kMenuStatus[] = {
    {"Status da conexão (log)", NULL, 0, 0, 1.0f, KEY_ACTION, KEYBOARD_ACTION_STATUS},
};

static const KeyDef kMenuQuit[] = {
    {"Sair do Kindow", NULL, 0, 0, 1.0f, KEY_ACTION, KEYBOARD_ACTION_QUIT},
};

typedef struct {
    const KeyDef *keys;
    int count;
} RowDef;

#define ROW(r) {r, (int)(sizeof(r) / sizeof((r)[0]))}

#define KB_ROWS 6

static const RowDef kPageLetters[KB_ROWS] = {
    ROW(kRowDigits), ROW(kRowQwerty), ROW(kRowHome),
    ROW(kRowShift), ROW(kRowSpaceLetters), ROW(kRowNav),
};

static const RowDef kPageSymbols[KB_ROWS] = {
    ROW(kRowDigits), ROW(kRowSym1), ROW(kRowSym2),
    ROW(kRowSym3), ROW(kRowSpaceSymbols), ROW(kRowNav),
};

static const RowDef kPageMenu[KB_ROWS] = {
    ROW(kMenuBack), ROW(kMenuZoomApps), ROW(kMenuZoomDeco),
    ROW(kMenuZoomPanel), ROW(kMenuStatus), ROW(kMenuQuit),
};

#define KB_PAGES 3
#define PAGE_LETTERS 0
#define PAGE_SYMBOLS 1
#define PAGE_MENU 2
#define KB_MAX_KEYS 64

typedef struct {
    const KeyDef *def;
    int x, y, w, h;
} KeyPlace;

struct Keyboard {
    bool shift_armed;
    bool ctrl_armed;
    int page; /* 0 = letras, 1 = símbolos */
    KeyPlace places[KB_PAGES][KB_MAX_KEYS];
    int counts[KB_PAGES];
};

/* Geometria calculada uma vez: cada linha ocupa a largura inteira (larguras relativas
 * normalizadas pelo total de units da linha — bordas acumuladas via float pra não sobrar
 * nem faltar pixel no fim da linha), e as KB_ROWS linhas dividem a altura igualmente
 * (resto de pixels vai pra última linha). */
static void layout_page(const RowDef rows[KB_ROWS], int width, int height,
                        KeyPlace *places, int *out_count) {
    int row_h = height / KB_ROWS;
    int count = 0;
    for (int r = 0; r < KB_ROWS; r++) {
        int y = r * row_h;
        int h = (r == KB_ROWS - 1) ? height - y : row_h;

        float total_units = 0;
        for (int k = 0; k < rows[r].count; k++) {
            total_units += rows[r].keys[k].units;
        }

        float cum = 0;
        int x = 0;
        for (int k = 0; k < rows[r].count; k++) {
            cum += rows[r].keys[k].units;
            int x_end = (int)(cum * (float)width / total_units + 0.5f);
            places[count].def = &rows[r].keys[k];
            places[count].x = x;
            places[count].y = y;
            places[count].w = x_end - x;
            places[count].h = h;
            count++;
            x = x_end;
        }
    }
    *out_count = count;
}

Keyboard *keyboard_create(int width_px, int height_px) {
    Keyboard *keyboard = calloc(1, sizeof(Keyboard));
    if (!keyboard) {
        return NULL;
    }
    layout_page(kPageLetters, width_px, height_px, keyboard->places[PAGE_LETTERS],
                &keyboard->counts[PAGE_LETTERS]);
    layout_page(kPageSymbols, width_px, height_px, keyboard->places[PAGE_SYMBOLS],
                &keyboard->counts[PAGE_SYMBOLS]);
    layout_page(kPageMenu, width_px, height_px, keyboard->places[PAGE_MENU],
                &keyboard->counts[PAGE_MENU]);
    return keyboard;
}

int keyboard_key_count(const Keyboard *keyboard) {
    return keyboard->counts[keyboard->page];
}

KeyboardKeyView keyboard_key_view(const Keyboard *keyboard, int index) {
    const KeyPlace *place = &keyboard->places[keyboard->page][index];
    /* Chord do menu: com Ctrl+Shift armados, a tecla de página vira a porta do menu —
     * rótulo muda pra "Menu" e ganha destaque, pro recurso ser visível em vez de
     * segredo (mesma engrenagem de rótulo dinâmico do Shift). */
    bool menu_chord = keyboard->shift_armed && keyboard->ctrl_armed &&
                      place->def->type == KEY_PAGE && keyboard->page != PAGE_MENU;

    KeyboardKeyView view = {
        .x = place->x,
        .y = place->y,
        .w = place->w,
        .h = place->h,
        /* Com Shift armado, a tecla mostra o que vai sair de verdade (q→Q, 1→!) — o
         * toggle de Shift já marca visual_changed, então o redraw acontece sozinho. */
        .label = menu_chord ? "Menu"
                 : (keyboard->shift_armed && place->def->shifted_label)
                     ? place->def->shifted_label
                     : place->def->label,
        .highlighted = menu_chord ||
                       (place->def->type == KEY_SHIFT && keyboard->shift_armed) ||
                       (place->def->type == KEY_CTRL && keyboard->ctrl_armed),
    };
    return view;
}

static const KeyPlace *find_key(const Keyboard *keyboard, int x, int y) {
    const KeyPlace *places = keyboard->places[keyboard->page];
    for (int i = 0; i < keyboard->counts[keyboard->page]; i++) {
        if (x >= places[i].x && x < places[i].x + places[i].w && y >= places[i].y &&
            y < places[i].y + places[i].h) {
            return &places[i];
        }
    }
    return NULL;
}

int keyboard_key_index_at(const Keyboard *keyboard, int x, int y) {
    const KeyPlace *place = find_key(keyboard, x, y);
    if (!place) {
        return -1;
    }
    return (int)(place - keyboard->places[keyboard->page]);
}

int keyboard_handle_tap(Keyboard *keyboard, int x, int y,
                        KeyboardEvent out_events[KEYBOARD_MAX_EVENTS],
                        KeyboardAction *out_action, bool *out_visual_changed) {
    *out_action = KEYBOARD_ACTION_NONE;
    *out_visual_changed = false;

    const KeyPlace *place = find_key(keyboard, x, y);
    if (!place) {
        return 0;
    }

    switch (place->def->type) {
    case KEY_SHIFT:
        keyboard->shift_armed = !keyboard->shift_armed;
        *out_visual_changed = true;
        return 0;
    case KEY_CTRL:
        keyboard->ctrl_armed = !keyboard->ctrl_armed;
        *out_visual_changed = true;
        return 0;
    case KEY_PAGE:
        if (keyboard->page == PAGE_MENU) {
            keyboard->page = PAGE_LETTERS; /* "Voltar ao teclado" */
        } else if (keyboard->shift_armed && keyboard->ctrl_armed) {
            /* chord Ctrl+Shift: abre o menu e consome os modificadores */
            keyboard->page = PAGE_MENU;
            keyboard->shift_armed = false;
            keyboard->ctrl_armed = false;
        } else {
            keyboard->page = 1 - keyboard->page; /* letras <-> símbolos */
        }
        *out_visual_changed = true;
        return 0;
    case KEY_ACTION:
        *out_action = place->def->action;
        return 0;
    case KEY_NORMAL:
        break;
    }

    uint32_t keysym = (keyboard->shift_armed && place->def->shifted) ? place->def->shifted
                                                                     : place->def->keysym;
    int n = 0;
    /* Ctrl de verdade precisa do modificador segurado em volta da tecla (diferente do
     * Shift, que o servidor resolve pelo próprio keysym da variante — 'A' já é 'A') —
     * é assim que Ctrl+C vira ETX no terminal remoto. */
    if (keyboard->ctrl_armed) {
        out_events[n++] = (KeyboardEvent){KS_CONTROL_L, true};
    }
    out_events[n++] = (KeyboardEvent){keysym, true};
    out_events[n++] = (KeyboardEvent){keysym, false};
    if (keyboard->ctrl_armed) {
        out_events[n++] = (KeyboardEvent){KS_CONTROL_L, false};
    }

    /* sticky: qualquer tecla normal consome os modificadores armados */
    if (keyboard->shift_armed || keyboard->ctrl_armed) {
        keyboard->shift_armed = false;
        keyboard->ctrl_armed = false;
        *out_visual_changed = true;
    }
    return n;
}

void keyboard_destroy(Keyboard *keyboard) {
    free(keyboard);
}
