@echo off
rem Ejecutar DESPUES de 14-preparar-raw.cmd y de volver a ejecutar 3-massstorage.cmd, como administrador.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\15-gpt-final.ps1" %*
echo.
pause
