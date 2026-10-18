#pragma once
/*
 * Portable state machine for ownership of a queued USB transfer.
 *
 * This is a model of the rules for a future WDF request/worker/DPC adapter,
 * NOT a thread-safe implementation. The kernel adapter must serialize every
 * transition under one spin lock; callers must NOT complete a request merely
 * because this model returns "ready". The UdeCx/URB completion threading and
 * IRQL contract still require runtime validation.
 */
#define AVCAM_UDE_COMPLETION_NONE 0u
#define AVCAM_UDE_COMPLETION_DATA 1u
#define AVCAM_UDE_COMPLETION_CANCELLED 2u

typedef struct _AVCAM_UDE_REQUEST_LIFECYCLE {
    unsigned char WorkerOwnsBuffer;
    unsigned char WorkerFinished;
    unsigned char CancelRequested;
    unsigned char CompletionClaimed;
} AVCAM_UDE_REQUEST_LIFECYCLE;

static __inline void
AvCamUdeRequestInit(AVCAM_UDE_REQUEST_LIFECYCLE* request)
{
    if (request == 0) return;
    request->WorkerOwnsBuffer = 1;
    request->WorkerFinished = 0;
    request->CancelRequested = 0;
    request->CompletionClaimed = 0;
}

static __inline int
AvCamUdeRequestMarkCancelled(AVCAM_UDE_REQUEST_LIFECYCLE* request)
{
    if (request == 0 || request->CompletionClaimed) return 0;
    request->CancelRequested = 1;
    return 1;
}

static __inline int
AvCamUdeRequestWorkerFinished(AVCAM_UDE_REQUEST_LIFECYCLE* request)
{
    if (request == 0 || request->WorkerFinished ||
        request->CompletionClaimed || !request->WorkerOwnsBuffer) {
        return 0;
    }
    request->WorkerOwnsBuffer = 0;
    request->WorkerFinished = 1;
    return 1;
}

/*
 * Returns exactly one terminal decision. Never claim while the buffer is
 * still owned by an asynchronous encoder/mapper. Cancellation wins if it
 * arrived before the completion claim.
 */
static __inline unsigned int
AvCamUdeRequestClaimCompletion(AVCAM_UDE_REQUEST_LIFECYCLE* request)
{
    if (request == 0 || request->CompletionClaimed ||
        request->WorkerOwnsBuffer || !request->WorkerFinished) {
        return AVCAM_UDE_COMPLETION_NONE;
    }
    request->CompletionClaimed = 1;
    return request->CancelRequested ?
        AVCAM_UDE_COMPLETION_CANCELLED :
        AVCAM_UDE_COMPLETION_DATA;
}
