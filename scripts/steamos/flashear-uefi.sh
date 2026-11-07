#!/usr/bin/env bash
# Flashea una imagen UEFI (Mu-*.img) en la particion boot, con el recovery abierto.
# Uso: ./flashear-uefi.sh ~/sm-t500/kit/Mu-gta4lwifi-memdtb-xhci.img
. "$(dirname "$0")/comun.sh"
IMG="${1:-}"; [ -f "$IMG" ] || morir "Uso: $0 imagen.img"
necesito_adb
H=$(sha256sum "$IMG" | cut -d' ' -f1); echo "Imagen: $IMG  sha256 $H"
adb push "$IMG" /tmp/u.img || morir "adb push fallo"
HT=$(adb shell sha256sum /tmp/u.img | cut -d' ' -f1 | tr -d '\r')
[ "$H" = "$HT" ] || morir "La copia en la tablet no coincide ($HT)"
adb shell 'dd if=/tmp/u.img of=/dev/block/by-name/boot bs=1M && sync' || morir "dd fallo"
verde "UEFI flasheado. Reinicia la tablet (adb reboot) para arrancar Windows."
