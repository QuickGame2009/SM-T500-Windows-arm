#!/usr/bin/env bash
# Version Linux/SteamOS de 17-patch-partmgr.ps1. Con la tablet en modo almacenamiento (03-massstorage.sh).
# A diferencia del de Windows NO cambia los tipos de particion en la GPT: Linux monta las particiones igual.
#
# Opciones (mismos nombres que en Windows):
#   --esp N / --win N          numero de particion de ESP (76) y de Windows (80)
#   --sin-volmgr               vuelve a poner el volmgr.sys original
#   --desactivar a,b,c         servicios con Start=4 (por defecto: dam). --desactivar ninguno = no desactivar
#   --activar a,b,c            servicios con Start=0 (p. ej. USBXHCI,UrsSynopsys,UrsCx01000,USBHUB3)
#   --keepalive N              instala keepalive.sys con Modo=N  (-2 = desactivarlo)
#   --numproc N                0 = todos los nucleos, N = solo N
#   --arreglar-setup           Setup\Status\ChildCompletion\setup.exe = 3
#   --arreglar-logon           inicio de sesion automatico (tablet / Cambiar123, dominio = nombre del equipo)
#   --consola / --quitar-consola   boton de Accesibilidad de la pantalla de inicio = cmd como SYSTEM
#   --bootlog                  activa ntbtlog.txt
. "$(dirname "$0")/comun.sh"
ESP=76; WIN=80; SINVOL=0; DES="dam"; ACT=""; KA=""; NP=""; ASET=0; ALOG=0; CONS=0; QCONS=0; BLOG=0
while [ $# -gt 0 ]; do
  case "$1" in
    --esp) ESP=$2; shift;; --win) WIN=$2; shift;;
    --sin-volmgr) SINVOL=1;; --desactivar) DES=$2; shift;; --activar) ACT=$2; shift;;
    --keepalive) KA=$2; shift;; --numproc) NP=$2; shift;;
    --arreglar-setup) ASET=1;; --arreglar-logon) ALOG=1;; --consola) CONS=1;; --quitar-consola) QCONS=1;;
    --bootlog) BLOG=1;;
    -h|--help) sed -n 2,16p "$0"; exit 0;;
    *) morir "Opcion desconocida: $1";;
  esac; shift
done
[ "$DES" = ninguno ] && DES=""
mkdir -p "$KIT"; exec > >(tee "$KIT/diag-17-linux.txt") 2>&1
necesito_caja
DISCO=$(buscar_disco) || morir "No encuentro el disco de la tablet (03-massstorage.sh)."
echo "Disco de la tablet: $DISCO"
montar "$ESP" "$MNT/esp" vfat
montar "$WIN" "$MNT/win" ntfs
W="$MNT/win/Windows"; E="$MNT/esp/EFI/Microsoft/Boot"
[ -d "$W/System32" ] || morir "En la particion $WIN no esta Windows"
trap desmontar_todo EXIT
# Las colmenas se editan en copias dentro de ~/sm-t500/trabajo (el contenedor no ve los montajes) y se devuelven al final.
T="$BASE/trabajo"; mkdir -p "$T"; CP="$T/copia-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$CP"
cp "$W/System32/config/SYSTEM" "$W/System32/config/SOFTWARE" "$E/BCD" "$CP/" || morir "No pude copiar las colmenas"
cp "$CP/SYSTEM" "$T/SYSTEM"; cp "$CP/SOFTWARE" "$T/SOFTWARE"; cp "$CP/BCD" "$T/BCD"
SYS="$T/SYSTEM"; SW="$T/SOFTWARE"; BCD="$T/BCD"
echo "Copia de seguridad de las colmenas en $CP"
echo "SYSTEM: $(colmena_ok "$SYS")   SOFTWARE: $(colmena_ok "$SW")   BCD: $(colmena_ok "$BCD")"
DRV="$W/System32/drivers"

sha() { sha256sum "$1" | cut -d' ' -f1 | tr a-f A-F; }
kitf() { for f in "$KIT/$1" "$REPO/binarios/$1" "$REPO/$1" "$REPO/scripts/$1"; do [ -f "$f" ] && { echo "$f"; return 0; }; done; return 1; }

# --- partmgr.sys ---
PM="$DRV/partmgr.sys"
[ -f "$PM.orig" ] || { cp "$PM" "$PM.orig"; echo "Copia original: partmgr.sys.orig"; }
if [ "$(sha "$PM.orig")" != 6B9F427CA3ED805E00EA9596D7DF337D440C6E455175E3121D15C8F119AB8D37 ]; then rojo "partmgr.sys.orig no es la version esperada; no se toca"
elif P=$(kitf partmgr-parcheado.sys); then cp "$P" "$PM"; echo "partmgr.sys parcheado: $(sha "$PM")"
else rojo "Falta partmgr-parcheado.sys"; fi
# --- volmgr.sys ---
VM="$DRV/volmgr.sys"
[ -f "$VM.orig" ] || { cp "$VM" "$VM.orig"; echo "Copia original: volmgr.sys.orig"; }
if [ $SINVOL = 1 ]; then cp "$VM.orig" "$VM"; amar "volmgr.sys original restaurado"
elif [ "$(sha "$VM.orig")" != 0769135F1C9E8A3020E23DCAB52D69F590B0716916BD627117F48F9633B1A8AB ]; then rojo "volmgr.sys.orig no es la version esperada; no se toca"
elif P=$(kitf volmgr-parcheado.sys); then cp "$P" "$VM"; echo "volmgr.sys parcheado: $(sha "$VM")"
else amar "Falta volmgr-parcheado.sys; volmgr sin parchear"; fi

# --- SYSTEM: servicios, keepalive, setup ---
R=$(mktemp -p "$T" --suffix .reg); printf 'Windows Registry Editor Version 5.00\r\n\r\n' > "$R"; N=0
S='HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Services'
for svc in ${DES//,/ }; do
  if [ -n "$(regget "$SYS" "ControlSet001\\Services\\$svc" Start)" ]; then printf '[%s\\%s]\r\n"Start"=dword:00000004\r\n\r\n' "$S" "$svc" >> "$R"; N=1; echo "Servicio $svc -> desactivado (Start=4)"
  else amar "Servicio $svc no existe"; fi
done
for svc in ${ACT//,/ }; do
  if [ -n "$(regget "$SYS" "ControlSet001\\Services\\$svc" Start)" ]; then printf '[%s\\%s]\r\n"Start"=dword:00000000\r\n\r\n' "$S" "$svc" >> "$R"; N=1; echo "Servicio $svc -> activado (Start=0)"
  else amar "Servicio $svc no existe"; fi
done
if [ -n "$KA" ] && [ "$KA" -ge 0 ] 2>/dev/null; then
  if P=$(kitf keepalive.sys) || P=$(kitf drivers/keepalive/out/keepalive.sys); then
    cp "$P" "$DRV/keepalive.sys"
    printf '[%s\\keepalive]\r\n"Type"=dword:00000001\r\n"Start"=dword:00000000\r\n"ErrorControl"=dword:00000000\r\n"Group"="Boot Bus Extender"\r\n"ImagePath"=hex(2):%s\r\n"Modo"=dword:%08x\r\n\r\n' \
      "$S" "$(hexsz 'System32\drivers\keepalive.sys')" "$KA" >> "$R"; N=1; echo "keepalive.sys instalado (Modo=$KA)"
  else rojo "Falta keepalive.sys"; fi
elif [ "$KA" = -2 ]; then printf '[%s\\keepalive]\r\n"Start"=dword:00000004\r\n\r\n' "$S" >> "$R"; N=1; echo "keepalive desactivado"; fi
if [ $ASET = 1 ]; then printf '[HKEY_LOCAL_MACHINE\\SYSTEM\\Setup\\Status\\ChildCompletion]\r\n"setup.exe"=dword:00000003\r\n\r\n' >> "$R"; N=1; echo "setup.exe = 3"; fi
[ $N = 1 ] && { regmerge "$SYS" 'HKEY_LOCAL_MACHINE\SYSTEM' "$R" && verde "SYSTEM actualizado" || rojo "ERROR al escribir SYSTEM"; }

# --- SOFTWARE: inicio de sesion automatico ---
if [ $ALOG = 1 ]; then
  CN=$(regget "$SYS" 'ControlSet001\Control\ComputerName\ComputerName' ComputerName); CN=${CN:-DESKTOP-DJPRG0N}
  K='Microsoft\Windows NT\CurrentVersion\Winlogon'
  echo "Antes: AutoAdminLogon=$(regget "$SW" "$K" AutoAdminLogon) DefaultUserName=$(regget "$SW" "$K" DefaultUserName) DefaultDomainName=$(regget "$SW" "$K" DefaultDomainName)"
  printf 'Windows Registry Editor Version 5.00\r\n\r\n[HKEY_LOCAL_MACHINE\\SOFTWARE\\%s]\r\n"DefaultDomainName"="%s"\r\n"AltDefaultDomainName"="%s"\r\n"AutoAdminLogon"="1"\r\n"ForceAutoLogon"="1"\r\n"DefaultUserName"="tablet"\r\n"DefaultPassword"="Cambiar123"\r\n' "$K" "$CN" "$CN" > "$R"
  [ -n "$(regget "$SW" "$K" AutoLogonCount)" ] && printf '"AutoLogonCount"=-\r\n' >> "$R"
  regmerge "$SW" 'HKEY_LOCAL_MACHINE\SOFTWARE' "$R" && verde "Winlogon: dominio=$CN usuario=tablet" || rojo "ERROR al escribir SOFTWARE"
fi

# --- Consola de rescate ---
U="$W/System32/Utilman.exe"
if [ $CONS = 1 ]; then [ -f "$U.orig" ] || cp "$U" "$U.orig"; cp "$W/System32/cmd.exe" "$U"; echo "Consola: Utilman.exe = cmd.exe"
elif [ $QCONS = 1 ] && [ -f "$U.orig" ]; then cp "$U.orig" "$U"; echo "Consola quitada"; fi

# --- BCD ---
DEF=$(regget "$BCD" 'Objects\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\Elements\23000003' Element)
if [ -n "$DEF" ]; then
  O="HKEY_LOCAL_MACHINE\\BCD00000000\\Objects\\$DEF\\Elements"
  printf 'Windows Registry Editor Version 5.00\r\n\r\n[%s\\250000e0]\r\n"Element"=hex:01,00,00,00,00,00,00,00\r\n\r\n[%s\\16000009]\r\n"Element"=hex:00\r\n\r\n' "$O" "$O" > "$R"
  [ $BLOG = 1 ] && printf '[%s\\26000090]\r\n"Element"=hex:01\r\n\r\n' "$O" >> "$R"
  if [ -n "$NP" ] && [ "$NP" -gt 0 ] 2>/dev/null; then printf '[%s\\25000061]\r\n"Element"=hex:%02x,00,00,00,00,00,00,00\r\n\r\n' "$O" "$NP" >> "$R"; fi
  regmerge "$BCD" 'HKEY_LOCAL_MACHINE\BCD00000000' "$R" && echo "BCD ($DEF): bootstatuspolicy IgnoreAllFailures, recoveryenabled No${NP:+, numproc $NP}" || rojo "ERROR al escribir BCD"
  if [ "$NP" = 0 ] && [ -n "$(regget "$BCD" "Objects\\$DEF\\Elements\\25000061" Element)" ]; then
    printf 'cd \\Objects\\%s\\Elements\\25000061\ndel\ncommit\n' "$DEF" > "$T/del.hsh"
    caja hivexsh -w "$BCD" -f "$T/del.hsh" && echo "numproc quitado (todos los nucleos)"
  fi
else rojo "No encuentro {default} en el BCD"; fi

# Devolver las colmenas que hayan cambiado
for h in SYSTEM:"$W/System32/config/SYSTEM" SOFTWARE:"$W/System32/config/SOFTWARE" BCD:"$E/BCD"; do
  n=${h%%:*}; dst=${h#*:}
  if ! cmp -s "$T/$n" "$CP/$n"; then cp "$T/$n" "$dst" && echo "$n escrito en la tablet"; fi
done
rm -f "$R" "$T/del.hsh" "$W/bootstat.dat" "$E/bootstat.dat"
trap - EXIT; desmontar_todo
verde "Listo. Reinicia la tablet con Encendido."
