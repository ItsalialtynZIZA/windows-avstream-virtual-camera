#pragma once

#include <guiddef.h>

// Shared identifier used by the user-mode feeder and the driver.
inline constexpr GUID GUID_DEVINTERFACE_AVCAM_FRAME_SINK =
{
    0x7b28304f, 0x6b89, 0x4df0,
    { 0x9e, 0x54, 0xb0, 0x48, 0xf4, 0xa1, 0x62, 0x13 }
};

constexpr unsigned long AVCAM_PROTOCOL_VERSION = 1;

struct AvCamFrameHeader
{
    unsigned long Version;
    unsigned long Width;
    unsigned long Height;
    unsigned long Stride;
    unsigned long PixelFormat;
    unsigned long long Timestamp100ns;
    unsigned long PayloadBytes;
};

enum : unsigned long
{
    AVCAM_PIXEL_BGRA32 = 1,
    AVCAM_PIXEL_NV12   = 2,
};
