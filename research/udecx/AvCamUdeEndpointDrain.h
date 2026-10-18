#pragma once
/*
 * Endpoint START/PURGE/active-I/O drain state machine.
 *
 * This header is a pure model, NOT a synchronization primitive. The kernel
 * adapter must hold the SAME WDFSPINLOCK around every transition. Never
 * invoke a WDF/UdeCx completion while that lock is held.
 *
 * Active includes all requests currently owned by the client, even when a
 * PASSIVE_LEVEL worker is still mapping or encoding their buffers.
 * MarkDone is legal only AFTER the worker can no longer access the request.
 */
#define AVCAM_UDE_DRAIN_OK 0
#define AVCAM_UDE_DRAIN_INVALID 1
#define AVCAM_UDE_DRAIN_BUSY 2

typedef struct _AVCAM_UDE_ENDPOINT_DRAIN {
    unsigned int Active;
    unsigned char Accepting;
    unsigned char PurgePending;
    unsigned long long Generation;
} AVCAM_UDE_ENDPOINT_DRAIN;

static __inline void
AvCamUdeDrainInit(AVCAM_UDE_ENDPOINT_DRAIN* state)
{
    if (state == 0) return;
    state->Active = 0;
    state->Accepting = 0;
    state->PurgePending = 0;
    state->Generation = 1;
}

static __inline int
AvCamUdeDrainStart(AVCAM_UDE_ENDPOINT_DRAIN* state)
{
    if (state == 0 || state->PurgePending || state->Active != 0 ||
        state->Generation == 0 || state->Generation == ~0ull) {
        return AVCAM_UDE_DRAIN_INVALID;
    }
    ++state->Generation;
    state->Accepting = 1;
    return AVCAM_UDE_DRAIN_OK;
}

static __inline int
AvCamUdeDrainEnter(AVCAM_UDE_ENDPOINT_DRAIN* state)
{
    if (state == 0 || !state->Accepting || state->PurgePending ||
        state->Active == ~0u) {
        return AVCAM_UDE_DRAIN_BUSY;
    }
    ++state->Active;
    return AVCAM_UDE_DRAIN_OK;
}

static __inline int
AvCamUdeDrainLeave(AVCAM_UDE_ENDPOINT_DRAIN* state)
{
    if (state == 0 || state->Active == 0) {
        return AVCAM_UDE_DRAIN_INVALID;
    }
    --state->Active;
    return AVCAM_UDE_DRAIN_OK;
}

/* Return BUSY on a second purge while one is outstanding.
 * Queued I/O that UdeCx has never delivered is owned by UdeCx.
 */
static __inline int
AvCamUdeDrainBeginPurge(AVCAM_UDE_ENDPOINT_DRAIN* state)
{
    if (state == 0 || state->PurgePending) {
        return AVCAM_UDE_DRAIN_BUSY;
    }
    state->Accepting = 0;
    state->PurgePending = 1;
    return AVCAM_UDE_DRAIN_OK;
}

/* Exactly one acknowledgment after ALL client-owned I/O has drained.
 * A client must call this under the lock, then acknowledge via UdeCx OUTSIDE
 * the lock, at PASSIVE_LEVEL. A new START cannot occur before acknowledgement.
 */
static __inline int
AvCamUdeDrainClaimPurgeAck(AVCAM_UDE_ENDPOINT_DRAIN* state)
{
    if (state == 0 || !state->PurgePending || state->Active != 0) {
        return 0;
    }
    state->PurgePending = 0;
    return 1;
}
