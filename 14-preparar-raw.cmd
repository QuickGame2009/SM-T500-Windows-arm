@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Sustituye a 5-preparar.cmd (sin que Windows reescriba la GPT).
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\14-preparar-raw.ps1" %*
echo.
pause
