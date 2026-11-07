#!/usr/bin/env bash
# Copia los logs de arranque de Windows a ~/sm-t500/kit/logs-tablet-HHMMSS (modo almacenamiento).
. "$(dirname "$0")/comun.sh"
WIN=${1:-80}
DISCO=$(buscar_disco) || morir "No encuentro el disco de la tablet (03-massstorage.sh)."
montar "$WIN" "$MNT/win" ntfs; trap desmontar_todo EXIT
D="$KIT/logs-tablet-$(date +%H%M%S)"; mkdir -p "$D"
for f in canary-log.txt ntbtlog.txt Panther/setupact.log Panther/setuperr.log Panther/UnattendGC/setupact.log \
         System32/Sysprep/Panther/setupact.log INF/setupapi.dev.log INF/setupapi.setup.log Minidump MEMORY.DMP LiveKernelReports; do
  p="$MNT/win/Windows/$f"
  if [ -e "$p" ]; then cp -r "$p" "$D/" && echo "Copiado $f ($(date -r "$p" '+%d/%m %H:%M'))"; else echo "No existe $f"; fi
done
for f in "$MNT"/win/*.txt; do [ -f "$f" ] && cp "$f" "$D/" && echo "Copiado ${f##*/} (raiz de Windows)"; done
ls -t "$MNT/win/Windows/System32/drivers"/*.sys | head -5 | while read -r f; do echo "drivers: ${f##*/} $(date -r "$f" '+%d/%m %H:%M')"; done
verde "Logs en $D"
