#include "AvCamCommon.h"
#include "AvCamPin.h"

namespace
{
constexpr LONG kWidth = 1280;
constexpr LONG kHeight = 720;
constexpr ULONG kBytesPerPixel = 4;
constexpr ULONG kStride = static_cast<ULONG>(kWidth) * kBytesPerPixel;
constexpr ULONG kFrameBytes = kStride * static_cast<ULONG>(kHeight);
constexpr ULONG kFramesPerSecond = 30;
constexpr ULONG kBitRate = kFrameBytes * 8u * kFramesPerSecond;
constexpr REFERENCE_TIME kFrameInterval100ns = 333333; // ~30 fps

GUID g_AvCamCapturePinName = { STATIC_PINNAME_VIDEO_CAPTURE };
GUID g_AvCamCapturePinCategory =
{
    0xfb6c4281, 0x0353, 0x11d1,
    { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba }
};

// Advertise one exact camera mode. This follows the KS_DATARANGE_VIDEO shape
// used by Microsoft's AVStream camera samples, but intentionally keeps the
// capabilities deterministic for the first working prototype.
const KS_DATARANGE_VIDEO g_AvCamVideoRange =
{
    {
        sizeof(KS_DATARANGE_VIDEO),
        0,
        kFrameBytes,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_VIDEO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_RGB32),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_VIDEOINFO)
    },
    TRUE,
    FALSE,
    0,
    0,
    {
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_VIDEOINFO),
        KS_AnalogVideo_None,
        kWidth, kHeight,
        kWidth, kHeight,
        kWidth, kHeight,
        1,
        1,
        1,
        1,
        kWidth, kHeight,
        kWidth, kHeight,
        1,
        1,
        0,
        0,
        0,
        0,
        kFrameInterval100ns,
        kFrameInterval100ns,
        kBitRate,
        kBitRate
    },
    {
        0, 0, 0, 0,
        0, 0, 0, 0,
        kBitRate,
        0,
        kFrameInterval100ns,
        sizeof(KS_BITMAPINFOHEADER),
        kWidth,
        kHeight,
        1,
        32,
        KS_BI_RGB,
        kFrameBytes,
        0,
        0,
        0,
        0
    }
};

const PKSDATARANGE g_AvCamVideoRanges[] =
{
    const_cast<PKSDATARANGE>(&g_AvCamVideoRange.DataRange)
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

bool IsExactAvCamFormat(_In_ const KS_DATAFORMAT_VIDEOINFOHEADER* format)
{
    if (format == nullptr)
    {
        return false;
    }

    const auto& dataFormat = format->DataFormat;
    const auto& vih = format->VideoInfoHeader;
    const auto& bmi = vih.bmiHeader;

    return
        IsEqualGUID(dataFormat.MajorFormat, KSDATAFORMAT_TYPE_VIDEO) &&
        IsEqualGUID(dataFormat.SubFormat, KSDATAFORMAT_SUBTYPE_RGB32) &&
        IsEqualGUID(dataFormat.Specifier, KSDATAFORMAT_SPECIFIER_VIDEOINFO) &&
        bmi.biSize >= sizeof(KS_BITMAPINFOHEADER) &&
        bmi.biWidth == kWidth &&
        bmi.biHeight == kHeight &&
        bmi.biPlanes == 1 &&
        bmi.biBitCount == 32 &&
        bmi.biCompression == KS_BI_RGB &&
        bmi.biSizeImage >= kFrameBytes &&
        vih.AvgTimePerFrame == kFrameInterval100ns;
}

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

NTSTATUS AvCamPinSetDataFormat(
    _In_ PKSPIN Pin,
    _In_opt_ PKSDATAFORMAT OldFormat,
    _In_opt_ PKSMULTIPLE_ITEM OldAttributeList,
    _In_ const KSDATARANGE* DataRange,
    _In_opt_ const KSATTRIBUTE_LIST* AttributeRange)
{
    UNREFERENCED_PARAMETER(OldAttributeList);
    UNREFERENCED_PARAMETER(AttributeRange);

    if (Pin == nullptr || DataRange == nullptr || Pin->ConnectionFormat == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (OldFormat != nullptr && Pin->DeviceState != KSSTATE_STOP)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (DataRange->FormatSize < sizeof(KS_DATARANGE_VIDEO) ||
        Pin->ConnectionFormat->FormatSize < sizeof(KS_DATAFORMAT_VIDEOINFOHEADER))
    {
        return STATUS_NO_MATCH;
    }

    const auto* format =
        reinterpret_cast<const KS_DATAFORMAT_VIDEOINFOHEADER*>(Pin->ConnectionFormat);

    return IsExactAvCamFormat(format) ? STATUS_SUCCESS : STATUS_NO_MATCH;
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
    for (LONG y = 0; y < kHeight; ++y)
    {
        auto* row = destination + static_cast<ULONG>(y) * kStride;
        for (LONG x = 0; x < kWidth; ++x)
        {
            const ULONG offset = static_cast<ULONG>(x) * kBytesPerPixel;
            const UCHAR horizontal = static_cast<UCHAR>((x * 255) / (kWidth - 1));
            const UCHAR vertical = static_cast<UCHAR>((y * 255) / (kHeight - 1));
            const bool checker = (((x / 80) + (y / 80)) & 1) != 0;

            // Windows RGB32/DIB memory order is B,G,R,X.
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

    if (CallerDataRange == nullptr || DescriptorDataRange == nullptr || DataSize == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (CallerDataRange->FormatSize < sizeof(KS_DATARANGE_VIDEO) ||
        DescriptorDataRange->FormatSize < sizeof(KS_DATARANGE_VIDEO))
    {
        return STATUS_NO_MATCH;
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

    const auto* descriptorVideo =
        reinterpret_cast<const KS_DATARANGE_VIDEO*>(DescriptorDataRange);
    auto* format = static_cast<PKS_DATAFORMAT_VIDEOINFOHEADER>(Data);

    RtlZeroMemory(format, required);
    RtlCopyMemory(&format->DataFormat, &descriptorVideo->DataRange, sizeof(KSDATAFORMAT));
    format->DataFormat.FormatSize = required;
    format->DataFormat.SampleSize = kFrameBytes;
    format->VideoInfoHeader = descriptorVideo->VideoInfoHeader;

    return STATUS_SUCCESS;
}

const KSPIN_DISPATCH g_AvCamPinDispatch =
{
    AvCamPinCreate,
    nullptr,
    AvCamPinProcess,
    nullptr,
    AvCamPinSetDataFormat,
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
