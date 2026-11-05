@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Solo lee; no cambia nada en Windows.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\07-diag-drivers.ps1" %*
echo.
pause
