/*
 * Native UdeCx ISO staging -> deferred WDF completion bridge.
 *
 * EXPERIMENTAL: controller autoplug and video endpoint dispatch gate remain
 * FALSE. Compiled and PREfast-analyzed, never installed by CI.
 *
 * Sealing UdeCx URB lengths MUST happen at PASSIVE_LEVEL. After the URB has
 * been staged and all buffer mappings are relinquished by this callback, a
 * distinct WDF DPC completes the request using WdfRequestComplete at
 * DISPATCH_LEVEL, as advised by the UdeCx client-driver guide.
 *
 * No pending request is marked cancelable because it spends only the short
 * DPC scheduling interval outside the endpoint's sequential queue. The DPC
 * checks WdfRequestIsCanceled before completion. Queued (not dispatched)
 * requests are owned by WDF/UdeCx. This path requires runtime cancellation
 * tests in the isolated VM and is NOT production ready.
 */
#include "AvCamUdeUsb.h"

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
AvCamUdeStageIsoRequestPassive(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request)
{
    PAVCAM_UDE_ENDPOINT_QUEUE_CONTEXT qctx =
        AvCamUdeEndpointQueueGetContext(queue);
    PAVCAM_UDE_ENDPOINT_CONTEXT endpoint;
    PAVCAM_UDE_USB_DEVICE_CONTEXT usb;
    PAVCAM_UDE_CONTROLLER_CONTEXT controller;
    WDF_REQUEST_PARAMETERS params;
    PURB urb;
    ULONG bytes = 0;
    ULONG packets;
    ULONGLONG firstMicroframe100ns = 0;
    AVCAM_UDE_ISO_CLOCK nextClock;
    BOOLEAN asap;
    BOOLEAN resetPump;
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        qctx->Endpoint == NULL || qctx->UsbDevice == NULL ||
        qctx->EndpointAddress != 0x81) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    endpoint = AvCamUdeEndpointGetContext(qctx->Endpoint);
    usb = AvCamUdeUsbGetContext(qctx->UsbDevice);
    controller = AvCamUdeControllerGetContext(usb->Controller);

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(request, &params);
    if (params.Type != WdfRequestTypeDeviceControlInternal ||
        params.Parameters.Others.IoControlCode !=
            IOCTL_INTERNAL_USB_SUBMIT_URB ||
        params.Parameters.Others.Arg1 == NULL) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    urb = (PURB)params.Parameters.Others.Arg1;
    status = AvCamUdeInspectIsoUrb(request);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    packets = urb->UrbIsochronousTransfer.NumberOfPackets;
    asap = (urb->UrbIsochronousTransfer.TransferFlags &
        USBD_START_ISO_TRANSFER_ASAP) != 0;

    /* FrameLock serializes publication and leased ISO source access.
     * Do not consume PumpResetPending until readiness has been checked:
     * an unavailable child/feeder must not lose its pending reset.
     */
    WdfWaitLockAcquire(controller->FrameLock, NULL);
    if (endpoint->PumpQuarantined) {
        /* A failed lease release invalidates this endpoint for its
         * remaining lifetime. New UVC COMMIT / START cannot clear it.
         */
        status = STATUS_DATA_ERROR;
        goto ExitLocked;
    }
    if (!controller->ChildPlugged ||
        controller->UsbDevice != qctx->UsbDevice ||
        !controller->FrameSourceReady ||
        !usb->Stream.Committed) {
        status = STATUS_DEVICE_NOT_READY;
        goto ExitLocked;
    }

    WdfSpinLockAcquire(endpoint->Lock);
    resetPump = endpoint->PumpResetPending;
    endpoint->PumpResetPending = FALSE;
    WdfSpinLockRelease(endpoint->Lock);

    /* Both Stream.Committed / CommitEpoch and FrameSource are protected
     * by FrameLock. A new COMMIT is a new video session; a previously
     * leased JPEG must not be resumed across that boundary.
     */
    if (endpoint->SeenCommitEpoch != usb->CommitEpoch) {
        resetPump = TRUE;
    }

    if (!endpoint->PumpInitialized) {
        if (AvCamUdeFramePumpInit(
                &endpoint->Pump, &controller->FrameSource) !=
            AVCAM_UDE_PUMP_OK) {
            status = STATUS_INVALID_DEVICE_STATE;
            goto ExitLocked;
        }
        endpoint->PumpInitialized = TRUE;
        endpoint->SeenCommitEpoch = usb->CommitEpoch;
        AvCamUdeIsoClockReset(&endpoint->Clock);
    }
    if (resetPump) {
        if (AvCamUdeFramePumpReset(&endpoint->Pump) !=
            AVCAM_UDE_PUMP_OK) {
            endpoint->PumpQuarantined = TRUE;
            status = STATUS_DATA_ERROR;
            goto ExitLocked;
        }
        AvCamUdeIsoClockReset(&endpoint->Clock);
        endpoint->SeenCommitEpoch = usb->CommitEpoch;
    }

    /* High-speed bInterval=1 uses eight 125-us packets per 1-ms USB
     * frame. Never mistake a 1-ms StartFrame for a 125-us microframe.
     * ASAP is deliberately mapped only to the monotonic emulation clock;
     * actual late scheduling still requires a USB host VM acceptance.
     */
    if (AvCamUdeIsoClockPlan(
            &endpoint->Clock, asap,
            urb->UrbIsochronousTransfer.StartFrame, packets,
            &firstMicroframe100ns, &nextClock) != AVCAM_UDE_CLOCK_OK) {
        status = STATUS_INVALID_PARAMETER;
        goto ResetFailedFrame;
    }
    status = AvCamUdePrepareIsoUrbFromPump(
        request, &endpoint->Pump, firstMicroframe100ns, &bytes);
    if (NT_SUCCESS(status)) {
        status = AvCamUdeSealPreparedIsoUrbPassive(request, bytes);
    }
    if (NT_SUCCESS(status)) {
        /* Commit planned host-frame time ONLY after the payload and
         * completion metadata were both validated successfully.
         */
        endpoint->Clock = nextClock;
        goto ExitLocked;
    }

ResetFailedFrame:
    if (AvCamUdeFramePumpReset(&endpoint->Pump) !=
        AVCAM_UDE_PUMP_OK) {
        endpoint->PumpQuarantined = TRUE;
    }
    AvCamUdeIsoClockReset(&endpoint->Clock);

ExitLocked:
    WdfWaitLockRelease(controller->FrameLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* The sequential UdeCx endpoint queue only hands us one in-flight
     * request until WdfRequestComplete. Never replace a pending WDFREQUEST.
     */
    /* WdfRequestComplete can reenter the sequential queue before the
     * previous DPC returns. Therefore enqueue and reserve the new request
     * under ONE spinlock. When the DPC is already running it will loop
     * and consume this request; do not requeue or mistake FALSE from
     * WdfDpcEnqueue for a safe request-completion decision.
     */
    WdfSpinLockAcquire(endpoint->Lock);
    if (endpoint->PendingIsoRequest != NULL ||
        endpoint->Drain.PurgePending) {
        /* The URB was already packetized and sealed. It will not reach
         * the host, so the next successful request must start a fresh
         * JPEG rather than continuing its partially emitted payload.
         */
        endpoint->PumpResetPending = TRUE;
        WdfSpinLockRelease(endpoint->Lock);
        return STATUS_DEVICE_BUSY;
    }
    endpoint->PendingIsoRequest = request;
    if (!endpoint->IsoDpcRunning &&
        !WdfDpcEnqueue(endpoint->IsoCompleteDpc)) {
        /* The lock prevents a DPC from stealing our request between
         * scheduling failure and rollback. Caller still owns it.
         */
        endpoint->PendingIsoRequest = NULL;
        endpoint->PumpResetPending = TRUE;
        WdfSpinLockRelease(endpoint->Lock);
        return STATUS_DEVICE_BUSY;
    }
    WdfSpinLockRelease(endpoint->Lock);
    return STATUS_SUCCESS;
}

/* The payload may have been sealed at PASSIVE_LEVEL but a PURGE or
 * cancellation can arrive before the separate DPC runs. Never publish
 * successful per-packet statuses for an URB that is being cancelled.
 * The request and its URB remain owned until WdfRequestComplete returns.
 */
static VOID
AvCamUdeCancelPreparedIsoAtDpc(_In_ WDFREQUEST request)
{
    WDF_REQUEST_PARAMETERS params;
    PURB urb;
    ULONG count;
    ULONG i;

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(request, &params);
    urb = (PURB)params.Parameters.Others.Arg1;
    if (urb == NULL) {
        return;
    }
    count = urb->UrbIsochronousTransfer.NumberOfPackets;
    /* Every staged URB passed the full variable-descriptor preflight
     * before the DPC could own it.
     */
    urb->UrbHeader.Status = USBD_STATUS_CANCELED;
    urb->UrbIsochronousTransfer.TransferBufferLength = 0;
    urb->UrbIsochronousTransfer.ErrorCount = count;
    for (i = 0; i < count; ++i) {
        urb->UrbIsochronousTransfer.IsoPacket[i].Length = 0;
        urb->UrbIsochronousTransfer.IsoPacket[i].Status = USBD_STATUS_CANCELED;
    }
}

VOID
AvCamUdeEvtIsoCompleteDpc(_In_ WDFDPC dpc)
{
    UDECXUSBENDPOINT usbEndpoint = (UDECXUSBENDPOINT)
        WdfDpcGetParentObject(dpc);
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx =
        AvCamUdeEndpointGetContext(usbEndpoint);
    WDFREQUEST request;
    BOOLEAN purging;
    BOOLEAN signalPurge;
    NTSTATUS completion;

    /* A completion can synchronously dispatch the next queued URB.
     * Keep the DPC active and drain each handoff rather than exiting
     * after one request and leaving a reentrant request stranded.
     */
    for (;;) {
        WdfSpinLockAcquire(ctx->Lock);
        ctx->IsoDpcRunning = TRUE;
        request = ctx->PendingIsoRequest;
        ctx->PendingIsoRequest = NULL;
        purging = ctx->Drain.PurgePending;
        if (request == NULL) {
            ctx->IsoDpcRunning = FALSE;
            WdfSpinLockRelease(ctx->Lock);
            return;
        }
        WdfSpinLockRelease(ctx->Lock);

        completion = (purging || WdfRequestIsCanceled(request)) ?
            STATUS_CANCELLED : STATUS_SUCCESS;

        if (completion == STATUS_CANCELLED) {
            AvCamUdeCancelPreparedIsoAtDpc(request);
            /* The staged JPEG's EOF may never have reached the host.
             * Force the next URB to start a new frame, never resume
             * abandoned MJPEG bytes after a canceled completion.
             */
            WdfSpinLockAcquire(ctx->Lock);
            ctx->PumpResetPending = TRUE;
            WdfSpinLockRelease(ctx->Lock);
        }
        WdfRequestComplete(request, completion);

        /* The request is invalid after WdfRequestComplete. Only now may
         * PURGE observe that its owned I/O has drained.
         */
        WdfSpinLockAcquire(ctx->Lock);
        (VOID)AvCamUdeDrainLeave(&ctx->Drain);
        signalPurge = ctx->Drain.PurgePending && ctx->Drain.Active == 0;
        WdfSpinLockRelease(ctx->Lock);
        if (signalPurge) {
            WdfWorkItemEnqueue(ctx->PurgeWorkItem);
        }
    }
}
