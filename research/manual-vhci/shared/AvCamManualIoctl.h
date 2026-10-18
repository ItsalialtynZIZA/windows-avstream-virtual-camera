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

#define IOCTL_AVCAM_READ_STREAM_STATS \
    CTL_CODE(FILE_DEVICE_AVCAM_BUS, 0x804, METHOD_BUFFERED, FILE_READ_DATA)
#define AVCAM_STREAM_STATS_VERSION 4u

typedef struct _AVCAM_STREAM_STATS {
    unsigned long Version;
    unsigned long FrameWrites;
    unsigned long LastAcceptedFrameBytes;
    unsigned long FrameSwaps;
    unsigned long ActiveFrameBytes;
    unsigned long PendingReady;
    unsigned long IsoUrbsCompleted;
    unsigned long IsoUrbsWithData;
    unsigned long IsoUrbsEmpty;
    unsigned long IsoPacketsWithData;
    unsigned long MjpegFramesCompleted;
    unsigned long LastIsoUrbBytes;
    unsigned long LastIsoUrbPackets;
    unsigned long long TotalIsoPayloadBytes;
    unsigned long PortStatusQueries;
    unsigned long PortStatusFailures;
    unsigned long LastPortStatusFlags;
    /* Version 3: instrument failures and timing without changing ISO behavior. */
    unsigned long IsoUrbsRejected;
    unsigned long IsoRejectedPacketLayout;
    unsigned long IsoRejectedMdlBounds;
    unsigned long LastIsoRejectReason;
    unsigned long LastIsoRequestedBytes;
    unsigned long LastIsoReturnedBytes;
    unsigned long LastIsoElapsed100ns;
    unsigned long MaxIsoElapsed100ns;
    unsigned long UvcCommitCount;
    unsigned long SelectAlt0Count;
    unsigned long SelectAlt1Count;
    /* Version 4: sparse ISO buffer extents and USB frame start telemetry. */
    unsigned long IsoUrbsWithSparseExtent;
    unsigned long IsoUrbsWithLeadingEmptyPackets;
    unsigned long LastIsoFirstFilledPacket;
    unsigned long LastIsoHighestOccupiedEnd;
    unsigned long LastIsoSparseExtentBytes;
    unsigned long MaxIsoSparseExtentBytes;
    unsigned long LastIsoStartFrame;
    unsigned long IsoAsapUrbsCompleted;
} AVCAM_STREAM_STATS, *PAVCAM_STREAM_STATS;

/* ISO rejection reasons exposed by the versioned statistics IOCTL. */
#define AVCAM_ISO_REJECT_SHORT_URB 1u
#define AVCAM_ISO_REJECT_INVALID_STATE 2u
#define AVCAM_ISO_REJECT_PACKET_LAYOUT 3u
#define AVCAM_ISO_REJECT_MDL_BOUNDS 4u
#define AVCAM_ISO_REJECT_NO_BUFFER 5u

#define AVCAM_DIAG_TRACE_CAPACITY 128u
#define AVCAM_DIAG_EVENT_INTERNAL_IOCTL 1u
#define AVCAM_DIAG_EVENT_URB 2u
#define AVCAM_DIAG_EVENT_QUERY_INTERFACE 3u
#define AVCAM_DIAG_EVENT_QUERY_INTERFACE_DETAILS 4u
#define AVCAM_DIAG_EVENT_USB_BUS_CALLBACK 5u
#define AVCAM_DIAG_EVENT_UVC_CONTROL 6u
#define AVCAM_DIAG_EVENT_UVC_REQUEST_LENGTH 7u
#define AVCAM_DIAG_EVENT_ISOCH_REQUEST 8u
#define AVCAM_DIAG_EVENT_ISOCH_FLAGS 9u
#define AVCAM_DIAG_EVENT_ISOCH_STATE 10u
#define AVCAM_DIAG_EVENT_ISOCH_OFFSETS 11u
#define AVCAM_DIAG_EVENT_ISOCH_COMPLETION 12u
#define AVCAM_DIAG_EVENT_FRAME_WRITE 13u
#define AVCAM_DIAG_EVENT_PORT_STATUS 14u

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
