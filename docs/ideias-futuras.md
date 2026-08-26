# Ideias futuras (não implementadas, pra retomar depois)

Diferente de `docs/findings/` (pesquisa já concluída, com decisão tomada), este arquivo
guarda ideias levantadas em conversa mas **não pesquisadas nem implementadas** — ponto de
partida pra uma sessão futura, não um registro do que já foi decidido.

## 1. Mostrar a mesma sessão "do monitor" (espelhar o desktop físico já existente)

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

## 2. Usar o Kindle como "segunda tela" de verdade (extensão real do desktop, não sessão espelhada)

Diferente da ideia 1 (espelhar), isso seria o Kindle funcionar como um **monitor externo de
verdade** — dá pra arrastar uma janela do monitor físico pro Kindle ao vivo, como faria com
um segundo monitor HDMI. Nem o modelo atual (`Xvnc`, sessão própria) nem o `x11vnc`
(espelhamento) entregam isso — os dois são sessões X independentes ou espelhadas, não "duas
saídas de uma mesma sessão", que é o que "segunda tela" significa de verdade em termos de
X11/Xrandr.

**Esse é o item mais incerto dos dois.** Não é só trocar de servidor VNC — seria precisar de
um mecanismo que faça o VNC se comportar como uma **saída Xrandr adicional de uma sessão já
existente** (o Pi enxergando o Kindle como se fosse um segundo HDMI conectado), o que não é
o modelo padrão de nenhum servidor VNC comum no Linux (nem `Xvnc` nem `x11vnc` foram feitos
pra isso). Antes de tentar implementar, vale uma pesquisa exploratória honesta: **pode ser
que isso simplesmente não exista com ferramentas padrão** — nesse caso o resultado da
pesquisa seria um "beco sem saída" documentado (mesmo padrão dos achados do tipo
`waf-path-dead-end.md` no projeto irmão `kindle`), não uma implementação.

### Sub-item pras duas ideias acima: movimento de mouse

Hoje o Kindow só transmite **cliques discretos** (toque → press+release), sem rastrear
movimento — decisão alinhada ao e-ink (movimento contínuo de ponteiro geraria tempestade de
refresh). Nos dois cenários acima isso precisa ser revisitado: numa sessão espelhada ou
estendida, o ponteiro que se move é o do próprio Pi (mouse físico de verdade), e cada posição
nova vira um update na tela do Kindle. Questões em aberto: suprimir/filtrar o desenho do
cursor nos updates? Throttle de refresh? Mostrar o ponteiro só quando parado? Também
relacionado: mouse físico **no Kindle** esbarra em USB host/OTG não habilitado nesse hardware
— projeto à parte, provavelmente beco sem saída.

## Contexto de onde essas ideias surgiram

Levantadas discutindo a implementação do resize automático de tela (`SetDesktopSize` via
RFB, pra fazer o frame do Pi bater exatamente com a resolução de qualquer Kindle que
conectar) — nessa discussão ficou claro que a arquitetura atual cria uma sessão nova, não
espelha nem estende a existente, e isso levantou as duas perguntas acima como possíveis
próximos passos, não decisões já tomadas.
