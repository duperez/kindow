# Kindow

Prova de conceito: transformar um Kindle jailbreakado numa tela sem fio pro Raspberry Pi, via
VNC — toque na tela do Kindle vira entrada remota, atualização de tela sob demanda (não tem
como um e-ink aguentar refresh contínuo).

**Status: PoC provada ponta a ponta no hardware real.** Texto digitado no Pi aparece no Kindle
automaticamente (conexão persistente com push do protocolo, sem botão/ação manual), e toque no
Kindle manda entrada real de volta pro Pi (confirmado: um clique deu foco no `xterm`, cursor
ficou visível). A tela remota se redimensiona sozinha pra bater 1:1 com a resolução real do
Kindle que conectar. Detalhes de tudo que precisou ser corrigido pra chegar lá em
[`kindle-hardware-test.md`](docs/findings/kindle-hardware-test.md).

## Contexto

Nasceu de uma investigação dentro do projeto [`kindle`](../kindle/) (o painel/dashboard que
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
   [`../kindle/docs/findings/landscape-orientation-blocked.md`](../kindle/docs/findings/landscape-orientation-blocked.md)),
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

- [`rfb-protocol.md`](docs/findings/rfb-protocol.md) — segurança `None`, formato de pixel padrão
  do servidor com conversão pra cinza no cliente, encoding `Raw` (+`CopyRect` opcional),
  confirmação de que atualização sob demanda é o próprio design nativo do protocolo (não um
  "jeito de forçar"). A recomendação original de reconectar a cada interação foi **revisada**
  depois do teste em hardware real — ver `kindle-hardware-test.md`: hoje o modelo é conexão
  persistente com push automático da própria lib.
- [`libvncclient-api.md`](docs/findings/libvncclient-api.md) — API mínima (`rfbGetClient` +
  `MallocFrameBuffer`/`GotFrameBufferUpdate`), integração com o loop do GTK via
  `GIOChannel`/`g_io_add_watch` (o fd do socket entra no loop assim que a conexão persistente
  vira o modelo), build mínimo via CMake (só zlib como dependência externa real), toolchain de
  cross-compile modelado no exemplo de MinGW do próprio repositório, e o achado importante de
  licença: **GPLv2** — o projeto inteiro precisa ser open-source quando for publicado.
- [`pi-vnc-server.md`](docs/findings/pi-vnc-server.md) — TigerVNC (`Xvnc`) em vez de `x11vnc`
  (não depende de sessão X já rodando), expondo um `xterm` sob `matchbox-window-manager` (tela
  cheia de verdade), unit `systemd` seguindo o padrão já usado no projeto `kindle`.
- [`kindle-hardware-test.md`](docs/findings/kindle-hardware-test.md) — o teste ponta a ponta no
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
   [`pi-vnc-server.md`](docs/findings/pi-vnc-server.md).
2. ~~Escrever o toolchain file de CMake e cross-compilar o `libvncclient`~~ — **feito e
   testado**: toolchain file em [`cmake/`](cmake/), `libvncclient` vendorizado como submódulo
   ([`vendor/libvncserver`](vendor/libvncserver), pinado em `LibVNCServer-0.9.15`), buildado
   dentro do container `kindle-toolchain` e instalado no sysroot compartilhado — confirmado com
   um programa mínimo que compilou e linkou de verdade contra a lib cross-compilada. Achados e
   uma correção importante (as flags `WITH_LIBVNCSERVER`/`WITH_LIBVNCCLIENT` não existem nessa
   versão) registrados em [`libvncclient-api.md`](docs/findings/libvncclient-api.md).
3. ~~Escrever o app GTK do Kindle~~ — **escrito, cross-compilado, revisado e com teste
   unitário**: [`app/`](app/) — módulo isolado `vnc_client.c`/`.h` (único lugar que fala com
   `libvncclient`), `main.c` com uma `GtkDrawingArea` como único filho direto da janela, tocar
   na imagem manda um `PointerEvent`. Conversão de pixel pra escala de cinza extraída num
   módulo puro (`pixel_convert.c`, com LUTs por canal + loops especializados por bpp) com 10
   casos de teste unitário.
4. ~~Testar ponta a ponta no hardware real~~ — **feito, funcionando**: texto digitado no Pi
   aparece no Kindle, toque no Kindle interage de volta com o Pi. Vários bugs reais só visíveis
   em hardware real foram encontrados e corrigidos nesse processo — ver
   [`kindle-hardware-test.md`](docs/findings/kindle-hardware-test.md).
5. ~~Conexão persistente com push automático + resize automático de tela~~ — **feito, testado**:
   o app conecta uma vez, chama `vnc_client_start_updates()` uma vez, e a própria `libvncclient`
   mantém sozinha um pedido incremental sempre em andamento — o servidor só responde quando o
   conteúdo muda de verdade (reconexão automática se a conexão cair). O botão "Atualizar" foi
   **removido** (perdeu função com o push automático, e resolveu junto o bug do botão
   intocável). O cliente também detecta a resolução real da tela (`gdk_screen_width/height`) e
   pede ao servidor pra redimensionar a área remota via `SetDesktopSize` (extensão RFB), pra
   qualquer Kindle que conectar receber o frame 1:1, sem escala. Três bugs reais na
   `libvncclient` vendorizada foram encontrados e contornados nesse processo — detalhes em
   [`kindle-hardware-test.md`](docs/findings/kindle-hardware-test.md).

## O que falta pra além da PoC (polish, não bloqueador)

- `SendKeyEvent`/teclado ainda não implementado — só toque (`PointerEvent`) funciona hoje. Uma
  direção cogitada (ainda sem pesquisa nem decisão) é um teclado virtual desenhado no próprio
  Kindle, que traduziria toque em `SendKeyEvent` — não é compromisso, só anotado como possível
  próximo passo.

## Ideias futuras (não implementadas)

Propostas levantadas em conversa, ainda sem pesquisa nem decisão — ver
[`docs/ideias-futuras.md`](docs/ideias-futuras.md): espelhar a sessão física do Pi (em vez de
uma sessão virtual própria) e usar o Kindle como segundo monitor de verdade (extensão via
Xrandr, não sessão espelhada).

## Estrutura do repositório

- `docs/findings/` — achados técnicos, um arquivo por problema/solução (mesmo padrão do
  projeto `kindle`).
- `app/` — o cliente GTK do Kindle (`src/main.c`, módulo isolado `src/vnc_client.c`/`.h`,
  `src/pixel_convert.c` testável, `tests/`, `meson.build`).
- `cmake/` — toolchain file de CMake pro cross-compile do `libvncclient`.
- `vendor/libvncserver` — submódulo git do `libvncclient` (LibVNC/libvncserver), pinado numa
  release estável.
