@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-pluto-v2.ps1" -Multiplayer
echo.
echo Pluto v2.0 Multiplayer exit code: %ERRORLEVEL%
pause
