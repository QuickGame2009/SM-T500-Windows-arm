@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Con el parametro -Aplicar ademas anade las claves que falten.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\06-diag-volumen.ps1" %*
echo.
pause
