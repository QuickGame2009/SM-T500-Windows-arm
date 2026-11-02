#!/usr/bin/env bash
# Recoge información del SM-T500 con TWRP abierto y la tablet conectada por USB.
# No modifica nada en la tablet: solo lee.
# Uso (Git Bash en Windows, o Linux/macOS con adb en el PATH):  ./01-info.sh [carpeta_salida]
# Después, comprime la carpeta "info" y envíamela.
set -u
OUT="${1:-info}"
mkdir -p "$OUT"

if ! adb get-state >/dev/null 2>&1; then
  echo "No veo la tablet por adb. Arranca en TWRP, conecta el cable y acepta la conexión." >&2
  exit 1
fi

# exec-out evita que Windows convierta los saltos de línea; tr -d '\r' por si acaso.
run() { adb exec-out "$1" 2>&1 | tr -d '\r'; }

echo "[1/6] Identidad y arranque"
run 'id'                                  > "$OUT/id.txt"
run 'getprop'                             > "$OUT/getprop.txt"
grep -i -E 'bootloader|verifiedboot|vbmeta|flash.locked|warranty|ro.product.(device|model|name)|ro.boot.hardware|ro.build.fingerprint' \
  "$OUT/getprop.txt" > "$OUT/arranque-resumen.txt"
run 'cat /proc/cmdline'                   > "$OUT/cmdline.txt"
run 'cat /proc/version'                   > "$OUT/kernel-version.txt"

echo "[2/6] Memoria y mapa de direcciones"
run 'cat /proc/iomem'                     > "$OUT/iomem.txt"
run 'cat /proc/meminfo'                   > "$OUT/meminfo.txt"

echo "[3/6] Identificadores de placa (para casar el DTB del UEFI)"
for f in model compatible; do
  run "cat /proc/device-tree/$f; echo"    > "$OUT/dt-$f.txt"
done
for f in qcom,msm-id qcom,board-id; do
  run "od -An -tx1 /proc/device-tree/$f"  > "$OUT/dt-$f.txt"
done

echo "[4/6] Particiones"
run 'ls -l /dev/block/by-name /dev/block/platform/*/by-name 2>/dev/null' > "$OUT/by-name.txt"
run 'cat /proc/partitions'                > "$OUT/proc-partitions.txt"
# Tabla "nombre nodo inicio tamaño" en sectores de 512 B. La usa 04-plan-partitions.sh.
run 'BYN=$(ls -d /dev/block/by-name /dev/block/platform/*/by-name 2>/dev/null | head -n1)
for l in $BYN/*; do
  n=${l##*/}; d=$(readlink -f "$l"); b=${d##*/}
  echo "$n $d $(cat /sys/class/block/$b/start 2>/dev/null) $(cat /sys/class/block/$b/size 2>/dev/null)"
done' > "$OUT/partitions.txt"
# Disco que contiene userdata y su tamaño total en sectores.
run 'BYN=$(ls -d /dev/block/by-name /dev/block/platform/*/by-name 2>/dev/null | head -n1)
d=$(readlink -f $BYN/userdata); b=${d##*/}
disk=$(basename "$(readlink -f /sys/class/block/$b/..)")
echo "$disk $(cat /sys/class/block/$disk/size)"' > "$OUT/disk.txt"

echo "[5/6] USB gadget (para el script de almacenamiento masivo)"
run 'ls /sys/class/udc; echo ---; ls /config/usb_gadget/g1 2>&1; echo ---; ls /config/usb_gadget/g1/functions 2>&1; echo ---; ls /config/usb_gadget/g1/configs/b.1 2>&1' > "$OUT/usb-gadget.txt"

echo "[6/6] Almacenamiento"
run 'ls /sys/class/mmc_host 2>&1; echo ---; ls -d /sys/class/ufs* 2>&1; echo ---; ls /dev/block | grep -E "^(mmcblk0|sda)$"' > "$OUT/storage-type.txt"

echo
echo "Listo. Resumen de lo importante:"
echo "---- Bootloader / estado de arranque"; cat "$OUT/arranque-resumen.txt"
echo "---- Disco (nombre sectores)";        cat "$OUT/disk.txt"
echo "---- msm-id / board-id";              cat "$OUT/dt-qcom,msm-id.txt" "$OUT/dt-qcom,board-id.txt"
echo
echo "Envíame la carpeta '$OUT' completa."
