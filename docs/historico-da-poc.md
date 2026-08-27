# Histórico da PoC (diário de bordo)

Este arquivo preserva o diário cronológico da prova de conceito — o conteúdo que foi a
porta de entrada do repositório até 27/08/2026, quando o README foi reestruturado pra
servir a quem chega de fora (o que é, como instala, como usa). Nada aqui foi reescrito:
é o registro fiel de como as decisões e entregas aconteceram, na ordem em que
aconteceram. Pra o estado ATUAL do projeto, ver o [README](../README.md); pra achados
técnicos pontuais, [`findings/`](findings/); pra fila do que vem depois,
[`ideias-futuras.md`](ideias-futuras.md).

Nota pra quem lê fora da máquina original: os links pro "projeto irmão `kindle`"
(`../../kindle/...`) apontam pra um repositório local privado que não acompanha este —
ficam quebrados aqui de propósito, preservados só como referência de onde certos achados
vieram.

## Contexto

Nasceu de uma investigação dentro do projeto [`kindle`](../../kindle/) (o painel/dashboard que
roda no mesmo device) — ao pesquisar como o Kindle poderia servir de tela ocasional pro Pi,
avaliamos alguns projetos de VNC pra Kindle já existentes e nenhum se encaixava bem: uns não
eram VNC interativo de verdade (só empurravam imagem estática), outros miravam hardware/API
antiga (`einkfb`, não `mxcfb` — a API que esse Kindle usa), e o mais sério tecnicamente
(`kindlevncviewer`) exigiria portar a camada de desenho pra API certa sem garantia de qualidade
do código original.

Virou projeto próprio porque deixou de ser exploração dentro do diário de bordo do `kindle` e
passou a ter escopo e decisões de arquitetura já estabelecidas — não faz mais sentido misturar
com aquele repositório.

## O que já decidimos

1. **Escopo mínimo da PoC**: no Pi, um servidor VNC maduro (`x11vnc` ou `TigerVNC`) expondo
   algo simples (nem precisa ser um desktop completo — um terminal já prova o conceito). No
   Kindle, um app GTK novo que conecta via TCP, faz o handshake RFB, recebe a tela e desenha
   via Cairo numa janela normal. Toque vira `PointerEvent` mandado de volta pro servidor.
   Atualização de tela **sob demanda** — o servidor só manda `FramebufferUpdate` quando o
   conteúdo muda de verdade (push do protocolo, não polling), nunca em loop contínuo — decisão
   de design pro hardware, não limitação da PoC. **Revisão**: originalmente isso era acionado
   por um botão "Atualizar" explícito no app; hoje a conexão é persistente e o pedido
   incremental fica sempre em andamento sozinho — ver "Próximos passos" e `rfb-protocol.md`.
2. **Não portar um projeto VNC existente.** Decisão deliberada: em vez de adaptar um dos
   projetos encontrados na pesquisa (qualidade/manutenção incertas), usar uma biblioteca madura
   só pra parte de protocolo — `libvncclient` (projeto LibVNCServer, ativo, 2277+ commits) — e
   escrever nós mesmos a camada de integração com o Kindle (Cairo, toque), que é a parte
   realmente específica desse hardware.
3. **Não precisa bypassar GTK/X11.** Diferente do que o KOReader faz pra desenhar direto no
   framebuffer (`mxcfb`, contornando X11 inteiramente — investigado no projeto `kindle`, ver
   [`../kindle/docs/findings/landscape-orientation-blocked.md`](../../kindle/docs/findings/landscape-orientation-blocked.md)),
   um cliente VNC só precisa desenhar pixels recebidos numa janela — isso o Cairo/GTK já fazem
   nativamente. Reaproveita o toolchain de cross-compilation já validado no `kindle`
   (Koxtoolchain + KMC SDK via Docker).
4. **Princípio de isolamento**: todo código que fala diretamente com `libvncclient` fica isolado
   num módulo próprio, com uma interface pequena e específica pro que a PoC precisa — não uma
   arquitetura de plugins genérica pra trocar de biblioteca (isso resolveria um problema que não
   temos ainda). O objetivo é não deixar a API de terceiro vazar pelo resto do código, não criar
   camada de abstração especulativa.

## Pesquisa técnica — concluída

As três frentes de pesquisa terminaram, cada uma com decisões concretas registradas em
`docs/findings/`:

- [`rfb-protocol.md`](findings/rfb-protocol.md) — segurança `None`, formato de pixel padrão
  do servidor com conversão pra cinza no cliente, encoding `ZRLE` (revisado de `Raw` em 26/08,
  depois de medir ~155x menos bytes num scroll real e resolver um freeze do Enter),
  confirmação de que atualização sob demanda é o próprio design nativo do protocolo (não um
  "jeito de forçar"). A recomendação original de reconectar a cada interação foi **revisada**
  depois do teste em hardware real — ver `kindle-hardware-test.md`: hoje o modelo é conexão
  persistente com push automático da própria lib.
- [`libvncclient-api.md`](findings/libvncclient-api.md) — API mínima (`rfbGetClient` +
  `MallocFrameBuffer`/`GotFrameBufferUpdate`), integração com o loop do GTK via
  `GIOChannel`/`g_io_add_watch` (o fd do socket entra no loop assim que a conexão persistente
  vira o modelo), build mínimo via CMake (só zlib como dependência externa real), toolchain de
  cross-compile modelado no exemplo de MinGW do próprio repositório, e o achado importante de
  licença: **GPLv2** — o projeto inteiro precisa ser open-source quando for publicado.
- [`pi-vnc-server.md`](findings/pi-vnc-server.md) — TigerVNC (`Xvnc`) em vez de `x11vnc`
  (não depende de sessão X já rodando), expondo um `xterm` sob `matchbox-window-manager` (tela
  cheia de verdade), unit `systemd` seguindo o padrão já usado no projeto `kindle`.
- [`kindle-hardware-test.md`](findings/kindle-hardware-test.md) — o teste ponta a ponta no
  device físico e os bugs reais encontrados só nesse teste (o principal: `client->updateRect`
  nunca inicializado, fazendo o framebuffer sempre chegar zerado), a investigação do resize
  automático de tela (mais 3 bugs reais na lib vendorizada), latência medida no hardware,
  mecânica de deploy no Kindle, e o esquema de título de janela que o window manager do Kindle
  exige pra tela cheia.

## Próximos passos

1. ~~Configurar o TigerVNC de verdade no Pi~~ — **feito e testado**: `Xtigervnc` + `xterm` sob
   `matchbox-window-manager` (tela cheia de verdade, revisão de 26/08) rodando via unit
   `systemd` (`vnc-kindle.service`), acessado com sucesso a partir de um cliente VNC nativo no
   Mac. Achados reais de RAM e uma pegadinha de versão do TigerVNC 1.15 registrados em
   [`pi-vnc-server.md`](findings/pi-vnc-server.md).
2. ~~Escrever o toolchain file de CMake e cross-compilar o `libvncclient`~~ — **feito e
   testado**: toolchain file em [`cmake/`](../cmake/), `libvncclient` vendorizado como submódulo
   ([`vendor/libvncserver`](../vendor/libvncserver), pinado em `LibVNCServer-0.9.15`), buildado
   dentro do container `kindle-toolchain` e instalado no sysroot compartilhado — confirmado com
   um programa mínimo que compilou e linkou de verdade contra a lib cross-compilada. Achados e
   uma correção importante (as flags `WITH_LIBVNCSERVER`/`WITH_LIBVNCCLIENT` não existem nessa
   versão) registrados em [`libvncclient-api.md`](findings/libvncclient-api.md).
3. ~~Escrever o app GTK do Kindle~~ — **escrito, cross-compilado, revisado e com teste
   unitário**: [`app/`](../app/) — módulo isolado `vnc_client.c`/`.h` (único lugar que fala com
   `libvncclient`), uma `GtkDrawingArea` como único filho direto da janela, tocar na imagem
   manda um `PointerEvent`. Conversão de pixel pra escala de cinza extraída num módulo puro
   (`pixel_convert.c`, com LUTs por canal + loops especializados por bpp) com 10 casos de
   teste unitário. **Revisão (refactor Ports & Adapters leve, 26/08)**: a lógica que antes
   morava em `main.c` foi separada em módulos — ver "Estrutura do repositório".
4. ~~Testar ponta a ponta no hardware real~~ — **feito, funcionando**: texto digitado no Pi
   aparece no Kindle, toque no Kindle interage de volta com o Pi. Vários bugs reais só visíveis
   em hardware real foram encontrados e corrigidos nesse processo — ver
   [`kindle-hardware-test.md`](findings/kindle-hardware-test.md).
5. ~~Conexão persistente com push automático + resize automático de tela~~ — **feito, testado**:
   o app conecta uma vez, chama `vnc_client_start_updates()` uma vez, e a própria `libvncclient`
   mantém sozinha um pedido incremental sempre em andamento — o servidor só responde quando o
   conteúdo muda de verdade (reconexão automática se a conexão cair). O botão "Atualizar" foi
   **removido** (perdeu função com o push automático, e resolveu junto o bug do botão
   intocável). O cliente também detecta a resolução real da tela (`gdk_screen_width/height`) e
   pede ao servidor pra redimensionar a área remota via `SetDesktopSize` (extensão RFB), pra
   qualquer Kindle que conectar receber o frame 1:1, sem escala. Três bugs reais na
   `libvncclient` vendorizada foram encontrados e contornados nesse processo — detalhes em
   [`kindle-hardware-test.md`](findings/kindle-hardware-test.md).

6. ~~Teclado virtual~~ (parte 2 da PoC, item 1 — **feito, testado no hardware real**): faixa fixa
   nos 35% de baixo da tela do Kindle (`KEYBOARD_HEIGHT_PERCENT` em `ui.c`), frame remoto ocupa
   os 65% de cima — a área útil (ex.: `1072x941` no device real) é a mesma que já era pedida via
   `SetDesktopSize`, sem mecanismo novo de resize. Módulo puro novo `app/src/keyboard.c`/`.h`
   (layout como dados: 2 páginas × 6 fileiras, hit-test, sticky Shift/Ctrl — modificador arma pro
   próximo toque, já que multi-touch confiável não existe nesse hardware; Ctrl embrulha a tecla
   em `Control_L` down/up, Shift troca keysym e rótulo exibido), desenho de alto contraste em
   Cairo no `ui.c` (teclas brancas/borda preta, modificador armado invertido), `SendKeyEvent`
   plumbado por `vnc_client_send_key`/`session_send_key`. Teste unitário novo
   `app/tests/test_keyboard.c` (9 casos, mesma convenção sem framework do `pixel_convert`).
   Validado no device: digitação, Shift, Ctrl+C, página de símbolos. Achado de plataforma: a
   fonte do Kindle não tem o glifo ⌫ (aparecia tofu) — rótulo trocado por "Bksp"; setas
   ←↑↓→ renderizam ok. De carona, o teste no hardware revelou e resolveu uma travadinha real do
   Enter — ver a revisão de encoding em [`rfb-protocol.md`](findings/rfb-protocol.md).

7. ~~Menu do app (páginas/ações locais) + zoom remoto em 3 camadas~~ — **feito, testado no
   hardware real**: uma terceira página do teclado virtual, aberta pelo chord Ctrl+Shift+tecla-de-página
   (o rótulo vira "Menu" destacado quando o chord arma — funciona a partir das páginas de
   letras e de símbolos). Ações: voltar ao teclado, três pares de zoom (A-/A+), status da
   conexão (log) e sair do Kindow — a primeira forma de sair sem SSH. Um novo tipo de tecla,
   `KEY_ACTION`, emite uma `KeyboardAction` local via um callback próprio (`ui`→`main`) que
   nunca chega a ir pro servidor VNC. **Zoom remoto em três camadas INDEPENDENTES** (evolução
   de um controle único, separado a pedido do usuário durante a sessão): Apps (Xft/DPI via
   `xsettingsd`, ao vivo pra qualquer app GTK), Janela (fonte do titlebar do Openbox em
   pontos, `rc.xml` + `openbox --reconfigure` ao vivo) e Painel (fontes do `tint2rc` +
   reinício do `tint2`). Servidor: [`pi/kindow-helperd`](../pi/kindow-helperd), protocolo TCP na
   porta 5910 (`dpi`/`deco`/`panel N`, `get` devolve os três, `ping`). Cliente:
   [`remote_control.c`/`.h`](../app/src/remote_control.c) + `ZoomSpec`/`handle_zoom` em
   `main.c`. Decisão-chave: o `xrdb` da sessão fica **congelado em 192** — era o elo que
   re-acoplava as três camadas entre si. Arquitetura núcleo-vs-shims documentada no próprio
   `kindow-helperd`: XSETTINGS é o núcleo genérico, Openbox/tint2 são shims por componente.
   De quebra, um achado real de hardware: **`l3afpad` trocado por `mousepad`** no `xstartup`
   — o `l3afpad` pina a fonte da área de edição (ignora Xft/DPI), diagnosticado com
   screenshots comparativos no Pi; o `mousepad` usa a fonte monoespaçada do sistema e reescala
   ao vivo junto com o zoom de Apps. Também entregue: **flash de tecla** — feedback de toque
   no teclado virtual (tecla normal/ação pisca invertida por ~180ms, sticky/página não
   piscam porque o feedback delas já é o próprio estado), validado no hardware. Teste
   unitário `test_keyboard.c` cresceu de 9 pra 15 casos. Detalhes das três técnicas de
   investigação (diagnóstico por screenshot, medição de `SO_SNDTIMEO`/`connect()`,
   generalização do self-match de `pgrep`/`pkill`) em
   [`kindle-hardware-test.md`](findings/kindle-hardware-test.md); revisão da composição
   da sessão do Pi em [`pi-vnc-server.md`](findings/pi-vnc-server.md). O item 5 da fila
   (menu do app) ficou **parcialmente** feito — ver `docs/ideias-futuras.md`: ainda falta
   desconectar/conectar em outro IP com persistência (o toggle do teclado por gesto virou
   botão dedicado na reestrutura de UI abaixo, item 8).

8. ~~Reestrutura de UI: barra fixa, painel unificado, arrasto real, scroll ajustável,
   proporção de tela~~ — **feito, revisado e VALIDADO no hardware pelo usuário** (sessão de
   27/08, madrugada→manhã: cinco etapas implementadas, cross-compiladas, deployadas e
   revisadas enquanto o usuário dormia, confirmadas funcionando por ele ao acordar — "está
   tudo funcionando"). (1) Barra fixa no rodapé com 4 botões (Scroll ↑, Scroll ↓, Teclado,
   Menu), sempre visível. (2) Teclado e menu viraram conteúdo mutuamente exclusivo de um
   "painel" único (`PanelMode` em `ui.c`), alternado pelas 3 regras de toggle (nada
   aberto→abre; outro aberto→troca; já aberto→fecha); `SetDesktopSize` é re-pedido a cada
   abertura/fechamento (`session_set_target_size`). (3) A etapa mais arriscada: a página de
   símbolos do teclado (`?123`) ganhou duas teclas, "Esquerdo" (sticky, arma clique
   contínuo) e "Direito" (ação imediata), substituindo o espaço só ali (letras mantém
   espaço normal) — exigiu rastreamento real de motion/release em `ui.c`
   (`GDK_POINTER_MOTION_MASK` + `HINT` pro coalescing do GTK2, throttle de 8px), o arrasto
   termina quando o dedo levanta da tela; `session_send_drag` sempre clampa coordenada em
   vez de descartar, pra garantir que o release sempre chegue. O que o arrasto significa
   (mover janela, redimensionar, selecionar texto) é decidido pelo Openbox/GTK do servidor,
   sem desambiguação nossa. (4) Par "Scroll A-"/"Scroll A+" no menu ajusta quantas catracas
   de roda cada toque manda (`session_get/set_scroll_lines`, puramente client-side, faixa
   1-10). (5) `BAR_HEIGHT_PX` fixo (60px) virou `BAR_HEIGHT_PERCENT` (4% da altura + piso de
   40px), com insets e bordas também proporcionais à altura local de cada linha/botão
   (`proportional_inset`/`proportional_border_width` em `ui.c`) — garante que o app funcione
   bem em Kindles de resolução diferente da testada. De carona, um bug real achado pelo
   `reviewer`: `left_click_armed` vazava entre páginas/painéis sem indicador visual,
   corrigido em dois pontos (`keyboard.c`, `ui.c`) com teste de regressão novo. Suíte
   `test_keyboard.c` tem agora 14 casos. Commits `e6de59e` (implementação) e `050b189`
   (confirmação de validação), ambos em `origin/main`. Detalhes técnicos completos dos itens
   3, 4 e 5 da fila em [`docs/ideias-futuras.md`](ideias-futuras.md).

## Ideias futuras (não implementadas)

A fila priorizada da **parte 2 da PoC** (definida em 26/08, ao fechar a parte 1) vive em
[`docs/ideias-futuras.md`](ideias-futuras.md). Dos itens originais, restam pendentes: o
resto do menu do app (desconectar/conectar em outro IP com persistência), orientação
paisagem — mais as duas exploratórias já anotadas antes (espelhar a sessão física do Pi e
usar o Kindle como segundo monitor de verdade). Os itens 1 (teclado virtual), 2 (GUI com
editor de texto), 3 (scroll) e 4 (botão direito/arrasto) já saíram da fila; o item 5 (menu
do app) ficou parcialmente feito — ver "Próximos passos" acima (itens 7 e 8).
