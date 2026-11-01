@echo off
setlocal
call :ChooseAPM
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-pluto-v2.ps1" -Multiplayer -Challenge -APM %APM%
echo.
echo Pluto v2.0 Challenge exit code: %ERRORLEVEL%
pause
exit /b

:ChooseAPM
echo Select Pluto command limit:
echo 1. 300 APM
echo 2. 500 APM (recommended starting point)
echo 3. 1000 APM
echo 4. Unlimited
set /p choice=Choose 1-4 [2]: 
if "%choice%"=="1" set "APM=300"& goto :eof
if "%choice%"=="3" set "APM=1000"& goto :eof
if "%choice%"=="4" set "APM=0"& goto :eof
set "APM=500"
goto :eof
