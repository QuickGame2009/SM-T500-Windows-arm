#!/usr/bin/env bash
# Expone la memoria de la tablet a la Steam Deck como disco USB (desde el recovery).
# adb deja de funcionar mientras tanto; para recuperarlo reinicia la tablet al recovery.
. "$(dirname "$0")/comun.sh"
necesito_adb
SRC="$REPO/scripts/MassStorage-emmc.sh"; [ -f "$SRC" ] || morir "Falta $SRC"
amar "Esto expone TODA la memoria de la tablet con escritura. Si SteamOS ofrece formatear algo: NO."
read -r -p "Escribe SI para continuar: " r; [ "$r" = SI ] || exit 0
tr -d '\r' < "$SRC" > /tmp/MassStorage-emmc.sh
adb push /tmp/MassStorage-emmc.sh /cache/MassStorage-emmc.sh || morir "adb push fallo"
adb shell 'chmod 744 /cache/MassStorage-emmc.sh; sh /cache/MassStorage-emmc.sh'
echo "Esperando el disco..."
for i in $(seq 1 30); do sleep 2; D=$(buscar_disco) && break; done
if [ -n "${D:-}" ]; then verde "Disco de la tablet: $D ($(lsblk -dno SIZE "$D"))"; lsblk "$D" -o NAME,SIZE,FSTYPE,PARTLABEL | grep -E 'NAME|esp|win|userdata' 
else rojo "No aparece el disco. Reinicia la tablet al recovery y ejecuta: adb shell cat /cache/ms.log"; fi
