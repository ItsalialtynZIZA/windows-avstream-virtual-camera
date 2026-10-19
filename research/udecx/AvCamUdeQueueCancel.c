/*
 * Allocation-free canceled-on-queue URB completion for UdeCx.
 *
 * Every canceled WDFREQUEST is owned by this callback until its separate
 * endpoint DPC has called WdfRequestComplete. The DPC preserves the
 * caller-versus-completer execution separation required by UdeCx.
 *
 * The driver-defined WDF completion Information field is temporarily
 * used as a singly-linked FIFO pointer while the REQUEST IS NOT visible
 * to the USB host. It is set to zero BEFORE completion. This is valid for
 * queued canceled requests: no data was transferred and completion
 * information must be zero. This field is never accessed after completion.
 *
 * This eliminates per-cancellation nonpaged pool allocation (and the
 * prohibited immediate-completion OOM fallback). It still needs Windows
 * VM Driver Verifier validation of UdeCx callback ordering and teardown.
 */
#include "AvCamUdeUsb.h"

VOID
AvCamUdeEvtEndpointCanceledOnQueue(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request)
{
    PAVCAM_UDE_ENDPOINT_QUEUE_CONTEXT qctx =
        AvCamUdeEndpointQueueGetContext(queue);
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx;

    /* UdeCx installs the associated queue only AFTER storing its endpoint
     * identity. The NULL path represents a broken framework lifetime
     * invariant; do not leak a canceled WDF request if it ever occurs.
     */
    NT_ASSERT(qctx->Endpoint != NULL);
    if (qctx->Endpoint == NULL) {
        WdfRequestCompleteWithInformation(request, STATUS_CANCELLED, 0);
        return;
    }
    ctx = AvCamUdeEndpointGetContext(qctx->Endpoint);

    WdfSpinLockAcquire(ctx->Lock);
    /* The canceled request is exclusively owned here, so its temporary
     * Information value can carry the NEXT request in our private chain.
     * No other callback may inspect it until the separate DPC owns it.
     */
    WdfRequestSetInformation(request, 0);
    if (ctx->QueueCancelTail != NULL) {
        WdfRequestSetInformation(ctx->QueueCancelTail, (ULONG_PTR)request);
    } else {
        ctx->QueueCancelHead = request;
    }
    ctx->QueueCancelTail = request;
    NT_ASSERT(ctx->QueueCancelOutstanding != ~0ull);
    ++ctx->QueueCancelOutstanding;

    if (!ctx->QueueCancelDpcScheduled) {
        /* Protect both the first enqueue and the DPC's final empty-list
         * exit with the same endpoint spinlock. A FALSE result may mean
         * the DPC is already queued or running.
         */
        ctx->QueueCancelDpcScheduled = TRUE;
        (VOID)WdfDpcEnqueue(ctx->QueueCancelDpc);
    }
    WdfSpinLockRelease(ctx->Lock);
}

VOID
AvCamUdeEvtQueuedCancelDpc(_In_ WDFDPC dpc)
{
    UDECXUSBENDPOINT endpoint = (UDECXUSBENDPOINT)
        WdfDpcGetParentObject(dpc);
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx = AvCamUdeEndpointGetContext(endpoint);

    for (;;) {
        WDFREQUEST request;
        WDFREQUEST next;
        BOOLEAN signalPurge;
        WDF_REQUEST_PARAMETERS params;

        WdfSpinLockAcquire(ctx->Lock);
        request = ctx->QueueCancelHead;
        if (request == NULL) {
            ctx->QueueCancelDpcScheduled = FALSE;
            signalPurge = ctx->Drain.PurgePending &&
                ctx->Drain.Active == 0 &&
                ctx->QueueCancelOutstanding == 0;
            WdfSpinLockRelease(ctx->Lock);
            if (signalPurge) {
                WdfWorkItemEnqueue(ctx->PurgeWorkItem);
            }
            return;
        }

        next = (WDFREQUEST)(ULONG_PTR)WdfRequestGetInformation(request);
        ctx->QueueCancelHead = next;
        if (next == NULL) {
            ctx->QueueCancelTail = NULL;
        }
        /* This request is removed from the private chain. Its completion
         * Information must be zero when observed by the Windows USB stack.
         */
        WdfRequestSetInformation(request, 0);
        WdfSpinLockRelease(ctx->Lock);

        /* The USB host stack may read URB Header.Status even on canceled
         * transfers that never reached the MJPEG packetizer.
         */
        WDF_REQUEST_PARAMETERS_INIT(&params);
        WdfRequestGetParameters(request, &params);
        if (params.Type == WdfRequestTypeDeviceControlInternal &&
            params.Parameters.Others.IoControlCode ==
                IOCTL_INTERNAL_USB_SUBMIT_URB &&
            params.Parameters.Others.Arg1 != NULL) {
            PURB urb = (PURB)params.Parameters.Others.Arg1;
            urb->UrbHeader.Status = USBD_STATUS_CANCELED;
        }
        WdfRequestCompleteWithInformation(request, STATUS_CANCELLED, 0);

        /* Do not touch the invalid WDFREQUEST after completion. Releasing
         * the cancellation reference only AFTER the call returns ensures
         * that PURGE cannot acknowledge while the USB host sees the IRP.
         */
        WdfSpinLockAcquire(ctx->Lock);
        NT_ASSERT(ctx->QueueCancelOutstanding != 0);
        if (ctx->QueueCancelOutstanding != 0) {
            --ctx->QueueCancelOutstanding;
        }
        WdfSpinLockRelease(ctx->Lock);
    }
}
