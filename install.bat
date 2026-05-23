@echo off
REM Install the Claude Usage Daemon as a Windows scheduled task.
REM
REM Creates a task named "Claude Usage Daemon" that runs at computer
REM startup under the current user's identity (via S4U so no password
REM is stored), with auto-restart on failure. Requires elevation
REM because S4U registration touches the local security policy.

setlocal enabledelayedexpansion

set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"
set "SCRIPT=%REPO_DIR%\daemon\claude_usage_daemon.py"
set "WORKDIR=%REPO_DIR%\daemon"
set "TASK_NAME=Claude Usage Daemon"

REM --- Elevation check -------------------------------------------------------
net session >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: This installer must be run as Administrator.
    echo Right-click install.bat and choose "Run as administrator".
    exit /b 1
)

REM --- Python on PATH --------------------------------------------------------
where python >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: python.exe not found in PATH.
    echo Install Python from https://www.python.org/downloads/ and tick
    echo "Add Python to PATH" during the installer, then re-run this script.
    exit /b 1
)

for /f "delims=" %%P in ('where python') do (
    set "PYTHON_EXE=%%P"
    goto :got_python
)
:got_python
echo Using Python: %PYTHON_EXE%

REM Prefer pythonw.exe so no console window appears at boot.
set "PYTHONW_EXE=%PYTHON_EXE:python.exe=pythonw.exe%"
if not exist "%PYTHONW_EXE%" set "PYTHONW_EXE=%PYTHON_EXE%"
echo Launcher:     %PYTHONW_EXE%

REM --- Sanity-check the daemon script ---------------------------------------
if not exist "%SCRIPT%" (
    echo ERROR: Daemon script not found at %SCRIPT%
    exit /b 1
)

REM --- Install Python dependencies ------------------------------------------
echo.
echo Installing Python dependencies (bleak, httpx)...
"%PYTHON_EXE%" -m pip install --upgrade bleak httpx
if %errorlevel% neq 0 (
    echo ERROR: pip install failed.
    exit /b 1
)

REM --- Register the scheduled task ------------------------------------------
echo.
echo Registering scheduled task "%TASK_NAME%"...

set "PS_SCRIPT=%TEMP%\install-claude-daemon.ps1"
> "%PS_SCRIPT%" echo $ErrorActionPreference = 'Stop'
>>"%PS_SCRIPT%" echo $user      = "$env:USERDOMAIN\$env:USERNAME"
>>"%PS_SCRIPT%" echo $action    = New-ScheduledTaskAction -Execute '%PYTHONW_EXE%' -Argument '"%SCRIPT%"' -WorkingDirectory '%WORKDIR%'
>>"%PS_SCRIPT%" echo $trigger   = New-ScheduledTaskTrigger -AtStartup
>>"%PS_SCRIPT%" echo $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType S4U -RunLevel Limited
>>"%PS_SCRIPT%" echo $settings  = New-ScheduledTaskSettingsSet -RestartInterval (New-TimeSpan -Minutes 1) -RestartCount 999 -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero)
>>"%PS_SCRIPT%" echo Register-ScheduledTask -TaskName '%TASK_NAME%' -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Polls Anthropic API for Claude Code usage and pushes payloads to the Clawdmeter over BLE.' -Force ^| Out-Null
>>"%PS_SCRIPT%" echo Write-Host "Registered as $user (S4U, AtStartup, auto-restart every 1 min on failure)."

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%"
set "RC=%errorlevel%"
del "%PS_SCRIPT%" >nul 2>nul

if %RC% neq 0 (
    echo.
    echo ERROR: Failed to register scheduled task ^(exit %RC%^).
    echo If you saw "A specified logon session does not exist", your account
    echo may not support S4U ^(common on non-domain local accounts^). Edit this
    echo script and change -LogonType S4U to -LogonType Interactive plus
    echo -Trigger ... -AtLogOn to fall back to a logon trigger.
    exit /b %RC%
)

echo.
echo Done. Task "%TASK_NAME%" is installed.
echo.
echo Useful commands:
echo   Start now: schtasks /Run    /TN "%TASK_NAME%"
echo   Stop:      schtasks /End    /TN "%TASK_NAME%"
echo   Remove:    schtasks /Delete /TN "%TASK_NAME%" /F
echo   Status:    schtasks /Query  /TN "%TASK_NAME%" /V /FO LIST

endlocal
