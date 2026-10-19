# Read-only UdeCx USB device enumeration report for an isolated Win11 VM.
# It never installs, removes, resets, enables, or disables any device.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$system = Get-CimInstance -ClassName Win32_ComputerSystem
$identity = "$($system.Manufacturer) $($system.Model)"
if ($identity -notmatch '(?i)virtual machine|vmware|virtualbox|qemu|kvm|bochs|xen|parallels|hyper-v') {
    throw "VM_REPORT_REFUSED: unrecognized VM hardware ($identity)"
}
Write-Host "VM identity: $identity"
Write-Host "Expected controller: ROOT\AvCamUdeController"
Write-Host "Expected child USB hardware: VID_1234&PID_5679"

$devices = @(Get-PnpDevice -ErrorAction Stop | Where-Object {
    $_.InstanceId -match '(?i)^(ROOT\\AVCAMUDECONTROLLER|USB\\VID_1234&PID_5679)([&\\]|$)'
})
if ($devices.Count -eq 0) {
    Write-Host 'No UdeCx controller or experimental USB child found.'
    Write-Host 'VM_UDECX_NOT_ENUMERATED'
    exit 20
}

foreach ($device in $devices) {
    Write-Host ""
    Write-Host "=== $($device.InstanceId) ==="
    $device | Format-List FriendlyName, Class, Status, Problem, InstanceId
    foreach ($key in @(
        'DEVPKEY_Device_Service',
        'DEVPKEY_Device_DriverInfPath',
        'DEVPKEY_Device_ProblemCode',
        'DEVPKEY_Device_Parent'
    )) {
        try {
            $property = Get-PnpDeviceProperty -InstanceId $device.InstanceId `
                -KeyName $key -ErrorAction Stop
            Write-Host "$key = $($property.Data)"
        } catch {
            Write-Host "$key = <not reported>"
        }
    }
}

$controller = @($devices | Where-Object {
    $_.InstanceId -match '(?i)^ROOT\\AVCAMUDECONTROLLER(\\|$)' -and
    $_.Status -eq 'OK'
})
$video = @($devices | Where-Object {
    $_.InstanceId -match '(?i)^USB\\VID_1234&PID_5679([&\\]|$)' -and
    $_.Status -eq 'OK' -and
    (Get-PnpDeviceProperty -InstanceId $_.InstanceId `
        -KeyName DEVPKEY_Device_Service -ErrorAction SilentlyContinue).Data -eq 'usbvideo'
})
if ($controller.Count -eq 1) {
    Write-Host 'VM_UDECX_CONTROLLER_PRESENT'
} else {
    Write-Host "VM_UDECX_CONTROLLER_NOT_READY count=$($controller.Count)"
}
if ($video.Count -eq 1) {
    Write-Host 'VM_UDECX_USBVIDEO_PRESENT'
} else {
    Write-Host "VM_UDECX_USBVIDEO_NOT_READY count=$($video.Count)"
}
if ($controller.Count -eq 1 -and $video.Count -eq 1) {
    Write-Host 'VM_UDECX_ENUMERATION_ONLY_PASSED'
    Write-Host 'No MJPEG, USB ISO, Media Foundation video samples, or browser frames were verified.'
    exit 0
}
exit 21
