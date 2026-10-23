@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Test-Talents.ps1" %*
exit /b %ERRORLEVEL%
