#pragma once

#include "AvCamCommon.h"
#include "../shared/VirtualCameraProtocol.h"

NTSTATUS AvCamFrameStoreInitialize();
void AvCamFrameStoreShutdown();

NTSTATUS AvCamSubmitFrame(
    _In_reads_bytes_(FrameBytes) const UCHAR* FrameData,
    _In_ ULONG FrameBytes,
    _In_ ULONGLONG Timestamp100ns);

_Success_(return != FALSE)
BOOLEAN AvCamCopyLatestFrame(
    _Out_writes_bytes_(DestinationBytes) UCHAR* Destination,
    _In_ ULONG DestinationBytes,
    _Out_opt_ PULONGLONG Timestamp100ns);

void AvCamRecordFallbackFrame();
void AvCamGetTransportStats(_Out_ AvCamTransportStats* Stats);
