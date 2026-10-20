@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-C270Hotfix.ps1" -Action Install
if errorlevel 1 (echo C270 update not completed.)
pause
