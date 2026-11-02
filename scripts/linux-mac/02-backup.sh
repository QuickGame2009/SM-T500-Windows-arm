#!/usr/bin/env bash
# Copia de seguridad de las particiones pequeñas y de la tabla GPT, con TWRP abierto.
# Solo lee de la tablet. Guarda los .img en el PC.
# Uso: ./02-backup.sh [carpeta_salida=backup] [tamaño_máximo_MiB=300]
#
# Se omiten userdata y las particiones grandes (system, vendor, super...). Esas se
# restauran con el firmware oficial. Lo que importa aquí son boot, recovery, vbmeta,
# efs, persist, modem, etc. y la GPT.
set -u
OUT="${1:-backup}"
MAX_MIB="${2:-300}"
mkdir -p "$OUT"

adb get-state >/dev/null 2>&1 || { echo "No veo la tablet por adb (TWRP abierto, cable conectado)." >&2; exit 1; }
run() { adb exec-out "$1" 2>/dev/null | tr -d '\r'; }

LIST="$OUT/_partitions.txt"
run 'BYN=$(ls -d /dev/block/by-name /dev/block/platform/*/by-name 2>/dev/null | head -n1)
for l in $BYN/*; do
  n=${l##*/}; d=$(readlink -f "$l"); b=${d##*/}
  echo "$n $d $(cat /sys/class/block/$b/start 2>/dev/null) $(cat /sys/class/block/$b/size 2>/dev/null)"
done' > "$LIST"
[ -s "$LIST" ] || { echo "No pude listar las particiones." >&2; exit 1; }

DISKINFO=$(run 'BYN=$(ls -d /dev/block/by-name /dev/block/platform/*/by-name 2>/dev/null | head -n1)
d=$(readlink -f $BYN/userdata); b=${d##*/}
disk=$(basename "$(readlink -f /sys/class/block/$b/..)")
echo "$disk $(cat /sys/class/block/$disk/size)"')
DISK=${DISKINFO% *}; DISK_SECTORS=${DISKINFO#* }
echo "Disco: $DISK  sectores: $DISK_SECTORS"

echo "== Particiones (límite ${MAX_MIB} MiB)"
SAVED=(); SKIPPED=()
while read -r name node start size; do
  [ -z "${name:-}" ] && continue
  case "$name" in userdata|cache) SKIPPED+=("$name(omitida)"); continue;; esac
  if [ -z "${size:-}" ] || [ "$size" -eq 0 ] 2>/dev/null; then SKIPPED+=("$name(sin tamaño)"); continue; fi
  mib=$(( size * 512 / 1048576 ))
  if [ "$mib" -gt "$MAX_MIB" ]; then SKIPPED+=("$name(${mib}MiB)"); continue; fi
  printf '  %-14s %6s MiB ... ' "$name" "$mib"
  adb exec-out "dd if=/dev/block/by-name/$name bs=1048576 2>/dev/null" > "$OUT/$name.img"
  got=$(stat -c %s "$OUT/$name.img" 2>/dev/null || wc -c < "$OUT/$name.img")
  want=$(( size * 512 ))
  if [ "$got" -eq "$want" ]; then echo ok; SAVED+=("$name"); else echo "TAMAÑO DISTINTO (esperado $want, obtenido $got)"; fi
done < "$LIST"

echo "== Tabla de particiones GPT (primarios 34 sectores y copia final 33)"
adb exec-out "dd if=/dev/block/$DISK bs=512 count=34 2>/dev/null" > "$OUT/gpt-primary.bin"
adb exec-out "dd if=/dev/block/$DISK bs=512 skip=$((DISK_SECTORS-33)) count=33 2>/dev/null" > "$OUT/gpt-backup.bin"
printf '%s %s\n' "$DISK" "$DISK_SECTORS" > "$OUT/disk.txt"
ls -l "$OUT/gpt-primary.bin" "$OUT/gpt-backup.bin"

echo "== Comprobaciones"
for must in boot recovery vbmeta efs persist; do
  case " ${SAVED[*]:-} " in *" $must "*) ;; *) echo "AVISO: no se guardó '$must'. Revisa antes de seguir.";; esac
done
if command -v sha256sum >/dev/null 2>&1; then (cd "$OUT" && sha256sum *.img *.bin > SHA256SUMS.txt); fi

echo
echo "Guardadas: ${SAVED[*]:-}"
echo "Omitidas:  ${SKIPPED[*]:-}"
echo "Copia la carpeta '$OUT' a un lugar seguro (otro disco o la nube) ANTES de particionar."
