@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Instala el registro SYSTEM con 25 controladores desactivados (Start=4).
rem Para volver al original:  21-controladores-minimos.cmd -Restaurar
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\21-controladores-minimos.ps1" %*
echo.
pause
