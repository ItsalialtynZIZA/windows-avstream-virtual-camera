# Targeted rollback of the NEW UdeCx root only, never the legacy AvCamBus.
# Run as Administrator. Nothing is removed unless -Execute is supplied.
#Requires -RunAsAdministrator
[CmdletBinding()]
param([switch]$Execute)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$devices = @(Get-PnpDevice -ErrorAction Stop | Where-Object {
    $_.InstanceId -match '(?i)^ROOT\\AVCAMUDECONTROLLER(\\|$)'
})
if ($devices.Count -gt 1) {
    throw 'Refusing to alter multiple UdeCx controllers. Inspect PnP state manually.'
}
if ($devices.Count -eq 0) {
    Write-Host 'No UdeCx research root node found. Legacy AvCamBus is untouched.'
    return
}
foreach ($device in $devices) {
    Write-Host "Target: $($device.InstanceId), $($device.FriendlyName)"
    if (-not $Execute) {
        Write-Host 'DRY RUN: pass -Execute to remove this exact root device.'
        continue
    }
    $pnp = Join-Path $env:WINDIR 'System32\\pnputil.exe'
    & $pnp /remove-device $device.InstanceId
    if ($LASTEXITCODE -ne 0) {
        throw "PnP remove failed with code $LASTEXITCODE; use Windows Recovery if necessary."
    }
}
Write-Host 'Rollback never deletes drivers from the driver store or modifies boot policy.'
Write-Host 'After successful removal, independently identify the new AvCamUde oemNN.inf in pnputil /enum-drivers before deleting it.'
