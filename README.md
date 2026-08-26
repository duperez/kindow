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

## Próximos passos

Pesquisa técnica antes de codar (nessa ordem):

- Protocolo RFB (RFC 6143) — handshake, tipos de segurança, formato de pixel, mensagens
  mínimas necessárias (`FramebufferUpdateRequest`, `PointerEvent`, `KeyEvent`).
- API do `libvncclient` — callbacks, como cross-compilar via CMake dentro do container Docker
  do toolchain (hoje só temos Meson configurado lá, pro projeto `kindle`).
- Servidor VNC pro lado do Pi — `x11vnc` vs `TigerVNC`/`Xvnc`, o que é mais leve pro Pi 3B
  (1GB RAM) e mais simples de rodar como serviço `systemd`.
- Ambiente gráfico mínimo no Pi — hoje ele é totalmente headless. Decidir o mínimo necessário
  pra ter algo pra "ver" (pode ser só um terminal, sem gerenciador de janelas).

## Estrutura do repositório

- `docs/findings/` — achados técnicos, um arquivo por problema/solução (mesmo padrão do
  projeto `kindle`).
