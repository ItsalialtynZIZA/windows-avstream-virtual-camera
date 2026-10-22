# Read-only feasibility snapshot for the strict AvCam physical-Logitech route.
# Does not start any camera, modify PnP/registry, or interact with FrameServer.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') {
    throw 'Run on the Windows computer containing Logitech C270.'
}

Write-Output 'AVCAM_NATIVE_PHYSICAL_ENDPOINT_READ_ONLY_BEGIN'
Write-Output 'AVCAM_NATIVE_DIAGNOSTIC_NO_CAPTURE=1'

$matcher = 'VID_046D&PID_0825'
$found = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
    Where-Object { [string]$_.InstanceId -match $matcher })

if ($found.Count -eq 0) {
    Write-Output 'AVCAM_LOGITECH_C270_PRESENT=0'
} else {
    Write-Output 'AVCAM_LOGITECH_C270_PRESENT=1'
    foreach ($device in $found) {
        Write-Output ('AVCAM_C270_INSTANCE_ID=' + $device.InstanceId)
        Write-Output ('AVCAM_C270_CLASS=' + $device.Class)
        Write-Output ('AVCAM_C270_FRIENDLY_NAME=' + $device.FriendlyName)
        Write-Output ('AVCAM_C270_STATUS=' + $device.Status)
        foreach ($key in @(
            'DEVPKEY_Device_Parent',
            'DEVPKEY_Device_ContainerId',
            'DEVPKEY_Device_EnumeratorName',
            'DEVPKEY_Device_Service'
        )) {
            try {
                $p = Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName $key -ErrorAction Stop
                if ($null -ne $p.Data) {
                    Write-Output ('AVCAM_C270_' +
                        $key.Replace('DEVPKEY_Device_', '').ToUpperInvariant() +
                        '=' + [string]$p.Data)
                }
            } catch {
                # Some property keys are absent depending on the USB stack.
                Write-Output ('AVCAM_C270_' +
                    $key.Replace('DEVPKEY_Device_', '').ToUpperInvariant() +
                    '=UNAVAILABLE')
            }
        }
    }
}

# A camera may be opened by a broker service rather than each client process.
# This merely reports service state; it is NOT a per-app consumer lookup.
try {
    $frameServer = Get-Service -Name FrameServer -ErrorAction Stop
    Write-Output ('AVCAM_FRAMESERVER_STATE=' + [string]$frameServer.Status)
} catch {
    Write-Output 'AVCAM_FRAMESERVER_STATE=UNAVAILABLE'
}

$avcamClsid =
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{8AF84A8D-96ED-4F61-B1C3-5EDC62F2D591}'
Write-Output ('AVCAM_UNSTABLE_MF_COM_REGISTERED=' +
    [int](Test-Path -LiteralPath $avcamClsid))

# Original per-consumer frame separation cannot be inferred from physical
# PnP identity and broker state alone. Never report it as validated.
Write-Output 'AVCAM_STRICT_PER_CLIENT_PHYSICAL_ROUTE=NOT_PROVEN'
Write-Output 'AVCAM_NATIVE_PHYSICAL_ENDPOINT_READ_ONLY_PASS'
