#include "strings.h"

static UiLanguage g_language = LANG_EN;

/* Indexadas por StringId — manter na MESMA ordem do enum (o teste unitário confere que
 * nenhuma entrada ficou vazia nas duas línguas, o que pega tabela desalinhada). */
static const char *const kEnglish[STR_COUNT] = {
    /* barra */
    [STR_BAR_KEYBOARD] = "Keyboard",
    [STR_BAR_MENU] = "Menu",
    [STR_BAR_QUIT] = "Quit",
    /* menu */
    [STR_MENU_APPS_OUT] = "Apps  A-",
    [STR_MENU_APPS_IN] = "Apps  A+",
    [STR_MENU_DECO_OUT] = "Window  A-",
    [STR_MENU_DECO_IN] = "Window  A+",
    [STR_MENU_PANEL_OUT] = "Panel  A-",
    [STR_MENU_PANEL_IN] = "Panel  A+",
    [STR_MENU_SCROLL_OUT] = "Scroll  A-",
    [STR_MENU_SCROLL_IN] = "Scroll  A+",
    [STR_MENU_DISCONNECT] = "Disconnect from Pi",
    [STR_MENU_STATUS] = "Connection status (log)",
    [STR_MENU_QUIT_APP] = "Quit Kindow",
    /* teclado */
    [STR_KEY_LEFT_CLICK] = "Left",
    [STR_KEY_RIGHT_CLICK] = "Right",
    /* tela de conexão */
    [STR_FORM_IP] = "IP: ",
    [STR_FORM_PORT] = "Port: ",
    [STR_FORM_PASSWORD] = "Password: ",
    [STR_FORM_CONNECT] = "Connect",
    [STR_FORM_CANCEL] = "Cancel",
    [STR_BACK] = "Back",
    [STR_CONNECTING] = "Connecting to %s:%d...",
    [STR_CONNECT_FAILED] = "Could not connect to %s:%d",
    [STR_CHECK_FIELDS] = "check the IP, port and password",
    /* erros do handshake VNC */
    [STR_ERR_NEEDS_PASSWORD] = ("this VNC server requires a password — fill in the "
                                "Password field on the connection form"),
    [STR_ERR_PASSWORD_REFUSED] = "the server most likely refused the VNC password",
    [STR_ERR_CONNECT_FAILED] = "could not connect to the VNC server",
    [STR_ERR_HANDSHAKE_FAILED] = "RFB handshake failed",
    [STR_ERR_OUT_OF_MEMORY] = "out of memory",
    [STR_ERR_INIT_FAILED] = "VNC client initialization failed",
    [STR_ERR_SESSION_SETUP_FAILED] = "failed to configure the VNC session",
    [STR_ERR_UPDATES_ALREADY_STARTED] = "internal error: updates already started",
    [STR_ERR_FIRST_UPDATE_FAILED] = "failed to request the first screen update",
};

static const char *const kPortuguese[STR_COUNT] = {
    /* barra */
    [STR_BAR_KEYBOARD] = "Teclado",
    [STR_BAR_MENU] = "Menu",
    [STR_BAR_QUIT] = "Sair",
    /* menu */
    [STR_MENU_APPS_OUT] = "Apps  A-",
    [STR_MENU_APPS_IN] = "Apps  A+",
    [STR_MENU_DECO_OUT] = "Janela  A-",
    [STR_MENU_DECO_IN] = "Janela  A+",
    [STR_MENU_PANEL_OUT] = "Painel  A-",
    [STR_MENU_PANEL_IN] = "Painel  A+",
    [STR_MENU_SCROLL_OUT] = "Scroll  A-",
    [STR_MENU_SCROLL_IN] = "Scroll  A+",
    [STR_MENU_DISCONNECT] = "Desconectar do Pi",
    [STR_MENU_STATUS] = "Status da conexão (log)",
    [STR_MENU_QUIT_APP] = "Sair do Kindow",
    /* teclado */
    [STR_KEY_LEFT_CLICK] = "Esquerdo",
    [STR_KEY_RIGHT_CLICK] = "Direito",
    /* tela de conexão */
    [STR_FORM_IP] = "IP: ",
    [STR_FORM_PORT] = "Porta: ",
    [STR_FORM_PASSWORD] = "Senha: ",
    [STR_FORM_CONNECT] = "Conectar",
    [STR_FORM_CANCEL] = "Cancelar",
    [STR_BACK] = "Voltar",
    [STR_CONNECTING] = "Conectando a %s:%d...",
    [STR_CONNECT_FAILED] = "Não foi possível conectar a %s:%d",
    [STR_CHECK_FIELDS] = "verifique o IP, a porta e a senha",
    /* erros do handshake VNC */
    [STR_ERR_NEEDS_PASSWORD] = ("este servidor VNC exige senha — preencha o campo "
                                "Senha no formulário de conexão"),
    [STR_ERR_PASSWORD_REFUSED] = "servidor provavelmente recusou a senha de VNC",
    [STR_ERR_CONNECT_FAILED] = "não foi possível conectar ao servidor VNC",
    [STR_ERR_HANDSHAKE_FAILED] = "handshake RFB falhou",
    [STR_ERR_OUT_OF_MEMORY] = "sem memória",
    [STR_ERR_INIT_FAILED] = "falha ao inicializar o cliente VNC",
    [STR_ERR_SESSION_SETUP_FAILED] = "falha ao configurar a sessão VNC",
    [STR_ERR_UPDATES_ALREADY_STARTED] = "erro interno: atualizações já iniciadas",
    [STR_ERR_FIRST_UPDATE_FAILED] = "falha ao pedir a primeira atualização de tela",
};

void strings_set_language(UiLanguage lang) {
    g_language = lang;
}

UiLanguage strings_language(void) {
    return g_language;
}

const char *tr(StringId id) {
    if (id < 0 || id >= STR_COUNT) {
        return "";
    }
    const char *s =
        (g_language == LANG_PT) ? kPortuguese[id] : kEnglish[id];
    return s ? s : "";
}
