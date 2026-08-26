# Kindow

Prova de conceito: transformar um Kindle jailbreakado numa tela sem fio pro Raspberry Pi, via
VNC — toque na tela do Kindle vira entrada remota, atualização de tela sob demanda (não tem
como um e-ink aguentar refresh contínuo).

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
   Atualização de tela **sob demanda** (botão/ação explícita), nunca em loop contínuo — decisão
   de design pro hardware, não limitação da PoC.
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
  "jeito de forçar"), e recomendação de reconectar a cada interação em vez de manter socket
  ocioso (mitiga o WiFi do Kindle entrando em economia de energia).
- [`libvncclient-api.md`](docs/findings/libvncclient-api.md) — API mínima (`rfbGetClient` +
  `MallocFrameBuffer`/`GotFrameBufferUpdate`), integração com o loop do GTK2 via
  `g_io_add_watch` (não thread separada, pelo menos de início), build mínimo via CMake (só zlib
  como dependência externa real), toolchain de cross-compile modelado no exemplo de MinGW do
  próprio repositório, e o achado importante de licença: **GPLv2** — o projeto inteiro precisa
  ser open-source quando for publicado.
- [`pi-vnc-server.md`](docs/findings/pi-vnc-server.md) — TigerVNC (`Xvnc`) em vez de `x11vnc`
  (não depende de sessão X já rodando), expondo só um `xterm` sem gerenciador de janelas,
  unit `systemd` seguindo o padrão já usado no projeto `kindle`.

## Próximos passos

1. ~~Configurar o TigerVNC de verdade no Pi~~ — **feito e testado**: `Xtigervnc` + `xterm`
   sozinho (sem WM) rodando via unit `systemd` (`vnc-kindle.service`), acessado com sucesso a
   partir de um cliente VNC nativo no Mac. Achados reais de RAM e uma pegadinha de versão do
   TigerVNC 1.15 registrados em [`pi-vnc-server.md`](docs/findings/pi-vnc-server.md).
2. ~~Escrever o toolchain file de CMake e cross-compilar o `libvncclient`~~ — **feito e
   testado**: toolchain file em [`cmake/`](cmake/), `libvncclient` vendorizado como submódulo
   ([`vendor/libvncserver`](vendor/libvncserver), pinado em `LibVNCServer-0.9.15`), buildado
   dentro do container `kindle-toolchain` e instalado no sysroot compartilhado — confirmado com
   um programa mínimo que compilou e linkou de verdade contra a lib cross-compilada. Achados e
   uma correção importante (as flags `WITH_LIBVNCSERVER`/`WITH_LIBVNCCLIENT` não existem nessa
   versão) registrados em [`libvncclient-api.md`](docs/findings/libvncclient-api.md).
3. ~~Escrever o app GTK do Kindle~~ — **escrito, cross-compilado, revisado e com teste
   unitário**: [`app/`](app/) — módulo isolado `vnc_client.c`/`.h` (único lugar que fala com
   `libvncclient`), `main.c` com uma `GtkDrawingArea` + botão "Atualizar", tocar na imagem manda
   um `PointerEvent` e busca o resultado. Conversão de pixel pra escala de cinza extraída num
   módulo puro (`pixel_convert.c`) com 10 casos de teste unitário. Binário ARM final confirmado
   linkando certo contra `libgtk-x11-2.0`, `libcairo` e `libvncclient`. Falta só o teste no
   hardware de verdade (próximo item).
4. Testar ponta a ponta no hardware real (Kindle ↔ Pi) — ainda não feito.

## Estrutura do repositório

- `docs/findings/` — achados técnicos, um arquivo por problema/solução (mesmo padrão do
  projeto `kindle`).
- `app/` — o cliente GTK do Kindle (`src/main.c`, módulo isolado `src/vnc_client.c`/`.h`,
  `src/pixel_convert.c` testável, `tests/`, `meson.build`).
- `cmake/` — toolchain file de CMake pro cross-compile do `libvncclient`.
- `vendor/libvncserver` — submódulo git do `libvncclient` (LibVNC/libvncserver), pinado numa
  release estável.
