#pragma once
/*
 * Portable, bounded, two-slot MJPEG frame source for the UdeCx research path.
 *
 * Caller supplies and owns both independent frame buffers. No allocation or
 * WDF callbacks occur in this module. ALL operations must be serialized by
 * the kernel adapter's endpoint/source lock; the model itself is NOT
 * thread-safe. A lease must be held across ALL ISO URBs for one JPEG frame.
 * Never replace the leased frame until the UVC EOF has been packetized.
 */
/* Never include user-mode CRT stdint.h in the WDK kernel build. The x64
 * driver already includes ntddk.h, where ULONG_PTR is pointer-width.
 */
#if defined(_AMD64_) && defined(WINNT)
typedef ULONG_PTR AVCAM_UDE_FRAME_POINTER_INTEGER;
#else
#include <stdint.h>
typedef uintptr_t AVCAM_UDE_FRAME_POINTER_INTEGER;
#endif
#include "../manual-vhci/shared/AvCamUvcControls.h"

#define AVCAM_UDE_FRAME_OK 0
#define AVCAM_UDE_FRAME_INVALID 1
#define AVCAM_UDE_FRAME_BUSY 2
#define AVCAM_UDE_FRAME_NO_DATA 3

typedef struct _AVCAM_UDE_FRAME_SLOT {
    unsigned char* Data;
    unsigned int Capacity;
    unsigned int Length;
    unsigned int Readers;
    unsigned long long Generation;
} AVCAM_UDE_FRAME_SLOT;

typedef struct _AVCAM_UDE_FRAME_SOURCE {
    AVCAM_UDE_FRAME_SLOT Slots[2];
    unsigned char Active;
    unsigned char HasFrame;
    unsigned long long NextGeneration;
} AVCAM_UDE_FRAME_SOURCE;

typedef struct _AVCAM_UDE_FRAME_LEASE {
    const unsigned char* Data;
    unsigned int Length;
    unsigned int Slot;
    unsigned long long Generation;
} AVCAM_UDE_FRAME_LEASE;

static __inline int
AvCamUdeFrameSourceInit(
    AVCAM_UDE_FRAME_SOURCE* source,
    unsigned char* first, unsigned int firstCapacity,
    unsigned char* second, unsigned int secondCapacity)
{
    if (source == 0 || first == 0 || second == 0 || first == second ||
        firstCapacity < 4u || secondCapacity < 4u ||
        firstCapacity > AVCAM_UVC_FRAME_MAX_BYTES ||
        secondCapacity > AVCAM_UVC_FRAME_MAX_BYTES) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    /* Reject partially overlapping source buffers, not only exact aliases. */
    if ((AVCAM_UDE_FRAME_POINTER_INTEGER)first < (AVCAM_UDE_FRAME_POINTER_INTEGER)second) {
        if ((AVCAM_UDE_FRAME_POINTER_INTEGER)second - (AVCAM_UDE_FRAME_POINTER_INTEGER)first < firstCapacity) {
            return AVCAM_UDE_FRAME_INVALID;
        }
    } else if ((AVCAM_UDE_FRAME_POINTER_INTEGER)first - (AVCAM_UDE_FRAME_POINTER_INTEGER)second < secondCapacity) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    source->Slots[0].Data = first;
    source->Slots[0].Capacity = firstCapacity;
    source->Slots[0].Length = 0;
    source->Slots[0].Readers = 0;
    source->Slots[0].Generation = 0;
    source->Slots[1].Data = second;
    source->Slots[1].Capacity = secondCapacity;
    source->Slots[1].Length = 0;
    source->Slots[1].Readers = 0;
    source->Slots[1].Generation = 0;
    source->Active = 0;
    source->HasFrame = 0;
    source->NextGeneration = 1;
    return AVCAM_UDE_FRAME_OK;
}

/* Atomic from callers' perspective ONLY if caller holds source lock.
 * Publication does not modify a buffer with outstanding leases.
 */
static __inline int
AvCamUdeFrameSourcePublish(
    AVCAM_UDE_FRAME_SOURCE* source,
    const unsigned char* jpeg, unsigned int bytes)
{
    unsigned int slot, i;
    AVCAM_UDE_FRAME_SLOT* target;
    if (source == 0 || jpeg == 0 || bytes < 4 ||
        bytes > AVCAM_UVC_FRAME_MAX_BYTES ||
        source->NextGeneration == 0) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    slot = source->HasFrame ? (unsigned int)(source->Active ^ 1u) : 0u;
    target = &source->Slots[slot];
    if (target->Data == 0 || target->Capacity < bytes) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    if (jpeg[0] != 0xffu || jpeg[1] != 0xd8u ||
        jpeg[bytes - 2u] != 0xffu || jpeg[bytes - 1u] != 0xd9u) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    if (target->Readers != 0) {
        return AVCAM_UDE_FRAME_BUSY;
    }
    for (i = 0; i < bytes; ++i) {
        target->Data[i] = jpeg[i];
    }
    target->Length = bytes;
    target->Generation = source->NextGeneration++;
    source->Active = (unsigned char)slot;
    source->HasFrame = 1;
    return AVCAM_UDE_FRAME_OK;
}

static __inline int
AvCamUdeFrameSourceAcquire(
    AVCAM_UDE_FRAME_SOURCE* source, AVCAM_UDE_FRAME_LEASE* lease)
{
    AVCAM_UDE_FRAME_SLOT* slot;
    if (source == 0 || lease == 0) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    lease->Data = 0;
    lease->Length = 0;
    lease->Slot = 0;
    lease->Generation = 0;
    if (!source->HasFrame) {
        return AVCAM_UDE_FRAME_NO_DATA;
    }
    slot = &source->Slots[source->Active];
    if (slot->Length < 4 || slot->Readers == ~0u) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    ++slot->Readers;
    lease->Data = slot->Data;
    lease->Length = slot->Length;
    lease->Slot = source->Active;
    lease->Generation = slot->Generation;
    return AVCAM_UDE_FRAME_OK;
}

static __inline int
AvCamUdeFrameSourceRelease(
    AVCAM_UDE_FRAME_SOURCE* source, AVCAM_UDE_FRAME_LEASE* lease)
{
    AVCAM_UDE_FRAME_SLOT* slot;
    if (source == 0 || lease == 0 || lease->Data == 0 ||
        lease->Slot >= 2u) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    slot = &source->Slots[lease->Slot];
    if (slot->Readers == 0 || slot->Generation != lease->Generation ||
        slot->Data != lease->Data || slot->Length != lease->Length) {
        return AVCAM_UDE_FRAME_INVALID;
    }
    --slot->Readers;
    lease->Data = 0;
    lease->Length = 0;
    lease->Slot = 0;
    lease->Generation = 0;
    return AVCAM_UDE_FRAME_OK;
}
