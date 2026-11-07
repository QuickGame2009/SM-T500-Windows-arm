#!/usr/bin/env bash
# Instala el driver tactil Himax SIN usar Windows: copia los ficheros a C:\HimaxInstall y crea un servicio
# temporal (LocalSystem) que en el proximo arranque ejecuta pnputil y deja los resultados en C:\HimaxInstall\*.txt.
# Con la tablet en modo almacenamiento (03-massstorage.sh).
#   ./22-instalar-tactil.sh CARPETA_DEL_DRIVER      (HimaxSpiTouch.sys/.inf/.cat y opcional *.cer del certificado de prueba)
#   ./22-instalar-tactil.sh --leer                  (tras arrancar Windows: copia los resultados a ~/sm-t500/kit/)
# Necesita el firmware: antes, con el recovery:  adb pull /vendor/firmware/Himax_firmware_lide_hsd.bin ~/sm-t500/kit/
. "$(dirname "$0")/comun.sh"
WIN=80; SRC=""; LEER=0
while [ $# -gt 0 ]; do case "$1" in --win) WIN=$2; shift;; --leer) LEER=1;; -h|--help) sed -n 2,9p "$0"; exit 0;; *) SRC=$1;; esac; shift; done
mkdir -p "$KIT"; exec > >(tee "$KIT/diag-22.txt") 2>&1
necesito_caja
DISCO=$(buscar_disco) || morir "No encuentro el disco de la tablet (03-massstorage.sh)."
montar "$WIN" "$MNT/win" ntfs; trap desmontar_todo EXIT
W="$MNT/win"; D="$W/HimaxInstall"
if [ $LEER = 1 ]; then
  [ -d "$D" ] || morir "No existe C:\\HimaxInstall (¿arrancó Windows ya?)"
  O="$KIT/himax-resultados-$(date +%H%M%S)"; mkdir -p "$O"; cp "$D"/*.txt "$O"/ 2>/dev/null; ls -l "$O"
  for f in "$O"/*.txt; do echo "---- ${f##*/}"; iconv -f utf-16 -t utf-8 "$f" 2>/dev/null | head -40 || head -40 "$f"; done; exit 0
fi
[ -d "$SRC" ] || morir "Uso: $0 CARPETA_DEL_DRIVER   (con HimaxSpiTouch.sys y .inf)"
SYSF=$(find "$SRC" -maxdepth 2 -iname 'HimaxSpiTouch.sys' | head -1); INF=$(find "$SRC" -maxdepth 2 -iname 'HimaxSpiTouch.inf' | head -1)
[ -n "$SYSF" ] && [ -n "$INF" ] || morir "Falta HimaxSpiTouch.sys o .inf en $SRC"
CAT=$(find "$SRC" -maxdepth 2 -iname '*.cat' | head -1); [ -n "$CAT" ] || amar "Sin .cat: pnputil rechazara el paquete salvo que este firmado/catalogado."
FW=$(ls "$KIT/Himax_firmware_lide_hsd.bin" "$SRC"/Himax_firmware_lide_hsd.bin 2>/dev/null | head -1) || true
[ -n "${FW:-}" ] || morir "Falta el firmware. Con el recovery:  adb pull /vendor/firmware/Himax_firmware_lide_hsd.bin $KIT/"
echo "firmware: $FW ($(stat -c %s "$FW") bytes, esperado 131072)"
mkdir -p "$D"; cp "$SYSF" "$INF" "$D/"; [ -n "$CAT" ] && cp "$CAT" "$D/"; cp "$FW" "$D/"
for c in "$SRC"/*.cer; do [ -f "$c" ] && cp "$c" "$D/"; done
cp "$FW" "$W/Windows/System32/drivers/Himax_firmware_lide_hsd.bin"
{
  printf '@echo off\r\ncd /d C:\\HimaxInstall\r\ndate /t > fecha.txt & time /t >> fecha.txt\r\n'
  printf 'bcdedit /enum {current} > bcd.txt 2>&1\r\n'
  printf 'for %%%%c in (*.cer) do (certutil -addstore Root %%%%c >> certs.txt 2>&1 & certutil -addstore TrustedPublisher %%%%c >> certs.txt 2>&1)\r\n'
  printf 'pnputil /add-driver C:\\HimaxInstall\\HimaxSpiTouch.inf /install > pnputil.txt 2>&1\r\n'
  printf 'pnputil /enum-devices /class System > dispositivos.txt 2>&1\r\n'
  printf 'pnputil /enum-drivers > drivers.txt 2>&1\r\n'
  printf 'sc query HimaxSpiTouch > servicio.txt 2>&1\r\n'
  printf 'sc delete HimaxInst >> servicio.txt 2>&1\r\n'
} > "$D/instalar.cmd"
T="$BASE/trabajo"; mkdir -p "$T"; S="$T/SYSTEM"; B="$KIT/SYSTEM.antes22-$(date +%H%M%S)"
cp "$W/Windows/System32/config/SYSTEM" "$S"; cp "$S" "$B"; echo "Copia de SYSTEM: $B   ($(colmena_ok "$S"))"
R="$T/inst.reg"
printf 'Windows Registry Editor Version 5.00\r\n\r\n[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\HimaxInst]\r\n"Type"=dword:00000010\r\n"Start"=dword:00000002\r\n"ErrorControl"=dword:00000000\r\n"ImagePath"="cmd.exe /c C:\\\\HimaxInstall\\\\instalar.cmd"\r\n"ObjectName"="LocalSystem"\r\n"DisplayName"="HimaxInst"\r\n' > "$R"
regmerge "$S" 'HKEY_LOCAL_MACHINE\SYSTEM' "$R" && verde "Servicio temporal HimaxInst creado" || morir "ERROR al escribir SYSTEM"
cp "$S" "$W/Windows/System32/config/SYSTEM" && verde "Listo. Flashea el UEFI con tactil si no lo has hecho, arranca Windows, espera 1-2 min y ejecuta:  $0 --leer"
