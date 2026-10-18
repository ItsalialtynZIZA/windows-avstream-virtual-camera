#pragma once
/*
 * Portable validation of an isochronous IN packet's buffer window.
 * The host supplies fixed offsets; short packets DO NOT compact later offsets.
 * This helper intentionally has no WDK dependencies so it can be fuzz-tested.
 */
#define AVCAM_ISO_MAX_MICROFRAME_BYTES 3072u

/* A sparse ISO layout keeps each original packet offset. Compare the
 * largest occupied byte offset with the sum of packet data lengths to
 * detect URBs whose occupied range exceeds TransferBufferLength's
 * returned-byte count. This is telemetry, not a layout rewrite. */
static __inline unsigned int AvCamIsoSparseExtentBytes(
    unsigned int highestOccupiedEnd,
    unsigned int returnedBytes
    )
{
    return highestOccupiedEnd > returnedBytes ?
        highestOccupiedEnd - returnedBytes : 0u;
}

static __inline int AvCamIsoPacketWindowValid(
    unsigned int start,
    unsigned int end,
    unsigned int transferCapacity
    )
{
    return start < end && end <= transferCapacity &&
           end - start > 2u &&
           end - start <= AVCAM_ISO_MAX_MICROFRAME_BYTES;
}
