$ErrorActionPreference = 'Stop'

Write-Host '=== AvCam verification ==='

$device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object {
        $_.InstanceId -eq 'ROOT\AVCAMVIRTUALCAMERA\0000' -or
        $_.InstanceId -like 'ROOT\AVCAMVIRTUALCAMERA*' -or
        $_.FriendlyName -eq 'AvCam Virtual Camera'
    } |
    Select-Object -First 1

if (-not $device) {
    Write-Error 'AvCam Virtual Camera was not found in the present PnP device tree.'
    exit 2
}

Write-Host ''
Write-Host 'Device:'
$device | Format-List Status, Class, FriendlyName, InstanceId, Problem, ConfigManagerErrorCode

Write-Host ''
Write-Host 'Hardware IDs:'
Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_HardwareIds' -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty Data

Write-Host ''
Write-Host 'Service:'
Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue |
    Select-Object KeyName, Data | Format-Table -AutoSize

if ($device.Status -ne 'OK') {
    Write-Error "Device is present but status is '$($device.Status)'."
    exit 3
}

if ($device.Class -ne 'Camera') {
    Write-Warning "Device is healthy but class is '$($device.Class)', expected 'Camera'."
}

Write-Host ''
Write-Host 'Camera-class devices currently present:'
Get-PnpDevice -Class Camera -PresentOnly -ErrorAction SilentlyContinue |
    Sort-Object FriendlyName |
    Format-Table Status, FriendlyName, InstanceId -AutoSize

Write-Host ''
Write-Host 'PnP verification passed.'
