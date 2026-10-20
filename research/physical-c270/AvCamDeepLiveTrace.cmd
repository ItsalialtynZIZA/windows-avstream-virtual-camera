@echo off
setlocal
set "HERE=%~dp0"
echo AvCam Deep Live Cam trace: close Chrome and Windows Camera first.
echo Device MFT must already be explicitly enabled for this test.
echo This command records diagnostic events; it does not install or enable MFT.
echo When TRACE_STARTED appears, open Deep Live Cam and try Logitech C270 once.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%HERE%AvCamPhysicalCaptureEtw.ps1" -Target DeepLiveCam -Seconds 45
echo.
echo Trace finished. Copy the PowerShell output or provide the ETL_FILE path.
pause
