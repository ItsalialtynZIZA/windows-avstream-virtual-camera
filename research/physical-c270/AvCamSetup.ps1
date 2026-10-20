# One-time AvCam Physical C270 setup; idempotent for a registered MFT.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$state = Join-Path $env:ProgramFiles 'AvCamPhysicalC270\install-state.json'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $ps = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $args = '-NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $PSCommandPath
    $process = Start-Process -FilePath $ps -ArgumentList $args -Verb RunAs -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Elevated setup failed with code $($process.ExitCode)" }
    return
}
$installScript = Join-Path $root 'AvCamPhysicalInstall.ps1'
$controlScript = Join-Path $root 'AvCamPhysicalControl.ps1'
if (-not (Test-Path -LiteralPath $installScript)) { throw 'Missing AvCamPhysicalInstall.ps1' }
if (-not (Test-Path -LiteralPath $controlScript)) { throw 'Missing AvCamPhysicalControl.ps1' }
if (-not (Test-Path -LiteralPath $state -PathType Leaf)) {
    Write-Host 'First-time setup: Device MFT registration needs explicit consent.'
    & $installScript -Enable
    if (-not (Test-Path -LiteralPath $state -PathType Leaf)) {
        throw 'Device MFT installation was not completed; background task not created.'
    }
} else {
    Write-Host 'Existing physical C270 Device MFT detected. Leaving working DLL untouched.'
}
& $controlScript -Action Setup
Write-Host 'AVCAM_PHYSICAL_C270_READY'
