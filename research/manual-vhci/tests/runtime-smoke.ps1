param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,

    [Parameter(Mandatory = $true)]
    [string]$DevConPath,

    [Parameter(Mandatory = $true)]
    [string]$ControllerPath
)

$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated PowerShell session.'
    }
}

function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments)
    Write-Host "> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath exited with code $LASTEXITCODE."
    }
}

Assert-Administrator

$package = (Resolve-Path $PackageDirectory).Path
$devcon = (Resolve-Path $DevConPath).Path
$controller = (Resolve-Path $ControllerPath).Path
$inf = Get-ChildItem $package -Recurse -Filter AvCamBus.inf -File | Select-Object -First 1
if (-not $inf) { throw 'AvCamBus.inf was not found in the package directory.' }

Write-Host 'Removing stale research bus instance if present...'
& $devcon remove 'Root\AvCamVirtualUsbHost' | Out-Host
Start-Sleep -Seconds 1

Write-Host 'Installing root-enumerated research bus...'
Invoke-Checked $devcon @('install', $inf.FullName, 'Root\AvCamVirtualUsbHost')
Start-Sleep -Seconds 2

$bus = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'ROOT\AVCAMVIRTUALUSBHOST*' } | Select-Object -First 1
if (-not $bus) { throw 'Root AvCamVirtualUsbHost device was not enumerated.' }
if ($bus.Status -ne 'OK') { throw "Root bus did not start cleanly. Status=$($bus.Status), InstanceId=$($bus.InstanceId)" }
Write-Host "Root bus OK: $($bus.InstanceId)"

Invoke-Checked $controller @('path')
Invoke-Checked $controller @('plug')
Start-Sleep -Seconds 2

$child = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\VID_1234&PID_5678*' } | Select-Object -First 1
if (-not $child) { throw 'Virtual USB child USB\VID_1234&PID_5678 was not enumerated.' }
Write-Host "Child enumerated: $($child.InstanceId) Status=$($child.Status) Class=$($child.Class)"

$hardwareIds = (Get-PnpDeviceProperty -InstanceId $child.InstanceId -KeyName 'DEVPKEY_Device_HardwareIds' -ErrorAction Stop).Data
if ($hardwareIds -notcontains 'USB\VID_1234&PID_5678') {
    throw "Child hardware IDs do not contain USB\VID_1234&PID_5678. Actual: $($hardwareIds -join ', ')"
}

$compatibleIds = (Get-PnpDeviceProperty -InstanceId $child.InstanceId -KeyName 'DEVPKEY_Device_CompatibleIds' -ErrorAction Stop).Data
if ($compatibleIds -notcontains 'USB\Class_0e&SubClass_03&Prot_00') {
    throw "Child compatible IDs are unexpected. Actual: $($compatibleIds -join ', ')"
}

Invoke-Checked $controller @('unplug')
Start-Sleep -Seconds 2

$remainingChild = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\VID_1234&PID_5678*' } | Select-Object -First 1
if ($remainingChild) { throw "Child remained present after unplug: $($remainingChild.InstanceId)" }

Write-Host 'Runtime smoke test passed: root bus start, interface open, child plug, ID validation, and child unplug.'
