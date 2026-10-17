$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'driver\AvCamBus.c'
$infPath = Join-Path $root 'driver\AvCamBus.inf'
$ioctlPath = Join-Path $root 'shared\AvCamManualIoctl.h'

$source = Get-Content $sourcePath -Raw
$inf = Get-Content $infPath -Raw
$ioctl = Get-Content $ioctlPath -Raw

function Assert-Contains {
    param([string]$Text,[string]$Pattern,[string]$Message)
    if ($Text -notmatch $Pattern) { throw $Message }
}

function Assert-NotContains {
    param([string]$Text,[string]$Pattern,[string]$Message)
    if ($Text -match $Pattern) { throw $Message }
}

Assert-Contains $inf 'Root\\AvCamVirtualUsbHost' 'INF must install the root bus on Root\AvCamVirtualUsbHost.'
Assert-Contains $inf 'DefaultDestDir\s*=\s*13' 'INF must use Driver Store isolation (DIRID 13).'
Assert-Contains $inf 'ServiceBinary\s*=\s*%13%\\AvCamBus\.sys' 'ServiceBinary must point to DIRID 13.'
Assert-Contains $inf '\[SourceDisksFiles\][\s\S]*AvCamBus\.sys\s*=\s*1' 'INF must list AvCamBus.sys in SourceDisksFiles.'
Assert-Contains $inf 'NTamd64\.10\.0\.\.\.16299' 'DIRID 13 models must be restricted to Windows 10 build 16299 or newer.'
Assert-NotContains $inf '(?m)^\[Standard\.NTamd64\]\s*$' 'Undecorated x64 model section must not bypass the TargetOSVersion restriction.'

Assert-Contains $source 'USB\\\\VID_1234&PID_5678&MI_00' 'UVC function PDO hardware ID is missing.'
Assert-Contains $source 'USB\\\\VID_1234&PID_5678' 'Base VID/PID hardware ID is missing.'
Assert-Contains $source 'USB\\\\Class_0E&SubClass_01&Prot_00' 'UVC VideoControl compatible ID is missing.'
Assert-Contains $source 'USB\\\\Class_0E&SubClass_01' 'UVC subclass-compatible ID is missing.'
Assert-Contains $source 'USB\\\\Class_0E' 'UVC class-compatible ID is missing.'
Assert-NotContains $source 'Class_0[eE]&SubClass_03' 'UVC function collection must not use the obsolete/incorrect subclass 03 identity.'
Assert-Contains $source 'IOCTL_INTERNAL_USB_SUBMIT_URB' 'Child queue must classify USB submit-URB requests.'
Assert-Contains $source 'URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE' 'Descriptor URB handler is missing.'
Assert-Contains $source 'USB_CONFIGURATION_DESCRIPTOR_TYPE' 'Configuration descriptor handling is missing.'
Assert-Contains $source 'URB_FUNCTION_SELECT_CONFIGURATION' 'Select-configuration URB handling is missing.'
Assert-Contains $source 'URB_FUNCTION_SELECT_INTERFACE' 'Select-interface URB handling is missing.'
Assert-Contains $source 'URB_FUNCTION_GET_CONFIGURATION' 'Get-configuration URB handling is missing.'
Assert-Contains $source 'URB_FUNCTION_GET_INTERFACE' 'Get-interface URB handling is missing.'
Assert-Contains $source 'UsbdPipeTypeIsochronous' 'Streaming interface must expose an isochronous pipe.'
Assert-Contains $source 'EndpointAddress\s*=\s*0x81' 'Streaming pipe endpoint must be 0x81 IN.'
Assert-Contains $source 'MaximumPacketSize\s*=\s*3072' 'High-bandwidth streaming pipe size is missing.'
Assert-Contains $source 'WdfIoQueueDispatchSequential' 'Control URB queue should serialize configuration state transitions.'
Assert-Contains $source 'WdfChildListAddOrUpdateChildDescriptionAsPresent' 'Plug path must mark the child present.'
Assert-Contains $source 'WdfChildListUpdateChildDescriptionAsMissing' 'Unplug path must mark the child missing.'

Assert-Contains $ioctl 'IOCTL_AVCAM_PLUG_DEVICE' 'Plug IOCTL is missing.'
Assert-Contains $ioctl 'IOCTL_AVCAM_UNPLUG_DEVICE' 'Unplug IOCTL is missing.'
Assert-Contains $ioctl 'IOCTL_AVCAM_WRITE_FRAME' 'Frame IOCTL is missing.'
Assert-Contains $ioctl 'IOCTL_AVCAM_READ_DIAGNOSTICS' 'Diagnostic trace IOCTL is missing.'
Assert-Contains $source 'WdfDeviceInitAssignWdmIrpPreprocessCallback' 'PnP query-interface instrumentation is missing.'
Assert-Contains $source 'AVCAM_DIAG_EVENT_URB' 'URB status instrumentation is missing.'
Assert-Contains $source 'AVCAM_DIAG_EVENT_INTERNAL_IOCTL' 'Internal IOCTL instrumentation is missing.'
Assert-Contains $source 'AVCAM_DIAG_EVENT_QUERY_INTERFACE_DETAILS' 'PnP query version and size instrumentation is missing.'
Assert-Contains $source 'AvCamAddUsbdiV1QueryInterface' 'USBDI V1 interface registration missing.'
Assert-Contains $source 'WdfDeviceAddQueryInterface' 'KMDF USB bus interface registration missing.'
Assert-Contains $source 'AvCamUnsupportedIsoOut' 'Unsupported ISO OUT operations must be rejected.'
Assert-Contains $source 'AvCamUnsupportedBusInformation' 'Unsupported bus information operations must be rejected.'
Assert-NotContains $ioctl 'METHOD_NEITHER' 'Research IOCTLs must not use METHOD_NEITHER.'

$functionNumbers = [regex]::Matches($ioctl, 'CTL_CODE\([^,]+,\s*(0x[0-9A-Fa-f]+)') | ForEach-Object { $_.Groups[1].Value }
if (($functionNumbers | Select-Object -Unique).Count -ne $functionNumbers.Count) {
    throw 'IOCTL function numbers must be unique.'
}

$critical = $source + "`n" + $inf + "`n" + $ioctl
Assert-NotContains $critical '(?im)\bTODO\b|\bFIXME\b' 'Critical manual-vHCI sources must not contain TODO/FIXME markers.'

Write-Host 'Static contract tests passed.'
