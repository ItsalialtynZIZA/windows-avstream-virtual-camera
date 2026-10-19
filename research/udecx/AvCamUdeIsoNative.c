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

    WdfSpinLockAcquire(endpoint->Lock);
    resetPump = endpoint->PumpResetPending;
    endpoint->PumpResetPending = FALSE;
    WdfSpinLockRelease(endpoint->Lock);

    /* FrameLock serializes publication and leased ISO source access. */
    WdfWaitLockAcquire(controller->FrameLock, NULL);
    if (!controller->ChildPlugged ||
        controller->UsbDevice != qctx->UsbDevice ||
        !controller->FrameSourceReady ||
        !usb->Stream.Committed) {
        status = STATUS_DEVICE_NOT_READY;
        goto ExitLocked;
    }

    if (!endpoint->PumpInitialized) {
        if (AvCamUdeFramePumpInit(
                &endpoint->Pump, &controller->FrameSource) !=
            AVCAM_UDE_PUMP_OK) {
            status = STATUS_INVALID_DEVICE_STATE;
            goto ExitLocked;
        }
        endpoint->PumpInitialized = TRUE;
        AvCamUdeIsoClockReset(&endpoint->Clock);
    }
    if (resetPump) {
        if (AvCamUdeFramePumpReset(&endpoint->Pump) !=
            AVCAM_UDE_PUMP_OK) {
            status = STATUS_DATA_ERROR;
            goto ExitLocked;
        }
        AvCamUdeIsoClockReset(&endpoint->Clock);
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
    (VOID)AvCamUdeFramePumpReset(&endpoint->Pump);
    AvCamUdeIsoClockReset(&endpoint->Clock);

ExitLocked:
    WdfWaitLockRelease(controller->FrameLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* The sequential UdeCx endpoint queue only hands us one in-flight
     * request until WdfRequestComplete. Never replace a pending WDFREQUEST.
     */
    WdfSpinLockAcquire(endpoint->Lock);
    if (endpoint->PendingIsoRequest != NULL ||
        endpoint->Drain.PurgePending) {
        WdfSpinLockRelease(endpoint->Lock);
        return STATUS_DEVICE_BUSY;
    }
    endpoint->PendingIsoRequest = request;
    WdfSpinLockRelease(endpoint->Lock);

    if (!WdfDpcEnqueue(endpoint->IsoCompleteDpc)) {
        /* The DPC could already be queued/executing. Clear only our own
         * reservation; do not complete here. The outer callback will
         * complete the request via its existing error path.
         */
        WdfSpinLockAcquire(endpoint->Lock);
        if (endpoint->PendingIsoRequest == request) {
            endpoint->PendingIsoRequest = NULL;
        }
        WdfSpinLockRelease(endpoint->Lock);
        return STATUS_DEVICE_BUSY;
    }
    return STATUS_SUCCESS;
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
    BOOLEAN signalPurge = FALSE;
    NTSTATUS completion;

    /* Do not hold an endpoint lock across WDF request completion. */
    WdfSpinLockAcquire(ctx->Lock);
    request = ctx->PendingIsoRequest;
    ctx->PendingIsoRequest = NULL;
    purging = ctx->Drain.PurgePending;
    WdfSpinLockRelease(ctx->Lock);
    if (request == NULL) {
        return;
    }
    completion = (purging || WdfRequestIsCanceled(request)) ?
        STATUS_CANCELLED : STATUS_SUCCESS;
    WdfRequestComplete(request, completion);

    /* Only AFTER WdfRequestComplete returns is PURGE allowed to claim
     * that this request is fully gone. The WDFREQUEST is now invalid.
     */
    WdfSpinLockAcquire(ctx->Lock);
    (VOID)AvCamUdeDrainLeave(&ctx->Drain);
    signalPurge = ctx->Drain.PurgePending && ctx->Drain.Active == 0;
    WdfSpinLockRelease(ctx->Lock);
    if (signalPurge) {
        WdfWorkItemEnqueue(ctx->PurgeWorkItem);
    }
}
