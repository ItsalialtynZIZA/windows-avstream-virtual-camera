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
