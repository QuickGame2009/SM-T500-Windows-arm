@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador.
rem Sin parametros: solo lee. Con -Aplicar: hace copia de las colmenas y registra los INF de almacenamiento con DISM.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\08-dism-drivers.ps1" %*
echo.
pause
