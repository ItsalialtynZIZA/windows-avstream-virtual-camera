# AvCam Physical C270 - reversible Device MFT experiment.
# Default: inspect only. -Enable registers the COM video transform on the
# physical Logitech camera interface. -Disable removes ONLY our registration.
[CmdletBinding()]
param([switch]$Enable,[switch]$Disable)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Enable -and $Disable) { throw 'Choose -Enable OR -Disable' }
# Multi-app camera works on the owner's Windows 11 25H2 system. The current
# Device MFT has no proven per-client isolation: it cannot safely route OBS
# only to Chrome while preserving Deep Live Cam's raw sensor stream.
# Keep preflight and rollback functional, but prohibit further opt-in installs.
if ($Enable) {
    throw 'AvCam physical C270 Device MFT installation is disabled until Chrome-only frame isolation is proven. Multi-app camera sharing must remain intact. -Disable remains available for rollback.'
}
$clsid = '{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
$cameraClass = '{E5323777-F976-4F5B-9B55-B94699C46E44}'
$deviceClassRoot = "Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\DeviceClasses\$cameraClass"
$folder = Join-Path $env:ProgramFiles 'AvCamPhysicalC270'
$installedDll = Join-Path $folder 'AvCamPhysicalDeviceMft.dll'
$backupState = Join-Path $folder 'install-state.json'

function Get-PhysicalLogitech {
    return @(Get-PnpDevice -PresentOnly | Where-Object {
        $_.InstanceId -match '(?i)^USB\\VID_046D&PID_0825' -and
        $_.Status -eq 'OK' -and $_.Class -match '(?i)Camera|Image'
    })
}
function Get-PhysicalInterface {
    param(
        [Parameter(Mandatory=$true)][string]$InstanceId,
        [string]$RegistryRoot = $deviceClassRoot
    )
    # Get-ChildItem on a DeviceClasses parent opens protected children
    # (e.g. Properties) even without -Recurse. Use the native RegistryKey
    # API: GetSubKeyNames() returns names WITHOUT opening any child.
    $match = [regex]::Match($RegistryRoot, '^(?i)Registry::(HKEY_LOCAL_MACHINE|HKEY_CURRENT_USER)\\(.+)$')
    if (-not $match.Success) {
        throw "Unsupported physical camera registry root: $RegistryRoot"
    }
    $hiveName = $match.Groups[1].Value.ToUpperInvariant()
    $subPath = $match.Groups[2].Value
    if ($hiveName -eq 'HKEY_LOCAL_MACHINE') {
        $hive = [Microsoft.Win32.Registry]::LocalMachine
    } else {
        $hive = [Microsoft.Win32.Registry]::CurrentUser
    }
    $root = $hive.OpenSubKey($subPath, $false)
    if ($null -eq $root) {
        throw "Camera interface registry root not found: $RegistryRoot"
    }
    try {
        $encodedInstance = $InstanceId.Replace('\', '#')
        $expectedSuffix = '#' + $cameraClass
        $names = @($root.GetSubKeyNames() | Where-Object {
            $_.IndexOf($encodedInstance,
                [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
            $_.EndsWith($expectedSuffix,
                [StringComparison]::OrdinalIgnoreCase)
        })
        $parameterKeys = @(
            foreach ($name in $names) {
                $relative = $name + '\#GLOBAL\Device Parameters'
                $direct = $root.OpenSubKey($relative, $false)
                if ($null -ne $direct) {
                    $direct.Dispose()
                    $parameterPath = "$RegistryRoot\$relative"
                    Get-Item -LiteralPath $parameterPath -ErrorAction Stop
                }
            }
        )
    } finally {
        $root.Dispose()
    }
    if ($parameterKeys.Count -ne 1) {
        Write-Host "Matched physical PnP instance: $InstanceId"
        $names | ForEach-Object { Write-Host "Interface: $_" }
        throw "Expected exactly one matching Logitech VIDEO_CAMERA Device Parameters key; found $($parameterKeys.Count). No changes made."
    }
    return $parameterKeys[0]
}
function Test-Administrator {
    $p = New-Object Security.Principal.WindowsPrincipal(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator PowerShell required'
    }
}

function Invoke-AvCamRegsvr32 {
    param([Parameter(Mandatory=$true)][string]$DllPath,[switch]$Unregister)
    # regsvr32.exe is a GUI-subsystem executable. Windows PowerShell may not
    # wait for its exit when invoked directly, leaving LASTEXITCODE unset.
    $regsvr32 = Join-Path $env:WINDIR 'System32\regsvr32.exe'
    $args = @()
    if ($Unregister) { $args += '/u' }
    $args += '/s'
    $args += ('"{0}"' -f $DllPath)
    $process = Start-Process -FilePath $regsvr32 -ArgumentList $args -Wait -PassThru -WindowStyle Hidden -ErrorAction Stop
    if ($null -eq $process) { throw 'Unable to obtain regsvr32 process exit status' }
    return [int]$process.ExitCode
}

# Rollback does not depend on the camera's current PnP health, status or
# enumeration. A broken camera extension must NEVER prevent recovery.
if ($Disable) {
    Test-Administrator
    if (-not (Test-Path -LiteralPath $backupState)) {
        throw "No saved AvCam installation record found: $backupState"
    }
    $saved = Get-Content -LiteralPath $backupState -Raw | ConvertFrom-Json
    if ($saved.Clsid -ne $clsid -or
        $saved.RegistryPath -notmatch '(?i)^HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\\{E5323777-F976-4F5B-9B55-B94699C46E44\}\\' -or
        $saved.RegistryPath -notmatch '(?i)VID_046D&PID_0825') {
        throw 'Refusing rollback: saved physical interface is not Logitech C270 VIDEO_CAMERA'
    }
    $savedPath = 'Registry::' + $saved.RegistryPath
    if (Test-Path -LiteralPath $savedPath) {
        $rollbackKey = Get-Item -LiteralPath $savedPath
        $current = $rollbackKey.GetValue('CameraDeviceMftClsid', $null)
        if ($current -and $current -ne $clsid) {
            throw 'Another vendor has changed the device transform; manual recovery required'
        }
        if ($current -eq $clsid) {
            Remove-ItemProperty -LiteralPath $savedPath -Name 'CameraDeviceMftClsid'
        }
    } else {
        Write-Warning 'Original USB camera registry interface is missing; no Device MFT key left to remove'
    }
    if (Test-Path -LiteralPath $installedDll) {
        $unregisterExit = Invoke-AvCamRegsvr32 -DllPath $installedDll -Unregister
        if ($unregisterExit -ne 0) {
            throw "COM unregistration failed with exit code $unregisterExit. Camera interface property was removed; install-state preserved for a retry."
        }
    }
    Remove-Item -LiteralPath $backupState -Force
    Write-Host 'AVCAM_PHYSICAL_C270_DISABLED'
    Write-Host 'Restart Windows Camera / browser; physical USB hardware and driver were never replaced.'
    return
}

$physical = @(Get-PhysicalLogitech)
Write-Host "Healthy physical Logitech C270 devices: $($physical.Count)"
foreach ($item in $physical) { Write-Host "$($item.FriendlyName) [$($item.InstanceId)]" }
if ($physical.Count -ne 1) { throw 'Exactly one live physical Logitech C270 camera is required' }
$key = Get-PhysicalInterface -InstanceId $physical[0].InstanceId
Write-Host "Physical VIDEO_CAMERA interface: $($key.Name)"
$existing = $key.GetValue('CameraDeviceMftClsid', $null)
$chain = $key.GetValue('CameraDeviceMftClsidChain', $null)
Write-Host "Existing CameraDeviceMftClsid: $existing"
Write-Host "Existing CameraDeviceMftClsidChain: $chain"
if (-not $Enable -and -not $Disable) {
    Write-Host 'PREFLIGHT_ONLY: physical Logitech driver/registry untouched.'
    Write-Host 'Run -Enable to opt into the reversible User-Mode Device MFT experiment.'
    Write-Host 'Run -Disable to remove ONLY the AvCam Device MFT.'
    return
}
Test-Administrator
$required = @('AvCamPhysicalDeviceMft.dll','AvCamPhysicalBridge.exe')
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $file) -PathType Leaf)) {
        throw "Missing experimental package component: $file"
    }
}
if ($existing -or $chain) {
    throw 'Existing Logitech Device MFT configuration detected; refusing third-party replacement.'
}
if (Test-Path -LiteralPath $backupState) {
    throw "AvCam installation record exists: $backupState. Use -Disable first."
}
Write-Host 'Only the physical Logitech camera interface will be updated. No USB drivers changed.'
Write-Host 'Experimental DLL may fail to load or stop camera streaming.'
$ack = Read-Host 'Type ENABLE_PHYSICAL_C270 to register the experimental camera transform'
if ($ack -ne 'ENABLE_PHYSICAL_C270') {
    Write-Host 'Not enabled.'
    return
}
New-Item -ItemType Directory -Path $folder -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'AvCamPhysicalDeviceMft.dll') -Destination $installedDll -Force
@{
    RegistryPath=$key.Name
    Clsid=$clsid
    Created=(Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath $backupState -Encoding UTF8
$registerExit = Invoke-AvCamRegsvr32 -DllPath $installedDll
if ($registerExit -ne 0) {
    throw "COM registration failed ($registerExit). Physical Logitech registry was not changed. Use -Disable to clean up the partial attempt."
}
try {
    New-ItemProperty -LiteralPath $key.PSPath -Name 'CameraDeviceMftClsid' -PropertyType String -Value $clsid -Force | Out-Null
    $verified = (Get-Item -LiteralPath $key.PSPath).GetValue('CameraDeviceMftClsid',$null)
    if ($verified -ne $clsid) { throw 'Device MFT registry write not verified' }
    Write-Host 'AVCAM_PHYSICAL_C270_ENABLED'
    Write-Host 'Logitech USB VID/PID and usbvideo.sys driver were not changed.'
    Write-Host 'Close camera apps, start OBS Virtual Camera, then AvCamPhysicalBridge.exe as admin.'
} catch {
    Remove-ItemProperty -LiteralPath $key.PSPath -Name 'CameraDeviceMftClsid' -ErrorAction SilentlyContinue
    $undoExit = Invoke-AvCamRegsvr32 -DllPath $installedDll -Unregister
    if ($undoExit -ne 0) {
        Write-Warning "Could not unregister AvCam COM class during rollback: $undoExit. Use -Disable."
    }
    throw
}
