@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 with C++ workload.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\amd64\MSBuild.exe`) do set MSBUILD=%%i

if not defined MSBUILD (
    echo ERROR: MSBuild.exe not found via vswhere.
    exit /b 1
)

set VCXPROJ=%~dp0Windows\VisualStudio\quakespasm-sdl2.vcxproj
set CONFIG=Release
set PLATFORM=x64

if /i "%1"=="debug"   set CONFIG=Debug
if /i "%1"=="Debug"   set CONFIG=Debug
if /i "%2"=="x86"     set PLATFORM=Win32
if /i "%2"=="Win32"   set PLATFORM=Win32

echo Building %CONFIG%^|%PLATFORM%...
"%MSBUILD%" "%VCXPROJ%" -p:Configuration=%CONFIG% -p:Platform=%PLATFORM% -m -nologo

if errorlevel 1 (
    echo.
    echo Build FAILED.
    exit /b 1
)

echo.
echo Build succeeded.
echo Output: %~dp0Windows\VisualStudio\Build-quakespasm-sdl2\%PLATFORM%\%CONFIG%\quakespasm-openvr.exe
