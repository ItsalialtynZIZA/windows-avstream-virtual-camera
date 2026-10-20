# Install or restore an opt-in Deep Live Cam 2.2.4 C270 capture hotfix.
# Replaces ONLY the two user-level application EXEs and backs them up.
[CmdletBinding()]
param([ValidateSet('Install','Restore')][string]$Action = 'Install')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$folder = Join-Path $env:LOCALAPPDATA 'Programs\DeepLiveCamStudio'
$backupRoot = Join-Path $env:LOCALAPPDATA 'DeepLiveCamStudio\C270HotfixBackups'
$files = @('DeepLiveCamStudio.exe','DeepLiveCamStudioCLI.exe')
if (-not (Test-Path -LiteralPath (Join-Path $folder '_internal') -PathType Container)) {
    throw "Deep Live Cam 2.2.4 per-user app runtime not found at $folder. No changes made."
}
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $folder $file) -PathType Leaf)) {
        throw "Missing installed application executable: $file. No changes made."
    }
}
$processes = @(Get-Process -Name DeepLiveCamStudio,DeepLiveCamStudioCLI -ErrorAction SilentlyContinue)
if ($processes.Count -gt 0) {
    throw 'Close Deep Live Cam Studio and CLI before updating or restoring. No programs were stopped.'
}
if ($Action -eq 'Install') {
    foreach ($file in $files) {
        if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $file) -PathType Leaf)) {
            throw "Incomplete patch package: $file. No changes made."
        }
    }
    $same = $true
    foreach ($file in $files) {
        if ((Get-FileHash -LiteralPath (Join-Path $folder $file) -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $file) -Algorithm SHA256).Hash) {
            $same = $false
        }
    }
    if ($same) {
        Write-Host 'DLC_C270_HOTFIX_ALREADY_INSTALLED'
        return
    }
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    $backup = Join-Path $backupRoot (Get-Date -Format 'yyyyMMdd-HHmmss')
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    foreach ($file in $files) {
        Copy-Item -LiteralPath (Join-Path $folder $file) -Destination (Join-Path $backup $file) -ErrorAction Stop
    }
    try {
        foreach ($file in $files) {
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $folder $file) -Force -ErrorAction Stop
        }
    } catch {
        foreach ($file in $files) {
            Copy-Item -LiteralPath (Join-Path $backup $file) -Destination (Join-Path $folder $file) -Force
        }
        throw
    }
    Write-Host 'DLC_C270_HOTFIX_APPLIED'
    Write-Host "Original executables backed up at: $backup"
    Write-Host 'Run DeepLiveCamStudioCLI.exe from the normal install location.'
    Write-Host 'AvCam Device MFT remains untouched; do not reinstall it yet.'
} elseif ($Action -eq 'Restore') {
    if (-not (Test-Path -LiteralPath $backupRoot -PathType Container)) {
        throw "No Deep Live Cam executable backups found at $backupRoot."
    }
    $backup = Get-ChildItem -LiteralPath $backupRoot -Directory |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName $files[0])) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName $files[1]))
        } | Sort-Object Name -Descending | Select-Object -First 1
    if ($null -eq $backup) { throw 'No complete C270 hotfix backup found.' }
    foreach ($file in $files) {
        Copy-Item -LiteralPath (Join-Path $backup.FullName $file) -Destination (Join-Path $folder $file) -Force
    }
    Write-Host 'DLC_C270_ORIGINAL_EXES_RESTORED'
    Write-Host "Restored from: $($backup.FullName)"
}
