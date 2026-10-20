@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0AvCamPhysicalControl.ps1" -Action Uninstall
if errorlevel 1 echo Uninstall did not finish. Check the messages above.
pause
