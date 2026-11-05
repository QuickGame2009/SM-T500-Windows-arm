@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Monta Windows y copia los logs de arranque (ntbtlog, Panther, minidump). Activa bootlog.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\18-leer-logs.ps1" %*
echo.
pause
