/*
 * Cancellation of requests while they are still in UdeCx endpoint queues.
 *
 * UdeCx guidance requires an independent DISPATCH_LEVEL DPC to complete
 * a canceled URB, rather than synchronously completing it from WDF's
 * EvtIoCanceledOnQueue callback. Queue cancellation is distinct from
 * cancellation of an already-dispatched client-owned ISO request.
 *
 * The DPC owns each request until WdfRequestComplete has RETURNED.
 * QueueCancelOutstanding is part of the endpoint PURGE acknowledgement
 * barrier, but these requests never call AvCamUdeDrainEnter/Leave.
 *
 * This is still experimental, uninstalled research code. Nonpaged pool
 * exhaustion has a last-resort synchronous STATUS_CANCELLED completion,
 * which avoids permanently leaking a WDF request but is not the preferred
 * USB host controller completion path. Full forward-progress guarantees
 * and Windows VM/Driver Verifier testing remain release blockers.
 */
#include "AvCamUdeUsb.h"

#define AVCAM_UDE_CANCEL_TAG 'cUcA'

typedef struct _AVCAM_UDE_QUEUED_CANCEL {
    LIST_ENTRY Entry;
    WDFREQUEST Request;
} AVCAM_UDE_QUEUED_CANCEL, *PAVCAM_UDE_QUEUED_CANCEL;

VOID
AvCamUdeEvtEndpointCanceledOnQueue(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request)
{
    PAVCAM_UDE_ENDPOINT_QUEUE_CONTEXT qctx =
        AvCamUdeEndpointQueueGetContext(queue);
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx;
    PAVCAM_UDE_QUEUED_CANCEL node;

    /* WDF has transferred sole completion ownership to this callback.
     * Never forward or requeue the canceled request. This callback can
     * run at DISPATCH_LEVEL, so UdeCx PASSIVE_LEVEL APIs are prohibited.
     */
    if (qctx->Endpoint == NULL) {
        WdfRequestComplete(request, STATUS_CANCELLED);
        return;
    }
    node = (PAVCAM_UDE_QUEUED_CANCEL)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*node), AVCAM_UDE_CANCEL_TAG);
    if (node == NULL) {
        /* Emergency path: a canceled WDF request cannot be abandoned
         * if memory is exhausted. This exception must be stress-tested.
         */
        WdfRequestComplete(request, STATUS_CANCELLED);
        return;
    }
    node->Request = request;
    ctx = AvCamUdeEndpointGetContext(qctx->Endpoint);
    WdfSpinLockAcquire(ctx->Lock);
    if (ctx->QueueCancelOutstanding == MAXULONG) {
        WdfSpinLockRelease(ctx->Lock);
        ExFreePoolWithTag(node, AVCAM_UDE_CANCEL_TAG);
        WdfRequestComplete(request, STATUS_CANCELLED);
        return;
    }
    InsertTailList(&ctx->QueueCancelledRequests, &node->Entry);
    ++ctx->QueueCancelOutstanding;

    if (!ctx->QueueCancelDpcScheduled) {
        /* The spinlock serializes enqueue with the final empty-list
         * exit of EvtQueuedCancelDpc. A FALSE return may simply mean
         * that the same DPC has not yet started running.
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
        PLIST_ENTRY entry;
        PAVCAM_UDE_QUEUED_CANCEL node;
        BOOLEAN signalPurge;
        WDF_REQUEST_PARAMETERS params;
        WDFREQUEST canceledRequest;

        WdfSpinLockAcquire(ctx->Lock);
        if (IsListEmpty(&ctx->QueueCancelledRequests)) {
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
        entry = RemoveHeadList(&ctx->QueueCancelledRequests);
        WdfSpinLockRelease(ctx->Lock);
        node = CONTAINING_RECORD(
            entry, AVCAM_UDE_QUEUED_CANCEL, Entry);
        canceledRequest = node->Request;

        /* The callback only enqueues a non-null WDFREQUEST. Check again
         * at the consumer boundary so PREfast can prove we never pass a
         * potentially null handle to a WDF API. A corrupt node cannot
         * be completed; flag it during Driver Verifier testing.
         */
        NT_ASSERT(canceledRequest != NULL);
        if (canceledRequest != NULL) {
            /* The USB host stack may inspect USBD_STATUS even for
             * canceled requests that never reached the packetizer.
             */
            WDF_REQUEST_PARAMETERS_INIT(&params);
            WdfRequestGetParameters(canceledRequest, &params);
            if (params.Type == WdfRequestTypeDeviceControlInternal &&
                params.Parameters.Others.IoControlCode ==
                    IOCTL_INTERNAL_USB_SUBMIT_URB &&
                params.Parameters.Others.Arg1 != NULL) {
                PURB urb = (PURB)params.Parameters.Others.Arg1;
                urb->UrbHeader.Status = USBD_STATUS_CANCELED;
            }
            WdfRequestComplete(canceledRequest, STATUS_CANCELLED);
        }
        node->Request = NULL;
        ExFreePoolWithTag(node, AVCAM_UDE_CANCEL_TAG);

        /* Not even a queued cancellation can drain until its separate
         * DPC's WdfRequestComplete has returned. The request is invalid.
         */
        WdfSpinLockAcquire(ctx->Lock);
        NT_ASSERT(ctx->QueueCancelOutstanding != 0);
        if (ctx->QueueCancelOutstanding != 0) {
            --ctx->QueueCancelOutstanding;
        }
        WdfSpinLockRelease(ctx->Lock);
    }
}
