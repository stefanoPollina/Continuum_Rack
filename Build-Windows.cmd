@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Build-Windows.ps1"
set "RACK_BUILD_RESULT=%ERRORLEVEL%"
echo.
if not "%RACK_BUILD_RESULT%"=="0" echo Build failed. Read the error above and START_HERE.md.
pause
exit /b %RACK_BUILD_RESULT%

