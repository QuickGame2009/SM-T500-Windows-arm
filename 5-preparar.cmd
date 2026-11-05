@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd. Hacer doble clic o lanzar desde una consola de administrador.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\05-preparar.ps1" %*
echo.
pause
