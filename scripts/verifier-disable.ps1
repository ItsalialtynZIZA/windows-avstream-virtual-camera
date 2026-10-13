$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as Administrator.'
}

verifier.exe /reset
if ($LASTEXITCODE -ne 0) {
    throw "verifier.exe failed with exit code $LASTEXITCODE"
}

Write-Host 'Driver Verifier configuration reset. Reboot Windows.'
