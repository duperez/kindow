# Kindow

*[English version](../README.md) (a principal do repositório)*

O Kindow transforma um Kindle jailbreakado numa tela de toque sem fio pra um Raspberry
Pi, via VNC. O desktop do Pi é renderizado no e-ink do Kindle, e o toque na tela é
enviado de volta como eventos de mouse e teclado. As atualizações de tela são
estritamente sob demanda — o servidor só transmite quando o conteúdo muda, que é tanto
o funcionamento natural do protocolo RFB quanto o que um painel e-ink exige.

<p align="center">
  <img src="images/kindow-photo.jpg" alt="Um Kindle num suporte, rodando o Kindow: o editor Mousepad do Pi com 'Hello from Kindow' digitado no teclado virtual" width="420">
  &nbsp;&nbsp;
  <img src="images/kindow-photo-files.jpg" alt="O mesmo Kindle navegando o filesystem do Pi com o gerenciador de arquivos PCManFM" width="420">
</p>

<p align="center">
  <img src="images/session.png" alt="Captura de framebuffer de uma sessão do Kindow: o desktop do Pi com Mousepad, taskbar e o teclado virtual" width="380">
  &nbsp;&nbsp;
  <img src="images/connect.png" alt="Captura de framebuffer da tela de conexão do Kindow: lista de servidores salvos, botão de adicionar e a barra do rodapé" width="380">
</p>

*Acima: fotos do device. Abaixo: capturas de framebuffer, idênticas pixel a pixel ao
que a tela mostra.*

**Status**: funcional de ponta a ponta em hardware real (Kindle KT5 + Raspberry Pi 4).

## Funcionalidades

- **Desktop remoto interativo** — a sessão X do Pi (Openbox, tint2, aplicações GTK)
  renderizada 1:1. A resolução remota se ajusta automaticamente à tela do Kindle que
  conectar, via a extensão `SetDesktopSize` do RFB; sem escala nem corte.
- **Entrada por toque** — um toque é um clique esquerdo. A página de símbolos do
  teclado tem teclas dedicadas de clique Esquerdo (pressionar-e-arrastar, solto quando
  o dedo levanta) e Direito.
- **Teclado virtual** — modificadores Shift/Ctrl sticky (combinações como Ctrl+C
  funcionam sem multi-touch) e página de símbolos.
- **Barra fixa no rodapé** — scroll pra cima/baixo com passo ajustável, alternância do
  teclado e menu.
- **Menu** — zoom remoto em três camadas independentes (conteúdo das aplicações via
  Xft/DPI, decoração de janela, painel), desconectar, status da conexão, sair.
- **Gerenciador de conexões** — histórico de servidores já usados, formulário pra
  adicionar novos (IP, porta, senha), autenticação VNC clássica e mensagens explícitas
  de erro em conexões que falham.
- **Abertura pela biblioteca** — inicia com um toque na biblioteca do Kindle; SSH só é
  necessário na instalação.

## Requisitos

- Um **Kindle jailbreakado** com suporte a scriptlets (um `.sh` tocável na biblioteca,
  o mecanismo padrão do [jailbreak atual](https://kindlemodding.org/)). Testado num KT5
  (1072×1448). O layout é proporcional e deve se adaptar a outras resoluções, mas
  nenhum outro modelo foi validado.
- Um **Raspberry Pi** — ou qualquer Linux Debian-like com `systemd` — na mesma rede,
  acessível por SSH.
- Pra compilar o cliente: o toolchain de cross-compilation do KindleModding
  (koxtoolchain + KMC SDK) num container. Ver "Compilando o cliente".

## Instalação

### Servidor (Pi)

```bash
scp -r pi/ pi@<ip-do-pi>:/tmp/kindow-pi && ssh -t pi@<ip-do-pi> 'bash /tmp/kindow-pi/install.sh'
```

O [`install.sh`](../pi/install.sh) é idempotente. Instala os pacotes necessários
(TigerVNC, Openbox, tint2, mousepad, xsettingsd), aplica a configuração da sessão sem
sobrescrever personalizações do usuário, habilita os dois serviços (`vnc-kindle` na
porta 5901, `kindow-helperd` na 5910) e verifica que ambos respondem.

### Cliente (Kindle)

Com o binário cross-compilado (ver abaixo) e acesso SSH root ao Kindle:

```bash
./kindle/deploy.sh <ip-do-kindle>
```

Copia o binário e o `libvncclient` pra `/mnt/us/kindow/`, instala o scriptlet
[`kindle/kindow.sh`](../kindle/kindow.sh) em `/mnt/us/documents/` — aparece como um
item "Kindow" tocável na biblioteca — e inicia a aplicação.

### Compilando o cliente

O cliente é escrito em C sobre GTK2 (o toolkit que o firmware do Kindle traz) e
cross-compilado com Meson dentro de um container com o toolchain do KindleModding. Siga
o [tutorial de GTK do KindleModding](https://kindlemodding.org/kindle-dev/gtk-tutorial/)
pra montar o container (koxtoolchain + KMC SDK), e então:

```bash
# Uma vez: cross-compilar o libvncclient vendorizado (submódulo) e instalar no sysroot
# do toolchain. A receita completa e testada está em docs/findings/libvncclient-api.md.
cd vendor/libvncserver && cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=../../cmake/Toolchain-arm-kindlehf-linux-gnueabihf.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<sysroot-do-toolchain>/usr \
  -DWITH_LIBVNCSERVER=OFF -DWITH_LIBVNCCLIENT=ON \
  -DWITH_GCRYPT=OFF -DWITH_OPENSSL=OFF -DWITH_GNUTLS=OFF -DWITH_JPEG=OFF -DWITH_PNG=OFF \
  -DBUILD_SHARED_LIBS=ON
cmake --build build && cmake --install build

# A aplicação
cd app && meson setup build --cross-file <seu-meson-crosscompile.txt> && ninja -C build
```

Os módulos puros têm testes unitários que rodam em qualquer máquina, sem toolchain:

```bash
cd app
cc -std=gnu11 -Wall -Wextra -Isrc src/connection_store.c tests/test_connection_store.c -o /tmp/t && /tmp/t
cc -std=gnu11 -Wall -Wextra -Isrc src/keyboard.c tests/test_keyboard.c -o /tmp/t && /tmp/t
cc -std=gnu11 -Wall -Wextra -Isrc src/pixel_convert.c tests/test_pixel_convert.c -o /tmp/t && /tmp/t
```

## Uso

1. Toque em **"Kindow"** na biblioteca do Kindle. A tela inicial do Kindle permanece
   visível por cerca de três segundos antes da aplicação aparecer; o lançador atrasa a
   abertura pra evitar uma corrida com o redesenho da tela inicial do sistema.
2. Na **tela de conexão**, toque num servidor já usado pra reconectar, ou em **"+"**
   pra informar IP, porta e senha de um novo. Senha em branco significa servidor sem
   autenticação, que é o padrão do `install.sh`. Conexões bem-sucedidas são salvas em
   `/mnt/us/kindow/connections.txt`; a senha é guardada em texto simples (a
   justificativa está em [`app/src/connection_store.h`](../app/src/connection_store.h)).
3. Na **sessão**, o toque interage diretamente com o desktop do Pi. A barra do rodapé
   alterna o teclado e o menu; a página `?123` do teclado tem as teclas de clique
   Esquerdo (arrasto) e Direito.
4. Pra **trocar de servidor ou sair**: menu → "Desconectar do Pi" volta pra tela de
   conexão. Sem sessão ativa, o botão "Menu" da barra vira "Sair".

## Limitações conhecidas

- **Só retrato.** Rotação pra paisagem está planejada
  ([`ideias-futuras.md`](ideias-futuras.md)).
- **Escala de cinza.** O cliente converte todas as cores pra 256 tons de cinza, dos
  quais o painel e-ink distingue efetivamente cerca de 16. Ainda não há dithering;
  degradês suaves podem apresentar faixas.
- **Um único modelo validado.** Só o KT5 (1072×1448) foi testado. O layout é
  proporcional por design, mas outros modelos não foram verificados.
- **Recuperação do screensaver.** A aplicação desliga o screensaver do Kindle enquanto
  roda e o restaura em todo caminho normal de saída. Se o processo morrer sem limpeza
  (SIGKILL, crash), o screensaver permanece desligado até rodar
  `lipc-set-prop -i com.lab126.powerd preventScreenSaver 0` ou reiniciar o device.
- **Atraso na abertura.** Cerca de três segundos a partir do toque na biblioteca,
  como descrito em "Uso".
- **Sem criptografia de transporte.** Só a autenticação VNC clássica, pensada pra rede
  local confiável. Não exponha essas portas pra internet.

## Arquitetura

O cliente segue uma estrutura leve de Ports & Adapters; cada dependência externa fica
isolada atrás de um módulo dedicado.

- [`app/src/main.c`](../app/src/main.c) — wiring: instancia os módulos e conecta os
  callbacks.
- [`app/src/session.c`](../app/src/session.c) — núcleo: ciclo de vida da conexão
  (conectar, reconexão automática, watch do socket), política de resize e despacho de
  entrada. Depende do GLib como event loop; sem GTK, sem VNC.
- [`app/src/ui.c`](../app/src/ui.c) — adapter de apresentação (GTK2/Cairo): janela,
  tratamento de toque, barra do rodapé, painel teclado/menu, telas de conexão. Sem VNC.
- [`app/src/vnc_client.c`](../app/src/vnc_client.c) — o único módulo que usa a
  `libvncclient`.
- [`app/src/kindle_platform.c`](../app/src/kindle_platform.c) — particularidades do
  device: controle do screensaver via `lipc`, a convenção de título de janela exigida
  pelo window manager do Kindle, o diretório de dados.
- Módulos puros com testes unitários (sem GTK, sem VNC):
  [`keyboard.c`](../app/src/keyboard.c) (layout, hit-test, modificadores sticky),
  [`connection_store.c`](../app/src/connection_store.c) (histórico de conexões) e
  [`pixel_convert.c`](../app/src/pixel_convert.c) (conversão de cor pra cinza).
- [`app/src/remote_control.c`](../app/src/remote_control.c) — cliente TCP do
  `kindow-helperd`, o canal lateral de zoom fora do protocolo RFB.
- [`pi/`](../pi/) — o lado servidor: sessão X, serviços, instalador.
- [`kindle/`](../kindle/) — scriptlet de lançamento e script de deploy.
- [`vendor/libvncserver`](../vendor/libvncserver) — submódulo git, pinado em 0.9.15.

## Documentação técnica

- [`findings/`](findings/) — achados técnicos, um arquivo por problema: o protocolo
  RFB e as escolhas de encoding, a API da libvncclient e os bugs contornados nela, a
  seleção do servidor VNC e os problemas que só o teste em hardware revelou.
- [`ideias-futuras.md`](ideias-futuras.md) — a fila do que vem depois, com o raciocínio
  registrado de cada item.
- [`historico-da-poc.md`](historico-da-poc.md) — o registro cronológico da fase de
  prova de conceito.

## Licença

[GPL-3.0](../LICENSE). A `libvncclient` vendorizada é licenciada como
GPL-2.0-or-later, então qualquer binário distribuído já está sujeito aos termos da
GPL; licenciar o projeto como GPL-3.0 mantém o repositório inteiro consistente com a
dependência.
