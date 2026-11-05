@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Restaura la GPT original desde gpt-backup.bin.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\13-gpt-restore.ps1" %*
echo.
pause
