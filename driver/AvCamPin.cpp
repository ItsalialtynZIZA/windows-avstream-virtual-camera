#include "AvCamCommon.h"
#include "AvCamPin.h"

namespace
{
constexpr LONG kWidth = 1280;
constexpr LONG kHeight = 720;
constexpr ULONG kBytesPerPixel = 4;
constexpr ULONG kStride = static_cast<ULONG>(kWidth) * kBytesPerPixel;
constexpr ULONG kFrameBytes = kStride * static_cast<ULONG>(kHeight);
constexpr REFERENCE_TIME kFrameInterval100ns = 333333; // ~30 fps

GUID g_AvCamCapturePinName = { STATIC_PINNAME_VIDEO_CAPTURE };
GUID g_AvCamCapturePinCategory =
{
    0xfb6c4281, 0x0353, 0x11d1,
    { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba }
};

const KSDATARANGE g_AvCamVideoRange =
{
    sizeof(KSDATARANGE),
    0,
    kFrameBytes,
    0,
    STATICGUIDOF(KSDATAFORMAT_TYPE_VIDEO),
    STATICGUIDOF(KSDATAFORMAT_SUBTYPE_RGB32),
    STATICGUIDOF(KSDATAFORMAT_SPECIFIER_VIDEOINFO)
};

const PKSDATARANGE g_AvCamVideoRanges[] =
{
    const_cast<PKSDATARANGE>(&g_AvCamVideoRange)
};

DECLARE_SIMPLE_FRAMING_EX(
    g_AvCamAllocatorFraming,
    STATICGUIDOF(KSMEMORY_TYPE_KERNEL_NONPAGED),
    KSALLOCATOR_REQUIREMENTF_SYSTEM_MEMORY | KSALLOCATOR_REQUIREMENTF_PREFERENCES_ONLY,
    2,
    0,
    kFrameBytes,
    kFrameBytes
);

NTSTATUS AvCamPinCreate(_In_ PKSPIN Pin, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Irp);

    if (Pin == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Pin->StreamHeaderSize = sizeof(KSSTREAM_HEADER);
    return STATUS_SUCCESS;
}

NTSTATUS AvCamPinSetState(
    _In_ PKSPIN Pin,
    _In_ KSSTATE ToState,
    _In_ KSSTATE FromState)
{
    UNREFERENCED_PARAMETER(Pin);
    UNREFERENCED_PARAMETER(ToState);
    UNREFERENCED_PARAMETER(FromState);
    return STATUS_SUCCESS;
}

void FillSyntheticFrame(_Out_writes_bytes_(kFrameBytes) UCHAR* destination)
{
    // Deterministic diagnostic pattern. Keeping synthesis in-kernel for M2
    // lets us prove AVStream negotiation and delivery before adding IPC/OBS.
    for (LONG y = 0; y < kHeight; ++y)
    {
        auto* row = destination + static_cast<ULONG>(y) * kStride;
        for (LONG x = 0; x < kWidth; ++x)
        {
            const ULONG offset = static_cast<ULONG>(x) * kBytesPerPixel;
            const UCHAR horizontal = static_cast<UCHAR>((x * 255) / (kWidth - 1));
            const UCHAR vertical = static_cast<UCHAR>((y * 255) / (kHeight - 1));
            const bool checker = (((x / 80) + (y / 80)) & 1) != 0;

            // Windows RGB32/DIB memory is B,G,R,X.
            row[offset + 0] = checker ? horizontal : 0x20;
            row[offset + 1] = vertical;
            row[offset + 2] = checker ? 0xE0 : horizontal;
            row[offset + 3] = 0xFF;
        }
    }
}

NTSTATUS AvCamPinProcess(_In_ PKSPIN Pin)
{
    if (Pin == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PKSSTREAM_POINTER leading =
        KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);

    if (leading == nullptr)
    {
        return STATUS_PENDING;
    }

    PKSSTREAM_HEADER header = leading->StreamHeader;
    if (header == nullptr || header->Data == nullptr || header->FrameExtent < kFrameBytes)
    {
        KsStreamPointerUnlock(leading, FALSE);
        return STATUS_BUFFER_TOO_SMALL;
    }

    FillSyntheticFrame(static_cast<UCHAR*>(header->Data));
    header->DataUsed = kFrameBytes;
    header->Duration = kFrameInterval100ns;
    header->OptionsFlags |= KSSTREAM_HEADER_OPTIONSF_DURATIONVALID;

    KsStreamPointerUnlock(leading, TRUE);
    return STATUS_PENDING;
}

NTSTATUS AvCamIntersectHandler(
    _In_ PKSFILTER Filter,
    _In_ PIRP Irp,
    _In_ PKSP_PIN PinInstance,
    _In_ PKSDATARANGE CallerDataRange,
    _In_ PKSDATARANGE DescriptorDataRange,
    _In_ ULONG BufferSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Data,
    _Out_ PULONG DataSize)
{
    UNREFERENCED_PARAMETER(Filter);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(PinInstance);
    UNREFERENCED_PARAMETER(CallerDataRange);
    UNREFERENCED_PARAMETER(DescriptorDataRange);

    if (DataSize == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    constexpr ULONG required = sizeof(KS_DATAFORMAT_VIDEOINFOHEADER);
    *DataSize = required;

    if (BufferSize == 0)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    if (Data == nullptr || BufferSize < required)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    auto* format = static_cast<PKS_DATAFORMAT_VIDEOINFOHEADER>(Data);
    RtlZeroMemory(format, required);

    format->DataFormat.FormatSize = required;
    format->DataFormat.Flags = 0;
    format->DataFormat.SampleSize = kFrameBytes;
    format->DataFormat.Reserved = 0;
    format->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_VIDEO;
    format->DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_RGB32;
    format->DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_VIDEOINFO;

    auto& vih = format->VideoInfoHeader;
    vih.rcSource.left = 0;
    vih.rcSource.top = 0;
    vih.rcSource.right = kWidth;
    vih.rcSource.bottom = kHeight;
    vih.rcTarget = vih.rcSource;
    vih.dwBitRate = kFrameBytes * 8u * 30u;
    vih.dwBitErrorRate = 0;
    vih.AvgTimePerFrame = kFrameInterval100ns;

    vih.bmiHeader.biSize = sizeof(KS_BITMAPINFOHEADER);
    vih.bmiHeader.biWidth = kWidth;
    vih.bmiHeader.biHeight = kHeight;
    vih.bmiHeader.biPlanes = 1;
    vih.bmiHeader.biBitCount = 32;
    vih.bmiHeader.biCompression = KS_BI_RGB;
    vih.bmiHeader.biSizeImage = kFrameBytes;
    vih.bmiHeader.biXPelsPerMeter = 0;
    vih.bmiHeader.biYPelsPerMeter = 0;
    vih.bmiHeader.biClrUsed = 0;
    vih.bmiHeader.biClrImportant = 0;

    return STATUS_SUCCESS;
}

const KSPIN_DISPATCH g_AvCamPinDispatch =
{
    AvCamPinCreate,
    nullptr,
    AvCamPinProcess,
    nullptr,
    nullptr,
    AvCamPinSetState,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};
} // namespace

const KSPIN_DESCRIPTOR_EX g_AvCamCapturePinDescriptor =
{
    &g_AvCamPinDispatch,
    nullptr,
    {
        0,
        nullptr,
        0,
        nullptr,
        RTL_NUMBER_OF(g_AvCamVideoRanges),
        g_AvCamVideoRanges,
        KSPIN_DATAFLOW_OUT,
        KSPIN_COMMUNICATION_BOTH,
        &g_AvCamCapturePinCategory,
        &g_AvCamCapturePinName,
        0
    },
    KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY |
        KSPIN_FLAG_INITIATE_PROCESSING_ON_EVERY_ARRIVAL,
    1,
    0,
    &g_AvCamAllocatorFraming,
    reinterpret_cast<PFNKSINTERSECTHANDLEREX>(AvCamIntersectHandler)
};
