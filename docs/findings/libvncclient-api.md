# API do `libvncclient` e cross-compilation

Baseado em leitura direta do repositório `LibVNC/libvncserver` (`README.md`, `CMakeLists.txt`,
`include/rfb/rfbclient.h`, `include/rfb/keysym.h`, `COPYING`, exemplos em `examples/client/`),
além do `hwhw/kindlevncviewer` (cliente real pra Kindle usando essa lib) e do plugin VNC do
Remmina (app GTK real em produção usando a lib). Onde a fonte não confirma diretamente, marco
como inferência.

## Chamadas principais

- **`rfbGetClient(8, 3, 4)`** — primeira chamada obrigatória (8 bits/amostra, 3 amostras/pixel,
  4 bytes/pixel = RGB 32-bit padrão, o que a maioria dos apps GTK/Cairo já espera).
- **Callbacks obrigatórios**: `MallocFrameBuffer` (aloca o buffer local quando o servidor informa
  dimensões — também o lugar certo pra redimensionar a superfície de desenho) e
  `GotFrameBufferUpdate` (dispara quando pixels novos chegam — **a lib já decodifica direto em
  `client->frameBuffer`**, não precisamos compor retângulo manualmente; dá pra ignorar os
  parâmetros x/y/w/h e simplesmente redesenhar o buffer inteiro toda vez, mais simples).
- **`FinishedFrameBufferUpdateProc`** — dispara uma vez por mensagem completa (não por
  retângulo). Relevante pra nós: dá pra usar isso pra disparar só **um** refresh de e-ink por
  rajada de atualização, em vez de um por retângulo — importante já que refresh parcial em e-ink
  é caro/feio visualmente.
- **Existe um caminho alternativo** (`GotBitmap`/`GotFillRect`) pra quem quer compor a tela
  manualmente — não precisamos, o caminho simples acima já serve.
- **Conectar**: usar `rfbClientConnect()` + `rfbClientInitialise()` separados (não
  `rfbInitClient()`, que é pensado pra receber `argv` de CLI — nosso host vem de outro lugar,
  não de linha de comando).

## Integração com o loop principal do GTK2

`client->sock` é um file descriptor POSIX comum (`#define rfbSocket int` em Linux) — dá pra
integrar sem sacrifício.

Duas abordagens reais encontradas:
1. **`g_io_add_watch()` no fd, single-thread** — a recomendada pra nós. Simples, sem lock, sem
   marshaling entre threads pra atualizar o framebuffer. GTK2 tem uma história frágil com
   threading (`gdk_threads_enter/leave`), então evitar multi-thread é a escolha mais segura por
   padrão.
2. **Thread dedicada** — é o que o Remmina faz de verdade em produção (loop bloqueante numa
   `pthread`, resultado repassado pra UI via mutex + `g_idle_add`). Mais robusto contra
   `HandleRFBServerMessage` bloquear a UI, mas mais complexo (temos que gerenciar lock/fila).

**Decisão**: começar com `g_io_add_watch` (opção 1). Só migrar pra thread dedicada se
percebermos travamento de UI na prática — não assumir isso de antemão.

## Entrada (toque → clique/tecla)

Confirmado as assinaturas exatas:
```c
SendPointerEvent(client, x, y, buttonMask);
SendKeyEvent(client, keysym, down);  // down/up são mensagens separadas, mandar as duas
```
A lib já vem com `keysym.h` (cópia do `keysymdef.h` padrão do X11, sem dependência de libX11 em
tempo de execução — só constantes). Achado útil: pra caracteres ASCII imprimíveis, o valor do
keysym **é** o próprio código do caractere (`XK_A == 0x41 == 'A'`) — não precisa de tabela pra
letras/números comuns, só pras teclas especiais (Backspace, Enter, setas), que aí sim precisam de
uma tabelinha de tradução (mesmo padrão que os exemplos oficiais da lib usam).

## Build mínimo (dependência)

Confirmado direto no `CMakeLists.txt`:

```
-DWITH_LIBVNCSERVER=OFF -DWITH_LIBVNCCLIENT=ON   # só cliente, sem servidor
-DWITH_GCRYPT=OFF -DWITH_OPENSSL=OFF -DWITH_GNUTLS=OFF   # sem crypto/TLS externo
-DWITH_JPEG=OFF -DWITH_PNG=OFF                            # Raw-only não precisa
-DWITH_SDL=OFF -DWITH_GTK=OFF -DWITH_QT=OFF -DWITH_FFMPEG=OFF -DWITH_LIBSSHTUNNEL=OFF
-DWITH_SASL=OFF -DWITH_XCB=OFF -DWITH_SYSTEMD=OFF -DWITH_WEBSOCKETS=OFF
-DWITH_EXAMPLES=OFF -DWITH_TESTS=OFF
```

Com `WITH_GCRYPT`/`WITH_OPENSSL` desligados, a lib cai automaticamente pra uma implementação
própria embutida de SHA1/DES (`crypto_included.c`) — zero dependência externa de crypto, o
suficiente pro `VNC Authentication` (não que a gente vá usar, decidimos `None` no achado do
protocolo RFB). Com `WITH_JPEG=OFF`, confirmado que o único arquivo que toca libjpeg
(`turbojpeg.c`) simplesmente não entra no build — seguro de desligar já que vamos usar só
encoding `Raw`.

**Achado importante**: `WITH_GTK` nesse CMakeLists controla só se o **exemplo** `gtkvncviewer` é
compilado — a biblioteca `vncclient` em si **não linka GTK em nenhum lugar**. Ou seja, não
precisamos de headers de desenvolvimento do GTK2 no sysroot só pra compilar essa dependência.

Sobra basicamente só o **zlib** como dependência externa real (miniLZO/crypto/D3DES vêm sempre
embutidos, não dá pra tirar mesmo desligando a flag).

## Cross-compilation

O projeto já usa Meson + toolchain Koxtoolchain via Docker (ver `kindle/docs/findings/gtk-toolchain-setup.md`),
mas `libvncclient` usa CMake — precisamos escrever um toolchain file próprio de CMake, seguindo o
mesmo padrão do arquivo de exemplo que o próprio repositório já traz pra MinGW
(`cmake/Toolchain-cross-mingw32-linux.cmake`), só trocando o compilador/sysroot alvo pro nosso
`arm-kindlehf-linux-gnueabihf`.

**Precedente real encontrado**: o `hwhw/kindlevncviewer` usa a mesma família de toolchain
(koxtoolchain) com sucesso em hardware Kindle real — confirma que a combinação
"koxtoolchain + libvncclient" funciona nesse tipo de device. Só não dá pra copiar a receita
literal dele — o projeto compila os `.c` na mão em vez de usar CMake, e a base de código dele
hoje é Lua/LuaJIT, não C puro.

Depois de compilar e instalar no sysroot, o `libvncclient.pc` (arquivo pkg-config que o próprio
CMake gera) deve deixar o Meson encontrar a lib via `dependency('libvncclient')`, desde que
`PKG_CONFIG_SYSROOT_DIR`/`PKG_CONFIG_PATH` no cross-file do Meson apontem pro sysroot certo —
mecanismo padrão, ainda não confirmado contra nosso `meson-crosscompile.txt` real.

## Licença — importante pro "manter público depois"

Confirmado direto no `COPYING`: **GPLv2** (não LGPL, apesar de algumas fontes secundárias
confundirem). Isso importa de verdade pro plano de deixar o `kindow` público: a obrigação de
disponibilizar código-fonte é acionada por **distribuição** do binário, não por uso interno — e
como a intenção é publicar o projeto pra outros donos de Kindle usarem, isso nos coloca sob
GPLv2 pro projeto inteiro, não só pela parte que toca a lib (linkar estático ou dinâmico não faz
diferença nenhuma sob GPLv2). Ou seja: quando formos publicar, o `kindow` precisa ser
open-source sob GPLv2 (ou compatível) — vale já ir escrevendo o código com isso em mente, não é
surpresa pra decidir depois.

## Checklist de setup

1. Build mínimo do CMake (seção acima) → só zlib como dependência externa real.
2. Toolchain file de CMake próprio, pro nosso `arm-kindlehf-linux-gnueabihf`, modelado no exemplo
   de MinGW que o repositório já traz.
3. Instalar lib + headers + `.pc` no mesmo sysroot que o Meson já usa; apontar
   `PKG_CONFIG_SYSROOT_DIR`/`PKG_CONFIG_PATH` no cross-file.
4. `rfbGetClient(8,3,4)` → setar `MallocFrameBuffer`/`GotFrameBufferUpdate` →
   `rfbClientConnect`+`rfbClientInitialise` → `g_io_add_watch` no `client->sock` integrado ao
   loop do GTK2, chamando `HandleRFBServerMessage` quando tiver dado pra ler.
5. `SendPointerEvent`/`SendKeyEvent` com constantes `XK_*` do `keysym.h` — ASCII direto pra
   caracteres imprimíveis, tabela pequena só pras teclas especiais.
6. Lembrar: GPLv2 se propaga pro projeto inteiro no momento de distribuir binário — planejar
   código aberto desde já, não como decisão de última hora.
