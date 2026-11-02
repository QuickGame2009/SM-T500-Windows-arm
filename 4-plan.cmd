@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\05-plan-diskpart.ps1" %*
echo.
pause
