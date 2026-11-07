#!/usr/bin/env bash
# Deja un servicio temporal (LocalSystem) que en el proximo arranque espera 2 minutos y guarda en C:\DiagWin\ el estado
# de Windows (procesos, dispositivos con problemas, pantalla, energia, ultimos eventos) mientras la pantalla esta en blanco.
#   ./23-diagnostico-windows.sh          (preparar; tablet en modo almacenamiento)
#   ./23-diagnostico-windows.sh --leer   (despues de arrancar Windows y esperar ~4 min; copia los textos a ~/sm-t500/kit/)
. "$(dirname "$0")/comun.sh"
WIN=80; LEER=0
while [ $# -gt 0 ]; do case "$1" in --win) WIN=$2; shift;; --leer) LEER=1;; *) morir "Opcion desconocida: $1";; esac; shift; done
mkdir -p "$KIT"; exec > >(tee "$KIT/diag-23.txt") 2>&1
necesito_caja
DISCO=$(buscar_disco) || morir "No encuentro el disco de la tablet (03-massstorage.sh)."
montar "$WIN" "$MNT/win" ntfs; trap desmontar_todo EXIT
W="$MNT/win"; D="$W/DiagWin"
if [ $LEER = 1 ]; then
  [ -d "$D" ] || morir "No existe C:\\DiagWin: Windows no llego a ejecutar el servicio."
  O="$KIT/diagwin-$(date +%H%M%S)"; mkdir -p "$O"; cp "$D"/*.txt "$O"/ 2>/dev/null; ls -l "$O"; verde "Copiado a $O (sube la carpeta)"; exit 0
fi
rm -rf "$D"; mkdir -p "$D"
{
  printf '@echo off\r\ncd /d C:\\DiagWin\r\n'
  printf 'powercfg /change monitor-timeout-ac 0\r\npowercfg /change monitor-timeout-dc 0\r\npowercfg /change standby-timeout-ac 0\r\npowercfg /change standby-timeout-dc 0\r\npowercfg /h off\r\n'
  printf 'echo inicio > inicio.txt\r\ntasklist /v /fo list > tasklist-0.txt 2>&1\r\npnputil /enum-devices /problem > problemas.txt 2>&1\r\nquery session > sesiones-0.txt 2>&1\r\n'
  printf 'for %%%%s in (0 1 2) do (\r\n  timeout /t 20 /nobreak >nul\r\n  set /a n=%%%%s\r\n'
  printf '  tasklist /v /fo list > tasklist-%%%%s.txt 2>&1\r\n  query session > sesiones-%%%%s.txt 2>&1\r\n)\r\n'
  printf 'pnputil /enum-devices /problem > problemas.txt 2>&1\r\n'
  printf 'pnputil /enum-devices /class Display > pantalla.txt 2>&1\r\n'
  printf 'pnputil /enum-devices /class Monitor >> pantalla.txt 2>&1\r\n'
  printf 'powercfg /a > energia.txt 2>&1\r\npowercfg /requests >> energia.txt 2>&1\r\n'
  printf 'wevtutil qe System /c:80 /rd:true /f:text > eventos-system.txt 2>&1\r\n'
  printf 'wevtutil qe Application /c:50 /rd:true /f:text > eventos-app.txt 2>&1\r\n'
  printf 'wevtutil qe Microsoft-Windows-Dwm-Core/Diagnostic /c:30 /rd:true /f:text > eventos-dwm.txt 2>&1\r\n'
  printf 'reg query "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Video" /s > video-reg.txt 2>&1\r\n'
  printf 'sc delete DiagWinSvc > servicio.txt 2>&1\r\n'
} > "$D/diag.cmd"
T="$BASE/trabajo"; mkdir -p "$T"; S="$T/SYSTEM"; B="$KIT/SYSTEM.antes23-$(date +%H%M%S)"
cp "$W/Windows/System32/config/SYSTEM" "$S"; cp "$S" "$B"; echo "Copia de SYSTEM: $B   ($(colmena_ok "$S"))"
printf 'Windows Registry Editor Version 5.00\r\n\r\n[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\DiagWinSvc]\r\n"Type"=dword:00000010\r\n"Start"=dword:00000002\r\n"ErrorControl"=dword:00000000\r\n"ImagePath"="cmd.exe /v:on /c C:\\\\DiagWin\\\\diag.cmd"\r\n"ObjectName"="LocalSystem"\r\n' > "$T/diag.reg"
regmerge "$S" 'HKEY_LOCAL_MACHINE\SYSTEM' "$T/diag.reg" && cp "$S" "$W/Windows/System32/config/SYSTEM" && verde "Listo. Arranca Windows, espera ~4 min (aunque se ponga en blanco) y vuelve con:  $0 --leer"
