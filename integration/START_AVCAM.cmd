@echo off
setlocal
cd /d "%~dp0"
if not exist "feeder\AvCamFeeder.exe" (
  echo ERROR: feeder\AvCamFeeder.exe not found.
  pause
  exit /b 2
)
echo AvCam integrated preview: OBS virtual camera to existing AvCam sink.
echo No driver or camera settings will be installed or modified.
echo Start OBS Studio, make sure it shows your intended source, then click Start Virtual Camera.
echo.
echo This launcher only uses the named OBS Virtual Camera device.
echo If the virtual camera AvCam is not installed, the feeder will wait for the sink.
echo.
echo Keep this window open. Press Ctrl+C to stop.
echo.
"feeder\AvCamFeeder.exe" --source obs-dshow --device-name "OBS Virtual Camera"
set "RC=%ERRORLEVEL%"
echo.
echo AvCam feeder exited with code %RC%.
pause
exit /b %RC%
