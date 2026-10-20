# Read-only AvCam / physical Logitech C270 capture-pipeline diagnostic.
# Does not change PnP, registry, driver, DLL or capture settings.
# Run in an elevated 64-bit Windows PowerShell with Windows Camera
# visibly previewing the physical Logitech C270.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$expected = '{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
Write-Host '=== AVCAM PHYSICAL C270 DIAGNOSTIC (READ-ONLY) ==='
Write-Host ('Process64Bit: {0}; PowerShell: {1}' -f [Environment]::Is64BitProcess, $PSVersionTable.PSVersion)
try {
    $os = Get-CimInstance Win32_OperatingSystem
    Write-Host ('WindowsBuild: {0}' -f $os.BuildNumber)
} catch { Write-Warning ('Windows build lookup: ' + $_.Exception.Message) }
Write-Host '--- PHYSICAL PNP ---'
try {
    Get-PnpDevice -PresentOnly | Where-Object {
        $_.InstanceId -match '(?i)^USB\\VID_046D&PID_0825'
    } | ForEach-Object {
        Write-Host ('{0} | {1} | {2}' -f $_.Status, $_.InstanceId, $_.FriendlyName)
    }
} catch { Write-Warning ('Physical PnP lookup: ' + $_.Exception.Message) }
Write-Host '--- AV-CAMERA REGISTRATION ---'
$state = Join-Path $env:ProgramFiles 'AvCamPhysicalC270\install-state.json'
if (Test-Path -LiteralPath $state -PathType Leaf) {
    $saved = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
    Write-Host ('InstallState: {0}' -f $state)
    if ($saved.RegistryPath -notmatch '(?i)^HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\' -or
        $saved.RegistryPath -notmatch '(?i)VID_046D&PID_0825') {
        Write-Warning 'Install-state registry path is not the physical C270 interface'
    } else {
        $binding = Get-Item -LiteralPath ('Registry::' + $saved.RegistryPath) -ErrorAction Stop
        Write-Host ('Binding: {0}' -f $binding.Name)
        foreach ($name in @('CameraDeviceMftClsid','CameraDeviceMftCLSIDChain','CLSID','FriendlyName')) {
            $raw = $binding.GetValue($name, $null)
            $value = if ($null -eq $raw) { '<absent>' } elseif ($raw -is [array]) {
                $raw -join ';'
            } else { [string]$raw }
            Write-Host ('{0}: {1}' -f $name, $value)
        }
    }
} else { Write-Warning 'No AvCamPhysicalC270 install-state.json' }
$comKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\' + $expected + '\InprocServer32'
$inproc = Get-Item -LiteralPath $comKey -ErrorAction SilentlyContinue
if ($null -eq $inproc) {
    Write-Warning ('COM InprocServer32 key missing: ' + $comKey)
} else {
    $filePath = [string]$inproc.GetValue('', '')
    Write-Host ('COM InprocServer32: {0}' -f $filePath)
    Write-Host ('DLL exists: {0}' -f (Test-Path -LiteralPath $filePath -PathType Leaf))
}
Write-Host '--- WINDOWS FRAME SERVER ---'
foreach ($name in @('FrameServer','FrameServerMonitor')) {
    $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
    if ($null -eq $svc) { Write-Host ('{0}: missing' -f $name) }
    else { Write-Host ('{0}: {1}; start={2}' -f $name, $svc.Status, $svc.StartType) }
}
foreach ($key in @(
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows Media Foundation\Platform',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows Media Foundation\Platform'
)) {
    $item = Get-Item -LiteralPath $key -ErrorAction SilentlyContinue
    $value = if ($null -eq $item) { '<absent>' } else {
        $v = $item.GetValue('EnableFrameServerMode', $null)
        if ($null -eq $v) { '<absent>' } else { $v }
    }
    Write-Host ('{0}\EnableFrameServerMode = {1}' -f $key, $value)
}
Write-Host '--- CAMERA APPLICATIONS ---'
Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -match '(?i)camera|frameserver' } |
    ForEach-Object { Write-Host ('{0} PID={1}' -f $_.ProcessName, $_.Id) }
Write-Host '--- DLL CURRENTLY LOADED ---'
& "$env:WINDIR\System32\tasklist.exe" /m AvCamPhysicalDeviceMft.dll
Write-Host '--- COM ACTIVATION IN THIS DIAGNOSTIC PROCESS ---'
# Class activation only: verifies Windows can load the compiled COM DLL.
# Does not initialize any camera, start streaming, or change registry.
$native = @'
using System;
using System.Runtime.InteropServices;
public static class AvCamComActivationProbe {
  [DllImport("ole32.dll", ExactSpelling=true, PreserveSig=true)]
  public static extern int CoCreateInstance(
    ref Guid classId, IntPtr outer, uint context,
    ref Guid interfaceId, out IntPtr result);
}
'@
try {
    Add-Type -TypeDefinition $native -ErrorAction Stop
    $clsid = [Guid]::Parse($expected)
    $iid = [Guid]::Parse('00000000-0000-0000-C000-000000000046')
    $ptr = [IntPtr]::Zero
    $hr = [AvCamComActivationProbe]::CoCreateInstance(
        [ref]$clsid, [IntPtr]::Zero, 1, [ref]$iid, [ref]$ptr)
    Write-Host ('CoCreateInstance(CLSID_AvCam,IUnknown) HRESULT=0x{0:X8}' -f [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$hr), 0))
    if ($ptr -ne [IntPtr]::Zero) { [void][Runtime.InteropServices.Marshal]::Release($ptr) }
} catch { Write-Warning ('COM probe failed: ' + $_.Exception.Message) }
Write-Host 'AVCAM_PHYSICAL_DIAGNOSTIC_FINISHED_NO_SYSTEM_CHANGES'
