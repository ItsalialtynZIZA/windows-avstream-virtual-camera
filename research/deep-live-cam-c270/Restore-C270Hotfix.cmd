@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-C270Hotfix.ps1" -Action Restore
if errorlevel 1 (echo Restoration not completed.)
pause
