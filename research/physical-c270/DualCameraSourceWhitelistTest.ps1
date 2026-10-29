# Pure synthetic PnP regression. Does not query or alter the host's devices.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$installer = Join-Path $PSScriptRoot 'AvCamDualCameraMft.ps1'
$code = Get-Content -LiteralPath $installer -Raw -ErrorAction Stop
$from = $code.IndexOf('# Strict whitelist of validated independent built-in source camera models.')
$to = $code.IndexOf("if (`$key.GetValue('CameraPostProcessingPluginCLSID'", $from)
if ($from -lt 0 -or $to -le $from) { throw 'Cannot locate independent source gate in installer' }
$gate = [scriptblock]::Create($code.Substring($from, $to - $from))
$script:devices = @()
function Get-PnpDevice {
    param([switch]$PresentOnly)
    if (-not $PresentOnly) { throw 'Only present device enumeration allowed' }
    return $script:devices
}
function New-Camera($id, $status='OK', $class='Camera') {
    [pscustomobject]@{ InstanceId=$id; FriendlyName='Synthetic Camera'; Class=$class; Status=$status }
}
function Check($name, $items, $expected) {
    $script:devices = @($items)
    $passed = $true
    try { & $gate *> $null } catch { $passed = $false }
    if ($passed -ne $expected) { throw "$name expected=$expected actual=$passed" }
    Write-Host "AVCAM_SOURCE_GATE_$name=PASS"
}
$logitech = New-Camera 'USB\VID_046D&PID_0825&MI_00\A'
$integrated13D3 = New-Camera 'USB\VID_13D3&PID_56FF&MI_00\B'
$integrated2B7E = New-Camera 'USB\VID_2B7E&PID_B685&MI_00\C'
$virtual = New-Camera 'ROOT\AVCAMVIRTUALCAMERA\0000'
Check '13D3_INTEGRATED_ACCEPTED' @($logitech,$integrated13D3,$virtual) $true
Check '2B7E_INTEGRATED_ACCEPTED' @($logitech,$integrated2B7E) $true
Check 'LOGITECH_ALONE_REJECTED' @($logitech) $false
Check 'VIRTUAL_ALONE_REJECTED' @($logitech,$virtual) $false
Check 'DUPLICATE_SOURCE_REJECTED' @($logitech,$integrated2B7E,$integrated13D3) $false
Check 'OFFLINE_SOURCE_REJECTED' @($logitech,(New-Camera $integrated13D3.InstanceId 'Error')) $false
Check 'UNSUPPORTED_SOURCE_REJECTED' @($logitech,(New-Camera 'USB\VID_9999&PID_9999&MI_00\X')) $false
Check 'WRONG_INTERFACE_REJECTED' @($logitech,(New-Camera 'USB\VID_13D3&PID_56FF&MI_02\B')) $false
Write-Host 'AVCAM_SOURCE_IDENTITY_WHITELIST_ALL_PASS'
