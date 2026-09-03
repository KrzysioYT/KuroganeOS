@echo off
setlocal EnableExtensions

set "ROOT=%~dp0..\.."
set "BUILD_SCRIPT=%ROOT%\scripts\build.ps1"

if not exist "%BUILD_SCRIPT%" (
    echo ERROR: Missing build script: "%BUILD_SCRIPT%" 1>&2
    exit /b 1
)

set "POWERSHELL_EXE="
where powershell.exe >nul 2>&1
if not errorlevel 1 set "POWERSHELL_EXE=powershell.exe"

if not defined POWERSHELL_EXE (
    where pwsh.exe >nul 2>&1
    if not errorlevel 1 set "POWERSHELL_EXE=pwsh.exe"
)

if not defined POWERSHELL_EXE (
    echo ERROR: Neither powershell.exe nor pwsh.exe is available. 1>&2
    exit /b 1
)

"%POWERSHELL_EXE%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%BUILD_SCRIPT%" %*
set "BUILD_EXIT=%ERRORLEVEL%"

if not "%BUILD_EXIT%"=="0" (
    echo ERROR: KuroganeOS build failed with exit code %BUILD_EXIT%. 1>&2
)

exit /b %BUILD_EXIT%
