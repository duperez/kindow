#!/bin/bash
# Instalador idempotente do lado-Pi do Kindow. Rodar NO PI, como o usuário da sessão
# (assume "pi", igual aos units), a partir do diretório com os arquivos deste pi/:
#
#   scp -r pi/ pi@<ip-do-pi>:/tmp/kindow-pi
#   ssh -t pi@<ip-do-pi> 'bash /tmp/kindow-pi/install.sh'
#
# (o -t garante terminal pro sudo pedir a senha). Re-rodar é seguro: configs que
# carregam escolha do usuário (rc.xml do openbox, xsettingsd.conf e tint2rc — os três
# guardam os zooms de deco/apps/painel) só são criadas quando não existem; o resto é
# sobrescrito pra refletir o repositório.
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)"

echo "== pacotes =="
sudo apt-get install -y tigervnc-standalone-server openbox tint2 pcmanfm mousepad \
    xsettingsd x11-xserver-utils

echo "== configs da sessão (usuário) =="
mkdir -p ~/.config/tigervnc ~/.config/tint2 ~/.config/openbox ~/.config/xsettingsd
install -m 0755 "$SRC/xstartup" ~/.config/tigervnc/xstartup

# tint2rc: só cria se não existir — o kindow-helperd persiste o zoom de PAINEL escrevendo
# task_font/clock_font aqui dentro (mesma razão da guarda do rc.xml e do xsettingsd.conf:
# reinstalar não deve resetar zoom escolhido pelo usuário — achado de review). Pra pegar
# um tint2rc novo do repositório, remova o arquivo antes de re-rodar.
if [ ! -f ~/.config/tint2/tint2rc ]; then
    install -m 0644 "$SRC/tint2rc" ~/.config/tint2/tint2rc
fi

# rc.xml do openbox: parte do default da distro + fontes de titlebar/menu em tamanho de
# dedo (a 192dpi). Só cria se não existir — o usuário pode ter personalizado o dele.
if [ ! -f ~/.config/openbox/rc.xml ]; then
    cp /etc/xdg/openbox/rc.xml ~/.config/openbox/rc.xml
    sed -i \
        -e '/<font place="ActiveWindow">/,/<\/font>/ s|<size>8</size>|<size>14</size>|' \
        -e '/<font place="InactiveWindow">/,/<\/font>/ s|<size>8</size>|<size>14</size>|' \
        -e '/<font place="Menu/,/<\/font>/ s|<size>9</size>|<size>12</size>|' \
        ~/.config/openbox/rc.xml
fi

# xsettingsd.conf: só cria se não existir — o kindow-helperd grava aqui o zoom escolhido
# pelo usuário, e reinstalar não deve resetar essa escolha.
if [ ! -f ~/.config/xsettingsd/xsettingsd.conf ]; then
    install -m 0644 "$SRC/xsettingsd.conf" ~/.config/xsettingsd/xsettingsd.conf
fi

echo "== serviços (sudo) =="
sudo install -m 0755 "$SRC/kindow-helperd" /usr/local/bin/kindow-helperd
sudo install -m 0644 "$SRC/kindow-helperd.service" /etc/systemd/system/
sudo install -m 0644 "$SRC/vnc-kindle.service" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now kindow-helperd.service
sudo systemctl enable vnc-kindle.service
# restart (não só start): sessão antiga pode estar rodando com xstartup anterior
sudo systemctl restart vnc-kindle.service

echo "== verificação =="
sleep 3
systemctl is-active kindow-helperd.service vnc-kindle.service
resposta=$(printf 'ping\n' | timeout 5 python3 -c '
import socket,sys
s=socket.create_connection(("127.0.0.1",5910),timeout=4)
s.sendall(sys.stdin.buffer.read())
print(s.makefile().readline().strip())')
echo "helperd respondeu: $resposta"
echo "OK — lado-Pi do Kindow instalado."
