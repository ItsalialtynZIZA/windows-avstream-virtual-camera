#pragma once
/*
 * ISO frame pump: one leased MJPEG image at a time, shared across URBs.
 *
 * Pure and intentionally NOT thread-safe. One endpoint worker must serialize
 * Fill/Reset/Publish under a common source/stream lock. A new source image
 * can only be selected AFTER the UVC EOF for the previous leased image.
 *
 * No USB request ownership, IRQL, cancellation, URB writes or completion is
 * performed here. Native UdeCx adapter must preflight the entire URB first.
 */
#include <limits.h>
#include "AvCamUdeFrameSource.h"
#include "AvCamUdeIso.h"

#define AVCAM_UDE_PUMP_OK 0
#define AVCAM_UDE_PUMP_INVALID 1
#define AVCAM_UDE_PUMP_NO_FRAME 2

typedef struct _AVCAM_UDE_FRAME_PUMP {
    AVCAM_UDE_FRAME_SOURCE* Source;
    AVCAM_UDE_FRAME_LEASE Lease;
    AVCAM_UDE_ISO_STREAM Stream;
} AVCAM_UDE_FRAME_PUMP;

static __inline int
AvCamUdeFramePumpInit(
    AVCAM_UDE_FRAME_PUMP* pump, AVCAM_UDE_FRAME_SOURCE* source)
{
    if (pump == 0 || source == 0) return AVCAM_UDE_PUMP_INVALID;
    pump->Source = source;
    pump->Lease.Data = 0;
    pump->Lease.Length = 0;
    pump->Lease.Slot = 0;
    pump->Lease.Generation = 0;
    AvCamUdeIsoReset(&pump->Stream);
    return AVCAM_UDE_PUMP_OK;
}

/* Do not call reset while an encoder worker is using this pump.
 * After an aborted URB, discard phase and release the frame lease.
 */
static __inline int
AvCamUdeFramePumpReset(AVCAM_UDE_FRAME_PUMP* pump)
{
    if (pump == 0 || pump->Source == 0) {
        return AVCAM_UDE_PUMP_INVALID;
    }
    if (pump->Lease.Data != 0 &&
        AvCamUdeFrameSourceRelease(pump->Source, &pump->Lease) !=
            AVCAM_UDE_FRAME_OK) {
        return AVCAM_UDE_PUMP_INVALID;
    }
    AvCamUdeIsoReset(&pump->Stream);
    return AVCAM_UDE_PUMP_OK;
}

static __inline int
AvCamUdeFramePumpFill(
    AVCAM_UDE_FRAME_PUMP* pump,
    unsigned char* transfer,
    unsigned int capacity,
    const unsigned int* offsets,
    unsigned int packetCount,
    unsigned long long firstMicroframe100ns,
    unsigned int* packetBytes,
    unsigned int* returnedBytes)
{
    unsigned int i;
    unsigned int total = 0;
    const unsigned int zeroOffset = 0;

    if (returnedBytes == 0) return AVCAM_UDE_PUMP_INVALID;
    *returnedBytes = 0;
    if (pump == 0 || pump->Source == 0 || transfer == 0 ||
        offsets == 0 || packetBytes == 0 || packetCount == 0 ||
        packetCount > AVCAM_UDE_ISO_MAX_PACKETS || capacity == 0 ||
        firstMicroframe100ns > ULLONG_MAX -
            (unsigned long long)packetCount * AVCAM_UDE_ISO_MICROFRAME_100NS -
            (unsigned long long)AVCAM_UVC_FRAME_INTERVAL_100NS) {
        return AVCAM_UDE_PUMP_INVALID;
    }
    if (pump->Lease.Data == 0 && !pump->Source->HasFrame) {
        return AVCAM_UDE_PUMP_NO_FRAME;
    }

    /* Verify ALL host-supplied packet windows before touching any bytes,
     * stream state, packet descriptors, or frame leases.
     */
    for (i = 0; i < packetCount; ++i) {
        const unsigned int end = i + 1u < packetCount ?
            offsets[i + 1u] : capacity;
        if (!AvCamIsoPacketWindowValid(offsets[i], end, capacity)) {
            return AVCAM_UDE_PUMP_INVALID;
        }
    }

    for (i = 0; i < packetCount; ++i) {
        const unsigned int start = offsets[i];
        const unsigned int end = i + 1u < packetCount ?
            offsets[i + 1u] : capacity;
        const unsigned int window = end - start;
        const unsigned long long microframe =
            firstMicroframe100ns +
            (unsigned long long)i * AVCAM_UDE_ISO_MICROFRAME_100NS;
        unsigned int written = 0;
        unsigned int filled = 0;

        packetBytes[i] = 0;
        if (pump->Stream.Initialized &&
            pump->Stream.Packetizer.Offset == 0 &&
            microframe < pump->Stream.NextFrameStart100ns) {
            continue;
        }

        if (pump->Lease.Data == 0) {
            if (AvCamUdeFrameSourceAcquire(
                    pump->Source, &pump->Lease) != AVCAM_UDE_FRAME_OK) {
                /* A source may temporarily lack a new frame: zero ISO
                 * packets remain valid. Never manufacture a false EOF.
                 */
                continue;
            }
        }

        if (pump->Stream.Packetizer.Offset >= pump->Lease.Length ||
            AvCamUdeIsoFill(
                &pump->Stream, pump->Lease.Data, pump->Lease.Length,
                transfer + start, window, &zeroOffset, 1u, microframe,
                &written, &filled) != AVCAM_UDE_ISO_OK ||
            written > window || filled != written ||
            total > capacity - written) {
            /* A bad worker state invalidates the stream. The caller MUST
             * fail this URB and reset after stopping concurrent access.
             */
            return AVCAM_UDE_PUMP_INVALID;
        }
        packetBytes[i] = written;
        total += written;

        if (pump->Stream.Packetizer.Offset == 0) {
            /* EOF has been committed to the packet bytes; old source may
             * now be replaced. Next packet acquires the newest frame.
             */
            if (AvCamUdeFrameSourceRelease(
                    pump->Source, &pump->Lease) != AVCAM_UDE_FRAME_OK) {
                return AVCAM_UDE_PUMP_INVALID;
            }
        }
    }
    *returnedBytes = total;
    return AVCAM_UDE_PUMP_OK;
}
