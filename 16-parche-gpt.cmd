@echo off
rem Con la tablet en TWRP (adb funcionando). Escribe la GPT corregida directamente desde la tablet (dd + sync).
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\16-parche-gpt.ps1" %*
echo.
pause
