@echo off
:: PathMux Camera Profile Collection — Windows launcher  v1.1
:: Double-click this file to run the collection script.
:: Requires PowerShell 5.1 or later (built into Windows 10/11).
::
:: https://github.com/Nutball-Labs/PathMux/issues/new/choose
:: SN: 00089

setlocal

:: Check that PowerShell is available
where powershell.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo  ERROR: PowerShell not found.
    echo  Windows 10 and 11 include PowerShell 5.1 by default.
    echo  If you are on an older OS, install PowerShell from:
    echo  https://aka.ms/powershell
    echo.
    pause
    exit /b 1
)

:: Resolve the directory this .bat lives in so we can find the .ps1
set "SCRIPT_DIR=%~dp0"
set "PS1=%SCRIPT_DIR%collect_cam_profile.ps1"

if not exist "%PS1%" (
    echo.
    echo  ERROR: collect_cam_profile.ps1 not found next to this file.
    echo  Make sure both files are in the same folder.
    echo.
    pause
    exit /b 1
)

:: Run the PowerShell script.
:: -ExecutionPolicy Bypass  — allows running unsigned local scripts without
::   changing the system-wide policy.  Safe for a single-run script like this.
:: -NoProfile               — faster startup, avoids user profile side-effects.
:: -File                    — run the named script file.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS1%"

endlocal
