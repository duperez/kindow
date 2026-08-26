# Protocolo RFB (RFC 6143) — referência pra usar o libvncclient

Base técnica pra entender o que o `libvncclient` faz por baixo dos panos, o suficiente pra usar
a API dele direito sem precisar implementar o protocolo na mão. Números de seção referem-se à
RFC 6143.

## Handshake — quatro trocas em sequência fixa

`ProtocolVersion → Security → SecurityResult → ClientInit/ServerInit`. O `rfbInitClient()` do
`libvncclient` cuida disso tudo sozinho; só precisamos saber o resultado final:

- **ProtocolVersion**: servidor manda `"RFB 003.008\n"` (12 bytes), cliente ecoa de volta. Contra
  x11vnc/TigerVNC vamos falar 3.8 (o mais comum hoje).
- **Security**: no 3.8, servidor lista os tipos suportados, cliente escolhe um (ver decisão
  abaixo).
- **SecurityResult**: 4 bytes, OK ou falha.
- **ServerInit**: aqui vem o que importa pra gente — largura/altura do framebuffer e o
  `PIXEL_FORMAT` padrão do servidor (16 bytes: bits-por-pixel, profundidade, true-colour,
  max/shift de R/G/B). O `libvncclient` expõe isso em `client->width`, `client->height`,
  `client->format`.

## Decisão: tipo de segurança `None`

Existem dois tipos relevantes: `None` (nada, sem handshake extra) e `VNC Authentication`
(desafio-resposta DES de 16 bytes, senha limitada a 8 caracteres significativos — limitação real
do protocolo, não bug de implementação).

**Escolhido: `None`.** Motivo: mesmo com autenticação, o RFB **não criptografa nada** depois do
handshake — toda a sessão (tela, teclas digitadas) trafega em texto claro de qualquer forma,
porque a criptografia de sessão não faz parte do protocolo base. Ou seja, autenticação VNC não
compra confidencialidade nenhuma numa rede doméstica confiável — só adiciona gerenciamento de
senha de 8 caracteres num teclado virtual de e-ink, sem ganho real. Bate com a postura de
segurança que já usamos no resto do projeto (SSH e `ttyd` do Pi também são simples/LAN-only). Se
um dia precisar de confidencialidade de verdade, o caminho certo é um túnel (SSH/WireGuard), não
VNC Authentication — nenhum dos dois tipos de segurança do RFB cobre isso.

## Formato de pixel — não existe "escala de cinza" nativa no protocolo

O `PIXEL_FORMAT` só tem dois modos: RGB de cor verdadeira, ou paleta indexada. Não tem flag de
"grayscale". Duas opções reais:
1. Pedir um formato reduzido (8bpp) — economiza banda (4x menos bytes que 32bpp), mas ainda é
   RGB quantizado, não cinza de verdade.
2. Aceitar o formato padrão do servidor e converter pra escala de cinza + dithering do nosso
   lado, no cliente.

**Escolhido: opção 2 pra PoC.** Aceitar o padrão (ou 16bpp), converter client-side. Evita
complexidade de modo paleta, e é o caminho mais testado da biblioteca (o padrão dela). Revisitar
8bpp depois só como otimização de banda, se necessário — troca pequena e isolada (uma struct +
uma chamada), não decisão de arquitetura.

## Encodings — só `Raw` (+ talvez `CopyRect`)

`Raw` é o fallback obrigatório: mesmo que a gente não peça, um servidor pode mandar retângulos
`Raw` de qualquer forma — então o decodificador precisa suportar de qualquer jeito. É a mais
simples de raciocinar (sem estado de compressor, sem casos extremos de tile), e como a tela só
atualiza sob demanda (não é vídeo contínuo), a eficiência que RRE/Hextile/ZRLE trazem importa bem
menos aqui do que numa sessão VNC interativa normal. `CopyRect` custa quase nada a mais (só um
"copia daqui pra ali" dentro do próprio framebuffer, sem pixel novo). Deferir o resto.

## Mensagens mínimas necessárias

- `FramebufferUpdate` (servidor→cliente): lista de retângulos com x/y/largura/altura/encoding.
  O `libvncclient` já entrega isso decodificado via callback `GotFrameBufferUpdateProc`.
- `FramebufferUpdateRequest` (cliente→servidor): `incremental=0` força reenvio completo (primeiro
  pedido, ou depois de reconectar); `incremental≠0` pede só o que mudou (caso comum: usuário
  toca "atualizar"). `SendFramebufferUpdateRequest()`/`SendIncrementalFramebufferUpdateRequest()`.
- `SetPixelFormat` + `SetEncodings`: configurados via `client->format` e uma chamada só,
  `SetFormatAndEncodings(client)`.
- `PointerEvent`: posição absoluta + máscara de botão, sempre reenviada inteira (não é delta).
  `SendPointerEvent(client, x, y, buttonMask)`.
- `KeyEvent`: identifica tecla por **keysym do X11** (não scancode) — down e up são mensagens
  separadas, o cliente é responsável por mandar as duas. `SendKeyEvent(client, keysym, down)`.

## Achado importante: atualização sob demanda é o próprio design do protocolo

Confirmado direto na RFC: **"o protocolo de atualização é guiado pela demanda do cliente... o
servidor não deve mandar atualizações não solicitadas."** Não é um "jeito de forçar" o RFB a se
comportar bem com e-ink — é literalmente como o protocolo foi desenhado. Um cliente que pede uma
vez, fica quieto por uma hora lendo a última tela, e pede de novo, é 100% compatível com o
protocolo — visto do lado do servidor, é indistinguível de um cliente "lento" qualquer.

**Pegadinhas reais, fora do protocolo em si:**
- Timeout de conexão ociosa é decisão de cada servidor, não do protocolo. Checamos x11vnc
  especificamente: o `-timeout` dele só vale pra conexão *inicial*, não desconecta sessão já
  estabelecida ociosa. TigerVNC também não tem desconexão por ociosidade por padrão. Vale
  confirmar ao vivo quando decidirmos o servidor, mas não parece ser problema.
- O risco real está no **rádio WiFi do Kindle entrando em modo economia de energia e derrubando
  a associação** enquanto o device fica parado entre toques do usuário — isso é problema do lado
  do cliente, não do protocolo.

**Recomendação prática (original desta pesquisa)**: reconectar do zero (TCP + handshake completo)
a cada interação do usuário, em vez de tentar manter um socket vivo indefinidamente durante longos
períodos ocioso. Como o handshake é barato (poucos round-trips, sem custo de autenticação já que
usamos `None`), reconectar por interação é mais simples e robusto que gerenciar conexão ociosa de
longa duração — e evita de vez a incerteza sobre comportamento de timeout de cada servidor.

**Revisão (teste em hardware real)**: essa recomendação foi revisada depois de medir o custo real
de reconectar — o TigerVNC manda um primeiro burst vazio em toda conexão nova, e só ~1-2s depois
vem o conteúdo de verdade (Achado #2 de
[`kindle-hardware-test.md`](kindle-hardware-test.md)), o que tornava reconectar a cada interação
caro na prática, não barato como estimado aqui. O modelo atual é **conexão persistente**: conecta
uma vez, chama `vnc_client_start_updates()` uma vez, e a própria `libvncclient` mantém sozinha um
pedido incremental sempre em andamento (`HandleRFBServerMessage` chama
`SendIncrementalFramebufferUpdateRequest` internamente depois de cada `FramebufferUpdate` —
`rfbclient.c` ~linha 2564) — o servidor só responde quando o conteúdo muda, então continua sendo
push de verdade, não polling, compatível com a decisão de atualização sob demanda acima. O risco
original que motivou "reconectar por interação" (WiFi do Kindle dormindo e derrubando a conexão
ociosa) continua real, só que tratado de outro jeito: reconexão automática com timer de 2s se a
conexão cair, em vez de reconectar preventivamente a cada interação. Detalhe completo em
[`kindle-hardware-test.md`](kindle-hardware-test.md).

## Resumo das decisões

| Aspecto | Decisão |
|---|---|
| Segurança | `None` |
| Formato de pixel | Aceitar padrão do servidor, converter pra cinza/dither no cliente |
| Encoding | `Raw` (+ `CopyRect` opcional) |
| Modelo de atualização | Sob demanda (push do protocolo quando o conteúdo muda) — é o design nativo do RFB |
| Ciclo de vida da conexão | ~~Reconectar a cada interação~~ **Revisado**: conexão persistente, pedido incremental sempre em andamento; reconecta só se a conexão cair (ver seção acima) |

## Superfície de API do `libvncclient` que vamos usar

`rfbGetClient` → ajustar `client->format` (opcional) → `rfbInitClient`/`ConnectToRFBServer`
(handshake) → `SetFormatAndEncodings` → loop de `SendFramebufferUpdateRequest` +
`WaitForMessage`/`HandleRFBServerMessage` (dispara `GotFrameBufferUpdateProc`/
`FinishedFrameBufferUpdateProc`) pro lado de leitura, e `SendPointerEvent`/`SendKeyEvent` pro
lado interativo.
