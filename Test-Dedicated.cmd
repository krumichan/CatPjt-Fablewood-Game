@echo off
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Test-Dedicated.ps1" %*
exit /b %errorlevel%
