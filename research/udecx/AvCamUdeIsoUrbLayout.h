#pragma once
/*
 * Portable preflight for a Windows USB 2.0 high-speed isochronous URB.
 * The kernel adapter separately checks URB_FUNCTION_ISOCH_TRANSFER,
 * direction flags, and each host-provided IsoPacket offset.
 *
 * Never trust a header's claimed packet count without validating that
 * its declared variable-length descriptor array contains every element.
 */
#include "AvCamUdeIso.h"

#define AVCAM_UDE_ISO_URB_VALID 0
#define AVCAM_UDE_ISO_URB_INVALID 1

static __inline int
AvCamUdeIsoUrbHeaderValid(
    unsigned int headerBytes,
    unsigned int descriptorPrefixBytes,
    unsigned int descriptorElementBytes,
    unsigned int packetCount,
    unsigned int urbTransferBytes,
    unsigned int mappedBufferBytes,
    int isInTransfer)
{
    if (!isInTransfer ||
        packetCount == 0 || packetCount > AVCAM_UDE_ISO_MAX_PACKETS ||
        descriptorElementBytes == 0 ||
        descriptorPrefixBytes > headerBytes ||
        packetCount > (headerBytes - descriptorPrefixBytes) /
            descriptorElementBytes ||
        urbTransferBytes == 0 ||
        urbTransferBytes > mappedBufferBytes) {
        return AVCAM_UDE_ISO_URB_INVALID;
    }
    return AVCAM_UDE_ISO_URB_VALID;
}

static __inline int
AvCamUdeIsoUrbOffsetValid(
    unsigned int packetStart,
    unsigned int followingPacketStart,
    unsigned int transferBytes)
{
    return AvCamIsoPacketWindowValid(
        packetStart, followingPacketStart, transferBytes) ?
        AVCAM_UDE_ISO_URB_VALID : AVCAM_UDE_ISO_URB_INVALID;
}

/* Completion metadata must preserve every host-supplied sparse offset.
 * Actual data size is the SUM of each ISO IN packet Length, never the
 * highest occupied offset. The original TransferBufferLength bounds every
 * packet window, even though a future completion helper may report a
 * smaller actual byte count to UdeCx.
 */
static __inline int
AvCamUdeIsoCompletionPacketValid(
    unsigned int start,
    unsigned int end,
    unsigned int capacity,
    unsigned int actualLength)
{
    return AvCamUdeIsoUrbOffsetValid(start, end, capacity) ==
               AVCAM_UDE_ISO_URB_VALID &&
           actualLength <= end - start ?
        AVCAM_UDE_ISO_URB_VALID : AVCAM_UDE_ISO_URB_INVALID;
}

static __inline int
AvCamUdeIsoCompletionValid(
    const unsigned int* offsets,
    const unsigned int* actualLengths,
    unsigned int packetCount,
    unsigned int transferCapacity,
    unsigned int reportedBytes)
{
    unsigned int i;
    unsigned int sum = 0;
    if (offsets == 0 || actualLengths == 0 ||
        packetCount == 0 || packetCount > AVCAM_UDE_ISO_MAX_PACKETS ||
        transferCapacity == 0 || reportedBytes > transferCapacity) {
        return AVCAM_UDE_ISO_URB_INVALID;
    }
    for (i = 0; i < packetCount; ++i) {
        const unsigned int end = i + 1u < packetCount ?
            offsets[i + 1u] : transferCapacity;
        const unsigned int bytes = actualLengths[i];
        if (AvCamUdeIsoCompletionPacketValid(
                offsets[i], end, transferCapacity, bytes) !=
                AVCAM_UDE_ISO_URB_VALID ||
            bytes > reportedBytes - sum) {
            return AVCAM_UDE_ISO_URB_INVALID;
        }
        sum += bytes;
    }
    return sum == reportedBytes ?
        AVCAM_UDE_ISO_URB_VALID : AVCAM_UDE_ISO_URB_INVALID;
}
