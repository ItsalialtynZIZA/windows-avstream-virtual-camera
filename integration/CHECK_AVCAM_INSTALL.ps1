# Read-only preflight for the AvCam AVStream integration.
# Does not install drivers, certificates, enable test mode, or access USB devices.
$ErrorActionPreference = 'Stop'
$expectedGuid = '{7B28304F-6B89-4DF0-9E54-B048F4A16213}'
$report = Join-Path $PSScriptRoot 'AvCam-Preflight-Report.txt'
Start-Transcript -Path $report -Force | Out-Null
try {
    Write-Host '=== AvCam integrated preflight (read-only) ==='
    Write-Host ('Windows: ' + [Environment]::OSVersion.VersionString)
    Write-Host ('Expected private sink interface: ' + $expectedGuid)
    Write-Host ''
    $devices = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId -like 'ROOT\AVCAMVIRTUALCAMERA*' -or
            $_.FriendlyName -eq 'AvCam Virtual Camera'
        })
    if ($devices.Count -eq 0) {
        Write-Host 'AVCAM_DEVICE: NOT_INSTALLED'
    } else {
        foreach ($dev in $devices) {
            Write-Host ('AVCAM_DEVICE: ' + $dev.Status + ' | ' + $dev.InstanceId)
        }
    }
    $iface = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.ClassGuid -eq $expectedGuid })
    if ($iface.Count -gt 0) {
        Write-Host ('FRAME_SINK_CLASS_DEVICES: ' + $iface.Count)
    } else {
        Write-Host 'FRAME_SINK_CLASS_DEVICES: none (note: device interfaces are not PnP device classes)'
    }
    $feeder = Join-Path $PSScriptRoot 'feeder\AvCamFeeder.exe'
    Write-Host ('FEEDER_PRESENT: ' + (Test-Path $feeder))
    if (Test-Path $feeder) {
        Write-Host ('FEEDER_SHA256: ' + (Get-FileHash -Algorithm SHA256 $feeder).Hash)
    }
    Write-Host 'No system configuration was changed.'
    Write-Host ('Report: ' + $report)
} finally { Stop-Transcript | Out-Null }
