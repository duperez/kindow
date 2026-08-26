# Servidor VNC no lado do Pi

## Contexto

O Pi 3 Model B (1GB RAM, Raspberry Pi OS Lite, Debian Bookworm) está totalmente headless hoje
— sem X11, sem ambiente gráfico nenhum instalado. Já roda dois serviços `systemd` (API Node.js
do projeto `kindle`, `ttyd`) com ~700MB livres de RAM em regime normal. Precisamos decidir: qual
servidor VNC, o que expor como conteúdo mínimo, e como isso encaixa no orçamento de recursos.

## Decisão: TigerVNC (`Xvnc`), não `x11vnc`

A diferença arquitetural que decide isso: `x11vnc` **compartilha uma sessão X11 já rodando** —
precisa de um servidor X de verdade rodando antes dele conseguir fazer qualquer coisa. Já o
`Xvnc` (do TigerVNC) **é ele mesmo um servidor X completo** — cria a própria tela virtual, não
precisa de nada rodando antes.

Como esse Pi não tem absolutamente nada gráfico hoje, isso muda a conta: com `x11vnc`
precisaríamos de dois componentes com dependência de ordem de inicialização (X primeiro, depois
`x11vnc` anexado nele). Com TigerVNC, um processo só já cria a tela **e** já serve ela via VNC.
Menos peças móveis pro mesmo resultado final.

Tamanho de pacote é parecido entre os dois (~1MB baixado, ~2-3MB instalado) — não é o que
decide. O que decide é que a lista de dependências do `x11vnc` é toda sobre *falar com* um
servidor X que já existe (Xlib, Xext, Xdamage...); a do TigerVNC inclui coisas como
`libxfont2` porque o `Xvnc` *é* o servidor X, precisa rasterizar fonte e tudo mais sozinho.

**Não instalar `xfce4`/`xfce4-goodies`** — isso é ambiente de desktop completo, muita coisa
desnecessária pra essa PoC.

## O que expor: um `xterm`, sob `matchbox-window-manager`

Decisão original desta pesquisa: rodar X sem WM, um único `xterm` sempre em tela cheia — padrão
real e comum (é como praticamente todo setup headless de CI/browser automatizado funciona).

**Revisado em 26/08**: sem WM, o `-fullscreen` do `xterm` não era de fato aplicado (sobrava fundo
preto do X ao redor) — o item ficou registrado como pendência no README até então. Resolvido
instalando o **`matchbox-window-manager`** (`apt install matchbox-window-manager`) — feito
especificamente pra "plataformas embarcadas sem desktop: handhelds, quiosques" com uma janela por
vez, em tela cheia, o que bate exatamente com o formato do nosso cliente (touch, um app por vez).
Com o WM presente, o `-fullscreen` do `xterm` passa a preencher a tela de verdade, e acompanha o
resize automático do frame (ver `SetDesktopSize` em
[`kindle-hardware-test.md`](kindle-hardware-test.md)).

Descartado: expor um console de texto puro em vez de X11 — VNC/RFB é fundamentalmente sobre
compartilhar um framebuffer de tela gráfica; não é um modo padrão de nenhum dos dois servidores
avaliados.

## Orçamento de recursos — medido no hardware real

A estimativa original (baseada em dados antigos/genéricos) era otimista. Medição real no Pi 3B,
via `ps`/`free`, com `Xtigervnc` 1.15 + `xterm` rodando e um cliente conectado:

| Componente | RAM estimada (antes) | RAM real medida |
|---|---|---|
| `Xvnc`/`Xtigervnc` | ~15-30MB | **~72MB** |
| `xterm` | ~10-15MB | **~10MB** |
| **Total** | ~25-45MB | **~82MB** |

Mesmo assim, cabe folgado: 662MB disponíveis (`free -h`) depois de tudo no ar, dos ~700MB livres
de base. A diferença pro estimado provavelmente vem do `Xtigervnc` moderno carregar mais coisa
(fontes, extensões) que as versões antigas de `Xvnc` que os dados genéricos assumiam.

## Passos concretos de instalação

```bash
sudo apt update
sudo apt install -y tigervnc-standalone-server tigervnc-common xterm
```

Senha (**não** é a senha de login Linux, é um armazenamento próprio do TigerVNC):

```bash
vncpasswd
```

Atenção: o `VncAuth` **trunca a senha em 8 caracteres** silenciosamente — usar algo curto,
seguindo o mesmo padrão de credencial simples que já usamos no SSH e no `ttyd` desse Pi (rede
doméstica confiável, sem exposição externa — não faz sentido usar TLS/`-SecurityTypes` mais
robusto aqui, seria inconsistente com o resto da postura de segurança do projeto).

`xstartup` apontando só pro `xterm` (não o desktop completo padrão) — no TigerVNC 1.15+ o
diretório de config é `~/.config/tigervnc/`, não `~/.vnc/` (ver pegadinha abaixo), e o `xterm`
precisa ser o processo final do script via `exec`, sem `&`:

```bash
mkdir -p ~/.config/tigervnc
cat > ~/.config/tigervnc/xstartup << 'EOF'
#!/bin/sh
exec xterm -geometry 100x30+0+0 -fullscreen
EOF
chmod +x ~/.config/tigervnc/xstartup
```

**Atualizado em 26/08** com `matchbox-window-manager` (ver seção acima) e fonte TrueType
configurável (`-fa`/`-fs`, resolvendo o texto pequeno em relação à tela do Kindle — tamanho 18
como ponto de partida, ajustável), rodando o WM antes do `xterm`:

```bash
cat > ~/.config/tigervnc/xstartup << 'EOF'
#!/bin/sh
matchbox-window-manager &
exec xterm -fa 'DejaVu Sans Mono' -fs 18 -fullscreen
EOF
```

## Pegadinha real: TigerVNC 1.15+ mudou como interpreta o `xstartup`

Encontrado rodando de verdade no Pi (Debian 13/trixie vem com TigerVNC 1.15). Duas mudanças de
comportamento em relação ao que a documentação mais antiga assume:

1. **Caminho de config migrou de `~/.vnc/` pra `~/.config/tigervnc/`** (padrão XDG). O
   `vncserver` tenta migrar sozinho na primeira execução, mas isso **falha silenciosamente** se
   `~/.config/` ainda não existir — mensagem `Could not migrate /home/pi/.vnc to
   /home/pi/.config/tigervnc`, servidor não sobe. Bastou criar o diretório (`mkdir -p ~/.config`)
   antes de rodar pela primeira vez.
2. **O wrapper agora monitora o processo do próprio script `xstartup`, não mais quanto tempo a
   sessão gráfica dura.** Um `xstartup` no formato clássico —
   ```sh
   #!/bin/sh
   xterm -geometry 100x30+0+0 -fullscreen &
   ```
   — dispara o xterm em background e o **script termina na hora**. O wrapper interpreta isso como
   "sessão encerrada cedo demais" (`cleanly exited too early (< 3 seconds)`) e mata o `Xvnc`
   praticamente na mesma hora — o xterm às vezes nem chega a conectar no display a tempo
   (`Xt error: Can't open display`).

   **Correção**: usar `exec` sem `&`, pro processo do script *virar* o processo do xterm em vez
   de só disparar e sair:
   ```sh
   #!/bin/sh
   exec xterm -geometry 100x30+0+0 -fullscreen
   ```
   Com isso o wrapper considera a sessão "viva" enquanto o xterm existir — que é o comportamento
   certo pro nosso caso de um único app.

## Serviço systemd

Duas opções — o pacote Debian já vem com uma unit template (`tigervncserver@.service` +
`/etc/tigervnc/vncserver.users`), mas pra manter consistência com o padrão já usado nesse
projeto (unit própria, `[Unit]`/`[Service]`/`[Install]`, `Restart=on-failure`, mesmo formato da
API Node.js e do `ttyd`), uma unit própria funciona igual de bem:

```ini
# /etc/systemd/system/vnc-kindle.service
[Unit]
Description=Servidor TigerVNC para a PoC de tela sem fio do Kindle
After=network.target

[Service]
Type=forking
User=pi
WorkingDirectory=/home/pi
ExecStartPre=-/usr/bin/vncserver -kill :1 > /dev/null 2>&1
ExecStart=/usr/bin/vncserver :1 -geometry 1024x758 -depth 24 -localhost no
ExecStop=/usr/bin/vncserver -kill :1
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

Pontos importantes desse unit:
- `Type=forking` porque o wrapper `vncserver` inicia o `Xvnc` em background e o próprio wrapper
  sai — assim o `Restart=on-failure` detecta corretamente uma queda de verdade do `Xvnc`, não
  confunde com a saída normal do wrapper.
- `-localhost no` é **obrigatório** — por padrão o TigerVNC só aceita conexão local, o que
  bloquearia silenciosamente o Kindle de conectar pela rede.
- `-geometry` precisa bater (aproximadamente) com a resolução real da tela do Kindle que formos
  usar — ajustar quando soubermos o modelo exato.

## Resumo da decisão

TigerVNC (`Xvnc`) + `xterm` sob `matchbox-window-manager` (revisão de 26/08 — ver seção "O que
expor" acima), unit `systemd` própria seguindo o padrão já estabelecido no projeto `kindle`.
