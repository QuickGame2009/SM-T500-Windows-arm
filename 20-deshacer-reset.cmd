@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Restaura el registro SYSTEM guardado antes de 19-reset-setup.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\20-deshacer-reset.ps1" %*
echo.
pause
