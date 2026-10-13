#include "AvCamFrameStore.h"
#include "../shared/VirtualCameraProtocol.h"

namespace
{
constexpr ULONG kAvCamFrameTag = 'FrCA';

struct AvCamFrameBlob
{
    volatile LONG RefCount;
    ULONG DataBytes;
    ULONGLONG Timestamp100ns;
    UCHAR Data[1];
};

KSPIN_LOCK g_FrameStoreLock;
AvCamFrameBlob* g_LatestFrame = nullptr;
BOOLEAN g_FrameStoreInitialized = FALSE;

void ReleaseFrame(_In_opt_ AvCamFrameBlob* Frame)
{
    if (Frame != nullptr && InterlockedDecrement(&Frame->RefCount) == 0)
    {
        ExFreePoolWithTag(Frame, kAvCamFrameTag);
    }
}

AvCamFrameBlob* AcquireLatestFrame()
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_FrameStoreLock, &oldIrql);

    AvCamFrameBlob* frame = g_LatestFrame;
    if (frame != nullptr)
    {
        InterlockedIncrement(&frame->RefCount);
    }

    KeReleaseSpinLock(&g_FrameStoreLock, oldIrql);
    return frame;
}
} // namespace

NTSTATUS AvCamFrameStoreInitialize()
{
    if (g_FrameStoreInitialized)
    {
        return STATUS_SUCCESS;
    }

    KeInitializeSpinLock(&g_FrameStoreLock);
    g_LatestFrame = nullptr;
    g_FrameStoreInitialized = TRUE;
    return STATUS_SUCCESS;
}

void AvCamFrameStoreShutdown()
{
    if (!g_FrameStoreInitialized)
    {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_FrameStoreLock, &oldIrql);
    AvCamFrameBlob* oldFrame = g_LatestFrame;
    g_LatestFrame = nullptr;
    KeReleaseSpinLock(&g_FrameStoreLock, oldIrql);

    ReleaseFrame(oldFrame);
    g_FrameStoreInitialized = FALSE;
}

NTSTATUS AvCamSubmitFrame(
    _In_reads_bytes_(FrameBytes) const UCHAR* FrameData,
    _In_ ULONG FrameBytes,
    _In_ ULONGLONG Timestamp100ns)
{
    if (!g_FrameStoreInitialized)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (FrameData == nullptr || FrameBytes != AVCAM_FRAME_BYTES_BGRA32)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    const SIZE_T allocationBytes =
        FIELD_OFFSET(AvCamFrameBlob, Data) + static_cast<SIZE_T>(FrameBytes);

    auto* newFrame = static_cast<AvCamFrameBlob*>(
        ExAllocatePool2(POOL_FLAG_NON_PAGED, allocationBytes, kAvCamFrameTag));

    if (newFrame == nullptr)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    newFrame->RefCount = 1; // global store owns one reference
    newFrame->DataBytes = FrameBytes;
    newFrame->Timestamp100ns = Timestamp100ns;
    RtlCopyMemory(newFrame->Data, FrameData, FrameBytes);

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_FrameStoreLock, &oldIrql);
    AvCamFrameBlob* oldFrame = g_LatestFrame;
    g_LatestFrame = newFrame;
    KeReleaseSpinLock(&g_FrameStoreLock, oldIrql);

    ReleaseFrame(oldFrame);
    return STATUS_SUCCESS;
}

BOOLEAN AvCamCopyLatestFrame(
    _Out_writes_bytes_(DestinationBytes) UCHAR* Destination,
    _In_ ULONG DestinationBytes,
    _Out_opt_ PULONGLONG Timestamp100ns)
{
    if (!g_FrameStoreInitialized || Destination == nullptr ||
        DestinationBytes < AVCAM_FRAME_BYTES_BGRA32)
    {
        return FALSE;
    }

    AvCamFrameBlob* frame = AcquireLatestFrame();
    if (frame == nullptr)
    {
        return FALSE;
    }

    RtlCopyMemory(Destination, frame->Data, frame->DataBytes);
    if (Timestamp100ns != nullptr)
    {
        *Timestamp100ns = frame->Timestamp100ns;
    }

    ReleaseFrame(frame);
    return TRUE;
}
