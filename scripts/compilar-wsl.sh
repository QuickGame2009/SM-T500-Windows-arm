#!/usr/bin/env bash
# Compila la UEFI del SM-T500 en tu PC (WSL2 + Ubuntu), igual que el flujo de GitHub pero sin colas ni descargas repetidas.
# Uso (dentro de Ubuntu en WSL):  bash compilar-wsl.sh [memmap] [rtc] [diag] [heartbeat] [DEBUG|RELEASE]
#   memmap: original|lime|dtb|iomem   rtc: original|dia1   diag: no|si   heartbeat: no|si
# Ej.:     bash compilar-wsl.sh original dia1 no no DEBUG
# Resultado: ~/sm-t500-out/Mu-gta4lwifi-<opciones>.img (cópialo a Windows: \\wsl$\Ubuntu\home\<usuario>\sm-t500-out)
set -euo pipefail
MEM=${1:-original}; RTC=${2:-original}; DIAG=${3:-no}; HB=${4:-no}; REL=${5:-DEBUG}
KIT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"          # carpeta del kit (tiene overlay/ y variants/)
UP=~/mu-silicium-work; REF=4bd8ae83bf37de41777f19e0777db70f59383a6d
OUT=~/sm-t500-out; mkdir -p "$OUT"
export CI_BUILD=true CLANGPDB_AARCH64_PREFIX=aarch64-linux-gnu-

if [ ! -d "$UP/.git" ]; then
  git clone https://github.com/Project-Silicium/Mu-Silicium.git "$UP"
fi
cd "$UP"
# Siempre se parte de un arbol limpio (los parches de Mu_Basecore y nuestros cambios se reaplican cada vez).
git fetch -q origin 2>/dev/null || true
git reset -q --hard && git clean -qfd -e Build -e Conf
git checkout -q "$REF"
git submodule update --init -q Silicium-ACPI
for s in $(git submodule foreach -q 'echo $sm_path'); do [ -d "$s/.git" ] || [ -f "$s/.git" ] && git -C "$s" reset -q --hard || true; done
[ -f .deps_ok ] || { bash ./setup_env.sh -p apt && touch .deps_ok; }

cp -r "$KIT/overlay/." .
case "$MEM" in lime) cp -r "$KIT/variants/lime-memmap/." . ;; dtb) cp -r "$KIT/variants/dtb-memmap/." . ;; iomem) cp -r "$KIT/variants/iomem-memmap/." . ;; esac
if [ "$RTC" = dia1 ]; then
  f=Silicon/Silicium/SiliciumNullPkg/Library/RealTimeClockLibNull/RealTimeClockNull.c
  sed -i 's|Time->Day = (ElapsedSeconds / SecondsPerDay);|Time->Day = (ElapsedSeconds / SecondsPerDay) + 1;|' "$f"
fi
if [ "$HB" = si ]; then
  cp "$KIT/variants/heartbeat/Heartbeat.patch" Resources/MuPatches/Heartbeat.patch
  sed -i 's/"Usb-Bus.patch"\]/"Usb-Bus.patch", "Heartbeat.patch"]/g' build_uefi.py
fi
if [ "$DIAG" = si ]; then
  cp "$KIT/variants/diag/Diag.patch" Resources/MuPatches/Diag.patch
  sed -i -E 's/("[A-Za-z-]+\.patch")\]/\1, "Diag.patch"]/g' build_uefi.py
fi

python3 build_uefi.py -d gta4lwifi -r "$REL" -c
N="Mu-gta4lwifi-mem$MEM-rtc$RTC-diag$DIAG-hb$HB-$REL"
cp Mu-gta4lwifi.img "$OUT/$N.img"
echo; echo "LISTO: $OUT/$N.img   (en Windows: \\\\wsl\$\\Ubuntu$OUT)"
