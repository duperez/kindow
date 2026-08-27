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

2. **GUI mais complexa com editor de texto** — trocar o `xterm` por um editor (ou um WM
   leve com apps) no `xstartup` do Pi. Quase todo o trabalho é server-side e simples — o
   cliente não muda nada, é só conteúdo novo na mesma tela. Vem depois do teclado porque
   um editor sem teclado não serve pra nada.

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

5. **Menu do app** — um menu local no Kindle (overlay ou gesto reservado) com: sair da
   aplicação (hoje só via SSH), desconectar do Pi e conectar em outro (pedindo IP/porta e
   demais dados necessários), lembrando os dados da última sessão (persistência local em
   `/mnt/us/kindow/`), e mudar tamanho de fonte. É o maior item client-side da fila
   (UI de formulário + estados de conexão + persistência), por isso vem depois dos ganhos
   rápidos. Sub-questão a pesquisar: tamanho de fonte hoje é configuração do `xterm` no
   *servidor* (`-fs`, ver `findings/pi-vnc-server.md`) — mudar a partir do cliente exige um
   mecanismo próprio (a investigar; não é um simples ajuste local). Também entra aqui, como
   opção configurável: **mostrar/esconder o teclado virtual por gesto** (decisão de 26/08:
   o teclado nasceu como faixa fixa reservada — melhor pro e-ink, zero refresh extra —, mas
   a variante overlay/toggle fica como escolha futura do usuário via este menu, não como
   substituição da faixa fixa).

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
