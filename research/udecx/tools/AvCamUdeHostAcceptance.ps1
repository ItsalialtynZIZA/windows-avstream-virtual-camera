# Read-only Windows 11 UdeCx UVC acceptance report.
# Does not install drivers, alter boot settings, stop services or probe the C270.
# Run elevated if access to all PnP properties is required.
[CmdletBinding()]
param([string]$OutputPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-DevicePropertySafe {
    param([string]$InstanceId, [string]$KeyName)
    try {
        $p = Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName -ErrorAction Stop
        return $p.Data
    } catch { return $null }
}

$os = Get-CimInstance Win32_OperatingSystem
$all = @(Get-PnpDevice -ErrorAction Stop)
# Root UdeCx controller, experimental USB child, and the Windows USB Video consumer only.
$target = @($all | Where-Object {
    $_.InstanceId -match '(?i)^ROOT\\AVCAMUDECONTROLLER(\\|$)' -or
    $_.InstanceId -match '(?i)^USB\\VID_1234&PID_5679(&|\\|$)' -or
    ($_.Service -eq 'usbvideo' -and $_.InstanceId -match '(?i)VID_1234&PID_5679')
})
$devices = @(
    foreach ($d in $target) {
        [pscustomobject]@{
            Name         = $d.FriendlyName
            InstanceId   = $d.InstanceId
            Class        = $d.Class
            Status       = [string]$d.Status
            ProblemCode  = Get-DevicePropertySafe -InstanceId $d.InstanceId -KeyName 'DEVPKEY_Device_ProblemCode'
            Service      = Get-DevicePropertySafe -InstanceId $d.InstanceId -KeyName 'DEVPKEY_Device_Service'
            DriverInf    = Get-DevicePropertySafe -InstanceId $d.InstanceId -KeyName 'DEVPKEY_Device_DriverInfPath'
            Parent       = Get-DevicePropertySafe -InstanceId $d.InstanceId -KeyName 'DEVPKEY_Device_Parent'
        }
    }
)
$usbVideo = @($devices | Where-Object { $_.Service -eq 'usbvideo' })
$child = @($devices | Where-Object { $_.InstanceId -match '(?i)^USB\\VID_1234&PID_5679' })
$root = @($devices | Where-Object { $_.InstanceId -match '(?i)^ROOT\\AVCAMUDECONTROLLER' })
$report = [ordered]@{
    TimeUtc = [DateTime]::UtcNow.ToString('o')
    OS = $os.Caption
    OSVersion = $os.Version
    RootControllerCount = $root.Count
    ExperimentalUsbChildCount = $child.Count
    UsbVideoBoundCount = $usbVideo.Count
    Devices = $devices
    Notes = @(
        'Presence and USBVIDEO binding do NOT prove successful MJPEG frames.',
        'Source Logitech VID_046D PID_0825 and legacy AvCamBus are intentionally excluded.',
        'Collect Windows Camera/consumer frame evidence separately after safe host installation.'
    )
}
$report | ConvertTo-Json -Depth 6 | Write-Output
if ($OutputPath) {
    $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resolved -Encoding UTF8
    Write-Host "Read-only report saved: $resolved"
}
if ($root.Count -eq 0) {
    Write-Warning 'AvCam UdeCx research controller is not installed. This is expected before the first host acceptance.'
} elseif ($child.Count -eq 0) {
    Write-Warning 'Controller detected but experimental USB child not detected; inspect driver startup and UdeCx plug-in.'
} elseif ($usbVideo.Count -eq 0) {
    Write-Warning 'Experimental USB child detected, but usbvideo.sys is not bound; inspect descriptors and PnP problem code.'
} else {
    Write-Host 'USBVIDEO_BINDING_DETECTED: still need actual frame reception and lifecycle acceptance.'
}
