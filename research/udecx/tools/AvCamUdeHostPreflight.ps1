# Physical Windows 11 preflight. No kernel/PNP changes; optional restore point.
# Run in an elevated PowerShell. Does NOT disable Secure Boot or BitLocker.
# Requires an existing recent restore point or -CreateRestorePoint.
#Requires -RunAsAdministrator
[CmdletBinding()]
param([switch]$CreateRestorePoint)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$os = Get-CimInstance Win32_OperatingSystem
$system = Get-CimInstance Win32_ComputerSystem
if ($null -eq $os -or $null -eq $system -or
    [version]$os.Version -lt [version]'10.0.22000' -or
    -not [Environment]::Is64BitOperatingSystem) {
    throw 'HOST_PREFLIGHT_REFUSED: Windows 11 x64 is required.'
}
$identity = "$($system.Manufacturer) $($system.Model)"
if ($identity -match '(?i)virtual machine|virtualbox|vmware|qemu|kvm|bochs|xen|parallels|hyper-v') {
    throw 'HOST_PREFLIGHT_REFUSED: run this package on the physical host only.'
}
$controllers = @(Get-PnpDevice -ErrorAction Stop | Where-Object {
    $_.InstanceId -match '(?i)^ROOT\\AVCAMUDECONTROLLER(\\|$)'
})
if ($controllers.Count -gt 1) {
    throw 'More than one AvCamUdeController root device exists. Inspect before proceeding.'
}
if ($CreateRestorePoint) {
    Checkpoint-Computer -Description 'Before AvCam UdeCx kernel experiment' -RestorePointType MODIFY_SETTINGS
}
$points = @(Get-ComputerRestorePoint -ErrorAction SilentlyContinue |
    Sort-Object -Property CreationTime -Descending)
if ($points.Count -eq 0) {
    throw 'No System Restore point exists. Enable protection of the Windows system drive and create one, or create a verified full-system backup before installing.'
}
$latest = $points[0].CreationTime
Write-Host "Latest restore point creation time: $latest"
# A restore point does not replace a full-system image backup.
$secureBoot = $null
try { $secureBoot = Confirm-SecureBootUEFI -ErrorAction Stop } catch {
    Write-Warning "Secure Boot status unavailable: $($_.Exception.Message)"
}
if ($secureBoot -eq $true) {
    Write-Warning 'Secure Boot is enabled. The test-signed driver may be refused. This script does NOT modify firmware settings.'
}
try {
    $volume = Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction Stop
    if ($volume.ProtectionStatus -eq 'On') {
        Write-Warning 'BitLocker is enabled: confirm the recovery key is safely available before changing boot policy.'
    }
} catch {
    Write-Warning "BitLocker status unavailable: $($_.Exception.Message)"
}
Write-Host "OS: $($os.Caption) / $($os.Version)"
Write-Host "Hardware: $identity"
Write-Host "Existing UdeCx research root devices: $($controllers.Count)"
Write-Host 'HOST_PREFLIGHT_REVIEW_COMPLETE'
Write-Host 'Not a guarantee against BSOD. Keep external recovery media available.'
