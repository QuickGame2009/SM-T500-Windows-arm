@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador.
rem Sin parametros solo lee.  Con -Aplicar crea ROOT\VOLMGR\0000.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\09-volmgr.ps1" %*
echo.
pause
