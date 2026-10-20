@echo off
set "EXE=%LOCALAPPDATA%\Programs\DeepLiveCamStudio\DeepLiveCamStudioCLI.exe"
if not exist "%EXE%" (echo Deep Live Cam is not installed & pause & exit /b 1)
start "DeepLiveCamStudio C270 MF" /D "%LOCALAPPDATA%\Programs\DeepLiveCamStudio" "%EXE%"
echo Deep Live Cam: choose Logitech C270 and Start Live.
echo OBS: capture the Live Preview window, THEN Start Virtual Camera.
echo AvCam Device MFT remains UNINSTALLED. Physical USB camera is unchanged.
pause
