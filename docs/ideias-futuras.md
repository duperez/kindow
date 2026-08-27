# Ideias futuras (não implementadas, pra retomar depois)

Diferente de `docs/findings/` (pesquisa já concluída, com decisão tomada), este arquivo
guarda ideias levantadas em conversa mas **não pesquisadas nem implementadas** — ponto de
partida pra sessões futuras, não um registro do que já foi decidido.

## Fila da parte 2 da PoC (ordenada em 26/08)

A parte 1 (provar que o Kindle funciona como tela interativa sem fio pro Pi) está fechada.
Estes são os próximos itens, ordenados por **o que faz mais sentido agora** e, como critério
de desempate, **o que é mais simples de fazer**:

1. ~~**Teclado virtual**~~ — **feito (26/08)**: faixa fixa na parte de baixo da tela
   (35%), teclas desenhadas com Cairo, sticky Shift/Ctrl com rótulos que mudam quando o
   Shift arma, página de símbolos, e a área útil do frame remoto ajustada via o resize
   automático que já existia. Módulo puro `keyboard.c` (layout/hit-test/estado) +
   `SendKeyEvent` no `vnc_client`/`session` + desenho e roteamento no `ui`. De carona, o
   teste no hardware revelou e resolveu a travadinha do Enter (encoding Raw → ZRLE, ver
   `findings/rfb-protocol.md`).

   **Polimento pendente anotado (26/08), feito no mesmo dia**: feedback visual de toque
   nas teclas — tecla normal/ação pisca invertida (mesmo visual das sticky Shift/Ctrl
   armadas, só que momentânea, ~180ms — `KEY_FLASH_MS` em `ui.c`); sticky/página não
   piscam, porque o feedback delas já é o próprio estado. Redraw só do retângulo da tecla
   (não a faixa inteira). Cuidado de e-ink que motivou a checagem — custo de dois redraws
   parciais por toque atrapalhar digitação rápida — **validado no hardware real: não
   atrapalha**.

2. ~~**GUI mais complexa com editor de texto**~~ — **feito (26/08)**: `l3afpad` no lugar
   do `xterm` no `xstartup` do Pi (server-side, cliente intocado), com `Xft.dpi: 192` via
   `xrdb` pra compensar os ~300dpi físicos do Kindle (fonte E menus 2x, agnóstico de
   toolkit) e matchbox sem titlebar (`-use_titlebar no` — o X de fechar só derrubava a
   sessão, que o systemd religa; sair de verdade é papel do futuro menu do Kindow).
   Digitação, menus e diálogos validados por toque no device.

   **Desdobramento anotado (26/08) — desktop completo com gerência de janelas**: quando
   evoluir de app único pra desktop de verdade (fechar/mover/minimizar janelas por toque),
   o matchbox sai de cena por design (é WM de quiosque: não move nem redimensiona janela).
   Candidato: **Openbox** — decorações escalam com a fonte do título (que segue o
   `Xft.dpi` já configurado), botões de janela em tamanho de dedo via config. Junto vêm:
   painel/taskbar pra minimizar/alternar (ex. `tint2`), mover janela só por contorno
   (arrasto opaco = tempestade de refresh no e-ink), e a dependência do item de
   scroll/arrasto no cliente (mover titlebar exige press-move-release, mesmo mecanismo do
   swipe do item 3).

3. **Scroll** — hoje não há como rolar conteúdo remoto a partir do Kindle. Em VNC, scroll
   é "botão de roda": `SendPointerEvent` com os bits dos botões 4 (cima) / 5 (baixo) — o
   mecanismo de envio já existe, o trabalho é o gesto. Idealmente **swipe** na área do
   frame (arrastar pra cima/baixo), com fallback de **botões** dedicados se o gesto não
   funcionar bem. Atenção de design pro e-ink: swipe contínuo tipo celular geraria
   tempestade de refresh — melhor gesto discreto (um swipe = N linhas de roda), na mesma
   filosofia do "clique discreto" atual. Também exige separar toque-que-é-clique de
   toque-que-é-arrasto no `ui` (threshold de movimento), o que de quebra prepara o terreno
   pro toque longo do botão direito.

4. **Botão direito** — um toque longo (segurar o dedo) vira clique direito
   (`button_mask` com o bit do botão 3, no mesmo `SendPointerEvent` que já existe). Quase
   todo o trabalho é client-side e pequeno: detecção de toque longo no `ui.c` + um
   `session_send_right_click`. É o item mais simples da fila, mas ficou **depois da GUI**
   de propósito (revisão de 26/08): no `xterm` puro o botão direito não faz nada de útil
   (só estende seleção) — só passa a valer a pena quando existir uma GUI onde clique
   direito significa alguma coisa.

5. **Menu do app** — **parcialmente feito (26/08)**. Saiu diferente do que este item
   previa: em vez de overlay/gesto, virou uma terceira página do teclado virtual (chord
   Ctrl+Shift + tecla de página, rótulo "Menu" quando arma — ver "Próximos passos" no
   README). **Sair da aplicação** ✓ feito (hoje só via SSH deixou de ser verdade). **Mudar
   tamanho de fonte** ✓ feito, e foi além do pedido original: virou zoom remoto em três
   camadas independentes (Apps/Janela/Painel), não só uma fonte única — ver
   `pi/kindow-helperd`. A sub-questão que este item deixava em aberto ("tamanho de fonte
   hoje é configuração do `xterm` no servidor, mudar do cliente exige mecanismo próprio")
   está **respondida**: o mecanismo é o `kindow-helperd`, um canal de comando TCP lateral
   que o RFB não cobre.

   **Ainda pendente** (o que sobrou do item original): desconectar do Pi e conectar em
   outro (pedindo IP/porta e demais dados necessários), lembrando os dados da última
   sessão (persistência local em `/mnt/us/kindow/`); e, como opção configurável,
   **mostrar/esconder o teclado virtual por gesto** (decisão de 26/08: o teclado nasceu
   como faixa fixa reservada — melhor pro e-ink, zero refresh extra —, mas a variante
   overlay/toggle fica como escolha futura do usuário via este menu, não como substituição
   da faixa fixa).

   **Sub-ideia registrada (26/08) — bootstrap do Pi pelo próprio Kindle**: no formulário
   de conexão deste menu, oferecer um checkbox "instalar e iniciar o serviço no Pi": o
   usuário informa IP + usuário + senha, o app abre um SSH pro Pi, verifica se o
   `kindow-helperd` existe, envia os arquivos (de um diretório temporário), inicia o
   serviço, testa e só então conecta — onboarding de um Pi virgem sem tocar em terminal.
   Pré-requisitos técnicos anotados: (a) roteamento de input local — parcialmente resolvido
   pelo mecanismo `KEY_ACTION`/`KeyboardAction` que a página de menu já usa (callback
   `ui`→`main`, nunca vai pro servidor), mas isso cobre botões de ação fixos, não um
   formulário de texto livre pra digitar IP/usuário/senha, que ainda não existe; (b) SSH a
   partir do Kindle (o jailbreak traz dropbear/`dbclient`, mas automatizar senha exige
   lidar com pty — não tem `sshpass` no firmware), (c) o instalador idempotente em
   `pi/install.sh` já existe e é o que o bootstrap reaproveitaria por baixo.

6. **Mudar orientação da tela (paisagem)** — rotação de 90° feita **no cliente**, dentro
   do pipeline que já é nosso: rotacionar na conversão de pixel, transformar as coordenadas
   de toque, e pedir o `SetDesktopSize` com largura/altura trocadas. Importante: fazer no
   cliente contorna o bloqueio de rotação do X11 do Kindle já documentado no projeto irmão
   (`../kindle/docs/findings/landscape-orientation-blocked.md`) — o X do Kindle não precisa
   girar nada. Complexidade moderada e independente dos demais itens.

7. **Espelhar a sessão física do Pi** — exploratória, ver seção detalhada abaixo. Envolve
   revisitar a escolha TigerVNC→x11vnc e pesquisa de suporte a resize; por isso fica atrás
   dos itens de implementação direta.

8. **Kindle como segunda tela de verdade** — a mais incerta de todas (pode ser beco sem
   saída com ferramentas padrão), ver seção detalhada abaixo. Só atacar com pesquisa
   exploratória honesta antes de qualquer implementação.

## Exploratórias (detalhes dos itens 7 e 8)

### Mostrar a mesma sessão "do monitor" (espelhar o desktop físico já existente)

Hoje o Kindow usa TigerVNC no modo `Xvnc`: ele **cria uma tela X virtual do zero**,
totalmente independente de qualquer sessão que já esteja rodando no monitor físico do Pi
(decisão registrada em [`findings/pi-vnc-server.md`](findings/pi-vnc-server.md), justamente
pra não depender de uma sessão X pré-existente). Ou seja: hoje o Kindle **nunca** vê o que
está no monitor real do Pi — vê uma sessão paralela, criada só pra ele.

Pra mostrar de verdade a sessão que já está no monitor (com o WM que já roda lá, os apps que
já estão abertos, etc.), a arquitetura precisaria trocar de `Xvnc` pro **`x11vnc`** — que
exporta uma sessão X **já em execução**, em vez de criar uma nova.

**O que precisa ser pesquisado antes de implementar:**
- Se `x11vnc` suporta a mesma extensão de protocolo que usamos pro resize dinâmico
  (`SetDesktopSize`/`ExtDesktopSize`, ver seção de resize automático no histórico do
  projeto) — suspeita forte de que o suporte é mais fraco ou inexistente ali, já que
  `x11vnc` não é dono da tela X do jeito que o `Xvnc` é (não pode simplesmente pedir ao
  Xrandr pra redimensionar uma sessão que ele não criou, com a mesma liberdade). Não
  verificado ainda — só suspeita, baseada em como os dois servidores se posicionam
  historicamente.
- Seria basicamente desfazer a escolha original documentada em `pi-vnc-server.md` (TigerVNC
  em vez de x11vnc) — vale revisitar esse trade-off explicitamente, não só trocar sem
  reconsiderar o motivo original.
- Seguranças diferentes: uma sessão física já em uso pode ter outras implicações de
  segurança/privacidade que a sessão virtual isolada de hoje não tinha (alguém vendo o
  Kindle veria literalmente o que está na tela do Pi, incluindo o que já estava lá antes de
  qualquer decisão de expor via VNC).

### Usar o Kindle como "segunda tela" de verdade (extensão real do desktop, não sessão espelhada)

Diferente de espelhar, isso seria o Kindle funcionar como um **monitor externo de
verdade** — dá pra arrastar uma janela do monitor físico pro Kindle ao vivo, como faria com
um segundo monitor HDMI. Nem o modelo atual (`Xvnc`, sessão própria) nem o `x11vnc`
(espelhamento) entregam isso — os dois são sessões X independentes ou espelhadas, não "duas
saídas de uma mesma sessão", que é o que "segunda tela" significa de verdade em termos de
X11/Xrandr.

**Esse é o item mais incerto de todos.** Não é só trocar de servidor VNC — seria precisar de
um mecanismo que faça o VNC se comportar como uma **saída Xrandr adicional de uma sessão já
existente** (o Pi enxergando o Kindle como se fosse um segundo HDMI conectado), o que não é
o modelo padrão de nenhum servidor VNC comum no Linux (nem `Xvnc` nem `x11vnc` foram feitos
pra isso). Antes de tentar implementar, vale uma pesquisa exploratória honesta: **pode ser
que isso simplesmente não exista com ferramentas padrão** — nesse caso o resultado da
pesquisa seria um "beco sem saída" documentado (mesmo padrão dos achados do tipo
`waf-path-dead-end.md` no projeto irmão `kindle`), não uma implementação.

### Sub-item pras duas exploratórias acima: movimento de mouse

Hoje o Kindow só transmite **cliques discretos** (toque → press+release), sem rastrear
movimento — decisão alinhada ao e-ink (movimento contínuo de ponteiro geraria tempestade de
refresh). Nos dois cenários acima isso precisa ser revisitado: numa sessão espelhada ou
estendida, o ponteiro que se move é o do próprio Pi (mouse físico de verdade), e cada posição
nova vira um update na tela do Kindle. Questões em aberto: suprimir/filtrar o desenho do
cursor nos updates? Throttle de refresh? Mostrar o ponteiro só quando parado? Também
relacionado: mouse físico **no Kindle** esbarra em USB host/OTG não habilitado nesse hardware
— projeto à parte, provavelmente beco sem saída.

## Contexto de onde essas ideias surgiram

As exploratórias (espelhar/segunda tela) surgiram discutindo a implementação do resize
automático (`SetDesktopSize` via RFB); os itens 1–5 da fila foram definidos em 26/08, ao
fechar a parte 1 da PoC, como escopo da parte 2.
