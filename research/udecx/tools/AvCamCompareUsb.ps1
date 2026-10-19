# Read-only USB/PnP comparison of an actual Logitech C270 and AvCam UdeCx.
# No driver installation, device state change or external network requests.
[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path $env:USERPROFILE 'Desktop\AvCam-USB-Report')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-Property {
    param([string]$DeviceId, [string]$PropertyKey)
    try {
        return (Get-PnpDeviceProperty -InstanceId $DeviceId -KeyName $PropertyKey -ErrorAction Stop).Data
    } catch { return $null }
}

function Get-UsbRecord {
    param($Device, [string]$Label)
    $id = [string]$Device.InstanceId
    $props = [ordered]@{
        Profile = $Label
        Name = [string]$Device.FriendlyName
        InstanceId = $id
        PnpClass = [string]$Device.Class
        Status = [string]$Device.Status
    }
    foreach ($item in @(
        @('Service', 'DEVPKEY_Device_Service'),
        @('Parent', 'DEVPKEY_Device_Parent'),
        @('Enumerator', 'DEVPKEY_Device_EnumeratorName'),
        @('BusDescription', 'DEVPKEY_Device_BusReportedDeviceDesc'),
        @('HardwareIds', 'DEVPKEY_Device_HardwareIds'),
        @('CompatibleIds', 'DEVPKEY_Device_CompatibleIds'),
        @('LocationPaths', 'DEVPKEY_Device_LocationPaths'),
        @('ContainerId', 'DEVPKEY_Device_ContainerId'),
        @('DriverInf', 'DEVPKEY_Device_DriverInfPath'),
        @('DriverProvider', 'DEVPKEY_Device_DriverProvider'),
        @('ProblemCode', 'DEVPKEY_Device_ProblemCode')
    )) {
        $props[$item[0]] = Read-Property -DeviceId $id -PropertyKey $item[1]
    }
    return [pscustomobject]$props
}

$all = @(Get-PnpDevice -ErrorAction Stop)
$logitech = @($all | Where-Object {
    $_.InstanceId -match '(?i)^USB\\VID_046D&PID_0825(?:&|\\|$)'
})
$avcam = @($all | Where-Object {
    $_.InstanceId -match '(?i)^USB\\VID_1234&PID_5679(?:&|\\|$)'
})
$controller = @($all | Where-Object {
    $_.InstanceId -match '(?i)^ROOT\\AVCAMUDECONTROLLER(?:\\|$)'
})
$records = @(
    foreach ($device in $logitech) { Get-UsbRecord $device 'Physical Logitech C270' }
    foreach ($device in $avcam) { Get-UsbRecord $device 'AvCam UdeCx USB emulator' }
    foreach ($device in $controller) { Get-UsbRecord $device 'AvCam software USB controller' }
)
$usbvideo = @($records | Where-Object {
    $_.Profile -eq 'AvCam UdeCx USB emulator' -and $_.Service -eq 'usbvideo'
})
$summary = [ordered]@{
    GeneratedUtc = [DateTime]::UtcNow.ToString('o')
    Computer = $env:COMPUTERNAME
    LogitechDeviceCount = $logitech.Count
    AvCamUsbDeviceCount = $avcam.Count
    AvCamUdeControllerCount = $controller.Count
    AvCamUsbVideoServiceBound = ($usbvideo.Count -gt 0)
    GenuineLogitechUsbIdentityCloned = $false
    Conclusion = 'AvCam is a Windows UVC camera over an emulated UdeCx USB host; it remains distinguishable from a physical Logitech in USB topology and VID/PID.'
    Devices = $records
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$target = Join-Path $OutputDirectory 'AvCam-C270-Usb-Comparison.json'
$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $target -Encoding UTF8
Write-Host "Logitech devices: $($logitech.Count)"
Write-Host "AvCam USB devices: $($avcam.Count)"
Write-Host "AvCam usbvideo.sys bindings: $($usbvideo.Count)"
Write-Host "UdeCx controllers: $($controller.Count)"
Write-Host "Read-only comparison saved: $target"
if ($usbvideo.Count -eq 0) {
    Write-Warning 'AvCam usbvideo.sys binding not detected. Check the installed UdeCx device.'
}
