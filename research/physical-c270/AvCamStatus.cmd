@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0AvCamPhysicalControl.ps1" -Action Status
pause
