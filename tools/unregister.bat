@echo off

cd /d "%~dp0"

REM Check for PowerShell Core (pwsh)
where pwsh >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set PS_CMD=pwsh
) else (
    set PS_CMD=powershell
)

%PS_CMD% -ExecutionPolicy Bypass -File "register.ps1" -Unregister
pause
