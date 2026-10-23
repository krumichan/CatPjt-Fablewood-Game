@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Test-Coop.ps1" %*
if errorlevel 1 pause