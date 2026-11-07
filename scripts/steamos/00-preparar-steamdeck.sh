#!/usr/bin/env bash
# Preparacion de una sola vez en la Steam Deck (modo escritorio, Konsole).
#  1. Antes: pon contrasena al usuario deck con  passwd  (sudo la necesita).
#  2. Descarga platform-tools (adb) de Google en ~/sm-t500/platform-tools
#  3. Crea un contenedor distrobox "smt500" (Arch) con hivex (editar el registro de Windows) y python-cryptography
# No toca el sistema de SteamOS (todo queda en tu carpeta personal).
. "$(dirname "$0")/comun.sh"
mkdir -p "$BASE" "$KIT" "$MNT"
if [ ! -x "$BASE/platform-tools/adb" ]; then
  echo "Descargando platform-tools..."
  curl -L -o "$BASE/pt.zip" https://dl.google.com/android/repository/platform-tools-latest-linux.zip || morir "Fallo la descarga"
  python3 -m zipfile -e "$BASE/pt.zip" "$BASE/" && rm -f "$BASE/pt.zip"
  chmod +x "$BASE/platform-tools/"* 
fi
"$BASE/platform-tools/adb" version | head -1
command -v distrobox >/dev/null || morir "Esta version de SteamOS no trae distrobox (necesitas SteamOS 3.5 o superior)."
if ! distrobox list 2>/dev/null | grep -q " $CAJA "; then
  distrobox create -n "$CAJA" -i docker.io/library/archlinux:latest -Y || morir "No pude crear el contenedor"
fi
caja sudo pacman -Syu --noconfirm --needed hivex perl python python-cryptography ntfs-3g || morir "Fallo pacman dentro del contenedor"
caja hivexregedit --help >/dev/null 2>&1 && verde "hivex OK"
echo
verde "Listo. Copia en $KIT los archivos del kit (ver LEEME.md) y usa los scripts de esta carpeta."
