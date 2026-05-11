@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-steamvr-bindings.ps1"
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
    echo SteamVR controller binding setup completed.
) else (
    echo SteamVR controller binding setup failed with exit code %RESULT%.
)

echo.
pause
exit /b %RESULT%
