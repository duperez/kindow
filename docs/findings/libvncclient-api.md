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
- **Conectar**: usar `ConnectToRFBServer()` + `InitialiseRFBConnection()` separados (não
  `rfbInitClient()`, que é pensado pra receber `argv` de CLI — nosso host vem de outro lugar,
  não de linha de comando). **Correção em relação ao rascunho original desta pesquisa**: os
  nomes reais na API são esses dois, não `rfbClientConnect`/`rfbClientInitialise` (que não
  existem) — confirmado direto no header vendorizado
  (`vendor/libvncserver/include/rfb/rfbclient.h`) e implementado em
  [`app/src/vnc_client.c`](../../app/src/vnc_client.c), com a sequência completa
  (`ConnectToRFBServer` → `InitialiseRFBConnection` → `MallocFrameBuffer` →
  `SetFormatAndEncodings`) copiada da função interna `rfbInitConnection` da própria lib
  (`vendor/libvncserver/src/libvncclient/vncviewer.c`, não exportada — por isso reimplementada).

## Integração com o loop principal do GTK — `GIOChannel`/`g_io_add_watch`

`client->sock` é um file descriptor POSIX comum (`#define rfbSocket int` em Linux) — dá pra
integrar sem sacrifício, **se** a conexão for persistente.

Duas abordagens foram cogitadas originalmente pra uma conexão de longa duração:
1. **`g_io_add_watch()` no fd, single-thread** — evita threading com GTK (história frágil de
   `gdk_threads_enter/leave`).
2. **Thread dedicada** — o que o Remmina faz de verdade em produção, mais robusto mas mais
   complexo (lock/fila).

**Decisão original desta pesquisa (revisada — ver abaixo)**: como o modelo de conexão do
projeto era reconectar do zero a cada interação (decisão original em `rfb-protocol.md` — nunca
manter socket ocioso), cada round-trip seria curto, então nem `g_io_add_watch` nem thread
dedicada pareciam necessários — a busca de frame bloquearia a thread principal do GTK por um
tempo curto dentro do próprio handler de clique.

**Revisão (teste em hardware real)**: o modelo de conexão mudou pra persistente (custo real de
reconectar medido em ~1-2s por burst vazio do TigerVNC — Achado #2 de
[`kindle-hardware-test.md`](kindle-hardware-test.md)), o que faz a primeira abordagem cogitada
acima ser exatamente a certa: `vnc_client_get_fd()` expõe o fd já conectado, e o app registra ele
no loop do GTK via `GIOChannel`/`g_io_add_watch()` (`g_unix_fd_add()` seria mais direto, mas não
existe no glib vendorizado nesse sysroot — confirmado em runtime, não achado em doc). A cada
sinal de leitura no fd, o app chama `vnc_client_handle_messages()`; a própria `libvncclient`
dispara o próximo pedido incremental sozinha internamente, então não há loop de polling nem
thread dedicada — o watch só existe pra não bloquear a UI enquanto espera a próxima mudança de
tela, que pode nunca vir. Mais simples que a thread dedicada do Remmina, mas não tão trivial
quanto "nem watch nem thread" cogitado quando o modelo ainda era reconectar por interação.

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

Flags testadas de verdade num cross-compile real (`LibVNCServer-0.9.15`, a release estável mais
recente, vendorizada como submódulo em `vendor/libvncserver`):

```
-DWITH_LIBVNCSERVER=OFF -DWITH_LIBVNCCLIENT=ON
-DWITH_GCRYPT=OFF -DWITH_OPENSSL=OFF -DWITH_GNUTLS=OFF   # sem crypto/TLS externo
-DWITH_JPEG=OFF -DWITH_PNG=OFF                            # Raw-only não precisa
-DWITH_SDL=OFF -DWITH_GTK=OFF -DWITH_QT=OFF -DWITH_FFMPEG=OFF -DWITH_LIBSSHTUNNEL=OFF
-DWITH_SASL=OFF -DWITH_XCB=OFF -DWITH_SYSTEMD=OFF -DWITH_WEBSOCKETS=OFF
-DWITH_EXAMPLES=OFF -DWITH_TESTS=OFF
```

**Correção importante em relação ao que a pesquisa por texto tinha achado**: nessa versão
(0.9.15), `WITH_LIBVNCSERVER`/`WITH_LIBVNCCLIENT` **não existem** como `option()` no
`CMakeLists.txt` — só ficam como cache "UNINITIALIZED" sem efeito nenhum (confirmado via
`grep`/`CMakeCache.txt`). O `add_library(vncserver ...)` roda incondicionalmente. Na prática:
**as duas libs (`vncclient` e `vncserver`) sempre buildam juntas** nessa versão, não tem como
compilar só o cliente. Não é um problema real pro projeto — nosso app só linka `vncclient`, o
`vncserver.so` fica parado no sysroot sem nunca ir pro binário final do Kindle — mas é importante
saber que a flag não faz o que o nome sugere.

Com `WITH_GCRYPT`/`WITH_OPENSSL` desligados, a lib cai automaticamente pra uma implementação
própria embutida de SHA1/DES (`crypto_included.c`) — zero dependência externa de crypto.

**Achado importante**: `WITH_GTK` nesse CMakeLists controla só se o **exemplo** `gtkvncviewer` é
compilado — a biblioteca `vncclient` em si **não linka GTK em nenhum lugar**. Não precisamos de
headers de desenvolvimento do GTK2 no sysroot só pra compilar essa dependência.

**Zlib confirmado já presente no sysroot** (extraído do firmware do Kindle via KMC SDK,
independente desse build) — realmente sobra só ele como dependência externa real, como a
pesquisa original já apontava, agora confirmado por teste e não só por leitura do CMakeLists.

## Cross-compilation — feito e testado

Toolchain file em [`cmake/Toolchain-arm-kindlehf-linux-gnueabihf.cmake`](../../cmake/Toolchain-arm-kindlehf-linux-gnueabihf.cmake),
espelhando o `meson-crosscompile.txt` do projeto `kindle` (mesmo compilador/sysroot,
`arm-kindlehf-linux-gnueabihf`).

**Precedente real encontrado na pesquisa**: o `hwhw/kindlevncviewer` usa a mesma família de
toolchain (koxtoolchain) com sucesso em hardware Kindle real. Não dava pra copiar a receita
literal dele (compila os `.c` na mão, base de código hoje é Lua/LuaJIT) — mas agora temos nossa
própria receita CMake testada.

**Passo a passo real, executado dentro do container `kindle-toolchain`** (mesmo container do
projeto `kindle`, com `cmake` instalado nele e um mount novo `-v
/Users/eduardoperez/projects/kindow:/kindow` adicionado — ver `docker run` na seção de setup):

```sh
# dentro do container, como usuário builder
cd /kindow/vendor/libvncserver
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=/kindow/cmake/Toolchain-arm-kindlehf-linux-gnueabihf.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/home/builder/x-tools/arm-kindlehf-linux-gnueabihf/arm-kindlehf-linux-gnueabihf/sysroot/usr \
  -DWITH_LIBVNCSERVER=OFF -DWITH_LIBVNCCLIENT=ON \
  -DWITH_GCRYPT=OFF -DWITH_OPENSSL=OFF -DWITH_GNUTLS=OFF \
  -DWITH_JPEG=OFF -DWITH_PNG=OFF \
  -DWITH_SDL=OFF -DWITH_GTK=OFF -DWITH_QT=OFF -DWITH_FFMPEG=OFF -DWITH_LIBSSHTUNNEL=OFF \
  -DWITH_SASL=OFF -DWITH_XCB=OFF -DWITH_SYSTEMD=OFF -DWITH_WEBSOCKETS=OFF \
  -DWITH_EXAMPLES=OFF -DWITH_TESTS=OFF \
  -DBUILD_SHARED_LIBS=ON
cmake --build build -j$(nproc)
sudo cmake --install build   # sudo só pra escrever no sysroot dentro do container
```

Confirmado que o resultado é um binário ARM de verdade, não do host:
```
$ file .../sysroot/usr/lib/libvncclient.so.0.9.15
ELF 32-bit LSB shared object, ARM, EABI5 version 1 (SYSV), dynamically linked, ...
```

E que o `.pc` gerado funciona de ponta a ponta — um programa mínimo (`rfbGetClient` +
`rfbClientCleanup`) compilou e linkou com sucesso via `pkg-config --cflags --libs libvncclient`,
gerando outro binário ARM válido. Isso confirma que quando o Meson do app GTK do Kindle apontar
`PKG_CONFIG_SYSROOT_DIR`/`PKG_CONFIG_LIBDIR` pro mesmo sysroot (mesmo mecanismo que o
`pkg_config_libdir` do `meson-crosscompile.txt` já usa pras deps do projeto `kindle`), ele vai
achar `libvncclient` via `dependency('libvncclient')` sem trabalho extra.

**Importante sobre reprodutibilidade**: o resultado do build (lib instalada no sysroot) vive só
dentro do filesystem do container Docker `kindle-toolchain`, não no repositório git. Se o
container for destruído, os comandos acima recriam o mesmo resultado a partir do submódulo
(`vendor/libvncserver`, pinado no commit da tag `LibVNCServer-0.9.15`) + do toolchain file — não
precisa refazer nenhuma decisão, só rerodar.

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

1. ~~Build mínimo do CMake~~ — **feito**: só zlib como dependência externa real (confirmado, já
   presente no sysroot do firmware).
2. ~~Toolchain file de CMake próprio~~ — **feito**:
   [`cmake/Toolchain-arm-kindlehf-linux-gnueabihf.cmake`](../../cmake/Toolchain-arm-kindlehf-linux-gnueabihf.cmake).
3. ~~Instalar lib + headers + `.pc` no sysroot~~ — **feito e testado**: `libvncclient.so`/headers/
   `libvncclient.pc` instalados no sysroot compartilhado com o Meson do projeto `kindle`; um
   programa mínimo compilou e linkou com sucesso via `pkg-config`.
4. ~~`rfbGetClient(8,3,4)` → conectar → `MallocFrameBuffer`/`FinishedFrameBufferUpdate` →
   pedir e esperar atualização~~ — **feito**: implementado em
   [`app/src/vnc_client.c`](../../app/src/vnc_client.c) (módulo isolado, ver princípio de
   isolamento na seção "O que já decidimos" do README). Fluxo atual (conexão persistente),
   orquestrado por [`app/src/session.c`](../../app/src/session.c) (refactor Ports & Adapters
   leve de 26/08 — antes vivia em `main.c`): `vnc_client_connect()` → `vnc_client_get_fd()`
   (registrado no loop do GLib via `GIOChannel`/`g_io_add_watch`, ver seção de integração
   acima) → `vnc_client_start_updates()` uma única vez → `vnc_client_handle_messages()` a cada
   sinal de leitura no fd → `vnc_client_disconnect()` ao encerrar.
5. ~~`SendPointerEvent` com toque~~ — **feito**: `vnc_client_send_pointer()`, chamado por
   `session_send_click()` em [`app/src/session.c`](../../app/src/session.c) a partir do toque
   que [`app/src/ui.c`](../../app/src/ui.c) captura na `GtkDrawingArea` e repassa via callback
   até virar `PointerEvent`. ~~`SendKeyEvent`/`keysym.h` ainda não implementado~~ — **feito
   (26/08, parte 2 da PoC)**: `vnc_client_send_key()`, chamado por `session_send_key()` a partir
   do teclado virtual (`app/src/keyboard.c`/`.h`, módulo puro de layout/hit-test/sticky
   modifiers) desenhado por `ui.c` numa faixa fixa da tela. Testado no device: digitação, Shift,
   Ctrl+C, página de símbolos — detalhe completo em
   [`kindle-hardware-test.md`](kindle-hardware-test.md).
6. ~~Resize automático pra bater com a resolução real de qualquer Kindle~~ — **feito**:
   `vnc_client_request_desktop_size()`, extensão RFB `SetDesktopSize`/`ExtDesktopSize`. Três
   bugs reais na lib vendorizada foram encontrados e contornados nesse processo (endianTest não
   inicializado, `SendExtDesktopSize()` com campos não preenchidos, parser de resposta
   descartando `screen.id==0`) — detalhe completo em
   [`kindle-hardware-test.md`](kindle-hardware-test.md).
7. Lembrar: GPLv2 se propaga pro projeto inteiro no momento de distribuir binário — planejar
   código aberto desde já, não como decisão de última hora.
