@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Start-Coop.ps1" -LatestPackaged %*
if errorlevel 1 pause