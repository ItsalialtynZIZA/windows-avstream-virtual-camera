#pragma once

#include <guiddef.h>

// Shared identifier used by the user-mode feeder and the driver.
inline constexpr GUID GUID_DEVINTERFACE_AVCAM_FRAME_SINK =
{
    0x7b28304f, 0x6b89, 0x4df0,
    { 0x9e, 0x54, 0xb0, 0x48, 0xf4, 0xa1, 0x62, 0x13 }
};

// Private KS method set used on the AvCam filter handle for producer -> driver frames.
inline constexpr GUID KSMETHODSETID_AVCAM_FRAME_TRANSPORT =
{
    0xa0a34070, 0xd3e3, 0x4c90,
    { 0x93, 0x6e, 0x3d, 0x4d, 0xf9, 0x84, 0x5e, 0x51 }
};

constexpr unsigned long AVCAM_PROTOCOL_VERSION = 1;
constexpr unsigned long AVCAM_FRAME_WIDTH = 1280;
constexpr unsigned long AVCAM_FRAME_HEIGHT = 720;
constexpr unsigned long AVCAM_FRAME_STRIDE_BGRA32 = AVCAM_FRAME_WIDTH * 4;
constexpr unsigned long AVCAM_FRAME_BYTES_BGRA32 = AVCAM_FRAME_STRIDE_BGRA32 * AVCAM_FRAME_HEIGHT;

enum : unsigned long
{
    AVCAM_METHOD_SUBMIT_FRAME = 1,
};

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
