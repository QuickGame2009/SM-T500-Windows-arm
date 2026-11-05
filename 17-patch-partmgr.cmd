@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Monta Windows, copia canary3.sys y parchea partmgr.sys.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\17-patch-partmgr.ps1" %*
echo.
pause
