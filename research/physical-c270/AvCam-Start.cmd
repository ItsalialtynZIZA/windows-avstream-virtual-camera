@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-AvCamPreview.ps1" %*
set "avcam_exit=%ERRORLEVEL%"
if not "%avcam_exit%"=="0" (
    echo.
    echo AvCam start failed. See the message above.
    pause
)
exit /b %avcam_exit%
