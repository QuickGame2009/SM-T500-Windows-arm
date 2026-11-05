@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Repara GUID de particion repetidos en la GPT real.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\12-gpt-fix.ps1" %*
echo.
pause
