#!/bin/sh
# Name: Kindow
# Author: eduardo
#
# Scriptlet do Kindle (não KUAL — não está disponível nesse jailbreak, ver
# docs/ideias-futuras.md): copiado pra /mnt/us/documents/ (ver kindle/deploy.sh), aparece
# como item tocável na biblioteca do Kindle, igual um livro. Mecanismo confirmado
# funcionando no projeto irmão kindle/ (mesmo jailbreak/toolchain), ver lá
# docs/findings/appmgrd-return-to-dashboard.md — é o mesmo caminho usado pelo
# pet_dashboard e pelo kterm oficial desse jailbreak.
#
# Mata uma instância antiga antes de lançar de novo (tocar de novo == reiniciar, não
# abrir uma segunda janela por cima).
#
# setsid + nohup (não só "&"): via scriptlet, quem executa este .sh é um processo interno
# do Kindle (o handler genérico de .sh do jailbreak) — sem desacoplar de verdade, o fim
# da sessão dele pode alcançar o kindow-client recém-lançado.
#
# O sleep ANTES do exec (dentro do subshell, DEPOIS do script principal já ter terminado)
# é o pulo do gato — achado ao vivo (27/08): quando o script termina, o framework do
# Kindle "volta pra Home", redesenhando a Home POR CIMA de tudo. O kindow-client mapeia a
# janela quase instantaneamente (tela de conexão local, sem rede), então sem o delay ela
# aparecia por ~1s e era coberta pela Home logo em seguida. Atrasando o lançamento, a
# janela mapeia DEPOIS da Home reassumir — e fica por cima. (O pet_dashboard, mesmo
# mecanismo, escapa por acaso: demora mais pra subir porque busca dados na rede antes.)

pkill -f /mnt/us/kindow/kindow-client 2>/dev/null

setsid nohup sh -c '
    sleep 3
    DISPLAY=:0 LD_LIBRARY_PATH=/mnt/us/kindow exec /mnt/us/kindow/kindow-client
' > /mnt/us/kindow/kindow.log 2>&1 < /dev/null &
