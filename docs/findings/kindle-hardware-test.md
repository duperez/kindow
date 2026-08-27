# Teste ponta a ponta no hardware real — Kindle + Pi

## Resultado: funcionou

Confirmado no device físico (Kindle jailbreakado ↔ Raspberry Pi via WiFi doméstico): texto
digitado no `xterm` do Pi apareceu no Kindle, e um toque na tela do Kindle mandou um clique real
que deu foco no terminal do Pi (cursor de texto ficou sólido/visível — o comportamento normal do
`xterm` ao ganhar foco). As duas metades da PoC — mostrar a tela remota e mandar entrada de volta
— estão provadas funcionando de ponta a ponta, não só em teoria. (Na sessão inicial isso dependia
de um refresh manual via botão/`SIGHUP`; desde a sessão de 26/08 — ver seção abaixo — a
atualização chega sozinha por push automático da conexão persistente.)

Chegar até aqui exigiu encontrar e corrigir vários bugs reais que nenhuma revisão estática nem
teste unitário pegaria — só apareceram testando contra o TigerVNC de verdade. Registrado aqui pra
não se perder.

## Revisão das 8 correções desta sessão — confirmada; o bug real que sobrou foi resolvido depois, por remoção

Depois de fechar os bugs #1-#5 abaixo, um `reviewer` conferiu o diff inteiro linha a linha contra
o código-fonte vendorizado da lib (não só contra a explicação dada) — as 8 correções (`updateRect`,
`encodingsString="raw"`, `got_pixels`/burst vazio, overflow guard em `MallocFrameBuffer`,
`show_error` sem diálogo modal, esquema de título de janela, clamp de clique, gatilho `SIGHUP`)
batem com o comportamento real da lib, sem leak/race/contradição com decisões já documentadas.
Confirmado também: o "segundo burst chega sozinho" não é o servidor decidindo mandar mais nada —
é a própria lib chamando `SendIncrementalFramebufferUpdateRequest()` internamente depois de
processar qualquer `FramebufferUpdate` (mesmo o vazio), usando o `updateRect` que só passou a
estar correto depois da correção #1. Comentário no código ajustado pra deixar isso explícito.

**Bug real, resolvido — por remoção, não por correção da hierarquia de widgets**: o revisor
calculou que aumentar `drawing_area` pra `1072x1448` (mesma resolução da tela cheia) —
empacotado no mesmo `GtkVBox` que o botão "Atualizar" logo abaixo — podia empurrar o botão pra
fora da área alocada de verdade, já que o `vbox` inteiro (drawing_area + botão + espaçamento)
passava a pedir mais altura do que a tela tem. **Confirmado no device**: o botão continuava
*visível*, mas **não era mais possível tocar nele** — bateu com a hipótese do revisor (área
clicável do botão desalinhada/fora do alcance de toque real, mesmo aparecendo na imagem).

Na sessão de hoje (26/08) o modelo de conexão passou de "reconecta a cada interação" pra
persistente com push automático da própria lib (ver seção "Resize automático..." abaixo e a
revisão registrada em [`rfb-protocol.md`](rfb-protocol.md)) — com isso, o botão "Atualizar"
perdeu a função (o próximo frame chega sozinho quando o conteúdo muda, sem precisar de gatilho
manual nenhum). **O botão foi removido completamente**, não só reposicionado — o bug morreu
junto, resolvido por remoção: `GtkDrawingArea` agora é filho único direto da janela, sem `vbox`
nenhum (confirmado por `ui-reviewer`). O gatilho `SIGHUP` continua existindo, mas mudou de
função: como chamar `vnc_client_start_updates()` duas vezes na mesma conexão violaria o contrato
da API (deixaria dois pedidos incrementais pendentes no servidor), `SIGHUP` hoje só imprime o
status da conexão no log, não força mais refresh.

## Bug real #1 (o principal): `client->updateRect` nunca inicializado

**Sintoma**: o app conectava, autenticava, negociava formato — tudo certo — mas o framebuffer
sempre chegava zerado (tela preta/branca sólida, sem nenhum pixel do conteúdo real).

**Causa raiz**: `rfbGetClient()` não inicializa `client->updateRect` (fica `{0,0,0,0}`, herdado
do `calloc` interno). A função `SendIncrementalFramebufferUpdateRequest()` usa esse campo
internamente:
```c
// vendor/libvncserver/src/libvncclient/rfbclient.c
rfbBool SendIncrementalFramebufferUpdateRequest(rfbClient* client) {
    return SendFramebufferUpdateRequest(client,
            client->updateRect.x, client->updateRect.y,
            client->updateRect.w, client->updateRect.h, TRUE);
}
```
Sem inicializar, isso pede um retângulo `0x0` — um pedido efetivamente vazio. O caminho de
referência da própria lib (`rfbInitConnection`, função interna que `rfbInitClient()` usa) faz essa
inicialização automaticamente logo depois do `SetFormatAndEncodings`:
```c
if (client->updateRect.x < 0) {
    client->updateRect.x = client->updateRect.y = 0;
    client->updateRect.w = client->width;
    client->updateRect.h = client->height;
    client->isUpdateRectManagedByLib = TRUE;
}
```
Como o `kindow` usa `ConnectToRFBServer`+`InitialiseRFBConnection` diretamente (decisão registrada
em `libvncclient-api.md`, pra não depender de `argv`), essa inicialização nunca foi replicada —
gap real entre "seguir o exemplo oficial de perto" e "copiar a sequência completa".

**Correção**: em `vnc_client_connect()` (`app/src/vnc_client.c`), logo depois do
`SetFormatAndEncodings`, setar `updateRect` pro tamanho cheio da tela e
`isUpdateRectManagedByLib = TRUE`, espelhando o trecho acima.

**Lição pra próxima vez que usar essa API "à mão"**: qualquer campo que o exemplo/caminho de
referência da lib toca merece replicar, mesmo que pareça incidental — `updateRect` não aparece em
nenhum lugar óbvio da documentação da API pública como "obrigatório".

## Achado #2: TigerVNC manda um primeiro burst vazio, sempre

Confirmado empiricamente, de forma 100% reproduzível (múltiplas conexões novas seguidas, sem
nenhuma mudança de tela entre elas): toda conexão nova recebe primeiro um
`FramebufferUpdate` com um retângulo `0x0` (nada), e só ~1-2 segundos depois vem um segundo burst
com o conteúdo real, de forma automática — **sem precisar pedir de novo**. Tentei "insistir"
mandando `SendIncrementalFramebufferUpdateRequest` no meio disso — não ajudou (e não é preciso,
já que o bug real era o #1 acima). A causa provável é algum comportamento interno de
inicialização/composição do `Xvnc` que não é instantâneo — não investigado a fundo, só confirmado
que existe e que só esperar passivamente resolve.

**Implicação pro código**: `vnc_client_fetch_frame()` não pode considerar a busca completa no
primeiro `FinishedFrameBufferUpdate` — precisa esperar até um retângulo com conteúdo real
(`w>0 && h>0`) realmente chegar, daí sim aceitar o próximo `FinishedFrameBufferUpdate`. Implementado
via um campo `got_pixels` na struct `VncClient`, setado pelo callback `GotFrameBufferUpdate`
(que antes não era usado, só `FinishedFrameBufferUpdate`).

## Achado #3: decisão de encoding "só Raw" nunca tinha sido implementada

`docs/findings/rfb-protocol.md` já documentava a decisão de usar só `Raw`, mas o código nunca
setava `client->appData.encodingsString` — o padrão da lib (`"tight zrle ultra copyrect hextile
zlib corre rre raw"`) ficava valendo, e como o build não desabilita zlib, o servidor podia escolher
`zrle` em vez de `Raw`. Não era a causa do bug #1 (confirmado testando com `raw` forçado e o bug
persistindo), mas é uma correção real pra bater com a decisão já tomada — `vnc_client_connect()`
agora seta `rfb->appData.encodingsString = "raw"` logo depois do `rfbGetClient`.

## Achado #4: diálogo de erro do GTK trava o app nesse Kindle

`gtk_message_dialog_new()` tenta carregar ícones de tema padrão (`gtk-ok`, `gtk-dialog-error`) que
não existem nesse ambiente mínimo — a falha em carregar produz uma cascata de
`GLib`/`Gtk-CRITICAL` e o diálogo modal nem renderiza nem fecha, travando o app inteiro assim que
qualquer erro acontece (mesmo um erro besta de rede). Trocado por `g_printerr()` simples em
`show_error()` — sem dependência de tema de ícone, e mais fácil de depurar via log de qualquer
forma.

## Achado #5: janela só aparece em tela cheia com título num formato específico

O window manager do Kindle (Awesome WM) só mapeia/exibe em tela cheia janelas cujo título segue um
esquema key-value específico (`L:A_N:application_ID:<reverse-domain>_PC:N`) — sem isso a janela
fica como um "stub" `10x10` nunca mapeado, mesmo com o processo rodando normalmente sem erro
nenhum. Descoberto comparando com o `pet_dashboard` do projeto irmão `kindle`, que já resolve isso
(`gtk_window_set_title(window, "L:A_N:application_ID:com.eduardo.petdashboard_PC:N")`). Detalhe
completo do esquema em
`../../../kindle/docs/findings/waf-path-dead-end.md` (seção "Achado complementar"). Resolução real
da tela confirmada via `xwininfo -root`: `1072x1448`.

## Sessão de 26/08: conexão persistente, resize automático de tela, latência medida

Resultado: funcionou ponta a ponta. O cliente detecta a resolução real do Kindle em runtime
(`gdk_screen_width`/`gdk_screen_height` — mesmo binário serve qualquer modelo) e pede ao servidor
pra redimensionar a área remota via extensão RFB `SetDesktopSize`
(`vnc_client_request_desktop_size()`, uma vez por conexão); o TigerVNC aplica via Xrandr. O frame
chega exatamente no tamanho da tela do Kindle (`1072x1448` nesse device), 1:1, sem escala no
cliente. A investigação encontrou **três bugs reais na `libvncclient` vendorizada (0.9.15)**,
todos contornados no nosso código (`vnc_client.c`), todos confirmados empiricamente e por
`reviewer` contra o código-fonte da lib — mesma categoria dos achados #1-#5 acima, só que na área
de resize em vez de framebuffer inicial.

**Ferramenta que destravou a investigação**: scripts Python de sonda RFB crua, rodando no próprio
Pi — handshake feito na mão (sem a lib) + hexdump da resposta bruta do servidor. Provaram que o
servidor e o protocolo estavam corretos e que o bug era do lado cliente, evitando horas de suspeita
errada em cima do TigerVNC/xrandr.

### Achado #6: `endianTest` nunca inicializado no caminho `ConnectToRFBServer`+`InitialiseRFBConnection`

Só `rfbInitClient()` seta `client->endianTest = 1`; como o `kindow` não passa por essa função
(mesmo motivo do achado #1 — decisão registrada em `libvncclient-api.md` de não depender de
`argv`), `endianTest` ficava em `0` (herdado do `calloc`). Com isso as macros
`rfbClientSwap16IfLE`/`rfbClientSwap32IfLE` viravam no-op mesmo em little-endian, corrompendo
qualquer mensagem que dependesse delas — categoria de bug idêntica ao `updateRect` do achado #1:
campo que o caminho de referência da lib toca de passagem, não documentado como obrigatório.
Corrigido setando `endianTest = 1` manualmente logo depois do handshake.

### Achado #7: `SendExtDesktopSize()` da própria lib monta a mensagem com campos incompletos

A função da lib monta o `rfbExtDesktopScreen` só preenchendo `width`/`height`, deixando
`id`/`x`/`y`/`flags` com lixo de pilha. O TigerVNC rejeita a mensagem com "Invalid screen layout
requested by client". **Contorno**: montar as duas mensagens (`SetDesktopSize` +
`rfbExtDesktopScreen`) na mão em `vnc_client.c`, usando as structs públicas de `rfbproto.h` +
`WriteToRFBServer`, com `memset` e todos os campos explícitos — não usar `SendExtDesktopSize()` da
lib.

### Achado #8: parser de resposta descarta silenciosamente `screen.id == 0`

Sintoma enganoso que custou horas: o resize *funcionava* no servidor (`xrandr` no Pi confirmava a
mudança de resolução), mas o cliente nunca atualizava `client->width`/`client->height` — parecia
trava de protocolo/timing. Causa real: o parser de resposta da própria lib
(`rfbclient.c`, ~linha 2165) descarta como "tela inválida" qualquer screen com `id == 0`, e o
servidor simplesmente ecoa de volta o `id` que o cliente mandou. Mandando `id=0` (valor natural de
"não pensei nisso"), o servidor aceitava e aplicava, mas a resposta era descartada em silêncio no
cliente. **Contorno**: usar `screen.id = 1` no pedido — a lib só compara esse campo com zero,
nunca interpreta o valor, então nem precisa de swap de endianness nele.

### Pedido não-incremental obrigatório depois do resize

Além dos três bugs da lib: depois do `SetDesktopSize` é preciso mandar um
`SendFramebufferUpdateRequest` **não-incremental** pro tamanho novo — o pedido incremental que já
estava pendente era da área antiga, e sem um pedido novo o servidor não manda mais nada pra área
redimensionada. TCP garante a ordem (o resize já foi processado pelo servidor quando ele lê esse
pedido), então não há condição de corrida a tratar.

### Latência medida no hardware real

Medido com `clock_gettime` em volta de cada etapa, logs no próprio código (não estimativa):

| Etapa | Antes | Depois (com LUT) |
|---|---|---|
| Conversão de pixel (frame 1024x758) | ~164ms | **~118ms** |
| Conversão de pixel (frame 1072x1448) | — | ~170-180ms |
| Cópia pro `cairo_surface` | — | 6-14ms |
| `cairo_paint` | — | 70-165ms (conforme o tamanho) |

Otimização: LUTs de 256 entradas por canal (`red_max`/`green_max`/`blue_max` são fixos por
conexão; divisão inteira é cara no Cortex-A9 desse Kindle, que não tem hardware de divisão) +
loops especializados por `bpp` em `pixel_convert.c` (o `switch` de bpp saiu de dentro do loop
quente, decidido uma vez por chamada em vez de uma vez por pixel). Saída bit-a-bit idêntica à
versão anterior — os 10 testes unitários de `pixel_convert.c` continuam passando sem alteração.

O piso físico do refresh do e-ink (~400-600ms pra um refresh `GC16` completo) não é otimizável por
software nenhum — é a expectativa calibrada pra qualquer trabalho futuro nessa área: a conversão
de pixel e o desenho no Cairo já são uma fração pequena do tempo total percebido pelo usuário.

### `preventScreenSaver` integrado ao ciclo de vida do app

O Kindle dormia com o Kindow aberto — quem segurava a trava contra o screensaver antes era o
`pet_dashboard` (projeto irmão `kindle`), que solta a trava por design quando outro app fica por
cima (ver
[`../../../kindle/docs/findings/screensaver-app-lifecycle.md`](../../../kindle/docs/findings/screensaver-app-lifecycle.md)).
Corrigido replicando o mesmo padrão no `kindow-client`: `lipc-set-prop com.lab126.powerd
preventScreenSaver=1` no início, `=0` em todo caminho de saída alcançável (destruição da janela,
`SIGTERM` — o sinal que o `kill` do processo de deploy manda —, e `SIGINT` via
`g_unix_signal_add_watch_full`). Desde o refactor Ports & Adapters leve de 26/08, o comando
`lipc-set-prop` em si fica isolado em `kindle_platform_keep_awake()`
([`app/src/kindle_platform.c`](../../app/src/kindle_platform.c)); `main.c` só chama essa função
no início e depois de `gtk_main()` retornar, cobrindo os mesmos caminhos de saída de antes.
`SIGKILL` continua sendo o único caminho que deixa a trava presa
até reboot (nenhum handler de sinal sobrevive a `SIGKILL`, por definição). Testado nos dois
sentidos ao vivo no device (screensaver não dispara com o app aberto; volta a disparar depois que
o app fecha normalmente).

## Ferramenta de debug: `SIGHUP`

Originalmente `main.c` escutava `SIGHUP` (`g_unix_signal_add_watch_full`) pra disparar o mesmo
`refresh_frame()` que o botão "Atualizar" chamava — `kill -HUP <pid>` via SSH tinha o mesmo efeito
de tocar no botão, sem precisar acertar a tela fisicamente. **Mudou no mesmo dia (26/08)**: com o
modelo de conexão persistente, chamar `vnc_client_start_updates()` de novo na mesma conexão
violaria o contrato da API (dois pedidos incrementais pendentes no servidor) — então `SIGHUP` não
força mais refresh, só imprime o status da conexão no log. `main.c` continua registrando o
handler do sinal (é wiring), mas quem sabe o que logar é `session_log_status()`
([`app/src/session.c`](../../app/src/session.c), depois do refactor Ports & Adapters leve do
mesmo dia — antes a lógica de status vivia dentro do próprio handler em `main.c`). Continua útil
como ferramenta de debug (confirmar que o app está vivo e conectado sem precisar tocar a tela),
só que com escopo menor que antes.
**`SIGUSR1` não funciona** nesse sysroot específico — a versão de `glib` vendorizada só aceita
`SIGHUP`/`SIGINT`/`SIGTERM` em `g_unix_signal_add_watch_full` (confirmado por erro em runtime:
`assertion 'signum == SIGHUP || signum == SIGINT || signum == SIGTERM' failed`). É uma ferramenta
de dev, não faz parte do fluxo de uso real do app.

## Mecânica de deploy no Kindle (pra próxima vez)

- **IP do Kindle fixado em `192.168.0.74`** (26/08) — não é reserva DHCP no roteador, é edição
  direta de `/usr/share/udhcpc/default.script` no próprio Kindle (`mntroot rw`/`ro`, backup
  `.orig` mantido no device); o script tem checagem de ping antes de aplicar, pra não roubar um
  IP já em uso por outro device. Motivo: o Kindle mudou de IP depois de um reboot durante a
  sessão de hoje, o que atrapalhava o deploy — antes disso o IP não era fixo.
- Binário + `libvncclient.so.1` (renomeado a partir do `.so.0.9.15` real) vão em `/mnt/us/kindow/`
  — a partição raiz (`/`) só tem ~19MB livres, não cabe nada ali. `/mnt/us` é a partição de
  armazenamento do usuário (13GB livres), mesmo padrão do `pet_dashboard`.
- `/mnt/us` parece ser FAT — não dá pra sobrescrever um binário em execução nele (`scp` falha com
  "Failure" genérico); é preciso matar o processo antigo antes de reenviar.
- `LD_LIBRARY_PATH=/mnt/us/kindow` na hora de rodar, já que `libvncclient` não faz parte do
  firmware do Kindle.
- Rodar com `nohup ... < /dev/null &` — sem isso, o processo em background morre junto com a
  sessão SSH assim que ela fecha (SIGHUP na desconexão, sem handler pra sobreviver a isso — no
  sentido "matar", diferente do SIGHUP que a gente escuta de propósito pra outra coisa).
- `DISPLAY=:0` é o display X real do Kindle (onde o `awesome`/apps do sistema já rodam).

## Verificação (majoritariamente), não medição — exceto onde marcado

Este documento registra sobretudo o que foi observado e corrigido nas sessões de teste — resolução
do device, comportamento do TigerVNC, mecânica de deploy. A seção "Latência medida no hardware
real" (26/08) é a exceção deliberada: números de `clock_gettime` de verdade, não estimativa. O
resto segue confirmado/válido até prova em contrário (ex: em outro modelo de Kindle, ou versão
diferente do TigerVNC).
