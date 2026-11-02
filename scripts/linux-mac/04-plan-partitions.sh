#!/usr/bin/env bash
# Calcula el plan de particionado a partir de la carpeta "info" que genera 01-info.sh.
# NO toca la tablet: solo imprime los comandos de parted para que los revises y los pegues.
#
# Uso: ./04-plan-partitions.sh [info] [userdata_MiB=4096] [esp_MiB=512]
#
# Resultado: userdata se reduce a <userdata_MiB>, se crea esp (FAT32) y win (NTFS) con el resto.
# Requisito: userdata debe ser la ÚLTIMA partición del disco (lo habitual en Samsung).
set -eu
INFO="${1:-info}"
UD_MIB="${2:-4096}"
ESP_MIB="${3:-512}"
SEC_MIB=2048   # sectores de 512 B por MiB

[ -s "$INFO/partitions.txt" ] && [ -s "$INFO/disk.txt" ] || { echo "Falta $INFO/partitions.txt o $INFO/disk.txt (ejecuta 01-info.sh)." >&2; exit 1; }

read -r DISK DISK_SECTORS < "$INFO/disk.txt"
line=$(grep -E '^userdata ' "$INFO/partitions.txt" | head -n1 || true)
[ -n "$line" ] || { echo "No encuentro la partición userdata." >&2; exit 1; }
read -r _ NODE START SIZE <<<"$line"
END=$(( START + SIZE - 1 ))

# Número de partición: mmcblk0p41 -> 41 ; sda41 -> 41
NUM=$(echo "$NODE" | sed -E 's/.*[^0-9]([0-9]+)$/\1/')

# userdata debe ser la de mayor sector de inicio
MAXSTART=$(awk '$3 ~ /^[0-9]+$/ {print $3}' "$INFO/partitions.txt" | sort -n | tail -n1)
if [ "$START" -ne "$MAXSTART" ]; then
  echo "ERROR: userdata no es la última partición (inicio $START, la última empieza en $MAXSTART)." >&2
  echo "No generes un plan automático. Envíame la carpeta info y lo reviso a mano." >&2
  exit 2
fi

UD_END=$(( START + UD_MIB * SEC_MIB - 1 ))
ESP_START=$(( (UD_END + 1 + SEC_MIB - 1) / SEC_MIB * SEC_MIB ))   # alinear a 1 MiB hacia arriba
ESP_END=$(( ESP_START + ESP_MIB * SEC_MIB - 1 ))
WIN_START=$(( ESP_END + 1 ))
WIN_END=$END
WIN_MIB=$(( (WIN_END - WIN_START + 1) / SEC_MIB ))

if [ "$UD_END" -ge "$END" ]; then
  echo "ERROR: userdata actual ($(( SIZE / SEC_MIB )) MiB) no es mayor que lo pedido (${UD_MIB} MiB). Reduce userdata_MiB." >&2
  exit 3
fi
if [ "$WIN_MIB" -lt 20480 ]; then
  echo "ERROR: solo quedarían ${WIN_MIB} MiB para Windows (mínimo 20480). Reduce userdata_MiB." >&2
  exit 3
fi

echo "# Disco: /dev/block/$DISK  (${DISK_SECTORS} sectores)"
echo "# userdata actual: partición $NUM, sectores $START-$END ($(( SIZE / SEC_MIB )) MiB)"
[ $(( START % SEC_MIB )) -ne 0 ] && echo "# AVISO: userdata no empieza alineada a 1 MiB; parted puede avisar. Responde 'Ignore' solo si el inicio coincide con el original."
echo
echo "# ---- Plan nuevo (sectores de 512 B)"
printf '#   userdata  %s-%s  (%s MiB)\n' "$START" "$UD_END" "$UD_MIB"
printf '#   esp       %s-%s  (%s MiB)\n' "$ESP_START" "$ESP_END" "$ESP_MIB"
printf '#   win       %s-%s  (%s MiB = %s GiB)\n' "$WIN_START" "$WIN_END" "$WIN_MIB" "$(( WIN_MIB / 1024 ))"
echo
cat <<EOF
# ---- Pasos (en TWRP, con TODO desmontado: Mount > desmarcar todo)
adb shell mkdir -p /workspace
adb push parted gdisk /workspace/
adb shell chmod 744 /workspace/parted /workspace/gdisk
adb shell
  cd /workspace
  ./parted /dev/block/$DISK
    (parted) unit s
    (parted) print                      # comprueba que userdata es la $NUM y que los números coinciden con lo de arriba
    (parted) rm $NUM
    (parted) mkpart userdata ext4 ${START}s ${UD_END}s
    (parted) mkpart esp fat32 ${ESP_START}s ${ESP_END}s
    (parted) mkpart win ntfs ${WIN_START}s ${WIN_END}s
    (parted) print                      # deberían ser las particiones $NUM, $((NUM+1)) y $((NUM+2))
    (parted) set $((NUM+1)) esp on
    (parted) quit
  reboot recovery

# ---- Después, otra vez en TWRP, formatear
adb shell
  mke2fs -t ext4 /dev/block/by-name/userdata
  mkfs.fat -F32 -s1 /dev/block/by-name/esp
  mkfs.ntfs -f /dev/block/by-name/win
EOF
