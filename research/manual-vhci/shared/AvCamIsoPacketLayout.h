#pragma once
/*
 * Portable validation of an isochronous IN packet's buffer window.
 * The host supplies fixed offsets; short packets DO NOT compact later offsets.
 * This helper intentionally has no WDK dependencies so it can be fuzz-tested.
 */
#define AVCAM_ISO_MAX_MICROFRAME_BYTES 3072u

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
