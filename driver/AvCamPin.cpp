#include "AvCamCommon.h"
#include "AvCamPin.h"

namespace
{
constexpr LONG kWidth = 1280;
constexpr LONG kHeight = 720;
constexpr ULONG kBytesPerPixel = 4;
constexpr ULONG kFrameBytes = static_cast<ULONG>(kWidth * kHeight * kBytesPerPixel);
constexpr REFERENCE_TIME kFrameInterval100ns = 333333; // ~30 fps

GUID g_AvCamCapturePinName = { STATIC_PINNAME_VIDEO_CAPTURE };

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
} // namespace

const KSPIN_DESCRIPTOR_EX g_AvCamCapturePinDescriptor =
{
    nullptr,
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
        &PIN_CATEGORY_CAPTURE,
        &g_AvCamCapturePinName,
        0
    },
    KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY,
    1,
    0,
    nullptr,
    reinterpret_cast<PFNKSINTERSECTHANDLEREX>(AvCamIntersectHandler)
};
