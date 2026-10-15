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

Assert-Contains $source 'USB\\\\VID_1234&PID_5678' 'Child PDO hardware ID is missing.'
Assert-Contains $source 'USB\\\\Class_0e&SubClass_03&Prot_00' 'UVC-compatible ID is missing.'
Assert-Contains $source 'IOCTL_INTERNAL_USB_SUBMIT_URB' 'Child queue must classify USB submit-URB requests.'
Assert-Contains $source 'WdfChildListAddOrUpdateChildDescriptionAsPresent' 'Plug path must mark the child present.'
Assert-Contains $source 'WdfChildListUpdateChildDescriptionAsMissing' 'Unplug path must mark the child missing.'

Assert-Contains $ioctl 'IOCTL_AVCAM_PLUG_DEVICE' 'Plug IOCTL is missing.'
Assert-Contains $ioctl 'IOCTL_AVCAM_UNPLUG_DEVICE' 'Unplug IOCTL is missing.'
Assert-Contains $ioctl 'IOCTL_AVCAM_WRITE_FRAME' 'Frame IOCTL is missing.'
Assert-NotContains $ioctl 'METHOD_NEITHER' 'Research IOCTLs must not use METHOD_NEITHER.'

$functionNumbers = [regex]::Matches($ioctl, 'CTL_CODE\([^,]+,\s*(0x[0-9A-Fa-f]+)') | ForEach-Object { $_.Groups[1].Value }
if (($functionNumbers | Select-Object -Unique).Count -ne $functionNumbers.Count) {
    throw 'IOCTL function numbers must be unique.'
}

$critical = $source + "`n" + $inf + "`n" + $ioctl
Assert-NotContains $critical '(?im)\bTODO\b|\bFIXME\b' 'Critical manual-vHCI sources must not contain TODO/FIXME markers.'

Write-Host 'Static contract tests passed.'
