# Ideias futuras (não implementadas, pra retomar depois)

Diferente de `docs/findings/` (pesquisa já concluída, com decisão tomada), este arquivo
guarda ideias levantadas em conversa mas **não pesquisadas nem implementadas** — ponto de
partida pra sessões futuras, não um registro do que já foi decidido.

## Fila da parte 2 da PoC (ordenada em 26/08)

**Nota sobre a sessão de 27/08 (noite → manhã)**: os itens 3, 4 e 5 (scroll ajustável,
clique esquerdo/direito, proporção de tela) foram implementados numa sessão em que o
usuário foi dormir e pediu pra eu avançar sozinho. Cross-compilado, deployado, revisado
(reviewer achou um bug real — vazamento do clique-esquerdo-armado entre páginas/painéis
— corrigido e reconfirmado por recheck) e commitado localmente ainda de madrugada, sem
alegar verificação visual nenhuma até aí (regra do projeto: só os olhos do usuário
contam). **Testado e confirmado funcionando pelo usuário ao acordar** — incluindo o item
4 (arrasto/clique contínuo), que era a peça mais arriscada de toda a reestrutura (primeira
vez que o app rastreia movimento contínuo). Os três itens abaixo já refletem esse
"feito", não mais "pendente".

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

3. **Scroll** — **feito, parcial (27/08)**. Saiu diferente do que este item previa: em
   vez de swipe no conteúdo, virou dois botões dedicados (↑/↓) numa barra fixa nova no
   rodapé da tela — decisão tomada numa discussão de desenho sobre ambiguidade de
   gestos (scroll por swipe no conteúdo colidiria com seleção de texto por arrasto, mesmo
   gesto físico disputando a mesma área; ver histórico da sessão de 27/08 e o item de
   Menu abaixo, que a mesma discussão remodelou). `SendPointerEvent` com os bits dos
   botões 4/5, na última posição tocada no conteúdo (`session_send_scroll`,
   `last_touch_x/y` em `session.c`) — um toque = uma "catraca" de roda.

   **Etapa 4 da reestrutura de UI — feito e validado (27/08)**: usuário ajusta quantas
   catracas cada toque manda, por um par -/+ no menu ("Scroll A-"/"Scroll A+").
   Puramente client-side (diferente do zoom, não passa pelo `kindow-helperd`/Pi) —
   `session_get/set_scroll_lines` guardam o valor (default 1, faixa 1-10),
   `session_send_scroll` manda N pares press+release em vez de sempre 1.

4. **Botão direito** — **feito e validado (27/08)**. O
   desenho original (toque longo) foi revisto durante a reestrutura de UI da
   barra/teclado, numa troca de mensagens com o usuário antes de dormir: o timing de
   toque longo nunca chegou a ser validado no hardware, e o padrão que vinha se firmando
   na sessão (botão explícito vence gesto/timing — mesma razão por trás do zoom, do
   scroll acima, e do toggle de painel abaixo) apontou pra uma alternativa mais simples e
   mais confiável — mas o usuário corrigiu a primeira tentativa de desenho (colocar os
   botões na barra fixa): a intenção real dele era a página de SÍMBOLOS do teclado
   (`?123`) ganhar dois botões substituindo só o espaço ALI, mantendo o espaço normal na
   página de letras (o caso comum de digitação continua intocado). Implementado assim:
   `KEY_LEFT_CLICK`/`KEY_RIGHT_CLICK` em `keyboard.c`, teclas "Esquerdo" (sticky: arma
   "clique contínuo" — consultável de fora via `keyboard_left_click_armed`, consumido
   via `keyboard_consume_left_click_arm` quando o toque que usa o arme acontece no
   FRAME, uma área que `keyboard.c` não enxerga) e "Direito" (ação imediata, reporta via
   out-param de `keyboard_handle_tap`, mesma convenção do scroll quanto a alvo — última
   posição tocada). O arrasto em si: `ui.c` ganhou rastreamento de motion/release
   (`GDK_POINTER_MOTION_MASK` + `GDK_POINTER_MOTION_HINT_MASK` pro coalescing padrão do
   GTK2, mais um throttle por distância mínima de 8px não validado/medido), termina
   quando o dedo LEVANTA da tela (sem precisar apertar de novo). `session_send_drag`
   sempre CLAMPA coordenada fora dos limites em vez de descartar (diferente do clique) —
   um arrasto em andamento precisa terminar com release de verdade, ou o botão fica
   preso pressionado do lado do servidor. O que o arrasto significa (mover janela,
   redimensionar, selecionar texto) é decidido pelo servidor (Openbox/GTK) pela posição
   onde começou — nenhuma desambiguação nossa.

   **Validado no hardware pelo usuário (27/08, manhã)**: as três dúvidas que ficaram em
   aberto quando isso foi implementado sem ninguém pra testar — (a) se o Openbox
   realmente move/redimensiona janela a partir de eventos de motion sintetizados via
   VNC, (b) se esse touchscreen rastreia contato contínuo de forma confiável, (c) se o
   throttle de 8px basta pra evitar tempestade de refresh no e-ink — todas confirmadas
   funcionando na prática ("está tudo funcionando"). Testes unitários em
   `test_keyboard.c` cobrem a LÓGICA de `keyboard.c` (Esquerdo só na página de símbolos,
   toggle, consumo externo, Direito reportando sem armar nada); a experiência física do
   arrasto em si só o teste no device confirma, e confirmou.

   **Achado real de review corrigido antes do commit**: `left_click_armed` vazava entre
   páginas/painéis — armar "Esquerdo" na página de símbolos e depois trocar de página
   (via "abc") ou de painel (Teclado↔Menu na barra) deixava o arme vivo **sem indicador
   visual nenhum** (a tecla some junto com a página/painel), fazendo o próximo arrasto
   no frame disparar sem o usuário perceber. Corrigido em dois pontos — `case KEY_PAGE:`
   em `keyboard.c` (troca de letras↔símbolos) e `toggle_panel` em `ui.c` (troca de
   painel) — ambos agora desarmam explicitamente via `keyboard_consume_left_click_arm`,
   idempotente quando já desarmado. Novo teste
   `test_left_click_arm_cleared_by_page_switch` cobre o caminho de `keyboard.c`; o de
   `ui.c` (troca de painel) só é verificável no device, já que `ui.c` não é módulo puro
   testável.

5. **Menu do app** — **feito (27/08, revisado de novo)**. A forma de acessar mudou uma
   segunda vez desde a primeira revisão (26/08, chord Ctrl+Shift + página do teclado):
   virou conteúdo próprio, mutuamente exclusivo com o teclado, dentro de um "painel" que
   ocupa a mesma área da tela — alternado por um botão dedicado "Menu" na barra fixa do
   rodapé (junto de "Teclado" e scroll ↑/↓), com as 3 regras de toggle já implementadas:
   nada aberto → abre o tocado; o outro aberto → troca; o próprio já aberto → fecha (ver
   `PanelMode`/`MenuAction` em `ui.c`). **Sair da aplicação** ✓, **zoom em 3 camadas
   independentes** ✓ (sem mudança nessa parte, ver `pi/kindow-helperd`).

   **Ajuste (27/08, tarde) — botão "Menu" vira "Sair" sem sessão ativa**: substituiu o
   desenho anterior de bloquear o toque no botão "Menu" enquanto não havia sessão (não
   fazia sentido abrir o painel de menu na tela de conexão) — sem uma saída, o usuário
   ficava PRESO na tela de conexão, sem nenhum jeito de sair do app antes de conectar
   (achado do usuário). Agora, sem sessão ativa, o mesmo botão da barra mostra "Sair" e
   fecha a aplicação em vez de abrir o painel de menu (`draw_bar` em `ui.c`); com sessão
   ativa o botão continua "Menu", sem mudança de comportamento. Itens do painel de menu
   também ganharam feedback de toque (pedido do usuário) — piscam ao toque
   (`FLASH_MENU_ITEM`), mesmo mecanismo já usado nas teclas do teclado (item 1 acima).

   **Mostrar/esconder o teclado** ✓ feito também (27/08) — mas não por gesto como esse
   item previa: virou o botão "Teclado" da mesma barra, no mesmo state machine do menu
   (mais confiável que gesto, mesmo padrão da sessão). `SetDesktopSize` é re-pedido a
   cada abertura/fechamento do painel (`session_set_target_size`), então a área do frame
   realmente cresce/encolhe, não só o desenho.

   **Ainda pendente, feito depois (27/08, tarde)**: desconectar do Pi e conectar em
   outro, lembrando os dados de sessões anteriores — item "Desconectar do Pi" no menu
   (`MENU_ACTION_DISCONNECT`) chama `session_disconnect` e volta pra tela de conexão via
   `show_connect_list`, que já reconstrói a lista a partir do histórico persistido
   (`connection_store`, ver item 10 abaixo). Entregue como parte do trabalho de
   persistência/senha do item 10, não como item isolado.

   **Etapa 5 da reestrutura de UI — feito e validado (27/08)**: `BAR_HEIGHT_PX` (60px
   fixos) virou `BAR_HEIGHT_PERCENT` (4% da altura da tela, com piso de 40px pra telas
   hipotéticas muito baixas) — reproduz ~57px no device testado (1072×1448), perto do
   valor original. Insets entre teclas/botões/itens de menu e largura de borda também
   viraram proporcionais (`proportional_inset`/`proportional_border_width` em `ui.c`,
   calculados a partir da altura LOCAL de cada linha/botão, não da tela inteira — mantém
   o mesmo peso visual relativo em qualquer resolução). A fórmula bateu exatamente com o
   esperado no log do device (`keyboard_top=904`, conferido à mão contra a conta:
   `screen_height=1448 → bar_height=max(1448*4/100, 40)=57 → bar_top=1391 →
   keyboard_top=1391*65/100=904`), e o resultado visual (bordas/gaps) foi confirmado
   pelo usuário — continua com a mesma aparência/toque confortável de antes.

   **Fora do escopo desta etapa, por já estar resolvido**: a área do frame (tela do Pi)
   não precisa de conversão nenhuma — ela nunca foi pixel fixo. `ui_frame_width/height`
   pedem ao servidor, via `SetDesktopSize`, exatamente a resolução real detectada em
   runtime (menos o que bar/painel reservam), e o frame que chega é desenhado 1:1
   (`ui_show_frame`, sem escala/distorção) — por construção nunca sobra espaço em branco
   ao redor dele, em nenhum Kindle. O que a etapa 5 muda é só a PROPORÇÃO de quanto do
   total da tela sobra pro frame vs. bar/painel entre devices diferentes — não afeta a
   correção do frame em si, que já está garantida desde a feature de resize automático
   (ver `kindle-hardware-test.md`).

   **Sub-ideia registrada (26/08) — bootstrap do Pi pelo próprio Kindle**: no formulário
   de conexão deste menu, oferecer um checkbox "instalar e iniciar o serviço no Pi": o
   usuário informa IP + usuário + senha, o app abre um SSH pro Pi, verifica se o
   `kindow-helperd` existe, envia os arquivos (de um diretório temporário), inicia o
   serviço, testa e só então conecta — onboarding de um Pi virgem sem tocar em terminal.
   Pré-requisitos técnicos anotados: (a) roteamento de input local — parcialmente
   resolvido pelo mecanismo `MenuAction` que o painel de menu já usa (callback
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

## Registrado em 27/08 (tarde) — pendências da tela de conexão e do empacotamento

Os três itens abaixo (9, 10, 11) foram **implementados e validados no hardware pelo
usuário na mesma tarde de 27/08** — a fila original (não ordenados entre si nem contra
1-8) virou "feito" antes mesmo de ser atacada em ordem. Os dois "Investigar" que seguem
continuam pendentes, sem mudança de código.

9. ~~**Feedback visual de erro na tela "Conectando..."**~~ — **feito e validado
   (27/08)**. `session.c` ganhou o callback `on_connect_attempt_failed`, que conta
   falhas seguidas (`consecutive_failures`, zerado a cada sucesso) e reporta o motivo a
   cada tentativa. `main.c` troca a mensagem via `ui_show_connect_error` só a partir da
   2ª falha seguida (`CONNECT_FAILURES_BEFORE_ERROR`, ~2-4s considerando o retry de 2s
   já existente) — antes disso a tela normal de "Conectando..." continua, pra não
   piscar erro numa demora de rede momentânea. A sessão continua tentando em segundo
   plano mesmo com a mensagem de erro na tela: se uma tentativa posterior vingar, a tela
   de conexão normal volta sozinha. "Voltar" continua disponível o tempo todo.

10. ~~**Suporte e alerta pra senha de VNC**~~ — **feito e validado com servidor real
    (27/08)**. O formulário "+" ganhou uma 3ª fileira com campo Senha mascarado (`*`,
    vazio = sem senha). `vnc_client.c` registra o `GetPassword` da libvncclient — a
    senha precisa existir ANTES de conectar, porque esse callback é síncrono e disparado
    no MEIO do handshake (`InitialiseRFBConnection`), por isso o campo fica no
    formulário em vez de virar um prompt separado. Mensagens de erro específicas:
    "este servidor VNC exige senha..." (sem senha preenchida) e "servidor
    provavelmente recusou a senha" (senha errada — o "provavelmente" é honesto: numa
    janela estreita do handshake a falha pode ser de rede, não de senha, e o código não
    tem como distinguir os dois casos com certeza). A senha persiste no
    `connection_store` como 3º token opcional do formato de texto ("host porta
    [senha]", retrocompatível com arquivos antigos sem senha), em **texto simples** —
    decisão consciente de 27/08: o firmware do Kindle não tem keychain/storage seguro, e
    quem tem SSH ou acesso físico ao device já lê qualquer arquivo do cartão de qualquer
    jeito; ver comentário em `connection_store.h`. Validado de ponta a ponta contra um
    `Xtigervnc` temporário com VncAuth rodando no Pi (porta 5902): sem senha contra
    servidor que exige → mensagem específica ✓; senha errada → recusa (com blacklist
    temporário do TigerVNC gerando mensagens genéricas intermitentes — comportamento do
    servidor, não bug do cliente) ✓; senha certa → conectou, frames fluindo, persistiu
    no histórico ✓. `test_connection_store.c` tem 21 casos, incluindo senha no limite de
    tamanho, truncamento, token extra, eviction preservando senha, round-trip e
    retrocompatibilidade com arquivos sem senha.

    **Ideia registrada (27/08) — criptografar a senha no arquivo**: hoje é texto
    simples por decisão consciente (ver acima). Criptografar exigiria decidir ONDE
    guardar a chave num firmware sem keychain — sem isso, na prática seria só
    ofuscação (quem tem acesso ao arquivo da senha cifrada também teria acesso à chave
    guardada do lado), valor de segurança limitado. Fica como ideia a considerar, não
    como plano — não há decisão de implementar isso ainda.

11. ~~**Pacote/launcher pra abrir sem SSH**~~ — **feito e validado (27/08)**, mas o
    mecanismo NÃO é KUAL como este item previa: KUAL não está disponível nesse
    jailbreak (confirmado no projeto irmão `kindle/`, `docs/findings/appmgrd-return-to-dashboard.md`
    de lá — o repositório oficial desse jailbreak só tem koreader/kpm/kompanion/kterm,
    KUAL é "Legacy"). O caminho real foi um **scriptlet** (`kindle/kindow.sh`, novo no
    repo): um `.sh` com cabeçalho `# Name: Kindow` copiado pra
    `/mnt/us/documents/` aparece tocável na biblioteca do Kindle, mesmo mecanismo do
    `pet_dashboard`/`kterm` oficiais. Dois achados reais no caminho: (a) `setsid` +
    `nohup` são necessários — quem executa o `.sh` via scriptlet é um processo interno
    do jailbreak, e o fim dele derruba o grupo de processos sem esse desacoplamento;
    (b) o pulo do gato: um `sleep 3` ANTES do `exec`, dentro do subshell — quando o
    script termina, o framework do Kindle "volta pra Home", redesenhando-a POR CIMA de
    tudo; sem o delay, a janela do Kindow (que mapeia quase instantaneamente, é a tela
    de conexão local sem rede) aparecia por ~1s e era coberta logo em seguida (o
    `pet_dashboard` escapa por acaso — demora mais pra subir porque busca dados na rede
    antes). Também novo: `kindle/deploy.sh` (build → scp do binário + libvncclient +
    scriptlet → relança o processo, uso: `./kindle/deploy.sh <ip>`).

### Investigar: recuperação do screensaver em crash abrupto (SIGKILL/kernel panic)

O app desativa o screensaver do Kindle no início (`lipc-set-prop -i com.lab126.powerd
preventScreenSaver 1`, `kindle_platform_keep_awake(true)`) e restaura no encerramento
limpo — `SIGTERM`/`SIGINT` tratados via `g_unix_signal_add_watch_full`, mais o
`gtk_main_quit` do fechamento de janela, todos passando pela limpeza pós-`gtk_main`
(`kindle_platform_keep_awake(false)`). `SIGKILL` (ou um crash que mate o processo sem
rodar handler nenhum) pula essa limpeza — a trava de screensaver fica presa em "1" até
reboot ou reversão manual (`lipc-set-prop -i com.lab126.powerd preventScreenSaver 0`).
A perguntar antes de decidir o que fazer: vale só documentar o comando de emergência
(ex. num README do lado do Pi/Kindle), ou vale um script de verificação/reset rodado no
boot do Kindle (ex. via upstart) que zera a trava se o `kindow-client` não estiver
rodando? A segunda opção evita depender do usuário lembrar do comando manual, mas é
mais código novo pra um cenário que só acontece em crash/kill -9, não no uso normal.

### Investigar: dithering espacial na conversão de escala de cinza

`app/src/pixel_convert.c` converte as cores recebidas do Pi pra escala de cinza
matematicamente "perfeita" (256 níveis), mas o painel físico do Kindle só distingue de
verdade uns 16 níveis (4 bits) — a conversão 1:1 de hoje não faz dithering nenhum
(Floyd-Steinberg, Bayer/ordered dithering, etc.), então gradientes e sombras da UI do
desktop remoto (ex. sombra de janela do Openbox, ícones com transparência) podem sair
com "faixas" duras de transição de tom em vez de um degradê suave. Precisa validar
primeiro se isso é perceptível de verdade no hardware (o e-ink já tem um jeito
particular de renderizar cinza que pode mascarar ou acentuar o banding, não dá pra
saber só olhando o código) antes de decidir se vale a complexidade de um dithering
pré-calculado na LUT — achado a confirmar visualmente no device antes de qualquer
implementação, mesmo espírito de honestidade das outras seções deste arquivo.

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
