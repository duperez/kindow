#ifndef KINDOW_SESSION_H
#define KINDOW_SESSION_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Núcleo da aplicação: o ciclo de vida da sessão VNC — conectar, manter o push de
 * atualizações rodando, reconectar sozinho se a conexão cair (WiFi oscilando, Pi
 * reiniciando), pedir o redimensionamento da tela remota pro tamanho-alvo, e traduzir
 * cliques em PointerEvents válidos.
 *
 * Fronteiras (Ports & Adapters, versão leve): este módulo fala com o protocolo só através
 * de vnc_client.h e usa GLib pelo papel de event loop do processo (g_timeout_add,
 * g_io_add_watch) — mas NÃO conhece GTK, GDK nem Cairo. O que fazer com um frame pronto é
 * decisão do chamador, via callback.
 */

typedef struct Session Session;

typedef struct {
    /* Frame completo pronto (já convertido pra escala de cinza ARGB32, ver vnc_client.h).
     * O buffer só é válido durante a chamada — copiar se precisar guardar. */
    void (*on_frame)(int width, int height, const uint32_t *argb32_pixels, void *user_data);
    /* Uma tentativa de conexão falhou (TCP, handshake ou senha recusada) e a sessão vai
     * re-tentar sozinha em 2s. consecutive_failures conta desde o último sucesso (1 na
     * primeira falha), zerando quando uma conexão vinga — o chamador decide a partir de
     * quantas falhas vale avisar o usuário (ver tela "Conectando...", item 9 de
     * docs/ideias-futuras.md). error é a mensagem da falha (só válida durante a
     * chamada); pode ser NULL. Callback opcional (NULL = ninguém quer saber). */
    void (*on_connect_attempt_failed)(int consecutive_failures, const char *error,
                                      void *user_data);
    void *user_data;
} SessionCallbacks;

/* Cria a sessão SEM conectar a nenhum host ainda (tela de conexão, ver
 * docs/ideias-futuras.md item 5 — o app não assume mais um host fixo desde o boot).
 * target_width/height é o tamanho real da tela local, mesmo papel de antes: alvo do
 * pedido de resize remoto assim que uma conexão trouxer o primeiro frame. Só retorna
 * NULL por falta de memória. */
Session *session_create(int target_width, int target_height, SessionCallbacks callbacks);

/* (Re)conecta a host:port — primeiro derruba qualquer conexão/tentativa em andamento
 * (mesmo efeito de session_disconnect), depois tenta a nova. password: senha de VNC
 * Authentication, ou NULL/"" pra servidor sem senha (ver vnc_client_connect). Como
 * antes, falha de rede não é erro fatal: a sessão fica re-tentando sozinha a cada 2s,
 * agora contra o NOVO host (cada falha reportada via on_connect_attempt_failed). */
void session_connect(Session *session, const char *host, int port, const char *password);

/* Para de tentar conectar e derruba a conexão atual, se houver — a Session continua viva
 * (dá pra chamar session_connect de novo depois), só fica sem host/conexão até isso
 * acontecer. Usado por "Desconectar" no menu e por "Voltar" na tela de conectando. */
void session_disconnect(Session *session);

/* Muda o tamanho-alvo em runtime e pede o resize imediatamente ao servidor (se conectado
 * agora) — diferente do alvo inicial de session_start, que só é aplicado quando o
 * primeiro frame chegar. Usado quando abrir/fechar o painel teclado/menu muda a área
 * útil da tela local (reestrutura de 27/08). Sem conexão agora, só atualiza o alvo — o
 * pedido acontece sozinho quando reconectar e o primeiro frame chegar (mesmo caminho de
 * session_start). */
void session_set_target_size(Session *session, int width, int height);

/* Clique esquerdo (press+release) na posição dada, em coordenadas do frame. Ignorado em
 * silêncio se não há conexão, nenhum frame chegou ainda, ou a posição cai fora do frame
 * atual (coordenadas inválidas não devem chegar ao servidor). */
void session_send_click(Session *session, int x, int y);

/* Scroll (roda do "mouse") na última posição tocada no conteúdo — up=true rola pra cima,
 * false pra baixo. VNC não tem noção de "janela em foco"; o evento carrega coordenada e o
 * X11 entrega pro que estiver sob ela, então reaproveitar a última posição de clique é o
 * que faz o scroll cair na janela certa. Ignorado em silêncio nas mesmas condições do
 * clique (sem conexão ou nenhum frame ainda). Manda `session_get_scroll_lines()` notches
 * de roda (uma catraca = um par press+release do botão 4/5). */
void session_send_scroll(Session *session, bool up);

/* Quantas "catracas" de roda cada session_send_scroll manda — ajustável pelo usuário via
 * menu (etapa 4 da reestrutura de UI, ver docs/ideias-futuras.md). Default 1. Puramente
 * client-side (diferente do zoom remoto, não passa pelo kindow-helperd/Pi). */
int session_get_scroll_lines(const Session *session);
void session_set_scroll_lines(Session *session, int lines);

/* Clique direito imediato (down+release) na última posição tocada no conteúdo — mesma
 * convenção do scroll quanto a alvo (sem foco de janela no VNC, a coordenada é quem
 * manda). Ignorado em silêncio nas mesmas condições do clique. */
void session_send_right_click(Session *session);

/* Um evento de "clique contínuo" (arrasto): held=true mantém o botão 1 pressionado na
 * posição dada — chamar pro press inicial e pra cada posição intermediária durante o
 * arrasto; held=false solta. Diferente de session_send_click (press+release imediato
 * numa chamada só), aqui o CHAMADOR controla o ciclo de vida através de várias chamadas
 * — usado só quando o clique esquerdo sticky foi armado (ver ui.c). Ao contrário do
 * clique normal, coordenada fora dos limites do frame é CLAMPADA, não descartada: um
 * arrasto em andamento precisa terminar com um release de verdade — descartar o release
 * silenciosamente deixaria o botão preso pressionado do lado do servidor. */
void session_send_drag(Session *session, int x, int y, bool held);

/* Evento de tecla (keysym X11; down=true pressiona, false solta). Ignorado em silêncio se
 * não há conexão — diferente do clique, não depende de frame nenhum ter chegado (tecla não
 * carrega coordenada pra validar). */
void session_send_key(Session *session, uint32_t keysym, bool down);

/* Imprime no stderr o estado atual (conectado? tamanho do último frame?) — gatilho de
 * debug, ver o handler de SIGHUP em main.c. */
void session_log_status(const Session *session);

/* Encerra a conexão e libera tudo, inclusive timers/watches pendentes de reconexão. */
void session_shutdown(Session *session);

#endif
