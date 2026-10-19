# Read-only acceptance for AvCam UdeCx in a DISPOSABLE Windows 11 VM.
# No driver install/uninstall, registry mutation, PnP change or verifier edits.
# Caller must intentionally configure the VM and experimental driver first.
[CmdletBinding()]
param(
    [string]$MfProbePath = ".\AvCamUdeMfProbe.exe",
    [switch]$SkipMediaFoundation
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$system = Get-CimInstance -ClassName Win32_ComputerSystem
if ($null -eq $system) {
    throw "Cannot identify Windows host. VM verification is mandatory."
}
$identity = "$($system.Manufacturer) $($system.Model)"
# HypervisorPresent by itself is NOT proof of a VM; Hyper-V hosts report it.
if ($identity -notmatch '(?i)virtual machine|vmware|virtualbox|qemu|kvm|bochs|xen|parallels|hyper-v') {
    throw "Safety stop: VM not detected ($identity). Do not test on primary Windows."
}
Write-Host "VM check passed: $identity"

$devices = @(Get-CimInstance -ClassName Win32_PnPEntity)
if ($devices.Count -eq 0) {
    throw "No PnP devices were returned. Cannot verify emulated USB hardware."
}

$controllers = @($devices | Where-Object {
    $_.PNPDeviceID -match '(?i)^ROOT\\AVCAMUDECONTROLLER(\\|$)'
})
if ($controllers.Count -ne 1) {
    throw "Expected exactly one isolated ROOT\AvCamUdeController; found $($controllers.Count)."
}
if ($controllers[0].ConfigManagerErrorCode -ne 0) {
    throw "Controller PnP problem code: $($controllers[0].ConfigManagerErrorCode)"
}
Write-Host "Controller PnP: $($controllers[0].PNPDeviceID)"

# Composite USB cameras can have both a parent VID/PID device and a
# usbvideo-bound interface child. Find the actual usbvideo service on the
# research hardware ID; do not depend on the friendly-name localization.
$cameras = @($devices | Where-Object {
    $_.PNPDeviceID -match '(?i)^USB\\VID_1234&PID_5678([&\\]|$)' -and
    $_.Service -match '(?i)^usbvideo$'
})
if ($cameras.Count -ne 1) {
    throw "Expected exactly one USB\VID_1234&PID_5678 camera bound to usbvideo; found $($cameras.Count)."
}
$camera = $cameras[0]
if ($camera.ConfigManagerErrorCode -ne 0) {
    throw "Camera PnP problem code: $($camera.ConfigManagerErrorCode)."
}
Write-Host "USB VIDEO BINDING CONFIRMED: $($camera.PNPDeviceID)"
Write-Host "Device name: $($camera.Name)"

if ($SkipMediaFoundation) {
    Write-Host "VM_ENUMERATION_ONLY: passed. No actual video sample has been tested."
    return
}

$probe = (Resolve-Path -LiteralPath $MfProbePath -ErrorAction Stop).ProviderPath
if ([IO.Path]::GetExtension($probe) -ine '.exe') {
    throw "Expected the compiled AvCamUdeMfProbe.exe, got: $probe"
}
Write-Host "Probing the FIRST actual Media Foundation MJPEG sample with $probe"
$raw = & $probe 2>&1
$exitCode = $LASTEXITCODE
$logText = ($raw | Out-String)
Write-Host $logText
if ($exitCode -ne 0 -or
    $logText -notmatch '(?m)^MEDIA_FOUNDATION_SAMPLE_RECEIVED(?:\s|$)') {
    throw "Media Foundation failed to return a nonempty sample (exit code $exitCode). USB enumeration is not video."
}
Write-Host "VM_ACCEPTANCE_FIRST_MEDIA_FOUNDATION_FRAME_RECEIVED"
