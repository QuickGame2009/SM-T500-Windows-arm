@echo off
rem ============================================================================
rem Aplica Windows 11 ARM64 en las particiones "win" y "esp" de la tablet.
rem Requisitos: la tablet en TWRP con MassStorage-emmc.sh en marcha, CMD como
rem administrador, y las particiones win y esp con letra de unidad asignada
rem (diskpart: sel dis N / sel par X / assign letter=R).
rem
rem Uso:    apply-windows.cmd <install.wim> <letra_win> <letra_esp> [indice]
rem Ejemplo: apply-windows.cmd D:\wim\install.wim R X 1
rem
rem Basado en Project-Silicium/Guides (General/OS/Full-Windows).
rem ============================================================================
setlocal
if "%~3"=="" (
  echo Uso: %~nx0 ^<install.wim^> ^<letra_win^> ^<letra_esp^> [indice]
  exit /b 1
)
set "WIM=%~1"
set "WINL=%~2:"
set "ESPL=%~3:"
set "IDX=%~4"
if "%IDX%"=="" set "IDX=1"

if not exist "%WIM%" (echo No existe %WIM% & exit /b 1)
if not exist "%WINL%\" (echo No existe la unidad %WINL% & exit /b 1)
if not exist "%ESPL%\" (echo No existe la unidad %ESPL% & exit /b 1)

echo.
echo Unidad de Windows (debe ser la particion "win", NTFS):
vol %WINL%
echo Unidad ESP (debe ser la particion "esp", FAT32):
vol %ESPL%
echo.
echo Se va a BORRAR y escribir en %WINL% y %ESPL%. Comprueba que NO son discos de tu PC.
pause

echo.
echo [1/5] Aplicando la imagen (tarda varios minutos)...
dism /apply-image /ImageFile:"%WIM%" /index:%IDX% /ApplyDir:%WINL%\ || goto :fail

echo.
echo [2/5] Creando los ficheros de arranque en la ESP...
bcdboot %WINL%\Windows /s %ESPL% /f UEFI || goto :fail

echo.
echo [3/5] Configurando el BCD...
set "BCD=%ESPL%\EFI\Microsoft\Boot\BCD"
bcdedit /store "%BCD%" /set "{default}" testsigning on || goto :fail
bcdedit /store "%BCD%" /set "{default}" nointegritychecks on || goto :fail
bcdedit /store "%BCD%" /set "{default}" recoveryenabled no || goto :fail

echo.
echo [4/5] Arreglo del USB (OsDefaultRoleSwitchMode = 1)...
reg load HKLM\OFFLINE %WINL%\Windows\System32\Config\SYSTEM || goto :fail
reg add HKLM\OFFLINE\ControlSet001\Control\USB /v OsDefaultRoleSwitchMode /t REG_DWORD /d 1 /f
reg unload HKLM\OFFLINE

echo.
echo [5/5] Respuesta desatendida (salta el asistente inicial)...
if exist "%~dp0unattend.xml" (
  if not exist "%WINL%\Windows\Panther" mkdir "%WINL%\Windows\Panther"
  copy /y "%~dp0unattend.xml" "%WINL%\Windows\Panther\unattend.xml"
) else (
  echo No hay unattend.xml junto al script; se omite.
)

echo.
echo Hecho. Reinicia la tablet (quita el almacenamiento masivo) y arranca el UEFI.
exit /b 0

:fail
echo.
echo ERROR en el paso anterior. No sigas; revisa el mensaje.
reg unload HKLM\OFFLINE >nul 2>&1
exit /b 1
