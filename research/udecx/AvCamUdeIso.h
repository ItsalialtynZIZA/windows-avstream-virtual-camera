#pragma once
/*
 * Portable UVC MJPEG -> ISO packet-window encoder.
 *
 * This deliberately does NOT complete USB requests. The eventual UdeCx
 * adapter must validate the actual URB header/packet-descriptor array,
 * map the transfer buffer at PASSIVE_LEVEL, and complete asynchronously
 * on a separate DPC as required by UdeCx. This pure helper is independent
 * of WDF, IRQL, cancellation, and endpoint lifetime.
 */
#include <limits.h>
#include "../manual-vhci/shared/AvCamUvcPayload.h"
#include "../manual-vhci/shared/AvCamIsoPacketLayout.h"

#define AVCAM_UDE_ISO_OK 0
#define AVCAM_UDE_ISO_INVALID 1
#define AVCAM_UDE_ISO_PACKET_ERROR 2
#define AVCAM_UDE_ISO_MAX_PACKETS 1024u
#define AVCAM_UDE_ISO_MICROFRAME_100NS 1250ull

typedef struct _AVCAM_UDE_ISO_STREAM {
    AVCAM_UVC_PACKETIZER Packetizer;
    unsigned long long NextFrameStart100ns;
    unsigned char Initialized;
} AVCAM_UDE_ISO_STREAM;

static __inline void
AvCamUdeIsoReset(AVCAM_UDE_ISO_STREAM* stream)
{
    if (stream == 0) {
        return;
    }
    AvCamUvcPacketizerReset(&stream->Packetizer);
    stream->NextFrameStart100ns = 0;
    stream->Initialized = 0;
}

/*
 * All offsets are supplied by the USB host and MUST remain fixed.
 * packetBytes[i] is the actual length written into the original packet
 * window. It is zero when the 30fps scheduler intentionally leaves a
 * microframe empty.
 *
 * frame must stay immutable for the entire duration of each JPEG frame.
 * There is no user-mode frame feeder or USB completion in this helper.
 */
static __inline int
AvCamUdeIsoFill(
    AVCAM_UDE_ISO_STREAM* stream,
    const unsigned char* frame,
    unsigned int frameBytes,
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
    AVCAM_UDE_ISO_STREAM state;

    if (returnedBytes == 0) {
        return AVCAM_UDE_ISO_INVALID;
    }
    *returnedBytes = 0;

    if (stream == 0 || frame == 0 || transfer == 0 ||
        offsets == 0 || packetBytes == 0 ||
        frameBytes == 0 || frameBytes > AVCAM_UVC_FRAME_MAX_BYTES ||
        packetCount == 0 || packetCount > AVCAM_UDE_ISO_MAX_PACKETS ||
        capacity == 0 || stream->Packetizer.Offset >= frameBytes) {
        return AVCAM_UDE_ISO_INVALID;
    }

    /* Reject time overflow before modifying caller buffers or state. */
    if (firstMicroframe100ns >
        ULLONG_MAX - (unsigned long long)packetCount *
            AVCAM_UDE_ISO_MICROFRAME_100NS -
            (unsigned long long)AVCAM_UVC_FRAME_INTERVAL_100NS) {
        return AVCAM_UDE_ISO_INVALID;
    }

    /* Validate every host-provided window before the first byte is written.
     * A malformed later packet must not partially mutate the stream.
     */
    for (i = 0; i < packetCount; ++i) {
        unsigned int start = offsets[i];
        unsigned int end = i + 1u < packetCount ?
            offsets[i + 1u] : capacity;

        if (!AvCamIsoPacketWindowValid(start, end, capacity)) {
            return AVCAM_UDE_ISO_INVALID;
        }
    }

    state = *stream;

    for (i = 0; i < packetCount; ++i) {
        const unsigned int start = offsets[i];
        const unsigned int end = i + 1u < packetCount ?
            offsets[i + 1u] : capacity;
        const unsigned int window = end - start;
        const unsigned long long slotTime =
            firstMicroframe100ns + (unsigned long long)i *
            AVCAM_UDE_ISO_MICROFRAME_100NS;
        unsigned int bounded;
        unsigned int wrote = 0;

        packetBytes[i] = 0;
        if (!state.Initialized) {
            state.NextFrameStart100ns = slotTime;
            state.Initialized = 1;
        }

        if (state.Packetizer.Offset == 0) {
            if (slotTime < state.NextFrameStart100ns) {
                continue;
            }
            /* Schedule frame STARTS at 30fps rather than waiting for
             * another entire frame interval after each EOF.
             */
            state.NextFrameStart100ns = slotTime +
                (unsigned long long)AVCAM_UVC_FRAME_INTERVAL_100NS;
        }

        bounded = AvCamUvcPacedPacketCapacity(
            frameBytes, window, AVCAM_UVC_FRAME_INTERVAL_100NS,
            (unsigned int)AVCAM_UDE_ISO_MICROFRAME_100NS);

        if (AvCamUvcPacketizeMjpeg(
                &state.Packetizer, frame, frameBytes,
                transfer + start, bounded, &wrote) !=
                AVCAM_UVC_PACKET_OK ||
            wrote == 0 || wrote > window ||
            wrote > capacity - start) {
            return AVCAM_UDE_ISO_PACKET_ERROR;
        }

        if (total > capacity - wrote) {
            return AVCAM_UDE_ISO_PACKET_ERROR;
        }
        total += wrote;
        packetBytes[i] = wrote;
    }

    *stream = state;
    *returnedBytes = total;
    return AVCAM_UDE_ISO_OK;
}
