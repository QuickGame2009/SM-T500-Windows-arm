@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\04-plan-partitions.ps1" %*
echo.
pause
