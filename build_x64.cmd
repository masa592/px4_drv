@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo  PLEX PX-S1UD WinUSB BonDriver Build Script (x64)
echo ===================================================

rem Check MSBuild existence in PATH or vswhere
where msbuild >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set "MSBUILD_EXE=msbuild"
    goto :BUILD
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe was not found. Please install Visual Studio Build Tools.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD_EXE=%%i"
)

if not defined MSBUILD_EXE (
    echo [ERROR] MSBuild.exe was not found.
    exit /b 1
)

:BUILD
echo Found MSBuild: "%MSBUILD_EXE%"

set "SOLUTION_DIR=%~dp0winusb"
if not exist "%SOLUTION_DIR%\px4_winusb.sln" (
    set "SOLUTION_DIR=%~dp0"
)

echo Building BonDriver_S1UD_WinUSB x64 Release...
"%MSBUILD_EXE%" "%SOLUTION_DIR%\src\BonDriver_S1UD_WinUSB\BonDriver_S1UD_WinUSB.vcxproj" /p:Configuration=Release-static /p:Platform=x64 /m
set "BUILD_RESULT=%ERRORLEVEL%"

if %BUILD_RESULT% neq 0 (
    echo [ERROR] Build failed with exit code %BUILD_RESULT%.
    exit /b %BUILD_RESULT%
)

echo [SUCCESS] Build completed successfully.
echo Output: %SOLUTION_DIR%\build\x64\Release-static\BonDriver_S1UD_WinUSB.dll
exit /b 0
