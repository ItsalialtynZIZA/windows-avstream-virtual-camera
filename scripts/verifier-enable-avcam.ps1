$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as Administrator.'
}

Write-Host 'Configuring Driver Verifier for AvCam.sys only.'
Write-Host 'This is intended for a dedicated development/test machine.'

verifier.exe /reset | Out-Null
verifier.exe /standard /driver AvCam.sys
if ($LASTEXITCODE -ne 0) {
    throw "verifier.exe failed with exit code $LASTEXITCODE"
}

Write-Host ''
Write-Host 'Driver Verifier is configured for AvCam.sys.'
Write-Host 'Reboot Windows before testing.'
Write-Host 'If the machine becomes unstable, boot to recovery/safe mode and run verifier.exe /reset.'
