#pragma once

#ifdef _KERNEL_MODE
#include <devioctl.h>
#else
#include <Windows.h>
#include <winioctl.h>
#endif

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


#define IOCTL_AVCAM_READ_DIAGNOSTICS \
    CTL_CODE(FILE_DEVICE_AVCAM_BUS, 0x803, METHOD_BUFFERED, FILE_READ_DATA)

#define AVCAM_DIAG_TRACE_CAPACITY 128u
#define AVCAM_DIAG_EVENT_INTERNAL_IOCTL 1u
#define AVCAM_DIAG_EVENT_URB 2u
#define AVCAM_DIAG_EVENT_QUERY_INTERFACE 3u

typedef struct _AVCAM_DIAG_EVENT {
    unsigned long Sequence;
    unsigned long EventType;
    unsigned long Code;
    long Status;
} AVCAM_DIAG_EVENT, *PAVCAM_DIAG_EVENT;

typedef struct _AVCAM_DIAG_BUFFER {
    unsigned long Count;
    unsigned long TotalEvents;
    AVCAM_DIAG_EVENT Events[AVCAM_DIAG_TRACE_CAPACITY];
} AVCAM_DIAG_BUFFER, *PAVCAM_DIAG_BUFFER;
