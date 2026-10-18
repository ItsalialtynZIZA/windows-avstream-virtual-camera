$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$controller = Get-Content (Join-Path $root "AvCamUde.c") -Raw
$usb = Get-Content (Join-Path $root "AvCamUdeUsb.c") -Raw
$header = Get-Content (Join-Path $root "AvCamUdeUsb.h") -Raw
$inf = Get-Content (Join-Path $root "AvCamUde.inf") -Raw
$workflow = Get-Content (Join-Path $root "..\..\.github\workflows\udecx-foundation.yml") -Raw

function Assert-Matches {
    param([string]$Data, [string]$Pattern, [string]$Message)
    if ($Data -notmatch $Pattern) {
        throw $Message
    }
}

Assert-Matches $inf 'Root\\AvCamUdeController' 'UdeCx must use a separate PnP identity.'
if ($inf -match 'Root\\AvCamVirtualUsbHost') {
    throw 'UdeCx must not replace the installed legacy bus device.'
}

Assert-Matches $controller 'UdecxInitializeWdfDeviceInit' 'UdeCx init is missing.'
Assert-Matches $controller 'UdecxWdfDeviceAddUsbDeviceEmulation' 'Controller USB emulation registration is missing.'
Assert-Matches $controller 'UdecxWdfDeviceTryHandleUserIoctl' 'UdeCx standard controller requests are not routed.'
Assert-Matches $controller 'g_AvCamUdeEnableAutoplug\s*=\s*FALSE' 'Unsafe: UVC device automatic plug-in must remain disabled.'
Assert-Matches $controller 'if\s*\(!g_AvCamUdeEnableAutoplug\)' 'Autoplug protection is missing.'
Assert-Matches $controller 'AvCamUdeUsbDisconnect\(device\)' 'Power exit must clean up emulated USB child.'
Assert-Matches $usb 'UdecxUsbDeviceInitAllocate' 'UdeCx USB device init allocation is missing.'
Assert-Matches $usb 'UdecxUsbDeviceInitSetSpeed\(init,\s*UdecxUsbHighSpeed\)' 'USB speed selection missing.'
Assert-Matches $usb 'g_AvCamUsbDeviceDescriptor' 'Shared USB device descriptor missing.'
Assert-Matches $usb 'g_AvCamUsbConfigurationDescriptor' 'Shared UVC config descriptor missing.'
Assert-Matches $usb 'UdecxUsbDeviceCreate' 'Native UdeCx child device creation missing.'
Assert-Matches $usb 'UdecxUsbEndpointCreate' 'Native UdeCx endpoints missing.'
Assert-Matches $usb 'UdecxUsbDevicePlugIn' 'Explicit USB child plug-in mechanism missing.'
Assert-Matches $usb 'UdecxUrbCompleteWithNtStatus\(request,\s*STATUS_NOT_SUPPORTED\)' 'Incomplete endpoint requests must not return false success.'
Assert-Matches $header 'AVCAM_UDE_CONTROLLER_CONTEXT' 'Controller lifecycle context missing.'

if ($workflow -match 'upload-artifact|pnputil|AvCamBusInstall\.exe') {
    throw 'Compile-only UdeCx CI must not publish an installable package or attempt installation.'
}
Assert-Matches $workflow 'EnablePREfast=true' 'UdeCx PREfast analysis must remain enabled.'

Write-Host "UdeCx guarded USB child contract: passed"
