@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Solo lee y copia drivers a drv-copia\
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\10-partmgr.ps1" %*
echo.
pause
