#!/bin/bash
# Deploy do lado-Kindle do Kindow: copia o binário (já cross-compilado) + libvncclient +
# o scriptlet de lançamento, e reinicia o processo em execução, se houver.
#
#   ./kindle/deploy.sh <ip-do-kindle>
#
# Pré-requisitos, não cobertos por este script:
#   - app/build/kindow-client já cross-compilado pro alvo ARM (ver app/meson.build; o
#     toolchain de cross-compilação em si ainda não está documentado neste repo — ver
#     docs/ideias-futuras.md).
#   - SSH como root no Kindle sem senha (padrão desse jailbreak).
#
# Depois de rodar uma vez, o app fica lançável tocando "Kindow" na biblioteca do Kindle
# (o scriptlet, ver kindle/kindow.sh) — não precisa mais rodar este script de novo a
# menos que o binário mude.
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "uso: $0 <ip-do-kindle>" >&2
    exit 1
fi
KINDLE="$1"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/app/build/kindow-client"
LIB="$ROOT/vendor/libvncserver/build/libvncclient.so.1"
SCRIPTLET="$ROOT/kindle/kindow.sh"

for f in "$BIN" "$LIB" "$SCRIPTLET"; do
    if [ ! -f "$f" ]; then
        echo "faltando: $f (rode o build antes de fazer deploy)" >&2
        exit 1
    fi
done

echo "== parando processo antigo, se houver =="
ssh "root@$KINDLE" "kill -TERM \$(pidof kindow-client) 2>/dev/null; sleep 1; true"

echo "== copiando binário + libvncclient =="
ssh "root@$KINDLE" "mkdir -p /mnt/us/kindow"
scp "$BIN" "$LIB" "root@$KINDLE:/mnt/us/kindow/"
ssh "root@$KINDLE" "chmod +x /mnt/us/kindow/kindow-client"

echo "== instalando o scriptlet (aparece na biblioteca do Kindle) =="
scp "$SCRIPTLET" "root@$KINDLE:/mnt/us/documents/kindow.sh"
ssh "root@$KINDLE" "chmod +x /mnt/us/documents/kindow.sh"

echo "== relançando =="
ssh "root@$KINDLE" "cd /mnt/us/kindow && DISPLAY=:0 LD_LIBRARY_PATH=/mnt/us/kindow nohup ./kindow-client > kindow.log 2>&1 < /dev/null & sleep 2; pidof kindow-client" \
    || { echo "kindow-client não subiu — confira /mnt/us/kindow/kindow.log no device" >&2; exit 1; }

echo "OK — kindow-client rodando, e lançável tocando 'Kindow' na biblioteca do Kindle."
