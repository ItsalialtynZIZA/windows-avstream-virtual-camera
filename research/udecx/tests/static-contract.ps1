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
Assert-Matches $controller 'if\s*\(!AvCamUdeAutoplugEnabled\(\)\)' 'UdeCx D0 entry must be guarded by VM-only or disabled-default autoplug decision.'
Assert-Matches $controller '#if defined\(AVCAM_UDE_VM_ENUMERATION_ONLY\)' 'Enumeration option must be compile-time gated.'
Assert-Matches $controller '#else\s+return g_AvCamUdeEnableAutoplug;' 'Default kernel build must keep the USB child unplugged.'
Assert-Matches $usb 'g_AvCamUdeEnableNativeIso\s*=\s*FALSE' 'Video ISO must stay disabled, even with VM enumeration mode.'
$wdkProject = Get-Content (Join-Path $root 'AvCamUde.vcxproj') -Raw
Assert-Matches $wdkProject 'AvCamUdeVmEnumerationTest' 'Explicit VM enumeration MSBuild property is missing.'
Assert-Matches $wdkProject 'I_UNDERSTAND_DISPOSABLE_VM_ONLY' 'VM-only enumeration property must require explicit acknowledgment.'
Assert-Matches $wdkProject 'AVCAM_UDE_VM_ENUMERATION_ONLY' 'VM-only build must supply its own compiler define.'
Assert-Matches $controller 'AvCamUdeUsbDisconnect\(device\)' 'Power exit must clean up emulated USB child.'
Assert-Matches $usb 'UdecxUsbDeviceInitAllocate' 'UdeCx USB device init allocation is missing.'
Assert-Matches $usb 'UdecxUsbDeviceInitSetSpeed\(init,\s*UdecxUsbHighSpeed\)' 'USB speed selection missing.'
Assert-Matches $usb 'g_AvCamUdeIsolatedDeviceDescriptor' 'UdeCx child must enumerate using isolated experimental identity.'
$isolatedIdentity = Get-Content (Join-Path $root 'AvCamUdeTestIdentity.h') -Raw
$legacyIdentity = Get-Content (Join-Path $root '../manual-vhci/shared/AvCamUsbDescriptors.h') -Raw
Assert-Matches $isolatedIdentity '#define AVCAM_UDE_TEST_PID 0x5679u' 'Isolated UdeCx test PID must be 5679.'
Assert-Matches $legacyIdentity '#define AVCAM_USB_PID 0x5678u' 'Legacy AvCamBus PID must remain 5678.'
Assert-Matches $usb '#include "AvCamUdeTestIdentity.h"' 'UdeCx device must include isolated identity header.'
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
Assert-Matches $controller 'if\s*\(!AvCamUdeAutoplugEnabled\(\)\)' 'Experimental USB device must remain gated at D0 entry.'

$vm = Get-Content (Join-Path $root 'tools/AvCamUdeVmAcceptance.ps1') -Raw
Assert-Matches $vm 'Win32_ComputerSystem' 'VM acceptance must check virtualization.'
Assert-Matches $vm 'VID_1234&PID_5679' 'VM acceptance must target UdeCx research VID/PID.'
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
Assert-Matches $adapter 'AvCamUdeSealPreparedIsoUrbPassive' 'Native PASSIVE ISO sealing API missing.'
Assert-Matches $adapter 'UdecxUrbSetBytesCompleted' 'Native ISO sealing must explicitly report transferred bytes.'
Assert-Matches $adapter 'AvCamUdeIsoCompletionPacketValid' 'Sealed ISO URB must preserve host window boundaries.'
Assert-Matches $header 'AvCamUdeSealPreparedIsoUrbPassive' 'PASSIVE ISO seal signature missing.'
$native = Get-Content (Join-Path $root 'AvCamUdeIsoNative.c') -Raw
Assert-Matches $project 'AvCamUdeIsoNative.c' 'Native ISO DPC must be WDK compiled.'
Assert-Matches $usb 'g_AvCamUdeEnableNativeIso\s*=\s*FALSE' 'Unsafe: unfinished live ISO must remain disabled.'
Assert-Matches $usb 'qctx->EndpointAddress == 0x81 && g_AvCamUdeEnableNativeIso' 'Native endpoint must retain fail-closed gate.'
Assert-Matches $usb 'WDF_DPC_CONFIG_INIT\(&dpcConfig, AvCamUdeEvtIsoCompleteDpc\)' 'Separate DPC completion object is missing.'
Assert-Matches $usb 'AvCamUdeStageIsoRequestPassive' 'ISO handler does not transfer ownership to native bridge.'
Assert-Matches $native 'AvCamUdePrepareIsoUrbFromPump' 'Real ISO URB must use source-backed MJPEG encoder.'
Assert-Matches $native 'AvCamUdeSealPreparedIsoUrbPassive' 'Native ISO worker must seal bytes on PASSIVE_LEVEL.'
Assert-Matches $native 'WdfDpcEnqueue' 'Deferred WDF request completion must be independently queued.'
Assert-Matches $native 'WdfRequestIsCanceled' 'DPC must check canceled requests.'
Assert-Matches $native 'WdfRequestComplete\(request, completion\)' 'DPC must complete the native request.'
Assert-Matches $native 'AvCamUdeDrainLeave' 'DPC must release active I/O only after WDF completion.'
Assert-Matches $usb 'WdfWaitLockAcquire\(controller->FrameLock' 'EP0 commits must serialize against ISO staging.'
if ($native -match 'UdecxUrbComplete(WithNtStatus)?\s*\(') {
    throw 'Never call PASSIVE_LEVEL-only UdeCx URB completion from the DPC.'
}
Assert-Matches $usb 'ctx->PumpResetPending = TRUE' 'Endpoint START/PURGE/RESET must invalidate packetizer.'
Assert-Matches $usb 'AvCamUdeDrainStart\(&ctx->Drain\)' 'New dynamic endpoint must restart under its own lock.'
if ($usb -match '(?s)AvCamUdeEvtEndpointsConfigure\([^)]*\)\s*\{.{0,900}AvCamUdeEndpointGetContext\(videoEndpoint\)') {
    throw 'Dynamic endpoint can be destroyed before configuration callback; stale handle access prohibited.'
}
$clock = Get-Content (Join-Path $root 'AvCamUdeIsoClock.h') -Raw
$native = Get-Content (Join-Path $root 'AvCamUdeIsoNative.c') -Raw
Assert-Matches $clock 'AvCamUdeIsoClockPlan' 'Must preserve USB host StartFrame semantics.'
Assert-Matches $clock 'AVCAM_UDE_CLOCK_MICROFRAMES_PER_FRAME' 'High-speed one-frame packet multiple missing.'
Assert-Matches $native 'USBD_START_ISO_TRANSFER_ASAP' 'ASAP URB scheduling missing.'
Assert-Matches $native 'AvCamUdeIsoClockPlan' 'Native ISO path ignores the host StartFrame.'
Assert-Matches $native 'endpoint->Clock = nextClock' 'USB time must only commit after a successful seal.'
Assert-Matches $urb 'packets % 8u != 0' 'High-speed ISO URBs must have 8-packet aligned counts.'
Assert-Matches $native 'endpoint->IsoDpcRunning' 'Reentrant DPC handoff guard missing.'
Assert-Matches $native 'for \(;;\)' 'DPC must drain requests delivered during WDF completion.'
Assert-Matches $usb 'g_AvCamUdeEnableNativeIso\s*=\s*FALSE' 'Do not enable native ISO on the host.'
if ($native -match 'NextMicroframe100ns\s*\+=') {
    throw 'Synthetic-only packet clock is prohibited once StartFrame support exists.'
}
Assert-Matches $native 'AvCamUdeCancelPreparedIsoAtDpc' 'Canceled URB packet metadata must not report success.'
Assert-Matches $native 'USBD_STATUS_CANCELED' 'Canceled host ISO packets must use USB failure status.'
Assert-Matches $native 'UrbIsochronousTransfer.ErrorCount = count' 'All canceled packets must be counted.'
Assert-Matches $native 'urb->UrbIsochronousTransfer.TransferBufferLength = 0' 'Canceled byte count must be zero.'
Assert-Matches $native 'FrameLock serializes publication and leased ISO source access' 'ISO staging must retain the frame lock.'
Assert-Matches $header 'ULONGLONG CommitEpoch;' 'USB stream needs a commit generation.'
Assert-Matches $header 'ULONGLONG SeenCommitEpoch;' 'Endpoint must remember negotiated generation.'
Assert-Matches $usb '[+][+]device->CommitEpoch' 'Successful UVC commit must advance the generation.'
Assert-Matches $native 'SeenCommitEpoch != usb->CommitEpoch' 'Native ISO staging must detect renegotiation.'
Assert-Matches $native 'endpoint->SeenCommitEpoch = usb->CommitEpoch' 'Reset must record the active commit generation.'
Assert-Matches $native 'endpoint->PumpResetPending = TRUE' 'Discard a staged but never delivered ISO frame on DPC handoff failure.'
Assert-Matches $header 'WDFDEVICE Controller;' 'Endpoint must retain a stable parent controller identity.'
Assert-Matches $usb 'ctx->Controller = controller' 'Endpoint controller identity not initialized.'
Assert-Matches $usb 'AvCamUdeFramePumpReset\(&ctx->Pump\)' 'Purge must release incomplete MJPEG source leases.'
Assert-Matches $usb 'WdfWaitLockAcquire\(controller->FrameLock' 'Purge lease release must serialize with frame publication.'
Assert-Matches $usb 'UdecxUsbEndpointPurgeComplete\(endpoint\)' 'Purge acknowledgement must remain present.'
$queuedCancel = Get-Content (Join-Path $root 'AvCamUdeQueueCancel.c') -Raw
Assert-Matches $project 'AvCamUdeQueueCancel.c' 'Queued-cancellation DPC must be WDK compiled.'
Assert-Matches $usb 'queueConfig.EvtIoCanceledOnQueue\s*=\s*AvCamUdeEvtEndpointCanceledOnQueue' 'Queued URBs require explicit cancellation ownership.'
Assert-Matches $usb 'WDF_DPC_CONFIG_INIT\(&dpcConfig, AvCamUdeEvtQueuedCancelDpc\)' 'Separate queue-cancel completion DPC missing.'
Assert-Matches $header 'QueueCancelOutstanding' 'Queued cancellations must block premature PURGE.'
Assert-Matches $usb 'ctx->QueueCancelOutstanding == 0' 'PURGE must wait for canceled URB completions.'
Assert-Matches $queuedCancel 'WdfDpcEnqueue\(ctx->QueueCancelDpc\)' 'Canceled URBs must be deferred into separate DPC.'
Assert-Matches $queuedCancel 'AvCamUdeEvtQueuedCancelDpc' 'Queue-cancel DPC callback missing.'
Assert-Matches $queuedCancel 'USBD_STATUS_CANCELED' 'Canceled queued USB URBs require failure metadata.'
Assert-Matches $queuedCancel 'WdfRequestCompleteWithInformation\(request, STATUS_CANCELLED, 0\)' 'DPC must finish canceled URBs with zero transfer information.'
Assert-Matches $queuedCancel '--ctx->QueueCancelOutstanding' 'Queue cancellation drain must happen after completion.'
Assert-Matches $queuedCancel 'ctx->QueueCancelDpcScheduled = FALSE' 'Reentrant DPC schedule must reset when empty.'
Assert-Matches $queuedCancel 'WdfRequestGetInformation\(request\)' 'Cancellation FIFO must retrieve the next request without pool allocation.'
Assert-Matches $queuedCancel 'WdfRequestSetInformation\(request, 0\)' 'Cancel chain must clear completion information.'
Assert-Matches $queuedCancel 'ctx->QueueCancelHead = next' 'Cancellation FIFO must safely pop its head.'
Assert-Matches $header 'WDFREQUEST QueueCancelHead;' 'Cancellation queue needs an allocation-free request head.'
Assert-Matches $header 'WDFREQUEST QueueCancelTail;' 'Cancellation queue needs an allocation-free request tail.'
if ($queuedCancel -match 'ExAllocatePool|ExFreePool|POOL_FLAG_|WdfObjectAllocateContext') {
    throw 'Canceled-on-queue completion must not depend on extra allocations.'
}
if ($usb -match '(?s)AvCamUdeEvtEndpointCanceledOnQueue\s*\([^)]*\)\s*\{[^}]*WdfRequestComplete\(') {
    throw 'UdeCx queue cancellation must use a separate DPC, not inline completion.'
}
Assert-Matches $usb 'queueAttributes.ParentObject = \(WDFOBJECT\)\*endpoint' 'Dynamic endpoint must own its request queue.'
Assert-Matches $usb 'UdecxUsbEndpointSetWdfIoQueue\(\*endpoint, queue\)' 'New endpoint child queue must be registered with UdeCx.'
if ($usb -match 'WdfObjectDelete\(queue\)') {
    throw 'Endpoint-owned WDF queue must not be independently double deleted.'
}
Assert-Matches $queuedCancel 'ctx->QueueCancelTail = request' 'Canceled queue requests need FIFO storage until DPC owns them.'
Assert-Matches $queuedCancel 'WdfRequestCompleteWithInformation\(request, STATUS_CANCELLED, 0\)' 'Queue cancellations must complete from their own DPC.'
Assert-Matches $usb 'ctx->QueueCancelOutstanding == 0' 'Queued-cancel completion count must gate PURGE.'
Assert-Matches $usb 'WdfDpcCancel\(ctx->QueueCancelDpc, TRUE\)' 'PURGE must wait for queue-cancel DPC return.'
Assert-Matches $usb 'WdfDpcCancel\(ctx->IsoCompleteDpc, TRUE\)' 'PURGE must wait for ISO completion DPC return.'
Assert-Matches $usb 'ctx->QueueCancelDpcScheduled = TRUE' 'Canceled URBs arriving during purge quiescence must be rescheduled.'
Assert-Matches $usb 'AvCamUdeDrainClaimPurgeAck' 'Final PURGE claim missing after DPC quiescence.'
# The endpoint may not claim PURGE while the last MJPEG lease is still
# held: reclaiming it after clearing PurgePending creates a START race.
$purgeBegin = $usb.IndexOf("VOID" + [Environment]::NewLine + "AvCamUdeEvtEndpointPurgeWorkItem")
$purgeFinish = $usb.IndexOf("VOID" + [Environment]::NewLine + "AvCamUdeEvtEndpointReset", $purgeBegin)
if ($purgeBegin -lt 0 -or $purgeFinish -le $purgeBegin) {
    throw 'Cannot isolate endpoint PURGE completion for lifetime audit.'
}
$purgeBody = $usb.Substring($purgeBegin, $purgeFinish - $purgeBegin)
$resetPosition = $purgeBody.IndexOf('AvCamUdeFramePumpReset(&ctx->Pump)')
$claimPosition = $purgeBody.IndexOf('AvCamUdeDrainClaimPurgeAck(&ctx->Drain)')
$ackPosition = $purgeBody.IndexOf('UdecxUsbEndpointPurgeComplete(endpoint)')
if ($resetPosition -lt 0 -or $claimPosition -le $resetPosition -or
    $ackPosition -le $claimPosition) {
    throw 'Unsafe PURGE order: free leased JPEG, claim ACK under lock, then notify UdeCx.'
}
# A failed MJPEG frame-lease reset must not permit stale ISO data after
# UdeCx PURGE. Structural checks complement (not replace) VM concurrency tests.
$nativeIso = Get-Content (Join-Path $root 'AvCamUdeIsoNative.c') -Raw
Assert-Matches $header 'BOOLEAN PumpQuarantined;' 'Endpoint needs latched failed-pump safety state.'
Assert-Matches $usb 'ctx->PumpQuarantined = TRUE;' 'PURGE reset failure must quarantine the endpoint.'
Assert-Matches $usb 'ctx->PumpQuarantined = FALSE;' 'New endpoint must initialize the quarantine latch.'
Assert-Matches $nativeIso 'if \(endpoint->PumpQuarantined\)' 'Native ISO must refuse quarantined frame pumps.'
Assert-Matches $nativeIso 'endpoint->PumpQuarantined = TRUE;' 'Failed native JPEG lease reset must fail closed.'
$vmInstaller = Get-Content (Join-Path $root 'tools/AvCamUdeVmRootInstall.cpp') -Raw
$vmEnumeration = Get-Content (Join-Path $root 'tools/AvCamUdeVmEnumerationReport.ps1') -Raw
Assert-Matches $vmInstaller 'Root\\AvCamUdeController' 'Guest-only installer must use isolated new root identity.'
Assert-Matches $vmInstaller 'RequireDisposableVmHardware' 'Guest-only root installer must enforce SMBIOS safety gate.'
Assert-Matches $vmInstaller 'I_UNDERSTAND_DISPOSABLE_VM_ONLY' 'Guest-only root installer requires explicit acknowledgment.'
if ($vmInstaller -match 'Root\\AvCamVirtualUsbHost|oem61\.inf|VID_046D&PID_0825') {
    throw 'VM installer must never refer to legacy AvCamBus or physical Logitech camera.'
}
Assert-Matches $vmEnumeration 'VM_UDECX_ENUMERATION_ONLY_PASSED' 'VM report must distinguish enumeration from MF video.'
Assert-Matches $vmEnumeration 'VID_1234&PID_5679' 'VM report must target isolated test PID.'
if ($vmEnumeration -match '(?i)\bpnputil\s+/(add-driver|delete-driver|install)|New-PnpDevice|Disable-PnpDevice|Enable-PnpDevice|UpdateDriverForPlugAndPlayDevices') {
    throw 'VM enumeration report must remain read-only.'
}
Assert-Matches $udecxTests 'AvCamUdeVmRootInstall.cpp' 'VM root installation tool must be independently compiled.'
$udecxMfProbe = Get-Content (Join-Path $root 'tools/AvCamUdeMfProbe.cpp') -Raw
$udecxTests = Get-Content (Join-Path $root 'tests/CMakeLists.txt') -Raw
Assert-Matches $udecxMfProbe 'link.find\(L"vid_1234&pid_5679"\)' 'MF acceptance must check isolated UdeCx symbolic-link PID.'
Assert-Matches $udecxMfProbe 'const bool matchingUsbId' 'MF acceptance must identify its USB device, not merely a friendly name.'
if ($udecxMfProbe -match 'if\s*\(matchingName\s*\|\|\s*matchingUsbId\)') {
    throw 'MF acceptance must not select legacy AvCamBus by friendly name.'
}
Assert-Matches $udecxTests '\.\./tools/AvCamUdeMfProbe\.cpp' 'Build must use isolated UdeCx-only MF acceptance source.'
if ($udecxTests -match '\.\./\.\./manual-vhci/tools/AvCamMfProbe\.cpp') {
    throw 'UdeCx acceptance must not compile the legacy AvCamBus camera selector.'
}
# Physical Logitech C270 must be an unambiguous, read-only user-mode
# source. Never choose a USB camera solely by its friendly name.
$selector = Get-Content (Join-Path $root '../../feeder/src/MediaFoundationDeviceSelector.h') -Raw
$capture = Get-Content (Join-Path $root '../../feeder/src/MediaFoundationFrameSource.cpp') -Raw
$feeder = Get-Content (Join-Path $root 'tools/AvCamUdeObsFeeder.cpp') -Raw
Assert-Matches $selector 'HasUsbIdentity' 'Physical USB identity matcher missing.'
Assert-Matches $selector 'IsVidPidPair' 'Source VID/PID must be strictly validated.'
Assert-Matches $capture 'MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK' 'Physical source must validate MF symbolic-link identity.'
Assert-Matches $capture 'MatchesSource' 'Physical MF source must require BOTH display name and VID/PID.'
Assert-Matches $capture 'matches != 1u' 'Ambiguous physical camera matches must fail closed.'
Assert-Matches $feeder '--source-usb-id' 'Feeder must expose an explicit real USB source selector.'
Assert-Matches $feeder 'sourceUsbId' 'Feeder must pass source USB identity to MF source.'
Assert-Matches $feeder 'bool dryRun = false' 'Source smoke test must not silently become a device upload.'
Assert-Matches $udecxTests 'mf-device-selector-tests.cpp' 'Portable physical C270 selector tests must be wired to CI.'
Assert-Matches $udecxTests 'udecx-physical-c270-source-selection' 'Physical identity regression CTest target missing.'
Write-Host "UdeCx guarded USB child contract: passed"
