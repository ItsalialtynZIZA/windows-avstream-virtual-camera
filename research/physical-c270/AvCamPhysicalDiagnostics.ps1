# AvCam Physical C270 diagnostics. READ ONLY. No device or registry writes.
# Run from an elevated Windows PowerShell while Logitech is open in Camera.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$clsid = '{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
$cameraClass = '{E5323777-F976-4F5B-9B55-B94699C46E44}'
$rootPath = "SYSTEM\CurrentControlSet\Control\DeviceClasses\$cameraClass"

function Print([string]$name,[object]$value) {
    if ($null -eq $value -or "$value" -eq '') { $value = '<empty>' }
    Write-Host ('{0}={1}' -f $name,$value)
}
function Get-DeviceRegistry {
    $present = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
        Where-Object {
            $_.InstanceId -match '(?i)^USB\\VID_046D&PID_0825' -and
            $_.Class -match '(?i)Camera|Image'
        })
    Print 'C270_PRESENT_COUNT' $present.Count
    if ($present.Count -ne 1) { throw 'Expected exactly one present physical Logitech C270 camera' }
    Print 'C270_PNP_INSTANCE' $present[0].InstanceId
    Print 'C270_PNP_STATUS' $present[0].Status
    $root = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($rootPath,$false)
    if ($null -eq $root) { throw 'KSCATEGORY_VIDEO_CAMERA registry root not found' }
    try {
        $encoded = $present[0].InstanceId.Replace('\','#')
        $matches = @($root.GetSubKeyNames() | Where-Object {
            $_.IndexOf($encoded,[StringComparison]::OrdinalIgnoreCase) -ge 0 -and
            $_.EndsWith(('#' + $cameraClass),[StringComparison]::OrdinalIgnoreCase)
        })
        Print 'C270_MATCHED_INTERFACE_COUNT' $matches.Count
        if ($matches.Count -ne 1) { throw 'Cannot uniquely identify active physical Logitech interface' }
        $params = $root.OpenSubKey(($matches[0] + '\#GLOBAL\Device Parameters'),$false)
        if ($null -eq $params) { throw 'Logitech Device Parameters registry key inaccessible' }
        try {
            Print 'C270_INTERFACE' $params.Name
            foreach ($valueName in @('CameraDeviceMftClsid','CameraDeviceMftClsidChain',
                 'CameraPostProcessingPluginCLSID','CLSID','FriendlyName','RTCFlags')) {
                $value = $params.GetValue($valueName,$null)
                $kind = if ($params.GetValueNames() -contains $valueName) {
                    $params.GetValueKind($valueName)
                } else { 'Absent' }
                Print ('C270_{0}' -f $valueName) $value
                Print ('C270_{0}_TYPE' -f $valueName) $kind
            }
            Print 'AVCAM_DEVICE_MFT_REGISTRATION_MATCH' ($params.GetValue('CameraDeviceMftClsid',$null) -ieq $clsid)
        } finally { $params.Dispose() }
    } finally { $root.Dispose() }
}

Write-Host '=== AvCam Physical C270 read-only diagnostics ==='
Print 'WINDOWS_VERSION' ([Environment]::OSVersion.VersionString)
Print 'POWERSHELL_VERSION' $PSVersionTable.PSVersion.ToString()
Print 'IS_64BIT_PROCESS' ([Environment]::Is64BitProcess)
$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
Print 'IS_ADMIN' ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
Get-DeviceRegistry

# Validate that the 64-bit COM registration points to an actual DLL.
$comPath = "SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
$com = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($comPath,$false)
if ($null -eq $com) {
    Print 'COM_REGISTERED_X64' $false
} else {
    try {
        $server = $com.GetValue('',$null)
        Print 'COM_REGISTERED_X64' $true
        Print 'COM_DLL_PATH' $server
        Print 'COM_DLL_EXISTS' ([bool]($server -and (Test-Path -LiteralPath $server -PathType Leaf)))
        Print 'COM_THREADING_MODEL' $com.GetValue('ThreadingModel',$null)
    } finally { $com.Dispose() }
}

# Frame Server may be disabled by a legacy override. Read only: do not
# start/stop services or edit a system-wide camera setting during diagnostics.
foreach ($serviceName in @('FrameServer','FrameServerMonitor')) {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -eq $service) {
        Print ('SERVICE_{0}' -f $serviceName) 'NotFound'
    } else {
        Print ('SERVICE_{0}' -f $serviceName) $service.Status
        Print ('SERVICE_{0}_START_TYPE' -f $serviceName) $service.StartType
    }
}
foreach ($platformPath in @(
    'SOFTWARE\Microsoft\Windows Media Foundation\Platform',
    'SOFTWARE\WOW6432Node\Microsoft\Windows Media Foundation\Platform'
)) {
    $platform = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($platformPath,$false)
    $flag = if ($null -eq $platform) {
        '<absent>'
    } else {
        try {
            if ($platform.GetValueNames() -contains 'EnableFrameServerMode') {
                [string]$platform.GetValue('EnableFrameServerMode')
            } else { '<absent>' }
        } finally { $platform.Dispose() }
    }
    Print ('FRAMESERVER_OVERRIDE_{0}' -f $platformPath) $flag
}

# Best effort: Windows camera service processes may block module enumeration.
# No processes are injected, restarted, paused, or modified.
$hits = [System.Collections.Generic.List[string]]::new()
$inspectable = 0
$blocked = 0
foreach ($p in @(Get-Process -ErrorAction SilentlyContinue)) {
    try {
        $modules = $p.Modules
        $inspectable++
        foreach ($module in $modules) {
            if ($module.FileName -like '*AvCamPhysicalDeviceMft.dll') {
                $hits.Add(('{0} (PID {1})' -f $p.ProcessName,$p.Id))
                break
            }
        }
    } catch {
        $blocked++
    }
}
Print 'PROCESS_MODULE_SCAN_INSPECTABLE' $inspectable
Print 'PROCESS_MODULE_SCAN_BLOCKED' $blocked
Print 'PROCESS_WITH_AVCAM_MFT_LOADED_COUNT' $hits.Count
foreach ($hit in $hits) { Print 'AVCAM_MFT_LOADED_IN' $hit }
if ($hits.Count -eq 0) {
    Write-Host 'NOTE: No visible module is inconclusive when module inspection is blocked.'
}
Write-Host 'DIAGNOSTICS_ONLY_NO_CAMERA_CHANGES'
