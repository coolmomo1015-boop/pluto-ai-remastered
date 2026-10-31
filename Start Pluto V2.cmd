@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-pluto-v2.ps1"
echo.
echo Pluto v2.0 exit code: %ERRORLEVEL%
pause
