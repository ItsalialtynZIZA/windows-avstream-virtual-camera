# Explicit, reversible opt-in, user-level Deep Live Cam 2.2.4 update.
# Installs patched EXE files and AvCamMfCapture.dll; leaves cameras/drivers alone.
[CmdletBinding()]
param([ValidateSet('Install','Restore')][string]$Action='Install')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$target=Join-Path $env:LOCALAPPDATA 'Programs\DeepLiveCamStudio'
$backups=Join-Path $env:LOCALAPPDATA 'DeepLiveCamStudio\NativeMfC270Backups'
$exes=@('DeepLiveCamStudio.exe','DeepLiveCamStudioCLI.exe')
$payload=@('DeepLiveCamStudio.exe','DeepLiveCamStudioCLI.exe','AvCamMfCapture.dll')
if (-not (Test-Path -LiteralPath (Join-Path $target '_internal') -PathType Container)) {
    throw 'Expected per-user Deep Live Cam 2.2.4 installation not found. No changes made.'
}
$running=@(Get-Process -Name DeepLiveCamStudio,DeepLiveCamStudioCLI -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) { throw 'Close Deep Live Cam and its CLI before proceeding. No changes made.' }
foreach ($exe in $exes) {
    if (-not (Test-Path -LiteralPath (Join-Path $target $exe) -PathType Leaf)) {
        throw "Installed app EXE missing: $exe. No changes made."
    }
}
if ($Action -eq 'Install') {
    foreach ($item in $payload) {
        if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $item) -PathType Leaf)) {
            throw "Patch incomplete: $item. No changes made."
        }
    }
    $backup=Join-Path $backups (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $originalDll=Test-Path -LiteralPath (Join-Path $target 'AvCamMfCapture.dll')
    [IO.File]::WriteAllText((Join-Path $backup 'HadNativeDll.txt'),
            $(if ($originalDll) { 'true' } else { 'false' }))
    foreach ($file in $exes) {
        Copy-Item -LiteralPath (Join-Path $target $file) -Destination (Join-Path $backup $file)
    }
    if ($originalDll) {
        Copy-Item -LiteralPath (Join-Path $target 'AvCamMfCapture.dll') -Destination $backup
    }
    try {
        foreach ($file in $payload) {
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $target $file) -Force
        }
    } catch {
        foreach ($file in $exes) {
            Copy-Item -LiteralPath (Join-Path $backup $file) -Destination (Join-Path $target $file) -Force
        }
        if ($originalDll) {
            Copy-Item -LiteralPath (Join-Path $backup 'AvCamMfCapture.dll') -Destination $target -Force
        } else {
            Remove-Item -LiteralPath (Join-Path $target 'AvCamMfCapture.dll') -Force -ErrorAction SilentlyContinue
        }
        throw
    }
    Write-Host 'DLC_NATIVE_MF_C270_INSTALLED'
    Write-Host "Original executables backed up: $backup"
    Write-Host 'AvCam Device MFT was NOT changed. Keep it uninstalled.'
} else {
    $backup=Get-ChildItem -LiteralPath $backups -Directory -ErrorAction SilentlyContinue |
        Where-Object { (Test-Path -LiteralPath (Join-Path $_.FullName $exes[0])) -and
                       (Test-Path -LiteralPath (Join-Path $_.FullName $exes[1])) } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $backup) { throw 'No original executable backup found.' }
    foreach ($file in $exes) {
        Copy-Item -LiteralPath (Join-Path $backup.FullName $file) -Destination (Join-Path $target $file) -Force
    }
    $flag=Get-Content -LiteralPath (Join-Path $backup.FullName 'HadNativeDll.txt') -Raw
    if ($flag.Trim() -eq 'true') {
        Copy-Item -LiteralPath (Join-Path $backup.FullName 'AvCamMfCapture.dll') -Destination $target -Force
    } else {
        Remove-Item -LiteralPath (Join-Path $target 'AvCamMfCapture.dll') -Force -ErrorAction SilentlyContinue
    }
    Write-Host 'DLC_NATIVE_MF_C270_RESTORED'
    Write-Host "Restored backup: $($backup.FullName)"
}
