#include "vnc_client.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rfb/rfbclient.h>

#include "pixel_convert.h"

/* Deadline total pra vnc_client_fetch_frame — evita travar o app pra sempre se o Pi
 * ficar inalcançável no meio de uma interação (WiFi do Kindle caindo, etc). */
#define FETCH_TIMEOUT_USEC (5 * 1000 * 1000)
#define POLL_SLICE_USEC (200 * 1000)

struct VncClient {
    rfbClient *rfb;
    bool frame_ready;
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

VncClient *vnc_client_connect(const char *host, int port, char **out_error) {
    rfbClient *rfb = rfbGetClient(8, 3, 4);
    if (!rfb) {
        set_error(out_error, "rfbGetClient falhou");
        return NULL;
    }

    VncClient *self = calloc(1, sizeof(VncClient));
    if (!self) {
        rfbClientCleanup(rfb);
        set_error(out_error, "sem memória");
        return NULL;
    }
    self->rfb = rfb;

    rfb->MallocFrameBuffer = on_malloc_framebuffer;
    rfb->FinishedFrameBufferUpdate = on_finished_update;
    rfbClientSetClientData(rfb, (void *)kClientDataTag, self);

    if (!ConnectToRFBServer(rfb, host, port)) {
        set_error(out_error, "não foi possível conectar ao servidor VNC");
        rfbClientCleanup(rfb);
        free(self);
        return NULL;
    }

    if (!InitialiseRFBConnection(rfb)) {
        set_error(out_error, "handshake RFB falhou");
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

    return self;
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
    pixel_convert_to_grayscale_argb32(rfb->frameBuffer, width, height, &format, pixels);

    on_frame(width, height, pixels, user_data);
    free(pixels);
}

bool vnc_client_fetch_frame(VncClient *client, VncFrameReadyFn on_frame, void *user_data,
                             char **out_error) {
    rfbClient *rfb = client->rfb;
    client->frame_ready = false;

    if (!SendFramebufferUpdateRequest(rfb, 0, 0, rfb->width, rfb->height, FALSE)) {
        set_error(out_error, "falha ao pedir atualização de tela");
        return false;
    }

    int waited_usec = 0;
    while (!client->frame_ready) {
        int n = WaitForMessage(rfb, POLL_SLICE_USEC);
        if (n < 0) {
            set_error(out_error, "conexão perdida esperando atualização de tela");
            return false;
        }
        if (n == 0) {
            waited_usec += POLL_SLICE_USEC;
            if (waited_usec >= FETCH_TIMEOUT_USEC) {
                set_error(out_error, "tempo esgotado esperando atualização de tela");
                return false;
            }
            continue;
        }
        if (!HandleRFBServerMessage(rfb)) {
            set_error(out_error, "erro processando mensagem do servidor");
            return false;
        }
    }

    convert_and_emit(rfb, on_frame, user_data);
    return true;
}

void vnc_client_send_pointer(VncClient *client, int x, int y, int button_mask) {
    if (!client) {
        return;
    }
    SendPointerEvent(client->rfb, x, y, button_mask);
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
