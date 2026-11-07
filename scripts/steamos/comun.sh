#!/usr/bin/env bash
# Funciones comunes para los scripts de SteamOS / Linux.
# Herramientas: ~/sm-t500/platform-tools (adb) y el contenedor distrobox "smt500" (hivex, python-cryptography).
# Se preparan una sola vez con 00-preparar-steamdeck.sh
set -u
AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$AQUI/../.." && pwd)"
BASE="${SMT500_BASE:-$HOME/sm-t500}"
KIT="${SMT500_KIT:-$BASE/kit}"            # aqui van los .sys, .img y los logs (ver LEEME.md)
MNT="$BASE/mnt"
CAJA=smt500

rojo()  { printf '\033[31m%s\033[0m\n' "$*"; }
verde() { printf '\033[32m%s\033[0m\n' "$*"; }
amar()  { printf '\033[33m%s\033[0m\n' "$*"; }
morir() { rojo "$*"; exit 1; }

# --- adb ---
ADB_BIN="$BASE/platform-tools/adb"
[ -x "$ADB_BIN" ] || ADB_BIN="$(command -v adb || true)"
adb() {
  [ -n "$ADB_BIN" ] || morir "No hay adb. Ejecuta antes 00-preparar-steamdeck.sh"
  if "$ADB_BIN" devices 2>/dev/null | grep -q 'no permissions'; then sudo "$ADB_BIN" "$@"; else "$ADB_BIN" "$@"; fi
}
necesito_adb() {
  adb get-state >/dev/null 2>&1 || morir "No veo la tablet por adb (arranca el recovery, conecta el cable y acepta la conexion)."
}

# --- herramientas dentro de la caja distrobox ---
caja() { distrobox enter "$CAJA" -- "$@"; }
necesito_caja() {
  command -v distrobox >/dev/null || morir "No hay distrobox. Ejecuta 00-preparar-steamdeck.sh"
  caja sh -c 'command -v hivexregedit >/dev/null' || morir "Falta hivex en la caja $CAJA. Ejecuta 00-preparar-steamdeck.sh"
}

# --- disco de la tablet en modo almacenamiento (03-massstorage.sh) ---
buscar_disco() {
  local d
  for d in /sys/block/sd*; do
    [ -e "$d" ] || continue
    if grep -qi 'File-Stor' "$d/device/model" 2>/dev/null; then echo "/dev/${d##*/}"; return 0; fi
  done
  return 1
}
part() { echo "${DISCO}$1"; }   # /dev/sdb + 80 -> /dev/sdb80

montar() {   # montar <num_particion> <carpeta> <tipo: ntfs|vfat>
  local p; p="$(part "$1")"; local m="$2"
  [ -b "$p" ] || morir "No existe $p"
  mkdir -p "$m"
  if mountpoint -q "$m"; then return 0; fi
  # Si el escritorio de SteamOS lo monto solo, desmontarlo primero
  for x in $(findmnt -rno TARGET -S "$p"); do sudo umount "$x" || true; done
  if [ "$3" = ntfs ]; then
    sudo mount -t ntfs3 -o "uid=$(id -u),gid=$(id -g),force,iocharset=utf8" "$p" "$m" 2>/dev/null \
      || sudo mount -t ntfs-3g -o "uid=$(id -u),gid=$(id -g),remove_hiberfile" "$p" "$m" \
      || morir "No pude montar $p (NTFS)"
  else
    sudo mount -t vfat -o "uid=$(id -u),gid=$(id -g),utf8" "$p" "$m" || morir "No pude montar $p (FAT)"
  fi
}
desmontar_todo() {
  sync
  for m in "$MNT/win" "$MNT/esp"; do mountpoint -q "$m" && sudo umount "$m"; done
  sync
}

# --- registro de Windows (hivex) ---
# regget <colmena> 'Clave\Sub' valor     -> imprime el valor (o nada)
regget() { caja hivexget "$1" "\\$2" "$3" 2>/dev/null; }
# regmerge <colmena> <prefijo> <archivo.reg>
regmerge() { caja hivexregedit --merge --prefix "$2" "$1" "$3"; }
# Avisa si la colmena no se cerro bien (Windows apagado a la fuerza): los cambios pendientes del .LOG se pierden.
colmena_ok() {
  python3 - "$1" <<'P'
import struct,sys
h=open(sys.argv[1],'rb').read(16)
if h[:4]!=b'regf': print('NO ES UNA COLMENA'); sys.exit(1)
a,b=struct.unpack_from('<II',h,4)
print('limpia' if a==b else 'sucia (apagado a la fuerza; se ignoraran cambios no guardados de esa sesion)')
P
}
# Cadena -> hex(2)/hex(1) estilo .reg (UTF-16LE con nulo final)
hexsz() { python3 -c "import sys;b=(sys.argv[1]+'\0').encode('utf-16le');print(','.join('%02x'%x for x in b))" "$1"; }
