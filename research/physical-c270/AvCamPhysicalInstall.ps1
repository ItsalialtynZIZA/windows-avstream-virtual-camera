# AvCam Physical C270 - reversible Device MFT experiment.
# Default: inspect only. -Enable registers the COM video transform on the
# physical Logitech camera interface. -Disable removes ONLY our registration.
[CmdletBinding()]
param([switch]$Enable,[switch]$Disable)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Enable -and $Disable) { throw 'Choose -Enable OR -Disable' }
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
    if (-not (Test-Path -LiteralPath $RegistryRoot -PathType Container)) {
        throw "Camera interface registry root not found: $RegistryRoot"
    }
    # DeviceClasses has protected subkeys such as #GLOBAL\Properties.
    # Enumerate ONLY immediate camera interface keys; never use -Recurse.
    # Match the complete present physical PnP instance, not merely VID/PID
    # (otherwise an old, unplugged C270 could be selected by mistake).
    $encodedInstance = $InstanceId.Replace('\', '#')
    $expectedSuffix = '#' + $cameraClass
    $interfaceKeys = @(Get-ChildItem -LiteralPath $RegistryRoot -ErrorAction Stop |
        Where-Object {
            $_.PSChildName.IndexOf($encodedInstance,
                [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
            $_.PSChildName.EndsWith($expectedSuffix,
                [StringComparison]::OrdinalIgnoreCase)
        })
    $parameterKeys = @(
        foreach ($interface in $interfaceKeys) {
            # Open this one exact subkey; never traverse a protected sibling.
            $parameterPath = Join-Path $interface.PSPath '#GLOBAL\Device Parameters'
            if (Test-Path -LiteralPath $parameterPath -PathType Container) {
                Get-Item -LiteralPath $parameterPath -ErrorAction Stop
            }
        }
    )
    if ($parameterKeys.Count -ne 1) {
        Write-Host "Matched physical PnP instance: $InstanceId"
        $interfaceKeys | ForEach-Object { Write-Host "Interface: $($_.Name)" }
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
        & "$env:WINDIR\System32\regsvr32.exe" /u /s $installedDll
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "COM unregister returned exit code $LASTEXITCODE; AvCam camera interface setting removed."
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
& "$env:WINDIR\System32\regsvr32.exe" /s $installedDll
if ($LASTEXITCODE -ne 0) {
    throw "COM registration failed ($LASTEXITCODE). Physical Logitech registry was not changed."
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
    & "$env:WINDIR\System32\regsvr32.exe" /u /s $installedDll
    throw
}
