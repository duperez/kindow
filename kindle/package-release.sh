#!/bin/bash
# Monta o pacote binário de release do Kindow — o zip anexado nas GitHub Releases, pra
# quem quer instalar sem montar o toolchain de cross-compilation.
#
#   ./kindle/package-release.sh <versão>          # ex.: ./kindle/package-release.sh 0.1.0
#
# Pré-requisitos: app/build/kindow-client e vendor/libvncserver/build/libvncclient.so.1
# já cross-compilados (ver README), e o binário de strip do toolchain acessível — por
# padrão via o container docker "kindle-toolchain" (STRIP_CMD sobrescreve).
#
# Conteúdo do pacote (autocontido — instala os DOIS lados sem clonar o repo):
#   kindle/   binário strippado + libvncclient + scriptlet de lançamento
#   pi/       lado servidor completo (instalador idempotente incluso)
#   install-kindle.sh   copia o lado Kindle pro device via SSH (o mesmo fluxo do
#                       kindle/deploy.sh, sem a etapa de build)
#   LICENSE + INSTALL.txt
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "uso: $0 <versão>  (ex.: $0 0.1.0)" >&2
    exit 1
fi
VERSION="$1"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/app/build/kindow-client"
LIB="$ROOT/vendor/libvncserver/build/libvncclient.so.1"
# Container com o toolchain (pro strip do binário ARM) — sobrescrever TOOLCHAIN_CONTAINER
# se o seu container tiver outro nome.
TOOLCHAIN_CONTAINER="${TOOLCHAIN_CONTAINER:-kindle-toolchain}"
STRIP_BIN="/home/builder/x-tools/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-strip"

for f in "$BIN" "$LIB"; do
    [ -f "$f" ] || { echo "faltando: $f (rode o build antes de empacotar)" >&2; exit 1; }
done

STAGE="$(mktemp -d)/kindow-$VERSION"
trap 'rm -rf "$(dirname "$STAGE")"' EXIT
mkdir -p "$STAGE/kindle"

# strip: o build carrega debug_info (útil pra nós, peso morto pra quem só instala)
docker cp "$BIN" "$TOOLCHAIN_CONTAINER:/tmp/kindow-client-release"
docker exec "$TOOLCHAIN_CONTAINER" "$STRIP_BIN" /tmp/kindow-client-release
docker cp "$TOOLCHAIN_CONTAINER:/tmp/kindow-client-release" "$STAGE/kindle/kindow-client"
docker exec "$TOOLCHAIN_CONTAINER" rm -f /tmp/kindow-client-release
cp "$LIB" "$STAGE/kindle/libvncclient.so.1"
cp "$ROOT/kindle/kindow.sh" "$STAGE/kindle/kindow.sh"
cp -r "$ROOT/pi" "$STAGE/pi"
cp "$ROOT/LICENSE" "$STAGE/LICENSE"

# instalador do lado Kindle: o deploy.sh sem a checagem de build (os arquivos já estão
# no pacote, em caminhos relativos a ele)
cat > "$STAGE/install-kindle.sh" <<'EOS'
#!/bin/bash
# Installs the Kindow client on a jailbroken Kindle over SSH (root).
#   ./install-kindle.sh <kindle-ip>
set -euo pipefail
[ $# -eq 1 ] || { echo "usage: $0 <kindle-ip>" >&2; exit 1; }
KINDLE="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"

ssh "root@$KINDLE" "kill -TERM \$(pidof kindow-client) 2>/dev/null; sleep 1; mkdir -p /mnt/us/kindow; true"
scp "$HERE/kindle/kindow-client" "$HERE/kindle/libvncclient.so.1" "root@$KINDLE:/mnt/us/kindow/"
scp "$HERE/kindle/kindow.sh" "root@$KINDLE:/mnt/us/documents/kindow.sh"
ssh "root@$KINDLE" "chmod +x /mnt/us/kindow/kindow-client /mnt/us/documents/kindow.sh"
echo "Done — tap 'Kindow' in the Kindle's library to launch it."
EOS
chmod +x "$STAGE/install-kindle.sh"

cat > "$STAGE/INSTALL.txt" <<EOS
Kindow $VERSION — pre-built package
===================================

Server (Raspberry Pi, or any Debian-like Linux with systemd):
    scp -r pi/ pi@<pi-ip>:/tmp/kindow-pi && ssh -t pi@<pi-ip> 'bash /tmp/kindow-pi/install.sh'

Client (jailbroken Kindle with SSH):
    ./install-kindle.sh <kindle-ip>

Then tap "Kindow" in the Kindle's library. Full documentation:
    https://github.com/duperez/kindow

The client binary targets ARM (Kindle KT5 and similar, jailbroken firmware).
Licensed under GPL-3.0 — source code at the repository above.
EOS

OUT="$ROOT/kindow-$VERSION.zip"
rm -f "$OUT"
(cd "$(dirname "$STAGE")" && zip -qr "$OUT" "kindow-$VERSION")
echo "pacote: $OUT"
unzip -l "$OUT"
