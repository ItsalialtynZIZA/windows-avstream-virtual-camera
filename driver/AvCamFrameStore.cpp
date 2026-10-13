#include "AvCamFrameStore.h"

namespace
{
constexpr ULONG kAvCamFrameTag = 'FrCA';
constexpr ULONG kFrameSlotCount = 3;
constexpr ULONG kInvalidSlot = MAXULONG;

struct AvCamFrameSlot
{
    UCHAR* Data;
    ULONGLONG Timestamp100ns;
    LONG Readers;
    BOOLEAN Valid;
    BOOLEAN Writing;
};

KSPIN_LOCK g_FrameStoreLock;
EX_RUNDOWN_REF g_FrameStoreRundown;
AvCamFrameSlot g_FrameSlots[kFrameSlotCount]{};
ULONG g_LatestSlot = kInvalidSlot;
volatile LONG g_FrameStoreInitialized = 0;

volatile LONG64 g_SubmittedFrames = 0;
volatile LONG64 g_ReplacedFrames = 0;
volatile LONG64 g_DroppedFrames = 0;
volatile LONG64 g_CaptureReads = 0;
volatile LONG64 g_FallbackFrames = 0;

ULONGLONG ReadCounter(_In_ volatile LONG64* Counter)
{
    return static_cast<ULONGLONG>(
        InterlockedCompareExchange64(Counter, 0, 0));
}

BOOLEAN IsFrameStoreInitialized()
{
    return InterlockedCompareExchange(&g_FrameStoreInitialized, 0, 0) != 0;
}

void ResetSlotMetadata(_Inout_ AvCamFrameSlot& Slot)
{
    Slot.Timestamp100ns = 0;
    Slot.Readers = 0;
    Slot.Valid = FALSE;
    Slot.Writing = FALSE;
}
} // namespace

NTSTATUS AvCamFrameStoreInitialize()
{
    if (IsFrameStoreInitialized())
    {
        return STATUS_SUCCESS;
    }

    KeInitializeSpinLock(&g_FrameStoreLock);
    ExInitializeRundownProtection(&g_FrameStoreRundown);
    g_LatestSlot = kInvalidSlot;

    for (ULONG index = 0; index < kFrameSlotCount; ++index)
    {
        ResetSlotMetadata(g_FrameSlots[index]);
        g_FrameSlots[index].Data = static_cast<UCHAR*>(
            ExAllocatePool2(
                POOL_FLAG_NON_PAGED,
                AVCAM_FRAME_BYTES_BGRA32,
                kAvCamFrameTag));

        if (g_FrameSlots[index].Data == nullptr)
        {
            for (ULONG cleanupIndex = 0; cleanupIndex < index; ++cleanupIndex)
            {
                ExFreePoolWithTag(g_FrameSlots[cleanupIndex].Data, kAvCamFrameTag);
                g_FrameSlots[cleanupIndex].Data = nullptr;
            }
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    InterlockedExchange64(&g_SubmittedFrames, 0);
    InterlockedExchange64(&g_ReplacedFrames, 0);
    InterlockedExchange64(&g_DroppedFrames, 0);
    InterlockedExchange64(&g_CaptureReads, 0);
    InterlockedExchange64(&g_FallbackFrames, 0);
    InterlockedExchange(&g_FrameStoreInitialized, 1);
    return STATUS_SUCCESS;
}

void AvCamFrameStoreShutdown()
{
    if (InterlockedExchange(&g_FrameStoreInitialized, 0) == 0)
    {
        return;
    }

    // Prevent teardown while a submit/capture copy is using one of the fixed slots.
    ExWaitForRundownProtectionRelease(&g_FrameStoreRundown);

    g_LatestSlot = kInvalidSlot;
    for (ULONG index = 0; index < kFrameSlotCount; ++index)
    {
        if (g_FrameSlots[index].Data != nullptr)
        {
            ExFreePoolWithTag(g_FrameSlots[index].Data, kAvCamFrameTag);
            g_FrameSlots[index].Data = nullptr;
        }
        ResetSlotMetadata(g_FrameSlots[index]);
    }
}

NTSTATUS AvCamSubmitFrame(
    _In_reads_bytes_(FrameBytes) const UCHAR* FrameData,
    _In_ ULONG FrameBytes,
    _In_ ULONGLONG Timestamp100ns)
{
    if (!IsFrameStoreInitialized())
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (FrameData == nullptr || FrameBytes != AVCAM_FRAME_BYTES_BGRA32)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    if (!ExAcquireRundownProtection(&g_FrameStoreRundown))
    {
        return STATUS_DEVICE_NOT_READY;
    }

    ULONG writeSlot = kInvalidSlot;
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_FrameStoreLock, &oldIrql);

    for (ULONG index = 0; index < kFrameSlotCount; ++index)
    {
        AvCamFrameSlot& slot = g_FrameSlots[index];
        if (index != g_LatestSlot && !slot.Writing && slot.Readers == 0)
        {
            slot.Writing = TRUE;
            writeSlot = index;
            break;
        }
    }

    KeReleaseSpinLock(&g_FrameStoreLock, oldIrql);

    if (writeSlot == kInvalidSlot)
    {
        InterlockedIncrement64(&g_DroppedFrames);
        ExReleaseRundownProtection(&g_FrameStoreRundown);
        // Dropping is intentional back-pressure, not a transport disconnect.
        return STATUS_SUCCESS;
    }

    AvCamFrameSlot& target = g_FrameSlots[writeSlot];
    RtlCopyMemory(target.Data, FrameData, AVCAM_FRAME_BYTES_BGRA32);

    KeAcquireSpinLock(&g_FrameStoreLock, &oldIrql);
    const BOOLEAN replacedExisting =
        g_LatestSlot != kInvalidSlot && g_FrameSlots[g_LatestSlot].Valid;

    target.Timestamp100ns = Timestamp100ns;
    target.Valid = TRUE;
    target.Writing = FALSE;
    g_LatestSlot = writeSlot;
    KeReleaseSpinLock(&g_FrameStoreLock, oldIrql);

    InterlockedIncrement64(&g_SubmittedFrames);
    if (replacedExisting)
    {
        InterlockedIncrement64(&g_ReplacedFrames);
    }

    ExReleaseRundownProtection(&g_FrameStoreRundown);
    return STATUS_SUCCESS;
}

_Success_(return != FALSE)
BOOLEAN AvCamCopyLatestFrame(
    _Out_writes_bytes_(DestinationBytes) UCHAR* Destination,
    _In_ ULONG DestinationBytes,
    _Out_opt_ PULONGLONG Timestamp100ns)
{
    if (Timestamp100ns != nullptr)
    {
        *Timestamp100ns = 0;
    }

    if (!IsFrameStoreInitialized() || Destination == nullptr ||
        DestinationBytes < AVCAM_FRAME_BYTES_BGRA32)
    {
        return FALSE;
    }

    if (!ExAcquireRundownProtection(&g_FrameStoreRundown))
    {
        return FALSE;
    }

    ULONG readSlot = kInvalidSlot;
    ULONGLONG timestamp = 0;

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_FrameStoreLock, &oldIrql);

    if (g_LatestSlot != kInvalidSlot)
    {
        AvCamFrameSlot& latest = g_FrameSlots[g_LatestSlot];
        if (latest.Valid && !latest.Writing && latest.Data != nullptr)
        {
            ++latest.Readers;
            readSlot = g_LatestSlot;
            timestamp = latest.Timestamp100ns;
        }
    }

    KeReleaseSpinLock(&g_FrameStoreLock, oldIrql);

    if (readSlot == kInvalidSlot)
    {
        ExReleaseRundownProtection(&g_FrameStoreRundown);
        return FALSE;
    }

    RtlCopyMemory(
        Destination,
        g_FrameSlots[readSlot].Data,
        AVCAM_FRAME_BYTES_BGRA32);

    KeAcquireSpinLock(&g_FrameStoreLock, &oldIrql);
    --g_FrameSlots[readSlot].Readers;
    KeReleaseSpinLock(&g_FrameStoreLock, oldIrql);

    if (Timestamp100ns != nullptr)
    {
        *Timestamp100ns = timestamp;
    }

    InterlockedIncrement64(&g_CaptureReads);
    ExReleaseRundownProtection(&g_FrameStoreRundown);
    return TRUE;
}

void AvCamRecordFallbackFrame()
{
    InterlockedIncrement64(&g_FallbackFrames);
}

void AvCamGetTransportStats(_Out_ AvCamTransportStats* Stats)
{
    if (Stats == nullptr)
    {
        return;
    }

    RtlZeroMemory(Stats, sizeof(*Stats));
    Stats->Version = AVCAM_PROTOCOL_VERSION;
    Stats->SubmittedFrames = ReadCounter(&g_SubmittedFrames);
    Stats->ReplacedFrames = ReadCounter(&g_ReplacedFrames);
    Stats->DroppedFrames = ReadCounter(&g_DroppedFrames);
    Stats->CaptureReads = ReadCounter(&g_CaptureReads);
    Stats->FallbackFrames = ReadCounter(&g_FallbackFrames);
}
