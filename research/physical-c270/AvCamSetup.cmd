@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0AvCamSetup.ps1"
if errorlevel 1 (
  echo AvCam setup stopped with an error. No USB driver has been changed.
  pause
  exit /b 1
)
echo AvCam Physical C270 setup finished. Use Start Menu - AvCam Physical C270.
pause
