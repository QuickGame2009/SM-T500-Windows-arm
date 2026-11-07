#!/usr/bin/env bash
# Solo lectura: que drivers estan activados/instalados (USB, keepalive, partmgr/volmgr) y que dispositivos USB ha visto Windows.
# Con la tablet en modo almacenamiento (03-massstorage.sh).  Resultado: ~/sm-t500/kit/diag-drivers.txt
. "$(dirname "$0")/comun.sh"
WIN=${1:-80}
mkdir -p "$KIT"; exec > >(tee "$KIT/diag-drivers.txt") 2>&1
necesito_caja
DISCO=$(buscar_disco) || morir "No encuentro el disco de la tablet (03-massstorage.sh)."
montar "$WIN" "$MNT/win" ntfs; trap desmontar_todo EXIT
W="$MNT/win/Windows"; T="$BASE/trabajo"; mkdir -p "$T"; cp "$W/System32/config/SYSTEM" "$T/SYSTEM-lectura"; S="$T/SYSTEM-lectura"
echo "== Servicios (Start: 0=arranque 1=sistema 2=auto 3=manual 4=desactivado)"
for s in USBXHCI UrsSynopsys UrsCx01000 USBHUB3 usbhub USBHUB HidUsb kbdhid mouhid hidusb keepalive partmgr volmgr dam; do
  v=$(regget "$S" "ControlSet001\\Services\\$s" Start); f=$(regget "$S" "ControlSet001\\Services\\$s" ImagePath)
  printf '%-14s Start=%-10s %s\n' "$s" "${v:-NO EXISTE}" "$f"
done
echo; echo "== Ficheros en System32\\drivers"
for f in USBXHCI.sys UrsSynopsys.sys UrsCx01000.sys UsbHub3.sys usbhub.sys HidUsb.sys kbdhid.sys mouhid.sys keepalive.sys partmgr.sys volmgr.sys; do
  p=$(find "$W/System32/drivers" -maxdepth 1 -iname "$f" | head -1); [ -n "$p" ] \
    && echo "SI  ${p##*/} $(stat -c %s "$p") bytes $(date -r "$p" '+%d/%m/%Y')" || echo "NO  $f"
done
echo; echo "== Dispositivos USB que Windows ha enumerado (Enum\\USB)"
printf 'cd \\ControlSet001\\Enum\\USB\nls\n' > "$T/usb.hsh"; caja hivexsh -f "$T/usb.hsh" "$S" 2>&1 | head -30
echo; echo "== urssynopsys en DriverStore"; find "$W/System32/DriverStore/FileRepository" -iname "urssynopsys*" | head
echo; echo "== Drivers instalados de terceros (DriverStore)"
ls "$W/System32/DriverStore/FileRepository" | grep -i "usb\|urs\|hid\|himax\|keepalive" | head -20
verde "Listo: $KIT/diag-drivers.txt"
