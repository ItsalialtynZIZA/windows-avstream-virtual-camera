#pragma once

#include "AvCamCommon.h"

NTSTATUS AvCamFrameStoreInitialize();
void AvCamFrameStoreShutdown();

NTSTATUS AvCamSubmitFrame(
    _In_reads_bytes_(FrameBytes) const UCHAR* FrameData,
    _In_ ULONG FrameBytes,
    _In_ ULONGLONG Timestamp100ns);

BOOLEAN AvCamCopyLatestFrame(
    _Out_writes_bytes_(DestinationBytes) UCHAR* Destination,
    _In_ ULONG DestinationBytes,
    _Out_opt_ PULONGLONG Timestamp100ns);
