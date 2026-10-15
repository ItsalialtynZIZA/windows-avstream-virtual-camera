#pragma once

#include <winioctl.h>

#define FILE_DEVICE_AVCAM_BUS 0x00008010u

#define IOCTL_AVCAM_PLUG_DEVICE \
    CTL_CODE(FILE_DEVICE_AVCAM_BUS, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_AVCAM_UNPLUG_DEVICE \
    CTL_CODE(FILE_DEVICE_AVCAM_BUS, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_AVCAM_WRITE_FRAME \
    CTL_CODE(FILE_DEVICE_AVCAM_BUS, 0x802, METHOD_IN_DIRECT, FILE_WRITE_DATA)

#define AVCAM_CHILD_SERIAL 1u
#define AVCAM_MAX_FRAME_BYTES (8u * 1024u * 1024u)

typedef struct _AVCAM_FRAME_HEADER {
    unsigned long Width;
    unsigned long Height;
    unsigned long FourCC;
    unsigned long PayloadBytes;
    unsigned long long Timestamp100ns;
} AVCAM_FRAME_HEADER, *PAVCAM_FRAME_HEADER;
