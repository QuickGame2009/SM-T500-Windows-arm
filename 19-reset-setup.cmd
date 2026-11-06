@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Pone a 0 las marcas de instalacion a medias (Setup) del registro SYSTEM.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\19-reset-setup.ps1" %*
echo.
pause
