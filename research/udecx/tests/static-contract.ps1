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
$lifecycle = Get-Content (Join-Path $root 'AvCamUdeRequestLifecycle.h') -Raw
Assert-Matches $lifecycle 'WorkerOwnsBuffer' 'ISO cancellation must not release worker-owned buffers.'
Assert-Matches $lifecycle 'CompletionClaimed' 'Each UdeCx URB must have exactly one completion owner.'
Assert-Matches $lifecycle 'AvCamUdeRequestMarkCancelled' 'Cancellation path model missing.'
Assert-Matches $lifecycle 'AvCamUdeRequestClaimCompletion' 'Terminal completion ownership model missing.'
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

$adapter = Get-Content (Join-Path $root 'AvCamUdeIsoAdapter.c') -Raw
$source = Get-Content (Join-Path $root 'AvCamUdeFrameSource.h') -Raw
$project = Get-Content (Join-Path $root 'AvCamUde.vcxproj') -Raw
Assert-Matches $project 'AvCamUdeIsoAdapter.c' 'Native ISO staging adapter must be WDK-built.'
Assert-Matches $adapter 'AvCamUdeInspectIsoUrb' 'Native adapter must validate the real URB.'
Assert-Matches $adapter 'AvCamUdeIsoFill' 'Native adapter must call the MJPEG ISO encoder.'
Assert-Matches $adapter 'USBD_STATUS_SUCCESS' 'Native adapter must update ISO packet statuses.'
Assert-Matches $source 'AvCamUdeFrameSourcePublish' 'Immutable JPEG publication API missing.'
Assert-Matches $source 'AvCamUdeFrameSourceAcquire' 'Immutable JPEG lease API missing.'
Assert-Matches $source 'AvCamUdeFrameSourceRelease' 'Frame lease release API missing.'
Assert-Matches $source 'target->Readers != 0' 'Publishing must not overwrite leased frames.'
Assert-Matches $usb 'STATUS_NOT_SUPPORTED' 'Live ISO endpoint must remain disabled until request ownership is safe.'
Assert-Matches $usb 'callbacks.EvtUsbEndpointStart\s*=\s*AvCamUdeEvtEndpointStart' 'Endpoint start callback missing.'
Assert-Matches $usb 'callbacks.EvtUsbEndpointPurge\s*=\s*AvCamUdeEvtEndpointPurge' 'Endpoint purge callback missing.'
Assert-Matches $usb 'UdecxUsbEndpointPurgeComplete\(endpoint\)' 'Purge must acknowledge UdeCx after draining forwarded I/O.'
if ($usb -match 'WdfIoQueue(Start|Purge)\(') {
    throw 'UdeCx owns the direct endpoint queues; do not independently start or purge them.'
}

$drain = Get-Content (Join-Path $root 'AvCamUdeEndpointDrain.h') -Raw
Assert-Matches $drain 'AvCamUdeDrainEnter' 'An endpoint must count accepted requests.'
Assert-Matches $drain 'AvCamUdeDrainLeave' 'An endpoint must drain accepted requests.'
Assert-Matches $drain 'AvCamUdeDrainClaimPurgeAck' 'Purge acknowledgement must be single-owner.'
Assert-Matches $header 'AVCAM_UDE_ENDPOINT_CONTEXT' 'Native endpoint-specific lifecycle context is missing.'
Assert-Matches $usb 'WdfSpinLockAcquire' 'Endpoint state must be synchronized.'
Assert-Matches $usb 'WdfWorkItemEnqueue' 'Purge must be deferred to PASSIVE_LEVEL.'
Assert-Matches $usb 'WdfWorkItemGetParentObject' 'Purge worker must retain endpoint identity.'
Assert-Matches $usb 'AvCamUdeDrainClaimPurgeAck' 'Purge worker must verify that outstanding I/O drained.'
Assert-Matches $usb 'AvCamUdeEvtEndpointPurgeWorkItem' 'Purge worker callback missing.'
if ($usb -match '(?s)AvCamUdeEvtEndpointPurge\([^)]*\)\s*\{[^}]*UdecxUsbEndpointPurgeComplete') {
    throw 'Unsafe inline purge acknowledgement from DISPATCH_LEVEL callback.'
}

$pump = Get-Content (Join-Path $root 'AvCamUdeFramePump.h') -Raw
Assert-Matches $pump 'AvCamUdeFrameSourceAcquire' 'Frame pump must acquire immutable JPEG leases.'
Assert-Matches $pump 'AvCamUdeFrameSourceRelease' 'Frame pump must release leases at EOF.'
Assert-Matches $pump 'AvCamUdeIsoFill' 'Frame pump must emit native ISO packets.'
Assert-Matches $adapter 'AvCamUdeFramePumpFill' 'Native adapter must stage source-backed ISO URBs.'
Assert-Matches $header 'AvCamUdePrepareIsoUrbFromPump' 'Native frame pump API declaration missing.'
if ($usb -match 'AvCamUdePrepareIsoUrbFromPump\s*\(') {
    throw 'Streaming endpoint must remain fail-closed until WDF async completion is verified.'
}

$upload = Get-Content (Join-Path $root 'AvCamUdeUploadProtocol.h') -Raw
Assert-Matches $upload 'AvCamUdeValidateUpload' 'Versioned JPEG upload validation is required.'
Assert-Matches $upload 'AvCamUdeValidateJpegHeader' 'MJPEG frame dimensions must be validated.'
Assert-Matches $upload 'IOCTL_AVCAM_UDE_SUBMIT_MJPEG' 'Private write-only upload IOCTL must be declared.'
Assert-Matches $controller 'WdfDeviceInitAssignSDDLString' 'Private controller access must be restricted.'
Assert-Matches $controller 'IOCTL_AVCAM_UDE_SUBMIT_MJPEG' 'Controller must handle JPEG frame ingestion.'
Assert-Matches $controller 'WdfRequestRetrieveInputBuffer' 'Upload must retrieve a bounded METHOD_BUFFERED input.'
Assert-Matches $controller 'AvCamUdeValidateUpload' 'Upload validation must run before frame publication.'
Assert-Matches $controller 'AvCamUdeFrameSourcePublish' 'Controller must publish MJPEG into immutable slots.'
Assert-Matches $controller 'AvCamUdeEnsureFrameStoreLocked' 'Source allocation must be lazy and bounded.'
Assert-Matches $controller 'AvCamUdeEvtControllerCleanup' 'MJPEG slots need a controller cleanup callback.'
Assert-Matches $usb 'context->ChildPlugged = FALSE' 'Plug-out must revoke ingestion.'
Assert-Matches $controller 'if \(!g_AvCamUdeEnableAutoplug\)' 'Experimental USB device must remain unplugged by default.'

$vm = Get-Content (Join-Path $root 'tools/AvCamUdeVmAcceptance.ps1') -Raw
Assert-Matches $vm 'Win32_ComputerSystem' 'VM acceptance must check virtualization.'
Assert-Matches $vm 'VID_1234&PID_5678' 'VM acceptance must target UdeCx research VID/PID.'
Assert-Matches $vm 'usbvideo' 'VM acceptance must confirm real Windows USB video binding.'
Assert-Matches $vm 'MEDIA_FOUNDATION_SAMPLE_RECEIVED' 'VM acceptance must require an MF sample.'
if ($vm -match '(?i)\bpnputil\s+/(add-driver|delete-driver|install)|New-PnpDevice|Disable-PnpDevice|Enable-PnpDevice') {
    throw 'VM acceptance probe must never install or change drivers.'
}

$async = Get-Content (Join-Path $root 'AvCamUdeAsyncIso.h') -Raw
Assert-Matches $async 'AVCAM_UDE_ASYNC_MAX_REQUESTS' 'Bounded ISO queue size is required.'
Assert-Matches $async 'AvCamUdeAsyncSubmit' 'Admission state machine missing.'
Assert-Matches $async 'AvCamUdeAsyncWorkerStart' 'Worker ownership gate missing.'
Assert-Matches $async 'AvCamUdeAsyncWorkerDone' 'Worker buffer release transition missing.'
Assert-Matches $async 'AvCamUdeAsyncCancel' 'Cancellation transition missing.'
Assert-Matches $async 'AvCamUdeAsyncClaimCompletion' 'Single-completion transition missing.'
Assert-Matches $async 'AVCAM_UDE_ASYNC_COMPLETING' 'Completion ownership must be reserved until native callback return.'
Assert-Matches $async 'AvCamUdeAsyncCompletionReturned' 'Drain reference must survive native WDF completion.'
Assert-Matches $async 'AvCamUdeAsyncBeginPurge' 'Purge cancellation path missing.'
Assert-Matches $async 'AvCamUdeAsyncClaimPurgeAck' 'Purge barrier missing.'
Assert-Matches $async 'AvCamUdeAsyncBeginReset' 'Reset cancellation path missing.'
Assert-Matches $async 'AvCamUdeAsyncFinishReset' 'Reset generation barrier missing.'
Assert-Matches $header 'AvCamUdeAsyncIso.h' 'WDK native header must compile portable ownership model.'
if ($usb -match '(?s)endpoint->EndpointAddress == 0x81.{0,600}AvCamUdePrepareIsoUrbFromPump\s*\(') {
    throw 'Research UdeCx endpoint must remain fail-closed until native WDF cancel/IRQL verified.'
}
Assert-Matches $drain 'AvCamUdeDrainResetWhenIdle' 'Reset must reject active endpoint I/O.'
Assert-Matches $usb 'AvCamUdeDrainResetWhenIdle' 'Native endpoint reset must use drain barrier.'
Assert-Matches $usb 'WdfRequestCompleteWithInformation' 'UdeCx reset request must report explicit status.'
Assert-Matches $usb 'STATUS_DEVICE_BUSY' 'A reset with outstanding URBs must not report success.'
Write-Host "UdeCx guarded USB child contract: passed"
