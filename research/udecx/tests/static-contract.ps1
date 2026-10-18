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
$iso = Get-Content (Join-Path $root "AvCamUdeIso.h") -Raw
Assert-Matches $iso 'AvCamUdeIsoFill' 'Portable host-window ISO packetizer is missing.'
$urb = Get-Content (Join-Path $root 'AvCamUdeIsoUrb.c') -Raw
$layout = Get-Content (Join-Path $root 'AvCamUdeIsoUrbLayout.h') -Raw
Assert-Matches $urb 'WdfRequestGetParameters' 'Native ISO URBs must be read from WDF request parameters.'
Assert-Matches $urb 'URB_FUNCTION_ISOCH_TRANSFER' 'ISO URB function must be checked.'
Assert-Matches $urb 'UdecxUrbRetrieveBuffer' 'ISO transfer buffer must be mapped via UdeCx.'
Assert-Matches $urb 'AvCamUdeIsoUrbOffsetValid' 'ISO packet offsets must be validated.'
Assert-Matches $layout 'AvCamUdeIsoUrbHeaderValid' 'Variable-size ISO URB descriptor count must be validated.'
Assert-Matches $usb 'AvCamUdeInspectIsoUrb' 'Native ISO validation must be routed from the video endpoint.'
Assert-Matches $controller 'WdfExecutionLevelPassive' 'UdeCx buffer inspection must run at PASSIVE_LEVEL.'
Assert-Matches $iso 'AvCamIsoPacketWindowValid' 'Every host ISO packet window must be validated.'
Assert-Matches $iso 'AvCamUvcPacedPacketCapacity' 'ISO MJPEG must enforce bounded packet pacing.'
Assert-Matches $iso 'AVCAM_UDE_ISO_MAX_PACKETS' 'ISO packet count limit must be explicit.'
Assert-Matches $iso 'AvCamUvcPacketizeMjpeg' 'ISO packetizer must preserve UVC FID/EOF.'
Assert-Matches $iso 'NextFrameStart100ns' '30fps scheduling state is missing.'

Assert-Matches $usb 'UdecxEndpointTypeDynamic' 'UVC alt0 and alt1 require UdeCx dynamic endpoints.'
Assert-Matches $usb 'EvtUsbDeviceDefaultEndpointAdd\s*=\s*AvCamUdeEvtDefaultEndpointAdd' 'Default endpoint must be added by UdeCx callback.'
Assert-Matches $usb 'EvtUsbDeviceEndpointAdd\s*=\s*AvCamUdeEvtDynamicEndpointAdd' 'UVC video endpoint must be added dynamically.'
Assert-Matches $usb 'EvtUsbDeviceEndpointsConfigure\s*=\s*AvCamUdeEvtEndpointsConfigure' 'UVC interface alternate-setting callback missing.'
if ($usb -match 'UdecxEndpointTypeSimple') {
    throw 'Simple UdeCx endpoints are incompatible with the advertised UVC alternate settings.'
}
$control = Get-Content (Join-Path $root "AvCamUdeControl.h") -Raw
Assert-Matches $control 'AvCamUdeHandleUvcInterfaceControl' 'Portable bounded UVC control validator is missing.'
Assert-Matches $control 'setupLength\s*>\s*transferCapacity' 'USB setup length must never exceed available transfer buffer.'

Assert-Matches $usb 'AvCamUdeEndpointQueueGetContext' 'URB dispatch needs endpoint-specific queue context.'
Assert-Matches $usb 'UdecxUrbRetrieveControlSetupPacket' 'UVC EP0 handler must read the real USB setup packet.'
Assert-Matches $usb 'UdecxUrbRetrieveBuffer' 'UVC EP0 handler must map the UdeCx URB transfer buffer.'
Assert-Matches $usb 'AvCamUdeHandleUvcInterfaceControl' 'Kernel EP0 handler must call bounded UVC Probe/Commit validation.'
Assert-Matches $usb 'UdecxUrbSetBytesCompleted' 'UdeCx completion must report actual transferred bytes.'
Assert-Matches $usb 'UdecxUrbComplete\(request, USBD_STATUS_STALL_PID\)' 'Unsupported UVC class requests must STALL explicitly.'
Assert-Matches $usb 'AvCamUvcInitState' 'Each USB child must initialize its Probe/Commit state.'
Assert-Matches $usb 'transferred > transferCapacity' 'Completed USB data length must be bounded by the UdeCx buffer.'
Assert-Matches $usb 'UdecxUsbEndpointCreate' 'Native UdeCx endpoints missing.'
Assert-Matches $usb 'UdecxUsbDevicePlugIn' 'Explicit USB child plug-in mechanism missing.'
Assert-Matches $usb 'UdecxUrbCompleteWithNtStatus\(request,\s*STATUS_NOT_SUPPORTED\)' 'Incomplete endpoint requests must not return false success.'
Assert-Matches $header 'AVCAM_UDE_CONTROLLER_CONTEXT' 'Controller lifecycle context missing.'

$activeWorkflow = ($workflow -split "\r?\n" | Where-Object { $_ -notmatch '^\s*#' }) -join "`n"
if ($activeWorkflow -match 'actions/upload-artifact|pnputil|AvCamBusInstall\.exe') {
    throw 'Compile-only UdeCx CI must not publish an installable package or attempt installation.'
}
Assert-Matches $workflow 'EnablePREfast=true' 'UdeCx PREfast analysis must remain enabled.'

Write-Host "UdeCx guarded USB child contract: passed"
