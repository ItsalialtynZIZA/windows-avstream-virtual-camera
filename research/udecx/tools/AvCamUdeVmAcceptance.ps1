# Read-only VM acceptance for the UdeCx migration.
# NEVER installs, disables, replaces, or uninstalls any driver.
# Run only after an intentionally controlled installation in a disposable VM.
[CmdletBinding()]
param(
    [string]$MfProbePath = ".\AvCamUdeMfProbe.exe",
    [switch]$SkipMediaFoundation
)
$ErrorActionPreference = "Stop"

$system = Get-CimInstance -ClassName Win32_ComputerSystem
$identity = "$($system.Manufacturer) $($system.Model)"
if ($identity -notmatch '(?i)virtual machine|vmware|virtualbox|qemu|kvm|hyper-v|hypervisor|bochs|xen|parallels') {
    throw "Safety stop: VM not detected ($identity). Do not test the unfinished UdeCx driver on the primary Windows installation."
}
Write-Host "VM check passed: $identity"

$devices = @(Get-CimInstance -ClassName Win32_PnPEntity)
$controller = @($devices | Where-Object {
    $_.PNPDeviceID -match '(?i)^ROOT\\AVCAMUDECONTROLLER' -or
    $_.Name -match 'AvCam UdeCx Research USB Host Controller'
})
if ($controller.Count -ne 1) {
    throw "Expected exactly one isolated Root\AvCamUdeController, found $($controller.Count). Do not assume USB enumeration succeeded."
}
Write-Host "Controller: $($controller[0].Name)"
Write-Host "Controller ID: $($controller[0].PNPDeviceID)"

$child = @($devices | Where-Object {
    $_.PNPDeviceID -match '(?i)^USB\\VID_' -and
    ($_.Name -match '(?i)AvCam (Research )?Virtual USB Camera')
})
if ($child.Count -ne 1) {
    throw "Expected exactly one UdeCx camera under USB\VID_, found $($child.Count). Child USB enumeration not established."
}
$camera = $child[0]
if ($camera.Service -notmatch '(?i)^usbvideo$') {
    throw "Camera found but not bound to usbvideo.sys (service=$($camera.Service))."
}
if ($camera.ConfigManagerErrorCode -ne 0) {
    throw "Camera has PnP ConfigManager error $($camera.ConfigManagerErrorCode)."
}
Write-Host "usbvideo.sys binding confirmed: $($camera.Name)"
Write-Host "USB PnP instance: $($camera.PNPDeviceID)"

if ($SkipMediaFoundation) {
    Write-Host "Only enumeration checked; no Media Foundation sample claimed."
    return
}
$probe = (Resolve-Path -LiteralPath $MfProbePath -ErrorAction Stop).ProviderPath
Write-Host "Reading one MJPEG sample from Media Foundation using $probe ..."
$raw = & $probe 2>&1
$exitCode = $LASTEXITCODE
$logText = ($raw | Out-String)
Write-Host $logText
if ($exitCode -ne 0 -or $logText -notmatch 'MEDIA_FOUNDATION_SAMPLE_RECEIVED') {
    throw "Media Foundation did not return a nonempty sample (exit code $exitCode). USB enumeration alone is not sufficient."
}
Write-Host "VM_ACCEPTANCE_FIRST_MEDIA_FOUNDATION_FRAME_RECEIVED"
