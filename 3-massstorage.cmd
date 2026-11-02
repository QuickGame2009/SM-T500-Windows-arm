@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\03-massstorage.ps1" %*
echo.
pause
