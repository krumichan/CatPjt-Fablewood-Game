@echo off
setlocal
where pwsh.exe >nul 2>nul
if errorlevel 1 (
  echo PowerShell 7.4 or newer is required. Install it and run this launcher again.
  exit /b 1
)
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Start-OnlineServer.ps1" %*
set "FABLE_LAUNCH_EXIT=%errorlevel%"
if not "%FABLE_LAUNCH_EXIT%"=="0" pause
exit /b %FABLE_LAUNCH_EXIT%
