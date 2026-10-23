@echo off
setlocal
cd /d "%~dp0"
where pwsh.exe >nul 2>nul
if errorlevel 1 (
  echo PowerShell 7.4 or later is required. Install it, then run this command again.
  exit /b 1
)
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Start-Backend.ps1" %*
exit /b %errorlevel%
