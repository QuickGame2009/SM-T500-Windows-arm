@echo off
rem Diagnostico del servicio Plug and Play, para ejecutar EN LA TABLET.
rem Uso: abre un cmd como administrador y ejecuta:  diag-plugplay.cmd [ruta\Procmon64a.exe]
rem Deja los resultados en C:\DiagPnP (sube esa carpeta).
setlocal enabledelayedexpansion
net session >nul 2>&1 || (echo Ejecuta este script como administrador. & exit /b 1)

set "OUT=C:\DiagPnP"
if exist "%OUT%" rd /s /q "%OUT%" >nul 2>&1
mkdir "%OUT%" || (echo No puedo crear %OUT% & exit /b 1)

rem --- Buscar Procmon ---
set "PM=%~1"
if not defined PM if exist "%~dp0Procmon64a.exe" set "PM=%~dp0Procmon64a.exe"
if not defined PM if exist "%~dp0Procmon64.exe" set "PM=%~dp0Procmon64.exe"
if not defined PM for %%P in (Procmon64a.exe Procmon64.exe) do if not defined PM where %%P >nul 2>&1 && set "PM=%%P"
if not defined PM (echo AVISO: no encuentro Procmon; solo se recogeran datos basicos.) else echo Procmon: %PM%

echo [1/6] Estado inicial y configuracion
(
  echo ==== sc queryex plugplay ====
  sc queryex plugplay
  echo ==== sc qc plugplay ====
  sc qc plugplay
  echo ==== reg PlugPlay ====
  reg query HKLM\SYSTEM\CurrentControlSet\Services\PlugPlay /s
  echo ==== dam / bam / BrokerInfrastructure ====
  for %%S in (dam bam BrokerInfrastructure keepalive) do (
    echo -- %%S
    sc qc %%S
    sc query %%S
  )
  echo ==== hora ====
  date /t & time /t
) > "%OUT%\1-estado-inicial.txt" 2>&1

echo [2/6] Dispositivos con problema
pnputil /enum-devices /problem > "%OUT%\2-problemas.txt" 2>&1
pnputil /enum-devices /instanceid "ACPI\HMXT8310\1" /properties > "%OUT%\2-tactil.txt" 2>&1

echo [3/6] Captura de Procmon + arranque del servicio
if defined PM (
  start "" /min "%PM%" /AcceptEula /Quiet /Minimized /BackingFile "%OUT%\pm.pml"
  timeout /t 8 /nobreak >nul
)
sc start plugplay > "%OUT%\3-sc-start.txt" 2>&1
echo Esperando 60 s a ver si el servicio llega a RUNNING...
for /l %%i in (1,1,6) do (
  timeout /t 10 /nobreak >nul
  sc queryex plugplay | findstr /i "STATE PID" >> "%OUT%\3-estados.txt"
  echo --- >> "%OUT%\3-estados.txt"
)
sc queryex plugplay > "%OUT%\3-estado-final.txt" 2>&1

echo [4/6] Procesos y servicios en ese momento
tasklist /svc /fo list > "%OUT%\4-tasklist-svc.txt" 2>&1
tasklist /m /fi "imagename eq svchost.exe" > "%OUT%\4-svchost-modulos.txt" 2>&1

echo [5/6] Detener captura y filtrar
if defined PM (
  "%PM%" /Terminate
  timeout /t 8 /nobreak >nul
  "%PM%" /AcceptEula /Quiet /OpenLog "%OUT%\pm.pml" /SaveAs "%OUT%\pm.csv"
  timeout /t 5 /nobreak >nul
  if exist "%OUT%\pm.csv" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag-plugplay-filtro.ps1" "%OUT%"
  ) else echo No se genero pm.csv; sube pm.pml
)

echo [6/6] Eventos
wevtutil qe System /c:60 /rd:true /f:text > "%OUT%\6-eventos-system.txt" 2>&1
wevtutil qe Application /c:40 /rd:true /f:text > "%OUT%\6-eventos-app.txt" 2>&1
wevtutil qe Microsoft-Windows-Kernel-PnP/Configuration /c:60 /rd:true /f:text > "%OUT%\6-eventos-kernelpnp.txt" 2>&1
copy /y C:\Windows\INF\setupapi.dev.log "%OUT%\6-setupapi.dev.log" >nul 2>&1

echo.
echo Hecho. Sube la carpeta %OUT% (pm.pml no hace falta si existen los 5-*.csv).
echo El pm.csv completo puede ser muy grande; sube primero los 5-*.csv.
endlocal
