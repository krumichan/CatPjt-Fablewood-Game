@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Package-Prototype.ps1" %*
set "FABLEWOOD_PACKAGE_EXIT=%ERRORLEVEL%"
if not "%FABLEWOOD_PACKAGE_EXIT%"=="0" pause
exit /b %FABLEWOOD_PACKAGE_EXIT%
