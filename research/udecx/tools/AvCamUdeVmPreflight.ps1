# AvCam UdeCx Windows 11 VM preflight (read-only).
# Run INSIDE a disposable Windows 11 virtual machine.
# Never installs a driver, changes boot policy, touches driver verifier,
# modifies PnP state, or contacts the primary Windows installation.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$system = Get-CimInstance -ClassName Win32_ComputerSystem
$os = Get-CimInstance -ClassName Win32_OperatingSystem
if ($null -eq $system -or $null -eq $os) {
    throw 'Unable to read guest OS identity; refusing test.'
}
$identity = "$($system.Manufacturer) $($system.Model)"
if ($identity -notmatch '(?i)virtual machine|vmware|virtualbox|qemu|kvm|bochs|xen|parallels|hyper-v') {
    throw "VM_PREFLIGHT_REFUSED: no recognizable VM hardware ($identity)."
}
# HypervisorPresent is NOT used as a VM proof: physical Windows hosts can
# expose a hypervisor when WSL2/Hyper-V/virtualization-based security runs.
$version = [Version]$os.Version
if ($version.Major -ne 10 -or $version.Build -lt 22000) {
    throw "Windows 11 guest required (detected $($os.Caption), build $($version.Build))."
}
if (-not [Environment]::Is64BitOperatingSystem -or
    $os.OSArchitecture -notmatch '64') {
    throw 'Windows 11 x64 guest is required for the WDK x64 AvCam driver.'
}

# This only reports existing device presence; no PnP operation is performed.
$devices = @(Get-CimInstance -ClassName Win32_PnPEntity)
$controllers = @($devices | Where-Object {
    $_.PNPDeviceID -match '(?i)^ROOT\\AVCAMUDECONTROLLER(\\|$)'
})
$camDevices = @($devices | Where-Object {
    $_.PNPDeviceID -match '(?i)^USB\\VID_1234&PID_5678([&\\]|$)'
})

$bootDisk = Get-PSDrive -Name $env:SystemDrive.TrimEnd(':') -ErrorAction Stop
$freeGiB = [Math]::Round($bootDisk.Free / 1GB, 1)
$memoryGiB = [Math]::Round([double]$system.TotalPhysicalMemory / 1GB, 1)
if ($memoryGiB -lt 4) {
    throw "The disposable VM needs at least 4 GiB RAM; detected $memoryGiB GiB."
}
if ($freeGiB -lt 8) {
    throw "The disposable VM needs at least 8 GiB free on its system volume; detected $freeGiB GiB."
}

Write-Host "Guest: $($os.Caption), build $($version.Build), x64"
Write-Host "Hypervisor identity: $identity"
Write-Host "Guest memory: $memoryGiB GiB; system volume free: $freeGiB GiB"
Write-Host "Existing AvCam research controllers: $($controllers.Count)"
Write-Host "Existing AvCam USB research nodes: $($camDevices.Count)"
if ($controllers.Count -gt 1 -or $camDevices.Count -gt 2) {
    throw 'Unexpected duplicate research hardware IDs in the VM: inspect before testing.'
}
Write-Host 'VM_PREFLIGHT_GUEST_READY'
Write-Host 'A guest preflight cannot verify the hypervisor checkpoint: create a VM snapshot on the HOST before any experimental driver installation.'
Write-Host 'No driver was installed, modified, enabled or started by this script.'
