@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Build.ps1" -Action Editor
if errorlevel 1 pause
endlocal
