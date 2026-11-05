@echo off
rem Ejecutar DESPUES de 3-massstorage.cmd, como administrador. Solo lee la tabla GPT.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\11-gpt.ps1" %*
echo.
pause
