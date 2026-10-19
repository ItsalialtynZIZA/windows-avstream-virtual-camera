#pragma once
/*
 * Experimental AvCam 1280x720 MJPEG USB/UVC compatibility profile.
 * Distinct research VID/PID and product name remain in place. This is NOT
 * the genuine Logitech device or its proprietary USB identity.
 *
 * Converts ONLY a copy of the existing, tested single-frame descriptor.
 * The legacy KMDF vHCI shared descriptor is never changed.
 */
#include <stddef.h>
#define AVCAM_UDE_C270_WIDTH 1280u
#define AVCAM_UDE_C270_HEIGHT 720u

static __inline int AvCamUdeBuildC270Configuration(
    const unsigned char* base, size_t length,
    unsigned char* output, size_t capacity)
{
    size_t pos;
    unsigned int frames = 0u;
    if (base == 0 || output == 0 || length < 9u || length > capacity ||
        (unsigned int)base[2] + ((unsigned int)base[3] << 8) != length) {
        return 0;
    }
    for (pos = 0; pos < length; ++pos) output[pos] = base[pos];
    pos = 0u;
    while (pos < length) {
        unsigned int part;
        if (length - pos < 2u) return 0;
        part = output[pos];
        if (part < 2u || part > length - pos) return 0;
        if (part >= 30u && output[pos + 1u] == 0x24u &&
            output[pos + 2u] == 0x07u) {
            /* UVC VS_FRAME_MJPEG: fields width/height at descriptor + 5/7. */
            if (frames != 0u) return 0;
            output[pos + 5u] = 0x00u;
            output[pos + 6u] = 0x05u; /* 1280 */
            output[pos + 7u] = 0xD0u;
            output[pos + 8u] = 0x02u; /* 720 */
            ++frames;
        }
        pos += part;
    }
    return frames == 1u;
}
