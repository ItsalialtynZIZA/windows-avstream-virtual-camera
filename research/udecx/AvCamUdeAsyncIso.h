#pragma once
/*
 * Bounded asynchronous ISO endpoint/URB ownership model.
 *
 * This file deliberately contains NO WDF operations. It must be called with
 * ONE external endpoint lock held for every transition, and its tokens are
 * never kernel pointers. Native adapter obligations:
 *   - retain WDFREQUEST until a unique completion owner is established
 *   - honor WdfRequestMarkCancelableEx / UnmarkCancelable ownership rules
 *   - never let cancel callbacks free memory used by the codec worker
 *   - release USB buffers before MarkWorkerDone, then complete at validated IRQL
 *   - acknowledge purge only after ALL owned requests have drained
 *
 * The Windows UdeCx documentation currently gives PASSIVE_LEVEL for
 * UdecxUrbComplete* but separately recommends DPC completion of host URBs.
 * This model cannot resolve that API-level discrepancy. It is NOT connected
 * to live endpoint 0x81 until the native completion path is verified.
 */
#include "AvCamUdeEndpointDrain.h"

#define AVCAM_UDE_ASYNC_MAX_REQUESTS 8u
#define AVCAM_UDE_ASYNC_INVALID 0
#define AVCAM_UDE_ASYNC_OK 1
#define AVCAM_UDE_ASYNC_BUSY 2
#define AVCAM_UDE_ASYNC_NOT_FOUND 3

#define AVCAM_UDE_ASYNC_FREE 0u
#define AVCAM_UDE_ASYNC_WAITING 1u
#define AVCAM_UDE_ASYNC_WORKING 2u
#define AVCAM_UDE_ASYNC_READY 3u

#define AVCAM_UDE_ASYNC_FINISH_NONE 0u
#define AVCAM_UDE_ASYNC_FINISH_DATA 1u
#define AVCAM_UDE_ASYNC_FINISH_CANCELLED 2u

typedef struct _AVCAM_UDE_ASYNC_SLOT {
    unsigned long long Token;
    unsigned long long Generation;
    unsigned char Phase;
    unsigned char CancelRequested;
} AVCAM_UDE_ASYNC_SLOT;

typedef struct _AVCAM_UDE_ASYNC_ISO {
    AVCAM_UDE_ENDPOINT_DRAIN Drain;
    AVCAM_UDE_ASYNC_SLOT Slots[AVCAM_UDE_ASYNC_MAX_REQUESTS];
    unsigned long long NextToken;
    unsigned char ResetPending;
} AVCAM_UDE_ASYNC_ISO;

static __inline void
AvCamUdeAsyncInit(AVCAM_UDE_ASYNC_ISO* endpoint)
{
    unsigned int i;
    if (endpoint == 0) return;
    AvCamUdeDrainInit(&endpoint->Drain);
    endpoint->NextToken = 1;
    endpoint->ResetPending = 0;
    for (i = 0; i < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++i) {
        endpoint->Slots[i].Token = 0;
        endpoint->Slots[i].Generation = 0;
        endpoint->Slots[i].Phase = AVCAM_UDE_ASYNC_FREE;
        endpoint->Slots[i].CancelRequested = 0;
    }
}

static __inline AVCAM_UDE_ASYNC_SLOT*
AvCamUdeAsyncFind(AVCAM_UDE_ASYNC_ISO* endpoint, unsigned long long token)
{
    unsigned int i;
    if (endpoint == 0 || token == 0) return 0;
    for (i = 0; i < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++i) {
        if (endpoint->Slots[i].Phase != AVCAM_UDE_ASYNC_FREE &&
            endpoint->Slots[i].Token == token) {
            return &endpoint->Slots[i];
        }
    }
    return 0;
}

static __inline int
AvCamUdeAsyncStart(AVCAM_UDE_ASYNC_ISO* endpoint)
{
    if (endpoint == 0 || endpoint->ResetPending) {
        return AVCAM_UDE_ASYNC_INVALID;
    }
    return AvCamUdeDrainStart(&endpoint->Drain) == AVCAM_UDE_DRAIN_OK ?
        AVCAM_UDE_ASYNC_OK : AVCAM_UDE_ASYNC_BUSY;
}

/* Admission and the capacity decision are one locked operation. No input
 * is retained if full or stopped. Ticket never wraps and is not reused.
 */
static __inline int
AvCamUdeAsyncSubmit(
    AVCAM_UDE_ASYNC_ISO* endpoint,
    unsigned long long* token)
{
    unsigned int i;
    AVCAM_UDE_ASYNC_SLOT* slot = 0;
    if (token == 0) return AVCAM_UDE_ASYNC_INVALID;
    *token = 0;
    if (endpoint == 0 || endpoint->ResetPending ||
        endpoint->NextToken == 0 || endpoint->NextToken == ~0ull) {
        return AVCAM_UDE_ASYNC_INVALID;
    }
    for (i = 0; i < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++i) {
        if (endpoint->Slots[i].Phase == AVCAM_UDE_ASYNC_FREE) {
            slot = &endpoint->Slots[i];
            break;
        }
    }
    if (slot == 0) return AVCAM_UDE_ASYNC_BUSY;
    if (AvCamUdeDrainEnter(&endpoint->Drain) != AVCAM_UDE_DRAIN_OK) {
        return AVCAM_UDE_ASYNC_BUSY;
    }
    slot->Token = endpoint->NextToken++;
    slot->Generation = endpoint->Drain.Generation;
    slot->Phase = AVCAM_UDE_ASYNC_WAITING;
    slot->CancelRequested = 0;
    *token = slot->Token;
    return AVCAM_UDE_ASYNC_OK;
}

static __inline int
AvCamUdeAsyncCancel(
    AVCAM_UDE_ASYNC_ISO* endpoint, unsigned long long token)
{
    AVCAM_UDE_ASYNC_SLOT* slot = AvCamUdeAsyncFind(endpoint, token);
    if (slot == 0) return AVCAM_UDE_ASYNC_NOT_FOUND;
    slot->CancelRequested = 1;
    return AVCAM_UDE_ASYNC_OK;
}

/* WorkerStart may touch mapped URB memory only on success. After it starts,
 * Cancel/Purge/Reset cannot complete the request until WorkerDone.
 */
static __inline int
AvCamUdeAsyncWorkerStart(
    AVCAM_UDE_ASYNC_ISO* endpoint, unsigned long long token)
{
    AVCAM_UDE_ASYNC_SLOT* slot = AvCamUdeAsyncFind(endpoint, token);
    if (slot == 0) return AVCAM_UDE_ASYNC_NOT_FOUND;
    if (slot->Phase != AVCAM_UDE_ASYNC_WAITING ||
        slot->CancelRequested || endpoint->Drain.PurgePending ||
        endpoint->ResetPending ||
        slot->Generation != endpoint->Drain.Generation) {
        return AVCAM_UDE_ASYNC_BUSY;
    }
    slot->Phase = AVCAM_UDE_ASYNC_WORKING;
    return AVCAM_UDE_ASYNC_OK;
}

/* Ready means all buffer mappings/leases have been released by the native
 * worker. WAITING can be skipped only after cancellation or purge/reset.
 */
static __inline int
AvCamUdeAsyncWorkerDone(
    AVCAM_UDE_ASYNC_ISO* endpoint, unsigned long long token)
{
    AVCAM_UDE_ASYNC_SLOT* slot = AvCamUdeAsyncFind(endpoint, token);
    if (slot == 0) return AVCAM_UDE_ASYNC_NOT_FOUND;
    if (slot->Phase == AVCAM_UDE_ASYNC_WORKING ||
        (slot->Phase == AVCAM_UDE_ASYNC_WAITING &&
         slot->CancelRequested)) {
        slot->Phase = AVCAM_UDE_ASYNC_READY;
        return AVCAM_UDE_ASYNC_OK;
    }
    return AVCAM_UDE_ASYNC_BUSY;
}

/* Exactly one terminal decision and active-counter decrement. WDF cancellation
 * ownership still has to be resolved separately by the native adapter.
 */
static __inline unsigned int
AvCamUdeAsyncClaimCompletion(
    AVCAM_UDE_ASYNC_ISO* endpoint, unsigned long long token)
{
    AVCAM_UDE_ASYNC_SLOT* slot = AvCamUdeAsyncFind(endpoint, token);
    unsigned int result;
    if (slot == 0 || slot->Phase != AVCAM_UDE_ASYNC_READY) {
        return AVCAM_UDE_ASYNC_FINISH_NONE;
    }
    result = (slot->CancelRequested || endpoint->Drain.PurgePending ||
              endpoint->ResetPending ||
              slot->Generation != endpoint->Drain.Generation) ?
        AVCAM_UDE_ASYNC_FINISH_CANCELLED :
        AVCAM_UDE_ASYNC_FINISH_DATA;
    if (AvCamUdeDrainLeave(&endpoint->Drain) != AVCAM_UDE_DRAIN_OK) {
        return AVCAM_UDE_ASYNC_FINISH_NONE;
    }
    slot->Token = 0;
    slot->Generation = 0;
    slot->Phase = AVCAM_UDE_ASYNC_FREE;
    slot->CancelRequested = 0;
    return result;
}

static __inline int
AvCamUdeAsyncBeginPurge(AVCAM_UDE_ASYNC_ISO* endpoint)
{
    unsigned int i;
    if (endpoint == 0 || endpoint->ResetPending ||
        AvCamUdeDrainBeginPurge(&endpoint->Drain) != AVCAM_UDE_DRAIN_OK) {
        return AVCAM_UDE_ASYNC_BUSY;
    }
    for (i = 0; i < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++i) {
        if (endpoint->Slots[i].Phase != AVCAM_UDE_ASYNC_FREE) {
            endpoint->Slots[i].CancelRequested = 1;
        }
    }
    return AVCAM_UDE_ASYNC_OK;
}

static __inline int
AvCamUdeAsyncClaimPurgeAck(AVCAM_UDE_ASYNC_ISO* endpoint)
{
    if (endpoint == 0 || endpoint->ResetPending) return 0;
    return AvCamUdeDrainClaimPurgeAck(&endpoint->Drain);
}

/* Reset is held until all workers release their mappings and all requests
 * are claimed. Only then can the stream's FID/EOF state be safely reset.
 * This is an abstract reset barrier, not the UdeCx reset request callback.
 */
static __inline int
AvCamUdeAsyncBeginReset(AVCAM_UDE_ASYNC_ISO* endpoint)
{
    unsigned int i;
    if (endpoint == 0 || endpoint->ResetPending ||
        endpoint->Drain.PurgePending) {
        return AVCAM_UDE_ASYNC_BUSY;
    }
    endpoint->ResetPending = 1;
    endpoint->Drain.Accepting = 0;
    for (i = 0; i < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++i) {
        if (endpoint->Slots[i].Phase != AVCAM_UDE_ASYNC_FREE) {
            endpoint->Slots[i].CancelRequested = 1;
        }
    }
    return AVCAM_UDE_ASYNC_OK;
}

static __inline int
AvCamUdeAsyncFinishReset(AVCAM_UDE_ASYNC_ISO* endpoint)
{
    if (endpoint == 0 || !endpoint->ResetPending ||
        endpoint->Drain.Active != 0) {
        return AVCAM_UDE_ASYNC_BUSY;
    }
    endpoint->ResetPending = 0;
    if (AvCamUdeDrainStart(&endpoint->Drain) != AVCAM_UDE_DRAIN_OK) {
        endpoint->ResetPending = 1;
        return AVCAM_UDE_ASYNC_BUSY;
    }
    return AVCAM_UDE_ASYNC_OK;
}
