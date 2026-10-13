#include "AvCamCommon.h"
#include "AvCamFilter.h"
#include "AvCamFrameStore.h"
#include "AvCamPin.h"
#include "../shared/VirtualCameraProtocol.h"

namespace
{
NTSTATUS AvCamSubmitFrameMethod(
    _In_ PIRP Irp,
    _In_ PKSIDENTIFIER Request,
    _Inout_ PVOID Data)
{
    if (Irp == nullptr || Request == nullptr || Data == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    const auto* method = reinterpret_cast<const KSMETHOD*>(Request);
    if (!IsEqualGUID(method->Set, KSMETHODSETID_AVCAM_FRAME_TRANSPORT) ||
        method->Id != AVCAM_METHOD_SUBMIT_FRAME)
    {
        return STATUS_NOT_SUPPORTED;
    }

    const PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    const ULONG requiredBytes =
        sizeof(AvCamFrameHeader) + AVCAM_FRAME_BYTES_BGRA32;

    if (stack->Parameters.DeviceIoControl.OutputBufferLength < requiredBytes)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    const auto* header = static_cast<const AvCamFrameHeader*>(Data);
    if (header->Version != AVCAM_PROTOCOL_VERSION ||
        header->Width != AVCAM_FRAME_WIDTH ||
        header->Height != AVCAM_FRAME_HEIGHT ||
        header->Stride != AVCAM_FRAME_STRIDE_BGRA32 ||
        header->PixelFormat != AVCAM_PIXEL_BGRA32 ||
        header->PayloadBytes != AVCAM_FRAME_BYTES_BGRA32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    const auto* payload =
        reinterpret_cast<const UCHAR*>(header) + sizeof(AvCamFrameHeader);

    const NTSTATUS status = AvCamSubmitFrame(
        payload,
        header->PayloadBytes,
        header->Timestamp100ns);

    Irp->IoStatus.Information = 0;
    return status;
}

DEFINE_KSMETHOD_TABLE(g_AvCamMethodItems)
{
    DEFINE_KSMETHOD_ITEM(
        AVCAM_METHOD_SUBMIT_FRAME,
        KSMETHOD_TYPE_SEND | KSMETHOD_TYPE_WRITE,
        AvCamSubmitFrameMethod,
        sizeof(KSMETHOD),
        sizeof(AvCamFrameHeader) + AVCAM_FRAME_BYTES_BGRA32,
        nullptr)
};

DEFINE_KSMETHOD_SET_TABLE(g_AvCamMethodSets)
{
    DEFINE_KSMETHOD_SET(
        &KSMETHODSETID_AVCAM_FRAME_TRANSPORT,
        RTL_NUMBER_OF(g_AvCamMethodItems),
        g_AvCamMethodItems,
        0,
        nullptr)
};

DEFINE_KSAUTOMATION_TABLE(g_AvCamFilterAutomation)
{
    DEFINE_KSAUTOMATION_PROPERTIES_NULL,
    DEFINE_KSAUTOMATION_METHODS(g_AvCamMethodSets),
    DEFINE_KSAUTOMATION_EVENTS_NULL
};
} // namespace

static const GUID g_AvCamFilterCategories[] =
{
    STATICGUIDOF(KSCATEGORY_VIDEO_CAMERA),
    STATICGUIDOF(KSCATEGORY_VIDEO),
    STATICGUIDOF(KSCATEGORY_CAPTURE)
};

const KSFILTER_DESCRIPTOR g_AvCamFilterDescriptor =
{
    nullptr,
    &g_AvCamFilterAutomation,
    KSFILTER_DESCRIPTOR_VERSION,
    0,
    nullptr,
    1,
    sizeof(KSPIN_DESCRIPTOR_EX),
    &g_AvCamCapturePinDescriptor,
    RTL_NUMBER_OF(g_AvCamFilterCategories),
    g_AvCamFilterCategories,
    0,
    0,
    nullptr,
    0,
    nullptr,
    nullptr
};
