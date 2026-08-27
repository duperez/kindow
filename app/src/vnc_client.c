#include "vnc_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rfb/rfbclient.h>

#include "pixel_convert.h"
#include "timing.h"

struct VncClient {
    rfbClient *rfb;
    bool frame_ready;
    bool got_pixels;
    /* Trava do contrato "vnc_client_start_updates só uma vez por conexão" (ver .h) —
     * documentação sozinha já quase foi violada uma vez (um handler de sinal chamando de
     * novo, pego em review); melhor o próprio módulo recusar do que confiar no comentário. */
    bool updates_started;
    /* Senha pro handshake (VNC Authentication clássica) — "" = sem senha. Guardada aqui
     * porque o GetPassword da lib é um callback síncrono disparado no MEIO de
     * InitialiseRFBConnection: a senha precisa existir antes de conectar, não dá pra
     * perguntar ao usuário na hora (por isso o campo no formulário de conexão, ver ui.c). */
    char password[64];
    /* true se o servidor pediu senha durante o handshake — permite uma mensagem de erro
     * específica ("este servidor exige senha") em vez do genérico "handshake falhou". */
    bool password_requested;
};

/* Qualquer endereço único serve de "tag" pra rfbClientSetClientData/GetClientData. */
static const char kClientDataTag[] = "kindow-vnc-client";

static void set_error(char **out_error, const char *msg) {
    if (out_error) {
        *out_error = strdup(msg);
    }
}

static rfbBool on_malloc_framebuffer(rfbClient *rfb) {
    /* si.framebufferWidth/Height vêm do servidor como uint16_t cada — em alvo de 32 bits
     * (Kindle/ARM) width*height*bpp pode estourar size_t antes do malloc. Mesma proteção que
     * o MallocFrameBuffer default da própria lib faz (vencviewer.c). */
    uint64_t size = (uint64_t)rfb->width * (uint64_t)rfb->height * (rfb->format.bitsPerPixel / 8);
    if (size == 0 || size > SIZE_MAX) {
        return FALSE;
    }

    free(rfb->frameBuffer);
    rfb->frameBuffer = malloc((size_t)size);
    return rfb->frameBuffer != NULL;
}

static void on_finished_update(rfbClient *rfb) {
    VncClient *self = rfbClientGetClientData(rfb, (void *)kClientDataTag);
    if (self) {
        self->frame_ready = true;
    }
}

/* Observado na prática contra o TigerVNC real: a primeira resposta a um
 * FramebufferUpdateRequest não-incremental às vezes vem com um retângulo vazio (0x0) —
 * um "ack" sem conteúdo, antes do frame de verdade chegar num segundo burst. Sem isso,
 * vnc_client_handle_messages consideraria a busca completa cedo demais e devolveria um
 * framebuffer inteiramente zerado (preto). */
/* Chamado pela lib no meio do handshake quando o servidor anuncia VNC Authentication.
 * A lib dá free() no retorno (ver HandleVncAuth em rfbclient.c), por isso o strdup.
 * Retornar NULL aborta a autenticação — é o que acontece quando o servidor exige senha
 * e o usuário não deu nenhuma (password_requested fica marcado pra mensagem de erro
 * específica em vnc_client_connect). O default da lib (prompt no terminal) bloquearia
 * pra sempre no Kindle, onde não há stdin interativo. */
static char *on_get_password(rfbClient *rfb) {
    VncClient *self = rfbClientGetClientData(rfb, (void *)kClientDataTag);
    if (!self) {
        return NULL;
    }
    self->password_requested = true;
    if (!self->password[0]) {
        return NULL;
    }
    return strdup(self->password);
}

static void on_got_update(rfbClient *rfb, int x, int y, int w, int h) {
    (void)x;
    (void)y;
    if (w <= 0 || h <= 0) {
        return;
    }
    VncClient *self = rfbClientGetClientData(rfb, (void *)kClientDataTag);
    if (self) {
        self->got_pixels = true;
    }
}

VncClient *vnc_client_connect(const char *host, int port, const char *password,
                              char **out_error) {
    rfbClient *rfb = rfbGetClient(8, 3, 4);
    if (!rfb) {
        set_error(out_error, "rfbGetClient falhou");
        return NULL;
    }

    /* Revisão (26/08) da decisão original de docs/findings/rfb-protocol.md (que era "só
     * Raw", pela simplicidade): medido no hardware real, Raw fazia um Enter com scroll de
     * tela cheia custar megabytes no WiFi (60 linhas de scroll = 2,2MB medidos no wlan0),
     * congelando o app por 3-5s com o loop do GTK bloqueado lendo o socket. ZRLE (zlib,
     * já presente no build mínimo da lib) comprime tela de texto dezenas de vezes, e
     * CopyRect deixa o servidor transformar scroll em "copie essa região", quase de graça.
     * A ordem da string é a ordem de preferência anunciada. O workaround do burst vazio
     * (on_got_update) continua válido: GotFrameBufferUpdate dispara pra todo retângulo,
     * qualquer que seja o encoding, CopyRect incluso (rfbclient.c:2561). */
    rfb->appData.encodingsString = "zrle copyrect raw";

    VncClient *self = calloc(1, sizeof(VncClient));
    if (!self) {
        rfbClientCleanup(rfb);
        set_error(out_error, "sem memória");
        return NULL;
    }
    self->rfb = rfb;
    snprintf(self->password, sizeof(self->password), "%s", password ? password : "");

    rfb->MallocFrameBuffer = on_malloc_framebuffer;
    rfb->GotFrameBufferUpdate = on_got_update;
    rfb->FinishedFrameBufferUpdate = on_finished_update;
    rfb->GetPassword = on_get_password;
    rfbClientSetClientData(rfb, (void *)kClientDataTag, self);

    if (!ConnectToRFBServer(rfb, host, port)) {
        set_error(out_error, "não foi possível conectar ao servidor VNC");
        rfbClientCleanup(rfb);
        free(self);
        return NULL;
    }

    if (!InitialiseRFBConnection(rfb)) {
        if (self->password_requested) {
            /* "provavelmente": password_requested marca que o servidor PEDIU senha, não
             * que a resposta foi lida — numa janela estreita (rede caindo no meio da
             * leitura do resultado da autenticação), a falha é de rede, não de senha
             * (achado de review, 27/08). A tentativa seguinte do retry esclarece. */
            set_error(out_error, self->password[0]
                                      ? "servidor provavelmente recusou a senha de VNC"
                                      : "este servidor VNC exige senha — preencha o campo "
                                        "Senha no formulário de conexão");
        } else {
            set_error(out_error, "handshake RFB falhou");
        }
        rfbClientCleanup(rfb);
        free(self);
        return NULL;
    }

    rfb->width = rfb->si.framebufferWidth;
    rfb->height = rfb->si.framebufferHeight;

    if (!rfb->MallocFrameBuffer(rfb)) {
        set_error(out_error, "sem memória pro framebuffer");
        rfbClientCleanup(rfb);
        free(self);
        return NULL;
    }

    if (!SetFormatAndEncodings(rfb)) {
        set_error(out_error, "SetFormatAndEncodings falhou");
        free(rfb->frameBuffer);
        rfbClientCleanup(rfb);
        free(self);
        return NULL;
    }

    /* client->updateRect nunca é inicializado por rfbGetClient (fica {0,0,0,0}) — a lib
     * espera que o chamador faça isso (é o que rfbInitConnection, a versão interna que
     * rfbInitClient usa, faz nesse ponto exato). Sem isso, SendIncrementalFramebufferUpdate-
     * Request (chamada automaticamente pela lib depois de cada FramebufferUpdate — ver
     * vnc_client_start_updates) pediria sempre um retângulo 0x0. */
    rfb->updateRect.x = 0;
    rfb->updateRect.y = 0;
    rfb->updateRect.w = rfb->width;
    rfb->updateRect.h = rfb->height;
    rfb->isUpdateRectManagedByLib = TRUE;

    /* Mesma categoria de gap do updateRect acima: rfbGetClient não inicializa
     * endianTest (fica 0 via calloc interno) — só rfbInitClient() faz isso
     * (vncviewer.c: "client->endianTest = 1"), e a gente não passa por ali. Sem isso, as
     * macros rfbClientSwap16/32IfLE (usadas por SendExtDesktopSize, entre outras) nunca
     * trocam a ordem de bytes de verdade — em vez de detectar little-endian, sempre
     * tratam como se já estivesse na ordem de rede, corrompendo qualquer valor de 16/32
     * bits que dependa dessas macros nesse ARM little-endian. */
    rfb->endianTest = 1;

    return self;
}

int vnc_client_get_fd(const VncClient *client) {
    return client->rfb->sock;
}

bool vnc_client_start_updates(VncClient *client, char **out_error) {
    rfbClient *rfb = client->rfb;
    if (client->updates_started) {
        set_error(out_error, "vnc_client_start_updates já foi chamado nessa conexão");
        return false;
    }
    client->updates_started = true;
    client->frame_ready = false;
    client->got_pixels = false;

    if (!SendFramebufferUpdateRequest(rfb, 0, 0, rfb->width, rfb->height, FALSE)) {
        set_error(out_error, "falha ao pedir a primeira atualização de tela");
        return false;
    }
    return true;
}

/* Converte client->frameBuffer (no formato de pixel negociado, seja qual for) pra um
 * buffer ARGB32 em escala de cinza, pronto pro Cairo — decisão de pixel format em
 * docs/findings/rfb-protocol.md. A matemática de conversão em si vive em pixel_convert.c,
 * separada daqui pra ser testável como unidade sem precisar de um rfbClient real. */
static void convert_and_emit(rfbClient *rfb, VncFrameReadyFn on_frame, void *user_data) {
    int width = rfb->width;
    int height = rfb->height;
    rfbPixelFormat *pf = &rfb->format;

    uint32_t *pixels = malloc((size_t)width * (size_t)height * sizeof(uint32_t));
    if (!pixels) {
        return;
    }

    PixelFormat format = {
        .bits_per_pixel = pf->bitsPerPixel,
        .red_shift = pf->redShift,
        .red_max = pf->redMax,
        .green_shift = pf->greenShift,
        .green_max = pf->greenMax,
        .blue_shift = pf->blueShift,
        .blue_max = pf->blueMax,
    };

    struct timespec t0 = timing_now();
    pixel_convert_to_grayscale_argb32(rfb->frameBuffer, width, height, &format, pixels);
    struct timespec t1 = timing_now();
    fprintf(stderr, "kindow: conversão de pixel (%dx%d) levou %ld ms\n", width, height,
            timing_elapsed_ms(t0, t1));

    on_frame(width, height, pixels, user_data);
    free(pixels);
}

bool vnc_client_handle_messages(VncClient *client, VncFrameReadyFn on_frame, void *user_data,
                                 char **out_error) {
    rfbClient *rfb = client->rfb;

    if (!HandleRFBServerMessage(rfb)) {
        set_error(out_error, "erro processando mensagem do servidor");
        return false;
    }

    if (client->frame_ready && !client->got_pixels) {
        /* Burst vazio (Achado #2) — a lib já disparou sozinha o próximo pedido
         * incremental (HandleRFBServerMessage, rfbclient.c ~linha 2564); só falta esperar
         * a resposta de verdade chegar numa próxima leitura do fd. */
        client->frame_ready = false;
        return false;
    }

    if (!(client->frame_ready && client->got_pixels)) {
        return false;
    }

    client->frame_ready = false;
    client->got_pixels = false;
    convert_and_emit(rfb, on_frame, user_data);
    return true;
}

/* Réplica local de rfbClientSwap16IfLE (macro pública em rfbclient.h) — a macro original
 * assume uma variável chamada literalmente "client" do tipo rfbClient* no escopo, o que
 * colide com o nosso próprio parâmetro "client" (do tipo VncClient*, o wrapper deste
 * módulo). Mesma lógica, só parametrizada explicitamente pra evitar a colisão de nomes. */
static uint16_t swap16_if_le(rfbClient *rfb, uint16_t s) {
    if (*(char *)&rfb->endianTest) {
        return (uint16_t)(((s & 0xffu) << 8) | ((s >> 8) & 0xffu));
    }
    return s;
}

bool vnc_client_request_desktop_size(VncClient *client, int width, int height,
                                      char **out_error) {
    if (!client) {
        return false;
    }
    rfbClient *rfb = client->rfb;

    /* Não usa SendExtDesktopSize() da própria lib: ela monta o rfbExtDesktopScreen só
     * preenchendo width/height, deixando id/x/y/flags com lixo de pilha não inicializado —
     * confirmado contra o TigerVNC real, que rejeita com "Invalid screen layout requested
     * by client" quando id vem com valor aleatório em vez de um id de tela válido. Monta a
     * mensagem por conta própria (mesmo formato de wire, mesmas structs/constantes
     * públicas), zerando tudo que a lib deixa passar batido. */
    rfbSetDesktopSizeMsg sdm;
    memset(&sdm, 0, sizeof(sdm));
    sdm.type = rfbSetDesktopSize;
    sdm.width = swap16_if_le(rfb, (uint16_t)width);
    sdm.height = swap16_if_le(rfb, (uint16_t)height);
    sdm.numberOfScreens = 1;

    rfbExtDesktopScreen screen;
    memset(&screen, 0, sizeof(screen));
    /* id=0 é tratado como "tela inválida" pelo código de LEITURA da própria lib
     * (rfbclient.c: "if (screen.id != 0 && screen.width && screen.height)") — o servidor
     * ecoa de volta o id que a gente manda, e se for 0, nosso próprio parser descarta a
     * resposta como invalidScreen, pulando ResizeClientBuffer (client->width/height nunca
     * atualiza, mesmo o servidor aplicando o resize de verdade do lado dele). Precisa ser
     * um id não-zero — 1 é a convenção padrão pra tela única; a lib só checa "diferente de
     * zero", não compara valor específico, então não precisa de swap de byte-order aqui. */
    screen.id = 1;
    screen.width = swap16_if_le(rfb, (uint16_t)width);
    screen.height = swap16_if_le(rfb, (uint16_t)height);

    fprintf(stderr, "kindow: pedindo redimensionamento da tela remota pra %dx%d\n", width,
            height);
    if (!WriteToRFBServer(rfb, (char *)&sdm, sz_rfbSetDesktopSizeMsg) ||
        !WriteToRFBServer(rfb, (char *)&screen, sz_rfbExtDesktopScreen)) {
        set_error(out_error, "falha ao pedir redimensionamento de tela");
        return false;
    }

    /* De propósito, não atualiza rfb->screen aqui: o parser da própria lib sobrescreve
     * client->screen quando a resposta do servidor chega (rfbclient.c ~linha 2166) — a
     * resposta ecoada é a fonte de verdade, não o que a gente pediu. */

    /* Testado ao vivo contra o TigerVNC real: o SetDesktopSize sozinho não basta — o
     * servidor aplica o resize (confirmado via xrandr), mas não empurra nada de volta
     * sozinho depois. O pedido incremental que já estava em andamento antes disso era pra
     * área antiga (que deixou de existir), e o servidor não reage a ele pra área nova —
     * fica esperando um pedido novo, explícito, na área nova. Como as duas mensagens vão
     * na mesma conexão TCP, o servidor processa em ordem: quando ler este pedido, o resize
     * de cima já foi aplicado (diferente da tentativa anterior, que mandava isso ANTES do
     * resize ser aceito, e por isso batia "exceeds framebuffer"). */
    if (!SendFramebufferUpdateRequest(rfb, 0, 0, width, height, FALSE)) {
        set_error(out_error, "falha ao pedir atualização depois do redimensionamento");
        return false;
    }

    return true;
}

void vnc_client_send_pointer(VncClient *client, int x, int y, int button_mask) {
    if (!client) {
        return;
    }
    if (!SendPointerEvent(client->rfb, x, y, button_mask)) {
        /* write() falhou — a conexão provavelmente caiu. Não propaga erro daqui (a
         * assinatura é void, decisão de manter a chamada simples pro chamador); o watch de
         * G_IO_HUP/G_IO_ERR no fd (main.c) ainda vai pegar a queda de conexão sozinho. */
        fprintf(stderr, "kindow: falha ao mandar PointerEvent, conexão provavelmente caiu\n");
    }
}

void vnc_client_send_key(VncClient *client, uint32_t keysym, bool down) {
    if (!client) {
        return;
    }
    if (!SendKeyEvent(client->rfb, keysym, down ? TRUE : FALSE)) {
        fprintf(stderr, "kindow: falha ao mandar KeyEvent, conexão provavelmente caiu\n");
    }
}

void vnc_client_disconnect(VncClient *client) {
    if (!client) {
        return;
    }
    free(client->rfb->frameBuffer);
    client->rfb->frameBuffer = NULL;
    rfbClientCleanup(client->rfb);
    free(client);
}
